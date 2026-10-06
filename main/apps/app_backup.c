// app_backup.c — Impostazioni › Backup: crea un backup sulla microSD e ripristina quelli
// salvati. Il ripristino chiede conferma (secondo swipe) e poi riavvia la scheda.
#include "apps.h"
#include "backup.h"
#include "pet.h"
#include "settings.h"
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAXB 24

static backup_info_t *list;
static int n, sel;
static list_view_t lv;
static uint32_t confirm_until;
static char label[3][48], sub[96];

static const char *item(int i, char *buf, int len)
{
    if (i < 0 || i >= n) return NULL;
    if (list[i].created[0]) snprintf(buf, len, "%s", list[i].created);
    else snprintf(buf, len, "%s", list[i].name);
    return buf;
}

static void render(int dir)
{
    if (n <= 0) {
        list_view_set(&lv, LV_SYMBOL_SD_CARD, NULL, n < 0 ? "microSD assente" : "Nessun backup",
                      n < 0 ? "Inserisci la microSD" : "Crealo da Impostazioni › Backup", NULL, 0, 0, dir);
        return;
    }
    const backup_info_t *b = &list[sel];
    if (!b->usable) snprintf(sub, sizeof(sub), "%s", b->why);
    else if (lv_tick_get() < confirm_until) snprintf(sub, sizeof(sub), "Swipe a destra di nuovo: ripristina e riavvia");
    else snprintf(sub, sizeof(sub), "Firmware %s · swipe a destra per ripristinare", b->firmware);
    list_view_set(&lv, LV_SYMBOL_SD_CARD, item(sel - 1, label[0], 48), item(sel, label[1], 48), sub,
                  item(sel + 1, label[2], 48), sel, n, dir);
    lv_obj_set_style_text_color(lv.sub, !b->usable || lv_tick_get() < confirm_until ? C_WARN : ui_accent(), 0);
}

static void enter(lv_obj_t *root, void *arg)
{
    if (!list) list = heap_caps_malloc(sizeof(backup_info_t) * MAXB, MALLOC_CAP_SPIRAM);
    n = list ? backup_list(list, MAXB) : 0;
    if (sel >= n) sel = 0;
    confirm_until = 0;
    list_view_create(&lv, root);
    render(0);
}

static void restore(void)
{
    const backup_info_t *b = &list[sel];
    pet_save();   // se il ripristino fallisce a metà, almeno lo stato attuale è salvato
    if (!backup_restore(b->name)) {
        ui_toast(backup_error());
        return;
    }
    // riavvio subito: nessun salvataggio in memoria deve sovrascrivere i dati ripristinati
    list_view_set(&lv, LV_SYMBOL_OK, NULL, "Ripristinato", "Riavvio…", NULL, 0, 0, 0);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_NEXT: if (sel + 1 < n) { sel++; confirm_until = 0; render(+1); } return true;
    case NAV_PREV: if (sel > 0) { sel--; confirm_until = 0; render(-1); } return true;
    case NAV_SELECT:
        if (n <= 0 || !list[sel].usable) return true;
        if (lv_tick_get() < confirm_until) restore();
        else { confirm_until = lv_tick_get() + 4000; render(0); }
        return true;
    default:
        return false;
    }
}

static void tick(void)
{
    if (confirm_until && lv_tick_get() >= confirm_until) { confirm_until = 0; render(0); }
}

static const char *title(void *arg) { return "Impostazioni › Ripristina un backup"; }

const app_t app_restore = {
    .name = "Ripristino", .icon = LV_SYMBOL_SD_CARD,
    .enter = enter, .nav = nav, .tick = tick, .title = title,
};

/* ---------------- Impostazioni › Backup ---------------- */

static void v_where(char *b, int len)
{
    int c = backup_count();
    if (c < 0) snprintf(b, len, "microSD assente");
    else snprintf(b, len, "%d backup nella cartella /backup", c);
}

static void a_create(void)
{
    char name[40], t[96];
    pet_save();   // il polipetto in memoria va prima nell'NVS
    if (backup_create(name, sizeof(name))) {
        snprintf(t, sizeof(t), "Backup salvato: %s", name);
        ui_toast(t);
    } else {
        ui_toast(backup_error());
    }
}

static void v_auto(char *b, int len) { snprintf(b, len, "%s", g_set.backup_before_ota ? "Sì (se c'è la microSD)" : "No"); }
static void a_auto(void) { g_set.backup_before_ota = !g_set.backup_before_ota; settings_save(); }

static const menu_item_t items[] = {
    {.icon = LV_SYMBOL_SAVE, .label = "Crea un backup ora", .value = v_where, .on_select = a_create},
    {.icon = LV_SYMBOL_REFRESH, .label = "Ripristina un backup", .hint = "Solo backup di questa versione o più vecchi", .app = &app_restore},
    {.icon = LV_SYMBOL_DOWNLOAD, .label = "Backup prima degli aggiornamenti", .value = v_auto, .on_select = a_auto},
    {.icon = ICON_INFO, .label = "Cosa contiene",
     .hint = "Impostazioni, Wi-Fi, polipetto, Radar, livella. Il file è in chiaro: tienilo al sicuro"},
};
menu_t backup_menu = {"Impostazioni › Backup", items, sizeof(items) / sizeof(items[0]), 0};
