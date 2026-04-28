/*
 * System Management Module Implementation
 * 
 * Finite State Machine (FSM) implementation for system coordination.
 * Implements a clear state machine with safety-first design:
 * 1. Data Collection - Read all sensors first
 * 2. State Decision - Determine next state based on conditions
 * 3. State Transition - Execute state change with safety checks
 * 4. Action Execution - Issue mutually exclusive control commands
 */

#include "system_management.h"
#include "config_manager.h"
#include "data_structures.h"
#include "hardware_interface.h"
#include "i2c_interface.h"
#include "bms.h"
#include "power_management.h"
#include "web_server.h"
#include "pins_config.h"
#include "ups_hid_service.h"
#include "mqtt_service.h"
#include "utils.h"
#include <WiFi.h>

// External reference to system config (defined in sketch_jan14a.ino)
extern Configuration* systemConfig;

// =============================================================================
// Constructor
// =============================================================================

SystemManagement::SystemManagement(
    HardwareInterface& hw,
    BMS* battery,
    PowerManagement* pm,
    ConfigManager& cm,
    UPS_HID_Service* upsHid,
    MQTTService* mqtt
) : hardware(&hw)
    , bms(battery)
    , powerManagement(pm)
    , configManager(&cm)
    , upsHidService(upsHid),
    mqttService(mqtt)  // MQTT 服务注入
    , currentState(SYS_STATE_INIT)
    , previousState(SYS_STATE_INIT)
    , systemInitialized(false)
    , isShutdownRequested(false)
    , millisOverflowCount(0)
    , lastStateChangeTime(0)
    , lastStatusUpdateTime(0)
    , lastIndicatorUpdateTime(0)
    , lastMillisValue(0)
    , criticalRecoveryCounter_(0)
    , delayedStartTime_(0)
    , delayedStartDelay_(5000)  // 默认延迟 5 秒
    , delayedStartExecuted_(false)
    , lastPowerModeChangeTime(0)
    , lastRegisterCheckTime(0)
    , regMismatchCountBQ24780s(0)
    , regMismatchCountBQ76920(0)
    , bq24780sRegWarning(false)
    , bq76920RegWarning(false)
    , emergency_discharge_disabled_(false) {
    
    // 初始化全局状态黑板
    memset(&globalState, 0, sizeof(System_Global_State));
    
    // 如果 BMS 或 PowerManagement 为 nullptr，在全局状态中记录警告 (非故障)
   if (!bms) {
        Serial.println(F("SystemManagement: BMS is nullptr - will run in WARNING mode"));
        globalState.overall_status = 1;  // 警告状态，非故障
    }
   if (!powerManagement) {
        Serial.println(F("SystemManagement: PowerManagement is nullptr - will run in WARNING mode"));
        globalState.overall_status = 1;  // 警告状态，非故障
    }
}

// =============================================================================
// Destructor
// =============================================================================

SystemManagement::~SystemManagement() {
    // USB HID 服务由外部（setup）管理，不在这里释放
}

// =============================================================================
// Initialization - 简化：允许模块初始化失败，不阻止系统启动
// =============================================================================

bool SystemManagement::initialize() {
    Serial.println(F("=== System Management FSM Initializing ==="));
    
    lastStateChangeTime = millis();
    lastMillisValue = millis();
    millisOverflowCount = 0;
    criticalRecoveryCounter_ = 0;
    
    // 解析固件版本字符串 "SIG:<PROJECT_NAME>:VER:<VERSION>"
    // 示例："SIG:OPENUPS-ESP32S3:VER:1.0.2"
    Utils::parseFirmwareVersion(FIRMWARE_ID_TAG, 
                                globalState.system.firmware_version, sizeof(globalState.system.firmware_version),
                                globalState.system.hardware_version, sizeof(globalState.system.hardware_version));
    
    // 订阅配置变更事件
    EventBus::getInstance().subscribe(EVT_CONFIG_SYSTEM_CHANGED, onConfigSystemChanged);
    EventBus::getInstance().subscribe(EVT_CONFIG_BMS_CHANGED, onConfigBmsChanged);
    EventBus::getInstance().subscribe(EVT_CONFIG_POWER_CHANGED, onConfigPowerChanged);
    
    // 订阅按键长按事件 - 重置 WiFi 配置
    EventBus::getInstance().subscribe(EVT_BTN_LONG_PRESS, onBtnLongPress);
    
    // 订阅 BMS 故障检测事件
    EventBus::getInstance().subscribe(EVT_BMS_FAULT_DETECTED, onBmsFaultDetected);
    
    // 订阅 BMS 运输模式请求事件
    EventBus::getInstance().subscribe(EVT_BMS_SHIPMODE_REQUEST, onBmsShipModeRequest);

    hardware->syncInitialState();
    
    // 初始状态转换：从 INIT 状态开始，由 handleStateInit 决定下一步
    transitionToState(SYS_STATE_INIT);
    
    systemInitialized = true;
    hardware->setBuzzer(BUZZER_MODE_BEEP_ONCE);

    Serial.println(F("=== System Management FSM Ready ==="));
    return true;
}

// =============================================================================
// FSM 主循环 - 四步流程（重构为明确状态机）
// =============================================================================

void SystemManagement::update() {
    if (!systemInitialized) {
        return;
    }
    
    // =========================================
    // [优先] 处理所有硬件中断和事件，确保数据是最新的
    // =========================================
    if (hardware) {
        hardware->update();
    }
    
    // =========================================
    // 新增：检查并执行延迟启动
    // =========================================
    checkAndExecuteDelayedStart();
    
    // =========================================
    // Step 1: 数据收集 - 读取所有传感器数据
    // =========================================
    collectData();
    
    // =========================================
    // Step 2: 状态决策 - 基于更新后的状态决定下一状态
    // =========================================
    SystemState nextState = decideNextState();
    
    // =========================================
    // Step 3: 状态流转 - 执行状态切换
    // =========================================
    performStateTransition(nextState);
    
    // =========================================
    // Step 4: 动作执行 - 发出互斥的控制指令
    // =========================================
    executeStateActions();

    // 状态循环动作（每个周期执行）
    onStateLoop();

}

// =============================================================================
// Step 1: 数据收集 - 分层采集策略
// =============================================================================

void SystemManagement::collectData() {
    // 更新系统运行时间（正确处理 millis() 溢出，使用秒为单位）
    unsigned long current_time = millis();
    
    // 检测 millis() 溢出：如果当前值小于上一次值，说明发生了溢出
    if (current_time < lastMillisValue) {
        millisOverflowCount++;
    }
    lastMillisValue = current_time;
    
    // 计算运行时间（秒）
    // 每次溢出 = 2^32 ms = 4294967 秒（约49.7天）
    // uptime = (溢出次数 * 4294967) + (当前millis / 1000)
    globalState.system.uptime = (millisOverflowCount * 4294967UL) + (current_time / 1000);

    // 检查保护标志（快速响应故障）
    globalState.over_current_protection = (globalState.power.fault_type != POWER_FAULT_NONE);

    // Step 1: 更新 BMS 数据（如果 BMS 存在）
    if (bms != nullptr) {
        // 检查是否需要更新慢速数据
        bms->update(globalState);
        // 注意：即使不更新 BMS，也保持全局状态中的旧数据有效
    } else {
        // BMS 不存在时，标记为未连接
        globalState.bms.is_connected = false;
        globalState.bms.soc = 0;
        globalState.bms.voltage = 0;
        globalState.bms.current = 0;
    }
    
    // Step 2: 更新 PowerManagement 数据（如果存在，且依赖 BMS 数据）
    if (powerManagement != nullptr) {
        // PowerManagement 可以每循环更新，因为它需要快速响应 AC 状态
        powerManagement->update(globalState);
    } else {
        // PowerManagement 不存在时，标记基本状态
        globalState.power.ac_present = false;
    }

    // 每 2 秒更新一次 WiFi 状态（减少系统调用开销）
    if (current_time - lastSlowDataUpdate >= 2000) {
        globalState.system.wifi_connected = WiFi.status() == WL_CONNECTED;
        if (globalState.system.wifi_connected) {
            globalState.system.wifi_rssi = WiFi.RSSI();
            strncpy(globalState.system.wifi_ssid, WiFi.SSID().c_str(), 
                    sizeof(globalState.system.wifi_ssid) - 1);
        }
        lastSlowDataUpdate = current_time;
    }
    
    // 更新电源模式（带防抖）
    updatePowerMode();

    // 寄存器一致性检查，每 5 秒执行一次
    if (current_time - lastRegisterCheckTime >= 5000) {
        lastRegisterCheckTime = current_time;
        checkBQ24780sRegisters();
        checkBQ76920Registers();
    }
}

// =============================================================================
// Step 2: 状态决策 - 基于传感器数据和安全优先级 (简化版)
// =============================================================================

SystemManagement::SystemState SystemManagement::decideNextState() {
    // =========================================
    // 优先级 1: CRITICAL 条件检测（模块掉线/不存在）
    // =========================================
    if (checkCriticalConditions()) {
        return SYS_STATE_CRITICAL;
    }
    
    // =========================================
    // 优先级 2: WARNING 条件检测（模块在线但存在故障）
    // =========================================
    if (checkWarningConditions()) {
        return SYS_STATE_WARNING;
    }
    
    // =========================================
    // 默认正常状态
    // =========================================
    return SYS_STATE_NORMAL;
}

// =============================================================================
// Step 3: 状态流转
// =============================================================================

void SystemManagement::performStateTransition(SystemState nextState) {
    if (nextState != currentState) {
        // 防抖检查
        if (millis() - lastStateChangeTime < STATE_DEBOUNCE_TIME) {
            return;
        }
        
        SystemState oldState = currentState;
        
        // 退出旧状态
        onStateExit(oldState);
        
        // 转换到新状态
        currentState = nextState;
        lastStateChangeTime = millis();
        
        // 进入新状态
        onStateEnter(currentState);
        
        // 同步 overall_status 到全局状态
        syncOverallStatus();
        
    }
}

// =============================================================================
// Step 4: 动作执行 - 精简版（仅数据采集，不执行控制）
// =============================================================================

void SystemManagement::executeStateActions() {
    //还是要发布数据的，比如说这个，比如说未来的mqtt。
    // 更新UPS HID服务
    if (upsHidService != nullptr) {
        upsHidService->update(globalState);
    }
    
    // MQTT loop
    if (mqttService) mqttService->loop();
}

// =============================================================================
// 状态生命周期回调
// =============================================================================

void SystemManagement::onStateEnter(SystemState newState) {
    Serial.printf_P(PSTR("FSM: Entering state: %s\n"), 
                   newState == SYS_STATE_INIT ? "INIT" :
                   newState == SYS_STATE_NORMAL ? "NORMAL" :
                   newState == SYS_STATE_WARNING ? "WARNING" : "CRITICAL");
    
    // 进入新状态时重置恢复计数器
    if (newState != SYS_STATE_CRITICAL) {
        criticalRecoveryCounter_ = 0;
    }
}

void SystemManagement::onStateExit(SystemState oldState) {
    // 清除旧状态的指示
    hardware->setBuzzer(BUZZER_MODE_OFF);
}

void SystemManagement::onStateLoop() {
    // 根据当前状态调用对应的状态处理函数
    switch (currentState) {
        case SYS_STATE_INIT:
            handleStateInit();
            break;
        case SYS_STATE_NORMAL:
            handleStateNormal();
            break;
        case SYS_STATE_WARNING:
            handleStateWarning();
            break;
        case SYS_STATE_CRITICAL:
            handleStateCritical();
            break;
    }

    // 紧急关机放电保护检查（INIT 状态之外均可运行）
    if (currentState != SYS_STATE_INIT) {
        checkEmergencyShutdown();
    }

    // 更新放电指示灯（INIT 状态之外均可运行）
    if (currentState != SYS_STATE_INIT) {
        updateDischargeIndicator();
    }
}




// =============================================================================
// 事件处理回调
// =============================================================================

void SystemManagement::onBtnLongPress(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        // 重置 WiFi 配置
        if (systemManager->configManager != nullptr) {
            systemManager->hardware->setBuzzer(BUZZER_MODE_BEEP_ONCE);
            systemManager->configManager->resetWiFiConfig();
        }
    }
}

// =============================================================================
// 辅助方法
// =============================================================================

void SystemManagement::transitionToState(SystemState newState) {
    if (currentState != newState) {
        SystemState oldState = currentState;
        onStateExit(oldState);
        currentState = newState;
        lastStateChangeTime = millis();
        onStateEnter(currentState);
    }
}

bool SystemManagement::isSystemReady() const {
   return systemInitialized;
}

SystemManagement::SystemState SystemManagement::getState() const {
    return currentState;
}

const char* SystemManagement::getStateString() const {
    switch (currentState) {
        case SYS_STATE_INIT: return "INIT";
        case SYS_STATE_NORMAL: return "NORMAL";
        case SYS_STATE_WARNING: return "WARNING";
        case SYS_STATE_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}


// =============================================================================
// FSM 状态处理函数 - 每个状态独立的处理逻辑
// =============================================================================

/**
 * @brief 处理 INIT 状态：初始化检查
 * 若设备就绪，转入 NORMAL；若设备缺失，转入 CRITICAL
 */
void SystemManagement::handleStateInit() {
    // 检查关键模块是否存在
    bool bms_ready = (bms != nullptr);
    bool pm_ready = (powerManagement != nullptr);
    
    if (bms_ready && pm_ready) {
        // 设备就绪，转入 NORMAL
        Serial.println(F("[FSM] INIT: All modules ready, transitioning to NORMAL"));
        transitionToState(SYS_STATE_NORMAL);
    } else {
        // 设备缺失，转入 CRITICAL
        Serial.println(F("[FSM] INIT: Missing modules, transitioning to CRITICAL"));
        if (!bms_ready) Serial.println(F("[FSM] INIT: BMS is nullptr"));
        if (!pm_ready) Serial.println(F("[FSM] INIT: PowerManagement is nullptr"));
        transitionToState(SYS_STATE_CRITICAL);
    }
}

/**
 * @brief 处理 NORMAL 状态：正常运行
 * 检测是否满足进入 WARNING 或 CRITICAL 的条件
 * 若保持正常，调用 applyIndicatorNormal() 执行呼吸绿灯
 */
void SystemManagement::handleStateNormal() {
    // 优先检测 CRITICAL 条件（模块掉线）
    if (checkCriticalConditions()) {
        Serial.println(F("[FSM] NORMAL: CRITICAL condition detected"));
        transitionToState(SYS_STATE_CRITICAL);
        return;
    }
    
    // 检测 WARNING 条件（模块在线但存在故障）
    if (checkWarningConditions()) {
        Serial.println(F("[FSM] NORMAL: WARNING condition detected"));
        transitionToState(SYS_STATE_WARNING);
        return;
    }
    
    // 保持正常状态，执行呼吸绿灯
    applyIndicatorNormal();
}

/**
 * @brief 处理 WARNING 状态：警告
 * 优先检测是否降级为 CRITICAL
 * 若故障恢复，转回 NORMAL
 * 若保持警告，调用 applyIndicatorWarning() 执行呼吸橙灯
 */
void SystemManagement::handleStateWarning() {
    // 优先检测 CRITICAL 条件（模块掉线/BMS全部错误）
    if (checkCriticalConditions()) {
        Serial.println(F("[FSM] WARNING: CRITICAL condition detected, downgrading"));
        transitionToState(SYS_STATE_CRITICAL);
        return;
    }
    
    // 检测故障是否恢复
    if (!checkWarningConditions()) {
        Serial.println(F("[FSM] WARNING: Conditions recovered, transitioning to NORMAL"));
        transitionToState(SYS_STATE_NORMAL);
        return;
    }
    
    // 保持警告状态，执行呼吸橙灯
    applyIndicatorWarning();
}

/**
 * @brief 处理 CRITICAL 状态：危急
 * 实现恢复防抖：只有当设备连续 N 次循环检测都正常且无故障时，才允许转回 NORMAL
 * 若保持危急，调用 applyIndicatorCritical() 执行呼吸红灯+蜂鸣器间歇响
 */
void SystemManagement::handleStateCritical() {
    // 检查是否满足恢复条件
    if (!checkCriticalConditions() && !checkWarningConditions()) {
        // 条件恢复，增加计数器
        criticalRecoveryCounter_++;
        Serial.printf_P(PSTR("[FSM] CRITICAL: Recovery counter: %d/%d\n"), 
                       criticalRecoveryCounter_, CRITICAL_RECOVERY_COUNT);
        
        // 连续 N 次检测正常，才允许恢复
        if (criticalRecoveryCounter_ >= CRITICAL_RECOVERY_COUNT) {
            Serial.println(F("[FSM] CRITICAL: Recovery confirmed, transitioning to NORMAL"));
            criticalRecoveryCounter_ = 0;
            transitionToState(SYS_STATE_NORMAL);
            return;
        }
    } else {
        // 条件未恢复，重置计数器
        if (criticalRecoveryCounter_ > 0) {
            Serial.println(F("[FSM] CRITICAL: Recovery counter reset due to ongoing issues"));
            criticalRecoveryCounter_ = 0;
        }
    }
    
    // 保持危急状态，执行呼吸红灯+蜂鸣器间歇响
    applyIndicatorCritical();
}

// =============================================================================
// 条件检查函数 - 提取复杂的布尔逻辑
// =============================================================================

/**
 * @brief 检查是否满足 CRITICAL 条件（模块掉线/不存在/BMS全部错误）
 * @return true 如果存在 CRITICAL 条件
 */
bool SystemManagement::checkCriticalConditions() const {
    // BMS 模块不存在（指针为空）
    if (bms == nullptr) {
        return true;
    }
    
    // PowerManagement 模块不存在（指针为空）
    if (powerManagement == nullptr) {
        return true;
    }
    
    // BMS 模块未连接（is_connected == false）
    if (!globalState.bms.is_connected) {
        return true;
    }
    
    // PowerManagement 模块未连接（bq24780s_connected == false）
    if (!globalState.power.bq24780s_connected) {
        return true;
    }
    
    // BMS 全部错误视为 CRITICAL
    // 包括：过压、欠压、过流、短路、过温、芯片错误、被动关机
    if (globalState.bms.fault_type != BMS_FAULT_NONE) {
        return true;
    }
    
    return false;
}

/**
 * @brief 检查是否满足 WARNING 条件（模块在线但存在非BMS故障）
 * @return true 如果存在 WARNING 条件
 */
bool SystemManagement::checkWarningConditions() const {
    // PowerManagement 故障（fault_type != NONE）
    if (powerManagement && globalState.power.fault_type != POWER_FAULT_NONE) {
        return true;
    }
    
    // 硬件保护标志
    if (globalState.over_current_protection || globalState.over_temp_protection) {
        return true;
    }

    // 芯片保护配置是否一致

    if (bq24780sRegWarning || bq76920RegWarning) {
        return true;
    }
    
    return false;
}

// =============================================================================
// 指示灯控制函数 - 纯动作执行，不含业务逻辑
// =============================================================================

/**
 * @brief 设置正常指示：绿呼吸，关蜂鸣
 */
void SystemManagement::applyIndicatorNormal() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {0, 255, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_ON);
    hardware->setBuzzer(BUZZER_MODE_OFF);
}

/**
 * @brief 设置警告指示：橙呼吸，关蜂鸣
 */
void SystemManagement::applyIndicatorWarning() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {255, 165, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_BLINK_SLOW);
    hardware->setBuzzer(BUZZER_MODE_OFF);
}

/**
 * @brief 设置危急指示：红呼吸，开蜂鸣间歇
 */
void SystemManagement::applyIndicatorCritical() {
    hardware->setRGBLED(RGB_MODE_BREATHING, {255, 0, 0});
    hardware->setLED(POWER_LED_PIN,LED_MODE_BLINK_FAST);
    hardware->setBuzzer(BUZZER_MODE_ALARM);
}

/**
 * @brief 紧急关机放电保护检查
 * 条件：AC 离线 且 BMS SOC <= discharge_soc_stop * 150% 时，设置 emergency_shutdown = true
 *       SOC <= discharge_soc_stop 时，关闭 BMS 放电开关
 * 恢复：AC 在线后 emergency_shutdown = false
 *       BMS SOC >= discharge_soc_stop * 150% 时，打开 BMS 放电开关
 */
void SystemManagement::checkEmergencyShutdown() {
    if (bms == nullptr || powerManagement == nullptr || !globalState.bms.is_connected) {
        return;
    }

    // 获取放电停止 SOC 阈值
    Power_Config_t* powerConfig = configManager->getPowerConfig();
    float soc_stop = powerConfig->discharge_soc_stop;
    float soc_threshold = soc_stop * 1.5f;  // 150% 阈值
    float current_soc = globalState.bms.soc;
    bool ac_online = globalState.power.ac_present;

    if (!ac_online) {
        // AC 离线：检查是否需要紧急关机
        if (current_soc <= soc_threshold) {
            if (!globalState.emergency_shutdown) {
                globalState.emergency_shutdown = true;
                Serial.printf_P(PSTR("[EmergencyShutdown] AC offline + SOC %.1f%% <= %.1f%% (150%% of %.1f%%), emergency shutdown activated\n"),
                               current_soc, soc_threshold, soc_stop);
            }

            // SOC <= discharge_soc_stop 时，关闭 BMS 放电开关
            if (current_soc <= soc_stop && !emergency_discharge_disabled_) {
                Serial.printf_P(PSTR("[EmergencyShutdown] SOC %.1f%% <= %.1f%%, disabling BMS discharge\n"),
                               current_soc, soc_stop);
                bms->disableDischarge();
                emergency_discharge_disabled_ = true;
            }
        }
    } else {
        // AC 在线：恢复条件
        if (globalState.emergency_shutdown) {
            globalState.emergency_shutdown = false;
            Serial.println(F("[EmergencyShutdown] AC online, emergency shutdown cleared"));
        }

        // SOC >= discharge_soc_stop * 150% 时，重新打开 BMS 放电开关
        if (emergency_discharge_disabled_ && current_soc >= soc_threshold) {
            Serial.printf_P(PSTR("[EmergencyShutdown] AC online + SOC %.1f%% >= %.1f%%, re-enabling BMS discharge\n"),
                           current_soc, soc_threshold);
            bms->enableDischarge();
            emergency_discharge_disabled_ = false;
        }
    }
}

/**
 * @brief 更新放电指示灯
 * 独立于主状态颜色逻辑，但在 INIT 状态之外均可运行
 * 逻辑：若 globalState.bms.current < -10 (mA)，亮 DISCHARGE_LED_PIN；否则灭
 * 
 * 电流阈值说明：
 * - 正值表示充电电流
 * - 负值表示放电电流
 * - 阈值 -10mA 用于过滤微小电流波动，避免指示灯频繁闪烁
 */
void SystemManagement::updateDischargeIndicator() {
    // 电流阈值防抖：仅在放电电流 > 10mA (即 current < -10) 时亮起
    if (globalState.bms.current < DISCHARGE_CURRENT_THRESHOLD) {
        hardware->setLED(DISCHARGING_LED_PIN, LED_MODE_ON);
        // AC 离线时，放电指示灯亮起的同时启动蜂鸣器间断长鸣
        if (!globalState.power.ac_present) {
            hardware->setBuzzer(BUZZER_MODE_CONTINUOUS);
        }
    } else {
        hardware->setLED(DISCHARGING_LED_PIN, LED_MODE_OFF);
        // 放电停止，关闭蜂鸣器（仅关闭由本函数启动的 CONTINUOUS 模式）
        if (!globalState.power.ac_present) {
            hardware->setBuzzer(BUZZER_MODE_OFF);
        }
    }
}

// =============================================================================
// 配置变更事件回调实现
// =============================================================================

/**
 * @brief 系统配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (Configuration*)
 */
void SystemManagement::onConfigSystemChanged(EventType type, void* param) {
    
    if (param == nullptr) {
        Serial.println(F("[SysMgr] ERROR: Config param is null"));
        return;
    }
    
    Configuration* config = static_cast<Configuration*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->hardware == nullptr) {
        Serial.println(F("[SysMgr] ERROR: System manager or hardware is null"));
        return;
    }
    
    // 提取配置参数
    bool buzzer_enabled = config->buzzer_enabled;
    uint8_t buzzer_volume = config->buzzer_volume;
    uint8_t led_brightness = config->led_brightness;
    
    // 应用新配置到硬件接口
    systemManager->hardware->setBuzzerEnabled(buzzer_enabled);
    systemManager->hardware->setBuzzerVolume(buzzer_volume);
    systemManager->hardware->setLEDBrightness(led_brightness);
    
    Serial.println(F("[SysMgr] System config applied successfully"));
}

/**
 * @brief BMS 配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (BMS_Config_t*)
 */
void SystemManagement::onConfigBmsChanged(EventType type, void* param) {

    if (param == nullptr) {
        Serial.println(F("[SysMgr] ERROR: BMS config param is null"));
        return;
    }
    
    BMS_Config_t* config = static_cast<BMS_Config_t*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->bms == nullptr) {
        Serial.println(F("[SysMgr] ERROR: System manager or BMS is null"));
        return;
    }
    
    // 调用 BMS 的 applyNewConfig 方法
    if (!systemManager->bms->applyNewConfig(*config)) {
        Serial.println(F("[SysMgr] ERROR: Failed to apply BMS config"));
        return;
    }
    
    Serial.println(F("[SysMgr] BMS config applied successfully"));
}

/**
 * @brief 电源配置变更事件回调
 * @param type 事件类型
 * @param param 事件参数 (Power_Config_t*)
 */
void SystemManagement::onConfigPowerChanged(EventType type, void* param) {
    if (param == nullptr) {
        Serial.println(F("[SysMgr] ERROR: Power config param is null"));
        return;
    }
    
    Power_Config_t* config = static_cast<Power_Config_t*>(param);
    
    // 空指针检查
    if (systemManager == nullptr || systemManager->powerManagement == nullptr) {
        Serial.println(F("[SysMgr] ERROR: System manager or PowerManagement is null"));
        return;
    }
    
    // 调用 PowerManagement 的 applyNewConfig 方法
    if (!systemManager->powerManagement->applyNewConfig(*config)) {
        Serial.println(F("[SysMgr] ERROR: Failed to apply Power config"));
        return;
    }
    
    Serial.println(F("[SysMgr] Power config applied successfully"));
}

// =============================================================================
// 延迟启动相关方法实现
// =============================================================================

/**
 * @brief 检查并执行延迟启动
 */
void SystemManagement::checkAndExecuteDelayedStart() {
    if (delayedStartExecuted_) {
        return;  // 已执行，直接返回
    }
    
    // 首次调用时记录基准时间
    if (delayedStartTime_ == 0) {
        delayedStartTime_ = millis();
        return;
    }
    
    // 检查是否达到延迟时间
    unsigned long elapsed = millis() - delayedStartTime_;
    if (elapsed >= delayedStartDelay_) {
        onDelayedStart();  // 调用延迟启动回调
        delayedStartExecuted_ = true;
    }
}

/**
 * @brief 同步 overall_status 到全局状态
 * 根据当前 FSM 状态更新 globalState.overall_status
 */
void SystemManagement::syncOverallStatus() {
    uint8_t new_status;
    
    switch (currentState) {
        case SYS_STATE_NORMAL:
            new_status = OVERALL_STATUS_NORMAL;
            break;
        case SYS_STATE_WARNING:
            new_status = OVERALL_STATUS_WARNING;
            break;
        case SYS_STATE_CRITICAL:
            new_status = OVERALL_STATUS_FAULT;
            break;
        default:  // INIT
            new_status = OVERALL_STATUS_NORMAL;
            break;
    }
    
    if (globalState.overall_status != new_status) {
        globalState.overall_status = new_status;
    }
}

/**
 * @brief 更新电源模式
 * 根据 AC 状态、充电状态、混合供电状态决定 power_mode
 * 带防抖逻辑（2秒），避免 AC 插拔瞬间模式抖动
 */
void SystemManagement::updatePowerMode() {
    uint8_t new_mode;
    
    if (globalState.power.ac_present) {
        // AC 存在，判断具体模式
        if (globalState.power.charger_enabled && 
            globalState.bms.current > CHARGING_CURRENT_THRESHOLD) {
            new_mode = POWER_MODE_CHARGING;      // 3: 充电中
        } else if (globalState.power.hybrid_mode) {
            new_mode = POWER_MODE_HYBRID;        // 2: 混合供电
        } else {
            new_mode = POWER_MODE_AC;            // 0: AC 供电
        }
    } else {
        // AC 不存在，纯电池
        new_mode = POWER_MODE_BATTERY;           // 1: 电池供电
    }
    
    // 防抖：仅在模式变化且超过防抖时间才更新
    if (globalState.power_mode != new_mode) {
        unsigned long current_time = millis();
        if (current_time - lastPowerModeChangeTime >= POWER_MODE_DEBOUNCE_TIME) {
            globalState.power_mode = new_mode;
            lastPowerModeChangeTime = current_time;
        }
    }
}

/**
 * @brief BMS 故障检测事件回调（静态方法，供 EventBus 调用）
 * @param type 事件类型
 * @param param 事件参数 (int* 故障码)
 */
void SystemManagement::onBmsFaultDetected(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        BMS_Fault_t fault_type;
        
        if (param != nullptr) {
            fault_type = static_cast<BMS_Fault_t>(*static_cast<int*>(param));
        } else {
            // 如果没有参数，从全局状态获取当前故障类型
            fault_type = systemManager->globalState.bms.fault_type;
        }
        systemManager->handleBmsFault(fault_type);
    }
}

/**
 * @brief BMS 故障处理（成员函数）
 * 根据 BMS 故障类型执行相应的保护措施
 * @param fault_type BMS 故障类型
 * 
 * 故障处理策略：
 * - BMS_FAULT_OVER_TEMP:     过温保护 - 关闭充放电
 * - BMS_FAULT_OVER_VOLTAGE:  过压保护 - 关闭充电
 * - BMS_FAULT_UNDER_VOLTAGE: 欠压保护 - 关闭放电
 * - BMS_FAULT_OVER_CURRENT:  过流保护 - 关闭充放电
 * - BMS_FAULT_SHORT_CIRCUIT: 短路保护 - 紧急关闭充放电
 * - BMS_FAULT_CHIP_ERROR:    芯片错误 - 不处理，因为已经无法操作了
 * - BMS_FAULT_PASSIVE_SHUTDOWN: 被动关机 - 关闭充放电
 */
void SystemManagement::handleBmsFault(BMS_Fault_t fault_type) {
    if (!bms || !powerManagement) {
        Serial.println(F("[SysMgr] Cannot handle BMS fault: BMS or PowerManagement is null"));
        return;
    }
    
    switch (fault_type) {
        case BMS_FAULT_OVER_TEMP:
            Serial.println(F("[SysMgr] OVER TEMP protection: Disabling both charge and discharge"));
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_OVER_VOLTAGE:
            Serial.println(F("[SysMgr] OVER VOLTAGE protection: Disabling charge"));
            bms->disableCharge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_UNDER_VOLTAGE:
            Serial.println(F("[SysMgr] UNDER VOLTAGE protection: Disabling discharge"));
            bms->disableDischarge();
            break;
            
        case BMS_FAULT_OVER_CURRENT:
            Serial.println(F("[SysMgr] OVER CURRENT protection: Disabling both charge and discharge"));
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_SHORT_CIRCUIT:
            Serial.println(F("[SysMgr] SHORT CIRCUIT protection: Emergency shutdown!"));
            bms->emergencyShutdown();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_CHIP_ERROR:
            Serial.println(F("[SysMgr] CHIP ERROR protection: Disabling both charge and discharge"));
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_PASSIVE_SHUTDOWN:
            Serial.println(F("[SysMgr] PASSIVE SHUTDOWN protection: Disabling both charge and discharge"));
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
            
        case BMS_FAULT_NONE:
            // 故障清除，不执行任何操作
            // 故障恢复逻辑由 FSM 的状态检查函数处理
            Serial.println(F("[SysMgr] BMS fault cleared"));
            break;
            
        default:
            Serial.printf_P(PSTR("[SysMgr] Unknown BMS fault type: %d, taking safe action\n"), 
                           static_cast<int>(fault_type));
            bms->disableCharge();
            bms->disableDischarge();
            powerManagement->stopCharging();
            break;
    }
}

/**
 * @brief 延迟启动回调函数
 * 在系统运行指定时间后自动调用一次
 * 用户可以在此函数中实现自定义的延迟初始化逻辑
 */
void SystemManagement::onDelayedStart() {
    if (upsHidService != nullptr) {
        //延迟执行 ups 启动，保证数据获取正常
        upsHidService->begin();
    }

    if (mqttService != nullptr && systemConfig != nullptr) {
        // 检查 MQTT 配置是否有效（broker 和端口）
        if (strlen(systemConfig->mqtt_broker) > 0 && systemConfig->mqtt_port > 0) {
            Serial.println("[SysMgr] Starting MQTT service...");
            mqttService->setBrokerAddress(systemConfig->mqtt_broker);
            mqttService->setBrokerPort(systemConfig->mqtt_port);
            if (strlen(systemConfig->mqtt_username) > 0) {
                mqttService->setBrokerCredentials(
                    systemConfig->mqtt_username,
                    systemConfig->mqtt_password);
            }
            mqttService->setDeviceIdentifier(systemConfig->identifier);
            bool begin_ok = mqttService->begin(systemConfig, &globalState);
            bool connect_ok = mqttService->connect();
            if (!connect_ok) {
                Serial.println("[SysMgr] WARNING: MQTT connect failed, will retry in loop()");
            }
        } else {
            Serial.println("[SysMgr] MQTT not configured, skipping");
        }
    } else {
        Serial.println("[SysMgr] MQTT service not created (config not enabled)");
    }
}

// =============================================================================
// 寄存器一致性检查函数实现
// =============================================================================

/**
 * @brief 检查 BQ24780S 寄存器值与配置是否一致
 * 连续 3 次不匹配才触发警告
 * 调用处负责控制检查频率
 */
void SystemManagement::checkBQ24780sRegisters() {
    // 需要 PowerManagement 和配置管理器
    if (!powerManagement || !configManager) {
        return;
    }

    // 需要芯片连接
    if (!globalState.power.bq24780s_connected) {
        regMismatchCountBQ24780s = 0;
        bq24780sRegWarning = false;
        return;
    }

    Power_Config_t* config = configManager->getPowerConfig();
    bool mismatch = false;

    // BQ24780S 寄存器索引: [9]=DISCHARGE_CURRENT [10]=INPUT_CURRENT [5]=PROCHOT_OPTION1
    uint16_t* regs = globalState.power.bq24780s_registers;

    // 检查 DISCHARGE_CURRENT vs max_discharge_current
    int reg_discharge = Utils::parseBQ24780sDischargeCurrent(regs[9]);
    Serial.printf_P(PSTR("[RegCheck] BQ24780S DISCHARGE: raw=0x%04X, parsed=%d mA, config=%d mA\n"),
        regs[9], reg_discharge, config->max_discharge_current);
    if (reg_discharge > 0) {
        float tolerance = config->max_discharge_current * 0.05f;
        if (abs(reg_discharge - config->max_discharge_current) > tolerance) {
            mismatch = true;
        }
    }

    // 检查 INPUT_CURRENT vs over_current_threshold
    int reg_input = Utils::parseBQ24780sInputCurrent(regs[10]);
    Serial.printf_P(PSTR("[RegCheck] BQ24780S INPUT: raw=0x%04X, parsed=%d mA, config=%d mA\n"),
        regs[10], reg_input, config->over_current_threshold);
    if (reg_input > 0) {
        float tolerance = config->over_current_threshold * 0.05f;
        if (abs(reg_input - config->over_current_threshold) > tolerance) {
            mismatch = true;
        }
    }

    if (mismatch) {
        regMismatchCountBQ24780s++;
        if (regMismatchCountBQ24780s >= 3) {
            if (!bq24780sRegWarning) {
                Serial.println(F("[SysMgr] WARNING: BQ24780S register mismatch detected (chip error)"));
                bq24780sRegWarning = true;
            }
        }
    } else {
        regMismatchCountBQ24780s = 0;
        bq24780sRegWarning = false;
    }
}

/**
 * @brief 检查 BQ76920 寄存器值与配置是否一致
 * 连续 3 次不匹配才触发警告
 * 调用处负责控制检查频率
 */
void SystemManagement::checkBQ76920Registers() {
    // 需要 BMS 和配置管理器
    if (!bms || !configManager) {
        return;
    }

    // 需要 BMS 连接
    if (!globalState.bms.is_connected) {
        regMismatchCountBQ76920 = 0;
        bq76920RegWarning = false;
        return;
    }

    BMS_Config_t* config = configManager->getBMSConfig();
    bool mismatch = false;

    // BQ76920 寄存器索引: [4]=PROTECT1 [5]=PROTECT2 [7]=OV_TRIP [8]=UV_TRIP
    uint8_t* regs = globalState.bms.bq76920_registers;

    // 检查 PROTECT1 (SCD) vs short_circuit_threshold
    int reg_scd_ma = Utils::parseBQ76920Protect1(regs[4]);
    Serial.printf_P(PSTR("[RegCheck] BQ76920 SCD: raw=0x%02X, parsed=%d mA, config=%d mA\n"),
        regs[4], reg_scd_ma, config->short_circuit_threshold);
    if (reg_scd_ma > 0 && config->short_circuit_threshold > 0) {
        float tolerance = config->short_circuit_threshold * 0.05f;
        if (abs(reg_scd_ma - config->short_circuit_threshold) > tolerance) {
            mismatch = true;
        }
    }

    // 检查 PROTECT2 (OCD) vs max_discharge_current
    int reg_ocd_ma = Utils::parseBQ76920Protect2(regs[5], regs[4]);
    Serial.printf_P(PSTR("[RegCheck] BQ76920 OCD: raw=0x%02X, parsed=%d mA, config=%d mA\n"),
        regs[5], reg_ocd_ma, config->max_discharge_current);
    if (reg_ocd_ma > 0 && config->max_discharge_current > 0) {
        float tolerance = config->max_discharge_current * 0.05f;
        if (abs(reg_ocd_ma - config->max_discharge_current) > tolerance) {
            mismatch = true;
        }
    }

    // 检查 OV_TRIP vs cell_ov_threshold
    uint8_t default_gain = 0x10;
    uint8_t default_offset = 0x00;
    float reg_ov = Utils::parseBQ76920OvTrip(regs[7], default_gain, default_offset);
    Serial.printf_P(PSTR("[RegCheck] BQ76920 OV: raw=0x%02X, parsed=%.1f mV, config=%d mV\n"),
        regs[7], reg_ov, config->cell_ov_threshold);
    if (reg_ov > 0 && config->cell_ov_threshold > 0) {
        float tolerance = config->cell_ov_threshold * 0.01f;
        if (abs(reg_ov - config->cell_ov_threshold) > tolerance) {
            mismatch = true;
        }
    }

    // 检查 UV_TRIP vs cell_uv_threshold
    float reg_uv = Utils::parseBQ76920UvTrip(regs[8], default_gain, default_offset);
    Serial.printf_P(PSTR("[RegCheck] BQ76920 UV: raw=0x%02X, parsed=%.1f mV, config=%d mV\n"),
        regs[8], reg_uv, config->cell_uv_threshold);
    if (reg_uv > 0 && config->cell_uv_threshold > 0) {
        float tolerance = config->cell_uv_threshold * 0.01f;
        if (abs(reg_uv - config->cell_uv_threshold) > tolerance) {
            mismatch = true;
        }
    }

    if (mismatch) {
        regMismatchCountBQ76920++;
        if (regMismatchCountBQ76920 >= 3) {
            if (!bq76920RegWarning) {
                Serial.println(F("[SysMgr] WARNING: BQ76920 register mismatch detected (chip error)"));
                bq76920RegWarning = true;
            }
        }
    } else {
        regMismatchCountBQ76920 = 0;
        bq76920RegWarning = false;
    }
}

// =============================================================================
// BMS 运输模式请求事件回调实现
// =============================================================================

/**
 * @brief BMS 运输模式请求事件回调（静态方法，供 EventBus 调用）
 * @param type 事件类型
 * @param param 事件参数 (nullptr)
 */
void SystemManagement::onBmsShipModeRequest(EventType type, void* param) {
    if (systemManager && systemManager->systemInitialized) {
        Serial.println(F("[SysMgr] BMS Ship Mode Request event received"));
        systemManager->handleBmsShipModeRequest();
    }
}

/**
 * @brief BMS 运输模式处理（成员函数）
 * 执行 BMS 的 enterShipMode 指令，并将 BMS 标记为离线
 */
void SystemManagement::handleBmsShipModeRequest() {
    if (!bms) {
        Serial.println(F("[SysMgr] ERROR: Cannot enter ship mode - BMS is null"));
        return;
    }
    
    Serial.println(F("[SysMgr] Executing BMS enterShipMode command..."));
    
    // 执行 BMS 的 enterShipMode 指令
    bool result = bms->enterShipMode();
    
    if (result) {
        Serial.println(F("[SysMgr] BMS entered ship mode successfully"));
        // 将 BMS 标记为离线
        globalState.bms.is_connected = false;
        Serial.println(F("[SysMgr] BMS marked as offline"));
    } else {
        Serial.println(F("[SysMgr] ERROR: Failed to enter BMS ship mode"));
    }
}

// =============================================================================
// ADC Calibration 访问函数实现
// =============================================================================

/**
 * @brief 获取 ADC 校准系数数组
 * @return 校准系数数组指针，如果 hardware 为 nullptr 则返回 nullptr
 */
const uint8_t* SystemManagement::getADCCalibration() {
    return hardware ? hardware->getADCCalibration() : nullptr;
}

/**
 * @brief 设置单个 ADC 通道的校准系数
 * @param pin ADC 引脚编号
 * @param coefficient 校准系数
 */
void SystemManagement::setADCCalibration(uint8_t pin, uint8_t coefficient) {
    if (hardware) {
        hardware->setADCCalibration(pin, coefficient);
    }
}
