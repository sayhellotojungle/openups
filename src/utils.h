#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 全局工具类 - 提供跨模块的通用工具函数
 * 
 * 设计原则：
 * - 所有方法均为静态方法，无需实例化
 * - 无内部状态，完全基于输入参数操作
 * - 最小化依赖，提高复用性
 */
class Utils {
public:
    /**
     * @brief 解析固件版本字符串
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
    static void parseFirmwareVersion(const char* versionTag, 
                                     char* firmware_version, size_t fw_len,
                                     char* hardware_version, size_t hw_len);
    
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
    static uint8_t applyGammaMapping(uint8_t percent, uint8_t max_value);

    // ============================================================
    // BQ24780S 寄存器解析函数 (16位寄存器)
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
    static int parseBQ24780sDischargeCurrent(uint16_t reg_value);

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
    static int parseBQ24780sInputCurrent(uint16_t reg_value);

    /**
     * @brief 解析 BQ24780S PROCHOT_OPTION1 寄存器 (0x3DH) 的 IDCHG 阈值
     * 
     * 位15-10: IDCHG 阈值 (6位, 0-32256mA, 步进512mA)
     * 
     * @param reg_value 16位寄存器值
     * @return IDCHG 阈值 (mA)
     */
    static int parseBQ24780sIDCHG(uint16_t reg_value);

    // ============================================================
    // BQ76920 寄存器解析函数 (8位寄存器)
    // ============================================================

    /**
     * @brief 解析 BQ76920 PROTECT1 寄存器 (0x06) 的 SCD 短路阈值
     * 
     * 8位寄存器：
     * - 位7: RSNS 量程倍增 (0=低量程, 1=高量程x2)
     * - 位2-0: SCD_THRESH (3位阈值)
     * 
     * 参考 bq76920.cpp 中 setSCDProtection/getSCDProtection 实现：
     * - 写入：current_mA * RSENSE_VALUE (0.005) → mV → 查找表 → 寄存器
     * - 读取：寄存器 → 查找表 → mV → / RSENSE_VALUE → mA
     * 
     * @param reg_value 8位寄存器值
     * @return SCD 阈值 (mA)
     */
    static int parseBQ76920Protect1(uint8_t reg_value);

    /**
     * @brief 解析 BQ76920 PROTECT2 寄存器 (0x07) 的 OCD 过流阈值
     * 
     * 8位寄存器：
     * - 位3-0: OCD_THRESH (4位阈值)
     * 
     * 参考 bq76920.cpp 中 setOCDProtection/getOCDProtection 实现：
     * - 写入：current_mA * RSENSE_VALUE (0.005) → mV → 查找表 → 寄存器
     * - 读取：寄存器 → 查找表 → mV → / RSENSE_VALUE → mA
     * 
     * @param reg_value PROTECT2 寄存器值 (8位)
     * @param protect1_reg PROTECT1 寄存器值 (8位)，用于提取位7的 RSNS 量程
     * @return OCD 阈值 (mA)
     */
    static int parseBQ76920Protect2(uint8_t reg_value, uint8_t protect1_reg);

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
    static float parseBQ76920OvTrip(uint8_t reg_value, uint8_t gain_reg, uint8_t offset_reg);

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
    static float parseBQ76920UvTrip(uint8_t reg_value, uint8_t gain_reg, uint8_t offset_reg);
};

#endif // UTILS_H