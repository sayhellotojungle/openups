#include "time_utils.h"
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>

// 【核心技巧】内部静态对象
// 这个对象只在当前 .cpp 文件中可见，外部无法访问，保证了封装性
// 它充当了全局函数的后端引擎
static class TimeEngine {
public:
    bool synced;
    long gmtOffset;
    const char* server;
    unsigned long lastAttempt;

    TimeEngine() : synced(false), gmtOffset(0), server(nullptr), lastAttempt(0) {}

    void begin(long offset, const char* srv) {
        gmtOffset = offset;
        server = srv;
        configTime(gmtOffset, 0, server);
        lastAttempt = millis();
        Serial.println("[Time] System initialized.");
    }

    void loop() {
        if (synced) return; // 已同步则跳过

        if (WiFi.status() != WL_CONNECTED) return;

        // 每 5 秒尝试一次
        if (millis() - lastAttempt < 5000) return;
        
        lastAttempt = millis();
        
        time_t now;
        time(&now);

        // 验证时间是否合理 (大于 2020-01-01)
        if (now > 1773666766) {
            synced = true;
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                char buf[64];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                Serial.printf("[Time] Synced: %s\n", buf);
            }
        } else {
            // 可选：重新触发 configTime 以强制重试，视 ESP32 核心版本而定
            // configTime(gmtOffset, 0, server); 
        }
    }

    uint32_t getTs() {
        time_t now;
        time(&now);
        if (synced || now > 1773666766) {
            if (!synced && now > 1773666766) synced = true; // 自动纠正状态
            return (uint32_t)now;
        }
        // 未同步时返回 uptime 秒数
        return (uint32_t)(millis() / 1000);
    }
    
    bool getStr(char* buf, size_t len, const char* fmt) {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) return false;
        strftime(buf, len, fmt, &timeinfo);
        return true;
    }

} gTimeEngine; // 实例化静态全局对象

// ============================================================
// 全局函数实现 (直接调用上面的 gTimeEngine)
// ============================================================

void initTimeSystem(long offset, const char* srv) {
    gTimeEngine.begin(offset, srv);
}

void updateTimeSystem() {
    gTimeEngine.loop();
}

uint32_t getTimestamp() {
    return gTimeEngine.getTs();
}

bool isTimeSynced() {
    return gTimeEngine.synced;
}

bool getFormattedTime(char* buffer, size_t size, const char* format) {
    return gTimeEngine.getStr(buffer, size, format);
}