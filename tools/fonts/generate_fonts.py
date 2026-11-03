#!/usr/bin/env python3
"""
Regenerates the LVGL fonts used by SimpleVescDisplay.

The fonts are compiled into the sketch (firmware/SimpleVescDisplay/src/ui/fonts),
so the firmware does not depend on which built-in fonts are enabled in lv_conf.h.

Requirements:
  * Node.js + lv_font_conv  (npm install lv_font_conv@1.5.3)
  * The LVGL source tree (for Montserrat-Medium.ttf and the FontAwesome symbol font,
    both found in lvgl/scripts/built_in_font/)

Usage:
  python generate_fonts.py --lvgl <path-to-lvgl> [--conv <path-to-lv_font_conv>]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "firmware", "SimpleVescDisplay", "src", "ui", "fonts"))

# Same symbol list LVGL uses for its built-in fonts (LV_SYMBOL_*)
SYMBOLS = ("61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,"
           "61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,"
           "61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,"
           "61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,"
           "62189,62810,63426,63650")

UI_SIZES = [12, 14, 16, 20, 24, 28, 32]
SPEED_SIZES = [120, 170]          # 2.8" (320x240) and 3.5" (480x320) speed digits
SPEED_RANGE = "0x2D-0x2E,0x30-0x39"  # "-", "." and 0-9


def run(cmd):
    print(" ".join(cmd))
    subprocess.check_call(cmd)


def fix_include(path):
    """lv_font_conv emits '#include "lvgl/lvgl.h"' which fails on Arduino; use "lvgl.h"."""
    src = open(path, encoding="utf-8").read()
    src = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE\s*#include "lvgl.h"\s*#else\s*#include "lvgl/lvgl.h"\s*#endif',
                 '#include "lvgl.h"\n#include "svd_font_conf.h"  /* per-board font selection */', src)
    # keep the header comment free of local absolute paths
    src = re.sub(r'(--font|-o) \S*?([^\\/\s]+\.(?:ttf|woff|c))', r'\1 \2', src)
    src = re.sub(r'(-o) .*?([^\\/]+\.c)\n', r'\1 \2\n', src)
    open(path, "w", encoding="utf-8", newline="\n").write(src)


def tighten_line_height(path):
    """Shrink line_height/base_line to the real glyph bounds (digits only font),
    so a label using the huge speed font is not much taller than the digits."""
    src = open(path, encoding="utf-8").read()
    glyphs = re.findall(r'\{\.bitmap_index = \d+, \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}', src)
    tops, bottoms = [], []
    for box_w, box_h, ofs_x, ofs_y in glyphs:
        box_h, ofs_y = int(box_h), int(ofs_y)
        if box_h == 0:
            continue
        tops.append(box_h + ofs_y)
        bottoms.append(ofs_y)
    top, bottom = max(tops), min(bottoms)
    line_height = top - bottom
    base_line = -bottom
    src = re.sub(r'\.line_height = \d+,', '.line_height = %d,' % line_height, src)
    src = re.sub(r'\.base_line = -?\d+,', '.base_line = %d,' % base_line, src)
    open(path, "w", encoding="utf-8", newline="\n").write(src)
    print("  tightened: line_height=%d base_line=%d" % (line_height, base_line))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lvgl", required=True, help="path to the LVGL library folder")
    ap.add_argument("--conv", default=shutil.which("lv_font_conv") or "lv_font_conv")
    args = ap.parse_args()

    font_dir = os.path.join(args.lvgl, "scripts", "built_in_font")
    ttf = os.path.join(font_dir, "Montserrat-Medium.ttf")
    fa = os.path.join(font_dir, "FontAwesome5-Solid+Brands+Regular.woff")
    for f in (ttf, fa):
        if not os.path.isfile(f):
            sys.exit("missing font file: " + f)

    os.makedirs(OUT_DIR, exist_ok=True)
    conv = [args.conv]
    if conv[0].lower().endswith((".cmd", ".bat")):
        conv = ["cmd", "/c", conv[0]]

    for size in UI_SIZES:
        name = "svd_font_%d" % size
        out = os.path.join(OUT_DIR, name + ".c")
        run(conv + ["--no-compress", "--no-prefilter", "--bpp", "4", "--size", str(size),
                    "--font", ttf, "-r", "0x20-0x7E,0xB0,0x2022",
                    "--font", fa, "-r", SYMBOLS,
                    "--format", "lvgl", "--lv-font-name", name, "--force-fast-kern-format", "-o", out])
        fix_include(out)

    for size in SPEED_SIZES:
        name = "svd_font_speed_%d" % size
        out = os.path.join(OUT_DIR, name + ".c")
        run(conv + ["--no-compress", "--no-prefilter", "--bpp", "4", "--size", str(size),
                    "--font", ttf, "-r", SPEED_RANGE,
                    "--format", "lvgl", "--lv-font-name", name, "-o", out])
        fix_include(out)
        tighten_line_height(out)


if __name__ == "__main__":
    main()
