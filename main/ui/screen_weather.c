#include "ui.h"
#include "lvgl.h"
#include <string.h>
#include "esp_log.h"

#define TAG "screen_weather"

// ---- 外部控件句柄（供 ui_update_weather_* 使用）----
lv_obj_t *g_weather_time_label   = NULL;
lv_obj_t *g_weather_date_label   = NULL;
lv_obj_t *g_weather_cur_label    = NULL;
lv_obj_t *g_weather_cur_icon     = NULL;
lv_obj_t *g_weather_chart        = NULL;
lv_chart_series_t *g_temp_series = NULL;
lv_obj_t *g_hourly_panel         = NULL;
lv_obj_t *g_hourly_labels[8]     = {NULL};  // 3小时间隔最多 8 个

/* 温度数据外部数组（供图表刷新） */
int32_t g_temp_data[8] = {0};

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_menu();
    }
}

void screen_weather_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // ======================= 顶部栏 =======================
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 1024, 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(btn_back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "时间天气");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // ======================= 时间区域（左上方） =======================
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

    // ======================= 当前天气区域（右上方） =======================
    g_weather_cur_icon = lv_label_create(scr);
    lv_label_set_text(g_weather_cur_icon, "  ");
    lv_obj_set_style_text_font(g_weather_cur_icon, &myicon_font, 0);  // 大图标字体
    lv_obj_set_style_text_color(g_weather_cur_icon, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(g_weather_cur_icon, LV_ALIGN_TOP_RIGHT, -40, 100);

    g_weather_cur_label = lv_label_create(scr);
    lv_label_set_text(g_weather_cur_label, "--°C   --");
    lv_obj_set_style_text_color(g_weather_cur_label, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_text_font(g_weather_cur_label, &lv_font_utf_24, 0);
    lv_obj_align(g_weather_cur_label, LV_ALIGN_TOP_RIGHT, -40, 160);

    // ======================= 温度曲线（中部） =======================
    g_weather_chart = lv_chart_create(scr);
    lv_obj_set_size(g_weather_chart, 700, 200);
    lv_obj_align(g_weather_chart, LV_ALIGN_CENTER, 0, -20);
    lv_chart_set_type(g_weather_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(g_weather_chart, LV_CHART_AXIS_PRIMARY_Y, -20, 50);
    lv_chart_set_point_count(g_weather_chart, 8);
    lv_chart_set_div_line_count(g_weather_chart, 3, 8);
    lv_chart_set_update_mode(g_weather_chart, LV_CHART_UPDATE_MODE_SHIFT);

    g_temp_series = lv_chart_add_series(g_weather_chart, lv_color_hex(0x4fc3f7),
                                         LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(g_weather_chart, g_temp_series, g_temp_data);

    // 样式：主背景 + 分割线颜色
    lv_obj_set_style_bg_color(g_weather_chart, lv_color_hex(0x16213e), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_weather_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(g_weather_chart, lv_color_hex(0x333355), LV_PART_ITEMS);

    // ======================= 底部逐时预报卡片 =======================
    g_hourly_panel = lv_obj_create(scr);
    lv_obj_set_size(g_hourly_panel, 960, 70);
    lv_obj_align(g_hourly_panel, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(g_hourly_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(g_hourly_panel, 0, 0);
    lv_obj_set_style_radius(g_hourly_panel, 10, 0);
    lv_obj_set_flex_flow(g_hourly_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_hourly_panel, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 8; i++) {
        g_hourly_labels[i] = lv_label_create(g_hourly_panel);
        lv_label_set_text(g_hourly_labels[i], "--°\n --:--");
        lv_obj_set_style_text_color(g_hourly_labels[i], lv_color_hex(0xcccccc), 0);
        lv_obj_set_style_text_font(g_hourly_labels[i], &lv_font_utf_24, 0);
    }
}