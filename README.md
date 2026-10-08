# Gadget — launcher per Waveshare ESP32-S3-Touch-LCD-3.49 (V1) e ESP32-S3-Touch-AMOLED-1.75

Firmware ESP-IDF 5.4 + LVGL 9.2 con menu a gesti e app modulari. Lo stesso firmware gira su due schede e all'avvio riconosce su quale si trova:
- **ESP32-S3-Touch-LCD-3.49**: schermo in orizzontale 640×172;
- **ESP32-S3-Touch-AMOLED-1.75** (anche -B e -G): AMOLED tondo 466×466, interfaccia in stile smartwatch.

Si aggiorna via Wi-Fi: ogni modifica unita su `main` diventa una release che la scheda scarica e installa da Impostazioni › Sistema (vedi [Aggiornamenti](#aggiornamenti)).

**Le app** (nel launcher in ordine alfabetico, con *Cerca* in cima):
[8-Ball veggente](#8-ball-veggente) · [Accordatore](#accordatore) · [Appunti](#appunti-testo-dal-pc--tastiera-usb) · [Berciometro](#berciometro) · [Dadi](#dadi) · [Doom](#doom) · [Livella](#livella) · [Morse](#morse) · [Orologio](#orologio) · [Orologio scacchi](#orologio-scacchi) · [Polipetto](#polipetto) · [Q-20](#q-20) · [Radar](#radar) · [Scacchi](#scacchi) · [Scanner Wi-Fi e Bluetooth](#scanner-wi-fi-e-bluetooth) · [Sismografo](#sismografo) · [Snake](#snake) · [Spada laser](#spada-laser) · [Tester Wi-Fi](#tester-wi-fi) · [Theremin](#theremin) · [Torcia](#torcia) · [Impostazioni](#impostazioni)

## Scheda tonda (AMOLED 1.75)
Il riconoscimento è automatico: se sul bus I2C dei pin 47/48 risponde il TCA9554 è la 3.49, se sui pin 15/14 risponde l'AXP2101 è l'AMOLED. Pin, display, touch, alimentazione, batteria, microSD e audio si configurano di conseguenza. Il primo flash si fa via USB (`gadget.bin` a `0x0`), poi gli aggiornamenti OTA sono gli stessi della 3.49.

Sullo schermo tondo è tutto più grande (caratteri circa 1,4 volte, generati con `tools/gen_fonts_round.py` dai font di `tools/fonts`) e centrato:
- in cima ora, Wi-Fi, Bluetooth e batteria (con il simbolo di carica), sotto il titolo;
- le liste: voce precedente in alto, icona, voce corrente grande con il valore, la successiva in basso, e la posizione su un arco a destra;
- gli stessi gesti di sempre (swipe, BOOT, PWR: clic = schermo, tenuto 2 s = spegnimento dall'AXP2101).

**Già pronte per il tondo:** menu e impostazioni (con la tastiera tonda per le password Wi-Fi), Orologio, aggiornamento OTA, configurazione Wi-Fi dal telefono, Scanner Wi-Fi, Tester Wi-Fi, Sismografo, Snake, Scacchi, Morse e la schermata "Sul telefono" con i QR. Le altre app sul tondo non si aprono ancora (un avviso lo dice) e arriveranno adattate una alla volta; Doom resta solo sulla 3.49.

## Comandi
| Gesto / tasto | Azione |
|---|---|
| Swipe su / giù | voce successiva / precedente (invertibile in Impostazioni › Schermo) |
| Swipe a destra | avanti / conferma; sulle voci regolabili entra in modifica (poi su/giù cambia, destra o sinistra esce) |
| Swipe a sinistra · BOOT | indietro / annulla |
| Dito fermo ~1 s · BOOT tenuto | azione rapida (Impostazioni › Azione rapida) |
| Tocco breve | nessuna azione (risveglia lo schermo se si è spento per inattività) |
| PWR | spegne / riaccende lo schermo. Spento così resta **bloccato**: tocchi e BOOT non fanno nulla (in tasca) finché non ripremi PWR. Spento per inattività, invece, si riaccende toccandolo |
| PWR tenuto 2 s | spegne la scheda (solo a batteria) |

Gli swipe lenti sono supportati: il dito si considera sollevato solo dopo ~85 ms senza contatto.

**Cerca** (dal launcher): scrivi con la tastiera e trova app e voci di tutti i menu. Prima vengono i nomi che iniziano con quello che hai scritto, poi le parole interne; a parità, le app del launcher prima delle voci dei sottomenu (così "polipetto" apre l'app, e sotto ci sono le sue impostazioni). Su/giù scorre i risultati, destra apre.

## 8-Ball veggente
Dal launcher: **8-Ball veggente**. Pensa a una domanda (da sì o no), scuoti la scheda o fai swipe a destra: la sfera ondeggia e dalla finestrella affiora il triangolo con una delle 20 risposte classiche (10 sì, 5 vaghe, 5 no; verde, colore d'accento, rosso). Mai la stessa due volte di fila.

## Accordatore
Dal launcher: **Accordatore**. Ascolta con i due microfoni e riconosce la nota (algoritmo YIN, da circa 30 Hz, il Si grave del basso a 5 corde, a oltre 1,3 kHz).
- **Cromatico**: qualsiasi strumento o voce: nota, ottava e scostamento in centesimi.
- **Chitarra**, **Basso** (4, 5 e 6 corde), **Ukulele** (Sol acuto, Low G, Re, baritono): accordatura guidata corda per corda, anche con le accordature alternative.
- **LA di riferimento** regolabile, con i **LA storici** (440, 442, 432, 415 barocco…) e il ritorno a 440 Hz. Lo usa anche il Theremin.

## Appunti (testo dal PC → tastiera USB)
Serve per ridigitare codici e stringhe su un PC facendo da tastiera USB. Dal launcher: **Appunti**.

1. **Ricevi dal PC** (Bluetooth): il Gadget si fa trovare per qualche minuto. Dal PC apri la pagina Appunti in Chrome o Edge, premi *Collega il Gadget*, scegli il nome `Gadget-xxxx`. Poi ogni testo che incolli nella pagina arriva nella lista del Gadget. Se il collegamento cade, *Invia* ricollega da solo il Gadget già scelto. Se la pagina dice che il Gadget non risponde, riapri *Ricevi dal PC*; se il Gadget compare tra i dispositivi associati nelle impostazioni Bluetooth di Windows, rimuovilo da lì (Windows ricorderebbe un elenco di servizi vecchio). Dopo questo aggiornamento scarica di nuovo la pagina dall'hotspot: la copia salvata prima ha la versione vecchia. Per ricevere la radio passa al Bluetooth: se prima c'era il Wi-Fi, uscendo da questa schermata il Bluetooth si spegne e torna il Wi-Fi.
2. **Scarica la pagina** (Wi-Fi): se sul PC non hai ancora la pagina, qui il Gadget apre un hotspot Wi-Fi con una pagina da salvare (premi *Scarica*). Chiudendo questa schermata l'hotspot si spegne e il Wi-Fi torna com'era. Nel browser scrivi l'indirizzo con `http://` (`http://192.168.4.1`): Edge e Chrome provano prima `https://`, ma il Gadget non ha un certificato. Se compare l'avviso "connessione non sicura" o "certificato", scegli di continuare: la pagina non manda niente in rete. La pagina salvata funziona poi da sola (apri il file, usa il Bluetooth).
3. **Codici salvati**: la lista. Swipe a destra su una voce apre il dettaglio; con la USB-C collegata a un PC, un secondo swipe la digita come se la scrivessi a tastiera. BOOT (due volte) cancella la voce; *Cancella tutti* svuota la lista.
4. **Layout tastiera**: come è impostata la tastiera del PC su cui digiti: Italiano (predefinito), US, US internazionale, Regno Unito, Tedesco, Francese, Spagnolo. Scegli quello del PC, non quello della tastiera che vedi: il Gadget preme i tasti e il PC li traduce col suo layout. Le lettere accentate senza tasto proprio passano dal tasto morto (es. ´ poi a). Le cifre su Francese vogliono Shift: il Gadget lo fa da solo.

Solo il PC collegato da *Ricevi dal PC* (o un dispositivo associato) può mandare testi: altri dispositivi nei paraggi vengono rifiutati. I testi restano salvati finché non li cancelli tu. Attenzione: mentre la tastiera USB è attiva la console seriale su USB si sospende (sull'ESP32-S3 la porta fa una cosa per volta) e torna quando esci.

## Berciometro
Dal launcher: **Berciometro**. Misura il livello sonoro dal microfono (RMS su blocchi da 50 ms, media "Fast" da 125 ms come i fonometri), con grafico in tempo reale e record. BOOT o swipe a destra azzerano il record, su/giù regolano la calibrazione. I dB sono stimati (sensibilità del microfono e guadagno non sono documentati): ottimo per confronti, non è uno strumento certificato.

## Dadi
Dal launcher: **Dadi**. Si compone un "pool" di dadi misti, con la somma automatica:
- **d4, d6, d8, d10, d12, d20, d100**: quanti di ciascuno (destra per regolare, su/giù cambia); **Modificatore** da aggiungere; **Svuota il pool**.
- **Tira**: risultato di ogni dado e totale. Swipe a destra o una scossa ritira, sinistra torna al pool.
- **Daggerheart**: i due dadi Speranza e Paura con modificatore e vantaggio/svantaggio; il risultato dice se è *con Speranza* o *con Paura*.

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

## Morse
Dal launcher: **Morse**. Per imparare il codice Morse e usarlo fra Gadget vicini.

- **Impara ad ascoltare** (metodo Koch): il Gadget suona una lettera e tu scegli quale era fra quattro (su/giù, destra conferma, BOOT la fa risentire). Si parte da K e M. Quando nelle ultime 20 risposte arrivi al 90%, si apre una lettera nuova, fino a 40 (lettere, numeri e punteggiatura). Le lettere nuove e quelle che sbagli escono più spesso. Dopo ogni risposta vedi la lettera e i suoi segni (• —).
- **Impara a trasmettere**: batti la lettera mostrata con BOOT (sul tondo anche col dito): tocco corto = punto, lungo = linea. Il Gadget riconosce cosa hai battuto, misura la tua velocità e passa alla lettera dopo.
- **Telegrafo** (ESP-NOW): parla con altri Gadget vicini via radio, **senza rete, router né associazione**.
  - Quello che batti con BOOT si sente in tempo reale su tutti i Gadget vicini, e ciascuno lo decodifica e lo scrive.
  - Con *destra* scrivi un messaggio con la tastiera: gli altri lo ricevono, lo leggono e lo sentono in Morse.
  - In alto il canale, il tuo nome (Gadget-XXXX) e quanti Gadget sono in ascolto; *su* pulisce il testo.
  - Tutti devono stare sullo stesso **Canale del Telegrafo** (impostazione, 1-13). Finché il Telegrafo è aperto la radio serve solo a quello: la connessione Wi-Fi è sospesa e torna uscendo.
- **Ascolta col microfono**: decodifica il Morse che sente (un altro Gadget, un'app, un fischio): un tono fra 400 e 1200 Hz, la velocità si adatta da sola. BOOT cancella il testo.
- **Tabella**: tutti i caratteri con i loro segni; destra li fa sentire.
- **Impostazioni**: velocità (5-35 parole al minuto), velocità effettiva (spaziatura di Farnsworth: lettere veloci e pause lunghe, il modo consigliato per imparare), tono (400-1200 Hz), canale del Telegrafo, ricomincia il corso. Progressi e impostazioni entrano nei backup.

## Orologio
Dal launcher: **Orologio**. Ora grande con la data; swipe a destra mostra o nasconde i secondi. Sul tondo l'ora sta al centro del cerchio, secondi e data sotto. Il Polipetto passeggia accanto all'ora (a destra sul 3,49", sotto la data sul tondo); si toglie da Impostazioni › Polipetto › Sull'orologio. L'ora arriva da Internet (Wi-Fi) e si conserva nell'orologio della scheda: vedi [Data e ora](#data-e-ora).

## Orologio scacchi
Dal launcher: **Orologio scacchi** → *Nuova partita*. Lo schermo è diviso in due: chi ha mosso tocca la sua metà e parte l'orologio dell'avversario (all'inizio il nero tocca la sua metà per far partire il bianco). **BOOT** mette in pausa (con *Riprendi* e *Azzera*); in partita gli swipe non fanno nulla, si esce dalla pausa con swipe a sinistra.

Impostazioni nel menu:
- **Cadenza**: Bullet 1+0 e 2+1, Blitz 3+0, 3+2, 5+0, 5+3, Rapid 10+0, 10+5, 15+10, 25+10, Classica 30+0, 60+30, 90+30, FIDE (90' per 40 mosse + 30', +30" a mossa), Ritardo 5 min 5", Clessidra 1 min; cambiando le voci sotto diventa *Personalizzata*.
- **Tempo** e **Tempo del nero** (diverso per dare un handicap), da 15 s a 3 ore.
- **Modalità**: incremento (Fischer), Bronstein, ritardo (delay), clessidra o nessun incremento; **Secondi per mossa** (0–60).
- **Secondo periodo**: dopo 20–60 mosse si aggiunge del tempo (5–90 min).
- **Lato del bianco**, **Suoni** (clic della mossa, avvisi, tempo scaduto), **Avviso tempo basso** (10 s – 1 min: tempo in rosso, un bip, poi un tic al secondo negli ultimi 10), **Conta le mosse**.

## Polipetto
Un Tamagotchi originale con un polpetto in pixel art. Dal launcher: Polipetto (al primo avvio compare un uovo che si schiude in un minuto). Gira sul 3,49"; sul tondo si vede nell'Orologio.

| Comando | Azione |
|---|---|
| Swipe su / giù | sceglie l'icona: Cibo, Gioca, Pulisci, Medicina, Luce, Sgrida, Diario e altro |
| Swipe a destra | conferma; nei sottomenu sceglie (Pasto, Spuntino, Acqua…) |
| Swipe a sinistra · BOOT | indietro / esce |
| Dito tenuto premuto | coccole |
| Scuotere la scheda | lo fa ridere (se dorme lo svegli!) |

Cresce da neonato a bimbo, ragazzo e adulto; la forma adulta (saggio, esploratore, normale, goloso, pasticcione) dipende dalle cure. Fame, sete, felicità, disciplina, peso, inchiostro da pulire, malattie, nanna con la luce: se lo trascuri può morire e si ricomincia da un nuovo uovo.

**Nome, sesso e carattere.** Alla nascita riceve un nome (si cambia da *Diario e altro › Cambia nome*), un cognome di famiglia, il sesso e i suoi geni.

**Cibo.**
- *Pasto*: +1 fame.
- *Acqua*: +1 sete.
- *Spuntino*: non sazia, dà +1 felicità. I primi 3 del giorno non fanno ingrassare, dal quarto sì (e troppi lo rendono goloso). Se lo dai entro 10 minuti da una sgridata meritata (un capriccio) è un **premio**: +2 felicità, +1 disciplina, e non conta come golosità.

**Gioca.**
- *Gioca*: due tiri a palla, +1 felicità senza minigioco (una volta ogni 20 minuti).
- *1, 2, 3 stella*: il polipetto conta girato verso lo scoglio. Tieni premuto (dito o BOOT) e il tuo pesciolino avanza. Quando si gira lascia subito, altrimenti ti vede e si riparte da zero. Hai 9 giri per arrivare; a volte finge di girarsi.
- *Memoria*: ripeti la sequenza di frecce con gli swipe; a ogni giro si allunga (BOOT esce).
- *Ritmo*: tocca lo schermo (o BOOT) quando la bolla entra nel cerchio. Perfetto = 2 punti, bene = 1.
- *Da che parte?*, *Pesca* (inclinando la scheda), *Passeggiata* (contapassi).

I minigiochi danno **conchiglie** e tengono i record. Le conchiglie arrivano anche da 1000 passi, dalle feste, dai compleanni e dalle visite.

**Diario e altro.**
- *Stato*: fame, sete, felicità, disciplina, passi.
- *Diario*: nascita, evoluzioni, malattie, record, acquisti, visite, feste (gli ultimi 30 avvenimenti, con data e ora).
- *Album di famiglia*: la collezione dei colori e delle forme scoperti, il polipetto di adesso e tutti quelli passati, con i loro colori.
- *Famiglia e geni*: genitori, i quattro tratti con i due alleli (vedi sotto) e l'uovo nel nido.
- *Negozio*: cappelli (cappellino, fiocco, cuffia, cappello da festa, cilindro, pirata, corona), accessori (fiore, papillon, sciarpa, occhiali da sole, monocolo) e decorazioni del fondale (stella marina, corallo, anfora, conchiglia gigante, forziere, castello, sottomarino). Destra compra (due volte, per conferma); poi indossa/toglie o espone/ritira.
- *Incontra un amico*: vedi sotto.

**Genetica.** Quattro tratti, ognuno con due alleli (uno dalla mamma, uno dal papà):
- *Colore*: arancione, corallo, viola, azzurro, verde, rosa, oro. L'oro è recessivo e raro.
- *Motivo*: tinta unita, a puntini (dominante), a strisce (recessivo).
- *Tentacoli*: normali, lunghi, corti (recessivi).
- *Carattere*:
  - calmo: si rattrista più piano, meno capricci;
  - vivace: le vittorie valgono doppio, ma si annoia prima;
  - goloso: ha fame più spesso, gli spuntini valgono doppio.

Si vede l'allele dominante (a pari dominanza quello della mamma). Ogni figlio prende a caso un allele per tratto da ciascun genitore, con una piccola probabilità di mutazione. I polipetti dei firmware precedenti restano arancioni a tinta unita.

**Incontra un amico (ESP-NOW).** I Gadget vicini con *Diario e altro › Incontra un amico* aperto si trovano via radio, senza rete. Ognuno vede l'altro polipetto con i suoi colori e vestiti.
- Se vicino ce n'è uno solo compare da sé. Se ce ne sono di più si sceglie da un elenco: su/giù li fa affacciare uno alla volta (nome, sesso, età), destra fa comparire quello scelto. Sinistra torna all'elenco.
- Si gioca, ci si regala e si fanno uova solo fra due polipetti che **si sono scelti a vicenda**. Finché l'altro non ha scelto te, compare un punto di domanda.
- Dopo 5 secondi insieme è una visita: +2 felicità e 3 conchiglie (una volta all'ora per amico). La visita finisce nel diario e l'amico resta nella lista.
- *Su*: regala 5 conchiglie all'altro.
- *Destra* (solo adulti di sesso diverso, con il nido vuoto): propone un uovo. Quando lo propongono tutti e due, **ognuno riceve un uovo** con un allele per tratto da ciascun genitore. L'uovo aspetta nel nido (si vede sul fondo).

**Generazioni.** Dai 25 giorni non invecchia più: vive per sempre, oppure da Impostazioni › Polipetto puoi lasciarlo tornare nell'oceano. Con un uovo nel nido può partire già da adulto. Quando parte:
- se c'è un uovo nel nido, nasce quello;
- altrimenti lascia un uovo suo, della stessa famiglia, con i suoi geni rimescolati.

Chi se ne va finisce nell'album.

**Calendario.**
- Il fondale segue l'ora (alba, giorno, tramonto, notte con il plancton luminoso) e la stagione: neve marina d'inverno, petali in primavera, raggi di sole d'estate, foglie in superficie d'autunno.
- Feste con decorazioni e un regalo di 10 conchiglie una volta l'anno: Capodanno, Befana, San Valentino, Pasqua e Pasquetta, Ferragosto, Giornata del polpo (8 ottobre), Halloween, Natale, San Silvestro.
- Ogni settimana di età è un compleanno, con torta e 5 conchiglie.

### Guida al Polipetto

**I primi giorni**
1. Apri *Polipetto*: c'è un uovo che si schiude in un minuto. Alla nascita il polipetto riceve nome, sesso, cognome e i geni. Il nome si cambia da *Diario e altro › Cambia nome*.
2. Quando l'icona in fondo a destra lampeggia ti sta chiamando. La riga sotto dice cosa vuole: fame, sete, tristezza, malattia, luce da spegnere o un capriccio. Rispondere entro 15 minuti evita gli errori di cura, e meno errori fanno una forma adulta migliore.
3. **Capricci**: quando fa i capricci sgridalo (*Sgrida*). Poi, entro 10 minuti, dagli uno *Spuntino*: è il premio che fissa la lezione e alza la disciplina. Con la disciplina al massimo i capricci finiscono. Sgridarlo senza motivo invece lo rattrista.
4. Se hai poco tempo usa *Gioca › Gioca*: un cuore subito, una volta ogni 20 minuti. Con più tempo i minigiochi danno più cuori e anche conchiglie.

**Guadagnare e spendere le conchiglie**
- Minigiochi: più vai bene più ne prendi. Ogni gioco tiene il suo record, e un record nuovo finisce nel diario.
- Ogni 1000 passi con il contapassi acceso ne trova una.
- Ogni festa ne regala 10 (una volta l'anno), ogni compleanno settimanale 5, ogni visita a un amico 3 (una volta all'ora per amico).
- Si spendono nel *Negozio*: sfoglia con su/giù, l'anteprima mostra il polipetto con l'oggetto o la decorazione al suo posto. Destra due volte per comprare, poi destra per indossare/togliere o esporre/ritirare. Gli oggetti restano tuoi anche per le generazioni successive.

**Incontrare un amico, passo per passo**
1. Su tutti e due i Gadget: *Polipetto › Diario e altro › Incontra un amico*. Il Wi-Fi resta sospeso finché si è lì.
2. Se ci sono più polipetti vicini scegli dall'elenco quello con cui giocare. Anche l'altro padrone deve scegliere il tuo.
3. Dopo 5 secondi insieme è una visita: i polipetti giocano a palla, prendono felicità e 3 conchiglie, e l'amico entra nel diario.
4. *Su* regala 5 conchiglie all'amico scelto (fino a 5 regali per incontro).

**Avere un uovo**
1. Servono due polipetti **adulti** (dai 3 giorni circa) di **sesso diverso**, e tutti e due i nidi vuoti.
2. Nell'incontro, *destra* propone l'uovo. Quando l'hanno proposto tutti e due, ognuno riceve un uovo nel proprio nido. Lo si vede in basso a sinistra nel fondale e in *Famiglia e geni*.
3. L'uovo nasce quando il genitore parte: Impostazioni › Polipetto › *Lascialo tornare nell'oceano*. Con l'uovo nel nido si può già da adulto, senza aspettare i 25 giorni. Il genitore finisce nell'album, il figlio nasce un minuto dopo con la generazione successiva e il cognome della tua famiglia.

**Leggere i geni** (*Famiglia e geni*)
- Ogni pagina è un tratto: in grande quello che si vede, sotto i due alleli (mamma e papà). A sinistra due neonati mostrano com'è ciascun allele.
- Esempio: un polipetto *Viola* con alleli *Viola + Oro* porta l'oro nascosto. Incontrando un altro che porta l'oro, ogni figlio ha 1 probabilità su 4 di nascere oro.
- Lo stesso vale per le strisce e per i tentacoli corti, recessivi. I puntini invece si vedono anche con un solo allele.
- Il carattere non si vede ma si sente: calmo, vivace o goloso cambia un po' come si comporta.

**Generazioni senza amici.** Se parte per l'oceano senza un uovo nel nido, lascia un uovo suo: stessa famiglia, i suoi geni rimescolati (gli alleli nascosti possono venire fuori). Se invece muore per trascuratezza, l'uovo nuovo è selvatico ma tiene il cognome.

**Diario, album e collezione.** Il diario annota tutto da solo. L'album parte dalla *Collezione*, con i colori scoperti (i fantasmini sono quelli che mancano) e le forme adulte raggiunte, poi mostra il polipetto di adesso e quelli passati, ognuno con i suoi colori. Dalla schermata d'addio, su/giù aprono diario e album.

**Feste e stagioni.** Non c'è niente da fare: aprendo l'app nel giorno giusto il polipetto festeggia, con il cappello della festa e le decorazioni, e arriva il regalo. Il fondale cambia da solo con l'ora e la stagione (serve l'ora impostata, via Wi-Fi).

Impostazioni › Polipetto: scorrere del tempo, orari di nanna e sveglia, versi, contapassi, verso dell'inclinazione, **Sull'orologio** (il polipetto passeggia accanto all'ora), partenza per l'oceano, nuovo uovo.
- *Ibrida* (consigliata): tempo reale, anche a scheda spenta, ma negli orari di nanna scelti dorme protetto: nessuna chiamata, nessun errore, nessun pericolo. Senza ora impostata avanza solo a scheda accesa.
- *Tempo reale*, *Solo a scheda accesa*, *Solo con l'app aperta*.

## Q-20
Dal launcher: **Q-20**. Pensa a qualcosa (animale, oggetto, cibo, luogo, persona…) e rispondi alle domande toccando *Sì*, *Forse sì*, *Non so*, *Forse no*, *No*. Dopo una ventina di domande (prima, se è sicuro) prova a indovinare; se sbaglia continua, fino a 30. Se vince lui, ricorda ancora meglio quella cosa; se perde ti chiede cosa pensavi e la impara. Il pulsante ← (o BOOT) torna indietro all'ultima domanda se cambi idea, anche quando sta già provando a indovinare.

- Conosce all'inizio 250 cose e 100 domande (tabella generata da `tools/q20_gen.py`).
- Una risposta sbagliata non lo manda fuori strada: ogni risposta pesa, nessuna elimina.
- **Imparate**: dalla schermata iniziale, l'elenco delle cose che ha imparato da te; swipe a destra (due volte) per fargliene dimenticare una.
- Quello che impara sta in `q20/kb.txt` sulla microSD ed entra nei backup. Senza microSD si gioca, ma non impara.
- Ampliare la conoscenza iniziale: modifica `tools/q20_gen.py` (le domande nuove solo in fondo), lancia `python3 tools/q20_gen.py` e aumenta `SEED_VERSION`: il Gadget aggiunge le cose nuove al suo file senza toccare quello che ha imparato.

## Radar
Dal launcher: **Radar**. Un "radar" Wi-Fi passivo con personalità, in stile pwnagotchi: una faccina che reagisce a quello che sente, livello ed esperienza che crescono scoprendo reti nuove e captando handshake. **Tutto l'ascolto è passivo**: il Gadget non trasmette nulla e non disconnette nessuno.
- **Apri il radar** (faccia e statistiche), **Avvia / ferma**.
- **Pokédex reti**: le reti scoperte, salvate sulla microSD (`pwn/pokedex.dat`, anche nei backup).
- **Impostazioni**: nome, *Cattura pcap* (salva gli handshake su microSD), personalità, stato della microSD, azzera Pokédex e livelli.

Mentre il radar ascolta (modalità promiscua) il Wi-Fi normale è sospeso: la radio fa una cosa per volta.

## Scacchi
Dal launcher: **Scacchi**. Un motore scritto apposta per il Gadget gira sulla scheda, senza Internet. Sullo schermo tondo la scacchiera è più grande, ma si gioca bene anche sul 3,49".

- **Gioca contro il Gadget**: livello da 400 a 2400 Elo, oppure *Adattivo*, che gioca al tuo Elo. Scegli il tuo colore: bianco, nero o a caso.
  - Ai livelli bassi il Gadget sceglie spesso una mossa buona ma non la migliore, come una persona; ai livelli alti cerca fino a 4 secondi per mossa.
  - In apertura segue un piccolo libro di linee principali (spagnola, siciliana, francese, gambetto di donna…).
  - L'Elo del Gadget è indicativo: i livelli sono in ordine di forza, ma non sono tarati su giocatori veri.
- **Comandi sulla scacchiera**:
  - muovi il cursore blu con gli swipe e conferma con BOOT: la prima volta sceglie il pezzo, la seconda la casa. Sul tondo si possono anche toccare il pezzo e la casa; sul 3,49" no, perché il touch a volte registra tocchi fantasma al centro;
  - nella partita a due sul 3,49" a destra c'è il pulsante **Conferma**, che fa come BOOT: entrambi i giocatori muovono senza il tasto fisico;
  - l'ultima mossa è una freccia arancione (pallino sulla casa di partenza, cornice su quella d'arrivo) ed è scritta anche a parole, senza notazione: "Gadget: Cavallo da g8 a f6, scacco". Il suggerimento e la mossa migliore dell'analisi sono frecce blu;
  - i puntini mostrano dove può andare il pezzo scelto; in una promozione scegli il pezzo;
  - il **dito tenuto** apre il menu della partita: suggerimento, annulla mossa, gira la scacchiera, proponi patta (il Gadget accetta solo se sta peggio), abbandona, esci.
- **Riprendi la partita**: una partita interrotta si salva a ogni mossa e si riprende anche dopo aver spento.
- **Partita a due**: sulla stessa scacchiera, senza motore. Si può girare la scacchiera a ogni mossa.
- **Il tuo Elo**: parte da 1200 e cambia dopo ogni partita finita contro il Gadget, con la formula Elo.
  - Le prime 20 partite contano di più (K=40, poi 20).
  - Suggerimenti e mosse annullate tolgono la partita dal conto.
  - La schermata mostra l'Elo, il grafico dell'andamento e vinte, patte e perse.
- **Partite giocate**: tutte le partite finite. Ognuna si rivede mossa per mossa (su/giù).
- **Analisi**: con *destra* il Gadget analizza la partita, circa 0,6 s per posizione (una partita di 40 mosse in meno di un minuto).
  - Ogni mossa riceve un giudizio: migliore, buona, imprecisione, errore, grave errore. Il giudizio dipende da quanta probabilità di vittoria fa perdere, come sui siti di scacchi.
  - Per ogni mossa la valutazione; per ogni errore la mossa migliore. *Destra* salta al tuo prossimo errore, BOOT mostra sulla scacchiera la mossa migliore.
  - Alla fine c'è la precisione di ciascun giocatore. Dopo una partita, *destra* la analizza subito.
- **Allenamento**: le posizioni dove hai commesso un errore o un grave errore (nelle partite analizzate) diventano esercizi: trova la mossa migliore.
  - Se giochi una mossa diversa ma buona quanto quella del Gadget, il Gadget la controlla e la accetta.
  - Gli esercizi sbagliati tornano più spesso.
- **Esporta per un'IA**: scrive `scacchi/per_IA.txt` con istruzioni, riepilogo (Elo e andamento, risultati, aperture che scegli, precisione media) e tutte le partite contro il Gadget in PGN, con i commenti dell'analisi dove c'è. Poi passa al telefono, senza Internet:
  1. inquadra il primo QR: il telefono si collega all'hotspot del Gadget e di solito la pagina si apre da sola (se no, il secondo QR apre http://192.168.4.1);
  2. sulla pagina **Copia tutto** e incolla nella chat di un assistente come Claude, per un'analisi del tuo stile e un piano di allenamento; oppure **Scarica il file**.

  Sul tondo i QR si vedono uno alla volta (su/giù). Su iPhone, nella finestrina che si apre al collegamento i pulsanti possono non funzionare: chiudila restando collegato e apri 192.168.4.1 nel browser. Uscendo dalla schermata l'hotspot si spegne e il Wi-Fi torna com'era.
- **Sulla microSD** (cartella `scacchi/`): `partite/0001.pgn`… (PGN standard, si aprono con qualunque programma di scacchi), `indice.csv`, `elo.csv`, `esercizi.csv`, `incorso.txt`. Senza scheda si gioca lo stesso, ma le partite non restano; l'Elo sì, nella memoria interna.
- **Notazione**: italiana (C A T D R) o inglese (N B R Q K), solo sullo schermo; i file sono sempre in PGN inglese.

## Scanner Wi-Fi e Bluetooth
- **Scanner Wi-Fi**: le reti vicine con banda, potenza (dBm e percentuale), sicurezza e canale; quella a cui sei collegato è segnata. Swipe a destra su una rete per collegarti (con la tastiera per la password); il dito tenuto rifà la scansione. È la stessa schermata di Impostazioni › Wi-Fi › Cerca reti e funziona anche mentre sei connesso.
- **Scanner Bluetooth**: i dispositivi Bluetooth LE nei paraggi; swipe a destra per collegarti a uno collegabile e vederne i servizi, dito tenuto (o BOOT tenuto) per una nuova scansione. Si apre anche da Impostazioni › Bluetooth.

## Sismografo
Dal launcher: **Sismografo** → *Avvia*. Appoggia il Gadget su una superficie ferma (tavolo, pavimento): l'accelerometro misura le vibrazioni 200 volte al secondo e, tolta la gravità, il grafico mostra l'asse verticale con la scala che si adatta da sola. In alto la vibrazione attuale e il picco in mg (millesimi di g), con un'intensità locale stimata sulla scala Mercalli (Wald 1999: è quella che sente il Gadget, non quella del terremoto). BOOT azzera il picco, swipe a destra apre gli eventi. Lo schermo può spegnersi: le letture continuano finché l'app è aperta.

- **Eventi**: dopo 20 s di calibrazione, un evento scatta quando la media breve dell'energia (STA, 0,5 s) supera di 3,5–5 volte quella lunga (LTA, 20 s) e la vibrazione supera la soglia scelta (metodo STA/LTA dei sismografi). Finisce dopo 2 s di calma (al massimo 2 minuti).
- **Sensibilità**: alta (anche vibrazioni deboli), media, bassa (solo scosse forti). I colpi secchi e brevi (un passo, una porta) di solito non bastano: serve una vibrazione che duri almeno qualche decimo di secondo.
- **Registra sulla microSD**: ogni evento, con 5 s prima e 5 s dopo, va in `sismo/AAAAMMGG-hhmmss.csv` (100 campioni al secondo, x/y/z in mg: si apre con Excel); il registro di tutti è `sismo/eventi.csv`. Dal menu si consultano gli ultimi 50 e si cancellano.

## Snake
Dal launcher: **Snake**, in pixel art con i colori del Polipetto: un serpente di quadratini arancioni (la testa ha gli occhi) che mangia i pesci gialli. Con gli swipe nelle quattro direzioni si gira: la svolta parte appena il dito si sposta, senza aspettare che si stacchi, e un solo gesto a "L" (per esempio giù e poi a sinistra, senza alzare il dito) fa l'inversione a U su due quadratini di fila. Fino a tre svolte restano in coda; muri e coda fanno perdere, e ogni pesce lo rende un po' più veloce. Lo schermo resta tutto per il gioco: sul 3,49" una griglia 53×12, sul tondo 20×20 con quadratini più grandi.

- **Partenza**: uno swipe qualsiasi (sinistra esce).
- **Pausa**: BOOT o dito tenuto. In pausa e a fine partita: destra riprende o ricomincia, sinistra esce.
- **Record**: resta in memoria anche spegnendo.

## Spada laser
Dal launcher: **Spada laser** › *Accendi*. BOOT accende e spegne la lama; i suoni sono sintetizzati in tempo reale (nessun campione registrato) e seguono i movimenti dal giroscopio:
- **swing** muovendo la scheda, **scontro** con un colpo secco, **affondo**, **tocco** sullo schermo = colpo di blaster, **dito tenuto** = lockup.
- Nel menu: **Colore** della lama, **Volume** (lo stesso di Impostazioni › Audio), **Sensibilità scontro** (bassa, media, alta).

## Tester Wi-Fi
Dal launcher: **Tester Wi-Fi**. Per controllare la copertura di una rete girando per le stanze.

- **Elenco**: le reti vicine, dalla più forte, una voce per nome anche se la rete esce da più apparecchi (router, ripetitori, mesh). Sotto ogni voce: potenza, giudizio, numero di apparecchi e canali. Swipe a destra apre la misura, il dito tenuto rifà la scansione.
- **Misura**: la potenza in dBm grande, il giudizio (Ottimo ≥ -55, Buono ≥ -65, Discreto ≥ -72, Scarso ≥ -80, sotto Pessimo) e un indicatore che sale e scende con il segnale (sul 3,49" una barra a sinistra, sul tondo due archi sui lati). Il grafico mostra gli ultimi 30 secondi.
- **Più apparecchi**: se la rete esce da più apparecchi, il Gadget scrive quale arriva meglio dove sei (AP 1, AP 2… con le ultime cifre dell'indirizzo e il canale) e la potenza degli altri. Il valore grande è quello del migliore.
- **Bip**: swipe a destra o BOOT accende un bip a ogni misura, più acuto quando il segnale è più forte, per camminare senza guardare lo schermo.
- **Come misura**: scansiona solo quella rete e solo sui suoi canali, così aggiorna 3-5 volte al secondo; ogni 12 misure guarda tutti i canali, per trovare apparecchi nuovi. Il Gadget riceve solo i 2,4 GHz: le reti a 5 GHz non si vedono. Non salva nulla.

## Theremin
Dal launcher: **Theremin**. Inclina la scheda avanti/indietro per cambiare la nota, ruotala a destra/sinistra per il volume (a sinistra fino al silenzio). Suona finché tieni il dito sulla zona grande a sinistra; **BOOT** imposta la posizione zero (la nota a metà dell'estensione). Dal menu Comandi puoi scambiare i due movimenti.

Pulsanti a destra:
- **Suono**: forma d'onda (Theremin, Sinusoide, Triangolo, Dente di sega, Quadra), estensione (nota più bassa e più alta, fino a quella del pianoforte: La0 – Do8, sposta di un'ottava, ripristino a La3 – La5), glide, vibrato e sua velocità, volume.
- **Scala**: libera (glissando, come il theremin vero) oppure agganciata a cromatica, maggiore, minore, pentatonica o blues, con la tonica a scelta. Il LA di riferimento è quello dell'Accordatore.
- **Effetti**: eco (corta, media, lunga) e ripetizioni, timbro (da scuro a brillante).
- **Comandi**: quale movimento cambia la nota (inclinazione o rotazione), cosa fa l'altro (volume, vibrato, timbro o niente: volume fisso, quello del menu Suono), quanti gradi servono per tutte le note e per l'altro movimento (da 20° a 180°, cioè da rivolta in su a rivolta in giù), inversione dei due movimenti, "suona sempre", volume del microfono.
- **Registra**: registra quello che suoni in un file WAV nella cartella `theremin` della microSD. Con **Mic: sì** mixa anche il microfono, per cantare mentre suoni (il microfono sente anche l'altoparlante).
- **Registrazioni**: elenco per riascoltarle (swipe a destra) o cancellarle (BOOT due volte).

## Torcia
Dal launcher: **Torcia** (o come *azione rapida*, tenendo il dito). Lo schermo diventa una luce piena alla massima luminosità: destra accende e spegne, su/giù cambia colore (bianca, calda, rossa per la visione notturna, verde…).

## Impostazioni
Dal launcher: **Impostazioni**.
- **Wi-Fi** e **Bluetooth**: vedi sotto. Wi-Fi e Bluetooth non sono mai accesi insieme.
- **Schermo**: luminosità, spegnimento automatico, ruota di 180°, inverti lo scorrimento (su/giù), colore d'accento.
- **Audio**: prova audio e volume (vedi [Audio](#audio)).
- **Azione rapida**: cosa fa il dito tenuto (o BOOT tenuto) fuori dalle app che lo usano: niente, Spegni schermo, una qualsiasi app del launcher oppure una sua schermata interna (es. "Accordatore » Cromatico", "Morse » Telegrafo"). L'elenco si costruisce da solo dal launcher, quindi ogni app nuova compare senza modifiche; la scelta si salva per nome.
- **App all'avvio**: vedi sotto.
- **Polipetto**: vedi [Polipetto](#polipetto).
- **Backup e ripristino**, **Data e ora**: vedi sotto.
- **Sistema**: batteria, memoria libera (RAM interna e PSRAM), versione del firmware, [aggiornamento](#aggiornamenti) e controllo automatico degli aggiornamenti, tempo da quando è acceso, riavvia, spegni, ripristina le impostazioni.

### Wi-Fi
Impostazioni › Wi-Fi › **Cerca reti e collegati**: elenco delle reti vicine, swipe a destra per collegarti (con la tastiera per la password). È la stessa schermata dello Scanner Wi-Fi, che funziona anche mentre sei connesso. Tocco prolungato: nuova scansione. In alternativa "Configura dal telefono" apre un hotspot con una pagina web.

### Bluetooth
**Wi-Fi e Bluetooth non sono mai accesi insieme**: l'ESP32-S3 ha una sola radio e con tutti e due attivi il Wi-Fi diventa lento e instabile. Di predefinito è acceso il Wi-Fi; accendere il Bluetooth (anche collegando un dispositivo) spegne il Wi-Fi, e viceversa. *Appunti › Ricevi dal PC* usa il Bluetooth solo finché è aperta, poi torna al Wi-Fi. La schermata di aggiornamento riaccende da sola il Wi-Fi. Gli scanner funzionano comunque, finché sono aperti.

Il Bluetooth è predisposto per collegarsi nei due sensi (le funzioni vere arriveranno sopra):
- **Collega a telefono o computer** (il Gadget è l'accessorio): per 2 minuti il Gadget si fa trovare col suo nome; sceglilo nelle impostazioni Bluetooth dell'altro dispositivo. Se l'altro ha una tastiera, sul Gadget compare un codice da digitare. Dopo l'associazione si ricollega da solo; nessun altro può associarsi a finestra chiusa. Base per la futura tastiera Bluetooth. Nota: iPhone elenca nelle impostazioni solo accessori di tipo noto (tastiera, cuffie…), quindi il Gadget comparirà lì quando avrà il profilo tastiera.
- **Collega un dispositivo** (il Gadget comanda): scansione, swipe a destra su un dispositivo collegabile per collegarti e vedere i servizi che offre (batteria, battito cardiaco, tastiera…).
- **Dispositivi**: collegamenti attivi e dispositivi associati; swipe a destra (due volte) per scollegare o dimenticare.

Limite dell'hardware: l'ESP32-S3 ha solo il Bluetooth Low Energy. Casse e cuffie riproducono l'audio col Bluetooth "classico" (A2DP), che questo chip non ha: per l'audio servirebbe un modulo esterno.

### Audio
Impostazioni › **Audio**: *Prova audio* suona tre note (do-mi-sol) e accanto dice com'è andato l'avvio dell'audio: "Pronto" (con i microfoni ok o assenti), oppure "ES8311 non risponde" / "I2S occupato" se qualcosa non va, e se l'uscita è occupata da un'app. *Volume* è lo stesso della Spada laser e vale per tutte le app (Theremin, Polipetto, scacchi, Doom).

### Data e ora
Impostazioni › **Data e ora**: ora e data, *Sincronizza ora* (da Internet, NTP), fuso orario, e **Orologio della scheda**, che dice se l'orologio interno (RTC PCF85063) risponde, se ha l'ora e com'era all'accensione.

Sulla scheda 3.49 V1 l'orologio interno non ha una batteria tampone: da spenta l'ora si perde ("all'accensione era fermo") e torna appena il Gadget si collega al Wi-Fi. Finché resta acceso, anche a batteria, l'ora rimane.

### Backup e ripristino
Impostazioni › Backup e ripristino:
- **Crea un backup ora**: salva in un file della cartella `backup` sulla microSD tutto quello che scegli o crei:
  - dalla memoria interna: impostazioni e Wi-Fi, Polipetto (con conchiglie, oggetti, diario, album e amici), livella, Radar, Theremin, Scacchi (impostazioni ed Elo), Sismografo, gli Appunti ricevuti, il record di Snake, il corso Morse;
  - dalla microSD, cartelle intere: `pwn/` (Pokédex e catture del Radar), `q20/` (quello che ha imparato), `scacchi/` (partite, Elo, esercizi), `sismo/` (eventi), `theremin/` (registrazioni), `doom/` (salvataggi e configurazione; i WAD no, si ricopiano a parte).

  Con molte registrazioni il backup diventa grande e ci mette un po'.
- **Ripristina un backup**: elenca i backup; si possono usare solo quelli fatti con questa versione del firmware o con una più vecchia. Da un backup vecchio le impostazioni nuove prendono il valore predefinito e quelle che non esistono più si ignorano. Dopo il ripristino la scheda si riavvia.
- **Backup prima degli aggiornamenti**: prima di ogni aggiornamento OTA ne crea uno da solo.

Il ripristino riscrive i file contenuti nel backup e non cancella niente sulla microSD: una partita o una registrazione fatta dopo il backup resta.

Il file è di testo con un controllo CRC: un backup rovinato o incompleto viene rifiutato senza toccare niente. Contiene anche la password del Wi-Fi in chiaro, quindi tienilo al sicuro.

### App all'avvio
Impostazioni › App all'avvio: se scegli un'app (o una sua schermata interna, stesso elenco automatico dell'Azione rapida), all'accensione si apre quella; swipe indietro torna al menu. "Nessuna" lascia tutto com'era. Le scelte fatte con i firmware precedenti vengono convertite da sole.

## Aggiornamenti
- **Dalla scheda (OTA):** Impostazioni › Sistema › Aggiornamento firmware. Ogni merge su `main` pubblica una release su GitHub con il firmware; la scheda la scarica via Wi-Fi e si riavvia. Se il nuovo firmware non riesce ad avviarsi, al riavvio torna da solo quello precedente. Mentre sei nella schermata di aggiornamento il Bluetooth va in pausa (la radio è condivisa col Wi-Fi) e si riaccende uscendo. Con "Cerca aggiornamenti da solo" avvisa quando ne esce uno nuovo. Se il controllo fallisce due volte, il Gadget ricollega da capo il Wi-Fi (indirizzo e DNS nuovi, con un DNS di riserva) e riprova. Sotto l'errore compare il motivo (DNS, connessione rifiutata, nessuna risposta, tempo scaduto, con il codice); nel log seriale c'è il dettaglio.
- **Via USB:** `gadget.bin` a `0x0` (immagine unica) oppure solo `gadget-app.bin` a `0x20000`.

Dalla 0.14 le impostazioni (NVS) stanno in fondo alla flash (`0xFF0000`): né l'OTA né i file qui sopra le toccano più.
**Passaggio dalla 0.13 (una volta sola):** la tabella delle partizioni cambia, quindi serve un flash via USB. Con l'immagine unica le impostazioni vecchie si perdono (stavano proprio nella zona che l'immagine riempie). Per conservarle, flasha invece i file separati della release/artifact: `bootloader.bin` a `0x0`, `partition-table.bin` a `0x8000`, `ota_data_initial.bin` a `0xf000`, `gadget-app.bin` a `0x20000`. Al primo avvio vengono copiate nella nuova posizione.

## Compilare
1. Installa ESP-IDF **5.4.x** (estensione ESP-IDF di VS Code o installer ufficiale).
2. Scarica LVGL nella cartella dei componenti:
   `git clone -b v9.2.2 --depth 1 https://github.com/lvgl/lvgl components/lvgl`
3. `idf.py set-target esp32s3` e poi `idf.py build flash monitor`.

Immagine unica per il flasher web (indirizzo 0x0), come fa la build su GitHub:
`esptool.py --chip esp32s3 merge_bin -o gadget.bin --flash_mode dio --flash_size 16MB 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xf000 build/ota_data_initial.bin 0x20000 build/gadget.bin`

## Prove sul PC
Senza la scheda si possono provare parecchie cose sul PC:
- **Simulatore dell'interfaccia** (`tools/sim/run.sh`): compila LVGL e le schermate con gcc e salva le immagini in `tools/sim/png/`, sia per il 3,49" (`349_*`) sia per il tondo (`tondo_*`). Le scene (in `sim_main.c`) percorrono menu, Tester Wi-Fi, Sismografo, Snake, Scacchi e Polipetto (`run.sh pet` fa solo il Polipetto); la "microSD" è la cartella `/sdcard` del PC. Serve prima un `idf.py build` (per `build/config/sdkconfig.h`) e Python con Pillow.
- **Polipetto** (`tools/pet/pet_test.c`): genetica (dominanza, eredità, mutazioni), spuntino come premio, "Gioca" veloce, salvataggi vecchi, partenza con l'uovo nel nido.
- **Morse** (`tools/morse/morse_test.c`): il testo diventa audio con rumore a varie velocità, frequenze e volumi, e il rilevatore con la decodifica devono restituirlo; prova anche una battuta a mano irregolare.
- **Motore degli Scacchi** (`tools/chess/chess_test.c`): perft su posizioni di riferimento, notazione, libro delle aperture, problemi tattici e partite fra livelli; `tools/chess/store_test.c` prova l'archivio (salvataggio, analisi, rilettura PGN, Elo, esportazione). I comandi sono in cima ai file.

## Monitor seriale
Il firmware conserva in RAM gli ultimi 16 KB di log dall'accensione. Dal monitor seriale (115200) invia:
`L` ristampa il log dall'accensione · `P` log della sessione prima dell'ultimo riavvio (dopo un crash o un watchdog) · `I` info di sistema · `R` riavvio software · `H` aiuto.

**Cicli di riavvii.** Se la scheda si riavvia per un errore mentre il Bluetooth è acceso, al riavvio il Bluetooth resta spento (con un avviso). Dopo tre riavvii per errore di fila parte in *modalità sicura*: Bluetooth spento e niente app all'avvio. Il comando `P` mostra il log di quello che è successo prima del riavvio.

Se il touch smette di rispondere (bus I2C bloccato, per esempio dopo un riavvio software a metà di una lettura), il firmware lo sblocca e ricrea il bus da solo entro mezzo secondo. `I` dice quante volte è successo e quante letture del touch sono andate a buon fine, senza dito, non valide o in errore (con gli ultimi byte strani ricevuti); il log riporta il motivo.

## Struttura
- `main/board.*` riconoscimento della scheda, pin, alimentazione (3.49: latch TCA9554 EXIO6 e batteria sull'ADC; AMOLED: AXP2101), RTC PCF85063, IMU QMI8658, pulsanti
- `main/display.*` dimensioni dello schermo, pannello AXS15231B in QSPI (rotazione software), task LVGL · `main/display_round.*` AMOLED CO5300 466×466 (invio delle sole zone cambiate)
- `main/fonts_sel.c` caratteri per la scheda (`main/fonts/font_*_r.c` per il tondo)
- `main/input.*` touch (AXS15231B o CST9217) + riconoscimento gesti + pulsanti → eventi `nav_t`
- `main/ui.*` pila di schermate, barra di stato, spegnimento schermo, azione rapida, `list_view`
- `main/menu.c` menu generico (usato da home e impostazioni)
- `main/wifi_mgr.*` Wi-Fi, scansione, portale captive per configurare la rete dal telefono, NTP
- `main/ble_mgr.*` NimBLE: visibilità, scansione, collegamenti nei due sensi, associazioni, profili · `main/apps/app_bt.c` le schermate
- `main/pet_core.*` regole del Polipetto e genetica (pure, senza hardware) · `main/pet.*` tempo reale, salvataggio, contapassi, versi, conchiglie, diario, album, amici, calendario delle feste
- `main/apps/app_pet.c` l'app del Polipetto · `pet_ui.h` i moduli: `pet_games.c` (1-2-3 stella, Memoria, Ritmo), `pet_book.c` (diario, album, famiglia, negozio), `pet_social.c` (visite ESP-NOW), `pet_mini.c` (sull'orologio) · `pet_art.c` pixel art, colori dei geni, cappelli, decorazioni, fondale
- `main/clips.*` Appunti (testi dal PC) · `main/clip_ble.c` servizio Bluetooth di ricezione · `main/usbhid.*` tastiera USB · `main/apps/app_clips.c` le schermate
- `main/backup.*` backup e ripristino su microSD · `main/ota.*` aggiornamenti via internet · `main/settings.*` impostazioni (con migrazione dalla vecchia tabella)
- `main/chess_engine.*` regole e motore degli Scacchi · `main/chess_game.*` partite, analisi, Elo, archivio PGN · `main/apps/chess_ui.c` scacchiera · `main/apps/app_chessplay.c` le schermate · `tools/chess/` prove sul PC (perft, notazione, livelli, archivio)
- `main/apps/app_share.c` "Sul telefono": hotspot + QR per passare un file di testo al telefono (Copia tutto / Scarica)
- `main/morse.*` codice Morse: tabella, tempi, decodifica, rilevatore del tono (prove in `tools/morse/`) · `main/apps/app_morse.c` corso, Telegrafo ESP-NOW (`wifi_mgr_espnow_*`), ascolto
- `main/seismo.*` acquisizione e rilevamento degli eventi del Sismografo · `main/audio.*` codec ES8311 e microfoni ES7210 (con la diagnostica) · `main/theremin.*` sintesi e registrazione del Theremin
- `main/apps/` le app (`app_pet.c` + `pet_art.c` per il Polipetto, `app_ota.c` per gli aggiornamenti, `app_backup.c`, `app_level.c` per la livella, `app_wifitest.c` il Tester Wi-Fi, `app_snake.c` lo Snake, `app_seismo.c` il Sismografo)
- `components/axs15231b` driver Waveshare inclusi nel progetto
- `tools/` simulatore (`sim/`), prove degli Scacchi (`chess/`), generatori dei font tondi e del Q-20

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
