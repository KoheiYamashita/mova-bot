#include <Arduino.h>
#include <Wire.h>
#include <VL53L0X.h>

#include "obstacle_sensor.h"
#include "display.h"

namespace mova {

// ── Static state (spinlock-protected) ───────────────────────────
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_distance_mm[4] = {};
static uint8_t  s_blocked = 0;
static bool     s_sensorHealthy = false;
static uint8_t  s_sensorError = 0;
static uint8_t  s_lastStopDir = 0;
static uint16_t s_lastStopDist = 0;
static uint32_t s_lastStopUptime = 0;

// ── Module state ────────────────────────────────────────────────
static bool s_initialized = false;
static VL53L0X s_sensors[TOF_SENSOR_COUNT];
static bool s_sensorEnabled[TOF_SENSOR_COUNT] = {};
static uint8_t s_timeoutCount[TOF_SENSOR_COUNT] = {};  // consecutive timeout counter
static uint8_t s_successCount[TOF_SENSOR_COUNT] = {};   // consecutive success counter
static SemaphoreHandle_t s_i2cMutex = nullptr;
static QueueHandle_t s_motorQueue = nullptr;
static MotorController* s_motor = nullptr;

static constexpr uint8_t TIMEOUT_THRESHOLD = 3;  // consecutive timeouts → error
static constexpr uint8_t RECOVERY_THRESHOLD = 3; // consecutive successes → recovered

// ── PaHUB2 channel select ───────────────────────────────────────
static void pahub2Select(uint8_t ch) {
    Wire.beginTransmission(PAHUB2_ADDRESS);
    Wire.write(1 << ch);
    Wire.endTransmission();
}

// ── Recalculate blocked bitmask and health from distances ───────
static uint8_t recalcBlockedMask() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < TOF_SENSOR_COUNT; i++) {
        if (s_sensorEnabled[i] && s_distance_mm[i] > 0 &&
            s_distance_mm[i] < 8190 && s_distance_mm[i] <= TOF_THRESHOLD_MM) {
            mask |= (1 << i);
        }
    }
    return mask;
}

static void updateSensorHealth() {
    // Any sensor in error → unhealthy
    portENTER_CRITICAL(&s_spinlock);
    s_sensorHealthy = (s_sensorError == 0);
    portEXIT_CRITICAL(&s_spinlock);
}

// ── Display error on screen ─────────────────────────────────────
// NOTE: g_displayQueue may be null during early boot (sensor init runs before
// display queue creation). Always null-check before sending.
static void showSensorError(uint8_t ch, const char* errorType) {
    if (!g_displayQueue) return;
    const char* dirName = sensorDirectionToString(static_cast<SensorDirection>(ch));
    DisplayCommand dcmd = {};
    dcmd.type = DisplayCommand::STATUS;
    snprintf(dcmd.statusText, sizeof(dcmd.statusText), "SENSOR: %s %s", dirName, errorType);
    xQueueSend(g_displayQueue, &dcmd, pdMS_TO_TICKS(50));
}

// ── obstacleInit ────────────────────────────────────────────────
bool obstacleInit(SemaphoreHandle_t i2cMutex, QueueHandle_t motorQueue,
                  MotorController* motor) {
    if (!i2cMutex || !motorQueue || !motor) {
        Serial.println("[Sensor] ERROR: null parameter");
        return false;
    }

    s_i2cMutex = i2cMutex;
    s_motorQueue = motorQueue;
    s_motor = motor;

    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[Sensor] ERROR: failed to acquire I2C mutex");
        return false;
    }

    Wire.begin(PORT_A_SDA, PORT_A_SCL);

    uint8_t initCount = 0;
    for (uint8_t ch = 0; ch < TOF_SENSOR_COUNT; ch++) {
        pahub2Select(ch);
        delay(10);

        s_sensors[ch].setBus(&Wire);
        s_sensors[ch].setTimeout(TOF_SENSOR_TIMEOUT_MS);

        if (s_sensors[ch].init()) {
            s_sensors[ch].setMeasurementTimingBudget(TOF_TIMING_BUDGET_US);
            s_sensorEnabled[ch] = true;
            initCount++;
            Serial.printf("[Sensor] CH%d (%s) initialized OK\n", ch,
                sensorDirectionToString(static_cast<SensorDirection>(ch)));
        } else {
            s_sensorEnabled[ch] = false;
            Serial.printf("[Sensor] ERROR: CH%d (%s) init failed\n", ch,
                sensorDirectionToString(static_cast<SensorDirection>(ch)));
        }
    }

    xSemaphoreGive(s_i2cMutex);

    if (initCount == 0) {
        Serial.println("[Sensor] ERROR: no sensors initialized");
        portENTER_CRITICAL(&s_spinlock);
        s_sensorHealthy = false;
        s_sensorError = 0x0F;  // all sensors in error
        portEXIT_CRITICAL(&s_spinlock);
        s_initialized = false;
        return false;
    }

    // Check if any sensor failed → mark unhealthy (fail-closed)
    uint8_t errorMask = 0;
    for (uint8_t ch = 0; ch < TOF_SENSOR_COUNT; ch++) {
        if (!s_sensorEnabled[ch]) {
            errorMask |= (1 << ch);
        }
    }

    portENTER_CRITICAL(&s_spinlock);
    s_sensorError = errorMask;
    s_sensorHealthy = (errorMask == 0);
    portEXIT_CRITICAL(&s_spinlock);

    if (errorMask != 0) {
        Serial.printf("[Sensor] WARNING: some sensors failed (error mask: 0x%02X) - motors blocked\n", errorMask);
    }

    s_initialized = true;
    Serial.printf("[Sensor] %d/%d sensors initialized, healthy=%d\n",
        initCount, TOF_SENSOR_COUNT, s_sensorHealthy);
    return true;
}

// ── taskObstacleDetection ───────────────────────────────────────
void taskObstacleDetection(void* param) {
    (void)param;

    if (!s_initialized) {
        Serial.println("[Sensor] Task aborted: not initialized");
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        uint32_t cycleStart = millis();

        for (uint8_t ch = 0; ch < TOF_SENSOR_COUNT; ch++) {
            if (!s_sensorEnabled[ch]) continue;

            if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(50)) != pdTRUE) continue;

            pahub2Select(ch);
            uint16_t dist = s_sensors[ch].readRangeSingleMillimeters();
            bool timeout = s_sensors[ch].timeoutOccurred();

            xSemaphoreGive(s_i2cMutex);

            // Handle timeout tracking
            if (timeout) {
                s_successCount[ch] = 0;
                s_timeoutCount[ch]++;
                if (s_timeoutCount[ch] >= TIMEOUT_THRESHOLD) {
                    // Sensor entered error state
                    bool wasHealthy;
                    portENTER_CRITICAL(&s_spinlock);
                    wasHealthy = !(s_sensorError & (1 << ch));
                    s_sensorError |= (1 << ch);
                    s_distance_mm[ch] = 8190;
                    s_blocked = recalcBlockedMask();
                    portEXIT_CRITICAL(&s_spinlock);

                    if (wasHealthy) {
                        Serial.printf("[Sensor] ERROR: CH%d (%s) consecutive timeouts - sensor unhealthy\n",
                            ch, sensorDirectionToString(static_cast<SensorDirection>(ch)));
                        showSensorError(ch, "TIMEOUT");
                        updateSensorHealth();
                    }
                } else {
                    portENTER_CRITICAL(&s_spinlock);
                    s_distance_mm[ch] = 8190;
                    s_blocked = recalcBlockedMask();
                    portEXIT_CRITICAL(&s_spinlock);
                    Serial.printf("[Sensor] WARN: CH%d timeout (%d/%d)\n",
                        ch, s_timeoutCount[ch], TIMEOUT_THRESHOLD);
                }
                continue;
            }

            // Successful reading
            s_timeoutCount[ch] = 0;
            s_successCount[ch]++;

            // Check for recovery from error state
            if (s_sensorError & (1 << ch)) {
                if (s_successCount[ch] >= RECOVERY_THRESHOLD) {
                    portENTER_CRITICAL(&s_spinlock);
                    s_sensorError &= ~(1 << ch);
                    portEXIT_CRITICAL(&s_spinlock);
                    updateSensorHealth();
                    Serial.printf("[Sensor] CH%d (%s) recovered\n",
                        ch, sensorDirectionToString(static_cast<SensorDirection>(ch)));
                }
            }

            // Update distance and check for new obstacle
            uint8_t prevBlocked;
            bool newlyBlocked = false;

            portENTER_CRITICAL(&s_spinlock);
            prevBlocked = s_blocked;
            s_distance_mm[ch] = dist;
            s_blocked = recalcBlockedMask();

            // Check if this direction is newly blocked
            if ((s_blocked & (1 << ch)) && !(prevBlocked & (1 << ch))) {
                newlyBlocked = true;
                s_lastStopDir = ch;
                s_lastStopDist = dist;
                s_lastStopUptime = millis() / 1000;
            }
            portEXIT_CRITICAL(&s_spinlock);

            if (newlyBlocked) {
                Serial.printf("[Sensor] Obstacle detected: %s at %dmm - emergency stop\n",
                    sensorDirectionToString(static_cast<SensorDirection>(ch)), dist);

                MotorCommand cmd = {};
                cmd.type = MotorCommandType::EMERGENCY_STOP;
                if (xQueueSendToFront(s_motorQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
                    s_motor->emergencyStop();
                }
            }
        }

        // Maintain polling rate
        uint32_t elapsed = millis() - cycleStart;
        if (elapsed < TOF_POLL_INTERVAL_MS) {
            vTaskDelay(pdMS_TO_TICKS(TOF_POLL_INTERVAL_MS - elapsed));
        }
    }
}

// ── Thread-safe snapshot ────────────────────────────────────────
ObstacleSnapshot getObstacleSnapshot() {
    ObstacleSnapshot snap = {};
    portENTER_CRITICAL(&s_spinlock);
    for (uint8_t i = 0; i < 4; i++) {
        snap.distance_mm[i] = s_distance_mm[i];
    }
    snap.blocked = s_blocked;
    snap.sensorHealthy = s_sensorHealthy;
    snap.sensorError = s_sensorError;
    snap.lastStopDirection = s_lastStopDir;
    snap.lastStopDistance_mm = s_lastStopDist;
    snap.lastStopUptime_sec = s_lastStopUptime;
    portEXIT_CRITICAL(&s_spinlock);
    return snap;
}

// ── Direction query ─────────────────────────────────────────────
bool isDirectionBlocked(SensorDirection dir) {
    portENTER_CRITICAL(&s_spinlock);
    bool blocked = (s_blocked & (1 << static_cast<uint8_t>(dir))) != 0;
    portEXIT_CRITICAL(&s_spinlock);
    return blocked;
}

// ── Kinematics-based obstacle check ─────────────────────────────
bool wouldMoveTowardObstacle(uint8_t motorId, MotorDirection direction) {
    if (motorId >= 4) return false;
    if (direction == MotorDirection::STOP || direction == MotorDirection::BRAKE) return false;

    if (!s_initialized) return false;

    ObstacleSnapshot snap = getObstacleSnapshot();
    if (snap.blocked == 0) return false;

    auto contrib = MOTOR_CONTRIB_CW[motorId];
    int8_t fwd = (direction == MotorDirection::CW) ? contrib.forward : -contrib.forward;
    int8_t rgt = (direction == MotorDirection::CW) ? contrib.right   : -contrib.right;

    if (fwd > 0 && (snap.blocked & (1 << static_cast<uint8_t>(SensorDirection::FRONT)))) return true;
    if (fwd < 0 && (snap.blocked & (1 << static_cast<uint8_t>(SensorDirection::REAR))))  return true;
    if (rgt > 0 && (snap.blocked & (1 << static_cast<uint8_t>(SensorDirection::RIGHT)))) return true;
    if (rgt < 0 && (snap.blocked & (1 << static_cast<uint8_t>(SensorDirection::LEFT))))  return true;

    return false;
}

// ── Sensor system health ────────────────────────────────────────
bool isSensorSystemHealthy() {
    if (!s_initialized) return true;  // No sensors → don't block motors

    portENTER_CRITICAL(&s_spinlock);
    bool healthy = s_sensorHealthy;
    portEXIT_CRITICAL(&s_spinlock);
    return healthy;
}

// ── Direction name ──────────────────────────────────────────────
const char* sensorDirectionToString(SensorDirection dir) {
    switch (dir) {
        case SensorDirection::FRONT: return "front";
        case SensorDirection::RIGHT: return "right";
        case SensorDirection::REAR:  return "rear";
        case SensorDirection::LEFT:  return "left";
        default:                     return "unknown";
    }
}

}  // namespace mova
