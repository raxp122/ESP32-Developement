#!/usr/bin/env python3
"""q20_gen.py — genera main/q20_seed.c, la conoscenza iniziale del gioco Q-20.

Ogni cosa è descritta da etichette (le chiavi delle domande):
    chiave   = sì     ~chiave = a volte / dipende     !chiave = no (anche se una regola dice sì)
Le regole IMPLICA completano il resto (mammifero → animale → vivo …). Tutto ciò che non è
detto vale "no".

Le domande si possono solo AGGIUNGERE IN FONDO (il file sulla microSD le indicizza per
posizione). Dopo una modifica: python3 tools/q20_gen.py, e aumenta SEED_VERSION se hai
aggiunto cose nuove (il Gadget le aggiunge al suo file senza toccare quello che ha imparato).
"""
import os, sys

SEED_VERSION = 1

QUESTIONS = [  # (chiave, testo) — solo in fondo!
    ("vivo", "È un essere vivente?"),
    ("animale", "È un animale?"),
    ("persona", "È una persona o un mestiere?"),
    ("mammifero", "È un mammifero?"),
    ("uccello", "È un uccello?"),
    ("pesce", "È un pesce?"),
    ("rettile", "È un rettile o un anfibio?"),
    ("insetto", "È un insetto o un piccolo invertebrato?"),
    ("pianta", "È una pianta, un fiore o un albero?"),
    ("cibo", "Si mangia?"),
    ("frutto", "È un frutto?"),
    ("verdura", "È una verdura o un ortaggio?"),
    ("dolce", "È un dolce?"),
    ("bevanda", "È una bevanda?"),
    ("artificiale", "È fatto dall'uomo?"),
    ("veicolo", "È un mezzo di trasporto?"),
    ("luogo", "È un luogo o un edificio?"),
    ("musica", "È uno strumento musicale?"),
    ("elettronico", "È un apparecchio elettrico o elettronico?"),
    ("corrente", "Funziona con la corrente o le batterie?"),
    ("cucina", "Si usa o si trova in cucina?"),
    ("mobile", "È un mobile?"),
    ("attrezzo", "È un attrezzo o un utensile?"),
    ("indossa", "Si indossa?"),
    ("gioco", "Si usa per giocare o fare sport?"),
    ("casa", "Si trova di solito in casa?"),
    ("natura", "Si trova in natura?"),
    ("mano", "Sta in una mano?"),
    ("grande", "È più grande di una persona?"),
    ("enorme", "È più grande di un'automobile?"),
    ("acqua", "Vive nell'acqua?"),
    ("vola", "Vola?"),
    ("zampe4", "Ha quattro zampe?"),
    ("pelo", "Ha il pelo?"),
    ("piume", "Ha le piume?"),
    ("squame", "Ha le squame?"),
    ("domestico", "È un animale domestico?"),
    ("fattoria", "Vive in una fattoria?"),
    ("pericoloso", "È pericoloso?"),
    ("carnivoro", "Mangia altri animali?"),
    ("africa", "Vive in Africa?"),
    ("italia", "Vive libero in Italia?"),
    ("coda", "Ha la coda?"),
    ("moltezampe", "Ha più di quattro zampe?"),
    ("notturno", "È attivo soprattutto di notte?"),
    ("cavalcare", "Si può cavalcare?"),
    ("uova", "Depone le uova?"),
    ("metallo", "È di metallo?"),
    ("legno", "È di legno?"),
    ("plastica", "È di plastica?"),
    ("vetro", "È di vetro o di ceramica?"),
    ("stoffa", "È di stoffa?"),
    ("carta", "È di carta?"),
    ("ruote", "Ha le ruote?"),
    ("motore", "Ha un motore?"),
    ("schermo", "Ha uno schermo?"),
    ("suona", "Fa rumore o suona?"),
    ("morbido", "È morbido?"),
    ("tagliente", "È appuntito o tagliente?"),
    ("fragile", "Si rompe facilmente?"),
    ("liquidi", "Contiene un liquido?"),
    ("verde", "Di solito è verde?"),
    ("rosso", "Di solito è rosso?"),
    ("giallo", "Di solito è giallo o arancione?"),
    ("bianco", "Di solito è bianco?"),
    ("nero", "Di solito è nero o molto scuro?"),
    ("marrone", "Di solito è marrone?"),
    ("cotto", "Si mangia cotto?"),
    ("sapdolce", "Ha un sapore dolce?"),
    ("salato", "Ha un sapore salato?"),
    ("latte", "Contiene latte o formaggio?"),
    ("colazione", "Si mangia o si beve a colazione?"),
    ("italiano", "È tipico della cucina italiana?"),
    ("albero", "Cresce sugli alberi?"),
    ("buccia", "Ha una buccia da togliere?"),
    ("caldo", "Si mangia o si beve caldo?"),
    ("alcol", "È alcolico?"),
    ("passeggeri", "Trasporta molte persone?"),
    ("citta", "Si trova in città?"),
    ("salute", "Ha a che fare con la salute o l'ospedale?"),
    ("divisa", "Porta una divisa?"),
    ("scuola", "Si usa a scuola o in ufficio?"),
    ("bagno", "Si usa in bagno?"),
    ("reale", "Esiste davvero (non è immaginario)?"),
    ("inverno", "Si usa o si vede soprattutto d'inverno?"),
    ("mare", "Si trova al mare o in spiaggia?"),
    ("naviga", "Va sull'acqua?"),
    ("aperto", "Sta soprattutto all'aperto?"),
    ("famoso", "È famoso per qualcosa in particolare?"),
    ("caro", "Costa molto?"),
    ("leggere", "Si legge?"),
    ("pulire", "Serve a pulire?"),
    ("collo", "Si porta al collo?"),
    ("soldi", "Ha a che fare con i soldi?"),
    ("posta", "Si spedisce per posta?"),
    ("abitare", "Ci si abita?"),
    ("religione", "Ha a che fare con la religione?"),
    ("spesa", "Ci si va a fare acquisti?"),
    ("viaggio", "Ha a che fare con i viaggi?"),
    ("minuscolo", "È più piccolo di una noce?"),
]

IMPLICA = {
    "mammifero": ["animale"], "uccello": ["animale", "piume", "uova", "vola", "coda"],
    "pesce": ["animale", "acqua", "squame", "uova", "coda"],
    "rettile": ["animale", "uova", "squame", "coda"], "insetto": ["animale", "moltezampe", "uova", "mano"],
    "animale": ["vivo"], "persona": ["vivo"], "pianta": ["vivo", "natura"],
    "frutto": ["cibo", "~pianta", "natura"], "verdura": ["cibo", "~pianta", "natura"],
    "dolce": ["cibo", "sapdolce", "artificiale"], "bevanda": ["liquidi"],
    "veicolo": ["artificiale"], "musica": ["artificiale", "suona"], "elettronico": ["artificiale", "corrente"],
    "mobile": ["artificiale", "casa"], "attrezzo": ["artificiale"], "indossa": ["artificiale"],
    "enorme": ["grande"],
}

C = """
# ---------------- mammiferi ----------------
cane: mammifero domestico zampe4 pelo coda ~carnivoro ~pericoloso casa citta ~nero ~marrone ~bianco suona famoso
gatto: mammifero domestico zampe4 pelo coda carnivoro notturno casa citta morbido ~nero ~bianco ~giallo
cavallo: mammifero zampe4 pelo coda fattoria cavalcare grande marrone aperto ~nero ~bianco
mucca: mammifero zampe4 pelo coda fattoria grande bianco nero aperto
maiale: mammifero zampe4 pelo coda fattoria ~cibo
pecora: mammifero zampe4 pelo coda fattoria bianco morbido aperto
capra: mammifero zampe4 pelo coda fattoria ~italia aperto ~bianco
asino: mammifero zampe4 pelo coda fattoria cavalcare ~grande suona aperto
coniglio: mammifero ~domestico zampe4 pelo coda ~fattoria italia morbido ~casa ~bianco ~marrone
topo: mammifero zampe4 pelo coda mano notturno italia citta ~casa
criceto: mammifero domestico zampe4 pelo mano morbido casa ~marrone
leone: mammifero zampe4 pelo coda carnivoro pericoloso africa grande giallo famoso aperto
tigre: mammifero zampe4 pelo coda carnivoro pericoloso grande giallo nero aperto
elefante: mammifero zampe4 coda enorme africa ~cavalcare aperto famoso
giraffa: mammifero zampe4 pelo coda enorme africa giallo aperto
zebra: mammifero zampe4 pelo coda grande africa bianco nero aperto
scimmia: mammifero pelo coda africa ~pericoloso marrone ~zampe4 aperto
orso: mammifero zampe4 pelo carnivoro pericoloso grande marrone ~italia aperto
lupo: mammifero zampe4 pelo coda carnivoro pericoloso italia notturno aperto ~nero
volpe: mammifero zampe4 pelo coda carnivoro italia notturno giallo ~rosso aperto
cervo: mammifero zampe4 pelo coda italia grande marrone aperto
cinghiale: mammifero zampe4 pelo coda italia ~pericoloso marrone nero aperto
riccio: mammifero zampe4 italia notturno mano tagliente marrone aperto
scoiattolo: mammifero zampe4 pelo coda italia ~mano marrone ~rosso aperto
pipistrello: mammifero vola notturno italia nero ~mano ~pelo
delfino: mammifero !pelo coda acqua grande mare famoso
balena: mammifero !pelo coda acqua enorme mare ~nero famoso
foca: mammifero coda acqua ~grande mare ~nero
cammello: mammifero zampe4 pelo coda africa grande cavalcare marrone aperto
canguro: mammifero pelo coda ~grande marrone aperto famoso
panda: mammifero zampe4 pelo grande bianco nero morbido famoso
ippopotamo: mammifero zampe4 enorme africa pericoloso ~acqua aperto
rinoceronte: mammifero zampe4 coda enorme africa pericoloso aperto
koala: mammifero pelo morbido ~mano aperto
# ---------------- uccelli ----------------
gallina: uccello ~vola fattoria ~bianco ~marrone aperto
gallo: uccello ~vola fattoria suona ~rosso aperto
anatra: uccello ~acqua fattoria italia suona aperto
aquila: uccello carnivoro italia ~pericoloso marrone aperto famoso
gufo: uccello notturno carnivoro italia marrone aperto
pappagallo: uccello domestico verde ~rosso ~giallo suona casa
pinguino: uccello !vola acqua bianco nero famoso
struzzo: uccello !vola africa grande aperto
piccione: uccello citta italia ~bianco aperto
passero: uccello italia mano citta marrone aperto
cigno: uccello acqua italia bianco aperto
gabbiano: uccello mare bianco italia aperto suona
# ---------------- acqua ----------------
pesce rosso: pesce domestico mano giallo rosso casa
squalo: pesce carnivoro pericoloso grande mare famoso
tonno: pesce mare ~grande cibo
salmone: pesce ~rosso cibo cotto
polpo: animale acqua mare moltezampe carnivoro cibo
granchio: animale acqua mare moltezampe ~rosso cibo ~tagliente ~mano
medusa: animale acqua mare ~pericoloso
stella marina: animale acqua mare ~mano ~giallo ~rosso
# ---------------- rettili e anfibi ----------------
coccodrillo: rettile zampe4 carnivoro pericoloso grande africa acqua verde aperto
serpente: rettile ~pericoloso carnivoro italia ~verde aperto
tartaruga: rettile zampe4 ~domestico ~acqua ~mano verde ~marrone
lucertola: rettile zampe4 italia mano verde aperto
rana: rettile !squame zampe4 acqua italia verde mano suona
dinosauro: rettile ~reale enorme ~carnivoro ~pericoloso coda zampe4 famoso
drago: rettile !reale vola pericoloso enorme ~verde ~rosso famoso
# ---------------- insetti e piccoli animali ----------------
ape: insetto vola italia giallo nero ~pericoloso aperto minuscolo
formica: insetto italia nero ~casa aperto minuscolo
farfalla: insetto vola italia aperto
zanzara: insetto vola notturno italia ~casa minuscolo
mosca: insetto vola ~casa nero minuscolo
ragno: insetto notturno ~casa ~pericoloso nero ~minuscolo
lumaca: insetto !moltezampe italia aperto ~cibo marrone
coccinella: insetto vola italia rosso nero aperto minuscolo
lombrico: insetto !moltezampe !uova aperto marrone
# ---------------- immaginari ----------------
unicorno: mammifero !reale zampe4 pelo coda bianco cavalcare famoso
fantasma: !reale ~vola notturno bianco
# ---------------- persone ----------------
medico: persona salute ~divisa citta bianco
infermiere: persona salute divisa citta
pompiere: persona divisa ~pericoloso citta
poliziotto: persona divisa citta aperto
cuoco: persona cucina ~divisa bianco
insegnante: persona scuola citta
contadino: persona fattoria aperto natura
astronauta: persona divisa ~vola famoso caro
pilota: persona divisa vola
calciatore: persona gioco divisa aperto famoso
cantante: persona suona famoso
bambino: persona gioco casa
re: persona famoso caro
babbo natale: persona !reale inverno rosso bianco ~vola famoso
strega: persona !reale ~vola nero
# ---------------- frutta ----------------
mela: frutto albero rosso ~verde mano sapdolce colazione
pera: frutto albero verde ~giallo mano sapdolce
banana: frutto giallo buccia mano sapdolce colazione
arancia: frutto albero giallo buccia mano sapdolce inverno italiano
limone: frutto albero giallo buccia mano italiano
fragola: frutto rosso mano sapdolce
uva: frutto verde ~nero mano sapdolce italiano ~minuscolo
anguria: frutto verde rosso sapdolce buccia
ciliegia: frutto albero rosso mano sapdolce ~minuscolo
pesca: frutto albero giallo ~rosso mano sapdolce
ananas: frutto giallo buccia sapdolce
kiwi: frutto marrone verde buccia mano sapdolce
noce di cocco: frutto albero marrone bianco buccia mano
mandarino: frutto albero giallo buccia mano sapdolce inverno
# ---------------- verdure ----------------
carota: verdura giallo mano ~cotto
pomodoro: verdura rosso mano ~cotto italiano
patata: verdura marrone mano cotto buccia
insalata: verdura verde
cipolla: verdura bianco mano ~cotto buccia
zucchina: verdura verde mano cotto
melanzana: verdura nero cotto mano
peperone: verdura rosso ~verde ~giallo ~cotto mano
fungo: verdura ~pianta natura marrone ~pericoloso cotto mano
aglio: verdura bianco mano ~cotto buccia italiano
piselli: verdura verde cotto mano minuscolo
# ---------------- cibi ----------------
pizza: cibo cotto salato latte caldo italiano ~artificiale famoso
pasta: cibo cotto caldo italiano ~artificiale ~giallo
lasagna: cibo cotto latte caldo italiano ~artificiale
gelato: dolce latte mano ~colazione !caldo
torta: dolce latte ~colazione cotto
biscotto: dolce colazione mano cotto marrone
cioccolato: dolce marrone mano ~latte
pane: cibo cotto colazione ~salato marrone ~artificiale
formaggio: cibo latte salato ~giallo ~bianco italiano ~artificiale
uovo: cibo colazione cotto bianco mano fragile
panino: cibo salato mano ~artificiale
patatine: cibo salato giallo mano cotto ~artificiale
hamburger: cibo cotto salato caldo ~artificiale marrone
cornetto: dolce colazione mano cotto italiano
miele: cibo sapdolce giallo colazione ~liquidi
prosciutto: cibo salato rosso italiano ~artificiale
pollo: cibo cotto caldo
tiramisù: dolce latte italiano marrone
caramella: dolce mano ~artificiale ~minuscolo
popcorn: cibo salato mano cotto ~sapdolce bianco
# ---------------- bevande ----------------
acqua: bevanda natura
latte: bevanda latte bianco colazione ~caldo
caffè: bevanda caldo nero colazione italiano
tè: bevanda caldo colazione ~inverno
succo di frutta: bevanda sapdolce colazione ~giallo artificiale
vino: bevanda alcol rosso ~bianco italiano artificiale
birra: bevanda alcol giallo artificiale
bibita gassata: bevanda sapdolce artificiale ~nero
# ---------------- casa ----------------
sedia: mobile legno ~plastica ~cucina
tavolo: mobile legno ~cucina
letto: mobile morbido ~grande legno
divano: mobile morbido ~grande stoffa
armadio: mobile legno grande
lampada: elettronico casa ~mano
frigorifero: elettronico cucina casa grande bianco caro
forno: elettronico cucina casa metallo
microonde: elettronico cucina casa
lavatrice: elettronico casa ~bagno bianco suona caro pulire
televisore: elettronico schermo casa suona nero caro
computer: elettronico schermo casa scuola caro
cellulare: elettronico schermo mano suona caro ~soldi
orologio: mano metallo ~elettronico artificiale ~suona ~caro
radio: elettronico suona casa
aspirapolvere: elettronico casa suona pulire
asciugacapelli: elettronico bagno mano suona caldo
lavastoviglie: elettronico cucina casa suona pulire
# ---------------- utensili e oggetti ----------------
forbici: attrezzo tagliente metallo mano scuola casa
coltello: attrezzo cucina tagliente metallo mano pericoloso casa
forchetta: attrezzo cucina metallo mano casa ~tagliente
cucchiaio: attrezzo cucina metallo mano casa
pentola: attrezzo cucina metallo casa liquidi
bicchiere: cucina vetro fragile liquidi mano casa artificiale
piatto: cucina vetro fragile bianco casa artificiale
bottiglia: vetro ~plastica liquidi mano artificiale ~cucina
tazza: cucina vetro fragile liquidi mano colazione casa artificiale
martello: attrezzo metallo legno mano
chiave: metallo mano artificiale
cacciavite: attrezzo metallo mano tagliente
ombrello: stoffa artificiale ~mano citta inverno
libro: carta mano scuola casa artificiale leggere
matita: legno mano scuola tagliente artificiale
penna: plastica mano scuola artificiale
quaderno: carta scuola mano artificiale ~leggere
zaino: indossa stoffa scuola
scarpe: indossa ~stoffa
cappello: indossa stoffa ~inverno
occhiali: indossa vetro mano fragile ~salute
maglione: indossa stoffa morbido inverno
sciarpa: indossa stoffa morbido inverno collo
guanti: indossa stoffa inverno mano
costume da bagno: indossa stoffa mare
anello: indossa metallo mano caro ~soldi
spazzolino: attrezzo bagno plastica mano casa pulire
sapone: bagno mano casa artificiale pulire
asciugamano: bagno stoffa morbido casa artificiale ~pulire
specchio: vetro fragile bagno casa artificiale
candela: casa mano bianco ~pericoloso artificiale
cuscino: casa morbido stoffa artificiale
coperta: casa morbido stoffa inverno artificiale
moneta: metallo mano artificiale soldi minuscolo
lettera: carta mano artificiale leggere posta
giornale: carta mano artificiale leggere
bandiera: stoffa artificiale aperto
# ---------------- giochi ----------------
pallone: gioco aperto ~plastica artificiale
bambola: gioco casa plastica mano artificiale
orsacchiotto: gioco casa morbido stoffa marrone artificiale
aquilone: gioco vola aperto artificiale
carte da gioco: gioco carta mano artificiale
skateboard: gioco ruote legno artificiale
videogioco: gioco elettronico schermo casa suona
# ---------------- musica ----------------
chitarra: musica legno
pianoforte: musica legno grande nero bianco casa caro
violino: musica legno ~mano
batteria: musica casa
flauto: musica mano
tromba: musica metallo giallo mano
campana: metallo suona artificiale
fischietto: suona mano ~metallo ~plastica artificiale
# ---------------- trasporti ----------------
bicicletta: veicolo ruote metallo aperto citta ~gioco
automobile: veicolo ruote motore metallo citta grande caro
moto: veicolo ruote motore metallo citta suona
autobus: veicolo ruote motore enorme passeggeri citta ~viaggio
treno: veicolo ruote motore enorme passeggeri ~corrente viaggio
aereo: veicolo vola motore enorme passeggeri metallo caro suona viaggio
elicottero: veicolo vola motore enorme metallo suona caro
nave: veicolo naviga motore enorme passeggeri mare viaggio
barca: veicolo naviga ~motore mare ~legno
monopattino: veicolo ruote ~corrente citta ~gioco
camion: veicolo ruote motore enorme suona
trattore: veicolo ruote motore grande fattoria aperto
razzo: veicolo vola motore enorme caro famoso
sottomarino: veicolo naviga motore enorme metallo
mongolfiera: veicolo vola enorme passeggeri aperto ~viaggio
# ---------------- luoghi e natura ----------------
scuola: luogo artificiale citta enorme scuola
ospedale: luogo artificiale citta enorme salute
spiaggia: luogo natura mare aperto giallo
montagna: luogo natura enorme aperto ~inverno
bosco: luogo natura verde enorme aperto
mare: luogo natura enorme aperto
parco: luogo citta verde natura aperto enorme
chiesa: luogo artificiale citta enorme religione
casa: luogo artificiale citta enorme abitare
cinema: luogo artificiale citta enorme schermo
supermercato: luogo artificiale citta enorme spesa soldi
stazione: luogo artificiale citta enorme viaggio
castello: luogo artificiale enorme famoso
deserto: luogo natura enorme aperto giallo africa
luna: natura enorme bianco notturno famoso
sole: natura enorme giallo famoso
nuvola: natura enorme bianco aperto
fuoco: natura pericoloso rosso giallo caldo
neve: natura bianco inverno aperto
albero: pianta grande verde legno marrone aperto
fiore: pianta mano aperto ~rosso ~giallo
rosa: pianta mano rosso tagliente
girasole: pianta giallo ~grande aperto
cactus: pianta tagliente verde
erba: pianta verde aperto
"""

def parse():
    keys = [k for k, _ in QUESTIONS]
    idx = {k: i for i, k in enumerate(keys)}
    objs = []
    for line in C.strip().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, tags = line.split(":", 1)
        yes, maybe, no = set(), set(), set()
        for t in tags.split():
            if t.startswith("~"): maybe.add(t[1:])
            elif t.startswith("!"): no.add(t[1:])
            else: yes.add(t)
        # regole: si applicano finché cambiano
        changed = True
        while changed:
            changed = False
            for k in list(yes):
                for imp in IMPLICA.get(k, []):
                    soft = imp.startswith("~")
                    imp = imp.lstrip("~")
                    if imp not in idx: continue
                    if imp in no or imp in yes: continue
                    if soft:
                        if imp not in maybe: maybe.add(imp); changed = True
                    elif imp in maybe: pass   # l'etichetta esplicita "a volte" vince
                    else: yes.add(imp); changed = True
        # predefiniti di buon senso
        if "reale" not in no and "reale" not in maybe: yes.add("reale")
        if "animale" in yes and "natura" not in yes | no | maybe:
            (maybe if ("domestico" in yes or "fattoria" in yes) else yes).add("natura")
        for t in yes | maybe | no:
            if t not in idx:
                sys.exit(f"etichetta sconosciuta '{t}' in: {name}")
        row = []
        for k in keys:
            row.append("N" if k in no else "Y" if k in yes else "?" if k in maybe else "N")
        objs.append((name.strip(), "".join(row)))
    names = [n for n, _ in objs]
    dup = {n for n in names if names.count(n) > 1}
    if dup: sys.exit(f"doppioni: {dup}")
    # due cose identiche non si potrebbero mai distinguere
    rows = {}
    for n, r in objs:
        if r in rows: print(f"attenzione: '{n}' e '{rows[r]}' hanno le stesse risposte", file=sys.stderr)
        rows[r] = n
    return objs

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

def main():
    objs = parse()
    out = os.path.join(os.path.dirname(__file__), "..", "main", "q20_seed.c")
    with open(out, "w", encoding="utf-8") as f:
        f.write("// q20_seed.c — GENERATO da tools/q20_gen.py: non modificare a mano.\n")
        f.write("// Conoscenza iniziale del Q-20: domande e risposte attese per ogni cosa\n")
        f.write("// (Y = sì, N = no, ? = a volte). Il Gadget poi impara e salva sulla microSD.\n")
        f.write('#include "q20.h"\n\n')
        f.write(f"const int q20_seed_version = {SEED_VERSION};\n")
        f.write(f"const int q20_nq = {len(QUESTIONS)};\n")
        f.write(f"const int q20_seed_n = {len(objs)};\n\n")
        f.write("const char *const q20_questions[] = {\n")
        for _, t in QUESTIONS: f.write(f"    {cstr(t)},\n")
        f.write("};\n\nconst q20_seed_t q20_seed[] = {\n")
        for n, r in objs: f.write(f"    {{{cstr(n)}, {cstr(r)}}},\n")
        f.write("};\n")
    print(f"{len(QUESTIONS)} domande, {len(objs)} cose -> {os.path.normpath(out)}")

if __name__ == "__main__":
    main()
