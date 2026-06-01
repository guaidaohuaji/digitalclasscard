#include "ai_chat_api.h"
#include "ai_chat_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "audio_recorder.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ai_chat_api"

// 全局 HTTP 缓冲区 (PSRAM 分配)
static char *g_http_buf = NULL;
static int   g_http_buf_len = 0;

/* ---------- HTTP 事件回调 ---------- */
static esp_err_t _http_event_cb(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (g_http_buf && (g_http_buf_len + evt->data_len < AI_HTTP_BUF_SIZE)) {
            memcpy(g_http_buf + g_http_buf_len, evt->data, evt->data_len);
            g_http_buf_len += evt->data_len;
            g_http_buf[g_http_buf_len] = 0;
        } else {
            ESP_LOGW(TAG, "HTTP 响应缓冲区溢出");
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ---------- 读取整个文件到内存 ---------- */
static uint8_t *read_whole_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "无法打开文件: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        ESP_LOGE(TAG, "文件为空或读取失败: %s", path);
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    if (!buf) {
        ESP_LOGE(TAG, "文件缓冲区分配失败 (%ld 字节)", file_size);
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);

    if (read != (size_t)file_size) {
        ESP_LOGE(TAG, "文件读取不完整: %u/%ld", read, file_size);
        free(buf);
        return NULL;
    }

    *out_size = (size_t)file_size;
    return buf;
}

/* ---------- 调用阿里云一句话识别 (Binary WAV 直传模式) ---------- */
static esp_err_t call_asr(const uint8_t *wav_data, size_t wav_size,
                          char *text_out, size_t text_size)
{
    ESP_LOGI(TAG, "ASR: 发送 %u 字节 WAV 数据 (binary 直传)...", wav_size);

    // 初始化 HTTP 缓冲区（只用于接收响应，不用于编码）
    if (!g_http_buf) {
        g_http_buf = (char *)malloc(AI_HTTP_BUF_SIZE);
        if (!g_http_buf) {
            ESP_LOGE(TAG, "HTTP 缓冲区分配失败");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(g_http_buf, 0, AI_HTTP_BUF_SIZE);
    g_http_buf_len = 0;

    // ---- Binary WAV 直传模式 ----
    // 通过 URL 参数传递 model/sample_rate/format，Body 为原始 WAV 二进制数据
    // 避免 Base64 编码带来的 1.33x 膨胀和 100KB+ 额外内存分配
    char url_with_params[512];
    snprintf(url_with_params, sizeof(url_with_params),
             "%s?model=paraformer-v2&sample_rate=16000&format=wav",
             DASHSCOPE_ASR_URL);
    ESP_LOGI(TAG, "ASR URL: %s", url_with_params);

    // ---- HTTP 请求 ----
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DASHSCOPE_API_KEY);

    esp_http_client_config_t cfg = {
        .url = url_with_params,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .event_handler = _http_event_cb,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "ASR: HTTP 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    // 直接发送原始 WAV 二进制数据，无需 Base64/JSON
    esp_http_client_set_post_field(client, (const char *)wav_data, (int)wav_size);

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ASR: HTTP 请求失败, err=%d (可能是 TLS 握手/Socket 异常)", err);
        // 异常路径: 先延时再 cleanup，确保底层资源完全释放
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_http_client_cleanup(client);
    // 注意：wav_data 由调用者管理，这里不需要 free

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "ASR: HTTP 失败, err=%d, status=%d", err, status);
        if (g_http_buf_len > 0) {
            ESP_LOGE(TAG, "ASR 响应: %s", g_http_buf);
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ASR 响应: %s", g_http_buf);

    // 解析 JSON: {"output": {"sentence": {"text": "..."}}}
    cJSON *resp = cJSON_Parse(g_http_buf);
    if (!resp) {
        ESP_LOGE(TAG, "ASR: JSON 解析失败");
        return ESP_FAIL;
    }

    cJSON *output = cJSON_GetObjectItem(resp, "output");
    cJSON *sentence = output ? cJSON_GetObjectItem(output, "sentence") : NULL;
    cJSON *text_item = sentence ? cJSON_GetObjectItem(sentence, "text") : NULL;

    if (!text_item || !text_item->valuestring) {
        ESP_LOGE(TAG, "ASR: 响应中缺少识别文本");
        cJSON_Delete(resp);
        return ESP_FAIL;
    }

    strncpy(text_out, text_item->valuestring, text_size - 1);
    text_out[text_size - 1] = '\0';
    cJSON_Delete(resp);

    ESP_LOGI(TAG, "ASR 识别结果: %s", text_out);
    return ESP_OK;
}

/* ---------- 调用通义千问对话 ---------- */
static esp_err_t call_llm(const char *user_text,
                          char *reply_out, size_t reply_size)
{
    ESP_LOGI(TAG, "LLM: 发送问题 \"%s\"...", user_text);

    // 重置 HTTP 缓冲区
    memset(g_http_buf, 0, AI_HTTP_BUF_SIZE);
    g_http_buf_len = 0;

    // 构建 JSON 请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DASHSCOPE_LLM_MODEL);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    // system message
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", AI_SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    // user message
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_text);
    cJSON_AddItemToArray(messages, user_msg);

    // 控制回复长度
    cJSON_AddNumberToObject(root, "max_tokens", 200);

    char *post_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!post_body) {
        ESP_LOGE(TAG, "LLM: JSON 序列化失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LLM 请求体: %s", post_body);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DASHSCOPE_API_KEY);

    esp_http_client_config_t cfg = {
        .url = DASHSCOPE_LLM_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .event_handler = _http_event_cb,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "LLM: HTTP 客户端初始化失败");
        free(post_body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_body, (int)strlen(post_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LLM: HTTP 请求失败, err=%d (可能是 TLS 握手/Socket 异常)", err);
        // 异常路径: 先延时再 cleanup，确保底层资源完全释放
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_http_client_cleanup(client);
    free(post_body);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "LLM: HTTP 失败, err=%d, status=%d", err, status);
        if (g_http_buf_len > 0) {
            ESP_LOGE(TAG, "LLM 响应: %s", g_http_buf);
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LLM 响应: %s", g_http_buf);

    // 解析 JSON: {"choices": [{"message": {"content": "..."}}]}
    cJSON *resp = cJSON_Parse(g_http_buf);
    if (!resp) {
        ESP_LOGE(TAG, "LLM: JSON 解析失败");
        return ESP_FAIL;
    }

    cJSON *choices = cJSON_GetObjectItem(resp, "choices");
    cJSON *choice0 = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg = choice0 ? cJSON_GetObjectItem(choice0, "message") : NULL;
    cJSON *content = msg ? cJSON_GetObjectItem(msg, "content") : NULL;

    if (!content || !content->valuestring) {
        ESP_LOGE(TAG, "LLM: 响应中缺少回复文本");
        cJSON_Delete(resp);
        return ESP_FAIL;
    }

    strncpy(reply_out, content->valuestring, reply_size - 1);
    reply_out[reply_size - 1] = '\0';
    cJSON_Delete(resp);

    ESP_LOGI(TAG, "AI 回复: %s", reply_out);
    return ESP_OK;
}

/* ========== 公开接口 ========== */
esp_err_t ai_chat_process(
    const char *wav_path,
    char *asr_text_out,
    size_t asr_out_size,
    char *ai_reply_out,
    size_t reply_out_size)
{
    if (!wav_path || !asr_text_out || !ai_reply_out) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. 读取完整 WAV 文件 (含 44 字节头部，直接发给 ASR)
    size_t wav_size = 0;
    uint8_t *wav_data = read_whole_file(wav_path, &wav_size);
    if (!wav_data) {
        snprintf(ai_reply_out, reply_out_size, "读取录音文件失败");
        return ESP_FAIL;
    }

    if (wav_size == 0) {
        ESP_LOGE(TAG, "WAV 数据为空");
        free(wav_data);
        snprintf(ai_reply_out, reply_out_size, "录音内容为空，请重新录音");
        return ESP_FAIL;
    }

    // 2. 语音识别
    esp_err_t ret = call_asr(wav_data, wav_size, asr_text_out, asr_out_size);
    free(wav_data);

    if (ret != ESP_OK) {
        snprintf(ai_reply_out, reply_out_size, "语音识别失败，请检查网络后重试");
        return ret;
    }

    if (strlen(asr_text_out) == 0) {
        snprintf(ai_reply_out, reply_out_size, "未识别到有效语音内容，请重新录音");
        return ESP_FAIL;
    }

    // 3. AI 对话
    ret = call_llm(asr_text_out, ai_reply_out, reply_out_size);
    if (ret != ESP_OK) {
        snprintf(ai_reply_out, reply_out_size, "AI 服务请求失败，请稍后重试");
    }

    return ret;
}