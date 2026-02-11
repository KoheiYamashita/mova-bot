#ifndef MOVA_MOTOR_CONTROLLER_H
#define MOVA_MOTOR_CONTROLLER_H

#include <cstdint>
#include <M5Module4EncoderMotor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "config.h"

namespace mova {

// ── Motor command types ─────────────────────────────────────────
enum class MotorCommandType : uint8_t {
    SET_MOTOR      = 0,  // 個別モーター設定
    EMERGENCY_STOP = 1,  // 全モーター即時停止
};

struct MotorCommand {
    MotorCommandType type;
    uint8_t          motorId;    // 0-3 (SET_MOTOR 時のみ)
    uint16_t         speed;      // 0-4095 (SET_MOTOR 時のみ)
    MotorDirection   direction;  // (SET_MOTOR 時のみ)
};

// ── Motor controller ────────────────────────────────────────────
class MotorController {
public:
    bool begin(SemaphoreHandle_t i2cMutex);
    void setMotor(uint8_t index, MotorDirection direction, uint16_t speed);
    void emergencyStop();
    void feedWatchdog();
    bool checkWatchdog();  // true = タイムアウトで停止した
    void getMotorStates(MotorState states[4]) const;
    bool isEnabled() const;

    static int8_t mapSpeedToDuty(uint16_t speed);

private:
    M5Module4EncoderMotor driver_;
    SemaphoreHandle_t i2cMutex_ = nullptr;
    bool initialized_ = false;
    bool enabled_ = false;
    MotorState states_[4] = {};
    uint32_t lastCommandMs_ = 0;
};

void taskMotorControl(void* param);

// ── Global motor queue (defined in main.cpp) ────────────────────
extern QueueHandle_t g_motorQueue;

}  // namespace mova

#endif  // MOVA_MOTOR_CONTROLLER_H
