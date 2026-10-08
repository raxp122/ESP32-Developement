// pet_social.c — "Incontra un amico": due Gadget vicini con l'app aperta si trovano via
// ESP-NOW (pacchetti broadcast, niente rete né associazione). I polipetti si vedono sullo
// schermo dell'altro con i loro colori, giocano insieme (felicità e conchiglie), si
// possono regalare conchiglie e, se sono adulti di sesso diverso e tutti e due i padroni
// lo propongono, ognuno riceve un uovo con un allele per tratto da ciascun genitore.
#include "pet_ui.h"
#include "wifi_mgr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VIS_CHANNEL   1
#define VIS_HELLO_MS  400
#define VIS_LOST_MS   3000
#define VIS_PLAY_MS   5000      // insieme per tanto = una visita
#define VIS_REWARD_S  3600      // una ricompensa all'ora per amico

enum { VF_PROPOSE = 1, VF_EGG = 2 };

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t ver, flags;
    uint32_t uid, peer;           // chi sono e chi vedo
    char name[PET_NAME_LEN], family[PET_NAME_LEN];
    uint8_t sex, stage, form, hat, acc, asleep, nest;
    uint8_t genes[GENE_COUNT][2];
    uint16_t gen;
    uint8_t gift_seq, gift_n;     // regalo: numero progressivo e conchiglie
} vpkt_t;

// casella di posta fra il task del Wi-Fi e l'interfaccia (seqlock: dispari = in scrittura)
static struct { volatile uint32_t seq; vpkt_t p; } box;
static uint32_t box_seen;

static vpkt_t peer;
static bool have_peer, radio_ok, visited, egg_made, proposed;
static uint32_t t_hello, t_last, t_met, gift_seq_rx, n_gift_tx;
static uint8_t gift_seq_tx, gift_n_tx;

static void on_now(const uint8_t *mac, const uint8_t *data, int len)
{
    if (len < (int)sizeof(vpkt_t) || memcmp(data, "GPET", 4)) return;
    const vpkt_t *p = (const vpkt_t *)data;
    if (p->ver != 1 || p->uid == pet_get()->uid) return;
    __atomic_add_fetch(&box.seq, 1, __ATOMIC_ACQ_REL);
    memcpy((void *)&box.p, p, sizeof(vpkt_t));
    __atomic_add_fetch(&box.seq, 1, __ATOMIC_ACQ_REL);
}

static bool fetch(vpkt_t *out)
{
    uint32_t s0 = __atomic_load_n(&box.seq, __ATOMIC_ACQUIRE);
    if (s0 == box_seen || (s0 & 1)) return false;
    memcpy(out, (const void *)&box.p, sizeof(vpkt_t));
    if (__atomic_load_n(&box.seq, __ATOMIC_ACQUIRE) != s0) return false;   // riscritto nel frattempo: al prossimo giro
    box_seen = s0;
    out->name[PET_NAME_LEN - 1] = out->family[PET_NAME_LEN - 1] = 0;
    return true;
}

static void send_hello(void)
{
    const pet_t *p = pet_get();
    const pet_world_t *w = pet_world();
    vpkt_t k = {.magic = {'G', 'P', 'E', 'T'}, .ver = 1, .uid = p->uid, .peer = have_peer ? peer.uid : 0,
                .sex = p->sex, .stage = p->stage, .form = p->form, .hat = w->hat, .acc = w->acc,
                .asleep = p->asleep, .nest = w->has_egg, .gen = p->generation,
                .gift_seq = gift_seq_tx, .gift_n = gift_n_tx};
    k.flags = (proposed ? VF_PROPOSE : 0) | (egg_made ? VF_EGG : 0);
    memcpy(k.name, p->name, PET_NAME_LEN);
    memcpy(k.family, p->family, PET_NAME_LEN);
    memcpy(k.genes, p->genes, sizeof(k.genes));
    wifi_mgr_espnow_send(&k, sizeof(k));
}

// peer_done: l'altro ha appena fatto il suo uovo con noi (il suo nido ora è pieno, va bene)
static bool can_breed_ex(bool peer_done)
{
    const pet_t *p = pet_get();
    return have_peer && p->stage == PET_ADULT && peer.stage == PET_ADULT && p->sex && peer.sex &&
           p->sex != peer.sex && !pet_world()->has_egg && (!peer.nest || peer_done);
}

static void make_egg(void)
{
    const pet_t *p = pet_get();
    pet_world_t *w = pet_world();
    bool mom = p->sex == SEX_F;
    pet_egg_t e;
    memset(&e, 0, sizeof(e));
    uint32_t r = p->rng ^ peer.uid ^ pu_now();
    pet_genes_child(e.genes, mom ? p->genes : peer.genes, mom ? peer.genes : p->genes, &r);
    snprintf(e.parent[0], PET_NAME_LEN, "%s", mom ? p->name : peer.name);
    snprintf(e.parent[1], PET_NAME_LEN, "%s", mom ? peer.name : p->name);
    snprintf(e.family, PET_NAME_LEN, "%s", p->family);
    e.generation = p->generation + 1;
    w->nest = e;
    w->has_egg = 1;
    egg_made = true;
    pet_world_save();
    pet_diary_add("%s e %s hanno avuto un uovo!", e.parent[0], e.parent[1]);
    pet_play(SND_HATCH);
    pu_say("Un uovo nel nido! Nascerà quando il genitore partirà", false);
}

static void vis_enter(void)
{
    have_peer = visited = egg_made = proposed = false;
    t_hello = t_last = t_met = 0;
    gift_seq_tx = gift_n_tx = 0;
    gift_seq_rx = 0xFFFFFFFF;
    n_gift_tx = 0;
    box_seen = __atomic_load_n(&box.seq, __ATOMIC_ACQUIRE);
    radio_ok = wifi_mgr_espnow_start(VIS_CHANNEL, on_now);
    if (!radio_ok) pu_say("Radio non disponibile", true);
}

static void vis_leave(void) { wifi_mgr_espnow_stop(); }

static void vis_update(uint32_t dt)
{
    uint32_t now = pu_now();
    if (!radio_ok) return;
    if (now - t_hello >= VIS_HELLO_MS) { t_hello = now; send_hello(); }
    vpkt_t k;
    if (fetch(&k)) {
        if (!have_peer || k.uid != peer.uid) {
            have_peer = true;
            t_met = now;
            visited = egg_made = proposed = false;
            gift_seq_rx = k.gift_seq;   // i regali vecchi non contano
            pet_friend_t *f = pet_friend_meet(k.uid, k.name, k.family, k.sex, k.stage, k.form);
            char t[64];
            snprintf(t, sizeof(t), f->visits ? "Rieccoti, %s!" : "Hai trovato %s!", k.name);
            pu_say(t, false);
            pet_play(SND_CALL);
        }
        peer = k;
        t_last = now;
        // regalo in arrivo (una volta per numero)
        if (k.gift_seq != (uint8_t)gift_seq_rx && k.gift_n) {
            gift_seq_rx = k.gift_seq;
            pet_shells_add(k.gift_n);
            pet_world_save();
            char t[64];
            snprintf(t, sizeof(t), "%s ti regala %u conchiglie!", k.name, k.gift_n);
            pu_say(t, false);
            pet_diary_add("Regalo da %s: %u conchiglie", k.name, k.gift_n);
            pet_play(SND_WIN);
        }
        // uovo: tutti e due l'hanno proposto
        if (proposed && !egg_made && (k.flags & VF_PROPOSE) && can_breed_ex(k.flags & VF_EGG)) make_egg();
    }
    if (have_peer && now - t_last > VIS_LOST_MS) {
        have_peer = false;
        proposed = false;
        pu_say("L'amico si è allontanato", false);
    }
    // insieme abbastanza a lungo: è una visita
    if (have_peer && !visited && now - t_met >= VIS_PLAY_MS) {
        visited = true;
        pet_friend_t *f = pet_friend_meet(peer.uid, peer.name, peer.family, peer.sex, peer.stage, peer.form);
        int64_t ep = 0;
        bool known = pet_now(&ep);
        bool reward = !known || !f->last || (uint32_t)ep - f->last >= VIS_REWARD_S;
        if (f->visits < 255) f->visits++;
        if (known) f->last = (uint32_t)ep;
        pet_friends_save();
        if (reward) {
            pet_result_t r;
            pet_do(ACT_VISIT, &r);
            pet_shells_add(3);
            pet_world_save();
            pet_diary_add("Ha giocato con %s %s", peer.name, peer.family);
            pu_say("Che bella visita! +3 conchiglie", false);
        } else {
            pu_say("Si sono già visti da poco: solo coccole", false);
        }
    }
}

static void vis_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    int w, h;
    pu_dims(&w, &h);
    if (!have_peer) {
        art_pet(p->stage, p->form, (LW - w) / 2, FLOOR - h + 1, EXPR_NORMAL, now / 400, (now / 1500) & 1, (now / 1500) & 1 ? 1 : -1, TINT_NONE);
        if ((now / 500) & 1) art_sprite(&SPR_QUESTION, (LW - w) / 2 + w + 1, FLOOR - h - 4, false);
        // onde radio
        int r = (now / 150) % 8;
        art_px(LW / 2 - r * 3, 4, 0x7FC8EE);
        art_px(LW / 2 + r * 3, 4, 0x7FC8EE);
        return;
    }
    // insieme: il mio a sinistra, l'amico a destra (con i suoi colori e vestiti)
    bool play = now - t_met < VIS_PLAY_MS + 1500 || ((now / 3000) & 1);
    int jump = play && ((now / 250) & 1) ? -2 : 0;
    art_pet(p->stage, p->form, 8, FLOOR - h + 1 + jump, EXPR_HAPPY, now / 200, false, 1, TINT_NONE);
    pu_look_genes(peer.genes, peer.hat, peer.acc);
    int fw, fh;
    int fs = peer.stage >= PET_BABY && peer.stage <= PET_ADULT ? peer.stage : PET_CHILD;
    art_pet_size(fs, peer.form, &fw, &fh);
    art_pet(fs, peer.form, LW - fw - 8, FLOOR - fh + 1 + (jump ? 0 : -2), peer.asleep ? EXPR_SLEEP : EXPR_HAPPY, now / 200 + 1, true, 1, TINT_NONE);
    pu_look_self();
    // in mezzo: cuori o l'uovo
    if (egg_made || (proposed && (peer.flags & VF_PROPOSE))) art_sprite(&SPR_NEST_EGG, LW / 2 - 3, FLOOR - 6, false);
    else if (proposed || (peer.flags & VF_PROPOSE)) art_sprite(&SPR_HEART_BIG, LW / 2 - 3, 8 + ((now / 400) & 1), false);
    else art_sprite(&SPR_HEART, LW / 2 - 2, 6 + (int)((now / 120) % 10), false);
    if (play) art_sprite(&SPR_BALL, LW / 2 - 2 + (int)(10 * sinf(now / 300.0f)), 2 + (int)((now / 100) % 6), false);
}

static void vis_panel(char *big, int nb, char *hint, int nh)
{
    if (!radio_ok) { snprintf(big, nb, "Radio spenta"); snprintf(hint, nh, "ESP-NOW non disponibile"); return; }
    if (!have_peer) {
        snprintf(big, nb, "Cerco amici…");
        snprintf(hint, nh, "Sull'altro Gadget apri Polipetto » Diario e altro » Incontra un amico");
        return;
    }
    snprintf(big, nb, "%s", peer.name);
    const pet_world_t *w = pet_world();
    if (egg_made) { snprintf(hint, nh, "Uovo nel nido! · su: regala 5 conchiglie (ne hai %u)", w->shells); return; }
    if (can_breed_ex(peer.flags & VF_EGG)) {
        if (proposed && !(peer.flags & VF_PROPOSE)) snprintf(hint, nh, "Uovo proposto: aspetto che anche l'altro dica sì");
        else if (!proposed && (peer.flags & VF_PROPOSE)) snprintf(hint, nh, "%s vuole un uovo! Destra: sì · su: regala conchiglie", peer.name);
        else snprintf(hint, nh, "%s %s · destra: proponi un uovo · su: regala 5 conchiglie", peer.sex == SEX_F ? "femmina" : "maschio", peer.family);
        return;
    }
    const char *why = "";
    const pet_t *p = pet_get();
    if (p->stage != PET_ADULT || peer.stage != PET_ADULT) why = "uova solo da adulti";
    else if (p->sex == peer.sex) why = "stesso sesso: niente uova";
    else if (w->has_egg || peer.nest) why = "c'è già un uovo nel nido";
    snprintf(hint, nh, "%s %s · %s · su: regala 5 conchiglie (ne hai %u)", peer.sex == SEX_F ? "femmina" : "maschio", peer.family, why, w->shells);
}

static bool vis_nav(nav_t ev)
{
    if (!have_peer) return ev != NAV_BACK;
    if (ev == NAV_SELECT && !proposed && can_breed_ex(peer.flags & VF_EGG)) {
        proposed = true;
        send_hello();
        if (peer.flags & VF_PROPOSE) make_egg();   // l'altro aveva già detto sì
        return true;
    }
    if (ev == NAV_NEXT) {
        pet_world_t *w = pet_world();
        if (w->shells < 5) { pu_say("Servono almeno 5 conchiglie", true); return true; }
        if (n_gift_tx >= 5) { pu_say("Basta regali per questo incontro", false); return true; }
        pet_shells_add(-5);
        pet_world_save();
        gift_seq_tx++;
        gift_n_tx = 5;
        n_gift_tx++;
        send_hello();
        pu_say("Regalo partito: 5 conchiglie", false);
        return true;
    }
    return ev != NAV_BACK;
}

const pet_mod_t PM_VISIT = {.enter = vis_enter, .leave = vis_leave, .update = vis_update, .draw = vis_draw,
                            .nav = vis_nav, .panel = vis_panel, .period = 50};
