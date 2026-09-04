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

typedef enum {
    FACE_IDENTITY_UNKNOWN = 0,
    FACE_IDENTITY_RECOGNIZED,
    FACE_IDENTITY_ENROLLED,
    FACE_IDENTITY_ENROLL_WAIT_FACE,
    FACE_IDENTITY_ENROLL_WAIT_SINGLE_FACE,
    FACE_IDENTITY_DB_CLEARED,
    FACE_IDENTITY_DB_ERROR,
} face_identity_event_type_t;

typedef struct {
    face_identity_event_type_t type;
    uint16_t id;
    float similarity;
    uint16_t database_count;
    uint32_t latency_ms;
} face_identity_event_t;

typedef void (*face_identity_event_cb_t)(const face_identity_event_t *event);

/**
 * @brief Set callback used to publish the latest face detection boxes.
 *
 * The callback runs from the face inference task. UI callbacks therefore need
 * to acquire the LVGL adapter lock before touching LVGL objects.
 */
void face_detector_set_result_callback(face_detect_result_cb_t cb);

/**
 * @brief Set callback used to publish enrollment / recognition events.
 *
 * The callback runs from the same face inference task as detection.
 */
void face_detector_set_identity_callback(face_identity_event_cb_t cb);

/**
 * @brief Enable face detection/recognition and lazily create the inference worker.
 */
esp_err_t face_detector_start(void);

/**
 * @brief Disable new inference submissions.
 *
 * The worker and models remain allocated so they can be reused on the next
 * visit to the face page. Any in-flight inference may finish, but its result
 * is discarded after detection is disabled.
 */
void face_detector_stop(void);

/**
 * @brief Submit a RGB565 little-endian frame for asynchronous inference.
 *
 * Only one inference frame is owned by the AI worker at a time. If the worker
 * is busy, ESP_ERR_TIMEOUT is returned and the camera path should drop the frame.
 */
esp_err_t face_detector_submit_rgb565(const void *frame,
                                      size_t frame_bytes,
                                      uint16_t width,
                                      uint16_t height);

/**
 * @brief Request enrollment of exactly one visible face.
 *
 * The request stays pending while no face is visible or while multiple faces
 * are present. The next frame containing exactly one face is enrolled once.
 */
esp_err_t face_detector_request_enroll(void);

/**
 * @brief Request deletion of all enrolled face features from the SD database.
 *
 * The actual storage operation is serialized inside the inference task.
 */
esp_err_t face_detector_request_clear_database(void);

/**
 * @brief Return true while inference submissions are enabled.
 */
bool face_detector_is_running(void);

#ifdef __cplusplus
}
#endif
