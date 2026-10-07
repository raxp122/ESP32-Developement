// app_settings.c — menu Impostazioni e sottomenu
#include "apps.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include "radio.h"
#include "board.h"
#include <stdio.h>
#include <time.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "ota.h"
#include "audio.h"
#include <math.h>


static const char *onoff(bool v) { return v ? "Acceso" : "Spento"; }

/* ---------------- Wi-Fi ---------------- */

static void v_wifi_toggle(char *b, int n) { snprintf(b, n, "%s", onoff(g_set.wifi_on)); }
static void a_wifi_toggle(void)
{
    if (!g_set.wifi_on) radio_only_wifi();   // uno dei due alla volta
    g_set.wifi_on = !g_set.wifi_on;
    settings_save();
    wifi_mgr_apply();
    if (g_set.wifi_on) ota_auto_start();   // se all'avvio era spento, il controllo non era partito
}

static void v_wifi_net(char *b, int n)
{
    snprintf(b, n, "%s", g_set.wifi_ssid[0] ? g_set.wifi_ssid : "Nessuna rete salvata");
}

static void v_wifi_state(char *b, int n)
{
    switch (wifi_mgr_state()) {
    case WIFI_CONNECTED:  snprintf(b, n, "Connesso · %s", wifi_mgr_ip()); break;
    case WIFI_CONNECTING: snprintf(b, n, "Connessione a %s…", g_set.wifi_ssid); break;
    case WIFI_NO_NETWORK: snprintf(b, n, "Nessuna rete configurata"); break;
    default:              snprintf(b, n, "Wi-Fi spento"); break;
    }
}

static void v_wifi_rssi(char *b, int n)
{
    int r = wifi_mgr_rssi();
    if (r) snprintf(b, n, "%d dBm", r);
    else snprintf(b, n, "—");
}

static void a_wifi_forget(void) { wifi_mgr_forget(); ui_toast("Rete dimenticata"); }

static const menu_item_t wifi_items[] = {
    {.icon = LV_SYMBOL_WIFI, .label = "Wi-Fi", .value = v_wifi_toggle, .on_select = a_wifi_toggle},
    {.icon = ICON_SEARCH, .label = "Cerca reti e collegati", .value = v_wifi_net, .app = &app_wifiscan, .arg = "Wi-Fi » Reti"},
    {.icon = ICON_MOBILE, .label = "Configura dal telefono", .hint = "Hotspot con pagina web", .app = &app_portal},
    {.icon = ICON_INFO, .label = "Stato", .value = v_wifi_state},
    {.icon = ICON_TOWER, .label = "Segnale", .value = v_wifi_rssi},
    {.icon = LV_SYMBOL_TRASH, .label = "Dimentica rete", .on_select = a_wifi_forget, .confirm = true},
};
static menu_t wifi_menu = {"Impostazioni » Wi-Fi", wifi_items, sizeof(wifi_items) / sizeof(wifi_items[0]), 0, NULL};

/* ---------------- Bluetooth ---------------- */

static void v_ble(char *b, int n) { snprintf(b, n, "%s", onoff(g_set.ble_on)); }
static void a_ble(void)
{
    if (!g_set.ble_on) radio_only_ble();   // uno dei due alla volta
    g_set.ble_on = !g_set.ble_on;
    settings_save();
    ble_mgr_apply();
}
static void v_ble_vis(char *b, int n) { snprintf(b, n, "%s", g_set.ble_visible ? "Sì" : "No"); }
static void a_ble_vis(void)
{
    g_set.ble_visible = !g_set.ble_visible;
    settings_save();
    ble_mgr_apply();
    if (g_set.ble_visible && !g_set.ble_on) ui_toast("Accendi il Bluetooth per essere visibile");
}
static void v_ble_name(char *b, int n) { snprintf(b, n, "%s", ble_mgr_name()); }
static void v_ble_addr(char *b, int n) { ble_mgr_addr(b, n); }

static void v_ble_conns(char *b, int n)
{
    ble_conn_t c[BLE_MAX_CONN];
    ble_bond_t bd[8];
    int nc = ble_mgr_conns(c, BLE_MAX_CONN), nb = ble_mgr_bonds(bd, 8);
    if (nc) snprintf(b, n, "%d collegat%s · %d associat%s", nc, nc == 1 ? "o" : "i", nb, nb == 1 ? "o" : "i");
    else if (nb) snprintf(b, n, "%d associat%s", nb, nb == 1 ? "o" : "i");
    else snprintf(b, n, "Nessuno");
}
static void a_ble_forget_all(void) { ble_mgr_forget_all(); ui_toast("Associazioni cancellate"); }

static const menu_item_t ble_items[] = {
    {.icon = LV_SYMBOL_BLUETOOTH, .label = "Bluetooth", .value = v_ble, .on_select = a_ble},
    {.icon = ICON_MOBILE, .label = "Collega a telefono o computer", .hint = "Il Gadget si fa trovare per 2 minuti", .app = &app_ble_pair},
    {.icon = ICON_SEARCH, .label = "Collega un dispositivo", .hint = "Cerca sensori, tastiere, telecomandi…", .app = &app_blescan, .arg = "Bluetooth » Cerca"},
    {.icon = LV_SYMBOL_LIST, .label = "Dispositivi", .value = v_ble_conns, .app = &app_ble_conns},
    {.icon = ICON_EYE, .label = "Visibile agli altri", .value = v_ble_vis, .on_select = a_ble_vis},
    {.icon = LV_SYMBOL_EDIT, .label = "Nome", .value = v_ble_name},
    {.icon = ICON_CHIP, .label = "Indirizzo", .value = v_ble_addr},
    {.icon = LV_SYMBOL_TRASH, .label = "Dimentica tutti i dispositivi", .on_select = a_ble_forget_all, .confirm = true},
};
static menu_t ble_menu = {"Impostazioni » Bluetooth", ble_items, sizeof(ble_items) / sizeof(ble_items[0]), 0, NULL};

/* ---------------- Schermo ---------------- */

static void v_bright(char *b, int n) { snprintf(b, n, "%d%%", g_set.brightness); }
static void j_bright(int d)
{
    int v = g_set.brightness + d * 10;
    if (v < 5) v = 5;
    if (v > 100) v = 100;
    if (g_set.brightness == 5 && d > 0) v = 10;
    g_set.brightness = v;
    display_set_brightness(v);
    settings_save();
}

static const uint16_t sleeps[] = {15, 30, 60, 120, 300, 0};
#define NSLEEP (int)(sizeof(sleeps) / sizeof(sleeps[0]))
static void v_sleep(char *b, int n)
{
    if (!g_set.sleep_s) snprintf(b, n, "Mai");
    else if (g_set.sleep_s < 60) snprintf(b, n, "%d secondi", g_set.sleep_s);
    else snprintf(b, n, "%d minut%s", g_set.sleep_s / 60, g_set.sleep_s == 60 ? "o" : "i");
}
static void j_sleep(int d)
{
    int i = 0;
    while (i < NSLEEP && sleeps[i] != g_set.sleep_s) i++;
    if (i == NSLEEP) i = 2;
    i += d;
    if (i < 0) i = 0;
    if (i >= NSLEEP) i = NSLEEP - 1;
    g_set.sleep_s = sleeps[i];
    settings_save();
}

static void v_flip(char *b, int n) { snprintf(b, n, "%s", g_set.flipped ? "Sì" : "No"); }
static void a_flip(void)
{
    g_set.flipped = !g_set.flipped;
    display_set_flipped(g_set.flipped);
    settings_save();
}

static void v_invert(char *b, int n) { snprintf(b, n, "%s", g_set.invert_scroll ? "Sì" : "No"); }
static void a_invert(void) { g_set.invert_scroll = !g_set.invert_scroll; settings_save(); }

static void v_accent(char *b, int n) { snprintf(b, n, "%s", ui_accent_name(g_set.accent)); }
static void j_accent(int d)
{
    int c = ui_accent_count();
    g_set.accent = (g_set.accent + d + c) % c;
    settings_save();
}

static const menu_item_t screen_items[] = {
    {.icon = ICON_SUN, .label = "Luminosità", .value = v_bright, .on_adjust = j_bright},
    {.icon = ICON_MOON, .label = "Spegnimento automatico", .value = v_sleep, .on_adjust = j_sleep},
    {.icon = LV_SYMBOL_REFRESH, .label = "Ruota di 180°", .value = v_flip, .on_select = a_flip},
    {.icon = ICON_SLIDERS, .label = "Inverti scorrimento", .value = v_invert, .on_select = a_invert},
    {.icon = ICON_PALETTE, .label = "Colore", .value = v_accent, .on_adjust = j_accent},
};
static menu_t screen_menu = {"Impostazioni » Schermo", screen_items, sizeof(screen_items) / sizeof(screen_items[0]), 0, NULL};

/* ---------------- Data e ora ---------------- */

static void v_time(char *b, int n)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) snprintf(b, n, "Non impostata");
    else snprintf(b, n, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
}
static void v_date(char *b, int n)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 124) snprintf(b, n, "—");
    else snprintf(b, n, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
}
static void v_tsrc(char *b, int n)
{
    if (wifi_mgr_time_synced())
        snprintf(b, n, "%s", wifi_mgr_time_saved() ? "Internet (NTP), salvata nell'RTC" : "Internet (NTP), ma l'RTC non l'ha presa");
    else if (board_rtc_boot_status() == RTC_OK) snprintf(b, n, "Orologio della scheda (RTC)");
    else snprintf(b, n, "Nessuna: serve il Wi-Fi");
}
// L'RTC (PCF85063) ha l'ora in UTC; qui si vede in ora locale, letta adesso dal chip, e
// com'era all'accensione: se si è fermato è rimasto senza corrente mentre la scheda era spenta.
static void v_rtc(char *b, int n)
{
    static const char *const boot[] = {"all'accensione aveva l'ora", "non risponde",
                                       "all'accensione era fermo (senza corrente da spento)",
                                       "all'accensione non aveva un'ora"};
    struct tm u;
    rtc_status_t s = board_rtc_get(&u);
    if (s != RTC_OK) { snprintf(b, n, "%s · %s", s == RTC_NO_CHIP ? "Non risponde" : "Senza ora", boot[board_rtc_boot_status()]); return; }
    // UTC → locale, senza toccare il fuso (lo usano anche gli altri task): giorni dal 1970
    int y = u.tm_year + 1900, m = u.tm_mon + 1;
    y -= m <= 2;
    int era = y / 400, yoe = y - era * 400, mp = (m + 9) % 12;
    long days = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + (153 * mp + 2) / 5 + u.tm_mday - 1 - 719468;
    time_t t = (time_t)days * 86400 + u.tm_hour * 3600 + u.tm_min * 60 + u.tm_sec;
    struct tm l;
    localtime_r(&t, &l);
    snprintf(b, n, "%02d:%02d:%02d · %s", l.tm_hour, l.tm_min, l.tm_sec, boot[board_rtc_boot_status()]);
}
static void a_sync(void)
{
    if (wifi_mgr_state() != WIFI_CONNECTED) { ui_toast("Serve il Wi-Fi collegato"); return; }
    wifi_mgr_sync_time();
    ui_toast("Sincronizzazione avviata");
}
static void v_tz(char *b, int n) { snprintf(b, n, "Europa/Roma"); }

static const menu_item_t time_items[] = {
    {.icon = ICON_CLOCK, .label = "Ora", .value = v_time},
    {.icon = LV_SYMBOL_LIST, .label = "Data", .value = v_date},
    {.icon = ICON_SYNC, .label = "Sincronizza ora", .value = v_tsrc, .on_select = a_sync},
    {.icon = ICON_CHIP, .label = "Orologio della scheda", .value = v_rtc},
    {.icon = LV_SYMBOL_GPS, .label = "Fuso orario", .value = v_tz},
};
static menu_t time_menu = {"Impostazioni » Data e ora", time_items, sizeof(time_items) / sizeof(time_items[0]), 0, NULL};

/* ---------------- Sistema ---------------- */

static void v_batt(char *b, int n)
{
    float v = board_battery_volts();
    if (v > 2.5f) snprintf(b, n, "%.2f V · %d%%", v, board_battery_percent(v));
    else snprintf(b, n, "Nessuna batteria rilevata");
}
static void v_mem(char *b, int n)
{
    snprintf(b, n, "RAM %u KB · PSRAM %.1f MB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0);
}
static void v_fw(char *b, int n) { snprintf(b, n, "Gadget %s · ESP-IDF %s", ota_current(), esp_get_idf_version()); }
static void v_ota(char *b, int n)
{
    switch (ota_state()) {
    case OTA_AVAILABLE:   snprintf(b, n, "Disponibile la %s", ota_latest()); break;
    case OTA_UP_TO_DATE:  snprintf(b, n, "Aggiornato (%s)", ota_current()); break;
    case OTA_DOWNLOADING: snprintf(b, n, "Download %d%%", ota_progress()); break;
    default:              snprintf(b, n, "Controlla su internet"); break;
    }
}
static void v_ota_auto(char *b, int n) { snprintf(b, n, "%s", g_set.ota_auto ? "Sì, quando c'è internet" : "No"); }
static void a_ota_auto(void) { g_set.ota_auto = !g_set.ota_auto; settings_save(); }
static void v_up(char *b, int n)
{
    int s = (int)(esp_timer_get_time() / 1000000);
    snprintf(b, n, "%dh %02dm %02ds", s / 3600, (s / 60) % 60, s % 60);
}
static void a_restart(void) { esp_restart(); }
static void a_off(void) { ui_power_off(); }
static void a_reset(void)
{
    settings_reset();
    display_set_flipped(g_set.flipped);
    display_set_brightness(g_set.brightness);
    wifi_mgr_apply();
    ble_mgr_apply();
    ui_toast("Impostazioni ripristinate");
}

static const menu_item_t sys_items[] = {
    {.icon = LV_SYMBOL_BATTERY_FULL, .label = "Batteria", .value = v_batt},
    {.icon = ICON_CHIP, .label = "Memoria libera", .value = v_mem},
    {.icon = ICON_INFO, .label = "Firmware", .value = v_fw},
    {.icon = LV_SYMBOL_DOWNLOAD, .label = "Aggiornamento firmware", .value = v_ota, .app = &app_ota},
    {.icon = ICON_SYNC, .label = "Cerca aggiornamenti da solo", .value = v_ota_auto, .on_select = a_ota_auto},
    {.icon = ICON_CLOCK, .label = "Acceso da", .value = v_up},
    {.icon = LV_SYMBOL_REFRESH, .label = "Riavvia", .on_select = a_restart, .confirm = true},
    {.icon = LV_SYMBOL_POWER, .label = "Spegni", .on_select = a_off, .confirm = true},
    {.icon = LV_SYMBOL_WARNING, .label = "Ripristina impostazioni", .on_select = a_reset, .confirm = true},
};
static menu_t sys_menu = {"Impostazioni » Sistema", sys_items, sizeof(sys_items) / sizeof(sys_items[0]), 0, NULL};

/* ---------------- Audio ---------------- */

// prova: tre note che salgono (do-mi-sol, 1,2 s), poi l'uscita si libera da sola
static volatile uint32_t t_n;
static float t_ph;
static void test_synth(int16_t *b, int n)
{
    static const float NOTE[3] = {523.25f, 659.25f, 783.99f};
    const uint32_t len = AUDIO_RATE * 4 / 10;
    for (int i = 0; i < n; i++) {
        uint32_t t = t_n;
        if (t >= 3 * len) { b[i] = 0; continue; }
        t_ph += NOTE[t / len] / AUDIO_RATE;
        if (t_ph >= 1) t_ph -= 1;
        float env = (t % len) < 240 ? (t % len) / 240.0f : (len - t % len) < 1200 ? (len - t % len) / 1200.0f : 1;
        b[i] = (int16_t)(sinf(6.2831853f * t_ph) * env * 12000);
        t_n = t + 1;
    }
    if (t_n >= 3 * len) audio_stop_if(test_synth);
}

static void v_audio(char *b, int n)
{
    audio_synth_t cur = audio_current();
    snprintf(b, n, "%s%s%s", audio_status(),
             audio_status()[0] == 'P' ? (audio_mic_ok() ? " · microfoni ok" : " · microfoni assenti") : "",
             cur && cur != test_synth ? " · uscita occupata da un'app" : audio_mic_active() ? " · microfoni in ascolto" : "");
}
static void a_audio_test(void)
{
    if (!audio_init()) { ui_toast(audio_status()); return; }
    if (g_set.volume == 0) ui_toast("Il volume è a 0%");
    audio_set_volume(g_set.volume);
    t_n = 0;
    t_ph = 0;
    audio_start(test_synth);
}
static void v_volume(char *b, int n) { snprintf(b, n, "%d%%", g_set.volume); }
static void j_volume(int d)
{
    int v = g_set.volume + d * 10;
    g_set.volume = v < 0 ? 0 : v > 100 ? 100 : v;
    audio_set_volume(g_set.volume);
    settings_save();
}

static const menu_item_t audio_items[] = {
    {.icon = LV_SYMBOL_PLAY, .label = "Prova audio", .value = v_audio, .on_select = a_audio_test},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Volume", .value = v_volume, .on_adjust = j_volume},
};
static menu_t audio_menu = {"Impostazioni » Audio", audio_items, sizeof(audio_items) / sizeof(audio_items[0]), 0, NULL};

/* ---------------- Impostazioni ---------------- */

static void v_wifi_sum(char *b, int n)
{
    switch (wifi_mgr_state()) {
    case WIFI_CONNECTED: snprintf(b, n, "%s", g_set.wifi_ssid); break;
    case WIFI_CONNECTING: snprintf(b, n, "Connessione…"); break;
    case WIFI_NO_NETWORK: snprintf(b, n, "Da configurare"); break;
    default: snprintf(b, n, "Spento");
    }
}
static void v_quick(char *b, int n) { snprintf(b, n, "Tieni premuto: %s", settings_quick_name(g_set.quick_action)); }
static void j_quick(int d)
{
    g_set.quick_action = (g_set.quick_action + d + QUICK_COUNT) % QUICK_COUNT;
    settings_save();
}

static void v_boot(char *b, int n) { snprintf(b, n, "%s", boot_app_name(g_set.boot_app)); }
static void j_boot(int d)
{
    int c = boot_app_count();
    g_set.boot_app = (g_set.boot_app + d + c) % c;
    settings_save();
}

static const menu_item_t settings_items[] = {
    {.icon = LV_SYMBOL_WIFI, .label = "Wi-Fi", .value = v_wifi_sum, .app = &app_menu, .arg = &wifi_menu},
    {.icon = LV_SYMBOL_BLUETOOTH, .label = "Bluetooth", .value = v_ble, .app = &app_menu, .arg = &ble_menu},
    {.icon = ICON_SUN, .label = "Schermo", .app = &app_menu, .arg = &screen_menu},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Audio", .value = v_volume, .app = &app_menu, .arg = &audio_menu},
    {.icon = ICON_BOLT, .label = "Azione rapida", .value = v_quick, .on_adjust = j_quick},
    {.icon = LV_SYMBOL_HOME, .label = "App all'avvio", .value = v_boot, .on_adjust = j_boot},
    {.icon = ICON_GAMEPAD, .label = "Polipetto", .app = &app_menu, .arg = &pet_settings_menu},
    {.icon = LV_SYMBOL_SD_CARD, .label = "Backup e ripristino", .app = &app_menu, .arg = &backup_menu},
    {.icon = ICON_CLOCK, .label = "Data e ora", .value = v_time, .app = &app_menu, .arg = &time_menu},
    {.icon = ICON_CHIP, .label = "Sistema", .value = v_fw, .app = &app_menu, .arg = &sys_menu},
};
menu_t settings_menu = {"Impostazioni", settings_items, sizeof(settings_items) / sizeof(settings_items[0]), 0, NULL};
