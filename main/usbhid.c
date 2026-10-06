// usbhid.c — tastiera USB (HID) con TinyUSB. Vedi usbhid.h.
//
// Il codice vero è compilato solo quando c'è il componente esp_tinyusb (HAVE_USBHID,
// definito dal CMakeLists se la cartella components/esp_tinyusb esiste). Senza, resta
// un guscio che dice "non disponibile", così il firmware si compila lo stesso.
#include "usbhid.h"
#include "settings.h"

#ifndef HAVE_USBHID

bool usbhid_supported(void) { return false; }
void usbhid_begin(void) {}
void usbhid_end(void) {}
bool usbhid_mounted(void) { return false; }
int  usbhid_type(const char *t, int l, int layout) { (void)t; (void)l; (void)layout; return 0; }
void usbhid_cancel(void) {}
void usbhid_arm(void) {}

#else   /* ================= TinyUSB presente ================= */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "usb_keymap.h"

static const char *TAG = "usbhid";
static bool installed;
static volatile bool cancel;

/* ---- descrittori ---- */

static const uint8_t hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };

// callback richiesti da TinyUSB
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return hid_report; }
uint16_t tud_hid_get_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t *buf, uint16_t len)
{ (void)i; (void)id; (void)t; (void)buf; (void)len; return 0; }
void tud_hid_set_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t const *buf, uint16_t len)
{ (void)i; (void)id; (void)t; (void)buf; (void)len; }

#define EPNUM_HID 0x81
static const uint8_t cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_report), EPNUM_HID, 16, 10),
};

static const tusb_desc_device_t dev_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00, .bDeviceSubClass = 0x00, .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,          // Espressif
    .idProduct = 0x4004,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01, .iProduct = 0x02, .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const char *str_desc[] = {
    (const char[]){0x09, 0x04},  // lingua: inglese
    "Gadget", "Gadget Keyboard", "0001",
};

bool usbhid_supported(void) { return true; }
bool usbhid_mounted(void) { return installed && tud_mounted(); }
void usbhid_cancel(void) { cancel = true; }
void usbhid_arm(void) { cancel = false; }

void usbhid_begin(void)
{
    if (installed) return;
    tinyusb_config_t cfg = {
        .device_descriptor = &dev_desc,
        .string_descriptor = str_desc,
        .string_descriptor_count = sizeof(str_desc) / sizeof(str_desc[0]),
        .external_phy = false,
        .configuration_descriptor = cfg_desc,
    };
    if (tinyusb_driver_install(&cfg) != ESP_OK) { ESP_LOGE(TAG, "tinyusb non installato"); return; }
    installed = true;
}

void usbhid_end(void)
{
    if (!installed) return;
#if defined(CONFIG_TINYUSB_INIT_IN_DEFAULT_TASK) || 1
    // ripristina la porta per la console (dove supportato dalla versione del componente)
    tinyusb_driver_uninstall();
#endif
    installed = false;
}

static bool wait_ready(void)
{
    for (int i = 0; i < 200 && !cancel; i++) {   // fino a ~2 s
        if (tud_mounted() && tud_hid_ready()) return true;
        if (!tud_mounted() && i > 10) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return tud_mounted() && tud_hid_ready();
}

static bool send(uint8_t mod, uint8_t key)
{
    if (!wait_ready()) return false;
    uint8_t kc[6] = {key, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, mod, key ? kc : NULL);
    if (!wait_ready()) return false;
    tud_hid_keyboard_report(0, 0, NULL);          // rilascia
    vTaskDelay(pdMS_TO_TICKS(6));
    return true;
}

int usbhid_type(const char *text, int len, int layout)
{
    if (!installed) return 0;
    if (len < 0) len = strlen(text);
    int done = 0;
    for (int i = 0; i < len && !cancel; ) {
        // decodifica UTF-8 → codice del carattere (le lettere accentate sono 2 byte)
        uint32_t c = (unsigned char)text[i];
        int l = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + l > len) break;
        if (l > 1) {
            c &= 0xFF >> (l + 1);
            for (int k = 1; k < l; k++) c = (c << 6) | ((unsigned char)text[i + k] & 0x3F);
        }
        i += l;
        kb_stroke_t k;
        if (!kb_map(c, layout, &k)) continue;   // carattere che il layout non sa scrivere: saltato
        // eventuale accento (tasto morto) prima, eventuale spazio dopo; tra due pressioni
        // dello stesso tasto send() manda sempre un report vuoto che le separa
        if (k.pkey && !send(k.pmod, k.pkey)) break;
        if (!send(k.mod, k.key)) break;
        if (k.space && !send(0, K_SPACE)) break;
        done++;
    }
    tud_hid_keyboard_report(0, 0, NULL);
    return done;
}

#endif
