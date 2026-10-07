// seismo.h — Sismografo: misura le vibrazioni con l'accelerometro (scheda appoggiata),
// riconosce gli eventi con il metodo STA/LTA e li registra sulla microSD.
//
//   - 200 letture al secondo; la gravità e le derive lente si tolgono con un passa-alto
//     a 0,5 Hz per asse, quello che resta è la vibrazione (in mg, millesimi di g);
//   - evento: la media breve dell'energia (STA, 0,5 s) supera di SEISMO_RATIO volte quella
//     lunga (LTA, 20 s) e la vibrazione supera la soglia della sensibilità;
//   - ogni evento (con 5 s prima e 5 s dopo) va in sismo/AAAAMMGG-hhmmss.csv a 100 campioni
//     al secondo, e una riga nel registro sismo/eventi.csv.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t sens;      // 0 alta, 1 media, 2 bassa
    uint8_t record;    // registra gli eventi sulla microSD
} seismo_cfg_t;

extern seismo_cfg_t seismo_cfg;
void seismo_cfg_load(void);
void seismo_cfg_save(void);
const char *seismo_sens_name(int s);

enum { SEISMO_WARMUP, SEISMO_LISTEN, SEISMO_EVENT };

bool seismo_start(void);            // avvia le letture (false se manca l'accelerometro)
void seismo_stop(void);
void seismo_feed(float x, float y, float z);   // una lettura (in g): la chiama il task (o il simulatore)

// per la schermata
int   seismo_trace(int16_t *out, int max, uint32_t *cursor);   // nuovi punti del grafico (0,1 mg, asse verticale)
float seismo_now_mg(void);          // vibrazione attuale (efficace, 0,5 s)
float seismo_peak_mg(void);         // picco da quando si è aperta (o azzerato)
void  seismo_reset_peak(void);
int   seismo_state(void);
int   seismo_warmup_left(void);     // secondi di calibrazione rimasti
int   seismo_session_events(void);  // eventi da quando si è aperta
bool  seismo_writing(void);         // sta salvando un evento

// intensità locale stimata (scala Mercalli, 1..10) dal picco di accelerazione
int seismo_mmi(float pga_mg);
const char *seismo_roman(int mmi);

// registro degli eventi sulla microSD (dal più recente)
typedef struct {
    char when[24];      // "07/10/2026 21:34:05"
    float dur_s;
    float peak_mg;
    int mmi;
} seismo_event_t;
int  seismo_events(seismo_event_t *out, int max);
bool seismo_clear_events(void);
