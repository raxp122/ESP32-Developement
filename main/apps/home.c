// home.c — menu principale: "Cerca" in cima, poi le app in ordine alfabetico
#include "apps.h"
#include "textnorm.h"
#include "settings.h"
#include <stdlib.h>
#include <string.h>

static menu_item_t items[] = {
    {.icon = ICON_SEARCH,   .label = "Cerca",             .app = &app_search},
    // da qui in poi l'ordine viene fatto da home_init(): aggiungi pure in fondo
    {.icon = ICON_MIC,      .label = "Berciometro",       .app = &app_bercio},
    {.icon = ICON_TUNER,    .label = "Accordatore",       .app = &app_menu, .arg = &tuner_menu},
    {.icon = ICON_GHOST,    .label = "Radar",             .app = &app_menu, .arg = &radar_menu},
    {.icon = ICON_CLOCK,    .label = "Orologio",          .app = &app_clock},
    {.icon = LV_SYMBOL_WIFI,.label = "Scanner Wi-Fi",     .app = &app_wifiscan},
    {.icon = LV_SYMBOL_BLUETOOTH, .label = "Scanner Bluetooth", .app = &app_blescan},
    {.icon = ICON_D20,      .label = "Dadi",              .app = &app_menu, .arg = &dice_menu},
    {.icon = ICON_BULB,     .label = "Torcia",            .app = &app_torch},
    {.icon = ICON_BOLT,     .label = "Spada laser",       .app = &app_menu, .arg = &saber_menu},
    {.icon = ICON_GHOST,    .label = "Doom",              .app = &app_menu, .arg = &doom_menu},
    {.icon = ICON_GAMEPAD,  .label = "Polipetto",         .app = &app_pet},
    {.icon = ICON_SLIDERS,  .label = "Livella",           .app = &app_level},
    {.icon = LV_SYMBOL_COPY, .label = "Appunti",           .app = &app_menu, .arg = &clips_menu},
    {.icon = ICON_EYE,      .label = "8-Ball veggente",   .app = &app_8ball},
    {.icon = ICON_GAMEPAD,  .label = "Snake",             .app = &app_snake},
    {.icon = ICON_GAMEPAD,  .label = "Q-20",              .app = &app_q20},
    {.icon = ICON_TUNER,    .label = "Theremin",          .app = &app_theremin},
    {.icon = LV_SYMBOL_SHUFFLE, .label = "Sismografo",     .app = &app_menu, .arg = &seismo_menu},
    {.icon = LV_SYMBOL_WIFI, .label = "Tester Wi-Fi",      .app = &app_wifitest},
    {.icon = ICON_GAMEPAD,  .label = "Scacchi",           .app = &app_menu, .arg = &chessplay_menu},
    {.icon = ICON_CLOCK,    .label = "Orologio scacchi",  .app = &app_menu, .arg = &chess_menu},
    {.icon = LV_SYMBOL_SETTINGS, .label = "Impostazioni", .app = &app_menu, .arg = &settings_menu},
};

menu_t home_menu = {
    .title = "Gadget",
    .items = items,
    .count = sizeof(items) / sizeof(items[0]),
};

static int by_label(const void *a, const void *b)
{
    char x[48], y[48];
    text_norm(((const menu_item_t *)a)->label, x, sizeof(x));
    text_norm(((const menu_item_t *)b)->label, y, sizeof(y));
    return strcmp(x, y);
}

void home_init(void)
{
    tuner_menu_init();
    chess_menu_init();
    qsort(items + 1, home_menu.count - 1, sizeof(menu_item_t), by_label);
}

/* ---------------- app all'avvio ----------------
 * L'indice è salvato nelle impostazioni: le voci nuove vanno aggiunte in fondo.
 * Doom è escluso perché riavvia la scheda in una modalità a parte.
 */
typedef struct { const char *name; const app_t *app; void *arg; } boot_app_t;

static const boot_app_t boot_apps[] = {
    {"Nessuna", NULL, NULL},
    {"Polipetto", &app_pet, NULL},
    {"Orologio", &app_clock, NULL},
    {"Torcia", &app_torch, NULL},
    {"Scanner Wi-Fi", &app_wifiscan, NULL},
    {"Scanner Bluetooth", &app_blescan, NULL},
    {"Radar", &app_menu, &radar_menu},
    {"Berciometro", &app_bercio, NULL},
    {"Accordatore", &app_menu, &tuner_menu},
    {"Dadi", &app_menu, &dice_menu},
    {"Spada laser", &app_menu, &saber_menu},
    {"Cerca", &app_search, NULL},
    {"Livella", &app_level, NULL},
    {"Appunti", &app_menu, &clips_menu},
    {"8-Ball veggente", &app_8ball, NULL},
    {"Q-20", &app_q20, NULL},
    {"Theremin", &app_theremin, NULL},
    {"Sismografo", &app_menu, &seismo_menu},   // al posto della Macchina della verità (stesso indice)
    {"Orologio scacchi", &app_menu, &chess_menu},
    {"Tester Wi-Fi", &app_wifitest, NULL},   // al posto della Mappa Wi-Fi (stesso indice)
    {"Snake", &app_snake, NULL},
    {"Scacchi", &app_menu, &chessplay_menu},
};
#define N_BOOT (int)(sizeof(boot_apps) / sizeof(boot_apps[0]))

int boot_app_count(void) { return N_BOOT; }

const char *boot_app_name(int i) { return (i > 0 && i < N_BOOT) ? boot_apps[i].name : boot_apps[0].name; }

void boot_app_launch(void)
{
    int i = g_set.boot_app;
    if (i > 0 && i < N_BOOT) ui_push(boot_apps[i].app, boot_apps[i].arg);
}
