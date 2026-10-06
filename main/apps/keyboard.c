// keyboard.c — tastiera touch a schermo intero, riutilizzabile (password Wi-Fi, nomi…).
// Stesso schema della tastiera di Cerca: in alto la riga del testo, sempre libera, sotto
// tre righe di tasti da 42 px. Le lettere sono tutte su una scheda; numeri, simboli e
// lettere accentate stanno su schede a parte. Con le schede si scrive qualunque carattere
// ASCII stampabile, quindi qualsiasi password Wi-Fi.
//   Maiuscolo: un tocco = solo la lettera successiva, due tocchi rapidi = fisso.
//   Su/giù cambia scheda, destra conferma, sinistra cancella (a testo vuoto esce).
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_H    42
#define KB_Y     (SCR_H - 3 * KEY_H)    // 46: sopra resta la riga del testo
#define KEY_W    64
#define ROW3_W   56                     // tasti lettera della terza riga (lascia spazio ai comandi)
#define MAX_SHOW 46                     // caratteri visibili nella riga del testo
// Il controller del touch a volte "perde" il dito per qualche lettura anche se è fermo:
// se bastassero due letture vuote, un solo tocco diventerebbe due lettere. Il dito conta
// come sollevato solo dopo RELEASE_MS senza letture; un tocco più corto di PRESS_MIN_MS
// (un rimbalzo del pannello) non scrive nulla.
#define RELEASE_MS   70
#define PRESS_MIN_MS 25

enum { P_LOWER, P_UPPER, P_NUM, P_SYM, P_ACC, P_COUNT };
// ogni scheda: 10 + 9 + 7 caratteri (seconda riga + cancella, terza + comandi)
static const char *const rows[P_COUNT][3] = {
    [P_LOWER] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    [P_UPPER] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    [P_NUM]   = {"1234567890", "-/:;()&@\"", ".,?!'#%"},
    [P_SYM]   = {"[]{}^*+=_\\", "|~<>$£°§`", "¿¡«»±×¬"},
    [P_ACC]   = {"àèéìòùáíóú",
                 "âêîôûäëïö",
                 "üçñßÀÈÉ"},
};
static const char *const page_next_label[P_COUNT] = {"123", "123", "#+=", "àé", "abc"};

enum { K_CHAR, K_SHIFT, K_LETTERS, K_PAGE, K_BS, K_SPACE, K_ENTER };

typedef struct { char str[8]; int code; int x, y, w; lv_obj_t *obj; } kb_key_t;

static kb_key_t keys[32];
static int n_keys, page, key_down = -1;
static bool caps;              // maiuscolo fisso
static uint32_t shift_tick;    // per il doppio tocco
static char text[72];
static bool mask_mode;
static int maxlen;
static kb_done_t cb;
static void *cb_arg;
static lv_obj_t *root_obj, *l_title, *l_text, *l_count, *kb_cont;
static lv_timer_t *tmr;

static int utf8_len(const char *c)
{
    int l = 1;
    while ((c[l] & 0xC0) == 0x80) l++;
    return l;
}

static void add(const char *str, int code, int x, int y, int w)
{
    kb_key_t *k = &keys[n_keys++];
    snprintf(k->str, sizeof(k->str), "%s", str);
    k->code = code; k->x = x; k->y = y; k->w = w; k->obj = NULL;
}

static void add_row(const char *row, int x, int y, int w)
{
    for (const char *c = row; *c;) {
        int l = utf8_len(c);
        char s[5] = {0};
        memcpy(s, c, l);
        add(s, K_CHAR, x, y, w);
        x += w;
        c += l;
    }
}

static void build_keys(void)
{
    n_keys = 0;
    bool letters = page == P_LOWER || page == P_UPPER;
    add_row(rows[page][0], 0, KB_Y, KEY_W);
    add_row(rows[page][1], 0, KB_Y + KEY_H, KEY_W);
    add(LV_SYMBOL_BACKSPACE, K_BS, 9 * KEY_W, KB_Y + KEY_H, KEY_W);
    int y = KB_Y + 2 * KEY_H;
    add(letters ? LV_SYMBOL_UP : "abc", letters ? K_SHIFT : K_LETTERS, 0, y, KEY_W);
    add_row(rows[page][2], KEY_W, y, ROW3_W);
    int x = KEY_W + 7 * ROW3_W;
    add(page_next_label[page], K_PAGE, x, y, KEY_W);
    add("spazio", K_SPACE, x + KEY_W, y, KEY_W);
    add(LV_SYMBOL_NEW_LINE, K_ENTER, x + 2 * KEY_W, y, SCR_W - x - 2 * KEY_W);
}

static void style(int i, bool down)
{
    kb_key_t *k = &keys[i];
    bool special = k->code != K_CHAR;
    bool shift_on = k->code == K_SHIFT && page == P_UPPER;
    lv_color_t bg = down ? ui_accent() : shift_on ? lv_color_hex(0x3A3F46) : lv_color_hex(special ? 0x23272C : 0x16191D);
    lv_color_t fg = down ? C_BG : (k->code == K_ENTER || (k->code == K_SHIFT && caps)) ? ui_accent() : C_TEXT;
    lv_obj_set_style_bg_color(k->obj, bg, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(k->obj, 0), fg, 0);
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
        int c = keys[i].code;
        lv_obj_set_style_text_font(l, c == K_SPACE || c == K_PAGE || c == K_LETTERS ? &font_s : &font_m, 0);
        lv_label_set_text(l, keys[i].str);
        lv_obj_center(l);
        keys[i].obj = o;
        style(i, false);
    }
}

static void rebuild(void)
{
    if (kb_cont) lv_obj_delete(kb_cont);   // contenitore dei soli tasti
    kb_cont = lv_obj_create(root_obj);
    lv_obj_remove_style_all(kb_cont);
    lv_obj_set_size(kb_cont, SCR_W, SCR_H);
    lv_obj_set_pos(kb_cont, 0, 0);
    lv_obj_clear_flag(kb_cont, LV_OBJ_FLAG_SCROLLABLE);
    build_keys();
    render_keys();
}

static void set_page(int p)
{
    page = p;
    if (p != P_UPPER) caps = false;
    rebuild();
}

static void show_text(void)
{
    char buf[4 * 72 + 8];
    int o = 0, glyphs = 0;
    // costruisce il testo da mostrare (con i pallini se è una password: l'ultimo
    // carattere resta visibile per controllare cosa si è appena digitato)
    for (const char *c = text; *c;) {
        int l = utf8_len(c);
        bool last = !c[l];
        if (mask_mode && !last) { memcpy(buf + o, "•", 3); o += 3; }
        else { memcpy(buf + o, c, l); o += l; }
        glyphs++;
        c += l;
    }
    buf[o] = 0;
    // se non ci sta, si mostra la fine (dove si sta scrivendo)
    const char *shown = buf;
    while (glyphs > MAX_SHOW) { shown += utf8_len(shown); glyphs--; }
    char line[sizeof(buf) + 8];
    snprintf(line, sizeof(line), "%s%s_", shown != buf ? "…" : "", shown);
    ui_set_text(l_text, line);
    ui_set_text_color(l_text, text[0] ? C_TEXT : C_DIM);
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%d/%d", (int)strlen(text), maxlen);
    ui_set_text(l_count, cnt);
}

static void backspace(void)
{
    int len = strlen(text);
    if (!len) return;
    int l = 1;
    while (len - l > 0 && (text[len - l] & 0xC0) == 0x80) l++;
    text[len - l] = 0;
}

static void finish(const char *t)
{
    if (!cb) return;
    kb_done_t c = cb;
    void *a = cb_arg;
    cb = NULL;
    c(t, a);
}

static void press(int i)
{
    int len = strlen(text);
    switch (keys[i].code) {
    case K_CHAR: {
        int add_len = strlen(keys[i].str);
        if (len + add_len < (int)sizeof(text) && len + add_len <= maxlen) strcat(text, keys[i].str);
        if (page == P_UPPER && !caps) { set_page(P_LOWER); }   // maiuscola singola
        break;
    }
    case K_SPACE:
        if (len < maxlen && len + 1 < (int)sizeof(text)) { text[len] = ' '; text[len + 1] = 0; }
        break;
    case K_BS:
        backspace();
        break;
    case K_SHIFT: {
        uint32_t now = lv_tick_get();
        if (caps) { caps = false; set_page(P_LOWER); }
        else if (page == P_UPPER && now - shift_tick < 450) { caps = true; rebuild(); }   // doppio tocco
        else set_page(page == P_UPPER ? P_LOWER : P_UPPER);
        shift_tick = now;
        return;
    }
    case K_LETTERS: set_page(P_LOWER); return;
    case K_PAGE:    set_page(page == P_LOWER || page == P_UPPER ? P_NUM : page == P_ACC ? P_LOWER : page + 1); return;
    case K_ENTER:   finish(text); return;
    }
    show_text();
}

static int hit(int x, int y)
{
    for (int i = 0; i < n_keys; i++)
        if (x >= keys[i].x && x < keys[i].x + keys[i].w && y >= keys[i].y && y < keys[i].y + KEY_H) return i;
    return -1;
}

static bool t_isdown, t_moved;
static int t_sx, t_sy;
static uint32_t t_down, t_seen;

static void touch_cb(lv_timer_t *t)
{
    int x, y;
    uint32_t now = lv_tick_get();
    if (input_touch(&x, &y)) {
        t_seen = now;
        if (!t_isdown) {
            t_isdown = true; t_moved = false; t_sx = x; t_sy = y; t_down = now;
            key_down = hit(x, y);
            if (key_down >= 0) style(key_down, true);
            return;
        }
        if (abs(x - t_sx) > 26 || abs(y - t_sy) > 26) {
            t_moved = true;   // è uno swipe: annulla il tasto
            if (key_down >= 0) { style(key_down, false); key_down = -1; }
        }
        return;
    }
    if (!t_isdown || now - t_seen < RELEASE_MS) return;
    t_isdown = false;
    if (key_down >= 0) {
        int k = key_down;
        key_down = -1;
        if (keys[k].obj) style(k, false);
        if (!t_moved && t_seen - t_down >= PRESS_MIN_MS) press(k);   // può ricostruire i tasti o chiudere la schermata
    }
}

static lv_obj_t *mk_label(const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(root_obj);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

void keyboard_open(lv_obj_t *root, const char *title, const char *initial, bool mask,
                   int ml, kb_done_t on_done, void *arg)
{
    root_obj = root;
    kb_cont = NULL;
    page = P_LOWER;
    caps = false;
    mask_mode = mask;
    maxlen = ml > (int)sizeof(text) - 4 ? (int)sizeof(text) - 4 : ml;
    cb = on_done;
    cb_arg = arg;
    snprintf(text, sizeof(text), "%s", initial ? initial : "");

    // riga del testo: titolo piccolo, testo, contatore. I tasti iniziano sotto (y = 46).
    l_title = mk_label(&font_s, C_DIM);
    lv_label_set_text(l_title, title ? title : "");
    lv_obj_set_pos(l_title, 12, 2);
    l_text = mk_label(&font_m, C_TEXT);
    lv_obj_set_pos(l_text, 12, 19);
    lv_obj_set_width(l_text, SCR_W - 90);
    lv_label_set_long_mode(l_text, LV_LABEL_LONG_CLIP);
    l_count = mk_label(&font_s, C_DIM);
    lv_obj_align(l_count, LV_ALIGN_TOP_RIGHT, -10, 24);
    show_text();

    rebuild();
    key_down = -1;
    t_isdown = false;
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
    // su/giù cambiano scheda, comodo da una mano; destra conferma; sinistra cancella o esce
    switch (ev) {
    case NAV_NEXT: set_page(page <= P_UPPER ? P_NUM : page == P_ACC ? P_LOWER : page + 1); return true;
    case NAV_PREV: set_page(page <= P_UPPER ? P_ACC : page == P_NUM ? P_LOWER : page - 1); return true;
    case NAV_QUICK: return true;   // il dito tenuto su un tasto non deve aprire l'azione rapida
    case NAV_SELECT: finish(text); return true;
    case NAV_BACK:
        if (text[0]) { backspace(); show_text(); return true; }
        finish(NULL);
        return false;
    default:
        return false;
    }
}
