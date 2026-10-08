// ui.c — gestione schermate, barra di stato, risparmio schermo, azione rapida
#include "ui.h"
#include <stdlib.h>
#include "apps/apps.h"
#include "board.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_system.h"

#define STACK_MAX 10

typedef struct { const app_t *app; void *arg; } frame_t;

static frame_t stack[STACK_MAX];
static int depth;
static lv_obj_t *scr, *bar, *content, *bar_title, *bar_right, *bar_time, *bar_wifi, *bar_ble, *bar_batt, *toast;
static lv_timer_t *toast_timer;
static bool sleeping, swallow_touch;

static const struct { const char *name; uint32_t hex; } accents[] = {
    {"Ambra", 0xFFB020}, {"Ciano", 0x3FD8FF}, {"Fosforo", 0x5CFF8A},
    {"Magenta", 0xFF4FD8}, {"Corallo", 0xFF6B57}, {"Bianco", 0xEDEDED},
};

lv_color_t ui_accent(void) { return lv_color_hex(accents[g_set.accent % ui_accent_count()].hex); }
int ui_accent_count(void) { return sizeof(accents) / sizeof(accents[0]); }
const char *ui_accent_name(int i) { return accents[i % ui_accent_count()].name; }

bool ui_set_text(lv_obj_t *l, const char *t)
{
    if (!strcmp(lv_label_get_text(l), t)) return false;
    lv_label_set_text(l, t);
    return true;
}

void ui_set_bg_color(lv_obj_t *obj, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), c)) lv_obj_set_style_bg_color(obj, c, 0);
}

void ui_set_text_color(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), c)) lv_obj_set_style_text_color(o, c, 0);
}

static void set_pos(lv_obj_t *o, int32_t x, int32_t y)
{
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}

static void set_size(lv_obj_t *o, int32_t w, int32_t h)
{
    if (lv_obj_get_style_width(o, 0) != w || lv_obj_get_style_height(o, 0) != h) lv_obj_set_size(o, w, h);
}

static void set_width(lv_obj_t *o, int32_t w)
{
    if (lv_obj_get_style_width(o, 0) != w) lv_obj_set_width(o, w);
}

static void set_bg(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), c)) lv_obj_set_style_bg_color(o, c, 0);
}

static void set_hidden(lv_obj_t *o, bool hide)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hide) return;
    if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void anim_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void anim_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

/* ---------------- barra di stato ---------------- */

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}



static void bar_update(void)
{
    // chiamata ogni secondo: si tocca un'etichetta solo se cambia davvero, altrimenti
    // ogni secondo si ridisegnerebbe (e invierebbe al pannello) tutto lo schermo
    const frame_t *f = &stack[depth - 1];
    const char *title = f->app->title ? f->app->title(f->arg) : f->app->name;
    if (title && SCR_ROUND) {   // sul tondo c'è poco spazio: solo l'ultima parte del percorso
        const char *last = strstr(title, "» ");
        while (last) { title = last + strlen("» "); last = strstr(title, "» "); }
    }
    ui_set_text(bar_title, title ? title : "");

    char t[24];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_year >= 124) snprintf(t, sizeof(t), "%02d:%02d", lt.tm_hour, lt.tm_min);
    else t[0] = 0;
    ui_set_text(bar_time, t);

    switch (wifi_mgr_state()) {
    case WIFI_CONNECTED:
        ui_set_text(bar_wifi, LV_SYMBOL_WIFI);
        ui_set_text_color(bar_wifi, ui_accent());
        break;
    case WIFI_CONNECTING:
        ui_set_text(bar_wifi, LV_SYMBOL_WIFI);
        ui_set_text_color(bar_wifi, (lv_tick_get() / 1000) % 2 ? C_TEXT : C_DIM);
        break;
    case WIFI_NO_NETWORK:
        ui_set_text(bar_wifi, LV_SYMBOL_WIFI);
        ui_set_text_color(bar_wifi, C_DIM);
        break;
    default:
        ui_set_text(bar_wifi, "");
    }
    ui_set_text(bar_ble, ble_mgr_on() ? LV_SYMBOL_BLUETOOTH : "");

    // Una lettura sola dell'ADC oscilla di qualche % tra un secondo e l'altro: ogni volta
    // si ridisegnava tutto lo schermo (e partiva una notifica Bluetooth). Media mobile e
    // cambi di almeno 2 punti.
    static float v_avg;
    static int p_shown = -1;
    float v = board_battery_volts();
    if (v > 2.5f) {
        v_avg = v_avg < 2.5f ? v : v_avg + (v - v_avg) * 0.2f;
        int p = board_battery_percent(v_avg);
        if (p_shown < 0 || abs(p - p_shown) >= 2 || p == 100 || p == 0) p_shown = p;
        p = p_shown;
        ble_mgr_set_battery(p);
        const char *ic = p > 85 ? LV_SYMBOL_BATTERY_FULL : p > 60 ? LV_SYMBOL_BATTERY_3
                       : p > 35 ? LV_SYMBOL_BATTERY_2 : p > 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        // sull'AMOLED l'AXP2101 dice anche se è in carica
        snprintf(t, sizeof(t), "%s%s %d%%", board_charging() ? LV_SYMBOL_CHARGE " " : "", ic, p);
        ui_set_text(bar_batt, t);
        ui_set_text_color(bar_batt, p <= 12 ? C_WARN : C_TEXT);
    } else {
        v_avg = 0;
        p_shown = -1;
        ui_set_text(bar_batt, LV_SYMBOL_USB);
        ui_set_text_color(bar_batt, C_TEXT);
    }
}

/* ---------------- pila di schermate ---------------- */

static void show_top(int dir)
{
    lv_obj_clean(content);
    const frame_t *f = &stack[depth - 1];
    bool full = f->app->flags & APP_FULLSCREEN;
    if (full) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(content, 0, 0);
        lv_obj_set_size(content, SCR_W, SCR_H);
    } else {
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(content, 0, STATUS_H);
        lv_obj_set_size(content, SCR_W, CONTENT_H);
    }
    lv_obj_t *root = lv_obj_create(content);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    f->app->enter(root, f->arg);
    if (dir) {
        // breve scorrimento orizzontale: avanti da destra, indietro da sinistra
        lv_obj_set_style_translate_x(root, dir > 0 ? 40 : -40, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, root);
        lv_anim_set_values(&a, dir > 0 ? 40 : -40, 0);
        lv_anim_set_time(&a, 140);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, anim_tx);
        lv_anim_start(&a);
    }
    bar_update();
}

static bool closing;   // leave_top per un ritorno indietro (ui_pop/ui_home)

bool ui_closing(void) { return closing; }

static void leave_top(void)
{
    const frame_t *f = &stack[depth - 1];
    if (f->app->leave) f->app->leave();
}

static void close_top(void)
{
    closing = true;
    leave_top();
    closing = false;
}

void ui_push(const app_t *app, void *arg)
{
    if (depth >= STACK_MAX) return;
    // sullo schermo tondo le app si adattano una alla volta: quelle non ancora pronte
    // non si aprono (la loro grafica è fatta per 640×172)
    if (SCR_ROUND && !(app->flags & APP_ROUND_OK)) {
        ui_toast("Su questo schermo arriva con un prossimo aggiornamento");
        return;
    }
    if (depth > 0) leave_top();
    stack[depth++] = (frame_t){app, arg};
    show_top(+1);
}

void ui_pop(void)
{
    if (depth <= 1) return;
    close_top();
    depth--;
    show_top(-1);
}

void ui_home(void)
{
    while (depth > 1) { close_top(); depth--; }
    show_top(-1);
}

void ui_rebuild(void)
{
    leave_top();
    show_top(0);
}

/* ---------------- toast ---------------- */

static void toast_hide(lv_timer_t *t)
{
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    toast_timer = NULL;
}

void ui_toast(const char *msg)
{
    lv_label_set_text(toast, msg);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast);
    if (toast_timer) lv_timer_delete(toast_timer);
    toast_timer = lv_timer_create(toast_hide, 1800, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

/* ---------------- schermo e alimentazione ---------------- */

// Spento per inattività (o dall'azione rapida): un tocco lo riaccende. Spento col tasto di
// accensione: bloccato, tocchi e BOOT non fanno nulla (in tasca) finché non si ripreme il tasto.
static void screen_off(bool lock)
{
    sleeping = true;
    input_set_locked(lock);
    display_set_brightness(0);
}

void ui_screen_off(void) { screen_off(false); }

static int bright_override = -1;   // es. la torcia: al risveglio torna a questa luminosità

void ui_set_brightness_override(int pct) { bright_override = pct; }

static void screen_on(void)
{
    sleeping = false;
    input_set_locked(false);
    input_mark_activity();
    display_set_brightness(bright_override >= 0 ? bright_override : g_set.brightness);
}

static void power_off_check(lv_timer_t *t)
{
    // se siamo ancora vivi l'alimentazione arriva dalla USB
    screen_on();
    ui_toast("Alimentata via USB: scollega il cavo per spegnere");
}

// Durante lo spegnimento la schermata dell'app è già stata chiusa (leave + oggetti
// cancellati): finché non si torna (alimentazione USB) niente tick né eventi all'app.
static bool powering_off;

static void rebuild_cb(lv_timer_t *t)
{
    powering_off = false;
    show_top(0);   // leave è già stato chiamato in ui_power_off
}

void ui_power_off(void)
{
    if (powering_off) return;
    screen_on();
    leave_top();
    powering_off = true;
    lv_obj_clean(content);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_size(content, SCR_W, SCR_H);
    lv_obj_t *l = lv_label_create(content);
    lv_obj_set_style_text_font(l, &font_l, 0);
    lv_obj_set_style_text_color(l, C_DIM, 0);
    lv_label_set_text(l, "Spegnimento…");
    lv_obj_center(l);
    lv_refr_now(NULL);
    board_power_off();
    lv_timer_t *t = lv_timer_create(power_off_check, 1500, NULL);
    lv_timer_set_repeat_count(t, 1);
    // al ritorno ridisegna la schermata corrente
    lv_timer_t *r = lv_timer_create(rebuild_cb, 1500, NULL);
    lv_timer_set_repeat_count(r, 1);
}

static void quick_action(void)
{
    switch (g_set.quick_action) {
    case QUICK_TORCH:      ui_push(&app_torch, NULL); break;
    case QUICK_SCREEN_OFF: ui_screen_off(); break;
    case QUICK_DICE:       ui_push(&app_menu, &dice_menu); break;
    case QUICK_WIFI_SCAN:  ui_push(&app_wifiscan, NULL); break;
    case QUICK_BLE_SCAN:   ui_push(&app_blescan, NULL); break;
    case QUICK_CLOCK:      ui_push(&app_clock, NULL); break;
    default:               ui_toast("Nessuna azione rapida: sceglila in Impostazioni"); break;
    }
}

/* ---------------- dispatch eventi ---------------- */

static void on_nav(nav_t ev)
{
    if (powering_off) return;
    if (ev == NAV_PWR_LONG) { ui_power_off(); return; }
    if (ev == NAV_PWR_CLICK) { if (sleeping) screen_on(); else screen_off(true); return; }

    if (sleeping && input_locked()) return;   // bloccato col tasto: solo il tasto lo riaccende
    if (sleeping) {
        // qualunque contatto risveglia lo schermo e viene "consumato"
        if (ev == NAV_TOUCH_DOWN) swallow_touch = true;
        screen_on();
        return;
    }
    if (ev == NAV_TOUCH_DOWN) { swallow_touch = false; return; }
    // il tocco di risveglio va consumato, ma un pulsante no: se quel tocco non ha
    // prodotto un gesto, il flag resterebbe e mangerebbe il BOOT successivo
    if (ev == NAV_BTN || ev == NAV_QUICK) swallow_touch = false;
    if (swallow_touch) { swallow_touch = false; return; }
    if (ev == NAV_TAP) return; // il tocco semplice non conferma nulla

    const app_t *app = stack[depth - 1].app;
    if (ev == NAV_HOLD) ev = NAV_QUICK;
    if (ev == NAV_BTN) {
        if (app->nav && app->nav(NAV_BTN)) return;
        ev = NAV_BACK;
    }
    if (ev == NAV_QUICK && !(app->flags & APP_OWN_QUICK)) { quick_action(); return; }
    if (app->nav && app->nav(ev)) return;
    if (ev == NAV_BACK) ui_pop();
}

static void tick_cb(lv_timer_t *t)
{
    if (powering_off) return;
    const app_t *app = stack[depth - 1].app;
    if (app->tick) app->tick();
    if (!sleeping && g_set.sleep_s && !(app->flags & APP_NO_SLEEP) && input_idle_ms() > g_set.sleep_s * 1000u)
        ui_screen_off();
}

static void bar_cb(lv_timer_t *t)
{
    if (!sleeping && !lv_obj_has_flag(bar, LV_OBJ_FLAG_HIDDEN)) bar_update();
}

/* ---------------- lista "precedente / corrente / successiva" ---------------- */

/* Sullo schermo tondo (stile smartwatch) tutto è centrato: sopra la voce precedente,
 * poi l'icona, la voce corrente grande con il valore sotto, in basso la successiva; la
 * posizione nella lista è un arco sul bordo destro. Le altezze sono riferite al centro
 * del cerchio (ROUND_CY nel contenuto, sotto la barra). */
#define ROUND_CY (SCR_H / 2 - STATUS_H)

static lv_obj_t *mk_center(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int w, int y)
{
    lv_obj_t *l = mk_label(p, f, c);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void list_view_create_round(list_view_t *v, lv_obj_t *root)
{
    v->prev = mk_center(root, &font_m, C_DIM, 300, ROUND_CY - 146);
    v->icon = mk_center(root, &font_icon, ui_accent(), 120, ROUND_CY - 102);
    v->main = mk_center(root, &font_l, C_TEXT, 400, ROUND_CY - 30);
    v->sub = mk_center(root, &font_m, ui_accent(), 380, ROUND_CY + 22);
    lv_obj_set_style_radius(v->sub, 6, 0);
    v->next = mk_center(root, &font_m, C_DIM, 280, ROUND_CY + 128);
    v->marker = NULL;
    v->thumb = NULL;
    // arco sul bordo destro: tutta la lista, e in colore la posizione
    v->track = lv_arc_create(root);
    lv_obj_remove_style_all(v->track);
    lv_obj_set_size(v->track, SCR_W - 16, SCR_H - 16);
    lv_obj_set_pos(v->track, 8, 8 - STATUS_H);
    lv_arc_set_bg_angles(v->track, 325, 35);
    lv_obj_set_style_arc_width(v->track, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(v->track, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(v->track, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(v->track, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(v->track, ui_accent(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(v->track, true, LV_PART_INDICATOR);
    lv_arc_set_mode(v->track, LV_ARC_MODE_NORMAL);
    lv_obj_clear_flag(v->track, LV_OBJ_FLAG_CLICKABLE);
}

static void list_view_set_round(list_view_t *v, const char *t, bool has_sub, int index, int count)
{
    // la voce corrente grande; se non ci sta, carattere medio su due righe. Si misura il
    // testo passato: quello dell'etichetta, tagliato con i puntini, sarebbe già più corto
    const lv_font_t *f = lv_text_get_width(t, strlen(t), &font_l, 0) > 390 ? &font_m : &font_l;
    if (lv_obj_get_style_text_font(v->main, 0) != f) {
        lv_obj_set_style_text_font(v->main, f, 0);
        lv_label_set_long_mode(v->main, f == &font_m ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    }
    lv_obj_t *o[] = {v->main, v->sub};
    bool two = f == &font_m && lv_text_get_width(t, strlen(t), &font_m, 0) > 390;   // su due righe
    int ys[] = {two ? ROUND_CY - 44 : has_sub ? ROUND_CY - 30 : ROUND_CY - 24, two ? ROUND_CY + 30 : ROUND_CY + 22};
    for (int i = 0; i < 2; i++)
        if (lv_obj_get_style_y(o[i], 0) != ys[i]) lv_obj_align(o[i], LV_ALIGN_TOP_MID, 0, ys[i]);
    if (count > 1) {
        // l'arco va da 325° a 35° (70°): il segno è lungo 70/count, almeno 8°
        int span = 70 / count;
        if (span < 8) span = 8;
        int start = 325 + (70 - span) * index / (count - 1);
        int s = start % 360, e = (start + span) % 360;
        if (lv_arc_get_angle_start(v->track) != s || lv_arc_get_angle_end(v->track) != e) lv_arc_set_angles(v->track, s, e);
        if (!lv_color_eq(lv_obj_get_style_arc_color(v->track, LV_PART_INDICATOR), ui_accent()))
            lv_obj_set_style_arc_color(v->track, ui_accent(), LV_PART_INDICATOR);
        set_hidden(v->track, false);
    } else {
        set_hidden(v->track, true);
    }
}

void list_view_create(list_view_t *v, lv_obj_t *root)
{
    if (SCR_ROUND) { list_view_create_round(v, root); return; }
    v->prev = mk_label(root, &font_m, C_DIM);
    lv_obj_set_pos(v->prev, 76, 8);
    lv_obj_set_width(v->prev, 520);

    v->next = mk_label(root, &font_m, C_DIM);
    lv_obj_set_pos(v->next, 76, CONTENT_H - 30);
    lv_obj_set_width(v->next, 520);

    v->marker = lv_obj_create(root);
    lv_obj_remove_style_all(v->marker);
    lv_obj_set_size(v->marker, 5, 52);
    lv_obj_set_pos(v->marker, 0, CONTENT_H / 2 - 26);
    lv_obj_set_style_bg_color(v->marker, ui_accent(), 0);
    lv_obj_set_style_bg_opa(v->marker, LV_OPA_COVER, 0);

    v->icon = mk_label(root, &font_icon, ui_accent());
    lv_obj_set_width(v->icon, 56);
    lv_obj_set_style_text_align(v->icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(v->icon, 12, CONTENT_H / 2 - 24);

    v->main = mk_label(root, &font_l, C_TEXT);
    lv_obj_set_width(v->main, 530);

    v->sub = mk_label(root, &font_m, ui_accent());
    lv_obj_set_width(v->sub, 530);
    lv_obj_set_style_pad_hor(v->sub, 0, 0);
    lv_obj_set_style_radius(v->sub, 4, 0);

    v->track = lv_obj_create(root);
    lv_obj_remove_style_all(v->track);
    lv_obj_set_size(v->track, 3, CONTENT_H - 20);
    lv_obj_set_pos(v->track, SCR_W - 10, 10);
    lv_obj_set_style_bg_color(v->track, C_FAINT, 0);
    lv_obj_set_style_bg_opa(v->track, LV_OPA_COVER, 0);

    v->thumb = lv_obj_create(root);
    lv_obj_remove_style_all(v->thumb);
    lv_obj_set_style_bg_color(v->thumb, ui_accent(), 0);
    lv_obj_set_style_bg_opa(v->thumb, LV_OPA_COVER, 0);
}

static void anim_in(lv_obj_t *o, int dy)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, dy, 0);
    lv_anim_set_time(&a, 130);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_ty);
    lv_anim_start(&a);
}

void list_view_set(list_view_t *v, const char *icon, const char *prev, const char *main,
                   const char *sub, const char *next, int index, int count, int dir)
{
    // i menu si ridisegnano ogni secondo per i valori dinamici: si cambia solo il necessario
    lv_color_t acc = ui_accent();
    if (v->marker) set_bg(v->marker, acc);
    if (v->thumb) set_bg(v->thumb, acc);
    ui_set_text_color(v->icon, acc);
    ui_set_text_color(v->sub, acc);
    ui_set_text(v->icon, icon ? icon : "");
    ui_set_text(v->prev, prev ? prev : "");
    ui_set_text(v->next, next ? next : "");
    ui_set_text(v->main, main ? main : "");
    bool has_sub = sub && *sub;
    ui_set_text(v->sub, has_sub ? sub : "");
    if (SCR_ROUND) {
        list_view_set_round(v, main ? main : "", has_sub, index, count);
        if (dir) {
            int dy = dir > 0 ? 22 : -22;
            anim_in(v->main, dy);
            anim_in(v->icon, dy);
            anim_in(v->sub, dy);
        }
        return;
    }
    int x = icon ? 76 : 22;
    set_width(v->main, SCR_W - x - 24);
    set_width(v->sub, SCR_W - x - 24);
    if (has_sub) {
        set_pos(v->main, x, CONTENT_H / 2 - 36);
        set_pos(v->sub, x, CONTENT_H / 2 + 6);
    } else {
        set_pos(v->main, x, CONTENT_H / 2 - 20);
        set_pos(v->sub, x, CONTENT_H / 2 + 6);
    }

    int track_h = CONTENT_H - 20;
    if (count > 1) {
        int th = track_h / count;
        if (th < 10) th = 10;
        set_size(v->thumb, 3, th);
        set_pos(v->thumb, SCR_W - 10, 10 + (track_h - th) * index / (count - 1));
        set_hidden(v->thumb, false);
        set_hidden(v->track, false);
    } else {
        set_hidden(v->thumb, true);
        set_hidden(v->track, true);
    }
    if (dir) {
        int dy = dir > 0 ? 16 : -16;
        anim_in(v->main, dy);
        anim_in(v->icon, dy);
        anim_in(v->sub, dy);
    }
}

/* ---------------- init ---------------- */

void ui_init(void)
{
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, SCR_W, STATUS_H);
    if (!SCR_ROUND) {
        lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, C_FAINT, 0);
    }

    bar_title = mk_label(bar, &font_s, C_DIM);
    // ora, Wi-Fi, Bluetooth, batteria in una riga flex: a destra sulla 3.49; sul tondo
    // centrata in cima (dove il cerchio è stretto) con il titolo sotto
    bar_right = lv_obj_create(bar);
    lv_obj_remove_style_all(bar_right);
    lv_obj_set_flex_flow(bar_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar_right, 12, 0);
    if (SCR_ROUND) {
        lv_obj_set_width(bar_title, 260);
        lv_obj_set_style_text_align(bar_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(bar_title, LV_ALIGN_TOP_MID, 0, 50);
        // larghezza fissa: con LV_SIZE_CONTENT la riga non si allargava quando cambiavano
        // i testi, e ora e Wi-Fi restavano tagliati fuori
        // in cima il cerchio è stretto: la riga sta un po' più in basso, con voci più vicine
        lv_obj_set_size(bar_right, 260, 30);
        lv_obj_set_flex_align(bar_right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bar_right, 8, 0);
        lv_obj_align(bar_right, LV_ALIGN_TOP_MID, 0, 18);
    } else {
        lv_obj_set_width(bar_title, 330);
        lv_obj_align(bar_title, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_size(bar_right, 280, STATUS_H);   // fissa, voci allineate a destra (vedi sopra)
        lv_obj_align(bar_right, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    bar_time = mk_label(bar_right, &font_s, C_TEXT);
    bar_wifi = mk_label(bar_right, &font_s, C_TEXT);
    bar_ble = mk_label(bar_right, &font_s, C_TEXT);
    bar_batt = mk_label(bar_right, &font_s, C_TEXT);
    lv_label_set_long_mode(bar_time, LV_LABEL_LONG_WRAP);
    lv_label_set_long_mode(bar_wifi, LV_LABEL_LONG_WRAP);
    lv_label_set_long_mode(bar_ble, LV_LABEL_LONG_WRAP);
    lv_label_set_long_mode(bar_batt, LV_LABEL_LONG_WRAP);

    content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    toast = lv_label_create(scr);
    lv_obj_set_style_text_font(toast, &font_m, 0);
    lv_obj_set_style_text_color(toast, C_BG, 0);
    lv_obj_set_style_bg_color(toast, C_TEXT, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(toast, 14, 0);
    lv_obj_set_style_pad_ver(toast, 6, 0);
    lv_obj_set_style_radius(toast, 6, 0);
    if (SCR_ROUND) {   // più in alto (in basso il cerchio è stretto) e su più righe
        lv_obj_set_style_max_width(toast, 340, 0);
        lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
        lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -60);
    } else {
        lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

    home_init();
    depth = 0;
    stack[depth++] = (frame_t){&app_menu, &home_menu};
    show_top(0);

    input_init(on_nav);
    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(bar_cb, 1000, NULL);
}
