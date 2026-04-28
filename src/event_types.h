// event_types.h
#pragma once

#include <stdint.h>

// 系统事件枚举
// 命名规范：EVT_<模块>_<具体事件>
enum EventType {
    EVT_NONE = 0,

    // --- 硬件事件 (Hardware) ---
    EVT_HW_ACOK_CHANGED,          // AC 电源插拔 (payload: bool*)
    EVT_HW_PROCHOT_ALERT,         // 充电芯片过热/报警 (payload: bool*)
    EVT_HW_TBSTAT_CHANGED,        // 混合供电状态变化 (payload: bool*)
    EVT_BMS_ALARM_INTERRUPT,      // BMS 芯片报警中断 (payload: nullptr, 需读取寄存器)
    
    // --- 传感器事件 (Sensors) ---
    EVT_TEMP_BOARD_UPDATED,       // 主板温度更新 (payload: float*)
    EVT_TEMP_ENV_UPDATED,         // 环境温度更新 (payload: float*)
    
    // --- BMS 事件 (BMS) ---
    EVT_BMS_FAULT_DETECTED,       // BMS 故障检测 (payload: int* 故障码)
    EVT_BMS_SOC_CHANGED,          // SOC 变化超过阈值 (payload: float* 新 SOC)
    EVT_BMS_BALANCING_STARTED,    // 均衡启动 (payload: uint8_t* 均衡掩码)
    EVT_BMS_BALANCING_STOPPED,    // 均衡停止 (payload: nullptr)
    
    // --- 电源管理事件 (Power Management) ---
    EVT_PM_MODE_CHANGED,          // 电源管理模式切换 (payload: int* 新模式)
    EVT_PM_CHARGE_COMPLETE,       // 充电完成 (payload: nullptr)
    EVT_PM_CHARGE_STARTED,        // 充电开始 (payload: nullptr)
    
    // --- 系统控制事件 (System) ---
    EVT_BTN_SHORT_PRESS,          // 按键短按
    EVT_BTN_LONG_PRESS,           // 按键长按

    // --- 辅助事件 (Utils) ---
    EVT_LOG_MESSAGE,              // 日志消息 (payload: const char*)
    
    // --- 配置变更事件 (Configuration) ---
    EVT_CONFIG_SYSTEM_CHANGED,    // 系统配置变更 (payload: Configuration*)
    EVT_CONFIG_WIFI_CHANGED,      // WiFi 配置变更 (payload: Configuration*)
    EVT_CONFIG_BMS_CHANGED,       // BMS 配置变更 (payload: BMS_Config_t*)
    EVT_CONFIG_POWER_CHANGED,     // 电源配置变更 (payload: Power_Config_t*)
    EVT_CONFIG_UPS_CHANGED,      // UPS配置变更 (payload: Configuration*)
    EVT_CONFIG_SYSTEM_CHANGE_REQUEST,// 系统配置变更请求 (payload: Configuration*)
    
    // --- BMS 控制事件 ---
    EVT_BMS_SHIPMODE_REQUEST,       // BMS 进入运输模式请求 (payload: nullptr)
    
    EVT_MAX_TYPES                 // 用于数组边界检查
};

// 回调函数类型定义
// param: 事件携带的可选数据指针
typedef void (*EventCallback)(EventType type, void* param);