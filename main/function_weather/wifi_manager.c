#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <string.h>

#define TAG "wifi_mgr"

#define WIFI_SSID      "jjs"
#define WIFI_PASSWORD  "2sheng2shi"
#define WIFI_MAX_RETRY 10

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_count < WIFI_MAX_RETRY) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "重连 %d/%d", s_retry_count, WIFI_MAX_RETRY);
            } else {
                ESP_LOGE(TAG, "重连次数耗尽");
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获得 IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_task(void *pvParameters)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi‑Fi 启动完成，开始连接...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);
}

bool wifi_manager_is_connected(void)
{
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_manager_wait_connected(TickType_t timeout_ticks)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE, timeout_ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}