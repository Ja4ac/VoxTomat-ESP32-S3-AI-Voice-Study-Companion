#ifndef SR_EVENT_H_
#define SR_EVENT_H_

#include "esp_err.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * SR 事件定义：
 * 这个文件只负责“SR 往上层发什么消息”。
 * 先保持最小集，方便你理解整条链路。
 */

typedef enum {
    SR_EVENT_NONE = 0,
    SR_EVENT_WAKEUP,
    SR_EVENT_VAD_START,
    SR_EVENT_AUDIO_READY,
    SR_EVENT_ERROR,
} sr_event_type_t;

typedef struct {
    sr_event_type_t type;           // 标志事件类型
    esp_err_t error_code;           // 错误码
    int wake_word_index;            // 唤醒词索引

    /*
     * 当 type == SR_EVENT_AUDIO_READY 时：
     * audio / audio_bytes 表示一整句裁剪好的 PCM。
     * 这块内存默认由“上层收到事件后”负责释放。
     */
    int16_t *audio;                 // 音频数据指针
    size_t audio_bytes;             // 音频数据总字节
} sr_event_t;

static inline void sr_event_reset(sr_event_t *event)
{
    if (event == NULL) {
        return;
    }

    event->type = SR_EVENT_NONE;
    event->error_code = ESP_OK;
    event->wake_word_index = 0;
    event->audio = NULL;
    event->audio_bytes = 0;
}

static inline void sr_event_release_audio(sr_event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (event->audio != NULL) {
        free(event->audio);
        event->audio = NULL;
    }
    event->audio_bytes = 0;
}

#endif
