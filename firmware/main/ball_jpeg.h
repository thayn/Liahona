#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

/* A decoded picture: dsc points into buf (RGB565). Free with ball_jpeg_free. */
typedef struct {
    lv_image_dsc_t dsc;
    uint8_t       *buf;
} ball_jpeg_t;

bool ball_jpeg_load(const char *path, ball_jpeg_t *out);                 /* file on the SD card */
bool ball_jpeg_decode_mem(const uint8_t *jpg, size_t len, ball_jpeg_t *out);
void ball_jpeg_free(ball_jpeg_t *img);
