// chess_test.c — prove del motore sul PC: perft (conteggio delle mosse legali su posizioni
// note), notazione, velocità e partite fra livelli diversi.
// gcc -O2 -DCE_HOST -Imain tools/chess/chess_test.c main/chess_engine.c -lm -o /tmp/ct && /tmp/ct
#include "chess_engine.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

static uint32_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static uint64_t perft(const cpos_t *p, int d)
{
    cmove_t m[CP_MAXMOVES];
    int n = cp_legal(p, m);
    if (d == 1) return n;
    uint64_t t = 0;
    for (int i = 0; i < n; i++) { cpos_t q = *p; cp_make(&q, m[i]); t += perft(&q, d - 1); }
    return t;
}

int main(int argc, char **argv)
{
    ce_now_ms = now;
    ce_init(16);
    static const struct { const char *fen; int d; uint64_t n; } T[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
    };
    int bad = 0;
    for (int i = 0; i < 6; i++) {
        cpos_t p;
        cp_from_fen(&p, T[i].fen);
        uint32_t t0 = now();
        uint64_t n = perft(&p, T[i].d);
        printf("perft %d: %llu %s (%u ms)\n", i, (unsigned long long)n, n == T[i].n ? "ok" : "SBAGLIATO", now() - t0);
        if (n != T[i].n) bad++;
        char f[100];
        cp_to_fen(&p, f);
        if (strncmp(f, T[i].fen, strlen(f) - 4)) { printf("  FEN diversa: %s\n", f); bad++; }
    }
    // notazione: andata e ritorno su tutte le mosse di una posizione complessa
    {
        cpos_t p;
        cp_from_fen(&p, T[1].fen);
        cmove_t m[CP_MAXMOVES], q;
        int n = cp_legal(&p, m);
        char s[16], it[16];
        for (int i = 0; i < n; i++) {
            cp_san(&p, m[i], s);
            if (!cp_parse_san(&p, s, &q) || !cm_eq(q, m[i])) { printf("SAN non riletta: %s\n", s); bad++; }
        }
        cp_san(&p, m[0], s); cp_san_it(s, it);
        printf("SAN ok (%d mosse), es. %s / %s\n", n, s, it);
    }
    // matto in 2 e velocità
    {
        cpos_t p;
        cresult_t r;
        cp_from_fen(&p, "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
        ce_search(&p, NULL, 0, 6, 2000, &r);
        char s[16]; cp_san(&p, r.best, s);
        printf("matto del barbiere: %s (%d)\n", s, r.score);
        if (strcmp(s, "Qxf7#")) bad++;
        cp_start(&p);
        ce_clear();
        uint32_t t0 = now();
        ce_search(&p, NULL, 0, 7, 0, &r);
        uint32_t dt = now() - t0;
        cp_san(&p, r.best, s);
        printf("apertura prof. %d: %s %d cp, %u nodi in %u ms (%u nodi/s)\n", r.depth, s, r.score, r.nodes, dt, dt ? r.nodes * 1000 / dt : 0);
    }
    // partite fra livelli: elo A (bianco) contro elo B
    if (argc > 2) {
        int ea = atoi(argv[1]), eb = atoi(argv[2]), games = argc > 3 ? atoi(argv[3]) : 10, pts2 = 0;
        for (int g = 0; g < games; g++) {
            cpos_t p; cp_start(&p); ce_clear();
            static uint64_t h[600]; int nh = 0, ply = 0;
            int wa = g & 1;   // colori alternati
            cstatus_t st;
            while ((st = cp_status(&p, h, nh)) == CS_PLAY && ply < 300) {
                int elo = (p.side == 0) == !wa ? ea : eb;
                ce_now_ms = now;
                cmove_t m = ce_play(&p, h, nh, elo, ply, g * 1000 + ply + 7, NULL);
                h[nh++] = p.hash;
                cp_make(&p, m);
                ply++;
            }
            int res = st == CS_MATE ? (p.side ? 2 : 0) : 1;   // 2 = vince il bianco
            int a_pts = wa ? 2 - res : res;
            pts2 += a_pts;
            printf("partita %d: %s, %d semimosse, A %s\n", g, st == CS_MATE ? "matto" : "patta", ply, a_pts == 2 ? "vince" : a_pts ? "patta" : "perde");
        }
        printf("A (%d) contro B (%d): %.1f/%d\n", ea, eb, pts2 / 2.0, games);
    }
    int bb = ce_book_check();
    printf("libro delle aperture: %s\n", bb ? "linee sbagliate" : "ok");
    bad += bb;
    printf(bad ? "ERRORI: %d\n" : "tutto ok\n", bad);
    return bad;
}
