#!/usr/bin/env python3
"""
Renders the default boot splash (480x320 and 320x240 PNG) and the C header
firmware/SimpleVescDisplay/src/ui/images/splash_png.h

  pip install pillow
  python make_splash.py --font <path>/Montserrat-Medium.ttf

Montserrat-Medium.ttf ships with LVGL (lvgl/scripts/built_in_font/).
Change the texts / colours below to make your own splash, or simply put a
splash.png / splash.jpg on the SD card (see README) - no rebuild needed.
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.normpath(os.path.join(HERE, "..", "..", "firmware", "SimpleVescDisplay", "src", "ui", "images",
                                       "splash_png.h"))

TITLE = ("Simple", "Vesc", "Display")      # middle part is drawn in the accent colour
SUBTITLE = ("made by ", "NetworkDir")
ACCENT = (0, 180, 255)
ACCENT2 = (0, 90, 255)
SS = 4                                     # supersampling factor


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        c = lerp(top, bottom, y / (h - 1))
        for x in range(w):
            px[x, y] = c
    return img


def text_size(draw, text, font, stroke=0):
    l, t, r, b = draw.textbbox((0, 0), text, font=font, stroke_width=stroke)
    return r - l, b - t, l, t


def render(W, H, font_path):
    w, h = W * SS, H * SS
    img = gradient(w, h, (2, 6, 12), (5, 22, 40))

    # soft glow behind the logo
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    cx, cy = w // 2, int(h * 0.30)
    gd.ellipse([cx - w * 0.38, cy - h * 0.22, cx + w * 0.38, cy + h * 0.22], fill=ACCENT + (60,))
    glow = glow.filter(ImageFilter.GaussianBlur(w // 14))
    img.paste(glow, (0, 0), glow)

    d = ImageDraw.Draw(img)

    # road perspective at the bottom (same motif as the dashboard)
    vy = int(h * 0.80)
    road = (28, 58, 85)
    lw = max(2, int(SS * 1.6))
    d.line([(w * 0.08, h), (w * 0.46, vy)], fill=road, width=lw)
    d.line([(w * 0.92, h), (w * 0.54, vy)], fill=road, width=lw)
    seg = (h - vy) / 7
    for i in range(3):
        y0 = h - seg * (i * 2) - seg * 0.4
        d.line([(w / 2, y0), (w / 2, y0 - seg * 0.7)], fill=road, width=max(1, lw - SS // 2))

    # ---- "VESC" wordmark: heavy italic letters with a blue gradient + speed stripes ----
    size = int(h * 0.30)
    font = ImageFont.truetype(font_path, size)
    stroke = max(1, int(size * 0.055))
    tw, th, tl, tt = text_size(d, "VESC", font, stroke)
    mask = Image.new("L", (tw + 2 * stroke + size, th + 2 * stroke), 0)
    md = ImageDraw.Draw(mask)
    md.text((size // 2 - tl + stroke, -tt + stroke), "VESC", font=font, fill=255, stroke_width=stroke,
            stroke_fill=255)
    shear = 0.22
    mask = mask.transform(mask.size, Image.AFFINE, (1, shear, -shear * mask.size[1] * 0.5, 0, 1, 0),
                          resample=Image.BICUBIC)
    fill = Image.new("RGB", mask.size)
    fp = fill.load()
    for x in range(mask.size[0]):
        c = lerp((120, 225, 255), ACCENT2, x / (mask.size[0] - 1))
        for y in range(mask.size[1]):
            fp[x, y] = c
    stripes_w = int(size * 0.75)
    total_w = stripes_w + mask.size[0]
    ox = (w - total_w) // 2 + stripes_w
    oy = int(h * 0.30) - mask.size[1] // 2
    img.paste(fill, (ox, oy), mask)

    # three speed stripes in front of the "V"
    sh = mask.size[1]
    for i, (frac, alpha) in enumerate(((1.0, 255), (0.72, 190), (0.45, 120))):
        sw = int(size * 0.09)
        gap = int(size * 0.20)
        x1 = ox - gap * (i + 1) + int(size * 0.25)
        top, bot = oy + int(sh * (1 - frac) * 0.5) + stroke, oy + sh - int(sh * (1 - frac) * 0.5) - stroke
        slant = (bot - top) * shear
        poly = [(x1 + slant, top), (x1 + slant + sw, top), (x1 + sw, bot), (x1, bot)]
        layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(layer).polygon(poly, fill=ACCENT + (alpha,))
        img.paste(layer, (0, 0), layer)

    # thin accent line
    ly = int(h * 0.50)
    line = Image.new("RGBA", (w, max(2, SS)), (0, 0, 0, 0))
    lp = line.load()
    for x in range(w):
        a = int(255 * max(0.0, 1 - abs(x - w / 2) / (w * 0.32)))
        for y in range(line.size[1]):
            lp[x, y] = ACCENT + (a,)
    img.paste(line, (0, ly), line)

    # ---- title ----
    tfont = ImageFont.truetype(font_path, int(h * 0.105))
    parts = [(TITLE[0], (255, 255, 255)), (TITLE[1], ACCENT), (TITLE[2], (255, 255, 255))]
    widths = [text_size(d, p, tfont)[0] for p, _ in parts]
    x = (w - sum(widths)) // 2
    ty = int(h * 0.555)
    for (p, col), pw in zip(parts, widths):
        d.text((x, ty), p, font=tfont, fill=col, stroke_width=SS // 2, stroke_fill=col)
        x += pw

    # ---- subtitle ----
    sfont = ImageFont.truetype(font_path, int(h * 0.058))
    s0w = text_size(d, SUBTITLE[0], sfont)[0]
    s1w = text_size(d, SUBTITLE[1], sfont)[0]
    x = (w - s0w - s1w) // 2
    sy = int(h * 0.695)
    d.text((x, sy), SUBTITLE[0], font=sfont, fill=(130, 150, 175))
    d.text((x + s0w, sy), SUBTITLE[1], font=sfont, fill=(235, 242, 250))

    out = img.resize((W, H), Image.LANCZOS)
    return out


def png_bytes(img):
    q = img.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    buf = io.BytesIO()
    q.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def c_array(name, data):
    lines = ["static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True, help="Montserrat-Medium.ttf")
    ap.add_argument("--preview-dir", default=HERE, help="where to save the preview PNGs")
    args = ap.parse_args()

    large = render(480, 320, args.font)
    small = render(320, 240, args.font)
    large.save(os.path.join(args.preview_dir, "splash_480x320.png"))
    small.save(os.path.join(args.preview_dir, "splash_320x240.png"))
    dl, ds = png_bytes(large), png_bytes(small)

    header = "\n".join([
        "// Generated by tools/images/make_splash.py - do not edit by hand.",
        "// Default boot splash. To use your own image without rebuilding, put",
        "// splash.png or splash.jpg on the SD card (any size, it is scaled to fit).",
        "// To compile your own image in, run tools/images/png2header.py.",
        "#pragma once",
        "#include <stdint.h>",
        "#include \"../../config/board_config.h\"",
        "",
        "#if defined(UI_LARGE)",
        c_array("splash_png", dl),
        "#else",
        c_array("splash_png", ds),
        "#endif",
        "",
    ])
    open(HEADER, "w", newline="\n").write(header)
    print("480x320: %d bytes, 320x240: %d bytes -> %s" % (len(dl), len(ds), HEADER))


if __name__ == "__main__":
    sys.exit(main())
