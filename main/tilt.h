// tilt.h — inclinazione dall'accelerometro, relativa a una posizione di riposo calibrata
#pragma once
#include <stdbool.h>

void  tilt_calibrate(void);                    // la posizione attuale diventa il "centro"
bool  tilt_calibrated(void);
bool  tilt_read(float *steer, float *pitch);   // gradi: sterzo (+ = destra), avanti (+ = avanti)
float tilt_full_scale(void);                   // gradi per l'escursione massima (dalla sensibilità)
float tilt_axis(float deg);                    // -1…+1 con zona morta
