#include "sr_model.h"

#include <string.h>

#include "esp_log.h"
#include "esp_vad.h"
#include "esp_wn_models.h"

static const char *TAG = "SR_MODEL";

// 打印当前加载的所有可用语音模型信息
void sr_model_log_available(const sr_model_ctx_t *ctx)
{
    int i;

    if (ctx == NULL || ctx->models == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Loaded %d SR models from partition 'model'", ctx->models->num);
    for (i = 0; i < ctx->models->num; i++) {
        ESP_LOGI(TAG, "Model[%d]: %s", i, ctx->models->model_name[i]);
    }
}

esp_err_t sr_model_init(sr_model_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    // 第一步：从 model 分区加载所有已打包的 SR 模型。
    ctx->models = esp_srmodel_init("model");
    if (ctx->models == NULL) {
        ESP_LOGE(TAG, "Failed to load SR models from partition 'model'");
        return ESP_FAIL;
    }
    sr_model_log_available(ctx);

    // 第二步：选择一个 WakeNet 模型。
    ctx->wakenet_model_name = esp_srmodel_filter(ctx->models, ESP_WN_PREFIX, NULL);
    if (ctx->wakenet_model_name == NULL) {
        ESP_LOGE(TAG, "No WakeNet model found");
        sr_model_deinit(ctx);
        return ESP_ERR_NOT_FOUND;
    }

    // 从模型中提取唤醒词的文本内容（如：你好乐鑫）
    ctx->wake_words = esp_srmodel_get_wake_words(ctx->models, ctx->wakenet_model_name);

    // 第三步：创建AFE配置：单麦输入(M) + 绑定模型 + 语音识别专用模式 + 高性能模式
    ctx->afe_config = afe_config_init("M", ctx->models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (ctx->afe_config == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE config");
        sr_model_deinit(ctx);
        return ESP_FAIL;
    }

    // 第四步：打开 WakeNet：负责唤醒词检测，打开 VAD：负责判断用户什么时候开始/结束说话
    ctx->afe_config->wakenet_init = true;
    ctx->afe_config->wakenet_model_name = ctx->wakenet_model_name;      // 绑定筛选到的唤醒词模型名称
    ctx->afe_config->vad_init = true;
    ctx->afe_config->vad_mode = VAD_MODE_3;                             // VAD工作模式（等级3，灵敏度/稳定性均衡）
    ctx->afe_config->vad_min_speech_ms = 200;                           // VAD最小语音时长：判定为有效语音的最短时间
    ctx->afe_config->vad_min_noise_ms = 2000;                            // VAD最小静音时长：判定为结束说话的最短时间
    ctx->afe_config->vad_delay_ms = 200;                                // VAD检测延迟时间

    // 校验AFE配置参数合法性，自动修正错误配置
    ctx->afe_config = afe_config_check(ctx->afe_config);
    if (ctx->afe_config == NULL) {
        ESP_LOGE(TAG, "Failed to validate AFE config");
        sr_model_deinit(ctx);
        return ESP_FAIL;
    }

    // 第五步：从配置拿到 AFE 接口句柄。create_from_config/feed/fetch 都不是普通全局函数，它们属于这个接口表里的函数指针。
    ctx->afe_handle = esp_afe_handle_from_config(ctx->afe_config);
    if (ctx->afe_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get AFE handle");
        sr_model_deinit(ctx);
        return ESP_FAIL;
    }

    // 第六步：创建 AFE 运行实例。
    ctx->afe_data = ctx->afe_handle->create_from_config(ctx->afe_config);
    if (ctx->afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE instance");
        sr_model_deinit(ctx);
        return ESP_FAIL;
    }

    // 第七步：缓存后续 feed/fetch 任务要用到的块大小信息。
    ctx->feed_chunksize = ctx->afe_handle->get_feed_chunksize(ctx->afe_data);           // 获取每次需要喂入AFE的音频帧大小（采样点数）
    ctx->fetch_chunksize = ctx->afe_handle->get_fetch_chunksize(ctx->afe_data);         // 获取从AFE输出的音频帧大小（采样点数）
    ctx->feed_channel_num = ctx->afe_handle->get_feed_channel_num(ctx->afe_data);       // 获取音频输入通道数（单麦=1）

    // 打印AFE音频处理流水线信息
    ctx->afe_handle->print_pipeline(ctx->afe_data);
    ESP_LOGI(TAG,
             "SR initialized, wakenet=%s, wake_words=%s, feed_chunksize=%d, fetch_chunksize=%d, channel_num=%d",
             ctx->wakenet_model_name,
             ctx->wake_words ? ctx->wake_words : "unknown",
             ctx->feed_chunksize,
             ctx->fetch_chunksize,
             ctx->feed_channel_num);
    return ESP_OK;
}

void sr_model_deinit(sr_model_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->afe_handle != NULL && ctx->afe_data != NULL) {
        ctx->afe_handle->destroy(ctx->afe_data);
    }

    if (ctx->models != NULL) {
        esp_srmodel_deinit(ctx->models);
    }

    memset(ctx, 0, sizeof(*ctx));
}
