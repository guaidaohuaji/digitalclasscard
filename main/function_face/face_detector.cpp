#include "face_detector.h"

#include "human_face_detect.hpp"
#include "dl_image_define.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstring>
#include <new>

#define TAG "face_detector"

#define DETECTOR_TASK_STACK   (12 * 1024)
#define DETECTOR_TASK_PRIO    5
#define DETECTOR_TASK_CORE    1
#define DETECTOR_MAX_WIDTH    640
#define DETECTOR_MAX_HEIGHT   360
#define DETECTOR_BPP          2
#define DETECTOR_BUFFER_BYTES (DETECTOR_MAX_WIDTH * DETECTOR_MAX_HEIGHT * DETECTOR_BPP)

typedef struct {
    uint16_t width;
    uint16_t height;
} detector_frame_t;

static QueueHandle_t s_frame_queue = NULL;
static SemaphoreHandle_t s_buffer_free = NULL;
static TaskHandle_t s_task = NULL;
static uint8_t *s_frame_buffer = NULL;
static size_t s_frame_buffer_size = 0;
static size_t s_cache_align = 64;
static volatile bool s_enabled = false;
static face_detect_result_cb_t s_result_cb = NULL;

static int clamp_coord(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void detector_task(void *arg)
{
    (void)arg;

    HumanFaceDetect *detector = new (std::nothrow) HumanFaceDetect();
    if (detector == NULL) {
        ESP_LOGE(TAG, "Failed to create HumanFaceDetect");
        s_enabled = false;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "ESP-DL face detector task ready");

    detector_frame_t frame = {0};
    while (true) {
        if (xQueueReceive(s_frame_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_enabled) {
            xSemaphoreGive(s_buffer_free);
            continue;
        }

        dl::image::img_t img;
        img.data = s_frame_buffer;
        img.width = frame.width;
        img.height = frame.height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

        const int64_t start_us = esp_timer_get_time();
        auto &results = detector->run(img);
        const uint32_t latency_ms = (uint32_t)((esp_timer_get_time() - start_us + 500) / 1000);

        face_detect_result_t output[FACE_DETECT_MAX_RESULTS] = {};
        size_t count = 0;

        for (const auto &res : results) {
            if (count >= FACE_DETECT_MAX_RESULTS) break;
            if (res.box.size() < 4) continue;

            int x1 = clamp_coord(res.box[0], 0, (int)frame.width - 1);
            int y1 = clamp_coord(res.box[1], 0, (int)frame.height - 1);
            int x2 = clamp_coord(res.box[2], 0, (int)frame.width - 1);
            int y2 = clamp_coord(res.box[3], 0, (int)frame.height - 1);
            if (x2 <= x1 || y2 <= y1) continue;

            output[count].x1 = x1;
            output[count].y1 = y1;
            output[count].x2 = x2;
            output[count].y2 = y2;
            output[count].score = res.score;
            count++;
        }

        ESP_LOGI(TAG, "faces=%u latency=%u ms",
                 (unsigned int)count, (unsigned int)latency_ms);

        if (s_enabled && s_result_cb != NULL) {
            s_result_cb(output, count, latency_ms);
        }

        xSemaphoreGive(s_buffer_free);
    }
}

static esp_err_t ensure_detector_worker(void)
{
    if (s_task != NULL) return ESP_OK;

    if (s_frame_queue == NULL) {
        s_frame_queue = xQueueCreate(1, sizeof(detector_frame_t));
        if (s_frame_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create frame queue");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_buffer_free == NULL) {
        s_buffer_free = xSemaphoreCreateBinary();
        if (s_buffer_free == NULL) {
            ESP_LOGE(TAG, "Failed to create frame semaphore");
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(s_buffer_free);
    }

    if (s_frame_buffer == NULL) {
        if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_align) != ESP_OK || s_cache_align == 0) {
            s_cache_align = 64;
        }

        s_frame_buffer_size = DETECTOR_BUFFER_BYTES;
        s_frame_buffer = (uint8_t *)heap_caps_aligned_calloc(
            s_cache_align, 1, s_frame_buffer_size, MALLOC_CAP_SPIRAM);
        if (s_frame_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %u-byte inference buffer",
                     (unsigned int)s_frame_buffer_size);
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t ok = xTaskCreatePinnedToCore(detector_task, "face_inference",
                                             DETECTOR_TASK_STACK, NULL,
                                             DETECTOR_TASK_PRIO, &s_task,
                                             DETECTOR_TASK_CORE);
    if (ok != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "Failed to create face inference task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

extern "C" void face_detector_set_result_callback(face_detect_result_cb_t cb)
{
    s_result_cb = cb;
}

extern "C" esp_err_t face_detector_start(void)
{
    esp_err_t ret = ensure_detector_worker();
    if (ret != ESP_OK) return ret;

    s_enabled = true;
    ESP_LOGI(TAG, "Face detection enabled");
    return ESP_OK;
}

extern "C" void face_detector_stop(void)
{
    if (s_enabled) {
        s_enabled = false;
        ESP_LOGI(TAG, "Face detection disabled");
    }
}

extern "C" esp_err_t face_detector_submit_rgb565(const void *frame,
                                                   size_t frame_bytes,
                                                   uint16_t width,
                                                   uint16_t height)
{
    if (!s_enabled || s_frame_queue == NULL || s_buffer_free == NULL || s_frame_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t required = (size_t)width * (size_t)height * DETECTOR_BPP;
    if (width > DETECTOR_MAX_WIDTH || height > DETECTOR_MAX_HEIGHT ||
        required > s_frame_buffer_size || frame_bytes < required) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Never block the camera task. Busy inference means this frame is dropped. */
    if (xSemaphoreTake(s_buffer_free, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(s_frame_buffer, frame, required);

    detector_frame_t item = {
        .width = width,
        .height = height,
    };

    if (xQueueSend(s_frame_queue, &item, 0) != pdTRUE) {
        xSemaphoreGive(s_buffer_free);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

extern "C" bool face_detector_is_running(void)
{
    return s_enabled;
}
