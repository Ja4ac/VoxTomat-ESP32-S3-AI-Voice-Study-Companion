#ifndef TTS_H_
#define TTS_H_

#include "esp_err.h"
#include "project_secrets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

/**
 * TTS 模块职责：
 * 1. 调用方先通过 LLM 拿到待播报文本。
 * 2. 调用 tts_speak_text() 把文本上传到豆包语音合成模型。
 * 3. 云端按流式方式返回 base64 音频片段。
 * 4. 模块把音频片段解码成 PCM，并立即写入 I2S 功放输出。
 * 5. 函数在整段语音播放完成后返回。
 *
 * 使用前请先填写以下鉴权与音色配置：
 * - TTS_APP_ID：火山引擎应用 ID
 * - TTS_ACCESS_KEY：火山引擎 Access Key
 * - TTS_RESOURCE_ID：语音合成模型资源 ID，豆包语音合成 2.0 使用 seed-tts-2.0
 * - TTS_SPEAKER：已开通并可用的音色 ID
 */
#define TTS_APP_ID            PROJECT_TTS_APP_ID
#define TTS_ACCESS_KEY        PROJECT_TTS_ACCESS_KEY
#define TTS_RESOURCE_ID       PROJECT_TTS_RESOURCE_ID
#define TTS_API_URL           "https://openspeech.bytedance.com/api/v3/tts/unidirectional/sse"
#define TTS_SPEAKER           PROJECT_TTS_SPEAKER

#define TTS_AUDIO_FORMAT      "pcm"
#define TTS_SAMPLE_RATE       16000
#define TTS_CHANNELS          1
#define TTS_BITS_PER_SAMPLE   16
#define TTS_MAX_TEXT_BYTES    1024
#define TTS_SPEECH_RATE       20
#define TTS_LOUDNESS_RATE     -10



esp_err_t tts_init(void);
bool tts_is_configured(void);
esp_err_t tts_speak_text(const char *text);

// static esp_err_t tts_pcm_copy_init(void);// pcm拷贝器初始化
// static const char *tts_skip_spaces(const char *text);// 跳过字符串开头的空白字符，方便解析 event: 和 data: 后面的值
// static esp_err_t tts_append_text(char **buffer, size_t *current_len, const char *data, size_t data_len);// 把一段文本追加到动态字符串尾部，供 SSE 按行拼装数据使用
// static void tts_reset_current_event(tts_sse_parser_t *parser);// 清理当前已经组装好的一个 SSE 事件，准备接收下一个事件
// static void tts_deinit_parser(tts_sse_parser_t *parser);// 释放整个 SSE 解析器内部占用的动态内存
// static void tts_make_request_id(char out[40]);// 生成请求 ID，便于服务端日志和本地日志对齐排查问题/
// static esp_err_t tts_pcm_buffer_copy(uint8_t *pcm, size_t pcm_len);// 拷贝base64解析出的pcm数据到缓冲区
// static void task_tts_play(void *pvParameters);// tts pcm播放任务
// static void tts_apply_soft_gain(int16_t *pcm, size_t sample_count);// 对 16bit PCM 做带饱和保护的软件增益，避免放大后数值溢出
// static esp_err_t tts_decode_and_play_audio(const char *base64_audio);// 把服务端返回的 base64 音频片段解码为 PCM，再立即送到功放播放
// static esp_err_t tts_handle_event_json(tts_sse_parser_t *parser);// 处理单个 SSE 事件里的 JSON 数据：提取音频、判定结束或记录错误
// static esp_err_t tts_process_sse_message(tts_sse_parser_t *parser);// 处理一条完整的 SSE 消息，由空行作为消息结束标记。
// static esp_err_t tts_process_sse_line(tts_sse_parser_t *parser, char *line);// 处理一行 SSE 文本，识别 event:、data: 和空行三种关键内容
// static esp_err_t tts_feed_sse_bytes(tts_sse_parser_t *parser, const char *data, size_t data_len);// 把 HTTP 流中的字节持续喂给 SSE 解析器，直到拆出完整的 event/data 行
// static esp_err_t tts_build_request_body(const char *text, char **body_out);// 构造豆包 TTS 请求 JSON，声明待合成文本、音色和输出音频参数
// static void tts_log_error_response(esp_http_client_handle_t client);// 读取错误响应体，便于打印出服务端返回的错误详情
// bool tts_is_configured(void);// 检查 TTS 运行所需的鉴权参数和音色是否已经填写

#endif
