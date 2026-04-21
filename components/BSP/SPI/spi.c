#include "spi.h"

#include <string.h>

static const char *TAG = "SPI";
static spi_device_handle_t s_spi_device = NULL;

esp_err_t spi_init(void)
{
    if (s_spi_device != NULL) {
        return ESP_OK;
    }

    esp_err_t err;
    spi_bus_config_t spi_bus_config = {
        .miso_io_num = SPI_MISO_GPIO_PIN,
        .mosi_io_num = SPI_MOSI_GPIO_PIN,
        .sclk_io_num = SPI_CLK_GPIO_PIN,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1,
        .max_transfer_sz = SPI_MAX_CHUNK,
    };
    spi_device_interface_config_t spi_device_interface_config = {
        .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
        .mode = 0,
        .spics_io_num = SPI_CS_GPIO_PIN,
        .queue_size = 7,
    };
    err = spi_bus_initialize(SPI2_HOST, &spi_bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG,"Failed to init spi bus: %s", esp_err_to_name(err));
        return err;
    }
    err = spi_bus_add_device(SPI2_HOST, &spi_device_interface_config, &s_spi_device);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG,"Failed to add device: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static void spi_write_bytes(const uint8_t *data, int len)
{
    if (s_spi_device == NULL || data == NULL || len <= 0) {
        return;
    }

    while (len > 0) {
        spi_transaction_t transfer = {0};
        int chunk_len = (len > SPI_MAX_CHUNK) ? SPI_MAX_CHUNK : len;

        transfer.length = chunk_len * 8;
        if (chunk_len <= 4) {
            transfer.flags = SPI_TRANS_USE_TXDATA;
            memcpy(transfer.tx_data, data, chunk_len);
        } else {
            transfer.tx_buffer = data;
        }

        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi_device, &transfer));

        data += chunk_len;
        len -= chunk_len;
    }
}

void spi_write_cmd(uint8_t cmd)
{
    spi_write_bytes(&cmd, 1);
}

void spi_write_data(const uint8_t *data, int len)
{
    spi_write_bytes(data, len);
}
