/*
 * Host test: plays the Manager's sample "Leader Trial" journey from start to finish.
 * journey.json / roster.json are made from samples/leader-trial.yaml (see Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "journey.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int64_t T;
static int64_t now_ms(void) { return T; }
static char last_audio[64]; static int last_start, last_dur, plays, stops, checkins, last_station;
static int saved_stage = -1, saved_task, saved_wrongs;
static int led_mode = -1;
static void audio_play(const char *f, int s, int d) { snprintf(last_audio, sizeof last_audio, "%s", f); last_start = s; last_dur = d; plays++; }
static void audio_stop(void) { stops++; }
static void checkin(const uint8_t *u, int n, int st) { (void)u; (void)n; checkins++; last_station = st; }
static void save(const char *id, int s, int t, int w) { (void)id; saved_stage = s; saved_task = t; saved_wrongs = w; }
static bool load(const char *id, int *s, int *t, int *w) { (void)id; if (saved_stage < 0) return false; *s = saved_stage; *t = saved_task; *w = saved_wrongs; return true; }
static int minutes = 12 * 60;
static bool local_min(int *m) { *m = minutes; return true; }
static void leds(int m) { led_mode = m; }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1); size_t got = fread(b, 1, n, f); b[got] = 0; fclose(f);
    return b;
}

static jv_view_t V;
static void view(void) { journey_view(&V); }
static void advance(int secs) { T += (int64_t)secs * 1000; journey_tick(); view(); }

/* place i's tag in the generated file: 04AA00000000 + two digits */
static const char *place_tag(int i) { static char b[20]; snprintf(b, sizeof b, "04AA00000000%02d", i); return b; }
enum { LOT, CLASSROOM, PLATE1, PLATE2, PLATE3, GUARD, SHED, OTHER_CHURCH, STOPLIGHT, CHAPEL };
#define ANN "04C1000000000001"
#define BEN "04C1000000000002"
#define CAL "04C1000000000003"   /* leader on our team */
#define DEE "04C1000000000004"   /* leader on another team */

static void roll_call(void)
{
    CHECK(V.mode == JV_GATHER);
    journey_tag(ANN); journey_tag(BEN); journey_tag(CAL); view();
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p1[256], p2[256];
    snprintf(p1, sizeof p1, "%s/journey.json", dir);
    snprintf(p2, sizeof p2, "%s/roster.json", dir);
    char *jj = slurp(p1), *rj = slurp(p2);
    jhooks_t h = { now_ms, save, load, audio_play, audio_stop, checkin, local_min, leds };
    char err[160];

    /* bad file */
    CHECK(journey_load("{oops", rj, 1, &h, err, sizeof err) == -1);
    view(); CHECK(V.mode == JV_ERROR && strstr(V.body, "valid JSON"));

    CHECK(journey_load(jj, rj, 1, &h, err, sizeof err) == 0);
    view();
    printf("stage %d/%d: %s\n", V.stage_no, V.stage_count, V.title);
    CHECK(V.stage_count == 8 && V.stage_no == 1);

    /* 1. Tent door: roll call, brief, intro */
    CHECK(V.mode == JV_GATHER && strstr(V.body, "Lehi") && strstr(V.status, "0 of 3"));
    journey_tag(DEE); view(); CHECK(V.mode == JV_GATHER);            /* other team's leader: needs a 2nd tap */
    journey_tag(ANN); view(); CHECK(strstr(V.status, "Ann"));
    T += 5000; view(); CHECK(strstr(V.status, "1 of 3"));
    journey_tag(BEN); journey_tag(CAL); view();
    CHECK(V.mode == JV_BRIEF && strstr(V.ref, "1 Nephi 16:10") && strstr(V.body, "curious workmanship"));
    journey_continue(); view();
    CHECK(V.mode == JV_TASK && !strcmp(V.kind, "intro") && V.can_continue);
    journey_continue(); view();

    /* 2. Leave Jerusalem: clip-only brief is skipped; question with a hint, wrong answer darkens */
    roll_call();
    CHECK(V.mode == JV_TASK && !strcmp(V.kind, "question") && V.nchoices == 3);
    CHECK(!V.can_hint);
    advance(46); CHECK(V.can_hint);
    journey_hint(); view(); CHECK(strstr(V.hint, "1 Nephi 2:4"));
    journey_choice(0); view();
    CHECK(V.mode == JV_DARK && V.countdown_s == 30 && led_mode == 2);
    journey_tag(ANN); journey_tag(BEN); view(); CHECK(V.mode == JV_DARK && strstr(V.status, "2 of 3"));
    journey_tag(CAL); view(); CHECK(V.mode == JV_TASK && !strcmp(V.kind, "question"));   /* unity restore */
    /* the same question again: the last tap (a team leader's card, still on the reader)
     * mustn't skip it straight away; after 2 s a leader's tap may skip on purpose */
    journey_tag(CAL); view(); CHECK(V.mode == JV_TASK && !strcmp(V.kind, "question"));
    advance(1); journey_tag(CAL); view(); CHECK(!strcmp(V.kind, "question"));
    journey_choice(1); view();
    CHECK(!strcmp(V.kind, "checkin") && !strstr(V.body, "lot corner"));
    journey_tag(place_tag(SHED)); view(); CHECK(strstr(V.status, "Not this place"));
    journey_tag(place_tag(LOT)); view();
    CHECK(checkins == 1 && last_station > 0);

    /* 3. Valley of Lemuel: audio clue, audio hints and a replay, then a check-in */
    roll_call();
    CHECK(!strcmp(V.kind, "audio_clue") && V.audio && !strcmp(last_audio, "hymn-clue.mp3") && last_start == 0 && last_dur == 8000);
    CHECK(!V.can_replay);
    advance(121); CHECK(V.can_hint && V.can_replay);
    journey_hint(); CHECK(last_start == 8000 && last_dur == 5000);     /* continue: the next 5 s */
    journey_replay(); CHECK(last_start == 0 && last_dur == 13000);     /* replay what's unlocked */
    view(); CHECK(!V.can_replay);
    journey_continue(); view();
    CHECK(!strcmp(V.kind, "checkin"));
    journey_tag(place_tag(CLASSROOM)); view();

    /* 4. Plates: fragments with a decoy and a time limit */
    roll_call();
    CHECK(V.mode == JV_BRIEF); journey_continue(); view();
    CHECK(!strcmp(V.kind, "gather_fragments") && V.countdown_s == 480);
    journey_tag(place_tag(PLATE1)); journey_tag(place_tag(PLATE2)); view(); CHECK(strstr(V.status, "Fragment found") || strstr(V.status, "2 of 3"));
    journey_tag(place_tag(GUARD)); view(); CHECK(V.mode == JV_DARK && V.countdown_s == 60);   /* 2nd wrong: 60 s */
    advance(61); CHECK(V.mode == JV_TASK && V.countdown_s == 480);       /* waited it out; clock restarts */
    journey_tag(place_tag(PLATE3)); view();

    /* 5. Broken bow: be_still isn't supported yet (leader moves on), then leader approves */
    roll_call();
    journey_continue(); view();
    CHECK(!strcmp(V.kind, "be_still") && strstr(V.status, "leader"));
    journey_tag(ANN); view(); CHECK(!strcmp(V.kind, "be_still"));
    journey_tag(CAL); view(); CHECK(!strcmp(V.kind, "leader_approve"));
    journey_leader(1, 0); view();                                        /* LoRa unlock */

    /* 6. Nahom: cipher (leader skip over LoRa), check-in */
    roll_call();
    CHECK(!strcmp(V.kind, "cipher"));
    journey_leader(2, 0); view(); CHECK(!strcmp(V.kind, "checkin"));
    journey_tag(place_tag(SHED)); view();

    /* 7. Race to Bountiful: stage limit, three check-ins; run out of time once */
    roll_call();
    CHECK(!strcmp(V.kind, "checkin") && !strcmp(V.heading, "Race") && V.countdown_s == 480);
    advance(481); CHECK(V.mode == JV_DARK && V.countdown_s == 120);      /* 3rd wrong: 120 s */
    journey_leader(1, 0); view(); CHECK(V.mode == JV_TASK);
    journey_tag(place_tag(OTHER_CHURCH)); journey_tag(place_tag(STOPLIGHT)); journey_tag(place_tag(LOT)); view();

    /* 8. The storm: scripted darkness restored by unity, hymn with restart hints, check-in */
    CHECK(V.mode == JV_DARK && strstr(V.ref, "18:12"));
    journey_tag(ANN); journey_tag(BEN); journey_tag(CAL); view();
    CHECK(!strcmp(V.kind, "name_that_hymn") && last_dur == 6000);
    advance(121);
    journey_hint(); CHECK(last_start == 0 && last_dur == 10000);         /* restart, 4 s longer */
    CHECK(saved_wrongs == 3);

    /* resume after a reboot: same task */
    journey_unload();
    CHECK(journey_load(jj, rj, 1, &h, err, sizeof err) == 0); view();
    CHECK(!strcmp(V.kind, "name_that_hymn") && V.stage_no == 8);
    journey_choice(1); view();
    CHECK(!strcmp(V.kind, "checkin"));
    journey_tag(place_tag(CHAPEL)); view();
    CHECK(V.mode == JV_DONE && strstr(V.heading, "complete"));

    /* leader jump to stage 3 */
    journey_leader(5, 3); view(); CHECK(V.stage_no == 3 && V.mode == JV_GATHER);

    /* no roster: no roll calls */
    saved_stage = -1;
    CHECK(journey_load(jj, NULL, 1, &h, err, sizeof err) == 0); view();
    CHECK(V.mode == JV_BRIEF);

    /* short IDs from a USB keyboard reader (4 bytes, often byte-reversed) still match
     * the ball's full 7-byte read */
    {
        const char *short_roster =
            "{\"teams\":[{\"id\":\"t1\",\"name\":\"Moore\",\"ballId\":\"1\"}],"
            "\"people\":[{\"id\":\"a\",\"name\":\"A\",\"role\":\"youth\",\"teamId\":\"t1\"},"
                       "{\"id\":\"b\",\"name\":\"B\",\"role\":\"youth\",\"teamId\":\"t1\"},"
                       "{\"id\":\"c\",\"name\":\"C\",\"role\":\"youth\",\"teamId\":\"t1\"}],"
            "\"cards\":{\"A6FA4A04\":\"a\",\"044AFAA7\":\"b\",\"044afaa8\":\"c\"}}";
        saved_stage = -1;
        CHECK(journey_load(jj, short_roster, 1, &h, err, sizeof err) == 0); view();
        CHECK(V.mode == JV_GATHER && strstr(V.status, "0 of 3"));
        journey_tag("044AFAA6801234"); view(); CHECK(!strcmp(V.status, "A")); advance(5); CHECK(strstr(V.status, "1 of 3"));   /* reversed */
        journey_tag("044AFAA7801234"); advance(5); CHECK(strstr(V.status, "2 of 3"));   /* in order */
        journey_tag("0499FAA6801234"); advance(5); CHECK(strstr(V.status, "2 of 3"));   /* stranger */
        journey_tag("044AFAA8801234"); view(); CHECK(V.mode == JV_BRIEF);           /* lower-case key */
    }

    journey_unload();
    free(jj); free(rj);
    printf(fails ? "%d FAILED\n" : "journey host test passed\n", fails);
    return fails ? 1 : 0;
}
