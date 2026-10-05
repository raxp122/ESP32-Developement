// ota.c — aggiornamenti via internet. Ogni merge su main pubblica una release su GitHub
// (vedi .github/workflows/build.yml) con:
//   version.json    {"version":"0.14.N"}
//   gadget-app.bin  il firmware da installare nello slot OTA libero
// La scheda legge version.json, confronta con la versione in uso e, se l'utente
// conferma, scarica il firmware in HTTPS (certificati dal bundle di ESP-IDF).
// Con il rollback del bootloader attivo: se il nuovo firmware non arriva a chiamare
// ota_mark_valid() (si blocca o si riavvia prima), al riavvio torna il precedente.
#include "ota.h"
#include "display.h"
#include "pet.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

#define OTA_REPO     "raxp122/ESP32-Developement"
#define URL_VERSION  "https://github.com/" OTA_REPO "/releases/latest/download/version.json"
#define URL_APP      "https://github.com/" OTA_REPO "/releases/latest/download/gadget-app.bin"

static volatile ota_state_t state = OTA_IDLE;
static volatile int progress;
static bool busy;   // un controllo o un'installazione in corso (accesso atomico)
static char latest[32], err_msg[64];

ota_state_t ota_state(void) { return state; }
int ota_progress(void) { return progress; }
const char *ota_latest(void) { return latest; }
const char *ota_error(void) { return err_msg; }
const char *ota_current(void) { return esp_app_get_description()->version; }

static void fail(const char *msg)
{
    snprintf(err_msg, sizeof(err_msg), "%s", msg);
    state = OTA_ERROR;
    ESP_LOGW(TAG, "%s", msg);
}

void ota_mark_valid(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "nuovo firmware %s confermato", ota_current());
    }
}

/* ---------------- versioni ---------------- */

// "0.14.7", "v0.14.7-3-gabc": i primi tre numeri; tutto il resto conta 0
static void parse_ver(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    for (int i = 0; i < 3 && *s >= '0' && *s <= '9'; i++) {
        v[i] = (int)strtol(s, (char **)&s, 10);
        if (*s != '.') break;
        s++;
    }
}

static bool newer(const char *a, const char *b)   // a più nuova di b?
{
    int x[3], y[3];
    parse_ver(a, x);
    parse_ver(b, y);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

/* ---------------- version.json ---------------- */

typedef struct { char buf[256]; int len; } body_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    // con i redirect di GitHub conta solo il corpo della risposta finale
    if (e->event_id == HTTP_EVENT_ON_DATA && esp_http_client_get_status_code(e->client) == 200) {
        int n = e->data_len;
        if (b->len + n > (int)sizeof(b->buf) - 1) n = sizeof(b->buf) - 1 - b->len;
        if (n > 0) { memcpy(b->buf + b->len, e->data, n); b->len += n; }
    }
    return ESP_OK;
}

static bool fetch_latest(void)
{
    body_t body = {0};
    esp_http_client_config_t c = {
        .url = URL_VERSION,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,      // gli URL di redirect di GitHub sono lunghi
        .buffer_size_tx = 2048,
        .event_handler = on_http,
        .user_data = &body,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { fail("Memoria insufficiente"); return false; }
    esp_err_t r = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if (r != ESP_OK) { fail("GitHub non raggiungibile"); return false; }
    if (status == 404) { fail("Nessuna versione pubblicata"); return false; }
    if (status != 200) { snprintf(err_msg, sizeof(err_msg), "Risposta inattesa (%d)", status); state = OTA_ERROR; return false; }

    // {"version":"0.14.N", ...}
    const char *k = strstr(body.buf, "\"version\"");
    const char *q = k ? strchr(k + 9, '"') : NULL;
    const char *e = q ? strchr(q + 1, '"') : NULL;
    if (!e || e - q - 1 <= 0 || e - q - 1 >= (int)sizeof(latest)) { fail("version.json non valido"); return false; }
    memcpy(latest, q + 1, e - q - 1);
    latest[e - q - 1] = 0;
    return true;
}

/* ---------------- task ---------------- */

typedef enum { JOB_CHECK, JOB_INSTALL, JOB_AUTO } job_t;

static void do_check(void)
{
    state = OTA_CHECKING;
    if (wifi_mgr_state() != WIFI_CONNECTED) { fail("Collega il Wi-Fi a internet"); return; }
    if (!fetch_latest()) return;
    state = newer(latest, ota_current()) ? OTA_AVAILABLE : OTA_UP_TO_DATE;
    ESP_LOGI(TAG, "in uso %s, disponibile %s", ota_current(), latest);
}

static void do_install(void)
{
    if (wifi_mgr_state() != WIFI_CONNECTED) { fail("Collega il Wi-Fi a internet"); return; }
    state = OTA_DOWNLOADING;
    progress = 0;
    esp_http_client_config_t http = {
        .url = URL_APP,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = {.http_config = &http};
    esp_https_ota_handle_t h = NULL;
    if (esp_https_ota_begin(&cfg, &h) != ESP_OK) { fail("Download non riuscito"); return; }

    // controllo di sicurezza: deve essere davvero un firmware di questo progetto
    esp_app_desc_t d;
    if (esp_https_ota_get_img_desc(h, &d) != ESP_OK ||
        strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name))) {
        esp_https_ota_abort(h);
        fail("Il file scaricato non è un firmware del Gadget");
        return;
    }
    int total = esp_https_ota_get_image_size(h);
    esp_err_t r;
    while ((r = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(h);
        if (total > 0) progress = got * 100 / total;
    }
    if (r != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        fail("Download interrotto");
        return;
    }
    if (esp_https_ota_finish(h) != ESP_OK) { fail("Firmware non valido"); return; }

    progress = 100;
    state = OTA_DONE;
    ESP_LOGI(TAG, "installato %s: riavvio", d.version);
    // salva il polipetto (vive nel task di LVGL) e riparte con il firmware nuovo
    display_lock();
    pet_save();
    display_unlock();
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static void do_auto(void)
{
    // aspetta internet (anche a lungo: il Wi-Fi può arrivare dopo), poi un controllo solo
    while (wifi_mgr_state() != WIFI_CONNECTED) vTaskDelay(pdMS_TO_TICKS(5000));
    vTaskDelay(pdMS_TO_TICKS(20000));   // lascia finire NTP e il resto dell'avvio
    // se nel frattempo l'utente ha già controllato (o sta installando), lascia stare
    if (state != OTA_IDLE || wifi_mgr_state() != WIFI_CONNECTED) return;
    if (__atomic_exchange_n(&busy, true, __ATOMIC_ACQ_REL)) return;
    do_check();
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    if (state == OTA_AVAILABLE) {
        char t[112];
        snprintf(t, sizeof(t), "Aggiornamento %s disponibile: Impostazioni › Sistema", latest);
        display_lock();
        ui_toast(t);
        display_unlock();
    } else if (state == OTA_ERROR) {
        state = OTA_IDLE;   // un controllo automatico fallito non deve apparire come errore
    }
}

static void task(void *arg)
{
    job_t j = (job_t)(intptr_t)arg;
    switch (j) {
    case JOB_CHECK:   do_check(); break;
    case JOB_INSTALL: do_install(); break;
    case JOB_AUTO:    do_auto(); break;   // gestisce "busy" da sé: può aspettare a lungo
    }
    if (j != JOB_AUTO) __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

static void run(job_t j)
{
    // controllo e installazione uno alla volta; quello automatico aspetta senza bloccarli
    if (j != JOB_AUTO && __atomic_exchange_n(&busy, true, __ATOMIC_ACQ_REL)) return;
    // TLS e HTTP vogliono stack; il task vive solo il tempo dell'operazione
    if (xTaskCreatePinnedToCore(task, "ota", 8192, (void *)(intptr_t)j, 3, NULL, 0) != pdPASS) {
        if (j != JOB_AUTO) __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
        fail("Memoria insufficiente");
    }
}

void ota_check(void) { run(JOB_CHECK); }
void ota_install(void) { run(JOB_INSTALL); }
void ota_auto_start(void) { if (g_set.ota_auto) run(JOB_AUTO); }
