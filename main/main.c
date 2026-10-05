// main.c — avvio del launcher
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include "logcon.h"
#include "sd.h"
#include "doom_app.h"

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

void app_main(void)
{
    logcon_init();         // conserva in RAM anche i log di avvio (comando "L" dal monitor)
    board_init();          // subito dopo: tiene accesa la scheda quando va a batteria
    settings_load();
    time_from_rtc();
    sd_mount();

    if (doom_boot_requested()) {
        // modalità Doom: niente Wi-Fi/Bluetooth, tutta la memoria al gioco
        display_init(g_set.flipped);
        logcon_start();
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
    ble_mgr_apply();
    logcon_start();
    ESP_LOGI(TAG, "pronto · comandi dal monitor seriale: L log, I info, R riavvia");
}
