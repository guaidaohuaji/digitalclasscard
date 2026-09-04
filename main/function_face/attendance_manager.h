#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATTENDANCE_NAME_MAX 48
#define ATTENDANCE_TIME_STR_MAX 20

/**
 * Consecutive recognitions of the same person inside this window are treated
 * as one attendance action. Change this value if the product needs a longer
 * or shorter anti-duplicate window.
 */
#define ATTENDANCE_DEDUP_WINDOW_SEC 60

typedef enum {
    ATTENDANCE_EVENT_RECORDED = 0,
    ATTENDANCE_EVENT_DUPLICATE,
    ATTENDANCE_EVENT_TIME_INVALID,
    ATTENDANCE_EVENT_STORAGE_ERROR,
} attendance_event_type_t;

typedef struct {
    attendance_event_type_t type;
    uint16_t id;
    float similarity;
    char name[ATTENDANCE_NAME_MAX];
    char timestamp[ATTENDANCE_TIME_STR_MAX];
} attendance_event_t;

typedef void (*attendance_event_cb_t)(const attendance_event_t *event);

/** Start the asynchronous attendance worker. Safe to call repeatedly. */
esp_err_t attendance_manager_start(void);

/** Set the callback used to publish attendance results to the UI. */
void attendance_manager_set_callback(attendance_event_cb_t cb);

/**
 * Submit a recognized face. This call never performs SD-card I/O and never
 * blocks the face inference task. ESP_ERR_TIMEOUT means the attendance queue
 * is currently full and this recognition sample was dropped.
 */
esp_err_t attendance_manager_submit_recognition(uint16_t id, float similarity);

/**
 * Inform the manager that a new face ID was enrolled. A default name mapping
 * such as "用户1" is created in /sdcard/face_users.csv when no mapping exists.
 */
esp_err_t attendance_manager_notify_enrolled(uint16_t id);

/**
 * Inform the manager that the ESP-DL face database was cleared. User mappings
 * and in-memory de-duplication state are reset, while historical attendance
 * records are intentionally retained.
 */
esp_err_t attendance_manager_notify_database_cleared(void);

#ifdef __cplusplus
}
#endif
