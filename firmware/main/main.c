/*
 * Liahona ball firmware — milestone 1: foundation + bench self-test.
 *
 * Boots the round 720x720 display, starts every hardware module, and shows
 * a compass home screen plus a self-test page with PASS / FAIL per part.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "liahona.h"

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

void app_main(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state_lock = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    firmware_info();

    /* Shared I2C bus (touch, codec, GPS, compass, NFC). */
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
    sensors_start(bus);
    gps_start(bus);
    nfc_start(bus);
    inputs_start();
    leds_start();
    lora_start();
    audio_test_init();

    ESP_LOGI(TAG, "All modules started");
}
