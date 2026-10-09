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
#include "pet.h"
#include "pet_art.h"
#include <math.h>
#include <unistd.h>
#include "board.h"

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
    g_set.brightness = 80; g_set.accent = 0; g_set.wifi_on = 1; g_set.pet_clock = 1; g_set.pet_steps = 1;
    g_set.a4_x10 = 4400; g_set.volume = 70; g_set.saber_clash = 1;
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
    pet_init();
    if (argc > 2 && !strcmp(argv[2], "pet")) goto pet;
    if (argc > 2 && !strcmp(argv[2], "apps")) goto apps;
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
        // inversione a U con un solo gesto a "L": giù e poi a sinistra, il dito sempre giù
        extern int sim_touch, sim_tx, sim_ty;
        ui_push(&app_snake, NULL); run(300);
        sim_nav(NAV_SELECT); run(200);   // parte verso destra
        sim_tx = 300; sim_ty = 70; sim_touch = 1; run(40);
        sim_ty = 100; run(40);           // giù di 30 px: prima svolta
        sim_tx = 270; run(40);           // a sinistra di 30 px: seconda svolta
        sim_touch = 0; run(500);
        shot("24_snake_inversione");
        ui_pop(); run(300);
        // swipe lento verso destra con un punto fantasma al centro a metà: deve svoltare a destra
        ui_push(&app_snake, NULL); run(300);
        sim_nav(NAV_NEXT); run(200);     // parte verso l'alto
        sim_touch = 1;
        for (int i = 0; i <= 12; i++) {
            sim_tx = (sim_round ? 260 : 400) + i * 4; sim_ty = sim_round ? 300 : 120;
            if (i == 6) { sim_tx = sim_round ? 233 : 320; sim_ty = sim_round ? 233 : 86; }   // fantasma
            run(20);
        }
        sim_touch = 0; run(300);
        shot("25_snake_swipe_lento");
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
    // Radar: la mascotte nelle sue espressioni (motore finto, vedi stubs.c)
    if (!sim_round) {
        extern menu_t radar_menu;
        extern int sim_pwn_near, sim_pwn_run;
        extern uint32_t sim_pwn_hs, sim_pwn_nets;
        ui_home(); run(300);
        ui_push(&app_menu, &radar_menu); run(200);
        nav(NAV_SELECT); run(1000);
        sim_pwn_near = 3; run(700); shot("80_radar_guarda");
        sim_pwn_nets++; run(300); shot("81_radar_contento");
        run(2000); sim_pwn_hs++; run(400); shot("82_radar_handshake");
        run(3000); sim_pwn_near = 0; run(600); shot("83_radar_annoiato");
        sim_pwn_near = 8; run(600); shot("84_radar_caccia");
        sim_pwn_run = 0; run(600); shot("85_radar_dorme");
        sim_pwn_run = 1; sim_pwn_near = 3;
        ui_home(); run(300);
    }
    // Dadi: preferiti (incantesimi) e tiro con su/giù fra i preferiti
    {
        extern menu_t dice_menu;
        extern void dice_sim_add_fav(const char *, const int *, int);
        static const int fb[7] = {0, 8, 0, 0, 0, 0, 0}, mm[7] = {3, 0, 0, 0, 0, 0, 0}, cw[7] = {0, 0, 1, 0, 0, 0, 0};
        dice_sim_add_fav("Palla di fuoco", fb, 0);
        dice_sim_add_fav("Dardo incantato", mm, 3);
        dice_sim_add_fav("Cura ferite", cw, 3);
        ui_home(); run(300);
        ui_push(&app_menu, &dice_menu); run(200);
        for (int i = 0; i < 11; i++) nav(NAV_NEXT);   // Preferiti
        shot("90_dadi_menu");
        nav(NAV_SELECT);
        shot("91_dadi_preferiti");
        nav(NAV_SELECT); run(1200);
        shot("92_dadi_palla_di_fuoco");
        nav(NAV_NEXT); run(200);
        shot("93_dadi_dardo");
        nav(NAV_SELECT); run(1200);
        shot("94_dadi_dardo_tiro");
        ui_pop(); run(300);                           // all'elenco dei preferiti
        for (int i = 0; i < 3; i++) nav(NAV_NEXT);   // Modifica un preferito
        nav(NAV_SELECT);
        nav(NAV_SELECT);                              // Palla di fuoco
        nav(NAV_NEXT); nav(NAV_NEXT);                 // d6
        shot("95_dadi_modifica");
        ui_home(); run(300);
    }
    // Diagnostica senza monitor seriale
    ui_home(); run(300);
    ui_push(&app_menu, &settings_menu); run(200);
    settings_menu.sel = settings_menu.count - 1;
    ui_pop(); run(100); ui_push(&app_menu, &settings_menu); run(200);
    nav(NAV_SELECT);
    shot("48_diagnostica");
    ui_home(); run(300);
apps:
    // Le altre app (solo 3,49": sul tondo non si aprono ancora), per il README
    if (!sim_round) {
        extern int sim_tasks, sim_audio, sim_touch, sim_tx, sim_ty;
        extern float sim_mic_hz, sim_mic_amp;
        extern long long sim_timer_us;
        extern vec3_t sim_acc;
        #define TAP(x, y) do { sim_tx = (x); sim_ty = (y); sim_touch = 1; run(60); sim_touch = 0; run(300); } while (0)
        extern const app_t app_8ball, app_tuner, app_bercio, app_level, app_q20, app_theremin, app_torch,
                           app_search, app_blescan, app_clip_list;
        extern menu_t tuner_menu, clips_menu, chess_menu, saber_menu;
        // 8-Ball
        ui_home(); run(300);
        ui_push(&app_8ball, NULL);
        shot("100_8ball");
        nav(NAV_SELECT); run(3000);
        shot("101_8ball_risposta");
        // Accordatore: chitarra standard, un La appena calante; poi cromatico
        ui_home(); run(300);
        ui_push(&app_menu, &tuner_menu); run(200);
        shot("102_accordatore_menu");
        sim_tasks = 1; sim_audio = 1; sim_mic_amp = 6000; sim_mic_hz = 108.6f;
        nav(NAV_NEXT); nav(NAV_SELECT); nav(NAV_SELECT);
        for (int i = 0; i < 20; i++) { usleep(100000); run(100); }
        shot("103_accordatore_chitarra");
        ui_home(); run(300);
        sim_mic_hz = 442.0f;
        ui_push(&app_tuner, NULL);
        for (int i = 0; i < 20; i++) { usleep(100000); run(100); }
        shot("104_accordatore_cromatico");
        ui_home(); run(300);
        // Berciometro: una voce forte
        sim_mic_hz = 300; sim_mic_amp = 1200;
        ui_push(&app_bercio, NULL);
        for (int i = 0; i < 25; i++) { usleep(100000); run(100); }
        shot("105_berciometro");
        ui_home(); run(300);
        sim_mic_amp = 0; sim_audio = 0;
        // Appunti
        ui_push(&app_menu, &clips_menu); run(200);
        shot("106_appunti_menu");
        ui_push(&app_clip_list, NULL); run(300);
        shot("107_appunti_codici");
        ui_home(); run(300);
        // Livella: calibrazione, poi in piano e sul lato
        ui_push(&app_level, NULL); run(300);
        shot("108_livella_calibrazione");
        sim_acc = (vec3_t){0, 0, 1}; nav(NAV_SELECT); run(2500);
        sim_acc = (vec3_t){0, -1, 0}; nav(NAV_SELECT); run(2500);
        sim_acc = (vec3_t){0.035f, -0.02f, 0.999f}; run(2500);
        shot("109_livella_piano");
        sim_acc = (vec3_t){0.06f, -0.998f, 0}; run(2500);
        shot("110_livella_lato");
        sim_acc = (vec3_t){0, 0, 1};
        ui_home(); run(300);
        // Orologio scacchi: menu e una partita in corso
        ui_push(&app_menu, &chess_menu); run(200);
        shot("111_orologio_scacchi_menu");
        nav(NAV_SELECT); run(300);
        TAP(160, 90);                                  // il nero fa partire il bianco
        for (int i = 0; i < 37; i++) { sim_timer_us += 1000000; run(100); }
        TAP(480, 90);                                  // il bianco muove
        for (int i = 0; i < 12; i++) { sim_timer_us += 1000000; run(100); }
        shot("112_orologio_scacchi_partita");
        sim_nav(NAV_BTN); run(300);
        sim_nav(NAV_BACK); run(300);
        ui_home(); run(300);
        // Q-20: una partita a metà
        ui_push(&app_q20, NULL); run(300);
        shot("113_q20");
        TAP(150, 140);
        TAP(80, 140); TAP(560, 140);
        shot("114_q20_domanda");
        ui_home(); run(300);
        // Spada laser
        ui_push(&app_menu, &saber_menu); run(200);
        shot("115_spada_menu");
        nav(NAV_SELECT); run(300);
        nav(NAV_SELECT); run(3500);
        shot("116_spada_accesa");
        sim_nav(NAV_BACK); run(1500);
        ui_home(); run(300);
        // Theremin: il dito sulla zona a sinistra
        sim_audio = 1;
        ui_push(&app_theremin, NULL); run(300);
        sim_acc = (vec3_t){0, 0.42f, 0.91f};          // inclinata: la nota sale
        sim_tx = 60; sim_ty = 60; sim_touch = 1; run(600);
        shot("117_theremin");
        sim_touch = 0; run(600);
        sim_audio = 0; sim_acc = (vec3_t){0, 0, 1};
        ui_home(); run(300);
        // Torcia
        ui_push(&app_torch, NULL); run(300);
        shot("118_torcia");
        nav(NAV_NEXT); run(300);
        shot("119_torcia_colore");
        ui_home(); run(300);
        // Cerca: "dadi"
        ui_push(&app_search, NULL); run(300);
        TAP(2 * 64 + 32, 46 + 42 + 21); TAP(0 * 64 + 32, 46 + 42 + 21); TAP(2 * 64 + 32, 46 + 42 + 21); TAP(7 * 64 + 32, 46 + 21);
        shot("120_cerca");
        ui_home(); run(300);
        // Scanner Bluetooth
        ui_push(&app_blescan, NULL); run(1500);
        shot("121_scanner_bt");
        ui_home(); run(300);
        sim_tasks = 0;
    }
    if (argc > 2 && !strcmp(argv[2], "apps")) return 0;
pet:
    // Polipetto: una polpa saggia viola a puntini, con corona e una decorazione
    ui_home(); run(300);
    {
        extern int sim_touch;
        extern wifi_espnow_cb_t sim_espnow_cb;
        pet_t *p = pet_get();
        pet_core_new_egg(p, 1234);
        uint8_t g[GENE_COUNT][2] = {{COL_PURPLE, COL_GOLD}, {PAT_SPOTS, PAT_NONE}, {TENT_LONG, TENT_NORMAL}, {TEMP_LIVELY, TEMP_CALM}};
        memcpy(p->genes, g, sizeof(g));
        snprintf(p->parent[0], PET_NAME_LEN, "Perla");
        snprintf(p->parent[1], PET_NAME_LEN, "Guizzo");
        pet_clock_t c = {.hour = -1, .sleep_h = -1, .wake_h = -1};
        pet_core_step(p, 61, &c);   // si schiude: nome e sesso
        p->sex = SEX_F;
        snprintf(p->name, PET_NAME_LEN, "Ottavia");
        p->stage = PET_ADULT; p->form = FORM_SAGE; p->weight = 31;
        p->hunger = p->thirst = 4; p->happy = 3; p->discipline = 3; p->age_s = 9 * 86400;
        p->poop = 0; p->needs = 0; p->asleep = 0; p->sick = 0; p->tantrum = 0;
        pet_world_t *w = pet_world();
        w->shells = 57; w->owned = (1u << 7) | (1u << 18) | (1u << 14); w->hat = 7; w->deco = (1u << 18) | (1u << 14);
        w->colors_seen = (1 << COL_ORANGE) | (1 << COL_PURPLE) | (1 << COL_BLUE); w->forms_seen = 1 << FORM_SAGE;
        w->best[REC_MEMORY] = 7; w->best[REC_RHYTHM] = 41;
        pet_diary_add("È nata Ottavia, figlia di Perla e Guizzo");
        pet_diary_add("Ottavia ora è: Polpo saggio");
        pet_diary_add("Comprato: Corona");
        if (sim_round) {   // il Polipetto c'è solo sulla 3,49"; sul tondo si vede nell'orologio
            ui_push(&app_clock, NULL);
            shot("47_orologio_polipetto");
            ui_home(); run(300);
            return 0;
        }
        ui_push(&app_pet, NULL); run(500);
        shot("50_polipetto");
        run(3500);   // finisce la festa (regalo della Giornata del polpo, se è oggi)
        nav(NAV_NEXT); nav(NAV_SELECT);            // Gioca
        shot("51_gioca_menu");
        nav(NAV_SELECT); run(900);                 // Gioca veloce: palla
        shot("52_gioca_palla");
        run(3000);
        nav(NAV_SELECT); nav(NAV_NEXT); nav(NAV_SELECT);   // menu Gioca, 1 2 3 stella
        run(2500); sim_touch = 1; run(900);
        shot("53_stella_avanti");
        for (int i = 0; i < 200 && 1; i++) { run(50); }
        sim_touch = 0;
        shot("54_stella");
        sim_nav(NAV_BACK); run(300);
        nav(NAV_NEXT); nav(NAV_SELECT); run(1500);   // Memoria
        shot("55_memoria");
        sim_nav(NAV_BTN); run(300);
        nav(NAV_NEXT); nav(NAV_SELECT); run(2900);   // Ritmo
        shot("56_ritmo");
        sim_nav(NAV_BACK); run(300);
        sim_nav(NAV_BACK); run(300);                 // al menu principale
        p->t_reward = 0; p->snacks_today = 1;
        sim_nav(NAV_PREV); run(100);                 // Cibo
        nav(NAV_SELECT); nav(NAV_NEXT);
        shot("57_spuntino");
        sim_nav(NAV_BACK); run(200);
        sim_nav(NAV_PREV); run(100);                 // Diario e altro
        nav(NAV_SELECT);
        shot("58_altro");
        nav(NAV_NEXT); nav(NAV_SELECT);              // Diario
        shot("59_diario");
        sim_nav(NAV_BACK); run(200);
        nav(NAV_NEXT); nav(NAV_SELECT);              // Album
        shot("60_album");
        sim_nav(NAV_PREV); run(200);
        shot("61_collezione");
        sim_nav(NAV_BACK); run(200);
        nav(NAV_NEXT); nav(NAV_SELECT); nav(NAV_NEXT);   // Famiglia: colore
        shot("62_geni_colore");
        nav(NAV_NEXT);
        shot("63_geni_motivo");
        sim_nav(NAV_BACK); run(200);
        nav(NAV_NEXT); nav(NAV_SELECT);              // Negozio
        for (int i = 0; i < 2; i++) nav(NAV_NEXT);
        shot("64_negozio_cuffia");
        for (int i = 0; i < 8; i++) nav(NAV_NEXT);
        shot("65_negozio_occhiali");
        for (int i = 0; i < 7; i++) nav(NAV_NEXT);
        shot("66_negozio_castello");
        sim_nav(NAV_BACK); run(200);
        nav(NAV_NEXT); nav(NAV_SELECT); run(500);    // Incontra un amico
        shot("67_amico_cerca");
        // arriva un altro Gadget: un maschio adulto azzurro a strisce, col cilindro
        struct __attribute__((packed)) {
            char magic[4]; uint8_t ver, flags; uint32_t uid, peer; char name[PET_NAME_LEN], family[PET_NAME_LEN];
            uint8_t sex, stage, form, hat, acc, asleep, nest; uint8_t genes[GENE_COUNT][2]; uint16_t gen; uint8_t gift_seq, gift_n;
        } k = {{'G', 'P', 'E', 'T'}, 1, 0, 0xBEEF, 0, "Nettuno", "Scogliera", SEX_M, PET_ADULT, FORM_EXPLORER, 0, 11, 0, 0,
               {{COL_BLUE, COL_BLUE}, {PAT_STRIPES, PAT_STRIPES}, {0, 0}, {0, 0}}, 3, 0, 0};
        // e un'altra, una bimba corallo: con due vicini si sceglie dall'elenco
        __typeof__(k) k2 = k;
        k2.uid = 0xCAFE; snprintf(k2.name, PET_NAME_LEN, "Bollicina"); snprintf(k2.family, PET_NAME_LEN, "Maree");
        k2.sex = SEX_F; k2.stage = PET_CHILD; k2.form = FORM_BASE; k2.acc = 9;
        uint8_t g2[GENE_COUNT][2] = {{COL_CORAL, COL_CORAL}, {PAT_NONE, PAT_NONE}, {TENT_LONG, TENT_LONG}, {0, 0}};
        memcpy(k2.genes, g2, sizeof(g2));
        k.peer = p->uid;   // Nettuno ha scelto noi
        for (int i = 0; i < 3; i++) {
            sim_espnow_cb(NULL, (const uint8_t *)&k2, sizeof(k2)); sim_espnow_cb(NULL, (const uint8_t *)&k, sizeof(k)); run(300);
        }
        shot("68_amici_vicini");
        nav(NAV_NEXT);
        shot("68b_amici_vicini_2");
        nav(NAV_SELECT);   // compare Nettuno
        for (int i = 0; i < 20; i++) {
            sim_espnow_cb(NULL, (const uint8_t *)&k2, sizeof(k2)); sim_espnow_cb(NULL, (const uint8_t *)&k, sizeof(k)); run(300);
        }
        shot("68c_amico_visita");
        nav(NAV_SELECT);
        k.flags = 1;
        for (int i = 0; i < 4; i++) {
            sim_espnow_cb(NULL, (const uint8_t *)&k2, sizeof(k2)); sim_espnow_cb(NULL, (const uint8_t *)&k, sizeof(k)); run(300);
        }
        shot("69_amico_uovo");
        sim_nav(NAV_BACK); run(300);
        sim_nav(NAV_BACK); run(300);
        shot("70_polipetto_nido");
        ui_home(); run(300);
        ui_push(&app_clock, NULL);
        shot("47_orologio_polipetto");
        ui_home(); run(300);
    }
    return 0;
}
