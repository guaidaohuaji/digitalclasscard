#include "ui.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"   
#include <stdio.h>            

static lv_display_t *g_disp = NULL;

// 各界面的屏幕对象
static lv_obj_t *scr_menu    = NULL;
static lv_obj_t *scr_weather = NULL;
static lv_obj_t *scr_face    = NULL;
static lv_obj_t *scr_ai      = NULL;

// 各界面的创建函数声明
extern void screen_menu_create(lv_obj_t *scr);
extern void screen_weather_create(lv_obj_t *scr);
extern void screen_face_create(lv_obj_t *scr);
extern void screen_ai_create(lv_obj_t *scr);

void ui_init(lv_display_t *disp)
{
    g_disp = disp;

    // 创建所有界面的屏幕对象
    scr_menu    = lv_obj_create(NULL);
    scr_weather = lv_obj_create(NULL);
    scr_face    = lv_obj_create(NULL);
    scr_ai      = lv_obj_create(NULL);

    // 初始化各界面内容
    screen_menu_create(scr_menu);
    screen_weather_create(scr_weather);
    screen_face_create(scr_face);
    screen_ai_create(scr_ai);

    // 默认显示主菜单
    ui_show_menu();
}

void ui_show_menu(void)
{
    lv_screen_load_anim(scr_menu, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
}

void ui_show_weather(void)
{
    lv_screen_load_anim(scr_weather, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_show_face(void)
{
    lv_screen_load_anim(scr_face, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_show_ai(void)
{
    lv_screen_load_anim(scr_ai, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

// ===================== 天气 UI 更新函数 =====================
// 这些函数由天气任务在获取数据后调用

// screen_weather.c 中定义的控件句柄
extern lv_obj_t *g_weather_time_label;
extern lv_obj_t *g_weather_date_label;
extern lv_obj_t *g_weather_cur_label;
extern lv_obj_t *g_weather_cur_icon;
extern lv_obj_t *g_weather_chart;
extern lv_chart_series_t *g_temp_series;
extern int32_t g_temp_data[8];
extern lv_obj_t *g_hourly_labels[8];

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

void ui_update_weather_info(const char *weather_str)
{
    if (g_weather_cur_label) {
        esp_lv_adapter_lock(-1);
        lv_label_set_text(g_weather_cur_label, weather_str);
        esp_lv_adapter_unlock();
    }
}

void ui_update_weather_hourly(int temps[], const char *times[], int count)
{
    if (count > 8) count = 8;

    esp_lv_adapter_lock(-1);

    // 更新图表数据（保持不变）
    for (int i = 0; i < count; i++) {
        g_temp_data[i] = temps[i];
    }
    lv_chart_set_ext_y_array(g_weather_chart, g_temp_series, g_temp_data);
    lv_chart_refresh(g_weather_chart);

    // 更新底部逐时标签：格式“HH:00\n温度°C”
    for (int i = 0; i < count; i++) {
        if (g_hourly_labels[i]) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s\n%d°", times[i] ? times[i] : "--:--", temps[i]);
            lv_label_set_text(g_hourly_labels[i], buf);
        }
    }

    esp_lv_adapter_unlock();
}