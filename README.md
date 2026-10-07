# Gadget — launcher per Waveshare ESP32-S3-Touch-LCD-3.49 (V1) e ESP32-S3-Touch-AMOLED-1.75

Firmware ESP-IDF 5.4 + LVGL 9.2 con menu a gesti e app modulari. Lo stesso firmware gira su due schede e all'avvio riconosce su quale si trova:
- **ESP32-S3-Touch-LCD-3.49**: schermo in orizzontale 640×172;
- **ESP32-S3-Touch-AMOLED-1.75** (anche -B e -G): AMOLED tondo 466×466, interfaccia in stile smartwatch.

## Scheda tonda (AMOLED 1.75)
Il riconoscimento è automatico: se sul bus I2C dei pin 47/48 risponde il TCA9554 è la 3.49, se sui pin 15/14 risponde l'AXP2101 è l'AMOLED. Pin, display, touch, alimentazione, batteria, microSD e audio si configurano di conseguenza. Il primo flash si fa via USB (`gadget.bin` a `0x0`), poi gli aggiornamenti OTA sono gli stessi della 3.49.

Sullo schermo tondo è tutto più grande (caratteri circa 1,4 volte, generati con `tools/gen_fonts_round.py` dai font di `tools/fonts`) e centrato:
- in cima ora, Wi-Fi, Bluetooth e batteria (con il simbolo di carica), sotto il titolo;
- le liste: voce precedente in alto, icona, voce corrente grande con il valore, la successiva in basso, e la posizione su un arco a destra;
- gli stessi gesti di sempre (swipe, BOOT, PWR: clic = schermo, tenuto 2 s = spegnimento dall'AXP2101).

**Fase 1, la base:** menu e impostazioni, orologio, aggiornamento OTA, Wi-Fi (ricerca reti con tastiera tonda, o configurazione dal telefono). Le altre app sullo schermo tondo non si aprono ancora (un avviso lo dice): arriveranno adattate una alla volta. Doom resta solo sulla 3.49.

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
**Wi-Fi e Bluetooth non sono mai accesi insieme**: l'ESP32-S3 ha una sola radio e con tutti e due attivi il Wi-Fi diventa lento e instabile. Di predefinito è acceso il Wi-Fi; accendere il Bluetooth (anche da Appunti › Ricevi dal PC o collegando un dispositivo) spegne il Wi-Fi, e viceversa. La schermata di aggiornamento riaccende da sola il Wi-Fi. Gli scanner funzionano comunque, finché sono aperti.

Il Bluetooth è predisposto per collegarsi nei due sensi (le funzioni vere arriveranno sopra):
- **Collega a telefono o computer** (il Gadget è l'accessorio): per 2 minuti il Gadget si fa trovare col suo nome; sceglilo nelle impostazioni Bluetooth dell'altro dispositivo. Se l'altro ha una tastiera, sul Gadget compare un codice da digitare. Dopo l'associazione si ricollega da solo; nessun altro può associarsi a finestra chiusa. Base per la futura tastiera Bluetooth. Nota: iPhone elenca nelle impostazioni solo accessori di tipo noto (tastiera, cuffie…), quindi il Gadget comparirà lì quando avrà il profilo tastiera.
- **Collega un dispositivo** (il Gadget comanda): scansione, swipe a destra su un dispositivo collegabile per collegarti e vedere i servizi che offre (batteria, battito cardiaco, tastiera…).
- **Dispositivi**: collegamenti attivi e dispositivi associati; swipe a destra (due volte) per scollegare o dimenticare.

Limite dell'hardware: l'ESP32-S3 ha solo il Bluetooth Low Energy. Casse e cuffie riproducono l'audio col Bluetooth "classico" (A2DP), che questo chip non ha: per l'audio servirebbe un modulo esterno.

## 8-Ball veggente
Dal launcher: **8-Ball veggente**. Pensa a una domanda (da sì o no), scuoti la scheda o fai swipe a destra: la sfera ondeggia e dalla finestrella affiora il triangolo con una delle 20 risposte classiche (10 sì, 5 vaghe, 5 no; verde, colore d'accento, rosso). Mai la stessa due volte di fila.

## Theremin
Dal launcher: **Theremin**. Inclina la scheda avanti/indietro per cambiare la nota, ruotala a destra/sinistra per il volume (a sinistra fino al silenzio). Suona finché tieni il dito sulla zona grande a sinistra; **BOOT** imposta la posizione zero (la nota a metà dell'estensione). Dal menu Comandi puoi scambiare i due movimenti.

Pulsanti a destra:
- **Suono**: forma d'onda (Theremin, Sinusoide, Triangolo, Dente di sega, Quadra), estensione (nota più bassa e più alta, fino a quella del pianoforte: La0 – Do8, sposta di un'ottava, ripristino a La3 – La5), glide, vibrato e sua velocità, volume.
- **Scala**: libera (glissando, come il theremin vero) oppure agganciata a cromatica, maggiore, minore, pentatonica o blues, con la tonica a scelta. Il LA di riferimento è quello dell'Accordatore.
- **Effetti**: eco (corta, media, lunga) e ripetizioni, timbro (da scuro a brillante).
- **Comandi**: quale movimento cambia la nota (inclinazione o rotazione), cosa fa l'altro (volume, vibrato, timbro o niente: volume fisso, quello del menu Suono), quanti gradi servono per tutte le note e per l'altro movimento (da 20° a 180°, cioè da rivolta in su a rivolta in giù), inversione dei due movimenti, "suona sempre", volume del microfono.
- **Registra**: registra quello che suoni in un file WAV nella cartella `theremin` della microSD. Con **Mic: sì** mixa anche il microfono, per cantare mentre suoni (il microfono sente anche l'altoparlante).
- **Registrazioni**: elenco per riascoltarle (swipe a destra) o cancellarle (BOOT due volte).

## Sismografo
Dal launcher: **Sismografo** → *Avvia*. Appoggia il Gadget su una superficie ferma (tavolo, pavimento): l'accelerometro misura le vibrazioni 200 volte al secondo e, tolta la gravità, il grafico mostra l'asse verticale con la scala che si adatta da sola. In alto la vibrazione attuale e il picco in mg (millesimi di g), con un'intensità locale stimata sulla scala Mercalli (Wald 1999: è quella che sente il Gadget, non quella del terremoto). BOOT azzera il picco, swipe a destra apre gli eventi. Lo schermo può spegnersi: le letture continuano finché l'app è aperta.

- **Eventi**: dopo 20 s di calibrazione, un evento scatta quando la media breve dell'energia (STA, 0,5 s) supera di 3,5–5 volte quella lunga (LTA, 20 s) e la vibrazione supera la soglia scelta (metodo STA/LTA dei sismografi). Finisce dopo 2 s di calma (al massimo 2 minuti).
- **Sensibilità**: alta (anche vibrazioni deboli), media, bassa (solo scosse forti). I colpi secchi e brevi (un passo, una porta) di solito non bastano: serve una vibrazione che duri almeno qualche decimo di secondo.
- **Registra sulla microSD**: ogni evento, con 5 s prima e 5 s dopo, va in `sismo/AAAAMMGG-hhmmss.csv` (100 campioni al secondo, x/y/z in mg: si apre con Excel); il registro di tutti è `sismo/eventi.csv`. Dal menu si consultano gli ultimi 50 e si cancellano.

## Orologio scacchi
Dal launcher: **Orologio scacchi** → *Nuova partita*. Lo schermo è diviso in due: chi ha mosso tocca la sua metà e parte l'orologio dell'avversario (all'inizio il nero tocca la sua metà per far partire il bianco). **BOOT** mette in pausa (con *Riprendi* e *Azzera*); in partita gli swipe non fanno nulla, si esce dalla pausa con swipe a sinistra.

Impostazioni nel menu:
- **Cadenza**: Bullet 1+0 e 2+1, Blitz 3+0, 3+2, 5+0, 5+3, Rapid 10+0, 10+5, 15+10, 25+10, Classica 30+0, 60+30, 90+30, FIDE (90' per 40 mosse + 30', +30" a mossa), Ritardo 5 min 5", Clessidra 1 min; cambiando le voci sotto diventa *Personalizzata*.
- **Tempo** e **Tempo del nero** (diverso per dare un handicap), da 15 s a 3 ore.
- **Modalità**: incremento (Fischer), Bronstein, ritardo (delay), clessidra o nessun incremento; **Secondi per mossa** (0–60).
- **Secondo periodo**: dopo 20–60 mosse si aggiunge del tempo (5–90 min).
- **Lato del bianco**, **Suoni** (clic della mossa, avvisi, tempo scaduto), **Avviso tempo basso** (10 s – 1 min: tempo in rosso, un bip, poi un tic al secondo negli ultimi 10), **Conta le mosse**.

## Q-20
Dal launcher: **Q-20**. Pensa a qualcosa (animale, oggetto, cibo, luogo, persona…) e rispondi alle domande toccando *Sì*, *Forse sì*, *Non so*, *Forse no*, *No*. Dopo una ventina di domande (prima, se è sicuro) prova a indovinare; se sbaglia continua, fino a 30. Se vince lui, ricorda ancora meglio quella cosa; se perde ti chiede cosa pensavi e la impara. Il pulsante ← (o BOOT) torna indietro all'ultima domanda se cambi idea, anche quando sta già provando a indovinare.

- Conosce all'inizio 250 cose e 100 domande (tabella generata da `tools/q20_gen.py`).
- Una risposta sbagliata non lo manda fuori strada: ogni risposta pesa, nessuna elimina.
- **Imparate**: dalla schermata iniziale, l'elenco delle cose che ha imparato da te; swipe a destra (due volte) per fargliene dimenticare una.
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
- **Dalla scheda (OTA):** Impostazioni › Sistema › Aggiornamento firmware. Ogni merge su `main` pubblica una release su GitHub con il firmware; la scheda la scarica via Wi-Fi e si riavvia. Se il nuovo firmware non riesce ad avviarsi, al riavvio torna da solo quello precedente. Mentre sei nella schermata di aggiornamento il Bluetooth va in pausa (la radio è condivisa col Wi-Fi) e si riaccende uscendo. Con "Cerca aggiornamenti da solo" avvisa quando ne esce uno nuovo.
- **Via USB:** `gadget.bin` a `0x0` (immagine unica) oppure solo `gadget-app.bin` a `0x20000`.

Dalla 0.14 le impostazioni (NVS) stanno in fondo alla flash (`0xFF0000`): né l'OTA né i file qui sopra le toccano più.
**Passaggio dalla 0.13 (una volta sola):** la tabella delle partizioni cambia, quindi serve un flash via USB. Con l'immagine unica le impostazioni vecchie si perdono (stavano proprio nella zona che l'immagine riempie). Per conservarle, flasha invece i file separati della release/artifact: `bootloader.bin` a `0x0`, `partition-table.bin` a `0x8000`, `ota_data_initial.bin` a `0xf000`, `gadget-app.bin` a `0x20000`. Al primo avvio vengono copiate nella nuova posizione.

## Struttura
- `main/board.*` riconoscimento della scheda, pin, alimentazione (3.49: latch TCA9554 EXIO6 e batteria sull'ADC; AMOLED: AXP2101), RTC PCF85063, IMU QMI8658, pulsanti
- `main/display.*` dimensioni dello schermo, pannello AXS15231B in QSPI (rotazione software), task LVGL · `main/display_round.*` AMOLED CO5300 466×466 (invio delle sole zone cambiate)
- `main/fonts_sel.c` caratteri per la scheda (`main/fonts/font_*_r.c` per il tondo)
- `main/input.*` touch (AXS15231B o CST9217) + riconoscimento gesti + pulsanti → eventi `nav_t`
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
Il file viene compilato in automatico. Flag utili: `APP_FULLSCREEN`, `APP_NO_SLEEP`, `APP_OWN_QUICK`, `APP_ROUND_OK` (l'app è disegnata anche per lo schermo tondo: usa `SCR_W`/`SCR_H`/`SCR_ROUND` invece di coordinate fisse).
Il callback `tick` viene chiamato ogni 200 ms mentre l'app è aperta. Tutto gira nel task LVGL:
dagli altri task non toccare oggetti LVGL senza `display_lock()`.
