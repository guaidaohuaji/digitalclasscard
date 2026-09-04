#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACE_DETECT_MAX_RESULTS 5

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
    float score;
} face_detect_result_t;

typedef void (*face_detect_result_cb_t)(const face_detect_result_t *results,
                                        size_t count,
                                        uint32_t latency_ms);

/**
 * @brief Set callback used to publish the latest detection results.
 *
 * The callback runs from the face inference task. UI callbacks therefore need
 * to acquire the LVGL adapter lock before touching LVGL objects.
 */
void face_detector_set_result_callback(face_detect_result_cb_t cb);

/**
 * @brief Enable face detection and lazily create the inference worker.
 */
esp_err_t face_detector_start(void);

/**
 * @brief Disable new inference submissions.
 *
 * The worker remains allocated so the model can be reused the next time the
 * face page is entered. Any in-flight inference is allowed to finish, but its
 * result is discarded after detection is disabled.
 */
void face_detector_stop(void);

/**
 * @brief Submit a RGB565 little-endian frame for asynchronous face detection.
 *
 * Only one inference frame is owned by the detector at a time. If the worker
 * is still busy, ESP_ERR_TIMEOUT is returned and the caller should simply drop
 * that frame rather than block the camera capture path.
 */
esp_err_t face_detector_submit_rgb565(const void *frame,
                                      size_t frame_bytes,
                                      uint16_t width,
                                      uint16_t height);

/**
 * @brief Return true while detection submissions are enabled.
 */
bool face_detector_is_running(void);

#ifdef __cplusplus
}
#endif
