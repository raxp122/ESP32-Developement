// menu.c — schermata menu generica usata da home e impostazioni
#include "ui.h"
#include "settings.h"
#include "apps/apps.h"
#include <stdio.h>
#include <string.h>

static menu_t *m;
static bool active;
static list_view_t lv;
static bool editing;
static int confirm_idx = -1;
static uint32_t confirm_tick;

static const char *item_label(int i)
{
    return (i >= 0 && i < m->count) ? m->items[i].label : NULL;
}

static void render(int dir)
{
    const menu_item_t *it = &m->items[m->sel];
    char val[96] = "";
    if (confirm_idx == m->sel) {
        snprintf(val, sizeof(val), "Swipe a destra di nuovo per confermare");
    } else if (it->value) {
        it->value(val, sizeof(val));
    } else if (it->hint) {
        snprintf(val, sizeof(val), "%s", it->hint);
    }
    char sub[112];
    if (editing) snprintf(sub, sizeof(sub), LV_SYMBOL_UP " %s " LV_SYMBOL_DOWN, val);
    else snprintf(sub, sizeof(sub), "%s", val);

    list_view_set(&lv, it->icon ? it->icon : LV_SYMBOL_RIGHT, item_label(m->sel - 1), it->label, sub,
                  item_label(m->sel + 1), m->sel, m->count, dir);

    // in modifica il valore diventa una "pillola" piena. Ogni stile si tocca solo se cambia:
    // questa funzione gira ogni secondo e ogni modifica ridisegna lo schermo.
    lv_opa_t opa = editing ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (lv_obj_get_style_bg_opa(lv.sub, 0) != opa) lv_obj_set_style_bg_opa(lv.sub, opa, 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(lv.sub, 0), ui_accent())) lv_obj_set_style_bg_color(lv.sub, ui_accent(), 0);
    ui_set_text_color(lv.sub, editing ? C_BG : (confirm_idx == m->sel ? C_WARN : ui_accent()));
    int32_t pad = editing ? 8 : 0;
    if (lv_obj_get_style_pad_left(lv.sub, 0) != pad) lv_obj_set_style_pad_hor(lv.sub, pad, 0);
    int32_t w = editing ? LV_SIZE_CONTENT : SCR_W - 100;
    if (lv_obj_get_style_width(lv.sub, 0) != w) lv_obj_set_width(lv.sub, w);
}

static void enter(lv_obj_t *root, void *arg)
{
    m = arg;
    active = true;
    editing = false;
    confirm_idx = -1;
    if (m->sel >= m->count) m->sel = 0;
    list_view_create(&lv, root);
    render(0);
}

static void move(int d)
{
    int n = m->sel + d;
    if (n < 0 || n >= m->count) return;
    m->sel = n;
    confirm_idx = -1;
    render(d);
}

static bool nav(nav_t ev)
{
    const menu_item_t *it = &m->items[m->sel];
    if (editing) {
        switch (ev) {
        case NAV_NEXT: it->on_adjust(+1); render(0); return true;
        case NAV_PREV: it->on_adjust(-1); render(0); return true;
        case NAV_SELECT:
        case NAV_BACK: editing = false; settings_defer(false); render(0); return true;
        default: return false;
        }
    }
    // nel menu principale BOOT riporta in cima (a "Cerca") senza dover scorrere
    if (ev == NAV_BTN && m == &home_menu && m->sel != 0) {
        m->sel = 0;
        confirm_idx = -1;
        render(-1);
        return true;
    }
    switch (ev) {
    case NAV_NEXT: move(+1); return true;
    case NAV_PREV: move(-1); return true;
    case NAV_SELECT:
        if (it->confirm && !(confirm_idx == m->sel && lv_tick_elaps(confirm_tick) < 4000)) {
            confirm_idx = m->sel;
            confirm_tick = lv_tick_get();
            render(0);
            return true;
        }
        confirm_idx = -1;
        if (it->on_adjust) { editing = true; settings_defer(true); render(0); return true; }
        if (it->app) { ui_push(it->app, it->arg); return true; }
        if (it->on_pick) it->on_pick(it->arg);
        else if (it->on_select) it->on_select();
        // l'azione potrebbe aver cambiato schermata: aggiorna solo se siamo ancora qui
        if (active) render(0);
        return true;
    default:
        return false;
    }
}

static void tick(void)
{
    // aggiorna i valori dinamici (stato Wi-Fi, ora, batteria…)
    static int n;
    if (++n % 5) return;
    if (confirm_idx >= 0 && lv_tick_elaps(confirm_tick) >= 4000) confirm_idx = -1;
    render(0);
}

static void leave(void)
{
    active = false;
    if (editing) { editing = false; settings_defer(false); }   // salva quello che si stava regolando
}

static const char *title(void *arg) { return ((menu_t *)arg)->title; }

const app_t app_menu = {
    .name = "Menu",
    .enter = enter,
    .leave = leave,
    .nav = nav,
    .tick = tick,
    .title = title,
};
