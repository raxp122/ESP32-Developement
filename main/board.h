// board.h — Waveshare ESP32-S3-Touch-LCD-3.49 V1: pin e periferiche di bordo
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "driver/i2c_master.h"

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

typedef struct { float x, y, z; } vec3_t;

void board_init(void);                      // I2C, latch alimentazione, ADC, pulsanti
i2c_master_dev_handle_t board_touch_dev(void);
i2c_master_bus_handle_t board_i2c0(void);   // bus di TCA9554, RTC, IMU e codec audio

void  board_power_off(void);               // rilascia il latch (solo a batteria)
float board_battery_volts(void);            // tensione batteria (0 se non leggibile)
int   board_battery_percent(float v);

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
