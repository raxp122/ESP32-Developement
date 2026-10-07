// board.c — inizializzazione hardware di base
#include "board.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t bus0, bus1;
static i2c_master_dev_handle_t dev_tca, dev_rtc, dev_imu, dev_touch;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t adc_cali;
static bool imu_ok;

static esp_err_t reg_write(i2c_master_dev_handle_t d, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(d, b, 2, 50);
}

static esp_err_t reg_read(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(d, &reg, 1, buf, n, 50);
}

static i2c_master_dev_handle_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 300000,
    };
    i2c_master_dev_handle_t h = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &h));
    return h;
}

static void tca_init(void)
{
    // Uscite 6 e 7 alte (come il firmware di fabbrica): la 6 tiene accesa la scheda a batteria
    uint8_t out = 0xFF, cfg = 0xFF;
    reg_read(dev_tca, 0x01, &out, 1);
    reg_read(dev_tca, 0x03, &cfg, 1);
    out |= (1 << EXIO_POWER_HOLD) | (1 << EXIO_AUX);
    cfg &= ~((1 << EXIO_POWER_HOLD) | (1 << EXIO_AUX));
    if (reg_write(dev_tca, 0x01, out) != ESP_OK || reg_write(dev_tca, 0x03, cfg) != ESP_OK)
        ESP_LOGW(TAG, "TCA9554 non risponde");
}

static void imu_init(void)
{
    uint8_t id = 0;
    if (reg_read(dev_imu, 0x00, &id, 1) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 non trovato (id=0x%02x)", id);
        return;
    }
    reg_write(dev_imu, 0x02, 0x40); // CTRL1: auto-incremento indirizzi
    reg_write(dev_imu, 0x03, 0x15); // CTRL2: ±4 g, ODR ~ 470 Hz
    reg_write(dev_imu, 0x08, 0x01); // CTRL7: abilita accelerometro
    imu_ok = true;
}

void board_init(void)
{
    i2c_master_bus_config_t bc = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = PIN_I2C0_SCL,
        .sda_io_num = PIN_I2C0_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus0));
    bc.i2c_port = I2C_NUM_1;
    bc.scl_io_num = PIN_I2C1_SCL;
    bc.sda_io_num = PIN_I2C1_SDA;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus1));

    dev_tca = add_dev(bus0, ADDR_TCA9554);
    dev_rtc = add_dev(bus0, ADDR_RTC);
    dev_imu = add_dev(bus0, ADDR_IMU);
    dev_touch = add_dev(bus1, ADDR_TOUCH);

    tca_init();
    imu_init();

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    // Batteria: ADC1 canale 3 (GPIO4), partitore 1/3
    adc_oneshot_unit_init_cfg_t ac = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ac, &adc));
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    adc_oneshot_config_channel(adc, ADC_CHANNEL_3, &ch);
    adc_cali_curve_fitting_config_t cc = {
        .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cc, &adc_cali) != ESP_OK) adc_cali = NULL;
}

i2c_master_dev_handle_t board_touch_dev(void) { return dev_touch; }
i2c_master_bus_handle_t board_i2c0(void) { return bus0; }

void board_power_off(void)
{
    uint8_t out = 0xFF;
    reg_read(dev_tca, 0x01, &out, 1);
    out &= ~(1 << EXIO_POWER_HOLD);
    reg_write(dev_tca, 0x01, out);
}

float board_battery_volts(void)
{
    int raw = 0, mv = 0;
    if (adc_oneshot_read(adc, ADC_CHANNEL_3, &raw) != ESP_OK) return 0;
    if (adc_cali) adc_cali_raw_to_voltage(adc_cali, raw, &mv);
    else mv = raw * 3300 / 4095;
    return mv * 3.0f / 1000.0f;
}

int board_battery_percent(float v)
{
    // curva approssimata per una Li-ion a vuoto
    static const float tab[][2] = {
        {4.15f, 100}, {4.05f, 90}, {3.95f, 78}, {3.85f, 62}, {3.78f, 50},
        {3.72f, 38}, {3.66f, 25}, {3.58f, 12}, {3.45f, 4}, {3.30f, 0},
    };
    if (v >= tab[0][0]) return 100;
    for (int i = 1; i < 10; i++) {
        if (v >= tab[i][0]) {
            float t = (v - tab[i][0]) / (tab[i - 1][0] - tab[i][0]);
            return (int)(tab[i][1] + t * (tab[i - 1][1] - tab[i][1]));
        }
    }
    return 0;
}

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static int rtc_boot = -1;

rtc_status_t board_rtc_boot_status(void) { return rtc_boot < 0 ? RTC_NO_CHIP : (rtc_status_t)rtc_boot; }

static rtc_status_t rtc_get(struct tm *t)
{
    uint8_t r[7];
    if (reg_read(dev_rtc, 0x04, r, 7) != ESP_OK) return RTC_NO_CHIP;
    if (r[0] & 0x80) return RTC_STOPPED; // flag OS: oscillatore fermato (senza corrente), orario non affidabile
    memset(t, 0, sizeof(*t));
    t->tm_sec = bcd2bin(r[0] & 0x7F);
    t->tm_min = bcd2bin(r[1] & 0x7F);
    t->tm_hour = bcd2bin(r[2] & 0x3F);
    t->tm_mday = bcd2bin(r[3] & 0x3F);
    t->tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(r[6]) + 100;
    return t->tm_year >= 124 ? RTC_OK : RTC_UNSET; // almeno 2024
}

rtc_status_t board_rtc_get(struct tm *t)
{
    rtc_status_t s = rtc_get(t);
    if (rtc_boot < 0) rtc_boot = s;
    return s;
}

bool board_rtc_read(struct tm *t) { return board_rtc_get(t) == RTC_OK; }

bool board_rtc_write(const struct tm *t)
{
    uint8_t b[8] = {
        0x04,
        bin2bcd(t->tm_sec), bin2bcd(t->tm_min), bin2bcd(t->tm_hour),
        bin2bcd(t->tm_mday), (uint8_t)t->tm_wday, bin2bcd(t->tm_mon + 1),
        bin2bcd((t->tm_year - 100) % 100),
    };
    for (int tries = 0; tries < 3; tries++) {
        if (i2c_master_transmit(dev_rtc, b, sizeof(b), 50) != ESP_OK) continue;
        struct tm r;
        if (rtc_get(&r) != RTC_OK) continue;
        // rilettura: stessa data e al massimo un paio di secondi di differenza
        int d = (r.tm_hour * 3600 + r.tm_min * 60 + r.tm_sec) - (t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
        if (r.tm_mday == t->tm_mday && r.tm_mon == t->tm_mon && r.tm_year == t->tm_year && d >= 0 && d <= 2) return true;
    }
    return false;
}

bool board_imu_ok(void) { return imu_ok; }

bool board_imu_accel(vec3_t *g)
{
    if (!imu_ok) return false;
    uint8_t r[6];
    if (reg_read(dev_imu, 0x35, r, 6) != ESP_OK) return false;
    const float k = 1.0f / 8192.0f; // ±4 g
    g->x = (int16_t)(r[0] | r[1] << 8) * k;
    g->y = (int16_t)(r[2] | r[3] << 8) * k;
    g->z = (int16_t)(r[4] | r[5] << 8) * k;
    return true;
}

void board_imu_gyro_enable(bool on)
{
    if (!imu_ok) return;
    reg_write(dev_imu, 0x04, 0x65);            // CTRL3: ±1024 °/s, ODR ~ 470 Hz
    reg_write(dev_imu, 0x08, on ? 0x03 : 0x01); // CTRL7: accelerometro (+ giroscopio)
}

bool board_imu_read6(vec3_t *g, vec3_t *w)
{
    if (!imu_ok) return false;
    uint8_t r[12];
    if (reg_read(dev_imu, 0x35, r, 12) != ESP_OK) return false;
    const float ka = 1.0f / 8192.0f, kg = 1.0f / 32.0f;
    g->x = (int16_t)(r[0] | r[1] << 8) * ka;
    g->y = (int16_t)(r[2] | r[3] << 8) * ka;
    g->z = (int16_t)(r[4] | r[5] << 8) * ka;
    w->x = (int16_t)(r[6] | r[7] << 8) * kg;
    w->y = (int16_t)(r[8] | r[9] << 8) * kg;
    w->z = (int16_t)(r[10] | r[11] << 8) * kg;
    return true;
}

bool board_btn_boot(void) { return gpio_get_level(PIN_BTN_BOOT) == 0; }
bool board_btn_pwr(void) { return gpio_get_level(PIN_BTN_PWR) == 0; }
