// i_gadget.c — piattaforma prboom per il gadget: video scalato, inclinazione, pulsanti touch
// Basato sul main di prboom-go (retro-go, GPLv3).
#include <sys/stat.h>
#include <sys/unistd.h>
#include <string.h>
#include <stdio.h>
#include <doomtype.h>
#include <doomstat.h>
#include <doomdef.h>
#include <d_main.h>
#include <d_event.h>
#include <g_game.h>
#include <i_system.h>
#include <i_video.h>
#include <i_sound.h>
#include <i_main.h>
#include <m_argv.h>
#include <m_fixed.h>
#include <m_misc.h>
#include <r_draw.h>
#include <r_fps.h>
#include <s_sound.h>
#include <st_stuff.h>
#include <v_video.h>
#include <z_zone.h>
#include <lprintf.h>
#include <w_wad.h>
#include <sounds.h>
#include <oplplayer.h>
#include <mus2mid.h>
#include "audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

#include "board.h"
#include "display.h"
#include "input.h"
#include "settings.h"
#include "tilt.h"
#include "sd.h"
#include "ui.h"
#include "doom_app.h"

#undef malloc
#undef free

static const char *TAG = "doom";

// --- variabili richieste da prboom ---
int snd_card = 1, mus_card = 1;
int snd_samplerate = 24000;
int current_palette = 0;
int i_analog_turn, i_analog_forward;

// --- geometria: 320×200 scalato a 275×172 al centro, bande laterali per i comandi ---
#define DOOM_W   320
#define DOOM_H   200
#define GAME_W   275
#define GAME_X0  ((SCR_W - GAME_W) / 2)
#define BAND_L   GAME_X0
#define BAND_R0  (GAME_X0 + GAME_W)

static uint8_t *fb8;                    // framebuffer 8 bit di Doom (RAM interna)
static uint16_t palette[256];           // RGB565 big-endian
static uint16_t sx_lut[GAME_W];         // colonna sorgente per ogni colonna di gioco
static uint32_t sy_off[LCD_W];          // offset riga sorgente per ogni pixel fisico
static bool flipped;

/* ---------------- pulsanti nelle bande laterali ---------------- */

typedef enum { B_MENU, B_USE, B_WEAPON, B_PAUSE, B_MAP, B_FIRE, B_N } btn_t;
static const char *btn_label[B_N] = {
    LV_SYMBOL_LIST "\nMenu", LV_SYMBOL_UP "\nUsa", LV_SYMBOL_SHUFFLE "\nArma",
    LV_SYMBOL_PAUSE "\nPausa", LV_SYMBOL_GPS "\nMappa", "SPARA",
};
static lv_area_t rects[B_N];
static lv_obj_t *objs[B_N];
static int pressed = -1;
static bool ui_dirty;

static void set_rect(int b, int x, int y, int w, int h)
{
    rects[b] = (lv_area_t){x, y, x + w - 1, y + h - 1};
}

static void build_bands(void)
{
    const int m = 4, h3 = SCR_H / 3;
    set_rect(B_MENU, m, m, BAND_L - 2 * m, h3 - 2 * m);
    set_rect(B_USE, m, h3 + m, BAND_L - 2 * m, h3 - 2 * m);
    set_rect(B_WEAPON, m, 2 * h3 + m, BAND_L - 2 * m, SCR_H - 2 * h3 - 2 * m);
    int rw = SCR_W - BAND_R0;
    set_rect(B_PAUSE, BAND_R0 + m, m, rw / 2 - m - m / 2, h3 - 2 * m);
    set_rect(B_MAP, BAND_R0 + rw / 2 + m / 2, m, rw / 2 - m - m / 2, h3 - 2 * m);
    set_rect(B_FIRE, BAND_R0 + m, h3 + m, rw - 2 * m, SCR_H - h3 - 2 * m);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    for (int b = 0; b < B_N; b++) {
        lv_obj_t *o = lv_obj_create(scr);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, rects[b].x1, rects[b].y1);
        lv_obj_set_size(o, lv_area_get_width(&rects[b]), lv_area_get_height(&rects[b]));
        lv_obj_set_style_radius(o, 10, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_t *l = lv_label_create(o);
        lv_label_set_text(l, btn_label[b]);
        lv_obj_set_style_text_font(l, b == B_FIRE ? &font_l : &font_s, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        objs[b] = o;
    }
}

static void style_buttons(void)
{
    for (int b = 0; b < B_N; b++) {
        bool on = b == pressed;
        lv_obj_set_style_bg_color(objs[b], on ? ui_accent() : lv_color_hex(b == B_FIRE ? 0x2A1410 : 0x15181C), 0);
        lv_obj_set_style_border_color(objs[b], b == B_FIRE ? lv_color_hex(0x8A2A1A) : C_FAINT, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(objs[b], 0), on ? C_BG : (b == B_FIRE ? lv_color_hex(0xFF6B57) : C_TEXT), 0);
    }
}

static int hit(int x, int y)
{
    for (int b = 0; b < B_N; b++)
        if (x >= rects[b].x1 && x <= rects[b].x2 && y >= rects[b].y1 && y <= rects[b].y2) return b;
    return -1;
}

/* ---------------- video ---------------- */

void I_StartFrame(void) {}
void I_UpdateNoBlit(void) {}
bool I_StartDisplay(void) { return true; }
void I_EndDisplay(void) {}

void I_FinishUpdate(void)
{
    if (ui_dirty) {
        // ridisegna le bande (LVGL scrive solo fuori dall'area di gioco)
        ui_dirty = false;
        style_buttons();
        lv_refr_now(NULL);
    }
    uint16_t *frame = display_frame();
    for (int i = 0; i < GAME_W; i++) {
        int x = GAME_X0 + i;
        int py = flipped ? x : (LCD_H - 1 - x);
        uint16_t *row = frame + py * LCD_W;
        const uint8_t *col = fb8 + sx_lut[i];
        for (int px = 0; px < LCD_W; px++) row[px] = palette[col[sy_off[px]]];
    }
    display_push();
}

void I_SetPalette(int pal)
{
    uint16_t *p = V_BuildPalette(pal, 16);
    for (int i = 0; i < 256; i++) palette[i] = (p[i] << 8) | (p[i] >> 8);
    Z_Free(p);
    current_palette = pal;
}

void I_InitGraphics(void)
{
    for (int i = 0; i < 3; i++) {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENWIDTH;
    }
    screens[0].data = fb8;
    screens[0].not_on_heap = true;
    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENWIDTH;
}

void I_UpdateVideoMode(void) {}
void I_ShutdownGraphics(void) {}

/* ---------------- tempo e sistema ---------------- */

int I_GetTimeMS(void) { return (int)(esp_timer_get_time() / 1000); }
int I_GetTime(void) { return (int)((int64_t)I_GetTimeMS() * TICRATE * realtic_clock_rate / 100000); }
void I_uSleep(unsigned long usecs) { vTaskDelay(pdMS_TO_TICKS(usecs / 1000 ? usecs / 1000 : 1)); }
const char *I_DoomExeDir(void) { return SD_MOUNT "/doom"; }

void I_SafeExit(int rc)
{
    // uscita da Doom = riavvio nel launcher
    esp_restart();
}

void gadget_doom_panic(const char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

/* ---------------- audio: effetti + musica OPL ---------------- */
// Effetti: campioni 8 bit del WAD (11/22 kHz) ricampionati al volo e mescolati su 8 canali.
// Musica: i brani MUS vengono convertiti in MIDI e suonati dall'emulatore OPL2 (il suono
// della scheda Sound Blaster/AdLib dell'epoca), lo stesso usato da prboom-go.
// Il mixaggio gira nel task audio sul core 0, Doom sul core 1.

#define NUM_MIX_CHANNELS 8

typedef struct {
    uint16_t unused1;
    uint16_t samplerate;
    uint16_t length;
    uint16_t unused2;
    uint8_t samples[];
} doom_sfx_t;

typedef struct {
    const doom_sfx_t *volatile sfx;
    uint32_t pos;       // posizione in 16.16
    uint32_t step;
    int vol_l, vol_r;   // 0–255
    int starttic;
} channel_t;

static channel_t channels[NUM_MIX_CHANNELS];
static const doom_sfx_t *sfx[NUMSFX];
static const music_player_t *music_player = &opl_synth_player;
static volatile bool music_playing, audio_on;

static void set_params(channel_t *c, int vol, int sep)
{
    // vol 0–127, sep 0 (sinistra) … 255 (destra): lo speaker è mono, quindi pesa poco
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    c->vol_l = c->vol_r = vol * 2;
    (void)sep;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    if (handle >= 0 && handle < NUM_MIX_CHANNELS) set_params(&channels[handle], vol, sep);
}

int I_StartSound(int id, int channel, int vol, int sep, int pitch, int priority)
{
    if (id <= 0 || id >= NUMSFX || !sfx[id]) return -1;
    int oldest = gametic, slot = 0;
    // questi suoni ne suonano uno alla volta
    if (id == sfx_sawup || id == sfx_sawidl || id == sfx_sawful || id == sfx_sawhit || id == sfx_stnmov || id == sfx_pistol)
        for (int i = 0; i < NUM_MIX_CHANNELS; i++)
            if (channels[i].sfx == sfx[id]) channels[i].sfx = NULL;
    for (int i = 0; i < NUM_MIX_CHANNELS; i++) {
        if (!channels[i].sfx) { slot = i; break; }
        if (channels[i].starttic < oldest) { slot = i; oldest = channels[i].starttic; }
    }
    channel_t *c = &channels[slot];
    c->sfx = NULL;
    c->pos = 0;
    c->step = ((uint32_t)sfx[id]->samplerate << 16) / AUDIO_RATE;
    c->starttic = gametic;
    set_params(c, vol, sep);
    c->sfx = sfx[id];
    return slot;
}

void I_StopSound(int handle)
{
    if (handle >= 0 && handle < NUM_MIX_CHANNELS) channels[handle].sfx = NULL;
}

bool I_SoundIsPlaying(int handle) { return handle >= 0 && handle < NUM_MIX_CHANNELS && channels[handle].sfx; }

bool I_AnySoundStillPlaying(void)
{
    for (int i = 0; i < NUM_MIX_CHANNELS; i++)
        if (channels[i].sfx) return true;
    return false;
}

static void doom_mix(int16_t *out, int n)
{
    static int16_t mus[256 * 2];
    if (n > 256) n = 256;
    bool have_mus = music_playing && snd_MusicVolume > 0;
    if (have_mus) music_player->render(mus, n);
    int sfx_gain = snd_SfxVolume;   // 0–15
    for (int i = 0; i < n; i++) {
        int acc = 0;
        for (int c = 0; c < NUM_MIX_CHANNELS; c++) {
            channel_t *ch = &channels[c];
            const doom_sfx_t *s = ch->sfx;
            if (!s) continue;
            uint32_t p = ch->pos >> 16;
            if (p >= s->length) { ch->sfx = NULL; continue; }
            acc += ((int)s->samples[p] - 128) * ch->vol_l;   // ±128 × 255
            ch->pos += ch->step;
        }
        int v = (acc * sfx_gain) / 15 / 2;                    // effetti
        if (have_mus) v += (mus[2 * i] + mus[2 * i + 1]) / 2; // musica (volume già applicato dal player)
        out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
}

void I_InitSound(void)
{
    for (int i = 1; i < NUMSFX; i++)
        if (S_sfx[i].lumpnum != -1) sfx[i] = W_CacheLumpNum(S_sfx[i].lumpnum);
    music_player->init(AUDIO_RATE);
    music_player->setvolume(snd_MusicVolume);
    audio_on = audio_init();
    if (audio_on) {
        audio_set_volume(g_set.volume);
        audio_start(doom_mix);
    } else {
        ESP_LOGW(TAG, "audio non disponibile");
    }
}

void I_ShutdownSound(void)
{
    audio_stop();
    music_player->shutdown();
}

void I_SetChannels(void) {}
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_UpdateMusic(void) {}
void I_SetMusicVolume(int volume) { music_player->setvolume(volume); }
void I_PauseSong(int handle) { music_player->pause(); music_playing = false; }
void I_ResumeSong(int handle) { music_player->resume(); music_playing = true; }
void I_PlaySong(int handle, int looping) { music_player->play((void *)handle, looping); music_playing = true; }
void I_StopSong(int handle) { music_player->stop(); music_playing = false; }
void I_UnRegisterSong(int handle) { music_player->unregistersong((void *)handle); }

int I_RegisterSong(const void *data, size_t len)
{
    uint8_t *mid = NULL;
    size_t midlen;
    int handle;
    if (mus2mid(data, len, &mid, &midlen, 64) == 0) handle = (int)music_player->registersong(mid, midlen);
    else handle = (int)music_player->registersong(data, len);
    if (mid) Z_Free(mid);   // mus2mid alloca con lo heap a zone di prboom
    return handle;
}

/* ---------------- input ---------------- */

static void post_key(int key, bool down)
{
    event_t ev = {.type = down ? ev_keydown : ev_keyup, .data1 = key};
    D_PostEvent(&ev);
}

static void tap_key(int key)
{
    post_key(key, true);
    post_key(key, false);
}

static void button_keys(int b, bool down)
{
    switch (b) {
    case B_MENU:   post_key(key_escape, down); break;
    case B_USE:    post_key(key_use, down); post_key(key_backspace, down); break;
    case B_WEAPON: post_key(key_weapontoggle, down); break;
    case B_PAUSE:  post_key(key_pause, down); break;
    case B_MAP:    post_key(key_map, down); break;
    case B_FIRE:   post_key(key_fire, down); post_key(key_enter, down); break;
    }
}

static void say(const char *msg)
{
    if (gamestate == GS_LEVEL) players[consoleplayer].message = msg;
}

void I_StartTic(void)
{
    static int64_t boot_t0, pwr_t0;
    static bool boot_down, pwr_down = true, pwr_long;
    static int menu_repeat;
    static gamestate_t last_state = -1;
    int64_t now = esp_timer_get_time();

    // calibrazione automatica all'inizio di ogni livello
    if (gamestate != last_state) {
        if (gamestate == GS_LEVEL) {
            tilt_calibrate();
            say("Inclinazione calibrata - BOOT per ricalibrare");
        }
        last_state = gamestate;
    }

    // --- tasto BOOT: breve = calibra, lungo = esci ---
    bool b = board_btn_boot();
    if (b && !boot_down) { boot_down = true; boot_t0 = now; }
    if (b && boot_down && now - boot_t0 > 1500000) {
        ESP_LOGI(TAG, "uscita verso il launcher");
        esp_restart();
    }
    if (!b && boot_down) {
        boot_down = false;
        tilt_calibrate();
        say("Inclinazione calibrata");
    }

    // --- tasto PWR: tenuto 2 s = spegni (a batteria) ---
    bool p = board_btn_pwr();
    if (p && !pwr_down) { pwr_down = true; pwr_t0 = now; pwr_long = false; }
    if (p && pwr_down && !pwr_long && now - pwr_t0 > 2000000) { pwr_long = true; board_power_off(); }
    if (!p) pwr_down = false;

    // --- pulsanti touch (un dito alla volta) ---
    int x, y, cur = -1;
    if (input_touch(&x, &y)) cur = hit(x, y);
    if (cur != pressed) {
        if (pressed >= 0) button_keys(pressed, false);
        if (cur >= 0) button_keys(cur, true);
        pressed = cur;
        ui_dirty = true;
    }

    // --- inclinazione ---
    float st = 0, pt = 0;
    tilt_read(&st, &pt);
    float ax = tilt_axis(st), ay = tilt_axis(pt);
    if (menuactive) {
        // nei menu l'inclinazione diventa frecce con ripetizione
        i_analog_turn = i_analog_forward = 0;
        int key = 0;
        if (ay > 0.6f) key = key_menu_up;
        else if (ay < -0.6f) key = key_menu_down;
        else if (ax > 0.6f) key = key_menu_right;
        else if (ax < -0.6f) key = key_menu_left;
        if (!key) menu_repeat = 0;
        else if (menu_repeat-- <= 0) { tap_key(key); menu_repeat = 8; }
    } else if (gamestate == GS_LEVEL && !paused) {
        i_analog_turn = (int)(ax * 1100);
        i_analog_forward = (int)(ay * 50);
    } else {
        i_analog_turn = i_analog_forward = 0;
    }
}

void I_Init(void)
{
    snd_channels = NUM_MIX_CHANNELS;
    snd_samplerate = AUDIO_RATE;
    // le versioni precedenti salvavano i volumi a zero in prboom.cfg: in quel caso li ripristino
    if (snd_SfxVolume == 0 && snd_MusicVolume == 0) {
        snd_SfxVolume = 12;
        snd_MusicVolume = 9;
    }
}

/* ---------------- avvio ---------------- */

static const char *doom_argv[8];

static void doom_task(void *arg)
{
    myargv = doom_argv;
    myargc = 5;
    heap_caps_malloc_extmem_enable(0); // tutto in PSRAM tranne il framebuffer
    Z_Init();
    D_DoomMain();
    vTaskDelete(NULL);
}

void doom_run(void)
{
    flipped = g_set.flipped;
    char iwad[64];
    if (!doom_find_wad(iwad, sizeof(iwad))) {
        ESP_LOGE(TAG, "nessun file WAD trovato in %s/doom", SD_MOUNT);
        esp_restart();
    }
    mkdir(SD_MOUNT "/doom", 0777);

    SCREENWIDTH = DOOM_W;
    SCREENHEIGHT = DOOM_H;
    ESP_LOGI(TAG, "RAM interna libera %u KB (blocco max %u KB), PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    fb8 = heap_caps_malloc(DOOM_W * DOOM_H, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!fb8) {
        ESP_LOGW(TAG, "framebuffer in PSRAM (RAM interna insufficiente): sarà un po' più lento");
        fb8 = heap_caps_malloc(DOOM_W * DOOM_H, MALLOC_CAP_SPIRAM);
    }
    if (!fb8) gadget_doom_panic("memoria insufficiente per il framebuffer");
    for (int i = 0; i < GAME_W; i++) sx_lut[i] = i * DOOM_W / GAME_W;
    for (int px = 0; px < LCD_W; px++) {
        int y = flipped ? (LCD_W - 1 - px) : px;
        sy_off[px] = (y * DOOM_H / SCR_H) * DOOM_W;
    }

    // righe fisiche dell'area di gioco: LVGL disegna solo le bande ai lati
    int r0 = flipped ? GAME_X0 : LCD_H - GAME_X0 - GAME_W;
    display_keep_rows(r0, r0 + GAME_W - 1);
    build_bands();
    style_buttons();
    memset(display_frame(), 0, LCD_W * LCD_H * 2);
    lv_refr_now(NULL);
    display_set_brightness(g_set.brightness);

    static char iwad_arg[64];
    strlcpy(iwad_arg, iwad, sizeof(iwad_arg));
    doom_argv[0] = "doom";
    doom_argv[1] = "-save";
    doom_argv[2] = SD_MOUNT "/doom";
    doom_argv[3] = "-iwad";
    doom_argv[4] = iwad_arg;
    doom_argv[5] = NULL;
    ESP_LOGI(TAG, "avvio con %s", iwad);
    // stack in RAM interna se c'è spazio, altrimenti in PSRAM (più lento ma funziona)
    if (xTaskCreatePinnedToCoreWithCaps(doom_task, "doom", 32 * 1024, NULL, 5, NULL, 1,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "stack di Doom in PSRAM");
        if (xTaskCreatePinnedToCoreWithCaps(doom_task, "doom", 32 * 1024, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS)
            gadget_doom_panic("impossibile avviare il task di Doom");
    }
}
