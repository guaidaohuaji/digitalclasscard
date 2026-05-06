#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "lvgl_adapter_init.h"
#include "ui.h"
#include "esp_lv_adapter.h"
#include "wifi_manager.h"
#include "weather_task.h"
#include <assert.h>

#define TAG "main"

// ---- 全局事件组，用于各任务间同步 ----
// Bit0: Wi-Fi 已连接且获取到 IP
// Bit1: 可扩展（如人脸模块就绪等）
EventGroupHandle_t g_system_event_group;

void app_main(void)
{
    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 创建系统事件组
    g_system_event_group = xEventGroupCreate();
    assert(g_system_event_group != NULL);

    // 3. 初始化显示和 LVGL
    bsp_display_cfg_t cfg = {
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            },
        },
    };
    lv_display_t *disp = lvgl_adapter_init(&cfg);
    assert(disp != NULL && "Failed to init LVGL adapter");
    bsp_display_backlight_on();

    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    ui_init(disp);
    esp_lv_adapter_unlock();

    // 4. 启动 Wi-Fi（内部创建 Network Task）
    wifi_manager_start();

    // 5. 启动天气任务（内部等待 Wi-Fi 就绪后再同步 NTP 并获取天气）
    weather_task_start();

    // 6. app_main 自身退化为 UI 刷新任务
    while (1) {
        ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
        lv_timer_handler();
        esp_lv_adapter_unlock();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}