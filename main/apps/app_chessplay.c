// app_chessplay.c — Scacchi: partite contro il Gadget (Elo regolabile o adattivo) e a due,
// analisi delle partite, allenamento sulle posizioni dove hai sbagliato, il tuo Elo, archivio
// sulla microSD ed esportazione per un'IA. Regole e motore in chess_engine.c, archivio in
// chess_game.c, scacchiera in chess_ui.c.
//
// Comandi sulla scacchiera: tocca un pezzo e poi la casa, oppure muovi il cursore con gli
// swipe e conferma con BOOT. Il dito tenuto apre il menu della partita.
// Il motore pensa in un task a parte: lo schermo resta fluido.
#include "apps.h"
#include "chess_ui.h"
#include "chess_game.h"
#include "settings.h"
#include "sd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_heap_caps.h"
#ifndef SEISMO_SIM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"
#endif

#define HINT_MS   2500
#define CHECK_MS  1500
#define AN_MS      600   // analisi: tempo per posizione

#define C_GOOD lv_color_hex(0x5CFF8A)
#define C_INAC lv_color_hex(0xFFD23C)
#define C_MIST lv_color_hex(0xFF9A3D)

static cgame_t *G, *V;          // partita in corso, partita nel visore
static bool g_active;           // c'è una partita in corso (in memoria)
static cpos_t P;                // posizione della partita in corso
static uint64_t *H;             // hash delle posizioni precedenti
static cboard_t B;

static void init_once(void)
{
    static bool done;
    if (done) return;
    done = true;
    cp_init();
    ce_init(16);   // tabella delle trasposizioni: 1 MB in PSRAM
    cg_cfg_load();
    G = heap_caps_calloc(1, sizeof(cgame_t), MALLOC_CAP_SPIRAM);
    V = heap_caps_calloc(1, sizeof(cgame_t), MALLOC_CAP_SPIRAM);
    H = heap_caps_calloc(CG_MAXPLY + 1, sizeof(uint64_t), MALLOC_CAP_SPIRAM);
}

static lv_color_t cls_color(int c)
{
    return c == CG_CLS_BLUNDER ? C_WARN : c == CG_CLS_MISTAKE ? C_MIST : c == CG_CLS_INACC ? C_INAC : c == CG_CLS_BEST ? C_GOOD : C_TEXT;
}

static void show_san(const char *san, char *out)
{
    if (cg_cfg.notation_it) cp_san_it(san, out);
    else strcpy(out, san);
}

static void fmt_eval(char *b, int n, int v)
{
    if (v >= 9000) snprintf(b, n, "matto per il bianco");
    else if (v <= -9000) snprintf(b, n, "matto per il nero");
    else snprintf(b, n, "%+.1f", v / 100.0);
}

// "2026.10.08 14:30" → "08/10"
static void dm(char *o, const char *date) { snprintf(o, 8, "%.2s/%.2s", date + 8, date + 5); }

/* ================= il motore in un task ================= */

enum { J_NONE, J_BOT, J_HINT, J_ANALYZE, J_CHECK };
static volatile int job;
static volatile bool job_busy, job_done;
static volatile uint32_t job_gen;       // per riconoscere un risultato ormai inutile
static cpos_t j_pos;
static uint64_t *j_hist;
static int j_nhist, j_elo;
static cmove_t j_result, j_test;
static int j_score, j_score2;
static cgame_t *j_game;
static volatile int an_k, an_n;

static uint32_t rnd32(void)
{
#ifdef SEISMO_SIM
    return (uint32_t)rand() * 2654435761u;
#else
    return esp_random();
#endif
}

static bool an_progress(int k, int n) { an_k = k; an_n = n; return !ce_abort; }

static void run_job(void)
{
    cresult_t r;
    switch (job) {
    case J_BOT: {
        cmove_t m;
        if (j_elo >= 800 && ce_book(G->mv, G->n, rnd32(), &m)) { j_result = m; j_score = 0; break; }
        j_result = ce_play(&j_pos, j_hist, j_nhist, j_elo, j_nhist, rnd32(), &r);
        j_score = r.score;
        break;
    }
    case J_HINT:
        ce_search(&j_pos, j_hist, j_nhist, 30, HINT_MS, &r);
        j_result = r.best;
        j_score = r.score;
        break;
    case J_CHECK: {
        ce_clear();
        ce_search(&j_pos, j_hist, 0, 30, CHECK_MS, &r);
        j_result = r.best;
        j_score = r.score;
        cpos_t q = j_pos;
        cp_make(&q, j_test);
        cmove_t l[CP_MAXMOVES];
        if (!cp_legal(&q, l)) j_score2 = cp_in_check(&q) ? CE_MATE : 0;
        else {
            uint64_t h0 = j_pos.hash;
            cresult_t r2;
            ce_search(&q, &h0, 1, 30, CHECK_MS, &r2);
            j_score2 = -r2.score;
        }
        break;
    }
    case J_ANALYZE:
        cg_analyze(j_game, AN_MS, an_progress);
        break;
    }
}

#ifdef SEISMO_SIM
#include <time.h>
// nel simulatore la ricerca gira subito, con l'orologio vero (lv_tick non avanza durante)
static uint32_t sim_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static void job_start(int j)
{
    ce_now_ms = sim_ms;
    job = j;
    job_gen++;
    ce_abort = false;
    job_busy = true;
    run_job();   // nel simulatore subito, senza task
    job_busy = false;
    job_done = true;
}
#else
static TaskHandle_t worker_h;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void yield(void) { vTaskDelay(1); }   // il resto del sistema (e il watchdog) respira

static void worker(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_job();
        job_busy = false;
        job_done = true;
    }
}

static void job_start(int j)
{
    if (!worker_h) {
        ce_now_ms = now_ms;
        ce_yield = yield;
        // stack in PSRAM: la ricerca scende parecchio
        if (xTaskCreatePinnedToCoreWithCaps(worker, "scacchi", 32 * 1024, NULL, 2, &worker_h, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
            ui_toast("Il motore non parte (memoria)");
            return;
        }
    }
    job = j;
    job_gen++;
    ce_abort = false;
    job_done = false;
    job_busy = true;
    xTaskNotifyGive(worker_h);
}
#endif

static void job_cancel(void)
{
    if (job_busy) ce_abort = true;
    job_done = false;
    job = J_NONE;
    job_gen++;   // il risultato del lavoro fermato non è più di nessuno
}

/* ================= pannello di testo (comune) ================= */

static lv_obj_t *l_t, *l_a, *l_b, *l_c, *l_h, *b_ok;
static uint32_t b_ok_t;

// Sul 3,49" il controller del touch registra a volte tocchi fantasma al centro: lì la
// scacchiera non si tocca, si usano solo gli swipe (cursore) e BOOT. Sul tondo si tocca.
#define TAP_BOARD (SCR_ROUND)
#define BTN_X 516   // pulsante Conferma (3,49", partita a due): a destra, lontano dal centro
#define BTN_Y 26
#define BTN_W 116
#define BTN_H 118

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

// scacchiera e testi: sul 3,49" scacchiera a sinistra e testi a destra; sul tondo la
// scacchiera al centro, una riga sopra e due sotto
static void build(lv_obj_t *root)
{
    b_ok = NULL;
    b_ok_t = 0;
    if (SCR_ROUND) {
        cb_create(&B, root, (SCR_W - 320) / 2, (SCR_H - 320) / 2, 320);
        l_t = mk(root, &font_s, C_DIM);
        lv_obj_add_flag(l_t, LV_OBJ_FLAG_HIDDEN);
        l_a = mk(root, &font_m, C_TEXT);
        l_b = mk(root, &font_s, C_TEXT);
        l_c = mk(root, &font_s, C_DIM);
        l_h = mk(root, &font_s, C_DIM);
        lv_obj_add_flag(l_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *ls[] = {l_a, l_b, l_c};
        int ys[] = {30, 398, 424}, ws[] = {250, 300, 236};
        for (int i = 0; i < 3; i++) {
            // sul tondo le righe sono corte: un testo lungo scorre invece di essere tagliato
            lv_label_set_long_mode(ls[i], i ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
            lv_obj_set_width(ls[i], ws[i]);
            lv_obj_set_style_text_align(ls[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(ls[i], LV_ALIGN_TOP_MID, 0, ys[i]);
        }
    } else {
        cb_create(&B, root, 6, 6, 160);
        int x = 180, w = SCR_W - x - 8;
        l_t = mk(root, &font_s, C_DIM);
        l_a = mk(root, &font_m, C_TEXT);
        l_b = mk(root, &font_s, C_TEXT);
        l_c = mk(root, &font_s, C_DIM);
        l_h = mk(root, &font_s, C_DIM);
        lv_obj_t *ls[] = {l_t, l_a, l_b, l_c, l_h};
        int ys[] = {4, 22, 56, 100, 150};
        for (int i = 0; i < 5; i++) {
            lv_obj_set_width(ls[i], w);
            lv_label_set_long_mode(ls[i], (i == 2 || i == 3) ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
            lv_obj_set_pos(ls[i], x, ys[i]);
        }
    }
    lv_obj_set_height(l_a, lv_font_get_line_height(&font_m));   // una riga sola
    lv_label_set_long_mode(l_a, LV_LABEL_LONG_DOT);
    B.cursor = -1;
    B.last_from = B.last_to = B.hint_from = B.hint_to = -1;
    B.show_marks = cg_cfg.show_moves;
    cb_clear_marks(&B);
    cb_tap_reset();
}

// swipe → cursore (su/giù rispettano l'inversione dello scorrimento delle liste)
static bool swipe_cursor(nav_t ev)
{
    int df = 0, dr = 0;
    if (ev == NAV_NEXT || ev == NAV_PREV) {
        bool up = ev == NAV_NEXT;
        if (g_set.invert_scroll) up = !up;
        dr = up ? 1 : -1;
    } else if (ev == NAV_SELECT) df = 1;
    else if (ev == NAV_BACK) df = -1;
    else return false;
    cb_move_cursor(&B, df, dr);
    return true;
}

// scelta del pezzo e della casa: true se è stata scelta una mossa (in *out)
static bool pick_square(const cpos_t *p, int sq, int side, cmove_t *out, bool *needs_promo)
{
    if (sq < 0) return false;
    cmove_t l[CP_MAXMOVES];
    int n = cp_legal(p, l);
    if (B.sel >= 0 && B.mark[sq]) {
        for (int i = 0; i < n; i++)
            if (l[i].from == B.sel && l[i].to == sq) {
                *out = l[i];
                *needs_promo = l[i].promo != 0;
                cb_clear_marks(&B);
                return true;
            }
    }
    cb_clear_marks(&B);
    int pc = p->sq[sq];
    if (pc && CP_COLOR(pc) == side) {
        B.sel = sq;
        for (int i = 0; i < n; i++) if (l[i].from == sq) B.mark[l[i].to] = 1;
    }
    return false;
}

/* ================= partita ================= */

typedef struct { int mode, color, elo; bool fresh; } start_t;
static start_t req;
static lv_timer_t *g_tmr;
static bool g_thinking, g_over;
static uint32_t g_think_t0, g_bot_gen, g_hint_gen;
static int g_last_bot_score;
static cmove_t g_promo_move;
static int g_promo_pick;           // pezzo scelto per la promozione (dal menu), 0 nessuno
static int g_action;               // azione dal menu della partita
enum { A_NONE, A_HINT, A_UNDO, A_FLIP, A_DRAW, A_RESIGN, A_EXIT };
static char g_info[96];
static bool g_analyze_after;
static const app_t app_cview;
static menu_t gm_menu, pr_menu;
static bool g_flip_user, g_exit;

static bool bot_turn(void) { return G->info.mode == CG_MODE_BOT && P.side != G->info.color; }
static int my_side(void) { return G->info.mode == CG_MODE_BOT ? G->info.color : P.side; }

static void rebuild_pos(void)
{
    cg_position(G, G->n, &P, H);
    if (G->n) { B.last_from = G->mv[G->n - 1].from; B.last_to = G->mv[G->n - 1].to; }
    else B.last_from = B.last_to = -1;
}

// l'ultima mossa a parole, per chi non conosce la notazione: "Gadget: Cavallo da g8 a f6, scacco"
static void last_move_text(char *o, int n)
{
    static const char *const NAME[7] = {"", "Pedone", "Cavallo", "Alfiere", "Torre", "Donna", "Re"};
    o[0] = 0;
    if (!G->n) return;
    cpos_t p;
    cg_position(G, G->n - 1, &p, NULL);
    cmove_t m = G->mv[G->n - 1];
    const char *who = G->info.mode == CG_MODE_BOT ? (p.side == G->info.color ? "Tu" : "Gadget") : (p.side ? "Nero" : "Bianco");
    char fr[3] = {(char)('a' + CP_FILE(m.from)), (char)('1' + CP_RANK(m.from)), 0};
    char to[3] = {(char)('a' + CP_FILE(m.to)), (char)('1' + CP_RANK(m.to)), 0};
    int k;
    if (m.flags & CM_CASTLE) k = snprintf(o, n, "%s: arrocco %s", who, CP_FILE(m.to) == 6 ? "corto" : "lungo");
    else if (m.flags & CM_CAPTURE)
        k = snprintf(o, n, "%s: %s da %s prende in %s%s", who, NAME[CP_TYPE(p.sq[m.from])], fr, to, (m.flags & CM_EP) ? " (en passant)" : "");
    else k = snprintf(o, n, "%s: %s da %s a %s", who, NAME[CP_TYPE(p.sq[m.from])], fr, to);
    if (m.promo && k < n) k += snprintf(o + k, n - k, ", diventa %s", NAME[m.promo]);
    if (cp_in_check(&P) && k < n) {
        cmove_t l[CP_MAXMOVES];
        snprintf(o + k, n - k, cp_legal(&P, l) ? ", scacco" : ", scacco matto");
    }
}

static void moves_text(char *b, int n, int max_plies)
{
    int k0 = G->n - max_plies;
    if (k0 < 0) k0 = 0;
    int o = 0;
    b[0] = 0;
    for (int k = k0; k < G->n && o < n - 16; k++) {
        char s[16], t[16];
        cg_san(G, k, s);
        show_san(s, t);
        if (!(k & 1)) o += snprintf(b + o, n - o, "%s%d. %s", o ? "  " : "", k / 2 + 1, t);
        else if (k == k0) o += snprintf(b + o, n - o, "%d... %s", k / 2 + 1, t);
        else o += snprintf(b + o, n - o, " %s", t);
    }
}

// partita a due sul 3,49": il pulsante Conferma fa quello che fa BOOT, così entrambi i
// giocatori possono muovere senza il tasto fisico
static void ok_button(lv_obj_t *root)
{
    b_ok = lv_obj_create(root);
    lv_obj_remove_style_all(b_ok);
    lv_obj_set_pos(b_ok, BTN_X, BTN_Y);
    lv_obj_set_size(b_ok, BTN_W, BTN_H);
    lv_obj_set_style_radius(b_ok, 14, 0);
    lv_obj_set_style_bg_opa(b_ok, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b_ok, C_FAINT, 0);
    lv_obj_set_style_border_width(b_ok, 2, 0);
    lv_obj_set_style_border_color(b_ok, ui_accent(), 0);
    lv_obj_clear_flag(b_ok, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = mk(b_ok, &font_l, ui_accent());
    lv_label_set_text(ic, LV_SYMBOL_OK);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -12);
    lv_obj_t *l = mk(b_ok, &font_s, C_TEXT);
    lv_label_set_text(l, "Conferma");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 26);
    // i testi a destra si stringono per lasciargli posto
    lv_obj_t *ls[] = {l_t, l_a, l_b, l_c, l_h};
    for (int i = 0; i < 5; i++) lv_obj_set_width(ls[i], BTN_X - 180 - 10);
}

static bool in_ok_button(int x, int y)
{
    return b_ok && x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

static void ok_flash(bool on)
{
    if (b_ok) lv_obj_set_style_bg_color(b_ok, on ? ui_accent() : C_FAINT, 0);
    b_ok_t = on ? lv_tick_get() : 0;
}

static void g_confirm(void);

static void g_render(void)
{
    char b[128];
    if (G->info.mode == CG_MODE_BOT)
        snprintf(b, sizeof(b), "Tu (%d, %s) · Gadget %d", cg_cfg.elo, G->info.color ? "nero" : "bianco", G->info.bot_elo);
    else snprintf(b, sizeof(b), "Partita a due");
    ui_set_text(l_t, b);
    lv_color_t col = C_TEXT;
    if (g_over) {
        int r = G->info.result;
        if (G->info.mode == CG_MODE_BOT) {
            int mine = G->info.color ? CG_RES_BLACK : CG_RES_WHITE;
            snprintf(b, sizeof(b), SCR_ROUND ? "%s" : "%s · %s", r == CG_RES_DRAW ? "Patta" : r == mine ? "Hai vinto!" : "Hai perso",
                     cg_end_str(G->info.end));
            col = r == mine ? C_GOOD : r == CG_RES_DRAW ? C_TEXT : C_WARN;
        } else snprintf(b, sizeof(b), SCR_ROUND ? "%s" : "%s · %s", r == CG_RES_DRAW ? "Patta" : r == CG_RES_WHITE ? "Vince il bianco" : "Vince il nero",
                        cg_end_str(G->info.end));
    } else if (g_thinking) snprintf(b, sizeof(b), "Il Gadget pensa…");
    else {
        bool chk = cp_in_check(&P);
        if (G->info.mode == CG_MODE_BOT) snprintf(b, sizeof(b), "%s", chk ? "Scacco! Tocca a te" : "Tocca a te");
        else snprintf(b, sizeof(b), "Tocca al %s%s", P.side ? "nero" : "bianco", chk ? " · scacco!" : "");
        if (chk) col = C_WARN;
    }
    ui_set_text(l_a, b);
    ui_set_text_color(l_a, col);
    moves_text(b, sizeof(b), SCR_ROUND ? 2 : 8);
    if (g_over && SCR_ROUND) {   // sul tondo il motivo va sotto la scacchiera
        snprintf(b, sizeof(b), "%s", cg_end_str(G->info.end));
        b[0] = (char)toupper((unsigned char)b[0]);
    }
    char lm[96];
    last_move_text(lm, sizeof(lm));
    if (SCR_ROUND && !g_over && G->n) ui_set_text(l_b, lm);   // sul tondo al posto della notazione
    else ui_set_text(l_b, G->n || g_over ? b : "Prima mossa");
    ui_set_text(l_c, g_info[0] || SCR_ROUND || g_over ? g_info : lm);
    ui_set_text(l_h, g_over ? "Destra: analizza · BOOT: nuova partita · sinistra: esci"
                            : TAP_BOARD ? "Tocca o swipe+BOOT: muovi · dito tenuto: menu"
                            : b_ok ? "Swipe: cursore · Conferma · dito tenuto: menu"
                                   : "Swipe: cursore · BOOT: scegli e muovi · dito tenuto: menu");
    cb_draw(&B, &P);
}

static void finish(int result, int end)
{
    G->info.result = result;
    G->info.end = end;
    g_over = true;
    g_active = false;
    B.cursor = -1;
    cb_clear_marks(&B);
    if (G->info.mode == CG_MODE_BOT) G->info.rated = !G->info.hints && !G->info.undos;
    bool saved = cg_save(G);
    if (G->info.mode == CG_MODE_BOT && G->info.rated) {
        int mine = G->info.color ? CG_RES_BLACK : CG_RES_WHITE;
        float score = result == CG_RES_DRAW ? 0.5f : result == mine ? 1.0f : 0.0f;
        int before = cg_cfg.elo, after = cg_elo_update(G, score);
        if (saved) cg_save(G);   // con l'Elo prima e dopo
        snprintf(g_info, sizeof(g_info), "Elo %d → %d (%+d)", before, after, after - before);
    } else if (G->info.mode == CG_MODE_BOT) {
        snprintf(g_info, sizeof(g_info), "Non conta per l'Elo: hai usato %s", G->info.hints ? "suggerimenti" : "annulla mossa");
    } else g_info[0] = 0;
    if (!saved) {
        size_t l = strlen(g_info);
        snprintf(g_info + l, sizeof(g_info) - l, "%sNon salvata: manca la microSD", l ? " · " : "");
    }
    cg_resume_clear();
}

static void check_end(void)
{
    cstatus_t s = cp_status(&P, H, G->n);
    if (s == CS_PLAY) return;
    int res = s == CS_MATE ? (P.side ? CG_RES_WHITE : CG_RES_BLACK) : CG_RES_DRAW;
    int end = s == CS_MATE ? CG_END_MATE : s == CS_STALEMATE ? CG_END_STALEMATE : s == CS_FIFTY ? CG_END_FIFTY
            : s == CS_REPETITION ? CG_END_REPETITION : CG_END_MATERIAL;
    finish(res, end);
}

static void start_bot(void)
{
    if (g_over || !bot_turn() || g_thinking) return;
    g_thinking = true;
    g_think_t0 = lv_tick_get();
    if (job_busy) return;   // il motore sta ancora fermando un lavoro vecchio: si parte al prossimo giro
    j_pos = P;
    j_hist = H;
    j_nhist = G->n;
    j_elo = G->info.bot_elo;
    job_start(J_BOT);
    g_bot_gen = job_gen;
}

static void play(cmove_t m)
{
    if (G->n >= CG_MAXPLY) { finish(CG_RES_DRAW, CG_END_AGREED); return; }
    H[G->n] = P.hash;
    G->mv[G->n++] = m;
    cp_make(&P, m);
    B.last_from = m.from;
    B.last_to = m.to;
    B.hint_from = B.hint_to = -1;
    g_info[0] = 0;
    if (G->info.mode == CG_MODE_TWO && cg_cfg.flip_two) B.flip = P.side == 1;
    check_end();
    if (!g_over) {
        cg_resume_save(G);
        start_bot();
    }
}

static void g_new(void)
{
    int color = req.color == 2 ? (int)(rnd32() & 1) : req.color;
    int elo = req.elo;
    if (req.mode == CG_MODE_BOT && cg_cfg.adaptive) elo = (cg_cfg.elo + 25) / 50 * 50;
    if (elo < 400) elo = 400;
    cg_new(G, req.mode, req.mode == CG_MODE_BOT ? color : 0, req.mode == CG_MODE_BOT ? elo : 0);
    ce_clear();
    g_active = true;
    g_over = false;
    g_thinking = false;
    g_info[0] = 0;
    B.flip = req.mode == CG_MODE_BOT && color == 1;
    rebuild_pos();
}

static void undo(void)
{
    if (g_thinking || !G->n) return;
    // contro il Gadget si torna alla tua ultima mossa (la sua e la tua)
    int back = 1;
    if (G->info.mode == CG_MODE_BOT) back = (G->n >= 2 && P.side == G->info.color) ? 2 : 1;
    if (G->info.mode == CG_MODE_BOT && G->n - back < 0) return;
    G->n -= back;
    G->info.undos++;
    rebuild_pos();
    cb_clear_marks(&B);
    B.hint_from = B.hint_to = -1;
    snprintf(g_info, sizeof(g_info), "Mossa annullata: la partita non conta più per l'Elo");
    cg_resume_save(G);
    start_bot();   // se tocca al Gadget (dopo un annullamento del tuo primo tratto da nero)
}

static void g_tick(lv_timer_t *t)
{
    if (g_exit) { g_exit = false; ui_pop(); return; }   // "Esci" dal menu della partita
    // risposta del motore
    if (job_done && job_gen == g_bot_gen && g_thinking && job == J_BOT) {
        // mossa dal libro o trovata in fretta: un attimo di "pensiero" perché si veda
        if (lv_tick_elaps(g_think_t0) < 700) return;
        job_done = false;
        g_thinking = false;
        g_last_bot_score = j_score;
        play(j_result);
        g_render();
        return;
    }
    if (job_done && job_gen == g_hint_gen && job == J_HINT) {
        job_done = false;
        if (!cm_null(j_result)) {
            char s[16], t[16];
            cp_san(&P, j_result, s);
            show_san(s, t);
            B.hint_from = j_result.from;
            B.hint_to = j_result.to;
            snprintf(g_info, sizeof(g_info), "Suggerimento: %s", t);
        }
        g_render();
        return;
    }
    if (g_thinking && !job_busy && job_gen != g_bot_gen) { g_thinking = false; start_bot(); }   // partenza rimandata
    if (b_ok_t && lv_tick_elaps(b_ok_t) > 150) ok_flash(false);
    int x, y;
    if (cb_tap_poll(&x, &y)) {
        if (in_ok_button(x, y)) { ok_flash(true); if (!g_over) g_confirm(); return; }
        if (!TAP_BOARD || g_over) return;
        if (g_thinking || bot_turn()) return;
        cmove_t m;
        bool promo = false;
        B.cursor = -1;
        if (pick_square(&P, cb_hit(&B, x, y), my_side(), &m, &promo)) {
            if (promo) { g_promo_move = m; g_promo_pick = 0; pr_menu.sel = 0; ui_push(&app_menu, &pr_menu); return; }
            play(m);
        }
        g_render();
    }
}

static void g_enter(lv_obj_t *root, void *arg)
{
    init_once();
    build(root);
    if (req.fresh) { req.fresh = false; g_new(); }
    else if (!g_active && !g_over && cg_resume_load(G)) {   // "Riprendi" dalla microSD
        g_active = true;
        g_over = false;
        g_info[0] = 0;
        B.flip = G->info.mode == CG_MODE_BOT && G->info.color == 1;
        rebuild_pos();
    }
    if (G->info.mode == CG_MODE_TWO && cg_cfg.flip_two) B.flip = P.side == 1;
    else B.flip = G->info.mode == CG_MODE_BOT && G->info.color == 1;
    B.flip ^= g_flip_user;
    if (!SCR_ROUND && G->info.mode == CG_MODE_TWO) ok_button(root);
    // la scacchiera si ricostruisce a ogni ritorno (dal menu, dalla promozione): l'ultima mossa resta segnata
    if (G->n) { B.last_from = G->mv[G->n - 1].from; B.last_to = G->mv[G->n - 1].to; }
    // ritorno dalla scelta della promozione o dal menu della partita
    if (g_promo_pick) {
        cmove_t m = g_promo_move;
        m.promo = g_promo_pick;
        g_promo_pick = 0;
        play(m);
    }
    int a = g_action;
    g_action = A_NONE;
    if (a == A_HINT && !g_over && !g_thinking && !bot_turn() && !job_busy) {
        G->info.hints++;
        j_pos = P; j_hist = H; j_nhist = G->n;
        snprintf(g_info, sizeof(g_info), "Cerco la mossa migliore…");
        job_start(J_HINT);
        g_hint_gen = job_gen;
    } else if (a == A_UNDO) undo();
    else if (a == A_FLIP) { g_flip_user = !g_flip_user; B.flip = !B.flip; }
    else if (a == A_DRAW && !g_over) {
        // il Gadget accetta solo se sta peggio
        if (G->info.mode == CG_MODE_TWO || g_last_bot_score <= -50) finish(CG_RES_DRAW, CG_END_AGREED);
        else snprintf(g_info, sizeof(g_info), "Il Gadget rifiuta la patta");
    } else if (a == A_RESIGN && !g_over) {
        job_cancel();
        g_thinking = false;
        finish(my_side() ? CG_RES_WHITE : CG_RES_BLACK, CG_END_RESIGN);
    } else if (a == A_EXIT) g_exit = true;   // si esce dal timer, non mentre si costruisce la schermata
    if (g_active && !g_over) start_bot();
    g_render();
    g_tmr = lv_timer_create(g_tick, 30, NULL);
}

static void g_leave(void)
{
    if (g_tmr) { lv_timer_delete(g_tmr); g_tmr = NULL; }
    if (ui_closing()) {
        if (g_thinking) { job_cancel(); g_thinking = false; }   // si riprende e il Gadget ripensa
    }
}

// BOOT o il pulsante Conferma: fa comparire il cursore, poi sceglie il pezzo o la casa
static void g_confirm(void)
{
    if (B.cursor < 0) { cb_move_cursor(&B, 0, 0); g_render(); return; }
    if (g_thinking || bot_turn()) return;
    cmove_t m;
    bool promo = false;
    if (pick_square(&P, B.cursor, my_side(), &m, &promo)) {
        if (promo) { g_promo_move = m; g_promo_pick = 0; pr_menu.sel = 0; ui_push(&app_menu, &pr_menu); return; }
        play(m);
    }
    g_render();
}

static bool g_nav(nav_t ev)
{
    if (g_over) {
        if (ev == NAV_SELECT) {   // analisi della partita appena finita
            memcpy(V, G, sizeof(cgame_t));
            g_analyze_after = true;
            ui_push(&app_cview, NULL);
            return true;
        }
        if (ev == NAV_BTN) {   // nuova partita, stesse impostazioni
            req = (start_t){G->info.mode, cg_cfg.color, cg_cfg.bot_elo, false};
            g_new();
            start_bot();
            g_render();
            return true;
        }
        return ev == NAV_NEXT || ev == NAV_PREV || ev == NAV_QUICK;
    }
    if (ev == NAV_QUICK) { gm_menu.sel = 0; ui_push(&app_menu, &gm_menu); return true; }
    if (ev == NAV_BTN) { g_confirm(); return true; }
    if (swipe_cursor(ev)) { g_render(); return true; }
    return false;
}

static const app_t app_cgame = {
    .name = "Scacchi", .icon = ICON_GAMEPAD,
    .enter = g_enter, .leave = g_leave, .nav = g_nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

// menu della partita (dito tenuto)
static void act(void *a) { g_action = (int)(intptr_t)a; ui_pop(); }
static const menu_item_t gm_items[] = {
    {.icon = LV_SYMBOL_EYE_OPEN, .label = "Suggerimento", .hint = "La mossa migliore secondo il Gadget (niente Elo)", .on_pick = act, .arg = (void *)A_HINT},
    {.icon = LV_SYMBOL_LOOP, .label = "Annulla mossa", .hint = "Torna indietro (niente Elo)", .on_pick = act, .arg = (void *)A_UNDO},
    {.icon = LV_SYMBOL_REFRESH, .label = "Gira la scacchiera", .on_pick = act, .arg = (void *)A_FLIP},
    {.icon = LV_SYMBOL_MINUS, .label = "Proponi patta", .on_pick = act, .arg = (void *)A_DRAW},
    {.icon = LV_SYMBOL_CLOSE, .label = "Abbandona", .on_pick = act, .arg = (void *)A_RESIGN, .confirm = true},
    {.icon = LV_SYMBOL_LEFT, .label = "Esci", .hint = "La partita resta da riprendere", .on_pick = act, .arg = (void *)A_EXIT},
};
static menu_t gm_menu = {"Partita", gm_items, sizeof(gm_items) / sizeof(gm_items[0]), 0, NULL};

// promozione
static void promo(void *a) { g_promo_pick = (int)(intptr_t)a; ui_pop(); }
static const menu_item_t pr_items[] = {
    {.icon = LV_SYMBOL_UP, .label = "Donna", .on_pick = promo, .arg = (void *)CP_Q},
    {.icon = LV_SYMBOL_UP, .label = "Cavallo", .on_pick = promo, .arg = (void *)CP_N},
    {.icon = LV_SYMBOL_UP, .label = "Torre", .on_pick = promo, .arg = (void *)CP_R},
    {.icon = LV_SYMBOL_UP, .label = "Alfiere", .on_pick = promo, .arg = (void *)CP_B},
};
static menu_t pr_menu = {"Promozione", pr_items, 4, 0, NULL};

/* ================= visore e analisi ================= */

static int v_k;                 // posizione dopo v_k semimosse
static bool v_best, v_running;
static uint32_t v_gen;
static lv_timer_t *v_tmr;

static bool v_mine(int k)
{
    return V->info.mode == CG_MODE_TWO || (k & 1) == V->info.color;
}

static void v_render(void)
{
    char b[128], s[16], t[16];
    const char *res = V->info.result == CG_RES_DRAW ? "patta" : V->info.result == CG_RES_NONE ? "in corso"
                    : V->info.mode == CG_MODE_TWO ? (V->info.result == CG_RES_WHITE ? "vince il bianco" : "vince il nero")
                    : V->info.result == (V->info.color ? CG_RES_BLACK : CG_RES_WHITE) ? "vinta" : "persa";
    char d[8];
    dm(d, V->info.date);
    if (V->info.mode == CG_MODE_BOT) snprintf(b, sizeof(b), "%s · Gadget %d · %s", d, V->info.bot_elo, res);
    else snprintf(b, sizeof(b), "%s · a due · %s", d, res);
    ui_set_text(l_t, b);
    cpos_t p;
    int k = v_k;
    bool showing_best = v_best && k > 0 && V->analyzed && V->cls[k - 1] >= CG_CLS_INACC && !cm_null(V->best[k - 1]);
    cg_position(V, showing_best ? k - 1 : k, &p, NULL);
    B.hint_from = B.hint_to = -1;
    if (k > 0) { B.last_from = V->mv[k - 1].from; B.last_to = V->mv[k - 1].to; }
    else B.last_from = B.last_to = -1;
    if (k == 0) {
        ui_set_text(l_a, "Posizione iniziale");
        ui_set_text_color(l_a, C_TEXT);
        ui_set_text(l_b, "");
    } else {
        cg_san(V, k - 1, s);
        show_san(s, t);
        int c = V->analyzed ? V->cls[k - 1] : CG_CLS_NONE;
        snprintf(b, sizeof(b), "%d%s %s%s%s", (k - 1) / 2 + 1, (k - 1) & 1 ? "..." : ".", t, c ? " · " : "", cg_cls_name(c));
        ui_set_text(l_a, b);
        ui_set_text_color(l_a, cls_color(c));
        if (V->analyzed) {
            char e[32];
            fmt_eval(e, sizeof(e), V->eval[k]);
            if (c >= CG_CLS_INACC && !cm_null(V->best[k - 1])) {
                cpos_t q;
                cg_position(V, k - 1, &q, NULL);
                char bs[16], bt[16];
                cp_san(&q, V->best[k - 1], bs);
                show_san(bs, bt);
                if (showing_best) { B.hint_from = V->best[k - 1].from; B.hint_to = V->best[k - 1].to; }
                snprintf(b, sizeof(b), SCR_ROUND ? "Meglio %s · %s" : "Meglio %s · valutazione dopo la mossa: %s", bt, e);
            } else snprintf(b, sizeof(b), "Valutazione %s", e);
            ui_set_text(l_b, b);
        } else ui_set_text(l_b, "");
    }
    if (v_running) snprintf(b, sizeof(b), "Analisi %d/%d…", an_k, an_n ? an_n : V->n + 1);
    else if (!V->analyzed) snprintf(b, sizeof(b), "Destra: analizza (circa %d s)", (V->n + 1) * AN_MS / 1000 + 1);
    else if (V->info.mode == CG_MODE_BOT)
        snprintf(b, sizeof(b), SCR_ROUND ? "Tu %d%% · Gadget %d%%" : "Precisione: tu %d%% · Gadget %d%%",
                 V->info.acc[V->info.color], V->info.acc[V->info.color ^ 1]);
    else snprintf(b, sizeof(b), SCR_ROUND ? "Bianco %d%% · nero %d%%" : "Precisione: bianco %d%% · nero %d%%", V->info.acc[0], V->info.acc[1]);
    ui_set_text(l_c, b);
    ui_set_text(l_h, V->analyzed ? "Su/giù: mosse · destra: prossimo errore · BOOT: la migliore"
                                 : "Su/giù: mosse · destra: analizza");
    B.flip = V->info.mode == CG_MODE_BOT && V->info.color == 1;
    B.cursor = -1;
    cb_clear_marks(&B);
    cb_draw(&B, &p);
}

static void v_tick(lv_timer_t *t)
{
    if (!v_running) return;
    if (job_done && job_gen == v_gen) {
        job_done = false;
        v_running = false;
        if (V->analyzed) {
            bool ok = cg_save(V);
            if (V->info.id && G->info.id == V->info.id) memcpy(G, V, sizeof(cgame_t));
            ui_toast(ok ? "Analisi salvata" : "Analisi fatta (senza microSD non si salva)");
        }
    }
    v_render();
}

static void v_analyze(void)
{
    if (V->analyzed || v_running || !V->n) return;
    if (job_busy) { ui_toast("Il motore è occupato, riprova"); return; }
    j_game = V;
    an_k = 0;
    an_n = V->n + 1;
    v_running = true;
    job_start(J_ANALYZE);
    v_gen = job_gen;
}

static void v_enter(lv_obj_t *root, void *arg)
{
    init_once();
    build(root);
    if (g_analyze_after) { g_analyze_after = false; v_k = V->n; v_best = false; v_analyze(); }
    else if (!v_running) { v_k = 0; v_best = false; }
    v_render();
    v_tmr = lv_timer_create(v_tick, 250, NULL);
}

static void v_leave(void)
{
    if (v_tmr) { lv_timer_delete(v_tmr); v_tmr = NULL; }
    if (ui_closing() && v_running) { job_cancel(); v_running = false; }
}

static bool v_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (v_k < V->n) { v_k++; v_best = false; v_render(); } return true;
    case NAV_PREV: if (v_k > 0) { v_k--; v_best = false; v_render(); } return true;
    case NAV_SELECT:
        if (!V->analyzed) { v_analyze(); v_render(); return true; }
        for (int i = 1; i <= V->n; i++) {   // prossimo errore (tuo, contro il Gadget)
            int k = (v_k + i - 1) % V->n + 1;
            if (V->cls[k - 1] >= CG_CLS_MISTAKE && v_mine(k - 1)) { v_k = k; v_best = false; v_render(); return true; }
        }
        ui_toast("Nessun errore: complimenti!");
        return true;
    case NAV_BTN: v_best = !v_best; v_render(); return true;
    default: return false;
    }
}

static const app_t app_cview = {
    .name = "Analisi", .icon = ICON_GAMEPAD,
    .enter = v_enter, .leave = v_leave, .nav = v_nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_ROUND_OK,
};

/* ================= elenco delle partite ================= */

#define MAXLIST 100
static cg_info_t *glist;
static int g_n, g_sel;
static list_view_t lvw;

static void gl_render(int dir)
{
    if (!g_n) {
        list_view_set(&lvw, ICON_GAMEPAD, NULL, "Nessuna partita",
                      cg_store_ok() ? "Le partite finite si salvano qui" : "Serve la microSD", NULL, 0, 0, dir);
        return;
    }
    char m[64], s[96], pv[48], nx[48];
    const cg_info_t *i = &glist[g_sel];
    const char *res;
    if (i->mode == CG_MODE_BOT) {
        int mine = i->color ? CG_RES_BLACK : CG_RES_WHITE;
        res = i->result == CG_RES_DRAW ? "patta" : i->result == mine ? "vinta" : "persa";
        char d[8];
        dm(d, i->date);
        snprintf(m, sizeof(m), "%s · %s", d, res);
        snprintf(s, sizeof(s), "Gadget %d · col %s · %d mosse", i->bot_elo, i->color ? "nero" : "bianco", (i->plies + 1) / 2);
        if (i->acc[i->color] >= 0) { size_t l = strlen(s); snprintf(s + l, sizeof(s) - l, " · precisione %d%%", i->acc[i->color]); }
    } else {
        res = i->result == CG_RES_DRAW ? "patta" : i->result == CG_RES_WHITE ? "vince il bianco" : "vince il nero";
        char d[8];
        dm(d, i->date);
        snprintf(m, sizeof(m), "%s · a due", d);
        snprintf(s, sizeof(s), "%s · %d mosse", res, (i->plies + 1) / 2);
    }
    if (g_sel > 0) dm(pv, glist[g_sel - 1].date);
    if (g_sel + 1 < g_n) dm(nx, glist[g_sel + 1].date);
    list_view_set(&lvw, ICON_GAMEPAD, g_sel > 0 ? pv : NULL, m, s, g_sel + 1 < g_n ? nx : NULL, g_sel, g_n, dir);
}

static void gl_enter(lv_obj_t *root, void *arg)
{
    init_once();
    if (!glist) glist = heap_caps_malloc(sizeof(cg_info_t) * MAXLIST, MALLOC_CAP_SPIRAM);
    list_view_create(&lvw, root);
    g_n = glist ? cg_list(glist, MAXLIST) : 0;
    if (g_sel >= g_n) g_sel = 0;
    gl_render(0);
}

static bool gl_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (g_sel + 1 < g_n) { g_sel++; gl_render(1); } return true;
    case NAV_PREV: if (g_sel > 0) { g_sel--; gl_render(-1); } return true;
    case NAV_SELECT:
        if (!g_n) return true;
        if (!cg_load(glist[g_sel].id, V)) { ui_toast("Non riesco a leggere la partita"); return true; }
        v_running = false;
        ui_push(&app_cview, NULL);
        return true;
    default: return false;
    }
}

static const app_t app_clist = {
    .name = "Partite giocate", .icon = LV_SYMBOL_LIST,
    .enter = gl_enter, .nav = gl_nav, .flags = APP_ROUND_OK,
};

/* ================= allenamento ================= */

static cg_puzzle_t pz;
static int pz_idx, pz_done_ok, pz_done_n;
static enum { PZ_NONE, PZ_SOLVE, PZ_CHECK, PZ_RESULT } pz_state;
static cpos_t pz_pos;
static cmove_t pz_move;
static bool pz_ok;
static uint32_t pz_gen;
static lv_timer_t *pz_tmr;

static void pz_next(void)
{
    pz_state = PZ_NONE;
    if (!cg_puzzle_pick(&pz, &pz_idx)) return;
    cp_from_fen(&pz_pos, pz.fen);
    pz_state = PZ_SOLVE;
    B.flip = pz_pos.side == 1;
    B.last_from = B.last_to = B.hint_from = B.hint_to = -1;
    B.cursor = -1;
    cb_clear_marks(&B);
}

static void pz_render(void)
{
    char b[128];
    int todo, n = cg_puzzle_count(&todo);
    ui_set_text(l_t, pz_state == PZ_NONE ? "Allenamento" : "Allenamento · dalle tue partite");
    if (pz_state == PZ_NONE) {
        ui_set_text(l_a, "Nessun esercizio");
        ui_set_text_color(l_a, C_TEXT);
        ui_set_text(l_b, "Analizza le tue partite: qui troverai le posizioni dove hai sbagliato");
        ui_set_text(l_c, "");
        ui_set_text(l_h, "");
        cpos_t s;
        cp_start(&s);
        cb_draw(&B, &s);
        return;
    }
    if (pz_state == PZ_SOLVE) {
        snprintf(b, sizeof(b), "Muove il %s: trova la mossa migliore", pz_pos.side ? "nero" : "bianco");
        ui_set_text(l_a, SCR_ROUND ? (pz_pos.side ? "Muove il nero" : "Muove il bianco") : b);
        ui_set_text_color(l_a, C_TEXT);
        snprintf(b, sizeof(b), "Partita n. %d, mossa %d", pz.game_id, pz.ply / 2 + 1);
        ui_set_text(l_b, SCR_ROUND ? "Trova la mossa migliore" : b);
    } else if (pz_state == PZ_CHECK) {
        ui_set_text(l_a, "Controllo…");
        ui_set_text_color(l_a, C_TEXT);
    } else {
        char s[16], t[16], s2[16], t2[16];
        cp_san(&pz_pos, pz.best, s);
        show_san(s, t);
        ui_set_text(l_a, pz_ok ? "Giusto!" : "Non proprio");
        ui_set_text_color(l_a, pz_ok ? C_GOOD : C_WARN);
        if (!cm_null(pz.played)) { cp_san(&pz_pos, pz.played, s2); show_san(s2, t2); }
        if (pz_ok) snprintf(b, sizeof(b), "La migliore: %s", t);
        else if (!cm_null(pz.played)) snprintf(b, sizeof(b), "Era %s (in partita avevi giocato %s)", t, t2);
        else snprintf(b, sizeof(b), "Era %s", t);
        ui_set_text(l_b, b);
        B.hint_from = pz.best.from;
        B.hint_to = pz.best.to;
    }
    snprintf(b, sizeof(b), "Oggi %d su %d · da risolvere %d di %d", pz_done_ok, pz_done_n, todo, n);
    ui_set_text(l_c, b);
    if (TAP_BOARD) ui_set_text(l_h, pz_state == PZ_RESULT ? "Tocca o BOOT: il prossimo · dito tenuto: esci"
                                                          : "Tocca o swipe+BOOT: muovi · dito tenuto: esci");
    else ui_set_text(l_h, pz_state == PZ_RESULT ? "BOOT: il prossimo · dito tenuto: esci"
                                                : "Swipe: cursore · BOOT: scegli e muovi · dito tenuto: esci");
    cpos_t q = pz_pos;
    cb_draw(&B, &q);
}

static void pz_answer(cmove_t m)
{
    pz_move = m;
    B.last_from = m.from;
    B.last_to = m.to;
    if (cm_eq(m, pz.best)) {
        pz_ok = true;
        pz_state = PZ_RESULT;
        pz_done_ok++;
        pz_done_n++;
        cg_puzzle_result(pz_idx, true);
        return;
    }
    if (job_busy) return;
    // un'altra mossa può essere buona quanto quella del Gadget: la controlla il motore
    j_pos = pz_pos;
    j_test = m;
    pz_state = PZ_CHECK;
    job_start(J_CHECK);
    pz_gen = job_gen;
}

static void pz_tick(lv_timer_t *t)
{
    if (pz_state == PZ_CHECK && job_done && job_gen == pz_gen) {
        job_done = false;
        int d = j_score - j_score2;   // quanto è peggio della migliore (dal punto di vista di chi muove)
        pz_ok = d <= 30 || (j_score > CE_MATE - 200 && j_score2 > CE_MATE - 200);
        pz_state = PZ_RESULT;
        pz_done_n++;
        if (pz_ok) pz_done_ok++;
        cg_puzzle_result(pz_idx, pz_ok);
        pz_render();
        return;
    }
    int x, y;
    if (!cb_tap_poll(&x, &y) || !TAP_BOARD) return;
    if (pz_state == PZ_RESULT) { pz_next(); pz_render(); return; }
    if (pz_state != PZ_SOLVE) return;
    cmove_t m;
    bool promo = false;
    B.cursor = -1;
    if (pick_square(&pz_pos, cb_hit(&B, x, y), pz_pos.side, &m, &promo)) {
        if (promo) m.promo = CP_Q;
        pz_answer(m);
    }
    pz_render();
}

static void pz_enter(lv_obj_t *root, void *arg)
{
    init_once();
    build(root);
    if (pz_state == PZ_NONE || pz_state == PZ_CHECK) pz_next();
    pz_render();
    pz_tmr = lv_timer_create(pz_tick, 30, NULL);
}

static void pz_leave(void)
{
    if (pz_tmr) { lv_timer_delete(pz_tmr); pz_tmr = NULL; }
    if (ui_closing()) { if (pz_state == PZ_CHECK) job_cancel(); pz_state = PZ_NONE; }
}

static bool pz_nav(nav_t ev)
{
    if (ev == NAV_QUICK) { ui_pop(); return true; }
    if (pz_state == PZ_RESULT && (ev == NAV_BTN || ev == NAV_SELECT)) { pz_next(); pz_render(); return true; }
    if (pz_state == PZ_NONE) return false;
    if (ev == NAV_BTN && pz_state == PZ_SOLVE) {
        if (B.cursor < 0) { cb_move_cursor(&B, 0, 0); pz_render(); return true; }
        cmove_t m;
        bool promo = false;
        if (pick_square(&pz_pos, B.cursor, pz_pos.side, &m, &promo)) {
            if (promo) m.promo = CP_Q;
            pz_answer(m);
        }
        pz_render();
        return true;
    }
    if (pz_state == PZ_SOLVE && swipe_cursor(ev)) { pz_render(); return true; }
    return ev != NAV_BACK;
}

static const app_t app_ctrain = {
    .name = "Allenamento", .icon = ICON_GAMEPAD,
    .enter = pz_enter, .leave = pz_leave, .nav = pz_nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

/* ================= il tuo Elo ================= */

static void elo_enter(lv_obj_t *root, void *arg)
{
    init_once();
    static int16_t hist[120];
    int n = cg_elo_history(hist, 120);
    int won, drawn, lost, mx = cg_cfg.elo;
    cg_stats(&won, &drawn, &lost);
    for (int i = 0; i < n; i++) if (hist[i] > mx) mx = hist[i];
    char b[96];
    bool r = SCR_ROUND;
    lv_obj_t *big = mk(root, &font_xl, ui_accent());
    snprintf(b, sizeof(b), "%d", cg_cfg.elo);
    lv_label_set_text(big, b);
    lv_obj_t *l1 = mk(root, &font_s, C_TEXT), *l2 = mk(root, &font_s, C_DIM), *l3 = mk(root, &font_s, C_DIM);
    snprintf(b, sizeof(b), "Il tuo Elo · %d partite valide", cg_cfg.rated_games);
    lv_label_set_text(l1, b);
    snprintf(b, sizeof(b), "Vinte %d · patte %d · perse %d", won, drawn, lost);
    lv_label_set_text(l2, b);
    snprintf(b, sizeof(b), "Massimo %d · stima indicativa", mx);
    lv_label_set_text(l3, b);
    lv_obj_t *chart = lv_chart_create(root);
    if (r) {
        lv_obj_align(big, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_t *ls[] = {l1, l2, l3};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_width(ls[i], 330);
            lv_obj_set_style_text_align(ls[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(ls[i], LV_LABEL_LONG_WRAP);
        }
        lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 124);
        lv_obj_set_size(chart, 300, 110);
        lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 160);
        lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 284);
        lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 330);
    } else {
        lv_obj_set_pos(big, 12, 0);
        lv_obj_set_pos(l1, 14, 76);
        lv_obj_set_pos(l2, 14, 98);
        lv_obj_set_pos(l3, 14, 120);
        lv_obj_set_size(chart, SCR_W - 270, CONTENT_H - 20);
        lv_obj_set_pos(chart, 256, 8);
    }
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    int pts = n < 2 ? 2 : n;
    lv_chart_set_point_count(chart, pts);
    int lo = 10000, hi = 0;
    for (int i = 0; i < n; i++) { if (hist[i] < lo) lo = hist[i]; if (hist[i] > hi) hi = hist[i]; }
    if (n == 0) { lo = hi = cg_cfg.elo; }
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - 30, hi + 30);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_chart_series_t *ser = lv_chart_add_series(chart, ui_accent(), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < pts; i++) lv_chart_set_next_value(chart, ser, n ? hist[i < n ? i : n - 1] : cg_cfg.elo);
}

static const app_t app_celo = {
    .name = "Il tuo Elo", .icon = ICON_GAMEPAD, .enter = elo_enter, .flags = APP_ROUND_OK,
};

/* ================= menu degli Scacchi ================= */

static void v_play(char *b, int n)
{
    init_once();
    const char *c = cg_cfg.color == 0 ? "bianco" : cg_cfg.color == 1 ? "nero" : "a caso";
    if (cg_cfg.adaptive) snprintf(b, n, "Gadget al tuo livello (%d) · tu col %s", (cg_cfg.elo + 25) / 50 * 50, c);
    else snprintf(b, n, "Gadget %d · tu col %s", cg_cfg.bot_elo, c);
}
static void a_play(void)
{
    init_once();
    if (g_active && G->n) ui_toast("Nuova partita: quella interrotta non si riprende più");
    req = (start_t){CG_MODE_BOT, cg_cfg.color, cg_cfg.bot_elo, true};
    ui_push(&app_cgame, NULL);
}
static void v_resume(char *b, int n)
{
    init_once();
    if (g_active && G->n) snprintf(b, n, "%s · mossa %d", G->info.mode == CG_MODE_BOT ? "contro il Gadget" : "a due", G->n / 2 + 1);
    else if (cg_resume_exists()) snprintf(b, n, "Dalla microSD");
    else snprintf(b, n, "Nessuna partita in corso");
}
static void a_resume(void)
{
    init_once();
    if (!(g_active && G->n) && !cg_resume_exists()) { ui_toast("Nessuna partita in corso"); return; }
    req.fresh = false;
    ui_push(&app_cgame, NULL);
}
static void a_two(void)
{
    init_once();
    req = (start_t){CG_MODE_TWO, 0, 0, true};
    ui_push(&app_cgame, NULL);
}
static void v_games(char *b, int n)
{
    if (!cg_store_ok()) { snprintf(b, n, "Serve la microSD"); return; }
    snprintf(b, n, "Rivedi e analizza le partite");
}
static void v_train(char *b, int n)
{
    int todo, t = cg_puzzle_count(&todo);
    if (!t) snprintf(b, n, "Gli errori delle partite analizzate");
    else snprintf(b, n, "%d esercizi, %d da risolvere", t, todo);
}
static void v_elo(char *b, int n) { init_once(); snprintf(b, n, "%d · %d partite valide", cg_cfg.elo, cg_cfg.rated_games); }
static void a_export(void)
{
    init_once();
    char p[64];
    if (!cg_export_llm(p, sizeof(p))) { ui_toast(cg_store_ok() ? "Non riesco a scrivere sulla microSD" : "Serve la microSD"); return; }
    // e subito sul telefono: QR dell'hotspot, pagina con Copia tutto e Scarica
    static const share_req_t sr = {SD_MOUNT "/scacchi/per_IA.txt", "scacchi-per-IA.txt", "Scacchi per un'IA"};
    ui_push(&app_share, (void *)&sr);
}
static void v_level(char *b, int n)
{
    init_once();
    if (cg_cfg.adaptive) snprintf(b, n, "Adattivo: al tuo Elo");
    else snprintf(b, n, "Elo %d%s", cg_cfg.bot_elo, cg_cfg.bot_elo < 800 ? " · principiante" : cg_cfg.bot_elo < 1400 ? " · amatore"
                                                    : cg_cfg.bot_elo < 1900 ? " · da circolo" : " · forte");
}
static void j_level(int d)
{
    // 400 … 2400 a passi di 100, poi "adattivo"
    if (cg_cfg.adaptive) { cg_cfg.adaptive = 0; cg_cfg.bot_elo = d > 0 ? 400 : 2400; }
    else {
        int e = cg_cfg.bot_elo + d * 100;
        if (e > 2400 || e < 400) cg_cfg.adaptive = 1;
        else cg_cfg.bot_elo = e;
    }
    cg_cfg_save();
}
static void v_color(char *b, int n) { snprintf(b, n, "%s", cg_cfg.color == 0 ? "Bianco" : cg_cfg.color == 1 ? "Nero" : "A caso"); }
static void j_color(int d) { cg_cfg.color = (cg_cfg.color + d + 3) % 3; cg_cfg_save(); }
static void v_not(char *b, int n) { snprintf(b, n, "%s", cg_cfg.notation_it ? "Italiana (C A T D R)" : "Inglese (N B R Q K)"); }
static void a_not(void) { cg_cfg.notation_it = !cg_cfg.notation_it; cg_cfg_save(); }
static void v_marks(char *b, int n) { snprintf(b, n, "%s", cg_cfg.show_moves ? "Sì" : "No"); }
static void a_marks(void) { cg_cfg.show_moves = !cg_cfg.show_moves; cg_cfg_save(); }
static void v_flip(char *b, int n) { snprintf(b, n, "%s", cg_cfg.flip_two ? "Sì, a ogni mossa" : "No"); }
static void a_flip(void) { cg_cfg.flip_two = !cg_cfg.flip_two; cg_cfg_save(); }

static void menu_close(void) { cg_cfg_save(); }

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Gioca contro il Gadget", .value = v_play, .on_select = a_play},
    {.icon = LV_SYMBOL_REFRESH, .label = "Riprendi la partita", .value = v_resume, .on_select = a_resume},
    {.icon = ICON_GAMEPAD, .label = "Partita a due", .hint = "Sulla stessa scacchiera, senza motore", .on_select = a_two},
    {.icon = LV_SYMBOL_LIST, .label = "Partite giocate", .value = v_games, .app = &app_clist},
    {.icon = ICON_EYE, .label = "Allenamento", .value = v_train, .app = &app_ctrain},
    {.icon = ICON_SLIDERS, .label = "Il tuo Elo", .value = v_elo, .app = &app_celo},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Esporta per un'IA", .hint = "Sul telefono con un QR (e in scacchi/per_IA.txt)", .on_select = a_export},
    {.icon = ICON_CHIP, .label = "Livello del Gadget", .value = v_level, .on_adjust = j_level},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Il tuo colore", .value = v_color, .on_adjust = j_color},
    {.icon = LV_SYMBOL_EDIT, .label = "Notazione", .value = v_not, .on_select = a_not},
    {.icon = LV_SYMBOL_EYE_OPEN, .label = "Mostra le mosse possibili", .value = v_marks, .on_select = a_marks},
    {.icon = LV_SYMBOL_LOOP, .label = "A due: gira la scacchiera", .value = v_flip, .on_select = a_flip},
};

menu_t chessplay_menu = {"Scacchi", items, sizeof(items) / sizeof(items[0]), 0, menu_close};
