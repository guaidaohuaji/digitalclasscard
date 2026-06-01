#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// WAV 文件头部结构体
typedef struct {
    uint32_t riff_id;          // "RIFF"
    uint32_t file_size;        // 文件大小 - 8
    uint32_t wave_id;          // "WAVE"
    uint32_t fmt_id;           // "fmt "
    uint32_t fmt_size;         // 格式块大小 (16 for PCM)
    uint16_t audio_format;     // 音频格式 (1 = PCM)
    uint16_t num_channels;     // 声道数
    uint32_t sample_rate;      // 采样率
    uint32_t byte_rate;        // 字节率
    uint16_t block_align;      // 数据块对齐
    uint16_t bits_per_sample;  // 位深
    uint32_t data_id;          // "data"
    uint32_t data_size;        // 数据大小
} __attribute__((packed)) wav_header_t;

/**
 * @brief 初始化录音模块 (I2C, ES8311, I2S)
 */
esp_err_t audio_recorder_init(void);

/**
 * @brief 开始录音并保存到文件
 * @param file_path 保存路径，例如 "/sdcard/ai_record.wav"
 */
void audio_recorder_start(const char *file_path);

/**
 * @brief 停止录音并正确关闭文件
 */
void audio_recorder_stop(void);

#ifdef __cplusplus
}
#endif