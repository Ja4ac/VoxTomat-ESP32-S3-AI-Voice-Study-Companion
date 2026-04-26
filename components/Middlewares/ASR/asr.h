#ifndef ASR_H_
#define ASR_H_

#include "esp_err.h"
#include "project_secrets.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ASR_BAIDU_API_KEY      PROJECT_ASR_BAIDU_API_KEY
#define ASR_BAIDU_SECRET_KEY   PROJECT_ASR_BAIDU_SECRET_KEY

#define ASR_RECORD_SECONDS     4                // 录制音频时长
#define ASR_SAMPLE_RATE        16000            // 音频采样率
#define ASR_MAX_DATA_SIZE      (ASR_SAMPLE_RATE * ASR_RECORD_SECONDS * sizeof(int16_t))         // 最大音频数据大小（字节）

esp_err_t asr_init(void);
char *asr_recognize(int16_t *audio, size_t audio_bytes);

#endif
