// ota.h — aggiornamenti del firmware via internet (release di GitHub).
// Il controllo e il download girano in un task a parte; l'interfaccia legge lo stato.
// Le impostazioni non si toccano: stanno nell'NVS, che l'OTA non scrive mai.
#pragma once
#include <stdbool.h>

typedef enum {
    OTA_IDLE,         // nessun controllo fatto
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,    // c'è una versione più nuova: ota_latest()
    OTA_DOWNLOADING,  // ota_progress() 0–100
    OTA_DONE,         // installato: la scheda si riavvia da sola
    OTA_ERROR,        // ota_error()
} ota_state_t;

void        ota_mark_valid(void);       // all'avvio, quando il firmware funziona (annulla il ritorno automatico)
void        ota_check(void);            // controlla se c'è una versione nuova (in background)
void        ota_install(void);          // scarica e installa l'ultima versione (in background)
void        ota_auto_start(void);       // al primo collegamento a internet controlla e avvisa
ota_state_t ota_state(void);
int         ota_progress(void);
const char *ota_current(void);          // versione in uso
const char *ota_latest(void);           // ultima versione trovata ("" se non nota)
const char *ota_error(void);
