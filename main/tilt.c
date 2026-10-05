// tilt.c
// Sterzo: angolo del vettore gravità lungo l'asse lungo del dispositivo.
// Avanti/indietro: rotazione della gravità nel piano (asse corto, normale allo schermo).
// L'asse lungo si ricava alla calibrazione: è quello (fra X e Y dell'IMU) su cui la
// gravità pesa meno quando tieni il gadget in orizzontale davanti a te.
#include "tilt.h"
#include "board.h"
#include "settings.h"
#include <math.h>

#define DEAD_DEG 3.0f
#define RAD2DEG  57.29578f

static vec3_t rest;
static bool has_rest, long_is_x;

static bool read_norm(vec3_t *g)
{
    vec3_t a = {0}, s;
    for (int i = 0; i < 3; i++) {
        if (!board_imu_accel(&s)) return false;
        a.x += s.x; a.y += s.y; a.z += s.z;
    }
    float n = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (n < 0.01f) return false;
    g->x = a.x / n; g->y = a.y / n; g->z = a.z / n;
    return true;
}

void tilt_calibrate(void)
{
    vec3_t g;
    if (!read_norm(&g)) return;
    rest = g;
    long_is_x = fabsf(g.x) < fabsf(g.y);
    has_rest = true;
}

bool tilt_calibrated(void) { return has_rest; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

bool tilt_read(float *steer, float *pitch)
{
    vec3_t g;
    if (!read_norm(&g)) return false;
    if (!has_rest) tilt_calibrate();
    float gl = long_is_x ? g.x : g.y, rl = long_is_x ? rest.x : rest.y;
    float gs = long_is_x ? g.y : g.x, rs = long_is_x ? rest.y : rest.x;
    float st = (asinf(clampf(gl, -1, 1)) - asinf(clampf(rl, -1, 1))) * RAD2DEG;
    float pt = (atan2f(gs, g.z) - atan2f(rs, rest.z)) * RAD2DEG;
    if (pt > 180) pt -= 360;
    if (pt < -180) pt += 360;
    if (g_set.flipped) { st = -st; pt = -pt; }
    if (g_set.doom_inv_steer) st = -st;
    if (g_set.doom_inv_pitch) pt = -pt;
    *steer = st;
    *pitch = pt;
    return true;
}

float tilt_full_scale(void)
{
    static const float fs[] = {30, 25, 20, 15, 11};
    int s = g_set.doom_sens;
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    return fs[s - 1];
}

float tilt_axis(float deg)
{
    float a = fabsf(deg);
    if (a < DEAD_DEG) return 0;
    float v = (a - DEAD_DEG) / (tilt_full_scale() - DEAD_DEG);
    if (v > 1) v = 1;
    return deg < 0 ? -v : v;
}
