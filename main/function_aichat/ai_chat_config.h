#pragma once

#include "ai_chat_config_private.h"  // 私有 API Key 和 OSS Key（不提交到 Git）

/**
 * @brief 阿里云 OSS 配置
 *
 * 用于临时存放录音文件，供 paraformer-v2 下载识别
 * Bucket 地域必须与 paraformer 服务地域一致（cn-beijing）
 */
#define OSS_BUCKET          "esp32-asr-audio"
#define OSS_ENDPOINT        "oss-cn-beijing.aliyuncs.com"
#define OSS_REGION          "cn-beijing"
#define OSS_OBJECT_KEY      "asr/ai_record.wav"   // 固定覆盖写，无需清理

/**
 * @brief 阿里云 paraformer-v2 异步 ASR 接口
 */
#define DASHSCOPE_ASR_SUBMIT_URL  "https://dashscope.aliyuncs.com/api/v1/services/audio/asr/transcription"
#define DASHSCOPE_ASR_QUERY_URL   "https://dashscope.aliyuncs.com/api/v1/tasks/"  // 拼接 task_id

/**
 * @brief 通义千问对话
 */
#define DASHSCOPE_LLM_URL         "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
#define DASHSCOPE_LLM_MODEL       "qwen-turbo"

/**
 * @brief 系统提示词
 */
#define AI_SYSTEM_PROMPT          "你是一个电子班牌的AI助手，名字叫小班。你回答问题时简洁友善，适合中小学生使用。回答控制在100字以内。"

/**
 * @brief HTTP 响应缓冲区大小
 */
#define AI_HTTP_BUF_SIZE          32768

/**
 * @brief ASR 轮询参数
 */
#define ASR_POLL_INTERVAL_MS      1000   // 每次轮询间隔
#define ASR_POLL_MAX_TIMES        30     // 最多轮询30次（30秒超时）
