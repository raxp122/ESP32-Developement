// backup.h — backup completo del dispositivo su microSD (cartella /backup) e ripristino.
// Contiene tutte le nostre voci dell'NVS (impostazioni, polipetto, Radar, livella) e i
// file di stato sulla microSD (Pokédex). Formato di testo con controllo CRC.
//
// Compatibilità: un backup si può ripristinare sul firmware che l'ha creato o su uno più
// nuovo; mai su uno più vecchio. Su un firmware più nuovo le impostazioni passano dalle
// solite migrazioni: quello che manca prende il valore predefinito, quello che non esiste
// più si ignora.
#pragma once
#include <stdbool.h>

typedef struct {
    char name[40];        // nome del file in /backup
    char firmware[24];    // versione che l'ha creato
    char created[24];     // data e ora ("" se l'orologio non era impostato)
    int  format, settings;
    bool usable;          // ripristinabile su questo firmware
    char why[72];         // se non lo è, perché
} backup_info_t;

bool        backup_create(char *name, int n);              // name: file creato
int         backup_list(backup_info_t *out, int max);      // dal più recente; -1 se manca la microSD
int         backup_count(void);                            // -1 se manca la microSD (con piccola cache)
bool        backup_restore(const char *name);              // poi va riavviata la scheda
const char *backup_error(void);
