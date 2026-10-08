// store_test.c — prova dell'archivio degli Scacchi sul PC (la "microSD" è /sdcard):
// una partita fra due livelli, salvataggio, analisi, rilettura del PGN, Elo, esercizi, esportazione.
// gcc -O2 -DCE_HOST -Imain -Itools/sim/inc tools/chess/store_test.c main/chess_game.c main/chess_engine.c -lm
#include "chess_game.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

bool sd_ok(void) { return true; }
static uint32_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static bool prog(int k, int n) { if (k == n) printf("analisi: %d/%d\n", k, n); return true; }

int main(void)
{
    ce_now_ms = now;
    ce_init(16);
    cg_cfg_load();
    static cgame_t g, h;
    cg_new(&g, CG_MODE_BOT, 0, 1500);
    cpos_t p; cp_start(&p);
    static uint64_t hist[CG_MAXPLY];
    while (cp_status(&p, hist, g.n) == CS_PLAY && g.n < 160) {
        cmove_t m = ce_play(&p, hist, g.n, (g.n & 1) ? 1500 : 1100, g.n, g.n + 3, NULL);
        hist[g.n] = p.hash;
        g.mv[g.n++] = m;
        cp_make(&p, m);
    }
    cstatus_t st = cp_status(&p, hist, g.n);
    g.info.result = st == CS_MATE ? (p.side ? CG_RES_WHITE : CG_RES_BLACK) : st == CS_PLAY ? CG_RES_NONE : CG_RES_DRAW;
    g.info.end = st == CS_MATE ? CG_END_MATE : st == CS_PLAY ? CG_END_NONE : CG_END_REPETITION;
    g.info.rated = 1;
    printf("partita: %d semimosse, %s\n", g.n, cg_result_str(g.info.result));
    int elo = cg_elo_update(&g, g.info.result == CG_RES_WHITE ? 1 : g.info.result == CG_RES_DRAW ? 0.5f : 0);
    printf("Elo: %d -> %d\n", g.info.elo_before, elo);
    if (!cg_save(&g)) { printf("salvataggio fallito\n"); return 1; }
    uint32_t t0 = now();
    cg_analyze(&g, 40, prog);
    printf("analisi in %u ms, precisione bianco %d%% nero %d%%\n", now() - t0, g.info.acc[0], g.info.acc[1]);
    int cnt[6] = {0};
    for (int k = 0; k < g.n; k++) cnt[g.cls[k]]++;
    printf("giudizi: migliori %d buone %d imprecisioni %d errori %d gravi %d\n", cnt[1], cnt[2], cnt[3], cnt[4], cnt[5]);
    cg_save(&g);
    if (!cg_load(g.info.id, &h)) { printf("rilettura fallita\n"); return 1; }
    int bad = h.n != g.n;
    for (int k = 0; k < g.n && k < h.n; k++) {
        if (!cm_eq(h.mv[k], g.mv[k])) { bad++; printf("mossa %d diversa\n", k); break; }
        if (g.cls[k] >= CG_CLS_INACC && (h.cls[k] != g.cls[k] || !cm_eq(h.best[k], g.best[k]))) { bad++; printf("analisi %d diversa\n", k); break; }
    }
    for (int k = 0; k <= g.n; k++) {
        int a = g.eval[k], b = h.eval[k];
        if ((a >= 9000) != (b >= 9000) || (a > -9000 && a < 9000 && a != b)) { bad++; printf("eval %d diversa %d %d\n", k, a, b); break; }
    }
    printf("rilettura: %s (id %d, %d semimosse, acc %d/%d, analizzata %d)\n", bad ? "DIVERSA" : "ok", h.info.id, h.n, h.info.acc[0], h.info.acc[1], h.analyzed);
    cg_info_t L[10];
    int n = cg_list(L, 10);
    printf("indice: %d partite, ultima id %d %s\n", n, L[0].id, L[0].date);
    int todo, np = cg_puzzle_count(&todo);
    cg_puzzle_t z; int zi;
    if (cg_puzzle_pick(&z, &zi)) { char b[8]; cp_uci(z.best, b); printf("esercizi: %d (%d da fare), es. %s -> %s\n", np, todo, z.fen, b); cg_puzzle_result(zi, true); }
    cg_resume_save(&g);
    cgame_t *r = &h;
    if (!cg_resume_load(r) || r->n != g.n) { bad++; printf("ripresa diversa\n"); }
    cg_resume_clear();
    char ep[64];
    if (cg_export_llm(ep, sizeof(ep))) printf("esportato %s\n", ep);
    return bad;
}
