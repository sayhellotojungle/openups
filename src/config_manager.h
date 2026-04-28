#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Preferences.h>
#include "data_structures.h"
#include "bms.h"
#include "power_management.h"

class ConfigManager {
public:
    ConfigManager();

    // 初始化（订阅事件等）
    void begin();

    // Configuration lifecycle - 支持强制重置参数
    bool loadConfiguration(bool forceReset = false);
    bool saveConfiguration();
    void resetConfiguration();
    
    // Reset specific configurations
    void resetWiFiConfig();  // 仅重置 WiFi 配置
    
    // Configuration update methods - update memory and optionally save to flash
    bool updateSystemConfig(const Configuration& config, bool immediate = false);
    bool updateBMSConfig(const BMS_Config_t& config, bool immediate = false);
    bool updatePowerConfig(const Power_Config_t& config, bool immediate = false);
    
    // Direct configuration access for manual updates
    Configuration* getSystemConfig() { return &systemConfig; }
    BMS_Config_t* getBMSConfig() { return &bmsConfig; }
    Power_Config_t* getPowerConfig() { return &powerConfig; }
    
    // Utility methods - 返回加载时确定的配置模式状态
    bool isConfigMode() const { return m_isConfigModeRequired; }
    

private:
    Preferences preferences;
    
    // Configuration data
    Configuration systemConfig;
    BMS_Config_t bmsConfig;
    Power_Config_t powerConfig;
    
    // 配置模式状态标志 - 由 loadConfiguration 确定
    bool m_isConfigModeRequired;
    
    // Private helpers
    void loadDefaults();
    void loadSystemDefaults();
    void loadBMSDefaults();
    void loadPowerDefaults();
    
    // 内部重置方法 - 设置为安全保守值
    void resetToDefaults();
    
    bool validateSystemConfig(const Configuration& config);
    bool validateBMSConfig(const BMS_Config_t& config);
    bool validatePowerConfig(const Power_Config_t& config);
    
    // IP地址格式验证辅助函数
    bool isValidIPAddress(const char* ip);
    
    // 生成随机设备标识符 (4个字符: 0-9a-z)
    void generateDeviceIdentifier(char* identifier, size_t size);

    // Internal save methods that actually write to flash
    bool writeToFlash();

    // 事件回调
    static void onConfigChangeRequest(EventType type, void* param);
};

#endif