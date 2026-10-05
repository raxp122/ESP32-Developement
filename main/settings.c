// settings.c
#include "settings.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define SETTINGS_VERSION 10
settings_t g_set;

static void defaults(void)
{
    memset(&g_set, 0, sizeof(g_set));
    g_set.version = SETTINGS_VERSION;
    g_set.brightness = 70;
    g_set.sleep_s = 60;
    g_set.accent = 0;
    g_set.flipped = true;           // tasti fisici in alto
    g_set.quick_action = QUICK_TORCH;
    g_set.wifi_on = true;
    g_set.ble_on = false;
    g_set.ble_visible = false;
    g_set.doom_sens = 3;
    g_set.doom_inv_steer = true;
    g_set.doom_inv_pitch = true;
    g_set.volume = 70;
    g_set.saber_color = 0;
    g_set.saber_clash = 1;
    g_set.a4_x10 = 4400;
    strcpy(g_set.pwn_name, "Gadget");
    g_set.pwn_pcap = 1;
    g_set.pwn_ai = 1;
    g_set.boot_app = 0;
    g_set.pet_time = PET_TIME_REAL;
    g_set.pet_sound = 1;
    g_set.pet_steps = 1;
    g_set.pet_tilt_inv = 0;
}

void settings_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    defaults();
    nvs_handle_t h;
    if (nvs_open("gadget", NVS_READONLY, &h) != ESP_OK) return;
    // I campi nuovi sono in fondo: un salvataggio più vecchio (e più corto) si copia
    // sopra ai valori predefiniti e i campi aggiunti restano al default.
    settings_t tmp = g_set;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && tmp.version >= 1 && tmp.version <= SETTINGS_VERSION) {
        uint8_t old = tmp.version;
        memcpy(&g_set, &tmp, len);
        if (old < 2) g_set.flipped = true;   // v2: tasti fisici in alto
        if (g_set.doom_sens < 1 || g_set.doom_sens > 5) g_set.doom_sens = 3;
        if (old < 4) { g_set.volume = 70; g_set.saber_color = 0; }
        if (g_set.volume > 100) g_set.volume = 70;
        if (old < 5 || g_set.saber_clash > 2) g_set.saber_clash = 1;
        if (old < 6 || g_set.bercio_cal > 40 || g_set.bercio_cal < -40) g_set.bercio_cal = 0;
        if (old < 7 || g_set.a4_x10 < 3500 || g_set.a4_x10 > 5000) g_set.a4_x10 = 4400;
        if (old < 8) { g_set.doom_inv_steer = true; g_set.doom_inv_pitch = true; }
        if (old < 9) { strcpy(g_set.pwn_name, "Gadget"); g_set.pwn_pcap = 1; g_set.pwn_ai = 1; }
        if (old < 10) {
            g_set.boot_app = 0;
            g_set.pet_time = PET_TIME_REAL;
            g_set.pet_sound = 1;
            g_set.pet_steps = 1;
            g_set.pet_tilt_inv = 0;
        }
        if (g_set.pet_time >= PET_TIME_COUNT) g_set.pet_time = PET_TIME_REAL;
        if (old < SETTINGS_VERSION) {
            g_set.version = SETTINGS_VERSION;
            nvs_close(h);
            settings_save();
            return;
        }
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("gadget", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &g_set, sizeof(g_set));
    nvs_commit(h);
    nvs_close(h);
}

void settings_reset(void)
{
    defaults();
    settings_save();
}

const char *g_set_pwn_name(void) { return g_set.pwn_name; }

const char *settings_quick_name(int q)
{
    static const char *n[QUICK_COUNT] = {
        "Nessuna", "Torcia", "Spegni schermo", "Dadi", "Scanner Wi-Fi", "Scanner BLE", "Orologio",
    };
    return (q >= 0 && q < QUICK_COUNT) ? n[q] : "?";
}
