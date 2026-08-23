#include "weather_api.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "weather_api"

#define OWM_URL             "https://api.open-meteo.com/v1/forecast"
#define WEATHER_HTTP_BUF_SIZE 16384

static char *g_http_buf = NULL;
static int  g_http_buf_len = 0;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (g_http_buf && (g_http_buf_len + evt->data_len < WEATHER_HTTP_BUF_SIZE)) {
            memcpy(g_http_buf + g_http_buf_len, evt->data, evt->data_len);
            g_http_buf_len += evt->data_len;
            g_http_buf[g_http_buf_len] = 0;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void _build_url(float lat, float lon, char *url_buf, size_t buf_size)
{
    /* forecast_days=2 returns two complete local calendar days. This is
     * important for the UI, which renders fixed Today/Tomorrow rows. */
    snprintf(url_buf, buf_size,
             "%s?latitude=%.4f&longitude=%.4f"
             "&hourly=temperature_2m,weathercode,relativehumidity_2m"
             "&timeformat=unixtime&forecast_days=2&timezone=auto",
             OWM_URL, (double)lat, (double)lon);
    ESP_LOGI(TAG, "请求 URL: %s", url_buf);
}

int weather_fetch_forecast(float lat, float lon,
                           weather_hourly_t *hourly_out,
                           int max_count,
                           int *out_count)
{
    if (!hourly_out || !out_count || max_count <= 0) return -1;
    *out_count = 0;

    if (!g_http_buf) {
        g_http_buf = (char *)malloc(WEATHER_HTTP_BUF_SIZE);
        if (!g_http_buf) {
            ESP_LOGE(TAG, "无法分配 HTTP 缓冲区");
            return -1;
        }
    }
    memset(g_http_buf, 0, WEATHER_HTTP_BUF_SIZE);
    g_http_buf_len = 0;

    char url[512];
    _build_url(lat, lon, url, sizeof(url));

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = _http_event_handler,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return -1;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "HTTP 失败，err=%d, status=%d", err, status_code);
        if (g_http_buf_len > 0) ESP_LOGE(TAG, "响应体(前200字节): %.200s", g_http_buf);
        return -1;
    }

    ESP_LOGI(TAG, "HTTP 成功，%d 字节", g_http_buf_len);

    cJSON *root = cJSON_Parse(g_http_buf);
    if (!root) {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON 解析失败: %s", err_ptr ? err_ptr : "unknown");
        return -2;
    }

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    cJSON *time_arr = hourly ? cJSON_GetObjectItem(hourly, "time") : NULL;
    cJSON *temp_arr = hourly ? cJSON_GetObjectItem(hourly, "temperature_2m") : NULL;
    cJSON *code_arr = hourly ? cJSON_GetObjectItem(hourly, "weathercode") : NULL;
    cJSON *hum_arr  = hourly ? cJSON_GetObjectItem(hourly, "relativehumidity_2m") : NULL;

    if (!time_arr || !temp_arr || !code_arr) {
        ESP_LOGE(TAG, "缺少关键 hourly 数组");
        cJSON_Delete(root);
        return -4;
    }

    int total = cJSON_GetArraySize(time_arr);
    int count = 0;
    for (int i = 0; i < total && count < max_count; i++) {
        cJSON *t = cJSON_GetArrayItem(time_arr, i);
        cJSON *v = cJSON_GetArrayItem(temp_arr, i);
        cJSON *w = cJSON_GetArrayItem(code_arr, i);
        if (!t || !v) continue;

        hourly_out[count].timestamp = (int32_t)t->valuedouble;
        hourly_out[count].temp = (float)v->valuedouble;
        int code = w ? w->valueint : -1;
        snprintf(hourly_out[count].icon, sizeof(hourly_out[count].icon), "%d", code);

        hourly_out[count].humidity = 0.0f;
        if (hum_arr) {
            cJSON *h = cJSON_GetArrayItem(hum_arr, i);
            if (h) hourly_out[count].humidity = (float)h->valuedouble;
        }

        switch (code) {
            case 0: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "晴"); break;
            case 1: case 2: case 3: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "多云"); break;
            case 45: case 48: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "雾"); break;
            case 51: case 53: case 55: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "小雨"); break;
            case 61: case 63: case 65: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "雨"); break;
            case 71: case 73: case 75: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "雪"); break;
            case 95: case 96: case 99: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "雷暴"); break;
            default: snprintf(hourly_out[count].desc, sizeof(hourly_out[count].desc), "未知"); break;
        }
        count++;
    }

    *out_count = count;
    ESP_LOGI(TAG, "解析到 %d 条逐小时预报（原始 %d 条）", count, total);
    cJSON_Delete(root);
    return count > 0 ? 0 : -5;
}

void weather_cleanup(void)
{
    if (g_http_buf) {
        free(g_http_buf);
        g_http_buf = NULL;
    }
}
