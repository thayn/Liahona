/*
 * Compass calibration screen (self-test -> tap the Compass row).
 *
 *  1. Hold the ball level and turn it slowly, two full turns. Each of the
 *     36 dots round the edge lights up once that direction has been seen.
 *  2. Save. The centre and stretch of the circle the readings make are
 *     kept in NVS (sensors.c applies them to every reading).
 *  3. Check: turn the ball clockwise and the number should go up. If it
 *     goes down, tap Flip.
 *  4. Point the top of the screen at north (phone compass) and tap Set N.
 *
 * Do it with the ball fully assembled: the battery, speaker magnet and
 * screws are what the calibration corrects for.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "board_pins.h"
#include "liahona.h"
#include "ui_fonts.h"

#define CX 360
#define CY 360
#define NSEC        36          /* 10 degree sectors */
#define NEED_SEC    34          /* allow a couple of gaps */
#define MIN_SPAN    0.12f       /* gauss; the earth's horizontal field is ~0.2 G, so a turn spans ~0.4 */
#define MAX_SAMPLES 900
#define MIN_STEP    0.004f      /* only keep a reading once the field has moved this much */

#define COL_BG   lv_color_hex(0x0B0805)
#define COL_GOLD lv_color_hex(0xF2C766)
#define COL_TAN  lv_color_hex(0x6B4A1B)
#define COL_TEXT lv_color_hex(0xEDE3CC)
#define COL_DIM  lv_color_hex(0x3A2E20)
#define COL_PASS lv_color_hex(0x58C46B)

static const char *TAG = "cal";

static lv_obj_t *scr, *dot[NSEC], *lbl_head, *lbl_status, *lbl_help, *btn_save;
static float sx[MAX_SAMPLES], sy[MAX_SAMPLES];
static int   n_samples;
static bool  collecting, ready, beeped;
static float c_off[2], c_scale[2];

static lv_obj_t *label(const lv_font_t *f, lv_color_t c, int w)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (w) { lv_obj_set_width(l, w); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); }
    return l;
}

static lv_obj_t *button(const char *text, lv_event_cb_t cb, int x, int y)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_size(b, 124, 56);
    lv_obj_set_style_border_color(b, COL_GOLD, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, COL_TAN, 0);
    lv_obj_set_style_bg_color(b, COL_DIM, LV_STATE_DISABLED);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, y);
    return b;
}

static void start_collecting(void)
{
    n_samples = 0;
    collecting = true;
    ready = beeped = false;
    lv_obj_add_state(btn_save, LV_STATE_DISABLED);
    for (int i = 0; i < NSEC; i++) lv_obj_set_style_bg_color(dot[i], COL_DIM, 0);
    lv_label_set_text(lbl_help, "Hold the ball level and turn it slowly,\ntwo full turns.");
}

/* ---- buttons ---- */

static void again_cb(lv_event_t *e) { start_collecting(); }

static void save_cb(lv_event_t *e)
{
    if (!ready) return;
    mag_cal_t c;
    mag_cal_get(&c);
    c.valid = 1;
    c.off[0] = c_off[0];   c.off[1] = c_off[1];
    c.scale[0] = c_scale[0]; c.scale[1] = c_scale[1];
    bool ok = mag_cal_set(&c);
    collecting = false;
    lv_obj_add_state(btn_save, LV_STATE_DISABLED);
    lv_label_set_text(lbl_status, ok ? "Saved" : "Could not save (NVS)");
    lv_label_set_text(lbl_help, "Turn clockwise: the number should go UP.\n"
                                "If it goes down, tap Flip.\n"
                                "Then point the top of the screen north and tap Set N.");
    audio_test_beep();
}

static void north_cb(lv_event_t *e)
{
    mag_cal_t c;
    STATE_LOCK(); float gx = g_state.mag_x, gy = g_state.mag_y; bool ok = g_state.mag == TEST_PASS; STATE_UNLOCK();
    if (!ok) return;
    mag_cal_get(&c);
    c.north_deg = 0;
    float h = mag_heading(gx, gy, &c);              /* magnetic, before any north correction */
    /* the compass screen shows heading + declination; make that read 0 now */
    c.north_deg = fmodf(-(h + MAG_DECLINATION_DEG) + 720.0f, 360.0f);
    mag_cal_set(&c);
    lv_label_set_text(lbl_status, "North set");
    audio_test_beep();
}

static void flip_cb(lv_event_t *e)
{
    mag_cal_t c;
    mag_cal_get(&c);
    c.mirror = !c.mirror;
    c.north_deg = 0;                                /* north has to be set again after a flip */
    mag_cal_set(&c);
    lv_label_set_text(lbl_status, c.mirror ? "Flipped - now Set N" : "Unflipped - now Set N");
}

static void back_cb(lv_event_t *e)
{
    collecting = false;
    ui_show_test();
}

/* ---- live update ---- */

static void tick_cb(lv_timer_t *t)
{
    if (lv_screen_active() != scr) return;
    STATE_LOCK();
    float gx = g_state.mag_x, gy = g_state.mag_y;
    bool  mag_ok = g_state.mag == TEST_PASS;
    float hdg = g_state.heading_deg;
    STATE_UNLOCK();

    char buf[80];
    if (!mag_ok) {
        lv_label_set_text(lbl_head, "--");
        lv_label_set_text(lbl_status, "Compass not found");
        return;
    }
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)(fmodf(hdg + MAG_DECLINATION_DEG + 360.0f, 360.0f) + 0.5f) % 360);
    lv_label_set_text(lbl_head, buf);
    if (!collecting) return;

    if (n_samples < MAX_SAMPLES &&
        (n_samples == 0 || fabsf(gx - sx[n_samples - 1]) + fabsf(gy - sy[n_samples - 1]) > MIN_STEP)) {
        sx[n_samples] = gx; sy[n_samples] = gy; n_samples++;
    }
    if (n_samples < 4) { lv_label_set_text(lbl_status, "Start turning"); return; }

    float x0 = sx[0], x1 = sx[0], y0 = sy[0], y1 = sy[0];
    for (int i = 1; i < n_samples; i++) {
        x0 = fminf(x0, sx[i]); x1 = fmaxf(x1, sx[i]);
        y0 = fminf(y0, sy[i]); y1 = fmaxf(y1, sy[i]);
    }
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, wx = x1 - x0, wy = y1 - y0;

    /* which directions have we seen, measured from the current centre */
    bool seen[NSEC] = { 0 };
    for (int i = 0; i < n_samples; i++) {
        float a = atan2f(sy[i] - cy, sx[i] - cx) * 180.0f / (float)M_PI;
        if (a < 0) a += 360.0f;
        seen[(int)(a / (360.0f / NSEC)) % NSEC] = true;
    }
    int covered = 0;
    for (int i = 0; i < NSEC; i++) {
        covered += seen[i];
        lv_obj_set_style_bg_color(dot[i], seen[i] ? COL_GOLD : COL_DIM, 0);
    }

    ready = covered >= NEED_SEC && wx > MIN_SPAN && wy > MIN_SPAN;
    if (ready) {
        float r = (wx + wy) / 4;                    /* average radius */
        c_off[0] = cx;          c_off[1] = cy;
        c_scale[0] = r / (wx / 2); c_scale[1] = r / (wy / 2);
        lv_obj_remove_state(btn_save, LV_STATE_DISABLED);
        snprintf(buf, sizeof(buf), "Done - tap Save  (%.2f x %.2f G)", wx, wy);
        if (!beeped) {
            beeped = true;
            audio_test_beep();
            ESP_LOGI(TAG, "ready: centre %.3f %.3f  span %.3f %.3f  %d samples", cx, cy, wx, wy, n_samples);
        }
    } else if (n_samples >= MAX_SAMPLES) {
        snprintf(buf, sizeof(buf), "Too slow or not level - tap Again");
    } else if (covered >= NEED_SEC) {
        snprintf(buf, sizeof(buf), "Weak signal (%.2f x %.2f G) - keep away from metal", wx, wy);
    } else {
        snprintf(buf, sizeof(buf), "Keep turning: %d of %d", covered, NSEC);
    }
    lv_label_set_text(lbl_status, buf);
}

void ui_cal_build(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < NSEC; i++) {
        /* sector 0 = the field's +x direction; just a ring of progress dots */
        float a = (i * (360.0f / NSEC) - 90.0f) * (float)M_PI / 180.0f;
        dot[i] = lv_obj_create(scr);
        lv_obj_set_size(dot[i], 16, 16);
        lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot[i], 0, 0);
        lv_obj_set_style_bg_color(dot[i], COL_DIM, 0);
        lv_obj_remove_flag(dot[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(dot[i], CX + (int)(285 * cosf(a)) - 8, CY + (int)(285 * sinf(a)) - 8);
    }

    lv_obj_t *title = label(&font_cinzel_30, COL_GOLD, 0);
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_label_set_text(title, "COMPASS");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 90);

    lbl_head = label(&font_cinzel_56, COL_TEXT, 0);
    lv_label_set_text(lbl_head, "--");
    lv_obj_align(lbl_head, LV_ALIGN_TOP_MID, 0, 140);

    lbl_status = label(&lv_font_montserrat_20, COL_GOLD, 480);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 225);

    lbl_help = label(&lv_font_montserrat_20, COL_TEXT, 480);
    lv_obj_align(lbl_help, LV_ALIGN_TOP_MID, 0, 270);

    button("Again", again_cb, -67, -190);
    btn_save = button("Save", save_cb, 67, -190);
    button("Set N", north_cb, -134, -115);
    button("Flip", flip_cb, 0, -115);
    button("Back", back_cb, 134, -115);

    lv_timer_create(tick_cb, 100, NULL);
    start_collecting();
    collecting = false;
}

void ui_cal_show(void)
{
    start_collecting();
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}
