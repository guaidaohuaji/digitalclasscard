#include "ui.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include <stdio.h>

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
    lv_screen_load_anim(scr_weather, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

void ui_show_face(void)
{
    lv_screen_load_anim(scr_face, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_show_ai(void)
{
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

    // 更新日期
    if (g_today_date_label) {
        lv_label_set_text(g_today_date_label, date_str);
    }

    // 更新8个标签
    for (int i = 0; i < 8; i++) {
        if (g_today_labels[i]) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s\n%d°\n%s",
                     times[i] ? times[i] : "--",
                     temps[i],
                     descs[i] ? descs[i] : "--");
            lv_label_set_text(g_today_labels[i], buf);

            // 高亮处理
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

    // 更新日期
    if (g_tomorrow_date_label) {
        lv_label_set_text(g_tomorrow_date_label, date_str);
    }

    // 更新8个标签，明天全部不高亮
    for (int i = 0; i < 8; i++) {
        if (g_tomorrow_labels[i]) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s\n%d°\n%s",
                     times[i] ? times[i] : "--",
                     temps[i],
                     descs[i] ? descs[i] : "--");
            lv_label_set_text(g_tomorrow_labels[i], buf);

            // 统一默认样式
            lv_obj_set_style_bg_color(g_tomorrow_labels[i], lv_color_hex(0x16213e), 0);
            lv_obj_set_style_text_color(g_tomorrow_labels[i], lv_color_hex(0xcccccc), 0);
        }
    }

    esp_lv_adapter_unlock();
}