// app_morse.c — Morse: corso per imparare ad ascoltare (metodo Koch) e a trasmettere, il
// Telegrafo per parlare con altri Gadget vicini via ESP-NOW (senza rete né associazione),
// l'ascolto col microfono, la tabella. Codice, tempi e decodifica in morse.c.
//
// Il tasto del telegrafo è BOOT (sul tondo anche il dito sullo schermo; sul 3,49" no: il
// touch a volte registra tocchi fantasma).
#include "apps.h"
#include "audio.h"
#include "board.h"
#include "keyboard.h"
#include "morse.h"
#include "settings.h"
#include "wifi_mgr.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#ifndef SEISMO_SIM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_random.h"
#endif

#define NKOCH ((int)sizeof(MORSE_KOCH) - 1)

/* ================= impostazioni e progressi (NVS "morse") ================= */

#define CFG_MAGIC 0x4D525301u
typedef struct {
    uint32_t magic;
    uint8_t wpm, eff;           // velocità dei segni e velocità effettiva (Farnsworth, per imparare)
    uint16_t freq;              // tono (Hz)
    uint8_t channel;            // canale ESP-NOW del Telegrafo
    uint8_t level;              // lettere del corso già aperte (dalla serie Koch)
    uint8_t ok[NKOCH], tot[NKOCH];
    uint32_t recent;            // ultime risposte (bit 1 = giusta)
    uint8_t recent_n;
} mcfg_t;
static mcfg_t cfg;
static bool cfg_loaded;

static void cfg_defaults(void)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = CFG_MAGIC;
    cfg.wpm = 15;
    cfg.eff = 8;
    cfg.freq = 700;
    cfg.channel = 1;
    cfg.level = 2;
}

static void cfg_load(void)
{
    if (cfg_loaded) return;
    cfg_loaded = true;
    cfg_defaults();
    nvs_handle_t h;
    if (nvs_open("morse", NVS_READONLY, &h) != ESP_OK) return;
    mcfg_t c;
    size_t len = sizeof(c);
    if (nvs_get_blob(h, "cfg", &c, &len) == ESP_OK && len == sizeof(c) && c.magic == CFG_MAGIC) cfg = c;
    nvs_close(h);
}

static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("morse", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &cfg, sizeof(cfg));
    nvs_commit(h);
    nvs_close(h);
}

static uint32_t rnd(void)
{
#ifdef SEISMO_SIM
    return (uint32_t)rand();
#else
    return esp_random();
#endif
}

/* ================= suono ================= */

// Un solo sintetizzatore per tutto: tono acceso dal tasto (key_on), dal Gadget lontano
// (remote_on) o da una sequenza da suonare (timeline). Attacco e rilascio di 5 ms: niente clic.
static volatile bool key_on, remote_on;
static int16_t tl[1200];
static volatile int tl_n, tl_i;
static volatile int32_t tl_left;
static volatile bool tl_tone;
static float ph, amp;
static bool audio_ok;

static void synth(int16_t *b, int n)
{
    float step = 2.0f * (float)M_PI * cfg.freq / AUDIO_RATE;
    for (int i = 0; i < n; i++) {
        if (tl_i < tl_n) {
            if (tl_left <= 0) {
                int v = tl[tl_i];
                tl_tone = v > 0;
                tl_left = (int32_t)abs(v) * AUDIO_RATE / 1000;
            }
            if (--tl_left <= 0) { tl_i = tl_i + 1; if (tl_i >= tl_n) tl_tone = false; }
        }
        bool on = key_on || remote_on || (tl_i < tl_n && tl_tone);
        float target = on ? 1.0f : 0.0f;
        amp += (target - amp) * 0.0083f;   // ~5 ms a 24 kHz
        ph += step;
        if (ph > 6.2831853f) ph -= 6.2831853f;
        b[i] = (int16_t)(sinf(ph) * amp * 9000);
    }
}

static void sound_on(void)
{
    audio_ok = audio_init();
    if (audio_ok) { audio_set_volume(g_set.volume); audio_start(synth); }
}

static void sound_off(void)
{
    key_on = remote_on = false;
    tl_n = tl_i = 0;
    if (audio_ok) audio_stop_if(synth);
}

static void play_text(const char *s, int wpm, int eff)
{
    tl_n = 0;   // ferma quella in corso
    tl_i = 0;
    tl_left = 0;
    if (!audio_ok) return;   // senza audio non resta "in riproduzione" per sempre
    int n = morse_timeline(s, wpm, eff, tl, sizeof(tl) / sizeof(tl[0]));
    tl_n = n;
}
static bool playing(void) { return tl_i < tl_n; }

// ".-" → "• —" per lo schermo
static void pretty(const char *code, char *o, int n)
{
    int k = 0;
    for (const char *c = code; c && *c && k < n - 6; c++) {
        if (k) o[k++] = ' ';
        const char *g = *c == '-' ? "\xE2\x80\x94" : "\xE2\x80\xA2";
        memcpy(o + k, g, 3);
        k += 3;
    }
    o[k] = 0;
}

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int w, int x, int y, bool center)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    if (w) lv_obj_set_width(l, w);
    if (center) {
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, x, y);
    } else lv_obj_set_pos(l, x, y);
    return l;
}

/* ================= il tasto (BOOT, e sul tondo il dito) ================= */

static bool key_down;
static uint32_t key_t0;       // inizio dell'ultimo cambio
static morse_dec_t dk;       // decodifica di quello che batti

static bool key_pressed(void)
{
    if (board_btn_boot()) return true;
    int x, y;
    return SCR_ROUND && input_touch(&x, &y);
}

// da chiamare ogni 10 ms; restituisce +1 tasto giù, -1 tasto su, 0 niente
static int key_poll(void)
{
    bool p = key_pressed();
    uint32_t now = lv_tick_get();
    if (p == key_down) {
        if (!key_down) md_gap(&dk, now - key_t0);
        return 0;
    }
    if (now - key_t0 < 12) return 0;   // rimbalzo del pulsante
    int ms = now - key_t0;
    key_down = p;
    key_t0 = now;
    key_on = p;
    if (!p) md_mark(&dk, ms);
    return p ? 1 : -1;
}

/* ================= corso: ascolta ================= */

static enum { L_PLAY, L_ANSWER, L_RESULT } l_state;
static int l_target, l_opt[4], l_nopt, l_sel;
static bool l_right;
static lv_obj_t *l_top, *l_big, *l_pat, *l_optl[4], *l_res, *l_help;
static lv_timer_t *l_tmr;
static uint32_t l_t0;

static int koch_index(char c) { const char *p = strchr(MORSE_KOCH, c); return p ? (int)(p - MORSE_KOCH) : -1; }

static int pick_char(void)
{
    // la lettera nuova esce più spesso; poi pesa quanto spesso la sbagli
    int n = cfg.level;
    if (rnd() % 100 < 30) return n - 1;
    float w[NKOCH], tot = 0;
    for (int i = 0; i < n; i++) {
        float err = cfg.tot[i] ? 1.0f - (float)cfg.ok[i] / cfg.tot[i] : 0.5f;
        w[i] = 0.3f + err;
        tot += w[i];
    }
    float x = (rnd() % 10000) / 10000.0f * tot;
    for (int i = 0; i < n; i++) if ((x -= w[i]) <= 0) return i;
    return n - 1;
}

static int recent_pct(void)
{
    int n = cfg.recent_n, ok = 0;
    for (int i = 0; i < n; i++) ok += (cfg.recent >> i) & 1;
    return n ? ok * 100 / n : 0;
}

static void l_render(void)
{
    char b[96];
    snprintf(b, sizeof(b), "Lezione %d: %d lettere · ultime risposte %d%%", cfg.level - 1, cfg.level, recent_pct());
    ui_set_text(l_top, b);
    char c = MORSE_KOCH[l_target];
    if (l_state == L_RESULT) {
        b[0] = c;
        b[1] = 0;
        ui_set_text(l_big, b);
        pretty(morse_code(c), b, sizeof(b));
        ui_set_text(l_pat, b);
        ui_set_text(l_res, l_right ? "Giusto!" : "No, era questa");
        ui_set_text_color(l_res, l_right ? C_OK : C_WARN);
    } else {
        ui_set_text(l_big, "?");
        ui_set_text(l_pat, "");
        ui_set_text(l_res, l_state == L_PLAY ? "Ascolta…" : "Quale lettera?");
        ui_set_text_color(l_res, C_TEXT);
    }
    for (int i = 0; i < 4; i++) {
        if (i >= l_nopt) { lv_obj_add_flag(l_optl[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(l_optl[i], LV_OBJ_FLAG_HIDDEN);
        b[0] = MORSE_KOCH[l_opt[i]];
        b[1] = 0;
        ui_set_text(l_optl[i], b);
        bool sel = i == l_sel && l_state != L_PLAY;
        bool right = l_state == L_RESULT && l_opt[i] == l_target;
        lv_obj_set_style_bg_opa(l_optl[i], sel || right ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(l_optl[i], right ? C_OK : ui_accent(), 0);
        ui_set_text_color(l_optl[i], sel || right ? C_BG : C_TEXT);
    }
    ui_set_text(l_help, l_state == L_RESULT ? "Destra: la prossima · BOOT: risenti"
                                            : "Su/giù: scegli · destra: conferma · BOOT: risenti");
}

static void l_next(void)
{
    l_target = pick_char();
    // opzioni: la giusta e fino a tre diverse fra quelle aperte, in ordine sparso
    l_nopt = cfg.level < 4 ? cfg.level : 4;
    l_opt[0] = l_target;
    for (int k = 1; k < l_nopt; k++) {
        int c;
        bool dup;
        do {
            c = rnd() % cfg.level;
            dup = false;
            for (int j = 0; j < k; j++) dup |= l_opt[j] == c;
        } while (dup);
        l_opt[k] = c;
    }
    for (int k = l_nopt - 1; k > 0; k--) { int j = rnd() % (k + 1), t = l_opt[k]; l_opt[k] = l_opt[j]; l_opt[j] = t; }
    l_sel = 0;
    char s[2] = {MORSE_KOCH[l_target], 0};
    play_text(s, cfg.wpm, cfg.wpm);
    l_state = L_PLAY;
    l_t0 = lv_tick_get();
}

static void l_answer(void)
{
    l_right = l_opt[l_sel] == l_target;
    int i = l_target;
    if (cfg.tot[i] < 250) { cfg.tot[i]++; if (l_right) cfg.ok[i]++; }
    cfg.recent = (cfg.recent << 1) | l_right;
    if (cfg.recent_n < 20) cfg.recent_n++;
    l_state = L_RESULT;
    // 90% nelle ultime 20: si apre una lettera nuova
    if (cfg.recent_n >= 20 && recent_pct() >= 90 && cfg.level < NKOCH) {
        cfg.level++;
        cfg.recent = 0;
        cfg.recent_n = 0;
        char m[64], p[32];
        pretty(morse_code(MORSE_KOCH[cfg.level - 1]), p, sizeof(p));
        snprintf(m, sizeof(m), "Lettera nuova: %c  %s", MORSE_KOCH[cfg.level - 1], p);
        ui_toast(m);
    } else if (!l_right) {
        char s[2] = {MORSE_KOCH[l_target], 0};
        play_text(s, cfg.wpm, cfg.wpm);   // la si risente con la risposta sotto gli occhi
    }
    cfg_save();
}

static void l_tick(lv_timer_t *t)
{
    if (l_state == L_PLAY && !playing() && lv_tick_elaps(l_t0) > 200) { l_state = L_ANSWER; l_render(); }
}

static void layout_options(lv_obj_t *root)
{
    bool r = SCR_ROUND;
    for (int i = 0; i < 4; i++) {
        l_optl[i] = mk(root, &font_l, C_TEXT, r ? 64 : 70, 0, 0, false);
        lv_obj_set_style_text_align(l_optl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_radius(l_optl[i], 10, 0);
        lv_obj_set_style_pad_ver(l_optl[i], 6, 0);
        if (r) lv_obj_align(l_optl[i], LV_ALIGN_TOP_MID, -111 + i * 74, SCR_H / 2 - STATUS_H + 30);
        else lv_obj_set_pos(l_optl[i], 330 + i * 76, 34);
    }
}

static void l_enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    bool r = SCR_ROUND;
    int cy = SCR_H / 2 - STATUS_H;
    if (r) {
        l_top = mk(root, &font_s, C_DIM, 300, 0, cy - 150, true);
        lv_label_set_long_mode(l_top, LV_LABEL_LONG_SCROLL_CIRCULAR);   // una riga: sotto c'è la lettera
        l_big = mk(root, &font_xl, ui_accent(), 200, 0, cy - 120, true);
        l_pat = mk(root, &font_l, C_TEXT, 300, 0, cy - 24, true);
        l_res = mk(root, &font_m, C_TEXT, 300, 0, cy + 92, true);
        l_help = mk(root, &font_s, C_DIM, 260, 0, cy + 130, true);
        lv_label_set_long_mode(l_help, LV_LABEL_LONG_SCROLL_CIRCULAR);
    } else {
        l_top = mk(root, &font_s, C_DIM, SCR_W - 24, 12, 4, false);
        l_big = mk(root, &font_xl, ui_accent(), 120, 12, 26, false);
        lv_obj_set_style_text_align(l_big, LV_TEXT_ALIGN_CENTER, 0);
        l_pat = mk(root, &font_l, C_TEXT, 190, 136, 46, false);
        l_res = mk(root, &font_m, C_TEXT, 300, 330, 92, false);
        l_help = mk(root, &font_s, C_DIM, SCR_W - 24, 12, CONTENT_H - 20, false);
    }
    layout_options(root);
    sound_on();
    l_next();
    l_render();
    l_tmr = lv_timer_create(l_tick, 50, NULL);
}

static void l_leave(void)
{
    if (l_tmr) { lv_timer_delete(l_tmr); l_tmr = NULL; }
    sound_off();
}

static bool l_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (l_state == L_ANSWER && l_sel + 1 < l_nopt) { l_sel++; l_render(); } return true;
    case NAV_PREV: if (l_state == L_ANSWER && l_sel > 0) { l_sel--; l_render(); } return true;
    case NAV_SELECT:
        if (l_state == L_ANSWER) l_answer();
        else if (l_state == L_RESULT) l_next();
        l_render();
        return true;
    case NAV_BTN: {
        char s[2] = {MORSE_KOCH[l_target], 0};
        play_text(s, cfg.wpm, cfg.wpm);
        return true;
    }
    default: return false;
    }
}

static const app_t app_mlearn = {
    .name = "Impara ad ascoltare", .icon = ICON_EYE,
    .enter = l_enter, .leave = l_leave, .nav = l_nav, .flags = APP_NO_SLEEP | APP_ROUND_OK,
};

/* ================= corso: trasmetti ================= */

static int k_target;
static enum { K_WAIT, K_RESULT } k_state;
static bool k_right;
static char k_got;
static uint32_t k_t;
static lv_timer_t *k_tmr;

static void k_render(void)
{
    char b[64], p[32];
    char c = MORSE_KOCH[k_target];
    snprintf(b, sizeof(b), "Batti con BOOT%s · velocità %d parole/min", SCR_ROUND ? " o il dito" : "", md_wpm(&dk));
    ui_set_text(l_top, b);
    b[0] = c;
    b[1] = 0;
    ui_set_text(l_big, b);
    pretty(morse_code(c), p, sizeof(p));
    ui_set_text(l_pat, p);
    if (k_state == K_RESULT) {
        if (k_right) snprintf(b, sizeof(b), "Giusto!");
        else if (k_got == '?') snprintf(b, sizeof(b), "Non riconosciuto: riprova");
        else snprintf(b, sizeof(b), "Hai battuto %c", k_got);
        ui_set_text(l_res, b);
        ui_set_text_color(l_res, k_right ? C_OK : C_WARN);
    } else {
        ui_set_text(l_res, key_down ? "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" : "");
        ui_set_text_color(l_res, ui_accent());
    }
    ui_set_text(l_help, "Corto = punto, lungo = linea · destra: un'altra lettera");
}

static void k_new(void)
{
    k_target = rnd() % cfg.level;
    k_state = K_WAIT;
    md_clear(&dk);
}

static void k_tick(lv_timer_t *t)
{
    int e = key_poll();
    if (dk.got) {
        dk.got = false;
        k_got = dk.last;
        k_right = k_got == MORSE_KOCH[k_target];
        k_state = K_RESULT;
        k_t = lv_tick_get();
        k_render();
        return;
    }
    if (k_state == K_RESULT && lv_tick_elaps(k_t) > (k_right ? 900 : 1600)) {   // si passa da soli
        if (k_right) k_new();
        else k_state = K_WAIT;
        k_render();
        return;
    }
    if (e) k_render();
}

static void k_enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    l_enter(root, NULL);   // stessa disposizione del corso d'ascolto
    if (l_tmr) { lv_timer_delete(l_tmr); l_tmr = NULL; }
    tl_n = tl_i = 0;
    for (int i = 0; i < 4; i++) lv_obj_add_flag(l_optl[i], LV_OBJ_FLAG_HIDDEN);
    md_init(&dk, cfg.wpm);
    key_down = false;
    key_t0 = lv_tick_get();
    k_new();
    k_render();
    k_tmr = lv_timer_create(k_tick, 10, NULL);
}

static void k_leave(void)
{
    if (k_tmr) { lv_timer_delete(k_tmr); k_tmr = NULL; }
    sound_off();
}

static bool k_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { k_new(); k_render(); return true; }
    return ev == NAV_BTN || ev == NAV_QUICK || ev == NAV_NEXT || ev == NAV_PREV;   // BOOT è il tasto
}

static const app_t app_mkey = {
    .name = "Impara a trasmettere", .icon = ICON_EYE,
    .enter = k_enter, .leave = k_leave, .nav = k_nav, .flags = APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

/* ================= Telegrafo (ESP-NOW) ================= */

// pacchetto: "GMRS", versione, tipo, numero, mittente, poi il contenuto
enum { P_HELLO = 1, P_KEY, P_TEXT };
typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t ver, type;
    uint16_t seq;
    uint32_t from;
    uint8_t state, wpm;
    uint8_t len;
    char text[160];
} mpkt_t;

typedef struct { uint8_t type, state; uint32_t from; uint16_t seq; char text[161]; } mev_t;

static uint32_t my_id;
static uint16_t my_seq;
static morse_dec_t dr;               // decodifica di quello che arriva
static char t_log[6][176];            // ultimi messaggi e testo
static int t_nlog;
static lv_obj_t *t_rx, *t_tx, *t_log_l;
static lv_timer_t *t_tmr;
static bool t_ok;
static uint32_t t_hello, t_keepalive, r_last, r_t;
static bool r_down;
static uint32_t peers[8], peers_t[8];
static char t_compose[161];
static bool t_send_pending;
#ifndef SEISMO_SIM
static QueueHandle_t q;
#endif

static void name_of(uint32_t id, char *b, int n) { snprintf(b, n, "Gadget-%04X", (unsigned)(id & 0xFFFF)); }

static void on_now(const uint8_t *mac, const uint8_t *data, int len)
{
    // dal task del Wi-Fi: solo copia in coda, il resto lo fa l'interfaccia
    if (len < 13 || memcmp(data, "GMRS", 4)) return;
    const mpkt_t *p = (const mpkt_t *)data;
    mev_t e = {.type = p->type, .state = p->state, .from = p->from, .seq = p->seq};
    if (p->type == P_TEXT) {
        int l = p->len < 160 ? p->len : 160;
        if (len < (int)offsetof(mpkt_t, text) + l) return;
        memcpy(e.text, p->text, l);
        e.text[l] = 0;
    }
#ifndef SEISMO_SIM
    if (q) xQueueSend(q, &e, 0);
#endif
}

static void send_pkt(uint8_t type, uint8_t state, const char *text)
{
    mpkt_t p = {.magic = {'G', 'M', 'R', 'S'}, .ver = 1, .type = type, .seq = ++my_seq, .from = my_id, .state = state, .wpm = cfg.wpm};
    int len = offsetof(mpkt_t, text);
    if (text) {
        p.len = strlen(text) > 160 ? 160 : strlen(text);
        memcpy(p.text, text, p.len);
        len += p.len;
    }
    wifi_mgr_espnow_send(&p, len);
}

static void t_add_log(const char *s)
{
    if (t_nlog == 6) { memmove(t_log[0], t_log[1], sizeof(t_log[0]) * 5); t_nlog = 5; }
    snprintf(t_log[t_nlog++], sizeof(t_log[0]), "%.170s", s);
}

static int peer_count(void)
{
    int n = 0;
    uint32_t now = lv_tick_get();
    for (int i = 0; i < 8; i++) if (peers[i] && now - peers_t[i] < 6000) n++;
    return n;
}

static void peer_seen(uint32_t id)
{
    int free_i = -1;
    for (int i = 0; i < 8; i++) {
        if (peers[i] == id) { peers_t[i] = lv_tick_get(); return; }
        if (!peers[i] || lv_tick_get() - peers_t[i] > 6000) free_i = i;
    }
    if (free_i >= 0) { peers[free_i] = id; peers_t[free_i] = lv_tick_get(); }
}

static void t_render(void)
{
    char b[400], nm[16];
    name_of(my_id, nm, sizeof(nm));
    int n = peer_count();
    if (!t_ok) snprintf(b, sizeof(b), "ESP-NOW non disponibile");
    else if (n) snprintf(b, sizeof(b), "Canale %d · %s · %d Gadget vicin%s", cfg.channel, nm, n, n == 1 ? "o" : "i");
    else snprintf(b, sizeof(b), "Canale %d · %s · nessun Gadget vicino (ancora)", cfg.channel, nm);
    ui_set_text(l_top, b);
    snprintf(b, sizeof(b), "%s%.199s", remote_on ? "\xE2\x80\xA2 " : "", dr.len ? dr.text : "In arrivo: …");
    ui_set_text(t_rx, b);
    ui_set_text_color(t_rx, remote_on ? ui_accent() : C_TEXT);
    snprintf(b, sizeof(b), "Tu: %.199s", dk.len ? dk.text : "");
    ui_set_text(t_tx, b);
    b[0] = 0;
    for (int i = (t_nlog > (SCR_ROUND ? 1 : 2) ? t_nlog - (SCR_ROUND ? 1 : 2) : 0); i < t_nlog; i++) {
        size_t l = strlen(b);
        snprintf(b + l, sizeof(b) - l, "%s%s", l ? "\n" : "", t_log[i]);
    }
    ui_set_text(t_log_l, b);
}

static void t_tick(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    bool changed = false;
    // il tuo tasto: tono, decodifica e pacchetti (lo stato si ripete ogni 100 ms finché è giù:
    // se un pacchetto "su" si perde, chi ascolta spegne da solo)
    int e = key_poll();
    if (e && t_ok) { send_pkt(P_KEY, e > 0, NULL); t_keepalive = now; changed = true; }
    if (key_down && t_ok && now - t_keepalive > 100) { send_pkt(P_KEY, 1, NULL); t_keepalive = now; }
    if (dk.got) { dk.got = false; changed = true; }
    if (t_ok && now - t_hello > 2000) { send_pkt(P_HELLO, 0, NULL); t_hello = now; }
    if (t_send_pending && t_ok) {
        t_send_pending = false;
        for (int i = 0; i < 2; i++) send_pkt(P_TEXT, 0, t_compose);   // due volte: si perde di rado
        char b[176];
        snprintf(b, sizeof(b), "Tu: %.160s", t_compose);
        t_add_log(b);
        changed = true;
    }
#ifndef SEISMO_SIM
    mev_t ev;
    static uint32_t last_text_from;
    static uint16_t last_text_seq;
    while (q && xQueueReceive(q, &ev, 0)) {
        if (ev.from == my_id) continue;
        peer_seen(ev.from);
        if (ev.type == P_KEY) {
            r_last = now;
            if (ev.state != r_down) {
                if (r_down) md_mark(&dr, now - r_t);
                r_down = ev.state;
                r_t = now;
                remote_on = r_down;
                changed = true;
            }
        } else if (ev.type == P_TEXT) {
            if (ev.from == last_text_from && (uint16_t)(ev.seq - last_text_seq) <= 1) continue;   // la copia
            last_text_from = ev.from;
            last_text_seq = ev.seq;
            char b[200], nm[16];
            name_of(ev.from, nm, sizeof(nm));
            snprintf(b, sizeof(b), "%s: %.160s", nm, ev.text);
            t_add_log(b);
            play_text(ev.text, cfg.wpm, cfg.eff);   // si sente in Morse (con le pause del corso)
            changed = true;
        }
    }
#endif
    if (r_down && now - r_last > 300) {   // il pacchetto "su" si è perso
        md_mark(&dr, now - r_t);
        r_down = false;
        remote_on = false;
        r_t = now;
        changed = true;
    }
    if (!r_down) {
        int before = dr.len;
        md_gap(&dr, now - r_t);
        if (dr.len != before) changed = true;
    }
    static uint32_t last_r;
    if (changed || now - last_r > 1000) { t_render(); last_r = now; }
}

static void t_kb_done(const char *text, void *arg)
{
    if (text && text[0]) { snprintf(t_compose, sizeof(t_compose), "%s", text); t_send_pending = true; }
    ui_pop();
}
static void kb_enter(lv_obj_t *root, void *arg) { keyboard_open(root, "Messaggio in Morse", "", false, 120, t_kb_done, NULL); }
static void kb_leave(void) { keyboard_close(); }
static bool kb_nav(nav_t ev) { return keyboard_nav(ev); }
static const app_t app_mcompose = {
    .name = "Messaggio", .enter = kb_enter, .leave = kb_leave, .nav = kb_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK | APP_ROUND_OK,
};

static void t_enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    bool r = SCR_ROUND;
    int cy = SCR_H / 2 - STATUS_H;
    if (!my_id) {
#ifdef SEISMO_SIM
        my_id = 0x1234ABCD;
#else
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        my_id = (uint32_t)mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5];
#endif
    }
#ifndef SEISMO_SIM
    if (!q) q = xQueueCreate(32, sizeof(mev_t));
#endif
    if (r) {
        l_top = mk(root, &font_s, C_DIM, 300, 0, cy - 150, true);
        lv_label_set_long_mode(l_top, LV_LABEL_LONG_SCROLL_CIRCULAR);
        t_rx = mk(root, &font_l, C_TEXT, 330, 0, cy - 110, true);
        t_tx = mk(root, &font_m, C_DIM, 340, 0, cy - 10, true);
        t_log_l = mk(root, &font_s, C_TEXT, 320, 0, cy + 40, true);
        l_help = mk(root, &font_s, C_DIM, 250, 0, cy + 130, true);
        lv_label_set_long_mode(l_help, LV_LABEL_LONG_SCROLL_CIRCULAR);
    } else {
        l_top = mk(root, &font_s, C_DIM, SCR_W - 24, 12, 4, false);
        t_rx = mk(root, &font_l, C_TEXT, SCR_W - 24, 12, 22, false);
        t_tx = mk(root, &font_m, C_DIM, SCR_W - 24, 12, 62, false);
        t_log_l = mk(root, &font_s, C_TEXT, SCR_W - 24, 12, 92, false);
        l_help = mk(root, &font_s, C_DIM, SCR_W - 24, 12, CONTENT_H - 20, false);
    }
    lv_label_set_long_mode(t_rx, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(t_tx, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(t_log_l, LV_LABEL_LONG_WRAP);
    ui_set_text(l_help, r ? "BOOT o dito: batti · destra: scrivi · su: pulisci"
                         : "BOOT: batti · destra: scrivi un messaggio · su: pulisci · sinistra: esci");
    if (!t_send_pending) {   // primo ingresso (non il ritorno dalla tastiera)
        md_init(&dk, cfg.wpm);
        md_init(&dr, cfg.wpm);
        t_ok = wifi_mgr_espnow_start(cfg.channel, on_now);
        t_hello = 0;
    }
    key_down = r_down = false;
    key_t0 = r_t = lv_tick_get();
    sound_on();
    t_render();
    t_tmr = lv_timer_create(t_tick, 10, NULL);
}

static void t_leave(void)
{
    if (t_tmr) { lv_timer_delete(t_tmr); t_tmr = NULL; }
    if (key_down && t_ok) send_pkt(P_KEY, 0, NULL);
    sound_off();
    if (ui_closing()) { wifi_mgr_espnow_stop(); t_ok = false; }   // verso la tastiera resta acceso
}

static bool t_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { ui_push(&app_mcompose, NULL); return true; }
    if (ev == NAV_NEXT) { md_clear(&dr); md_clear(&dk); t_render(); return true; }   // su: pulisce
    return ev == NAV_BTN || ev == NAV_QUICK || ev == NAV_PREV;
}

static const char *t_title(void *arg) { return "Telegrafo"; }

static const app_t app_mtele = {
    .name = "Telegrafo", .icon = LV_SYMBOL_WIFI,
    .enter = t_enter, .leave = t_leave, .nav = t_nav, .title = t_title,
    .flags = APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

/* ================= ascolto col microfono ================= */

static morse_dec_t dm;
static morse_det_t det;
static volatile bool m_run;
static volatile float m_level;
static volatile int m_freq;
static volatile bool m_on;
#ifndef SEISMO_SIM
static TaskHandle_t m_task;
static SemaphoreHandle_t m_mtx;

static void mic_task(void *arg)
{
    static int16_t st[240 * 2], mono[240];
    bool on = false;
    int run = 0;
    while (m_run) {
        int n = audio_mic_read(st, 240, 100);
        if (n < 240) continue;
        for (int i = 0; i < 240; i++) mono[i] = (int16_t)((st[2 * i] + st[2 * i + 1]) / 2);
        bool s = mdet_block(&det, mono, 240);
        xSemaphoreTake(m_mtx, portMAX_DELAY);
        if (s != on) {
            if (on) md_mark(&dm, run * 10);
            on = s;
            run = 0;
        }
        run++;
        if (!on) md_gap(&dm, run * 10);
        xSemaphoreGive(m_mtx);
        m_level = det.level;
        m_freq = det.freq;
        m_on = on;
    }
    m_task = NULL;
    vTaskDelete(NULL);
}
#endif

static lv_obj_t *m_bar;
static lv_timer_t *m_tmr;

static void m_tick(lv_timer_t *t)
{
    char b[200];
#ifndef SEISMO_SIM
    if (m_mtx) xSemaphoreTake(m_mtx, portMAX_DELAY);
#endif
    snprintf(b, sizeof(b), "%s", dm.len ? dm.text : "In ascolto…");
    int wpm = md_wpm(&dm);
#ifndef SEISMO_SIM
    if (m_mtx) xSemaphoreGive(m_mtx);
#endif
    ui_set_text(t_rx, b);
    if (m_on) snprintf(b, sizeof(b), "Tono a %d Hz · %d parole/min", m_freq, wpm);
    else snprintf(b, sizeof(b), "Fischia o suona un tono tra 400 e 1200 Hz · %d parole/min", wpm);
    ui_set_text(l_top, b);
    lv_bar_set_value(m_bar, (int)(m_level * 100), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_bar, m_on ? C_OK : C_DIM, LV_PART_INDICATOR);
}

static void m_enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    bool r = SCR_ROUND;
    int cy = SCR_H / 2 - STATUS_H;
    m_bar = lv_bar_create(root);
    lv_bar_set_range(m_bar, 0, 100);
    lv_obj_set_style_bg_color(m_bar, C_FAINT, LV_PART_MAIN);
    if (r) {
        l_top = mk(root, &font_s, C_DIM, 300, 0, cy - 150, true);
        lv_label_set_long_mode(l_top, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_size(m_bar, 260, 12);
        lv_obj_align(m_bar, LV_ALIGN_TOP_MID, 0, cy - 110);
        t_rx = mk(root, &font_l, C_TEXT, 340, 0, cy - 60, true);
        lv_label_set_long_mode(t_rx, LV_LABEL_LONG_WRAP);
        l_help = mk(root, &font_s, C_DIM, 250, 0, cy + 130, true);
    } else {
        l_top = mk(root, &font_s, C_DIM, SCR_W - 24, 12, 4, false);
        lv_obj_set_size(m_bar, SCR_W - 24, 10);
        lv_obj_set_pos(m_bar, 12, 26);
        t_rx = mk(root, &font_l, C_TEXT, SCR_W - 24, 12, 46, false);
        lv_label_set_long_mode(t_rx, LV_LABEL_LONG_WRAP);
        l_help = mk(root, &font_s, C_DIM, SCR_W - 24, 12, CONTENT_H - 20, false);
    }
    ui_set_text(l_help, "BOOT: cancella il testo");
    md_init(&dm, cfg.wpm);
    mdet_init(&det, AUDIO_RATE);
#ifndef SEISMO_SIM
    if (!m_mtx) m_mtx = xSemaphoreCreateMutex();
    if (!audio_init() || !audio_mic_start()) { ui_set_text(t_rx, "Microfono non disponibile"); return; }
    m_run = true;
    xTaskCreatePinnedToCore(mic_task, "morse_mic", 4096, NULL, 5, &m_task, 0);
#endif
    m_tmr = lv_timer_create(m_tick, 100, NULL);
}

static void m_leave(void)
{
    if (m_tmr) { lv_timer_delete(m_tmr); m_tmr = NULL; }
#ifndef SEISMO_SIM
    m_run = false;
    for (int i = 0; i < 30 && m_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    audio_mic_stop();
#endif
}

static bool m_nav(nav_t ev)
{
    if (ev == NAV_BTN) {
#ifndef SEISMO_SIM
        xSemaphoreTake(m_mtx, portMAX_DELAY);
#endif
        md_clear(&dm);
#ifndef SEISMO_SIM
        xSemaphoreGive(m_mtx);
#endif
        return true;
    }
    return false;
}

static const app_t app_mlisten = {
    .name = "Ascolta col microfono", .icon = ICON_MIC,
    .enter = m_enter, .leave = m_leave, .nav = m_nav, .flags = APP_NO_SLEEP | APP_ROUND_OK,
};

/* ================= tabella ================= */

static list_view_t lvw;
static int tb_sel;

static void tb_render(int dir)
{
    char m[40], s[48], pv[8], nx[8];
    char c = morse_nth(tb_sel);
    snprintf(m, sizeof(m), "%c", c);
    pretty(morse_code(c), s, sizeof(s));
    int k = koch_index(c);
    if (k >= 0 && k < cfg.level) { size_t l = strlen(s); snprintf(s + l, sizeof(s) - l, "  ·  nel corso"); }
    snprintf(pv, sizeof(pv), "%c", morse_nth(tb_sel - 1));
    snprintf(nx, sizeof(nx), "%c", morse_nth(tb_sel + 1));
    list_view_set(&lvw, ICON_EYE, tb_sel > 0 ? pv : NULL, m, s, tb_sel + 1 < morse_count() ? nx : NULL, tb_sel, morse_count(), dir);
}

static void tb_enter(lv_obj_t *root, void *arg)
{
    cfg_load();
    list_view_create(&lvw, root);
    sound_on();
    tb_render(0);
}

static void tb_leave(void) { sound_off(); }

static bool tb_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (tb_sel + 1 < morse_count()) { tb_sel++; tb_render(1); } return true;
    case NAV_PREV: if (tb_sel > 0) { tb_sel--; tb_render(-1); } return true;
    case NAV_SELECT: {
        char s[2] = {morse_nth(tb_sel), 0};
        play_text(s, cfg.wpm, cfg.wpm);
        return true;
    }
    default: return false;
    }
}

static const app_t app_mtable = {
    .name = "Tabella", .icon = LV_SYMBOL_LIST,
    .enter = tb_enter, .leave = tb_leave, .nav = tb_nav, .flags = APP_ROUND_OK,
};

/* ================= menu ================= */

static void v_learn(char *b, int n)
{
    cfg_load();
    char letters[48] = "";
    int k = 0;
    for (int i = 0; i < cfg.level && k < 40; i++) { letters[k++] = MORSE_KOCH[i]; if (i + 1 < cfg.level && k < 40) letters[k++] = ' '; }
    letters[k] = 0;
    snprintf(b, n, "Lezione %d: %s", cfg.level - 1, letters);
}
static void v_wpm(char *b, int n) { cfg_load(); snprintf(b, n, "%d parole al minuto (punto %d ms)", cfg.wpm, 1200 / cfg.wpm); }
static void j_wpm(int d) { int v = cfg.wpm + d; cfg.wpm = v < 5 ? 5 : v > 35 ? 35 : v; if (cfg.eff > cfg.wpm) cfg.eff = cfg.wpm; cfg_save(); }
static void v_eff(char *b, int n) { cfg_load(); snprintf(b, n, cfg.eff >= cfg.wpm ? "Come i segni (%d)" : "%d: pause più lunghe fra le lettere", cfg.eff); }
static void j_eff(int d) { int v = cfg.eff + d; cfg.eff = v < 3 ? 3 : v > cfg.wpm ? cfg.wpm : v; cfg_save(); }
static void v_freq(char *b, int n) { cfg_load(); snprintf(b, n, "%d Hz", cfg.freq); }
static void j_freq(int d) { int v = cfg.freq + d * 50; cfg.freq = v < 400 ? 400 : v > 1200 ? 1200 : v; cfg_save(); }
static void v_chan(char *b, int n) { cfg_load(); snprintf(b, n, "%d (uguale sugli altri Gadget)", cfg.channel); }
static void j_chan(int d) { int v = cfg.channel + d; cfg.channel = v < 1 ? 13 : v > 13 ? 1 : v; cfg_save(); }
static void a_reset(void)
{
    cfg_load();
    memset(cfg.ok, 0, sizeof(cfg.ok));
    memset(cfg.tot, 0, sizeof(cfg.tot));
    cfg.level = 2;
    cfg.recent = 0;
    cfg.recent_n = 0;
    cfg_save();
    ui_toast("Corso dall'inizio: K e M");
}

static const menu_item_t items[] = {
    {.icon = ICON_EYE, .label = "Impara ad ascoltare", .value = v_learn, .app = &app_mlearn},
    {.icon = ICON_BOLT, .label = "Impara a trasmettere", .hint = "Batti le lettere del corso con BOOT", .app = &app_mkey},
    {.icon = LV_SYMBOL_WIFI, .label = "Telegrafo", .hint = "Con altri Gadget vicini via ESP-NOW, senza rete", .app = &app_mtele},
    {.icon = ICON_MIC, .label = "Ascolta col microfono", .hint = "Decodifica un tono o un fischio", .app = &app_mlisten},
    {.icon = LV_SYMBOL_LIST, .label = "Tabella", .hint = "Tutti i caratteri, da sentire", .app = &app_mtable},
    {.icon = ICON_SLIDERS, .label = "Velocità", .value = v_wpm, .on_adjust = j_wpm},
    {.icon = ICON_SLIDERS, .label = "Velocità effettiva", .value = v_eff, .on_adjust = j_eff},
    {.icon = ICON_TUNER, .label = "Tono", .value = v_freq, .on_adjust = j_freq},
    {.icon = LV_SYMBOL_WIFI, .label = "Canale del Telegrafo", .value = v_chan, .on_adjust = j_chan},
    {.icon = LV_SYMBOL_REFRESH, .label = "Ricomincia il corso", .on_select = a_reset, .confirm = true},
};

menu_t morse_menu = {"Morse", items, sizeof(items) / sizeof(items[0]), 0, NULL};
