#ifndef DATE_H
#define DATE_H

#include "wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

#define DATE_BASE_URL       "http://101.34.207.105/api/time/getapi.php"
#define DATE_USER_ID        PROJECT_DATE_USER_ID
#define DATE_USER_KEY       PROJECT_DATE_USER_KEY    
#define DATE_TYPE           "20"

typedef struct 
{
    int code;           // 状态码200成功
    int year;              // 年
    int month;              // 月
    int day;              // 日
    int hour;              // 时
    int minute;              // 分
    int second;              // 秒
}date_t;

esp_err_t date_init(void);
esp_err_t date_deinit(void);
esp_err_t date_get_time(void);
esp_err_t date_get_current_time(date_t *app_date);

#endif