#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "data_structures.h"
#include "config_manager.h"

// Forward declaration
class SystemManagement;
 
class WebServer {
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    ConfigManager* configManager;
    SystemManagement* systemManager;  // 添加SystemManagement指针

    // HTML 页面内容
    const char* spa_html;

    // 辅助函数声明
    void setupHttpRoutes();
    bool updateConfigurationFromRequest(const JsonDocument& doc);
    void sendErrorResponse(AsyncWebServerRequest *request, const String& message, int code);
    void sendConfigModeResponse(AsyncWebServerRequest *request);

    // SPA 页面渲染
    void renderSPA(AsyncWebServerRequest *request);
    
public:
    WebServer(ConfigManager* configMgr, SystemManagement* sysMgr, int port = 80);
    ~WebServer();
    bool begin();
    void notifyClients();
    
    // 页面处理函数
    void handleRoot(AsyncWebServerRequest *request);
    void handleSaveConfig(AsyncWebServerRequest *request);
    
    // API 处理函数
    void handleApiRequest(AsyncWebServerRequest* request, const char* route);
    void buildStatusResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    void buildBmsResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    void buildPowerResponse(DynamicJsonDocument& doc, const System_Global_State& state);
    
    // Prometheus metrics
    void handleMetricsRequest(AsyncWebServerRequest* request);

    // OTA update handlers
    void handleFirmwareUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);

    // BMS Ship Mode handler
    void handleBmsShipMode(AsyncWebServerRequest* request);

    // ADC Calibration API handlers
    void handleCalibrationGet(AsyncWebServerRequest* request);
    void handleCalibrationPost(AsyncWebServerRequest* request);

    // 辅助函数
    void replaceStringInBuffer(char* buffer, size_t bufferSize, const char* search, const char* replace, char* tempBuffer);

    // WebSocket 事件处理
    void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len);
};

#endif