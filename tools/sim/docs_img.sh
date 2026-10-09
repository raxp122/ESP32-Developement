#!/bin/sh
# Copia in docs/img/ le schermate del simulatore usate nel README (prima: tools/sim/run.sh).
# Il tondo solo per le app che sul tondo si aprono già.
set -e
cd "$(dirname "$0")"
D=../../docs/img
mkdir -p $D
# nome nel README : scena
for s in \
    home:1_home impostazioni:2_impostazioni diagnostica:48_diagnostica aggiornamento:9_aggiornamento \
    orologio:12_orologio sismografo:15_sismo_evento tester_wifi:19_tester_misura scanner_wifi:5_reti \
    snake:22_snake_gioco scacchi:26_scacchi_partita scacchi_analisi:30_scacchi_analisi \
    morse_ascolta:41_morse_ascolta morse_batti:43_morse_batti; do
    cp png/349_${s#*:}.png $D/349_${s%%:*}.png
    cp png/tondo_${s#*:}.png $D/tondo_${s%%:*}.png
done
for s in \
    cerca:120_cerca 8ball:100_8ball 8ball_risposta:101_8ball_risposta \
    accordatore_chitarra:103_accordatore_chitarra accordatore_cromatico:104_accordatore_cromatico \
    appunti:106_appunti_menu appunti_codici:107_appunti_codici berciometro:105_berciometro \
    dadi_preferito:92_dadi_palla_di_fuoco dadi_tiro:94_dadi_dardo_tiro \
    livella_piano:109_livella_piano livella_lato:110_livella_lato orologio_polipetto:47_orologio_polipetto \
    orologio_scacchi:112_orologio_scacchi_partita polipetto:50_polipetto polipetto_stella:54_stella \
    polipetto_negozio:64_negozio_cuffia polipetto_visita:68c_amico_visita q20:114_q20_domanda \
    radar:80_radar_guarda radar_handshake:82_radar_handshake scanner_bt:121_scanner_bt \
    spada_laser:116_spada_accesa theremin:117_theremin torcia:119_torcia_colore; do
    cp png/349_${s#*:}.png $D/349_${s%%:*}.png
done
echo "schermate in docs/img"
