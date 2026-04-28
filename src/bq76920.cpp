#include "bq76920.h"
#include "i2c_interface.h"
#include <Arduino.h>

// =============================================================================
// Constructor and Initialization
// =============================================================================

BQ76920::BQ76920()
    : i2c(nullptr)
    , bq_gain(DEFAULT_GAIN_MV_PER_LSB)
    , bq_offset(DEFAULT_OFFSET_MV)
    , current_zero_drift(CURRENT_ZERO_DRIFT)
    , actual_cell_count(0) {
}

BQ76920::BQ76920(I2CInterface& i2c_interface)
    : i2c(&i2c_interface)
    , bq_gain(DEFAULT_GAIN_MV_PER_LSB)
    , bq_offset(DEFAULT_OFFSET_MV)
    , current_zero_drift(CURRENT_ZERO_DRIFT)
    , actual_cell_count(0) {
}

/**
 * @brief Initialize BQ76920 device
 * @return true if initialization successful, false otherwise
 */
bool BQ76920::begin() {    
    // Verify I2C interface availability
    if (i2c == nullptr) {
        Serial.println(F("BQ76920: I2C interface not available"));
        return false;
    }
    
    // Check device connection status
    if (!isConnected()) {
        Serial.println(F("BQ76920: Device not connected"));
        return false;
    }
    //首先要关闭充放电，以免旧设置影响
    setMOS(0,0);
    delay(INIT_DELAY_MS);
    Serial.println(F("BQ76920: Initialized successfully"));
    return true;
}

/**
 * @brief Complete chip initialization process
 * @param config 初始化配置参数
 * @return true if initialization successful, false otherwise
 */
bool BQ76920::initializeChip(const BQ76920_InitConfig &config) {
    if (config.cell_count < 3 || config.cell_count > 5) {
        Serial.println(F("BQ76920: Invalid cell count, must be 3-5"));
        return false;
    }
    
    // Store cell count
    actual_cell_count = config.cell_count;
    
    if (!writeRegister(STATUS_REG_ADDR, 0xff)) {
        Serial.println(F("BQ76920: Failed to clear status register"));
        return false;
    }
    delay(INIT_DELAY_MS);

    // cc_cfg write 0x19
    if (!writeRegister(CC_CFG_REG_ADDR, 0x19)) {
        Serial.println(F("BQ76920: Failed to write CC_CFG register"));
        return false;
    }
    delay(INIT_DELAY_MS);
    
    // Force disable cell balancing to prevent 100 ohm resistor from causing voltage drop
    // 用户后续可通过 setCellBalance 开启
    if (!writeRegister(CELL_BALANCE_REG_ADDR, 0x00)) {
        Serial.println(F("BQ76920: Failed to disable cell balancing"));
        return false;
    }
    delay(INIT_DELAY_MS);

    uint8_t sys_ctrl1;
    if (!readRegister(SYS_CTRL1_REG_ADDR, &sys_ctrl1)) {
        Serial.println(F("BQ76920: Failed to read SYS_CTRL1 register"));
        return false;
    }
    
    // Bit 4 (ADC_EN): Enable voltage sampling
    // Bit 3 (TEMP_SEL): 1 for external NTC, 0 for internal sensing. Usually select 1
    sys_ctrl1 |= (ADC_ENABLE_BIT | TEMP_SELECT_BIT);
    if (!writeRegister(SYS_CTRL1_REG_ADDR, sys_ctrl1)) {
        Serial.println(F("BQ76920: Failed to enable ADC and temperature sensing"));
        return false;
    }

    // Read gain and offset calibration parameters ---
    Serial.println(F("BQ76920: Reading calibration parameters..."));
    uint8_t retry = INIT_RETRY_COUNT;
    bool calibration_success = false;
    
    while(retry--) {
        readADCParams();
        if(getBqGain() > CALIBRATION_MIN_GAIN) {
            calibration_success = true;
            break;
        }
        delay(CALIBRATION_RETRY_DELAY_MS);
    }
    
    if (!calibration_success) {
        Serial.println(F("BQ76920: Calibration parameter read failed, using defaults"));
        bq_gain = DEFAULT_CALIBRATION_GAIN;
        bq_offset = 0;
    }
    
    // Force correction! Give it a reasonable value regardless of what was read
    if(getBqOffset() < CALIBRATION_OFFSET_MIN || getBqOffset() > CALIBRATION_OFFSET_MAX) {
        bq_offset = 0;
        Serial.println(F("BQ76920: Offset out of range, reset to 0"));
    }

    if(getBqGain() < CALIBRATION_GAIN_MIN || getBqGain() > CALIBRATION_GAIN_MAX) {
        bq_gain = DEFAULT_CALIBRATION_GAIN;
        Serial.println(F("BQ76920: Gain out of range, using default value"));
    }
    
    // Apply gain adjustment factor
    bq_gain = bq_gain * GAIN_ADJUSTMENT_FACTOR;

    // Configure Over/Under Voltage Thresholds
    uint8_t ovReg = calculateTripReg(config.cell_ov_threshold, bq_gain, bq_offset);
    uint8_t uvReg = calculateTripReg(config.cell_uv_threshold, bq_gain, bq_offset);
    if (!writeRegister(OV_TRIP_REG_ADDR, ovReg)) {
        Serial.println(F("BQ76920: Failed to configure over-voltage threshold"));
        return false;
    }
    if (!writeRegister(UV_TRIP_REG_ADDR, uvReg)) {
        Serial.println(F("BQ76920: Failed to configure under-voltage threshold"));
        return false;
    }

    // Configure Over/Under Voltage Delays (PROTECT3)
    uint8_t protect3 = ((config.uv_delay & 0x03) << 6) | ((config.ov_delay & 0x03) << 4);
    if (!writeRegister(PROTECT3_REG_ADDR, protect3)) {
        Serial.println(F("BQ76920: Failed to configure voltage protection delay"));
        return false;
    }

    // Configure Over-Current/Short-Circuit Protection (OCD/SCD)
    if (!setOCDProtection(config.max_discharge_current * RSENSE_VALUE, config.ocd_delay)) {
        Serial.println(F("BQ76920: Failed to configure over-current protection"));
        return false;
    }
    if (!setSCDProtection(config.short_circuit_threshold * RSENSE_VALUE, config.scd_delay)) {
        Serial.println(F("BQ76920: Failed to configure short circuit protection"));
        return false;
    }
    
    Serial.printf_P(PSTR("BQ76920: Initialization complete. Cells: %d, Gain: %.4f mV/LSB, Offset: %d mV\n"), 
    actual_cell_count, bq_gain, bq_offset);
    
    // Initialize coulomb counter if enabled
    if (config.coulomb_counter_enabled) {
        uint8_t sys_ctrl2;
        if (!readRegister(SYS_CTRL2_REG_ADDR, &sys_ctrl2)) {
            Serial.println(F("BQ76920: Failed to read SYS_CTRL2 register"));
            return false;
        }
        
        // Bit 6 (CC_EN): Enable continuous current sampling
        sys_ctrl2 |= CC_ENABLE_BIT;
        if (!writeRegister(SYS_CTRL2_REG_ADDR, sys_ctrl2)) {
            Serial.println(F("BQ76920: Failed to enable coulomb counter"));
            return false;
        }

        // --- Step 3: Necessary waiting ---
        Serial.println(F("BQ76920: Waiting for ADC stabilization..."));
        delay(ADC_STABILIZE_DELAY_MS);
        Serial.println(F("BMS: Calibrating current zero drift..."));
        calibrateCurrentZero();
    }
    
    Serial.println(F("BQ76920: Chip initialized successfully"));
    return true;
}

/**
 * @brief 动态更新配置参数
 * 仅更新运行时可变的配置项，无需重新初始化整个芯片
 * 适用于在系统运行过程中调整保护阈值、延迟等参数
 * @param config 新的配置参数
 * @return true 更新成功，false 更新失败
 */
bool BQ76920::updateConfiguration(const BQ76920_InitConfig &config) {
    // 验证电池串数有效性（但不修改已初始化的值）
    if (config.cell_count < 3 || config.cell_count > 5) {
        Serial.println(F("BQ76920: Invalid cell count in update, must be 3-5"));
        return false;
    }
    
    bool success = true;
    
    // 1. 更新过压/欠压阈值
    uint8_t ovReg = calculateTripReg(config.cell_ov_threshold, bq_gain, bq_offset);
    uint8_t uvReg = calculateTripReg(config.cell_uv_threshold, bq_gain, bq_offset);
    
    if (!writeRegister(OV_TRIP_REG_ADDR, ovReg)) {
        Serial.println(F("BQ76920: Failed to update over-voltage threshold"));
        success = false;
    }
    
    if (!writeRegister(UV_TRIP_REG_ADDR, uvReg)) {
        Serial.println(F("BQ76920: Failed to update under-voltage threshold"));
        success = false;
    }
    
    // 2. 更新过压/欠压延时
    uint8_t protect3 = ((config.uv_delay & 0x03) << 6) | ((config.ov_delay & 0x03) << 4);
    if (!writeRegister(PROTECT3_REG_ADDR, protect3)) {
        Serial.println(F("BQ76920: Failed to update voltage protection delay"));
        success = false;
    }
    
    // 3. 更新过流保护配置
    if (!setOCDProtection(config.max_discharge_current * RSENSE_VALUE, config.ocd_delay)) {
        Serial.println(F("BQ76920: Failed to update over-current protection"));
        success = false;
    }
    
    // 4. 更新短路保护配置
    if (!setSCDProtection(config.short_circuit_threshold * RSENSE_VALUE, config.scd_delay)) {
        Serial.println(F("BQ76920: Failed to update short circuit protection"));
        success = false;
    }
    
    if (success) {
        Serial.printf_P(PSTR("BQ76920: Configuration updated. Cells: %d, OV: %dmV, UV: %dmV, Max Current: %dmA\n"),
            config.cell_count, config.cell_ov_threshold, config.cell_uv_threshold, config.max_discharge_current);
    }
    
    return success;
}

// =============================================================================
// Device Connection Detection
// =============================================================================

bool BQ76920::isConnected() {
    if (i2c == nullptr) {
        return false;
    }
    
    // 检查缓存是否有效（1 秒内）
    unsigned long current_time = millis();
    if (current_time - last_connection_check_time_ < 1000UL) {
        // 缓存有效期内直接返回上次结果
        return last_connection_status_;
    }
    
    // 超过 1 秒，执行实际的 I2C 检测
    last_connection_status_ = i2c->isDeviceConnected(BQ76920_ADDRESS);
    last_connection_check_time_ = current_time;
    
    return last_connection_status_;
}

// =============================================================================
// Register Read/Write Operations
// =============================================================================

bool BQ76920::readRegister(uint8_t reg, uint8_t *value) {
    if (i2c == nullptr) {
        return false;
    }
    
    bool result = i2c->readRegisterByteCRC(BQ76920_ADDRESS, reg, value);
    
    // 读取成功时刷新连接状态缓存
    if (result) {
        last_connection_status_ = true;
        last_connection_check_time_ = millis();
    }
    
    return result;
}
bool BQ76920::readRegisterWord(uint8_t reg, uint16_t *value) {
    if (i2c == nullptr) {
        return false;
    }
    
    bool result = i2c->readRegisterWordCRC(BQ76920_ADDRESS, reg, value);
    
    // 读取成功时刷新连接状态缓存
    if (result) {
        last_connection_status_ = true;
        last_connection_check_time_ = millis();
    }
    
    return result;
}

bool BQ76920::writeRegister(uint8_t reg, uint8_t value) {
    if (i2c == nullptr) {
        return false;
    }
    
    bool result = i2c->writeRegisterByteCRC(BQ76920_ADDRESS, reg, value);
    
    // 写入成功时刷新连接状态缓存
    if (result) {
        last_connection_status_ = true;
        last_connection_check_time_ = millis();
    }
    
    return result;
}

// =============================================================================
// ADC Parameter Calibration
// =============================================================================

void BQ76920::readADCParams() {
    uint8_t gain_reg1, gain_reg2, offset_reg;
    
    const bool gain_success = readRegister(GAIN_REG1_ADDR, &gain_reg1) && 
                             readRegister(GAIN_REG2_ADDR, &gain_reg2);
    
    if (gain_success) {
        const uint16_t gain_uV = BASE_GAIN_UV + 
                                (((uint16_t)(gain_reg1 & GAIN_MASK1) << GAIN_SHIFT1) + 
                                 ((uint16_t)(gain_reg2 & GAIN_MASK2) >> GAIN_SHIFT2));
        bq_gain = gain_uV / UV_TO_MV_FACTOR;
    } else {
        bq_gain = DEFAULT_GAIN_MV_PER_LSB;
    }
    
    if (readRegister(OFFSET_REG_ADDR, &offset_reg)) {
        bq_offset = static_cast<int8_t>(offset_reg);
    } else {
        bq_offset = DEFAULT_OFFSET_MV;
    }
}

// =============================================================================
// Voltage Measurement
// =============================================================================
// 获取指定电池单元电压，增加逻辑索引到硬件寄存器的映射
uint16_t BQ76920::getCellVoltage_mV(uint8_t cell_idx) {
    // 1. 检查索引有效性
    if (cell_idx >= actual_cell_count) {
        return 0;
    }
    
    // 2. 逻辑索引到硬件通道的映射（基于你的 3/4/5 串逻辑）
    uint8_t hw_channel;
    if (cell_idx < (actual_cell_count - 1)) {
        hw_channel = cell_idx; // VC1, VC2...
    } else {
        hw_channel = 4;        // 最后一节始终对应 VC5
    }
    
    // 计算起始寄存器地址 (例如 VC1_HI 是 0x0C)
    const uint8_t base_addr = VOLTAGE_BASE_ADDR + (hw_channel * VOLTAGE_REG_STEP);

    // 3. 使用原子函数读取双字节（含 CRC 校验）
    uint16_t adc_raw_word = 0;
    if (!readRegisterWord(base_addr, &adc_raw_word)) {
        // 如果 CRC 校验失败或 I2C 通信失败，直接返回 0
        return 0;
    }

    // 4. 处理 ADC 原始值
    // 手册规定电压 ADC 为 14 位，取低 14 位即可 (0x3FFF = 0011 1111 1111 1111)
    const uint16_t adc_raw = adc_raw_word & 0x3FFF;
    
    // 5. 根据公式计算毫伏值: V = GAIN * ADC + OFFSET
    // 注意：bq_gain 通常是 365-396 uV/LSB，bq_offset 单位是 mV
    // 如果 bq_gain 是整数且单位是 uV，计算如下：
    const uint16_t voltage_mv = static_cast<uint16_t>((adc_raw * bq_gain) + bq_offset);
    
    return voltage_mv;
}


// 批量读取电芯电压 (优化 I2C 通信效率)
bool BQ76920::getCellVoltages_mV(uint16_t* voltages, uint8_t count) {
    if (count > actual_cell_count || voltages == nullptr) return false;
    
    for (uint8_t i = 0; i < count; i++) {
        voltages[i] = getCellVoltage_mV(i);
    }
    return true;
}

// 根据逻辑电芯索引获取硬件均衡位掩码
uint8_t BQ76920::getBalanceMaskForCell(uint8_t cell_idx) {
    if (cell_idx >= actual_cell_count) {
        return 0;
    }
    
    // [新增] 逻辑索引到硬件均衡位的映射
    // CELL_BALANCE_REG_ADDR (0x01) 的 bit0-4 分别控制 VC1-VC5 的均衡
    // 3串: 逻辑0->bit0, 逻辑1->bit1, 逻辑2->bit4
    // 4串: 逻辑0->bit0, 逻辑1->bit1, 逻辑2->bit2, 逻辑3->bit4
    // 5串: 逻辑0->bit0, 逻辑1->bit1, 逻辑2->bit2, 逻辑3->bit3, 逻辑4->bit4
    if (cell_idx < (actual_cell_count - 1)) {
        return (1 << cell_idx);
    } else {
        return (1 << 4);  // 最后一节电芯始终对应 bit4 (VC5)
    }
}

// =============================================================================
// MOS Tube Control
// =============================================================================

void BQ76920::setMOS(uint8_t chg, uint8_t dsg) {
    // Clear all fault flag bits to ensure MOS tubes can be controlled normally
    writeRegister(STATUS_REG_ADDR, 0xff);
    
    uint8_t control_reg;
    if (!readRegister(SYS_CTRL2_REG_ADDR, &control_reg)) {
        return;
    }
    
    if (chg) {
        control_reg |= CHARGE_ENABLE_BIT;
    } else {
        control_reg &= ~CHARGE_ENABLE_BIT;
    }
    
    if (dsg) {
        control_reg |= DISCHARGE_ENABLE_BIT;
    } else {
        control_reg &= ~DISCHARGE_ENABLE_BIT;
    }
    
    writeRegister(SYS_CTRL2_REG_ADDR, control_reg); 
}
/**
 * @brief 读取充放电 MOS 管硬件状态
 * 读取 SYS_CTRL2 寄存器并解析 CHG_EN 和 DSG_EN 位
 */
bool BQ76920::getMOS(bool& isCHG, bool& isDSG) {
    uint8_t control_reg = 0;
    
    // 读取系统控制寄存器 2 (SYS_CTRL2)
    if (!readRegister(SYS_CTRL2_REG_ADDR, &control_reg)) {
        return false;
    }

    // 解析充电 MOS 状态 (Bit 0: CHARGE_ENABLE_BIT)
    isCHG = (control_reg & CHARGE_ENABLE_BIT) != 0;

    // 解析放电 MOS 状态 (Bit 1: DISCHARGE_ENABLE_BIT)
    isDSG = (control_reg & DISCHARGE_ENABLE_BIT) != 0;

    return true;
}

// =============================================================================
// Current Measurement
// =============================================================================

// 设置电流零漂
void BQ76920::setCurrentZeroDrift(int16_t drift) {
    current_zero_drift = drift;
}

// 校准电流零漂 (需在无负载静止状态下调用)
void BQ76920::calibrateCurrentZero() {
    uint16_t raw_word = 0;
    // 使用原子读取获取 CC_HI 和 CC_LO (0x32)
    if (readRegisterWord(CURRENT_REG_HIGH, &raw_word)) {
        // 直接转为有符号 16 位，因为 CC 是补码格式
        current_zero_drift = (int16_t)raw_word;
        Serial.printf_P(PSTR("BQ76920: Current zero drift calibrated to %d\n"), current_zero_drift);
    }
}
int16_t BQ76920::getCurrent_mA() {
    uint16_t raw_word = 0;
    
    // 1. [原子操作] 一次性读取 [CC_HI, CRC, CC_LO, CRC]
    if (!readRegisterWord(CURRENT_REG_HIGH, &raw_word)) {
        return 0;
    }

    // 2. 转换为有符号 16 位原始值
    int16_t cc_raw = (int16_t)raw_word;
    
    // 3. 应用动态零漂校准
    int16_t corrected_raw = cc_raw - current_zero_drift;
    
    // 4. 计算电流 (mA)
    // 公式: I = (CC_Reading * 8.44 uV) / Rsense_mOhm
    // CURRENT_SCALE_FACTOR 通常应为 (8.44 / Rsense)
    float current_ma = (float)corrected_raw * CURRENT_SCALE_FACTOR;
    
    return (int16_t)current_ma;
}

// =============================================================================
// Temperature Measurement
// =============================================================================

float BQ76920::getTempCelsius() {
    uint16_t adc_raw_word = 0;

    // 1. [原子操作] 读取寄存器，失败直接返回错误码
    if (!readRegisterWord(TEMP_REG_HIGH, &adc_raw_word)) {
        return INVALID_TEMPERATURE;
    }

    // 2. 获取 14 位有效数据 (掩码 0x3FFF)
    // 注意：BQ76920 温度 ADC 通常是无符号的，但为了计算安全使用 uint16_t
    uint16_t adc_value = adc_raw_word & 0x3FFF;

    // 防御性检查：确保表至少有 2 个点才能插值
    if (TEMP_TABLE_SIZE < 2) {
        return INVALID_TEMPERATURE;
    }

    // 3. 【边界快速检查】利用单调递减特性
    // 表结构假设: index 0 (低温/高ADC) -> index N (高温/低ADC)
    // 例如: [-10°C: 3500mV, ..., 95°C: 500mV]
    
    // 如果 ADC 比表中最大值还大 (比最冷还冷)
    if (adc_value >= TEMP_LOOKUP_TABLE[0]) {
        return (float)TEMP_TABLE_START;
    }
    // 如果 ADC 比表中最小值还小 (比最热还热)
    if (adc_value <= TEMP_LOOKUP_TABLE[TEMP_TABLE_SIZE - 1]) {
        return (float)TEMP_TABLE_END;
    }

    // 4. 【二分查找】定位区间
    // 目标：找到索引 i，使得 TEMP_LOOKUP_TABLE[i] >= adc_value > TEMP_LOOKUP_TABLE[i+1]
    // 因为数组是递减的：Left 指向较大的值(低温侧)，Right 指向较小的值(高温侧)
    int left = 0;
    int right = (int)TEMP_TABLE_SIZE - 1;
    int mid;

    // 当左右索引不相邻时继续缩小范围
    while (left < right - 1) {
        mid = left + (right - left) / 2;

        if (TEMP_LOOKUP_TABLE[mid] == adc_value) {
            // 精确匹配，直接返回对应温度
            return (float)(TEMP_TABLE_START + mid);
        } else if (TEMP_LOOKUP_TABLE[mid] > adc_value) {
            // 中间值 > 当前值。因为数组递减，说明当前值在右侧 (高温侧/小值侧)
            // 但 mid 本身可能是区间的上界 (Left)，因为我们要找的是 >= adc 的那个点
            left = mid; 
        } else {
            // 中间值 < 当前值。说明当前值在左侧 (低温侧/大值侧)
            // mid 可能是区间的下界 (Right)
            right = mid;
        }
    }

    // 循环结束时，left 和 right 必然相邻 (right = left + 1)
    // 此时区间锁定在: [TEMP_LOOKUP_TABLE[left], TEMP_LOOKUP_TABLE[right]]
    // 对应温度: [TEMP_TABLE_START + left, TEMP_TABLE_START + right]
    
    // 5. 【线性插值】
    // 物理意义：ADC 从 High (left) 降到 Low (right)，温度从 T_base 升到 T_base + 1
    uint16_t adc_high = TEMP_LOOKUP_TABLE[left];   // 对应较低温度 (较大 ADC)
    uint16_t adc_low  = TEMP_LOOKUP_TABLE[right];  // 对应较高温度 (较小 ADC)
    
    float adc_range = (float)(adc_high - adc_low);
    
    // 避免除以零 (虽然理论上单调表不会出现，但防万一)
    if (adc_range == 0.0f) {
        return (float)(TEMP_TABLE_START + left);
    }

    // 计算比例：(当前偏离高点的距离) / (总跨度)
    // 因为 ADC 随温度升高而降低，所以分子是 (adc_high - adc_value)
    float ratio = (float)(adc_high - adc_value) / adc_range;

    // 最终温度 = 基础温度 + 比例 * 1°C
    return (float)TEMP_TABLE_START + (float)left + ratio;
}


// =============================================================================
// Protection Configuration Helpers
// =============================================================================

// 核心计算逻辑（静态或私有辅助函数）
uint8_t BQ76920::calculateTripReg(uint16_t voltage_mV, float gain, float offset) {
    // 1. 逆向计算 Trip 值 (公式: Voltage = (Trip << 4) * Gain + Offset)
    // 所以 TripFull = (Voltage - Offset) / Gain
    float tripFull = ((float)voltage_mV - offset) / gain;

    // 2. 边界限幅 (BQ76920 内部比较器通常是 13-14位，但寄存器映射逻辑需参考手册)
    // 确保不小于 0，且不超过 uint16 能表示的范围
    if (tripFull < 0.0f) tripFull = 0.0f;
    if (tripFull > 16383.0f) tripFull = 16383.0f; // 0x3FFF

    // 3. 四舍五入后再强转，避免精度丢失导致的"缩水"
    uint16_t tripInt = (uint16_t)roundf(tripFull);

    // 4. 取高 8 位 (根据手册，OV_TRIP/UV_TRIP 寄存器对应的是 trip_value 的 [11:4] 位)
    return (uint8_t)((tripInt >> 4) & 0xFF);
}

uint8_t BQ76920::findClosestOCDThreshold(float target_mV, bool &rsnsBit) {
    float bestErr = 1000.0;
    uint8_t bestIdx = 0;
    rsnsBit = true;

   for (int i = 0; i <= 0xF; i++) {
        float err = abs(BQ76920_OCD_THRESH_HIGH[i] - target_mV);
        if (err < bestErr) {
            bestErr = err;
            bestIdx = i;
        }
    }

    float highRangeErr = bestErr;
    uint8_t highRangeIdx = bestIdx;

    // 如果目标值很小，尝试低量程 (RSNS=0)
    if (target_mV < 17.0) {
        rsnsBit = false;
        bestErr = 1000.0;
        bestIdx = 0;
        for (int i = 0; i <= 0xF; i++) {
            float err = abs(BQ76920_OCD_THRESH_LOW[i] - target_mV);
            if (err < bestErr) {
                bestErr = err;
                bestIdx = i;
            }
        }
    }

        // 比较高低量程哪个误差更小
    if (bestErr < highRangeErr) {
        rsnsBit = false; // 选择低量程
        return bestIdx;
    } else {
        rsnsBit = true;  // 选择高量程
        return highRangeIdx;
    }
}

uint8_t BQ76920::findClosestSCDThreshold(float target_mV, bool &rsnsBit) {
    float bestErr = 1000.0;
    uint8_t bestIdx = 0;
    bool bestRsns = true;

    // 遍历两种量程，找出全局误差最小的组合
    
    // 1. 尝试高量程 (RSNS=1) - 8 个元素
    for (int i = 0; i < 8; i++) {
        float err = abs(BQ76920_SCD_THRESH_HIGH[i] - target_mV);
        if (err < bestErr) {
            bestErr = err;
            bestIdx = i;
            bestRsns = true;
        }
    }

    // 2. 尝试低量程 (RSNS=0) - 8 个元素
    for (int i = 0; i < 8; i++) {
        float err = abs(BQ76920_SCD_THRESH_LOW[i] - target_mV);
        if (err < bestErr) {
            bestErr = err;
            bestIdx = i;
            bestRsns = false;
        }
    }

    rsnsBit = bestRsns;
    return bestIdx;
}

// =============================================================================
// Public Protection API
// =============================================================================

bool BQ76920::setOVThreshold(uint16_t voltage_mV) {
    uint8_t regVal = calculateTripReg(voltage_mV, bq_gain, bq_offset);
    return writeRegister(OV_TRIP_REG_ADDR, regVal);
}

bool BQ76920::setUVThreshold(uint16_t voltage_mV) {
    uint8_t regVal = calculateTripReg(voltage_mV, bq_gain, bq_offset);
    return writeRegister(UV_TRIP_REG_ADDR, regVal);
}

float BQ76920::getOVThreshold() {
    uint8_t regVal = 0;
    if (!readRegister(OV_TRIP_REG_ADDR, &regVal)) return 0.0f;
    uint16_t fullAdc = ((uint16_t)regVal << 4) | 0x08;
    fullAdc |= 0x2000; 
    float voltage_mV = (fullAdc * bq_gain) + bq_offset;
    return voltage_mV / 1000.0;
}

float BQ76920::getUVThreshold() {
    uint8_t regVal = 0;
    if (!readRegister(UV_TRIP_REG_ADDR, &regVal)) return 0.0f;
    uint16_t fullAdc = ((uint16_t)regVal << 4);
    fullAdc |= 0x1000;
    float voltage_mV = (fullAdc * bq_gain) + bq_offset;
    return voltage_mV / 1000.0;
}

bool BQ76920::setOVUVDelay(BMS_OVDelay_t ovDelay, BMS_UVDelay_t uvDelay) {
    uint8_t protect3 = ((uvDelay & 0x03) << 6) | ((ovDelay & 0x03) << 4);
    return writeRegister(PROTECT3_REG_ADDR, protect3);
}

// [修复] 重写 OCD 设置逻辑，避免覆盖 SCD 的 RSNS 位
bool BQ76920::setOCDProtection(float threshold_mV, BMS_OCDDelay_t delay) {
    bool rsnsBit;
    uint8_t threshIdx = findClosestOCDThreshold(threshold_mV, rsnsBit);
    
    uint8_t protect2 = ((delay & 0x07) << 4) | (threshIdx & 0x0F);
    
    // 读取当前 PROTECT1，只修改 RSNS 位，保留 SCD 的配置
    uint8_t p1 = 0;
    if (!readRegister(PROTECT1_REG_ADDR, &p1)) {
        return false;
    }
    
    if (rsnsBit) p1 |= 0x80;
    else p1 &= ~0x80;
    
    if (!writeRegister(PROTECT1_REG_ADDR, p1)) {
        return false;
    }
    
    return writeRegister(PROTECT2_REG_ADDR, protect2);
}

// [修复] 重写 SCD 设置逻辑，避免覆盖 OCD 的 RSNS 位
bool BQ76920::setSCDProtection(float threshold_mV, BMS_SCDDelay_t delay) {
    bool rsnsBit;
    uint8_t threshIdx = findClosestSCDThreshold(threshold_mV, rsnsBit);
    
    uint8_t p1 = 0;
    if (!readRegister(PROTECT1_REG_ADDR, &p1)) {
        return false;
    }
    
    // 保留 Bit 7 (RSNS) 以外的位？不，我们需要更新 SCD 的 Delay 和 Thresh
    // 但要小心不要破坏未来的扩展位。这里我们明确构造新的 P1 值
    // 策略：先读取，清除 SCD 相关位 (Bit 4:0)，然后填入新值和当前的 RSNS 决策
    
    // 清除 SCD_DELAY (4:3) 和 SCD_THRESH (2:0)，保留 RSNS (7) 和其他保留位
    p1 &= 0x80; 
    
    // 如果本次计算需要 RSNS=1，则置位，否则保持 0 (注意：如果 OCD 需要 RSNS=1，setOCDProtection 会再次置位)
    // 这里的逻辑是：SCD 设置函数只负责"如果我自己需要高量程，我就置位"。
    // 如果 OCD 也需要，它也会置位。如果 OCD 不需要但 SCD 需要，SCD 置位。
    // 唯一的风险是：OCD 需要高量程，但 SCD 不需要且最后被调用，它会清除 RSNS。
    // **因此，最佳实践是同时调用或提供一个联合设置函数**。
    // 为了兼容性，这里我们采取保守策略：如果 SCD 需要高量程，则置位；如果不需，**不清除**，留给 OCD 决定？
    // 不行，寄存器位是共享的。
    // **修正策略**: 读取当前 RSNS 状态。如果 SCD 需要高量程，强制置 1。如果 SCD 不需要，**保持原状** (假设 OCD 可能已经设置了)。
    // 这样如果 OCD 先运行设置了 1，SCD 后运行且不需要，不会清除它。
    
    uint8_t current_rsns = p1 & 0x80;
    if (rsnsBit) {
        p1 |= 0x80;
    } 
    // else: do nothing, keep current_rsns
    
    p1 |= ((delay & 0x03) << 3);
    p1 |= (threshIdx & 0x07);
    
    return writeRegister(PROTECT1_REG_ADDR, p1);
}

static float getOCDThresholdFromIndex(uint8_t index, bool rsnsHigh) {
    if (index > 0xF) return 0.0f;
    
     return rsnsHigh ? BQ76920_OCD_THRESH_HIGH[index] : BQ76920_OCD_THRESH_LOW[index];
}

// 辅助函数：根据索引和 RSNS 位获取 SCD 阈值表中的 mV 值
static float getSCDThresholdFromIndex(uint8_t index, bool rsnsHigh) {
    if (index > 0x7) return 0.0f;
    
     return rsnsHigh ? BQ76920_SCD_THRESH_HIGH[index] : BQ76920_SCD_THRESH_LOW[index];
}

// 辅助函数：将 OCD 延迟枚举转换为 ms
static uint16_t getOCDDelayMs(uint8_t regVal) {
    if (regVal > 7) return 0;
    return BQ76920_OCD_DELAY_MS[regVal];
}

// 辅助函数：将 SCD 延迟枚举转换为 us
static uint16_t getSCDDelayUs(uint8_t regVal) {
    if (regVal > 3) return 0;
    return BQ76920_SCD_DELAY_US[regVal];
}

bool BQ76920::getOCDProtection(float* threshold_mV, uint16_t* delay_ms) {
    if (!threshold_mV || !delay_ms) return false;

    uint8_t protect1 = 0;
    uint8_t protect2 = 0;

    // 读取 PROTECT1 (获取 RSNS 位) 和 PROTECT2 (获取 OCD 配置)
    if (!readRegister(PROTECT1_REG_ADDR, &protect1)) return false;
    if (!readRegister(PROTECT2_REG_ADDR, &protect2)) return false;

    // 解析 RSNS 位 (Bit 7 of PROTECT1)
    bool rsnsHigh = (protect1 & 0x80) != 0;

    // 解析 OCD 阈值索引 (Low 4 bits of PROTECT2)
    uint8_t threshIdx = protect2 & 0x0F;
    
    // 解析 OCD 延迟 (Bits 4-6 of PROTECT2)
    uint8_t delayIdx = (protect2 >> 4) & 0x07;

    // 计算结果
    *threshold_mV = getOCDThresholdFromIndex(threshIdx, rsnsHigh);
    *delay_ms = getOCDDelayMs(delayIdx);

    return true;
}

bool BQ76920::getSCDProtection(float* threshold_mV, uint16_t* delay_us) {
    if (!threshold_mV || !delay_us) return false;

    uint8_t protect1 = 0;

    // 读取 PROTECT1 (包含 SCD 配置和 RSNS 位)
    if (!readRegister(PROTECT1_REG_ADDR, &protect1)) return false;

    // 解析 RSNS 位 (Bit 7 of PROTECT1)
    bool rsnsHigh = (protect1 & 0x80) != 0;

    // 解析 SCD 阈值索引 (Bits 0-2 of PROTECT1)
    uint8_t threshIdx = protect1 & 0x07;
    
    // 解析 SCD 延迟 (Bits 3-4 of PROTECT1)
    uint8_t delayIdx = (protect1 >> 3) & 0x03;

    // 计算结果
    *threshold_mV = getSCDThresholdFromIndex(threshIdx, rsnsHigh);
    *delay_us = getSCDDelayUs(delayIdx);

    return true;
}

/**
 * @brief 读取系统故障状态
 * @return 状态寄存器值 (bit0:OCD, bit1:SCD, bit2:OV, bit3:UV, bit4:ALERT, bit5:XREADY, bit7:CC_READY)
 */
uint8_t BQ76920::getFaultStatus() {
    uint8_t status = 0;
    readRegister(STATUS_REG_ADDR, &status);
    return status;
}

/**
 * @brief 清除指定的故障标志位
 * @param mask 要清除的位掩码 (例如: STAT_OV | STAT_UV)
 * @return true 成功
 */
bool BQ76920::clearFaults(uint8_t mask) {
    // 写入 1 清除对应位
    return writeRegister(STATUS_REG_ADDR, mask);
}

/**
 * @brief 设置电池均衡 参数改为逻辑电芯掩码
 * @param cellMask  bitmask, bit0 对应 Cell1, bit4 对应 Cell5. (注意: 不能同时均衡相邻电芯)
 * @return true 成功
 */
bool BQ76920::setCellBalance(uint8_t cellMask) {
    // [新增] 将逻辑电芯掩码转换为硬件均衡位掩码
    uint8_t hwBalanceMask = 0;
    for (uint8_t i = 0; i < actual_cell_count; i++) {
        if (cellMask & (1 << i)) {
            hwBalanceMask |= getBalanceMaskForCell(i);
        }
    }
    
    // 检查相邻电芯均衡警告
    if (hwBalanceMask & (hwBalanceMask << 1)) {
        Serial.println(F("Warning: Adjacent cells balanced simultaneously!"));
    }
    
    return writeRegister(CELL_BALANCE_REG_ADDR, hwBalanceMask);
}

/**
 * @brief 检测负载是否存在
 * @return true 负载存在
 */
bool BQ76920::isLoadPresent() {
    uint8_t sys_ctrl1 = 0;
    if (readRegister(SYS_CTRL1_REG_ADDR, &sys_ctrl1)) {
        return (sys_ctrl1 & LOAD_PRESENT_BIT) != 0;
    }
    return false;
}

/**
 * @brief 进入 SHIP 模式 (低功耗)
 * 需要特定的写序列: 先写 0x02 再写 0x04 到 SYS_CTRL1
 * @return true 成功
 */
bool BQ76920::enterShipMode() {
    uint8_t sys_ctrl1 = 0;
    if (!readRegister(SYS_CTRL1_REG_ADDR, &sys_ctrl1)) return false;
    
    // 确保 ADC 和 Temp 已关闭 (可选，视需求而定)
    sys_ctrl1 &= ~(ADC_ENABLE_BIT | TEMP_SELECT_BIT);
    
    // 写入 SHIP 序列
    // 第一步: 写 0x02 (Bit 1)
    if (!writeRegister(SYS_CTRL1_REG_ADDR, 0x02)) return false;
    delay(1);
    // 第二步: 写 0x04 (Bit 2)
    if (!writeRegister(SYS_CTRL1_REG_ADDR, 0x04)) return false;
    
    return true;
}

// =============================================================================
// 库仑计与电量统计实现 (Coulomb Counter Implementation)
// 基于数据手册 Section 8.3.1.1.3
// LSB = 8.44 uV, Sampling Interval = 250 ms
// =============================================================================

// 构造函数初始化列表中需确保新增变量被初始化
// 请找到 BQ76920::BQ76920() 构造函数，修改如下：
/*
BQ76920::BQ76920() 
    : i2c(nullptr)
    , bq_gain(DEFAULT_GAIN_MV_PER_LSB)
    , bq_offset(DEFAULT_OFFSET_MV)
    , current_zero_drift(CURRENT_ZERO_DRIFT)
    , actual_cell_count(0) {          // 确保 existing 变量也初始化
}
*/

/**
 * @brief 检查库仑计就绪标志
 */
bool BQ76920::isCoulombCounterReady() {
    uint8_t status = 0;
    if (readRegister(STATUS_REG_ADDR, &status)) {
        return (status & STAT_CC_READY) != 0;
    }
    return false;
}
 
/**
 * @brief 清除库仑计就绪标志
 * 手册说明：向 SYS_STAT 的对应位写 1 即可清除
 */
bool BQ76920::clearCoulombCounterFlag() {
    return writeRegister(STATUS_REG_ADDR, STAT_CC_READY);
}

/**
 * @brief 读取原始库仑计值
 */
bool BQ76920::readCoulombCounterRaw(int16_t &rawValue) {
    uint16_t wordValue;
    // 使用 readRegisterWord 读取 16-bit 有符号数 (2's complement)
    // CURRENT_REG_HIGH 是起始地址，readRegisterWord 会连续读取两个字节
    if (readRegisterWord(CURRENT_REG_HIGH, &wordValue)) {
        // 将 16-bit 无符号数转换为有符号数 (2's complement)
        rawValue = static_cast<int16_t>(wordValue);
        return true;
    }
    return false;
}

// =============================================================================
// 调试接口实现 (Debug Interface Implementation)
// =============================================================================

/**
 * @brief 打印关键寄存器值到串口
 * 打印地址 0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B 的寄存器值
 */
void BQ76920::printRegisters() {
    const uint8_t registers[] = {
        0x01, // CELL_BALANCE
        0x04, // SYS_CTRL1
        0x05, // SYS_CTRL2
        0x06, // PROTECT1
        0x07, // PROTECT2
        0x08, // PROTECT3
        0x09, // OV_TRIP
        0x0A, // UV_TRIP
        0x0B  // CC_CFG
    };
    
    const char* regNames[] = {
        "CELL_BALANCE (0x01)",
        "SYS_CTRL1    (0x04)",
        "SYS_CTRL2    (0x05)",
        "PROTECT1     (0x06)",
        "PROTECT2     (0x07)",
        "PROTECT3     (0x08)",
        "OV_TRIP      (0x09)",
        "UV_TRIP      (0x0A)",
        "CC_CFG       (0x0B)"
    };
    
    Serial.println(F("=== BQ76920 Register Dump ==="));
    
    for (int i = 0; i < 9; i++) {
        uint8_t value = 0;
        if (readRegister(registers[i], &value)) {
            Serial.printf_P(PSTR("%s: 0x%02X (%03d)\r\n"), regNames[i], value, value);
        } else {
            Serial.printf_P(PSTR("%s: READ FAILED\r\n"), regNames[i]);
        }
    }
    
    Serial.println(F("==============================="));
}

// =============================================================================
// Total Voltage Measurement
// =============================================================================

/**
 * @brief 读取电池组总电压
 * 
 * 读取 VOLTAGE_ALL_ADDR (0x2A) 寄存器获取所有串联电池的总电压
 * 寄存器为 16-bit 值，LSB ≈ 1.532mV
 * 
 * @return uint16_t 总电压值 (mV), 失败时返回 0
 */
uint16_t BQ76920::getTotalVoltage_mV() {
    uint16_t adc_raw_word = 0;
    
    // 1. 使用原子函数读取双字节（含 CRC 校验）
    // VOLTAGE_ALL_ADDR (0x2A) 是高位寄存器地址
    if (!readRegisterWord(VOLTAGE_ALL_ADDR, &adc_raw_word)) {
        // 如果 CRC 校验失败或 I2C 通信失败，直接返回 0
        return 0;
    }
    
    // 2. 处理 ADC 原始值
    // 手册规定电压 ADC 为 16 位，取低 16 位即可 (0xFFFF = 1111 1111 1111 1111)
    const uint16_t adc_raw = adc_raw_word & 0xFFFF;
    
    // 3. 根据公式计算毫伏值：V = ADC * 1.532 mV/LSB
    // 使用定点运算避免浮点数：1.532 ≈ 1532/1000
    // 为了保持精度，先乘后除：voltage_mv = (adc_raw * 1532) / 1000
    const uint16_t voltage_mv = static_cast<uint16_t>((static_cast<uint32_t>(adc_raw) * 1532UL) / 1000UL);
    
    return voltage_mv;
}

bool BQ76920::readAllRegisters(uint8_t* values) {
    if (i2c == nullptr || values == nullptr) {
        return false;
    }
    
    // 定义所有需要读取的寄存器地址
    const uint8_t registers[] = {
        STATUS_REG_ADDR,         // 0x00
        CELL_BALANCE_REG_ADDR,   // 0x01
        SYS_CTRL1_REG_ADDR,      // 0x04
        SYS_CTRL2_REG_ADDR,      // 0x05
        PROTECT1_REG_ADDR,       // 0x06
        PROTECT2_REG_ADDR,       // 0x07
        PROTECT3_REG_ADDR,       // 0x08
        OV_TRIP_REG_ADDR,        // 0x09
        UV_TRIP_REG_ADDR,        // 0x0A
        CC_CFG_REG_ADDR          // 0x0B
    };
    
    const int num_registers = sizeof(registers) / sizeof(registers[0]);
    
    // 逐个读取寄存器
    for (int i = 0; i < num_registers; i++) {
        if (!readRegister(registers[i], &values[i])) {
            Serial.printf_P(PSTR("BQ76920: Failed to read register 0x%02X\n"), registers[i]);
            return false;
        }
    }

    // 添加增益和偏移值
    values[num_registers] = static_cast<uint8_t>(bq_gain * 1000.0f - 365.0f);
    values[num_registers + 1] = static_cast<uint8_t>(bq_offset);
    
    return true;
}
