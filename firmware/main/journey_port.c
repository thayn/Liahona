/*
 * Runs the journey engine (components/journey) on the ball:
 * loads journey.json + roster.json from the SD card, feeds it NFC taps and leader
 * commands, saves progress to flash so a restart resumes, and hands the screen a view.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"
#include "board_pins.h"
#include "liahona.h"
#include "journey.h"
#include "journey_port.h"
#include "update.h"
#include "liahona_proto.h"

static const char *TAG = "journey";
static SemaphoreHandle_t s_lock;          /* the engine is single-threaded: one caller at a time */
#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ---------------------------------------------------------------- hooks */
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void save(const char *id, int stage, int task, int wrongs)
{
    nvs_handle_t h;
    if (nvs_open("journey", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "id", id);
    nvs_set_i32(h, "stage", stage);
    nvs_set_i32(h, "task", task);
    nvs_set_i32(h, "wrongs", wrongs);
    nvs_commit(h);
    nvs_close(h);
}

static bool load(const char *id, int *stage, int *task, int *wrongs)
{
    nvs_handle_t h;
    if (nvs_open("journey", NVS_READONLY, &h) != ESP_OK) return false;
    char saved[64] = "";
    size_t n = sizeof(saved);
    int32_t s = 0, t = 0, w = 0;
    bool ok = nvs_get_str(h, "id", saved, &n) == ESP_OK && strcmp(saved, id) == 0 &&
              nvs_get_i32(h, "stage", &s) == ESP_OK && nvs_get_i32(h, "task", &t) == ESP_OK;
    nvs_get_i32(h, "wrongs", &w);
    nvs_close(h);
    if (ok) { *stage = s; *task = t; *wrongs = w; }
    return ok;                              /* a different journey on the card starts fresh */
}

static void checkin(const uint8_t *uid, int n, int station) { lora_send_checkin(uid, n, (uint8_t)station); }

static bool local_minutes(int *m)
{
    STATE_LOCK();
    int hh, mm, ss;
    bool ok = g_state.gps_time[0] && sscanf(g_state.gps_time, "%d:%d:%d", &hh, &mm, &ss) == 3;
    STATE_UNLOCK();
    if (!ok) return false;
    *m = ((hh * 60 + mm + LOCAL_UTC_OFFSET_MIN) % 1440 + 1440) % 1440;
    return true;
}

static void leds(int mode) { if (!g_state.dimmed) leds_set_mode(mode); }

static const jhooks_t HOOKS = {
    .now_ms = now_ms, .save = save, .load = load,
    .audio_play = audio_play, .audio_stop = audio_stop,
    .checkin = checkin, .local_minutes = local_minutes, .leds = leds,
};

/* ---------------------------------------------------------------- files */
static char *read_file(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (size_t)n > max) { fclose(f); return NULL; }
    char *b = malloc(n + 1);
    if (b) { size_t got = fread(b, 1, n, f); b[got] = 0; }
    fclose(f);
    return b;
}

static int ball_id(void)
{
    FILE *f = fopen(BALL_ID_FILE, "r");
    int v = 0;
    if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
    return (v >= 1 && v <= 254) ? v : BALL_ID_DEFAULT;
}

/* ---------------------------------------------------------------- task */
static void apply_dim(bool dim)
{
    STATE_LOCK(); g_state.dimmed = dim; STATE_UNLOCK();
    if (dim) { bsp_display_backlight_off(); leds_set_mode(2); audio_stop(); }
    else { bsp_display_backlight_on(); leds_set_mode(0); }
}

static volatile bool s_reload;
void journey_request_reload(void) { s_reload = true; }

/* (Re)load the journey from the SD card. Progress is kept in NVS, so the same
 * journey carries on where it was; a different one starts at the beginning. */
static void load_from_sd(void)
{
    LOCK();
    if (journey_loaded()) journey_unload();
    UNLOCK();
    char *jj = read_file(CONTENT_JOURNEY, 512 * 1024);
    if (!jj) {
        ESP_LOGI(TAG, "no %s on the SD card: compass and self-test only", CONTENT_JOURNEY);
        STATE_LOCK(); g_state.journey_active = false; STATE_UNLOCK();
        return;
    }
    char *rj = read_file(CONTENT_ROSTER, 256 * 1024);
    if (!rj) ESP_LOGW(TAG, "no %s: roll calls are skipped and leaders can't be recognised", CONTENT_ROSTER);

    char err[160];
    int id = ball_id();
    LOCK();
    int e = journey_load(jj, rj, id, &HOOKS, err, sizeof(err));
    UNLOCK();
    free(jj);
    free(rj);
    if (e) ESP_LOGE(TAG, "%s", err);
    else ESP_LOGI(TAG, "journey loaded for ball %d", id);
    STATE_LOCK(); g_state.journey_active = (e == 0); STATE_UNLOCK();
}

static void journey_task(void *arg)
{
    /* The SD card is mounted before this task starts. */
    load_from_sd();

    uint32_t tap_seen, cmd_seen;
    STATE_LOCK(); tap_seen = g_state.nfc_tap_seq; cmd_seen = g_state.leader_cmd_count; STATE_UNLOCK();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_reload) { s_reload = false; load_from_sd(); }
        char uid[24] = "";
        uint8_t cmd = 0, arg8 = 0;
        bool tap = false, got_cmd = false;
        STATE_LOCK();
        if (g_state.nfc_tap_seq != tap_seen) { tap_seen = g_state.nfc_tap_seq; strlcpy(uid, g_state.nfc_tap_uid, sizeof(uid)); tap = true; }
        if (g_state.leader_cmd_count != cmd_seen) { cmd_seen = g_state.leader_cmd_count; cmd = g_state.leader_cmd; arg8 = g_state.leader_cmd_arg; got_cmd = true; }
        STATE_UNLOCK();

        if (got_cmd && cmd == LP_CMD_DIM) apply_dim(true);
        else if (got_cmd && cmd == LP_CMD_RESTORE) apply_dim(false);
        else if (got_cmd && cmd == LP_CMD_UPDATE) update_start();

        LOCK();
        if (tap) { ESP_LOGI(TAG, "tap %s", uid); journey_tag(uid); }
        if (got_cmd && cmd != LP_CMD_DIM && cmd != LP_CMD_RESTORE && cmd != LP_CMD_UPDATE) journey_leader(cmd, arg8);
        journey_tick();
        jv_view_t v;
        journey_view(&v);
        int stage = journey_stage_no();
        UNLOCK();

        STATE_LOCK();
        g_state.stage = (uint8_t)stage;
        if (journey_loaded()) {
            g_state.target_set = v.has_target;
            if (v.has_target) { g_state.target_lat = v.target_lat; g_state.target_lon = v.target_lon; }
        }
        STATE_UNLOCK();
        if (tap) audio_tap_sound();
    }
}

/* ---------------------------------------------------------------- UI calls (LVGL task) */
void journey_ui_view(jv_view_t *v) { if (!s_lock) { memset(v, 0, sizeof(*v)); return; } LOCK(); journey_view(v); UNLOCK(); }
void journey_ui_choice(int i)  { LOCK(); journey_choice(i); UNLOCK(); }
void journey_ui_continue(void) { LOCK(); journey_continue(); UNLOCK(); }
void journey_ui_hint(void)     { LOCK(); journey_hint(); UNLOCK(); }
void journey_ui_replay(void)   { LOCK(); journey_replay(); UNLOCK(); }
void journey_ui_restart(void)  { LOCK(); journey_restart(); UNLOCK(); }

void journey_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(journey_task, "journey", 8192, NULL, 4, NULL);
}
