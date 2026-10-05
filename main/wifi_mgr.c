// wifi_mgr.c — Wi-Fi STA, scansione, portale captive (hotspot + DNS + pagina web), NTP
#include "wifi_mgr.h"
#include "settings.h"
#include "board.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "wifi";

static esp_netif_t *sta_netif, *ap_netif;
static bool started, portal_on, sniff_on;
static wifi_sniff_cb_t sniff_cb;
static void update_mode(void);
static int scan_users;
static wifi_mode_t cur_mode = WIFI_MODE_NULL;
static volatile wifi_state_t state = WIFI_OFF;
static char ip_str[16];
static volatile bool scanning;
static volatile bool conn_fail;   // tentativi esauriti
static volatile uint8_t conn_reason;
static int conn_try;
static volatile uint32_t scan_gen;
static wifi_ap_t aps[32];
static int ap_n;
static SemaphoreHandle_t mtx;
static esp_timer_handle_t reconnect_timer;
static int retry_ms = 1000;
static bool sntp_started;
static volatile bool synced;
static httpd_handle_t httpd;
static volatile bool dns_run;
static volatile bool portal_saved;
static volatile int portal_clients;
static char ap_ssid[24];

static bool want_sta(void) { return g_set.wifi_on && g_set.wifi_ssid[0]; }

/* ---------------- NTP ---------------- */

static void time_cb(struct timeval *tv)
{
    synced = true;
    struct tm utc;
    gmtime_r(&tv->tv_sec, &utc);
    board_rtc_write(&utc);
    ESP_LOGI(TAG, "ora sincronizzata");
}

static void sntp_kick(void)
{
    if (!sntp_started) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        c.sync_cb = time_cb;
        esp_netif_sntp_init(&c);
        sntp_started = true;
    } else {
        esp_netif_sntp_start();
    }
}

bool wifi_mgr_time_synced(void) { return synced; }
void wifi_mgr_sync_time(void) { if (state == WIFI_CONNECTED) sntp_kick(); }

/* ---------------- connessione ---------------- */

static void apply_sta_config(void)
{
    wifi_config_t c = {0};
    strlcpy((char *)c.sta.ssid, g_set.wifi_ssid, sizeof(c.sta.ssid));
    strlcpy((char *)c.sta.password, g_set.wifi_pass, sizeof(c.sta.password));
    c.sta.threshold.authmode = g_set.wifi_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &c);
}

static void reconnect_cb(void *arg)
{
    if (started && want_sta() && state != WIFI_CONNECTED && !conn_fail) esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (want_sta()) { state = WIFI_CONNECTING; esp_wifi_connect(); }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = data;
            conn_reason = d ? d->reason : 0;
            ip_str[0] = 0;
            if (started && want_sta()) {
                // password errata o AP che rifiuta: non martellare, segnala il fallimento
                bool auth_err = conn_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                conn_reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                                conn_reason == WIFI_REASON_AUTH_FAIL ||
                                conn_reason == WIFI_REASON_AUTH_EXPIRE ||
                                conn_reason == WIFI_REASON_CONNECTION_FAIL ||
                                conn_reason == WIFI_REASON_NO_AP_FOUND;
                if (auth_err && ++conn_try >= 3) {
                    conn_fail = true;
                    state = WIFI_NO_NETWORK;
                    break;
                }
                state = WIFI_CONNECTING;
                esp_timer_stop(reconnect_timer);
                esp_timer_start_once(reconnect_timer, (uint64_t)retry_ms * 1000);
                retry_ms = retry_ms * 2 > 30000 ? 30000 : retry_ms * 2;
            } else {
                state = g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF;
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE: {
            scanning = false;
            if (state == WIFI_CONNECTING) { esp_wifi_clear_ap_list(); scan_gen++; break; }
            uint16_t n = 32;
            wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
            if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
                xSemaphoreTake(mtx, portMAX_DELAY);
                ap_n = 0;
                for (int i = 0; i < n; i++) {
                    if (!rec[i].ssid[0]) continue; // reti nascoste
                    wifi_ap_t *a = &aps[ap_n++];
                    strlcpy(a->ssid, (char *)rec[i].ssid, sizeof(a->ssid));
                    a->rssi = rec[i].rssi;
                    a->channel = rec[i].primary;
                    a->auth = rec[i].authmode;
                    memcpy(a->bssid, rec[i].bssid, 6);
                }
                xSemaphoreGive(mtx);
            } else {
                esp_wifi_clear_ap_list();
            }
            free(rec);
            scan_gen++;
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: portal_clients++; break;
        case WIFI_EVENT_AP_STADISCONNECTED: if (portal_clients > 0) portal_clients--; break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&e->ip_info.ip));
        state = WIFI_CONNECTED;
        retry_ms = 1000;
        conn_fail = false;
        conn_try = 0;
        sntp_kick();
    }
}

static void sniff_trampoline(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (sniff_cb) sniff_cb(buf, type);
}

bool wifi_mgr_sniff_start(wifi_sniff_cb_t cb)
{
    if (sniff_on) return true;
    // ferma tutto il resto: niente connessione, niente scansione, niente portale
    if (scanning) { esp_wifi_scan_stop(); scanning = false; }
    if (started && (state == WIFI_CONNECTED || state == WIFI_CONNECTING)) esp_wifi_disconnect();
    sniff_cb = cb;
    sniff_on = true;
    if (cur_mode != WIFI_MODE_STA) { esp_wifi_set_mode(WIFI_MODE_STA); cur_mode = WIFI_MODE_STA; }
    if (!started) { esp_wifi_start(); started = true; }
    state = WIFI_OFF;
    ip_str[0] = 0;
    wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(sniff_trampoline);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) { sniff_on = false; return false; }
    return true;
}

void wifi_mgr_sniff_stop(void)
{
    if (!sniff_on) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    sniff_cb = NULL;
    sniff_on = false;
    update_mode();   // ripristina il Wi-Fi normale
    if (started && want_sta()) { state = WIFI_CONNECTING; esp_wifi_connect(); }
}

void wifi_mgr_sniff_channel(int ch)
{
    if (sniff_on && ch >= 1 && ch <= 13) esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

bool wifi_mgr_sniffing(void) { return sniff_on; }

static void update_mode(void)
{
    if (sniff_on) return;   // la modalità promiscua gestisce la radio da sé
    bool need = g_set.wifi_on || scan_users > 0 || portal_on;
    if (!need) {
        if (started) { esp_wifi_stop(); started = false; }
        state = WIFI_OFF;
        ip_str[0] = 0;
        return;
    }
    wifi_mode_t mode = portal_on ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (mode != cur_mode) { esp_wifi_set_mode(mode); cur_mode = mode; }
    if (portal_on) {
        wifi_config_t ap = {0};
        strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen(ap_ssid);
        ap.ap.channel = 1;
        ap.ap.max_connection = 4;
        ap.ap.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (want_sta()) apply_sta_config();

    if (!started) {
        state = want_sta() ? WIFI_CONNECTING : (g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF);
        esp_wifi_start();
        started = true;
        return;
    }
    if (want_sta()) {
        if (state != WIFI_CONNECTED && state != WIFI_CONNECTING) { state = WIFI_CONNECTING; esp_wifi_connect(); }
    } else {
        if (state == WIFI_CONNECTED || state == WIFI_CONNECTING) esp_wifi_disconnect();
        state = g_set.wifi_on ? WIFI_NO_NETWORK : WIFI_OFF;
    }
}

void wifi_mgr_init(void)
{
    mtx = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    const esp_timer_create_args_t ta = {.callback = reconnect_cb, .name = "wifi_rc"};
    esp_timer_create(&ta, &reconnect_timer);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ap_ssid, sizeof(ap_ssid), "Gadget-%02X%02X", mac[4], mac[5]);
    esp_netif_set_hostname(sta_netif, "gadget");
    update_mode();
}

void wifi_mgr_apply(void)
{
    if (started && want_sta()) {
        // credenziali cambiate o riaccensione: riparti da zero
        esp_wifi_disconnect();
        state = WIFI_NO_NETWORK;
        retry_ms = 1000;
    }
    update_mode();
}

wifi_state_t wifi_mgr_state(void) { return state; }
const char *wifi_mgr_ip(void) { return ip_str; }

int wifi_mgr_rssi(void)
{
    wifi_ap_record_t r;
    if (state == WIFI_CONNECTED && esp_wifi_sta_get_ap_info(&r) == ESP_OK) return r.rssi;
    return 0;
}

void wifi_mgr_forget(void)
{
    g_set.wifi_ssid[0] = 0;
    g_set.wifi_pass[0] = 0;
    settings_save();
    if (started) esp_wifi_disconnect();
    update_mode();
}

/* ---------------- scansione ---------------- */

void wifi_mgr_scan_acquire(void) { scan_users++; update_mode(); }
void wifi_mgr_scan_release(void) { if (scan_users > 0) scan_users--; update_mode(); }

bool wifi_mgr_scan_start(void)
{
    // non scansionare durante un tentativo di connessione: i due usi della radio si
    // escludono a vicenda e lascerebbero il Wi-Fi bloccato
    if (!started || scanning || state == WIFI_CONNECTING) return false;
    wifi_scan_config_t sc = {.show_hidden = false};
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) return false;
    scanning = true;
    return true;
}

bool wifi_mgr_scan_busy(void) { return scanning; }
uint32_t wifi_mgr_scan_gen(void) { return scan_gen; }

int wifi_mgr_scan_results(wifi_ap_t *out, int max)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    int n = ap_n < max ? ap_n : max;
    memcpy(out, aps, n * sizeof(wifi_ap_t));
    xSemaphoreGive(mtx);
    return n;
}

bool wifi_mgr_conn_failed(void) { return conn_fail; }
int wifi_mgr_conn_reason(void) { return conn_reason; }

const char *wifi_mgr_band(int ch)
{
    return ch >= 32 ? "5 GHz" : "2.4 GHz";
}

bool wifi_mgr_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64) return false;
    strlcpy(g_set.wifi_ssid, ssid, sizeof(g_set.wifi_ssid));
    strlcpy(g_set.wifi_pass, pass, sizeof(g_set.wifi_pass));
    g_set.wifi_on = true;
    settings_save();
    // la radio deve smettere di scansionare prima di potersi collegare
    if (scanning) { esp_wifi_scan_stop(); scanning = false; }
    if (started) esp_wifi_disconnect();
    retry_ms = 1000;
    conn_fail = false;
    conn_try = 0;
    state = WIFI_CONNECTING;
    apply_sta_config();
    esp_wifi_connect();
    return true;
}

const char *wifi_mgr_auth_name(int a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return "Aperta";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "Altro";
    }
}

/* ---------------- portale captive ---------------- */

static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval to = {.tv_sec = 0, .tv_usec = 300000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { close(s); vTaskDelete(NULL); }
    uint8_t buf[512];
    while (dns_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        int q = 12;
        while (q < n && buf[q]) q += buf[q] + 1;
        q += 5; // terminatore + QTYPE + QCLASS
        if (q > n || q + 16 > (int)sizeof(buf)) continue;
        bool type_a = buf[q - 4] == 0 && buf[q - 3] == 1;
        buf[2] = 0x81; buf[3] = 0x80;
        buf[4] = 0; buf[5] = 1;
        buf[6] = 0; buf[7] = type_a ? 1 : 0;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = q;
        if (type_a) {
            // ogni nome risolve su 192.168.4.1, così il telefono apre il portale da solo
            const uint8_t ans[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
            memcpy(buf + q, ans, 16);
            len += 16;
        }
        sendto(s, buf, len, 0, (struct sockaddr *)&from, fl);
    }
    close(s);
    vTaskDelete(NULL);
}

static void html_escape(httpd_req_t *r, const char *s)
{
    char out[8];
    for (; *s; s++) {
        switch (*s) {
        case '<': httpd_resp_sendstr_chunk(r, "&lt;"); break;
        case '>': httpd_resp_sendstr_chunk(r, "&gt;"); break;
        case '&': httpd_resp_sendstr_chunk(r, "&amp;"); break;
        case '"': httpd_resp_sendstr_chunk(r, "&quot;"); break;
        case '\'': httpd_resp_sendstr_chunk(r, "&#39;"); break;
        default: out[0] = *s; out[1] = 0; httpd_resp_sendstr_chunk(r, out);
        }
    }
}

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=it><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>Gadget Wi-Fi</title>"
    "<style>body{font-family:system-ui,sans-serif;background:#0b0d10;color:#ededed;margin:0;padding:24px;max-width:480px}"
    "h1{font-size:1.4rem;margin:0 0 4px}p{color:#9aa3ad;margin:0 0 18px}"
    "button.n{display:flex;justify-content:space-between;width:100%;text-align:left;padding:12px 14px;margin:0 0 6px;"
    "background:#16191d;color:#ededed;border:1px solid #2a2f35;border-radius:10px;font:inherit}"
    "button.n span{color:#9aa3ad}label{display:block;margin:16px 0 6px;color:#9aa3ad}"
    "input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #2a2f35;background:#16191d;color:#ededed;font:inherit}"
    "button.s{margin-top:18px;width:100%;padding:14px;border:0;border-radius:10px;background:#ffb020;color:#111;font:600 1rem system-ui}"
    "</style>";

static esp_err_t page_get(httpd_req_t *r)
{
    if (strcmp(r->uri, "/") != 0) {
        // qualsiasi altro indirizzo (es. i controlli captive di Android/iOS) porta al portale
        httpd_resp_set_status(r, "302 Found");
        httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
        return httpd_resp_send(r, NULL, 0);
    }
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(r, PAGE_HEAD);
    httpd_resp_sendstr_chunk(r, "<h1>Collega il gadget al Wi-Fi</h1><p>Scegli una rete o scrivine il nome.</p>");
    static wifi_ap_t list[32];
    int n = wifi_mgr_scan_results(list, 32);
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) if (!strcmp(list[i].ssid, list[j].ssid)) dup = true;
        if (dup) continue;
        httpd_resp_sendstr_chunk(r, "<button type=button class=n onclick=\"document.getElementById('s').value=this.dataset.s;document.getElementById('p').focus()\" data-s=\"");
        html_escape(r, list[i].ssid);
        httpd_resp_sendstr_chunk(r, "\">");
        html_escape(r, list[i].ssid);
        char meta[32];
        snprintf(meta, sizeof(meta), "<span>%d dBm</span></button>", list[i].rssi);
        httpd_resp_sendstr_chunk(r, meta);
    }
    httpd_resp_sendstr_chunk(r,
        "<form method=post action=/save><label for=s>Nome rete</label><input id=s name=s maxlength=32 required>"
        "<label for=p>Password</label><input id=p name=p type=password maxlength=64>"
        "<button class=s>Salva e collega</button></form></html>");
    return httpd_resp_sendstr_chunk(r, NULL);
}

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && isxdigit((int)s[1]) && isxdigit((int)s[2])) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

static esp_err_t save_post(httpd_req_t *r)
{
    char body[256] = {0};
    int len = r->content_len < (int)sizeof(body) - 1 ? r->content_len : (int)sizeof(body) - 1;
    int got = 0;
    while (got < len) {
        int k = httpd_req_recv(r, body + got, len - got);
        if (k <= 0) return ESP_FAIL;
        got += k;
    }
    char ssid[100] = "", pass[200] = "";
    httpd_query_key_value(body, "s", ssid, sizeof(ssid));
    httpd_query_key_value(body, "p", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(r, PAGE_HEAD);
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64) {
        httpd_resp_sendstr_chunk(r, "<h1>Dati non validi</h1><p><a href=/ style=color:#ffb020>Riprova</a></p>");
        return httpd_resp_sendstr_chunk(r, NULL);
    }
    strlcpy(g_set.wifi_ssid, ssid, sizeof(g_set.wifi_ssid));
    strlcpy(g_set.wifi_pass, pass, sizeof(g_set.wifi_pass));
    g_set.wifi_on = true;
    settings_save();
    httpd_resp_sendstr_chunk(r, "<h1>Salvato</h1><p>Il gadget si sta collegando a <b>");
    html_escape(r, ssid);
    httpd_resp_sendstr_chunk(r, "</b>. Controlla lo schermo: puoi chiudere questa pagina.</p></html>");
    httpd_resp_sendstr_chunk(r, NULL);
    portal_saved = true;
    wifi_mgr_apply();
    return ESP_OK;
}

void wifi_mgr_portal_start(void)
{
    if (portal_on) return;
    portal_on = true;
    portal_saved = false;
    portal_clients = 0;
    update_mode();
    wifi_mgr_scan_start();

    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.lru_purge_enable = true;
    c.stack_size = 6144;
    if (httpd_start(&httpd, &c) == ESP_OK) {
        httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
        httpd_uri_t page = {.uri = "/*", .method = HTTP_GET, .handler = page_get};
        httpd_register_uri_handler(httpd, &save);
        httpd_register_uri_handler(httpd, &page);
    }
    dns_run = true;
    xTaskCreate(dns_task, "dns", 4096, NULL, 4, NULL);
}

void wifi_mgr_portal_stop(void)
{
    if (!portal_on) return;
    dns_run = false;
    if (httpd) { httpd_stop(httpd); httpd = NULL; }
    vTaskDelay(pdMS_TO_TICKS(350)); // lascia chiudere il task DNS
    portal_on = false;
    update_mode();
}

const char *wifi_mgr_portal_ssid(void) { return ap_ssid; }
int wifi_mgr_portal_clients(void) { return portal_clients; }
bool wifi_mgr_portal_saved(void) { return portal_saved; }
