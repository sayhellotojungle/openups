#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware_interface.h"
#include "bms.h"
#include "bq24780s.h"
#include "i2c_interface.h"
#include "data_structures.h"  // 引入全局状态结构体

// =============================================================================
// 电源管理状态定义
// =============================================================================

// 电源工作模式已移至 data_structures.h 统一定义：
//   POWER_MODE_AC        = 0  // AC 供电
//   POWER_MODE_BATTERY   = 1  // 电池供电
//   POWER_MODE_HYBRID    = 2  // 混合供电
//   POWER_MODE_CHARGING  = 3  // 充电中

// 充电管理状态机（防止震荡，支持自适应降流）
typedef enum {
    CHARGE_STATE_IDLE = 0,                  // 空闲状态，等待启动条件
    CHARGE_STATE_ACTIVE,                    // 充电激活中
    CHARGE_STATE_COOLDOWN,                  // 冷却期，防止频繁启停震荡
    CHARGE_STATE_SUSPENDED_LOW_INPUT,       // 因输入电压不足挂起，等待重试
    CHARGE_STATE_SUSPENDED_FAULT            // 因故障挂起
} Charge_Management_State_t;


// =============================================================================
// 电源管理配置和数据结构
// =============================================================================

// 时间窗口结构体
typedef struct {
    uint8_t day_mask;         // 星期掩码 (bit0=周日，bit1=周一，... bit6=周六)
    uint8_t start_hour;       // 开始小时 (0-23)
    uint8_t end_hour;         // 结束小时 (0-23)
} ChargingTimeWindow_t;

// 充电规则评估结果
typedef enum {
    CHARGE_ALLOW = 0,          // 允许充电
    CHARGE_DENY_TEMP,          // 温度不允许
    CHARGE_DENY_TIME,          // 时间窗口不允许
    CHARGE_FULL                  // 电池已满
} ChargeRuleResult_t;

typedef struct {
    // BQ24780S 硬件配置
    BQ24780SConst::IadpGain iadp_gain_setting;        ///< IADP 增益配置
    BQ24780SConst::IdchgGain idchg_gain_setting;      ///< IDCHG 增益配置
    bool enable_hybrid_boost;                    ///< 混合供电增强使能
    
    // 充电配置
    uint16_t max_charge_current;        // 最大充电电流 (mA)
    uint16_t charge_voltage_limit;      // 充电电压限制 (mV)
    float charge_soc_start;             // 充电启动 SOC (%)
    float charge_soc_stop;              // 充电停止 SOC (%)
    
    // 放电配置
    uint16_t max_discharge_current;     // 最大放电电流 (mA)
    float discharge_soc_stop;           // 放电停止 SOC (%)

    // 保护配置
    uint16_t over_current_threshold;    // 过流阈值 (mA)
    float over_temp_threshold;          // 过温阈值 (°C)

    uint32_t charge_timeout_ms;         // 充电超时时间 (ms)
    uint32_t ac_stable_delay_ms;        // AC 电源稳定延迟时间 (ms)
    
    // 温度相关充电配置
    float charge_temp_high_limit;       // 高温充电限制 (°C)
    float charge_temp_low_limit;        // 低温充电限制 (°C)
    
    // 时间窗口充电配置
    ChargingTimeWindow_t charging_windows[5];  // 最多支持 5 个时间窗口
    uint8_t charging_window_count;             // 实际使用的时间窗口数量
    
} Power_Config_t;

// =============================================================================
// 电源管理类定义 - 专注电源管理逻辑
// =============================================================================

class PowerManagement {
public:
    /**
     * @brief 构造函数
     * @param config 电源管理配置结构体
     * @param hardware 硬件接口引用
     * @param bms BMS 指针（允许为 nullptr）
     */
    PowerManagement(const Power_Config_t& config, HardwareInterface& hardware);
    
    /**
     * @brief 初始化电源管理
     * @return true 初始化成功, false 初始化失败
     */
    bool begin();
    /**
     * @brief 更新电源管理状态（主循环调用）- 填充全局状态
     * @param globalState 系统全局状态引用，用于填充 Power 数据
     */
    void update(System_Global_State& globalState);


    
    // =============================================================================
    // 电源控制接口
    // =============================================================================
    
    /**
     * @brief 设置充电电流并启动充电流程
     * @param current_mA 充电电流 (mA)
     * @return true 设置成功且 AC 存在，false 失败（AC 不存在或参数错误）
     * @note 此函数会自动：
     *       1. 检查 AC 电源是否存在
     *       2. 启用 BQ24780S 充电功能
     *       3. 设置充电电压为配置最大值
     *       4. 设置充电电流
     *       5. 更新状态为充电中
     *       6. 点亮充电指示灯
     */
    bool setChargingCurrent(uint16_t current_mA);
    
    /**
     * @brief 停止充电
     * @return true 操作成功，false 操作失败
     * @note 此函数会自动：
     *       1. 禁用 BQ24780S 充电功能
     *       2. 更新状态为非充电中
     *       3. 关闭充电指示灯
     */
    bool stopCharging();
    
    /**
     * @brief 设置放电电流限制
     * @param current_mA 放电电流限制 (mA)
     * @return true 设置成功, false 设置失败
     */
    bool setDischargeCurrentLimit(uint16_t current_mA);

    /**
     * @brief 获取默认电源管理配置
     * @return 默认电源管理配置
     */
    static Power_Config_t getDefaultConfig();
    
    /**
     * @brief 应用新的电源管理配置（热更新）
     * @param config 新的电源管理配置
     * @param globalState 系统全局状态引用（用于访问充电状态等）
     * @return true 更新成功，false 更新失败
     * @note 此函数在 EVT_CONFIG_POWER_CHANGED 事件触发时被 SystemManagement 调用
     */
    bool applyNewConfig(const Power_Config_t& config);
    
    /**
     * @brief 应用待更新的配置到硬件（在update循环中调用）
     * @param globalState 系统全局状态引用
     * @note 此函数在 update() 中检测到 config_update_pending_ 时自动调用
     */
    void applyPendingConfig(System_Global_State& globalState);

private:

    Power_Config_t config_;
    
    // 外部接口
    BQ24780S bq24780s_;
    HardwareInterface* hal_;
    
    // 内部状态
    bool initialized_;
    bool available_;                    // 模块可用性标志
    uint32_t last_fast_update_;         // 快速更新时间戳
    uint32_t last_medium_update_;       // 中速更新时间戳
    uint32_t last_slow_update_;         // 慢速更新时间戳
    uint32_t last_periodic_update_;     // 周期性更新时间戳
    
    // 事件驱动的状态标志（来自中断事件）
    bool ac_present_;                   // AC 电源存在标志（来自中断事件）
    bool prochot_latched_;              // PROCHOT 锁存标志（来自中断事件）
    bool tbstat_latched_;               // TBSTAT 锁存标志（来自中断事件）
    
    // 充电状态跟踪（临时变量，用于运行时控制）
    unsigned long charge_start_time_;   // 充电开始时间 (ms)
    uint16_t last_charge_current_mA_;   // 最后设置的充电电流值（用于喂狗）
    
    // 充电维持状态跟踪
    uint32_t zero_current_detect_count_;  // 零电流检测计数
    
    // PROCHOT 故障清除跟踪
    bool prochot_pending_clear_;        // PROCHOT 待清除标志
    uint32_t last_prochot_clear_time_;  // PROCHOT 清除时间戳（用于 60 秒超时）
    
    // AC 电源稳定延迟跟踪
    unsigned long ac_connect_time_;     // AC 连接时间戳（用于稳定延迟）
    
    // 系统启动延迟跟踪
    unsigned long startup_time_;        // 启动时间戳（>0 表示延迟中，0 表示已完成）
    static const unsigned long STARTUP_DELAY_MS = 10000;  // 10 秒启动延迟
    
    // 充电状态机相关（防止震荡，支持自适应降流）
    Charge_Management_State_t charge_mgmt_state_;  // 当前充电管理状态
    unsigned long last_stop_time_;                 // 上次停止充电的时间戳 (用于计算冷却)
    unsigned long suspend_retry_time_;             // 下次尝试恢复充电的时间戳
    uint16_t adaptive_charge_current_;             // 当前动态调整后的充电电流值
    uint32_t i2c_error_count_;                     // 连续 I2C 错误计数（用于故障保护）
    
    // 充电状态机常量
    static const unsigned long COOLDOWN_TIME_MS = 60000;           // 60 秒冷却时间
    static const unsigned long SUSPEND_RETRY_INTERVAL_MS = 30000;  // 30 秒重试间隔
    static const uint16_t MIN_CHARGE_CURRENT_MA = 20;             // 最小充电电流 20mA
    static const uint8_t MAX_I2C_ERRORS = 10;                      // 最大连续 I2C 错误次数
    
    // RTC 无效时的应急充电阈值
    static constexpr float RTC_INVALID_SOC_START = 30.0f;
    static constexpr float RTC_INVALID_SOC_STOP = 50.0f;
    
    // 异步配置更新标记
    bool config_update_pending_;
    Power_Config_t pending_config_;
    
    
    // 私有辅助函数
    // 各层级具体实现
    void updateCriticalParameters(System_Global_State& globalState);    // 快速更新具体实现
    void updateOperationalData(System_Global_State& globalState);       // 中速更新具体实现
    void updateStatisticalData(System_Global_State& globalState);       // 慢速更新具体实现
    void updateHistoricalData(System_Global_State& globalState);        // 周期性更新具体实现
    
    static void onAcokChangedStatic(EventType type, void* param);
    static void onProchotAlertStatic(EventType type, void* param);
    static void onTbstatChangedStatic(EventType type, void* param);

    // 事件处理方法 - 重构为仅更新状态标志
    void handleAcokStateChanged(bool acok_state);
    void handleProchotAlert(bool prochot_state);
    void handleTbstatChanged(bool tbstat_state);
    
    // 故障状态更新
    void updateFault(System_Global_State& globalState);
    
    // 时间窗口检查
    bool isWithinTimeWindow(uint8_t current_day, uint8_t current_hour);
    
    // 充电规则评估与执行（带状态机）
    void evaluateChargingRulesAndApply(System_Global_State& globalState);
    
    // 充电维持逻辑（自适应降流）
    void maintainCharging(System_Global_State& globalState);
    
    // 过温保护检查（板载温度 + 环境温度）
    void checkOverTemperatureProtection(System_Global_State& globalState);
    
    // RTC 时间有效性检查
    bool isTimeValid();

};

#endif // POWER_MANAGEMENT_H