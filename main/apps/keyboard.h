// keyboard.h — tastiera touch riutilizzabile a schermo intero, con pagine di simboli.
// Chi la usa crea un'app contenitore e, in enter(), chiama keyboard_open().
#pragma once
#include "ui.h"

typedef void (*kb_done_t)(const char *text, void *arg);  // text = NULL se annullato

// Apre la tastiera dentro root. title compare in alto. Se mask è true i caratteri
// sono nascosti (password). on_done viene chiamato con il testo a conferma (⏎) o
// NULL se l'utente esce (swipe a sinistra a campo vuoto).
void keyboard_open(lv_obj_t *root, const char *title, const char *initial, bool mask,
                   int maxlen, kb_done_t on_done, void *arg);
void keyboard_close(void);        // da chiamare in leave()
bool keyboard_nav(nav_t ev);      // da inoltrare da nav()
