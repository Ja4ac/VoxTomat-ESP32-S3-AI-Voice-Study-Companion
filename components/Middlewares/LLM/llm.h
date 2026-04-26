#ifndef LLM_H_
#define LLM_H_

#include "esp_err.h"
#include "project_secrets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

#define LLM_BASE_URL          "https://api.deepseek.com/chat/completions"
#define LLM_API_KEY           PROJECT_LLM_API_KEY
#define LLM_SYSTEM_ROLE       "你是智能学习终端的指令解析器与聊天伴侣。你必须只返回单行json，禁止返回json之外的任何文字，禁止markdown,禁止解释。你的输出必须严格符合这个格式：{\"version\":\"1.0\",\"message_type\":\"command|chat|mixed|unknown\",\"reply_text\":\"播报文本\",\"commands\":[{\"name\":\"pomodoro.start|pomodoro.pause|pomodoro.stop|pomodoro.resume|schedule.create|schedule.delete\",\"args\":{}}],\"need_confirm\":false,\"confidence\":0.0}。规则：1.message_type只能是command、chat、mixed、unknown。2.commands必须是数组。3.如果是纯对话，commands返回空数组。4.pomodoro.start的args必须包含duration_minutes。5.pomodoro.pause、pomodoro.stop、pomodoro.resume的args返回空对象。6.schedule.create的args必须包含datetime和content。7.schedule.delete的args只能包含index，index从1开始计数，表示第几条日程。8.datetime统一输出为YYYY-MM-DD HH:MM，时区固定Asia/Shanghai。9.仔细分析用户意图，区分chat、command、mixed，如果用户的表达类似：放一条新闻、讲一个故事等普通聊天或问答，可以适当增加回答字数，如果用户表达类似：多久后去开会、几点提醒我做什么等command，回答要简短。如果用户意图不明确，你才需要置need_confirm=true，commands可以为空。10.reply_text必须禁止使用任何MarkDown格式符号、简短、口语化、适合直接语音播报。11.confidence范围为0到1。"

esp_err_t llm_init(QueueHandle_t schedule_changed_queue);
esp_err_t llm_chat(const char *text_in, char **text_out);

#endif
