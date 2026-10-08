// pet.h — il Polipetto vive qui: tempo, salvataggio, contapassi, versi.
// Tutte le funzioni (tranne il task dell'accelerometro, interno) vanno chiamate dal
// task di LVGL, come le app.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet_core.h"

enum {
    SND_CALL, SND_HAPPY, SND_EAT, SND_DRINK, SND_NO, SND_CLEAN, SND_MEDICINE, SND_SCOLD,
    SND_HATCH, SND_EVOLVE, SND_DEATH, SND_WIN, SND_LOSE, SND_TICK, SND_COUNT
};

void     pet_init(void);                 // all'avvio, con LVGL bloccato
pet_t   *pet_get(void);
void     pet_new_egg(void);
uint32_t pet_do(pet_action_t a, pet_result_t *res);   // azione + verso + salvataggio
uint32_t pet_release(void);              // dai 25 giorni (o da adulto con un uovo nel nido): torna nell'oceano
uint32_t pet_take_events(void);          // EV_* accumulati dall'ultima chiamata
uint32_t pet_take_shakes(void);          // scossoni rilevati con l'app aperta
void     pet_set_foreground(bool on);    // l'app del polipetto è aperta
void     pet_set_walking(bool on);       // passeggiata: i passi valgono doppio
void     pet_play(int snd);
void     pet_save(void);
bool     pet_time_known(void);           // l'orologio è impostato

/* ---------------- il mondo del giocatore ----------------
 * Quello che resta da una generazione all'altra: conchiglie, oggetti, record, il nido,
 * il diario, l'album di famiglia e gli amici degli altri Gadget. Salvato in NVS a pezzi
 * (ogni voce sotto i 2 KB, così entra nel backup).
 */
#define PET_DIARY_N   30
#define PET_ALBUM_N   20
#define PET_FRIENDS_N 12

enum { REC_FISH, REC_LR, REC_STAR, REC_MEMORY, REC_RHYTHM, REC_COUNT };
enum { EV_FEST = 1 << 11, EV_BDAY = 1 << 12, EV_SHELLS = 1 << 13 };   // in aggiunta agli EV_* del core

typedef struct {
    uint32_t magic;
    uint16_t version, shells;
    uint32_t owned;            // oggetti del negozio comprati (bit = numero dell'oggetto)
    uint32_t deco;             // decorazioni esposte
    uint8_t  hat, acc;         // indossati (0 = niente)
    uint8_t  has_egg;          // c'è un uovo nel nido
    uint8_t  colors_seen;      // colori dei geni già visti (bit)
    uint16_t forms_seen;       // forme già raggiunte (bit)
    uint16_t best[REC_COUNT];  // record dei minigiochi
    pet_egg_t nest;
    uint16_t fest_year;        // anno delle feste già festeggiate
    uint16_t fest_done;        // bit = HOL_*
    uint32_t bday_uid;         // compleanni (settimane di età) già festeggiati: di chi
    uint16_t bday_weeks;       // e fino a quante
    uint16_t shells_total;     // conchiglie guadagnate in tutto
    uint32_t steps_shells;     // passi già trasformati in conchiglie
} pet_world_t;

typedef struct { uint32_t epoch, age_s; uint16_t gen; char text[58]; } pet_diary_t;   // 30 × 68 byte: sotto i 2 KB del backup
typedef struct {
    char name[PET_NAME_LEN], family[PET_NAME_LEN], parent[2][PET_NAME_LEN];
    uint8_t sex, stage, form, death;
    uint8_t genes[GENE_COUNT][2];
    uint16_t generation, pad;
    uint32_t age_s, epoch;
} pet_album_t;
typedef struct {
    uint32_t uid;
    char name[PET_NAME_LEN], family[PET_NAME_LEN];
    uint8_t sex, visits, stage, form;
    uint32_t last;             // ultima visita (epoch, 0 = ora non nota)
} pet_friend_t;

pet_world_t *pet_world(void);
void pet_world_save(void);
void pet_shells_add(int n);
void pet_diary_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  pet_diary_count(void);
const pet_diary_t *pet_diary_get(int i);     // 0 = la più recente
int  pet_album_count(void);
const pet_album_t *pet_album_get(int i);     // 0 = il più recente
int  pet_friend_count(void);
pet_friend_t *pet_friend_get(int i);         // 0 = il visto più di recente
pet_friend_t *pet_friend_meet(uint32_t uid, const char *name, const char *family, int sex, int stage, int form);
void pet_friends_save(void);
void pet_set_name(const char *name);
void pet_record(int rec, int score);         // aggiorna il record (e lo annota nel diario)

const char *pet_stage_name(const pet_t *p);
const char *pet_form_name(int stage, int form);
int  pet_holiday(void);                      // HOL_* di oggi (HOL_NONE se l'ora non è nota)
const char *pet_holiday_name(int h);
int  pet_season(void);                       // SEASON_*
int  pet_daypart(void);                      // DAY_*
bool pet_now(int64_t *epoch);                // ora reale nota
