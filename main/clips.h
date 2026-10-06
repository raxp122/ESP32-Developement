// clips.h — elenco dei testi ricevuti dal PC (codici, stringhe) da ridigitare via USB.
// Il PC li manda via Bluetooth (vedi clip_ble.c); qui si conservano, si leggono e si
// cancellano. Salvati nell'NVS: restano anche dopo lo spegnimento, finché non li
// cancelli tu (uno alla volta o tutti).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CLIP_MAX_LEN  512     // caratteri per voce (il resto viene troncato)
#define CLIP_MAX      40      // voci conservate (le più vecchie cadono)

typedef struct {
    uint32_t id;              // identificativo crescente
    uint32_t ts;             // quando è arrivato (epoch; 0 se l'orologio non era impostato)
    uint16_t len;
    uint8_t  used;           // già digitato almeno una volta
    char     text[CLIP_MAX_LEN];
} clip_t;

void clips_init(void);                          // all'avvio: carica dall'NVS
int  clips_count(void);
bool clips_get(int i, clip_t *out);             // i = 0 è il più recente
int  clips_add(const char *text, int len);      // ritorna l'indice (0) o -1; dedup del più recente
void clips_mark_used(uint32_t id);
void clips_delete(int i);
void clips_clear(void);
uint32_t clips_gen(void);                       // cambia a ogni modifica (per aggiornare la UI)
