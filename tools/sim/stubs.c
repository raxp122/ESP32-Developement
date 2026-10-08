// sostituti per il simulatore: niente hardware, dati finti
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "ui.h"
#include "board.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "audio.h"
#include "ble_mgr.h"
#include "ota.h"
#include "radio.h"
#include "textnorm.h"
#include "apps.h"

int sim_round = 1;
int16_t g_scr_w = 466, g_scr_h = 466;
settings_t g_set;
nav_handler_t sim_nav;
wifi_state_t sim_wifi = WIFI_CONNECTED;
ota_state_t sim_ota = OTA_AVAILABLE;

board_kind_t board_kind(void) { return sim_round ? BOARD_AMOLED175 : BOARD_LCD349; }
const char *board_name(void) { return "sim"; }
void board_power_off(void) {}
float board_battery_volts(void) { return 3.9f; }
int board_battery_percent(float v) { return 76; }
bool board_charging(void) { return sim_round; }
rtc_status_t board_rtc_get(struct tm *t) { time_t n = time(NULL); gmtime_r(&n, t); return RTC_OK; }
rtc_status_t board_rtc_boot_status(void) { return RTC_OK; }
void display_set_brightness(int p) {}
void display_set_flipped(bool f) {}
long long esp_timer_get_time(void) { return 0; }
const char *esp_get_idf_version(void) { return "v5.4.2"; }
void esp_restart(void) {}
uint32_t input_idle_ms(void) { return 0; }
void input_init(nav_handler_t h) { sim_nav = h; }
void input_mark_activity(void) {}
bool input_touch(int *x, int *y) { return false; }
bool board_btn_boot(void) { return false; }
bool wifi_mgr_espnow_start(int ch, wifi_espnow_cb_t cb) { return true; }
void wifi_mgr_espnow_stop(void) {}
bool wifi_mgr_espnow_send(const void *d, int n) { return true; }
void ble_mgr_addr(char *b, int n) { snprintf(b, n, "AA:BB:CC:DD:EE:FF"); }
void ble_mgr_apply(void) {}
int ble_mgr_bonds(ble_bond_t *o, int m) { return 0; }
int ble_mgr_conns(ble_conn_t *o, int m) { return 0; }
void ble_mgr_forget_all(void) {}
const char *ble_mgr_name(void) { return "Gadget"; }
bool ble_mgr_on(void) { return false; }
void ble_mgr_set_battery(int p) {}
void ble_mgr_suspend(bool on) {}
void ota_auto_start(void) {}
void ota_check(void) {}
const char *ota_current(void) { return "0.14.69"; }
const char *ota_error(void) { return ""; }
void ota_install(void) {}
const char *ota_latest(void) { return "0.15.0"; }
int ota_progress(void) { return 42; }
ota_state_t ota_state(void) { return sim_ota; }
void radio_only_ble(void) {}
void radio_only_wifi(void) {}
void settings_defer(bool on) {}
const char *settings_quick_name(int q) { return "Torcia"; }
void settings_reset(void) {}
void settings_save(void) {}
void text_norm(const char *in, char *out, size_t n) { snprintf(out, n, "%s", in); }
void wifi_mgr_apply(void) {}
const char *wifi_mgr_auth_name(int a) { return a ? "WPA2" : "aperta"; }
const char *wifi_mgr_band(int c) { return c > 14 ? "5" : "2.4"; }
bool wifi_mgr_conn_failed(void) { return false; }
int wifi_mgr_conn_reason(void) { return 0; }
bool wifi_mgr_connect(const char *s, const char *p) { return true; }
void wifi_mgr_forget(void) {}
const char *wifi_mgr_ip(void) { return "192.168.1.42"; }
int wifi_mgr_portal_clients(void) { return 0; }
void wifi_mgr_portal_poll(void) {}
bool wifi_mgr_portal_saved(void) { return false; }
const char *wifi_mgr_portal_ssid(void) { return "Gadget-Setup"; }
void wifi_mgr_portal_start(void) {}
void wifi_mgr_portal_stop(void) {}
void wifi_mgr_portal_start_file(const char *p, const char *n, const char *t) {}
int wifi_mgr_rssi(void) { return -55; }
void wifi_mgr_scan_acquire(void) {}
bool wifi_mgr_scan_busy(void) { return false; }
// la scena può dare i suoi risultati (sim_ap_n >= 0) e far avanzare le scansioni
wifi_ap_t sim_aps[32];
int sim_ap_n = -1;
uint32_t sim_scan_gen = 1;
uint32_t wifi_mgr_scan_gen(void) { return sim_scan_gen; }
void wifi_mgr_scan_release(void) {}
int wifi_mgr_scan_results(wifi_ap_t *o, int max)
{
    static const wifi_ap_t a[] = {
        {"Casa", -48, 6, 3}, {"Casa_5G", -61, 36, 3}, {"Vodafone-A1B2C3", -70, 11, 3}, {"Ospiti", -77, 1, 0},
    };
    if (sim_ap_n >= 0) {
        int n = sim_ap_n < max ? sim_ap_n : max;
        memcpy(o, sim_aps, n * sizeof(wifi_ap_t));
        return n;
    }
    int n = sizeof(a) / sizeof(a[0]);
    memcpy(o, a, sizeof(a));
    return n;
}
bool wifi_mgr_scan_start(void) { return true; }
bool wifi_mgr_scan_start_ex(const char *ssid, uint16_t m) { return true; }
bool audio_init(void) { return false; }
void audio_start(audio_synth_t s) {}
void audio_stop_if(audio_synth_t s) {}
audio_synth_t audio_current(void) { return NULL; }
bool audio_mic_active(void) { return false; }
bool audio_mic_ok(void) { return true; }
const char *audio_status(void) { return "Pronto"; }
void audio_set_volume(int p) {}
wifi_state_t wifi_mgr_state(void) { return sim_wifi; }
void wifi_mgr_sync_time(void) {}
bool wifi_mgr_time_saved(void) { return true; }
bool wifi_mgr_time_synced(void) { return true; }

#define DUMMY_APP(n) static void n##_e(lv_obj_t *r, void *a) {} const app_t n = {.name = #n, .enter = n##_e};
DUMMY_APP(app_8ball) DUMMY_APP(app_bercio) DUMMY_APP(app_ble_conns) DUMMY_APP(app_ble_pair) DUMMY_APP(app_blescan)
DUMMY_APP(app_level) DUMMY_APP(app_pet) DUMMY_APP(app_q20) DUMMY_APP(app_search)
DUMMY_APP(app_theremin) DUMMY_APP(app_torch)
static const menu_item_t none[] = {{.label = "x"}};
menu_t backup_menu = {"Backup", none, 1, 0, NULL}, chess_menu = {"Scacchi", none, 1, 0, NULL},
       clips_menu = {"Appunti", none, 1, 0, NULL}, dice_menu = {"Dadi", none, 1, 0, NULL},
       doom_menu = {"Doom", none, 1, 0, NULL}, pet_settings_menu = {"Pet", none, 1, 0, NULL},
       radar_menu = {"Radar", none, 1, 0, NULL}, saber_menu = {"Spada", none, 1, 0, NULL},
       tuner_menu = {"Accordatore", none, 1, 0, NULL};
void chess_menu_init(void) {}
bool sd_ok(void) { return true; }
void tuner_menu_init(void) {}
