// pet_games.c — minigiochi del Polipetto: "1, 2, 3 stella", Memoria e Ritmo.
// Ognuno è un modulo (pet_ui.h): l'app gli dà la scena da disegnare e il pannello.
#include "pet_ui.h"
#include "board.h"
#include "input.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"

static uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }
static inline bool reached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

/* =====================================================================
 * 1, 2, 3 stella
 * Il polipetto conta girato verso lo scoglio: tieni premuto (dito o BOOT) e il pesciolino
 * avanza. Quando si gira devi lasciare subito, altrimenti ti vede e si ricomincia da capo.
 * Bisogna arrivare prima che finiscano i suoi giri.
 * ===================================================================== */

#define ST_FILL_MS   8000   // tenendo premuto per tanto si arriva
#define ST_TURNS     9      // giri a disposizione
enum { ST_READY, ST_BACK, ST_PEEK, ST_FACE, ST_CAUGHT };

static struct {
    int phase, turns, resets;
    uint32_t t_phase, t_end, grace;
    float prog;
    bool held, was_held;
} st;

static void st_phase(int ph, uint32_t ms)
{
    st.phase = ph;
    st.t_phase = pu_now();
    st.t_end = pu_now() + ms;
}

static void st_enter(void)
{
    memset(&st, 0, sizeof(st));
    st.turns = ST_TURNS;
    st_phase(ST_READY, 1800);
}

static void st_update(uint32_t dt)
{
    uint32_t now = pu_now();
    st.held = pu_held();
    if (dt > 100) dt = 100;
    switch (st.phase) {
    case ST_READY:
        if (reached(now, st.t_end)) st_phase(ST_BACK, 1300 + rnd(1700));
        break;
    case ST_BACK:
    case ST_PEEK:
        if (st.held) {
            st.prog += (float)dt / ST_FILL_MS;
            if (st.prog >= 1) {
                char t[64];
                bool big = !st.resets;
                snprintf(t, sizeof(t), big ? "Stella! Mai visto: perfetto!" : "Stella! Ce l'hai fatta");
                pu_result(REC_STAR, st.turns * 10 + (big ? 50 : 0), 3 + (big ? 2 : 0) + st.turns / 3, true, big, t);
                return;
            }
        }
        if (st.phase == ST_PEEK && reached(now, st.t_end)) { st_phase(ST_BACK, 500 + rnd(1200)); break; }
        if (st.phase == ST_BACK && reached(now, st.t_end)) {
            // dal terzo giro ogni tanto finge di girarsi (sbirciata) e torna a contare
            if (st.turns <= ST_TURNS - 3 && rnd(5) == 0) { st_phase(ST_PEEK, 350); break; }
            st.grace = 330 - (uint32_t)(st.prog * 120);   // più sei vicino, meno tempo hai per lasciare
            st_phase(ST_FACE, 1300 + rnd(1200));
            pet_play(SND_TICK);
        }
        break;
    case ST_FACE:
        if (st.held && now - st.t_phase > st.grace) {
            st.prog = 0;
            st.resets++;
            pet_play(SND_NO);
            st_phase(ST_CAUGHT, 1200);
            break;
        }
        if (reached(now, st.t_end)) {
            if (--st.turns <= 0) {
                pu_result(REC_STAR, 0, 0, false, false, "Giri finiti: non sei arrivato in tempo");
                return;
            }
            st_phase(ST_BACK, 1200 + rnd(1800));
        }
        break;
    case ST_CAUGHT:
        if (reached(now, st.t_end)) {
            if (--st.turns <= 0) {
                pu_result(REC_STAR, 0, 0, false, false, "Ti ha visto! Giri finiti");
                return;
            }
            st_phase(ST_BACK, 1200 + rnd(1800));
        }
        break;
    }
}

static void st_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    // lo scoglio contro cui conta
    art_rect(LW - 5, 8, 5, 21, 0x6E747C);
    art_rect(LW - 5, 8, 1, 21, 0x8C939B);
    int w, h;
    pu_dims(&w, &h);
    int px = LW - w - 6, py = FLOOR - h + 1;
    expr_t e = EXPR_BACK;
    int look = 0;
    bool flip = false;
    if (st.phase == ST_FACE || st.phase == ST_READY) { e = EXPR_NORMAL; look = -1; }
    if (st.phase == ST_CAUGHT) { e = EXPR_ANGRY; look = -1; }
    if (st.phase == ST_PEEK) { e = EXPR_SURPRISE; look = -1; flip = true; }
    int bob = st.phase == ST_BACK && (now / 300) & 1 ? -1 : 0;   // conta dondolando
    art_pet(p->stage, p->form, px, py + bob, e, now / 300, flip, look, TINT_NONE);
    if (st.phase == ST_CAUGHT && (now / 200) & 1) art_sprite(&SPR_BANG, px - 3, py - 2, false);

    // il pesciolino del giocatore: avanza con la barra
    float k = st.prog;
    int fx = 2 + (int)(k * (px - 12));
    int fy = FLOOR - 5 + ((st.held && (now / 150) & 1) ? -1 : 0);
    if (st.phase == ST_CAUGHT && (now / 120) & 1) art_sprite_color(&SPR_FISH, fx, fy, 0xE5484D);
    else art_sprite(&SPR_FISH, fx, fy, true);

    // barra in alto
    art_rect(3, 1, LW - 6, 3, 0x3A4048);
    art_rect(4, 2, (int)((LW - 8) * k), 1, st.phase == ST_FACE ? 0xE5484D : 0xFFD23C);
}

static void st_panel(char *big, int nb, char *hint, int nh)
{
    uint32_t el = pu_now() - st.t_phase;
    static const char *const count[] = {"Un…", "Un, due…", "Un, due, tre…"};
    switch (st.phase) {
    case ST_READY:  snprintf(big, nb, "Pronto?"); break;
    case ST_BACK:   snprintf(big, nb, "%s", count[el / 450 < 3 ? el / 450 : 2]); break;
    case ST_PEEK:   snprintf(big, nb, "…mmh?"); break;
    case ST_FACE:   snprintf(big, nb, "STELLA!"); break;
    default:        snprintf(big, nb, "Ti ho visto!"); break;
    }
    snprintf(hint, nh, "%s · giri rimasti %d · sinistra esce",
             st.phase == ST_FACE ? "Lascia!" : "Tieni premuto (dito o BOOT)", st.turns);
}

static bool st_nav(nav_t ev) { return ev != NAV_BACK; }   // tutto il resto è il dito sullo schermo

const pet_mod_t PM_STAR = {.enter = st_enter, .update = st_update, .draw = st_draw, .nav = st_nav, .panel = st_panel, .period = 30};

/* =====================================================================
 * Memoria
 * Il polipetto indica una sequenza di frecce; ripetila con gli swipe. A ogni giro si
 * allunga di una. BOOT esce (la sinistra serve per giocare).
 * ===================================================================== */

#define MEM_MAX 32
enum { MM_SHOW, MM_INPUT, MM_OK, MM_FAIL };
enum { D_UP, D_DOWN, D_LEFT, D_RIGHT };

static struct {
    uint8_t seq[MEM_MAX];
    int len, pos, phase, flash, best;
    uint32_t t;
} mm;

static void mm_next_round(void)
{
    if (mm.len < MEM_MAX) mm.seq[mm.len++] = (uint8_t)rnd(4);
    mm.pos = 0;
    mm.phase = MM_SHOW;
    mm.t = pu_now() + 700;
    mm.flash = -1;
}

static void mm_enter(void)
{
    memset(&mm, 0, sizeof(mm));
    mm.len = 2;
    mm.seq[0] = (uint8_t)rnd(4);
    mm.seq[1] = (uint8_t)rnd(4);
    mm.pos = 0;
    mm.phase = MM_SHOW;
    mm.t = pu_now() + 900;
    mm.flash = -1;
}

static void mm_end(void)
{
    char t[64];
    int s = mm.best;
    snprintf(t, sizeof(t), "Ricordavi %d frecce di fila!", s);
    pu_result(REC_MEMORY, s, s >= 4 ? s / 2 : 0, s >= 5, s >= 8, t);
}

static void mm_update(uint32_t dt)
{
    uint32_t now = pu_now();
    switch (mm.phase) {
    case MM_SHOW:
        // ogni freccia: 550 ms accesa, 200 di pausa
        if (!reached(now, mm.t)) break;
        if (mm.flash < 0) {
            if (mm.pos >= mm.len) { mm.phase = MM_INPUT; mm.pos = 0; break; }
            mm.flash = mm.seq[mm.pos];
            mm.t = now + 550;
            pet_play(SND_TICK);
        } else {
            mm.flash = -1;
            mm.pos++;
            mm.t = now + 200;
        }
        break;
    case MM_INPUT:
        if (mm.flash >= 0 && reached(now, mm.t)) mm.flash = -1;
        break;
    case MM_OK:
        if (reached(now, mm.t)) mm_next_round();
        break;
    case MM_FAIL:
        if (reached(now, mm.t)) { mm_end(); return; }
        break;
    }
}

// freccia 9×9 al centro di (cx, cy)
static void arrow(int dir, int cx, int cy, uint32_t col)
{
    for (int i = 0; i < 5; i++)
        for (int j = -i; j <= i; j++) {
            int x = 0, y = 0;
            switch (dir) {
            case D_UP:    x = j; y = -4 + i; break;
            case D_DOWN:  x = j; y = 4 - i; break;
            case D_LEFT:  x = -4 + i; y = j; break;
            default:      x = 4 - i; y = j; break;
            }
            art_px(cx + x, cy + y, col);
        }
    for (int k = 0; k < 4; k++) {   // gambo
        switch (dir) {
        case D_UP:   art_rect(cx - 1, cy + 1 + k, 3, 1, col); break;
        case D_DOWN: art_rect(cx - 1, cy - 1 - k, 3, 1, col); break;
        case D_LEFT: art_rect(cx + 1 + k, cy - 1, 1, 3, col); break;
        default:     art_rect(cx - 1 - k, cy - 1, 1, 3, col); break;
        }
    }
}

static const uint32_t dir_col[4] = {0xFFD23C, 0x3F9FE0, 0x3DBA5A, 0xE5484D};

static void mm_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    int w, h;
    pu_dims(&w, &h);
    int px = LW - w - 4;
    expr_t e = mm.phase == MM_FAIL ? EXPR_SAD : mm.phase == MM_OK ? EXPR_HAPPY : EXPR_NORMAL;
    int look = mm.phase == MM_SHOW ? -1 : 0;
    art_pet(p->stage, p->form, px, FLOOR - h + 1, e, now / 300, false, look, TINT_NONE);
    // le quattro frecce spente, quella di turno accesa
    static const int8_t ax[4] = {20, 20, 10, 30}, ay[4] = {7, 22, 15, 15};
    for (int d = 0; d < 4; d++) arrow(d, ax[d], ay[d], mm.flash == d ? dir_col[d] : 0x2A3440);
    if (mm.phase == MM_FAIL) art_sprite(&SPR_ANGER, px + w - 1, FLOOR - h - 2, false);
    // a che punto sei della sequenza
    for (int i = 0; i < mm.len && i < 16; i++)
        art_px(2 + i * 2, 30, i < mm.pos && mm.phase != MM_SHOW ? 0xFFD23C : 0x3A4048);
}

static void mm_panel(char *big, int nb, char *hint, int nh)
{
    switch (mm.phase) {
    case MM_SHOW:  snprintf(big, nb, "Guarda bene…"); break;
    case MM_INPUT: snprintf(big, nb, "Tocca a te: %d/%d", mm.pos, mm.len); break;
    case MM_OK:    snprintf(big, nb, "Giusto!"); break;
    default:       snprintf(big, nb, "Sbagliato!"); break;
    }
    snprintf(hint, nh, "Ripeti con gli swipe (su, giù, sinistra, destra) · record %u · BOOT esce", pet_world()->best[REC_MEMORY]);
}

static bool mm_nav(nav_t ev)
{
    if (ev == NAV_BTN) { pu_say("Partita annullata", false); pu_exit(); return true; }
    int d = ev == NAV_NEXT ? D_UP : ev == NAV_PREV ? D_DOWN : ev == NAV_BACK ? D_LEFT : ev == NAV_SELECT ? D_RIGHT : -1;
    if (d < 0 || mm.phase != MM_INPUT) return true;
    mm.flash = d;
    mm.t = pu_now() + 250;
    if (mm.seq[mm.pos] != d) {
        mm.phase = MM_FAIL;
        mm.t = pu_now() + 1300;
        pet_play(SND_NO);
        return true;
    }
    pet_play(SND_TICK);
    if (++mm.pos >= mm.len) {
        mm.best = mm.len;
        mm.phase = MM_OK;
        mm.t = pu_now() + 800;
        if (mm.len >= MEM_MAX) { mm_end(); return true; }
    }
    return true;
}

const pet_mod_t PM_MEMORY = {.enter = mm_enter, .update = mm_update, .draw = mm_draw, .nav = mm_nav, .panel = mm_panel, .period = 40};

/* =====================================================================
 * Ritmo
 * Le bolle arrivano da sinistra verso il cerchio davanti al polipetto: tocca lo schermo
 * (o premi BOOT) quando ci sono dentro. Perfetto = 2 punti, bene = 1.
 * ===================================================================== */

#define RH_NOTES   28
#define RH_TRAVEL  1600     // ms dalla comparsa al cerchio
#define RH_TX      40       // x del cerchio
#define RH_X0      -4
#define RH_PERFECT 110
#define RH_GOOD    220

static struct {
    uint32_t t0;              // inizio del brano
    uint32_t hit_at[RH_NOTES];
    int8_t  res[RH_NOTES];    // 0 in arrivo, 2 perfetto, 1 bene, -1 mancata
    int score, n, judge;      // judge: ultimo giudizio (per il testo)
    uint32_t t_judge;
    bool prev;
} rh;

static void rh_enter(void)
{
    memset(&rh, 0, sizeof(rh));
    rh.t0 = pu_now() + 1500;
    // brano: crome e semiminime a 100 bpm, con qualche pausa; accelera un poco
    uint32_t t = RH_TRAVEL;
    for (int i = 0; i < RH_NOTES; i++) {
        rh.hit_at[i] = t;
        int beat = i < 10 ? 600 : i < 20 ? 520 : 450;
        uint32_t r = rnd(6);
        t += r == 0 ? beat / 2 : r == 5 ? beat * 2 : beat;
        if (rh.hit_at[i] - (i ? rh.hit_at[i - 1] : 0) < 220 && i) rh.hit_at[i] = rh.hit_at[i - 1] + 220;
    }
    rh.n = RH_NOTES;
    rh.prev = true;   // un dito già sullo schermo non vale come primo colpo
}

static void rh_tap(uint32_t t)
{
    int best = -1;
    uint32_t bd = 0xFFFFFFFF;
    for (int i = 0; i < rh.n; i++) {
        if (rh.res[i]) continue;
        int32_t d = (int32_t)(t - rh.hit_at[i]);
        uint32_t ad = d < 0 ? -d : d;
        if (ad < bd) { bd = ad; best = i; }
    }
    if (best < 0 || bd > RH_GOOD) return;   // nessuna bolla vicina: niente
    rh.res[best] = bd <= RH_PERFECT ? 2 : 1;
    rh.score += rh.res[best];
    rh.judge = rh.res[best];
    rh.t_judge = pu_now();
    pet_play(SND_TICK);
}

static void rh_update(uint32_t dt)
{
    uint32_t now = pu_now();
    if (!reached(now, rh.t0)) { rh.prev = pu_held(); return; }
    uint32_t t = now - rh.t0;
    // il tocco si legge senza antirimbalzo lungo: conta l'istante in cui comincia
    int x, y;
    bool raw = (input_touch(&x, &y) && !input_locked()) || board_btn_boot();
    if (raw && !rh.prev) rh_tap(t);
    rh.prev = raw;
    bool all = true;
    for (int i = 0; i < rh.n; i++) {
        if (!rh.res[i] && (int32_t)(t - rh.hit_at[i]) > RH_GOOD) { rh.res[i] = -1; rh.judge = -1; rh.t_judge = now; }
        if (!rh.res[i]) all = false;
    }
    if (all && t > rh.hit_at[rh.n - 1] + 600) {
        int max = rh.n * 2;
        char m[64];
        snprintf(m, sizeof(m), "Ritmo: %d punti su %d", rh.score, max);
        pu_result(REC_RHYTHM, rh.score, rh.score / 6, rh.score * 10 >= max * 6, rh.score * 10 >= max * 9, m);
    }
}

static void rh_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    int w, h;
    pu_dims(&w, &h);
    int px = LW - w - 3;
    uint32_t t = reached(now, rh.t0) ? now - rh.t0 : 0;
    // balla a tempo (sul battito della prossima bolla)
    int bob = (now / 300) & 1 ? -1 : 0;
    expr_t e = now - rh.t_judge < 400 ? (rh.judge > 0 ? EXPR_HAPPY : rh.judge < 0 ? EXPR_SAD : EXPR_NORMAL) : EXPR_NORMAL;
    art_pet(p->stage, p->form, px, FLOOR - h + 1 + bob, e, now / 200, false, -1, TINT_NONE);
    // corsia e cerchio
    int cy = 16;
    art_rect(0, cy, RH_TX + 4, 1, 0x1F3A4E);
    uint32_t ring = now - rh.t_judge < 250 && rh.judge > 0 ? 0xFFD23C : 0xA8E6FF;
    for (int a = -3; a <= 3; a++) {
        art_px(RH_TX + a, cy - 3, ring); art_px(RH_TX + a, cy + 3, ring);
        art_px(RH_TX - 3, cy + a, ring); art_px(RH_TX + 3, cy + a, ring);
    }
    for (int i = 0; i < rh.n; i++) {
        if (rh.res[i] > 0) continue;
        int32_t d = (int32_t)(rh.hit_at[i] - t);   // ms all'arrivo
        if (d > RH_TRAVEL || d < -RH_GOOD) continue;
        int x = RH_TX - (int)((float)d / RH_TRAVEL * (RH_TX - RH_X0));
        uint32_t c = rh.res[i] < 0 ? 0x6E747C : 0x7FC8EE;
        art_rect(x - 1, cy - 2, 3, 5, c);
        art_rect(x - 2, cy - 1, 5, 3, c);
        art_px(x - 1, cy - 1, 0xEAF0F8);
    }
}

static void rh_panel(char *big, int nb, char *hint, int nh)
{
    uint32_t now = pu_now();
    if (!reached(now, rh.t0)) snprintf(big, nb, "Pronti…");
    else if (now - rh.t_judge < 500 && rh.judge) snprintf(big, nb, "%s", rh.judge == 2 ? "Perfetto!" : rh.judge == 1 ? "Bene" : "Mancata");
    else snprintf(big, nb, "Punti: %d", rh.score);
    snprintf(hint, nh, "Tocca (o BOOT) quando la bolla è nel cerchio · record %u · sinistra esce", pet_world()->best[REC_RHYTHM]);
}

static bool rh_nav(nav_t ev) { return ev != NAV_BACK; }

const pet_mod_t PM_RHYTHM = {.enter = rh_enter, .update = rh_update, .draw = rh_draw, .nav = rh_nav, .panel = rh_panel, .period = 20};
