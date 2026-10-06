// app_wifiscan.c — reti Wi-Fi vicine con banda (2.4/5 GHz); swipe a destra = connetti.
// La stessa schermata si apre anche da Impostazioni › Wi-Fi (arg = titolo da mostrare).
#include "apps.h"
#include "keyboard.h"
#include "wifi_mgr.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

#define MAXN 32
static wifi_ap_t list[MAXN];
static int n, sel;
static uint32_t seen_gen;
static bool pending;
static list_view_t lv;
static char title_buf[48];

static int quality(int rssi)
{
    int q = (rssi + 92) * 100 / 62;
    return q < 0 ? 0 : q > 100 ? 100 : q;
}

static bool is_current(const wifi_ap_t *a)
{
    return wifi_mgr_state() == WIFI_CONNECTED && !strcmp(a->ssid, g_set.wifi_ssid);
}

static void render(int dir)
{
    if (n == 0) {
        list_view_set(&lv, LV_SYMBOL_WIFI, NULL,
                      wifi_mgr_scan_busy() || pending ? "Scansione…" : "Nessuna rete trovata",
                      wifi_mgr_scan_busy() || pending ? "" : "Swipe a destra per riprovare", NULL, 0, 0, dir);
        return;
    }
    const wifi_ap_t *a = &list[sel];
    char sub[112];
    snprintf(sub, sizeof(sub), "%s · %d dBm · %d%% · %s · ch %d%s%s",
             wifi_mgr_band(a->channel), a->rssi, quality(a->rssi), wifi_mgr_auth_name(a->auth), a->channel,
             a->auth == 0 ? "" : " " LV_SYMBOL_EYE_CLOSE, is_current(a) ? " · " LV_SYMBOL_OK " connesso" : "");
    list_view_set(&lv, LV_SYMBOL_WIFI, sel > 0 ? list[sel - 1].ssid : NULL,
                  a->ssid, sub, sel + 1 < n ? list[sel + 1].ssid : NULL, sel, n, dir);
}

static void enter(lv_obj_t *root, void *arg)
{
    list_view_create(&lv, root);
    wifi_mgr_scan_acquire();
    n = wifi_mgr_scan_results(list, MAXN);
    if (sel >= n) sel = 0;
    seen_gen = wifi_mgr_scan_gen();
    pending = true;
    render(0);
}

static void leave(void) { wifi_mgr_scan_release(); }

static void tick(void)
{
    if (pending && wifi_mgr_scan_start()) pending = false;
    uint32_t g = wifi_mgr_scan_gen();
    if (g != seen_gen) {
        seen_gen = g;
        // resta sulla rete selezionata anche se l'ordine cambia
        char keep[33] = "";
        if (sel < n) snprintf(keep, sizeof(keep), "%s", list[sel].ssid);
        n = wifi_mgr_scan_results(list, MAXN);
        if (sel >= n) sel = n ? n - 1 : 0;
        for (int i = 0; keep[0] && i < n; i++) if (!strcmp(list[i].ssid, keep)) { sel = i; break; }
    }
    render(0);
}

/* ---- schermata di esito della connessione ---- */

static char pending_ssid[33];
static lv_obj_t *res_icon, *res_main, *res_sub;

static void result_enter(lv_obj_t *root, void *arg)
{
    res_icon = lv_label_create(root);
    lv_obj_set_style_text_font(res_icon, &font_icon, 0);
    lv_obj_align(res_icon, LV_ALIGN_LEFT_MID, 40, 0);
    res_main = lv_label_create(root);
    lv_obj_set_style_text_font(res_main, &font_l, 0);
    lv_obj_align(res_main, LV_ALIGN_LEFT_MID, 130, -16);
    res_sub = lv_label_create(root);
    lv_obj_set_style_text_font(res_sub, &font_m, 0);
    lv_obj_set_style_text_color(res_sub, C_DIM, 0);
    lv_obj_set_width(res_sub, SCR_W - 150);
    lv_label_set_long_mode(res_sub, LV_LABEL_LONG_WRAP);
    lv_obj_align(res_main, LV_ALIGN_LEFT_MID, 130, -16);
    lv_obj_align(res_sub, LV_ALIGN_LEFT_MID, 130, 22);
}

static void result_tick(void)
{
    if (wifi_mgr_state() == WIFI_CONNECTED) {
        lv_label_set_text(res_icon, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(res_icon, C_OK, 0);
        lv_label_set_text(res_main, "Connesso");
        lv_obj_set_style_text_color(res_main, C_OK, 0);
        lv_label_set_text_fmt(res_sub, "%s · %s", pending_ssid, wifi_mgr_ip());
    } else if (wifi_mgr_conn_failed()) {
        bool no_ap = wifi_mgr_conn_reason() == 201;
        lv_label_set_text(res_icon, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(res_icon, C_WARN, 0);
        lv_label_set_text(res_main, no_ap ? "Rete non trovata" : "Password errata");
        lv_obj_set_style_text_color(res_main, C_WARN, 0);
        lv_label_set_text(res_sub, no_ap ? "Controlla che la rete sia a portata e a 2,4 GHz. Swipe a destra per riprovare."
                                         : "Swipe a destra per reinserire la password · sinistra per uscire");
    } else {
        lv_label_set_text(res_icon, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(res_icon, ui_accent(), 0);
        lv_label_set_text(res_main, "Connessione…");
        lv_obj_set_style_text_color(res_main, C_TEXT, 0);
        lv_label_set_text_fmt(res_sub, "%s", pending_ssid);
    }
}

static void start_connect(const char *pass);

static bool result_nav(nav_t ev)
{
    if (ev == NAV_SELECT) {
        if (wifi_mgr_state() == WIFI_CONNECTED) { ui_pop(); return true; }   // torna all'elenco
        // riprova: se protetta richiede di nuovo la password
        ui_pop();
        return true;
    }
    return false;
}

static const app_t app_wifi_result = {
    .name = "Connessione", .enter = result_enter, .tick = result_tick, .nav = result_nav,
    .flags = APP_NO_SLEEP,
};

/* ---- connessione: tastiera per la password ---- */

static void start_connect(const char *pass)
{
    if (wifi_mgr_connect(pending_ssid, pass)) ui_push(&app_wifi_result, NULL);
    else ui_toast("Dati non validi");
}

static void got_pass(const char *pass, void *arg)
{
    keyboard_close();
    ui_pop();   // chiude la tastiera
    if (!pass) return;
    start_connect(pass);
}

static void kb_enter(lv_obj_t *root, void *arg)
{
    char title[64];
    snprintf(title, sizeof(title), "Password di %s", pending_ssid);
    keyboard_open(root, title, "", true, 64, got_pass, NULL);
}

static void kb_leave(void) { keyboard_close(); }
static bool kb_nav(nav_t ev) { return keyboard_nav(ev); }

static const app_t app_wifi_pass = {
    .name = "Password", .enter = kb_enter, .leave = kb_leave, .nav = kb_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

static void connect_sel(void)
{
    if (!n) return;
    const wifi_ap_t *a = &list[sel];
    snprintf(pending_ssid, sizeof(pending_ssid), "%s", a->ssid);
    if (a->auth == 0) start_connect("");   // rete aperta: niente password
    else ui_push(&app_wifi_pass, NULL);
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n) { sel++; render(+1); } return true;
    case NAV_PREV: if (sel > 0) { sel--; render(-1); } return true;
    case NAV_SELECT:
        if (n) connect_sel();
        else { pending = true; render(0); }   // elenco vuoto: nuova scansione
        return true;
    case NAV_QUICK: pending = true; render(0); return true;  // tocco prolungato (o BOOT tenuto) = nuova scansione
    default: return false;
    }
}

static const char *title(void *arg)
{
    snprintf(title_buf, sizeof(title_buf), "%s · %d reti", arg ? (const char *)arg : "Scanner Wi-Fi", n);
    return title_buf;
}

const app_t app_wifiscan = {
    .name = "Scanner Wi-Fi", .icon = LV_SYMBOL_WIFI,
    .enter = enter, .leave = leave, .nav = nav, .tick = tick, .title = title,
    .flags = APP_OWN_QUICK,   // il tocco prolungato rifà la scansione
};
