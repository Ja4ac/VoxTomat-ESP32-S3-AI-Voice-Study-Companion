#include "asr.h"
#include "wifi.h"
#include "i2s.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"

static const char *TAG = "ASR";
static char *s_access_token = NULL;

// 从 HTTP 客户端读取完整响应体到动态分配的缓冲区
static esp_err_t asr_http_read(esp_http_client_handle_t http_client, char **buffer, int content_length)
{
    // content_length 为 0 时不一定没数据，可能只是服务端没带长度
    int buffer_size = (content_length > 0 ) ? content_length + 1 : 4096;
    *buffer = malloc(buffer_size);
    if(*buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while(total_read < buffer_size - 1)
    {
        int current_read = esp_http_client_read(http_client, *buffer + total_read, buffer_size -1 - total_read);
        if(current_read < 0)
        {
            free(*buffer);
            ESP_LOGE(TAG, "Failed to read http data");
            return ESP_FAIL;
        }
        else if(current_read == 0)
        {
            // read==0 表示已经读到响应末尾
            break;
        }
        total_read += current_read;
    }
    (*buffer)[total_read] = '\0';
    ESP_LOGI(TAG, "Read %d bytes: %s", total_read, *buffer);
    return ESP_OK;
}


// token 获取流程：发请求 -> 读响应 -> 解析 JSON -> 保存 access_token
static bool asr_get_baidu_access_token(void)
{
    bool ok = true;
    esp_err_t err;
    esp_http_client_handle_t http_client = NULL;
    char *new_token = NULL;             // 存放取得的新的access_token
    char url[512];                      // 拼接url
    int headers;                        // http服务器返回的响应头长度
    int status_code;                    // http服务器返回的状态码
    char *buffer = NULL;                // http服务器返回的数据
    cJSON *root = NULL;                 // 将http服务器返回的数据解析为JSON
    cJSON *token;                       // 存放新s_access_token
    cJSON *error;                       // 存放错误
    cJSON *error_desc;                  // 存放错误信息

    if(s_access_token != NULL)
    {
        // 当前写法是先删旧 token，再申请新 token
        free(s_access_token);
        s_access_token = NULL;
    }
    snprintf(url, sizeof(url), 
             "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             ASR_BAIDU_API_KEY, ASR_BAIDU_SECRET_KEY);
    ESP_LOGI(TAG, "Requesting Baidu token...");

    // HTTPS 请求要挂 CA 证书包，否则 TLS 校验会失败
    esp_http_client_config_t http_client_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 40000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };

    http_client = esp_http_client_init(&http_client_config);
    if(http_client == NULL)
    {
        ok = false;
        goto cleanup;
    }
    esp_http_client_set_header(http_client,
                               "Accept",
                               "application/json");

    err = esp_http_client_open(http_client, 0);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        ok = false;
        goto cleanup;
    }

    headers = esp_http_client_fetch_headers(http_client);
    status_code = esp_http_client_get_status_code(http_client);
    ESP_LOGI(TAG, "HTTP status: %d, content length: %d", status_code, headers);
    if(headers < 0)
    {
        ESP_LOGE(TAG, "Failed to get content length");
        ok = false;
        goto cleanup;
    }
    if(status_code != 200)
    {
        ESP_LOGE(TAG, "Token request failed with status: %d", status_code);
        ok = false;
        goto cleanup;
    }

    err = asr_http_read(http_client, &buffer, headers);
    esp_http_client_cleanup(http_client);
    http_client = NULL;
    if(err != ESP_OK)
    {
        ok = false;
        goto cleanup;
    }

    root = cJSON_Parse(buffer);
    if(root != NULL)
    {
        token = cJSON_GetObjectItem(root, "access_token");
        // 先放到临时指针里，确认成功后再转交给 access_token
        if(token != NULL && cJSON_IsString(token))
        {
            new_token = strdup(token -> valuestring);
            ESP_LOGI(TAG, "Got access token");
        }
        else
        {
            error = cJSON_GetObjectItem(root, "error");
            error_desc = cJSON_GetObjectItem(root, "error_description");
            ESP_LOGE(TAG, "API error: %s - %s",
                     error ? error -> valuestring : "unknown",
                     error_desc ? error_desc -> valuestring : "");
            ok = false;
            goto cleanup;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        ok = false;
        goto cleanup;
    }
    if(s_access_token != NULL)
    {
        free(s_access_token);
    }
    s_access_token = new_token;
    ok = true;
    goto cleanup;

    cleanup:
    if (root != NULL) 
    {
        cJSON_Delete(root);
    }
    if (buffer != NULL) 
    {
        free(buffer);
    }
    if (http_client != NULL) 
    {
        esp_http_client_cleanup(http_client);
    }
    return ok;
}

// 初始化ASR模块：连接wifi并获取百度语音识别access_token。
esp_err_t asr_init(void)
{
    esp_err_t err;
    err = wifi_sta_init();
    if(err != ESP_OK)
    {
        return err;
    }
    err = wifi_sta_connect(pdMS_TO_TICKS(15000));
    if(err != ESP_OK)
    {
        return err;
    }
    if(!asr_get_baidu_access_token())
    {
        ESP_LOGE(TAG, "Failed to init ASR reason: Failed to get Baidu access token");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ASR init successfully");
    return ESP_OK;
}

// 从MIC中读取固定时长音频存入audio_data
static esp_err_t asr_record_audio(int16_t *audio_data,int *audio_bytes)
{
    size_t total_samples = 0;
    int64_t start_time = esp_timer_get_time();
    size_t max_samples = ASR_SAMPLE_RATE * ASR_RECORD_SECONDS;
    // 连续读满 4 秒音频，或者缓冲区先满就结束
    ESP_LOGI(TAG, "开始进行 %d 秒的语音识别", ASR_RECORD_SECONDS);

    while((esp_timer_get_time() - start_time) < (ASR_RECORD_SECONDS * 1000000LL) && (total_samples < max_samples))
    {
        size_t samples_to_read = max_samples - total_samples;
        if(samples_to_read > 1024)
        {
            samples_to_read = 1024;
        }
        size_t samples = i2s_mic_read(audio_data + total_samples, samples_to_read);
        total_samples += samples;
    }
    *audio_bytes = total_samples * sizeof(int16_t);
    ESP_LOGI(TAG, "Record %d samples", total_samples);
    return (*audio_bytes > 0) ? ESP_OK : ESP_FAIL;
}

// 调用sr_event中的audio并通过百度语音识别API转为文本。
// 识别成功返回由malloc分配的字符串指针，调用者需 free()；
char *asr_recognize(int16_t *audio, size_t audio_bytes)
{
    if(audio == NULL || audio_bytes == 0)
    {
        return NULL;
    }
    esp_err_t err;
    esp_http_client_handle_t http_client = NULL;

    char url[512];                  // 拼接url
    int written;                    // 向http服务器一次写入的字节
    int total_written;              // 向http服务器总共写入的字节
    int content_length;             // http服务器发送的响应头长度
    int status_code;                // http服务器发送的状态码
    char *result = NULL;            // 语音识别结果
    int result_arr_size;            // 存储识别结果的数组大小
    char *response = NULL;          // http发送的数据
    cJSON *root = NULL;             // 将http发送的数据解析为JSON
    cJSON *err_no;                  // 是否有错误
    cJSON *err_msg;                 // 错误信息
    cJSON *result_arr;              // 存储识别结果的数组
    cJSON *result_context;          // 待处理的语音识别结果

    if(!wifi_sta_is_connected())
    {
        ESP_LOGE(TAG, "WIFI not connected");
        return  NULL;
    }
    if(s_access_token == NULL)
    {
        ESP_LOGE(TAG, "No access token available");
        return NULL;
    }

    // 百度接口要求把 token 和音频长度一起带进 URL
    snprintf(url, sizeof(url), 
             "https://vop.baidu.com/server_api?dev_pid=1537&cuid=esp32s3&token=%s&len=%d",
             s_access_token, audio_bytes);
    
    esp_http_client_config_t http_client_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };
    http_client = esp_http_client_init(&http_client_config);
    if(http_client == NULL)
    {
        err = ESP_FAIL;
        goto cleanup;
    }
    esp_http_client_set_header(http_client, "Content-Type", "audio/pcm; rate=16000");

    err = esp_http_client_open(http_client, audio_bytes);
    ESP_LOGI(TAG, "Sending RAW PCM audio: %d bytes...", audio_bytes);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        goto cleanup;
    }

    written = -1;
    total_written = 0;
    while(total_written < audio_bytes)
    {
        // HTTP body 可能一次写不完，所以这里按偏移循环写完
        written = esp_http_client_write(http_client, (char *)audio + total_written, audio_bytes - total_written);
        if(written == -1)
        {
            ESP_LOGE(TAG, "Failed to write audio data");
            err = ESP_FAIL;
            goto cleanup;
        }
        total_written += written;
    }
    ESP_LOGI(TAG, "Wrote %d bytes of audio data", total_written);

    content_length = esp_http_client_fetch_headers(http_client);
    status_code = esp_http_client_get_status_code(http_client);

    if(content_length < 0)
    {
        ESP_LOGE(TAG, "Failed to fetch headers");
        goto cleanup;
    }
    if(status_code != 200)
    {
        ESP_LOGE(TAG, "Recoginize request failed with status: %d", status_code);
        goto cleanup;
    }

    err = asr_http_read(http_client, &response, content_length);
    if(err != ESP_OK)
    {
        goto cleanup;
    }

    root = cJSON_Parse(response);
    if(root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse json");
        goto cleanup;
    }
    err_no = cJSON_GetObjectItem(root, "err_no");
    err_msg = cJSON_GetObjectItem(root, "err_msg");
    int err_no_value = -1;

    if (err_no != NULL && cJSON_IsNumber(err_no)) 
    {
        err_no_value = err_no->valueint;
    }
    if (err_no_value != 0)
    {
        ESP_LOGE(TAG, "Failed to get err_no, err_no: %d, err_msg: %s",
                err_no_value,
                err_msg ? err_msg->valuestring : "unknown");
        if (err_no_value == 110 || err_no_value == 111)
        {
            ESP_LOGW(TAG, "Access token expired, retrieve it again");
            asr_get_baidu_access_token();
        }
        goto cleanup;
    }
    result_arr = cJSON_GetObjectItem(root, "result");
    if(result_arr == NULL || !cJSON_IsArray(result_arr))
    {
        ESP_LOGE(TAG, "Failed to get result_arr");
        goto cleanup;
    }
    result_arr_size = cJSON_GetArraySize(result_arr);
    ESP_LOGI(TAG, "result array size: %d", result_arr_size);
    if(result_arr_size <= 0)
    {
        ESP_LOGE(TAG, "Result_arr size empty");
        goto cleanup;
    }
    // 百度返回的是字符串数组，这里只取第 0 个识别结果
    result_context = cJSON_GetArrayItem(result_arr, 0);
    if(result_context == NULL)
    {
        ESP_LOGE(TAG, "Failed to get array item");
        goto cleanup;
    }
    result = strdup(result_context -> valuestring);
    if(result == NULL)
    {
        ESP_LOGE(TAG, "Failed to strdup result");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Recognition: %s", result);
    cJSON_Delete(root);
    free(response);
    esp_http_client_cleanup(http_client);
    return result;

    cleanup:
        if(root != NULL)
        {
            cJSON_Delete(root);
        }
        if(response != NULL)
        {
            free(response);
        }
        if(http_client != NULL)
        {
            esp_http_client_cleanup(http_client);
        }
        return NULL;
}
