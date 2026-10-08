// logcon.h — log in memoria + comandi dalla seriale USB
// Comandi (una lettera + Invio) dal monitor seriale:
//   L  ristampa il log dall'accensione      R  riavvia
//   I  informazioni di sistema               H  aiuto
//   P  log della sessione prima dell'ultimo riavvio (dopo un crash, un watchdog, un riavvio)
#pragma once
void logcon_init(void);   // chiamare per prima cosa: cattura anche i log di avvio
void logcon_start(void);  // avvia il task che legge i comandi
