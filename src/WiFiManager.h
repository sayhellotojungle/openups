#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include "src/pins_config.h"
#include "src/hardware_interface.h"

class WiFiManager {
public:
    WiFiManager(HardwareInterface* hw);
    void begin(const Configuration* config);
    void startAPMode(const char* apPassword = "12345678");
    void loop();
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    String getIPAddress() const { return isConnected() ? WiFi.localIP().toString() : ""; }
    void disconnect();
    void subscribeToWiFiConfigEvents();

private:
    HardwareInterface* hardware;
    char _ssid[33];      // WiFi SSID 最大 32 字节 + '\0'
    char _password[65];  // WiFi 密码最大 64 字节 + '\0'
    char _deviceName[16];
    bool _isAPMode;
    unsigned long _lastConnectAttempt;
    unsigned long _lastTick;  // 上次周期性检查时间
    
    static constexpr unsigned long RECONNECT_INTERVAL = 10000;
    static constexpr unsigned long TICK_INTERVAL = 1000;  // 周期性检查间隔 1s，减少发热

    void updateLEDStatus();
    void applyNetworkConfig(const Configuration* config = nullptr);
    void generateDeviceName(const char* identifier);
    bool isSSIDEmpty() const { return strlen(_ssid) == 0; }
    static void onWiFiConfigChanged(EventType, void* param);
    void handleWiFiConfigChange(const Configuration* newConfig);
};

#endif // WIFI_MANAGER_H
