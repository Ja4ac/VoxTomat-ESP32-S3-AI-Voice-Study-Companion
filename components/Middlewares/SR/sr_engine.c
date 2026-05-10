#include "sr_engine.h"

#include <string.h>

#include "esp_log.h"
#include "esp_vad.h"
#include "esp_wn_iface.h"
#include "i2s.h"

static const char *TAG = "SR_ENGINE";

static esp_err_t sr_engine_emit_event(sr_engine_t *engine, sr_event_t *event)
{
    if (engine == NULL || event == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (engine->event_queue == NULL) 
    {
        sr_event_release_audio(event);
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(engine->event_queue, event, pdMS_TO_TICKS(100)) != pdPASS) 
    {
        ESP_LOGW(TAG, "Failed to send SR event, queue full");
        sr_event_release_audio(event);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t sr_engine_emit_error(sr_engine_t *engine, esp_err_t err)
{
    sr_event_t event;

    sr_event_reset(&event);
    event.type = SR_EVENT_ERROR;
    event.error_code = err;
    ESP_LOGE(TAG, "Receive sr_event error: %s", esp_err_to_name(err));
    return sr_engine_emit_event(engine, &event);
}

// 读取指定样本数量音频
static esp_err_t sr_engine_read_exact_samples(sr_engine_t *engine, int16_t *buffer, size_t sample_count)
{
    size_t total_read = 0;

    if (engine == NULL || buffer == NULL || sample_count == 0) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    while (engine->running && total_read < sample_count) 
    {
        size_t current_read = i2s_mic_read(buffer + total_read, sample_count - total_read);
        if (current_read == 0)              // 如果本次没有读取到音频，短暂延时后继续
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        total_read += current_read;
    }

    return engine->running ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// 三态运行逻辑处理，获取真实音频数据
static void sr_handle_fetch_result(sr_engine_t *engine, const afe_fetch_result_t *result)
{
    esp_err_t err;
    sr_event_t event;

    if (engine == NULL || result == NULL) 
    {
        return;
    }

    // 运行态：等待命中唤醒词
    if (!engine->awakened) 
    {
        if (result->wakeup_state == WAKENET_DETECTED) 
        {
            engine->awakened = true;
            engine->recording = false;
            sr_session_reset(&engine->session);

            // 关闭WakeNet，防止超时重置管道干扰VAD
            engine->model.afe_handle->disable_wakenet(engine->model.afe_data);

            // 发送唤醒事件
            sr_event_reset(&event);
            event.type = SR_EVENT_WAKEUP;
            event.wake_word_index = result->wake_word_index;
            sr_engine_emit_event(engine, &event);

            ESP_LOGI(TAG, "Wake word detected, wake_word_index=%d", result->wake_word_index);
        }
        return;
    }

    // 唤醒态：等待VAD判断用户开始说话
    if (!engine->recording) 
    {
        if (result->vad_state == VAD_SPEECH) 
        {
            engine->recording = true;
            sr_session_reset(&engine->session);

            // 插入VAD预缓存音频
            err = sr_session_prepend_vad_cache(&engine->session, result->vad_cache, result->vad_cache_size);
            if (err != ESP_OK) 
            {
                ESP_LOGE(TAG, "Failed to prepend vad_cache: %s", esp_err_to_name(err));
                sr_engine_emit_error(engine, err);
            }
            // 追加当前语音帧
            err = sr_session_append(&engine->session, result->data, result->data_size);
            if (err != ESP_OK) 
            {
                ESP_LOGE(TAG, "Failed to append first speech chunk: %s", esp_err_to_name(err));
                sr_engine_emit_error(engine, err);
            }

            // 发送语音开始事件
            sr_event_reset(&event);
            event.type = SR_EVENT_VAD_START;
            sr_engine_emit_event(engine, &event);
            ESP_LOGI(TAG, "VAD start, begin caching one sentence");
        }
        return;
    }

    // 录音态：直到用户说话结束
    if (result->vad_state == VAD_SPEECH) 
    {
        err = sr_session_append(&engine->session, result->data, result->data_size);
        if (err != ESP_OK) 
        {
            ESP_LOGE(TAG, "Failed to append speech chunk: %s", esp_err_to_name(err));
            sr_engine_emit_error(engine, err);
        }
        return;
    }

    /*
     * 到这里说明：
     * - 已经被唤醒
     * - 也已经开始正式收音
     * - 现在 VAD 判断用户说完了
     *
     * 因此把整句音频复制出来，通过事件交给上层。
     */
    if (sr_session_has_audio(&engine->session)) 
    {
        sr_event_reset(&event);
        event.type = SR_EVENT_AUDIO_READY;

        err = sr_session_clone_audio(&engine->session, &event.audio, &event.audio_bytes);
        if (err != ESP_OK) 
        {
            ESP_LOGE(TAG, "Failed to clone sentence audio: %s", esp_err_to_name(err));
            sr_engine_emit_error(engine, err);
        } 
        else 
        {
            sr_engine_emit_event(engine, &event);
            ESP_LOGI(TAG, "One sentence ready, bytes=%u", (unsigned)event.audio_bytes);
        }
    }

    sr_engine_reset_session(engine);
}

// 音频喂入任务：持续从麦克风读取音频，喂给AFE音频前端
static void sr_feed_task(void *arg)
{
    sr_engine_t *engine = (sr_engine_t *)arg;
    int16_t *feed_buffer = NULL;
    size_t sample_count;
    esp_err_t err;
    int feed_ret;

    if (engine == NULL) 
    {
        vTaskDelete(NULL);
        return;
    }

    // AFE 要求一次 feed 固定大小的数据，对当前单麦场景，sample_count = feed_chunksize * 1。
    sample_count = (size_t)engine->model.feed_chunksize * (size_t)engine->model.feed_channel_num;
    feed_buffer = malloc(sample_count * sizeof(int16_t));
    if (feed_buffer == NULL) 
    {
        ESP_LOGE(TAG, "Failed to allocate feed buffer");
        sr_engine_emit_error(engine, ESP_ERR_NO_MEM);
        engine->feed_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (engine->running) 
    {
        // 读取指定样本数量音频
        err = sr_engine_read_exact_samples(engine, feed_buffer, sample_count);
        if (err != ESP_OK)
        {
            break;
        }

        // 将音频数据喂给AFE
        feed_ret = engine->model.afe_handle->feed(engine->model.afe_data, feed_buffer);
        if (feed_ret <= 0) 
        {
            ESP_LOGW(TAG, "AFE feed returned %d", feed_ret);
        }
    }

    free(feed_buffer);
    engine->feed_task_handle = NULL;
    vTaskDelete(NULL);
}

// 音频获取任务：持续从AFE获取预处理结果，并执行识别逻辑
static void sr_fetch_task(void *arg)
{
    sr_engine_t *engine = (sr_engine_t *)arg;
    afe_fetch_result_t *result = NULL;

    if (engine == NULL) 
    {
        vTaskDelete(NULL);
        return;
    }

    while (engine->running) 
    {
        // 从AFE中获取输出结果
        if (engine->model.afe_handle->fetch_with_delay != NULL) 
        {
            result = engine->model.afe_handle->fetch_with_delay(engine->model.afe_data, pdMS_TO_TICKS(200));
        } 
        else 
        {
            result = engine->model.afe_handle->fetch(engine->model.afe_data);
        }

        if (result == NULL) 
        {
            continue;
        }

        sr_handle_fetch_result(engine, result);
    }

    engine->fetch_task_handle = NULL;
    vTaskDelete(NULL);
}

// 初始化顺序：先建模型和 AFE。再建“缓存一句话”的 session。start 时再创建任务。
esp_err_t sr_engine_init(sr_engine_t *engine, QueueHandle_t event_queue)
{
    esp_err_t err;

    if (engine == NULL || event_queue == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(engine, 0, sizeof(*engine));
    engine->event_queue = event_queue;

    err = sr_model_init(&engine->model);
    if (err != ESP_OK) 
    {
        return err;
    }

    err = sr_session_init(&engine->session, SR_SESSION_DEFAULT_MAX_AUDIO_BYTES);
    if (err != ESP_OK) 
    {
        sr_model_deinit(&engine->model);
        return err;
    }

    engine->running = false;
    engine->awakened = false;
    engine->recording = false;
    return ESP_OK;
}

esp_err_t sr_engine_start(sr_engine_t *engine)
{
    BaseType_t ok;

    if (engine == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (engine->running) 
    {
        return ESP_OK;
    }

    engine->running = true;

    ok = xTaskCreate(sr_feed_task,
                     "sr_feed_task",
                     SR_ENGINE_FEED_TASK_STACK,
                     engine,
                     SR_ENGINE_TASK_PRIORITY,
                     &engine->feed_task_handle);
    if (ok != pdPASS) 
    {
        engine->running = false;
        engine->feed_task_handle = NULL;
        return ESP_FAIL;
    }

    ok = xTaskCreate(sr_fetch_task,
                     "sr_fetch_task",
                     SR_ENGINE_FETCH_TASK_STACK,
                     engine,
                     SR_ENGINE_TASK_PRIORITY,
                     &engine->fetch_task_handle);
    if (ok != pdPASS) 
    {
        engine->running = false;
        if (engine->feed_task_handle != NULL) 
        {
            vTaskDelete(engine->feed_task_handle);
            engine->feed_task_handle = NULL;
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SR engine started");
    return ESP_OK;
}

esp_err_t sr_engine_stop(sr_engine_t *engine)
{
    if (engine == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    engine->running = false;

    /*
     * 先给任务一个自然退出的机会：
     * - feed task 最多会卡在一次 I2S 读取超时
     * - fetch task 最多会卡在一次 fetch_with_delay
     */
    vTaskDelay(pdMS_TO_TICKS(250));

    if (engine->feed_task_handle != NULL) 
    {
        vTaskDelete(engine->feed_task_handle);
        engine->feed_task_handle = NULL;
    }

    if (engine->fetch_task_handle != NULL) 
    {
        vTaskDelete(engine->fetch_task_handle);
        engine->fetch_task_handle = NULL;
    }

    ESP_LOGI(TAG, "SR engine stopped");
    return ESP_OK;
}

esp_err_t sr_engine_reset_session(sr_engine_t *engine)
{
    if (engine == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    sr_session_reset(&engine->session);
    engine->awakened = false;
    engine->recording = false;

    if (engine->model.afe_handle != NULL && engine->model.afe_data != NULL) 
    {
        engine->model.afe_handle->reset_buffer(engine->model.afe_data);
        engine->model.afe_handle->reset_vad(engine->model.afe_data);
        // 重新打开WakeNet，恢复下一次唤醒词检测
        engine->model.afe_handle->enable_wakenet(engine->model.afe_data);
    }

    return ESP_OK;
}

bool sr_engine_is_awakened(const sr_engine_t *engine)
{
    if (engine == NULL) 
    {
        return false;
    }

    return engine->awakened;
}

void sr_engine_deinit(sr_engine_t *engine)
{
    if (engine == NULL) 
    {
        return;
    }

    sr_engine_stop(engine);
    sr_session_deinit(&engine->session);
    sr_model_deinit(&engine->model);
    memset(engine, 0, sizeof(*engine));
}
