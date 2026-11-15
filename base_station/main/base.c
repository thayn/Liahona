/*
 * Liahona base station firmware.
 * Seeed XIAO ESP32-S3 + Wio-SX1262 (the Meshtastic kit), flashed with our own code.
 *
 * Listens for the balls and prints what it hears as JSON lines on USB.
 * Takes commands on USB (see bridge.h), sends them, and retries until the ball ACKs.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sx1262.h"
#include "liahona_proto.h"
#include "radio_config.h"
#include "bridge.h"

/* Wio-SX1262 for XIAO pin map (same as Meshtastic's seeed_xiao_s3 variant) */
#define PIN_SCK     7
#define PIN_MISO    8
#define PIN_MOSI    9
#define PIN_NSS     41
#define PIN_RESET   42
#define PIN_BUSY    40
#define PIN_DIO1    39
#define PIN_RXEN    38
#define TCXO_V      1.8f

#define BASE_POWER_DBM  14
#define RETRIES         3          /* resends after the first try */
#define RETRY_MS        1500
#define MAX_PENDING     8

static const char *TAG = "base";
static sx1262_t s_radio;
static QueueHandle_t s_cmdq;       /* bridge_cmd_t from USB */
static uint8_t s_seq;

typedef struct {
    bool        used;
    lp_packet_t pkt;
    int         tries_left;
    int64_t     next_ms;
} pending_t;
static pending_t s_pending[MAX_PENDING];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ---------- USB lines ---------- */
static void usb_write(const char *s, size_t n)
{
    usb_serial_jtag_write_bytes(s, n, pdMS_TO_TICKS(50));
}

static void usb_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) usb_write(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

static void usb_reader_task(void *arg)
{
    char line[200];
    size_t n = 0;
    uint8_t ch;
    for (;;) {
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (n + 1 < sizeof(line)) line[n++] = (char)ch;
            continue;
        }
        line[n] = '\0';
        n = 0;
        if (!line[0]) continue;
        bridge_cmd_t c;
        char err[48];
        if (bridge_parse_line(line, &c, err, sizeof(err)) == 0) {
            xQueueSend(s_cmdq, &c, pdMS_TO_TICKS(100));
        } else {
            char esc[96];
            bridge_json_escape(err, esc, sizeof(esc));
            usb_printf("{\"ev\":\"error\",\"msg\":\"%s\"}\n", esc);
        }
    }
}

/* ---------- radio ---------- */
static void hello(void)
{
    usb_printf("{\"ev\":\"hello\",\"fw\":\"liahona-base %s\",\"freq\":%lu,\"bw\":%u,\"sf\":%u,"
               "\"power\":%d,\"tcxo\":%s}\n", "0.1.0", (unsigned long)RADIO_FREQ_HZ, RADIO_BW_KHZ,
               RADIO_SF, s_radio.cfg.power_dbm, s_radio.tcxo_used ? "true" : "false");
}

static bool send_now(const lp_packet_t *p)
{
    uint8_t buf[LP_MAX_LEN];
    size_t n = lp_encode(p, buf, sizeof(buf));
    if (!n) return false;
    int e = sx1262_transmit(&s_radio, buf, n, sx1262_airtime_ms(&s_radio.cfg, n) + 200);
    sx1262_start_receive(&s_radio);
    if (e) usb_printf("{\"ev\":\"error\",\"msg\":\"radio send failed (%d)\"}\n", e);
    return e == 0;
}

static void start_command(const bridge_cmd_t *c)
{
    if (c->kind == BC_INFO) { hello(); return; }
    if (c->kind == BC_POWER) {
        sx1262_set_power(&s_radio, (int8_t)c->dbm);
        usb_printf("{\"ev\":\"info\",\"power\":%d}\n", s_radio.cfg.power_dbm);
        return;
    }
    lp_packet_t p;
    if (bridge_build_packet(c, ++s_seq, &p)) return;
    bool ok = send_now(&p);
    usb_printf("{\"ev\":\"sent\",\"type\":\"%s\",\"to\":%u,\"seq\":%u,\"ok\":%s}\n",
               lp_type_name(p.type), p.to, p.seq, ok ? "true" : "false");
    if (!lp_wants_ack(&p)) return;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!s_pending[i].used) {
            s_pending[i] = (pending_t){ .used = true, .pkt = p, .tries_left = RETRIES,
                                        .next_ms = now_ms() + RETRY_MS };
            return;
        }
    }
    usb_printf("{\"ev\":\"error\",\"msg\":\"too many unanswered sends\"}\n");
}

static void handle_rx(const lp_packet_t *p, int16_t rssi, float snr)
{
    if (p->to != LP_ID_BASE && p->to != LP_ID_ALL) return;
    if (p->from == LP_ID_BASE) return;

    char line[300];
    size_t n = bridge_format_rx(p, rssi, snr, line, sizeof(line));
    if (n) usb_write(line, n);

    if (p->type == LP_ACK) {
        for (int i = 0; i < MAX_PENDING; i++)
            if (s_pending[i].used && s_pending[i].pkt.to == p->from &&
                s_pending[i].pkt.seq == p->u.ack.acked_seq)
                s_pending[i].used = false;
    }
    if (lp_wants_ack(p)) {                      /* check-ins */
        lp_packet_t ack = { .type = LP_ACK, .from = LP_ID_BASE, .to = p->from, .seq = p->seq };
        ack.u.ack.acked_seq = p->seq;
        ack.u.ack.ok = 1;
        send_now(&ack);
    }
}

static void retry_pending(void)
{
    int64_t t = now_ms();
    for (int i = 0; i < MAX_PENDING; i++) {
        pending_t *q = &s_pending[i];
        if (!q->used || t < q->next_ms) continue;
        if (q->tries_left-- <= 0) {
            usb_printf("{\"ev\":\"timeout\",\"type\":\"%s\",\"to\":%u,\"seq\":%u}\n",
                       lp_type_name(q->pkt.type), q->pkt.to, q->pkt.seq);
            q->used = false;
            continue;
        }
        send_now(&q->pkt);
        q->next_ms = t + RETRY_MS;
    }
}

static void radio_task(void *arg)
{
    const sx1262_cfg_t cfg = {
        .spi_host = SPI2_HOST,
        .sck = PIN_SCK, .mosi = PIN_MOSI, .miso = PIN_MISO, .nss = PIN_NSS,
        .reset = PIN_RESET, .busy = PIN_BUSY, .dio1 = PIN_DIO1, .rxen = PIN_RXEN,
        .tcxo_v = TCXO_V, .dio2_rf_switch = true,
        .freq_hz = RADIO_FREQ_HZ, .bw_khz = RADIO_BW_KHZ, .sf = RADIO_SF,
        .cr_denom = RADIO_CR_DENOM, .preamble = RADIO_PREAMBLE, .sync_word = RADIO_SYNC_WORD,
        .power_dbm = BASE_POWER_DBM,
    };
    uint8_t sync[2];
    int e = sx1262_probe(&s_radio, &cfg, sync);
    if (e == 0) e = sx1262_begin(&s_radio);
    if (e == 0) e = sx1262_start_receive(&s_radio);
    if (e) {
        for (;;) {                               /* keep telling the laptop */
            usb_printf("{\"ev\":\"error\",\"msg\":\"radio not ready (%d, sync %02X %02X)\"}\n",
                       e, sync[0], sync[1]);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
    hello();

    uint8_t buf[256];
    for (;;) {
        if (sx1262_wait_irq(&s_radio, 50)) {
            int16_t rssi = 0;
            float snr = 0;
            int n = sx1262_read_packet(&s_radio, buf, sizeof(buf), &rssi, &snr);
            lp_packet_t p;
            if (n > 0 && lp_decode(buf, n, &p)) handle_rx(&p, rssi, snr);
        }
        bridge_cmd_t c;
        while (xQueueReceive(s_cmdq, &c, 0) == pdTRUE) start_command(&c);
        retry_pending();
    }
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t usb = { .rx_buffer_size = 1024, .tx_buffer_size = 2048 };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    s_cmdq = xQueueCreate(8, sizeof(bridge_cmd_t));
    ESP_LOGW(TAG, "Liahona base station starting");
    xTaskCreate(usb_reader_task, "usb", 4096, NULL, 5, NULL);
    xTaskCreate(radio_task, "radio", 6144, NULL, 6, NULL);
}
