// app_theremin.c — Theremin: inclinando la scheda avanti/indietro cambia la nota, ruotandola
// a destra/sinistra il volume (o vibrato o timbro). Suona tenendo il dito sulla zona grande
// a sinistra (o sempre, dal menu). BOOT = nuova posizione zero.
// A destra i pulsanti: Suono, Scala, Effetti, Comandi, ● Registra, Microfono, Registrazioni.
#include "apps.h"
#include "board.h"
#include "theremin.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define RAD2DEG   57.29578f
#define PAD_W     428                  // zona da suonare (a sinistra)
#define COL_X     (PAD_W + 8)          // colonna dei pulsanti
#define NB        7
#define RELEASE_MS 70

static bool cfg_loaded, dirty;   // impostazioni da salvare (cambiate nei menu)

/* ================= menu delle impostazioni ================= */

static void touch_cfg(void) { dirty = true; }

static void v_wave(char *b, int n) { snprintf(b, n, "%s", th_wave_name(th_cfg.wave)); }
static void j_wave(int d) { th_cfg.wave = (th_cfg.wave + d + TH_WAVE_COUNT) % TH_WAVE_COUNT; touch_cfg(); }
static void v_oct(char *b, int n) { snprintf(b, n, "Zero su La%d", 4 + th_cfg.octave); }
static void j_oct(int d) { int v = th_cfg.octave + d; th_cfg.octave = v < -2 ? -2 : v > 2 ? 2 : v; touch_cfg(); }
static void v_span(char *b, int n) { snprintf(b, n, "%d ottav%s", th_cfg.span, th_cfg.span == 1 ? "a" : "e"); }
static void j_span(int d) { int v = th_cfg.span + d; th_cfg.span = v < 1 ? 1 : v > 3 ? 3 : v; touch_cfg(); }
static void v_glide(char *b, int n)
{
    if (th_cfg.glide) snprintf(b, n, "%d ms", th_glide_ms(th_cfg.glide));
    else snprintf(b, n, "Nessuno (salta di nota)");
}
static void j_glide(int d) { int v = th_cfg.glide + d; th_cfg.glide = v < 0 ? 0 : v > 4 ? 4 : v; touch_cfg(); }
static void v_vib(char *b, int n)
{
    if (th_cfg.vib_depth) snprintf(b, n, "%d%% di semitono", th_cfg.vib_depth * 10);
    else snprintf(b, n, "Spento");
}
static void j_vib(int d) { int v = th_cfg.vib_depth + d; th_cfg.vib_depth = v < 0 ? 0 : v > 10 ? 10 : v; touch_cfg(); }
static void v_vrate(char *b, int n) { snprintf(b, n, "%d Hz", th_cfg.vib_rate); }
static void j_vrate(int d) { int v = th_cfg.vib_rate + d; th_cfg.vib_rate = v < 3 ? 3 : v > 8 ? 8 : v; touch_cfg(); }
static void v_level(char *b, int n) { snprintf(b, n, "%d%%", th_cfg.level); }
static void j_level(int d) { int v = th_cfg.level + d * 10; th_cfg.level = v < 10 ? 10 : v > 100 ? 100 : v; touch_cfg(); }

static const menu_item_t sound_items[] = {
    {.icon = ICON_TUNER, .label = "Forma d'onda", .value = v_wave, .on_adjust = j_wave},
    {.icon = LV_SYMBOL_UP, .label = "Ottava", .value = v_oct, .on_adjust = j_oct},
    {.icon = ICON_SLIDERS, .label = "Estensione", .value = v_span, .on_adjust = j_span},
    {.icon = LV_SYMBOL_LOOP, .label = "Glide (scivolata)", .value = v_glide, .on_adjust = j_glide},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Vibrato", .value = v_vib, .on_adjust = j_vib},
    {.icon = ICON_CLOCK, .label = "Velocità del vibrato", .value = v_vrate, .on_adjust = j_vrate},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Volume", .value = v_level, .on_adjust = j_level},
};
static menu_t sound_menu = {"Theremin » Suono", sound_items, sizeof(sound_items) / sizeof(sound_items[0]), 0, NULL};

static void v_scale(char *b, int n) { snprintf(b, n, "%s", th_scale_name(th_cfg.scale)); }
static void j_scale(int d) { th_cfg.scale = (th_cfg.scale + d + TH_SCALE_COUNT) % TH_SCALE_COUNT; touch_cfg(); }
static void v_root(char *b, int n)
{
    snprintf(b, n, "%s%s", th_note_name(th_cfg.root), th_cfg.scale <= TH_SCALE_CHROMATIC ? " (vale per le scale)" : "");
}
static void j_root(int d) { th_cfg.root = (th_cfg.root + d + 12) % 12; touch_cfg(); }
static void v_a4(char *b, int n) { snprintf(b, n, "%d,%d Hz (come l'Accordatore)", g_set.a4_x10 / 10, g_set.a4_x10 % 10); }

static const menu_item_t scale_items[] = {
    {.icon = LV_SYMBOL_LIST, .label = "Scala", .value = v_scale, .on_adjust = j_scale},
    {.icon = ICON_TUNER, .label = "Tonica", .value = v_root, .on_adjust = j_root},
    {.icon = ICON_INFO, .label = "LA di riferimento", .value = v_a4},
};
static menu_t scale_menu = {"Theremin » Scala", scale_items, sizeof(scale_items) / sizeof(scale_items[0]), 0, NULL};

static void v_echo(char *b, int n)
{
    static const char *const e[] = {"Spenta", "Corta", "Media", "Lunga"};
    snprintf(b, n, "%s", e[th_cfg.echo]);
}
static void j_echo(int d) { th_cfg.echo = (th_cfg.echo + d + 4) % 4; touch_cfg(); }
static void v_fb(char *b, int n)
{
    static const char *const e[] = {"Poche", "Medie", "Molte", "Moltissime"};
    snprintf(b, n, "%s", e[th_cfg.echo_fb]);
}
static void j_fb(int d) { int v = th_cfg.echo_fb + d; th_cfg.echo_fb = v < 0 ? 0 : v > 3 ? 3 : v; touch_cfg(); }
static void v_tone(char *b, int n)
{
    static const char *const t[] = {"Scuro", "Morbido", "Medio", "Chiaro", "Brillante"};
    snprintf(b, n, "%s", t[th_cfg.tone]);
}
static void j_tone(int d) { int v = th_cfg.tone + d; th_cfg.tone = v < 0 ? 0 : v > 4 ? 4 : v; touch_cfg(); }

static const menu_item_t fx_items[] = {
    {.icon = LV_SYMBOL_LOOP, .label = "Eco", .value = v_echo, .on_adjust = j_echo},
    {.icon = LV_SYMBOL_REFRESH, .label = "Ripetizioni dell'eco", .value = v_fb, .on_adjust = j_fb},
    {.icon = ICON_SUN, .label = "Timbro", .value = v_tone, .on_adjust = j_tone},
};
static menu_t fx_menu = {"Theremin » Effetti", fx_items, sizeof(fx_items) / sizeof(fx_items[0]), 0, NULL};

static void v_roll(char *b, int n) { snprintf(b, n, "%s", th_roll_name(th_cfg.roll_fn)); }
static void j_roll(int d) { th_cfg.roll_fn = (th_cfg.roll_fn + d + TH_ROLL_COUNT) % TH_ROLL_COUNT; touch_cfg(); }
static void v_sens(char *b, int n)
{
    static const char *const s[] = {"Bassa", "Media", "Alta"};
    snprintf(b, n, "%s (%d° per tutta l'estensione)", s[th_cfg.sens], th_sens_deg(th_cfg.sens));
}
static void j_sens(int d) { int v = th_cfg.sens + d; th_cfg.sens = v < 0 ? 0 : v > 2 ? 2 : v; touch_cfg(); }
static void v_invp(char *b, int n) { snprintf(b, n, "%s", th_cfg.inv_pitch ? "Sì" : "No"); }
static void a_invp(void) { th_cfg.inv_pitch = !th_cfg.inv_pitch; touch_cfg(); }
static void v_invr(char *b, int n) { snprintf(b, n, "%s", th_cfg.inv_roll ? "Sì" : "No"); }
static void a_invr(void) { th_cfg.inv_roll = !th_cfg.inv_roll; touch_cfg(); }
static void v_always(char *b, int n) { snprintf(b, n, "%s", th_cfg.always ? "Sempre" : "Tenendo il dito sullo schermo"); }
static void a_always(void) { th_cfg.always = !th_cfg.always; touch_cfg(); }
static void v_micg(char *b, int n) { snprintf(b, n, "%d di 4", th_cfg.mic_gain); }
static void j_micg(int d) { int v = th_cfg.mic_gain + d; th_cfg.mic_gain = v < 1 ? 1 : v > 4 ? 4 : v; touch_cfg(); }

static const menu_item_t ctl_items[] = {
    {.icon = LV_SYMBOL_REFRESH, .label = "La rotazione controlla", .value = v_roll, .on_adjust = j_roll},
    {.icon = ICON_SLIDERS, .label = "Sensibilità", .value = v_sens, .on_adjust = j_sens},
    {.icon = LV_SYMBOL_UP, .label = "Inverti la nota", .value = v_invp, .on_select = a_invp},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Inverti la rotazione", .value = v_invr, .on_select = a_invr},
    {.icon = LV_SYMBOL_PLAY, .label = "Suona", .value = v_always, .on_select = a_always},
    {.icon = ICON_MIC, .label = "Volume del microfono", .value = v_micg, .on_adjust = j_micg},
};
static menu_t ctl_menu = {"Theremin » Comandi", ctl_items, sizeof(ctl_items) / sizeof(ctl_items[0]), 0, NULL};

/* ================= registrazioni ================= */

#define MAXREC 64
static char (*recs)[40];
static int n_recs, r_sel;
static uint32_t r_del_until;
static list_view_t r_lv;
static char r_rows[3][48], r_title[48], r_playing[40];

// "20261006-193012.wav" → "06/10/2026 19:30:12"
static const char *pretty(int i, char *b)
{
    if (i < 0 || i >= n_recs) return NULL;
    const char *s = recs[i];
    if (strlen(s) >= 19 && s[8] == '-')
        snprintf(b, 48, "%.2s/%.2s/%.4s %.2s:%.2s:%.2s", s + 6, s + 4, s, s + 9, s + 11, s + 13);
    else snprintf(b, 48, "%s", s);
    return b;
}

static void r_reload(void)
{
    if (!recs) recs = heap_caps_malloc(MAXREC * 40, MALLOC_CAP_SPIRAM);
    n_recs = recs ? th_list(recs, MAXREC) : 0;
    if (r_sel >= n_recs) r_sel = n_recs ? n_recs - 1 : 0;
}

static void r_render(int dir)
{
    if (!n_recs) {
        list_view_set(&r_lv, ICON_MIC, NULL, "Nessuna registrazione",
                      "Nel Theremin tocca Registra · i file vanno nella cartella theremin della microSD",
                      NULL, 0, 0, dir);
        return;
    }
    char sub[96];
    uint32_t len = th_file_ms(recs[r_sel]);
    bool del = r_del_until && (int32_t)(lv_tick_get() - r_del_until) < 0;
    if (del) snprintf(sub, sizeof(sub), "BOOT di nuovo per cancellarla");
    else if (th_playing() && !strcmp(r_playing, recs[r_sel])) {
        uint32_t p = th_play_ms();
        snprintf(sub, sizeof(sub), LV_SYMBOL_PLAY " %lu:%02lu / %lu:%02lu · destra: ferma",
                 (unsigned long)(p / 60000), (unsigned long)(p / 1000 % 60),
                 (unsigned long)(len / 60000), (unsigned long)(len / 1000 % 60));
    } else
        snprintf(sub, sizeof(sub), "%lu:%02lu · destra: ascolta · BOOT: cancella",
                 (unsigned long)(len / 60000), (unsigned long)(len / 1000 % 60));
    list_view_set(&r_lv, ICON_MIC, pretty(r_sel - 1, r_rows[0]), pretty(r_sel, r_rows[1]), sub,
                  pretty(r_sel + 1, r_rows[2]), r_sel, n_recs, dir);
    ui_set_text_color(r_lv.sub, del ? C_WARN : C_DIM);
}

static void r_enter(lv_obj_t *root, void *arg)
{
    list_view_create(&r_lv, root);
    r_del_until = 0;
    r_playing[0] = 0;
    r_reload();
    r_render(0);
}

static void r_leave(void) { th_play_stop(); }

static void r_tick(void)
{
    if (r_del_until && (int32_t)(lv_tick_get() - r_del_until) >= 0) r_del_until = 0;
    if (r_playing[0] && !th_playing()) { th_play_stop(); r_playing[0] = 0; }
    r_render(0);   // list_view_set ridisegna solo ciò che cambia (avanzamento)
}

static bool r_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (r_sel + 1 < n_recs) { r_sel++; r_del_until = 0; r_render(+1); } return true;
    case NAV_PREV: if (r_sel > 0) { r_sel--; r_del_until = 0; r_render(-1); } return true;
    case NAV_SELECT:
        if (!n_recs) return true;
        if (th_playing() && !strcmp(r_playing, recs[r_sel])) { th_play_stop(); r_playing[0] = 0; }
        else if (th_play(recs[r_sel])) snprintf(r_playing, sizeof(r_playing), "%s", recs[r_sel]);
        else ui_toast("Non riesco ad aprire il file");
        r_render(0);
        return true;
    case NAV_BTN:
        if (!n_recs) return false;
        if (!r_del_until) { r_del_until = lv_tick_get() + 4000; r_render(0); return true; }
        r_del_until = 0;
        if (!strcmp(r_playing, recs[r_sel])) { th_play_stop(); r_playing[0] = 0; }
        if (th_delete(recs[r_sel])) ui_toast("Registrazione cancellata");
        r_reload();
        r_render(0);
        return true;
    default:
        return false;
    }
}

static const char *r_titlef(void *arg)
{
    snprintf(r_title, sizeof(r_title), "Theremin » Registrazioni · %d", n_recs);
    return r_title;
}

static const app_t app_th_recs = {
    .name = "Registrazioni", .icon = ICON_MIC,
    .enter = r_enter, .leave = r_leave, .nav = r_nav, .tick = r_tick, .title = r_titlef,
    .flags = APP_NO_SLEEP,
};

/* ================= schermata principale ================= */

enum { B_SOUND, B_SCALE, B_FX, B_CTL, B_REC, B_MIC, B_LIST };
static const struct { int x, y, w; const char *txt; } BTN[NB] = {
    {COL_X, 4, 96, "Suono"},        {COL_X + 100, 4, 96, "Scala"},
    {COL_X, 46, 96, "Effetti"},     {COL_X + 100, 46, 96, "Comandi"},
    {COL_X, 88, 96, ""},            {COL_X + 100, 88, 96, ""},
    {COL_X, 130, 196, "Registrazioni"},
};
#define BTN_H 38

static lv_obj_t *pad, *l_note, *l_cents, *l_hz, *track, *marker, *rtrack, *rfill, *l_roll, *l_hint;
static lv_obj_t *b_obj[NB], *b_lbl[NB];
static lv_timer_t *tmr, *stmr;   // tocco, sensori
static bool ok;
// inclinazione
static vec3_t g_f, rest;
static bool long_is_x, have_g;
// tocco
static bool t_down, t_moved, t_pad;
static int t_sx, t_sy, t_btn = -1;
static uint32_t t_seen, pad_release;
static int ui_div;

static void btn_style(int i, bool down)
{
    bool rec = i == B_REC && th_recording(), mic = i == B_MIC && th_cfg.mic;
    lv_color_t bg = down ? ui_accent() : rec ? lv_color_hex(0x7A1A14) : mic ? lv_color_hex(0x1E3A2A) : lv_color_hex(0x1C2025);
    ui_set_bg_color(b_obj[i], bg);
    ui_set_text_color(b_lbl[i], down ? C_BG : rec ? lv_color_hex(0xFF8A80) : C_TEXT);
}

static void btn_labels(void)
{
    char t[24];
    if (th_recording()) {
        uint32_t ms = th_rec_ms();
        snprintf(t, sizeof(t), LV_SYMBOL_STOP " %lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
    } else snprintf(t, sizeof(t), "\xE2\x80\xA2 Registra");   // • (il pallino pieno non è nei font)
    ui_set_text(b_lbl[B_REC], t);
    ui_set_text(b_lbl[B_MIC], th_cfg.mic ? ICON_MIC " Mic: sì" : ICON_MIC " Mic: no");
    btn_style(B_REC, t_btn == B_REC);
    btn_style(B_MIC, t_btn == B_MIC);
}

static void calibrate(void)
{
    if (!have_g) return;
    rest = g_f;
    long_is_x = fabsf(rest.x) < fabsf(rest.y);   // l'asse lungo: quello che sente meno gravità
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// inclinazione (avanti/indietro) e rotazione (destra/sinistra) in gradi dalla posizione zero
static void angles(float *pitch, float *roll)
{
    float n = sqrtf(g_f.x * g_f.x + g_f.y * g_f.y + g_f.z * g_f.z);
    if (n < 0.01f) { *pitch = *roll = 0; return; }
    vec3_t g = {g_f.x / n, g_f.y / n, g_f.z / n};
    float rn = sqrtf(rest.x * rest.x + rest.y * rest.y + rest.z * rest.z);
    vec3_t r = {rest.x / rn, rest.y / rn, rest.z / rn};
    float gl = long_is_x ? g.x : g.y, rl = long_is_x ? r.x : r.y;
    float gs = long_is_x ? g.y : g.x, rs = long_is_x ? r.y : r.x;
    float ro = (asinf(clampf(gl, -1, 1)) - asinf(clampf(rl, -1, 1))) * RAD2DEG;
    float pt = (atan2f(gs, g.z) - atan2f(rs, r.z)) * RAD2DEG;
    if (pt > 180) pt -= 360;
    if (pt < -180) pt += 360;
    if (g_set.flipped) { ro = -ro; pt = -pt; }
    if (th_cfg.inv_pitch) pt = -pt;
    if (th_cfg.inv_roll) ro = -ro;
    *pitch = pt;
    *roll = ro;
}

static void render(float p, float r, bool gate)
{
    char t[32];
    int midi;
    float cents = th_note(&midi);
    // nome della nota in maiuscolo (il carattere grande ha solo maiuscole e cifre)
    const char *nm = th_note_name(midi);
    int o = 0;
    for (; nm[o] && o < 8; o++) t[o] = (nm[o] >= 'a' && nm[o] <= 'z') ? nm[o] - 32 : nm[o];
    snprintf(t + o, sizeof(t) - o, "%d", midi / 12 - 1);
    ui_set_text(l_note, t);
    ui_set_text_color(l_note, gate ? C_TEXT : C_DIM);
    snprintf(t, sizeof(t), "%+d cent", (int)lroundf(cents));
    ui_set_text(l_cents, th_cfg.scale == TH_SCALE_FREE ? t : th_scale_name(th_cfg.scale));
    ui_set_text_color(l_cents, fabsf(cents) < 8 ? C_OK : C_DIM);
    snprintf(t, sizeof(t), "%.1f Hz", th_freq());
    ui_set_text(l_hz, t);
    // segnaposto della nota sulla barra (tutta l'estensione)
    int x = 16 + (int)lroundf((p + 1) * 0.5f * (PAD_W - 32 - 8));
    if (lv_obj_get_x(marker) != x) lv_obj_set_x(marker, x);
    // rotazione
    int w = (int)lroundf((r + 1) * 0.5f * (PAD_W - 32));
    if (lv_obj_get_width(rfill) != w) lv_obj_set_width(rfill, w < 1 ? 1 : w);
    snprintf(t, sizeof(t), "Rotazione: %s", th_roll_name(th_cfg.roll_fn));
    ui_set_text(l_roll, t);
    ui_set_bg_color(pad, gate ? lv_color_hex(0x10161C) : C_BG);
    btn_labels();
}

static void sample_cb(lv_timer_t *tm)
{
    vec3_t a;
    if (board_imu_accel(&a)) {
        if (!have_g) { g_f = a; have_g = true; calibrate(); }
        g_f.x += (a.x - g_f.x) * 0.3f;
        g_f.y += (a.y - g_f.y) * 0.3f;
        g_f.z += (a.z - g_f.z) * 0.3f;
    }
    float pitch = 0, roll = 0;
    if (have_g) angles(&pitch, &roll);
    float half = th_sens_deg(th_cfg.sens) / 2.0f;
    float p = clampf(pitch / half, -1, 1), r = clampf(roll / half, -1, 1);
    bool gate = th_cfg.always || (t_down && t_pad);
    th_control(p, r, gate);
    if (gate) input_mark_activity();
    if (++ui_div >= 4) { ui_div = 0; render(p, r, gate); }   // ~12 volte al secondo
}

static void on_button(int i)
{
    if (i != B_REC && i != B_MIC && th_recording()) { ui_toast("Ferma prima la registrazione"); return; }
    // i menu sostituiscono la schermata (gli oggetti di questa vengono cancellati): return,
    // non break, perché btn_labels() qui sotto li toccherebbe dopo la cancellazione
    switch (i) {
    case B_SOUND: ui_push(&app_menu, &sound_menu); return;
    case B_SCALE: ui_push(&app_menu, &scale_menu); return;
    case B_FX:    ui_push(&app_menu, &fx_menu); return;
    case B_CTL:   ui_push(&app_menu, &ctl_menu); return;
    case B_LIST:  ui_push(&app_th_recs, NULL); return;
    case B_MIC:
        if (th_recording()) { ui_toast("Si sceglie prima di registrare"); break; }
        th_cfg.mic = !th_cfg.mic;
        dirty = true;
        ui_toast(th_cfg.mic ? "Registrazione con il microfono: puoi cantare mentre suoni" : "Registrazione del solo theremin");
        break;
    case B_REC:
        if (th_recording()) {
            th_rec_stop();
            char m[64];
            snprintf(m, sizeof(m), "Salvata: %s", th_rec_name());
            ui_toast(th_error()[0] ? th_error() : m);
        } else if (!th_rec_start()) ui_toast(th_error());
        break;
    }
    btn_labels();
}

static void touch_cb(lv_timer_t *tm)
{
    int x, y;
    uint32_t now = lv_tick_get();
    if (input_touch(&x, &y)) {
        t_seen = now;
        if (!t_down) {
            t_down = true; t_moved = false; t_sx = x; t_sy = y;
            t_pad = x < PAD_W;
            t_btn = -1;
            for (int i = 0; !t_pad && i < NB; i++)
                if (x >= BTN[i].x && x < BTN[i].x + BTN[i].w && y >= BTN[i].y && y < BTN[i].y + BTN_H) t_btn = i;
            if (t_btn >= 0) btn_style(t_btn, true);
            return;
        }
        if (abs(x - t_sx) > 26 || abs(y - t_sy) > 26) {
            t_moved = true;
            if (t_btn >= 0) { btn_style(t_btn, false); t_btn = -1; }
        }
        return;
    }
    if (!t_down || now - t_seen < RELEASE_MS) return;
    t_down = false;
    if (t_pad) pad_release = now;
    if (t_btn >= 0) {
        int b = t_btn;
        t_btn = -1;
        btn_style(b, false);
        if (!t_moved) on_button(b);   // può cambiare schermata
    }
}

static lv_obj_t *mkl(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void enter(lv_obj_t *root, void *arg)
{
    if (!cfg_loaded) { th_cfg_load(); cfg_loaded = true; }
    if (dirty) { th_cfg_save(); dirty = false; }   // di ritorno da un menu
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    pad = rect(root, 0, 0, PAD_W, SCR_H, C_BG, 0);
    l_note = mkl(pad, &font_xl, C_DIM, 16, 0);
    l_cents = mkl(pad, &font_m, C_DIM, 250, 14);
    l_hz = mkl(pad, &font_s, C_DIM, 250, 42);
    track = rect(pad, 16, 88, PAD_W - 32, 10, C_FAINT, 5);
    marker = rect(pad, 16, 84, 8, 18, ui_accent(), 3);
    rtrack = rect(pad, 16, 116, PAD_W - 32, 6, C_FAINT, 3);
    rfill = rect(pad, 16, 116, (PAD_W - 32) / 2, 6, lv_color_hex(0x3D6B8C), 3);
    l_roll = mkl(pad, &font_s, C_DIM, 16, 126);
    l_hint = mkl(pad, &font_s, C_DIM, 16, 150);
    lv_label_set_text(l_hint, th_cfg.always ? "Inclina per la nota · BOOT: posizione zero"
                                            : "Tieni il dito qui per suonare · BOOT: posizione zero");
    for (int i = 0; i < NB; i++) {
        b_obj[i] = rect(root, BTN[i].x, BTN[i].y, BTN[i].w, BTN_H, lv_color_hex(0x1C2025), 8);
        b_lbl[i] = lv_label_create(b_obj[i]);
        lv_obj_set_style_text_font(b_lbl[i], &font_s, 0);
        lv_label_set_text(b_lbl[i], BTN[i].txt);
        lv_obj_center(b_lbl[i]);
        btn_style(i, false);
    }
    btn_labels();

    t_down = false;
    t_btn = -1;
    have_g = false;
    ok = th_start();
    if (!ok) lv_label_set_text(l_hint, "Audio non disponibile");
    if (!board_imu_ok()) lv_label_set_text(l_hint, "Accelerometro non disponibile");
    tmr = lv_timer_create(touch_cb, 15, NULL);
    stmr = lv_timer_create(sample_cb, 20, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    if (stmr) { lv_timer_delete(stmr); stmr = NULL; }
    th_stop();   // ferma suono e registrazione (il file viene chiuso)
    if (dirty) { th_cfg_save(); dirty = false; }
}

static bool nav(nav_t ev)
{
    if (ev == NAV_BTN) { calibrate(); ui_toast("Posizione zero impostata"); return true; }
    if (ev == NAV_QUICK) return true;   // il dito tenuto sulla zona serve a suonare
    // i gesti nati suonando (il dito che scivola sulla zona) non devono uscire né scorrere
    bool from_pad = t_pad && (t_down || lv_tick_get() - pad_release < 400);
    if (from_pad && (ev == NAV_BACK || ev == NAV_SELECT || ev == NAV_NEXT || ev == NAV_PREV)) return true;
    return false;   // sinistra sui pulsanti: esce
}

const app_t app_theremin = {
    .name = "Theremin", .icon = ICON_TUNER,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};
