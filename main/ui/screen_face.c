#include "ui.h"
#include "lvgl.h"
#include "camera_preview.h"
#include "face_detector.h"
#include "esp_lv_adapter.h"

#define FACE_BOX_COUNT FACE_DETECT_MAX_RESULTS

static lv_obj_t *s_face_boxes[FACE_BOX_COUNT] = {0};
static lv_obj_t *s_face_score_labels[FACE_BOX_COUNT] = {0};
static lv_obj_t *s_detect_info = NULL;

static void hide_face_boxes_locked(void)
{
    for (int i = 0; i < FACE_BOX_COUNT; i++) {
        if (s_face_boxes[i]) {
            lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_detect_info) {
        lv_label_set_text(s_detect_info, "AI: idle");
    }
}

static void face_detect_ui_cb(const face_detect_result_t *results,
                              size_t count,
                              uint32_t latency_ms)
{
    if (esp_lv_adapter_lock(-1) != ESP_OK) return;

    size_t shown = count;
    if (shown > FACE_BOX_COUNT) shown = FACE_BOX_COUNT;

    for (size_t i = 0; i < FACE_BOX_COUNT; i++) {
        lv_obj_t *box = s_face_boxes[i];
        if (!box) continue;

        if (i < shown) {
            int width = results[i].x2 - results[i].x1 + 1;
            int height = results[i].y2 - results[i].y1 + 1;
            if (width < 2) width = 2;
            if (height < 2) height = 2;

            lv_obj_set_pos(box, results[i].x1, results[i].y1);
            lv_obj_set_size(box, width, height);
            lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);

            if (s_face_score_labels[i]) {
                lv_label_set_text_fmt(s_face_score_labels[i], "%.0f%%", results[i].score * 100.0f);
            }
        } else {
            lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_detect_info) {
        lv_label_set_text_fmt(s_detect_info, "AI: %u face | %u ms",
                              (unsigned int)shown,
                              (unsigned int)latency_ms);
    }

    esp_lv_adapter_unlock();
}

static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        hide_face_boxes_locked();
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

    s_detect_info = lv_label_create(header);
    lv_label_set_text(s_detect_info, "AI: idle");
    lv_obj_set_style_text_color(s_detect_info, lv_color_hex(0x81c784), 0);
    lv_obj_align(s_detect_info, LV_ALIGN_RIGHT_MID, -18, 0);

    // 摄像头预览区域：640x360 (16:9)
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

    // 透明叠加层只负责显示检测框，不修改摄像头帧本身。
    lv_obj_t *face_overlay = lv_obj_create(cam_box);
    lv_obj_set_size(face_overlay, 640, 360);
    lv_obj_center(face_overlay);
    lv_obj_set_style_bg_opa(face_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_overlay, 0, 0);
    lv_obj_set_style_pad_all(face_overlay, 0, 0);
    lv_obj_set_style_radius(face_overlay, 0, 0);
    lv_obj_clear_flag(face_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(face_overlay, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < FACE_BOX_COUNT; i++) {
        s_face_boxes[i] = lv_obj_create(face_overlay);
        lv_obj_set_style_bg_opa(s_face_boxes[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_face_boxes[i], lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_border_width(s_face_boxes[i], 3, 0);
        lv_obj_set_style_radius(s_face_boxes[i], 4, 0);
        lv_obj_set_style_pad_all(s_face_boxes[i], 2, 0);
        lv_obj_clear_flag(s_face_boxes[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s_face_boxes[i], LV_OBJ_FLAG_CLICKABLE);

        s_face_score_labels[i] = lv_label_create(s_face_boxes[i]);
        lv_label_set_text(s_face_score_labels[i], "--");
        lv_obj_set_style_text_color(s_face_score_labels[i], lv_color_hex(0x00ff88), 0);
        lv_obj_align(s_face_score_labels[i], LV_ALIGN_TOP_LEFT, 2, 2);

        lv_obj_add_flag(s_face_boxes[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 底部状态提示
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "进入页面后将自动启动摄像头");
    lv_obj_set_style_text_color(status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(status, &lv_font_utf_24, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -28);

    face_detector_set_result_callback(face_detect_ui_cb);
    camera_preview_bind_ui(camera_canvas, status);
}
