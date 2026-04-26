#include "app.h"
#include <string.h>

typedef struct 
{
    uint16_t minutes;
}app_pomodoro_ctx_t;

typedef struct
{
    int year;               // 年
    int month;              // 月
    int day;                // 日
    int hour;               // 时
    int minute;             // 分
    int second;            // 秒
}app_time_ctx_t;

static app_pomodoro_ctx_t s_pomodoro_ctx;
static app_time_ctx_t s_time_ctx;
static voxtomat_schedule_item_t s_schedule_items[VOXTOMAT_SCHEDULE_MAX];
static uint8_t s_schedule_count;

static void app_pomodoro_start_async_cb(void *arg)
{
    voxtomat_countdown_pause();
    voxtomat_countdown_reset(s_pomodoro_ctx.minutes);
    voxtomat_countdown_start();

}

static void app_pomodoro_pause_async_cb(void *arg)
{
    (void)arg;
    voxtomat_countdown_pause();
}

static void app_pomodoro_stop_async_cb(void *arg)
{
    (void)arg;
    voxtomat_countdown_pause();
    voxtomat_countdown_reset(25);
}

static void app_pomodoro_resume_async_cb(void *arg)
{
    (void)arg;
    voxtomat_countdown_start();
}

esp_err_t app_pomodoro_start_async(uint16_t minutes)
{
    s_pomodoro_ctx.minutes = minutes;
    return lv_async_call(app_pomodoro_start_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t app_pomodoro_pause_async(void)
{
    return lv_async_call(app_pomodoro_pause_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t app_pomodoro_stop_async(void)
{
    return lv_async_call(app_pomodoro_stop_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t app_pomodoro_resume_async(void)
{
    return lv_async_call(app_pomodoro_resume_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

static void app_time_update_async_cb(void *arg)
{
    voxtomat_time_update(s_time_ctx.year, s_time_ctx.month, s_time_ctx.day, s_time_ctx.hour, s_time_ctx.minute);
}

esp_err_t app_time_update_async(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute)
{
    s_time_ctx.year = year;
    s_time_ctx.month = month;
    s_time_ctx.day = day;
    s_time_ctx.hour = hour;
    s_time_ctx.minute = minute;
    return lv_async_call(app_time_update_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

static void app_schedule_update_async_cb(void *arg)
{
    (void)arg;
    voxtomat_schedule_refresh(s_schedule_items, s_schedule_count);
}

esp_err_t app_schedule_update_async(const voxtomat_schedule_item_t *ui_items, uint8_t count)
{
    if(ui_items == NULL && count > 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if(count > VOXTOMAT_SCHEDULE_MAX)
    {
        count = VOXTOMAT_SCHEDULE_MAX;
    }

    s_schedule_count = count;   

    for(uint8_t n = 0; n < count; n++)
    {
        s_schedule_items[n] = ui_items[n];
    }
    return lv_async_call(app_schedule_update_async_cb, NULL) == LV_RESULT_OK ? ESP_OK : ESP_FAIL;
}

static void app_dialogue_set_text_async_cb(void *arg)
{
    char *text = (char *)arg;
    voxtomat_dialogue_set_text(text);
    free(text);
}

esp_err_t app_dialogue_set_text_async(const char *text)
{
    if(text == NULL)
    {
        text = "";
    }
    char *text_cpy = strdup(text);
    if(text_cpy == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    if(lv_async_call(app_dialogue_set_text_async_cb, text_cpy) == LV_RESULT_OK)
    {
        return ESP_OK;
    }
    else
    {
        free(text_cpy);
        return ESP_FAIL;
    }

}