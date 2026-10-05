// app_bercio.c — Berciometro: livello sonoro dal microfono, grafico in tempo reale e record.
// BOOT o swipe a destra azzerano il record; su/giù regolano la calibrazione.
//
// Misura: RMS su blocchi da 50 ms con media esponenziale "Fast" (125 ms), come i fonometri.
// I dB sono stimati: dBFS + una costante di calibrazione (sensibilità del microfono e
// guadagno dell'ADC non sono documentati). Ottimo per confronti, non è uno strumento certificato.
#include "apps.h"
#include "audio.h"
#include "settings.h"
#include <math.h>
#include "esp_attr.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLOCK_FRAMES (AUDIO_RATE / 20)  // 50 ms
#define CHART_PTS    160
#define DB_MIN       30
#define DB_MAX       125

static volatile float level_db = 0, block_peak_db;
static volatile uint32_t level_seq;
static volatile bool clipping, run;
static TaskHandle_t task_h;
static float record_db;
static lv_obj_t *l_now, *l_unit, *l_rec, *l_rec_t, *rec_box, *chart, *l_info, *l_clip;
static lv_chart_series_t *ser;
static lv_timer_t *ui_timer;
static uint32_t seen_seq, rec_flash_until;

static float cal(void) { return 122.0f + g_set.bercio_cal * 0.5f; }

static void mic_task(void *arg)
{
    EXT_RAM_BSS_ATTR static int16_t buf[BLOCK_FRAMES * 2];   // in PSRAM: la RAM interna serve a Doom
    float ms_fast = 1e-10f, dc = 0;
    const float a = 1.0f - expf(-0.05f / 0.125f); // costante "Fast" su blocchi da 50 ms
    while (run) {
        int n = audio_mic_read(buf, BLOCK_FRAMES, 200);
        if (n <= 0) continue;
        double sum = 0;
        bool clip = false;
        for (int i = 0; i < n; i++) {
            float s = 0.5f * ((float)buf[2 * i] + (float)buf[2 * i + 1]);  // media dei due microfoni
            if (buf[2 * i] > 32000 || buf[2 * i] < -32000) clip = true;
            dc += (s - dc) * 0.001f;                                      // toglie la componente continua
            float x = (s - dc) / 32768.0f;
            sum += x * x;
        }
        float ms = (float)(sum / n);
        ms_fast += (ms - ms_fast) * a;
        level_db = 10.0f * log10f(ms_fast + 1e-12f) + cal();
        block_peak_db = 10.0f * log10f(ms + 1e-12f) + cal();
        clipping = clip;
        level_seq++;
    }
    task_h = NULL;
    vTaskDelete(NULL);
}

static void show_record(void)
{
    if (record_db <= 0) lv_label_set_text(l_rec, "--.----");
    else lv_label_set_text_fmt(l_rec, "%.4f", record_db);
}

static void ui_cb(lv_timer_t *t)
{
    if (level_seq == seen_seq) return;
    seen_seq = level_seq;
    float db = level_db;
    if (db < 0) db = 0;
    lv_label_set_text_fmt(l_now, "%.4f", db);
    lv_chart_set_next_value(chart, ser, (int32_t)(db * 10));

    if (db > record_db) {
        record_db = db;
        show_record();
        rec_flash_until = lv_tick_get() + 300;
    }
    bool flash = lv_tick_get() < rec_flash_until;
    lv_obj_set_style_border_color(rec_box, flash ? ui_accent() : C_FAINT, 0);
    lv_obj_set_style_bg_color(rec_box, flash ? lv_color_mix(ui_accent(), C_BG, 60) : C_BG, 0);
    if (clipping) lv_obj_clear_flag(l_clip, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(l_clip, LV_OBJ_FLAG_HIDDEN);
    input_mark_activity();
}

static void show_info(void)
{
    lv_label_set_text_fmt(l_info, "BOOT o swipe " LV_SYMBOL_RIGHT " azzera il record  ·  " LV_SYMBOL_UP LV_SYMBOL_DOWN " calibrazione %+.1f dB",
                          g_set.bercio_cal * 0.5f);
}

static lv_obj_t *mk(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static void enter(lv_obj_t *root, void *arg)
{
    l_now = mk(root, &font_xl, C_TEXT);
    lv_obj_set_pos(l_now, 14, -6);
    lv_label_set_text(l_now, "--.----");
    l_unit = mk(root, &font_m, C_DIM);
    lv_label_set_text(l_unit, "dB");
    lv_obj_set_pos(l_unit, 330, 46);

    l_clip = mk(root, &font_s, C_WARN);
    lv_label_set_text(l_clip, "SATURO");
    lv_obj_set_pos(l_clip, 330, 14);
    lv_obj_add_flag(l_clip, LV_OBJ_FLAG_HIDDEN);

    // riquadro del record
    rec_box = lv_obj_create(root);
    lv_obj_remove_style_all(rec_box);
    lv_obj_set_size(rec_box, 230, 70);
    lv_obj_set_pos(rec_box, SCR_W - 244, 4);
    lv_obj_set_style_radius(rec_box, 10, 0);
    lv_obj_set_style_border_width(rec_box, 2, 0);
    lv_obj_set_style_border_color(rec_box, C_FAINT, 0);
    lv_obj_set_style_bg_opa(rec_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rec_box, C_BG, 0);
    l_rec_t = mk(rec_box, &font_s, ui_accent());
    lv_label_set_text(l_rec_t, ICON_TROPHY "  RECORD (dB)");
    lv_obj_set_pos(l_rec_t, 12, 6);
    l_rec = mk(rec_box, &font_l, C_TEXT);
    lv_obj_set_pos(l_rec, 12, 24);
    show_record();

    // grafico
    chart = lv_chart_create(root);
    lv_obj_set_size(chart, SCR_W - 28, 50);
    lv_obj_set_pos(chart, 14, 78);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_PTS);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, DB_MIN * 10, DB_MAX * 10);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, C_FAINT, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    ser = lv_chart_add_series(chart, ui_accent(), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ser, LV_CHART_POINT_NONE);

    l_info = mk(root, &font_s, C_DIM);
    lv_obj_set_pos(l_info, 14, CONTENT_H - 18);
    show_info();

    if (!audio_init() || !audio_mic_start()) {
        lv_label_set_text(l_now, "MIC?");
        ui_toast("Microfono non disponibile");
        return;
    }
    run = true;
    seen_seq = level_seq;
    xTaskCreatePinnedToCore(mic_task, "bercio", 4096, NULL, 5, &task_h, 0);
    ui_timer = lv_timer_create(ui_cb, 40, NULL);
}

static void leave(void)
{
    run = false;
    for (int i = 0; i < 40 && task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    audio_mic_stop();
    if (ui_timer) { lv_timer_delete(ui_timer); ui_timer = NULL; }
}

static bool nav(nav_t ev)
{
    switch (ev) {
    case NAV_BTN:
    case NAV_SELECT:
        record_db = 0;
        show_record();
        ui_toast("Record azzerato: via!");
        return true;
    case NAV_NEXT:
    case NAV_PREV:
        g_set.bercio_cal += ev == NAV_NEXT ? 1 : -1;
        if (g_set.bercio_cal > 40) g_set.bercio_cal = 40;
        if (g_set.bercio_cal < -40) g_set.bercio_cal = -40;
        settings_save();
        show_info();
        return true;
    default:
        return false;
    }
}

const app_t app_bercio = {
    .name = "Berciometro", .icon = ICON_MIC,
    .enter = enter, .leave = leave, .nav = nav, .flags = APP_NO_SLEEP,
};
