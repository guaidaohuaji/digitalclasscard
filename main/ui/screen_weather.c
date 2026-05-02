#include "ui.h"
#include "lvgl.h"

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_menu();
    }
}

void screen_weather_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // 顶部栏
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 1024, 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    // 返回按钮
    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(btn_back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // 标题
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "时间天气");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // 时间显示（占位）
    lv_obj_t *time_label = lv_label_create(scr);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_utf_24, 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -60);

    // 日期显示（占位）
    lv_obj_t *date_label = lv_label_create(scr);
    lv_label_set_text(date_label, "2026-01-01");
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(date_label, &lv_font_utf_24, 0);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 20);

    // 天气显示（占位）
    lv_obj_t *weather_label = lv_label_create(scr);
    lv_label_set_text(weather_label, "天气: -- °C");
    lv_obj_set_style_text_color(weather_label, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_text_font(weather_label, &lv_font_utf_24, 0);
    lv_obj_align(weather_label, LV_ALIGN_CENTER, 0, 80);
}