#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s.h"
#include "asr.h"
#include "tts.h"
#include "llm.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "APP";

static esp_err_t app_init(void);
static void app_start_tasks(void);

static SemaphoreHandle_t xShareResourceMutex;           // 用于在进行语音识别时禁止麦克风收音
static QueueHandle_t xQueue_task1_to_task2;
static QueueHandle_t xQueue_task2_to_task3;
static QueueHandle_t xQueue_task3_to_task4;
static QueueHandle_t xQueue_task4_to_task5;

// 任务1 ASR 配置
#define TASK1_ASR_STACK 8192
#define TASK1_ASR_PRIORITY 1
static TaskHandle_t task1_asr_handle = NULL;
static void task1_asr(void *pvParameters);

// 任务2 LLM 配置
#define TASK2_LLM_STACK 8192
#define TASK2_LLM_PRIORITY 1
static TaskHandle_t task2_llm_handle = NULL;
static void task2_llm(void *pvParameters);

// 任务3 TTS 配置
#define TASK3_TTS_STACK 8192
#define TASK3_TTS_PRIORITY 1
static TaskHandle_t task3_tts_handle = NULL;
static void task3_tts(void *pvParameters);

void app_main(void)
{
    esp_err_t err = app_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init app: %s", esp_err_to_name(err));
        return;
    }

    xShareResourceMutex = xSemaphoreCreateMutex();
    xQueue_task1_to_task2 = xQueueCreate(3, sizeof (char*));
    xQueue_task2_to_task3 = xQueueCreate(3, sizeof (char*));
    xQueue_task3_to_task4 = xQueueCreate(3, sizeof (char*));
    xQueue_task4_to_task5 = xQueueCreate(3, sizeof (char*));
    if(xQueue_task1_to_task2 == NULL || xQueue_task2_to_task3 == NULL || xQueue_task3_to_task4 == NULL || xQueue_task4_to_task5 == NULL)
    {
        ESP_LOGE(TAG, "app_main: Failed to create Queue");
        return;
    }

    app_start_tasks();
}

static esp_err_t app_init(void)
{
    esp_err_t err = i2s_mic_init();
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
    err = asr_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init ASR: %s", esp_err_to_name(err));
        return err;
    }
    err = tts_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init TTS: %s", esp_err_to_name(err));
        return err;
    }
    err = llm_init();
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to init LLM: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static void app_start_tasks(void)
{
    BaseType_t ok = xTaskCreate(
        (TaskFunction_t)task1_asr,
        "task1_asr",
        TASK1_ASR_STACK,
        NULL,
        TASK1_ASR_PRIORITY,
        &task1_asr_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ASR task");
    }

    ok = xTaskCreate(
        (TaskFunction_t)task2_llm,
        "task2_llm",
        TASK2_LLM_STACK,
        NULL,
        TASK2_LLM_PRIORITY,
        &task2_llm_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LLM task");
    }

    ok = xTaskCreate(
        (TaskFunction_t)task3_tts,
        "task3_tts",
        TASK3_TTS_STACK,
        NULL,
        TASK3_TTS_PRIORITY,
        &task3_tts_handle
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS task");
    }
}

// ASR任务：循环执行语音识别，将识别结果通过队列发送给 TTS 任务。
static void task1_asr(void *pvParameters)
{
    char *asr_buffer = NULL;
    while (1) {
        xSemaphoreTake(xShareResourceMutex, portMAX_DELAY);
        ESP_LOGI(TAG, "Start to ASR");
        xSemaphoreGive(xShareResourceMutex);
        asr_buffer = asr_recognize();
        if(asr_buffer == NULL)
        {
            ESP_LOGE(TAG, "Task1: Failed to asr recognize");
        }
        else
        {
            ESP_LOGI(TAG, "Task1: Recognize result: %s", asr_buffer);
            if(xQueueSend(xQueue_task1_to_task2, &asr_buffer, pdMS_TO_TICKS(2000)) != pdPASS)
            {
                ESP_LOGE(TAG, "Task1: xQueueSend error");
                free(asr_buffer);
            }
        }
        
        ESP_LOGI(TAG, "Task1: Wait 0.5s before the next ASR");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// LLM任务：等待 ASR 结果，将 ASR 结果发送至 LLM 进行语义处理
static void task2_llm(void *pvParameters)
{
    char *asr_buffer = NULL;
    char *llm_buffer = NULL;
    esp_err_t err;
    while(1)
    {
        llm_buffer = NULL;
        if(xQueueReceive(xQueue_task1_to_task2, &asr_buffer, portMAX_DELAY) == pdPASS)
        {
            
            err = llm_chat(asr_buffer, &llm_buffer);
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Task2: llm_chat error: %s", esp_err_to_name(err));
                vPortFree(asr_buffer);
                asr_buffer = NULL;
                continue;
            }
            vPortFree(asr_buffer);
            asr_buffer = NULL;
            if(xQueueSend(xQueue_task2_to_task3, &llm_buffer, pdMS_TO_TICKS(1000)) != pdPASS)
            {
                ESP_LOGE(TAG, "Task2: xQueueSend error");
                free(llm_buffer);
            }
            
        }
    }
}

// TTS任务：阻塞等待队列中的识别文本，调用豆包 TTS 合成语音并播放，
static void task3_tts(void *pvParameters)
{
    char *llm_buffer = NULL;
    esp_err_t err;
    while(1)
    {
        ESP_LOGI(TAG, "Task3: Before TTS");
        if(xQueueReceive(xQueue_task2_to_task3, &llm_buffer, portMAX_DELAY) == pdPASS)
        {
            xSemaphoreTake(xShareResourceMutex, portMAX_DELAY);
            ESP_LOGI(TAG, "Task3: Start to TTS");
            err = tts_speak_text(llm_buffer);
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Task3: tts_speak_text error: %s", esp_err_to_name(err));
            }
            ESP_LOGI(TAG, "Task3: After TTS");
            vPortFree(llm_buffer);
            xSemaphoreGive(xShareResourceMutex);
        }
    }
}