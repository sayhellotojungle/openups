#include "bms.h"
#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include "event_bus.h"
#include "time_utils.h"

// 定义并初始化静态指针
BMS* BMS::instancePtr = nullptr;

BMS::BMS(I2CInterface& i2c_interface, const BMS_Config_t& config)
    : bq76920_(i2c_interface), config_(config), initialized_(false), available_(true),
      discharge_enabled_(false), charge_enabled_(false), i2c_failure_count_(0), i2c_power_enabled_(false),
      last_fast_update_(0), last_medium_update_(0), last_slow_update_(0), last_cc_update_(0),
      last_periodic_update_(0), last_day_update_(0), current_remaining_capacity(0.0f),
      accumulated_charge_mAh(0.0f), accumulated_discharge_mAh(0.0f),
      cc_ready_pending_(false), soc_initialized_(false), last_stable_soc_(50.0f),
      last_soc_update_timestamp_(0), self_discharge_rate_per_day_(0.0005f),
      soc_stable_start_time_(0), soc_waiting_for_stable_(false),
      hardware_fault_wait_(BMS_FAULT_NONE),
      full_charge_calibrated_(false),
      empty_discharge_calibrated_(false),
      last_full_charge_time_(0),
      last_empty_discharge_time_(0),
      cc_accumulated_raw_mAh_(0.0f),
      config_update_pending_(false) {
    
    memset(&stats_, 0, sizeof(BMS_Statistics_t));
    memset(cell_balance_timer, 0, sizeof(cell_balance_timer));
    
    preferences_.begin(BMS_PREFS_NAMESPACE, false);
    Serial.println(F("BMS: Constructor done"));
}

BMS_Config_t BMS::getDefaultConfig(uint8_t cell_count) {
    return {
        .cell_count = cell_count,
        .nominal_capacity_mAh = 2000,
        .cell_ov_threshold = 4210,
        .cell_uv_threshold = 3000,
        .cell_ov_recover = 4180,
        .cell_uv_recover = 3050,
        .max_charge_current = 2000,
        .max_discharge_current = 12000,
        .short_circuit_threshold = 20000,
        .temp_overheat_threshold = 65.0f,
        .balancing_enabled = true,
        .balancing_voltage_diff = 50.0f
    };
}

bool BMS::begin() {
    Serial.println(F("BMS: Initializing..."));
    
    // [关键] 将当前实例保存到静态变量
    instancePtr = this;
    
    if (!validateConfig(config_)) {
        Serial.println(F("BMS: Invalid config"));
        return false;
    }
    
    i2cPowerOn();
    if (!bq76920_.isConnected()) {
        Serial.println(F("BMS: BQ76920 not connected"));
        available_ = false;
        initialized_ = true;
        return true;
    }
    
    BQ76920_InitConfig chip_config = generateChipConfig(config_);
    if (!bq76920_.initializeChip(chip_config)) {
        Serial.println(F("BMS: Chip config failed"));
        available_ = false;
        return true;
    }

    loadFromStorage();
    
    if (stats_.soh <= 0 || stats_.soh > 100) {
        stats_.soh = 100.0f;
        Serial.println(F("BMS: SOH reset to 100%"));
    }
    
    // 恢复之前保存的剩余容量，而不是重置为-1
    float q_max = getAvailableCapacity();
    if (current_remaining_capacity > 0 && current_remaining_capacity <= q_max) {
        Serial.printf_P(PSTR("BMS: Restored capacity: %.1f mAh (%.1f%%)\n"),
            current_remaining_capacity, (current_remaining_capacity / q_max) * 100.0f);
    } else {
        current_remaining_capacity = -1.0f;
        Serial.println(F("BMS: No valid stored capacity, will init from OCV"));
    }
    
    soc_initialized_ = false;
    
    // Initialize SOH learning context
    soh_learning_ctx_.is_learning = false;
    soh_learning_ctx_.soc_start = 0.0f;
    soh_learning_ctx_.ah_start = 0.0f;
    soh_learning_ctx_.learning_start_time = 0;
    cc_accumulated_raw_mAh_ = 0.0f;
    
    full_charge_calibrated_ = false;
    empty_discharge_calibrated_ = false;
    
    Serial.printf_P(PSTR("BMS: SOH=%.1f%%, waiting for OCV init\n"), stats_.soh);
    
 
    // [修改] 订阅事件回调
    // 使用 [] 空捕获以兼容函数指针类型
    EventBus::getInstance().subscribe(EVT_BMS_ALARM_INTERRUPT, 
        [](EventType type, void* param) {
            // 通过静态指针调用实例方法
            if (BMS::instancePtr) {
                BMS::instancePtr->handleBmsAlertInterrupt(0);
            }
        });
    initialized_ = true;
    available_ = true;
    
    bq76920_.setMOS(1, 1);
    discharge_enabled_ = true;
    charge_enabled_ = true;
    Serial.println(F("BMS: Init complete, charge/discharge enabled"));
    
    return true;
}

void BMS::update(System_Global_State& globalState) {
    if (!initialized_) return;
    
    unsigned long current_time = millis();
    BMS_State& bmsState = globalState.bms;

    // 处理待更新的配置（优先执行，确保后续操作使用新配置）
    if (config_update_pending_) {
        applyPendingConfig();
    }

    if (hardware_fault_wait_ != BMS_FAULT_NONE) {
        bmsState.fault_type = hardware_fault_wait_;
        bmsState.bms_mode = BMS_MODE_FAULT;
        stats_.last_fault = hardware_fault_wait_;
        stats_.last_fault_time = getTimestamp();
        // 在 update() 阶段安全地发布故障事件，避免在 ISR 或初始化阶段触发 WDT
        EventBus::getInstance().publishInt(EVT_BMS_FAULT_DETECTED, static_cast<int>(hardware_fault_wait_));
        hardware_fault_wait_ = BMS_FAULT_NONE;
    }

    //处理万一alert中断迟滞粘连
    if (current_time - last_cc_update_ > 1000)
    {
        handleBmsAlertInterrupt(2);
    }
    
    // 处理所有待处理的库仑计数据
    if (cc_ready_pending_) {
        last_cc_update_ = current_time;
        processCoulombCounterData();
    }

    if (last_fast_update_ == 0) {
        last_medium_update_ = last_slow_update_ = last_periodic_update_ = last_day_update_ = current_time;
    }
    
    if (current_time - last_fast_update_ >= 500) {
        bool read_success = updateBasicInfo(bmsState);
        
        if (read_success) {
            i2c_failure_count_ = 0;
            if (bmsState.fault_type == BMS_FAULT_CHIP_ERROR) {
                bmsState.fault_type = BMS_FAULT_NONE;
                bmsState.bms_mode = BMS_MODE_NORMAL;
            }
            bmsState.last_update_time = millis();
            checkCriticalFaults(bmsState);
            bmsState.is_connected = true;
        } else {
            handleCommunicationLoss(bmsState);
        }
        last_fast_update_ = current_time;
    }
    
    if (current_time - last_medium_update_ >= 1000) {
        if (i2c_failure_count_ < 10) {
            updateFaultLogic(bmsState);
            updateSOC(bmsState);
            
            // 满充/放空校准检测（每秒检查）
            detectFullChargeCalibration(bmsState);
            detectEmptyDischargeCalibration(bmsState);
        }
        last_medium_update_ = current_time;
    }
    
    if (current_time - last_slow_update_ >= 10000) {
        updateSOHLearning(bmsState);
        bmsState.soh = stats_.soh;
        bmsState.cycle_count = stats_.total_cycles;
        
        i2cPowerOn();
        bq76920_.readAllRegisters(bmsState.bq76920_registers);
        last_slow_update_ = current_time;
    }

    if (current_time - last_periodic_update_ >= 60000) {
        evaluateAndExecuteBalancing(bmsState);
        last_periodic_update_ = current_time;
    }

    if (current_time - last_day_update_ >= 86400000) {
        saveToStorage();
        last_day_update_ = current_time;
    }
    
    i2cPowerOff();
}

bool BMS::disableDischarge() {
    if (!initialized_ || !available_) return false;
    i2cPowerOn();
    bq76920_.setMOS(charge_enabled_ ? 1 : 0, 0);
    discharge_enabled_ = false;
    Serial.println(F("BMS: DFET disabled"));
    return true;
}

bool BMS::enableDischarge() {
    if (!initialized_ || !available_) return false;
    i2cPowerOn();
    bq76920_.setMOS(charge_enabled_ ? 1 : 0, 1);
    discharge_enabled_ = true;
    Serial.println(F("BMS: DFET enabled"));
    return true;
}

bool BMS::isDischargeEnabled() const {
    return discharge_enabled_;
}

bool BMS::disableCharge() {
    if (!initialized_ || !available_) return false;
    i2cPowerOn();
    bq76920_.setMOS(0, discharge_enabled_ ? 1 : 0);
    charge_enabled_ = false;
    Serial.println(F("BMS: CFET disabled"));
    return true;
}

bool BMS::enableCharge() {
    if (!initialized_ || !available_) return false;
    i2cPowerOn();
    bq76920_.setMOS(1, discharge_enabled_ ? 1 : 0);
    charge_enabled_ = true;
    Serial.println(F("BMS: CFET enabled"));
    return true;
}

bool BMS::isChargeEnabled() const {
    return charge_enabled_;
}

bool BMS::enterShipMode() {
    if (!initialized_ || !available_) return false;
    i2cPowerOn();
    bool success = bq76920_.enterShipMode();
    if (success) {
        discharge_enabled_ = charge_enabled_ = false;
        Serial.println(F("BMS: Ship mode entered"));
    }
    return success;
}

void BMS::processAlertStatus(uint8_t fault_reg) {
    // 解析故障寄存器，仅设置标志位，不执行耗时操作
    // 所有实际处理逻辑推迟到 update() 函数中执行，避免在 ISR 或初始化阶段触发 WDT
    uint8_t real_faults = fault_reg & (~STAT_CC_READY);
    
    if (real_faults == 0 && (fault_reg & STAT_CC_READY)) {
        // 仅设置标志位，不立即处理库仑计数据
        cc_ready_pending_ = true;
        return;
    }
    
    if (real_faults != 0) {
        cc_ready_pending_ = false;
        BMS_Fault_t chip_fault = translateChipFault(real_faults);
        if (chip_fault != BMS_FAULT_NONE) {
            // 仅设置标志位，不立即发布事件
            hardware_fault_wait_ = chip_fault;
        }
    }
}

void BMS::handleBmsAlertInterrupt(uint8_t call_tag) {
    i2cPowerOn();
    uint8_t fault_reg = bq76920_.getFaultStatus();
    processAlertStatus(fault_reg);
}

void BMS::handleCommunicationLoss(BMS_State& bmsState) {
    i2c_failure_count_++;
    if (i2c_failure_count_ >= 5) {
        bmsState.fault_type = BMS_FAULT_CHIP_ERROR;
        bmsState.bms_mode = BMS_MODE_FAULT;
        stats_.last_fault = BMS_FAULT_CHIP_ERROR;
        stats_.last_fault_time = getTimestamp();
        bmsState.voltage = 0;
        bmsState.current = 0;
        bmsState.temperature = -99.0f;
        memset(bmsState.cell_voltages, 0, sizeof(bmsState.cell_voltages));
        bmsState.is_connected = false;
    }
}

bool BMS::setBalancingEnabled(bool enable) {
    config_.balancing_enabled = enable;
    if (!enable) return stopBalancing();
    return true;
}

bool BMS::startBalancing(const BMS_State& bmsState) {
    if (!initialized_ || !config_.balancing_enabled) return false;
    
    bool valid_data = (bmsState.cell_voltage_max > 2500 && bmsState.cell_voltage_max < 4500 &&
                       bmsState.cell_voltage_min > 2500 && bmsState.cell_voltage_min < 4500);
    if (!valid_data) return false;

    uint8_t balance_mask = 0;
    const uint16_t* cell_voltages = bmsState.cell_voltages;
    uint16_t avg_voltage = bmsState.cell_voltage_avg;
    
    for (uint8_t i = 0; i < config_.cell_count; i++) {
        bool should_balance = (cell_voltages[i] > avg_voltage + config_.balancing_voltage_diff);
        if (should_balance && i > 0 && (balance_mask & (1 << (i - 1)))) {
            should_balance = false;
        }
        if (should_balance) balance_mask |= (1 << i);
    }

    if (balance_mask == 0) return false;

    i2cPowerOn();
            
    if (bq76920_.setCellBalance(balance_mask)) {
        unsigned long current_time = millis();
        bool should_count_total = false;
        
        for (uint8_t i = 0; i < config_.cell_count; i++) {
            if (balance_mask & (1 << i)) {
                if (current_time - cell_balance_timer[i] >= BALANCE_COUNT_INTERVAL) {
                    uint8_t cell_shift = i * 10;
                    uint8_t cell_count = (stats_.balancing_events >> cell_shift) & 0x3FF;
                    cell_count++;
                    
                    int64_t clear_mask = ~(((int64_t)0x3FF) << cell_shift);
                    stats_.balancing_events &= clear_mask;
                    stats_.balancing_events |= ((int64_t)cell_count << cell_shift);
                    cell_balance_timer[i] = current_time;
                    should_count_total = true;
                }
            }
        }
        
        if (should_count_total) {
            int64_t current_total = (stats_.balancing_events >> 50) & 0x3FFF;
            current_total++;
            int64_t clear_mask = ~(((int64_t)0x3FFF) << 50);
            stats_.balancing_events &= clear_mask;
            stats_.balancing_events |= (current_total << 50);
        }
        
        Serial.printf_P(PSTR("BMS: Balancing mask: 0x%02X\n"), balance_mask);
        EventBus::getInstance().publish(EVT_BMS_BALANCING_STARTED, &balance_mask);
        return true;
    }
    return false;
}

bool BMS::stopBalancing() {
    if (!initialized_) return false;
    i2cPowerOn();
    if (bq76920_.setCellBalance(0)) {
        EventBus::getInstance().publish(EVT_BMS_BALANCING_STOPPED, nullptr);
        return true;
    }
    return false;
}

void BMS::evaluateAndExecuteBalancing(BMS_State& bmsState) {
    if (!initialized_ || !config_.balancing_enabled) {
        if (initialized_) stopBalancing();
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
        return;
    }

    const int16_t BALANCING_MAX_CURRENT_MA = 50;
    bool is_current_suitable = (bmsState.current >= -5 && bmsState.current <= BALANCING_MAX_CURRENT_MA);
    if (!is_current_suitable) {
        stopBalancing();
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
        return;
    }

    bool valid_reading = false;
    for (uint8_t i = 0; i < config_.cell_count; i++) {
        if (bmsState.cell_voltages[i] > 2500 && bmsState.cell_voltages[i] < 4500) {
            valid_reading = true;
            break;
        }
    }
    if (!valid_reading) {
        stopBalancing();
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
        return;
    }

    uint16_t voltage_diff = bmsState.cell_voltage_max - bmsState.cell_voltage_min;
    if (voltage_diff < config_.balancing_voltage_diff) {
        stopBalancing();
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
        return;
    }

    // 根据压差动态计算最低启动SOC阈值
    // 压差越大，允许的最低SOC越低（安全优先）
    float min_balancing_soc;
    if (voltage_diff >= 200) {
        min_balancing_soc = 50.0f;
    } else if (voltage_diff >= 150) {
        min_balancing_soc = 60.0f;
    } else if (voltage_diff >= 100) {
        min_balancing_soc = 65.0f;
    } else if (voltage_diff >= 50) {
        min_balancing_soc = 70.0f;
    } else {
        min_balancing_soc = 75.0f;
    }

    if (bmsState.soc < min_balancing_soc) {
        stopBalancing();
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
        return;
    }

    // 启动均衡并更新状态
    if (startBalancing(bmsState)) {
        bmsState.balancing_active = true;
        // 计算当前均衡掩码
        uint8_t balance_mask = 0;
        const uint16_t* cell_voltages = bmsState.cell_voltages;
        uint16_t avg_voltage = bmsState.cell_voltage_avg;
        for (uint8_t i = 0; i < config_.cell_count; i++) {
            if (cell_voltages[i] > avg_voltage + config_.balancing_voltage_diff) {
                balance_mask |= (1 << i);
            }
        }
        bmsState.balance_mask = balance_mask;
    } else {
        bmsState.balancing_active = false;
        bmsState.balance_mask = 0;
    }
}

bool BMS::clearFault() {
    if (!initialized_) return false;
    i2cPowerOn();
    return bq76920_.clearFaults(0xFF);
}

bool BMS::emergencyShutdown() {
    if (!initialized_) return false;
    i2cPowerOn();
    bq76920_.setMOS(0, 0);
    Serial.println(F("BMS: Emergency shutdown"));
    return true;
}

float BMS::getAvailableCapacity() const {
    float q_nominal = (float)config_.nominal_capacity_mAh;
    
    // Apply SOH scaling
    float q_soh = q_nominal;
    if (stats_.soh > 0 && stats_.soh <= 100) {
        q_soh = q_nominal * (stats_.soh / 100.0f);
    }
    
    // Temperature compensation (assuming we have access to temperature)
    // Note: This function is const, so we can't access bmsState directly
    // Temperature compensation will be applied in the calling context
    return q_soh;
}

float BMS::getTemperatureCompensatedCapacity(float temperature) const {
    float q_base = getAvailableCapacity();
    float k_temp = 1.0f;
    
    // Temperature compensation model based on 25°C reference
    if (temperature < 0.0f) {
        // T < 0°C: K_temp = 0.6 ~ 0.8 (linear interpolation from -20°C to 0°C)
        k_temp = 0.6f + ((temperature + 20.0f) / 20.0f) * 0.2f;
        if (k_temp < 0.6f) k_temp = 0.6f;
        if (k_temp > 0.8f) k_temp = 0.8f;
    } else if (temperature <= 25.0f) {
        // 0°C ≤ T ≤ 25°C: K_temp from 0.8 linearly rising to 1.0
        k_temp = 0.8f + (temperature / 25.0f) * 0.2f;
    } else if (temperature <= 45.0f) {
        // 25°C < T ≤ 45°C: K_temp = 1.0
        k_temp = 1.0f;
    } else {
        // T > 45°C: K_temp gradually decreases (e.g., 0.9)
        k_temp = 1.0f - ((temperature - 45.0f) / 20.0f) * 0.1f;
        if (k_temp < 0.9f) k_temp = 0.9f;
    }
    
    return q_base * k_temp;
}

void BMS::compensateSelfDischarge(unsigned long delta_time_ms) {
    if (delta_time_ms == 0) return;
    
    float days = (float)delta_time_ms / (1000.0f * 3600.0f * 24.0f);
    float q_max = getAvailableCapacity();
    float loss_mah = q_max * self_discharge_rate_per_day_ * days;
    
    current_remaining_capacity -= loss_mah;
    if (current_remaining_capacity < 0) current_remaining_capacity = 0;
    
    if (loss_mah > 1.0f) {
        Serial.printf_P(PSTR("BMS: Self-discharge %.2fmAh over %.2f days\n"), loss_mah, days);
    }
}

float BMS::calculateSOC_Voltage(const BMS_State& bmsState) {
    if (!initialized_ || !bq76920_.isConnected()) return -1.0f;

    uint16_t ref_cell_mv;
    if (bmsState.current > 100) {
        ref_cell_mv = bmsState.cell_voltage_max;  // 充电时：最高电压
    } else if (bmsState.current < -100) {
        ref_cell_mv = bmsState.cell_voltage_min;  // 放电时：最低电压
    } else {
        ref_cell_mv = bmsState.cell_voltage_avg;  // 静置时：平均电压
    }

    if (ref_cell_mv < 2500 || ref_cell_mv > 4500) return -1.0f;

    if (ref_cell_mv <= OCV_SOC_TABLE[OCV_TABLE_SIZE - 1][0]) return 0.0f;
    if (ref_cell_mv >= OCV_SOC_TABLE[0][0]) return 100.0f;

    for (int i = 0; i < OCV_TABLE_SIZE - 1; i++) {
        uint16_t v1 = OCV_SOC_TABLE[i][0];
        uint16_t soc1 = OCV_SOC_TABLE[i][1];
        uint16_t v2 = OCV_SOC_TABLE[i+1][0];
        uint16_t soc2 = OCV_SOC_TABLE[i+1][1];

        if (ref_cell_mv >= v2 && ref_cell_mv <= v1) {
            float soc = soc2 + (ref_cell_mv - v2) * (soc1 - soc2) / (float)(v1 - v2);
            return constrain(soc, 0.0f, 100.0f);
        }
    }
    return -1.0f;
}

float BMS::calculateSOC_FromVoltage(uint16_t voltage_mv) {
    if (voltage_mv < 2500 || voltage_mv > 4500) return -1.0f;

    if (voltage_mv <= OCV_SOC_TABLE[OCV_TABLE_SIZE - 1][0]) return 0.0f;
    if (voltage_mv >= OCV_SOC_TABLE[0][0]) return 100.0f;

    for (int i = 0; i < OCV_TABLE_SIZE - 1; i++) {
        uint16_t v1 = OCV_SOC_TABLE[i][0];
        uint16_t soc1 = OCV_SOC_TABLE[i][1];
        uint16_t v2 = OCV_SOC_TABLE[i+1][0];
        uint16_t soc2 = OCV_SOC_TABLE[i+1][1];

        if (voltage_mv >= v2 && voltage_mv <= v1) {
            float soc = soc2 + (voltage_mv - v2) * (soc1 - soc2) / (float)(v1 - v2);
            return constrain(soc, 0.0f, 100.0f);
        }
    }
    return -1.0f;
}

void BMS::updateTemporarySOH(BMS_State& bmsState) {
    uint16_t voltage_diff = bmsState.cell_voltage_max - bmsState.cell_voltage_min;

    // 压差小于50mV时，不启用临时SOH
    if (voltage_diff < 50) {
        bmsState.temporary_soh = stats_.soh;
        return;
    }

    float soc_min = calculateSOC_FromVoltage(bmsState.cell_voltage_min);
    float soc_max = calculateSOC_FromVoltage(bmsState.cell_voltage_max);

    // 电压无效时，使用原始SOH
    if (soc_min < 0 || soc_max < 0) {
        bmsState.temporary_soh = stats_.soh;
        return;
    }

    float charge_space = 1.0f - (soc_max / 100.0f);
    float discharge_space = soc_min / 100.0f;
    float effective_capacity_ratio = charge_space + discharge_space;

    bmsState.temporary_soh = stats_.soh * effective_capacity_ratio;
}

float BMS::calculateSOC_Coulomb() {
    // Note: This function doesn't have access to temperature
    // Temperature compensation is applied in updateSOC where bmsState is available
    float q_max = getAvailableCapacity();
    if (q_max <= 0) q_max = (float)config_.nominal_capacity_mAh;
    return constrain((current_remaining_capacity / q_max) * 100.0f, 0.0f, 100.0f);
}

bool BMS::applyNewConfig(const BMS_Config_t& config) {
    if (!validateConfig(config)) return false;
    
    // 仅更新内部配置并标记需要更新硬件配置
    pending_config_ = config;
    config_update_pending_ = true;
    
    Serial.println(F("BMS: Config update marked, will apply in next update cycle"));
    return true;
}

void BMS::applyPendingConfig() {
    // 应用待更新的配置到硬件
    i2cPowerOn();
    
    // 如果芯片已连接，更新芯片配置
    if (bq76920_.isConnected()) {
        BQ76920_InitConfig chip_config = generateChipConfig(pending_config_);
        if (bq76920_.updateConfiguration(chip_config)) {
            // 硬件配置更新成功，更新内部配置并清除标记
            config_ = pending_config_;
            config_update_pending_ = false;
            Serial.println(F("BMS: Pending config applied to chip successfully"));
        } else {
            Serial.println(F("BMS: WARNING - Failed to update chip config, will retry"));
            // 注意：不清除标记，下次循环继续尝试
        }
    } else {
        Serial.println(F("BMS: WARNING - Chip not connected, config saved but not applied to chip"));
        // 芯片未连接时，仍然更新内部配置并清除标记（避免无限重试）
        config_ = pending_config_;
        config_update_pending_ = false;
    }
}

void BMS::updateSOC(BMS_State& bmsState) {
    unsigned long current_time = millis();
    unsigned long delta_ms = current_time - last_soc_update_timestamp_;

    if (last_soc_update_timestamp_ > 0) {
        compensateSelfDischarge(delta_ms);
    }
    last_soc_update_timestamp_ = current_time;

    // 温度补偿容量
    float q_max = getTemperatureCompensatedCapacity(bmsState.temperature);
    if (q_max <= 0) q_max = (float)config_.nominal_capacity_mAh;

    // 计算临时SOH并应用
    updateTemporarySOH(bmsState);
    if (stats_.soh > 0) {
        q_max = q_max * (bmsState.temporary_soh / stats_.soh);
    }

    float stable_current_threshold = config_.nominal_capacity_mAh / 20.0f;
    
    // ========== SOC 初始化 ==========
    if (!soc_initialized_) {
        Serial.println(F("BMS: SOC init starting..."));
        
        // 优先使用已恢复的存储容量
        if (current_remaining_capacity > 0 && current_remaining_capacity <= q_max) {
            last_stable_soc_ = (current_remaining_capacity / q_max) * 100.0f;
            soc_initialized_ = true;
            soc_waiting_for_stable_ = false;
            bmsState.soc = constrain(last_stable_soc_, 0.0f, 100.0f);
            Serial.printf_P(PSTR("BMS: SOC init from stored capacity: %.1f%%\n"), bmsState.soc);
            return;
        }
        
        // 电流过高时无法使用OCV，暂用50%
        if (abs(bmsState.current) > stable_current_threshold) {
            current_remaining_capacity = q_max * 0.5f;
            last_stable_soc_ = 50.0f;
            soc_initialized_ = true;
            soc_waiting_for_stable_ = true;
            soc_stable_start_time_ = current_time;
            bmsState.soc = 50.0f;
            Serial.println(F("BMS: SOC init 50% (high current, waiting for stable)"));
            return;
        }
        
        // 电流稳定，使用OCV初始化
        float soc_ocv = calculateSOC_Voltage(bmsState);
        if (soc_ocv > 0 && soc_ocv < 100) {
            current_remaining_capacity = q_max * (soc_ocv / 100.0f);
            last_stable_soc_ = soc_ocv;
            soc_waiting_for_stable_ = false;
            Serial.printf_P(PSTR("BMS: SOC init by OCV: %.1f%%\n"), soc_ocv);
        } else {
            // OCV不可用，默认50%
            current_remaining_capacity = q_max * 0.5f;
            last_stable_soc_ = 50.0f;
            soc_waiting_for_stable_ = false;
            Serial.println(F("BMS: SOC init default 50% (OCV unavailable)"));
        }
        
        soc_initialized_ = true;
        bmsState.soc = constrain(last_stable_soc_, 0.0f, 100.0f);
        Serial.printf_P(PSTR("BMS: SOC init done: %.1f%%\n"), bmsState.soc);
        return;
    }
    
    // ========== 等待电流稳定后用OCV重新校准 ==========
    if (soc_waiting_for_stable_) {
        if (abs(bmsState.current) <= stable_current_threshold) {
            if (current_time - soc_stable_start_time_ >= 60000) {
                float soc_ocv = calculateSOC_Voltage(bmsState);
                if (soc_ocv > 0 && soc_ocv < 100) {
                    current_remaining_capacity = q_max * (soc_ocv / 100.0f);
                    last_stable_soc_ = soc_ocv;
                    Serial.printf_P(PSTR("BMS: SOC recalibrated by OCV: %.1f%%\n"), soc_ocv);
                }
                soc_waiting_for_stable_ = false;
            }
        } else {
            soc_stable_start_time_ = current_time;
        }
    }
    
    // ========== 常规SOC计算 ==========
    float soc_coulomb = (current_remaining_capacity / q_max) * 100.0f;
    float soc_voltage = calculateSOC_Voltage(bmsState);
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;
    
    // 满充锚定：高压 + 小电流充电
    if (bmsState.cell_voltage_max > 4150 && bmsState.current > 0 && 
        bmsState.current < cutoff_current) {
        current_remaining_capacity = q_max;
        last_stable_soc_ = 100.0f;
        Serial.println(F("BMS: SOC anchored to 100% (CV taper)"));
    }
    // 放空锚定：低压 + 小电流放电
    else if (bmsState.cell_voltage_min < 3000 && bmsState.current < 0 && 
             abs(bmsState.current) < cutoff_current) {
        current_remaining_capacity = 0;
        last_stable_soc_ = 0.0f;
        Serial.println(F("BMS: SOC anchored to 0% (cutoff)"));
    }
    // 低电流时用电压收敛
    else if (abs(bmsState.current) < cutoff_current && soc_voltage > 0) {
        float diff = soc_voltage - soc_coulomb;
        if (abs(diff) > 2.0f) {
            // 使用温和的收敛系数，避免跳跃
            float convergence_rate = 0.02f;  // 2% per update
            float delta_cap = (diff / 100.0f) * q_max * convergence_rate;
            current_remaining_capacity += delta_cap;
            current_remaining_capacity = constrain(current_remaining_capacity, 0.0f, q_max);
            Serial.printf_P(PSTR("BMS: SOC convergence diff=%.1f%%, adj=%.2fmAh\n"), 
                diff, delta_cap);
        }
        last_stable_soc_ = (current_remaining_capacity / q_max) * 100.0f;
    }
    // 充放电中，信任库仑计
    else {
        last_stable_soc_ = constrain(soc_coulomb, 0.0f, 100.0f);
    }
    
    float soc_delta = abs(bmsState.soc - last_stable_soc_);
    bmsState.soc = constrain(last_stable_soc_, 0.0f, 100.0f);
    bmsState.capacity_remaining = current_remaining_capacity;
    
    if (soc_delta >= 1.0f) {
        EventBus::getInstance().publish(EVT_BMS_SOC_CHANGED, &bmsState.soc);
        Serial.printf_P(PSTR("BMS: SOC %.1f%% (delta=%.1f%%)\n"), bmsState.soc, soc_delta);
    }
}

void BMS::updateSOHLearning(BMS_State& bmsState) {
    float stable_current_threshold = config_.nominal_capacity_mAh / 20.0f;
    float q_nominal = (float)config_.nominal_capacity_mAh;
    float current_soc = bmsState.soc;
    
    if (!soh_learning_ctx_.is_learning) {
        // 等待开始条件：静置电流 + 合理SOC范围 + 距上次≥5分钟
        if (abs(bmsState.current) <= stable_current_threshold && 
            current_soc >= 10.0f && current_soc <= 85.0f &&
            (millis() - soh_learning_ctx_.learning_start_time > 300000)) {
            
            soh_learning_ctx_.is_learning = true;
            soh_learning_ctx_.soc_start = current_soc;
            // 记录原始库仑计累积值，而不是SOH修正后的值
            soh_learning_ctx_.ah_start = cc_accumulated_raw_mAh_;
            soh_learning_ctx_.learning_start_time = millis();
            Serial.printf_P(PSTR("BMS: SOH learning started at SOC=%.1f%%\n"), current_soc);
        }
    } else {
        // 学习进行中
        bool current_ok = (abs(bmsState.current) <= stable_current_threshold * 3);
        
        if (current_ok) {
            float delta_soc = abs(current_soc - soh_learning_ctx_.soc_start);
            
            // SOC变化≥5%即可计算
            if (delta_soc >= 5.0f) {
                float delta_ah_raw = abs(cc_accumulated_raw_mAh_ - soh_learning_ctx_.ah_start);
                
                if (delta_soc > 0.1f && delta_ah_raw > 10.0f) {
                    float q_actual = delta_ah_raw / (delta_soc / 100.0f);
                    float soh_calc = (q_actual / q_nominal) * 100.0f;
                    
                    float soh_new = 0.85f * stats_.soh + 0.15f * soh_calc;
                    soh_new = constrain(soh_new, 40.0f, 100.0f);
                    
                    Serial.printf_P(PSTR("BMS: SOH learned: dSOC=%.1f%% dAh_raw=%.1f "
                        "Q_act=%.1f SOH_calc=%.1f%% -> SOH=%.1f%%\n"),
                        delta_soc, delta_ah_raw, q_actual, soh_calc, soh_new);
                    
                    stats_.soh = soh_new;
                    bmsState.soh = stats_.soh;
                }
                
                soh_learning_ctx_.is_learning = false;
                soh_learning_ctx_.learning_start_time = millis();
                cc_accumulated_raw_mAh_ = 0.0f;
            }
        } else {
            soh_learning_ctx_.is_learning = false;
            soh_learning_ctx_.learning_start_time = millis();
            cc_accumulated_raw_mAh_ = 0.0f;
            Serial.println(F("BMS: SOH learning cancelled (current changed)"));
        }
    }
}

void BMS::detectFullChargeCalibration(BMS_State& bmsState) {
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;
    unsigned long current_time = millis();
    
    // 满充检测：最高单体>4150mV + 充电电流>0且<C/20 + 距上次>10分钟
    if (bmsState.cell_voltage_max > 4150 && 
        bmsState.current > 0 && 
        bmsState.current < cutoff_current &&
        (current_time - last_full_charge_time_ > 600000)) {
        
        float q_max = getTemperatureCompensatedCapacity(bmsState.temperature);
        if (q_max <= 0) q_max = (float)config_.nominal_capacity_mAh;
        
        float old_soc = bmsState.soc;
        current_remaining_capacity = q_max;
        last_stable_soc_ = 100.0f;
        bmsState.soc = 100.0f;
        bmsState.capacity_remaining = current_remaining_capacity;
        full_charge_calibrated_ = true;
        last_full_charge_time_ = current_time;
        
        // 重置SOH学习上下文
        cc_accumulated_raw_mAh_ = 0.0f;
        soh_learning_ctx_.is_learning = false;
        
        Serial.printf_P(PSTR("BMS: FULL CHARGE calibration: %.1f%% -> 100%% (cap=%.1fmAh)\n"),
            old_soc, q_max);
        EventBus::getInstance().publish(EVT_BMS_SOC_CHANGED, &bmsState.soc);
    }
}

void BMS::detectEmptyDischargeCalibration(BMS_State& bmsState) {
    float cutoff_current = config_.nominal_capacity_mAh / 20.0f;
    unsigned long current_time = millis();
    
    // 放空检测：最低单体<3000mV + 放电电流<0且<C/20 + 距上次>10分钟
    if (bmsState.cell_voltage_min < 3000 && 
        bmsState.current < 0 && 
        abs(bmsState.current) < cutoff_current &&
        (current_time - last_empty_discharge_time_ > 600000)) {
        
        float old_soc = bmsState.soc;
        current_remaining_capacity = 0;
        last_stable_soc_ = 0.0f;
        bmsState.soc = 0.0f;
        bmsState.capacity_remaining = 0;
        empty_discharge_calibrated_ = true;
        last_empty_discharge_time_ = current_time;
        
        // 重置SOH学习上下文
        cc_accumulated_raw_mAh_ = 0.0f;
        soh_learning_ctx_.is_learning = false;
        
        Serial.printf_P(PSTR("BMS: EMPTY calibration: %.1f%% -> 0%%\n"), old_soc);
        EventBus::getInstance().publish(EVT_BMS_SOC_CHANGED, &bmsState.soc);
    }
}

void BMS::checkCriticalFaults(BMS_State& bmsState) {
    BMS_Fault_t current_fault = BMS_FAULT_NONE;
    
    float max_v = 0, min_v = 10;
    bool voltage_valid = false;
    
    for (int i = 0; i < config_.cell_count && i < 5; i++) {
        float v = bmsState.cell_voltages[i];
        if (v > 0.5f && v < 5.0f) {
            voltage_valid = true;
            if (v > max_v) max_v = v;
            if (v < min_v) min_v = v;
        }
    }
    
    if (bmsState.current < -(int32_t)config_.short_circuit_threshold) {
        current_fault = BMS_FAULT_SHORT_CIRCUIT;
    } else if (voltage_valid && max_v > config_.cell_ov_threshold) {
        current_fault = BMS_FAULT_OVER_VOLTAGE;
    } else if (voltage_valid && min_v < config_.cell_uv_threshold) {
        current_fault = BMS_FAULT_UNDER_VOLTAGE;
    } else if (bmsState.current > (int32_t)config_.max_charge_current || 
               bmsState.current < -(int32_t)config_.max_discharge_current) {
        current_fault = BMS_FAULT_OVER_CURRENT;
    } else if (bmsState.temperature > config_.temp_overheat_threshold) {
        current_fault = BMS_FAULT_OVER_TEMP;
    }
    
    bmsState.fault_type = current_fault;
    
    if (current_fault != BMS_FAULT_NONE) {
        bmsState.bms_mode = BMS_MODE_FAULT;
        stats_.last_fault = current_fault;
        stats_.last_fault_time = getTimestamp();
        EventBus::getInstance().publishInt(EVT_BMS_FAULT_DETECTED, static_cast<int>(current_fault));
    } else {
        bmsState.bms_mode = BMS_MODE_NORMAL;
    }
}

void BMS::updateFaultLogic(BMS_State& bmsState) {
    i2cPowerOn();
    if (!bq76920_.isConnected()) {
        bmsState.fault_type = BMS_FAULT_NONE;
        return;
    }
    
    if (bmsState.fault_type == BMS_FAULT_OVER_TEMP) return;
    
    if (bmsState.fault_type != BMS_FAULT_NONE) {
        uint8_t fault_reg = bq76920_.getFaultStatus();
        bmsState.fault_type = translateChipFault(fault_reg);
        
        if (bmsState.fault_type == BMS_FAULT_NONE) {
            bmsState.bms_mode = BMS_MODE_NORMAL;
            Serial.println(F("BMS: Fault cleared"));
        } else {
            bmsState.bms_mode = BMS_MODE_FAULT;
        }
    }
}

bool BMS::updateBasicInfo(BMS_State& bmsState) {
    bool read_success = true;
    bmsState.capacity_full = config_.nominal_capacity_mAh;
    
    i2cPowerOn();
    uint16_t cell_voltages[5];
    if (bq76920_.getCellVoltages_mV(cell_voltages, config_.cell_count)) {
        for (uint8_t i = 0; i < config_.cell_count && i < 5; i++) {
            bmsState.cell_voltages[i] = cell_voltages[i];
        }
        
        uint32_t sum = 0;
        bmsState.cell_voltage_min = bmsState.cell_voltage_max = cell_voltages[0];
        
        for (uint8_t i = 0; i < config_.cell_count; i++) {
            if (cell_voltages[i] < bmsState.cell_voltage_min) bmsState.cell_voltage_min = cell_voltages[i];
            if (cell_voltages[i] > bmsState.cell_voltage_max) bmsState.cell_voltage_max = cell_voltages[i];
            sum += cell_voltages[i];
        }
        bmsState.cell_voltage_avg = sum / config_.cell_count;
        bmsState.voltage = sum;
    } else {
        read_success = false;
    }
    
    uint16_t total_voltage = bq76920_.getTotalVoltage_mV();
    if (total_voltage > 9000) bmsState.voltage = total_voltage;
    
    int16_t current = bq76920_.getCurrent_mA();
    if (current != -32768) {
        bmsState.current = current;
    } else {
        read_success = false;
    }
    
    float temp = bq76920_.getTempCelsius();
    if (temp > -100.0f && temp < 150.0f) {
        bmsState.temperature = temp;
    } else {
        read_success = false;
    }
    
    return read_success;
}

bool BMS::processCoulombCounterData() {
    i2cPowerOn();
    if (!initialized_ || !bq76920_.isConnected()) return false;
    if (!cc_ready_pending_) return false;
    
    int16_t raw_cc_value = 0;
    if (!bq76920_.readCoulombCounterRaw(raw_cc_value)) {
        Serial.println(F("BMS: CC read failed"));
        cc_ready_pending_ = false;
        return false;
    }

    bq76920_.clearCoulombCounterFlag();
    cc_ready_pending_ = false;

    // 正确的计算方式：
    // 每个LSB: 8.44μV, 积分时间250ms, 检流电阻5mΩ
    // 电荷量(mAh) = raw_cc_value * (8.44μV / 5mΩ) * (0.25s / 3600s/h)
    //             = raw_cc_value * (8.44f / 5.0f) / 14400.0f
    //const float CC_LSB_TO_MAH = (8.44f / 5.0f) / 14400.0f;
    float delta_mah = raw_cc_value * 0.000117222f;

    current_remaining_capacity += delta_mah;
    if (delta_mah > 0) {
        accumulated_charge_mAh += delta_mah;
    } else {
        accumulated_discharge_mAh -= delta_mah;
    }
    
    // SOH学习用的原始累积（不受SOH缩放影响）
    cc_accumulated_raw_mAh_ += delta_mah;
    
    // 限制范围
    float q_max = getAvailableCapacity();
    if (q_max <= 0) q_max = (float)config_.nominal_capacity_mAh;
    current_remaining_capacity = constrain(current_remaining_capacity, 0.0f, q_max);
    
    // 循环计数
    float q_nominal = (float)config_.nominal_capacity_mAh;
    float half_throughput = (accumulated_charge_mAh + accumulated_discharge_mAh) / 2.0f;
    
    while (half_throughput >= q_nominal) {
        stats_.total_cycles++;
        accumulated_charge_mAh -= q_nominal;
        accumulated_discharge_mAh -= q_nominal;
        half_throughput = (accumulated_charge_mAh + accumulated_discharge_mAh) / 2.0f;
        Serial.printf_P(PSTR("BMS: Cycle count incremented to %u\n"), stats_.total_cycles);
    }
    
    return true;
}

BMS_Fault_t BMS::translateChipFault(uint8_t fault_register) {
    uint8_t faults = fault_register & (~STAT_CC_READY);
    if (faults == 0) return BMS_FAULT_NONE;
    if (faults & STAT_OV) return BMS_FAULT_OVER_VOLTAGE;
    if (faults & STAT_UV) return BMS_FAULT_UNDER_VOLTAGE;
    if (faults & STAT_OCD) return BMS_FAULT_OVER_CURRENT;
    if (faults & STAT_SCD) return BMS_FAULT_SHORT_CIRCUIT;
    if (faults & STAT_DEVICE_XREADY) return BMS_FAULT_CHIP_ERROR;
    if (faults & STAT_OVRD_ALERT) return BMS_FAULT_PASSIVE_SHUTDOWN;
    return BMS_FAULT_NONE;
}

bool BMS::saveToStorage() {
    if (!initialized_) return false;
    preferences_.putFloat(PREFS_KEY_SOH, stats_.soh);
    preferences_.putUInt(PREFS_KEY_CYCLES, stats_.total_cycles);
    preferences_.putUInt(PREFS_KEY_LAST_FAULT, static_cast<uint32_t>(stats_.last_fault));
    preferences_.putUInt(PREFS_KEY_LAST_FAULT_TIME, stats_.last_fault_time);
    preferences_.putULong64(PREFS_KEY_BAL_EVENTS, stats_.balancing_events);
    
    // 保存剩余容量
    preferences_.putFloat(PREFS_KEY_REMAINING_CAP, current_remaining_capacity);
    
    Serial.printf_P(PSTR("BMS: Saved (SOH=%.1f%%, cap=%.1f mAh)\n"),
        stats_.soh, current_remaining_capacity);
    return true;
}

bool BMS::loadFromStorage() {
    stats_.soh = preferences_.getFloat(PREFS_KEY_SOH, 100.0f);
    stats_.total_cycles = preferences_.getUInt(PREFS_KEY_CYCLES, 0);
    stats_.last_fault = static_cast<BMS_Fault_t>(preferences_.getUInt(PREFS_KEY_LAST_FAULT, 0));
    stats_.last_fault_time = preferences_.getUInt(PREFS_KEY_LAST_FAULT_TIME, 0);
    stats_.balancing_events = preferences_.getULong64(PREFS_KEY_BAL_EVENTS, 0);
    
    // 恢复剩余容量
    current_remaining_capacity = preferences_.getFloat(PREFS_KEY_REMAINING_CAP, -1.0f);
    
    if (stats_.soh < 0 || stats_.soh > 100) stats_.soh = 100.0f;
    return true;
}

bool BMS::validateConfig(const BMS_Config_t& config) {
    if (config.cell_count < 1 || config.cell_count > 5) return false;
    if (config.nominal_capacity_mAh == 0) return false;
    if (config.cell_ov_threshold <= config.cell_uv_threshold) return false;
    return true;
}

BQ76920_InitConfig BMS::generateChipConfig(const BMS_Config_t& config) {
    return {
        .cell_count = config.cell_count,
        .cell_ov_threshold = config.cell_ov_threshold,
        .cell_uv_threshold = config.cell_uv_threshold,
        .ov_delay = OV_DELAY_1_S,
        .uv_delay = UV_DELAY_1_S,
        .max_discharge_current = config.max_discharge_current,
        .ocd_delay = OCD_DELAY_80_MS,
        .short_circuit_threshold = config.short_circuit_threshold,
        .scd_delay = SCD_DELAY_100_US,
        .coulomb_counter_enabled = true
    };
}