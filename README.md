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

## Polipetto
Un Tamagotchi originale con un polpetto arancione in pixel art. Dal launcher: Polipetto (al primo avvio compare un uovo che si schiude in un minuto).

| Comando | Azione |
|---|---|
| Swipe su / giù | sceglie l'icona: Cibo, Gioca, Pulisci, Medicina, Luce, Sgrida, Stato |
| Swipe a destra | conferma; nei sottomenu sceglie (Pasto, Spuntino, Acqua…) |
| Swipe a sinistra · BOOT | indietro / esce |
| Dito tenuto premuto | coccole |
| Scuotere la scheda | lo fa ridere (se dorme lo svegli!) |

Cresce da neonato a bimbo, ragazzo e adulto; la forma adulta (saggio, esploratore, normale, goloso, pasticcione) dipende dalle cure. Fame, sete, felicità, disciplina, peso, inchiostro da pulire, malattie, nanna con la luce: se lo trascuri può morire e si ricomincia da un nuovo uovo.
Giochi: *Da che parte?*, *Pesca* (inclinando la scheda) e *Passeggiata* (contapassi: i passi lo rendono felice e sbloccano l'esploratore).
Impostazioni › Polipetto: scorrere del tempo, orari di nanna e sveglia, versi, contapassi, verso dell'inclinazione, nuovo uovo.
- *Ibrida* (consigliata): tempo reale, anche a scheda spenta, ma negli orari di nanna scelti dorme protetto: nessuna chiamata, nessun errore, nessun pericolo. Senza ora impostata avanza solo a scheda accesa.
- *Tempo reale*, *Solo a scheda accesa*, *Solo con l'app aperta*.

Dai 25 giorni non invecchia più: vive per sempre, oppure da Impostazioni puoi lasciarlo tornare nell'oceano e ricominciare da un uovo.

## Livella
Dal launcher: Livella. La prima volta chiede una calibrazione in due passi (appoggiata, poi in piedi sul lato lungo): così sa come è montato l'accelerometro. Poi riconosce da sola il modo:
- **appoggiata**: bolla circolare con inclinazione X e Y;
- **sul lato lungo**: tubo orizzontale;
- **sul lato corto**: filo a piombo.

| Comando | Azione |
|---|---|
| Swipe a destra | azzera sulla superficie attuale |
| Swipe su / giù | unità: gradi, percentuale, mm/m |
| BOOT | blocca / sblocca la lettura |
| Dito tenuto premuto | rifà la calibrazione |
| Swipe a sinistra | esce |

## Backup e ripristino
Impostazioni › Backup e ripristino:
- **Crea un backup ora**: salva tutto (impostazioni, Wi-Fi, polipetto, Radar e Pokédex, livella) in un file della cartella `backup` sulla microSD.
- **Ripristina un backup**: elenca i backup; si possono usare solo quelli fatti con questa versione del firmware o con una più vecchia. Da un backup vecchio le impostazioni nuove prendono il valore predefinito e quelle che non esistono più si ignorano. Dopo il ripristino la scheda si riavvia.
- **Backup prima degli aggiornamenti**: prima di ogni aggiornamento OTA ne crea uno da solo.

Il file è di testo con un controllo CRC: un backup rovinato o incompleto viene rifiutato senza toccare niente. Contiene anche la password del Wi-Fi in chiaro, quindi tienilo al sicuro.

## App all'avvio
Impostazioni › App all'avvio: se scegli un'app, all'accensione si apre quella; swipe indietro torna al menu. "Nessuna" lascia tutto com'era.

## Monitor seriale
Il firmware conserva in RAM gli ultimi 16 KB di log dall'accensione. Dal monitor seriale (115200) invia:
`L` ristampa il log dall'accensione · `I` info di sistema · `R` riavvio software · `H` aiuto.

## Compilare
1. Installa ESP-IDF **5.4.x** (estensione ESP-IDF di VS Code o installer ufficiale).
2. Scarica LVGL nella cartella dei componenti:
   `git clone -b v9.2.2 --depth 1 https://github.com/lvgl/lvgl components/lvgl`
3. `idf.py set-target esp32s3` e poi `idf.py build flash monitor`.

Immagine unica per il flasher web (indirizzo 0x0), come fa la build su GitHub:
`esptool.py --chip esp32s3 merge_bin -o gadget.bin --flash_mode dio --flash_size 16MB 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xf000 build/ota_data_initial.bin 0x20000 build/gadget.bin`

## Aggiornamenti
- **Dalla scheda (OTA):** Impostazioni › Sistema › Aggiornamento firmware. Ogni merge su `main` pubblica una release su GitHub con il firmware; la scheda la scarica via Wi-Fi e si riavvia. Se il nuovo firmware non riesce ad avviarsi, al riavvio torna da solo quello precedente. Con "Cerca aggiornamenti da solo" avvisa quando ne esce uno nuovo.
- **Via USB:** `gadget.bin` a `0x0` (immagine unica) oppure solo `gadget-app.bin` a `0x20000`.

Dalla 0.14 le impostazioni (NVS) stanno in fondo alla flash (`0xFF0000`): né l'OTA né i file qui sopra le toccano più.
**Passaggio dalla 0.13 (una volta sola):** la tabella delle partizioni cambia, quindi serve un flash via USB. Con l'immagine unica le impostazioni vecchie si perdono (stavano proprio nella zona che l'immagine riempie). Per conservarle, flasha invece i file separati della release/artifact: `bootloader.bin` a `0x0`, `partition-table.bin` a `0x8000`, `ota_data_initial.bin` a `0xf000`, `gadget-app.bin` a `0x20000`. Al primo avvio vengono copiate nella nuova posizione.

## Struttura
- `main/board.*` pin, latch di alimentazione (TCA9554 EXIO6), batteria, RTC PCF85063, IMU QMI8658, pulsanti
- `main/display.*` pannello AXS15231B in QSPI, rotazione software, task LVGL
- `main/input.*` touch + riconoscimento gesti + pulsanti → eventi `nav_t`
- `main/ui.*` pila di schermate, barra di stato, spegnimento schermo, azione rapida, `list_view`
- `main/menu.c` menu generico (usato da home e impostazioni)
- `main/wifi_mgr.*` Wi-Fi, scansione, portale captive per configurare la rete dal telefono, NTP
- `main/ble_mgr.*` NimBLE: visibilità e scansione
- `main/pet_core.*` regole del Polipetto (pure, senza hardware) · `main/pet.*` tempo reale, salvataggio, contapassi, versi
- `main/backup.*` backup e ripristino su microSD · `main/ota.*` aggiornamenti via internet · `main/settings.*` impostazioni (con migrazione dalla vecchia tabella)
- `main/apps/` le app (`app_pet.c` + `pet_art.c` per il Polipetto, `app_ota.c` per gli aggiornamenti, `app_backup.c`, `app_level.c` per la livella)
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
