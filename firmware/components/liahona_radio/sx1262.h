/*
 * Small SX1262 LoRa driver (no libraries). Used by the ball (ESP32-P4) and the
 * base station (XIAO ESP32-S3 + Wio-SX1262). Only what Liahona needs:
 * one fixed channel, explicit-header LoRa packets with CRC, continuous receive.
 *
 * Antenna switch: DIO2 drives the TX side (SetDIO2AsRfSwitchCtrl) and, if `rxen`
 * is set, a GPIO drives the RX side (high while receiving, low while sending).
 * On the Waveshare Core1262 that means a short wire from DIO2 to TXEN, and RXEN
 * to a spare GPIO. The Wio-SX1262 already has DIO2 wired; its RXEN is GPIO38.
 *
 * TCXO: tried first at `tcxo_v` volts on DIO3; if the chip reports the oscillator
 * didn't start, the driver resets and runs on the plain crystal instead.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      spi_host;                       /* e.g. SPI2_HOST */
    int      sck, mosi, miso, nss;
    int      reset, busy, dio1;
    int      rxen;                           /* -1 = not used */
    float    tcxo_v;                         /* 0 = no TCXO; 1.8 typical */
    bool     dio2_rf_switch;
    uint32_t freq_hz;
    uint16_t bw_khz;                         /* 125 / 250 / 500 */
    uint8_t  sf;                             /* 7..12 */
    uint8_t  cr_denom;                       /* 5..8 -> 4/5..4/8 */
    uint16_t preamble;
    uint8_t  sync_word;                      /* 0x12 private, 0x34 LoRaWAN */
    int8_t   power_dbm;                      /* -9..22 */
} sx1262_cfg_t;

typedef struct {
    sx1262_cfg_t cfg;
    bool     tcxo_used;
    bool     ready;
    uint16_t last_irq;
    uint16_t last_errors;
    void    *hal;                            /* owned by the HAL */
} sx1262_t;

enum {
    SX_OK = 0,
    SX_ERR_HAL = -1,         /* SPI/GPIO setup failed */
    SX_ERR_NOT_FOUND = -2,   /* chip didn't answer (wiring) */
    SX_ERR_BUSY = -3,        /* BUSY stuck high */
    SX_ERR_TX_TIMEOUT = -4,
    SX_ERR_CRC = -5,
    SX_ERR_ARG = -6,
};

/* Reset the chip and read the sync-word register (0x14 0x24 after reset). */
int  sx1262_probe(sx1262_t *r, const sx1262_cfg_t *cfg, uint8_t sync_out[2]);
/* Full setup for LoRa on cfg's channel. Call after probe. Leaves the radio in standby. */
int  sx1262_begin(sx1262_t *r);
/* Send one packet and wait for it to finish (blocks for the airtime). Radio ends in standby. */
int  sx1262_transmit(sx1262_t *r, const uint8_t *data, size_t len, uint32_t timeout_ms);
/* Listen continuously. Packets raise DIO1. */
int  sx1262_start_receive(sx1262_t *r);
/* Wait up to ms for DIO1 (a packet or TX done). */
bool sx1262_wait_irq(sx1262_t *r, uint32_t ms);
/* After DIO1 in receive mode: copy the packet out. Returns length, 0 if nothing, <0 on error. */
int  sx1262_read_packet(sx1262_t *r, uint8_t *buf, size_t cap, int16_t *rssi_dbm, float *snr_db);
/* Change TX power at runtime. */
int  sx1262_set_power(sx1262_t *r, int8_t dbm);
/* Time on air for a packet of len bytes, in milliseconds (rounded up). */
uint32_t sx1262_airtime_ms(const sx1262_cfg_t *cfg, size_t len);

/* ---- HAL: implemented once per platform (sx1262_hal_esp.c; a mock for host tests) ---- */
int  sx_hal_init(sx1262_t *r);
int  sx_hal_xfer(sx1262_t *r, const uint8_t *tx, uint8_t *rx, size_t n);   /* waits for BUSY low */
void sx_hal_reset_pin(sx1262_t *r, int level);
bool sx_hal_busy(sx1262_t *r);
void sx_hal_rxen(sx1262_t *r, int level);
bool sx_hal_wait_dio1(sx1262_t *r, uint32_t ms);
void sx_hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
