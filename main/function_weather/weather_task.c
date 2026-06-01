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

// 动态分配，存储 48 条逐小时数据（两天）
static weather_hourly_t *g_hourly_data = NULL;
static int g_hourly_count = 0;

static void weather_task(void *pvParameters)
{
    // 分配 48 条预报数据的空间（两天逐小时）
    g_hourly_data = (weather_hourly_t *)malloc(sizeof(weather_hourly_t) * 48);
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
                                             g_hourly_data, 48, &g_hourly_count);
                if (ret != 0) {
                    // 等待 65s 确保 TIME_WAIT 回收完毕（配合 MSL=15s 可缩短至 20s+，但保守起见保持 65s）
                    ESP_LOGE(TAG, "获取天气失败，重试 %d/3（65s 后），错误码: %d", retry + 1, ret);
                    vTaskDelay(pdMS_TO_TICKS(65000));
                    retry++;
                }
            }

            if (ret == 0 && g_hourly_count > 0) {
                // 拆分为今天和明天各 8 个点（每 3 小时取一个）
                int today_temps[8], tomorrow_temps[8];
                const char *today_descs[8], *tomorrow_descs[8];
                const char *today_times[8], *tomorrow_times[8];
                char today_time_buf[8][6], tomorrow_time_buf[8][6];

                int step = 3;  // 逐小时数据，间隔 3 小时取点
                // 今天数据：索引 0~23，取 i*step
                for (int i = 0; i < 8; i++) {
                    int idx = i * step;
                    if (idx >= g_hourly_count) idx = g_hourly_count - 1;
                    today_temps[i] = (int)roundf(g_hourly_data[idx].temp);
                    today_descs[i] = g_hourly_data[idx].desc;
                    time_t t = (time_t)g_hourly_data[idx].timestamp;
                    struct tm tm_info;
                    localtime_r(&t, &tm_info);
                    snprintf(today_time_buf[i], sizeof(today_time_buf[i]), "%02d:%02d",
                             tm_info.tm_hour, tm_info.tm_min);
                    today_times[i] = today_time_buf[i];
                }
                // 明天数据：索引从第24小时开始（0~23是今天，24~47是明天）
                int tomorrow_start = step * 8;  // = 24（如果有完整48条）
                if (tomorrow_start >= g_hourly_count) {
                    tomorrow_start = g_hourly_count / 2;  // 数据不够时折半
                }
                for (int i = 0; i < 8; i++) {
                    int idx = tomorrow_start + i * step;
                    if (idx >= g_hourly_count) idx = g_hourly_count - 1;
                    tomorrow_temps[i] = (int)roundf(g_hourly_data[idx].temp);
                    tomorrow_descs[i] = g_hourly_data[idx].desc;
                    time_t t = (time_t)g_hourly_data[idx].timestamp;
                    struct tm tm_info;
                    localtime_r(&t, &tm_info);
                    snprintf(tomorrow_time_buf[i], sizeof(tomorrow_time_buf[i]), "%02d:%02d",
                             tm_info.tm_hour, tm_info.tm_min);
                    tomorrow_times[i] = tomorrow_time_buf[i];
                }

                if (g_hourly_count < 16) {
                    ESP_LOGW(TAG, "警告：只获取到 %d 条数据，预计少于48条，UI可能不完整", g_hourly_count);
                }

                // 计算今天和明天的日期
                time_t today_ts = (time_t)g_hourly_data[0].timestamp;
                int tomorrow_idx = step * 8;
                if (tomorrow_idx >= g_hourly_count) tomorrow_idx = g_hourly_count - 1;
                time_t tomorrow_ts = (time_t)g_hourly_data[tomorrow_idx].timestamp;
                struct tm tm_today, tm_tomorrow;
                localtime_r(&today_ts, &tm_today);
                localtime_r(&tomorrow_ts, &tm_tomorrow);
                char today_date[16], tomorrow_date[16];
                strftime(today_date, sizeof(today_date), "%m-%d", &tm_today);
                strftime(tomorrow_date, sizeof(tomorrow_date), "%m-%d", &tm_tomorrow);

                // 寻找今天高亮索引（距离当前时间最近的点）
                time_t now_ts = time(NULL);
                int highlight = 0;
                time_t min_diff = (time_t)abs((int)(g_hourly_data[0].timestamp - now_ts));
                for (int i = 1; i < 8; i++) {
                    int idx = i * step;
                    if (idx >= g_hourly_count) break;
                    time_t diff = (time_t)abs((int)(g_hourly_data[idx].timestamp - now_ts));
                    if (diff < min_diff) {
                        min_diff = diff;
                        highlight = i;
                    }
                }
                ESP_LOGI(TAG, "highlight index = %d", highlight);
                // 更新 UI
                ui_update_today_forecast(today_temps, today_descs, today_times, today_date, highlight);
                ui_update_tomorrow_forecast(tomorrow_temps, tomorrow_descs, tomorrow_times, tomorrow_date);

                ESP_LOGI(TAG, "天气数据已更新（今天 %d 点，明天 %d 点）", 8, 8);
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