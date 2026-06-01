#pragma once

#include "ai_chat_config_private.h"  // 私有 API Key（不提交到 Git）

/**
 * @brief 阿里云一句话识别 (Paraformer)
 * 
 * 语音转文字接口，接收 16kHz/16bit/mono PCM 音频
 */
#define DASHSCOPE_ASR_URL           "https://dashscope.aliyuncs.com/api/v1/services/audio/asr/asr"

/**
 * @brief 通义千问对话 (Qwen-Turbo)
 * 
 * 兼容 OpenAI 接口格式
 * 可用模型：qwen-turbo, qwen-plus, qwen-max
 */
#define DASHSCOPE_LLM_URL           "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
#define DASHSCOPE_LLM_MODEL         "qwen-turbo"

/**
 * @brief 系统提示词 (可自定义 AI 助手人设)
 */
#define AI_SYSTEM_PROMPT            "你是一个电子班牌的AI助手，名字叫小班。你回答问题时简洁友善，适合中小学生使用。回答控制在100字以内。"

/**
 * @brief HTTP 缓冲区大小
 */
#define AI_HTTP_BUF_SIZE            8192