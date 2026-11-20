/*
 * Ball log, readable from the Manager after a crash.
 *
 * Every ESP_LOG line also goes into a 16 KB ring in RAM that a restart doesn't
 * clear. At start-up, if the last restart was a crash, watchdog or brownout,
 * that ring is saved to the SD card as liahona/logs/<n>-<reason>.txt. On the
 * next Wi-Fi update, saved logs go up to the Manager (POST /api/ball/log),
 * along with a snapshot of the current run ("now"). Uploaded files are renamed
 * to .sent.
 *
 * A crash also writes its reason and the CPU registers (PC, RA, SP) into the
 * ring (esp_panic_handler is wrapped, see CMakeLists.txt), so the uploaded log
 * says where it crashed. Turn an address into a line of code with:
 *   riscv32-esp-elf-addr2line -pfiaC -e build/liahona_fw.elf 0x4000xxxx
 * (use the .elf from the same build that's on the ball).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_private/panic_internal.h"
#include "riscv/rvruntime-frames.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "cJSON.h"
#include "board_pins.h"
#include "logbuf.h"

#define RING_SIZE  16384
#define RING_MAGIC 0x4C494148u          /* "LIAH" */
#define LOG_DIR    CONTENT_DIR "/logs"

typedef struct {
    uint32_t magic;
    uint32_t head;                      /* next write position */
    uint32_t wrapped;
    char     buf[RING_SIZE];
} ring_t;

static __NOINIT_ATTR ring_t s_ring;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_uart;
static char s_last_reason[24] = "power on";

static const char *TAG = "logbuf";

static void ring_put(const char *s, int n)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) {
        s_ring.buf[s_ring.head++] = s[i];
        if (s_ring.head >= RING_SIZE) { s_ring.head = 0; s_ring.wrapped = 1; }
    }
    taskEXIT_CRITICAL(&s_mux);
}

static int log_vprintf(const char *fmt, va_list ap)
{
    char line[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(line, sizeof(line), fmt, ap2);
    va_end(ap2);
    if (n > 0) ring_put(line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
    return s_uart ? s_uart(fmt, ap) : n;
}

/* Copy the ring out in order (oldest first). Returns a malloc'd string. */
static char *ring_copy(void)
{
    char *out = heap_caps_malloc(RING_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) return NULL;
    taskENTER_CRITICAL(&s_mux);
    size_t n = 0;
    if (s_ring.wrapped) {
        memcpy(out, s_ring.buf + s_ring.head, RING_SIZE - s_ring.head);
        n = RING_SIZE - s_ring.head;
    }
    memcpy(out + n, s_ring.buf, s_ring.head);
    n += s_ring.head;
    taskEXIT_CRITICAL(&s_mux);
    out[n] = 0;
    for (size_t i = 0; i < n; i++) if (out[i] == 0) out[i] = '?';   /* keep it one string */
    return out;
}

/* ---- crash capture (runs inside the panic handler: no locks, no printf) ---- */
static void IRAM_ATTR raw_put(const char *s)
{
    if (!s) return;
    while (*s) {
        s_ring.buf[s_ring.head++] = *s++;
        if (s_ring.head >= RING_SIZE) { s_ring.head = 0; s_ring.wrapped = 1; }
    }
}
static void IRAM_ATTR raw_hex(const char *label, uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char b[12] = { ' ', '0', 'x' };
    for (int i = 0; i < 8; i++) b[3 + i] = d[(v >> (28 - 4 * i)) & 15];
    b[11] = 0;
    raw_put(label);
    raw_put(b);
}

void __real_esp_panic_handler(panic_info_t *info);
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
    if (s_ring.magic == RING_MAGIC && s_ring.head < RING_SIZE && info) {
        raw_put("\n!!! CRASH on core ");
        char c[2] = { (char)('0' + (info->core & 7)), 0 };
        raw_put(c);
        raw_put(": ");
        raw_put(info->reason);
        if (info->description) { raw_put(" - "); raw_put(info->description); }
        raw_hex("\n!!! addr", (uint32_t)(uintptr_t)info->addr);
        const RvExcFrame *f = (const RvExcFrame *)info->frame;
        if (f) {
            raw_hex("  PC", (uint32_t)f->mepc);
            raw_hex("  RA", (uint32_t)f->ra);
            raw_hex("  SP", (uint32_t)f->sp);
            raw_hex("  MCAUSE", (uint32_t)f->mcause);
            raw_hex("  MTVAL", (uint32_t)f->mtval);
        }
        raw_put("\n");
    }
    __real_esp_panic_handler(info);
}

static const char *reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_SW:        return "restart";
    case ESP_RST_PANIC:     return "crash";
    case ESP_RST_INT_WDT:   return "watchdog (int)";
    case ESP_RST_TASK_WDT:  return "watchdog (task)";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "wake";
    default:                return "other reset";
    }
}

static bool is_bad(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT || r == ESP_RST_BROWNOUT || r == ESP_RST_UNKNOWN;
}

const char *logbuf_last_reset(void) { return s_last_reason; }

void logbuf_start(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    strlcpy(s_last_reason, reason_name(r), sizeof(s_last_reason));
    bool kept = s_ring.magic == RING_MAGIC && s_ring.head < RING_SIZE;
    if (!kept) { s_ring.magic = RING_MAGIC; s_ring.head = 0; s_ring.wrapped = 0; }
    /* keep the previous run in the ring until logbuf_save_crash() has saved it */
    char mark[80];
    int n = snprintf(mark, sizeof(mark), "\n===== boot: last reset = %s =====\n", s_last_reason);
    ring_put(mark, n);
    s_uart = esp_log_set_vprintf(log_vprintf);
    if (is_bad(r)) ESP_LOGW(TAG, "last restart was a %s", s_last_reason);
}

void logbuf_save_crash(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    if (!is_bad(r)) return;
    char *text = ring_copy();
    if (!text) return;
    mkdir(LOG_DIR, 0775);

    uint32_t no = 0;
    nvs_handle_t h;
    if (nvs_open("logbuf", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "n", &no);
        nvs_set_u32(h, "n", ++no);
        nvs_commit(h);
        nvs_close(h);
    }
    char path[96], tag[24];
    strlcpy(tag, s_last_reason, sizeof(tag));
    for (char *c = tag; *c; c++) if (*c == ' ' || *c == '(' || *c == ')') *c = '_';
    snprintf(path, sizeof(path), LOG_DIR "/%03lu-%s.txt", (unsigned long)no, tag);
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); ESP_LOGW(TAG, "saved %s", path); }
    free(text);
}

/* POST {"name":..., "reason":..., "text":...} to the Manager. */
static bool post_log(const char *manager, const char *key, int ball, const char *name, const char *text)
{
    char url[320];
    snprintf(url, sizeof(url), "%s/api/ball/log?key=%s&ball=%d", manager, key, ball);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddStringToObject(o, "reason", s_last_reason);
    cJSON_AddStringToObject(o, "text", text);
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!body) return false;
    esp_http_client_config_t c = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000,
                                   .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 2048, .buffer_size_tx = 4096 };
    esp_http_client_handle_t hc = esp_http_client_init(&c);
    bool ok = false;
    if (hc) {
        esp_http_client_set_header(hc, "Content-Type", "application/json");
        esp_http_client_set_post_field(hc, body, strlen(body));
        if (esp_http_client_perform(hc) == ESP_OK) {
            int st = esp_http_client_get_status_code(hc);
            ok = st == 200;
            if (!ok) ESP_LOGW(TAG, "log upload %s: HTTP %d", name, st);
        }
        esp_http_client_cleanup(hc);
    }
    free(body);
    return ok;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *b = heap_caps_malloc(RING_SIZE + 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t n = b ? fread(b, 1, RING_SIZE + 1023, f) : 0;
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

int logbuf_upload(const char *manager, const char *key, int ball)
{
    int sent = 0;
    DIR *d = opendir(LOG_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcasecmp(e->d_name + l - 4, ".txt")) continue;
            char path[300], done[310];
            snprintf(path, sizeof(path), LOG_DIR "/%s", e->d_name);
            char *t = read_all(path);
            if (!t) continue;
            if (post_log(manager, key, ball, e->d_name, t)) {
                snprintf(done, sizeof(done), "%s.sent", path);
                unlink(done);
                rename(path, done);
                sent++;
            }
            free(t);
        }
        closedir(d);
    }
    char *now = ring_copy();
    if (now) { if (post_log(manager, key, ball, "now.txt", now)) sent++; free(now); }
    return sent;
}
