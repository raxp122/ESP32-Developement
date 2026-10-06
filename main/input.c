// input.c — polling del touch AXS15231B, gesti e pulsanti
#include "input.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include <stdlib.h>
#include "lvgl.h"
#include "esp_timer.h"

#define POLL_MS        12
#define TAP_MAX_MOVE   18    // px
#define SWIPE_MIN      30    // px
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

static bool touch_read_raw(int *px, int *py)
{
    static const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buf[14] = {0};
    if (i2c_master_transmit_receive(board_touch_dev(), cmd, sizeof(cmd), buf, sizeof(buf), 20) != ESP_OK)
        return false;
    if (buf[1] == 0 || buf[1] > 4) return false;
    int rx = ((buf[2] & 0x0F) << 8) | buf[3];
    int ry = ((buf[4] & 0x0F) << 8) | buf[5];
    if (rx > LCD_H - 1) rx = LCD_H - 1;
    if (ry > LCD_W - 1) ry = LCD_W - 1;
    // coordinate verticali native (172×640), stessa mappatura del firmware di fabbrica
    *px = ry;
    *py = LCD_H - rx;
    return true;
}

bool input_touch(int *x, int *y)
{
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
