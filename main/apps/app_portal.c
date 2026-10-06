// app_portal.c — configurazione Wi-Fi dal telefono tramite hotspot e pagina web
#include "apps.h"
#include "wifi_mgr.h"
#include "settings.h"
#include <stdio.h>

static lv_obj_t *l_step1, *l_step2, *l_status;

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void tick(void)
{
    char b[96];
    lv_color_t c;
    wifi_mgr_portal_poll();
    if (wifi_mgr_portal_saved()) {
        switch (wifi_mgr_state()) {
        case WIFI_CONNECTED:
            snprintf(b, sizeof(b), LV_SYMBOL_OK "  Connesso a %s · %s", g_set.wifi_ssid, wifi_mgr_ip());
            c = C_OK;
            break;
        case WIFI_CONNECTING:
            snprintf(b, sizeof(b), "Mi collego a %s…", g_set.wifi_ssid);
            c = ui_accent();
            break;
        default:
            snprintf(b, sizeof(b), "Connessione non riuscita: controlla la password");
            c = C_WARN;
        }
    } else if (wifi_mgr_portal_clients() > 0) {
        snprintf(b, sizeof(b), "Telefono collegato: apri la pagina");
        c = ui_accent();
    } else {
        snprintf(b, sizeof(b), "In attesa del telefono…");
        c = C_DIM;
    }
    // solo se cambia: ogni modifica ridisegna e invia l'intero schermo
    ui_set_text(l_status, b);
    ui_set_text_color(l_status, c);
}

static void enter(lv_obj_t *root, void *arg)
{
    wifi_mgr_portal_start();
    l_step1 = mk(root, &font_m, C_TEXT, 24, 14);
    lv_label_set_text(l_step1, "1  Dal telefono collegati alla rete");
    lv_obj_t *v1 = mk(root, &font_m, ui_accent(), 0, 14);
    lv_label_set_text(v1, wifi_mgr_portal_ssid());
    lv_obj_align_to(v1, l_step1, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    l_step2 = mk(root, &font_m, C_TEXT, 24, 50);
    lv_label_set_text(l_step2, "2  Si apre la pagina, altrimenti vai su");
    lv_obj_t *v2 = mk(root, &font_m, ui_accent(), 0, 50);
    lv_label_set_text(v2, "192.168.4.1");
    lv_obj_align_to(v2, l_step2, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    l_status = mk(root, &font_l, C_DIM, 24, 92);
    lv_obj_set_width(l_status, SCR_W - 48);
    lv_label_set_long_mode(l_status, LV_LABEL_LONG_DOT);
    tick();
}

static void leave(void) { wifi_mgr_portal_stop(); }

const app_t app_portal = {
    .name = "Configura Wi-Fi", .icon = ICON_MOBILE,
    .enter = enter, .leave = leave, .tick = tick, .flags = APP_NO_SLEEP,
};
