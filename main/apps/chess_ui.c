// chess_ui.c — scacchiera in pixel art e tocchi (vedi chess_ui.h)
#include "chess_ui.h"
#include "pet_art.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

// pezzi 10×10: '#' contorno, 'o' riempimento, 'k' occhio (il cavallo)
static const char *const PIECES[7][10] = {
    {0},
    {"..........", "....##....", "...#oo#...", "...#oo#...", "....##....", "...#oo#...", "...#oo#...", "..#oooo#..",
     ".#oooooo#.", ".########."},
    {"..........", "....##.#..", "...#oo#o#.", "..#ooooo#.", ".#okoooo#.", "#ooooooo#.", "#oo##ooo#.", ".##.#ooo#.",
     "...#oooo#.", ".########."},
    {"....##....", "...#oo#...", "..#oo#o#..", "..#o#oo#..", "..#oooo#..", "...#oo#...", "....##....", "...#oo#...",
     "..#oooo#..", ".########."},
    {"..........", ".##.##.##.", ".#o#oo#o#.", ".#oooooo#.", "..#oooo#..", "..#oooo#..", "..#oooo#..", ".#oooooo#.",
     ".#oooooo#.", ".########."},
    {".#..##..#.", ".#o#oo#o#.", ".#oooooo#.", "..#oooo#..", "..#oooo#..", "...#oo#...", "...#oo#...", "..#oooo#..",
     ".#oooooo#.", ".########."},
    {"....##....", "...####...", "....##....", ".###oo###.", ".#oooooo#.", ".#oooooo#.", "..#oooo#..", "..#oooo#..",
     ".#oooooo#.", ".########."},
};

#define C_LIGHT   0xEBD3B0
#define C_DARK    0xB07A50
#define C_LAST_L  0xF2E28A
#define C_LAST_D  0xCDB35A
#define C_SEL     0x8FC46A
#define C_HINT_L  0xA9D2F5
#define C_HINT_D  0x6FA6D6
#define C_CHECK   0xE5484D
#define C_CURSOR  0x2D7FF9
#define C_MARK    0x3A2C22

static uint16_t *buf;
static uint32_t stride;

void cb_create(cboard_t *b, lv_obj_t *root, int x, int y, int px)
{
    b->x = x;
    b->y = y;
    b->sc = px / 80;
    b->px = b->sc * 80;
    stride = lv_draw_buf_width_to_stride(b->px, LV_COLOR_FORMAT_RGB565) / 2;
    if (!buf) buf = heap_caps_aligned_alloc(64, stride * 2 * b->px, MALLOC_CAP_SPIRAM);   // una volta (stessa scheda)
    b->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(b->canvas, buf, b->px, b->px, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(b->canvas, x, y);
    lv_obj_clear_flag(b->canvas, LV_OBJ_FLAG_CLICKABLE);
}

void cb_clear_marks(cboard_t *b)
{
    memset(b->mark, 0, sizeof(b->mark));
    b->sel = -1;
}

// casa → angolo in alto a sinistra in pixel logici
static void sq_xy(const cboard_t *b, int sq, int *x, int *y)
{
    int f = CP_FILE(sq), r = CP_RANK(sq);
    if (b->flip) { f = 7 - f; r = 7 - r; }
    *x = f * 10;
    *y = (7 - r) * 10;
}

static void draw_piece(int pc, int x, int y)
{
    int t = CP_TYPE(pc);
    bool black = CP_COLOR(pc);
    uint32_t fill = black ? 0x2E2A2B : 0xF7F3EA, line = black ? 0x0E0C0C : 0x2A2420, eye = black ? 0xCFC6B8 : 0x2A2420;
    for (int j = 0; j < 10; j++)
        for (int i = 0; i < 10; i++) {
            char c = PIECES[t][j][i];
            if (c == '#') art_px(x + i, y + j, line);
            else if (c == 'o') art_px(x + i, y + j, fill);
            else if (c == 'k') art_px(x + i, y + j, eye);
        }
    if (black) art_px(x + 4, y + (t == CP_P ? 2 : 4), 0x5A5355);   // un riflesso: i neri non sembrano buchi
}

void cb_draw(cboard_t *b, const cpos_t *p)
{
    art_begin(buf, 80, 80, b->sc, stride);
    int check = cp_in_check(p) ? p->ksq[p->side] : -1;
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++) {
            int sq = CP_SQ(f, r), x, y;
            sq_xy(b, sq, &x, &y);
            bool light = (f + r) & 1;
            uint32_t c = light ? C_LIGHT : C_DARK;
            if (sq == b->last_from || sq == b->last_to) c = light ? C_LAST_L : C_LAST_D;
            if (sq == b->hint_from || sq == b->hint_to) c = light ? C_HINT_L : C_HINT_D;
            if (sq == b->sel) c = C_SEL;
            if (sq == check) c = C_CHECK;
            art_rect(x, y, 10, 10, c);
        }
    for (int s = 0; s < 128; s++) {
        if (s & 0x88) { s += 7; continue; }
        int x, y;
        sq_xy(b, s, &x, &y);
        if (p->sq[s]) draw_piece(p->sq[s], x, y);
        if (b->show_marks && b->mark[s]) {
            if (p->sq[s]) {   // presa: quattro angoli
                art_rect(x, y, 2, 1, C_MARK); art_rect(x, y, 1, 2, C_MARK);
                art_rect(x + 8, y, 2, 1, C_MARK); art_rect(x + 9, y, 1, 2, C_MARK);
                art_rect(x, y + 9, 2, 1, C_MARK); art_rect(x, y + 8, 1, 2, C_MARK);
                art_rect(x + 8, y + 9, 2, 1, C_MARK); art_rect(x + 9, y + 8, 1, 2, C_MARK);
            } else art_rect(x + 4, y + 4, 2, 2, C_MARK);
        }
    }
    if (b->cursor >= 0) {
        int x, y;
        sq_xy(b, b->cursor, &x, &y);
        art_rect(x, y, 10, 1, C_CURSOR); art_rect(x, y + 9, 10, 1, C_CURSOR);
        art_rect(x, y, 1, 10, C_CURSOR); art_rect(x + 9, y, 1, 10, C_CURSOR);
    }
    lv_obj_invalidate(b->canvas);
}

int cb_hit(const cboard_t *b, int sx, int sy)
{
    int x = sx - b->x, y = sy - b->y;
    if (x < 0 || y < 0 || x >= b->px || y >= b->px) return -1;
    int f = x * 8 / b->px, r = 7 - y * 8 / b->px;
    if (b->flip) { f = 7 - f; r = 7 - r; }
    return CP_SQ(f, r);
}

void cb_move_cursor(cboard_t *b, int df, int dr)
{
    if (b->cursor < 0) { b->cursor = b->flip ? CP_SQ(4, 6) : CP_SQ(4, 1); return; }   // compare sul pedone del re
    if (b->flip) { df = -df; dr = -dr; }
    int f = CP_FILE(b->cursor) + df, r = CP_RANK(b->cursor) + dr;
    if (f < 0) f = 0;
    if (f > 7) f = 7;
    if (r < 0) r = 0;
    if (r > 7) r = 7;
    b->cursor = CP_SQ(f, r);
}

/* ---------------- tocchi ---------------- */

// come la tastiera: il controller perde il dito per qualche lettura, quindi il dito è
// sollevato solo dopo RELEASE_MS senza letture; un tocco lungo o spostato non è un tocco
#define RELEASE_MS 70
#define TAP_MAX_MS 600
static bool t_down, t_moved;
static int t_sx, t_sy;
static uint32_t t_t0, t_seen;

void cb_tap_reset(void) { t_down = false; }

bool cb_tap_poll(int *x, int *y)
{
    int tx, ty;
    uint32_t now = lv_tick_get();
    int lim = SCR_ROUND ? 25 : 18;
    if (input_touch(&tx, &ty)) {
        t_seen = now;
        if (!t_down) { t_down = true; t_moved = false; t_sx = tx; t_sy = ty; t_t0 = now; return false; }
        if (abs(tx - t_sx) > lim || abs(ty - t_sy) > lim) t_moved = true;
        return false;
    }
    if (!t_down || now - t_seen < RELEASE_MS) return false;
    t_down = false;
    if (t_moved || t_seen - t_t0 > TAP_MAX_MS) return false;
    *x = t_sx;
    *y = t_sy;
    return true;
}
