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
    {.icon = ICON_EYE,      .label = "Morse",             .app = &app_menu, .arg = &morse_menu},
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

