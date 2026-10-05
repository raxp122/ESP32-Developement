// doom_app.h — avvio di Doom: il launcher imposta un flag e riavvia, al boot parte Doom
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool doom_find_wad(char *path, size_t n);   // cerca un IWAD su /sdcard/doom
void doom_launch(void);                     // riavvia direttamente in Doom
bool doom_boot_requested(void);             // chiamato da main: true = avvia Doom (e azzera il flag)
void doom_run(void);                        // avvia il motore (modalità Doom)
