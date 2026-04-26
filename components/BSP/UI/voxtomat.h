#ifndef VOXTOMAT_H
#define VOXTOMAT_H

#include "lvgl.h"

#include <stdio.h>

#define VOXTOMAT_SCHEDULE_MAX      2
#define VOXTOMAT_SCHEDULE_TEXT_MAX 64
typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    char text[VOXTOMAT_SCHEDULE_TEXT_MAX];
} voxtomat_schedule_item_t;

void voxtomat_create(void);
void voxtomat_countdown_start(void);
void voxtomat_countdown_pause(void);
void voxtomat_countdown_reset(uint16_t minutes);
void voxtomat_bat_set(uint8_t bat_percent);
void voxtomat_time_update(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);
void voxtomat_schedule_refresh(const voxtomat_schedule_item_t *items, uint8_t count);
void voxtomat_dialogue_set_text(const char *text);


#endif
