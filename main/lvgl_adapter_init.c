/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lvgl_adapter_init.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "bsp/esp-bsp.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lvgl_adapter_init";

#define LVGL_ADAPTER_BUFFER_HEIGHT 10

static void lvgl_adapter_get_resolution(uint32_t *out_hres, uint32_t *out_vres)
{
    if (out_hres) {
        *out_hres = BSP_LCD_H_RES;
    }
    if (out_vres) {
        *out_vres = BSP_LCD_V_RES;
    }
}

lv_display_t *lvgl_adapter_init(const bsp_display_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Display config is NULL");
        return NULL;
    }

    bsp_lcd_handles_t handles = { 0 };
    esp_err_t err = bsp_display_new_with_handles(&cfg->hw_cfg, &handles);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP display init failed (%d)", err);
        return NULL;
    }

    const esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    err = esp_lv_adapter_init(&adapter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed (%d)", err);
        return NULL;
    }

    uint32_t hres = 0;
    uint32_t vres = 0;
    lvgl_adapter_get_resolution(&hres, &vres);

    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        handles.panel, handles.io, hres, vres, ESP_LV_ADAPTER_ROTATE_0);

    disp_cfg.profile.buffer_height = LVGL_ADAPTER_BUFFER_HEIGHT;
    disp_cfg.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (disp == NULL) {
        /* Retry with internal SRAM if PSRAM allocation failed */
        ESP_LOGW(TAG, "PSRAM display buffer failed, retrying with internal SRAM");
        disp_cfg.profile.use_psram = false;
        disp = esp_lv_adapter_register_display(&disp_cfg);
    }
    if (disp == NULL) {
        ESP_LOGE(TAG, "Register display failed");
        return NULL;
    }

    esp_lcd_touch_handle_t touch = NULL;
    err = bsp_touch_new(NULL, &touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed (%d)", err);
        return NULL;
    }

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
    lv_indev_t *indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (indev == NULL) {
        ESP_LOGE(TAG, "Register touch failed");
        return NULL;
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed (%d)", err);
        return NULL;
    }

    return disp;
}

/* ============================================================================
 * LVGL 文件系统驱动 'S:' → SD 卡根目录
 * ========================================================================== */
#include "esp_vfs_fat.h"

static void *sd_fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    const char *fs_mode;
    if (mode & LV_FS_MODE_WR) {
        fs_mode = "rb+";
    } else {
        fs_mode = "rb";
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", BSP_SD_MOUNT_POINT, path);
    FILE *f = fopen(full_path, fs_mode);
    if (!f) {
        ESP_LOGE(TAG, "SD FS: fopen(%s) failed", full_path);
        return NULL;
    }
    return f;
}

static lv_fs_res_t sd_fs_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    if (file_p) fclose((FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    *br = (uint32_t)fread(buf, 1, btr, (FILE *)file_p);
    return (*br > 0) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sd_fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    int w;
    switch (whence) {
    case LV_FS_SEEK_SET:
        w = SEEK_SET;
        break;
    case LV_FS_SEEK_CUR:
        w = SEEK_CUR;
        break;
    case LV_FS_SEEK_END:
        w = SEEK_END;
        break;
    default:
        return LV_FS_RES_INV_PARAM;
    }
    fseek((FILE *)file_p, (long)pos, w);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
    long pos = ftell((FILE *)file_p);
    if (pos < 0) return LV_FS_RES_FS_ERR;
    *pos_p = (uint32_t)pos;
    return LV_FS_RES_OK;
}

void lvgl_adapter_register_sd_fs(void)
{
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = 'S';
    fs_drv.cache_size = 0;
    fs_drv.open_cb = sd_fs_open;
    fs_drv.close_cb = sd_fs_close;
    fs_drv.read_cb = sd_fs_read;
    fs_drv.seek_cb = sd_fs_seek;
    fs_drv.tell_cb = sd_fs_tell;
    lv_fs_drv_register(&fs_drv);

    ESP_LOGI(TAG, "LVGL FS driver 'S:' registered → %s", BSP_SD_MOUNT_POINT);
}
