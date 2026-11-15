/*
 * Speaker: the self-test chime, and journey audio from the SD card.
 *
 * One task owns the ES8311 codec and plays one request at a time. A new play
 * (or audio_stop) cuts off whatever is playing. Files: WAV (16-bit PCM) or MP3,
 * any sample rate, mono or stereo (mixed down to mono for the single speaker).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "minimp3.h"
#include "board_pins.h"
#include "liahona.h"

static const char *TAG = "audio";
static esp_codec_dev_handle_t s_spk;
static QueueHandle_t s_q;
static volatile bool s_stop;
static volatile bool s_playing;
static int s_rate;                     /* codec's current sample rate, 0 = closed */

#define BEEP_RATE 22050
#define FADE_SAMPLES 256               /* short fades so starts and stops don't click */

typedef struct {
    int  kind;                         /* 0 chime, 1 file, 2 tap tick */
    char path[160];
    int  start_ms, dur_ms;
} audio_req_t;

static void set_rate(int rate)
{
    if (rate == s_rate) return;
    if (s_rate) esp_codec_dev_close(s_spk);
    esp_codec_dev_sample_info_t fs = { .sample_rate = rate, .channel = 1, .bits_per_sample = 16 };
    esp_codec_dev_open(s_spk, &fs);
    esp_codec_dev_set_out_vol(s_spk, AUDIO_VOLUME);
    s_rate = rate;
}

/* ---------------------------------------------------------------- chime */
static void play_tone(float hz, int ms)
{
    static int16_t buf[512];
    int total = BEEP_RATE * ms / 1000, done = 0;
    while (done < total) {
        int n = total - done; if (n > 512) n = 512;
        for (int i = 0; i < n; i++) {
            int k = done + i;
            float env = 1.0f;
            if (k < 400) env = k / 400.0f;                       /* fade in  */
            if (total - k < 1500) env = (total - k) / 1500.0f;   /* fade out */
            buf[i] = (int16_t)(sinf(2.0f * (float)M_PI * hz * k / BEEP_RATE) * 9000 * env);
        }
        esp_codec_dev_write(s_spk, buf, n * sizeof(int16_t));
        done += n;
    }
}

/* ---------------------------------------------------------------- output with fades */
typedef struct { long played, total; } outpos_t;

/* Write mono samples, fading in at the start and out at the end of the window. */
static void out_mono(int16_t *s, int n, outpos_t *o)
{
    for (int i = 0; i < n; i++) {
        long k = o->played + i;
        long left = o->total - k;
        float g = 1.0f;
        if (k < FADE_SAMPLES) g = (float)k / FADE_SAMPLES;
        if (left < FADE_SAMPLES) g = (float)left / FADE_SAMPLES;
        if (g < 1.0f) s[i] = (int16_t)(s[i] * g);
    }
    esp_codec_dev_write(s_spk, s, n * sizeof(int16_t));
    o->played += n;
}

static void silence_tail(void)
{
    static int16_t z[256];
    esp_codec_dev_write(s_spk, z, sizeof(z));
}

/* ---------------------------------------------------------------- WAV */
static uint32_t rd32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t rd16(const uint8_t *b) { return b[0] | (b[1] << 8); }

static void play_wav(FILE *f, int start_ms, int dur_ms)
{
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { ESP_LOGW(TAG, "not a WAV file"); return; }
    int ch = 0, rate = 0, bits = 0, fmt = 0;
    uint32_t data_len = 0;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) return;
        uint32_t len = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t b[16];
            if (len < 16 || fread(b, 1, 16, f) != 16) return;
            fmt = rd16(b); ch = rd16(b + 2); rate = rd32(b + 4); bits = rd16(b + 14);
            if (len > 16) fseek(f, len - 16, SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            data_len = len;
            break;
        } else {
            fseek(f, len + (len & 1), SEEK_CUR);
        }
    }
    if (fmt != 1 || bits != 16 || ch < 1 || ch > 2 || rate < 8000) {
        ESP_LOGW(TAG, "WAV must be 16-bit PCM (got format %d, %d-bit, %d ch)", fmt, bits, ch);
        return;
    }
    set_rate(rate);
    long frames = data_len / (2 * ch);
    long first = (long)start_ms * rate / 1000;
    if (first >= frames) return;
    long want = dur_ms > 0 ? (long)dur_ms * rate / 1000 : frames - first;
    if (first + want > frames) want = frames - first;
    fseek(f, first * 2 * ch, SEEK_CUR);

    static int16_t in[1024 * 2], mono[1024];
    outpos_t o = { 0, want };
    while (o.played < want && !s_stop) {
        long n = want - o.played; if (n > 1024) n = 1024;
        size_t got = fread(in, 2 * ch, n, f);
        if (!got) break;
        for (size_t i = 0; i < got; i++) mono[i] = ch == 2 ? (int16_t)(((int)in[2 * i] + in[2 * i + 1]) / 2) : in[i];
        out_mono(mono, got, &o);
    }
}

/* ---------------------------------------------------------------- MP3 */
static void play_mp3(FILE *f, int start_ms, int dur_ms)
{
    static mp3dec_t dec;
    static uint8_t buf[16 * 1024];
    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME], mono[MINIMP3_MAX_SAMPLES_PER_FRAME / 2 + 8];
    mp3dec_init(&dec);

    /* skip an ID3v2 tag (album art can be large) */
    uint8_t id3[10];
    if (fread(id3, 1, 10, f) == 10 && !memcmp(id3, "ID3", 3)) {
        long sz = ((id3[6] & 0x7F) << 21) | ((id3[7] & 0x7F) << 14) | ((id3[8] & 0x7F) << 7) | (id3[9] & 0x7F);
        fseek(f, 10 + sz, SEEK_SET);
    } else {
        fseek(f, 0, SEEK_SET);
    }

    size_t have = 0, pos = 0;
    long skip = -1, want = -1;            /* in samples, set once the rate is known */
    outpos_t o = { 0, 0 };
    bool eof = false;
    while (!s_stop) {
        if (!eof && have - pos < 4096) {   /* refill */
            memmove(buf, buf + pos, have - pos);
            have -= pos; pos = 0;
            size_t got = fread(buf + have, 1, sizeof(buf) - have, f);
            if (!got) eof = true;
            have += got;
        }
        if (pos >= have) break;
        mp3dec_frame_info_t info;
        int n = mp3dec_decode_frame(&dec, buf + pos, have - pos, pcm, &info);
        if (info.frame_bytes == 0) { if (eof) break; pos = have; continue; }
        pos += info.frame_bytes;
        if (n <= 0) continue;
        if (skip < 0) {
            set_rate(info.hz);
            skip = (long)start_ms * info.hz / 1000;
            want = dur_ms > 0 ? (long)dur_ms * info.hz / 1000 : 0x7FFFFFFF;
            o.total = want;
        }
        int off = 0;
        if (skip > 0) { off = skip >= n ? n : (int)skip; skip -= off; }
        int m = 0;
        for (int i = off; i < n && o.played + m < want; i++)
            mono[m++] = info.channels == 2 ? (int16_t)(((int)pcm[2 * i] + pcm[2 * i + 1]) / 2) : pcm[i];
        if (m) out_mono(mono, m, &o);
        if (o.played >= want) break;
    }
}

/* ---------------------------------------------------------------- task */
static void play_file(const audio_req_t *r)
{
    FILE *f = fopen(r->path, "rb");
    if (!f) { ESP_LOGW(TAG, "can't open %s", r->path); return; }
    const char *dot = strrchr(r->path, '.');
    if (dot && (!strcasecmp(dot, ".wav"))) play_wav(f, r->start_ms, r->dur_ms);
    else play_mp3(f, r->start_ms, r->dur_ms);
    fclose(f);
    silence_tail();
}

static void audio_task(void *arg)
{
    audio_req_t r;
    while (1) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        s_stop = false;
        s_playing = true;
        if (r.kind == 0) {
            set_rate(BEEP_RATE);
            play_tone(784.0f, 180);   /* G5 */
            play_tone(1175.0f, 320);  /* D6 */
            STATE_LOCK(); g_state.audio = TEST_PASS; STATE_UNLOCK();
        } else if (r.kind == 2) {
            set_rate(BEEP_RATE);
            play_tone(1319.0f, 90);   /* E6: "got it" */
        } else {
            play_file(&r);
        }
        s_playing = false;
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
    set_rate(BEEP_RATE);
    s_q = xQueueCreate(2, sizeof(audio_req_t));
    xTaskCreate(audio_task, "audio", 8192, NULL, 5, NULL);
}

void audio_test_beep(void)
{
    audio_req_t r = { .kind = 0 };
    if (s_q) { s_stop = true; xQueueSend(s_q, &r, 0); }
}

void audio_play(const char *file, int start_ms, int dur_ms)
{
    if (!s_q || !file) return;
    audio_req_t r = { .kind = 1, .start_ms = start_ms, .dur_ms = dur_ms };
    if (file[0] == '/') snprintf(r.path, sizeof(r.path), "%s", file);
    else snprintf(r.path, sizeof(r.path), "%s/%s", CONTENT_MEDIA_DIR, file);
    s_stop = true;                     /* cut off anything playing */
    xQueueReset(s_q);
    xQueueSend(s_q, &r, 0);
}

/* A short tick when a medallion or tag is read, unless something is playing. */
void audio_tap_sound(void)
{
    audio_req_t r = { .kind = 2 };
    if (s_q && !s_playing && uxQueueMessagesWaiting(s_q) == 0) xQueueSend(s_q, &r, 0);
}

void audio_stop(void) { s_stop = true; if (s_q) xQueueReset(s_q); }
bool audio_busy(void) { return s_playing; }
