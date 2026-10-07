// wifimap.h — Mappa Wi-Fi relativa: un grafo delle reti costruito camminando.
//
// Niente pianta né GPS. A ogni scansione la potenza di ogni rete dà una distanza stimata
// dal Gadget (modello di propagazione: -40 dBm a 1 m, esponente 2,7). Le ultime scansioni
// sono "pose" (dove ero, con le distanze dalle reti viste): pose e posizioni delle reti si
// sistemano insieme finché le distanze tornano, con il vincolo che fra una scansione e
// l'altra si fanno pochi passi (come i robot che si orientano solo con le distanze). Ne
// esce una mappa relativa: distanze indicative (qualche metro), orientamento arbitrario.
// Due reti viste nella stessa scansione sono collegate da un "filo" nel grafo.
//
// Fisse e mobili: una rete è "hotspot" se ha l'indirizzo casuale o un nome da telefono, e
// "mobile" se nella mappa continua a spostarsi molto più delle altre (in proporzione alla
// sua distanza). Hotspot e mobili non deformano la mappa: si stimano a parte, sul cerchio
// della loro distanza da me, con la loro scia.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wifi_mgr.h"

#define WM_MAX    48
#define WM_TRAIL  64
#define WM_MTRAIL 8

enum { WM_FIXED, WM_MOVING, WM_HOTSPOT };   // tipo di rete

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    float x, y;            // posizione nella mappa (metri, relativi)
    float rssi;            // potenza recente (media mobile)
    float dist;            // distanza stimata all'ultima scansione
    uint16_t seen;         // scansioni in cui è stata vista
    uint32_t last;         // numero dell'ultima scansione in cui c'era
    uint8_t kind;          // WM_*
    uint8_t placed;        // ha una posizione
    uint16_t checks, bad;  // controlli di coerenza con le distanze e quanti falliti
    float drift;           // quanto si sposta nella mappa a ogni scansione, rispetto alla distanza (media)
    float dist_avg;        // distanza media dal Gadget quando la vede
    float tx[WM_MTRAIL], ty[WM_MTRAIL];   // scia (solo mobili)
    uint8_t tn;
} wm_node_t;

void wm_reset(void);
void wm_add_scan(const wifi_ap_t *aps, int n);
int  wm_count(void);
const wm_node_t *wm_node(int i);
bool wm_edge(int i, int j, float *len, int *count);   // il filo fra due reti (se c'è)
int  wm_links(int i);                                 // fili di una rete (-1: tutti)
uint32_t wm_scans(void);
bool wm_me(float *x, float *y);                       // dove si trova il Gadget
int  wm_trail(float (*pts)[2], int max);              // scia del percorso (dalla più vecchia)
float wm_rssi_dist(float rssi);                       // dBm → metri (stima)

bool wm_save(const char *path);
bool wm_load(const char *path);
bool wm_export(const char *dir);                      // reti.csv e collegamenti.csv
