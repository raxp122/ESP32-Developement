// input.h — touch con riconoscimento gesti + pulsanti fisici
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NAV_NONE = 0,
    NAV_PREV,       // swipe giù (o su, se invertito): voce precedente
    NAV_NEXT,       // swipe su: voce successiva
    NAV_SELECT,     // swipe a destra: avanti / conferma
    NAV_BACK,       // swipe a sinistra / tasto BOOT: indietro / annulla
    NAV_QUICK,      // BOOT tenuto premuto: azione rapida
    NAV_HOLD,       // dito fermo sullo schermo per ~1 s: azione rapida
    NAV_TAP,        // tocco breve: non fa nulla (evita conferme involontarie)
    NAV_BTN,        // tasto BOOT premuto: se l'app non lo usa vale come "indietro"
    NAV_TOUCH_DOWN, // primo contatto (serve per risvegliare lo schermo)
    NAV_PWR_CLICK,  // tasto PWR
    NAV_PWR_LONG,   // tasto PWR tenuto 2 s
} nav_t;

typedef void (*nav_handler_t)(nav_t ev);

void input_init(nav_handler_t handler);   // da chiamare con LVGL bloccato
bool input_touch(int *x, int *y);         // stato attuale, in coordinate dello schermo (SCR_W×SCR_H)
uint32_t input_idle_ms(void);
void input_mark_activity(void);
