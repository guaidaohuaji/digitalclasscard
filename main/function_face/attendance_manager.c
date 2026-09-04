#include "attendance_manager.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define TAG "attendance"

#define ATTENDANCE_TASK_STACK   (5 * 1024)
#define ATTENDANCE_TASK_PRIO    4
#define ATTENDANCE_QUEUE_LEN    8
#define ATTENDANCE_CACHE_SIZE   32
#define DUP_NOTICE_INTERVAL_SEC 2

/* Keep these names FAT 8.3 compatible. This makes attendance storage work
 * even when CONFIG_FATFS_LFN_* is not enabled in an already-generated
 * sdkconfig. Long-file-name support is also enabled in sdkconfig.defaults
 * for future files. */
#define USERS_FILE_PATH      BSP_SD_MOUNT_POINT "/users.csv"
#define ATTENDANCE_FILE_PATH BSP_SD_MOUNT_POINT "/attend.csv"

typedef enum {
    ATTENDANCE_MSG_RECOGNIZED = 0,
    ATTENDANCE_MSG_ENROLLED,
    ATTENDANCE_MSG_DB_CLEARED,
} attendance_msg_type_t;

typedef struct {
    attendance_msg_type_t type;
    uint16_t id;
    float similarity;
} attendance_msg_t;

typedef struct {
    bool used;
    uint16_t id;
    time_t last_record_time;
    time_t last_duplicate_notice_time;
} attendance_cache_entry_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static attendance_event_cb_t s_callback = NULL;
static attendance_cache_entry_t s_cache[ATTENDANCE_CACHE_SIZE];

static bool sd_ready(void)
{
    struct stat st = {0};
    return stat(BSP_SD_MOUNT_POINT, &st) == 0;
}

static void trim_line(char *text)
{
    if (!text) return;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
}

static void sanitize_csv_text(char *text)
{
    if (!text) return;
    for (char *p = text; *p; ++p) {
        if (*p == ',' || *p == '\r' || *p == '\n') {
            *p = ' ';
        }
    }
}

static void log_file_error(const char *action, const char *path)
{
    int err = errno;
    ESP_LOGE(TAG, "%s %s failed: errno=%d (%s)",
             action, path, err, strerror(err));
}

static esp_err_t ensure_file_header(const char *path, const char *header)
{
    struct stat st = {0};
    if (stat(path, &st) == 0 && st.st_size > 0) {
        return ESP_OK;
    }

    errno = 0;
    FILE *f = fopen(path, "wb");
    if (!f) {
        log_file_error("Create", path);
        return ESP_FAIL;
    }

    if (fputs(header, f) < 0) {
        log_file_error("Write header to", path);
        fclose(f);
        return ESP_FAIL;
    }

    if (fclose(f) != 0) {
        log_file_error("Close", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void default_name(uint16_t id, char *out, size_t out_size)
{
    snprintf(out, out_size, "用户%u", (unsigned int)id);
}

static bool lookup_name(uint16_t id, char *out, size_t out_size)
{
    default_name(id, out, out_size);

    if (ensure_file_header(USERS_FILE_PATH, "id,name\n") != ESP_OK) {
        return false;
    }

    errno = 0;
    FILE *f = fopen(USERS_FILE_PATH, "rb");
    if (!f) {
        log_file_error("Open", USERS_FILE_PATH);
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        trim_line(line);
        char *comma = strchr(line, ',');
        if (!comma) continue;

        *comma = '\0';
        char *end = NULL;
        unsigned long parsed_id = strtoul(line, &end, 10);
        if (end == line || parsed_id != id) continue;

        const char *name = comma + 1;
        if (*name != '\0') {
            snprintf(out, out_size, "%s", name);
        }
        fclose(f);
        return true;
    }

    fclose(f);
    return false;
}

static esp_err_t ensure_user_profile(uint16_t id)
{
    char name[ATTENDANCE_NAME_MAX];
    if (lookup_name(id, name, sizeof(name))) {
        return ESP_OK;
    }

    default_name(id, name, sizeof(name));
    sanitize_csv_text(name);

    errno = 0;
    FILE *f = fopen(USERS_FILE_PATH, "ab");
    if (!f) {
        log_file_error("Open for append", USERS_FILE_PATH);
        return ESP_FAIL;
    }

    int written = fprintf(f, "%u,%s\n", (unsigned int)id, name);
    if (written <= 0) {
        log_file_error("Write", USERS_FILE_PATH);
        fclose(f);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        log_file_error("Close", USERS_FILE_PATH);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created default user mapping: ID=%u name=%s",
             (unsigned int)id, name);
    return ESP_OK;
}

static esp_err_t reset_user_profiles(void)
{
    errno = 0;
    FILE *f = fopen(USERS_FILE_PATH, "wb");
    if (!f) {
        log_file_error("Reset", USERS_FILE_PATH);
        return ESP_FAIL;
    }

    int ok = fputs("id,name\n", f);
    if (ok < 0) {
        log_file_error("Write", USERS_FILE_PATH);
        fclose(f);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        log_file_error("Close", USERS_FILE_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool get_local_time(time_t *now_out, struct tm *tm_out,
                           char *time_text, size_t time_text_size)
{
    time_t now = time(NULL);
    struct tm tm_info = {0};
    localtime_r(&now, &tm_info);

    /* Reject the unsynchronised boot-time clock instead of recording 1970. */
    if (tm_info.tm_year + 1900 < 2024) {
        return false;
    }

    if (strftime(time_text, time_text_size, "%Y-%m-%d %H:%M:%S", &tm_info) == 0) {
        return false;
    }

    if (now_out) *now_out = now;
    if (tm_out) *tm_out = tm_info;
    return true;
}

static attendance_cache_entry_t *get_cache_entry(uint16_t id)
{
    attendance_cache_entry_t *free_entry = NULL;
    attendance_cache_entry_t *oldest = &s_cache[0];

    for (size_t i = 0; i < ATTENDANCE_CACHE_SIZE; i++) {
        if (s_cache[i].used && s_cache[i].id == id) {
            return &s_cache[i];
        }
        if (!s_cache[i].used && free_entry == NULL) {
            free_entry = &s_cache[i];
        }
        if (s_cache[i].last_record_time < oldest->last_record_time) {
            oldest = &s_cache[i];
        }
    }

    attendance_cache_entry_t *entry = free_entry ? free_entry : oldest;
    memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->id = id;
    return entry;
}

static void publish_event(attendance_event_type_t type,
                          uint16_t id,
                          float similarity,
                          const char *name,
                          const char *timestamp)
{
    if (!s_callback) return;

    attendance_event_t event = {0};
    event.type = type;
    event.id = id;
    event.similarity = similarity;
    if (name) snprintf(event.name, sizeof(event.name), "%s", name);
    if (timestamp) snprintf(event.timestamp, sizeof(event.timestamp), "%s", timestamp);
    s_callback(&event);
}

static void handle_recognition(uint16_t id, float similarity)
{
    char name[ATTENDANCE_NAME_MAX] = {0};
    char timestamp[ATTENDANCE_TIME_STR_MAX] = {0};
    time_t now = 0;

    if (!sd_ready()) {
        ESP_LOGE(TAG, "SD mount point is unavailable: %s", BSP_SD_MOUNT_POINT);
        publish_event(ATTENDANCE_EVENT_STORAGE_ERROR, id, similarity, NULL, NULL);
        return;
    }

    if (!get_local_time(&now, NULL, timestamp, sizeof(timestamp))) {
        publish_event(ATTENDANCE_EVENT_TIME_INVALID, id, similarity, NULL, NULL);
        return;
    }

    lookup_name(id, name, sizeof(name));

    attendance_cache_entry_t *entry = get_cache_entry(id);
    if (entry->last_record_time != 0) {
        double delta = difftime(now, entry->last_record_time);
        if (delta >= 0 && delta < ATTENDANCE_DEDUP_WINDOW_SEC) {
            double notice_delta = difftime(now, entry->last_duplicate_notice_time);
            if (entry->last_duplicate_notice_time == 0 || notice_delta >= DUP_NOTICE_INTERVAL_SEC) {
                entry->last_duplicate_notice_time = now;
                publish_event(ATTENDANCE_EVENT_DUPLICATE,
                              id, similarity, name, timestamp);
            }
            return;
        }
    }

    if (ensure_file_header(ATTENDANCE_FILE_PATH,
                           "timestamp,id,name,similarity\n") != ESP_OK) {
        publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                      id, similarity, name, timestamp);
        return;
    }

    char csv_name[ATTENDANCE_NAME_MAX];
    snprintf(csv_name, sizeof(csv_name), "%s", name);
    sanitize_csv_text(csv_name);

    errno = 0;
    FILE *f = fopen(ATTENDANCE_FILE_PATH, "ab");
    if (!f) {
        log_file_error("Open for append", ATTENDANCE_FILE_PATH);
        publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                      id, similarity, name, timestamp);
        return;
    }

    int written = fprintf(f, "%s,%u,%s,%.4f\n",
                          timestamp,
                          (unsigned int)id,
                          csv_name,
                          similarity);
    if (written <= 0) {
        log_file_error("Write", ATTENDANCE_FILE_PATH);
        fclose(f);
        publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                      id, similarity, name, timestamp);
        return;
    }
    if (fclose(f) != 0) {
        log_file_error("Close", ATTENDANCE_FILE_PATH);
        publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                      id, similarity, name, timestamp);
        return;
    }

    entry->last_record_time = now;
    entry->last_duplicate_notice_time = 0;

    ESP_LOGI(TAG, "Recorded attendance: %s ID=%u name=%s sim=%.3f",
             timestamp, (unsigned int)id, name, similarity);
    publish_event(ATTENDANCE_EVENT_RECORDED,
                  id, similarity, name, timestamp);
}

static void attendance_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Attendance worker ready, dedup=%d s, users=%s, records=%s",
             ATTENDANCE_DEDUP_WINDOW_SEC,
             USERS_FILE_PATH,
             ATTENDANCE_FILE_PATH);

    attendance_msg_t msg = {0};
    while (true) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.type) {
        case ATTENDANCE_MSG_RECOGNIZED:
            handle_recognition(msg.id, msg.similarity);
            break;

        case ATTENDANCE_MSG_ENROLLED:
            if (sd_ready() && ensure_user_profile(msg.id) != ESP_OK) {
                publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                              msg.id, 0.0f, NULL, NULL);
            }
            break;

        case ATTENDANCE_MSG_DB_CLEARED:
            memset(s_cache, 0, sizeof(s_cache));
            if (!sd_ready() || reset_user_profiles() != ESP_OK) {
                publish_event(ATTENDANCE_EVENT_STORAGE_ERROR,
                              0, 0.0f, NULL, NULL);
            } else {
                ESP_LOGI(TAG, "User mappings reset; attendance history retained");
            }
            break;

        default:
            break;
        }
    }
}

esp_err_t attendance_manager_start(void)
{
    if (s_task != NULL) return ESP_OK;

    if (s_queue == NULL) {
        s_queue = xQueueCreate(ATTENDANCE_QUEUE_LEN, sizeof(attendance_msg_t));
        if (s_queue == NULL) return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(attendance_task,
                                "attendance",
                                ATTENDANCE_TASK_STACK,
                                NULL,
                                ATTENDANCE_TASK_PRIO,
                                &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void attendance_manager_set_callback(attendance_event_cb_t cb)
{
    s_callback = cb;
}

static esp_err_t submit_message(attendance_msg_type_t type,
                                uint16_t id,
                                float similarity)
{
    if (s_task == NULL || s_queue == NULL) return ESP_ERR_INVALID_STATE;

    attendance_msg_t msg = {
        .type = type,
        .id = id,
        .similarity = similarity,
    };

    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t attendance_manager_submit_recognition(uint16_t id, float similarity)
{
    if (id == 0) return ESP_ERR_INVALID_ARG;
    return submit_message(ATTENDANCE_MSG_RECOGNIZED, id, similarity);
}

esp_err_t attendance_manager_notify_enrolled(uint16_t id)
{
    if (id == 0) return ESP_ERR_INVALID_ARG;
    return submit_message(ATTENDANCE_MSG_ENROLLED, id, 0.0f);
}

esp_err_t attendance_manager_notify_database_cleared(void)
{
    return submit_message(ATTENDANCE_MSG_DB_CLEARED, 0, 0.0f);
}
