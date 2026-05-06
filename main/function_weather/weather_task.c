#include "weather_task.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "weather_api.h"
#include "ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>   
#include <time.h>
#define TAG "weather_task"

#define WEATHER_UPDATE_INTERVAL_SEC  1800
#define TIME_UPDATE_INTERVAL_SEC     1

#define SCHOOL_LAT  30.2741f
#define SCHOOL_LON  120.1551f

// 改为指针，在任务内动态分配
static weather_hourly_t *g_hourly_data = NULL;
static int g_hourly_count = 0;

static void weather_task(void *pvParameters)
{
    // 分配 24 条预报数据的空间
    g_hourly_data = (weather_hourly_t *)malloc(sizeof(weather_hourly_t) * 24);
    if (!g_hourly_data) {
        ESP_LOGE(TAG, "无法分配天气预报缓冲区");
        vTaskDelete(NULL);
        return;
    }

    // ---- 等待 Wi‑Fi 连接 ----
    ESP_LOGI(TAG, "等待 Wi‑Fi 连接...");
    if (!wifi_manager_wait_connected(pdMS_TO_TICKS(60000))) {
        ESP_LOGE(TAG, "Wi‑Fi 连接超时，任务退出");
        free(g_hourly_data);
        vTaskDelete(NULL);
        return;
    }

    // ---- 同步 NTP ----
    ESP_LOGI(TAG, "开始 NTP 时间同步...");
    if (!ntp_time_sync(60)) {
        ESP_LOGE(TAG, "NTP 同步失败，任务退出");
        free(g_hourly_data);
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_weather_update = 0;
    TickType_t last_time_update    = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "Wi‑Fi 断开，等待重连...");
            wifi_manager_wait_connected(portMAX_DELAY);
            last_weather_update = 0;
            last_time_update = 0;
        }

        // 获取天气（带重试）
        if ((now - last_weather_update) > pdMS_TO_TICKS(WEATHER_UPDATE_INTERVAL_SEC * 1000)
            || last_weather_update == 0) {

            int ret = -1;
            int retry = 0;
            while (retry < 3 && ret != 0) {
                ret = weather_fetch_forecast(SCHOOL_LAT, SCHOOL_LON,
                                             g_hourly_data, 24, &g_hourly_count);
                if (ret != 0) {
                    ESP_LOGE(TAG, "获取天气失败，重试 %d/3，错误码: %d", retry + 1, ret);
                    vTaskDelay(pdMS_TO_TICKS(10000));  // 等待 10 秒再试
                    retry++;
                }
            }

            if (ret == 0 && g_hourly_count > 0) {
                int temps[8];
                const char *times[8];          // 改为时间字符串数组
                char time_str_buf[8][6];       // 存储“HH:00\0”，共5个字符+结束符

                int step = (g_hourly_count > 8) ? (g_hourly_count / 8) : 1;
                for (int i = 0; i < 8; i++) {
                    int idx = i * step;
                    if (idx >= g_hourly_count) idx = g_hourly_count - 1;
                    temps[i] = (int)roundf(g_hourly_data[idx].temp);

                    // 将时间戳转换为 HH:00
                    time_t t = (time_t)g_hourly_data[idx].timestamp;
                    struct tm timeinfo;
                    localtime_r(&t, &timeinfo);
                    snprintf(time_str_buf[i], sizeof(time_str_buf[i]), "%02d:00", timeinfo.tm_hour);
                    times[i] = time_str_buf[i];
                }
                ui_update_weather_hourly(temps, times, 8);   // 传入时间数组

                // 当前天气描述保持不变
                char cur_buf[128];
                snprintf(cur_buf, sizeof(cur_buf), "%.0f°C   %s",
                        g_hourly_data[0].temp,
                        g_hourly_data[0].desc);
                ui_update_weather_info(cur_buf);

                ESP_LOGI(TAG, "天气数据已更新（%d 条）", g_hourly_count);
            } else {
                ESP_LOGE(TAG, "获取天气数据最终失败，错误码: %d", ret);
            }
            last_weather_update = now;
        }

        // 更新时钟
        if ((now - last_time_update) > pdMS_TO_TICKS(TIME_UPDATE_INTERVAL_SEC * 1000)
            || last_time_update == 0) {
            char time_buf[16], date_buf[16];
            ntp_get_time_str(time_buf, sizeof(time_buf));
            ntp_get_date_str(date_buf, sizeof(date_buf));
            ui_update_weather_time(time_buf);
            ui_update_weather_date(date_buf);
            last_time_update = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void weather_task_start(void)
{
    xTaskCreate(weather_task, "weather_task", 12288, NULL, 4, NULL);
}