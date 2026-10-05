// ble_mgr.h — Bluetooth LE (NimBLE): visibilità e scansione
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    char name[32];
} ble_dev_t;

void ble_mgr_apply(void);          // applica g_set.ble_on / ble_visible
bool ble_mgr_on(void);             // l'utente ha attivato il Bluetooth
bool ble_mgr_ready(void);          // stack avviato e sincronizzato
const char *ble_mgr_name(void);
void ble_mgr_addr(char *buf, int n);

void ble_mgr_scan_acquire(void);   // accende lo stack anche se il BT è spento
void ble_mgr_scan_release(void);
bool ble_mgr_scan_start(int ms);
bool ble_mgr_scan_busy(void);
uint32_t ble_mgr_scan_gen(void);
int  ble_mgr_scan_results(ble_dev_t *out, int max); // ordinati per segnale
