/*
 * LoRa link to the base station (milestone 2).
 *
 * - Finds the SX1262 (self-test PASS), sets it up on the shared channel
 *   (radio_config.h) and listens all the time. Receiving is always safe.
 * - Sends only if LORA_TX_ENABLED is 1 (board_pins.h): a status report every
 *   LORA_STATUS_EVERY_S seconds, a reply to pings, ACKs, and NFC check-ins.
 * - Leader messages and commands land in g_state for the screen / journey code.
 *
 * One task owns the radio; other code asks it to send through a small queue.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "board_pins.h"
#include "liahona.h"
#include "sx1262.h"
#include "liahona_proto.h"
#include "radio_config.h"

static const char *TAG = "lora";
static sx1262_t s_radio;
static QueueHandle_t s_txq;            /* lp_packet_t to send */
static uint8_t s_seq;
static uint8_t s_id = BALL_ID_DEFAULT;

/* last command seq from the base, so a retried command isn't applied twice */
static int s_last_base_seq = -1;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static uint8_t read_ball_id(void)
{
    FILE *f = fopen(BALL_ID_FILE, "r");
    if (!f) return BALL_ID_DEFAULT;
    int v = 0;
    int n = fscanf(f, "%d", &v);
    fclose(f);
    return (n == 1 && v >= 1 && v <= 254) ? (uint8_t)v : BALL_ID_DEFAULT;
}

static void fill_status(lp_packet_t *p)
{
    memset(p, 0, sizeof(*p));
    p->type = LP_STATUS;
    p->from = s_id;
    p->to = LP_ID_BASE;
    STATE_LOCK();
    if (g_state.gps_fix) {
        p->u.status.lat_e7 = (int32_t)lround(g_state.gps_lat * 1e7);
        p->u.status.lon_e7 = (int32_t)lround(g_state.gps_lon * 1e7);
        p->u.status.flags |= LP_F_GPS_FIX;
    }
    float v = (g_state.batt == TEST_PASS) ? g_state.batt_v : 0.0f;
    p->u.status.batt_mv = (uint16_t)(v * 1000.0f);
    if (v > 0.0f && v < 3.5f) p->u.status.flags |= LP_F_LOW_BATT;
    if (!g_state.lid_closed) p->u.status.flags |= LP_F_LID_OPEN;
    p->u.status.stage = g_state.stage;
    float h = fmodf(g_state.heading_deg + 360.0f, 360.0f);
    p->u.status.heading_d10 = (uint16_t)(h * 10.0f) % 3600;
    STATE_UNLOCK();
}

static void queue_packet(const lp_packet_t *p)
{
    if (s_txq) xQueueSend(s_txq, p, 0);
}

void lora_send_status_now(void)
{
    lp_packet_t p;
    fill_status(&p);
    queue_packet(&p);
}

void lora_send_checkin(const uint8_t *uid, int uid_len, uint8_t station)
{
    lp_packet_t p = { .type = LP_CHECKIN, .from = s_id, .to = LP_ID_BASE };
    if (uid_len > (int)sizeof(p.u.checkin.uid)) uid_len = sizeof(p.u.checkin.uid);
    p.u.checkin.uid_len = (uint8_t)uid_len;
    memcpy(p.u.checkin.uid, uid, uid_len);
    p.u.checkin.station = station;
    queue_packet(&p);
}

void lora_send_update(uint8_t state, uint8_t detail, uint8_t done, uint8_t total, const char *version)
{
    lp_packet_t p = { .type = LP_UPDATE, .from = s_id, .to = LP_ID_BASE };
    p.u.update.state = state;
    p.u.update.detail = detail;
    p.u.update.done = done;
    p.u.update.total = total;
    strlcpy(p.u.update.version, version ? version : "", sizeof(p.u.update.version));
    queue_packet(&p);
}

/* Send now (radio task only). Returns to listening afterwards. */
static void send_packet(lp_packet_t *p)
{
    if (!LORA_TX_ENABLED) return;
    if (p->type != LP_ACK) p->seq = ++s_seq;
    uint8_t buf[LP_MAX_LEN];
    size_t n = lp_encode(p, buf, sizeof(buf));
    if (!n) return;
    int e = sx1262_transmit(&s_radio, buf, n, sx1262_airtime_ms(&s_radio.cfg, n) + 200);
    STATE_LOCK();
    if (e == SX_OK) g_state.lora_tx_count++;
    else g_state.lora_err = e;
    STATE_UNLOCK();
    if (e) ESP_LOGW(TAG, "send %s failed (%d)", lp_type_name(p->type), e);
    sx1262_start_receive(&s_radio);
}

static void handle_packet(const lp_packet_t *p, int16_t rssi, float snr)
{
    if (p->to != s_id && p->to != LP_ID_ALL) return;       /* for another ball */
    if (p->from != LP_ID_BASE) return;                      /* balls only listen to the base */

    STATE_LOCK();
    g_state.lora_rx_count++;
    g_state.lora_last_rssi = rssi;
    g_state.lora_last_snr = snr;
    STATE_UNLOCK();

    bool repeat = (p->to == s_id && p->seq == s_last_base_seq);
    if (p->to == s_id) s_last_base_seq = p->seq;

    switch (p->type) {
    case LP_PING: {
        /* Everyone answers a broadcast ping; spread replies so they don't collide. */
        if (p->to == LP_ID_ALL) vTaskDelay(pdMS_TO_TICKS(150 * (s_id % 20) + (esp_random() % 100)));
        lp_packet_t st;
        fill_status(&st);
        send_packet(&st);
        break;
    }
    case LP_MESSAGE:
        if (!repeat) {
            STATE_LOCK();
            strlcpy(g_state.leader_msg, p->u.message.text, sizeof(g_state.leader_msg));
            g_state.leader_msg_count++;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "message: %s", p->u.message.text);
        }
        break;
    case LP_COMMAND:
        if (!repeat) {
            STATE_LOCK();
            g_state.leader_cmd = p->u.command.cmd;
            g_state.leader_cmd_arg = p->u.command.arg;
            g_state.leader_cmd_count++;
            if (p->u.command.cmd == LP_CMD_STAGE) g_state.stage = p->u.command.arg;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "command: %s %u", lp_cmd_name(p->u.command.cmd), p->u.command.arg);
        }
        break;
    default:
        break;
    }

    if (lp_wants_ack(p)) {
        lp_packet_t ack = { .type = LP_ACK, .from = s_id, .to = LP_ID_BASE, .seq = p->seq };
        ack.u.ack.acked_seq = p->seq;
        ack.u.ack.ok = (p->type != LP_COMMAND ||
                        (p->u.command.cmd >= LP_CMD_UNLOCK && p->u.command.cmd <= LP_CMD_UPDATE)) ? 1 : 0;
        send_packet(&ack);
    }
}

static void lora_task(void *arg)
{
    const sx1262_cfg_t cfg = {
        .spi_host = SPI2_HOST,
        .sck = LORA_PIN_SCK, .mosi = LORA_PIN_MOSI, .miso = LORA_PIN_MISO, .nss = LORA_PIN_NSS,
        .reset = LORA_PIN_RESET, .busy = LORA_PIN_BUSY, .dio1 = LORA_PIN_DIO1,
        .rxen = LORA_PIN_RXEN, .tcxo_v = LORA_TCXO_V, .dio2_rf_switch = true,
        .freq_hz = RADIO_FREQ_HZ, .bw_khz = RADIO_BW_KHZ, .sf = RADIO_SF,
        .cr_denom = RADIO_CR_DENOM, .preamble = RADIO_PREAMBLE, .sync_word = RADIO_SYNC_WORD,
        .power_dbm = LORA_TX_POWER_DBM,
    };

    /* 1. Is it there? (self-test row) */
    uint8_t sync[2] = { 0 };
    int e = sx1262_probe(&s_radio, &cfg, sync);
    STATE_LOCK();
    g_state.lora_sync[0] = sync[0];
    g_state.lora_sync[1] = sync[1];
    g_state.lora = (e == SX_OK) ? TEST_PASS : TEST_FAIL;
    g_state.lora_err = e;
    g_state.lora_tx_enabled = LORA_TX_ENABLED;
    STATE_UNLOCK();
    if (e != SX_OK) {
        ESP_LOGW(TAG, "SX1262 not found (%d, sync %02X %02X) - check wiring", e, sync[0], sync[1]);
        vTaskDelete(NULL);
        return;
    }

    /* 2. Full setup, then listen */
    e = sx1262_begin(&s_radio);
    if (e == SX_OK) e = sx1262_start_receive(&s_radio);
    STATE_LOCK();
    g_state.lora_ready = (e == SX_OK);
    g_state.lora_tcxo = s_radio.tcxo_used;
    g_state.lora_err = e;
    STATE_UNLOCK();
    if (e != SX_OK) {
        ESP_LOGE(TAG, "SX1262 setup failed (%d, errors 0x%04X)", e, s_radio.last_errors);
        vTaskDelete(NULL);
        return;
    }

    /* 3. Who am I? (SD card is mounted by now, or never will be) */
    vTaskDelay(pdMS_TO_TICKS(1500));
    s_id = read_ball_id();
    STATE_LOCK();
    g_state.ball_id = s_id;
    STATE_UNLOCK();
    ESP_LOGI(TAG, "ball %u listening on %lu Hz SF%u/%ukHz, %s, TX %s",
             s_id, (unsigned long)RADIO_FREQ_HZ, RADIO_SF, RADIO_BW_KHZ,
             s_radio.tcxo_used ? "TCXO" : "crystal", LORA_TX_ENABLED ? "ON" : "off");

    int64_t next_status = now_ms() + 5000 + (esp_random() % 5000);
    uint8_t buf[256];

    for (;;) {
        if (sx1262_wait_irq(&s_radio, 100)) {
            int16_t rssi = 0;
            float snr = 0;
            int n = sx1262_read_packet(&s_radio, buf, sizeof(buf), &rssi, &snr);
            lp_packet_t p;
            if (n > 0 && lp_decode(buf, n, &p)) handle_packet(&p, rssi, snr);
        }

        lp_packet_t out;
        while (xQueueReceive(s_txq, &out, 0) == pdTRUE) send_packet(&out);

        if (LORA_TX_ENABLED && now_ms() >= next_status) {
            lp_packet_t st;
            fill_status(&st);
            send_packet(&st);
            next_status = now_ms() + LORA_STATUS_EVERY_S * 1000 + (esp_random() % 4000);
        }
    }
}

void lora_start(void)
{
    s_txq = xQueueCreate(6, sizeof(lp_packet_t));
    xTaskCreate(lora_task, "lora", 6144, NULL, 4, NULL);
}
