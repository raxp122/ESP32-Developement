// backup.c — backup e ripristino su microSD.
//
// File di testo, una voce per riga, così resta leggibile e robusto fra le versioni:
//   GADGET-BACKUP 1
//   firmware 0.14.30
//   settings 12
//   created 2026-10-06 21:30
//   ns gadget cfg blob 0b46…          voce dell'NVS: namespace, chiave, tipo, valore
//   ns pwn xp u32 1200
//   file pwn/pokedex.dat 14000        file della microSD, seguito da righe "data <hex>"
//   data 50574e31…
//   crc 1a2b3c4d                      CRC32 di tutte le righe precedenti
//   end
// Il ripristino controlla tutto (formato, versioni, CRC, righe) prima di toccare
// qualunque cosa; poi svuota i nostri namespace e riscrive le voci.
#include "backup.h"
#include "ota.h"
#include "pet_core.h"
#include "sd.h"
#include "settings.h"
#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "backup";

#define BACKUP_DIR  SD_MOUNT "/backup"
#define FORMAT      1
#define LINE_MAX    4700      // una voce da MAX_BLOB byte in esadecimale più l'intestazione
#define MAX_BLOB    2048
#define FILE_CHUNK  1024
#define MAX_FILE    (256 * 1024)

// Cosa finisce nel backup. Un namespace o un file nuovo va aggiunto qui.
static const char *const NAMESPACES[] = {"gadget", "pet", "pwn", "level", "theremin", "chess"};
static const char *const FILES[] = {"pwn/pokedex.dat", "q20/kb.txt"};   // q20: quello che ha imparato
#define N_NS    (int)(sizeof(NAMESPACES) / sizeof(NAMESPACES[0]))
#define N_FILES (int)(sizeof(FILES) / sizeof(FILES[0]))

static char err[96];

const char *backup_error(void) { return err; }

static bool fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, sizeof(err), fmt, ap);
    va_end(ap);
    ESP_LOGW(TAG, "%s", err);
    return false;
}

// copia troncando senza avvisi del compilatore (snprintf "%s" su buffer piccoli li dà)
static void copy(char *dst, size_t n, const char *src)
{
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

static bool known_ns(const char *ns)
{
    for (int i = 0; i < N_NS; i++)
        if (!strcmp(ns, NAMESPACES[i])) return true;
    return false;
}

static bool known_file(const char *rel)
{
    for (int i = 0; i < N_FILES; i++)
        if (!strcmp(rel, FILES[i])) return true;
    return false;
}

/* ---------------- scrittura ---------------- */

typedef struct { FILE *f; uint32_t crc; bool bad; } wr_t;

static void w_raw(wr_t *w, const void *s, size_t n)
{
    if (fwrite(s, 1, n, w->f) != n) w->bad = true;
    w->crc = esp_rom_crc32_le(w->crc, s, n);
}

static void w_fmt(wr_t *w, const char *fmt, ...)
{
    char b[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n > 0) w_raw(w, b, n < (int)sizeof(b) ? (size_t)n : sizeof(b) - 1);
}

static void w_hex(wr_t *w, const uint8_t *d, size_t n)
{
    static const char H[] = "0123456789abcdef";
    char b[256];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        b[k++] = H[d[i] >> 4];
        b[k++] = H[d[i] & 15];
        if (k == sizeof(b)) { w_raw(w, b, k); k = 0; }
    }
    if (k) w_raw(w, b, k);
}

static int dump_entry(wr_t *w, nvs_handle_t h, const char *ns, const nvs_entry_info_t *e)
{
    const char *k = e->key;
    switch (e->type) {
    case NVS_TYPE_U8:  { uint8_t v;  if (nvs_get_u8(h, k, &v))  return 0; w_fmt(w, "ns %s %s u8 %u\n", ns, k, v); return 1; }
    case NVS_TYPE_I8:  { int8_t v;   if (nvs_get_i8(h, k, &v))  return 0; w_fmt(w, "ns %s %s i8 %d\n", ns, k, v); return 1; }
    case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(h, k, &v)) return 0; w_fmt(w, "ns %s %s u16 %u\n", ns, k, v); return 1; }
    case NVS_TYPE_I16: { int16_t v;  if (nvs_get_i16(h, k, &v)) return 0; w_fmt(w, "ns %s %s i16 %d\n", ns, k, v); return 1; }
    case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(h, k, &v)) return 0; w_fmt(w, "ns %s %s u32 %lu\n", ns, k, (unsigned long)v); return 1; }
    case NVS_TYPE_I32: { int32_t v;  if (nvs_get_i32(h, k, &v)) return 0; w_fmt(w, "ns %s %s i32 %ld\n", ns, k, (long)v); return 1; }
    case NVS_TYPE_U64: { uint64_t v; if (nvs_get_u64(h, k, &v)) return 0; w_fmt(w, "ns %s %s u64 %llu\n", ns, k, (unsigned long long)v); return 1; }
    case NVS_TYPE_I64: { int64_t v;  if (nvs_get_i64(h, k, &v)) return 0; w_fmt(w, "ns %s %s i64 %lld\n", ns, k, (long long)v); return 1; }
    case NVS_TYPE_STR:
    case NVS_TYPE_BLOB: {
        bool str = e->type == NVS_TYPE_STR;
        size_t n = 0;
        if ((str ? nvs_get_str(h, k, NULL, &n) : nvs_get_blob(h, k, NULL, &n)) != ESP_OK || n > MAX_BLOB) return 0;
        uint8_t *b = malloc(n ? n : 1);
        if (!b) return 0;
        int ok = (str ? nvs_get_str(h, k, (char *)b, &n) : nvs_get_blob(h, k, b, &n)) == ESP_OK;
        if (ok) {
            if (str && n) n--;   // senza il terminatore
            w_fmt(w, "ns %s %s %s ", ns, k, str ? "str" : "blob");
            w_hex(w, b, n);
            w_raw(w, "\n", 1);
        }
        free(b);
        return ok;
    }
    default:
        return 0;
    }
}

static int dump_ns(wr_t *w, const char *ns)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;   // namespace vuoto
    int n = 0;
    nvs_iterator_t it = NULL;
    esp_err_t r = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY, &it);
    while (r == ESP_OK) {
        nvs_entry_info_t e;
        nvs_entry_info(it, &e);
        n += dump_entry(w, h, ns, &e);
        r = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);
    return n;
}

static void dump_file(wr_t *w, const char *rel)
{
    char path[80];
    snprintf(path, sizeof(path), SD_MOUNT "/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *b = malloc(FILE_CHUNK);
    if (b && size >= 0 && size <= MAX_FILE) {
        w_fmt(w, "file %s %ld\n", rel, size);
        size_t n;
        while ((n = fread(b, 1, FILE_CHUNK, f)) > 0) {
            w_raw(w, "data ", 5);
            w_hex(w, b, n);
            w_raw(w, "\n", 1);
        }
    }
    free(b);
    fclose(f);
}

bool backup_create(char *name, int n)
{
    if (!sd_ok()) return fail("microSD assente");
    mkdir(BACKUP_DIR, 0777);

    char fn[40], created[24] = "";
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_year >= 124) {
        strftime(fn, sizeof(fn), "%Y-%m-%d_%H%M%S.bak", &lt);
        strftime(created, sizeof(created), "%Y-%m-%d %H:%M", &lt);
    } else {
        // senza orologio: numerati
        struct stat st;
        char p[80];
        int i = 1;
        do {
            snprintf(fn, sizeof(fn), "backup-%03d.bak", i);
            snprintf(p, sizeof(p), BACKUP_DIR "/%s", fn);
        } while (stat(p, &st) == 0 && ++i < 1000);
    }

    char path[80], tmp[84];
    snprintf(path, sizeof(path), BACKUP_DIR "/%s", fn);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return fail("Non riesco a scrivere sulla microSD");

    wr_t w = {.f = f};
    w_fmt(&w, "GADGET-BACKUP %d\n", FORMAT);
    w_fmt(&w, "firmware %s\n", ota_current());
    w_fmt(&w, "settings %d\n", settings_version());
    w_fmt(&w, "created %s\n", created[0] ? created : "-");
    int entries = 0;
    for (int i = 0; i < N_NS; i++) entries += dump_ns(&w, NAMESPACES[i]);
    for (int i = 0; i < N_FILES; i++) dump_file(&w, FILES[i]);

    char tail[32];
    int tn = snprintf(tail, sizeof(tail), "crc %08lx\nend\n", (unsigned long)w.crc);
    if (fwrite(tail, 1, tn, f) != (size_t)tn) w.bad = true;
    if (fclose(f) != 0) w.bad = true;
    if (w.bad || !entries) {
        remove(tmp);
        return fail(entries ? "Scrittura sulla microSD non riuscita" : "Niente da salvare");
    }
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return fail("Scrittura sulla microSD non riuscita"); }
    if (name) snprintf(name, n, "%s", fn);
    ESP_LOGI(TAG, "backup %s: %d voci", fn, entries);
    return true;
}

/* ---------------- lettura ---------------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// decodifica in place; -1 se non è esadecimale valido
static int unhex(char *s, uint8_t *out, int max)
{
    int n = 0;
    while (s[0] && s[0] != '\n' && s[0] != '\r') {
        int a = hexval(s[0]), b = s[1] ? hexval(s[1]) : -1;
        if (a < 0 || b < 0 || n >= max) return -1;
        out[n++] = (uint8_t)(a << 4 | b);
        s += 2;
    }
    return n;
}

static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static bool read_info(FILE *f, backup_info_t *b)
{
    char line[96];
    memset(b->firmware, 0, sizeof(b->firmware));
    b->created[0] = 0;
    b->format = b->settings = 0;
    for (int i = 0; i < 4 && fgets(line, sizeof(line), f); i++) {
        chomp(line);
        if (i == 0 && sscanf(line, "GADGET-BACKUP %d", &b->format) != 1) return false;
        else if (!strncmp(line, "firmware ", 9)) copy(b->firmware, sizeof(b->firmware), line + 9);
        else if (!strncmp(line, "settings ", 9)) b->settings = atoi(line + 9);
        else if (!strncmp(line, "created ", 8) && strcmp(line + 8, "-")) copy(b->created, sizeof(b->created), line + 8);
    }
    return b->format > 0;
}

static void judge(backup_info_t *b)
{
    b->usable = false;
    if (b->format < 1 || b->format > FORMAT) snprintf(b->why, sizeof(b->why), "Formato non riconosciuto");
    else if (ota_compare(b->firmware, ota_current()) > 0)
        snprintf(b->why, sizeof(b->why), "Fatto con la %s: aggiorna prima il firmware", b->firmware);
    else if (b->settings > settings_version()) snprintf(b->why, sizeof(b->why), "Impostazioni di una versione più nuova");
    else { b->usable = true; b->why[0] = 0; }
}

static int by_name_desc(const void *a, const void *b)
{
    return strcmp(((const backup_info_t *)b)->name, ((const backup_info_t *)a)->name);
}

int backup_list(backup_info_t *out, int max)
{
    if (!sd_ok()) return -1;
    DIR *d = opendir(BACKUP_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l >= sizeof(out[n].name) || strcasecmp(e->d_name + l - 4, ".bak")) continue;
        char path[sizeof(BACKUP_DIR) + sizeof(out[n].name) + 1];
        backup_info_t *b = &out[n];
        copy(b->name, sizeof(b->name), e->d_name);
        snprintf(path, sizeof(path), BACKUP_DIR "/%s", b->name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (read_info(f, b)) { judge(b); n++; }
        fclose(f);
    }
    closedir(d);
    qsort(out, n, sizeof(backup_info_t), by_name_desc);
    return n;
}

int backup_count(void)
{
    static int cached = -2;
    static int64_t at;
    int64_t now = esp_timer_get_time();
    if (cached != -2 && now - at < 5000000) return cached;
    at = now;
    if (!sd_ok()) return cached = -1;
    DIR *d = opendir(BACKUP_DIR);
    int n = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t l = strlen(e->d_name);
            if (l >= 5 && !strcasecmp(e->d_name + l - 4, ".bak")) n++;
        }
        closedir(d);
    }
    return cached = n;
}

/* ---------------- ripristino ---------------- */

static bool set_entry(const char *ns, const char *key, const char *type, char *val, uint8_t *buf, bool apply)
{
    nvs_handle_t h = 0;
    bool keep = known_ns(ns);   // i namespace che questo firmware non conosce si ignorano
    if (apply && keep && nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return fail("Errore nella memoria interna");
    esp_err_t r = ESP_OK;
    char *end;
    if (!strcmp(type, "blob") || !strcmp(type, "str")) {
        int n = unhex(val, buf, MAX_BLOB);
        if (n < 0) { if (h) nvs_close(h); return fail("Backup danneggiato (dato non valido)"); }
        if (apply && keep) {
            // il polipetto riparte da "adesso": niente recupero del tempo passato dal backup
            if (!strcmp(ns, "pet") && !strcmp(key, "st") && n >= (int)(offsetof(pet_t, last_epoch) + sizeof(int64_t)) &&
                ((pet_t *)buf)->magic == PET_MAGIC)
                memset(buf + offsetof(pet_t, last_epoch), 0, sizeof(int64_t));
            if (type[0] == 's') { buf[n] = 0; r = nvs_set_str(h, key, (char *)buf); }
            else r = nvs_set_blob(h, key, buf, n);
        }
    } else {
        static const char *const nums[] = {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"};
        bool num = false;
        for (size_t i = 0; i < sizeof(nums) / sizeof(nums[0]); i++) num |= !strcmp(type, nums[i]);
        if (!num) { if (h) nvs_close(h); return true; }   // tipo di una versione futura: si ignora
        bool sign = type[0] == 'i';
        long long sv = sign ? strtoll(val, &end, 10) : 0;
        unsigned long long uv = sign ? 0 : strtoull(val, &end, 10);
        if (end == val) { if (h) nvs_close(h); return fail("Backup danneggiato (numero non valido)"); }
        if (apply && keep) {
            if (!strcmp(type, "u8")) r = nvs_set_u8(h, key, (uint8_t)uv);
            else if (!strcmp(type, "i8")) r = nvs_set_i8(h, key, (int8_t)sv);
            else if (!strcmp(type, "u16")) r = nvs_set_u16(h, key, (uint16_t)uv);
            else if (!strcmp(type, "i16")) r = nvs_set_i16(h, key, (int16_t)sv);
            else if (!strcmp(type, "u32")) r = nvs_set_u32(h, key, (uint32_t)uv);
            else if (!strcmp(type, "i32")) r = nvs_set_i32(h, key, (int32_t)sv);
            else if (!strcmp(type, "u64")) r = nvs_set_u64(h, key, (uint64_t)uv);
            else if (!strcmp(type, "i64")) r = nvs_set_i64(h, key, (int64_t)sv);
        }
    }
    if (h) {
        if (r == ESP_OK) r = nvs_commit(h);
        nvs_close(h);
    }
    return r == ESP_OK ? true : fail("Errore nella memoria interna");
}

// Legge tutto il file. apply = false: solo verifica (CRC, righe, valori). apply = true: scrive.
static bool process(FILE *f, char *line, uint8_t *buf, bool apply)
{
    uint32_t crc = 0;
    bool crc_ok = false, ended = false;
    FILE *out = NULL;
    long left = 0;
    int lineno = 0;
    while (fgets(line, LINE_MAX, f)) {
        lineno++;
        size_t len = strlen(line);
        if (len == LINE_MAX - 1 && line[len - 1] != '\n') { if (out) fclose(out); return fail("Backup danneggiato (riga troppo lunga)"); }
        bool is_crc = !strncmp(line, "crc ", 4);
        bool is_end = !strncmp(line, "end", 3);
        // dopo il CRC può venire solo "end": righe aggiunte lì non sarebbero coperte dal controllo
        if (crc_ok && !is_end) { if (out) fclose(out); return fail("Backup danneggiato (dati dopo il CRC)"); }
        if (is_crc) {
            crc_ok = strtoul(line + 4, NULL, 16) == crc;
            if (!crc_ok) { if (out) fclose(out); return fail("Backup danneggiato (controllo CRC fallito)"); }
            continue;
        }
        if (is_end) { ended = true; break; }
        crc = esp_rom_crc32_le(crc, (const uint8_t *)line, len);
        chomp(line);
        if (lineno <= 4) continue;   // intestazione, già letta

        if (!strncmp(line, "ns ", 3)) {
            char ns[16], key[16], type[8];
            int off = 0;
            if (sscanf(line, "ns %15s %15s %7s %n", ns, key, type, &off) != 3 || !off)
                return fail("Backup danneggiato (riga %d)", lineno);
            if (!set_entry(ns, key, type, line + off, buf, apply)) { if (out) fclose(out); return false; }
        } else if (!strncmp(line, "file ", 5)) {
            char rel[64];
            if (out) { fclose(out); out = NULL; }
            if (sscanf(line, "file %63s %ld", rel, &left) != 2 || left < 0 || left > MAX_FILE)
                return fail("Backup danneggiato (riga %d)", lineno);
            if (apply && known_file(rel)) {
                char path[80];
                snprintf(path, sizeof(path), SD_MOUNT "/%s", rel);
                char *slash = strrchr(path, '/');
                if (slash) { *slash = 0; mkdir(path, 0777); *slash = '/'; }
                out = fopen(path, "wb");
                if (!out) return fail("Non riesco a scrivere %s", rel);
            }
        } else if (!strncmp(line, "data ", 5)) {
            int n = unhex(line + 5, buf, MAX_BLOB);
            if (n < 0 || n > left) { if (out) fclose(out); return fail("Backup danneggiato (riga %d)", lineno); }
            left -= n;
            if (out && fwrite(buf, 1, n, out) != (size_t)n) { fclose(out); return fail("Scrittura sulla microSD non riuscita"); }
        }
        // righe sconosciute (backup di versioni future con cose nuove): si ignorano
    }
    if (out) fclose(out);
    if (!ended) return fail("Backup incompleto");
    if (!crc_ok) return fail("Backup danneggiato (controllo CRC fallito)");
    return true;
}

bool backup_restore(const char *name)
{
    if (!sd_ok()) return fail("microSD assente");
    char path[80];
    snprintf(path, sizeof(path), BACKUP_DIR "/%s", name);
    FILE *f = fopen(path, "r");
    if (!f) return fail("Backup non trovato");

    backup_info_t info;
    if (!read_info(f, &info)) { fclose(f); return fail("Non è un backup del Gadget"); }
    judge(&info);
    if (!info.usable) { fclose(f); return fail("%s", info.why); }

    char *line = heap_caps_malloc(LINE_MAX, MALLOC_CAP_SPIRAM);
    uint8_t *buf = heap_caps_malloc(MAX_BLOB + 1, MALLOC_CAP_SPIRAM);
    bool ok = line && buf;
    if (!ok) fail("Memoria insufficiente");

    // 1) verifica completa, senza toccare niente
    if (ok) { rewind(f); ok = process(f, line, buf, false); }
    // 2) svuota i nostri namespace (quello che il backup non ha torna al predefinito) e scrive
    if (ok) {
        for (int i = 0; i < N_NS; i++) {
            nvs_handle_t h;
            if (nvs_open(NAMESPACES[i], NVS_READWRITE, &h) == ESP_OK) {
                nvs_erase_all(h);
                nvs_commit(h);
                nvs_close(h);
            }
        }
        // anche i file: quelli che il backup non contiene spariscono (tornano "nuovi")
        for (int i = 0; i < N_FILES; i++) {
            char p[80];
            snprintf(p, sizeof(p), SD_MOUNT "/%s", FILES[i]);
            remove(p);
        }
        rewind(f);
        ok = process(f, line, buf, true);
    }
    free(line);
    free(buf);
    fclose(f);
    if (ok) ESP_LOGI(TAG, "ripristinato %s (firmware %s)", name, info.firmware);
    return ok;
}
