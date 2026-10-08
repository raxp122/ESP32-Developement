// audio.c
// 3.49: pin dal firmware di fabbrica Waveshare (MCLK 7, BCLK 15, WS 46, DOUT 45, DIN 6);
// l'amplificatore è abilitato da EXIO7 del TCA9554 (già alto all'avvio).
// AMOLED 1.75: MCLK 42, BCLK 9, WS 45, DOUT 8, DIN 10 (ES7210, MIC1/MIC2 su SDOUT1);
// l'amplificatore NS4150B si abilita con GPIO46.
#include "audio.h"
#include "board.h"
#include "settings.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "es7210.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/gpio.h"

#define PIN_MCLK (BOARD_IS_ROUND() ? 42 : 7)
#define PIN_BCLK (BOARD_IS_ROUND() ? 9 : 15)
#define PIN_WS   (BOARD_IS_ROUND() ? 45 : 46)
#define PIN_DOUT (BOARD_IS_ROUND() ? 8 : 45)
#define PIN_DIN  (BOARD_IS_ROUND() ? 10 : 6)
#define PIN_PA_ROUND 46
#define BLOCK    240   // 10 ms

static const char *TAG = "audio";
static i2s_chan_handle_t tx, rx;
static bool mic_ok, rx_on;
static es8311_handle_t codec;
static volatile audio_synth_t synth;
static TaskHandle_t task_h;
static bool ok;
static const char *status = "Non ancora avviato";
static volatile bool in_synth;   // il task audio sta eseguendo il sintetizzatore
static es7210_dev_handle_t adc;   // microfoni
static int mic_reconf;            // riconfigurazioni dei microfoni (diagnostica)

// Configura l'ES7210. Si rifà a ogni accensione dei microfoni: se il convertitore perde la
// configurazione (per esempio configurato con il clock non ancora stabile) manda solo
// silenzio, e prima restava così fino allo spegnimento (l'accordatore "non sentiva").
static bool mic_config(void)
{
    if (!adc) return false;
    es7210_codec_config_t mc = {
        .sample_rate_hz = AUDIO_RATE,
        .mclk_ratio = 512,
        .i2s_format = ES7210_I2S_FMT_I2S,
        .bit_width = ES7210_I2S_BITS_16B,
        .mic_bias = ES7210_MIC_BIAS_2V87,
        .mic_gain = ES7210_MIC_GAIN_12DB,   // margine per le urla: satura oltre ~120 dB
        .flags.tdm_enable = false,
    };
    if (es7210_config_codec(adc, &mc) != ESP_OK) return false;
    es7210_config_volume(adc, 0);
    return true;
}

static void audio_task(void *arg)
{
    static int16_t mono[BLOCK], stereo[BLOCK * 2];
    for (;;) {
        audio_synth_t s = synth;
        if (!s) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
        in_synth = true;
        s(mono, BLOCK);
        in_synth = false;
        for (int i = 0; i < BLOCK; i++) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
        size_t w;
        i2s_channel_write(tx, stereo, sizeof(stereo), &w, portMAX_DELAY);
    }
}

// ES7210 (microfoni): l'indirizzo dipende dai pin A0/A1, lo cerco
static void mic_find(void)
{
    for (uint8_t a = 0x40; a <= 0x43 && !mic_ok; a++) {
        if (i2c_master_probe(board_i2c0(), a, 50) != ESP_OK) continue;
        if (!adc) {
            i2c_device_config_t md = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = a, .scl_speed_hz = 300000};
            i2c_master_dev_handle_t mdev;
            if (i2c_master_bus_add_device(board_i2c0(), &md, &mdev) != ESP_OK) break;
            es7210_i2c_config_t ic = {.dev = mdev};
            if (es7210_new_codec(&ic, &adc) != ESP_OK) { adc = NULL; break; }
        }
        mic_ok = mic_config();
        ESP_LOGI(TAG, "ES7210 a 0x%02x: %s", a, mic_ok ? "ok" : "errore");
    }
}

bool audio_init(void)
{
    if (ok) {
        // alla prima accensione non aveva risposto: si riprova (al massimo ogni 10 s, la
        // ricerca blocca qualche decina di ms se il chip non c'è)
        static int64_t last_try;
        int64_t now = esp_timer_get_time();
        if (!mic_ok && now - last_try > 10000000) { last_try = now; mic_find(); }
        return true;
    }
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    if (i2s_new_channel(&cc, &tx, &rx) != ESP_OK) { status = "I2S occupato"; return false; }   // full duplex: stessi clock
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN,
        },
    };
    // MCLK = 512 × fs = 12,288 MHz: è la combinazione supportata sia da ES8311 sia da ES7210 a 24 kHz
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
    i2c_master_dev_handle_t dev = NULL;
    status = "I2S non configurabile";
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK || i2s_channel_init_std_mode(rx, &sc) != ESP_OK) goto fail;
    i2s_channel_enable(tx); // l'MCLK deve girare prima di configurare il codec

    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ES8311_ADDRESS_0, .scl_speed_hz = 300000};
    status = "ES8311 non risponde";
    if (i2c_master_bus_add_device(board_i2c0(), &dc, &dev) != ESP_OK) { dev = NULL; goto fail; }
    codec = es8311_create(dev);
    es8311_clock_config_t clk = {
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_RATE * 512,
        .sample_frequency = AUDIO_RATE,
    };
    if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 non risponde");
        goto fail;
    }
    audio_set_volume(g_set.volume);
    if (BOARD_IS_ROUND()) {   // amplificatore acceso
        gpio_config_t pa = {.pin_bit_mask = 1ULL << PIN_PA_ROUND, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&pa);
        gpio_set_level(PIN_PA_ROUND, 1);
    }

    mic_find();
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 6, &task_h, 0);
    ok = true;
    status = "Pronto";
    return true;

fail:
    // libera tutto: altrimenti l'I2S resta occupato e ogni tentativo successivo fallisce
    if (codec) { es8311_delete(codec); codec = NULL; }
    if (dev) i2c_master_bus_rm_device(dev);
    i2s_channel_disable(tx);   // se non era abilitato restituisce solo un errore
    i2s_del_channel(tx);
    i2s_del_channel(rx);
    tx = rx = NULL;
    return false;
}

void audio_set_volume(int pct)
{
    if (codec) es8311_voice_volume_set(codec, pct, NULL);
}

void audio_start(audio_synth_t s)
{
    if (!ok) return;
    synth = s;
    xTaskNotifyGive(task_h);
}

// Dopo il ritorno il sintetizzatore non è più in esecuzione (chi lo ferma può liberarne
// lo stato). Dal task audio stesso (un synth che si ferma da solo) non si aspetta.
void audio_stop(void)
{
    synth = NULL;
    if (!task_h || xTaskGetCurrentTaskHandle() == task_h) return;
    for (int i = 0; in_synth && i < 20; i++) vTaskDelay(1);
}

// Ferma l'uscita solo se sta suonando ancora `s`: un sintetizzatore che finisce da solo
// non deve spegnere quello appena avviato da un'altra app.
void audio_stop_if(audio_synth_t s)
{
    audio_synth_t cur = s;
    __atomic_compare_exchange_n(&synth, &cur, NULL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

audio_synth_t audio_current(void) { return synth; }
bool audio_mic_active(void) { return rx_on; }
const char *audio_status(void) { return status; }
bool audio_mic_ok(void) { return mic_ok; }

bool audio_mic_start(void)
{
    if (!ok || !mic_ok) return false;
    if (!rx_on) {
        // il clock (MCLK dall'uscita) gira già: si riconfigura il convertitore da capo
        if (!mic_config()) ESP_LOGW(TAG, "ES7210: riconfigurazione non riuscita");
        else mic_reconf++;
        i2s_channel_enable(rx);
        rx_on = true;
    }
    return true;
}

bool audio_mic_probe(int ms, int *peak, int *rms, int *zeros_pct)
{
    if (!audio_init()) return false;
    bool mine = !rx_on;
    if (mine && !audio_mic_start()) return false;
    EXT_RAM_BSS_ATTR static int16_t buf[480 * 2];   // in PSRAM: serve solo per la prova
    long long sum = 0;
    int n = 0, pk = 0, z = 0;
    for (int t = 0; t < ms; t += 20) {
        int got = audio_mic_read(buf, 480, 100);
        for (int i = 0; i < got * 2; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > pk) pk = v;
            if (!buf[i]) z++;
            sum += (long long)buf[i] * buf[i];
            n++;
        }
    }
    if (mine) audio_mic_stop();
    *peak = pk;
    *rms = n ? (int)__builtin_sqrt((double)sum / n) : 0;
    *zeros_pct = n ? z * 100 / n : 100;
    ESP_LOGI(TAG, "prova microfono: %d campioni, picco %d, rms %d, zeri %d%%", n, pk, *rms, *zeros_pct);
    return n > 0;
}

void audio_mic_stop(void)
{
    if (rx_on) { i2s_channel_disable(rx); rx_on = false; }
}

int audio_mic_read(int16_t *stereo, int frames, int timeout_ms)
{
    if (!rx_on) return 0;
    size_t got = 0;
    i2s_channel_read(rx, stereo, frames * 4, &got, pdMS_TO_TICKS(timeout_ms));
    return got / 4;
}
