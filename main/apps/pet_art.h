// pet_art.h — pixel art del Polipetto e un mini motore di disegno su buffer RGB565.
// Si disegna in "pixel logici" che vengono ingranditi (scale) nel buffer reale.
// Nessuna dipendenza da LVGL: si può provare anche sul PC.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet_core.h"

typedef struct { uint8_t w, h; const char *const *rows; } sprite_t;

typedef enum {
    EXPR_NORMAL, EXPR_BLINK, EXPR_HAPPY, EXPR_SAD, EXPR_ANGRY, EXPR_SLEEP, EXPR_SICK, EXPR_EAT, EXPR_SURPRISE,
    EXPR_BACK,   // di spalle: niente faccia
} expr_t;

enum { TINT_NONE, TINT_SICK, TINT_WHITE, TINT_GHOST };

// superficie di disegno corrente (stride in pixel reali)
void art_begin(uint16_t *buf, int lw, int lh, int scale, int stride);
void art_brightness(uint8_t b);   // 255 = normale, meno = più scuro (luce spenta)
void art_fill(uint32_t rgb);
void art_px(int x, int y, uint32_t rgb);
void art_rect(int x, int y, int w, int h, uint32_t rgb);
void art_sprite(const sprite_t *s, int x, int y, bool flip);
void art_sprite_color(const sprite_t *s, int x, int y, uint32_t rgb);   // sagoma tinta unita
void art_sprite_from(const sprite_t *s, int x, int y, int col0);       // solo dalla colonna col0 (cibo morsicato)

// Aspetto: colore, motivo e tentacoli (dai geni) più cappello e accessorio (dal negozio).
// Vale per i polipetti disegnati dopo; NULL = il classico arancione.
typedef struct { uint8_t color, pattern, tent, hat, acc; } art_look_t;
void art_look(const art_look_t *l);
uint32_t art_body_rgb(int color);   // colore principale (per i testi e le anteprime)

// Ambiente del fondale: momento del giorno, stagione, festa e decorazioni esposte
// (DAY_*, SEASON_*, HOL_* sono in pet_core.h)
typedef struct { uint8_t daypart, season, holiday; uint32_t deco; } art_env_t;
void art_env(const art_env_t *e);   // NULL = giorno, niente feste né decorazioni

// Negozio: oggetti numerati da 1 (0 = niente)
enum { ITEM_HAT, ITEM_ACC, ITEM_DECO };
typedef struct { const char *name; uint8_t kind; uint8_t price; } art_item_t;
extern const art_item_t ART_ITEMS[];
extern const int ART_ITEM_COUNT;    // compreso lo 0
void art_deco(int item, int frame); // una decorazione al suo posto nel fondale

// Polipetto. (x, y) = angolo in alto a sinistra della testa; i tentacoli scendono sotto.
// look: -1 guarda a sinistra, 0 avanti, +1 a destra (sullo schermo).
void art_pet_size(int stage, int form, int *w, int *h);
void art_pet(int stage, int form, int x, int y, expr_t e, int frame, bool flip, int look, int tint);
void art_egg(int x, int y, int crack);          // crack 0..3
void art_background(int frame);

#define EGG_W 10
#define EGG_H 12

extern const sprite_t SPR_HEART, SPR_HEART_BIG, SPR_DROP_BIG, SPR_SKULL, SPR_INK, SPR_FISH,
                      SPR_COOKIE, SPR_GLASS, SPR_PILL, SPR_BUBBLE, SPR_SPARK, SPR_JELLY, SPR_Z,
                      SPR_BANG, SPR_QUESTION, SPR_ANGER, SPR_HALO, SPR_BALL, SPR_BOOK, SPR_SHELL,
                      SPR_CAKE, SPR_NEST_EGG, SPR_GIFT;

// icone delle azioni (8×8, '#' = acceso)
enum { PICON_FOOD, PICON_PLAY, PICON_CLEAN, PICON_MEDICINE, PICON_LIGHT, PICON_SCOLD, PICON_STATS, PICON_CALL, PICON_COUNT };
extern const sprite_t PET_ICONS[PICON_COUNT];
