// pwn.h — motore del "radar" passivo: sniffing, Pokédex reti, handshake, pcap, livelli.
// Nessun pacchetto trasmesso: solo ascolto. Lo stato persiste in NVS/SD.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PWN_MAX_NETS 256
#define PWN_NAME_MAX 20

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;         // migliore visto
    uint8_t channel;
    uint8_t auth;        // 0 aperta, 1 WEP, 2 WPA/2/3
    uint32_t first_seen; // epoch o uptime
    uint32_t last_seen;
    bool handshake;      // catturato almeno un EAPOL per questa rete
} pwn_net_t;

typedef struct {
    char name[PWN_NAME_MAX + 1];
    uint32_t xp;
    uint16_t level;
    uint32_t nets_total;     // reti distinte mai viste
    uint32_t handshakes;     // "pasti"
    uint32_t pkts;           // pacchetti osservati (sessione)
    uint32_t uptime_s;       // tempo totale di caccia
    int mood;                // 0..N, calcolato
} pwn_stats_t;

void pwn_start(void);         // avvia sniffing + hopping (task)
void pwn_stop(void);
bool pwn_running(void);

void pwn_get_stats(pwn_stats_t *out);
void pwn_set_name(const char *name);   // nome mostrato (anche a motore acceso)
int  pwn_net_count(void);                     // reti note (nel Pokédex)
bool pwn_net_get(int i, pwn_net_t *out);      // per indice, ordinate per ultima visita
int  pwn_recent_channel(void);                // canale corrente dell'hopping
int  pwn_aps_near(void);                      // reti viste negli ultimi secondi

void pwn_save(void);          // salva stato (NVS) e flush del pcap
void pwn_reset_pokedex(void); // azzera reti note e livelli (con conferma a monte)
const char *pwn_last_event(void);  // testo dell'ultimo evento, per la faccia
