// app_saber.c — spada laser: BOOT accende/spegne, swing e scontri dal giroscopio.
// Tutti i suoni sono sintetizzati in tempo reale (nessun campione registrato).
#include "apps.h"
#include "audio.h"
#include "board.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TWO_PI      6.2831853f
#define IGNITE_S    0.55f
#define RETRACT_S   0.50f
#define CLASH_S     0.40f
#define BLASTER_S   0.32f
#define STAB_S      0.45f
#define BLADE_X     84
#define BLADE_W     (SCR_W - BLADE_X - 14)

/* ---------------- palette ---------------- */

static const struct { const char *name; uint32_t c; } colors[] = {
    {"Blu", 0x2E7BFF}, {"Verde", 0x3DFF5A}, {"Rosso", 0xFF2A2A}, {"Viola", 0xB24BFF},
    {"Giallo", 0xFFD23C}, {"Arancione", 0xFF8A1C}, {"Ciano", 0x2FF5FF}, {"Bianco", 0xEEF0FF},
};
#define NCOL (int)(sizeof(colors) / sizeof(colors[0]))
static lv_color_t blade_color(void) { return lv_color_hex(colors[g_set.saber_color % NCOL].c); }

/* ---------------- stato condiviso ---------------- */

enum { S_OFF, S_IGNITE, S_ON, S_RETRACT };
enum { CMD_NONE, CMD_IGNITE, CMD_RETRACT };
static int mode = S_OFF;                // stato visivo (solo task UI)
static volatile int cmd;                // comando UI → sintetizzatore
static volatile bool reset_req;
static volatile int synth_mode = S_OFF; // stato effettivo del suono
static volatile float swing;            // 0…1, dal task IMU
static volatile float spin_dps;         // velocità angolare, per l'effetto "doppler"
static volatile uint32_t clash_req;     // incrementato dal task IMU a ogni scontro
static volatile uint32_t blaster_req;   // tocco breve sullo schermo
static volatile uint32_t stab_req;      // affondo in avanti
static volatile bool lockup;            // dito tenuto sullo schermo: lame incrociate
static volatile float audio_level;      // livello del suono (0…1) per far pulsare la lama
static volatile int64_t stab_until;     // dopo un affondo niente scontri per un attimo

/* ---------------- sintetizzatore ----------------
 * Il suono classico nasce da due cose: il ronzio di un motore di proiettore e il
 * "buzz" di interferenza di un tubo catodico. Qui:
 *  - strato scuro: due denti di sega a ~86 Hz leggermente stonati (battimento lento)
 *  - buzz: impulso stretto al doppio della frequenza, con un filo di jitter
 *  - swing: come lo "smoothswing" delle spade da collezione, il tono sale e il
 *    timbro si apre seguendo la rotazione, con un'oscillazione di volume legata
 *    all'angolo percorso (effetto doppler "vvvooom") invece del soffio di vento
 *  - scontro: crepitio denso e distorto + "zap" che scende di frequenza
 */

static uint32_t rng = 0x12345678;
static inline float frand(void) { rng = rng * 1664525u + 1013904223u; return (int32_t)rng * (1.0f / 2147483648.0f); }
static inline float wrap(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float saw(float p) { return 2.0f * p - 1.0f; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static void synth(int16_t *out, int n)
{
    static float ph1, ph2, ph3, lfo, lp, lp2, low, nlp, angle, sw_s, bright;
    static float crk1, crk2, zph, bph, am = 1, am_t = 1, pm = 1, pm_t = 1, pop_e, lk_e, lk_b;
    static uint32_t t, wob_t, clash_seen, clash_t = 0xFFFFFFFF;
    static uint32_t bl_seen, bl_t = 0xFFFFFFFF, st_seen, st_t = 0xFFFFFFFF;
    static int cur = S_OFF;
    const float dt = 1.0f / AUDIO_RATE;

    if (reset_req) {
        reset_req = false; cur = S_OFF;
        clash_t = bl_t = st_t = 0xFFFFFFFF;
        clash_seen = clash_req; bl_seen = blaster_req; st_seen = stab_req;
    }
    int c = cmd;
    if (c) {
        cmd = CMD_NONE;
        if (c == CMD_IGNITE && (cur == S_OFF || cur == S_RETRACT)) { cur = S_IGNITE; t = 0; }
        if (c == CMD_RETRACT && (cur == S_ON || cur == S_IGNITE)) { cur = S_RETRACT; t = 0; }
    }
    if (clash_req != clash_seen) { clash_seen = clash_req; clash_t = 0; }
    if (blaster_req != bl_seen) { bl_seen = blaster_req; bl_t = 0; bph = 0; }
    if (stab_req != st_seen) { st_seen = stab_req; st_t = 0; }
    float spin = spin_dps;
    bool lk = lockup && cur == S_ON;
    float acc = 0;

    for (int i = 0; i < n; i++, t++) {
        float ts = t * dt;
        float pitch = 1, vol = 0, tgt_bright = 0.10f, hiss = 0, crackle = 0;
        sw_s += (swing - sw_s) * 0.0025f;

        switch (cur) {
        case S_IGNITE: {
            float k = ts / IGNITE_S;
            if (k >= 1) { cur = S_ON; k = 1; }
            pitch = 0.25f + 0.83f * (1 - expf(-k * 4.0f)) + 0.08f * sinf(k * 3.14159f) - 0.08f * k;
            vol = clampf(k * 3.0f, 0, 1);
            tgt_bright = 0.55f - 0.4f * k;
            hiss = ts < 0.08f ? (1 - ts / 0.08f) : 0;
            crackle = (1 - k) * 0.5f;
            break;
        }
        case S_ON:
            pitch = 1.0f + 0.28f * sw_s;
            vol = 0.55f + 0.45f * sw_s;
            tgt_bright = 0.10f + 0.45f * sw_s;
            break;
        case S_RETRACT: {
            float k = ts / RETRACT_S;
            if (k >= 1) { cur = S_OFF; k = 1; }
            pitch = 1.0f - 0.78f * (1 - expf(-k * 2.5f)) / (1 - expf(-2.5f));
            vol = powf(1 - k, 1.3f);
            tgt_bright = 0.25f * (1 - k);
            crackle = (1 - k) * 0.25f;
            hiss = k > 0.85f ? (k - 0.85f) * 4.0f : 0;
            break;
        }
        default: break;
        }

        // --- vita in idle: derive lente e casuali di volume e tono, crepitii rari ---
        if (++wob_t >= 1500) {
            wob_t = 0;
            am_t = 1.0f + 0.09f * frand();
            pm_t = 1.0f + 0.010f * frand();
        }
        am += (am_t - am) * 0.0005f;
        pm += (pm_t - pm) * 0.0004f;
        if (cur == S_ON && frand() > 0.99994f) pop_e = 0.5f + 0.5f * frand();
        pop_e *= 0.9965f;

        // --- affondo: spinta di tono e volume ---
        float stab_e = 0;
        if (st_t < (uint32_t)(STAB_S * AUDIO_RATE)) {
            float sx = st_t * dt;
            stab_e = (sx < 0.03f ? sx / 0.03f : 1.0f) * expf(-sx * 7.0f);
            st_t++;
        }
        pitch *= 1.0f + 0.35f * stab_e;
        tgt_bright += 0.35f * stab_e;
        bright += (tgt_bright - bright) * 0.002f;

        // --- swing: effetto doppler legato all'angolo percorso ---
        angle += spin * 0.01745f * dt;
        if (angle > 6283.0f) angle -= 6283.0f;
        float dop = sinf(angle * 0.5f);
        pitch *= (1.0f + 0.06f * sw_s * dop) * pm;
        float amp = (1.0f + 0.35f * sw_s * dop) * am * (1.0f + 0.6f * stab_e);

        float f = 86.0f * pitch;
        ph1 = wrap(ph1 + f * dt);
        ph2 = wrap(ph2 + f * 1.006f * dt);
        ph3 = wrap(ph3 + f * 2.0f * (1.0f + 0.002f * frand()) * dt);
        lfo = wrap(lfo + 0.7f * dt);

        // --- ronzio con "corpo" per lo speaker piccolo ---
        // la fondamentale (86 Hz) quasi non si sente: rinforzo 2ª–4ª armonica (170–350 Hz),
        // il cervello ricostruisce il basso mancante
        float dark = 0.55f * saw(ph1) + 0.45f * saw(ph2);
        float harm = 0.55f * sinf(TWO_PI * 2 * ph1) + 0.40f * sinf(TWO_PI * 3 * ph1 + 0.5f) + 0.28f * sinf(TWO_PI * 4 * ph2);
        float buzz = (ph3 < 0.12f ? 1.0f : -0.136f);
        float hum = 0.5f * dark + 0.75f * harm + buzz * (0.26f + 0.25f * bright);
        hum *= 0.94f + 0.06f * sinf(TWO_PI * lfo);
        low += (hum - low) * 0.022f;          // ~85 Hz
        hum -= 0.55f * low;                   // toglie l'energia che lo speaker non riproduce

        float cut = 0.09f + bright;
        lp += (hum - lp) * cut;
        lp2 += (lp - lp2) * (cut + 0.15f);
        float y = lp2 * vol * amp;

        float nz = frand();
        nlp += (nz - nlp) * 0.35f;
        y += (nz - nlp) * hiss * 0.7f;
        if (crackle > 0 && frand() > 0.97f) y += frand() * crackle;
        if (pop_e > 0.02f && frand() > 0.6f) y += frand() * pop_e * 0.35f;   // crepitio raro

        // --- crepitio denso (scontro e lockup) ---
        float imp = frand() > 0.55f ? frand() * 2.0f : 0;
        crk1 += (imp - crk1) * 0.5f;
        crk2 += (crk1 - crk2) * 0.08f;
        float crk = crk1 - crk2;

        // --- lockup: sfrigolio continuo a raffiche ---
        lk_e += ((lk ? 1.0f : 0.0f) - lk_e) * (lk ? 0.004f : 0.0015f);
        if (lk_e > 0.01f) {
            if (frand() > 0.9992f) lk_b = 0.6f + 0.4f * frand();   // raffiche irregolari
            lk_b += (0.35f - lk_b) * 0.0008f;
            y = y * (1.0f + 1.5f * lk_e) + lk_e * lk_b * 1.3f * crk;
            y = clampf(y * (1.0f + 1.8f * lk_e), -1, 1);
        }

        // --- scontro ---
        if (clash_t < (uint32_t)(CLASH_S * AUDIO_RATE)) {
            float ct = clash_t * dt;
            float e = (ct < 0.004f ? ct / 0.004f : 1.0f) * expf(-ct * 9.0f);
            zph = wrap(zph + (150.0f + 1700.0f * expf(-ct * 16.0f)) * dt);
            y = y * 2.2f + e * (1.4f * crk + 0.5f * saw(zph));
            y = clampf(y * (1.0f + 2.5f * e), -1.0f, 1.0f);
            clash_t++;
        }

        // --- deflessione del blaster: "zing" che scende + schiocco ---
        if (bl_t < (uint32_t)(BLASTER_S * AUDIO_RATE)) {
            float bt = bl_t * dt;
            float e = (bt < 0.002f ? bt / 0.002f : 1.0f) * expf(-bt * 13.0f);
            bph = wrap(bph + (520.0f + 2600.0f * expf(-bt * 10.0f)) * dt);
            float zing = sinf(TWO_PI * bph) + 0.4f * sinf(TWO_PI * 2.01f * bph);
            y += e * (0.75f * zing + (bt < 0.03f ? 1.2f * crk : 0));
            bl_t++;
        }

        y = y / (1.0f + fabsf(y)) * 1.15f;
        y = clampf(y, -1, 1);
        acc += fabsf(y);
        out[i] = (int16_t)(y * 30000.0f);
    }
    audio_level = clampf(acc / n * 2.2f, 0, 1);
    synth_mode = cur;
}

/* ---------------- sensori: task dedicato a ~330 Hz ----------------
 * Scontro = urto: un picco di "jerk" (variazione brusca dell'accelerazione in
 * pochi millisecondi). Lo swing normale produce accelerazioni anche forti ma che
 * cambiano gradualmente, quindi non lo fa scattare.
 * In più: swing veloce fermato di colpo, ma solo se resta fermo per 60 ms
 * (un'inversione avanti-indietro passa per zero ma riparte subito).
 */

static TaskHandle_t imu_task_h;
static volatile bool imu_run;

static void imu_task(void *arg)
{
    static const float jerk_th[3] = {2.2f, 1.5f, 1.0f};  // g tra due letture (~3 ms)
    static const float stop_th[3] = {520, 400, 300};     // °/s di picco per lo stop brusco
    vec3_t g, w, gp = {0, 0, 1};
    float peak = 0, stop_ref = 0, long_lp = 0;
    int long_axis = -1;                 // 0 = X, 1 = Y: asse lungo (direzione della lama)
    int64_t last_stab = 0;
    int64_t peak_t = 0, stop_t = 0, last_clash = 0;
    bool armed = true, first = true;

    while (imu_run) {
        int64_t now = esp_timer_get_time();
        if (board_imu_read6(&g, &w)) {
            float wm = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
            float dx = g.x - gp.x, dy = g.y - gp.y, dz = g.z - gp.z;
            float jerk = first ? 0 : sqrtf(dx * dx + dy * dy + dz * dz);
            gp = g;
            if (first) {
                // asse lungo = quello fra X e Y su cui pesa meno la gravità (gadget tenuto in orizzontale)
                long_axis = fabsf(g.x) < fabsf(g.y) ? 0 : 1;
                long_lp = long_axis ? g.y : g.x;
            }
            first = false;

            // affondo: accelerazione lineare lungo la lama, con poca rotazione
            float al = long_axis ? g.y : g.x;
            long_lp += (al - long_lp) * 0.02f;          // gravità e derive lente
            float lin = al - long_lp;

            float target = clampf((wm - 50.0f) / 450.0f, 0, 1);
            swing = swing + (target - swing) * (target > swing ? 0.25f : 0.06f);
            spin_dps = wm;

            int s = g_set.saber_clash % 3;   // indice di tabella: mai fuori misura
            bool on = synth_mode == S_ON;
            bool cooled = now - last_clash > 350000;

            // picco di rotazione recente (finestra 80 ms)
            if (wm > peak || now - peak_t > 80000) { peak = wm; peak_t = now; }
            if (wm > 150) armed = true;

            static const float stab_th[3] = {2.2f, 1.7f, 1.3f};
            if (on && fabsf(lin) > stab_th[s] && wm < 160 && now - last_stab > 600000 && cooled) {
                stab_req++;
                last_stab = now;
                stab_until = now + 350000;              // l'arresto dell'affondo non è uno scontro
            }
            bool hit = on && cooled && now > stab_until && jerk > jerk_th[s];
            // stop brusco: candidato, poi conferma se resta fermo
            if (on && armed && cooled && !stop_t && peak > stop_th[s] && wm < peak * 0.25f) {
                stop_t = now;
                stop_ref = peak;
            }
            if (stop_t) {
                if (wm > stop_ref * 0.4f) stop_t = 0;                 // era un'inversione
                else if (now - stop_t > 60000) { hit = cooled && now > stab_until; stop_t = 0; }
            }
            if (hit) {
                clash_req++;
                last_clash = now;
                armed = false;
                peak = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    imu_task_h = NULL;
    vTaskDelete(NULL);
}

static void imu_start(void)
{
    if (imu_task_h) return;
    imu_run = true;
    xTaskCreatePinnedToCore(imu_task, "saber_imu", 4096, NULL, 5, &imu_task_h, 1);
}

static void imu_stop(void)
{
    imu_run = false;
    for (int i = 0; i < 20 && imu_task_h; i++) vTaskDelay(pdMS_TO_TICKS(5));
}

/* ---------------- grafica ---------------- */

static lv_obj_t *root_obj, *blade, *core, *hint, *spark;
static lv_timer_t *fx_timer, *touch_timer;
static uint32_t blaster_seen_ui, stab_seen_ui, spark_until, stab_flash_until;
static bool exit_after, audio_ok;
static uint32_t phase_t0;
static uint32_t flash_until, clash_seen_ui;

static void anim_w(void *o, int32_t v) { lv_obj_set_width(o, v); }

static void blade_anim(int from, int to, int ms, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, blade);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_exec_cb(&a, anim_w);
    lv_anim_start(&a);
}

static void ignite(void)
{
    if (mode == S_ON || mode == S_IGNITE) return;
    mode = S_IGNITE;
    phase_t0 = lv_tick_get();
    cmd = CMD_IGNITE;
    audio_start(synth);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    blade_anim(0, BLADE_W, (int)(IGNITE_S * 1000), lv_anim_path_ease_out);
}

static void retract(void)
{
    if (mode == S_OFF || mode == S_RETRACT) return;
    mode = S_RETRACT;
    phase_t0 = lv_tick_get();
    cmd = CMD_RETRACT;
    blade_anim(lv_obj_get_width(blade), 0, (int)(RETRACT_S * 1000), lv_anim_path_ease_in);
}

static void fx_cb(lv_timer_t *t)
{
    // passaggio IGNITE → ON quando il suono ha finito l'accensione
    // (senza audio si va a tempo)
    uint32_t el = lv_tick_elaps(phase_t0);
    if (mode == S_IGNITE && (audio_ok ? synth_mode == S_ON : el > IGNITE_S * 1000)) mode = S_ON;
    if (mode == S_RETRACT && (audio_ok ? synth_mode == S_OFF : el > RETRACT_S * 1000)) {
        mode = S_OFF;
        audio_stop();
        lv_obj_clear_flag(hint, LV_OBJ_FLAG_HIDDEN);
        if (exit_after) { exit_after = false; ui_pop(); return; }
    }
    if (mode == S_OFF) return;
    if (clash_req != clash_seen_ui) { clash_seen_ui = clash_req; flash_until = lv_tick_get() + 90; input_mark_activity(); }
    if (swing > 0.05f) input_mark_activity();

    if (blaster_req != blaster_seen_ui) {
        blaster_seen_ui = blaster_req;
        // scintilla in un punto a caso della lama
        lv_obj_set_pos(spark, BLADE_X + 80 + esp_random() % (BLADE_W - 160), SCR_H / 2 - 14);
        spark_until = lv_tick_get() + 140;
    }
    if (stab_req != stab_seen_ui) { stab_seen_ui = stab_req; stab_flash_until = lv_tick_get() + 220; }

    // la lama pulsa con il suono: livello audio → alone e luminosità
    float lvl = audio_ok ? audio_level : 0.5f;
    lv_color_t c = blade_color();
    uint32_t now = lv_tick_get();
    bool flash = now < flash_until || (lockup && (esp_random() & 1));
    bool stab = now < stab_flash_until;
    int glow = 10 + (int)(lvl * 34) + (int)(swing * 14) + (stab ? 22 : 0);
    lv_obj_set_style_shadow_width(blade, flash ? 64 : glow, 0);
    lv_obj_set_style_shadow_opa(blade, flash ? LV_OPA_COVER : (lv_opa_t)clampf(140 + lvl * 140, 0, 255), 0);
    lv_obj_set_style_shadow_color(blade, flash ? lv_color_white() : c, 0);
    lv_obj_set_style_bg_color(blade, flash ? lv_color_white() : lv_color_mix(lv_color_white(), c, (lv_opa_t)(lvl * 70)), 0);
    lv_obj_set_style_height(core, stab ? 14 : 8 + (int)(lvl * 4), 0);
    // solo quando cambia: ogni 30 ms invalidava (e ridisegnava) l'intero schermo
    ui_set_bg_color(root_obj, flash ? lv_color_hex(0x303030) : C_BG);
    if (now < spark_until) lv_obj_clear_flag(spark, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(spark, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------- tocchi: breve = blaster, tenuto = lockup ---------------- */

// stato del tocco: azzerato a ogni ingresso (uscendo con uno swipe il rilascio può
// non essere visto, e il primo tocco della volta dopo sembrerebbe la sua continuazione)
static bool down, moved, lock;
static int sx, sy, rel;
static uint32_t t0;

static void touch_cb(lv_timer_t *t)
{
    int x, y;
    bool p = input_touch(&x, &y);
    if (p) {
        rel = 0;
        if (!down) { down = true; moved = false; lock = false; sx = x; sy = y; t0 = lv_tick_get(); return; }
        if (abs(x - sx) > 22 || abs(y - sy) > 22) moved = true;    // è uno swipe: non è per noi
        if (!moved && !lock && mode == S_ON && lv_tick_elaps(t0) > 280) {
            lock = true;
            lockup = true;
            clash_req++;                                           // le lame si incrociano
        }
        return;
    }
    if (!down || ++rel < 2) return;
    down = false;
    if (lock) { lockup = false; return; }
    if (!moved && mode == S_ON && lv_tick_elaps(t0) <= 280) blaster_req++;
}

/* ---------------- ciclo di vita ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    root_obj = root;
    down = moved = lock = false;
    rel = 0;
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    // impugnatura
    lv_obj_t *hilt = lv_obj_create(root);
    lv_obj_remove_style_all(hilt);
    lv_obj_set_size(hilt, 76, 30);
    lv_obj_set_pos(hilt, 10, SCR_H / 2 - 15);
    lv_obj_set_style_radius(hilt, 4, 0);
    lv_obj_set_style_bg_opa(hilt, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hilt, lv_color_hex(0x8C9096), 0);
    lv_obj_set_style_bg_grad_color(hilt, lv_color_hex(0x3A3D42), 0);
    lv_obj_set_style_bg_grad_dir(hilt, LV_GRAD_DIR_VER, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = lv_obj_create(hilt);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 4, 30);
        lv_obj_set_pos(r, 8 + i * 9, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(0x202226), 0);
    }
    lv_obj_t *btn = lv_obj_create(hilt);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 8, 8);
    lv_obj_set_pos(btn, 52, 3);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xD03030), 0);

    // lama: colore pieno con alone, nucleo bianco
    blade = lv_obj_create(root);
    lv_obj_remove_style_all(blade);
    lv_obj_set_size(blade, 0, 24);
    lv_obj_set_pos(blade, BLADE_X, SCR_H / 2 - 12);
    lv_obj_set_style_radius(blade, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(blade, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(blade, blade_color(), 0);
    lv_obj_set_style_shadow_color(blade, blade_color(), 0);
    lv_obj_set_style_shadow_width(blade, 24, 0);
    lv_obj_set_style_shadow_spread(blade, 3, 0);
    core = lv_obj_create(blade);
    lv_obj_remove_style_all(core);
    lv_obj_set_size(core, lv_pct(100), 10);
    lv_obj_center(core);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(core, lv_color_white(), 0);

    spark = lv_obj_create(root);
    lv_obj_remove_style_all(spark);
    lv_obj_set_size(spark, 28, 28);
    lv_obj_set_style_radius(spark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(spark, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(spark, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(spark, 30, 0);
    lv_obj_set_style_shadow_color(spark, lv_color_hex(0xFFE7A0), 0);
    lv_obj_add_flag(spark, LV_OBJ_FLAG_HIDDEN);

    hint = lv_label_create(root);
    lv_obj_set_style_text_font(hint, &font_s, 0);
    lv_obj_set_style_text_color(hint, C_DIM, 0);
    lv_label_set_text(hint, "BOOT on/off  ·  tocco: blaster  ·  dito tenuto: lockup  ·  swipe ← esci");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    mode = S_OFF;
    synth_mode = S_OFF;
    cmd = CMD_NONE;
    reset_req = true;
    swing = 0;
    exit_after = false;
    audio_ok = audio_init();
    if (!audio_ok) ui_toast("Audio non disponibile");
    audio_set_volume(g_set.volume);
    board_imu_gyro_enable(true);
    display_set_brightness(100);
    clash_seen_ui = clash_req;
    blaster_seen_ui = blaster_req;
    stab_seen_ui = stab_req;
    lockup = false;
    stab_until = 0;
    imu_start();
    fx_timer = lv_timer_create(fx_cb, 30, NULL);
    touch_timer = lv_timer_create(touch_cb, 15, NULL);
}

static void leave(void)
{
    imu_stop();
    if (fx_timer) { lv_timer_delete(fx_timer); fx_timer = NULL; }
    if (touch_timer) { lv_timer_delete(touch_timer); touch_timer = NULL; }
    lockup = false;
    lv_anim_delete(blade, anim_w);
    mode = S_OFF;
    audio_stop();
    board_imu_gyro_enable(false);
    display_set_brightness(g_set.brightness);
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_BTN:
    case NAV_SELECT:
        if (mode == S_OFF || mode == S_RETRACT) ignite();
        else retract();
        return true;
    case NAV_BACK:
        if (mode == S_OFF) return false;  // spenta: esce
        exit_after = true;                // accesa: prima si spegne, poi esce
        retract();
        return true;
    default:
        return true;
    }
}

const app_t app_saber = {
    .name = "Spada laser", .icon = ICON_BOLT,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};

/* ---------------- menu ---------------- */

static void v_color(char *b, int n) { snprintf(b, n, "%s", colors[g_set.saber_color % NCOL].name); }
static void j_color(int d) { g_set.saber_color = (g_set.saber_color + d + NCOL) % NCOL; settings_save(); }
static void v_vol(char *b, int n) { snprintf(b, n, "%d%%", g_set.volume); }
static void j_vol(int d)
{
    int v = g_set.volume + d * 10;
    g_set.volume = v < 0 ? 0 : v > 100 ? 100 : v;
    audio_set_volume(g_set.volume);
    settings_save();
}
static void v_help(char *b, int n) { snprintf(b, n, "BOOT on/off · swing · urto = scontro · affondo · tocco = blaster · dito tenuto = lockup"); }
static void v_clash(char *b, int n)
{
    static const char *nm[3] = {"Bassa · solo colpi forti", "Media", "Alta · scatta facilmente"};
    snprintf(b, n, "%s", nm[g_set.saber_clash % 3]);
}
static void j_clash(int d)
{
    int v = g_set.saber_clash + d;
    g_set.saber_clash = v < 0 ? 0 : v > 2 ? 2 : v;
    settings_save();
}

static const menu_item_t saber_items[] = {
    {.icon = ICON_BOLT, .label = "Accendi", .value = v_color, .app = &app_saber},
    {.icon = ICON_PALETTE, .label = "Colore", .value = v_color, .on_adjust = j_color},
    {.icon = LV_SYMBOL_VOLUME_MAX, .label = "Volume", .value = v_vol, .on_adjust = j_vol},
    {.icon = ICON_BOLT, .label = "Sensibilità scontro", .value = v_clash, .on_adjust = j_clash},
    {.icon = ICON_INFO, .label = "Comandi", .value = v_help},
};
menu_t saber_menu = {"Spada laser", saber_items, sizeof(saber_items) / sizeof(saber_items[0]), 0, NULL};
