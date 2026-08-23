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
#include <string.h>

#define TAG "weather_task"

#define WEATHER_UPDATE_INTERVAL_SEC 1800
#define TIME_UPDATE_INTERVAL_SEC    1
#define CONNECT_RETRY_DELAY_MS      5000
#define NTP_RETRY_DELAY_MS          10000
#define WEATHER_RETRY_DELAY_MS      15000

#define SCHOOL_LAT 30.2741f
#define SCHOOL_LON 120.1551f

static weather_hourly_t *g_hourly_data = NULL;
static int g_hourly_count = 0;

static bool same_calendar_day(time_t a, time_t b)
{
    struct tm ta, tb;
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
    return ta.tm_year == tb.tm_year && ta.tm_yday == tb.tm_yday;
}

static int collect_day_indices(time_t day_ref, int *indices, int max_indices)
{
    int count = 0;
    for (int i = 0; i < g_hourly_count && count < max_indices; i++) {
        if (same_calendar_day((time_t)g_hourly_data[i].timestamp, day_ref)) {
            indices[count++] = i;
        }
    }
    return count;
}

static int nearest_index_for_hour(const int *indices, int count, int target_hour)
{
    if (count <= 0) return -1;
    int best = indices[0];
    int best_diff = 100;
    for (int i = 0; i < count; i++) {
        struct tm ti;
        time_t ts = (time_t)g_hourly_data[indices[i]].timestamp;
        localtime_r(&ts, &ti);
        int diff = abs(ti.tm_hour - target_hour);
        if (diff < best_diff) {
            best_diff = diff;
            best = indices[i];
        }
    }
    return best;
}

static void build_day_ui_data(time_t day_ref,
                              int temps[8], const char *descs[8], const char *times[8],
                              char time_buf[8][6], char date_buf[16],
                              int *highlight_idx, bool highlight_current_time)
{
    int indices[24];
    int count = collect_day_indices(day_ref, indices, 24);
    const int target_hours[8] = {0, 3, 6, 9, 12, 15, 18, 21};

    for (int i = 0; i < 8; i++) {
        int idx = nearest_index_for_hour(indices, count, target_hours[i]);
        if (idx < 0) {
            temps[i] = 0;
            descs[i] = "--";
            snprintf(time_buf[i], 6, "--:--");
            times[i] = time_buf[i];
            continue;
        }

        temps[i] = (int)roundf(g_hourly_data[idx].temp);
        descs[i] = g_hourly_data[idx].desc;

        struct tm ti;
        time_t ts = (time_t)g_hourly_data[idx].timestamp;
        localtime_r(&ts, &ti);
        snprintf(time_buf[i], 6, "%02d:%02d", ti.tm_hour, ti.tm_min);
        times[i] = time_buf[i];
    }

    struct tm td;
    localtime_r(&day_ref, &td);
    strftime(date_buf, 16, "%m-%d", &td);

    if (highlight_idx) {
        *highlight_idx = 0;
        if (highlight_current_time && count > 0) {
            time_t now = time(NULL);
            time_t min_diff = labs((long)((time_t)g_hourly_data[nearest_index_for_hour(indices, count, target_hours[0])].timestamp - now));
            for (int i = 1; i < 8; i++) {
                int idx = nearest_index_for_hour(indices, count, target_hours[i]);
                if (idx < 0) continue;
                time_t diff = labs((long)((time_t)g_hourly_data[idx].timestamp - now));
                if (diff < min_diff) {
                    min_diff = diff;
                    *highlight_idx = i;
                }
            }
        }
    }
}

static bool wait_for_wifi_forever(void)
{
    while (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "等待 Wi-Fi 连接...");
        if (wifi_manager_wait_connected(pdMS_TO_TICKS(10000))) return true;
        vTaskDelay(pdMS_TO_TICKS(CONNECT_RETRY_DELAY_MS));
    }
    return true;
}

static bool ensure_time_synced(void)
{
    if (time(NULL) > 1700000000) return true;

    while (1) {
        if (!wait_for_wifi_forever()) continue;
        ESP_LOGI(TAG, "开始 NTP 时间同步...");
        if (ntp_time_sync(30)) return true;
        ESP_LOGW(TAG, "NTP 同步失败，%d ms 后重试", NTP_RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(NTP_RETRY_DELAY_MS));
    }
}

static bool fetch_weather_with_retry(void)
{
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (!wait_for_wifi_forever()) continue;
        int ret = weather_fetch_forecast(SCHOOL_LAT, SCHOOL_LON,
                                         g_hourly_data, 48, &g_hourly_count);
        if (ret == 0 && g_hourly_count > 0) return true;

        ESP_LOGE(TAG, "获取天气失败，尝试 %d/3，错误码=%d", attempt, ret);
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_DELAY_MS));
    }
    return false;
}

static void update_forecast_ui(void)
{
    time_t now_ts = time(NULL);
    struct tm now_tm;
    localtime_r(&now_ts, &now_tm);

    struct tm tomorrow_tm = now_tm;
    tomorrow_tm.tm_mday += 1;
    tomorrow_tm.tm_hour = 12;
    tomorrow_tm.tm_min = 0;
    tomorrow_tm.tm_sec = 0;
    time_t tomorrow_ts = mktime(&tomorrow_tm);

    int today_temps[8], tomorrow_temps[8];
    const char *today_descs[8], *tomorrow_descs[8];
    const char *today_times[8], *tomorrow_times[8];
    char today_time_buf[8][6], tomorrow_time_buf[8][6];
    char today_date[16], tomorrow_date[16];
    int highlight = 0;

    build_day_ui_data(now_ts,
                      today_temps, today_descs, today_times,
                      today_time_buf, today_date, &highlight, true);
    build_day_ui_data(tomorrow_ts,
                      tomorrow_temps, tomorrow_descs, tomorrow_times,
                      tomorrow_time_buf, tomorrow_date, NULL, false);

    ui_update_today_forecast(today_temps, today_descs, today_times, today_date, highlight);
    ui_update_tomorrow_forecast(tomorrow_temps, tomorrow_descs, tomorrow_times, tomorrow_date);

    ESP_LOGI(TAG, "天气 UI 已按本地自然日更新，highlight=%d", highlight);
}

static void weather_task(void *pvParameters)
{
    g_hourly_data = (weather_hourly_t *)malloc(sizeof(weather_hourly_t) * 48);
    if (!g_hourly_data) {
        ESP_LOGE(TAG, "无法分配天气预报缓冲区");
        vTaskDelete(NULL);
        return;
    }

    wait_for_wifi_forever();
    ensure_time_synced();

    TickType_t last_weather_update = 0;
    TickType_t last_time_update = 0;

    while (1) {
        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "Wi-Fi 断开，等待恢复...");
            wait_for_wifi_forever();
            last_weather_update = 0;
        }

        if (time(NULL) <= 1700000000) {
            ensure_time_synced();
        }

        TickType_t now_tick = xTaskGetTickCount();

        if (last_weather_update == 0 ||
            (now_tick - last_weather_update) >= pdMS_TO_TICKS(WEATHER_UPDATE_INTERVAL_SEC * 1000)) {
            if (fetch_weather_with_retry()) {
                update_forecast_ui();
                last_weather_update = xTaskGetTickCount();
            } else {
                ESP_LOGW(TAG, "本轮天气更新失败，将在 60 秒后再次尝试");
                vTaskDelay(pdMS_TO_TICKS(60000));
                last_weather_update = 0;
            }
        }

        now_tick = xTaskGetTickCount();
        if (last_time_update == 0 ||
            (now_tick - last_time_update) >= pdMS_TO_TICKS(TIME_UPDATE_INTERVAL_SEC * 1000)) {
            char time_buf[16], date_buf[16];
            ntp_get_time_str(time_buf, sizeof(time_buf));
            ntp_get_date_str(date_buf, sizeof(date_buf));
            ui_update_weather_time(time_buf);
            ui_update_weather_date(date_buf);
            last_time_update = now_tick;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void weather_task_start(void)
{
    xTaskCreate(weather_task, "weather_task", 12288, NULL, 4, NULL);
}
