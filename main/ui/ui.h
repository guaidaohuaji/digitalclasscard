#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(myicon_font);

#define MY_SYMBOL_USER    "\xEF\x80\x87"
#define MY_SYMBOL_CHAT    "\xEF\x83\xA5"
#define MY_SYMBOL_WEATHER "\xEF\x83\xA9"

void ui_init(lv_display_t *disp);
void ui_show_weather(void);
void ui_show_face(void);
void ui_show_ai(void);
void screen_ai_font_init(void);

void ui_update_weather_time(const char *time_str);
void ui_update_weather_date(const char *date_str);

// 更新今天预报
void ui_update_today_forecast(int temps[], const char *descs[], const char *times[],
                              const char *date_str, int highlight_idx);
// 更新明天预报
void ui_update_tomorrow_forecast(int temps[], const char *descs[], const char *times[],
                                 const char *date_str);

LV_FONT_DECLARE(lv_font_utf_24);