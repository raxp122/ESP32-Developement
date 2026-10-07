// ui.h — framework del launcher: schermate a pila, barra di stato, menu
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "input.h"
#include "display.h"

// ---- Font (generati con lv_font_conv, includono accenti e icone) ----
// Due serie, scelte all'avvio in base alla scheda (fonts_sel.c): sullo schermo tondo i pixel
// sono più piccoli e i caratteri sono circa 1,4 volte più grandi (fra parentesi).
// Nel codice si usano come prima: &font_m, ecc.
extern const lv_font_t *ui_font_s, *ui_font_m, *ui_font_l, *ui_font_xl, *ui_font_icon;
#define font_s    (*ui_font_s)     // 14 px (20), testo + icone
#define font_m    (*ui_font_m)     // 20 px (28), testo + icone
#define font_l    (*ui_font_l)     // 34 px (46), testo + icone
#define font_xl   (*ui_font_xl)    // 72 px (96), cifre e maiuscole
#define font_icon (*ui_font_icon)  // 44 px (60), solo icone
void ui_fonts_init(void);          // dopo board_init

// ---- Icone extra (Font Awesome 5) oltre a LV_SYMBOL_* ----
#define ICON_DICE      "\xEF\x94\xA2"  // f522
#define ICON_D20       "\xEF\x9B\x8F"  // f6cf
#define ICON_BULB      "\xEF\x83\xAB"  // f0eb
#define ICON_INFO      "\xEF\x84\xA9"  // f129
#define ICON_DISH      "\xEF\x9F\x80"  // f7c0
#define ICON_TOWER     "\xEF\x94\x99"  // f519
#define ICON_CHIP      "\xEF\x8B\x9B"  // f2db
#define ICON_CLOCK     "\xEF\x80\x97"  // f017
#define ICON_SUN       "\xEF\x86\x85"  // f185
#define ICON_PALETTE   "\xEF\x94\xBF"  // f53f
#define ICON_BOLT      "\xEF\x83\xA7"  // f0e7
#define ICON_GAMEPAD   "\xEF\x84\x9B"  // f11b
#define ICON_TERMINAL  "\xEF\x84\xA0"  // f120
#define ICON_GHOST     "\xEF\x9B\xA2"  // f6e2
#define ICON_PLUG      "\xEF\x87\xA6"  // f1e6
#define ICON_SYNC      "\xEF\x8B\xB1"  // f2f1
#define ICON_EYE       "\xEF\x81\xAE"  // f06e
#define ICON_MOON      "\xEF\x86\x86"  // f186
#define ICON_MOBILE    "\xEF\x8F\x8D"  // f3cd
#define ICON_SLIDERS   "\xEF\x87\x9E"  // f1de
#define ICON_SEARCH    "\xEF\x80\x82"  // f002
#define ICON_MIC       "\xEF\x84\xB0"  // f130
#define ICON_TROPHY    "\xEF\x82\x91"  // f091
#define ICON_GUITAR    "\xEF\x9E\xA6"  // f7a6
#define ICON_TUNER     LV_SYMBOL_AUDIO

// ---- Colori ----
#define C_BG     lv_color_hex(0x000000)
#define C_TEXT   lv_color_hex(0xEDEDED)
#define C_DIM    lv_color_hex(0x6E747C)
#define C_FAINT  lv_color_hex(0x24282D)
#define C_WARN   lv_color_hex(0xFF6B57)
#define C_OK     lv_color_hex(0x5CFF8A)

#define STATUS_H   (SCR_ROUND ? 78 : 24)   // sul tondo: ora e icone in alto, sotto il titolo
#define CONTENT_H  (SCR_H - STATUS_H)

enum {
    APP_FULLSCREEN = 1 << 0,  // nasconde la barra di stato
    APP_NO_SLEEP   = 1 << 1,  // lo schermo non si spegne
    APP_OWN_QUICK  = 1 << 2,  // il tocco prolungato va all'app invece che all'azione rapida
    APP_ROUND_OK   = 1 << 3,  // disegnata anche per lo schermo tondo (le altre lì non si aprono)
};

typedef struct app_s {
    const char *name;
    const char *icon;
    void (*enter)(lv_obj_t *root, void *arg); // costruisce la UI dentro root
    void (*leave)(void);                      // libera risorse (gli oggetti LVGL li cancella il framework)
    bool (*nav)(nav_t ev);                    // true = evento gestito
    void (*tick)(void);                       // ogni 200 ms mentre l'app è in primo piano
    uint8_t flags;
    const char *(*title)(void *arg);          // titolo dinamico per la barra (opzionale)
} app_t;

void ui_init(void);
void ui_push(const app_t *app, void *arg);
void ui_pop(void);
void ui_home(void);
bool ui_closing(void);                 // dentro leave(): la schermata si chiude (indietro), non ne copre un'altra
void ui_rebuild(void);                 // ricostruisce la schermata corrente
void ui_toast(const char *msg);
void ui_screen_off(void);
void ui_set_brightness_override(int pct);   // -1 = usa quella delle impostazioni
void ui_power_off(void);
lv_color_t ui_accent(void);
// Come lv_label_set_text / lv_obj_set_style_text_color, ma non fanno nulla se il valore
// è già quello: ogni modifica fa ridisegnare e inviare al pannello l'intero schermo.
bool ui_set_text(lv_obj_t *label, const char *text);   // true se è cambiato
void ui_set_text_color(lv_obj_t *obj, lv_color_t c);
void ui_set_bg_color(lv_obj_t *obj, lv_color_t c);       // come sopra, per lo sfondo
int  ui_accent_count(void);
const char *ui_accent_name(int i);

// ---- Menu generico ----
typedef struct {
    const char *icon;
    const char *label;
    void (*value)(char *buf, int n);   // testo secondario (stato, valore)
    void (*on_select)(void);           // azione con swipe a destra
    void (*on_adjust)(int dir);        // se presente: destra = modifica, su/giù = cambia valore
    const app_t *app;                  // se presente: destra apre questa schermata
    void *arg;
    bool confirm;                      // serve un secondo swipe a destra per confermare
    const char *hint;                  // testo secondario fisso (se manca value)
    void (*on_pick)(void *arg);        // azione con argomento (riceve arg)
} menu_item_t;

typedef struct {
    const char *title;
    const menu_item_t *items;
    int count;
    int sel;
    void (*on_close)(void);   // opzionale: si esce dal menu tornando indietro (non aprendo una voce)
} menu_t;

extern const app_t app_menu;           // arg = menu_t*

// Helper per le app a lista: disegna le righe "precedente / corrente / successiva"
typedef struct {
    lv_obj_t *prev, *icon, *main, *sub, *next, *track, *thumb, *marker;
} list_view_t;
void list_view_create(list_view_t *lv, lv_obj_t *root);
void list_view_set(list_view_t *lv, const char *icon, const char *prev, const char *main,
                   const char *sub, const char *next, int index, int count, int dir);
