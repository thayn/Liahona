#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge.h"

static void set_err(char *err, size_t cap, const char *msg)
{
    if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = '\0'; }
}

/* ---------- tiny JSON helpers: flat objects only ---------- */

/* Find "key": and return a pointer to the start of its value, or NULL. */
static const char *json_find(const char *js, const char *key)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = js;
    while ((p = strstr(p, pat)) != NULL) {
        p += strlen(pat);
        while (isspace((unsigned char)*p)) p++;
        if (*p == ':') {
            p++;
            while (isspace((unsigned char)*p)) p++;
            return p;
        }
    }
    return NULL;
}

/* Copy a JSON string value (p points at the opening quote). Returns 0 or -1. */
static int json_string(const char *p, char *out, size_t cap)
{
    if (!p || *p != '"') return -1;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '"': case '\\': case '/': c = e; break;
            case 'u': {                                   /* \uXXXX: keep ASCII, else '?' */
                unsigned v = 0;
                for (int i = 0; i < 4 && isxdigit((unsigned char)*p); i++, p++)
                    v = v * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
                c = (v < 0x80) ? (char)v : '?';
                break;
            }
            default: return -1;
            }
        }
        if (n + 1 < cap) out[n++] = c;
    }
    if (*p != '"') return -1;
    out[n] = '\0';
    return 0;
}

/* Ball id: a number 1..254, or "all"/255. Returns -1 if invalid. */
static int parse_target_word(const char *w)
{
    if (strcmp(w, "all") == 0) return LP_ID_ALL;
    char *end;
    long v = strtol(w, &end, 10);
    if (end == w || *end) return -1;
    if (v == LP_ID_ALL || (v >= 1 && v <= 254)) return (int)v;
    return -1;
}

static int json_target(const char *js)
{
    const char *p = json_find(js, "to");
    if (!p) return -1;
    if (*p == '"') {
        char w[8];
        if (json_string(p, w, sizeof(w))) return -1;
        return parse_target_word(w);
    }
    char w[8] = { 0 };
    size_t i = 0;
    while (i + 1 < sizeof(w) && (isdigit((unsigned char)p[i]))) { w[i] = p[i]; i++; }
    return parse_target_word(w);
}

static int json_int(const char *js, const char *key, int *out)
{
    const char *p = json_find(js, key);
    if (!p) return -1;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    *out = (int)v;
    return 0;
}

/* ---------- parsing ---------- */

static int finish(const char *name, int to, int num, const char *text, bridge_cmd_t *o, char *err, size_t ec)
{
    memset(o, 0, sizeof(*o));
    if (strcmp(name, "info") == 0) { o->kind = BC_INFO; return 0; }
    if (strcmp(name, "power") == 0) {
        if (num < -9 || num > 22) { set_err(err, ec, "power must be -9..22 dBm"); return -1; }
        o->kind = BC_POWER; o->dbm = num; return 0;
    }
    if (to < 0) { set_err(err, ec, "need a ball number 1-254 or all"); return -1; }
    o->to = (uint8_t)to;
    if (strcmp(name, "ping") == 0) { o->kind = BC_PING; return 0; }
    if (strcmp(name, "msg") == 0) {
        if (!text || !*text) { set_err(err, ec, "msg needs text"); return -1; }
        if (strlen(text) > LP_TEXT_MAX) { set_err(err, ec, "msg text over 48 bytes"); return -1; }
        o->kind = BC_MSG;
        strncpy(o->text, text, LP_TEXT_MAX);
        return 0;
    }
    int c = lp_cmd_from_name(name);
    if (c < 0) { set_err(err, ec, "unknown command"); return -1; }
    o->kind = BC_COMMAND;
    o->cmd = (uint8_t)c;
    if (c == LP_CMD_STAGE) {
        if (num < 0 || num > 255) { set_err(err, ec, "stage needs a step number 0-255"); return -1; }
        o->arg = (uint8_t)num;
    }
    return 0;
}

int bridge_parse_line(const char *line, bridge_cmd_t *o, char *err, size_t ec)
{
    while (isspace((unsigned char)*line)) line++;
    if (!*line) { set_err(err, ec, "empty"); return -1; }

    if (*line == '{') {
        char name[16], text[LP_TEXT_MAX * 2 + 1] = "";
        if (json_string(json_find(line, "cmd"), name, sizeof(name))) { set_err(err, ec, "missing \"cmd\""); return -1; }
        const char *tp = json_find(line, "text");
        if (tp && json_string(tp, text, sizeof(text))) { set_err(err, ec, "bad \"text\""); return -1; }
        int num = -1;
        if (json_int(line, "stage", &num) && json_int(line, "dbm", &num)) num = -1000;
        return finish(name, json_target(line), num, text, o, err, ec);
    }

    /* plain words: <cmd> [ball] [number | text...] */
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    char *name = strtok(buf, " ");
    for (char *q = name; q && *q; q++) *q = (char)tolower((unsigned char)*q);
    char *rest = name ? strtok(NULL, "") : NULL;
    if (!name) { set_err(err, ec, "empty"); return -1; }

    if (strcmp(name, "power") == 0) {
        return finish(name, -1, rest ? atoi(rest) : -1000, NULL, o, err, ec);
    }
    if (strcmp(name, "info") == 0) return finish(name, -1, 0, NULL, o, err, ec);

    char *who = rest ? strtok(rest, " ") : NULL;
    char *tail = who ? strtok(NULL, "") : NULL;
    int to = who ? parse_target_word(who) : -1;
    int num = (tail && strcmp(name, "stage") == 0) ? atoi(tail) : -1;
    return finish(name, to, num, tail, o, err, ec);
}

int bridge_build_packet(const bridge_cmd_t *c, uint8_t seq, lp_packet_t *p)
{
    memset(p, 0, sizeof(*p));
    p->from = LP_ID_BASE;
    p->to = c->to;
    p->seq = seq;
    switch (c->kind) {
    case BC_PING:    p->type = LP_PING; return 0;
    case BC_MSG:
        p->type = LP_MESSAGE;
        p->u.message.len = (uint8_t)strlen(c->text);
        memcpy(p->u.message.text, c->text, p->u.message.len);
        return 0;
    case BC_COMMAND:
        p->type = LP_COMMAND;
        p->u.command.cmd = c->cmd;
        p->u.command.arg = c->arg;
        return 0;
    default:
        return -1;
    }
}

/* ---------- formatting ---------- */

void bridge_json_escape(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    for (; *s && n + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
        else if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (c < 0x20) n += (size_t)snprintf(out + n, cap - n, "\\u%04x", c);
        else out[n++] = (char)c;
    }
    out[n] = '\0';
}

size_t bridge_format_rx(const lp_packet_t *p, int16_t rssi, float snr, char *o, size_t cap)
{
    int n = 0;
    switch (p->type) {
    case LP_STATUS: {
        const bool fix = p->u.status.flags & LP_F_GPS_FIX;
        n = snprintf(o, cap, "{\"ev\":\"status\",\"ball\":%u,\"seq\":%u,\"fix\":%s,", p->from, p->seq,
                     fix ? "true" : "false");
        if (fix)
            n += snprintf(o + n, cap - n, "\"lat\":%.7f,\"lon\":%.7f,",
                          p->u.status.lat_e7 / 1e7, p->u.status.lon_e7 / 1e7);
        else
            n += snprintf(o + n, cap - n, "\"lat\":null,\"lon\":null,");
        n += snprintf(o + n, cap - n,
                      "\"batt\":%.2f,\"low_batt\":%s,\"lid\":\"%s\",\"stage\":%u,\"heading\":%.1f,",
                      p->u.status.batt_mv / 1000.0, (p->u.status.flags & LP_F_LOW_BATT) ? "true" : "false",
                      (p->u.status.flags & LP_F_LID_OPEN) ? "open" : "closed", p->u.status.stage,
                      p->u.status.heading_d10 / 10.0);
        break;
    }
    case LP_CHECKIN: {
        char uid[2 * sizeof(p->u.checkin.uid) + 1] = "";
        for (int i = 0; i < p->u.checkin.uid_len; i++) sprintf(uid + 2 * i, "%02X", p->u.checkin.uid[i]);
        n = snprintf(o, cap, "{\"ev\":\"checkin\",\"ball\":%u,\"seq\":%u,\"uid\":\"%s\",\"station\":%u,",
                     p->from, p->seq, uid, p->u.checkin.station);
        break;
    }
    case LP_ACK:
        n = snprintf(o, cap, "{\"ev\":\"ack\",\"ball\":%u,\"ack_seq\":%u,\"ok\":%s,",
                     p->from, p->u.ack.acked_seq, p->u.ack.ok ? "true" : "false");
        break;
    default:
        n = snprintf(o, cap, "{\"ev\":\"other\",\"ball\":%u,\"type\":\"%s\",", p->from, lp_type_name(p->type));
        break;
    }
    if (n < 0 || (size_t)n >= cap) return 0;
    n += snprintf(o + n, cap - n, "\"rssi\":%d,\"snr\":%.1f}\n", rssi, snr);
    return ((size_t)n < cap) ? (size_t)n : 0;
}
