/**
 * @file lv_port_disp.c
 */

#include "lv_port_disp.h"

#include "lcd.h"

#include <stdbool.h>

#define MY_DISP_HOR_RES LCD_WIDTH
#define MY_DISP_VER_RES LCD_HEIGHT
#define BYTE_PER_PIXEL LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565)
#define DISP_BUF_LINES 20

static void disp_init(void);
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

static lv_display_t *s_display = NULL;
static volatile bool s_disp_flush_enabled = true;

void lv_port_disp_init(void)
{
    if (s_display != NULL) {
        return;
    }

    disp_init();

    s_display = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    if (s_display == NULL) {
        return;
    }

    static uint8_t s_buf1[MY_DISP_HOR_RES * DISP_BUF_LINES * BYTE_PER_PIXEL];
    static uint8_t s_buf2[MY_DISP_HOR_RES * DISP_BUF_LINES * BYTE_PER_PIXEL];

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, disp_flush);
    lv_display_set_buffers(s_display, s_buf1, s_buf2, sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void disp_enable_update(void)
{
    s_disp_flush_enabled = true;
}

void disp_disable_update(void)
{
    s_disp_flush_enabled = false;
}

static void disp_init(void)
{
    lcd_init();
}

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (!s_disp_flush_enabled) {
        lv_display_flush_ready(disp);
        return;
    }

    uint32_t px_count = (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);

    lv_draw_sw_rgb565_swap(px_map, px_count);
    lcd_set_window((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2);
    lcd_write_data(px_map, px_count * BYTE_PER_PIXEL);
    lv_display_flush_ready(disp);
}
