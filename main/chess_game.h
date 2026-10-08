// chess_game.h — partite, analisi e archivio degli Scacchi sulla microSD (cartella scacchi/):
//   partite/0001.pgn …  ogni partita in PGN (con i commenti dell'analisi, se fatta)
//   indice.csv           una riga per partita, per l'elenco
//   elo.csv              l'andamento del tuo Elo (solo partite contro il Gadget senza aiuti)
//   esercizi.csv         le posizioni dove hai sbagliato, per l'allenamento
//   incorso.txt          la partita interrotta, da riprendere
//   per_IA.txt           l'esportazione per farle analizzare a un'IA
// Elo e impostazioni restano anche nella memoria interna (NVS): senza scheda si gioca lo stesso.
// Niente grafica: si prova anche sul PC (tools/chess/).
#pragma once
#include "chess_engine.h"
#include <stdbool.h>
#include <stdint.h>

#define CG_MAXPLY 400

enum { CG_MODE_BOT, CG_MODE_TWO };
enum { CG_RES_NONE, CG_RES_WHITE, CG_RES_BLACK, CG_RES_DRAW };
enum { CG_END_NONE, CG_END_MATE, CG_END_STALEMATE, CG_END_FIFTY, CG_END_REPETITION, CG_END_MATERIAL,
       CG_END_RESIGN, CG_END_AGREED };
// giudizio di una mossa dopo l'analisi
enum { CG_CLS_NONE, CG_CLS_BEST, CG_CLS_GOOD, CG_CLS_INACC, CG_CLS_MISTAKE, CG_CLS_BLUNDER };

typedef struct {
    uint16_t id;           // 0 = non ancora salvata
    char date[20];         // "2026.10.08 14:30"
    uint8_t mode, color;   // color: il tuo colore contro il Gadget (0 bianco, 1 nero)
    int16_t bot_elo;
    uint8_t result, end;
    uint16_t plies;
    int16_t elo_before, elo_after;   // 0 se la partita non conta per l'Elo
    int8_t acc[2];                    // precisione del bianco e del nero (-1: non analizzata)
    uint8_t hints, undos;
    uint8_t rated;
} cg_info_t;

typedef struct {
    cg_info_t info;
    cmove_t mv[CG_MAXPLY];
    int n;
    // analisi (valutazioni dal punto di vista del bianco, in centipedoni; ±9999 = matto)
    bool analyzed;
    int16_t eval[CG_MAXPLY + 1];
    cmove_t best[CG_MAXPLY];
    uint8_t cls[CG_MAXPLY];
} cgame_t;

typedef struct {
    int16_t bot_elo;       // livello del Gadget
    uint8_t adaptive;      // il Gadget gioca al tuo Elo
    uint8_t color;         // 0 bianco, 1 nero, 2 a caso
    uint8_t notation_it;   // C A T D R invece di N B R Q K sullo schermo
    uint8_t flip_two;      // a due: la scacchiera si gira a ogni mossa
    uint8_t show_moves;    // puntini sulle case dove può andare il pezzo
    int16_t elo;           // il tuo Elo
    uint16_t rated_games;
    uint16_t next_id;
} cg_cfg_t;
extern cg_cfg_t cg_cfg;

void cg_cfg_load(void);
void cg_cfg_save(void);

// posizione dopo k semimosse (k = 0: l'inizio); hist: hash delle posizioni precedenti
void cg_position(const cgame_t *g, int k, cpos_t *p, uint64_t *hist);
void cg_san(const cgame_t *g, int k, char *out);          // la semimossa k in SAN (inglese)
void cg_san_show(const cgame_t *g, int k, char *out);     // per lo schermo (italiano se scelto)
void cg_new(cgame_t *g, int mode, int color, int bot_elo);
const char *cg_result_str(int result);                     // "1-0", "0-1", "1/2-1/2", "*"
const char *cg_end_str(int end);                           // "scacco matto", …

// archivio
bool cg_store_ok(void);                                    // c'è la microSD
bool cg_save(cgame_t *g);                                  // assegna l'id se manca; aggiorna l'indice
bool cg_load(int id, cgame_t *g);
int  cg_list(cg_info_t *out, int max);                     // dalla più recente
bool cg_delete(int id);
// Elo: punteggio 1, 0.5, 0 contro un avversario di quell'Elo; restituisce il nuovo Elo
int  cg_elo_update(cgame_t *g, float score);
int  cg_elo_history(int16_t *out, int max);                // valori in ordine di tempo
void cg_stats(int *won, int *drawn, int *lost);            // contro il Gadget

// partita in corso
bool cg_resume_save(const cgame_t *g);
bool cg_resume_load(cgame_t *g);
void cg_resume_clear(void);
bool cg_resume_exists(void);

// analisi: a ogni posizione il motore cerca per ms_per_pos; progress(k, n) dopo ogni posizione
// (se restituisce false si interrompe). Poi giudizi, precisione ed esercizi dagli errori.
typedef bool (*cg_progress_t)(int k, int n);
bool cg_analyze(cgame_t *g, int ms_per_pos, cg_progress_t progress);
const char *cg_cls_name(int cls);
int  cg_win_pct(int cp);                                   // probabilità di vittoria (0–100)

// allenamento
typedef struct {
    char fen[92];
    cmove_t best, played;
    uint16_t game_id, ply;
    uint8_t ok, ko;        // volte risolto / sbagliato
} cg_puzzle_t;
int  cg_puzzle_count(int *todo);                           // todo: quelli non ancora risolti
bool cg_puzzle_pick(cg_puzzle_t *p, int *index);           // il prossimo (prima i non risolti)
void cg_puzzle_result(int index, bool ok);

// esportazione per un'IA: tutte le partite contro il Gadget, con riepilogo e istruzioni
bool cg_export_llm(char *path_out, int n);
