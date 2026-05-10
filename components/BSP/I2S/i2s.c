#include "i2s.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"


static int32_t stereo_buf[I2S_MIC_MAX_SAMPLES_PER_READ * 2];  // 8KB, BSS段
static const char *TAG = "I2S";
static i2s_chan_handle_t s_i2s_mic_chan = NULL;
static i2s_chan_handle_t s_i2s_spk_chan = NULL;

// 创建 I2S 通道并切到标准模式
esp_err_t i2s_mic_init(void)
{
    esp_err_t err;

    i2s_chan_config_t i2s_chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    err = i2s_new_channel(&i2s_chan_config, NULL, &s_i2s_mic_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Fail to create mic channel: %s", esp_err_to_name(err));
        return err;
    }

    // INMP441 常见做法是先按 32bit stereo 读，再手动取左声道高 16 位
    i2s_std_config_t i2s_std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .bclk = I2S_MIC_SCK,
            .ws = I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_MIC_SD,
            .mclk = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    err = i2s_channel_init_std_mode(s_i2s_mic_chan, &i2s_std_config);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init mic std mode: %s", esp_err_to_name(err));
        i2s_channel_disable(s_i2s_mic_chan);
        i2s_del_channel(s_i2s_mic_chan);
        s_i2s_mic_chan = NULL;
        return err;
    }

    err = i2s_channel_enable(s_i2s_mic_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable i2s mic channel: %s", esp_err_to_name(err));
        i2s_channel_disable(s_i2s_mic_chan);
        i2s_del_channel(s_i2s_mic_chan);
        s_i2s_mic_chan = NULL;
        return err;
    }

    return ESP_OK;
}

// 内部按 32bit stereo 读取后取左声道高 16 位转换为 mono。
size_t i2s_mic_read(int16_t *data, size_t sample_count)
{
    if(s_i2s_mic_chan == NULL)
    {
        ESP_LOGE(TAG, "Failed to read mic, reason: I2S has not initialized");
        return 0;
    }

    if (sample_count > I2S_MIC_MAX_SAMPLES_PER_READ) 
    {
      sample_count = I2S_MIC_MAX_SAMPLES_PER_READ;
    }
    // 计算 stereo_bytes（sample_count 可能已被截断）
    size_t stereo_bytes = sample_count * 2 * sizeof(int32_t);

    size_t bytes_read;
    esp_err_t err = i2s_channel_read(s_i2s_mic_chan, (void*)stereo_buf, stereo_bytes, &bytes_read, pdMS_TO_TICKS(100));
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(err));
        return 0;
    }   

    // 每个frame有左右两个slot，这里只取左声道压成16bit mono PCM
    size_t slot_read = bytes_read / sizeof(int32_t);
    size_t stereo_frames = slot_read / 2;
    ESP_LOGD(TAG, "Read %d bytes (%d stereo frames)", bytes_read, stereo_frames);

    size_t count = 0;
    for(size_t i = 0; i < (stereo_frames) && (count < sample_count); i++)
    {
        int32_t left_channel_slot = stereo_buf[i*2];
        data[count++] = (int16_t)(left_channel_slot >> 16);
    }

    return count;
}

esp_err_t i2s_mic_deinit(void)
{
    esp_err_t err;
    err = i2s_channel_disable(s_i2s_mic_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to disable I2S MIC channel");
        return err;
    }
    err = i2s_del_channel(s_i2s_mic_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to delete I2S MIC channel");
        return err;
    }
    // 删通道后手动清空句柄，避免后面误用悬空句柄
    s_i2s_mic_chan = NULL;
    ESP_LOGI(TAG, "Deinit I2S MIC channel success");
    return err;
}

esp_err_t i2s_spk_init(void)
{
    esp_err_t err;
    i2s_chan_config_t i2s_chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_chan_config.auto_clear = true;
    err = i2s_new_channel(&i2s_chan_config, &s_i2s_spk_chan, NULL);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Fail to create spk channel: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t i2s_std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
                .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = 16,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .bclk = I2S_SPK_BCLK,
            .ws = I2S_SPK_LRC,
            .dout = I2S_SPK_DIN,
            .mclk = GPIO_NUM_NC,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    // 拉高SD，可以增益输出
    gpio_config_t io_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << I2S_SPK_SD),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_config);
    gpio_set_level(I2S_SPK_SD, 1);

    err = i2s_channel_init_std_mode(s_i2s_spk_chan, &i2s_std_config);
    if(err != ESP_OK)
    {
        ESP_LOGE("SPK", "Failed to init I2S std mode: %d", err);
        return err;
    }

    err = i2s_channel_enable(s_i2s_spk_chan);
    if (err != ESP_OK) {
        ESP_LOGE("SPK", "Failed to enable I2S channel: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "SPK init success");
    return ESP_OK;
}

// 向功放写入PCM音频数据，阻塞式写入，超时1000ms。
esp_err_t i2s_spk_write(const uint8_t *data, size_t size, size_t *bytes_written)
{
    esp_err_t err;
    err = i2s_channel_write(s_i2s_spk_chan, data, size, bytes_written, pdMS_TO_TICKS(1000));
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write spk data: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2s_spk_deinit(void)
{
    esp_err_t err;
    err = i2s_channel_disable(s_i2s_spk_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to disable I2S SPK channel");
        return err;
    }
    err = i2s_del_channel(s_i2s_spk_chan);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to delete I2S SPK channel");
        return err;
    }
    s_i2s_spk_chan = NULL;
    ESP_LOGI(TAG, "Deinit I2S SPK channel success");
    return err;
}