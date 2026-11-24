/*
 * Liahona ball firmware — milestone 1: foundation + bench self-test.
 *
 * Boots the round 720x720 display, starts every hardware module, and shows
 * a compass home screen plus a self-test page with PASS / FAIL per part.
 */
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "board_pins.h"
#include "liahona.h"
#include "update.h"
#include "c6_update.h"
#include "logbuf.h"

static const char *TAG = "liahona";

liahona_state_t   g_state;
SemaphoreHandle_t g_state_lock;

static void firmware_info(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *run = esp_ota_get_running_partition();
    strlcpy(g_state.fw_version, app->version, sizeof(g_state.fw_version));
    strlcpy(g_state.ota_partition, run ? run->label : "?", sizeof(g_state.ota_partition));
    ESP_LOGI(TAG, "Liahona firmware %s running from %s", g_state.fw_version, g_state.ota_partition);

    /* If this image was just installed by OTA, mark it good so it is kept.
     * (Harmless on a normal USB flash.) */
    esp_ota_mark_app_valid_cancel_rollback();
}

/* Standard bus recovery: clock SCL until a stuck device lets go of SDA, then a STOP. */
static void i2c_unstick(int sda, int scl)
{
    gpio_config_t io = { .pin_bit_mask = (1ULL << sda) | (1ULL << scl), .mode = GPIO_MODE_INPUT_OUTPUT_OD,
                         .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(sda)) { gpio_reset_pin(sda); gpio_reset_pin(scl); return; }
    ESP_LOGW(TAG, "I2C bus stuck (SDA low): clocking it free");
    for (int i = 0; i < 18 && !gpio_get_level(sda); i++) {
        gpio_set_level(scl, 0); esp_rom_delay_us(10);
        gpio_set_level(scl, 1); esp_rom_delay_us(10);
    }
    gpio_set_level(sda, 0); esp_rom_delay_us(10);          /* STOP: SDA rises while SCL is high */
    gpio_set_level(scl, 1); esp_rom_delay_us(10);
    gpio_set_level(sda, 1); esp_rom_delay_us(10);
    ESP_LOGW(TAG, "I2C bus %s", gpio_get_level(sda) ? "free again" : "still stuck: turn the ball fully off and on");
    gpio_reset_pin(sda);
    gpio_reset_pin(scl);
}

void app_main(void)
{
    logbuf_start();          /* log ring that survives a crash (logbuf.c) */
    memset(&g_state, 0, sizeof(g_state));
    g_state_lock = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    firmware_info();

    /* Shared I2C bus (touch, codec, GPS, compass, NFC). A restart doesn't power-cycle the
     * devices, so one stuck mid-byte (holding SDA low) would make the touch screen
     * "not found" and the BSP stops. Clock it free first. */
    i2c_unstick(I2C_SDA_PIN, I2C_SCL_PIN);
    ESP_ERROR_CHECK(bsp_i2c_init());
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();

    /* Screen + touch + LVGL, then our UI. */
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Display failed to start - check the LCD type in menuconfig");
    }
    bsp_display_backlight_on();
    ui_start();

    /* Hardware modules. Each runs in its own task and reports into g_state,
     * so one missing part never stops the others. */
    storage_start();
    if (g_state.sd == TEST_PASS) logbuf_save_crash();
    sensors_start(bus);
    gps_start(bus);
    nfc_start(bus);
    inputs_start();
    leds_start();
    lora_start();
    audio_test_init();
    journey_start();         /* reads liahona/journey.json + roster.json from the SD card */

    c6_update_start();       /* updates the Wi-Fi chip if liahona/c6_wifi.bin is on the SD card */

    ESP_LOGI(TAG, "All modules started");

    /* After a Wi-Fi firmware update, keep the new firmware once it has run for a bit.
     * If it crashes before this, the bootloader goes back to the old one. */
    vTaskDelay(pdMS_TO_TICKS(20000));
    update_confirm_boot();
}
