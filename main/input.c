// input.c — polling del touch AXS15231B, gesti e pulsanti
#include "input.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "input";

#define POLL_MS        12
// in pixel della 3.49; sullo schermo tondo i pixel sono più piccoli (circa 1,4 volte)
#define TAP_MAX_MOVE   (SCR_ROUND ? 25 : 18)
#define SWIPE_MIN      (SCR_ROUND ? 42 : 30)
#define HOLD_MS        1000
// Il controller a volte "perde" il dito per qualche lettura durante uno swipe lento:
// consideriamo il dito sollevato solo dopo ~85 ms di letture vuote consecutive.
#define RELEASE_POLLS  7

static nav_handler_t handler;
static int64_t last_activity_us;

static struct {
    bool down, moved, hold_fired;
    int sx, sy, lx, ly;
    int64_t t0;
    int release_cnt;
} g;

static struct { bool down; int64_t t0; bool long_fired; } btn_boot, btn_pwr;

// Letture del touch fallite di fila (errore I2C, non "nessun dito"): se il controller o il
// driver restano incastrati (succedeva all'avvio, soprattutto dopo un riavvio software) si
// ricrea il bus invece di lasciare il touch morto fino allo spegnimento
#define TOUCH_ERR_RECOVER  25        // ~300 ms
#define TOUCH_RECOVER_GAP  3000000   // µs fra un recupero e l'altro
static int touch_err_run;
static int64_t touch_recover_us;
// diagnostica (comando I dal seriale): quante letture con il dito, senza, con dati non
// validi e con errore I2C, e gli ultimi byte grezzi non validi
static uint32_t st_touch, st_none, st_bad, st_err;
static uint8_t st_raw[8];

static bool touch_i2c_ok(esp_err_t r)
{
    if (r == ESP_OK) { touch_err_run = 0; return true; }
    st_err++;
    if (touch_err_run++ == 0) ESP_LOGW(TAG, "touch: lettura I2C fallita (%s)", esp_err_to_name(r));
    return false;
}

static bool touch_read_raw(int *px, int *py)
{
    static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buf[14] = {0};
    if (!touch_i2c_ok(i2c_master_transmit_receive(board_touch_dev(), cmd, sizeof(cmd), buf, sizeof(buf), 20)))
        return false;
    if (buf[1] == 0) { st_none++; return false; }
    if (buf[1] > 4) { st_bad++; memcpy(st_raw, buf, sizeof(st_raw)); return false; }
    st_touch++;
    int rx = ((buf[2] & 0x0F) << 8) | buf[3];
    int ry = ((buf[4] & 0x0F) << 8) | buf[5];
    if (rx > LCD_H - 1) rx = LCD_H - 1;
    if (ry > LCD_W - 1) ry = LCD_W - 1;
    // coordinate verticali native (172×640), stessa mappatura del firmware di fabbrica
    *px = ry;
    *py = LCD_H - rx;
    return true;
}

// CST9217 (AMOLED 1.75): comando 0xD000, poi conferma 0xAB. Un dito: stato 0x06 nei 4 bit
// bassi del primo byte, coordinate a 12 bit. Il pannello è montato girato: si specchiano
// tutte e due le coordinate (come nel BSP Waveshare).
static bool touch_read_round(int *x, int *y)
{
    static const uint8_t rd[2] = {0xD0, 0x00}, ack[3] = {0xD0, 0x00, 0xAB};
    uint8_t b[15] = {0};
    if (!touch_i2c_ok(i2c_master_transmit_receive(board_touch_dev(), rd, 2, b, sizeof(b), 20))) return false;
    i2c_master_transmit(board_touch_dev(), ack, 3, 20);
    if (b[6] != 0xAB) { st_bad++; memcpy(st_raw, b, sizeof(st_raw)); return false; }
    int n = b[5] & 0x7F;
    if (n == 0 || n > 2 || (b[0] & 0x0F) != 0x06) { st_none++; return false; }
    st_touch++;
    int rx = (b[1] << 4) | (b[3] >> 4);
    int ry = (b[2] << 4) | (b[3] & 0x0F);
    if (rx > R_LCD_W - 1) rx = R_LCD_W - 1;
    if (ry > R_LCD_H - 1) ry = R_LCD_H - 1;
    *x = R_LCD_W - 1 - rx;
    *y = R_LCD_H - 1 - ry;
    return true;
}

static volatile bool locked;
void input_set_locked(bool l) { locked = l; }
bool input_locked(void) { return locked; }

bool input_touch(int *x, int *y)
{
    if (locked) return false;
    if (BOARD_IS_ROUND()) return touch_read_round(x, y);
    int px, py;
    if (!touch_read_raw(&px, &py)) return false;
    // stessa trasformazione che LVGL applica con la rotazione 90°/270°
    if (display_is_flipped()) { px = LCD_W - px - 1; py = LCD_H - py - 1; }
    *x = LCD_H - py - 1;
    *y = px;
    return true;
}

uint32_t input_idle_ms(void) { return (uint32_t)((esp_timer_get_time() - last_activity_us) / 1000); }
void input_mark_activity(void) { last_activity_us = esp_timer_get_time(); }

static void emit(nav_t e)
{
    input_mark_activity();
    if (handler) handler(e);
}

static void process_touch(int64_t now)
{
    int x, y;
    bool pressed = input_touch(&x, &y);
    if (pressed) {
        g.release_cnt = 0;
        if (!g.down) {
            g = (typeof(g)){.down = true, .sx = x, .sy = y, .lx = x, .ly = y, .t0 = now};
            emit(NAV_TOUCH_DOWN);
            return;
        }
        g.lx = x; g.ly = y;
        if (abs(x - g.sx) > TAP_MAX_MOVE || abs(y - g.sy) > TAP_MAX_MOVE) g.moved = true;
        if (!g.moved && !g.hold_fired && (now - g.t0) / 1000 > HOLD_MS) {
            g.hold_fired = true;
            emit(NAV_HOLD);
        }
        return;
    }
    if (!g.down) return;
    if (++g.release_cnt < RELEASE_POLLS) return;
    g.down = false;
    if (g.hold_fired) return;

    int dx = g.lx - g.sx, dy = g.ly - g.sy;
    if (!g.moved) { emit(NAV_TAP); return; }
    if (abs(dy) > abs(dx)) {
        if (abs(dy) < SWIPE_MIN) return;
        bool up = dy < 0;
        if (g_set.invert_scroll) up = !up;
        emit(up ? NAV_NEXT : NAV_PREV);
    } else {
        if (abs(dx) < SWIPE_MIN) return;
        emit(dx < 0 ? NAV_BACK : NAV_SELECT);
    }
}

static void process_btn(bool pressed, int64_t now, typeof(btn_boot) *b, nav_t click, nav_t lng, int long_ms)
{
    if (pressed && !b->down) { b->down = true; b->t0 = now; b->long_fired = false; return; }
    if (pressed && b->down && !b->long_fired && (now - b->t0) / 1000 > long_ms) {
        b->long_fired = true;
        emit(lng);
        return;
    }
    if (!pressed && b->down) {
        b->down = false;
        if (!b->long_fired && (now - b->t0) / 1000 > 30) emit(click);
    }
}

static void poll_cb(lv_timer_t *t)
{
    int64_t now = esp_timer_get_time();
    if (touch_err_run >= TOUCH_ERR_RECOVER && now - touch_recover_us > TOUCH_RECOVER_GAP) {
        touch_recover_us = now;
        touch_err_run = 0;
        board_touch_recover();
    }
    process_touch(now);
    process_btn(board_btn_boot(), now, &btn_boot, NAV_BTN, NAV_QUICK, 800);
    process_btn(board_btn_pwr(), now, &btn_pwr, NAV_PWR_CLICK, NAV_PWR_LONG, 2000);
}

void input_init(nav_handler_t h)
{
    handler = h;
    input_mark_activity();
    // Se la scheda è stata accesa tenendo premuto PWR, ignora quella pressione
    btn_pwr.down = board_btn_pwr();
    btn_pwr.long_fired = true;
    lv_timer_create(poll_cb, POLL_MS, NULL);
}

void input_touch_stats(char *b, int n)
{
    snprintf(b, n, "touch: %lu con il dito, %lu senza, %lu non valide, %lu errori I2C · ultimi byte non validi "
             "%02X %02X %02X %02X %02X %02X %02X %02X%s",
             (unsigned long)st_touch, (unsigned long)st_none, (unsigned long)st_bad, (unsigned long)st_err,
             st_raw[0], st_raw[1], st_raw[2], st_raw[3], st_raw[4], st_raw[5], st_raw[6], st_raw[7],
             locked ? " · BLOCCATO (schermo spento col tasto)" : "");
}
