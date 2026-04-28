#include "power_management.h"
#include <Arduino.h>
#include <math.h>
#include "event_bus.h"
#include "time_utils.h"
#include <time.h>

extern PowerManagement* powerManagement; 
PowerManagement::PowerManagement(const Power_Config_t& config, HardwareInterface& hardware)
    : bq24780s_(&hardware.getI2CInterface())
    , hal_(&hardware)
    , initialized_(false)
    , available_(false)
    , last_fast_update_(0)
    , last_medium_update_(0)
    , last_slow_update_(0)
    , last_periodic_update_(0)
    , ac_present_(false)
    , prochot_latched_(false)
    , tbstat_latched_(false)
    , charge_start_time_(0)
    , last_charge_current_mA_(0)
    , zero_current_detect_count_(0)
    , prochot_pending_clear_(false)
    , last_prochot_clear_time_(0)
    , ac_connect_time_(0)  // 初始化为 0，表示 AC 未连接
    , startup_time_(10000)  // 初始化为 10000，表示需要启动延迟
    , charge_mgmt_state_(CHARGE_STATE_IDLE)
    , last_stop_time_(0)
    , suspend_retry_time_(0)
    , adaptive_charge_current_(0)
    , i2c_error_count_(0)
    , config_update_pending_(false) {

    // 初始化配置
    memset(&config_, 0, sizeof(Power_Config_t));
    
    // 保存配置
    config_ = config;
}

Power_Config_t PowerManagement::getDefaultConfig() {
    Power_Config_t config = {};
    
    // BQ24780S 硬件配置 - 默认增益设置
    config.iadp_gain_setting = BQ24780SConst::IadpGain::Gain20x;      // 20 倍增益
    config.idchg_gain_setting = BQ24780SConst::IdchgGain::Gain16x;    // 16 倍增益


    // 充电配置
    config.max_charge_current = 500; // 500mA
    config.charge_voltage_limit = 12600; // 12.6V (3 串三元锂)
    config.charge_soc_start = 40.0f; // 40% 开始充电
    config.charge_soc_stop = 80.0f; // 80% 停止充电

    // 放电配置
    config.max_discharge_current = 10000; // 10A
    config.discharge_soc_stop = 5.0f; // 5% 停止放电

    // 混合供电配置
    config.enable_hybrid_boost = true;

    // 保护配置
    config.over_current_threshold = 10000;
    config.over_temp_threshold = 75.0f;

    config.charge_timeout_ms = 8 * 3600 * 1000; // 充电超时 8 小时
    config.ac_stable_delay_ms = 60000; // AC 电源稳定延迟 60 秒（防止电源质量差时立即充电导致掉电）

    // 温度相关充电配置
    config.charge_temp_high_limit = 55.0f; // 高温充电限制 55°C
    config.charge_temp_low_limit = 5.0f; // 低温充电限制 5°C
    
    // 时间窗口充电配置 - 默认示例：工作日 10:00-18:00
    config.charging_window_count = 2;

    // 窗口 1: 周一至周五 10:00-18:00 (bit1-bit5 = 0b00111110)
    config.charging_windows[0].day_mask = 0b00111110;  
    config.charging_windows[0].start_hour = 10;
    config.charging_windows[0].end_hour = 18;

    // 窗口 2: 周六 12:00-16:00 (bit6 = 0b01000000)
    config.charging_windows[1].day_mask = 0b01000000;
    config.charging_windows[1].start_hour = 12;
    config.charging_windows[1].end_hour = 16;

    // 其余窗口清零
    for (int i = 2; i < 5; i++) {
        config.charging_windows[i].day_mask = 0;
        config.charging_windows[i].start_hour = 0;
        config.charging_windows[i].end_hour = 0;
    }
    
  return config;
}

bool PowerManagement::begin() {
    Serial.println(F("PowerManagement: Initializing..."));
    
    // 初始化 BQ24780S - 失败时不阻止系统运行
    if (!bq24780s_.begin()) {
        Serial.println(F("PowerManagement: WARNING - Failed to initialize BQ24780S"));
        available_ = false;
    } else {
        Serial.println(F("PowerManagement: BQ24780S initialized successfully"));
        available_ = true;
        
        // 应用电源配置中的 BQ24780S 硬件配置
        BQ24780S::HardwareConfig hw_config;
        hw_config.iadp_gain = config_.iadp_gain_setting;
        hw_config.idchg_gain = config_.idchg_gain_setting;
        hw_config.enable_idchg_monitor = true;   // 强制开启 IDCHG 输出
        hw_config.enable_pmon_monitor = true;    // 强制开启 PMON 输出
        hw_config.enable_discharge_regulation = true;  // 开启放电调节
        hw_config.enable_hybrid_boost = config_.enable_hybrid_boost;
        
        // 电流限制配置（从 config_ 映射）
        hw_config.input_current_limit = config_.over_current_threshold;     // 输入电流限制 (mA)
        hw_config.discharge_current_max = config_.max_discharge_current; // 最大放电电流 (mA)
        
        if (bq24780s_.applyHardwareConfig(hw_config)) {
            Serial.println(F("PowerManagement: BQ24780S hardware configuration applied"));
        } else {
            Serial.println(F("PowerManagement: WARNING - Failed to apply BQ24780S config"));
        }

        EventBus::getInstance().subscribe(EVT_HW_ACOK_CHANGED, &PowerManagement::onAcokChangedStatic);
        EventBus::getInstance().subscribe(EVT_HW_PROCHOT_ALERT, &PowerManagement::onProchotAlertStatic);
        EventBus::getInstance().subscribe(EVT_HW_TBSTAT_CHANGED, &PowerManagement::onTbstatChangedStatic);
    }
    
    initialized_ = true;
    
    // 记录启动时间，用于启动延迟
    startup_time_ = millis();
    
    Serial.println(F("PowerManagement: Initialization complete"));
    return true;  // 总是返回 true，即使芯片不可用
}

void PowerManagement::handleAcokStateChanged(bool acok_state) {
    // 更新内部状态标志（事件驱动的核心）
    ac_present_ = acok_state;

    if (acok_state)
    {
       hal_->setLED(POWER_LED_PIN, LED_MODE_ON);
       // 记录 AC 连接时间，用于稳定延迟
       ac_connect_time_ = millis();
    }
    else{
       hal_->setLED(POWER_LED_PIN, LED_MODE_OFF);
       // AC 断开，重置连接时间
       ac_connect_time_ = 0;
    }
    
}

void PowerManagement::handleProchotAlert(bool prochot_state) {
    // PROCHOT# 低电平有效（false = 有故障，true = 正常）
    // 当 prochot_state = false 时，表示有故障，需要查询芯片状态
    if (!prochot_state) {
        // PROCHOT# 被拉低，标记需要查询芯片状态
        prochot_pending_clear_ = true;
    }
    prochot_latched_ = prochot_state;
}

void PowerManagement::handleTbstatChanged(bool tbstat_state) {
    // 更新内部锁存标志（事件驱动的核心）
    tbstat_latched_ = tbstat_state;
}

void PowerManagement::update(System_Global_State& globalState) {
    if (!initialized_) {
        return;
    }
    
    // 处理待更新的配置（优先执行，确保后续操作使用新配置）
    if (config_update_pending_) {
        applyPendingConfig(globalState);
    }
    
    if (!available_) {
        // 标记基本状态
        globalState.power.ac_present = ac_present_;
        globalState.power.charger_enabled = false;
        globalState.power.bq24780s_connected = false;
        return;
    } else if (!globalState.power.bq24780s_connected) {

        globalState.power.bq24780s_connected = bq24780s_.isConnected();

    }
    
    unsigned long current_time = millis();

    if (last_fast_update_ == 0)
    {
        last_fast_update_ = last_medium_update_ = last_slow_update_ =last_periodic_update_ = current_time;
    }
    
    
    // 分层更新任务 - 直接填充全局状态数据
    if (current_time - last_fast_update_ >= 100) {
        updateCriticalParameters(globalState);
        globalState.power.last_update_time = millis();
        last_fast_update_ = current_time;
    }
    
    if (current_time - last_medium_update_ >= 1000) {
        updateOperationalData(globalState);
        last_medium_update_ = current_time;
    }
    
    if (current_time - last_slow_update_ >= 5000) {
        updateStatisticalData(globalState);
        last_slow_update_ = current_time;
    }
    
    if (current_time - last_periodic_update_ >= 60000) {
        updateHistoricalData(globalState);
        last_periodic_update_ = current_time;
    }
}

void PowerManagement::updateCriticalParameters(System_Global_State& globalState) {
    
    globalState.power.charger_enabled = (charge_start_time_ > 0);
    globalState.power.ac_present = ac_present_;
    globalState.power.prochot_status = prochot_latched_;
    globalState.power.tbstat_status = tbstat_latched_;
    
}

void PowerManagement::updateOperationalData(System_Global_State& globalState) {

    // 智能充电规则评估
    evaluateChargingRulesAndApply(globalState);
    // 检查 BQ24780S 芯片连接状态
    globalState.power.bq24780s_connected = bq24780s_.isConnected();
    
    // 【重构】直接读取三个关键模拟引脚电压并计算
   if (hal_ != nullptr) {
        // 1. 读取原始电压值 (mV)
       uint16_t iadp_voltage = hal_->readVoltage(BQ24780S_IADP_PIN);
       uint16_t idchg_voltage = hal_->readVoltage(BQ24780S_IDCHG_PIN);
       uint16_t pmon_voltage = hal_->readVoltage(BQ24780S_PMON_PIN);
        
        // 调用 bq24780s_ 内部函数进行计算（使用内部缓存的增益配置）
        globalState.power.input_current = bq24780s_.calculateIadpCurrent(iadp_voltage);
        globalState.power.battery_current = bq24780s_.calculateIdchgCurrent(idchg_voltage);
        globalState.power.output_power = bq24780s_.calculatePmonPower(pmon_voltage);
        
    }
    
    // 更新故障状态（PROCHOT 等）
    updateFault(globalState);
}


void PowerManagement::updateStatisticalData(System_Global_State& globalState) {
    // 更新一般统计数据（5s 级）
    if (hal_ != nullptr) {
        globalState.power.input_voltage = hal_->readVoltage(INPUT_VOLTAGE_PIN, 10.0f);
        
        globalState.power.battery_voltage = hal_->readVoltage(BATTERY_VOLTAGE_PIN, 10.0f);

        globalState.system.board_temperature = hal_->readBoardTemperature();

        globalState.system.environment_temperature = hal_->readEnvironmentTemperature();
    }
    
    // 过温保护检查（在温度数据读取后执行）
    checkOverTemperatureProtection(globalState);
    
    //充电维持逻辑（动态喂狗 + 终止条件检测）
    if (globalState.power.charger_enabled) {
        maintainCharging(globalState);
    }

    //同步所有的寄存器值
    bq24780s_.readAllRegisters(globalState.power.bq24780s_registers);
    
}

void PowerManagement::updateHistoricalData(System_Global_State& globalState) {
    // 更新长周期历史数据（1min 级）
    
}

bool PowerManagement::setChargingCurrent(uint16_t current_mA) {
    if (!initialized_ || !available_) {
        Serial.println("PowerManagement: Not initialized or available");
        return false;
    }
    
    // 检查 AC 电源是否存在
    if (!ac_present_) {
        Serial.println("PowerManagement: AC not present, charging aborted");
        return false;
    }
    
    // 限制电流范围
    if (current_mA > config_.max_charge_current || current_mA <= 0) {
        current_mA = config_.max_charge_current;
    }
    
    Serial.printf_P(PSTR("PowerManagement: Starting charging with %d mA\n"), current_mA);
    
    // 1. 启用 BQ24780S 充电功能
    bool result = bq24780s_.setCharging(true);
    if (!result) {
        Serial.println("PowerManagement: Failed to enable charging");
        return false;
    }
    
    // 2. 设置充电电压为配置最大值
    result = bq24780s_.setChargeVoltage(config_.charge_voltage_limit);
    if (!result) {
        Serial.println("PowerManagement: Failed to set charge voltage");
        bq24780s_.setCharging(false);
        return false;
    }
    
    // 3. 设置充电电流
    result = bq24780s_.setChargeCurrent(current_mA);
    if (!result) {
        Serial.println("PowerManagement: Failed to set charge current");
        bq24780s_.setCharging(false);
        return false;
    }
    
    // 4. 更新内部状态跟踪
    charge_start_time_ = millis();
    last_charge_current_mA_ = current_mA;
    adaptive_charge_current_ = current_mA;  // 初始化自适应电流
    
    // 5. 点亮充电指示灯
    if (hal_ != nullptr) {
        hal_->setLED(CHARGING_LED_PIN, LED_MODE_ON);
    }
    
    Serial.printf_P(PSTR("PowerManagement: Charging started - Voltage: %d mV, Current: %d mA\n"), 
                  config_.charge_voltage_limit, current_mA);
    
    // 发布充电启动事件
    EventBus::getInstance().publish(EVT_PM_CHARGE_STARTED, nullptr);
    
    return true;
}

bool PowerManagement::stopCharging() {
    if (!initialized_) {
        return false;
    }
    
    Serial.println("PowerManagement: Stopping charging");
    
    // 1. 禁用 BQ24780S 充电功能
    bool result = bq24780s_.setCharging(false);
    if (!result) {
        Serial.println("PowerManagement: Failed to disable charging");
        return false;
    }
    
    // 2. 设置充电电流为 0（确保完全关闭）
    bq24780s_.setChargeCurrent(0);
    
    // 3. 重置内部状态跟踪
    charge_start_time_ = 0;
    last_charge_current_mA_ = 0;
    adaptive_charge_current_ = 0;  // 重置自适应电流
    
    // 4. 关闭充电指示灯
    if (hal_ != nullptr) {
        hal_->setLED(CHARGING_LED_PIN, LED_MODE_OFF);
    }
    
    Serial.println("PowerManagement: Charging stopped");
    
    // 发布充电完成事件
    EventBus::getInstance().publish(EVT_PM_CHARGE_COMPLETE, nullptr);
    
    return true;
}

bool PowerManagement::setDischargeCurrentLimit(uint16_t current_mA) {
    if (!initialized_) {
        return false;
    }
    
    // 限制电流范围
    if (current_mA > config_.max_discharge_current) {
        current_mA = config_.max_discharge_current;
    }
    
    bool result = bq24780s_.setDischargeCurrentLimit(current_mA);
    if (result) {
        Serial.printf_P(PSTR("PowerManagement: Discharge current limit set to %d mA\n"), current_mA);
    }
    return result;
}


/**
 * @brief 应用新的电源管理配置（热更新）- 异步模式
 * @param config 新的电源管理配置
 * @param globalState 系统全局状态引用（用于访问充电状态等）
 * @return true 更新成功，false 更新失败
 * @note 此函数在 EVT_CONFIG_POWER_CHANGED 事件触发时被 SystemManagement 调用
 *       采用异步模式：仅保存配置并标记待更新，在下一个 update 循环中执行硬件操作
 */
bool PowerManagement::applyNewConfig(const Power_Config_t& config) {
    Serial.println(F("[PowerMgr] Config update received"));
    
    // 1. 验证新配置
    if (config.max_charge_current == 0 || config.max_discharge_current == 0) {
        Serial.println(F("[PowerMgr] Invalid config: zero current limits"));
        return false;
    }
    
    // 2. 保存待更新的配置并标记需要更新
    pending_config_ = config;
    config_update_pending_ = true;
    
    Serial.println(F("[PowerMgr] Configuration update marked, will apply in next update cycle"));
    return true;
}

/**
 * @brief 应用待更新的配置到硬件（在update循环中调用）
 * @param globalState 系统全局状态引用
 * @note 此函数在 update() 中检测到 config_update_pending_ 时自动调用
 */
void PowerManagement::applyPendingConfig(System_Global_State& globalState) {
    Serial.println(F("[PowerMgr] Applying pending configuration..."));
    
    // 1. 检查 BQ24780S 硬件配置是否发生变化
    bool hw_config_changed = 
        (config_.iadp_gain_setting != pending_config_.iadp_gain_setting) ||
        (config_.idchg_gain_setting != pending_config_.idchg_gain_setting) ||
        (config_.enable_hybrid_boost != pending_config_.enable_hybrid_boost);
    
    if (hw_config_changed && available_) {
        Serial.println(F("[PowerMgr] BQ24780S hardware config changed, applying..."));
        
        BQ24780S::HardwareConfig hw_config;
        hw_config.iadp_gain = pending_config_.iadp_gain_setting;
        hw_config.idchg_gain = pending_config_.idchg_gain_setting;
        hw_config.enable_idchg_monitor = true;
        hw_config.enable_pmon_monitor = true;
        hw_config.enable_discharge_regulation = true;
        hw_config.enable_hybrid_boost = pending_config_.enable_hybrid_boost;
        hw_config.input_current_limit = pending_config_.over_current_threshold;
        hw_config.discharge_current_max = pending_config_.max_discharge_current;
        
        if (bq24780s_.applyHardwareConfig(hw_config)) {
            Serial.println(F("[PowerMgr] BQ24780S hardware config updated successfully"));
        } else {
            Serial.println(F("[PowerMgr] WARNING - Failed to update BQ24780S hardware config, will retry"));
            // 注意：不清除标记，下次循环继续尝试
            return;
        }
    }
    
    // 2. 硬件配置更新成功（或无需更新），更新内部配置
    config_ = pending_config_;
    config_update_pending_ = false;
    
    Serial.println(F("[PowerMgr] Configuration applied successfully"));
    
    // 3. 如果当前处于充电激活状态，并且新的限制比当前低，立即应用
    if (globalState.power.charger_enabled && available_) {
        if (last_charge_current_mA_ > config_.max_charge_current) {
            last_charge_current_mA_ = config_.max_charge_current;
            adaptive_charge_current_ = config_.max_charge_current;
            
            Serial.printf_P(PSTR("[PowerMgr] Adjusting charge current from %d to %d mA\n"),
                           last_charge_current_mA_, config_.max_charge_current);
            
            setChargingCurrent(last_charge_current_mA_);
        }
    }
}


/**
 * @brief 智能充电规则评估并自动执行（带状态机）
 * @param globalState 系统全局状态引用
 * @note 状态机逻辑：IDLE → ACTIVE → COOLDOWN → IDLE（防止震荡）
 *       SUSPENDED_LOW_INPUT → IDLE（自动重试）
 */
void PowerManagement::evaluateChargingRulesAndApply(System_Global_State& globalState) {
    // 检查系统启动延迟（startup_time_ > 0 表示延迟中）
    if (startup_time_ > 0) {
        if (millis() - startup_time_ < STARTUP_DELAY_MS) {
            // 启动延迟期间，不执行充电规则评估
            return;
        }
        startup_time_ = 0;  // 延迟完成，置为 0
        Serial.println(F("[ChargeRule] System startup delay completed"));
    }
    
    // 仅在 AC 存在时评估充电规则
    if (!ac_present_) {
        // AC 断开时，如果正在充电，停止并进入冷却
        if (charge_mgmt_state_ == CHARGE_STATE_ACTIVE) {
            Serial.println(F("[ChargeRule] AC disconnected, stopping charging"));
            stopCharging();
            charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
            last_stop_time_ = millis();
        }
        return;
    }
    
    // 获取当前时间
    unsigned long current_time = millis();
    
    // 检查 AC 电源稳定延迟（防止电源质量差时立即充电导致掉电）
    // 注意：millis() 约 49.7 天溢出，使用安全的时间差计算
    if (config_.ac_stable_delay_ms > 0 && ac_connect_time_ > 0) {
        // 安全计算时间差：处理 millis() 溢出情况
        unsigned long ac_stable_elapsed;
        if (current_time >= ac_connect_time_) {
            ac_stable_elapsed = current_time - ac_connect_time_;
        } else {
            // millis() 溢出情况：计算溢出后的实际经过时间
            ac_stable_elapsed = (0xFFFFFFFFUL - ac_connect_time_) + current_time + 1;
        }
        
        // 如果还没达到稳定延迟时间，等待
        if (ac_stable_elapsed < config_.ac_stable_delay_ms) {
            return;
        }
    }
    
    // 检查 BMS 数据有效性
    if (!globalState.bms.is_connected) {
        Serial.println(F("[ChargeRule] BMS not connected, skipping charge evaluation"));
        return;
    }
    
    // 检查 SOC 有效性
    float current_soc = globalState.bms.soc;
    if (current_soc < 0 || current_soc > 100) {
        Serial.println(F("[ChargeRule] SOC invalid, skipping charge evaluation"));
        return;
    }
    
    // 检查温度有效性
    float battery_temp = globalState.bms.temperature;
    if (battery_temp < -50.0f || battery_temp > 100.0f) {
        Serial.println(F("[ChargeRule] Temperature invalid, skipping charge evaluation"));
        return;
    }
    
    // RTC 时间安全校验与应急充电逻辑
    bool rtc_valid = isTimeValid();
    bool time_window_ok = true;
    
    if (config_.charging_window_count > 0 && !rtc_valid) {
        // RTC 无效时的应急充电逻辑：30% - 50%
        if (current_soc < RTC_INVALID_SOC_START) {
            if (charge_mgmt_state_ == CHARGE_STATE_IDLE) {
                Serial.println(F("[ChargeRule] RTC invalid & SOC low, emergency charging started"));
                // 使用配置的最大充电电流的一半启动充电
                uint16_t emergency_current = config_.max_charge_current / 2;
                bool success = setChargingCurrent(emergency_current);
                if (success) {
                    charge_mgmt_state_ = CHARGE_STATE_ACTIVE;
                    adaptive_charge_current_ = emergency_current;
                }
            }
            return;
        } else if (current_soc >= RTC_INVALID_SOC_STOP) {
            if (charge_mgmt_state_ == CHARGE_STATE_ACTIVE) {
                Serial.println(F("[ChargeRule] RTC invalid & SOC reached 50%, emergency charging stopped"));
                stopCharging();
                charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
                last_stop_time_ = current_time;
            }
            return;
        } else {
            // 在 30%-50% 之间，维持当前状态
            return;
        }
    } else if (config_.charging_window_count > 0) {
        // RTC 有效时，正常检查时间窗口
        uint32_t timestamp = getTimestamp();
        time_t local_time = (time_t)timestamp;
        struct tm time_info;
        localtime_r(&local_time, &time_info);
        uint8_t current_day = time_info.tm_wday;
        uint8_t current_hour = time_info.tm_hour;
        time_window_ok = isWithinTimeWindow(current_day, current_hour);
        
        if (!time_window_ok) {
            if (charge_mgmt_state_ == CHARGE_STATE_ACTIVE) {
                stopCharging();
                charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
                last_stop_time_ = current_time;
            }
            return;
        }
    }
    
    // ==================== 状态机处理 ====================
    
    // 【状态 1】COOLDOWN：冷却期，防止频繁启停震荡
    if (charge_mgmt_state_ == CHARGE_STATE_COOLDOWN) {
        if (current_time - last_stop_time_ < COOLDOWN_TIME_MS) {
            // 冷却期未满，禁止启动
            return;
        }
        // 冷却期满，转为 IDLE
        Serial.println(F("[ChargeRule] Cooldown completed, transitioning to IDLE"));
        charge_mgmt_state_ = CHARGE_STATE_IDLE;
    }
    
    // 【状态 3】SUSPENDED_FAULT：因故障挂起，需要外部干预才能恢复
    if (charge_mgmt_state_ == CHARGE_STATE_SUSPENDED_FAULT) {
        // 故障状态不自动恢复，需要系统复位或外部干预
        return;
    }
    
    // ==================== 1. 强制停止区 ====================
    // SOC 超过停止阈值，无论当前状态如何，必须停止充电
    if (current_soc >= config_.charge_soc_stop) {
        if (charge_mgmt_state_ == CHARGE_STATE_ACTIVE) {
            Serial.printf_P(PSTR("[ChargeRule] SOC stop threshold reached: %.1f%% >= %.1f%%, stopping charging\n"),
                           current_soc, config_.charge_soc_stop);
            stopCharging();
            charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
            last_stop_time_ = current_time;
        }
        return;
    }
    
    // ==================== 2. 强制启动区 ====================
    // SOC 低于启动阈值且当前未充电时，检查条件并启动
    // 仅在 IDLE 状态下允许启动（ACTIVE 状态已在前面处理）
    if (current_soc <= config_.charge_soc_start && charge_mgmt_state_ == CHARGE_STATE_IDLE) {
        
        // 检查温度条件
        // 低温检查
        if (battery_temp < config_.charge_temp_low_limit) {
            return;
        }
        
        // 高温检查
        if (battery_temp > config_.charge_temp_high_limit) {
            return;
        }
        
        // 时间窗口检查
        uint32_t timestamp = getTimestamp();
        time_t local_time = (time_t)timestamp;
        
        struct tm time_info;
        localtime_r(&local_time, &time_info);
        uint8_t current_day = time_info.tm_wday;
        uint8_t current_hour = time_info.tm_hour;
        
        if (!isWithinTimeWindow(current_day, current_hour)) {
            return;
        }
        
        // 所有条件满足，启动充电
        Serial.printf_P(PSTR("[ChargeRule] All conditions met (SOC=%.1f%%, Temp=%.1f°C, Time=%d:%d), starting charging\n"),
                       current_soc, battery_temp, current_day, current_hour);
        
        // 使用自适应充电电流（如果已降流，则使用降流后的值）
        uint16_t charge_current = adaptive_charge_current_;
        if (charge_current == 0) {
            charge_current = config_.max_charge_current;
        }
        
        bool success = setChargingCurrent(charge_current);
        
        if (success) {
            Serial.printf_P(PSTR("[ChargeRule] Charging started with %d mA\n"), charge_current);
            charge_mgmt_state_ = CHARGE_STATE_ACTIVE;
            adaptive_charge_current_ = charge_current;
        } else {
            Serial.println(F("[ChargeRule] Failed to start charging"));
        }
        
        return;
    }
    
    // ==================== 3. 保持区（死区）====================
    // config_.charge_soc_start < current_soc < config_.charge_soc_stop
    // 不执行任何操作，维持当前状态，防止边界震荡
    // 无需额外代码，直接返回即可
}

/**
 * @brief 检查当前时间是否在配置的时间窗口内
 * @param current_day 当前星期几 (0=周日，1=周一，... 6=周六)
 * @param current_hour 当前小时 (0-23)
 * @return true 在时间窗口内，false 不在
 */
bool PowerManagement::isWithinTimeWindow(uint8_t current_day, uint8_t current_hour) {
    // 如果时间无效（RTC 未同步），默认允许充电
    if (current_day > 6 || current_hour > 23) {
        return true;
    }
    
    // 遍历所有配置的时间窗口
    for (uint8_t i = 0; i < config_.charging_window_count; i++) {
        const ChargingTimeWindow_t& window = config_.charging_windows[i];
        
        // 检查这一天是否在这个窗口的掩码中
        if (window.day_mask & (1 << current_day)) {
            // 检查小时是否在范围内
            if (current_hour >= window.start_hour && current_hour < window.end_hour) {
                return true;
            }
        }
    }
    
    // 没有任何窗口匹配
    return false;
}

void PowerManagement::onAcokChangedStatic(EventType type, void* param) {
    // 安全检查：确保全局指针有效且 param 有效
    if (powerManagement && param) {
        bool state = *static_cast<bool*>(param);
        // 调用成员函数处理逻辑
        powerManagement->handleAcokStateChanged(state);
    }
}

void PowerManagement::onProchotAlertStatic(EventType type, void* param) {
    if (powerManagement && param) {
        bool state = *static_cast<bool*>(param);
        powerManagement->handleProchotAlert(state);
    }
}

void PowerManagement::onTbstatChangedStatic(EventType type, void* param) {
    if (powerManagement && param) {
        bool state = *static_cast<bool*>(param);
        powerManagement->handleTbstatChanged(state);
    }
}

void PowerManagement::updateFault(System_Global_State& globalState) {
    // ==================== PROCHOT 故障处理 ====================
    // 只有当 prochot_pending_clear_ 为 true 时才查询芯片状态
    // 这表示 PROCHOT# 引脚被拉低（有故障），避免浪费带宽
    if (prochot_pending_clear_) {
        // 使用 BQ24780S 类暴露的 readProchotStatus 函数读取状态
        uint16_t status_reg = 0;
        bool read_success = bq24780s_.readProchotStatus(&status_reg);
        globalState.power.bq24780s_status = status_reg & 0xFF;  // 保存低 8 位
        
        // 检查芯片通信是否成功
        if (!read_success) {
            // 通信失败仅标记异常，不触发系统关机
            globalState.power.fault_type = POWER_FAULT_I2C_COMMUNICATION;
            return;
        }
        
        // 通信恢复，清除 I2C 故障
        if (globalState.power.fault_type == POWER_FAULT_I2C_COMMUNICATION) {
            globalState.power.fault_type = POWER_FAULT_NONE;
            available_ = true;
        }
        
        // 读取 PROCHOT_STATUS 寄存器获取详细故障源
        // 注意：芯片的 PROCHOT_STATUS 寄存器在读取后会自动清零
        uint8_t prochot_bits = status_reg & 0xFF;
        
        // 解析具体故障类型（根据 BQ24780S 数据手册）
        // Bit 0: ACOK 事件触发
        // Bit 1: BATPRES 事件触发
        // Bit 2: VSYS 欠压事件触发
        // Bit 3: IDCHG 过流事件触发
        // Bit 4: INOM 过流事件触发
        // Bit 5: ICRIT 严重过流事件触发
        // Bit 6: 独立比较器事件触发
        
        String fault_details = F("[PROCHOT] Fault detected: ");
        bool has_real_fault = false;
        
        if (prochot_bits & 0x01) {  // Bit 0: ACOK
            has_real_fault = true;
        }
        if (prochot_bits & 0x02) {  // Bit 1: BATPRES
            has_real_fault = true;
        }
        if (prochot_bits & 0x04) {  // Bit 2: VSYS
            has_real_fault = true;
        }
        if (prochot_bits & 0x08) {  // Bit 3: IDCHG
            has_real_fault = true;
        }
        if (prochot_bits & 0x10) {  // Bit 4: INOM
            has_real_fault = true;
        }
        if (prochot_bits & 0x20) {  // Bit 5: ICRIT
            has_real_fault = true;
        }
        if (prochot_bits & 0x40) {  // Bit 6: Comparator
            has_real_fault = true;
        }
        
        if (has_real_fault) {
            // 存在真实故障，根据具体故障位设置对应的故障类型
            if (prochot_bits & 0x20) {  // Bit 5: ICRIT 严重过流
                globalState.power.fault_type = POWER_FAULT_SHORT_CIRCUIT;
            } else if (prochot_bits & 0x08) {  // Bit 3: IDCHG 过流
                globalState.power.fault_type = POWER_FAULT_OVER_CURRENT;
            } else if (prochot_bits & 0x10) {  // Bit 4: INOM 过流
                globalState.power.fault_type = POWER_FAULT_OVER_CURRENT;
            } else if (prochot_bits & 0x04) {  // Bit 2: VSYS 欠压
                globalState.power.fault_type = POWER_FAULT_INPUT_UNDERVOLTAGE;
            } else {
                // 其他故障（ACOK/BATPRES/Comparator）使用通用芯片错误
                globalState.power.fault_type = POWER_FAULT_CHIP_ERROR;
            }
            Serial.println(F("[PROCHOT] Status register read (auto-cleared by chip). Fault recorded, will auto-clear after 60s."));
            
            // 记录清除时间戳，用于 60 秒后自动清除
            last_prochot_clear_time_ = millis();
        } else {
            // 没有真实故障位，仅为瞬态干扰
            Serial.println(F("[PROCHOT] No real fault bits set, likely transient noise."));
            // 清除 pending 标志，不记录故障
            prochot_pending_clear_ = false;
        }
    }
    
    // ==================== 60 秒自动清除 ====================
    // 检查 60 秒超时，自动清除 globalState 中的故障
    if (prochot_pending_clear_ && (millis() - last_prochot_clear_time_ >= 60000UL)) {
        Serial.println(F("[PROCHOT] 60s timeout reached, clearing fault from globalState."));
        prochot_pending_clear_ = false;
        if (globalState.power.fault_type == POWER_FAULT_CHIP_ERROR) {
            globalState.power.fault_type = POWER_FAULT_NONE;
        }
    }
}

/**
 * @brief 充电维持逻辑 - 自适应降流 + 动态喂狗 + 终止条件检测
 * @param globalState 系统全局状态引用
 * @note 每 5 秒调用一次（在 updateStatisticalData 中）
 *       实现"能充则充，不能充则降流，实在不行再挂起"的策略
 */
void PowerManagement::maintainCharging(System_Global_State& globalState) {
    // 仅在充电激活时执行
    if (charge_mgmt_state_ != CHARGE_STATE_ACTIVE || !ac_present_) {
        // 重置维持状态
        zero_current_detect_count_ = 0;
        return;
    }
    
    unsigned long current_time = millis();
    
    // 【终止条件 1】总时长限制检查
    if (charge_start_time_ > 0 && current_time - charge_start_time_ > config_.charge_timeout_ms) {
        Serial.println(F("[ChargeMaint] Timeout reached, stopping charging"));
        stopCharging();
        charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
        last_stop_time_ = current_time;
        return;
    }
    // 【终止条件 2】电压检查
    //  去掉了，芯片自动会处理相关的情况
    
    // 【终止条件 3】电流为零检测（无法充入）
    // 每 5 秒检查一次（与调用频率同步）
    if (last_charge_current_mA_ > 0) {
        // 读取 BMS 实际电流（单位：mA）
        int16_t actual_current_mA = globalState.bms.current;
        
        // 检测条件：设定电流 > 0 但实际电流很小
        if (actual_current_mA < MIN_CHARGE_CURRENT_MA) {
            zero_current_detect_count_++;
            
            // 连续检测到 3 次（15 秒）判定为无法充入
            if (zero_current_detect_count_ >= 3) {
                Serial.printf_P(PSTR("[ChargeMaint] Zero current detected %d times (actual=%dmA, set=%dmA), suspending charging\n"),
                               zero_current_detect_count_, actual_current_mA, last_charge_current_mA_);
                stopCharging();
                charge_mgmt_state_ = CHARGE_STATE_SUSPENDED_LOW_INPUT;
                suspend_retry_time_ = current_time + SUSPEND_RETRY_INTERVAL_MS;
                zero_current_detect_count_ = 0;
                return;
            }
        } else {
            // 电流恢复正常，重置计数
            zero_current_detect_count_ = 0;
        }
    }
    
    // 【喂狗安全增强】重新写入最后一次设定的电流值到 BQ24780S
    if (last_charge_current_mA_ > 0 && available_) {
        // 【安全校验】喂狗前必须校验电流不超过最大限制
        if (last_charge_current_mA_ > config_.max_charge_current) {
            Serial.printf_P(PSTR("[ChargeMaint] ERROR: Charge current %d mA exceeds max %d mA, stopping charging\n"),
                           last_charge_current_mA_, config_.max_charge_current);
            stopCharging();
            charge_mgmt_state_ = CHARGE_STATE_SUSPENDED_FAULT;
            return;
        }
        
        bool success = bq24780s_.setChargeCurrent(last_charge_current_mA_);
        if (success) {
            // 喂狗成功，重置 I2C 错误计数
            i2c_error_count_ = 0;
        } else {
            // 喂狗失败，累加错误计数
            i2c_error_count_++;
            
            // 连续 3 次喂狗失败，触发故障保护
            if (i2c_error_count_ >= MAX_I2C_ERRORS) {
                stopCharging();
                charge_mgmt_state_ = CHARGE_STATE_SUSPENDED_FAULT;
                return;
            }
        }
    } else {
        // 电流为 0 或芯片不可用，停止充电
        Serial.println(F("[ChargeMaint] Charge current is 0 or chip unavailable, stopping charging"));
        stopCharging();
        charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
        last_stop_time_ = current_time;
    }
}

/**
 * @brief 过温保护检查
 * @param globalState 系统全局状态引用
 * @note 每 5 秒调用一次（在 updateStatisticalData 中，温度数据读取后）
 *       检查板载温度和环境温度，只要有一个超过阈值即触发保护
 *       恢复条件：温度需低于阈值 10°C 以上才解除保护（迟滞防震荡）
 */
void PowerManagement::checkOverTemperatureProtection(System_Global_State& globalState) {
    float board_temp = globalState.system.board_temperature;
    float env_temp = globalState.system.environment_temperature;
    float temp_threshold = config_.over_temp_threshold;
    float temp_hysteresis = 10.0f;  // 10°C 迟滞，防止边界震荡
    
    // 温度有效性检查（排除传感器异常值）
    bool board_temp_valid = (board_temp >= -50.0f && board_temp <= 150.0f);
    bool env_temp_valid = (env_temp >= -50.0f && env_temp <= 150.0f);
    
    // 只要有一个温度超过阈值即触发保护
    bool over_temp = (board_temp_valid && board_temp > temp_threshold) || 
                     (env_temp_valid && env_temp > temp_threshold);
    
    // 恢复条件：两个温度都需要低于（阈值 - 10°C）才解除保护
    float recover_threshold = temp_threshold - temp_hysteresis;
    bool temp_recovered = true;
    if (board_temp_valid && board_temp > recover_threshold) {
        temp_recovered = false;
    }
    if (env_temp_valid && env_temp > recover_threshold) {
        temp_recovered = false;
    }
    
    if (over_temp) {
        // 过温保护触发
        if (globalState.power.fault_type != POWER_FAULT_OVER_TEMPERATURE) {
            // 首次触发时记录日志
            Serial.printf_P(PSTR("[OverTemp] Protection triggered! Board: %.1f°C, Env: %.1f°C, Threshold: %.1f°C\n"),
                           board_temp, env_temp, temp_threshold);
        }
        
        // 设置故障类型和过温保护标志
        globalState.power.fault_type = POWER_FAULT_OVER_TEMPERATURE;
        globalState.over_temp_protection = true;
        
        // 如果正在充电，立即停止充电
        if (charge_mgmt_state_ == CHARGE_STATE_ACTIVE) {
            Serial.println(F("[OverTemp] Stopping charging due to over-temperature"));
            stopCharging();
            charge_mgmt_state_ = CHARGE_STATE_SUSPENDED_FAULT;
        }
        //@todo限制功率等
        
    } else if (temp_recovered) {
        // 温度恢复到安全范围（低于阈值 10°C 以上）
        if (globalState.over_temp_protection) {
            Serial.printf_P(PSTR("[OverTemp] Temperature recovered. Board: %.1f°C, Env: %.1f°C, Recover threshold: %.1f°C\n"),
                           board_temp, env_temp, recover_threshold);
            globalState.over_temp_protection = false;
        }
        
        // 如果故障类型是过温且温度已恢复，清除故障
        if (globalState.power.fault_type == POWER_FAULT_OVER_TEMPERATURE) {
            globalState.power.fault_type = POWER_FAULT_NONE;
        }
        
        // 如果因过温挂起且温度已恢复，转为冷却状态（允许后续重试充电）
        if (charge_mgmt_state_ == CHARGE_STATE_SUSPENDED_FAULT && 
            globalState.power.fault_type == POWER_FAULT_NONE) {
            charge_mgmt_state_ = CHARGE_STATE_COOLDOWN;
            last_stop_time_ = millis();
            Serial.println(F("[OverTemp] Entering cooldown before retry after temperature recovered"));
        }
    }
    // 注意：温度在 recover_threshold 和 temp_threshold 之间时保持当前状态不变（迟滞区）
}

/**
 * @brief 检查 RTC 时间是否有效
 * @return true 时间有效（>= 2021 年），false 时间无效
 * @note 用于安全校验，防止 RTC 未同步时错误允许充电
 */
bool PowerManagement::isTimeValid() {
    uint32_t timestamp = getTimestamp();
    
    if (timestamp < 1773666766)
    {
        return false;
    }
    // 时间有效
    return true;
}