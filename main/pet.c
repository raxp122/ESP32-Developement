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
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "pet";

#define CATCHUP_MAX_S  (30LL * 86400)
#define SAVE_DIRTY_US  60000000LL
#define SAVE_EVERY_US  300000000LL

static pet_t P;
static bool fg, walking, dirty;
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
    pet_t tmp;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "st", &tmp, &len) == ESP_OK && len == sizeof(tmp) &&
        tmp.magic == PET_MAGIC && tmp.version == PET_VERSION)
        P = tmp;
    nvs_close(h);
}

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
    if (done && __atomic_load_n(&req_gen, __ATOMIC_ACQUIRE) == gen) audio_stop();
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
    int64_t gap = now - P.last_epoch;
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

    bool runs = (P.stage == PET_EGG || pet_core_alive(&P)) && (g_set.pet_time != PET_TIME_APP || fg);
    if (runs && dt) {
        // senza un'ora valida la modalità ibrida non sa quando è notte: avanza come
        // "solo a scheda accesa" (nessun recupero, nessun orario)
        pet_clock_t c = clock_at(known ? lt.tm_hour : -1);
        ev |= pet_core_step(&P, dt, &c);
        dirty = true;
    }

    uint32_t n = __atomic_exchange_n(&steps_pending, 0, __ATOMIC_RELAXED);
    if (n && g_set.pet_steps && pet_core_alive(&P)) {
        ev |= pet_core_steps(&P, n, walking);
        dirty = true;
    }
    if (known && P.steps_yday != lt.tm_yday + 1) {   // contapassi del giorno
        P.steps_yday = lt.tm_yday + 1;
        P.steps_today = 0;
    }
    // senza un'ora valida non si sa quanto resta spenta: niente recupero al prossimo avvio
    P.last_epoch = known ? epoch : 0;

    if (ev) {
        sound_for_events(ev);
        events |= ev;
    }
    int64_t since = us - last_save_us;
    if (P.stage == PET_NONE) return;   // niente da salvare finché non c'è un uovo
    if ((ev & (EV_HATCH | EV_EVOLVE | EV_DEATH)) || (dirty && since > SAVE_DIRTY_US) || since > SAVE_EVERY_US)
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
    lv_timer_create(timer_cb, 1000, NULL);
    xTaskCreatePinnedToCore(imu_task, "pet_imu", 3072, NULL, 2, NULL, 1);
}

pet_t *pet_get(void) { return &P; }

void pet_new_egg(void)
{
    pet_core_new_egg(&P, esp_random());
    events = 0;
    pet_save();
}

uint32_t pet_do(pet_action_t a, pet_result_t *res)
{
    pet_result_t r;
    uint32_t ev = pet_core_action(&P, a, &r);
    if (res) *res = r;
    dirty = true;
    int snd = -1;
    if (r == RES_OK) {
        switch (a) {
        case ACT_MEAL: case ACT_SNACK: snd = SND_EAT; break;
        case ACT_WATER:    snd = SND_DRINK; break;
        case ACT_CLEAN:    snd = SND_CLEAN; break;
        case ACT_MEDICINE: snd = SND_MEDICINE; break;
        case ACT_LIGHT:    snd = SND_TICK; break;
        case ACT_SCOLD:    snd = SND_SCOLD; break;
        case ACT_PET: case ACT_SHAKE: snd = SND_HAPPY; break;
        case ACT_GAME_WIN: case ACT_GAME_BIG_WIN: snd = SND_WIN; break;
        case ACT_GAME_LOSE: snd = SND_LOSE; break;
        }
    } else if (r == RES_FULL || r == RES_REFUSE || r == RES_SAD || r == RES_WOKE) {
        snd = SND_NO;
    }
    if (snd >= 0) pet_play(snd);
    return ev;
}

uint32_t pet_release(void)
{
    uint32_t ev = pet_core_release(&P);
    if (ev) {
        events |= ev;
        pet_play(SND_EVOLVE);
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
