#ifndef APP_H
#define APP_H

#include "voxtomat.h"
#include "lvgl.h"
#include "esp_err.h"

#include <stdio.h>

esp_err_t app_pomodoro_start_async(uint16_t minutes);
esp_err_t app_pomodoro_pause_async(void);
esp_err_t app_pomodoro_stop_async(void);
esp_err_t app_pomodoro_resume_async(void);
esp_err_t app_time_update_async(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);
esp_err_t app_schedule_update_async(const voxtomat_schedule_item_t *items, uint8_t count);
esp_err_t app_dialogue_set_text_async(const char *text);

#endif