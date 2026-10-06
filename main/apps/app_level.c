// app_level.c — Livella digitale con l'accelerometro.
//
// Tre modi, scelti da come tieni la scheda:
//  - appoggiata (schermo in su): bolla circolare, inclinazione X e Y
//  - in piedi sul lato lungo:     tubo orizzontale, una sola inclinazione
//  - in piedi sul lato corto:     filo a piombo
// L'orientamento dell'accelerometro rispetto allo schermo non è documentato, quindi si
// misura una volta con la calibrazione guidata (piatta, poi in piedi): da lì si ricavano
// gli assi dello schermo in modo esatto. "Azzera" fissa lo zero sulla superficie attuale.
#include "apps.h"
#include "board.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"

#define RAD2DEG   57.29578f
#define SAMPLE_MS 20
#define UI_EVERY  3          // aggiorna lo schermo ogni 3 campioni (~16 volte al secondo)
#define FULL_DEG  10.0f      // inclinazione a fondo scala della bolla
#define LEVEL_DEG 0.25f      // sotto questa soglia è "in bolla"
#define CAL_MAGIC 0x314C564Cu   // "LVL1"

typedef struct {
    uint32_t magic;
    float ax[3], ay[3], az[3];   // assi dello schermo (x destra, y giù, z uscente) visti dall'IMU
    float zero[4];               // zeri: piatta X, piatta Y, lato lungo, lato corto
    uint8_t flipped;             // rotazione dello schermo alla calibrazione
} level_cal_t;

enum { MODE_FLAT, MODE_EDGE, MODE_PLUMB };
enum { ST_RUN, ST_CAL_FLAT, ST_CAL_EDGE };
enum { U_DEG, U_PCT, U_MMM, U_COUNT };

static level_cal_t cal;
static int state, mode = -1, pending_mode = -1, unit;
static uint32_t pending_since;
static vec3_t lp, cal_flat;
static float motion = 1;
static bool held, have_lp;
static float ang[2];                 // angoli correnti del modo (gradi, già azzerati)
static int ui_div;
static int laid = -2;                // disposizione attuale (per non rifarla a ogni giro)
static lv_timer_t *tmr;

static lv_obj_t *vial, *ring, *cross_h, *cross_v, *tube, *mark_l, *mark_r, *bubble;
static lv_obj_t *l_big, *l_unit, *l_x, *l_y, *l_mode, *l_hint;

/* ---------------- vettori ---------------- */

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static bool norm(float v[3])
{
    float n = sqrtf(dot(v, v));
    if (n < 1e-6f) return false;
    v[0] /= n; v[1] /= n; v[2] /= n;
    return true;
}

static void cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* ---------------- calibrazione salvata ---------------- */

static void cal_load(void)
{
    memset(&cal, 0, sizeof(cal));
    nvs_handle_t h;
    if (nvs_open("level", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(cal);
    if (nvs_get_blob(h, "cal", &cal, &len) != ESP_OK || len != sizeof(cal) || cal.magic != CAL_MAGIC) memset(&cal, 0, sizeof(cal));
    nvs_close(h);
}

static void cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open("level", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cal", &cal, sizeof(cal));
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- misura ---------------- */

// accelerazione filtrata, nelle coordinate dello schermo (normalizzata)
static bool screen_vec(float v[3])
{
    float a[3] = {lp.x, lp.y, lp.z};
    if (!norm(a)) return false;
    v[0] = dot(cal.ax, a);
    v[1] = dot(cal.ay, a);
    v[2] = dot(cal.az, a);
    if (cal.flipped != g_set.flipped) { v[0] = -v[0]; v[1] = -v[1]; }   // schermo ruotato dopo la calibrazione
    return true;
}

static int mode_of(const float v[3])
{
    if (fabsf(v[2]) >= 0.80f) return MODE_FLAT;
    return fabsf(v[1]) >= fabsf(v[0]) ? MODE_EDGE : MODE_PLUMB;
}

// angoli "grezzi" del modo, senza zero. Positivo = lato destro (o in basso) più alto:
// la bolla va verso il lato più alto, come in una livella vera.
static void raw_angles(int m, const float v[3], float out[2])
{
    switch (m) {
    case MODE_FLAT:
        out[0] = atan2f(v[0], fabsf(v[2])) * RAD2DEG;
        out[1] = atan2f(v[1], fabsf(v[2])) * RAD2DEG;
        break;
    case MODE_EDGE:   // in piedi sul lato lungo (anche capovolta)
        out[0] = atan2f(v[1] < 0 ? v[0] : -v[0], fabsf(v[1])) * RAD2DEG;
        out[1] = 0;
        break;
    default:          // in piedi sul lato corto: scostamento dalla verticale
        out[0] = atan2f(v[0] < 0 ? v[1] : -v[1], fabsf(v[0])) * RAD2DEG;
        out[1] = 0;
        break;
    }
}

static float zero_of(int m, int i)
{
    if (m == MODE_FLAT) return cal.zero[i];
    return cal.zero[m == MODE_EDGE ? 2 : 3];
}

/* ---------------- interfaccia ---------------- */

static lv_obj_t *box(lv_obj_t *p, int w, int h, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void show(lv_obj_t *o, bool on)
{
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool set_text(lv_obj_t *l, const char *s)
{
    if (!strcmp(lv_label_get_text(l), s)) return false;
    lv_label_set_text(l, s);
    return true;
}

static void set_color(lv_obj_t *o, lv_color_t c, bool bg)
{
    lv_color_t cur = bg ? lv_obj_get_style_bg_color(o, 0) : lv_obj_get_style_text_color(o, 0);
    if (lv_color_eq(cur, c)) return;
    if (bg) lv_obj_set_style_bg_color(o, c, 0);
    else lv_obj_set_style_text_color(o, c, 0);
}

static void move(lv_obj_t *o, int x, int y)
{
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}

static const char *mode_name(int m)
{
    return m == MODE_FLAT ? "Appoggiata" : m == MODE_EDGE ? "Sul lato lungo" : "Filo a piombo";
}

// disposizione degli oggetti per il modo (MODE_* o -1 = calibrazione)
static void layout(int m)
{
    if (m == laid) return;
    laid = m;
    bool flat = m == MODE_FLAT, line = m == MODE_EDGE || m == MODE_PLUMB;
    show(vial, flat); show(ring, flat); show(cross_h, flat); show(cross_v, flat);
    show(l_x, flat); show(l_y, flat);
    show(tube, line); show(mark_l, line); show(mark_r, line);
    show(l_big, !flat); show(l_unit, line);
    show(bubble, m >= 0);
    if (flat) {
        lv_obj_set_size(bubble, 30, 30);
        lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
        move(l_mode, 210, 112);
        move(l_hint, 210, 142);
        lv_obj_set_width(l_hint, SCR_W - 224);
    } else if (line) {
        lv_obj_set_size(bubble, 78, 34);
        lv_obj_set_style_radius(bubble, 17, 0);
        move(l_big, 24, 78);
        move(l_mode, 330, 82);
        move(l_hint, 330, 112);
        lv_obj_set_width(l_hint, SCR_W - 344);
    } else {   // calibrazione: numero del passo a sinistra, istruzioni accanto
        move(l_big, 34, 40);
        move(l_mode, 130, 40);
        move(l_hint, 130, 76);
        lv_obj_set_width(l_hint, SCR_W - 150);
    }
}

// valore nell'unità scelta; i valori che si arrotondano a zero diventano 0 (niente "-0.0")
static void fmt(float deg, char *b, int n, bool with_unit)
{
    float t = tanf(deg / RAD2DEG);
    static const char *const u[] = {"°", "%", " mm/m"};
    float x = unit == U_PCT ? t * 100 : unit == U_MMM ? t * 1000 : deg;
    float thr = unit == U_MMM ? 0.5f : 0.05f;
    if (fabsf(x) < thr) x = 0;
    snprintf(b, n, unit == U_MMM ? "%.0f%s" : "%.1f%s", x, with_unit ? u[unit] : "");
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static void render(void)
{
    char t[40];
    if (state != ST_RUN) {
        layout(-1);
        set_text(l_big, state == ST_CAL_FLAT ? "1" : "2");
        set_color(l_big, ui_accent(), false);
        set_text(l_mode, state == ST_CAL_FLAT ? "Calibrazione 1 di 2" : "Calibrazione 2 di 2");
        set_color(l_mode, ui_accent(), false);
        set_text(l_hint, state == ST_CAL_FLAT
                 ? "Appoggia la scheda su un piano, schermo in su, e tienila ferma. Poi swipe a destra"
                 : "Ora mettila in piedi sul lato lungo in basso, schermo verso di te. Poi swipe a destra");
        return;
    }
    if (mode < 0) return;
    bool level = fabsf(ang[0]) < LEVEL_DEG && fabsf(ang[1]) < LEVEL_DEG;
    set_color(bubble, level ? C_OK : ui_accent(), true);
    set_text(l_mode, held ? LV_SYMBOL_PAUSE " Bloccata" : mode_name(mode));
    set_color(l_mode, held ? C_WARN : ui_accent(), false);

    if (mode == MODE_FLAT) {
        const int cx = 100, cy = 86, r = 78 - 15;
        int bx = cx + (int)lroundf(clampf(ang[0] / FULL_DEG, -1, 1) * r);
        int by = cy + (int)lroundf(clampf(ang[1] / FULL_DEG, -1, 1) * r);
        move(bubble, bx - 15, by - 15);
        char a[20];
        fmt(ang[0], a, sizeof(a), true); snprintf(t, sizeof(t), "X  %s", a); set_text(l_x, t);
        fmt(ang[1], a, sizeof(a), true); snprintf(t, sizeof(t), "Y  %s", a); set_text(l_y, t);
        set_color(l_x, fabsf(ang[0]) < LEVEL_DEG ? C_OK : C_TEXT, false);
        set_color(l_y, fabsf(ang[1]) < LEVEL_DEG ? C_OK : C_TEXT, false);
    } else {
        const int x0 = 20, w = SCR_W - 40, half = w / 2 - 39 - 4;
        int bx = x0 + w / 2 + (int)lroundf(clampf(ang[0] / FULL_DEG, -1, 1) * half);
        move(bubble, bx - 39, 22);
        fmt(ang[0], t, sizeof(t), false);
        static const char *const u[] = {"°", "%", "mm/m"};
        bool moved = set_text(l_big, t);
        moved |= set_text(l_unit, u[unit]);
        if (moved) lv_obj_align_to(l_unit, l_big, LV_ALIGN_OUT_RIGHT_TOP, 6, 6);
        set_color(l_big, level ? C_OK : C_TEXT, false);
    }
    set_text(l_hint, held ? "Lettura bloccata · BOOT per sbloccare"
                          : "Destra: azzera · su/giù: unità · BOOT: blocca · tieni premuto: ricalibra");
}

/* ---------------- campionamento ---------------- */

static void sample_cb(lv_timer_t *t)
{
    vec3_t g;
    if (!board_imu_accel(&g)) return;
    if (!have_lp) { lp = g; have_lp = true; }
    float d = fabsf(g.x - lp.x) + fabsf(g.y - lp.y) + fabsf(g.z - lp.z);
    motion += (d - motion) * 0.1f;
    // filtro più rapido quando si muove, più fermo quando è quasi immobile (numeri stabili)
    float k = motion > 0.05f ? 0.35f : 0.08f;
    lp.x += (g.x - lp.x) * k;
    lp.y += (g.y - lp.y) * k;
    lp.z += (g.z - lp.z) * k;

    if (state == ST_RUN && !held) {
        float v[3];
        if (screen_vec(v)) {
            int m = mode_of(v);
            uint32_t now = lv_tick_get();
            // cambio di modo solo se dura un attimo (niente sfarfallio a 45°)
            if (m != mode) {
                if (m != pending_mode) { pending_mode = m; pending_since = now; }
                else if (now - pending_since > 300 || mode < 0) { mode = m; pending_mode = -1; layout(m); }
            } else pending_mode = -1;
            if (mode >= 0) {
                float raw[2];
                raw_angles(mode, v, raw);
                ang[0] = raw[0] - zero_of(mode, 0);
                ang[1] = mode == MODE_FLAT ? raw[1] - zero_of(mode, 1) : 0;
            }
        }
    }
    if (++ui_div >= UI_EVERY) { ui_div = 0; render(); }
}

/* ---------------- azioni ---------------- */

static bool still(void) { return motion < 0.012f; }

static void cal_step(void)
{
    float a[3] = {lp.x, lp.y, lp.z};
    float mag = sqrtf(dot(a, a));
    if (!still() || mag < 0.85f || mag > 1.15f) { ui_toast("Tienila ferma un attimo"); return; }
    if (state == ST_CAL_FLAT) {
        cal_flat = lp;
        state = ST_CAL_EDGE;
        return;
    }
    // ST_CAL_EDGE: z = verso l'alto da piatta (esce dallo schermo); da in piedi la
    // gravità misurata punta verso l'alto, cioè -y dello schermo
    float z[3] = {cal_flat.x, cal_flat.y, cal_flat.z}, up[3] = {a[0], a[1], a[2]};
    norm(z);
    norm(up);
    float dz = dot(up, z);
    if (fabsf(dz) > 0.3f) { ui_toast("Non sembra in piedi sul lato: riprova"); return; }
    float y[3] = {-(up[0] - dz * z[0]), -(up[1] - dz * z[1]), -(up[2] - dz * z[2])}, x[3];
    norm(y);
    cross(z, y, x);   // x = z × y: destra dello schermo
    cal.magic = CAL_MAGIC;
    memcpy(cal.ax, x, sizeof(x));
    memcpy(cal.ay, y, sizeof(y));
    memcpy(cal.az, z, sizeof(z));
    memset(cal.zero, 0, sizeof(cal.zero));
    cal.flipped = g_set.flipped;
    cal_save();
    state = ST_RUN;
    mode = pending_mode = -1;
    ui_toast("Livella calibrata");
}

static void set_zero(void)
{
    float v[3], raw[2];
    if (mode < 0 || !screen_vec(v)) return;
    raw_angles(mode, v, raw);
    if (mode == MODE_FLAT) { cal.zero[0] = raw[0]; cal.zero[1] = raw[1]; }
    else cal.zero[mode == MODE_EDGE ? 2 : 3] = raw[0];
    cal_save();
    ui_toast("Zero impostato su questa superficie");
}

static bool nav(nav_t ev)
{
    if (state != ST_RUN) {
        if (ev == NAV_SELECT) { cal_step(); render(); return true; }
        if (ev == NAV_BACK || ev == NAV_BTN) {
            if (cal.magic != CAL_MAGIC) return false;   // mai calibrata: si esce
            state = ST_RUN;
            mode = pending_mode = -1;
            return true;
        }
        return true;
    }
    switch (ev) {
    case NAV_SELECT: if (!held) set_zero(); return true;
    case NAV_NEXT:   unit = (unit + 1) % U_COUNT; render(); return true;
    case NAV_PREV:   unit = (unit + U_COUNT - 1) % U_COUNT; render(); return true;
    case NAV_BTN:    held = !held; render(); return true;
    case NAV_QUICK:  held = false; state = ST_CAL_FLAT; render(); return true;
    default:         return false;
    }
}

/* ---------------- ciclo di vita ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    laid = -2;   // oggetti nuovi: la disposizione va rifatta

    vial = box(root, 156, 156, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(vial, 22, 8);
    lv_obj_set_style_border_width(vial, 3, 0);
    lv_obj_set_style_border_color(vial, C_DIM, 0);
    ring = box(root, 38, 38, LV_RADIUS_CIRCLE);   // bolla centrata: dentro questo cerchio
    lv_obj_set_pos(ring, 100 - 19, 86 - 19);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, C_FAINT, 0);
    cross_h = box(root, 150, 1, 0);
    lv_obj_set_pos(cross_h, 25, 86);
    cross_v = box(root, 1, 150, 0);
    lv_obj_set_pos(cross_v, 100, 11);
    lv_obj_set_style_bg_color(cross_h, C_FAINT, 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cross_v, C_FAINT, 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);

    tube = box(root, SCR_W - 40, 50, 25);
    lv_obj_set_pos(tube, 20, 14);
    lv_obj_set_style_border_width(tube, 3, 0);
    lv_obj_set_style_border_color(tube, C_DIM, 0);
    mark_l = box(root, 2, 50, 0);
    mark_r = box(root, 2, 50, 0);
    lv_obj_set_pos(mark_l, SCR_W / 2 - 43, 14);
    lv_obj_set_pos(mark_r, SCR_W / 2 + 41, 14);
    lv_obj_set_style_bg_color(mark_l, C_DIM, 0);
    lv_obj_set_style_bg_opa(mark_l, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mark_r, C_DIM, 0);
    lv_obj_set_style_bg_opa(mark_r, LV_OPA_COVER, 0);

    bubble = box(root, 30, 30, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, ui_accent(), 0);

    l_big = label(root, &font_xl, C_TEXT);
    l_unit = label(root, &font_l, C_DIM);
    l_x = label(root, &font_l, C_TEXT);
    lv_obj_set_pos(l_x, 210, 20);
    l_y = label(root, &font_l, C_TEXT);
    lv_obj_set_pos(l_y, 210, 62);
    l_mode = label(root, &font_m, ui_accent());
    l_hint = label(root, &font_s, C_DIM);
    lv_label_set_long_mode(l_hint, LV_LABEL_LONG_WRAP);
    layout(-1);

    cal_load();
    state = cal.magic == CAL_MAGIC ? ST_RUN : ST_CAL_FLAT;
    mode = pending_mode = -1;
    held = have_lp = false;
    motion = 1;
    ui_div = 0;
    if (!board_imu_ok()) {
        set_text(l_mode, "Accelerometro non disponibile");
        return;
    }
    tmr = lv_timer_create(sample_cb, SAMPLE_MS, NULL);
    render();
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
}

const app_t app_level = {
    .name = "Livella", .icon = ICON_SLIDERS,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};
