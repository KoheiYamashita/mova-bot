#include "web_server.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>

#include "config.h"
#include "display.h"
#include "audio_player.h"
#include "camera.h"

namespace mova {

// Sentinel value indicating body was too large (no allocation to free)
// Cannot use constexpr with reinterpret_cast, so use a define
#define BODY_TOO_LARGE_SENTINEL ((void*)1)

// ── init ────────────────────────────────────────────────────────

void MOVAWebServer::init(QueueHandle_t motorQ, QueueHandle_t displayQ,
                         QueueHandle_t audioQ, MotorController* motor) {
    motorQueue_   = motorQ;
    displayQueue_ = displayQ;
    audioQueue_   = audioQ;
    motor_        = motor;

    // D6: 😐 (U+1F610) UTF-8
    memcpy(currentEmoji_, "\xF0\x9F\x98\x90", 5);  // 4 bytes + NUL
}

// ── begin ───────────────────────────────────────────────────────

bool MOVAWebServer::begin() {
    if (running_) return true;

    // D8: LittleFS mount (no auto-format)
    bool fsOk = LittleFS.begin(false);
    if (!fsOk) {
        Serial.println("[Web] WARNING: LittleFS mount failed - static files disabled");
    }

    // --- CORS preflight (explicit handler) ---
    server_.on("/*", HTTP_OPTIONS, [this](AsyncWebServerRequest* request) {
        auto* resp = request->beginResponse(204);
        addCorsHeaders(resp);
        request->send(resp);
    });

    // --- POST /command (body accumulation + handler) ---
    server_.on("/command", HTTP_POST,
        // onRequest: process accumulated body
        [this](AsyncWebServerRequest* request) {
            handleCommand(request);
        },
        nullptr,  // onUpload
        // onBody: accumulate chunks
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
           size_t index, size_t total) {
            // D5: body too large → sentinel
            if (total > MAX_JSON_BODY_SIZE) {
                if (request->_tempObject && request->_tempObject != BODY_TOO_LARGE_SENTINEL) {
                    free(request->_tempObject);
                }
                request->_tempObject = BODY_TOO_LARGE_SENTINEL;
                return;
            }
            // Already marked as too large
            if (request->_tempObject == BODY_TOO_LARGE_SENTINEL) return;

            // First chunk: allocate buffer
            if (index == 0) {
                request->_tempObject = malloc(total + 1);
                if (!request->_tempObject) return;  // malloc failed → stays null
            }
            if (!request->_tempObject) return;

            // Copy chunk
            memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);

            // Last chunk: NUL-terminate
            if (index + len == total) {
                static_cast<uint8_t*>(request->_tempObject)[total] = '\0';
            }
        }
    );

    // --- POST /emergency_stop ---
    server_.on("/emergency_stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleEmergencyStop(request);
    });

    // --- GET /status ---
    server_.on("/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleStatus(request);
    });

    // --- GET /capture ---
    server_.on("/capture", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCapture(request);
    });

    // --- Static files from LittleFS (with CORS headers via middleware) ---
    if (fsOk) {
        auto& handler = server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        handler.addMiddleware([this](AsyncWebServerRequest* request, ArMiddlewareNext next) {
            next();
            addCorsHeaders(request->getResponse());
        });
    }

    // --- 404 handler ---
    server_.onNotFound([this](AsyncWebServerRequest* request) {
        sendJson(request, 404, "{\"status\":\"error\",\"message\":\"Not found\"}");
    });

    server_.begin();
    running_ = true;
    Serial.printf("[Web] Server started on port %d\n", HTTP_PORT);
    return true;
}

// ── stop ────────────────────────────────────────────────────────

void MOVAWebServer::stop() {
    if (!running_) return;
    server_.end();
    running_ = false;
    Serial.println("[Web] Server stopped");
}

// ── handleCommand ───────────────────────────────────────────────

void MOVAWebServer::handleCommand(AsyncWebServerRequest* request) {
    void* tmp = request->_tempObject;
    request->_tempObject = nullptr;

    // D5: sentinel → 413
    if (tmp == BODY_TOO_LARGE_SENTINEL) {
        sendJson(request, 413, "{\"status\":\"error\",\"message\":\"Payload too large\"}");
        return;
    }

    // No body
    if (!tmp) {
        sendJson(request, 400, "{\"status\":\"error\",\"message\":\"Empty body\"}");
        return;
    }

    // Parse JSON (ArduinoJson v7 copies data internally)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, static_cast<char*>(tmp));

    // Free raw body immediately — ArduinoJson v7 has its own copy (D10)
    free(tmp);
    tmp = nullptr;

    if (err) {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"status\":\"error\",\"message\":\"Invalid JSON: %s\"}", err.c_str());
        sendJson(request, 400, msg);
        return;
    }

    char errBuf[128] = {};
    bool hasAction = false;

    // Process motors
    if (!doc["motors"].isNull()) {
        if (!doc["motors"].is<JsonArray>()) {
            sendJson(request, 400, "{\"status\":\"error\",\"message\":\"'motors' must be an array\"}");
            return;
        }
        hasAction = true;
        int rc = parseMotors(doc["motors"].as<JsonArrayConst>(), errBuf, sizeof(errBuf));
        if (rc != 0) {
            sendJson(request, rc, errBuf);
            return;
        }
    }

    // Process emoji
    if (!doc["emoji"].isNull()) {
        if (!doc["emoji"].is<const char*>()) {
            sendJson(request, 400, "{\"status\":\"error\",\"message\":\"'emoji' must be a string\"}");
            return;
        }
        hasAction = true;
        int rc = parseEmoji(doc["emoji"].as<const char*>(), errBuf, sizeof(errBuf));
        if (rc != 0) {
            sendJson(request, rc, errBuf);
            return;
        }
    }

    // Process audio
    if (!doc["audio"].isNull()) {
        if (!doc["audio"].is<JsonObject>()) {
            sendJson(request, 400, "{\"status\":\"error\",\"message\":\"'audio' must be an object\"}");
            return;
        }
        hasAction = true;
        int rc = parseAudio(doc["audio"].as<JsonObjectConst>(), errBuf, sizeof(errBuf));
        if (rc != 0) {
            sendJson(request, rc, errBuf);
            return;
        }
    }

    if (!hasAction) {
        sendJson(request, 400, "{\"status\":\"error\",\"message\":\"No recognized command fields\"}");
        return;
    }

    sendJson(request, 200, "{\"status\":\"ok\"}");
}

// ── parseMotors (D3: 2-pass validation) ─────────────────────────

int MOVAWebServer::parseMotors(JsonArrayConst motors, char* errBuf, size_t errBufLen) {
    // Pass 1: validate all entries
    for (JsonObjectConst m : motors) {
        if (!m["id"].is<int>()) {
            snprintf(errBuf, errBufLen,
                "{\"status\":\"error\",\"message\":\"Motor entry missing 'id'\"}");
            return 400;
        }
        int id = m["id"].as<int>();
        if (id < 0 || id > 3) {
            snprintf(errBuf, errBufLen,
                "{\"status\":\"error\",\"message\":\"Invalid motor id: %d (must be 0-3)\"}", id);
            return 400;
        }

        if (!m["direction"].is<const char*>()) {
            snprintf(errBuf, errBufLen,
                "{\"status\":\"error\",\"message\":\"Motor %d missing 'direction'\"}", id);
            return 400;
        }
        MotorDirection dir = parseDirection(m["direction"].as<const char*>());
        if (dir == static_cast<MotorDirection>(0xFF)) {
            snprintf(errBuf, errBufLen,
                "{\"status\":\"error\",\"message\":\"Invalid direction: '%s'\"}",
                m["direction"].as<const char*>());
            return 400;
        }
    }

    // Pass 2: send all commands
    for (JsonObjectConst m : motors) {
        MotorCommand cmd = {};
        cmd.type      = MotorCommandType::SET_MOTOR;
        cmd.motorId   = static_cast<uint8_t>(m["id"].as<int>());
        cmd.direction = parseDirection(m["direction"].as<const char*>());

        // Read as int first to catch negative values, then clamp to 0-4095
        int speed = m["speed"] | 0;
        if (speed < 0) speed = 0;
        if (speed > 4095) speed = 4095;
        cmd.speed = static_cast<uint16_t>(speed);

        if (xQueueSend(motorQueue_, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
            snprintf(errBuf, errBufLen,
                "{\"status\":\"error\",\"message\":\"Motor command queue full\"}");
            return 400;
        }
    }

    return 0;
}

// ── parseEmoji ──────────────────────────────────────────────────

int MOVAWebServer::parseEmoji(const char* emojiStr, char* errBuf, size_t errBufLen) {
    // Empty string → reset to 😐
    if (!emojiStr || emojiStr[0] == '\0') {
        memcpy(currentEmoji_, "\xF0\x9F\x98\x90", 5);
    } else {
        strncpy(currentEmoji_, emojiStr, sizeof(currentEmoji_) - 1);
        currentEmoji_[sizeof(currentEmoji_) - 1] = '\0';
    }

    DisplayCommand dcmd = {};
    dcmd.type = DisplayCommand::EMOJI;
    // emojiIndex is used by Phase 6 sprite lookup; for now just store 0
    dcmd.emojiIndex = 0;

    if (xQueueSend(displayQueue_, &dcmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Display command queue full\"}");
        return 400;
    }

    Serial.printf("[Web] Emoji set to: %s\n", currentEmoji_);
    return 0;
}

// ── parseAudio (D9: PCM alignment check) ────────────────────────

int MOVAWebServer::parseAudio(JsonObjectConst audio, char* errBuf, size_t errBufLen) {
    // Validate sample_rate
    uint16_t sampleRate = audio["sample_rate"] | static_cast<uint16_t>(0);
    if (sampleRate != 8000 && sampleRate != 16000 && sampleRate != 44100) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Invalid sample_rate: %d (must be 8000/16000/44100)\"}",
            sampleRate);
        return 400;
    }

    // Validate bits
    uint8_t bits = audio["bits"] | static_cast<uint8_t>(0);
    if (bits != 8 && bits != 16) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Invalid bits: %d (must be 8 or 16)\"}", bits);
        return 400;
    }

    // Validate channels
    uint8_t channels = audio["channels"] | static_cast<uint8_t>(0);
    if (channels != 1) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Invalid channels: %d (must be 1)\"}", channels);
        return 400;
    }

    // Validate data
    if (!audio["data"].is<const char*>()) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Missing 'data' field\"}");
        return 400;
    }
    const char* b64data = audio["data"].as<const char*>();
    size_t b64len = strlen(b64data);
    if (b64len == 0) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Empty audio data\"}");
        return 400;
    }
    if (b64len > MAX_AUDIO_BASE64_LEN) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Audio data too large (%zu > %zu)\"}",
            b64len, MAX_AUDIO_BASE64_LEN);
        return 400;
    }

    // Base64 decode: get output size
    size_t decodedLen = 0;
    int ret = mbedtls_base64_decode(nullptr, 0, &decodedLen,
        reinterpret_cast<const unsigned char*>(b64data), b64len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Invalid base64 data\"}");
        return 400;
    }

    // Allocate PSRAM buffer
    uint8_t* pcmBuf = static_cast<uint8_t*>(ps_malloc(decodedLen));
    if (!pcmBuf) {
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Out of memory\"}");
        return 500;
    }

    // Actual decode
    size_t actualLen = 0;
    ret = mbedtls_base64_decode(pcmBuf, decodedLen, &actualLen,
        reinterpret_cast<const unsigned char*>(b64data), b64len);
    if (ret != 0) {
        free(pcmBuf);
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Base64 decode failed\"}");
        return 400;
    }

    // D9: PCM alignment check
    size_t frameSize = (bits / 8) * channels;
    if (actualLen % frameSize != 0) {
        free(pcmBuf);
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"PCM data length not aligned to frame size\"}");
        return 400;
    }

    // Build AudioCommand and send to queue
    AudioCommand acmd = {};
    acmd.sampleRate = sampleRate;
    acmd.bits       = bits;
    acmd.channels   = channels;
    acmd.pcmData    = pcmBuf;
    acmd.pcmLength  = actualLen;

    if (xQueueSend(audioQueue_, &acmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(pcmBuf);
        snprintf(errBuf, errBufLen,
            "{\"status\":\"error\",\"message\":\"Audio command queue full\"}");
        return 400;
    }

    Serial.printf("[Web] Audio queued: %dHz %dbit %zu bytes\n", sampleRate, bits, actualLen);
    return 0;
}

// ── handleEmergencyStop (D2: fallback guarantee) ────────────────

void MOVAWebServer::handleEmergencyStop(AsyncWebServerRequest* request) {
    MotorCommand cmd = {};
    cmd.type = MotorCommandType::EMERGENCY_STOP;

    if (xQueueSendToFront(motorQueue_, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
        // D2: queue full fallback — call emergencyStop directly
        motor_->emergencyStop();
        Serial.println("[Web] Emergency stop: queue full, direct fallback used");
    }

    sendJson(request, 200, "{\"status\":\"ok\"}");
}

// ── handleStatus ────────────────────────────────────────────────

void MOVAWebServer::handleStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["battery_level"] = M5.Power.getBatteryLevel();
    doc["wifi_rssi"]     = WiFi.RSSI();
    doc["motor_enabled"] = motor_->isEnabled();
    doc["uptime_sec"]    = millis() / 1000;
    doc["current_emoji"] = currentEmoji_;

    MotorState states[4];
    motor_->getMotorStates(states);

    JsonArray arr = doc["motor_states"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        JsonObject mo = arr.add<JsonObject>();
        mo["id"]        = i;
        mo["speed"]     = states[i].speed;
        mo["direction"] = directionToString(states[i].direction);
    }

    String body;
    serializeJson(doc, body);

    auto* resp = request->beginResponse(200, "application/json", body);
    addCorsHeaders(resp);
    request->send(resp);
}

// ── handleCapture ────────────────────────────────────────────────

void MOVAWebServer::handleCapture(AsyncWebServerRequest* request) {
    size_t jpegLen = 0;
    uint8_t* jpeg = cameraCaptureJpeg(&jpegLen);

    if (!jpeg) {
        sendJson(request, 503, "{\"status\":\"error\",\"message\":\"Camera not available\"}");
        return;
    }

    auto* resp = request->beginResponse(200, "image/jpeg", jpeg, jpegLen);
    addCorsHeaders(resp);

    // onDisconnect で JPEG バッファを解放 (非同期送信完了後)
    request->onDisconnect([jpeg]() {
        free(jpeg);
    });

    request->send(resp);
}

// ── Utilities ───────────────────────────────────────────────────

MotorDirection MOVAWebServer::parseDirection(const char* str) {
    if (!str) return static_cast<MotorDirection>(0xFF);
    if (strcmp(str, "cw")    == 0) return MotorDirection::CW;
    if (strcmp(str, "ccw")   == 0) return MotorDirection::CCW;
    if (strcmp(str, "brake") == 0) return MotorDirection::BRAKE;
    if (strcmp(str, "stop")  == 0) return MotorDirection::STOP;
    return static_cast<MotorDirection>(0xFF);  // invalid sentinel
}

const char* MOVAWebServer::directionToString(MotorDirection dir) {
    switch (dir) {
        case MotorDirection::CW:    return "cw";
        case MotorDirection::CCW:   return "ccw";
        case MotorDirection::BRAKE: return "brake";
        case MotorDirection::STOP:  return "stop";
        default:                    return "unknown";
    }
}

void MOVAWebServer::addCorsHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void MOVAWebServer::sendJson(AsyncWebServerRequest* request, int code, const char* body) {
    auto* resp = request->beginResponse(code, "application/json", body);
    addCorsHeaders(resp);
    request->send(resp);
}

}  // namespace mova
