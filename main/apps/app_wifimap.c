// app_wifimap.c — Mappa Wi-Fi: cammina per l'edificio e il Gadget costruisce un grafo
// delle reti (vedi wifimap.c). I punti sono le reti (colore: fissa, mobile, hotspot), i fili
// collegano le reti viste insieme, il punto bianco sei tu con la scia del percorso.
// Su/giù sceglie una rete, destra apre l'elenco, BOOT mette in pausa. La mappa si salva
// sulla microSD (wifimap/) e si riprende la volta dopo.
#include "apps.h"
#include "wifimap.h"
#include "wifi_mgr.h"
#include "sd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_attr.h"

#define MAP_DIR   SD_MOUNT "/wifimap"
#define MAP_FILE  MAP_DIR "/mappa.bin"
#define MAX_EDGES 80
#define MAX_MOB   6

static bool loaded, show_mobile = true;

static lv_color_t kind_color(int k)
{
    return k == WM_HOTSPOT ? lv_color_hex(0xFF4FD8) : k == WM_MOVING ? C_WARN : ui_accent();
}
static const char *kind_name(int k) { return k == WM_HOTSPOT ? "hotspot" : k == WM_MOVING ? "mobile" : "fissa"; }

static lv_obj_t *mkl_c(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void load_once(void)
{
    if (loaded) return;
    loaded = true;
    if (sd_ok()) wm_load(MAP_FILE);
}

static void save(void)
{
    if (!sd_ok()) return;
    mkdir(MAP_DIR, 0775);
    wm_save(MAP_FILE);
}

// reti in ordine di potenza recente (le più vicine prima)
static int order[WM_MAX], n_order;
static void sort_nodes(void)
{
    n_order = 0;
    for (int i = 0; i < wm_count(); i++) {
        const wm_node_t *p = wm_node(i);
        if (!show_mobile && p->kind != WM_FIXED) continue;
        order[n_order++] = i;
    }
    for (int a = 1; a < n_order; a++)
        for (int b = a; b > 0; b--) {
            const wm_node_t *x = wm_node(order[b]), *y = wm_node(order[b - 1]);
            float kx = x->rssi - (wm_scans() - x->last) * 3.0f, ky = y->rssi - (wm_scans() - y->last) * 3.0f;
            if (kx <= ky) break;
            int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
        }
}

/* ================= elenco ================= */

static int l_sel;
static list_view_t l_lv;

static void l_render(int dir)
{
    sort_nodes();
    if (!n_order) {
        list_view_set(&l_lv, LV_SYMBOL_WIFI, NULL, "Nessuna rete", "Apri la mappa e cammina", NULL, 0, 0, dir);
        return;
    }
    if (l_sel >= n_order) l_sel = n_order - 1;
    const wm_node_t *p = wm_node(order[l_sel]);
    char sub[112];
    snprintf(sub, sizeof(sub), "%s · %.0f dBm · vista %u volte · %d fili", kind_name(p->kind), p->rssi, p->seen, wm_links(order[l_sel]));
    list_view_set(&l_lv, LV_SYMBOL_WIFI, l_sel > 0 ? wm_node(order[l_sel - 1])->ssid : NULL, p->ssid, sub,
                  l_sel + 1 < n_order ? wm_node(order[l_sel + 1])->ssid : NULL, l_sel, n_order, dir);
}

static void l_enter(lv_obj_t *root, void *arg)
{
    load_once();
    list_view_create(&l_lv, root);
    l_render(0);
}

static bool l_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (l_sel + 1 < n_order) { l_sel++; l_render(+1); } return true;
    case NAV_PREV: if (l_sel > 0) { l_sel--; l_render(-1); } return true;
    case NAV_SELECT: return true;
    default: return false;
    }
}

static const char *l_title(void *arg) { return "Mappa Wi-Fi » Reti"; }

static const app_t app_wifimap_list = {
    .name = "Reti della mappa", .icon = LV_SYMBOL_LIST,
    .enter = l_enter, .nav = l_nav, .title = l_title,
    .flags = APP_ROUND_OK,
};

/* ================= condividi sul telefono =================
 * La mappa diventa una pagina HTML autonoma sulla microSD (wifimap/mappa.html, vedi
 * wifimap_html.c); il Gadget apre un hotspot che la serve. Due QR: il primo collega il
 * telefono all'hotspot (aperto), il secondo apre la pagina, se non si apre da sola. */

#define HTML_FILE MAP_DIR "/mappa.html"

static lv_obj_t *s_qr[2], *s_cap[2], *s_status, *s_text;
static int s_step;            // tondo: quale QR si vede
static bool s_ok;
static char s_wifi_qr[64];

static void s_show(void)
{
    if (!SCR_ROUND) return;
    for (int i = 0; i < 2; i++) {
        if (i == s_step) { lv_obj_clear_flag(s_qr[i], LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_cap[i], LV_OBJ_FLAG_HIDDEN); }
        else { lv_obj_add_flag(s_qr[i], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_cap[i], LV_OBJ_FLAG_HIDDEN); }
    }
    ui_set_text(s_text, s_step == 0 ? "Inquadralo: il telefono si collega all'hotspot del Gadget"
                                    : "Se la pagina non si apre da sola, inquadra questo");
}

static void s_tick(void)
{
    if (!s_ok) return;
    char b[64];
    int n = wifi_mgr_portal_clients();
    if (n) snprintf(b, sizeof(b), LV_SYMBOL_OK " %d telefon%s collegat%s", n, n == 1 ? "o" : "i", n == 1 ? "o" : "i");
    else if (SCR_ROUND) snprintf(b, sizeof(b), "Su/giù: l'altro QR");
    else snprintf(b, sizeof(b), "Hotspot %s: in attesa…", wifi_mgr_portal_ssid());
    ui_set_text(s_status, b);
    ui_set_text_color(s_status, n ? C_OK : C_DIM);
}

static lv_obj_t *s_mkqr(lv_obj_t *root, int size, const char *data)
{
    lv_obj_t *q = lv_qrcode_create(root);
    lv_qrcode_set_size(q, size);
    lv_qrcode_set_dark_color(q, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(q, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(q, data, strlen(data));
    // margine bianco attorno: senza, molte fotocamere non lo leggono su fondo nero
    lv_obj_set_style_border_color(q, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(q, 8, 0);
    return q;
}

static void s_enter(lv_obj_t *root, void *arg)
{
    load_once();
    bool r = SCR_ROUND;
    s_ok = false;
    s_step = 0;
    s_text = mkl_c(root, &font_s, C_TEXT);
    s_status = mkl_c(root, &font_s, C_DIM);
    if (!sd_ok() || !wm_count()) {
        ui_set_text(s_text, !sd_ok() ? "Serve la microSD: la pagina si salva lì" : "La mappa è vuota: aprila e cammina un po'");
        lv_obj_set_width(s_text, r ? 340 : SCR_W - 40);
        lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
        lv_obj_align(s_text, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    mkdir(MAP_DIR, 0775);
    char when[48] = "";
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 124) snprintf(when, sizeof(when), "%02d/%02d/%04d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    if (!wm_export_html(HTML_FILE, when)) {
        ui_set_text(s_text, "Non riesco a scrivere sulla microSD");
        lv_obj_align(s_text, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    wifi_mgr_portal_start_file(HTML_FILE, "mappa-wifi.html");
    snprintf(s_wifi_qr, sizeof(s_wifi_qr), "WIFI:T:nopass;S:%s;;", wifi_mgr_portal_ssid());
    s_ok = true;
    if (r) {   // tondo: un QR alla volta, grande, al centro
        for (int i = 0; i < 2; i++) {
            s_cap[i] = mkl_c(root, &font_m, ui_accent());
            lv_obj_set_width(s_cap[i], 340);
            lv_obj_set_style_text_align(s_cap[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_cap[i], LV_ALIGN_TOP_MID, 0, 0);
            s_qr[i] = s_mkqr(root, 196, i ? "http://192.168.4.1/" : s_wifi_qr);
            lv_obj_align(s_qr[i], LV_ALIGN_TOP_MID, 0, 36);
        }
        ui_set_text(s_cap[0], "1 · Collegati");
        ui_set_text(s_cap[1], "2 · Apri la mappa");
        lv_obj_set_width(s_text, 330);
        lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
        lv_obj_align(s_text, LV_ALIGN_TOP_MID, 0, 254);
        lv_obj_set_width(s_status, 300);
        lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 316);
        s_show();
    } else {   // 3.49: i due QR affiancati a sinistra, le istruzioni a destra
        for (int i = 0; i < 2; i++) {
            s_qr[i] = s_mkqr(root, 108, i ? "http://192.168.4.1/" : s_wifi_qr);
            lv_obj_set_pos(s_qr[i], 14 + i * 140, 2);
            s_cap[i] = mkl_c(root, &font_s, ui_accent());
            lv_label_set_text(s_cap[i], i ? "2 · Apri la mappa" : "1 · Collegati");
            lv_obj_set_width(s_cap[i], 132);
            lv_obj_set_style_text_align(s_cap[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(s_cap[i], 2 + i * 140, 128);
        }
        lv_obj_set_width(s_text, SCR_W - 306);
        lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(s_text, 296, 6);
        lv_label_set_text(s_text, "Inquadra il primo QR con la fotocamera: il telefono si collega all'hotspot e "
                                  "di solito la mappa si apre da sola; se no, il secondo QR. Sulla pagina: "
                                  "Scarica immagine e Salva la pagina, da condividere.");
        lv_obj_set_width(s_status, SCR_W - 306);
        lv_obj_set_pos(s_status, 296, 124);
    }
    s_tick();
}

static void s_leave(void) { if (s_ok) wifi_mgr_portal_stop(); s_ok = false; }

static bool s_nav(nav_t ev)
{
    if ((ev == NAV_NEXT || ev == NAV_PREV) && SCR_ROUND && s_ok) { s_step ^= 1; s_show(); return true; }
    return false;
}

static const char *s_title(void *arg) { return "Mappa Wi-Fi » Condividi"; }

static const app_t app_wifimap_share = {
    .name = "Condividi la mappa", .icon = ICON_MOBILE,
    .enter = s_enter, .leave = s_leave, .nav = s_nav, .tick = s_tick, .title = s_title,
    .flags = APP_NO_SLEEP | APP_ROUND_OK,
};

/* ================= mappa ================= */

static lv_obj_t *box, *dots[WM_MAX], *me_dot, *trail_line, *mtrail[MAX_MOB], *edge_line[MAX_EDGES], *sel_ring, *sel_name;
static lv_obj_t *l_sel_lbl, *l_det, *l_stat, *l_hint, *l_scale;
EXT_RAM_BSS_ATTR static lv_point_precise_t trail_pts[WM_TRAIL], mt_pts[MAX_MOB][WM_MTRAIL], edge_pts[MAX_EDGES][2];
static int bw, bh, sel = -1;   // sel: indice della rete scelta (in wm_node), -1 nessuna
static uint32_t seen_gen, drawn_scans = ~0u;
static bool paused, pending;
static lv_timer_t *tmr;
static float sc, ox, oy;       // metri → pixel

static void fit(void)
{
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int k = 0;
    for (int i = 0; i < wm_count(); i++) {
        const wm_node_t *p = wm_node(i);
        if (!p->placed || (!show_mobile && p->kind != WM_FIXED)) continue;
        if (p->x < minx) minx = p->x;
        if (p->x > maxx) maxx = p->x;
        if (p->y < miny) miny = p->y;
        if (p->y > maxy) maxy = p->y;
        k++;
    }
    float mx, my;
    if (wm_me(&mx, &my)) {
        if (mx < minx) minx = mx;
        if (mx > maxx) maxx = mx;
        if (my < miny) miny = my;
        if (my > maxy) maxy = my;
        k++;
    }
    if (!k) { minx = miny = -5; maxx = maxy = 5; }
    float w = maxx - minx, h = maxy - miny;
    if (w < 6) { minx -= (6 - w) / 2; w = 6; }
    if (h < 6) { miny -= (6 - h) / 2; h = 6; }
    sc = fminf((bw - 24) / w, (bh - 24) / h);
    ox = bw / 2 - (minx + w / 2) * sc;
    oy = bh / 2 - (miny + h / 2) * sc;
}

static int X(float x) { return (int)lroundf(ox + x * sc); }
static int Y(float y) { return (int)lroundf(oy + y * sc); }

static void place_dot(lv_obj_t *o, float x, float y, int size)
{
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, X(x) - size / 2, Y(y) - size / 2);
}

// posizioni sullo schermo dei punti: quelli troppo vicini si allargano quanto basta per
// distinguerli (restando il più possibile al loro posto)
static float SXp[WM_MAX], SYp[WM_MAX];
static bool node_vis(const wm_node_t *p) { return p && p->placed && (show_mobile || p->kind == WM_FIXED); }

static void spread(void)
{
    const float d = SCR_ROUND ? 16 : 13;
    float ox_[WM_MAX], oy_[WM_MAX];
    int n = wm_count();
    for (int i = 0; i < n; i++) {
        const wm_node_t *p = wm_node(i);
        SXp[i] = ox_[i] = ox + p->x * sc;
        SYp[i] = oy_[i] = oy + p->y * sc;
    }
    for (int it = 0; it < 80; it++) {
        for (int i = 0; i < n; i++) {
            if (!node_vis(wm_node(i))) continue;
            for (int j = i + 1; j < n; j++) {
                if (!node_vis(wm_node(j))) continue;
                float dx = SXp[j] - SXp[i], dy = SYp[j] - SYp[i], l = sqrtf(dx * dx + dy * dy);
                if (l >= d) continue;
                if (l < 0.01f) { dx = cosf(i + j * 2.4f); dy = sinf(i + j * 2.4f); l = 1; }
                float f = (d - l) / 2 / l;
                SXp[i] -= dx * f; SYp[i] -= dy * f;
                SXp[j] += dx * f; SYp[j] += dy * f;
            }
        }
        for (int i = 0; i < n; i++) {
            if (it < 65) { SXp[i] += (ox_[i] - SXp[i]) * 0.05f; SYp[i] += (oy_[i] - SYp[i]) * 0.05f; }
            SXp[i] = fminf(bw - 7, fmaxf(7, SXp[i]));
            SYp[i] = fminf(bh - 7, fmaxf(7, SYp[i]));
        }
    }
}

static void place_node(lv_obj_t *o, int i, int size)
{
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, (int)lroundf(SXp[i]) - size / 2, (int)lroundf(SYp[i]) - size / 2);
}

typedef struct { int a, b, n; } edge_ref_t;
static int by_count(const void *x, const void *y) { return ((const edge_ref_t *)y)->n - ((const edge_ref_t *)x)->n; }

static void redraw(void)
{
    fit();
    spread();
    // fili: le coppie di reti fisse viste insieme più spesso (al massimo MAX_EDGES)
    EXT_RAM_BSS_ATTR static edge_ref_t all[WM_MAX * (WM_MAX - 1) / 2];
    int na = 0;
    for (int a = 0; a < wm_count(); a++)
        for (int b = a + 1; b < wm_count(); b++) {
            int c;
            if (wm_node(a)->kind != WM_FIXED || wm_node(b)->kind != WM_FIXED || !wm_edge(a, b, NULL, &c) || c < 2) continue;
            all[na++] = (edge_ref_t){a, b, c};
        }
    qsort(all, na, sizeof(all[0]), by_count);
    int ne = na < MAX_EDGES ? na : MAX_EDGES;
    edge_ref_t *best = all;
    for (int e = 0; e < MAX_EDGES; e++) {
        if (e >= ne) { lv_obj_add_flag(edge_line[e], LV_OBJ_FLAG_HIDDEN); continue; }
        int a = best[e].a, b = best[e].b;
        edge_pts[e][0].x = (int)lroundf(SXp[a]); edge_pts[e][0].y = (int)lroundf(SYp[a]);
        edge_pts[e][1].x = (int)lroundf(SXp[b]); edge_pts[e][1].y = (int)lroundf(SYp[b]);
        lv_line_set_points(edge_line[e], edge_pts[e], 2);
        lv_obj_clear_flag(edge_line[e], LV_OBJ_FLAG_HIDDEN);
    }
    // scia del percorso: le ultime 40 posizioni, ognuna media con le vicine (una
    // posizione da sola oscilla di qualche metro)
    float tp[WM_TRAIL][2];
    int tn = wm_trail(tp, 40);
    for (int i = 0; i < tn; i++) {
        float sx = 0, sy = 0;
        int k = 0;
        for (int j = i - 2; j <= i + 2; j++) if (j >= 0 && j < tn) { sx += tp[j][0]; sy += tp[j][1]; k++; }
        trail_pts[i].x = X(sx / k);
        trail_pts[i].y = Y(sy / k);
    }
    lv_line_set_points(trail_line, trail_pts, tn);
    // reti
    int nm = 0;
    for (int i = 0; i < WM_MAX; i++) {
        const wm_node_t *p = wm_node(i);
        if (!node_vis(p)) { lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN); continue; }
        bool recent = wm_scans() - p->last < 3;
        place_node(dots[i], i, recent ? 12 : 9);
        lv_obj_set_style_bg_color(dots[i], kind_color(p->kind), 0);
        lv_obj_set_style_bg_opa(dots[i], recent ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
        if (p->kind != WM_FIXED && nm < MAX_MOB && p->tn > 1) {   // scia delle mobili
            for (int t = 0; t < p->tn; t++) { mt_pts[nm][t].x = X(p->tx[t]); mt_pts[nm][t].y = Y(p->ty[t]); }
            lv_line_set_points(mtrail[nm], mt_pts[nm], p->tn);
            lv_obj_set_style_line_color(mtrail[nm], kind_color(p->kind), 0);
            lv_obj_clear_flag(mtrail[nm], LV_OBJ_FLAG_HIDDEN);
            nm++;
        }
    }
    for (int m = nm; m < MAX_MOB; m++) lv_obj_add_flag(mtrail[m], LV_OBJ_FLAG_HIDDEN);
    float mx, my;
    if (wm_me(&mx, &my)) { place_dot(me_dot, mx, my, 14); lv_obj_clear_flag(me_dot, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(me_dot); }
    else lv_obj_add_flag(me_dot, LV_OBJ_FLAG_HIDDEN);

    // rete scelta
    char b[112];
    const wm_node_t *p = sel >= 0 ? wm_node(sel) : NULL;
    if (node_vis(p)) {
        place_node(sel_ring, sel, 22);
        lv_obj_clear_flag(sel_ring, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(sel_name, p->ssid);
        int sx = (int)lroundf(SXp[sel]), nx = sx + 14, ny = (int)lroundf(SYp[sel]) - 10;
        if (nx > bw - 90) nx = sx - 104;
        lv_obj_set_pos(sel_name, nx < 2 ? 2 : nx, ny < 2 ? 2 : ny > bh - 22 ? bh - 22 : ny);
        lv_obj_clear_flag(sel_name, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(l_sel_lbl, p->ssid);
        ui_set_text_color(l_sel_lbl, kind_color(p->kind));
        float dme = mx - p->x, dmy = my - p->y;
        snprintf(b, sizeof(b), "%.0f dBm · ~%.0f m da te · %s · %d fili", p->rssi, sqrtf(dme * dme + dmy * dmy),
                 kind_name(p->kind), wm_links(sel));
        ui_set_text(l_det, b);
    } else {
        lv_obj_add_flag(sel_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sel_name, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(l_sel_lbl, wm_count() ? "Su/giù: scegli una rete" : "Cammina per l'edificio");
        ui_set_text_color(l_sel_lbl, C_TEXT);
        ui_set_text(l_det, wm_count() ? "Punti: reti · bianco: tu" : "Nasce dalle reti che vede");
    }
    int fixed = 0, mob = 0;
    for (int i = 0; i < wm_count(); i++) {
        if (wm_node(i)->kind == WM_FIXED) fixed++;
        else mob++;
    }
    snprintf(b, sizeof(b), "%d fisse · %d mobili · %lu scansioni%s", fixed, mob, (unsigned long)wm_scans(),
             paused ? " · pausa" : "");
    ui_set_text(l_stat, b);
    snprintf(b, sizeof(b), "%.0f m", (bw - 24) / sc);
    ui_set_text(l_scale, b);
    drawn_scans = wm_scans();
}

static void tick_cb(lv_timer_t *t)
{
    if (paused) return;
    uint32_t g = wifi_mgr_scan_gen();
    if (g != seen_gen && !wifi_mgr_scan_busy()) {
        seen_gen = g;
        if (!pending) {   // scansione finita: nella mappa
            static wifi_ap_t aps[32];
            int n = wifi_mgr_scan_results(aps, 32);
            if (n) wm_add_scan(aps, n);
        }
        pending = false;
    }
    if (!wifi_mgr_scan_busy() && wifi_mgr_scan_start()) seen_gen = wifi_mgr_scan_gen();
    if (wm_scans() != drawn_scans) redraw();
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

static lv_obj_t *mkdot(lv_obj_t *p, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static lv_obj_t *mkline(lv_obj_t *p, lv_color_t c, int w)
{
    lv_obj_t *l = lv_line_create(p);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    load_once();
    bool r = SCR_ROUND;
    box = lv_obj_create(root);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0B0E12), 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (r) { bw = 340; bh = 236; lv_obj_set_size(box, bw, bh); lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 4); }
    else { bw = 380; bh = 140; lv_obj_set_size(box, bw, bh); lv_obj_set_pos(box, SCR_W - bw - 6, 4); }

    for (int e = 0; e < MAX_EDGES; e++) { edge_line[e] = mkline(box, lv_color_hex(0x2C333B), 1); lv_obj_add_flag(edge_line[e], LV_OBJ_FLAG_HIDDEN); }
    trail_line = mkline(box, lv_color_hex(0x46505A), 2);
    for (int m = 0; m < MAX_MOB; m++) { mtrail[m] = mkline(box, C_WARN, 1); lv_obj_add_flag(mtrail[m], LV_OBJ_FLAG_HIDDEN); }
    sel_ring = mkdot(box, C_BG);
    lv_obj_set_style_bg_opa(sel_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sel_ring, 2, 0);
    lv_obj_set_style_border_color(sel_ring, C_TEXT, 0);
    for (int i = 0; i < WM_MAX; i++) dots[i] = mkdot(box, ui_accent());
    me_dot = mkdot(box, C_TEXT);
    lv_obj_set_style_border_width(me_dot, 3, 0);
    lv_obj_set_style_border_color(me_dot, lv_color_hex(0x0B0E12), 0);
    sel_name = mkl(box, &font_s, C_TEXT);
    lv_obj_set_width(sel_name, 100);
    l_scale = mkl(box, &font_s, C_DIM);
    lv_obj_align(l_scale, LV_ALIGN_BOTTOM_RIGHT, -8, -4);

    l_sel_lbl = mkl(root, &font_m, C_TEXT);
    l_det = mkl(root, &font_s, C_DIM);
    l_stat = mkl(root, &font_s, C_DIM);
    l_hint = mkl(root, &font_s, C_DIM);
    if (r) {
        int y = bh + 14;
        lv_obj_t *ls[] = {l_sel_lbl, l_det, l_stat};
        int ys[] = {y, y + 36, y + 64}, ws[] = {360, 350, 300};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_width(ls[i], ws[i]);
            lv_obj_set_style_text_align(ls[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(ls[i], LV_ALIGN_TOP_MID, 0, ys[i]);
        }
        lv_obj_add_flag(l_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        int w = SCR_W - bw - 24;
        lv_obj_t *ls[] = {l_sel_lbl, l_det, l_stat, l_hint};
        int ys[] = {6, 38, 64, 112};
        for (int i = 0; i < 4; i++) { lv_obj_set_width(ls[i], w); lv_obj_set_pos(ls[i], 12, ys[i]); }
        lv_label_set_long_mode(l_det, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(l_stat, 12, 86);   // sotto il dettaglio, che può andare su due righe
        lv_label_set_text(l_hint, "Tieni premuto: condividi");
    }
    wifi_mgr_scan_acquire();
    seen_gen = wifi_mgr_scan_gen();
    pending = true;   // una scansione eventualmente in corso è di prima: si salta
    paused = false;
    drawn_scans = ~0u;
    redraw();
    tmr = lv_timer_create(tick_cb, 200, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    wifi_mgr_scan_release();
    save();
}

static void select_step(int d)
{
    sort_nodes();
    if (!n_order) return;
    int pos = -1;
    for (int i = 0; i < n_order; i++) if (order[i] == sel) pos = i;
    pos = pos < 0 ? 0 : (pos + d + n_order) % n_order;
    sel = order[pos];
    redraw();
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: select_step(+1); return true;
    case NAV_PREV: select_step(-1); return true;
    case NAV_SELECT: ui_push(&app_wifimap_list, NULL); return true;
    case NAV_QUICK: ui_push(&app_wifimap_share, NULL); return true;   // dito tenuto o BOOT tenuto
    case NAV_BTN:
        paused = !paused;
        ui_toast(paused ? "Mappa in pausa" : "Mappa ripresa");
        redraw();
        return true;
    default: return false;
    }
}

const app_t app_wifimap = {
    .name = "Mappa Wi-Fi", .icon = LV_SYMBOL_WIFI,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_NO_SLEEP | APP_OWN_QUICK | APP_ROUND_OK,
};

/* ================= menu ================= */

static void v_count(char *b, int n)
{
    load_once();
    int f = 0;
    for (int i = 0; i < wm_count(); i++) if (wm_node(i)->kind == WM_FIXED) f++;
    snprintf(b, n, "%d reti (%d fisse) · %lu scansioni", wm_count(), f, (unsigned long)wm_scans());
}
static void v_mob(char *b, int n) { snprintf(b, n, "%s", show_mobile ? "Sì, con la loro scia" : "No, solo le fisse"); }
static void a_mob(void) { show_mobile = !show_mobile; }
static void a_export(void)
{
    load_once();
    if (!sd_ok()) { ui_toast("Manca la microSD"); return; }
    ui_toast(wm_export(MAP_DIR) ? "Salvati wifimap/reti.csv e collegamenti.csv" : "Non riesco a scrivere sulla microSD");
}
static void a_reset(void)
{
    wm_reset();
    loaded = true;
    sel = -1;
    if (sd_ok()) remove(MAP_FILE);
    ui_toast("Mappa nuova: cammina per costruirla");
}

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Apri la mappa", .value = v_count, .app = &app_wifimap},
    {.icon = LV_SYMBOL_LIST, .label = "Reti della mappa", .hint = "Fisse, mobili e hotspot", .app = &app_wifimap_list},
    {.icon = ICON_MOBILE, .label = "Condividi sul telefono", .hint = "QR: pagina con la mappa, immagine e file", .app = &app_wifimap_share},
    {.icon = LV_SYMBOL_EYE_OPEN, .label = "Mostra le reti mobili", .value = v_mob, .on_select = a_mob},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Esporta su microSD", .hint = "CSV per rifare la mappa sul PC", .on_select = a_export},
    {.icon = LV_SYMBOL_TRASH, .label = "Nuova mappa", .hint = "Cancella quella costruita finora", .on_select = a_reset, .confirm = true},
};

menu_t wifimap_menu = {"Mappa Wi-Fi", items, sizeof(items) / sizeof(items[0]), 0, NULL};
