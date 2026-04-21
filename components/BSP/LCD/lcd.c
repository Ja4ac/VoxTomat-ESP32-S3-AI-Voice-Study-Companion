#include "LCD.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

lcd_dev_t lcddev;
uint16_t POINT_COLOR = 0x0000;
uint16_t BACK_COLOR = 0xFFFF;

#define LCD_CS_SET  gpio_set_level(SPI_CS_GPIO_PIN, 1)
#define LCD_CS_CLR  gpio_set_level(SPI_CS_GPIO_PIN, 0)
#define LCD_DC_SET  gpio_set_level(SPI_DC_GPIO_PIN, 1)
#define LCD_DC_CLR  gpio_set_level(SPI_DC_GPIO_PIN, 0)
#define LCD_RST_SET gpio_set_level(SPI_RST_GPIO_PIN, 1)
#define LCD_RST_CLR gpio_set_level(SPI_RST_GPIO_PIN, 0)
#define LCD_LED_ON  gpio_set_level(SPI_LEDK_GPIO_PIN, 1)
#define LCD_LED_OFF gpio_set_level(SPI_LEDK_GPIO_PIN, 0)

/**
 * @brief 配置 LCD 控制引脚。
 */
static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SPI_CS_GPIO_PIN) | (1ULL << SPI_DC_GPIO_PIN) |
                        (1ULL << SPI_RST_GPIO_PIN) | (1ULL << SPI_LEDK_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(SPI_CS_GPIO_PIN, 1);
    gpio_set_level(SPI_DC_GPIO_PIN, 1);
    gpio_set_level(SPI_RST_GPIO_PIN, 1);
    gpio_set_level(SPI_LEDK_GPIO_PIN, 0);
}

/**
 * @brief 向 LCD 写入 1 个命令字节。
 */
void lcd_write_cmd(uint8_t data)
{
    LCD_CS_CLR;
    LCD_DC_CLR;
    spi_write_cmd(data);
    LCD_CS_SET;
}

/**
 * @brief 向 LCD 写入 1 个数据字节。
 */
void lcd_write_data8(uint8_t data)
{
    uint8_t tx_data = data;

    LCD_CS_CLR;
    LCD_DC_SET;
    spi_write_data(&tx_data, sizeof(tx_data));
    LCD_CS_SET;
}

void lcd_write_data(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    LCD_CS_CLR;
    LCD_DC_SET;
    spi_write_data(data, (int)len);
    LCD_CS_SET;
}

/**
 * @brief 向指定寄存器写入 1 个参数。
 */
static void lcd_write_reg(uint8_t lcd_reg, uint16_t lcd_reg_value)
{
    lcd_write_cmd(lcd_reg);
    lcd_write_data8((uint8_t)lcd_reg_value);
}

/**
 * @brief 在连续写像素数据前发送 GRAM 写入命令。
 */
static void lcd_prepare_write_ram(void)
{
    lcd_write_cmd(lcddev.wramcmd);
}

/**
 * @brief 向 LCD 写入 1 个 16 位 RGB565 像素。
 */
void lcd_write_data16(uint16_t data)
{
    uint8_t tx_data[2] = {(uint8_t)(data >> 8), (uint8_t)data};

    LCD_CS_CLR;
    LCD_DC_SET;
    spi_write_data(tx_data, sizeof(tx_data));
    LCD_CS_SET;
}

/**
 * @brief 在指定坐标绘制 1 个像素点。
 */
void lcd_draw_point(uint16_t x, uint16_t y)
{
    lcd_set_cursor(x, y);
    lcd_write_data16(POINT_COLOR);
}

/**
 * @brief 使用单一颜色填充整个显示区域。
 *
 * 软件 SPI 清屏较慢，这里周期性让出调度，避免触发任务看门狗。
 */
void lcd_clear(uint16_t color)
{
    uint32_t i;
    uint32_t m;
    uint16_t width = lcddev.width;
    uint16_t height = lcddev.height;
    uint8_t line_buf[320 * 2];

    if (width > 320) {
        width = 320;
    }

    for (m = 0; m < width; m++) {
        line_buf[m * 2] = (uint8_t)(color >> 8);
        line_buf[m * 2 + 1] = (uint8_t)color;
    }

    lcd_set_window(0, 0, lcddev.width - 1, lcddev.height - 1);
    for (i = 0; i < height; i++) {
        LCD_CS_CLR;
        LCD_DC_SET;
        spi_write_data(line_buf, width * 2);
        LCD_CS_SET;

        // 周期性让出 CPU，避免大面积清屏时饿死空闲任务。
        if ((i & 0x1F) == 0x1F) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/**
 * @brief 按照时序要求复位 LCD 控制器。
 */
static void lcd_reset(void)
{
    LCD_RST_CLR;
    vTaskDelay(pdMS_TO_TICKS(100));
    LCD_RST_SET;
    vTaskDelay(pdMS_TO_TICKS(120));
}

/**
 * @brief 设置后续像素写入使用的显示窗口。
 */
void lcd_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    lcd_write_cmd(lcddev.setxcmd);
    lcd_write_data8((uint8_t)(x_start >> 8));
    lcd_write_data8((uint8_t)(x_start & 0xFF));
    lcd_write_data8((uint8_t)(x_end >> 8));
    lcd_write_data8((uint8_t)(x_end & 0xFF));

    lcd_write_cmd(lcddev.setycmd);
    lcd_write_data8((uint8_t)(y_start >> 8));
    lcd_write_data8((uint8_t)(y_start & 0xFF));
    lcd_write_data8((uint8_t)(y_end >> 8));
    lcd_write_data8((uint8_t)(y_end & 0xFF));

    lcd_prepare_write_ram();
}

/**
 * @brief 将 LCD 光标移动到指定像素位置。
 */
void lcd_set_cursor(uint16_t x_pos, uint16_t y_pos)
{
    lcd_set_window(x_pos, y_pos, x_pos, y_pos);
}

/**
 * @brief 配置显示扫描方向和逻辑分辨率。
 */
void lcd_set_direction(uint8_t direction)
{
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;

    switch (direction) {
        case 0:
            lcddev.width = LCD_WIDTH;
            lcddev.height = LCD_HEIGHT;
            lcd_write_reg(0x36, (1 << 3) | (0 << 6) | (0 << 7));
            break;
        case 1:
            lcddev.width = LCD_HEIGHT;
            lcddev.height = LCD_WIDTH;
            lcd_write_reg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
            break;
        case 2:
            lcddev.width = LCD_WIDTH;
            lcddev.height = LCD_HEIGHT;
            lcd_write_reg(0x36, (1 << 3) | (1 << 6) | (1 << 7));
            break;
        case 3:
            lcddev.width = LCD_HEIGHT;
            lcddev.height = LCD_WIDTH;
            lcd_write_reg(0x36, (1 << 3) | (1 << 7) | (1 << 5));
            break;
        default:
            break;
    }
}

/**
 * @brief 初始化 ILI9341 控制器并打开背光。
 */
void lcd_init(void)
{
    spi_init();
    lcd_gpio_init();
    lcd_reset();

    lcd_write_cmd(0xCF);
    lcd_write_data8(0x00);
    lcd_write_data8(0xD9);
    lcd_write_data8(0x30);
    lcd_write_cmd(0xED);
    lcd_write_data8(0x64);
    lcd_write_data8(0x03);
    lcd_write_data8(0x12);
    lcd_write_data8(0x81);
    lcd_write_cmd(0xE8);
    lcd_write_data8(0x85);
    lcd_write_data8(0x10);
    lcd_write_data8(0x7A);
    lcd_write_cmd(0xCB);
    lcd_write_data8(0x39);
    lcd_write_data8(0x2C);
    lcd_write_data8(0x00);
    lcd_write_data8(0x34);
    lcd_write_data8(0x02);
    lcd_write_cmd(0xF7);
    lcd_write_data8(0x20);
    lcd_write_cmd(0xEA);
    lcd_write_data8(0x00);
    lcd_write_data8(0x00);
    lcd_write_cmd(0xC0);
    lcd_write_data8(0x1B);
    lcd_write_cmd(0xC1);
    lcd_write_data8(0x12);
    lcd_write_cmd(0xC5);
    lcd_write_data8(0x26);
    lcd_write_data8(0x26);
    lcd_write_cmd(0xC7);
    lcd_write_data8(0xB0);
    lcd_write_cmd(0x36);
    lcd_write_data8(0x08);
    lcd_write_cmd(0x3A);
    lcd_write_data8(0x55);
    lcd_write_cmd(0xB1);
    lcd_write_data8(0x00);
    lcd_write_data8(0x1A);
    lcd_write_cmd(0xB6);
    lcd_write_data8(0x0A);
    lcd_write_data8(0xA2);
    lcd_write_cmd(0xF2);
    lcd_write_data8(0x00);
    lcd_write_cmd(0x26);
    lcd_write_data8(0x01);
    lcd_write_cmd(0xE0);
    lcd_write_data8(0x1F);
    lcd_write_data8(0x24);
    lcd_write_data8(0x24);
    lcd_write_data8(0x0D);
    lcd_write_data8(0x12);
    lcd_write_data8(0x09);
    lcd_write_data8(0x52);
    lcd_write_data8(0xB7);
    lcd_write_data8(0x3F);
    lcd_write_data8(0x0C);
    lcd_write_data8(0x15);
    lcd_write_data8(0x06);
    lcd_write_data8(0x0E);
    lcd_write_data8(0x08);
    lcd_write_data8(0x00);
    lcd_write_cmd(0xE1);
    lcd_write_data8(0x00);
    lcd_write_data8(0x1B);
    lcd_write_data8(0x1B);
    lcd_write_data8(0x02);
    lcd_write_data8(0x0E);
    lcd_write_data8(0x06);
    lcd_write_data8(0x2E);
    lcd_write_data8(0x48);
    lcd_write_data8(0x3F);
    lcd_write_data8(0x03);
    lcd_write_data8(0x0A);
    lcd_write_data8(0x09);
    lcd_write_data8(0x31);
    lcd_write_data8(0x37);
    lcd_write_data8(0x1F);

    lcd_write_cmd(0x2B);
    lcd_write_data8(0x00);
    lcd_write_data8(0x00);
    lcd_write_data8(0x01);
    lcd_write_data8(0x3F);
    lcd_write_cmd(0x2A);
    lcd_write_data8(0x00);
    lcd_write_data8(0x00);
    lcd_write_data8(0x00);
    lcd_write_data8(0xEF);
    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_write_cmd(0x29);

    lcd_set_direction(USE_HORIZONTAL);
    LCD_LED_ON;
}

/**
 * @brief 打开 LCD 显示。
 */
void lcd_display_on(void)
{
    lcd_write_cmd(0x29);
}

/**
 * @brief 关闭 LCD 显示。
 */
void lcd_display_off(void)
{
    lcd_write_cmd(0x28);
}
