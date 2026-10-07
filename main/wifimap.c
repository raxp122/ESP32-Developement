// wifimap.c — grafo delle reti Wi-Fi (vedi wifimap.h). Nessuna dipendenza dall'hardware:
// si prova anche sul PC (tools/sim).
#include "wifimap.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define BIG EXT_RAM_BSS_ATTR   // tabelle grandi nella PSRAM: la RAM interna serve al Wi-Fi
#else
#define BIG
#endif

#define NE        (WM_MAX * (WM_MAX - 1) / 2)
#define P0        (-40.0f)     // dBm a 1 m
#define PL_N      2.7f         // esponente di propagazione in casa
#define MAGIC     0x3250414Du  // "MAP2"
#define STEP_M    3.0f         // metri fra due scansioni oltre i quali si frena
#define STEP_W    0.5f

typedef struct {
    uint16_t n;          // scansioni in cui le due reti c'erano insieme (il "filo")
} edge_t;

BIG static wm_node_t nodes[WM_MAX];
static int n_nodes;
BIG static edge_t edges[NE];
static uint32_t scans;
static float me_x, me_y;
static bool me_ok;
BIG static float trail[WM_TRAIL][2];
static int trail_n;

// le ultime scansioni ("pose"): dove ero e le distanze misurate dalle reti
#define WIN   48
#define MEAS  24
typedef struct { float x, y; int n; uint8_t idx[MEAS]; float d[MEAS]; } pose_t;
BIG static pose_t win[WIN];
static int win_n;

static int eidx(int i, int j)
{
    if (i > j) { int t = i; i = j; j = t; }
    return j * (j - 1) / 2 + i;
}

float wm_rssi_dist(float rssi)
{
    float d = powf(10.0f, (P0 - rssi) / (10.0f * PL_N));
    return d < 0.5f ? 0.5f : d > 80 ? 80 : d;
}

static float frand(void) { return (rand() % 2001 - 1000) / 1000.0f; }

void wm_reset(void)
{
    memset(nodes, 0, sizeof(nodes));
    memset(edges, 0, sizeof(edges));
    n_nodes = 0;
    scans = 0;
    me_x = me_y = 0;
    me_ok = false;
    trail_n = 0;
    win_n = 0;
}

int wm_count(void) { return n_nodes; }
const wm_node_t *wm_node(int i) { return i >= 0 && i < n_nodes ? &nodes[i] : NULL; }
uint32_t wm_scans(void) { return scans; }


bool wm_edge(int i, int j, float *len, int *count)
{
    if (i == j || i < 0 || j < 0 || i >= n_nodes || j >= n_nodes) return false;
    const edge_t *e = &edges[eidx(i, j)];
    if (!e->n) return false;
    if (len) *len = hypotf(nodes[i].x - nodes[j].x, nodes[i].y - nodes[j].y);
    if (count) *count = e->n;
    return true;
}

int wm_links(int i)
{
    int c = 0;
    for (int a = 0; a < n_nodes; a++)
        for (int b = a + 1; b < n_nodes; b++)
            if (edges[eidx(a, b)].n && (i < 0 || a == i || b == i)) c++;
    return c;
}

bool wm_me(float *x, float *y) { if (x) *x = me_x; if (y) *y = me_y; return me_ok; }

int wm_trail(float (*p)[2], int max)
{
    int k = trail_n < max ? trail_n : max;
    for (int i = 0; i < k; i++) {
        p[i][0] = trail[trail_n - k + i][0];
        p[i][1] = trail[trail_n - k + i][1];
    }
    return k;
}

// hotspot di telefoni e simili: indirizzo "amministrato localmente" (casuale) o nome tipico
static bool looks_hotspot(const uint8_t *b, const char *ssid)
{
    if (b[0] & 0x02) return true;
    static const char *const pat[] = {"iPhone", "Android", "DIRECT-", "Galaxy", "Redmi", "Pixel", "Hotspot", "hotspot", "OnePlus", "Xiaomi"};
    for (unsigned i = 0; i < sizeof(pat) / sizeof(pat[0]); i++) if (strstr(ssid, pat[i])) return true;
    return false;
}

static int find(const uint8_t *b)
{
    for (int i = 0; i < n_nodes; i++) if (!memcmp(nodes[i].bssid, b, 6)) return i;
    return -1;
}

static void drop(int k)   // toglie la rete k (e i suoi fili), spostando l'ultima al suo posto
{
    int last = n_nodes - 1;
    if (k != last) {
        nodes[k] = nodes[last];
        for (int o = 0; o < last; o++) {
            if (o == k) continue;
            edges[eidx(k, o)] = edges[eidx(last, o)];
        }
    }
    for (int o = 0; o < last; o++) memset(&edges[eidx(last, o)], 0, sizeof(edge_t));
    // le scansioni recenti: via le misure della rete tolta, l'ultima prende il suo numero
    for (int p = 0; p < win_n; p++) {
        int w = 0;
        for (int i = 0; i < win[p].n; i++) {
            int x = win[p].idx[i];
            if (x == k) continue;
            win[p].idx[w] = x == last ? k : x;
            win[p].d[w++] = win[p].d[i];
        }
        win[p].n = w;
    }
    n_nodes--;
}

static int add_node(const wifi_ap_t *a)
{
    if (n_nodes >= WM_MAX) {
        // piena: si fa posto togliendo la rete vista meno spesso tra quelle sparite da un po'
        int worst = -1;
        for (int i = 0; i < n_nodes; i++)
            if (scans - nodes[i].last > 30 && (worst < 0 || nodes[i].seen < nodes[worst].seen)) worst = i;
        if (worst < 0) return -1;
        drop(worst);
    }
    wm_node_t *n = &nodes[n_nodes];
    memset(n, 0, sizeof(*n));
    memcpy(n->bssid, a->bssid, 6);
    snprintf(n->ssid, sizeof(n->ssid), "%s", a->ssid);
    n->rssi = a->rssi;
    n->kind = looks_hotspot(a->bssid, a->ssid) ? WM_HOTSPOT : WM_FIXED;
    return n_nodes++;
}

static bool fixed(int i) { return nodes[i].kind == WM_FIXED; }

/* ---------- ottimizzazione: reti e percorso insieme ----------
 * Le ultime WIN scansioni sono "pose" (dove ero) con le distanze misurate dalle reti.
 * Si sistemano insieme pose e posizioni delle reti fisse perché |posa - rete| torni con
 * le distanze (errore relativo: la potenza sbaglia di più da lontano), con due vincoli:
 * tra una scansione e l'altra si fanno pochi passi, e una rete non si sposta di molto da
 * dove la mettevano le scansioni ormai uscite dalla finestra (memoria). */

static float wgt(float d) { return 1.0f / (0.12f * d * d + 1.0f); }

static void recenter(void)
{
    // la mappa non ha un'origine: si tiene centrata sulle reti fisse
    float cx = 0, cy = 0;
    int k = 0;
    for (int i = 0; i < n_nodes; i++) if (fixed(i) && nodes[i].placed) { cx += nodes[i].x; cy += nodes[i].y; k++; }
    if (!k) return;
    cx /= k; cy /= k;
    for (int i = 0; i < n_nodes; i++) {
        nodes[i].x -= cx; nodes[i].y -= cy;
        for (int t = 0; t < nodes[i].tn; t++) { nodes[i].tx[t] -= cx; nodes[i].ty[t] -= cy; }
    }
    for (int t = 0; t < trail_n; t++) { trail[t][0] -= cx; trail[t][1] -= cy; }
    for (int p = 0; p < win_n; p++) { win[p].x -= cx; win[p].y -= cy; }
    me_x -= cx; me_y -= cy;
}

static void optimize(int iters)
{
    float ax[WM_MAX], ay[WM_MAX];   // dove stavano le reti: la memoria delle scansioni vecchie
    for (int i = 0; i < n_nodes; i++) { ax[i] = nodes[i].x; ay[i] = nodes[i].y; }
    for (int it = 0; it < iters; it++) {
        float gx[WM_MAX] = {0}, gy[WM_MAX] = {0}, gw[WM_MAX] = {0};
        for (int p = 0; p < win_n; p++) {
            pose_t *q = &win[p];
            float px = 0, py = 0, pw = 0;
            for (int k = 0; k < q->n; k++) {
                int i = q->idx[k];
                if (i >= n_nodes || !fixed(i) || !nodes[i].placed) continue;
                float dx = q->x - nodes[i].x, dy = q->y - nodes[i].y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
                float w = wgt(q->d[k]), r = (d - q->d[k]) / d;
                px += w * r * dx; py += w * r * dy; pw += w;
                gx[i] += w * r * dx; gy[i] += w * r * dy; gw[i] += w;   // la rete va verso la posa (o via)
            }
            // pochi passi fra una scansione e la successiva (oltre STEP_M la molla tira)
            for (int s = -1; s <= 1; s += 2) {
                int o = p + s;
                if (o < 0 || o >= win_n) continue;
                float dx = q->x - win[o].x, dy = q->y - win[o].y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
                if (d > STEP_M) { float r = (d - STEP_M) / d; px += STEP_W * r * dx; py += STEP_W * r * dy; pw += STEP_W; }
            }
            if (pw > 0) { q->x -= 0.5f * px / pw; q->y -= 0.5f * py / pw; }
        }
        for (int i = 0; i < n_nodes; i++) {
            if (!fixed(i) || !nodes[i].placed || gw[i] <= 0) continue;
            // memoria: più è stata vista, più resta dov'era
            float m = nodes[i].seen > WIN ? 0.02f * (nodes[i].seen - WIN) : 0;
            if (m > 2) m = 2;
            gx[i] += m * (ax[i] - nodes[i].x);
            gy[i] += m * (ay[i] - nodes[i].y);
            gw[i] += m;
            nodes[i].x += 0.5f * gx[i] / gw[i];
            nodes[i].y += 0.5f * gy[i] / gw[i];
        }
    }
    if (win_n) { me_x = win[win_n - 1].x; me_y = win[win_n - 1].y; }
}

// errore relativo delle distanze di una rete nell'ultima scansione: se resta alto, si muove
static void check_moving(const pose_t *q)
{
    for (int k = 0; k < q->n; k++) {
        int i = q->idx[k];
        wm_node_t *p = &nodes[i];
        if (!fixed(i) || !p->placed || p->seen < 4) continue;
        float dx = q->x - p->x, dy = q->y - p->y, d = sqrtf(dx * dx + dy * dy);
        float rel = fabsf(d - q->d[k]) / (q->d[k] + 2.0f);
        p->checks++;
        if (rel > 0.6f) p->bad++;
        if (p->checks >= 15 && p->bad * 100 > p->checks * 40) p->kind = WM_MOVING;
    }
}

void wm_add_scan(const wifi_ap_t *aps, int n)
{
    int idx[MEAS];
    float dist[MEAS];
    int m = 0;
    scans++;
    for (int k = 0; k < n && m < MEAS; k++) {
        if (!aps[k].ssid[0]) continue;
        int i = find(aps[k].bssid);
        if (i < 0) i = add_node(&aps[k]);
        if (i < 0) continue;
        bool dup = false;
        for (int q = 0; q < m; q++) if (idx[q] == i) dup = true;
        if (dup) continue;
        wm_node_t *p = &nodes[i];
        p->rssi = p->seen ? p->rssi * 0.6f + aps[k].rssi * 0.4f : aps[k].rssi;
        p->dist = wm_rssi_dist(aps[k].rssi);   // la distanza di adesso, non la media
        p->dist_avg = p->seen ? p->dist_avg * 0.9f + p->dist * 0.1f : p->dist;
        p->seen++;
        p->last = scans;
        idx[m] = i;
        dist[m] = p->dist;
        m++;
    }
    // fili: le reti viste insieme nella stessa scansione (la loro distanza è quella
    // nella mappa, che viene dalle pose)
    for (int a = 0; a < m; a++)
        for (int b = a + 1; b < m; b++) {
            edge_t *e = &edges[eidx(idx[a], idx[b])];
            if (e->n < 65535) e->n++;
        }
    // nuova posa: parte da dove ero
    if (win_n == WIN) { memmove(win, win + 1, sizeof(win[0]) * (WIN - 1)); win_n--; }
    pose_t *q = &win[win_n++];
    q->x = me_x; q->y = me_y; q->n = 0;
    for (int a = 0; a < m; a++) { q->idx[q->n] = idx[a]; q->d[q->n] = dist[a]; q->n++; }
    // le reti fisse nuove partono alla loro distanza da me, in una direzione a caso
    for (int a = 0; a < m; a++) {
        wm_node_t *p = &nodes[idx[a]];
        if (p->placed || !fixed(idx[a])) continue;
        float ang = frand() * 3.14159f;
        p->x = me_x + dist[a] * cosf(ang);
        p->y = me_y + dist[a] * sinf(ang);
        p->placed = 1;
    }
    float ox[WM_MAX], oy[WM_MAX];
    for (int i = 0; i < n_nodes; i++) { ox[i] = nodes[i].x; oy[i] = nodes[i].y; }
    optimize(40);
    check_moving(q);
    // deriva: le reti fisse si assestano, una che si muove continua a spostarsi. Si
    // confronta con la deriva tipica (mediana) delle altre, che cala con il tempo.
    float dr[WM_MAX];
    int nd = 0;
    for (int i = 0; i < n_nodes; i++) {
        if (!fixed(i) || !nodes[i].placed || nodes[i].seen < 2) continue;
        // le reti lontane oscillano di più (la potenza sbaglia di più): si misura in
        // proporzione alla loro distanza media
        float d = hypotf(nodes[i].x - ox[i], nodes[i].y - oy[i]) / (nodes[i].dist_avg + 3.0f);
        nodes[i].drift = nodes[i].drift * 0.95f + d * 0.05f;
        dr[nd++] = nodes[i].drift;
    }
    if (nd >= 4) {
        for (int a = 1; a < nd; a++) for (int b = a; b > 0 && dr[b] < dr[b - 1]; b--) { float t = dr[b]; dr[b] = dr[b - 1]; dr[b - 1] = t; }
        float med = dr[nd / 2];
        for (int i = 0; i < n_nodes; i++)
            if (fixed(i) && nodes[i].seen > 40 && nodes[i].drift > 0.02f && nodes[i].drift > 3.0f * med) nodes[i].kind = WM_MOVING;
    }
    recenter();
    me_ok = true;
    if (trail_n == WM_TRAIL) { memmove(trail, trail + 1, sizeof(trail[0]) * (WM_TRAIL - 1)); trail_n--; }
    trail[trail_n][0] = me_x;
    trail[trail_n][1] = me_y;
    trail_n++;
    // le mobili: sul cerchio della loro distanza attorno a me, dalla parte dov'erano prima
    for (int a = 0; a < m; a++) {
        wm_node_t *p = &nodes[idx[a]];
        if (fixed(idx[a])) continue;
        float dx = p->x - me_x, dy = p->y - me_y, d = sqrtf(dx * dx + dy * dy);
        if (!p->placed || d < 0.01f) { float ang = frand() * 3.14159f; dx = cosf(ang); dy = sinf(ang); d = 1; }
        p->x = me_x + dx / d * dist[a];
        p->y = me_y + dy / d * dist[a];
        p->placed = 1;
        if (p->tn == WM_MTRAIL) { memmove(p->tx, p->tx + 1, sizeof(float) * (WM_MTRAIL - 1)); memmove(p->ty, p->ty + 1, sizeof(float) * (WM_MTRAIL - 1)); p->tn--; }
        p->tx[p->tn] = p->x;
        p->ty[p->tn] = p->y;
        p->tn++;
    }
}

/* ---------- salvataggio ---------- */

bool wm_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t h[2] = {MAGIC, (uint32_t)n_nodes};
    bool ok = fwrite(h, sizeof(h), 1, f) == 1 && fwrite(&scans, sizeof(scans), 1, f) == 1 &&
              fwrite(nodes, sizeof(wm_node_t), n_nodes, f) == (size_t)n_nodes &&
              fwrite(edges, sizeof(edges), 1, f) == 1;
    fclose(f);
    return ok;
}

bool wm_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t h[2];
    bool ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == MAGIC && h[1] <= WM_MAX;
    if (ok) {
        wm_reset();
        ok = fread(&scans, sizeof(scans), 1, f) == 1 && fread(nodes, sizeof(wm_node_t), h[1], f) == h[1] &&
             fread(edges, sizeof(edges), 1, f) == 1;
        n_nodes = ok ? (int)h[1] : 0;
        win_n = 0;
        if (!ok) wm_reset();
    }
    fclose(f);
    // la posizione del Gadget non si salva: ripartirà dalla prima scansione
    return ok;
}

bool wm_export(const char *dir)
{
    char p[96];
    mkdir(dir, 0775);
    snprintf(p, sizeof(p), "%s/reti.csv", dir);
    FILE *f = fopen(p, "w");
    if (!f) return false;
    static const char *const kn[] = {"fissa", "mobile", "hotspot"};
    fprintf(f, "rete,bssid,x_m,y_m,tipo,viste,dBm\n");
    for (int i = 0; i < n_nodes; i++) {
        const wm_node_t *n = &nodes[i];
        fprintf(f, "\"%s\",%02X:%02X:%02X:%02X:%02X:%02X,%.1f,%.1f,%s,%u,%.0f\n", n->ssid, n->bssid[0], n->bssid[1],
                n->bssid[2], n->bssid[3], n->bssid[4], n->bssid[5], n->x, n->y, kn[n->kind % 3], n->seen, n->rssi);
    }
    fclose(f);
    snprintf(p, sizeof(p), "%s/collegamenti.csv", dir);
    f = fopen(p, "w");
    if (!f) return false;
    fprintf(f, "rete_a,rete_b,distanza_m,volte\n");
    for (int a = 0; a < n_nodes; a++)
        for (int b = a + 1; b < n_nodes; b++) {
            const edge_t *e = &edges[eidx(a, b)];
            // la distanza nella mappa (quella stimata dal solo filo è meno precisa)
            if (e->n) fprintf(f, "\"%s\",\"%s\",%.1f,%u\n", nodes[a].ssid, nodes[b].ssid,
                              hypotf(nodes[a].x - nodes[b].x, nodes[a].y - nodes[b].y), e->n);
        }
    fclose(f);
    return true;
}
