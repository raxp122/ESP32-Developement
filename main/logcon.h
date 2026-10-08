// logcon.h — log in memoria + comandi dalla seriale USB
// Comandi (una lettera + Invio) dal monitor seriale:
//   L  ristampa il log dall'accensione      R  riavvia
//   I  informazioni di sistema               H  aiuto
//   P  log della sessione prima dell'ultimo riavvio (dopo un crash, un watchdog, un riavvio)
#pragma once
void logcon_init(void);   // chiamare per prima cosa: cattura anche i log di avvio
void logcon_start(void);  // avvia il task che legge i comandi
#include <stdbool.h>
void logcon_info(char *buf, int n);              // le informazioni del comando I (più righe)
const char *logcon_reset_reason(void);           // motivo dell'ultimo reset, in parole
bool logcon_has_prev(void);                       // c'è il log della sessione prima del riavvio
bool logcon_write_report(const char *path);       // informazioni + log precedente + log attuale in un file
