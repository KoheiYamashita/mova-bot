#include "camera.h"
#include "config.h"

#include <M5Unified.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <esp_http_server.h>

namespace mova {

// ── Module state ─────────────────────────────────────────────────
static bool s_cameraInitialized = false;
static httpd_handle_t s_streamServer = nullptr;

// ── Helpers ──────────────────────────────────────────────────────

// Clamp quality to valid JPEG range [10, 63]. 0 means "use default".
static uint8_t resolveQuality(uint8_t quality) {
    if (quality == 0) quality = CAM_DEFAULT_QUALITY;
    if (quality < 10) return 10;
    if (quality > 63) return 63;
    return quality;
}

// Capture a frame and convert to JPEG. Returns malloc'd buffer (caller frees).
// Returns nullptr on failure.
static uint8_t* captureFrameAsJpeg(uint8_t quality, size_t* outLen) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[Camera] fb_get failed");
        return nullptr;
    }

    uint8_t* jpegBuf = nullptr;
    size_t jpegLen = 0;
    bool ok = frame2jpg(fb, quality, &jpegBuf, &jpegLen);
    esp_camera_fb_return(fb);

    if (!ok || !jpegBuf) {
        Serial.println("[Camera] frame2jpg failed");
        free(jpegBuf);  // safe even if null; jpegBuf may be non-null on partial failure
        *outLen = 0;
        return nullptr;
    }

    *outLen = jpegLen;
    return jpegBuf;
}

// ── cameraInit ───────────────────────────────────────────────────

bool cameraInit() {
    // 内部 I2C バスを解放 (SCCB と共有のため)
    M5.In_I2C.release();
    // TODO: M5.In_I2C.release() 後に内部 I2C デバイス (AXP2101 バッテリー監視等) が
    //       不安定になる場合、カメラ初期化後に M5.In_I2C.begin() で再取得を検討

    camera_config_t config = {};
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
    config.ledc_timer   = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size   = FRAMESIZE_QVGA;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        Serial.printf("[Camera] GC0308 initialized OK (PID=0x%02X)\n", sensor->id.PID);
        sensor->set_whitebal(sensor, 1);    // AWB 有効
        sensor->set_awb_gain(sensor, 1);    // AWB ゲイン有効
    } else {
        Serial.println("[Camera] initialized OK (sensor info unavailable)");
    }

    s_cameraInitialized = true;
    return true;
}

// ── cameraIsInitialized ──────────────────────────────────────────

bool cameraIsInitialized() {
    return s_cameraInitialized;
}

// ── cameraCaptureJpeg ────────────────────────────────────────────

uint8_t* cameraCaptureJpeg(size_t* outLen, uint8_t quality) {
    if (!outLen) return nullptr;
    if (!s_cameraInitialized) return nullptr;

    return captureFrameAsJpeg(resolveQuality(quality), outLen);
}

// ── Stream handler (ESP-IDF httpd callback) ──────────────────────

static const char STREAM_CONTENT_TYPE[] = "multipart/x-mixed-replace;boundary=frame";
static const char STREAM_PART_HEADER[] =
    "--frame\r\n"
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n"
    "\r\n";

static esp_err_t streamHandler(httpd_req_t* req) {
    // Parse query parameters
    uint8_t quality = CAM_DEFAULT_QUALITY;
    char queryBuf[64] = {};
    if (httpd_req_get_url_query_str(req, queryBuf, sizeof(queryBuf)) == ESP_OK) {
        char valBuf[16] = {};

        // ?quality=N  (clamped to 10-63)
        if (httpd_query_key_value(queryBuf, "quality", valBuf, sizeof(valBuf)) == ESP_OK) {
            int q = atoi(valBuf);
            if (q >= 1) {
                quality = resolveQuality(static_cast<uint8_t>(q));
            }
        }

        // ?resolution=QQVGA|QVGA|VGA
        if (httpd_query_key_value(queryBuf, "resolution", valBuf, sizeof(valBuf)) == ESP_OK) {
            sensor_t* sensor = esp_camera_sensor_get();
            if (sensor) {
                framesize_t fs = FRAMESIZE_QVGA;
                if (strcasecmp(valBuf, "QQVGA") == 0) {
                    fs = FRAMESIZE_QQVGA;
                } else if (strcasecmp(valBuf, "VGA") == 0) {
                    fs = FRAMESIZE_VGA;
                }
                if (sensor->set_framesize(sensor, fs) != 0) {
                    Serial.println("[Camera] set_framesize failed, resetting to QVGA");
                    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
                }
            }
        }
    }

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    Serial.println("[Camera] Stream client connected");

    while (true) {
        size_t jpegLen = 0;
        uint8_t* jpegBuf = captureFrameAsJpeg(quality, &jpegLen);
        if (!jpegBuf) break;

        // Send multipart header
        char partHeader[128];
        int headerLen = snprintf(partHeader, sizeof(partHeader), STREAM_PART_HEADER,
                                 static_cast<unsigned>(jpegLen));
        if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(partHeader)) {
            free(jpegBuf);
            break;
        }

        esp_err_t res = httpd_resp_send_chunk(req, partHeader, headerLen);
        if (res != ESP_OK) {
            free(jpegBuf);
            break;
        }

        // Send JPEG data
        res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(jpegBuf), jpegLen);
        free(jpegBuf);
        if (res != ESP_OK) {
            break;
        }

        // Send trailing CRLF
        res = httpd_resp_send_chunk(req, "\r\n", 2);
        if (res != ESP_OK) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(CAM_FRAME_DELAY_MS));
    }

    // Reset to QVGA on stream end
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    }

    Serial.println("[Camera] Stream client disconnected");
    return ESP_OK;
}

// ── cameraStreamServerStart ──────────────────────────────────────

bool cameraStreamServerStart() {
    if (!s_cameraInitialized) return false;
    if (s_streamServer) return true;  // already running

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = STREAM_PORT;
    config.ctrl_port   = STREAM_PORT + 1;  // デフォルト (32768) との衝突回避
    config.stack_size  = CAM_HTTPD_STACK_SIZE;
    config.core_id     = 1;

    esp_err_t err = httpd_start(&s_streamServer, &config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] Stream server start failed: 0x%x\n", err);
        return false;
    }

    httpd_uri_t streamUri = {};
    streamUri.uri     = "/stream";
    streamUri.method  = HTTP_GET;
    streamUri.handler = streamHandler;

    err = httpd_register_uri_handler(s_streamServer, &streamUri);
    if (err != ESP_OK) {
        Serial.printf("[Camera] URI handler register failed: 0x%x\n", err);
        httpd_stop(s_streamServer);
        s_streamServer = nullptr;
        return false;
    }

    Serial.printf("[Camera] Stream server started on port %d\n", STREAM_PORT);
    return true;
}

}  // namespace mova
