// apps.h — registro delle app. Per aggiungerne una: crea apps/app_xxx.c con un
// "const app_t app_xxx", dichiarala qui e aggiungi una voce in home.c.
#pragma once
#include "ui.h"

extern const app_t app_clock;
extern const app_t app_wifiscan;
extern const app_t app_blescan;
extern const app_t app_torch;
extern const app_t app_portal;
extern const app_t app_search;
extern const app_t app_bercio;

extern menu_t home_menu;
void home_init(void);   // ordina le app alfabeticamente
extern menu_t settings_menu;
extern menu_t doom_menu;
extern menu_t dice_menu;
extern menu_t saber_menu;
extern menu_t tuner_menu;
extern const app_t app_tuner;
void tuner_menu_init(void);
extern menu_t radar_menu;
