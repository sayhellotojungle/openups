#ifndef BQ76920_H
#define BQ76920_H

#include <stdint.h>
#include "i2c_interface.h"

// =============================================================================
// BQ76920 设备配置
// =============================================================================

// 设备基本信息
#define BQ76920_ADDRESS         0x08    ///< BQ76920 I2C地址
#define MAX_CELL_COUNT          5       ///< 最大电池单元数

// 默认校准参数
#define DEFAULT_GAIN_MV_PER_LSB 0.380f  ///< 默认增益值 (mV/LSB)
#define DEFAULT_OFFSET_MV       0       ///< [修正] 默认偏移值 (mV)，原为99，现改为0以避免误差

#define RSENSE_VALUE           0.005f  // 检测电阻阻值5m

// =============================================================================
// 寄存器地址定义
// =============================================================================

// 系统控制寄存器
#define STATUS_REG_ADDR         0x00    ///< 状态寄存器
#define CELL_BALANCE_REG_ADDR   0x01    ///< 电池均衡寄存器
#define SYS_CTRL1_REG_ADDR      0x04    ///< 系统控制寄存器1
#define SYS_CTRL2_REG_ADDR      0x05    ///< 系统控制寄存器2
#define PROTECT1_REG_ADDR       0x06    ///< 短路保护寄存器1
#define PROTECT2_REG_ADDR       0x07    ///< 过流保护寄存器2
#define PROTECT3_REG_ADDR       0x08    ///< 压延保护延时
#define OV_TRIP_REG_ADDR        0x09    ///< 过压值寄存器
#define UV_TRIP_REG_ADDR        0x0A    ///< 欠压值寄存器

// --- 状态位定义 (SYS_STAT 0x00) ---
#define STAT_CC_READY       (1 << 7)
#define STAT_DEVICE_XREADY  (1 << 5)
#define STAT_OVRD_ALERT     (1 << 4) ///< 外部二级保护触发
#define STAT_UV             (1 << 3)
#define STAT_OV             (1 << 2)
#define STAT_SCD            (1 << 1)
#define STAT_OCD            (1 << 0)

// 控制位定义
#define ADC_ENABLE_BIT          0x10    ///< ADC使能位 SYS_CTRL1 bit4
#define TEMP_SELECT_BIT         0x08    ///< 温度选择位 SYS_CTRL1 bit3
#define CC_ENABLE_BIT           0x40    ///< 库仑计使能位 SYS_CTRL2 bit6
#define CHARGE_ENABLE_BIT       0x01    ///< 充电使能位 SYS_CTRL2 bit0
#define DISCHARGE_ENABLE_BIT    0x02    ///< 放电使能位 SYS_CTRL2 bit1
#define LOAD_PRESENT_BIT        0x04    ///< 负载检测位 SYS_CTRL1 bit2

// 电压测量寄存器
#define VOLTAGE_BASE_ADDR       0x0C    ///< 电压寄存器基地址
#define VOLTAGE_REG_STEP        2       ///< 电压寄存器步长
#define VOLTAGE_HIGH_MASK       0x3F    ///< 电压高位掩码
#define VOLTAGE_ALL_ADDR        0x2A    ///< 所有电压寄存器地址高位 16bit

// 电流测量寄存器
#define CURRENT_REG_HIGH        0x32    ///< 电流高字节寄存器
#define CURRENT_REG_LOW         0x33    ///< 电流低字节寄存器
// [注意] CURRENT_ZERO_DRIFT 现在建议通过 calibrateCurrentZero() 动态获取，此处保留宏定义兼容旧代码
#define CURRENT_ZERO_DRIFT      1     ///< 电流零漂值 (默认值，建议校准后覆盖)
#define CURRENT_SCALE_FACTOR    (8.44f / 5.0f)  ///< 电流缩放因子 //与检测电阻RSENSE_VALUE相关

// 温度测量寄存器
#define TEMP_REG_HIGH           0x2C    ///< 温度高字节寄存器
#define TEMP_REG_LOW            0x2D    ///< 温度低字节寄存器
#define TEMP_HIGH_MASK          0x3F    ///< 温度高位掩码

// ADC校准寄存器
#define CC_CFG_REG_ADDR         0x0B    ///< CC配置寄存器
#define GAIN_REG1_ADDR          0x50    ///< 增益寄存器1
#define GAIN_REG2_ADDR          0x59    ///< 增益寄存器2
#define OFFSET_REG_ADDR         0x51    ///< 偏移寄存器

// ADC校准计算常量
#define BASE_GAIN_UV            365     ///< 基础增益值 (uV)
#define GAIN_MASK1              0x0C    ///< 增益掩码1
#define GAIN_MASK2              0xE0    ///< 增益掩码2
#define GAIN_SHIFT1             1       ///< 增益左移位数
#define GAIN_SHIFT2             5       ///< 增益右移位数
#define UV_TO_MV_FACTOR         1000.0f ///< uV转mV因子

// 温度计算常量
#define TEMP_TABLE_START        -10     ///< 表格起始温度
#define TEMP_TABLE_END          95      ///< 表格结束温度
#define TEMP_TABLE_SIZE         106     ///< 表格元素个数

// 10k b3950
const uint16_t TEMP_LOOKUP_TABLE[] = {
    7373, 7310, 7246, 7179, 7110, 7040, 6967, 6893, 6816, 6738, // -10 ~ -1°C
    6658, 6577, 6493, 6408, 6322, 6234, 6145, 6054, 5962, 5870, // 0 ~ 9°C
    5776, 5681, 5586, 5489, 5393, 5295, 5198, 5100, 5002, 4903, // 10 ~ 19°C
    4805, 4707, 4610, 4512, 4416, 4319, 4224, 4129, 4035, 3942, // 20 ~ 29°C
    3849, 3758, 3668, 3579, 3491, 3405, 3320, 3236, 3154, 3073, // 30 ~ 39°C
    2993, 2915, 2838, 2763, 2690, 2618, 2548, 2479, 2411, 2345, // 40 ~ 49°C
    2281, 2218, 2157, 2098, 2039, 1983, 1927, 1873, 1821, 1770, // 50 ~ 59°C
    1720, 1672, 1625, 1579, 1534, 1491, 1449, 1408, 1369, 1330, // 60 ~ 69°C
    1293, 1256, 1221, 1187, 1154, 1121, 1090, 1060, 1030, 1001, // 70 ~ 79°C
    974, 947, 921, 895, 871, 847, 824, 801, 779, 758, // 80 ~ 89°C
    738, 718, 698, 680, 662, 644, // 90 ~ 95°C
};

#define INVALID_TEMPERATURE     -99.0f  ///< 无效温度值


// 初始化参数
#define INIT_RETRY_COUNT        5       ///< 初始化重试次数
#define INIT_DELAY_MS           10      ///< 初始化延时(ms)
#define ADC_STABILIZE_DELAY_MS  100    ///< ADC稳定延时(ms)
#define CALIBRATION_RETRY_DELAY_MS 100  ///< 校准重试延时(ms)
#define CALIBRATION_MIN_GAIN    0.30f   ///< 最小增益阈值
#define CALIBRATION_OFFSET_MIN  -50     ///< 偏移最小值
#define CALIBRATION_OFFSET_MAX  50      ///< 偏移最大值
#define CALIBRATION_GAIN_MIN    0.35f   ///< 增益最小值
#define CALIBRATION_GAIN_MAX    0.40f   ///< 增益最大值
#define DEFAULT_CALIBRATION_GAIN 0.381f ///< 默认校准增益
#define GAIN_ADJUSTMENT_FACTOR  1.0f ///< 增益调整因子

/**
 * @brief 温度源选择
 */
typedef enum {
    TEMP_SOURCE_INTERNAL = 0, // 内部 Die 温度
    TEMP_SOURCE_EXTERNAL = 1  // 外部热敏电阻 (TS1)
} BMS_TempSource_t;

/**
 * @brief 过流/短路延迟时间枚举 (对应寄存器位)
 */
typedef enum {
    OCD_DELAY_8_MS    = 0,
    OCD_DELAY_20_MS   = 1,
    OCD_DELAY_40_MS   = 2,
    OCD_DELAY_80_MS   = 3,
    OCD_DELAY_160_MS  = 4,
    OCD_DELAY_320_MS  = 5,
    OCD_DELAY_640_MS  = 6,
    OCD_DELAY_1280_MS = 7
} BMS_OCDDelay_t;

typedef enum {
    SCD_DELAY_70_US  = 0,
    SCD_DELAY_100_US = 1,
    SCD_DELAY_200_US = 2,
    SCD_DELAY_400_US = 3
} BMS_SCDDelay_t;

typedef enum {
    OV_DELAY_1_S  = 0,
    OV_DELAY_2_S  = 1,
    OV_DELAY_4_S  = 2,
    OV_DELAY_8_S  = 3
} BMS_OVDelay_t;

typedef enum {
    UV_DELAY_1_S  = 0,
    UV_DELAY_4_S  = 1,
    UV_DELAY_8_S  = 2,
    UV_DELAY_16_S = 3
} BMS_UVDelay_t;

// OCD 阈值查找表 (单位: mV)
static constexpr float BQ76920_OCD_THRESH_HIGH[] = {17, 22, 28, 33, 39, 44, 50, 56, 61, 67, 72, 78, 83, 89, 94, 100};
static constexpr float BQ76920_OCD_THRESH_LOW[]  = {8, 11, 14, 17, 19, 22, 25, 28, 31, 33, 36, 39, 42, 44, 47, 50};

// SCD 阈值查找表 (单位: mV)
static constexpr float BQ76920_SCD_THRESH_HIGH[] = {44, 67, 89, 111, 133, 155, 178, 200};
static constexpr float BQ76920_SCD_THRESH_LOW[]  = {22, 33, 44, 56, 67, 78, 89, 100};

// OCD 延迟查找表 (单位: ms)
static constexpr uint16_t BQ76920_OCD_DELAY_MS[] = {8, 20, 40, 80, 160, 320, 640, 1280};

// SCD 延迟查找表 (单位: us)
static constexpr uint16_t BQ76920_SCD_DELAY_US[] = {70, 100, 200, 400};

/**
 * @brief BQ76920芯片数据结构
 */
struct BQ76920Data {
    // 单体电压（mV）
    uint16_t cell_voltage[MAX_CELL_COUNT];
    
    // 电流（mA）
    int16_t current;
    
    // 温度（°C）
    float temperature;
    
    // ADC校准参数
    float gain;
    int8_t offset;
    
    // MOS管状态
    bool chg_enabled;
    bool dsg_enabled;
    
    // 更新时间戳
    unsigned long last_update;

    // 实际电池串数
    uint8_t actual_cell_count;
};

/**
 * @brief 简化的BQ76920初始化配置结构体
 * 只包含芯片初始化必需的参数
 */
struct BQ76920_InitConfig {
    uint8_t cell_count;                   // 电池串数 (3-5)
    uint16_t cell_ov_threshold;           // 单节过压阈值 (mV)
    uint16_t cell_uv_threshold;           // 单节欠压阈值 (mV)
    BMS_OVDelay_t ov_delay;               // 过压延时
    BMS_UVDelay_t uv_delay;               // 欠压延时
    int16_t max_discharge_current;        // 最大放电电流 (mA)
    BMS_OCDDelay_t ocd_delay;             // 过流延时
    int16_t short_circuit_threshold;      // 短路阈值 (mA)
    BMS_SCDDelay_t scd_delay;             // 短路延时
    bool coulomb_counter_enabled;         // 是否启用库仑计
};

/**
 * @brief BQ76920电池监控芯片驱动类
 * 
 * 提供电压、电流、温度测量以及MOS管控制功能
 */
class BQ76920 {
public:
    /**
     * @brief 默认构造函数
     */
    BQ76920();
    
    /**
     * @brief 构造函数 - 接受I2C接口参数
     * @param i2c_interface I2C接口实例
     */
    explicit BQ76920(I2CInterface& i2c_interface);
    
    /**
     * @brief 初始化设备
     * @return true 初始化成功, false 初始化失败
     */
    bool begin();
    
    /**
     * @brief 芯片完整初始化流程
     * 包括寄存器清理、ADC 使能、校准参数读取等
     * @return true 初始化成功，false 初始化失败
     */
    bool initializeChip(const BQ76920_InitConfig &config);
    
    /**
     * @brief 动态更新配置参数
     * 仅更新运行时可变的配置项，无需重新初始化整个芯片
     * 适用于在系统运行过程中调整保护阈值、延迟等参数
     * @param config 新的配置参数
     * @return true 更新成功，false 更新失败
     */
    bool updateConfiguration(const BQ76920_InitConfig &config);

    // 电压保护配置与读取
    bool setOVThreshold(uint16_t voltage_mV);
    bool setUVThreshold(uint16_t voltage_mV);
    float getOVThreshold();
    float getUVThreshold();
    bool setOVUVDelay(BMS_OVDelay_t ovDelay, BMS_UVDelay_t uvDelay);
     // 过流/短路保护读取
    /**
     * @brief 读取过流保护 (OCD) 配置
     * @param threshold_mV 输出指针：返回阈值电压 (mV)
     * @param delay_ms 输出指针：返回延迟时间 (ms)，若失败则不变
     * @return true 读取成功, false 失败
     */
    bool getOCDProtection(float* threshold_mV, uint16_t* delay_ms);

    /**
     * @brief 读取短路保护 (SCD) 配置
     * @param threshold_mV 输出指针：返回阈值电压 (mV)
     * @param delay_us 输出指针：返回延迟时间 (us)，若失败则不变
     * @return true 读取成功, false 失败
     */
    bool getSCDProtection(float* threshold_mV, uint16_t* delay_us);
    
    // 故障状态管理
    uint8_t getFaultStatus();
    bool clearFaults(uint8_t mask);
    
    // 电池均衡控制
    bool setCellBalance(uint8_t cellMask);
    
    // 负载检测
    bool isLoadPresent();
    
    // 低功耗模式
    bool enterShipMode();
    
    // 电流校准
    void calibrateCurrentZero();
    void setCurrentZeroDrift(int16_t drift); // 允许手动设置零漂

    /**
     * @brief 读取寄存器值
     * @param reg 寄存器地址
     * @param value 存储读取值的指针
     * @return true 读取成功, false 读取失败
     */
    bool readRegister(uint8_t reg, uint8_t *value);

    /**
     * @brief 读取寄存器值,连续两个字节
     * @param reg 寄存器地址
     * @param value 存储读取值的指针
     * @return true 读取成功, false 读取失败
     */
    bool readRegisterWord(uint8_t reg, uint16_t *value);

    
    
    /**
     * @brief 写入寄存器值
     * @param reg 寄存器地址
     * @param value 要写入的值
     * @return true 写入成功, false 写入失败
     */
    bool writeRegister(uint8_t reg, uint8_t value);
    
    /**
     * @brief 获取设备I2C地址
     * @return 设备地址
     */
    uint8_t getDeviceAddress() const { return BQ76920_ADDRESS; }
    
    /**
     * @brief 检查设备是否连接
     * @return true 设备已连接, false 设备未连接
     */
    bool isConnected();
    
    /**
     * @brief 读取ADC校准参数
     */
    void readADCParams();
    
    /**
     * @brief 获取指定电池单元电压
     * @param cell_idx 电池单元索引 (0-4)
     * @return 电压值 (mV)
     */
    uint16_t getCellVoltage_mV(uint8_t cell_idx);
    
    // 批量读取所有电芯电压
    bool getCellVoltages_mV(uint16_t* voltages, uint8_t count);

    // 根据逻辑电芯索引获取硬件均衡位
    uint8_t getBalanceMaskForCell(uint8_t cell_idx);

    // 获取实际电池串数
    uint8_t getActualCellCount() const { return actual_cell_count; }
    /**
     * @brief 设置MOS管状态
     * @param chg 充电使能 (0=关闭, 1=开启)
     * @param dsg 放电使能 (0=关闭, 1=开启)
     */
    void setMOS(uint8_t chg, uint8_t dsg);

    /**
     * @brief 读取充放电 MOS 管硬件状态
     * @param isCHG true=充电MOS打开, false=关闭
     * @param isDSG true=放电MOS打开, false=关闭
     * @return true 读取成功, false 读取失败
     */
    bool getMOS(bool& isCHG, bool& isDSG);

    /**
     * @brief 获取电流值
     * @return 电流值 (mA), 正值表示充电, 负值表示放电
     */
    int16_t getCurrent_mA();
    
    /**
     * @brief 获取温度值
     * @return 温度值 (°C)
     */
    float getTempCelsius();
    
    /**
     * @brief 读取电池组总电压
     * @return 总电压值 (mV), 失败时返回 0
     */
    uint16_t getTotalVoltage_mV();
    
    /**
     * @brief 获取当前增益值
     * @return 增益值 (mV/LSB)
     */
    float getBqGain() const { return bq_gain; }
    
    /**
     * @brief 获取当前偏移值
     * @return 偏移值 (mV)
     */
    int8_t getBqOffset() const { return bq_offset; }

    // =============================================================================
    // 库仑计与电量统计 (Coulomb Counter & SOC)
    // =============================================================================
    
    /**
     * @brief 检查库仑计是否有新数据就绪 (CC_READY flag)
     * @return true 有新数据，false 无新数据
     */
    bool isCoulombCounterReady();

    /**
     * @brief 清除库仑计就绪标志 (SYS_STAT Bit 7)
     * @return true 成功清除
     */
    bool clearCoulombCounterFlag();

    /**
     * @brief 读取原始库仑计计数值 (有符号 16-bit)
     * @param rawValue 输出参数，存储原始计数值
     * @return true 读取成功
     */
    bool readCoulombCounterRaw(int16_t &rawValue);

    // =============================================================================
    // 调试接口 (Debug Interface)
    // =============================================================================
    
    /**
     * @brief 打印关键寄存器值到串口
     * 打印地址 0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B 的寄存器值
     */
    void printRegisters();

    /**
     * @brief 读取所有寄存器的原始值
     * @param values 存储寄存器值的数组指针（至少需要10个元素）
     * @return true 读取成功，false 读取失败
     *
     * @note 数组顺序：
     *   [0]  STATUS (0x00)
     *   [1]  CELL_BALANCE (0x01)
     *   [2]  SYS_CTRL1 (0x04)
     *   [3]  SYS_CTRL2 (0x05)
     *   [4]  PROTECT1 (0x06)
     *   [5]  PROTECT2 (0x07)
     *   [6]  PROTECT3 (0x08)
     *   [7]  OV_TRIP (0x09)
     *   [8]  UV_TRIP (0x0A)
     *   [9]  CC_CFG (0x0B)
     *   [10]  ADC_GAIN (0x0C) 
     *   [11]  ADC_OFFSET (0x0D)
     */
    bool readAllRegisters(uint8_t* values);

private:
    I2CInterface* i2c;      ///< I2C 接口指针
    float bq_gain;          ///< ADC 增益值 (mV/LSB)
    int8_t bq_offset;       ///< ADC 偏移值 (mV)
    int16_t current_zero_drift; ///< 动态电流零漂值
    uint8_t actual_cell_count;  ///< 实际电池串数
    
    // 连接状态缓存
    bool last_connection_status_ = false;           ///< 上次连接检测结果
    unsigned long last_connection_check_time_ = 0;  ///< 上次连接检测时间戳
    
    // 保护计算辅助
    uint8_t calculateTripReg(uint16_t voltage_mV, float gain, float offset);
    uint8_t findClosestOCDThreshold(float target_mV, bool &rsnsBit);
    uint8_t findClosestSCDThreshold(float target_mV, bool &rsnsBit);

    // 过流/短路保护配置
    bool setOCDProtection(float threshold_mV, BMS_OCDDelay_t delay);
    bool setSCDProtection(float threshold_mV, BMS_SCDDelay_t delay);

};

#endif // BQ76920_H