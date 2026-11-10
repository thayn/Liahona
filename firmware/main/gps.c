/*
 * Adafruit PA1010D mini GPS over I2C (address 0x10).
 * The module streams NMEA text; reading returns it in chunks, padded with
 * '\n' (0x0A) when no new data is waiting. We parse $xxRMC and $xxGGA.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "gps";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_gps;

static char s_line[100];
static int  s_len;

/* NMEA ddmm.mmmm + hemisphere -> signed decimal degrees */
static double nmea_deg(const char *v, const char *hemi)
{
    if (!v || !*v) return 0;
    double raw = atof(v);
    int deg = (int)(raw / 100);
    double d = deg + (raw - deg * 100) / 60.0;
    if (hemi && (*hemi == 'S' || *hemi == 'W')) d = -d;
    return d;
}

/* Split on commas without collapsing empty fields. */
static int split(char *s, char **f, int max)
{
    int n = 0;
    f[n++] = s;
    for (char *p = s; *p && n < max; p++) {
        if (*p == ',' || *p == '*') { *p = 0; f[n++] = p + 1; }
    }
    return n;
}

static void parse_line(char *line)
{
    if (line[0] != '$' || strlen(line) < 7) return;
    char *f[20];
    int n = split(line, f, 20);
    const char *type = f[0] + 3;   /* skip "$GN" / "$GP" */

    if (strncmp(type, "RMC", 3) == 0 && n > 6) {
        STATE_LOCK();
        if (strlen(f[1]) >= 6) {
            snprintf(g_state.gps_time, sizeof(g_state.gps_time), "%.2s:%.2s:%.2s",
                     f[1], f[1] + 2, f[1] + 4);
        }
        g_state.gps_fix = (f[2][0] == 'A');
        if (g_state.gps_fix) {
            g_state.gps_lat = nmea_deg(f[3], f[4]);
            g_state.gps_lon = nmea_deg(f[5], f[6]);
        }
        STATE_UNLOCK();
    } else if (strncmp(type, "GGA", 3) == 0 && n > 7) {
        STATE_LOCK();
        g_state.gps_sats = atoi(f[7]);
        STATE_UNLOCK();
    }
}

static void gps_task(void *arg)
{
    uint8_t buf[64];
    int fails = 0;
    while (1) {
        if (i2c_master_receive(s_gps, buf, sizeof(buf), 100) == ESP_OK) {
            fails = 0;
            int real = 0;
            for (int i = 0; i < (int)sizeof(buf); i++) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (s_len > 0) { s_line[s_len] = 0; parse_line(s_line); s_len = 0; }
                    continue;
                }
                if (c < 0x20 || c > 0x7E) continue;
                real++;
                if (s_len < (int)sizeof(s_line) - 1) s_line[s_len++] = c;
                else s_len = 0;
            }
            STATE_LOCK();
            g_state.gps_bytes += real;
            if (g_state.gps_bytes > 0) g_state.gps = TEST_PASS;
            STATE_UNLOCK();
            /* Padding only = nothing new; wait a bit before reading again. */
            vTaskDelay(pdMS_TO_TICKS(real ? 5 : 100));
        } else {
            if (++fails == 30) {
                ESP_LOGW(TAG, "PA1010D not answering at 0x%02X", I2C_ADDR_GPS);
                STATE_LOCK(); g_state.gps = TEST_FAIL; STATE_UNLOCK();
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void gps_start(i2c_master_bus_handle_t bus)
{
    s_bus = bus;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_GPS,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &cfg, &s_gps) != ESP_OK) {
        g_state.gps = TEST_FAIL;
        return;
    }
    xTaskCreate(gps_task, "gps", 4096, NULL, 3, NULL);
}
