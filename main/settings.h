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
    // v10
    uint8_t  boot_app;        // app aperta all'accensione: indice in boot_apps (0 = nessuna)
    uint8_t  pet_time;        // polipetto: 0 tempo reale, 1 solo a scheda accesa, 2 solo con l'app aperta
    uint8_t  pet_sound;       // versi del polipetto
    uint8_t  pet_steps;       // contapassi
    uint8_t  pet_tilt_inv;    // inverte l'inclinazione nel minigioco
    // v11
    uint8_t  pet_sleep_h;     // modalità ibrida: ora della nanna (0–23)
    uint8_t  pet_wake_h;      // modalità ibrida: ora della sveglia (0–23)
    uint8_t  ota_auto;        // controlla gli aggiornamenti quando c'è internet
    // v12
    uint8_t  backup_before_ota;   // backup automatico sulla microSD prima di ogni aggiornamento
    // v13 - Appunti (testo dal PC → tastiera USB)
    uint8_t  kb_layout;           // layout della tastiera USB (kb_layout_t)
} settings_t;

// layout della tastiera USB. L'ordine è salvato: le voci nuove vanno in fondo.
enum { KB_LAYOUT_IT = 0, KB_LAYOUT_US, KB_LAYOUT_US_INTL, KB_LAYOUT_UK,
       KB_LAYOUT_DE, KB_LAYOUT_FR, KB_LAYOUT_ES, KB_LAYOUT_COUNT };
const char *settings_layout_name(int i);

// i valori sono salvati: quelli nuovi vanno in fondo
enum { PET_TIME_REAL = 0, PET_TIME_DEVICE, PET_TIME_APP, PET_TIME_HYBRID, PET_TIME_COUNT };

const char *g_set_pwn_name(void);

extern settings_t g_set;

void settings_load(void);
void settings_save(void);
void settings_defer(bool on);   // true: i salvataggi si rimandano; false: salva se serve
void settings_reset(void);
int  settings_version(void);   // versione del formato delle impostazioni (per i backup)
const char *settings_quick_name(int q);
