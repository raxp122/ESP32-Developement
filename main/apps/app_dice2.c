// app_dice2.c — Dadi: pool di dadi misti con somma automatica + modalità Daggerheart
//
// Menu Dadi:  Tira · d4 … d100 (quantità) · Modificatore · Azzera · Daggerheart
// Schermata risultato: swipe a destra o scuoti = ritira, sinistra = torna al pool
#include "apps.h"
#include "board.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_random.h"

#define NT 7
static const int faces[NT] = {4, 6, 8, 10, 12, 20, 100};
static int counts[NT] = {0, 0, 0, 0, 0, 1, 0};   // di default: 1d20
static int mod;

static int rnd(int n) { return (int)(esp_random() % (uint32_t)n) + 1; }

static int pool_dice(void)
{
    int n = 0;
    for (int i = 0; i < NT; i++) n += counts[i];
    return n;
}

static void pool_text(char *b, int n)
{
    int o = 0;
    b[0] = 0;
    for (int i = 0; i < NT && o < n - 1; i++)
        if (counts[i]) o += snprintf(b + o, n - o, "%s%dd%d", o ? " + " : "", counts[i], faces[i]);
    if (mod && o < n - 1) o += snprintf(b + o, n - o, " %c %d", mod > 0 ? '+' : '-', mod > 0 ? mod : -mod);
    if (!o) snprintf(b, n, "Nessun dado: aggiungine qui sotto");
}

/* ---------------- shake ---------------- */

static uint32_t last_shake;

static bool shaken(void)
{
    vec3_t a;
    if (!board_imu_accel(&a)) return false;
    float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (g > 2.0f && lv_tick_elaps(last_shake) > 1200) {
        last_shake = lv_tick_get();
        input_mark_activity();
        return true;
    }
    return false;
}

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ================= tiro del pool ================= */

static lv_obj_t *r_total, *r_detail, *r_pool, *r_note, *r_hist;
static lv_timer_t *r_anim;
static int r_steps, r_result, r_rolls[64], r_nrolls;
static int hist[6], hist_n;

static void r_show_final(void)
{
    // dettaglio: "d6: 3 5 · d8: 7 · +2"
    // con molti dadi il testo può superare il buffer: snprintf tronca, ma "o" crescerebbe
    // oltre la fine e sizeof(d) - o diventerebbe enorme (scrittura fuori dal buffer)
    char d[256];
    int o = 0, k = 0;
#define ROOM() (o < (int)sizeof(d) - 1)
    for (int i = 0; i < NT && ROOM(); i++) {
        if (!counts[i]) continue;
        o += snprintf(d + o, sizeof(d) - o, "%sd%d:", o ? "  ·  " : "", faces[i]);
        for (int j = 0; j < counts[i] && k < r_nrolls && ROOM(); j++) o += snprintf(d + o, sizeof(d) - o, " %d", r_rolls[k++]);
    }
    if (mod && ROOM()) snprintf(d + o, sizeof(d) - o, "  ·  %+d", mod);
#undef ROOM
    lv_label_set_text(r_detail, d);

    lv_label_set_text_fmt(r_total, "%d", r_result);
    lv_obj_set_style_text_color(r_total, C_TEXT, 0);
    // naturale 20/1 se si tira un solo d20
    const char *note = "";
    if (pool_dice() == 1 && counts[5] == 1) {
        if (r_rolls[0] == 20) { note = "20 naturale!"; lv_obj_set_style_text_color(r_total, C_OK, 0); }
        if (r_rolls[0] == 1)  { note = "1 naturale";   lv_obj_set_style_text_color(r_total, C_WARN, 0); }
    }
    lv_label_set_text(r_note, note);

    memmove(hist + 1, hist, sizeof(int) * 5);
    hist[0] = r_result;
    if (hist_n < 6) hist_n++;
    char h[64] = "";
    int ho = 0;
    for (int i = 0; i < hist_n; i++) ho += snprintf(h + ho, sizeof(h) - ho, "%s%d", i ? "  " : "", hist[i]);
    lv_label_set_text(r_hist, h);
}

static void r_anim_cb(lv_timer_t *t)
{
    if (--r_steps > 0) {
        int fake = mod;
        for (int i = 0; i < NT; i++) for (int j = 0; j < counts[i]; j++) fake += rnd(faces[i]);
        lv_label_set_text_fmt(r_total, "%d", fake);
        lv_obj_set_style_text_color(r_total, C_DIM, 0);
        return;
    }
    lv_timer_delete(r_anim);
    r_anim = NULL;
    r_show_final();
}

static void r_roll(void)
{
    if (r_anim || !pool_dice()) return;
    r_nrolls = 0;
    r_result = mod;
    for (int i = 0; i < NT; i++)
        for (int j = 0; j < counts[i]; j++) {
            int v = rnd(faces[i]);
            if (r_nrolls < 64) r_rolls[r_nrolls++] = v;
            r_result += v;
        }
    lv_label_set_text(r_note, "");
    lv_label_set_text(r_detail, "");
    r_steps = 12;
    r_anim = lv_timer_create(r_anim_cb, 45, NULL);
}

static void r_enter(lv_obj_t *root, void *arg)
{
    char p[96];
    pool_text(p, sizeof(p));
    r_pool = mk(root, &font_m, ui_accent());
    lv_label_set_text_fmt(r_pool, ICON_D20 "  %s", p);
    lv_obj_set_pos(r_pool, 24, 10);
    lv_obj_set_width(r_pool, 420);
    lv_label_set_long_mode(r_pool, LV_LABEL_LONG_DOT);

    r_total = mk(root, &font_xl, C_TEXT);
    lv_obj_set_pos(r_total, 24, 34);
    lv_label_set_text(r_total, "?");

    r_note = mk(root, &font_m, C_DIM);
    lv_obj_set_pos(r_note, 230, 60);

    r_detail = mk(root, &font_m, C_DIM);
    lv_obj_set_pos(r_detail, 24, CONTENT_H - 30);
    lv_obj_set_width(r_detail, SCR_W - 48);
    lv_label_set_long_mode(r_detail, LV_LABEL_LONG_DOT);

    lv_obj_t *h = mk(root, &font_s, C_DIM);
    lv_label_set_text(h, "Ultimi tiri");
    lv_obj_align(h, LV_ALIGN_TOP_RIGHT, -24, 12);
    r_hist = mk(root, &font_m, C_TEXT);
    lv_obj_align(r_hist, LV_ALIGN_TOP_RIGHT, -24, 32);
    hist_n = 0;
    r_roll();
}

static void r_leave(void) { if (r_anim) { lv_timer_delete(r_anim); r_anim = NULL; } }

static bool r_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { r_roll(); return true; }
    return ev == NAV_NEXT || ev == NAV_PREV;
}

static void r_tick(void) { if (shaken()) r_roll(); }

static const app_t app_roll = {
    .name = "Tiro", .icon = ICON_D20,
    .enter = r_enter, .leave = r_leave, .nav = r_nav, .tick = r_tick,
};

/* ================= Daggerheart ================= */

#define C_HOPE lv_color_hex(0xFFC24D)
#define C_FEAR lv_color_hex(0xB07CFF)

static int dh_mod, dh_adv;   // dh_adv: 0 nessuno, 1 vantaggio (+d6), -1 svantaggio (−d6)
static lv_obj_t *d_hope, *d_fear, *d_total, *d_outcome, *d_detail;
static lv_timer_t *d_anim;
static int d_steps, d_h, d_f, d_x;

static void d_final(void)
{
    lv_label_set_text_fmt(d_hope, "%d", d_h);
    lv_label_set_text_fmt(d_fear, "%d", d_f);
    lv_obj_set_style_text_color(d_hope, C_HOPE, 0);
    lv_obj_set_style_text_color(d_fear, C_FEAR, 0);
    int total = d_h + d_f + dh_mod + dh_adv * d_x;
    lv_label_set_text_fmt(d_total, "%d", total);

    if (d_h == d_f) {
        lv_label_set_text(d_outcome, "CRITICO!");
        lv_obj_set_style_text_color(d_outcome, C_OK, 0);
        lv_obj_set_style_text_color(d_total, C_OK, 0);
    } else if (d_h > d_f) {
        lv_label_set_text(d_outcome, "con Speranza");
        lv_obj_set_style_text_color(d_outcome, C_HOPE, 0);
        lv_obj_set_style_text_color(d_total, C_HOPE, 0);
    } else {
        lv_label_set_text(d_outcome, "con Paura");
        lv_obj_set_style_text_color(d_outcome, C_FEAR, 0);
        lv_obj_set_style_text_color(d_total, C_FEAR, 0);
    }

    char b[96];
    int o = snprintf(b, sizeof(b), "%d + %d", d_h, d_f);
    if (dh_adv) o += snprintf(b + o, sizeof(b) - o, " %c %d (d6 %s)", dh_adv > 0 ? '+' : '-', d_x, dh_adv > 0 ? "vantaggio" : "svantaggio");
    if (dh_mod) snprintf(b + o, sizeof(b) - o, " %c %d", dh_mod > 0 ? '+' : '-', dh_mod > 0 ? dh_mod : -dh_mod);
    lv_label_set_text(d_detail, b);
}

static void d_anim_cb(lv_timer_t *t)
{
    if (--d_steps > 0) {
        lv_label_set_text_fmt(d_hope, "%d", rnd(12));
        lv_label_set_text_fmt(d_fear, "%d", rnd(12));
        lv_label_set_text(d_total, "");
        return;
    }
    lv_timer_delete(d_anim);
    d_anim = NULL;
    d_final();
}

static void d_roll(void)
{
    if (d_anim) return;
    d_h = rnd(12);
    d_f = rnd(12);
    d_x = rnd(6);
    lv_label_set_text(d_outcome, "");
    lv_label_set_text(d_detail, "");
    lv_obj_set_style_text_color(d_hope, C_DIM, 0);
    lv_obj_set_style_text_color(d_fear, C_DIM, 0);
    d_steps = 14;
    d_anim = lv_timer_create(d_anim_cb, 45, NULL);
}

static void d_enter(lv_obj_t *root, void *arg)
{
    // colonne: Speranza | Paura | Risultato
    lv_obj_t *l1 = mk(root, &font_m, C_HOPE);
    lv_label_set_text(l1, "Speranza");
    lv_obj_set_pos(l1, 24, 8);
    d_hope = mk(root, &font_xl, C_HOPE);
    lv_obj_set_pos(d_hope, 24, 30);

    lv_obj_t *l2 = mk(root, &font_m, C_FEAR);
    lv_label_set_text(l2, "Paura");
    lv_obj_set_pos(l2, 170, 8);
    d_fear = mk(root, &font_xl, C_FEAR);
    lv_obj_set_pos(d_fear, 170, 30);

    lv_obj_t *sep = lv_obj_create(root);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, CONTENT_H - 50);
    lv_obj_set_pos(sep, 320, 12);
    lv_obj_set_style_bg_color(sep, C_FAINT, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    lv_obj_t *l3 = mk(root, &font_m, C_DIM);
    lv_label_set_text(l3, "Risultato");
    lv_obj_set_pos(l3, 344, 8);
    d_total = mk(root, &font_xl, C_TEXT);
    lv_obj_set_pos(d_total, 344, 30);
    d_outcome = mk(root, &font_m, C_TEXT);
    lv_obj_set_pos(d_outcome, 470, 64);

    d_detail = mk(root, &font_m, C_DIM);
    lv_obj_set_pos(d_detail, 24, CONTENT_H - 30);
    lv_obj_set_width(d_detail, SCR_W - 48);
    lv_label_set_long_mode(d_detail, LV_LABEL_LONG_DOT);
    d_roll();
}

static void d_leave(void) { if (d_anim) { lv_timer_delete(d_anim); d_anim = NULL; } }

static bool d_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { d_roll(); return true; }
    return ev == NAV_NEXT || ev == NAV_PREV;
}

static void d_tick(void) { if (shaken()) d_roll(); }

static const app_t app_dh_roll = {
    .name = "Tiro di Dualità", .icon = ICON_D20,
    .enter = d_enter, .leave = d_leave, .nav = d_nav, .tick = d_tick,
};

static void v_dh_pool(char *b, int n)
{
    int o = snprintf(b, n, "2d12 Speranza e Paura");
    if (dh_adv) o += snprintf(b + o, n - o, " %c d6", dh_adv > 0 ? '+' : '-');
    if (dh_mod) snprintf(b + o, n - o, " %c %d", dh_mod > 0 ? '+' : '-', dh_mod > 0 ? dh_mod : -dh_mod);
}
static void v_dh_mod(char *b, int n) { snprintf(b, n, "%+d", dh_mod); }
static void j_dh_mod(int d) { dh_mod += d; if (dh_mod > 20) dh_mod = 20; if (dh_mod < -20) dh_mod = -20; }
static void v_dh_adv(char *b, int n) { snprintf(b, n, "%s", dh_adv > 0 ? "Vantaggio (+d6)" : dh_adv < 0 ? "Svantaggio (-d6)" : "Nessuno"); }
static void j_dh_adv(int d) { dh_adv += d; if (dh_adv > 1) dh_adv = 1; if (dh_adv < -1) dh_adv = -1; }

static const menu_item_t dh_items[] = {
    {.icon = ICON_D20, .label = "Tira", .value = v_dh_pool, .app = &app_dh_roll},
    {.icon = ICON_SLIDERS, .label = "Modificatore", .value = v_dh_mod, .on_adjust = j_dh_mod},
    {.icon = ICON_BOLT, .label = "Vantaggio", .value = v_dh_adv, .on_adjust = j_dh_adv},
};
static menu_t dh_menu = {"Dadi › Daggerheart", dh_items, sizeof(dh_items) / sizeof(dh_items[0]), 0};

/* ================= menu Dadi ================= */

static void v_pool(char *b, int n) { pool_text(b, n); }
static void a_roll(void)
{
    if (!pool_dice()) { ui_toast("Aggiungi almeno un dado"); return; }
    ui_push(&app_roll, NULL);
}

static void v_count(int i, char *b, int n) { snprintf(b, n, counts[i] ? "× %d" : "nessuno", counts[i]); }
static void j_count(int i, int d)
{
    counts[i] += d;
    if (counts[i] < 0) counts[i] = 0;
    if (counts[i] > 10) counts[i] = 10;
}

#define DIE_FUNCS(i) \
    static void v_c##i(char *b, int n) { v_count(i, b, n); } \
    static void j_c##i(int d) { j_count(i, d); }
DIE_FUNCS(0) DIE_FUNCS(1) DIE_FUNCS(2) DIE_FUNCS(3) DIE_FUNCS(4) DIE_FUNCS(5) DIE_FUNCS(6)

static void v_mod(char *b, int n) { snprintf(b, n, "%+d", mod); }
static void j_mod(int d) { mod += d; if (mod > 50) mod = 50; if (mod < -50) mod = -50; }
static void a_clear(void)
{
    memset(counts, 0, sizeof(counts));
    mod = 0;
    ui_toast("Pool svuotato");
}

static const menu_item_t dice_items[] = {
    {.icon = ICON_D20, .label = "Tira", .value = v_pool, .on_select = a_roll},
    {.icon = ICON_DICE, .label = "d4",   .value = v_c0, .on_adjust = j_c0},
    {.icon = ICON_DICE, .label = "d6",   .value = v_c1, .on_adjust = j_c1},
    {.icon = ICON_DICE, .label = "d8",   .value = v_c2, .on_adjust = j_c2},
    {.icon = ICON_DICE, .label = "d10",  .value = v_c3, .on_adjust = j_c3},
    {.icon = ICON_DICE, .label = "d12",  .value = v_c4, .on_adjust = j_c4},
    {.icon = ICON_D20,  .label = "d20",  .value = v_c5, .on_adjust = j_c5},
    {.icon = ICON_DICE, .label = "d100", .value = v_c6, .on_adjust = j_c6},
    {.icon = ICON_SLIDERS, .label = "Modificatore", .value = v_mod, .on_adjust = j_mod},
    {.icon = LV_SYMBOL_TRASH, .label = "Svuota il pool", .on_select = a_clear},
    {.icon = ICON_GHOST, .label = "Daggerheart", .value = NULL, .app = &app_menu, .arg = &dh_menu},
};
menu_t dice_menu = {"Dadi", dice_items, sizeof(dice_items) / sizeof(dice_items[0]), 0};
