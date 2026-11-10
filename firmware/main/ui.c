/*
 * Round 720x720 user interface (LVGL 9).
 *
 *  - Compass screen (ui_compass.c): concept-style dial and spindles.
 *    Press and hold anywhere to open the self-test.
 *  - Self-test screen: one row per part with PASS / FAIL / waiting.
 *
 * Everything stays inside the visible circle (~600 px usable).
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "liahona.h"
#include "ui_fonts.h"

#define SCR_W 720
#define SCR_H 720
#define CX    (SCR_W / 2)
#define CY    (SCR_H / 2)

#define COL_BG      lv_color_hex(0x0B0805)   /* near-black warm brown */
#define COL_GOLD    lv_color_hex(0xF2C766)
#define COL_TAN     lv_color_hex(0x6B4A1B)
#define COL_TEXT    lv_color_hex(0xEDE3CC)
#define COL_PASS    lv_color_hex(0x58C46B)
#define COL_FAIL    lv_color_hex(0xE0533D)
#define COL_WAIT    lv_color_hex(0x9C9184)

static lv_obj_t *scr_compass, *scr_test;


/* self-test rows */
enum { R_TOUCH, R_I2C, R_MAG, R_GPS, R_NFC, R_LORA, R_LID, R_LEDS, R_BATT, R_SD, R_AUDIO, R_FW, R_COUNT };
static const char *row_name[R_COUNT] = {
    "Touch", "I2C bus", "Compass", "GPS", "NFC", "LoRa", "Lid switch", "LEDs",
    "Battery", "SD card", "Speaker", "Firmware",
};
static lv_obj_t *row_dot[R_COUNT], *row_val[R_COUNT];

static int s_led_mode = 1;

/* ------------------------------------------------------------------ */

static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 124, 56);
    lv_obj_set_style_border_color(b, COL_GOLD, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, COL_TAN, 0);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = make_label(b, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void any_touch_cb(lv_event_t *e)
{
    STATE_LOCK(); g_state.touch = TEST_PASS; STATE_UNLOCK();
}

static void go_test_cb(lv_event_t *e)    { lv_screen_load_anim(scr_test, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false); }
static void go_compass_cb(lv_event_t *e) { lv_screen_load_anim(scr_compass, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false); }
static void beep_cb(lv_event_t *e)       { audio_test_beep(); }
static void mark_cb(lv_event_t *e)
{
    /* Bench demo: remember where you are standing as "the station",
     * then walk away and the gold spindle points back here. */
    STATE_LOCK();
    bool fix = g_state.gps_fix;
    if (fix) {
        g_state.target_lat = g_state.gps_lat;
        g_state.target_lon = g_state.gps_lon;
        g_state.target_set = true;
    }
    STATE_UNLOCK();
    if (fix) go_compass_cb(e);
    else audio_test_beep();
}
static void leds_cb(lv_event_t *e)
{
    /* Each tap: test cycle -> off -> gold glow -> test cycle ... */
    s_led_mode = (s_led_mode == 1) ? 2 : (s_led_mode == 2) ? 0 : 1;
    leds_set_mode(s_led_mode);
}

static void build_test(void)
{
    scr_test = make_screen();
    lv_obj_add_event_cb(scr_test, any_touch_cb, LV_EVENT_PRESSED, NULL);

    lv_obj_t *title = make_label(scr_test, &font_cinzel_30, COL_GOLD);
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_label_set_text(title, "SELF-TEST");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    const int top = 118, step = 33, x_dot = 140, x_name = 165, x_val = 330;
    for (int i = 0; i < R_COUNT; i++) {
        int y = top + i * step;
        row_dot[i] = lv_obj_create(scr_test);
        lv_obj_set_size(row_dot[i], 16, 16);
        lv_obj_set_style_radius(row_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(row_dot[i], 0, 0);
        lv_obj_set_pos(row_dot[i], x_dot, y + 5);
        lv_obj_remove_flag(row_dot[i], LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *n = make_label(scr_test, &lv_font_montserrat_20, COL_TEXT);
        lv_label_set_text(n, row_name[i]);
        lv_obj_set_pos(n, x_name, y);

        row_val[i] = make_label(scr_test, &lv_font_montserrat_20, COL_WAIT);
        lv_obj_set_style_text_align(row_val[i], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(row_val[i], 290);
        lv_label_set_long_mode(row_val[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(row_val[i], x_val, y);
    }

    static const char *names[4] = { "Beep", "LEDs", "Mark", "Back" };
    lv_event_cb_t cbs[4] = { beep_cb, leds_cb, mark_cb, go_compass_cb };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_button(scr_test, names[i], cbs[i]);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, -201 + i * 134, -110);
    }
}

/* ------------------------------------------------------------------ */

static void set_row(int r, test_result_t res, const char *text)
{
    lv_color_t c = res == TEST_PASS ? COL_PASS : res == TEST_FAIL ? COL_FAIL : COL_WAIT;
    lv_obj_set_style_bg_color(row_dot[r], c, 0);
    lv_obj_set_style_text_color(row_val[r], res == TEST_FAIL ? COL_FAIL : COL_TEXT, 0);
    lv_label_set_text(row_val[r], text);
}

static const char *res_word(test_result_t r)
{
    return r == TEST_PASS ? "PASS" : r == TEST_FAIL ? "FAIL" : r == TEST_NA ? "n/a" : "...";
}

static void refresh_cb(lv_timer_t *t)
{
    liahona_state_t s;
    STATE_LOCK(); s = g_state; STATE_UNLOCK();
    char buf[64];

    if (lv_screen_active() != scr_test) return;

    /* --- self-test --- */
    set_row(R_TOUCH, s.touch, s.touch == TEST_PASS ? "PASS" : "tap the screen");

    {
        char a[48] = "";
        for (int i = 0; i < s.i2c_count && strlen(a) < 40; i++) {
            char h[6]; snprintf(h, sizeof(h), "%s%02X", i ? " " : "", s.i2c_addrs[i]); strlcat(a, h, sizeof(a));
        }
        set_row(R_I2C, s.i2c_count ? TEST_PASS : TEST_PENDING, s.i2c_count ? a : "scanning");
    }

    if (s.mag == TEST_PASS) snprintf(buf, sizeof(buf), "%d°  (%.2f %.2f %.2f)", (int)s.heading_deg, s.mag_x, s.mag_y, s.mag_z);
    else snprintf(buf, sizeof(buf), "%s", res_word(s.mag));
    set_row(R_MAG, s.mag, buf);

    if (s.gps != TEST_PASS) snprintf(buf, sizeof(buf), "%s", res_word(s.gps));
    else if (s.gps_fix) snprintf(buf, sizeof(buf), "%.5f %.5f  %d sats", s.gps_lat, s.gps_lon, s.gps_sats);
    else snprintf(buf, sizeof(buf), "no fix, %d in view (%s)", s.gps_in_view, s.gps_time[0] ? s.gps_time : "--");
    set_row(R_GPS, s.gps, buf);

    if (s.nfc != TEST_PASS) snprintf(buf, sizeof(buf), "%s", res_word(s.nfc));
    else if (s.nfc_tag_present) snprintf(buf, sizeof(buf), "tag %s", s.nfc_uid);
    else snprintf(buf, sizeof(buf), "PASS - hold a tag near");
    set_row(R_NFC, s.nfc, buf);

    if (s.lora == TEST_PENDING) snprintf(buf, sizeof(buf), "...");
    else snprintf(buf, sizeof(buf), "%s  (sync %02X %02X)", res_word(s.lora), s.lora_sync[0], s.lora_sync[1]);
    set_row(R_LORA, s.lora, buf);

    snprintf(buf, sizeof(buf), "%s  (%d changes)", s.lid_closed ? "CLOSED" : "OPEN", s.lid_changes);
    set_row(R_LID, s.lid_changes > 0 ? TEST_PASS : TEST_PENDING, buf);

    set_row(R_LEDS, s.leds, s.leds == TEST_PASS ? "check colours by eye" : res_word(s.leds));

    if (s.batt == TEST_PASS) snprintf(buf, sizeof(buf), "%.2f V", s.batt_v);
    else snprintf(buf, sizeof(buf), "not wired");
    set_row(R_BATT, s.batt, buf);

    if (s.sd == TEST_PASS) snprintf(buf, sizeof(buf), "%llu MB free, %s", s.sd_free_mb,
                                    s.manifest_found ? "journey found" : "no journey");
    else snprintf(buf, sizeof(buf), "%s", s.sd == TEST_FAIL ? "FAIL - FAT32 card?" : "...");
    set_row(R_SD, s.sd, buf);

    set_row(R_AUDIO, s.audio, s.audio == TEST_PASS ? "PASS" : s.audio == TEST_FAIL ? "FAIL" : "press Beep");

    snprintf(buf, sizeof(buf), "%s  [%s]", s.fw_version, s.ota_partition);
    set_row(R_FW, TEST_PASS, buf);
}

static void compass_timer_cb(lv_timer_t *t)
{
    if (lv_screen_active() != scr_compass) return;
    liahona_state_t s;
    STATE_LOCK(); s = g_state; STATE_UNLOCK();
    ui_compass_tick(&s);
}

static void long_press_cb(lv_event_t *e) { go_test_cb(e); }

void ui_start(void)
{
    bsp_display_lock(0);
    scr_compass = make_screen();
    ui_compass_build(scr_compass);
    lv_obj_add_event_cb(scr_compass, any_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr_compass, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    build_test();
    lv_screen_load(scr_compass);
    lv_timer_create(refresh_cb, 200, NULL);
    lv_timer_create(compass_timer_cb, 33, NULL);
    bsp_display_unlock();
}
