#ifndef SPI_H_
#define SPI_H_

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"

#define SPI_CLOCK_SPEED_HZ  20 * 1000 * 1000
#define SPI_MAX_CHUNK       4096

#define SPI_MOSI_GPIO_PIN   GPIO_NUM_39
#define SPI_MISO_GPIO_PIN   GPIO_NUM_NC
#define SPI_CLK_GPIO_PIN    GPIO_NUM_40
#define SPI_CS_GPIO_PIN     GPIO_NUM_47
#define SPI_LEDK_GPIO_PIN   GPIO_NUM_41
#define SPI_DC_GPIO_PIN     GPIO_NUM_38
#define SPI_RST_GPIO_PIN    GPIO_NUM_48

esp_err_t spi_init(void);
void spi_write_cmd(uint8_t cmd);
void spi_write_data(const uint8_t *data, int len);



#endif
