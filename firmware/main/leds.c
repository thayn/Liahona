/*
 * Band LEDs (WS2812 / NeoPixel) on GPIO47 via the 74AHCT125 level shifter.
 * Mode 0: warm gold "breathing" glow. Mode 1: red/green/blue test cycle. 2: off.
 */
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "leds";
static led_strip_handle_t s_strip;
static volatile int s_mode = 1;   /* start with the test cycle */

void leds_set_mode(int mode) { s_mode = mode; }

static uint8_t scale(int v) { return (uint8_t)(v * LED_MAX_BRIGHTNESS / 255); }

static void fill(int r, int g, int b)
{
    for (int i = 0; i < LED_COUNT; i++) led_strip_set_pixel(s_strip, i, scale(r), scale(g), scale(b));
    led_strip_refresh(s_strip);
}

static void leds_task(void *arg)
{
    int step = 0;
    while (1) {
        switch (s_mode) {
        case 1: {                                   /* test: R, G, B, white, then chase */
            int phase = (step / 10) % 5;
            if (phase < 4) {
                static const uint8_t c[4][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255} };
                fill(c[phase][0], c[phase][1], c[phase][2]);
            } else {
                led_strip_clear(s_strip);
                led_strip_set_pixel(s_strip, step % LED_COUNT, scale(255), scale(160), scale(40));
                led_strip_refresh(s_strip);
            }
            break;
        }
        case 0: {                                   /* idle: gold breathing */
            float k = 0.35f + 0.65f * (0.5f + 0.5f * sinf(step * 0.06f));
            fill((int)(255 * k), (int)(150 * k), (int)(30 * k));
            break;
        }
        default:
            led_strip_clear(s_strip);
            break;
        }
        step++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void leds_start(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = LED_PIN_DATA,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&cfg, &rmt, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed");
        g_state.leds = TEST_FAIL;
        return;
    }
    led_strip_clear(s_strip);
    /* The chip can't see the LEDs: PASS here only means the driver is running.
     * Confirm by eye that the colours cycle. */
    g_state.leds = TEST_PASS;
    xTaskCreate(leds_task, "leds", 3072, NULL, 2, NULL);
}
