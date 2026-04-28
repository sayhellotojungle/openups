#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// ==================== 电源管理系统引脚定义 ====================
// BQ24780S充放电控制芯片引脚
#define BQ24780S_ACOK_PIN     13  // ACOK - 电源输入状态检测
#define BQ24780S_PROCHOT_PIN  14  // PROCHOT# - 芯片报警状态检测
#define BQ24780S_TBSTAT_PIN   15  // TB_STAT# - 混合供电状态检测
#define BQ24780S_IADP_PIN     1   // IADP - 电源电流ADC采集 (GPIO1)
#define BQ24780S_IDCHG_PIN    2   // IDCHG - 放电电流ADC采集 (GPIO2)
#define BQ24780S_PMON_PIN     9   // PMON - 系统功率监测 (GPIO9)

// BQ76920电池管理芯片引脚
#define BQ76920_ALERT_PIN     16  // ALERT - 电池管理告警中断
#define BQ76920_I2CVCC_PIN    6   // i2cVCC - 控制电池端iso1640芯片的vcc供电，降低功耗

// ==================== 系统状态监测引脚 ====================
#define INPUT_VOLTAGE_PIN     4   // 输入电压监测 (1/10分压)
#define BATTERY_VOLTAGE_PIN   5   // 电池电压监测 (1/10分压)
#define BOARD_TEMP_PIN        7   // 主板温度监测 (NTC 10K电阻)
#define TEMP_POWER_PIN        21  // 温度监测NTC供电
#define ENVIRONMENT_TEMP_PIN  8   // 环境温度监测 (NTC 10K电阻)

// ==================== 指示灯引脚定义 ====================
#define POWER_LED_PIN         42  // 电源指示灯 (ACOK正常时亮，初始化时闪烁)
#define CHARGING_LED_PIN      41  // 充电指示灯 (充电时亮)
#define DISCHARGING_LED_PIN   40  // 放电指示灯 (放电时亮)
#define WIFI_FAIL_LED_PIN     39  // WiFi连接失败指示灯
#define WIFI_SUCCESS_LED_PIN  38  // WiFi连接成功指示灯

// ==================== 报警与控制引脚 ====================
#define BUZZER_PIN            18  // 报警扬声器 (系统故障或电池低电量时响)
#define RESET_BUTTON_PIN      17   // 重置配置按键

// ==================== RGB LED引脚定义 ====================
#define RGB_LED_PIN           48  // WS2812B可寻址RGB LED (用于状态指示)

// ==================== I2C通信引脚 ====================
#define I2C_SCL_PIN           12  // I2C时钟线
#define I2C_SDA_PIN           11  // I2C数据线



#endif