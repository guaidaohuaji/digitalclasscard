#include "ui.h"
#include "lvgl.h"
#include "audio_recorder.h"
#include "ai_chat_api.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lv_adapter.h"

static const char *TAG = "screen_ai";

static lv_obj_t *status_label = NULL; // 底部状态标签
static lv_obj_t *chat_box = NULL;     // 对话框容器
static lv_obj_t *rec_bar = NULL;      // 录音进度条
static lv_obj_t *rec_time_label = NULL; // 录音时间标签
static lv_timer_t *rec_timer = NULL;  // 录音进度刷新定时器
static uint32_t    rec_start_ms = 0;   // 录音开始时刻 (ms)
static bool        s_processing = false; // 防止重复处理

// 前向声明
static void rec_timer_cb(lv_timer_t *timer);

// FreeType 中文字体 (从 SD 卡加载)
static lv_font_t *g_chinese_font = NULL;
static bool       g_font_ready = false;

// 向对话框添加一条文字
static void chat_add_message(const char *role, const char *text, lv_color_t color)
{
    if (!chat_box) return;

    // 创建消息容器
    lv_obj_t *msg = lv_obj_create(chat_box);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_obj_set_height(msg, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(msg, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(msg, 0, 0);
    lv_obj_set_style_pad_all(msg, 5, 0);

    // 文字标签
    lv_obj_t *label = lv_label_create(msg);
    lv_label_set_text_fmt(label, "[%s] %s", role, text);
    lv_obj_set_style_text_color(label, color, 0);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(label, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(label, &lv_font_utf_24, 0);
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

// AI 处理任务 (在单独任务中执行，避免阻塞 LVGL)
static void ai_process_task(void *arg)
{
    (void)arg;

    char asr_text[512] = {0};
    char ai_reply[1024] = {0};

    // 调用 AI 处理
    esp_err_t ret = ai_chat_process(
        "/sdcard/ai_record.wav",
        asr_text, sizeof(asr_text),
        ai_reply, sizeof(ai_reply)
    );

    // LVGL 非线程安全，必须加锁
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "你说: %s", asr_text);
        ESP_LOGI(TAG, "小班: %s", ai_reply);
        if (status_label) {
            lv_label_set_text(status_label, "Hold button to start speaking");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xaaaaaa), 0);
        }
        if (chat_box) {
            chat_add_message("你", asr_text, lv_color_hex(0x4fc3f7));
            chat_add_message("小班", ai_reply, lv_color_hex(0xa5d6a7));
        }
    } else {
        ESP_LOGE(TAG, "AI 处理失败 (err=%d): %s", ret, ai_reply);
        if (status_label) {
            lv_label_set_text(status_label, "Failed, please retry");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xff4444), 0);
        }
        if (chat_box) {
            chat_add_message("系统", ai_reply, lv_color_hex(0xff6b6b));
        }
    }

    esp_lv_adapter_unlock();

    s_processing = false;
    vTaskDelete(NULL);
}

// 按下录音按钮
static void mic_press_cb(lv_event_t *e)
{
    if (s_processing) return;
    audio_recorder_start("/sdcard/ai_record.wav");
    if (status_label) {
        lv_label_set_text(status_label, "● Recording...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xff4444), 0);
    }
    // 显示进度条并启动定时器
    if (rec_bar) lv_obj_clear_flag(rec_bar, LV_OBJ_FLAG_HIDDEN);
    if (rec_time_label) lv_obj_clear_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);
    rec_start_ms = lv_tick_get();
    if (!rec_timer) {
        rec_timer = lv_timer_create(rec_timer_cb, 200, NULL);
    } else {
        lv_timer_resume(rec_timer);
    }
    ESP_LOGI(TAG, "Recording started");
}

// 松开录音按钮
static void mic_release_cb(lv_event_t *e)
{
    if (s_processing) return;
    audio_recorder_stop();
    s_processing = true;

    // WiFi 连接检查
    if (!wifi_manager_is_connected()) {
        ESP_LOGE(TAG, "WiFi 未连接，无法进行 AI 处理");
        if (status_label) {
            lv_label_set_text(status_label, "WiFi 未连接，请检查网络");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xff4444), 0);
        }
        if (chat_box) {
            chat_add_message("系统", "WiFi 未连接，请检查网络后重试", lv_color_hex(0xff6b6b));
        }
        s_processing = false;
        return;
    }

    // 停止录音进度定时器并隐藏进度条
    if (rec_timer) lv_timer_pause(rec_timer);
    if (rec_bar) lv_obj_add_flag(rec_bar, LV_OBJ_FLAG_HIDDEN);
    if (rec_time_label) lv_obj_add_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);

    if (status_label) {
        lv_label_set_text(status_label, "思考中...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xffaa00), 0);
    }
    ESP_LOGI(TAG, "Recording stopped, starting AI processing");
    xTaskCreate(ai_process_task, "ai_task", 12288, NULL, 5, NULL);
}

// 录音进度刷新定时器回调
static void rec_timer_cb(lv_timer_t *timer)
{
    uint32_t elapsed = lv_tick_get() - rec_start_ms;
    uint32_t seconds = elapsed / 1000;
    uint32_t max_seconds = 60;  // 与 audio_recorder.c 的 MAX_RECORD_SECONDS 保持一致
    int32_t progress = (seconds < max_seconds) ? (int32_t)(seconds * 100 / max_seconds) : 100;

    if (rec_time_label) {
        lv_label_set_text_fmt(rec_time_label, "%lus / %lus", seconds, max_seconds);
    }
    if (rec_bar) {
        lv_bar_set_value(rec_bar, progress, LV_ANIM_ON);
    }
}

// 返回按钮
static void btn_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_show_weather();
    }
}

void screen_ai_font_init(void)
{
#if defined(CONFIG_LV_USE_FREETYPE) && CONFIG_LV_USE_FREETYPE
    if (g_font_ready) return;

    g_chinese_font = lv_freetype_font_create("S:/simhei.ttf",
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              24,
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    if (g_chinese_font) {
        ESP_LOGI(TAG, "FreeType 中文字体加载成功 (24px)");
        g_font_ready = true;
    } else {
        ESP_LOGW(TAG, "FreeType 字体加载失败，降级为 lv_font_utf_24");
    }
#endif
}

void screen_ai_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    // -------- 顶部栏 --------
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
    lv_label_set_text(title, "AI Assistant");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(title, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(title, &lv_font_utf_24, 0);
    }
    lv_obj_center(title);

    // -------- 对话框 --------
    chat_box = lv_obj_create(scr);
    lv_obj_set_size(chat_box, 900, 380);
    lv_obj_align(chat_box, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(chat_box, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(chat_box, 0, 0);
    lv_obj_set_style_radius(chat_box, 10, 0);
    lv_obj_set_flex_flow(chat_box, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *chat_hint = lv_label_create(chat_box);
    lv_label_set_text(chat_hint, "对话将显示在这里...");
    lv_obj_set_style_text_color(chat_hint, lv_color_hex(0x666666), 0);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(chat_hint, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(chat_hint, &lv_font_utf_24, 0);
    }

    // -------- 录音进度条区域 (默认隐藏) --------
    rec_bar = lv_bar_create(scr);
    lv_obj_set_size(rec_bar, 500, 10);
    lv_obj_align(rec_bar, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_style_bg_color(rec_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rec_bar, lv_color_hex(0xe53935), LV_PART_INDICATOR);
    lv_bar_set_range(rec_bar, 0, 100);
    lv_bar_set_value(rec_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(rec_bar, LV_OBJ_FLAG_HIDDEN);

    rec_time_label = lv_label_create(scr);
    lv_label_set_text(rec_time_label, "0s / 60s");
    lv_obj_set_style_text_color(rec_time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(rec_time_label, LV_ALIGN_BOTTOM_MID, 0, -125);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(rec_time_label, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(rec_time_label, &lv_font_utf_24, 0);
    }
    lv_obj_add_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);

    // -------- 底部输入区域 --------
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_size(bottom, 900, 70);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_radius(bottom, 10, 0);

    lv_obj_t *mic_btn = lv_btn_create(bottom);
    lv_obj_set_size(mic_btn, 120, 50);
    lv_obj_align(mic_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0xce93d8), 0);
    lv_obj_t *mic_label = lv_label_create(mic_btn);
    lv_label_set_text(mic_label, LV_SYMBOL_AUDIO " 按住说话");
    lv_obj_center(mic_label);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(mic_label, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(mic_label, &lv_font_utf_24, 0);
    }

    // 状态提示
    status_label = lv_label_create(bottom);
    lv_label_set_text(status_label, "Hold button to start speaking");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 10, 0);
    if (g_font_ready && g_chinese_font) {
        lv_obj_set_style_text_font(status_label, g_chinese_font, 0);
    } else {
        lv_obj_set_style_text_font(status_label, &lv_font_utf_24, 0);
    }

    // 绑定录音按钮事件
    lv_obj_add_event_cb(mic_btn, mic_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(mic_btn, mic_release_cb, LV_EVENT_RELEASED, NULL);
}