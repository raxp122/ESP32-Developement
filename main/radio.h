// radio.h — Wi-Fi e Bluetooth non stanno accesi insieme.
// L'ESP32-S3 ha una sola radio: con tutti e due attivi se la dividono a turno e il Wi-Fi
// diventa lento e instabile (gli aggiornamenti da GitHub fallivano). Accenderne uno spegne
// l'altro; di predefinito è acceso il Wi-Fi. Le scansioni (Scanner Wi-Fi/Bluetooth) restano
// possibili: accendono la radio che serve solo finché la schermata è aperta.
#pragma once

void radio_only_wifi(void);   // prima di accendere il Wi-Fi: spegne il Bluetooth (se era acceso)
void radio_only_ble(void);    // prima di accendere il Bluetooth: spegne il Wi-Fi (se era acceso)
