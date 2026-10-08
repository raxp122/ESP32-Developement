// app_seismo.c — Sismografo: appoggia il Gadget su una superficie e guarda le vibrazioni.
// Il grafico scorre (asse verticale, scala automatica); gli eventi si riconoscono da soli e
// si registrano sulla microSD (vedi seismo.c). BOOT azzera il picco, swipe a destra apre
// l'elenco degli eventi. Lo schermo può spegnersi: le letture continuano.
#include "apps.h"
#include "seismo.h"
#include "sd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cfg_loaded;
static void cfg(void) { if (!cfg_loaded) { seismo_cfg_load(); cfg_loaded = true; } }

// 12.3 → "12,3"
static void fmt_mg(char *b, int n, float v)
{
    snprintf(b, n, v < 100 ? "%.1f" : "%.0f", v);
    char *p = strchr(b, '.');
    if (p) *p = ',';
}

/* ================= elenco degli eventi ================= */

#define MAXEV 50
static seismo_event_t *evs;
static int n_evs, e_sel;
static list_view_t e_lv;

static void e_render(int dir)
{
    if (!n_evs) {
        list_view_set(&e_lv, LV_SYMBOL_LIST, NULL, "Nessun evento",
                      sd_ok() ? "Lascia il Gadget appoggiato con il Sismografo aperto" : "Manca la microSD",
                      NULL, 0, 0, dir);
        return;
    }
    const seismo_event_t *e = &evs[e_sel];
    char sub[96], pk[16];
    fmt_mg(pk, sizeof(pk), e->peak_mg);
    snprintf(sub, sizeof(sub), "picco %s mg · %.1f s · intensità %s", pk, e->dur_s, seismo_roman(e->mmi));
    char *p = strchr(sub + 10, '.');   // anche la durata con la virgola
    if (p && p[1] >= '0' && p[1] <= '9') *p = ',';
    list_view_set(&e_lv, LV_SYMBOL_LIST, e_sel > 0 ? evs[e_sel - 1].when : NULL, e->when, sub,
                  e_sel + 1 < n_evs ? evs[e_sel + 1].when : NULL, e_sel, n_evs, dir);
}

static void e_enter(lv_obj_t *root, void *arg)
{
    if (!evs) evs = malloc(sizeof(seismo_event_t) * MAXEV);
    n_evs = evs ? seismo_events(evs, MAXEV) : 0;
    if (e_sel >= n_evs) e_sel = 0;
    list_view_create(&e_lv, root);
    e_render(0);
}

static bool e_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (e_sel + 1 < n_evs) { e_sel++; e_render(+1); } return true;
    case NAV_PREV: if (e_sel > 0) { e_sel--; e_render(-1); } return true;
    case NAV_SELECT: return true;
    default: return false;
    }
}

static const char *e_title(void *arg) { return "Sismografo » Eventi"; }

static const app_t app_seismo_events = {
    .name = "Eventi", .icon = LV_SYMBOL_LIST,
    .enter = e_enter, .nav = e_nav, .title = e_title,
    .flags = APP_ROUND_OK,
};

/* ================= schermata del grafico ================= */

#define MAX_PTS 240
static lv_obj_t *l_now, *l_unit, *l_peak, *l_int, *l_state, *l_scale, *box, *line;
static lv_point_precise_t pts[MAX_PTS];
static int16_t vals[MAX_PTS];
static int n_pts, box_w, box_h;
static uint32_t cursor;
static lv_timer_t *tmr;
static bool ok;

static int nice_scale(int v01)   // fondo scala in 0,1 mg: 2, 5, 10, 20… mg
{
    static const int s[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 32000};
    for (unsigned i = 0; i < sizeof(s) / sizeof(s[0]); i++) if (v01 <= s[i]) return s[i];
    return 32000;
}

static void update(lv_timer_t *t)
{
    int16_t in[64];
    int n;
    while ((n = seismo_trace(in, 64, &cursor)) > 0) {   // i nuovi punti entrano da destra
        if (n >= n_pts) { memcpy(vals, in + n - n_pts, n_pts * 2); continue; }
        memmove(vals, vals + n, (n_pts - n) * 2);
        memcpy(vals + n_pts - n, in, n * 2);
    }
    int mx = 0;
    for (int i = 0; i < n_pts; i++) if (abs(vals[i]) > mx) mx = abs(vals[i]);
    int sc = nice_scale(mx * 6 / 5);
    float k = (box_h / 2 - 3) / (float)sc;
    for (int i = 0; i < n_pts; i++) pts[i].y = box_h / 2 - vals[i] * k;
    lv_line_set_points(line, pts, n_pts);

    char b[48], v[16];
    snprintf(b, sizeof(b), "±%d mg", sc / 10);
    ui_set_text(l_scale, b);
    fmt_mg(v, sizeof(v), seismo_now_mg());
    ui_set_text(l_now, v);
    fmt_mg(v, sizeof(v), seismo_peak_mg());
    snprintf(b, sizeof(b), "picco %s mg", v);
    ui_set_text(l_peak, b);
    snprintf(b, sizeof(b), "intensità %s", seismo_roman(seismo_mmi(seismo_peak_mg())));
    ui_set_text(l_int, b);

    int st = seismo_state();
    lv_color_t c = ui_accent();
    if (!ok) { snprintf(b, sizeof(b), "Accelerometro non disponibile"); c = C_WARN; }
    else if (st == SEISMO_WARMUP && seismo_settling()) { snprintf(b, sizeof(b), "Appoggia la scheda e lasciala ferma… %d s", seismo_warmup_left()); c = C_DIM; }
    else if (st == SEISMO_WARMUP) { snprintf(b, sizeof(b), "Calibrazione… %d s, non toccare", seismo_warmup_left()); c = C_DIM; }
    else if (st == SEISMO_EVENT) { snprintf(b, sizeof(b), "EVENTO in corso"); c = C_WARN; }
    else if (seismo_writing()) snprintf(b, sizeof(b), "Salvataggio sulla microSD…");
    else {
        int e = seismo_session_events();
        snprintf(b, sizeof(b), "In ascolto · %d event%s", e, e == 1 ? "o" : "i");
    }
    ui_set_text(l_state, b);
    ui_set_text_color(l_state, c);
    lv_color_t lc = st == SEISMO_EVENT ? C_WARN : ui_accent();
    if (!lv_color_eq(lv_obj_get_style_line_color(line, 0), lc)) lv_obj_set_style_line_color(line, lc, 0);
}

static lv_obj_t *mkl(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void center(lv_obj_t *l, int w, int y)
{
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
}

static void enter(lv_obj_t *root, void *arg)
{
    cfg();
    bool r = SCR_ROUND;
    box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0B0E12), 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (r) {   // tondo: valore in alto, grafico al centro, il resto sotto
        box_w = 380; box_h = 150;
        lv_obj_set_size(box, box_w, box_h);
        lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 62);
    } else {   // 3.49: valori a sinistra, grafico a destra
        box_w = 462; box_h = 128;
        lv_obj_set_size(box, box_w, box_h);
        lv_obj_set_pos(box, SCR_W - box_w - 8, 8);
    }
    lv_obj_t *mid = lv_obj_create(box);   // lo zero
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, box_w, 1);
    lv_obj_set_pos(mid, 0, box_h / 2);
    lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mid, C_FAINT, 0);
    line = lv_line_create(box);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, ui_accent(), 0);
    n_pts = box_w / 2 > MAX_PTS ? MAX_PTS : box_w / 2;
    for (int i = 0; i < n_pts; i++) { pts[i].x = i * (box_w - 1) / (n_pts - 1); pts[i].y = box_h / 2; vals[i] = 0; }
    lv_line_set_points(line, pts, n_pts);
    l_scale = mkl(box, &font_s, C_DIM);
    lv_obj_set_pos(l_scale, 8, 4);

    l_now = mkl(root, &font_l, C_TEXT);
    l_unit = mkl(root, &font_s, C_DIM);
    lv_label_set_text(l_unit, "mg adesso");
    l_peak = mkl(root, &font_m, C_TEXT);
    l_int = mkl(root, &font_s, C_DIM);
    l_state = mkl(root, &font_s, C_DIM);
    if (r) {
        lv_obj_align(l_now, LV_ALIGN_TOP_MID, -30, 0);
        lv_obj_align(l_unit, LV_ALIGN_TOP_MID, 70, 18);
        center(l_peak, 360, 222);
        center(l_int, 360, 256);
        center(l_state, 340, 286);
        lv_label_set_long_mode(l_state, LV_LABEL_LONG_WRAP);
    } else {
        lv_obj_set_pos(l_now, 12, 2);
        lv_obj_set_pos(l_unit, 14, 40);
        lv_obj_set_pos(l_peak, 12, 62);
        lv_obj_set_pos(l_int, 14, 90);
        lv_obj_set_pos(l_state, 14, 112);
        lv_obj_set_width(l_state, SCR_W - box_w - 30);
        lv_label_set_long_mode(l_state, LV_LABEL_LONG_WRAP);
    }
    cursor = 0;
    ok = seismo_start();
    update(NULL);
    tmr = lv_timer_create(update, 100, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    seismo_stop();
}

static bool nav(nav_t ev)
{
    if (ev == NAV_BTN) { seismo_reset_peak(); ui_toast("Picco azzerato"); return true; }
    if (ev == NAV_SELECT) { ui_push(&app_seismo_events, NULL); return true; }
    return false;
}

const app_t app_seismo = {
    .name = "Sismografo", .icon = LV_SYMBOL_SHUFFLE,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_ROUND_OK,
};

/* ================= menu ================= */

static int ev_count = -1;
static uint32_t ev_count_t;

static void v_events(char *b, int n)
{
    // il registro si rilegge al massimo ogni 5 s (il menu si ridisegna ogni secondo)
    if (ev_count < 0 || lv_tick_get() - ev_count_t > 5000) {
        seismo_event_t *tmp = malloc(sizeof(seismo_event_t) * MAXEV);
        ev_count = tmp ? seismo_events(tmp, MAXEV) : 0;
        free(tmp);
        ev_count_t = lv_tick_get();
    }
    if (!sd_ok()) snprintf(b, n, "Manca la microSD");
    else snprintf(b, n, "%d%s", ev_count, ev_count >= MAXEV ? " (gli ultimi)" : "");
}
static void v_sens(char *b, int n)
{
    cfg();
    static const char *const d[] = {"anche vibrazioni deboli", "colpi e passaggi vicini", "solo scosse forti"};
    snprintf(b, n, "%s · %s", seismo_sens_name(seismo_cfg.sens), d[seismo_cfg.sens % 3]);
}
static void j_sens(int d) { cfg(); int v = seismo_cfg.sens + d; seismo_cfg.sens = v < 0 ? 0 : v > 2 ? 2 : v; seismo_cfg_save(); }
static void v_rec(char *b, int n)
{
    cfg();
    if (!seismo_cfg.record) snprintf(b, n, "No");
    else snprintf(b, n, "%s", sd_ok() ? "Sì · cartella sismo della microSD" : "Sì, ma manca la microSD");
}
static void a_rec(void) { cfg(); seismo_cfg.record = !seismo_cfg.record; seismo_cfg_save(); }
static void a_clear(void)
{
    if (seismo_clear_events()) ui_toast("Eventi cancellati");
    else ui_toast("Niente da cancellare");
    ev_count = -1;
}

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Avvia", .hint = "Appoggia il Gadget su una superficie ferma", .app = &app_seismo},
    {.icon = LV_SYMBOL_LIST, .label = "Eventi registrati", .value = v_events, .app = &app_seismo_events},
    {.icon = ICON_SLIDERS, .label = "Sensibilità", .value = v_sens, .on_adjust = j_sens},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Registra sulla microSD", .value = v_rec, .on_select = a_rec},
    {.icon = LV_SYMBOL_TRASH, .label = "Cancella tutti gli eventi", .hint = "Registro e file sulla microSD", .on_select = a_clear, .confirm = true},
};

menu_t seismo_menu = {"Sismografo", items, sizeof(items) / sizeof(items[0]), 0, NULL};
