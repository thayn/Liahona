/*
 * Compass screen, drawn to match the Liahona concept renderings:
 *
 *  - gold rim, dark warm radial background, faint glow ring (drawn once
 *    into an image at start-up, with dithering so the gradient doesn't band)
 *  - a dial with 36 ticks and N / E / S / W that turns so N points north
 *  - thin cream spindle   -> north
 *  - gold glowing spindle -> the station (or a marked spot); with no station
 *    it drifts gently near north, "waiting"
 *  - distance in Cinzel ("340 m") with a small caption ("TO MARKED SPOT")
 *
 * All sizes are the concept's 380 px drawing scaled to the 720 px screen.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "board_pins.h"
#include "liahona.h"
#include "ui_fonts.h"

#define W   720
#define CX  360
#define CY  360
#define DEG (float)(M_PI / 180.0)

/* concept palette */
#define C_GLOW      0xF2C766
#define C_CREAM     0xEDE3CC
#define C_SPINDLE_N 0xE9DFC6
#define C_MUTED     0xB8AB8F
#define C_HUB       0xF3D88F
#define C_HUB_EDGE  0x6B4A1B

static lv_obj_t *s_overlay, *s_lbl_big, *s_lbl_small;
static lv_draw_buf_t *s_bg;

/* animated state (degrees, screen angle clockwise from "up") */
static float s_heading = 0;      /* smoothed compass heading */
static float s_target  = 0;      /* smoothed gold-spindle angle */
static float s_drawn_heading = 999, s_drawn_target = 999;

/* ------------------------------------------------------------------ */
/* Background: rendered once, pixel by pixel                           */

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float mixf(float a, float b, float t)   { return a + (b - a) * t; }

typedef struct { float r, g, b; } rgbf;
static rgbf hex(uint32_t h) { return (rgbf){ (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF }; }
static rgbf mixc(rgbf a, rgbf b, float t) { return (rgbf){ mixf(a.r, b.r, t), mixf(a.g, b.g, t), mixf(a.b, b.b, t) }; }

/* coverage of a ring edge at radius e (1 inside, 0 outside, 1-px soft edge) */
static inline float inside(float r, float e) { return clampf(e - r + 0.5f, 0, 1); }

static void build_background(void)
{
    s_bg = lv_draw_buf_create(W, W, LV_COLOR_FORMAT_RGB565, 0);
    if (!s_bg) return;
    const uint32_t stride = s_bg->header.stride;

    const rgbf gold = hex(0xB98F3E), black = hex(0x050403);
    const rgbf g0 = hex(0x2B1D0B), g1 = hex(0x090604), glow = hex(C_GLOW);
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };

    for (int y = 0; y < W; y++) {
        uint16_t *row = (uint16_t *)(s_bg->data + y * stride);
        for (int x = 0; x < W; x++) {
            float dx = x + 0.5f - CX, dy = y + 0.5f - CY;
            float r = sqrtf(dx * dx + dy * dy);

            /* warm radial gradient, centre a little above the middle */
            float gy = dy + 33.0f;
            float t = clampf(sqrtf(dx * dx + gy * gy) / 391.0f, 0, 1);
            rgbf c = mixc(g0, g1, t);

            /* faint glow ring */
            float ring = clampf(1.6f - fabsf(r - 296.0f), 0, 1) * 0.30f;
            c = mixc(c, glow, ring);

            /* black inner bezel, then the gold rim with a little metallic shading */
            c = mixc(black, c, inside(r, 326.0f));
            float shade = 0.85f + 0.25f * clampf(-(dx + dy) / (r * 1.4142f + 1.0f), -1, 1);
            rgbf rim = { gold.r * shade, gold.g * shade, gold.b * shade };
            c = mixc(rim, c, inside(r, 341.0f));
            c = mixc((rgbf){ 0, 0, 0 }, c, inside(r, 360.0f));

            /* ordered dither before cutting to 5-6-5 bits */
            float d = (bayer[y & 3][x & 3] / 16.0f - 0.5f);
            int R = (int)clampf(c.r + d * 8.0f, 0, 255);
            int G = (int)clampf(c.g + d * 4.0f, 0, 255);
            int B = (int)clampf(c.b + d * 8.0f, 0, 255);
            row[x] = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Dynamic drawing                                                     */

static void pt(lv_point_precise_t *p, float ang_deg, float r)
{
    p->x = (lv_value_precise_t)(CX + sinf(ang_deg * DEG) * r);
    p->y = (lv_value_precise_t)(CY - cosf(ang_deg * DEG) * r);
}

static void tri(lv_layer_t *L, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t c,
                uint32_t col, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0] = a; d.p[1] = b; d.p[2] = c;
    d.color = lv_color_hex(col);
    d.opa = opa;
    lv_draw_triangle(L, &d);
}

/* A spindle: a long thin diamond from `tail` (behind the centre) to `tip`. */
static void spindle(lv_layer_t *L, float ang, float tip, float half_w, float tail,
                    uint32_t col, lv_opa_t opa)
{
    lv_point_precise_t a, b, c, d;
    pt(&a, ang, tip);
    pt(&b, ang + 90, half_w);
    pt(&c, ang + 180, tail);
    pt(&d, ang - 90, half_w);
    tri(L, a, b, d, col, opa);
    tri(L, c, b, d, col, opa);
}

static void letter(lv_layer_t *L, const char *txt, const lv_font_t *f, uint32_t col, float ang, float r)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = lv_color_hex(col);
    d.text = txt;
    d.align = LV_TEXT_ALIGN_CENTER;
    int x = (int)(CX + sinf(ang * DEG) * r);
    int y = (int)(CY - cosf(ang * DEG) * r);
    int h = lv_font_get_line_height(f);
    lv_area_t a = { x - 40, y - h / 2, x + 40, y + h / 2 };
    lv_draw_label(L, &d, &a);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *L = lv_event_get_layer(e);
    const float north = -s_heading;          /* where north is on screen */

    /* 36 ticks; longer every 30 degrees */
    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = lv_color_hex(C_GLOW);
    ln.opa = 190;
    ln.width = 4;
    ln.round_start = ln.round_end = 1;
    for (int i = 0; i < 36; i++) {
        float a = north + i * 10;
        pt(&ln.p1, a, (i % 3 == 0) ? 254 : 269);
        pt(&ln.p2, a, 284);
        lv_draw_line(L, &ln);
    }

    letter(L, "N", &font_cinzel_bold_46, C_GLOW,  north,       216);
    letter(L, "E", &font_cinzel_30,      C_MUTED, north + 90,  220);
    letter(L, "S", &font_cinzel_30,      C_MUTED, north + 180, 220);
    letter(L, "W", &font_cinzel_30,      C_MUTED, north + 270, 220);

    /* diamond on the ring marking the station direction */
    lv_point_precise_t a, b, c, d;
    float ta = s_target;
    pt(&a, ta, 277 + 15); pt(&c, ta, 277 - 15);
    pt(&b, ta + 3.1f, 277); pt(&d, ta - 3.1f, 277);
    tri(L, a, b, d, C_GLOW, LV_OPA_COVER);
    tri(L, c, b, d, C_GLOW, LV_OPA_COVER);

    /* thin north spindle (under the gold one) */
    spindle(L, north, 171, 9.5f, 45, C_SPINDLE_N, 215);

    /* gold spindle with a soft glow: a few widening, faint copies first */
    for (int k = 4; k >= 1; k--) {
        spindle(L, ta, 227 + k * 5, 15 + k * 5, 76 + k * 4, C_GLOW, 26);
    }
    spindle(L, ta, 227, 15, 76, C_GLOW, LV_OPA_COVER);

    /* hub */
    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.radius = LV_RADIUS_CIRCLE;
    hub.bg_color = lv_color_hex(C_HUB);
    hub.border_color = lv_color_hex(C_HUB_EDGE);
    hub.border_width = 4;
    lv_area_t ha = { CX - 17, CY - 17, CX + 17, CY + 17 };
    lv_draw_rect(L, &hub, &ha);
}

/* ------------------------------------------------------------------ */

static float wrap180(float a)
{
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}

/* great-circle distance (m) and initial bearing (deg, true north) */
static void dist_bearing(double lat1, double lon1, double lat2, double lon2, double *dist, double *brg)
{
    const double R = 6371000.0, rad = M_PI / 180.0;
    double p1 = lat1 * rad, p2 = lat2 * rad, dl = (lon2 - lon1) * rad, dp = p2 - p1;
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    *dist = 2 * R * atan2(sqrt(h), sqrt(1 - h));
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    *brg = fmod(atan2(y, x) / rad + 360.0, 360.0);
}

/* Called by the UI timer (~30 times a second) inside the LVGL task. */
void ui_compass_tick(const liahona_state_t *s)
{
    /* magnetic -> true heading, then smooth (handles the 359 -> 0 wrap) */
    float h = s->heading_deg + MAG_DECLINATION_DEG;
    s_heading += wrap180(h - s_heading) * 0.18f;
    s_heading = fmodf(s_heading + 360.0f, 360.0f);

    char big[24], small[32];
    float want;
    if (s->target_set && s->gps_fix) {
        double dist, brg;
        dist_bearing(s->gps_lat, s->gps_lon, s->target_lat, s->target_lon, &dist, &brg);
        want = (float)brg - s_heading;
        if (dist < 1000) snprintf(big, sizeof(big), "%d m", (int)(dist + 0.5));
        else snprintf(big, sizeof(big), "%.1f km", dist / 1000.0);
        snprintf(small, sizeof(small), "TO MARKED SPOT");
    } else {
        /* no station: the spindle rests near north and drifts a little */
        float t = esp_timer_get_time() / 1e6f;
        want = -s_heading + 9.0f * sinf(t * 0.7f) + 4.0f * sinf(t * 1.9f);
        if (s->mag == TEST_PASS) snprintf(big, sizeof(big), "%d\xC2\xB0", (int)(s_heading + 0.5f) % 360);
        else snprintf(big, sizeof(big), "--");
        snprintf(small, sizeof(small), s->target_set ? "WAITING FOR GPS" : "NO STATION SET");
    }
    s_target += wrap180(want - s_target) * 0.15f;

    if (strcmp(lv_label_get_text(s_lbl_big), big)) lv_label_set_text(s_lbl_big, big);
    if (strcmp(lv_label_get_text(s_lbl_small), small)) lv_label_set_text(s_lbl_small, small);

    /* only redraw the dial when something visibly moved */
    if (fabsf(wrap180(s_heading - s_drawn_heading)) > 0.25f ||
        fabsf(wrap180(s_target - s_drawn_target)) > 0.25f) {
        s_drawn_heading = s_heading;
        s_drawn_target = s_target;
        lv_area_t a = { CX - 300, CY - 300, CX + 300, CY + 300 };
        lv_obj_invalidate_area(s_overlay, &a);
    }
}

void ui_compass_build(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    build_background();
    if (s_bg) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, s_bg);
        lv_obj_set_pos(img, 0, 0);
    }

    s_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, W, W);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_overlay, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    s_lbl_big = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_big, &font_cinzel_56, 0);
    lv_obj_set_style_text_color(s_lbl_big, lv_color_hex(C_CREAM), 0);
    lv_label_set_text(s_lbl_big, "");
    lv_obj_align(s_lbl_big, LV_ALIGN_CENTER, 0, 128);

    s_lbl_small = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_small, &font_inter_22, 0);
    lv_obj_set_style_text_color(s_lbl_small, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_letter_space(s_lbl_small, 5, 0);
    lv_label_set_text(s_lbl_small, "");
    lv_obj_align(s_lbl_small, LV_ALIGN_CENTER, 0, 182);
}
