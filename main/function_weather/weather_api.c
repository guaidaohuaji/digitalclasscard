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

// ========== Open-Meteo 免费天气 API ==========
// 无需 API Key！
#define OWM_URL             "https://api.open-meteo.com/v1/forecast"
// 48 小时 × 4 数组(timestamp+temp+code+humidity) 的 JSON 约 8~12KB，
// 分配 16KB 确保不截断
#define WEATHER_HTTP_BUF_SIZE 16384
// ============================================

static char *g_http_buf = NULL;
static int  g_http_buf_len = 0;

/* HTTP 事件回调 */
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

/* 构建请求 URL */
static void _build_url(float lat, float lon, char *url_buf, size_t buf_size)
{
    snprintf(url_buf, buf_size,
             "%s?latitude=%.4f&longitude=%.4f"
             "&hourly=temperature_2m,weathercode,relativehumidity_2m"
             "&timeformat=unixtime&forecast_hours=48&timezone=auto",
             OWM_URL, (double)lat, (double)lon);
    ESP_LOGI(TAG, "请求 URL: %s", url_buf);
}

int weather_fetch_forecast(float lat, float lon,
                           weather_hourly_t *hourly_out,
                           int max_count,
                           int *out_count)
{
    *out_count = 0;

    // 1. 分配 HTTP 缓冲区（仅首次）
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

    // 2. HTTP 客户端配置（HTTPS + Keep-Alive 复用连接）
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = _http_event_handler,
        .buffer_size = 4096,
        // ---- Keep-Alive：复用 TLS 连接，避免频繁创建/销毁 socket ----
        .keep_alive_enable = true,
        .keep_alive_idle = 10,     // 10s 空闲后发送 Keep-Alive 探测
        .keep_alive_interval = 5,  // 探测间隔 5s
        .keep_alive_count = 3,     // 最多 3 次探测
        .crt_bundle_attach = esp_crt_bundle_attach,   // 验证证书
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
        // 打印部分响应便于排查
        if (g_http_buf_len > 0) {
            ESP_LOGE(TAG, "响应体(前200字节): %.200s", g_http_buf);
        }
        return -1;
    }

    ESP_LOGI(TAG, "HTTP 成功，%d 字节", g_http_buf_len);

    // 3. 解析 JSON
    cJSON *root = cJSON_Parse(g_http_buf);
    if (!root) {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON 解析失败: %s", err_ptr ? err_ptr : "unknown");
        ESP_LOGE(TAG, "原始响应(前400字节): %.400s", g_http_buf);
        return -2;
    }

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (!hourly) {
        ESP_LOGE(TAG, "缺少 hourly 对象");
        cJSON_Delete(root);
        return -3;
    }

    // 获取时间、温度、天气码、湿度数组
    cJSON *time_arr = cJSON_GetObjectItem(hourly, "time");
    cJSON *temp_arr = cJSON_GetObjectItem(hourly, "temperature_2m");
    cJSON *code_arr = cJSON_GetObjectItem(hourly, "weathercode");
    cJSON *hum_arr  = cJSON_GetObjectItem(hourly, "relativehumidity_2m");

    if (!time_arr || !temp_arr || !code_arr) {
        ESP_LOGE(TAG, "缺少关键数组");
        cJSON_Delete(root);
        return -4;
    }

    int total = cJSON_GetArraySize(time_arr);
    if (total <= 0) {
        ESP_LOGE(TAG, "数组为空");
        cJSON_Delete(root);
        return -5;
    }

    // 4. 逐条填充全部数据（不做抽取，返回全部 48 条）
    int count = 0;
    for (int i = 0; i < total && i < max_count; i++) {
        cJSON *t = cJSON_GetArrayItem(time_arr, i);
        cJSON *v = cJSON_GetArrayItem(temp_arr, i);
        cJSON *w = cJSON_GetArrayItem(code_arr, i);

        if (t && v) {
            hourly_out[count].timestamp = (int32_t)t->valueint;
            hourly_out[count].temp = (float)v->valuedouble;
            if (w) {
                // 将天气码转为字符串，作为 icon 字段
                snprintf(hourly_out[count].icon, sizeof(hourly_out[count].icon),
                         "%d", w->valueint);
            }
            if (hum_arr) {
                cJSON *h = cJSON_GetArrayItem(hum_arr, i);
                if (h) hourly_out[count].humidity = (float)h->valuedouble;
            }
            // 天气描述：简单映射几个常见天气码，你也可以后续扩充
            int code = w ? w->valueint : 0;
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
    }

    *out_count = count;
    ESP_LOGI(TAG, "解析到 %d 条逐小时预报（共 %d 条原始数据）", count, total);
    cJSON_Delete(root);
    return 0;
}

void weather_cleanup(void)
{
    if (g_http_buf) {
        free(g_http_buf);
        g_http_buf = NULL;
    }
}