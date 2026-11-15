/*
 * Journey screen: "the writing upon the ball". Shows the current stage, task text,
 * answer buttons, hints, countdowns and roll-call progress from the journey engine.
 *
 * Layout: one centred, scrollable column sized for the round 720 px glass.
 * Long-press anywhere for the self-test page; "Compass" opens the compass when a
 * place has coordinates (tap the compass to come back).
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "liahona.h"
#include "journey_port.h"
#include "ui_fonts.h"

#define C_BG      0x0E0B07
#define C_GOLD    0xE8C66A
#define C_GOLD_D  0x8F6A24
#define C_TAN     0x3A2D18
#define C_CREAM   0xEDE3CC
#define C_MUTED   0xB8AB8F
#define C_DIM     0x5E554A
#define C_GOOD    0x8FC46A
#define C_WARN    0xE0794F

static lv_obj_t *scr, *col;
static lv_obj_t *lbl_step, *lbl_title, *lbl_head, *lbl_ref, *lbl_body, *lbl_count, *lbl_status, *lbl_hint;
static lv_obj_t *box_choices, *row_actions, *btn_hint, *btn_replay, *btn_cont, *lbl_cont, *btn_compass;
static uint32_t s_version = 0xFFFFFFFF;
static jv_mode_t s_mode = (jv_mode_t)-1;
static bool s_shown_once;

/* LVGL's fonts here are ASCII: fold common typography so nothing shows as a box. */
static void ascii(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)in;
    while (*s && o + 4 < cap) {
        if (s[0] < 0x80) { out[o++] = (char)*s++; continue; }
        if (s[0] == 0xE2 && s[1] == 0x80) {
            unsigned char c = s[2];
            if (c == 0x98 || c == 0x99) out[o++] = '\'';
            else if (c == 0x9C || c == 0x9D) out[o++] = '"';
            else if (c == 0x93 || c == 0x94) out[o++] = '-';
            else if (c == 0xA6) { out[o++] = '.'; out[o++] = '.'; out[o++] = '.'; }
            s += 3;
            continue;
        }
        if (s[0] == 0xC2 && s[1] == 0xA0) { out[o++] = ' '; s += 2; continue; }
        if (s[0] == 0xC3 && s[1] >= 0xA0 && s[1] <= 0xBF) {          /* accented lower-case vowels */
            static const char map[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyby";
            out[o++] = map[s[1] - 0xA0];
            s += 2;
            continue;
        }
        s++;                                                       /* skip anything else */
        while ((*s & 0xC0) == 0x80) s++;
    }
    out[o] = 0;
}

static void set_text(lv_obj_t *l, const char *t)
{
    static char buf[800];
    ascii(t ? t : "", buf, sizeof(buf));
    if (strcmp(lv_label_get_text(l), buf)) lv_label_set_text(l, buf);
    if (buf[0]) lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, uint32_t color, int width)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, width);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int w, lv_event_cb_t cb, void *ud, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_width(b, w);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(b, 64, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_TAN), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_GOLD_D), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_hex(C_GOLD), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_radius(b, 32, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = label(b, &lv_font_montserrat_28, C_CREAM, w - 40);
    char buf[96];
    ascii(text, buf, sizeof(buf));
    lv_label_set_text(l, buf);
    lv_obj_center(l);
    if (lbl_out) *lbl_out = l;
    return b;
}

/* ---------------------------------------------------------------- callbacks */
static void choice_cb(lv_event_t *e) { journey_ui_choice((int)(intptr_t)lv_event_get_user_data(e)); }
static void cont_cb(lv_event_t *e)   { journey_ui_continue(); }
static void hint_cb(lv_event_t *e)   { journey_ui_hint(); }
static void replay_cb(lv_event_t *e) { journey_ui_replay(); }
static void compass_cb(lv_event_t *e){ ui_show_compass(); }
static void long_cb(lv_event_t *e)   { ui_show_test(); }

static void tick_cb(lv_timer_t *t) { ui_journey_tick(); }

/* ---------------------------------------------------------------- build */
void ui_journey_build(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, long_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* thin gold ring, like the compass rim */
    lv_obj_t *ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 712, 712);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(C_GOLD_D), 0);
    lv_obj_set_style_border_width(ring, 6, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 540, 640);
    lv_obj_center(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_set_style_pad_top(col, 70, 0);
    lv_obj_set_style_pad_bottom(col, 90, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(col, long_cb, LV_EVENT_LONG_PRESSED, NULL);

    lbl_step  = label(col, &font_inter_22, C_MUTED, 420);
    lv_obj_set_style_text_letter_space(lbl_step, 4, 0);
    lbl_title = label(col, &font_cinzel_30, C_GOLD, 480);
    lbl_head  = label(col, &font_inter_22, C_MUTED, 460);
    lv_obj_set_style_text_letter_space(lbl_head, 3, 0);
    lbl_count = label(col, &font_cinzel_56, C_GOLD, 300);
    lbl_ref   = label(col, &font_cinzel_30, C_CREAM, 460);
    lbl_body  = label(col, &lv_font_montserrat_28, C_CREAM, 500);
    lv_obj_set_style_text_line_space(lbl_body, 6, 0);

    box_choices = lv_obj_create(col);
    lv_obj_remove_style_all(box_choices);
    lv_obj_set_size(box_choices, 500, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box_choices, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box_choices, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box_choices, 12, 0);

    lbl_status = label(col, &font_inter_22, C_GOLD, 460);
    lbl_hint   = label(col, &lv_font_montserrat_20, C_MUTED, 460);

    row_actions = lv_obj_create(col);
    lv_obj_remove_style_all(row_actions);
    lv_obj_set_size(row_actions, 500, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_actions, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row_actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_actions, 12, 0);
    lv_obj_set_style_pad_row(row_actions, 12, 0);
    btn_hint   = button(row_actions, "Hint", 150, hint_cb, NULL, NULL);
    btn_replay = button(row_actions, "Play again", 210, replay_cb, NULL, NULL);
    btn_cont   = button(row_actions, "Continue", 300, cont_cb, NULL, &lbl_cont);

    btn_compass = button(scr, "Compass", 200, compass_cb, NULL, NULL);
    lv_obj_align(btn_compass, LV_ALIGN_BOTTOM_MID, 0, -36);

    lv_timer_create(tick_cb, 100, NULL);
}

/* ---------------------------------------------------------------- update */
static void rebuild(const jv_view_t *v)
{
    char step[96] = "";
    if (v->stage_count) snprintf(step, sizeof(step), "STEP %d OF %d", v->stage_no, v->stage_count);
    set_text(lbl_step, v->mode == JV_ERROR || v->mode == JV_DONE ? "" : step);
    set_text(lbl_title, v->title);
    char head[96];
    snprintf(head, sizeof(head), "%s", v->heading);
    for (char *p = head; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    set_text(lbl_head, head);
    set_text(lbl_ref, v->ref);
    set_text(lbl_body, v->body);
    set_text(lbl_hint, v->hint);

    lv_obj_clean(box_choices);
    for (int i = 0; i < v->nchoices; i++) button(box_choices, v->choices[i], 480, choice_cb, (void *)(intptr_t)i, NULL);
    show(box_choices, v->nchoices > 0);

    if (v->can_continue) { char b[40]; ascii(v->continue_label, b, sizeof(b)); lv_label_set_text(lbl_cont, b); }

    bool dark = v->mode == JV_DARK;
    lv_obj_set_style_bg_color(scr, lv_color_hex(dark ? 0x000000 : C_BG), 0);
    lv_obj_set_style_text_color(lbl_body, lv_color_hex(dark ? C_DIM : C_CREAM), 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(dark ? C_DIM : C_GOLD), 0);
    lv_obj_set_style_text_color(lbl_count, lv_color_hex(dark ? C_MUTED : C_GOLD), 0);
    lv_obj_scroll_to_y(col, 0, LV_ANIM_OFF);
}

void ui_journey_tick(void)
{
    if (!scr) return;
    jv_view_t v;
    journey_ui_view(&v);

    if (!s_shown_once && v.mode != JV_NONE) {           /* a journey appeared: show it */
        s_shown_once = true;
        if (!ui_journey_is_active_screen()) ui_journey_show();
    }
    if (v.version != s_version || v.mode != s_mode) {
        s_version = v.version;
        s_mode = v.mode;
        rebuild(&v);
    }

    /* things that change with time, without a new version */
    set_text(lbl_status, v.status);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(
        strstr(v.status, "Not") || strstr(v.status, "false") || strstr(v.status, "ran out") ? C_WARN :
        strstr(v.status, "Correct") || strstr(v.status, "shines") ? C_GOOD : C_GOLD), 0);
    char cd[16] = "";
    if (v.countdown_s >= 0) snprintf(cd, sizeof(cd), "%d:%02d", v.countdown_s / 60, v.countdown_s % 60);
    set_text(lbl_count, cd);
    show(btn_hint, v.can_hint);
    show(btn_replay, v.can_replay);
    show(btn_cont, v.can_continue);
    show(row_actions, v.can_hint || v.can_replay || v.can_continue);
    show(btn_compass, v.has_target);
}

void ui_journey_show(void)
{
    if (scr) lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 250, 0, false);
}

bool ui_journey_is_active_screen(void) { return scr && lv_screen_active() == scr; }
