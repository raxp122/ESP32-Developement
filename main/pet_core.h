// pet_core.h — logica del Polipetto (un "Tamagotchi" originale). Solo regole del gioco:
// niente hardware, niente LVGL, così si può simulare e collaudare anche sul PC.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PET_MAGIC    0x50455431u   // "PET1"
#define PET_VERSION  2
#define PET_MAX      4             // cuori/gocce/tacche massimi di ogni indicatore

typedef enum { PET_NONE = 0, PET_EGG, PET_BABY, PET_CHILD, PET_TEEN, PET_ADULT, PET_DEAD } pet_stage_t;

typedef enum {
    FORM_BASE = 0,
    FORM_TEEN_GOOD, FORM_TEEN_BAD,
    FORM_SAGE,       // cure perfette e disciplina alta
    FORM_EXPLORER,   // tante passeggiate da ragazzo
    FORM_NORMAL,
    FORM_GLUTTON,    // troppi spuntini, sovrappeso
    FORM_MESSY,      // molti errori di cura
} pet_form_t;

// DEATH_OLD: non è una morte, è il saluto quando il giocatore lo lascia tornare nell'oceano
typedef enum { DEATH_NONE = 0, DEATH_HUNGER, DEATH_THIRST, DEATH_SICK, DEATH_OLD } pet_death_t;

// Bisogni che lo fanno "chiamare"
enum {
    NEED_HUNGER  = 1 << 0,
    NEED_THIRST  = 1 << 1,
    NEED_HAPPY   = 1 << 2,
    NEED_SICK    = 1 << 3,
    NEED_LIGHT   = 1 << 4,   // dorme con la luce accesa
    NEED_TANTRUM = 1 << 5,   // capriccio: va sgridato
};

// Eventi restituiti da step/azioni: servono a suoni e animazioni
enum {
    EV_CALL    = 1 << 0,   // è comparso un bisogno nuovo
    EV_HATCH   = 1 << 1,
    EV_EVOLVE  = 1 << 2,
    EV_DEATH   = 1 << 3,
    EV_POOP    = 1 << 4,
    EV_SICK    = 1 << 5,
    EV_SLEEP   = 1 << 6,
    EV_WAKE    = 1 << 7,
    EV_MISTAKE = 1 << 8,
    EV_HAPPY   = 1 << 9,   // cuore guadagnato camminando
    EV_ELDER   = 1 << 10,  // ha compiuto PET_RELEASE_S: ora si può lasciarlo andare
};

// Orologio e orari passati a ogni passo
typedef struct {
    int8_t hour;              // ora locale, -1 se non nota (allora non dorme mai)
    int8_t sleep_h, wake_h;   // orari personalizzati (modalità ibrida); -1 = quelli dell'età
    bool   safe_night;        // ibrida: mentre dorme non può succedergli niente
} pet_clock_t;

typedef enum {
    ACT_MEAL, ACT_SNACK, ACT_WATER, ACT_CLEAN, ACT_MEDICINE, ACT_LIGHT,
    ACT_SCOLD, ACT_PET, ACT_SHAKE, ACT_GAME_WIN, ACT_GAME_BIG_WIN, ACT_GAME_LOSE,
    ACT_PLAY,      // "Gioca" veloce: un cuore senza minigioco (con ricarica)
    ACT_VISIT,     // ha giocato con un amico di un altro Gadget
} pet_action_t;

typedef enum {
    RES_OK,        // fatto
    RES_FULL,      // è sazio / non ha sete
    RES_REFUSE,    // capriccio: rifiuta
    RES_ASLEEP,    // sta dormendo
    RES_NOTHING,   // non serviva (niente da pulire, non è malato)
    RES_SAD,       // sgridato senza motivo
    RES_COOLDOWN,  // coccole/gioco ripetuti troppo presto: nessun effetto
    RES_WOKE,      // svegliato di soprassalto
    RES_DEAD,
    RES_REWARD,    // spuntino dato come premio dopo una sgridata
    RES_GREEDY,    // spuntino di troppo oggi: ingrassa
} pet_result_t;

/* ---------------- genetica ----------------
 * Ogni tratto ha due alleli, uno dalla mamma e uno dal papà. Si vede quello dominante;
 * a pari dominanza prevale quello della mamma. Ogni figlio prende a caso un allele per
 * tratto da ciascun genitore, con una piccola probabilità di mutazione.
 */
enum { GENE_COLOR, GENE_PATTERN, GENE_TENT, GENE_TEMPER, GENE_COUNT };
enum { COL_ORANGE, COL_CORAL, COL_PURPLE, COL_BLUE, COL_GREEN, COL_PINK, COL_GOLD, COL_COUNT };
enum { PAT_NONE, PAT_SPOTS, PAT_STRIPES, PAT_COUNT };
enum { TENT_NORMAL, TENT_LONG, TENT_SHORT, TENT_COUNT };
enum { TEMP_CALM, TEMP_LIVELY, TEMP_GREEDY, TEMP_COUNT };
enum { SEX_NONE = 0, SEX_M, SEX_F };

#define PET_NAME_LEN 14

// calendario (per il fondale e le feste)
enum { DAY_DAWN, DAY_DAY, DAY_DUSK, DAY_NIGHT };
enum { SEASON_WINTER, SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN };
enum {
    HOL_NONE, HOL_NEWYEAR, HOL_BEFANA, HOL_VALENTINE, HOL_EASTER, HOL_FERRAGOSTO,
    HOL_OCTOPUS, HOL_HALLOWEEN, HOL_CHRISTMAS, HOL_NYE, HOL_COUNT,
};

typedef struct {
    uint8_t genes[GENE_COUNT][2];
    char parent[2][PET_NAME_LEN];   // mamma, papà ("" = uovo selvatico)
    char family[PET_NAME_LEN];      // cognome
    uint16_t generation;
} pet_egg_t;

int  pet_gene_count(int gene);                         // quanti alleli possibili
int  pet_gene_show(const uint8_t g[2], int gene);      // allele visibile
const char *pet_allele_name(int gene, int allele);
const char *pet_gene_name(int gene);
void pet_genes_random(uint8_t g[GENE_COUNT][2], uint32_t *rng);   // uovo selvatico
// figlio: un allele da ciascun genitore (a = mamma, b = papà)
void pet_genes_child(uint8_t out[GENE_COUNT][2], const uint8_t a[GENE_COUNT][2], const uint8_t b[GENE_COUNT][2], uint32_t *rng);
uint32_t pet_rand(uint32_t *rng);

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  stage, form, death;
    uint8_t  hunger, thirst, happy, discipline;   // 0..PET_MAX
    uint8_t  weight;                              // grammi
    uint8_t  poop;                                // macchie d'inchiostro sul fondo, 0..4
    uint8_t  sick;                                // dosi di medicina ancora necessarie
    uint8_t  asleep, light_off;
    uint8_t  needs, mistake_done;                 // NEED_* attivi / già contati come errore
    uint8_t  tantrum;
    uint8_t  mistakes, mistakes_stage, snacks_stage;
    uint16_t generation;
    uint16_t steps_yday;                          // giorno a cui si riferisce steps_today
    uint32_t rng;
    uint32_t age_s, stage_s, lifespan_s, best_age_s;   // lifespan_s: non più usato (vive per sempre)
    uint32_t t_hunger, t_thirst, t_happy, t_poop; // conti alla rovescia (s)
    uint32_t t_tantrum, t_tantrum_on, t_check;
    uint32_t t_needs;                             // da quanto una chiamata attende risposta
    uint32_t t_empty;                             // da quanto fame o sete sono a zero
    uint32_t t_sick;                              // da quanto è malato
    uint32_t t_dirty;                             // da quanto c'è inchiostro da pulire
    uint32_t t_pet_cd, t_shake_cd;                // ricarica di coccole e scossoni
    uint32_t steps_total, steps_stage, steps_today;
    uint32_t steps_happy_acc, steps_weight_acc;
    int64_t  last_epoch;                          // ultimo istante reale noto (0 = sconosciuto)
    // dalla versione 2
    char     name[PET_NAME_LEN], family[PET_NAME_LEN];
    uint8_t  sex;
    uint8_t  genes[GENE_COUNT][2];
    char     parent[2][PET_NAME_LEN];
    uint32_t uid;                                 // identità (per gli amici degli altri Gadget)
    uint32_t t_play_cd;                           // ricarica del "Gioca" veloce
    uint32_t t_reward;                            // dopo una sgridata: tempo per premiarlo
    uint8_t  snacks_today;                        // spuntini di oggi (dal quarto ingrassa)
    uint8_t  pad2[3];
} pet_t;

void     pet_core_new_egg(pet_t *p, uint32_t seed);                 // conserva generazione e record
// uovo con genitori (o selvatico se egg = NULL); nome e sesso si decidono alla schiusa
void     pet_core_new_egg_from(pet_t *p, uint32_t seed, const pet_egg_t *egg);
void     pet_core_upgrade(pet_t *p, uint32_t seed);                 // salvataggio v1: dà nome, sesso e geni
const char *pet_random_name(int sex, uint32_t *rng);
const char *pet_random_family(uint32_t *rng);
uint32_t pet_core_step(pet_t *p, uint32_t dt, const pet_clock_t *c);
uint32_t pet_core_action(pet_t *p, pet_action_t a, pet_result_t *res);
uint32_t pet_core_steps(pet_t *p, uint32_t n, bool walking);
uint8_t  pet_core_base_weight(int stage);
bool     pet_core_alive(const pet_t *p);                            // nato e non morto
uint32_t pet_core_egg_left(const pet_t *p);                         // secondi alla schiusa
bool     pet_core_can_release(const pet_t *p);                      // ha almeno PET_RELEASE_S
// lo lascia tornare nell'oceano (early: da adulto anche prima dei 25 giorni, se c'è un uovo nel nido)
uint32_t pet_core_release(pet_t *p, bool early);

// Durate (s), esposte per UI e test
#define PET_EGG_S        60u
#define PET_BABY_S       3600u
#define PET_CHILD_S      86400u
#define PET_TEEN_S       (2u * 86400u)
#define PET_CALL_S       900u      // 15 minuti per rispondere a una chiamata
#define PET_STARVE_S     (8u * 3600u)
#define PET_SICK_DEATH_S (8u * 3600u)
#define PET_RELEASE_S    (25u * 86400u)   // da qui in poi vive finché non lo lasci andare
#define PET_PLAY_CD_S    1200u            // "Gioca" veloce: una volta ogni 20 minuti
#define PET_REWARD_S     600u             // 10 minuti per premiare dopo la sgridata
#define PET_SNACKS_FREE  3                // spuntini al giorno che non fanno ingrassare
