#ifndef MOVA_WIFI_CONFIG_H
#define MOVA_WIFI_CONFIG_H

#include <cstdint>

namespace mova {

class WiFiConfig {
public:
    /// SD カードから /wifi.txt を読み込み SSID/PASS を取得
    /// @return true=読み込み成功, false=SD エラーまたはファイル不正
    bool loadFromSD();

    /// WiFi STA モードで接続試行 (1回、タイムアウト付き)
    /// @return true=接続成功, false=タイムアウト
    bool connect();

    const char* getIPAddress() const;
    bool isConnected() const;
    const char* getSSID() const;

private:
    char ssid_[33] = {};      // max 32 chars + null
    char password_[65] = {};  // max 64 chars + null
    char ipAddress_[16] = {};
};

}  // namespace mova

#endif  // MOVA_WIFI_CONFIG_H
