// chess_engine.c — regole e motore (vedi chess_engine.h)
#include "chess_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#ifndef CE_HOST
#include "esp_heap_caps.h"
#include "esp_attr.h"
#define TT_ALLOC(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM)
#else
#define TT_ALLOC(n) calloc(1, (n))
#define EXT_RAM_BSS_ATTR
#endif
// le tabelle grandi stanno in PSRAM: la RAM interna serve a Wi-Fi e Bluetooth

/* ================= hash di Zobrist ================= */

EXT_RAM_BSS_ATTR static uint64_t Z_PIECE[16][128];
static uint64_t Z_SIDE, Z_CASTLE[16], Z_EP[8];
static uint8_t CASTLE_MASK[128];

static uint64_t rnd64(uint64_t *s)
{
    // xorshift64*: numeri fissi, la stessa posizione ha sempre lo stesso hash
    *s ^= *s >> 12; *s ^= *s << 25; *s ^= *s >> 27;
    return *s * 2685821657736338717ULL;
}

void cp_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for (int p = 0; p < 16; p++)
        for (int q = 0; q < 128; q++) Z_PIECE[p][q] = rnd64(&s);
    Z_SIDE = rnd64(&s);
    for (int i = 0; i < 16; i++) Z_CASTLE[i] = rnd64(&s);
    for (int i = 0; i < 8; i++) Z_EP[i] = rnd64(&s);
    memset(CASTLE_MASK, 15, sizeof(CASTLE_MASK));
    CASTLE_MASK[CP_SQ(4, 0)] = 15 & ~3;   // re bianco
    CASTLE_MASK[CP_SQ(7, 0)] = 15 & ~1;
    CASTLE_MASK[CP_SQ(0, 0)] = 15 & ~2;
    CASTLE_MASK[CP_SQ(4, 7)] = 15 & ~12;  // re nero
    CASTLE_MASK[CP_SQ(7, 7)] = 15 & ~4;
    CASTLE_MASK[CP_SQ(0, 7)] = 15 & ~8;
}

static uint64_t full_hash(const cpos_t *p)
{
    uint64_t h = 0;
    for (int s = 0; s < 128; s++) if (!(s & 0x88) && p->sq[s]) h ^= Z_PIECE[p->sq[s]][s];
    if (p->side) h ^= Z_SIDE;
    h ^= Z_CASTLE[p->castle];
    if (p->ep >= 0) h ^= Z_EP[CP_FILE(p->ep)];
    return h;
}

/* ================= posizione ================= */

bool cp_from_fen(cpos_t *p, const char *fen)
{
    cp_init();
    memset(p, 0, sizeof(*p));
    p->ep = -1;
    int r = 7, f = 0;
    const char *c = fen;
    for (; *c && *c != ' '; c++) {
        if (*c == '/') { r--; f = 0; continue; }
        if (isdigit((unsigned char)*c)) { f += *c - '0'; continue; }
        const char *T = strchr("pnbrqk", tolower((unsigned char)*c));
        if (!T || f > 7 || r < 0) return false;
        int t = (int)(T - "pnbrqk") + 1, col = islower((unsigned char)*c) ? CP_BLACK : 0;
        p->sq[CP_SQ(f, r)] = t | col;
        if (t == CP_K) p->ksq[col ? 1 : 0] = CP_SQ(f, r);
        f++;
    }
    if (*c) c++;
    p->side = *c == 'b';
    while (*c && *c != ' ') c++;
    if (*c) c++;
    for (; *c && *c != ' '; c++) {
        if (*c == 'K') p->castle |= 1;
        if (*c == 'Q') p->castle |= 2;
        if (*c == 'k') p->castle |= 4;
        if (*c == 'q') p->castle |= 8;
    }
    if (*c) c++;
    if (*c >= 'a' && *c <= 'h' && c[1] >= '1' && c[1] <= '8') { p->ep = CP_SQ(*c - 'a', c[1] - '1'); c += 2; }
    while (*c && *c != ' ') c++;
    int hm = 0, fm = 1;
    if (*c) sscanf(c, "%d %d", &hm, &fm);
    p->halfmove = hm;
    p->fullmove = fm < 1 ? 1 : fm;
    p->hash = full_hash(p);
    return true;
}

void cp_start(cpos_t *p) { cp_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

void cp_to_fen(const cpos_t *p, char *o)
{
    for (int r = 7; r >= 0; r--) {
        int e = 0;
        for (int f = 0; f < 8; f++) {
            int pc = p->sq[CP_SQ(f, r)];
            if (!pc) { e++; continue; }
            if (e) { *o++ = '0' + e; e = 0; }
            char ch = "?pnbrqk"[CP_TYPE(pc)];
            *o++ = CP_COLOR(pc) ? ch : (char)toupper((unsigned char)ch);
        }
        if (e) *o++ = '0' + e;
        if (r) *o++ = '/';
    }
    *o++ = ' ';
    *o++ = p->side ? 'b' : 'w';
    *o++ = ' ';
    if (!p->castle) *o++ = '-';
    if (p->castle & 1) *o++ = 'K';
    if (p->castle & 2) *o++ = 'Q';
    if (p->castle & 4) *o++ = 'k';
    if (p->castle & 8) *o++ = 'q';
    *o++ = ' ';
    if (p->ep >= 0) { *o++ = 'a' + CP_FILE(p->ep); *o++ = '1' + CP_RANK(p->ep); }
    else *o++ = '-';
    sprintf(o, " %d %d", p->halfmove, p->fullmove);
}

/* ================= attacchi e mosse ================= */

static const int8_t N_OFF[8] = {33, 31, 18, 14, -33, -31, -18, -14};
static const int8_t K_OFF[8] = {1, -1, 16, -16, 15, 17, -15, -17};
static const int8_t B_DIR[4] = {15, 17, -15, -17};
static const int8_t R_DIR[4] = {1, -1, 16, -16};

bool cp_attacked(const cpos_t *p, int sq, int by)
{
    int col = by ? CP_BLACK : 0;
    // pedoni: il bianco attacca verso l'alto, quindi sta sotto la casa
    if (by == 0) {
        int a = sq - 15, b = sq - 17;
        if (!(a & 0x88) && p->sq[a] == (CP_P | col)) return true;
        if (!(b & 0x88) && p->sq[b] == (CP_P | col)) return true;
    } else {
        int a = sq + 15, b = sq + 17;
        if (!(a & 0x88) && p->sq[a] == (CP_P | col)) return true;
        if (!(b & 0x88) && p->sq[b] == (CP_P | col)) return true;
    }
    for (int i = 0; i < 8; i++) {
        int t = sq + N_OFF[i];
        if (!(t & 0x88) && p->sq[t] == (CP_N | col)) return true;
        t = sq + K_OFF[i];
        if (!(t & 0x88) && p->sq[t] == (CP_K | col)) return true;
    }
    for (int i = 0; i < 4; i++) {
        for (int t = sq + B_DIR[i]; !(t & 0x88); t += B_DIR[i]) {
            int pc = p->sq[t];
            if (!pc) continue;
            if (pc == (CP_B | col) || pc == (CP_Q | col)) return true;
            break;
        }
        for (int t = sq + R_DIR[i]; !(t & 0x88); t += R_DIR[i]) {
            int pc = p->sq[t];
            if (!pc) continue;
            if (pc == (CP_R | col) || pc == (CP_Q | col)) return true;
            break;
        }
    }
    return false;
}

static inline void add(cmove_t *o, int *n, int f, int t, int promo, int flags)
{
    o[(*n)++] = (cmove_t){(uint8_t)f, (uint8_t)t, (uint8_t)promo, (uint8_t)flags};
}

static void add_pawn(cmove_t *o, int *n, int f, int t, int flags, int last_rank)
{
    if (CP_RANK(t) == last_rank) {
        add(o, n, f, t, CP_Q, flags | CM_PROMO);
        add(o, n, f, t, CP_N, flags | CM_PROMO);
        add(o, n, f, t, CP_R, flags | CM_PROMO);
        add(o, n, f, t, CP_B, flags | CM_PROMO);
    } else add(o, n, f, t, 0, flags);
}

// pseudo-legali; captures_only: solo prese e promozioni (quiescenza)
static int gen(const cpos_t *p, cmove_t *o, bool captures_only)
{
    int n = 0, us = p->side, them = us ^ 1;
    int col = us ? CP_BLACK : 0;
    int fwd = us ? -16 : 16, start = us ? 6 : 1, last = us ? 0 : 7;
    for (int s = 0; s < 128; s++) {
        if (s & 0x88) { s += 7; continue; }
        int pc = p->sq[s];
        if (!pc || CP_COLOR(pc) != us) continue;
        int t = CP_TYPE(pc);
        if (t == CP_P) {
            int to = s + fwd;
            if (!(to & 0x88) && !p->sq[to]) {
                if (!captures_only || CP_RANK(to) == last) add_pawn(o, &n, s, to, 0, last);
                if (!captures_only && CP_RANK(s) == start && !p->sq[to + fwd]) add(o, &n, s, to + fwd, 0, CM_DOUBLE);
            }
            for (int k = -1; k <= 1; k += 2) {
                int c = s + fwd + k;
                if (c & 0x88) continue;
                if (p->sq[c] && CP_COLOR(p->sq[c]) == them) add_pawn(o, &n, s, c, CM_CAPTURE, last);
                else if (c == p->ep) add(o, &n, s, c, 0, CM_CAPTURE | CM_EP);
            }
            continue;
        }
        if (t == CP_N || t == CP_K) {
            const int8_t *off = t == CP_N ? N_OFF : K_OFF;
            for (int i = 0; i < 8; i++) {
                int to = s + off[i];
                if (to & 0x88) continue;
                int q = p->sq[to];
                if (!q) { if (!captures_only) add(o, &n, s, to, 0, 0); }
                else if (CP_COLOR(q) == them) add(o, &n, s, to, 0, CM_CAPTURE);
            }
            continue;
        }
        for (int i = 0; i < 8; i++) {   // alfiere: diagonali, torre: ortogonali, donna: tutte
            int d = i < 4 ? B_DIR[i] : R_DIR[i - 4];
            if (t == CP_B && i >= 4) break;
            if (t == CP_R && i < 4) continue;
            for (int to = s + d; !(to & 0x88); to += d) {
                int q = p->sq[to];
                if (!q) { if (!captures_only) add(o, &n, s, to, 0, 0); continue; }
                if (CP_COLOR(q) == them) add(o, &n, s, to, 0, CM_CAPTURE);
                break;
            }
        }
    }
    (void)col;
    // arrocco
    if (!captures_only) {
        int r = us ? 7 : 0, k = CP_SQ(4, r);
        int ks = us ? 4 : 1, qs = us ? 8 : 2;
        if ((p->castle & ks) && !p->sq[k + 1] && !p->sq[k + 2] &&
            !cp_attacked(p, k, them) && !cp_attacked(p, k + 1, them) && !cp_attacked(p, k + 2, them))
            add(o, &n, k, k + 2, 0, CM_CASTLE);
        if ((p->castle & qs) && !p->sq[k - 1] && !p->sq[k - 2] && !p->sq[k - 3] &&
            !cp_attacked(p, k, them) && !cp_attacked(p, k - 1, them) && !cp_attacked(p, k - 2, them))
            add(o, &n, k, k - 2, 0, CM_CASTLE);
    }
    return n;
}

void cp_make(cpos_t *p, cmove_t m)
{
    int us = p->side, col = us ? CP_BLACK : 0;
    int pc = p->sq[m.from], cap = p->sq[m.to];
    uint64_t h = p->hash;
    if (p->ep >= 0) h ^= Z_EP[CP_FILE(p->ep)];
    h ^= Z_CASTLE[p->castle];
    p->ep = -1;
    p->halfmove++;
    if (cap) { h ^= Z_PIECE[cap][m.to]; p->halfmove = 0; }
    h ^= Z_PIECE[pc][m.from];
    p->sq[m.from] = 0;
    int np = m.promo ? (m.promo | col) : pc;
    p->sq[m.to] = np;
    h ^= Z_PIECE[np][m.to];
    if (CP_TYPE(pc) == CP_P) {
        p->halfmove = 0;
        if (m.flags & CM_EP) {
            int cs = m.to + (us ? 16 : -16);
            h ^= Z_PIECE[p->sq[cs]][cs];
            p->sq[cs] = 0;
        } else if (m.flags & CM_DOUBLE) {
            // en passant solo se un pedone avversario può davvero prendere (hash più stabili)
            int e = (m.from + m.to) / 2, them = (us ^ 1) ? CP_BLACK : 0;
            int a = m.to - 1, b = m.to + 1;
            if ((!(a & 0x88) && p->sq[a] == (CP_P | them)) || (!(b & 0x88) && p->sq[b] == (CP_P | them))) p->ep = e;
        }
    }
    if (CP_TYPE(pc) == CP_K) {
        p->ksq[us] = m.to;
        if (m.flags & CM_CASTLE) {
            int rf = m.to > m.from ? m.from + 3 : m.from - 4, rt = m.to > m.from ? m.from + 1 : m.from - 1;
            int rk = p->sq[rf];
            h ^= Z_PIECE[rk][rf] ^ Z_PIECE[rk][rt];
            p->sq[rt] = rk;
            p->sq[rf] = 0;
        }
    }
    p->castle &= CASTLE_MASK[m.from] & CASTLE_MASK[m.to];
    h ^= Z_CASTLE[p->castle];
    if (p->ep >= 0) h ^= Z_EP[CP_FILE(p->ep)];
    if (us) p->fullmove++;
    p->side ^= 1;
    h ^= Z_SIDE;
    p->hash = h;
}

int cp_legal(const cpos_t *p, cmove_t *out)
{
    cmove_t buf[CP_MAXMOVES];
    int n = gen(p, buf, false), k = 0;
    for (int i = 0; i < n; i++) {
        cpos_t q = *p;
        cp_make(&q, buf[i]);
        if (!cp_attacked(&q, q.ksq[p->side], q.side)) out[k++] = buf[i];
    }
    return k;
}

/* ================= notazione ================= */

void cp_uci(cmove_t m, char *o)
{
    o[0] = 'a' + CP_FILE(m.from); o[1] = '1' + CP_RANK(m.from);
    o[2] = 'a' + CP_FILE(m.to);   o[3] = '1' + CP_RANK(m.to);
    int k = 4;
    if (m.promo) o[k++] = "?pnbrqk"[m.promo];
    o[k] = 0;
}

bool cp_parse_uci(const cpos_t *p, const char *u, cmove_t *m)
{
    cmove_t l[CP_MAXMOVES];
    int n = cp_legal(p, l);
    char b[8];
    for (int i = 0; i < n; i++) { cp_uci(l[i], b); if (!strncmp(b, u, strlen(b)) && (!u[strlen(b)] || u[strlen(b)] == ' ')) { *m = l[i]; return true; } }
    return false;
}

void cp_san(const cpos_t *p, cmove_t m, char *o)
{
    int pc = p->sq[m.from], t = CP_TYPE(pc);
    char *s = o;
    if (m.flags & CM_CASTLE) s += sprintf(s, CP_FILE(m.to) == 6 ? "O-O" : "O-O-O");
    else {
        if (t == CP_P) {
            if (m.flags & CM_CAPTURE) { *s++ = 'a' + CP_FILE(m.from); *s++ = 'x'; }
        } else {
            *s++ = " PNBRQK"[t];
            // disambiguazione: altri pezzi uguali che possono andare nella stessa casa
            cmove_t l[CP_MAXMOVES];
            int n = cp_legal(p, l);
            bool amb = false, same_file = false, same_rank = false;
            for (int i = 0; i < n; i++) {
                if (l[i].to != m.to || l[i].from == m.from || p->sq[l[i].from] != pc) continue;
                amb = true;
                if (CP_FILE(l[i].from) == CP_FILE(m.from)) same_file = true;
                if (CP_RANK(l[i].from) == CP_RANK(m.from)) same_rank = true;
            }
            if (amb) {
                if (!same_file) *s++ = 'a' + CP_FILE(m.from);
                else if (!same_rank) *s++ = '1' + CP_RANK(m.from);
                else { *s++ = 'a' + CP_FILE(m.from); *s++ = '1' + CP_RANK(m.from); }
            }
            if (m.flags & CM_CAPTURE) *s++ = 'x';
        }
        *s++ = 'a' + CP_FILE(m.to);
        *s++ = '1' + CP_RANK(m.to);
        if (m.promo) { *s++ = '='; *s++ = " PNBRQK"[m.promo]; }
    }
    cpos_t q = *p;
    cp_make(&q, m);
    if (cp_in_check(&q)) {
        cmove_t l[CP_MAXMOVES];
        *s++ = cp_legal(&q, l) ? '+' : '#';
    }
    *s = 0;
}

void cp_san_it(const char *san, char *o)
{
    // N B R Q K → C A T D R (solo le maiuscole dei pezzi: le colonne sono minuscole)
    for (; *san; san++) {
        char c = *san;
        if (c == 'N') c = 'C';
        else if (c == 'B') c = 'A';
        else if (c == 'R') c = 'T';
        else if (c == 'Q') c = 'D';
        else if (c == 'K') c = 'R';
        *o++ = c;
    }
    *o = 0;
}

static void strip(const char *in, char *out, int n)
{
    int k = 0;
    for (; *in && k < n - 1; in++) {
        if (strchr("+#!?", *in)) continue;
        out[k++] = *in == '0' ? 'O' : *in;   // anche 0-0
    }
    out[k] = 0;
}

bool cp_parse_san(const cpos_t *p, const char *san, cmove_t *m)
{
    char want[16], got[16], b[16];
    strip(san, want, sizeof(want));
    cmove_t l[CP_MAXMOVES];
    int n = cp_legal(p, l);
    for (int i = 0; i < n; i++) {
        cp_san(p, l[i], b);
        strip(b, got, sizeof(got));
        if (!strcmp(got, want)) { *m = l[i]; return true; }
    }
    // promozione senza "=" (e8Q)
    for (int i = 0; i < n; i++) {
        if (!l[i].promo) continue;
        cp_san(p, l[i], b);
        strip(b, got, sizeof(got));
        char *eq = strchr(got, '=');
        if (eq) memmove(eq, eq + 1, strlen(eq));
        if (!strcmp(got, want)) { *m = l[i]; return true; }
    }
    return false;
}

static bool insufficient(const cpos_t *p)
{
    int minors = 0, bishops_sq[2] = {0, 0};
    for (int s = 0; s < 128; s++) {
        if (s & 0x88) { s += 7; continue; }
        int t = CP_TYPE(p->sq[s]);
        if (!t || t == CP_K) continue;
        if (t == CP_P || t == CP_R || t == CP_Q) return false;
        minors++;
        if (t == CP_B) bishops_sq[(CP_FILE(s) + CP_RANK(s)) & 1]++;
    }
    if (minors <= 1) return true;
    // solo alfieri tutti sullo stesso colore
    return (bishops_sq[0] == minors) || (bishops_sq[1] == minors);
}

cstatus_t cp_status(const cpos_t *p, const uint64_t *hist, int nhist)
{
    cmove_t l[CP_MAXMOVES];
    if (!cp_legal(p, l)) return cp_in_check(p) ? CS_MATE : CS_STALEMATE;
    if (p->halfmove >= 100) return CS_FIFTY;
    if (insufficient(p)) return CS_MATERIAL;
    int rep = 0;
    for (int i = nhist - 1; i >= 0 && i >= nhist - p->halfmove; i--) if (hist[i] == p->hash) rep++;
    if (rep >= 2) return CS_REPETITION;
    return CS_PLAY;
}

/* ================= valutazione ================= */

static const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};

// tabelle (dal punto di vista del bianco, traversa 8 in alto): "Simplified Evaluation Function"
static const int8_t PST[7][64] = {
    {0},
    { 0,  0,  0,  0,  0,  0,  0,  0, 50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,  0,  0,  0, 20, 20,  0,  0,  0,  5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0},
    {-50,-40,-30,-30,-30,-30,-40,-50,-40,-20,  0,  0,  0,  0,-20,-40,-30,  0, 10, 15, 15, 10,  0,-30,
     -30,  5, 15, 20, 20, 15,  5,-30,-30,  0, 15, 20, 20, 15,  0,-30,-30,  5, 10, 15, 15, 10,  5,-30,
     -40,-20,  0,  5,  5,  0,-20,-40,-50,-40,-30,-30,-30,-30,-40,-50},
    {-20,-10,-10,-10,-10,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,-10,  0,  5, 10, 10,  5,  0,-10,
     -10,  5,  5, 10, 10,  5,  5,-10,-10,  0, 10, 10, 10, 10,  0,-10,-10, 10, 10, 10, 10, 10, 10,-10,
     -10,  5,  0,  0,  0,  0,  5,-10,-20,-10,-10,-10,-10,-10,-10,-20},
    { 0,  0,  0,  0,  0,  0,  0,  0,  5, 10, 10, 10, 10, 10, 10,  5, -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,  0,  0,  0,  5,  5,  0,  0,  0},
    {-20,-10,-10, -5, -5,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,-10,  0,  5,  5,  5,  5,  0,-10,
      -5,  0,  5,  5,  5,  5,  0, -5,  0,  0,  5,  5,  5,  5,  0, -5,-10,  5,  5,  5,  5,  5,  0,-10,
     -10,  0,  5,  0,  0,  0,  0,-10,-20,-10,-10, -5, -5,-10,-10,-20},
    {-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,-20,-30,-30,-40,-40,-30,-30,-20,-10,-20,-20,-20,-20,-20,-20,-10,
      20, 20,  0,  0,  0,  0, 20, 20, 20, 30, 10,  0,  0, 10, 30, 20},
};
static const int8_t KING_EG[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,-30,-20,-10,  0,  0,-10,-20,-30,-30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,-30,-10, 30, 40, 40, 30,-10,-30,-30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,-50,-30,-30,-30,-30,-30,-30,-50};

int ce_eval(const cpos_t *p)
{
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0, bishops[2] = {0, 0};
    int pawns_file[2][8] = {{0}};
    for (int s = 0; s < 128; s++) {
        if (s & 0x88) { s += 7; continue; }
        int pc = p->sq[s];
        if (!pc) continue;
        int t = CP_TYPE(pc), c = CP_COLOR(pc);
        int f = CP_FILE(s), r = CP_RANK(s);
        int idx = c ? r * 8 + f : (7 - r) * 8 + f;   // il nero si specchia
        if (t == CP_K) { mg[c] += PST[CP_K][idx]; eg[c] += KING_EG[idx]; continue; }
        int v = VAL[t] + PST[t][idx];
        mg[c] += v;
        eg[c] += v;
        if (t == CP_P) {
            pawns_file[c][f]++;
            int adv = c ? 6 - r : r - 1;   // pedoni avanzati contano di più nel finale
            eg[c] += adv * adv * 2;
        }
        if (t == CP_B) bishops[c]++;
        if (t == CP_N || t == CP_B) phase += 1;
        if (t == CP_R) phase += 2;
        if (t == CP_Q) phase += 4;
    }
    for (int c = 0; c < 2; c++) {
        if (bishops[c] >= 2) { mg[c] += 30; eg[c] += 40; }
        for (int f = 0; f < 8; f++) {
            int n = pawns_file[c][f];
            if (!n) continue;
            if (n > 1) { mg[c] -= 10 * (n - 1); eg[c] -= 20 * (n - 1); }   // doppiati
            bool iso = (f == 0 || !pawns_file[c][f - 1]) && (f == 7 || !pawns_file[c][f + 1]);
            if (iso) { mg[c] -= 10 * n; eg[c] -= 15 * n; }
        }
    }
    if (phase > 24) phase = 24;
    int score = ((mg[0] - mg[1]) * phase + (eg[0] - eg[1]) * (24 - phase)) / 24;
    return (p->side ? -score : score) + 10;   // +10: avere la mossa
}

/* ================= ricerca ================= */

uint32_t (*ce_now_ms)(void);
void (*ce_yield)(void);
volatile bool ce_abort;

typedef struct { uint32_t key; int16_t score; uint8_t depth, flag; cmove_t move; uint32_t pad; } tt_t;
enum { TT_EXACT = 1, TT_LOWER, TT_UPPER };
static tt_t *tt;
static uint32_t tt_mask;
// liste delle mosse per livello (in PSRAM sulla scheda): ogni chiamata ricorsiva usa poco stack
static cmove_t (*MV)[CP_MAXMOVES];

#define MAXPLY 64
EXT_RAM_BSS_ATTR static cmove_t killers[MAXPLY][2];
EXT_RAM_BSS_ATTR static int hist_h[16][128];
EXT_RAM_BSS_ATTR static uint64_t path[MAXPLY + 512];   // hash della partita e del percorso di ricerca
static int path_n, root_n;
static uint32_t nodes, t_end, t_start;
static bool stopped;
EXT_RAM_BSS_ATTR static cmove_t pv_tab[MAXPLY][MAXPLY];
static int pv_len[MAXPLY];

bool ce_init(int bits)
{
    cp_init();
    if (tt) return true;
    MV = TT_ALLOC(sizeof(*MV) * MAXPLY);
    tt = TT_ALLOC(sizeof(tt_t) << bits);
    if (!tt || !MV) return false;
    tt_mask = (1u << bits) - 1;
    return true;
}

void ce_clear(void)
{
    if (tt) memset(tt, 0, sizeof(tt_t) * (tt_mask + 1));
    memset(killers, 0, sizeof(killers));
    memset(hist_h, 0, sizeof(hist_h));
}

static inline bool time_up(void)
{
    if ((nodes & 2047) == 0) {
        if (ce_yield) ce_yield();
        if (ce_now_ms && (int32_t)(ce_now_ms() - t_end) >= 0) stopped = true;
    }
    if (ce_abort) stopped = true;
    return stopped;
}

static int score_move(const cpos_t *p, cmove_t m, cmove_t ttm, int ply)
{
    if (cm_eq(m, ttm)) return 1000000;
    if (m.flags & CM_CAPTURE) {
        int victim = (m.flags & CM_EP) ? CP_P : CP_TYPE(p->sq[m.to]);
        return 100000 + VAL[victim] * 10 - VAL[CP_TYPE(p->sq[m.from])] / 10 + (m.promo ? 5000 : 0);
    }
    if (m.promo) return 90000 + VAL[m.promo];
    if (ply < MAXPLY) {
        if (cm_eq(m, killers[ply][0])) return 80000;
        if (cm_eq(m, killers[ply][1])) return 79000;
    }
    return hist_h[p->sq[m.from]][m.to];
}

static int sc[CP_MAXMOVES];   // punteggi per l'ordinamento: si ordina prima di scendere

static void sort_moves(const cpos_t *p, cmove_t *m, int n, cmove_t ttm, int ply)
{
    for (int i = 0; i < n; i++) sc[i] = score_move(p, m[i], ttm, ply);
    for (int i = 1; i < n; i++) {   // inserimento: liste corte
        cmove_t mv = m[i];
        int s = sc[i], j = i - 1;
        while (j >= 0 && sc[j] < s) { m[j + 1] = m[j]; sc[j + 1] = sc[j]; j--; }
        m[j + 1] = mv;
        sc[j + 1] = s;
    }
}

static int to_tt(int s, int ply) { return s > CE_MATE - 200 ? s + ply : s < -CE_MATE + 200 ? s - ply : s; }
static int from_tt(int s, int ply) { return s > CE_MATE - 200 ? s - ply : s < -CE_MATE + 200 ? s + ply : s; }

static int qsearch(const cpos_t *p, int alpha, int beta, int ply)
{
    nodes++;
    if (time_up()) return 0;
    int stand = ce_eval(p);
    if (ply >= MAXPLY - 1) return stand;
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    cmove_t *m = MV[ply];
    int n = gen(p, m, true);
    sort_moves(p, m, n, CM_NONE, MAXPLY);
    for (int i = 0; i < n; i++) {
        // delta pruning: anche prendendo il pezzo non si arriva ad alfa
        if (!m[i].promo && !(m[i].flags & CM_EP) && stand + VAL[CP_TYPE(p->sq[m[i].to])] + 200 < alpha) continue;
        cpos_t q = *p;
        cp_make(&q, m[i]);
        if (cp_attacked(&q, q.ksq[p->side], q.side)) continue;
        int s = -qsearch(&q, -beta, -alpha, ply + 1);
        if (stopped) return 0;
        if (s >= beta) return s;
        if (s > alpha) alpha = s;
    }
    return alpha;
}

static bool has_pieces(const cpos_t *p, int side)
{
    for (int s = 0; s < 128; s++) {
        if (s & 0x88) { s += 7; continue; }
        int pc = p->sq[s];
        if (pc && CP_COLOR(pc) == side && CP_TYPE(pc) != CP_P && CP_TYPE(pc) != CP_K) return true;
    }
    return false;
}

static int search(const cpos_t *p, int depth, int alpha, int beta, int ply, bool null_ok)
{
    pv_len[ply] = 0;
    bool in_check = cp_in_check(p);
    if (in_check) depth++;   // scacco: si guarda una mossa in più
    if (depth <= 0) return qsearch(p, alpha, beta, ply);
    nodes++;
    if (time_up()) return 0;
    if (ply >= MAXPLY - 1) return ce_eval(p);
    // patta per ripetizione o 50 mosse (dentro la ricerca basta una ripetizione)
    if (ply > 0) {
        if (p->halfmove >= 100) return 0;
        for (int i = path_n - 3; i >= 0 && i >= path_n - 1 - p->halfmove; i -= 2) if (path[i] == p->hash) return 0;
    }
    // tabella delle trasposizioni
    tt_t *e = &tt[p->hash & tt_mask];
    cmove_t ttm = CM_NONE;
    uint32_t key = (uint32_t)(p->hash >> 32);
    if (e->key == key) {
        ttm = e->move;
        if (ply > 0 && e->depth >= depth) {
            int s = from_tt(e->score, ply);
            if (e->flag == TT_EXACT) return s;
            if (e->flag == TT_LOWER && s >= beta) return s;
            if (e->flag == TT_UPPER && s <= alpha) return s;
        }
    }
    // mossa nulla: se anche passando si resta sopra beta, il ramo si può tagliare
    if (null_ok && !in_check && depth >= 3 && ply > 0 && has_pieces(p, p->side) && ce_eval(p) >= beta) {
        cpos_t q = *p;
        if (q.ep >= 0) { q.hash ^= Z_EP[CP_FILE(q.ep)]; q.ep = -1; }
        q.side ^= 1;
        q.hash ^= Z_SIDE;
        q.halfmove = 0;
        path[path_n++] = q.hash;
        int s = -search(&q, depth - 3, -beta, -beta + 1, ply + 1, false);
        path_n--;
        if (stopped) return 0;
        if (s >= beta) return beta;
    }
    cmove_t *m = MV[ply];
    int n = gen(p, m, false);
    sort_moves(p, m, n, ttm, ply);
    int best = -CE_MATE - 1, legal = 0, a0 = alpha;
    cmove_t bm = CM_NONE;
    for (int i = 0; i < n; i++) {
        cpos_t q = *p;
        cp_make(&q, m[i]);
        if (cp_attacked(&q, q.ksq[p->side], q.side)) continue;
        legal++;
        path[path_n++] = q.hash;
        int s;
        bool quiet = !(m[i].flags & CM_CAPTURE) && !m[i].promo;
        if (legal == 1) s = -search(&q, depth - 1, -beta, -alpha, ply + 1, true);
        else {
            // mosse tardive e tranquille: prima più corte, poi piene se sembrano buone
            int r = (quiet && !in_check && depth >= 3 && legal > 4 && !cp_in_check(&q)) ? 1 + (legal > 12) : 0;
            s = -search(&q, depth - 1 - r, -alpha - 1, -alpha, ply + 1, true);
            if (s > alpha && (r || s < beta)) s = -search(&q, depth - 1, -beta, -alpha, ply + 1, true);
        }
        path_n--;
        if (stopped) return 0;
        if (s > best) {
            best = s;
            bm = m[i];
            if (s > alpha) {
                alpha = s;
                pv_tab[ply][0] = m[i];
                int k = pv_len[ply + 1] < MAXPLY - 2 ? pv_len[ply + 1] : MAXPLY - 2;
                memcpy(&pv_tab[ply][1], pv_tab[ply + 1], k * sizeof(cmove_t));
                pv_len[ply] = k + 1;
                if (s >= beta) {
                    if (quiet && ply < MAXPLY) {
                        if (!cm_eq(killers[ply][0], m[i])) { killers[ply][1] = killers[ply][0]; killers[ply][0] = m[i]; }
                        int *h = &hist_h[p->sq[m[i].from]][m[i].to];
                        *h += depth * depth;
                        if (*h > 60000) for (int a = 0; a < 16; a++) for (int b = 0; b < 128; b++) hist_h[a][b] /= 2;
                    }
                    break;
                }
            }
        }
    }
    if (!legal) return in_check ? -CE_MATE + ply : 0;
    e->key = key;
    e->score = to_tt(best, ply);
    e->depth = depth;
    e->flag = best >= beta ? TT_LOWER : best > a0 ? TT_EXACT : TT_UPPER;
    e->move = bm;
    return best;
}

static void begin(const uint64_t *hist, int nhist, int max_ms)
{
    if (nhist > 500) { hist += nhist - 500; nhist = 500; }
    memcpy(path, hist, nhist * sizeof(uint64_t));
    path_n = root_n = nhist;
    nodes = 0;
    stopped = false;
    t_start = ce_now_ms ? ce_now_ms() : 0;
    t_end = t_start + (max_ms > 0 ? max_ms : 1000000);
}

void ce_search(const cpos_t *p, const uint64_t *hist, int nhist, int max_depth, int max_ms, cresult_t *r)
{
    memset(r, 0, sizeof(*r));
    if (!tt && !ce_init(16)) return;
    begin(hist, nhist, max_ms);
    path[path_n++] = p->hash;
    cmove_t l[CP_MAXMOVES];
    int nl = cp_legal(p, l);
    if (!nl) { path_n--; return; }
    r->best = l[0];
    if (max_depth > MAXPLY - 4) max_depth = MAXPLY - 4;
    for (int d = 1; d <= max_depth; d++) {
        int s = search(p, d, -CE_MATE - 1, CE_MATE + 1, 0, false);
        if (stopped && d > 1) break;
        if (pv_len[0]) {
            r->best = pv_tab[0][0];
            r->score = s;
            r->depth = d;
            r->pv_len = pv_len[0] < 16 ? pv_len[0] : 16;
            memcpy(r->pv, pv_tab[0], r->pv_len * sizeof(cmove_t));
        }
        if (stopped) break;
        if (s > CE_MATE - 100 || s < -CE_MATE + 100) break;   // matto trovato: inutile andare oltre
        // un'altra iterazione costa ~3-4 volte la precedente: se non c'è il tempo non si comincia
        if (ce_now_ms && max_ms > 0 && (ce_now_ms() - t_start) * 3 > (uint32_t)max_ms) break;
    }
    r->nodes = nodes;
    path_n--;
}

/* ================= forza regolabile ================= */

int ce_think_ms(int elo)
{
    int ms = 300 + (elo - 400) * 2;
    return ms < 300 ? 300 : ms > 4000 ? 4000 : ms;
}

static int elo_depth(int elo)
{
    int d = (elo - 400) / 250 + 1;
    return d < 1 ? 1 : d > 12 ? 12 : d;
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

cmove_t ce_play(const cpos_t *p, const uint64_t *hist, int nhist, int elo, int ply, uint32_t seed, cresult_t *r)
{
    cresult_t tmp;
    if (!r) r = &tmp;
    memset(r, 0, sizeof(*r));
    cmove_t l[CP_MAXMOVES];
    int n = cp_legal(p, l);
    if (!n) return CM_NONE;
    if (n == 1) { r->best = l[0]; return l[0]; }
    int depth = elo_depth(elo), ms = ce_think_ms(elo);
    // "temperatura": quanto può scegliere una mossa peggiore della migliore (in centipedoni)
    int temp = (1800 - elo) / 6;
    if (ply < 8 && temp < 20) temp = 20;   // un po' di varietà in apertura anche ai livelli alti
    if (temp <= 0) {
        ce_search(p, hist, nhist, depth, ms, r);
        return r->best;
    }
    // livelli umani: si valutano tutte le mosse (a profondità ridotta) e si sceglie con una
    // probabilità che cala con quanto è peggiore della migliore
    if (!tt && !ce_init(16)) return l[0];
    int d = depth > 4 ? 4 : depth;
    begin(hist, nhist, ms);
    path[path_n++] = p->hash;
    int rs[CP_MAXMOVES], best = -CE_MATE - 1;
    for (int i = 0; i < n; i++) {
        cpos_t q = *p;
        cp_make(&q, l[i]);
        path[path_n++] = q.hash;
        rs[i] = d <= 1 ? -qsearch(&q, -CE_MATE - 1, CE_MATE + 1, 1) : -search(&q, d - 1, -CE_MATE - 1, CE_MATE + 1, 1, true);
        path_n--;
        if (stopped) { // tempo finito: le mosse non valutate restano fuori
            n = i;
            break;
        }
        if (rs[i] > best) best = rs[i];
    }
    path_n--;
    if (n == 0) { r->best = l[0]; return l[0]; }
    uint32_t s = seed ? seed : 12345;
    // i livelli più bassi ogni tanto "non vedono" proprio: mossa a caso
    if (elo < 900 && (int)(lcg(&s) % 100) < (900 - elo) / 25) {
        int k = lcg(&s) % n;
        r->best = l[k];
        r->score = rs[k];
        return l[k];
    }
    double w[CP_MAXMOVES], tot = 0;
    for (int i = 0; i < n; i++) {
        int dl = best - rs[i];
        if (best > CE_MATE - 200 && rs[i] < best) dl = 10000;   // un matto trovato non si spreca
        w[i] = dl > temp * 6 ? 0 : exp(-(double)dl / temp);
        tot += w[i];
    }
    double x = (lcg(&s) % 1000000) / 1000000.0 * tot;
    int k = 0;
    for (; k < n - 1; k++) { if ((x -= w[k]) <= 0) break; }
    while (w[k] == 0 && k > 0) k--;
    r->best = l[k];
    r->score = rs[k];
    r->depth = d;
    r->nodes = nodes;
    return l[k];
}

/* ================= libro delle aperture ================= */

static const char *const BOOK[] = {
    "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7",   // spagnola
    "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6",   // italiana
    "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8e7 e1g1 e8g8",   // due cavalli, tranquilla
    "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 b1c3 f8b4",   // scozzese
    "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5",   // russa
    "e2e4 e7e5 b1c3 g8f6 g1f3 b8c6 f1b5 f8b4",             // quattro cavalli
    "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6",   // siciliana Najdorf
    "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5",   // siciliana Sveshnikov
    "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 a7a6 f1d3 g8f6",   // siciliana Kan
    "e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6",   // siciliana chiusa
    "e2e4 c7c5 c2c3 g8f6 e4e5 f6d5 d2d4 c5d4 g1f3 b8c6",   // siciliana Alapin
    "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7",   // francese
    "e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6",   // francese, spinta
    "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6",   // Caro-Kann
    "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6 f1e2 c6c5",   // Caro-Kann, spinta
    "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5",   // scandinava
    "e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 g1f3 g8f6 f1e2 e8g8",   // moderna / Pirc
    "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8",   // gambetto di donna rifiutato
    "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5",   // slava
    "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5",   // gambetto di donna accettato
    "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5",   // nimzoindiana
    "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8",   // est-indiana
    "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7",   // ovest-indiana
    "d2d4 d7d5 g1f3 g8f6 c1f4 e7e6 e2e3 c7c5 c2c3 b8c6",   // sistema di Londra
    "d2d4 g8f6 g1f3 g7g6 c1f4 f8g7 e2e3 d7d6 h2h3 e8g8",   // Londra contro l'est-indiana
    "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5",   // inglese
    "c2c4 g8f6 b1c3 e7e6 g1f3 d7d5 d2d4 f8e7 c1f4 e8g8",   // inglese verso il gambetto di donna
    "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8",   // Réti
};

static bool book_move(const cpos_t *p, const char *u, cmove_t *m)
{
    char b[6];
    memcpy(b, u, 4);
    b[4] = 0;
    return cp_parse_uci(p, b, m);
}

bool ce_book(const cmove_t *moves, int n, uint32_t seed, cmove_t *out)
{
    cmove_t cand[32];
    int nc = 0;
    char u[8];
    for (unsigned i = 0; i < sizeof(BOOK) / sizeof(BOOK[0]); i++) {
        const char *s = BOOK[i];
        int k = 0;
        bool match = true;
        for (; k < n && match; k++) {
            if (!*s) { match = false; break; }
            cp_uci(moves[k], u);
            if (strncmp(s, u, 4)) match = false;
            s += strlen(s) >= 5 ? 5 : strlen(s);
        }
        if (!match || !*s) continue;
        cpos_t p;
        cp_start(&p);
        for (int j = 0; j < n; j++) cp_make(&p, moves[j]);
        cmove_t m;
        if (!book_move(&p, s, &m)) continue;
        bool dup = false;
        for (int j = 0; j < nc; j++) if (cm_eq(cand[j], m)) dup = true;
        if (!dup && nc < 32) cand[nc++] = m;
    }
    if (!nc) return false;
    uint32_t s = seed * 2654435761u + 0x9E3779B9u;
    *out = cand[(s >> 16) % nc];
    return true;
}

int ce_book_check(void)
{
    int bad = 0;
    for (unsigned i = 0; i < sizeof(BOOK) / sizeof(BOOK[0]); i++) {
        cpos_t p;
        cp_start(&p);
        for (const char *s = BOOK[i]; *s;) {
            cmove_t m;
            if (!book_move(&p, s, &m)) { bad++; break; }
            cp_make(&p, m);
            s += strlen(s) >= 5 ? 5 : strlen(s);
        }
    }
    return bad;
}
