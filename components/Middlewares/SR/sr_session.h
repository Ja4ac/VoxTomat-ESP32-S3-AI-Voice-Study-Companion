#ifndef SR_SESSION_H_
#define SR_SESSION_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * sr_session 用来缓存“用户这一句话”的 PCM。
 * 它只负责音频缓冲，不和网络、ASR、LLM、TTS 耦合。
 */

/* 默认缓存 8 秒 16kHz 单声道 16bit PCM，足够第一阶段使用。 */
#define SR_SESSION_DEFAULT_MAX_AUDIO_BYTES   (16000 * 8 * sizeof(int16_t))

typedef struct 
{
    int16_t *buffer;                    // 16bit PCM 音频数据缓存缓冲区（存储麦克风采集/AFE预处理后的音频数据）
    size_t capacity_bytes;              // 缓冲区总容量（单位：字节），标识最大能存储多少音频数据
    size_t length_bytes;                // 缓冲区当前存储的有效音频数据长度（单位：字节）
    bool vad_cache_inserted;            // VAD语音活动检测缓存插入标记：标识是否已完成VAD相关数据的缓存插入
    bool initialized;
} sr_session_t;

esp_err_t sr_session_init(sr_session_t *session, size_t capacity_bytes);
void sr_session_reset(sr_session_t *session);
esp_err_t sr_session_prepend_vad_cache(sr_session_t *session, const int16_t *data, size_t bytes);
esp_err_t sr_session_append(sr_session_t *session, const int16_t *data, size_t bytes);
bool sr_session_has_audio(const sr_session_t *session);
esp_err_t sr_session_clone_audio(const sr_session_t *session, int16_t **out_audio, size_t *out_bytes);
void sr_session_deinit(sr_session_t *session);

#endif
