#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取当前系统时间戳 (Unix Timestamp)
 * 
 * 行为逻辑：
 * 1. 如果 NTP 已同步：返回自 1970-01-01 以来的秒数 (绝对时间)。
 * 2. 如果 NTP 未同步：返回自开机以来的秒数 (millis() / 1000)。
 * 
 * @return uint32_t 时间戳 (秒)
 */
uint32_t getTimestamp();

/**
 * @brief 检查时间是否已同步到网络时间
 * @return true 已同步，false 未同步
 */
bool isTimeSynced();

/**
 * @brief 获取格式化的时间字符串
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param format 格式 (默认 "%Y-%m-%d %H:%M:%S")
 * @return true 成功，false 失败
 */
bool getFormattedTime(char* buffer, size_t size, const char* format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief 初始化时间系统 (需在 setup 中调用一次)
 * @param timezone_offset_sec 时区偏移 (中国为 28800)
 * @param ntp_server NTP 服务器地址
 */
void initTimeSystem(long timezone_offset_sec = 28800, const char* ntp_server = "pool.ntp.org");

/**
 * @brief 时间系统心跳 (需在 loop 中调用，用于处理 NTP 握手)
 */
void updateTimeSystem();

#ifdef __cplusplus
}
#endif

#endif // TIME_UTILS_H