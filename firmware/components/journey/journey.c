/*
 * Journey engine. See journey.h for the overview.
 *
 * Supported task types: intro, question, true_false, name_that_hymn, audio_clue,
 * checkin, gather_fragments, leader_approve, testimony, scripted_darken, rest.
 * Any other type shows its prompt and waits for a leader (card tap or LoRa unlock),
 * so a journey never gets stuck on something the ball can't judge yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "journey.h"

#define MAX_MEMBERS   24
#define MAX_FRAGMENTS 16
#define FLASH_MS      4000

typedef struct {
    cJSON   *root, *roster;
    jhooks_t h;
    bool     loaded;
    char     err[160];
    char     id[64];
    cJSON   *stages, *places;
    int      nstages;
    int      hint_default;          /* seconds */
    int      darken_s[8], ndarken;
    bool     unity_restore;

    /* the team this ball belongs to */
    char     team_name[64];
    int      nmembers;
    char     member[MAX_MEMBERS][40];

    /* progress */
    int      stage, task;           /* 0-based */
    int      wrongs;
    jv_mode_t mode;
    uint32_t version;

    /* taps (roll call, unity restore, testimony) */
    bool     tapped[MAX_MEMBERS];
    int      ntapped;
    int      leader_taps;           /* consecutive leader taps during a roll call */

    /* task runtime */
    int64_t  task_start;
    int64_t  deadline;              /* 0 = none */
    int      deadline_len_ms;
    int      text_hints_used, audio_hints_used, replays_used;
    int      audio_len_ms;          /* how much of the clip is "unlocked" */
    int      audio_pos_ms;          /* where a "continue" hint resumes */
    bool     frag_found[MAX_FRAGMENTS];
    char     hint[200];

    /* darkness */
    bool     dark_scripted;
    char     dark_restore[8];       /* unity / leader / wait */
    int64_t  dark_until;            /* 0 = until restored */
    int64_t  tag_quiet_until;       /* after the light returns, ignore taps until then */

    char     flash[96];
    int64_t  flash_until;
    char     dark_reason[64];
} engine_t;

static engine_t E;

/* ------------------------------------------------------------------ helpers */
static int64_t now(void) { return E.h.now_ms ? E.h.now_ms() : 0; }
static void bump(void) { E.version++; }

static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static double num(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static cJSON *obj(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }
static void copy(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    snprintf(dst, cap, "%s", src ? src : "");
}

static cJSON *stage_obj(void) { return cJSON_GetArrayItem(E.stages, E.stage); }
static cJSON *tasks_arr(void) { return obj(stage_obj(), "tasks"); }
static cJSON *task_obj(void) { return cJSON_GetArrayItem(tasks_arr(), E.task); }
static const char *task_type(void) { const char *t = str(task_obj(), "type"); return t ? t : ""; }
static bool is(const char *t) { return strcmp(task_type(), t) == 0; }

static void flash(const char *msg)
{
    copy(E.flash, sizeof(E.flash), msg);
    E.flash_until = now() + FLASH_MS;
}

static void save(void)
{
    if (E.h.save) E.h.save(E.id, E.stage, E.task, E.wrongs);
}

/* ------------------------------------------------------------------ uid matching */
/* The ball reads the card's full ID (7 bytes for NTAG215, e.g. 044AFAA6xxxxxx).
 * Many USB keyboard readers type only the first 4 bytes, often in reverse order
 * (A6FA4A04). Accept: an exact match, the first 4 bytes, or those 4 bytes reversed. */
static bool uid_match(const char *known, const char *tapped)
{
    if (!known || !tapped) return false;
    if (strcasecmp(known, tapped) == 0) return true;
    size_t kl = strlen(known), tl = strlen(tapped);
    if (kl != 8 || tl < 8) return false;
    if (tl > 8 && strncasecmp(known, tapped, 8) == 0) return true;      /* first 4 bytes, in order */
    for (int i = 0; i < 4; i++)                                          /* reversed byte order */
        if (strncasecmp(known + i * 2, tapped + (3 - i) * 2, 2) != 0) return false;
    return true;
}

/* ------------------------------------------------------------------ roster */
static const cJSON *person_for_uid(const char *uid)
{
    if (!E.roster || !uid) return NULL;
    const char *pid = str(obj(E.roster, "cards"), uid);
    if (!pid) {
        const cJSON *c;
        cJSON_ArrayForEach(c, obj(E.roster, "cards"))
            if (cJSON_IsString(c) && uid_match(c->string, uid)) { pid = c->valuestring; break; }
    }
    if (!pid) return NULL;
    const cJSON *p;
    cJSON_ArrayForEach(p, obj(E.roster, "people")) {
        const char *id = str(p, "id");
        if (id && strcmp(id, pid) == 0) return p;
    }
    return NULL;
}
static bool is_leader(const cJSON *p) { const char *r = str(p, "role"); return r && strcmp(r, "leader") == 0; }
static int member_index(const cJSON *p)
{
    const char *id = str(p, "id");
    for (int i = 0; id && i < E.nmembers; i++) if (strcmp(E.member[i], id) == 0) return i;
    return -1;
}

static void load_team(int ball_id)
{
    E.nmembers = 0;
    E.team_name[0] = 0;
    if (!E.roster) return;
    char want[12];
    snprintf(want, sizeof(want), "%d", ball_id);
    const char *team_id = NULL;
    const cJSON *t;
    cJSON_ArrayForEach(t, obj(E.roster, "teams")) {
        const char *b = str(t, "ballId");
        if (b && strcmp(b, want) == 0) { team_id = str(t, "id"); copy(E.team_name, sizeof(E.team_name), str(t, "name")); break; }
    }
    if (!team_id) return;
    const cJSON *p;
    cJSON_ArrayForEach(p, obj(E.roster, "people")) {
        const char *tid = str(p, "teamId"), *id = str(p, "id");
        if (tid && id && strcmp(tid, team_id) == 0 && E.nmembers < MAX_MEMBERS)
            copy(E.member[E.nmembers++], sizeof(E.member[0]), id);
    }
}

static void clear_taps(void)
{
    memset(E.tapped, 0, sizeof(E.tapped));
    E.ntapped = 0;
    E.leader_taps = 0;
}

/* ------------------------------------------------------------------ places */
/* Place key whose tag list holds uid, or NULL. station = 1-based order in the file. */
static const char *place_for_uid(const char *uid, int *station)
{
    int i = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, E.places) {
        i++;
        const cJSON *tag;
        cJSON_ArrayForEach(tag, obj(p, "tags")) {
            if (cJSON_IsString(tag) && uid_match(tag->valuestring, uid)) {
                if (station) *station = i;
                return p->string;
            }
        }
    }
    return NULL;
}
/* Place names are for planners only: the ball never shows them. */
static bool list_has(const cJSON *arr, const char *key)
{
    const cJSON *v;
    cJSON_ArrayForEach(v, arr) if (cJSON_IsString(v) && key && strcmp(v->valuestring, key) == 0) return true;
    return false;
}
static int list_index(const cJSON *arr, const char *key)
{
    int i = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr) { if (cJSON_IsString(v) && key && strcmp(v->valuestring, key) == 0) return i; i++; }
    return -1;
}
static void uid_bytes(const char *hex, uint8_t *out, int *n)
{
    int k = 0;
    for (const char *p = hex; p[0] && p[1] && k < 10; p += 2) {
        char b[3] = { p[0], p[1], 0 };
        out[k++] = (uint8_t)strtol(b, NULL, 16);
    }
    *n = k;
}

/* ------------------------------------------------------------------ audio */
static bool audio_task(void) { return is("audio_clue") || is("name_that_hymn"); }
static void play_window(int start_ms, int len_ms)
{
    const char *file = str(task_obj(), "audio");
    if (file && E.h.audio_play && len_ms > 0) E.h.audio_play(file, start_ms, len_ms);
}
static int task_start_ms(void) { return (int)(num(task_obj(), "start", 0) * 1000); }

/* ------------------------------------------------------------------ flow */
static void start_stage(int i);
static void start_task(int t);

static void set_deadline(int seconds)
{
    if (seconds > 0) { E.deadline_len_ms = seconds * 1000; E.deadline = now() + E.deadline_len_ms; }
    else { E.deadline = 0; E.deadline_len_ms = 0; }
}

static void complete_task(void)
{
    if (E.h.audio_stop) E.h.audio_stop();
    start_task(E.task + 1);
}

static void darken(void)
{
    if (E.h.audio_stop) E.h.audio_stop();
    copy(E.dark_reason, sizeof(E.dark_reason), E.flash_until > now() ? E.flash : "");
    E.flash_until = 0;
    E.wrongs++;
    int s = E.ndarken ? E.darken_s[(E.wrongs - 1) < E.ndarken ? E.wrongs - 1 : E.ndarken - 1] : 30;
    E.mode = JV_DARK;
    E.dark_scripted = false;
    copy(E.dark_restore, sizeof(E.dark_restore), E.unity_restore ? "unity" : "wait");
    E.dark_until = now() + (int64_t)s * 1000;
    clear_taps();
    if (E.h.leds) E.h.leds(2);
    save();
    bump();
}

static void restore(void)
{
    if (E.mode != JV_DARK) return;
    E.dark_until = 0;
    clear_taps();
    if (E.h.leds) E.h.leds(0);
    if (E.dark_scripted) { E.mode = JV_TASK; complete_task(); return; }
    E.mode = JV_TASK;
    E.tag_quiet_until = now() + 2000;                           /* ignore medallion taps for 2 s */
    if (E.deadline) E.deadline = now() + E.deadline_len_ms;      /* a fresh go at the clock */
    flash("The Liahona shines again");
    bump();
}

static void start_task(int t)
{
    cJSON *tasks = tasks_arr();
    if (t >= cJSON_GetArraySize(tasks)) { start_stage(E.stage + 1); return; }
    E.task = t;
    E.mode = JV_TASK;
    E.task_start = now();
    E.tag_quiet_until = 0;
    E.text_hints_used = E.audio_hints_used = E.replays_used = 0;
    E.hint[0] = 0;
    memset(E.frag_found, 0, sizeof(E.frag_found));
    clear_taps();
    cJSON *tk = task_obj();
    int limit = (int)num(tk, "limit", 0);
    if (!limit) limit = (int)num(obj(tk, "travel"), "limit", 0);
    if (!limit) limit = (int)num(stage_obj(), "limit", 0);
    set_deadline(limit);

    if (audio_task()) {
        E.audio_len_ms = (int)(num(tk, "play_for", 10) * 1000);
        E.audio_pos_ms = task_start_ms() + E.audio_len_ms;
        play_window(task_start_ms(), E.audio_len_ms);
    }
    if (is("scripted_darken")) {
        E.mode = JV_DARK;
        E.dark_scripted = true;
        const char *r = str(tk, "restore");
        copy(E.dark_restore, sizeof(E.dark_restore), r ? r : "unity");
        int secs = (int)num(tk, "seconds", 0);
        E.dark_until = (strcmp(E.dark_restore, "wait") == 0 && secs > 0) ? now() + (int64_t)secs * 1000 : 0;
        if (E.h.leds) E.h.leds(2);
    } else if (E.h.leds) {
        E.h.leds(0);
    }
    save();
    bump();
}

static void enter_brief(void)
{
    E.flash_until = 0;
    cJSON *b = obj(stage_obj(), "brief");
    if (b && (str(b, "text") || str(b, "ref"))) { E.mode = JV_BRIEF; save(); bump(); }
    else start_task(0);
}

static void start_stage(int i)
{
    if (E.h.audio_stop) E.h.audio_stop();
    E.deadline = 0;
    if (i >= E.nstages) {
        E.stage = E.nstages;
        E.task = 0;
        E.mode = JV_DONE;
        if (E.h.leds) E.h.leds(3);
        save();
        bump();
        return;
    }
    E.stage = i < 0 ? 0 : i;
    E.task = 0;
    E.flash_until = 0;
    clear_taps();
    if (E.h.leds) E.h.leds(3);                      /* pulse: a new stage */
    const char *g = str(stage_obj(), "gather");
    if (g && strcmp(g, "none") != 0 && E.nmembers > 0) { E.mode = JV_GATHER; save(); bump(); }
    else enter_brief();
}

/* ------------------------------------------------------------------ load */
int journey_load(const char *jj, const char *rj, int ball_id, const jhooks_t *hooks, char *err, size_t cap)
{
    static uint32_t loads;
    journey_unload();
    memset(&E, 0, sizeof(E));
    E.version = ++loads << 20;          /* a reload always looks new to the screen */
    if (hooks) E.h = *hooks;
    E.loaded = true;
    E.mode = JV_ERROR;
    bump();

    E.root = cJSON_Parse(jj ? jj : "");
    if (!E.root) { copy(E.err, sizeof(E.err), "journey.json isn't valid JSON. Download the Ball file again."); goto fail; }
    if (num(E.root, "format", 0) != 1) { copy(E.err, sizeof(E.err), "journey.json is a format this firmware doesn't know."); goto fail; }
    E.stages = obj(E.root, "stages");
    E.places = obj(E.root, "places");
    E.nstages = cJSON_IsArray(E.stages) ? cJSON_GetArraySize(E.stages) : 0;
    if (!E.nstages) { copy(E.err, sizeof(E.err), "journey.json has no stages."); goto fail; }
    copy(E.id, sizeof(E.id), str(E.root, "id"));
    E.hint_default = (int)num(E.root, "hint_after", 120);
    cJSON *pen = obj(E.root, "penalties");
    const cJSON *d;
    cJSON_ArrayForEach(d, obj(pen, "darken_s")) if (cJSON_IsNumber(d) && E.ndarken < 8) E.darken_s[E.ndarken++] = d->valueint;
    const cJSON *u = obj(pen, "unity_restore");
    E.unity_restore = !u || cJSON_IsTrue(u);

    if (rj) {
        E.roster = cJSON_Parse(rj);
        load_team(ball_id);
    }

    int st = 0, tk = 0, wr = 0;
    if (E.h.load && E.h.load(E.id, &st, &tk, &wr) && st >= 0 && st <= E.nstages) {
        E.wrongs = wr;
        if (st >= E.nstages) { start_stage(E.nstages); }
        else {
            start_stage(st);
            /* resume mid-stage at the task they were on (the roll call was done) */
            if (tk > 0 && E.mode == JV_GATHER) { E.mode = JV_TASK; start_task(tk); }
            else if (tk > 0) start_task(tk);
        }
    } else {
        start_stage(0);
    }
    if (err && cap) err[0] = 0;
    return 0;

fail:
    if (err && cap) copy(err, cap, E.err);
    bump();
    return -1;
}

void journey_unload(void)
{
    if (E.root) cJSON_Delete(E.root);
    if (E.roster) cJSON_Delete(E.roster);
    uint32_t v = E.version;
    memset(&E, 0, sizeof(E));
    E.version = v + 1;
}

bool journey_loaded(void) { return E.loaded && E.mode != JV_ERROR; }

void journey_restart(void)
{
    if (!journey_loaded()) return;
    E.wrongs = 0;
    start_stage(0);
}

/* ------------------------------------------------------------------ events */
void journey_tick(void)
{
    if (!journey_loaded()) return;
    int64_t t = now();
    if (E.mode == JV_DARK && E.dark_until && t >= E.dark_until) { restore(); return; }
    if (E.mode == JV_TASK && E.deadline && t >= E.deadline) {
        flash("Time ran out");
        darken();
        return;
    }
    if (E.mode == JV_TASK && is("rest")) {
        const char *until = str(task_obj(), "until");
        int hh, mm, cur;
        if (until && sscanf(until, "%d:%d", &hh, &mm) == 2 && E.h.local_minutes && E.h.local_minutes(&cur)
            && cur >= hh * 60 + mm) complete_task();
    }
}

static void roll_call_tap(const cJSON *p)
{
    int mi = member_index(p);
    if (mi >= 0) {
        E.leader_taps = 0;
        if (!E.tapped[mi]) { E.tapped[mi] = true; E.ntapped++; }
        if (E.ntapped >= E.nmembers) { enter_brief(); return; }
        flash(str(p, "name"));
        return;
    }
    if (is_leader(p)) {
        if (++E.leader_taps >= 2) { enter_brief(); return; }
        flash("Leader: tap again to start without everyone");
        return;
    }
    flash("That medallion is from another team");
}

void journey_tag(const char *uid)
{
    if (!journey_loaded() || !uid || !uid[0]) return;
    const cJSON *p = person_for_uid(uid);
    int station = 0;
    const char *place = p ? NULL : place_for_uid(uid, &station);

    switch (E.mode) {
    case JV_GATHER:
        if (p) roll_call_tap(p);
        else if (place) flash("Everyone taps their medallion first");
        else { char m[64]; snprintf(m, sizeof m, "Card %.20s isn't on the roster", uid); flash(m); }
        return;

    case JV_DARK:
        if (!p) return;
        if (is_leader(p) && member_index(p) < 0) { restore(); return; }     /* a visiting leader can always help */
        if (strcmp(E.dark_restore, "leader") == 0) { if (is_leader(p)) restore(); return; }
        if (strcmp(E.dark_restore, "unity") == 0) {
            int mi = member_index(p);
            if (mi < 0) return;
            if (!E.tapped[mi]) { E.tapped[mi] = true; E.ntapped++; }
            if (E.ntapped >= E.nmembers) restore();
        }
        return;

    case JV_TASK:
        if (p && now() < E.tag_quiet_until) return;           /* the same medallions, still on the reader */
        break;
    default:
        return;
    }

    cJSON *tk = task_obj();
    if (is("checkin")) {
        if (place && list_has(obj(tk, "tags"), place)) {
            uint8_t b[10]; int n;
            uid_bytes(uid, b, &n);
            if (E.h.checkin) E.h.checkin(b, n, station);
            flash("Found it!");
            complete_task();
        } else if (place) flash("Not this place");
        else if (p && is_leader(p) && member_index(p) < 0) complete_task();   /* a station leader vouches */
        else if (p) flash("That's a medallion. Find the place's tag.");
        return;
    }
    if (is("gather_fragments")) {
        if (!place) return;
        int i = list_index(obj(tk, "tags"), place);
        if (i >= 0 && i < MAX_FRAGMENTS) {
            if (!E.frag_found[i]) { E.frag_found[i] = true; flash("Fragment found"); bump(); }
            int n = cJSON_GetArraySize(obj(tk, "tags")), got = 0;
            for (int k = 0; k < n && k < MAX_FRAGMENTS; k++) got += E.frag_found[k];
            if (got >= n) complete_task();
        } else if (list_has(obj(tk, "decoys"), place)) {
            flash("A false fragment");
            darken();
        } else flash("Not a fragment");
        return;
    }
    if (is("testimony")) {
        if (!p) return;
        int mi = member_index(p);
        if (mi >= 0 && !E.tapped[mi]) { E.tapped[mi] = true; E.ntapped++; flash(str(p, "name")); }
        if (E.ntapped >= E.nmembers || (is_leader(p) && mi < 0)) complete_task();
        return;
    }
    /* leader_approve, rest, and anything the ball can't judge: a leader's card moves on */
    if (p && is_leader(p)) { complete_task(); return; }
    if (p && !strcmp(task_type(), "leader_approve")) flash("A leader needs to tap their medallion");
}

static int answer_index(void)
{
    cJSON *a = obj(task_obj(), "answer");
    if (is("true_false")) return cJSON_IsTrue(a) ? 0 : 1;
    return cJSON_IsNumber(a) ? a->valueint : -1;
}

void journey_choice(int i)
{
    if (!journey_loaded() || E.mode != JV_TASK) return;
    if (!(is("question") || is("true_false") || is("name_that_hymn"))) return;
    if (i == answer_index()) { flash("Correct"); complete_task(); }
    else { flash("Not quite"); darken(); }
}

void journey_continue(void)
{
    if (!journey_loaded()) return;
    if (E.mode == JV_BRIEF) { start_task(0); return; }
    if (E.mode == JV_ERROR || E.mode == JV_DONE) return;
    if (E.mode == JV_TASK && (is("intro") || is("audio_clue"))) complete_task();
}

static int hint_wait(void)
{
    return (int)num(task_obj(), "hint_after", E.hint_default);
}
static bool hint_ready(void) { return now() - E.task_start >= (int64_t)hint_wait() * 1000; }
static int text_hint_count(void) { return cJSON_GetArraySize(obj(task_obj(), "hints")); }
static int audio_hint_count(void) { return audio_task() ? (int)num(task_obj(), "audio_hints", 0) : 0; }
static int replay_count(void) { return audio_task() ? (int)num(task_obj(), "replays", 0) : 0; }

void journey_hint(void)
{
    if (!journey_loaded() || E.mode != JV_TASK || !hint_ready()) return;
    bool used = false;
    if (E.audio_hints_used < audio_hint_count()) {
        cJSON *tk = task_obj();
        int add = (int)(num(tk, "hint_adds", 5) * 1000);
        const char *mode = str(tk, "hint_audio");
        E.audio_hints_used++;
        if (mode && strcmp(mode, "restart") == 0) {
            E.audio_len_ms += add;
            play_window(task_start_ms(), E.audio_len_ms);
        } else {
            play_window(E.audio_pos_ms, add);
            E.audio_pos_ms += add;
            E.audio_len_ms += add;
        }
        used = true;
    }
    if (E.text_hints_used < text_hint_count()) {
        copy(E.hint, sizeof(E.hint), cJSON_GetArrayItem(obj(task_obj(), "hints"), E.text_hints_used)->valuestring);
        E.text_hints_used++;
        used = true;
    }
    if (used) bump();
}

void journey_replay(void)
{
    if (!journey_loaded() || E.mode != JV_TASK || !audio_task() || !hint_ready()) return;
    if (E.replays_used >= replay_count()) return;
    E.replays_used++;
    play_window(task_start_ms(), E.audio_len_ms);
    bump();
}

void journey_leader(int cmd, int arg)
{
    if (!journey_loaded()) return;
    switch (cmd) {
    case 1:                                       /* unlock: open whatever is waiting */
        if (E.mode == JV_GATHER) enter_brief();
        else if (E.mode == JV_DARK) restore();
        else if (E.mode == JV_BRIEF) start_task(0);
        else if (E.mode == JV_TASK) complete_task();
        break;
    case 2:                                       /* skip the current task / stage start */
        if (E.mode == JV_DARK) { E.mode = JV_TASK; if (E.h.leds) E.h.leds(0); complete_task(); }
        else if (E.mode == JV_GATHER || E.mode == JV_BRIEF) start_task(0);
        else if (E.mode == JV_TASK) complete_task();
        break;
    case 5:                                       /* jump to stage <arg> (1-based) */
        if (arg >= 1 && arg <= E.nstages + 1) start_stage(arg - 1);
        else if (arg == 0) journey_restart();
        break;
    default:
        break;
    }
}

int journey_stage_no(void)
{
    if (!journey_loaded()) return 0;
    return E.stage < E.nstages ? E.stage + 1 : E.nstages;
}

/* ------------------------------------------------------------------ view */
static void add_choice(jv_view_t *v, const char *c)
{
    if (v->nchoices < JV_MAX_CHOICES && c) copy(v->choices[v->nchoices++], sizeof(v->choices[0]), c);
}

static const char *STD_INTRO =
    "I am the Liahona. I will point the way, and I will write upon myself what you need to know. "
    "I work by your faith, diligence and heed. If you quarrel or answer carelessly, I will grow dark. "
    "Work together and I will shine again.";

void journey_view(jv_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->version = E.version;
    v->countdown_s = -1;
    if (!E.loaded) { v->mode = JV_NONE; return; }
    v->mode = E.mode;
    if (E.mode == JV_ERROR) {
        copy(v->heading, sizeof(v->heading), "Journey can't load");
        copy(v->body, sizeof(v->body), E.err);
        return;
    }
    v->stage_count = E.nstages;
    v->stage_no = journey_stage_no();
    int64_t t = now();
    if (E.flash_until > t) copy(v->status, sizeof(v->status), E.flash);

    if (E.mode == JV_DONE) {
        copy(v->title, sizeof(v->title), str(E.root, "title"));
        copy(v->heading, sizeof(v->heading), "Journey complete");
        copy(v->body, sizeof(v->body), "You followed the Liahona to the end. Return to camp.");
        return;
    }

    cJSON *st = stage_obj();
    copy(v->title, sizeof(v->title), str(st, "name"));

    if (E.mode == JV_GATHER) {
        copy(v->heading, sizeof(v->heading), "Gather your team");
        snprintf(v->body, sizeof(v->body), "Everyone on %s taps their medallion to begin.",
                 E.team_name[0] ? E.team_name : "the team");
        if (!v->status[0]) snprintf(v->status, sizeof(v->status), "%d of %d have tapped", E.ntapped, E.nmembers);
        return;
    }

    if (E.mode == JV_BRIEF) {
        cJSON *b = obj(st, "brief");
        copy(v->ref, sizeof(v->ref), str(b, "ref"));
        copy(v->body, sizeof(v->body), str(b, "text"));
        if (!v->body[0] && v->ref[0]) snprintf(v->body, sizeof(v->body), "Read %s together.", v->ref);
        v->can_continue = true;
        copy(v->continue_label, sizeof(v->continue_label), "Begin");
        return;
    }

    cJSON *tk = task_obj();
    copy(v->kind, sizeof(v->kind), task_type());

    if (E.mode == JV_DARK) {
        if (E.dark_scripted) {
            cJSON *b = obj(tk, "brief");
            copy(v->ref, sizeof(v->ref), str(b, "ref"));
            copy(v->body, sizeof(v->body), str(b, "text"));
        } else {
            snprintf(v->body, sizeof(v->body), "%s%sThe Liahona works by faith and diligence.",
                     E.dark_reason, E.dark_reason[0] ? ". " : "");
        }
        copy(v->heading, sizeof(v->heading), "The Liahona has gone dark");
        if (strcmp(E.dark_restore, "unity") == 0 && E.nmembers > 0) {
            if (!v->status[0]) snprintf(v->status, sizeof(v->status), "Unite: %d of %d have tapped", E.ntapped, E.nmembers);
        } else if (strcmp(E.dark_restore, "leader") == 0) {
            if (!v->status[0]) copy(v->status, sizeof(v->status), "A leader must restore it");
        }
        if (E.dark_until) v->countdown_s = (int)((E.dark_until - t + 999) / 1000);
        return;
    }

    /* JV_TASK */
    if (E.deadline) v->countdown_s = (int)((E.deadline - t + 999) / 1000);
    const char *type = task_type();
    const char *prompt = str(tk, "prompt");
    bool ready = hint_ready();
    v->can_hint = ready && (E.text_hints_used < text_hint_count() || E.audio_hints_used < audio_hint_count());
    copy(v->hint, sizeof(v->hint), E.hint);
    v->audio = audio_task();
    v->can_replay = v->audio && ready && E.replays_used < replay_count();

    if (!strcmp(type, "intro")) {
        copy(v->heading, sizeof(v->heading), "The Liahona speaks");
        copy(v->body, sizeof(v->body), str(tk, "text") ? str(tk, "text") : STD_INTRO);
        v->can_continue = true;
        copy(v->continue_label, sizeof(v->continue_label), "We are ready");
    } else if (!strcmp(type, "question") || !strcmp(type, "name_that_hymn")) {
        copy(v->heading, sizeof(v->heading), !strcmp(type, "question") ? "Question" : "Name that hymn");
        copy(v->body, sizeof(v->body), prompt ? prompt : "Which hymn is this?");
        const cJSON *c;
        cJSON_ArrayForEach(c, obj(tk, "choices")) if (cJSON_IsString(c)) add_choice(v, c->valuestring);
    } else if (!strcmp(type, "true_false")) {
        copy(v->heading, sizeof(v->heading), "True or false");
        copy(v->body, sizeof(v->body), prompt);
        add_choice(v, "True");
        add_choice(v, "False");
    } else if (!strcmp(type, "audio_clue")) {
        copy(v->heading, sizeof(v->heading), "Listen");
        copy(v->body, sizeof(v->body), prompt ? prompt : "Where does this song send you?");
        v->can_continue = true;
        copy(v->continue_label, sizeof(v->continue_label), "We know where");
    } else if (!strcmp(type, "checkin")) {
        cJSON *tr = obj(tk, "travel");
        const char *to = str(tr, "to");
        const cJSON *tags = obj(tk, "tags");
        if (!to && cJSON_GetArraySize(tags) > 0) to = cJSON_GetArrayItem(tags, 0)->valuestring;
        const cJSON *pl = obj(E.places, to);
        double lat = num(pl, "lat", 1000), lon = num(pl, "lon", 1000);
        const char *mode = str(tr, "mode");
        copy(v->heading, sizeof(v->heading), mode && !strcmp(mode, "race") ? "Race" : "Journey on");
        if (lat < 999 && lon < 999 && !(mode && !strcmp(mode, "directions"))) {
            v->has_target = true;
            v->target_lat = lat;
            v->target_lon = lon;
            copy(v->body, sizeof(v->body), "Follow the gold spindle, then tap the tag there.");  /* no place name: finding it is the point */
        } else {
            copy(v->body, sizeof(v->body), "Find the place, then tap the tag there.");  /* never name it */
        }
        if (cJSON_GetArraySize(tags) > 1) snprintf(v->body + strlen(v->body), sizeof(v->body) - strlen(v->body),
                                                   " (%d places count.)", cJSON_GetArraySize(tags));
    } else if (!strcmp(type, "gather_fragments")) {
        int n = cJSON_GetArraySize(obj(tk, "tags")), got = 0;
        for (int k = 0; k < n && k < MAX_FRAGMENTS; k++) got += E.frag_found[k];
        copy(v->heading, sizeof(v->heading), "Gather the fragments");
        snprintf(v->body, sizeof(v->body), "Find every fragment and tap its tag. Beware: a false fragment will darken the Liahona.");
        if (!v->status[0]) snprintf(v->status, sizeof(v->status), "%d of %d found", got, n);
    } else if (!strcmp(type, "leader_approve")) {
        copy(v->heading, sizeof(v->heading), "Challenge");
        copy(v->body, sizeof(v->body), prompt);
        if (!v->status[0]) copy(v->status, sizeof(v->status), "A leader taps their medallion when you've done it");
    } else if (!strcmp(type, "testimony")) {
        copy(v->heading, sizeof(v->heading), "Testimony");
        copy(v->body, sizeof(v->body), prompt ? prompt : "Each of you share something you know is true, then tap your medallion.");
        if (!v->status[0]) snprintf(v->status, sizeof(v->status), "%d of %d have shared", E.ntapped, E.nmembers);
    } else if (!strcmp(type, "rest")) {
        copy(v->heading, sizeof(v->heading), "Rest");
        snprintf(v->body, sizeof(v->body), "%s", prompt ? prompt : "Rest now.");
        const char *u = str(tk, "until");
        if (u && !v->status[0]) snprintf(v->status, sizeof(v->status), "The Liahona wakes at %s", u);
    } else {
        /* not supported on the ball yet: show what we can, a leader moves it on */
        copy(v->heading, sizeof(v->heading), "Challenge");
        copy(v->body, sizeof(v->body), prompt ? prompt : "Your leader will explain this challenge.");
        if (!v->status[0]) copy(v->status, sizeof(v->status), "A leader taps their medallion to continue");
    }
}
