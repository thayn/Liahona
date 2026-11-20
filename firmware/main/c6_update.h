#pragma once
#include <stdbool.h>

/* Update the ESP32-C6 Wi-Fi chip from liahona/c6_wifi.bin on the SD card (see c6_update.c). */
void c6_update_start(void);
bool c6_update_busy(void);
