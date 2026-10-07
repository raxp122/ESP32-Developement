#!/usr/bin/env python3
"""Genera i caratteri per lo schermo tondo (AMOLED 1.75", 466x466) partendo dagli stessi
parametri dei caratteri della scheda 3.49" (riga "Opts:" in main/fonts/font_*.c), con le
dimensioni scalate: lo schermo tondo ha pixel più piccoli (circa 1,4 volte più densi).

Uso: python3 tools/gen_fonts_round.py /percorso/lv_font_conv
(lv_font_conv: npm install lv_font_conv@1.5.3). Scrive main/fonts/font_*_r.c.
"""
import os, re, shlex, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = {"font_s": 20, "font_m": 28, "font_l": 46, "font_xl": 96, "font_icon": 60}
conv = sys.argv[1] if len(sys.argv) > 1 else "lv_font_conv"

for name, size in SIZES.items():
    src = os.path.join(ROOT, "main", "fonts", name + ".c")
    with open(src) as f:
        for line in f:
            if "Opts:" in line:
                opts = line.split("Opts:", 1)[1].strip()
                break
    args = shlex.split(opts)
    out = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--size":
            out += ["--size", str(size)]; i += 2; continue
        if a == "-o":
            out += ["-o", os.path.join(ROOT, "main", "fonts", name + "_r.c")]; i += 2; continue
        if a == "--font":
            p = args[i + 1]
            p = re.sub(r"^/home/claude/fonts/", os.path.join(ROOT, "tools", "fonts") + "/", p)
            p = re.sub(r"^/home/claude/lvgl/", os.path.join(ROOT, "components", "lvgl") + "/", p)
            out += ["--font", p]; i += 2; continue
        out.append(a); i += 1
    print(name, size)
    subprocess.run([conv] + out, check=True)
