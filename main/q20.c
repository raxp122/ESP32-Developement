// q20.c — motore del Q-20 (vedi q20.h).
//
// Probabilità: ogni cosa ha un punteggio logaritmico; ogni risposta a lo aggiorna con
// log(L), L = 0,03 + exp(-2 (a - e)²), dove e = peso/100 è la risposta attesa (-1..1)
// e a la risposta data (-1, -0,5, 0,5, 1). Il pavimento 0,03 fa sì che un errore non
// elimini mai del tutto la cosa giusta.
// Domanda successiva: quella con la varianza più alta delle risposte attese, pesata con
// le probabilità attuali (divide meglio i candidati rimasti).
#include "q20.h"
#include "sd.h"
#include "textnorm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "q20";

#define MAXQ       112          // domande per cosa (spazio per quelle future)
#define MAXOBJ     900          // cose al massimo (il file resta sotto i 256 KB del backup)
#define NAMELEN    32
#define GAME_MAXQ  30           // domande al massimo in una partita
#define FIRST_GUESS 20          // poi ogni 5 domande in più dopo un tentativo sbagliato
#define SURE       0.92f        // abbastanza sicuro da tentare prima
#define MIN_EARLY  12           // ...ma non prima di questa domanda (ci pensa un po', come l'originale)
#define HIST       64

#define KB_DIR  SD_MOUNT "/q20"
#define KB_PATH KB_DIR "/kb.txt"
#define KB_TMP  KB_DIR "/kb.tmp"

typedef struct { char name[NAMELEN]; int8_t w[MAXQ]; } obj_t;

static obj_t *objs;
static float *logp, *pbuf;   // punteggi e probabilità normalizzate (PSRAM)
static uint8_t *rejected;
static int n_obj, games, wins;
static bool loaded;

// partita: risposte e tentativi sbagliati, nell'ordine (per poter annullare)
typedef struct { bool wrong; int16_t id; int8_t a; } step_t;
static step_t hist[HIST];
static int n_hist, n_asked, n_wrong;
static bool asked_q[MAXQ];

/* ---------------- conoscenza ---------------- */

static int8_t seed_w(char c) { return c == 'Y' ? 100 : c == 'N' ? -100 : 0; }

static int find(const char *name)
{
    char a[NAMELEN], b[NAMELEN];
    text_norm(name, a, sizeof(a));
    for (int i = 0; i < n_obj; i++) {
        text_norm(objs[i].name, b, sizeof(b));
        if (!strcmp(a, b)) return i;
    }
    return -1;
}

static const q20_seed_t *seed_of(const char *name)
{
    char a[NAMELEN], b[NAMELEN];
    text_norm(name, a, sizeof(a));
    for (int i = 0; i < q20_seed_n; i++) {
        text_norm(q20_seed[i].name, b, sizeof(b));
        if (!strcmp(a, b)) return &q20_seed[i];
    }
    return NULL;
}

static void add_seed(const q20_seed_t *s)
{
    if (n_obj >= MAXOBJ) return;
    obj_t *o = &objs[n_obj++];
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", s->name);
    for (int q = 0; q < q20_nq && q < MAXQ; q++) o->w[q] = seed_w(s->ans[q]);
}

static void load_seed(void)
{
    n_obj = 0;
    for (int i = 0; i < q20_seed_n; i++) add_seed(&q20_seed[i]);
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// legge il file: "Q20 1 <versione seme> <domande> <partite> <vinte>", poi "nome<TAB>pesi in hex"
static bool load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char *line = malloc(512);
    int ver = 0, sv = 0, nq = 0, g = 0, w = 0;
    bool ok = line && fgets(line, 512, f) && sscanf(line, "Q20 %d %d %d %d %d", &ver, &sv, &nq, &g, &w) == 5 &&
              ver == 1 && nq > 0 && nq <= MAXQ;
    if (ok) {
        n_obj = 0;
        while (n_obj < MAXOBJ && fgets(line, 512, f)) {
            char *tab = strchr(line, '\t');
            if (!tab || tab == line) continue;
            *tab = 0;
            obj_t *o = &objs[n_obj];
            memset(o, 0, sizeof(*o));
            snprintf(o->name, sizeof(o->name), "%s", line);
            const char *h = tab + 1;
            int q;
            for (q = 0; q < nq; q++) {
                int hi = hexv(h[2 * q]), lo = hi < 0 ? -1 : hexv(h[2 * q + 1]);
                if (hi < 0 || lo < 0) break;
                o->w[q] = (int8_t)(uint8_t)(hi * 16 + lo);
            }
            // domande arrivate con un firmware più nuovo: dal seme, se la cosa c'è
            const q20_seed_t *s = q < q20_nq ? seed_of(o->name) : NULL;
            for (; q < q20_nq && q < MAXQ; q++) o->w[q] = s ? seed_w(s->ans[q]) : 0;
            n_obj++;
        }
        games = g;
        wins = w;
        // cose nuove nel seme di un firmware più recente: si aggiungono, il resto non si tocca
        if (sv < q20_seed_version)
            for (int i = 0; i < q20_seed_n; i++)
                if (find(q20_seed[i].name) < 0) add_seed(&q20_seed[i]);
    }
    free(line);
    fclose(f);
    return ok && n_obj > 0;
}

static void save(void)
{
    if (!sd_ok()) return;
    mkdir(KB_DIR, 0777);
    FILE *f = fopen(KB_TMP, "w");
    if (!f) { ESP_LOGW(TAG, "non riesco a scrivere %s", KB_TMP); return; }
    fprintf(f, "Q20 1 %d %d %d %d\n", q20_seed_version, q20_nq, games, wins);
    char hex[2 * MAXQ + 1];
    for (int i = 0; i < n_obj; i++) {
        for (int q = 0; q < q20_nq; q++) snprintf(hex + 2 * q, 3, "%02x", (uint8_t)objs[i].w[q]);
        fprintf(f, "%s\t%s\n", objs[i].name, hex);
    }
    bool ok = fflush(f) == 0;
    fclose(f);
    if (!ok) return;
    // sostituzione: su FAT rename non sovrascrive. Se si spegne qui, al prossimo avvio
    // si riparte dal file temporaneo
    remove(KB_PATH);
    rename(KB_TMP, KB_PATH);
}

bool q20_init(void)
{
    if (loaded) return true;
    objs = heap_caps_malloc(sizeof(obj_t) * MAXOBJ, MALLOC_CAP_SPIRAM);
    logp = heap_caps_malloc(sizeof(float) * MAXOBJ, MALLOC_CAP_SPIRAM);
    pbuf = heap_caps_malloc(sizeof(float) * MAXOBJ, MALLOC_CAP_SPIRAM);
    rejected = heap_caps_malloc(MAXOBJ, MALLOC_CAP_SPIRAM);
    if (!objs || !logp || !pbuf || !rejected || q20_nq > MAXQ) {
        free(objs); free(logp); free(pbuf); free(rejected);
        objs = NULL; logp = pbuf = NULL; rejected = NULL;
        return false;
    }
    bool from_file = sd_ok() && (load_file(KB_PATH) || load_file(KB_TMP));
    if (!from_file) load_seed();
    if (!from_file || (sd_ok() && access(KB_PATH, F_OK) != 0)) save();
    ESP_LOGI(TAG, "%d cose, %d domande%s", n_obj, q20_nq, from_file ? " (dalla microSD)" : "");
    loaded = true;
    return true;
}

int  q20_count(void) { return n_obj; }
bool q20_can_learn(void) { return sd_ok(); }
void q20_stats(int *g, int *w) { *g = games; *w = wins; }
const char *q20_question(int q) { return q >= 0 && q < q20_nq ? q20_questions[q] : ""; }
const char *q20_name(int i) { return i >= 0 && i < n_obj ? objs[i].name : ""; }
int  q20_asked(void) { return n_asked; }

/* ---------------- partita ---------------- */

static float answer_value(int a) { return a * 0.5f; }   // -2..2 → -1..1

static void apply(int q, int a)
{
    float av = answer_value(a);
    for (int i = 0; i < n_obj; i++) {
        float d = av - objs[i].w[q] / 100.0f;
        logp[i] += logf(0.03f + expf(-2.0f * d * d));
    }
}

static void recompute(void)
{
    memset(logp, 0, sizeof(float) * n_obj);
    memset(rejected, 0, n_obj);
    memset(asked_q, 0, sizeof(asked_q));
    n_asked = n_wrong = 0;
    for (int k = 0; k < n_hist; k++) {
        if (hist[k].wrong) { rejected[hist[k].id] = 1; n_wrong++; continue; }
        asked_q[hist[k].id] = true;
        n_asked++;
        if (hist[k].a) apply(hist[k].id, hist[k].a);
    }
}

void q20_new_game(void)
{
    n_hist = 0;
    recompute();
}

// probabilità normalizzate (in logp non si tocca: si scrivono in out)
static float probs(float *out, int *best)
{
    float mx = -1e30f;
    for (int i = 0; i < n_obj; i++) if (!rejected[i] && logp[i] > mx) mx = logp[i];
    float sum = 0;
    for (int i = 0; i < n_obj; i++) { out[i] = rejected[i] ? 0 : expf(logp[i] - mx); sum += out[i]; }
    float top = 0;
    *best = -1;
    for (int i = 0; i < n_obj; i++) {
        out[i] = sum > 0 ? out[i] / sum : 0;
        if (out[i] > top) { top = out[i]; *best = i; }
    }
    return top;
}

int q20_next_question(void)
{
    float *p = pbuf;
    int best;
    float top = probs(p, &best);
    if (best < 0) return -1;
    if (n_asked >= FIRST_GUESS + 5 * n_wrong) return -1;
    if (n_asked >= MIN_EARLY && top > SURE) return -1;
    if (n_asked >= GAME_MAXQ) return -1;
    int bq = -1;
    float bv = 0;
    for (int q = 0; q < q20_nq; q++) {
        if (asked_q[q]) continue;
        float m = 0, m2 = 0;
        for (int i = 0; i < n_obj; i++) {
            if (p[i] < 1e-6f) continue;
            float e = objs[i].w[q] / 100.0f;
            m += p[i] * e;
            m2 += p[i] * e * e;
        }
        float v = m2 - m * m;
        // un pizzico di caso: partite diverse anche pensando la stessa cosa
        v *= 1.0f + (esp_random() % 1000) / 8000.0f;
        if (v > bv) { bv = v; bq = q; }
    }
    return bv < 0.004f ? -1 : bq;   // nessuna domanda divide più: si tenta
}

void q20_answer(int q, int a)
{
    if (q < 0 || q >= q20_nq || n_hist >= HIST) return;
    hist[n_hist++] = (step_t){.wrong = false, .id = (int16_t)q, .a = (int8_t)a};
    asked_q[q] = true;
    n_asked++;
    if (a) apply(q, a);
}

bool q20_undo(void)
{
    // via i tentativi sbagliati in coda, poi l'ultima risposta
    while (n_hist > 0 && hist[n_hist - 1].wrong) n_hist--;
    if (n_hist == 0) return false;
    n_hist--;
    recompute();
    return true;
}

int q20_guess(void)
{
    int best;
    probs(pbuf, &best);
    return best;
}

void q20_wrong(int i)
{
    if (i < 0 || i >= n_obj || n_hist >= HIST) return;
    hist[n_hist++] = (step_t){.wrong = true, .id = (int16_t)i};
    rejected[i] = 1;
    n_wrong++;
}

bool q20_over(void)
{
    if (n_asked >= GAME_MAXQ) return true;
    for (int i = 0; i < n_obj; i++) if (!rejected[i]) return false;
    return true;
}

// sposta i pesi della cosa i verso le risposte di questa partita
static void learn(int i, float rate)
{
    for (int k = 0; k < n_hist; k++) {
        if (hist[k].wrong || !hist[k].a) continue;
        int q = hist[k].id;
        float target = hist[k].a * 50.0f;   // -100..100
        float w = objs[i].w[q] + (target - objs[i].w[q]) * rate;
        objs[i].w[q] = (int8_t)(w > 100 ? 100 : w < -100 ? -100 : lroundf(w));
    }
}

void q20_win(int i)
{
    if (i < 0 || i >= n_obj) return;
    learn(i, 0.3f);
    games++;
    wins++;
    save();
}

int q20_teach(const char *name)
{
    char clean[NAMELEN];
    int o = 0;
    for (const char *s = name; *s && o < NAMELEN - 1; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\r') continue;
        if (o == 0 && *s == ' ') continue;
        clean[o++] = *s;
    }
    while (o > 0 && clean[o - 1] == ' ') o--;
    clean[o] = 0;
    games++;
    if (!clean[0]) { save(); return -1; }
    int i = find(clean);
    if (i >= 0) {
        learn(i, 0.5f);   // la conosceva ma l'ha sbagliata: si corregge di più
    } else if (n_obj < MAXOBJ) {
        i = n_obj++;
        memset(&objs[i], 0, sizeof(obj_t));
        snprintf(objs[i].name, sizeof(objs[i].name), "%s", clean);
        learn(i, 0.8f);   // cosa nuova: le risposte di questa partita sono tutto ciò che sa
    }
    save();
    return i;
}
