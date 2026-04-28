#ifndef UPS_HID_SERVICE_H
#define UPS_HID_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "data_structures.h"
#include <USB.h>
#include <USBHID.h>
#include "event_types.h"
#include "event_bus.h"

#define HID_PD_IPRODUCT              0x01 // 产品字符串特征
#define HID_PD_SERIAL                0x02 // 序列号特征
#define HID_PD_MANUFACTURER          0x03 // 制造商字符串特征
#define HID_PD_RECHARGEABLE          0x06 // 可充电特性
#define HID_PD_PRESENTSTATUS         0x07 // 状态报告
#define HID_PD_CONFIGVOLTAGE         0x0A // 配置电压
#define HID_PD_VOLTAGE               0x0B // 电压
#define HID_PD_REMAININGCAPACITY     0x0C // 剩余容量
#define HID_PD_RUNTIMETOEMPTY        0x0D // 剩余运行时间
#define HID_PD_CYCLECOUNT            0x04 // 循环次数
#define HID_PD_FULLCHRGECAPACITY     0x0E // 满电容量
#define HID_PD_WARNCAPACITYLIMIT     0x0F // 警告限制容量
#define HID_PD_CPCTYGRANULARITY1     0x10 // 容量粒度1
#define HID_PD_CPCTYGRANULARITY2     0x18 // 容量粒度2
#define HID_PD_REMNCAPACITYLIMIT     0x11 // 剩余限制容量
#define HID_PD_CAPACITYMODE          0x16 // 容量模式
#define HID_PD_DESIGNCAPACITY        0x17 // 设计容量

// =============================================================================
// UPS HID Service Class
// =============================================================================

class UPS_HID_Service : public USBHIDDevice {
public:
    UPS_HID_Service();
    ~UPS_HID_Service();
    
    // 初始化USB HID设备
    bool begin();
    
    // 更新UPS数据并发送报告
    void update(const System_Global_State& globalState);
    
    // 检查是否已连接到主机
    bool isConnected() const;
    
    // 停止服务
    void end();

    // 配置接口
    void setBatteryConfig(uint8_t series_count, uint16_t nominal_voltage_mV, uint32_t design_capacity_mAh);
    void setCapacityMode(uint8_t mode);  // 0: mAh, 1: mWh, 2: %
    void setDefaultSOH(float soh);       // 设置默认 SOH 值（用于初始化预填充）
    void setDeviceIdentifier(const char* identifier);  // 设置设备识别码

private:
    bool initialized;
    bool connected;
    uint32_t lastReportTime;
    uint8_t reportSendIndex;  // 跟踪当前发送的 report 索引 (0, 1, 2)
    
    // 定时填充数据相关
    unsigned long lastFillTime_;      // 上次填充数据的时间戳

    // 电池配置参数
    uint8_t battery_series_count;       // 电池串数
    uint16_t battery_nominal_voltage;   // 标称电压 (mV)
    uint32_t battery_design_capacity;   // 设计容量 (mAh)
    uint8_t capacity_mode;              // 容量汇报模式 (0: mAh, 1: mWh, 2: %)
    float default_soh;                  // 默认 SOH 值（用于初始化时预填充）
    const char* device_identifier;      // 设备识别码

    // USB HID 实例引用
    USBHID hid;
    
    // ================================================================
    // UPS FEATURE Report 结构体 (对应描述符中的 FEATURE 部分)
    // ================================================================
    typedef struct __attribute__((packed)) {
        // Report ID 4: CycleCount (INPUT + FEATURE)
        uint16_t cycle_count;           // 循环次数
        
        // Report ID 6: Rechargable
        uint8_t rechargable;            // 可充电标志
        
        // Report ID 7: PresentStatus 集合 (INPUT + FEATURE)
        uint8_t charging;               // Charging
        uint8_t discharging;            // Discharging
        uint8_t ac_present;             // ACPresent
        uint8_t battery_present;        // BatteryPresent
        uint8_t below_capacity_limit;   // BelowRemainingCapacityLimit
        uint8_t need_replacement;       // NeedReplacement
        uint8_t shutdown_imminent;      // ShutdownImminent
        uint8_t overload;               // Overload
        
        // Report ID 10: ConfigVoltage
        uint16_t config_voltage;        // 配置电压 (mV)
        
        // Report ID 11: Voltage (INPUT + FEATURE)
        uint16_t voltage;               // 电压 (mV)
        
        // Report ID 12: RemainingCapacity (INPUT + FEATURE)
        uint32_t remaining_capacity;    // 剩余容量 (mAh/mWh/%)
        
        // Report ID 13: RunTimeToEmpty (INPUT + FEATURE)
        uint16_t run_time_to_empty;     // 剩余运行时间 (秒)
        
        // Report ID 14: FullChargeCapacity
        uint32_t full_charge_capacity;  // 满充容量 (mAh/mWh/%)
        
        // Report ID 15: WarningCapacityLimit
        uint32_t warning_capacity_limit; // 警告容量限制
        
        // Report ID 16: CapacityGranularity1
        uint8_t capacity_granularity1; // 容量粒度 1
        
        // Report ID 17: RemainingCapacityLimit
        uint32_t remaining_capacity_limit; // 剩余容量限制
        
        // Report ID 22: CapacityMode
        uint8_t capacity_mode;          // 容量模式 (0=mAh, 1=mWh, 2=%)
        
        // Report ID 23: DesignCapacity
        uint32_t design_capacity;       // 设计容量 (mAh/mWh/%)
        
        // Report ID 24: CapacityGranularity2
        uint8_t capacity_granularity2; // 容量粒度 2
    } ups_feature_report_t;
    
    // UPS FEATURE Report 实例（非静态，每个服务实例拥有独立副本）
    ups_feature_report_t featureReport;
    
    // USB HID报告描述符 (UPS格式)
    static const uint8_t ups_hid_report_descriptor[];
    static const size_t UPS_HID_REPORT_DESCRIPTOR_SIZE;
    
    // 内部方法
    void fillFeatureData(const System_Global_State& globalState, float soh_override = -1.0f);
    bool sendReport();
    void handleConnectionStatus();
    void handleUsbHotRestart();
    
    // 实现 USBHIDDevice 虚函数
    uint16_t _onGetDescriptor(uint8_t* dst) override;
    uint16_t _onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len);
    void _onSetFeature(uint8_t report_id, uint8_t const* buffer, uint16_t len);
    
    // 禁用拷贝构造和赋值
    UPS_HID_Service(const UPS_HID_Service&) = delete;
    UPS_HID_Service& operator=(const UPS_HID_Service&) = delete;
    
    // 事件回调处理
    static void onHidConfigChanged(EventType type, void* param);
    void handleConfigChange(const Configuration* newConfig);
    
    // USB 热重启相关
    bool pendingRestart;              // 待重启标志
    unsigned long usbStopTime;        // USB 停止时间戳
    bool needReconnect;               // 需要重新连接标志
};

#endif // UPS_HID_SERVICE_H