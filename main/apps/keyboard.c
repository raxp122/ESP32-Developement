// keyboard.c — tastiera touch a schermo intero, riutilizzabile
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_H   38
#define KB_Y    (SCR_H - 3 * KEY_H - KEY_H - 2)   // 3 righe + riga inferiore, sotto la barra testo

// pagine: 0 minuscole, 1 maiuscole, 2 numeri/simboli base, 3 simboli estesi
static const char *rows[4][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "-/:;()&@\"", ".,?!'"},
    {"[]{}#%^*+=", "_\\|~<>$", "\u20ac\u00a3\u00b0"},
};

enum { K_CHAR, K_SHIFT, K_PAGE, K_BS, K_SPACE, K_ENTER };

typedef struct { char str[8]; int code; int x, y, w; lv_obj_t *obj; } kb_key_t;

static kb_key_t keys[48];
static int n_keys, page, key_down = -1;
static char text[72];
static bool mask_mode;
static int maxlen;
static kb_done_t cb;
static void *cb_arg;
static lv_obj_t *l_title, *l_text, *kb_cont;
static lv_timer_t *tmr;

static void add(const char *str, int code, int x, int y, int w)
{
    kb_key_t *k = &keys[n_keys++];
    snprintf(k->str, sizeof(k->str), "%s", str);
    k->code = code; k->x = x; k->y = y; k->w = w; k->obj = NULL;
}

static void build_keys(void)
{
    n_keys = 0;
    // 3 righe di caratteri
    for (int r = 0; r < 3; r++) {
        const char *row = rows[page][r];
        int len = 0;
        for (const char *c = row; *c; ) { int l = 1; while ((c[l] & 0xC0) == 0x80) l++; len++; c += l; }
        int w = 60, x0 = (SCR_W - len * w) / 2;
        if (r == 2) x0 = (SCR_W - (len + 2) * w) / 2;   // spazio per shift e backspace
        int x = x0, y = KB_Y + r * KEY_H;
        if (r == 2) { add(LV_SYMBOL_UP, K_SHIFT, x, y, w); x += w; }
        for (const char *c = row; *c; ) {
            int l = 1; while ((c[l] & 0xC0) == 0x80) l++;
            char s[5] = {0}; memcpy(s, c, l);
            add(s, K_CHAR, x, y, w); x += w; c += l;
        }
        if (r == 2) add(LV_SYMBOL_BACKSPACE, K_BS, x, y, w);
    }
    // riga inferiore: cambio pagina, spazio, invio
    int y = KB_Y + 3 * KEY_H;
    add(page >= 2 ? "ABC" : "?123", K_PAGE, 2, y, 110);
    add("spazio", K_SPACE, 118, y, SCR_W - 118 - 112);
    add(LV_SYMBOL_NEW_LINE, K_ENTER, SCR_W - 108, y, 106);
}

static void style(int i, bool down)
{
    kb_key_t *k = &keys[i];
    bool special = k->code != K_CHAR;
    lv_obj_set_style_bg_color(k->obj, down ? ui_accent() : lv_color_hex(special ? 0x23272C : 0x16191D), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(k->obj, 0),
                                down ? C_BG : (k->code == K_ENTER ? ui_accent() : C_TEXT), 0);
}

static void render_keys(void)
{
    for (int i = 0; i < n_keys; i++) {
        lv_obj_t *o = lv_obj_create(kb_cont);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, keys[i].x + 2, keys[i].y + 2);
        lv_obj_set_size(o, keys[i].w - 4, KEY_H - 4);
        lv_obj_set_style_radius(o, 6, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_t *l = lv_label_create(o);
        lv_obj_set_style_text_font(l, keys[i].code == K_SPACE || keys[i].code == K_PAGE ? &font_s : &font_m, 0);
        lv_label_set_text(l, keys[i].str);
        lv_obj_center(l);
        keys[i].obj = o;
        style(i, false);
    }
}

static void rebuild(void)
{
    if (kb_cont) lv_obj_delete(kb_cont);     // contenitore dei soli tasti
    kb_cont = lv_obj_create(lv_obj_get_parent(l_title));
    lv_obj_remove_style_all(kb_cont);
    lv_obj_set_size(kb_cont, SCR_W, SCR_H);
    lv_obj_set_pos(kb_cont, 0, 0);
    lv_obj_clear_flag(kb_cont, LV_OBJ_FLAG_SCROLLABLE);
    build_keys();
    render_keys();
}

static void show_text(void)
{
    if (!text[0]) {
        lv_label_set_text(l_text, "…");
        lv_obj_set_style_text_color(l_text, C_DIM, 0);
        return;
    }
    lv_obj_set_style_text_color(l_text, C_TEXT, 0);
    if (mask_mode) {
        char buf[72];
        int len = strlen(text), i;
        for (i = 0; i < len - 1 && i < 69; i++) buf[i] = '*';
        buf[i] = len ? text[len - 1] : 0;   // mostra l'ultimo carattere digitato
        buf[i + (len ? 1 : 0)] = 0;
        lv_label_set_text(l_text, buf);
    } else {
        lv_label_set_text(l_text, text);
    }
}

static void press(int i)
{
    int len = strlen(text);
    switch (keys[i].code) {
    case K_CHAR:
        if (len + 3 < (int)sizeof(text) && len < maxlen) { strcat(text, keys[i].str); }
        break;
    case K_SPACE:
        if (len && len < maxlen && text[len - 1] != ' ') { text[len] = ' '; text[len + 1] = 0; }
        break;
    case K_BS:
        if (len) { int l = 1; while (len - l > 0 && (text[len - l] & 0xC0) == 0x80) l++; text[len - l] = 0; }
        break;
    case K_SHIFT: page = page == 0 ? 1 : 0; rebuild(); return;
    case K_PAGE:  page = page < 2 ? 2 : page == 2 ? 3 : 0; rebuild(); return;
    case K_ENTER:
        if (cb) { kb_done_t c = cb; void *a = cb_arg; cb = NULL; c(text, a); }
        return;
    }
    show_text();
}

static int hit(int x, int y)
{
    for (int i = 0; i < n_keys; i++)
        if (x >= keys[i].x && x < keys[i].x + keys[i].w && y >= keys[i].y && y < keys[i].y + KEY_H) return i;
    return -1;
}

static void touch_cb(lv_timer_t *t)
{
    static bool down, moved;
    static int sx, sy, rel;
    int x, y;
    if (input_touch(&x, &y)) {
        rel = 0;
        if (!down) {
            down = true; moved = false; sx = x; sy = y;
            key_down = hit(x, y);
            if (key_down >= 0) style(key_down, true);
            return;
        }
        if (abs(x - sx) > 26 || abs(y - sy) > 26) {
            moved = true;
            if (key_down >= 0) { style(key_down, false); key_down = -1; }
        }
        return;
    }
    if (!down || ++rel < 2) return;
    down = false;
    if (key_down >= 0) {
        int k = key_down;
        key_down = -1;
        if (keys[k].obj) style(k, false);
        if (!moved) press(k);
    }
}

void keyboard_open(lv_obj_t *root, const char *title, const char *initial, bool mask,
                   int ml, kb_done_t on_done, void *arg)
{
    page = 0;
    mask_mode = mask;
    maxlen = ml > (int)sizeof(text) - 4 ? (int)sizeof(text) - 4 : ml;
    cb = on_done;
    cb_arg = arg;
    snprintf(text, sizeof(text), "%s", initial ? initial : "");

    l_title = lv_label_create(root);
    lv_obj_set_style_text_font(l_title, &font_s, 0);
    lv_obj_set_style_text_color(l_title, C_DIM, 0);
    lv_label_set_text(l_title, title ? title : "");
    lv_obj_set_pos(l_title, 14, 8);

    // barra del testo con sfondo, così non viene coperta dai tasti
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, SCR_W - 20, 30);
    lv_obj_set_pos(bar, 10, 24);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x16191D), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, C_FAINT, 0);
    l_text = lv_label_create(bar);
    lv_obj_set_style_text_font(l_text, &font_m, 0);
    lv_obj_set_width(l_text, SCR_W - 44);
    lv_label_set_long_mode(l_text, LV_LABEL_LONG_DOT);
    lv_obj_align(l_text, LV_ALIGN_LEFT_MID, 8, 0);
    show_text();

    rebuild();
    key_down = -1;
    tmr = lv_timer_create(touch_cb, 15, NULL);
}

void keyboard_close(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    kb_cont = NULL;
    cb = NULL;
}

bool keyboard_nav(nav_t ev)
{
    // su/giù cambiano pagina, comodo da una mano; sinistra cancella o esce
    switch (ev) {
    case NAV_NEXT: page = (page + 1) % 4; rebuild(); return true;
    case NAV_PREV: page = (page + 3) % 4; rebuild(); return true;
    case NAV_QUICK: return true;   // evita l'azione rapida mentre si scrive
    case NAV_SELECT:
        if (cb) { kb_done_t c = cb; void *a = cb_arg; cb = NULL; c(text, a); }
        return true;
    case NAV_BACK:
        if (text[0]) { int len = strlen(text); int l = 1; while (len - l > 0 && (text[len - l] & 0xC0) == 0x80) l++; text[len - l] = 0; show_text(); return true; }
        if (cb) { kb_done_t c = cb; void *a = cb_arg; cb = NULL; c(NULL, a); }
        return false;
    default:
        return false;
    }
}
