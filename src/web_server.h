#ifndef MOVA_WEB_SERVER_H
#define MOVA_WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "motor_controller.h"

namespace mova {

class MOVAWebServer {
public:
    void init(QueueHandle_t motorQ, QueueHandle_t displayQ,
              QueueHandle_t audioQ, MotorController* motor);
    bool begin();
    void stop();

private:
    AsyncWebServer server_{HTTP_PORT};
    bool running_ = false;

    QueueHandle_t motorQueue_   = nullptr;
    QueueHandle_t displayQueue_ = nullptr;
    QueueHandle_t audioQueue_   = nullptr;
    MotorController* motor_     = nullptr;
    char currentEmoji_[32] = {};

    // Route handlers
    void handleCommand(AsyncWebServerRequest* request);
    void handleEmergencyStop(AsyncWebServerRequest* request);
    void handleStatus(AsyncWebServerRequest* request);
    void handleCapture(AsyncWebServerRequest* request);
    void handleMicRecord(AsyncWebServerRequest* request);

    // /command sub-parsers (return 0 on success, HTTP error code on failure)
    int parseMotors(JsonArrayConst motors, char* errBuf, size_t errBufLen);
    int parseEmoji(const char* emojiStr, char* errBuf, size_t errBufLen);
    int parseAudio(JsonObjectConst audio, char* errBuf, size_t errBufLen);

    // Utility
    static MotorDirection parseDirection(const char* str);
    static const char* directionToString(MotorDirection dir);
    void addCorsHeaders(AsyncWebServerResponse* response);
    void sendJson(AsyncWebServerRequest* request, int code, const char* body);
};

}  // namespace mova

#endif  // MOVA_WEB_SERVER_H
