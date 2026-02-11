#include <M5Unified.h>

#include "config.h"
#include "wifi_config.h"
#include "motor_controller.h"
#include "obstacle_sensor.h"
#include "camera.h"
#include "display.h"
#include "emoji_data.h"
#include "audio_player.h"
#include "mic_recorder.h"
#include "web_server.h"

static mova::WiFiConfig g_wifi;
static mova::MotorController g_motor;
static mova::MOVAWebServer g_webServer;

QueueHandle_t mova::g_motorQueue   = nullptr;
QueueHandle_t mova::g_displayQueue = nullptr;
QueueHandle_t mova::g_audioQueue   = nullptr;

static SemaphoreHandle_t g_wire1Mutex = nullptr;  // Wire1: M138 + M5.update()
static SemaphoreHandle_t g_portAMutex = nullptr;  // Wire (Port A): PaHUB2 + VL53L0X

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

    // --- Camera initialization ---
    showBootStatus("Init Camera...");
    if (!mova::cameraInit()) {
        Serial.println("[Camera] WARNING: Camera init failed - streaming disabled");
        showBootStatus("Camera WARN", "Init failed");
        delay(2000);
    }

    // --- I2C Mutex creation ---
    g_wire1Mutex = xSemaphoreCreateMutex();
    if (!g_wire1Mutex) {
        fatalError("Wire1 Mutex alloc failed");
    }

    g_portAMutex = xSemaphoreCreateMutex();
    if (!g_portAMutex) {
        fatalError("PortA Mutex alloc failed");
    }

    // --- Motor initialization sequence ---
    showBootStatus("Init Motors...");

    if (!g_motor.begin(g_wire1Mutex)) {
        Serial.println("[Motor] WARNING: M138 init failed - motor control disabled");
        showBootStatus("Motor Init WARN", "M138 not found");
        delay(3000);
    }

    // Motor queue + task
    mova::g_motorQueue = xQueueCreate(mova::QUEUE_SIZE_MOTOR, sizeof(mova::MotorCommand));
    if (!mova::g_motorQueue) {
        fatalError("Motor queue alloc failed");
    }

    xTaskCreatePinnedToCore(
        mova::taskMotorControl, "MotorCtrl",
        mova::TASK_STACK_MOTOR, &g_motor,
        mova::TASK_PRIORITY_MOTOR, nullptr, 0);

    // --- Obstacle sensors (PaHUB2 + 4x VL53L0X) ---
    showBootStatus("Init Sensors...");
    if (!mova::obstacleInit(g_portAMutex, mova::g_motorQueue, &g_motor)) {
        Serial.println("[Sensor] WARNING: ToF init failed - obstacle detection disabled");
        showBootStatus("Sensor WARN", "ToF not found");
        delay(2000);
    } else {
        xTaskCreatePinnedToCore(
            mova::taskObstacleDetection, "ObstDet",
            mova::TASK_STACK_OBSTACLE, nullptr,
            mova::TASK_PRIORITY_OBSTACLE, nullptr, 0);
    }

    // --- Display / Audio queue + task setup ---
    mova::g_displayQueue = xQueueCreate(mova::QUEUE_SIZE_DISPLAY, sizeof(mova::DisplayCommand));
    if (!mova::g_displayQueue) {
        fatalError("Display queue alloc failed");
    }

    mova::g_audioQueue = xQueueCreate(mova::QUEUE_SIZE_AUDIO, sizeof(mova::AudioCommand));
    if (!mova::g_audioQueue) {
        fatalError("Audio queue alloc failed");
    }

    if (!mova::audioInit()) {
        Serial.println("[Audio] WARNING: Speaker init failed - audio playback disabled");
    }

    xTaskCreatePinnedToCore(
        mova::taskDisplay, "Display",
        mova::TASK_STACK_DISPLAY, nullptr,
        mova::TASK_PRIORITY_DISPLAY, nullptr, 1);

    xTaskCreatePinnedToCore(
        mova::taskAudioPlayback, "Audio",
        mova::TASK_STACK_AUDIO, nullptr,
        mova::TASK_PRIORITY_AUDIO, nullptr, 0);

    // --- Microphone initialization ---
    showBootStatus("Init Mic...");
    if (mova::micInit()) {
        xTaskCreatePinnedToCore(
            mova::taskMicRecording, "Mic",
            mova::TASK_STACK_MIC, nullptr,
            mova::TASK_PRIORITY_MIC, nullptr, 0);
        Serial.println("[Mic] Recording task started");
    } else {
        Serial.println("[Mic] WARNING: Mic init failed - recording disabled");
    }

    // --- Web server ---
    showBootStatus("Starting Web...");
    g_webServer.init(mova::g_motorQueue, mova::g_displayQueue,
                     mova::g_audioQueue, &g_motor);

    if (!g_webServer.begin()) {
        Serial.println("[Web] WARNING: Web server start failed");
    }

    // --- Stream server (port 81) ---
    if (mova::cameraIsInitialized()) {
        if (!mova::cameraStreamServerStart()) {
            Serial.println("[Camera] WARNING: Stream server start failed");
        }
    }

    showBootStatus("Connected", g_wifi.getIPAddress());
    printMemoryInfo();
    Serial.println("Boot complete.");

    // Show default emoji after boot (via queue so taskDisplay draws it)
    mova::DisplayCommand dcmd = {};
    dcmd.type = mova::DisplayCommand::EMOJI;
    dcmd.emojiIndex = mova::EMOJI_DEFAULT_INDEX;
    xQueueSend(mova::g_displayQueue, &dcmd, pdMS_TO_TICKS(100));
}

void loop() {
    // Wire1 mutex: protect M5.update() from motor I2C contention
    if (xSemaphoreTake(g_wire1Mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        M5.update();
        xSemaphoreGive(g_wire1Mutex);
    }
    delay(100);
}
