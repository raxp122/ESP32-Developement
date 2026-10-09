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
long long sim_timer_us;   // fermo, tranne nelle scene che lo fanno avanzare (Orologio scacchi)
long long esp_timer_get_time(void) { return sim_timer_us; }
const char *esp_get_idf_version(void) { return "v5.4.2"; }
void esp_restart(void) {}
uint32_t input_idle_ms(void) { return 0; }
void input_init(nav_handler_t h) { sim_nav = h; }
void input_mark_activity(void) {}
int sim_touch, sim_tx = 100, sim_ty = 80;
bool input_touch(int *x, int *y) { if (x) *x = sim_tx; if (y) *y = sim_ty; return sim_touch; }
bool display_is_dark(void) { return false; }
bool board_imu_ok(void) { return true; }
vec3_t sim_acc = {0, 0, 1};
bool board_imu_accel(vec3_t *g) { *g = sim_acc; return true; }
static bool sim_locked;
void input_set_locked(bool l) { sim_locked = l; }
bool input_locked(void) { return sim_locked; }
bool board_btn_boot(void) { return false; }
wifi_espnow_cb_t sim_espnow_cb;
bool wifi_mgr_espnow_start(int ch, wifi_espnow_cb_t cb) { sim_espnow_cb = cb; return true; }
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
const char *ota_error_detail(void) { return ""; }
void ota_install(void) {}
const char *ota_latest(void) { return "0.15.0"; }
int ota_progress(void) { return 42; }
ota_state_t ota_state(void) { return sim_ota; }
void radio_only_ble(void) {}
void radio_only_wifi(void) {}
void settings_defer(bool on) {}
void settings_reset(void) {}
void settings_save(void) {}
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
int sim_audio;   // 1: l'altoparlante risponde (per il Theremin)
bool audio_init(void) { return sim_audio; }
bool audio_mic_active(void) { return false; }
bool audio_mic_ok(void) { return true; }
bool audio_mic_probe(int ms, int *pk, int *rms, int *z) { *pk = 9000; *rms = 800; *z = 0; return true; }
const char *audio_status(void) { return "Pronto"; }
void audio_set_volume(int p) {}
wifi_state_t wifi_mgr_state(void) { return sim_wifi; }
void wifi_mgr_sync_time(void) {}
bool wifi_mgr_time_saved(void) { return true; }
bool wifi_mgr_time_synced(void) { return true; }

#define DUMMY_APP(n) static void n##_e(lv_obj_t *r, void *a) {} const app_t n = {.name = #n, .enter = n##_e};
DUMMY_APP(app_ble_conns) DUMMY_APP(app_ble_pair)
static const menu_item_t none[] = {{.label = "x"}};
menu_t backup_menu = {"Backup", none, 1, 0, NULL},
       doom_menu = {"Doom", none, 1, 0, NULL};
bool sd_ok(void) { return true; }
const char *logcon_reset_reason(void) { return "errore (panic)"; }
bool logcon_has_prev(void) { return true; }
bool logcon_write_report(const char *p) { return true; }
void input_touch_counts(uint32_t c[4]) { c[0] = 812; c[1] = 40210; c[2] = 0; c[3] = 3; }
int board_touch_recoveries(void) { return 1; }
// Radar: motore finto (la scena sceglie reti vicine e handshake)
#include "pwn.h"
int sim_pwn_near = 3, sim_pwn_run = 1;
uint32_t sim_pwn_hs, sim_pwn_nets = 12;
void pwn_start(void) { sim_pwn_run = 1; }
void pwn_stop(void) { sim_pwn_run = 0; }
bool pwn_running(void) { return sim_pwn_run; }
void pwn_get_stats(pwn_stats_t *o) { memset(o, 0, sizeof(*o)); snprintf(o->name, sizeof(o->name), "Gadget"); o->level = 7; o->xp = 3000; o->nets_total = sim_pwn_nets; o->handshakes = sim_pwn_hs; o->pkts = 48213; }
void pwn_set_name(const char *n) {}
int pwn_net_count(void) { return 0; }
bool pwn_net_get(int i, pwn_net_t *o) { return false; }
int pwn_recent_channel(void) { return 6; }
int pwn_aps_near(void) { return sim_pwn_near; }
void pwn_save(void) {}
void pwn_reset_pokedex(void) {}
const char *pwn_last_event(void) { return "guardo in giro"; }

/* app aggiunte per gli screenshot del README: microfono, BLE, USB e giroscopio finti */
#include "usbhid.h"
bool audio_mic_start(void) { return true; }
void audio_mic_stop(void) {}
bool ble_mgr_ready(void) { return true; }
void ble_mgr_scan_acquire(void) {}
void ble_mgr_scan_release(void) {}
bool ble_mgr_scan_start(int ms) { return true; }
bool ble_mgr_scan_busy(void) { return false; }
uint32_t ble_mgr_scan_gen(void) { return 1; }
int ble_mgr_scan_results(ble_dev_t *out, int max)
{
    static const struct { const char *n; int8_t r; uint16_t app, svc; } D[] = {
        {"Cuffie di Marco", -48, 0x0941, 0x110B}, {"Mi Band 8", -61, 0x00C1, 0x180D},
        {"", -70, 0, 0xFE9F}, {"Termometro bagno", -79, 0x0300, 0x181A}, {"", -88, 0, 0}};
    int n = 0;
    for (; n < 5 && n < max; n++) {
        memset(&out[n], 0, sizeof(out[n]));
        for (int k = 0; k < 6; k++) out[n].addr[k] = 0x10 * n + k;
        snprintf(out[n].name, sizeof(out[n].name), "%s", D[n].n);
        out[n].rssi = D[n].r; out[n].appearance = D[n].app; out[n].svc16 = D[n].svc;
        out[n].connectable = n < 3;
    }
    return n;
}
void ble_mgr_pair_start(int s) {}
void ble_mgr_pair_stop(void) {}
int ble_mgr_pair_left(void) { return 0; }
const char *ble_mgr_svc_name(uint16_t u) { return u == 0x180D ? "Battito" : u == 0x110B ? "Audio" : u == 0x181A ? "Ambiente" : NULL; }
const char *ble_mgr_appearance_name(uint16_t a) { return a == 0x0941 ? "Cuffie" : a == 0x00C1 ? "Orologio" : a == 0x0300 ? "Termometro" : NULL; }
void board_imu_gyro_enable(bool on) {}
const char *settings_layout_name(int i) { return i ? "US" : "Italiano"; }
bool usbhid_supported(void) { return true; }
void usbhid_begin(void) {}
void usbhid_end(void) {}
bool usbhid_mounted(void) { return true; }
void usbhid_cancel(void) {}
void usbhid_arm(void) {}
int usbhid_type(const char *t, int l, int lay) { return l; }
void wifi_mgr_portal_start_clips(void) {}
DUMMY_APP(app_ble_device)
#include "clips.h"
static const char *const CLIPS[] = {"Riunione spostata alle 15:30, sala Verdi", "https://github.com/raxp122/ESP32-Developement", "WiFi ospiti: Gadget-2026!"};
int clips_count(void) { return 3; }
bool clips_get(int i, clip_t *o)
{
    if (i < 0 || i >= 3) return false;
    memset(o, 0, sizeof(*o));
    o->id = 3 - i; o->ts = time(NULL) - 600 * (i + 1) * (i + 1); o->used = i > 0;
    snprintf(o->text, sizeof(o->text), "%s", CLIPS[i]);
    o->len = strlen(o->text);
    return true;
}
bool clips_delete_id(uint32_t id) { return true; }
void clips_clear(void) {}
uint32_t clips_gen(void) { return 1; }

/* task veri (pthread) per le scene che li chiedono, e un microfono che "sente" un tono */
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include "freertos/task.h"
int sim_tasks;
float sim_mic_hz, sim_mic_amp;   // tono che arriva ai microfoni (0 = silenzio)
int xTaskCreatePinnedToCore(void (*f)(void *), const char *n, uint32_t s, void *a, int p, TaskHandle_t *h, int c)
{
    if (!sim_tasks) return 1;
    pthread_t t;
    if (pthread_create(&t, NULL, (void *(*)(void *))f, a)) return 0;
    pthread_detach(t);
    if (h) *h = (TaskHandle_t)t;
    return 1;
}
void vTaskDelay(TickType_t t) { usleep(t * 1000); }
void vTaskDelete(TaskHandle_t h) { if (!h) pthread_exit(NULL); }
int audio_mic_read(int16_t *st, int frames, int timeout_ms)
{
    static double ph;
    usleep(frames * 1000000 / AUDIO_RATE);
    for (int i = 0; i < frames; i++) {
        double v = sim_mic_amp * (sin(ph) + 0.3 * sin(2 * ph)) + (rand() % 201 - 100) * 0.5;
        ph += 2 * M_PI * sim_mic_hz / AUDIO_RATE;
        st[2 * i] = st[2 * i + 1] = (int16_t)v;
    }
    return frames;
}
vec3_t sim_gyro;
bool board_imu_read6(vec3_t *g, vec3_t *w) { board_imu_accel(g); *w = sim_gyro; return true; }
void clips_mark_used(uint32_t id) {}

/* uscita audio: con sim_audio il sintetizzatore gira davvero (in un thread), senza suono */
static audio_synth_t volatile cur_synth;
static void *synth_thread(void *a)
{
    int16_t buf[240];
    while (cur_synth == (audio_synth_t)a) { ((audio_synth_t)a)(buf, 240); usleep(10000); }
    return NULL;
}
void audio_start(audio_synth_t s)
{
    cur_synth = s;
    pthread_t t;
    if (sim_tasks && !pthread_create(&t, NULL, synth_thread, (void *)s)) pthread_detach(t);
}
void audio_stop(void) { cur_synth = NULL; }
void audio_stop_if(audio_synth_t s) { if (cur_synth == s) cur_synth = NULL; }
audio_synth_t audio_current(void) { return cur_synth; }
