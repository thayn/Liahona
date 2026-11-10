/*
 * Speaker test: a short two-note chime through the ES8311 codec.
 */
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "liahona.h"

static const char *TAG = "audio";
static esp_codec_dev_handle_t s_spk;
static TaskHandle_t s_task;

#define RATE 22050

static void play_tone(float hz, int ms)
{
    static int16_t buf[512];
    int total = RATE * ms / 1000, done = 0;
    while (done < total) {
        int n = total - done; if (n > 512) n = 512;
        for (int i = 0; i < n; i++) {
            int k = done + i;
            float env = 1.0f;
            if (k < 400) env = k / 400.0f;                       /* fade in  */
            if (total - k < 1500) env = (total - k) / 1500.0f;   /* fade out */
            buf[i] = (int16_t)(sinf(2.0f * (float)M_PI * hz * k / RATE) * 9000 * env);
        }
        esp_codec_dev_write(s_spk, buf, n * sizeof(int16_t));
        done += n;
    }
}

static void audio_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        play_tone(784.0f, 180);   /* G5 */
        play_tone(1175.0f, 320);  /* D6 */
        STATE_LOCK(); g_state.audio = TEST_PASS; STATE_UNLOCK();
    }
}

void audio_test_init(void)
{
    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGE(TAG, "Speaker codec init failed");
        g_state.audio = TEST_FAIL;
        return;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = RATE, .channel = 1, .bits_per_sample = 16,
    };
    esp_codec_dev_open(s_spk, &fs);
    esp_codec_dev_set_out_vol(s_spk, 70);
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, &s_task);
}

void audio_test_beep(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
