/*
 * Base station <-> laptop text protocol (pure C, no ESP-IDF, so it can be unit-tested).
 *
 * Laptop -> base, one per line, either JSON or plain words (handy in a serial monitor):
 *   {"cmd":"ping","to":3}                ping 3          (to "all" / 255 = every ball)
 *   {"cmd":"msg","to":3,"text":"Hi"}      msg 3 Hi there
 *   {"cmd":"unlock","to":3}               unlock 3        (also skip, dim, restore)
 *   {"cmd":"stage","to":3,"stage":4}      stage 3 4
 *   {"cmd":"power","dbm":20}              power 20
 *   {"cmd":"info"}                        info
 *
 * Base -> laptop, one JSON object per line, always with "ev":
 *   hello, status, checkin, ack, sent, timeout, error, info
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "liahona_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BC_NONE = 0, BC_PING, BC_MSG, BC_COMMAND, BC_POWER, BC_INFO } bc_kind_t;

typedef struct {
    bc_kind_t kind;
    uint8_t   to;                        /* ball id or LP_ID_ALL */
    uint8_t   cmd, arg;                  /* BC_COMMAND */
    int       dbm;                       /* BC_POWER */
    char      text[LP_TEXT_MAX + 1];     /* BC_MSG */
} bridge_cmd_t;

/* Parse one line. Returns 0 on success, or fills err (short reason) and returns -1. */
int bridge_parse_line(const char *line, bridge_cmd_t *out, char *err, size_t err_cap);

/* Build the radio packet for a command (not for BC_POWER / BC_INFO). */
int bridge_build_packet(const bridge_cmd_t *c, uint8_t seq, lp_packet_t *out);

/* One JSON line (with "\n") for a packet heard from a ball. Returns length. */
size_t bridge_format_rx(const lp_packet_t *p, int16_t rssi, float snr, char *out, size_t cap);

/* JSON-escape s into out (no surrounding quotes). */
void bridge_json_escape(const char *s, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
