#include "src/WiFiManager.h"
#include <Arduino.h>
#include "src/event_bus.h"

WiFiManager::WiFiManager(HardwareInterface* hw) : hardware(hw), _lastConnectAttempt(0), _isAPMode(false), _lastTick(0) {
    memset(_ssid, 0, sizeof(_ssid));
    memset(_password, 0, sizeof(_password));
    memset(_deviceName, 0, sizeof(_deviceName));
}

void WiFiManager::begin(const Configuration* config) {
    if (!config) return;

    generateDeviceName(config->identifier);
    subscribeToWiFiConfigEvents();
    
    // 复制字符串到本地缓冲区，避免指针指向同一内存
    strlcpy(_ssid, config->wifi_ssid, sizeof(_ssid));
    strlcpy(_password, config->wifi_pass, sizeof(_password));

    if (isSSIDEmpty()) {
        startAPMode();
        return;
    }

    Serial.print(F("WiFi: "));
    Serial.println(_ssid);
    WiFi.mode(WIFI_STA);
    applyNetworkConfig(config);
}

void WiFiManager::generateDeviceName(const char* identifier) {
    if (!identifier || strlen(identifier) != 4) {
        snprintf(_deviceName, sizeof(_deviceName), "OpenUPS-0000");
        return;
    }
    snprintf(_deviceName, sizeof(_deviceName), "OpenUPS-%s", identifier);
}

void WiFiManager::startAPMode(const char* apPassword) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_deviceName, apPassword);
    Serial.print(F("AP: "));
    Serial.println(WiFi.softAPIP());
    _isAPMode = true;
    updateLEDStatus();
}

void WiFiManager::loop() {
    unsigned long now = millis();

    // 每秒执行一次周期性检查（包括 LED 更新和重连检测）
    if (now - _lastTick < TICK_INTERVAL) {
        return;
    }
    _lastTick = now;

    // AP 模式：仅更新 LED 状态
    if (_isAPMode) {
        updateLEDStatus();
        return;
    }

    wl_status_t status = WiFi.status();

    // 已连接：更新 LED 状态
    if (status == WL_CONNECTED) {
        updateLEDStatus();
        return;
    }

    // 以下状态允许重连尝试
    bool canReconnect = (status == WL_IDLE_STATUS || 
                         status == WL_NO_SSID_AVAIL || 
                         status == WL_CONNECT_FAILED || 
                         status == WL_CONNECTION_LOST);

    if (canReconnect && _ssid && now - _lastConnectAttempt >= RECONNECT_INTERVAL) {
        _lastConnectAttempt = now;
        WiFi.setHostname(_deviceName);
        WiFi.begin(_ssid, _password);
    }

    // 更新 LED 状态显示当前连接情况
    updateLEDStatus();
}

void WiFiManager::applyNetworkConfig(const Configuration* config) {
    
    if (config && config->use_static_ip) {
        IPAddress ip, gw, sn, dns;
        ip.fromString(config->static_ip);
        gw.fromString(config->static_gateway);
        sn.fromString(config->static_subnet);
        dns.fromString(config->static_dns);
        WiFi.config(ip, gw, sn, dns);
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    Serial.print(F("Connect: "));
    Serial.println(_ssid);
    WiFi.setHostname(_deviceName);
    WiFi.begin(_ssid, _password);
    _lastConnectAttempt = millis();
    updateLEDStatus();
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    _lastConnectAttempt = 0;
    updateLEDStatus();
}

void WiFiManager::updateLEDStatus() {
    if (!hardware) return;
    bool connected = WiFi.status() == WL_CONNECTED;
    hardware->setLED(WIFI_SUCCESS_LED_PIN, connected ? LED_MODE_ON : LED_MODE_OFF);
    hardware->setLED(WIFI_FAIL_LED_PIN, connected ? LED_MODE_OFF : (_isAPMode ? LED_MODE_BLINK_SLOW : LED_MODE_BLINK_FAST));
}

void WiFiManager::subscribeToWiFiConfigEvents() {
    EventBus::getInstance().subscribe(EVT_CONFIG_WIFI_CHANGED, onWiFiConfigChanged);
}

void WiFiManager::onWiFiConfigChanged(EventType, void* param) {
    if (!param) return;

    extern WiFiManager* wifiManager;
    if (wifiManager == nullptr) {
        Serial.println(F("[EVENT] ERROR: Wifimanager is null"));
        return;
    }

    wifiManager->handleWiFiConfigChange(static_cast<Configuration*>(param));

}

void WiFiManager::handleWiFiConfigChange(const Configuration* newConfig) {
    if (!newConfig) return;

    // 检查配置是否真正变化（任一字段不同即需重连）
    bool ssidChanged = (strcmp(_ssid, newConfig->wifi_ssid) != 0);
    bool passChanged = (strcmp(_password, newConfig->wifi_pass) != 0);

    if (!ssidChanged && !passChanged) {
        Serial.println(F("[WiFiMgr] WiFi config unchanged, skip reconnect"));
        return;
    }

    Serial.println(F("[WiFiMgr] WiFi config changed, applying new settings..."));

    // 复制新配置到本地缓冲区
    strlcpy(_ssid, newConfig->wifi_ssid, sizeof(_ssid));
    strlcpy(_password, newConfig->wifi_pass, sizeof(_password));
    
    _isAPMode = false;
    _lastConnectAttempt = 0;

    // 如果 SSID 为空，切换到 AP 模式
    if (isSSIDEmpty()) {
        Serial.println(F("[WiFiMgr] SSID is empty, switching to AP mode"));
        startAPMode();
        return;
    }

    // 先断开现有连接（延迟 500ms 清理资源）
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("[WiFiMgr] Disconnecting from current WiFi..."));
        WiFi.disconnect(true);
    }

    // 切换到 STA 模式并应用新配置
    WiFi.mode(WIFI_STA);
    applyNetworkConfig(newConfig);
}
