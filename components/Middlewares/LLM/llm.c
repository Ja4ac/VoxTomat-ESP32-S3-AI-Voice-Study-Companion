#include "llm.h"

#include "wifi.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "app.h"
#include "schedule.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define LLM_HTTP_TIMEOUT_MS    60000      // HTTP 建连、发送、接收的统一超时时间。

static const char *TAG = "LLM";
static bool s_llm_initialized = false;

static QueueHandle_t s_queue_schedule_changed = NULL;

static bool llm_is_configured(void)
{
    return (LLM_BASE_URL[0] != '\0') &&
           (LLM_API_KEY[0] != '\0') &&
           (LLM_SYSTEM_ROLE[0] != '\0');
}

esp_err_t llm_init(QueueHandle_t queue_schedule_changed)
{
    esp_err_t err = ESP_OK;
    if(s_llm_initialized)
    {
        ESP_LOGI(TAG, "llm is initialized");
        return err;
    }

    if(!llm_is_configured())
    {
        ESP_LOGE(TAG, "Please fill LLM_BASE_URL, LLM_API_KEY and LLM_SYSTEM_ROLE first");
        return ESP_ERR_INVALID_STATE;
    }
    if(!wifi_sta_is_initialized())
    {
        err = wifi_sta_init();
        if (err != ESP_OK)
        {
         ESP_LOGE(TAG, "Failed to init Wi-Fi: %s", esp_err_to_name(err));
         return err;
        }
    }
    
    if(!wifi_sta_is_connected())
    {
        err = wifi_sta_connect(pdMS_TO_TICKS(15000));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to connect Wi-Fi: %s", esp_err_to_name(err));
            return err;
        }
    }

    if(queue_schedule_changed != NULL)
    {
        s_queue_schedule_changed = queue_schedule_changed;
    }
    ESP_LOGI(TAG, "LLM init done");
    s_llm_initialized = true;
    return ESP_OK;
}

/** 构建请求体 期望的JSON格式
* {
*   "model": "deepseek-chat",
*   "stream": false,
*   "max_tokens": 512,
*   "temperature": 0.1,
*   "response_format": {
*     "type": "json_object"
*   },
*   "messages": [
*     {
*       "role": "system",
*       "content": "你是智能学习终端的指令解析器......"
*     },
*     {
*       "role": "user",
*       "content": "current_time=2026-04-22 20:30; timezone=Asia/Shanghai; user_text=明天晚上八点提醒我开会"
*     }
*   ]
* }
*/  
static esp_err_t llm_build_request_body(const char *text_in, char **out_body)
{
    cJSON *root;
    cJSON *response_format;
    cJSON *message_arr;
    cJSON *message_sys;
    cJSON *message_user;
    char *body;

    root = cJSON_CreateObject();
    if(root == NULL)
    {
        ESP_LOGE(TAG, "Failed to build cJSON Object: NO MEM");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "model", "deepseek-chat");
    cJSON_AddFalseToObject(root, "stream");
    cJSON_AddNumberToObject(root, "max_tokens", 512);
    cJSON_AddNumberToObject(root, "temperature", 0.1);

    response_format = cJSON_CreateObject();
    if(response_format == NULL)
    {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to build cJSON Object: NO MEM");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(response_format, "type", "json_object");
    cJSON_AddItemToObject(root, "response_format", response_format);

    message_arr = cJSON_CreateArray();
    if(message_arr == NULL)
    {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to build cJSON Object: NO MEM");
        return ESP_ERR_NO_MEM;
    }

    message_sys = cJSON_CreateObject();
    if(message_sys == NULL)
    {
        cJSON_Delete(message_arr);
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to build cJSON Object: NO MEM");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(message_sys, "content", LLM_SYSTEM_ROLE);
    cJSON_AddStringToObject(message_sys, "role", "system");
    cJSON_AddItemToArray(message_arr, message_sys);

    message_user = cJSON_CreateObject();
    if(message_user == NULL)
    {
        cJSON_Delete(message_arr);
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to build cJSON Object: NO MEM");
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(message_user, "content", text_in);
    cJSON_AddStringToObject(message_user, "role", "user");
    cJSON_AddItemToArray(message_arr, message_user);

    cJSON_AddItemToObject(root, "messages", message_arr);

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(body == NULL)
    {
        ESP_LOGE(TAG, "Failed to build request body: NO MEM");
        return ESP_ERR_NO_MEM;
    }

    *out_body = body;
    return ESP_OK;
}

// 处理命令请求
static esp_err_t llm_handle_event_commands(cJSON *commands)
{
    esp_err_t err;
    uint8_t cmd_num = cJSON_GetArraySize(commands);
    for(uint8_t i = 0; i < cmd_num; i++)
    {
        cJSON *command = cJSON_GetArrayItem(commands, i);
        if(!cJSON_IsObject(command))
        {
            return ESP_ERR_INVALID_ARG;
        }
        cJSON *name = cJSON_GetObjectItem(command, "name");
        if(!cJSON_IsString(name))
        {
            return ESP_ERR_INVALID_ARG;
        }
        cJSON *args = cJSON_GetObjectItem(command, "args");
        if(!cJSON_IsObject(args))
        {
            return ESP_ERR_INVALID_ARG;
        }
        // 解析命令
        if(strcmp(name -> valuestring, "pomodoro.start") == 0)
        {
            cJSON *duration = cJSON_GetObjectItem(args, "duration_minutes");
            if(cJSON_IsNumber(duration) && duration->valueint > 0 && duration->valueint <= 180) 
            {
                ESP_LOGI(TAG, "cmd: pomodoro.start, duration=%d", duration->valueint);
            }
            else
            {
                err = ESP_ERR_INVALID_RESPONSE;
                return err;
            }
            err = app_pomodoro_start_async(duration->valueint);
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to execute command: pomodoro.stop");
                return err;
            }
        }
        else if(strcmp(name -> valuestring, "pomodoro.pause") == 0)
        {
            ESP_LOGI(TAG, "cmd: pomodoro.pause");
            err = app_pomodoro_pause_async();
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to execute command: pomodoro.stop");
                return err;
            }
        }
        else if(strcmp(name -> valuestring, "pomodoro.stop") == 0)
        {
            ESP_LOGI(TAG, "cmd: pomodoro.stop");
            err = app_pomodoro_stop_async();
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to execute command: pomodoro.stop");
                return err;
            }
        }
        else if(strcmp(name -> valuestring, "pomodoro.resume") == 0)
        {
            ESP_LOGI(TAG, "cmd: pomodoro.resume");
            err = app_pomodoro_resume_async();
            if(err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to execute command: pomodoro.stop");
                return err;
            }
        }
        else if(strcmp(name -> valuestring, "schedule.create") == 0)
        {
            cJSON *datetime = cJSON_GetObjectItem(args, "datetime");
            cJSON *content_item = cJSON_GetObjectItem(args, "content");
            if(cJSON_IsString(datetime) && cJSON_IsString(content_item)) 
            {
                int y, m, d, h, min;
                if(sscanf(datetime->valuestring, "%d-%d-%d %d:%d", &y, &m, &d, &h, &min) == 5)
                {
                    err = schedule_add(y, m, d, h, min, content_item->valuestring);
                    if(err == ESP_OK && s_queue_schedule_changed != NULL)
                    {
                        uint8_t changed = 1;
                        xQueueSend(s_queue_schedule_changed, &changed, pdMS_TO_TICKS(100));
                    }
                }
                else
                {
                    return ESP_ERR_INVALID_RESPONSE;
                }
                ESP_LOGI(TAG, "cmd: schedule.create, datetime=%s, content=%s",
                        datetime->valuestring, content_item->valuestring);
            }
        }
        else if(strcmp(name -> valuestring, "schedule.delete") == 0)
        {
            cJSON *index = cJSON_GetObjectItem(args, "index");
            if(cJSON_IsNumber(index)) 
            {
                err = schedule_delete(index->valueint);
                if(err == ESP_OK && s_queue_schedule_changed != NULL)
                {
                    uint8_t changed = 1;
                    xQueueSend(s_queue_schedule_changed, &changed, pdMS_TO_TICKS(100));
                }
                ESP_LOGI(TAG, "cmd: schedule.delete, index=%d", index->valueint);
            }
            else
            {
                return ESP_ERR_INVALID_RESPONSE;    
            }
        }
    }
    return ESP_OK;
}

// 处理对话请求
static esp_err_t llm_handle_event_chat(cJSON *reply_text, char **text_out)
{    
    *text_out = strdup(reply_text -> valuestring);
    if(*text_out == NULL)
    {
        ESP_LOGE(TAG, "Failed to strdup text_out");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/** 处理 LLM 返回的内容 期望的JSON格式
*    {
*    "version": "1.0",
*    "message_type": "mixed",
*    "reply_text": "好的，已开始番茄钟。先专注二十五分钟。",
*    "commands": [
*        {
*        "name": "pomodoro.start",
*        "args": {
*            "duration_minutes": 25
*        }
*        }
*    ],
*    "need_confirm": false,
*    "confidence": 0.95
*    }
*/
static esp_err_t llm_handle_event_json(const char *buffer, char **text_out)
{
    esp_err_t err = ESP_OK;
    cJSON *root = NULL;
    cJSON *choices = NULL;
    cJSON *choices_item = NULL;
    cJSON *message = NULL;
    cJSON *content = NULL;

    cJSON *content_root = NULL;
    cJSON *message_type = NULL;         // 用户请求
    cJSON *reply_text = NULL;           // 对话文本
    cJSON *commands = NULL;             // 命令
    cJSON *need_confirm = NULL;
    cJSON *confidence = NULL;

    if(buffer == NULL || text_out == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    // 解析 DeepSeek 返回的 JSON
    root = cJSON_Parse(buffer);
    if(root == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    char *body = cJSON_PrintUnformatted(root);
    if(body != NULL)
    {
        ESP_LOGI(TAG, "========== 完整 LLM 响应 JSON ==========");
        ESP_LOGI(TAG, "%s", body); // 直接打印整个JSON字符串
        ESP_LOGI(TAG, "=======================================");
    }
    else
    {
        ESP_LOGE(TAG, "JSON 打印失败，内存不足");
    }

    choices = cJSON_GetObjectItem(root , "choices");
    if(choices == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    choices_item = cJSON_GetArrayItem(choices, 0);
    if(choices_item == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    message = cJSON_GetObjectItem(choices_item, "message");
    if(message == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    content = cJSON_GetObjectItem(message, "content");
    if(content == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    // 解析业务逻辑
    content_root = cJSON_Parse(content -> valuestring);
    cJSON_Delete(root);
    root = NULL;
    if(content_root == NULL)
    {
        err = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Failed to parse content_root: NO MEM");
        goto cleanup;
    }

    message_type = cJSON_GetObjectItem(content_root, "message_type");
    reply_text = cJSON_GetObjectItem(content_root, "reply_text");
    commands = cJSON_GetObjectItem(content_root, "commands");
    need_confirm = cJSON_GetObjectItem(content_root, "need_confirm");
    confidence = cJSON_GetObjectItem(content_root, "confidence");

    if(!cJSON_IsString(message_type) || !cJSON_IsString(reply_text) || !cJSON_IsArray(commands) || !cJSON_IsBool(need_confirm))
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    if(cJSON_IsTrue(need_confirm))
    {
        ESP_LOGW(TAG, "LLM returned need confirm");
        err = llm_handle_event_chat(reply_text, text_out);
        goto cleanup;
    }
    // message_type:command、chat、mixed、unknown
    else if(strcmp(message_type -> valuestring, "command") == 0)
    {
        ESP_LOGW(TAG, "LLM returned message_type: command");
        err = llm_handle_event_commands(commands);
        if(err != ESP_OK)
        {
            goto cleanup;
        }
        err = llm_handle_event_chat(reply_text, text_out);
        goto cleanup;
    }
    else if((strcmp(message_type -> valuestring, "chat")) == 0)
    {
        ESP_LOGW(TAG, "LLM returned message_type: chat");
        err = llm_handle_event_chat(reply_text, text_out);
        goto cleanup;
    }
    else if((strcmp(message_type -> valuestring, "mixed")) == 0)
    {
        ESP_LOGW(TAG, "LLM returned message_type: mixed");
        err = llm_handle_event_commands(commands);
        if(err != ESP_OK)
        {
            goto cleanup;
        }
        err = llm_handle_event_chat(reply_text, text_out);
        goto cleanup;
    }
    else if((strcmp(message_type -> valuestring, "unknown")) == 0)
    {
        ESP_LOGW(TAG, "LLM returned message_type: unknown");
        err = llm_handle_event_chat(reply_text, text_out);
        goto cleanup;
    }
    
    cleanup:
        if(content_root != NULL)
        {
            cJSON_Delete(content_root);
        }
        if(root != NULL)
        {
            cJSON_Delete(root);
        }
        return err;
}

// 从 http 中读取数据
static esp_err_t llm_http_read(esp_http_client_handle_t http_client, char **buffer, int content_length)
{
    // content_length 为 0 时不一定没数据，可能只是服务端没带长度
    int capacity = content_length > 0 ? content_length + 1 : 1024;
    *buffer = malloc(capacity);

    if(*buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int realloc_times = 0;
    while(1)
    {
        if(capacity - total_read <= 1)
        {
            int new_capacity = capacity * 2;
            char *new_buffer = realloc(*buffer, new_capacity);
            if(new_buffer == NULL)
            {
                if(++realloc_times == 3)
                {
                    ESP_LOGE(TAG, "Failed to realloc http read buffer, over");
                    return ESP_ERR_NO_MEM;
                }
                ESP_LOGW(TAG, "Failed to realloc http read buffer, again");
                continue;
            }
            *buffer = new_buffer;
            capacity = new_capacity;
        }
        int current_read = esp_http_client_read(http_client, *buffer + total_read, capacity -total_read - 1);
        if(current_read == -1)
        {
            ESP_LOGE(TAG, "Failed to read http");
            return ESP_FAIL;
        }
        if(current_read == 0)
        {
            break;
        }
        total_read += current_read;
    }
    (*buffer)[total_read] = '\0';
    ESP_LOGI(TAG, "Read %d bytes: %s", total_read, *buffer);
    return ESP_OK;
}

// 读取错误响应体，便于打印出服务端返回的错误详情。
static void llm_log_error_response(esp_http_client_handle_t client)
{
    char buffer[256];
    int read_len = 0;

    memset(buffer, 0, sizeof(buffer));
    read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
    if (read_len > 0) {
        buffer[read_len] = '\0';
        ESP_LOGE(TAG, "LLM error body: %s", buffer);
    }
}

// 构建对话请求
esp_err_t llm_chat(const char *text_in, char **text_out)
{
    char *request_body = NULL;
    esp_err_t err;
    int content_length;
    int status_code;
    char *buffer = NULL;

    if(text_in == NULL)
    {
        return ESP_FAIL;
    }

    if(text_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *text_out = NULL;

    esp_http_client_config_t http_client_config = {
        .url = LLM_BASE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = LLM_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };

    err = llm_build_request_body(text_in, &request_body);
    if(err != ESP_OK)
    {
        return err;
    }

    esp_http_client_handle_t http_client = esp_http_client_init(&http_client_config);
    if(http_client == NULL)
    {
        err =  ESP_FAIL;
        goto cleanup;
    }
    char authorization_value[256];
    snprintf(authorization_value, sizeof(authorization_value), "Bearer %s", LLM_API_KEY);

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_header(http_client, "Accept", "application/json");
    esp_http_client_set_header(http_client, "Authorization",  authorization_value);

    err = esp_http_client_open(http_client, strlen(request_body));
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open LLM HTTP connection: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if(esp_http_client_write(http_client, request_body, strlen(request_body)) < 0 )
    {
        err = ESP_FAIL;
        ESP_LOGE(TAG, "Failed to write LLM request body");
        goto cleanup;
    }

    content_length = esp_http_client_fetch_headers(http_client);
    status_code = esp_http_client_get_status_code(http_client);

    if (content_length < 0) 
    {
        ESP_LOGW(TAG, "Failed to fetch LLM headers: %d", content_length);
        err = ESP_FAIL;
        goto cleanup;
    }
    if (status_code != 200) 
    {
        ESP_LOGE(TAG, "LLM request failed with status: %d", status_code);
        llm_log_error_response(http_client);
        err = ESP_FAIL;
        goto cleanup;
    }

    err = llm_http_read(http_client, &buffer, content_length);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read http");
        goto cleanup;
    }

    err = llm_handle_event_json(buffer, text_out);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to handle event json");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Get LLM content success");
    goto cleanup;

    cleanup:
        if(request_body != NULL)
        {
            cJSON_free(request_body);
        }
        if(http_client != NULL)
        {
            esp_http_client_cleanup(http_client);
        }
        if(buffer != NULL)
        {
            free(buffer);
            buffer = NULL;
        }
        return err;
}