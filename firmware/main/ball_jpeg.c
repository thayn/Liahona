/*
 * JPEG pictures from the SD card, decoded by the ESP32-P4's hardware JPEG engine
 * into an LVGL image (RGB565). Used for journey images (and video frames later).
 *
 * Baseline JPEG only (the hardware can't do progressive). The Manager shrinks
 * uploads to fit the screen; very large pictures are refused to save memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_decode.h"
#include "ball_jpeg.h"

static const char *TAG = "jpeg";
static jpeg_decoder_handle_t s_dec;

#define MAX_FILE  (3 * 1024 * 1024)
#define MAX_SIDE  2048

static bool engine(void)
{
    if (s_dec) return true;
    jpeg_decode_engine_cfg_t cfg = { .intr_priority = 0, .timeout_ms = 500 };
    if (jpeg_new_decoder_engine(&cfg, &s_dec) != ESP_OK) { ESP_LOGE(TAG, "no JPEG engine"); s_dec = NULL; }
    return s_dec != NULL;
}

static uint32_t up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

bool ball_jpeg_decode_mem(const uint8_t *jpg, size_t len, ball_jpeg_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!engine()) return false;
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(jpg, len, &info) != ESP_OK || !info.width || !info.height) {
        ESP_LOGW(TAG, "not a JPEG the hardware can read (baseline JPEG only)");
        return false;
    }
    if (info.width > MAX_SIDE || info.height > MAX_SIDE) {
        ESP_LOGW(TAG, "picture too big (%lux%lu): resize it to 600 px", (unsigned long)info.width, (unsigned long)info.height);
        return false;
    }
    uint32_t aw = 8, ah = 8;                     /* output padding follows the chroma sampling */
    if (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) { aw = 16; ah = 16; }
    else if (info.sample_method == JPEG_DOWN_SAMPLING_YUV422) { aw = 16; ah = 8; }
    uint32_t pw = up(info.width, aw), ph = up(info.height, ah);

    jpeg_decode_memory_alloc_cfg_t oc = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    size_t cap = 0;
    uint8_t *buf = jpeg_alloc_decoder_mem((size_t)pw * ph * 2, &oc, &cap);
    if (!buf) { ESP_LOGW(TAG, "no memory for %lux%lu", (unsigned long)pw, (unsigned long)ph); return false; }

    jpeg_decode_cfg_t dc = { .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                             .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
                             .conv_std = JPEG_YUV_RGB_CONV_STD_BT601 };
    uint32_t got = 0;
    if (jpeg_decoder_process(s_dec, &dc, jpg, len, buf, cap, &got) != ESP_OK || !got) {
        ESP_LOGW(TAG, "decode failed");
        free(buf);
        return false;
    }
    out->buf = buf;
    out->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    out->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    out->dsc.header.w = info.width;
    out->dsc.header.h = info.height;
    out->dsc.header.stride = pw * 2;
    out->dsc.data_size = pw * 2 * info.height;
    out->dsc.data = buf;
    return true;
}

bool ball_jpeg_load(const char *path, ball_jpeg_t *out)
{
    memset(out, 0, sizeof(*out));
    struct stat st;
    if (stat(path, &st) != 0) { ESP_LOGW(TAG, "can't find %s", path); return false; }
    if (st.st_size <= 0 || st.st_size > MAX_FILE) { ESP_LOGW(TAG, "%s is too big (%ld bytes)", path, (long)st.st_size); return false; }
    jpeg_decode_memory_alloc_cfg_t ic = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    size_t cap = 0;
    uint8_t *in = jpeg_alloc_decoder_mem(st.st_size, &ic, &cap);
    if (!in) return false;
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(in, 1, st.st_size, f) : 0;
    if (f) fclose(f);
    bool ok = n == (size_t)st.st_size && ball_jpeg_decode_mem(in, n, out);
    free(in);
    if (ok) ESP_LOGI(TAG, "%s: %lux%lu", path, (unsigned long)out->dsc.header.w, (unsigned long)out->dsc.header.h);
    return ok;
}

void ball_jpeg_free(ball_jpeg_t *img)
{
    if (img->buf) free(img->buf);
    memset(img, 0, sizeof(*img));
}
