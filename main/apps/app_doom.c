// app_doom.c — menu Doom nel launcher: avvio, prova dei comandi, sensibilità
#include "apps.h"
#include "settings.h"
#include "tilt.h"
#include "sd.h"
#include "doom_app.h"
#include <stdio.h>
#include <string.h>

/* ---------------- prova inclinazione ---------------- */

static lv_obj_t *bar_st, *bar_pt, *val_st, *val_pt;
static lv_timer_t *tt;

static lv_obj_t *mk_bar(lv_obj_t *root, int y, const char *name, lv_obj_t **val)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, &font_m, 0);
    lv_obj_set_style_text_color(l, C_DIM, 0);
    lv_label_set_text(l, name);
    lv_obj_set_pos(l, 24, y);

    lv_obj_t *b = lv_bar_create(root);
    lv_bar_set_range(b, -100, 100);
    lv_bar_set_mode(b, LV_BAR_MODE_SYMMETRICAL);
    lv_obj_set_size(b, 330, 14);
    lv_obj_set_pos(b, 200, y + 4);
    lv_obj_set_style_bg_color(b, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, ui_accent(), LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(b, 0, 0);

    *val = lv_label_create(root);
    lv_obj_set_style_text_font(*val, &font_m, 0);
    lv_obj_set_style_text_color(*val, C_TEXT, 0);
    lv_obj_set_pos(*val, 548, y);
    return b;
}

static void tt_cb(lv_timer_t *t)
{
    float st, pt;
    if (!tilt_read(&st, &pt)) {
        lv_label_set_text(val_st, "IMU?");
        return;
    }
    lv_bar_set_value(bar_st, (int)(tilt_axis(st) * 100), LV_ANIM_OFF);
    lv_bar_set_value(bar_pt, (int)(tilt_axis(pt) * 100), LV_ANIM_OFF);
    lv_label_set_text_fmt(val_st, "%+d°", (int)st);
    lv_label_set_text_fmt(val_pt, "%+d°", (int)pt);
    input_mark_activity();
}

static void tt_enter(lv_obj_t *root, void *arg)
{
    bar_st = mk_bar(root, 22, "Sterzo  " LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, &val_st);
    bar_pt = mk_bar(root, 62, "Avanti  " LV_SYMBOL_UP " " LV_SYMBOL_DOWN, &val_pt);
    lv_obj_t *h = lv_label_create(root);
    lv_obj_set_style_text_font(h, &font_s, 0);
    lv_obj_set_style_text_color(h, C_DIM, 0);
    lv_obj_set_width(h, SCR_W - 48);
    lv_label_set_text(h, "Tienilo come per giocare e fai swipe a destra per calibrare. Inclina a destra e in avanti: "
                         "le barre devono andare verso destra. Se vanno al contrario usa le voci Inverti.");
    lv_obj_set_pos(h, 24, 100);
    tilt_calibrate();
    tt = lv_timer_create(tt_cb, 60, NULL);
}

static void tt_leave(void) { if (tt) { lv_timer_delete(tt); tt = NULL; } }

static bool tt_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { tilt_calibrate(); ui_toast("Posizione di riposo salvata"); return true; }
    return ev == NAV_NEXT || ev == NAV_PREV;
}

static const app_t app_tilt = {
    .name = "Prova inclinazione", .icon = ICON_GAMEPAD,
    .enter = tt_enter, .leave = tt_leave, .nav = tt_nav, .flags = APP_NO_SLEEP,
};

/* ---------------- menu Doom ---------------- */

static void v_wad(char *b, int n)
{
    char p[64];
    if (!sd_ok()) snprintf(b, n, "microSD non trovata");
    else if (doom_find_wad(p, sizeof(p))) snprintf(b, n, "%s", p + strlen(SD_MOUNT) + 1);
    else snprintf(b, n, "Manca: copia doom1.wad nella cartella doom della microSD");
}

static void a_play(void)
{
    char p[64];
    if (!doom_find_wad(p, sizeof(p))) { ui_toast("Nessun file WAD sulla microSD"); return; }
    ui_toast("Avvio Doom…");
    lv_refr_now(NULL);
    doom_launch();
}

static void v_sens(char *b, int n) { snprintf(b, n, "%d di 5 · piena a %d°", g_set.doom_sens, (int)tilt_full_scale()); }
static void j_sens(int d)
{
    int s = g_set.doom_sens + d;
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    g_set.doom_sens = s;
    settings_save();
}
static void v_inv_st(char *b, int n) { snprintf(b, n, "%s", g_set.doom_inv_steer ? "Sì" : "No"); }
static void a_inv_st(void) { g_set.doom_inv_steer = !g_set.doom_inv_steer; settings_save(); }
static void v_inv_pt(char *b, int n) { snprintf(b, n, "%s", g_set.doom_inv_pitch ? "Sì" : "No"); }
static void a_inv_pt(void) { g_set.doom_inv_pitch = !g_set.doom_inv_pitch; settings_save(); }
static void v_help(char *b, int n) { snprintf(b, n, "BOOT ricalibra · BOOT tenuto esce · PWR tenuto spegne"); }

static const menu_item_t doom_items[] = {
    {.icon = ICON_GAMEPAD, .label = "Gioca", .value = v_wad, .on_select = a_play},
    {.icon = ICON_SLIDERS, .label = "Prova inclinazione", .app = &app_tilt},
    {.icon = ICON_BOLT, .label = "Sensibilità", .value = v_sens, .on_adjust = j_sens},
    {.icon = LV_SYMBOL_LOOP, .label = "Inverti sterzo", .value = v_inv_st, .on_select = a_inv_st},
    {.icon = LV_SYMBOL_LOOP, .label = "Inverti avanti/indietro", .value = v_inv_pt, .on_select = a_inv_pt},
    {.icon = ICON_INFO, .label = "Comandi", .value = v_help},
};
menu_t doom_menu = {"Doom", doom_items, sizeof(doom_items) / sizeof(doom_items[0]), 0};
