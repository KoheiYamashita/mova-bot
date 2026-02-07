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
//   Motor 1: CH3=BIN1, CH4=BIN2, CH5=PWMB  (TB6612FNG #1 B側)
//   Motor 2: CH6=AIN1, CH7=AIN2, CH8=PWMA  (TB6612FNG #2 A側)
//   Motor 3: CH9=BIN1, CH10=BIN2, CH11=PWMB (TB6612FNG #2 B側)
constexpr MotorPinMap MOTOR_PINS[4] = {
    { 0,  1,  2},  // Motor 0
    { 3,  4,  5},  // Motor 1
    { 6,  7,  8},  // Motor 2
    { 9, 10, 11},  // Motor 3
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
constexpr size_t MAX_JSON_BODY_SIZE   = 65536;  // POST /command 最大ボディサイズ (64KB)
constexpr size_t MAX_AUDIO_BASE64_LEN = 49152;  // Base64 音声データ上限 (48KB → ~36KB PCM)

// ── WiFi ───────────────────────────────────────────────────────
constexpr const char* WIFI_CONFIG_PATH       = "/wifi.txt";
constexpr uint32_t    WIFI_CONNECT_TIMEOUT_MS = 10000;  // Single connection attempt timeout
constexpr uint32_t    WIFI_RETRY_INTERVAL_MS  = 5000;

}  // namespace mova

#endif  // MOVA_CONFIG_H
