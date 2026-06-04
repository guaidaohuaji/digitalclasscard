#include "audio_recorder.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "audio_recorder"

// ---------- 录音参数 ----------
// 16kHz 采样率：INMP441 最低 BCLK=1MHz 要求
// BCLK = 16kHz × 32bit × 2ch = 1.024MHz ✅
#define SAMPLE_RATE         16000
#define BITS_PER_SAMPLE     16
#define CHANNELS            1
#define BYTES_PER_SAMPLE    (BITS_PER_SAMPLE / 8)
#define MAX_RECORD_SECONDS  60  // 最大录音时长（秒）

// ---------- INMP441 引脚接线 (I2S1) ----------
// INMP441 SCK  -> GPIO 20 (I2S1 BCLK)
// INMP441 WS   -> GPIO 21 (I2S1 WS)
// INMP441 SD   -> GPIO 22 (I2S1 DATA IN)
// INMP441 L/R  -> GND (左声道)
#define I2S_BCLK_IO         GPIO_NUM_20
#define I2S_WS_IO           GPIO_NUM_21
#define I2S_DIN_IO          GPIO_NUM_22
#define I2S_PORT_NUM        I2S_NUM_1

// ---------- 内部状态 ----------
static volatile bool            s_recording = false;
static FILE                    *s_rec_file = NULL;
static volatile uint32_t        s_data_bytes = 0;
static TaskHandle_t             s_rec_task_handle = NULL;
static volatile uint32_t        s_rec_start_tick = 0;

// ==================== 录音任务 ====================
static void rec_task(void *arg)
{
    i2s_chan_handle_t rx_chan = (i2s_chan_handle_t)arg;
    // INMP441 输出 24-bit 数据在 32-bit 时隙中，故用 int32_t 接收
    int32_t buffer[512];
    size_t bytes_read;

    while (s_recording) {
        // 超时保护：超过最大录音时长自动停止
        if ((xTaskGetTickCount() - s_rec_start_tick) > pdMS_TO_TICKS(MAX_RECORD_SECONDS * 1000)) {
            ESP_LOGW(TAG, "录音达到最大时长 %d 秒，自动停止", MAX_RECORD_SECONDS);
            break;
        }
        if (i2s_channel_read(rx_chan, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(100)) != ESP_OK) {
            ESP_LOGE(TAG, "I2S 读取错误");
            break;
        }
        if (bytes_read > 0) {
            // INMP441 输出标准 I2S 立体声，每帧 2 个 32-bit 通道 = 8 字节
            // L/R 接地 → 左声道有效，右声道为 0
            // 取左声道 32-bit 值右移 8 位取高 16 位（INMP441 24-bit 左对齐，高 16 位 = 有效音频）
            int frames = bytes_read / 8;
            int16_t mono[256];
            for (int i = 0; i < frames; i++) {
                // INMP441 输出 24-bit 左对齐（bits 31:8），>> 16 取高 16 位 → 全动态范围
                mono[i] = (int16_t)(buffer[i * 2] >> 16);
            }
            size_t mono_bytes = frames * sizeof(int16_t);
            if (s_rec_file && s_recording) {
                fwrite(mono, 1, mono_bytes, s_rec_file);
                s_data_bytes += mono_bytes;
            }
        }
    }

    // 录音结束，释放 I2S 资源
    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);
    ESP_LOGI(TAG, "I2S1 已释放，DMA 内存归还");

    vTaskDelete(NULL);
}

// ==================== 外部接口 ====================
esp_err_t audio_recorder_init(void)
{
    // 不在启动时创建 I2S 通道，避免 DMA 缓冲区与 SDIO 竞争
    // 所有资源延迟到录音开始时分配
    ESP_LOGI(TAG, "INMP441 音频录制模块就绪 (延迟初始化模式, %d Hz, %d bit)",
             SAMPLE_RATE, BITS_PER_SAMPLE);
    return ESP_OK;
}

void audio_recorder_start(const char *file_path)
{
    if (s_recording) audio_recorder_stop();

    // ---- 打开文件 ----
    s_rec_file = fopen(file_path, "wb");
    if (!s_rec_file) {
        ESP_LOGE(TAG, "无法创建录音文件: %s", file_path);
        return;
    }

    wav_header_t dummy = {0};
    fwrite(&dummy, sizeof(dummy), 1, s_rec_file);
    s_data_bytes = 0;

    // ---- 创建并初始化 I2S1 通道（仅录音时占用 DMA 内存）----
    i2s_chan_handle_t rx_chan = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败");
        fclose(s_rec_file);
        s_rec_file = NULL;
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        // INMP441 输出 24-bit 数据，需使用 32-bit 时隙宽度以匹配其时钟节拍
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(rx_chan, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 标准模式失败");
        i2s_del_channel(rx_chan);
        fclose(s_rec_file);
        s_rec_file = NULL;
        return;
    }

    // ---- enable 并开始录音任务 ----
    if (i2s_channel_enable(rx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "I2S enable 失败");
        i2s_del_channel(rx_chan);
        fclose(s_rec_file);
        s_rec_file = NULL;
        return;
    }

    s_recording = true;
    s_rec_start_tick = xTaskGetTickCount();
    // 将 rx_chan 传递给录音任务，由任务负责在结束时释放
    xTaskCreate(rec_task, "rec_task", 8192, (void *)rx_chan, 5, &s_rec_task_handle);
    ESP_LOGI(TAG, "开始录音: %s (I2S1 已动态创建)", file_path);
}

void audio_recorder_stop(void)
{
    if (!s_recording) return;
    s_recording = false;

    // 等待录音任务自行结束并释放 I2S 资源
    while (eTaskGetState(s_rec_task_handle) != eDeleted) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 写 WAV 头部
    uint32_t data_size = s_data_bytes;
    uint32_t file_size = data_size + sizeof(wav_header_t) - 8;
    wav_header_t header = {
        .riff_id = 0x46464952,
        .file_size = file_size,
        .wave_id = 0x45564157,
        .fmt_id = 0x20746D66,
        .fmt_size = 16,
        .audio_format = 1,
        .num_channels = CHANNELS,
        .sample_rate = SAMPLE_RATE,
        .byte_rate = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE,
        .block_align = CHANNELS * BYTES_PER_SAMPLE,
        .bits_per_sample = BITS_PER_SAMPLE,
        .data_id = 0x61746164,
        .data_size = data_size,
    };

    fseek(s_rec_file, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, s_rec_file);
    fclose(s_rec_file);
    s_rec_file = NULL;

    ESP_LOGI(TAG, "录音结束，数据 %lu 字节", data_size);
}