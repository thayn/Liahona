/*
 * Host tests (run on a PC, no hardware): packet format, base-station text protocol,
 * and the SX1262 command sequence against a fake chip.
 *   make -C firmware/components/liahona_radio/test_host
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "liahona_proto.h"
#include "sx1262.h"
#include "radio_config.h"
#include "bridge.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ------------------------------------------------------------ fake SX1262 */
static uint8_t  log_ops[512];
static int      n_ops;
static uint8_t  regs[0x1000];
static int      has_tcxo = 1;          /* does the fake module have a TCXO? */
static int      tcxo_enabled;
static uint16_t irq;
static uint8_t  rxbuf[256], rxlen;
static uint8_t  last_tx[256], last_tx_len;
static uint32_t frf_set;
static int      rxen_level = -1, dio2_switch;

int  sx_hal_init(sx1262_t *r) { (void)r; return SX_OK; }
void sx_hal_reset_pin(sx1262_t *r, int level)
{
    (void)r;
    if (level == 0) { memset(regs, 0, sizeof(regs)); regs[0x740] = 0x14; regs[0x741] = 0x24; tcxo_enabled = 0; }
}
bool sx_hal_busy(sx1262_t *r) { (void)r; return false; }
void sx_hal_rxen(sx1262_t *r, int level) { (void)r; rxen_level = level; }
bool sx_hal_wait_dio1(sx1262_t *r, uint32_t ms) { (void)r; (void)ms; return irq != 0; }
void sx_hal_delay_ms(uint32_t ms) { (void)ms; }

int sx_hal_xfer(sx1262_t *r, const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)r;
    memset(rx, 0, n);
    uint8_t op = tx[0];
    if (n_ops < (int)sizeof(log_ops)) log_ops[n_ops++] = op;
    uint16_t a = (uint16_t)((tx[1] << 8) | tx[2]);
    switch (op) {
    case 0x1D: for (size_t i = 4; i < n; i++) rx[i] = regs[a + i - 4]; break;            /* ReadRegister */
    case 0x0D: for (size_t i = 3; i < n; i++) regs[a + i - 3] = tx[i]; break;            /* WriteRegister */
    case 0x97: tcxo_enabled = 1; break;
    case 0x17: {                                                                          /* GetDeviceErrors */
        int xosc_bad = (has_tcxo && !tcxo_enabled) || (!has_tcxo && tcxo_enabled);
        rx[3] = xosc_bad ? 0x20 : 0x00;
        break;
    }
    case 0x86: frf_set = ((uint32_t)tx[1] << 24) | (tx[2] << 16) | (tx[3] << 8) | tx[4]; break;
    case 0x9D: dio2_switch = tx[1]; break;
    case 0x0E: last_tx_len = (uint8_t)(n - 2); memcpy(last_tx, tx + 2, n - 2); break;    /* WriteBuffer */
    case 0x83: irq = 0x0001; break;                                                       /* SetTx -> TxDone */
    case 0x12: rx[2] = irq >> 8; rx[3] = irq & 0xFF; break;
    case 0x02: irq = 0; break;
    case 0x13: rx[2] = rxlen; rx[3] = 0; break;
    case 0x1E: memcpy(rx + 3, rxbuf, n - 3); break;
    case 0x14: rx[2] = 160; rx[3] = (uint8_t)(int8_t)38; break;                          /* -80 dBm, 9.5 dB */
    }
    return SX_OK;
}

static int count_op(uint8_t op) { int c = 0; for (int i = 0; i < n_ops; i++) c += log_ops[i] == op; return c; }

static sx1262_cfg_t cfg(void)
{
    sx1262_cfg_t c = {
        .rxen = 20, .tcxo_v = 1.8f, .dio2_rf_switch = true,
        .freq_hz = RADIO_FREQ_HZ, .bw_khz = RADIO_BW_KHZ, .sf = RADIO_SF, .cr_denom = RADIO_CR_DENOM,
        .preamble = RADIO_PREAMBLE, .sync_word = RADIO_SYNC_WORD, .power_dbm = 14,
    };
    return c;
}

static void test_driver(void)
{
    sx1262_t r;
    sx1262_cfg_t c = cfg();
    uint8_t s[2];

    /* module with a TCXO */
    has_tcxo = 1; n_ops = 0;
    CHECK(sx1262_probe(&r, &c, s) == SX_OK);
    CHECK(s[0] == 0x14 && s[1] == 0x24);
    CHECK(sx1262_begin(&r) == SX_OK);
    CHECK(r.tcxo_used);
    CHECK(frf_set == 0x39300000);                     /* 915 MHz */
    CHECK(dio2_switch == 1);
    CHECK(regs[0x740] == 0x14 && regs[0x741] == 0x24);/* sync word 0x12 */
    CHECK((regs[0x736] & 0x04) && (regs[0x8D8] & 0x1E) == 0x1E);
    CHECK(count_op(0x8B) == 1 && count_op(0x08) == 1);

    /* crystal-only module: TCXO try fails, falls back */
    has_tcxo = 0; n_ops = 0;
    CHECK(sx1262_probe(&r, &c, s) == SX_OK);
    CHECK(sx1262_begin(&r) == SX_OK);
    CHECK(!r.tcxo_used);
    CHECK(count_op(0x89) == 2);                       /* calibrated twice */

    /* transmit: RX switch off during TX, packet bytes reach the buffer */
    uint8_t pkt[5] = { 1, 2, 3, 4, 5 };
    CHECK(sx1262_transmit(&r, pkt, 5, 500) == SX_OK);
    CHECK(rxen_level == 0);
    CHECK(last_tx_len == 5 && memcmp(last_tx, pkt, 5) == 0);

    /* receive */
    CHECK(sx1262_start_receive(&r) == SX_OK);
    CHECK(rxen_level == 1);
    memcpy(rxbuf, "hello", 5); rxlen = 5; irq = 0x0002;
    uint8_t got[32]; int16_t rssi; float snr;
    CHECK(sx1262_read_packet(&r, got, sizeof(got), &rssi, &snr) == 5);
    CHECK(memcmp(got, "hello", 5) == 0 && rssi == -80 && fabsf(snr - 9.5f) < 0.01f);
    irq = 0x0002 | 0x0040;
    CHECK(sx1262_read_packet(&r, got, sizeof(got), &rssi, &snr) == SX_ERR_CRC);

    /* airtime: 20-byte status at SF10/500 kHz */
    uint32_t t = sx1262_airtime_ms(&c, 20);
    printf("airtime 20 B: %u ms, 64 B: %u ms\n", t, sx1262_airtime_ms(&c, 64));
    CHECK(t > 60 && t < 120);
}

static void test_proto(void)
{
    uint8_t b[LP_MAX_LEN];
    lp_packet_t p = { .type = LP_STATUS, .from = 3, .to = 0, .seq = 9 }, q;
    p.u.status.lat_e7 = 466612345; p.u.status.lon_e7 = -1210012345;
    p.u.status.batt_mv = 3920; p.u.status.flags = LP_F_GPS_FIX | LP_F_LID_OPEN;
    p.u.status.stage = 4; p.u.status.heading_d10 = 1234;
    size_t n = lp_encode(&p, b, sizeof(b));
    CHECK(n == 20);
    CHECK(lp_decode(b, n, &q) && q.u.status.lon_e7 == -1210012345 && q.u.status.heading_d10 == 1234);
    CHECK(!lp_decode(b, 10, &q));                     /* truncated */

    lp_packet_t m = { .type = LP_MESSAGE, .from = 0, .to = 3, .seq = 1 };
    strcpy(m.u.message.text, "Go to the river"); m.u.message.len = 15;
    n = lp_encode(&m, b, sizeof(b));
    CHECK(lp_decode(b, n, &q) && strcmp(q.u.message.text, "Go to the river") == 0 && lp_wants_ack(&q));
    m.to = LP_ID_ALL; CHECK(!lp_wants_ack(&m));

    lp_packet_t ci = { .type = LP_CHECKIN, .from = 2, .to = 0, .seq = 5 };
    ci.u.checkin.uid_len = 7; memcpy(ci.u.checkin.uid, "\x04\xA1\xB2\xC3\xD4\xE5\xF6", 7); ci.u.checkin.station = 6;
    n = lp_encode(&ci, b, sizeof(b));
    CHECK(lp_decode(b, n, &q) && q.u.checkin.station == 6 && q.u.checkin.uid[6] == 0xF6);

    b[0] = 'X'; CHECK(!lp_decode(b, n, &q));
}

static void test_bridge(void)
{
    bridge_cmd_t c; char err[48];
    CHECK(bridge_parse_line("ping 3", &c, err, sizeof err) == 0 && c.kind == BC_PING && c.to == 3);
    CHECK(bridge_parse_line("ping all", &c, err, sizeof err) == 0 && c.to == 255);
    CHECK(bridge_parse_line("msg 4 Head to the river", &c, err, sizeof err) == 0 && c.kind == BC_MSG
          && strcmp(c.text, "Head to the river") == 0);
    CHECK(bridge_parse_line("stage 2 7", &c, err, sizeof err) == 0 && c.cmd == LP_CMD_STAGE && c.arg == 7);
    CHECK(bridge_parse_line("UNLOCK 5\r\n", &c, err, sizeof err) == 0 && c.cmd == LP_CMD_UNLOCK && c.to == 5);
    CHECK(bridge_parse_line("power 20", &c, err, sizeof err) == 0 && c.kind == BC_POWER && c.dbm == 20);
    CHECK(bridge_parse_line("{\"cmd\":\"msg\",\"to\":3,\"text\":\"Say \\\"hi\\\"\"}", &c, err, sizeof err) == 0
          && strcmp(c.text, "Say \"hi\"") == 0 && c.to == 3);
    CHECK(bridge_parse_line("{\"cmd\":\"dim\",\"to\":\"all\"}", &c, err, sizeof err) == 0 && c.cmd == LP_CMD_DIM && c.to == 255);
    CHECK(bridge_parse_line("{\"cmd\":\"stage\",\"to\":1,\"stage\":3}", &c, err, sizeof err) == 0 && c.arg == 3);
    CHECK(bridge_parse_line("{\"cmd\":\"info\"}", &c, err, sizeof err) == 0 && c.kind == BC_INFO);
    CHECK(bridge_parse_line("ping 0", &c, err, sizeof err) == -1);
    CHECK(bridge_parse_line("msg 3", &c, err, sizeof err) == -1);
    CHECK(bridge_parse_line("dance 3", &c, err, sizeof err) == -1);
    CHECK(bridge_parse_line("power 40", &c, err, sizeof err) == -1);
    CHECK(bridge_parse_line("msg 3 this text is far too long for one packet on the radio ok", &c, err, sizeof err) == -1);
    CHECK(bridge_parse_line("{\"cmd\":\"stage\",\"to\":1}", &c, err, sizeof err) == -1);

    lp_packet_t p;
    bridge_parse_line("msg 3 Hi", &c, err, sizeof err);
    CHECK(bridge_build_packet(&c, 7, &p) == 0 && p.type == LP_MESSAGE && p.seq == 7 && p.u.message.len == 2);

    lp_packet_t st = { .type = LP_STATUS, .from = 3, .to = 0, .seq = 9 };
    st.u.status.lat_e7 = 466612345; st.u.status.lon_e7 = -1210012345; st.u.status.batt_mv = 3920;
    st.u.status.flags = LP_F_GPS_FIX | LP_F_LID_OPEN; st.u.status.heading_d10 = 1234;
    char line[300];
    bridge_format_rx(&st, -80, 9.5f, line, sizeof line);
    printf("%s", line);
    CHECK(strstr(line, "\"lat\":46.6612345") && strstr(line, "\"lid\":\"open\"") && strstr(line, "\"rssi\":-80"));
    /* update progress: encode, decode, format */
    {
        lp_packet_t u = { .type = LP_UPDATE, .from = 3, .to = LP_ID_BASE, .seq = 9 }, d;
        u.u.update.state = LP_UPD_FAILED; u.u.update.detail = LP_UPE_WIFI; u.u.update.done = 1; u.u.update.total = 4;
        strcpy(u.u.update.version, "0.5.0");
        uint8_t b[LP_MAX_LEN]; size_t bn = lp_encode(&u, b, sizeof b);
        CHECK(bn == LP_HDR_LEN + 5 + 5 && lp_decode(b, bn, &d) && d.u.update.state == LP_UPD_FAILED && !strcmp(d.u.update.version, "0.5.0"));
        CHECK(!lp_wants_ack(&d));
        bridge_format_rx(&d, -90, 3.0f, line, sizeof line);
        printf("%s", line);
        CHECK(strstr(line, "\"ev\":\"update\"") && strstr(line, "\"state\":\"failed\"") && strstr(line, "join Wi-Fi"));
        CHECK(bridge_parse_line("update all", &c, err, sizeof err) == 0 && c.cmd == LP_CMD_UPDATE && c.to == 255);
        CHECK(bridge_parse_line("{\"cmd\":\"update\",\"to\":4}", &c, err, sizeof err) == 0 && c.cmd == LP_CMD_UPDATE && c.to == 4);
    }
    char esc[64]; bridge_json_escape("a\"b\\c\n", esc, sizeof esc);
    CHECK(strcmp(esc, "a\\\"b\\\\c\\n") == 0);
}

int main(void)
{
    test_proto();
    test_bridge();
    test_driver();
    printf(fails ? "%d FAILED\n" : "all host tests passed\n", fails);
    return fails ? 1 : 0;
}
