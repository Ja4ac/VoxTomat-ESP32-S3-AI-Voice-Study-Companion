#ifndef I2S_H_
#define I2S_H_

#include "esp_err.h"
#include "driver/gpio.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

//I2S_MIC:INMP441       :先以int32_t采样，实际有效数据长度为高24位，取16位作为采集内容
//I2S_SPK:MAX98357A

#define I2S_MIC_WS          GPIO_NUM_4
#define I2S_MIC_SCK         GPIO_NUM_5
#define I2S_MIC_SD          GPIO_NUM_6

#define I2S_SPK_LRC    GPIO_NUM_7
#define I2S_SPK_BCLK   GPIO_NUM_15
#define I2S_SPK_DIN    GPIO_NUM_16
#define I2S_SPK_SD     GPIO_NUM_8

#define I2S_MIC_MAX_SAMPLES_PER_READ    1024

esp_err_t i2s_mic_init(void);
size_t i2s_mic_read(int16_t *data, size_t sample_count);
esp_err_t i2s_mic_deinit(void);

esp_err_t i2s_spk_init(void);
esp_err_t i2s_spk_write(const uint8_t *data, size_t size, size_t *bytes_written);
esp_err_t i2s_spk_deinit(void);

#endif
