#include "config_manager.h"
#include "Arduino.h"
#include "pins_config.h"
#include "event_bus.h"
#include "event_types.h"

// 静态实例指针，供事件回调使用
static ConfigManager* s_configManagerInstance = nullptr;

ConfigManager::ConfigManager() : m_isConfigModeRequired(false) {
    loadDefaults();
}

void ConfigManager::begin() {
    s_configManagerInstance = this;
    EventBus::getInstance().subscribe(EVT_CONFIG_SYSTEM_CHANGE_REQUEST, onConfigChangeRequest);
}

bool ConfigManager::loadConfiguration(bool forceReset) {
    Serial.println(F("Loading configuration from flash..."));
    
    // 1. 优先判断：如果强制重置，直接恢复默认值并进入配置模式
    if (forceReset) {
        Serial.println(F("Force reset requested - resetting to defaults"));
        resetToDefaults();
        m_isConfigModeRequired = true;
        
        // 保存默认配置到 Flash
        writeToFlash();
        return true;
    }
    
    // 2. 尝试从 NVS 读取配置
    preferences.begin("ups_config", true); // ReadOnly mode
    
    // Load all configurations together
    Configuration loadedSystemConfig;
    BMS_Config_t loadedBMSConfig;
    Power_Config_t loadedPowerConfig;
    
    size_t systemBytes = preferences.getBytes("sys_config", &loadedSystemConfig, sizeof(Configuration));
    size_t bmsBytes = preferences.getBytes("bms_config", &loadedBMSConfig, sizeof(BMS_Config_t));
    size_t powerBytes = preferences.getBytes("power_config", &loadedPowerConfig, sizeof(Power_Config_t));
    
    preferences.end();
    
    // 3. 检查读取是否成功
    bool systemValid = (systemBytes == sizeof(Configuration)) && validateSystemConfig(loadedSystemConfig);
    bool bmsValid = (bmsBytes == sizeof(BMS_Config_t)) && validateBMSConfig(loadedBMSConfig);
    bool powerValid = (powerBytes == sizeof(Power_Config_t)) && validatePowerConfig(loadedPowerConfig);

    // 4. 核心业务完整性判断
    bool allValid = systemValid && bmsValid && powerValid;
    
    if (!allValid) {
        Serial.println(F("Configuration read failed or invalid - entering config mode"));
        
        // 打印详细错误信息
        if (!systemValid) {
            if (systemBytes > 0) {
                Serial.println(F("  - System configuration invalid or incomplete"));
            } else {
                Serial.println(F("  - System configuration not found"));
            }
        }
        if (!bmsValid) {
            if (bmsBytes > 0) {
                Serial.println(F("  - BMS configuration invalid or incomplete"));
            } else {
                Serial.println(F("  - BMS configuration not found"));
            }
        }
        if (!powerValid) {
            if (powerBytes > 0) {
                Serial.println(F("  - Power configuration invalid or incomplete"));
            } else {
                Serial.println(F("  - Power configuration not found"));
            }
        }
        
        // 恢复默认值并进入配置模式（会自动生成新的identifier）
        resetToDefaults();
        m_isConfigModeRequired = true;
        
        // 保存默认配置到 Flash
        writeToFlash();
        return true;
    }
    
    // 5. 所有检查通过，使用加载的配置
    Serial.println(F("Configuration loaded and validated successfully"));
    systemConfig = loadedSystemConfig;
    bmsConfig = loadedBMSConfig;
    powerConfig = loadedPowerConfig;
    
    // 设置为正常运行模式
    m_isConfigModeRequired = false;
    return false;
}

bool ConfigManager::saveConfiguration() {
    Serial.println(F("Saving all configuration to flash immediately..."));
    return writeToFlash();
}

void ConfigManager::resetWiFiConfig() {
    Serial.println(F("Resetting WiFi configuration to defaults..."));
    
    // 仅重置 WiFi 相关配置，保留其他系统配置
    memset(systemConfig.wifi_ssid, 0, sizeof(systemConfig.wifi_ssid));
    memset(systemConfig.wifi_pass, 0, sizeof(systemConfig.wifi_pass));
    
    // 发布 WiFi 配置变更事件
    EventBus::getInstance().publish(EVT_CONFIG_WIFI_CHANGED, &systemConfig);
    
    // 保存到 Flash
    writeToFlash();
    
}

void ConfigManager::resetConfiguration() {
    Serial.println(F("Resetting configuration to defaults..."));
    
    // Clear flash storage
    preferences.begin("ups_config", false);
    preferences.clear();
    preferences.end();
    
    // Load default values
    resetToDefaults();
    
    // 设置配置模式标志
    m_isConfigModeRequired = true;
}

bool ConfigManager::updateSystemConfig(const Configuration& config, bool immediate) {
    if (!validateSystemConfig(config)) {
        Serial.println(F("Invalid system configuration"));
        return false;
    }
    
    // 检查系统配置是否发生变化（用于事件触发）
    bool systemConfigChanged = (systemConfig.led_brightness != config.led_brightness) ||
                        (systemConfig.buzzer_volume != config.buzzer_volume) ||
                        (systemConfig.buzzer_enabled != config.buzzer_enabled);
    
    // 检查 WiFi 配置是否发生变化
    bool wifiConfigChanged = (strcmp(systemConfig.wifi_ssid, config.wifi_ssid) != 0) ||
                            (strcmp(systemConfig.wifi_pass, config.wifi_pass) != 0);
    
    // 检查 HID 配置是否发生变化
    bool hidConfigChanged = (systemConfig.hid_enabled != config.hid_enabled) ||
                           (systemConfig.hid_report_mode != config.hid_report_mode);
    
    // Update configuration in memory
    systemConfig = config;
    Serial.println(F("System configuration updated in memory"));
    
    // 如果系统配置发生变化，发布系统配置事件
    if (systemConfigChanged) {
        Serial.println(F("[ConfigMgr] System config changed - publishing event"));
        EventBus::getInstance().publish(EVT_CONFIG_SYSTEM_CHANGED, &systemConfig);
    }
    
    // 如果 WiFi 配置发生变化，发布 WiFi 配置事件
    if (wifiConfigChanged) {
        Serial.println(F("[ConfigMgr] WiFi config changed - publishing event"));
        EventBus::getInstance().publish(EVT_CONFIG_WIFI_CHANGED, &systemConfig);
    }
    
    // 如果 HID 配置发生变化，发布 UPS 配置事件
    if (hidConfigChanged) {
        Serial.println(F("[ConfigMgr] Publishing EVT_CONFIG_UPS_CHANGED event"));
        EventBus::getInstance().publish(EVT_CONFIG_UPS_CHANGED, &systemConfig);
    }
    
    // Optionally save to flash immediately
    if (immediate) {
        Serial.println(F("Saving system configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

bool ConfigManager::updateBMSConfig(const BMS_Config_t& config, bool immediate) {
    if (!validateBMSConfig(config)) {
        Serial.println(F("Invalid BMS configuration"));
        return false;
    }
    
    // Update configuration in memory
    bmsConfig = config;
 
    // Optionally save to flash immediately
    if (immediate) {
        Serial.println(F("Saving BMS configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

bool ConfigManager::updatePowerConfig(const Power_Config_t& config, bool immediate) {
    if (!validatePowerConfig(config)) {
        Serial.println(F("Invalid power configuration"));
        return false;
    }
    
    // Update configuration in memory
    powerConfig = config;
    
    // Optionally save to flash immediately
    if (immediate) {
        Serial.println(F("Saving power configuration to flash immediately..."));
        return writeToFlash();
    }
    
    return true;
}

// Private helper methods

void ConfigManager::loadDefaults() {
    loadSystemDefaults();
    loadBMSDefaults();
    loadPowerDefaults();
}

void ConfigManager::loadSystemDefaults() {
    // System configuration defaults
    memset(&systemConfig, 0, sizeof(Configuration));
    
    // 生成随机设备标识符 (4个字符: 0-9a-z)
    generateDeviceIdentifier(systemConfig.identifier, sizeof(systemConfig.identifier));
    Serial.printf_P(PSTR("Generated device identifier: %s\n"), systemConfig.identifier);
    
    systemConfig.buzzer_enabled = true;
    systemConfig.buzzer_volume = 50;  // 50% 音量
    systemConfig.led_brightness = 30; // 30% 亮度
    systemConfig.hid_enabled = true;  // 默认启用 HID 服务
    systemConfig.hid_report_mode = 2; // 默认百分比模式 (0: mAh, 1: mWh, 2: %)

    // 静态 IP 配置默认值 - 默认使用 DHCP
    systemConfig.use_static_ip = false;
    strncpy(systemConfig.static_ip, "192.168.1.100", sizeof(systemConfig.static_ip) - 1);
    strncpy(systemConfig.static_gateway, "192.168.1.1", sizeof(systemConfig.static_gateway) - 1);
    strncpy(systemConfig.static_subnet, "255.255.255.0", sizeof(systemConfig.static_subnet) - 1);
    strncpy(systemConfig.static_dns, "8.8.8.8", sizeof(systemConfig.static_dns) - 1);

    // NTP 服务器默认值
    strncpy(systemConfig.ntp_server, "ntp.aliyun.com", sizeof(systemConfig.ntp_server) - 1);
}

void ConfigManager::loadBMSDefaults() {
    // BMS configuration defaults - 使用安全的保守值
    bmsConfig = BMS::getDefaultConfig(3); // 3 串默认配置
    
    // 确保为安全保守值
    if (bmsConfig.max_charge_current > 1000) {
        bmsConfig.max_charge_current = 1000; // 限制为 1A 安全电流
    }
}

void ConfigManager::loadPowerDefaults() {
    // Power configuration defaults - 使用安全的保守值
    powerConfig = PowerManagement::getDefaultConfig();
    
    // 确保为安全保守值
    if (powerConfig.max_charge_current > 500) {
        powerConfig.max_charge_current = 500; // 限制为 0.5A 安全电流
    }
    
    Serial.println(F("Power default configuration loaded with safe values"));
}

bool ConfigManager::validateSystemConfig(const Configuration& config) {
    // Basic validation
    
    // 验证设备标识符：必须为4个字符，且为0-9a-z
    if (strlen(config.identifier) != 4) {
        Serial.printf_P(PSTR("  Invalid device identifier length: %d (must be 4)\n"), strlen(config.identifier));
        return false;
    }
    
    for (int i = 0; i < 4; i++) {
        char c = config.identifier[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) {
            Serial.printf_P(PSTR("  Invalid device identifier character: '%c' at position %d\n"), c, i);
            return false;
        }
    }
    
    // Validate WiFi SSID (can be empty for AP mode)
    // Validate LED brightness range
    if (config.led_brightness > 100) {
        Serial.println(F("  LED brightness out of range (0-100)"));
        return false;
    }
    
    // Validate buzzer volume range
    if (config.buzzer_volume > 100) {
        Serial.println(F("  Buzzer volume out of range (0-100)"));
        return false;
    }
    
    // Validate hid_report_mode range
    if (config.hid_report_mode > 2) {
        Serial.println(F("  HID report mode out of range (0-2)"));
        return false;
    }
    
    // 验证静态 IP 配置（如果启用）
    if (config.use_static_ip) {
        if (!isValidIPAddress(config.static_ip)) {
            Serial.println(F("  Invalid static IP address format"));
            return false;
        }
        if (!isValidIPAddress(config.static_gateway)) {
            Serial.println(F("  Invalid gateway IP address format"));
            return false;
        }
        if (!isValidIPAddress(config.static_subnet)) {
            Serial.println(F("  Invalid subnet mask format"));
            return false;
        }
        if (!isValidIPAddress(config.static_dns)) {
            Serial.println(F("  Invalid DNS server IP address format"));
            return false;
        }
    }
    
    return true;
}

// IP 地址格式验证辅助函数
bool ConfigManager::isValidIPAddress(const char* ip) {
    if (ip == nullptr || strlen(ip) == 0) {
        return false;
    }
    
    int num, dots = 0;
    const char* ptr = ip;
    
    while (*ptr) {
        // 提取数字
        num = 0;
        while (*ptr && *ptr != '.') {
            if (*ptr < '0' || *ptr > '9') {
                return false; // 非数字字符
            }
            num = num * 10 + (*ptr - '0');
            ptr++;
        }
        
        // 检查数字范围 (0-255)
        if (num < 0 || num > 255) {
            return false;
        }
        
        // 检查点号
        if (*ptr == '.') {
            dots++;
            ptr++;
            if (dots > 3) {
                return false; // 超过 3 个点
            }
        } else if (*ptr != '\0') {
            return false; // 非法字符
        }
    }
    
    // 必须恰好有 3 个点（4 个数字）
    return (dots == 3);
}

// 生成随机设备标识符 (4个字符: 0-9a-z)
void ConfigManager::generateDeviceIdentifier(char* identifier, size_t size) {
    if (size < 5) { // 需要至少5字节空间 (4字符 + 结束符)
        Serial.println(F("Warning: identifier buffer too small"));
        strncpy(identifier, "0000", size - 1);
        identifier[size - 1] = '\0';
        return;
    }
    
    // 字符集: 0-9 (10个) + a-z (26个) = 36个字符
    const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const size_t charset_size = sizeof(charset) - 1; // 36
    
    // 使用硬件随机数生成器
    randomSeed(esp_random());
    
    for (int i = 0; i < 4; i++) {
        identifier[i] = charset[random(0, charset_size)];
    }
    identifier[4] = '\0'; // 确保字符串正确终止
}

bool ConfigManager::validateBMSConfig(const BMS_Config_t& config) {
    // 关键业务完整性检查 1: 串数必须在合法范围 (3-5)
    if (config.cell_count < 3 || config.cell_count > 5) {
        Serial.printf_P(PSTR("  Invalid cell count: %d (must be 3-5)\n"), config.cell_count);
        return false;
    }
    
    // 容量必须有效
    if (config.nominal_capacity_mAh == 0) {
        Serial.println(F("  Invalid nominal capacity: 0"));
        return false;
    }
    
    // 电压阈值必须合理 (单位：mV)
    if (config.cell_ov_threshold <= config.cell_uv_threshold) {
        Serial.println(F("  OV threshold must be greater than UV threshold"));
        return false;
    }
    
    // 充电电流必须大于 0
    if (config.max_charge_current == 0) {
        Serial.println(F("  Max charge current is 0"));
        return false;
    }
    
    return true;
}

bool ConfigManager::validatePowerConfig(const Power_Config_t& config) {
    // 关键业务完整性检查 2: 充电电流必须大于 0
    if (config.max_charge_current == 0) {
        Serial.println(F("  Max charge current is 0"));
        return false;
    }
    
    // 放电电流必须大于 0
    if (config.max_discharge_current == 0) {
        Serial.println(F("  Max discharge current is 0"));
        return false;
    }
    
    // 验证时间窗口配置
    if (config.charging_window_count > 5) {
        Serial.printf_P(PSTR("  Invalid charging window count: %d (max 5)\n"), config.charging_window_count);
        return false;
    }
    
    // 验证每个时间窗口的合法性
    for (uint8_t i = 0; i < config.charging_window_count; i++) {
        const ChargingTimeWindow_t& window = config.charging_windows[i];
        
        // 检查掩码是否有效（至少有一天）
        if (window.day_mask == 0) {
            Serial.printf_P(PSTR("  Window %d has invalid day mask (0)\n"), i);
            return false;
        }
        
        // 检查小时范围
        if (window.start_hour > 23 || window.end_hour > 24) {
            Serial.printf_P(PSTR("  Window %d has invalid hour range (%d-%d)\n"),
                           i, window.start_hour, window.end_hour);
            return false;
        }
        
        // 检查开始时间不能晚于结束时间
        if (window.start_hour >= window.end_hour) {
            Serial.printf_P(PSTR("  Window %d start hour (%d) must be before end hour (%d)\n"), 
                           i, window.start_hour, window.end_hour);
            return false;
        }
    }
    
    return true;
}

// 内部重置方法 - 将所有参数设置为安全的保守值
void ConfigManager::resetToDefaults() {
    Serial.println(F("Resetting to safe default values..."));
    
    // 使用 loadDefaults() 加载各模块的默认配置
    // 这样可以保持架构一致性，默认配置由各模块自己提供
    loadDefaults();
    
    Serial.println(F("Safe default values loaded"));
}

bool ConfigManager::writeToFlash() {
    Serial.println(F("Writing configuration to flash..."));    
    preferences.begin("ups_config", false); // ReadWrite mode
    
    bool success = true;
    
    // Load current flash configurations for comparison
    Configuration flashSystemConfig;
    BMS_Config_t flashBMSConfig;
    Power_Config_t flashPowerConfig;
    
    size_t systemBytes = preferences.getBytes("sys_config", &flashSystemConfig, sizeof(Configuration));
    size_t bmsBytes = preferences.getBytes("bms_config", &flashBMSConfig, sizeof(BMS_Config_t));
    size_t powerBytes = preferences.getBytes("power_config", &flashPowerConfig, sizeof(Power_Config_t));
    

    bool systemChanged = (systemBytes != sizeof(Configuration)) || 
                        (memcmp(&systemConfig, &flashSystemConfig, sizeof(Configuration)) != 0);
    bool bmsChanged = (bmsBytes != sizeof(BMS_Config_t)) || 
                     (memcmp(&bmsConfig, &flashBMSConfig, sizeof(BMS_Config_t)) != 0);
    bool powerChanged = (powerBytes != sizeof(Power_Config_t)) || 
                       (memcmp(&powerConfig, &flashPowerConfig, sizeof(Power_Config_t)) != 0);
    
    // Save only changed configurations
    if (systemChanged) {
        if (preferences.putBytes("sys_config", &systemConfig, sizeof(Configuration))) {
            Serial.println(F("System configuration saved to flash"));
        } else {
            Serial.println(F("Failed to save system configuration to flash"));
            success = false;
        }
    } else {
        Serial.println(F("System configuration unchanged, skipping flash write"));
    }
    
    if (bmsChanged) {
        if (preferences.putBytes("bms_config", &bmsConfig, sizeof(BMS_Config_t))) {
            // 发布 BMS 配置变更事件
            EventBus::getInstance().publish(EVT_CONFIG_BMS_CHANGED, &bmsConfig);
            Serial.println(F("BMS configuration saved to flash"));
        } else {
            Serial.println(F("Failed to save BMS configuration to flash"));
            success = false;
        }
    } else {
        Serial.println(F("BMS configuration unchanged, skipping flash write"));
    }
    
    if (powerChanged) {
        if (preferences.putBytes("power_config", &powerConfig, sizeof(Power_Config_t))) {
            // 发布电源配置变更事件
            EventBus::getInstance().publish(EVT_CONFIG_POWER_CHANGED, &powerConfig);
            Serial.println(F("Power configuration saved to flash"));
        } else {
            Serial.println(F("Failed to save power configuration to flash"));
            success = false;
        }
    } else {
        Serial.println(F("Power configuration unchanged, skipping flash write"));
    }
    
    preferences.end();
    
    if (success) {
        Serial.println(F("Configuration write to flash completed successfully"));
    } else {
        Serial.println(F("Configuration write to flash completed with errors"));
    }
    
    return success;
}

void ConfigManager::onConfigChangeRequest(EventType type, void* param) {
    if (!s_configManagerInstance || !param) return;
    const Configuration* config = static_cast<const Configuration*>(param);
    s_configManagerInstance->updateSystemConfig(*config, true);
}