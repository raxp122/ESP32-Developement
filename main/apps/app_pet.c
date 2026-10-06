// app_pet.c — Polipetto: un Tamagotchi originale con un polpetto arancione.
// A sinistra la scena in pixel art (64×32 pixel ingranditi 5 volte), a destra le
// icone delle cure e i messaggi. Su/giù sceglie, destra conferma, sinistra torna
// indietro. Dito tenuto sullo schermo: coccole. Scuotere la scheda: lo fa ridere.
// Le regole del gioco sono in pet_core.c, il tempo che scorre in pet.c.
#include "apps.h"
#include "pet.h"
#include "pet_art.h"
#include "board.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"

#define LW       64
#define LH       32
#define SC       5
#define FLOOR    28        // ultima riga d'acqua: i tentacoli poggiano qui
#define IC_SLOT  12        // pixel logici per icona
#define IC_SC    3
#define IC_LW    (PICON_COUNT * IC_SLOT)
#define IC_LH    10
#define PX       340       // pannello di destra
#define PW       (SCR_W - PX - 8)
#define N_SEL    7         // icone selezionabili: l'ultima segnala solo le chiamate
#define FISH_MS  30000
#define C_DIM_RGB 0x6E747C

typedef enum { M_MAIN, M_FOOD, M_PLAY, M_STATS, M_LR, M_FISH, M_WALK } view_t;
typedef enum {
    A_NONE, A_EAT, A_SNACK, A_DRINK, A_NO, A_HAPPY, A_CLEAN, A_MED, A_SCOLD, A_SAD,
    A_HEARTS, A_LAUGH, A_GRUMPY, A_HATCH, A_EVOLVE,
} anim_t;

static const uint16_t anim_ms[] = {
    [A_EAT] = 2400, [A_SNACK] = 2400, [A_DRINK] = 2400, [A_NO] = 1200, [A_HAPPY] = 1600,
    [A_CLEAN] = 1600, [A_MED] = 1800, [A_SCOLD] = 1500, [A_SAD] = 1500, [A_HEARTS] = 1500,
    [A_LAUGH] = 1800, [A_GRUMPY] = 1500, [A_HATCH] = 2000, [A_EVOLVE] = 2600,
};

static uint16_t *scene_buf, *icon_buf;
static int scene_stride, icon_stride;   // in pixel
static lv_obj_t *scene, *icons, *l_title, *l_info, *l_main, *l_hint;
static lv_timer_t *tmr;
static view_t mode;
static int sel, sub, page;
static anim_t anim;
static uint32_t anim_t0;
static int anim_arg;
static char msg[80];
static uint32_t msg_until;
// confronti tra istanti di lv_tick_get() che restano giusti anche quando il contatore
// ricomincia da zero (dopo ~49 giorni di accensione)
static inline bool reached(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }
static bool msg_warn;
static uint32_t now_ms, last_ms;
static float pos_x = 20;
static int target_x = 20;
static bool face_left;
static uint32_t next_wander;
static struct { int8_t x, y; } bub[3] = {{3, 20}, {5, 9}, {59, 15}};

/* ---------------- utilità ---------------- */

static uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s)) lv_label_set_text(l, s);
}

static void say(const char *s, bool warn)
{
    snprintf(msg, sizeof(msg), "%s", s);
    msg_until = lv_tick_get() + 2800;
    msg_warn = warn;
}

static void start(anim_t a, int arg)
{
    anim = a;
    anim_t0 = now_ms;
    anim_arg = arg;
}

static float anim_k(void)
{
    if (anim == A_NONE) return 1;
    float k = (float)(now_ms - anim_t0) / anim_ms[anim];
    return k > 1 ? 1 : k;
}

static uint32_t accent_rgb(void) { return lv_color_to_u32(ui_accent()) & 0xFFFFFF; }

static void fmt_age(uint32_t s, char *b, int n)
{
    if (s < 3600) snprintf(b, n, "%lu min", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(b, n, "%lu or%s", (unsigned long)(s / 3600), s < 7200 ? "a" : "e");
    else snprintf(b, n, "%lu giorn%s", (unsigned long)(s / 86400), s < 2 * 86400 ? "o" : "i");
}

static const char *stage_name(const pet_t *p)
{
    switch (p->stage) {
    case PET_EGG:   return "Uovo";
    case PET_BABY:  return "Polipetto neonato";
    case PET_CHILD: return "Polipetto bimbo";
    case PET_TEEN:  return p->form == FORM_TEEN_BAD ? "Polipetto ribelle" : "Polipetto ragazzo";
    case PET_ADULT:
        switch (p->form) {
        case FORM_SAGE:     return "Polpo saggio";
        case FORM_EXPLORER: return "Polpo esploratore";
        case FORM_GLUTTON:  return "Polpo goloso";
        case FORM_MESSY:    return "Polpo pasticcione";
        default:            return "Polpo";
        }
    case PET_DEAD:  return "Addio, polipetto…";
    default:        return "Polipetto";
    }
}

static const char *need_text(const pet_t *p)
{
    if (p->stage == PET_EGG) return "Sta per schiudersi…";
    if (p->sick) return "Sta male: dagli la medicina!";
    if (p->asleep && !p->light_off) return "Si è addormentato: spegni la luce";
    if (p->asleep) return "Zzz… sta dormendo";
    if (!p->hunger) return "Ha fame!";
    if (!p->thirst) return "Ha sete!";
    if (p->tantrum) return "Fa i capricci: sgridalo!";
    if (!p->happy) return "È triste: gioca con lui";
    if (p->poop >= 2) return "Pulisci l'inchiostro";
    return NULL;
}

/* ---------------- inclinazione (minigioco) ----------------
 * Come in tilt.c: l'asse lungo si sceglie alla calibrazione. Sulla scheda il verso
 * "naturale" è quello invertito (stesso default di Doom); si può girare nelle impostazioni.
 */

static vec3_t rest;
static bool rest_long_x;

static bool accel_norm(vec3_t *g)
{
    vec3_t a = {0}, s;
    for (int i = 0; i < 2; i++) {
        if (!board_imu_accel(&s)) return false;
        a.x += s.x; a.y += s.y; a.z += s.z;
    }
    float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (n < 0.1f) return false;
    g->x = a.x / n; g->y = a.y / n; g->z = a.z / n;
    return true;
}

static void tilt_cal(void)
{
    if (!accel_norm(&rest)) rest = (vec3_t){0, 0, 1};
    rest_long_x = fabsf(rest.x) < fabsf(rest.y);
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static float tilt_now(void)
{
    vec3_t g;
    if (!accel_norm(&g)) return 0;
    float gl = rest_long_x ? g.x : g.y, rl = rest_long_x ? rest.x : rest.y;
    float deg = (asinf(clampf(gl, -1, 1)) - asinf(clampf(rl, -1, 1))) * 57.29578f;
    if (g_set.flipped) deg = -deg;
    if (!g_set.pet_tilt_inv) deg = -deg;
    float a = fabsf(deg);
    if (a < 3) return 0;
    float v = (a - 3) / 17;
    if (v > 1) v = 1;
    return deg < 0 ? -v : v;
}

/* ---------------- minigiochi: stato ---------------- */

static int lr_round, lr_score, lr_dir, lr_guess;
static uint32_t lr_t0;

typedef struct { float x, y, vy; uint8_t jelly, on; } drop_t;
static drop_t drops[6];
static float fish_x;
static int fish_score;
static uint32_t fish_t0, fish_next, fish_stun, fish_last;

static uint32_t walk_steps0, walk_seen, walk_last_ms;
static int walk_scroll;

/* ---------------- scena ---------------- */

static void pet_dims(const pet_t *p, int *w, int *h) { art_pet_size(p->stage, p->form, w, h); }

// limite destro per il polipetto: non deve calpestare l'inchiostro
static const int8_t ink_xy[4][2] = {{56, 24}, {49, 24}, {56, 19}, {42, 24}};

static int right_limit(const pet_t *p)
{
    int lim = LW - 1;
    for (int i = 0; i < p->poop && i < 4; i++)
        if (ink_xy[i][0] - 1 < lim) lim = ink_xy[i][0] - 1;
    return lim;
}

static expr_t base_expr(const pet_t *p)
{
    if (p->asleep) return EXPR_SLEEP;
    if (p->sick) return EXPR_SICK;
    if (p->tantrum) return EXPR_ANGRY;
    if (!p->hunger || !p->thirst || !p->happy) return EXPR_SAD;
    if (now_ms % 4000 < 160) return EXPR_BLINK;
    if (p->happy >= PET_MAX && now_ms % 9000 < 1500) return EXPR_HAPPY;
    return EXPR_NORMAL;
}

static void draw_bubbles(void)
{
    if ((now_ms / 100) % 3 == 0)
        for (int i = 0; i < 3; i++)
            if (--bub[i].y < 1) { bub[i].y = FLOOR - 2; bub[i].x = (int8_t)(i == 2 ? 58 + rnd(3) : 2 + rnd(4)); }
    for (int i = 0; i < 3; i++) art_px(bub[i].x + ((bub[i].y >> 2) & 1), bub[i].y, 0x7FC8EE);
}

static void draw_main(const pet_t *p)
{
    bool dark = p->light_off;
    art_brightness(dark ? 60 : 255);
    art_background(now_ms / 100);
    if (!dark) draw_bubbles();

    // inchiostro sul fondo; durante la pulizia l'onda lo porta via da sinistra
    int ink = anim == A_CLEAN ? anim_arg : p->poop;
    int wave = anim == A_CLEAN ? (int)(anim_k() * (LW + 10)) - 5 : -100;
    for (int i = 0; i < ink && i < 4; i++)
        if (ink_xy[i][0] > wave) art_sprite(&SPR_INK, ink_xy[i][0], ink_xy[i][1], false);
    if (anim == A_CLEAN)
        for (int y = 10; y <= FLOOR; y += 4) art_sprite(&SPR_BUBBLE, wave + ((y >> 2) & 1), y, false);

    if (p->stage == PET_EGG) {
        uint32_t left = pet_core_egg_left(p);
        int wob = left <= 20 && ((now_ms / 150) & 1) ? 1 : 0;
        int crack = left <= 15 ? (int)(15 - left) / 5 + 1 : 0;
        art_egg((LW - EGG_W) / 2 + wob, FLOOR - EGG_H + 1, crack);
        art_brightness(255);
        return;
    }

    int w, h;
    pet_dims(p, &w, &h);
    // passeggiata qua e là quando non succede niente
    if (anim == A_NONE && !p->asleep) {
        int maxx = right_limit(p) - w;
        if (maxx < 1) maxx = 1;
        if (reached(now_ms, next_wander)) {
            target_x = 1 + rnd(maxx);
            next_wander = now_ms + 2500 + rnd(3500);
        }
        if (target_x > maxx) target_x = maxx;
        if (pos_x < target_x - 0.2f) { pos_x += 0.25f; face_left = false; }
        else if (pos_x > target_x + 0.2f) { pos_x -= 0.25f; face_left = true; }
    }
    int x = (int)pos_x;
    int bob = (!p->asleep && anim == A_NONE && (now_ms / 700) & 1) ? -1 : 0;
    int y = FLOOR - h + 1 + bob;
    int fr = now_ms / 400;

    expr_t e = base_expr(p);
    int tint = p->sick ? TINT_SICK : TINT_NONE;
    bool flip = face_left;
    int look = 0;
    float k = anim_k();
    switch (anim) {
    case A_EAT: case A_SNACK: case A_DRINK: e = EXPR_EAT; fr = now_ms / 250; flip = false; look = 1; break;
    case A_HAPPY: case A_HEARTS: case A_HATCH: case A_CLEAN: e = EXPR_HAPPY; break;
    case A_LAUGH: e = EXPR_HAPPY; x += (now_ms / 100) & 1; fr = now_ms / 120; break;
    case A_NO: e = EXPR_ANGRY; flip = (now_ms / 150) & 1; break;
    case A_GRUMPY: e = EXPR_ANGRY; break;
    case A_SCOLD: case A_SAD: e = EXPR_SAD; break;
    case A_MED: e = k < 0.5f ? EXPR_SURPRISE : EXPR_HAPPY; break;
    case A_EVOLVE: if ((now_ms / 150) & 1) tint = TINT_WHITE; e = EXPR_HAPPY; break;
    default: break;
    }
    art_pet(p->stage, p->form, x, y, e, fr, flip, look, tint);

    // effetti
    switch (anim) {
    case A_EAT: case A_SNACK: case A_DRINK: {
        const sprite_t *s = anim == A_EAT ? &SPR_FISH : anim == A_SNACK ? &SPR_COOKIE : &SPR_GLASS;
        int ix = x + w + 1;
        if (ix + s->w > LW) ix = LW - s->w;
        int iy = FLOOR - s->h + 1 - (h > 10 ? 3 : 0);
        if (anim == A_DRINK) {
            art_sprite(s, ix, iy, false);
            int by = iy - (int)(k * 12) % 6;
            art_sprite(&SPR_BUBBLE, ix - 2, by, false);
        } else {
            art_sprite_from(s, ix, iy, (int)(k * 4) * s->w / 3);   // tre morsi
        }
        break;
    }
    case A_HAPPY: case A_HEARTS: case A_HATCH:
        art_sprite(&SPR_HEART, x - 3, y - 2 - (int)(k * 8), false);
        art_sprite(&SPR_HEART, x + w - 2, y - 5 - (int)(k * 8), false);
        if (anim == A_HATCH) {
            art_sprite(&SPR_SPARK, x - 5, y + 2, false);
            art_sprite(&SPR_SPARK, x + w + 2, y + 4, false);
        }
        break;
    case A_LAUGH:
        for (int i = 0; i < 4; i++)
            art_sprite(&SPR_BUBBLE, x + (i * 5) % (w + 4) - 2, y - 2 - (int)((k * 14 + i * 3)) % 14, false);
        break;
    case A_MED: {
        int px0 = LW - 8, px1 = x + w;
        int pxp = px0 + (int)((px1 - px0) * (k < 0.5f ? k * 2 : 1));
        if (k < 0.5f) art_sprite(&SPR_PILL, pxp, y + 3, false);
        else art_sprite(&SPR_SPARK, x + w / 2 - 1, y - 4 - (int)(k * 4), false);
        break;
    }
    case A_SCOLD:
        if ((now_ms / 200) & 1) art_sprite(&SPR_BANG, x + w + 2, y, false);
        break;
    case A_GRUMPY:
        art_sprite(&SPR_ANGER, x + w - 1, y - 3, false);
        break;
    case A_EVOLVE:
        for (int i = 0; i < 3; i++)
            if (((now_ms / 200) + i) & 1) art_sprite(&SPR_SPARK, x - 4 + i * (w / 2 + 3), y - 4 + (i & 1) * 6, false);
        break;
    default:
        break;
    }

    // stato fisso
    if (anim == A_NONE) {
        if (p->sick && (now_ms / 500) & 1) art_sprite(&SPR_SKULL, x - 6 < 0 ? x + w + 1 : x - 6, y, false);
        if (p->tantrum) art_sprite(&SPR_ANGER, x + w - 1, y - 3, false);
    }
    art_brightness(255);
    if (p->asleep) {
        int zt = (now_ms / 600) % 3;
        art_sprite(&SPR_Z, x + w + 1, y - zt * 3, false);
        if (zt) art_sprite(&SPR_Z, x + w + 4, y - 6 - zt * 2, false);
    }
}

static void draw_dead(const pet_t *p)
{
    art_background(now_ms / 100);
    draw_bubbles();
    int w, h;
    art_pet_size(PET_DEAD, p->form, &w, &h);
    int x = (LW - w) / 2;
    if (p->death == DEATH_OLD) {
        // torna negli abissi: risale e sparisce, poi ricomincia
        int y = FLOOR - h + 1 - (int)((now_ms / 120) % (FLOOR + 6));
        art_pet(PET_ADULT, p->form, x, y, EXPR_HAPPY, now_ms / 250, false, 0, TINT_NONE);
    } else {
        int y = 6 + (((now_ms / 600) & 1) ? 1 : 0);
        art_pet(PET_DEAD, p->form, x, y, EXPR_SLEEP, now_ms / 500, false, 0, TINT_GHOST);
        art_sprite(&SPR_HALO, x + w / 2 - 3, y - 3, false);
    }
}

static void draw_stats(const pet_t *p)
{
    art_fill(0x0B1C2C);
    int w, h;
    pet_dims(p, &w, &h);
    switch (page) {
    case 1: case 3:
        for (int i = 0; i < PET_MAX; i++) {
            int v = page == 1 ? p->hunger : p->happy;
            if (i < v) art_sprite(&SPR_HEART_BIG, 8 + i * 13, 13, false);
            else art_sprite_color(&SPR_HEART_BIG, 8 + i * 13, 13, 0x3A4048);
        }
        break;
    case 2:
        for (int i = 0; i < PET_MAX; i++) {
            if (i < p->thirst) art_sprite(&SPR_DROP_BIG, 10 + i * 13, 12, false);
            else art_sprite_color(&SPR_DROP_BIG, 10 + i * 13, 12, 0x3A4048);
        }
        break;
    case 4:
        art_rect(10, 12, 44, 8, 0x3A4048);
        art_rect(11, 13, 42, 6, 0x0B1C2C);
        for (int i = 0; i < p->discipline; i++) art_rect(12 + i * 10, 14, 9, 4, 0xFFD23C);
        break;
    default:
        if (p->stage == PET_EGG) art_egg((LW - EGG_W) / 2, FLOOR - EGG_H + 1, 0);
        else art_pet(p->stage, p->form, (LW - w) / 2, FLOOR - h + 1, page == 5 ? EXPR_HAPPY : EXPR_NORMAL,
                     page == 5 ? now_ms / 150 : now_ms / 400, false, 0, TINT_NONE);
        if (page == 6) art_sprite(&SPR_SPARK, (LW - w) / 2 - 5, 8, false);
        break;
    }
}

static void draw_lr(const pet_t *p)
{
    art_background(now_ms / 100);
    int w, h;
    pet_dims(p, &w, &h);
    int x = (LW - w) / 2, y = FLOOR - h + 1;
    if (!lr_t0) {
        art_pet(p->stage, p->form, x, y, EXPR_NORMAL, now_ms / 400, false, 0, TINT_NONE);
        if ((now_ms / 400) & 1) art_sprite(&SPR_QUESTION, x + w / 2 - 1, y - 7, false);
    } else {
        bool ok = lr_guess == lr_dir;
        art_pet(p->stage, p->form, x + lr_dir * 3, y, ok ? EXPR_HAPPY : EXPR_SAD, now_ms / 200, lr_dir < 0, lr_dir, TINT_NONE);
        if (ok) art_sprite(&SPR_HEART, x + w / 2 - 2, y - 6, false);
    }
}

static void draw_fish(const pet_t *p)
{
    art_background(now_ms / 100);
    for (int i = 0; i < 6; i++)
        if (drops[i].on) art_sprite(drops[i].jelly ? &SPR_JELLY : &SPR_FISH, (int)drops[i].x, (int)drops[i].y, false);
    int w, h;
    pet_dims(p, &w, &h);
    bool stun = !reached(now_ms, fish_stun);
    expr_t e = stun ? EXPR_SICK : (now_ms - fish_last < 300 ? EXPR_EAT : EXPR_HAPPY);
    art_pet(p->stage, p->form, (int)fish_x, FLOOR - h + 1, e, now_ms / 200, false, 0, stun ? TINT_SICK : TINT_NONE);
}

static void draw_walk(const pet_t *p)
{
    art_background(0);
    for (int i = 0; i < 6; i++) {
        int px = ((i * 13 - walk_scroll) % (LW + 4) + LW + 4) % (LW + 4) - 2;
        art_rect(px, 30, 2, 1, 0x8C939B);
    }
    int w, h;
    pet_dims(p, &w, &h);
    bool moving = now_ms - walk_last_ms < 1500;
    int bob = moving && (now_ms / 150) & 1 ? -1 : 0;
    art_pet(p->stage, p->form, (LW - w) / 2, FLOOR - h + 1 + bob, moving ? EXPR_HAPPY : EXPR_NORMAL,
            moving ? now_ms / 150 : now_ms / 400, false, 1, TINT_NONE);
    if (anim == A_HEARTS) art_sprite(&SPR_HEART, (LW - w) / 2 + w, FLOOR - h - 3 - (int)(anim_k() * 6), false);
}

static void draw_icons(const pet_t *p)
{
    art_begin(icon_buf, IC_LW, IC_LH, IC_SC, icon_stride);
    art_fill(0x000000);
    bool menu = mode == M_MAIN || mode == M_FOOD || mode == M_PLAY || mode == M_STATS;
    for (int i = 0; i < PICON_COUNT; i++) {
        uint32_t col = mode == M_MAIN ? 0xEDEDED : C_DIM_RGB;
        if (i == PICON_CALL) {
            col = p->needs ? (((now_ms / 400) & 1) ? 0xFF6B57 : C_DIM_RGB) : 0x24282D;
        } else if (i == sel && menu) {
            art_rect(i * IC_SLOT, 0, IC_SLOT - 1, IC_LH, mode == M_MAIN ? accent_rgb() : C_DIM_RGB);
            col = 0x000000;
        }
        art_sprite_color(&PET_ICONS[i], i * IC_SLOT + 2, 1, col);
    }
    lv_obj_invalidate(icons);
}

/* ---------------- pannello di testo ---------------- */

static const char *const main_labels[N_SEL] = {"Cibo", "Gioca", "Pulisci", "Medicina", "Luce", "Sgrida", "Stato"};
static const char *const food_labels[] = {"Pasto", "Spuntino", "Acqua"};
static const char *const play_labels[] = {"Da che parte?", "Pesca", "Passeggiata"};
static const char *const play_hints[] = {
    "Indovina dove guarderà: 5 round",
    "Inclina la scheda e raccogli i pesci",
    "Portalo a spasso: conta i passi",
};
static const char *const stats_labels[] = {"Età e peso", "Fame", "Sete", "Felicità", "Disciplina", "Passi", "Ricordi"};
#define N_PAGES 7

static void draw_panel(const pet_t *p)
{
    char t[96], a[24], b[24];
    set_text(l_title, stage_name(p));

    if (p->stage == PET_DEAD) {
        fmt_age(p->best_age_s, b, sizeof(b));
        snprintf(t, sizeof(t), "generazione %u · record %s", p->generation, b);
        set_text(l_info, t);
        static const char *const why[] = {"", "Di fame…", "Di sete…", "Di malattia…", "Buon viaggio!"};
        set_text(l_main, why[p->death <= DEATH_OLD ? p->death : 0]);
        fmt_age(p->age_s, a, sizeof(a));
        snprintf(t, sizeof(t), "%s · visse %s · swipe a destra: nuovo uovo",
                 p->death == DEATH_OLD ? "È tornato nel grande oceano" : "È diventato un angioletto", a);
        set_text(l_hint, t);
        lv_obj_set_style_text_color(l_hint, C_DIM, 0);
        return;
    }

    if (p->stage == PET_EGG) snprintf(t, sizeof(t), "si schiude tra %lu s", (unsigned long)pet_core_egg_left(p));
    else {
        fmt_age(p->age_s, a, sizeof(a));
        snprintf(t, sizeof(t), "età %s · %u g · generazione %u", a, p->weight, p->generation);
    }
    set_text(l_info, t);

    const char *hint = NULL;
    bool warn = false;
    switch (mode) {
    case M_MAIN:
        if (sel == 4) set_text(l_main, p->light_off ? "Accendi la luce" : "Spegni la luce");
        else set_text(l_main, main_labels[sel]);
        hint = need_text(p);
        warn = hint != NULL;
        if (!hint) hint = "Su/giù sceglie · destra conferma · tieni premuto: coccole";
        break;
    case M_FOOD:
        set_text(l_main, food_labels[sub]);
        snprintf(t, sizeof(t), "Fame %d/4 · sete %d/4 · peso %u g", p->hunger, p->thirst, p->weight);
        hint = t;
        break;
    case M_PLAY:
        set_text(l_main, play_labels[sub]);
        hint = play_hints[sub];
        break;
    case M_STATS:
        set_text(l_main, stats_labels[page]);
        switch (page) {
        case 0: fmt_age(p->age_s, a, sizeof(a)); snprintf(t, sizeof(t), "%s · peso %u g", a, p->weight); break;
        case 1: snprintf(t, sizeof(t), "%d su 4", p->hunger); break;
        case 2: snprintf(t, sizeof(t), "%d su 4", p->thirst); break;
        case 3: snprintf(t, sizeof(t), "%d su 4", p->happy); break;
        case 4: snprintf(t, sizeof(t), "%d%%", p->discipline * 25); break;
        case 5: snprintf(t, sizeof(t), "Oggi %lu · in tutto %lu", (unsigned long)p->steps_today, (unsigned long)p->steps_total); break;
        default:
            fmt_age(p->best_age_s, b, sizeof(b));
            snprintf(t, sizeof(t), "Generazione %u · record %s", p->generation, p->best_age_s ? b : "nessuno");
            break;
        }
        hint = t;
        break;
    case M_LR:
        snprintf(a, sizeof(a), "Round %d di 5", lr_round + 1 > 5 ? 5 : lr_round + 1);
        set_text(l_main, a);
        if (lr_t0) hint = lr_guess == lr_dir ? "Giusto!" : "Sbagliato…";
        else hint = "Swipe a sinistra o a destra · BOOT esce";
        break;
    case M_FISH: {
        snprintf(a, sizeof(a), "Pesci: %d", fish_score);
        set_text(l_main, a);
        uint32_t el = now_ms - fish_t0;
        snprintf(t, sizeof(t), "Inclina la scheda · %lu s · BOOT esce",
                 (unsigned long)(el >= FISH_MS ? 0 : (FISH_MS - el + 999) / 1000));
        hint = t;
        break;
    }
    case M_WALK:
        snprintf(a, sizeof(a), "Passi: %lu", (unsigned long)(p->steps_total - walk_steps0));
        set_text(l_main, a);
        snprintf(t, sizeof(t), "Oggi %lu · cammina con la scheda in tasca", (unsigned long)p->steps_today);
        hint = t;
        break;
    }
    if (!reached(now_ms, msg_until)) { hint = msg; warn = msg_warn; }
    set_text(l_hint, hint ? hint : "");
    lv_obj_set_style_text_color(l_hint, warn ? C_WARN : C_DIM, 0);
}

/* ---------------- azioni ---------------- */

static void feed(pet_action_t a)
{
    pet_result_t r;
    pet_do(a, &r);
    switch (r) {
    case RES_OK:     start(a == ACT_WATER ? A_DRINK : a == ACT_SNACK ? A_SNACK : A_EAT, 0); break;
    case RES_FULL:   start(A_NO, 0); say(a == ACT_WATER ? "Non ha sete" : "È sazio!", false); break;
    case RES_REFUSE: start(A_NO, 0); say("Capriccio: non vuole! Sgridalo", true); break;
    case RES_ASLEEP: say("Sta dormendo…", false); break;
    default: break;
    }
}

static bool refuse_play(const pet_t *p)
{
    if (p->asleep) { say("Sta dormendo…", false); return true; }
    if (p->tantrum && (esp_random() & 1)) { start(A_NO, 0); say("Capriccio: non vuole giocare. Sgridalo", true); return true; }
    return false;
}

static void lr_start(void)
{
    mode = M_LR;
    lr_round = lr_score = 0;
    lr_t0 = 0;
}

static void game_result(bool win, bool big, const char *text)
{
    pet_result_t r;
    pet_do(big ? ACT_GAME_BIG_WIN : win ? ACT_GAME_WIN : ACT_GAME_LOSE, &r);
    mode = M_MAIN;
    start(win ? A_HAPPY : A_SAD, 0);
    say(text, false);
}

static void lr_update(void)
{
    if (!lr_t0 || now_ms - lr_t0 < 1300) return;
    lr_t0 = 0;
    if (++lr_round < 5) return;
    char t[48];
    bool win = lr_score >= 3;
    snprintf(t, sizeof(t), win ? "Hai vinto %d a %d!" : "Hai perso %d a %d", lr_score, 5 - lr_score);
    game_result(win, lr_score == 5, t);
}

static void fish_start(const pet_t *p)
{
    int w, h;
    pet_dims(p, &w, &h);
    tilt_cal();
    mode = M_FISH;
    fish_x = (LW - w) / 2.0f;
    fish_score = 0;
    fish_t0 = now_ms;
    fish_next = now_ms + 600;
    fish_stun = now_ms;
    fish_last = 0;
    memset(drops, 0, sizeof(drops));
    lv_timer_set_period(tmr, 40);
}

static void fish_end(bool aborted)
{
    lv_timer_set_period(tmr, 100);
    if (aborted) { mode = M_PLAY; say("Partita annullata", false); return; }
    char t[48];
    snprintf(t, sizeof(t), "Hai pescato %d pesci!", fish_score);
    game_result(fish_score >= 5, fish_score >= 10, t);
}

static void fish_update(const pet_t *p, uint32_t dt)
{
    int w, h;
    pet_dims(p, &w, &h);
    if (dt > 100) dt = 100;
    if (reached(now_ms, fish_stun)) fish_x += tilt_now() * 0.045f * dt;
    fish_x = clampf(fish_x, 0, LW - w);

    if (reached(now_ms, fish_next)) {
        fish_next = now_ms + 550 + rnd(500);
        for (int i = 0; i < 6; i++)
            if (!drops[i].on) {
                drops[i] = (drop_t){.x = 1 + rnd(LW - 8), .y = -5, .vy = 0.010f + rnd(80) / 10000.0f,
                                    .jelly = rnd(4) == 0, .on = 1};
                break;
            }
    }
    int top = FLOOR - h + 1;
    for (int i = 0; i < 6; i++) {
        drop_t *d = &drops[i];
        if (!d->on) continue;
        d->y += d->vy * dt;
        int dw = d->jelly ? 5 : 7;
        if (d->y + 4 >= top && d->y < FLOOR && d->x + dw > fish_x && d->x < fish_x + w) {
            d->on = 0;
            if (d->jelly) {
                fish_stun = now_ms + 1200;
                if (fish_score > 0) fish_score--;
                pet_play(SND_NO);
            } else {
                fish_score++;
                fish_last = now_ms;
                pet_play(SND_TICK);
            }
        } else if (d->y > FLOOR) {
            d->on = 0;
        }
    }
    if (now_ms - fish_t0 >= FISH_MS) fish_end(false);
}

static void walk_start(const pet_t *p)
{
    mode = M_WALK;
    walk_steps0 = walk_seen = p->steps_total;
    walk_last_ms = 0;
    pet_set_walking(true);
}

static void walk_end(const pet_t *p)
{
    char t[64];
    pet_set_walking(false);
    mode = M_PLAY;
    snprintf(t, sizeof(t), "Passeggiata finita: %lu passi", (unsigned long)(p->steps_total - walk_steps0));
    say(t, false);
}

static void cuddle(const pet_t *p)
{
    if (!pet_core_alive(p)) return;
    pet_result_t r;
    pet_do(ACT_PET, &r);
    if (r == RES_ASLEEP) say("Shh… sta dormendo", false);
    else { start(A_HEARTS, 0); say(r == RES_OK ? "Adora le coccole!" : "Ancora coccole!", false); }
}

static void activate(pet_t *p)
{
    pet_result_t r;
    if (p->stage == PET_EGG) { say("È ancora un uovo: aspetta che si schiuda", false); return; }
    switch (sel) {
    case 0: mode = M_FOOD; sub = 0; break;
    case 1: if (!p->asleep) { mode = M_PLAY; sub = 0; } else say("Sta dormendo…", false); break;
    case 2: {
        int before = p->poop;
        pet_do(ACT_CLEAN, &r);
        if (r == RES_OK) start(A_CLEAN, before);
        else say("È già tutto pulito!", false);
        break;
    }
    case 3:
        pet_do(ACT_MEDICINE, &r);
        if (r == RES_OK) { start(A_MED, 0); say(p->sick ? "Ancora una dose…" : "Guarito!", false); }
        else { start(A_NO, 0); say("Non è malato", false); }
        break;
    case 4:
        pet_do(ACT_LIGHT, &r);
        say(p->light_off ? "Luce spenta. Buonanotte!" : "Luce accesa", false);
        break;
    case 5:
        pet_do(ACT_SCOLD, &r);
        if (r == RES_OK) { start(A_SCOLD, 0); say("Ha capito la lezione!", false); }
        else if (r == RES_SAD) { start(A_SAD, 0); say("Non aveva fatto niente… ora è triste", true); }
        else if (r == RES_ASLEEP) say("Sta dormendo…", false);
        break;
    case 6: mode = M_STATS; page = 0; break;
    }
}

/* ---------------- ciclo ---------------- */

static void handle_events(pet_t *p)
{
    uint32_t ev = pet_take_events();
    char t[64];
    if (ev & EV_DEATH) {
        if (mode == M_WALK) pet_set_walking(false);
        if (mode == M_FISH) lv_timer_set_period(tmr, 100);
        mode = M_MAIN;
        anim = A_NONE;
        return;
    }
    if (ev & EV_HATCH) { start(A_HATCH, 0); say("È nato il tuo polipetto!", false); }
    else if (ev & EV_EVOLVE) {
        start(A_EVOLVE, 0);
        snprintf(t, sizeof(t), "Si è evoluto: %s!", stage_name(p));
        say(t, false);
    } else if ((ev & EV_HAPPY) && anim == A_NONE) {
        start(A_HEARTS, 0);
        if (mode == M_WALK) say("Che bella passeggiata!", false);
    }
    if (ev & EV_ELDER) say("Ha 25 giorni! Ora vive per sempre, o puoi lasciarlo partire (Impostazioni)", false);
    else if (ev & EV_SICK) say("Si è ammalato!", true);
    else if (ev & EV_SLEEP) say("Si è addormentato", false);
    else if (ev & EV_WAKE) say("Buongiorno!", false);
    else if (ev & EV_POOP) say("Ha fatto un po' d'inchiostro", false);

    // scossoni: lo fanno ridere (o lo svegliano di soprassalto)
    if (pet_take_shakes() && mode == M_MAIN && anim == A_NONE && pet_core_alive(p)) {
        pet_result_t r;
        pet_do(ACT_SHAKE, &r);
        if (r == RES_WOKE) { start(A_GRUMPY, 0); say("L'hai svegliato! Torna a dormire…", true); }
        else { start(A_LAUGH, 0); say("Ahah! Bolle d'inchiostro!", false); }
    }
}

static void frame_cb(lv_timer_t *t)
{
    now_ms = lv_tick_get();
    uint32_t dt = last_ms ? now_ms - last_ms : 0;
    last_ms = now_ms;
    pet_t *p = pet_get();

    handle_events(p);
    if (anim != A_NONE && now_ms - anim_t0 >= anim_ms[anim]) anim = A_NONE;
    if (mode == M_LR) lr_update();
    if (mode == M_FISH) fish_update(p, dt);
    if (mode == M_WALK && p->steps_total != walk_seen) {
        walk_scroll += (int)(p->steps_total - walk_seen) * 2;
        walk_seen = p->steps_total;
        walk_last_ms = now_ms;
    }

    // a schermo spento si fa avanzare il gioco ma non si disegna (si riprende alla riaccensione)
    if (display_is_dark()) return;

    art_begin(scene_buf, LW, LH, SC, scene_stride);
    if (p->stage == PET_DEAD) draw_dead(p);
    else switch (mode) {
        case M_STATS: draw_stats(p); break;
        case M_LR:    draw_lr(p); break;
        case M_FISH:  draw_fish(p); break;
        case M_WALK:  draw_walk(p); break;
        default:      draw_main(p); break;
        }
    lv_obj_invalidate(scene);
    draw_icons(p);
    draw_panel(p);
}

static bool nav(nav_t ev)
{
    if (!scene_buf || !icon_buf) return false;   // memoria non disponibile: l'app non è partita
    pet_t *p = pet_get();
    if (p->stage == PET_DEAD) {
        if (ev == NAV_SELECT) {
            pet_new_egg();
            mode = M_MAIN;
            anim = A_NONE;
            say("Un nuovo uovo! Si schiude tra un minuto", false);
            return true;
        }
        return ev == NAV_QUICK;
    }
    switch (mode) {
    case M_MAIN:
        if (ev == NAV_NEXT) { sel = (sel + 1) % N_SEL; return true; }
        if (ev == NAV_PREV) { sel = (sel + N_SEL - 1) % N_SEL; return true; }
        if (ev == NAV_QUICK) { cuddle(p); return true; }
        if (ev != NAV_SELECT) return false;   // sinistra / BOOT: esce dall'app
        if (anim == A_NONE) activate(p);
        return true;

    case M_FOOD:
    case M_PLAY:
        if (ev == NAV_NEXT) { sub = (sub + 1) % 3; return true; }
        if (ev == NAV_PREV) { sub = (sub + 2) % 3; return true; }
        if (ev == NAV_BACK) { mode = M_MAIN; return true; }
        if (ev == NAV_QUICK) { cuddle(p); return true; }
        if (ev != NAV_SELECT || anim != A_NONE) return ev != NAV_BTN;
        if (mode == M_FOOD) {
            static const pet_action_t acts[] = {ACT_MEAL, ACT_SNACK, ACT_WATER};
            feed(acts[sub]);
        } else if (!refuse_play(p)) {
            if (sub == 0) lr_start();
            else if (sub == 1) {
                if (board_imu_ok()) fish_start(p);
                else say("Accelerometro non disponibile", true);
            } else if (!g_set.pet_steps) say("Attiva il contapassi in Impostazioni › Polipetto", true);
            else walk_start(p);
        }
        return true;

    case M_STATS:
        if (ev == NAV_NEXT || ev == NAV_SELECT) { page = (page + 1) % N_PAGES; return true; }
        if (ev == NAV_PREV) { page = (page + N_PAGES - 1) % N_PAGES; return true; }
        if (ev == NAV_BACK) { mode = M_MAIN; return true; }
        return ev != NAV_BTN;

    case M_LR:
        if (ev == NAV_BTN) { mode = M_PLAY; say("Partita annullata", false); return true; }
        if (!lr_t0 && (ev == NAV_BACK || ev == NAV_SELECT)) {
            lr_guess = ev == NAV_SELECT ? 1 : -1;
            lr_dir = (esp_random() & 1) ? 1 : -1;
            if (lr_guess == lr_dir) { lr_score++; pet_play(SND_TICK); }
            lr_t0 = now_ms ? now_ms : 1;
        }
        return true;

    case M_FISH:
        if (ev == NAV_BTN || ev == NAV_BACK) fish_end(true);
        return true;

    case M_WALK:
        if (ev == NAV_BTN || ev == NAV_BACK) walk_end(p);
        return true;
    }
    return false;
}

/* ---------------- ciclo di vita dell'app ---------------- */

static lv_obj_t *mk_label(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, PX, y);
    lv_obj_set_width(l, PW);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    // scadenze "già passate" ma vicine: con il confronto che regge al giro del contatore,
    // uno 0 sembrerebbe nel futuro dopo ~25 giorni di accensione
    msg_until = next_wander = fish_stun = lv_tick_get();
    if (!scene_buf) {
        uint32_t ss = lv_draw_buf_width_to_stride(LW * SC, LV_COLOR_FORMAT_RGB565);
        uint32_t is = lv_draw_buf_width_to_stride(IC_LW * IC_SC, LV_COLOR_FORMAT_RGB565);
        scene_buf = heap_caps_aligned_alloc(64, ss * LH * SC, MALLOC_CAP_SPIRAM);
        icon_buf = heap_caps_aligned_alloc(64, is * IC_LH * IC_SC, MALLOC_CAP_SPIRAM);
        scene_stride = ss / 2;
        icon_stride = is / 2;
    }
    if (!scene_buf || !icon_buf) {
        lv_obj_t *l = mk_label(root, &font_m, C_WARN, 70);
        lv_label_set_text(l, "Memoria insufficiente");
        return;
    }

    scene = lv_canvas_create(root);
    lv_canvas_set_buffer(scene, scene_buf, LW * SC, LH * SC, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(scene, 8, 6);

    l_title = mk_label(root, &font_m, ui_accent(), 6);
    l_info = mk_label(root, &font_s, C_DIM, 32);
    icons = lv_canvas_create(root);
    lv_canvas_set_buffer(icons, icon_buf, IC_LW * IC_SC, IC_LH * IC_SC, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(icons, PX, 54);
    l_main = mk_label(root, &font_l, C_TEXT, 90);
    l_hint = mk_label(root, &font_s, C_DIM, 132);
    lv_label_set_long_mode(l_hint, LV_LABEL_LONG_WRAP);

    pet_set_foreground(true);
    pet_t *p = pet_get();
    if (p->stage == PET_NONE) {
        pet_new_egg();
        say("Ecco il tuo uovo! Si schiude tra un minuto", false);
    }
    if (mode == M_WALK) pet_set_walking(true);
    if (mode == M_FISH || mode == M_LR) mode = M_MAIN;   // le partite non riprendono
    anim = A_NONE;
    last_ms = 0;
    tmr = lv_timer_create(frame_cb, 100, NULL);
    frame_cb(NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    pet_set_walking(false);
    pet_set_foreground(false);
    pet_save();
}

const app_t app_pet = {
    .name = "Polipetto", .icon = ICON_GAMEPAD,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

/* ---------------- Impostazioni › Polipetto ---------------- */

// ordine in cui le modalità compaiono scorrendo (i valori salvati restano quelli dell'enum)
static const uint8_t time_order[PET_TIME_COUNT] = {PET_TIME_HYBRID, PET_TIME_REAL, PET_TIME_DEVICE, PET_TIME_APP};
static const char *const time_names[PET_TIME_COUNT] = {
    [PET_TIME_REAL] = "Tempo reale", [PET_TIME_DEVICE] = "Solo a scheda accesa",
    [PET_TIME_APP] = "Solo con l'app aperta", [PET_TIME_HYBRID] = "Ibrida: reale, ma la notte è protetta",
};

static void v_time(char *b, int n)
{
    int m = g_set.pet_time % PET_TIME_COUNT;
    bool real = m == PET_TIME_REAL || m == PET_TIME_HYBRID;
    if (real && !pet_time_known()) snprintf(b, n, "%s (ora non impostata: avanza solo a scheda accesa)", time_names[m]);
    else snprintf(b, n, "%s", time_names[m]);
}
static void j_time(int d)
{
    int i = 0;
    while (i < PET_TIME_COUNT && time_order[i] != g_set.pet_time) i++;
    i = ((i < PET_TIME_COUNT ? i : 0) + d + PET_TIME_COUNT) % PET_TIME_COUNT;
    g_set.pet_time = time_order[i];
    settings_save();
}
static void v_hours(char *b, int n, int h)
{
    if (g_set.pet_time == PET_TIME_HYBRID) snprintf(b, n, "%02d:00", h);
    else snprintf(b, n, "%02d:00 · vale nella modalità ibrida", h);
}
static void v_sleep_h(char *b, int n) { v_hours(b, n, g_set.pet_sleep_h); }
static void v_wake_h(char *b, int n) { v_hours(b, n, g_set.pet_wake_h); }
static void j_sleep_h(int d) { g_set.pet_sleep_h = (g_set.pet_sleep_h + d + 24) % 24; settings_save(); }
static void j_wake_h(int d) { g_set.pet_wake_h = (g_set.pet_wake_h + d + 24) % 24; settings_save(); }
static void v_release(char *b, int n)
{
    const pet_t *p = pet_get();
    if (!pet_core_alive(p)) snprintf(b, n, "Quando avrà 25 giorni");
    else if (pet_core_can_release(p)) snprintf(b, n, "Ha 25 giorni: se vuoi, può partire");
    else snprintf(b, n, "Dai 25 giorni · mancano %lu giorni", (unsigned long)((PET_RELEASE_S - p->age_s + 86399) / 86400));
}
static void a_release(void)
{
    if (pet_release()) ui_toast("Buon viaggio, polipetto!");
    else ui_toast("Potrà partire quando avrà 25 giorni");
}
static void v_sound(char *b, int n) { snprintf(b, n, "%s", g_set.pet_sound ? "Acceso" : "Spento"); }
static void a_sound(void)
{
    g_set.pet_sound = !g_set.pet_sound;
    settings_save();
    if (g_set.pet_sound) pet_play(SND_CALL);
}
static void a_listen(void)
{
    if (!g_set.pet_sound) { ui_toast("I versi sono spenti"); return; }
    pet_play(SND_CALL);
}
static void v_steps(char *b, int n)
{
    if (g_set.pet_steps) snprintf(b, n, "Acceso · oggi %lu passi", (unsigned long)pet_get()->steps_today);
    else snprintf(b, n, "Spento");
}
static void a_steps(void) { g_set.pet_steps = !g_set.pet_steps; settings_save(); }
static void v_tilt(char *b, int n) { snprintf(b, n, "%s", g_set.pet_tilt_inv ? "Invertita" : "Normale"); }
static void a_tilt(void) { g_set.pet_tilt_inv = !g_set.pet_tilt_inv; settings_save(); }
static void v_state(char *b, int n)
{
    const pet_t *p = pet_get();
    char a[24];
    if (p->stage == PET_NONE) { snprintf(b, n, "Non ancora nato: apri l'app"); return; }
    fmt_age(p->age_s, a, sizeof(a));
    snprintf(b, n, "%s · %s", stage_name(p), a);
}
static void a_egg(void) { pet_new_egg(); ui_toast("Nuovo uovo pronto"); }

static const menu_item_t pet_items[] = {
    {.icon = ICON_GAMEPAD, .label = "Gioca con il polipetto", .value = v_state, .app = &app_pet},
    {.icon = ICON_CLOCK, .label = "Scorrere del tempo", .value = v_time, .on_adjust = j_time},
    {.icon = ICON_MOON, .label = "Ora della nanna", .value = v_sleep_h, .on_adjust = j_sleep_h},
    {.icon = ICON_SUN, .label = "Ora della sveglia", .value = v_wake_h, .on_adjust = j_wake_h},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Versi", .value = v_sound, .on_select = a_sound},
    {.icon = LV_SYMBOL_PLAY, .label = "Ascolta il verso", .hint = "Blub-blub-pii!", .on_select = a_listen},
    {.icon = LV_SYMBOL_GPS, .label = "Contapassi", .value = v_steps, .on_select = a_steps},
    {.icon = ICON_SLIDERS, .label = "Inclinazione nel gioco", .value = v_tilt, .on_select = a_tilt},
    {.icon = LV_SYMBOL_UPLOAD, .label = "Lascialo tornare nell'oceano", .value = v_release, .on_select = a_release, .confirm = true},
    {.icon = LV_SYMBOL_REFRESH, .label = "Ricomincia da un uovo", .hint = "Il polipetto attuale se ne va", .on_select = a_egg, .confirm = true},
};
menu_t pet_settings_menu = {"Impostazioni › Polipetto", pet_items, sizeof(pet_items) / sizeof(pet_items[0]), 0};
