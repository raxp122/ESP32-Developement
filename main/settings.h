// settings.h — impostazioni persistenti (NVS)
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    QUICK_NONE = 0,
    QUICK_TORCH,
    QUICK_SCREEN_OFF,
    QUICK_DICE,
    QUICK_WIFI_SCAN,
    QUICK_BLE_SCAN,
    QUICK_CLOCK,
    QUICK_COUNT
} quick_action_t;

typedef struct {
    uint8_t  version;
    // schermo
    uint8_t  brightness;      // 5–100
    uint16_t sleep_s;         // 0 = mai
    bool     flipped;
    bool     invert_scroll;
    uint8_t  accent;          // indice palette
    // input
    uint8_t  quick_action;    // quick_action_t
    // wi-fi
    bool     wifi_on;
    char     wifi_ssid[33];
    char     wifi_pass[65];
    // bluetooth
    bool     ble_on;
    bool     ble_visible;
    // doom (aggiunti in v3: i campi nuovi vanno sempre in fondo)
    uint8_t  doom_sens;       // 1–5
    bool     doom_inv_steer;
    bool     doom_inv_pitch;
    // v4
    uint8_t  volume;          // 0–100
    uint8_t  saber_color;     // indice palette spada
    // v5
    uint8_t  saber_clash;     // sensibilità scontro: 0 bassa, 1 media, 2 alta
    // v6
    int8_t   bercio_cal;      // calibrazione Berciometro, passi da 0,5 dB
    // v7
    uint16_t a4_x10;          // LA di riferimento in decimi di Hz (4400 = 440,0 Hz)
    // v9 - pwnagotchi
    char     pwn_name[21];    // nome del "pet"
    uint8_t  pwn_pcap;        // 0 = non salvare pcap, 1 = salva handshake
    uint8_t  pwn_ai;          // 0 = statico, 1 = reattivo all'ambiente
} settings_t;

const char *g_set_pwn_name(void);

extern settings_t g_set;

void settings_load(void);
void settings_save(void);
void settings_reset(void);
const char *settings_quick_name(int q);
