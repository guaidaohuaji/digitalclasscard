#include "ui.h"
#include "lvgl.h"
#include "camera_preview.h"

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_weather();
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

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(btn_back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "人脸签到");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    // 摄像头预览区域：640x360 (16:9)，与后续人脸检测叠加层保持独立
    lv_obj_t *cam_box = lv_obj_create(scr);
    lv_obj_set_size(cam_box, 660, 380);
    lv_obj_align(cam_box, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(cam_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(cam_box, lv_color_hex(0x81c784), 0);
    lv_obj_set_style_border_width(cam_box, 2, 0);
    lv_obj_set_style_radius(cam_box, 10, 0);
    lv_obj_set_style_pad_all(cam_box, 8, 0);
    lv_obj_clear_flag(cam_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *camera_canvas = lv_canvas_create(cam_box);
    lv_obj_set_size(camera_canvas, 640, 360);
    lv_obj_center(camera_canvas);
    lv_obj_set_style_bg_color(camera_canvas, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(camera_canvas, LV_OPA_COVER, 0);

    // 底部状态提示
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "进入页面后将自动启动摄像头");
    lv_obj_set_style_text_color(status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(status, &lv_font_utf_24, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -28);

    camera_preview_bind_ui(camera_canvas, status);
}
