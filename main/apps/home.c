// home.c — menu principale: "Cerca" in cima, poi le app in ordine alfabetico
#include "apps.h"
#include "textnorm.h"
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
    qsort(items + 1, home_menu.count - 1, sizeof(menu_item_t), by_label);
}
