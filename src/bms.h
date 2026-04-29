#ifndef BMS_H
#define BMS_H

#include <stdint.h>
#include <stdbool.h>
#include <Preferences.h>
#include "bq76920.h"
#include "i2c_interface.h"
#include "data_structures.h"
#include "event_types.h"
#include "pins_config.h"

#define BMS_PREFS_NAMESPACE     "bms_data"
#define PREFS_KEY_SOH           "soh"
#define PREFS_KEY_CYCLES        "cycles"
#define PREFS_KEY_LAST_FAULT    "last_fault"
#define PREFS_KEY_BAL_EVENTS    "bal_events"
#define PREFS_KEY_LAST_FAULT_TIME "last_fault_time"
#define PREFS_KEY_REMAINING_CAP "bms_rem_cap"

// OCV-SOC lookup table (NCM)
const uint16_t OCV_SOC_TABLE[21][2] = {
    {4200, 100}, {4150, 96}, {4100, 91}, {4050, 87}, {4000, 82},
    {3950, 78},  {3900, 73}, {3850, 68}, {3810, 63}, {3780, 58},
    {3750, 52},  {3720, 47}, {3690, 41}, {3660, 35}, {3630, 28},
    {3590, 21},  {3540, 15}, {3480, 10}, {3350, 5},  {3150, 2},
    {3000, 0}
};
const int OCV_TABLE_SIZE = sizeof(OCV_SOC_TABLE) / sizeof(OCV_SOC_TABLE[0]);

typedef enum {
    BMS_MODE_NORMAL = 0,
    BMS_MODE_FAULT,
} BMS_Mode_t;

typedef struct {
    float soh;
    uint32_t total_cycles;
    uint64_t balancing_events;
    BMS_Fault_t last_fault;
    uint32_t last_fault_time;
} BMS_Statistics_t;

// SOH Learning Context for capacity-based SOH estimation
typedef struct {
    bool is_learning;
    float soc_start;            // 学习开始时的 SOC (%)
    float ah_start;             // 学习开始时的原始库仑计累积值 (mAh)
    unsigned long learning_start_time;
} SOH_Learning_Context_t;

typedef struct {
    uint8_t cell_count;
    uint32_t nominal_capacity_mAh;
    uint16_t cell_ov_threshold;
    uint16_t cell_uv_threshold;
    uint16_t cell_ov_recover;
    uint16_t cell_uv_recover;
    uint16_t max_charge_current;
    uint16_t max_discharge_current;
    uint16_t short_circuit_threshold;
    float temp_overheat_threshold;
    bool balancing_enabled;
    float balancing_voltage_diff;
} BMS_Config_t;

class BMS {
public:
    explicit BMS(I2CInterface& i2c_interface, const BMS_Config_t& config);
    
    bool begin();
    void update(System_Global_State& globalState);
    
    // 声明静态指针，用于存储当前实例
    static BMS* instancePtr;
    
private:
    void processAlertStatus(uint8_t fault_reg);
    
public:

    // DFET Control
    bool disableDischarge();
    bool enableDischarge();
    bool isDischargeEnabled() const;

    // CFET Control
    bool disableCharge();
    bool enableCharge();
    bool isChargeEnabled() const;

    bool enterShipMode();
    bool setBalancingEnabled(bool enable);
    bool isInitialized() const { return initialized_; }
    bool startBalancing(const BMS_State& bmsState);
    bool stopBalancing();
    bool clearFault();
    bool emergencyShutdown();
    BMS_Fault_t translateChipFault(uint8_t fault_register);
    bool saveToStorage();
    bool loadFromStorage();
    static BMS_Config_t getDefaultConfig(uint8_t cell_count);
    bool applyNewConfig(const BMS_Config_t& config);
    void applyPendingConfig();

    BQ76920 bq76920_;
    BMS_Config_t config_;
    BMS_Statistics_t stats_;
    Preferences preferences_;
    
    bool initialized_;
    bool available_;
    bool discharge_enabled_;
    bool charge_enabled_;
    uint8_t i2c_failure_count_;
    bool i2c_power_enabled_;
    
    unsigned long last_fast_update_;
    unsigned long last_medium_update_;
    unsigned long last_slow_update_;
    unsigned long last_periodic_update_;
    unsigned long last_day_update_;
    unsigned long last_cc_update_;
    
    float current_remaining_capacity;
    float accumulated_charge_mAh;
    float accumulated_discharge_mAh;
    bool cc_ready_pending_;
    
    bool soc_initialized_;
    float last_stable_soc_;
    unsigned long last_soc_update_timestamp_;
    float self_discharge_rate_per_day_;
    
    // SOH Learning context
    SOH_Learning_Context_t soh_learning_ctx_;
    unsigned long soc_stable_start_time_;
    bool soc_waiting_for_stable_;

    BMS_Fault_t hardware_fault_wait_;
    
    // 满充/放空校准锚点
    bool full_charge_calibrated_;
    bool empty_discharge_calibrated_;
    unsigned long last_full_charge_time_;
    unsigned long last_empty_discharge_time_;
    
    // SOH学习专用的原始库仑计累积(不受SOH缩放影响)
    float cc_accumulated_raw_mAh_;
    
    // 异步配置更新标记
    bool config_update_pending_;
    BMS_Config_t pending_config_;
    
    void updateSOC(BMS_State& bmsState);
    void updateFaultLogic(BMS_State& bmsState);
    void checkCriticalFaults(BMS_State& bmsState);
    void evaluateAndExecuteBalancing(BMS_State& bmsState);
    bool updateBasicInfo(BMS_State& bmsState);
    bool validateConfig(const BMS_Config_t& config);
    
    void handleBmsAlertInterrupt(uint8_t call_tag = 0);
    void handleCommunicationLoss(BMS_State& bmsState);
    
    float calculateSOC_Voltage(const BMS_State& bmsState);
    float calculateSOC_FromVoltage(uint16_t voltage_mv);
    float calculateSOC_Coulomb();
    float getAvailableCapacity() const;
    float getTemperatureCompensatedCapacity(float temperature) const;
    void updateTemporarySOH(BMS_State& bmsState);
    
    bool processCoulombCounterData();
    void compensateSelfDischarge(unsigned long delta_time_ms);
    void detectFullChargeCalibration(BMS_State& bmsState);
    void detectEmptyDischargeCalibration(BMS_State& bmsState);
    void updateSOHLearning(BMS_State& bmsState);
    BQ76920_InitConfig generateChipConfig(const BMS_Config_t& config);
    
    // I2C隔离芯片电源控制
    inline void i2cPowerOn() {
        if (!i2c_power_enabled_) {
            digitalWrite(BQ76920_I2CVCC_PIN, HIGH);
            delayMicroseconds(1200);
            i2c_power_enabled_ = true;
        }
    }
    inline void i2cPowerOff() {
        if (i2c_power_enabled_) {
            digitalWrite(BQ76920_I2CVCC_PIN, LOW);
            i2c_power_enabled_ = false;
        }
    }

    unsigned long cell_balance_timer[5];
    static const unsigned long BALANCE_COUNT_INTERVAL = 600000;
};

#endif