// pet_book.c — le schermate "da sfogliare" del Polipetto: diario, album di famiglia,
// famiglia e geni, negozio. Su/giù sfoglia, destra agisce, sinistra torna indietro.
#include "pet_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int pg;   // pagina corrente (vale per il modulo aperto)

static void fmt_when(uint32_t epoch, uint32_t age_s, char *b, int n)
{
    static const char *const mesi[] = {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"};
    if (epoch) {
        time_t t = epoch;
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(b, n, "%d %s %d, %02d:%02d", lt.tm_mday, mesi[lt.tm_mon], lt.tm_year + 1900, lt.tm_hour, lt.tm_min);
    } else {
        char a[24];
        pu_fmt_age(age_s, a, sizeof(a));
        snprintf(b, n, "a %s di età", a);
    }
}

static bool flip_pages(nav_t ev, int n)
{
    if (n <= 0) return false;
    if (ev == NAV_NEXT) { pg = (pg + 1) % n; return true; }
    if (ev == NAV_PREV) { pg = (pg + n - 1) % n; return true; }
    return false;
}

// un piccolo polipetto (neonato) con i geni dati, per le anteprime
static void mini(int x, const uint8_t g[GENE_COUNT][2], bool seen, uint32_t now)
{
    pu_look_genes(g, 0, 0);
    int w, h;
    art_pet_size(PET_BABY, FORM_BASE, &w, &h);
    if (seen) art_pet(PET_BABY, FORM_BASE, x, FLOOR - h + 1, EXPR_HAPPY, now / 400 + x, false, 0, TINT_NONE);
    else art_pet(PET_BABY, FORM_BASE, x, FLOOR - h + 1, EXPR_NORMAL, 0, false, 0, TINT_GHOST);
    pu_look_self();
}

/* ---------------- diario ---------------- */

static void dia_enter(void) { pg = 0; }

static void dia_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    art_sprite(&SPR_BOOK, 10, 13, false);
    // le pagine si girano
    if ((now / 700) & 1) art_rect(14, 14, 1, 5, 0xC8D6E2);
    if (pet_core_alive(p)) {
        int w, h;
        pu_dims(&w, &h);
        art_pet(p->stage, p->form, LW - w - 8, FLOOR - h + 1, EXPR_NORMAL, now / 400, false, -1, TINT_NONE);
    }
}

static void dia_panel(char *big, int nb, char *hint, int nh)
{
    const pet_diary_t *d = pet_diary_get(pg);
    if (!d) { snprintf(big, nb, "Diario vuoto"); snprintf(hint, nh, "Qui finirà tutto quello che gli succede"); return; }
    fmt_when(d->epoch, d->age_s, big, nb);
    snprintf(hint, nh, "%s · %d/%d · su/giù sfoglia", d->text, pg + 1, pet_diary_count());
}

static bool dia_nav(nav_t ev) { return flip_pages(ev, pet_diary_count()) || ev == NAV_SELECT || ev == NAV_QUICK; }

const pet_mod_t PM_DIARY = {.enter = dia_enter, .draw = dia_draw, .nav = dia_nav, .panel = dia_panel};

/* ---------------- album di famiglia ---------------- */
// pagina 0: la collezione (colori e forme scoperti); poi il polipetto di adesso e gli antenati

static bool alb_now(void) { return pet_core_alive(pet_get()); }
static int alb_pages(void) { return 1 + (alb_now() ? 1 : 0) + pet_album_count(); }

static void alb_enter(void) { pg = alb_pages() > 1 ? 1 : 0; }

static int popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static void alb_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    if (pg == 0) {
        const pet_world_t *w = pet_world();
        for (int c = 0; c < COL_COUNT; c++) {
            uint8_t g[GENE_COUNT][2] = {{c, c}};
            mini(1 + c * 9, g, w->colors_seen >> c & 1, now);
        }
        return;
    }
    if (alb_now() && pg == 1) {
        int w, h;
        pu_dims(&w, &h);
        art_pet(p->stage, p->form, (LW - w) / 2, FLOOR - h + 1, EXPR_HAPPY, now / 400, false, 0, TINT_NONE);
        return;
    }
    const pet_album_t *a = pet_album_get(pg - 1 - (alb_now() ? 1 : 0));
    if (!a) return;
    pu_look_genes(a->genes, 0, 0);
    int w, h;
    art_pet_size(a->stage, a->form, &w, &h);
    int x = (LW - w) / 2, y = FLOOR - h + 1;
    if (a->death && a->death != DEATH_OLD) {   // un angioletto
        art_pet(a->stage, a->form, x, y - 4 + ((now / 600) & 1), EXPR_SLEEP, now / 500, false, 0, TINT_NONE);
        art_sprite(&SPR_HALO, x + w / 2 - 3, y - 7 + ((now / 600) & 1), false);
    } else {
        art_pet(a->stage, a->form, x, y, EXPR_HAPPY, now / 400, false, 0, TINT_NONE);
    }
    // una cornice da foto
    art_rect(x - 3, 1, w + 6, 1, 0xC9A66B);
    art_rect(x - 3, FLOOR + 2, w + 6, 1, 0xC9A66B);
    art_rect(x - 3, 1, 1, FLOOR + 2, 0xC9A66B);
    art_rect(x + w + 2, 1, 1, FLOOR + 2, 0xC9A66B);
    pu_look_self();
}

static void alb_panel(char *big, int nb, char *hint, int nh)
{
    const pet_world_t *w = pet_world();
    if (pg == 0) {
        snprintf(big, nb, "Collezione");
        snprintf(hint, nh, "Colori scoperti %d su %d · forme adulte %d su 5 · %d/%d", popcount(w->colors_seen), COL_COUNT,
                 popcount(w->forms_seen & ((1 << FORM_SAGE) | (1 << FORM_EXPLORER) | (1 << FORM_NORMAL) | (1 << FORM_GLUTTON) | (1 << FORM_MESSY))),
                 pg + 1, alb_pages());
        return;
    }
    char age[24];
    if (alb_now() && pg == 1) {
        const pet_t *p = pet_get();
        pu_fmt_age(p->age_s, age, sizeof(age));
        snprintf(big, nb, "%s %s", p->name, p->family);
        snprintf(hint, nh, "Adesso · gen. %u · %s · %s · %d/%d", p->generation, pet_stage_name(p), age, pg + 1, alb_pages());
        return;
    }
    const pet_album_t *a = pet_album_get(pg - 1 - (alb_now() ? 1 : 0));
    if (!a) return;
    pu_fmt_age(a->age_s, age, sizeof(age));
    snprintf(big, nb, "%s %s", a->name[0] ? a->name : "Polipetto", a->family);
    static const char *const end[] = {"partito", "di fame", "di sete", "di malattia", "tornato nell'oceano"};
    char par[48] = "";
    if (a->parent[0][0] && a->parent[1][0]) snprintf(par, sizeof(par), " · figlio di %s e %s", a->parent[0], a->parent[1]);
    snprintf(hint, nh, "Gen. %u · %s · visse %s (%s)%s · %d/%d", a->generation, pet_form_name(a->stage, a->form), age,
             end[a->death <= DEATH_OLD ? a->death : 0], par, pg + 1, alb_pages());
}

static bool alb_nav(nav_t ev) { return flip_pages(ev, alb_pages()) || ev == NAV_SELECT || ev == NAV_QUICK; }

const pet_mod_t PM_ALBUM = {.enter = alb_enter, .draw = alb_draw, .nav = alb_nav, .panel = alb_panel};

/* ---------------- famiglia e geni ---------------- */
// 0 genitori, 1..4 i quattro tratti, 5 il nido

#define FAM_PAGES (2 + GENE_COUNT)

static void fam_enter(void) { pg = 0; }

static void fam_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    art_background(now / 100);
    int w, h;
    pu_dims(&w, &h);
    if (pg >= 1 && pg <= GENE_COUNT) {
        // i due alleli: a sinistra due neonati che differiscono solo in questo tratto
        int gene = pg - 1;
        for (int k = 0; k < 2; k++) {
            uint8_t g[GENE_COUNT][2];
            memcpy(g, p->genes, sizeof(g));
            g[gene][0] = g[gene][1] = p->genes[gene][k];
            mini(3 + k * 11, g, true, now);
        }
        art_sprite(&SPR_SPARK, 25, 10, false);
    }
    if (pg == FAM_PAGES - 1) {
        if (pet_world()->has_egg) art_sprite(&SPR_NEST_EGG, 10, FLOOR - 5, false);
        else art_rect(10, FLOOR, 6, 1, 0x7A4A22);
    }
    if (p->stage == PET_EGG) art_egg(LW - EGG_W - 8, FLOOR - EGG_H + 1, 0);
    else if (pet_core_alive(p)) art_pet(p->stage, p->form, LW - w - 6, FLOOR - h + 1, EXPR_NORMAL, now / 400, false, -1, TINT_NONE);
    if (pg == 0 && p->parent[0][0]) {
        art_sprite(&SPR_HEART, 12, 8, false);
        art_sprite(&SPR_HEART, 20, 12, false);
    }
}

static const char *const temper_hint[TEMP_COUNT] = {
    "Si rattrista più piano e fa meno capricci",
    "Ogni vittoria nei minigiochi vale doppio, ma si annoia prima",
    "Ha fame più spesso, ma gli spuntini valgono doppio",
};
static const char *const dom_hint[GENE_COUNT] = {
    "l'oro è recessivo: si vede solo con due alleli oro",
    "i puntini dominano, le strisce sono recessive",
    "i tentacoli corti sono recessivi",
    "",
};

static void fam_panel(char *big, int nb, char *hint, int nh)
{
    const pet_t *p = pet_get();
    const pet_world_t *w = pet_world();
    if (pg == 0) {
        if (p->parent[0][0] && p->parent[1][0]) snprintf(big, nb, "%s e %s", p->parent[0], p->parent[1]);
        else if (p->parent[0][0] || p->parent[1][0]) snprintf(big, nb, "Figlio di %s", p->parent[0][0] ? p->parent[0] : p->parent[1]);
        else snprintf(big, nb, "Uovo selvatico");
        snprintf(hint, nh, "Famiglia %s · generazione %u · %s · su/giù: i geni", p->family, p->generation,
                 p->sex == SEX_F ? "femmina" : p->sex == SEX_M ? "maschio" : "sesso ancora ignoto");
        return;
    }
    if (pg <= GENE_COUNT) {
        int gene = pg - 1;
        int show = pet_gene_show(p->genes[gene], gene);
        snprintf(big, nb, "%s: %s", pet_gene_name(gene), pet_allele_name(gene, show));
        char why[72];
        snprintf(why, sizeof(why), "%s", gene == GENE_TEMPER ? temper_hint[show] : dom_hint[gene]);
        snprintf(hint, nh, "Alleli: %s (mamma) e %s (papà) · %s", pet_allele_name(gene, p->genes[gene][0]),
                 pet_allele_name(gene, p->genes[gene][1]), why);
        return;
    }
    if (w->has_egg) {
        snprintf(big, nb, "Uovo nel nido");
        snprintf(hint, nh, "Figlio di %s e %s: nascerà quando %s partirà per l'oceano (Impostazioni » Polipetto)",
                 w->nest.parent[0], w->nest.parent[1], p->name[0] ? p->name : "il polipetto");
    } else {
        snprintf(big, nb, "Nido vuoto");
        snprintf(hint, nh, "Da adulti, incontrando un polipetto dell'altro sesso (Incontra un amico) si ha un uovo a testa");
    }
}

static bool fam_nav(nav_t ev) { return flip_pages(ev, FAM_PAGES) || ev == NAV_SELECT || ev == NAV_QUICK; }

const pet_mod_t PM_FAMILY = {.enter = fam_enter, .draw = fam_draw, .nav = fam_nav, .panel = fam_panel};

/* ---------------- negozio ---------------- */

static int confirm_item;
static uint32_t confirm_until;

static void shop_enter(void) { pg = 0; confirm_item = 0; }

static int shop_item(void) { return 1 + pg; }

static void shop_draw(const pet_t *p)
{
    uint32_t now = pu_now();
    int it = shop_item();
    const art_item_t *a = &ART_ITEMS[it];
    pet_world_t *w = pet_world();
    // anteprima: la decorazione al suo posto, o il polipetto con addosso l'oggetto
    art_env_t e = {.daypart = pet_daypart(), .season = pet_season(), .holiday = HOL_NONE,
                   .deco = a->kind == ITEM_DECO ? 1u << it : 0};
    art_env(&e);
    art_background(now / 100);
    int stage = pet_core_alive(p) ? p->stage : PET_ADULT, form = pet_core_alive(p) ? p->form : FORM_NORMAL;
    if (stage == PET_BABY) stage = PET_CHILD;   // sul neonato non si vede niente
    pu_look_genes(p->genes, a->kind == ITEM_HAT ? it : w->hat, a->kind == ITEM_ACC ? it : w->acc);
    int pw, ph;
    art_pet_size(stage, form, &pw, &ph);
    int x = a->kind == ITEM_DECO ? LW - pw - 4 : (LW - pw) / 2;
    art_pet(stage, form, x, FLOOR - ph + 1, EXPR_HAPPY, now / 400, false, 0, TINT_NONE);
    pu_look_self();
    pu_env();
    // le conchiglie che hai, in alto
    art_sprite(&SPR_SHELL, 1, 1, false);
}

static bool owned(int it) { return pet_world()->owned >> it & 1; }

static bool in_use(int it)
{
    const pet_world_t *w = pet_world();
    switch (ART_ITEMS[it].kind) {
    case ITEM_HAT: return w->hat == it;
    case ITEM_ACC: return w->acc == it;
    default:       return w->deco >> it & 1;
    }
}

static void shop_panel(char *big, int nb, char *hint, int nh)
{
    int it = shop_item();
    const art_item_t *a = &ART_ITEMS[it];
    const pet_world_t *w = pet_world();
    snprintf(big, nb, "%s", a->name);
    static const char *const kind[] = {"cappello", "accessorio", "decorazione"};
    if (confirm_item == it && !((int32_t)(pu_now() - confirm_until) >= 0))
        snprintf(hint, nh, "Destra di nuovo per comprarlo: %u conchiglie (ne hai %u)", a->price, w->shells);
    else if (owned(it)) {
        bool deco = a->kind == ITEM_DECO;
        snprintf(hint, nh, "%s · tuo · destra: %s · %d/%d", kind[a->kind],
                 in_use(it) ? (deco ? "ritira" : "togli") : (deco ? "esponi" : "indossa"), pg + 1, ART_ITEM_COUNT - 1);
    } else
        snprintf(hint, nh, "%s · %u conchiglie · ne hai %u · %d/%d", kind[a->kind], a->price, w->shells, pg + 1, ART_ITEM_COUNT - 1);
}

static bool shop_nav(nav_t ev)
{
    if (flip_pages(ev, ART_ITEM_COUNT - 1)) { confirm_item = 0; return true; }
    if (ev != NAV_SELECT) return ev == NAV_QUICK;
    int it = shop_item();
    const art_item_t *a = &ART_ITEMS[it];
    pet_world_t *w = pet_world();
    if (!owned(it)) {
        if (w->shells < a->price) { pu_say("Non hai abbastanza conchiglie: vinci ai minigiochi!", true); pet_play(SND_NO); return true; }
        if (confirm_item != it || (int32_t)(pu_now() - confirm_until) >= 0) {
            confirm_item = it;
            confirm_until = pu_now() + 4000;
            return true;
        }
        confirm_item = 0;
        pet_shells_add(-(int)a->price);
        w->owned |= 1u << it;
        pet_diary_add("Comprato: %s", a->name);
        pet_play(SND_WIN);
        pu_say("Comprato! Destra per usarlo", false);
        pet_world_save();
        return true;
    }
    switch (a->kind) {
    case ITEM_HAT: w->hat = w->hat == it ? 0 : it; break;
    case ITEM_ACC: w->acc = w->acc == it ? 0 : it; break;
    default:       w->deco ^= 1u << it; break;
    }
    pet_play(SND_TICK);
    pet_world_save();
    return true;
}

const pet_mod_t PM_SHOP = {.enter = shop_enter, .draw = shop_draw, .nav = shop_nav, .panel = shop_panel};
