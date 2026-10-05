// app_tuner.c — Accordatore cromatico e guidato (chitarra, basso, ukulele)
//
// Rilevamento dell'altezza: algoritmo YIN su finestre da 85 ms a 24 kHz, media dei due
// microfoni (copre da ~30 Hz, SI grave del basso a 5 corde, a oltre 1,3 kHz). Il riferimento del LA è nelle impostazioni del menu Accordatore.
#include "apps.h"
#include "audio.h"
#include "settings.h"
#include <math.h>
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

/* ---------------- accordature ---------------- */

typedef struct {
    const char *name;
    int n;
    uint8_t midi[7];     // dalla corda più grave alla più acuta
} tuning_t;

#define T(nm, ...) {nm, sizeof((uint8_t[]){__VA_ARGS__}), {__VA_ARGS__}}

static const tuning_t gtr[] = {
    T("Standard", 40, 45, 50, 55, 59, 64),
    T("Drop D", 38, 45, 50, 55, 59, 64),
    T("Mezzo tono sotto", 39, 44, 49, 54, 58, 63),
    T("Un tono sotto", 38, 43, 48, 53, 57, 62),
    T("Drop C", 36, 43, 48, 53, 57, 62),
    T("Double Drop D", 38, 45, 50, 55, 59, 62),
    T("DADGAD", 38, 45, 50, 55, 57, 62),
    T("Open G", 38, 43, 50, 55, 59, 62),
    T("Open D", 38, 45, 50, 54, 57, 62),
    T("Open E", 40, 47, 52, 56, 59, 64),
    T("Open C", 36, 43, 48, 55, 60, 64),
};
static const tuning_t bass[] = {
    T("4 corde standard", 28, 33, 38, 43),
    T("4 corde Drop D", 26, 33, 38, 43),
    T("4 corde mezzo tono sotto", 27, 32, 37, 42),
    T("5 corde standard", 23, 28, 33, 38, 43),
    T("6 corde standard", 23, 28, 33, 38, 43, 48),
};
static const tuning_t uke[] = {
    T("Standard (Sol acuto)", 67, 60, 64, 69),
    T("Low G (Sol grave)", 55, 60, 64, 69),
    T("Re (Sol acuto)", 69, 62, 66, 71),
    T("Re con La grave", 57, 62, 66, 71),
    T("Baritono", 50, 55, 59, 64),
};

/* ---------------- nomi delle note ---------------- */

static const char *n_us[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *n_it[12] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};

static float a4(void) { return g_set.a4_x10 / 10.0f; }
static float midi_hz(float m) { return a4() * powf(2.0f, (m - 69.0f) / 12.0f); }
static float hz_midi(float f) { return 69.0f + 12.0f * log2f(f / a4()); }

static void tuning_notes(const tuning_t *t, char *b, int n)
{
    int o = 0;
    b[0] = 0;
    for (int i = 0; i < t->n; i++) o += snprintf(b + o, n - o, "%s%s", i ? " " : "", n_us[t->midi[i] % 12]);
}

/* ---------------- rilevamento (task microfono) ---------------- */

#define FS      AUDIO_RATE   // 24 kHz: a frequenze alte il sotto-campionamento costava qualche cent
#define WIN     2048         // 85 ms
#define TAU_MIN 18           // ~1330 Hz
#define TAU_MAX 800          // 30 Hz
#define HOP     1024         // un'analisi ogni ~43 ms

static volatile float det_hz;      // 0 = nessuna nota
static volatile float det_level;   // dBFS
static volatile uint32_t det_seq;
static volatile bool run;
static TaskHandle_t task_h;

static float yin(const float *x)
{
    EXT_RAM_BSS_ATTR static float d[TAU_MAX + 1], raw[TAU_MAX + 1];
    const int len = WIN - TAU_MAX;
    d[0] = 1;
    raw[0] = 0;
    float run_sum = 0;
    for (int tau = 1; tau <= TAU_MAX; tau++) {
        float s = 0;
        const float *a = x, *b = x + tau;
        for (int j = 0; j < len; j++) { float v = a[j] - b[j]; s += v * v; }
        raw[tau] = s;
        run_sum += s;
        d[tau] = run_sum > 0 ? s * tau / run_sum : 1;   // differenza normalizzata cumulativa
    }
    int best = -1;
    for (int tau = TAU_MIN; tau < TAU_MAX; tau++) {
        if (d[tau] < 0.12f) {
            while (tau + 1 < TAU_MAX && d[tau + 1] < d[tau]) tau++;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        // nessun valore sotto soglia: prendi il minimo globale se è abbastanza buono
        float mn = 1;
        for (int tau = TAU_MIN; tau < TAU_MAX; tau++) if (d[tau] < mn) { mn = d[tau]; best = tau; }
        if (mn > 0.30f) return 0;
    }
    // interpolazione parabolica sulla differenza grezza (meno distorta alle frequenze alte)
    float t = best;
    if (best > 1 && best < TAU_MAX) {
        float s0 = raw[best - 1], s1 = raw[best], s2 = raw[best + 1];
        float den = s0 + s2 - 2 * s1;
        if (fabsf(den) > 1e-9f) t += 0.5f * (s0 - s2) / den;
    }
    return FS / t;
}

static void mic_task(void *arg)
{
    EXT_RAM_BSS_ATTR static int16_t in[HOP * 2];
    float *buf = heap_caps_calloc(WIN, sizeof(float), MALLOC_CAP_INTERNAL);
    float hist[3] = {0};
    int filled = 0, hi = 0;
    float dc = 0;
    while (run && buf) {
        int n = audio_mic_read(in, HOP, 300);
        if (n <= 0) continue;
        memmove(buf, buf + n, (WIN - n) * sizeof(float));
        float *dst = buf + WIN - n;
        double e = 0;
        for (int i = 0; i < n; i++) {
            float x = 0.5f * ((float)in[2 * i] + in[2 * i + 1]) / 32768.0f;   // media dei due microfoni
            dc += (x - dc) * 0.001f;
            x -= dc;
            *dst++ = x;
            e += x * x;
        }
        filled += n;
        if (filled < WIN) continue;
        float lvl = 10.0f * log10f((float)(e / n) + 1e-12f);
        det_level = lvl;
        float f = lvl > -62.0f ? yin(buf) : 0;
        // mediana di 3 per eliminare salti d'ottava sporadici
        hist[hi] = f;
        hi = (hi + 1) % 3;
        float a = hist[0], b = hist[1], c = hist[2], med;
        if ((a <= b && b <= c) || (c <= b && b <= a)) med = b;
        else if ((b <= a && a <= c) || (c <= a && a <= b)) med = a;
        else med = c;
        det_hz = (f > 0 && med > 0) ? med : 0;
        det_seq++;
    }
    free(buf);
    task_h = NULL;
    vTaskDelete(NULL);
}

/* ---------------- interfaccia ---------------- */

#define G_CX   320
#define G_CY   166
#define G_R    116
#define TICKS  21

static const tuning_t *tun;   // NULL = cromatico
static lv_obj_t *l_note, *l_oct, *l_it, *l_hz, *l_cents, *l_status, *l_ref, *needle, *hub;
static lv_obj_t *chips[7];
static lv_point_precise_t tick_pts[TICKS][2], needle_pts[2];
static lv_timer_t *ui_timer;
static uint32_t seen;
static float shown_c;
static int last_string = -1, silent_frames;
static uint32_t in_tune_since;
static bool string_ok[7];

static void polar(float cents, float r, lv_point_precise_t *p)
{
    float ang = (cents / 50.0f) * 1.30f;   // ±50 cent → ±75°
    p->x = G_CX + r * sinf(ang);
    p->y = G_CY - r * cosf(ang);
}

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    lv_obj_set_pos(l, x, y);
    return l;
}

static void build_gauge(lv_obj_t *root)
{
    for (int i = 0; i < TICKS; i++) {
        float c = -50 + i * 5;
        bool major = i % 5 == 0, center = i == 10;
        polar(c, G_R, &tick_pts[i][0]);
        polar(c, G_R - (center ? 22 : major ? 16 : 9), &tick_pts[i][1]);
        lv_obj_t *t = lv_line_create(root);
        lv_line_set_points(t, tick_pts[i], 2);
        lv_obj_set_style_line_width(t, center ? 4 : major ? 3 : 2, 0);
        lv_color_t col = fabsf(c) <= 5 ? C_OK : fabsf(c) <= 15 ? lv_color_hex(0xFFB020) : C_DIM;
        lv_obj_set_style_line_color(t, col, 0);
        lv_obj_set_style_line_rounded(t, true, 0);
    }
    lv_point_precise_t p;
    polar(-50, G_R + 12, &p);
    lv_obj_t *lm = mk(root, &font_s, C_DIM, (int)p.x - 26, (int)p.y - 6);
    lv_label_set_text(lm, "-50");
    polar(50, G_R + 12, &p);
    lv_obj_t *lp = mk(root, &font_s, C_DIM, (int)p.x + 4, (int)p.y - 6);
    lv_label_set_text(lp, "+50");

    needle = lv_line_create(root);
    needle_pts[0] = (lv_point_precise_t){G_CX, G_CY};
    needle_pts[1] = (lv_point_precise_t){G_CX, G_CY - G_R + 26};
    lv_line_set_points(needle, needle_pts, 2);
    lv_obj_set_style_line_width(needle, 4, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    lv_obj_set_style_line_color(needle, C_DIM, 0);

    hub = lv_obj_create(root);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 14, 14);
    lv_obj_set_pos(hub, G_CX - 7, G_CY - 7);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hub, C_TEXT, 0);
}

static void set_needle(float cents, lv_color_t col)
{
    if (cents > 55) cents = 55;
    if (cents < -55) cents = -55;
    polar(cents, G_R - 26, &needle_pts[1]);
    lv_line_set_points(needle, needle_pts, 2);
    lv_obj_set_style_line_color(needle, col, 0);
}

static void style_chips(int active)
{
    if (!tun) return;
    for (int i = 0; i < tun->n; i++) {
        bool on = i == active;
        lv_obj_set_style_bg_opa(chips[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(chips[i], ui_accent(), 0);
        lv_obj_set_style_border_color(chips[i], string_ok[i] ? C_OK : C_FAINT, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(chips[i], 0), on ? C_BG : (string_ok[i] ? C_OK : C_TEXT), 0);
    }
}

static void show_ref(void)
{
    lv_label_set_text_fmt(l_ref, "LA = %.1f Hz", a4());
}

static void ui_cb(lv_timer_t *t)
{
    if (det_seq == seen) return;
    seen = det_seq;
    float f = det_hz;
    if (f <= 0) {
        // niente nota: dopo un attimo l'ago torna a riposo
        if (++silent_frames > 6) {
            lv_label_set_text(l_status, det_level > -62 ? "Suona una nota singola" : "In ascolto…");
            lv_obj_set_style_text_color(l_status, C_DIM, 0);
            set_needle(0, C_FAINT);
            in_tune_since = 0;
        }
        return;
    }
    silent_frames = 0;
    input_mark_activity();
    float m = hz_midi(f);
    int mi = (int)lroundf(m);
    if (mi < 0) return;
    lv_label_set_text(l_note, n_us[mi % 12]);
    lv_label_set_text_fmt(l_oct, "%d", mi / 12 - 1);
    lv_obj_align_to(l_oct, l_note, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -10);
    lv_label_set_text(l_it, n_it[mi % 12]);
    lv_label_set_text_fmt(l_hz, "%.1f Hz", f);

    // in modalità guidata: corda più vicina
    float target = mi;
    if (tun) {
        int best = 0;
        for (int i = 1; i < tun->n; i++)
            if (fabsf(m - tun->midi[i]) < fabsf(m - tun->midi[best])) best = i;
        target = tun->midi[best];
        if (best != last_string) { last_string = best; in_tune_since = 0; }
        style_chips(best);
    }
    float cents = (m - target) * 100.0f;
    shown_c += (cents - shown_c) * 0.45f;    // ago morbido
    float ac = fabsf(shown_c);
    lv_color_t col = ac <= 5 ? C_OK : ac <= 15 ? lv_color_hex(0xFFB020) : C_WARN;
    set_needle(shown_c, col);
    lv_obj_set_style_text_color(l_note, ac <= 5 ? C_OK : C_TEXT, 0);

    if (tun) {
        int tm = (int)target;
        lv_label_set_text_fmt(l_cents, "%+.0f cent · corda %s%d (%s) %.1f Hz", cents, n_us[tm % 12], tm / 12 - 1,
                              n_it[tm % 12], midi_hz(target));
    } else {
        lv_label_set_text_fmt(l_cents, "%+.1f cent", cents);
    }

    if (ac <= 5) {
        if (!in_tune_since) in_tune_since = lv_tick_get();
        bool held = lv_tick_elaps(in_tune_since) > 600;
        lv_label_set_text(l_status, held ? LV_SYMBOL_OK " Accordato" : "Quasi…");
        lv_obj_set_style_text_color(l_status, C_OK, 0);
        if (held && tun && last_string >= 0 && !string_ok[last_string]) {
            string_ok[last_string] = true;
            style_chips(last_string);
        }
    } else {
        in_tune_since = 0;
        // in modalità guidata il consiglio si basa sulla corda, con margine anche oltre ±50 cent
        lv_label_set_text(l_status, cents < 0 ? LV_SYMBOL_UP " Tendi la corda" : LV_SYMBOL_DOWN " Allenta la corda");
        lv_obj_set_style_text_color(l_status, ac <= 15 ? lv_color_hex(0xFFB020) : C_WARN, 0);
        if (!tun) lv_label_set_text(l_status, cents < 0 ? LV_SYMBOL_UP " Calante" : LV_SYMBOL_DOWN " Crescente");
    }
}

static void enter(lv_obj_t *root, void *arg)
{
    tun = arg;
    memset(string_ok, 0, sizeof(string_ok));
    last_string = -1;
    shown_c = 0;
    silent_frames = 99;

    // a sinistra: nota (americana grande, italiana sotto)
    l_note = mk(root, &font_xl, C_TEXT, 18, tun ? 36 : 14);
    lv_label_set_text(l_note, "-");
    l_oct = mk(root, &font_l, C_DIM, 0, 0);
    l_it = mk(root, &font_l, ui_accent(), 20, tun ? 118 : 100);

    // al centro: tachimetro
    build_gauge(root);

    // a destra: frequenza e stato
    l_hz = mk(root, &font_l, C_TEXT, 456, tun ? 40 : 18);
    lv_label_set_text(l_hz, "--- Hz");
    l_status = mk(root, &font_m, C_DIM, 456, tun ? 84 : 64);
    lv_label_set_text(l_status, "In ascolto…");
    l_ref = mk(root, &font_s, C_DIM, 456, SCR_H - 22);
    show_ref();
    l_cents = mk(root, &font_s, C_DIM, 456, tun ? 112 : 96);
    lv_obj_set_width(l_cents, SCR_W - 466);
    lv_label_set_long_mode(l_cents, LV_LABEL_LONG_WRAP);

    if (tun) {
        // in alto: le corde, dalla più grave
        int w = 46, x0 = G_CX - (tun->n * w) / 2;
        for (int i = 0; i < tun->n; i++) {
            lv_obj_t *c = lv_obj_create(root);
            lv_obj_remove_style_all(c);
            lv_obj_set_size(c, w - 6, 28);
            lv_obj_set_pos(c, x0 + i * w, 4);
            lv_obj_set_style_radius(c, 6, 0);
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_t *l = lv_label_create(c);
            lv_obj_set_style_text_font(l, &font_m, 0);
            lv_label_set_text(l, n_us[tun->midi[i] % 12]);
            lv_obj_center(l);
            chips[i] = c;
        }
        style_chips(-1);
        lv_obj_t *tn = mk(root, &font_s, C_DIM, 18, 8);
        lv_label_set_text(tn, tun->name);
    }

    if (!audio_init() || !audio_mic_start()) {
        lv_label_set_text(l_status, "Microfono non disponibile");
        return;
    }
    run = true;
    seen = det_seq;
    xTaskCreatePinnedToCore(mic_task, "tuner", 6144, NULL, 4, &task_h, 0);
    ui_timer = lv_timer_create(ui_cb, 30, NULL);
}

static void leave(void)
{
    run = false;
    for (int i = 0; i < 50 && task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    audio_mic_stop();
    if (ui_timer) { lv_timer_delete(ui_timer); ui_timer = NULL; }
}

static bool nav(nav_t ev)
{
    if (ev == NAV_SELECT || ev == NAV_BTN) {
        // azzera le spunte delle corde accordate
        memset(string_ok, 0, sizeof(string_ok));
        style_chips(last_string);
        return true;
    }
    return ev == NAV_NEXT || ev == NAV_PREV;
}

static const char *title(void *arg) { return arg ? ((const tuning_t *)arg)->name : "Accordatore cromatico"; }

const app_t app_tuner = {
    .name = "Accordatore", .icon = ICON_TUNER,
    .enter = enter, .leave = leave, .nav = nav, .title = title,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP,
};

/* ---------------- menu ---------------- */

#define MAX_ITEMS 12
static menu_item_t gtr_items[MAX_ITEMS], bass_items[MAX_ITEMS], uke_items[MAX_ITEMS];
static char hints[3][MAX_ITEMS][32];
static menu_t gtr_menu = {"Accordatore › Chitarra"}, bass_menu = {"Accordatore › Basso"}, uke_menu = {"Accordatore › Ukulele"};

static void fill(menu_t *m, menu_item_t *items, const tuning_t *t, int n, char h[][32], const char *icon)
{
    for (int i = 0; i < n && i < MAX_ITEMS; i++) {
        tuning_notes(&t[i], h[i], 32);
        items[i] = (menu_item_t){.icon = icon, .label = t[i].name, .hint = h[i], .app = &app_tuner, .arg = (void *)&t[i]};
    }
    m->items = items;
    m->count = n;
}

/* LA di riferimento: libero e preset storici */

typedef struct { uint16_t x10; const char *label; const char *desc; } pitch_t;
static const pitch_t pitches[] = {
    {4400, "440 Hz · standard odierno", "ISO 16, in uso dal 1955 (conferenza di Londra 1939)"},
    {4420, "442 Hz · orchestre europee", "molte orchestre italiane ed europee di oggi"},
    {4430, "443 Hz · area tedesca", "prassi di molte orchestre tedesche e austriache"},
    {4320, "432 Hz · \"accordatura verdiana\"", "sostenuto da Verdi nel 1884"},
    {4350, "435 Hz · diapason normal", "Francia 1859, adottato poi in molti paesi"},
    {4390, "439 Hz · New Philharmonic", "Londra, 1896"},
    {4525, "452,5 Hz · Old Philharmonic", "Londra, seconda metà dell'800"},
    {4300, "430 Hz · periodo classico", "circa l'epoca di Haydn e Mozart"},
    {4225, "422,5 Hz · diapason di Händel", "attribuito al suo diapason del 1751"},
    {4150, "415 Hz · barocco", "convenzione moderna per la musica barocca"},
    {3920, "392 Hz · barocco francese", "ton de chapelle, Versailles"},
    {4660, "466 Hz · Chorton / Cornetton", "barocco tedesco e veneziano, organi e ottoni"},
};
#define NP (int)(sizeof(pitches) / sizeof(pitches[0]))
static menu_item_t pitch_items[NP];

static void pick_pitch(void *arg)
{
    const pitch_t *p = arg;
    g_set.a4_x10 = p->x10;
    settings_save();
    char b[48];
    snprintf(b, sizeof(b), "LA impostato a %.1f Hz", p->x10 / 10.0f);
    ui_toast(b);
}

static menu_t pitch_menu = {"Accordatore › LA storici"};

static void v_a4(char *b, int n) { snprintf(b, n, "%.1f Hz", a4()); }
static void j_a4(int d)
{
    int v = g_set.a4_x10 + d * 5;   // passi da 0,5 Hz
    g_set.a4_x10 = v < 3800 ? 3800 : v > 4800 ? 4800 : v;
    settings_save();
}
static void a_a4_reset(void) { g_set.a4_x10 = 4400; settings_save(); ui_toast("LA riportato a 440 Hz"); }

static const menu_item_t tuner_items[] = {
    {.icon = ICON_TUNER, .label = "Cromatico", .hint = "qualsiasi strumento e voce", .app = &app_tuner},
    {.icon = ICON_GUITAR, .label = "Chitarra", .hint = "standard e accordature alternative", .app = &app_menu, .arg = &gtr_menu},
    {.icon = ICON_GUITAR, .label = "Basso", .hint = "4, 5 e 6 corde", .app = &app_menu, .arg = &bass_menu},
    {.icon = ICON_GUITAR, .label = "Ukulele", .hint = "Sol acuto, Low G, Re, baritono", .app = &app_menu, .arg = &uke_menu},
    {.icon = ICON_SLIDERS, .label = "LA di riferimento", .value = v_a4, .on_adjust = j_a4},
    {.icon = LV_SYMBOL_LIST, .label = "LA storici", .hint = "440, 442, 432, 415 barocco…", .app = &app_menu, .arg = &pitch_menu},
    {.icon = LV_SYMBOL_REFRESH, .label = "Riporta il LA a 440 Hz", .on_select = a_a4_reset},
};
menu_t tuner_menu = {"Accordatore", tuner_items, sizeof(tuner_items) / sizeof(tuner_items[0]), 0};

void tuner_menu_init(void)
{
    fill(&gtr_menu, gtr_items, gtr, sizeof(gtr) / sizeof(gtr[0]), hints[0], ICON_GUITAR);
    fill(&bass_menu, bass_items, bass, sizeof(bass) / sizeof(bass[0]), hints[1], ICON_GUITAR);
    fill(&uke_menu, uke_items, uke, sizeof(uke) / sizeof(uke[0]), hints[2], ICON_GUITAR);
    for (int i = 0; i < NP; i++)
        pitch_items[i] = (menu_item_t){.icon = LV_SYMBOL_AUDIO, .label = pitches[i].label, .hint = pitches[i].desc,
                                       .on_pick = pick_pitch, .arg = (void *)&pitches[i]};
    pitch_menu.items = pitch_items;
    pitch_menu.count = NP;
}
