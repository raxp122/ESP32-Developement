// app_bt.c — schermate dei collegamenti Bluetooth (vedi ble_mgr.h):
//   Dispositivo: il Gadget si collega a un dispositivo scelto nello scanner (altri → Gadget)
//   Associa:     il Gadget si fa trovare da un telefono o un computer (Gadget → altri)
//   Collegati:   collegamenti attivi e dispositivi associati
// Le funzioni vere (tastiera, telecomando…) arriveranno come profili sopra questi.
#include "apps.h"
#include "ble_mgr.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static uint32_t confirm_until;

static lv_obj_t *mk(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y, bool wrap)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, SCR_W - 48);
    lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 24, y);
    lv_label_set_text(l, "");
    return l;
}

static void fmt_addr(const uint8_t a[6], char *b, int n)
{
    snprintf(b, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

// aggiunge s in fondo a b (senza superare n): niente snprintf su buffer già pieni a metà
static void append(char *b, int n, const char *s)
{
    int o = strlen(b), l = strlen(s);
    if (o + l > n - 1) l = n - 1 - o;
    if (l <= 0) return;
    memcpy(b + o, s, l);
    b[o + l] = 0;
}

static bool confirming(void) { return confirm_until && lv_tick_get() < confirm_until; }

// "Batteria · Info dispositivo · Tastiera / HID …"
static void svc_list(const ble_conn_t *c, char *b, int n)
{
    b[0] = 0;
    for (int i = 0; i < c->n_svcs; i++) {
        uint16_t u = c->svcs[i];
        if (u == 0x1800 || u == 0x1801) continue;   // ci sono sempre: non dicono nulla
        const char *nm = ble_mgr_svc_name(u);
        char tmp[16];
        if (!nm) { snprintf(tmp, sizeof(tmp), "0x%04X", u); nm = tmp; }
        if (b[0]) append(b, n, " · ");
        append(b, n, nm);
    }
}

static bool find_conn(const uint8_t addr[6], bool central, ble_conn_t *out)
{
    ble_conn_t cs[BLE_MAX_CONN];
    int n = ble_mgr_conns(cs, BLE_MAX_CONN);
    for (int i = 0; i < n; i++)
        if (cs[i].central == central && !memcmp(cs[i].addr, addr, 6)) { *out = cs[i]; return true; }
    return false;
}

/* ================= Dispositivo (altri → Gadget) ================= */

static ble_dev_t target;
static bool want_connect;   // da collegare appena lo stack è pronto
static lv_obj_t *d_name, *d_state, *d_info, *d_hint;

static void dev_render(void)
{
    char t[200], a[20];
    ble_conn_t c = {0};
    if (want_connect && ble_mgr_ready()) { want_connect = false; ble_mgr_connect(&target); }
    bool up = find_conn(target.addr, true, &c);
    ble_link_t ls = ble_mgr_link_state(target.addr);
    const char *type = ble_mgr_appearance_name(up && c.appearance ? c.appearance : target.appearance);

    ui_set_text(d_name, up && c.name[0] ? c.name : target.name[0] ? target.name : "Senza nome");
    lv_color_t col = C_TEXT;
    const char *hint = "";
    uint32_t pk = ble_mgr_passkey();
    if (up) {
        snprintf(t, sizeof(t), "%s Collegato%s", LV_SYMBOL_OK, c.bonded ? " e associato" : c.encrypted ? " (cifrato)" : "");
        if (pk) snprintf(t, sizeof(t), "Codice: %06u", (unsigned)pk);
        col = C_OK;
        hint = confirming() ? "Swipe a destra di nuovo per scollegare" : "Destra: scollega · sinistra: indietro (resta collegato)";
    } else if (ls == BLE_LINK_CONNECTING || want_connect) {
        snprintf(t, sizeof(t), "%s Collegamento…", LV_SYMBOL_REFRESH);
        col = ui_accent();
        hint = "Sinistra: indietro";
    } else if (ls == BLE_LINK_FAILED) {
        snprintf(t, sizeof(t), "%s %s", LV_SYMBOL_WARNING, ble_mgr_link_error());
        col = C_WARN;
        hint = "Destra: riprova";
    } else {
        snprintf(t, sizeof(t), "Non collegato");
        col = C_DIM;
        hint = target.connectable ? "Destra: collega" : "Non accetta collegamenti (solo annunci)";
    }
    ui_set_text(d_state, t);
    ui_set_text_color(d_state, col);

    fmt_addr(target.addr, a, sizeof(a));
    t[0] = 0;
    if (type) { append(t, sizeof(t), type); append(t, sizeof(t), " · "); }
    append(t, sizeof(t), a);
    if (up && c.disc == BLE_DISC_RUNNING) append(t, sizeof(t), " · cerco i servizi…");
    else if (up && c.n_svcs) {
        char sv[140];
        svc_list(&c, sv, sizeof(sv));
        if (sv[0]) { append(t, sizeof(t), "\n"); append(t, sizeof(t), sv); }
    }
    if (ble_mgr_is_audio(target.appearance))
        append(t, sizeof(t), "\nAudio non disponibile: serve il Bluetooth classico");
    ui_set_text(d_info, t);
    ui_set_text(d_hint, hint);
    ui_set_text_color(d_hint, confirming() ? C_WARN : C_DIM);
}

static void dev_enter(lv_obj_t *root, void *arg)
{
    if (arg) target = *(const ble_dev_t *)arg;
    d_name = mk(root, &font_l, C_TEXT, 4, false);
    d_state = mk(root, &font_m, C_TEXT, 46, false);
    d_info = mk(root, &font_s, C_DIM, 74, true);
    d_hint = mk(root, &font_s, C_DIM, 128, false);
    confirm_until = 0;
    ble_conn_t c = {0};
    // apri = collega, se non lo è già
    want_connect = !find_conn(target.addr, true, &c) && target.connectable &&
                   ble_mgr_link_state(target.addr) != BLE_LINK_CONNECTING;
    dev_render();
}

static bool dev_nav(nav_t ev)
{
    if (ev != NAV_SELECT) return false;
    ble_conn_t c = {0};
    if (find_conn(target.addr, true, &c)) {
        if (!confirming()) { confirm_until = lv_tick_get() + 4000; dev_render(); return true; }
        confirm_until = 0;
        ble_mgr_disconnect(c.handle);
    } else if (ble_mgr_link_state(target.addr) != BLE_LINK_CONNECTING) {
        if (target.connectable || ble_mgr_link_state(target.addr) == BLE_LINK_FAILED) want_connect = true;
    }
    dev_render();
    return true;
}

const app_t app_ble_device = {
    .name = "Dispositivo", .icon = LV_SYMBOL_BLUETOOTH,
    .enter = dev_enter, .nav = dev_nav, .tick = dev_render, .flags = APP_NO_SLEEP,
};

/* ================= Associa (Gadget → altri) ================= */

static lv_obj_t *p_name, *p_state, *p_info, *p_hint;
static bool p_paired;
static char p_peer[32];

// collegamenti in entrata che c'erano già all'apertura non contano come "nuovi"
static uint16_t p_known[BLE_MAX_CONN];
static int p_nknown;

static void pair_render(void)
{
    char t[160];
    ble_conn_t cs[BLE_MAX_CONN];
    int n = ble_mgr_conns(cs, BLE_MAX_CONN);
    for (int i = 0; i < n; i++) {
        if (cs[i].central) continue;
        bool old = false;
        for (int k = 0; k < p_nknown; k++) if (p_known[k] == cs[i].handle) old = true;
        if (old) continue;
        if (cs[i].name[0]) snprintf(p_peer, sizeof(p_peer), "%s", cs[i].name);
        else fmt_addr(cs[i].addr, p_peer, sizeof(p_peer));
        if (cs[i].bonded) p_paired = true;
    }
    int left = ble_mgr_pair_left();
    uint32_t pk = ble_mgr_passkey();
    ui_set_text(p_name, ble_mgr_name());
    lv_color_t col = C_TEXT;
    if (p_paired) {
        snprintf(t, sizeof(t), "%s Associato a %s", LV_SYMBOL_OK, p_peer);
        col = C_OK;
    } else if (pk) {
        snprintf(t, sizeof(t), "Codice: %06u", (unsigned)pk);
        col = ui_accent();
    } else if (p_peer[0]) {
        snprintf(t, sizeof(t), "Collegato a %s…", p_peer);
        col = ui_accent();
    } else if (left) {
        snprintf(t, sizeof(t), "In attesa · %d:%02d", left / 60, left % 60);
    } else {
        snprintf(t, sizeof(t), "Finestra chiusa");
        col = C_DIM;
    }
    ui_set_text(p_state, t);
    ui_set_text_color(p_state, col);
    ui_set_text(p_info, p_paired ? "Si ricollegherà da solo quando il Bluetooth è acceso."
                       : pk ? "Digita il codice sul telefono o sul computer per confermare."
                       : "Sul telefono o sul computer apri le impostazioni Bluetooth e scegli il Gadget "
                         "con questo nome.");
    ui_set_text(p_hint, left || p_paired ? "Sinistra: chiudi" : "Destra: riapri per 2 minuti · sinistra: chiudi");
}

static void pair_enter(lv_obj_t *root, void *arg)
{
    p_name = mk(root, &font_l, ui_accent(), 4, false);
    p_state = mk(root, &font_m, C_TEXT, 46, false);
    p_info = mk(root, &font_s, C_DIM, 76, true);
    p_hint = mk(root, &font_s, C_DIM, 128, false);
    p_paired = false;
    p_peer[0] = 0;
    ble_conn_t cs[BLE_MAX_CONN];
    int n = ble_mgr_conns(cs, BLE_MAX_CONN);
    p_nknown = 0;
    for (int i = 0; i < n; i++) if (!cs[i].central) p_known[p_nknown++] = cs[i].handle;
    ble_mgr_pair_start(120);
    pair_render();
}

static void pair_leave(void) { ble_mgr_pair_stop(); }

static bool pair_nav(nav_t ev)
{
    if (ev == NAV_SELECT && !ble_mgr_pair_left() && !p_paired) {
        ble_mgr_pair_start(120);
        pair_render();
        return true;
    }
    return ev == NAV_SELECT;
}

const app_t app_ble_pair = {
    .name = "Associa", .icon = LV_SYMBOL_BLUETOOTH,
    .enter = pair_enter, .leave = pair_leave, .nav = pair_nav, .tick = pair_render, .flags = APP_NO_SLEEP,
};

/* ================= Collegati e associati ================= */

typedef struct {
    bool live;              // collegamento attivo
    ble_conn_t c;           // se live
    ble_bond_t b;           // se solo associato
} entry_t;

static entry_t ents[BLE_MAX_CONN + 8];
static int n_ents, c_sel;
static uint32_t c_gen;
static list_view_t c_lv;
static char c_title[48], c_names[3][40];

static const uint8_t *ent_addr(const entry_t *e) { return e->live ? e->c.addr : e->b.addr; }

static void c_reload(void)
{
    // ricorda chi era selezionato: se la lista si riordina, la selezione (e una conferma
    // già armata) deve restare su quel dispositivo, non passare a un altro
    uint8_t keep[6];
    bool had = c_sel < n_ents;
    if (had) memcpy(keep, ent_addr(&ents[c_sel]), 6);
    ble_conn_t cs[BLE_MAX_CONN];
    ble_bond_t bs[8];
    int nc = ble_mgr_conns(cs, BLE_MAX_CONN), nb = ble_mgr_bonds(bs, 8);
    n_ents = 0;
    for (int i = 0; i < nc; i++) ents[n_ents++] = (entry_t){.live = true, .c = cs[i]};
    for (int i = 0; i < nb; i++) {
        bool dup = false;
        for (int k = 0; k < nc; k++) if (!memcmp(cs[k].addr, bs[i].addr, 6)) dup = true;
        if (!dup) ents[n_ents++] = (entry_t){.live = false, .b = bs[i]};
    }
    bool found = false;
    for (int i = 0; had && i < n_ents; i++)
        if (!memcmp(ent_addr(&ents[i]), keep, 6)) { c_sel = i; found = true; break; }
    if (had && !found) confirm_until = 0;   // il dispositivo selezionato non c'è più
    if (c_sel >= n_ents) c_sel = n_ents ? n_ents - 1 : 0;
}

static const char *c_label(int i, char *b, int n)
{
    if (i < 0 || i >= n_ents) return NULL;
    if (ents[i].live && ents[i].c.name[0]) snprintf(b, n, "%s", ents[i].c.name);
    else fmt_addr(ents[i].live ? ents[i].c.addr : ents[i].b.addr, b, n);
    return b;
}

static void c_render(int dir)
{
    if (!n_ents) {
        list_view_set(&c_lv, LV_SYMBOL_BLUETOOTH, NULL, "Nessun dispositivo",
                      ble_mgr_on() ? "Associa un telefono o collegati a un dispositivo" : "Il Bluetooth è spento",
                      NULL, 0, 0, dir);
        return;
    }
    const entry_t *e = &ents[c_sel];
    char sub[96];
    if (confirming())
        snprintf(sub, sizeof(sub), "Swipe a destra di nuovo per %s", e->live ? "scollegare" : "dimenticarlo");
    else if (e->live)
        snprintf(sub, sizeof(sub), "%s Collegato · %s%s", LV_SYMBOL_OK,
                 e->c.central ? "il Gadget usa lui" : "lui usa il Gadget", e->c.bonded ? " · associato" : "");
    else
        snprintf(sub, sizeof(sub), "Associato · non collegato · destra: dimentica");
    list_view_set(&c_lv, LV_SYMBOL_BLUETOOTH, c_label(c_sel - 1, c_names[0], 40), c_label(c_sel, c_names[1], 40), sub,
                  c_label(c_sel + 1, c_names[2], 40), c_sel, n_ents, dir);
}

static void c_enter(lv_obj_t *root, void *arg)
{
    list_view_create(&c_lv, root);
    confirm_until = 0;
    c_gen = ble_mgr_conn_gen();
    c_reload();
    c_render(0);
}

static void c_tick(void)
{
    uint32_t g = ble_mgr_conn_gen();
    if (g != c_gen) { c_gen = g; c_reload(); }
    c_render(0);
}

static bool c_nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (c_sel + 1 < n_ents) { c_sel++; confirm_until = 0; c_render(+1); } return true;
    case NAV_PREV: if (c_sel > 0) { c_sel--; confirm_until = 0; c_render(-1); } return true;
    case NAV_SELECT:
        if (!n_ents) return true;
        if (!confirming()) { confirm_until = lv_tick_get() + 4000; c_render(0); return true; }
        confirm_until = 0;
        if (ents[c_sel].live) ble_mgr_disconnect(ents[c_sel].c.handle);
        else { ble_mgr_forget(&ents[c_sel].b); ui_toast("Dispositivo dimenticato"); }
        c_reload();
        c_render(0);
        return true;
    default: return false;
    }
}

static const char *c_titlef(void *arg)
{
    snprintf(c_title, sizeof(c_title), "Bluetooth » Dispositivi · %d", n_ents);
    return c_title;
}

const app_t app_ble_conns = {
    .name = "Dispositivi Bluetooth", .icon = LV_SYMBOL_BLUETOOTH,
    .enter = c_enter, .nav = c_nav, .tick = c_tick, .title = c_titlef,
};
