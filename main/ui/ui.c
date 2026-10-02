/*
 * ======================== 学习注释：UI 总调度 ========================
 *
 * 本文件不是具体页面布局，而是负责：
 *   - 创建三个主页面；
 *   - 页面之间切换；
 *   - 控制进入/退出人脸页面时摄像头的启动和停止；
 *   - 提供 Weather Task 调用的线程安全 UI 更新接口。
 *
 * 页面关系：
 *   Weather <----> Face
 *      |
 *      +---------> AI
 *
 * 为什么更新 LVGL 前要 esp_lv_adapter_lock()：
 *   LVGL 默认不是线程安全的。Weather Task、Camera/AI 回调等都可能运行在
 *   非 LVGL 任务上下文，因此必须先拿 LVGL 适配层锁，再修改对象。
 *
 * 阅读时重点看 ui_show_face()/ui_show_weather()/ui_show_ai()：
 * 页面切换不仅是动画，也承担摄像头生命周期管理。
 * ====================================================================
 */
#include "ui.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "camera_preview.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "ui"

static lv_display_t *g_disp = NULL;

static lv_obj_t *scr_weather = NULL;
static lv_obj_t *scr_face    = NULL;
static lv_obj_t *scr_ai      = NULL;

extern void screen_weather_create(lv_obj_t *scr);
extern void screen_face_create(lv_obj_t *scr);
extern void screen_ai_create(lv_obj_t *scr);

void ui_init(lv_display_t *disp)
{
    g_disp = disp;
    scr_weather = lv_obj_create(NULL);
    scr_face    = lv_obj_create(NULL);
    scr_ai      = lv_obj_create(NULL);

    screen_weather_create(scr_weather);
    screen_face_create(scr_face);
    screen_ai_create(scr_ai);

    lv_screen_load_anim(scr_weather, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void ui_show_weather(void)
{
    camera_preview_stop();
    lv_screen_load_anim(scr_weather, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

void ui_show_face(void)
{
    lv_screen_load_anim(scr_face, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    esp_err_t ret = camera_preview_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera_preview_start failed: %s", esp_err_to_name(ret));
    }
}

void ui_show_ai(void)
{
    camera_preview_stop();
    lv_screen_load_anim(scr_ai, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

// 以下为天气 UI 更新函数

extern lv_obj_t *g_weather_time_label;
extern lv_obj_t *g_weather_date_label;
extern lv_obj_t *g_today_date_label;
extern lv_obj_t *g_tomorrow_date_label;
extern lv_obj_t *g_today_labels[8];
extern lv_obj_t *g_tomorrow_labels[8];

void ui_update_weather_time(const char *time_str)
{
    if (g_weather_time_label) {
        esp_lv_adapter_lock(-1);
        lv_label_set_text(g_weather_time_label, time_str);
        esp_lv_adapter_unlock();
    }
}

void ui_update_weather_date(const char *date_str)
{
    if (g_weather_date_label) {
        esp_lv_adapter_lock(-1);
        lv_label_set_text(g_weather_date_label, date_str);
        esp_lv_adapter_unlock();
    }
}

void ui_update_today_forecast(int temps[], const char *descs[], const char *times[],
                              const char *date_str, int highlight_idx)
{
    esp_lv_adapter_lock(-1);

    if (g_today_date_label) {
        lv_label_set_text(g_today_date_label, date_str);
    }

    for (int i = 0; i < 8; i++) {
        if (g_today_labels[i]) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s\n%d°\n%s",
                     times[i] ? times[i] : "--",
                     temps[i],
                     descs[i] ? descs[i] : "--");
            lv_label_set_text(g_today_labels[i], buf);

            if (i == highlight_idx) {
                lv_obj_set_style_bg_color(g_today_labels[i], lv_color_hex(0x16213e), 0);
                lv_obj_set_style_text_color(g_today_labels[i], lv_color_hex(0xffd700), 0);
            } else {
                lv_obj_set_style_bg_color(g_today_labels[i], lv_color_hex(0x16213e), 0);
                lv_obj_set_style_text_color(g_today_labels[i], lv_color_hex(0xcccccc), 0);
            }
        }
    }

    esp_lv_adapter_unlock();
}

void ui_update_tomorrow_forecast(int temps[], const char *descs[], const char *times[],
                                 const char *date_str)
{
    esp_lv_adapter_lock(-1);

    if (g_tomorrow_date_label) {
        lv_label_set_text(g_tomorrow_date_label, date_str);
    }

    for (int i = 0; i < 8; i++) {
        if (g_tomorrow_labels[i]) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s\n%d°\n%s",
                     times[i] ? times[i] : "--",
                     temps[i],
                     descs[i] ? descs[i] : "--");
            lv_label_set_text(g_tomorrow_labels[i], buf);

            lv_obj_set_style_bg_color(g_tomorrow_labels[i], lv_color_hex(0x16213e), 0);
            lv_obj_set_style_text_color(g_tomorrow_labels[i], lv_color_hex(0xcccccc), 0);
        }
    }

    esp_lv_adapter_unlock();
}
