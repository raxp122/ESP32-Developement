// board.h — pin e periferiche di bordo. Lo stesso firmware gira su due schede, riconosciute
// all'avvio (board_init):
//   - Waveshare ESP32-S3-Touch-LCD-3.49 V1: LCD 640×172 (AXS15231B), touch AXS su un bus
//     a parte, latch di alimentazione sul TCA9554, batteria sull'ADC;
//   - Waveshare ESP32-S3-Touch-AMOLED-1.75 (anche -B e -G): AMOLED tondo 466×466 (CO5300),
//     touch CST9217, alimentazione e batteria dall'AXP2101, PWR letto dal TCA9554.
// Stesso ESP32-S3R8 (8 MB PSRAM ottale) e 16 MB di flash: un solo file per tutte e due.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "driver/i2c_master.h"

typedef enum { BOARD_LCD349, BOARD_AMOLED175 } board_kind_t;
board_kind_t board_kind(void);
#define BOARD_IS_ROUND() (board_kind() == BOARD_AMOLED175)
const char *board_name(void);

// ===================== ESP32-S3-Touch-LCD-3.49 =====================

// --- Display QSPI (AXS15231B) ---
#define PIN_LCD_CS      9
#define PIN_LCD_PCLK    10
#define PIN_LCD_D0      11
#define PIN_LCD_D1      12
#define PIN_LCD_D2      13
#define PIN_LCD_D3      14
#define PIN_LCD_RST     21
#define PIN_LCD_BL      8   // V1. Sulla V2 la retroilluminazione è su un altro pin!
#define LCD_W           172 // risoluzione fisica (verticale)
#define LCD_H           640

// --- Bus I2C ---
#define PIN_I2C0_SDA    47  // TCA9554, RTC, IMU
#define PIN_I2C0_SCL    48
#define PIN_I2C1_SDA    17  // touch
#define PIN_I2C1_SCL    18
#define ADDR_TOUCH      0x3B
#define ADDR_TCA9554    0x20
#define ADDR_RTC        0x51
#define ADDR_IMU        0x6B

// --- Pulsanti ---
#define PIN_BTN_BOOT    0
#define PIN_BTN_PWR     16

// --- Espansore TCA9554 ---
#define EXIO_POWER_HOLD 6   // alto = resta acceso a batteria
#define EXIO_AUX        7   // alto come nel firmware di fabbrica

// ===================== ESP32-S3-Touch-AMOLED-1.75 =====================
#define R_PIN_I2C_SDA   15  // AXP2101, TCA9554, RTC, IMU, touch, codec
#define R_PIN_I2C_SCL   14
#define R_PIN_LCD_CS    12
#define R_PIN_LCD_PCLK  38
#define R_PIN_LCD_D0    4
#define R_PIN_LCD_D1    5
#define R_PIN_LCD_D2    6
#define R_PIN_LCD_D3    7
#define R_PIN_LCD_RST   39
#define R_PIN_TP_RST    40
#define R_PIN_TP_INT    11
#define R_LCD_W         466
#define R_LCD_H         466
#define R_LCD_X_GAP     6   // il controller ha 6 colonne in più a sinistra
#define R_ADDR_TOUCH    0x5A
#define R_ADDR_AXP2101  0x34
#define R_EXIO_PWR      4   // SYS_OUT: alto mentre PWR è premuto
#define R_EXIO_GPS_RST  7   // solo -G: reset del GPS (alto = acceso)

typedef struct { float x, y, z; } vec3_t;

void board_init(void);                      // I2C, latch alimentazione, ADC, pulsanti
i2c_master_dev_handle_t board_touch_dev(void);   // NULL se il bus del touch non c'è
// Il touch non risponde più (bus bloccato, driver I2C in errore dopo un timeout): sblocca
// e ricrea il bus (3.49) o azzera bus e controller (tondo). Dal task che legge il touch.
bool board_touch_recover(void);
int  board_touch_recoveries(void);
i2c_master_bus_handle_t board_i2c0(void);   // bus di TCA9554, RTC, IMU e codec audio

void  board_power_off(void);               // rilascia il latch (solo a batteria)
float board_battery_volts(void);            // tensione batteria (0 se non c'è o non è leggibile)
int   board_battery_percent(float v);        // sull'AMOLED dal misuratore dell'AXP2101 (v serve solo sulla 3.49)
bool  board_charging(void);                  // in carica (solo AMOLED; false se non si sa)

typedef enum { RTC_OK, RTC_NO_CHIP, RTC_STOPPED, RTC_UNSET } rtc_status_t;
rtc_status_t board_rtc_get(struct tm *utc);      // RTC_STOPPED: l'oscillatore si è fermato (è rimasto senza corrente)
bool  board_rtc_read(struct tm *utc);       // false se l'orario nell'RTC non è valido
bool  board_rtc_write(const struct tm *utc);   // scrive e rilegge: false se non ha preso l'ora
rtc_status_t board_rtc_boot_status(void);   // com'era l'RTC alla prima lettura (all'accensione)

bool  board_imu_ok(void);
bool  board_imu_accel(vec3_t *g);           // accelerazione in g
void  board_imu_gyro_enable(bool on);       // il giroscopio consuma: si accende solo quando serve
bool  board_imu_read6(vec3_t *g, vec3_t *dps); // accelerazione (g) + velocità angolare (°/s)

bool  board_btn_boot(void);                 // true = premuto
bool  board_btn_pwr(void);
