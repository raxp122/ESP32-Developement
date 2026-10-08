// pet_test.c — prove sul PC delle regole del Polipetto (pet_core.c): genetica, spuntino
// come premio, "Gioca" veloce, salvataggi della versione 1.
// cd tools/pet && gcc -I../../main pet_test.c ../../main/pet_core.c -o pet_test && ./pet_test
#include "pet_core.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLITO riga %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void hatch(pet_t *p)
{
    pet_clock_t c = {.hour = -1, .sleep_h = -1, .wake_h = -1};
    pet_core_step(p, PET_EGG_S, &c);
}

int main(void)
{
    pet_t p;
    memset(&p, 0, sizeof(p));

    // un uovo selvatico nasce con nome, cognome, sesso e geni validi
    pet_core_new_egg(&p, 42);
    CHECK(p.stage == PET_EGG && p.family[0]);
    hatch(&p);
    CHECK(p.stage == PET_BABY && p.name[0] && (p.sex == SEX_M || p.sex == SEX_F) && p.uid);
    for (int g = 0; g < GENE_COUNT; g++) CHECK(p.genes[g][0] < pet_gene_count(g) && p.genes[g][1] < pet_gene_count(g));

    // dominanza: oro recessivo, puntini dominanti
    uint8_t a[2] = {COL_GOLD, COL_PURPLE}, b[2] = {COL_GOLD, COL_GOLD}, c[2] = {PAT_NONE, PAT_SPOTS};
    CHECK(pet_gene_show(a, GENE_COLOR) == COL_PURPLE);
    CHECK(pet_gene_show(b, GENE_COLOR) == COL_GOLD);
    CHECK(pet_gene_show(c, GENE_PATTERN) == PAT_SPOTS);

    // figli: ogni allele viene da un genitore (salvo mutazioni rare)
    uint8_t mom[GENE_COUNT][2] = {{COL_PURPLE, COL_PURPLE}, {PAT_SPOTS, PAT_SPOTS}, {0, 0}, {0, 0}};
    uint8_t dad[GENE_COUNT][2] = {{COL_BLUE, COL_BLUE}, {PAT_STRIPES, PAT_STRIPES}, {1, 1}, {2, 2}};
    uint32_t r = 7;
    int ok = 0, n = 2000, gold = 0;
    for (int i = 0; i < n; i++) {
        uint8_t k[GENE_COUNT][2];
        pet_genes_child(k, mom, dad, &r);
        if (k[GENE_COLOR][0] == COL_PURPLE && k[GENE_COLOR][1] == COL_BLUE) ok++;
        if (k[GENE_COLOR][0] == COL_GOLD || k[GENE_COLOR][1] == COL_GOLD) gold++;
    }
    printf("colore ereditato giusto: %d/%d, oro per mutazione: %d\n", ok, n, gold);
    CHECK(ok > n * 9 / 10 && gold < n / 20);

    // spuntino: dopo la sgridata è un premio (disciplina +1), poi tre al giorno gratis
    pet_core_new_egg(&p, 99);
    hatch(&p);
    p.stage = PET_CHILD; p.tantrum = 1; p.discipline = 0; p.happy = 1;
    pet_result_t res;
    pet_core_action(&p, ACT_SCOLD, &res);
    CHECK(res == RES_OK && p.discipline == 1 && p.t_reward == PET_REWARD_S);
    pet_core_action(&p, ACT_SNACK, &res);
    CHECK(res == RES_REWARD && p.discipline == 2 && p.t_reward == 0 && p.snacks_today == 0);
    uint8_t w0 = p.weight;
    for (int i = 0; i < PET_SNACKS_FREE; i++) { pet_core_action(&p, ACT_SNACK, &res); CHECK(res == RES_OK); }
    CHECK(p.weight == w0);
    pet_core_action(&p, ACT_SNACK, &res);
    CHECK(res == RES_GREEDY && p.weight == w0 + 2);

    // Gioca veloce: un cuore, poi ricarica
    p.happy = 1;
    pet_core_action(&p, ACT_PLAY, &res);
    CHECK(res == RES_OK && p.happy == 2);
    pet_core_action(&p, ACT_PLAY, &res);
    CHECK(res == RES_COOLDOWN && p.happy == 2);
    pet_clock_t ck = {.hour = -1, .sleep_h = -1, .wake_h = -1};
    pet_core_step(&p, PET_PLAY_CD_S, &ck);
    pet_core_action(&p, ACT_PLAY, &res);
    CHECK(res == RES_OK);

    // salvataggio v1: diventa v2 arancione con nome e sesso
    memset(&p, 0, sizeof(p));
    p.magic = PET_MAGIC; p.version = 1; p.stage = PET_ADULT; p.rng = 5;
    pet_core_upgrade(&p, 1);
    CHECK(p.version == PET_VERSION && p.name[0] && p.sex && pet_gene_show(p.genes[GENE_COLOR], GENE_COLOR) == COL_ORANGE);

    // partenza anticipata solo da adulto e con il permesso (uovo nel nido)
    CHECK(pet_core_release(&p, false) == 0);
    CHECK(pet_core_release(&p, true) != 0 && p.death == DEATH_OLD);

    // uovo con genitori: geni e nomi passano
    pet_egg_t e = {.genes = {{COL_GREEN, COL_PINK}}, .parent = {"Perla", "Guizzo"}, .family = "Abissi", .generation = 4};
    pet_core_new_egg_from(&p, 3, &e);
    CHECK(p.generation == 4 && !strcmp(p.parent[0], "Perla") && !strcmp(p.family, "Abissi") && p.genes[GENE_COLOR][1] == COL_PINK);

    printf(fails ? "%d prove fallite\n" : "tutto ok\n", fails);
    return fails != 0;
}
