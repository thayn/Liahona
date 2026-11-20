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
#include "update.h"
#include "liahona_proto.h"
#include "esp_timer.h"
#include "liahona_proto.h"
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
/* "Back" from the self-test: to the journey when one is running, else the compass */
static void back_cb(lv_event_t *e)
{
    bool j;
    STATE_LOCK(); j = g_state.journey_active; STATE_UNLOCK();
    if (j) ui_journey_show(); else go_compass_cb(e);
}
void ui_show_test(void)    { go_test_cb(NULL); }
void ui_show_compass(void) { go_compass_cb(NULL); }
static void beep_cb(lv_event_t *e)       { audio_test_beep(); }

/* GPS noise test: screen dark for 60 s, GPS keeps listening (watch the log). */
static lv_timer_t *s_quiet_timer;
static void quiet_end(lv_timer_t *t)
{
    bsp_display_backlight_on();
    STATE_LOCK(); g_state.quiet_mode = false; STATE_UNLOCK();
    lv_timer_delete(t);
    s_quiet_timer = NULL;
}
static void quiet_cb(lv_event_t *e)
{
    if (s_quiet_timer) return;
    STATE_LOCK(); g_state.quiet_mode = true; STATE_UNLOCK();
    bsp_display_backlight_off();
    s_quiet_timer = lv_timer_create(quiet_end, 60000, NULL);
}
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
static void lora_tap_cb(lv_event_t *e)
{
    lora_send_status_now();
    audio_test_beep();
}

/* tap the Firmware row = Wi-Fi update now (same as Update on the Field dashboard) */
static void update_tap_cb(lv_event_t *e)
{
    audio_test_beep();
    update_start();
}

/* tap the Compass row = calibration screen (ui_cal.c) */
static void cal_tap_cb(lv_event_t *e) { ui_cal_show(); }

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

    /* tap the LoRa row = send a status report now (only if TX is enabled) */
    lv_obj_add_flag(row_val[R_LORA], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_val[R_LORA], lora_tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(row_val[R_FW], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_val[R_FW], update_tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(row_val[R_MAG], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_val[R_MAG], cal_tap_cb, LV_EVENT_CLICKED, NULL);

    /* tap the GPS row = screen-off noise test */
    lv_obj_add_flag(row_val[R_GPS], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row_val[R_GPS], quiet_cb, LV_EVENT_CLICKED, NULL);

    static const char *names[4] = { "Beep", "LEDs", "Mark", "Back" };
    lv_event_cb_t cbs[4] = { beep_cb, leds_cb, mark_cb, back_cb };
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

    if (s.mag == TEST_PASS) snprintf(buf, sizeof(buf), "%d°  %s", (int)s.heading_deg,
                                     s.mag_cal_valid ? "calibrated" : "tap to calibrate");
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

    /* LoRa: found -> listening -> what it heard. Tap the row to send a status report. */
    if (s.lora == TEST_PENDING) snprintf(buf, sizeof(buf), "...");
    else if (s.lora != TEST_PASS) snprintf(buf, sizeof(buf), "%s  (sync %02X %02X)", res_word(s.lora), s.lora_sync[0], s.lora_sync[1]);
    else if (!s.lora_ready) snprintf(buf, sizeof(buf), "found, setup err %d", s.lora_err);
    else if (s.lora_rx_count == 0) snprintf(buf, sizeof(buf), "#%u listening, TX %s%s", s.ball_id,
                                            s.lora_tx_enabled ? "on" : "off", s.lora_tcxo ? "" : " (xtal)");
    else snprintf(buf, sizeof(buf), "#%u rx %lu tx %lu  %d dBm", s.ball_id, (unsigned long)s.lora_rx_count,
                  (unsigned long)s.lora_tx_count, s.lora_last_rssi);
    {   /* show a new leader message / command on the row for 10 s */
        static uint32_t seen_msg, seen_cmd;
        static int64_t show_until;
        static char note[64];
        int64_t now = esp_timer_get_time() / 1000;
        if (s.leader_msg_count != seen_msg) {
            seen_msg = s.leader_msg_count;
            snprintf(note, sizeof(note), "msg: %s", s.leader_msg);
            show_until = now + 10000;
        }
        if (s.leader_cmd_count != seen_cmd) {
            seen_cmd = s.leader_cmd_count;
            snprintf(note, sizeof(note), "cmd: %s %u", lp_cmd_name(s.leader_cmd), s.leader_cmd_arg);
            show_until = now + 10000;
        }
        if (now < show_until) strlcpy(buf, note, sizeof(buf));
    }
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

    if (s.update_text[0])
        snprintf(buf, sizeof(buf), "%s: %s", s.fw_version, s.update_text);
    else
        snprintf(buf, sizeof(buf), "%s  [%s]  tap to update", s.fw_version, s.ota_partition);
    set_row(R_FW, s.update_state == LP_UPD_FAILED ? TEST_FAIL : TEST_PASS, buf);
}

static void compass_timer_cb(lv_timer_t *t)
{
    if (lv_screen_active() != scr_compass) return;
    liahona_state_t s;
    STATE_LOCK(); s = g_state; STATE_UNLOCK();
    ui_compass_tick(&s);
}

static void long_press_cb(lv_event_t *e) { go_test_cb(e); }
/* a short tap on the compass goes back to the journey, if one is running */
static void compass_click_cb(lv_event_t *e)
{
    bool j;
    STATE_LOCK(); j = g_state.journey_active; STATE_UNLOCK();
    if (j) ui_journey_show();
}

void ui_start(void)
{
    /* Wait as long as it takes for the LVGL lock (-1 = forever). A timeout of 0 means
     * "don't wait" with this BSP, and it returns esp_err_t (ESP_OK = 0), not bool. */
    bsp_display_lock(-1);
    scr_compass = make_screen();
    ui_compass_build(scr_compass);
    lv_obj_add_event_cb(scr_compass, any_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr_compass, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(scr_compass, compass_click_cb, LV_EVENT_SHORT_CLICKED, NULL);
    build_test();
    ui_journey_build();
    ui_cal_build();
    lv_screen_load(scr_compass);
    lv_timer_create(refresh_cb, 200, NULL);
    lv_timer_create(compass_timer_cb, 33, NULL);
    bsp_display_unlock();
}
