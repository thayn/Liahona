/*
 * Wi-Fi chip (ESP32-C6) firmware update, from the SD card.
 *
 * The board's C6 ships with very old ESP-Hosted firmware (it reports 0.0.0).
 * Our Wi-Fi library is 3.1.0, and with that mix Wi-Fi joins but data stalls.
 * Fix: build firmware_c6/ (same ESP-Hosted 3.1.0), copy its .bin to
 *   liahona/c6_wifi.bin
 * on the SD card and restart the ball. A few seconds after start-up, before
 * Wi-Fi is used, the ball streams the file to the C6 over the existing link
 * (ESP-Hosted OTA: begin / write / end / activate), renames it to
 * c6_wifi.bin.done and restarts. The C6 keeps its old firmware in its other
 * slot until the new one is complete, so a failed copy is safe to retry.
 *
 * Progress shows on the self-test Firmware row and in the log ("c6").
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "eh_host_cp_ota.h"
#include "eh_host_sys.h"
#include "eh_host_api_types.h"
#include "liahona.h"
#include "c6_update.h"

static const char *TAG = "c6";

#define C6_FILE   "/sdcard/liahona/c6_wifi.bin"
#define C6_DONE   C6_FILE ".done"
#define CHUNK     1024

static volatile bool s_busy;

bool c6_update_busy(void) { return s_busy; }

static void show(const char *t)
{
    STATE_LOCK(); strlcpy(g_state.update_text, t, sizeof(g_state.update_text)); STATE_UNLOCK();
    ESP_LOGI(TAG, "%s", t);
}

static bool cp_version(eh_host_coprocessor_fwver_t *v)
{
    for (int i = 0; i < 3; i++) {
        memset(v, 0, sizeof(*v));
        if (eh_host_sys_get_cp_fw_version(v) == ESP_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

static void c6_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(6000));            /* let the C6 link come up first */

    struct stat st;
    if (stat(C6_FILE, &st) != 0) { s_busy = false; vTaskDelete(NULL); return; }

    eh_host_coprocessor_fwver_t v;
    bool have_ver = cp_version(&v);
    ESP_LOGI(TAG, "Wi-Fi chip firmware: %s %lu.%lu.%lu", have_ver ? "" : "(unknown)",
             (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
    if (have_ver && v.major1 >= 3) {
        show("Wi-Fi chip already up to date");
        rename(C6_FILE, C6_DONE);
        s_busy = false; vTaskDelete(NULL); return;
    }

    FILE *f = fopen(C6_FILE, "rb");
    esp_image_header_t hdr;
    if (!f || fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        hdr.magic != ESP_IMAGE_HEADER_MAGIC || hdr.chip_id != ESP_CHIP_ID_ESP32C6) {
        if (f) fclose(f);
        show("c6_wifi.bin isn't ESP32-C6 firmware");
        s_busy = false; vTaskDelete(NULL); return;
    }
    rewind(f);

    show("Wi-Fi chip: starting update");
    esp_err_t e = eh_host_cp_ota_begin();
    if (e != ESP_OK) {
        fclose(f);
        char t[64]; snprintf(t, sizeof(t), "Wi-Fi chip update refused (%s)", esp_err_to_name(e));
        show(t);
        s_busy = false; vTaskDelete(NULL); return;
    }

    static uint8_t buf[CHUNK];
    long sent = 0;
    int last_pct = -1;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if ((e = eh_host_cp_ota_write(buf, n)) != ESP_OK) break;
        sent += n;
        int pct = (int)(sent * 100 / st.st_size);
        if (pct / 5 != last_pct / 5) {
            last_pct = pct;
            char t[48]; snprintf(t, sizeof(t), "Wi-Fi chip: %d%%", pct);
            show(t);
        }
    }
    fclose(f);
    if (e == ESP_OK) e = eh_host_cp_ota_end();
    if (e != ESP_OK) {
        char t[64]; snprintf(t, sizeof(t), "Wi-Fi chip update failed at %ld KB (%s)", sent / 1024, esp_err_to_name(e));
        show(t);
        s_busy = false; vTaskDelete(NULL); return;
    }

    /* CP firmware 2.6+ needs an explicit activate; older ones switch on end() */
    if (have_ver && (v.major1 > 2 || (v.major1 == 2 && v.minor1 > 5))) {
        e = eh_host_cp_ota_activate();
        if (e != ESP_OK) ESP_LOGW(TAG, "activate: %s", esp_err_to_name(e));
    }
    rename(C6_FILE, C6_DONE);
    show("Wi-Fi chip updated, restarting");
    /* give the C6 time to finish and reboot before we reset it at start-up */
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

void c6_update_start(void)
{
    s_busy = true;
    xTaskCreate(c6_task, "c6_update", 6144, NULL, 3, NULL);
}
