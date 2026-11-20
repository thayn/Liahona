#pragma once
/* Crash-surviving log ring, saved to the SD card and uploaded over Wi-Fi (logbuf.c). */
void        logbuf_start(void);         /* first thing in app_main */
void        logbuf_save_crash(void);    /* once the SD card is mounted */
int         logbuf_upload(const char *manager, const char *key, int ball);  /* Wi-Fi up; returns files sent */
const char *logbuf_last_reset(void);    /* "power on", "brownout", "crash", ... */
