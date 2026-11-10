/*
 * Adafruit PA1010D mini GPS over I2C (address 0x10).
 * The module streams NMEA text; reading returns it in chunks, padded with
 * '\n' (0x0A) when no new data is waiting. We parse $xxRMC and $xxGGA.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "gps";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_gps;

static char s_line[100];
static int  s_len;
static char s_prev;

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

/* NMEA checksum: XOR of everything between '$' and '*' must match the hex after '*'. */
static bool checksum_ok(const char *line)
{
    const char *star = strchr(line, '*');
    if (!star || strlen(star) < 3) return false;
    uint8_t x = 0;
    for (const char *p = line + 1; p < star; p++) x ^= (uint8_t)*p;
    return x == (uint8_t)strtol(star + 1, NULL, 16);
}

static void parse_line(char *line)
{
    if (line[0] != '$' || strlen(line) < 7) return;
    if (!checksum_ok(line)) return;
    if (strncmp(line + 3, "RMC", 3) == 0 || strncmp(line + 3, "GGA", 3) == 0) {
        ESP_LOGD(TAG, "%s", line);
    }
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
    } else if (strncmp(type, "GSV", 3) == 0 && n > 3) {
        /* satellites in view, per constellation (GP, GL, GA, BD/GB) */
        static const char *talk[] = { "GP", "GL", "GA", "BD", "GB" };
        static int seen[5];
        for (int i = 0; i < 5; i++) {
            if (strncmp(f[0] + 1, talk[i], 2) == 0) seen[i] = atoi(f[3]);
        }
        STATE_LOCK();
        g_state.gps_in_view = seen[0] + seen[1] + seen[2] + seen[3] + seen[4];
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
                /* The module pads with lone '\n' bytes when it has nothing to
                 * send. Only "\r\n" ends a sentence; ignore the padding. */
                if (c == '\n') {
                    if (s_prev == '\r' && s_len > 0) {
                        s_line[s_len] = 0;
                        parse_line(s_line);
                        s_len = 0;
                    }
                    s_prev = c;
                    continue;
                }
                s_prev = c;
                if (c == '\r') continue;
                if (c < 0x20 || c > 0x7E) continue;
                real++;
                if (c == '$') s_len = 0;             /* new sentence always restarts */
                if (s_len < (int)sizeof(s_line) - 1) s_line[s_len++] = c;
                else s_len = 0;
            }
            STATE_LOCK();
            g_state.gps_bytes += real;
            if (g_state.gps_bytes > 0) g_state.gps = TEST_PASS;
            STATE_UNLOCK();
            static int64_t last_log;
            int64_t now = esp_timer_get_time();
            if (now - last_log > 5000000) {
                last_log = now;
                STATE_LOCK();
                ESP_LOGI(TAG, "fix=%d  sats used=%d  in view=%d  time=%s",
                         g_state.gps_fix, g_state.gps_sats, g_state.gps_in_view, g_state.gps_time);
                STATE_UNLOCK();
            }
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
