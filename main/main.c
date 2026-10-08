// main.c — avvio del launcher
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include "logcon.h"
#include "sd.h"
#include "doom_app.h"
#include "pet.h"
#include "ota.h"
#include "apps/apps.h"
#include "clips.h"

void clip_ble_register(void);

static const char *TAG = "main";

static void time_from_rtc(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    struct tm utc;
    if (board_rtc_read(&utc)) {
        // l'RTC conserva l'ora UTC
        setenv("TZ", "UTC0", 1); tzset();
        time_t t = mktime(&utc);
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
        struct timeval tv = {.tv_sec = t};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "ora letta dall'RTC");
    }
}

// Riavvii per errore di fila (memoria RTC: sopravvive al reset, non allo spegnimento). Si
// azzera dopo 30 s di funzionamento; al terzo errore di fila la scheda parte in modalità
// sicura (niente Bluetooth, niente app all'avvio), così un'impostazione che la fa bloccare
// non la tiene in un ciclo di riavvii.
#define CRASH_MAGIC 0xC4A5u
static RTC_NOINIT_ATTR uint32_t crash_run;
static void crash_clear(void *arg) { crash_run = 0; }

static bool safe_mode_check(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
    uint32_t n = (crash_run >> 16) == CRASH_MAGIC ? crash_run & 0xFFFF : 0;
    n = crash ? n + 1 : 0;
    crash_run = (CRASH_MAGIC << 16) | (n & 0xFFFF);
    static esp_timer_handle_t t;
    const esp_timer_create_args_t a = {.callback = crash_clear, .name = "crash_clr"};
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 30 * 1000000);
    if (n < 3) return false;
    ESP_LOGW(TAG, "%u riavvii per errore di fila: modalità sicura", (unsigned)n);
    g_set.ble_on = false;
    settings_save();
    return true;
}

void app_main(void)
{
    logcon_init();         // conserva in RAM anche i log di avvio (comando "L" dal monitor)
    board_init();          // subito dopo: tiene accesa la scheda quando va a batteria
    ui_fonts_init();       // caratteri per lo schermo della scheda riconosciuta
    settings_load();
    bool safe = safe_mode_check();
    time_from_rtc();
    sd_mount();
    clips_init();          // Appunti: testi ricevuti dal PC

    if (doom_boot_requested() && !BOARD_IS_ROUND()) {
        // modalità Doom: niente Wi-Fi/Bluetooth, tutta la memoria al gioco
        display_init(g_set.flipped);
        logcon_start();
        ota_mark_valid();
        doom_run();
        return;
    }

    display_init(g_set.flipped);

    display_lock();
    ui_init();
    display_unlock();
    display_start_task();
    vTaskDelay(pdMS_TO_TICKS(150));          // accende la retroilluminazione dopo il primo frame
    display_set_brightness(g_set.brightness);

    wifi_mgr_init();
    clip_ble_register();   // servizio Bluetooth degli Appunti (prima di accendere lo stack)
    bool ble_guard = ble_mgr_boot_guard();
    ble_mgr_apply();

    // il polipetto vive in sottofondo; poi l'eventuale app scelta per l'avvio
    // (dopo Wi-Fi e Bluetooth, che gli scanner usano subito)
    display_lock();
    pet_init();
    if (!safe) boot_app_launch();
    else ui_toast("Modalità sicura: la scheda si era riavviata per errore più volte (Bluetooth spento)");
    if (ble_guard) ui_toast("Bluetooth spento: la scheda si era bloccata col Bluetooth acceso (log: comando P dal seriale)");
    display_unlock();

    logcon_start();
    ota_mark_valid();   // arrivati fin qui il firmware funziona: niente ritorno al precedente
    ota_auto_start();
    ESP_LOGI(TAG, "pronto · comandi dal monitor seriale: L log, I info, R riavvia");
}
