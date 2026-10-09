// pet.c — il Polipetto vive anche ad app chiusa: un timer LVGL ogni secondo fa
// scorrere il tempo, un task legge l'accelerometro (passi e scossoni) e i versi sono
// sintetizzati al volo. Lo stato (poche centinaia di byte) va in NVS: niente microSD.
//
// Tempo reale: all'accensione si "recupera" il tempo passato a scheda spenta, un
// minuto alla volta, così fame, sonno, evoluzioni e anche la morte avvengono come se
// il polipetto fosse rimasto acceso.
#include "pet.h"
#include "settings.h"
#include "audio.h"
#include "board.h"
#include "lvgl.h"
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "pet";

#define CATCHUP_MAX_S  (30LL * 86400)
#define SAVE_DIRTY_US  60000000LL    // dopo un'azione (pappa, pulizia…) o dei passi: entro 1 min
#define SAVE_EVERY_US  900000000LL   // solo il tempo che passa: ogni 15 min (meno usura della flash;
                                      // in modalità reale/ibrida il recupero rifà comunque il resto)

static pet_t P;
static pet_world_t W;
static EXT_RAM_BSS_ATTR struct { uint8_t n, pad[3]; pet_diary_t e[PET_DIARY_N]; } D;     // 0 = più recente
static EXT_RAM_BSS_ATTR struct { uint8_t n, pad[3]; pet_album_t e[PET_ALBUM_N]; } A;
static EXT_RAM_BSS_ATTR struct { uint8_t n, pad[3]; pet_friend_t e[PET_FRIENDS_N]; } F;
#define W_MAGIC 0x50574C44u   // "PWLD"
static void chronicle(uint32_t ev);
static void calendar(void);
static bool fg, walking, dirty;
static bool caught;              // recupero del tempo a scheda spenta già fatto (serve un'ora valida)
static uint32_t sim_unknown;     // secondi già simulati mentre l'ora non era nota
static uint32_t events;
static int64_t last_us, last_save_us;
static uint32_t acc_us;
static uint32_t steps_pending, shakes_pending;   // scritti dal task IMU (atomici)

/* ---------------- orologio ---------------- */

static bool local_now(struct tm *lt, int64_t *epoch)
{
    time_t t = time(NULL);
    localtime_r(&t, lt);
    if (lt->tm_year < 124) return false;   // ora mai impostata
    if (epoch) *epoch = t;
    return true;
}

bool pet_time_known(void)
{
    struct tm lt;
    return local_now(&lt, NULL);
}

/* ---------------- salvataggio ---------------- */

static void load(void)
{
    memset(&P, 0, sizeof(P));
    nvs_handle_t h;
    if (nvs_open("pet", NVS_READONLY, &h) != ESP_OK) return;
    // un salvataggio di una versione precedente (anche più corto: i campi nuovi stanno
    // in fondo) si accetta, e quello che manca resta a zero; uno più nuovo no
    pet_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "st", &tmp, &len) == ESP_OK && len >= offsetof(pet_t, last_epoch) &&
        tmp.magic == PET_MAGIC && tmp.version <= PET_VERSION &&
        tmp.stage <= PET_DEAD && tmp.form <= FORM_MESSY) {   // valori fuori misura indicizzerebbero tabelle
        P = tmp;
        pet_core_upgrade(&P, esp_random());   // dalla v1: nome, sesso e geni
    }
    // il mondo: voci separate, ognuna può mancare (salvataggi vecchi) o essere più corta
    len = sizeof(W);
    if (nvs_get_blob(h, "w", &W, &len) != ESP_OK || W.magic != W_MAGIC) memset(&W, 0, sizeof(W));
    len = sizeof(D);
    if (nvs_get_blob(h, "dia", &D, &len) != ESP_OK || D.n > PET_DIARY_N) memset(&D, 0, sizeof(D));
    len = sizeof(A);
    if (nvs_get_blob(h, "alb", &A, &len) != ESP_OK || A.n > PET_ALBUM_N) memset(&A, 0, sizeof(A));
    len = sizeof(F);
    if (nvs_get_blob(h, "fri", &F, &len) != ESP_OK || F.n > PET_FRIENDS_N) memset(&F, 0, sizeof(F));
    nvs_close(h);
}

static void save_blob(const char *key, const void *v, size_t n)
{
    nvs_handle_t h;
    if (nvs_open("pet", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, key, v, n);
    nvs_commit(h);
    nvs_close(h);
}

void pet_world_save(void)
{
    W.magic = W_MAGIC;
    W.version = 1;
    save_blob("w", &W, sizeof(W));
}
static void diary_save(void) { save_blob("dia", &D, sizeof(D)); }
static void album_save(void) { save_blob("alb", &A, sizeof(A)); }
void pet_friends_save(void) { save_blob("fri", &F, sizeof(F)); }

void pet_save(void)
{
    nvs_handle_t h;
    if (nvs_open("pet", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "st", &P, sizeof(P));
    nvs_commit(h);
    nvs_close(h);
    dirty = false;
    last_save_us = esp_timer_get_time();
}

/* ---------------- versi ----------------
 * Chiptune: onde quadre e triangolari, rumore, e una "bolla" (sinusoide che sale di
 * colpo e si spegne): è il suono del polipetto. Il richiamo è "blub-blub-pii!".
 */

enum { W_END = 0, W_REST, W_SQ, W_SQ25, W_TRI, W_BUB, W_NOISE };
typedef struct { uint8_t w; uint16_t f0, f1, ms; uint8_t vol; } tone_t;

static const tone_t s_call[] = {
    {W_BUB, 320, 760, 60, 95}, {W_REST, 0, 0, 30, 0}, {W_BUB, 380, 900, 60, 95}, {W_REST, 0, 0, 50, 0},
    {W_SQ25, 1180, 1560, 90, 55}, {W_SQ25, 1560, 1400, 70, 50}, {W_END},
};
static const tone_t s_happy[] = {
    {W_BUB, 350, 850, 50, 85}, {W_SQ25, 880, 880, 55, 50}, {W_SQ25, 1175, 1175, 55, 50},
    {W_SQ25, 1568, 1568, 110, 50}, {W_END},
};
static const tone_t s_eat[] = {
    {W_SQ, 330, 200, 50, 55}, {W_REST, 0, 0, 60, 0}, {W_SQ, 330, 200, 50, 55}, {W_REST, 0, 0, 60, 0},
    {W_SQ, 330, 200, 50, 55}, {W_REST, 0, 0, 40, 0}, {W_BUB, 400, 900, 50, 70}, {W_END},
};
static const tone_t s_drink[] = {
    {W_BUB, 240, 620, 70, 90}, {W_REST, 0, 0, 60, 0}, {W_BUB, 240, 620, 70, 90}, {W_REST, 0, 0, 60, 0},
    {W_BUB, 260, 700, 70, 90}, {W_END},
};
static const tone_t s_no[] = {{W_SQ, 330, 330, 90, 50}, {W_REST, 0, 0, 40, 0}, {W_SQ, 247, 247, 150, 50}, {W_END}};
static const tone_t s_clean[] = {{W_NOISE, 0, 0, 260, 40}, {W_BUB, 300, 1200, 120, 70}, {W_END}};
static const tone_t s_med[] = {{W_TRI, 660, 660, 70, 80}, {W_TRI, 990, 990, 70, 80}, {W_TRI, 1320, 1320, 130, 80}, {W_END}};
static const tone_t s_scold[] = {{W_SQ, 180, 140, 220, 55}, {W_END}};
static const tone_t s_hatch[] = {
    {W_NOISE, 0, 0, 60, 60}, {W_REST, 0, 0, 80, 0}, {W_NOISE, 0, 0, 60, 60}, {W_REST, 0, 0, 140, 0},
    {W_SQ25, 1047, 1047, 80, 55}, {W_SQ25, 1319, 1319, 80, 55}, {W_SQ25, 1568, 1568, 80, 55},
    {W_SQ25, 2093, 2093, 170, 55}, {W_BUB, 320, 760, 60, 90}, {W_END},
};
static const tone_t s_evolve[] = {
    {W_SQ25, 523, 523, 100, 55}, {W_SQ25, 659, 659, 100, 55}, {W_SQ25, 784, 784, 100, 55},
    {W_SQ25, 1047, 1047, 150, 55}, {W_REST, 0, 0, 40, 0}, {W_SQ25, 784, 784, 90, 55},
    {W_SQ25, 1047, 1047, 280, 55}, {W_END},
};
static const tone_t s_death[] = {
    {W_TRI, 784, 784, 240, 80}, {W_TRI, 659, 659, 240, 80}, {W_TRI, 523, 523, 240, 80},
    {W_TRI, 392, 370, 700, 80}, {W_END},
};
static const tone_t s_win[] = {
    {W_SQ25, 784, 784, 80, 55}, {W_SQ25, 988, 988, 80, 55}, {W_SQ25, 1175, 1175, 80, 55},
    {W_SQ25, 1568, 1568, 220, 55}, {W_END},
};
static const tone_t s_lose[] = {{W_SQ, 392, 392, 130, 50}, {W_SQ, 330, 330, 130, 50}, {W_SQ, 262, 250, 320, 50}, {W_END}};
static const tone_t s_tick[] = {{W_SQ25, 1500, 1500, 25, 40}, {W_END}};

static const tone_t *const sounds[SND_COUNT] = {
    [SND_CALL] = s_call, [SND_HAPPY] = s_happy, [SND_EAT] = s_eat, [SND_DRINK] = s_drink,
    [SND_NO] = s_no, [SND_CLEAN] = s_clean, [SND_MEDICINE] = s_med, [SND_SCOLD] = s_scold,
    [SND_HATCH] = s_hatch, [SND_EVOLVE] = s_evolve, [SND_DEATH] = s_death, [SND_WIN] = s_win,
    [SND_LOSE] = s_lose, [SND_TICK] = s_tick,
};

static const tone_t *req_seq;   // richiesta dal task LVGL
static uint32_t req_gen;

static void pet_synth(int16_t *out, int n)
{
    static uint32_t gen, t, nz = 0x1234567;
    static const tone_t *seq;
    static float ph;
    uint32_t g = __atomic_load_n(&req_gen, __ATOMIC_ACQUIRE);
    if (g != gen) { gen = g; seq = req_seq; t = 0; ph = 0; }
    bool done = false;
    for (int i = 0; i < n; i++) {
        if (!seq || seq->w == W_END) { out[i] = 0; done = true; continue; }
        uint32_t len = seq->ms * (AUDIO_RATE / 1000);
        float k = (float)t / len, v = 0;
        float f = seq->f0 + ((float)seq->f1 - seq->f0) * (seq->w == W_BUB ? sqrtf(k) : k);
        ph += f / AUDIO_RATE;
        if (ph >= 1) ph -= 1;
        switch (seq->w) {
        case W_SQ:    v = ph < 0.5f ? 1 : -1; break;
        case W_SQ25:  v = ph < 0.25f ? 1 : -1; break;
        case W_TRI:   v = 4 * fabsf(ph - 0.5f) - 1; break;
        case W_BUB:   v = sinf(6.2831853f * ph) * expf(-3.5f * k); break;
        case W_NOISE: nz = nz * 1664525u + 1013904223u; v = ((int32_t)nz >> 16) / 32768.0f * (1 - k); break;
        default:      v = 0; break;
        }
        // attacco e rilascio brevi: niente "clic"
        float a = t < 48 ? t / 48.0f : 1, r = len - t < 144 ? (len - t) / 144.0f : 1;
        out[i] = (int16_t)(v * a * r * seq->vol * 110);
        if (++t >= len) { seq++; t = 0; }
    }
    // finito: libera l'uscita (a meno che nel frattempo sia arrivato un altro verso)
    if (done && __atomic_load_n(&req_gen, __ATOMIC_ACQUIRE) == gen) audio_stop_if(pet_synth);
}

void pet_play(int snd)
{
    if (!g_set.pet_sound || snd < 0 || snd >= SND_COUNT) return;
    audio_synth_t cur = audio_current();
    if ((cur && cur != pet_synth) || audio_mic_active()) return;   // un'altra app sta usando l'audio
    if (!audio_init()) return;
    req_seq = sounds[snd];
    __atomic_add_fetch(&req_gen, 1, __ATOMIC_RELEASE);
    audio_start(pet_synth);
}

static void sound_for_events(uint32_t ev)
{
    if (ev & EV_DEATH) pet_play(SND_DEATH);
    else if (ev & EV_EVOLVE) pet_play(SND_EVOLVE);
    else if (ev & EV_HATCH) pet_play(SND_HATCH);
    else if (ev & EV_CALL) pet_play(SND_CALL);
}

/* ---------------- tempo ---------------- */

// orologio per le regole: in modalità ibrida valgono gli orari scelti e la notte è protetta
static pet_clock_t clock_at(int hour)
{
    pet_clock_t c = {.hour = (int8_t)hour, .sleep_h = -1, .wake_h = -1, .safe_night = false};
    if (g_set.pet_time == PET_TIME_HYBRID) {
        c.sleep_h = g_set.pet_sleep_h;
        c.wake_h = g_set.pet_wake_h;
        c.safe_night = true;
    }
    return c;
}

// il tempo a scheda spenta si recupera in modalità reale e ibrida (serve un'ora valida)
static bool real_time_mode(void) { return g_set.pet_time == PET_TIME_REAL || g_set.pet_time == PET_TIME_HYBRID; }

// recupera il tempo trascorso a scheda spenta (solo in modalità "tempo reale")
static void catch_up(void)
{
    struct tm lt;
    int64_t now;
    if (!real_time_mode() || P.last_epoch <= 0) return;
    if (P.stage != PET_EGG && !pet_core_alive(&P)) return;
    if (!local_now(&lt, &now) || now <= P.last_epoch) return;
    caught = true;
    // il tempo passato a scheda accesa prima che l'ora fosse nota è già stato simulato
    int64_t gap = now - P.last_epoch - sim_unknown;
    if (gap <= 0) { P.last_epoch = now; return; }
    if (gap > CATCHUP_MAX_S) gap = CATCHUP_MAX_S;

    time_t t0 = (time_t)P.last_epoch;
    localtime_r(&t0, &lt);
    int sod = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;   // secondo del giorno simulato
    uint32_t ev = 0;
    int64_t left = gap;
    while (left > 0 && P.stage != PET_DEAD) {
        uint32_t dt = left > 60 ? 60 : (uint32_t)left;
        pet_clock_t c = clock_at(sod / 3600);
        ev |= pet_core_step(&P, dt, &c);
        sod = (sod + dt) % 86400;
        left -= dt;
    }
    // le novità importanti si vedono (e si sentono) quando si apre l'app
    events |= ev & (EV_HATCH | EV_EVOLVE | EV_DEATH | EV_ELDER);
    P.last_epoch = now;
    chronicle(ev);
    ESP_LOGI(TAG, "recuperati %lld s di vita a scheda spenta", (long long)gap);
    pet_save();
}

static void timer_cb(lv_timer_t *tm)
{
    int64_t us = esp_timer_get_time();
    acc_us += (uint32_t)(us - last_us);
    last_us = us;
    uint32_t dt = acc_us / 1000000;
    acc_us %= 1000000;

    struct tm lt;
    int64_t epoch = 0;
    bool known = local_now(&lt, &epoch);
    uint32_t ev = 0;

    // l'ora è diventata valida solo ora (es. Wi-Fi dopo l'avvio): recupera lo spento adesso
    if (known && !caught) catch_up();

    bool runs = (P.stage == PET_EGG || pet_core_alive(&P)) && (g_set.pet_time != PET_TIME_APP || fg);
    if (runs && dt) {
        // senza un'ora valida la modalità ibrida non sa quando è notte: avanza come
        // "solo a scheda accesa" (nessun recupero, nessun orario)
        // (chi dormiva resta a dormire; dopo un'ora senza orologio però si fa giorno)
        pet_clock_t c = clock_at(known ? lt.tm_hour : sim_unknown >= 3600 ? 12 : -1);
        ev |= pet_core_step(&P, dt, &c);
        if (!known) sim_unknown += dt;
    }

    uint32_t n = __atomic_exchange_n(&steps_pending, 0, __ATOMIC_RELAXED);
    if (n && g_set.pet_steps && pet_core_alive(&P)) {
        ev |= pet_core_steps(&P, n, walking);
        dirty = true;
        // ogni 1000 passi una conchiglia trovata sulla spiaggia
        W.steps_shells += n;
        if (W.steps_shells >= 1000) {
            pet_shells_add(W.steps_shells / 1000);
            W.steps_shells %= 1000;
            pet_world_save();
            ev |= EV_SHELLS;
        }
    }
    if (known && P.steps_yday != lt.tm_yday + 1) {   // contapassi (e spuntini) del giorno
        P.steps_yday = lt.tm_yday + 1;
        P.steps_today = 0;
        P.snacks_today = 0;
    }
    if (ev) chronicle(ev);
    static int cal_min = -1;   // feste e compleanni: un controllo al minuto
    if (known && lt.tm_min != cal_min) { cal_min = lt.tm_min; calendar(); }
    // senza un'ora valida si tiene l'ultima nota: il recupero la userà quando l'ora arriva
    if (known) P.last_epoch = epoch;

    if (ev) {
        sound_for_events(ev);
        // ad app chiusa si tengono solo le novità importanti: aprendola ore dopo non deve
        // comparire "si è addormentato" di stamattina
        events |= fg ? ev : ev & (EV_HATCH | EV_EVOLVE | EV_DEATH | EV_ELDER | EV_FEST | EV_BDAY);
    }
    int64_t since = us - last_save_us;
    if (P.stage == PET_NONE) return;   // niente da salvare finché non c'è un uovo
    if ((ev & (EV_HATCH | EV_EVOLVE | EV_DEATH)) || (dirty && since > SAVE_DIRTY_US) || (runs && since > SAVE_EVERY_US))
        pet_save();
}

/* ---------------- accelerometro: passi e scossoni ---------------- */

static void imu_task(void *arg)
{
    float base = 1.0f, s = 0;
    bool armed = false;
    int run = 0, spikes = 0;
    int64_t last_step = 0, spike_t0 = 0, quiet_until = 0;
    for (;;) {
        bool want = (g_set.pet_steps && pet_core_alive(&P)) || fg;
        if (!want || !board_imu_ok()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        vTaskDelay(pdMS_TO_TICKS(40));   // 25 Hz
        vec3_t g;
        if (!board_imu_accel(&g)) continue;
        float mag = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
        base += (mag - base) * 0.04f;    // gravità (media lenta)
        float d = mag - base;
        s += (d - s) * 0.5f;             // movimento, un po' filtrato
        int64_t now = esp_timer_get_time();

        // scossone: almeno 4 picchi forti in meno di un secondo
        if (fabsf(d) > 0.9f) {
            if (now - spike_t0 > 900000) { spike_t0 = now; spikes = 0; }
            if (++spikes >= 4) {
                spikes = 0;
                spike_t0 = 0;
                if (fg) __atomic_add_fetch(&shakes_pending, 1, __ATOMIC_RELAXED);
                quiet_until = now + 1500000;
            }
        }
        if (now < quiet_until) { armed = false; run = 0; continue; }

        // passo: un picco dopo una valle, a ritmo umano (0,25–2 s). Si contano solo
        // le camminate di almeno 4 passi di fila: un urto isolato non vale.
        if (s < -0.04f) armed = true;
        if (armed && s > 0.10f) {
            armed = false;
            int64_t gap = now - last_step;
            last_step = now;
            if (gap > 250000 && gap < 2000000) {
                if (++run == 4) __atomic_add_fetch(&steps_pending, 4, __ATOMIC_RELAXED);
                else if (run > 4) __atomic_add_fetch(&steps_pending, 1, __ATOMIC_RELAXED);
            } else {
                run = 1;
            }
        }
    }
}

/* ---------------- API ---------------- */

void pet_init(void)
{
    load();
    last_us = last_save_us = esp_timer_get_time();
    catch_up();
    calendar();
    lv_timer_create(timer_cb, 1000, NULL);
    xTaskCreatePinnedToCore(imu_task, "pet_imu", 3072, NULL, 2, NULL, 1);
}

pet_t *pet_get(void) { return &P; }

static void album_add(void);

void pet_new_egg(void)
{
    // chi era ancora vivo (Ricomincia da un uovo) va nell'album
    if (pet_core_alive(&P)) album_add();
    pet_egg_t self;
    const pet_egg_t *egg = NULL;
    if (W.has_egg) {          // l'uovo nel nido, avuto incontrando un altro polipetto
        egg = &W.nest;
        W.has_egg = 0;
    } else if (P.stage == PET_DEAD && P.death == DEATH_OLD) {
        // partito per l'oceano: lascia un uovo suo (stessa famiglia, i suoi geni rimescolati)
        memset(&self, 0, sizeof(self));
        uint32_t r = esp_random();
        pet_genes_child(self.genes, P.genes, P.genes, &r);
        snprintf(self.parent[P.sex == SEX_M ? 1 : 0], PET_NAME_LEN, "%s", P.name);
        snprintf(self.family, PET_NAME_LEN, "%s", P.family);
        egg = &self;
    }
    pet_core_new_egg_from(&P, esp_random(), egg);
    events = 0;
    pet_save();
    pet_world_save();
    if (egg && egg->parent[0][0] && egg->parent[1][0])
        pet_diary_add("Nuovo uovo: figlio di %s e %s", egg->parent[0], egg->parent[1]);
    else if (egg) pet_diary_add("Nuovo uovo lasciato da %s", egg->parent[0][0] ? egg->parent[0] : egg->parent[1]);
    else pet_diary_add("Un nuovo uovo della famiglia %s", P.family);
}

uint32_t pet_do(pet_action_t a, pet_result_t *res)
{
    pet_result_t r;
    uint32_t ev = pet_core_action(&P, a, &r);
    if (res) *res = r;
    dirty = true;
    // la luce si salva subito: spesso la si spegne e poi si spegne anche la scheda
    if (a == ACT_LIGHT) pet_save();
    int snd = -1;
    if (r == RES_OK) {
        switch (a) {
        case ACT_MEAL: case ACT_SNACK: snd = SND_EAT; break;
        case ACT_WATER:    snd = SND_DRINK; break;
        case ACT_CLEAN:    snd = SND_CLEAN; break;
        case ACT_MEDICINE: snd = SND_MEDICINE; break;
        case ACT_LIGHT:    snd = SND_TICK; break;
        case ACT_SCOLD:    snd = SND_SCOLD; break;
        case ACT_PET: case ACT_SHAKE: case ACT_PLAY: case ACT_VISIT: snd = SND_HAPPY; break;
        case ACT_GAME_WIN: case ACT_GAME_BIG_WIN: snd = SND_WIN; break;
        case ACT_GAME_LOSE: snd = SND_LOSE; break;
        }
    } else if (r == RES_REWARD) {
        snd = SND_HAPPY;
    } else if (r == RES_GREEDY) {
        snd = SND_EAT;
    } else if (r == RES_FULL || r == RES_REFUSE || r == RES_SAD || r == RES_WOKE) {
        snd = SND_NO;
    }
    if (snd >= 0) pet_play(snd);
    return ev;
}

uint32_t pet_release(void)
{
    uint32_t ev = pet_core_release(&P, W.has_egg);
    if (ev) {
        events |= ev;
        pet_play(SND_EVOLVE);
        chronicle(ev);
        pet_save();
    }
    return ev;
}

uint32_t pet_take_events(void)
{
    uint32_t e = events;
    events = 0;
    return e;
}

uint32_t pet_take_shakes(void) { return __atomic_exchange_n(&shakes_pending, 0, __ATOMIC_RELAXED); }

void pet_set_foreground(bool on)
{
    fg = on;
    if (!on) __atomic_store_n(&shakes_pending, 0, __ATOMIC_RELAXED);
}

void pet_set_walking(bool on) { walking = on; }

/* ---------------- il mondo del giocatore ---------------- */

pet_world_t *pet_world(void) { return &W; }

void pet_shells_add(int n)
{
    int v = W.shells + n;
    W.shells = v < 0 ? 0 : v > 9999 ? 9999 : v;
    if (n > 0) W.shells_total = W.shells_total + n > 65535 ? 65535 : W.shells_total + n;
}

bool pet_now(int64_t *epoch)
{
    struct tm lt;
    return local_now(&lt, epoch);
}

void pet_diary_add(const char *fmt, ...)
{
    memmove(&D.e[1], &D.e[0], sizeof(D.e[0]) * (PET_DIARY_N - 1));
    pet_diary_t *d = &D.e[0];
    memset(d, 0, sizeof(*d));
    int64_t ep = 0;
    if (pet_now(&ep)) d->epoch = (uint32_t)ep;
    d->age_s = P.age_s;
    d->gen = P.generation;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->text, sizeof(d->text), fmt, ap);
    va_end(ap);
    if (D.n < PET_DIARY_N) D.n++;
    diary_save();
}

int pet_diary_count(void) { return D.n; }
const pet_diary_t *pet_diary_get(int i) { return i >= 0 && i < D.n ? &D.e[i] : NULL; }
int pet_album_count(void) { return A.n; }
const pet_album_t *pet_album_get(int i) { return i >= 0 && i < A.n ? &A.e[i] : NULL; }
int pet_friend_count(void) { return F.n; }
pet_friend_t *pet_friend_get(int i) { return i >= 0 && i < F.n ? &F.e[i] : NULL; }

pet_friend_t *pet_friend_meet(uint32_t uid, const char *name, const char *family, int sex, int stage, int form)
{
    int i = 0;
    while (i < F.n && F.e[i].uid != uid) i++;
    pet_friend_t f;
    if (i < F.n) f = F.e[i];
    else { memset(&f, 0, sizeof(f)); f.uid = uid; i = F.n < PET_FRIENDS_N ? F.n++ : PET_FRIENDS_N - 1; }
    snprintf(f.name, sizeof(f.name), "%s", name);
    snprintf(f.family, sizeof(f.family), "%s", family);
    f.sex = sex;
    f.stage = stage;
    f.form = form;
    // il più recente in cima
    memmove(&F.e[1], &F.e[0], sizeof(F.e[0]) * i);
    F.e[0] = f;
    return &F.e[0];
}

void pet_set_name(const char *name)
{
    if (!name || !name[0]) return;
    char old[PET_NAME_LEN];
    snprintf(old, sizeof(old), "%s", P.name);
    snprintf(P.name, sizeof(P.name), "%s", name);
    pet_save();
    if (strcmp(old, P.name)) pet_diary_add("Ora si chiama %s", P.name);
}

void pet_record(int rec, int score)
{
    static const char *const games[REC_COUNT] = {"Pesca", "Da che parte?", "1, 2, 3 stella", "Memoria", "Ritmo"};
    if (rec < 0 || rec >= REC_COUNT || score <= W.best[rec]) return;
    W.best[rec] = score > 65535 ? 65535 : score;
    pet_world_save();
    pet_diary_add("Nuovo record a %s: %d", games[rec], score);
}

static void album_add(void)
{
    memmove(&A.e[1], &A.e[0], sizeof(A.e[0]) * (PET_ALBUM_N - 1));
    pet_album_t *a = &A.e[0];
    memset(a, 0, sizeof(*a));
    snprintf(a->name, sizeof(a->name), "%s", P.name);
    snprintf(a->family, sizeof(a->family), "%s", P.family);
    memcpy(a->parent, P.parent, sizeof(a->parent));
    a->sex = P.sex;
    a->stage = pet_core_alive(&P) ? P.stage : P.form >= FORM_SAGE ? PET_ADULT : P.form >= FORM_TEEN_GOOD ? PET_TEEN : PET_CHILD;
    a->form = P.form;
    a->death = P.stage == PET_DEAD ? P.death : DEATH_NONE;
    memcpy(a->genes, P.genes, sizeof(a->genes));
    a->generation = P.generation;
    a->age_s = P.age_s;
    int64_t ep = 0;
    if (pet_now(&ep)) a->epoch = (uint32_t)ep;
    if (A.n < PET_ALBUM_N) A.n++;
    album_save();
}

static const char *const form_names[] = {
    [FORM_BASE] = "Polpo", [FORM_TEEN_GOOD] = "Polipetto ragazzo", [FORM_TEEN_BAD] = "Polipetto ribelle",
    [FORM_SAGE] = "Polpo saggio", [FORM_EXPLORER] = "Polpo esploratore", [FORM_NORMAL] = "Polpo",
    [FORM_GLUTTON] = "Polpo goloso", [FORM_MESSY] = "Polpo pasticcione",
};

const char *pet_form_name(int stage, int form)
{
    switch (stage) {
    case PET_EGG:   return "Uovo";
    case PET_BABY:  return "Polipetto neonato";
    case PET_CHILD: return "Polipetto bimbo";
    case PET_TEEN:  return form == FORM_TEEN_BAD ? "Polipetto ribelle" : "Polipetto ragazzo";
    case PET_ADULT: return form <= FORM_MESSY ? form_names[form] : "Polpo";
    default:        return "Polipetto";
    }
}

const char *pet_stage_name(const pet_t *p)
{
    if (p->stage == PET_DEAD) return "Addio, polipetto…";
    return pet_form_name(p->stage, p->form);
}

// le novità della vita finiscono nel diario (e nell'album quando se ne va)
static void chronicle(uint32_t ev)
{
    if (ev & EV_HATCH) {
        if (P.parent[0][0] && P.parent[1][0]) pet_diary_add("È nat%c %s, figli%c di %s e %s", P.sex == SEX_F ? 'a' : 'o', P.name, P.sex == SEX_F ? 'a' : 'o', P.parent[0], P.parent[1]);
        else pet_diary_add("È nat%c %s %s!", P.sex == SEX_F ? 'a' : 'o', P.name, P.family);
        W.colors_seen |= 1u << pet_gene_show(P.genes[GENE_COLOR], GENE_COLOR);
        pet_world_save();
    }
    if (ev & EV_EVOLVE) {
        pet_diary_add("%s ora è: %s", P.name, pet_stage_name(&P));
        if (P.stage == PET_ADULT) { W.forms_seen |= 1u << P.form; pet_world_save(); }
    }
    if (ev & EV_SICK) pet_diary_add("%s si è ammalat%c", P.name, P.sex == SEX_F ? 'a' : 'o');
    if (ev & EV_ELDER) pet_diary_add("%s ha compiuto 25 giorni!", P.name);
    if (ev & EV_DEATH) {
        if (P.death == DEATH_OLD) pet_diary_add("%s è tornat%c nel grande oceano", P.name, P.sex == SEX_F ? 'a' : 'o');
        else pet_diary_add("%s è diventat%c un angioletto", P.name, P.sex == SEX_F ? 'a' : 'o');
        album_add();
    }
}

/* ---------------- calendario ---------------- */

// domenica di Pasqua (algoritmo di Meeus/Jones/Butcher): giorno dell'anno, 0 = 1 gennaio
static int easter_yday(int y)
{
    int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451, month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
    struct tm t = {.tm_year = y - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = 12};
    mktime(&t);
    return t.tm_yday;
}

int pet_holiday(void)
{
    struct tm t;
    if (!local_now(&t, NULL)) return HOL_NONE;
    int m = t.tm_mon + 1, d = t.tm_mday;
    if (m == 1 && d == 1) return HOL_NEWYEAR;
    if (m == 1 && d == 6) return HOL_BEFANA;
    if (m == 2 && d == 14) return HOL_VALENTINE;
    int e = easter_yday(t.tm_year + 1900);
    if (t.tm_yday == e || t.tm_yday == e + 1) return HOL_EASTER;   // Pasqua e Pasquetta
    if (m == 8 && d == 15) return HOL_FERRAGOSTO;
    if (m == 10 && d == 8) return HOL_OCTOPUS;                      // giornata mondiale del polpo
    if (m == 10 && d == 31) return HOL_HALLOWEEN;
    if (m == 12 && d >= 24 && d <= 26) return HOL_CHRISTMAS;
    if (m == 12 && d == 31) return HOL_NYE;
    return HOL_NONE;
}

const char *pet_holiday_name(int h)
{
    static const char *const n[HOL_COUNT] = {
        "", "Capodanno", "Befana", "San Valentino", "Pasqua", "Ferragosto",
        "Giornata del polpo", "Halloween", "Natale", "San Silvestro",
    };
    return h > 0 && h < HOL_COUNT ? n[h] : "";
}

int pet_season(void)
{
    struct tm t;
    if (!local_now(&t, NULL)) return 0xFF;
    int m = t.tm_mon + 1;
    return m == 12 || m <= 2 ? SEASON_WINTER : m <= 5 ? SEASON_SPRING : m <= 8 ? SEASON_SUMMER : SEASON_AUTUMN;
}

int pet_daypart(void)
{
    struct tm t;
    if (!local_now(&t, NULL)) return DAY_DAY;
    int h = t.tm_hour;
    return h >= 6 && h < 8 ? DAY_DAWN : h >= 8 && h < 18 ? DAY_DAY : h >= 18 && h < 21 ? DAY_DUSK : DAY_NIGHT;
}

// feste (un regalo una volta l'anno) e compleanni (ogni settimana di età)
static void calendar(void)
{
    struct tm t;
    if (!local_now(&t, NULL) || !pet_core_alive(&P)) return;
    bool changed = false;
    int h = pet_holiday();
    if (W.fest_year != t.tm_year + 1900) { W.fest_year = t.tm_year + 1900; W.fest_done = 0; changed = true; }
    if (h && !(W.fest_done >> h & 1)) {
        W.fest_done |= 1u << h;
        pet_shells_add(10);
        events |= EV_FEST;
        changed = true;
        pet_diary_add("Festa: %s! Regalo di 10 conchiglie", pet_holiday_name(h));
    }
    int weeks = P.age_s / (7 * 86400);
    if (W.bday_uid != P.uid) { W.bday_uid = P.uid; W.bday_weeks = weeks; changed = true; }   // si parte da adesso
    if (weeks > W.bday_weeks) {
        W.bday_weeks = weeks;
        pet_shells_add(5);
        events |= EV_BDAY;
        changed = true;
        pet_diary_add("%s compie %d settiman%c! Torta e 5 conchiglie", P.name, weeks, weeks == 1 ? 'a' : 'e');
    }
    if (changed) pet_world_save();
}
