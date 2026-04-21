# Changelog

## v1.1.0 - 2026-04-12

### Added
- 新增 `DeepSeek` 对话处理能力（非流式），目前可以实现 `ASR->LLM->TTS` 的对话流程

### Improved
- 优化 代码结构，使架构更规范
- 优化 `FreeRTOS` 任务通信流程，提升系统运行速度

## v1.2.0 - 2026-04-16

### Added
- 新增 `idf_component.yml` 管理组件
- 新增 `SR` 模块，接入 `ESP-SR` 语音前端能力
- 新增 `WakeNet` 唤醒词模型，现在可以通过说出唤醒词：“嗨 小鑫”激活系统并输入后续命令
- 新增 `VAD` 检测，用于识别有效语音起止
- 新增 `SR -> ASR -> LLM -> TTS` 事件驱动语音交互链路

### Improved
- 优化 `partions-16MiB.csv` 的分区表配置，增加 `model` 分区以支持 `ESP-SR` 模型加载
- 优化 `tts.c`的 PCM 语音处理逻辑，使用 `StreamBuffer` 与 `PSRAM` 缓冲，降低阻塞与内存压力
- 优化 开启 I2S DMA 的 `auto_clear`，避免 DMA 搬运无效内容导致扬声器播放噪声
- 优化 `LLM_SYSTEM_ROLE`，避免 `LLM` 输出的文本中包含特殊符号导致 `TTS` 合成的语音中带有不必要的内容
- 优化 变量与函数的命名，同一代码风格
- 修改 `CMakeLists.txt`配置文本
- 更新 `README.md`

## v1.3.0 - 2026-04-21

### Added
- 新增 `SPI` `LCD` 模块，实现显示屏的显示
- 新增 `LVGL` 组件
- 新增 `UI` `LV_DRIVER` 模块，实现 `LVGL` 的移植，并绘制了UI
- 新增 原理图、PCB、实物图

### Improved
- 优化 项目结构
- 更新 `README.md`

