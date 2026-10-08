// logcon.c — log in memoria (PSRAM) + comandi dalla seriale USB
#include "board.h"
#include "logcon.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#define RING 16384
#define LOG_MAGIC 0x4C4F4731u   // "LOG1"
// In PSRAM non azzerata all'avvio: dopo un riavvio per errore (crash, watchdog) il log della
// sessione di prima è ancora lì e si legge col comando P. Dopo uno spegnimento è spazzatura:
// lo dice il numero magico (e il motivo del reset).
static EXT_RAM_NOINIT_ATTR struct { uint32_t magic; size_t head, used; char buf[RING]; } L;
static char *ring = L.buf;
static char *prev;               // copia del log della sessione prima (se c'è)
static size_t prev_n;
static int prev_reason = -1;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t orig;

static void ring_put(const char *s, int n)
{
    if (!ring) return;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < n; i++) {
        ring[L.head] = s[i];
        L.head = (L.head + 1) % RING;
        if (L.used < RING) L.used++;
    }
    portEXIT_CRITICAL(&mux);
}

static int hook(const char *fmt, va_list ap)
{
    char line[256];
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(line, sizeof(line), fmt, cp);
    va_end(cp);
    if (n > 0) ring_put(line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
    return orig ? orig(fmt, ap) : vprintf(fmt, ap);
}

void logcon_init(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool kept = r != ESP_RST_POWERON && r != ESP_RST_UNKNOWN && r != ESP_RST_EXT;
    if (kept && L.magic == LOG_MAGIC && L.used <= RING && L.head < RING) {
        prev = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM);
        if (prev) {
            size_t start = (L.head + RING - L.used) % RING;
            for (size_t i = 0; i < L.used; i++) prev[i] = L.buf[(start + i) % RING];
            prev_n = L.used;
            prev_reason = r;
        }
    }
    L.magic = LOG_MAGIC;
    L.head = L.used = 0;
    orig = esp_log_set_vprintf(hook);
}

static const char *reason_name(int r)
{
    switch (r) {
    case ESP_RST_SW:       return "riavvio software";
    case ESP_RST_PANIC:    return "errore (panic)";
    case ESP_RST_INT_WDT:  return "watchdog degli interrupt";
    case ESP_RST_TASK_WDT: return "watchdog dei task";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "tensione bassa (brownout)";
    case ESP_RST_DEEPSLEEP: return "risveglio";
    default:               return "altro";
    }
}

static void dump_prev(void)
{
    if (!prev) { printf("\nNessun log della sessione precedente (era spenta, o non c'era)\n"); fflush(stdout); return; }
    printf("\n===== log della sessione precedente · finita per: %s (%d) =====\n", reason_name(prev_reason), prev_reason);
    fwrite(prev, 1, prev_n, stdout);
    printf("===== fine log precedente =====\n");
    fflush(stdout);
}

static void dump(void)
{
    char *copy = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM);
    if (!copy || !ring) { free(copy); return; }
    size_t n, start;
    portENTER_CRITICAL(&mux);
    n = L.used;
    start = (L.head + RING - L.used) % RING;
    for (size_t i = 0; i < n; i++) copy[i] = ring[(start + i) % RING];
    portEXIT_CRITICAL(&mux);
    printf("\n===== log dall'accensione (%u byte) =====\n", (unsigned)n);
    fwrite(copy, 1, n, stdout);
    printf("===== fine log =====\n");
    fflush(stdout);
    free(copy);
}

static void info(void)
{
    int s = (int)(esp_timer_get_time() / 1000000);
    printf("\nGadget · ESP-IDF %s · acceso da %dh%02dm%02ds\n", esp_get_idf_version(), s / 3600, (s / 60) % 60, s % 60);
    printf("RAM libera %u KB (minimo %u KB) · PSRAM libera %u KB\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    printf("Motivo dell'ultimo reset: %s (%d) · recuperi del touch: %d\n", reason_name(esp_reset_reason()), esp_reset_reason(), board_touch_recoveries());
    fflush(stdout);
}

static void task(void *arg)
{
    uint8_t c;
    for (;;) {
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
        switch (c) {
        case 'L': case 'l': dump(); break;
        case 'R': case 'r': printf("\nRiavvio…\n"); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart(); break;
        case 'I': case 'i': info(); break;
        case 'P': case 'p': dump_prev(); break;
        case 'H': case 'h': case '?':
            printf("\nComandi: L = log dall'accensione · P = log della sessione prima del riavvio · I = info · R = riavvia\n"); fflush(stdout); break;
        default: break;
        }
    }
}

void logcon_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return;
    usb_serial_jtag_vfs_use_driver();
    xTaskCreate(task, "logcon", 4096, NULL, 2, NULL);
}
