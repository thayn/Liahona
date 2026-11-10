/* Fonts converted from Cinzel and Inter (SIL Open Font License, see fonts/OFL-*.txt). */
#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(font_cinzel_56);       /* Cinzel Medium 56: distances, headings, messages */
LV_FONT_DECLARE(font_cinzel_bold_46);  /* Cinzel Bold 46: "N" */
LV_FONT_DECLARE(font_cinzel_30);       /* Cinzel Regular 30: E S W, titles */
LV_FONT_DECLARE(font_inter_22);        /* Inter SemiBold 22: small captions */

void ui_compass_build(lv_obj_t *scr);
