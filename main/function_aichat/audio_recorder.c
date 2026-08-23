#include "audio_recorder.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "audio_recorder"

#define SAMPLE_RATE         16000
#define BITS_PER_SAMPLE     16
#define CHANNELS            1
#define BYTES_PER_SAMPLE    (BITS_PER_SAMPLE / 8)
#define MAX_RECORD_SECONDS  60

#define I2S_BCLK_IO         GPIO_NUM_20
#define I2S_WS_IO           GPIO_NUM_21
#define I2S_DIN_IO          GPIO_NUM_22
#define I2S_PORT_NUM        I2S_NUM_1

static volatile bool            s_recording = false;
static FILE                    *s_rec_file = NULL;
static volatile uint32_t        s_data_bytes = 0;
static TaskHandle_t             s_rec_task_handle = NULL;
static volatile uint32_t        s_rec_start_tick = 0;

static void rec_task(void *arg)
{
    i2s_chan_handle_t rx_chan = (i2s_chan_handle_t)arg;
    int32_t buffer[512];
    size_t bytes_read;

    while (s_recording) {
        if ((xTaskGetTickCount() - s_rec_start_tick) > pdMS_TO_TICKS(MAX_RECORD_SECONDS * 1000)) {
            ESP_LOGW(TAG, "录音达到最大时长 %d 秒，自动停止", MAX_RECORD_SECONDS);
            break;
        }
        if (i2s_channel_read(rx_chan, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(100)) != ESP_OK) {
            ESP_LOGE(TAG, "I2S 读取错误");
            break;
        }
        if (bytes_read > 0) {
            int frames = bytes_read / 8;
            int16_t mono[256];
            for (int i = 0; i < frames; i++) {
                mono[i] = (int16_t)(buffer[i * 2] >> 16);
            }
            size_t mono_bytes = frames * sizeof(int16_t);
            if (s_rec_file && s_recording) {
                fwrite(mono, 1, mono_bytes, s_rec_file);
                s_data_bytes += mono_bytes;
            }
        }
    }

    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);
    ESP_LOGI(TAG, "I2S1 已释放，DMA 内存归还");
    vTaskDelete(NULL);
}

esp_err_t audio_recorder_init(void)
{
    ESP_LOGI(TAG, "INMP441 音频录制模块就绪 (延迟初始化模式, %d Hz, %d bit)", SAMPLE_RATE, BITS_PER_SAMPLE);
    return ESP_OK;
}

esp_err_t audio_recorder_start(const char *file_path)
{
    if (!file_path) return ESP_ERR_INVALID_ARG;
    if (s_recording) audio_recorder_stop();

    s_rec_file = fopen(file_path, "wb");
    if (!s_rec_file) {
        ESP_LOGE(TAG, "无法创建录音文件: %s", file_path);
        return ESP_FAIL;
    }

    wav_header_t dummy = {0};
    fwrite(&dummy, sizeof(dummy), 1, s_rec_file);
    s_data_bytes = 0;

    i2s_chan_handle_t rx_chan = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败: %s", esp_err_to_name(ret));
        fclose(s_rec_file);
        s_rec_file = NULL;
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_IO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    ret = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 标准模式失败: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        fclose(s_rec_file);
        s_rec_file = NULL;
        return ret;
    }

    ret = i2s_channel_enable(rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S enable 失败: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        fclose(s_rec_file);
        s_rec_file = NULL;
        return ret;
    }

    s_recording = true;
    s_rec_start_tick = xTaskGetTickCount();
    if (xTaskCreate(rec_task, "rec_task", 8192, (void *)rx_chan, 5, &s_rec_task_handle) != pdPASS) {
        s_recording = false;
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
        fclose(s_rec_file);
        s_rec_file = NULL;
        ESP_LOGE(TAG, "创建录音任务失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "开始录音: %s (I2S1 已动态创建)", file_path);
    return ESP_OK;
}

void audio_recorder_stop(void)
{
    if (!s_recording) return;
    s_recording = false;

    if (s_rec_task_handle) {
        while (eTaskGetState(s_rec_task_handle) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_rec_task_handle = NULL;
    }

    if (!s_rec_file) return;

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
