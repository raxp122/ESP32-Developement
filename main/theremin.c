// theremin.c — motore del Theremin (vedi theremin.h).
#include "theremin.h"
#include "audio.h"
#include "sd.h"
#include "settings.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "theremin";

#define FS        AUDIO_RATE
#define DIR_PATH  SD_MOUNT "/theremin"
#define CFG_MAGIC 0x314D4854u   // "THM1"
#define TAB       1024          // tabella del seno
#define DLY_MAX   (FS)          // eco fino a 1 s
#define RING      16384         // campioni verso il registratore (~0,7 s)
#define TWO_PI    6.2831853f

/* ---------------- impostazioni ---------------- */

th_cfg_t th_cfg;

static void cfg_defaults(void)
{
    th_cfg = (th_cfg_t){
        .magic = CFG_MAGIC, .wave = TH_WAVE_THEREMIN, .low = 57, .high = 81, .glide = 2,
        .vib_depth = 2, .vib_rate = 5, .scale = TH_SCALE_FREE, .root = 0, .echo = 0, .echo_fb = 1,
        .tone = 3, .roll_fn = TH_ROLL_VOLUME, .sens = 1, .always = 0, .mic = 0, .mic_gain = 2, .level = 80,
    };
}

void th_cfg_load(void)
{
    cfg_defaults();
    nvs_handle_t h;
    if (nvs_open("theremin", NVS_READONLY, &h) != ESP_OK) return;
    th_cfg_t tmp = th_cfg;
    size_t len = sizeof(tmp);
    // un salvataggio più corto (versione precedente) si copia sopra ai predefiniti
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && tmp.magic == CFG_MAGIC && len <= sizeof(tmp)) {
        memcpy(&th_cfg, &tmp, len);
        // valori fuori misura (backup rovinato) tornano ai predefiniti
        if (th_cfg.wave >= TH_WAVE_COUNT) th_cfg.wave = TH_WAVE_THEREMIN;
        // (anche il salvataggio della prima versione, che qui aveva ottava ed estensione)
        if (th_cfg.low < TH_NOTE_MIN || th_cfg.high > TH_NOTE_MAX || th_cfg.high - th_cfg.low < TH_RANGE_MIN)
            th_range_default();
        if (th_cfg.glide > 4) th_cfg.glide = 2;
        if (th_cfg.vib_depth > 10) th_cfg.vib_depth = 2;
        if (th_cfg.vib_rate < 3 || th_cfg.vib_rate > 8) th_cfg.vib_rate = 5;
        if (th_cfg.scale >= TH_SCALE_COUNT) th_cfg.scale = TH_SCALE_FREE;
        if (th_cfg.root > 11) th_cfg.root = 0;
        if (th_cfg.echo > 3) th_cfg.echo = 0;
        if (th_cfg.echo_fb > 3) th_cfg.echo_fb = 1;
        if (th_cfg.tone > 4) th_cfg.tone = 3;
        if (th_cfg.roll_fn >= TH_ROLL_COUNT) th_cfg.roll_fn = TH_ROLL_VOLUME;
        if (th_cfg.sens > 2) th_cfg.sens = 1;
        if (th_cfg.mic_gain < 1 || th_cfg.mic_gain > 4) th_cfg.mic_gain = 2;
        if (th_cfg.level < 10 || th_cfg.level > 100) th_cfg.level = 80;
        if (th_cfg.swap > 1) th_cfg.swap = 0;
    }
    nvs_close(h);
}

void th_range_default(void) { th_cfg.low = 57; th_cfg.high = 81; }

void th_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("theremin", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &th_cfg, sizeof(th_cfg));
    nvs_commit(h);
    nvs_close(h);
}

const char *th_wave_name(int w)
{
    static const char *const n[] = {"Theremin", "Sinusoide", "Triangolo", "Dente di sega", "Quadra"};
    return w >= 0 && w < TH_WAVE_COUNT ? n[w] : "";
}
const char *th_scale_name(int s)
{
    static const char *const n[] = {"Libera (glissando)", "Cromatica", "Maggiore", "Minore", "Pentatonica", "Blues"};
    return s >= 0 && s < TH_SCALE_COUNT ? n[s] : "";
}
const char *th_note_name(int n)
{
    static const char *const names[] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
    return names[((n % 12) + 12) % 12];
}
const char *th_roll_name(int r)
{
    static const char *const n[] = {"Volume", "Vibrato", "Timbro", "Niente"};
    return r >= 0 && r < TH_ROLL_COUNT ? n[r] : "";
}
int th_glide_ms(int i) { static const int ms[] = {0, 30, 80, 150, 300}; return ms[i < 0 ? 0 : i > 4 ? 4 : i]; }
int th_sens_deg(int i) { static const int d[] = {90, 60, 40}; return d[i < 0 ? 0 : i > 2 ? 2 : i]; }

/* ---------------- sintetizzatore (task audio) ---------------- */

static float sintab[TAB + 1];
static int16_t *dly;            // eco (PSRAM)
static int dpos;

// obiettivi scritti dall'interfaccia, letti dal task audio
static volatile float t_freq = 440, t_vol, t_cut = 0.5f, t_vib;
static volatile bool t_gate;
static volatile float out_level;
// stato del sintetizzatore
static float freq = 440, vol, env, phase, lfo, lp;

// registratore: copia dell'uscita (un produttore, un consumatore)
static int16_t *ring;
static volatile uint32_t r_head, r_tail;
static volatile bool rec_on;

static inline float tsin(float ph)   // ph in giri (0..1)
{
    float x = ph * TAB;
    int i = (int)x;
    float f = x - i;
    i &= TAB - 1;
    return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}

static inline float osc(float ph)
{
    switch (th_cfg.wave) {
    case TH_WAVE_SINE:     return tsin(ph);
    case TH_WAVE_TRIANGLE: return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
    case TH_WAVE_SAW:      return 2 * ph - 1;
    case TH_WAVE_SQUARE:   return ph < 0.5f ? 0.7f : -0.7f;
    default: {             // theremin: sinusoide con un po' di armoniche, voce "vocale"
        float p2 = ph * 2, p3 = ph * 3;
        p2 -= (int)p2;
        p3 -= (int)p3;
        return (tsin(ph) + 0.3f * tsin(p2) + 0.12f * tsin(p3)) * 0.73f;
    }
    }
}

static void th_synth(int16_t *buf, int n)
{
    const float gk = th_cfg.glide ? 1.0f - expf(-1000.0f / (th_glide_ms(th_cfg.glide) * FS)) : 1.0f;
    const float rate = th_cfg.vib_rate / (float)FS;
    const int dlen = th_cfg.echo ? (int)(FS * (th_cfg.echo == 1 ? 0.18f : th_cfg.echo == 2 ? 0.33f : 0.52f)) : 0;
    const float fb = 0.2f + 0.18f * th_cfg.echo_fb;
    const float level = th_cfg.level / 100.0f;
    float peak = 0;
    for (int i = 0; i < n; i++) {
        freq += (t_freq - freq) * gk;
        vol += (t_vol - vol) * 0.002f;                         // ~20 ms: niente scatti
        float tgt = t_gate ? 1.0f : 0.0f;
        env += (tgt - env) * (t_gate ? 0.004f : 0.0008f);      // attacco ~10 ms, rilascio ~50 ms
        lfo += rate;
        if (lfo >= 1) lfo -= 1;
        float f = freq * (1.0f + t_vib * 0.0578f * tsin(lfo));   // vibrato in semitoni (≈ 2^(x/12))
        phase += f / FS;
        if (phase >= 1) phase -= (int)phase;
        lp += (osc(phase) - lp) * t_cut;                       // filtro passa-basso: timbro
        float s = lp * vol * env * level;
        if (dlen && dly) {
            int rd = dpos - dlen;
            if (rd < 0) rd += DLY_MAX;
            float e = dly[rd] / 32767.0f;
            float o = s * 0.8f + e * 0.4f;   // un po' di spazio: con l'eco non deve distorcere
            float w = s + e * fb;
            dly[dpos] = (int16_t)(w > 1 ? 32767 : w < -1 ? -32767 : w * 32767);
            if (++dpos >= DLY_MAX) dpos = 0;
            s = o;
        }
        if (s > 1) s = 1;
        if (s < -1) s = -1;
        float a = fabsf(s);
        if (a > peak) peak = a;
        int16_t v = (int16_t)(s * 32000);
        buf[i] = v;
        if (rec_on && ring) {
            uint32_t h = r_head;
            if (h - r_tail < RING) { ring[h % RING] = v; r_head = h + 1; }
        }
    }
    out_level = peak;
}

bool th_start(void)
{
    if (!sintab[TAB / 4]) for (int i = 0; i <= TAB; i++) sintab[i] = sinf(TWO_PI * i / TAB);
    if (!dly) dly = heap_caps_calloc(DLY_MAX, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!ring) ring = heap_caps_malloc(RING * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (dly) memset(dly, 0, DLY_MAX * sizeof(int16_t));
    if (!audio_init()) return false;
    audio_set_volume(g_set.volume);
    env = vol = 0;
    t_gate = false;
    audio_start(th_synth);
    return true;
}

void th_stop(void)
{
    th_rec_stop();
    t_gate = false;
    audio_stop_if(th_synth);
}

/* ---------------- controlli ---------------- */

static const uint16_t SCALES[TH_SCALE_COUNT] = {
    0, 0x0FFF,                                            // libera, cromatica
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   // maggiore
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // minore
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          // pentatonica
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              // blues
};

// la nota (midi, con decimali) più vicina della scala
static float snap(float n)
{
    uint16_t mask = SCALES[th_cfg.scale];
    if (!mask) return n;
    int best = (int)lroundf(n);
    float bd = 1e9f;
    for (int k = (int)floorf(n) - 6; k <= (int)ceilf(n) + 6; k++) {
        int deg = ((k - th_cfg.root) % 12 + 12) % 12;
        if (!(mask & (1 << deg))) continue;
        float d = fabsf(n - k);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

static float a4(void) { return g_set.a4_x10 / 10.0f; }

void th_control(float p, float r, bool gate)
{
    if (p < -1) p = -1;
    if (p > 1) p = 1;
    if (r < -1) r = -1;
    if (r > 1) r = 1;
    // nota: dalla più bassa (tutto da una parte) alla più alta; posizione zero = a metà
    float note = snap(th_cfg.low + (p + 1) * 0.5f * (th_cfg.high - th_cfg.low));
    t_freq = a4() * powf(2.0f, (note - 69) / 12.0f);

    float vib = th_cfg.vib_depth * 0.1f;
    static const float cut[] = {0.04f, 0.09f, 0.2f, 0.45f, 1.0f};
    float k = cut[th_cfg.tone];
    float v = 1.0f;
    switch (th_cfg.roll_fn) {
    case TH_ROLL_VOLUME:  v = 0.75f + 0.75f * r; break;            // a sinistra fino al silenzio
    case TH_ROLL_VIBRATO: if (r > 0) vib += r * 1.2f; break;       // a destra più vibrato
    case TH_ROLL_TONE:    k *= powf(2.0f, r * 2.5f); break;        // a destra più brillante
    default: break;
    }
    t_vol = v < 0 ? 0 : v > 1 ? 1 : v;
    t_vib = vib;
    t_cut = k < 0.02f ? 0.02f : k > 1 ? 1 : k;
    t_gate = gate;
}

float th_freq(void) { return t_freq; }
float th_level(void) { return out_level; }

float th_note(int *midi)
{
    float n = 69 + 12 * log2f(t_freq / a4());
    int m = (int)lroundf(n);
    *midi = m;
    return (n - m) * 100;
}

/* ---------------- registratore ---------------- */

static TaskHandle_t rec_h;
static volatile bool rec_run;
static volatile uint32_t rec_samples;
static char rec_name[40], err[64];

const char *th_error(void) { return err; }
const char *th_rec_name(void) { return rec_name; }
bool th_recording(void) { return rec_h != NULL; }
uint32_t th_rec_ms(void) { return (uint32_t)((uint64_t)rec_samples * 1000 / FS); }

static void wav_header(FILE *f, uint32_t samples)
{
    uint32_t data = samples * 2;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                     16, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0,
                     'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t riff = 36 + data, rate = FS, bps = FS * 2;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &bps, 4);
    memcpy(h + 40, &data, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

static void rec_task(void *arg)
{
    FILE *f = arg;
    static int16_t st[240 * 2], out[240];
    bool mic = th_cfg.mic && audio_mic_start();
    float gain = th_cfg.mic_gain * 1.5f, dc = 0;
    uint32_t samples = 0;
    while (rec_run) {
        int n = 240;
        if (mic) {
            n = audio_mic_read(st, 240, 100);
            if (n <= 0) continue;
        } else {
            while (rec_run && r_head - r_tail < 240) vTaskDelay(pdMS_TO_TICKS(5));
            if (!rec_run) break;
        }
        // col microfono il ritmo lo dà lui: se il suono si accumula si salta avanti
        if (r_head - r_tail > 4800) r_tail = r_head - 240;
        for (int i = 0; i < n; i++) {
            float s = 0;
            if (r_head != r_tail) { s = ring[r_tail % RING]; r_tail = r_tail + 1; }
            if (mic) {
                float m = 0.5f * (st[2 * i] + st[2 * i + 1]);
                dc += (m - dc) * 0.001f;               // via la componente continua
                s += (m - dc) * gain;
            }
            out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        if (fwrite(out, 2, n, f) != (size_t)n) { snprintf(err, sizeof(err), "microSD piena o non scrivibile"); break; }
        samples += n;
        rec_samples = samples;
    }
    rec_on = false;
    if (mic) audio_mic_stop();
    wav_header(f, samples);
    fclose(f);
    ESP_LOGI(TAG, "registrato %s: %lu ms", rec_name, (unsigned long)th_rec_ms());
    rec_h = NULL;
    vTaskDelete(NULL);
}

bool th_rec_start(void)
{
    err[0] = 0;
    if (rec_h) return true;
    if (!sd_ok()) { snprintf(err, sizeof(err), "Serve la microSD"); return false; }
    if (!ring) { snprintf(err, sizeof(err), "Memoria insufficiente"); return false; }
    mkdir(DIR_PATH, 0777);
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 124) strftime(rec_name, sizeof(rec_name), "%Y%m%d-%H%M%S.wav", &t);
    else {   // senza ora: numerate
        for (int i = 1; i < 1000; i++) {
            snprintf(rec_name, sizeof(rec_name), "rec_%03d.wav", i);
            char p[80];
            snprintf(p, sizeof(p), DIR_PATH "/%s", rec_name);
            struct stat s;
            if (stat(p, &s) != 0) break;
        }
    }
    char path[80];
    snprintf(path, sizeof(path), DIR_PATH "/%s", rec_name);
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(err, sizeof(err), "Non riesco a creare il file"); return false; }
    setvbuf(f, NULL, _IOFBF, 8192);
    wav_header(f, 0);
    rec_samples = 0;
    r_tail = r_head;
    rec_on = true;
    rec_run = true;
    if (xTaskCreatePinnedToCore(rec_task, "th_rec", 4096, f, 4, &rec_h, 1) != pdPASS) {
        rec_on = rec_run = false;
        fclose(f);
        remove(path);
        snprintf(err, sizeof(err), "Memoria insufficiente");
        return false;
    }
    return true;
}

void th_rec_stop(void)
{
    if (!rec_h) return;
    rec_run = false;
    for (int i = 0; i < 300 && rec_h; i++) vTaskDelay(pdMS_TO_TICKS(10));   // chiude il file
}

/* ---------------- registrazioni salvate ---------------- */

static FILE *pf;
static volatile bool p_done;
static volatile uint32_t p_samples;

static void play_synth(int16_t *buf, int n)
{
    int got = pf ? (int)fread(buf, 2, n, pf) : 0;
    if (got < n) {
        memset(buf + got, 0, (n - got) * 2);
        p_done = true;
        audio_stop_if(play_synth);   // finito: l'uscita si libera (chiude il file th_play_stop)
    }
    p_samples += got;
}

static int by_name_desc(const void *a, const void *b) { return strcmp((const char *)b, (const char *)a); }

int th_list(char (*names)[40], int max)
{
    DIR *d = sd_ok() ? opendir(DIR_PATH) : NULL;
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l >= 40 || strcasecmp(e->d_name + l - 4, ".wav")) continue;
        memcpy(names[n++], e->d_name, l + 1);   // l < 40 controllato sopra
    }
    closedir(d);
    qsort(names, n, 40, by_name_desc);
    return n;
}

uint32_t th_file_ms(const char *name)
{
    char p[80];
    snprintf(p, sizeof(p), DIR_PATH "/%s", name);
    struct stat s;
    if (stat(p, &s) != 0 || s.st_size < 44) return 0;
    return (uint32_t)((uint64_t)(s.st_size - 44) / 2 * 1000 / FS);
}

bool th_play(const char *name)
{
    th_play_stop();
    char p[80];
    snprintf(p, sizeof(p), DIR_PATH "/%s", name);
    pf = fopen(p, "rb");
    if (!pf) return false;
    setvbuf(pf, NULL, _IOFBF, 16384);
    fseek(pf, 44, SEEK_SET);
    if (!audio_init()) { fclose(pf); pf = NULL; return false; }
    audio_set_volume(g_set.volume);
    p_done = false;
    p_samples = 0;
    audio_start(play_synth);
    return true;
}

void th_play_stop(void)
{
    // se sta ancora suonando: audio_stop attende che il task audio abbia finito di leggere
    if (audio_current() == play_synth) audio_stop();
    if (pf) { fclose(pf); pf = NULL; }
    p_done = true;
}

bool th_playing(void) { return pf && !p_done; }
uint32_t th_play_ms(void) { return (uint32_t)((uint64_t)p_samples * 1000 / FS); }

bool th_delete(const char *name)
{
    char p[80];
    snprintf(p, sizeof(p), DIR_PATH "/%s", name);
    return remove(p) == 0;
}
