// app_8ball.c — 8-Ball veggente: fai una domanda, scuoti la scheda (o swipe a destra) e
// la sfera risponde con una delle 20 risposte classiche: 10 sì, 5 vaghe, 5 no.
#include "apps.h"
#include "board.h"
#include <math.h>
#include <stdio.h>
#include "esp_random.h"
#include "esp_heap_caps.h"

#define BALL     150                 // diametro della sfera
#define WIN      96                  // diametro della finestrella
#define TRI_W    80                  // triangolo dentro la finestrella
#define TRI_H    70
#define SHAKE_G  1.9f                // soglia dello scossone (in g)
#define COOLDOWN 1500                // ms tra una risposta e la successiva

typedef enum { POS, VAGUE, NEG } kind_t;
static const struct { const char *txt; kind_t k; } answers[20] = {
    {"È certo", POS},
    {"È decisamente così", POS},
    {"Senza alcun dubbio", POS},
    {"Sì, senza dubbio", POS},
    {"Puoi contarci", POS},
    {"Per come la vedo io, sì", POS},
    {"Molto probabilmente", POS},
    {"Le prospettive sono buone", POS},
    {"Sì", POS},
    {"I segni indicano di sì", POS},
    {"Risposta confusa, riprova", VAGUE},
    {"Chiedimelo più tardi", VAGUE},
    {"Meglio non dirtelo ora", VAGUE},
    {"Non posso prevederlo ora", VAGUE},
    {"Concentrati e chiedi di nuovo", VAGUE},
    {"Non contarci", NEG},
    {"La mia risposta è no", NEG},
    {"Le mie fonti dicono di no", NEG},
    {"Le prospettive non sono buone", NEG},
    {"Molto dubbio", NEG},
};

static lv_obj_t *ball, *win, *eight, *tri, *l_ans, *l_hint;
static lv_timer_t *tmr;
static uint16_t *tri_buf;
static int last = -1;
static uint32_t last_shake;
static bool busy;

/* ---------------- disegno ---------------- */

static lv_obj_t *circle(lv_obj_t *p, int d, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// triangolo blu con la punta in basso, come nella sfera vera
static void draw_triangle(void)
{
    lv_canvas_fill_bg(tri, lv_color_hex(0x05081C), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(tri, &layer);
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = lv_color_hex(0x2A49D8);
    d.bg_opa = LV_OPA_COVER;
    d.p[0].x = 0;          d.p[0].y = 0;
    d.p[1].x = TRI_W - 1;  d.p[1].y = 0;
    d.p[2].x = TRI_W / 2;  d.p[2].y = TRI_H - 1;
    lv_draw_triangle(&layer, &d);
    lv_canvas_finish_layer(tri, &layer);
}

static void set_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, v, 0); }
static void set_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }

static void fade(lv_obj_t *o, int from, int to, int ms, int delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_start(&a);
}

/* ---------------- risposta ---------------- */

static void reveal(lv_anim_t *a)
{
    int i;
    do i = esp_random() % 20; while (i == last);   // mai la stessa due volte di fila
    last = i;
    lv_obj_set_style_translate_x(ball, 0, 0);   // la sfera torna al suo posto
    lv_label_set_text(l_ans, answers[i].txt);
    lv_obj_set_style_text_color(l_ans, answers[i].k == POS ? C_OK : answers[i].k == NEG ? C_WARN : ui_accent(), 0);
    lv_obj_add_flag(eight, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(tri, LV_OBJ_FLAG_HIDDEN);
    // il triangolo affiora dal liquido scuro, poi compare la risposta
    fade(tri, 0, 255, 700, 0);
    fade(l_ans, 0, 255, 500, 450);
    lv_label_set_text(l_hint, "Un'altra domanda? Scuoti di nuovo");
    busy = false;
}

static void ask(void)
{
    if (busy) return;
    busy = true;
    last_shake = lv_tick_get();
    input_mark_activity();
    lv_obj_set_style_opa(l_ans, 0, 0);
    fade(tri, lv_obj_get_style_opa(tri, 0), 0, 150, 0);
    lv_label_set_text(l_hint, "La sfera sta pensando…");
    // la sfera ondeggia, poi rivela
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ball);
    lv_anim_set_values(&a, -9, 9);
    lv_anim_set_time(&a, 90);
    lv_anim_set_playback_time(&a, 90);
    lv_anim_set_repeat_count(&a, 4);
    lv_anim_set_exec_cb(&a, set_tx);
    lv_anim_set_completed_cb(&a, reveal);
    lv_anim_start(&a);
}

static void sample_cb(lv_timer_t *t)
{
    if (busy || display_is_dark() || lv_tick_elaps(last_shake) < COOLDOWN) return;
    vec3_t g;
    if (!board_imu_accel(&g)) return;
    if (sqrtf(g.x * g.x + g.y * g.y + g.z * g.z) > SHAKE_G) ask();
}

/* ---------------- ciclo di vita ---------------- */

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    busy = false;
    last_shake = lv_tick_get();

    ball = circle(root, BALL, lv_color_hex(0x15171B));
    lv_obj_set_style_border_width(ball, 2, 0);
    lv_obj_set_style_border_color(ball, lv_color_hex(0x30343A), 0);
    lv_obj_align(ball, LV_ALIGN_LEFT_MID, 36, 0);
    win = circle(ball, WIN, lv_color_hex(0x05081C));
    lv_obj_center(win);

    eight = lv_label_create(win);
    lv_obj_set_style_text_font(eight, &font_xl, 0);
    lv_obj_set_style_text_color(eight, C_TEXT, 0);
    lv_label_set_text(eight, "8");
    lv_obj_center(eight);

    tri = lv_canvas_create(win);
    uint32_t stride = lv_draw_buf_width_to_stride(TRI_W, LV_COLOR_FORMAT_RGB565);
    if (!tri_buf) tri_buf = heap_caps_aligned_alloc(64, stride * TRI_H, MALLOC_CAP_SPIRAM);
    if (tri_buf) {
        lv_canvas_set_buffer(tri, tri_buf, TRI_W, TRI_H, LV_COLOR_FORMAT_RGB565);
        draw_triangle();
    }
    lv_obj_align(tri, LV_ALIGN_CENTER, 0, 4);
    lv_obj_add_flag(tri, LV_OBJ_FLAG_HIDDEN);

    l_ans = lv_label_create(root);
    lv_obj_set_style_text_font(l_ans, &font_l, 0);
    lv_obj_set_width(l_ans, SCR_W - BALL - 100);
    lv_label_set_long_mode(l_ans, LV_LABEL_LONG_WRAP);
    lv_obj_align(l_ans, LV_ALIGN_LEFT_MID, BALL + 70, -14);
    lv_label_set_text(l_ans, "Fai una domanda…");
    lv_obj_set_style_text_color(l_ans, C_TEXT, 0);

    l_hint = lv_label_create(root);
    lv_obj_set_style_text_font(l_hint, &font_s, 0);
    lv_obj_set_style_text_color(l_hint, C_DIM, 0);
    lv_obj_align(l_hint, LV_ALIGN_BOTTOM_LEFT, BALL + 70, -14);
    lv_label_set_text(l_hint, board_imu_ok() ? "Pensala forte e scuoti la scheda · oppure swipe a destra"
                                             : "Pensala forte e fai swipe a destra");

    tmr = board_imu_ok() ? lv_timer_create(sample_cb, 40, NULL) : NULL;
}

static void leave(void)
{
    if (tmr) { lv_timer_delete(tmr); tmr = NULL; }
    // le animazioni legate agli oggetti si cancellano con gli oggetti
    lv_anim_delete(ball, NULL);
    lv_anim_delete(tri, NULL);
    lv_anim_delete(l_ans, NULL);
    busy = false;
}

static bool nav(nav_t ev)
{
    if (ev == NAV_SELECT) { ask(); return true; }
    return false;
}

const app_t app_8ball = {
    .name = "8-Ball veggente", .icon = ICON_EYE,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN,
};
