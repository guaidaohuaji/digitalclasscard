#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bind the LVGL objects used by the camera preview.
 *
 * The canvas and status label are owned by the UI layer. The camera module only
 * updates them while the face screen is active.
 */
void camera_preview_bind_ui(lv_obj_t *canvas, lv_obj_t *status_label);

/**
 * @brief Start MIPI-CSI camera preview.
 *
 * Camera hardware is initialized lazily on the first call. Subsequent calls
 * reuse the initialized camera subsystem.
 */
esp_err_t camera_preview_start(void);

/**
 * @brief Request preview stop.
 *
 * This call is non-blocking so it is safe to use from an LVGL event callback.
 */
void camera_preview_stop(void);

/**
 * @brief Return true while the capture task is running.
 */
bool camera_preview_is_running(void);

#ifdef __cplusplus
}
#endif
