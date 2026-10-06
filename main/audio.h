// audio.h — uscita audio: codec ES8311 via I2S, sintesi in tempo reale
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_RATE 24000

// Riempie n campioni mono (int16). Chiamata dal task audio.
typedef void (*audio_synth_t)(int16_t *buf, int n);

bool audio_init(void);                 // idempotente; false se il codec non risponde
void audio_start(audio_synth_t synth); // avvia la riproduzione continua
void audio_stop(void);
void audio_stop_if(audio_synth_t synth); // ferma solo se sta suonando proprio questo
void audio_set_volume(int percent);    // 0–100
audio_synth_t audio_current(void);     // sintetizzatore in uso (NULL se l'uscita è libera)
bool audio_mic_active(void);           // i microfoni sono in ascolto

// Microfoni (ES7210, due canali): campioni stereo interlacciati a 16 bit
bool audio_mic_start(void);
void audio_mic_stop(void);
int  audio_mic_read(int16_t *stereo, int frames, int timeout_ms);   // frame letti
