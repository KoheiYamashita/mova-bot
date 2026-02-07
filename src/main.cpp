#include <M5Unified.h>
#include <Wire.h>

#include "config.h"
#include "wifi_config.h"
#include "motor_controller.h"
#include "display.h"
#include "audio_player.h"
#include "web_server.h"

static mova::WiFiConfig g_wifi;
static mova::MotorController g_motor;
static mova::MOVAWebServer g_webServer;

QueueHandle_t mova::g_motorQueue   = nullptr;
QueueHandle_t mova::g_displayQueue = nullptr;
QueueHandle_t mova::g_audioQueue   = nullptr;

static SemaphoreHandle_t g_i2cMutex = nullptr;

static void printBanner() {
    Serial.println("========================================");
    Serial.println(" MOVA - 4D Omni-Wheel RC");
    Serial.printf(" Firmware v%s\n", mova::FIRMWARE_VERSION);
    Serial.println("========================================");
}

static void showBootStatus(const char* line1, const char* line2 = nullptr) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(middle_center);

    int16_t cx = M5.Display.width() / 2;
    int16_t cy = M5.Display.height() / 2;

    if (line2) {
        M5.Display.drawString(line1, cx, cy - 16);
        M5.Display.drawString(line2, cx, cy + 16);
    } else {
        M5.Display.drawString(line1, cx, cy);
    }
}

static void printMemoryInfo() {
    Serial.printf("PSRAM: %d / %d bytes free\n", ESP.getFreePsram(), ESP.getPsramSize());
    Serial.printf("Heap:  %d bytes free\n", ESP.getFreeHeap());
}

static void fatalError(const char* msg) {
    Serial.printf("[FATAL] %s\n", msg);
    showBootStatus("FATAL ERROR", msg);
    for (;;) { delay(1000); }
}

void setup() {
    delay(2000);  // Wait for USB CDC enumeration
    Serial.begin(115200);

    // Disable internal I2C devices to avoid Wire bus contention
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);
    printBanner();
    showBootStatus("MOVA Booting...");
    printMemoryInfo();

    // --- WiFi connection sequence ---
    showBootStatus("Loading WiFi...");

    while (!g_wifi.loadFromSD()) {
        showBootStatus("SD Error", "Insert SD with wifi.txt");
        delay(mova::WIFI_RETRY_INTERVAL_MS);
    }

    showBootStatus("Connecting WiFi...", g_wifi.getSSID());

    while (!g_wifi.connect()) {
        showBootStatus("WiFi Failed", "Retrying...");
        delay(mova::WIFI_RETRY_INTERVAL_MS);
    }

    showBootStatus("Connected", g_wifi.getIPAddress());
    Serial.printf("IP: %s\n", g_wifi.getIPAddress());

    // --- Motor initialization sequence ---
    // 生成順序に依存関係あり: Mutex → begin() → Queue → Task
    showBootStatus("Init Motors...");

    // 1. Mutex 生成 (FATAL: 失敗時は停止)
    g_i2cMutex = xSemaphoreCreateMutex();
    if (!g_i2cMutex) {
        fatalError("I2C Mutex alloc failed");
    }

    // 2. MotorController 初期化 (WARNING: 失敗時は続行)
    if (!g_motor.begin(g_i2cMutex)) {
        Serial.println("[Motor] WARNING: PCA9685 init failed - motor control disabled");
        showBootStatus("Motor Init WARN", "PCA9685 not found");
        delay(3000);
    }

    // 3. キュー生成 (FATAL: 失敗時は停止)
    mova::g_motorQueue = xQueueCreate(mova::QUEUE_SIZE_MOTOR, sizeof(mova::MotorCommand));
    if (!mova::g_motorQueue) {
        fatalError("Motor queue alloc failed");
    }

    // 4. モータータスク起動
    xTaskCreatePinnedToCore(
        mova::taskMotorControl, "MotorCtrl",
        mova::TASK_STACK_MOTOR, &g_motor,
        mova::TASK_PRIORITY_MOTOR, nullptr, 0);

    // --- Display / Audio queue + task setup ---
    mova::g_displayQueue = xQueueCreate(mova::QUEUE_SIZE_DISPLAY, sizeof(mova::DisplayCommand));
    if (!mova::g_displayQueue) {
        fatalError("Display queue alloc failed");
    }

    mova::g_audioQueue = xQueueCreate(mova::QUEUE_SIZE_AUDIO, sizeof(mova::AudioCommand));
    if (!mova::g_audioQueue) {
        fatalError("Audio queue alloc failed");
    }

    xTaskCreatePinnedToCore(
        mova::taskDisplay, "Display",
        mova::TASK_STACK_DISPLAY, nullptr,
        mova::TASK_PRIORITY_DISPLAY, nullptr, 1);

    xTaskCreatePinnedToCore(
        mova::taskAudioPlayback, "Audio",
        mova::TASK_STACK_AUDIO, nullptr,
        mova::TASK_PRIORITY_AUDIO, nullptr, 0);

    // --- Web server ---
    showBootStatus("Starting Web...");
    g_webServer.init(mova::g_motorQueue, mova::g_displayQueue,
                     mova::g_audioQueue, &g_motor);

    if (!g_webServer.begin()) {
        Serial.println("[Web] WARNING: Web server start failed");
    }

    showBootStatus("Connected", g_wifi.getIPAddress());
    printMemoryInfo();
    Serial.println("Boot complete.");
}

void loop() {
    M5.update();
    delay(100);
}
