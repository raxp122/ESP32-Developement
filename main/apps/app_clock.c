// app_clock.c — orologio grande con data
#include "apps.h"
#include <time.h>
#include <stdio.h>

static lv_obj_t *l_time, *l_sec, *l_date;
static bool show_sec = true;

static const char *giorni[] = {"Domenica", "Lunedì", "Martedì", "Mercoledì", "Giovedì", "Venerdì", "Sabato"};
static const char *mesi[] = {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio",
                             "agosto", "settembre", "ottobre", "novembre", "dicembre"};

static void update(void)
{
    // chiamata 5 volte al secondo: le etichette si toccano solo quando cambiano
    char b[48];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) {
        ui_set_text(l_time, "--:--");
        ui_set_text(l_sec, "");
        ui_set_text(l_date, "Ora non impostata: collega il Wi-Fi");
        return;
    }
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    // le cifre non sono tutte larghe uguali: i secondi seguono la nuova larghezza dell'ora
    // (sullo schermo tondo stanno sotto, centrati)
    if (ui_set_text(l_time, b) && !SCR_ROUND) lv_obj_align_to(l_sec, l_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -8);
    if (show_sec) snprintf(b, sizeof(b), "%02d", t.tm_sec);
    else b[0] = 0;
    ui_set_text(l_sec, b);
    snprintf(b, sizeof(b), "%s %d %s", giorni[t.tm_wday], t.tm_mday, mesi[t.tm_mon]);
    ui_set_text(l_date, b);
}

static void enter(lv_obj_t *root, void *arg)
{
    l_time = lv_label_create(root);
    lv_obj_set_style_text_font(l_time, &font_xl, 0);
    lv_obj_set_style_text_color(l_time, C_TEXT, 0);
    lv_obj_align(l_time, LV_ALIGN_LEFT_MID, 36, -14);

    l_sec = lv_label_create(root);
    lv_obj_set_style_text_font(l_sec, &font_l, 0);
    lv_obj_set_style_text_color(l_sec, ui_accent(), 0);
    lv_obj_align_to(l_sec, l_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -8);

    l_date = lv_label_create(root);
    lv_obj_set_style_text_font(l_date, &font_m, 0);
    lv_obj_set_style_text_color(l_date, C_DIM, 0);
    lv_obj_align(l_date, LV_ALIGN_LEFT_MID, 40, 46);
    if (SCR_ROUND) {   // ora al centro del cerchio, secondi e data sotto
        int cy = SCR_H / 2 - STATUS_H;
        lv_obj_align(l_time, LV_ALIGN_TOP_MID, 0, cy - 70);
        lv_obj_align(l_sec, LV_ALIGN_TOP_MID, 0, cy + 36);
        lv_obj_set_width(l_date, 340);
        lv_obj_set_style_text_align(l_date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l_date, LV_ALIGN_TOP_MID, 0, cy + 96);
        update();
        return;
    }
    update();
    lv_obj_align_to(l_sec, l_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -8);
}

static bool nav(nav_t ev)
{
    if (ev == NAV_SELECT) { show_sec = !show_sec; update(); return true; }
    return false;
}

const app_t app_clock = {
    .name = "Orologio", .icon = ICON_CLOCK,
    .enter = enter, .nav = nav, .tick = update, .flags = APP_ROUND_OK,
};
