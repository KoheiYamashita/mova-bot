#include <Arduino.h>

#include "motor_controller.h"
#include "obstacle_sensor.h"

namespace mova {

// ── Speed mapping: API 0-4095 → M138 0-127 ─────────────────────
int8_t MotorController::mapSpeedToDuty(uint16_t speed) {
    if (speed == 0) return 0;
    if (speed >= MOTOR_SPEED_MAX_API) return MOTOR_DUTY_MAX_M138;
    return static_cast<int8_t>((uint32_t(speed) * 127 + 2047) / 4095);
}

// ── begin ───────────────────────────────────────────────────────
bool MotorController::begin(SemaphoreHandle_t i2cMutex) {
    if (!i2cMutex) {
        Serial.println("[Motor] ERROR: i2cMutex is null");
        return false;
    }
    i2cMutex_ = i2cMutex;

    if (xSemaphoreTake(i2cMutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[Motor] ERROR: failed to acquire I2C mutex");
        return false;
    }

    if (!driver_.begin(&Wire1, M138_I2C_ADDR, M138_I2C_SDA, M138_I2C_SCL, 100000)) {
        Serial.println("[Motor] ERROR: M138 not found on I2C bus");
        xSemaphoreGive(i2cMutex_);
        return false;
    }

    // Set all motors to NORMAL_MODE with speed 0
    for (uint8_t i = 0; i < 4; i++) {
        driver_.setMode(i, NORMAL_MODE);
        driver_.setMotorSpeed(i, 0);
        states_[i] = {MotorDirection::STOP, 0};
    }

    // Log firmware version (note: library has typo "Fireware")
    uint8_t fw = 0;
    if (driver_.getFirewareVersion(&fw)) {
        Serial.printf("[Motor] M138 firmware v%d\n", fw);
    }

    xSemaphoreGive(i2cMutex_);

    enabled_ = false;
    initialized_ = true;
    Serial.println("[Motor] M138 initialized OK");
    return true;
}

// ── setMotor ────────────────────────────────────────────────────
void MotorController::setMotor(uint8_t index, MotorDirection direction, uint16_t speed) {
    if (!initialized_ || index >= 4) return;

    if (speed > 4095) speed = 4095;

    if (xSemaphoreTake(i2cMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    int8_t duty = mapSpeedToDuty(speed);

    switch (direction) {
        case MotorDirection::CW:
            driver_.setMotorSpeed(index, +duty);
            if (!enabled_) enabled_ = true;
            break;
        case MotorDirection::CCW:
            driver_.setMotorSpeed(index, -duty);
            if (!enabled_) enabled_ = true;
            break;
        case MotorDirection::BRAKE:
        case MotorDirection::STOP:
        default:
            driver_.setMotorSpeed(index, 0);
            speed = 0;  // /status に実態を反映 (M138 は coast stop)
            break;
    }

    states_[index] = {direction, speed};

    xSemaphoreGive(i2cMutex_);
}

// ── emergencyStop ───────────────────────────────────────────────
void MotorController::emergencyStop() {
    if (!initialized_) return;

    if (xSemaphoreTake(i2cMutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;

    for (uint8_t i = 0; i < 4; i++) {
        driver_.setMotorSpeed(i, 0);
        states_[i] = {MotorDirection::STOP, 0};
    }

    enabled_ = false;

    xSemaphoreGive(i2cMutex_);

    Serial.println("[Motor] Emergency stop");
}

// ── Watchdog ────────────────────────────────────────────────────
void MotorController::feedWatchdog() {
    lastCommandMs_ = millis();
}

bool MotorController::checkWatchdog() {
    if (!enabled_) return false;

    if (millis() - lastCommandMs_ >= WATCHDOG_TIMEOUT_MS) {
        Serial.println("[Motor] Watchdog timeout - stopping motors");
        emergencyStop();
        return true;
    }
    return false;
}

// ── State accessors ─────────────────────────────────────────────
void MotorController::getMotorStates(MotorState states[4]) const {
    for (uint8_t i = 0; i < 4; i++) {
        states[i] = states_[i];
    }
}

bool MotorController::isEnabled() const {
    return enabled_;
}

// ── FreeRTOS task ───────────────────────────────────────────────
void taskMotorControl(void* param) {
    auto* controller = static_cast<MotorController*>(param);
    MotorCommand cmd;

    for (;;) {
        BaseType_t received = xQueueReceive(
            g_motorQueue, &cmd, pdMS_TO_TICKS(MOTOR_WATCHDOG_POLL_MS));

        if (received == pdTRUE) {
            switch (cmd.type) {
                case MotorCommandType::SET_MOTOR:
                    // Fail-Closed: センサー異常時は全モーター動作拒否
                    if (!isSensorSystemHealthy()) {
                        Serial.printf("[Motor] Blocked: sensor system unhealthy\n");
                        controller->setMotor(cmd.motorId, MotorDirection::STOP, 0);
                    } else if (wouldMoveTowardObstacle(cmd.motorId, cmd.direction)) {
                        Serial.printf("[Motor] Blocked: motor %d toward obstacle\n", cmd.motorId);
                        controller->setMotor(cmd.motorId, MotorDirection::STOP, 0);
                    } else {
                        controller->setMotor(cmd.motorId, cmd.direction, cmd.speed);
                    }
                    controller->feedWatchdog();
                    break;
                case MotorCommandType::EMERGENCY_STOP:
                    controller->emergencyStop();
                    break;
            }
        } else {
            controller->checkWatchdog();
        }
    }
}

}  // namespace mova
