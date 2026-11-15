/*
 * ESP-IDF glue for the SX1262 driver: SPI bus, BUSY/RESET/RXEN pins and a DIO1 interrupt.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "sx1262.h"

static const char *TAG = "sx1262";

typedef struct {
    spi_device_handle_t spi;
    SemaphoreHandle_t dio1_sem;
} hal_t;

static void IRAM_ATTR dio1_isr(void *arg)
{
    hal_t *h = (hal_t *)arg;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(h->dio1_sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

int sx_hal_init(sx1262_t *r)
{
    static hal_t s_hal;          /* one radio per device */
    hal_t *h = &s_hal;
    const sx1262_cfg_t *c = &r->cfg;
    r->hal = h;
    if (h->spi) return SX_OK;    /* already set up (probe then begin) */

    gpio_config_t out = { .pin_bit_mask = 1ULL << c->reset, .mode = GPIO_MODE_OUTPUT };
    if (c->rxen >= 0) out.pin_bit_mask |= 1ULL << c->rxen;
    gpio_config(&out);
    gpio_set_level(c->reset, 1);
    if (c->rxen >= 0) gpio_set_level(c->rxen, 0);

    gpio_config_t in = { .pin_bit_mask = 1ULL << c->busy, .mode = GPIO_MODE_INPUT };
    gpio_config(&in);
    gpio_config_t irq = { .pin_bit_mask = 1ULL << c->dio1, .mode = GPIO_MODE_INPUT,
                          .intr_type = GPIO_INTR_POSEDGE };
    gpio_config(&irq);

    spi_bus_config_t bus = {
        .sclk_io_num = c->sck, .mosi_io_num = c->mosi, .miso_io_num = c->miso,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 300,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 8 * 1000 * 1000, .mode = 0,
        .spics_io_num = c->nss, .queue_size = 2,
    };
    esp_err_t e = spi_bus_initialize(c->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG, "SPI bus: %s", esp_err_to_name(e)); return SX_ERR_HAL; }
    if (spi_bus_add_device(c->spi_host, &dev, &h->spi) != ESP_OK) { ESP_LOGE(TAG, "SPI device"); return SX_ERR_HAL; }

    h->dio1_sem = xSemaphoreCreateBinary();
    e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return SX_ERR_HAL;   /* already installed is fine */
    gpio_isr_handler_add(c->dio1, dio1_isr, h);
    return SX_OK;
}

bool sx_hal_busy(sx1262_t *r) { return gpio_get_level(r->cfg.busy) != 0; }

/* Wait for BUSY to drop, by the clock (not by RTOS ticks, which may be 10 ms long).
 * Calibration with a TCXO can keep BUSY high for several milliseconds. */
static bool wait_not_busy(sx1262_t *r, uint32_t max_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)max_ms * 1000;
    while (sx_hal_busy(r)) {
        if (esp_timer_get_time() > deadline) return false;
        esp_rom_delay_us(50);
    }
    return true;
}

int sx_hal_xfer(sx1262_t *r, const uint8_t *tx, uint8_t *rx, size_t n)
{
    hal_t *h = (hal_t *)r->hal;
    if (!wait_not_busy(r, 200)) return SX_ERR_BUSY;
    spi_transaction_t t = { .length = n * 8, .tx_buffer = tx, .rx_buffer = rx };
    return spi_device_polling_transmit(h->spi, &t) == ESP_OK ? SX_OK : SX_ERR_HAL;
}

void sx_hal_reset_pin(sx1262_t *r, int level) { gpio_set_level(r->cfg.reset, level); }

void sx_hal_rxen(sx1262_t *r, int level)
{
    if (r->cfg.rxen >= 0) gpio_set_level(r->cfg.rxen, level);
}

bool sx_hal_wait_dio1(sx1262_t *r, uint32_t ms)
{
    hal_t *h = (hal_t *)r->hal;
    if (gpio_get_level(r->cfg.dio1)) return true;          /* already high */
    TickType_t t = pdMS_TO_TICKS(ms);
    return xSemaphoreTake(h->dio1_sem, t ? t : 1) == pdTRUE;
}

void sx_hal_delay_ms(uint32_t ms)
{
    /* Short waits by the clock; longer ones give the CPU back (rounded up to a tick). */
    if (ms < 20) { esp_rom_delay_us(ms * 1000); return; }
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t ? t + 1 : 1);
}
