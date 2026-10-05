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
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) {
        lv_label_set_text(l_time, "--:--");
        lv_label_set_text(l_sec, "");
        lv_label_set_text(l_date, "Ora non impostata: collega il Wi-Fi");
        return;
    }
    lv_label_set_text_fmt(l_time, "%02d:%02d", t.tm_hour, t.tm_min);
    if (show_sec) lv_label_set_text_fmt(l_sec, "%02d", t.tm_sec);
    else lv_label_set_text(l_sec, "");
    lv_label_set_text_fmt(l_date, "%s %d %s", giorni[t.tm_wday], t.tm_mday, mesi[t.tm_mon]);
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
    .enter = enter, .nav = nav, .tick = update,
};
