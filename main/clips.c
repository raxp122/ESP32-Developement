// clips.c — elenco dei testi ricevuti dal PC, in RAM e salvato nell'NVS.
// Scritto dal task del Bluetooth (clips_add) e letto/modificato dal task di LVGL:
// tutto passa da un mutex. Il più recente è in testa (indice 0).
#include "clips.h"
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

/* ---------------- persistenza ----------------
 * Tutto l'elenco in un unico blob "list" (≈ pochi KB): semplice e atomico.
 * Il blob è solo l'array usato (n voci), preceduto da n e next_id.
 */
static void save_locked(void)
{
    nvs_handle_t h;
    if (nvs_open("clips", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "next", next_id);
    nvs_set_blob(h, "list", items, n * sizeof(clip_t));
    nvs_set_u8(h, "n", (uint8_t)n);
    nvs_commit(h);
    nvs_close(h);
}

void clips_init(void)
{
    if (!mtx) mtx = xSemaphoreCreateMutex();
    lock();
    n = 0;
    next_id = 1;
    nvs_handle_t h;
    if (nvs_open("clips", NVS_READONLY, &h) == ESP_OK) {
        uint8_t cnt = 0;
        nvs_get_u8(h, "n", &cnt);
        nvs_get_u32(h, "next", &next_id);
        if (cnt > CLIP_MAX) cnt = CLIP_MAX;
        size_t len = cnt * sizeof(clip_t);
        if (cnt && nvs_get_blob(h, "list", items, &len) == ESP_OK && len == cnt * sizeof(clip_t))
            n = cnt;
        nvs_close(h);
    }
    // igiene: ogni voce deve essere terminata e di lunghezza coerente
    for (int i = 0; i < n; i++) {
        if (items[i].len >= CLIP_MAX_LEN) items[i].len = CLIP_MAX_LEN - 1;
        items[i].text[items[i].len] = 0;
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
    if (n >= CLIP_MAX) n = CLIP_MAX - 1;   // scarta la più vecchia
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
    save_locked();
    unlock();
    gen++;
    return 0;
}

void clips_mark_used(uint32_t id)
{
    lock();
    for (int i = 0; i < n; i++)
        if (items[i].id == id) { items[i].used = 1; save_locked(); break; }
    unlock();
    gen++;
}

void clips_delete(int i)
{
    lock();
    if (i >= 0 && i < n) {
        memmove(&items[i], &items[i + 1], (n - i - 1) * sizeof(clip_t));
        n--;
        save_locked();
    }
    unlock();
    gen++;
}

void clips_clear(void)
{
    lock();
    n = 0;
    save_locked();
    unlock();
    gen++;
}
