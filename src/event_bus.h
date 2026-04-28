// event_bus.h
#pragma once

#include "event_types.h"

// 事件回调函数类型定义
typedef void (*EventCallback)(EventType type, void* param);

// 配置项
#define MAX_SUBSCRIBERS_PER_EVENT 8  // 每个事件最多允许多少个订阅者
#define MAX_EVENT_TYPES EVT_MAX_TYPES

class EventBus {
public:
    // 获取单例实例
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }

    // 订阅事件
    // 返回 true 表示成功，false 表示订阅者列表已满
    bool subscribe(EventType type, EventCallback callback) {
        if (type >= MAX_EVENT_TYPES || callback == nullptr) {
            return false;
        }
        
        // 简单的去重检查（可选，防止重复注册）
        for (int i = 0; i < subscriberCount[type]; i++) {
            if (subscribers[type][i] == callback) {
                return true; // 已经订阅过
            }
        }

        if (subscriberCount[type] < MAX_SUBSCRIBERS_PER_EVENT) {
            subscribers[type][subscriberCount[type]++] = callback;
            return true;
        }
        
        // 如果空间不足，这里可以选择覆盖最后一个或忽略，这里选择忽略并打印警告
        #ifdef ARDUINO
        Serial.printf("[EventBus] Warning: Subscriber list full for event %d\n", type);
        #endif
        return false;
    }

    // 取消订阅
    bool unsubscribe(EventType type, EventCallback callback) {
        if (type >= MAX_EVENT_TYPES || callback == nullptr) {
            return false;
        }

        for (int i = 0; i < subscriberCount[type]; i++) {
            if (subscribers[type][i] == callback) {
                // 移动后续元素填补空缺
                for (int j = i; j < subscriberCount[type] - 1; j++) {
                    subscribers[type][j] = subscribers[type][j + 1];
                }
                subscriberCount[type]--;
                return true;
            }
        }
        return false;
    }

    // 发布事件
    // 注意：此函数会同步执行所有回调。若回调耗时过长，会阻塞主循环。
    void publish(EventType type, void* param = nullptr) {
        if (type >= MAX_EVENT_TYPES) {
            return;
        }

        // 临时复制计数器和指针，防止在回调中修改订阅列表导致崩溃
        // 虽然简单场景下直接遍历也可以，但这样更安全
        uint8_t count = subscriberCount[type];
        EventCallback callbacks[MAX_SUBSCRIBERS_PER_EVENT];
        
        for (uint8_t i = 0; i < count; i++) {
            callbacks[i] = subscribers[type][i];
        }

        // 执行回调
        for (uint8_t i = 0; i < count; i++) {
            if (callbacks[i]) {
                callbacks[i](type, param);
            }
        }
    }

    // 工具函数：发布带整型数据的事件
    void publishInt(EventType type, int value) {
        publish(type, (void*)&value);
    }

    // 工具函数：发布带布尔数据的事件
    void publishBool(EventType type, bool value) {
        publish(type, (void*)&value);
    }

private:
    // 私有构造函数，禁止外部实例化
    EventBus() {
        memset(subscriberCount, 0, sizeof(subscriberCount));
        // 不需要显式初始化 subscribers 数组，全局/静态默认为 null 或 0
    }

    // 存储回调函数的二维数组 [事件类型][订阅者索引]
    EventCallback subscribers[MAX_EVENT_TYPES][MAX_SUBSCRIBERS_PER_EVENT];
    uint8_t subscriberCount[MAX_EVENT_TYPES];
};

// 宏定义方便调用
#define EVENT_BUS EventBus::getInstance()
#define SUBSCRIBE(evt, cb) EVENT_BUS.subscribe(evt, cb)
#define PUBLISH(evt, param) EVENT_BUS.publish(evt, param)