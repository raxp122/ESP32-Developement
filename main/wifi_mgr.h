// wifi_mgr.h — Wi-Fi: connessione, scansione, portale di configurazione, ora NTP
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { WIFI_OFF, WIFI_NO_NETWORK, WIFI_CONNECTING, WIFI_CONNECTED } wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth;      // wifi_auth_mode_t
    uint8_t bssid[6];
} wifi_ap_t;

const char *wifi_mgr_band(int channel);  // "2.4" o "5" GHz dal numero di canale
bool wifi_mgr_conn_failed(void);          // il tentativo di connessione è fallito (password/AP)
int  wifi_mgr_conn_reason(void);          // ultimo motivo di disconnessione (wifi_err_reason_t)

// Modalità promiscua (cattura passiva, usata dal pwnagotchi). Mentre è attiva il
// Wi-Fi normale è sospeso: la radio può fare una cosa sola.
typedef void (*wifi_sniff_cb_t)(void *pkt, int type);   // wifi_promiscuous_pkt_t*, wifi_promiscuous_pkt_type_t
bool wifi_mgr_sniff_start(wifi_sniff_cb_t cb);
void wifi_mgr_sniff_stop(void);
void wifi_mgr_sniff_channel(int ch);      // 1–13
bool wifi_mgr_sniffing(void);
bool wifi_mgr_connect(const char *ssid, const char *pass);  // salva e si collega

void wifi_mgr_init(void);
void wifi_mgr_apply(void);                 // applica g_set.wifi_on / credenziali
wifi_state_t wifi_mgr_state(void);
const char *wifi_mgr_ip(void);
int  wifi_mgr_rssi(void);
void wifi_mgr_forget(void);

// Scansione (accende temporaneamente la radio se il Wi-Fi è spento)
void wifi_mgr_scan_acquire(void);
void wifi_mgr_scan_release(void);
bool wifi_mgr_scan_start(void);
bool wifi_mgr_scan_busy(void);
uint32_t wifi_mgr_scan_gen(void);          // aumenta a ogni scansione completata
int  wifi_mgr_scan_results(wifi_ap_t *out, int max);
const char *wifi_mgr_auth_name(int auth);

// Portale: hotspot + pagina web per inserire la rete dal telefono
void wifi_mgr_portal_start(void);         // pagina: configura il Wi-Fi
void wifi_mgr_portal_start_clips(void);   // pagina: Appunti (manda testi via Bluetooth)
// pagina: un file HTML (es. sulla microSD). "/" lo mostra, "/<download_name>" lo scarica
void wifi_mgr_portal_start_file(const char *path, const char *download_name);
void wifi_mgr_portal_open(void);          // interna: avvia l'hotspot con la pagina scelta
void wifi_mgr_portal_stop(void);          // chiude l'hotspot e ripristina il Wi-Fi normale
const char *wifi_mgr_portal_ssid(void);
int  wifi_mgr_portal_clients(void);
bool wifi_mgr_portal_saved(void);
void wifi_mgr_portal_poll(void);          // dal task dell'interfaccia: applica le credenziali ricevute          // credenziali ricevute

// Ora
bool wifi_mgr_time_synced(void);
bool wifi_mgr_time_saved(void);    // l'ultima sincronizzazione è stata scritta (e riletta) nell'RTC
void wifi_mgr_sync_time(void);
