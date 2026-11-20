/*
 * Wi-Fi updates (v0.5).
 *
 * The leader presses Update on the Field dashboard; the base station sends
 * LP_CMD_UPDATE over LoRa. This task then:
 *   1. joins the Wi-Fi in liahona/wifi.json (via the board's ESP32-C6, ESP-Hosted),
 *   2. fetches the ball package from the Manager (/api/ball/package),
 *   3. downloads every file whose rev changed into liahona/ (temp file, size check,
 *      then rename), remembering revs in liahona/.sync.json,
 *   4. installs new firmware into the spare app slot (esp_https_ota) and restarts,
 *   5. reports each step back over LoRa (LP_UPDATE) and on the self-test page.
 *
 * wifi.json: { "ssid": "...", "password": "...", "manager": "https://...", "key": "..." }
 * Made by the Manager: Settings -> Balls over Wi-Fi -> Download wifi.json.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "cJSON.h"
#include "liahona.h"
#include "board_pins.h"
#include "journey_port.h"
#include "liahona_proto.h"
#include "update.h"

static const char *TAG = "update";

#define DIR_       "/sdcard/liahona"
#define WIFI_FILE  DIR_ "/wifi.json"
#define SYNC_FILE  DIR_ "/.sync.json"
#define WIFI_TIMEOUT_MS   30000
#define HTTP_TIMEOUT_MS   20000

static volatile bool s_busy;
static EventGroupHandle_t s_wifi_ev;
#define EV_UP   BIT0
#define EV_FAIL BIT1

/* ---------------------------------------------------------------- reporting */
static void report(uint8_t state, uint8_t detail, uint8_t done, uint8_t total, const char *text)
{
    const esp_app_desc_t *d = esp_app_get_description();
    lora_send_update(state, detail, done, total, d->version);
    STATE_LOCK();
    strlcpy(g_state.update_text, text, sizeof(g_state.update_text));
    g_state.update_state = state;
    STATE_UNLOCK();
    ESP_LOGI(TAG, "%s", text);
}

static void fail(uint8_t err, uint8_t done, uint8_t total)
{
    char t[64];
    snprintf(t, sizeof(t), "Update failed: %s", lp_upd_err_name(err));
    report(LP_UPD_FAILED, err, done, total, t);
}

/* ---------------------------------------------------------------- files */
static char *read_text(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (size_t)n > max) { fclose(f); return NULL; }
    char *b = malloc(n + 1);
    if (b) { size_t got = fread(b, 1, n, f); b[got] = 0; }
    fclose(f);
    return b;
}

static bool write_text(const char *path, const char *text)
{
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    ok = (fclose(f) == 0) && ok;
    if (!ok) { unlink(tmp); return false; }
    unlink(path);
    return rename(tmp, path) == 0;
}

/* ---------------------------------------------------------------- Wi-Fi */
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) xEventGroupSetBits(s_wifi_ev, EV_FAIL);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_wifi_ev, EV_UP);
}

static bool wifi_up(const char *ssid, const char *pass)
{
    static bool inited;
    if (!inited) {
        esp_err_t e = esp_netif_init();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
        e = esp_event_loop_create_default();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi didn't start (is the ESP32-C6 answering?)"); return false; }
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        s_wifi_ev = xEventGroupCreate();
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
        inited = true;
    }
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password));
    wc.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    xEventGroupClearBits(s_wifi_ev, EV_UP | EV_FAIL);
    if (esp_wifi_start() != ESP_OK) return false;

    /* a few tries: phone hotspots often drop the first association */
    for (int attempt = 0; attempt < 4; attempt++) {
        xEventGroupClearBits(s_wifi_ev, EV_UP | EV_FAIL);
        esp_wifi_connect();
        EventBits_t b = xEventGroupWaitBits(s_wifi_ev, EV_UP | EV_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_TIMEOUT_MS / 4));
        if (b & EV_UP) return true;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

static void wifi_down(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
}

/* ---------------------------------------------------------------- HTTP */
/* Absolute URL: links from the Manager may be a path ("/api/...") or a full https URL. */
static void full_url(const char *manager, const char *u, char *out, size_t cap)
{
    if (strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0) strlcpy(out, u, cap);
    else snprintf(out, cap, "%s%s", manager, u);
}

static esp_http_client_handle_t open_get(const char *url)
{
    esp_http_client_config_t c = {
        .url = url, .timeout_ms = HTTP_TIMEOUT_MS, .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096, .buffer_size_tx = 4096,       /* presigned S3 links are long */
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return NULL;
    if (esp_http_client_open(h, 0) != ESP_OK) { esp_http_client_cleanup(h); return NULL; }
    esp_http_client_fetch_headers(h);
    return h;
}

/* GET into memory (manifest). Returns malloc'd text or NULL; *status = HTTP status. */
static char *http_get_text(const char *url, size_t max, int *status)
{
    *status = 0;
    esp_http_client_handle_t h = open_get(url);
    if (!h) return NULL;
    *status = esp_http_client_get_status_code(h);
    char *buf = malloc(max + 1);
    size_t n = 0;
    if (buf) {
        int r;
        while (n < max && (r = esp_http_client_read(h, buf + n, max - n)) > 0) n += r;
        buf[n] = 0;
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return buf;
}

/* GET into a file on the SD card (temp name, then rename). expect = -1 if unknown. */
static bool http_get_file(const char *url, const char *path, long expect)
{
    esp_http_client_handle_t h = open_get(url);
    if (!h) return false;
    int status = esp_http_client_get_status_code(h);
    bool ok = status == 200;
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.part", path);
    FILE *f = ok ? fopen(tmp, "wb") : NULL;
    long total = 0;
    if (f) {
        static char chunk[4096];
        int r;
        while ((r = esp_http_client_read(h, chunk, sizeof(chunk))) > 0) {
            if (fwrite(chunk, 1, r, f) != (size_t)r) { ok = false; break; }
            total += r;
        }
        if (r < 0) ok = false;
        if (fclose(f) != 0) ok = false;
    } else ok = false;
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    if (ok && expect >= 0 && total != expect) { ESP_LOGW(TAG, "%s: got %ld bytes, expected %ld", path, total, expect); ok = false; }
    if (!ok) { unlink(tmp); ESP_LOGW(TAG, "download failed (%d): %s", status, path); return false; }
    unlink(path);
    return rename(tmp, path) == 0;
}

/* ---------------------------------------------------------------- firmware */
static int ota_install(const char *url)
{
    esp_http_client_config_t hc = { .url = url, .timeout_ms = HTTP_TIMEOUT_MS, .crt_bundle_attach = esp_crt_bundle_attach,
                                    .buffer_size = 4096, .buffer_size_tx = 4096, .keep_alive_enable = true };
    esp_https_ota_config_t oc = { .http_config = &hc };
    esp_https_ota_handle_t ota = NULL;
    if (esp_https_ota_begin(&oc, &ota) != ESP_OK) return -1;
    int total = esp_https_ota_get_image_size(ota), last = -1;
    esp_err_t e;
    while ((e = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(ota);
        int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
        if (pct / 10 != last / 10) {
            last = pct;
            char t[48];
            snprintf(t, sizeof(t), "Installing firmware: %d%%", pct);
            report(LP_UPD_FIRMWARE, (uint8_t)pct, 0, 0, t);
        }
    }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(ota)) { esp_https_ota_abort(ota); return -1; }
    return esp_https_ota_finish(ota) == ESP_OK ? 0 : -1;
}

/* ---------------------------------------------------------------- the update */
static void run_update(void)
{
    char *wj = read_text(WIFI_FILE, 4096);
    cJSON *w = wj ? cJSON_Parse(wj) : NULL;
    free(wj);
    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(w, "ssid"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(w, "password"));
    const char *mgr = cJSON_GetStringValue(cJSON_GetObjectItem(w, "manager"));
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(w, "key"));
    if (!ssid || !mgr || !key) { cJSON_Delete(w); fail(LP_UPE_NO_CONFIG, 0, 0); return; }
    char manager[128];
    strlcpy(manager, mgr, sizeof(manager));
    size_t ml = strlen(manager);
    while (ml && manager[ml - 1] == '/') manager[--ml] = 0;

    /* "update all": spread the balls out so their radio reports don't collide */
    STATE_LOCK(); int me_id = g_state.ball_id; STATE_UNLOCK();
    vTaskDelay(pdMS_TO_TICKS(300 * (me_id % 16)));
    report(LP_UPD_WIFI, 0, 0, 0, "Joining Wi-Fi...");
    if (!wifi_up(ssid, pass)) { cJSON_Delete(w); wifi_down(); fail(LP_UPE_WIFI, 0, 0); return; }

    report(LP_UPD_CHECK, 0, 0, 0, "Asking the Manager what's new...");
    static char url[2560];                          /* presigned S3 links run to ~2 KB */
    STATE_LOCK(); int ball = g_state.ball_id; STATE_UNLOCK();
    snprintf(url, sizeof(url), "%s/api/ball/package?key=%s&ball=%d", manager, key, ball);
    int status;
    char *mj = http_get_text(url, 256 * 1024, &status);
    cJSON_Delete(w);
    if (status == 401 || status == 403) { free(mj); wifi_down(); fail(LP_UPE_KEY, 0, 0); return; }
    cJSON *m = (mj && status == 200) ? cJSON_Parse(mj) : NULL;
    free(mj);
    cJSON *files = cJSON_GetObjectItem(m, "files");
    if (!cJSON_IsArray(files)) { cJSON_Delete(m); wifi_down(); fail(LP_UPE_MANAGER, 0, 0); return; }

    /* which files changed? */
    char *sj = read_text(SYNC_FILE, 64 * 1024);
    cJSON *sync = sj ? cJSON_Parse(sj) : NULL;
    free(sj);
    if (!cJSON_IsObject(sync)) { cJSON_Delete(sync); sync = cJSON_CreateObject(); }
    mkdir(DIR_, 0775);
    mkdir(DIR_ "/media", 0775);

    int total = 0, done = 0, changed = 0;
    cJSON *f;
    cJSON_ArrayForEach(f, files) {
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(f, "path"));
        const char *rev = cJSON_GetStringValue(cJSON_GetObjectItem(f, "rev"));
        if (!path || !rev) continue;
        char local[160];
        snprintf(local, sizeof(local), DIR_ "/%s", path);
        struct stat st;
        const char *have = cJSON_GetStringValue(cJSON_GetObjectItem(sync, path));
        if (!have || strcmp(have, rev) != 0 || stat(local, &st) != 0) total++;
    }
    if (total) audio_stop();                        /* don't play a file while it's being replaced */
    bool journey_changed = false;
    cJSON_ArrayForEach(f, files) {
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(f, "path"));
        const char *rev = cJSON_GetStringValue(cJSON_GetObjectItem(f, "rev"));
        const char *u = cJSON_GetStringValue(cJSON_GetObjectItem(f, "url"));
        cJSON *sz = cJSON_GetObjectItem(f, "size");
        if (!path || !rev || !u || strstr(path, "..")) continue;
        char local[160];
        snprintf(local, sizeof(local), DIR_ "/%s", path);
        struct stat st;
        const char *have = cJSON_GetStringValue(cJSON_GetObjectItem(sync, path));
        if (have && strcmp(have, rev) == 0 && stat(local, &st) == 0) continue;
        char t[64];
        snprintf(t, sizeof(t), "Downloading %d of %d", done + 1, total);
        report(LP_UPD_FILES, 0, (uint8_t)done, (uint8_t)total, t);
        full_url(manager, u, url, sizeof(url));
        if (!http_get_file(url, local, cJSON_IsNumber(sz) ? (long)sz->valuedouble : -1)) {
            cJSON_Delete(m); cJSON_Delete(sync); wifi_down();
            fail(LP_UPE_DOWNLOAD, (uint8_t)done, (uint8_t)total);
            return;
        }
        cJSON_DeleteItemFromObject(sync, path);
        cJSON_AddStringToObject(sync, path, rev);
        char *out = cJSON_PrintUnformatted(sync);         /* save as we go: a dropped link resumes */
        bool saved = out && write_text(SYNC_FILE, out);
        free(out);
        if (!saved) { cJSON_Delete(m); cJSON_Delete(sync); wifi_down(); fail(LP_UPE_SD, (uint8_t)done, (uint8_t)total); return; }
        if (!strcmp(path, "journey.json") || !strcmp(path, "roster.json")) journey_changed = true;
        done++; changed++;
    }
    cJSON_Delete(sync);
    if (journey_changed) journey_request_reload();

    /* firmware */
    const esp_app_desc_t *me = esp_app_get_description();
    cJSON *fw = cJSON_GetObjectItem(m, "firmware");
    const char *fv = cJSON_GetStringValue(cJSON_GetObjectItem(fw, "version"));
    const char *fu = cJSON_GetStringValue(cJSON_GetObjectItem(fw, "url"));
    if (fv && fu && strcmp(fv, me->version) != 0) {
        full_url(manager, fu, url, sizeof(url));
        char t[64];
        snprintf(t, sizeof(t), "Installing firmware %s...", fv);
        report(LP_UPD_FIRMWARE, 0, (uint8_t)changed, (uint8_t)total, t);
        if (ota_install(url) != 0) { cJSON_Delete(m); wifi_down(); fail(LP_UPE_FIRMWARE, (uint8_t)changed, (uint8_t)total); return; }
        cJSON_Delete(m);
        wifi_down();
        snprintf(t, sizeof(t), "Firmware %s installed, restarting", fv);
        lora_send_update(LP_UPD_REBOOT, 0, (uint8_t)changed, (uint8_t)total, fv);
        STATE_LOCK(); strlcpy(g_state.update_text, t, sizeof(g_state.update_text)); STATE_UNLOCK();
        vTaskDelay(pdMS_TO_TICKS(3000));            /* let the radio send it */
        esp_restart();
    }
    cJSON_Delete(m);
    wifi_down();
    if (changed) {
        char t[64];
        snprintf(t, sizeof(t), "Updated %d file%s", changed, changed == 1 ? "" : "s");
        report(LP_UPD_DONE, 0, (uint8_t)changed, (uint8_t)total, t);
    } else {
        report(LP_UPD_CURRENT, 0, 0, 0, "Already up to date");
    }
}

static void update_task(void *arg)
{
    run_update();
    s_busy = false;
    vTaskDelete(NULL);
}

void update_start(void)
{
    if (s_busy) { lora_send_update(LP_UPD_FAILED, LP_UPE_BUSY, 0, 0, esp_app_get_description()->version); return; }
    s_busy = true;
    if (xTaskCreate(update_task, "update", 12288, NULL, 3, NULL) != pdPASS) s_busy = false;
}

bool update_busy(void) { return s_busy; }

/* After a Wi-Fi firmware update the new image runs "on trial". Once the ball has
 * started everything (and the UI is up), keep it; if it crashes before this, the
 * bootloader goes back to the previous firmware. */
void update_confirm_boot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware %s confirmed", esp_app_get_description()->version);
        STATE_LOCK();
        snprintf(g_state.update_text, sizeof(g_state.update_text), "Running new firmware %s", esp_app_get_description()->version);
        STATE_UNLOCK();
        lora_send_update(LP_UPD_DONE, 0, 0, 0, esp_app_get_description()->version);
    }
}
