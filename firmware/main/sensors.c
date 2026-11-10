/*
 * I2C bus scan + LIS3MDL magnetometer (compass).
 *
 * Heading is uncalibrated and not tilt-compensated: good enough to prove the
 * part works. Hard-iron calibration (spin the ball once) comes later.
 */
#include <math.h>
#include <string.h>
#include "esp_log.h"
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
                float h = atan2f(gy, gx) * 180.0f / (float)M_PI;
                if (h < 0) h += 360.0f;
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
