// app_chess.c — Orologio per scacchi: due orologi, uno per metà schermo.
//
// Chi ha mosso tocca la propria metà e parte l'orologio dell'avversario (all'inizio il nero
// tocca la sua metà per far partire il bianco). BOOT mette in pausa.
// Cadenze pronte (bullet, blitz, rapid, classica, FIDE, clessidra) o personalizzate: tempo
// (anche diverso per il nero), incremento Fischer, Bronstein, ritardo, clessidra, secondo
// periodo dopo N mosse, avvisi e suoni. Impostazioni nell'NVS (namespace "chess", nei backup).
#include "apps.h"
#include "audio.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "nvs.h"

/* ================= impostazioni ================= */

enum { CM_FISCHER, CM_BRONSTEIN, CM_DELAY, CM_HOURGLASS, CM_NONE, CM_COUNT };

typedef struct {
    uint32_t magic;
    uint16_t base_w, base_b;   // secondi
    uint8_t mode;              // CM_*
    uint8_t inc;               // secondi (incremento o ritardo)
    uint8_t stage_moves;       // 0 = nessun secondo periodo
    uint8_t stage_min;         // minuti aggiunti dopo stage_moves mosse
    uint8_t white_right;       // il bianco sulla metà destra
    uint8_t sound;
    uint8_t warn;              // indice in WARN_S
    uint8_t show_moves;
} ccfg_t;

#define CFG_MAGIC 0x31534843u   // "CHS1"
#define BASE_MIN  15
#define BASE_MAX  (180 * 60)
static const int WARN_S[] = {0, 10, 20, 30, 60};
#define N_WARN (int)(sizeof(WARN_S) / sizeof(WARN_S[0]))
static const int STAGE_MOVES[] = {0, 20, 30, 40, 50, 60};
#define N_STAGE (int)(sizeof(STAGE_MOVES) / sizeof(STAGE_MOVES[0]))

static ccfg_t cfg;
static bool cfg_loaded, dirty;

static void cfg_defaults(void)
{
    cfg = (ccfg_t){.magic = CFG_MAGIC, .base_w = 300, .base_b = 300, .mode = CM_FISCHER, .inc = 3,
                   .stage_moves = 0, .stage_min = 30, .white_right = 0, .sound = 1, .warn = 1, .show_moves = 1};
}

static void cfg_load(void)
{
    if (cfg_loaded) return;
    cfg_loaded = true;
    cfg_defaults();
    nvs_handle_t h;
    if (nvs_open("chess", NVS_READONLY, &h) != ESP_OK) return;
    ccfg_t t;
    size_t len = sizeof(t);
    if (nvs_get_blob(h, "cfg", &t, &len) == ESP_OK && len == sizeof(t) && t.magic == CFG_MAGIC) {
        cfg = t;
        if (cfg.base_w < BASE_MIN || cfg.base_w > BASE_MAX) cfg.base_w = 300;
        if (cfg.base_b < BASE_MIN || cfg.base_b > BASE_MAX) cfg.base_b = cfg.base_w;
        if (cfg.mode >= CM_COUNT) cfg.mode = CM_FISCHER;
        if (cfg.inc > 60) cfg.inc = 3;
        if (cfg.stage_moves > 60) cfg.stage_moves = 0;
        if (cfg.stage_min < 1 || cfg.stage_min > 90) cfg.stage_min = 30;
        if (cfg.warn >= N_WARN) cfg.warn = 1;
    }
    nvs_close(h);
}

static void cfg_save(void)
{
    if (!dirty) return;
    dirty = false;
    nvs_handle_t h;
    if (nvs_open("chess", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &cfg, sizeof(cfg));
    nvs_commit(h);
    nvs_close(h);
}

static void touch_cfg(void) { dirty = true; }

/* ---------------- cadenze pronte ---------------- */

typedef struct { const char *name; uint16_t base; uint8_t mode, inc, stage_moves, stage_min; } preset_t;
static const preset_t PRESETS[] = {
    {"Bullet 1+0", 60, CM_NONE, 0, 0, 0},
    {"Bullet 2+1", 120, CM_FISCHER, 1, 0, 0},
    {"Blitz 3+0", 180, CM_NONE, 0, 0, 0},
    {"Blitz 3+2", 180, CM_FISCHER, 2, 0, 0},
    {"Blitz 5+0", 300, CM_NONE, 0, 0, 0},
    {"Blitz 5+3", 300, CM_FISCHER, 3, 0, 0},
    {"Rapid 10+0", 600, CM_NONE, 0, 0, 0},
    {"Rapid 10+5", 600, CM_FISCHER, 5, 0, 0},
    {"Rapid 15+10", 900, CM_FISCHER, 10, 0, 0},
    {"Rapid 25+10", 1500, CM_FISCHER, 10, 0, 0},
    {"Classica 30+0", 1800, CM_NONE, 0, 0, 0},
    {"Classica 60+30", 3600, CM_FISCHER, 30, 0, 0},
    {"Classica 90+30", 5400, CM_FISCHER, 30, 0, 0},
    {"FIDE: 90' / 40 mosse + 30', +30\"", 5400, CM_FISCHER, 30, 40, 30},
    {"Ritardo 5 min, 5\"", 300, CM_DELAY, 5, 0, 0},
    {"Clessidra 1 min", 60, CM_HOURGLASS, 0, 0, 0},
};
#define N_PRESETS (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))

static int preset_match(void)
{
    for (int i = 0; i < N_PRESETS; i++) {
        const preset_t *p = &PRESETS[i];
        bool inc_used = cfg.mode != CM_NONE && cfg.mode != CM_HOURGLASS;
        if (cfg.base_w == p->base && cfg.base_b == p->base && cfg.mode == p->mode &&
            (!inc_used || cfg.inc == p->inc) && cfg.stage_moves == p->stage_moves &&
            (!p->stage_moves || cfg.stage_min == p->stage_min))
            return i;
    }
    return -1;
}

/* ---------------- testo ---------------- */

// durata leggibile: "5 min", "1 min 30 s", "1 h 30 min"
static void fmt_dur(char *b, int n, int s)
{
    int h = s / 3600, m = s / 60 % 60, sec = s % 60;
    if (h && m) snprintf(b, n, "%d h %d min", h, m);
    else if (h) snprintf(b, n, "%d h", h);
    else if (m && sec) snprintf(b, n, "%d min %d s", m, sec);
    else if (m) snprintf(b, n, "%d min", m);
    else snprintf(b, n, "%d s", sec);
}

static const char *mode_name(int m)
{
    static const char *const n[] = {"Incremento (Fischer)", "Bronstein", "Ritardo (delay)", "Clessidra", "Nessun incremento"};
    return m >= 0 && m < CM_COUNT ? n[m] : "";
}

/* ---------------- voci del menu ---------------- */

extern const app_t app_chess;

static void v_preset(char *b, int n)
{
    int i = preset_match();
    snprintf(b, n, "%s", i >= 0 ? PRESETS[i].name : "Personalizzata");
}
static void j_preset(int d)
{
    int i = preset_match();
    i = i < 0 ? (d > 0 ? 0 : N_PRESETS - 1) : (i + d + N_PRESETS) % N_PRESETS;
    const preset_t *p = &PRESETS[i];
    cfg.base_w = cfg.base_b = p->base;
    cfg.mode = p->mode;
    cfg.inc = p->inc;
    cfg.stage_moves = p->stage_moves;
    if (p->stage_moves) cfg.stage_min = p->stage_min;
    touch_cfg();
}

static int step_base(int s, int d)
{
    int st = s + (d > 0 ? 0 : -1) < 300 ? 15 : s + (d > 0 ? 0 : -1) < 1200 ? 60 : s + (d > 0 ? 0 : -1) < 3600 ? 300 : 900;
    int v = (s / st + d) * st;   // si allinea al passo
    if (d > 0 && v <= s) v = s + st;
    return v < BASE_MIN ? BASE_MIN : v > BASE_MAX ? BASE_MAX : v;
}
static void v_base(char *b, int n) { fmt_dur(b, n, cfg.base_w); }
static void j_base(int d)
{
    bool same = cfg.base_b == cfg.base_w;
    cfg.base_w = step_base(cfg.base_w, d);
    if (same) cfg.base_b = cfg.base_w;   // il nero segue finché è uguale
    touch_cfg();
}
static void v_base_b(char *b, int n)
{
    if (cfg.base_b == cfg.base_w) { snprintf(b, n, "Uguale al bianco"); return; }
    char t[24];
    fmt_dur(t, sizeof(t), cfg.base_b);
    snprintf(b, n, "%s (diverso: handicap)", t);
}
static void j_base_b(int d) { cfg.base_b = step_base(cfg.base_b, d); touch_cfg(); }

static void v_mode(char *b, int n) { snprintf(b, n, "%s", mode_name(cfg.mode)); }
static void j_mode(int d) { cfg.mode = (cfg.mode + d + CM_COUNT) % CM_COUNT; touch_cfg(); }

static void v_inc(char *b, int n)
{
    if (cfg.mode == CM_NONE || cfg.mode == CM_HOURGLASS) snprintf(b, n, "%d s (non usati in questa modalità)", cfg.inc);
    else if (cfg.mode == CM_DELAY) snprintf(b, n, "%d s di attesa prima che il tempo scenda", cfg.inc);
    else if (cfg.mode == CM_BRONSTEIN) snprintf(b, n, "Fino a %d s restituiti a ogni mossa", cfg.inc);
    else snprintf(b, n, "+%d s a ogni mossa", cfg.inc);
}
static void j_inc(int d)
{
    int st = (cfg.inc + (d > 0 ? 0 : -1)) < 10 ? 1 : 5;
    int v = cfg.inc + d * st;
    cfg.inc = v < 0 ? 0 : v > 60 ? 60 : v;
    touch_cfg();
}

static void v_stage(char *b, int n)
{
    if (!cfg.stage_moves) snprintf(b, n, "Nessuno");
    else snprintf(b, n, "Dopo %d mosse: +%d min", cfg.stage_moves, cfg.stage_min);
}
static void j_stage(int d)
{
    int i = 0;
    while (i < N_STAGE - 1 && STAGE_MOVES[i] != cfg.stage_moves) i++;
    i += d;
    cfg.stage_moves = STAGE_MOVES[i < 0 ? 0 : i >= N_STAGE ? N_STAGE - 1 : i];
    touch_cfg();
}
static void v_stage_min(char *b, int n)
{
    snprintf(b, n, "+%d min%s", cfg.stage_min, cfg.stage_moves ? "" : " (secondo periodo spento)");
}
static void j_stage_min(int d)
{
    int v = cfg.stage_min + d * 5;
    cfg.stage_min = v < 5 ? 5 : v > 90 ? 90 : v;
    touch_cfg();
}

static void v_side(char *b, int n) { snprintf(b, n, "Bianco a %s", cfg.white_right ? "destra" : "sinistra"); }
static void a_side(void) { cfg.white_right = !cfg.white_right; touch_cfg(); }
static void v_sound(char *b, int n) { snprintf(b, n, "%s", cfg.sound ? "Sì (mossa, avvisi, tempo scaduto)" : "No"); }
static void a_sound(void) { cfg.sound = !cfg.sound; touch_cfg(); }
static void v_warn(char *b, int n)
{
    if (!WARN_S[cfg.warn]) snprintf(b, n, "Spento");
    else snprintf(b, n, "Sotto %d s: tempo in rosso%s", WARN_S[cfg.warn], cfg.sound ? " e bip" : "");
}
static void j_warn(int d) { int v = cfg.warn + d; cfg.warn = v < 0 ? 0 : v >= N_WARN ? N_WARN - 1 : v; touch_cfg(); }
static void v_moves(char *b, int n) { snprintf(b, n, "%s", cfg.show_moves ? "Sì" : "No"); }
static void a_moves(void) { cfg.show_moves = !cfg.show_moves; touch_cfg(); }
static void a_reset(void) { cfg_defaults(); touch_cfg(); ui_toast("Impostazioni predefinite: Blitz 5+3"); }

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Nuova partita", .app = &app_chess, .hint = "Il nero tocca la sua metà per far partire il bianco"},
    {.icon = ICON_CLOCK, .label = "Cadenza", .value = v_preset, .on_adjust = j_preset},
    {.icon = ICON_CLOCK, .label = "Tempo", .value = v_base, .on_adjust = j_base},
    {.icon = ICON_CLOCK, .label = "Tempo del nero", .value = v_base_b, .on_adjust = j_base_b},
    {.icon = ICON_SLIDERS, .label = "Modalità", .value = v_mode, .on_adjust = j_mode},
    {.icon = LV_SYMBOL_PLUS, .label = "Secondi per mossa", .value = v_inc, .on_adjust = j_inc},
    {.icon = LV_SYMBOL_LOOP, .label = "Secondo periodo", .value = v_stage, .on_adjust = j_stage},
    {.icon = LV_SYMBOL_PLUS, .label = "Tempo del secondo periodo", .value = v_stage_min, .on_adjust = j_stage_min},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Lato del bianco", .value = v_side, .on_select = a_side},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Suoni", .value = v_sound, .on_select = a_sound},
    {.icon = LV_SYMBOL_WARNING, .label = "Avviso tempo basso", .value = v_warn, .on_adjust = j_warn},
    {.icon = LV_SYMBOL_LIST, .label = "Conta le mosse", .value = v_moves, .on_select = a_moves},
    {.icon = LV_SYMBOL_REFRESH, .label = "Impostazioni predefinite", .hint = "Blitz 5+3", .on_select = a_reset, .confirm = true},
};

static void menu_closed(void) { cfg_save(); }

menu_t chess_menu = {"Orologio scacchi", items, sizeof(items) / sizeof(items[0]), 0, menu_closed};

// le voci del menu leggono le impostazioni: si caricano all'avvio (home_init)
void chess_menu_init(void) { cfg_load(); }

/* ================= suoni ================= */

static volatile uint32_t bp_n;   // campioni di bip ancora da suonare
static volatile float bp_step;
static float bp_ph;
static bool audio_ok;

static void beep_synth(int16_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (bp_n) {
            // onda quadra addolcita: si sente bene anche dall'altoparlantino
            float s = sinf(bp_ph);
            b[i] = (int16_t)((s > 0.3f ? 1 : s < -0.3f ? -1 : s / 0.3f) * 9000);
            bp_ph += bp_step;
            if (bp_ph > 6.2831853f) bp_ph -= 6.2831853f;
            bp_n--;
        } else b[i] = 0;
    }
}

static void beep(int hz, int ms)
{
    if (!cfg.sound || !audio_ok) return;
    bp_step = 6.2831853f * hz / AUDIO_RATE;
    bp_n = (uint32_t)AUDIO_RATE * ms / 1000;
}

/* ================= partita ================= */

#define HALF   (SCR_W / 2)
#define OV_W  360
#define OV_H  120
#define OV_X  ((SCR_W - OV_W) / 2)
#define OV_Y  ((SCR_H - OV_H) / 2)
#define OB_Y  58
#define OB_H  50

#define US     1000000LL

typedef enum { G_READY, G_RUN, G_PAUSE, G_FLAG } gstate_t;
static gstate_t gs;
static int64_t rem[2];          // tempo rimasto in µs (0 = bianco, 1 = nero)
static int moves[2];
static int turn;                // chi ha l'orologio che scorre
static int flag_side;
static int64_t turn_t0, last_t, pause_t0;
static int alarm_left;          // bip del tempo scaduto ancora da fare
static uint32_t alarm_next;
static int last_tick_s;         // ultimo secondo in cui ha fatto il tic dell'avviso
static bool warned[2];

static lv_obj_t *panel[2], *l_time[2], *l_info[2], *l_sub[2];
static lv_obj_t *ov, *ov_title, *ov_btn[2], *ov_lbl[2];
static int ov_n;
static lv_timer_t *tmr;

static int side_of(int player) { return (player == 0) != (cfg.white_right != 0); }   // 0 sinistra, 1 destra

static int64_t now_us(void) { return esp_timer_get_time(); }

// consuma il tempo fino a "now" per chi ha il turno
static void advance(int64_t now)
{
    if (gs != G_RUN) return;
    int64_t dt = now - last_t;
    if (dt <= 0) return;
    int64_t used = dt;
    if (cfg.mode == CM_DELAY) {
        int64_t d = (int64_t)cfg.inc * US;
        int64_t a = last_t - turn_t0, b = now - turn_t0;
        used = (b > d ? b - d : 0) - (a > d ? a - d : 0);
    }
    rem[turn] -= used;
    if (cfg.mode == CM_HOURGLASS) rem[1 - turn] += dt;
    last_t = now;
    if (rem[turn] <= 0) {
        rem[turn] = 0;
        gs = G_FLAG;
        flag_side = turn;
        alarm_left = 3;
        alarm_next = 0;
    }
}

static void reset_game(void)
{
    rem[0] = (int64_t)cfg.base_w * US;
    rem[1] = (int64_t)cfg.base_b * US;
    moves[0] = moves[1] = 0;
    turn = 0;
    gs = G_READY;
    alarm_left = 0;
    last_tick_s = -1;
    warned[0] = warned[1] = false;
}

// il giocatore p ha mosso (ha toccato la sua metà)
static void moved(int p)
{
    int64_t now = now_us();
    advance(now);
    if (gs != G_RUN || p != turn) return;
    int64_t used = now - turn_t0;
    moves[p]++;
    if (cfg.mode == CM_FISCHER) rem[p] += (int64_t)cfg.inc * US;
    else if (cfg.mode == CM_BRONSTEIN) rem[p] += used < (int64_t)cfg.inc * US ? used : (int64_t)cfg.inc * US;
    if (cfg.stage_moves && moves[p] == cfg.stage_moves) rem[p] += (int64_t)cfg.stage_min * 60 * US;
    if (rem[p] > (int64_t)WARN_S[cfg.warn] * US) warned[p] = false;
    turn = 1 - p;
    turn_t0 = last_t = now;
    beep(1800, 25);
}

static void start(void)
{
    gs = G_RUN;
    turn = 0;
    turn_t0 = last_t = now_us();
    beep(1800, 25);
}

static void pause_game(void)
{
    if (gs != G_RUN) return;
    advance(now_us());
    if (gs != G_RUN) return;
    gs = G_PAUSE;
    pause_t0 = now_us();
}

static void resume_game(void)
{
    if (gs != G_PAUSE) return;
    int64_t d = now_us() - pause_t0;
    turn_t0 += d;   // la pausa non conta per il ritardo e per Bronstein
    last_t += d;
    gs = G_RUN;
}

/* ---------------- disegno ---------------- */

static void fmt_clock(char *b, int n, int64_t us)
{
    if (us < 0) us = 0;
    if (us < 20 * US) {   // ultimi 20 secondi: anche i decimi
        int64_t ds = us / (US / 10);
        snprintf(b, n, "0:%02d.%d", (int)(ds / 10), (int)(ds % 10));
        return;
    }
    int64_t s = (us + US - 1) / US;   // arrotonda in su: 0:00 solo quando è finito
    if (s >= 3600) snprintf(b, n, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    else snprintf(b, n, "%d:%02d", (int)(s / 60), (int)(s % 60));
}

static void place_overlay_buttons(void)
{
    int w = (OV_W - 24 - 8 * (ov_n - 1)) / (ov_n ? ov_n : 1);
    for (int i = 0; i < ov_n; i++) {
        lv_obj_set_pos(ov_btn[i], 12 + i * (w + 8), OB_Y);
        lv_obj_set_size(ov_btn[i], w, OB_H);
        lv_obj_center(ov_lbl[i]);
    }
}

static void overlay(const char *title, const char *a, const char *b)
{
    if (!title) { lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN); ov_n = 0; return; }
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ov);
    ui_set_text(ov_title, title);
    ov_n = b ? 2 : 1;
    const char *t[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        if (i >= ov_n) { lv_obj_add_flag(ov_btn[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(ov_btn[i], LV_OBJ_FLAG_HIDDEN);
        ui_set_text(ov_lbl[i], t[i]);
    }
    place_overlay_buttons();
}

static void render(void)
{
    for (int p = 0; p < 2; p++) {
        char t[32];
        bool active = (gs == G_RUN || gs == G_PAUSE) && turn == p;
        bool flag = gs == G_FLAG && flag_side == p;
        bool low = WARN_S[cfg.warn] && rem[p] < (int64_t)WARN_S[cfg.warn] * US;
        fmt_clock(t, sizeof(t), rem[p]);
        // con le ore il carattere grande non ci sta: si passa a quello medio
        const lv_font_t *f = lv_text_get_width(t, strlen(t), &font_xl, 0) > HALF - 24 ? &font_l : &font_xl;
        if (lv_obj_get_style_text_font(l_time[p], 0) != f) lv_obj_set_style_text_font(l_time[p], f, 0);
        ui_set_text(l_time[p], t);
        lv_color_t bg = flag ? lv_color_hex(0x7A1A14) : active ? lv_color_hex(0x16324A) : lv_color_hex(0x0B0D10);
        ui_set_bg_color(panel[p], bg);
        ui_set_text_color(l_time[p], flag ? C_TEXT : low ? C_WARN : active ? C_TEXT : C_DIM);
        // riga sopra: chi è e quante mosse
        if (cfg.show_moves) snprintf(t, sizeof(t), "%s · %d moss%s", p ? "Nero" : "Bianco", moves[p], moves[p] == 1 ? "a" : "e");
        else snprintf(t, sizeof(t), "%s", p ? "Nero" : "Bianco");
        ui_set_text(l_info[p], t);
        ui_set_text_color(l_info[p], active ? ui_accent() : C_DIM);
        // riga sotto: cosa succede
        const char *sub = "";
        char s2[48];
        if (flag) sub = "Tempo scaduto!";
        else if (gs == G_READY) sub = p == 1 ? "Tocca qui per far partire il bianco" : "";
        else if (active && cfg.mode == CM_DELAY && gs == G_RUN) {
            int64_t left = (int64_t)cfg.inc * US - (now_us() - turn_t0);
            if (left > 0) { snprintf(s2, sizeof(s2), "Ritardo %d s", (int)((left + US - 1) / US)); sub = s2; }
        } else if (active && gs == G_RUN) sub = "Hai mosso? Tocca qui";
        if (cfg.stage_moves && !flag && moves[p] < cfg.stage_moves && gs != G_READY && !sub[0]) {
            snprintf(s2, sizeof(s2), "%d mosse al secondo periodo", cfg.stage_moves - moves[p]);
            sub = s2;
        }
        ui_set_text(l_sub[p], sub);
    }
}

static void tick_cb(lv_timer_t *t)
{
    int64_t now = now_us();
    gstate_t before = gs;
    advance(now);
    if (before == G_RUN && gs == G_FLAG) overlay(flag_side ? "Tempo scaduto: vince il bianco" : "Tempo scaduto: vince il nero",
                                                 "Nuova partita", NULL);
    // avvisi: un bip doppio quando scende sotto la soglia, poi un tic al secondo negli ultimi 10
    if (gs == G_RUN && WARN_S[cfg.warn]) {
        int64_t r = rem[turn];
        if (!warned[turn] && r < (int64_t)WARN_S[cfg.warn] * US) {
            warned[turn] = true;
            beep(1400, 120);
        }
        int s = (int)(r / US);
        if (r < 10 * US && s != last_tick_s) { last_tick_s = s; beep(1000, 30); }
    }
    if (alarm_left > 0 && lv_tick_get() >= alarm_next) {
        beep(880, 350);
        alarm_left--;
        alarm_next = lv_tick_get() + 550;
    }
    render();
}

/* ---------------- tocchi ---------------- */

#define RELEASE_MS 70
static bool t_down;
static int t_btn = -1;
static uint32_t t_seen;
static lv_timer_t *touch_tmr;

static int ov_hit(int x, int y)
{
    if (!ov_n || y < OV_Y + OB_Y || y >= OV_Y + OB_Y + OB_H) return -1;
    int w = (OV_W - 24 - 8 * (ov_n - 1)) / ov_n;
    for (int i = 0; i < ov_n; i++) {
        int bx = OV_X + 12 + i * (w + 8);
        if (x >= bx && x < bx + w) return i;
    }
    return -1;
}

static void on_overlay(int i)
{
    if (gs == G_PAUSE) {
        if (i == 0) { resume_game(); overlay(NULL, NULL, NULL); }
        else { reset_game(); overlay(NULL, NULL, NULL); }
    } else if (gs == G_FLAG) {
        reset_game();
        overlay(NULL, NULL, NULL);
    }
    render();
}

static void touch_cb(lv_timer_t *tm)
{
    int x, y;
    uint32_t now = lv_tick_get();
    if (input_touch(&x, &y)) {
        t_seen = now;
        if (t_down) return;
        t_down = true;
        t_btn = -1;
        if (ov_n) {   // finestra in primo piano: si tocca solo lei
            t_btn = ov_hit(x, y);
            if (t_btn >= 0) ui_set_bg_color(ov_btn[t_btn], ui_accent());
            return;
        }
        // l'orologio si ferma al tocco, non al rilascio: conta il momento esatto
        int side = x >= HALF;
        int p = side_of(0) == side ? 0 : 1;
        if (gs == G_READY) {
            if (p == 1) start();
            else ui_toast("Il nero tocca la sua metà per far partire il bianco");
        } else if (gs == G_RUN) moved(p);
        render();
        return;
    }
    if (!t_down || now - t_seen < RELEASE_MS) return;
    t_down = false;
    if (t_btn >= 0) {
        int b = t_btn;
        t_btn = -1;
        ui_set_bg_color(ov_btn[b], lv_color_hex(0x1C2025));
        on_overlay(b);
    }
}

/* ---------------- ciclo di vita ---------------- */

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *lbl(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    cfg_save();
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    for (int p = 0; p < 2; p++) {
        int x = side_of(p) * HALF;
        panel[p] = box(root, x + 3, 3, HALF - 6, SCR_H - 6, lv_color_hex(0x0B0D10), 12);
        l_info[p] = lbl(panel[p], &font_m, C_DIM);
        lv_obj_align(l_info[p], LV_ALIGN_TOP_MID, 0, 8);
        l_time[p] = lbl(panel[p], &font_xl, C_DIM);
        lv_obj_align(l_time[p], LV_ALIGN_CENTER, 0, 2);
        l_sub[p] = lbl(panel[p], &font_s, C_DIM);
        lv_obj_align(l_sub[p], LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    ov = box(root, OV_X, OV_Y, OV_W, OV_H, lv_color_hex(0x15191E), 14);
    lv_obj_set_style_border_width(ov, 2, 0);
    lv_obj_set_style_border_color(ov, ui_accent(), 0);
    ov_title = lbl(ov, &font_m, C_TEXT);
    lv_obj_set_width(ov_title, OV_W - 24);
    lv_obj_set_style_text_align(ov_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ov_title, LV_LABEL_LONG_DOT);
    lv_obj_align(ov_title, LV_ALIGN_TOP_MID, 0, 16);
    for (int i = 0; i < 2; i++) {
        ov_btn[i] = box(ov, 0, OB_Y, 10, OB_H, lv_color_hex(0x1C2025), 10);
        ov_lbl[i] = lbl(ov_btn[i], &font_m, C_TEXT);
        lv_obj_center(ov_lbl[i]);
    }
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    ov_n = 0;

    audio_ok = audio_init();
    if (audio_ok) {
        audio_set_volume(g_set.volume);
        bp_n = 0;
        audio_start(beep_synth);
    }
    reset_game();
    t_down = false;
    t_btn = -1;
    render();
    tmr = lv_timer_create(tick_cb, 50, NULL);
    touch_tmr = lv_timer_create(touch_cb, 10, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    if (touch_tmr) { lv_timer_delete(touch_tmr); touch_tmr = NULL; }
    if (audio_ok) audio_stop_if(beep_synth);
}

static bool nav(nav_t ev)
{
    if (ev == NAV_BTN) {
        if (gs == G_RUN) {
            pause_game();
            overlay("Pausa · BOOT riprende", "Riprendi", "Azzera");
            render();
            return true;
        }
        if (gs == G_PAUSE) { resume_game(); overlay(NULL, NULL, NULL); render(); return true; }
        return false;   // pronto o finita: esce
    }
    if (gs == G_RUN) return true;   // in partita nessun gesto: si toccano solo gli orologi
    if (ev == NAV_BACK) return false;
    return true;
}

const app_t app_chess = {
    .name = "Orologio scacchi", .icon = ICON_CLOCK,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};
