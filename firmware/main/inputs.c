/*
 * Lid switch (GPIO48, pull-up, debounced) and optional battery sense (GPIO21 ADC).
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "inputs";

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_chan;
static adc_cali_handle_t s_cali;
static bool s_adc_ok;

static void batt_init(void)
{
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(BATT_PIN, &unit, &s_chan) != ESP_OK) return;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, s_chan, &ccfg);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = { .unit_id = unit, .chan = s_chan,
                                           .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_cali_create_scheme_curve_fitting(&cc, &s_cali);
#endif
    s_adc_ok = true;
}

static float batt_read(void)
{
    if (!s_adc_ok) return 0;
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(s_adc, s_chan, &raw);
        if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) sum += mv;
        else sum += raw * 3300 / 4095;
    }
    return (sum / 8) / 1000.0f * BATT_DIVIDER;
}

static void inputs_task(void *arg)
{
    int stable = gpio_get_level(LID_PIN), count = 0, tick = 0;
    STATE_LOCK(); g_state.lid_closed = (stable == LID_CLOSED_LEVEL); STATE_UNLOCK();

    while (1) {
        int lvl = gpio_get_level(LID_PIN);
        if (lvl != stable) {
            if (++count >= 3) {                  /* 30 ms debounce */
                stable = lvl; count = 0;
                STATE_LOCK();
                g_state.lid_closed = (stable == LID_CLOSED_LEVEL);
                g_state.lid_changes++;
                STATE_UNLOCK();
                ESP_LOGI(TAG, "Lid %s", stable == LID_CLOSED_LEVEL ? "closed" : "open");
            }
        } else {
            count = 0;
        }

        if (++tick >= 100) {                      /* battery once a second */
            tick = 0;
            float v = batt_read();
            STATE_LOCK();
            g_state.batt_v = v;
            g_state.batt = (v >= BATT_PRESENT_MIN_V) ? TEST_PASS : TEST_NA;
            STATE_UNLOCK();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void inputs_start(void)
{
    gpio_config_t lid = {
        .pin_bit_mask = 1ULL << LID_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&lid);
    batt_init();
    xTaskCreate(inputs_task, "inputs", 3072, NULL, 4, NULL);
}
