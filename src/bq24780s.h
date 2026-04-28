#ifndef BQ24780S_H
#define BQ24780S_H

#include <stdint.h>
#include <limits.h>
#include "i2c_interface.h"

// 前向声明HardwareInterface类
class HardwareInterface;

namespace BQ24780SConst {
    // ==================== 设备基本信息 ====================
   constexpr uint8_t ADDRESS = 0x09;  ///< I2C 地址
    
    // ==================== 电流检测参数 ====================
    namespace Current {
       constexpr uint16_t SENSE_RESISTOR = 10;         ///< 电流检测电阻 (毫欧) - 10mΩ
        // 充电电流相关常量 (基于 10mΩ电阻)，如果是 5mΩ，放大这些值即可
       constexpr float CHARGE_CURRENT_STEP = 64.0f;  ///< 充电电流步进值 (mA)
       constexpr float INPUT_CURRENT_STEP = 128.0f;  ///< 输入电流步进值 (mA)
       constexpr float DISCHARGE_CURRENT_STEP = 512.0f; ///< 放电电流步进值 (mA)
       constexpr uint16_t CHARGE_CURRENT_MAX = 8128; ///< 最大充电电流 (mA) - 基于 7 位设置
       constexpr uint16_t INPUT_CURRENT_MAX = 8064;  ///< 最大输入电流 (mA) - 基于 6 位设置 (63×128=8064mA)
       constexpr uint16_t DISCHARGE_CURRENT_MAX = 20000; ///< 最大放电电流 (mA) - 基于 6 位设置最大电流为 32256，为了安全，手动限制为 20A
    }
    
    // ==================== 增益配置枚举 ====================
    /**
     * @brief IADP 增益配置枚举
     * Bit 4 of CHARGE_OPTION0 (0x12)
     */
    enum class IadpGain : uint8_t {
        Gain20x = 0,  ///< 20 倍增益
        Gain40x = 1   ///< 40 倍增益
    };
    
    /**
     * @brief IDCHG 增益配置枚举
     * Bit 3 of CHARGE_OPTION0 (0x12)
     */
    enum class IdchgGain : uint8_t {
        Gain8x = 0,   ///< 8 倍增益
        Gain16x = 1   ///< 16 倍增益
    };
    
    // ==================== 电压设置参数 ====================
    namespace Voltage {
       constexpr float CHARGE_VOLTAGE_STEP = 16.0f;  ///< 充电电压步进值 (mV)
       constexpr uint16_t CHARGE_VOLTAGE_MAX = 12600; ///< 最大充电电压 (mV) - 基于 11 位设置的理论值为 32752，但为了安全，手动限制为 12.6v
    }
    
    // ==================== 功率监测参数 ====================
    namespace Power {
       constexpr uint32_t PMON_RESISTANCE = 14300;         ///< PMON 外接电阻 (欧姆) - 14.3kΩ
       constexpr uint32_t PMON_SCALE_FACTOR = 1;           ///< PMON 比例因子 (μA/W) - 1μA/W 
    }
    
    // ==================== 寄存器地址 ====================
    namespace Registers {
        // 控制寄存器
       constexpr uint8_t CHARGE_OPTION0 = 0x12;      ///< Charge Options Control 0
                                                    ///< Bit 4: IADP gain (0=20x, 1=40x)
                                                    ///< Bit 3: IDCHG gain (0=8x, 1=16x)
                                                    ///< Bit 0: CHRG_INHIBIT (0=enable, 1=disable)
       constexpr uint8_t CHARGE_OPTION1 = 0x3B;      ///< Charge Options Control 1
                                                    ///< Bit 15: Battery UV threshold (0=2.8V, 1=3.0V)
                                                    ///< Bit 11: EN_IDCHG (1=enable IDCHG output)
                                                    ///< Bit 10: EN_PMON (1=enable PMON output)
       constexpr uint8_t CHARGE_OPTION2 = 0x38;      ///< Charge Options Control 2
       constexpr uint8_t CHARGE_OPTION3 = 0x37;      ///< Charge Options Control 3
                                                    ///< Bit 15: EN_IDCHG_REG (1=enable discharge regulation)
                                                    ///< Bit 2: EN_BOOST (0=Disable hybrid power boost, 1=Enable)
       constexpr uint8_t PROCHOT_OPTION0 = 0x3C;     ///< PROCHOT Options Control 0
       constexpr uint8_t PROCHOT_OPTION1 = 0x3D;     ///< PROCHOT Options Control 1
       constexpr uint8_t PROCHOT_STATUS = 0x3A;      ///< PROCHOT status (只读)
        
        // 控制寄存器
       constexpr uint8_t CHARGE_CURRENT = 0x14;      ///< 7-bit Charge Current Setting
       constexpr uint8_t CHARGE_VOLTAGE = 0x15;      ///< 11-bit Charge Voltage Setting
       constexpr uint8_t DISCHARGE_CURRENT = 0x39;   ///< 6-bit Discharge Current Setting
       constexpr uint8_t INPUT_CURRENT = 0x3F;       ///< 6-bit Input Current Setting
    }
}


/**
 * @brief BQ24780S寄存器数据结构
 */
struct BQ24780SRegisters {
    // 配置寄存器
    uint16_t charge_option0 = 0;
    uint16_t charge_option1 = 0;
    uint16_t charge_option2 = 0;
    uint16_t charge_option3 = 0;
    uint16_t prochot_option0 = 0;
    uint16_t prochot_option1 = 0;
    uint16_t prochot_status = 0;
    
    // 控制寄存器
    uint16_t charge_current = 0;
    uint16_t charge_voltage = 0;
    uint16_t discharge_current = 0;
    uint16_t input_current = 0;
    
    // 时间戳（统一管理所有寄存器的缓存时间）
    unsigned long last_update = 0;
};

/**
 * @brief BQ24780S 芯片数据结构
 */
struct BQ24780SData {
    // 寄存器数据
    BQ24780SRegisters registers;
    
};

/**
 * @brief BQ24780S电源管理芯片驱动类
 * 
 * 提供完整的BQ24780S芯片控制和监测功能
 */
class BQ24780S {
public:
    /**
     * @brief 构造函数 - 直接传入 I2C 接口
     * @param i2c I2C 接口指针
     */
    BQ24780S(I2CInterface* i2c);
    
    
    /**
     * @brief 初始化设备
     * @return true 初始化成功，false 初始化失败
     */
    bool begin();
    
    /**
     * @brief 应用硬件配置（增益、监测使能等）
     * @param config 配置结构体引用
     * @return true 配置成功，false 配置失败
     */
    struct HardwareConfig {
        BQ24780SConst::IadpGain iadp_gain = BQ24780SConst::IadpGain::Gain20x;
        BQ24780SConst::IdchgGain idchg_gain = BQ24780SConst::IdchgGain::Gain16x;
        bool enable_idchg_monitor = true;
        bool enable_pmon_monitor = true;
        bool enable_discharge_regulation = true;
        bool enable_hybrid_boost = true;
        
        // 电流限制配置 (mA)
        uint16_t input_current_limit = 20000;         // 输入电流限制 (默认 20A)
        uint16_t discharge_current_max = 15000;       // 最大放电电流 (默认 15A)
    };
    
    bool applyHardwareConfig(const HardwareConfig& config);
    
    // ==================== 寄存器读写基础函数 ====================
    /**
     * @brief 读取寄存器值
     * @param reg 寄存器地址
     * @param value 存储读取值的指针
     * @return true 读取成功, false 读取失败
     */
    bool readRegister(uint8_t reg, uint16_t *value);
    
    /**
     * @brief 写入寄存器值
     * @param reg 寄存器地址
     * @param value 要写入的值
     * @return true 写入成功, false 写入失败
     */
    bool writeRegister(uint8_t reg, uint16_t value);
    
    // ==================== 充电控制功能 ====================
    /**
     * @brief 设置充电电流
     * @param current_mA 充电电流值(mA)，基于10mΩ检测电阻
     * @return true 设置成功, false 设置失败
     */
    bool setChargeCurrent(uint16_t current_mA);
    
    /**
     * @brief 读取当前充电电流设置
     * @param current_mA 存储读取的充电电流值(mA)
     * @return true 读取成功, false 读取失败
     */
    bool getChargeCurrent(uint16_t* current_mA);
    
    /**
     * @brief 设置充电电压
     * @param voltage_mV 充电电压值(mV)
     * @return true 设置成功, false 设置失败
     */
    bool setChargeVoltage(uint16_t voltage_mV);
    
    /**
     * @brief 读取当前充电电压设置
     * @param voltage_mV 存储读取的充电电压值(mV)
     * @return true 读取成功, false 读取失败
     */
    bool getChargeVoltage(uint16_t* voltage_mV);
    
    /**
     * @brief 启用/禁用充电功能
     * @param enable true启用充电，false禁用充电
     * @return true 设置成功, false 设置失败
     */
    bool setCharging(bool enable);
    
    /**
     * @brief 检查充电是否启用
     * @return true 充电已启用, false 充电已禁用
     */
    bool isChargingEnabled();
    
    /**
     * @brief 设置输入电流限制
     * @param current_mA 输入电流限制值(mA)，基于10mΩ检测电阻
     * @return true 设置成功, false 设置失败
     */
    bool setInputCurrentLimit(uint16_t current_mA);
    
    /**
     * @brief 读取当前输入电流限制
     * @param current_mA 存储读取的输入电流限制值(mA)
     * @return true 读取成功, false 读取失败
     */
    bool getInputCurrentLimit(uint16_t* current_mA);
    
    /**
     * @brief 设置放电电流限制
     * @param current_mA 放电电流限制值(mA)，基于10mΩ检测电阻
     * @return true 设置成功, false 设置失败
     */
    bool setDischargeCurrentLimit(uint16_t current_mA);
    
    /**
     * @brief 读取当前放电电流限制
     * @param current_mA 存储读取的放电电流限制值(mA)
     * @return true 读取成功, false 读取失败
     */
    bool getDischargeCurrentLimit(uint16_t* current_mA);
    
    /**
     * @brief 检查设备是否连接
     * @return true 设备已连接，false 设备未连接
     */
    bool isConnected();
    
    // ==================== 增益配置功能 ====================
    /**
     * @brief 设置 IADP 增益
     * @param gain IADP 增益配置枚举值
     * @return true 设置成功，false 设置失败
     * @note 修改 CHARGE_OPTION0 (0x12) Bit 4
     */
    bool setIadpGain(BQ24780SConst::IadpGain gain);
    
    /**
     * @brief 设置 IDCHG 增益
     * @param gain IDCHG 增益配置枚举值
     * @return true 设置成功，false 设置失败
     * @note 修改 CHARGE_OPTION0 (0x12) Bit 3
     */
    bool setIdchgGain(BQ24780SConst::IdchgGain gain);
    
    /**
     * @brief 获取当前 IADP 增益配置
     * @return IADP 增益枚举值
     */
    BQ24780SConst::IadpGain getIadpGain() const { return current_iadp_gain_; }
    
    /**
     * @brief 获取当前 IDCHG 增益配置
     * @return IDCHG 增益枚举值
     */
    BQ24780SConst::IdchgGain getIdchgGain() const { return current_idchg_gain_; }
    
    // ==================== 模拟信号计算功能（Public） ====================
    /**
     * @brief 根据 IADP 电压计算输入电流
     * @param voltage_mV IADP 引脚电压值 (mV)
     * @return 输入电流值 (mA)
     */
    uint16_t calculateIadpCurrent(uint16_t voltage_mV) const;
    
    /**
     * @brief 根据 IDCHG 电压计算放电电流
     * @param voltage_mV IDCHG 引脚电压值 (mV)
     * @return 放电电流值 (mA)
     */
    uint16_t calculateIdchgCurrent(uint16_t voltage_mV) const;
    
    /**
     * @brief 根据 PMON 电压计算输出功率
     * @param voltage_mV PMON 引脚电压值 (mV)
     * @return 输出功率值 (mW)
     * @note 该函数不需要 data 参数，因为 PMON 计算使用固定参数
     */
    uint32_t calculatePmonPower(uint16_t voltage_mV) const;

    /**
     * @brief 启用/禁用混合供电增强模式
     * @param enable true 启用混合模式增强，false 禁用
     * @return true 设置成功，false 设置失败
     * @note 修改 CHARGE_OPTION3 (0x37) Bit 2 (EN_BOOST)
     */
    bool setHybridPowerBoost(bool enable);

    // ==================== 状态读取与故障管理 ====================
    /**
     * @brief 读取 PROCHOT 状态寄存器
     * @param status 存储状态值的指针（bit0-bit6 分别表示不同故障源）
     * @return true 读取成功，false 读取失败
     * 
     * @note PROCHOT_STATUS (0x3A) 寄存器位定义：
     *   - Bit 0: ACOK 事件触发
     *   - Bit 1: BATPRES 事件触发
     *   - Bit 2: VSYS 欠压事件触发
     *   - Bit 3: IDCHG 过流事件触发
     *   - Bit 4: INOM 过流事件触发
     *   - Bit 5: ICRIT 严重过流事件触发
     *   - Bit 6: 独立比较器事件触发
     * 
     * @note 该寄存器为只读，在以下情况下会自动清零：
     *   - 主机首次读取后
     *   - PROCHOT 信号变低开始新的脉冲
     */
    bool readProchotStatus(uint16_t* status);
    
    /**
     * @brief 直接读取充电参数寄存器原始值并打印到串口（用于调试）
     * @return true 读取成功，false 读取失败
     * 
     * @note 直接读取以下寄存器，不经过缓存：
     *   - CHARGE_VOLTAGE (0x15): 充电电压设置寄存器
     *   - CHARGE_CURRENT (0x14): 充电电流设置寄存器
     *   - CHARGE_OPTION0 (0x12): 充电控制选项寄存器（包含充电使能位）
     */
    bool debugReadChargeRegisters();

    /**
     * @brief 读取所有寄存器的原始值
     * @param values 存储寄存器值的数组指针（至少需要11个元素）
     * @return true 读取成功，false 读取失败
     *
     * @note 数组顺序：
     *   [0]  CHARGE_OPTION0 (0x12)
     *   [1]  CHARGE_OPTION1 (0x3B)
     *   [2]  CHARGE_OPTION2 (0x38)
     *   [3]  CHARGE_OPTION3 (0x37)
     *   [4]  PROCHOT_OPTION0 (0x3C)
     *   [5]  PROCHOT_OPTION1 (0x3D)
     *   [6]  PROCHOT_STATUS (0x3A)
     *   [7]  CHARGE_CURRENT (0x14)
     *   [8]  CHARGE_VOLTAGE (0x15)
     *   [9]  DISCHARGE_CURRENT (0x39)
     *   [10] INPUT_CURRENT (0x3F)
     */
    bool readAllRegisters(uint16_t* values);

private:
    I2CInterface* i2c;                 ///< I2C 接口指针
    bool initialized_;                 ///< 初始化状态标志
    
    // 当前硬件配置状态
    BQ24780SConst::IadpGain current_iadp_gain_ = BQ24780SConst::IadpGain::Gain20x;      ///< 当前 IADP 增益配置
    BQ24780SConst::IdchgGain current_idchg_gain_ = BQ24780SConst::IdchgGain::Gain16x;   ///< 当前 IDCHG 增益配置
    
    // 连接状态缓存
    bool last_connection_status_ = false;           ///< 上次连接检测结果
    unsigned long last_connection_check_time_ = 0;  ///< 上次连接检测时间戳
    
    
    // 新增辅助配置函数
    bool configureMonitoringOutputs(bool enable_idchg, bool enable_pmon);
    bool configureDischargeRegulation(bool enable);

};

#endif