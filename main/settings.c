// settings.c
#include "settings.h"
#include <string.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define SETTINGS_VERSION 11
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
    g_set.pet_sleep_h = 22;
    g_set.pet_wake_h = 8;
    g_set.ota_auto = 1;
}

/* ---------------- migrazione dalla vecchia tabella delle partizioni ----------------
 * Fino alla 0.13 l'NVS stava a 0x9000, proprio dove l'immagine unica gadget.bin (che
 * riempie i vuoti di 0xFF) lo cancellava a ogni flash. Ora sta in fondo alla flash.
 * Al primo avvio, se il nuovo NVS è vuoto, copiamo i nostri dati dalla vecchia zona
 * ("nvs_v1"), che sopravvive se si flashano i file separati invece dell'immagine unica.
 */
#define OLD_NVS "nvs_v1"
static const char *const our_namespaces[] = {"gadget", "pet", "pwn"};

static void copy_entry(const nvs_entry_info_t *e)
{
    nvs_handle_t src, dst;
    if (nvs_open_from_partition(OLD_NVS, e->namespace_name, NVS_READONLY, &src) != ESP_OK) return;
    if (nvs_open(e->namespace_name, NVS_READWRITE, &dst) != ESP_OK) { nvs_close(src); return; }
    switch (e->type) {
    case NVS_TYPE_U8:  { uint8_t v;  if (nvs_get_u8(src, e->key, &v) == ESP_OK) nvs_set_u8(dst, e->key, v); break; }
    case NVS_TYPE_I8:  { int8_t v;   if (nvs_get_i8(src, e->key, &v) == ESP_OK) nvs_set_i8(dst, e->key, v); break; }
    case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(src, e->key, &v) == ESP_OK) nvs_set_u16(dst, e->key, v); break; }
    case NVS_TYPE_I16: { int16_t v;  if (nvs_get_i16(src, e->key, &v) == ESP_OK) nvs_set_i16(dst, e->key, v); break; }
    case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(src, e->key, &v) == ESP_OK) nvs_set_u32(dst, e->key, v); break; }
    case NVS_TYPE_I32: { int32_t v;  if (nvs_get_i32(src, e->key, &v) == ESP_OK) nvs_set_i32(dst, e->key, v); break; }
    case NVS_TYPE_U64: { uint64_t v; if (nvs_get_u64(src, e->key, &v) == ESP_OK) nvs_set_u64(dst, e->key, v); break; }
    case NVS_TYPE_I64: { int64_t v;  if (nvs_get_i64(src, e->key, &v) == ESP_OK) nvs_set_i64(dst, e->key, v); break; }
    case NVS_TYPE_STR:
    case NVS_TYPE_BLOB: {
        size_t n = 0;
        bool str = e->type == NVS_TYPE_STR;
        esp_err_t r = str ? nvs_get_str(src, e->key, NULL, &n) : nvs_get_blob(src, e->key, NULL, &n);
        if (r != ESP_OK || n > 4096) break;
        void *buf = malloc(n ? n : 1);
        if (!buf) break;
        r = str ? nvs_get_str(src, e->key, buf, &n) : nvs_get_blob(src, e->key, buf, &n);
        if (r == ESP_OK) {
            if (str) nvs_set_str(dst, e->key, buf);
            else nvs_set_blob(dst, e->key, buf, n);
        }
        free(buf);
        break;
    }
    default:
        break;
    }
    nvs_commit(dst);
    nvs_close(dst);
    nvs_close(src);
}

static void migrate_old_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("gadget", NVS_READONLY, &h) == ESP_OK) { nvs_close(h); return; }   // già fatto (o nuovo)
    if (nvs_flash_init_partition(OLD_NVS) != ESP_OK) return;
    int copied = 0;
    for (size_t i = 0; i < sizeof(our_namespaces) / sizeof(our_namespaces[0]); i++) {
        nvs_iterator_t it = NULL;
        esp_err_t r = nvs_entry_find(OLD_NVS, our_namespaces[i], NVS_TYPE_ANY, &it);
        while (r == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            copy_entry(&info);
            copied++;
            r = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
    }
    nvs_flash_deinit_partition(OLD_NVS);
    if (copied) ESP_LOGI("settings", "migrate %d voci dalla vecchia partizione NVS", copied);
}

void settings_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    migrate_old_nvs();
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
        if (old < 11) { g_set.pet_sleep_h = 22; g_set.pet_wake_h = 8; g_set.ota_auto = 1; }
        if (g_set.pet_time >= PET_TIME_COUNT) g_set.pet_time = PET_TIME_REAL;
        if (g_set.pet_sleep_h > 23) g_set.pet_sleep_h = 22;
        if (g_set.pet_wake_h > 23) g_set.pet_wake_h = 8;
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
