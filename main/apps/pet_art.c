// pet_art.c — pixel art originale del Polipetto: un polpetto arancione che cresce
// (neonato → bimbo → ragazzo → adulto, con cinque forme adulte), più cibo, effetti,
// fondale marino e icone. Gli sprite sono stringhe: un carattere = un pixel.
#include "pet_art.h"
#include "pet_core.h"

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
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) art_px(x + i, y + j, rgb);
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
SPR(SPR_Z, 3, 3, "zzz", ".z.", "zzz");
SPR(SPR_BANG, 1, 5, "r", "r", "r", ".", "r");
SPR(SPR_QUESTION, 3, 5, "zz.", "..z", ".z.", "...", ".z.");
SPR(SPR_ANGER, 3, 3, "r.r", ".r.", "r.r");
SPR(SPR_HALO, 6, 2, ".tttt.", "t....t");

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

void art_pet_size(int stage, int form, int *w, int *h)
{
    const body_t *b = body_for(stage, form);
    *w = b->w;
    *h = b->h + b->tl;
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
            bpx(c, r, ch);
        }

    // tentacoli: la metà bassa ondeggia, la punta fa il ricciolo
    int half = b->tn / 2;
    for (int i = 0; i < b->tn; i++) {
        int dir = i < half ? -1 : 1;
        if ((b->tn & 1) && i == half) dir = (frame & 1) ? 1 : -1;
        for (int k = 0; k < b->tl; k++) {
            int off = (k >= b->tl / 2 && (frame & 1)) ? dir : 0;
            bool tip = k == b->tl - 1;
            bpx(b->tx[i] + off, b->h + k, tip ? 'o' : 'O');
            if (tip) bpx(b->tx[i] + off + ((frame & 1) ? -dir : dir), b->h + k, 'o');
        }
    }

    if (tint == TINT_WHITE) return;
    int lk = flip ? -look : look;
    eye(b->ex + lk, b->ey, b->esz, e, false);
    eye(b->erx + lk, b->ey, b->esz, e, true);
    mouth(b, e, frame);
    if (b->cy && tint == TINT_NONE && (e == EXPR_NORMAL || e == EXPR_HAPPY || e == EXPR_EAT || e == EXPR_BLINK)) {
        bpx(b->clx, b->cy, 'p');
        bpx(b->crx, b->cy, 'p');
    }
    if (tint != TINT_GHOST) overlays(b, stage == PET_DEAD ? FORM_BASE : form, e);
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

void art_background(int frame)
{
    for (int y = 0; y < 29; y++) art_rect(0, y, S.lw, 1, lerp(0x0B1C2C, 0x174560, y * 255 / 28));
    for (int y = 29; y < S.lh; y++)
        for (int x = 0; x < S.lw; x++)
            art_px(x, y, ((x * 7 + y * 3) % 11 == 0) ? pal('S') : pal('s'));
    weed(2, 7, frame);
    weed(4, 4, frame + 3);
    weed(S.lw - 4, 6, frame + 5);
    art_rect(S.lw - 9, 27, 3, 2, pal('x'));
    art_px(S.lw - 7, 28, pal('X'));
}
