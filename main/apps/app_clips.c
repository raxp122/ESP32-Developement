// app_clips.c — "Appunti": i testi mandati dal PC (vedi clips.c / clip_ble.c), da
// ridigitare su un altro PC via USB (vedi usbhid.c).
//   Ricevi dal PC        → il Gadget si fa trovare via Bluetooth; dal browser si incollano i testi
//   Scarica la pagina    → hotspot Wi-Fi con la pagina da salvare sul PC
//   Codici salvati       → la lista; swipe a destra su una voce la digita via USB
//   Layout tastiera, Cancella tutti
#include "apps.h"
#include "clips.h"
#include "usbhid.h"
#include "ble_mgr.h"
#include "wifi_mgr.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

/* ---------------- anteprima di un testo ---------------- */

// una riga leggibile: niente a capo, troncata; i caratteri strani diventano spazi
static void preview(const clip_t *c, char *b, int n)
{
    int o = 0;
    for (int i = 0; i < c->len && o < n - 1; i++) {
        unsigned char ch = c->text[i];
        if (ch == '\n' || ch == '\r' || ch == '\t') { if (o && b[o - 1] != ' ') b[o++] = ' '; continue; }
        if (ch < 0x20) continue;
        // un carattere UTF-8 entra intero o per niente (mezzo carattere si vedrebbe come un quadratino)
        int l = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
        if (o + l > n - 1 || i + l > c->len) break;
        memcpy(b + o, c->text + i, l);
        o += l;
        i += l - 1;
    }
    b[o] = 0;
}

static void when(const clip_t *c, char *b, int n)
{
    if (!c->ts) { snprintf(b, n, "%d caratteri", c->len); return; }
    time_t t = c->ts;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(b, n, "%02d/%02d %02d:%02d · %d caratteri", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, c->len);
}

/* ================= lista dei codici ================= */

static int sel;
static uint32_t seen_gen;
static list_view_t lv;
static char rows[3][64];
static uint32_t sel_id;   // voce selezionata: la si ritrova anche se arrivano testi nuovi
static char title_buf[40];

static const char *row_text(int i, char *buf)
{
    clip_t c;
    if (!clips_get(i, &c)) return NULL;
    preview(&c, buf, 60);
    return buf;
}

static void list_render(int dir)
{
    int n = clips_count();
    if (n == 0) {
        list_view_set(&lv, LV_SYMBOL_COPY, NULL, "Nessun codice salvato",
                      "Usa 'Ricevi dal PC' e incolla dal browser", NULL, 0, 0, dir);
        return;
    }
    if (sel >= n) sel = n - 1;
    clip_t c;
    clips_get(sel, &c);
    sel_id = c.id;
    char sub[80];
    when(&c, sub, sizeof(sub));
    if (c.used) snprintf(sub + strlen(sub), sizeof(sub) - strlen(sub), " · " LV_SYMBOL_OK);
    list_view_set(&lv, LV_SYMBOL_COPY, sel > 0 ? row_text(sel - 1, rows[0]) : NULL,
                  row_text(sel, rows[1]), sub, sel + 1 < n ? row_text(sel + 1, rows[2]) : NULL, sel, n, dir);
}

static void detail_open(void);

static void list_enter(lv_obj_t *root, void *arg)
{
    list_view_create(&lv, root);
    seen_gen = clips_gen();
    if (sel >= clips_count()) sel = 0;
    list_render(0);
}

static void list_tick(void)
{
    if (clips_gen() == seen_gen) return;
    seen_gen = clips_gen();
    clip_t c;
    for (int i = 0; clips_get(i, &c); i++) if (c.id == sel_id) { sel = i; break; }
    if (sel >= clips_count()) sel = 0;
    list_render(0);
}

static bool list_nav(nav_t ev)
{
    int n = clips_count();
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n) { sel++; list_render(+1); } return true;
    case NAV_PREV: if (sel > 0) { sel--; list_render(-1); } return true;
    case NAV_SELECT: if (n) detail_open(); return true;
    default: return false;
    }
}

static const char *list_title(void *arg)
{
    snprintf(title_buf, sizeof(title_buf), "Codici salvati · %d", clips_count());
    return title_buf;
}

const app_t app_clip_list = {
    .name = "Codici salvati", .icon = LV_SYMBOL_COPY,
    .enter = list_enter, .nav = list_nav, .tick = list_tick, .title = list_title,
};

/* ================= dettaglio e digitazione ================= */

static clip_t cur;
static lv_obj_t *d_text, *d_state, *d_hint;
static enum { D_IDLE, D_CONFIRM, D_TYPING, D_DONE, D_CONFIRM_DEL } d_mode;
static volatile int d_typed;
static volatile bool d_typing_done;
static TaskHandle_t d_task;
static uint32_t d_confirm_until;

static void detail_render(void)
{
    char t[96];
    const char *hint;
    lv_color_t col = C_DIM;
    switch (d_mode) {
    case D_CONFIRM:
        snprintf(t, sizeof(t), LV_SYMBOL_WARNING " Swipe a destra di nuovo per digitare");
        col = C_WARN;
        hint = "Metti il cursore sul PC dove vuoi scrivere · sinistra: annulla";
        break;
    case D_CONFIRM_DEL:
        snprintf(t, sizeof(t), LV_SYMBOL_TRASH " BOOT di nuovo per cancellarlo");
        col = C_WARN;
        hint = "Qualsiasi altro gesto annulla";
        break;
    case D_TYPING:
        snprintf(t, sizeof(t), "Sto digitando…");
        col = ui_accent();
        hint = "Non toccare il PC · sinistra: ferma";
        break;
    case D_DONE:
        snprintf(t, sizeof(t), LV_SYMBOL_OK " Digitati %d caratteri", d_typed);
        col = C_OK;
        hint = "Swipe a destra: di nuovo · sinistra: indietro";
        break;
    default:
        if (!usbhid_supported()) { snprintf(t, sizeof(t), "Digitazione USB non disponibile in questo firmware"); hint = "Puoi comunque vedere e cancellare i codici"; }
        else if (!usbhid_mounted()) { snprintf(t, sizeof(t), "Collega la USB-C al PC"); hint = "Poi swipe a destra per digitare · BOOT: cancella"; }
        else { snprintf(t, sizeof(t), "Pronto a digitare come tastiera"); col = C_OK; hint = "Swipe a destra: digita · BOOT: cancella"; }
    }
    ui_set_text(d_state, t);
    ui_set_text_color(d_state, col);
    ui_set_text(d_hint, hint);
}

static void detail_enter(lv_obj_t *root, void *arg)
{
    clips_get(sel, &cur);
    d_mode = D_IDLE;
    d_typed = 0;
    if (usbhid_supported()) usbhid_begin();

    d_text = lv_label_create(root);
    lv_obj_set_style_text_font(d_text, &font_m, 0);
    lv_obj_set_style_text_color(d_text, C_TEXT, 0);
    lv_obj_set_pos(d_text, 24, 10);
    lv_obj_set_size(d_text, SCR_W - 48, 76);
    lv_label_set_long_mode(d_text, LV_LABEL_LONG_WRAP);
    char pv[200];
    preview(&cur, pv, sizeof(pv));   // anteprima su una riga, senza svelare tutto a capo
    lv_label_set_text(d_text, pv);

    d_state = lv_label_create(root);
    lv_obj_set_style_text_font(d_state, &font_m, 0);
    lv_obj_set_pos(d_state, 24, 92);
    lv_obj_set_width(d_state, SCR_W - 48);
    lv_label_set_long_mode(d_state, LV_LABEL_LONG_DOT);

    d_hint = lv_label_create(root);
    lv_obj_set_style_text_font(d_hint, &font_s, 0);
    lv_obj_set_style_text_color(d_hint, C_DIM, 0);
    lv_obj_set_pos(d_hint, 24, 124);
    lv_obj_set_width(d_hint, SCR_W - 48);
    lv_label_set_long_mode(d_hint, LV_LABEL_LONG_WRAP);
    detail_render();
}

static void detail_leave(void)
{
    usbhid_cancel();
    // aspetta che il task di digitazione esca prima di spegnere la USB
    for (int i = 0; d_task && i < 200; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (usbhid_supported()) usbhid_end();
}

// La digitazione gira in un task a parte: può durare secondi (testi lunghi) e non deve
// bloccare l'interfaccia né far scattare il watchdog del task di LVGL.
static void type_task(void *arg)
{
    d_typed = usbhid_type(cur.text, cur.len, g_set.kb_layout);
    if (d_typed > 0) clips_mark_used(cur.id);
    d_typing_done = true;
    d_task = NULL;
    vTaskDelete(NULL);
}

static void do_type(void)
{
    if (d_task) return;
    d_mode = D_TYPING;
    d_typed = 0;
    d_typing_done = false;
    usbhid_arm();   // qui e non nel task: un "ferma" dato prima che parta non va perso
    detail_render();
    xTaskCreatePinnedToCore(type_task, "clip_type", 4096, NULL, 4, &d_task, 0);
}

static bool detail_nav(nav_t ev)
{
    if (ev == NAV_BTN) {   // BOOT = cancella questa voce, con conferma (non mentre digita)
        if (d_mode == D_TYPING) return true;
        if (d_mode != D_CONFIRM_DEL) {
            d_mode = D_CONFIRM_DEL;
            d_confirm_until = lv_tick_get() + 5000;
            detail_render();
            return true;
        }
        clips_delete_id(cur.id);   // per id: la lista può essersi spostata nel frattempo
        ui_pop();
        return true;
    }
    if (d_mode == D_CONFIRM_DEL) {   // qualsiasi altro gesto annulla la cancellazione
        d_mode = D_IDLE;
        detail_render();
        if (ev != NAV_BACK) return true;
    }
    if (ev == NAV_SELECT) {
        if (!usbhid_supported()) { ui_toast("Serve il supporto USB"); return true; }
        if (d_mode == D_TYPING) return true;
        if (d_mode == D_IDLE || d_mode == D_DONE) { d_mode = D_CONFIRM; d_confirm_until = lv_tick_get() + 5000; detail_render(); return true; }
        if (d_mode == D_CONFIRM) { do_type(); return true; }
    }
    if (ev == NAV_BACK && d_mode == D_TYPING) { usbhid_cancel(); return true; }
    return false;
}

static void detail_tick(void)
{
    if (d_mode == D_TYPING && d_typing_done) { d_mode = D_DONE; detail_render(); }
    else if ((d_mode == D_CONFIRM || d_mode == D_CONFIRM_DEL) && (int32_t)(lv_tick_get() - d_confirm_until) > 0) { d_mode = D_IDLE; detail_render(); }
    else if (d_mode == D_IDLE) detail_render();   // aggiorna se la USB viene collegata ora
}

static const app_t app_clip_detail = {
    .name = "Codice", .enter = detail_enter, .leave = detail_leave, .nav = detail_nav, .tick = detail_tick,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};

static void detail_open(void) { ui_push(&app_clip_detail, NULL); }

/* ================= Ricevi dal PC (Bluetooth) ================= */

static lv_obj_t *r_state, *r_info;

static void listen_render(void)
{
    char t[120];
    ble_conn_t cs[BLE_MAX_CONN];
    int nc = ble_mgr_conns(cs, BLE_MAX_CONN), periph = 0;
    for (int i = 0; i < nc; i++) if (!cs[i].central) periph++;
    int left = ble_mgr_pair_left();
    if (periph) {
        snprintf(t, sizeof(t), LV_SYMBOL_OK " PC collegato · %d in memoria", clips_count());
        ui_set_text_color(r_state, C_OK);
    } else if (left) {
        snprintf(t, sizeof(t), "In attesa del PC · %d:%02d", left / 60, left % 60);
        ui_set_text_color(r_state, ui_accent());
    } else {
        snprintf(t, sizeof(t), "Finestra chiusa");
        ui_set_text_color(r_state, C_DIM);
    }
    ui_set_text(r_state, t);
}

static void listen_enter(lv_obj_t *root, void *arg)
{
    r_state = lv_label_create(root);
    lv_obj_set_style_text_font(r_state, &font_l, 0);
    lv_obj_set_pos(r_state, 24, 12);
    lv_obj_set_width(r_state, SCR_W - 48);
    lv_label_set_long_mode(r_state, LV_LABEL_LONG_DOT);

    r_info = lv_label_create(root);
    lv_obj_set_style_text_font(r_info, &font_s, 0);
    lv_obj_set_style_text_color(r_info, C_DIM, 0);
    lv_obj_set_pos(r_info, 24, 58);
    lv_obj_set_width(r_info, SCR_W - 48);
    lv_label_set_long_mode(r_info, LV_LABEL_LONG_WRAP);
    char info[200];
    snprintf(info, sizeof(info), "Dal PC apri la pagina Appunti (Chrome o Edge), premi \"Collega il Gadget\" e "
             "scegli %s. Poi incolla i testi: compaiono qui. Swipe a destra per riaprire la finestra.",
             ble_mgr_name());
    lv_label_set_text(r_info, info);

    ble_mgr_pair_start(180);   // connettibile via Bluetooth per 3 minuti
    listen_render();
}

static void listen_leave(void) { ble_mgr_pair_stop(); }

static bool listen_nav(nav_t ev)
{
    if (ev == NAV_SELECT) { ble_mgr_pair_start(180); listen_render(); return true; }
    return false;
}

static const app_t app_clip_listen = {
    .name = "Ricevi dal PC", .icon = LV_SYMBOL_BLUETOOTH,
    .enter = listen_enter, .leave = listen_leave, .nav = listen_nav, .tick = listen_render,
    .flags = APP_NO_SLEEP,
};

/* ================= Scarica la pagina (Wi-Fi) ================= */

static lv_obj_t *w_status;

static void wpage_tick(void)
{
    bool pc = wifi_mgr_portal_clients() > 0;
    // solo se cambia: ogni modifica ridisegna l'intero schermo
    ui_set_text(w_status, pc ? "PC collegato: apri 192.168.4.1 e salva la pagina" : "In attesa del PC…");
    ui_set_text_color(w_status, pc ? ui_accent() : C_DIM);
}

static void wpage_enter(lv_obj_t *root, void *arg)
{
    wifi_mgr_portal_start_clips();
    lv_obj_t *s1 = lv_label_create(root);
    lv_obj_set_style_text_font(s1, &font_m, 0);
    lv_obj_set_pos(s1, 24, 12);
    lv_label_set_text(s1, "1  Dal PC collegati alla rete Wi-Fi");
    lv_obj_t *v1 = lv_label_create(root);
    lv_obj_set_style_text_font(v1, &font_m, 0);
    lv_obj_set_style_text_color(v1, ui_accent(), 0);
    lv_label_set_text(v1, wifi_mgr_portal_ssid());
    lv_obj_align_to(v1, s1, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_t *s2 = lv_label_create(root);
    lv_obj_set_style_text_font(s2, &font_m, 0);
    lv_obj_set_pos(s2, 24, 48);
    lv_obj_set_width(s2, SCR_W - 48);
    lv_label_set_long_mode(s2, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s2, "2  Si apre la pagina (o vai su 192.168.4.1). Premi \"Scarica\" per tenerla sul PC, poi usala.");
    w_status = lv_label_create(root);
    lv_obj_set_style_text_font(w_status, &font_m, 0);
    lv_obj_set_pos(w_status, 24, 118);
    lv_obj_set_width(w_status, SCR_W - 48);
    lv_label_set_long_mode(w_status, LV_LABEL_LONG_DOT);
    wpage_tick();
}

// alla chiusura l'hotspot si spegne e il Wi-Fi torna com'era
static void wpage_leave(void) { wifi_mgr_portal_stop(); }

static const app_t app_clip_portal = {
    .name = "Scarica la pagina", .icon = ICON_MOBILE,
    .enter = wpage_enter, .leave = wpage_leave, .tick = wpage_tick, .flags = APP_NO_SLEEP,
};

/* ================= menu Appunti ================= */

static void v_count(char *b, int n) { snprintf(b, n, "%d salvat%s", clips_count(), clips_count() == 1 ? "o" : "i"); }
static void v_layout(char *b, int n) { snprintf(b, n, "%s", settings_layout_name(g_set.kb_layout)); }
static void j_layout(int d)
{
    g_set.kb_layout = (g_set.kb_layout + d + KB_LAYOUT_COUNT) % KB_LAYOUT_COUNT;
    settings_save();
}
static void a_clear(void) { clips_clear(); ui_toast("Appunti svuotati"); }
static void v_usb(char *b, int n)
{
    if (!usbhid_supported()) snprintf(b, n, "Non in questo firmware");
    else snprintf(b, n, "%s", usbhid_mounted() ? "PC collegato" : "Collega la USB-C");
}

static const menu_item_t clip_items[] = {
    {.icon = LV_SYMBOL_BLUETOOTH, .label = "Ricevi dal PC", .hint = "Collega il browser via Bluetooth", .app = &app_clip_listen},
    {.icon = ICON_MOBILE, .label = "Scarica la pagina", .hint = "Hotspot Wi-Fi con la pagina da salvare", .app = &app_clip_portal},
    {.icon = LV_SYMBOL_COPY, .label = "Codici salvati", .value = v_count, .app = &app_clip_list},
    {.icon = LV_SYMBOL_KEYBOARD, .label = "Tastiera USB", .value = v_usb, .hint = NULL},
    {.icon = ICON_SLIDERS, .label = "Layout tastiera", .value = v_layout, .on_adjust = j_layout},
    {.icon = LV_SYMBOL_TRASH, .label = "Cancella tutti", .on_select = a_clear, .confirm = true},
};
menu_t clips_menu = {"Appunti", clip_items, sizeof(clip_items) / sizeof(clip_items[0]), 0};
