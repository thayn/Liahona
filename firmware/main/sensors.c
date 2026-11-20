/*
 * I2C bus scan + LIS3MDL magnetometer (compass).
 *
 * Heading is not tilt-compensated, so keep the ball roughly level.
 * Calibration (Compass row on the self-test -> turn the ball) removes the
 * pull of the battery, speaker magnet and screws. It is kept in NVS, so it
 * survives power-off, SD card changes and Wi-Fi firmware updates.
 */
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "sensors";

#define MAG_WHO_AM_I   0x0F
#define MAG_CTRL1      0x20
#define MAG_CTRL2      0x21
#define MAG_CTRL3      0x22
#define MAG_CTRL4      0x23
#define MAG_CTRL5      0x24
#define MAG_OUT_X_L    0x28
#define MAG_ID         0x3D
#define MAG_LSB_PER_G  6842.0f   /* +/-4 gauss range */

static i2c_master_bus_handle_t s_bus;
static mag_cal_t s_cal = { .scale = { 1, 1 } };
static portMUX_TYPE s_cal_mux = portMUX_INITIALIZER_UNLOCKED;

static void cal_load(void)
{
    nvs_handle_t h;
    mag_cal_t c;
    size_t n = sizeof(c);
    if (nvs_open("compass", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "cal", &c, &n) == ESP_OK && n == sizeof(c) && c.version == MAG_CAL_VERSION) {
        s_cal = c;
        ESP_LOGI(TAG, "compass cal: off %.3f %.3f  scale %.3f %.3f  north %+.1f  %s",
                 c.off[0], c.off[1], c.scale[0], c.scale[1], c.north_deg, c.mirror ? "mirrored" : "");
    }
    nvs_close(h);
}

void mag_cal_get(mag_cal_t *out)
{
    taskENTER_CRITICAL(&s_cal_mux); *out = s_cal; taskEXIT_CRITICAL(&s_cal_mux);
}

bool mag_cal_set(const mag_cal_t *c)
{
    mag_cal_t v = *c;
    v.version = MAG_CAL_VERSION;
    taskENTER_CRITICAL(&s_cal_mux); s_cal = v; taskEXIT_CRITICAL(&s_cal_mux);
    STATE_LOCK(); g_state.mag_cal_valid = v.valid; STATE_UNLOCK();

    nvs_handle_t h;
    if (nvs_open("compass", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "cal", &v, sizeof(v)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    ESP_LOGI(TAG, "compass cal saved (%s)", ok ? "ok" : "FAILED");
    return ok;
}

/* Raw field (gauss) -> heading in degrees, 0 = magnetic north, clockwise. */
float mag_heading(float gx, float gy, const mag_cal_t *c)
{
    float x = gx, y = gy;
    if (c->valid) {
        x = (gx - c->off[0]) * c->scale[0];
        y = (gy - c->off[1]) * c->scale[1];
    }
    float h = atan2f(y, x) * 180.0f / (float)M_PI;
    if (c->mirror) h = -h;
    h += c->north_deg;
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    return h;
}
static i2c_master_dev_handle_t s_mag;

static esp_err_t mag_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_mag, b, 2, 50);
}

static esp_err_t mag_read(uint8_t reg, uint8_t *out, size_t n)
{
    if (n > 1) reg |= 0x80;   /* auto-increment */
    return i2c_master_transmit_receive(s_mag, &reg, 1, out, n, 50);
}

static void i2c_scan(void)
{
    int n = 0;
    uint8_t found[16];
    for (uint8_t a = 0x08; a < 0x78 && n < 16; a++) {
        if (i2c_master_probe(s_bus, a, 20) == ESP_OK) {
            found[n++] = a;
        }
    }
    STATE_LOCK();
    memcpy(g_state.i2c_addrs, found, n);
    g_state.i2c_count = n;
    STATE_UNLOCK();
    for (int i = 0; i < n; i++) ESP_LOGI(TAG, "I2C device at 0x%02X", found[i]);
}

static bool mag_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_MAG,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &cfg, &s_mag) != ESP_OK) return false;

    uint8_t id = 0;
    if (mag_read(MAG_WHO_AM_I, &id, 1) != ESP_OK || id != MAG_ID) {
        ESP_LOGW(TAG, "LIS3MDL not found (id=0x%02X)", id);
        return false;
    }
    mag_write(MAG_CTRL1, 0x7C);   /* XY ultra-high performance, 80 Hz */
    mag_write(MAG_CTRL2, 0x00);   /* +/-4 gauss */
    mag_write(MAG_CTRL3, 0x00);   /* continuous */
    mag_write(MAG_CTRL4, 0x0C);   /* Z ultra-high performance */
    mag_write(MAG_CTRL5, 0x40);   /* block data update */
    ESP_LOGI(TAG, "LIS3MDL ready");
    return true;
}

static void sensors_task(void *arg)
{
    cal_load();
    STATE_LOCK(); g_state.mag_cal_valid = s_cal.valid; STATE_UNLOCK();
    i2c_scan();

    bool ok = mag_init();
    STATE_LOCK();
    g_state.mag = ok ? TEST_PASS : TEST_FAIL;
    STATE_UNLOCK();

    int rescan = 0;
    while (1) {
        if (ok) {
            uint8_t raw[6];
            if (mag_read(MAG_OUT_X_L, raw, 6) == ESP_OK) {
                int16_t x = (int16_t)(raw[1] << 8 | raw[0]);
                int16_t y = (int16_t)(raw[3] << 8 | raw[2]);
                int16_t z = (int16_t)(raw[5] << 8 | raw[4]);
                float gx = x / MAG_LSB_PER_G, gy = y / MAG_LSB_PER_G, gz = z / MAG_LSB_PER_G;
                mag_cal_t c;
                mag_cal_get(&c);
                float h = mag_heading(gx, gy, &c);
                STATE_LOCK();
                g_state.mag_x = gx; g_state.mag_y = gy; g_state.mag_z = gz;
                g_state.heading_deg = h;
                STATE_UNLOCK();
            }
        } else if (++rescan >= 50) {   /* retry every ~5 s if plugged in late */
            rescan = 0;
            i2c_scan();
            if (s_mag) { i2c_master_bus_rm_device(s_mag); s_mag = NULL; }
            ok = mag_init();
            STATE_LOCK();
            g_state.mag = ok ? TEST_PASS : TEST_FAIL;
            STATE_UNLOCK();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void sensors_start(i2c_master_bus_handle_t bus)
{
    s_bus = bus;
    xTaskCreate(sensors_task, "sensors", 4096, NULL, 4, NULL);
}
