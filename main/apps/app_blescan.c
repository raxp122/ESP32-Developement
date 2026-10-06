// app_blescan.c — dispositivi Bluetooth LE nei paraggi; swipe a destra = collegati,
// tocco prolungato (o BOOT tenuto) = nuova scansione (come lo scanner Wi-Fi). Si apre anche da
// Impostazioni » Bluetooth (arg = titolo da mostrare).
#include "apps.h"
#include "ble_mgr.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

#define MAXN 48
static ble_dev_t list[MAXN];
static int n, sel;
static uint32_t seen_gen;
static bool pending;
static list_view_t lv;
static char title_buf[48], names[3][40];

static const char *dev_label(int i, char *buf, int len)
{
    if (i < 0 || i >= n) return NULL;
    const ble_dev_t *d = &list[i];
    if (d->name[0]) snprintf(buf, len, "%s", d->name);
    else snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", d->addr[5], d->addr[4], d->addr[3], d->addr[2], d->addr[1], d->addr[0]);
    return buf;
}

static void render(int dir)
{
    bool busy = ble_mgr_scan_busy() || pending;
    if (n == 0) {
        list_view_set(&lv, LV_SYMBOL_BLUETOOTH, NULL, busy ? "Scansione…" : "Nessun dispositivo",
                      busy ? (ble_mgr_ready() ? "" : "Avvio del Bluetooth…") : "Swipe a destra per riprovare", NULL, 0, 0, dir);
        return;
    }
    const ble_dev_t *d = &list[sel];
    char sub[128];
    const char *type = ble_mgr_appearance_name(d->appearance);
    if (!type && d->svc16) type = ble_mgr_svc_name(d->svc16);
    snprintf(sub, sizeof(sub), "%d dBm · %s%s%02X:%02X:%02X:%02X:%02X:%02X%s%s", d->rssi,
             type ? type : "", type ? " · " : "",
             d->addr[5], d->addr[4], d->addr[3], d->addr[2], d->addr[1], d->addr[0],
             d->connectable ? " · " LV_SYMBOL_RIGHT " collega" : "", busy ? " · cerco…" : "");
    list_view_set(&lv, LV_SYMBOL_BLUETOOTH, dev_label(sel - 1, names[0], 40), dev_label(sel, names[1], 40), sub,
                  dev_label(sel + 1, names[2], 40), sel, n, dir);
}

static void enter(lv_obj_t *root, void *arg)
{
    list_view_create(&lv, root);
    ble_mgr_scan_acquire();
    n = ble_mgr_scan_results(list, MAXN);
    if (sel >= n) sel = 0;
    seen_gen = ble_mgr_scan_gen();
    pending = true;
    render(0);
}

static void leave(void) { ble_mgr_scan_release(); }

static void tick(void)
{
    if (pending && ble_mgr_scan_start(6000)) pending = false;
    // durante la scansione aggiorna l'elenco man mano che arrivano i dispositivi
    static int k;
    uint32_t g = ble_mgr_scan_gen();
    if (g != seen_gen || (ble_mgr_scan_busy() && ++k % 5 == 0)) {
        seen_gen = g;
        uint8_t keep[6] = {0};
        bool had = sel < n;
        if (had) memcpy(keep, list[sel].addr, 6);
        n = ble_mgr_scan_results(list, MAXN);
        if (sel >= n) sel = n ? n - 1 : 0;
        // resta sul dispositivo che stavi guardando anche se l'ordine cambia
        for (int i = 0; had && i < n; i++)
            if (!memcmp(list[i].addr, keep, 6)) { sel = i; break; }
    }
    render(0);
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n) { sel++; render(+1); } return true;
    case NAV_PREV: if (sel > 0) { sel--; render(-1); } return true;
    case NAV_SELECT:
        if (!n) { pending = true; render(0); return true; }   // elenco vuoto: nuova scansione
        if (!list[sel].connectable) { ui_toast("Questo dispositivo non accetta collegamenti"); return true; }
        // i collegamenti vivono col Bluetooth acceso (lo scanner da solo lo accende solo per sé)
        if (!g_set.ble_on) { g_set.ble_on = true; settings_save(); ble_mgr_apply(); }
        ui_push(&app_ble_device, &list[sel]);
        return true;
    case NAV_QUICK: pending = true; render(0); return true;   // tocco prolungato (o BOOT tenuto) = nuova scansione
    default: return false;
    }
}

static const char *title(void *arg)
{
    snprintf(title_buf, sizeof(title_buf), "%s · %d dispositivi", arg ? (const char *)arg : "Scanner Bluetooth", n);
    return title_buf;
}

const app_t app_blescan = {
    .name = "Scanner Bluetooth", .icon = LV_SYMBOL_BLUETOOTH,
    .enter = enter, .leave = leave, .nav = nav, .tick = tick, .title = title,
    .flags = APP_OWN_QUICK,   // il tocco prolungato rifà la scansione
};
