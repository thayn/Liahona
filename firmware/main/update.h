#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Wi-Fi update (see update.c). Safe to call from any task; ignored while one runs. */
void update_start(void);
bool update_busy(void);
/* Call once the ball has started everything: keeps new firmware after an OTA. */
void update_confirm_boot(void);
