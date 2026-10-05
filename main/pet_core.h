// pet_core.h — logica del Polipetto (un "Tamagotchi" originale). Solo regole del gioco:
// niente hardware, niente LVGL, così si può simulare e collaudare anche sul PC.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PET_MAGIC    0x50455431u   // "PET1"
#define PET_VERSION  1
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
};

typedef enum {
    ACT_MEAL, ACT_SNACK, ACT_WATER, ACT_CLEAN, ACT_MEDICINE, ACT_LIGHT,
    ACT_SCOLD, ACT_PET, ACT_SHAKE, ACT_GAME_WIN, ACT_GAME_BIG_WIN, ACT_GAME_LOSE,
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
} pet_result_t;

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
    uint32_t age_s, stage_s, lifespan_s, best_age_s;
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
} pet_t;

void     pet_core_new_egg(pet_t *p, uint32_t seed);                 // conserva generazione e record
uint32_t pet_core_step(pet_t *p, uint32_t dt, int hour);            // hour = -1 se l'ora non è nota
uint32_t pet_core_action(pet_t *p, pet_action_t a, pet_result_t *res);
uint32_t pet_core_steps(pet_t *p, uint32_t n, bool walking);
uint8_t  pet_core_base_weight(int stage);
bool     pet_core_alive(const pet_t *p);                            // nato e non morto
uint32_t pet_core_egg_left(const pet_t *p);                         // secondi alla schiusa

// Durate (s), esposte per UI e test
#define PET_EGG_S        60u
#define PET_BABY_S       3600u
#define PET_CHILD_S      86400u
#define PET_TEEN_S       (2u * 86400u)
#define PET_CALL_S       900u      // 15 minuti per rispondere a una chiamata
#define PET_STARVE_S     (8u * 3600u)
#define PET_SICK_DEATH_S (8u * 3600u)
