// display_round.c — AMOLED tondo 466×466 della ESP32-S3-Touch-AMOLED-1.75 (controller CO5300
// in QSPI). Sequenza di avvio, scostamento di 6 colonne e luminosità come nel BSP Waveshare.
//
// A differenza del pannello della 3.49 il CO5300 accetta finestre parziali (CASET/RASET):
// LVGL disegna in un frame intero in PSRAM (modalità DIRECT) e qui si inviano solo le zone
// cambiate, allineate a coordinate pari come vuole il controller.
#include "display_round.h"
#include "board.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

#define HOST        SPI3_HOST
#define W           R_LCD_W
#define H           R_LCD_H
#define CHUNK_ROWS  24
#define CHUNK_BYTES (W * CHUNK_ROWS * 2)

static const char *TAG = "amoled";
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done_sem;
static uint16_t *dma_buf;

typedef struct { uint8_t cmd; uint8_t data[4]; uint8_t len; uint16_t delay_ms; } init_cmd_t;
static const init_cmd_t init_cmds[] = {
    {0xFE, {0x20}, 1, 0},
    {0x19, {0x10}, 1, 0},
    {0x1C, {0xA0}, 1, 0},
    {0xFE, {0x00}, 1, 0},
    {0xC4, {0x80}, 1, 0},
    {0x3A, {0x55}, 1, 0},          // RGB565
    {0x35, {0x00}, 1, 0},          // segnale TE
    {0x53, {0x20}, 1, 0},
    {0x51, {0x00}, 1, 0},          // luminosità 0: si accende dopo il primo frame
    {0x63, {0xFF}, 1, 0},
    {0x2A, {0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, {0}, 0, 600},           // fuori dallo sleep
    {0x29, {0}, 0, 0},             // display acceso
};

static esp_err_t cmd(uint8_t c, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(io, (0x02 << 24) | (c << 8), data, len);
}

static bool on_done(esp_lcd_panel_io_handle_t h, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done_sem, &woken);
    return woken == pdTRUE;
}

static void window(int x1, int y1, int x2, int y2)
{
    x1 += R_LCD_X_GAP;
    x2 += R_LCD_X_GAP;
    uint8_t c[4] = {x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF};
    uint8_t r[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    cmd(0x2A, c, 4);
    cmd(0x2B, r, 4);
}

// invia la zona (x1..x2, y1..y2) del frame di LVGL (RGB565 little-endian, larghezza W)
static void send_area(const uint8_t *frame, int x1, int y1, int x2, int y2)
{
    int w = x2 - x1 + 1;
    int rows = CHUNK_BYTES / (w * 2);
    if (rows < 1) rows = 1;
    for (int y = y1; y <= y2; y += rows) {
        int n = y + rows - 1 > y2 ? y2 - y + 1 : rows;
        // il trasferimento precedente deve aver finito prima di riscrivere il buffer DMA
        if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(200)) != pdTRUE) { xSemaphoreGive(done_sem); return; }
        uint16_t *d = dma_buf;
        for (int r = 0; r < n; r++) {
            const uint16_t *s = (const uint16_t *)(frame + ((y + r) * W + x1) * 2);
            for (int i = 0; i < w; i++) d[i] = (uint16_t)((s[i] << 8) | (s[i] >> 8));   // al pannello big-endian
            d += w;
        }
        window(x1, y, x2, y + n - 1);
        if (esp_lcd_panel_io_tx_color(io, (0x32 << 24) | (0x2C << 8), dma_buf, w * n * 2) != ESP_OK) {
            xSemaphoreGive(done_sem);
            return;
        }
    }
    // aspetta l'ultimo pezzo: LVGL può ridisegnare subito dopo flush_ready
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(200)) == pdTRUE) xSemaphoreGive(done_sem);
}

static volatile bool *dark_flag;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    if (!*dark_flag) send_area(px, a->x1, a->y1, a->x2, a->y2);
    lv_display_flush_ready(d);
}

// il CO5300 vuole finestre che partono da coordinate pari e finiscono su dispari
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
    if (a->x2 > W - 1) a->x2 = W - 1;
    if (a->y2 > H - 1) a->y2 = H - 1;
}

lv_display_t *display_round_init(volatile bool *dark)
{
    dark_flag = dark;
    done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(done_sem);

    gpio_config_t rst = {.pin_bit_mask = 1ULL << R_PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);

    spi_bus_config_t bus = {
        .sclk_io_num = R_PIN_LCD_PCLK,
        .data0_io_num = R_PIN_LCD_D0,
        .data1_io_num = R_PIN_LCD_D1,
        .data2_io_num = R_PIN_LCD_D2,
        .data3_io_num = R_PIN_LCD_D3,
        .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t ioc = {
        .cs_gpio_num = R_PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(HOST, &ioc, &io));

    gpio_set_level(R_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(R_PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(R_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));
    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        if (cmd(init_cmds[i].cmd, init_cmds[i].len ? init_cmds[i].data : NULL, init_cmds[i].len) != ESP_OK)
            ESP_LOGW(TAG, "comando 0x%02X non inviato", init_cmds[i].cmd);
        if (init_cmds[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    dma_buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *fb = heap_caps_calloc(1, W * H * 2, MALLOC_CAP_SPIRAM);
    assert(dma_buf && fb);

    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(disp, fb, NULL, W * H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    return disp;
}

void display_round_brightness(int pct)
{
    uint8_t v = (uint8_t)(pct * 255 / 100);
    cmd(0x51, &v, 1);
}
