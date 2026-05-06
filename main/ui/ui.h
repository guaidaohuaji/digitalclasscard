#pragma once
#include "lvgl.h"
/* 声明字体 */
LV_FONT_DECLARE(myicon_font);

/* 定义图标宏（根据你生成的 Unicode 编码转换） */
#define MY_SYMBOL_USER    "\xEF\x80\x87" // U+F007 (人脸/用户图标)
#define MY_SYMBOL_CHAT    "\xEF\x83\xA5" // U+F0E5 (对话图标)
#define MY_SYMBOL_WEATHER    "\xEF\x83\xA9" // U+F0E9 (助手/火图标)
// 界面切换函数
void ui_init(lv_display_t *disp);
void ui_show_menu(void);
void ui_show_weather(void);
void ui_show_face(void);
void ui_show_ai(void);


void ui_update_weather_time(const char *time_str);
void ui_update_weather_date(const char *date_str);
void ui_update_weather_info(const char *weather_str);
void ui_update_weather_hourly(int temps[], const char *times[], int count);
LV_FONT_DECLARE(lv_font_utf_24);