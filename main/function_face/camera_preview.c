#include "camera_preview.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ppa.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <string.h>

#define TAG "camera_preview"

#define CAPTURE_BUF_COUNT   2
#define PREVIEW_WIDTH       640
#define PREVIEW_HEIGHT      360
#define PREVIEW_BPP         2
#define PREVIEW_TASK_STACK  (8 * 1024)
#define PREVIEW_TASK_PRIO   6
#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

typedef struct {
    void *addr;
    size_t len;
} capture_buf_t;

static lv_obj_t *s_canvas = NULL;
static lv_obj_t *s_status = NULL;
static bool s_camera_initialized = false;
static volatile bool s_running = false;
static volatile bool s_stop_requested = false;
static TaskHandle_t s_task = NULL;
static int s_fd = -1;
static capture_buf_t s_capture[CAPTURE_BUF_COUNT];
static uint8_t *s_preview_buf[CAPTURE_BUF_COUNT] = {0};
static size_t s_preview_buf_size = 0;
static size_t s_cache_align = 64;
static ppa_client_handle_t s_ppa = NULL;
static lv_color_format_t s_lv_fmt = LV_COLOR_FORMAT_RGB565;

static void ui_set_status(const char *text, uint32_t color)
{
    if (!s_status) return;
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        lv_label_set_text(s_status, text);
        lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
        esp_lv_adapter_unlock();
    }
}

static void cleanup_capture_buffers(void)
{
    for (int i = 0; i < CAPTURE_BUF_COUNT; i++) {
        if (s_capture[i].addr && s_capture[i].addr != MAP_FAILED) {
            munmap(s_capture[i].addr, s_capture[i].len);
        }
        s_capture[i].addr = NULL;
        s_capture[i].len = 0;
    }
}

static void close_video(void)
{
    cleanup_capture_buffers();
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
}

static esp_err_t ensure_camera_initialized(void)
{
    if (s_camera_initialized) return ESP_OK;

    esp_err_t ret = bsp_camera_start(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_camera_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ret = ppa_register_client(&ppa_cfg, &s_ppa);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_align) != ESP_OK || s_cache_align == 0) {
        s_cache_align = 64;
    }

    s_preview_buf_size = ALIGN_UP(PREVIEW_WIDTH * PREVIEW_HEIGHT * PREVIEW_BPP, s_cache_align);
    for (int i = 0; i < CAPTURE_BUF_COUNT; i++) {
        s_preview_buf[i] = heap_caps_aligned_calloc(
            s_cache_align, 1, s_preview_buf_size, MALLOC_CAP_SPIRAM);
        if (!s_preview_buf[i]) {
            ESP_LOGE(TAG, "Failed to allocate preview buffer %d", i);
            return ESP_ERR_NO_MEM;
        }
    }

    s_camera_initialized = true;
    ESP_LOGI(TAG, "Camera subsystem initialized, preview=%dx%d", PREVIEW_WIDTH, PREVIEW_HEIGHT);
    return ESP_OK;
}

static esp_err_t open_and_configure_video(void)
{
    s_fd = open(BSP_CAMERA_DEVICE, O_RDONLY);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", BSP_CAMERA_DEVICE);
        return ESP_FAIL;
    }

    struct v4l2_capability cap = {0};
    if (ioctl(s_fd, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
        close_video();
        return ESP_FAIL;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
        close_video();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera native format: %lux%lu fourcc=0x%08lx",
             (unsigned long)fmt.fmt.pix.width,
             (unsigned long)fmt.fmt.pix.height,
             (unsigned long)fmt.fmt.pix.pixelformat);

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 &&
        fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565X) {
        struct v4l2_format requested = fmt;
        requested.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(s_fd, VIDIOC_S_FMT, &requested) == 0 &&
            ioctl(s_fd, VIDIOC_G_FMT, &fmt) == 0) {
            ESP_LOGI(TAG, "Camera converted to RGB565");
        }
    }

    if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565X) {
        s_lv_fmt = LV_COLOR_FORMAT_RGB565_SWAPPED;
    } else if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565) {
        s_lv_fmt = LV_COLOR_FORMAT_RGB565;
    } else {
        ESP_LOGE(TAG, "Camera output is not RGB565 (fourcc=0x%08lx)",
                 (unsigned long)fmt.fmt.pix.pixelformat);
        close_video();
        return ESP_ERR_NOT_SUPPORTED;
    }

    struct v4l2_requestbuffers req = {0};
    req.count = CAPTURE_BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0 || req.count < CAPTURE_BUF_COUNT) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        close_video();
        return ESP_FAIL;
    }

    for (int i = 0; i < CAPTURE_BUF_COUNT; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF(%d) failed", i);
            close_video();
            return ESP_FAIL;
        }

        s_capture[i].len = buf.length;
        s_capture[i].addr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, s_fd, buf.m.offset);
        if (s_capture[i].addr == MAP_FAILED || s_capture[i].addr == NULL) {
            ESP_LOGE(TAG, "mmap(%d) failed", i);
            close_video();
            return ESP_FAIL;
        }

        if (ioctl(s_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF(%d) failed", i);
            close_video();
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t scale_to_preview(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                                  uint8_t *dst, size_t dst_size)
{
    if (!s_ppa) return ESP_ERR_INVALID_STATE;

    float sx = (float)PREVIEW_WIDTH / (float)src_w;
    float sy = (float)PREVIEW_HEIGHT / (float)src_h;
    float scale = sx < sy ? sx : sy;
    uint32_t out_w = (uint32_t)(src_w * scale);
    uint32_t out_h = (uint32_t)(src_h * scale);
    if (out_w == 0 || out_h == 0) return ESP_ERR_INVALID_SIZE;

    ppa_srm_oper_config_t cfg = {
        .in.buffer = (void *)src,
        .in.pic_w = src_w,
        .in.pic_h = src_h,
        .in.block_w = src_w,
        .in.block_h = src_h,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = dst,
        .out.buffer_size = dst_size,
        .out.pic_w = PREVIEW_WIDTH,
        .out.pic_h = PREVIEW_HEIGHT,
        .out.block_offset_x = (PREVIEW_WIDTH - out_w) / 2,
        .out.block_offset_y = (PREVIEW_HEIGHT - out_h) / 2,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale,
        .scale_y = scale,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    memset(dst, 0, dst_size);
    return ppa_do_scale_rotate_mirror(s_ppa, &cfg);
}

static void preview_task(void *arg)
{
    (void)arg;
    bool stream_on = false;

    ui_set_status("正在启动摄像头...", 0xaaaaaa);

    esp_err_t ret = ensure_camera_initialized();
    if (ret != ESP_OK || s_stop_requested) {
        if (ret != ESP_OK) ui_set_status("摄像头初始化失败", 0xff4444);
        goto exit_task;
    }

    ret = open_and_configure_video();
    if (ret != ESP_OK || s_stop_requested) {
        if (ret != ESP_OK) ui_set_status("无法打开摄像头，请检查模组", 0xff4444);
        goto exit_task;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ui_set_status("读取摄像头格式失败", 0xff4444);
        goto exit_task;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_STREAMON, &type) != 0) {
        ui_set_status("摄像头启动失败", 0xff4444);
        goto exit_task;
    }
    stream_on = true;

    ui_set_status("摄像头已开启，请面向摄像头", 0x81c784);

    while (!s_stop_requested) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(s_fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_DQBUF failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (buf.index < CAPTURE_BUF_COUNT) {
            uint8_t *dst = s_preview_buf[buf.index];
            ret = scale_to_preview((const uint8_t *)s_capture[buf.index].addr,
                                   fmt.fmt.pix.width, fmt.fmt.pix.height,
                                   dst, s_preview_buf_size);
            if (ret == ESP_OK && s_canvas && !s_stop_requested) {
                if (esp_lv_adapter_lock(-1) == ESP_OK) {
                    lv_canvas_set_buffer(s_canvas, dst, PREVIEW_WIDTH, PREVIEW_HEIGHT, s_lv_fmt);
                    lv_obj_invalidate(s_canvas);
                    esp_lv_adapter_unlock();
                }
            }
        }

        if (ioctl(s_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed while streaming");
            break;
        }
    }

exit_task:
    if (stream_on && s_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(s_fd, VIDIOC_STREAMOFF, &type);
    }
    close_video();
    s_running = false;
    s_stop_requested = false;
    s_task = NULL;
    ESP_LOGI(TAG, "Preview task stopped");
    vTaskDelete(NULL);
}

void camera_preview_bind_ui(lv_obj_t *canvas, lv_obj_t *status_label)
{
    s_canvas = canvas;
    s_status = status_label;
}

esp_err_t camera_preview_start(void)
{
    if (s_running) return ESP_OK;
    if (!s_canvas) return ESP_ERR_INVALID_STATE;

    s_stop_requested = false;
    s_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(preview_task, "camera_preview",
                                            PREVIEW_TASK_STACK, NULL,
                                            PREVIEW_TASK_PRIO, &s_task, 0);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void camera_preview_stop(void)
{
    if (s_running) {
        s_stop_requested = true;
    }
}

bool camera_preview_is_running(void)
{
    return s_running;
}
