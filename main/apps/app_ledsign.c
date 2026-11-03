// app_ledsign.c — Insegna LED: scrivi una frase e la vedi scorrere a tutto schermo come
// un pannello a matrice di LED. Funziona su entrambe le schede (3,49" e tondo).
// Impostazioni: colore, modalità arcobaleno, velocità, luminosità, direzione, stile.
// Le frasi si salvano con un nome e si richiamano dall'elenco (come i preferiti dei dadi).
#include "apps.h"
#include "ui.h"
#include "display.h"
#include "keyboard.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "nvs.h"

/* ---------------- font 5×7 (una colonna per byte, bit 0 = riga in alto) ---------------- */
// ASCII stampabili 0x20..0x7E. Le lettere accentate si mappano sulla lettera base.
#define GLYPH_W 5
static const uint8_t FONT[95][GLYPH_W] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x08,0x2A,0x1C,0x08},
};
#define FONT_ROWS 7

// Lettera base per una vocale/consonante accentata (Unicode) — per un'insegna basta e avanza.
static char base_of(uint32_t cp)
{
    switch (cp) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: return 'a';
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: return 'A';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'E';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'O';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'U';
    case 0xE7: return 'c'; case 0xC7: return 'C';
    case 0xF1: return 'n'; case 0xD1: return 'N';
    case 0x20AC: return 'E';   // €
    default: return 0;
    }
}

static const uint8_t *glyph_of(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) return FONT[cp - 0x20];
    char b = base_of(cp);
    if (b) return FONT[(int)b - 0x20];
    return FONT['?' - 0x20];
}

// Un carattere UTF-8 → codepoint; avanza il puntatore.
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    if (s[0] < 0x80) { cp = s[0]; if (s[0]) (*p)++; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); *p += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3;
    } else { cp = '?'; (*p)++; }
    return cp;
}

/* ---------------- impostazioni (NVS "ledsign") ---------------- */

typedef struct { const char *name; uint32_t rgb; } palette_t;
static const palette_t PAL[] = {
    {"Rosso", 0xFF2020}, {"Ambra", 0xFF9000}, {"Giallo", 0xFFE000}, {"Verde", 0x20FF40},
    {"Ciano", 0x20E0FF}, {"Blu", 0x4060FF}, {"Magenta", 0xFF40D0}, {"Rosa", 0xFF80C0},
    {"Bianco", 0xFFFFFF},
};
#define N_PAL (int)(sizeof(PAL) / sizeof(PAL[0]))

static const char *const SPEED_NAME[] = {"Lenta", "Media", "Veloce", "Velocissima"};
static const int SPEED_MS[] = {120, 70, 42, 24};   // ms per colonna
#define N_SPEED 4
static const int BRIGHT_PCT[] = {25, 50, 75, 100};
#define N_BRIGHT 4
static const char *const DIR_NAME[] = {"Verso sinistra", "Verso destra", "Fisso (se entra)"};
#define DIR_LEFT 0
#define DIR_RIGHT 1
#define DIR_STILL 2
#define N_DIR 3

#define CFG_MAGIC 0x4C
typedef struct {
    uint8_t magic, color, rainbow, speed, bright, dir, round_dots, grid;
} cfg_t;
static cfg_t cfg = {CFG_MAGIC, 1, 0, 1, 3, DIR_LEFT, 1, 1};   // ambra, media, 100%, ← , tondi, griglia

static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open("ledsign", NVS_READONLY, &h) != ESP_OK) return;
    cfg_t t;
    size_t n = sizeof(t);
    if (nvs_get_blob(h, "cfg", &t, &n) == ESP_OK && n == sizeof(t) && t.magic == CFG_MAGIC) {
        cfg = t;
        if (cfg.color >= N_PAL) cfg.color = 0;
        if (cfg.speed >= N_SPEED) cfg.speed = 1;
        if (cfg.bright >= N_BRIGHT) cfg.bright = N_BRIGHT - 1;
        if (cfg.dir >= N_DIR) cfg.dir = DIR_LEFT;
    }
    nvs_close(h);
}
static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ledsign", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &cfg, sizeof(cfg));
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- frase corrente e frasi salvate ---------------- */

#define MSG_LEN 48        // byte per frase (UTF-8)
#define NAME_LEN 24
#define MSG_MAX 16
typedef struct { char name[NAME_LEN]; char text[MSG_LEN]; } saved_t;

static char cur_text[MSG_LEN];       // la frase da mostrare
static saved_t saved[MSG_MAX];
static int n_saved;
static bool saved_loaded;

static void saved_load(void)
{
    saved_loaded = true;
    n_saved = 0;
    memset(saved, 0, sizeof(saved));
    nvs_handle_t h;
    if (nvs_open("ledsign", NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(saved);
    nvs_get_blob(h, "msgs", saved, &n);
    uint8_t cnt = 0;
    nvs_get_u8(h, "nmsg", &cnt);
    n_saved = cnt > MSG_MAX ? MSG_MAX : cnt;
    size_t cn = sizeof(cur_text);
    nvs_get_str(h, "last", cur_text, &cn);
    nvs_close(h);
}
static void saved_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ledsign", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "msgs", saved, sizeof(saved));
    nvs_set_u8(h, "nmsg", (uint8_t)n_saved);
    nvs_set_str(h, "last", cur_text);
    nvs_commit(h);
    nvs_close(h);
}
static void cur_save(void)   // ricorda l'ultima frase scritta
{
    nvs_handle_t h;
    if (nvs_open("ledsign", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "last", cur_text);
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- colonne della frase ---------------- */

#define COLS_MAX 512
static uint8_t colbits[COLS_MAX];   // ogni colonna: 7 bit (riga 0 = in alto)
static int n_cols;

static void build_cols(const char *text)
{
    n_cols = 0;
    const char *p = text;
    bool first = true;
    while (*p && n_cols < COLS_MAX - GLYPH_W - 1) {
        uint32_t cp = utf8_next(&p);
        if (!first) colbits[n_cols++] = 0;   // una colonna di stacco fra le lettere
        first = false;
        const uint8_t *g = glyph_of(cp);
        for (int c = 0; c < GLYPH_W && n_cols < COLS_MAX; c++) colbits[n_cols++] = g[c];
    }
}

/* ---------------- colori ---------------- */

static uint32_t hsv(int h, int s, int v)   // h 0..359, s/v 0..255
{
    int r, g, b, i = h / 60, f = h % 60;
    int p = v * (255 - s) / 255;
    int q = v * (255 - s * f / 60) / 255;
    int t = v * (255 - s * (60 - f) / 60) / 255;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static uint32_t scale_rgb(uint32_t c, int pct)
{
    int r = ((c >> 16) & 0xFF) * pct / 100, g = ((c >> 8) & 0xFF) * pct / 100, b = (c & 0xFF) * pct / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static inline uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F));
}

/* ---------------- schermata a tutto schermo ---------------- */

static lv_obj_t *canvas;
static uint16_t *buf;
static int stride_px;          // colonne nel buffer (per allineamento)
static lv_timer_t *tmr;
static int dot, cols_vis, xoff, yoff, led;
static int scroll_base;
static int rainbow_phase;
static uint32_t acc_ms, phase_ms;
static bool still;             // frase ferma (entra tutta e direzione "fisso")
static int xoff_cols(void);

static inline void px(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    buf[y * stride_px + x] = c;
}
static void fill_cell(int cx, int cy, uint16_t c)
{
    int x0 = xoff + cx * dot + (dot - led) / 2, y0 = yoff + cy * dot + (dot - led) / 2;
    int corner = (cfg.round_dots && led >= 5) ? 1 : 0;   // smussa gli angoli: pare un LED tondo
    for (int y = 0; y < led; y++)
        for (int x = 0; x < led; x++) {
            if (corner && (x == 0 || x == led - 1) && (y == 0 || y == led - 1)) continue;
            px(x0 + x, y0 + y, c);
        }
}

static void render(void)
{
    // sfondo nero
    for (int y = 0; y < SCR_H; y++)
        memset(&buf[y * stride_px], 0, SCR_W * 2);

    uint16_t off = to565(0x0C0C0C);   // LED spenti (griglia)
    int span = n_cols + cols_vis;     // dopo la frase, uno spazio pari allo schermo, poi si ripete
    if (span < 1) span = 1;
    int bright = BRIGHT_PCT[cfg.bright];

    for (int c = 0; c < cols_vis; c++) {
        int src;
        if (still) src = c - xoff_cols();
        else {
            src = scroll_base + c;
            src = ((src % span) + span) % span;
        }
        uint8_t bits = (src >= 0 && src < n_cols) ? colbits[src] : 0;
        // colore della colonna
        uint32_t rgb;
        if (cfg.rainbow) {
            int h = ((still ? c : scroll_base + c) * 10 + rainbow_phase) % 360;
            if (h < 0) h += 360;
            rgb = hsv(h, 255, 255);
        } else rgb = PAL[cfg.color].rgb;
        uint16_t on = to565(scale_rgb(rgb, bright));
        for (int r = 0; r < FONT_ROWS; r++) {
            if (bits & (1 << r)) fill_cell(c, r, on);
            else if (cfg.grid) fill_cell(c, r, off);
        }
    }
    lv_obj_invalidate(canvas);
}
// per la modalità "fisso": colonna di partenza che centra la frase
static int xoff_cols(void) { return (cols_vis - n_cols) / 2; }

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t step = SPEED_MS[cfg.speed];
    acc_ms += 15;
    bool redraw = false;
    if (!still) {
        if (acc_ms >= step) {
            acc_ms = 0;
            scroll_base += (cfg.dir == DIR_RIGHT) ? -1 : 1;
            rainbow_phase += 6;
            redraw = true;
        }
    } else if (cfg.rainbow) {
        phase_ms += 15;
        if (phase_ms >= 70) { phase_ms = 0; rainbow_phase += 6; redraw = true; }
    }
    if (redraw) render();
}

static void show_enter(lv_obj_t *root, void *arg)
{
    (void)arg;
    lv_obj_set_style_bg_color(root, C_BG, 0);
    build_cols(cur_text[0] ? cur_text : "Insegna LED");

    int rows = FONT_ROWS;
    dot = SCR_W / 22;                       // circa 22 colonne di LED
    int dmax = SCR_H / (rows + 2);
    if (dot > dmax) dot = dmax;
    if (dot < 3) dot = 3;
    cols_vis = SCR_W / dot;
    xoff = (SCR_W - cols_vis * dot) / 2;
    yoff = (SCR_H - rows * dot) / 2;
    int gap = dot >= 10 ? 2 : 1;
    led = dot - gap;
    if (led < 1) led = 1;

    still = (cfg.dir == DIR_STILL) && (n_cols <= cols_vis);
    scroll_base = still ? 0 : -cols_vis;    // entra da destra
    rainbow_phase = acc_ms = phase_ms = 0;

    uint32_t sb = lv_draw_buf_width_to_stride(SCR_W, LV_COLOR_FORMAT_RGB565);
    stride_px = sb / 2;
    if (!buf) buf = heap_caps_aligned_alloc(64, sb * SCR_H, MALLOC_CAP_SPIRAM);
    if (!buf) { ui_toast("Memoria insufficiente"); return; }   // niente canvas: lo swipe a sinistra esce
    canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(canvas, buf, SCR_W, SCR_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);

    ui_set_brightness_override(100);        // un'insegna va vista: massima luce del pannello
    render();
    tmr = lv_timer_create(tick_cb, 15, NULL);
}
static void show_leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    ui_set_brightness_override(-1);
    canvas = NULL;   // gli oggetti LVGL li libera il framework; il buffer resta per riaprire
}
static bool show_nav(nav_t ev)
{
    if (ev == NAV_BACK || ev == NAV_BTN) return false;   // esce
    if (ev == NAV_NEXT) { if (cfg.speed + 1 < N_SPEED) { cfg.speed++; cfg_save(); } return true; }
    if (ev == NAV_PREV) { if (cfg.speed > 0) { cfg.speed--; cfg_save(); } return true; }
    return true;   // niente altro fa uscire per sbaglio
}
static const app_t app_show = {
    .name = "Insegna", .enter = show_enter, .leave = show_leave, .nav = show_nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

static void show_current(void)
{
    if (!cur_text[0]) { ui_toast("Scrivi prima una frase"); return; }
    ui_push(&app_show, NULL);
}

// ganci per il simulatore (non usati dal firmware)
void ledsign_sim_config(int color, int rainbow, int speed, int dir)
{
    cfg.color = color; cfg.rainbow = rainbow; cfg.speed = speed; cfg.dir = dir;
}
void ledsign_sim_show(const char *t)
{
    snprintf(cur_text, sizeof(cur_text), "%s", t);
    ui_push(&app_show, NULL);
}

/* ---------------- tastiera: scrivi la frase ---------------- */

static char kb_buf[MSG_LEN];
static void kb_done(const char *t, void *arg)
{
    keyboard_close();
    ui_pop();
    if (!t) return;
    snprintf(cur_text, sizeof(cur_text), "%s", t);
    cur_save();
    if (cur_text[0]) ui_push(&app_show, NULL);   // scrivi e mostra
}
static void kb_enter(lv_obj_t *root, void *arg)
{
    (void)arg;
    snprintf(kb_buf, sizeof(kb_buf), "%s", cur_text);
    keyboard_open(root, "Scrivi la frase", kb_buf, false, MSG_LEN - 1, kb_done, NULL);
}
static void kb_leave(void) { keyboard_close(); }
static bool kb_nav(nav_t ev) { return keyboard_nav(ev); }
static const app_t app_kb = {
    .name = "Scrivi", .enter = kb_enter, .leave = kb_leave, .nav = kb_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

/* ---------------- frasi salvate ---------------- */

static int edit_idx = -1;

// rinomina (arg=1) o nome di una frase nuova (arg=NULL), via tastiera
static char name_buf[NAME_LEN];
static void name_done(const char *t, void *arg)
{
    keyboard_close();
    ui_pop();
    if (!t || !t[0]) return;
    if (arg) {   // rinomina quella in modifica
        if (edit_idx >= 0 && edit_idx < n_saved) {
            snprintf(saved[edit_idx].name, NAME_LEN, "%s", t);
            saved_save();
        }
        return;
    }
    int i = 0;
    while (i < n_saved && strcasecmp(saved[i].name, t)) i++;   // stesso nome: si aggiorna
    if (i == n_saved) {
        if (n_saved >= MSG_MAX) { ui_toast("Frasi piene: eliminane una"); return; }
        n_saved++;
    }
    snprintf(saved[i].name, NAME_LEN, "%s", t);
    snprintf(saved[i].text, MSG_LEN, "%s", cur_text);
    saved_save();
    char b[48];
    snprintf(b, sizeof(b), "Salvata: %.24s", saved[i].name);
    ui_toast(b);
}
static void name_enter(lv_obj_t *root, void *arg)
{
    if (arg && edit_idx >= 0 && edit_idx < n_saved) snprintf(name_buf, NAME_LEN, "%s", saved[edit_idx].name);
    else name_buf[0] = 0;
    keyboard_open(root, arg ? "Nuovo nome" : "Nome della frase (es. Buon compleanno)", name_buf, false,
                  NAME_LEN - 1, name_done, arg);
}
static void name_leave(void) { keyboard_close(); }
static bool name_nav(nav_t ev) { return keyboard_nav(ev); }
static const app_t app_name = {
    .name = "Nome", .enter = name_enter, .leave = name_leave, .nav = name_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

// ---- menu di modifica di una frase (Mostra / Rinomina / Elimina) ----
static char ed_title[48];
static void ed_retitle(void)
{
    snprintf(ed_title, sizeof(ed_title), "Frase » %.24s", edit_idx >= 0 ? saved[edit_idx].name : "");
}
static void ed_show(void)
{
    if (edit_idx < 0 || edit_idx >= n_saved) return;
    snprintf(cur_text, sizeof(cur_text), "%s", saved[edit_idx].text);
    cur_save();
    ui_push(&app_show, NULL);
}
static void ed_rename(void) { ui_push(&app_name, (void *)1); }
static void ed_delete(void)
{
    if (edit_idx < 0 || edit_idx >= n_saved) return;
    for (int i = edit_idx; i + 1 < n_saved; i++) saved[i] = saved[i + 1];
    n_saved--;
    memset(&saved[n_saved], 0, sizeof(saved[0]));
    saved_save();
    ui_pop(); ui_pop();   // torna all'elenco
    ui_toast("Frase eliminata");
}
static const menu_item_t ed_items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Mostra", .on_select = ed_show},
    {.icon = LV_SYMBOL_EDIT, .label = "Rinomina", .on_select = ed_rename},
    {.icon = LV_SYMBOL_TRASH, .label = "Elimina", .on_select = ed_delete, .confirm = true},
};
static menu_t ed_menu = {ed_title, ed_items, sizeof(ed_items) / sizeof(ed_items[0]), 0, NULL};
static void ed_open(void *arg)
{
    edit_idx = (int)(intptr_t)arg;
    ed_menu.sel = 0;
    ed_retitle();
    ui_push(&app_menu, &ed_menu);
}

// ---- elenco "modifica una frase" ----
static menu_item_t pick_items[MSG_MAX];
static menu_t pick_menu = {"Modifica una frase", pick_items, 0, 0, NULL};
static void a_pick(void)
{
    saved_load();
    for (int i = 0; i < n_saved; i++)
        pick_items[i] = (menu_item_t){.icon = LV_SYMBOL_LIST, .label = saved[i].name,
                                      .hint = saved[i].text, .on_pick = ed_open, .arg = (void *)(intptr_t)i};
    pick_menu.count = n_saved;
    pick_menu.sel = 0;
    if (!n_saved) { ui_toast("Nessuna frase salvata"); return; }
    ui_push(&app_menu, &pick_menu);
}

// ---- elenco principale delle frasi salvate ----
static void sv_show(void *arg)
{
    int i = (int)(intptr_t)arg;
    if (i < 0 || i >= n_saved) return;
    snprintf(cur_text, sizeof(cur_text), "%s", saved[i].text);
    cur_save();
    ui_push(&app_show, NULL);
}
static void a_save_current(void)
{
    if (!cur_text[0]) { ui_toast("Scrivi prima una frase"); return; }
    ui_push(&app_name, NULL);
}
static menu_item_t sv_items[MSG_MAX + 2];
static menu_t sv_menu = {"Frasi salvate", sv_items, 0, 0, NULL};
static void a_saved(void)
{
    saved_load();
    int k = 0;
    for (int i = 0; i < n_saved; i++)
        sv_items[k++] = (menu_item_t){.icon = LV_SYMBOL_LIST, .label = saved[i].name, .hint = saved[i].text,
                                      .on_pick = sv_show, .arg = (void *)(intptr_t)i};
    sv_items[k++] = (menu_item_t){.icon = LV_SYMBOL_SAVE, .label = "Salva la frase attuale",
                                  .hint = "Le dai un nome", .on_select = a_save_current};
    sv_items[k++] = (menu_item_t){.icon = LV_SYMBOL_EDIT, .label = "Modifica una frase", .on_select = a_pick};
    sv_menu.count = k;
    sv_menu.sel = 0;
    ui_push(&app_menu, &sv_menu);
}

/* ---------------- impostazioni (voci del menu principale) ---------------- */

static void v_color(char *b, int n) { snprintf(b, n, "%s", cfg.rainbow ? "(arcobaleno)" : PAL[cfg.color].name); }
static void j_color(int d) { cfg.color = (cfg.color + d + N_PAL) % N_PAL; cfg.rainbow = 0; cfg_save(); }
static void v_rain(char *b, int n) { snprintf(b, n, "%s", cfg.rainbow ? "Sì" : "No"); }
static void a_rain(void) { cfg.rainbow = !cfg.rainbow; cfg_save(); }
static void v_speed(char *b, int n) { snprintf(b, n, "%s", SPEED_NAME[cfg.speed]); }
static void j_speed(int d) { cfg.speed = (cfg.speed + d + N_SPEED) % N_SPEED; cfg_save(); }
static void v_bright(char *b, int n) { snprintf(b, n, "%d%%", BRIGHT_PCT[cfg.bright]); }
static void j_bright(int d) { cfg.bright = (cfg.bright + d + N_BRIGHT) % N_BRIGHT; cfg_save(); }
static void v_dir(char *b, int n) { snprintf(b, n, "%s", DIR_NAME[cfg.dir]); }
static void j_dir(int d) { cfg.dir = (cfg.dir + d + N_DIR) % N_DIR; cfg_save(); }
static void v_dots(char *b, int n) { snprintf(b, n, "%s", cfg.round_dots ? "LED tondi" : "Quadrati"); }
static void a_dots(void) { cfg.round_dots = !cfg.round_dots; cfg_save(); }
static void v_grid(char *b, int n) { snprintf(b, n, "%s", cfg.grid ? "Sì" : "No"); }
static void a_grid(void) { cfg.grid = !cfg.grid; cfg_save(); }

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_EDIT, .label = "Scrivi e mostra", .hint = "Scrivi una frase e parte subito", .app = &app_kb},
    {.icon = LV_SYMBOL_PLAY, .label = "Mostra l'ultima", .on_select = show_current},
    {.icon = LV_SYMBOL_LIST, .label = "Frasi salvate", .on_select = a_saved},
    {.icon = ICON_PALETTE, .label = "Colore", .value = v_color, .on_adjust = j_color},
    {.icon = ICON_SUN, .label = "Arcobaleno", .value = v_rain, .on_select = a_rain},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Velocità", .value = v_speed, .on_adjust = j_speed},
    {.icon = ICON_BULB, .label = "Luminosità", .value = v_bright, .on_adjust = j_bright},
    {.icon = LV_SYMBOL_RIGHT, .label = "Direzione", .value = v_dir, .on_adjust = j_dir},
    {.icon = ICON_CHIP, .label = "Stile LED", .value = v_dots, .on_select = a_dots},
    {.icon = ICON_SLIDERS, .label = "Griglia spenta", .value = v_grid, .on_select = a_grid},
};
menu_t ledsign_menu = {"Insegna LED", items, sizeof(items) / sizeof(items[0]), 0, NULL};

// all'avvio: carica impostazioni e frasi (come tuner_menu_init / chess_menu_init)
void ledsign_init(void) { cfg_load(); saved_load(); }
