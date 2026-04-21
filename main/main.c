#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s.h"
#include "asr.h"
#include "tts.h"
#include "llm.h"
#include "sr_engine.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl_port.h"

static const char *TAG = "APP";

static esp_err_t app_init(void);
static void app_start_tasks(void);

static sr_engine_t s_sr_engine;
static sr_event_t s_sr_event;

static QueueHandle_t s_xQueue_asr_to_llm;
static QueueHandle_t s_xQueue_llm_to_tts;
static QueueHandle_t s_xQueue_sr_event;
static QueueHandle_t s_xQueue_sr_event_to_asr;

#define APP_JTAG_SAFE_BOOT_DELAY_MS 2000

// 任务 ASR 配置
#define TASK_ASR_STACK 8192
#define TASK_ASR_PRIORITY 1
static TaskHandle_t task_asr_handle = NULL;
static void task_asr(void *pvParameters);

// 任务 LLM 配置
#define TASK_LLM_STACK 8192
#define TASK_LLM_PRIORITY 1
static TaskHandle_t task_llm_handle = NULL;
static void task_llm(void *pvParameters);

// 任务 TTS 配置
#define TASK_TTS_STACK 12288
#define TASK_TTS_PRIORITY 1
static TaskHandle_t task_tts_handle = NULL;
static void task_tts(void *pvParameters);

// 任务 SR配置存放在sr_engine中

// 任务 SR_EVENT 配置
#define TASK_SR_EVENT_STACK 2048
#define TASK_SR_EVENT_PRIORITY 1
static TaskHandle_t task_sr_event_handle = NULL;
static void task_sr_event(void *pvParameters);

void app_main(void)
{
    ESP_LOGI(TAG, "Version: 1.3.0");
    esp_err_t err;

    /*
     * Leave a short attach window after reset so OpenOCD/JTAG can halt the chip
     * before SPI/LCD/LVGL/PSRAM related activity starts ramping up.
     */
    // ESP_LOGI(TAG, "JTAG safe boot delay: %d ms", APP_JTAG_SAFE_BOOT_DELAY_MS);
    // vTaskDelay(pdMS_TO_TICKS(APP_JTAG_SAFE_BOOT_DELAY_MS));

    s_xQueue_asr_to_llm = xQueueCreate(3, sizeof (char*));
    s_xQueue_llm_to_tts = xQueueCreate(3, sizeof (char*));
    s_xQueue_sr_event = xQueueCreate(3, sizeof(sr_event_t));
    s_xQueue_sr_event_to_asr = xQueueCreate(3, sizeof (sr_event_t));
    if(s_xQueue_asr_to_llm == NULL || s_xQueue_llm_to_tts == NULL || s_xQueue_sr_event == NULL || s_xQueue_sr_event_to_asr == NULL)
    {
        ESP_LOGE(TAG, "app_main: Failed to create Queue");
        return;
    }

    err = app_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init app: %s", esp_err_to_name(err));
        return;
    }

    err = sr_engine_init(&s_sr_engine, s_xQueue_sr_event);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init sr engine: %s", esp_err_to_name(err));
        return;
    }
    
    app_start_tasks();
    err = sr_engine_start(&s_sr_engine);
    if(err != ESP_OK)
    {
        sr_engine_emit_error(&s_sr_engine, err);
    }
}

static esp_err_t app_init(void)
{
    esp_err_t err;

    err = i2s_mic_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init MIC: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_spk_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init spk: %s", esp_err_to_name(err));
        return err;
    }
    err = tts_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init TTS: %s", esp_err_to_name(err));
        return err;
    }
    err = asr_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init ASR: %s", esp_err_to_name(err));
        return err;
    }
    err = llm_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init LLM: %s", esp_err_to_name(err));
        return err;
    }

    err = lvgl_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init LVGL: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

static void app_start_tasks(void)
{
    BaseType_t ok = xTaskCreate(
        (TaskFunction_t)task_asr,
        "task_asr",
        TASK_ASR_STACK,
        NULL,
        TASK_ASR_PRIORITY,
        &task_asr_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ASR task");
    }

    ok = xTaskCreate(
        (TaskFunction_t)task_llm,
        "task_llm",
        TASK_LLM_STACK,
        NULL,
        TASK_LLM_PRIORITY,
        &task_llm_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LLM task");
    }

    ok = xTaskCreate(
        (TaskFunction_t)task_tts,
        "task_tts",
        TASK_TTS_STACK,
        NULL,
        TASK_TTS_PRIORITY,
        &task_tts_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS task");
    }

    ok = xTaskCreate(
        (TaskFunction_t)task_sr_event,
        "task_sr_event",
        TASK_SR_EVENT_STACK,
        NULL,
        TASK_SR_EVENT_PRIORITY,
        &task_sr_event_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SR_EVENT task");
    }
}

// ASR任务：接收到sr_event发送的信号后，执行语音识别，将识别结果通过队列发送给 TTS 任务。
static void task_asr(void *pvParameters)
{
    char *asr_buffer = NULL;
    sr_event_t sr_event_cpy;
    while (1) {
        if(xQueueReceive(s_xQueue_sr_event_to_asr, &sr_event_cpy, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI(TAG, "Start to ASR");
            asr_buffer = asr_recognize(sr_event_cpy.audio, sr_event_cpy.audio_bytes);
            sr_engine_reset_session(&s_sr_engine);
            free(sr_event_cpy.audio);
            sr_event_cpy.audio = NULL;

            if(asr_buffer == NULL || asr_buffer[0] == '\0')
            {
                ESP_LOGE(TAG, "Task_asr: Empty asr result");
                free(asr_buffer);
                asr_buffer = NULL;
                
                esp_err_t err = sr_engine_start(&s_sr_engine);
                if (err != ESP_OK) 
                {
                    sr_engine_emit_error(&s_sr_engine, err);
                }
                continue;
            }
            else
            {
                ESP_LOGI(TAG, "Task_asr: Recognize result: %s", asr_buffer);
                if(xQueueSend(s_xQueue_asr_to_llm, &asr_buffer, pdMS_TO_TICKS(2000)) != pdPASS)
                {
                    ESP_LOGE(TAG, "Task_asr: xQueueSend error");
                    sr_engine_emit_error(&s_sr_engine, ESP_FAIL);
                    free(asr_buffer);
                    asr_buffer = NULL;
                }
            }
        }
    }
}

// LLM任务：等待 ASR 结果，将 ASR 结果发送至 LLM 进行语义处理
static void task_llm(void *pvParameters)
{
    char *asr_buffer = NULL;
    char *llm_buffer = NULL;
    esp_err_t err;
    while(1)
    {
        llm_buffer = NULL;
        if(xQueueReceive(s_xQueue_asr_to_llm, &asr_buffer, portMAX_DELAY) == pdPASS)
        {
            err = llm_chat(asr_buffer, &llm_buffer);
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Task_llm: llm_chat error: %s", esp_err_to_name(err));
                free(asr_buffer);
                asr_buffer = NULL;
                sr_engine_emit_error(&s_sr_engine, err);
                continue;
            }
            free(asr_buffer);
            asr_buffer = NULL;
            if(xQueueSend(s_xQueue_llm_to_tts, &llm_buffer, pdMS_TO_TICKS(2000)) != pdPASS)
            {
                ESP_LOGE(TAG, "Task_llm: xQueueSend error");
                free(llm_buffer);
                llm_buffer = NULL;
                sr_engine_emit_error(&s_sr_engine, ESP_FAIL);
            }
        }
    }
}

// TTS任务：阻塞等待队列中的识别文本，调用豆包 TTS 合成语音并播放，
static void task_tts(void *pvParameters)
{
    char *llm_buffer = NULL;
    esp_err_t err;
    while(1)
    {
        if(xQueueReceive(s_xQueue_llm_to_tts, &llm_buffer, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI(TAG, "Task_tts: Start to TTS");
            err = tts_speak_text(llm_buffer);
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Task_tts: tts_speak_text error: %s", esp_err_to_name(err));
                sr_engine_emit_error(&s_sr_engine, err);
                free(llm_buffer);
                llm_buffer = NULL;
                continue;
            }
            free(llm_buffer);
            llm_buffer = NULL;
        }
        err = sr_engine_start(&s_sr_engine);
        if(err != ESP_OK)
        {
            sr_engine_emit_error(&s_sr_engine, err);
        }
    }
}

// SR_EVENT任务：判断SR_EVENT事件标志
static void task_sr_event(void *pvParameters)
{
    esp_err_t err;
    sr_event_reset(&s_sr_event);
    while(1)
    {
        if(xQueueReceive(s_sr_engine.event_queue, &s_sr_event, portMAX_DELAY) == pdPASS)
        {
            if(s_sr_event.type == SR_EVENT_WAKEUP)
            {
                ESP_LOGI(TAG, "Receive SR_EVENT: WAKEUP");
            }
            if(s_sr_event.type == SR_EVENT_VAD_START)
            {
                ESP_LOGI(TAG, "Receive SR_EVENT: VAD_START");
            }
            if(s_sr_event.type == SR_EVENT_AUDIO_READY)
            {
                ESP_LOGI(TAG, "Receive SR_EVENT: AUDIO_READY");
                err = sr_engine_stop(&s_sr_engine);
                if(err != ESP_OK)
                {
                    free(s_sr_event.audio);
                    ESP_LOGE(TAG, "Failed to stop s_sr_engine");
                    continue;
                }
                if(xQueueSend(s_xQueue_sr_event_to_asr, &s_sr_event, portMAX_DELAY) != pdPASS)
                {
                    free(s_sr_event.audio);
                    sr_engine_emit_error(&s_sr_engine, ESP_FAIL);
                }
            }
            if(s_sr_event.type == SR_EVENT_ERROR)
            {
                free(s_sr_event.audio);
                s_sr_event.audio = NULL;

                ESP_LOGE(TAG, "Receive SR_ERROR: %s", esp_err_to_name(s_sr_event.error_code));
                err = sr_engine_stop(&s_sr_engine);
                if(err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to stop s_sr_engine: %s", esp_err_to_name(err));
                }

                err = sr_engine_reset_session(&s_sr_engine);
                if(err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to reset s_sr_engine_session: %s", esp_err_to_name(err));
                }

                err = sr_engine_start(&s_sr_engine);
                if(err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to restart engine: %s", esp_err_to_name(err));
                }
            }
        }
    }
}
