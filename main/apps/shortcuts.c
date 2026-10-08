// shortcuts.c — le scorciatoie per l'Azione rapida e l'App all'avvio. L'elenco si costruisce
// da solo dal launcher: tutte le app e, per quelle con un menu, le voci che aprono una
// schermata (es. "Accordatore » Cromatico", "Morse » Telegrafo"). Un'app nuova nel launcher
// compare qui senza fare niente. La scelta si salva per nome: l'elenco può cambiare ordine.
#include "apps.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define SC_OFF "Spegni schermo"
#define MAXSC 200

typedef struct { char name[48]; const app_t *app; void *arg; } sc_t;
static sc_t *sc;
static int n_sc;

static void add(const char *name, const app_t *app, void *arg)
{
    if (n_sc >= MAXSC) return;
    snprintf(sc[n_sc].name, sizeof(sc[n_sc].name), "%s", name);
    sc[n_sc].app = app;
    sc[n_sc].arg = arg;
    n_sc++;
}

static void build(void)
{
    if (sc) return;
    sc = heap_caps_calloc(MAXSC, sizeof(sc_t), MALLOC_CAP_SPIRAM);
    if (!sc) return;
    add("Nessuna", NULL, NULL);
    add(SC_OFF, NULL, NULL);
    for (int i = 0; i < home_menu.count; i++) {
        const menu_item_t *it = &home_menu.items[i];
        if (!it->app) continue;
        add(it->label, it->app, it->arg);
        if (it->app != &app_menu || !it->arg) continue;
        // un livello dentro: le voci che aprono una schermata (non quelle che cambiano un valore)
        const menu_t *m = it->arg;
        for (int k = 0; k < m->count; k++) {
            const menu_item_t *s = &m->items[k];
            if (!s->app || s->on_adjust) continue;
            char nm[48];
            snprintf(nm, sizeof(nm), "%s \xC2\xBB %s", it->label, s->label);
            add(nm, s->app, s->arg);
        }
    }
}

int shortcut_count(void) { build(); return n_sc; }
const char *shortcut_name(int i) { build(); return i >= 0 && i < n_sc ? sc[i].name : ""; }

static int find(const char *name)
{
    build();
    for (int i = 0; i < n_sc; i++) if (!strcmp(sc[i].name, name)) return i;
    return -1;
}

bool shortcut_run(const char *name)
{
    if (!strcmp(name, SC_OFF)) { ui_screen_off(); return true; }
    int i = find(name);
    if (i <= 0 || !sc[i].app) return false;
    ui_push(sc[i].app, sc[i].arg);
    return true;
}

/* ---------------- scelta (Impostazioni) ---------------- */

static menu_item_t *pick_items;
static menu_t pick_menu;
static bool pick_boot;

static void picked(void *arg)
{
    int i = (int)(intptr_t)arg;
    char *dst = pick_boot ? g_set.boot_name : g_set.quick_name;
    snprintf(dst, sizeof(g_set.quick_name), "%s", sc[i].name);
    settings_save();
    char t[80];
    snprintf(t, sizeof(t), "%s: %s", pick_boot ? "All'avvio" : "Azione rapida", sc[i].name);
    ui_pop();
    ui_toast(t);
}

void shortcut_pick(bool boot)
{
    build();
    if (!sc) return;
    if (!pick_items) pick_items = heap_caps_calloc(MAXSC, sizeof(menu_item_t), MALLOC_CAP_SPIRAM);
    if (!pick_items) return;
    pick_boot = boot;
    const char *cur = boot ? g_set.boot_name : g_set.quick_name;
    int k = 0, sel = 0;
    for (int i = 0; i < n_sc; i++) {
        if (boot && !strcmp(sc[i].name, SC_OFF)) continue;   // all'avvio non ha senso
        bool now = !strcmp(sc[i].name, cur);
        if (now) sel = k;
        pick_items[k] = (menu_item_t){
            .icon = i == 0 ? LV_SYMBOL_CLOSE : strstr(sc[i].name, "\xC2\xBB") ? LV_SYMBOL_RIGHT : LV_SYMBOL_PLAY,
            .label = sc[i].name, .hint = now ? LV_SYMBOL_OK " Scelta attuale" : "Destra per sceglierla",
            .on_pick = picked, .arg = (void *)(intptr_t)i,
        };
        k++;
    }
    pick_menu = (menu_t){boot ? "App all'avvio" : "Azione rapida", pick_items, k, sel, NULL};
    ui_push(&app_menu, &pick_menu);
}

void boot_app_launch(void)
{
    if (g_set.boot_name[0] && strcmp(g_set.boot_name, SC_OFF)) shortcut_run(g_set.boot_name);
}
