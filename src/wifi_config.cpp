#include "wifi_config.h"
#include "config.h"

#include <SD.h>
#include <WiFi.h>
#include <cstring>

namespace mova {

// Trim leading/trailing whitespace in-place, return pointer to trimmed start
static char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

bool WiFiConfig::loadFromSD() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[WiFi] SD.begin() failed");
        return false;
    }

    File file = SD.open(WIFI_CONFIG_PATH);
    if (!file) {
        Serial.printf("[WiFi] Cannot open %s\n", WIFI_CONFIG_PATH);
        SD.end();
        return false;
    }

    ssid_[0] = '\0';
    password_[0] = '\0';

    char line[128];
    while (file.available()) {
        int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';

        char* trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

        // Find '=' separator
        char* eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trim(trimmed);
        char* val = trim(eq + 1);

        if (strcmp(key, "SSID") == 0) {
            strncpy(ssid_, val, sizeof(ssid_) - 1);
            ssid_[sizeof(ssid_) - 1] = '\0';
        } else if (strcmp(key, "PASS") == 0) {
            strncpy(password_, val, sizeof(password_) - 1);
            password_[sizeof(password_) - 1] = '\0';
        }
    }

    file.close();
    SD.end();

    if (ssid_[0] == '\0') {
        Serial.println("[WiFi] SSID not found in wifi.txt");
        return false;
    }

    Serial.printf("[WiFi] Loaded SSID: %s\n", ssid_);
    return true;
}

bool WiFiConfig::connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_, password_);

    Serial.printf("[WiFi] Connecting to %s ...\n", ssid_);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[WiFi] Connection timed out");
            WiFi.disconnect(true, true);
            return false;
        }
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipAddress_, sizeof(ipAddress_));
    Serial.printf("[WiFi] Connected, IP: %s\n", ipAddress_);
    return true;
}

const char* WiFiConfig::getIPAddress() const {
    return ipAddress_;
}

bool WiFiConfig::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

const char* WiFiConfig::getSSID() const {
    return ssid_;
}

}  // namespace mova
