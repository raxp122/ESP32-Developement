// morse.h — codice Morse: tabella, sequenza dei toni da suonare, decodifica dei tempi e
// rilevatore del tono nell'audio del microfono. Niente grafica né hardware: si prova sul PC
// (tools/morse/morse_test.c).
//
// Tempi (standard ITU): punto = 1 unità, linea = 3, pausa fra segni = 1, fra lettere = 3,
// fra parole = 7. Unità in ms = 1200 / parole al minuto. Con la spaziatura di Farnsworth i
// segni restano veloci e si allungano solo le pause fra lettere e parole (per imparare).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MORSE_KOCH "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X"   // ordine del metodo Koch

const char *morse_code(char c);            // ".-" per 'A' (maiuscole); NULL se non c'è
char morse_char(const char *code);         // il contrario; 0 se il codice non esiste
int  morse_count(void);                    // caratteri nella tabella
char morse_nth(int i);                     // i-esimo carattere della tabella

// Sequenza da suonare: durate in ms, positive = tono, negative = silenzio. Restituisce
// quante voci ha scritto. char_wpm: velocità dei segni; eff_wpm: velocità effettiva (pause).
int morse_timeline(const char *text, int char_wpm, int eff_wpm, int16_t *out, int max);

/* ---------------- decodifica dai tempi ---------------- */

typedef struct {
    float unit;          // durata stimata del punto (ms), si adatta a chi trasmette
    char cur[8];         // segni della lettera in corso
    int16_t dur[8];      // e le loro durate (ms): si classificano a lettera finita
    int n;
    char text[200];      // testo decodificato (gli ultimi caratteri)
    int len;
    bool word_done;      // la pausa di parola è già stata messa
    char last;           // ultimo carattere riconosciuto ('?' se il codice non esiste)
    bool got;            // c'è un carattere nuovo (si azzera leggendolo)
} morse_dec_t;

void md_init(morse_dec_t *d, int wpm);
void md_mark(morse_dec_t *d, int ms);       // un tono di ms è appena finito
void md_gap(morse_dec_t *d, int ms);        // silenzio da ms (chiamata anche mentre dura)
void md_clear(morse_dec_t *d);
static inline int md_wpm(const morse_dec_t *d) { return (int)(1200 / d->unit + 0.5f); }

/* ---------------- rilevatore del tono ---------------- */

// Cerca un tono fra 400 e 1200 Hz (filtri di Goertzel a passi di 50 Hz) a blocchi di 10 ms.
// Si aggancia alla frequenza più forte e decide acceso/spento con isteresi sul rapporto fra
// quella frequenza e le altre (il rumore di fondo è largo, un fischio è stretto).
#define MDET_BINS 17
typedef struct {
    int rate, block;
    float coef[MDET_BINS];
    float level;         // livello del tono (0..1, per l'indicatore)
    float snr;           // rapporto tono/rumore dell'ultimo blocco
    int freq;            // frequenza agganciata (Hz)
    bool on;
    int hold;            // blocchi di conferma prima di cambiare stato
    int lock;            // filtro agganciato durante il tono
    float peak;          // potenza massima del tono in corso
} morse_det_t;

void mdet_init(morse_det_t *t, int rate);
bool mdet_block(morse_det_t *t, const int16_t *mono, int n);   // n = t->block campioni; true = tono
