#ifndef MOVA_OBSTACLE_SENSOR_H
#define MOVA_OBSTACLE_SENSOR_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "config.h"
#include "motor_controller.h"

namespace mova {

enum class SensorDirection : uint8_t {
    FRONT = 0, RIGHT = 1, REAR = 2, LEFT = 3,
};

struct ObstacleSnapshot {
    uint16_t distance_mm[4];       // 各方向の距離 (0=無効, 8190=範囲外)
    uint8_t  blocked;              // ビットマスク: bit0=FRONT ... bit3=LEFT
    bool     sensorHealthy;        // 全センサー正常なら true
    uint8_t  sensorError;          // エラーのあるセンサーのビットマスク
    uint8_t  lastStopDirection;    // SensorDirection 値
    uint16_t lastStopDistance_mm;
    uint32_t lastStopUptime_sec;
};

bool obstacleInit(SemaphoreHandle_t i2cMutex, QueueHandle_t motorQueue,
                  MotorController* motor);
void taskObstacleDetection(void* param);
ObstacleSnapshot getObstacleSnapshot();
bool isDirectionBlocked(SensorDirection dir);
bool wouldMoveTowardObstacle(uint8_t motorId, MotorDirection direction);
bool isSensorSystemHealthy();
const char* sensorDirectionToString(SensorDirection dir);

}  // namespace mova

#endif  // MOVA_OBSTACLE_SENSOR_H
