#include "schedule.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SCHEDULE";

static schedule_ctx_t s_schedule_items[SCHEDULE_MAX_SIZE];
static uint8_t s_schedule_count = 0;

static int schedule_compare_time(const schedule_ctx_t *a, const schedule_ctx_t *b)
{
    if(a->year != b->year) return a->year - b->year;
    if(a->month != b->month) return a->month - b->month;
    if(a->day != b->day) return a->day - b->day;
    if(a->hour != b->hour) return a->hour - b->hour;
    return a->minute - b->minute;
}

static void schedule_sort_by_time(void)
{
    for(uint8_t i = 0; i < s_schedule_count; i++)
    {
        for(uint8_t j = i + 1; j < s_schedule_count; j++)
        {
            if(schedule_compare_time(&s_schedule_items[i], &s_schedule_items[j]) > 0)
            {
                schedule_ctx_t tmp = s_schedule_items[i];
                s_schedule_items[i] = s_schedule_items[j];
                s_schedule_items[j] = tmp;
            }
        }
    }
}

esp_err_t schedule_add(int year, int month, int day, int hour, int minute,const  char *text)
{
    if(text == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if(s_schedule_count >= SCHEDULE_MAX_SIZE)
    {
        return ESP_ERR_NO_MEM;
    }

    schedule_ctx_t *item = &s_schedule_items[s_schedule_count];
    item->year = year;
    item->month = month;
    item->day = day;
    item->hour = hour;
    item->minute = minute;
    snprintf(item->text, sizeof(item->text), "%s", text);
    s_schedule_count++;
    schedule_sort_by_time();
    return ESP_OK;
}

esp_err_t schedule_delete(int index)
{
    if(index <= 0 || index > s_schedule_count)
    {
        ESP_LOGE(TAG, "The item to be deleted does not exist");
        return ESP_ERR_INVALID_ARG;
    }
    for(uint8_t i = index - 1; i < s_schedule_count - 1; i++)
    {
        s_schedule_items[i] = s_schedule_items[i + 1];
    }
    s_schedule_count--;
    return ESP_OK;
}

uint8_t schedule_copy_items(schedule_ctx_t *out_items, uint8_t max_count)
{
    if(out_items == NULL || max_count == 0)
    {
        return 0;
    }
    uint8_t count = 0;
    for(uint8_t i = 0; i < s_schedule_count && i < max_count; i++)
    {
        out_items[i].year = s_schedule_items[i].year;
        out_items[i].month = s_schedule_items[i].month;
        out_items[i].day = s_schedule_items[i].day;
        out_items[i].hour = s_schedule_items[i].hour;
        out_items[i].minute = s_schedule_items[i].minute;
        snprintf(out_items[i].text, sizeof(out_items[i].text),
                         "%s", s_schedule_items[i].text);
        count++;
    }
    return count;
}