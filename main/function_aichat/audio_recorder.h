#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t riff_id;
    uint32_t file_size;
    uint32_t wave_id;
    uint32_t fmt_id;
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_size;
} __attribute__((packed)) wav_header_t;

esp_err_t audio_recorder_init(void);

/**
 * @brief 开始录音并保存到文件
 * @param file_path 保存路径，例如 "/sdcard/ai_record.wav"
 * @return ESP_OK 表示录音任务已成功启动；其他值表示启动失败
 */
esp_err_t audio_recorder_start(const char *file_path);

void audio_recorder_stop(void);

#ifdef __cplusplus
}
#endif
