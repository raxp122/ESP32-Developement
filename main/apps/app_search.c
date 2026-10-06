// app_search.c — Cerca: tastiera touch, risultati da app e voci di tutti i menu.
// Tocca i tasti per scrivere · su/giù scorre i risultati · swipe a destra (o ⏎) apre · sinistra esce
#include "apps.h"
#include "textnorm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

/* ---------------- indice ---------------- */

typedef struct {
    const menu_item_t *item;
    menu_t *menu;        // menu che contiene la voce
    int idx;             // posizione nel menu
    char norm[48];       // etichetta normalizzata
    char where[40];      // es. "Impostazioni » Schermo"
} entry_t;

#define MAX_ENTRIES 256   // ~150 voci oggi: con 120 metà delle app non si trovavano
static entry_t *entries;
static int n_entries;

static void index_menu(menu_t *m, const char *where, int depth)
{
    for (int i = 0; i < m->count && n_entries < MAX_ENTRIES; i++) {
        const menu_item_t *it = &m->items[i];
        if (m == &home_menu && it->app == &app_search) continue;
        if (n_entries == MAX_ENTRIES - 1) ESP_LOGW("search", "indice pieno: aumenta MAX_ENTRIES");
        entry_t *e = &entries[n_entries++];
        e->item = it;
        e->menu = m;
        e->idx = i;
        text_norm(it->label, e->norm, sizeof(e->norm));
        snprintf(e->where, sizeof(e->where), "%s", where);
        if (it->app == &app_menu && it->arg && depth < 3) {
            char sub[40];
            snprintf(sub, sizeof(sub), "%s", it->label);
            index_menu((menu_t *)it->arg, sub, depth + 1);
        }
    }
}

/* ---------------- ricerca ---------------- */

#define MAX_RES 12
static char query[24];
static int res[MAX_RES], n_res, sel;

static void do_search(void)
{
    n_res = 0;
    sel = 0;
    if (!query[0]) return;
    // prima le parole che iniziano con la ricerca, poi le corrispondenze interne
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n_entries && n_res < MAX_RES; i++) {
            const char *p = strstr(entries[i].norm, query);
            if (!p) continue;
            bool word_start = p == entries[i].norm || p[-1] == ' ' || p[-1] == '-';
            if ((pass == 0) != word_start) continue;
            res[n_res++] = i;
        }
}

static void open_result(void)
{
    if (!n_res) return;
    entry_t *e = &entries[res[sel]];
    const menu_item_t *it = e->item;
    ui_pop();   // chiude la ricerca
    if (e->menu == &home_menu) {
        if (it->app) ui_push(it->app, it->arg);
        else if (it->on_select) it->on_select();
    } else {
        // apre il menu che contiene la voce, già posizionato su di essa
        e->menu->sel = e->idx;
        ui_push(&app_menu, e->menu);
    }
}

/* ---------------- tastiera ---------------- */

#define KEY_H   42
#define KB_Y    (SCR_H - 3 * KEY_H)
enum { K_BS = 1, K_SPACE, K_ENTER };

typedef struct { const char *label; char ch; int code; int x, y, w; lv_obj_t *obj; } kb_key_t;
static kb_key_t keys[30];
static int n_keys, key_down = -1;

static void add_row(const char *chars, int y, int x0, int w)
{
    for (const char *c = chars; *c; c++) {
        kb_key_t *k = &keys[n_keys++];
        static char lbl[30][2];
        lbl[n_keys - 1][0] = (char)(*c - 32);   // etichetta maiuscola
        lbl[n_keys - 1][1] = 0;
        *k = (kb_key_t){lbl[n_keys - 1], *c, 0, x0, y, w, NULL};
        x0 += w;
    }
}

static void add_key(const char *label, int code, int x, int y, int w)
{
    keys[n_keys++] = (kb_key_t){label, 0, code, x, y, w, NULL};
}

static lv_obj_t *l_query, *l_res, *l_where, *l_count;

static void style_key(int i, bool down)
{
    lv_obj_set_style_bg_color(keys[i].obj, down ? ui_accent() : lv_color_hex(0x16191D), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(keys[i].obj, 0), down ? C_BG : C_TEXT, 0);
}

static void render(void)
{
    char q[40];
    snprintf(q, sizeof(q), "%s_", query);
    lv_label_set_text(l_query, query[0] ? q : "Scrivi…");
    lv_obj_set_style_text_color(l_query, query[0] ? C_TEXT : C_DIM, 0);
    if (!query[0]) {
        lv_label_set_text(l_res, "");
        lv_label_set_text(l_where, "");
        lv_label_set_text(l_count, "");
    } else if (!n_res) {
        lv_label_set_text(l_res, "Nessun risultato");
        lv_obj_set_style_text_color(l_res, C_DIM, 0);
        lv_label_set_text(l_where, "");
        lv_label_set_text(l_count, "");
    } else {
        entry_t *e = &entries[res[sel]];
        lv_label_set_text_fmt(l_res, "%s  %s", e->item->icon ? e->item->icon : "", e->item->label);
        lv_obj_set_style_text_color(l_res, ui_accent(), 0);
        lv_label_set_text(l_where, e->menu == &home_menu ? "App" : e->where);
        lv_label_set_text_fmt(l_count, "%d/%d", sel + 1, n_res);
    }
}

static void press(int i)
{
    kb_key_t *k = &keys[i];
    size_t len = strlen(query);
    if (k->ch && len + 1 < sizeof(query)) { query[len] = k->ch; query[len + 1] = 0; }
    else if (k->code == K_SPACE && len && query[len - 1] != ' ' && len + 1 < sizeof(query)) { query[len] = ' '; query[len + 1] = 0; }
    else if (k->code == K_BS && len) query[len - 1] = 0;
    else if (k->code == K_ENTER) { open_result(); return; }
    do_search();
    render();
}

static int hit(int x, int y)
{
    for (int i = 0; i < n_keys; i++)
        if (x >= keys[i].x && x < keys[i].x + keys[i].w && y >= keys[i].y && y < keys[i].y + KEY_H) return i;
    return -1;
}

static lv_timer_t *touch_timer;

// Stesso schema di keyboard.c: il controller del touch perde il dito per qualche lettura,
// quindi il dito conta come sollevato solo dopo RELEASE_MS senza letture (altrimenti un
// tocco scriveva due lettere) e un tocco più corto di PRESS_MIN_MS è un rimbalzo.
#define RELEASE_MS   70
#define PRESS_MIN_MS 25
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
            if (key_down >= 0) style_key(key_down, true);
            return;
        }
        if (abs(x - t_sx) > 26 || abs(y - t_sy) > 26) {
            t_moved = true;    // è uno swipe: annulla il tasto
            if (key_down >= 0) { style_key(key_down, false); key_down = -1; }
        }
        return;
    }
    if (!t_isdown || now - t_seen < RELEASE_MS) return;
    t_isdown = false;
    if (key_down >= 0) {
        int k = key_down;
        style_key(k, false);
        key_down = -1;
        if (!t_moved && t_seen - t_down >= PRESS_MIN_MS) press(k);   // può chiudere la schermata: niente dopo
    }
}

/* ---------------- ciclo di vita ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    if (!entries) entries = heap_caps_malloc(sizeof(entry_t) * MAX_ENTRIES, MALLOC_CAP_SPIRAM);
    n_entries = 0;
    index_menu(&home_menu, "App", 0);

    // riga superiore: ricerca e risultato
    lv_obj_t *ic = lv_label_create(root);
    lv_obj_set_style_text_font(ic, &font_m, 0);
    lv_obj_set_style_text_color(ic, C_DIM, 0);
    lv_label_set_text(ic, ICON_SEARCH);
    lv_obj_set_pos(ic, 12, 10);

    l_query = lv_label_create(root);
    lv_obj_set_style_text_font(l_query, &font_m, 0);
    lv_obj_set_pos(l_query, 40, 8);
    lv_obj_set_width(l_query, 200);
    lv_label_set_long_mode(l_query, LV_LABEL_LONG_CLIP);

    l_res = lv_label_create(root);
    lv_obj_set_style_text_font(l_res, &font_m, 0);
    lv_obj_set_pos(l_res, 250, 2);
    lv_obj_set_width(l_res, 320);
    lv_label_set_long_mode(l_res, LV_LABEL_LONG_DOT);

    l_where = lv_label_create(root);
    lv_obj_set_style_text_font(l_where, &font_s, 0);
    lv_obj_set_style_text_color(l_where, C_DIM, 0);
    lv_obj_set_pos(l_where, 250, 24);
    lv_obj_set_width(l_where, 320);
    lv_label_set_long_mode(l_where, LV_LABEL_LONG_DOT);

    l_count = lv_label_create(root);
    lv_obj_set_style_text_font(l_count, &font_s, 0);
    lv_obj_set_style_text_color(l_count, C_DIM, 0);
    lv_obj_align(l_count, LV_ALIGN_TOP_RIGHT, -10, 14);

    // tastiera: 3 righe, tasti da 64 px
    n_keys = 0;
    add_row("qwertyuiop", KB_Y, 0, 64);
    add_row("asdfghjkl", KB_Y + KEY_H, 0, 64);
    add_key(LV_SYMBOL_BACKSPACE, K_BS, 9 * 64, KB_Y + KEY_H, 64);
    add_row("zxcvbnm", KB_Y + 2 * KEY_H, 0, 64);
    add_key("spazio", K_SPACE, 7 * 64, KB_Y + 2 * KEY_H, 128);
    add_key(LV_SYMBOL_NEW_LINE, K_ENTER, 9 * 64, KB_Y + 2 * KEY_H, 64);
    for (int i = 0; i < n_keys; i++) {
        lv_obj_t *o = lv_obj_create(root);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, keys[i].x + 2, keys[i].y + 2);
        lv_obj_set_size(o, keys[i].w - 4, KEY_H - 4);
        lv_obj_set_style_radius(o, 6, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_t *l = lv_label_create(o);
        lv_obj_set_style_text_font(l, keys[i].code == K_SPACE ? &font_s : &font_m, 0);
        lv_label_set_text(l, keys[i].label);
        lv_obj_center(l);
        keys[i].obj = o;
        style_key(i, false);
    }
    if (keys[n_keys - 1].code == K_ENTER) lv_obj_set_style_text_color(lv_obj_get_child(keys[n_keys - 1].obj, 0), ui_accent(), 0);

    do_search();
    render();
    key_down = -1;
    t_isdown = false;
    touch_timer = lv_timer_create(touch_cb, 15, NULL);
}

static void leave(void)
{
    if (touch_timer) { lv_timer_delete(touch_timer); touch_timer = NULL; }
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n_res) { sel++; render(); } return true;
    case NAV_PREV: if (sel > 0) { sel--; render(); } return true;
    case NAV_SELECT: open_result(); return true;
    case NAV_QUICK: return true;   // il dito tenuto su un tasto non deve aprire l'azione rapida
    case NAV_BACK:
        if (query[0]) { query[0] = 0; do_search(); render(); return true; }   // prima svuota
        return false;
    default: return false;
    }
}

const app_t app_search = {
    .name = "Cerca", .icon = ICON_SEARCH,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};
