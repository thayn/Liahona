/*
 * PN532 NFC reader over I2C (address 0x24; DIP switch 1 ON, 2 OFF).
 * Reads the firmware version once, then polls for a tag and shows its UID.
 */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "nfc";

static i2c_master_dev_handle_t s_nfc;

#define PN_HOST_TO_PN 0xD4
#define PN_PN_TO_HOST 0xD5

static bool wait_ready(int timeout_ms)
{
    uint8_t st = 0;
    for (int t = 0; t < timeout_ms; t += 5) {
        if (i2c_master_receive(s_nfc, &st, 1, 20) == ESP_OK && (st & 0x01)) return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

/* Send a command; returns number of payload bytes after [D5, cmd+1], or -1. */
static int pn_cmd(uint8_t cmd, const uint8_t *p, int plen, uint8_t *out, int out_max, int timeout_ms)
{
    uint8_t f[64];
    int len = plen + 2;                      /* TFI + CMD + params */
    int i = 0;
    f[i++] = 0x00; f[i++] = 0x00; f[i++] = 0xFF;
    f[i++] = (uint8_t)len; f[i++] = (uint8_t)(~len + 1);
    f[i++] = PN_HOST_TO_PN; f[i++] = cmd;
    uint8_t sum = PN_HOST_TO_PN + cmd;
    for (int k = 0; k < plen; k++) { f[i++] = p[k]; sum += p[k]; }
    f[i++] = (uint8_t)(~sum + 1);
    f[i++] = 0x00;
    if (i2c_master_transmit(s_nfc, f, i, 50) != ESP_OK) return -1;

    /* ACK frame */
    if (!wait_ready(100)) return -1;
    uint8_t ack[7];
    if (i2c_master_receive(s_nfc, ack, 7, 50) != ESP_OK) return -1;
    static const uint8_t ACK[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    if (memcmp(ack + 1, ACK, 6) != 0) return -1;

    /* Response frame */
    if (!wait_ready(timeout_ms)) return -1;
    uint8_t r[64];
    if (i2c_master_receive(s_nfc, r, sizeof(r), 50) != ESP_OK) return -1;
    /* r[0]=status, r[1..3]=00 00 FF, r[4]=LEN, r[5]=LCS, r[6]=D5, r[7]=cmd+1 */
    if (r[1] != 0x00 || r[2] != 0x00 || r[3] != 0xFF) return -1;
    int rlen = r[4];
    if (rlen < 2 || r[6] != PN_PN_TO_HOST || r[7] != cmd + 1) return -1;
    int n = rlen - 2;
    if (n > out_max) n = out_max;
    if (8 + n > (int)sizeof(r)) return -1;
    memcpy(out, r + 8, n);
    return n;
}

static void nfc_task(void *arg)
{
    uint8_t d[32];
    bool ok = false;

    for (int attempt = 0; attempt < 5 && !ok; attempt++) {
        int n = pn_cmd(0x02, NULL, 0, d, sizeof(d), 200);          /* GetFirmwareVersion */
        if (n >= 4 && d[0] == 0x32) {
            STATE_LOCK();
            g_state.nfc_fw = (d[0] << 16) | (d[1] << 8) | d[2];
            g_state.nfc = TEST_PASS;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "PN532 found, firmware %d.%d", d[1], d[2]);
            ok = true;
        } else {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    if (!ok) {
        ESP_LOGW(TAG, "PN532 not responding - check the DIP switches (1 ON, 2 OFF)");
        STATE_LOCK(); g_state.nfc = TEST_FAIL; STATE_UNLOCK();
        vTaskDelete(NULL);
    }

    const uint8_t sam[] = { 0x01, 0x14, 0x01 };                     /* normal mode */
    pn_cmd(0x14, sam, sizeof(sam), d, sizeof(d), 100);
    const uint8_t retries[] = { 0x05, 0xFF, 0x01, 0x02 };           /* don't wait forever */
    pn_cmd(0x32, retries, sizeof(retries), d, sizeof(d), 100);

    while (1) {
        const uint8_t list[] = { 0x01, 0x00 };                      /* 1 target, ISO14443A */
        int n = pn_cmd(0x4A, list, sizeof(list), d, sizeof(d), 300);
        STATE_LOCK();
        if (n >= 6 && d[0] >= 1) {
            /* d: NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID... */
            int idlen = d[5];
            char *u = g_state.nfc_uid;
            u[0] = 0;
            for (int k = 0; k < idlen && 6 + k < n && k < 10; k++) {
                char hx[4];
                snprintf(hx, sizeof(hx), k ? ":%02X" : "%02X", d[6 + k]);
                strlcat(u, hx, sizeof(g_state.nfc_uid));
            }
            g_state.nfc_tag_present = true;
        } else {
            g_state.nfc_tag_present = false;
        }
        STATE_UNLOCK();
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

void nfc_start(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_NFC,
        .scl_speed_hz    = 100000,     /* PN532 is happiest at 100 kHz */
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_nfc) != ESP_OK) {
        g_state.nfc = TEST_FAIL;
        return;
    }
    xTaskCreate(nfc_task, "nfc", 4096, NULL, 3, NULL);
}
