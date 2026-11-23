/*
 * Freeze watch. If the screen stops updating (the LVGL task is stuck), log
 * what every task is doing and who holds the shared locks, then restart.
 * The log survives the restart (logbuf.c) and goes to the Manager on the next
 * Wi-Fi update. The journey resumes where it was, so a team loses ~20 s
 * instead of needing someone to power-cycle the ball.
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "liahona.h"
#include "journey_port.h"
#include "freeze_watch.h"

#define STALL_MS   15000

static const char *TAG = "freeze";
static volatile int64_t s_beat;

static void beat_cb(lv_timer_t *t) { s_beat = esp_timer_get_time() / 1000; }

static const char *holder(SemaphoreHandle_t m)
{
    TaskHandle_t h = m ? xSemaphoreGetMutexHolder(m) : NULL;
    return h ? pcTaskGetName(h) : "nobody";
}

static void dump_tasks(void)
{
#if configUSE_TRACE_FACILITY
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *ts = malloc(n * sizeof(TaskStatus_t));
    if (!ts) return;
    n = uxTaskGetSystemState(ts, n, NULL);
    static const char *st[] = { "running", "ready", "blocked", "suspended", "deleted", "invalid" };
    for (UBaseType_t i = 0; i < n; i++)
        ESP_LOGE(TAG, "  task %-16s %-9s prio %2u  stack left %lu",
                 ts[i].pcTaskName, st[ts[i].eCurrentState < 6 ? ts[i].eCurrentState : 5],
                 (unsigned)ts[i].uxCurrentPriority, (unsigned long)ts[i].usStackHighWaterMark);
    free(ts);
#else
    ESP_LOGE(TAG, "  (task list needs CONFIG_FREERTOS_USE_TRACE_FACILITY)");
#endif
}

static void watch_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(STALL_MS));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int64_t now = esp_timer_get_time() / 1000;
        if (now - s_beat < STALL_MS) continue;
        ESP_LOGE(TAG, "SCREEN FROZE: no LVGL update for %lld s", (long long)((now - s_beat) / 1000));
        ESP_LOGE(TAG, "  journey lock held by: %s", holder(journey_port_lock()));
        ESP_LOGE(TAG, "  state lock held by:   %s", holder(g_state_lock));
        dump_tasks();
        vTaskDelay(pdMS_TO_TICKS(200));             /* let the log land */
        esp_system_abort("screen froze (see freeze log)");
    }
}

void freeze_watch_start(void)
{
    s_beat = esp_timer_get_time() / 1000;
    lv_timer_create(beat_cb, 500, NULL);           /* called with the display lock held, from ui_start */
    xTaskCreate(watch_task, "freeze", 4096, NULL, 10, NULL);
}
