// display.c — driver QSPI, rotazione software e task LVGL
#include "display.h"
#include "board.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_axs15231b.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LCD_HOST      SPI3_HOST
#define CHUNK_ROWS    64
#define CHUNK_BYTES   (LCD_W * CHUNK_ROWS * 2)
#define FRAME_BYTES   (LCD_W * LCD_H * 2)

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t flush_sem, lv_mux;
static uint16_t *dma_buf;
static uint8_t *rot_buf;
static lv_display_t *disp;
static bool is_flipped;
static volatile bool dark;            // retroilluminazione spenta: niente invii al pannello
static bool lv_task_on;               // c'è il task di LVGL (non in modalità Doom)
static int keep_y0 = -1, keep_y1 = -1; // righe fisiche che LVGL non deve sovrascrivere
static uint8_t *lv_tmp;                // frame ruotato di LVGL quando ci sono righe protette

static const axs15231b_lcd_init_cmd_t init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(flush_sem, &woken);
    return woken == pdTRUE;
}

static void push_frame(const uint16_t *src)
{
    // Il pannello in QSPI non accetta RASET: ogni frame va scritto intero partendo dalla riga 0
    // Attese con timeout: se un trasferimento fallisce l'interrupt di fine non arriva, e
    // un'attesa infinita (con il mutex di LVGL preso) bloccherebbe tutta l'interfaccia.
    xSemaphoreGive(flush_sem);
    for (int y = 0; y < LCD_H; y += CHUNK_ROWS) {
        if (xSemaphoreTake(flush_sem, pdMS_TO_TICKS(200)) != pdTRUE) return;
        memcpy(dma_buf, src, CHUNK_BYTES);
        if (esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_W, y + CHUNK_ROWS, dma_buf) != ESP_OK) return;
        src += LCD_W * CHUNK_ROWS;
    }
    xSemaphoreTake(flush_sem, pdMS_TO_TICKS(200));
}

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    // Modalità DIRECT: px è l'intero frame logico (640×172) e area è solo la zona
    // ridisegnata. Si ruota quella zona nel frame fisico (172×640), che resta in memoria:
    // il resto è già giusto. Il frame di LVGL non si tocca (la conversione dei byte si fa
    // sulla copia ruotata), perché LVGL ci disegna sopra al giro successivo.
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_area_t ra = *area;
    lv_display_rotate_area(d, &ra);
    const uint32_t src_stride = lv_draw_buf_width_to_stride(lv_display_get_horizontal_resolution(d), LV_COLOR_FORMAT_RGB565);
    const uint32_t dst_stride = LCD_W * 2;
    const uint8_t *src = px + area->y1 * src_stride + area->x1 * 2;
    uint8_t *base = keep_y0 >= 0 ? lv_tmp : rot_buf;
    lv_draw_sw_rotate(src, base + ra.y1 * dst_stride + ra.x1 * 2, w, h, src_stride, dst_stride,
                      lv_display_get_rotation(d), LV_COLOR_FORMAT_RGB565);
    int32_t rw = lv_area_get_width(&ra);
    for (int32_t y = ra.y1; y <= ra.y2; y++) {
        if (keep_y0 >= 0 && y >= keep_y0 && y <= keep_y1) continue;   // righe protette (es. Doom)
        uint8_t *row = rot_buf + y * dst_stride + ra.x1 * 2;
        if (keep_y0 >= 0) memcpy(row, lv_tmp + y * dst_stride + ra.x1 * 2, rw * 2);
        lv_draw_sw_rgb565_swap(row, rw);
    }
    // il pannello vuole sempre il frame intero: si invia una volta sola, all'ultima zona,
    // e mai a schermo spento (lo si manda alla riaccensione)
    if (lv_display_flush_is_last(d) && !dark) push_frame((uint16_t *)rot_buf);
    lv_display_flush_ready(d);
}

uint16_t *display_frame(void) { return (uint16_t *)rot_buf; }
void display_push(void) { push_frame((uint16_t *)rot_buf); }

void display_keep_rows(int y0, int y1)
{
    if (!lv_tmp) lv_tmp = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!lv_tmp) return;   // senza memoria niente righe protette (meglio che scrivere su NULL)
    keep_y0 = y0;
    keep_y1 = y1;
}

static void tick_cb(void *arg) { lv_tick_inc(2); }

static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_3,
        .freq_hz = 50000,
        .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_3,
        .duty = 255, // logica invertita: 255 = spento
    };
    ledc_channel_config(&c);
}

void display_set_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (dark && pct > 0 && rot_buf) {
        display_lock();
        dark = false;
        if (lv_task_on) {
            // a schermo spento LVGL era fermo: riparte e disegna subito il frame attuale
            // (orologio, stato…), che viene inviato prima di accendere la luce
            lv_timer_resume(lv_display_get_refr_timer(disp));
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(disp);
        } else {
            push_frame((uint16_t *)rot_buf);   // l'ultimo frame, non inviato mentre era spento
        }
        display_unlock();
    } else if (!dark && pct == 0 && lv_task_on) {
        // schermo spento: niente disegno né rotazione dei frame (prima si calcolavano tutti
        // e si scartava solo l'invio). Timer, tick e input delle app continuano a girare.
        display_lock();
        lv_timer_pause(lv_display_get_refr_timer(disp));
        display_unlock();
    }
    dark = pct == 0;
    uint32_t duty = 255 - (pct * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void display_init(bool flipped)
{
    backlight_init();
    flush_sem = xSemaphoreCreateBinary();
    lv_mux = xSemaphoreCreateRecursiveMutex();

    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT, .pull_up_en = 1};
    gpio_config(&rst);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_PCLK,
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t ioc = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &ioc, &io));

    axs15231b_vendor_config_t vc = {
        .init_cmds = init_cmds,
        .init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vc,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &pc, &panel));

    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(250));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    lv_init();
    disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    // La rotazione va impostata PRIMA dei buffer: LVGL calcola larghezza e stride
    // dei buffer dalla risoluzione già ruotata (640×172). Il passaggio 90°↔270°
    // non cambia la risoluzione, quindi dopo si può ruotare liberamente.
    display_set_flipped(flipped);
    // Un solo buffer in modalità DIRECT: LVGL ridisegna solo le zone cambiate e l'invio
    // al pannello è sincrono, quindi non serve un secondo buffer.
    uint8_t *b1 = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
    rot_buf = heap_caps_calloc(1, FRAME_BYTES, MALLOC_CAP_SPIRAM);
    dma_buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(b1 && rot_buf && dma_buf);
    lv_display_set_buffers(disp, b1, NULL, FRAME_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);

    const esp_timer_create_args_t ta = {.callback = tick_cb, .name = "lv_tick"};
    esp_timer_handle_t th;
    esp_timer_create(&ta, &th);
    esp_timer_start_periodic(th, 2000);
}

void display_set_flipped(bool flipped)
{
    is_flipped = flipped;
    lv_display_set_rotation(disp, flipped ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
}

bool display_is_flipped(void) { return is_flipped; }
bool display_is_dark(void) { return dark; }

void display_lock(void) { xSemaphoreTakeRecursive(lv_mux, portMAX_DELAY); }
void display_unlock(void) { xSemaphoreGiveRecursive(lv_mux); }

static void lvgl_task(void *arg)
{
    for (;;) {
        display_lock();
        uint32_t wait = lv_timer_handler();
        display_unlock();
        if (wait < 5) wait = 5;
        if (wait > 50) wait = 50;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void display_start_task(void)
{
    lv_task_on = true;
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12 * 1024, NULL, 4, NULL, 1);
}
