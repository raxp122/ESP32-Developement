// app_ota.c — Impostazioni › Sistema › Aggiornamento firmware.
// All'apertura controlla se su GitHub c'è una versione nuova; swipe a destra la installa
// (serve un secondo swipe di conferma). A fine download la scheda si riavvia da sola.
#include "apps.h"
#include "ota.h"
#include "wifi_mgr.h"
#include <stdio.h>

static lv_obj_t *l_ver, *l_state, *l_hint, *bar;
static uint32_t confirm_until;

static lv_obj_t *mk(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, SCR_W - 48);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 24, y);
    lv_label_set_text(l, "");
    return l;
}

static void render(void)
{
    char t[96];
    ota_state_t s = ota_state();
    const char *latest = ota_latest();
    snprintf(t, sizeof(t), "In uso: %s%s%s", ota_current(), latest[0] ? " · ultima: " : "", latest);
    ui_set_text(l_ver, t);   // gira a ogni tick: solo i cambiamenti ridisegnano lo schermo

    bool confirming = lv_tick_get() < confirm_until && s == OTA_AVAILABLE;
    const char *state = "", *hint = "";
    lv_color_t col = C_TEXT;
    switch (s) {
    case OTA_IDLE:       state = "Pronto"; hint = "Swipe a destra: controlla"; break;
    case OTA_CHECKING:   state = "Controllo in corso…"; break;
    case OTA_UP_TO_DATE: state = "È già l'ultima versione"; col = C_OK; hint = "Swipe a destra: controlla di nuovo"; break;
    case OTA_AVAILABLE:
        snprintf(t, sizeof(t), "Nuova versione %s", latest);
        state = t;
        col = ui_accent();
        hint = confirming ? "Swipe a destra di nuovo per installare" : "Swipe a destra: installa (impostazioni e polipetto restano)";
        break;
    case OTA_DOWNLOADING:
        snprintf(t, sizeof(t), "Download… %d%%", ota_progress());
        state = t;
        hint = "Non spegnere la scheda";
        break;
    case OTA_DONE:       state = "Installato! Riavvio…"; col = C_OK; break;
    case OTA_ERROR:      state = ota_error(); col = C_WARN; hint = "Swipe a destra: riprova"; break;
    }
    ui_set_text(l_state, state);
    ui_set_text_color(l_state, col);
    ui_set_text(l_hint, hint);
    ui_set_text_color(l_hint, confirming ? C_WARN : C_DIM);
    bool show = s == OTA_DOWNLOADING || s == OTA_DONE;
    if (show == lv_obj_has_flag(bar, LV_OBJ_FLAG_HIDDEN)) {
        if (show) lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (show) lv_bar_set_value(bar, ota_progress(), LV_ANIM_OFF);
}

static void enter(lv_obj_t *root, void *arg)
{
    l_ver = mk(root, &font_m, C_DIM, 14);
    l_state = mk(root, &font_l, C_TEXT, 48);
    bar = lv_bar_create(root);
    lv_obj_set_size(bar, SCR_W - 48, 10);
    lv_obj_set_pos(bar, 24, 98);
    lv_obj_set_style_bg_color(bar, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, ui_accent(), LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    l_hint = mk(root, &font_s, C_DIM, 118);
    confirm_until = 0;
    ota_state_t s = ota_state();
    if (s == OTA_IDLE || s == OTA_UP_TO_DATE || s == OTA_ERROR) ota_check();
    render();
}

static bool nav(nav_t ev)
{
    if (ev != NAV_SELECT) return false;
    switch (ota_state()) {
    case OTA_AVAILABLE:
        if (lv_tick_get() < confirm_until) { confirm_until = 0; ota_install(); }
        else confirm_until = lv_tick_get() + 4000;
        break;
    case OTA_IDLE:
    case OTA_UP_TO_DATE:
    case OTA_ERROR:
        ota_check();
        break;
    default:
        break;
    }
    render();
    return true;
}

static const char *title(void *arg) { return "Impostazioni › Aggiornamento firmware"; }

const app_t app_ota = {
    .name = "Aggiornamento", .icon = LV_SYMBOL_DOWNLOAD,
    .enter = enter, .nav = nav, .tick = render, .title = title,
    .flags = APP_NO_SLEEP,
};
