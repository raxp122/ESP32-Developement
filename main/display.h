// display.h — pannello AXS15231B + LVGL 9 in orizzontale (640×172 logici)
#pragma once
#include <stdbool.h>
#include "lvgl.h"

#define SCR_W 640
#define SCR_H 172

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
