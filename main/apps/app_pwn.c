// app_pwn.c — "radar" passivo con personalità. Faccia, statistiche, Pokédex, impostazioni.
// Tutto l'ascolto è passivo: nessun pacchetto trasmesso, nessuna deautenticazione.
#include "apps.h"
#include "keyboard.h"
#include "pwn.h"
#include "sd.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

/* ================= faccia (personaggio originale) ================= */
// Espressioni in stile emoticon, disegnate da noi.
enum { M_WAKE, M_LOOK, M_HAPPY, M_EXCITED, M_BORED, M_SLEEP, M_COOL, N_MOODS };
// NB: gli occhi usano font_xl (solo codepoint 32–95: maiuscole, cifre, simboli)
// e le bocche font_l (ASCII + pochi simboli). Qui si usano solo glifi presenti
// in quei font, altrimenti il carattere non viene disegnato.
static const char *eyes[N_MOODS]  = {"O   O", "'   '", "^   ^", "*   *", "-   -", "Z   Z", "=   ="};
static const char *mouth[N_MOODS] = {"  ___  ", "   u   ", "  \\_/  ", "  \\O/  ", "   —   ", "  ...  ", "  ~~~  "};
static const char *quip[N_MOODS]  = {
    "mi sveglio…", "guardo in giro", "che bella rete!", "handshake!!", "qui è tranquillo", "zzz… poca gente", "modalità caccia",
};

static lv_obj_t *f_eyes, *f_mouth, *f_name, *f_quip, *f_stat, *f_ch, *f_bar;
static lv_timer_t *tmr;
static int mood = M_WAKE;
static uint32_t last_hs, last_net_total;
static uint32_t mood_until;

static int pick_mood(const pwn_stats_t *s)
{
    uint32_t now = lv_tick_get();
    if (now < mood_until) return mood;   // mantiene l'espressione di un evento
    if (!pwn_running()) return M_SLEEP;
    int near = pwn_aps_near();
    if (near == 0) return M_BORED;
    if (near >= 6) return M_COOL;
    return M_LOOK;
}

static void face_tick(void)
{
    pwn_stats_t s;
    pwn_get_stats(&s);

    // eventi → espressioni temporanee
    if (s.handshakes != last_hs) { last_hs = s.handshakes; mood = M_EXCITED; mood_until = lv_tick_get() + 2500; }
    else if (s.nets_total != last_net_total) { last_net_total = s.nets_total; mood = M_HAPPY; mood_until = lv_tick_get() + 1500; }
    else mood = pick_mood(&s);

    // ogni mezzo secondo: le etichette si toccano solo se cambiano (meno ridisegni)
    char b[64];
    ui_set_text(f_eyes, eyes[mood]);
    ui_set_text(f_mouth, mouth[mood]);
    ui_set_text(f_quip, pwn_running() ? pwn_last_event() : quip[M_SLEEP]);

    snprintf(b, sizeof(b), "%s  Lv%d", s.name, s.level);
    ui_set_text(f_name, b);
    snprintf(b, sizeof(b), "reti %lu · pasti %lu · pkt %lu", (unsigned long)s.nets_total,
             (unsigned long)s.handshakes, (unsigned long)s.pkts);
    ui_set_text(f_stat, b);
    if (pwn_running()) snprintf(b, sizeof(b), "ch %d · vicine %d", pwn_recent_channel(), pwn_aps_near());
    else snprintf(b, sizeof(b), "fermo");
    ui_set_text(f_ch, b);

    // barra XP verso il livello successivo (stessa curva del motore)
    uint32_t lv = s.level > 9999 ? 0 : s.level;
    uint32_t base = 50u * lv * lv + 50u * lv;
    uint32_t next = 50u * (lv + 1) * (lv + 1) + 50u * (lv + 1);
    int pct = 0;
    if (next > base && s.xp >= base) pct = (int)((uint64_t)(s.xp - base) * 100 / (next - base));
    lv_bar_set_value(f_bar, pct < 0 ? 0 : pct > 100 ? 100 : pct, LV_ANIM_OFF);
}

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    lv_obj_set_pos(l, x, y);
    return l;
}

static void face_enter(lv_obj_t *root, void *arg)
{
    // faccia a sinistra
    f_eyes = mk(root, &font_xl, C_TEXT, 24, 14);
    f_mouth = mk(root, &font_l, ui_accent(), 30, 86);
    // info a destra
    f_name = mk(root, &font_l, C_TEXT, 300, 12);
    f_quip = mk(root, &font_m, ui_accent(), 300, 52);
    lv_obj_set_width(f_quip, SCR_W - 312);
    lv_label_set_long_mode(f_quip, LV_LABEL_LONG_DOT);
    f_stat = mk(root, &font_m, C_DIM, 300, 84);
    f_ch = mk(root, &font_s, C_DIM, 300, SCR_H - 40);

    f_bar = lv_bar_create(root);
    lv_obj_set_size(f_bar, SCR_W - 312, 8);
    lv_obj_set_pos(f_bar, 300, SCR_H - 18);
    lv_obj_set_style_bg_color(f_bar, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(f_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(f_bar, ui_accent(), LV_PART_INDICATOR);

    if (!pwn_running()) pwn_start();
    face_tick();
    tmr = lv_timer_create((lv_timer_cb_t)face_tick, 500, NULL);
}

static void face_leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    // il motore continua a girare: si ferma dal menu o uscendo dall'app radar
}

static bool face_nav(nav_t ev)
{
    if (ev == NAV_SELECT) {
        if (pwn_running()) { pwn_stop(); ui_toast("In pausa"); }
        else { pwn_start(); ui_toast("Caccia avviata"); }
        return true;
    }
    return false;
}

static const app_t app_pwn_face = {
    .name = "Radar", .icon = ICON_GHOST,
    .enter = face_enter, .leave = face_leave, .nav = face_nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};

/* ================= Pokédex ================= */

static int pk_sel;
static list_view_t pk_lv;
static char pk_title[48];

static void pk_render(int dir)
{
    int n = pwn_net_count();
    if (n == 0) {
        list_view_set(&pk_lv, LV_SYMBOL_WIFI, NULL, "Nessuna rete ancora",
                      "Avvia il radar per popolare il Pokédex", NULL, 0, 0, dir);
        return;
    }
    if (pk_sel >= n) pk_sel = n - 1;
    pwn_net_t nw;
    pwn_net_get(pk_sel, &nw);
    char main[40], sub[112], prev[40] = "", next[40] = "";
    pwn_net_t tmp;
    snprintf(main, sizeof(main), "%s", nw.ssid[0] ? nw.ssid : "(rete nascosta)");
    const char *sec = nw.auth >= 2 ? "WPA" : nw.auth == 1 ? "WEP" : "aperta";
    snprintf(sub, sizeof(sub), "%s · ch %d · %d dBm · %s%s", sec, nw.channel, nw.rssi,
             nw.channel >= 32 ? "5 GHz" : "2.4 GHz", nw.handshake ? " · " LV_SYMBOL_OK " pasto" : "");
    if (pk_sel > 0) { pwn_net_get(pk_sel - 1, &tmp); snprintf(prev, sizeof(prev), "%s", tmp.ssid[0] ? tmp.ssid : "(nascosta)"); }
    if (pk_sel + 1 < n) { pwn_net_get(pk_sel + 1, &tmp); snprintf(next, sizeof(next), "%s", tmp.ssid[0] ? tmp.ssid : "(nascosta)"); }
    list_view_set(&pk_lv, nw.handshake ? ICON_TROPHY : LV_SYMBOL_WIFI, prev, main, sub, next, pk_sel, n, dir);
}

static void pk_enter(lv_obj_t *root, void *arg) { list_view_create(&pk_lv, root); pk_render(0); }
static void pk_tick(void) { pk_render(0); }
static bool pk_nav(nav_t ev)
{
    int n = pwn_net_count();
    if (ev == NAV_NEXT && pk_sel + 1 < n) { pk_sel++; pk_render(+1); return true; }
    if (ev == NAV_PREV && pk_sel > 0) { pk_sel--; pk_render(-1); return true; }
    return ev == NAV_NEXT || ev == NAV_PREV;
}
static const char *pk_title_fn(void *a) { snprintf(pk_title, sizeof(pk_title), "Pokédex reti · %d", pwn_net_count()); return pk_title; }

static const app_t app_pwn_dex = {
    .name = "Pokédex reti", .icon = LV_SYMBOL_LIST,
    .enter = pk_enter, .nav = pk_nav, .tick = pk_tick, .title = pk_title_fn,
};

/* ================= impostazioni ================= */

static char nm_buf[21];

static void got_name(const char *t, void *arg)
{
    keyboard_close();
    ui_pop();
    if (t && t[0]) {
        strlcpy(g_set.pwn_name, t, sizeof(g_set.pwn_name));
        settings_save();
        pwn_set_name(g_set.pwn_name);   // si vede subito, anche col radar acceso
        ui_toast("Nome salvato");
    }
}
static void name_enter(lv_obj_t *root, void *arg)
{
    strlcpy(nm_buf, g_set.pwn_name, sizeof(nm_buf));
    keyboard_open(root, "Nome del radar", nm_buf, false, 20, got_name, NULL);
}
static void name_leave(void) { keyboard_close(); }
static bool name_nav(nav_t ev) { return keyboard_nav(ev); }
static const app_t app_pwn_name = {
    .name = "Nome", .enter = name_enter, .leave = name_leave, .nav = name_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

static void v_name(char *b, int n) { snprintf(b, n, "%s", g_set.pwn_name); }
static void v_pcap(char *b, int n) { snprintf(b, n, "%s", g_set.pwn_pcap ? "Salva handshake su SD" : "Non salvare"); }
static void a_pcap(void) { g_set.pwn_pcap = !g_set.pwn_pcap; settings_save(); }
static void v_ai(char *b, int n) { snprintf(b, n, "%s", g_set.pwn_ai ? "Reattiva all'ambiente" : "Statica"); }
static void a_ai(void) { g_set.pwn_ai = !g_set.pwn_ai; settings_save(); }
static void v_sd(char *b, int n) { snprintf(b, n, sd_ok() ? "microSD presente" : "microSD assente"); }
static void a_reset(void) { pwn_reset_pokedex(); ui_toast("Pokédex e livelli azzerati"); }

static const menu_item_t set_items[] = {
    {.icon = LV_SYMBOL_EDIT, .label = "Nome", .value = v_name, .app = &app_pwn_name},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Cattura pcap", .value = v_pcap, .on_select = a_pcap},
    {.icon = ICON_GHOST, .label = "Personalità", .value = v_ai, .on_select = a_ai},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Stato microSD", .value = v_sd},
    {.icon = LV_SYMBOL_WARNING, .label = "Azzera Pokédex e livelli", .on_select = a_reset, .confirm = true},
};
static menu_t set_menu = {"Radar › Impostazioni", set_items, sizeof(set_items) / sizeof(set_items[0]), 0};

/* ================= menu principale del radar ================= */

static void v_state(char *b, int n)
{
    pwn_stats_t s; pwn_get_stats(&s);
    if (pwn_running()) snprintf(b, n, "In caccia · Lv%d · %lu reti", s.level, (unsigned long)s.nets_total);
    else snprintf(b, n, "Fermo · Lv%d · %lu reti", s.level, (unsigned long)s.nets_total);
}
static void a_toggle(void)
{
    if (pwn_running()) { pwn_stop(); ui_toast("Radar fermo · Wi-Fi ripristinato"); }
    else { pwn_start(); ui_toast("Radar avviato · Wi-Fi sospeso"); }
}

static const menu_item_t radar_items[] = {
    {.icon = ICON_GHOST, .label = "Apri il radar", .value = v_state, .app = &app_pwn_face},
    {.icon = LV_SYMBOL_PLAY, .label = "Avvia / ferma", .value = v_state, .on_select = a_toggle},
    {.icon = LV_SYMBOL_LIST, .label = "Pokédex reti", .app = &app_pwn_dex},
    {.icon = LV_SYMBOL_SETTINGS, .label = "Impostazioni", .app = &app_menu, .arg = &set_menu},
    {.icon = ICON_INFO, .label = "Cos'è", .hint = "Ascolto passivo: reti, dispositivi, handshake captati senza trasmettere nulla"},
};
menu_t radar_menu = {"Radar", radar_items, sizeof(radar_items) / sizeof(radar_items[0]), 0};
