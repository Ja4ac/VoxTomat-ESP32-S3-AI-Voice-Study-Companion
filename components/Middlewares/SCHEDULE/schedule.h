#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "esp_err.h"
#include <stdint.h>

// 最大日程存储量
#define SCHEDULE_MAX_SIZE           10
// 单条日程最大显示长度
#define SCHEDULE_MAX_TEXT_LEN       64

typedef struct 
{
    int year;               // 年
    int month;              // 月
    int day;                // 日
    int hour;               // 时
    int minute;             // 分
    char text[SCHEDULE_MAX_TEXT_LEN];   // 日程
}schedule_ctx_t;

esp_err_t schedule_add(int year, int month, int day, int hour, int minute, const char *text);
esp_err_t schedule_delete(int index);
uint8_t schedule_copy_items(schedule_ctx_t *out_items, uint8_t max_count);

#endif