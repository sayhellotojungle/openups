#include "hardware_interface.h"
#include "utils.h"
#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

static HardwareInterface* g_hardware_instance = nullptr;

HardwareInterface::HardwareInterface(uint8_t led_brightness, uint8_t buzzer_volume, bool buzzer_enabled)
    : i2cInterface_(), system_status_(0), last_update_time_(0), last_led_update_time_(0),
      button_press_start_time_(0), flag_acok_changed_(false), flag_prochot_alert_(false),
      flag_tbstat_changed_(false), flag_bms_alert_(false), flag_button_pressed_(false),
      button_state_(BTN_IDLE), last_acok_state_(false), last_prochot_state_(false),
      last_tbstat_state_(false), rgb_mode_(RGB_MODE_OFF), rgb_color_{0, 0, 0},
      buzzer_mode_(BUZZER_MODE_BEEP_ONCE) {
    
    led_brightness_ = Utils::applyGammaMapping(min(led_brightness, (uint8_t)100), 80);
    buzzer_volume_ = Utils::applyGammaMapping(min(buzzer_volume, (uint8_t)100), 150);
    buzzer_enabled_ = buzzer_enabled;
    
    memset(led_modes_, LED_MODE_OFF, sizeof(led_modes_));
    
    // Initialize ADC calibration coefficients to default (100 = 1.00x)
    memset(adc_calibration_, ADC_CAL_DEFAULT_VALUE, sizeof(adc_calibration_));
    
    // Initialize Preferences for ADC calibration storage
    adc_cal_preferences_.begin(ADC_CAL_NVS_NAMESPACE, false);
    
    g_hardware_instance = this;
    
    Serial.printf_P(PSTR("HW: brightness=%u%%->%u, volume=%u%%->%u, enabled=%s\n"),
                   min(led_brightness, (uint8_t)100), led_brightness_,
                   min(buzzer_volume, (uint8_t)100), buzzer_volume_,
                   buzzer_enabled_ ? "yes" : "no");
}

HardwareInterface::HardwareInterface(const Configuration& config)
    : HardwareInterface(config.led_brightness, config.buzzer_volume, config.buzzer_enabled) {}

bool HardwareInterface::begin() {
    Serial.println(F("HW: Initializing..."));
    
    initGPIOs();

    if (!i2cInterface_.initialize(I2C_SDA_PIN, I2C_SCL_PIN, 100000)) {
        Serial.println(F("HW: I2C init failed"));
        return false;
    }

    //i2cInterface_.scanI2CBus();
    
    attachInterrupt(digitalPinToInterrupt(BQ24780S_ACOK_PIN), handleAcokInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BQ24780S_PROCHOT_PIN), handleProchotInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BQ24780S_TBSTAT_PIN), handleTbstatInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BQ76920_ALERT_PIN), handleBmsAlertInterrupt, RISING); 
    attachInterrupt(digitalPinToInterrupt(RESET_BUTTON_PIN), handleButtonInterrupt, FALLING);
    
    // Initialize ADC calibration from Preferences
    initADCCalibration();
    
    Serial.println(F("HW: Init complete"));
    return true;
}

void HardwareInterface::initADCCalibration() {
    Serial.println(F("HW: Loading ADC calibration from Preferences..."));
    bool any_loaded = false;
    
    for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "pin_%u", ADC_CAL_PINS[i]);
        
        uint32_t cal_value = adc_cal_preferences_.getUInt(key, ADC_CAL_DEFAULT_VALUE);
        if (cal_value >= ADC_CAL_MIN_VALUE && cal_value <= ADC_CAL_MAX_VALUE) {
            adc_calibration_[i] = (uint8_t)cal_value;
            any_loaded = true;
            Serial.printf_P(PSTR("HW: ADC cal pin %u -> %u (%.2fx)\n"),
                           ADC_CAL_PINS[i], adc_calibration_[i], adc_calibration_[i] / 100.0f);
        }
    }
    
    if (any_loaded) {
        Serial.println(F("HW: ADC calibration loaded from Preferences"));
    } else {
        Serial.println(F("HW: No valid ADC calibration found, using defaults"));
    }
}

void HardwareInterface::initGPIOs() {
    // Input pins
    pinMode(BQ24780S_ACOK_PIN, INPUT);      
    pinMode(BQ24780S_PROCHOT_PIN, INPUT);   
    pinMode(BQ24780S_TBSTAT_PIN, INPUT); 
    pinMode(BQ76920_ALERT_PIN, INPUT); 
    
    // Output pins
    const uint8_t output_pins[] = {
        BQ76920_I2CVCC_PIN, POWER_LED_PIN, CHARGING_LED_PIN, DISCHARGING_LED_PIN,
        WIFI_FAIL_LED_PIN, WIFI_SUCCESS_LED_PIN, BUZZER_PIN, RGB_LED_PIN,
        TEMP_POWER_PIN
    };
    
    for (uint8_t pin : output_pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }

    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds_, 1);
    FastLED.setBrightness(led_brightness_); 
    
    analogSetAttenuation(ADC_11db); 
    analogReadResolution(12);
    
    Serial.println(F("HW: GPIOs configured"));
}

void HardwareInterface::syncInitialState() {
    Serial.println(F("HW: Syncing states..."));
    
    bool acok_state = digitalRead(BQ24780S_ACOK_PIN);
    bool prochot_state = digitalRead(BQ24780S_PROCHOT_PIN);
    bool tbstat_state = digitalRead(BQ24780S_TBSTAT_PIN);
    
    Serial.printf_P(PSTR("States - ACOK:%d PROCHOT:%d TBSTAT:%d\n"),
                   acok_state, prochot_state, tbstat_state);
    
    last_acok_state_ = acok_state;
    last_prochot_state_ = prochot_state;
    last_tbstat_state_ = tbstat_state;
    
    EventBus::getInstance().publish(EVT_HW_ACOK_CHANGED, &acok_state);
    EventBus::getInstance().publish(EVT_HW_PROCHOT_ALERT, &prochot_state);
    EventBus::getInstance().publish(EVT_HW_TBSTAT_CHANGED, &tbstat_state);
}

void HardwareInterface::update() {
    unsigned long current_time = millis();
    
    if (current_time - last_led_update_time_ >= 50) {
        updateLEDs();
        updateRGBLED();
        updateBuzzer();
        last_led_update_time_ = current_time;
    }
    
    processInterruptFlags();
    processButtonLogic();
}

bool HardwareInterface::readGPIO(uint8_t pin) {
    return digitalRead(pin) == HIGH;
}

void HardwareInterface::writeGPIO(uint8_t pin, bool value) {
    digitalWrite(pin, value ? HIGH : LOW);
}

uint16_t HardwareInterface::readVoltage(uint8_t pin, float divider_ratio) {
    return (uint16_t)(filterADCRead(pin) * divider_ratio);
}

float HardwareInterface::readNTCTemperature(uint8_t pin, const uint16_t* table, size_t table_size) {
    uint32_t adc_val = filterADCRead(pin);

    if (table_size < 2) return 25.0f;
    if (adc_val <= table[0]) return -10.0f;
    if (adc_val >= table[table_size - 1]) return -10.0f + (float)(table_size - 1);

    int left = 0, right = (int)table_size - 1;
    while (left < right - 1) {
        int mid = left + (right - left) / 2;
        if (table[mid] == adc_val) return -10.0f + (float)mid;
        else if (table[mid] < adc_val) left = mid;
        else right = mid;
    }
    
    float ratio = (float)(adc_val - table[left]) / (float)(table[right] - table[left]);
    return -10.0f + (float)left + ratio;
}

uint32_t HardwareInterface::filterADCRead(uint8_t pin) {
    uint32_t burst_sum = 0;
    for(int i = 0; i < 10; i++) {
        burst_sum += analogReadMilliVolts(pin);
    }
    uint32_t raw_value = burst_sum / 10;
    
    // Apply calibration coefficient if this is a calibrated pin
    int8_t idx = findCalibrationIndex(pin);
    if (idx >= 0) {
        // calibration coefficient: value/100
        // result = raw * cal / 100
        raw_value = (raw_value * adc_calibration_[idx]) / 100;
    }
    
    return raw_value;
}

int8_t HardwareInterface::findCalibrationIndex(uint8_t pin) const {
    for (uint8_t i = 0; i < ADC_CAL_PIN_COUNT; i++) {
        if (ADC_CAL_PINS[i] == pin) {
            return i;
        }
    }
    return -1;
}

// ADC Calibration public API
void HardwareInterface::setADCCalibration(uint8_t pin, uint8_t coefficient) {
    // Clamp to valid range
    if (coefficient < ADC_CAL_MIN_VALUE) coefficient = ADC_CAL_MIN_VALUE;
    if (coefficient > ADC_CAL_MAX_VALUE) coefficient = ADC_CAL_MAX_VALUE;
    
    int8_t idx = findCalibrationIndex(pin);
    if (idx < 0) {
        Serial.printf_P(PSTR("HW: Warning - pin %u is not a calibratable ADC pin\n"), pin);
        return;
    }
    
    adc_calibration_[idx] = coefficient;
    
    // Save to Preferences
    char key[16];
    snprintf(key, sizeof(key), "pin_%u", pin);
    adc_cal_preferences_.putUInt(key, coefficient);
    
    Serial.printf_P(PSTR("HW: ADC cal pin %u -> %u (%.2fx) saved\n"),
                   pin, coefficient, coefficient / 100.0f);
}

// Interrupt handlers
void IRAM_ATTR HardwareInterface::handleAcokInterrupt() {
    if (g_hardware_instance) g_hardware_instance->flag_acok_changed_ = true;
}

void IRAM_ATTR HardwareInterface::handleProchotInterrupt() {
    if (g_hardware_instance) g_hardware_instance->flag_prochot_alert_ = true;
}

void IRAM_ATTR HardwareInterface::handleTbstatInterrupt() {
    if (g_hardware_instance) g_hardware_instance->flag_tbstat_changed_ = true;
}

void IRAM_ATTR HardwareInterface::handleBmsAlertInterrupt() {
    if (g_hardware_instance) g_hardware_instance->flag_bms_alert_ = true;
}

void IRAM_ATTR HardwareInterface::handleButtonInterrupt() {
    if (g_hardware_instance) g_hardware_instance->flag_button_pressed_ = true;
}

void HardwareInterface::processInterruptFlags() {
    if (flag_acok_changed_) {
        flag_acok_changed_ = false;
        bool state = digitalRead(BQ24780S_ACOK_PIN);
        last_acok_state_ = state;
        EventBus::getInstance().publish(EVT_HW_ACOK_CHANGED, &state);
    }
    
    if (flag_prochot_alert_) {
        flag_prochot_alert_ = false;
        bool state = digitalRead(BQ24780S_PROCHOT_PIN);
        last_prochot_state_ = state;
        EventBus::getInstance().publish(EVT_HW_PROCHOT_ALERT, &state);
    }
    
    if (flag_tbstat_changed_) {
        flag_tbstat_changed_ = false;
        bool state = digitalRead(BQ24780S_TBSTAT_PIN);
        last_tbstat_state_ = state;
        EventBus::getInstance().publish(EVT_HW_TBSTAT_CHANGED, &state);
    }
    
    if (flag_bms_alert_) {
        flag_bms_alert_ = false;
        EventBus::getInstance().publish(EVT_BMS_ALARM_INTERRUPT, nullptr);
    }
    
    if (flag_button_pressed_) {
        flag_button_pressed_ = false;
    }
}

void HardwareInterface::processButtonLogic() {
    unsigned long current_time = millis();
    bool button_pressed = !digitalRead(RESET_BUTTON_PIN);
    
    switch (button_state_) {
        case BTN_IDLE:
            if (button_pressed) {
                button_state_ = BTN_DEBOUNCING;
                button_press_start_time_ = current_time;
            }
            break;
            
        case BTN_DEBOUNCING:
            if (current_time - button_press_start_time_ >= 50) {
                button_state_ = button_pressed ? BTN_PRESSED : BTN_IDLE;
                if (button_pressed) button_press_start_time_ = current_time;
            }
            break;
            
        case BTN_PRESSED:
            if (!button_pressed) {
                unsigned long duration = current_time - button_press_start_time_;
                if (duration >= 3000) {
                    EventBus::getInstance().publish(EVT_BTN_LONG_PRESS, nullptr);
                    Serial.println(F("[BTN] Long press"));
                } else if (duration >= 50) {
                    EventBus::getInstance().publish(EVT_BTN_SHORT_PRESS, nullptr);
                    Serial.println(F("[BTN] Short press"));
                }
                button_state_ = BTN_WAITING_RELEASE;
            } else if (current_time - button_press_start_time_ >= 3000) {
                EventBus::getInstance().publish(EVT_BTN_LONG_PRESS, nullptr);
                Serial.println(F("[BTN] Long hold"));
                button_state_ = BTN_WAITING_RELEASE;
            }
            break;
            
        case BTN_WAITING_RELEASE:
            if (!button_pressed) button_state_ = BTN_IDLE;
            break;
    }
}

float HardwareInterface::readBoardTemperature() {
    digitalWrite(TEMP_POWER_PIN, HIGH);
    delayMicroseconds(1000);
    float temp = readNTCTemperature(BOARD_TEMP_PIN, NTC_TEMP_TABLE_BOARD, 
                                    sizeof(NTC_TEMP_TABLE_BOARD) / sizeof(NTC_TEMP_TABLE_BOARD[0]));
    digitalWrite(TEMP_POWER_PIN, LOW);
    return temp;
}

float HardwareInterface::readEnvironmentTemperature() {
    digitalWrite(TEMP_POWER_PIN, HIGH);
    delayMicroseconds(1000);
    float temp = readNTCTemperature(ENVIRONMENT_TEMP_PIN, NTC_TEMP_TABLE_SYS,
                                    sizeof(NTC_TEMP_TABLE_SYS) / sizeof(NTC_TEMP_TABLE_SYS[0]));
    digitalWrite(TEMP_POWER_PIN, LOW);
    return temp;
}

void HardwareInterface::setLED(uint8_t pin, LED_Mode_t mode) {
    int8_t index = pinToIndex(pin);
    if (index >= 0) led_modes_[index] = mode;
}

void HardwareInterface::setLEDBrightness(uint8_t brightness) {
    brightness = min(brightness, (uint8_t)100);
    led_brightness_ = Utils::applyGammaMapping(brightness, 80);
    FastLED.setBrightness(led_brightness_);
    Serial.printf_P(PSTR("[HW] LED Brightness: %u%% -> %u\n"), brightness, led_brightness_);
}

void HardwareInterface::setBuzzerVolume(uint8_t volume) {
    volume = min(volume, (uint8_t)100);
    buzzer_volume_ = Utils::applyGammaMapping(volume, 150);
    if (buzzer_mode_ != BUZZER_MODE_OFF && buzzer_enabled_) {
        analogWrite(BUZZER_PIN, buzzer_volume_);
    }
    Serial.printf_P(PSTR("[HW] Buzzer Volume: %u%% -> %u\n"), volume, buzzer_volume_);
}

void HardwareInterface::setBuzzerEnabled(bool enabled) {
    buzzer_enabled_ = enabled;
    Serial.printf_P(PSTR("HW: Buzzer %s\n"), enabled ? "enabled" : "disabled");
    if (!enabled && buzzer_mode_ != BUZZER_MODE_OFF) {
        analogWrite(BUZZER_PIN, 0);
        buzzer_mode_ = BUZZER_MODE_OFF;
    }
}

void HardwareInterface::setRGBLED(RGB_Mode_t mode, RGB_Color_t color) {
    rgb_mode_ = mode;
    rgb_color_ = color;
}

void HardwareInterface::setBuzzer(Buzzer_Mode_t mode) {
    if (!buzzer_enabled_ && mode != BUZZER_MODE_OFF) {
        return;
    }

    // 不要打断正在播放的一次性蜂鸣（BEEP_ONCE/BEEP_DOUBLE），让它自然结束
    if (mode == BUZZER_MODE_OFF && buzzer_beep_on_ &&
        (buzzer_mode_ == BUZZER_MODE_BEEP_ONCE || buzzer_mode_ == BUZZER_MODE_BEEP_DOUBLE)) {
        return;
    }

    buzzer_last_time_ = 0;
    buzzer_beep_count_ = 0;
    buzzer_beep_on_ = false;
    buzzer_state_ = false;
    buzzer_mode_ = mode;
    
    if (mode == BUZZER_MODE_CONTINUOUS) {
        analogWrite(BUZZER_PIN, buzzer_volume_);
    } else if (mode == BUZZER_MODE_OFF) {
        analogWrite(BUZZER_PIN, 0);
    } else {
        analogWrite(BUZZER_PIN, 0);
    }
}

void HardwareInterface::updateLEDs() {
    unsigned long current_time = millis();
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        updateLED(LED_PINS[i], led_modes_[i], current_time);
    }
}

void HardwareInterface::updateRGBLED() {
    static uint8_t breath_step = 0;
    static unsigned long last_blink_time = 0;
    static bool blink_state = false;
    static uint8_t rainbow_step = 0;
    
    switch (rgb_mode_) {
        case RGB_MODE_OFF:
            leds_[0] = CRGB::Black;
            break;
            
        case RGB_MODE_ON:
            FastLED.setBrightness(led_brightness_);
            leds_[0] = CRGB(rgb_color_.r, rgb_color_.g, rgb_color_.b);
            break;
            
        case RGB_MODE_BREATHING: {
            uint8_t intensity = (sin(breath_step * 0.1f) + 1.0f) * 127.5f;
            FastLED.setBrightness(led_brightness_);
            leds_[0] = CRGB((rgb_color_.r * intensity) >> 8, 
                           (rgb_color_.g * intensity) >> 8, 
                           (rgb_color_.b * intensity) >> 8);
            breath_step++;
            break;
        }
            
        case RGB_MODE_BLINKING: {
            unsigned long current_time = millis();
            if (current_time - last_blink_time >= 500) {
                blink_state = !blink_state;
                if (blink_state) {
                    FastLED.setBrightness(led_brightness_);
                    leds_[0] = CRGB(rgb_color_.r, rgb_color_.g, rgb_color_.b);
                } else {
                    leds_[0] = CRGB::Black;
                }
                last_blink_time = current_time;
            }
            break;
        }
            
        case RGB_MODE_RAINBOW: {
            uint8_t r, g, b;
            if (rainbow_step < 85) {
                r = 255 - rainbow_step * 3;
                g = rainbow_step * 3;
                b = 0;
            } else if (rainbow_step < 170) {
                r = 0;
                g = 255 - (rainbow_step - 85) * 3;
                b = (rainbow_step - 85) * 3;
            } else {
                r = (rainbow_step - 170) * 3;
                g = 0;
                b = 255 - (rainbow_step - 170) * 3;
            }
            FastLED.setBrightness(led_brightness_);
            leds_[0] = CRGB(r, g, b);
            rainbow_step++;
            break;
        }
            
        default:
            leds_[0] = CRGB::Black;
            break;
    }
    FastLED.show();
}

void HardwareInterface::updateBuzzer() {
    unsigned long current_time = millis();
    
    switch(buzzer_mode_) {
        case BUZZER_MODE_OFF:
            analogWrite(BUZZER_PIN, 0);
            break;
            
        case BUZZER_MODE_BEEP_ONCE:
            if (!buzzer_beep_on_) {
                buzzer_beep_on_ = true;
                buzzer_last_time_ = current_time;
                analogWrite(BUZZER_PIN, buzzer_volume_);
            } else if (current_time - buzzer_last_time_ >= 200) {
                buzzer_mode_ = BUZZER_MODE_OFF;
                analogWrite(BUZZER_PIN, 0);
                buzzer_beep_on_ = false;
                buzzer_beep_count_ = 0;
            } else {
                analogWrite(BUZZER_PIN, buzzer_volume_);
            }
            break;
            
        case BUZZER_MODE_BEEP_DOUBLE:
            if (buzzer_beep_count_ == 0) {
                analogWrite(BUZZER_PIN, buzzer_volume_);
                buzzer_beep_on_ = true;
                buzzer_beep_count_ = 1;
                buzzer_last_time_ = current_time;
            } else if (buzzer_beep_on_ && current_time - buzzer_last_time_ >= 200) {
                analogWrite(BUZZER_PIN, 0);
                buzzer_beep_on_ = false;
                buzzer_last_time_ = current_time;
            } else if (!buzzer_beep_on_ && current_time - buzzer_last_time_ >= 200) {
                analogWrite(BUZZER_PIN, buzzer_volume_);
                buzzer_beep_on_ = true;
                buzzer_beep_count_ = 2;
                buzzer_last_time_ = current_time;
            } else if (buzzer_beep_on_ && current_time - buzzer_last_time_ >= 200) {
                analogWrite(BUZZER_PIN, 0);
                buzzer_mode_ = BUZZER_MODE_OFF;
                buzzer_beep_count_ = 0;
                buzzer_beep_on_ = false;
            }
            break;
            
        case BUZZER_MODE_BEEP_TRIPLE:
            if (buzzer_beep_count_ == 0) {
                analogWrite(BUZZER_PIN, buzzer_volume_);
                buzzer_beep_on_ = true;
                buzzer_beep_count_ = 1;
                buzzer_last_time_ = current_time;
            } else if (buzzer_beep_on_ && current_time - buzzer_last_time_ >= 200) {
                analogWrite(BUZZER_PIN, 0);
                buzzer_beep_on_ = false;
                buzzer_last_time_ = current_time;
            } else if (!buzzer_beep_on_ && current_time - buzzer_last_time_ >= 200) {
                buzzer_beep_count_++;
                if (buzzer_beep_count_ > 3) {
                    analogWrite(BUZZER_PIN, 0);
                    buzzer_mode_ = BUZZER_MODE_OFF;
                    buzzer_beep_count_ = 0;
                    buzzer_beep_on_ = false;
                } else {
                    analogWrite(BUZZER_PIN, buzzer_volume_);
                    buzzer_beep_on_ = true;
                    buzzer_last_time_ = current_time;
                }
            }
            break;
            
        case BUZZER_MODE_CONTINUOUS:
            analogWrite(BUZZER_PIN, buzzer_volume_);
            break;
            
        case BUZZER_MODE_ALARM:
            if (current_time - buzzer_last_time_ >= 200) {
                buzzer_state_ = !buzzer_state_;
                analogWrite(BUZZER_PIN, buzzer_state_ ? buzzer_volume_ : 0);
                buzzer_last_time_ = current_time;
            }
            break;
    }
}

void HardwareInterface::updateLED(uint8_t pin, LED_Mode_t mode, unsigned long current_time) {
    static unsigned long last_blink_times[LED_COUNT] = {0};
    static bool blink_states[LED_COUNT] = {false};
    static uint8_t pulse_steps[LED_COUNT] = {0};
    
    int8_t index = pinToIndex(pin);
    if (index < 0) return;
    
    uint32_t pwm_value = (uint32_t)led_brightness_ * 1023 / 255;
    
    switch(mode) {
        case LED_MODE_OFF:
            analogWrite(pin, 0);
            blink_states[index] = false;
            break;
            
        case LED_MODE_ON:
            analogWrite(pin, pwm_value);
            blink_states[index] = true;
            break;
            
        case LED_MODE_BLINK_SLOW:
            if (current_time - last_blink_times[index] >= 500) {
                blink_states[index] = !blink_states[index];
                analogWrite(pin, blink_states[index] ? pwm_value : 0);
                last_blink_times[index] = current_time;
            }
            break;
            
        case LED_MODE_BLINK_FAST:
            if (current_time - last_blink_times[index] >= 100) {
                blink_states[index] = !blink_states[index];
                analogWrite(pin, blink_states[index] ? pwm_value : 0);
                last_blink_times[index] = current_time;
            }
            break;
            
        case LED_MODE_PULSE: {
            uint8_t intensity = (sin(pulse_steps[index] * 0.1f) + 1.0f) * 127.5f;
            analogWrite(pin, (intensity * led_brightness_) >> 8);
            pulse_steps[index]++;
            break;
        }
    }
}

int8_t HardwareInterface::pinToIndex(uint8_t pin) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (LED_PINS[i] == pin) return i;
    }
    return -1;
}