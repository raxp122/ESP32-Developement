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
    return 0;
}
