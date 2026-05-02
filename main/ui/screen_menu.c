#include "ui.h"
#include "lvgl.h"

static void btn_weather_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_weather();
    }
}

static void btn_face_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_face();
    }
}

static void btn_ai_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_ai();
    }
}

void screen_menu_create(lv_obj_t *scr)
{
    // 背景颜色
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // 顶部标题栏
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 1024, 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "数字班牌系统");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // 三个功能按钮
    // 按钮1：时间天气
    lv_obj_t *btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 280, 300);
    lv_obj_align(btn1, LV_ALIGN_CENTER, -320, 30);
    lv_obj_set_style_bg_color(btn1, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(btn1, 20, 0);
    lv_obj_add_event_cb(btn1, btn_weather_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon1 = lv_label_create(btn1);
    lv_label_set_text(icon1, MY_SYMBOL_WEATHER);
    lv_obj_set_style_text_font(icon1, &myicon_font, 0);
    lv_obj_set_style_text_color(icon1, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(icon1, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl1 = lv_label_create(btn1);
    lv_label_set_text(lbl1, "时间天气");
    lv_obj_set_style_text_color(lbl1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl1, &lv_font_utf_24, 0);
    lv_obj_align(lbl1, LV_ALIGN_CENTER, 0, 40);

    // 按钮2：人脸签到
    lv_obj_t *btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 280, 300);
    lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn2, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(btn2, 20, 0);
    lv_obj_add_event_cb(btn2, btn_face_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon2 = lv_label_create(btn2);
    lv_label_set_text(icon2, MY_SYMBOL_USER);
    lv_obj_set_style_text_font(icon2, &myicon_font, 0);
    lv_obj_set_style_text_color(icon2, lv_color_hex(0x81c784), 0);
    lv_obj_align(icon2, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl2 = lv_label_create(btn2);
    lv_label_set_text(lbl2, "人脸签到");
    lv_obj_set_style_text_color(lbl2, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl2, &lv_font_utf_24, 0);
    lv_obj_align(lbl2, LV_ALIGN_CENTER, 0, 40);

    // 按钮3：AI对话
    lv_obj_t *btn3 = lv_btn_create(scr);
    lv_obj_set_size(btn3, 280, 300);
    lv_obj_align(btn3, LV_ALIGN_CENTER, 320, 30);
    lv_obj_set_style_bg_color(btn3, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(btn3, 20, 0);
    lv_obj_add_event_cb(btn3, btn_ai_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon3 = lv_label_create(btn3);
    lv_label_set_text(icon3, MY_SYMBOL_CHAT);
    lv_obj_set_style_text_font(icon3, &myicon_font, 0);
    lv_obj_set_style_text_color(icon3, lv_color_hex(0xce93d8), 0);
    lv_obj_align(icon3, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl3 = lv_label_create(btn3);
    lv_label_set_text(lbl3, "AI助手");
    lv_obj_set_style_text_color(lbl3, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl3, &lv_font_utf_24, 0);
    lv_obj_align(lbl3, LV_ALIGN_CENTER, 0, 40);
}