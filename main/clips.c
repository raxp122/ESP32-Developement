// clips.c — elenco dei testi ricevuti dal PC, in RAM e salvato nell'NVS.
// Scritto dal task del Bluetooth (clips_add) e letto/modificato dal task di LVGL:
// tutto passa da un mutex. Il più recente è in testa (indice 0).
#include "clips.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "clips";

static clip_t items[CLIP_MAX];
static int n;
static uint32_t next_id = 1;
static volatile uint32_t gen;
static SemaphoreHandle_t mtx;

static void lock(void) { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock(void) { if (mtx) xSemaphoreGive(mtx); }
static void bump(void) { __atomic_add_fetch(&gen, 1, __ATOMIC_RELAXED); }

/* ---------------- persistenza ----------------
 * Una voce NVS per testo ("c<id>": intestazione + solo i caratteri usati) e un piccolo
 * indice ("idx": gli id dal più recente). Ogni modifica scrive poche centinaia di byte
 * invece dell'intero elenco, e un'interruzione a metà non fa perdere gli altri testi:
 *   aggiunta:      prima il testo, poi l'indice (un testo senza indice si pulisce all'avvio)
 *   cancellazione: prima l'indice, poi il testo
 */
typedef struct __attribute__((packed)) {
    uint32_t ts;
    uint8_t  used;
    uint8_t  pad;
    uint16_t len;
} rec_hdr_t;

static void key_of(uint32_t id, char *k) { snprintf(k, 16, "c%lu", (unsigned long)id); }

static bool write_rec(nvs_handle_t h, const clip_t *c)
{
    static uint8_t buf[sizeof(rec_hdr_t) + CLIP_MAX_LEN];   // protetto dal mutex
    rec_hdr_t hd = {.ts = c->ts, .used = c->used, .len = c->len};
    memcpy(buf, &hd, sizeof(hd));
    memcpy(buf + sizeof(hd), c->text, c->len);
    char k[16];
    key_of(c->id, k);
    return nvs_set_blob(h, k, buf, sizeof(hd) + c->len) == ESP_OK;
}

static bool write_index(nvs_handle_t h)
{
    uint32_t ids[CLIP_MAX];
    for (int i = 0; i < n; i++) ids[i] = items[i].id;
    if (nvs_set_u32(h, "next", next_id) != ESP_OK) return false;
    if (n == 0) { nvs_erase_key(h, "idx"); return true; }
    return nvs_set_blob(h, "idx", ids, n * sizeof(uint32_t)) == ESP_OK;
}

static nvs_handle_t open_rw(void)
{
    nvs_handle_t h;
    return nvs_open("clips", NVS_READWRITE, &h) == ESP_OK ? h : 0;
}

static bool read_rec(nvs_handle_t h, uint32_t id, clip_t *c)
{
    static uint8_t buf[sizeof(rec_hdr_t) + CLIP_MAX_LEN];
    char k[16];
    key_of(id, k);
    size_t len = sizeof(buf);
    if (nvs_get_blob(h, k, buf, &len) != ESP_OK || len < sizeof(rec_hdr_t)) return false;
    rec_hdr_t hd;
    memcpy(&hd, buf, sizeof(hd));
    if (hd.len >= CLIP_MAX_LEN || len != sizeof(hd) + hd.len) return false;
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->ts = hd.ts;
    c->used = hd.used;
    c->len = hd.len;
    memcpy(c->text, buf + sizeof(hd), hd.len);
    c->text[hd.len] = 0;
    return true;
}

// testi rimasti senza indice (spegnimento a metà di un'aggiunta o di una cancellazione)
static void drop_orphans(nvs_handle_t h)
{
    nvs_iterator_t it = NULL;
    char dead[8][16];
    int nd = 0;
    esp_err_t r = nvs_entry_find(NVS_DEFAULT_PART_NAME, "clips", NVS_TYPE_BLOB, &it);
    while (r == ESP_OK && nd < 8) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (info.key[0] == 'c') {
            uint32_t id = strtoul(info.key + 1, NULL, 10);
            bool known = false;
            for (int i = 0; i < n; i++) if (items[i].id == id) known = true;
            if (!known) snprintf(dead[nd++], sizeof(dead[0]), "%s", info.key);
        }
        r = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    for (int i = 0; i < nd; i++) nvs_erase_key(h, dead[i]);
    if (nd) nvs_commit(h);
}

// formato della prima versione: tutto l'elenco in un blob "list" (+ "n")
static void migrate_v1(nvs_handle_t h)
{
    uint8_t cnt = 0;
    if (nvs_get_u8(h, "n", &cnt) != ESP_OK) return;
    if (cnt > CLIP_MAX) cnt = CLIP_MAX;
    size_t len = cnt * sizeof(clip_t);
    if (cnt && nvs_get_blob(h, "list", items, &len) == ESP_OK && len == cnt * sizeof(clip_t)) {
        n = cnt;
        for (int i = 0; i < n; i++) {
            if (items[i].len >= CLIP_MAX_LEN) items[i].len = CLIP_MAX_LEN - 1;
            items[i].text[items[i].len] = 0;
            write_rec(h, &items[i]);
        }
        write_index(h);
    }
    nvs_erase_key(h, "list");
    nvs_erase_key(h, "n");
    nvs_commit(h);
    ESP_LOGI(TAG, "convertite %d voci al nuovo formato", n);
}

void clips_init(void)
{
    if (!mtx) mtx = xSemaphoreCreateMutex();
    lock();
    n = 0;
    next_id = 1;
    nvs_handle_t h = open_rw();
    if (h) {
        nvs_get_u32(h, "next", &next_id);
        uint32_t ids[CLIP_MAX];
        size_t len = sizeof(ids);
        if (nvs_get_blob(h, "idx", ids, &len) == ESP_OK && len % sizeof(uint32_t) == 0) {
            int cnt = len / sizeof(uint32_t);
            for (int i = 0; i < cnt; i++)
                if (read_rec(h, ids[i], &items[n])) n++;
            for (int i = 0; i < n; i++) if (items[i].id >= next_id) next_id = items[i].id + 1;
        } else {
            migrate_v1(h);
        }
        drop_orphans(h);
        nvs_close(h);
    }
    if (next_id == 0) next_id = 1;
    unlock();
    ESP_LOGI(TAG, "%d voci caricate", n);
}

int clips_count(void) { lock(); int r = n; unlock(); return r; }
uint32_t clips_gen(void) { return gen; }

bool clips_get(int i, clip_t *out)
{
    bool ok = false;
    lock();
    if (i >= 0 && i < n) { *out = items[i]; ok = true; }
    unlock();
    return ok;
}

int clips_add(const char *text, int len)
{
    if (!text) return -1;
    if (len < 0) len = strlen(text);
    if (len <= 0) return -1;
    if (len >= CLIP_MAX_LEN) len = CLIP_MAX_LEN - 1;
    lock();
    // evita di accumulare copie identiche consecutive (lo stesso testo copiato due volte)
    if (n > 0 && items[0].len == len && !memcmp(items[0].text, text, len)) { unlock(); return 0; }
    uint32_t dropped = 0;
    if (n >= CLIP_MAX) { dropped = items[CLIP_MAX - 1].id; n = CLIP_MAX - 1; }   // scarta la più vecchia
    memmove(&items[1], &items[0], n * sizeof(clip_t));
    clip_t *c = &items[0];
    memset(c, 0, sizeof(*c));
    c->id = next_id++;
    c->len = len;
    memcpy(c->text, text, len);
    c->text[len] = 0;
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    c->ts = t.tm_year >= 124 ? (uint32_t)now : 0;
    n++;
    nvs_handle_t h = open_rw();
    if (h) {
        bool ok = write_rec(h, c) && write_index(h);
        if (dropped) { char k[16]; key_of(dropped, k); nvs_erase_key(h, k); }
        nvs_commit(h);
        nvs_close(h);
        if (!ok) ESP_LOGW(TAG, "salvataggio non riuscito (memoria piena?)");
    }
    unlock();
    bump();
    return 0;
}

void clips_mark_used(uint32_t id)
{
    lock();
    for (int i = 0; i < n; i++)
        if (items[i].id == id && !items[i].used) {
            items[i].used = 1;
            nvs_handle_t h = open_rw();
            if (h) { write_rec(h, &items[i]); nvs_commit(h); nvs_close(h); }
            break;
        }
    unlock();
    bump();
}

static void delete_locked(int i)
{
    uint32_t id = items[i].id;
    memmove(&items[i], &items[i + 1], (n - i - 1) * sizeof(clip_t));
    n--;
    nvs_handle_t h = open_rw();
    if (h) {
        write_index(h);
        char k[16];
        key_of(id, k);
        nvs_erase_key(h, k);
        nvs_commit(h);
        nvs_close(h);
    }
}

void clips_delete(int i)
{
    lock();
    if (i >= 0 && i < n) delete_locked(i);
    unlock();
    bump();
}

bool clips_delete_id(uint32_t id)
{
    bool found = false;
    lock();
    for (int i = 0; i < n; i++)
        if (items[i].id == id) { delete_locked(i); found = true; break; }
    unlock();
    bump();
    return found;
}

void clips_clear(void)
{
    lock();
    n = 0;
    nvs_handle_t h = open_rw();
    if (h) { nvs_erase_all(h); nvs_set_u32(h, "next", next_id); nvs_commit(h); nvs_close(h); }
    unlock();
    bump();
}
