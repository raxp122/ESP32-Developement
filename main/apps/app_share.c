// app_share.c — "Sul telefono": un file della microSD passa al telefono senza Internet.
// Il Gadget apre un hotspot (aperto) che serve una pagina col testo e i pulsanti Copia tutto
// e Scarica il file. Due QR: il primo collega il telefono all'hotspot (di solito la pagina si
// apre da sola), il secondo apre la pagina. Uscendo l'hotspot si spegne e il Wi-Fi torna com'era.
#include "apps.h"
#include "wifi_mgr.h"
#include "sd.h"
#include <stdio.h>
#include <string.h>

static share_req_t req;
static lv_obj_t *s_qr[2], *s_cap[2], *s_status, *s_text;
static int s_step;            // tondo: quale QR si vede
static bool s_ok;
static char s_wifi_qr[64];

static lv_obj_t *mkl(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

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

static lv_obj_t *mkqr(lv_obj_t *root, int size, const char *data)
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
    if (arg) req = *(const share_req_t *)arg;
    bool r = SCR_ROUND;
    s_ok = false;
    s_step = 0;
    s_text = mkl(root, &font_s, C_TEXT);
    s_status = mkl(root, &font_s, C_DIM);
    FILE *f = sd_ok() ? fopen(req.path, "r") : NULL;
    if (!f) {
        ui_set_text(s_text, sd_ok() ? "Il file non c'è sulla microSD" : "Serve la microSD");
        lv_obj_align(s_text, LV_ALIGN_CENTER, 0, 0);
        return;
    }
    fclose(f);
    wifi_mgr_portal_start_file(req.path, req.download_name, req.title);
    snprintf(s_wifi_qr, sizeof(s_wifi_qr), "WIFI:T:nopass;S:%s;;", wifi_mgr_portal_ssid());
    s_ok = true;
    if (r) {   // tondo: un QR alla volta, grande, al centro
        for (int i = 0; i < 2; i++) {
            s_cap[i] = mkl(root, &font_m, ui_accent());
            lv_obj_set_width(s_cap[i], 340);
            lv_obj_set_style_text_align(s_cap[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_cap[i], LV_ALIGN_TOP_MID, 0, 0);
            s_qr[i] = mkqr(root, 196, i ? "http://192.168.4.1/" : s_wifi_qr);
            lv_obj_align(s_qr[i], LV_ALIGN_TOP_MID, 0, 36);
        }
        ui_set_text(s_cap[0], "1 · Collegati");
        ui_set_text(s_cap[1], "2 · Apri la pagina");
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
            s_qr[i] = mkqr(root, 108, i ? "http://192.168.4.1/" : s_wifi_qr);
            lv_obj_set_pos(s_qr[i], 14 + i * 140, 2);
            s_cap[i] = mkl(root, &font_s, ui_accent());
            lv_label_set_text(s_cap[i], i ? "2 · Apri la pagina" : "1 · Collegati");
            lv_obj_set_width(s_cap[i], 132);
            lv_obj_set_style_text_align(s_cap[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(s_cap[i], 2 + i * 140, 128);
        }
        lv_obj_set_width(s_text, SCR_W - 306);
        lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(s_text, 296, 6);
        lv_label_set_text(s_text, "Inquadra il primo QR con la fotocamera: il telefono si collega all'hotspot e di "
                                  "solito la pagina si apre da sola; se no, il secondo QR. Sulla pagina: Copia tutto "
                                  "(da incollare nella chat) o Scarica il file.");
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

static const char *s_title(void *arg) { return req.title; }

const app_t app_share = {
    .name = "Sul telefono", .icon = ICON_MOBILE,
    .enter = s_enter, .leave = s_leave, .nav = s_nav, .tick = s_tick, .title = s_title,
    .flags = APP_NO_SLEEP | APP_ROUND_OK,
};
