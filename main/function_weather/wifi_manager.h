#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动 Wi-Fi 连接（自动重连）。
 *
 * 此函数会创建一个后台任务来管理 Wi-Fi 事件循环，
 * 非阻塞。Wi-Fi 状态通过 Event Group 同步。
 *
 * @note 必须在 NVS 初始化后调用。
 */
void wifi_manager_start(void);

/**
 * @brief 获取 Wi-Fi 是否已连接并获取到 IP。
 *
 * @return true 已连接并拥有 IP；false 未连接。
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 等待 Wi-Fi 连接完成（阻塞调用方任务）。
 *
 * @param timeout_ticks 超时时间（FreeRTOS Tick）
 * @return true 连接成功；false 超时。
 */
bool wifi_manager_wait_connected(TickType_t timeout_ticks);

/**
 * @brief 等待 SNTP 时间同步完成（阻塞调用方任务）。
 *
 * 在 OSS 签名等需要准确时间的操作之前调用此函数。
 *
 * @param timeout_ticks 超时时间（FreeRTOS Tick），建议 pdMS_TO_TICKS(15000)
 * @return true 时间同步成功；false 超时。
 */
bool wifi_manager_wait_sntp_synced(TickType_t timeout_ticks);

/**
 * @brief 获取 Wi-Fi 连接成功事件组 Bit。
 *
 * 可与其他模块的 Event Group 共用。
 */
#define WIFI_CONNECTED_BIT  BIT0

#ifdef __cplusplus
}
#endif
