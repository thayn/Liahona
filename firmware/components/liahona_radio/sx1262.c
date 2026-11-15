/*
 * SX1262 driver. Command codes and sequences follow the Semtech SX1261/2 datasheet
 * (rev 2.1), including its section 15 errata fixes. See sx1262.h for the overview.
 */
#include <string.h>
#include <math.h>
#include "sx1262.h"

/* opcodes */
#define OP_SET_STANDBY          0x80
#define OP_SET_PACKET_TYPE      0x8A
#define OP_SET_RF_FREQUENCY     0x86
#define OP_SET_PA_CONFIG        0x95
#define OP_SET_TX_PARAMS        0x8E
#define OP_SET_BUFFER_BASE      0x8F
#define OP_SET_MODULATION       0x8B
#define OP_SET_PACKET_PARAMS    0x8C
#define OP_SET_DIO_IRQ          0x08
#define OP_SET_DIO2_RF_SWITCH   0x9D
#define OP_SET_DIO3_TCXO        0x97
#define OP_SET_REGULATOR        0x96
#define OP_CALIBRATE            0x89
#define OP_CALIBRATE_IMAGE      0x98
#define OP_SET_TX               0x83
#define OP_SET_RX               0x82
#define OP_WRITE_BUFFER         0x0E
#define OP_READ_BUFFER          0x1E
#define OP_WRITE_REGISTER       0x0D
#define OP_READ_REGISTER        0x1D
#define OP_GET_IRQ_STATUS       0x12
#define OP_CLEAR_IRQ            0x02
#define OP_GET_RX_BUFFER_STATUS 0x13
#define OP_GET_PACKET_STATUS    0x14
#define OP_GET_DEVICE_ERRORS    0x17
#define OP_CLEAR_DEVICE_ERRORS  0x07

/* registers */
#define REG_SYNC_WORD           0x0740
#define REG_IQ_POLARITY         0x0736
#define REG_TX_CLAMP            0x08D8
#define REG_OCP                 0x08E7

/* IRQ bits */
#define IRQ_TX_DONE     0x0001
#define IRQ_RX_DONE     0x0002
#define IRQ_HEADER_ERR  0x0020
#define IRQ_CRC_ERR     0x0040
#define IRQ_TIMEOUT     0x0200
#define IRQ_ALL         (IRQ_TX_DONE | IRQ_RX_DONE | IRQ_HEADER_ERR | IRQ_CRC_ERR | IRQ_TIMEOUT)

#define ERR_XOSC_START  0x0020

/* ---------------------------------------------------------------- low level */
static int cmd(sx1262_t *r, uint8_t op, const uint8_t *data, size_t n)
{
    uint8_t tx[1 + 260], rx[1 + 260];
    if (n > 260) return SX_ERR_ARG;
    tx[0] = op;
    if (n) memcpy(tx + 1, data, n);
    return sx_hal_xfer(r, tx, rx, 1 + n);
}

/* Read commands: opcode, then a status byte, then data. */
static int cmd_read(sx1262_t *r, uint8_t op, uint8_t *out, size_t n)
{
    uint8_t tx[2 + 8] = { op }, rx[2 + 8];
    if (n > 8) return SX_ERR_ARG;
    int e = sx_hal_xfer(r, tx, rx, 2 + n);
    if (e == SX_OK) memcpy(out, rx + 2, n);
    return e;
}

static int write_reg(sx1262_t *r, uint16_t addr, const uint8_t *data, size_t n)
{
    uint8_t buf[2 + 8] = { addr >> 8, addr & 0xFF };
    if (n > 8) return SX_ERR_ARG;
    memcpy(buf + 2, data, n);
    return cmd(r, OP_WRITE_REGISTER, buf, 2 + n);
}

static int read_reg(sx1262_t *r, uint16_t addr, uint8_t *out, size_t n)
{
    uint8_t tx[4 + 8] = { OP_READ_REGISTER, addr >> 8, addr & 0xFF, 0 }, rx[4 + 8];
    if (n > 8) return SX_ERR_ARG;
    int e = sx_hal_xfer(r, tx, rx, 4 + n);
    if (e == SX_OK) memcpy(out, rx + 4, n);
    return e;
}

static int standby(sx1262_t *r)
{
    uint8_t d = 0x00;   /* STDBY_RC */
    sx_hal_rxen(r, 0);
    return cmd(r, OP_SET_STANDBY, &d, 1);
}

static uint16_t irq_status(sx1262_t *r)
{
    uint8_t d[2] = { 0 };
    cmd_read(r, OP_GET_IRQ_STATUS, d, 2);
    return (uint16_t)((d[0] << 8) | d[1]);
}

static void clear_irq(sx1262_t *r)
{
    uint8_t d[2] = { 0xFF, 0xFF };
    cmd(r, OP_CLEAR_IRQ, d, 2);
}

static int hw_reset(sx1262_t *r)
{
    sx_hal_reset_pin(r, 0);
    sx_hal_delay_ms(2);
    sx_hal_reset_pin(r, 1);
    sx_hal_delay_ms(10);
    for (int i = 0; i < 100 && sx_hal_busy(r); i++) sx_hal_delay_ms(1);
    return sx_hal_busy(r) ? SX_ERR_BUSY : SX_OK;
}

static uint8_t bw_code(uint16_t khz)
{
    switch (khz) { case 125: return 0x04; case 250: return 0x05; default: return 0x06; }
}

static int set_packet_params(sx1262_t *r, uint8_t payload_len)
{
    uint8_t d[6] = {
        (uint8_t)(r->cfg.preamble >> 8), (uint8_t)(r->cfg.preamble & 0xFF),
        0x00,               /* explicit header */
        payload_len,
        0x01,               /* CRC on */
        0x00,               /* standard IQ */
    };
    return cmd(r, OP_SET_PACKET_PARAMS, d, 6);
}

/* ---------------------------------------------------------------- public */
int sx1262_probe(sx1262_t *r, const sx1262_cfg_t *cfg, uint8_t sync_out[2])
{
    memset(r, 0, sizeof(*r));
    r->cfg = *cfg;
    if (sx_hal_init(r) != SX_OK) return SX_ERR_HAL;
    int e = hw_reset(r);
    if (e != SX_OK) return e;
    uint8_t s[2] = { 0 };
    e = read_reg(r, REG_SYNC_WORD, s, 2);
    if (sync_out) { sync_out[0] = s[0]; sync_out[1] = s[1]; }
    if (e != SX_OK) return e;
    return (s[0] == 0x14 && s[1] == 0x24) ? SX_OK : SX_ERR_NOT_FOUND;
}

static int setup_oscillator(sx1262_t *r, bool use_tcxo)
{
    uint8_t d[4] = { 0 };
    int e = standby(r);
    if (e) return e;
    cmd(r, OP_CLEAR_DEVICE_ERRORS, d, 2);   /* drop any stale error from power-up */
    if (use_tcxo) {
        /* voltage code: 1.6V=0x00, 1.7=0x01, 1.8=0x02, 2.2=0x03, 2.4=0x04, 2.7=0x05, 3.0=0x06, 3.3=0x07 */
        static const float volts[8] = { 1.6f, 1.7f, 1.8f, 2.2f, 2.4f, 2.7f, 3.0f, 3.3f };
        uint8_t code = 2;
        for (int i = 0; i < 8; i++) if (fabsf(volts[i] - r->cfg.tcxo_v) < 0.05f) code = (uint8_t)i;
        uint32_t delay = 320;               /* 5 ms in 15.625 us steps */
        d[0] = code; d[1] = delay >> 16; d[2] = (delay >> 8) & 0xFF; d[3] = delay & 0xFF;
        if ((e = cmd(r, OP_SET_DIO3_TCXO, d, 4))) return e;
    }
    d[0] = 0x7F;                            /* calibrate everything */
    if ((e = cmd(r, OP_CALIBRATE, d, 1))) return e;
    sx_hal_delay_ms(10);
    uint8_t err[2] = { 0 };
    cmd_read(r, OP_GET_DEVICE_ERRORS, err, 2);
    r->last_errors = (uint16_t)((err[0] << 8) | err[1]);
    d[0] = 0; d[1] = 0;
    cmd(r, OP_CLEAR_DEVICE_ERRORS, d, 2);
    return (r->last_errors & ERR_XOSC_START) ? SX_ERR_NOT_FOUND : SX_OK;
}

int sx1262_begin(sx1262_t *r)
{
    int e;
    uint8_t d[8];
    const sx1262_cfg_t *c = &r->cfg;

    /* 1. oscillator: TCXO first if configured, else / on failure the crystal */
    r->tcxo_used = false;
    if (c->tcxo_v > 0.0f) {
        if (setup_oscillator(r, true) == SX_OK) r->tcxo_used = true;
        else if ((e = hw_reset(r))) return e;
    }
    if (!r->tcxo_used && (e = setup_oscillator(r, false))) return e;

    /* 2. LDO regulator (works on every module; DC-DC needs the inductor) */
    d[0] = 0x00;
    if ((e = cmd(r, OP_SET_REGULATOR, d, 1))) return e;

    /* 3. antenna switch on DIO2 */
    if (c->dio2_rf_switch) {
        d[0] = 0x01;
        if ((e = cmd(r, OP_SET_DIO2_RF_SWITCH, d, 1))) return e;
    }

    /* 4. LoRa, frequency, image calibration for the band */
    d[0] = 0x01;
    if ((e = cmd(r, OP_SET_PACKET_TYPE, d, 1))) return e;
    uint32_t frf = (uint32_t)(((uint64_t)c->freq_hz << 25) / 32000000ULL);
    d[0] = frf >> 24; d[1] = (frf >> 16) & 0xFF; d[2] = (frf >> 8) & 0xFF; d[3] = frf & 0xFF;
    if ((e = cmd(r, OP_SET_RF_FREQUENCY, d, 4))) return e;
    if (c->freq_hz >= 902000000UL)      { d[0] = 0xE1; d[1] = 0xE9; }   /* 902-928 */
    else if (c->freq_hz >= 863000000UL) { d[0] = 0xD7; d[1] = 0xDB; }   /* 863-870 */
    else                                { d[0] = 0x6B; d[1] = 0x6F; }   /* 430-440 */
    if ((e = cmd(r, OP_CALIBRATE_IMAGE, d, 2))) return e;

    /* 5. PA: SX1262 high-power PA, then power */
    d[0] = 0x04; d[1] = 0x07; d[2] = 0x00; d[3] = 0x01;
    if ((e = cmd(r, OP_SET_PA_CONFIG, d, 4))) return e;
    d[0] = 0x38;                                   /* 140 mA over-current limit */
    if ((e = write_reg(r, REG_OCP, d, 1))) return e;
    if ((e = sx1262_set_power(r, c->power_dbm))) return e;
    /* errata 15.2: better PA clamping */
    if ((e = read_reg(r, REG_TX_CLAMP, d, 1))) return e;
    d[0] |= 0x1E;
    if ((e = write_reg(r, REG_TX_CLAMP, d, 1))) return e;

    /* 6. buffers, modulation, packet format */
    d[0] = 0x00; d[1] = 0x00;
    if ((e = cmd(r, OP_SET_BUFFER_BASE, d, 2))) return e;
    float sym_ms = (float)(1u << c->sf) / (float)c->bw_khz;
    d[0] = c->sf;
    d[1] = bw_code(c->bw_khz);
    d[2] = (uint8_t)(c->cr_denom - 4);             /* 4/5 = 0x01 */
    d[3] = sym_ms >= 16.0f ? 0x01 : 0x00;          /* low data-rate optimise */
    if ((e = cmd(r, OP_SET_MODULATION, d, 4))) return e;
    if ((e = set_packet_params(r, 0xFF))) return e;
    /* errata 15.4: IQ polarity bit for standard IQ */
    if ((e = read_reg(r, REG_IQ_POLARITY, d, 1))) return e;
    d[0] |= 0x04;
    if ((e = write_reg(r, REG_IQ_POLARITY, d, 1))) return e;

    /* 7. sync word: 0x12 -> registers 0x14 0x24 */
    d[0] = (uint8_t)((c->sync_word & 0xF0) | 0x04);
    d[1] = (uint8_t)(((c->sync_word & 0x0F) << 4) | 0x04);
    if ((e = write_reg(r, REG_SYNC_WORD, d, 2))) return e;

    /* 8. all the IRQs we use on DIO1 */
    d[0] = IRQ_ALL >> 8; d[1] = IRQ_ALL & 0xFF;     /* irq mask */
    d[2] = IRQ_ALL >> 8; d[3] = IRQ_ALL & 0xFF;     /* DIO1 */
    d[4] = d[5] = d[6] = d[7] = 0;                  /* DIO2, DIO3 */
    if ((e = cmd(r, OP_SET_DIO_IRQ, d, 8))) return e;
    clear_irq(r);

    r->ready = true;
    return SX_OK;
}

int sx1262_set_power(sx1262_t *r, int8_t dbm)
{
    if (dbm > 22) dbm = 22;
    if (dbm < -9) dbm = -9;
    r->cfg.power_dbm = dbm;
    uint8_t d[2] = { (uint8_t)dbm, 0x04 };          /* 200 us ramp */
    return cmd(r, OP_SET_TX_PARAMS, d, 2);
}

int sx1262_transmit(sx1262_t *r, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!r->ready || len == 0 || len > 255) return SX_ERR_ARG;
    int e;
    if ((e = standby(r))) return e;
    if ((e = set_packet_params(r, (uint8_t)len))) return e;
    uint8_t buf[1 + 255];
    buf[0] = 0x00;                                  /* buffer offset */
    memcpy(buf + 1, data, len);
    if ((e = cmd(r, OP_WRITE_BUFFER, buf, 1 + len))) return e;
    clear_irq(r);
    sx_hal_rxen(r, 0);                              /* RX side off; DIO2 turns TX side on */
    uint8_t t[3] = { 0, 0, 0 };                     /* no chip timeout; we time it here */
    if ((e = cmd(r, OP_SET_TX, t, 3))) return e;

    uint32_t waited = 0;
    uint16_t irq = 0;
    while (waited < timeout_ms) {
        if (sx_hal_wait_dio1(r, 20)) {
            irq = irq_status(r);
            if (irq & IRQ_TX_DONE) break;
        }
        waited += 20;
    }
    r->last_irq = irq;
    clear_irq(r);
    standby(r);
    return (irq & IRQ_TX_DONE) ? SX_OK : SX_ERR_TX_TIMEOUT;
}

int sx1262_start_receive(sx1262_t *r)
{
    if (!r->ready) return SX_ERR_ARG;
    int e;
    if ((e = standby(r))) return e;
    if ((e = set_packet_params(r, 0xFF))) return e;
    clear_irq(r);
    sx_hal_rxen(r, 1);
    uint8_t t[3] = { 0xFF, 0xFF, 0xFF };            /* continuous */
    return cmd(r, OP_SET_RX, t, 3);
}

bool sx1262_wait_irq(sx1262_t *r, uint32_t ms)
{
    return sx_hal_wait_dio1(r, ms);
}

int sx1262_read_packet(sx1262_t *r, uint8_t *buf, size_t cap, int16_t *rssi_dbm, float *snr_db)
{
    uint16_t irq = irq_status(r);
    r->last_irq = irq;
    clear_irq(r);
    if (!(irq & IRQ_RX_DONE)) return 0;
    if (irq & (IRQ_CRC_ERR | IRQ_HEADER_ERR)) return SX_ERR_CRC;

    uint8_t st[2] = { 0 };
    cmd_read(r, OP_GET_RX_BUFFER_STATUS, st, 2);    /* length, start offset */
    size_t len = st[0];
    if (len > cap) len = cap;

    uint8_t tx[3 + 255] = { OP_READ_BUFFER, st[1], 0 }, rx[3 + 255];
    int e = sx_hal_xfer(r, tx, rx, 3 + len);
    if (e) return e;
    memcpy(buf, rx + 3, len);

    uint8_t ps[3] = { 0 };
    cmd_read(r, OP_GET_PACKET_STATUS, ps, 3);       /* RssiPkt, SnrPkt, SignalRssiPkt */
    if (rssi_dbm) *rssi_dbm = (int16_t)(-(int)ps[0] / 2);
    if (snr_db) *snr_db = (float)(int8_t)ps[1] / 4.0f;
    return (int)len;
}

uint32_t sx1262_airtime_ms(const sx1262_cfg_t *c, size_t len)
{
    /* Semtech AN1200.13, explicit header, CRC on */
    double tsym = (double)(1u << c->sf) / (double)c->bw_khz;   /* ms */
    int de = (tsym >= 16.0) ? 1 : 0;
    double num = 8.0 * len - 4.0 * c->sf + 28 + 16;
    double den = 4.0 * (c->sf - 2 * de);
    double nsym = 8 + fmax(ceil(num / den) * c->cr_denom, 0);
    double t = (c->preamble + 4.25 + nsym) * tsym;
    return (uint32_t)ceil(t);
}
