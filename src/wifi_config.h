#ifndef MOVA_WIFI_CONFIG_H
#define MOVA_WIFI_CONFIG_H

#include <cstdint>

namespace mova {

class WiFiConfig {
public:
    bool begin();
    const char* getIPAddress() const;
    bool isConnected() const;

private:
    char ipAddress_[16] = {};
};

}  // namespace mova

#endif  // MOVA_WIFI_CONFIG_H
