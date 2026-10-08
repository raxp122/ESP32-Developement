// chess_engine.h — regole degli scacchi e motore di gioco, senza grafica né hardware
// (si prova anche sul PC: tools/chess_test.c).
//
// Scacchiera 0x88: casa = traversa*16 + colonna, a1 = 0, h8 = 0x77. Pezzi: tipo 1..6
// (pedone, cavallo, alfiere, torre, donna, re) più 8 per il nero; 0 = vuota.
// Il motore: ricerca alfa-beta con approfondimento iterativo, quiescenza, tabella delle
// trasposizioni, mosse killer e storia, mossa nulla. La forza si regola con un Elo
// indicativo (profondità, tempo e una scelta "umana" fra le mosse buone ai livelli bassi).
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { CP_EMPTY = 0, CP_P = 1, CP_N, CP_B, CP_R, CP_Q, CP_K };
#define CP_BLACK 8
#define CP_TYPE(p)  ((p) & 7)
#define CP_COLOR(p) ((p) >> 3)          // 0 bianco, 1 nero (solo per case non vuote)
#define CP_SQ(f, r) ((r) * 16 + (f))     // colonna 0..7 (a..h), traversa 0..7 (1..8)
#define CP_FILE(s)  ((s) & 7)
#define CP_RANK(s)  ((s) >> 4)

enum { CM_CAPTURE = 1, CM_EP = 2, CM_CASTLE = 4, CM_DOUBLE = 8, CM_PROMO = 16 };

typedef struct { uint8_t from, to, promo, flags; } cmove_t;   // promo = tipo (CP_Q…), 0 se nessuna
#define CM_NONE ((cmove_t){0, 0, 0, 0})
static inline bool cm_eq(cmove_t a, cmove_t b) { return a.from == b.from && a.to == b.to && a.promo == b.promo; }
static inline bool cm_null(cmove_t m) { return m.from == 0 && m.to == 0; }

typedef struct {
    uint8_t sq[128];
    uint8_t side;        // 0 bianco, 1 nero: chi muove
    uint8_t castle;      // 1 O-O bianco, 2 O-O-O bianco, 4 O-O nero, 8 O-O-O nero
    int8_t ep;           // casa della presa en passant, -1 nessuna
    uint8_t ksq[2];      // dove sono i re
    uint16_t halfmove;   // per la regola delle 50 mosse
    uint16_t fullmove;
    uint64_t hash;
} cpos_t;

#define CP_MAXMOVES 256

void cp_init(void);                                   // tabelle (una volta)
void cp_start(cpos_t *p);
bool cp_from_fen(cpos_t *p, const char *fen);
void cp_to_fen(const cpos_t *p, char *out);           // almeno 90 byte
int  cp_legal(const cpos_t *p, cmove_t *out);         // mosse legali
void cp_make(cpos_t *p, cmove_t m);                   // la mossa deve essere legale (o pseudo-legale)
bool cp_attacked(const cpos_t *p, int sq, int by);    // la casa è attaccata dal colore by
static inline bool cp_in_check(const cpos_t *p) { return cp_attacked(p, p->ksq[p->side], p->side ^ 1); }

// notazione algebrica (SAN, inglese: N B R Q K) con + e #; lettere italiane (C A T D R) per lo schermo
void cp_san(const cpos_t *p, cmove_t m, char *out);   // almeno 10 byte
void cp_san_it(const char *san, char *out);
bool cp_parse_san(const cpos_t *p, const char *san, cmove_t *m);
void cp_uci(cmove_t m, char *out);                    // "e2e4", "e7e8q"
bool cp_parse_uci(const cpos_t *p, const char *uci, cmove_t *m);

// stato della partita (hist: hash delle posizioni precedenti, per la ripetizione)
typedef enum { CS_PLAY, CS_MATE, CS_STALEMATE, CS_FIFTY, CS_REPETITION, CS_MATERIAL } cstatus_t;
cstatus_t cp_status(const cpos_t *p, const uint64_t *hist, int nhist);

/* ---------------- motore ---------------- */

#define CE_MATE 30000
typedef struct {
    cmove_t best;
    int score;          // centipedoni dal punto di vista di chi muove (±CE_MATE-n: matto)
    int depth;          // profondità completata
    uint32_t nodes;
    cmove_t pv[16];
    int pv_len;
} cresult_t;

// ora in ms e "lascia respirare il resto del sistema" (sulla scheda: vTaskDelay); il PC usa i suoi
extern uint32_t (*ce_now_ms)(void);
extern void (*ce_yield)(void);
extern volatile bool ce_abort;                        // ferma subito la ricerca in corso

bool ce_init(int tt_bits);                            // tabella delle trasposizioni: 2^bits voci da 16 byte
void ce_clear(void);                                  // nuova partita: svuota tabella e storia
int  ce_eval(const cpos_t *p);                        // valutazione statica, dal punto di vista di chi muove
// ricerca: fino a max_depth o max_ms (il primo che arriva)
void ce_search(const cpos_t *p, const uint64_t *hist, int nhist, int max_depth, int max_ms, cresult_t *r);
// la mossa del Gadget a un certo Elo (400–2400, indicativo). ply = semimosse giocate (varietà in apertura)
cmove_t ce_play(const cpos_t *p, const uint64_t *hist, int nhist, int elo, int ply, uint32_t seed, cresult_t *r);
int  ce_think_ms(int elo);                            // tempo massimo per mossa a quell'Elo
// libro delle aperture (linee principali): una mossa a caso fra quelle che proseguono la partita
// (moves: le semimosse giocate dall'inizio). false se la partita è uscita dal libro.
bool ce_book(const cmove_t *moves, int n, uint32_t seed, cmove_t *out);
int  ce_book_check(void);                             // prova: quante linee hanno mosse illegali
