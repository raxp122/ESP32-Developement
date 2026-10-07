// app_snake.c — Snake in pixel art, con i colori del Polipetto: un serpente di quadratini
// arancioni (la testa ha gli occhi) che mangia i pesci gialli. Si gira con gli swipe nelle
// quattro direzioni; i muri e la coda fanno perdere. Più pesci, più veloce.
// BOOT o dito tenuto: pausa. In pausa e a fine partita: destra riprende/ricomincia,
// sinistra esce. Il record resta in memoria (NVS).
#include "apps.h"
#include "pet_art.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "nvs.h"

#define SC    2      // un pixel logico = 2×2 pixel dello schermo
static int CELL;      // una casella in pixel logici: 6 sul 3,49", 8 sul tondo (c'è più spazio)
#define MAXLEN 1600

// colori (gli stessi del Polipetto)
#define COL_SEA    0x0B1A26
#define COL_SEA2   0x0D1F2D   // caselle alterne: la griglia si intravede
#define COL_WALL   0x24425A
#define COL_BODY   0xD97757
#define COL_SHADE  0xA9503A
#define COL_LIGHT  0xF4B19A
#define COL_EYE    0x1E1616
#define COL_FISH   0xFFD23C
#define COL_FISH2  0xE0A82A

static int GW, GH, LW, LH;           // griglia (caselle) e superficie (pixel logici)
static uint16_t *buf;
static uint32_t stride;
static lv_obj_t *canvas, *l_score, *l_best, *l_msg, *l_sub;

typedef struct { int8_t x, y; } pt_t;
static pt_t *body;                   // body[0] = testa
static int len, dir, ndir[2], nq;    // direzione: 0 destra, 1 giù, 2 sinistra, 3 su
static pt_t food;
static int score, best;
static uint32_t step_ms, last_step;
static enum { S_READY, S_PLAY, S_PAUSE, S_OVER } st;
static lv_timer_t *tmr;
static int frame;

static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};

/* ---------------- record ---------------- */

static void best_load(void)
{
    nvs_handle_t h;
    uint16_t v = 0;
    if (nvs_open("snake", NVS_READONLY, &h) == ESP_OK) { nvs_get_u16(h, "best", &v); nvs_close(h); }
    best = v;
}

static void best_save(void)
{
    nvs_handle_t h;
    if (nvs_open("snake", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u16(h, "best", (uint16_t)best);
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- disegno ---------------- */

static const char *const FISH_A[] = {".yy..y", "ykyyyy", "yyyyYy", ".yy..y"};
static const char *const FISH_B[] = {".yy...", "ykyyyy", "yyyyYy", ".yy...", };

static void draw_fish(int x, int y, int f)
{
    const char *const *rows = f ? FISH_B : FISH_A;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 6; i++) {
            char c = rows[j][i];
            if (c == 'y') art_px(x + i, y + j, COL_FISH);
            else if (c == 'Y') art_px(x + i, y + j, COL_FISH2);
            else if (c == 'k') art_px(x + i, y + j, COL_EYE);
        }
}

static void draw_segment(int cx, int cy, int k)
{
    int x = 1 + cx * CELL, y = 1 + cy * CELL;
    bool tail = k == len - 1 && len > 1;
    int s = CELL - 1 - (tail ? 2 : 0), o = tail ? 1 : 0;   // un pixel di stacco: la griglia; la coda più piccola
    x += o; y += o;
    uint32_t body_c = st == S_OVER && frame < 60 && (frame / 10) % 2 ? 0xFFFFFF : COL_BODY;   // lampeggia
    art_rect(x, y, s, s, body_c);
    art_rect(x, y + s - 1, s, 1, COL_SHADE);   // ombra sotto e a destra
    art_rect(x + s - 1, y, 1, s, COL_SHADE);
    art_px(x, y, COL_LIGHT);                   // riflesso
    if (s >= 7) {   // quadratini grandi: riflesso più ampio e angoli smussati
        art_px(x + 1, y, COL_LIGHT); art_px(x, y + 1, COL_LIGHT);
        art_px(x, y, COL_SEA); art_px(x + s - 1, y, COL_SEA); art_px(x, y + s - 1, COL_SEA); art_px(x + s - 1, y + s - 1, COL_SEA);
    }
    if (k) return;
    // testa: due occhi verso la direzione di marcia (sul tondo 2×2 con il riflesso bianco)
    int e = s >= 7 ? 2 : 1, a = 1, f = s - 1 - e;   // lato (2×2 sul tondo), dietro, davanti
    int ex[2], ey[2];
    if (dir == 0)      { ex[0] = ex[1] = f; ey[0] = a; ey[1] = f; }
    else if (dir == 2) { ex[0] = ex[1] = a; ey[0] = a; ey[1] = f; }
    else if (dir == 1) { ey[0] = ey[1] = f; ex[0] = a; ex[1] = f; }
    else               { ey[0] = ey[1] = a; ex[0] = a; ex[1] = f; }
    uint32_t ec = st == S_OVER ? COL_SHADE : COL_EYE;
    for (int i = 0; i < 2; i++) {
        art_rect(x + ex[i], y + ey[i], e, e, ec);
        if (e == 2 && st != S_OVER) art_px(x + ex[i], y + ey[i], 0xFFFFFF);
    }
}

static void draw(void)
{
    art_begin(buf, LW, LH, SC, stride);
    art_fill(COL_WALL);
    for (int cy = 0; cy < GH; cy++)
        for (int cx = 0; cx < GW; cx++)
            art_rect(1 + cx * CELL, 1 + cy * CELL, CELL, CELL, (cx + cy) & 1 ? COL_SEA2 : COL_SEA);
    if (CELL >= 8) art_sprite(&SPR_FISH, 1 + food.x * CELL, 1 + food.y * CELL + 1 + ((frame / 20) & 1), false);   // galleggia
    else draw_fish(1 + food.x * CELL, 1 + food.y * CELL + 1, (frame / 20) & 1);
    for (int k = len - 1; k >= 0; k--) draw_segment(body[k].x, body[k].y, k);
    lv_obj_invalidate(canvas);
}

static void hud(void)
{
    char b[32];
    snprintf(b, sizeof(b), "Pesci %d", score);
    ui_set_text(l_score, b);
    snprintf(b, sizeof(b), "Record %d", best);
    ui_set_text(l_best, b);
    const char *m = NULL, *s = NULL;
    if (st == S_READY) { m = "Snake"; s = "Swipe per partire · sinistra esce"; }
    else if (st == S_PAUSE) { m = "Pausa"; s = "Destra riprende · sinistra esce"; }
    else if (st == S_OVER) { m = score > 0 && score == best ? "Nuovo record!" : "Fine"; s = "Destra ricomincia · sinistra esce"; }
    if (m) {
        ui_set_text(l_msg, m);
        ui_set_text(l_sub, s);
        lv_obj_clear_flag(l_msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(l_sub, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l_msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(l_sub, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- gioco ---------------- */

static bool on_snake(int x, int y, int upto)
{
    for (int k = 0; k < upto; k++) if (body[k].x == x && body[k].y == y) return true;
    return false;
}

static void place_food(void)
{
    if (len >= GW * GH) return;
    do {
        food.x = esp_random() % GW;
        food.y = esp_random() % GH;
    } while (on_snake(food.x, food.y, len));
}

static void reset(void)
{
    len = 4;
    dir = 0;
    nq = 0;
    int cx = GW / 2, cy = GH / 2;
    for (int k = 0; k < len; k++) body[k] = (pt_t){(int8_t)(cx - k), (int8_t)cy};
    score = 0;
    step_ms = 170;
    place_food();
    st = S_READY;
}

static void step(void)
{
    if (nq) { dir = ndir[0]; ndir[0] = ndir[1]; nq--; }
    pt_t h = {(int8_t)(body[0].x + DX[dir]), (int8_t)(body[0].y + DY[dir])};
    bool eat = h.x == food.x && h.y == food.y;
    // la coda si sposta in questo passo (a meno che non mangi): ci si può entrare
    if (h.x < 0 || h.y < 0 || h.x >= GW || h.y >= GH || on_snake(h.x, h.y, eat ? len : len - 1)) {
        st = S_OVER;
        frame = 0;
        if (score > best) { best = score; best_save(); }
        hud();
        return;
    }
    if (eat && len < MAXLEN) len++;
    memmove(&body[1], &body[0], (len - 1) * sizeof(pt_t));
    body[0] = h;
    if (eat) {
        score++;
        if (step_ms > 70) step_ms -= 4;   // più pesci, più veloce
        place_food();
        hud();
    }
}

static void turn(int d)
{
    // al massimo due svolte in coda (due swipe rapidi); niente inversione su se stesso
    int last = nq ? ndir[nq - 1] : dir;
    if (d == last || d == (last + 2) % 4 || nq == 2) return;
    ndir[nq++] = d;
}

static void tick_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    frame++;
    bool moved = false;
    if (st == S_PLAY && now - last_step >= step_ms) {
        last_step = now;
        step();
        moved = true;
    }
    // si ridisegna a ogni passo e 5 volte al secondo per il pesce che scodinzola
    if (moved || frame % 10 == 0) draw();
}

static void start(int d)
{
    if (st == S_OVER) reset();
    st = S_PLAY;
    if (d >= 0) turn(d);
    last_step = lv_tick_get();
    hud();
}

#ifdef SEISMO_SIM
// simulatore: una direzione sicura verso il pesce (per le immagini di prova)
int snake_sim_dir(void)
{
    int bestd = dir, bests = 1 << 30;
    for (int d = 0; d < 4; d++) {
        if (d == (dir + 2) % 4) continue;
        int x = body[0].x + DX[d], y = body[0].y + DY[d];
        if (x < 0 || y < 0 || x >= GW || y >= GH || on_snake(x, y, len - 1)) continue;
        int s = abs(x - food.x) + abs(y - food.y);
        if (s < bests) { bests = s; bestd = d; }
    }
    return bestd;
}
#endif

/* ---------------- schermata ---------------- */

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    bool r = SCR_ROUND;
    int pw, ph, px, py;
    if (r) {   // un quadrato dentro il cerchio
        CELL = 8;
        GW = GH = 20;
        pw = ph = (GW * CELL + 2) * SC;
        px = (SCR_W - pw) / 2;
        py = (SCR_H - ph) / 2;
    } else {   // tutta la larghezza, sotto una riga per i punti
        CELL = 6;
        GW = (SCR_W / SC - 2) / CELL;
        GH = ((SCR_H - 24) / SC - 2) / CELL;
        pw = (GW * CELL + 2) * SC;
        ph = (GH * CELL + 2) * SC;
        px = (SCR_W - pw) / 2;
        py = SCR_H - ph;
    }
    LW = pw / SC;
    LH = ph / SC;
    stride = lv_draw_buf_width_to_stride(pw, LV_COLOR_FORMAT_RGB565) / 2;
    if (!body) body = heap_caps_malloc(MAXLEN * sizeof(pt_t), MALLOC_CAP_SPIRAM);
    if (!buf) buf = heap_caps_aligned_alloc(64, stride * 2 * ph, MALLOC_CAP_SPIRAM);   // una volta (stessa scheda)
    canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(canvas, buf, pw, ph, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, px, py);

    l_score = mk(root, &font_m, lv_color_hex(COL_FISH));
    l_best = mk(root, &font_s, C_DIM);
    if (r) {
        lv_obj_align(l_score, LV_ALIGN_TOP_MID, 0, py - 40);
        lv_obj_align(l_best, LV_ALIGN_TOP_MID, 0, py + ph + 8);
    } else {
        lv_obj_set_style_text_font(l_score, &font_s, 0);
        lv_obj_set_pos(l_score, 10, 3);
        lv_obj_align(l_best, LV_ALIGN_TOP_RIGHT, -10, 3);
    }
    // messaggi al centro, su una fascia scura
    l_msg = mk(root, r ? &font_l : &font_m, C_TEXT);
    l_sub = mk(root, &font_s, C_TEXT);
    lv_obj_t *ls[] = {l_msg, l_sub};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(ls[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(ls[i], LV_OPA_70, 0);
        lv_obj_set_style_pad_hor(ls[i], 10, 0);
        lv_obj_set_style_pad_ver(ls[i], 2, 0);
        lv_obj_set_style_radius(ls[i], 6, 0);
    }
    int cy = py + ph / 2;
    lv_obj_align(l_msg, LV_ALIGN_TOP_MID, 0, cy - (r ? 52 : 36));
    lv_obj_align(l_sub, LV_ALIGN_TOP_MID, 0, cy + (r ? 4 : 2));

    best_load();
    if (st != S_PAUSE) reset();   // tornando da un'altra schermata la partita resta in pausa
    frame = 0;
    hud();
    draw();
    tmr = lv_timer_create(tick_cb, 20, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    if (st == S_PLAY) st = S_PAUSE;
    if (ui_closing()) st = S_READY;
}

static bool nav(nav_t ev)
{
    int d = -1;
    if (ev == NAV_NEXT || ev == NAV_PREV) {
        bool up = ev == NAV_NEXT;
        if (g_set.invert_scroll) up = !up;   // come lo scorrimento delle liste
        d = up ? 3 : 1;
    } else if (ev == NAV_SELECT) d = 0;
    else if (ev == NAV_BACK) d = 2;

    switch (st) {
    case S_PLAY:
        if (ev == NAV_BTN || ev == NAV_QUICK) { st = S_PAUSE; hud(); return true; }
        if (d >= 0) { turn(d); return true; }
        return false;
    case S_READY:
        if (ev == NAV_BACK) return false;   // esce
        if (d >= 0) { start(d); return true; }
        if (ev == NAV_BTN) { start(-1); return true; }
        return ev == NAV_QUICK;
    default:   // pausa o fine partita
        if (ev == NAV_BACK) return false;
        if (ev == NAV_SELECT || ev == NAV_BTN) { start(-1); return true; }
        return true;
    }
}

const app_t app_snake = {
    .name = "Snake", .icon = ICON_GAMEPAD,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};
