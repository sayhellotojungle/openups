#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// 外部固件版本标签声明 (定义在 sketch_jan14a.ino 中)
// =============================================================================
extern char FIRMWARE_ID_TAG[];

// =============================================================================
// 常量定义
// =============================================================================

// overall_status 常量
#define OVERALL_STATUS_NORMAL    0    // 正常
#define OVERALL_STATUS_WARNING   1    // 警告
#define OVERALL_STATUS_FAULT     2    // 故障

// power_mode 常量
#define POWER_MODE_AC        0    // AC 供电
#define POWER_MODE_BATTERY   1    // 电池供电（含放电/待机）
#define POWER_MODE_HYBRID    2    // 混合供电（AC + 电池）
#define POWER_MODE_CHARGING  3    // 充电中

// =============================================================================
// 枚举定义
// =============================================================================

typedef enum {
    BMS_FAULT_NONE = 0,
    BMS_FAULT_OVER_VOLTAGE,
    BMS_FAULT_UNDER_VOLTAGE,
    BMS_FAULT_OVER_CURRENT,
    BMS_FAULT_SHORT_CIRCUIT,
    BMS_FAULT_OVER_TEMP,
    BMS_FAULT_CHIP_ERROR,
    BMS_FAULT_PASSIVE_SHUTDOWN,
} BMS_Fault_t;

typedef enum {
    POWER_FAULT_NONE = 0,
    POWER_FAULT_CHIP_ERROR,
    POWER_FAULT_OVER_CURRENT,
    POWER_FAULT_OVER_TEMPERATURE,
    POWER_FAULT_INPUT_OVERVOLTAGE,
    POWER_FAULT_INPUT_UNDERVOLTAGE,
    POWER_FAULT_BATTERY_OVERVOLTAGE,
    POWER_FAULT_BATTERY_UNDERVOLTAGE,
    POWER_FAULT_SHORT_CIRCUIT,
    POWER_FAULT_CHARGE_TIMEOUT,
    POWER_FAULT_I2C_COMMUNICATION
} Power_Fault_Type_t;

// =============================================================================
// 状态数据结构
// =============================================================================

typedef struct {
    // 基本参数
    float soc;                      // SOC 百分比 (0-100%)
    float soh;                      // SOH 百分比 (0-100%)
    uint16_t voltage;               // 总电压 (mV)
    int16_t current;                // 电流 (mA), 正值充电，负值放电
    float temperature;              // 电池温度 (°C)
    
    // 单体电压
    uint16_t cell_voltages[5];      // 单体电压数组 (mV)
    uint16_t cell_voltage_min;      // 最低单体电压 (mV)
    uint16_t cell_voltage_max;      // 最高单体电压 (mV)
    uint16_t cell_voltage_avg;      // 平均单体电压 (mV)
    
    // 电池状态
    uint32_t cycle_count;           // 循环次数
    uint32_t capacity_full;         // 容量 (mAh)
    uint32_t capacity_remaining;    // 剩余容量 (mAh)
    bool balancing_active;          // 均衡激活状态
    uint8_t balance_mask;           // 均衡掩码 (bit0-4 对应 cell1-5)
    
    // 均衡统计
    uint16_t balancing_events_total; // 总均衡次数（高 14bit）
    uint16_t cell_balancing_count[5]; // 每个电芯均衡次数（低 50bit，每 10bit 一个）
    
    // 故障和状态
    BMS_Fault_t fault_type;         // 当前故障类型
    bool is_connected;              // BMS 连接状态
    uint8_t bms_mode;               // BMS 工作模式 (0:正常，1:异常)
    
    // BQ76920 寄存器原始值
    uint8_t bq76920_registers[12];  // [0]STATUS [1]CELL_BALANCE [2]SYS_CTRL1 [3]SYS_CTRL2
                                    // [4]PROTECT1 [5]PROTECT2 [6]PROTECT3 [7]OV_TRIP [8]UV_TRIP [9]CC_CFG
    
    uint32_t last_update_time;      // 最后更新时间戳 (ms)
} BMS_State;

typedef struct {
    // 输入电源
    uint16_t input_voltage;         // 输入电压 (mV)
    uint16_t input_current;         // 输入电流 (mA)
    bool ac_present;                // AC 电源存在状态
    
    // 输出电源
    uint32_t output_power;          // 输出功率 (W)
    
    // 电池充放电
    uint16_t battery_voltage;       // 电池电压 (mV)
    uint16_t battery_current;       // 电池放电电流 (mA)
    bool charger_enabled;           // 充电器使能状态
    
    // 混合供电
    bool hybrid_mode;               // 混合供电模式
    
    // 故障
    Power_Fault_Type_t fault_type;  // 当前故障类型
    
    // BQ24780S 状态
    bool bq24780s_connected;        // 芯片连接状态
    uint8_t bq24780s_status;        // 状态寄存器值
    bool prochot_status;            // PROCHOT#引脚状态
    bool tbstat_status;             // TB_STAT#引脚状态
    
    // BQ24780S 寄存器原始值
    uint16_t bq24780s_registers[11]; // [0]CHARGE_OPTION0 [1]CHARGE_OPTION1 [2]CHARGE_OPTION2 [3]CHARGE_OPTION3
                                     // [4]PROCHOT_OPTION0 [5]PROCHOT_OPTION1 [6]PROCHOT_STATUS
                                     // [7]CHARGE_CURRENT [8]CHARGE_VOLTAGE [9]DISCHARGE_CURRENT [10]INPUT_CURRENT
    
    uint32_t last_update_time;      // 最后更新时间戳 (ms)
} Power_State;

typedef struct {
    // 系统信息
    uint32_t uptime;                // 运行时间 (秒)，节约空间
    uint32_t current_time;          // 当前时间 (Unix 时间戳)
    
    // 网络状态
    bool wifi_connected;            // WiFi 连接状态
    char wifi_ssid[32];             // WiFi 名称
    int8_t wifi_rssi;               // 信号强度 (dBm)
    uint8_t wifi_status;            // 状态码
    
    // 硬件状态
    float board_temperature;        // 主板温度 (°C)
    float environment_temperature;  // 环境温度 (°C)
    uint8_t led_brightness;         // LED 亮度 (0-255)
    bool buzzer_enabled;            // 蜂鸣器使能
    uint8_t buzzer_volume;          // 蜂鸣器音量 (0-255)
    
    // 版本信息
    char firmware_version[16];      // 固件版本
    char hardware_version[16];      // 硬件版本
    
    uint32_t last_update_time;      // 最后更新时间戳 (ms)
} System_Info;

// =============================================================================
// 主设备数据结构 - 系统全局"黑板"
// =============================================================================

typedef struct {
    BMS_State bms;                  // BMS 状态
    Power_State power;              // 电源状态
    System_Info system;             // 系统信息
    
    uint8_t overall_status;         // 整体状态 (0:正常，1:警告，2:故障) - 由 FSM 状态自动同步
    bool emergency_shutdown;        // 紧急关机状态
    uint32_t timestamp;             // 数据更新时间戳 (ms)
    
    uint8_t power_mode;             // 电源模式 (0:AC, 1:BATTERY, 2:HYBRID, 3:CHARGING)
    
    bool over_current_protection;   // 过流保护
    bool over_temp_protection;      // 过温保护
    bool short_circuit_protection;  // 短路保护
} System_Global_State;

// =============================================================================
// 配置结构体
// =============================================================================

struct Configuration {
    char identifier[5];             // 配置标识符 (4 字符 + 结束符)
    char wifi_ssid[50];             // WiFi SSID
    char wifi_pass[50];             // WiFi 密码

    // 静态 IP 配置
    bool use_static_ip;             // 是否使用静态 IP
    char static_ip[16];             // 静态 IP 地址
    char static_gateway[16];        // 网关
    char static_subnet[16];         // 子网掩码
    char static_dns[16];            // DNS 服务器

    char ntp_server[50];           // NTP 服务器地址

    uint8_t led_brightness;         // LED 亮度 (0-100)
    uint8_t buzzer_volume;          // 蜂鸣器音量 (0-100)
    bool buzzer_enabled;            // 蜂鸣器使能
    bool hid_enabled;               // HID 服务使能
    uint8_t hid_report_mode;        // HID 电量发送模式 (0: mAh, 1: mWh, 2: %)

    // MQTT 配置
    char mqtt_broker[64];           // MQTT Broker 地址
    uint16_t mqtt_port;             // MQTT 端口 (1883/8883)
    char mqtt_username[64];         // MQTT 用户名
    char mqtt_password[64];         // MQTT 密码
};

#endif // DATA_STRUCTURES_H