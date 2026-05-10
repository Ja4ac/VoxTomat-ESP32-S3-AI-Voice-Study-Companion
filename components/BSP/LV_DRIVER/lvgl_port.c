#include "lvgl_port.h"

#include "lv_port_disp.h"
#include "voxtomat.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define LVGL_TICK_PERIOD_MS 5
#define LVGL_TASK_STACK     10240
#define LVGL_TASK_PRIORITY  2
#define LVGL_TASK_MIN_DELAY 10
#define LVGL_TASK_MAX_DELAY 100

static const char *TAG = "LVGL_PORT";

static esp_timer_handle_t s_lvgl_tick_timer;
static TaskHandle_t s_lvgl_task_handle;

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    (void)arg;

    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms == LV_NO_TIMER_READY || delay_ms > LVGL_TASK_MAX_DELAY) {
            delay_ms = LVGL_TASK_MAX_DELAY;
        }
        if (delay_ms < LVGL_TASK_MIN_DELAY) {
            delay_ms = LVGL_TASK_MIN_DELAY;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t lvgl_port_init(void)
{
    if (s_lvgl_task_handle != NULL) {
        return ESP_OK;
    }

    lv_init();
    lv_port_disp_init();
    voxtomat_create();

    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_lvgl_tick_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000U);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreate(lvgl_task, "lvgl_task", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, &s_lvgl_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
