// morse.c — codice Morse (vedi morse.h)
#include "morse.h"
#include <math.h>
#include <string.h>

static const struct { char c; const char *m; } TABLE[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'F', "..-."}, {'G', "--."},
    {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."},
    {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"},
    {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"}, {'+', ".-.-."},
    {'-', "-....-"}, {'\'', ".----."}, {'!', "-.-.--"}, {'(', "-.--."}, {')', "-.--.-"}, {':', "---..."},
    {';', "-.-.-."}, {'"', ".-..-."}, {'@', ".--.-."}, {'&', ".-..."},
};
#define N_TABLE (int)(sizeof(TABLE) / sizeof(TABLE[0]))

int morse_count(void) { return N_TABLE; }
char morse_nth(int i) { return i >= 0 && i < N_TABLE ? TABLE[i].c : 0; }

const char *morse_code(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < N_TABLE; i++) if (TABLE[i].c == c) return TABLE[i].m;
    return NULL;
}

char morse_char(const char *code)
{
    for (int i = 0; i < N_TABLE; i++) if (!strcmp(TABLE[i].m, code)) return TABLE[i].c;
    return 0;
}

// le lettere accentate si mandano senza accento (À → A); il resto che non c'è si salta
static char plain(unsigned char c0, unsigned char c1, int *skip)
{
    *skip = 0;
    if (c0 == 0xC3 && c1) {
        *skip = 1;
        if ((c1 >= 0x80 && c1 <= 0x85) || (c1 >= 0xA0 && c1 <= 0xA5)) return 'A';
        if ((c1 >= 0x88 && c1 <= 0x8B) || (c1 >= 0xA8 && c1 <= 0xAB)) return 'E';
        if ((c1 >= 0x8C && c1 <= 0x8F) || (c1 >= 0xAC && c1 <= 0xAF)) return 'I';
        if ((c1 >= 0x92 && c1 <= 0x96) || (c1 >= 0xB2 && c1 <= 0xB6)) return 'O';
        if ((c1 >= 0x99 && c1 <= 0x9C) || (c1 >= 0xB9 && c1 <= 0xBC)) return 'U';
        return 0;
    }
    if (c0 >= 0x80) return 0;
    return (char)c0;
}

int morse_timeline(const char *text, int char_wpm, int eff_wpm, int16_t *out, int max)
{
    if (char_wpm < 5) char_wpm = 5;
    if (eff_wpm <= 0 || eff_wpm > char_wpm) eff_wpm = char_wpm;
    float u = 1200.0f / char_wpm, fu = u;
    if (eff_wpm < char_wpm) {
        // Farnsworth: il tempo in più si mette nelle pause fra lettere e parole (19 unità per "PARIS")
        float ta = (60.0f * char_wpm - 37.2f * eff_wpm) / (char_wpm * eff_wpm) * 1000.0f;
        fu = ta / 19.0f;
    }
    int n = 0;
    #define PUT(v) do { int _v = (int)(v); if (n && out[n - 1] < 0 && _v < 0) out[n - 1] += _v; else if (n < max) out[n++] = _v; } while (0)
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        int skip;
        char c = plain(s[0], s[1], &skip);
        s += skip;
        if (c == ' ' || c == '\n') { PUT(-7 * fu); continue; }
        const char *m = c ? morse_code(c) : NULL;
        if (!m) continue;
        for (const char *e = m; *e; e++) {
            PUT((*e == '-' ? 3 : 1) * u);
            if (e[1]) PUT(-u);
        }
        PUT(-3 * fu);
    }
    #undef PUT
    return n;
}

/* ---------------- decodifica ---------------- */

void md_init(morse_dec_t *d, int wpm)
{
    memset(d, 0, sizeof(*d));
    d->unit = 1200.0f / (wpm > 0 ? wpm : 12);
    d->word_done = true;
}

void md_clear(morse_dec_t *d)
{
    d->len = 0;
    d->text[0] = 0;
    d->n = 0;
    d->word_done = true;
}

static void put(morse_dec_t *d, char c)
{
    if (d->len >= (int)sizeof(d->text) - 1) {   // tiene gli ultimi caratteri
        memmove(d->text, d->text + 40, d->len - 40);
        d->len -= 40;
    }
    d->text[d->len++] = c;
    d->text[d->len] = 0;
}

static void clamp_unit(morse_dec_t *d)
{
    if (d->unit < 24) d->unit = 24;     // 50 parole al minuto
    if (d->unit > 300) d->unit = 300;   // 4 parole al minuto
}

// punti e linee della lettera: se ci sono segni corti e lunghi (uno almeno il doppio
// dell'altro) si separano fra loro, a prescindere dalla velocità; se sono tutti simili
// decide l'unità stimata finora
static void classify(morse_dec_t *d)
{
    int mn = 1 << 30, mx = 0;
    for (int i = 0; i < d->n; i++) { if (d->dur[i] < mn) mn = d->dur[i]; if (d->dur[i] > mx) mx = d->dur[i]; }
    bool mixed = mx >= 2 * mn;
    float mid = (mn + mx) / 2.0f;
    for (int i = 0; i < d->n; i++) d->cur[i] = (mixed ? d->dur[i] > mid : d->dur[i] > 2.0f * d->unit) ? '-' : '.';
    d->cur[d->n] = 0;
}

void md_mark(morse_dec_t *d, int ms)
{
    if (ms < 15) return;   // un "clic", non un segno
    if (d->n < 7) d->dur[d->n++] = (int16_t)(ms > 30000 ? 30000 : ms);
    // l'unità segue chi trasmette (le persone non tengono il ritmo perfetto)
    int mn = 1 << 30, mx = 0;
    for (int i = 0; i < d->n; i++) { if (d->dur[i] < mn) mn = d->dur[i]; if (d->dur[i] > mx) mx = d->dur[i]; }
    if (mx >= 2 * mn) {   // punti e linee nella stessa lettera: l'unità si misura direttamente
        float mid = (mn + mx) / 2.0f, s = 0;
        int k = 0;
        for (int i = 0; i < d->n; i++) if (d->dur[i] <= mid) { s += d->dur[i]; k++; }
        if (k) d->unit = d->unit * 0.5f + (s / k) * 0.5f;
    } else if (ms < 0.6f * d->unit) d->unit = d->unit * 0.6f + ms * 0.4f;            // più veloce: era un punto
    else if (ms > 4.5f * d->unit) d->unit = d->unit * 0.6f + ms / 3.0f * 0.4f;     // più lento: era una linea
    else d->unit = d->unit * 0.85f + (ms > 2.0f * d->unit ? ms / 3.0f : ms) * 0.15f;
    clamp_unit(d);
    d->word_done = false;
}

void md_gap(morse_dec_t *d, int ms)
{
    if (d->n && ms > 2.0f * d->unit) {   // fine della lettera
        classify(d);
        char c = morse_char(d->cur);
        d->last = c ? c : '?';
        d->got = true;
        put(d, d->last);
        d->n = 0;
    }
    if (!d->n && !d->word_done && ms > 5.0f * d->unit) {   // fine della parola
        if (d->len && d->text[d->len - 1] != ' ') put(d, ' ');
        d->word_done = true;
    }
}

/* ---------------- rilevatore del tono ---------------- */

void mdet_init(morse_det_t *t, int rate)
{
    memset(t, 0, sizeof(*t));
    t->rate = rate;
    t->block = rate / 100;   // 10 ms
    for (int k = 0; k < MDET_BINS; k++) t->coef[k] = 2.0f * cosf(2.0f * (float)M_PI * (400 + 50 * k) / rate);
    t->freq = 0;
}

bool mdet_block(morse_det_t *t, const int16_t *x, int n)
{
    float p[MDET_BINS];
    for (int k = 0; k < MDET_BINS; k++) {
        float s1 = 0, s2 = 0, c = t->coef[k];
        for (int i = 0; i < n; i++) {
            float s = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        p[k] = s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    // acceso: si guarda il filtro agganciato (e i vicini); spento: il più forte di tutti
    int km = 0;
    if (t->on) {
        km = t->lock;
        for (int k = t->lock - 1; k <= t->lock + 1; k++) if (k >= 0 && k < MDET_BINS && p[k] > p[km]) km = k;
    } else {
        for (int k = 1; k < MDET_BINS; k++) if (p[k] > p[km]) km = k;
    }
    float noise = 0;
    int nn = 0;
    for (int k = 0; k < MDET_BINS; k++) if (k < km - 2 || k > km + 2) { noise += p[k]; nn++; }
    noise = nn ? noise / nn : 1;
    float snr = p[km] / (noise + 1.0f);
    float amp = 2.0f * sqrtf(p[km]) / n;   // ampiezza del tono (unità del campione)
    t->snr = snr;
    t->level = amp / 6000.0f > 1 ? 1 : amp / 6000.0f;
    bool want;
    if (t->on) {
        if (p[km] > t->peak) t->peak = p[km];
        // il tono è finito quando cala di oltre 13 dB rispetto al suo massimo o si confonde col rumore
        want = p[km] > t->peak * 0.05f && snr > 4.0f;
    } else {
        // un tono è stretto: molto più forte delle altre frequenze (il rumore è largo)
        want = snr > 12.0f && amp > 40;
    }
    if (want != t->on) {
        if (++t->hold >= 2) {   // due blocchi uguali: niente sfarfallio
            t->on = want;
            t->hold = 0;
            if (want) { t->lock = km; t->peak = p[km]; }
        }
    } else t->hold = 0;
    if (t->on) t->freq = 400 + 50 * km;
    return t->on;
}
