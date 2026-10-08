// ble_mgr.c — avvio/arresto NimBLE su richiesta, advertising, scansione e collegamenti
// nei due sensi (vedi ble_mgr.h). Gli eventi di NimBLE arrivano nel suo task: qui si
// aggiornano tabelle protette da mutex che l'interfaccia legge quando vuole.
#include "ble_mgr.h"
#include "settings.h"
#include "radio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble";

void ble_store_config_init(void);   // NimBLE: archivio delle associazioni (NVS)

#define MAX_DEV      48
#define MAX_PROFILES 4

// tipi di report della scansione (HCI)
#define EVT_ADV_IND     0
#define EVT_DIR_IND     1
#define EVT_SCAN_RSP    4

static bool running;
static volatile bool synced, scanning;
static volatile uint32_t gen, conn_gen;
#define BUMP(x) __atomic_add_fetch(&(x), 1, __ATOMIC_RELAXED)
static int scan_users;
static uint8_t own_addr_type;
static char dev_name[24];
static ble_dev_t devs[MAX_DEV];
static int dev_n;
static SemaphoreHandle_t mtx;
static volatile uint8_t batt = 100;

static ble_conn_t conns[BLE_MAX_CONN];
static bool conn_used[BLE_MAX_CONN];

static const ble_profile_t *profiles[MAX_PROFILES];
static int n_profiles;

// collegamento in uscita (altri → Gadget)
static struct {
    ble_link_t state;
    uint8_t addr[6];
    char name[32];
    uint16_t appearance;
    char err[48];
} link;

static int64_t pair_until_us;       // finestra di associazione aperta fino a…
static volatile uint32_t passkey;
static esp_timer_handle_t pair_timer, kick_timer, guard_timer;
static volatile uint16_t guard_handle = BLE_HS_CONN_HANDLE_NONE;
#define GUARD_US (6 * 1000000)   // tempo concesso a uno sconosciuto per farsi riconoscere
static ble_store_write_fn *store_write_orig;

static int gap_cb(struct ble_gap_event *ev, void *arg);
static void adv_refresh(void);
static void add_dev(const struct ble_gap_disc_desc *d);

static void lock(void) { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(mtx); }

static bool pairing_open(void) { return pair_until_us && esp_timer_get_time() < pair_until_us; }

/* ---------------- server GATT di base ---------------- */

// Informazioni sul dispositivo (0x180A) e batteria (0x180F): li leggono telefoni e
// computer, e servono comunque a una futura tastiera (HID li richiede).
static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    const char *s = NULL;
    uint8_t b;
    switch ((int)(intptr_t)arg) {
    case 0: s = "Gadget"; break;
    case 1: s = "ESP32-S3 Gadget"; break;
    case 2: s = esp_app_get_description()->version; break;
    case 3: b = batt; return os_mbuf_append(ctxt->om, &b, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (!s) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s, strlen(s)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static uint16_t batt_handle;

static const struct ble_gatt_svc_def base_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = chr_access, .arg = (void *)0, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = BLE_UUID16_DECLARE(0x2A24), .access_cb = chr_access, .arg = (void *)1, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = BLE_UUID16_DECLARE(0x2A26), .access_cb = chr_access, .arg = (void *)2, .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = chr_access, .arg = (void *)3,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &batt_handle},
            {0},
        },
    },
    {0},
};

static void add_svcs(const struct ble_gatt_svc_def *s)
{
    if (ble_gatts_count_cfg(s) != 0 || ble_gatts_add_svcs(s) != 0) ESP_LOGE(TAG, "servizi GATT non registrati");
}

void ble_mgr_set_battery(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == batt) return;
    batt = pct;
    if (synced && batt_handle) ble_gatts_chr_updated(batt_handle);   // notifica chi è iscritto
}

bool ble_mgr_register_profile(const ble_profile_t *p)
{
    if (n_profiles >= MAX_PROFILES || running) return false;   // va fatto prima dell'avvio
    profiles[n_profiles++] = p;
    return true;
}

static uint16_t our_appearance(void)
{
    for (int i = 0; i < n_profiles; i++)
        if (profiles[i]->appearance) return profiles[i]->appearance;
    return 0;
}

/* ---------------- associazioni ---------------- */

// Le chiavi di una nuova associazione si salvano solo se l'ha voluta l'utente: finestra
// di associazione aperta, oppure dispositivo a cui il Gadget si è collegato da sé.
// Altrimenti uno sconosciuto potrebbe associarsi mentre il Gadget è collegabile.
static void kick_cb(void *arg)
{
    ble_addr_t a;
    lock();
    memcpy(&a, arg, sizeof(a));
    unlock();
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find_by_addr(&a, &d) == 0) ble_gap_terminate(d.conn_handle, BLE_ERR_AUTH_FAIL);
}

static ble_addr_t kick_addr;

static int store_write(int type, const union ble_store_value *val)
{
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        struct ble_gap_conn_desc d;
        bool ours = ble_gap_conn_find_by_addr(&val->sec.peer_addr, &d) == 0 && d.role == BLE_GAP_ROLE_MASTER;
        if (!pairing_open() && !ours) {
            ESP_LOGW(TAG, "associazione rifiutata: finestra chiusa");
            lock();
            kick_addr = val->sec.peer_addr;
            unlock();
            esp_timer_start_once(kick_timer, 1000);   // fuori dal contesto del gestore di sicurezza
            return BLE_HS_ESTORE_FAIL;
        }
    }
    return store_write_orig ? store_write_orig(type, val) : 0;
}

int ble_mgr_bonds(ble_bond_t *out, int max)
{
    if (!synced) return 0;
    ble_addr_t a[8];
    int n = 0;
    if (ble_store_util_bonded_peers(a, &n, 8) != 0) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { memcpy(out[i].addr, a[i].val, 6); out[i].type = a[i].type; }
    return n;
}

void ble_mgr_forget(const ble_bond_t *b)
{
    if (!synced) return;
    ble_addr_t a = {.type = b->type};
    memcpy(a.val, b->addr, 6);
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find_by_addr(&a, &d) == 0) ble_gap_terminate(d.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_util_delete_peer(&a);
    BUMP(conn_gen);
    adv_refresh();
}

void ble_mgr_forget_all(void)
{
    if (!synced) return;
    for (int i = 0; i < BLE_MAX_CONN; i++)
        if (conn_used[i]) ble_gap_terminate(conns[i].handle, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    BUMP(conn_gen);
    adv_refresh();
}

/* ---------------- advertising ---------------- */

static bool have_periph_conn(void)
{
    for (int i = 0; i < BLE_MAX_CONN; i++)
        if (conn_used[i] && !conns[i].central) return true;
    return false;
}

// Collegabile se la finestra di associazione è aperta o se c'è qualcuno di associato che
// deve potersi ricollegare; altrimenti, se "Visibile", solo annunci non collegabili.
static void adv_refresh(void)
{
    if (!synced) return;
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    if (!g_set.ble_on) return;
    ble_addr_t tmp[8];
    int nb = 0;
    ble_store_util_bonded_peers(tmp, &nb, 8);
    bool connectable = !have_periph_conn() && (pairing_open() || nb > 0);
    if (!connectable && !g_set.ble_visible && !pairing_open()) return;

    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)dev_name;
    f.name_len = strlen(dev_name);
    f.name_is_complete = 1;
    f.tx_pwr_lvl_is_present = 1;
    f.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    uint16_t app = our_appearance();
    if (app) { f.appearance = app; f.appearance_is_present = 1; }
    if (ble_gap_adv_set_fields(&f) != 0) return;
    struct ble_gap_adv_params p = {
        .conn_mode = connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON,
        // durante l'associazione si fa trovare in fretta; poi annunci più radi (batteria)
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = pairing_open() ? 0x30 : 0x140,
        .itvl_max = pairing_open() ? 0x60 : 0x200,
    };
    int r = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_cb, NULL);
    if (r) ESP_LOGW(TAG, "advertising non avviato (%d)", r);
}

static void pair_timer_cb(void *arg)
{
    pair_until_us = 0;
    passkey = 0;
    BUMP(conn_gen);
    adv_refresh();
}

void ble_mgr_pair_start(int seconds)
{
    if (!g_set.ble_on) { radio_only_ble(); g_set.ble_on = true; settings_save(); }
    pair_until_us = esp_timer_get_time() + (int64_t)seconds * 1000000;
    esp_timer_stop(pair_timer);
    esp_timer_start_once(pair_timer, (uint64_t)seconds * 1000000);
    ble_mgr_apply();
    adv_refresh();
}

void ble_mgr_pair_stop(void)
{
    if (!pair_until_us) return;
    esp_timer_stop(pair_timer);
    pair_timer_cb(NULL);
}

int ble_mgr_pair_left(void)
{
    if (!pairing_open()) return 0;
    return (int)((pair_until_us - esp_timer_get_time() + 999999) / 1000000);
}

uint32_t ble_mgr_passkey(void) { return passkey; }

/* ---------------- tabella dei collegamenti ---------------- */

static ble_conn_t *conn_get(uint16_t h)
{
    for (int i = 0; i < BLE_MAX_CONN; i++)
        if (conn_used[i] && conns[i].handle == h) return &conns[i];
    return NULL;
}

static void notify_profiles(uint16_t h, bool up)
{
    ble_conn_t c;
    lock();
    ble_conn_t *p = conn_get(h);
    if (p) c = *p;
    unlock();
    if (!p) return;
    for (int i = 0; i < n_profiles; i++)
        if (profiles[i]->on_conn) profiles[i]->on_conn(&c, up);
}

uint32_t ble_mgr_conn_gen(void) { return conn_gen; }

int ble_mgr_conns(ble_conn_t *out, int max)
{
    if (!mtx) return 0;
    int n = 0;
    lock();
    for (int i = 0; i < BLE_MAX_CONN && n < max; i++)
        if (conn_used[i]) out[n++] = conns[i];
    unlock();
    return n;
}

/* ---------------- client GATT: nome e servizi dell'altro ---------------- */

static int on_svc(uint16_t h, const struct ble_gatt_error *e, const struct ble_gatt_svc *s, void *arg)
{
    lock();
    ble_conn_t *c = conn_get(h);
    bool done = !e || e->status != 0;
    if (c && !done && c->n_svcs < BLE_MAX_SVCS) {
        uint16_t u = s->uuid.u.type == BLE_UUID_TYPE_16 ? ble_uuid_u16(&s->uuid.u) : 0xFFFF;
        c->svcs[c->n_svcs++] = u;
    }
    bool hid = false;
    if (c && done) {
        c->disc = e && e->status == BLE_HS_EDONE ? BLE_DISC_DONE : BLE_DISC_FAILED;
        for (int i = 0; i < c->n_svcs; i++) if (c->svcs[i] == 0x1812) hid = true;
    }
    bool central = c && c->central, enc = c && c->encrypted;
    unlock();
    if (done) {
        BUMP(conn_gen);
        // tastiere e telecomandi (HID) vogliono un collegamento cifrato e associato
        if (hid && central && !enc) ble_gap_security_initiate(h);
    }
    return 0;
}

static void start_discovery(uint16_t h)
{
    lock();
    ble_conn_t *c = conn_get(h);
    if (c) { c->disc = BLE_DISC_RUNNING; c->n_svcs = 0; }
    unlock();
    if (ble_gattc_disc_all_svcs(h, on_svc, NULL) != 0) {
        lock();
        if ((c = conn_get(h))) c->disc = BLE_DISC_FAILED;
        unlock();
        BUMP(conn_gen);
    }
}

// legge il nome del dispositivo (caratteristica 0x2A00), poi cerca i servizi
static int on_name(uint16_t h, const struct ble_gatt_error *e, struct ble_gatt_attr *a, void *arg)
{
    if (e && e->status == 0 && a && a->om) {
        char nm[32] = "";
        int len = OS_MBUF_PKTLEN(a->om);
        if (len > (int)sizeof(nm) - 1) len = sizeof(nm) - 1;
        if (os_mbuf_copydata(a->om, 0, len, nm) == 0) {
            nm[len] = 0;
            lock();
            ble_conn_t *c = conn_get(h);
            if (c && nm[0]) snprintf(c->name, sizeof(c->name), "%s", nm);
            unlock();
            BUMP(conn_gen);
        }
        return 0;   // arriva poi un'ultima chiamata con lo stato di fine
    }
    lock();
    ble_conn_t *c = conn_get(h);
    bool central = c && c->central;
    unlock();
    if (central) start_discovery(h);
    return 0;
}

bool ble_mgr_conn_trusted(uint16_t h)
{
    if (!mtx) return false;
    lock();
    ble_conn_t *c = conn_get(h);
    bool t = c && c->trusted;
    unlock();
    return t;
}

static void guard_cb(void *arg)
{
    uint16_t h = guard_handle;
    if (h != BLE_HS_CONN_HANDLE_NONE && synced && !ble_mgr_conn_trusted(h)) {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(h, &d) == 0) {
            ESP_LOGW(TAG, "dispositivo non associato: scollegato");
            ble_gap_terminate(h, BLE_ERR_AUTH_FAIL);
        }
    }
}

/* ---------------- eventi GAP ---------------- */

static void on_connect(uint16_t h)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(h, &d) != 0) return;
    bool central = d.role == BLE_GAP_ROLE_MASTER;
    lock();
    int slot = -1;
    for (int i = 0; i < BLE_MAX_CONN; i++) if (!conn_used[i]) { slot = i; break; }
    if (slot >= 0) {
        ble_conn_t *c = &conns[slot];
        memset(c, 0, sizeof(*c));
        conn_used[slot] = true;
        c->handle = h;
        c->central = central;
        memcpy(c->addr, d.peer_id_addr.val, 6);
        c->encrypted = d.sec_state.encrypted;
        c->bonded = d.sec_state.bonded;
        c->trusted = central || pairing_open() || (c->encrypted && c->bonded);
        if (central && !memcmp(link.addr, d.peer_id_addr.val, 6)) {
            snprintf(c->name, sizeof(c->name), "%s", link.name);
            c->appearance = link.appearance;
            link.state = BLE_LINK_OK;
        }
    }
    unlock();
    if (slot < 0) { ble_gap_terminate(h, BLE_ERR_CONN_LIMIT); return; }
    ESP_LOGI(TAG, "collegato (%s)", central ? "centrale" : "periferica");
    // Fuori dalla finestra di associazione il Gadget è collegabile solo perché un
    // dispositivo associato possa tornare: chi non si fa riconoscere (cifratura con
    // le chiavi salvate) entro pochi secondi viene scollegato.
    if (!ble_mgr_conn_trusted(h)) {
        guard_handle = h;
        esp_timer_stop(guard_timer);
        esp_timer_start_once(guard_timer, GUARD_US);
        // non tutti i telefoni cifrano da soli al ricollegamento: lo chiede il Gadget, così
        // chi è associato si fa riconoscere con le sue chiavi (uno sconosciuto non può
        // salvarne di nuove a finestra chiusa e viene scollegato allo scadere)
        ble_gap_security_initiate(h);
    }
    BUMP(conn_gen);
    if (ble_gattc_read_by_uuid(h, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A00), on_name, NULL) != 0 && central)
        start_discovery(h);
    notify_profiles(h, true);
}

static void on_disconnect(uint16_t h, int reason)
{
    notify_profiles(h, false);
    lock();
    for (int i = 0; i < BLE_MAX_CONN; i++)
        if (conn_used[i] && conns[i].handle == h) conn_used[i] = false;
    unlock();
    passkey = 0;
    ESP_LOGI(TAG, "scollegato (motivo 0x%x)", reason);
    BUMP(conn_gen);
}

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC: add_dev(&ev->disc); break;
    case BLE_GAP_EVENT_DISC_COMPLETE: scanning = false; BUMP(gen); break;

    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            on_connect(ev->connect.conn_handle);
        } else {
            lock();
            if (link.state == BLE_LINK_CONNECTING) {
                link.state = BLE_LINK_FAILED;
                snprintf(link.err, sizeof(link.err), "Non risponde (%d)", ev->connect.status);
            }
            unlock();
            BUMP(conn_gen);
        }
        adv_refresh();   // un collegamento in entrata ferma gli annunci: decide di nuovo
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnect(ev->disconnect.conn.conn_handle, ev->disconnect.reason);
        adv_refresh();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        adv_refresh();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        bool found = ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0;
        if (found) {
            lock();
            ble_conn_t *c = conn_get(ev->enc_change.conn_handle);
            if (c) {
                c->encrypted = d.sec_state.encrypted;
                c->bonded = d.sec_state.bonded;
                if (c->encrypted && c->bonded) c->trusted = true;
                memcpy(c->addr, d.peer_id_addr.val, 6);   // dopo l'associazione: indirizzo vero
            }
            unlock();
        }
        passkey = 0;
        // associazione riuscita dalla finestra: la si chiude, il lavoro è fatto
        if (found && ev->enc_change.status == 0 && pairing_open() && d.role == BLE_GAP_ROLE_SLAVE && d.sec_state.bonded) {
            pair_until_us = 0;
            esp_timer_stop(pair_timer);
            adv_refresh();   // torna agli annunci lenti (o a nessuno): niente più ogni 30 ms
        }
        BUMP(conn_gen);
        break;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // l'altro ha perso l'associazione e vuole rifarla: si accetta solo se l'utente
        // l'ha chiesto (finestra aperta o collegamento voluto dal Gadget)
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) != 0) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        if (!pairing_open() && d.role != BLE_GAP_ROLE_MASTER) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io io = {.action = ev->passkey.params.action};
        if (io.action == BLE_SM_IOACT_DISP) {
            // il codice compare sul Gadget e si digita sul telefono o sul computer
            io.passkey = esp_random() % 1000000;
            passkey = io.passkey ? io.passkey : 1;
            io.passkey = passkey;
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
            BUMP(conn_gen);
        } else if (io.action == BLE_SM_IOACT_NUMCMP) {
            io.numcmp_accept = 1;
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
        }
        break;
    }
    default: break;
    }
    return 0;
}

/* ---------------- avvio dello stack ---------------- */

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    synced = true;
    adv_refresh();
}

static void on_reset(int reason) { synced = false; }

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Lo stack acceso sopravvive ai riavvii per crash (memoria RTC): se la scheda è ripartita
// per un errore mentre il Bluetooth era acceso, all'avvio dopo non lo si riaccende, o un
// Bluetooth che manda in crash la scheda la farebbe riavviare in continuazione
#define BLE_LIVE_MAGIC 0xB1E0A11Eu
static RTC_NOINIT_ATTR uint32_t ble_live;

bool ble_mgr_boot_guard(void)
{
    uint32_t was = ble_live;
    ble_live = 0;
    esp_reset_reason_t r = esp_reset_reason();
    bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
    if (!crash || was != BLE_LIVE_MAGIC || !g_set.ble_on) return false;
    ESP_LOGW(TAG, "riavvio per errore (%d) col Bluetooth acceso: resta spento", r);
    g_set.ble_on = false;
    settings_save();
    return true;
}

static void stack_start(void)
{
    if (running) return;
    ESP_LOGI(TAG, "avvio dello stack · RAM interna libera %u (blocco %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ble_live = BLE_LIVE_MAGIC;
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init fallito"); ble_live = 0; return; }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // associazioni: il Gadget mostra un codice (se l'altro ha una tastiera), altrimenti
    // "funziona e basta"; si salvano le chiavi per ricollegarsi in automatico
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(dev_name);
    uint16_t app = our_appearance();
    if (app) ble_svc_gap_device_appearance_set(app);
    add_svcs(base_svcs);
    for (int i = 0; i < n_profiles; i++)
        if (profiles[i]->svcs) add_svcs(profiles[i]->svcs);
    ble_store_config_init();
    if (ble_hs_cfg.store_write_cb != store_write) store_write_orig = ble_hs_cfg.store_write_cb;
    ble_hs_cfg.store_write_cb = store_write;
    nimble_port_freertos_init(host_task);
    running = true;
}

static void stack_stop(void)
{
    if (!running) return;
    if (scanning) ble_gap_disc_cancel();
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    for (int i = 0; i < BLE_MAX_CONN; i++)
        if (conn_used[i]) {
            // con lo stack fermo l'evento di scollegamento non arriverebbe: avvisa ora i profili
            notify_profiles(conns[i].handle, false);
            ble_gap_terminate(conns[i].handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    esp_timer_stop(guard_timer);
    scanning = false;
    if (nimble_port_stop() == 0) nimble_port_deinit();
    running = false;
    ble_live = 0;
    synced = false;
    lock();
    memset(conn_used, 0, sizeof(conn_used));
    if (link.state == BLE_LINK_CONNECTING) link.state = BLE_LINK_IDLE;
    unlock();
    pair_until_us = 0;
    passkey = 0;
    BUMP(conn_gen);
}

// in pausa (es. durante un aggiornamento): radio tutta al Wi-Fi, impostazioni intatte
static bool suspended;

void ble_mgr_suspend(bool on)
{
    if (suspended == on) return;
    suspended = on;
    ble_mgr_apply();
}

static void update(void)
{
    bool need = (g_set.ble_on || scan_users > 0) && !suspended;
    if (!need) { stack_stop(); return; }
    stack_start();
    adv_refresh();   // se non è ancora sincronizzato, ci pensa on_sync
}

void ble_mgr_apply(void)
{
    if (!mtx) {
        mtx = xSemaphoreCreateMutex();
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(dev_name, sizeof(dev_name), "Gadget-%02X%02X", mac[4], mac[5]);
        const esp_timer_create_args_t pt = {.callback = pair_timer_cb, .name = "ble_pair"};
        esp_timer_create(&pt, &pair_timer);
        const esp_timer_create_args_t kt = {.callback = kick_cb, .arg = &kick_addr, .name = "ble_kick"};
        esp_timer_create(&kt, &kick_timer);
        const esp_timer_create_args_t gt = {.callback = guard_cb, .name = "ble_guard"};
        esp_timer_create(&gt, &guard_timer);
    }
    update();
}

bool ble_mgr_on(void) { return g_set.ble_on; }
bool ble_mgr_ready(void) { return running && synced; }
const char *ble_mgr_name(void) { return dev_name; }

void ble_mgr_addr(char *buf, int n)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(buf, n, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ---------------- collegamento in uscita ---------------- */

bool ble_mgr_connect(const ble_dev_t *dv)
{
    if (!g_set.ble_on) { radio_only_ble(); g_set.ble_on = true; settings_save(); ble_mgr_apply(); }
    lock();
    memcpy(link.addr, dv->addr, 6);
    snprintf(link.name, sizeof(link.name), "%s", dv->name);
    link.appearance = dv->appearance;
    link.err[0] = 0;
    unlock();
    if (!ble_mgr_ready()) {
        lock();
        link.state = BLE_LINK_FAILED;
        snprintf(link.err, sizeof(link.err), "Bluetooth non pronto");
        unlock();
        return false;
    }
    if (scanning) { ble_gap_disc_cancel(); scanning = false; BUMP(gen); }   // la radio fa una cosa per volta
    ble_addr_t a = {.type = dv->addr_type};
    memcpy(a.val, dv->addr, 6);
    lock();
    link.state = BLE_LINK_CONNECTING;
    unlock();
    int r = ble_gap_connect(own_addr_type, &a, 10000, NULL, gap_cb, NULL);
    if (r == BLE_HS_EALREADY) {   // un altro collegamento è già in corso
        lock();
        link.state = BLE_LINK_FAILED;
        snprintf(link.err, sizeof(link.err), "Un altro collegamento è in corso");
        unlock();
        BUMP(conn_gen);
        return false;
    }
    if (r == BLE_HS_EDONE) {   // già collegati
        lock();
        link.state = BLE_LINK_OK;
        unlock();
        return true;
    }
    if (r != 0) {
        lock();
        link.state = BLE_LINK_FAILED;
        snprintf(link.err, sizeof(link.err), r == BLE_HS_ENOMEM ? "Troppi collegamenti" : "Errore %d", r);
        unlock();
        BUMP(conn_gen);
        return false;
    }
    BUMP(conn_gen);
    return true;
}

ble_link_t ble_mgr_link_state(const uint8_t addr[6])
{
    if (!mtx) return BLE_LINK_IDLE;
    lock();
    ble_link_t s = memcmp(link.addr, addr, 6) ? BLE_LINK_IDLE : link.state;
    // il collegamento può essere caduto dopo: allora non è più "ok"
    if (s == BLE_LINK_OK) {
        bool alive = false;
        for (int i = 0; i < BLE_MAX_CONN; i++)
            if (conn_used[i] && conns[i].central && !memcmp(conns[i].addr, addr, 6)) alive = true;
        if (!alive) s = BLE_LINK_IDLE;
    }
    unlock();
    return s;
}

const char *ble_mgr_link_error(void) { return link.err; }

void ble_mgr_disconnect(uint16_t h)
{
    if (synced) ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
}

/* ---------------- scansione ---------------- */

static void add_dev(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    char name[32] = "";
    uint16_t app = 0, svc = 0;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0) {
        if (f.name && f.name_len) {
            int l = f.name_len < 31 ? f.name_len : 31;
            memcpy(name, f.name, l);
            name[l] = 0;
        }
        if (f.appearance_is_present) app = f.appearance;
        if (f.num_uuids16 && f.uuids16) svc = f.uuids16[0].value;
    }
    bool conn = d->event_type == EVT_ADV_IND || d->event_type == EVT_DIR_IND;
    lock();
    int i;
    for (i = 0; i < dev_n; i++)
        if (!memcmp(devs[i].addr, d->addr.val, 6)) break;
    if (i == dev_n) {
        if (dev_n >= MAX_DEV) { unlock(); return; }
        dev_n++;
        memset(&devs[i], 0, sizeof(devs[i]));
        memcpy(devs[i].addr, d->addr.val, 6);
        devs[i].addr_type = d->addr.type;
    }
    devs[i].rssi = d->rssi;
    if (conn) devs[i].connectable = true;
    if (name[0]) strlcpy(devs[i].name, name, sizeof(devs[i].name));
    if (app) devs[i].appearance = app;
    if (svc && d->event_type != EVT_SCAN_RSP) devs[i].svc16 = svc;
    else if (svc && !devs[i].svc16) devs[i].svc16 = svc;
    unlock();
}

void ble_mgr_scan_acquire(void) { scan_users++; ble_mgr_apply(); }
void ble_mgr_scan_release(void) { if (scan_users > 0) scan_users--; ble_mgr_apply(); }

bool ble_mgr_scan_start(int ms)
{
    if (!ble_mgr_ready() || scanning) return false;
    lock();
    bool busy = link.state == BLE_LINK_CONNECTING;
    if (!busy) dev_n = 0;
    unlock();
    if (busy) return false;   // un collegamento in corso: la scansione aspetta
    struct ble_gap_disc_params p = {.passive = 0, .filter_duplicates = 0, .itvl = 0x50, .window = 0x30};
    if (ble_gap_disc(own_addr_type, ms, &p, gap_cb, NULL) != 0) return false;
    scanning = true;
    return true;
}

bool ble_mgr_scan_busy(void) { return scanning; }
uint32_t ble_mgr_scan_gen(void) { return gen; }

static int by_rssi(const void *a, const void *b)
{
    return ((const ble_dev_t *)b)->rssi - ((const ble_dev_t *)a)->rssi;
}

int ble_mgr_scan_results(ble_dev_t *out, int max)
{
    if (!mtx) return 0;
    lock();
    int n = dev_n < max ? dev_n : max;
    memcpy(out, devs, n * sizeof(ble_dev_t));
    unlock();
    qsort(out, n, sizeof(ble_dev_t), by_rssi);
    return n;
}

/* ---------------- nomi ---------------- */

const char *ble_mgr_svc_name(uint16_t u)
{
    switch (u) {
    case 0x1800: return "Accesso generico";
    case 0x1801: return "Attributi generici";
    case 0x180A: return "Info dispositivo";
    case 0x180F: return "Batteria";
    case 0x180D: return "Battito cardiaco";
    case 0x1812: return "Tastiera / HID";
    case 0x1816: return "Velocità e cadenza";
    case 0x1818: return "Potenza ciclismo";
    case 0x1819: return "Posizione";
    case 0x181A: return "Sensori ambiente";
    case 0x181C: return "Dati utente";
    case 0x181D: return "Bilancia";
    case 0x1805: return "Ora corrente";
    case 0x1802: return "Allarme";
    case 0x1803: return "Perdita collegamento";
    case 0x1804: return "Potenza di trasmissione";
    case 0x1809: return "Termometro";
    case 0x1810: return "Pressione";
    case 0x1822: return "Ossimetro";
    case 0x1826: return "Attrezzi fitness";
    case 0x184E: case 0x184F: case 0x1850: case 0x1853: return "Audio LE";
    case 0x1844: case 0x1845: return "Volume";
    case 0x1848: return "Controllo media";
    case 0xFE2C: return "Fast Pair";
    case 0xFD6F: return "Notifiche di esposizione";
    case 0xFFFF: return "Servizio proprietario";
    default: return NULL;
    }
}

const char *ble_mgr_appearance_name(uint16_t a)
{
    switch (a) {
    case 0x03C1: return "Tastiera";
    case 0x03C2: return "Mouse";
    case 0x03C3: return "Joystick";
    case 0x03C4: return "Gamepad";
    case 0x03C5: return "Tavoletta grafica";
    case 0x0180: return "Telecomando";
    }
    switch (a >> 6) {   // categoria
    case 1:  return "Telefono";
    case 2:  return "Computer";
    case 3:  return "Orologio";
    case 5:  return "Display";
    case 6:  return "Telecomando";
    case 13: return "Fascia cardio";
    case 15: return "Dispositivo di input";
    case 33: return "Cassa audio";
    case 34: return "Sorgente audio";
    case 37: return "Cuffie o auricolari";
    case 49: return "Bilancia";
    default: return NULL;
    }
}

bool ble_mgr_is_audio(uint16_t a)
{
    int cat = a >> 6;
    return cat == 33 || cat == 37 || cat == 34;
}
