// ble_mgr.h — Bluetooth LE (NimBLE): visibilità, scansione e collegamenti.
//
// I collegamenti vanno nei due sensi:
//  - Gadget → altri (periferica): un telefono o un computer si collega al Gadget, che
//    poi potrà fare da tastiera, telecomando… Si apre una finestra di associazione
//    (ble_mgr_pair_start); fuori da lì solo i dispositivi già associati possono collegarsi.
//  - Altri → Gadget (centrale): il Gadget si collega a un dispositivo trovato con la
//    scansione (sensori, tastiere, telecomandi BLE…) e ne legge i servizi.
// Le funzioni vere e proprie (tastiera, audio…) si agganciano come "profili".
//
// Nota: l'ESP32-S3 ha solo il Bluetooth LE, non il Bluetooth "classico" (BR/EDR) che
// usano casse e cuffie per l'audio (A2DP): quello non si può fare con questa scheda.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    char name[32];
    bool connectable;       // accetta collegamenti
    uint16_t appearance;    // tipo di dispositivo dichiarato (0 = sconosciuto)
    uint16_t svc16;         // primo servizio dichiarato (UUID a 16 bit, 0 = nessuno)
} ble_dev_t;

void ble_mgr_apply(void);          // applica g_set.ble_on / ble_visible
bool ble_mgr_on(void);             // l'utente ha attivato il Bluetooth
bool ble_mgr_ready(void);          // stack avviato e sincronizzato
const char *ble_mgr_name(void);
void ble_mgr_addr(char *buf, int n);
void ble_mgr_set_battery(int pct); // livello pubblicato agli altri dispositivi

void ble_mgr_scan_acquire(void);   // accende lo stack anche se il BT è spento
void ble_mgr_scan_release(void);
bool ble_mgr_scan_start(int ms);
bool ble_mgr_scan_busy(void);
uint32_t ble_mgr_scan_gen(void);
int  ble_mgr_scan_results(ble_dev_t *out, int max); // ordinati per segnale

/* ---------------- collegamenti ---------------- */

#define BLE_MAX_CONN 3
#define BLE_MAX_SVCS 12

typedef enum { BLE_DISC_NONE, BLE_DISC_RUNNING, BLE_DISC_DONE, BLE_DISC_FAILED } ble_disc_t;

typedef struct {
    uint16_t handle;
    bool central;           // true: il Gadget si è collegato a lui (altri → Gadget)
    uint8_t addr[6];
    char name[32];
    bool encrypted, bonded;
    bool trusted;           // ammesso: voluto dal Gadget, accettato a finestra aperta o associato
    uint16_t appearance;
    uint16_t svcs[BLE_MAX_SVCS]; // servizi trovati (UUID a 16 bit; 0xFFFF = a 128 bit)
    int n_svcs;
    ble_disc_t disc;
} ble_conn_t;

typedef enum { BLE_LINK_IDLE, BLE_LINK_CONNECTING, BLE_LINK_OK, BLE_LINK_FAILED } ble_link_t;

uint32_t ble_mgr_conn_gen(void);                    // cambia a ogni evento di collegamento
int  ble_mgr_conns(ble_conn_t *out, int max);
bool ble_mgr_connect(const ble_dev_t *d);           // altri → Gadget
ble_link_t ble_mgr_link_state(const uint8_t addr[6]); // esito dell'ultimo ble_mgr_connect
const char *ble_mgr_link_error(void);
void ble_mgr_disconnect(uint16_t handle);
bool ble_mgr_conn_trusted(uint16_t handle);         // per i servizi che non vogliono sconosciuti

// finestra di associazione (Gadget → altri): il Gadget si fa trovare e accetta un
// nuovo telefono o computer per `seconds` secondi
void ble_mgr_pair_start(int seconds);
void ble_mgr_pair_stop(void);
int  ble_mgr_pair_left(void);                       // secondi rimasti (0 = chiusa)
uint32_t ble_mgr_passkey(void);                     // codice da mostrare (0 = nessuno)

// dispositivi associati (memorizzati nell'NVS)
typedef struct { uint8_t addr[6]; uint8_t type; } ble_bond_t;
int  ble_mgr_bonds(ble_bond_t *out, int max);
void ble_mgr_forget(const ble_bond_t *b);
void ble_mgr_forget_all(void);

// nomi leggibili per servizi e tipi di dispositivo
const char *ble_mgr_svc_name(uint16_t uuid);       // NULL se sconosciuto
const char *ble_mgr_appearance_name(uint16_t app); // NULL se sconosciuto
bool ble_mgr_is_audio(uint16_t appearance);        // casse, cuffie… (audio non supportato)

/* ---------------- profili ---------------- */

// Una funzione che usa i collegamenti (es. tastiera Bluetooth) si registra all'avvio,
// prima di ble_mgr_apply(). svcs è un array di struct ble_gatt_svc_def (NimBLE) terminato
// da {0}, aggiunto al server GATT del Gadget; on_conn segnala collegamenti e scollegamenti.
typedef struct {
    const char *name;
    const void *svcs;
    uint16_t appearance;    // se non 0, il Gadget si presenta come questo tipo di dispositivo
    void (*on_conn)(const ble_conn_t *c, bool up);
} ble_profile_t;
bool ble_mgr_register_profile(const ble_profile_t *p);
