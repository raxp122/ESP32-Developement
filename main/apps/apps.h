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
extern const app_t app_pet;
extern const app_t app_ota;
extern const app_t app_restore;
extern menu_t backup_menu;
extern const app_t app_level;
extern menu_t pet_settings_menu;
extern const app_t app_ble_device;   // arg = ble_dev_t* da collegare
extern const app_t app_ble_pair;
extern const app_t app_ble_conns;
extern menu_t clips_menu;             // Appunti: testi dal PC → tastiera USB
extern const app_t app_clip_list;
extern const app_t app_8ball;
extern const app_t app_q20;
extern const app_t app_theremin;

// App all'avvio (Impostazioni): elenco con indici stabili, salvati in g_set.boot_app
int  boot_app_count(void);
const char *boot_app_name(int i);
void boot_app_launch(void);   // apre l'app scelta sopra la home (da chiamare con LVGL bloccato)
