/*
 * microSD card: mount, report size, and look for the journey manifest.
 * The card must be FAT32 (not exFAT).
 */
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "bsp/esp-bsp.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "storage";

void storage_start(void)
{
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not mounted (%s) - is a FAT32 card inserted?", esp_err_to_name(err));
        g_state.sd = TEST_FAIL;
        return;
    }
    uint64_t total = 0, free_b = 0;
    esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free_b);
    struct stat st;
    bool manifest = (stat(CONTENT_MANIFEST, &st) == 0);

    STATE_LOCK();
    g_state.sd = TEST_PASS;
    g_state.sd_total_mb = total / (1024 * 1024);
    g_state.sd_free_mb  = free_b / (1024 * 1024);
    g_state.manifest_found = manifest;
    STATE_UNLOCK();
    ESP_LOGI(TAG, "SD mounted: %llu MB, %llu MB free, manifest %s",
             g_state.sd_total_mb, g_state.sd_free_mb, manifest ? "found" : "not found");
}
