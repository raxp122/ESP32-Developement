// usb_keymap.h — quali tasti premere per scrivere un carattere con la tastiera del PC
// impostata su un certo layout (KB_LAYOUT_* di settings.h).
// Codice puro, senza TinyUSB: si compila sempre e si prova anche sul PC.
//
// I codici HID indicano la posizione fisica del tasto (come su una tastiera US): è il PC
// a tradurla nel carattere secondo il suo layout. Per questo serve sapere quel layout.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// modificatori
#define KM_SHIFT 0x02   // Shift sinistro
#define KM_ALTGR 0x40   // Alt destro (AltGr)

// codici HID dei tasti usati
enum {
    K_A = 0x04, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
    K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z,
    K_1 = 0x1E, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0,
    K_ENTER = 0x28, K_TAB = 0x2B, K_SPACE = 0x2C,
    K_MINUS = 0x2D, K_EQUAL, K_LBRACKET, K_RBRACKET, K_BACKSLASH, K_EUR1,
    K_SEMICOLON, K_APOSTROPHE, K_GRAVE, K_COMMA, K_PERIOD, K_SLASH,
    K_EUR2 = 0x64,   // tasto in più delle tastiere europee, a sinistra della Z
};

// Una "battuta": eventuale tasto morto prima (accento), il tasto, eventuale spazio dopo
// (per scrivere il simbolo di un tasto morto da solo, es. ^ sulle tastiere che lo usano
// per gli accenti).
typedef struct {
    uint8_t pmod, pkey;   // tasto morto prima (0 = nessuno)
    uint8_t mod, key;
    bool    space;        // dopo, uno spazio
} kb_stroke_t;

// false se il layout non sa scrivere quel carattere (codice Unicode)
bool kb_map(uint32_t cp, int layout, kb_stroke_t *out);
