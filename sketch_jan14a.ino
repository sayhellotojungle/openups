/*
 * UPS Control System for ESP32 with BQ24780S and BQ76920
 */

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

#include "src/data_structures.h"
#include "src/pins_config.h"
#include "src/config_manager.h"
#include "src/hardware_interface.h"
#include "src/web_server.h"
#include "src/i2c_interface.h"
#include "src/bq24780s.h"
#include "src/bq76920.h"
#include "src/bms.h"
#include "src/power_management.h"
#include "src/system_management.h"
#include "src/WiFiManager.h"
#include "src/time_utils.h"
#include "src/ups_hid_service.h"
#include "src/mqtt_service.h"

// =============================================================================
// OTA 固件特征标签
// 格式: "SIG:<PROJECT_NAME>:VER:<VERSION>"
// =============================================================================
char FIRMWARE_ID_TAG[50];

// =============================================================================
// Global Objects
// =============================================================================

ConfigManager configManager;
HardwareInterface* hardware = nullptr;
BMS* bms = nullptr;
PowerManagement* powerManagement = nullptr;
SystemManagement* systemManager = nullptr;
WebServer* webServer = nullptr;
WiFiManager* wifiManager = nullptr;

System_Global_State globalSystemState;
Configuration* systemConfig = nullptr;
BMS_Config_t* bmsConfig = nullptr;
Power_Config_t* powerConfig = nullptr;

bool g_force_factory_reset = false;
UPS_HID_Service* upsHidService = nullptr;
MQTTService* mqttService = nullptr;

// =============================================================================
// Helper Functions
// =============================================================================

bool checkFactoryReset() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  char version[] = "SIG:OPENUPS-ESP32S3:VER:1.0.9";
  strcpy(FIRMWARE_ID_TAG, version);
  Serial.println(F("Checking for factory reset button..."));
  
  const unsigned long detectionTimeout = 3000;
  const unsigned long minPressDuration = 2500;
  unsigned long startTime = millis();
  unsigned long pressStartTime = 0;
  bool buttonPressed = false;
  
  while (millis() - startTime < detectionTimeout) {
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      if (!buttonPressed) {
        pressStartTime = millis();
        buttonPressed = true;
        Serial.println(F("Reset button pressed, timing..."));
      }
      if (millis() - pressStartTime >= minPressDuration) {
        Serial.println(F("Factory Reset Triggered!"));
        return true;
      }
    } else if (buttonPressed) {
      Serial.println(F("Button released too early (< 2.5s)"));
      buttonPressed = false;
    }
    delay(1);
  }
  
  Serial.println(F("Normal Boot"));
  return false;
}

template<typename T>
bool initModule(T*& module, const char* name) {
  unsigned long startTime = millis();
  Serial.printf_P(PSTR("%s::begin() started\n"), name);
  
  if (!module->begin()) {
    Serial.printf_P(PSTR("%s::begin() failed after %lu ms\n"), name, millis() - startTime);
    return false;
  }
  
  Serial.printf_P(PSTR("%s::begin() completed in %lu ms\n"), name, millis() - startTime);
  return true;
}

// =============================================================================
// System Initialization
// =============================================================================

bool initializeSystemModules() {
  Serial.println(F("=== Starting System Module Initialization ==="));
  
  // Step 1: ConfigManager
  Serial.println(F("Step 1: Initializing ConfigManager..."));
  configManager.begin();
  bool needsConfig = configManager.loadConfiguration(g_force_factory_reset);
  Serial.println(needsConfig ? F("ConfigManager: CONFIG MODE") : F("ConfigManager: NORMAL MODE"));
  delay(1);
  
  systemConfig = configManager.getSystemConfig();
  bmsConfig = configManager.getBMSConfig();
  powerConfig = configManager.getPowerConfig();
  
  if (!systemConfig || !bmsConfig || !powerConfig) {
    Serial.println(F("ERROR: Failed to get configuration pointers"));
    return false;
  }
  
  // Step 2: HardwareInterface
  Serial.println(F("Step 2: Initializing HardwareInterface..."));
  hardware = new HardwareInterface(*systemConfig);
  if (!initModule(hardware, "HardwareInterface")) {
    delete hardware;
    hardware = nullptr;
    return false;
  }
  if (g_force_factory_reset) {
    hardware->setBuzzer(BUZZER_MODE_BEEP_ONCE);
  }
  
  delay(1);
  
  // Config mode: skip heavy modules
  if (needsConfig) {
    Serial.println(F("=== CONFIG MODE: Skipping BMS/Power/System modules ==="));
    bms = nullptr;
    powerManagement = nullptr;
    systemManager = nullptr;
    
    Serial.println(F("Step 6: Initializing WebServer (CONFIG MODE)..."));
    webServer = new WebServer(&configManager, nullptr, 80);
    
    Serial.println(F("Step 7: Initializing WiFiManager..."));
    wifiManager = new WiFiManager(hardware);
    delay(1);
    
    hardware->setLED(POWER_LED_PIN, LED_MODE_BLINK_SLOW);
    hardware->setRGBLED(RGB_MODE_BREATHING, {0, 0, 255});
    Serial.println(F("=== CONFIG MODE Initialization Complete ==="));
    return true;
  }
  
  // Normal mode: full initialization
  // Step 3: BMS
  Serial.println(F("Step 3: Initializing BMS..."));
  bms = new BMS(hardware->getI2CInterface(), *bmsConfig);
  
  if (!initModule(bms, "BMS")) {
    Serial.println(F("WARNING: BMS not found, running in limited mode"));
    delete bms;
    bms = nullptr;
  }
  
  delay(1);
  
  // Step 4: PowerManagement
  Serial.println(F("Step 4: Initializing PowerManagement..."));
  Serial.printf_P(PSTR("PowerManagement: BMS pointer is %s\n"), bms ? "valid" : "nullptr");
  powerManagement = new PowerManagement(*powerConfig, *hardware);
  if (!initModule(powerManagement, "PowerManagement")) {
    Serial.println(F("WARNING: PowerManagement not found, running without charging control"));
    delete powerManagement;
    powerManagement = nullptr;
  }
  delay(1);
  
  // Step 5: UPS HID Service (conditional based on config)
  Serial.println(F("Step 5: Initializing UPS HID Service..."));
  if (systemConfig->hid_enabled) {
    upsHidService = new UPS_HID_Service();
    upsHidService->setDeviceIdentifier(systemConfig->identifier);
    upsHidService->setCapacityMode(systemConfig->hid_report_mode);
    upsHidService->setBatteryConfig(bmsConfig->cell_count, 3700, bmsConfig->nominal_capacity_mAh);
    //暂不启动，等待系统启动稳定后启动
  } else {
    Serial.println(F("UPS HID Service disabled in configuration"));
    upsHidService = nullptr;
  }
  delay(1);
  
  // Step 6: SystemManagement
  Serial.println(F("Step 6: Initializing SystemManagement..."));
  // MQTT Service initialization (conditional based on config)
  if (systemConfig->mqtt_broker[0] != 0 && systemConfig->mqtt_port > 0) {
    mqttService = new MQTTService();
  }
  
  // SystemManagement with optional MQTT service
  systemManager = new SystemManagement(*hardware, bms, powerManagement, configManager, upsHidService, mqttService);
  if (!systemManager->initialize()) {
    Serial.println(F("ERROR: Failed to initialize SystemManagement"));
    delete systemManager;
    systemManager = nullptr;
    return false;
  }
  Serial.println(F("SystemManagement initialized successfully"));
  delay(1);
  
  // Step 7: WebServer
  Serial.println(F("Step 7: Initializing WebServer..."));
  webServer = new WebServer(&configManager, systemManager, 80);
  delay(1);
  
  // Step 8: WiFiManager
  Serial.println(F("Step 8: Initializing WiFiManager..."));
  wifiManager = new WiFiManager(hardware);
  delay(1);
  
  Serial.println(F("=== All System Modules Initialized Successfully ==="));
  return true;
}

// =============================================================================
// Arduino Setup
// =============================================================================

void setup() {
  Serial.begin(115200);

  g_force_factory_reset = checkFactoryReset();
  
  Serial.println(F("=== UPS Control System Starting ==="));
  Serial.printf_P(PSTR("Build: %s %s\n"), __DATE__, __TIME__);
  
  if (!initializeSystemModules()) {
    Serial.println(F("FATAL Error: System initialization failed"));
    Serial.println(F("System will restart automatically..."));
    delay(100);
    while (1) delay(1000);
  }
  
  // WiFi setup - 统一使用begin()处理所有情况
  Serial.println(F("Initializing WiFi Manager..."));
  wifiManager->begin(systemConfig);
  
  Serial.println(F("Starting WebServer..."));
  webServer->begin();
  
  initTimeSystem(28800, systemConfig->ntp_server); 
  
  Serial.println(F("=== System Setup Complete ==="));
}

// =============================================================================
// Arduino Loop
// =============================================================================

void loop() {
  if (wifiManager) wifiManager->loop();
  updateTimeSystem();
  
  // Config mode: only basic services, reduced frequency to save power
  if (!systemManager) {
    hardware->update();
    delay(500);  // 降低循环频率，减少芯片发热
    return;
  }
  
  if (!systemManager->isSystemReady()) {
    delay(100);
    return;
  }
  
  systemManager->update();
  
  // Web notification (1s interval)
  static unsigned long lastWebNotify = 0;
  if (millis() - lastWebNotify > 3000) {
    webServer->notifyClients();
    lastWebNotify = millis();
  }
  
  delay(10);
}