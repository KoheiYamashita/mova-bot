#ifndef MOVA_CONFIG_H
#define MOVA_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace mova {

// ── Firmware ────────────────────────────────────────────────────
constexpr const char* FIRMWARE_VERSION = "0.1.0";

// ── I2C (Core S3 Port A) ───────────────────────────────────────
// NOTE: GPIO1/2 は要実機検証 (Port A ピン配置を確認のこと)
constexpr uint8_t I2C_SDA = 2;
constexpr uint8_t I2C_SCL = 1;

// ── PCA9685 ────────────────────────────────────────────────────
constexpr uint8_t  PCA9685_ADDRESS  = 0x40;
constexpr uint16_t PCA9685_PWM_FREQ = 1000;  // NOTE: Phase 2 で発熱・ノイズ実測後に調整

// ── Motor pin mapping (PCA9685 channels) ───────────────────────
struct MotorPinMap {
    uint8_t in1;
    uint8_t in2;
    uint8_t pwm;
};

// PCA9685 チャンネルマッピング (docs/wiring.md 準拠)
// 各モーター: IN1, IN2, PWM の順
//   Motor 0: CH0=AIN1, CH1=AIN2, CH2=PWMA  (TB6612FNG #1 A側)
//   Motor 1: CH4=BIN2, CH3=BIN1, CH5=PWMB  (TB6612FNG #1 B側 — B出力が B02,B01 逆順のため IN1/IN2 swap)
//   Motor 2: CH6=AIN1, CH7=AIN2, CH8=PWMA  (TB6612FNG #2 A側)
//   Motor 3: CH10=BIN2, CH9=BIN1, CH11=PWMB (TB6612FNG #2 B側 — B出力が B02,B01 逆順のため IN1/IN2 swap)
constexpr MotorPinMap MOTOR_PINS[4] = {
    { 0,  1,  2},  // Motor 0 (A側: そのまま)
    { 4,  3,  5},  // Motor 1 (B側: IN1/IN2 swapped)
    { 6,  7,  8},  // Motor 2 (A側: そのまま)
    {10,  9, 11},  // Motor 3 (B側: IN1/IN2 swapped)
};

// TB6612FNG standby channels
// NOTE: PCA9685 起動直後の出力は不定。外付けプルダウン抵抗を推奨 (Phase 2)
constexpr uint8_t STBY_CH_1 = 12;
constexpr uint8_t STBY_CH_2 = 13;

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
