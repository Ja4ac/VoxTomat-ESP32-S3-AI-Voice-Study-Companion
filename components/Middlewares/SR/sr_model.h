#ifndef SR_MODEL_H_
#define SR_MODEL_H_

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "model_path.h"

/*
 * sr_model 的职责：
 * 1. 从 model 分区加载 ESP-SR 模型。
 * 2. 选出本次要用的 WakeNet 模型。
 * 3. 建立 AFE 配置与 AFE 实例。
 *
 * 为了降低理解成本，这里把“模型”和“AFE 实例”放在同一个上下文里管理。
 */

typedef struct {
    srmodel_list_t *models;

    // AFE 音频前端配置结构体（声学预处理参数：麦克风、降噪、AEC等配置）
    afe_config_t *afe_config;
    // AFE 函数操作接口句柄（函数指针集合，提供feed/process/destroy等API）
    const esp_afe_sr_iface_t *afe_handle;
    // AFE 音频前端运行实例句柄（真正运行音频预处理的对象）
    esp_afe_sr_data_t *afe_data;

    char *wakenet_model_name;           // 唤醒词模型名称（如wn9_hilexin，由esp_srmodel_filter自动筛选获取）
    char *wake_words;                   // 唤醒词文本内容

    int feed_chunksize;                 // 喂入AFE的音频帧大小（单次输入的采样点数，16kHz下通常为320）
    int fetch_chunksize;                // 从AFE获取的音频帧大小（预处理后输出的采样点数）
    int feed_channel_num;               // 音频输入通道数（单麦=1，双麦=2）
} sr_model_ctx_t;

esp_err_t sr_model_init(sr_model_ctx_t *ctx);
void sr_model_deinit(sr_model_ctx_t *ctx);
void sr_model_log_available(const sr_model_ctx_t *ctx);

#endif
