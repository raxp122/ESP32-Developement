// ble_mgr.c — avvio/arresto NimBLE su richiesta, advertising e scansione
#include "ble_mgr.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble";

#define MAX_DEV 48

static bool running;
static volatile bool synced, scanning;
static volatile uint32_t gen;
static int scan_users;
static uint8_t own_addr_type;
static char dev_name[24];
static ble_dev_t devs[MAX_DEV];
static int dev_n;
static SemaphoreHandle_t mtx;

static int gap_cb(struct ble_gap_event *ev, void *arg);

static void start_adv(void)
{
    if (!synced || !g_set.ble_visible || ble_gap_adv_active()) return;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)dev_name;
    f.name_len = strlen(dev_name);
    f.name_is_complete = 1;
    f.tx_pwr_lvl_is_present = 1;
    f.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    if (ble_gap_adv_set_fields(&f) != 0) return;
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_NON, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_cb, NULL);
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    synced = true;
    start_adv();
}

static void on_reset(int reason) { synced = false; }

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void stack_start(void)
{
    if (running) return;
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init fallito"); return; }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(dev_name);
    nimble_port_freertos_init(host_task);
    running = true;
}

static void stack_stop(void)
{
    if (!running) return;
    if (scanning) ble_gap_disc_cancel();
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    scanning = false;
    if (nimble_port_stop() == 0) nimble_port_deinit();
    running = false;
    synced = false;
}

static void update(void)
{
    bool need = g_set.ble_on || scan_users > 0;
    if (!need) { stack_stop(); return; }
    stack_start();
    if (!synced) return; // l'advertising parte in on_sync
    if (g_set.ble_on && g_set.ble_visible) start_adv();
    else if (ble_gap_adv_active()) ble_gap_adv_stop();
}

void ble_mgr_apply(void)
{
    if (!mtx) {
        mtx = xSemaphoreCreateMutex();
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(dev_name, sizeof(dev_name), "Gadget-%02X%02X", mac[4], mac[5]);
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

/* ---------------- scansione ---------------- */

static void add_dev(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    char name[32] = "";
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0 && f.name && f.name_len) {
        int l = f.name_len < 31 ? f.name_len : 31;
        memcpy(name, f.name, l);
        name[l] = 0;
    }
    xSemaphoreTake(mtx, portMAX_DELAY);
    int i;
    for (i = 0; i < dev_n; i++)
        if (!memcmp(devs[i].addr, d->addr.val, 6)) break;
    if (i == dev_n) {
        if (dev_n >= MAX_DEV) { xSemaphoreGive(mtx); return; }
        dev_n++;
        memset(&devs[i], 0, sizeof(devs[i]));
        memcpy(devs[i].addr, d->addr.val, 6);
        devs[i].addr_type = d->addr.type;
    }
    devs[i].rssi = d->rssi;
    if (name[0]) strlcpy(devs[i].name, name, sizeof(devs[i].name));
    xSemaphoreGive(mtx);
}

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC: add_dev(&ev->disc); break;
    case BLE_GAP_EVENT_DISC_COMPLETE: scanning = false; gen++; break;
    default: break;
    }
    return 0;
}

void ble_mgr_scan_acquire(void) { scan_users++; ble_mgr_apply(); }
void ble_mgr_scan_release(void) { if (scan_users > 0) scan_users--; ble_mgr_apply(); }

bool ble_mgr_scan_start(int ms)
{
    if (!ble_mgr_ready() || scanning) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    dev_n = 0;
    xSemaphoreGive(mtx);
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
    xSemaphoreTake(mtx, portMAX_DELAY);
    int n = dev_n < max ? dev_n : max;
    memcpy(out, devs, n * sizeof(ble_dev_t));
    xSemaphoreGive(mtx);
    qsort(out, n, sizeof(ble_dev_t), by_rssi);
    return n;
}
