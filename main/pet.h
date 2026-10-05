// pet.h — il Polipetto vive qui: tempo, salvataggio, contapassi, versi.
// Tutte le funzioni (tranne il task dell'accelerometro, interno) vanno chiamate dal
// task di LVGL, come le app.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet_core.h"

enum {
    SND_CALL, SND_HAPPY, SND_EAT, SND_DRINK, SND_NO, SND_CLEAN, SND_MEDICINE, SND_SCOLD,
    SND_HATCH, SND_EVOLVE, SND_DEATH, SND_WIN, SND_LOSE, SND_TICK, SND_COUNT
};

void     pet_init(void);                 // all'avvio, con LVGL bloccato
pet_t   *pet_get(void);
void     pet_new_egg(void);
uint32_t pet_do(pet_action_t a, pet_result_t *res);   // azione + verso + salvataggio
uint32_t pet_take_events(void);          // EV_* accumulati dall'ultima chiamata
uint32_t pet_take_shakes(void);          // scossoni rilevati con l'app aperta
void     pet_set_foreground(bool on);    // l'app del polipetto è aperta
void     pet_set_walking(bool on);       // passeggiata: i passi valgono doppio
void     pet_play(int snd);
void     pet_save(void);
bool     pet_time_known(void);           // l'orologio è impostato
