// theremin.h — motore del Theremin: sintetizzatore, registratore e lettore di registrazioni.
//
// L'app (apps/app_theremin.c) legge l'inclinazione e chiama th_control() a ogni giro:
//   nota     = inclinazione avanti/indietro (continua o agganciata a una scala)
//   rotazione (destra/sinistra) = volume, vibrato o timbro, a scelta
//   suona    = dito sullo schermo (o sempre)
// Il suono si genera nel task audio (24 kHz). Le registrazioni sono WAV mono 16 bit sulla
// microSD (cartella theremin), con o senza il microfono mixato.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TH_WAVE_THEREMIN, TH_WAVE_SINE, TH_WAVE_TRIANGLE, TH_WAVE_SAW, TH_WAVE_SQUARE, TH_WAVE_COUNT };
enum { TH_SCALE_FREE, TH_SCALE_CHROMATIC, TH_SCALE_MAJOR, TH_SCALE_MINOR, TH_SCALE_PENTA, TH_SCALE_BLUES, TH_SCALE_COUNT };
enum { TH_ROLL_VOLUME, TH_ROLL_VIBRATO, TH_ROLL_TONE, TH_ROLL_NONE, TH_ROLL_COUNT };

// impostazioni (salvate nell'NVS, namespace "theremin"; incluse nei backup)
typedef struct {
    uint32_t magic;
    uint8_t wave;       // TH_WAVE_*
    int8_t  octave;     // -2..+2
    uint8_t span;       // estensione: 1..3 ottave
    uint8_t glide;      // 0..4 (indice in tabella)
    uint8_t vib_depth;  // 0..10 (vibrato fisso, in decimi di semitono)
    uint8_t vib_rate;   // 3..8 Hz
    uint8_t scale;      // TH_SCALE_*
    uint8_t root;       // tonica 0..11 (0 = Do)
    uint8_t echo;       // 0 spento, 1..3 corto/medio/lungo
    uint8_t echo_fb;    // 0..3 ripetizioni
    uint8_t tone;       // 0..4 timbro (scuro → brillante)
    uint8_t roll_fn;    // TH_ROLL_*
    uint8_t sens;       // 0..2: angolo per tutta l'estensione 90 / 60 / 40 gradi
    uint8_t inv_pitch, inv_roll;
    uint8_t always;     // suona sempre (non solo col dito sullo schermo)
    uint8_t mic;        // registrazione con il microfono
    uint8_t mic_gain;   // 1..4
    uint8_t level;      // 10..100 volume del sintetizzatore
} th_cfg_t;

extern th_cfg_t th_cfg;
void th_cfg_load(void);
void th_cfg_save(void);
const char *th_wave_name(int w);
const char *th_scale_name(int s);
const char *th_note_name(int n);     // 0..11, nomi italiani (Do, Do#, Re…)
const char *th_roll_name(int r);
int  th_glide_ms(int i);
int  th_sens_deg(int i);

// suono
bool th_start(void);                 // avvia il sintetizzatore (false se l'audio non c'è)
void th_stop(void);
// p, r: inclinazione e rotazione normalizzate (-1..1, 0 = posizione zero); gate: suona
void th_control(float p, float r, bool gate);
float th_freq(void);                 // frequenza attuale (Hz)
float th_note(int *midi);            // nota attuale: restituisce i centesimi dalla nota midi
float th_level(void);                // livello d'uscita 0..1 (per l'indicatore)

// registrazione
bool th_rec_start(void);
void th_rec_stop(void);
bool th_recording(void);
uint32_t th_rec_ms(void);
const char *th_rec_name(void);       // ultimo file (senza cartella)
const char *th_error(void);

// registrazioni salvate
int  th_list(char (*names)[40], int max);   // dalla più recente
bool th_play(const char *name);
void th_play_stop(void);
bool th_playing(void);
uint32_t th_play_ms(void);
uint32_t th_file_ms(const char *name);       // durata
bool th_delete(const char *name);
