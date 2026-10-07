// display_round.h — AMOLED tondo 466×466 (CO5300) della ESP32-S3-Touch-AMOLED-1.75
#pragma once
#include <stdbool.h>
#include "lvgl.h"

lv_display_t *display_round_init(volatile bool *dark);   // dopo lv_init(); dark: niente invii
void display_round_brightness(int pct);                  // 0–100
