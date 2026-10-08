// chess_game.c — partite, analisi e archivio degli Scacchi (vedi chess_game.h)
#include "chess_game.h"
#include "sd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "nvs.h"

#define DIR       SD_MOUNT "/scacchi"
#define GAMES_DIR DIR "/partite"
#define INDEX     DIR "/indice.csv"
#define ELO_FILE  DIR "/elo.csv"
#define PUZZLES   DIR "/esercizi.csv"
#define RESUME    DIR "/incorso.txt"
#define EXPORT    DIR "/per_IA.txt"

cg_cfg_t cg_cfg;

/* ================= impostazioni (NVS) ================= */

#define CFG_MAGIC 0x43480001u
typedef struct { uint32_t magic; cg_cfg_t c; } cfg_blob_t;

void cg_cfg_load(void)
{
    cg_cfg = (cg_cfg_t){.bot_elo = 1200, .adaptive = 0, .color = 0, .notation_it = 1, .flip_two = 0,
                        .show_moves = 1, .elo = 1200, .rated_games = 0, .next_id = 1};
    nvs_handle_t h;
    if (nvs_open("chess", NVS_READONLY, &h) != ESP_OK) return;
    cfg_blob_t b;
    size_t len = sizeof(b);
    if (nvs_get_blob(h, "cfg", &b, &len) == ESP_OK && len == sizeof(b) && b.magic == CFG_MAGIC) cg_cfg = b.c;
    nvs_close(h);
}

void cg_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("chess", NVS_READWRITE, &h) != ESP_OK) return;
    cfg_blob_t b = {CFG_MAGIC, cg_cfg};
    nvs_set_blob(h, "cfg", &b, sizeof(b));
    nvs_commit(h);
    nvs_close(h);
}

/* ================= partita ================= */

void cg_new(cgame_t *g, int mode, int color, int bot_elo)
{
    memset(&g->info, 0, sizeof(g->info));
    g->info.mode = mode;
    g->info.color = color;
    g->info.bot_elo = bot_elo;
    g->info.acc[0] = g->info.acc[1] = -1;
    g->n = 0;
    g->analyzed = false;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year + 1900 >= 2024) strftime(g->info.date, sizeof(g->info.date), "%Y.%m.%d %H:%M", &tm);
    else strcpy(g->info.date, "????.??.?? --:--");
}

void cg_position(const cgame_t *g, int k, cpos_t *p, uint64_t *hist)
{
    cp_start(p);
    for (int i = 0; i < k && i < g->n; i++) {
        if (hist) hist[i] = p->hash;
        cp_make(p, g->mv[i]);
    }
}

void cg_san(const cgame_t *g, int k, char *out)
{
    cpos_t p;
    cg_position(g, k, &p, NULL);
    cp_san(&p, g->mv[k], out);
}

void cg_san_show(const cgame_t *g, int k, char *out)
{
    char s[16];
    cg_san(g, k, s);
    if (cg_cfg.notation_it) cp_san_it(s, out);
    else strcpy(out, s);
}

const char *cg_result_str(int r)
{
    return r == CG_RES_WHITE ? "1-0" : r == CG_RES_BLACK ? "0-1" : r == CG_RES_DRAW ? "1/2-1/2" : "*";
}

const char *cg_end_str(int e)
{
    static const char *const S[] = {"in corso", "scacco matto", "stallo", "regola delle 50 mosse", "ripetizione",
                                    "materiale insufficiente", "abbandono", "patta d'accordo"};
    return e < 8 ? S[e] : "";
}

const char *cg_cls_name(int c)
{
    static const char *const S[] = {"", "Migliore", "Buona", "Imprecisione", "Errore", "Grave errore"};
    return c < 6 ? S[c] : "";
}

/* ================= PGN ================= */

static void fmt_eval(char *b, int v)
{
    if (v >= 9000) strcpy(b, "#1");
    else if (v <= -9000) strcpy(b, "#-1");
    else sprintf(b, "%.2f", v / 100.0);
}

static const char *player_name(const cgame_t *g, int side, char *buf)
{
    if (g->info.mode == CG_MODE_TWO) return side ? "Nero" : "Bianco";
    if (side == g->info.color) return "Tu";
    sprintf(buf, "Gadget %d", g->info.bot_elo);
    return buf;
}

static void write_pgn(FILE *f, const cgame_t *g)
{
    char date[11] = "????.??.??", tm[6] = "";
    memcpy(date, g->info.date, 10);
    if (strlen(g->info.date) >= 16) memcpy(tm, g->info.date + 11, 5);
    char b1[24], b2[24];
    fprintf(f, "[Event \"%s\"]\n[Site \"Gadget\"]\n[Date \"%s\"]\n[Time \"%s\"]\n[Round \"%d\"]\n",
            g->info.mode == CG_MODE_BOT ? "Partita contro il Gadget" : "Partita a due", date, tm, g->info.id);
    fprintf(f, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n", player_name(g, 0, b1), player_name(g, 1, b2),
            cg_result_str(g->info.result));
    if (g->info.mode == CG_MODE_BOT) {
        int me = g->info.elo_before ? g->info.elo_before : cg_cfg.elo;
        fprintf(f, "[WhiteElo \"%d\"]\n[BlackElo \"%d\"]\n", g->info.color ? g->info.bot_elo : me, g->info.color ? me : g->info.bot_elo);
    }
    if (g->info.end) fprintf(f, "[Termination \"%s\"]\n", cg_end_str(g->info.end));
    fprintf(f, "[PlyCount \"%d\"]\n", g->n);
    fprintf(f, "[GadgetMode \"%s\"]\n[GadgetColor \"%d\"]\n[GadgetBot \"%d\"]\n[GadgetEnd \"%d\"]\n",
            g->info.mode == CG_MODE_BOT ? "bot" : "due", g->info.color, g->info.bot_elo, g->info.end);
    fprintf(f, "[GadgetElo \"%d %d\"]\n[GadgetRated \"%d\"]\n[GadgetAids \"%d %d\"]\n[GadgetAcc \"%d %d\"]\n\n",
            g->info.elo_before, g->info.elo_after, g->info.rated, g->info.hints, g->info.undos, g->info.acc[0], g->info.acc[1]);
    cpos_t p;
    cp_start(&p);
    int col = 0;
    char line[160] = "", tok[96];
    bool after_comment = false;
    #define EMIT(s) do { const char *_s = (s); if (col + (int)strlen(_s) + 1 > 79) { fprintf(f, "%s\n", line); line[0] = 0; col = 0; } \
                         if (col) { strcat(line, " "); col++; } strcat(line, _s); col += strlen(_s); } while (0)
    if (g->analyzed) { char e[12]; fmt_eval(e, g->eval[0]); sprintf(tok, "{[%%eval %s]}", e); EMIT(tok); after_comment = true; }
    for (int k = 0; k < g->n; k++) {
        char san[16];
        cp_san(&p, g->mv[k], san);
        if (p.side == 0) { sprintf(tok, "%d.", p.fullmove); EMIT(tok); }
        else if (k == 0 || after_comment) { sprintf(tok, "%d...", p.fullmove); EMIT(tok); }
        EMIT(san);
        after_comment = false;
        if (g->analyzed) {
            int c = g->cls[k];
            if (c == CG_CLS_INACC) EMIT("$6");
            else if (c == CG_CLS_MISTAKE) EMIT("$2");
            else if (c == CG_CLS_BLUNDER) EMIT("$4");
            char e[12];
            fmt_eval(e, g->eval[k + 1]);
            if (c >= CG_CLS_INACC && !cm_null(g->best[k])) {
                char bs[16];
                cp_san(&p, g->best[k], bs);
                sprintf(tok, "{[%%eval %s] %s, meglio %s}", e, cg_cls_name(c), bs);
            } else sprintf(tok, "{[%%eval %s]}", e);
            // i commenti lunghi vanno a capo per conto loro
            if (col + (int)strlen(tok) + 1 > 79) { fprintf(f, "%s\n", line); line[0] = 0; col = 0; }
            EMIT(tok);
            after_comment = true;
        }
        cp_make(&p, g->mv[k]);
    }
    EMIT(cg_result_str(g->info.result));
    fprintf(f, "%s\n\n", line);
    #undef EMIT
}

static bool header(const char *buf, const char *key, char *out, int n)
{
    char k[32];
    snprintf(k, sizeof(k), "[%s \"", key);
    const char *s = strstr(buf, k);
    if (!s) return false;
    s += strlen(k);
    int i = 0;
    while (*s && *s != '"' && i < n - 1) out[i++] = *s++;
    out[i] = 0;
    return true;
}

static int eval_from(const char *s)
{
    if (*s == '#') return s[1] == '-' ? -9999 : 9999;
    return (int)lround(atof(s) * 100);
}

static bool parse_pgn(const char *buf, cgame_t *g)
{
    char v[64];
    memset(&g->info, 0, sizeof(g->info));
    g->info.acc[0] = g->info.acc[1] = -1;
    g->n = 0;
    g->analyzed = false;
    char d[12] = "", t[8] = "";
    header(buf, "Date", d, sizeof(d));
    header(buf, "Time", t, sizeof(t));
    snprintf(g->info.date, sizeof(g->info.date), "%.10s %.5s", d, t[0] ? t : "--:--");
    if (header(buf, "Round", v, sizeof(v))) g->info.id = atoi(v);
    if (header(buf, "Result", v, sizeof(v)))
        g->info.result = !strcmp(v, "1-0") ? CG_RES_WHITE : !strcmp(v, "0-1") ? CG_RES_BLACK : !strcmp(v, "1/2-1/2") ? CG_RES_DRAW : CG_RES_NONE;
    if (header(buf, "GadgetMode", v, sizeof(v))) g->info.mode = strcmp(v, "bot") ? CG_MODE_TWO : CG_MODE_BOT;
    if (header(buf, "GadgetColor", v, sizeof(v))) g->info.color = atoi(v);
    if (header(buf, "GadgetBot", v, sizeof(v))) g->info.bot_elo = atoi(v);
    if (header(buf, "GadgetEnd", v, sizeof(v))) g->info.end = atoi(v);
    if (header(buf, "GadgetRated", v, sizeof(v))) g->info.rated = atoi(v);
    int a, b;
    if (header(buf, "GadgetElo", v, sizeof(v)) && sscanf(v, "%d %d", &a, &b) == 2) { g->info.elo_before = a; g->info.elo_after = b; }
    if (header(buf, "GadgetAids", v, sizeof(v)) && sscanf(v, "%d %d", &a, &b) == 2) { g->info.hints = a; g->info.undos = b; }
    if (header(buf, "GadgetAcc", v, sizeof(v)) && sscanf(v, "%d %d", &a, &b) == 2) { g->info.acc[0] = a; g->info.acc[1] = b; }
    // mosse: dopo l'ultima intestazione
    const char *s = buf, *last = NULL;
    while ((s = strstr(s, "\n[")) != NULL) last = ++s;
    s = last ? strchr(last, '\n') : buf;
    if (!s) return false;
    cpos_t p, prev;
    cp_start(&p);
    prev = p;
    int k = 0;   // semimosse lette: i commenti si riferiscono alla posizione k
    while (*s) {
        if (*s == '{') {
            const char *e = strchr(s, '}');
            if (!e) break;
            const char *ev = strstr(s, "[%eval ");
            if (ev && ev < e) { g->eval[k] = eval_from(ev + 7); g->analyzed = true; }
            const char *bm = strstr(s, "meglio ");
            if (bm && bm < e && k > 0) {
                char san[16];
                int i = 0;
                bm += 7;
                while (bm < e && *bm != ' ' && *bm != '}' && i < 15) san[i++] = *bm++;
                san[i] = 0;
                cmove_t m;
                if (cp_parse_san(&prev, san, &m)) g->best[k - 1] = m;   // dalla posizione prima della mossa
            }
            s = e + 1;
            continue;
        }
        if (*s == '$') {
            int nag = atoi(s + 1);
            if (k > 0) g->cls[k - 1] = nag == 6 ? CG_CLS_INACC : nag == 2 ? CG_CLS_MISTAKE : nag == 4 ? CG_CLS_BLUNDER : g->cls[k - 1];
            while (*s && *s != ' ' && *s != '\n') s++;
            continue;
        }
        if (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') { s++; continue; }
        char tok[24];
        int i = 0;
        while (*s && *s != ' ' && *s != '\n' && *s != '\r' && *s != '{' && i < 23) tok[i++] = *s++;
        tok[i] = 0;
        if (!strcmp(tok, "1-0") || !strcmp(tok, "0-1") || !strcmp(tok, "1/2-1/2") || !strcmp(tok, "*")) break;
        char *dot = strrchr(tok, '.');
        const char *mv = dot ? dot + 1 : tok;   // "12.e4" oppure "12."
        if (!*mv || (mv[0] >= '0' && mv[0] <= '9' && !dot)) continue;
        cmove_t m;
        if (k >= CG_MAXPLY || !cp_parse_san(&p, mv, &m)) break;
        g->mv[k] = m;
        g->cls[k] = CG_CLS_GOOD;
        g->best[k] = CM_NONE;
        prev = p;
        cp_make(&p, m);
        k++;
    }
    g->n = k;
    g->info.plies = k;
    return true;
}

/* ================= archivio ================= */

bool cg_store_ok(void) { return sd_ok(); }

static void dirs(void)
{
    mkdir(DIR, 0775);
    mkdir(GAMES_DIR, 0775);
}

static char *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (b) { n = fread(b, 1, n, f); b[n] = 0; }
    fclose(f);
    if (len) *len = n;
    return b;
}

static void index_line(char *b, int n, const cg_info_t *i)
{
    snprintf(b, n, "%d;%s;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d\n", i->id, i->date, i->mode, i->color, i->bot_elo,
             i->result, i->end, i->plies, i->elo_before, i->elo_after, i->acc[0], i->acc[1], i->hints, i->undos, i->rated);
}

static bool parse_index(const char *l, cg_info_t *i)
{
    int v[15];
    char date[20];
    memset(i, 0, sizeof(*i));
    if (sscanf(l, "%d;%19[^;];%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d", &v[0], date, &v[2], &v[3], &v[4], &v[5], &v[6],
               &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14]) != 15) return false;
    i->id = v[0];
    strcpy(i->date, date);
    i->mode = v[2]; i->color = v[3]; i->bot_elo = v[4]; i->result = v[5]; i->end = v[6]; i->plies = v[7];
    i->elo_before = v[8]; i->elo_after = v[9]; i->acc[0] = v[10]; i->acc[1] = v[11]; i->hints = v[12]; i->undos = v[13];
    i->rated = v[14];
    return true;
}

// riscrive l'indice togliendo la riga di id e, se add, aggiungendo quella nuova
static bool index_update(int id, const cg_info_t *add)
{
    long n = 0;
    char *old = slurp(INDEX, &n);
    FILE *f = fopen(INDEX ".tmp", "w");
    if (!f) { free(old); return false; }
    for (char *l = old; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        if (atoi(l) != id && *l) fprintf(f, "%s\n", l);
        if (!e) break;
        l = e + 1;
    }
    free(old);
    if (add) { char b[160]; index_line(b, sizeof(b), add); fputs(b, f); }
    bool ok = !ferror(f);
    fclose(f);
    remove(INDEX);
    return ok && rename(INDEX ".tmp", INDEX) == 0;
}

bool cg_save(cgame_t *g)
{
    if (!sd_ok()) return false;
    dirs();
    if (!g->info.id) {
        // il prossimo numero libero (anche se l'NVS è stata cancellata)
        int id = cg_cfg.next_id ? cg_cfg.next_id : 1;
        char path[64];
        struct stat st;
        for (;; id++) { snprintf(path, sizeof(path), GAMES_DIR "/%04d.pgn", id); if (stat(path, &st)) break; }
        g->info.id = id;
        cg_cfg.next_id = id + 1;
        cg_cfg_save();
    }
    g->info.plies = g->n;
    char path[64];
    snprintf(path, sizeof(path), GAMES_DIR "/%04d.pgn", g->info.id);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    write_pgn(f, g);
    bool ok = !ferror(f);
    fclose(f);
    return ok && index_update(g->info.id, &g->info);
}

bool cg_load(int id, cgame_t *g)
{
    char path[64];
    snprintf(path, sizeof(path), GAMES_DIR "/%04d.pgn", id);
    char *b = slurp(path, NULL);
    if (!b) return false;
    bool ok = parse_pgn(b, g);
    free(b);
    if (ok && !g->info.id) g->info.id = id;
    return ok;
}

bool cg_delete(int id)
{
    char path[64];
    snprintf(path, sizeof(path), GAMES_DIR "/%04d.pgn", id);
    remove(path);
    return index_update(id, NULL);
}

int cg_list(cg_info_t *out, int max)
{
    char *b = slurp(INDEX, NULL);
    if (!b) return 0;
    int n = 0, total = 0;
    // si contano prima tutte le righe: si tengono le ultime max, dalla più recente
    for (char *l = b; *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : l + strlen(l)) total++;
    int skip = total > max ? total - max : 0, k = 0;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        cg_info_t i;
        if (k++ >= skip && parse_index(l, &i) && n < max) out[n++] = i;
        if (!e) break;
        l = e + 1;
    }
    free(b);
    for (int i = 0; i < n / 2; i++) { cg_info_t t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
    return n;
}

/* ================= Elo ================= */

int cg_elo_update(cgame_t *g, float score)
{
    int me = cg_cfg.elo;
    double exp_ = 1.0 / (1.0 + pow(10.0, (g->info.bot_elo - me) / 400.0));
    int k = cg_cfg.rated_games < 20 ? 40 : 20;   // le prime partite contano di più: l'Elo si assesta prima
    int nu = (int)lround(me + k * (score - exp_));
    if (nu < 100) nu = 100;
    g->info.elo_before = me;
    g->info.elo_after = nu;
    cg_cfg.elo = nu;
    cg_cfg.rated_games++;
    cg_cfg_save();
    if (sd_ok()) {
        dirs();
        FILE *f = fopen(ELO_FILE, "a");
        if (f) {
            fprintf(f, "%s;%d;%d;%.1f;%d;%d\n", g->info.date, g->info.id, g->info.bot_elo, score, me, nu);
            fclose(f);
        }
    }
    return nu;
}

int cg_elo_history(int16_t *out, int max)
{
    char *b = slurp(ELO_FILE, NULL);
    int n = 0, total = 0;
    if (!b) { if (max > 0) { out[0] = cg_cfg.elo; return 1; } return 0; }
    for (char *l = b; *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : l + strlen(l)) total++;
    int skip = total > max ? total - max : 0, k = 0;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        // data;id;bot;punti;prima;dopo
        const char *c = l;
        for (int s = 0; s < 5 && c; s++) { c = strchr(c, ';'); if (c) c++; }
        if (c && k++ >= skip && n < max) out[n++] = atoi(c);
        if (!e) break;
        l = e + 1;
    }
    free(b);
    return n;
}

void cg_stats(int *won, int *drawn, int *lost)
{
    *won = *drawn = *lost = 0;
    char *b = slurp(INDEX, NULL);
    if (!b) return;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        cg_info_t i;
        if (parse_index(l, &i) && i.mode == CG_MODE_BOT && i.result) {
            int mine = i.color ? CG_RES_BLACK : CG_RES_WHITE;
            if (i.result == CG_RES_DRAW) (*drawn)++;
            else if (i.result == mine) (*won)++;
            else (*lost)++;
        }
        if (!e) break;
        l = e + 1;
    }
    free(b);
}

/* ================= partita in corso ================= */

bool cg_resume_save(const cgame_t *g)
{
    if (!sd_ok()) return false;
    dirs();
    FILE *f = fopen(RESUME, "w");
    if (!f) return false;
    fprintf(f, "%d %d %d %d %d %s\n", g->info.mode, g->info.color, g->info.bot_elo, g->info.hints, g->info.undos, g->info.date);
    char u[8];
    for (int k = 0; k < g->n; k++) { cp_uci(g->mv[k], u); fprintf(f, "%s ", u); }
    fputc('\n', f);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool cg_resume_exists(void)
{
    struct stat st;
    return sd_ok() && stat(RESUME, &st) == 0;
}

bool cg_resume_load(cgame_t *g)
{
    char *b = slurp(RESUME, NULL);
    if (!b) return false;
    int mode, color, bot, hints, undos;
    char date[20] = "";
    bool ok = sscanf(b, "%d %d %d %d %d %19[^\n]", &mode, &color, &bot, &hints, &undos, date) >= 5;
    if (ok) {
        cg_new(g, mode, color, bot);
        if (date[0]) strcpy(g->info.date, date);
        g->info.hints = hints;
        g->info.undos = undos;
        char *s = strchr(b, '\n');
        cpos_t p;
        cp_start(&p);
        while (s && *s) {
            while (*s == ' ' || *s == '\n') s++;
            if (!*s) break;
            cmove_t m;
            if (g->n >= CG_MAXPLY || !cp_parse_uci(&p, s, &m)) break;
            g->mv[g->n++] = m;
            cp_make(&p, m);
            while (*s && *s != ' ' && *s != '\n') s++;
        }
    }
    free(b);
    return ok;
}

void cg_resume_clear(void) { remove(RESUME); }

/* ================= analisi ================= */

int cg_win_pct(int cp)
{
    if (cp > 3000) cp = 3000;
    if (cp < -3000) cp = -3000;
    return (int)lround(50 + 50 * (2 / (1 + exp(-0.00368208 * cp)) - 1));
}

static double win_d(int cp)
{
    if (cp > 3000) cp = 3000;
    if (cp < -3000) cp = -3000;
    return 50 + 50 * (2 / (1 + exp(-0.00368208 * cp)) - 1);
}

// esercizi: righe "fen;best;played;id;ply;ok;ko"
static bool puzzle_parse(char *l, cg_puzzle_t *p)
{
    char fen[92], b[8], pl[8];
    int id, ply, ok, ko;
    if (sscanf(l, "%91[^;];%7[^;];%7[^;];%d;%d;%d;%d", fen, b, pl, &id, &ply, &ok, &ko) != 7) return false;
    cpos_t q;
    if (!cp_from_fen(&q, fen) || !cp_parse_uci(&q, b, &p->best)) return false;
    if (!cp_parse_uci(&q, pl, &p->played)) p->played = CM_NONE;
    strcpy(p->fen, fen);
    p->game_id = id; p->ply = ply; p->ok = ok; p->ko = ko;
    return true;
}

static void puzzle_line(char *o, int n, const cg_puzzle_t *p)
{
    char b[8], pl[8] = "-";
    cp_uci(p->best, b);
    if (!cm_null(p->played)) cp_uci(p->played, pl);
    snprintf(o, n, "%s;%s;%s;%d;%d;%d;%d\n", p->fen, b, pl, p->game_id, p->ply, p->ok, p->ko);
}

static void puzzles_add_from(const cgame_t *g)
{
    if (!sd_ok() || !g->info.id) return;
    char *old = slurp(PUZZLES, NULL);
    FILE *f = fopen(PUZZLES, "a");
    if (!f) { free(old); return; }
    cpos_t p;
    cp_start(&p);
    for (int k = 0; k < g->n; k++) {
        bool mine = g->info.mode == CG_MODE_TWO || (k & 1) == g->info.color;
        if (mine && g->cls[k] >= CG_CLS_MISTAKE && !cm_null(g->best[k])) {
            cg_puzzle_t z = {.best = g->best[k], .played = g->mv[k], .game_id = g->info.id, .ply = k};
            cp_to_fen(&p, z.fen);
            if (!old || !strstr(old, z.fen)) {   // la stessa posizione una volta sola
                char l[160];
                puzzle_line(l, sizeof(l), &z);
                fputs(l, f);
            }
        }
        cp_make(&p, g->mv[k]);
    }
    fclose(f);
    free(old);
}

bool cg_analyze(cgame_t *g, int ms, cg_progress_t progress)
{
    uint64_t *hist = malloc(sizeof(uint64_t) * (CG_MAXPLY + 1));
    if (!hist) return false;
    cpos_t p;
    cp_start(&p);
    ce_clear();
    for (int k = 0; k <= g->n; k++) {
        if (progress && !progress(k, g->n + 1)) { free(hist); return false; }
        cmove_t l[CP_MAXMOVES];
        int ev;
        if (!cp_legal(&p, l)) {
            ev = cp_in_check(&p) ? (p.side ? 9999 : -9999) : 0;
            if (k < g->n) g->best[k] = CM_NONE;
        } else {
            cresult_t r;
            ce_search(&p, hist, k, 30, ms, &r);
            if (ce_abort) { free(hist); return false; }
            int s = r.score;
            if (s > CE_MATE - 200) s = 9999;
            else if (s < -CE_MATE + 200) s = -9999;
            else if (s > 3000) s = 3000;
            else if (s < -3000) s = -3000;
            ev = p.side ? -s : s;
            if (k < g->n) g->best[k] = r.best;
        }
        g->eval[k] = ev;
        if (k < g->n) { hist[k] = p.hash; cp_make(&p, g->mv[k]); }
    }
    // giudizi e precisione (come i siti di scacchi: dalla probabilità di vittoria persa)
    double acc_sum[2] = {0, 0};
    int acc_n[2] = {0, 0};
    for (int k = 0; k < g->n; k++) {
        int side = k & 1, sg = side ? -1 : 1;
        double before = win_d(g->eval[k] * sg), after = win_d(g->eval[k + 1] * sg);
        double drop = before - after;
        if (drop < 0) drop = 0;
        int c;
        if (cm_eq(g->mv[k], g->best[k])) c = CG_CLS_BEST;
        else if (drop >= 30) c = CG_CLS_BLUNDER;
        else if (drop >= 20) c = CG_CLS_MISTAKE;
        else if (drop >= 10) c = CG_CLS_INACC;
        else c = CG_CLS_GOOD;
        g->cls[k] = c;
        double a = 103.1668 * exp(-0.04354 * drop) - 3.1669;
        acc_sum[side] += a < 0 ? 0 : a > 100 ? 100 : a;
        acc_n[side]++;
    }
    free(hist);
    for (int s = 0; s < 2; s++) g->info.acc[s] = acc_n[s] ? (int8_t)lround(acc_sum[s] / acc_n[s]) : -1;
    g->analyzed = true;
    if (progress) progress(g->n + 1, g->n + 1);
    puzzles_add_from(g);
    return true;
}

/* ================= allenamento ================= */

int cg_puzzle_count(int *todo)
{
    char *b = slurp(PUZZLES, NULL);
    int n = 0, t = 0;
    for (char *l = b; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        cg_puzzle_t p;
        if (puzzle_parse(l, &p)) { n++; if (!p.ok) t++; }
        if (!e) break;
        l = e + 1;
    }
    free(b);
    if (todo) *todo = t;
    return n;
}

static uint32_t pick_seed = 1;

bool cg_puzzle_pick(cg_puzzle_t *out, int *index)
{
    char *b = slurp(PUZZLES, NULL);
    if (!b) return false;
    // si sceglie a caso, pesando di più quelli mai risolti o sbagliati più volte
    int idx = -1, i = 0;
    double best = -1;
    pick_seed = pick_seed * 1664525u + 1013904223u + (uint32_t)time(NULL);
    uint32_t s = pick_seed;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        cg_puzzle_t p;
        if (puzzle_parse(l, &p)) {
            s = s * 1664525u + 1013904223u;
            double w = (1.0 + p.ko * 2) / (1.0 + p.ok * 3) * ((s >> 8) % 1000 + 1);
            if (w > best) { best = w; idx = i; *out = p; }
        }
        i++;
        if (!e) break;
        l = e + 1;
    }
    free(b);
    if (index) *index = idx;
    return idx >= 0;
}

void cg_puzzle_result(int index, bool ok)
{
    char *b = slurp(PUZZLES, NULL);
    if (!b) return;
    FILE *f = fopen(PUZZLES ".tmp", "w");
    if (!f) { free(b); return; }
    int i = 0;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        cg_puzzle_t p;
        if (*l) {
            if (i == index && puzzle_parse(l, &p)) {
                if (ok) { if (p.ok < 250) p.ok++; } else if (p.ko < 250) p.ko++;
                char o[160];
                puzzle_line(o, sizeof(o), &p);
                fputs(o, f);
            } else fprintf(f, "%s\n", l);
        }
        i++;
        if (!e) break;
        l = e + 1;
    }
    fclose(f);
    free(b);
    remove(PUZZLES);
    rename(PUZZLES ".tmp", PUZZLES);
}

/* ================= esportazione per un'IA ================= */

bool cg_export_llm(char *path_out, int n)
{
    if (!sd_ok()) return false;
    dirs();
    cg_info_t *L = malloc(sizeof(cg_info_t) * 500);
    cgame_t *g = malloc(sizeof(cgame_t));
    int16_t *eh = malloc(sizeof(int16_t) * 200);
    FILE *f = L && g && eh ? fopen(EXPORT, "w") : NULL;
    if (!f) { free(L); free(g); free(eh); return false; }
    int nl = cg_list(L, 500);
    int won, drawn, lost;
    cg_stats(&won, &drawn, &lost);
    fprintf(f, "PARTITE A SCACCHI CONTRO IL GADGET (motore di scacchi su ESP32)\n\n");
    fprintf(f, "Istruzioni per l'IA: queste sono le mie partite contro un motore di scacchi regolato a diversi\n"
               "livelli Elo (indicativi). Analizza il mio stile di gioco: le aperture che scelgo e come le gioco,\n"
               "gli errori che ripeto (tattici, posizionali, nei finali, di gestione del vantaggio o dello svantaggio),\n"
               "i miei punti di forza, come cambio da bianco e da nero e come sto migliorando nel tempo. Poi dammi\n"
               "5 consigli concreti e un piano di allenamento di 4 settimane. Dove c'e' un commento [%%eval] la partita\n"
               "e' stata analizzata dal Gadget (valutazione in pedoni dal punto di vista del bianco; $6 imprecisione,\n"
               "$2 errore, $4 grave errore, con la mossa migliore indicata).\n\n");
    fprintf(f, "RIEPILOGO\n");
    fprintf(f, "Elo attuale (stima del Gadget): %d dopo %d partite valide\n", cg_cfg.elo, cg_cfg.rated_games);
    fprintf(f, "Contro il Gadget: %d vinte, %d patte, %d perse\n", won, drawn, lost);
    int ne = cg_elo_history(eh, 200);
    if (ne > 1) {
        fprintf(f, "Andamento dell'Elo:");
        for (int i = 0; i < ne; i++) fprintf(f, " %d", eh[i]);
        fputc('\n', f);
    }
    // aperture: prima mossa da bianco e risposta da nero
    char op_w[8][8] = {{0}}, op_b[8][16] = {{0}};
    int cw[8] = {0}, cb[8] = {0}, nw = 0, nb = 0, acc_sum = 0, acc_n = 0;
    for (int i = nl - 1; i >= 0; i--) {
        if (L[i].mode != CG_MODE_BOT || !cg_load(L[i].id, g) || g->n < 2) continue;
        char s[16];
        int k = L[i].color;   // la mia prima mossa
        cg_san(g, k, s);
        if (L[i].color == 0) {
            int j = 0;
            while (j < nw && strcmp(op_w[j], s)) j++;
            if (j == nw && nw < 8) strcpy(op_w[nw++], s);
            if (j < 8) cw[j]++;
        } else {
            char o[16];
            cg_san(g, 0, o);
            char pair[16];
            snprintf(pair, sizeof(pair), "%.6s %.6s", o, s);
            int j = 0;
            while (j < nb && strcmp(op_b[j], pair)) j++;
            if (j == nb && nb < 8) strcpy(op_b[nb++], pair);
            if (j < 8) cb[j]++;
        }
        if (L[i].acc[L[i].color] >= 0) { acc_sum += L[i].acc[L[i].color]; acc_n++; }
    }
    if (nw) { fprintf(f, "Da bianco apro con:"); for (int j = 0; j < nw; j++) fprintf(f, " %s (%d)", op_w[j], cw[j]); fputc('\n', f); }
    if (nb) { fprintf(f, "Da nero rispondo (sua mossa, mia risposta):"); for (int j = 0; j < nb; j++) fprintf(f, " %s (%d)", op_b[j], cb[j]); fputc('\n', f); }
    if (acc_n) fprintf(f, "Precisione media nelle partite analizzate: %d%% (%d partite)\n", acc_sum / acc_n, acc_n);
    fprintf(f, "\nPARTITE (PGN, dalla piu' vecchia)\n\n");
    int count = 0;
    for (int i = nl - 1; i >= 0; i--) {
        if (L[i].mode != CG_MODE_BOT || !cg_load(L[i].id, g)) continue;
        write_pgn(f, g);
        count++;
    }
    bool ok = !ferror(f);
    fclose(f);
    free(g);
    free(L);
    free(eh);
    if (path_out) snprintf(path_out, n, "scacchi/per_IA.txt (%d partite)", count);
    return ok;
}
