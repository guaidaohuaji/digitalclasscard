/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp/config.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "esp_lv_adapter_display.h"

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 1)
typedef struct {
    bsp_display_config_t hw_cfg;    /*!< Display HW configuration */
} bsp_display_cfg_t;
#endif

lv_display_t *lvgl_adapter_init(const bsp_display_cfg_t *cfg);

/**
 * @brief 注册 LVGL 文件系统驱动字母 'S'，映射到 SD 卡根目录
 * 
 * 调用此函数后，LVGL 可通过 "S:/filename" 路径访问 SD 卡上的文件。
 * 必须在 SD 卡挂载成功后调用。
 */
void lvgl_adapter_register_sd_fs(void);

#ifdef __cplusplus
}
#endif
