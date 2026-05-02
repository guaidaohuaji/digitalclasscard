#include "ui.h"
#include "lvgl.h"

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_menu();
    }
}

void screen_ai_create(lv_obj_t *scr)
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
    lv_label_set_text(title, "AI助手");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // 对话框
    lv_obj_t *chat_box = lv_obj_create(scr);
    lv_obj_set_size(chat_box, 900, 380);
    lv_obj_align(chat_box, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(chat_box, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(chat_box, 0, 0);
    lv_obj_set_style_radius(chat_box, 10, 0);
    lv_obj_set_flex_flow(chat_box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *chat_hint = lv_label_create(chat_box);
    lv_label_set_text(chat_hint, "对话内容将显示在这里...");
    lv_obj_set_style_text_color(chat_hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(chat_hint, &lv_font_utf_24, 0);
    // 底部输入区域
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_size(bottom, 900, 70);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_radius(bottom, 10, 0);

    // 麦克风按钮
    lv_obj_t *mic_btn = lv_btn_create(bottom);
    lv_obj_set_size(mic_btn, 120, 50);
    lv_obj_align(mic_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0xce93d8), 0);
    lv_obj_t *mic_label = lv_label_create(mic_btn);
    lv_label_set_text(mic_label, LV_SYMBOL_AUDIO " 说话");
    lv_obj_center(mic_label);
    lv_obj_set_style_text_font(mic_label, &lv_font_utf_24, 0);
    // 状态提示
    lv_obj_t *status = lv_label_create(bottom);
    lv_label_set_text(status, "点击麦克风开始说话");
    lv_obj_set_style_text_color(status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(status, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_font(status, &lv_font_utf_24, 0);
}