#include "utils.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/**
 * @brief 解析固件版本字符串并填充输出缓冲区
 * 
 * 格式："SIG:<PROJECT_NAME>:VER:<VERSION>"
 * 示例："SIG:OPENUPS-ESP32S3:VER:1.0.2"
 * 
 * @param versionTag 固件版本标签字符串
 * @param firmware_version 输出缓冲区：项目名称/固件版本
 * @param fw_len 固件版本缓冲区长度
 * @param hardware_version 输出缓冲区：硬件版本号
 * @param hw_len 硬件版本缓冲区长度
 */
void Utils::parseFirmwareVersion(const char* versionTag, 
                                 char* firmware_version, size_t fw_len,
                                 char* hardware_version, size_t hw_len) {
    if (versionTag == nullptr || strlen(versionTag) == 0) {
        strncpy(firmware_version, "UNKNOWN", fw_len - 1);
        firmware_version[fw_len - 1] = '\0';
        strncpy(hardware_version, "UNKNOWN", hw_len - 1);
        hardware_version[hw_len - 1] = '\0';
        return;
    }
    
    // 查找分隔符位置
    const char* sigPos = strstr(versionTag, "SIG:");
    const char* verPos = strstr(versionTag, ":VER:");
    
    if (sigPos == nullptr || verPos == nullptr) {
        strncpy(firmware_version, "INVALID", fw_len - 1);
        firmware_version[fw_len - 1] = '\0';
        strncpy(hardware_version, "INVALID", hw_len - 1);
        hardware_version[hw_len - 1] = '\0';
        return;
    }
    
    // 提取项目名称 (hardware_version)
    // 从 "SIG:" 之后到 ":VER:" 之前
    const char* nameStart = sigPos + 4;  // 跳过 "SIG:"
    size_t nameLen = verPos - nameStart;
    
    if (nameLen > 0 && nameLen < hw_len) {
        strncpy(hardware_version, nameStart, nameLen);
        hardware_version[nameLen] = '\0';
    } else {
        strncpy(hardware_version, "UNKNOWN", hw_len - 1);
        hardware_version[hw_len - 1] = '\0';
    }
    
    // 提取版本号 (firmware_version)
    // 从 ":VER:" 之后到字符串末尾
    const char* versionStart = verPos + 5;  // 跳过 ":VER:"
    strncpy(firmware_version, versionStart, fw_len - 1);
    firmware_version[fw_len - 1] = '\0';
}

/**
 * @brief Gamma 映射函数 - 将线性百分比值转换为符合人眼感知的非线性值
 * 
 * 使用 Gamma 校正曲线（gamma=1.6）和偏移量（offset=15）进行映射，
 * 使 LED 亮度或蜂鸣器音量等输出更符合人眼/人耳的感知特性。
 * 
 * @param percent 输入百分比值 (0-100)
 * @param max_value 输出最大值
 * @return 映射后的值 (0-max_value)
 * 
 * @note 当 percent=0 时返回 0，percent>=100 时返回 max_value
 * @note 保证输出至少为 1（当 percent>0 时），避免完全关闭
 */
uint8_t Utils::applyGammaMapping(uint8_t percent, uint8_t max_value) {
    if (percent == 0) return 0;
    if (percent >= 100) return max_value;

    const float gamma = 1.6f;
    const float offset = 15.0f;
    
    float normalized_in = static_cast<float>(percent) / 100.0f;
    float shifted_in = normalized_in + (offset / 100.0f);
    float base_shift = offset / 100.0f;
    
    float numerator = powf(shifted_in, gamma) - powf(base_shift, gamma);
    float denominator = powf(1.0f + base_shift, gamma) - powf(base_shift, gamma);
    float normalized_out = numerator / denominator;

    uint8_t result = static_cast<uint8_t>(normalized_out * max_value + 0.5f);
    if (result > max_value) result = max_value;
    if (result == 0 && percent > 0) result = 1;

    return result;
}

// ============================================================
// BQ24780S 寄存器解析函数实现 (16位寄存器)
// ============================================================

/**
 * @brief 解析 BQ24780S DISCHARGE_CURRENT 寄存器 (0x39H)
 * 
 * 6位寄存器 (位9-14)，使用 10-mΩ 采样电阻
 * - 位9: 512mA, 位10: 1024mA, 位11: 2048mA
 * - 位12: 4096mA, 位13: 8192mA, 位14: 16384mA
 * 
 * @param reg_value 16位寄存器值
 * @return 放电电流 (mA)
 */
int Utils::parseBQ24780sDischargeCurrent(uint16_t reg_value) {
    // 提取位9-14 (6位放电电流设置)
    int current_ma = 0;
    if (reg_value & (1 << 9))  current_ma += 512;
    if (reg_value & (1 << 10)) current_ma += 1024;
    if (reg_value & (1 << 11)) current_ma += 2048;
    if (reg_value & (1 << 12)) current_ma += 4096;
    if (reg_value & (1 << 13)) current_ma += 8192;
    if (reg_value & (1 << 14)) current_ma += 16384;
    
    return current_ma;
}

/**
 * @brief 解析 BQ24780S INPUT_CURRENT 寄存器 (0x3FH)
 * 
 * 6位寄存器 (位7-12)，使用 10-mΩ 采样电阻
 * - 位7: 128mA, 位8: 256mA, 位9: 512mA
 * - 位10: 1024mA, 位11: 2048mA, 位12: 4096mA
 * 
 * @param reg_value 16位寄存器值
 * @return 输入电流 (mA)
 */
int Utils::parseBQ24780sInputCurrent(uint16_t reg_value) {
    // 提取位7-12 (6位输入电流设置)
    int current_ma = 0;
    if (reg_value & (1 << 7))  current_ma += 128;
    if (reg_value & (1 << 8))  current_ma += 256;
    if (reg_value & (1 << 9))  current_ma += 512;
    if (reg_value & (1 << 10)) current_ma += 1024;
    if (reg_value & (1 << 11)) current_ma += 2048;
    if (reg_value & (1 << 12)) current_ma += 4096;
    
    return current_ma;
}
// ============================================================
// BQ76920 寄存器解析函数实现 (8位寄存器)
// ============================================================

/**
 * @brief 解析 BQ76920 PROTECT1 寄存器 (0x06) 的 SCD 短路阈值
 * 
 * 参考 bq76920.cpp 中 setSCDProtection/getSCDProtection 实现：
 * - 写入：current_mA * RSENSE_VALUE (0.005) → mV → 查找表 → 寄存器
 * - 读取：寄存器 → 查找表 → mV → / RSENSE_VALUE → mA
 * 
 * 8位寄存器：
 * - 位7: RSNS 量程倍增 (0=低量程, 1=高量程x2)
 * - 位2-0: SCD_THRESH (3位阈值)
 * 
 * @param reg_value 8位寄存器值
 * @return SCD 阈值 (mA)
 */
int Utils::parseBQ76920Protect1(uint8_t reg_value) {
    // 位7: RSNS 量程倍增
    int rsns = (reg_value >> 7) & 0x01;
    
    // 位2-0: SCD_THRESH (3位，索引 0-7)
    uint8_t scd_thresh = reg_value & 0x07;
    
    // 低量程阈值表 (RSNS=0): mV 值
    const int scd_threshold_low_mv[] = {22, 33, 44, 56, 67, 78, 89, 100};
    // 高量程阈值表 (RSNS=1): mV 值
    const int scd_threshold_high_mv[] = {44, 67, 89, 111, 133, 155, 178, 200};
    
    int threshold_mv;
    if (rsns == 0) {
        threshold_mv = scd_threshold_low_mv[scd_thresh];
    } else {
        threshold_mv = scd_threshold_high_mv[scd_thresh];
    }
    
    // 逆向计算：mV / RSENSE_VALUE (0.005) = mA
    // 即：mV * 200 = mA
    return threshold_mv * 200;
}

/**
 * @brief 解析 BQ76920 PROTECT2 寄存器 (0x07) 的 OCD 过流阈值
 * 
 * 参考 bq76920.cpp 中 setOCDProtection/getOCDProtection 实现：
 * - 写入：current_mA * RSENSE_VALUE (0.005) → mV → 查找表 → 寄存器
 * - 读取：寄存器 → 查找表 → mV → / RSENSE_VALUE → mA
 * 
 * 8位寄存器：
 * - 位3-0: OCD_THRESH (4位阈值)
 * 
 * @param reg_value PROTECT2 寄存器值 (8位)
 * @param protect1_reg PROTECT1 寄存器值 (8位)，用于提取位7的 RSNS 量程
 * @return OCD 阈值 (mA)
 */
int Utils::parseBQ76920Protect2(uint8_t reg_value, uint8_t protect1_reg) {
    // 从 PROTECT1 提取 RSNS (位7)
    int rsns = (protect1_reg >> 7) & 0x01;
    
    // 位3-0: OCD_THRESH (4位，索引 0-15)
    uint8_t ocd_thresh = reg_value & 0x0F;
    
    // 低量程阈值表 (RSNS=0): mV 值
    const int ocd_threshold_low_mv[] = {8, 11, 14, 17, 19, 22, 25, 28, 31, 33, 36, 39, 42, 44, 47, 50};
    // 高量程阈值表 (RSNS=1): mV 值
    const int ocd_threshold_high_mv[] = {17, 22, 28, 33, 39, 44, 50, 56, 61, 67, 72, 78, 83, 89, 94, 100};
    
    int threshold_mv;
    if (rsns == 0) {
        threshold_mv = ocd_threshold_low_mv[ocd_thresh];
    } else {
        threshold_mv = ocd_threshold_high_mv[ocd_thresh];
    }
    
    // 逆向计算：mV / RSENSE_VALUE (0.005) = mA
    // 即：mV * 200 = mA
    return threshold_mv * 200;
}

/**
 * @brief 解析 BQ76920 OV_TRIP 寄存器 (0x09) 的过压阈值电压
 * 
 * 参考 bq76920.cpp 中 getOVThreshold() 实现：
 * - fullAdc = (reg_value << 4) | 0x08 | 0x2000
 * - voltage_mV = fullAdc * bq_gain + bq_offset
 * 
 * @param reg_value OV_TRIP 寄存器值 (8位)
 * @param gain_reg 已组合的5位gain值 (0-31)，bq_gain = (365000 + gain_reg) / 1000.0f
 * @param offset_reg ADCOFFSET 寄存器 (0x51)，8位有符号数 (mV)
 * @return 过压阈值电压 (mV)
 */
float Utils::parseBQ76920OvTrip(uint8_t reg_value, uint8_t gain_reg, uint8_t offset_reg) {
    // gain_reg 已经是组合后的5位值 (0-31)，bq_gain = 365 + gain_reg (μV/LSB)
    float bq_gain_uv = 365.0f + gain_reg;  // 保持为 μV/LSB
    
    // offset_reg 为 8-bit 有符号数 (mV)
    int bq_offset = static_cast<int8_t>(offset_reg);
    
    // 构造完整的 ADC 值 (参考 getOVThreshold 实现)
    // fullAdc = (reg_value << 4) | 0x08 | 0x2000
    uint16_t fullAdc = ((uint16_t)reg_value << 4) | 0x08;
    fullAdc |= 0x2000;  // 设置高2位为 10
    
    // 计算电压: voltage_mV = (fullAdc * bq_gain_uv) / 1000.0f + bq_offset
    float voltage_mV = (fullAdc * bq_gain_uv) / 1000.0f + bq_offset;
    
    return voltage_mV;
}

/**
 * @brief 解析 BQ76920 UV_TRIP 寄存器 (0x0A) 的欠压阈值电压
 * 
 * 参考 bq76920.cpp 中 getUVThreshold() 实现：
 * - fullAdc = (reg_value << 4) | 0x1000
 * - voltage_mV = fullAdc * bq_gain + bq_offset
 * 
 * @param reg_value UV_TRIP 寄存器值 (8位)
 * @param gain_reg 已组合的5位gain值 (0-31)，bq_gain = (365000 + gain_reg) / 1000.0f
 * @param offset_reg ADCOFFSET 寄存器 (0x51)，8位有符号数 (mV)
 * @return 欠压阈值电压 (mV)
 */
float Utils::parseBQ76920UvTrip(uint8_t reg_value, uint8_t gain_reg, uint8_t offset_reg) {
    // gain_reg 已经是组合后的5位值 (0-31)，bq_gain = 365 + gain_reg (μV/LSB)
    float bq_gain_uv = 365.0f + gain_reg;  // 保持为 μV/LSB
    
    // offset_reg 为 8-bit 有符号数 (mV)
    int bq_offset = static_cast<int8_t>(offset_reg);
    
    // 构造完整的 ADC 值 (参考 getUVThreshold 实现)
    // fullAdc = (reg_value << 4) | 0x1000
    uint16_t fullAdc = ((uint16_t)reg_value << 4);
    fullAdc |= 0x1000;  // 设置高2位为 01
    
    // 计算电压: voltage_mV = (fullAdc * bq_gain_uv) / 1000.0f + bq_offset
    float voltage_mV = (fullAdc * bq_gain_uv) / 1000.0f + bq_offset;
    
    return voltage_mV;
}
