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
Dal launcher: Livella. La prima volta chiede una calibrazione in due passi (appoggiata, poi in piedi sul lato lungo): così sa come è montato l'accelerometro. A ogni passo fai lo swipe e lascia la scheda: aspetta che smetta di muoversi e misura per circa un secondo (se la tocchi ricomincia). Poi riconosce da sola il modo:
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

## Wi-Fi
Impostazioni › Wi-Fi › **Cerca reti e collegati**: elenco delle reti vicine, swipe a destra per collegarti (con la tastiera per la password). È la stessa schermata dello Scanner Wi-Fi, che funziona anche mentre sei connesso. Tocco prolungato: nuova scansione. In alternativa "Configura dal telefono" apre un hotspot con una pagina web.

## Bluetooth
Il Bluetooth è predisposto per collegarsi nei due sensi (le funzioni vere arriveranno sopra):
- **Collega a telefono o computer** (il Gadget è l'accessorio): per 2 minuti il Gadget si fa trovare col suo nome; sceglilo nelle impostazioni Bluetooth dell'altro dispositivo. Se l'altro ha una tastiera, sul Gadget compare un codice da digitare. Dopo l'associazione si ricollega da solo; nessun altro può associarsi a finestra chiusa. Base per la futura tastiera Bluetooth. Nota: iPhone elenca nelle impostazioni solo accessori di tipo noto (tastiera, cuffie…), quindi il Gadget comparirà lì quando avrà il profilo tastiera.
- **Collega un dispositivo** (il Gadget comanda): scansione, swipe a destra su un dispositivo collegabile per collegarti e vedere i servizi che offre (batteria, battito cardiaco, tastiera…).
- **Dispositivi**: collegamenti attivi e dispositivi associati; swipe a destra (due volte) per scollegare o dimenticare.

Limite dell'hardware: l'ESP32-S3 ha solo il Bluetooth Low Energy. Casse e cuffie riproducono l'audio col Bluetooth "classico" (A2DP), che questo chip non ha: per l'audio servirebbe un modulo esterno.

## 8-Ball veggente
Dal launcher: **8-Ball veggente**. Pensa a una domanda (da sì o no), scuoti la scheda o fai swipe a destra: la sfera ondeggia e dalla finestrella affiora il triangolo con una delle 20 risposte classiche (10 sì, 5 vaghe, 5 no; verde, colore d'accento, rosso). Mai la stessa due volte di fila.

## Q-20
Dal launcher: **Q-20**. Pensa a qualcosa (animale, oggetto, cibo, luogo, persona…) e rispondi alle domande toccando *Sì*, *Forse sì*, *Non so*, *Forse no*, *No*. Dopo una ventina di domande (prima, se è sicuro) prova a indovinare; se sbaglia continua, fino a 30. Se vince lui, ricorda ancora meglio quella cosa; se perde ti chiede cosa pensavi e la impara. BOOT annulla l'ultima risposta.

- Conosce all'inizio 250 cose e 100 domande (tabella generata da `tools/q20_gen.py`).
- Una risposta sbagliata non lo manda fuori strada: ogni risposta pesa, nessuna elimina.
- Quello che impara sta in `q20/kb.txt` sulla microSD ed entra nei backup. Senza microSD si gioca, ma non impara.
- Ampliare la conoscenza iniziale: modifica `tools/q20_gen.py` (le domande nuove solo in fondo), lancia `python3 tools/q20_gen.py` e aumenta `SEED_VERSION`: il Gadget aggiunge le cose nuove al suo file senza toccare quello che ha imparato.

## Appunti (testo dal PC → tastiera USB)
Serve per ridigitare codici e stringhe su un PC facendo da tastiera USB. Dal launcher: **Appunti**.

1. **Ricevi dal PC** (Bluetooth): il Gadget si fa trovare per qualche minuto. Dal PC apri la pagina Appunti in Chrome o Edge, premi *Collega il Gadget*, scegli il nome `Gadget-xxxx`. Poi ogni testo che incolli nella pagina arriva nella lista del Gadget.
2. **Scarica la pagina** (Wi-Fi): se sul PC non hai ancora la pagina, qui il Gadget apre un hotspot Wi-Fi con una pagina da salvare (premi *Scarica*). Chiudendo questa schermata l'hotspot si spegne e il Wi-Fi torna com'era. La pagina salvata funziona poi da sola (apri il file, usa il Bluetooth).
3. **Codici salvati**: la lista. Swipe a destra su una voce apre il dettaglio; con la USB-C collegata a un PC, un secondo swipe la digita come se la scrivessi a tastiera. BOOT (due volte) cancella la voce; *Cancella tutti* svuota la lista.
4. **Layout tastiera**: come è impostata la tastiera del PC su cui digiti: Italiano (predefinito), US, US internazionale, Regno Unito, Tedesco, Francese, Spagnolo. Scegli quello del PC, non quello della tastiera che vedi: il Gadget preme i tasti e il PC li traduce col suo layout. Le lettere accentate senza tasto proprio passano dal tasto morto (es. ´ poi a). Le cifre su Francese vogliono Shift: il Gadget lo fa da solo.

Solo il PC collegato da *Ricevi dal PC* (o un dispositivo associato) può mandare testi: altri dispositivi nei paraggi vengono rifiutati. I testi restano salvati finché non li cancelli tu. Attenzione: mentre la tastiera USB è attiva la console seriale su USB si sospende (sull'ESP32-S3 la porta fa una cosa per volta) e torna quando esci.

## Backup e ripristino
Impostazioni › Backup e ripristino:
- **Crea un backup ora**: salva tutto (impostazioni, Wi-Fi, polipetto, Radar e Pokédex, livella, quello che il Q-20 ha imparato) in un file della cartella `backup` sulla microSD.
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
- `main/ble_mgr.*` NimBLE: visibilità, scansione, collegamenti nei due sensi, associazioni, profili · `main/apps/app_bt.c` le schermate
- `main/pet_core.*` regole del Polipetto (pure, senza hardware) · `main/pet.*` tempo reale, salvataggio, contapassi, versi
- `main/clips.*` Appunti (testi dal PC) · `main/clip_ble.c` servizio Bluetooth di ricezione · `main/usbhid.*` tastiera USB · `main/apps/app_clips.c` le schermate
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
