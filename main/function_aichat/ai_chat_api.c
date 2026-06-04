#include "ai_chat_api.h"
#include "ai_chat_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define TAG "ai_chat_api"

/* ------------------------------------------------------------------ */
/*  全局 HTTP 响应缓冲区                                                */
/* ------------------------------------------------------------------ */
static char *g_http_buf     = NULL;
static int   g_http_buf_len = 0;

static esp_err_t _http_event_cb(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (g_http_buf && (g_http_buf_len + evt->data_len < AI_HTTP_BUF_SIZE)) {
            memcpy(g_http_buf + g_http_buf_len, evt->data, evt->data_len);
            g_http_buf_len += evt->data_len;
            g_http_buf[g_http_buf_len] = '\0';
        } else {
            ESP_LOGW(TAG, "HTTP 响应缓冲区溢出");
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void reset_http_buf(void)
{
    if (!g_http_buf) {
        g_http_buf = (char *)malloc(AI_HTTP_BUF_SIZE);
        if (!g_http_buf) {
            ESP_LOGE(TAG, "HTTP 缓冲区分配失败");
            return;
        }
    }
    memset(g_http_buf, 0, AI_HTTP_BUF_SIZE);
    g_http_buf_len = 0;
}

/* ------------------------------------------------------------------ */
/*  读取整个文件                                                         */
/* ------------------------------------------------------------------ */
static uint8_t *read_whole_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "无法打开: %s", path); return NULL; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* ------------------------------------------------------------------ */
/*  HMAC-SHA1 → Base64（OSS 签名需要）                                  */
/* ------------------------------------------------------------------ */
static esp_err_t hmac_sha1_base64(const char *key, size_t key_len,
                                   const char *data, size_t data_len,
                                   char *out_b64, size_t out_b64_size)
{
    unsigned char hmac[20];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (mbedtls_md_setup(&ctx, info, 1) != 0 ||
        mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key, key_len) != 0 ||
        mbedtls_md_hmac_update(&ctx, (const unsigned char *)data, data_len) != 0 ||
        mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    mbedtls_md_free(&ctx);

    size_t out_len = 0;
    if (mbedtls_base64_encode((unsigned char *)out_b64, out_b64_size,
                               &out_len, hmac, sizeof(hmac)) != 0) {
        return ESP_FAIL;
    }
    out_b64[out_len] = '\0';
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  生成 OSS 预签名 URL（PUT 上传 + GET 下载）                          */
/* ------------------------------------------------------------------ */
/*
 * OSS V1 预签名 URL 格式：
 *   StringToSign(VERB) = "VERB\n\n{Content-Type}\n{expires}\n/{bucket}/{object}"
 *   Signature           = Base64(HMAC-SHA1(secret, StringToSign))
 *   URL                 = https://{bucket}.{endpoint}/{object}
 *                          ?OSSAccessKeyId=...&Expires=...&Signature=...
 *
 *   PUT: Content-Type = audio/wav，供 ESP32 上传
 *   GET: Content-Type 为空，供 paraformer 下载（超时更长，确保异步任务完成前有效）
 */
static esp_err_t build_oss_put_url(char *url_out, size_t url_size,
                                    char *pub_url_out, size_t pub_url_size)
{
    time_t now = time(NULL);
    if (now < 1700000000) {
        ESP_LOGE(TAG, "系统时间未同步 (now=%ld)，OSS 签名将失败", (long)now);
        return ESP_FAIL;
    }

    long put_expires = (long)(now + 300);   // PUT: 5 分钟
    long get_expires = (long)(now + 1800);  // GET: 30 分钟（确保异步任务完成前有效）

    // --- PUT 预签名 ---
    char put_sts[256];
    snprintf(put_sts, sizeof(put_sts),
             "PUT\n\naudio/wav\n%ld\n/%s/%s",
             put_expires, OSS_BUCKET, OSS_OBJECT_KEY);

    char sig_b64[64];
    if (hmac_sha1_base64(OSS_ACCESS_KEY_SECRET, strlen(OSS_ACCESS_KEY_SECRET),
                          put_sts, strlen(put_sts),
                          sig_b64, sizeof(sig_b64)) != ESP_OK) {
        ESP_LOGE(TAG, "OSS PUT 签名计算失败");
        return ESP_FAIL;
    }

    // URL 编码 Signature
    char put_sig_enc[128] = {0};
    char *p = put_sig_enc;
    for (int i = 0; sig_b64[i] && (p - put_sig_enc < (int)sizeof(put_sig_enc) - 4); i++) {
        if (sig_b64[i] == '+')      { *p++ = '%'; *p++ = '2'; *p++ = 'B'; }
        else if (sig_b64[i] == '/') { *p++ = '%'; *p++ = '2'; *p++ = 'F'; }
        else if (sig_b64[i] == '=') { *p++ = '%'; *p++ = '3'; *p++ = 'D'; }
        else                        { *p++ = sig_b64[i]; }
    }
    *p = '\0';

    snprintf(url_out, url_size,
             "https://%s.%s/%s?OSSAccessKeyId=%s&Expires=%ld&Signature=%s",
             OSS_BUCKET, OSS_ENDPOINT, OSS_OBJECT_KEY,
             OSS_ACCESS_KEY_ID, put_expires, put_sig_enc);

    // --- GET 预签名（供 paraformer 下载）---
    char get_sts[256];
    snprintf(get_sts, sizeof(get_sts),
             "GET\n\n\n%ld\n/%s/%s",
             get_expires, OSS_BUCKET, OSS_OBJECT_KEY);

    if (hmac_sha1_base64(OSS_ACCESS_KEY_SECRET, strlen(OSS_ACCESS_KEY_SECRET),
                          get_sts, strlen(get_sts),
                          sig_b64, sizeof(sig_b64)) != ESP_OK) {
        ESP_LOGE(TAG, "OSS GET 签名计算失败");
        return ESP_FAIL;
    }

    char get_sig_enc[128] = {0};
    p = get_sig_enc;
    for (int i = 0; sig_b64[i] && (p - get_sig_enc < (int)sizeof(get_sig_enc) - 4); i++) {
        if (sig_b64[i] == '+')      { *p++ = '%'; *p++ = '2'; *p++ = 'B'; }
        else if (sig_b64[i] == '/') { *p++ = '%'; *p++ = '2'; *p++ = 'F'; }
        else if (sig_b64[i] == '=') { *p++ = '%'; *p++ = '3'; *p++ = 'D'; }
        else                        { *p++ = sig_b64[i]; }
    }
    *p = '\0';

    snprintf(pub_url_out, pub_url_size,
             "https://%s.%s/%s?OSSAccessKeyId=%s&Expires=%ld&Signature=%s",
             OSS_BUCKET, OSS_ENDPOINT, OSS_OBJECT_KEY,
             OSS_ACCESS_KEY_ID, get_expires, get_sig_enc);

    ESP_LOGI(TAG, "OSS PUT URL 生成完成, PUT expires=%ld, GET expires=%ld",
             put_expires, get_expires);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Step 1: 上传 WAV 到 OSS                                             */
/* ------------------------------------------------------------------ */
static esp_err_t upload_wav_to_oss(const uint8_t *wav_data, size_t wav_size,
                                    char *pub_url_out, size_t pub_url_size)
{
    char put_url[512];
    if (build_oss_put_url(put_url, sizeof(put_url),
                           pub_url_out, pub_url_size) != ESP_OK) {
        return ESP_FAIL;
    }

    reset_http_buf();

    esp_http_client_config_t cfg = {
        .url            = put_url,
        .method         = HTTP_METHOD_PUT,
        .timeout_ms     = 60000,
        .event_handler  = _http_event_cb,
        .buffer_size    = 4096,
        .buffer_size_tx = 65536,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "OSS: HTTP 客户端初始化失败"); return ESP_FAIL; }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_post_field(client, (const char *)wav_data, (int)wav_size);

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "OSS 上传: status=%d, err=%d", status, err);

    // OSS PUT 成功返回 200
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "OSS 上传失败, status=%d, err=%d", status, err);
        if (g_http_buf_len > 0) ESP_LOGE(TAG, "OSS 响应: %s", g_http_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OSS 上传成功, 公网URL: %s", pub_url_out);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Step 2: 提交 paraformer-v2 异步任务，返回 task_id                   */
/* ------------------------------------------------------------------ */
static esp_err_t submit_asr_task(const char *file_url,
                                  char *task_id_out, size_t task_id_size)
{
    reset_http_buf();

    // 构建请求体
    cJSON *root  = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "paraformer-v2");
    cJSON *input = cJSON_AddObjectToObject(root, "input");
    cJSON *urls  = cJSON_AddArrayToObject(input, "file_urls");
    cJSON_AddItemToArray(urls, cJSON_CreateString(file_url));
    cJSON *params = cJSON_AddObjectToObject(root, "parameters");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(params, "language_hints"),
                         cJSON_CreateString("zh"));

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) { ESP_LOGE(TAG, "ASR submit: JSON 序列化失败"); return ESP_FAIL; }

    ESP_LOGI(TAG, "ASR 提交请求体: %s", body);

    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", DASHSCOPE_API_KEY);

    esp_http_client_config_t cfg = {
        .url           = DASHSCOPE_ASR_SUBMIT_URL,
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 30000,
        .event_handler = _http_event_cb,
        .buffer_size   = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body); return ESP_FAIL; }

    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-DashScope-Async", "enable");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    free(body);

    ESP_LOGI(TAG, "ASR 提交: status=%d, resp=%s", status, g_http_buf);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "ASR 提交失败, status=%d", status);
        return ESP_FAIL;
    }

    // 解析 task_id
    cJSON *resp = cJSON_Parse(g_http_buf);
    if (!resp) { ESP_LOGE(TAG, "ASR 提交响应 JSON 解析失败"); return ESP_FAIL; }

    cJSON *output  = cJSON_GetObjectItem(resp, "output");
    cJSON *task_id = output ? cJSON_GetObjectItem(output, "task_id") : NULL;

    if (!task_id || !task_id->valuestring) {
        ESP_LOGE(TAG, "ASR 提交响应缺少 task_id");
        cJSON_Delete(resp);
        return ESP_FAIL;
    }

    strncpy(task_id_out, task_id->valuestring, task_id_size - 1);
    task_id_out[task_id_size - 1] = '\0';
    cJSON_Delete(resp);

    ESP_LOGI(TAG, "ASR task_id: %s", task_id_out);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Step 3: 轮询查询任务结果，获取 transcription_url                    */
/* ------------------------------------------------------------------ */
static esp_err_t poll_asr_result(const char *task_id,
                                  char *trans_url_out, size_t trans_url_size)
{
    char query_url[256];
    snprintf(query_url, sizeof(query_url), "%s%s", DASHSCOPE_ASR_QUERY_URL, task_id);

    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", DASHSCOPE_API_KEY);

    for (int i = 0; i < ASR_POLL_MAX_TIMES; i++) {
        vTaskDelay(pdMS_TO_TICKS(ASR_POLL_INTERVAL_MS));
        reset_http_buf();

        esp_http_client_config_t cfg = {
            .url           = query_url,
            .method        = HTTP_METHOD_GET,
            .timeout_ms    = 15000,
            .event_handler = _http_event_cb,
            .buffer_size   = 4096,
            .buffer_size_tx = 4096,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = false,
        };

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) continue;

        esp_http_client_set_header(client, "Authorization", auth);

        esp_err_t err = esp_http_client_perform(client);
        int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
        esp_http_client_cleanup(client);

        if (err != ESP_OK || status != 200) {
            ESP_LOGW(TAG, "ASR 轮询[%d] HTTP失败 status=%d", i, status);
            continue;
        }

        ESP_LOGI(TAG, "ASR 轮询[%d]: %s", i, g_http_buf);

        cJSON *resp = cJSON_Parse(g_http_buf);
        if (!resp) continue;

        cJSON *output      = cJSON_GetObjectItem(resp, "output");
        cJSON *task_status = output ? cJSON_GetObjectItem(output, "task_status") : NULL;

        if (!task_status || !task_status->valuestring) {
            cJSON_Delete(resp);
            continue;
        }

        const char *ts = task_status->valuestring;
        ESP_LOGI(TAG, "ASR 任务状态: %s", ts);

        if (strcmp(ts, "SUCCEEDED") == 0) {
            // 取第一个结果的 transcription_url
            cJSON *results  = output ? cJSON_GetObjectItem(output, "results") : NULL;
            cJSON *result0  = results ? cJSON_GetArrayItem(results, 0) : NULL;
            cJSON *trans_url = result0 ? cJSON_GetObjectItem(result0, "transcription_url") : NULL;

            if (trans_url && trans_url->valuestring) {
                strncpy(trans_url_out, trans_url->valuestring, trans_url_size - 1);
                trans_url_out[trans_url_size - 1] = '\0';
                cJSON_Delete(resp);
                ESP_LOGI(TAG, "ASR 完成, transcription_url 已获取");
                return ESP_OK;
            }
            cJSON_Delete(resp);
            ESP_LOGE(TAG, "ASR SUCCEEDED 但缺少 transcription_url");
            return ESP_FAIL;

        } else if (strcmp(ts, "FAILED") == 0) {
            cJSON_Delete(resp);
            ESP_LOGE(TAG, "ASR 任务失败");
            return ESP_FAIL;
        }
        // PENDING / RUNNING 继续轮询
        cJSON_Delete(resp);
    }

    ESP_LOGE(TAG, "ASR 轮询超时");
    return ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/*  Step 4: 下载 transcription JSON，提取识别文字                       */
/* ------------------------------------------------------------------ */
static esp_err_t fetch_asr_text(const char *trans_url,
                                 char *text_out, size_t text_size)
{
    reset_http_buf();

    esp_http_client_config_t cfg = {
        .url           = trans_url,
        .method        = HTTP_METHOD_GET,
        .timeout_ms    = 15000,
        .event_handler = _http_event_cb,
        .buffer_size   = AI_HTTP_BUF_SIZE,
        .buffer_size_tx = AI_HTTP_BUF_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "fetch_asr_text: 客户端初始化失败"); return ESP_FAIL; }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "transcription JSON 下载失败, status=%d", status);
        return ESP_FAIL;
    }

    // 响应格式:
    // {"transcripts":[{"channel_id":0,"text":"识别文字",...}],...}
    cJSON *resp = cJSON_Parse(g_http_buf);
    if (!resp) { ESP_LOGE(TAG, "transcription JSON 解析失败"); return ESP_FAIL; }

    cJSON *transcripts = cJSON_GetObjectItem(resp, "transcripts");
    cJSON *t0          = transcripts ? cJSON_GetArrayItem(transcripts, 0) : NULL;
    cJSON *text_item   = t0 ? cJSON_GetObjectItem(t0, "text") : NULL;

    if (!text_item || !text_item->valuestring) {
        ESP_LOGE(TAG, "transcription JSON 缺少 text 字段");
        cJSON_Delete(resp);
        return ESP_FAIL;
    }

    strncpy(text_out, text_item->valuestring, text_size - 1);
    text_out[text_size - 1] = '\0';
    cJSON_Delete(resp);

    ESP_LOGI(TAG, "ASR 识别结果: %s", text_out);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  call_asr: 整合四步流程                                              */
/* ------------------------------------------------------------------ */
static esp_err_t call_asr(const uint8_t *wav_data, size_t wav_size,
                           char *text_out, size_t text_size)
{
    // Step 1: 上传到 OSS
    char pub_url[512];
    esp_err_t ret = upload_wav_to_oss(wav_data, wav_size, pub_url, sizeof(pub_url));
    if (ret != ESP_OK) return ret;

    // Step 2: 提交 ASR 任务
    char task_id[64];
    ret = submit_asr_task(pub_url, task_id, sizeof(task_id));
    if (ret != ESP_OK) return ret;

    // Step 3: 轮询等待结果
    char trans_url[2048];
    ret = poll_asr_result(task_id, trans_url, sizeof(trans_url));
    if (ret != ESP_OK) return ret;

    // Step 4: 下载并提取识别文字
    ret = fetch_asr_text(trans_url, text_out, text_size);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  call_llm: 调用通义千问对话（不变）                                   */
/* ------------------------------------------------------------------ */
static esp_err_t call_llm(const char *user_text,
                           char *reply_out, size_t reply_size)
{
    ESP_LOGI(TAG, "LLM: 发送问题 \"%s\"", user_text);
    reset_http_buf();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DASHSCOPE_LLM_MODEL);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", AI_SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_text);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddNumberToObject(root, "max_tokens", 200);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) { ESP_LOGE(TAG, "LLM: JSON 序列化失败"); return ESP_FAIL; }

    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", DASHSCOPE_API_KEY);

    esp_http_client_config_t cfg = {
        .url           = DASHSCOPE_LLM_URL,
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 30000,
        .event_handler = _http_event_cb,
        .buffer_size   = 16384,
        .buffer_size_tx = 16384,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body); return ESP_FAIL; }

    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "LLM: HTTP 失败, status=%d, err=%d", status, err);
        if (g_http_buf_len > 0) ESP_LOGE(TAG, "LLM 响应: %s", g_http_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LLM 响应: %s", g_http_buf);

    cJSON *resp    = cJSON_Parse(g_http_buf);
    cJSON *choices = resp ? cJSON_GetObjectItem(resp, "choices") : NULL;
    cJSON *c0      = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg     = c0 ? cJSON_GetObjectItem(c0, "message") : NULL;
    cJSON *content = msg ? cJSON_GetObjectItem(msg, "content") : NULL;

    if (!content || !content->valuestring) {
        ESP_LOGE(TAG, "LLM: 响应缺少 content");
        if (resp) cJSON_Delete(resp);
        return ESP_FAIL;
    }

    strncpy(reply_out, content->valuestring, reply_size - 1);
    reply_out[reply_size - 1] = '\0';
    cJSON_Delete(resp);

    ESP_LOGI(TAG, "AI 回复: %s", reply_out);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  公开接口                                                             */
/* ------------------------------------------------------------------ */
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

    // 读取 WAV 文件
    size_t wav_size = 0;
    uint8_t *wav_data = read_whole_file(wav_path, &wav_size);
    if (!wav_data || wav_size == 0) {
        snprintf(ai_reply_out, reply_out_size, "读取录音文件失败");
        if (wav_data) free(wav_data);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "WAV 文件: %u 字节", (unsigned)wav_size);

    // ASR
    esp_err_t ret = call_asr(wav_data, wav_size, asr_text_out, asr_out_size);
    free(wav_data);

    if (ret != ESP_OK) {
        snprintf(ai_reply_out, reply_out_size, "语音识别失败，请检查网络后重试");
        return ret;
    }
    if (strlen(asr_text_out) == 0) {
        snprintf(ai_reply_out, reply_out_size, "未识别到有效语音，请重新录音");
        return ESP_FAIL;
    }

    // LLM
    ret = call_llm(asr_text_out, ai_reply_out, reply_out_size);
    if (ret != ESP_OK) {
        snprintf(ai_reply_out, reply_out_size, "AI 服务请求失败，请稍后重试");
    }
    return ret;
}
