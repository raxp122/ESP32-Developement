// seismo.c — motore del Sismografo (vedi seismo.h)
#include "seismo.h"
#include "sd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifndef SEISMO_SIM
#include "board.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define FS         200                 // letture al secondo
#define REC_DIV    2                   // registrazione a 100 campioni al secondo
#define TRACE_DIV  20                  // un punto del grafico ogni 100 ms
#define PRE_N      (5 * FS / REC_DIV)  // 5 s prima dell'evento
#define POST_S     5                   // 5 s dopo
#define MAX_N      (130 * FS / REC_DIV)
#define TRACE_N    512
#define WARMUP_S   20                  // la media lunga deve assestarsi
#define SETTLE_S   3                   // prima ancora: lo swipe che apre la schermata scuote la scheda
#define DIR_PATH   SD_MOUNT "/sismo"
#define LOG_PATH   DIR_PATH "/eventi.csv"
#define CFG_MAGIC  0x31534953u         // "SIS1"

/* ---------------- impostazioni ---------------- */

seismo_cfg_t seismo_cfg = {.magic = CFG_MAGIC, .sens = 1, .record = 1};

const char *seismo_sens_name(int s)
{
    static const char *const n[] = {"Alta", "Media", "Bassa"};
    return n[s < 0 ? 0 : s > 2 ? 2 : s];
}

// soglia minima della vibrazione (mg efficaci) e rapporto STA/LTA per la sensibilità
static const float MIN_MG[3] = {1.5f, 3.0f, 8.0f};
static const float RATIO[3] = {3.5f, 4.0f, 5.0f};

#ifndef SEISMO_SIM
void seismo_cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open("sismo", NVS_READONLY, &h) != ESP_OK) return;
    seismo_cfg_t t;
    size_t len = sizeof(t);
    if (nvs_get_blob(h, "cfg", &t, &len) == ESP_OK && len == sizeof(t) && t.magic == CFG_MAGIC) {
        seismo_cfg = t;
        if (seismo_cfg.sens > 2) seismo_cfg.sens = 1;
    }
    nvs_close(h);
}

void seismo_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("sismo", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &seismo_cfg, sizeof(seismo_cfg));
    nvs_commit(h);
    nvs_close(h);
}
#else
void seismo_cfg_load(void) {}
void seismo_cfg_save(void) {}
#endif

/* ---------------- analisi (una lettura alla volta) ---------------- */

static float base[3];             // gravità e derive lente (passa-basso a 0,5 Hz)
static bool have_base;
static int vert;                  // asse verticale: quello con più gravità
static float sta, lta;            // energia media breve e lunga (mg²)
static uint32_t n_samples;
static int state = SEISMO_WARMUP;
static float peak, now_rms;
static int session_events;

static int16_t trace[TRACE_N];    // grafico: per ogni 100 ms il valore più lontano da zero
static volatile uint32_t trace_head;
static int16_t trace_acc;
static int trace_k;

static int16_t (*pre)[3];         // ultimi 5 s (anello), 0,1 mg
static int pre_pos, pre_fill;
static int16_t (*ev)[3];          // l'evento in corso (o in salvataggio)
static int ev_n, ev_post_left, rec_k;
static float ev_peak;
static time_t ev_start;
static uint32_t ev_samples;       // durata (letture) dall'innesco allo sgancio
static int calm;                  // letture consecutive sotto la soglia di sgancio
static volatile bool writing;     // ev è in salvataggio: niente nuova registrazione

static void event_done(void);

static int16_t to01(float g)      // g → 0,1 mg, saturato
{
    float v = g * 10000.0f;
    return v > 32767 ? 32767 : v < -32767 ? -32767 : (int16_t)lrintf(v);
}

void seismo_feed(float x, float y, float z)
{
    const float a = 0.0156f;   // 1 - e^(-2π·0,5/200)
    float in[3] = {x, y, z};
    if (!have_base) {
        memcpy(base, in, sizeof(base));
        have_base = true;
        vert = fabsf(z) >= fabsf(x) && fabsf(z) >= fabsf(y) ? 2 : fabsf(y) > fabsf(x) ? 1 : 0;
    }
    n_samples++;
    // attesa iniziale: si segue solo la gravità (in fretta) e non si misura niente, così
    // lo scossone dello swipe non finisce né nel grafico né nella calibrazione
    if (n_samples <= SETTLE_S * FS) {
        for (int i = 0; i < 3; i++) base[i] += (in[i] - base[i]) * 0.05f;
        return;
    }
    float d[3];
    for (int i = 0; i < 3; i++) {
        base[i] += (in[i] - base[i]) * a;
        d[i] = in[i] - base[i];
    }
    float e = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 1e6f;   // mg²
    float mg = sqrtf(e);
    sta += (e - sta) * (1.0f / (0.5f * FS));
    now_rms = sqrtf(sta);
    if (n_samples > (SETTLE_S + 2) * FS && mg > peak) peak = mg;   // i primi istanti: il filtro si assesta

    // grafico: asse verticale, il valore più lontano da zero ogni 100 ms
    int16_t v = to01(d[vert]);
    if (abs(v) > abs(trace_acc)) trace_acc = v;
    if (++trace_k >= TRACE_DIV) {
        trace[trace_head % TRACE_N] = trace_acc;
        trace_head++;
        trace_acc = 0;
        trace_k = 0;
    }

    // registrazione a 100 campioni al secondo
    bool rec_tick = ++rec_k >= REC_DIV;
    if (rec_tick) {
        rec_k = 0;
        if (pre) {
            pre[pre_pos][0] = to01(d[0]);
            pre[pre_pos][1] = to01(d[1]);
            pre[pre_pos][2] = to01(d[2]);
            pre_pos = (pre_pos + 1) % PRE_N;
            if (pre_fill < PRE_N) pre_fill++;
        }
    }

    float ratio = lta > 0 ? sta / lta : 0;
    float thr = MIN_MG[seismo_cfg.sens % 3];
    switch (state) {
    case SEISMO_WARMUP:
        lta += (e - lta) * (1.0f / (2.0f * FS));   // all'inizio si assesta in fretta
        if (n_samples >= (SETTLE_S + WARMUP_S) * FS) state = SEISMO_LISTEN;
        break;
    case SEISMO_LISTEN:
        lta += (e - lta) * (1.0f / (20.0f * FS));
        if (ratio > RATIO[seismo_cfg.sens % 3] && now_rms > thr) {
            state = SEISMO_EVENT;
            session_events++;
            ev_peak = 0;
            ev_samples = 0;
            calm = 0;
            ev_start = time(NULL);
            ev_n = 0;
            ev_post_left = -1;
            if (ev && !writing && seismo_cfg.record) {   // si parte con i 5 s di prima
                for (int i = 0; i < pre_fill; i++) memcpy(ev[ev_n++], pre[(pre_pos - pre_fill + i + PRE_N) % PRE_N], 6);
            }
        }
        break;
    case SEISMO_EVENT:
        // la media lunga resta ferma: non deve "abituarsi" all'evento
        ev_samples++;
        if (mg > ev_peak) ev_peak = mg;
        if (ev_post_left < 0) {
            calm = ratio < 1.5f || now_rms < thr * 0.7f ? calm + 1 : 0;
            if (calm >= 2 * FS || ev_samples >= 120 * FS) ev_post_left = POST_S * FS;   // finito: altri 5 s
        } else if (--ev_post_left <= 0) {
            event_done();
            state = SEISMO_LISTEN;
            break;
        }
        if (rec_tick && ev && !writing && seismo_cfg.record && ev_n > 0 && ev_n < MAX_N) {
            ev[ev_n][0] = to01(d[0]);
            ev[ev_n][1] = to01(d[1]);
            ev[ev_n][2] = to01(d[2]);
            ev_n++;
        }
        break;
    }
}

int seismo_trace(int16_t *out, int max, uint32_t *cursor)
{
    uint32_t h = trace_head;
    if (h - *cursor > TRACE_N) *cursor = h - TRACE_N;
    int n = 0;
    while (*cursor != h && n < max) out[n++] = trace[(*cursor)++ % TRACE_N];
    return n;
}

float seismo_now_mg(void) { return now_rms; }
float seismo_peak_mg(void) { return peak; }
void seismo_reset_peak(void) { peak = 0; }
int seismo_state(void) { return state; }
int seismo_warmup_left(void) { int s = SETTLE_S + WARMUP_S - (int)(n_samples / FS); return s < 0 ? 0 : s; }
bool seismo_settling(void) { return n_samples <= SETTLE_S * FS; }
int seismo_session_events(void) { return session_events; }
bool seismo_writing(void) { return writing; }

// Wald et al. (1999): intensità dal picco di accelerazione (cm/s²)
int seismo_mmi(float pga_mg)
{
    float pga = pga_mg * 0.981f;
    if (pga < 1.0f) return 1;
    float i = pga < 68 ? 2.20f * log10f(pga) + 1.00f : 3.66f * log10f(pga) - 1.66f;   // sotto V la retta più bassa
    int r = (int)lroundf(i);
    return r < 1 ? 1 : r > 10 ? 10 : r;
}

const char *seismo_roman(int m)
{
    static const char *const r[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    return r[m < 1 ? 0 : m > 10 ? 9 : m - 1];
}

/* ---------------- microSD ---------------- */

static float last_dur, last_peak;
static time_t last_start;
static int last_n;

static void write_event(void)
{
    if (!sd_ok()) return;
    mkdir(DIR_PATH, 0775);
    struct tm t;
    localtime_r(&last_start, &t);
    if (last_n > 0) {
        char p[64];
        snprintf(p, sizeof(p), DIR_PATH "/%04d%02d%02d-%02d%02d%02d.csv", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
        FILE *f = fopen(p, "w");
        if (f) {
            fprintf(f, "# Sismografo Gadget: accelerazione senza gravita', in mg, %d campioni al secondo\n", FS / REC_DIV);
            fprintf(f, "# l'evento parte a t=%.2f s (prima: i 5 s precedenti)\n", (float)PRE_N * REC_DIV / FS);
            fprintf(f, "t_s,x_mg,y_mg,z_mg\n");
            for (int i = 0; i < last_n; i++)
                fprintf(f, "%.2f,%.1f,%.1f,%.1f\n", i * (float)REC_DIV / FS, ev[i][0] / 10.0f, ev[i][1] / 10.0f, ev[i][2] / 10.0f);
            fclose(f);
        }
    }
    bool fresh = access(LOG_PATH, F_OK) != 0;
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    if (fresh) fprintf(f, "data,ora,durata_s,picco_mg,intensita\n");
    fprintf(f, "%02d/%02d/%04d,%02d:%02d:%02d,%.1f,%.1f,%d\n", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour,
            t.tm_min, t.tm_sec, last_dur, last_peak, seismo_mmi(last_peak));
    fclose(f);
}

int seismo_events(seismo_event_t *out, int max)
{
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return 0;
    // le ultime "max" righe, dalla più recente: si tiene un anello
    seismo_event_t *ring = malloc(sizeof(seismo_event_t) * max);
    if (!ring) { fclose(f); return 0; }
    int n = 0;
    char line[96];
    while (fgets(line, sizeof(line), f)) {
        char d[12], o[10];
        float dur, pk;
        int mmi;
        if (sscanf(line, "%11[^,],%9[^,],%f,%f,%d", d, o, &dur, &pk, &mmi) != 5) continue;
        seismo_event_t *e = &ring[n % max];
        snprintf(e->when, sizeof(e->when), "%s %s", d, o);
        e->dur_s = dur;
        e->peak_mg = pk;
        e->mmi = mmi;
        n++;
    }
    fclose(f);
    int k = n < max ? n : max;
    for (int i = 0; i < k; i++) out[i] = ring[(n - 1 - i) % max];
    free(ring);
    return k;
}

bool seismo_clear_events(void)
{
    DIR *d = opendir(DIR_PATH);
    if (!d) return false;
    struct dirent *e;
    char p[300];
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcasecmp(e->d_name + l - 4, ".csv")) {
            snprintf(p, sizeof(p), DIR_PATH "/%s", e->d_name);
            remove(p);
        }
    }
    closedir(d);
    return true;
}

/* ---------------- task delle letture e del salvataggio ---------------- */

#ifndef SEISMO_SIM
static TaskHandle_t task_h, writer_h;
static volatile bool run;

static void event_done(void)
{
    last_dur = ev_samples / (float)FS;
    last_peak = ev_peak;
    last_start = ev_start;
    last_n = (ev && !writing && seismo_cfg.record) ? ev_n : 0;
    if (!seismo_cfg.record) return;
    if (writer_h && !writing) {   // il salvataggio va nel suo task: la microSD può essere lenta
        writing = true;
        xTaskNotifyGive(writer_h);
    }
}

static void writer_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!run && !writing) break;
        write_event();
        writing = false;
        if (!run) break;
    }
    writer_h = NULL;
    vTaskDelete(NULL);
}

static void sample_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    vec3_t g;
    while (run) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000 / FS));
        if (board_imu_accel(&g)) seismo_feed(g.x, g.y, g.z);
    }
    task_h = NULL;
    vTaskDelete(NULL);
}

bool seismo_start(void)
{
    if (!board_imu_ok()) return false;
    if (task_h) return true;
    if (!pre) pre = heap_caps_malloc(sizeof(*pre) * PRE_N, MALLOC_CAP_SPIRAM);
    if (!ev) ev = heap_caps_malloc(sizeof(*ev) * MAX_N, MALLOC_CAP_SPIRAM);
    have_base = false;
    sta = lta = 0;
    n_samples = 0;
    state = SEISMO_WARMUP;
    peak = now_rms = 0;
    session_events = 0;
    pre_pos = pre_fill = 0;
    trace_head = 0;
    run = true;
    if (!writer_h) xTaskCreatePinnedToCore(writer_task, "sismo_sd", 4096, NULL, 2, &writer_h, 0);
    xTaskCreatePinnedToCore(sample_task, "sismo", 3072, NULL, 5, &task_h, 1);
    return true;
}

void seismo_stop(void)
{
    run = false;
    for (int i = 0; i < 30 && task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    // un salvataggio in corso finisce da solo; altrimenti si sveglia il task per farlo uscire
    if (writer_h && !writing) xTaskNotifyGive(writer_h);
}
#else
// simulatore: niente task, gli eventi non si salvano
static void event_done(void) { last_dur = ev_samples / (float)FS; last_peak = ev_peak; }
bool seismo_start(void)
{
    if (!pre) pre = calloc(PRE_N, sizeof(*pre));
    have_base = false; sta = lta = 0; n_samples = 0; state = SEISMO_WARMUP; peak = now_rms = 0; trace_head = 0;
    return true;
}
void seismo_stop(void) {}
#endif
