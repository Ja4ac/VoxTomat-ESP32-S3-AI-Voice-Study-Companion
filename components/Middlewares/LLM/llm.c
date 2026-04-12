#include "llm.h"

#include "wifi.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define LLM_HTTP_TIMEOUT_MS    60000      // HTTP 建连、发送、接收的统一超时时间。

static const char *TAG = "LLM";
static bool s_llm_initialized = false;

static bool llm_is_configured(void)
{
    return (LLM_BASE_URL[0] != '\0') &&
           (LLM_API_KEY[0] != '\0') &&
           (LLM_SYSTEM_ROLE[0] != '\0');
}

esp_err_t llm_init(void)
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
    s_llm_initialized = true;
    return ESP_OK;
}

static esp_err_t llm_bulid_request_body(const char *text_in, char **out_body)
{
    cJSON *root;
    cJSON *message_arr;
    cJSON *message_sys;
    cJSON *message_user;
    char *body;

    root = cJSON_CreateObject();
    if(root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    message_arr = cJSON_CreateArray();
    if(message_arr == NULL)
    {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    message_sys = cJSON_CreateObject();
    if(message_sys == NULL)
    {
        cJSON_Delete(message_arr);
        cJSON_Delete(root);
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
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(message_user, "content", text_in);
    cJSON_AddStringToObject(message_user, "role", "user");
    cJSON_AddItemToArray(message_arr, message_user);

    cJSON_AddItemToObject(root, "messages", message_arr);

    cJSON_AddStringToObject(root, "model", "deepseek-chat");

    cJSON_AddFalseToObject(root, "stream");

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(body == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    *out_body = body;
    return ESP_OK;
}

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

esp_err_t llm_chat(const char *text_in, char **text_out)
{
    char *request_body = NULL;
    esp_err_t err;
    int content_length;
    int status_code;
    char *buffer = NULL;
    cJSON *root = NULL;
    cJSON *choices;
    cJSON *choices_item;
    cJSON *message;
    cJSON *content;

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

    err = llm_bulid_request_body(text_in, &request_body);
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
    char Authorization_value[256];
    snprintf(Authorization_value, sizeof(Authorization_value), "Bearer %s", LLM_API_KEY);

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_header(http_client, "Accept", "application/json");
    esp_http_client_set_header(http_client, "Authorization",  Authorization_value);

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
        goto cleanup;
    }

    root = cJSON_Parse(buffer);
    if(root == NULL)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
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

    *text_out = strdup(content -> valuestring);
    if(*text_out == NULL)
    {
        ESP_LOGE(TAG, "Failed to strdup text_out");
        err = ESP_ERR_NO_MEM;
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
        if(root != NULL)
        {
            cJSON_Delete(root);
        }
        return err;

}