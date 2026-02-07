#include <M5Unified.h>
#include <Wire.h>

#include "config.h"
#include "wifi_config.h"

static mova::WiFiConfig g_wifi;

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

static void probeI2C() {
    Wire.begin(mova::I2C_SDA, mova::I2C_SCL);
    Wire.beginTransmission(mova::PCA9685_ADDRESS);
    uint8_t result = Wire.endTransmission();

    switch (result) {
        case 0:
            Serial.printf("PCA9685 found at 0x%02X\n", mova::PCA9685_ADDRESS);
            break;
        case 2:
            Serial.printf("PCA9685 NACK at 0x%02X (device not responding)\n", mova::PCA9685_ADDRESS);
            break;
        case 1:
            Serial.printf("I2C error: data too long for buffer\n");
            break;
        case 3:
            Serial.printf("I2C error: NACK on data transmit\n");
            break;
        case 4:
            Serial.printf("I2C error: bus error (check SDA/SCL wiring)\n");
            break;
        default:
            Serial.printf("I2C error: unknown code %d\n", result);
            break;
    }
}

static void printMemoryInfo() {
    Serial.printf("PSRAM: %d / %d bytes free\n", ESP.getFreePsram(), ESP.getPsramSize());
    Serial.printf("Heap:  %d bytes free\n", ESP.getFreeHeap());
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
    probeI2C();
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

    Serial.println("Boot complete.");
}

void loop() {
    M5.update();
    delay(100);
}
