/*
 * Liahona radio protocol: the packets that travel between the balls and the base station.
 * Shared by the ball firmware (firmware/main) and the base station (base_station/).
 *
 * Every packet starts with a 6-byte header:
 *   [0] 'L' magic   [1] version   [2] type   [3] from   [4] to   [5] seq
 * IDs: base station = 0, balls = 1..254, 255 = everyone.
 * Multi-byte numbers are little-endian. LoRa's own CRC checks the bytes.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LP_MAGIC        0x4C    /* 'L' */
#define LP_VERSION      1
#define LP_HDR_LEN      6
#define LP_MAX_LEN      64      /* whole packet */
#define LP_TEXT_MAX     48      /* leader message text, bytes (UTF-8) */

#define LP_ID_BASE      0
#define LP_ID_ALL       255

typedef enum {
    /* ball -> base */
    LP_STATUS   = 0x01,     /* position, battery, lid, stage */
    LP_CHECKIN  = 0x02,     /* an NFC tag was tapped at a station (base ACKs) */
    LP_UPDATE   = 0x03,     /* Wi-Fi update progress (no ACK) */
    /* base -> ball */
    LP_PING     = 0x10,     /* "reply with your status now" */
    LP_MESSAGE  = 0x11,     /* text for the screen (ball ACKs) */
    LP_COMMAND  = 0x12,     /* unlock / skip / dim / restore / stage (ball ACKs) */
    /* both ways */
    LP_ACK      = 0x20,
} lp_type_t;

typedef enum {
    LP_CMD_UNLOCK  = 1,     /* open the current step */
    LP_CMD_SKIP    = 2,     /* move past the current step */
    LP_CMD_DIM     = 3,     /* screen + LEDs to rest mode */
    LP_CMD_RESTORE = 4,     /* undo dim */
    LP_CMD_STAGE   = 5,     /* jump to step <arg> */
    LP_CMD_UPDATE  = 6,     /* join Wi-Fi and fetch new content + firmware from the Manager */
} lp_cmd_t;

/* LP_UPDATE states */
typedef enum {
    LP_UPD_WIFI = 1,        /* joining Wi-Fi */
    LP_UPD_CHECK = 2,       /* asking the Manager what changed */
    LP_UPD_FILES = 3,       /* downloading files: done / total */
    LP_UPD_FIRMWARE = 4,    /* installing firmware: detail = percent */
    LP_UPD_DONE = 5,        /* finished: done = files changed, version = running firmware */
    LP_UPD_CURRENT = 6,     /* nothing to do */
    LP_UPD_FAILED = 7,      /* detail = lp_upd_err_t */
    LP_UPD_REBOOT = 8,      /* new firmware installed, restarting */
} lp_upd_state_t;

typedef enum {
    LP_UPE_NO_CONFIG = 1,   /* no liahona/wifi.json on the SD card */
    LP_UPE_WIFI = 2,        /* couldn't join the network */
    LP_UPE_MANAGER = 3,     /* Manager unreachable or bad reply */
    LP_UPE_KEY = 4,         /* Manager refused the ball key */
    LP_UPE_DOWNLOAD = 5,    /* a file download failed */
    LP_UPE_SD = 6,          /* couldn't write the SD card */
    LP_UPE_FIRMWARE = 7,    /* firmware download or check failed */
    LP_UPE_BUSY = 8,        /* already updating */
} lp_upd_err_t;

#define LP_VER_MAX 15

/* status flags */
#define LP_F_GPS_FIX    0x01
#define LP_F_LID_OPEN   0x02
#define LP_F_LOW_BATT   0x04

typedef struct {
    uint8_t type, from, to, seq;
    union {
        struct {
            int32_t  lat_e7, lon_e7;    /* degrees x 1e7 */
            uint16_t batt_mv;
            uint8_t  flags;
            uint8_t  stage;
            uint16_t heading_d10;       /* degrees x 10, 0..3599 */
        } status;
        struct {
            uint8_t uid_len;            /* 4..10 */
            uint8_t uid[10];
            uint8_t station;
        } checkin;
        struct {
            uint8_t len;
            char    text[LP_TEXT_MAX + 1];   /* NUL-terminated after decode */
        } message;
        struct {
            uint8_t cmd;                /* lp_cmd_t */
            uint8_t arg;
        } command;
        struct {
            uint8_t state;              /* lp_upd_state_t */
            uint8_t detail;             /* percent or lp_upd_err_t */
            uint8_t done, total;        /* files */
            char    version[LP_VER_MAX + 1];
        } update;
        struct {
            uint8_t acked_seq;
            uint8_t ok;                 /* 1 = applied, 0 = refused/unknown */
        } ack;
    } u;
} lp_packet_t;

/* Encode p into buf. Returns the byte length, or 0 if it doesn't fit / is invalid. */
size_t lp_encode(const lp_packet_t *p, uint8_t *buf, size_t cap);

/* Decode buf into p. Returns true if it is a valid Liahona packet. */
bool lp_decode(const uint8_t *buf, size_t len, lp_packet_t *p);

/* Does this packet need an ACK from the receiver? */
static inline bool lp_wants_ack(const lp_packet_t *p)
{
    return p->to != LP_ID_ALL &&
           (p->type == LP_MESSAGE || p->type == LP_COMMAND || p->type == LP_CHECKIN);
}

const char *lp_type_name(uint8_t type);
const char *lp_cmd_name(uint8_t cmd);
const char *lp_upd_state_name(uint8_t state);
const char *lp_upd_err_name(uint8_t err);
int lp_cmd_from_name(const char *name);     /* -1 if unknown */

#ifdef __cplusplus
}
#endif
