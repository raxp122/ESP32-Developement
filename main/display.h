// display.h — schermo e LVGL 9. Le dimensioni dipendono dalla scheda (board.h):
//   3.49: pannello AXS15231B in orizzontale, 640×172 logici (rotazione software)
//   AMOLED 1.75: tondo 466×466 (display_round.c)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

extern int16_t g_scr_w, g_scr_h;   // fissati da display_init
#define SCR_W ((int)g_scr_w)
#define SCR_H ((int)g_scr_h)
#define SCR_ROUND (g_scr_w == g_scr_h)   // schermo tondo: tutto deve stare nel cerchio

void display_init(bool flipped);          // flipped = ruotato di 180°
void display_set_flipped(bool flipped);
bool display_is_flipped(void);
void display_set_brightness(int percent); // 0–100
bool display_is_dark(void);               // retroilluminazione spenta (inutile disegnare)
void display_lock(void);
void display_unlock(void);
void display_start_task(void);

// Accesso diretto al frame fisico (172×640 RGB565 big-endian), usato da Doom
uint16_t *display_frame(void);
void display_push(void);                  // invia l'intero frame al pannello
void display_keep_rows(int y0, int y1);   // LVGL non sovrascrive queste righe fisiche            // avvia il task che esegue lv_timer_handler
