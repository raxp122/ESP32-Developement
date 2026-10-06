// usbhid.h — la scheda fa da tastiera USB verso un PC (per ridigitare i codici salvati).
// Usa la porta USB-C come una tastiera fisica. Funziona solo se il firmware è compilato
// con il supporto USB (componente esp_tinyusb): usbhid_supported() lo dice.
//
// Attenzione: mentre la tastiera USB è attiva la console seriale su USB si sospende
// (sull'ESP32-S3 la stessa porta fa una cosa per volta) e torna quando si chiude.
#pragma once
#include <stdbool.h>

bool usbhid_supported(void);     // il firmware ha il supporto USB compilato
void usbhid_begin(void);         // prende la porta USB (fa da tastiera); console sospesa
void usbhid_end(void);           // libera la porta; la console torna
bool usbhid_mounted(void);       // un PC ci ha riconosciuti come tastiera

// Digita il testo come una tastiera `layout` (KB_LAYOUT_* di settings.h; mappe in usb_keymap.c). Si ferma se
// scollegano la USB. Ritorna i caratteri digitati; se < della lunghezza, interrotto.
// Un carattere non rappresentabile nel layout viene saltato.
int  usbhid_type(const char *text, int len, int layout);
void usbhid_cancel(void);        // interrompe una digitazione in corso
void usbhid_arm(void);           // da chiamare prima di avviare usbhid_type (azzera l'interruzione)
