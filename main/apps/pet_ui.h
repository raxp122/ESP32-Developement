// pet_ui.h — interno all'app del Polipetto: le schermate in più (minigiochi, visite, negozio,
// diario, album, famiglia) sono moduli con la stessa forma. L'app disegna la scena in pixel
// art a sinistra e il pannello di testo a destra; il modulo dice cosa mettere in entrambi.
#pragma once
#include "apps.h"
#include "pet.h"
#include "pet_art.h"

#define LW       64        // scena in pixel logici
#define LH       32
#define FLOOR    28        // ultima riga d'acqua: i tentacoli poggiano qui

typedef struct {
    void (*enter)(void);
    void (*leave)(void);                    // facoltativa
    void (*update)(uint32_t dt);            // facoltativa: a ogni giro del timer
    void (*draw)(const pet_t *p);           // la scena (art_begin già fatto)
    bool (*nav)(nav_t ev);                  // i gesti; per uscire chiama pu_exit()
    void (*panel)(char *big, int nb, char *hint, int nh);   // testo grande e riga sotto
    uint16_t period;                        // ms del timer (0 = 100)
} pet_mod_t;

uint32_t pu_now(void);
void pu_exit(void);                                       // torna al menu di prima
void pu_say(const char *s, bool warn);                   // messaggio al posto della riga sotto
// fine di un minigioco: record, conchiglie, effetto sul polipetto e messaggio
void pu_result(int rec, int score, int shells, bool win, bool big, const char *text);
void pu_look_self(void);                                  // aspetto del polipetto (geni + vestiti)
void pu_look_genes(const uint8_t g[GENE_COUNT][2], int hat, int acc);
void pu_env(void);                                        // fondale del momento (ora, stagione, festa)
void pu_dims(int *w, int *h);                             // misure del polipetto (con i geni)
void pu_open_name(void);                                  // tastiera per cambiare nome
bool pu_held(void);                                       // dito sullo schermo o BOOT tenuto (con antirimbalzo)
void pu_fmt_age(uint32_t s, char *b, int n);

extern const pet_mod_t PM_STAR, PM_MEMORY, PM_RHYTHM, PM_VISIT, PM_SHOP, PM_DIARY, PM_ALBUM, PM_FAMILY;

// disegno mini del polipetto (orologio): fondale e polipetto che passeggia
void pet_mini_draw(uint16_t *buf, int lw, int lh, int sc, int stride, uint32_t ms);
