#!/bin/sh
# Simulatore dell'interfaccia: compila LVGL e le schermate del Gadget per il PC (gcc) e
# salva le immagini in tools/sim/png/ (tondo_* per l'AMOLED 1.75, 349_* per la 3.49).
# Serve prima un `idf.py build` (per build/config/sdkconfig.h) e Python con Pillow.
set -e
cd "$(dirname "$0")"
R=../..
M=$R/main
LV=$R/components/lvgl
CFLAGS="-w -O1 -I$LV -I$LV/src -Iinc -DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"sdkconfig_host.h\""
mkdir -p obj out png
if [ ! -f obj/liblvgl.a ]; then
    for f in $(find $LV/src -name '*.c'); do
        o=obj/$(echo "$f" | sed "s|$LV/src/||; s|/|_|g; s|\.c$|.o|")
        gcc $CFLAGS -c "$f" -o "$o" &
    done
    wait
    ar rcs obj/liblvgl.a obj/*.o
fi
SRC="$M/ui.c $M/menu.c $M/apps/home.c $M/apps/keyboard.c $M/apps/app_ota.c $M/apps/app_clock.c $M/apps/app_wifiscan.c $M/apps/app_portal.c $M/apps/app_settings.c $M/apps/app_seismo.c $M/seismo.c $M/apps/app_wifimap.c $M/wifimap.c $M/wifimap_html.c $M/fonts_sel.c $M/fonts/*.c"
gcc $CFLAGS -DSEISMO_SIM -I$M -I$M/apps $SRC stubs.c sim_main.c obj/liblvgl.a -lm -o obj/sim
./obj/sim round
./obj/sim lcd
python3 - <<'PY'
from PIL import Image, ImageDraw
import glob, os
for f in sorted(glob.glob('out/*.ppm')):
    im = Image.open(f).convert('RGB')
    if 'tondo' in f:   # fuori dal cerchio non c'è schermo
        mask = Image.new('L', im.size, 0)
        ImageDraw.Draw(mask).ellipse((0, 0, im.size[0]-1, im.size[1]-1), fill=255)
        bg = Image.new('RGB', im.size, (60, 60, 64))
        bg.paste(im, (0, 0), mask)
        ImageDraw.Draw(bg).ellipse((0, 0, im.size[0]-1, im.size[1]-1), outline=(110, 110, 118), width=2)
        im = bg
    im.save('png/' + os.path.basename(f)[:-4] + '.png')
PY
echo "immagini in tools/sim/png"
