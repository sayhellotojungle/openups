#include "ups_hid_service.h"
#include <Arduino.h>
#include <tusb.h> 


const uint8_t UPS_HID_Service::ups_hid_report_descriptor[] = {

    0x05, 0x84, // USAGE_PAGE (Power Device)
    0x09, 0x04, // USAGE (UPS)
    0xA1, 0x01, // COLLECTION (Application)
    0x09, 0x24, //   USAGE (Sink)
    0xA1, 0x02, //   COLLECTION (Logical)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00, //     LOGICAL_MAXIMUM (255)
    0x85, HID_PD_IPRODUCT, //     REPORT_ID (1)
    0x09, 0xFE, //     USAGE (iProduct)
    0x79, 0x02, //     STRING INDEX (2)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_SERIAL, //     REPORT_ID (2)
    0x09, 0xFF, //     USAGE (iSerialNumber)
    0x79, 0x03, //  STRING INDEX (3)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_MANUFACTURER, //     REPORT_ID (3)
    0x09, 0xFD, //     USAGE (iManufacturer)
    0x79, 0x01, //     STRING INDEX (1)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x05, 0x85, //     USAGE_PAGE (Battery System) ====================
    0x85, HID_PD_RECHARGEABLE, //     REPORT_ID (6)
    0x09, 0x8B, //     USAGE (Rechargable)                  
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CAPACITYMODE, //     REPORT_ID (22)
    0x09, 0x2C, //     USAGE (CapacityMode)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CPCTYGRANULARITY1, //     REPORT_ID (16)
    0x09, 0x8D, //     USAGE (CapacityGranularity1)
    0x26, 0x64,0x00, //     LOGICAL_MAXIMUM (100)    
    0xB1, 0x22, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CPCTYGRANULARITY2, //     REPORT_ID (24)
    0x09, 0x8E, //     USAGE (CapacityGranularity2)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_FULLCHRGECAPACITY, //     REPORT_ID (14)        
    0x09, 0x67, //     USAGE (FullChargeCapacity)
    0x75, 0x20, //     REPORT_SIZE (32)
    0x27, 0xE0, 0xFF, 0x0F, 0x00, //     LOGICAL_MAXIMUM (1048544)
    0xB1, 0x83, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_DESIGNCAPACITY, //     REPORT_ID (23)
    0x09, 0x83, //     USAGE (DesignCapacity)
    0xB1, 0x83, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_REMAININGCAPACITY, //     REPORT_ID (12)
    0x09, 0x66, //     USAGE (RemainingCapacity)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x66, //     USAGE (RemainingCapacity)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_WARNCAPACITYLIMIT, //     REPORT_ID (15)
    0x09, 0x8C, //     USAGE (WarningCapacityLimit)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_REMNCAPACITYLIMIT, //     REPORT_ID (17)
    0x09, 0x29, //     USAGE (RemainingCapacityLimit)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_RUNTIMETOEMPTY, //     REPORT_ID (13)    
    0x09, 0x68, //     USAGE (RunTimeToEmpty) 
    0x75, 0x10, //     REPORT_SIZE (16)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     LOGICAL_MAXIMUM (65534)
    0x66, 0x01, 0x10, //     UNIT (Seconds)
    0x55, 0x00, //     UNIT_EXPONENT (0)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x68, //     USAGE (RunTimeToEmpty)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)      
    0x85, HID_PD_CYCLECOUNT, //     REPORT_ID (4)
    0x09, 0x6B, //     USAGE (CycleCount)
    0x65, 0x00, //     UNIT (None)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x6B, //     USAGE (CycleCount)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x05, 0x84, //     USAGE_PAGE (Power Device) ====================
    0x85, HID_PD_CONFIGVOLTAGE, //     REPORT_ID (10)
    0x09, 0x40, //     USAGE (ConfigVoltage)
    0x75, 0x10, //     REPORT_SIZE (16)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     LOGICAL_MAXIMUM (65535)
    0x67, 0x21, 0xD1, 0xF0, 0x00, //     UNIT (Centivolts)
    0x55, 0x05, //     UNIT_EXPONENT (5) 这是巨坑 
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x40, //     USAGE (ConfigVoltage)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_VOLTAGE, //     REPORT_ID (11)
    0x09, 0x30, //     USAGE (Voltage)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x30, //     USAGE (Voltage)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x02, //     USAGE (PresentStatus)
    0xA1, 0x02, //     COLLECTION (Logical)
    0x85, HID_PD_PRESENTSTATUS, //       REPORT_ID (7)
    0x05, 0x85, //       USAGE_PAGE (Battery System) =================
    0x09, 0x44, //       USAGE (Charging)
    0x75, 0x01, //       REPORT_SIZE (1)
    0x15, 0x00, //       LOGICAL_MINIMUM (0)
    0x25, 0x01, //       LOGICAL_MAXIMUM (1)
    0x65, 0x00, //       UNIT (None)
    0x55, 0x00, //       UNIT_EXPONENT (0) 
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x44, //       USAGE (Charging)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x45, //       USAGE (Discharging)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x45, //       USAGE (Discharging)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0xD0, //       USAGE (ACPresent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0xD0, //       USAGE (ACPresent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0xD1, //       USAGE (BatteryPresent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0xD1, //       USAGE (BatteryPresent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x42, //       USAGE (BelowRemainingCapacityLimit)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x42, //       USAGE (BelowRemainingCapacityLimit)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x4B, //       USAGE (NeedReplacement)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x4B, //       USAGE (NeedReplacement)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)    
    0x05, 0x84, //       USAGE_PAGE (Power Device) =================
    0x09, 0x69, //       USAGE (ShutdownImminent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x69, //       USAGE (ShutdownImminent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x65, //       USAGE (Overload)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x65, //       USAGE (Overload)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0xC0,       //     END_COLLECTION
    0xC0,       //   END_COLLECTION
    0xC0        // END_COLLECTION
};


const size_t UPS_HID_Service::UPS_HID_REPORT_DESCRIPTOR_SIZE = sizeof(ups_hid_report_descriptor);

// =============================================================================
// 构造 / 析构
// =============================================================================

UPS_HID_Service::UPS_HID_Service()
    : initialized(false)
    , connected(false)
    , lastReportTime(0)
    , reportSendIndex(0)               // 初始发送索引为 0
    , lastFillTime_(0)                 // 初始化填充时间戳
    , battery_series_count(3)          // 默认 3 串
    , battery_nominal_voltage(3700)    // 默认标称电压 3.7V (3700mV)
    , battery_design_capacity(2000)    // 默认设计容量 2000mAh
    , capacity_mode(0)                 // 默认 mAh 模式
    , default_soh(100.0f)              // 默认 SOH 为 100%
    , device_identifier("0000")        // 默认设备识别码
    , pendingRestart(false)            // 待重启标志
    , usbStopTime(0)                   // USB 停止时间戳
    , needReconnect(false)             // 需要重新连接标志
    , featureReport({
        .cycle_count = 1,                   // 循环次数
        .rechargable = 1,                   // 可充电标志
        .charging = 0,                      // Charging
        .discharging = 0,                   // Discharging
        .ac_present = 1,                    // ACPresent
        .battery_present = 1,               // BatteryPresent
        .below_capacity_limit = 0,          // BelowRemainingCapacityLimit
        .need_replacement = 0,              // NeedReplacement
        .shutdown_imminent = 0,             // ShutdownImminent
        .overload = 0,                      // Overload
        .config_voltage = 11100,            // 配置电压 (mV)
        .voltage = 12000,                   // 电压 (mV)
        .remaining_capacity = 1500,         // 剩余容量
        .run_time_to_empty = 1800,          // 剩余运行时间 (秒)
        .full_charge_capacity = 2000,       // 满充容量
        .warning_capacity_limit = 20,       // 警告容量限制
        .capacity_granularity1 = 1,         // 容量粒度 1
        .remaining_capacity_limit = 100,    // 剩余容量限制
        .capacity_mode = 0,                 // 容量模式 0 mah 1 mwh 2 % 
        .design_capacity = 2000,            // 设计容量
        .capacity_granularity2 = 1,         // 容量粒度 2
      })
{
    
}

UPS_HID_Service::~UPS_HID_Service() {
    end();
}

// =============================================================================
// 公共接口
// =============================================================================

bool UPS_HID_Service::begin() {

    if (initialized) return true;

    Serial.println(F("[HID] Initializing..."));

    USB.VID(0x051D);
    USB.PID(0x0002);
    USB.manufacturerName("OpenCommunity");
    USB.productName("UPS");
    USB.serialNumber(device_identifier);

    hid.addDevice(this, UPS_HID_REPORT_DESCRIPTOR_SIZE);

    USB.begin();
    hid.begin();

    delay(300);

    initialized    = true;
    connected      = false;
    lastReportTime = millis();

    if (lastFillTime_ == 0){
        // 初始化时调用一次 fillFeatureData，使用默认 SOH 值
        fillFeatureData(System_Global_State(), default_soh);
    }

    EventBus::getInstance().subscribe(EVT_CONFIG_UPS_CHANGED, onHidConfigChanged);

    return true;
}

void UPS_HID_Service::update(const System_Global_State& globalState) {
    // 每秒填充一次数据（无论是否连接或初始化）
    unsigned long now = millis();
    if (now - lastFillTime_ >= 1000) {
        fillFeatureData(globalState);
        lastFillTime_ = now;
    }

    // 如果未初始化，直接返回
    if (!initialized) return;

    // 处理 USB 热重启逻辑
    handleUsbHotRestart();

    handleConnectionStatus();
    

    // 每 4 秒发送一次报告（仅在已连接时）
    if (now - lastReportTime >= 4000) {
        lastReportTime = now;

        if (connected && tud_mounted()) {
            sendReport();
        }
    }
}

bool UPS_HID_Service::isConnected() const {
    return initialized && connected;
}

void UPS_HID_Service::end() {
    if (initialized) {
        Serial.println(F("[HID] Stopping"));
        initialized = false;
        connected   = false;
    }
}

// =============================================================================
// 配置接口实现
// =============================================================================

void UPS_HID_Service::setBatteryConfig(uint8_t series_count, uint16_t nominal_voltage_mV, uint32_t design_capacity_mAh) {
    battery_series_count = series_count;
    battery_nominal_voltage = nominal_voltage_mV;
    battery_design_capacity = design_capacity_mAh;
    
    Serial.printf("[HID] Battery config updated: %dS, %dmV, %dmAh\n", 
                  series_count, nominal_voltage_mV, design_capacity_mAh);
}

void UPS_HID_Service::setCapacityMode(uint8_t mode) {
    if (mode > 2) {
        Serial.printf("[HID] Invalid capacity mode: %d, using default (mWh)\n", mode);
        capacity_mode = 1;
    } else {
        capacity_mode = mode;
    }
    // 立即更新静态 featureReport，确保主机获取到最新值
    featureReport.capacity_mode = capacity_mode;
    Serial.printf("[HID] CapacityMode set to: %d (0=mAh, 1=mWh, 2=%%)\n", capacity_mode);
    Serial.printf("[HID] featureReport.capacity_mode = %d\n", featureReport.capacity_mode);
}

void UPS_HID_Service::setDefaultSOH(float soh) {
    if (soh < 0.0f || soh > 100.0f) {
        Serial.printf(F("[HID] Invalid SOH value: %.1f%%, clamped to [0, 100]\n"), soh);
        default_soh = constrain(soh, 0.0f, 100.0f);
    } else {
        default_soh = soh;
    }
    Serial.printf(F("[HID] Default SOH set to: %.1f%%\n"), default_soh);
}

void UPS_HID_Service::setDeviceIdentifier(const char* identifier) {
    if (identifier && strlen(identifier) == 4) {
        device_identifier = identifier;
        Serial.printf(F("[HID] Device identifier set to: %s\n"), device_identifier);
    } else {
        device_identifier = "0000";
        Serial.println(F("[HID] Invalid identifier, using default: 0000"));
    }
}

// =============================================================================
// 私有方法
// =============================================================================

uint16_t UPS_HID_Service::_onGetDescriptor(uint8_t* dst) {
    memcpy(dst, ups_hid_report_descriptor, UPS_HID_REPORT_DESCRIPTOR_SIZE);
    return UPS_HID_REPORT_DESCRIPTOR_SIZE;
}

uint16_t UPS_HID_Service::_onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len) {
    uint8_t data[4];  // 最大 4 字节
    uint8_t data_len = 0;

    switch (report_id) {
        case HID_PD_IPRODUCT:
            data[0] = 2;
            data_len = 1;
            break;
        case HID_PD_SERIAL:
            data[0] = 3;
            data_len = 1;
            break;
        case HID_PD_MANUFACTURER:
            data[0] = 1;
            data_len = 1;
            break;
        // 循环次数
        case HID_PD_CYCLECOUNT:      // Report ID 4: CycleCount
            memcpy(data, &featureReport.cycle_count, sizeof(uint16_t));
            data_len = 2;
            break;
            
        // 状态报告
        case HID_PD_PRESENTSTATUS:   // Report ID 7: PresentStatus
            data[0] = (featureReport.charging << 0) |
                      (featureReport.discharging << 1) |
                      (featureReport.ac_present << 2) |
                      (featureReport.battery_present << 3) |
                      (featureReport.below_capacity_limit << 4) |
                      (featureReport.need_replacement << 5) |
                      (featureReport.shutdown_imminent << 6) |
                      (featureReport.overload << 7);
            data_len = 1;
            break;
            
        // 配置电压
        case HID_PD_CONFIGVOLTAGE:   // Report ID 10: ConfigVoltage
            {
                uint16_t voltage_cv = featureReport.config_voltage / 10;  // 毫伏转厘伏
                memcpy(data, &voltage_cv, sizeof(uint16_t));
                data_len = 2;
            }
            break;
            
        // 电压
        case HID_PD_VOLTAGE:         // Report ID 11: Voltage
            {
                uint16_t voltage_cv = featureReport.voltage / 10;  // 毫伏转厘伏
                memcpy(data, &voltage_cv, sizeof(uint16_t));
                data_len = 2;
            }
            break;
            
        // 剩余容量
        case HID_PD_REMAININGCAPACITY:  // Report ID 12: RemainingCapacity
            memcpy(data, &featureReport.remaining_capacity, sizeof(uint32_t));
            data_len = 4;
            break;
            
        // 运行时间
        case HID_PD_RUNTIMETOEMPTY:  // Report ID 13: RunTimeToEmpty
            memcpy(data, &featureReport.run_time_to_empty, sizeof(uint16_t));
            data_len = 2;
            break;
            
        // 满充容量
        case HID_PD_FULLCHRGECAPACITY:  // Report ID 14: FullChargeCapacity
            memcpy(data, &featureReport.full_charge_capacity, sizeof(uint32_t));
            data_len = 4;
            break;
            
        // 警告容量限制
        case HID_PD_WARNCAPACITYLIMIT:  // Report ID 15: WarningCapacityLimit
            memcpy(data, &featureReport.warning_capacity_limit, sizeof(uint32_t));
            data_len = 4;
            break;
            
        // 容量粒度 1
        case HID_PD_CPCTYGRANULARITY1:  // Report ID 16: CapacityGranularity1
            data[0] = featureReport.capacity_granularity1;
            data_len = 1;
            break;
            
        // 剩余容量限制
        case HID_PD_REMNCAPACITYLIMIT:  // Report ID 17: RemainingCapacityLimit
            memcpy(data, &featureReport.remaining_capacity_limit, sizeof(uint32_t));
            data_len = 4;
            break;
            
        // 容量模式
        case HID_PD_CAPACITYMODE:    // Report ID 22: CapacityMode
            data[0] = featureReport.capacity_mode;
            data_len = 1;
            break;
            
        // 设计容量
        case HID_PD_DESIGNCAPACITY:  // Report ID 23: DesignCapacity
            memcpy(data, &featureReport.design_capacity, sizeof(uint32_t));
            data_len = 4;
            break;
            
        // 容量粒度 2
        case HID_PD_CPCTYGRANULARITY2:  // Report ID 24: CapacityGranularity2
            data[0] = featureReport.capacity_granularity2;
            data_len = 1;
            break;
            
        // 可充电标志
        case HID_PD_RECHARGEABLE:    // Report ID 6: Rechargable
            data[0] = featureReport.rechargable;
            data_len = 1;
            break;
            
        default:
            // 静默忽略未知 report_id
            return 0;
    }

    //实际长度
    uint8_t actual_len = len < data_len ? len : data_len;
    // 复制数据到缓冲区
    memcpy(buffer, data, actual_len);
    
    return actual_len;
}

void UPS_HID_Service::_onSetFeature(uint8_t report_id, uint8_t const* buffer, uint16_t len) {
    // 预留：处理主机发来的 SET_FEATURE 命令（如关机请求）
    // 当前实现暂不处理，保持简洁
    (void)report_id;
    (void)buffer;
    (void)len;
}

void UPS_HID_Service::fillFeatureData(const System_Global_State& gs, float soh_override) {
    // 固定属性
    featureReport.capacity_mode = capacity_mode;
    featureReport.rechargable = 1;  // 可充电电池
    
    // 配置电压（串数 × 标称电压）
    featureReport.config_voltage = battery_series_count * battery_nominal_voltage;
    
    // 容量粒度（默认 1%）
    featureReport.capacity_granularity1 = 1;
    featureReport.capacity_granularity2 = 1;
    
    // 确定使用的 SOH 值：优先使用覆盖参数，其次使用 gs.bms.soh，最后使用 default_soh
    float effective_soh;
    if (soh_override >= 0.0f && soh_override <= 100.0f) {
        effective_soh = soh_override;
    } else {
        effective_soh = (gs.bms.soh > 0.0f && gs.bms.soh <= 100.0f) ? gs.bms.soh : default_soh;
    }
    
    // 根据容量模式计算容量相关字段
    if (capacity_mode == 0) {
        // mAh 模式：直接使用毫安时数据
        featureReport.design_capacity = (uint32_t)battery_design_capacity;
        featureReport.full_charge_capacity = (uint32_t)((float)battery_design_capacity * effective_soh / 100.0f);
        featureReport.warning_capacity_limit = (uint32_t)((float)battery_design_capacity * 1 / 100.0f);  // 1% 警告阈值
        featureReport.remaining_capacity_limit = (uint32_t)((float)battery_design_capacity * 5 / 100.0f);  // 5% 最低阈值
        featureReport.remaining_capacity = (uint32_t)gs.bms.capacity_remaining;
        
    } else if (capacity_mode == 1) {
        // mWh 模式：毫安时 × 电池组总电压 (V)
        float pack_voltage_V = (float)(battery_series_count * battery_nominal_voltage) / 1000.0f;
        featureReport.design_capacity = (uint32_t)((float)battery_design_capacity * pack_voltage_V);
        featureReport.full_charge_capacity = (uint32_t)((float)battery_design_capacity * effective_soh / 100.0f * pack_voltage_V);
        featureReport.warning_capacity_limit = (uint32_t)((float)battery_design_capacity * 1 / 100.0f * pack_voltage_V);  // 1% 警告阈值
        featureReport.remaining_capacity_limit = (uint32_t)((float)battery_design_capacity * 5 / 100.0f * pack_voltage_V);  // 5% 最低阈值
        featureReport.remaining_capacity = (uint32_t)((float)gs.bms.capacity_remaining * pack_voltage_V);
        
    } else {
        // 百分比模式
        featureReport.design_capacity = 100;
        featureReport.full_charge_capacity = 100;
        featureReport.warning_capacity_limit = 1;  // 1% 警告阈值
        featureReport.remaining_capacity_limit = 5;  // 5% 最低阈值
        featureReport.remaining_capacity = (uint32_t)gs.bms.soc;
    }
    
    // 电压（当前电池电压，单位 mV）
    featureReport.voltage = (uint16_t)gs.bms.voltage;
    
    // 剩余运行时间（单位秒，基于当前电流和剩余容量估算）
    if (gs.bms.current < -5) {
        // 放电状态，估算剩余时间
        featureReport.run_time_to_empty = (uint16_t)((float)gs.bms.capacity_remaining / (float)(-gs.bms.current) * 3600.0f);
    } else {
        // 充电或空闲状态
        featureReport.run_time_to_empty = 1800;
    }
    
    // 状态字段（基于 gs 数据）
    if (gs.bms.soh > 0) {
        featureReport.cycle_count = (uint16_t)(gs.bms.cycle_count + 1); //有时候初始情况是 0
    }
    
    // PresentStatus 状态位
    featureReport.charging = (gs.bms.current >= 5) ? 1 : 0;
    featureReport.discharging = (gs.bms.current < -5) ? 1 : 0;
    featureReport.ac_present = gs.power.ac_present ? 1 : 0;
    featureReport.battery_present = gs.bms.is_connected ? 1 : 0;
    featureReport.below_capacity_limit = (gs.bms.soc < 15) ? 1 : 0;
    featureReport.need_replacement = (gs.bms.soh < 60) ? 1 : 0;
    featureReport.shutdown_imminent = (gs.bms.soc < 3) ? 1 : 0;
    featureReport.overload = (gs.bms.temperature > 60.0f) ? 1 : 0;
}

bool UPS_HID_Service::sendReport() {
    if (!tud_hid_ready()) {
        return false;
    }

    bool ok = false;

    // 根据 reportSendIndex 轮流发送四个 report
    switch (reportSendIndex) {
        case 0:
            // 发送 HID_PD_REMAININGCAPACITY (Report ID 12)
            {
                uint32_t data = featureReport.remaining_capacity;
                ok = tud_hid_report(HID_PD_REMAININGCAPACITY, &data, sizeof(data));
            }
            break;
            
        case 1:
            // 发送 HID_PD_RUNTIMETOEMPTY (Report ID 13)
            {
                uint16_t data = featureReport.run_time_to_empty;
                ok = tud_hid_report(HID_PD_RUNTIMETOEMPTY, &data, sizeof(data));
            }
            break;
            
        case 2:
            // 发送 HID_PD_PRESENTSTATUS (Report ID 7)
            {
                uint8_t data = (featureReport.charging << 0) |
                               (featureReport.discharging << 1) |
                               (featureReport.ac_present << 2) |
                               (featureReport.battery_present << 3) |
                               (featureReport.below_capacity_limit << 4) |
                               (featureReport.need_replacement << 5) |
                               (featureReport.shutdown_imminent << 6) |
                               (featureReport.overload << 7);
                ok = tud_hid_report(HID_PD_PRESENTSTATUS, &data, sizeof(data));
            }
            break;
            
        case 3:
            // 发送 HID_PD_VOLTAGE (Report ID 11)
            {
                uint16_t data = featureReport.voltage / 10;  // 毫伏转厘伏
                ok = tud_hid_report(HID_PD_VOLTAGE, &data, sizeof(data));
            }
            break;
    }
    
    // 更新发送索引，轮流进行 (0 -> 1 -> 2 -> 3 -> 0 -> ...)
    reportSendIndex = (reportSendIndex + 1) % 4;

    return ok;
}

void UPS_HID_Service::handleConnectionStatus() {
    bool was = connected;
    connected = initialized && tud_mounted();

    if (connected != was) {
        if (connected) {
            Serial.println(F("[HID] +++ CONNECTED"));
            lastReportTime = 0;
        } else {
            Serial.println(F("[HID] --- DISCONNECTED"));
        }
    }
}

// =============================================================================
// 事件回调处理
// =============================================================================

void UPS_HID_Service::onHidConfigChanged(EventType type, void* param) {
    (void)type;  // 未使用参数
    extern UPS_HID_Service* upsHidService;
    if (upsHidService  == nullptr) {
        Serial.println(F("[HID] Error: Global instance pointer is null"));
        return;
    }

    if (!upsHidService || !param) {
        return;
    }
    
    // 转发到实例方法处理
    upsHidService->handleConfigChange(static_cast<Configuration*>(param));
}

void UPS_HID_Service::handleConfigChange(const Configuration* newConfig) {
    if (!newConfig) {
        return;
    }
    
    Serial.println(F("[HID] Configuration changed event received"));
    

    // 更新容量模式
    setCapacityMode(newConfig->hid_report_mode);
    
    // 注意：电池配置（串数、标称电压、设计容量）通常来自 BMS 配置
    // 暂不支持热更新，最好重启设备。也不支持热关闭，直接重启咯。
    // 目前保持现有的电池配置不变
    
    Serial.printf(F("[HID] Config updated: identifier=%s, mode=%d\n"), 
                  newConfig->identifier, newConfig->hid_report_mode);
    
    // 标记需要软重启
    pendingRestart = true;
    Serial.println(F("[HID] USB hot restart requested"));
}

// =============================================================================
// USB 热重启处理
// =============================================================================

void UPS_HID_Service::handleUsbHotRestart() {
    // 阶段 1: 检测是否需要断开 USB
    if (pendingRestart) {
        Serial.println(F("[HID] Disconnecting USB..."));
        tud_disconnect();
        usbStopTime = millis();
        needReconnect = true;
        pendingRestart = false;
        connected = false;
        return;
    }
    
    // 阶段 2: 检测是否需要重新连接 USB
    if (needReconnect && (millis() - usbStopTime >= 1000)) {
        Serial.println(F("[HID] Reconnecting USB..."));
        
        // 重置状态
        initialized = false;
        connected = false;
        tud_connect();
        
        // 清除标志
        needReconnect = false;
        initialized = true;
        
        Serial.println(F("[HID] USB reconnected successfully"));
    }
}
