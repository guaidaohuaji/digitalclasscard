#include "ui.h"
#include "lvgl.h"

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_menu();
    }
}

void screen_face_create(lv_obj_t *scr)
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
    lv_label_set_text(title, "人脸签到");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // 摄像头预览占位框
    lv_obj_t *cam_box = lv_obj_create(scr);
    lv_obj_set_size(cam_box, 400, 300);
    lv_obj_align(cam_box, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(cam_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(cam_box, lv_color_hex(0x81c784), 0);
    lv_obj_set_style_border_width(cam_box, 2, 0);
    lv_obj_set_style_radius(cam_box, 10, 0);

    lv_obj_t *cam_label = lv_label_create(cam_box);
    lv_label_set_text(cam_label, "摄像头画面");
    lv_obj_set_style_text_color(cam_label, lv_color_hex(0x666666), 0);
    lv_obj_center(cam_label);

    // 状态提示
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "请面向摄像头进行签到");
    lv_obj_set_style_text_color(status, lv_color_hex(0x81c784), 0);
    lv_obj_set_style_text_font(status, &lv_font_utf_24, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 180);
}