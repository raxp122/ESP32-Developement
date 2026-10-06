// clip_ble.c — servizio Bluetooth che riceve i testi dal PC e li mette negli Appunti.
// È un "profilo" registrato in ble_mgr (vedi ble_mgr.h): aggiunge un servizio GATT con
//   - caratteristica "in"    (scrittura): il PC manda il testo, a pezzi, chiuso da un byte 0
//   - caratteristica "stato"  (notifica):  il Gadget risponde quante voci ha in memoria
// Niente cifratura: il browser (Web Bluetooth) si collega e scrive senza associazione.
//
// UUID (da riportare identici nella pagina web):
//   servizio   6e6c0001-b5a3-f393-e0a9-e50e24dcca9e
//   in         6e6c0002-b5a3-f393-e0a9-e50e24dcca9e
//   stato      6e6c0003-b5a3-f393-e0a9-e50e24dcca9e
#include "clips.h"
#include "ble_mgr.h"
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "clip_ble";

#define U(b2, b3) BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                                   0x93, 0xf3, 0xa3, 0xb5, b2, b3, 0x6c, 0x6e)
static const ble_uuid128_t UUID_SVC = U(0x01, 0x00);
static const ble_uuid128_t UUID_IN  = U(0x02, 0x00);
static const ble_uuid128_t UUID_ST  = U(0x03, 0x00);

static uint16_t st_handle, st_conn = BLE_HS_CONN_HANDLE_NONE;
static char asm_buf[CLIP_MAX_LEN];
static int  asm_len;
static uint16_t asm_conn = BLE_HS_CONN_HANDLE_NONE;

static void notify_count(void)
{
    if (st_conn == BLE_HS_CONN_HANDLE_NONE || !st_handle) return;
    uint16_t c = (uint16_t)clips_count();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&c, sizeof(c));
    if (om) ble_gatts_notify_custom(st_conn, st_handle, om);
}

static void commit(void)
{
    if (asm_len > 0) { clips_add(asm_buf, asm_len); notify_count(); }
    asm_len = 0;
}

static int access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint16_t c = (uint16_t)clips_count();
        return os_mbuf_append(ctxt->om, &c, sizeof(c)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    // solo chi si è collegato dalla finestra "Ricevi dal PC" (o è associato) può scrivere:
    // altrimenti chiunque nei paraggi potrebbe riempire la lista di testi da digitare
    if (!ble_mgr_conn_trusted(conn)) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

    // un nuovo mittente azzera quello che era rimasto a metà
    if (conn != asm_conn) { asm_conn = conn; asm_len = 0; }
    int total = OS_MBUF_PKTLEN(ctxt->om);
    for (int off = 0; off < total; ) {
        uint8_t chunk[128];
        int got = total - off < (int)sizeof(chunk) ? total - off : (int)sizeof(chunk);
        if (os_mbuf_copydata(ctxt->om, off, got, chunk) != 0) break;
        for (int i = 0; i < got; i++) {
            char ch = (char)chunk[i];
            if (ch == 0) commit();                           // 0 = fine di un testo
            else {
                if (asm_len >= CLIP_MAX_LEN - 1) commit();   // troppo lungo: chiudi e ricomincia
                asm_buf[asm_len++] = ch;
            }
        }
        off += got;
    }
    return 0;
}

static const struct ble_gatt_svc_def svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = &UUID_IN.u, .access_cb = access,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = &UUID_ST.u, .access_cb = access, .val_handle = &st_handle,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
            {0},
        },
    },
    {0},
};

static void on_conn(const ble_conn_t *c, bool up)
{
    if (c->central) return;   // ci interessa chi si collega al Gadget (il PC)
    if (up) st_conn = c->handle;
    else if (st_conn == c->handle) { st_conn = BLE_HS_CONN_HANDLE_NONE; asm_len = 0; }
}

void clip_ble_register(void)
{
    static const ble_profile_t p = {.name = "Appunti", .svcs = svcs, .on_conn = on_conn};
    if (!ble_mgr_register_profile(&p)) ESP_LOGE(TAG, "profilo non registrato");
}
