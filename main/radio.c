// radio.c — esclusione tra Wi-Fi e Bluetooth (vedi radio.h). Dal task dell'interfaccia.
#include "radio.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ble_mgr.h"
#include "ui.h"

void radio_only_wifi(void)
{
    if (!g_set.ble_on) return;
    g_set.ble_on = false;
    settings_save();
    ble_mgr_apply();
    ui_toast("Bluetooth spento: la radio passa al Wi-Fi");
}

void radio_only_ble(void)
{
    if (!g_set.wifi_on) return;
    g_set.wifi_on = false;
    settings_save();
    wifi_mgr_apply();
    ui_toast("Wi-Fi spento: la radio passa al Bluetooth");
}
