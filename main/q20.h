// q20.h — Q-20: indovina a cosa stai pensando con (circa) venti domande.
//
// Conoscenza: per ogni cosa un peso da -100 (no) a +100 (sì) per ogni domanda. Parte da
// q20_seed.c (generato da tools/q20_gen.py) e si aggiorna giocando: a fine partita le
// risposte date spostano i pesi della cosa giusta, e le cose nuove si aggiungono. Tutto
// sta sulla microSD (q20/kb.txt, incluso nei backup); senza microSD si gioca ma non impara.
//
// Ogni risposta pesa, nessuna elimina: una risposta sbagliata (o un peso sbagliato) rende
// solo meno probabile la cosa giusta, che può risalire con le domande successive.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { const char *name; const char *ans; } q20_seed_t;   // ans: Y / N / ? per domanda
extern const int q20_seed_version, q20_nq, q20_seed_n;
extern const char *const q20_questions[];
extern const q20_seed_t q20_seed[];

// risposte
enum { Q20_NO = -2, Q20_PROB_NO = -1, Q20_DUNNO = 0, Q20_PROB_YES = 1, Q20_YES = 2 };

bool q20_init(void);                 // carica (una volta); false se manca la memoria
int  q20_count(void);                // cose conosciute
bool q20_can_learn(void);            // c'è la microSD
void q20_stats(int *games, int *wins);

void q20_new_game(void);
int  q20_asked(void);                // domande già fatte
int  q20_next_question(void);        // la prossima domanda, o -1 se è il momento di provare a indovinare
const char *q20_question(int q);
void q20_answer(int q, int a);
bool q20_undo(void);                 // toglie l'ultima risposta (e l'eventuale tentativo sbagliato dopo)
int  q20_guess(void);                // la cosa più probabile da proporre (-1 se non ce ne sono)
const char *q20_name(int i);
void q20_wrong(int i);               // non era quella
bool q20_over(void);                 // finite le domande: ha perso
void q20_win(int i);                 // era quella: impara e salva
int  q20_teach(const char *name);    // ha perso: era "name" (nuova o già nota). -1 se non valida

// registro delle cose imparate (quelle che non c'erano nella conoscenza iniziale)
int  q20_learned(int *out, int max); // indici, nell'ordine in cui le ha imparate
bool q20_forget(int i);              // la dimentica (e salva); interrompe la partita in corso
