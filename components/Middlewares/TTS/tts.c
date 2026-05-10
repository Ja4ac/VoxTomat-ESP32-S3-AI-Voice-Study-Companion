#include "tts.h"

#include "wifi.h"
#include "i2s.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "TTS";
static bool s_tts_initialized = false;
static volatile bool s_tts_is_play = false;
static volatile bool s_tts_stop_request = false;
static volatile bool s_tts_play_task_exited = false;
static StreamBufferHandle_t s_tts_stream_buf = NULL;

static StaticStreamBuffer_t s_tts_stream_buf_ctrl;
static uint8_t *s_tts_pcm_storage = NULL;

#define PCM_COPY_BUFFER_CAPACITY            (16000 * 2 * 3)      // 设置PCM缓冲区大小约为 3s 录制时长，单位：字节
#define PCM_COPY_BUFFER_START_CAPACITY      (16000 * 2 * 1)      // 在存储1秒音频后正式开启拷贝
#define PLAY_BUF_SIZE 4096

#define TTS_HTTP_TIMEOUT_MS    60000      // HTTP 建连、发送、接收的统一超时时间。
#define TTS_HTTP_READ_SIZE     4096       // 每次从 HTTP 流中读取的块大小。
#define TTS_EVENT_AUDIO        352        // SSE 事件：返回一段可播放音频。
#define TTS_EVENT_FINISH       152        // SSE 事件：整段合成完成。
#define TTS_EVENT_ERROR        153        // SSE 事件：服务端合成失败。
#define TTS_SUCCESS_CODE       20000000   // 官方成功码。
#define TTS_SOFT_GAIN_NUM      1          // 软件音量放大倍数分子，默认 2 倍增益。
#define TTS_SOFT_GAIN_DEN      1          // 软件音量放大倍数分母。

// 任务 TTS_PLAY 配置
#define TASK_TTS_PLAY_STACK 20480
#define TASK_TTS_PLAY_PRIORITY 2
static TaskHandle_t task_tts_play_handle = NULL;
static void task_tts_play(void *pvParameters);

// 定义 SSE 解析器
typedef struct 
{
    int current_event_id;                 // 当前正在组装的 SSE 事件 ID。
    char *event_data;                     // 当前事件对应的 data 字段文本。
    size_t event_data_len;                // 当前事件 data 的长度。
    char *line_buf;                       // 流式读取时正在拼接的一行文本。
    size_t line_len;                      // 当前行长度。
    bool finished;                        // 是否已经收到合成完成事件。
    esp_err_t error;                      // 解析或播放过程中记录的错误状态。
} tts_sse_parser_t;

// pcm拷贝器初始化
static esp_err_t tts_pcm_copy_init(void)
{
    if (s_tts_stream_buf == NULL)
    {
        ESP_LOGE(TAG, "tts_pcm_copybuf not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    s_tts_is_play = false;
    s_tts_stop_request = false;
    s_tts_play_task_exited = false;

    xStreamBufferReset(s_tts_stream_buf);
    return ESP_OK;
}

// 跳过字符串开头的空白字符，方便解析 event: 和 data: 后面的值
static const char *tts_skip_spaces(const char *text)
{
    while (text != NULL && *text != '\0' && isspace((unsigned char)*text)) 
    {
        text++;
    }
    return text;
}

// 把一段文本追加到动态字符串尾部，供 SSE 按行拼装数据使用
static esp_err_t tts_append_text(char **buffer, size_t *current_len, const char *data, size_t data_len)
{
    char *new_buffer = NULL;

    if (buffer == NULL || current_len == NULL || data == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    new_buffer = realloc(*buffer, *current_len + data_len + 1);
    if (new_buffer == NULL) 
    {
        return ESP_ERR_NO_MEM;
    }
 
    memcpy(new_buffer + *current_len, data, data_len);
    *current_len += data_len;
    new_buffer[*current_len] = '\0';

    *buffer = new_buffer;
    return ESP_OK;
}

// 清理当前已经组装好的一个 SSE 事件，准备接收下一个事件
static void tts_reset_current_event(tts_sse_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    if(parser->event_data != NULL)
    {
        free(parser->event_data);
        parser->event_data = NULL;
    }

    parser->current_event_id = 0;
    parser->event_data_len = 0;
}

// 释放整个 SSE 解析器内部占用的动态内存
static void tts_deinit_parser(tts_sse_parser_t *parser)
{
    if (parser == NULL) 
    {
        return;
    }

    tts_reset_current_event(parser);

    if(parser->line_buf != NULL)
    {
        free(parser->line_buf);
        parser->line_buf = NULL;
    }
    parser->line_len = 0;
}

// 生成请求 ID，便于服务端日志和本地日志对齐排查问题
static void tts_make_request_id(char out[40])
{
    snprintf(out, 40, "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
}

// 拷贝base64解析出的pcm数据到缓冲区
static esp_err_t tts_pcm_buffer_copy(uint8_t *pcm, size_t pcm_len)
{
    if(pcm == NULL || pcm_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    } 

    size_t sent = 0;
    while(sent < pcm_len)
    {
        size_t chunk = xStreamBufferSend(s_tts_stream_buf, pcm + sent,
                                          pcm_len - sent, pdMS_TO_TICKS(100));
        if(chunk == 0)
        {
            if(s_tts_stop_request) 
            { 
                free(pcm); 
                return ESP_FAIL; 
            }
            continue;
        }
        sent += chunk;
    }
    free(pcm);

    return ESP_OK;
}

// tts pcm播放任务
static void task_tts_play(void *pvParameters)
{
    uint8_t *play_buf = heap_caps_malloc(PLAY_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);     // 从内部RAM分配内存，按字节寻址
    if(play_buf == NULL)
    {
        ESP_LOGE(TAG, "Failed to malloc play buffer");
        s_tts_play_task_exited = true;
        task_tts_play_handle = NULL;
        vTaskDelete(NULL);
    }

    // 预缓冲：等待积累足够数据后才开始播放，避免开头卡顿
    while(xStreamBufferBytesAvailable(s_tts_stream_buf) < PCM_COPY_BUFFER_START_CAPACITY)
    {
        if(s_tts_stop_request)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while(1)
    {
        size_t received = xStreamBufferReceive(s_tts_stream_buf, play_buf, PLAY_BUF_SIZE, pdMS_TO_TICKS(20));
        if(received > 0)
        {
            size_t offset = 0;
            while(offset < received)
            {
                size_t bytes_written = 0;
                esp_err_t err = i2s_spk_write(play_buf + offset,
                                                received - offset, &bytes_written);
                if(err != ESP_OK || bytes_written == 0)
                {
                    ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
                    free(play_buf);
                    s_tts_play_task_exited = true;
                    task_tts_play_handle = NULL;
                    vTaskDelete(NULL);
                }
                offset += bytes_written;
            }
        }
        if(s_tts_stop_request && received == 0 && xStreamBufferBytesAvailable(s_tts_stream_buf) == 0)
        {
            break;
        }
    }
    free(play_buf);
    s_tts_play_task_exited = true;
    task_tts_play_handle = NULL;
    vTaskDelete(NULL);
}


// 对 16bit PCM 做带饱和保护的软件增益，避免放大后数值溢出
static void tts_apply_soft_gain(int16_t *pcm, size_t sample_count)
{
    if (pcm == NULL) {
        return;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        int32_t scaled = ((int32_t)pcm[i] * TTS_SOFT_GAIN_NUM) / TTS_SOFT_GAIN_DEN;

        if (scaled > INT16_MAX) {
            scaled = INT16_MAX;
        } else if (scaled < INT16_MIN) {
            scaled = INT16_MIN;
        }

        pcm[i] = (int16_t)scaled;
    }
}

// 把服务端返回的 base64 音频片段解码为 PCM，再立即送到功放播放
static esp_err_t tts_decode_and_play_audio(const char *base64_audio)
{
    unsigned char *pcm = NULL;
    size_t base64_len = 0;
    size_t pcm_len = 0;
    int ret = 0;
    esp_err_t err = ESP_OK;

    if (base64_audio == NULL || base64_audio[0] == '\0') 
    {
        return ESP_ERR_INVALID_ARG;
    }

    base64_len = strlen(base64_audio);
    pcm_len = (base64_len * 3) / 4 + 4;
    pcm = malloc(pcm_len);
    if (pcm == NULL) 
    {
        return ESP_ERR_NO_MEM;
    }

    // base64解码
    ret = mbedtls_base64_decode(pcm,
                                pcm_len,
                                &pcm_len,
                                (const unsigned char *)base64_audio,
                                base64_len);
    if (ret != 0) 
    {
        free(pcm);
        ESP_LOGE(TAG, "Failed to decode base64 audio: %d", ret);
        return ESP_FAIL;
    }

    // 云端返回的是 16bit PCM，播放前先做一次软件增益提升整体音量
    // tts_apply_soft_gain((int16_t *)pcm, pcm_len / sizeof(int16_t));

    return tts_pcm_buffer_copy(pcm, pcm_len);
}

// 处理单个 SSE 事件里的 JSON 数据：提取音频、判定结束或记录错误
static esp_err_t tts_handle_event_json(tts_sse_parser_t *parser)
{
    cJSON *root = NULL;
    cJSON *code = NULL;
    cJSON *message = NULL;
    cJSON *audio_data = NULL;
    int code_value = -1;
    esp_err_t err = ESP_OK;

    if (parser == NULL || parser->event_data == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    // 解析JSON
    root = cJSON_Parse(parser->event_data);
    if (root == NULL) 
    {
        ESP_LOGE(TAG, "Failed to parse SSE json payload");
        return ESP_ERR_INVALID_RESPONSE;
    }

    code = cJSON_GetObjectItem(root, "code");
    message = cJSON_GetObjectItem(root, "message");
    audio_data = cJSON_GetObjectItem(root, "data"); 

    // 判断返回的code是否正确
    if (code != NULL && cJSON_IsNumber(code)) 
    {
        code_value = code->valueint;
    }

    if (parser->current_event_id == TTS_EVENT_AUDIO) 
    {
        if (audio_data != NULL && cJSON_IsString(audio_data) && audio_data->valuestring[0] != '\0') 
        {
            err = tts_decode_and_play_audio(audio_data->valuestring);
        }
    } 
    else if (parser->current_event_id == TTS_EVENT_FINISH) 
    {
        parser->finished = true;
    } 
    else if (parser->current_event_id == TTS_EVENT_ERROR) 
    {
        ESP_LOGE(TAG,
                 "TTS server returned error event, message=%s",
                 (message != NULL && cJSON_IsString(message)) ? message->valuestring : "unknown");
        err = ESP_FAIL;
    }

    cJSON_Delete(root);
    return err;
}

// 处理一条完整的 SSE 消息，由空行作为消息结束标记
static esp_err_t tts_process_sse_message(tts_sse_parser_t *parser)
{
    esp_err_t err = ESP_OK;

    if (parser == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }
    // 如果接收到的消息为空就重置状态
    if (parser->event_data == NULL) 
    {
        tts_reset_current_event(parser);
        return ESP_OK;
    }

    err = tts_handle_event_json(parser);
    tts_reset_current_event(parser);
    return err;
}

// 处理一行 SSE 文本，识别 event:、data: 和空行三种关键内容
static esp_err_t tts_process_sse_line(tts_sse_parser_t *parser, char *line)
{
    const char *value = NULL;
    esp_err_t err = ESP_OK;

    if (parser == NULL || line == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (line[0] == '\0') 
    {
        return tts_process_sse_message(parser);
    }

    // 写入事件ID
    if (strncmp(line, "event:", 6) == 0) 
    {
        value = tts_skip_spaces(line + 6);
        parser->current_event_id = atoi(value);
        return ESP_OK;
    }

    // 拼接音频数据
    if (strncmp(line, "data:", 5) == 0) 
    {
        value = tts_skip_spaces(line + 5);
        err = tts_append_text(&parser->event_data, &parser->event_data_len, value, strlen(value));
        return err;
    }

    return ESP_OK;
}

// 把 HTTP 流中的字节持续喂给 SSE 解析器，直到拆出完整的 event/data 行
static esp_err_t tts_feed_sse_bytes(tts_sse_parser_t *parser, const char *data, size_t data_len)
{
    esp_err_t err = ESP_OK;

    if (parser == NULL || data == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < data_len; ++i) 
    {
        char ch = data[i];
        // \r\n标志一行结束了
        if (ch == '\n') 
        {
            if (parser->line_len > 0 && parser->line_buf[parser->line_len - 1] == '\r')
            {
                parser->line_buf[parser->line_len - 1] = '\0';
                parser->line_len--;
            } 
            else if (parser->line_buf != NULL)
            {
                parser->line_buf[parser->line_len] = '\0';
            }
            // 拼好了一行，交给行解析器处理
            err = tts_process_sse_line(parser, parser->line_buf != NULL ? parser->line_buf : "");
            free(parser->line_buf);
            parser->line_buf = NULL;
            parser->line_len = 0;
            if (err != ESP_OK) 
            {
                return err;
            }
            continue;
        }
        // 不是换行符就加入到行缓冲区
        err = tts_append_text(&parser->line_buf, &parser->line_len, &ch, 1);
        if (err != ESP_OK) 
        {
            return err;
        }
    }

    return ESP_OK;
}

// 构造豆包 TTS 请求 JSON，声明待合成文本、音色和输出音频参数
static esp_err_t tts_build_request_body(const char *text, char **body_out)
{
    cJSON *root = NULL;
    cJSON *user = NULL;
    cJSON *req_params = NULL;
    cJSON *audio_params = NULL;
    char *body = NULL;

    if (text == NULL || body_out == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) 
    {
        return ESP_ERR_NO_MEM;
    }
    user = cJSON_AddObjectToObject(root, "user");
    req_params = cJSON_AddObjectToObject(root, "req_params");
    audio_params = cJSON_AddObjectToObject(req_params, "audio_params");

    if (user == NULL || req_params == NULL || audio_params == NULL) 
    {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(user, "uid", "esp32s3-demo");

    cJSON_AddStringToObject(req_params, "text", text);
    cJSON_AddStringToObject(req_params, "speaker", TTS_SPEAKER);
    cJSON_AddStringToObject(audio_params, "format", TTS_AUDIO_FORMAT);
    cJSON_AddNumberToObject(audio_params, "sample_rate", TTS_SAMPLE_RATE);  
    cJSON_AddNumberToObject(audio_params, "speech_rate", TTS_SPEECH_RATE);
    cJSON_AddNumberToObject(audio_params, "loudness_rate", TTS_LOUDNESS_RATE);
    

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) 
    {
        return ESP_ERR_NO_MEM;
    }

    *body_out = body;
    return ESP_OK;
}

// 读取错误响应体，便于打印出服务端返回的错误详情
static void tts_log_error_response(esp_http_client_handle_t http_client)
{
    char buffer[256];
    int read_len = 0;

    memset(buffer, 0, sizeof(buffer));
    read_len = esp_http_client_read(http_client, buffer, sizeof(buffer) - 1);
    if (read_len > 0) 
    {
        buffer[read_len] = '\0';
        ESP_LOGE(TAG, "TTS error body: %s", buffer);
    }
}

// 检查 TTS 运行所需的鉴权参数和音色是否已经填写
bool tts_is_configured(void)
{
    return (TTS_APP_ID[0] != '\0') &&
           (TTS_ACCESS_KEY[0] != '\0') &&
           (TTS_RESOURCE_ID[0] != '\0') &&
           (TTS_SPEAKER[0] != '\0');
}

// 初始化 TTS 模块依赖：功放输出和 Wi-Fi 连接
esp_err_t tts_init(void)
{
    esp_err_t err = ESP_OK;

    if (s_tts_pcm_storage == NULL)
    {
        s_tts_pcm_storage = heap_caps_malloc(PCM_COPY_BUFFER_CAPACITY + 1,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_tts_pcm_storage == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate TTS PCM storage in PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_tts_stream_buf == NULL)
    {
        s_tts_stream_buf = xStreamBufferCreateStatic(
            PCM_COPY_BUFFER_CAPACITY,
            PCM_COPY_BUFFER_START_CAPACITY,
            s_tts_pcm_storage,
            &s_tts_stream_buf_ctrl);
        if (s_tts_stream_buf == NULL)
        {
            free(s_tts_pcm_storage);
            s_tts_pcm_storage = NULL;
            ESP_LOGE(TAG, "Failed to create static TTS stream buffer");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_tts_initialized) 
    {
        return ESP_OK;
    }

    if (!tts_is_configured()) 
    {
        ESP_LOGE(TAG, "Please fill TTS_APP_ID, TTS_ACCESS_KEY, TTS_RESOURCE_ID and TTS_SPEAKER first");
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

    s_tts_initialized = true;
    ESP_LOGI(TAG, "TTS init done");
    return ESP_OK;
}

// 把 LLM 提供的文本提交给豆包 TTS，并把返回音频实时播放到功放
esp_err_t tts_speak_text(const char *text)
{
    esp_http_client_handle_t http_client = NULL;
    tts_sse_parser_t parser = {0};
    char *request_body = NULL;
    char request_id[40];                            // 请求id
    char read_buffer[TTS_HTTP_READ_SIZE];           // 存放http返回的数据
    int status_code = 0;                            // http服务器返回的状态码
    int headers = 0;                                // http服务器返回的响应头长度
    esp_err_t err = ESP_OK;
    bool tts_play_task_created = false;             // 标记播放任务是否创建

    if (text == NULL || text[0] == '\0') 
    {
        ESP_LOGE(TAG, "LLM returned text is illegal");
        err =  ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    if (strlen(text) > TTS_MAX_TEXT_BYTES)          // LLM 返回的文本超过最大定义的文本长度
    {
        ESP_LOGE(TAG, "Input text is too long, max bytes: %d", TTS_MAX_TEXT_BYTES);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    err = tts_init();
    if (err != ESP_OK) 
    {
        goto cleanup;
    }

    tts_pcm_copy_init();

    esp_http_client_config_t http_client_config = 
    {
        .url = TTS_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TTS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 8192,
    };
    
    // 构造请求体
    err = tts_build_request_body(text, &request_body);
    if (err != ESP_OK) 
    {
        goto cleanup;
    }

    // 生成请求ID
    tts_make_request_id(request_id);

    http_client = esp_http_client_init(&http_client_config);
    if (http_client == NULL) 
    {
        err = ESP_FAIL;
        goto cleanup;
    }

    // 设置请求头
    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_header(http_client, "Accept", "text/event-stream");
    esp_http_client_set_header(http_client, "X-Api-App-Id", TTS_APP_ID);
    esp_http_client_set_header(http_client, "X-Api-Access-Key", TTS_ACCESS_KEY);
    esp_http_client_set_header(http_client, "X-Api-Resource-Id", TTS_RESOURCE_ID);
    esp_http_client_set_header(http_client, "X-Api-Request-Id", request_id);

    err = esp_http_client_open(http_client, strlen(request_body));
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open TTS HTTP connection: %s", esp_err_to_name(err));
        goto cleanup;
    }
    ESP_LOGI(TAG, "open TTS HTTP connection");

    if (esp_http_client_write(http_client, request_body, strlen(request_body)) < 0) 
    {
        ESP_LOGE(TAG, "Failed to write TTS request body");
        err = ESP_FAIL;
        goto cleanup;
    }
    ESP_LOGI(TAG, "write TTS request body");

    headers = esp_http_client_fetch_headers(http_client);
    status_code = esp_http_client_get_status_code(http_client);
    if (headers < 0) 
    {
        ESP_LOGW(TAG, "Failed to fetch TTS headers: %d", headers);
    }
    ESP_LOGI(TAG, "fetch TTS headers: %d", headers);
    if (status_code != 200) 
    {
        ESP_LOGE(TAG, "TTS request failed with status: %d", status_code);
        tts_log_error_response(http_client);
        err = ESP_FAIL;
        goto cleanup;
    }
    ESP_LOGI(TAG, "TTS request with status: %d", status_code);

    // 创建播放任务
    BaseType_t ok = xTaskCreate(
        (TaskFunction_t)task_tts_play,
        "task_tts_play",
        TASK_TTS_PLAY_STACK,
        NULL,
        TASK_TTS_PLAY_PRIORITY,
        &task_tts_play_handle
    );
    if(ok != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task_tts_play");
        err = ESP_FAIL;
        goto cleanup;
    }
    tts_play_task_created = true;           // 标志播放任务创建成功

    while (!parser.finished) 
    {
        int read_len = esp_http_client_read(http_client, read_buffer, sizeof(read_buffer));
        if (read_len == 0) 
        {
            break;
        }
        if (read_len == -ESP_ERR_HTTP_EAGAIN) 
        {
            continue;
        }
        if (read_len < 0) 
        {
            ESP_LOGE(TAG, "Failed to read TTS stream: %d", read_len);
            err = ESP_FAIL;
            goto cleanup;
        }

        err = tts_feed_sse_bytes(&parser, read_buffer, read_len);
        if (err != ESP_OK) 
        {   
            ESP_LOGE(TAG, "Failed to parse TTS stream: %s", esp_err_to_name(err));
            break;
        }
    }

    if (!parser.finished) 
    {
        ESP_LOGE(TAG, "TTS stream ended before finish event arrived");
        err = ESP_FAIL;
        goto cleanup;
    }

    goto cleanup;

cleanup:
    s_tts_stop_request = true;
    if(tts_play_task_created)
    {
        while(!s_tts_play_task_exited)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    tts_deinit_parser(&parser);
    
    if(request_body != NULL)
    {
        free(request_body);
        request_body = NULL;
    }
    if (http_client != NULL) 
    {
        esp_http_client_cleanup(http_client);
        http_client = NULL;
    }

    return err;
}
