// app_q20.c — Q-20: pensa a qualcosa e il Gadget lo indovina con (circa) venti domande.
// Le risposte si danno toccando i pulsanti in basso; BOOT annulla l'ultima risposta.
// Quando perde chiede cosa pensavi (tastiera) e lo impara (vedi q20.c).
#include "apps.h"
#include "keyboard.h"
#include "q20.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BTN_Y   112
#define BTN_H   56
#define NBTN    5

// pulsanti delle risposte (in partita) o delle scelte (tentativo, fine)
static const struct { const char *txt; int a; } answers[NBTN] = {
    {"Sì", Q20_YES}, {"Forse sì", Q20_PROB_YES}, {"Non so", Q20_DUNNO}, {"Forse no", Q20_PROB_NO}, {"No", Q20_NO},
};

typedef enum { S_INTRO, S_ASK, S_GUESS, S_WIN, S_LOSE, S_ERROR } state_t;
static state_t st;
static int cur_q = -1, cur_guess = -1;
static lv_obj_t *l_top, *l_main, *l_hint;
static lv_obj_t *btn[NBTN], *btn_l[NBTN];
static int btn_n;                       // pulsanti visibili
static int btn_x[NBTN], btn_w[NBTN];
static lv_timer_t *touch_tmr;

/* ---------------- testo ---------------- */

// "orsacchiotto" → "Orsacchiotto" (prima lettera maiuscola, anche se accentata)
static void cap(const char *in, char *out, int n)
{
    snprintf(out, n, "%s", in);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
    else if ((unsigned char)out[0] == 0xC3 && (unsigned char)out[1] >= 0xA0 && (unsigned char)out[1] <= 0xBE) out[1] -= 32;
}

/* ---------------- pulsanti ---------------- */

static void set_buttons(const char *const *labels, int n)
{
    btn_n = n;
    int gap = 6, w = (SCR_W - 24 - gap * (n - 1)) / n;
    for (int i = 0; i < NBTN; i++) {
        if (i >= n) { lv_obj_add_flag(btn[i], LV_OBJ_FLAG_HIDDEN); continue; }
        btn_x[i] = 12 + i * (w + gap);
        btn_w[i] = w;
        lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(btn[i], btn_x[i], BTN_Y);
        lv_obj_set_size(btn[i], w, BTN_H);
        lv_label_set_text(btn_l[i], labels[i]);
        lv_obj_center(btn_l[i]);
    }
}

static void press_style(int i, bool down)
{
    lv_obj_set_style_bg_color(btn[i], down ? ui_accent() : lv_color_hex(0x1C2025), 0);
    lv_obj_set_style_text_color(btn_l[i], down ? C_BG : C_TEXT, 0);
}

/* ---------------- stati ---------------- */

static void show_intro(void)
{
    st = S_INTRO;
    char t[160];
    int g, w;
    q20_stats(&g, &w);
    ui_set_text(l_top, "Q-20");
    ui_set_text(l_main, "Pensa a qualcosa: un animale, un oggetto, un cibo, un luogo… Io lo indovino.");
    if (g) snprintf(t, sizeof(t), "Conosco %d cose · %d partite, ne ho indovinate %d%s", q20_count(), g, w,
                    q20_can_learn() ? "" : " · senza microSD non imparo");
    else snprintf(t, sizeof(t), "Conosco %d cose e imparo giocando%s", q20_count(),
                  q20_can_learn() ? "" : " (serve la microSD)");
    ui_set_text(l_hint, t);
    static const char *const b[] = {"Ho pensato, inizia!"};
    set_buttons(b, 1);
}

static void next(void);

static void show_question(void)
{
    st = S_ASK;
    char t[48];
    snprintf(t, sizeof(t), "Domanda %d", q20_asked() + 1);
    ui_set_text(l_top, t);
    ui_set_text(l_main, q20_question(cur_q));
    ui_set_text(l_hint, q20_asked() ? "BOOT: annulla l'ultima risposta" : "");
    static const char *b[NBTN];
    for (int i = 0; i < NBTN; i++) b[i] = answers[i].txt;
    set_buttons(b, NBTN);
}

static void show_guess(void)
{
    st = S_GUESS;
    char name[48], t[96];
    cap(q20_name(cur_guess), name, sizeof(name));
    snprintf(t, sizeof(t), "Stai pensando a… %s?", name);
    char top[48];
    snprintf(top, sizeof(top), "Dopo %d domande", q20_asked());
    ui_set_text(l_top, top);
    ui_set_text(l_main, t);
    ui_set_text(l_hint, "");
    static const char *const b[] = {"Sì, indovinato!", "No"};
    set_buttons(b, 2);
}

static void show_win(void)
{
    st = S_WIN;
    char name[48], t[96];
    cap(q20_name(cur_guess), name, sizeof(name));
    snprintf(t, sizeof(t), "Ho vinto! Era %s, in %d domande.", name, q20_asked());
    ui_set_text(l_top, "Q-20");
    ui_set_text(l_main, t);
    ui_set_text(l_hint, q20_can_learn() ? "Me lo ricorderò ancora meglio" : "");
    static const char *const b[] = {"Gioca ancora"};
    set_buttons(b, 1);
}

static void show_lose(void)
{
    st = S_LOSE;
    ui_set_text(l_top, "Q-20");
    ui_set_text(l_main, "Mi hai battuto! A cosa pensavi?");
    ui_set_text(l_hint, q20_can_learn() ? "Dimmelo e la prossima volta lo indovino" : "Senza microSD non posso impararlo");
    static const char *const b[] = {"Te lo scrivo", "Lascia stare"};
    set_buttons(b, 2);
}

static void next(void)
{
    cur_q = q20_next_question();
    if (cur_q >= 0) { show_question(); return; }
    cur_guess = q20_guess();
    if (cur_guess < 0) { show_lose(); return; }
    show_guess();
}

/* ---------------- tastiera: cosa pensavi ---------------- */

static void got_name(const char *t, void *arg)
{
    keyboard_close();
    ui_pop();   // chiude la tastiera e torna al gioco (che si ricostruisce in enter)
    if (t && t[0]) {
        char name[48], msg[96];
        int i = q20_teach(t);
        cap(i >= 0 ? q20_name(i) : t, name, sizeof(name));
        snprintf(msg, sizeof(msg), q20_can_learn() ? "Grazie! Ora conosco: %s" : "%s: me lo dimenticherò, manca la microSD", name);
        ui_toast(msg);
    } else {
        q20_teach("");   // conta comunque la partita
    }
}

static void kb_enter(lv_obj_t *root, void *arg) { keyboard_open(root, "Cosa pensavi?", "", false, 30, got_name, NULL); }
static void kb_leave(void) { keyboard_close(); }
static bool kb_nav(nav_t ev) { return keyboard_nav(ev); }

static const app_t app_q20_name = {
    .name = "Cosa pensavi", .enter = kb_enter, .leave = kb_leave, .nav = kb_nav,
    .flags = APP_FULLSCREEN | APP_OWN_QUICK,
};

/* ---------------- azioni ---------------- */

static void on_button(int i)
{
    switch (st) {
    case S_INTRO:
    case S_WIN:
        q20_new_game();
        next();
        break;
    case S_ASK:
        q20_answer(cur_q, answers[i].a);
        next();
        break;
    case S_GUESS:
        if (i == 0) { q20_win(cur_guess); show_win(); break; }
        q20_wrong(cur_guess);
        if (q20_over()) show_lose();
        else next();
        break;
    case S_LOSE:
        if (i == 0) ui_push(&app_q20_name, NULL);   // al ritorno: nuova schermata iniziale
        else { q20_teach(""); show_intro(); }
        break;
    default:
        break;
    }
}

// tocchi: stesso schema della tastiera (il touch perde il dito per qualche lettura)
#define RELEASE_MS   70
#define PRESS_MIN_MS 25
static bool t_down, t_moved;
static int t_sx, t_sy, t_key = -1;
static uint32_t t0, t_seen;

static int hit(int x, int y)
{
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return -1;
    for (int i = 0; i < btn_n; i++) if (x >= btn_x[i] && x < btn_x[i] + btn_w[i]) return i;
    return -1;
}

static void touch_cb(lv_timer_t *t)
{
    int x, y;
    uint32_t now = lv_tick_get();
    if (input_touch(&x, &y)) {
        t_seen = now;
        if (!t_down) {
            t_down = true; t_moved = false; t_sx = x; t_sy = y; t0 = now;
            t_key = hit(x, y);
            if (t_key >= 0) press_style(t_key, true);
            return;
        }
        if (abs(x - t_sx) > 26 || abs(y - t_sy) > 26) {
            t_moved = true;   // è uno swipe
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
        if (!t_moved && t_seen - t0 >= PRESS_MIN_MS) on_button(k);   // può cambiare schermata
    }
}

/* ---------------- ciclo di vita ---------------- */

static lv_obj_t *label(lv_obj_t *root, const lv_font_t *f, lv_color_t c, int y, bool wrap)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, 16, y);
    lv_obj_set_width(l, SCR_W - 32);
    lv_label_set_long_mode(l, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    l_top = label(root, &font_s, ui_accent(), 4, false);
    l_main = label(root, &font_m, C_TEXT, 24, true);
    l_hint = label(root, &font_s, C_DIM, 90, false);
    for (int i = 0; i < NBTN; i++) {
        btn[i] = lv_obj_create(root);
        lv_obj_remove_style_all(btn[i]);
        lv_obj_set_style_radius(btn[i], 10, 0);
        lv_obj_set_style_bg_opa(btn[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        btn_l[i] = lv_label_create(btn[i]);
        lv_obj_set_style_text_font(btn_l[i], &font_m, 0);
        press_style(i, false);
    }
    t_down = false;
    t_key = -1;
    if (!q20_init()) {
        st = S_ERROR;
        ui_set_text(l_main, "Memoria insufficiente per il Q-20");
        set_buttons(NULL, 0);
        return;
    }
    show_intro();   // anche tornando dalla tastiera: la partita è finita
    touch_tmr = lv_timer_create(touch_cb, 15, NULL);
}

static void leave(void)
{
    if (touch_tmr) { lv_timer_delete(touch_tmr); touch_tmr = NULL; }
}

static bool nav(nav_t ev)
{
    if (ev == NAV_BTN && st == S_ASK && q20_asked() > 0) {   // annulla l'ultima risposta
        q20_undo();
        next();
        return true;
    }
    if (ev == NAV_BTN && st == S_GUESS) {   // dal tentativo si torna all'ultima domanda
        q20_undo();
        next();
        return true;
    }
    return false;   // sinistra (o BOOT a inizio partita) esce
}

const app_t app_q20 = {
    .name = "Q-20", .icon = ICON_GAMEPAD,
    .enter = enter, .leave = leave, .nav = nav,
    .flags = APP_FULLSCREEN | APP_NO_SLEEP,
};
