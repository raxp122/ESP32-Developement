// sim_main.c — simulatore dell'interfaccia sul PC: disegna le schermate principali
// (home, impostazioni, Wi-Fi, tastiera, aggiornamento, portale, orologio) e le salva in out/.
// Uso: tools/sim/run.sh (dopo un idf.py build, che crea build/config/sdkconfig.h)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ota.h"
#include "seismo.h"
#include "wifimap.h"
#include <math.h>

extern int sim_round;
extern int16_t g_scr_w, g_scr_h;
extern nav_handler_t sim_nav;
extern ota_state_t sim_ota;
static uint32_t now_ms;
static uint32_t tick(void) { return now_ms; }
static uint16_t *fb;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    int w = lv_display_get_horizontal_resolution(d);
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * w + a->x1], px + (y * w + a->x1) * 2, (a->x2 - a->x1 + 1) * 2);
    lv_display_flush_ready(d);
}
static void run(int ms) { for (int t = 0; t < ms; t += 10) { now_ms += 10; lv_timer_handler(); } }
static void shot(const char *name)
{
    run(600);
    lv_refr_now(NULL);
    char p[128];
    snprintf(p, sizeof(p), "out/%s_%s.ppm", sim_round ? "tondo" : "349", name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", g_scr_w, g_scr_h);
    for (int i = 0; i < g_scr_w * g_scr_h; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void nav(nav_t e) { sim_nav(e); run(300); }

int main(int argc, char **argv)
{
    sim_round = argc > 1 && !strcmp(argv[1], "round");
    if (!sim_round) { g_scr_w = 640; g_scr_h = 172; }
    g_set.brightness = 80; g_set.accent = 0; g_set.wifi_on = 1;
    snprintf(g_set.wifi_ssid, sizeof(g_set.wifi_ssid), "Casa");
    ui_fonts_init();
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(g_scr_w, g_scr_h);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    fb = calloc(g_scr_w * g_scr_h, 2);
    void *buf = calloc(g_scr_w * g_scr_h, 2);
    lv_display_set_buffers(d, buf, NULL, g_scr_w * g_scr_h * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush);
    ui_init();
    shot("1_home");
    ui_push(&app_menu, &settings_menu);
    shot("2_impostazioni");
    nav(NAV_SELECT);                     // Wi-Fi
    shot("3_wifi");
    ui_pop(); run(300);
    for (int i = 0; i < 6; i++) nav(NAV_NEXT);
    shot("4_impostazioni_giu");
    ui_home(); run(300);
    ui_push(&app_wifiscan, "Wi-Fi » Reti");
    run(300);
    shot("5_reti");
    nav(NAV_SELECT);                     // tastiera password
    shot("6_tastiera");
    nav(NAV_NEXT);                       // scheda numeri
    shot("7_tastiera_numeri");
    nav(NAV_SELECT);                     // conferma -> esito
    shot("8_connessione");
    ui_home(); run(300);
    ui_push(&app_ota, NULL);
    shot("9_aggiornamento");
    sim_ota = OTA_DOWNLOADING;
    shot("10_download");
    ui_home(); run(300);
    ui_push(&app_portal, NULL);
    shot("11_portale");
    ui_home(); run(300);
    ui_push(&app_clock, NULL);
    shot("12_orologio");
    ui_home(); run(300);
    ui_toast("Su questo schermo arriva con un prossimo aggiornamento");
    shot("13_avviso");
    ui_home(); run(2000);
    ui_push(&app_menu, &seismo_menu);
    shot("14_sismo_menu");
    ui_push(&app_seismo, NULL);
    // 25 s di fondo (calibrazione), poi una scossa: si danno le letture 200 al secondo
    srand(3);
    for (int i = 0; i < 200 * 34; i++) {
        double t = i / 200.0, nz = ((rand() % 2001) - 1000) / 1000.0 * 0.002;
        double z = 1.0 + nz, x = ((rand() % 2001) - 1000) / 1000.0 * 0.002, y = 0;
        if (t > 28) { double a = 0.02 * exp(-(t - 28) / 2.5); z += a * sin(2 * M_PI * 6 * t); x += a * 0.6 * sin(2 * M_PI * 4 * t); }
        seismo_feed(x, y, z);
        if (i % 20 == 0) run(100);
    }
    shot("15_sismo_evento");
    for (int i = 0; i < 200 * 14; i++) {
        double z = 1.0 + ((rand() % 2001) - 1000) / 1000.0 * 0.002;
        seismo_feed(((rand() % 2001) - 1000) / 1000.0 * 0.002, 0, z);
        if (i % 20 == 0) run(100);
    }
    shot("16_sismo_ascolto");

    // Mappa Wi-Fi: una passeggiata finta in un edificio 30x15 m con 7 reti fisse,
    // un furgone che passa e l'hotspot di un telefono che segue
    ui_home(); run(300);
    {
        static const struct { const char *n; double x, y; uint8_t b0; } AP[] = {
            {"Casa", 2, 2, 0x10}, {"Casa_5G", 3, 2.5, 0x10}, {"Ufficio", 25, 3, 0x20}, {"Vicino", 14, 13, 0x30},
            {"Stampante", 28, 13, 0x40}, {"Sala", 12, 4, 0x50}, {"Garage", -2, 12, 0x60}, {"Furgone", 0, 0, 0x70},
            {"iPhone di Ugo", 0, 0, 0x72}};
        double px[] = {0, 28, 28, 0, 0, 14, 14, 28}, py[] = {1, 1, 13, 13, 1, 1, 13, 13}, L[7], tot = 0;
        for (int q = 0; q < 7; q++) { L[q] = hypot(px[q + 1] - px[q], py[q + 1] - py[q]); tot += L[q]; }
        srand(2);
        ui_push(&app_menu, &wifimap_menu);
        ui_push(&app_wifimap, NULL);
        wm_reset();   // dopo l'apertura, che riprende la mappa salvata
        for (int s = 0; s < 260; s++) {
            double u = fmod(s * 2.0, tot); int k = 0;
            while (u > L[k]) { u -= L[k]; k++; }
            double x = px[k] + (px[k + 1] - px[k]) * u / L[k], y = py[k] + (py[k + 1] - py[k]) * u / L[k];
            wifi_ap_t sc[12]; int n = 0;
            for (int a = 0; a < 9; a++) {
                double ax = AP[a].x, ay = AP[a].y;
                if (a == 7) { ax = 15 + 12 * sin(s * 0.08); ay = 18; }
                if (a == 8) { ax = x + 3; ay = y + 1; }
                double d = hypot(x - ax, y - ay); if (d < 0.5) d = 0.5;
                double g = ((rand() % 2001) - 1000) / 1000.0 * 6.9;   // ~4 dB di rumore
                double rssi = -40 - 27 * log10(d) + g;
                if (rssi < -90) continue;
                memset(&sc[n], 0, sizeof(sc[n]));
                snprintf(sc[n].ssid, 33, "%s", AP[a].n); sc[n].rssi = rssi; sc[n].bssid[0] = AP[a].b0; sc[n].bssid[5] = a; n++;
            }
            wm_add_scan(sc, n);
            if (s % 10 == 0) run(220);
        }
        run(400);
        shot("17_mappa");
        nav(NAV_NEXT); nav(NAV_NEXT);
        shot("18_mappa_rete");
        nav(NAV_SELECT);
        shot("19_mappa_reti");
        ui_pop(); run(300);
        sim_nav(NAV_HOLD); run(400);   // dito tenuto: condividi
        shot("21_condividi");
        nav(NAV_NEXT);
        shot("22_condividi_2");
        ui_pop(); run(300); ui_pop(); run(300);
        shot("20_mappa_menu");
    }
    return 0;
}
