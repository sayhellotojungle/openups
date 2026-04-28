#ifndef HARDWARE_INTERFACE_H
#define HARDWARE_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "pins_config.h"
#include "i2c_interface.h"
#include <FastLED.h>
#include "event_bus.h"
#include "data_structures.h"
#include <Preferences.h>

// NTC lookup tables
const uint16_t NTC_TEMP_TABLE_BOARD[] = {
    583, 607, 632, 657, 683, 710, 737, 764, 792, 821,
    850, 879, 909, 939, 970, 1001, 1032, 1063, 1095, 1127,
    1160, 1192, 1225, 1257, 1290, 1323, 1356, 1389, 1422, 1455,
    1488, 1520, 1553, 1586, 1618, 1650, 1682, 1714, 1745, 1776,
    1807, 1838, 1868, 1898, 1927, 1956, 1985, 2013, 2041, 2069,
    2096, 2123, 2149, 2175, 2200, 2225, 2249, 2273, 2297, 2320,
    2343, 2365, 2387, 2408, 2429, 2450, 2470, 2489, 2508, 2527,
    2545, 2563, 2581, 2598, 2614, 2631, 2647, 2662, 2677, 2692,
    2707, 2721, 2734, 2748, 2761, 2774, 2786, 2798, 2810, 2822,
    2833, 2844, 2854, 2865, 2875, 2885, 2894, 2904, 2913, 2922,
    2931, 2939, 2947, 2955, 2963, 2971,
};

const uint16_t NTC_TEMP_TABLE_SYS[] = {
    484, 507, 532, 558, 584, 611, 639, 667, 696, 726,
    757, 788, 820, 852, 885, 919, 953, 987, 1022, 1058,
    1094, 1130, 1166, 1203, 1240, 1277, 1315, 1352, 1389, 1427,
    1464, 1502, 1539, 1576, 1613, 1650, 1687, 1723, 1759, 1794,
    1830, 1864, 1899, 1933, 1966, 1999, 2032, 2064, 2095, 2126,
    2157, 2186, 2216, 2244, 2272, 2300, 2327, 2353, 2379, 2404,
    2429, 2453, 2476, 2499, 2521, 2543, 2564, 2584, 2604, 2624,
    2643, 2661, 2679, 2697, 2714, 2730, 2746, 2762, 2777, 2792,
    2806, 2820, 2834, 2847, 2859, 2872, 2884, 2895, 2906, 2917,
    2928, 2938, 2948, 2958, 2967, 2977, 2985, 2994, 3002, 3010,
    3018, 3026, 3033, 3040, 3047, 3054,
};

// LED pin mapping
static const uint8_t LED_PINS[] = {
    POWER_LED_PIN, CHARGING_LED_PIN, DISCHARGING_LED_PIN,
    WIFI_FAIL_LED_PIN, WIFI_SUCCESS_LED_PIN
};

static const uint8_t LED_COUNT = sizeof(LED_PINS) / sizeof(LED_PINS[0]);

// LED modes
typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_BLINK_SLOW,
    LED_MODE_BLINK_FAST,
    LED_MODE_PULSE
} LED_Mode_t;

// RGB color
typedef struct {
    uint8_t r, g, b;
} RGB_Color_t;

// RGB modes
typedef enum {
    RGB_MODE_OFF = 0,
    RGB_MODE_ON,
    RGB_MODE_BREATHING,
    RGB_MODE_BLINKING,
    RGB_MODE_RAINBOW,
} RGB_Mode_t;

// Buzzer modes
typedef enum {
    BUZZER_MODE_OFF = 0,
    BUZZER_MODE_BEEP_ONCE,
    BUZZER_MODE_BEEP_DOUBLE,
    BUZZER_MODE_BEEP_TRIPLE,
    BUZZER_MODE_CONTINUOUS,
    BUZZER_MODE_ALARM
} Buzzer_Mode_t;

// ADC calibration coefficient constants
// Coefficient range: 50-255, representing 0.50-2.55x multiplier
// Default value 100 represents 1.00x (no calibration)
// Formula: multiplier = coefficient / 100.0
#define ADC_CAL_MIN_VALUE       50
#define ADC_CAL_MAX_VALUE       255
#define ADC_CAL_DEFAULT_VALUE   100
#define ADC_CAL_NVS_NAMESPACE   "adc_cal"

// ADC pins that support calibration
static const uint8_t ADC_CAL_PINS[] = {
    BQ24780S_IADP_PIN,
    BQ24780S_IDCHG_PIN,
    BQ24780S_PMON_PIN,
    INPUT_VOLTAGE_PIN,
    BATTERY_VOLTAGE_PIN,
    BOARD_TEMP_PIN,
    ENVIRONMENT_TEMP_PIN
};
static const uint8_t ADC_CAL_PIN_COUNT = sizeof(ADC_CAL_PINS) / sizeof(ADC_CAL_PINS[0]);

class HardwareInterface {
public:
    explicit HardwareInterface(uint8_t led_brightness = 50, uint8_t buzzer_volume = 50, bool buzzer_enabled = true);
    explicit HardwareInterface(const Configuration& config);
    
    bool begin();
    void update();
    
    // GPIO & Sensors
    bool readGPIO(uint8_t pin);
    void writeGPIO(uint8_t pin, bool value);
    uint16_t readVoltage(uint8_t pin, float divider_ratio = 1.0f);
    float readNTCTemperature(uint8_t pin, const uint16_t* table, size_t table_size);
    float readBoardTemperature();
    float readEnvironmentTemperature();
    
    // ADC Calibration
    void setADCCalibration(uint8_t pin, uint8_t coefficient);
    const uint8_t* getADCCalibration() const { return adc_calibration_; }
    
    // LED Control
    void setLED(uint8_t pin, LED_Mode_t mode);
    void setLEDBrightness(uint8_t brightness);
    void setRGBLED(RGB_Mode_t mode, RGB_Color_t color = {0, 0, 0});
    
    // Buzzer Control
    void setBuzzer(Buzzer_Mode_t mode);
    void setBuzzerVolume(uint8_t volume);
    void setBuzzerEnabled(bool enabled);
    uint8_t getBuzzerVolume() const { return buzzer_volume_; }
    bool isBuzzerEnabled() const { return buzzer_enabled_; }
    
    I2CInterface& getI2CInterface() { return i2cInterface_; }
    const I2CInterface& getI2CInterface() const { return i2cInterface_; }
    
    void syncInitialState();

private:
    CRGB leds_[1];
    I2CInterface i2cInterface_;
    uint32_t system_status_;
    
    LED_Mode_t led_modes_[5];
    uint8_t led_brightness_;
    uint8_t buzzer_volume_;
    bool buzzer_enabled_;
    RGB_Mode_t rgb_mode_;
    RGB_Color_t rgb_color_;
    Buzzer_Mode_t buzzer_mode_;
    
    unsigned long buzzer_last_time_;
    uint8_t buzzer_beep_count_;
    bool buzzer_beep_on_;
    bool buzzer_state_;
    
    unsigned long last_update_time_;
    unsigned long last_led_update_time_;
    unsigned long button_press_start_time_;
    
    volatile bool flag_acok_changed_;
    volatile bool flag_prochot_alert_;
    volatile bool flag_tbstat_changed_;
    volatile bool flag_bms_alert_;
    volatile bool flag_button_pressed_;
    
    bool last_acok_state_;
    bool last_prochot_state_;
    bool last_tbstat_state_;
    
    enum ButtonState { BTN_IDLE, BTN_PRESSED, BTN_DEBOUNCING, BTN_WAITING_RELEASE };
    ButtonState button_state_;
    
    // ADC calibration coefficients (one per calibrated pin)
    uint8_t adc_calibration_[ADC_CAL_PIN_COUNT];
    Preferences adc_cal_preferences_;
    
    void initGPIOs();
    void initADCCalibration();
    void processInterruptFlags();
    void processButtonLogic();
    void updateLEDs();
    void updateRGBLED();
    void updateBuzzer();
    void updateLED(uint8_t pin, LED_Mode_t mode, unsigned long current_time);
    uint32_t filterADCRead(uint8_t pin);
    int8_t findCalibrationIndex(uint8_t pin) const;
    static int8_t pinToIndex(uint8_t pin);
    
    static void IRAM_ATTR handleAcokInterrupt();
    static void IRAM_ATTR handleProchotInterrupt();
    static void IRAM_ATTR handleTbstatInterrupt();
    static void IRAM_ATTR handleBmsAlertInterrupt();
    static void IRAM_ATTR handleButtonInterrupt();
};

#endif