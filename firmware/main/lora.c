/*
 * SX1262 LoRa radio — presence check only (milestone 1).
 *
 * Resets the radio, reads its status and the LoRa sync-word register
 * (default 0x14 0x24). No transmitting yet: transmitting without an antenna
 * attached can damage the radio, and the protocol comes with the base station.
 */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "lora";
static spi_device_handle_t s_spi;

static bool wait_busy_low(int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t++) {
        if (gpio_get_level(LORA_PIN_BUSY) == 0) return true;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

static esp_err_t xfer(const uint8_t *tx, uint8_t *rx, int len)
{
    if (!wait_busy_low(100)) return ESP_ERR_TIMEOUT;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = tx, .rx_buffer = rx };
    return spi_device_polling_transmit(s_spi, &t);
}

static void lora_task(void *arg)
{
    /* Hardware reset */
    gpio_set_level(LORA_PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(LORA_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    bool busy_ok = wait_busy_low(200);

    uint8_t tx[5] = { 0xC0, 0x00 };          /* GetStatus */
    uint8_t rx[5] = { 0 };
    esp_err_t e1 = xfer(tx, rx, 2);
    uint8_t status = rx[1];

    uint8_t tx2[6] = { 0x1D, 0x07, 0x40, 0x00, 0x00, 0x00 };   /* ReadRegister 0x0740, 2 bytes */
    uint8_t rx2[6] = { 0 };
    esp_err_t e2 = xfer(tx2, rx2, 6);

    bool found = busy_ok && e1 == ESP_OK && e2 == ESP_OK &&
                 rx2[4] == 0x14 && rx2[5] == 0x24;

    STATE_LOCK();
    g_state.lora_status = status;
    g_state.lora_sync[0] = rx2[4];
    g_state.lora_sync[1] = rx2[5];
    g_state.lora = found ? TEST_PASS : TEST_FAIL;
    STATE_UNLOCK();

    if (found) {
        ESP_LOGI(TAG, "SX1262 found (status 0x%02X)", status);
    } else {
        ESP_LOGW(TAG, "SX1262 not found: busy=%d sync=%02X %02X - check wiring",
                 busy_ok, rx2[4], rx2[5]);
    }
    vTaskDelete(NULL);
}

void lora_start(void)
{
    gpio_config_t out = { .pin_bit_mask = 1ULL << LORA_PIN_RESET, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&out);
    gpio_set_level(LORA_PIN_RESET, 1);
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << LORA_PIN_BUSY) | (1ULL << LORA_PIN_DIO1),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);

    spi_bus_config_t bus = {
        .sclk_io_num = LORA_PIN_SCK, .mosi_io_num = LORA_PIN_MOSI, .miso_io_num = LORA_PIN_MISO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 256,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000, .mode = 0,
        .spics_io_num = LORA_PIN_NSS, .queue_size = 2,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK ||
        spi_bus_add_device(SPI2_HOST, &dev, &s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "SPI setup failed");
        g_state.lora = TEST_FAIL;
        return;
    }
    xTaskCreate(lora_task, "lora", 3072, NULL, 3, NULL);
}
