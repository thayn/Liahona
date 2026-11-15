/*
 * Journey engine: runs a journey compiled by the Liahona Manager ("Ball file",
 * <id>.ball.json, format 1) against the camp roster (liahona-roster.json).
 *
 * Pure C + cJSON, no ESP-IDF: the device glue (main/journey_port.c) feeds it
 * events and draws its view; host tests drive it directly.
 *
 * Flow per stage: gather (roll call) -> brief -> tasks in order -> next stage.
 * Wrong answers and decoy tags darken the ball (penalties.darken_s, escalating).
 * Leaders can always move things along with their card or over LoRa.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JV_MAX_CHOICES 6

typedef enum {
    JV_NONE = 0,        /* no journey loaded */
    JV_ERROR,           /* journey file problem: body says what */
    JV_GATHER,          /* roll call before a stage */
    JV_BRIEF,           /* story text before the tasks */
    JV_TASK,            /* a task (kind says which) */
    JV_DARK,            /* darkened: penalty or scripted */
    JV_DONE,            /* journey complete */
} jv_mode_t;

typedef struct {
    jv_mode_t mode;
    uint32_t  version;                  /* changes whenever the layout should rebuild */
    int       stage_no, stage_count;    /* 1-based */
    char      title[64];                /* stage name */
    char      kind[24];                 /* task type, "" if none */
    char      heading[64];              /* e.g. "Question", "Find the place" */
    char      body[720];                /* prompt / brief / directions */
    char      ref[48];                  /* scripture reference, if any */
    char      status[96];               /* live line: "3 of 9 have tapped", "Not this place" */
    int       nchoices;
    char      choices[JV_MAX_CHOICES][72];
    bool      can_continue;
    char      continue_label[24];
    bool      can_hint;                 /* a hint is available now */
    char      hint[200];                /* last hint shown */
    bool      can_replay;               /* audio: play again */
    bool      audio;                    /* this task has audio */
    int       countdown_s;              /* -1 = none */
    bool      has_target;               /* travel: a place with coordinates */
    double    target_lat, target_lon;
} jv_view_t;

/* What the engine needs from the device. Any hook may be NULL. */
typedef struct {
    int64_t (*now_ms)(void);
    void (*save)(const char *journey_id, int stage, int task, int wrongs);
    bool (*load)(const char *journey_id, int *stage, int *task, int *wrongs);
    void (*audio_play)(const char *file, int start_ms, int duration_ms);
    void (*audio_stop)(void);
    void (*checkin)(const uint8_t *uid, int uid_len, int station);   /* report over LoRa */
    bool (*local_minutes)(int *minutes_since_midnight);              /* for "rest until" */
    void (*leds)(int mode);                                          /* 0 glow, 2 off, 3 pulse */
} jhooks_t;

/* Load from JSON text. ball_id picks the team from the roster. Returns 0, or -1 with err set
 * (the view then shows JV_ERROR). roster_json may be NULL (no roll calls; leaders can't be told apart). */
int  journey_load(const char *journey_json, const char *roster_json, int ball_id,
                  const jhooks_t *hooks, char *err, size_t err_cap);
void journey_unload(void);
bool journey_loaded(void);

/* Events */
void journey_tick(void);                       /* call ~10 times a second */
void journey_tag(const char *uid_hex);         /* NFC tap: uppercase hex, no separators */
void journey_choice(int index);
void journey_continue(void);
void journey_hint(void);
void journey_replay(void);
void journey_leader(int lp_cmd, int arg);      /* lp_cmd_t from liahona_proto.h: 1 unlock, 2 skip, 5 stage */
void journey_restart(void);                    /* back to stage 1, progress cleared */

/* Snapshot of what to show. */
void journey_view(jv_view_t *out);
int  journey_stage_no(void);                   /* 0 = not started / none */

#ifdef __cplusplus
}
#endif
