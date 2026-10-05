// pwn.c — motore di cattura passiva. Solo ascolto, nessuna trasmissione.
#include "pwn.h"
#include "wifi_mgr.h"
#include "settings.h"
#include "sd.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <sys/stat.h>

static const char *TAG = "pwn";

/* ---------------- stato ---------------- */

static pwn_net_t nets[PWN_MAX_NETS];
static int n_nets;
static SemaphoreHandle_t mtx;
static pwn_stats_t stats;
static volatile bool running;
static TaskHandle_t task_h;
static volatile int cur_ch = 1;
static volatile uint32_t pkts_total;
static char last_event[48];
static int64_t near_window[14];   // ultimo "last_seen" per canale, per stimare le reti vicine

// pcap: la scrittura su SD NON va fatta nel callback Wi-Fi (bloccherebbe la radio).
// Il callback copia il pacchetto in una coda, il task lo scrive.
static FILE *pcap;
typedef struct { uint16_t len; uint32_t us; uint8_t data[512]; } pcap_item_t;
static QueueHandle_t pcap_q;

static uint32_t now_s(void)
{
    time_t t = time(NULL);
    if (t > 1700000000) return (uint32_t)t;
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* ---------------- livelli ---------------- */

// XP cresce scoprendo reti nuove e catturando handshake. La curva è dolce.
static uint32_t xp_for_level(int lv) { return (uint32_t)(50 * lv * lv + 50 * lv); }

static void add_xp(uint32_t amount)
{
    stats.xp += amount;
    while (stats.level < 999 && stats.xp >= xp_for_level(stats.level + 1)) stats.level++;
}

/* ---------------- pcap (formato classico, apribile in Wireshark) ---------------- */

static void pcap_open(void)
{
    if (!sd_ok()) return;
    mkdir(SD_MOUNT "/pwn", 0777);
    char path[64];
    struct tm tm;
    time_t t = time(NULL);
    localtime_r(&t, &tm);
    if (tm.tm_year >= 124)
        snprintf(path, sizeof(path), SD_MOUNT "/pwn/%04d%02d%02d-%02d%02d%02d.pcap",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    else
        snprintf(path, sizeof(path), SD_MOUNT "/pwn/sessione-%lu.pcap", (unsigned long)(esp_timer_get_time() / 1000000));
    pcap = fopen(path, "wb");
    if (!pcap) { ESP_LOGW(TAG, "pcap non apribile"); return; }
    // intestazione globale pcap, link-type 105 = IEEE 802.11
    uint32_t hdr[6] = {0xa1b2c3d4, 0, 0, 0, 0x40000, 105};
    uint16_t *ver = (uint16_t *)&hdr[1];
    ver[0] = 2; ver[1] = 4;
    fwrite(hdr, sizeof(hdr), 1, pcap);
    fflush(pcap);
    ESP_LOGI(TAG, "pcap: %s", path);
}

// chiamata dal callback Wi-Fi: solo copia in coda, nessuna I/O
static void pcap_enqueue(const uint8_t *data, int len, uint32_t us)
{
    if (!pcap_q || len > 512) return;
    pcap_item_t it;
    it.len = len; it.us = us;
    memcpy(it.data, data, len);
    xQueueSend(pcap_q, &it, 0);   // se piena, scarta: meglio perdere un frame che bloccare la radio
}

// chiamata dal task pwn
static void pcap_flush_queue(void)
{
    if (!pcap || !pcap_q) return;
    pcap_item_t it;
    while (xQueueReceive(pcap_q, &it, 0) == pdTRUE) {
        uint32_t rec[4] = {it.us / 1000000, it.us % 1000000, it.len, it.len};
        fwrite(rec, sizeof(rec), 1, pcap);
        fwrite(it.data, 1, it.len, pcap);
    }
}

static void pcap_close(void)
{
    if (!pcap) return;
    pcap_flush_queue();
    fclose(pcap);
    pcap = NULL;
}

/* ---------------- Pokédex delle reti ---------------- */

static int find_net(const uint8_t *bssid)
{
    for (int i = 0; i < n_nets; i++)
        if (!memcmp(nets[i].bssid, bssid, 6)) return i;
    return -1;
}

static void auth_from_beacon(const uint8_t *ies, int len, pwn_net_t *net)
{
    // euristica: presenza di RSN (0x30) o WPA vendor → WPA; Privacy bit via tag non parsato qui
    net->auth = 0;
    for (int i = 0; i + 2 <= len;) {
        uint8_t id = ies[i], l = ies[i + 1];
        if (i + 2 + l > len) break;
        if (id == 48) { net->auth = 2; return; }                 // RSN
        if (id == 221 && l >= 4 && ies[i + 2] == 0x00 && ies[i + 3] == 0x50 &&
            ies[i + 4] == 0xf2 && ies[i + 5] == 0x01) net->auth = 2; // WPA
        i += 2 + l;
    }
}

static void on_beacon(const uint8_t *p, int len, int8_t rssi, uint8_t channel)
{
    // 802.11 mgmt header 24 byte, poi fixed params 12 byte, poi IE
    if (len < 38) return;
    const uint8_t *bssid = p + 16;
    const uint8_t *ies = p + 36;
    int ie_len = len - 36;
    char ssid[33] = "";
    uint8_t ch = channel;
    for (int i = 0; i + 2 <= ie_len;) {
        uint8_t id = ies[i], l = ies[i + 1];
        if (i + 2 + l > ie_len) break;
        if (id == 0 && l <= 32) { memcpy(ssid, ies + i + 2, l); ssid[l] = 0; }
        if (id == 3 && l >= 1) ch = ies[i + 2];
        i += 2 + l;
    }

    xSemaphoreTake(mtx, portMAX_DELAY);
    int idx = find_net(bssid);
    if (idx < 0) {
        if (n_nets < PWN_MAX_NETS) {
            idx = n_nets++;
            memset(&nets[idx], 0, sizeof(pwn_net_t));
            memcpy(nets[idx].bssid, bssid, 6);
            nets[idx].first_seen = now_s();
            nets[idx].rssi = -127;
            stats.nets_total++;
            add_xp(10);
            snprintf(last_event, sizeof(last_event), "nuova rete: %s", ssid[0] ? ssid : "(nascosta)");
            auth_from_beacon(ies, ie_len, &nets[idx]);
        } else {
            xSemaphoreGive(mtx);
            return;
        }
    }
    pwn_net_t *nw = &nets[idx];
    if (ssid[0]) strlcpy(nw->ssid, ssid, sizeof(nw->ssid));
    if (rssi > nw->rssi) nw->rssi = rssi;
    nw->channel = ch;
    nw->last_seen = now_s();
    if (ch >= 1 && ch <= 13) near_window[ch] = esp_timer_get_time();
    xSemaphoreGive(mtx);
}

/* ---------------- handshake EAPOL ---------------- */

static void on_eapol(const uint8_t *p, int len, const uint8_t *bssid)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    int idx = find_net(bssid);
    if (idx >= 0 && !nets[idx].handshake) {
        nets[idx].handshake = true;
        stats.handshakes++;
        add_xp(100);
        snprintf(last_event, sizeof(last_event), "handshake! %s", nets[idx].ssid[0] ? nets[idx].ssid : "rete");
    } else if (idx >= 0) {
        snprintf(last_event, sizeof(last_event), "EAPOL %s", nets[idx].ssid[0] ? nets[idx].ssid : "rete");
    }
    xSemaphoreGive(mtx);
}

/* ---------------- callback promiscuo ---------------- */

static void sniff_cb(void *buf, int type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24 || len > 2400) return;
    const uint8_t *p = pkt->payload;
    pkts_total++;

    uint8_t fc0 = p[0];
    uint8_t ftype = (fc0 >> 2) & 3;
    uint8_t subtype = (fc0 >> 4) & 0xF;

    if (ftype == 0 && (subtype == 8 || subtype == 5)) {      // beacon o probe response
        on_beacon(p, len, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
    } else if (ftype == 2) {                                  // data
        // EAPOL: LLC/SNAP (AA AA 03 00 00 00) + ethertype 0x888E. L'offset dipende da QoS/protezione.
        for (int off = 24; off <= 34 && off + 8 <= len; off += 2) {
            if (p[off] == 0xAA && p[off + 1] == 0xAA && p[off + 2] == 0x03 &&
                p[off + 6] == 0x88 && p[off + 7] == 0x8E) {
                // BSSID: con ToDS/FromDS varia; uso addr1 o addr3 come euristica
                const uint8_t *bssid = (p[1] & 0x01) ? p + 4 : p + 16;
                on_eapol(p, len, bssid);
                if (g_set.pwn_pcap) pcap_enqueue(p, len, pkt->rx_ctrl.timestamp);   // salva solo gli handshake
                return;
            }
        }
    }
}

/* ---------------- task: hopping tra i canali ---------------- */

static void task(void *arg)
{
    const uint8_t channels[] = {1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10, 12, 13};
    int ci = 0;
    int64_t t0 = esp_timer_get_time(), last_save = t0;
    while (running) {
        cur_ch = channels[ci];
        wifi_mgr_sniff_channel(cur_ch);
        ci = (ci + 1) % (int)sizeof(channels);
        vTaskDelay(pdMS_TO_TICKS(280));    // ~280 ms per canale
        stats.pkts = pkts_total;
        int64_t now = esp_timer_get_time();
        stats.uptime_s += (uint32_t)((now - t0) / 1000000);
        t0 = now;
        pcap_flush_queue();
        if (now - last_save > 60000000) { pwn_save(); last_save = now; if (pcap) fflush(pcap); }
    }
    task_h = NULL;
    vTaskDelete(NULL);
}

/* ---------------- stato persistente ---------------- */

static void load(void)
{
    nvs_handle_t h;
    memset(&stats, 0, sizeof(stats));
    strlcpy(stats.name, "Gadget", sizeof(stats.name));
    if (g_set_pwn_name()[0]) strlcpy(stats.name, g_set_pwn_name(), sizeof(stats.name));
    if (nvs_open("pwn", NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u32(h, "xp", &stats.xp);
    uint16_t lv = 0; nvs_get_u16(h, "lv", &lv); stats.level = lv;
    nvs_get_u32(h, "nets", &stats.nets_total);
    nvs_get_u32(h, "hs", &stats.handshakes);
    nvs_get_u32(h, "up", &stats.uptime_s);
    nvs_close(h);
    if (stats.level > 999) stats.level = 0;   // memoria sporca / versione vecchia
}

void pwn_save(void)
{
    nvs_handle_t h;
    if (nvs_open("pwn", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "xp", stats.xp);
    nvs_set_u16(h, "lv", stats.level);
    nvs_set_u32(h, "nets", stats.nets_total);
    nvs_set_u32(h, "hs", stats.handshakes);
    nvs_set_u32(h, "up", stats.uptime_s);
    nvs_commit(h);
    nvs_close(h);
}

void pwn_reset_pokedex(void)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    n_nets = 0;
    stats.xp = 0; stats.level = 0; stats.nets_total = 0; stats.handshakes = 0; stats.uptime_s = 0;
    xSemaphoreGive(mtx);
    pwn_save();
}

/* ---------------- API ---------------- */

void pwn_start(void)
{
    if (running) return;
    if (!mtx) { mtx = xSemaphoreCreateMutex(); pcap_q = xQueueCreate(16, sizeof(pcap_item_t)); }
    load();
    snprintf(last_event, sizeof(last_event), "in ascolto…");
    pkts_total = 0;
    if (!wifi_mgr_sniff_start(sniff_cb)) {
        snprintf(last_event, sizeof(last_event), "radio occupata");
        return;
    }
    if (g_set.pwn_pcap) pcap_open();
    running = true;
    xTaskCreatePinnedToCore(task, "pwn", 4096, NULL, 4, &task_h, 0);
}

void pwn_stop(void)
{
    if (!running) return;
    running = false;
    for (int i = 0; i < 40 && task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    wifi_mgr_sniff_stop();
    pcap_close();
    pwn_save();
}

bool pwn_running(void) { return running; }

void pwn_get_stats(pwn_stats_t *out)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    *out = stats;
    xSemaphoreGive(mtx);
}

int pwn_net_count(void) { return n_nets; }

bool pwn_net_get(int i, pwn_net_t *out)
{
    if (i < 0 || i >= n_nets) return false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    *out = nets[i];
    xSemaphoreGive(mtx);
    return true;
}

int pwn_recent_channel(void) { return cur_ch; }

int pwn_aps_near(void)
{
    int64_t now = esp_timer_get_time();
    int c = 0;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < n_nets; i++)
        if (now - (int64_t)nets[i].last_seen * 0 >= 0 && esp_timer_get_time() / 1000000 - nets[i].last_seen < 30) c++;
    xSemaphoreGive(mtx);
    return c;
}

const char *pwn_last_event(void) { return last_event; }
