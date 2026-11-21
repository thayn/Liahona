/* Thread-safe calls into the journey engine, for the screen (journey_port.c). */
#pragma once
#include "journey.h"

void journey_ui_view(jv_view_t *v);
void journey_ui_choice(int i);
void journey_ui_continue(void);
void journey_ui_hint(void);
void journey_ui_replay(void);
void journey_ui_restart(void);
void journey_ui_skip(void);     /* skip the current step (self-test, for testing) */
/* Read journey.json + roster.json again (after a Wi-Fi update). */
void journey_request_reload(void);
