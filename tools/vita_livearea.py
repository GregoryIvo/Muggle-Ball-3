#!/usr/bin/env python3
"""Make a placeholder LiveArea set for the PlayStation Vita VPK: plain colour, text, no game art.

A Vita app ships four PNGs and a template.xml under sce_sys/ (icon0.png 128x128, pic0.png 960x544,
livearea/contents/bg.png 840x500, livearea/contents/startup.png 280x158). The images must be 8-bit
indexed (PNG colour type 3 with a palette): RGBA PNGs can fail to install or show a blank bubble.
This writes such a set from a title and two colours, with the standard library only, so the committed
placeholder set can be regenerated and checked (tests/test_vita_livearea.py). It knows nothing of the
game's artwork: a personal set built from your own copy of the game is a different, opt-in tool, and
its output is never committed.

    python3 tools/vita_livearea.py --out platform/vita/sce_sys [--title "Muggle Ball 3"]
"""

import argparse
import pathlib
import struct
import zlib

# What the LiveArea wants, by path under sce_sys/: (width, height).
LIVEAREA_FILES = {
    "icon0.png": (128, 128),
    "pic0.png": (960, 544),
    "livearea/contents/bg.png": (840, 500),
    "livearea/contents/startup.png": (280, 158),
}

# The LiveArea layout: a background image with the gate's start-up image over it.
TEMPLATE_XML = """<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
"""

# A 5x7 pixel font for the characters a title needs; each glyph is seven rows of five bits, top row first.
GLYPHS = {
    " ": (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    "0": (0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E),
    "1": (0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "2": (0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F),
    "3": (0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E),
    "4": (0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02),
    "5": (0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E),
    "6": (0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E),
    "7": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    "8": (0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E),
    "9": (0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C),
    "A": (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E),
    "D": (0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F),
    "H": (0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "I": (0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "J": (0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    "M": (0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11),
    "N": (0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11),
    "O": (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "P": (0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    "Q": (0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D),
    "R": (0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    "S": (0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E),
    "T": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "V": (0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A),
    "X": (0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04),
    "Z": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
    "-": (0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    ".": (0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C),
    "!": (0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04),
}
GLYPH_WIDTH, GLYPH_HEIGHT, GLYPH_GAP = 5, 7, 1

# Palette indices.
BACKGROUND, FOREGROUND, ACCENT = 0, 1, 2


def parse_colour(text):
    """'#rrggbb' or 'rrggbb' to an (r, g, b) tuple."""
    text = text.lstrip("#")
    if len(text) != 6:
        raise ValueError(f"colour {text!r} is not rrggbb")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_indexed_png(path, width, height, pixels, palette):
    """An 8-bit indexed PNG (colour type 3): `pixels` is one palette index per pixel, row by row."""
    if len(pixels) != width * height:
        raise ValueError("pixel count does not match the size")
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type: none
        rows.extend(pixels[y * width:(y + 1) * width])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(rgb) for rgb in palette)
    data = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"PLTE", plte) +
            png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def text_size(text, scale):
    width = len(text) * (GLYPH_WIDTH + GLYPH_GAP) * scale - GLYPH_GAP * scale
    return max(width, 0), GLYPH_HEIGHT * scale


def draw_text(pixels, width, text, x0, y0, scale, index):
    """Draws `text` with its top-left corner at (x0, y0), each glyph pixel `scale` pixels wide."""
    for n, ch in enumerate(text):
        glyph = GLYPHS.get(ch.upper(), GLYPHS[" "])
        gx = x0 + n * (GLYPH_WIDTH + GLYPH_GAP) * scale
        for row, bits in enumerate(glyph):
            for col in range(GLYPH_WIDTH):
                if bits & (1 << (GLYPH_WIDTH - 1 - col)):
                    for dy in range(scale):
                        for dx in range(scale):
                            x, y = gx + col * scale + dx, y0 + row * scale + dy
                            if 0 <= x < width:
                                pixels[y * width + x] = index


def largest_scale(text, width, height, margin):
    """The biggest glyph scale whose text fits within the margins, at least 1."""
    scale = 1
    while True:
        w, h = text_size(text, scale + 1)
        if w > width - 2 * margin or h > height - 2 * margin:
            return scale
        scale += 1


def make_image(width, height, title, subtitle):
    """Background colour, a frame in the accent colour, the title centred, and a subtitle below it."""
    pixels = bytearray([BACKGROUND]) * (width * height)
    frame = max(2, min(width, height) // 40)
    for y in range(height):
        for x in range(width):
            if x < frame or y < frame or x >= width - frame or y >= height - frame:
                pixels[y * width + x] = ACCENT
    margin = max(frame * 2, min(width, height) // 10)
    scale = largest_scale(title, width, height, margin)
    tw, th = text_size(title, scale)
    sub_scale = max(1, scale // 2) if subtitle else 0
    sw, sh = text_size(subtitle, sub_scale) if subtitle else (0, 0)
    gap = sub_scale * 2
    total = th + (gap + sh if subtitle else 0)
    y = (height - total) // 2
    draw_text(pixels, width, title, (width - tw) // 2, y, scale, FOREGROUND)
    if subtitle:
        draw_text(pixels, width, subtitle, (width - sw) // 2, y + th + gap, sub_scale, ACCENT)
    return pixels


def make_set(out, title, subtitle, background, foreground, accent):
    palette = [background, foreground, accent]
    for name, (width, height) in LIVEAREA_FILES.items():
        # The icon is too small for a subtitle; it gets the title's initials instead.
        text = "".join(word[0] for word in title.split() if word)[:3] if width <= 128 else title
        pixels = make_image(width, height, text, "" if width <= 128 else subtitle)
        write_indexed_png(out / name, width, height, pixels, palette)
    (out / "livearea" / "contents").mkdir(parents=True, exist_ok=True)
    (out / "livearea" / "contents" / "template.xml").write_text(TEMPLATE_XML, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=pathlib.Path, required=True, help="the sce_sys folder to write")
    parser.add_argument("--title", default="MUGGLE BALL 3")
    parser.add_argument("--subtitle", default="PLACEHOLDER")
    parser.add_argument("--background", default="#0d0d1a", help="rrggbb")
    parser.add_argument("--foreground", default="#f2e9c8", help="rrggbb")
    parser.add_argument("--accent", default="#c8893c", help="rrggbb")
    args = parser.parse_args()
    make_set(args.out, args.title, args.subtitle, parse_colour(args.background), parse_colour(args.foreground),
             parse_colour(args.accent))
    for name in LIVEAREA_FILES:
        print(f"{args.out / name}")
    print(f"{args.out / 'livearea' / 'contents' / 'template.xml'}")


if __name__ == "__main__":
    main()
