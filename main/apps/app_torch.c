// app_torch.c — schermo pieno come torcia; su/giù cambia colore, destra accende/spegne
#include "apps.h"
#include "settings.h"

static const struct { const char *name; uint32_t c; } modes[] = {
    {"Bianca", 0xFFFFFF}, {"Calda", 0xFFC27A}, {"Rossa (visione notturna)", 0xFF0000}, {"Verde", 0x00FF40},
};
#define NM (int)(sizeof(modes) / sizeof(modes[0]))

static int mode;
static bool on = true;
static lv_obj_t *bg, *label;
static lv_timer_t *hide_timer;

static void hide_label(lv_timer_t *t) { lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN); hide_timer = NULL; }

static void apply(void)
{
    lv_obj_set_style_bg_color(bg, on ? lv_color_hex(modes[mode].c) : C_BG, 0);
    lv_label_set_text(label, on ? modes[mode].name : "Spenta · swipe a destra per accendere");
    lv_obj_set_style_text_color(label, on ? C_BG : C_DIM, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    if (hide_timer) lv_timer_delete(hide_timer);
    hide_timer = NULL;
    if (on) {
        hide_timer = lv_timer_create(hide_label, 1200, NULL);
        lv_timer_set_repeat_count(hide_timer, 1);
    }
    display_set_brightness(on ? 100 : g_set.brightness);
}

static void enter(lv_obj_t *root, void *arg)
{
    bg = lv_obj_create(root);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    label = lv_label_create(bg);
    lv_obj_set_style_text_font(label, &font_m, 0);
    lv_obj_center(label);
    on = true;
    apply();
}

static void leave(void)
{
    if (hide_timer) { lv_timer_delete(hide_timer); hide_timer = NULL; }
    display_set_brightness(g_set.brightness);
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_SELECT: on = !on; apply(); return true;
    case NAV_NEXT: mode = (mode + 1) % NM; on = true; apply(); return true;
    case NAV_PREV: mode = (mode + NM - 1) % NM; on = true; apply(); return true;
    default: return false;
    }
}

const app_t app_torch = {
    .name = "Torcia", .icon = ICON_BULB,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};
