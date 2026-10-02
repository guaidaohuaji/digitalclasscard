/*
 * ======================= 学习注释：NTP/SNTP 校时 =====================
 *
 * 网络层次：
 *   SNTP/NTP -> UDP -> IP -> Wi-Fi
 *
 * 本工程直接配置 NTP Server 的 IP，而不是域名，因此省去了 DNS 查询。
 * esp_netif_sntp_init() 内部负责 UDP socket、NTP 报文收发和系统时间更新，
 * 应用层不需要自己调用 sendto()/recvfrom()。
 *
 * time_sync_notification_cb() 是“时间同步完成”回调。
 * 同步成功后设置 TZ=CST-8，再通过 localtime_r()/strftime() 得到北京时间字符串。
 *
 * 注意：NTP 返回不是 UDP 广播，而是服务器针对客户端请求返回的响应。
 * ====================================================================
 */
#include "ntp_time.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include <string.h>

#define TAG "ntp"

static bool s_time_synced = false;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP 时间同步成功");
    s_time_synced = true;
}

bool ntp_time_sync(uint32_t timeout_sec)
{
    s_time_synced = false;

    // 使用阿里云 NTP 服务器的 IP 地址，避免 DNS 解析
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("203.107.6.88");
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);

    // 等待同步
    uint32_t elapsed = 0;
    while (!s_time_synced && elapsed < timeout_sec) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        elapsed++;
    }

    if (!s_time_synced) {
        ESP_LOGE(TAG, "NTP 同步超时 (%lu 秒)", timeout_sec);
        return false;
    }

    // 设置时区为中国标准时间 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    return true;
}

void ntp_get_time_str(char *buf, size_t buf_size)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%H:%M:%S", &timeinfo);
}

void ntp_get_date_str(char *buf, size_t buf_size)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, buf_size, "%Y-%m-%d", &timeinfo);
}