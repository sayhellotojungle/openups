#include "web_server.h"
#include "data_structures.h"
#include "config_manager.h"
#include "templates/html_templates.h"
#include "system_management.h"
#include "event_bus.h"
#include "event_types.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <Ticker.h>


// OTA 配置 —— 修改项目名或最低版本时只需改这里
#define EXPECTED_SIG_PREFIX   "SIG:OPENUPS-ESP32S3:VER:"
#define MIN_REQUIRED_VERSION  "1.0.0"

// OTA 结果跟踪标志 (upload handler 与 request handler 共享)
static bool s_otaSuccess = false;

static WebServer* g_webServerInstance = nullptr;

// =============================================================================
// Constructor and Destructor
// =============================================================================

WebServer::WebServer(ConfigManager* configMgr, SystemManagement* sysMgr, int port) 
    : server(port), ws("/ws"), configManager(configMgr), systemManager(sysMgr) {
  spa_html = SPA_PAGE_TEMPLATE;
  
  if (systemManager == nullptr) {
    Serial.println(F("WebServer: CONFIG_MODE (no systemManager)"));
  } else {
    Serial.println(F("WebServer: NORMAL_MODE"));
  }
}

WebServer::~WebServer() {
  if (g_webServerInstance == this) {
    g_webServerInstance = nullptr;
  }
  Serial.println(F("WebServer: Destructor called"));
}

// =============================================================================
// Server Setup
// =============================================================================

bool WebServer::begin() {
  ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                    AwsEventType type, void* arg, uint8_t* data, size_t len) {
    this->onWsEvent(server, client, type, arg, data, len);
  });
  
  server.addHandler(&ws);
  setupHttpRoutes();
  g_webServerInstance = this;
  server.begin();
  
  Serial.println(F("Web server started on port 80"));
  return true;
}

void WebServer::setupHttpRoutes() {
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleRoot(request);
  });

  // /config and /update redirect to SPA root
  auto redirectToRoot = [this](AsyncWebServerRequest* request) {
    request->redirect("/");
  };
  server.on("/config", HTTP_GET, redirectToRoot);
  server.on("/update", HTTP_GET, redirectToRoot);

  server.on("/save", HTTP_POST, 
    [this](AsyncWebServerRequest* request) {
      this->handleSaveConfig(request);
    },
    NULL,
    [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
      if (!request->_tempObject) {
        request->_tempObject = new String("");
      }
      String* body = (String*)request->_tempObject;
      for (size_t i = 0; i < len; i++) {
        body->concat((char)data[i]);
      }
    }
  );
  
  // API endpoints - conditional based on systemManager
  const char* apiRoutes[] = {"/api/status", "/api/bms", "/api/power"};
  for (const char* route : apiRoutes) {
    server.on(route, HTTP_GET, [this, route](AsyncWebServerRequest* request) {
      if (systemManager == nullptr) {
        sendConfigModeResponse(request);
        return;
      }
      handleApiRequest(request, route);
    });
  }
  
  // BMS Ship Mode endpoint
  server.on("/bms/shipmode", HTTP_POST, [this](AsyncWebServerRequest* request) {
    this->handleBmsShipMode(request);
  });
  
  // ADC Calibration API - GET
  server.on("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleCalibrationGet(request);
  });
  
  // ADC Calibration API - POST
  server.on("/api/calibration", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      this->handleCalibrationPost(request);
    },
    NULL,
    [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
      if (!request->_tempObject) {
        request->_tempObject = new String("");
      }
      String* body = (String*)request->_tempObject;
      for (size_t i = 0; i < len; i++) {
        body->concat((char)data[i]);
      }
    }
  );
  
  // Prometheus metrics endpoint
  server.on("/metrics", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (systemManager == nullptr) {
      sendConfigModeResponse(request);
      return;
    }
    handleMetricsRequest(request);
  });
  
  // OTA firmware upload route
  server.on("/firmware", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      // Upload complete handler — 仅根据 s_otaSuccess 判断是否真正写入成功
      Serial.printf_P(PSTR("[OTA-REQ] s_otaSuccess=%d hasError=%d\n"), s_otaSuccess, Update.hasError());
      if (!s_otaSuccess || Update.hasError()) {
        StaticJsonDocument<256> doc;
        doc["success"] = false;
        doc["message"] = Update.hasError() ? Update.errorString() : "OTA verification or write failed";
        String response;
        serializeJson(doc, response);
        request->send(500, "application/json", response);
        Serial.println(F("OTA: 升级失败，固件未写入。"));
      } else {
        StaticJsonDocument<256> doc;
        doc["success"] = true;
        doc["message"] = "Firmware update successful. Device will reboot in 3 seconds...";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);

        Serial.println(F("==========================================="));
        Serial.println(F("OTA update successful! Rebooting in 3 seconds..."));
        Serial.println(F("==========================================="));

        static Ticker rebootTicker;
        rebootTicker.once(3, []() {
          Serial.println(F("Rebooting now..."));
          ESP.restart();
        });
      }
    },
    [this](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      // Upload handler
      this->handleFirmwareUpload(request, filename, index, data, len, final);
    }
  );
  
  Serial.println(systemManager ? F("API routes: NORMAL_MODE") : F("API routes: CONFIG_MODE (stubs)"));
}

// =============================================================================
// WebSocket Event Handling
// =============================================================================

void WebServer::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
    if (systemManager != nullptr) {
      notifyClients();
    } else {
      StaticJsonDocument<256> doc;
      doc["status"] = "config_mode";
      doc["message"] = "Hardware modules not initialized";
      doc["timestamp"] = millis();
      
      char buffer[512];
      serializeJson(doc, buffer);
      client->text(buffer);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    // 处理客户端消息
    String msg = "";
    for (size_t i = 0; i < len; i++) {
      msg += (char)data[i];
    }
    if (msg == "get" && systemManager != nullptr) {
      notifyClients();
    }
  }
}

// =============================================================================
// Client Notification
// =============================================================================

void WebServer::notifyClients() {
  if (systemManager == nullptr) {
    StaticJsonDocument<256> doc;
    doc["status"] = "config_mode";
    doc["message"] = "Hardware modules not initialized";
    doc["timestamp"] = millis();
    
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    ws.textAll(jsonBuffer);
    return;
  }
  
  if (ws.count() == 0) return;
  
  const System_Global_State& state = systemManager->getGlobalState();
  StaticJsonDocument<2048> doc;
  
  doc["status"] = "connected";
  doc["timestamp"] = millis();
  doc["overall_status"] = state.overall_status;
  doc["power_mode"] = state.power_mode;
  doc["emergency_shutdown"] = state.emergency_shutdown;
  
  // BMS data - 完整数据
  JsonObject bms = doc.createNestedObject("bms");
  bms["soc"] = state.bms.soc;
  bms["soh"] = state.bms.soh;
  bms["voltage"] = state.bms.voltage;
  bms["current"] = state.bms.current;
  bms["temperature"] = state.bms.temperature;
  bms["cycle_count"] = state.bms.cycle_count;
  bms["capacity_remaining"] = state.bms.capacity_remaining;
  bms["is_connected"] = state.bms.is_connected;
  bms["bms_mode"] = state.bms.bms_mode;
  bms["fault_type"] = state.bms.fault_type;
  bms["balancing_active"] = state.bms.balancing_active;
  bms["balance_mask"] = state.bms.balance_mask;
  
  // 单体电压数组
  JsonArray cells = bms.createNestedArray("cell_voltages");
  for (int i = 0; i < 5; i++) {
    cells.add(state.bms.cell_voltages[i]);
  }
  bms["cell_voltage_min"] = state.bms.cell_voltage_min;
  bms["cell_voltage_max"] = state.bms.cell_voltage_max;
  bms["cell_voltage_avg"] = state.bms.cell_voltage_avg;
  
  // BQ76920 寄存器数组
  JsonArray bq76920_regs = bms.createNestedArray("bq76920_registers");
  for (int i = 0; i < 12; i++) {
    bq76920_regs.add(state.bms.bq76920_registers[i]);
  }
  
  // Power data - 完整数据
  JsonObject power = doc.createNestedObject("power");
  power["input_voltage"] = state.power.input_voltage;
  power["input_current"] = state.power.input_current;
  power["output_power"] = state.power.output_power;
  power["battery_voltage"] = state.power.battery_voltage;
  power["battery_current"] = state.power.battery_current;
  power["ac_present"] = state.power.ac_present;
  power["charger_enabled"] = state.power.charger_enabled;
  power["hybrid_mode"] = state.power.hybrid_mode;
  power["fault_type"] = state.power.fault_type;
  power["bq24780s_connected"] = state.power.bq24780s_connected;
  power["prochot_status"] = state.power.prochot_status;
  power["tbstat_status"] = state.power.tbstat_status;
  
  // BQ24780S 寄存器数组
  JsonArray bq24780s_regs = power.createNestedArray("bq24780s_registers");
  for (int i = 0; i < 11; i++) {
    bq24780s_regs.add(state.power.bq24780s_registers[i]);
  }
  
  // System data - 完整数据
  JsonObject system = doc.createNestedObject("system");
  system["uptime"] = state.system.uptime;
  system["wifi_connected"] = state.system.wifi_connected;
  system["wifi_ssid"] = state.system.wifi_ssid;
  system["wifi_rssi"] = state.system.wifi_rssi;
  system["board_temperature"] = state.system.board_temperature;
  system["environment_temperature"] = state.system.environment_temperature;
  system["firmware_version"] = state.system.firmware_version;
  
  char jsonBuffer[2048];
  serializeJson(doc, jsonBuffer);
  ws.textAll(jsonBuffer);
}

// =============================================================================
// API Endpoints
// =============================================================================

void WebServer::handleApiRequest(AsyncWebServerRequest* request, const char* route) {
  if (systemManager == nullptr) {
    request->send(200, "application/json", "{\"status\":\"config_mode\",\"message\":\"Hardware modules not initialized\"}");
    return;
  }

  const System_Global_State& state = systemManager->getGlobalState();
  DynamicJsonDocument doc(1024);
  
  if (strcmp(route, "/api/status") == 0) {
    buildStatusResponse(doc, state);
  } else if (strcmp(route, "/api/bms") == 0) {
    buildBmsResponse(doc, state);
  } else if (strcmp(route, "/api/power") == 0) {
    buildPowerResponse(doc, state);
  }
  
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::buildStatusResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = millis();
  doc["uptime"] = state.system.uptime;
  doc["overall_status"] = state.overall_status;
  doc["power_mode"] = state.power_mode;
  doc["emergency_shutdown"] = state.emergency_shutdown;
  
  JsonObject bms = doc.createNestedObject("bms");
  bms["soc"] = state.bms.soc;
  bms["voltage"] = state.bms.voltage;
  bms["current"] = state.bms.current;
  bms["temperature"] = state.bms.temperature;
  bms["is_connected"] = state.bms.is_connected;
  
  JsonObject power = doc.createNestedObject("power");
  power["ac_present"] = state.power.ac_present;
  power["charger_enabled"] = state.power.charger_enabled;
  power["battery_voltage"] = state.power.battery_voltage;
  power["hybrid_mode"] = state.power.hybrid_mode;
  
  JsonObject system = doc.createNestedObject("system");
  system["wifi_connected"] = state.system.wifi_connected;
  system["wifi_rssi"] = state.system.wifi_rssi;
  system["board_temperature"] = state.system.board_temperature;
}

void WebServer::buildBmsResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = state.bms.last_update_time;
  doc["soc"] = state.bms.soc;
  doc["soh"] = state.bms.soh;
  doc["voltage"] = state.bms.voltage;
  doc["current"] = state.bms.current;
  doc["temperature"] = state.bms.temperature;
  doc["is_connected"] = state.bms.is_connected;
  doc["bms_mode"] = state.bms.bms_mode;
  doc["fault_type"] = state.bms.fault_type;
  doc["cycle_count"] = state.bms.cycle_count;
  doc["capacity_remaining"] = state.bms.capacity_remaining;
  doc["balancing_active"] = state.bms.balancing_active;
  doc["balance_mask"] = state.bms.balance_mask;
  
  JsonArray cells = doc.createNestedArray("cell_voltages");
  for (int i = 0; i < 5; i++) {
    cells.add(state.bms.cell_voltages[i]);
  }
  doc["cell_voltage_min"] = state.bms.cell_voltage_min;
  doc["cell_voltage_max"] = state.bms.cell_voltage_max;
  doc["cell_voltage_avg"] = state.bms.cell_voltage_avg;
}

void WebServer::buildPowerResponse(DynamicJsonDocument& doc, const System_Global_State& state) {
  doc["timestamp"] = state.power.last_update_time;
  
  JsonObject input = doc.createNestedObject("input");
  input["voltage"] = state.power.input_voltage;
  input["current"] = state.power.input_current;
  
  JsonObject output = doc.createNestedObject("output");
  output["power"] = state.power.output_power;
  
  JsonObject battery = doc.createNestedObject("battery");
  battery["voltage"] = state.power.battery_voltage;
  battery["current"] = state.power.battery_current;
  
  doc["ac_present"] = state.power.ac_present;
  doc["charger_enabled"] = state.power.charger_enabled;
  doc["hybrid_mode"] = state.power.hybrid_mode;
  doc["fault_type"] = state.power.fault_type;
  doc["prochot_status"] = state.power.prochot_status;
  doc["tbstat_status"] = state.power.tbstat_status;
}

// =============================================================================
// Prometheus Metrics Endpoint
// =============================================================================

void WebServer::handleMetricsRequest(AsyncWebServerRequest* request) {
  if (systemManager == nullptr) {
    request->send(200, "text/plain", "# Prometheus metrics not available in config mode\n");
    return;
  }

  const System_Global_State& state = systemManager->getGlobalState();
  
  // Build Prometheus text format response
  String metrics = "";
  
  // System metrics
  metrics += "# HELP ups_system_uptime System uptime in seconds\n";
  metrics += "# TYPE ups_system_uptime gauge\n";
  metrics += "ups_system_uptime " + String(state.system.uptime) + "\n\n";
  
  metrics += "# HELP ups_system_overall_status Overall system status (0=normal, 1=warning, 2=fault)\n";
  metrics += "# TYPE ups_system_overall_status gauge\n";
  metrics += "ups_system_overall_status " + String(state.overall_status) + "\n\n";
  
  metrics += "# HELP ups_system_power_mode Power mode (0=AC, 1=BATTERY, 2=HYBRID, 3=CHARGING)\n";
  metrics += "# TYPE ups_system_power_mode gauge\n";
  metrics += "ups_system_power_mode " + String(state.power_mode) + "\n\n";
  
  metrics += "# HELP ups_system_emergency_shutdown Emergency shutdown status (0=normal, 1=shutdown)\n";
  metrics += "# TYPE ups_system_emergency_shutdown gauge\n";
  metrics += "ups_system_emergency_shutdown " + String(state.emergency_shutdown ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_board_temperature Board temperature in Celsius\n";
  metrics += "# TYPE ups_system_board_temperature gauge\n";
  metrics += "ups_system_board_temperature " + String(state.system.board_temperature, 2) + "\n\n";
  
  metrics += "# HELP ups_system_environment_temperature Environment temperature in Celsius\n";
  metrics += "# TYPE ups_system_environment_temperature gauge\n";
  metrics += "ups_system_environment_temperature " + String(state.system.environment_temperature, 2) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_connected WiFi connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_system_wifi_connected gauge\n";
  metrics += "ups_system_wifi_connected " + String(state.system.wifi_connected ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_rssi WiFi signal strength in dBm\n";
  metrics += "# TYPE ups_system_wifi_rssi gauge\n";
  metrics += "ups_system_wifi_rssi " + String(state.system.wifi_rssi) + "\n\n";
  
  metrics += "# HELP ups_system_wifi_status WiFi status code\n";
  metrics += "# TYPE ups_system_wifi_status gauge\n";
  metrics += "ups_system_wifi_status " + String(state.system.wifi_status) + "\n\n";
  
  metrics += "# HELP ups_system_led_brightness LED brightness (0-255)\n";
  metrics += "# TYPE ups_system_led_brightness gauge\n";
  metrics += "ups_system_led_brightness " + String(state.system.led_brightness) + "\n\n";
  
  metrics += "# HELP ups_system_buzzer_enabled Buzzer enabled status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_system_buzzer_enabled gauge\n";
  metrics += "ups_system_buzzer_enabled " + String(state.system.buzzer_enabled ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_system_buzzer_volume Buzzer volume (0-255)\n";
  metrics += "# TYPE ups_system_buzzer_volume gauge\n";
  metrics += "ups_system_buzzer_volume " + String(state.system.buzzer_volume) + "\n\n";
  
  metrics += "# HELP ups_system_hardware_version Hardware version\n";
  metrics += "# TYPE ups_system_hardware_version gauge\n";
  metrics += "ups_system_hardware_version{version=\"" + String(state.system.hardware_version) + "\"} 1\n\n";
  
  // BMS metrics
  metrics += "# HELP ups_bms_soc Battery state of charge percentage\n";
  metrics += "# TYPE ups_bms_soc gauge\n";
  metrics += "ups_bms_soc " + String(state.bms.soc, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_soh Battery state of health percentage\n";
  metrics += "# TYPE ups_bms_soh gauge\n";
  metrics += "ups_bms_soh " + String(state.bms.soh, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_voltage Battery voltage in millivolts\n";
  metrics += "# TYPE ups_bms_voltage gauge\n";
  metrics += "ups_bms_voltage " + String(state.bms.voltage) + "\n\n";
  
  metrics += "# HELP ups_bms_current Battery current in milliamperes (positive=charging, negative=discharging)\n";
  metrics += "# TYPE ups_bms_current gauge\n";
  metrics += "ups_bms_current " + String(state.bms.current) + "\n\n";
  
  metrics += "# HELP ups_bms_temperature Battery temperature in Celsius\n";
  metrics += "# TYPE ups_bms_temperature gauge\n";
  metrics += "ups_bms_temperature " + String(state.bms.temperature, 2) + "\n\n";
  
  metrics += "# HELP ups_bms_cycle_count Battery cycle count\n";
  metrics += "# TYPE ups_bms_cycle_count gauge\n";
  metrics += "ups_bms_cycle_count " + String(state.bms.cycle_count) + "\n\n";
  
  metrics += "# HELP ups_bms_capacity_full Full battery capacity in mAh\n";
  metrics += "# TYPE ups_bms_capacity_full gauge\n";
  metrics += "ups_bms_capacity_full " + String(state.bms.capacity_full) + "\n\n";
  
  metrics += "# HELP ups_bms_capacity_remaining Remaining battery capacity in mAh\n";
  metrics += "# TYPE ups_bms_capacity_remaining gauge\n";
  metrics += "ups_bms_capacity_remaining " + String(state.bms.capacity_remaining) + "\n\n";
  
  metrics += "# HELP ups_bms_connected BMS connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_bms_connected gauge\n";
  metrics += "ups_bms_connected " + String(state.bms.is_connected ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_bms_balancing_active Cell balancing active status (0=inactive, 1=active)\n";
  metrics += "# TYPE ups_bms_balancing_active gauge\n";
  metrics += "ups_bms_balancing_active " + String(state.bms.balancing_active ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_bms_fault_type BMS fault type (0=none, see BMS_Fault_t enum)\n";
  metrics += "# TYPE ups_bms_fault_type gauge\n";
  metrics += "ups_bms_fault_type " + String(state.bms.fault_type) + "\n\n";
  
  // Cell voltages
  metrics += "# HELP ups_bms_cell_voltage Individual cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage gauge\n";
  for (int i = 0; i < 5; i++) {
    metrics += "ups_bms_cell_voltage{cell=\"" + String(i + 1) + "\"} " + String(state.bms.cell_voltages[i]) + "\n";
  }
  metrics += "\n";
  
  metrics += "# HELP ups_bms_cell_voltage_min Minimum cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_min gauge\n";
  metrics += "ups_bms_cell_voltage_min " + String(state.bms.cell_voltage_min) + "\n\n";
  
  metrics += "# HELP ups_bms_cell_voltage_max Maximum cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_max gauge\n";
  metrics += "ups_bms_cell_voltage_max " + String(state.bms.cell_voltage_max) + "\n\n";
  
  metrics += "# HELP ups_bms_cell_voltage_avg Average cell voltage in millivolts\n";
  metrics += "# TYPE ups_bms_cell_voltage_avg gauge\n";
  metrics += "ups_bms_cell_voltage_avg " + String(state.bms.cell_voltage_avg) + "\n\n";
  
  // Power metrics
  metrics += "# HELP ups_power_input_voltage Input voltage in millivolts\n";
  metrics += "# TYPE ups_power_input_voltage gauge\n";
  metrics += "ups_power_input_voltage " + String(state.power.input_voltage) + "\n\n";
  
  metrics += "# HELP ups_power_input_current Input current in milliamperes\n";
  metrics += "# TYPE ups_power_input_current gauge\n";
  metrics += "ups_power_input_current " + String(state.power.input_current) + "\n\n";
  
  metrics += "# HELP ups_power_output_power Output power in milliwatts\n";
  metrics += "# TYPE ups_power_output_power gauge\n";
  metrics += "ups_power_output_power " + String(state.power.output_power) + "\n\n";
  
  metrics += "# HELP ups_power_battery_voltage Battery voltage in millivolts\n";
  metrics += "# TYPE ups_power_battery_voltage gauge\n";
  metrics += "ups_power_battery_voltage " + String(state.power.battery_voltage) + "\n\n";
  
  metrics += "# HELP ups_power_battery_current Battery current in milliamperes\n";
  metrics += "# TYPE ups_power_battery_current gauge\n";
  metrics += "ups_power_battery_current " + String(state.power.battery_current) + "\n\n";
  
  metrics += "# HELP ups_power_ac_present AC power present status (0=absent, 1=present)\n";
  metrics += "# TYPE ups_power_ac_present gauge\n";
  metrics += "ups_power_ac_present " + String(state.power.ac_present ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_charger_enabled Charger enabled status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_power_charger_enabled gauge\n";
  metrics += "ups_power_charger_enabled " + String(state.power.charger_enabled ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_hybrid_mode Hybrid mode status (0=disabled, 1=enabled)\n";
  metrics += "# TYPE ups_power_hybrid_mode gauge\n";
  metrics += "ups_power_hybrid_mode " + String(state.power.hybrid_mode ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_fault_type Power fault type (0=none, see Power_Fault_Type_t enum)\n";
  metrics += "# TYPE ups_power_fault_type gauge\n";
  metrics += "ups_power_fault_type " + String(state.power.fault_type) + "\n\n";
  
  metrics += "# HELP ups_power_bq24780s_connected BQ24780S chip connection status (0=disconnected, 1=connected)\n";
  metrics += "# TYPE ups_power_bq24780s_connected gauge\n";
  metrics += "ups_power_bq24780s_connected " + String(state.power.bq24780s_connected ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_prochot_status PROCHOT pin status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_power_prochot_status gauge\n";
  metrics += "ups_power_prochot_status " + String(state.power.prochot_status ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_power_tbstat_status TB_STAT pin status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_power_tbstat_status gauge\n";
  metrics += "ups_power_tbstat_status " + String(state.power.tbstat_status ? 1 : 0) + "\n\n";
  
  // Protection status
  metrics += "# HELP ups_protection_over_current Over-current protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_over_current gauge\n";
  metrics += "ups_protection_over_current " + String(state.over_current_protection ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_protection_over_temp Over-temperature protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_over_temp gauge\n";
  metrics += "ups_protection_over_temp " + String(state.over_temp_protection ? 1 : 0) + "\n\n";
  
  metrics += "# HELP ups_protection_short_circuit Short-circuit protection status (0=normal, 1=triggered)\n";
  metrics += "# TYPE ups_protection_short_circuit gauge\n";
  metrics += "ups_protection_short_circuit " + String(state.short_circuit_protection ? 1 : 0) + "\n\n";
  
  request->send(200, "text/plain; version=0.0.4; charset=utf-8", metrics);
}

// =============================================================================
// Configuration Page Handlers
// =============================================================================

void WebServer::handleRoot(AsyncWebServerRequest* request) {
  Serial.println(F("WebServer: Serving SPA page"));
  renderSPA(request);
}

static String buildChargingWindowsJson(const void* windows, uint8_t count) {
  // 使用匿名结构避免依赖 power_management.h
  struct Window { uint8_t day_mask; uint8_t start_hour; uint8_t end_hour; };
  const Window* w = (const Window*)windows;

  String json = "[";
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(i);
    json += ",\"day_mask\":" + String(w[i].day_mask);
    json += ",\"start_hour\":" + String(w[i].start_hour);
    json += ",\"end_hour\":" + String(w[i].end_hour) + "}";
  }
  json += "]";
  return json;
}

void WebServer::renderSPA(AsyncWebServerRequest* request) {
  const Configuration* sysConfig = configManager->getSystemConfig();
  const BMS_Config_t* bmsConfig = configManager->getBMSConfig();
  const Power_Config_t* powerConfig = configManager->getPowerConfig();

  // 配置模式检测
  bool isConfigMode = (systemManager == nullptr);

  // 获取系统状态（仅正常模式使用）
  const System_Global_State* statePtr = nullptr;
  if (!isConfigMode) {
    statePtr = &systemManager->getGlobalState();
  }

  // 计算总缓冲区大小：HTML模板 + CSS + JavaScript + 大量额外空间用于替换和拼接
  size_t htmlSize = strlen_P(SPA_PAGE_TEMPLATE);
  size_t cssSize = strlen_P(COMMON_CSS) + strlen_P(CONFIG_CSS) + strlen_P(OTA_CSS) + strlen_P(WIZARD_CSS);
  size_t jsSize = strlen_P(SPA_PAGE_JS);
  // 增加安全余量：原始大小的 3 倍，确保 REPLACE 操作有足够空间
  size_t totalSize = (htmlSize + cssSize + jsSize) * 3;
  
  char* buffer = new char[totalSize];
  char* tempBuffer = new char[totalSize];
  
  // 第一步：复制 HTML 模板
  strcpy_P(buffer, SPA_PAGE_TEMPLATE);
  
  // 第二步：替换 CSS 占位符
  // 找到 <style id="dynamic-css"></style> 并替换内容
  const char* cssPlaceholderStart = strstr_P(buffer, PSTR("<style id=\"dynamic-css\"></style>"));

  if (cssPlaceholderStart) {
    // 找到占位符位置
    size_t placeholderPos = cssPlaceholderStart - buffer;
    const char* afterCss = cssPlaceholderStart + strlen("<style id=\"dynamic-css\"></style>");
    size_t afterCssLen = strlen(afterCss);
    
    // 创建临时缓冲区保存后半部分
    char* afterPart = new char[afterCssLen + 1];
    strcpy(afterPart, afterCss);
    
    // 在占位符处截断
    buffer[placeholderPos] = '\0';
    
    // 拼接：前半部分 + <style> + CSS + </style> + 后半部分
    strcat(buffer, "<style>");
    strcat_P(buffer, COMMON_CSS);
    strcat_P(buffer, CONFIG_CSS);
    strcat_P(buffer, OTA_CSS);
    strcat_P(buffer, WIZARD_CSS);
    strcat(buffer, "</style>");
    strcat(buffer, afterPart);
    
    delete[] afterPart;
  }

  // 第三步：替换 JavaScript 占位符
  const char* jsPlaceholderStart = strstr_P(buffer, PSTR("<script id=\"dynamic-js\"></script>"));
  
  if (jsPlaceholderStart) {
    // 找到占位符位置
    size_t placeholderPos = jsPlaceholderStart - buffer;
    const char* afterJs = jsPlaceholderStart + strlen("<script id=\"dynamic-js\"></script>");
    size_t afterJsLen = strlen(afterJs);
    
    // 创建临时缓冲区保存后半部分
    char* afterPart = new char[afterJsLen + 1];
    strcpy(afterPart, afterJs);
    
    // 在占位符处截断
    buffer[placeholderPos] = '\0';
    
    // 拼接：前半部分 + <script> + JS + </script> + 后半部分
    strcat(buffer, "<script>");
    strcat_P(buffer, SPA_PAGE_JS);
    strcat(buffer, "</script>");
    strcat(buffer, afterPart);
    
    delete[] afterPart;
  }
  
  strcpy(tempBuffer, buffer);
  
  #define REPLACE(key, val) do { replaceStringInBuffer(tempBuffer, totalSize, key, val, buffer); strcpy(tempBuffer, buffer); } while(0)
  #define REPLACE_FMT(key, fmt, val) do { char tmp[32]; snprintf(tmp, sizeof(tmp), fmt, val); REPLACE(key, tmp); } while(0)
  #define REPLACE_CHK(cond) REPLACE("%" #cond "_CHECKED%", (cond) ? "checked" : "")
  
  // System config
  
  // System config
  REPLACE("%WIFI_SSID%", sysConfig->wifi_ssid);
  REPLACE("%WIFI_PASS%", sysConfig->wifi_pass[0] ? sysConfig->wifi_pass : "");
  REPLACE("%BUZZER_STATUS%", sysConfig->buzzer_enabled ? "已启用" : "已禁用");
  REPLACE("%BUZZER_CHECKED%", sysConfig->buzzer_enabled ? "checked" : "");
  REPLACE_FMT("%VOLUME_VALUE%", "%d", sysConfig->buzzer_volume);
  REPLACE_FMT("%VOLUME_LEVEL%", "%d%%", sysConfig->buzzer_volume);
  REPLACE_FMT("%LIGHT_VALUE%", "%d", sysConfig->led_brightness);
  REPLACE_FMT("%LIGHT_BRIGHTNESS%", "%d%%", sysConfig->led_brightness);
  
  // HID 配置
  REPLACE("%HID_CHECKED%", sysConfig->hid_enabled ? "checked" : "");
  REPLACE("%HID_MODE_MAH%", sysConfig->hid_report_mode == 0 ? " selected" : "");
  REPLACE("%HID_MODE_MWH%", sysConfig->hid_report_mode == 1 ? " selected" : "");
  REPLACE("%HID_MODE_PCT%", sysConfig->hid_report_mode == 2 ? " selected" : "");

  // MQTT 配置
  REPLACE("%MQTT_CHECKED%", (sysConfig->mqtt_broker[0] != '\0' && sysConfig->mqtt_port > 0) ? "checked" : "");
  REPLACE("%MQTT_BROKER%", sysConfig->mqtt_broker);
  REPLACE_FMT("%MQTT_PORT%", "%d", sysConfig->mqtt_port);
  REPLACE("%MQTT_USERNAME%", sysConfig->mqtt_username);
  REPLACE("%MQTT_PASSWORD%", sysConfig->mqtt_password);

  // IP 模式配置 - 新增
  REPLACE("%IP_MODE_DHCP%", sysConfig->use_static_ip ? "" : " selected");
  REPLACE("%IP_MODE_STATIC%", sysConfig->use_static_ip ? " selected" : "");
  REPLACE("%STATIC_IP_DISPLAY%", sysConfig->use_static_ip ? "block" : "none");
   REPLACE("%STATIC_IP%", sysConfig->static_ip);
   REPLACE("%STATIC_GATEWAY%", sysConfig->static_gateway);
   REPLACE("%STATIC_SUBNET%", sysConfig->static_subnet);
   REPLACE("%STATIC_DNS%", sysConfig->static_dns);
   REPLACE("%NTP_SERVER%", sysConfig->ntp_server);

   // BMS config
  REPLACE_FMT("%CELL_COUNT%", "%d", bmsConfig->cell_count);
  REPLACE_FMT("%CAPACITY%", "%d", bmsConfig->nominal_capacity_mAh);
  REPLACE_FMT("%BMS_CHARGE_CURRENT%", "%d", bmsConfig->max_charge_current);
  REPLACE_FMT("%PWR_CHARGE_CURRENT%", "%d", powerConfig->max_charge_current);
  REPLACE_FMT("%PWR_DISCHARGE_CURRENT%", "%d", powerConfig->max_discharge_current);
  
  REPLACE("%BMS_CELL_COUNT_3%", (bmsConfig->cell_count == 3) ? " selected" : "");
  REPLACE("%BMS_CELL_COUNT_4%", (bmsConfig->cell_count == 4) ? " selected" : "");
  REPLACE("%BMS_CELL_COUNT_5%", (bmsConfig->cell_count == 5) ? " selected" : "");
  
  REPLACE_FMT("%BMS_NOMINAL_CAPACITY%", "%d", bmsConfig->nominal_capacity_mAh);
  REPLACE_FMT("%BMS_CELL_OV%", "%d", bmsConfig->cell_ov_threshold);
  REPLACE_FMT("%BMS_CELL_UV%", "%d", bmsConfig->cell_uv_threshold);
  REPLACE_FMT("%BMS_CELL_OV_RECOVER%", "%d", bmsConfig->cell_ov_recover);
  REPLACE_FMT("%BMS_CELL_UV_RECOVER%", "%d", bmsConfig->cell_uv_recover);
  REPLACE_FMT("%BMS_MAX_CHARGE%", "%d", bmsConfig->max_charge_current);
  REPLACE_FMT("%BMS_MAX_DISCHARGE%", "%d", bmsConfig->max_discharge_current);
  REPLACE_FMT("%BMS_SHORT_CIRCUIT%", "%d", bmsConfig->short_circuit_threshold);
  REPLACE_FMT("%BMS_OVERHEAT_THRESHOLD%", "%.1f", bmsConfig->temp_overheat_threshold);
  

  // 正常运行模式：完整替换所有配置
  REPLACE("%BMS_BALANCING_CHECKED%", bmsConfig->balancing_enabled ? "checked" : "");
  REPLACE_FMT("%BMS_BALANCING_DIFF%", "%.1f", bmsConfig->balancing_voltage_diff);
  REPLACE_FMT("%POWER_MAX_CHARGE%", "%d", powerConfig->max_charge_current);
  REPLACE_FMT("%POWER_CHARGE_VOLTAGE%", "%d", powerConfig->charge_voltage_limit);
  REPLACE_FMT("%POWER_CHARGE_SOC_START%", "%.0f", powerConfig->charge_soc_start);
  REPLACE_FMT("%POWER_CHARGE_SOC_STOP%", "%.0f", powerConfig->charge_soc_stop);
  REPLACE_FMT("%POWER_MAX_DISCHARGE%", "%d", powerConfig->max_discharge_current);
  REPLACE_FMT("%POWER_DISCHARGE_SOC_STOP%", "%.0f", powerConfig->discharge_soc_stop);
  REPLACE("%POWER_HYBRID_CHECKED%", powerConfig->enable_hybrid_boost ? "checked" : "");
  REPLACE_FMT("%POWER_OVER_CURRENT%", "%d", powerConfig->over_current_threshold);
  REPLACE_FMT("%POWER_OVER_TEMP%", "%.1f", powerConfig->over_temp_threshold);
  REPLACE_FMT("%POWER_CHARGE_TEMP_HIGH%", "%.1f", powerConfig->charge_temp_high_limit);
  REPLACE_FMT("%POWER_CHARGE_TEMP_LOW%", "%.1f", powerConfig->charge_temp_low_limit);

  if (!isConfigMode && statePtr != nullptr) {
    String firmwareVersion = String(statePtr->system.firmware_version);
    char versionBuf[32];
    snprintf(versionBuf, sizeof(versionBuf), "%s", firmwareVersion.c_str());
    REPLACE("%FIRMWARE_VERSION%", versionBuf);
  } else {
    REPLACE("%FIRMWARE_VERSION%", "Config Mode");
  }

  char spaceBuf[32];
  snprintf(spaceBuf, sizeof(spaceBuf), "%lu", (unsigned long)(ESP.getFreeSketchSpace() / 1024));
  REPLACE("%FREE_SKETCH_SPACE%", spaceBuf);
  char flashBuf[32];
  snprintf(flashBuf, sizeof(flashBuf), "%lu", (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
  REPLACE("%FLASH_SIZE%", flashBuf);


  // 将时间窗口数据注入到 HTML 页面
  String windowsJson = isConfigMode ? "[]" : buildChargingWindowsJson(powerConfig->charging_windows, powerConfig->charging_window_count);
  char windowInitCode[2048];
  snprintf(windowInitCode, sizeof(windowInitCode),
           "<script>window.IW=%s;</script>",
           windowsJson.c_str());

  #undef REPLACE
  #undef REPLACE_FMT
  #undef REPLACE_CHK

  // 注入配置模式标记到 </head> 后（确保在 JS 执行前定义）
  const char* configModeScript = isConfigMode ? "<script>window.CONFIG_MODE=1;</script>" : "<script>window.CONFIG_MODE=0;</script>";
  char* headEndPos = strstr(tempBuffer, "</head>");
  if (headEndPos) {
    size_t headPos = headEndPos - tempBuffer + strlen("</head>");
    size_t afterHeadLen = strlen(headEndPos);
    char* afterHead = new char[afterHeadLen + strlen(configModeScript) + 1];
    strcpy(afterHead, headEndPos);

    headEndPos[0] = '\0';
    strcat(tempBuffer, configModeScript);
    strcat(tempBuffer, afterHead);
    delete[] afterHead;
  }

  // 注入初始化脚本到 </body> 前
  char* bodyEndPos = strstr(tempBuffer, "</body>");
  if (bodyEndPos) {
    size_t bodyPos = bodyEndPos - tempBuffer;
    size_t afterBodyLen = strlen(bodyEndPos);
    char* afterBody = new char[afterBodyLen + strlen(windowInitCode) + 1];
    strcpy(afterBody, bodyEndPos);

    tempBuffer[bodyPos] = '\0';
    strcat(tempBuffer, windowInitCode);
    strcat(tempBuffer, afterBody);
    delete[] afterBody;
  }

  request->send(200, "text/html", tempBuffer);
  delete[] buffer;
  delete[] tempBuffer;
  
  Serial.printf_P(PSTR("SPA page: SSID=%s, Buzzer=%s, Vol=%d, Bright=%d, Windows=%d\n"),
                  sysConfig->wifi_ssid, sysConfig->buzzer_enabled ? "ON" : "OFF",
                  sysConfig->buzzer_volume, sysConfig->led_brightness,
                  powerConfig->charging_window_count);
}

// =============================================================================
// Save Configuration Handler
// =============================================================================

void WebServer::handleSaveConfig(AsyncWebServerRequest* request) {
  Serial.println(F("WebServer: Handling save config request"));
  
  String jsonString;
  if (request->_tempObject != nullptr) {
    jsonString = *(String*)request->_tempObject;
    delete (String*)request->_tempObject;
    request->_tempObject = nullptr;
  } else if (request->hasParam("plain", true)) {
    jsonString = request->getParam("plain", true)->value();
  } else if (request->hasArg("plain")) {
    jsonString = request->arg("plain");
  } else {
    sendErrorResponse(request, "Missing request body", 400);
    return;
  }
  
  Serial.printf_P(PSTR("Received JSON: %s\n"), jsonString.c_str());
  
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.printf_P(PSTR("JSON parse error: %s\n"), error.c_str());
    sendErrorResponse(request, "Invalid JSON format", 400);
    return;
  }
  
  if (!updateConfigurationFromRequest(doc)) {
    sendErrorResponse(request, "Failed to update configuration", 500);
    return;
  }
  
  if (!configManager->saveConfiguration()) {
    sendErrorResponse(request, "Failed to save configuration to flash", 500);
    return;
  }
  
  Serial.println(F("Configuration saved successfully"));
  
  // 判断是否在配置模式（systemManager 为 nullptr 表示配置模式）
  bool isConfigMode = (systemManager == nullptr);
  
  StaticJsonDocument<256> responseDoc;
  responseDoc["success"] = true;
  
  if (isConfigMode) {
    // 配置模式：需要重启才能应用配置
    responseDoc["message"] = "Configuration saved successfully. Device will reboot...";
    responseDoc["restart_required"] = true;

    char responseBuffer[512];
    serializeJson(responseDoc, responseBuffer);
    request->send(200, "application/json", responseBuffer);

    // 确保响应完全发送后再重启
    delay(100);

    Serial.println(F("==========================================="));
    Serial.println(F("Configuration saved. Rebooting..."));
    Serial.println(F("==========================================="));

    ESP.restart();
  } else {
    // 正常运行模式：热更新，不需要重启
    // 事件已经通过 updateSystemConfig/updateBMSConfig/updatePowerConfig 发布
    // SystemManagement 和 WiFiManager 会通过事件监听自动应用配置
    responseDoc["message"] = "Configuration saved and applied successfully";
    responseDoc["restart_required"] = false;
    
    char responseBuffer[512];
    serializeJson(responseDoc, responseBuffer);
    request->send(200, "application/json", responseBuffer);
    
    Serial.println(F("Configuration applied via hot update (no restart required)"));
  }
  return;
}

// =============================================================================
// Helper Functions
// =============================================================================

void WebServer::sendErrorResponse(AsyncWebServerRequest* request, const String& message, int code) {
  Serial.printf_P(PSTR("Error response: %s (code: %d)\n"), message.c_str(), code);
  request->send(code, "application/json", "{\"success\":false,\"message\":\"" + message + "\"}");
}

void WebServer::sendConfigModeResponse(AsyncWebServerRequest* request) {
  StaticJsonDocument<256> doc;
  doc["status"] = "config_mode";
  doc["message"] = "Hardware modules not initialized";
  doc["timestamp"] = millis();
  
  char buffer[512];
  serializeJson(doc, buffer);
  request->send(200, "application/json", buffer);
}

// =============================================================================
// Configuration Update Logic
// =============================================================================

bool WebServer::updateConfigurationFromRequest(const JsonDocument& doc) {
  Serial.println(F("Updating configuration from JSON"));
  
  bool success = true;
  
  // 创建临时配置副本，避免直接修改内部配置导致变化检测失败
  Configuration tempSysConfig = *configManager->getSystemConfig();
  BMS_Config_t tempBmsConfig = *configManager->getBMSConfig();
  Power_Config_t tempPowerConfig = *configManager->getPowerConfig();
  
  // Update System Configuration
  if (doc.containsKey("system")) {
    JsonVariantConst sys = doc["system"];
    
    if (sys.containsKey("wifi_ssid")) {
      const char* ssid = sys["wifi_ssid"];
      if (ssid) strlcpy(tempSysConfig.wifi_ssid, ssid, sizeof(tempSysConfig.wifi_ssid));
    }
    if (sys.containsKey("wifi_pass")) {
      const char* pass = sys["wifi_pass"];
      if (pass) strlcpy(tempSysConfig.wifi_pass, pass, sizeof(tempSysConfig.wifi_pass));
    }
    if (sys.containsKey("led_brightness")) {
      int val = sys["led_brightness"];
      if (val >= 0 && val <= 100) tempSysConfig.led_brightness = (uint8_t)val;
    }
    if (sys.containsKey("buzzer_enabled")) {
      tempSysConfig.buzzer_enabled = sys["buzzer_enabled"];
    }
    if (sys.containsKey("volume_level")) {
      int val = sys["volume_level"];
      if (val >= 0 && val <= 100) tempSysConfig.buzzer_volume = (uint8_t)val;
    }
    
    // ========== 处理 HID 配置 ==========
    if (sys.containsKey("hid_enabled")) {
      tempSysConfig.hid_enabled = sys["hid_enabled"];
    }
    if (sys.containsKey("hid_report_mode")) {
      int val = sys["hid_report_mode"];
      if (val >= 0 && val <= 2) tempSysConfig.hid_report_mode = (uint8_t)val;
    }
    // ================================

    // ========== 处理 MQTT 配置 ==========
    if (sys.containsKey("mqtt_enabled")) {
      bool mqtt_en = sys["mqtt_enabled"];
      if (mqtt_en) {
        // 启用 MQTT，需要填写 broker 和 port
        if (sys.containsKey("mqtt_broker") && strlen(sys["mqtt_broker"].as<const char*>()) > 0) {
          const char* broker = sys["mqtt_broker"];
          if (broker) strlcpy(tempSysConfig.mqtt_broker, broker, sizeof(tempSysConfig.mqtt_broker));
        }
        if (sys.containsKey("mqtt_port")) {
          uint16_t port = sys["mqtt_port"];
          if (port > 0 && port <= 65535) tempSysConfig.mqtt_port = port;
        }
        if (sys.containsKey("mqtt_username") && strlen(sys["mqtt_username"].as<const char*>()) > 0) {
          const char* usr = sys["mqtt_username"];
          if (usr) strlcpy(tempSysConfig.mqtt_username, usr, sizeof(tempSysConfig.mqtt_username));
        }
        if (sys.containsKey("mqtt_password") && strlen(sys["mqtt_password"].as<const char*>()) > 0) {
          const char* pwd = sys["mqtt_password"];
          if (pwd) strlcpy(tempSysConfig.mqtt_password, pwd, sizeof(tempSysConfig.mqtt_password));
        }
      } else {
        // 禁用 MQTT，清空配置
        tempSysConfig.mqtt_broker[0] = '\0';
        tempSysConfig.mqtt_port = 0;
        tempSysConfig.mqtt_username[0] = '\0';
        tempSysConfig.mqtt_password[0] = '\0';
      }
    }
    // =================================

    // ========== 处理静态 IP 配置 - 新增 ==========
    if (sys.containsKey("use_static_ip")) {
      tempSysConfig.use_static_ip = sys["use_static_ip"];
    }
    if (sys.containsKey("static_ip") && strlen(sys["static_ip"].as<const char*>()) > 0) {
      const char* ip = sys["static_ip"];
      if (ip) strlcpy(tempSysConfig.static_ip, ip, sizeof(tempSysConfig.static_ip));
    }
    if (sys.containsKey("static_gateway") && strlen(sys["static_gateway"].as<const char*>()) > 0) {
      const char* gw = sys["static_gateway"];
      if (gw) strlcpy(tempSysConfig.static_gateway, gw, sizeof(tempSysConfig.static_gateway));
    }
    if (sys.containsKey("static_subnet") && strlen(sys["static_subnet"].as<const char*>()) > 0) {
      const char* sn = sys["static_subnet"];
      if (sn) strlcpy(tempSysConfig.static_subnet, sn, sizeof(tempSysConfig.static_subnet));
    }
    if (sys.containsKey("static_dns") && strlen(sys["static_dns"].as<const char*>()) > 0) {
      const char* dns = sys["static_dns"];
      if (dns) strlcpy(tempSysConfig.static_dns, dns, sizeof(tempSysConfig.static_dns));
    }
    if (sys.containsKey("ntp_server") && strlen(sys["ntp_server"].as<const char*>()) > 0) {
      const char* ntp = sys["ntp_server"];
      if (ntp) strlcpy(tempSysConfig.ntp_server, ntp, sizeof(tempSysConfig.ntp_server));
    }
    // ======================================
    
    if (!configManager->updateSystemConfig(tempSysConfig, false)) {
      Serial.println(F("Error: Failed to update system config"));
      success = false;
    }
  }
  
  // Update BMS Configuration
  if (doc.containsKey("bms")) {
    JsonVariantConst bms = doc["bms"];
    
    if (bms.containsKey("cell_count")) {
      uint8_t val = bms["cell_count"];
      if (val >= 3 && val <= 5) tempBmsConfig.cell_count = val;
    }
    if (bms.containsKey("nominal_capacity_mAh")) {
      uint32_t val = bms["nominal_capacity_mAh"];
      if (val > 0 && val <= 50000) tempBmsConfig.nominal_capacity_mAh = val;
    }
    if (bms.containsKey("cell_ov_threshold")) {
      uint16_t val = bms["cell_ov_threshold"];
      if (val >= 4000 && val <= 4500) tempBmsConfig.cell_ov_threshold = val;
    }
    if (bms.containsKey("cell_uv_threshold")) {
      uint16_t val = bms["cell_uv_threshold"];
      if (val >= 2500 && val <= 3500) tempBmsConfig.cell_uv_threshold = val;
    }
    if (bms.containsKey("cell_ov_recover")) {
      uint16_t val = bms["cell_ov_recover"];
      if (val >= 4000 && val <= 4300) tempBmsConfig.cell_ov_recover = val;
    }
    if (bms.containsKey("cell_uv_recover")) {
      uint16_t val = bms["cell_uv_recover"];
      if (val >= 2800 && val <= 3300) tempBmsConfig.cell_uv_recover = val;
    }
    if (bms.containsKey("max_charge_current")) {
      uint16_t val = bms["max_charge_current"];
      if (val <= 10000) tempBmsConfig.max_charge_current = val;
    }
    if (bms.containsKey("max_discharge_current")) {
      uint16_t val = bms["max_discharge_current"];
      if (val <= 20000) tempBmsConfig.max_discharge_current = val;
    }
    if (bms.containsKey("short_circuit_threshold")) {
      uint16_t val = bms["short_circuit_threshold"];
      if (val <= 30000) tempBmsConfig.short_circuit_threshold = val;
    }
    if (bms.containsKey("temp_overheat_threshold")) {
      float val = bms["temp_overheat_threshold"];
      if (val >= 50.0f && val <= 80.0f) tempBmsConfig.temp_overheat_threshold = val;
    }
    if (bms.containsKey("balancing_enabled")) {
      tempBmsConfig.balancing_enabled = bms["balancing_enabled"];
    }
    if (bms.containsKey("balancing_voltage_diff")) {
      float val = bms["balancing_voltage_diff"];
      if (val >= 5.0f && val <= 100.0f) tempBmsConfig.balancing_voltage_diff = val;
    }
    
    if (!configManager->updateBMSConfig(tempBmsConfig, false)) {
      Serial.println(F("Error: Failed to update BMS config"));
      success = false;
    }
  }
  
  // Update Power Configuration
  if (doc.containsKey("power")) {
    JsonVariantConst pwr = doc["power"];
    
    if (pwr.containsKey("max_charge_current")) {
      uint16_t val = pwr["max_charge_current"];
      if (val <= 10000) tempPowerConfig.max_charge_current = val;
    }
    if (pwr.containsKey("charge_voltage_limit")) {
      uint16_t val = pwr["charge_voltage_limit"];
      if (val >= 10000 && val <= 25000) tempPowerConfig.charge_voltage_limit = val;
    }
    if (pwr.containsKey("charge_soc_start")) {
      float val = pwr["charge_soc_start"];
      if (val >= 0.0f && val <= 90.0f) tempPowerConfig.charge_soc_start = val;
    }
    if (pwr.containsKey("charge_soc_stop")) {
      float val = pwr["charge_soc_stop"];
      if (val >= 50.0f && val <= 100.0f) tempPowerConfig.charge_soc_stop = val;
    }
    if (pwr.containsKey("max_discharge_current")) {
      uint16_t val = pwr["max_discharge_current"];
      if (val <= 20000) tempPowerConfig.max_discharge_current = val;
    }
    if (pwr.containsKey("discharge_soc_stop")) {
      float val = pwr["discharge_soc_stop"];
      if (val >= 0.0f && val <= 30.0f) tempPowerConfig.discharge_soc_stop = val;
    }
    if (pwr.containsKey("enable_hybrid_boost")) {
      tempPowerConfig.enable_hybrid_boost = pwr["enable_hybrid_boost"];
    }
    if (pwr.containsKey("over_current_threshold")) {
      uint16_t val = pwr["over_current_threshold"];
      if (val <= 20000) tempPowerConfig.over_current_threshold = val;
    }
    if (pwr.containsKey("over_temp_threshold")) {
      float val = pwr["over_temp_threshold"];
      if (val >= 40.0f && val <= 100.0f) tempPowerConfig.over_temp_threshold = val;
    }
    if (pwr.containsKey("charge_temp_high_limit")) {
      float val = pwr["charge_temp_high_limit"];
      if (val >= 30.0f && val <= 60.0f) tempPowerConfig.charge_temp_high_limit = val;
    }
    if (pwr.containsKey("charge_temp_low_limit")) {
      float val = pwr["charge_temp_low_limit"];
      if (val >= -20.0f && val <= 10.0f) tempPowerConfig.charge_temp_low_limit = val;
    }

    // ========== 处理时间窗口配置 ==========
    if (pwr.containsKey("charging_windows") && pwr.containsKey("charging_window_count")) {
      JsonArrayConst windowsArray = pwr["charging_windows"].as<JsonArrayConst>();
      uint8_t windowCount = pwr["charging_window_count"];
      
      Serial.printf_P(PSTR("[Config] Processing %d charging windows\n"), windowCount);
      
      // 首先清空所有时间窗口
      memset(tempPowerConfig.charging_windows, 0, sizeof(tempPowerConfig.charging_windows));
      tempPowerConfig.charging_window_count = 0;
      
      // 解析并保存有效的时间窗口（最多 5 个）
      uint8_t validWindowCount = 0;
      for (size_t i = 0; i < windowsArray.size() && validWindowCount < 5; i++) {
        JsonObjectConst window = windowsArray[i].as<JsonObjectConst>();
        
        if (window.containsKey("day_mask") &&
            window.containsKey("start_hour") &&
            window.containsKey("end_hour")) {
          
          uint8_t dayMask = window["day_mask"];
          uint8_t startHour = window["start_hour"];
          uint8_t endHour = window["end_hour"];
          
          // 验证数据有效性
          if (dayMask > 0 && dayMask <= 127 &&  // 至少有一天，且不超过 7 位
              startHour < 24 && endHour <= 24 &&  // 小时范围有效
              startHour < endHour) {  // 开始时间必须小于结束时间
            
            tempPowerConfig.charging_windows[validWindowCount].day_mask = dayMask;
            tempPowerConfig.charging_windows[validWindowCount].start_hour = startHour;
            tempPowerConfig.charging_windows[validWindowCount].end_hour = endHour;
            
            validWindowCount++;
            
            Serial.printf_P(PSTR("[Config] Window %d: mask=0x%02X, %02d:00-%02d:00\n"),
                           validWindowCount, dayMask, startHour, endHour);
          } else {
            Serial.printf_P(PSTR("[Config] Invalid window %d: mask=%d, %d:00-%d:00 (skipped)\n"),
                           i, dayMask, startHour, endHour);
          }
        }
      }
      
      // 更新实际窗口数量
      tempPowerConfig.charging_window_count = validWindowCount;
      Serial.printf_P(PSTR("[Config] Total valid charging windows: %d\n"), validWindowCount);
    }
    // ======================================
    
    if (!configManager->updatePowerConfig(tempPowerConfig, false)) {
      Serial.println(F("Error: Failed to update power config"));
      success = false;
    }
  }
  
  Serial.println(success ? F("All configs updated successfully") : F("Some config updates failed"));
  return success;
}

void WebServer::replaceStringInBuffer(char* buffer, size_t bufferSize, const char* search, 
                                      const char* replace, char* tempBuffer) {
  if (!buffer || !search || !replace || !tempBuffer) return;
  
  size_t searchLen = strlen(search);
  if (searchLen == 0) return; // 防止空搜索字符串
  
  size_t replaceLen = strlen(replace);
  
  // 使用迭代而非递归，避免栈溢出
  while (true) {
    char* pos = strstr(buffer, search);
    if (!pos) break; // 没有更多匹配项，退出循环
    
    size_t prefixLen = pos - buffer;
    
    // 检查缓冲区是否足够
    size_t currentLen = strlen(buffer);
    size_t newLen = currentLen - searchLen + replaceLen;
    if (newLen >= bufferSize) {
      break; // 防止缓冲区溢出
    }
    
    // 复制前缀部分
    strncpy(tempBuffer, buffer, prefixLen);
    tempBuffer[prefixLen] = '\0';
    
    // 拼接替换内容
    strlcat(tempBuffer, replace, bufferSize);
    strlcat(tempBuffer, pos + searchLen, bufferSize);
    
    // 复制回原缓冲区
    strncpy(buffer, tempBuffer, bufferSize);
    buffer[bufferSize - 1] = '\0';
  }
}

// =============================================================================
// BMS Ship Mode Handler
// =============================================================================

void WebServer::handleBmsShipMode(AsyncWebServerRequest* request) {
  Serial.println(F("[WebServer] BMS Ship Mode request received"));
  
  // First, send response to frontend confirming receipt
  StaticJsonDocument<256> doc;
  doc["success"] = true;
  doc["message"] = "请求已接收，正在进入运输模式...";
  
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
  
  // Then, publish event for SystemManager to handle
  Serial.println(F("[WebServer] Publishing EVT_BMS_SHIPMODE_REQUEST event"));
  EventBus::getInstance().publish(EVT_BMS_SHIPMODE_REQUEST, nullptr);
}

// =============================================================================
// ADC Calibration API Handlers
// =============================================================================

void WebServer::handleCalibrationGet(AsyncWebServerRequest* request) {
  if (systemManager == nullptr || systemManager->getADCCalibration() == nullptr) {
    sendConfigModeResponse(request);
    return;
  }

  StaticJsonDocument<256> doc;
  doc["success"] = true;

  const uint8_t* cal = systemManager->getADCCalibration();
  JsonArray arr = doc.createNestedArray("calibration");
  for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
    arr.add(cal[i]);
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServer::handleCalibrationPost(AsyncWebServerRequest* request) {
  if (systemManager == nullptr) {
    sendErrorResponse(request, "Config mode", 503);
    return;
  }

  String jsonString;
  if (request->_tempObject != nullptr) {
    jsonString = *(String*)request->_tempObject;
    delete (String*)request->_tempObject;
    request->_tempObject = nullptr;
  } else {
    sendErrorResponse(request, "Missing request body", 400);
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    sendErrorResponse(request, "Invalid JSON format", 400);
    return;
  }

  if (!doc.containsKey("calibration")) {
    sendErrorResponse(request, "Missing calibration array", 400);
    return;
  }

  JsonArrayConst calArray = doc["calibration"];
  if (calArray.size() != ADC_CAL_PIN_COUNT) {
    sendErrorResponse(request, "Invalid calibration array size", 400);
    return;
  }

  for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
    systemManager->setADCCalibration(ADC_CAL_PINS[i], (uint8_t)calArray[i]);
  }

  StaticJsonDocument<128> resp;
  resp["success"] = true;
  resp["message"] = "Calibration saved";

  String response;
  serializeJson(resp, response);
  request->send(200, "application/json", response);

  Serial.println(F("[WebServer] ADC calibration updated"));
}

// =============================================================================
// OTA 版本解析与比对辅助函数
// =============================================================================

// 从缓冲区中提取标签后的版本字符串，返回写入 outVer 的字符数，0 表示未找到
// 格式："...SIG:UPS-ESP32S3:VER:1.0.2..." -> 提取 "1.0.2"
static size_t parseVersionFromTag(const uint8_t* buf, size_t bufLen,
                                  const char* prefix, char* outVer, size_t outSize) {
  size_t prefixLen = strlen(prefix);
  // 在 buf 中搜索 prefix (不依赖 memmem)
  for (size_t i = 0; i + prefixLen <= bufLen; i++) {
    if (memcmp(buf + i, prefix, prefixLen) == 0) {
      // prefix 之后即版本号，读取直到分隔符
      const uint8_t* verStart = buf + i + prefixLen;
      size_t j = 0;
      while (j < outSize - 1 && (i + prefixLen + j) < bufLen) {
        char c = (char)verStart[j];
        if (c == ' ' || c == '"' || c == '\n' || c == '\r' || c == '\0') break;
        outVer[j] = c;
        j++;
      }
      outVer[j] = '\0';
      return j;
    }
  }
  return 0;
}

// x.y.z 格式版本号比较：newVer >= minVer 返回 true
static bool isVersionGreaterOrEqual(const char* newVer, const char* minVer) {
  int nMajor = 0, nMinor = 0, nPatch = 0;
  int mMajor = 0, mMinor = 0, mPatch = 0;
  sscanf(newVer, "%d.%d.%d", &nMajor, &nMinor, &nPatch);
  sscanf(minVer,  "%d.%d.%d", &mMajor, &mMinor, &mPatch);
  if (nMajor != mMajor) return nMajor > mMajor;
  if (nMinor != mMinor) return nMinor > mMinor;
  return nPatch >= mPatch;
}

// =============================================================================
// OTA Update Handlers
// =============================================================================

void WebServer::handleFirmwareUpload(AsyncWebServerRequest* request, String filename,
                                       size_t index, uint8_t* data, size_t len, bool final) {
  // 二阶段 OTA：先缓冲并搜索签名+版本号，验证通过后再流式写入 flash
  // 签名在固件中的位置取决于链接器 .rodata 放置，可能不在前几 KB

  static bool    otaVerified = false;
  static bool    otaRejected = false;
  static size_t  lastPrint = 0;
  static size_t  totalBytes = 0;
  static uint8_t otaBuf[8192];
  static size_t  otaBufLen = 0;

  const size_t prefixLen = strlen(EXPECTED_SIG_PREFIX);

  if (!index) {
    otaVerified = false;
    otaRejected = false;
    lastPrint = 0;
    totalBytes = 0;
    otaBufLen = 0;
    s_otaSuccess = false;

    uint32_t maxSketchSpace = ESP.getFreeSketchSpace();
    if (maxSketchSpace == 0) {
      Serial.println(F("OTA ERROR: 无法获取可用 Flash 空间"));
      otaRejected = true;
      return;
    }
    uint32_t safeSize = maxSketchSpace - 0x1000;

    if (!Update.begin(safeSize)) {
      Serial.print(F("OTA ERROR: Update.begin() 失败："));
      Serial.println(Update.errorString());
      otaRejected = true;
      return;
    }
  }

  if (otaRejected) return;
  if (len == 0 && !final) return;

  // === 阶段 A：签名验证前，缓冲数据 ===
  if (!otaVerified) {
    // 追加到缓冲区（不超过 8KB）
    size_t remaining = sizeof(otaBuf) - otaBufLen;
    size_t copyLen = (len < remaining) ? len : remaining;
    if (copyLen > 0) memcpy(otaBuf + otaBufLen, data, copyLen);
    otaBufLen += copyLen;
    totalBytes += len;  // 包括溢出部分

    // 每次追加后在已缓冲数据中搜索签名
    for (size_t i = 0; i + prefixLen + 1 <= otaBufLen; i++) {
      if (memcmp(otaBuf + i, EXPECTED_SIG_PREFIX, prefixLen) != 0) continue;

      // 找到前缀，打印周围字节用于调试
      size_t dumpStart = (i >= 8) ? i - 8 : 0;
      size_t dumpEnd = i + prefixLen + 16;
      if (dumpEnd > otaBufLen) dumpEnd = otaBufLen;

      // 提取版本号
      char newVer[16] = {0};
      size_t j = 0;
      const uint8_t* verStart = otaBuf + i + prefixLen;
      size_t verAvail = otaBufLen - i - prefixLen;
      while (j < sizeof(newVer) - 1 && j < verAvail) {
        char c = (char)verStart[j];
        if (c == ' ' || c == '"' || c == '\n' || c == '\r' || c == '\0') break;
        newVer[j] = c;
        j++;
      }
      newVer[j] = '\0';

      if (j == 0) {
        Serial.printf_P(PSTR("OTA: 前缀已找到(偏移 %u, bufLen=%u)，等待版本号数据...\n"), (unsigned)i, (unsigned)otaBufLen);
        break;
      }

      // 有版本号，开始校验
      if (!isVersionGreaterOrEqual(newVer, MIN_REQUIRED_VERSION)) {
        Serial.printf_P(PSTR("OTA REJECT: 版本 %s < 最低要求 %s\n"), newVer, MIN_REQUIRED_VERSION);
        otaRejected = true;
        Update.abort();
        return;
      }

      // 版本校验通过，将缓冲区写入 flash
      if (Update.write(otaBuf, otaBufLen) != otaBufLen) {
        Serial.print(F("OTA ERROR: 缓冲区写入失败："));
        Serial.println(Update.errorString());
        otaRejected = true;
        return;
      }

      // 写入当前 chunk 中超出缓冲区的部分
      if (len > copyLen) {
        size_t extraOffset = copyLen;
        size_t extraLen = len - extraOffset;
        if (Update.write(data + extraOffset, extraLen) != extraLen) {
          Serial.print(F("OTA ERROR: 额外数据写入失败："));
          Serial.println(Update.errorString());
          otaRejected = true;
          return;
        }
      }

      otaVerified = true;
      lastPrint = totalBytes / 1024;
      break;
    }

    if (!otaVerified && otaBufLen >= sizeof(otaBuf) && !final) {
      Serial.printf_P(PSTR("OTA REJECT: 缓冲区已满(%u 字节)未找到签名\n"), (unsigned)otaBufLen);
      otaRejected = true;
      Update.abort();
    }

    if (final && !otaVerified && !otaRejected) {
      Serial.println(F("OTA REJECT: 全文未找到项目特征前缀。"));
      otaRejected = true;
      Update.abort();
    }
    return;
  }

  // === 阶段 B：签名已验证，流式写入 ===
  if (len && Update.write(data, len) != len) {
    Serial.print(F("OTA ERROR: 写入失败："));
    Serial.println(Update.errorString());
    otaRejected = true;
    return;
  }
  totalBytes += len;

  if (totalBytes - lastPrint > 51200) {
    Serial.printf_P(PSTR("OTA 进度：%lu KB\n"), totalBytes / 1024);
    lastPrint = totalBytes / 1024;
  }

  if (final) {
    Serial.printf_P(PSTR("OTA: 写入完成，共 %lu KB\n"), totalBytes / 1024);
    if (Update.end(true)) {
      s_otaSuccess = true;
      Serial.println(F("OTA 成功！即将重启..."));
      const esp_partition_t* bootPart = esp_ota_get_boot_partition();
      if (bootPart) Serial.printf_P(PSTR("OTA: 启动分区已切换至 %s @ 0x%06X\n"), bootPart->label, bootPart->address);
    } else {
      Serial.print(F("OTA ERROR: 结束失败："));
      Serial.println(Update.errorString());
    }
  }
}