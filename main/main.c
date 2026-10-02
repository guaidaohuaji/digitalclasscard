/*
 * ======================== 学习注释：系统入口 ========================
 *
 * 建议先读这个文件，再沿着函数调用进入各功能模块。
 *
 * app_main() 是 ESP-IDF 应用入口，当前工程的初始化顺序大致为：
 *   1. 打印 DRAM / PSRAM 状态，便于判断大图像缓冲区是否有足够内存；
 *   2. 初始化 NVS（Wi-Fi 等 ESP-IDF 组件可能依赖 NVS）；
 *   3. 打开 SD 卡供电；
 *   4. 创建系统 EventGroup；
 *   5. 初始化 MIPI-DSI 显示、LVGL 与触摸；
 *   6. 创建各个 UI 页面；
 *   7. 设置板级 I2C 上拉；
 *   8. 挂载 SD 卡，并把 SD 卡注册为 LVGL 的文件系统 "S:"；
 *   9. 初始化音频录制模块；
 *  10. 启动 Wi-Fi Task 和 Weather Task。
 *
 * 需要特别理解：
 * - app_main() 只负责“搭系统骨架”，真正长期运行的逻辑都在 FreeRTOS Task 中。
 * - LVGL 的 timer/task 循环由 esp_lv_adapter_start() 创建并维护，app_main()
 *   不能再次调用 lv_timer_handler()，否则会出现两个执行上下文同时驱动 LVGL。
 * - 本工程使用 PSRAM 保存较大的显示/摄像头/推理缓冲区，因此启动时打印内存很有意义。
 * ====================================================================
 */
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
#include "audio_recorder.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include <assert.h>

#define TAG "main"

#define SD_PWR_GPIO    GPIO_NUM_45
#define I2C_SDA_IO     GPIO_NUM_7
#define I2C_SCL_IO     GPIO_NUM_8

EventGroupHandle_t g_system_event_group;

void app_main(void)
{
    ESP_LOGI(TAG, "=== Memory Diagnostics ===");
    ESP_LOGI(TAG, "Free DRAM: %lu bytes", esp_get_free_internal_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Largest free PSRAM block: %lu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "==========================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_set_direction(SD_PWR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(SD_PWR_GPIO, 0);
    ESP_LOGI(TAG, "SD card power ON (GPIO%d)", SD_PWR_GPIO);

    g_system_event_group = xEventGroupCreate();
    assert(g_system_event_group != NULL);

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

    gpio_set_pull_mode(I2C_SDA_IO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(I2C_SCL_IO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "I2C pullups enabled on GPIO%d/GPIO%d", I2C_SDA_IO, I2C_SCL_IO);

    ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Recording will be disabled");
    } else {
        ESP_LOGI(TAG, "SD card mounted successfully at %s", BSP_SD_MOUNT_POINT);
        lvgl_adapter_register_sd_fs();
    }

    if (audio_recorder_init() != ESP_OK) {
        ESP_LOGE(TAG, "Audio recorder init failed");
    }

    wifi_manager_start();
    weather_task_start();

    /* esp_lv_adapter_start() already owns the LVGL timer/task loop.
     * Do not call lv_timer_handler() again from app_main(), otherwise LVGL
     * can be driven by two execution contexts and exhibit timing/race issues.
     */
    ESP_LOGI(TAG, "System initialization complete");
}
