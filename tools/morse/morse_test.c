// morse_test.c — prova del Morse sul PC: il testo diventa audio (tono con rumore, a varie
// velocità, frequenze e volumi), il rilevatore lo ascolta a blocchi di 10 ms e la decodifica
// deve restituire il testo. Prova anche la battuta "a mano" con tempi irregolari.
// gcc -O2 -Imain tools/morse/morse_test.c main/morse.c -lm -o /tmp/mt && /tmp/mt
#include "morse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 24000
static double gauss(void) { double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

static int run_audio(const char *msg, int wpm, int freq, double amp, double noise, char *out)
{
    static int16_t tl[4000];
    int n = morse_timeline(msg, wpm, wpm, tl, 4000);
    static int16_t pcm[RATE * 120];
    int len = RATE / 2, ph = 0;
    memset(pcm, 0, sizeof(pcm));
    for (int i = 0; i < len; i++) pcm[i] = (int16_t)(noise * gauss());
    double phase = 0;
    for (int k = 0; k < n; k++) {
        int ms = abs(tl[k]), ns = ms * RATE / 1000;
        for (int i = 0; i < ns && len < (int)(sizeof(pcm) / 2) - 1; i++, len++) {
            double env = 1;
            if (tl[k] > 0) { if (i < 120) env = i / 120.0; if (ns - i < 120) env = (ns - i) / 120.0; }
            double s = tl[k] > 0 ? amp * env * sin(phase) : 0;
            phase += 2 * M_PI * freq / RATE;
            pcm[len] = (int16_t)fmax(-32767, fmin(32767, s + noise * gauss()));
        }
    }
    for (int i = 0; i < RATE && len < (int)(sizeof(pcm) / 2); i++) pcm[len++] = (int16_t)(noise * gauss());
    (void)ph;
    morse_det_t t;
    morse_dec_t d;
    mdet_init(&t, RATE);
    md_init(&d, 12);   // il ricevitore non sa la velocità: parte da 12 e si adatta
    bool on = false;
    int run = 0;
    for (int i = 0; i + t.block <= len; i += t.block) {
        bool s = mdet_block(&t, pcm + i, t.block);
        if (s != on) {
            if (on) md_mark(&d, run * 10);
            on = s;
            run = 0;
        }
        run++;
        if (!on) md_gap(&d, run * 10);
    }
    strcpy(out, d.text);
    return t.freq;
}

int main(void)
{
    srand(3);
    const char *msg = "CIAO DAL GADGET 73 SOS";
    int bad = 0;
    static const struct { int wpm, freq; double amp, noise; } C[] = {
        {12, 700, 8000, 300}, {20, 600, 4000, 600}, {8, 900, 2000, 400}, {25, 750, 6000, 800}, {15, 1000, 1500, 500}, {12, 700, 600, 200},
    };
    for (int i = 0; i < 6; i++) {
        char out[256];
        int f = run_audio(msg, C[i].wpm, C[i].freq, C[i].amp, C[i].noise, out);
        while (strlen(out) && out[strlen(out) - 1] == ' ') out[strlen(out) - 1] = 0;
        int ok = !strcmp(out, msg);
        bad += !ok;
        printf("%2d wpm %4d Hz segnale %5.0f rumore %3.0f (SNR %4.1f dB): \"%s\" %s (freq vista %d)\n", C[i].wpm, C[i].freq, C[i].amp,
               C[i].noise, 20 * log10(C[i].amp / 1.414 / C[i].noise), out, ok ? "ok" : "SBAGLIATO", f);
    }
    // battuta a mano: tempi con +-25% di irregolarità
    {
        morse_dec_t d;
        md_init(&d, 15);
        const char *m2 = "PARIS PARIS";
        int16_t tl[400];
        int n = morse_timeline(m2, 14, 14, tl, 400);
        for (int k = 0; k < n; k++) {
            double j = 1 + 0.25 * (2.0 * rand() / RAND_MAX - 1);
            int ms = (int)(abs(tl[k]) * j);
            if (tl[k] > 0) md_mark(&d, ms);
            else for (int t = 10; t <= ms; t += 10) md_gap(&d, t);
        }
        md_gap(&d, 2000);
        int ok = !strncmp(d.text, m2, strlen(m2));
        bad += !ok;
        printf("a mano (14 wpm, +-25%%): \"%s\" %s, velocità stimata %d wpm\n", d.text, ok ? "ok" : "SBAGLIATO", md_wpm(&d));
    }
    int16_t tl[16];
    int n = morse_timeline("E", 20, 5, tl, 16);
    printf("Farnsworth 20/5: E = %d ms, pausa %d ms\n", tl[0], tl[1]);
    printf(bad ? "ERRORI: %d\n" : "tutto ok\n", bad);
    return bad;
}
