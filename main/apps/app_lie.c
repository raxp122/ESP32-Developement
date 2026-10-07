// app_lie.c — Macchina della verità: il soggetto tiene il Gadget con il braccio teso e
// l'operatore (che guarda lo schermo) fa le domande.
//
// Misura il tremore della mano (giroscopio e accelerometro, 200 letture al secondo, tolta
// la parte lenta dei movimenti) e lo confronta con una calibrazione:
//   1. traccia base: 10 s fermo e in silenzio;
//   2. quattro domande di controllo, due con risposta vera e due con risposta falsa
//      (in ordine V F F V o F V V F: la stanchezza del braccio pesa uguale sulle due);
//   3. le domande vere: il tremore della risposta si confronta con quello delle risposte
//      vere e false della calibrazione e ne esce la probabilità di bugia.
// È un gioco: il tremore cambia con l'emozione e la stanchezza, non è una prova di nulla.
#include "apps.h"
#include "board.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"

#define FS_HZ     200
#define BLOCK     50          // letture per blocco: 250 ms
#define BPS       (FS_HZ / BLOCK)
#define NBLK      128         // blocchi in memoria (32 s)
#define BASE_S    10          // traccia base
#define REC_S     6           // ogni risposta
#define SETTLE_MS 1500        // dopo il tocco: il colpo sullo schermo non deve contare
#define NCAL      4
#define NF        3           // tremore giroscopio, tremore accelerometro, irregolarità
#define MOVE_DPS  25.0f       // rotazione lenta oltre la base: si è mosso troppo

/* ================= misura (task) ================= */

typedef struct { float g, a, m; } blk_t;   // tremore (°/s rms), tremore (mg rms), rotazione lenta (°/s)
static blk_t ring[NBLK];
static volatile uint32_t n_blk;            // blocchi prodotti (il task scrive, la UI legge)
static TaskHandle_t task_h;
static volatile bool run;

static void imu_task(void *arg)
{
    vec3_t a, w, ws = {0}, as = {0}, gl = {0}, al = {0};
    bool first = true;
    float sg = 0, sa = 0, sm = 0;
    int k = 0;
    const float hp = 0.046f;   // 1 - e^(-2π·1,5/200): sotto 1,5 Hz è movimento, non tremore
    const float lp = 0.47f;    // 1 - e^(-2π·20/200): sopra 20 Hz è rumore del sensore
    TickType_t last = xTaskGetTickCount();
    while (run) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000 / FS_HZ));
        if (!board_imu_read6(&a, &w)) continue;
        if (first) { ws = w; as = a; first = false; }
        ws.x += (w.x - ws.x) * hp; ws.y += (w.y - ws.y) * hp; ws.z += (w.z - ws.z) * hp;
        as.x += (a.x - as.x) * hp; as.y += (a.y - as.y) * hp; as.z += (a.z - as.z) * hp;
        gl.x += (w.x - ws.x - gl.x) * lp; gl.y += (w.y - ws.y - gl.y) * lp; gl.z += (w.z - ws.z - gl.z) * lp;
        al.x += (a.x - as.x - al.x) * lp; al.y += (a.y - as.y - al.y) * lp; al.z += (a.z - as.z - al.z) * lp;
        sg += gl.x * gl.x + gl.y * gl.y + gl.z * gl.z;
        sa += (al.x * al.x + al.y * al.y + al.z * al.z) * 1e6f;   // in mg²
        sm += sqrtf(ws.x * ws.x + ws.y * ws.y + ws.z * ws.z);
        if (++k == BLOCK) {
            blk_t *b = &ring[n_blk % NBLK];
            b->g = sqrtf(sg / BLOCK);
            b->a = sqrtf(sa / BLOCK);
            b->m = sm / BLOCK;
            n_blk++;
            sg = sa = sm = 0;
            k = 0;
        }
    }
    task_h = NULL;
    vTaskDelete(NULL);
}

static void imu_start(void)
{
    if (task_h) return;
    board_imu_gyro_enable(true);
    run = true;
    xTaskCreatePinnedToCore(imu_task, "lie_imu", 4096, NULL, 5, &task_h, 1);
}

static void imu_stop(void)
{
    run = false;
    for (int i = 0; i < 20 && task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    board_imu_gyro_enable(false);
}

/* ================= analisi ================= */

static float base_g, base_a, base_m;   // traccia base
static float cal[NCAL][NF];
static bool cal_lie[NCAL];
static int n_cal;

typedef struct { float x[NF]; float g; bool moved; } feat_t;

// caratteristiche di una finestra di blocchi: tremore rispetto alla base (in log) e irregolarità
static feat_t features(uint32_t b0, int n)
{
    feat_t f = {0};
    float sg = 0, sa = 0, sl = 0, sl2 = 0, mmax = 0;
    for (int i = 0; i < n; i++) {
        const blk_t *b = &ring[(b0 + i) % NBLK];
        sg += b->g * b->g;
        sa += b->a * b->a;
        float l = logf(b->g + 1e-3f);
        sl += l;
        sl2 += l * l;
        if (b->m > mmax) mmax = b->m;
    }
    float g = sqrtf(sg / n), a = sqrtf(sa / n), ml = sl / n;
    f.g = g;
    f.x[0] = logf((g + 1e-3f) / (base_g + 1e-3f));
    f.x[1] = logf((a + 1e-3f) / (base_a + 1e-3f));
    f.x[2] = sqrtf(fmaxf(sl2 / n - ml * ml, 0));
    f.moved = mmax > base_m + MOVE_DPS;
    return f;
}

// Confronto con la calibrazione (discriminante lineare con varianza comune): restituisce la
// probabilità di bugia e la separazione fra risposte vere e false (in deviazioni standard).
static float classify(const float *x, float *sep)
{
    static const float floor_sd[NF] = {0.15f, 0.15f, 0.08f};   // una differenza più piccola è rumore
    float s2 = 0, llr = 0;
    for (int f = 0; f < NF; f++) {
        float mt = 0, ml = 0;
        int nt = 0, nl = 0;
        for (int i = 0; i < NCAL; i++) {
            if (cal_lie[i]) { ml += cal[i][f]; nl++; } else { mt += cal[i][f]; nt++; }
        }
        mt /= nt; ml /= nl;
        float v = 0;
        for (int i = 0; i < NCAL; i++) {
            float d = cal[i][f] - (cal_lie[i] ? ml : mt);
            v += d * d;
        }
        float sd = fmaxf(sqrtf(v / (NCAL - 2)), floor_sd[f]);
        float d = ml - mt;
        s2 += d * d / (sd * sd);
        llr += d / (sd * sd) * (x[f] - (mt + ml) * 0.5f);
    }
    *sep = sqrtf(s2);
    if (llr > 6) llr = 6;
    if (llr < -6) llr = -6;
    return 1.0f / (1.0f + expf(-llr));
}

/* ================= interfaccia ================= */

#define BTN_Y   112
#define BTN_H   56
#define NBTN    2
#define TR_X    440
#define TR_Y    26
#define TR_W    184
#define TR_H    64
#define NPTS    93            // punti della traccia (~23 s)

typedef enum { S_INTRO, S_BASE, S_CAL, S_TEST, S_SETTLE, S_REC, S_RESULT, S_ERROR } state_t;
static state_t st, rec_for;           // rec_for: cosa si sta registrando (S_BASE, S_CAL, S_TEST)
static uint32_t st_t0, rec_b0;
static int pattern;                   // 0: V F F V · 1: F V V F
static int ex_t, ex_l;                // esempi di domande
static lv_obj_t *l_top, *l_main, *l_big, *l_hint, *l_trace, *trace_box, *trace;
static lv_obj_t *btn[NBTN], *btn_l[NBTN];
static int btn_n, btn_x[NBTN], btn_w[NBTN];
static lv_timer_t *tmr;
static lv_point_precise_t pts[NPTS];
static uint32_t drawn_blk;

static const char *const EX_TRUE[] = {
    "Come ti chiami?", "Quanti anni hai?", "In che città vivi?", "Che giorno è oggi?",
    "Di che colore è la tua maglia?", "Hai fratelli o sorelle?",
};
static const char *const EX_LIE[] = {
    "Come ti chiami? → un nome falso", "Quanti anni hai? → un'età falsa", "Di che colore è il cielo? → verde",
    "Quanto fa due più due? → cinque", "In che città sei? → un'altra città", "Hai mai visto il mare? → il contrario",
};
#define N_EX (int)(sizeof(EX_TRUE) / sizeof(EX_TRUE[0]))

static void press_style(int i, bool down)
{
    ui_set_bg_color(btn[i], down ? ui_accent() : lv_color_hex(0x1C2025));
    ui_set_text_color(btn_l[i], down ? C_BG : C_TEXT);
}

static void set_buttons(const char *a, const char *b)
{
    btn_n = b ? 2 : a ? 1 : 0;
    int gap = 8, w = (SCR_W - 24 - gap * (btn_n - 1)) / (btn_n ? btn_n : 1);
    const char *t[NBTN] = {a, b};
    for (int i = 0; i < NBTN; i++) {
        if (i >= btn_n) { lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN); continue; }
        btn_x[i] = 12 + i * (w + gap);
        btn_w[i] = w;
        lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(btn[i], btn_x[i], BTN_Y);
        lv_obj_set_size(btn[i], w, BTN_H);
        lv_label_set_text(btn_l[i], t[i]);
        lv_obj_center(btn_l[i]);
        press_style(i, false);
    }
}

static void big(const char *txt, lv_color_t c)
{
    if (txt) {
        ui_set_text(l_big, txt);
        ui_set_text_color(l_big, c);
        lv_obj_clear_flag(l_big, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(l_main, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l_big, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(l_main, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool cal_is_lie(int i) { return (i == 1 || i == 2) != (pattern == 1); }

static void show_intro(void)
{
    st = S_INTRO;
    big(NULL, C_TEXT);
    ui_set_text(l_top, "Macchina della verità");
    ui_set_text(l_main, "Il soggetto tiene il Gadget con il braccio teso davanti a sé, lo schermo verso di te. "
                        "Tu leggi qui cosa fare e fai le domande.");
    ui_set_text(l_hint, "Misura il tremore della mano: è un gioco, non una prova");
    set_buttons("Inizia: traccia base", NULL);
}

static void show_cal(void)
{
    st = S_CAL;
    big(NULL, C_TEXT);
    bool lie = cal_is_lie(n_cal);
    char t[200];
    snprintf(t, sizeof(t), "Calibrazione %d di %d · risposta %s", n_cal + 1, NCAL, lie ? "FALSA" : "VERA");
    ui_set_text(l_top, t);
    if (lie) snprintf(t, sizeof(t), "Fai una domanda e chiedigli di MENTIRE nella risposta.\nEs. «%s»", EX_LIE[(ex_l + n_cal) % N_EX]);
    else snprintf(t, sizeof(t), "Fai una domanda a cui risponderà con la VERITÀ.\nEs. «%s»", EX_TRUE[(ex_t + n_cal) % N_EX]);
    ui_set_text(l_main, t);
    ui_set_text(l_hint, "Tocca Registra, aspetta il via, poi la domanda: risponde subito");
    set_buttons("Registra", NULL);
}

static void show_test(void)
{
    st = S_TEST;
    big(NULL, C_TEXT);
    ui_set_text(l_top, "Calibrazione fatta · domanda vera");
    ui_set_text(l_main, "Fai la domanda che vuoi verificare: il soggetto risponde come vuole.");
    ui_set_text(l_hint, "Tocca Registra, aspetta il via, poi la domanda");
    set_buttons("Registra", "Ricalibra");
}

static void start_rec(state_t what)
{
    rec_for = what;
    st = S_SETTLE;
    st_t0 = lv_tick_get();
    big(NULL, C_TEXT);
    ui_set_text(l_hint, what == S_BASE ? "Silenzio: braccio teso, respiro normale" : "Il soggetto tiene fermo il braccio");
    set_buttons("Annulla", NULL);
}

static void back_to_ready(void)
{
    if (rec_for == S_BASE) show_intro();
    else if (rec_for == S_CAL) show_cal();
    else show_test();
}

static void show_result(const feat_t *f)
{
    float sep, p = classify(f->x, &sep);
    int pc = (int)lroundf(p * 100);
    st = S_RESULT;
    const char *rel = sep < 1.0f ? "poco affidabile" : sep < 2.0f ? "abbastanza affidabile" : "affidabile";
    if (sep < 0.6f) big("NON SI CAPISCE", C_DIM);
    else if (p >= 0.7f) big("BUGIA", C_WARN);
    else if (p <= 0.3f) big("VERITÀ", C_OK);
    else big("INCERTO", lv_color_hex(0xFFC857));
    char t[160];
    snprintf(t, sizeof(t), "Probabilità di bugia %d%% · calibrazione %s", pc, rel);
    ui_set_text(l_top, t);
    // tremore medio delle risposte vere e false (rispetto alla base) e di questa
    float mt = 0, ml = 0;
    for (int i = 0; i < NCAL; i++) *(cal_lie[i] ? &ml : &mt) += cal[i][0] / 2;
    snprintf(t, sizeof(t), "Tremore: questa %+d%% · vere %+d%% · false %+d%%",
             (int)lroundf((expf(f->x[0]) - 1) * 100), (int)lroundf((expf(mt) - 1) * 100), (int)lroundf((expf(ml) - 1) * 100));
    ui_set_text(l_hint, sep < 0.6f ? "Vere e false si somigliano troppo: ricalibra" : t);
    set_buttons("Altra domanda", "Ricalibra");
}

static void finish_rec(void)
{
    if (rec_for == S_BASE) {
        float sg = 0, sa = 0, sm = 0;
        int n = BASE_S * BPS;
        for (int i = 0; i < n; i++) {
            const blk_t *b = &ring[(rec_b0 + i) % NBLK];
            sg += b->g * b->g;
            sa += b->a * b->a;
            sm += b->m;
        }
        base_g = sqrtf(sg / n);
        base_a = sqrtf(sa / n);
        base_m = sm / n;
        n_cal = 0;
        pattern = esp_random() & 1;
        ex_t = esp_random() % N_EX;
        ex_l = esp_random() % N_EX;
        show_cal();
        return;
    }
    feat_t f = features(rec_b0, REC_S * BPS);
    if (f.moved) {
        ui_toast("Si è mosso troppo: ripeti");
        back_to_ready();
        return;
    }
    if (rec_for == S_CAL) {
        memcpy(cal[n_cal], f.x, sizeof(f.x));
        cal_lie[n_cal] = cal_is_lie(n_cal);
        if (++n_cal < NCAL) show_cal();
        else show_test();
        return;
    }
    show_result(&f);
}

static void on_button(int i)
{
    switch (st) {
    case S_INTRO: start_rec(S_BASE); break;
    case S_CAL: start_rec(S_CAL); break;
    case S_TEST: if (i == 0) start_rec(S_TEST); else show_intro(); break;
    case S_RESULT: if (i == 0) show_test(); else show_intro(); break;
    case S_SETTLE:
    case S_REC: back_to_ready(); break;
    default: break;
    }
}

/* ---------------- traccia ---------------- */

static void draw_trace(void)
{
    uint32_t n = n_blk;
    if (n == drawn_blk) return;
    drawn_blk = n;
    float ref = base_g > 0 ? base_g : 0.5f;
    uint32_t k = n < NPTS ? n : NPTS;
    for (uint32_t i = 0; i < NPTS; i++) {
        float y = TR_H / 2;
        if (i >= NPTS - k) {
            const blk_t *b = &ring[(n - NPTS + i) % NBLK];
            // scala logaritmica: metà altezza = tremore doppio (o metà) rispetto alla base
            y = TR_H / 2 - logf((b->g + 1e-3f) / ref) / logf(2.0f) * (TR_H / 4);
            if (y < 1) y = 1;
            if (y > TR_H - 2) y = TR_H - 2;
        }
        pts[i].x = i * (TR_W - 1) / (NPTS - 1);
        pts[i].y = y;
    }
    lv_line_set_points(trace, pts, NPTS);
    const blk_t *b = &ring[(n - 1) % NBLK];
    char t[48];
    if (base_g > 0) snprintf(t, sizeof(t), "Tremore %+d%%", (int)lroundf((b->g / base_g - 1) * 100));
    else snprintf(t, sizeof(t), "Tremore %.2f °/s", b->g);
    ui_set_text(l_trace, t);
}

static void tick_cb(lv_timer_t *tm)
{
    draw_trace();
    uint32_t now = lv_tick_get();
    char t[96];
    if (st == S_SETTLE) {
        int left = (int)((SETTLE_MS - (now - st_t0) + 999) / 1000);
        if (now - st_t0 >= SETTLE_MS) {
            st = S_REC;
            st_t0 = now;
            rec_b0 = n_blk + 1;   // il blocco in corso è iniziato prima del via
            ui_set_text_color(l_main, C_TEXT);
        } else {
            snprintf(t, sizeof(t), "Pronti… %d", left);
            ui_set_text(l_main, t);
            return;
        }
    }
    if (st == S_REC) {
        int need = (rec_for == S_BASE ? BASE_S : REC_S) * BPS;
        int got = (int)(n_blk - rec_b0);
        if (got >= need) { lv_obj_set_style_line_color(trace, ui_accent(), 0); finish_rec(); return; }
        lv_obj_set_style_line_color(trace, C_WARN, 0);
        int left = (need - (got < 0 ? 0 : got) + BPS - 1) / BPS;
        if (rec_for == S_BASE) snprintf(t, sizeof(t), "Traccia base: fermo e in silenzio… %d", left);
        else snprintf(t, sizeof(t), "VIA! Fai la domanda adesso… %d", left);
        ui_set_text(l_main, t);
    }
}

/* ---------------- tocchi (come nel Q-20) ---------------- */

#define RELEASE_MS 70
static bool t_down, t_moved;
static int t_sx, t_sy, t_key = -1;
static uint32_t t_seen;
static lv_timer_t *touch_tmr;

static int hit(int x, int y)
{
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return -1;
    for (int i = 0; i < btn_n; i++) if (x >= btn_x[i] && x < btn_x[i] + btn_w[i]) return i;
    return -1;
}

static void touch_cb(lv_timer_t *tm)
{
    int x, y;
    uint32_t now = lv_tick_get();
    if (input_touch(&x, &y)) {
        t_seen = now;
        if (!t_down) {
            t_down = true; t_moved = false; t_sx = x; t_sy = y;
            t_key = hit(x, y);
            if (t_key >= 0) press_style(t_key, true);
            return;
        }
        if (abs(x - t_sx) > 26 || abs(y - t_sy) > 26) {
            t_moved = true;
            if (t_key >= 0) { press_style(t_key, false); t_key = -1; }
        }
        return;
    }
    if (!t_down || now - t_seen < RELEASE_MS) return;
    t_down = false;
    if (t_key >= 0) {
        int k = t_key;
        t_key = -1;
        press_style(k, false);
        if (!t_moved) on_button(k);
    }
}

/* ---------------- ciclo di vita ---------------- */

static lv_obj_t *label(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int x, int y, int w, bool wrap)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    l_top = label(root, &font_s, ui_accent(), 16, 4, SCR_W - 32, false);
    l_main = label(root, &font_m, C_TEXT, 16, 24, TR_X - 28, true);
    l_big = label(root, &font_l, C_TEXT, 16, 34, TR_X - 28, false);
    l_hint = label(root, &font_s, C_DIM, 16, 92, SCR_W - 32, false);

    trace_box = lv_obj_create(root);
    lv_obj_remove_style_all(trace_box);
    lv_obj_set_pos(trace_box, TR_X, TR_Y);
    lv_obj_set_size(trace_box, TR_W, TR_H);
    lv_obj_set_style_bg_opa(trace_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(trace_box, lv_color_hex(0x101418), 0);
    lv_obj_set_style_radius(trace_box, 6, 0);
    lv_obj_clear_flag(trace_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *mid = lv_obj_create(trace_box);   // la linea della base
    lv_obj_remove_style_all(mid);
    lv_obj_set_pos(mid, 0, TR_H / 2);
    lv_obj_set_size(mid, TR_W, 1);
    lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mid, C_FAINT, 0);
    trace = lv_line_create(trace_box);
    lv_obj_set_style_line_width(trace, 2, 0);
    lv_obj_set_style_line_color(trace, ui_accent(), 0);
    lv_obj_set_style_line_rounded(trace, true, 0);
    for (int i = 0; i < NPTS; i++) { pts[i].x = i * (TR_W - 1) / (NPTS - 1); pts[i].y = TR_H / 2; }
    lv_line_set_points(trace, pts, NPTS);
    l_trace = label(root, &font_s, C_DIM, TR_X, TR_Y + TR_H + 2, TR_W, false);

    for (int i = 0; i < NBTN; i++) {
        btn[i] = lv_obj_create(root);
        lv_obj_remove_style_all(btn[i]);
        lv_obj_set_style_radius(btn[i], 10, 0);
        lv_obj_set_style_bg_opa(btn[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        btn_l[i] = lv_label_create(btn[i]);
        lv_obj_set_style_text_font(btn_l[i], &font_m, 0);
        lv_obj_set_style_bg_color(btn[i], lv_color_hex(0x1C2025), 0);
        lv_obj_set_style_text_color(btn_l[i], C_TEXT, 0);
    }
    t_down = false;
    t_key = -1;
    if (!board_imu_ok()) {
        st = S_ERROR;
        ui_set_text(l_main, "Sensore di movimento non disponibile");
        set_buttons(NULL, NULL);
        return;
    }
    n_blk = drawn_blk = 0;
    base_g = 0;
    imu_start();
    show_intro();
    tmr = lv_timer_create(tick_cb, 50, NULL);
    touch_tmr = lv_timer_create(touch_cb, 15, NULL);
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    if (touch_tmr) { lv_timer_delete(touch_tmr); touch_tmr = NULL; }
    imu_stop();
}

static bool nav(nav_t ev)
{
    if (st == S_SETTLE || st == S_REC) {   // mentre misura nessun gesto esce
        if (ev == NAV_BTN) back_to_ready();
        return true;
    }
    return false;
}

const app_t app_lie = {
    .name = "Macchina della verità", .icon = ICON_EYE,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP | APP_OWN_QUICK,
};
