#include <string.h>
#include "liahona_proto.h"

static void put16(uint8_t *b, uint16_t v) { b[0] = v & 0xFF; b[1] = v >> 8; }
static void put32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (v >> (8 * i)) & 0xFF; }
static uint16_t get16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

size_t lp_encode(const lp_packet_t *p, uint8_t *buf, size_t cap)
{
    uint8_t tmp[LP_MAX_LEN];
    size_t n = LP_HDR_LEN;
    tmp[0] = LP_MAGIC; tmp[1] = LP_VERSION; tmp[2] = p->type;
    tmp[3] = p->from;  tmp[4] = p->to;      tmp[5] = p->seq;

    switch (p->type) {
    case LP_STATUS:
        put32(tmp + n, (uint32_t)p->u.status.lat_e7); n += 4;
        put32(tmp + n, (uint32_t)p->u.status.lon_e7); n += 4;
        put16(tmp + n, p->u.status.batt_mv); n += 2;
        tmp[n++] = p->u.status.flags;
        tmp[n++] = p->u.status.stage;
        put16(tmp + n, p->u.status.heading_d10); n += 2;
        break;
    case LP_CHECKIN:
        if (p->u.checkin.uid_len > sizeof(p->u.checkin.uid)) return 0;
        tmp[n++] = p->u.checkin.uid_len;
        memcpy(tmp + n, p->u.checkin.uid, p->u.checkin.uid_len); n += p->u.checkin.uid_len;
        tmp[n++] = p->u.checkin.station;
        break;
    case LP_PING:
        break;
    case LP_UPDATE: {
        size_t vl = 0;
        while (vl < LP_VER_MAX && p->u.update.version[vl]) vl++;
        tmp[n++] = p->u.update.state;
        tmp[n++] = p->u.update.detail;
        tmp[n++] = p->u.update.done;
        tmp[n++] = p->u.update.total;
        tmp[n++] = (uint8_t)vl;
        memcpy(tmp + n, p->u.update.version, vl); n += vl;
        break;
    }
    case LP_MESSAGE: {
        size_t len = p->u.message.len;
        if (len > LP_TEXT_MAX) return 0;
        tmp[n++] = (uint8_t)len;
        memcpy(tmp + n, p->u.message.text, len); n += len;
        break;
    }
    case LP_COMMAND:
        tmp[n++] = p->u.command.cmd;
        tmp[n++] = p->u.command.arg;
        break;
    case LP_ACK:
        tmp[n++] = p->u.ack.acked_seq;
        tmp[n++] = p->u.ack.ok;
        break;
    default:
        return 0;
    }
    if (n > cap) return 0;
    memcpy(buf, tmp, n);
    return n;
}

bool lp_decode(const uint8_t *b, size_t len, lp_packet_t *p)
{
    if (len < LP_HDR_LEN || b[0] != LP_MAGIC || b[1] != LP_VERSION) return false;
    memset(p, 0, sizeof(*p));
    p->type = b[2]; p->from = b[3]; p->to = b[4]; p->seq = b[5];
    const uint8_t *d = b + LP_HDR_LEN;
    size_t n = len - LP_HDR_LEN;

    switch (p->type) {
    case LP_STATUS:
        if (n < 14) return false;
        p->u.status.lat_e7 = (int32_t)get32(d);
        p->u.status.lon_e7 = (int32_t)get32(d + 4);
        p->u.status.batt_mv = get16(d + 8);
        p->u.status.flags = d[10];
        p->u.status.stage = d[11];
        p->u.status.heading_d10 = get16(d + 12);
        return true;
    case LP_CHECKIN:
        if (n < 2 || d[0] > sizeof(p->u.checkin.uid) || n < (size_t)d[0] + 2) return false;
        p->u.checkin.uid_len = d[0];
        memcpy(p->u.checkin.uid, d + 1, d[0]);
        p->u.checkin.station = d[1 + d[0]];
        return true;
    case LP_PING:
        return true;
    case LP_UPDATE:
        if (n < 5 || d[4] > LP_VER_MAX || n < (size_t)d[4] + 5) return false;
        p->u.update.state = d[0];
        p->u.update.detail = d[1];
        p->u.update.done = d[2];
        p->u.update.total = d[3];
        memcpy(p->u.update.version, d + 5, d[4]);
        p->u.update.version[d[4]] = '\0';
        return true;
    case LP_MESSAGE:
        if (n < 1 || d[0] > LP_TEXT_MAX || n < (size_t)d[0] + 1) return false;
        p->u.message.len = d[0];
        memcpy(p->u.message.text, d + 1, d[0]);
        p->u.message.text[d[0]] = '\0';
        return true;
    case LP_COMMAND:
        if (n < 2) return false;
        p->u.command.cmd = d[0];
        p->u.command.arg = d[1];
        return true;
    case LP_ACK:
        if (n < 2) return false;
        p->u.ack.acked_seq = d[0];
        p->u.ack.ok = d[1];
        return true;
    default:
        return false;
    }
}

const char *lp_type_name(uint8_t t)
{
    switch (t) {
    case LP_STATUS: return "status";   case LP_CHECKIN: return "checkin";
    case LP_PING: return "ping";       case LP_MESSAGE: return "msg";
    case LP_COMMAND: return "command"; case LP_ACK: return "ack";
    case LP_UPDATE: return "update";
    default: return "unknown";
    }
}

static const char *const CMD_NAMES[] = { "", "unlock", "skip", "dim", "restore", "stage", "update" };
#define CMD_LAST 6

const char *lp_cmd_name(uint8_t c)
{
    return (c >= 1 && c <= CMD_LAST) ? CMD_NAMES[c] : "unknown";
}

static const char *const UPD_NAMES[] = { "", "wifi", "check", "files", "firmware", "done", "current", "failed", "reboot" };
const char *lp_upd_state_name(uint8_t s) { return (s >= 1 && s <= 8) ? UPD_NAMES[s] : "unknown"; }

static const char *const UPE_NAMES[] = { "", "no wifi.json on the SD card", "couldn't join Wi-Fi",
    "Manager unreachable", "ball key refused", "download failed", "SD card write failed",
    "firmware failed", "already updating" };
const char *lp_upd_err_name(uint8_t e) { return (e >= 1 && e <= 8) ? UPE_NAMES[e] : "unknown"; }

int lp_cmd_from_name(const char *name)
{
    for (int i = 1; i <= CMD_LAST; i++)
        if (strcmp(name, CMD_NAMES[i]) == 0) return i;
    return -1;
}
