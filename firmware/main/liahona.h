/*
 * Shared state between the hardware tasks and the screen.
 * Hardware tasks write into g_state while holding the lock; the UI reads it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"

typedef enum { TEST_PENDING = 0, TEST_PASS, TEST_FAIL, TEST_NA } test_result_t;

typedef struct {
    /* I2C scan */
    uint8_t  i2c_addrs[16];
    int      i2c_count;

    /* Compass (LIS3MDL) */
    test_result_t mag;
    float    mag_x, mag_y, mag_z;      /* gauss */
    float    heading_deg;              /* 0 = magnetic north, uncalibrated */

    /* GPS (PA1010D) */
    test_result_t gps;
    bool     gps_fix;
    int      gps_sats;                 /* used in the fix */
    int      gps_in_view;              /* visible in the sky */
    int      gps_heard;                /* with a signal, last 5 s */
    int      gps_best_snr;             /* strongest signal, last 5 s (dB-Hz) */
    bool     quiet_mode;               /* screen off for the GPS noise test */
    double   gps_lat, gps_lon;
    char     gps_time[12];             /* hh:mm:ss UTC */

    /* Station the gold spindle points to (bench: "Mark" on the self-test page) */
    bool     target_set;
    double   target_lat, target_lon;
    uint32_t gps_bytes;                /* NMEA bytes seen (proves it talks) */

    /* NFC (PN532) */
    test_result_t nfc;
    uint32_t nfc_fw;                   /* IC/ver/rev */
    char     nfc_uid[24];              /* last tag UID as hex, "" if none */
    bool     nfc_tag_present;
    uint32_t nfc_tap_seq;              /* bumps each time a tag arrives */
    char     nfc_tap_uid[24];          /* that tag, uppercase hex, no separators */

    /* LoRa (SX1262) */
    test_result_t lora;                /* PASS = chip found on SPI */
    uint8_t  lora_sync[2];
    uint8_t  lora_status;
    bool     lora_ready;               /* fully set up and listening */
    bool     lora_tx_enabled;
    bool     lora_tcxo;
    int      lora_err;                 /* last driver error, 0 = none */
    uint8_t  ball_id;
    uint32_t lora_rx_count, lora_tx_count;
    int16_t  lora_last_rssi;           /* dBm of the last packet heard */
    float    lora_last_snr;
    /* from the leader (via the base station) */
    char     leader_msg[49];           /* last message text */
    uint32_t leader_msg_count;         /* bumps on each new message */
    uint8_t  leader_cmd;               /* last lp_cmd_t, 0 = none */
    uint8_t  leader_cmd_arg;
    uint32_t leader_cmd_count;
    uint8_t  stage;                    /* journey step (set by the journey engine) */
    bool     journey_active;           /* a journey is loaded and running */
    bool     dimmed;                   /* leader "dim" command */

    /* Lid switch */
    bool     lid_closed;
    int      lid_changes;              /* counts open/close events */

    /* LEDs */
    test_result_t leds;                /* PASS once initialised; verify by eye */

    /* Battery */
    test_result_t batt;
    float    batt_v;

    /* SD card */
    test_result_t sd;
    uint64_t sd_total_mb, sd_free_mb;
    bool     manifest_found;

    /* Touch (set by UI on first touch) */
    test_result_t touch;

    /* Speaker */
    test_result_t audio;

    /* Firmware */
    char     fw_version[32];
    char     ota_partition[16];
} liahona_state_t;

extern liahona_state_t g_state;
extern SemaphoreHandle_t g_state_lock;

#define STATE_LOCK()   xSemaphoreTake(g_state_lock, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(g_state_lock)

/* ---- module entry points ---- */
void ui_start(void);
void ui_compass_tick(const liahona_state_t *s);   /* ui_compass.c */

void sensors_start(i2c_master_bus_handle_t bus);   /* I2C scan + compass task */
void gps_start(i2c_master_bus_handle_t bus);
void nfc_start(i2c_master_bus_handle_t bus);
void lora_start(void);
void lora_send_status_now(void);                  /* queue a status report (if TX enabled) */
void lora_send_checkin(const uint8_t *uid, int uid_len, uint8_t station);
void leds_start(void);
void leds_set_mode(int mode);                     /* 0 idle glow, 1 test cycle, 2 off, 3 pulse (then glow) */
void inputs_start(void);                          /* lid switch + battery */
void storage_start(void);
void audio_test_init(void);
void audio_test_beep(void);                       /* plays a short tone (non-blocking) */
void audio_play(const char *file, int start_ms, int dur_ms);   /* media file name or full path */
void audio_stop(void);
bool audio_busy(void);
void audio_tap_sound(void);                       /* short tick, skipped while audio plays */

void journey_start(void);                         /* journey_port.c: loads from SD, runs the engine */
void ui_journey_build(void);                      /* ui_journey.c */
void ui_journey_tick(void);
void ui_journey_show(void);
bool ui_journey_is_active_screen(void);
void ui_show_test(void);                          /* ui.c */
void ui_show_compass(void);
