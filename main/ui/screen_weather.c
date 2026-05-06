#include "ui.h"
#include "lvgl.h"
#include <string.h>

#define TAG "screen_weather"

// 时间日期标签
lv_obj_t *g_weather_time_label   = NULL;
lv_obj_t *g_weather_date_label   = NULL;

// 今明两天预报行相关
lv_obj_t *g_today_date_label     = NULL;
lv_obj_t *g_tomorrow_date_label  = NULL;
lv_obj_t *g_today_labels[8]      = {NULL};
lv_obj_t *g_tomorrow_labels[8]   = {NULL};

// 跳转按钮回调
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

void screen_weather_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // ================= 顶部栏 =================
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 1024, 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "高三6班");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // ================= 时间和日期显示（左上方） =================
    g_weather_time_label = lv_label_create(scr);
    lv_label_set_text(g_weather_time_label, "--:--:--");
    lv_obj_set_style_text_color(g_weather_time_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(g_weather_time_label, &lv_font_utf_24, 0);
    lv_obj_align(g_weather_time_label, LV_ALIGN_TOP_LEFT, 20, 100);

    g_weather_date_label = lv_label_create(scr);
    lv_label_set_text(g_weather_date_label, "----/--/--");
    lv_obj_set_style_text_color(g_weather_date_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(g_weather_date_label, &lv_font_utf_24, 0);
    lv_obj_align(g_weather_date_label, LV_ALIGN_TOP_LEFT, 20, 140);

    // ================= 今天预报行 =================
    // 行容器
    lv_obj_t *today_row = lv_obj_create(scr);
    lv_obj_set_size(today_row, 960, 120);
    lv_obj_align(today_row, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(today_row, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(today_row, 0, 0);
    lv_obj_set_style_radius(today_row, 10, 0);
    lv_obj_set_flex_flow(today_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(today_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 今天日期标签（行首）
    g_today_date_label = lv_label_create(today_row);
    lv_label_set_text(g_today_date_label, "--/--");
    lv_obj_set_style_text_color(g_today_date_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(g_today_date_label, &lv_font_utf_24, 0);
    lv_obj_set_width(g_today_date_label, 80);   // 固定宽度

    // 今天 8 个预报标签
    for (int i = 0; i < 8; i++) {
        g_today_labels[i] = lv_label_create(today_row);
        lv_label_set_text(g_today_labels[i], "--:--\n--°\n--");
        lv_obj_set_style_text_color(g_today_labels[i], lv_color_hex(0xcccccc), 0);
        lv_obj_set_style_text_font(g_today_labels[i], &lv_font_utf_24, 0);
        lv_obj_set_flex_grow(g_today_labels[i], 1);  // 均匀占满剩余空间
    }

    // ================= 明天预报行 =================
    lv_obj_t *tomorrow_row = lv_obj_create(scr);
    lv_obj_set_size(tomorrow_row, 960, 120);
    lv_obj_align(tomorrow_row, LV_ALIGN_TOP_MID, 0, 340);
    lv_obj_set_style_bg_color(tomorrow_row, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(tomorrow_row, 0, 0);
    lv_obj_set_style_radius(tomorrow_row, 10, 0);
    lv_obj_set_flex_flow(tomorrow_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tomorrow_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 明天日期标签（行首）
    g_tomorrow_date_label = lv_label_create(tomorrow_row);
    lv_label_set_text(g_tomorrow_date_label, "--/--");
    lv_obj_set_style_text_color(g_tomorrow_date_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(g_tomorrow_date_label, &lv_font_utf_24, 0);
    lv_obj_set_width(g_tomorrow_date_label, 80);

    // 明天 8 个预报标签
    for (int i = 0; i < 8; i++) {
        g_tomorrow_labels[i] = lv_label_create(tomorrow_row);
        lv_label_set_text(g_tomorrow_labels[i], "--:--\n--°\n--");
        lv_obj_set_style_text_color(g_tomorrow_labels[i], lv_color_hex(0xcccccc), 0);
        lv_obj_set_style_text_font(g_tomorrow_labels[i], &lv_font_utf_24, 0);
        lv_obj_set_flex_grow(g_tomorrow_labels[i], 1);
    }

    // ================= 底部功能按钮 =================
    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, 960, 70);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(btn_row, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_radius(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 人脸签到按钮
    lv_obj_t *btn_face = lv_btn_create(btn_row);
    lv_obj_set_size(btn_face, 200, 60);
    lv_obj_set_style_bg_color(btn_face, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(btn_face, 10, 0);
    lv_obj_add_event_cb(btn_face, btn_face_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *face_label = lv_label_create(btn_face);
    lv_label_set_text(face_label, "人脸签到");
    lv_obj_set_style_text_font(face_label, &lv_font_utf_24, 0);
    lv_obj_center(face_label);

    // AI助手按钮
    lv_obj_t *btn_ai = lv_btn_create(btn_row);
    lv_obj_set_size(btn_ai, 200, 60);
    lv_obj_set_style_bg_color(btn_ai, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(btn_ai, 10, 0);
    lv_obj_add_event_cb(btn_ai, btn_ai_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ai_label = lv_label_create(btn_ai);
    lv_label_set_text(ai_label, "AI助手");
    lv_obj_set_style_text_font(ai_label, &lv_font_utf_24, 0);
    lv_obj_center(ai_label);
}