#ifndef LCD_H__
#define LCD_H__

#include "SPI.h"
#include <stdint.h>

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

#define USE_HORIZONTAL 1

#define WHITE       0xFFFF
#define BLACK       0x0000
#define BLUE        0x001F
#define BRED        0xF81F
#define GRED        0xFFE0
#define GBLUE       0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define GREEN       0x07E0
#define CYAN        0x7FFF
#define YELLOW      0xFFE0
#define BROWN       0xBC40
#define BRRED       0xFC07
#define GRAY        0x8430
#define DARKBLUE    0x01CF
#define LIGHTBLUE   0x7D7C
#define GRAYBLUE    0x5458
#define LIGHTGREEN  0x841F
#define LIGHTGRAY   0xEF5B
#define LGRAY       0xC618
#define LGRAYBLUE   0xA651
#define LBBLUE      0x2B12

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t id;
    uint8_t dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
} lcd_dev_t;

extern lcd_dev_t lcddev;
extern uint16_t POINT_COLOR;
extern uint16_t BACK_COLOR;

void lcd_init(void);
void lcd_display_on(void);
void lcd_display_off(void);
void lcd_clear(uint16_t color);
void lcd_set_cursor(uint16_t x_pos, uint16_t y_pos);
void lcd_draw_point(uint16_t x, uint16_t y);
void lcd_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
void lcd_write_cmd(uint8_t data);
void lcd_write_data(const uint8_t *data, uint32_t len);
void lcd_write_data8(uint8_t data);
void lcd_write_data16(uint16_t data);
void lcd_set_direction(uint8_t direction);

#endif
