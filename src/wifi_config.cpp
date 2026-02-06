#include "wifi_config.h"

namespace mova {

bool WiFiConfig::begin() {
    // TODO: Phase 1 - Load WiFi credentials from LittleFS and connect
    return false;
}

const char* WiFiConfig::getIPAddress() const {
    // TODO: Phase 1
    return ipAddress_;
}

bool WiFiConfig::isConnected() const {
    // TODO: Phase 1
    return false;
}

}  // namespace mova
