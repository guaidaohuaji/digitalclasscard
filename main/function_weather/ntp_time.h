#pragma once

#include <time.h>
#include <stdbool.h>   
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通过 SNTP 同步网络时间。
 *
 * 此函数会阻塞直到同步成功或超时。
 *
 * @param timeout_sec 超时时间（秒）
 * @return true 同步成功；false 超时。
 */
bool ntp_time_sync(uint32_t timeout_sec);

/**
 * @brief 获取当前时间字符串（HH:MM:SS）。
 *
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 */
void ntp_get_time_str(char *buf, size_t buf_size);

/**
 * @brief 获取当前日期字符串（YYYY-MM-DD）。
 *
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 */
void ntp_get_date_str(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif