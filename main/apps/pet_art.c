// pet_art.c — pixel art originale del Polipetto: un polpetto arancione che cresce
// (neonato → bimbo → ragazzo → adulto, con cinque forme adulte), più cibo, effetti,
// fondale marino e icone. Gli sprite sono stringhe: un carattere = un pixel.
#include "pet_art.h"
#include "pet_core.h"
#include <string.h>

#define TR 0xFFFFFFFFu   // trasparente

/* ---------------- superficie ---------------- */

static struct { uint16_t *buf; int lw, lh, sc, stride; uint8_t bri; } S;

void art_begin(uint16_t *buf, int lw, int lh, int scale, int stride)
{
    S.buf = buf;
    S.lw = lw;
    S.lh = lh;
    S.sc = scale;
    S.stride = stride;
    S.bri = 255;
}

void art_brightness(uint8_t b) { S.bri = b; }

static uint16_t c565(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (S.bri != 255) { r = r * S.bri / 255; g = g * S.bri / 255; b = b * S.bri / 255; }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void art_px(int x, int y, uint32_t rgb)
{
    if (rgb == TR || (unsigned)x >= (unsigned)S.lw || (unsigned)y >= (unsigned)S.lh) return;
    uint16_t c = c565(rgb);
    uint16_t *p = S.buf + (y * S.sc) * S.stride + x * S.sc;
    for (int j = 0; j < S.sc; j++, p += S.stride)
        for (int i = 0; i < S.sc; i++) p[i] = c;
}

void art_rect(int x, int y, int w, int h, uint32_t rgb)
{
    // ritaglio e riempimento a righe intere: è la funzione più usata (fondale, barre)
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > S.lw) w = S.lw - x;
    if (y + h > S.lh) h = S.lh - y;
    if (w <= 0 || h <= 0 || rgb == TR) return;
    uint16_t c = c565(rgb);
    int pw = w * S.sc;
    uint16_t *row = S.buf + (y * S.sc) * S.stride + x * S.sc;
    for (int j = 0; j < h * S.sc; j++, row += S.stride)
        for (int i = 0; i < pw; i++) row[i] = c;
}

void art_fill(uint32_t rgb) { art_rect(0, 0, S.lw, S.lh, rgb); }

/* ---------------- tavolozza ---------------- */

static uint32_t pal(char c)
{
    switch (c) {
    case 'O': return 0xD97757;   // corpo (arancione Claude)
    case 'o': return 0xA9503A;   // ombra
    case 'h': return 0xF4B19A;   // riflesso
    case 'k': return 0x1E1616;   // occhi, contorni
    case 'w': return 0xFFFFFF;
    case 'p': return 0xF28B9B;   // guance
    case 'm': return 0x5A2216;   // bocca
    case 'e': return 0xF3E9D2;   // guscio
    case 'E': return 0xD97757;   // macchie del guscio
    case 'f': return 0xC9B99A;   // ombra del guscio
    case 'n': return 0x2C2547;   // inchiostro
    case 'N': return 0x5A4E8C;
    case 's': return 0xC9A66B;   // sabbia
    case 'S': return 0x9C7E4E;
    case 'g': return 0x3DBA5A;   // alghe
    case 'G': return 0x217A3C;
    case 'b': return 0x8B5A2B;   // cappello
    case 'B': return 0x4A2C12;
    case 'y': return 0xFFD23C;
    case 'r': return 0xE5484D;
    case 'c': return 0xA8E6FF;   // bolle, acqua
    case 'C': return 0x3F9FE0;
    case 'x': return 0x8C939B;
    case 'X': return 0x4E555E;
    case 'l': return 0xEAF0F8;   // fantasmino
    case 'L': return 0xB4C2D4;
    case 't': return 0xFFE066;   // aureola
    case 'q': return 0xC77DFF;   // medusa
    case 'Q': return 0xEFD3FF;
    case 'z': return 0xEDEDED;
    case 'u': return 0xB07A45;   // biscotto
    case 'v': return 0x7A4A22;   // legno scuro
    case 'a': return 0xFF9F1C;   // zucca
    case 'd': return 0x2E7D32;   // abete
    case 'j': return 0x1B2A4A;   // blu notte
    default:  return TR;
    }
}

void art_sprite(const sprite_t *s, int x, int y, bool flip)
{
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++) {
            char ch = s->rows[r][c];
            if (ch == '.') continue;
            art_px(x + (flip ? s->w - 1 - c : c), y + r, pal(ch));
        }
}

void art_sprite_color(const sprite_t *s, int x, int y, uint32_t rgb)
{
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++)
            if (s->rows[r][c] != '.') art_px(x + c, y + r, rgb);
}

void art_sprite_from(const sprite_t *s, int x, int y, int col0)
{
    for (int r = 0; r < s->h; r++)
        for (int c = col0 < 0 ? 0 : col0; c < s->w; c++)
            if (s->rows[r][c] != '.') art_px(x + c, y + r, pal(s->rows[r][c]));
}

#define SPR(name, w, h, ...) static const char *const name##_rows[] = {__VA_ARGS__}; \
    const sprite_t name = {w, h, name##_rows}

/* ---------------- effetti e oggetti ---------------- */

SPR(SPR_HEART, 5, 4, "rr.rr", "rrrrr", ".rrr.", "..r..");
SPR(SPR_HEART_BIG, 7, 6, ".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r...");
SPR(SPR_DROP_BIG, 5, 7, "..C..", "..C..", ".CCC.", "CCcCC", "CCCCC", "CCCCC", ".CCC.");
SPR(SPR_SKULL, 5, 5, ".lll.", "lllll", "lklkl", ".lll.", ".l.l.");
SPR(SPR_INK, 6, 5, "..n...", ".nnN..", "nnNnn.", "nnnnnn", ".nnnn.");
SPR(SPR_FISH, 7, 4, ".yyy..y", "yykyyyy", "yyyyyyy", ".yyy..y");
SPR(SPR_COOKIE, 5, 5, ".uuu.", "uBuuu", "uuuBu", "uBuuu", ".uuu.");
SPR(SPR_GLASS, 5, 6, "c...c", "c...c", "cCCCc", "cCCCc", "cCCCc", ".ccc.");
SPR(SPR_PILL, 6, 3, ".rrww.", "rrrwww", ".rrww.");
SPR(SPR_BUBBLE, 3, 3, ".c.", "c.c", ".c.");
SPR(SPR_SPARK, 3, 3, ".y.", "yyy", ".y.");
SPR(SPR_JELLY, 5, 5, ".qqq.", "qQqQq", "qqqqq", "q.q.q", ".q.q.");
SPR(SPR_Z, 4, 4, "zzzz", "..z.", ".z..", "zzzz");
SPR(SPR_BANG, 1, 5, "r", "r", "r", ".", "r");
SPR(SPR_QUESTION, 3, 5, "zz.", "..z", ".z.", "...", ".z.");
SPR(SPR_ANGER, 3, 3, "r.r", ".r.", "r.r");
SPR(SPR_HALO, 6, 2, ".tttt.", "t....t");
SPR(SPR_BALL, 5, 5, ".rrw.", "rrwww", "wwwrr", "wwrrr", ".rrr.");
SPR(SPR_BOOK, 9, 7, "BbbbBbbbB", "BwwwBwwwB", "BwkwBwkwB", "BwwwBwwwB", "BwkwBwkwB", "BwwwBwwwB", "BbbbBbbbB");
SPR(SPR_SHELL, 5, 4, ".ppp.", "pwpwp", "ppppp", ".ppp.");
SPR(SPR_CAKE, 9, 8, "..y.y.y..", "..r.r.r..", ".wwwwwww.", ".wpwpwpw.", "uuuuuuuuu", "uppppppuu", "uuuuuuuuu", ".........");
SPR(SPR_NEST_EGG, 6, 7, "..ee..", ".eEee.", "eeeeEe", "eEeeee", "eeeEee", ".eeee.", "vvvvvv");
SPR(SPR_GIFT, 7, 7, "..y.y..", "...y...", "rrryrrr", "rrryrrr", "yyyyyyy", "rrryrrr", "rrryrrr");

/* ---------------- icone ---------------- */

static const char *const ic_food[] = {"#.#.#..#", "#.#.#.##", "#####.##", ".###..##", "..#...##", "..#....#", "..#....#", "..#....#"};
static const char *const ic_play[] = {"..####..", ".##..##.", "#..##..#", "#.####.#", "#.####.#", "#..##..#", ".##..##.", "..####.."};
static const char *const ic_clean[] = {"....##..", "...#..#.", "...#..#.", ".##.##..", "#..#....", "#..#.##.", ".##.#..#", ".....##."};
static const char *const ic_med[] = {"..####..", "..####..", "########", "########", "########", "########", "..####..", "..####.."};
static const char *const ic_light[] = {"..####..", ".#....#.", "#......#", "#......#", ".#....#.", "..#..#..", "..####..", "...##..."};
static const char *const ic_scold[] = {"...##...", "...##...", "...##...", "...##...", "...##...", "........", "...##...", "...##..."};
static const char *const ic_stats[] = {".##..##.", "#..##..#", "#......#", "#......#", ".#....#.", "..#..#..", "...##...", "........"};
static const char *const ic_call[] = {"..####..", ".######.", "##.##.##", "########", ".######.", ".#.##.#.", "#..##..#", "#..#..#."};

const sprite_t PET_ICONS[PICON_COUNT] = {
    {8, 8, ic_food}, {8, 8, ic_play}, {8, 8, ic_clean}, {8, 8, ic_med},
    {8, 8, ic_light}, {8, 8, ic_scold}, {8, 8, ic_stats}, {8, 8, ic_call},
};

/* ---------------- aspetto ---------------- */

static art_look_t L;   // tutto zero = arancione classico

// corpo, ombra, riflesso per ogni colore dei geni (stesso ordine di COL_*)
static const uint32_t body_rgb[][3] = {
    {0xD97757, 0xA9503A, 0xF4B19A},   // arancione
    {0xE8606A, 0xB03A48, 0xF8A8AE},   // corallo
    {0x9B6BD6, 0x6A4499, 0xCDB2F0},   // viola
    {0x4FA8E0, 0x2C6E9E, 0xA8D8F5},   // azzurro
    {0x5CBF7A, 0x357A4C, 0xA9E6BB},   // verde
    {0xF28BB8, 0xB55A85, 0xFAC3DB},   // rosa
    {0xE8C547, 0xA8862A, 0xF8E79A},   // oro
};
#define N_COL (int)(sizeof(body_rgb) / sizeof(body_rgb[0]))

void art_look(const art_look_t *l)
{
    if (l) L = *l;
    else memset(&L, 0, sizeof(L));
    if (L.color >= N_COL) L.color = 0;
}

uint32_t art_body_rgb(int color) { return body_rgb[color >= 0 && color < N_COL ? color : 0][0]; }

/* ---------------- polipetto ---------------- */

static const char *const baby_rows[] = {
    "..OOO..",
    ".OOOOO.",
    "OhOOOOO",
    "OOOOOOO",
    ".OOOOO.",
};
static const char *const child_rows[] = {
    "...OOOO...",
    "..OOOOOO..",
    ".OhhOOOOO.",
    ".OhOOOOOO.",
    "OOOOOOOOOO",
    "OOOOOOOOOO",
    "OOOOOOOOOO",
    ".OOOOOOOO.",
};
static const char *const teen_rows[] = {
    "....OOOO....",
    "..OOOOOOOO..",
    ".OOhhOOOOOO.",
    ".OhOOOOOOOO.",
    "OOhOOOOOOOOO",
    "OOOOOOOOOOOO",
    "OOOOOOOOOOOO",
    "OOOOOOOOOOOO",
    ".OOOOOOOOOO.",
};
static const char *const adult_rows[] = {
    ".....OOOO.....",
    "...OOOOOOOO...",
    "..OhhOOOOOOO..",
    ".OhOOOOOOOOOO.",
    ".OhOOOOOOOOOO.",
    "OOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOO",
    ".OOOOOOOOOOOO.",
};
static const char *const fat_rows[] = {
    "......OOOO......",
    "....OOOOOOOO....",
    "..OOhhOOOOOOOO..",
    ".OOhOOOOOOOOOOO.",
    "OOhOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOOOO",
    "OOOOOOOOOOOOOOOO",
    ".OOOOOOOOOOOOOO.",
    "..OOOOOOOOOOOO..",
};

typedef struct {
    const char *const *rows;
    uint8_t w, h;
    uint8_t ex, ey, erx, esz;   // occhio sinistro, x del destro, lato
    uint8_t mx, my, mw;         // bocca
    uint8_t cy, clx, crx;       // guance (cy = 0: niente guance)
    uint8_t tn, tl;             // tentacoli: quanti e quanto lunghi
    uint8_t tx[6];
} body_t;

static const body_t B_BABY  = {baby_rows,  7,  5,  2, 2, 4,  1, 3, 3, 1, 0, 0, 0,  3, 2, {1, 3, 5}};
static const body_t B_CHILD = {child_rows, 10, 8,  2, 4, 6,  2, 4, 6, 2, 6, 1, 8,  4, 3, {1, 3, 6, 8}};
static const body_t B_TEEN  = {teen_rows,  12, 9,  3, 5, 7,  2, 5, 7, 2, 7, 2, 9,  4, 4, {1, 4, 7, 10}};
static const body_t B_ADULT = {adult_rows, 14, 10, 3, 5, 9,  2, 6, 7, 2, 8, 2, 11, 6, 5, {1, 3, 5, 8, 10, 12}};
static const body_t B_FAT   = {fat_rows,   16, 11, 4, 5, 10, 2, 7, 8, 2, 8, 2, 13, 6, 4, {2, 4, 6, 9, 11, 13}};

static const body_t *body_for(int stage, int form)
{
    switch (stage) {
    case PET_BABY:  return &B_BABY;
    case PET_CHILD: return &B_CHILD;
    case PET_TEEN:  return &B_TEEN;
    case PET_ADULT: return form == FORM_GLUTTON ? &B_FAT : &B_ADULT;
    default:
        // il fantasmino prende la forma dell'ultima età raggiunta
        if (form >= FORM_SAGE) return form == FORM_GLUTTON ? &B_FAT : &B_ADULT;
        if (form >= FORM_TEEN_GOOD) return &B_TEEN;
        return &B_CHILD;
    }
}

// lunghezza dei tentacoli secondo i geni
static int tent_len(const body_t *b)
{
    int tl = b->tl + (L.tent == TENT_LONG ? 1 : L.tent == TENT_SHORT ? -1 : 0);
    return tl < 2 ? 2 : tl;
}

void art_pet_size(int stage, int form, int *w, int *h)
{
    const body_t *b = body_for(stage, form);
    *w = b->w;
    *h = b->h + tent_len(b);
}

static struct { int x, y, w, tint; bool flip; } B;

static uint32_t tinted(char c)
{
    if (c == '.') return TR;
    switch (B.tint) {
    case TINT_WHITE: return 0xFFFFFF;
    case TINT_SICK:
        if (c == 'O') return 0xC9A08A;
        if (c == 'o') return 0x9C7766;
        if (c == 'h') return 0xE6CFC2;
        if (c == 'p') return TR;
        break;
    case TINT_GHOST:
        if (c == 'O' || c == 'h') return 0xEAF0F8;
        if (c == 'o') return 0xB4C2D4;
        if (c == 'p') return TR;
        break;
    }
    if (c == 'O') return body_rgb[L.color][0];
    if (c == 'o') return body_rgb[L.color][1];
    if (c == 'h') return body_rgb[L.color][2];
    return pal(c);
}

static void bpx(int lx, int ly, char c)
{
    uint32_t col = tinted(c);
    if (col == TR) return;
    art_px(B.x + (B.flip ? B.w - 1 - lx : lx), B.y + ly, col);
}

static void eye(int x, int y, int sz, expr_t e, bool right)
{
    if (sz == 1) {
        bool closed = e == EXPR_BLINK || e == EXPR_SLEEP || e == EXPR_HAPPY || e == EXPR_EAT;
        bpx(x, y, closed ? 'o' : 'k');
        if (e == EXPR_SAD && !right) bpx(x, y + 1, 'c');
        return;
    }
    switch (e) {
    case EXPR_BLINK:
    case EXPR_SLEEP:
        bpx(x, y + 1, 'k'); bpx(x + 1, y + 1, 'k');
        break;
    case EXPR_HAPPY:
    case EXPR_EAT: {
        int b = right ? x - 1 : x;   // "^" simmetrico fra i due occhi
        bpx(b, y + 1, 'k'); bpx(b + 1, y, 'k'); bpx(b + 2, y + 1, 'k');
        break;
    }
    case EXPR_SAD:
        bpx(x, y + 1, 'k'); bpx(x + 1, y + 1, 'k');
        bpx(right ? x + 1 : x, y, 'k');
        if (!right) bpx(x, y + 2, 'c');   // lacrimuccia
        break;
    case EXPR_ANGRY:
        bpx(x, y, 'k'); bpx(x + 1, y, 'k'); bpx(x, y + 1, 'k'); bpx(x + 1, y + 1, 'k');
        if (right) { bpx(x + 1, y - 2, 'k'); bpx(x, y - 1, 'k'); }
        else       { bpx(x, y - 2, 'k'); bpx(x + 1, y - 1, 'k'); }
        break;
    case EXPR_SICK:
        if (right) { bpx(x + 1, y, 'k'); bpx(x, y + 1, 'k'); }
        else       { bpx(x, y, 'k'); bpx(x + 1, y + 1, 'k'); }
        break;
    default:   // normale e sorpreso
        bpx(x, y, 'w'); bpx(x + 1, y, 'k'); bpx(x, y + 1, 'k'); bpx(x + 1, y + 1, 'k');
        break;
    }
}

static void mouth(const body_t *b, expr_t e, int frame)
{
    int x = b->mx, y = b->my, w = b->mw;
    bool open = e == EXPR_HAPPY || e == EXPR_SURPRISE || (e == EXPR_EAT && (frame & 1));
    switch (e) {
    case EXPR_SLEEP:
    case EXPR_BLINK:
        if (w == 2) { bpx(x, y, 'm'); bpx(x + 1, y, 'm'); }
        break;
    case EXPR_SAD:
    case EXPR_SICK:
        if (w == 2) { bpx(x - 1, y + 1, 'm'); bpx(x, y, 'm'); bpx(x + 1, y, 'm'); bpx(x + 2, y + 1, 'm'); }
        else bpx(x, y + 1, 'm');
        break;
    default:
        for (int i = 0; i < w; i++) bpx(x + i, y, 'm');
        if (open) for (int i = 0; i < w; i++) bpx(x + i, y + 1, e == EXPR_SURPRISE ? 'm' : 'r');
        break;
    }
}

static void brows(const body_t *b)
{
    int x = b->ex, y = b->ey, rx = b->erx;
    bpx(x, y - 2, 'k'); bpx(x + 1, y - 1, 'k');
    bpx(rx + 1, y - 2, 'k'); bpx(rx, y - 1, 'k');
}

static void overlays(const body_t *b, int form, expr_t e)
{
    switch (form) {
    case FORM_TEEN_GOOD: {   // fiocco
        if (L.hat) break;
        int x = b->w - 5;
        bpx(x, 0, 'p'); bpx(x + 2, 0, 'p');
        bpx(x, 1, 'p'); bpx(x + 1, 1, 'r'); bpx(x + 2, 1, 'p');
        bpx(x, 2, 'p'); bpx(x + 2, 2, 'p');
        break;
    }
    case FORM_TEEN_BAD:      // sopracciglia sempre aggrottate
        if (e == EXPR_NORMAL || e == EXPR_BLINK) brows(b);
        break;
    case FORM_SAGE: {        // occhiali
        int ex[2] = {b->ex, b->erx}, y = b->ey;
        for (int k = 0; k < 2; k++) {
            for (int i = -1; i <= 2; i++) { bpx(ex[k] + i, y - 1, 'k'); bpx(ex[k] + i, y + 2, 'k'); }
            bpx(ex[k] - 1, y, 'k'); bpx(ex[k] - 1, y + 1, 'k');
            bpx(ex[k] + 2, y, 'k'); bpx(ex[k] + 2, y + 1, 'k');
        }
        for (int i = b->ex + 3; i < b->erx - 1; i++) bpx(i, y, 'k');
        break;
    }
    case FORM_EXPLORER: {    // cappello da esploratore
        if (L.hat) break;
        int cx = b->w / 2;
        for (int i = cx - 3; i < cx + 3; i++) { bpx(i, -2, 'b'); bpx(i, -1, 'b'); bpx(i, 0, 'B'); }
        for (int i = 1; i < b->w - 1; i++) bpx(i, 1, 'b');
        bpx(cx + 2, -3, 'r');   // piuma
        bpx(cx + 3, -4, 'r');
        break;
    }
    case FORM_MESSY:         // schizzi d'inchiostro e ciuffo spettinato
        bpx(3, 3, 'n'); bpx(4, 3, 'n'); bpx(10, 4, 'n');
        bpx(5, 8, 'n'); bpx(12, 6, 'n');
        bpx(b->w / 2 - 1, -1, 'o'); bpx(b->w / 2, -2, 'o'); bpx(b->w / 2 + 1, -1, 'o');
        break;
    default:
        break;
    }
}

/* ---------------- negozio ---------------- */

const art_item_t ART_ITEMS[] = {
    {"", 0, 0},
    {"Cappellino", ITEM_HAT, 15},
    {"Fiocco", ITEM_HAT, 10},
    {"Cuffia di lana", ITEM_HAT, 20},
    {"Cappello da festa", ITEM_HAT, 20},
    {"Cilindro", ITEM_HAT, 35},
    {"Cappello da pirata", ITEM_HAT, 40},
    {"Corona", ITEM_HAT, 60},
    {"Fiore", ITEM_ACC, 10},
    {"Papillon", ITEM_ACC, 15},
    {"Sciarpa", ITEM_ACC, 20},
    {"Occhiali da sole", ITEM_ACC, 25},
    {"Monocolo", ITEM_ACC, 30},
    {"Stella marina", ITEM_DECO, 15},
    {"Corallo", ITEM_DECO, 25},
    {"Anfora", ITEM_DECO, 30},
    {"Conchiglia gigante", ITEM_DECO, 35},
    {"Forziere", ITEM_DECO, 40},
    {"Castello", ITEM_DECO, 50},
    {"Sottomarino", ITEM_DECO, 80},
};
const int ART_ITEM_COUNT = sizeof(ART_ITEMS) / sizeof(ART_ITEMS[0]);

// cappelli: l'ultima riga poggia sulla cima della testa
static const char *const hat_cap[] = {"..rrrr...", ".rwrrrr..", ".rrrrrrrr"};
static const char *const hat_bow[] = {"pp.pp", "pprpp", "pp.pp"};
static const char *const hat_wool[] = {"...ww...", "..CCCC..", ".CcCCcC.", "wwwwwwww"};
static const char *const hat_party[] = {"..y..", "..q..", ".qrq.", ".rqr.", "qrqrq"};
static const char *const hat_top[] = {"..kkkk..", "..kkkk..", "..kkkk..", "..rrrr..", "kkkkkkkk"};
static const char *const hat_pirate[] = {"...kkkk...", "..kkwkkk..", ".kkkkkkkk.", "kyyyyyyyyk"};
static const char *const hat_crown[] = {"y..y..y", "yy.y.yy", "yyyyyyy", "yryyyry"};
static const char *const hat_santa[] = {"...rrrr..", "..rrrrrrw", ".rrrrrr..", "wwwwwwww."};
static const char *const hat_witch[] = {"....k...", "...kk...", "...kqk..", "..kkkk..", "kkkkkkkk"};

static const sprite_t HATS[] = {
    {0, 0, NULL}, {9, 3, hat_cap}, {5, 3, hat_bow}, {8, 4, hat_wool}, {5, 5, hat_party},
    {8, 5, hat_top}, {10, 4, hat_pirate}, {7, 4, hat_crown},
};
static const sprite_t HAT_SANTA = {9, 4, hat_santa}, HAT_WITCH = {8, 5, hat_witch};

static void bspr(const sprite_t *s, int x, int y)
{
    for (int r = 0; r < s->h; r++)
        for (int c = 0; c < s->w; c++)
            if (s->rows[r][c] != '.') bpx(x + c, y + r, s->rows[r][c]);
}

static int holiday_now;   // dall'ambiente: a Natale e Halloween il cappello è della festa

static void draw_hat(const body_t *b)
{
    const sprite_t *s = NULL;
    if (holiday_now == HOL_CHRISTMAS) s = &HAT_SANTA;
    else if (holiday_now == HOL_HALLOWEEN) s = &HAT_WITCH;
    else if (holiday_now == HOL_OCTOPUS || holiday_now == HOL_NYE || holiday_now == HOL_NEWYEAR) s = &HATS[4];
    if (!s) {
        if (!L.hat || L.hat >= (int)(sizeof(HATS) / sizeof(HATS[0]))) return;
        s = &HATS[L.hat];
    }
    if (s == &HATS[2]) { bspr(s, b->w - 5, 0); return; }   // il fiocco va di lato
    bspr(s, (b->w - s->w + 1) / 2, 1 - s->h);
}

static void draw_acc(const body_t *b, expr_t e)
{
    int y = b->ey;
    switch (L.acc) {
    case 8:    // fiore di lato
        bpx(1, 1, 'p'); bpx(0, 2, 'p'); bpx(2, 2, 'p'); bpx(1, 3, 'p'); bpx(1, 2, 'y');
        break;
    case 9: {  // papillon sotto la bocca
        int x = b->w / 2 - 2, yy = b->h - 2;
        bpx(x, yy - 1, 'r'); bpx(x, yy, 'r'); bpx(x, yy + 1, 'r'); bpx(x + 1, yy, 'r');
        bpx(x + 2, yy, 'k');
        bpx(x + 3, yy, 'r'); bpx(x + 4, yy - 1, 'r'); bpx(x + 4, yy, 'r'); bpx(x + 4, yy + 1, 'r');
        break;
    }
    case 10:   // sciarpa a righe con un capo che pende
        for (int i = 0; i < b->w; i++) bpx(i, b->h - 1, (i / 2) & 1 ? 'w' : 'r');
        bpx(2, b->h, 'r'); bpx(2, b->h + 1, 'w'); bpx(3, b->h, 'w');
        break;
    case 11:   // occhiali da sole
        if (e == EXPR_BACK) break;
        if (b->esz == 1) { bpx(b->ex - 1, y, 'k'); bpx(b->ex, y, 'k'); bpx(b->erx, y, 'k'); bpx(b->erx + 1, y, 'k'); break; }
        for (int i = -1; i <= 2; i++) { bpx(b->ex + i, y, 'k'); bpx(b->erx + i, y, 'k'); }
        for (int i = 0; i <= 1; i++) { bpx(b->ex + i, y + 1, 'k'); bpx(b->erx + i, y + 1, 'k'); }
        for (int i = b->ex + 3; i < b->erx - 1; i++) bpx(i, y, 'k');
        bpx(b->ex, y, 'X'); bpx(b->erx, y, 'X');
        break;
    case 12:   // monocolo sull'occhio destro, con la catenella
        if (e == EXPR_BACK) break;
        bpx(b->erx - 1, y - 1, 'y'); bpx(b->erx + 2, y - 1, 'y'); bpx(b->erx - 1, y + 2, 'y'); bpx(b->erx + 2, y + 2, 'y');
        bpx(b->erx, y - 1, 'y'); bpx(b->erx + 1, y - 1, 'y'); bpx(b->erx, y + 2, 'y'); bpx(b->erx + 1, y + 2, 'y');
        bpx(b->erx + 2, y + 3, 'y'); bpx(b->erx + 2, y + 4, 'y');
        break;
    }
}

void art_pet(int stage, int form, int x, int y, expr_t e, int frame, bool flip, int look, int tint)
{
    const body_t *b = body_for(stage, form);
    B.x = x;
    B.y = y;
    B.w = b->w;
    B.flip = flip;
    B.tint = tint;

    // corpo: il bordo destro e quello basso diventano ombra
    for (int r = 0; r < b->h; r++)
        for (int c = 0; c < b->w; c++) {
            char ch = b->rows[r][c];
            if (ch == '.') continue;
            if (ch == 'O' && (c + 1 >= b->w || b->rows[r][c + 1] == '.' || r + 1 >= b->h || b->rows[r + 1][c] == '.'))
                ch = 'o';
            // motivo dei geni: puntini o strisce color ombra
            if (ch == 'O' && tint != TINT_GHOST) {
                if (L.pattern == PAT_SPOTS && r >= 1 && (c * 5 + r * 3) % 7 == 0) ch = 'o';
                if (L.pattern == PAT_STRIPES && r >= 2 && r % 3 == 0 && c % 4 != 3) ch = 'o';
            }
            bpx(c, r, ch);
        }

    // tentacoli: la metà bassa ondeggia, la punta fa il ricciolo
    int half = b->tn / 2, tl = tent_len(b);
    for (int i = 0; i < b->tn; i++) {
        int dir = i < half ? -1 : 1;
        if ((b->tn & 1) && i == half) dir = (frame & 1) ? 1 : -1;
        for (int k = 0; k < tl; k++) {
            int off = (k >= tl / 2 && (frame & 1)) ? dir : 0;
            bool tip = k == tl - 1;
            bpx(b->tx[i] + off, b->h + k, tip ? 'o' : 'O');
            if (tip) bpx(b->tx[i] + off + ((frame & 1) ? -dir : dir), b->h + k, 'o');
        }
    }

    if (tint == TINT_WHITE) return;
    if (e != EXPR_BACK) {
        int lk = flip ? -look : look;
        eye(b->ex + lk, b->ey, b->esz, e, false);
        eye(b->erx + lk, b->ey, b->esz, e, true);
        mouth(b, e, frame);
        if (b->cy && tint == TINT_NONE && (e == EXPR_NORMAL || e == EXPR_HAPPY || e == EXPR_EAT || e == EXPR_BLINK)) {
            bpx(b->clx, b->cy, 'p');
            bpx(b->crx, b->cy, 'p');
        }
    }
    if (tint == TINT_GHOST) return;
    if (e != EXPR_BACK) overlays(b, stage == PET_DEAD ? FORM_BASE : form, e);
    if (stage != PET_DEAD) { draw_acc(b, e); draw_hat(b); }
}

/* ---------------- uovo ---------------- */

static const char *const egg_rows[] = {
    "...eeee...",
    "..eeeeee..",
    ".eeEeeeee.",
    ".eEEeeEee.",
    "eeEeeeEEee",
    "eeeeeeeeee",
    "eEeeeEeeee",
    "eEEeeeeeEe",
    "eeeeeeeEEe",
    ".eeEeeeee.",
    ".eeeeeeee.",
    "..eeeeee..",
};

void art_egg(int x, int y, int crack)
{
    for (int r = 0; r < EGG_H; r++)
        for (int c = 0; c < EGG_W; c++) {
            char ch = egg_rows[r][c];
            if (ch == '.') continue;
            if (ch == 'e' && (c + 1 >= EGG_W || egg_rows[r][c + 1] == '.' || r + 1 >= EGG_H || egg_rows[r + 1][c] == '.'))
                ch = 'f';
            art_px(x + c, y + r, pal(ch));
        }
    // crepa a zig-zag che si allarga dal centro
    static const int8_t cr[][2] = {{4, 6}, {5, 5}, {3, 5}, {6, 6}, {2, 6}, {7, 5}, {1, 5}, {8, 6}};
    int n = crack <= 0 ? 0 : crack >= 3 ? 8 : crack * 3;
    for (int i = 0; i < n; i++) art_px(x + cr[i][0], y + cr[i][1], 0x1E1616);
}

/* ---------------- fondale ---------------- */

static uint32_t lerp(uint32_t a, uint32_t b, int t)   // t 0..255
{
    int r = ((a >> 16) & 0xFF) + ((((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t) >> 8);
    int g = ((a >> 8) & 0xFF) + ((((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t) >> 8);
    int bl = (a & 0xFF) + ((((int)(b & 0xFF) - (int)(a & 0xFF)) * t) >> 8);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static void weed(int x, int h, int frame)
{
    for (int k = 0; k < h; k++) {
        int sway = (k >= 2 && ((k + frame / 4) & 2)) ? 1 : 0;
        art_px(x + sway, 28 - k, (k & 1) ? pal('G') : pal('g'));
    }
}

/* ---------------- decorazioni ---------------- */

SPR(D_STAR, 5, 5, "..a..", ".aaa.", "aaaaa", ".a.a.", "a...a");
SPR(D_CORAL, 6, 7, "r..r.r", "r.rr.r", ".rr.rr", "..rrr.", "..rr..", "..r...", "..r...");
SPR(D_AMPHORA, 5, 7, ".uuu.", "..u..", ".uuu.", "uuuuu", "uBuBu", "uuuuu", ".uuu.");
SPR(D_SHELL, 7, 5, "..ppp..", ".pwpwp.", "pwpwpwp", "ppppppp", ".ppppp.");
SPR(D_CHEST, 7, 5, ".bbbbb.", "bBBBBBb", "bbbybbb", "bbbbbbb", "BBBBBBB");
SPR(D_CASTLE, 9, 8, "x.x...x.x", "xxx...xxx", "xxx.x.xxx", "xxxxxxxxx", "xxxxXxxxx", "xxxXXXxxx", "xxxXXXxxx", "xxxXXXxxx");
SPR(D_SUB, 12, 5, "....yy......", "....yy......", ".yyyyyyyyyy.", "yycyycyycyyy", ".yyyyyyyyyy.");
SPR(D_TREE, 7, 9, "...y...", "...d...", "..ddd..", "..drd..", ".ddddd.", ".dyddd.", "ddddrdd", "...v...", "...v...");
SPR(D_PUMPKIN, 7, 5, "...g...", ".aaaaa.", "aakakaa", "aaaaaaa", ".akkka.");
SPR(D_EASTER, 5, 6, ".qqq.", "qyyyq", "qqqqq", "rrrrr", "qqqqq", ".qqq.");
SPR(D_SOCK, 5, 7, "wwww.", "rrrr.", "rwrr.", "rrrr.", "rrrrr", "rrrrr", ".rrr.");

void art_deco(int item, int frame)
{
    switch (item) {
    case 13: art_sprite(&D_STAR, 31, 26, false); break;
    case 14: art_sprite(&D_CORAL, 13, 22, false); break;
    case 15: art_sprite(&D_AMPHORA, 22, 22, false); break;
    case 16: art_sprite(&D_SHELL, 36, 24, false); break;
    case 17: art_sprite(&D_CHEST, 26, 24, false); break;
    case 18: art_sprite(&D_CASTLE, 5, 21, false); break;
    case 19: {   // passa piano in alto
        int x = (frame / 3) % (S.lw + 24) - 12;
        art_sprite(&D_SUB, x, 3, false);
        if ((frame / 4) & 1) art_px(x - 2, 6, 0xA8E6FF);
        break;
    }
    }
}

static art_env_t E = {.daypart = DAY_DAY, .season = 0xFF};   // 0xFF: nessuna stagione

void art_env(const art_env_t *e)
{
    if (e) E = *e;
    else { memset(&E, 0, sizeof(E)); E.daypart = DAY_DAY; E.season = 0xFF; }
    holiday_now = E.holiday;
}

// particelle che scendono o salgono (neve marina, petali, foglie, coriandoli)
static void drift(int frame, int n, int speed, bool up, const uint32_t *cols, int ncol)
{
    for (int i = 0; i < n; i++) {
        int x = (i * 23 + 7) % S.lw;
        int y = (i * 11 + frame * speed / 4) % 30;
        if (up) y = 29 - y;
        x += ((frame / 6 + i) & 2) ? 1 : 0;
        art_px(x, y, cols[i % ncol]);
    }
}

void art_background(int frame)
{
    // acqua: colori del momento del giorno
    static const uint32_t top[] = {0x3A2E4A, 0x0B1C2C, 0x3A2416, 0x03080F};
    static const uint32_t bot[] = {0x2E5A70, 0x174560, 0x2C4A58, 0x0A1E2C};
    int dp = E.daypart <= DAY_NIGHT ? E.daypart : DAY_DAY;
    uint32_t t = top[dp], b = bot[dp];
    if (dp == DAY_DAY && E.season == SEASON_SUMMER) { t = 0x0E2638; b = 0x1C5470; }   // estate: più luce
    if (dp == DAY_DAY && E.season == SEASON_WINTER) { t = 0x0B1824; b = 0x153A50; }
    for (int y = 0; y < 29; y++) art_rect(0, y, S.lw, 1, lerp(t, b, y * 255 / 28));
    if (dp == DAY_DAY && E.season == SEASON_SUMMER)   // raggi di sole
        for (int k = 0; k < 3; k++)
            for (int y = 0; y < 20; y++) art_px(10 + k * 18 + y / 3 + ((frame / 10 + k) & 1), y, lerp(t, 0xFFFFFF, 40));
    if (dp == DAY_NIGHT)   // plancton luminoso
        for (int i = 0; i < 7; i++)
            if (((frame / 5) + i * 3) % 7 < 5) art_px((i * 19 + 5) % S.lw, (i * 7 + 3) % 22, 0x4FD8C8);
    if (dp == DAY_DUSK || dp == DAY_DAWN)   // riflesso del sole in superficie
        art_rect(S.lw / 2 - 6 + (frame / 8) % 3, 0, 12, 1, dp == DAY_DUSK ? 0xFF9F55 : 0xF2A6C0);

    for (int y = 29; y < S.lh; y++)
        for (int x = 0; x < S.lw; x++)
            art_px(x, y, ((x * 7 + y * 3) % 11 == 0) ? pal('S') : pal('s'));
    weed(2, 7, frame);
    weed(4, 4, frame + 3);
    weed(S.lw - 4, 6, frame + 5);
    art_rect(S.lw - 9, 27, 3, 2, pal('x'));
    art_px(S.lw - 7, 28, pal('X'));

    // stagione
    static const uint32_t snow[] = {0xEDEDED, 0xC8D6E2}, petals[] = {0xF28BB8, 0xFAC3DB, 0xFFFFFF},
                          leaves[] = {0xD9822B, 0xB5531E, 0xE8B23A};
    switch (E.season) {
    case SEASON_WINTER: drift(frame, 9, 2, false, snow, 2); break;
    case SEASON_SPRING: drift(frame, 6, 1, false, petals, 3); break;
    case SEASON_AUTUMN:   // foglie che galleggiano in superficie
        for (int i = 0; i < 4; i++) art_rect((i * 17 + frame / 7) % (S.lw + 2) - 1, (i & 1), 2, 1, leaves[i % 3]);
        break;
    }

    // decorazioni comprate
    for (int it = 1; it < ART_ITEM_COUNT; it++)
        if (ART_ITEMS[it].kind == ITEM_DECO && (E.deco >> it & 1)) art_deco(it, frame);

    // feste
    static const uint32_t confetti[] = {0xFFD23C, 0xE5484D, 0x3F9FE0, 0x3DBA5A, 0xC77DFF};
    switch (E.holiday) {
    case HOL_CHRISTMAS: art_sprite(&D_TREE, S.lw - 17, 20, false); drift(frame, 9, 2, false, snow, 2); break;
    case HOL_HALLOWEEN: art_sprite(&D_PUMPKIN, S.lw - 17, 24, false); break;
    case HOL_EASTER:    art_sprite(&D_EASTER, S.lw - 15, 23, false); break;
    case HOL_BEFANA:    art_sprite(&D_SOCK, S.lw - 15, 21, false); break;
    case HOL_VALENTINE:
        for (int i = 0; i < 3; i++) art_sprite(&SPR_HEART, (i * 21 + 6) % S.lw, 20 - (frame / 2 + i * 9) % 22, false);
        break;
    case HOL_NYE: case HOL_NEWYEAR:   // fuochi d'artificio in superficie
        for (int k = 0; k < 2; k++) {
            int ph = (frame / 2 + k * 7) % 14, cx = 14 + k * 30, cy = 5 + k * 2;
            if (ph < 8) for (int a = 0; a < 8; a++) {
                static const int8_t dx[] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[] = {0, 1, 1, 1, 0, -1, -1, -1};
                art_px(cx + dx[a] * ph / 2, cy + dy[a] * ph / 2, confetti[(a + k) % 5]);
            }
        }
        break;
    case HOL_OCTOPUS: drift(frame, 12, 2, false, confetti, 5); break;
    case HOL_FERRAGOSTO:
        art_rect(S.lw - 12, 0, 6, 2, 0xFFD23C);   // il sole tremolante in superficie
        drift(frame, 5, 1, true, confetti, 1);
        break;
    }
}
