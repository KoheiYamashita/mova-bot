#include <Arduino.h>
#include <Wire.h>

#include "motor_controller.h"

namespace mova {

// ── Helper: PCA9685 digital output ──────────────────────────────
void MotorController::setChannelHigh(uint8_t ch) {
    pca9685_.setPWM(ch, 4096, 0);
}

void MotorController::setChannelLow(uint8_t ch) {
    pca9685_.setPWM(ch, 0, 4096);
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

    if (!Wire.begin(I2C_SDA, I2C_SCL)) {
        Serial.println("[Motor] ERROR: Wire.begin() failed");
        xSemaphoreGive(i2cMutex_);
        return false;
    }
    Wire.setClock(400000);  // I2C Fast Mode (PLAN.md 準拠)

    if (!pca9685_.begin()) {
        Serial.println("[Motor] ERROR: PCA9685 not found on I2C bus");
        xSemaphoreGive(i2cMutex_);
        return false;
    }
    pca9685_.setPWMFreq(PCA9685_PWM_FREQ);  // 1000Hz
    pca9685_.setOutputMode(true);            // トーテムポール出力

    // STBY を LOW に設定（安全設計: 起動直後のモーター暴走防止）
    setChannelLow(STBY_CH_1);
    setChannelLow(STBY_CH_2);

    // 全モーターチャンネルを STOP (IN1=LOW, IN2=LOW, PWM=0) に初期化
    for (uint8_t i = 0; i < 4; i++) {
        const auto& pins = MOTOR_PINS[i];
        setChannelLow(pins.in1);
        setChannelLow(pins.in2);
        pca9685_.setPWM(pins.pwm, 0, 4096);
        states_[i] = {MotorDirection::STOP, 0};
    }

    // I2C 疎通確認: 初期化書き込み後にバスが正常か検証
    Wire.beginTransmission(PCA9685_ADDRESS);
    if (Wire.endTransmission() != 0) {
        Serial.println("[Motor] ERROR: PCA9685 lost after init sequence");
        xSemaphoreGive(i2cMutex_);
        return false;
    }

    xSemaphoreGive(i2cMutex_);

    enabled_ = false;
    initialized_ = true;
    Serial.println("[Motor] PCA9685 initialized OK");
    return true;
}

// ── setMotor ────────────────────────────────────────────────────
void MotorController::setMotor(uint8_t index, MotorDirection direction, uint16_t speed) {
    if (!initialized_ || index >= 4) return;

    // Clamp speed to 12-bit range
    if (speed > 4095) speed = 4095;

    if (xSemaphoreTake(i2cMutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;

    const auto& pins = MOTOR_PINS[index];

    // STBY 有効化（初回コマンド時）
    if (!enabled_) {
        // 安全状態を確保してから STBY を有効化
        setChannelLow(pins.in1);
        setChannelLow(pins.in2);
        pca9685_.setPWM(pins.pwm, 0, 4096);
        setChannelHigh(STBY_CH_1);
        setChannelHigh(STBY_CH_2);
        enabled_ = true;
    }

    // 方向ピン + PWM 設定
    uint16_t actualSpeed = speed;
    switch (direction) {
        case MotorDirection::CW:
            setChannelHigh(pins.in1);
            setChannelLow(pins.in2);
            break;
        case MotorDirection::CCW:
            setChannelLow(pins.in1);
            setChannelHigh(pins.in2);
            break;
        case MotorDirection::BRAKE:
            setChannelHigh(pins.in1);
            setChannelHigh(pins.in2);
            actualSpeed = 4095;  // ショートブレーキ: speed 引数を無視
            break;
        case MotorDirection::STOP:
        default:
            setChannelLow(pins.in1);
            setChannelLow(pins.in2);
            actualSpeed = 0;
            break;
    }

    if (actualSpeed == 0) {
        pca9685_.setPWM(pins.pwm, 0, 4096);  // full OFF
    } else if (actualSpeed >= 4095) {
        pca9685_.setPWM(pins.pwm, 4096, 0);  // full ON
    } else {
        pca9685_.setPWM(pins.pwm, 0, actualSpeed);
    }

    states_[index] = {direction, actualSpeed};

    xSemaphoreGive(i2cMutex_);
}

// ── emergencyStop ───────────────────────────────────────────────
void MotorController::emergencyStop() {
    if (!initialized_) return;

    if (xSemaphoreTake(i2cMutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;

    // 全モーターを STOP: IN1=LOW, IN2=LOW, PWM=0
    for (uint8_t i = 0; i < 4; i++) {
        const auto& pins = MOTOR_PINS[i];
        setChannelLow(pins.in1);
        setChannelLow(pins.in2);
        pca9685_.setPWM(pins.pwm, 0, 4096);  // full OFF
        states_[i] = {MotorDirection::STOP, 0};
    }

    // STBY を LOW に設定（ハードウェアレベル無効化）
    setChannelLow(STBY_CH_1);
    setChannelLow(STBY_CH_2);
    enabled_ = false;

    xSemaphoreGive(i2cMutex_);

    Serial.println("[Motor] Emergency stop");
}

// ── Watchdog ────────────────────────────────────────────────────
void MotorController::feedWatchdog() {
    lastCommandMs_ = millis();
}

bool MotorController::checkWatchdog() {
    if (!enabled_) return false;  // 既に停止済み

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
                    controller->setMotor(cmd.motorId, cmd.direction, cmd.speed);
                    controller->feedWatchdog();
                    break;
                case MotorCommandType::EMERGENCY_STOP:
                    controller->emergencyStop();
                    // Watchdog は feed しない（意図的停止）
                    break;
            }
        } else {
            // キュータイムアウト — ウォッチドッグチェック
            controller->checkWatchdog();
        }
    }
}

}  // namespace mova
