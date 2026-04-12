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

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "TTS";

#define TTS_HTTP_TIMEOUT_MS    60000      // HTTP 建连、发送、接收的统一超时时间。
#define TTS_HTTP_READ_SIZE     1024       // 每次从 HTTP 流中读取的块大小。
#define TTS_EVENT_AUDIO        352        // SSE 事件：返回一段可播放音频。
#define TTS_EVENT_FINISH       152        // SSE 事件：整段合成完成。
#define TTS_EVENT_ERROR        153        // SSE 事件：服务端合成失败。
#define TTS_SUCCESS_CODE       20000000   // 官方成功码。
#define TTS_SOFT_GAIN_NUM      2          // 软件音量放大倍数分子，默认 2 倍增益。
#define TTS_SOFT_GAIN_DEN      1          // 软件音量放大倍数分母。

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

static bool s_tts_initialized = false;

// 跳过字符串开头的空白字符，方便解析 event: 和 data: 后面的值。
static const char *tts_skip_spaces(const char *text)
{
    while (text != NULL && *text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

// 把一段文本追加到动态字符串尾部，供 SSE 按行拼装数据使用。
static esp_err_t tts_append_text(char **buffer, size_t *current_len, const char *data, size_t data_len)
{
    char *new_buffer = NULL;

    if (buffer == NULL || current_len == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    new_buffer = realloc(*buffer, *current_len + data_len + 1);
    if (new_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
 
    memcpy(new_buffer + *current_len, data, data_len);
    *current_len += data_len;
    new_buffer[*current_len] = '\0';

    *buffer = new_buffer;
    return ESP_OK;
}

// 清理当前已经组装好的一个 SSE 事件，准备接收下一个事件。
static void tts_reset_current_event(tts_sse_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    parser->current_event_id = 0;
    free(parser->event_data);
    parser->event_data = NULL;
    parser->event_data_len = 0;
}

// 释放整个 SSE 解析器内部占用的动态内存。
static void tts_deinit_parser(tts_sse_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    tts_reset_current_event(parser);
    free(parser->line_buf);
    parser->line_buf = NULL;
    parser->line_len = 0;
}

// 生成请求 ID，便于服务端日志和本地日志对齐排查问题。
static void tts_make_request_id(char out[40])
{
    snprintf(out, 40, "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
}

// 判断返回 code 是否属于成功，兼容部分接口返回 0 的情况。
static bool tts_is_success_code(int code)
{
    return (code == 0) || (code == TTS_SUCCESS_CODE);
}

// 把一整段 PCM 数据完整写到 I2S 功放，避免部分写入丢音频。
static esp_err_t tts_write_pcm_all(const uint8_t *pcm, size_t pcm_len)
{
    size_t total_written = 0;

    while (total_written < pcm_len) {
        size_t bytes_written = 0;
        esp_err_t err = i2s_spk_write((uint8_t *)pcm + total_written,
                                      pcm_len - total_written,
                                      &bytes_written);
        if (err != ESP_OK) {
            return err;
        }
        if (bytes_written == 0) {
            return ESP_FAIL;
        }
        total_written += bytes_written;
    }

    return ESP_OK;
}

// 对 16bit PCM 做带饱和保护的软件增益，避免放大后数值溢出。
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

// 把服务端返回的 base64 音频片段解码为 PCM，再立即送到功放播放。
static esp_err_t tts_decode_and_play_audio(const char *base64_audio)
{
    unsigned char *pcm = NULL;
    size_t base64_len = 0;
    size_t pcm_len = 0;
    int ret = 0;
    esp_err_t err = ESP_OK;

    if (base64_audio == NULL || base64_audio[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    base64_len = strlen(base64_audio);
    pcm_len = (base64_len * 3) / 4 + 4;
    pcm = malloc(pcm_len);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // base64解码
    ret = mbedtls_base64_decode(pcm,
                                pcm_len,
                                &pcm_len,
                                (const unsigned char *)base64_audio,
                                base64_len);
    if (ret != 0) {
        free(pcm);
        ESP_LOGE(TAG, "Failed to decode base64 audio: %d", ret);
        return ESP_FAIL;
    }

    // 云端返回的是 16bit PCM，播放前先做一次软件增益提升整体音量。
    tts_apply_soft_gain((int16_t *)pcm, pcm_len / sizeof(int16_t));

    err = tts_write_pcm_all(pcm, pcm_len);
    free(pcm);
    return err;
}

// 处理单个 SSE 事件里的 JSON 数据：提取音频、判定结束或记录错误。
static esp_err_t tts_handle_event_json(tts_sse_parser_t *parser)
{
    cJSON *root = NULL;
    cJSON *code = NULL;
    cJSON *message = NULL;
    cJSON *audio_data = NULL;
    int code_value = -1;
    esp_err_t err = ESP_OK;

    if (parser == NULL || parser->event_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 解析JSON
    root = cJSON_Parse(parser->event_data);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse SSE json payload");
        return ESP_ERR_INVALID_RESPONSE;
    }

    code = cJSON_GetObjectItem(root, "code");
    message = cJSON_GetObjectItem(root, "message");
    audio_data = cJSON_GetObjectItem(root, "data"); 

    // 判断返回的code是否正确
    if (code != NULL && cJSON_IsNumber(code)) {
        code_value = code->valueint;
    }
    if (!tts_is_success_code(code_value)) {
        ESP_LOGE(TAG,
                 "TTS request failed, event=%d, code=%d, message=%s",
                 parser->current_event_id,
                 code_value,
                 (message != NULL && cJSON_IsString(message)) ? message->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
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

// 处理一条完整的 SSE 消息，由空行作为消息结束标记。
static esp_err_t tts_process_sse_message(tts_sse_parser_t *parser)
{
    esp_err_t err = ESP_OK;

    if (parser == NULL) 
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (parser->event_data == NULL) 
    {
        tts_reset_current_event(parser);
        return ESP_OK;
    }

    err = tts_handle_event_json(parser);
    tts_reset_current_event(parser);
    return err;
}

// 处理一行 SSE 文本，识别 event:、data: 和空行三种关键内容。
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

    if (strncmp(line, "event:", 6) == 0) 
    {
        value = tts_skip_spaces(line + 6);
        parser->current_event_id = atoi(value);
        return ESP_OK;
    }

    if (strncmp(line, "data:", 5) == 0) 
    {
        value = tts_skip_spaces(line + 5);
        err = tts_append_text(&parser->event_data, &parser->event_data_len, value, strlen(value));
        return err;
    }

    return ESP_OK;
}

// 把 HTTP 流中的字节持续喂给 SSE 解析器，直到拆出完整的 event/data 行。
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
        // 一行结束了
        if (ch == '\n') {
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
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

// 构造豆包 TTS 请求 JSON，声明待合成文本、音色和输出音频参数。
static esp_err_t tts_build_request_body(const char *text, char **body_out)
{
    cJSON *root = NULL;
    cJSON *user = NULL;
    cJSON *req_params = NULL;
    cJSON *audio_params = NULL;
    char *body = NULL;

    if (text == NULL || body_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    user = cJSON_AddObjectToObject(root, "user");
    req_params = cJSON_AddObjectToObject(root, "req_params");
    audio_params = cJSON_AddObjectToObject(req_params, "audio_params");
    if (user == NULL || req_params == NULL || audio_params == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(user, "uid", "esp32s3-demo");

    cJSON_AddStringToObject(req_params, "text", text);
    cJSON_AddStringToObject(req_params, "speaker", TTS_SPEAKER);
    cJSON_AddStringToObject(audio_params, "format", TTS_AUDIO_FORMAT);
    cJSON_AddNumberToObject(audio_params, "sample_rate", TTS_SAMPLE_RATE);

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *body_out = body;
    return ESP_OK;
}

// 读取错误响应体，便于打印出服务端返回的错误详情。
static void tts_log_error_response(esp_http_client_handle_t client)
{
    char buffer[256];
    int read_len = 0;

    memset(buffer, 0, sizeof(buffer));
    read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
    if (read_len > 0) {
        buffer[read_len] = '\0';
        ESP_LOGE(TAG, "TTS error body: %s", buffer);
    }
}

// 检查 TTS 运行所需的鉴权参数和音色是否已经填写。
bool tts_is_configured(void)
{
    return (TTS_APP_ID[0] != '\0') &&
           (TTS_ACCESS_KEY[0] != '\0') &&
           (TTS_RESOURCE_ID[0] != '\0') &&
           (TTS_SPEAKER[0] != '\0');
}

// 初始化 TTS 模块依赖：功放输出和 Wi-Fi 连接。
esp_err_t tts_init(void)
{
    esp_err_t err = ESP_OK;

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

// 把调用方提供的文本提交给豆包 TTS，并把返回音频实时播放到功放。
esp_err_t tts_speak_text(const char *text)
{
    esp_http_client_config_t http_cfg = 
    {
        .url = TTS_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TTS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = NULL;
    tts_sse_parser_t parser = {0};
    char *request_body = NULL;
    char request_id[40];
    char read_buffer[TTS_HTTP_READ_SIZE];
    int status_code = 0;
    int headers = 0;
    esp_err_t err = ESP_OK;

    if (text == NULL || text[0] == '\0') 
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(text) > TTS_MAX_TEXT_BYTES) 
    {
        ESP_LOGE(TAG, "Input text is too long, max bytes: %d", TTS_MAX_TEXT_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    err = tts_init();
    if (err != ESP_OK) 
    {
        return err;
    }

    err = tts_build_request_body(text, &request_body);
    if (err != ESP_OK) 
    {
        return err;
    }

    tts_make_request_id(request_id);

    client = esp_http_client_init(&http_cfg);
    if (client == NULL) 
    {
        cJSON_free(request_body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "text/event-stream");
    esp_http_client_set_header(client, "X-Api-App-Id", TTS_APP_ID);
    esp_http_client_set_header(client, "X-Api-Access-Key", TTS_ACCESS_KEY);
    esp_http_client_set_header(client, "X-Api-Resource-Id", TTS_RESOURCE_ID);
    esp_http_client_set_header(client, "X-Api-Request-Id", request_id);

    err = esp_http_client_open(client, strlen(request_body));
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open TTS HTTP connection: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if (esp_http_client_write(client, request_body, strlen(request_body)) < 0) 
    {
        ESP_LOGE(TAG, "Failed to write TTS request body");
        err = ESP_FAIL;
        goto cleanup;
    }

    headers = esp_http_client_fetch_headers(client);
    status_code = esp_http_client_get_status_code(client);
    if (headers < 0) 
    {
        ESP_LOGW(TAG, "Failed to fetch TTS headers: %d", headers);
    }
    if (status_code != 200) 
    {
        ESP_LOGE(TAG, "TTS request failed with status: %d", status_code);
        tts_log_error_response(client);
        err = ESP_FAIL;
        goto cleanup;
    }

    while (!parser.finished) 
    {
        int read_len = esp_http_client_read(client, read_buffer, sizeof(read_buffer));
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
            goto cleanup;
        }
    }

    if (!parser.finished) 
    {
        ESP_LOGE(TAG, "TTS stream ended before finish event arrived");
        err = ESP_FAIL;
    }

cleanup:
    tts_deinit_parser(&parser);
    cJSON_free(request_body);
    if (client != NULL) 
    {
        esp_http_client_cleanup(client);
    }
    return err;
}
