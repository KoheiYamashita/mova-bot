#ifndef MOVA_MOTOR_CONTROLLER_H
#define MOVA_MOTOR_CONTROLLER_H

#include <cstdint>
#include "config.h"

namespace mova {

struct MotorCommand {
    float vx;       // lateral velocity [-1.0, 1.0]
    float vy;       // forward velocity [-1.0, 1.0]
    float omega;    // rotational velocity [-1.0, 1.0]
};

class MotorController {
public:
    bool begin();
    void setMotor(uint8_t index, MotorDirection direction, uint16_t speed);
    void emergencyStop();
    void watchdog();

private:
    bool initialized_ = false;
};

void taskMotorControl(void* param);

}  // namespace mova

#endif  // MOVA_MOTOR_CONTROLLER_H
