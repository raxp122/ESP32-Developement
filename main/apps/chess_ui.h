// chess_ui.h — la scacchiera disegnata (pixel art, come il Polipetto) e i tocchi sulle case.
// La usano partita, analisi e allenamento (app_chessplay.c).
#pragma once
#include "apps.h"
#include "chess_engine.h"

typedef struct {
    lv_obj_t *canvas;
    int x, y, px;         // posizione e lato in pixel dello schermo
    int sc;               // pixel dello schermo per pixel logico (una casa = 10 pixel logici)
    bool flip;            // nero in basso
    int cursor;           // casa del cursore (-1 nascosto)
    int sel;              // pezzo scelto (-1 nessuno)
    uint8_t mark[128];    // 1 = il pezzo scelto può andare qui
    int last_from, last_to;   // ultima mossa (-1 nessuna)
    int hint_from, hint_to;   // mossa suggerita / migliore (-1 nessuna)
    bool show_marks;
} cboard_t;

void cb_create(cboard_t *b, lv_obj_t *root, int x, int y, int px);
void cb_draw(cboard_t *b, const cpos_t *p);
int  cb_hit(const cboard_t *b, int sx, int sy);   // casa toccata (-1 fuori)
void cb_move_cursor(cboard_t *b, int df, int dr); // df/dr in direzione dello schermo (destra, su)
void cb_clear_marks(cboard_t *b);

// tocchi brevi sullo schermo (gli swipe e il dito tenuto restano ai comandi normali)
void cb_tap_reset(void);
bool cb_tap_poll(int *x, int *y);
