// pet_core.c — regole del Polipetto. Ispirate ai Tamagotchi classici (fame, felicità,
// disciplina, errori di cura che decidono l'evoluzione, malattie, nanna con la luce),
// più sete e passi. Codice originale: nessun pezzo del firmware Bandai.
#include "pet_core.h"
#include <string.h>

typedef struct { uint16_t hunger, thirst, happy, poop, tantrum; } rates_t;

// secondi perché un indicatore perda una tacca (da sveglio)
static const rates_t rates[] = {
    [PET_BABY]  = {300,  270,  360,  600,  0},
    [PET_CHILD] = {1800, 1500, 2100, 5400, 4 * 3600},
    [PET_TEEN]  = {2700, 2400, 3000, 7200, 5 * 3600},
    [PET_ADULT] = {3600, 3000, 4200, 9000, 8 * 3600},
};

uint32_t pet_rand(uint32_t *rng)
{
    if (!*rng) *rng = 0x9E3779B9u;
    *rng ^= *rng << 13;
    *rng ^= *rng >> 17;
    *rng ^= *rng << 5;
    return *rng;
}

static uint32_t rnd(pet_t *p) { return pet_rand(&p->rng); }

/* ---------------- genetica ---------------- */

static const uint8_t n_alleles[GENE_COUNT] = {COL_COUNT, PAT_COUNT, TENT_COUNT, TEMP_COUNT};
// dominanza: più alto = si vede di più. L'oro è recessivo (serve da entrambi i genitori)
static const uint8_t dom_color[COL_COUNT] = {3, 2, 2, 1, 1, 1, 0};
static const uint8_t dom_pat[PAT_COUNT] = {1, 2, 0};
static const uint8_t dom_tent[TENT_COUNT] = {1, 1, 0};
static const uint8_t dom_temp[TEMP_COUNT] = {1, 1, 1};
static const uint8_t *const dom[GENE_COUNT] = {dom_color, dom_pat, dom_tent, dom_temp};

static const char *const allele_names[GENE_COUNT][7] = {
    {"Arancione", "Corallo", "Viola", "Azzurro", "Verde", "Rosa", "Oro"},
    {"Tinta unita", "A puntini", "A strisce"},
    {"Tentacoli normali", "Tentacoli lunghi", "Tentacoli corti"},
    {"Calmo", "Vivace", "Goloso"},
};
static const char *const gene_names[GENE_COUNT] = {"Colore", "Motivo", "Tentacoli", "Carattere"};

int pet_gene_count(int gene) { return gene >= 0 && gene < GENE_COUNT ? n_alleles[gene] : 0; }
const char *pet_gene_name(int gene) { return gene >= 0 && gene < GENE_COUNT ? gene_names[gene] : ""; }
const char *pet_allele_name(int gene, int a)
{
    if (gene < 0 || gene >= GENE_COUNT || a < 0 || a >= n_alleles[gene]) return "?";
    return allele_names[gene][a];
}

static uint8_t fix(int gene, uint8_t a) { return a < n_alleles[gene] ? a : 0; }

int pet_gene_show(const uint8_t g[2], int gene)
{
    uint8_t a = fix(gene, g[0]), b = fix(gene, g[1]);
    return dom[gene][b] > dom[gene][a] ? b : a;
}

// allele di un uovo selvatico: per lo più i più comuni, l'oro quasi mai
static uint8_t wild(int gene, uint32_t *rng)
{
    uint32_t r = pet_rand(rng) % 100;
    switch (gene) {
    case GENE_COLOR:
        if (r < 40) return COL_ORANGE;
        if (r < 52) return COL_CORAL;
        if (r < 64) return COL_PURPLE;
        if (r < 76) return COL_BLUE;
        if (r < 88) return COL_GREEN;
        if (r < 98) return COL_PINK;
        return COL_GOLD;
    case GENE_PATTERN: return r < 60 ? PAT_NONE : r < 85 ? PAT_SPOTS : PAT_STRIPES;
    case GENE_TENT:    return r < 55 ? TENT_NORMAL : r < 80 ? TENT_LONG : TENT_SHORT;
    default:           return (uint8_t)(r % TEMP_COUNT);
    }
}

void pet_genes_random(uint8_t g[GENE_COUNT][2], uint32_t *rng)
{
    for (int i = 0; i < GENE_COUNT; i++) { g[i][0] = wild(i, rng); g[i][1] = wild(i, rng); }
}

void pet_genes_child(uint8_t out[GENE_COUNT][2], const uint8_t a[GENE_COUNT][2], const uint8_t b[GENE_COUNT][2], uint32_t *rng)
{
    for (int i = 0; i < GENE_COUNT; i++) {
        out[i][0] = fix(i, a[i][pet_rand(rng) & 1]);
        out[i][1] = fix(i, b[i][pet_rand(rng) & 1]);
        for (int k = 0; k < 2; k++)   // mutazione: 1 allele su 40
            if (pet_rand(rng) % 40 == 0) out[i][k] = (uint8_t)(i == GENE_COLOR && pet_rand(rng) % 4 == 0 ? COL_GOLD : pet_rand(rng) % n_alleles[i]);
    }
}

/* ---------------- nomi ---------------- */

static const char *const names_m[] = {
    "Tentacolo", "Bolla", "Inchiostrino", "Ottavio", "Polpino", "Guizzo", "Corallo", "Spruzzo",
    "Nettuno", "Ricciolo", "Ventosa", "Pinolo", "Salsedine", "Gamberone", "Scoglio", "Marino",
};
static const char *const names_f[] = {
    "Bollicina", "Perla", "Ottavia", "Polpetta", "Spugna", "Medusina", "Marina", "Onda",
    "Conchiglia", "Ancora", "Stellina", "Alga", "Nereide", "Schiuma", "Laguna", "Ventosina",
};
static const char *const families[] = {
    "Abissi", "Scogliera", "Corallini", "Maree", "Fondali", "Lagunari", "Spumanti", "Inchiostri",
    "Polposi", "Salmastri", "Marosi", "Ventose", "Calamari", "Risacca", "Golfo", "Fari",
};
#define NN(a) (sizeof(a) / sizeof(a[0]))

const char *pet_random_name(int sex, uint32_t *rng)
{
    return sex == SEX_F ? names_f[pet_rand(rng) % NN(names_f)] : names_m[pet_rand(rng) % NN(names_m)];
}
const char *pet_random_family(uint32_t *rng) { return families[pet_rand(rng) % NN(families)]; }

static void copy_name(char *d, const char *s)
{
    int i = 0;
    for (; s && s[i] && i < PET_NAME_LEN - 1; i++) d[i] = s[i];
    d[i] = 0;
}

static void give_identity(pet_t *p)
{
    if (!p->sex) p->sex = (rnd(p) & 1) ? SEX_F : SEX_M;
    if (!p->name[0]) copy_name(p->name, pet_random_name(p->sex, &p->rng));
    if (!p->family[0]) copy_name(p->family, pet_random_family(&p->rng));
    if (!p->uid) p->uid = rnd(p) | 1;
}

void pet_core_upgrade(pet_t *p, uint32_t seed)
{
    if (p->magic != PET_MAGIC) return;
    if (!p->rng) p->rng = seed ? seed : 0x9E3779B9u;
    if (p->version < 2) {
        // il polipetto di prima resta com'era: arancione, tinta unita
        for (int i = 0; i < GENE_COUNT; i++) p->genes[i][0] = p->genes[i][1] = 0;
        p->genes[GENE_TEMPER][0] = p->genes[GENE_TEMPER][1] = (uint8_t)(rnd(p) % TEMP_COUNT);
        if (p->stage != PET_EGG) give_identity(p);
        else if (!p->family[0]) copy_name(p->family, pet_random_family(&p->rng));
    }
    for (int i = 0; i < GENE_COUNT; i++) { p->genes[i][0] = fix(i, p->genes[i][0]); p->genes[i][1] = fix(i, p->genes[i][1]); }
    p->name[PET_NAME_LEN - 1] = p->family[PET_NAME_LEN - 1] = 0;
    p->parent[0][PET_NAME_LEN - 1] = p->parent[1][PET_NAME_LEN - 1] = 0;
    if (p->sex > SEX_F) p->sex = SEX_NONE;
    p->version = PET_VERSION;
}

static int temper(const pet_t *p) { return pet_gene_show(p->genes[GENE_TEMPER], GENE_TEMPER); }

uint8_t pet_core_base_weight(int stage)
{
    switch (stage) {
    case PET_BABY:  return 5;
    case PET_CHILD: return 10;
    case PET_TEEN:  return 20;
    case PET_ADULT: return 30;
    default:        return 0;
    }
}

bool pet_core_alive(const pet_t *p) { return p->stage >= PET_BABY && p->stage <= PET_ADULT; }

bool pet_core_can_release(const pet_t *p) { return pet_core_alive(p) && p->age_s >= PET_RELEASE_S; }

uint32_t pet_core_egg_left(const pet_t *p)
{
    if (p->stage != PET_EGG) return 0;
    return p->stage_s >= PET_EGG_S ? 0 : PET_EGG_S - p->stage_s;
}

static void reset_timers(pet_t *p)
{
    const rates_t *r = &rates[p->stage];
    p->t_hunger = r->hunger;
    p->t_thirst = r->thirst;
    p->t_happy = r->happy;
    p->t_poop = r->poop;
    p->t_tantrum = r->tantrum ? r->tantrum / 2 + rnd(p) % r->tantrum : 0;
    p->t_check = 600;
}

static void set_stage(pet_t *p, pet_stage_t s)
{
    uint8_t old_base = pet_core_base_weight(p->stage);
    p->stage = s;
    p->stage_s = 0;
    p->mistakes_stage = 0;
    p->snacks_stage = 0;
    p->steps_stage = 0;
    p->tantrum = 0;
    int w = p->weight + pet_core_base_weight(s) - old_base;
    p->weight = w < pet_core_base_weight(s) ? pet_core_base_weight(s) : w > 99 ? 99 : w;
    reset_timers(p);
}

void pet_core_new_egg_from(pet_t *p, uint32_t seed, const pet_egg_t *egg)
{
    uint16_t gen = p->magic == PET_MAGIC ? p->generation : 0;
    uint32_t best = p->magic == PET_MAGIC ? p->best_age_s : 0;
    char fam[PET_NAME_LEN];
    copy_name(fam, p->magic == PET_MAGIC ? p->family : "");
    memset(p, 0, sizeof(*p));
    p->magic = PET_MAGIC;
    p->version = PET_VERSION;
    p->generation = gen + 1;
    p->best_age_s = best;
    p->rng = seed ? seed : 0x9E3779B9u;
    p->stage = PET_EGG;
    p->hunger = p->thirst = p->happy = 2;
    if (egg) {
        memcpy(p->genes, egg->genes, sizeof(p->genes));
        for (int i = 0; i < GENE_COUNT; i++) { p->genes[i][0] = fix(i, p->genes[i][0]); p->genes[i][1] = fix(i, p->genes[i][1]); }
        copy_name(p->parent[0], egg->parent[0]);
        copy_name(p->parent[1], egg->parent[1]);
        copy_name(p->family, egg->family[0] ? egg->family : fam);
        if (egg->generation) p->generation = egg->generation;
    } else {
        pet_genes_random(p->genes, &p->rng);
        // la famiglia continua (stesso cognome) finché non si ricomincia da zero
        copy_name(p->family, fam[0] ? fam : pet_random_family(&p->rng));
    }
}

void pet_core_new_egg(pet_t *p, uint32_t seed) { pet_core_new_egg_from(p, seed, NULL); }

static bool sleep_now(int stage, const pet_clock_t *c)
{
    if (c->hour < 0) return false;
    int s, e;
    if (c->sleep_h >= 0 && c->wake_h >= 0) {
        s = c->sleep_h;   // orari scelti dal giocatore: valgono a ogni età
        e = c->wake_h;
    } else {
        switch (stage) {
        case PET_CHILD: s = 20; e = 8; break;
        case PET_TEEN:  s = 21; e = 9; break;
        case PET_ADULT: s = 22; e = 9; break;
        default: return false;   // il neonato non ha orari
        }
    }
    if (s == e) return false;
    return s < e ? (c->hour >= s && c->hour < e) : (c->hour >= s || c->hour < e);
}

// fa scorrere un conto alla rovescia: a ogni scadenza toglie una tacca
static void decay(uint32_t *t, uint32_t dt, uint32_t period, uint8_t *v)
{
    if (!period) return;
    while (dt) {
        if (*t > dt) { *t -= dt; return; }
        dt -= *t;
        *t = period;
        if (*v) (*v)--;
    }
}

static void die(pet_t *p, pet_death_t why)
{
    p->stage = PET_DEAD;
    p->death = why;
    p->needs = 0;
    p->sick = 0;
    p->asleep = 0;
    p->light_off = 0;
    if (p->age_s > p->best_age_s) p->best_age_s = p->age_s;
}

static pet_form_t adult_form(pet_t *p)
{
    uint8_t base = pet_core_base_weight(PET_ADULT);
    if (p->steps_stage >= 5000 && p->mistakes_stage <= 4) return FORM_EXPLORER;
    if (p->weight >= base + 15 || p->snacks_stage >= 15) return FORM_GLUTTON;
    if (p->form == FORM_TEEN_GOOD && p->mistakes <= 2 && p->discipline >= 3) return FORM_SAGE;
    if (p->mistakes_stage >= 6) return FORM_MESSY;
    return FORM_NORMAL;
}

static uint32_t evolve(pet_t *p)
{
    switch (p->stage) {
    case PET_BABY:
        if (p->stage_s < PET_BABY_S) return 0;
        set_stage(p, PET_CHILD);
        p->form = FORM_BASE;
        return EV_EVOLVE;
    case PET_CHILD: {
        if (p->stage_s < PET_CHILD_S) return 0;
        bool good = p->mistakes_stage <= 2;
        set_stage(p, PET_TEEN);
        p->form = good ? FORM_TEEN_GOOD : FORM_TEEN_BAD;
        return EV_EVOLVE;
    }
    case PET_TEEN: {
        if (p->stage_s < PET_TEEN_S) return 0;
        // la forma si decide con i dati da ragazzo, prima di azzerarli
        pet_form_t f = adult_form(p);
        set_stage(p, PET_ADULT);
        p->form = f;
        p->lifespan_s = 0;
        return EV_EVOLVE;
    }
    default:
        return 0;
    }
}

uint32_t pet_core_step(pet_t *p, uint32_t dt, const pet_clock_t *c)
{
    uint32_t ev = 0;
    if (!dt || p->stage == PET_NONE || p->stage == PET_DEAD) return 0;
    p->stage_s += dt;
    if (p->stage == PET_EGG) {
        if (p->stage_s >= PET_EGG_S) {
            set_stage(p, PET_BABY);
            p->hunger = p->thirst = p->happy = 2;
            give_identity(p);
            ev |= EV_HATCH;
        }
        return ev;
    }
    if (p->age_s < PET_RELEASE_S && p->age_s + dt >= PET_RELEASE_S) ev |= EV_ELDER;
    p->age_s += dt;
    p->t_pet_cd = p->t_pet_cd > dt ? p->t_pet_cd - dt : 0;
    p->t_shake_cd = p->t_shake_cd > dt ? p->t_shake_cd - dt : 0;
    p->t_play_cd = p->t_play_cd > dt ? p->t_play_cd - dt : 0;
    p->t_reward = p->t_reward > dt ? p->t_reward - dt : 0;

    // nanna: a orari fissi; al risveglio la luce si riaccende da sola
    bool night = sleep_now(p->stage, c);
    if (night && !p->asleep) {
        p->asleep = 1;
        p->tantrum = 0;
        ev |= EV_SLEEP;
    } else if (!night && p->asleep) {
        p->asleep = 0;
        p->light_off = 0;
        ev |= EV_WAKE;
    }

    if (!p->asleep) {
        rates_t rr = rates[p->stage];
        const rates_t *r = &rr;
        // il carattere cambia un po' i ritmi
        switch (temper(p)) {
        case TEMP_CALM:   rr.happy = rr.happy * 5 / 4; rr.tantrum = rr.tantrum * 3 / 2; break;
        case TEMP_LIVELY: rr.happy = rr.happy * 4 / 5; break;
        case TEMP_GREEDY: rr.hunger = rr.hunger * 4 / 5; break;
        }
        decay(&p->t_hunger, dt, r->hunger, &p->hunger);
        decay(&p->t_thirst, dt, r->thirst, &p->thirst);
        decay(&p->t_happy, dt, r->happy, &p->happy);

        uint8_t room = 4 - p->poop, made;
        decay(&p->t_poop, dt, r->poop, &room);
        made = (4 - p->poop) - room;
        if (made) { p->poop += made; ev |= EV_POOP; }
        if (p->poop) p->t_dirty += dt;

        // capricci (non da neonato): con la disciplina al massimo smettono
        if (r->tantrum && !p->tantrum && p->discipline < PET_MAX) {
            if (p->t_tantrum > dt) p->t_tantrum -= dt;
            else {
                p->tantrum = 1;
                p->t_tantrum_on = 0;
                p->t_tantrum = r->tantrum / 2 + rnd(p) % r->tantrum;
            }
        }
        if (p->tantrum) {
            p->t_tantrum_on += dt;
            if (p->t_tantrum_on >= PET_CALL_S) p->tantrum = 0;   // occasione persa
        }

        // malattia: sporco, peso e un pizzico di sfortuna
        if (p->t_check > dt) p->t_check -= dt;
        else {
            p->t_check = 600;
            if (!p->sick) {
                // l'inchiostro fa male solo se resta lì a lungo
                uint32_t ch = 2 + (p->t_dirty >= 1800 ? p->poop * 50 : 0);
                if (p->weight >= pet_core_base_weight(p->stage) + 15) ch += 60;
                if (p->snacks_stage > 8) ch += 40;
                if (rnd(p) % 1000 < ch) {
                    p->sick = 1 + rnd(p) % 2;
                    ev |= EV_SICK;
                }
            }
        }

        // metabolismo: ogni 3 ore da sveglio smaltisce un grammo di troppo
        if (p->age_s / 10800 != (p->age_s - dt) / 10800 && p->weight > pet_core_base_weight(p->stage) + 5)
            p->weight--;

        // trascuratezza grave
        if (!p->hunger || !p->thirst) p->t_empty += dt;
        else p->t_empty = 0;
        if (p->sick) p->t_sick += dt;
        if (p->t_empty >= PET_STARVE_S) {
            die(p, !p->hunger ? DEATH_HUNGER : DEATH_THIRST);
            return ev | EV_DEATH;
        }
        if (p->t_sick >= PET_SICK_DEATH_S) {
            die(p, DEATH_SICK);
            return ev | EV_DEATH;
        }

        ev |= evolve(p);
    }
    // notte protetta (ibrida): la luce si spegne da sola
    if (p->asleep && c->safe_night) p->light_off = 1;

    // chiamate e errori di cura
    // mentre dorme non chiama (e non conta errori), tranne per la luce rimasta accesa
    uint8_t needs = 0;
    if (p->asleep) {
        if (!p->light_off) needs |= NEED_LIGHT;
    } else {
        if (!p->hunger) needs |= NEED_HUNGER;
        if (!p->thirst) needs |= NEED_THIRST;
        if (!p->happy) needs |= NEED_HAPPY;
        if (p->sick) needs |= NEED_SICK;
        if (p->tantrum) needs |= NEED_TANTRUM;
    }
    if (needs & ~p->needs) {
        ev |= EV_CALL;
        // il conto riparte se nessuna delle richieste di prima è ancora aperta: quando a
        // nanna la fame è sostituita dalla luce (e viceversa al risveglio) l'errore non deve
        // arrivare in anticipo, o subito
        if (!(p->needs & needs & ~NEED_TANTRUM)) p->t_needs = 0;
    }
    p->needs = needs;
    p->mistake_done &= needs;
    if (needs) {
        p->t_needs += dt;
        uint8_t care = needs & (NEED_HUNGER | NEED_THIRST | NEED_HAPPY | NEED_LIGHT) & ~p->mistake_done;
        if (care && p->t_needs >= PET_CALL_S) {
            if (p->mistakes < 255) p->mistakes++;
            if (p->mistakes_stage < 255) p->mistakes_stage++;
            p->mistake_done |= care;
            ev |= EV_MISTAKE;
        }
    } else {
        p->t_needs = 0;
    }
    return ev;
}

static void inc(uint8_t *v, int n)
{
    int x = *v + n;
    *v = x > PET_MAX ? PET_MAX : x < 0 ? 0 : x;
}

static void lose_weight(pet_t *p, int g)
{
    int w = p->weight - g, b = pet_core_base_weight(p->stage);
    p->weight = w < b ? b : w;
}

uint32_t pet_core_action(pet_t *p, pet_action_t a, pet_result_t *res)
{
    pet_result_t r = RES_OK;
    uint32_t ev = 0;
    if (!pet_core_alive(p)) { if (res) *res = RES_DEAD; return 0; }
    bool tantrum_refuse = p->tantrum && (rnd(p) & 1);

    switch (a) {
    case ACT_MEAL:
        if (p->asleep) r = RES_ASLEEP;
        else if (tantrum_refuse) r = RES_REFUSE;
        else if (p->hunger >= PET_MAX) r = RES_FULL;
        else { inc(&p->hunger, 1); if (p->weight < 99) p->weight++; }
        break;
    case ACT_SNACK:
        // lo spuntino non sazia: rende felici. Come premio subito dopo una sgridata fissa
        // la lezione (disciplina) e non conta come golosità; dal quarto del giorno ingrassa
        if (p->asleep) r = RES_ASLEEP;
        else if (p->t_reward) {
            p->t_reward = 0;
            inc(&p->happy, 2);
            inc(&p->discipline, 1);
            r = RES_REWARD;
        } else if (tantrum_refuse) r = RES_REFUSE;
        else {
            inc(&p->happy, temper(p) == TEMP_GREEDY ? 2 : 1);
            if (p->snacks_today < 255) p->snacks_today++;
            if (p->snacks_today > PET_SNACKS_FREE) {
                p->weight = p->weight > 97 ? 99 : p->weight + 2;
                if (p->snacks_stage < 255) p->snacks_stage++;
                r = RES_GREEDY;
            }
        }
        break;
    case ACT_WATER:
        if (p->asleep) r = RES_ASLEEP;
        else if (tantrum_refuse) r = RES_REFUSE;
        else if (p->thirst >= PET_MAX) r = RES_FULL;
        else inc(&p->thirst, 1);
        break;
    case ACT_CLEAN:
        if (!p->poop) r = RES_NOTHING;
        else { p->poop = 0; p->t_dirty = 0; }
        break;
    case ACT_MEDICINE:
        if (!p->sick) r = RES_NOTHING;
        else if (--p->sick == 0) p->t_sick = 0;
        break;
    case ACT_LIGHT:
        p->light_off = !p->light_off;
        break;
    case ACT_SCOLD:
        if (p->asleep) r = RES_ASLEEP;
        else if (p->tantrum) { p->tantrum = 0; inc(&p->discipline, 1); p->t_reward = PET_REWARD_S; }
        else { inc(&p->happy, -1); r = RES_SAD; }
        break;
    case ACT_PET:
        if (p->asleep) r = RES_ASLEEP;
        else if (p->t_pet_cd) r = RES_COOLDOWN;
        else { inc(&p->happy, 1); p->t_pet_cd = 1800; }
        break;
    case ACT_SHAKE:
        if (p->asleep) {
            if (!p->t_shake_cd) inc(&p->happy, -1);
            p->t_shake_cd = 1200;
            r = RES_WOKE;
        } else if (p->t_shake_cd) r = RES_COOLDOWN;
        else { inc(&p->happy, 1); p->t_shake_cd = 1200; }
        break;
    case ACT_GAME_WIN:     inc(&p->happy, temper(p) == TEMP_LIVELY ? 2 : 1); lose_weight(p, 1); break;
    case ACT_GAME_BIG_WIN: inc(&p->happy, 2); lose_weight(p, 1); break;
    case ACT_GAME_LOSE:    lose_weight(p, 1); break;
    case ACT_PLAY:
        if (p->asleep) r = RES_ASLEEP;
        else if (tantrum_refuse) r = RES_REFUSE;
        else if (p->t_play_cd) r = RES_COOLDOWN;
        else { inc(&p->happy, 1); p->t_play_cd = PET_PLAY_CD_S; }
        break;
    case ACT_VISIT:
        if (p->asleep) r = RES_ASLEEP;
        else { inc(&p->happy, 2); lose_weight(p, 1); }
        break;
    }
    // un bisogno soddisfatto spegne subito la sua chiamata
    if (r == RES_OK || r == RES_REWARD || r == RES_GREEDY) {
        uint8_t needs = p->needs;
        if (p->hunger) needs &= ~NEED_HUNGER;
        if (p->thirst) needs &= ~NEED_THIRST;
        if (p->happy) needs &= ~NEED_HAPPY;
        if (!p->sick) needs &= ~NEED_SICK;
        if (p->light_off || !p->asleep) needs &= ~NEED_LIGHT;
        if (!p->tantrum) needs &= ~NEED_TANTRUM;
        p->needs = needs;
        p->mistake_done &= needs;
        if (!needs) p->t_needs = 0;
    }
    if (res) *res = r;
    return ev;
}

uint32_t pet_core_steps(pet_t *p, uint32_t n, bool walking)
{
    uint32_t ev = 0;
    if (!pet_core_alive(p) || !n) return 0;
    p->steps_total += n;
    p->steps_stage += n;
    p->steps_today += n;
    p->steps_happy_acc += walking ? 2 * n : n;
    while (p->steps_happy_acc >= 300) {
        p->steps_happy_acc -= 300;
        if (p->happy < PET_MAX) { p->happy++; ev |= EV_HAPPY; }
    }
    p->steps_weight_acc += n;
    while (p->steps_weight_acc >= 500) {
        p->steps_weight_acc -= 500;
        lose_weight(p, 1);
    }
    if (p->happy) { p->needs &= ~NEED_HAPPY; p->mistake_done &= ~NEED_HAPPY; }
    return ev;
}

uint32_t pet_core_release(pet_t *p, bool early)
{
    if (!pet_core_can_release(p) && !(early && p->stage == PET_ADULT)) return 0;
    die(p, DEATH_OLD);
    return EV_DEATH;
}
