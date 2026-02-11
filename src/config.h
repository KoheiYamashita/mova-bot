#ifndef MOVA_CONFIG_H
#define MOVA_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace mova {

// ── Firmware ────────────────────────────────────────────────────
constexpr const char* FIRMWARE_VERSION = "0.1.0";

// ── M138 Encoder Motor Module ──────────────────────────────────
constexpr uint8_t  M138_I2C_ADDR = 0x24;
constexpr uint8_t  M138_I2C_SDA  = 12;  // M5-Bus internal
constexpr uint8_t  M138_I2C_SCL  = 11;  // M5-Bus internal

// Speed mapping: API 0-4095 (12-bit) → M138 0-127 (7-bit)
constexpr uint16_t MOTOR_SPEED_MAX_API = 4095;
constexpr int8_t   MOTOR_DUTY_MAX_M138 = 127;

// ── ToF Obstacle Sensors ───────────────────────────────────────
constexpr uint8_t  PORT_A_SDA          = 2;
constexpr uint8_t  PORT_A_SCL          = 1;
constexpr uint8_t  PAHUB2_ADDRESS      = 0x70;

constexpr uint8_t  TOF_CH_FRONT = 0;
constexpr uint8_t  TOF_CH_RIGHT = 1;
constexpr uint8_t  TOF_CH_REAR  = 2;
constexpr uint8_t  TOF_CH_LEFT  = 3;
constexpr uint8_t  TOF_SENSOR_COUNT = 4;

constexpr uint16_t TOF_THRESHOLD_MM     = 150;
constexpr uint16_t TOF_POLL_INTERVAL_MS = 50;   // 4センサー1周 = 50ms (20Hz)
constexpr uint32_t TOF_TIMING_BUDGET_US = 20000; // 1センサーあたりの測定予算
constexpr uint16_t TOF_SENSOR_TIMEOUT_MS = 50;   // VL53L0X setTimeout() 値

constexpr uint32_t TASK_STACK_OBSTACLE    = 4096;
constexpr uint8_t  TASK_PRIORITY_OBSTACLE = 6;  // > MOTOR (5)

// ── Omni-Wheel Kinematics ──────────────────────────────────────
// 各モーターの CW 回転が前進/右移動に寄与する度合い (+1/-1)
// Motor配置: 0=FL, 1=FR, 2=RL, 3=RR (X配置)
struct MotorContribution {
    int8_t forward;  // +1 = 前進に寄与, -1 = 後退に寄与
    int8_t right;    // +1 = 右移動に寄与, -1 = 左移動に寄与
};

constexpr MotorContribution MOTOR_CONTRIB_CW[4] = {
    { +1, +1 },  // Motor 0 (FL) CW → 前進+右
    { -1, +1 },  // Motor 1 (FR) CW → 後退+右 (※実機で要検証)
    { +1, -1 },  // Motor 2 (RL) CW → 前進+左
    { -1, -1 },  // Motor 3 (RR) CW → 後退+左
};

// ── Motor types ────────────────────────────────────────────────
enum class MotorDirection : uint8_t {
    STOP  = 0,
    CW    = 1,
    CCW   = 2,
    BRAKE = 3,
};

struct MotorState {
    MotorDirection direction;
    uint16_t speed;  // 0-4095 (12-bit)
};

// ── FreeRTOS tasks ─────────────────────────────────────────────
constexpr uint32_t TASK_STACK_MOTOR   = 4096;
constexpr uint32_t TASK_STACK_CAMERA  = 8192;
constexpr uint32_t TASK_STACK_AUDIO   = 4096;
constexpr uint32_t TASK_STACK_DISPLAY = 4096;

constexpr uint8_t TASK_PRIORITY_MOTOR   = 5;
constexpr uint8_t TASK_PRIORITY_CAMERA  = 3;
constexpr uint8_t TASK_PRIORITY_AUDIO   = 2;
constexpr uint8_t TASK_PRIORITY_DISPLAY = 2;

// ── Queue sizes ────────────────────────────────────────────────
constexpr uint8_t QUEUE_SIZE_MOTOR   = 8;
constexpr uint8_t QUEUE_SIZE_AUDIO   = 4;
constexpr uint8_t QUEUE_SIZE_DISPLAY = 4;

// ── Speaker ──────────────────────────────────────────────────
constexpr uint8_t SPEAKER_DEFAULT_VOLUME = 128;  // 0-255

// ── Watchdog ───────────────────────────────────────────────────
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 3000;  // NOTE: Phase 2 で実環境テスト時に調整

// taskMotorControl のキュー待ちタイムアウト
// WATCHDOG_TIMEOUT_MS の 1/6 以下に設定すること（最大発火遅延 = WATCHDOG_TIMEOUT_MS + MOTOR_WATCHDOG_POLL_MS）
constexpr uint32_t MOTOR_WATCHDOG_POLL_MS = 500;

// ── Network ────────────────────────────────────────────────────
constexpr uint16_t HTTP_PORT   = 80;
constexpr uint16_t STREAM_PORT = 81;

// ── SD card ──────────────────────────────────────────────────────
// NOTE: CS ピンはボードリビジョンで異なる場合あり。SD 認識失敗時はここと
//       SPI.begin() の要否を確認のこと (M5.begin() 後は通常不要)
constexpr uint8_t SD_CS_PIN = 4;  // CoreS3 built-in SD slot

// ── Web Server ───────────────────────────────────────────────────
constexpr size_t MAX_JSON_BODY_SIZE   = 196608;  // POST /command 最大ボディサイズ (192KB)
constexpr size_t MAX_AUDIO_BASE64_LEN = 131072;  // Base64 音声データ上限 (128KB → ~96KB PCM)

// ── WiFi ───────────────────────────────────────────────────────
constexpr const char* WIFI_CONFIG_PATH       = "/wifi.txt";
constexpr uint32_t    WIFI_CONNECT_TIMEOUT_MS = 10000;  // Single connection attempt timeout
constexpr uint32_t    WIFI_RETRY_INTERVAL_MS  = 5000;

// ── Camera (GC0308 on Core S3) ────────────────────────────────
constexpr int CAM_PIN_PWDN  = -1;
constexpr int CAM_PIN_RESET = -1;
constexpr int CAM_PIN_XCLK  = -1;   // Core S3: XCLK 未接続
constexpr int CAM_PIN_SIOD  = 12;   // 内部 I2C SDA (AXP2101/AW9523 と共有)
constexpr int CAM_PIN_SIOC  = 11;   // 内部 I2C SCL
constexpr int CAM_PIN_D7    = 47;
constexpr int CAM_PIN_D6    = 48;
constexpr int CAM_PIN_D5    = 16;
constexpr int CAM_PIN_D4    = 15;
constexpr int CAM_PIN_D3    = 42;
constexpr int CAM_PIN_D2    = 41;
constexpr int CAM_PIN_D1    = 40;
constexpr int CAM_PIN_D0    = 39;
constexpr int CAM_PIN_VSYNC = 46;
constexpr int CAM_PIN_HREF  = 38;
constexpr int CAM_PIN_PCLK  = 45;

constexpr int      CAM_XCLK_FREQ_HZ    = 20000000;   // 20MHz
constexpr uint8_t  CAM_DEFAULT_QUALITY  = 12;          // JPEG quality (10-63, 低=高画質)
constexpr uint32_t CAM_FRAME_DELAY_MS   = 33;          // ~30fps キャップ
constexpr uint32_t CAM_HTTPD_STACK_SIZE = 16384;       // httpd タスクスタック (frame2jpg が大きなスタックを要求)

// ── Microphone ──────────────────────────────────────────────────
constexpr uint32_t TASK_STACK_MIC          = 8192;
constexpr uint8_t  TASK_PRIORITY_MIC       = 3;
constexpr uint8_t  QUEUE_SIZE_MIC          = 1;      // 同時録音は 1 つのみ
constexpr float    MIC_MIN_DURATION        = 0.5f;   // 秒
constexpr float    MIC_MAX_DURATION        = 3.0f;   // 秒 (Base64 後 ~128KB 以内)
constexpr uint32_t MIC_DEFAULT_SAMPLE_RATE = 16000;

}  // namespace mova

#endif  // MOVA_CONFIG_H
