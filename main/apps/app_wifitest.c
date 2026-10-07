// app_wifitest.c — Tester Wi-Fi: scegli una rete e gira per le stanze guardando quanto
// arriva il segnale. Prima schermata: le reti vicine (una voce per nome, anche se esce da
// più apparecchi). Seconda: la potenza in dBm, un indicatore che sale e scende, il
// grafico degli ultimi 30 secondi e, se la rete esce da più apparecchi (router, ripetitori,
// mesh), quale arriva meglio in quel punto. BOOT accende un bip che sale con il segnale,
// per camminare senza guardare lo schermo. Non salva nulla.
//
// Per aggiornare in fretta si scansiona solo quella rete e solo sui suoi canali (una
// misura ogni 2-4 decimi di secondo); ogni tanto si guardano tutti i canali, per trovare
// apparecchi nuovi.
#include "apps.h"
#include "audio.h"
#include "wifi_mgr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ================= colori e giudizio ================= */

static int level(float rssi)   // 0–100 (−90 dBm → 0, −35 → 100)
{
    int q = (int)lroundf((rssi + 90) * 100 / 55);
    return q < 0 ? 0 : q > 100 ? 100 : q;
}
static const char *verdict(float rssi)
{
    return rssi >= -55 ? "Ottimo" : rssi >= -65 ? "Buono" : rssi >= -72 ? "Discreto" : rssi >= -80 ? "Scarso" : "Pessimo";
}
static lv_color_t verdict_color(float rssi)
{
    return rssi >= -65 ? C_OK : rssi >= -72 ? lv_color_hex(0xFFB020) : rssi >= -80 ? lv_color_hex(0xFF8A3D) : C_WARN;
}

static lv_obj_t *mkl(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

/* ================= elenco delle reti ================= */

#define MAXN 32
typedef struct {
    char ssid[33];
    int8_t rssi;        // il più forte
    uint8_t aps;        // apparecchi
    uint16_t ch_mask;   // canali (bit n = canale n)
} net_t;
static net_t nets[MAXN];
static int n_nets, sel;
static uint32_t seen_gen;
static bool pending;
static list_view_t lv;

static void collect(void)
{
    static wifi_ap_t a[MAXN];
    int n = wifi_mgr_scan_results(a, MAXN);
    n_nets = 0;
    for (int i = 0; i < n; i++) {
        int k = 0;
        while (k < n_nets && strcmp(nets[k].ssid, a[i].ssid)) k++;
        if (k == n_nets) {
            n_nets++;
            strlcpy(nets[k].ssid, a[i].ssid, sizeof(nets[k].ssid));
            nets[k].rssi = a[i].rssi;
            nets[k].aps = 0;
            nets[k].ch_mask = 0;
        }
        if (a[i].rssi > nets[k].rssi) nets[k].rssi = a[i].rssi;
        nets[k].aps++;
        if (a[i].channel < 16) nets[k].ch_mask |= 1u << a[i].channel;
    }
    // la più forte prima
    for (int i = 1; i < n_nets; i++)
        for (int j = i; j > 0 && nets[j].rssi > nets[j - 1].rssi; j--) {
            net_t t = nets[j]; nets[j] = nets[j - 1]; nets[j - 1] = t;
        }
}

static void chans(char *b, int n, uint16_t mask)
{
    int o = snprintf(b, n, "ch");
    for (int c = 1; c < 16 && o < n; c++) if (mask & (1u << c)) o += snprintf(b + o, n - o, " %d", c);
}

static void render(int dir)
{
    if (!n_nets) {
        bool busy = wifi_mgr_scan_busy() || pending;
        list_view_set(&lv, LV_SYMBOL_WIFI, NULL, busy ? "Scansione…" : "Nessuna rete trovata",
                      busy ? "" : "Tieni premuto per riprovare", NULL, 0, 0, dir);
        return;
    }
    const net_t *t = &nets[sel];
    char sub[96], ch[40];
    chans(ch, sizeof(ch), t->ch_mask);
    if (t->aps > 1) snprintf(sub, sizeof(sub), "%d dBm · %s · %d apparecchi · %s", t->rssi, verdict(t->rssi), t->aps, ch);
    else snprintf(sub, sizeof(sub), "%d dBm · %s · %s", t->rssi, verdict(t->rssi), ch);
    list_view_set(&lv, LV_SYMBOL_WIFI, sel > 0 ? nets[sel - 1].ssid : NULL, t->ssid, sub,
                  sel + 1 < n_nets ? nets[sel + 1].ssid : NULL, sel, n_nets, dir);
}

static void enter(lv_obj_t *root, void *arg)
{
    list_view_create(&lv, root);
    wifi_mgr_scan_acquire();
    if (!n_nets) collect();   // tornando dalla misura i risultati sono solo di quella rete
    if (sel >= n_nets) sel = 0;
    seen_gen = wifi_mgr_scan_gen();
    pending = true;
    render(0);
}

static void leave(void) { wifi_mgr_scan_release(); }

static void tick(void)
{
    if (pending && wifi_mgr_scan_start()) pending = false;
    uint32_t g = wifi_mgr_scan_gen();
    if (g != seen_gen && !pending) {
        seen_gen = g;
        char keep[33] = "";   // resta sulla rete scelta anche se l'ordine cambia
        if (sel < n_nets) strlcpy(keep, nets[sel].ssid, sizeof(keep));
        collect();
        if (sel >= n_nets) sel = n_nets ? n_nets - 1 : 0;
        for (int i = 0; keep[0] && i < n_nets; i++) if (!strcmp(nets[i].ssid, keep)) { sel = i; break; }
        pending = true;   // continua ad aggiornare l'elenco
    }
    render(0);
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n_nets) { sel++; render(+1); } return true;
    case NAV_PREV: if (sel > 0) { sel--; render(-1); } return true;
    case NAV_SELECT: if (n_nets) ui_push(&app_wifitest_meter, &nets[sel]); return true;
    case NAV_QUICK: n_nets = 0; pending = true; render(0); return true;
    default: return false;
    }
}

const app_t app_wifitest = {
    .name = "Tester Wi-Fi", .icon = LV_SYMBOL_WIFI,
    .enter = enter, .leave = leave, .nav = nav, .tick = tick,
    .flags = APP_OWN_QUICK | APP_ROUND_OK,
};

/* ================= misura di una rete ================= */

#define MAXAP   8
#define HIST    60     // punti del grafico, uno ogni mezzo secondo
#define LOST    4      // misure senza vederlo: l'apparecchio è fuori portata
#define FULL_EVERY 12  // ogni quante misure si guardano tutti i canali

typedef struct {
    uint8_t bssid[6];
    uint8_t ch, num;   // canale, numero (in ordine di scoperta)
    float rssi;        // media mobile
    uint8_t miss;      // misure consecutive in cui mancava
} ap_t;

static char m_ssid[33];
static uint16_t m_mask;
static ap_t aps[MAXAP];
static int n_aps, m_scans, best = -1;
static uint32_t m_gen;
static bool m_pending, beep_on, audio_ok;
static float shown = -100;   // valore mostrato (segue la misura)
static lv_timer_t *m_tmr;
static int hist_div;

static lv_obj_t *w_bar, *w_arc[2], *l_ssid, *l_val, *l_unit, *l_verdict, *l_best, *l_other, *l_hint, *chart;
static lv_chart_series_t *ser;

// bip: più alto con il segnale più forte
static volatile float bp_step;
static volatile uint32_t bp_n;
static float bp_ph;
static void beep_synth(int16_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (bp_n) {
            float s = sinf(bp_ph);
            b[i] = (int16_t)((s > 0.3f ? 1 : s < -0.3f ? -1 : s / 0.3f) * 8000);
            bp_ph += bp_step;
            if (bp_ph > 6.2831853f) bp_ph -= 6.2831853f;
            bp_n--;
        } else b[i] = 0;
    }
}
static void beep(int hz, int ms)
{
    if (!beep_on || !audio_ok) return;
    bp_step = 6.2831853f * hz / AUDIO_RATE;
    bp_n = (uint32_t)AUDIO_RATE * ms / 1000;
}

static void show_hint(void)
{
    if (SCR_ROUND) ui_set_text(l_hint, beep_on ? "Bip: sì (destra)" : "Bip: no (destra)");
    else ui_set_text(l_hint, beep_on ? "Bip: sì (destra o BOOT)" : "Bip: no (destra o BOOT)");
}

static void ap_name(char *b, int n, const ap_t *a)
{
    snprintf(b, n, "AP %d (…%02X:%02X, ch %d)", a->num, a->bssid[4], a->bssid[5], a->ch);
}

static void take_scan(void)
{
    static wifi_ap_t r[MAXN];
    int n = wifi_mgr_scan_results(r, MAXN);
    for (int k = 0; k < n_aps; k++) if (aps[k].miss < 255) aps[k].miss++;
    for (int i = 0; i < n; i++) {
        if (strcmp(r[i].ssid, m_ssid)) continue;
        if (r[i].channel < 16) m_mask |= 1u << r[i].channel;
        int k = 0;
        while (k < n_aps && memcmp(aps[k].bssid, r[i].bssid, 6)) k++;
        if (k == n_aps) {
            if (n_aps == MAXAP) continue;
            n_aps++;
            memcpy(aps[k].bssid, r[i].bssid, 6);
            aps[k].num = k + 1;
            aps[k].rssi = r[i].rssi;
        } else {
            // media mobile veloce: segue i passi ma toglie lo sfarfallio
            aps[k].rssi = aps[k].miss >= LOST ? r[i].rssi : aps[k].rssi * 0.4f + r[i].rssi * 0.6f;
        }
        aps[k].ch = r[i].channel;
        aps[k].miss = 0;
    }
    // il migliore, con un po' di isteresi per non saltare di continuo fra due quasi uguali
    int b = -1;
    for (int k = 0; k < n_aps; k++) if (aps[k].miss < LOST && (b < 0 || aps[k].rssi > aps[b].rssi)) b = k;
    if (best >= 0 && b >= 0 && b != best && aps[best].miss < LOST && aps[b].rssi < aps[best].rssi + 3) b = best;
    best = b;
    m_scans++;
    if (best >= 0) beep(300 + level(aps[best].rssi) * 17, 35);
}

static void show(void)
{
    char b[96];
    bool have = best >= 0;
    float v = have ? aps[best].rssi : -100;
    int q = have ? level(v) : 0;
    lv_color_t col = have ? verdict_color(v) : C_DIM;
    if (have) snprintf(b, sizeof(b), "%d", (int)lroundf(v));
    else snprintf(b, sizeof(b), "--");
    ui_set_text(l_val, b);
    ui_set_text_color(l_val, have ? C_TEXT : C_DIM);
    lv_obj_update_layout(l_val);
    lv_obj_align_to(l_unit, l_val, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -14);
    ui_set_text(l_verdict, have ? verdict(v) : m_scans ? "Fuori portata" : "Misuro…");
    ui_set_text_color(l_verdict, col);
    if (w_bar) {
        lv_bar_set_value(w_bar, q, LV_ANIM_ON);
        lv_obj_set_style_bg_color(w_bar, col, LV_PART_INDICATOR);
    }
    for (int i = 0; i < 2; i++) if (w_arc[i]) {
        lv_arc_set_value(w_arc[i], q);
        lv_obj_set_style_arc_color(w_arc[i], col, LV_PART_INDICATOR);
    }
    // apparecchi: il migliore e gli altri
    if (n_aps <= 1) {
        ui_set_text(l_best, have ? "Un solo apparecchio" : "");
        if (have) { char nm[40]; ap_name(nm, sizeof(nm), &aps[best]); snprintf(b, sizeof(b), "%s", nm); ui_set_text(l_other, b); }
        else ui_set_text(l_other, "");
    } else {
        if (have) {
            char nm[40];
            ap_name(nm, sizeof(nm), &aps[best]);
            snprintf(b, sizeof(b), LV_SYMBOL_OK " Migliore qui: %s", nm);
        } else snprintf(b, sizeof(b), "Nessun apparecchio in portata");
        ui_set_text(l_best, b);
        int o = 0;
        b[0] = 0;
        for (int k = 0; have && k < n_aps && o < (int)sizeof(b) - 20; k++) {
            if (k == best) continue;
            if (aps[k].miss < LOST) o += snprintf(b + o, sizeof(b) - o, "%sAP %d %d dBm", o ? " · " : "Altri: ", aps[k].num, (int)lroundf(aps[k].rssi));
            else o += snprintf(b + o, sizeof(b) - o, "%sAP %d fuori portata", o ? " · " : "Altri: ", aps[k].num);
        }
        ui_set_text(l_other, b);
    }
    shown = v;
}

static void m_tick(lv_timer_t *t)
{
    uint32_t g = wifi_mgr_scan_gen();
    if (g != m_gen && !wifi_mgr_scan_busy()) {
        m_gen = g;
        if (!m_pending) { take_scan(); show(); }
        m_pending = false;
    }
    if (!wifi_mgr_scan_busy()) {
        // di solito solo i canali della rete; ogni tanto tutti (apparecchi nuovi o spostati)
        uint16_t mask = (m_scans % FULL_EVERY == FULL_EVERY - 1 || !m_mask) ? 0 : m_mask;
        if (wifi_mgr_scan_start_ex(m_ssid, mask)) m_gen = wifi_mgr_scan_gen();
    }
    if (++hist_div >= 5) {   // 100 ms × 5: un punto ogni mezzo secondo
        hist_div = 0;
        lv_chart_set_next_value(chart, ser, best >= 0 ? (int32_t)lroundf(shown) : LV_CHART_POINT_NONE);
    }
}

static lv_obj_t *mk_arc(lv_obj_t *root, int start, int end, bool reverse)
{
    lv_obj_t *a = lv_arc_create(root);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, SCR_W - 16, SCR_H - 16);
    lv_obj_set_pos(a, 8, 8 - STATUS_H);
    lv_arc_set_bg_angles(a, start, end);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_mode(a, reverse ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, C_OK, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static void m_enter(lv_obj_t *root, void *arg)
{
    if (arg) {   // nuova rete (tornando da un'altra schermata si riprende quella di prima)
        const net_t *t = arg;
        strlcpy(m_ssid, t->ssid, sizeof(m_ssid));
        m_mask = t->ch_mask;
        n_aps = 0;
        best = -1;
        m_scans = 0;
    }
    bool r = SCR_ROUND;
    w_bar = NULL;
    w_arc[0] = w_arc[1] = NULL;
    chart = lv_chart_create(root);
    if (r) {
        // due archi sui lati che salgono insieme dal basso, come un livello
        w_arc[0] = mk_arc(root, 90, 222, false);    // sinistra: dal basso verso l'alto
        w_arc[1] = mk_arc(root, 318, 90, true);     // destra: idem, al contrario
        const int cy = SCR_H / 2 - STATUS_H;
        l_ssid = mkl(root, &font_m, C_TEXT);
        lv_obj_set_width(l_ssid, 300);
        lv_obj_set_style_text_align(l_ssid, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l_ssid, LV_ALIGN_TOP_MID, 0, cy - 146);
        l_val = mkl(root, &font_xl, C_TEXT);
        lv_label_set_long_mode(l_val, LV_LABEL_LONG_CLIP);
        lv_obj_align(l_val, LV_ALIGN_TOP_MID, -26, cy - 112);   // a sinistra quanto basta per "dBm"
        l_unit = mkl(root, &font_m, C_DIM);
        lv_label_set_text(l_unit, "dBm");
        l_verdict = mkl(root, &font_l, C_OK);
        lv_obj_set_width(l_verdict, 300);
        lv_obj_set_style_text_align(l_verdict, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l_verdict, LV_ALIGN_TOP_MID, 0, cy - 18);
        lv_obj_set_size(chart, 270, 56);
        lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, cy + 36);
        lv_obj_t *ls[] = {l_best = mkl(root, &font_s, C_TEXT), l_other = mkl(root, &font_s, C_DIM),
                          l_hint = mkl(root, &font_s, C_DIM)};
        int ys[] = {cy + 98, cy + 124, cy + 150}, ws[] = {330, 300, 240};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_width(ls[i], ws[i]);
            lv_obj_set_style_text_align(ls[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(ls[i], LV_ALIGN_TOP_MID, 0, ys[i]);
        }
    } else {
        // indicatore verticale a sinistra, numeri al centro, grafico e apparecchi a destra
        w_bar = lv_bar_create(root);
        lv_obj_set_size(w_bar, 30, 136);
        lv_obj_set_pos(w_bar, 12, 6);
        lv_bar_set_range(w_bar, 0, 100);
        lv_obj_set_style_bg_color(w_bar, C_FAINT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(w_bar, 8, LV_PART_MAIN);
        lv_obj_set_style_radius(w_bar, 8, LV_PART_INDICATOR);
        lv_obj_set_style_anim_duration(w_bar, 250, LV_PART_MAIN);
        l_ssid = mkl(root, &font_m, C_TEXT);
        lv_obj_set_width(l_ssid, 260);
        lv_obj_set_pos(l_ssid, 58, 2);
        l_val = mkl(root, &font_xl, C_TEXT);
        lv_label_set_long_mode(l_val, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(l_val, 56, 28);
        l_unit = mkl(root, &font_m, C_DIM);
        lv_label_set_text(l_unit, "dBm");
        l_verdict = mkl(root, &font_m, C_OK);
        lv_obj_set_width(l_verdict, 260);
        lv_obj_set_pos(l_verdict, 58, 114);
        lv_obj_set_size(chart, 290, 70);
        lv_obj_set_pos(chart, 338, 6);
        l_best = mkl(root, &font_s, C_TEXT);
        l_other = mkl(root, &font_s, C_DIM);
        l_hint = mkl(root, &font_s, C_DIM);
        lv_obj_t *ls[] = {l_best, l_other, l_hint};
        for (int i = 0; i < 3; i++) { lv_obj_set_width(ls[i], 292); lv_obj_set_pos(ls[i], 338, 82 + i * 22); }
    }
    ui_set_text(l_ssid, m_ssid);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HIST);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -95, -30);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    ser = lv_chart_add_series(chart, ui_accent(), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ser, LV_CHART_POINT_NONE);
    show_hint();

    if (beep_on && (audio_ok = audio_init())) audio_start(beep_synth);
    wifi_mgr_scan_acquire();
    m_gen = wifi_mgr_scan_gen();
    m_pending = true;   // una scansione già in corso è di prima (magari di tutte le reti)
    hist_div = 0;
    show();
    m_tmr = lv_timer_create(m_tick, 100, NULL);
}

static void m_leave(void)
{
    if (m_tmr) { lv_timer_delete(m_tmr); m_tmr = NULL; }
    if (audio_ok) audio_stop_if(beep_synth);
    audio_ok = false;
    wifi_mgr_scan_release();
}

static bool m_nav(nav_t ev)
{
    if (ev != NAV_BTN && ev != NAV_SELECT) return false;
    beep_on = !beep_on;
    if (beep_on && !audio_ok && (audio_ok = audio_init())) audio_start(beep_synth);
    if (!beep_on && audio_ok) { audio_stop_if(beep_synth); audio_ok = false; }
    if (beep_on && !audio_ok) { beep_on = false; ui_toast("Audio non disponibile"); }
    else ui_toast(beep_on ? "Bip acceso: più alto, più segnale" : "Bip spento");
    show_hint();
    return true;
}

static const char *m_title(void *arg) { return "Tester Wi-Fi"; }

const app_t app_wifitest_meter = {
    .name = "Tester Wi-Fi", .icon = LV_SYMBOL_WIFI,
    .enter = m_enter, .leave = m_leave, .nav = m_nav, .title = m_title,
    .flags = APP_NO_SLEEP | APP_ROUND_OK,
};
