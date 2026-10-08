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
#include <math.h>

extern int sim_round;
extern int16_t g_scr_w, g_scr_h;
extern nav_handler_t sim_nav;
extern wifi_ap_t sim_aps[32];
extern int sim_ap_n;
extern uint32_t sim_scan_gen;
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

    // Tester Wi-Fi: "Casa" esce da due apparecchi (router e ripetitore); camminando dal
    // router al ripetitore il migliore cambia, poi si esce dalla portata di entrambi
    ui_home(); run(300);
    {
        static const struct { const char *n; int ch; uint8_t b5; } AP[] = {
            {"Casa", 6, 0xA1}, {"Casa", 11, 0x3F}, {"Vodafone-A1B2C3", 1, 0x10}, {"Ospiti", 6, 0x20}};
        sim_ap_n = 4;
        for (int a = 0; a < 4; a++) {
            memset(&sim_aps[a], 0, sizeof(sim_aps[a]));
            snprintf(sim_aps[a].ssid, 33, "%s", AP[a].n);
            sim_aps[a].channel = AP[a].ch; sim_aps[a].auth = 3;
            sim_aps[a].bssid[0] = 0x24; sim_aps[a].bssid[4] = 0xC0; sim_aps[a].bssid[5] = AP[a].b5;
        }
        sim_aps[0].rssi = -50; sim_aps[1].rssi = -71; sim_aps[2].rssi = -66; sim_aps[3].rssi = -80;
        ui_push(&app_wifitest, NULL);
        sim_scan_gen++; run(500);
        shot("17_tester_reti");
        srand(4);
        // misura: 40 passi dal router (AP 1) al ripetitore (AP 2), poi lontano da tutti
        sim_ap_n = 2;
        nav(NAV_SELECT);
        for (int s = 0; s < 70; s++) {
            double t = s < 40 ? s / 40.0 : 1;
            double n1 = ((rand() % 2001) - 1000) / 1000.0 * 2, n2 = ((rand() % 2001) - 1000) / 1000.0 * 2;
            sim_aps[0].rssi = (int8_t)(-50 - 32 * t + n1);
            sim_aps[1].rssi = (int8_t)(-71 + 20 * t + n2);
            if (s == 40) shot("18_tester_ripetitore");
            sim_scan_gen++; run(300);
        }
        shot("19_tester_misura");
        sim_ap_n = 0;
        for (int s = 0; s < 6; s++) { sim_scan_gen++; run(300); }
        shot("20_tester_fuori");
        sim_ap_n = -1;
    }
    // Snake: partenza, una partita giocata da un pilota automatico, pausa
    ui_home(); run(300);
    {
        extern const app_t app_snake;
        extern int snake_sim_dir(void);
        srand(7);
        ui_push(&app_snake, NULL); run(300);
        shot("21_snake_via");
        sim_nav(NAV_SELECT); run(50);
        for (int s = 0; s < 900; s++) {
            int d = snake_sim_dir();
            static const nav_t NV[4] = {NAV_SELECT, NAV_PREV, NAV_BACK, NAV_NEXT};   // destra, giù, sinistra, su
            sim_nav(NV[d]);
            run(60);
        }
        shot("22_snake_gioco");
        sim_nav(NAV_BTN); run(300);
        shot("23_snake_pausa");
        ui_pop(); run(300);
    }
    // Scacchi: una partita col cursore (BOOT sceglie), suggerimento, abbandono, analisi,
    // allenamento, Elo ed elenco delle partite (l'archivio va nella "microSD" del PC)
    ui_home(); run(300);
    {
        extern menu_t chessplay_menu;
        ui_push(&app_menu, &chessplay_menu); run(300);
        shot("24_scacchi_menu");
        nav(NAV_SELECT); run(300);   // Gioca contro il Gadget (col bianco)
        int cf = 4, cr = 1;
        #define GOTO(f, r) do { while (cf < (f)) { nav(NAV_SELECT); cf++; } while (cf > (f)) { nav(NAV_BACK); cf--; } \
                                while (cr < (r)) { nav(NAV_NEXT); cr++; } while (cr > (r)) { nav(NAV_PREV); cr--; } \
                                sim_nav(NAV_BTN); run(200); } while (0)
        sim_nav(NAV_BTN); run(200);    // compare il cursore su e2
        sim_nav(NAV_BTN); run(200);    // sceglie il pedone
        shot("25_scacchi_scelta");
        GOTO(4, 3); run(1200);         // e4, poi risponde il Gadget
        GOTO(6, 0); GOTO(5, 2); run(1200);   // Cf3
        GOTO(5, 0); GOTO(2, 3); run(1200);   // Ac4
        shot("26_scacchi_partita");
        sim_nav(NAV_HOLD); run(400);
        shot("27_scacchi_menu_partita");
        nav(NAV_SELECT); run(600);     // suggerimento
        shot("28_scacchi_suggerimento");
        sim_nav(NAV_HOLD); run(400);
        for (int i = 0; i < 4; i++) nav(NAV_NEXT);
        nav(NAV_SELECT); nav(NAV_SELECT); run(600);   // abbandona (con conferma)
        shot("29_scacchi_fine");
        nav(NAV_SELECT); run(600);     // analisi della partita
        shot("30_scacchi_analisi");
        nav(NAV_SELECT); run(300);     // prossimo errore
        shot("31_scacchi_errore");
        sim_nav(NAV_BTN); run(300);    // la mossa migliore
        shot("32_scacchi_migliore");
        ui_pop(); run(300); ui_pop(); run(300);
        chessplay_menu.sel = 4; ui_push(&app_menu, &chessplay_menu); run(200);
        nav(NAV_SELECT); run(400);
        shot("33_scacchi_allenamento");
        ui_pop(); run(300);
        nav(NAV_PREV); nav(NAV_SELECT); run(400);
        shot("34_scacchi_partite");
        ui_pop(); run(300);
        nav(NAV_NEXT); nav(NAV_NEXT); nav(NAV_SELECT); run(400);
        shot("35_scacchi_elo");
        ui_pop(); run(300);
        nav(NAV_NEXT); nav(NAV_SELECT); run(400);   // Esporta per un'IA: QR sul telefono
        shot("36_scacchi_telefono");
        nav(NAV_NEXT); run(200);
        shot("37_scacchi_telefono_2");
        ui_pop(); run(300);
        chessplay_menu.sel = 2; ui_pop(); run(200);   // partita a due: sul 3,49" c'è il pulsante Conferma
        ui_push(&app_menu, &chessplay_menu); run(200);
        nav(NAV_SELECT); run(300);
        sim_nav(NAV_BTN); run(200); sim_nav(NAV_BTN); run(200); nav(NAV_NEXT); nav(NAV_NEXT); sim_nav(NAV_BTN); run(300);
        shot("38_scacchi_due");
        ui_pop(); run(300); ui_pop(); run(300);
        #undef GOTO
    }
    // Morse: menu, corso d'ascolto (risposta), corso di trasmissione, Telegrafo, tabella
    ui_home(); run(300);
    {
        extern menu_t morse_menu;
        ui_push(&app_menu, &morse_menu); run(300);
        shot("40_morse_menu");
        nav(NAV_SELECT); run(1500);
        shot("41_morse_ascolta");
        nav(NAV_SELECT); run(300);
        shot("42_morse_risposta");
        ui_pop(); run(300);
        nav(NAV_NEXT); nav(NAV_SELECT); run(400);
        shot("43_morse_batti");
        ui_pop(); run(300);
        nav(NAV_NEXT); nav(NAV_SELECT); run(400);
        shot("44_morse_telegrafo");
        ui_pop(); run(300);
        nav(NAV_NEXT); nav(NAV_NEXT); nav(NAV_SELECT); run(300);
        shot("45_morse_tabella");
        ui_pop(); run(300); ui_pop(); run(300);
    }
    // Azione rapida: l'elenco di tutte le app e delle voci interne
    ui_home(); run(300);
    shortcut_pick(false); run(300);
    for (int i = 0; i < 6; i++) nav(NAV_NEXT);
    shot("46_azione_rapida");
    ui_pop(); run(300);
    return 0;
}
