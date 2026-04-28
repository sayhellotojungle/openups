/*
 * System Management Module for UPS Control System
 *
 * This module implements a Finite State Machine (FSM) as the central decision maker.
 * It owns the global state blackboard, coordinates data updates, makes decisions,
 * and issues control commands to lower-level modules.
 */

#ifndef SYSTEM_MANAGEMENT_H
#define SYSTEM_MANAGEMENT_H

#include <Arduino.h>
#include "data_structures.h"
#include "event_bus.h"

// Forward declarations
class HardwareInterface;
class BMS;
class PowerManagement;
class ConfigManager;
class UPS_HID_Service;
class MQTTService;

class SystemManagement {
public:
    // =============================================================================
    // 系统状态枚举 - FSM 状态定义
    // =============================================================================
    enum SystemState {
        SYS_STATE_INIT = 0,       // 初始化状态：系统启动，检查设备就绪
        SYS_STATE_NORMAL = 1,     // 正常状态：模块在线且无故障，呼吸绿灯
        SYS_STATE_WARNING = 2,    // 警告状态：模块在线但存在故障，呼吸橙灯
        SYS_STATE_CRITICAL = 3    // 危急状态：模块掉线/不存在，或致命故障，如短路过流，高温。呼吸红灯+蜂鸣器
    };
    
    // Constructor with dependency injection - 支持 nullptr 参数
    explicit SystemManagement(
        HardwareInterface& hw,
        BMS* battery,      // 改为指针，允许 nullptr
        PowerManagement* pm,  // 改为指针，允许 nullptr
        ConfigManager& cm,
        UPS_HID_Service* upsHid = nullptr,
        MQTTService* mqtt = nullptr
    );

    // Destructor
    ~SystemManagement();

    // Initialization
    bool initialize();
    
    // Main update loop - FSM core
    void update();
    
    // Status queries
    bool isSystemReady() const;
    SystemState getState() const;
    const char* getStateString() const;
    
    // Global state access - read-only for other modules
    const System_Global_State& getGlobalState() const { return globalState; }
    
    // Module access (minimal exposure)
    BMS* getBMS() { return bms; }
    PowerManagement* getPowerManagement() { return powerManagement; }
    
    // ADC Calibration access (forwarding to hardware)
    const uint8_t* getADCCalibration();
    void setADCCalibration(uint8_t pin, uint8_t coefficient);

private:
    // =============================================================================
    // 依赖注入
    // =============================================================================
    HardwareInterface* hardware;
    BMS* bms;
    PowerManagement* powerManagement;
    ConfigManager* configManager;
    UPS_HID_Service* upsHidService;         // UPS HID服务
    MQTTService* mqttService;
    
    // =============================================================================
    // 核心状态管理
    // =============================================================================
    System_Global_State globalState;      // 全局状态黑板
    SystemState currentState;             // 当前 FSM 状态
    SystemState previousState;            // 上一状态
    bool systemInitialized;
    bool isShutdownRequested;             // 关机请求标志
    uint8_t millisOverflowCount;         // millis() 溢出计数器
    
    // =============================================================================
    // 定时任务计时器
    // =============================================================================
    unsigned long lastStatusUpdateTime;
    unsigned long lastIndicatorUpdateTime;
    unsigned long lastStateChangeTime;
    
    unsigned long lastSlowDataUpdate;     // 慢速数据更新时间戳
    unsigned long lastMillisValue;        // 上一次 millis() 值，用于检测溢出
    unsigned long lastPowerModeChangeTime; // 上次电源模式切换时间（用于防抖）
    
    // =============================================================================
    // 寄存器一致性检查
    // =============================================================================
    unsigned long lastRegisterCheckTime;  // 上次寄存器检查时间
    uint8_t regMismatchCountBQ24780s;     // BQ24780S 寄存器不匹配连续计数
    uint8_t regMismatchCountBQ76920;      // BQ76920 寄存器不匹配连续计数
    bool bq24780sRegWarning;              // BQ24780S 寄存器警告标志
    bool bq76920RegWarning;               // BQ76920 寄存器警告标志
    
    // =============================================================================
    // CRITICAL → NORMAL 恢复防抖计数器
    // =============================================================================
    uint8_t criticalRecoveryCounter_;     // 连续正常检测计数器，用于 CRITICAL → NORMAL 恢复防抖

    // =============================================================================
    // 紧急关机放电控制
    // =============================================================================
    bool emergency_discharge_disabled_;   // 紧急关机已关闭放电标志（用于恢复判断）
    
    // =============================================================================
    // 延迟启动相关
    // =============================================================================
    unsigned long delayedStartTime_;      // 延迟启动的基准时间（首次调用 update 时记录）
    unsigned long delayedStartDelay_;     // 延迟时间（毫秒），默认值可配置
    bool delayedStartExecuted_;           // 标记延迟启动是否已执行
    
    // =============================================================================
    // FSM 配置常量
    // =============================================================================
    static constexpr float SOC_LOW_THRESHOLD = 20.0f;        // 低电量阈值
    static constexpr float SOC_CRITICAL_THRESHOLD = 10.0f;   // 危急电量阈值
    static constexpr uint16_t STATE_DEBOUNCE_TIME = 2000;    // 状态防抖 2s
    static constexpr uint8_t CRITICAL_RECOVERY_COUNT = 5;    // CRITICAL → NORMAL 恢复所需连续正常次数
    static constexpr int16_t DISCHARGE_CURRENT_THRESHOLD = -10; // 放电电流阈值 (mA)，负值表示放电
    static constexpr uint16_t POWER_MODE_DEBOUNCE_TIME = 2000; // 电源模式切换防抖 2s
    static constexpr int16_t CHARGING_CURRENT_THRESHOLD = 10;  // 充电电流阈值 (mA)，正值表示充电
    

    // =============================================================================
    // FSM 核心方法
    // =============================================================================
    
    /**
     * @brief 第一步：数据收集 - 调用底层模块更新全局状态
     * REFACTORED: 分层采集 (快速数据每循环，慢速数据 2 秒)
     */
    void collectData();
    
    /**
     * @brief 第二步：状态决策 - 根据全局状态和当前状态计算下一状态
     * @return 下一状态
     */
    SystemState decideNextState();
    
    /**
     * @brief 第三步：状态流转 - 执行状态切换（调用 onStateExit/onStateEnter）
     * @param nextState 下一状态
     */
    void performStateTransition(SystemState nextState);
    
    /**
     * @brief 第四步：动作执行 - 在特定状态下执行控制指令
     */
    void executeStateActions();
    
    
    // =============================================================================
    // 状态生命周期回调
    // =============================================================================
    void onStateEnter(SystemState newState);
    void onStateExit(SystemState oldState);
    void onStateLoop();  // 每个循环都执行的动作
    
    // =============================================================================
    // FSM 状态处理函数 - 每个状态独立的处理逻辑
    // =============================================================================
    
    /**
     * @brief 处理 INIT 状态：初始化检查
     * 若设备就绪，转入 NORMAL；若设备缺失，转入 CRITICAL
     */
    void handleStateInit();
    
    /**
     * @brief 处理 NORMAL 状态：正常运行
     * 检测是否满足进入 WARNING 或 CRITICAL 的条件
     * 若保持正常，调用 applyIndicatorNormal() 执行呼吸绿灯
     */
    void handleStateNormal();
    
    /**
     * @brief 处理 WARNING 状态：警告
     * 优先检测是否降级为 CRITICAL
     * 若故障恢复，转回 NORMAL
     * 若保持警告，调用 applyIndicatorWarning() 执行呼吸橙灯
     */
    void handleStateWarning();
    
    /**
     * @brief 处理 CRITICAL 状态：危急
     * 实现恢复防抖：只有当设备连续 N 次循环检测都正常且无故障时，才允许转回 NORMAL
     * 若保持危急，调用 applyIndicatorCritical() 执行呼吸红灯+蜂鸣器间歇响
     */
    void handleStateCritical();
    
    // =============================================================================
    // 条件检查函数 - 提取复杂的布尔逻辑
    // =============================================================================
    
    /**
     * @brief 检查是否满足 CRITICAL 条件（模块掉线/不存在）
     * @return true 如果存在 CRITICAL 条件
     */
    bool checkCriticalConditions() const;
    
    /**
     * @brief 检查是否满足 WARNING 条件（模块在线但存在故障）
     * @return true 如果存在 WARNING 条件
     */
    bool checkWarningConditions() const;
    
    // =============================================================================
    // 指示灯控制函数 - 纯动作执行，不含业务逻辑
    // =============================================================================
    
    /**
     * @brief 设置正常指示：绿呼吸，关蜂鸣
     */
    void applyIndicatorNormal();
    
    /**
     * @brief 设置警告指示：橙呼吸，关蜂鸣
     */
    void applyIndicatorWarning();
    
    /**
     * @brief 设置危急指示：红呼吸，开蜂鸣间歇
     */
    void applyIndicatorCritical();
    
    /**
     * @brief 紧急关机放电保护检查
     * AC 离线 + SOC <= discharge_soc_stop * 150% 时触发紧急关机
     * SOC <= discharge_soc_stop 时关闭 BMS 放电开关
     */
    void checkEmergencyShutdown();

    /**
     * @brief 更新放电指示灯
     * 独立于主状态颜色逻辑，但在 INIT 状态之外均可运行
     * 逻辑：若 globalState.bms.current < -10 (mA)，亮 DISCHARGE_LED_PIN；否则灭
     */
    void updateDischargeIndicator();
    
    // =============================================================================
    // 全局状态同步方法
    // =============================================================================
    
    /**
     * @brief 同步 overall_status 到全局状态
     * 根据当前 FSM 状态更新 globalState.overall_status
     */
    void syncOverallStatus();
    
    /**
     * @brief 更新电源模式
     * 根据 AC 状态、充电状态、混合供电状态决定 power_mode
     * 带防抖逻辑，避免 AC 插拔瞬间模式抖动
     */
    void updatePowerMode();
    


    // =============================================================================
    // 事件处理回调（静态方法供 EventBus 调用）
    // =============================================================================
    static void onBmsAlarmInterrupt(EventType type, void* param);
    static void onAcokChanged(EventType type, void* param);
    static void onProchotAlert(EventType type, void* param);
    
    // 配置变更事件回调
    static void onConfigSystemChanged(EventType type, void* param);
    static void onConfigBmsChanged(EventType type, void* param);
    static void onConfigPowerChanged(EventType type, void* param);
    
    // 按键长按事件回调 - 重置 WiFi 配置
    static void onBtnLongPress(EventType type, void* param);
    
    // BMS 故障事件回调
    static void onBmsFaultDetected(EventType type, void* param);
    
    // BMS 故障处理（成员函数）
    void handleBmsFault(BMS_Fault_t fault_type);
    
    // BMS 运输模式请求事件回调
    static void onBmsShipModeRequest(EventType type, void* param);
    
    // BMS 运输模式处理（成员函数）
    void handleBmsShipModeRequest();

    // =============================================================================
    // 辅助方法
    // =============================================================================
    void transitionToState(SystemState newState);
    
    /**
     * @brief 检查并执行延迟启动
     */
    void checkAndExecuteDelayedStart();
    
    /**
     * @brief 延迟启动回调函数
     * 在系统运行指定时间后自动调用一次
     * 用户可以在此函数中实现自定义的延迟初始化逻辑
     */
    void onDelayedStart();
    
    // =============================================================================
    // 寄存器一致性检查函数
    // =============================================================================
    
    /**
     * @brief 检查 BQ24780S 寄存器值与配置是否一致
     * 每 5 秒检查一次，连续 3 次不匹配才触发警告
     * 
     * 检查项：
     * - DISCHARGE_CURRENT (reg[9]) vs Power_Config.max_discharge_current
     * - INPUT_CURRENT (reg[10]) vs Power_Config.over_current_threshold
     * - IDCHG (PROCHOT_OPTION1 reg[5]) vs Power_Config.max_discharge_current
     */
    void checkBQ24780sRegisters();
    
    /**
     * @brief 检查 BQ76920 寄存器值与配置是否一致
     * 每 5 秒检查一次，连续 3 次不匹配才触发警告
     * 
     * 检查项：
     * - PROTECT1 (reg[4]) vs BMS_Config.short_circuit_threshold
     * - PROTECT2 (reg[5]) vs BMS_Config.max_discharge_current
     * - OV_TRIP (reg[7]) vs BMS_Config.cell_ov_threshold
     * - UV_TRIP (reg[8]) vs BMS_Config.cell_uv_threshold
     */
    void checkBQ76920Registers();
};

// Global instance declaration
extern SystemManagement* systemManager;

#endif // SYSTEM_MANAGEMENT_H