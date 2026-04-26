#ifndef SR_ENGINE_H_
#define SR_ENGINE_H_

#include "sr_event.h"
#include "sr_model.h"
#include "sr_session.h"

#include "esp_err.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SR_ENGINE_FEED_TASK_STACK         6144
#define SR_ENGINE_FETCH_TASK_STACK        6144
#define SR_ENGINE_TASK_PRIORITY           4

/*
 * sr_engine 是 SR 模块里的核心：
 * - 初始化模型与 AFE
 * - 创建 feed/fetch 两个任务
 * - 维护“是否唤醒 / 是否正在录一句话”的状态
 * - 通过事件队列把结果告诉上层
 */
typedef struct 
{
    QueueHandle_t event_queue;              // 语音识别事件队列句柄，用于向应用层发送唤醒、语音就绪、错误等事件

    TaskHandle_t feed_task_handle;          // 音频喂入任务句柄：负责从麦克风读取音频并持续喂入AFE模块
    TaskHandle_t fetch_task_handle;         // 音频获取任务句柄：负责从AFE模块获取预处理后的音频并处理识别逻辑

    sr_model_ctx_t model;                   // 语音识别模型上下文：管理所有语音模型、AFE配置与运行实例
    sr_session_t session;                   // 语音会话上下文：管理单次语音交互的音频缓存、VAD状态等会话数据

    bool running;                           // 运行态
    bool awakened;                          // 唤醒态
    bool recording;                         // 录音态
} sr_engine_t;

esp_err_t sr_engine_emit_error(sr_engine_t *engine, esp_err_t err);
esp_err_t sr_engine_init(sr_engine_t *engine, QueueHandle_t event_queue);
esp_err_t sr_engine_start(sr_engine_t *engine);
esp_err_t sr_engine_stop(sr_engine_t *engine);
esp_err_t sr_engine_reset_session(sr_engine_t *engine);
bool sr_engine_is_awakened(const sr_engine_t *engine);
void sr_engine_deinit(sr_engine_t *engine);

#endif
