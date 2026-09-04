#include "ui.h"
#include "lvgl.h"
#include "camera_preview.h"
#include "face_detector.h"
#include "esp_lv_adapter.h"

#define FACE_BOX_COUNT FACE_DETECT_MAX_RESULTS

static lv_obj_t *s_face_boxes[FACE_BOX_COUNT] = {0};
static lv_obj_t *s_face_score_labels[FACE_BOX_COUNT] = {0};
static lv_obj_t *s_detect_info = NULL;
static lv_obj_t *s_identity_label = NULL;
static lv_obj_t *s_db_label = NULL;

static void set_identity_locked(const char *text, uint32_t color)
{
    if (!s_identity_label) return;
    lv_label_set_text(s_identity_label, text);
    lv_obj_set_style_text_color(s_identity_label, lv_color_hex(color), 0);
}

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
    if (s_db_label) {
        lv_label_set_text(s_db_label, "DB: --");
    }
    set_identity_locked("等待识别", 0xaaaaaa);
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
        lv_label_set_text_fmt(s_detect_info, "Detect: %u | %u ms",
                              (unsigned int)shown,
                              (unsigned int)latency_ms);
    }

    esp_lv_adapter_unlock();
}

static void face_identity_ui_cb(const face_identity_event_t *event)
{
    if (!event) return;
    if (esp_lv_adapter_lock(-1) != ESP_OK) return;

    if (s_db_label) {
        lv_label_set_text_fmt(s_db_label, "DB: %u",
                              (unsigned int)event->database_count);
    }

    switch (event->type) {
    case FACE_IDENTITY_RECOGNIZED:
        if (s_identity_label) {
            lv_label_set_text_fmt(s_identity_label,
                                  "识别成功\nID: %u\n相似度: %.1f%%\n%u ms",
                                  (unsigned int)event->id,
                                  event->similarity * 100.0f,
                                  (unsigned int)event->latency_ms);
            lv_obj_set_style_text_color(s_identity_label, lv_color_hex(0x00ff88), 0);
        }
        break;

    case FACE_IDENTITY_UNKNOWN:
        if (event->database_count == 0) {
            set_identity_locked("人脸库为空\n请先注册", 0xffc857);
        } else {
            set_identity_locked("未知人员", 0xff6b6b);
        }
        break;

    case FACE_IDENTITY_ENROLLED:
        if (s_identity_label) {
            lv_label_set_text_fmt(s_identity_label,
                                  "注册成功\nID: %u\n%u ms",
                                  (unsigned int)event->id,
                                  (unsigned int)event->latency_ms);
            lv_obj_set_style_text_color(s_identity_label, lv_color_hex(0x00ff88), 0);
        }
        break;

    case FACE_IDENTITY_ENROLL_WAIT_FACE:
        set_identity_locked("注册中...\n请面向摄像头", 0xffc857);
        break;

    case FACE_IDENTITY_ENROLL_WAIT_SINGLE_FACE:
        set_identity_locked("注册中...\n画面中请只保留一人", 0xffc857);
        break;

    case FACE_IDENTITY_DB_CLEARED:
        set_identity_locked("人脸库已清空", 0xffc857);
        break;

    case FACE_IDENTITY_DB_ERROR:
    default:
        set_identity_locked("人脸库错误\n请检查 SD 卡", 0xff4444);
        break;
    }

    esp_lv_adapter_unlock();
}

static void btn_enroll_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    esp_err_t ret = face_detector_request_enroll();
    if (ret == ESP_OK) {
        set_identity_locked("注册中...\n请面向摄像头", 0xffc857);
    } else {
        set_identity_locked("无法开始注册", 0xff4444);
    }
}

static void btn_clear_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;

    esp_err_t ret = face_detector_request_clear_database();
    if (ret == ESP_OK) {
        set_identity_locked("正在清空人脸库...", 0xffc857);
    } else {
        set_identity_locked("无法清空人脸库", 0xff4444);
    }
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
    lv_label_set_text(title, "人脸注册 / 识别");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    lv_obj_center(title);

    s_detect_info = lv_label_create(header);
    lv_label_set_text(s_detect_info, "AI: idle");
    lv_obj_set_style_text_color(s_detect_info, lv_color_hex(0x81c784), 0);
    lv_obj_align(s_detect_info, LV_ALIGN_RIGHT_MID, -18, 0);

    // 摄像头预览区域：640x360 (16:9)，向左偏移给识别控制区留空间。
    lv_obj_t *cam_box = lv_obj_create(scr);
    lv_obj_set_size(cam_box, 660, 380);
    lv_obj_align(cam_box, LV_ALIGN_CENTER, -90, -20);
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

    // 右侧识别信息 / 操作区。
    lv_obj_t *side_panel = lv_obj_create(scr);
    lv_obj_set_size(side_panel, 210, 380);
    lv_obj_align(side_panel, LV_ALIGN_CENTER, 350, -20);
    lv_obj_set_style_bg_color(side_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(side_panel, 0, 0);
    lv_obj_set_style_radius(side_panel, 10, 0);
    lv_obj_set_style_pad_all(side_panel, 12, 0);
    lv_obj_clear_flag(side_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_db_label = lv_label_create(side_panel);
    lv_label_set_text(s_db_label, "DB: --");
    lv_obj_set_style_text_color(s_db_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(s_db_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_identity_label = lv_label_create(side_panel);
    lv_label_set_text(s_identity_label, "等待识别");
    lv_obj_set_width(s_identity_label, 180);
    lv_obj_set_style_text_align(s_identity_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_identity_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(s_identity_label, &lv_font_utf_24, 0);
    lv_obj_align(s_identity_label, LV_ALIGN_TOP_MID, 0, 45);

    lv_obj_t *btn_enroll = lv_btn_create(side_panel);
    lv_obj_set_size(btn_enroll, 170, 56);
    lv_obj_align(btn_enroll, LV_ALIGN_BOTTOM_MID, 0, -78);
    lv_obj_set_style_bg_color(btn_enroll, lv_color_hex(0x0f6b50), 0);
    lv_obj_add_event_cb(btn_enroll, btn_enroll_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *enroll_label = lv_label_create(btn_enroll);
    lv_label_set_text(enroll_label, "注册一人");
    lv_obj_set_style_text_font(enroll_label, &lv_font_utf_24, 0);
    lv_obj_center(enroll_label);

    lv_obj_t *btn_clear = lv_btn_create(side_panel);
    lv_obj_set_size(btn_clear, 170, 56);
    lv_obj_align(btn_clear, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0x6b3030), 0);
    lv_obj_add_event_cb(btn_clear, btn_clear_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *clear_label = lv_label_create(btn_clear);
    lv_label_set_text(clear_label, "长按清空");
    lv_obj_set_style_text_font(clear_label, &lv_font_utf_24, 0);
    lv_obj_center(clear_label);

    // 底部状态提示
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "进入页面后将自动启动摄像头");
    lv_obj_set_style_text_color(status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(status, &lv_font_utf_24, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, -90, -18);

    face_detector_set_result_callback(face_detect_ui_cb);
    face_detector_set_identity_callback(face_identity_ui_cb);
    camera_preview_bind_ui(camera_canvas, status);
}
