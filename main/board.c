// board.c — inizializzazione hardware di base
#include "board.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t bus0, bus1;
static i2c_master_dev_handle_t dev_tca, dev_rtc, dev_imu, dev_touch, dev_axp;
static board_kind_t kind = BOARD_LCD349;

board_kind_t board_kind(void) { return kind; }
static bool lcd_v2;
bool board_lcd_v2(void) { return lcd_v2; }
const char *board_name(void)
{
    if (kind == BOARD_AMOLED175) return "ESP32-S3-Touch-AMOLED-1.75";
    return lcd_v2 ? "ESP32-S3-Touch-LCD-3.49 V2" : "ESP32-S3-Touch-LCD-3.49 V1";
}
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

// Uscita del TCA9554 (e la rende uscita): per la V2, reset e accensione della retroilluminazione
static void tca_out(int bit, bool level)
{
    uint8_t out = 0xFF, cfg = 0xFF;
    if (reg_read(dev_tca, 0x01, &out, 1) != ESP_OK || reg_read(dev_tca, 0x03, &cfg, 1) != ESP_OK) return;
    out = level ? out | (1 << bit) : out & ~(1 << bit);
    reg_write(dev_tca, 0x01, out);
    reg_write(dev_tca, 0x03, cfg & ~(1 << bit));
}
void board_lcd_v2_reset(bool level) { if (lcd_v2) tca_out(EXIO_V2_LCD_RST, level); }
void board_lcd_v2_bl_en(bool on)
{
    static int8_t cur = -1;   // si scrive solo quando cambia (la luminosità si regola spesso)
    if (!lcd_v2 || cur == on) return;
    cur = on;
    tca_out(EXIO_V2_BL_EN, on);
}

// V1 o V2? Sulla V2 il GPIO8 è l'INT del TCA9554 (open drain con pull-up da 10K, alto a riposo)
// e il GPIO42 va alla retroilluminazione (10K verso il nodo di retroazione del convertitore,
// pochi decimi di volt); sulla V1 i due pin sono scambiati. Si leggono solo, con il pull-down
// interno, prima di pilotare qualsiasi cosa: un pin pilotato contro l'INT del TCA sarebbe un corto.
static void detect_lcd_rev(void)
{
    gpio_config_t io = {.pin_bit_mask = (1ULL << PIN_LCD_BL) | (1ULL << PIN_LCD_BL_V2),
                        .mode = GPIO_MODE_INPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE};
    gpio_config(&io);
    int hi8 = 0, hi42 = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t in;
        reg_read(dev_tca, 0x00, &in, 1);   // la lettura degli ingressi rilascia l'INT
        esp_rom_delay_us(300);
        hi8 += gpio_get_level(PIN_LCD_BL);
        hi42 += gpio_get_level(PIN_LCD_BL_V2);
    }
    lcd_v2 = hi8 >= 6 && hi42 <= 1;
    ESP_LOGI(TAG, "3.49: GPIO8 alto %d/8, GPIO42 alto %d/8 -> %s", hi8, hi42, lcd_v2 ? "V2" : "V1");
    if (lcd_v2) {
        // GPIO8 resta un ingresso (senza pull-down); retroilluminazione spenta finché il
        // primo frame non è pronto, display tenuto fuori dal reset
        gpio_set_pull_mode(PIN_LCD_BL, GPIO_FLOATING);
        board_lcd_v2_bl_en(false);
        tca_out(EXIO_V2_LCD_RST, true);
    }
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

static i2c_master_bus_handle_t new_bus(int port, int sda, int scl)
{
    i2c_master_bus_config_t bc = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .scl_io_num = scl,
        .sda_io_num = sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t b = NULL;
    if (i2c_new_master_bus(&bc, &b) != ESP_OK) return NULL;
    return b;
}

// Bus bloccato: se la scheda si è riavviata (aggiornamento, riavvio software) a metà di una
// lettura, il dispositivo può essere rimasto a tenere SDA bassa aspettando altri colpi di
// clock, e nessuna transazione passa più finché non si toglie la corrente. Qualche impulso
// su SCL (al massimo 9) gli fa finire il byte; poi uno STOP libera il bus.
static void bus_unstick(int sda, int scl)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(sda)) return;   // libero
    int n = 0;
    while (!gpio_get_level(sda) && n < 9) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
        n++;
    }
    // STOP: SDA sale mentre SCL è alta
    gpio_set_level(scl, 0);
    esp_rom_delay_us(10);
    gpio_set_level(sda, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(10);
    ESP_LOGW(TAG, "bus I2C (SDA %d) bloccato: liberato con %d impulsi%s", sda, n, gpio_get_level(sda) ? "" : ", ma SDA resta bassa");
}

static i2c_master_bus_handle_t touch_bus_new(void)
{
    bus_unstick(PIN_I2C1_SDA, PIN_I2C1_SCL);
    i2c_master_bus_handle_t b = new_bus(I2C_NUM_1, PIN_I2C1_SDA, PIN_I2C1_SCL);
    if (!b) ESP_LOGE(TAG, "bus del touch non creato");
    return b;
}

static int touch_recoveries;
int board_touch_recoveries(void) { return touch_recoveries; }

bool board_touch_recover(void)
{
    touch_recoveries++;
    if (kind == BOARD_AMOLED175) {
        // il touch condivide il bus con gli altri: si azzera il bus e si resetta il CST9217
        i2c_master_bus_reset(bus0);
        gpio_set_level(R_PIN_TP_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(R_PIN_TP_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_LOGW(TAG, "touch: bus azzerato e controller resettato (%d)", touch_recoveries);
        return true;
    }
    // 3.49: il touch ha un bus tutto suo, si ricrea da capo (dopo averlo sbloccato)
    if (dev_touch) i2c_master_bus_rm_device(dev_touch);
    dev_touch = NULL;
    if (bus1) i2c_del_master_bus(bus1);
    bus1 = touch_bus_new();
    if (!bus1) return false;
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR_TOUCH, .scl_speed_hz = 300000};
    if (i2c_master_bus_add_device(bus1, &cfg, &dev_touch) != ESP_OK) { dev_touch = NULL; return false; }
    ESP_LOGW(TAG, "touch: bus ricreato (%d)", touch_recoveries);
    return true;
}

/* ---------------- AMOLED 1.75: AXP2101 ---------------- */

static uint8_t axp_get(uint8_t reg)
{
    uint8_t v = 0;
    reg_read(dev_axp, reg, &v, 1);
    return v;
}

static void axp_init(void)
{
    // misura della tensione della batteria (ADC) e misuratore di carica accesi
    reg_write(dev_axp, 0x30, axp_get(0x30) | 0x01);
    reg_write(dev_axp, 0x18, axp_get(0x18) | 0x08);
    // tasto PWR tenuto: lo spegnimento "di forza" dell'AXP solo dopo 10 s
    // (a 2 s spegne il firmware, con la sua schermata)
    reg_write(dev_axp, 0x27, axp_get(0x27) | 0x0C);
}

static void amoled_init(void)
{
    dev_axp = add_dev(bus0, R_ADDR_AXP2101);
    dev_tca = add_dev(bus0, ADDR_TCA9554);
    dev_rtc = add_dev(bus0, ADDR_RTC);
    dev_imu = add_dev(bus0, ADDR_IMU);
    dev_touch = add_dev(bus0, R_ADDR_TOUCH);
    axp_init();
    // TCA9554: tutto in ingresso tranne il reset del GPS (versione -G), tenuto alto
    uint8_t out = 0xFF, cfg = 0xFF;
    reg_read(dev_tca, 0x01, &out, 1);
    reg_read(dev_tca, 0x03, &cfg, 1);
    out |= 1 << R_EXIO_GPS_RST;
    cfg &= ~(1 << R_EXIO_GPS_RST);
    if (reg_write(dev_tca, 0x01, out) != ESP_OK || reg_write(dev_tca, 0x03, cfg) != ESP_OK)
        ESP_LOGW(TAG, "TCA9554 non risponde");
    // reset del touch (il display ha il suo, in display.c)
    gpio_config_t rst = {.pin_bit_mask = 1ULL << R_PIN_TP_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(R_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(R_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    imu_init();
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);
}

void board_init(void)
{
    // Quale scheda? Prima la 3.49 (TCA9554 sul bus 47/48: sull'AMOLED quei pin non sono
    // collegati) e subito, perché a batteria il suo latch va tenuto acceso al più presto.
    // Poi l'AMOLED: AXP2101 (id 0x4A) sul bus 15/14. Sulla 3.49 i pin 14 e 15 sono del
    // display e dell'audio, quindi non si tocca nulla lì se la 3.49 risponde.
    bus0 = new_bus(I2C_NUM_0, PIN_I2C0_SDA, PIN_I2C0_SCL);
    if (bus0 && i2c_master_probe(bus0, ADDR_TCA9554, 50) == ESP_OK) {
        kind = BOARD_LCD349;
    } else {
        if (bus0) i2c_del_master_bus(bus0);
        bus0 = new_bus(I2C_NUM_0, R_PIN_I2C_SDA, R_PIN_I2C_SCL);
        uint8_t id = 0, reg = 0x03;
        i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = R_ADDR_AXP2101, .scl_speed_hz = 300000};
        i2c_master_dev_handle_t d = NULL;
        if (bus0 && i2c_master_probe(bus0, R_ADDR_AXP2101, 50) == ESP_OK &&
            i2c_master_bus_add_device(bus0, &dc, &d) == ESP_OK &&
            i2c_master_transmit_receive(d, &reg, 1, &id, 1, 50) == ESP_OK && id == 0x4A) {
            kind = BOARD_AMOLED175;
        } else {
            // né l'una né l'altra: si parte come 3.49 (la scheda di sempre)
            ESP_LOGE(TAG, "scheda non riconosciuta (AXP2101 id=0x%02x)", id);
            if (bus0) i2c_del_master_bus(bus0);
            bus0 = new_bus(I2C_NUM_0, PIN_I2C0_SDA, PIN_I2C0_SCL);
        }
        if (d) i2c_master_bus_rm_device(d);
    }
    if (kind == BOARD_AMOLED175) { ESP_LOGI(TAG, "scheda: %s", board_name()); amoled_init(); return; }

    bus1 = touch_bus_new();

    dev_tca = add_dev(bus0, ADDR_TCA9554);
    dev_rtc = add_dev(bus0, ADDR_RTC);
    dev_imu = add_dev(bus0, ADDR_IMU);
    if (bus1) dev_touch = add_dev(bus1, ADDR_TOUCH);

    tca_init();
    detect_lcd_rev();
    ESP_LOGI(TAG, "scheda: %s", board_name());
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
    if (kind == BOARD_AMOLED175) {   // AXP2101: bit 0 del registro 0x10 spegne tutto
        reg_write(dev_axp, 0x10, axp_get(0x10) | 0x01);
        return;
    }
    uint8_t out = 0xFF;
    reg_read(dev_tca, 0x01, &out, 1);
    out &= ~(1 << EXIO_POWER_HOLD);
    reg_write(dev_tca, 0x01, out);
}

bool board_charging(void)
{
    if (kind != BOARD_AMOLED175) return false;
    return ((axp_get(0x01) >> 5) & 0x03) == 0x01;   // direzione della corrente: in carica
}

float board_battery_volts(void)
{
    if (kind == BOARD_AMOLED175) {
        if (!(axp_get(0x00) & 0x08)) return 0;   // batteria assente
        uint8_t r[2] = {0};
        if (reg_read(dev_axp, 0x34, r, 2) != ESP_OK) return 0;
        return (((r[0] & 0x3F) << 8) | r[1]) / 1000.0f;
    }
    int raw = 0, mv = 0;
    if (adc_oneshot_read(adc, ADC_CHANNEL_3, &raw) != ESP_OK) return 0;
    if (adc_cali) adc_cali_raw_to_voltage(adc_cali, raw, &mv);
    else mv = raw * 3300 / 4095;
    return mv * 3.0f / 1000.0f;
}

int board_battery_percent(float v)
{
    if (kind == BOARD_AMOLED175) {   // il misuratore dell'AXP2101 è più preciso della curva
        uint8_t p = axp_get(0xA4);
        if (p <= 100) return p;
    }
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
bool board_btn_pwr(void)
{
    if (kind == BOARD_AMOLED175) {   // dal TCA9554 (ingresso 4 alto = premuto)
        uint8_t in = 0;
        if (reg_read(dev_tca, 0x00, &in, 1) != ESP_OK) return false;
        return (in >> R_EXIO_PWR) & 1;
    }
    return gpio_get_level(PIN_BTN_PWR) == 0;
}
