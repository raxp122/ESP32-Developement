// logcon.c — log in memoria (PSRAM) + comandi dalla seriale USB
#include "logcon.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#define RING 16384
static char *ring;   // in PSRAM: la RAM interna serve a Wi-Fi, Bluetooth e DMA del display
static size_t head, used;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t orig;

static void ring_put(const char *s, int n)
{
    if (!ring) return;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < n; i++) {
        ring[head] = s[i];
        head = (head + 1) % RING;
        if (used < RING) used++;
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
    ring = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM);
    orig = esp_log_set_vprintf(hook);
}

static void dump(void)
{
    char *copy = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM);
    if (!copy || !ring) { free(copy); return; }
    size_t n, start;
    portENTER_CRITICAL(&mux);
    n = used;
    start = (head + RING - used) % RING;
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
    printf("Motivo dell'ultimo reset: %d\n", esp_reset_reason());
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
        case 'H': case 'h': case '?':
            printf("\nComandi: L = log dall'accensione · I = info · R = riavvia\n"); fflush(stdout); break;
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
