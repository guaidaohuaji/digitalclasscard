#include "ui.h"
#include "lvgl.h"

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