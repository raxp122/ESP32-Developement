# Gadget — launcher per Waveshare ESP32-S3-Touch-LCD-3.49 (V1)

Firmware ESP-IDF 5.4 + LVGL 9.2 con schermo in orizzontale (640×172), menu a gesti e app modulari.

## Comandi
| Gesto / tasto | Azione |
|---|---|
| Swipe su / giù | voce successiva / precedente (invertibile in Impostazioni › Schermo) |
| Swipe a destra | avanti / conferma; sulle voci regolabili entra in modifica (poi su/giù cambia, destra o sinistra esce) |
| Swipe a sinistra · BOOT | indietro / annulla |
| Dito fermo ~1 s · BOOT tenuto | azione rapida (Impostazioni › Azione rapida) |
| Tocco breve | nessuna azione (risveglia lo schermo se spento) |
| PWR | spegne / riaccende lo schermo |
| PWR tenuto 2 s | spegne la scheda (solo a batteria) |

Gli swipe lenti sono supportati: il dito si considera sollevato solo dopo ~85 ms senza contatto.

## Doom
Copia un IWAD nella cartella `doom` della microSD (FAT32): `doom1.wad` (shareware) oppure `doom.wad`, `doom2.wad`, `freedoom1.wad`…
Dal launcher: Doom › Gioca. La scheda si riavvia in modalità Doom (Wi-Fi e Bluetooth spenti, tutta la memoria al gioco).

| Comando | Azione |
|---|---|
| Inclinazione avanti/indietro | cammina avanti/indietro (nei menu: su/giù) |
| Inclinazione destra/sinistra | gira (nei menu: destra/sinistra) |
| BOOT | ricalibra la posizione di riposo (anche automatico all'inizio di ogni livello) |
| BOOT tenuto 1,5 s | esce e torna al launcher |
| Bande laterali | Menu · Usa · Arma (sinistra), Pausa · Mappa · SPARA (destra); SPARA conferma nei menu, Usa torna indietro |
| PWR tenuto 2 s | spegne (a batteria) |

Prima di giocare usa Doom › Prova inclinazione e, se serve, le voci Inverti.
Configurazione e salvataggi vanno in `doom/` sulla microSD. Audio non ancora implementato.

Il motore è prboom (dal progetto retro-go, GPL): il firmware che lo include è distribuito sotto GPL.

## Monitor seriale
Il firmware conserva in RAM gli ultimi 16 KB di log dall'accensione. Dal monitor seriale (115200) invia:
`L` ristampa il log dall'accensione · `I` info di sistema · `R` riavvio software · `H` aiuto.

## Compilare
1. Installa ESP-IDF **5.4.x** (estensione ESP-IDF di VS Code o installer ufficiale).
2. Scarica LVGL nella cartella dei componenti:
   `git clone -b v9.2.2 --depth 1 https://github.com/lvgl/lvgl components/lvgl`
3. `idf.py set-target esp32s3` e poi `idf.py build flash monitor`.

Immagine unica per il flasher web (indirizzo 0x0):
`esptool.py --chip esp32s3 merge_bin -o gadget.bin --flash_mode dio --flash_size 16MB 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/gadget.bin`

## Struttura
- `main/board.*` pin, latch di alimentazione (TCA9554 EXIO6), batteria, RTC PCF85063, IMU QMI8658, pulsanti
- `main/display.*` pannello AXS15231B in QSPI, rotazione software, task LVGL
- `main/input.*` touch + riconoscimento gesti + pulsanti → eventi `nav_t`
- `main/ui.*` pila di schermate, barra di stato, spegnimento schermo, azione rapida, `list_view`
- `main/menu.c` menu generico (usato da home e impostazioni)
- `main/wifi_mgr.*` Wi-Fi, scansione, portale captive per configurare la rete dal telefono, NTP
- `main/ble_mgr.*` NimBLE: visibilità e scansione
- `main/apps/` le app
- `components/axs15231b` driver Waveshare inclusi nel progetto

## Aggiungere un'app
```c
// main/apps/app_ciao.c
#include "apps.h"
static lv_obj_t *lbl;
static void enter(lv_obj_t *root, void *arg) {
    lbl = lv_label_create(root);
    lv_obj_set_style_text_font(lbl, &font_l, 0);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_label_set_text(lbl, "Ciao!");
    lv_obj_center(lbl);
}
static bool nav(nav_t ev) {
    if (ev == NAV_SELECT) { lv_label_set_text(lbl, "Confermato"); return true; }
    return false; // NAV_BACK non gestito = torna indietro
}
const app_t app_ciao = { .name = "Ciao", .icon = ICON_GHOST, .enter = enter, .nav = nav };
```
Poi aggiungi `extern const app_t app_ciao;` in `apps/apps.h` e una voce in `apps/home.c`.
Il file viene compilato in automatico. Flag utili: `APP_FULLSCREEN`, `APP_NO_SLEEP`, `APP_OWN_QUICK`.
Il callback `tick` viene chiamato ogni 200 ms mentre l'app è aperta. Tutto gira nel task LVGL:
dagli altri task non toccare oggetti LVGL senza `display_lock()`.
