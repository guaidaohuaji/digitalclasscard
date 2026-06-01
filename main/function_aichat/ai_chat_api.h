#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AI 聊天流程：语音 → 文字 → AI回答
 * 
 * @param wav_path        WAV 文件路径 (16kHz/16bit/mono)
 * @param asr_text_out    输出的语音识别文字 (调用者分配，建议 ≥512 字节)
 * @param asr_out_size    输出文字缓冲区大小
 * @param ai_reply_out    输出的 AI 回复文字 (调用者分配，建议 ≥1024 字节)
 * @param reply_out_size  回复文字缓冲区大小
 * @return esp_err_t      ESP_OK 成功，其他值表示失败
 */
esp_err_t ai_chat_process(
    const char *wav_path,
    char *asr_text_out,
    size_t asr_out_size,
    char *ai_reply_out,
    size_t reply_out_size
);

#ifdef __cplusplus
}
#endif