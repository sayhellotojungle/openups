// 在 cpp 文件中包含 AsyncMQTT_ESP32.h，避免头文件污染导致链接错误
#include <AsyncMQTT_ESP32.h>
#include "mqtt_service.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "event_bus.h"
#include "event_types.h"


// =============================================================================
// 静态成员初始化
// =============================================================================
MQTTService* MQTTService::s_instance = nullptr;

// =============================================================================
// MQTTService 构造函数
// =============================================================================
MQTTService::MQTTService()
    : m_config(nullptr), m_state(nullptr)
    , m_mqtt_client(nullptr)
    , m_use_tls(false), m_configured(false), m_connected(false)
    , m_discovery_published(false)
    , m_last_state_publish(0), m_last_heartbeat(0)
    , m_last_reconnect_attempt(0)
{
    s_instance = this;
    memset(m_device_id, 0, sizeof(m_device_id));
    memset(m_topic_base, 0, sizeof(m_topic_base));
    memset(m_mqtt_broker, 0, sizeof(m_mqtt_broker));
    memset(m_mqtt_username, 0, sizeof(m_mqtt_username));
    memset(m_mqtt_password, 0, sizeof(m_mqtt_password));
    m_mqtt_port = 0;
}

// =============================================================================
// MQTTService 析构函数
// =============================================================================
MQTTService::~MQTTService() {
    disconnect();
    if (m_mqtt_client) {
        delete m_mqtt_client;
        m_mqtt_client = nullptr;
    }
}

bool MQTTService::begin(Configuration* config, System_Global_State* state) {
    if (!config || !state) return false;

    m_config = config;
    m_state = state;
    if (strlen(config->identifier) > 0) {
        snprintf(m_device_id, sizeof(m_device_id), "ups-%s", config->identifier);
    } else {
        snprintf(m_device_id, sizeof(m_device_id), "ups-default");
    }
    snprintf(m_topic_base, sizeof(m_topic_base), "%s", m_device_id);

    // 创建 AsyncMqttClient 实例
    m_mqtt_client = new AsyncMqttClient();
    m_mqtt_client->onConnect([](bool sessionPresent) {
        onMqttConnect(sessionPresent);
    });
    m_mqtt_client->onDisconnect([](AsyncMqttClientDisconnectReason reason) {
        onMqttDisconnect(static_cast<int>(reason));
    });
    m_mqtt_client->onMessage([](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        onMqttMessage(topic, payload, static_cast<int>(properties.qos), len, index, total);
    });
    m_mqtt_client->setKeepAlive(MQTT_KEEPALIVE);

    m_configured = true;
    m_discovery_published = false;
    setStaticInstance(this);
    return true;
}

void MQTTService::setBrokerAddress(const char* broker) {
    if (broker) strncpy(m_mqtt_broker, broker, sizeof(m_mqtt_broker) - 1);
}

void MQTTService::setBrokerPort(uint16_t port) {
    m_mqtt_port = port;
    if (port == 8883) { m_use_tls = true; }
}

void MQTTService::setBrokerCredentials(const char* username, const char* password) {
    if (username) strncpy(m_mqtt_username, username, sizeof(m_mqtt_username) - 1);
    if (password) strncpy(m_mqtt_password, password, sizeof(m_mqtt_password) - 1);
}

void MQTTService::setStaticInstance(MQTTService* instance) { s_instance = instance; }

void MQTTService::loop() {
    if (!m_configured) {
        return;
    }

    if (!m_connected) {
        unsigned long now = millis();
        if (now - m_last_reconnect_attempt >= 5000) {
            m_last_reconnect_attempt = now;
            if (checkWifiConnected()) {
                connect();
            } 
        }
        return;
    }

    // AsyncMqttClient 是事件驱动的，不需要 loop() 调用

    unsigned long now = millis();
    if (now - m_last_state_publish >= 10000) {
        publishStateData();
        m_last_state_publish = now;
    }
    if (!m_discovery_published) {
        publishDiscoveryConfig();
        m_discovery_published = true;
    }
    if (now - m_last_heartbeat >= 60000) {
        publishAvailability(true);
        m_last_heartbeat = now;
    }
}

bool MQTTService::isConnected() { return m_connected; }

bool MQTTService::connect() {
    if (!checkWifiConnected()) {
        return false;
    }

    if (m_connected) {
        m_mqtt_client->disconnect();
        m_connected = false;
    }

    // 配置 AsyncMqttClient
    m_mqtt_client->setServer(m_mqtt_broker, m_mqtt_port);
    m_mqtt_client->setClientId(m_device_id);
    m_mqtt_client->setKeepAlive(MQTT_KEEPALIVE);

    if (strlen(m_mqtt_username) > 0) {
        m_mqtt_client->setCredentials(m_mqtt_username, m_mqtt_password);
    }

#if ASYNC_TCP_SSL_ENABLED
    if (m_use_tls) {
        m_mqtt_client->setSecure(true);
    }
#endif
    m_mqtt_client->connect();

    // 返回 true 表示连接尝试已发起，实际结果由 onConnect/onDisconnect 回调更新
    return true;
}

void MQTTService::disconnect() {
    if (m_connected) {
        publishAvailability(false);
    }
    if (m_mqtt_client) {
        m_mqtt_client->disconnect();
    }
    m_connected = false;
}

// =============================================================================
// AsyncMqttClient 回调
// =============================================================================
void MQTTService::onMqttConnect(bool sessionPresent) {
    Serial.printf("[MQTT] onConnect: sessionPresent=%s\n", sessionPresent ? "true" : "false");
    if (s_instance) {
        s_instance->m_connected = true;
        s_instance->subscribeCommandTopics();
        s_instance->publishAvailability(true);
    }
}

void MQTTService::onMqttDisconnect(int reason) {
    Serial.printf("[MQTT] onDisconnect: reason=%d\n", reason);
    if (s_instance) {
        s_instance->m_connected = false;
    }
}

void MQTTService::onMqttMessage(char* topic, char* payload, int properties, size_t len, size_t index, size_t total) {
    if (s_instance) {
        s_instance->handleCommand(topic, (const char*)payload, len);
    }
}

// =============================================================================
// 订阅命令
// =============================================================================
void MQTTService::subscribeCommandTopics() {
    if (!m_connected) {
        return;
    }
    char t[128];
    snprintf(t, sizeof(t), "%s/command/led_brightness/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
    snprintf(t, sizeof(t), "%s/command/buzzer_volume/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
    snprintf(t, sizeof(t), "%s/command/hid_report_mode/set", m_topic_base);
    m_mqtt_client->subscribe(t, 0);
}

void MQTTService::setDeviceIdentifier(const char* identifier) {
    if (identifier && strlen(identifier)) {
        snprintf(m_device_id, sizeof(m_device_id), "ups-%s", identifier);
        snprintf(m_topic_base, sizeof(m_topic_base), "ups-%s", identifier);
        m_discovery_published = false;
    }
}

// =============================================================================
// 发布辅助
// =============================================================================
bool MQTTService::publishPayload(const char* topic, const uint8_t* payload, size_t length, uint8_t qos, bool retain) {
    if (!m_connected || !m_mqtt_client) return false;
    uint16_t packetId = m_mqtt_client->publish(topic, qos, retain, reinterpret_cast<const char*>(payload), length);
    return packetId != 0;
}

// =============================================================================
// Discovery 配置
// =============================================================================
bool MQTTService::publishDiscoveryConfig() {
    if (!m_connected) {
        return false;
    }

    // 电池传感器
    publishSensorDiscovery("bms_soc", "UPS Battery SOC", "battery", "measurement", "%",
                          "state/bms/soc");
    publishSensorDiscovery("bms_soh", "UPS Battery SOH", "battery", "measurement", "%",
                          "state/bms/soh");
    publishSensorDiscovery("bms_voltage", "UPS Battery Voltage", "voltage", "measurement", "mV",
                          "state/bms/voltage");
    publishSensorDiscovery("bms_current", "UPS Battery Current", "current", "measurement", "mA",
                          "state/bms/current");
    publishSensorDiscovery("bms_temperature", "UPS Battery Temperature", "temperature", "measurement", "°C",
                          "state/bms/temperature");
    publishSensorDiscovery("bms_capacity_remaining", "UPS Battery Capacity Remaining", "battery_capacity", "measurement", "mAh",
                          "state/bms/capacity_remaining");
    publishSensorDiscovery("bms_cycle_count", "UPS Battery Cycle Count", nullptr, "measurement", nullptr,
                          "state/bms/cycle_count");

    // 单体电压 (Cell 1-5)
    publishSensorDiscovery("bms_cell_1", "UPS Cell 1 Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_1");
    publishSensorDiscovery("bms_cell_2", "UPS Cell 2 Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_2");
    publishSensorDiscovery("bms_cell_3", "UPS Cell 3 Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_3");
    publishSensorDiscovery("bms_cell_4", "UPS Cell 4 Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_4");
    publishSensorDiscovery("bms_cell_5", "UPS Cell 5 Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_5");
    publishSensorDiscovery("bms_cell_min", "UPS Cell Min Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_min");
    publishSensorDiscovery("bms_cell_max", "UPS Cell Max Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_max");
    publishSensorDiscovery("bms_cell_avg", "UPS Cell Avg Voltage", "voltage", "measurement", "mV",
                          "state/bms/cell_avg");

    // 电源传感器
    publishSensorDiscovery("power_input_voltage", "UPS Input Voltage", "voltage", "measurement", "mV",
                          "state/power/input_voltage");
    publishSensorDiscovery("power_input_current", "UPS Input Current", "current", "measurement", "mA",
                          "state/power/input_current");
    publishSensorDiscovery("power_output_power", "UPS Output Power", "power", "measurement", "W",
                          "state/power/output_power");
    publishSensorDiscovery("power_battery_voltage", "UPS Battery Voltage(adc)", "voltage", "measurement", "mV",
                          "state/power/battery_voltage");
    publishSensorDiscovery("power_battery_current", "UPS Battery Current(adc)", "current", "measurement", "mA",
                          "state/power/battery_current");

    // 二进制传感器 - 电源状态
    publishBinarySensorDiscovery("ac_present", "UPS AC Power", "power", "state/power/ac_present");
    publishBinarySensorDiscovery("charger_enabled", "UPS Charger Enabled", "plug", "state/power/charger_enabled");
    publishBinarySensorDiscovery("balancing_active", "UPS Balancing Active", "problem", "state/bms/balancing_active");
    publishBinarySensorDiscovery("wifi_connected", "UPS WiFi Connected", "connectivity", "state/system/wifi_connected");
    publishBinarySensorDiscovery("bms_fault", "UPS BMS Fault", "problem", "state/bms/fault_type");
    publishBinarySensorDiscovery("power_fault", "UPS Power Fault", "problem", "state/power/fault_type");
    publishBinarySensorDiscovery("emergency_shutdown", "UPS Emergency Shutdown", "problem", "state/system/emergency_shutdown");
    publishBinarySensorDiscovery("protection_over_current", "UPS Over Current Protection", "problem", "state/protection/over_current");
    publishBinarySensorDiscovery("protection_over_temp", "UPS Over Temperature Protection", "problem", "state/protection/over_temp");
    publishBinarySensorDiscovery("protection_short_circuit", "UPS Short Circuit Protection", "problem", "state/protection/short_circuit");

    // 系统传感器
    publishSensorDiscovery("system_uptime", "UPS System Uptime", "duration", "measurement", "s",
                          "state/system/uptime");
    publishSensorDiscovery("system_board_temperature", "UPS Board Temperature", "temperature", "measurement", "°C",
                          "state/system/board_temperature");
    publishSensorDiscovery("system_environment_temperature", "UPS Environment Temperature", "temperature", "measurement", "°C",
                          "state/system/environment_temperature");
    publishSensorDiscovery("system_wifi_rssi", "UPS WiFi RSSI", "signal_strength", "measurement", "dBm",
                          "state/system/wifi_rssi");
    // Note: state_class should NOT be set for string/non-numeric sensors
    publishSensorDiscovery("system_firmware_version", "UPS Firmware Version", nullptr, nullptr, nullptr,
                          "state/system/firmware_version");
    publishSensorDiscovery("system_power_mode", "UPS Power Mode", nullptr, nullptr, nullptr,
                          "state/system/power_mode");
    publishSensorDiscovery("system_overall_status", "UPS Overall Status", nullptr, nullptr, nullptr,
                          "state/system/overall_status");
    publishSensorDiscovery("system_wifi_ssid", "UPS WiFi SSID", nullptr, nullptr, nullptr,
                          "state/system/wifi_ssid");

    // 控制实体
    publishNumberDiscovery("led_brightness", "UPS LED Brightness",
                          "command/led_brightness/set", "state/config/led_brightness",
                          0, 100, 1, "%");
    publishNumberDiscovery("buzzer_volume", "UPS Buzzer Volume",
                          "command/buzzer_volume/set", "state/config/buzzer_volume",
                          0, 100, 1, "%");
    publishSelectDiscovery("hid_report_mode", "UPS HID Report Mode",
                          "command/hid_report_mode/set", "state/config/hid_report_mode",
                          "mAh,mWh,%");
    return true;
}


// =============================================================================
// 传感器 Discovery
// =============================================================================
bool MQTTService::publishSensorDiscovery(const char* entity_id, const char* name,
                                         const char* device_class, const char* state_class,
                                         const char* unit, const char* state_topic) {
    if (!m_connected) {
        return false;
    }

    StaticJsonDocument<512> doc;

    if (name) doc["name"] = name;
    if (device_class) doc["device_class"] = device_class;
    if (state_class) doc["state_class"] = state_class;
    if (unit) doc["unit_of_measurement"] = unit;
    doc["value_template"] = "{{ value_json.value }}";

    char state_topic_full[96];
    snprintf(state_topic_full, sizeof(state_topic_full), "%s/%s", m_topic_base, state_topic);
    doc["state_topic"] = state_topic_full;

    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = unique_id;

    JsonObject device = doc.createNestedObject("device");
    setupDeviceInfo(device);
    JsonObject avail = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    avail["topic"] = availability_topic;
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", "homeassistant", m_device_id, entity_id);

    char buf[768];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len >= sizeof(buf) - 1) {
        return false;
    }
    bool result = publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishBinarySensorDiscovery(const char* entity_id, const char* name,
                                               const char* device_class, const char* state_topic) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    if (device_class) doc["device_class"] = device_class;
    doc["payload_on"] = true; doc["payload_off"] = false;
    doc["value_template"] = "{{ value_json.value }}";
    char t[96]; snprintf(t, sizeof(t), "%s/%s", m_topic_base, state_topic);
    doc["state_topic"] = t;
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishSwitchDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic, const char* state_topic) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96], st[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    snprintf(st, sizeof(st), "%s/%s", m_topic_base, state_topic);
    doc["command_topic"] = ct; doc["state_topic"] = st;
    doc["payload_on"] = true; doc["payload_off"] = false; doc["retain"] = true;
    doc["value_template"] = "{{ value_json.value }}";
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/switch/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishNumberDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic, const char* state_topic,
                                         int min, int max, int step, const char* unit) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96], st[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    snprintf(st, sizeof(st), "%s/%s", m_topic_base, state_topic);
    doc["command_topic"] = ct; doc["state_topic"] = st;
    doc["min"] = min; doc["max"] = max; doc["step"] = step;
    if (unit) doc["unit_of_measurement"] = unit;
    doc["retain"] = true;
    doc["value_template"] = "{{ value_json.value }}";
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/number/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishSelectDiscovery(const char* entity_id, const char* name,
                                         const char* command_topic, const char* state_topic,
                                         const char* options) {
    if (!m_connected) {
        return false;
    }
    StaticJsonDocument<512> doc;
    if (name) doc["name"] = name;
    char ct[96], st[96];
    snprintf(ct, sizeof(ct), "%s/%s", m_topic_base, command_topic);
    snprintf(st, sizeof(st), "%s/%s", m_topic_base, state_topic);
    doc["command_topic"] = ct; doc["state_topic"] = st; doc["retain"] = true;
    doc["value_template"] = "{{ value_json.value }}";
    JsonArray opts = doc.createNestedArray("options");
    const char* p = options;
    while (p && *p) {
        const char* comma = strchr(p, ',');
        size_t olen = comma ? (comma - p) : strlen(p);
        char opt[16];
        if (olen >= sizeof(opt)) olen = sizeof(opt) - 1;
        strncpy(opt, p, olen); opt[olen] = '\0';
        opts.add(opt);
        p = comma ? comma + 1 : nullptr;
    }
    char uid[64]; snprintf(uid, sizeof(uid), "%s-%s", m_device_id, entity_id);
    doc["unique_id"] = uid;
    JsonObject dev = doc.createNestedObject("device");
    setupDeviceInfo(dev);
    JsonObject av = doc.createNestedObject("availability");
    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    av["topic"] = availability_topic;
    char topic[128]; snprintf(topic, sizeof(topic), "%s/select/%s/%s/config", "homeassistant", m_device_id, entity_id);
    char buf[768]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, true);
    return result;
}

bool MQTTService::publishStateData() {
    if (!m_connected) {
        return false;
    }
    if (!m_state) {
        return false;
    }

    // BMS
    publishStateToTopic("state/bms/soc", m_state->bms.soc);
    publishStateToTopic("state/bms/soh", m_state->bms.soh);
    publishStateToTopic("state/bms/voltage", m_state->bms.voltage);
    publishStateToTopic("state/bms/current", m_state->bms.current);
    publishStateToTopic("state/bms/temperature", m_state->bms.temperature);
    publishStateToTopic("state/bms/capacity_remaining", m_state->bms.capacity_remaining);
    publishStateToTopic("state/bms/cycle_count", (int)m_state->bms.cycle_count);
    publishStateToTopic("state/bms/cell_1", m_state->bms.cell_voltages[0]);
    publishStateToTopic("state/bms/cell_2", m_state->bms.cell_voltages[1]);
    publishStateToTopic("state/bms/cell_3", m_state->bms.cell_voltages[2]);
    publishStateToTopic("state/bms/cell_4", m_state->bms.cell_voltages[3]);
    publishStateToTopic("state/bms/cell_5", m_state->bms.cell_voltages[4]);
    publishStateToTopic("state/bms/cell_min", m_state->bms.cell_voltage_min);
    publishStateToTopic("state/bms/cell_max", m_state->bms.cell_voltage_max);
    publishStateToTopic("state/bms/cell_avg", m_state->bms.cell_voltage_avg);
    publishStateToTopic("state/bms/balancing_active", m_state->bms.balancing_active);
    publishStateToTopic("state/bms/is_connected", m_state->bms.is_connected);
    publishStateToTopic("state/bms/fault_type", (int)m_state->bms.fault_type);

    // Power
    publishStateToTopic("state/power/input_voltage", m_state->power.input_voltage);
    publishStateToTopic("state/power/input_current", m_state->power.input_current);
    publishStateToTopic("state/power/output_power", m_state->power.output_power);
    publishStateToTopic("state/power/battery_voltage", m_state->power.battery_voltage);
    publishStateToTopic("state/power/battery_current", m_state->power.battery_current);
    publishStateToTopic("state/power/ac_present", m_state->power.ac_present);
    publishStateToTopic("state/power/charger_enabled", m_state->power.charger_enabled);
    publishStateToTopic("state/power/hybrid_mode", m_state->power.hybrid_mode);
    publishStateToTopic("state/power/fault_type", (int)m_state->power.fault_type);

    // System
    publishStateToTopic("state/system/uptime", (int)m_state->system.uptime);
    publishStateToTopic("state/system/board_temperature", m_state->system.board_temperature);
    publishStateToTopic("state/system/environment_temperature", m_state->system.environment_temperature);
    publishStateToTopic("state/system/wifi_connected", m_state->system.wifi_connected);
    publishStateToTopic("state/system/wifi_rssi", m_state->system.wifi_rssi);
    publishStateToTopic("state/system/wifi_ssid", m_state->system.wifi_ssid);
    publishStateToTopic("state/system/firmware_version", m_state->system.firmware_version);
    publishStateToTopic("state/system/power_mode", m_state->power_mode);
    publishStateToTopic("state/system/overall_status", m_state->overall_status);
    publishStateToTopic("state/system/emergency_shutdown", m_state->emergency_shutdown);

    // Protection
    publishStateToTopic("state/protection/over_current", m_state->over_current_protection);
    publishStateToTopic("state/protection/over_temp", m_state->over_temp_protection);
    publishStateToTopic("state/protection/short_circuit", m_state->short_circuit_protection);

    // Config
    if (m_config) {
        publishStateToTopic("state/config/led_brightness", m_config->led_brightness);
        publishStateToTopic("state/config/buzzer_enabled", m_config->buzzer_enabled);
        publishStateToTopic("state/config/buzzer_volume", m_config->buzzer_volume);
        
        // 将 hid_report_mode 的数字索引转换为字符串
        publishStateToTopic("state/config/hid_report_mode", getHidReportModeString(m_config->hid_report_mode));
    }

    return true;
}

bool MQTTService::publishAvailability(bool online) {
    if (!m_connected) {
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, TOPIC_AVAILABILITY);
    const char* payload = online ? "online" : "offline";
    bool result = publishPayload(topic, (const uint8_t*)payload, strlen(payload), 0, true);
    return result;
}

bool MQTTService::checkWifiConnected() { return WiFi.isConnected() && WiFi.status() == WL_CONNECTED; }

/**
 * @brief 设置 Home Assistant 设备信息
 * @param device JsonObject 引用，用于填充设备信息
 */
void MQTTService::setupDeviceInfo(JsonObject device) {
    device["identifiers"][0] = m_device_id;
    device["name"] = "UPS";
    device["manufacturer"] = "OpenUPS";
    device["model"] = "ESP32-S3";
    device["sw_version"] = FIRMWARE_ID_TAG;
    setupDeviceConfigUrl(device);
}

void MQTTService::setupDeviceConfigUrl(JsonObject device) {
    char config_url[32];
    snprintf(config_url, sizeof(config_url), "http://%s/", WiFi.localIP().toString().c_str());
    device["configuration_url"] = config_url;
}

void MQTTService::handleCommand(const char* topic, const char* payload, unsigned int length) {
    char* cmd = strstr(topic, "/command/");
    if (!cmd) return;
    cmd += 9;
    char* end = strstr(cmd, "/set");
    if (!end) return;
    *end = '\0';
    char payload_str[64];
    strncpy(payload_str, payload, length < 63 ? length : 63);
    payload_str[length < 63 ? length : 63] = '\0';
    Serial.printf("[MQTT] Cmd: %s = %s\n", cmd, payload_str);
    
    // 检查配置指针有效性
    if (!m_config) {
        Serial.println("[MQTT] Config not available, command ignored");
        return;
    }
    
    // 创建配置副本（所有命令共用）
    Configuration config_copy;
    memcpy(&config_copy, m_config, sizeof(Configuration));
    
    bool config_changed = false;
    
    // 根据命令类型处理
    if (strcmp(cmd, "hid_report_mode") == 0) {
        uint8_t new_mode = 0;
        
        // 将字符串值转换为数字索引
        if (strcmp(payload_str, "mAh") == 0) {
            new_mode = 0;
        } else if (strcmp(payload_str, "mWh") == 0) {
            new_mode = 1;
        } else if (strcmp(payload_str, "%") == 0) {
            new_mode = 2;
        } else {
            // 尝试解析为数字（向后兼容）
            new_mode = (uint8_t)atoi(payload_str);
            if (new_mode > 2) {
                Serial.printf("[MQTT] Invalid hid_report_mode value: %s\n", payload_str);
                return;
            }
        }
        
        // 检查是否需要更新
        if (m_config->hid_report_mode != new_mode) {
            config_copy.hid_report_mode = new_mode;
            config_changed = true;
            Serial.printf("[MQTT] hid_report_mode change requested: %d (%s)\n", new_mode, payload_str);
            
            // 立即发布新状态以确认更改
            publishStateToTopic("state/config/hid_report_mode", getHidReportModeString(new_mode));
        }
    }
    else if (strcmp(cmd, "buzzer_volume") == 0) {
        uint8_t new_volume = (uint8_t)atoi(payload_str);
        
        // 验证范围 (0-100)
        if (new_volume > 100) {
            Serial.printf("[MQTT] Invalid buzzer_volume value: %s (must be 0-100)\n", payload_str);
            return;
        }
        
        // 检查是否需要更新
        if (m_config->buzzer_volume != new_volume) {
            config_copy.buzzer_volume = new_volume;
            config_changed = true;
            Serial.printf("[MQTT] buzzer_volume change requested: %d\n", new_volume);
            
            // 立即发布新状态以确认更改
            publishStateValue("state/config/buzzer_volume", new_volume);
        }
    }
    else if (strcmp(cmd, "led_brightness") == 0) {
        uint8_t new_brightness = (uint8_t)atoi(payload_str);
        
        // 验证范围 (0-100)
        if (new_brightness > 100) {
            Serial.printf("[MQTT] Invalid led_brightness value: %s (must be 0-100)\n", payload_str);
            return;
        }
        
        // 检查是否需要更新
        if (m_config->led_brightness != new_brightness) {
            config_copy.led_brightness = new_brightness;
            config_changed = true;
            Serial.printf("[MQTT] led_brightness change requested: %d\n", new_brightness);
            
            // 立即发布新状态以确认更改
            publishStateValue("state/config/led_brightness", new_brightness);
        }
    }
    else {
        // 未知命令
        Serial.printf("[MQTT] Unknown command: %s\n", cmd);
        return;
    }
    
    // 如果配置有变更，发布事件
    if (config_changed) {
        EventBus::getInstance().publish(EVT_CONFIG_SYSTEM_CHANGE_REQUEST, &config_copy);
    }
}

// =============================================================================
// 发布状态值 (通用方法)
// =============================================================================
bool MQTTService::publishStateValue(const char* topic, float value) {
    StaticJsonDocument<64> doc; doc["value"] = value;
    char buf[48]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, false);
    return result;
}

bool MQTTService::publishStateValue(const char* topic, int value) {
    StaticJsonDocument<32> doc; doc["value"] = value;
    char buf[32]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, false);
    return result;
}

bool MQTTService::publishStateValue(const char* topic, bool value) {
    StaticJsonDocument<32> doc; doc["value"] = value;
    char buf[32]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, false);
    return result;
}

bool MQTTService::publishStateValue(const char* topic, const char* value) {
    StaticJsonDocument<128> doc; doc["value"] = value;
    char buf[144]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, false);
    return result;
}

bool MQTTService::publishStateValue(const char* topic, uint16_t value) {
    StaticJsonDocument<32> doc; doc["value"] = value;
    char buf[32]; size_t len = serializeJson(doc, buf, sizeof(buf));
    bool result = len < sizeof(buf) - 1 && publishPayload(topic, (const uint8_t*)buf, len, 0, false);
    return result;
}

bool MQTTService::publishStateToTopic(const char* path, float value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, value);
}

bool MQTTService::publishStateToTopic(const char* path, int value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, value);
}

bool MQTTService::publishStateToTopic(const char* path, bool value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, value);
}

bool MQTTService::publishStateToTopic(const char* path, const char* value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, value);
}

bool MQTTService::publishStateToTopic(const char* path, uint16_t value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, value);
}

bool MQTTService::publishStateToTopic(const char* path, uint32_t value) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", m_topic_base, path);
    return publishStateValue(topic, (int)value);
}

// =============================================================================
// 辅助方法实现
// =============================================================================

const char* MQTTService::getHidReportModeString(uint8_t mode) const {
    switch (mode) {
        case 0: return "mAh";
        case 1: return "mWh";
        case 2: return "%";
        default: return "unknown";
    }
}
