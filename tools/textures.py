#!/usr/bin/env python3
"""Convert .dds textures to .png files beside them (same name, RGBA8).

Usage:
    python3 tools/textures.py DIR [DIR ...]

Every .dds under the given folders is decoded (top mip level only) and written
as a PNG, so the runtime only has to read JPEG and PNG. Supported: DXT1 (with
its 1-bit alpha mode), DXT3, DXT5 and uncompressed 8/16/24/32-bit RGB(A) of any
channel mask layout, which covers every file in the shipped game.
"""
import argparse
import functools
import os
import pathlib
import struct
import sys
import time
import zlib

HEADER_SIZE = 128  # "DDS " magic + the 124-byte DDS_HEADER
DDPF_ALPHAPIXELS = 0x1
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class DdsError(ValueError):
    pass


# --- DDS header -----------------------------------------------------------------

def read_header(data):
    """-> (width, height, pixel format flags, FourCC, bit count, [R, G, B, A masks])."""
    if len(data) < HEADER_SIZE or data[:4] != b"DDS ":
        raise DdsError("not a DDS file")
    size, _flags, height, width = struct.unpack_from("<4I", data, 4)
    pf_size, pf_flags, fourcc, bit_count, r, g, b, a = struct.unpack_from("<8I", data, 76)
    if size != 124 or pf_size != 32:
        raise DdsError("unexpected DDS header size")
    if width == 0 or height == 0:
        raise DdsError("empty image")
    return width, height, pf_flags, fourcc.to_bytes(4, "little"), bit_count, [r, g, b, a]


# --- DXT (BC1/BC2/BC3) ----------------------------------------------------------

def _rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2


def _palette(c0, c1, one_bit_alpha):
    """The four RGBA colours of a colour block, as 4-byte strings."""
    p0, p1 = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or not one_bit_alpha:
        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
        return [bytes(p + (255,)) for p in (p0, p1, p2, p3)]
    # DXT1 with c0 <= c1: the midpoint, and a transparent fourth entry.
    p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
    return [bytes(p + (255,)) for p in (p0, p1, p2)] + [b"\0\0\0\0"]


def _alpha_palette(a0, a1):
    if a0 > a1:
        return [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    return [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]


def _decode_dxt(data, width, height, fourcc):
    one_bit = fourcc == b"DXT1"  # only DXT1 colour blocks may select the transparent mode
    bw, bh = (width + 3) // 4, (height + 3) // 4
    block = 8 if one_bit else 16
    if len(data) < HEADER_SIZE + bw * bh * block:
        raise DdsError("pixel data is truncated")

    colour_cache, alpha_cache = {}, {}

    def colour_rows(offset):
        c0, c1, bits = struct.unpack_from("<HHI", data, offset)
        rows = colour_cache.get((c0, c1, bits))
        if rows is None:
            pal = _palette(c0, c1, one_bit)
            rows = colour_cache[(c0, c1, bits)] = tuple(
                b"".join(pal[(bits >> s) & 3] for s in range(8 * r, 8 * r + 8, 2)) for r in range(4))
        return rows

    def alpha_rows_dxt3(offset):  # 4 bits per pixel, a 16-bit word per row
        rows = []
        for word in struct.unpack_from("<4H", data, offset):
            row = alpha_cache.get(word)
            if row is None:
                row = alpha_cache[word] = bytes(((word >> s) & 15) * 17 for s in (0, 4, 8, 12))
            rows.append(row)
        return rows

    def alpha_rows_dxt5(offset):  # two endpoints and 3-bit indices
        a0, a1, lo, mid, hi = struct.unpack_from("<BB3H", data, offset)
        bits = lo | mid << 16 | hi << 32
        rows = alpha_cache.get((a0, a1, bits))
        if rows is None:
            pal = _alpha_palette(a0, a1)
            rows = alpha_cache[(a0, a1, bits)] = [
                bytes(pal[(bits >> s) & 7] for s in range(12 * r, 12 * r + 12, 3)) for r in range(4)]
        return rows

    alpha_rows = None if one_bit else (alpha_rows_dxt3 if fourcc == b"DXT3" else alpha_rows_dxt5)

    colour, alpha = bytearray(), bytearray()
    offset = HEADER_SIZE
    for _ in range(bh):
        crows = ([], [], [], [])
        arows = ([], [], [], [])
        for _ in range(bw):
            if alpha_rows:
                for r, row in enumerate(alpha_rows(offset)):
                    arows[r].append(row)
                offset += 8
            for r, row in enumerate(colour_rows(offset)):
                crows[r].append(row)
            offset += 8
        for r in range(4):
            colour += b"".join(crows[r])
            alpha += b"".join(arows[r])
    if alpha_rows:
        colour[3::4] = alpha
    return _crop(colour, bw * 4, width, height)


def _crop(rgba, padded_width, width, height):
    if padded_width == width and len(rgba) == width * height * 4:
        return bytes(rgba)
    stride, row = padded_width * 4, width * 4
    return b"".join(rgba[y * stride:y * stride + row] for y in range(height))


# --- Uncompressed RGB(A) --------------------------------------------------------

def _to_8bit(value, bits):
    """Scale a `bits`-wide channel value to 0..255 by bit replication (0xF -> 0xFF)."""
    if bits >= 8:
        return value >> (bits - 8)
    out, filled = value, bits
    while filled < 8:
        out, filled = out << bits | value, filled + bits
    return out >> (filled - 8)


def _channel(mask, missing):
    """-> (shift, value mask, lookup table) for one channel; `missing` fills absent channels."""
    if mask == 0:
        return 0, 0, [missing]
    shift = (mask & -mask).bit_length() - 1
    bits = (mask >> shift).bit_length()
    if mask >> shift != (1 << bits) - 1:
        raise DdsError(f"channel mask {mask:#x} is not contiguous")
    return shift, (1 << bits) - 1, [_to_8bit(v, bits) for v in range(1 << bits)]


def _mask_layout(masks):
    """Channel names ordered from the most significant bit, e.g. 'A4R4G4B4'."""
    present = sorted(((m, n) for m, n in zip(masks, "RGBA") if m), reverse=True)
    return "".join(f"{n}{bin(m).count('1')}" for m, n in present)


@functools.lru_cache(maxsize=None)
def _pixel_converter(bit_count, masks):
    """-> function from a raw pixel value to its 4 RGBA bytes."""
    (sr, mr, lr), (sg, mg, lg), (sb, mb, lb), (sa, ma, la) = (
        _channel(m, fill) for m, fill in zip(masks, (0, 0, 0, 255)))

    def to_rgba(v):
        return bytes((lr[v >> sr & mr], lg[v >> sg & mg], lb[v >> sb & mb], la[v >> sa & ma]))

    # Up to 16 bits a table over every possible pixel value beats converting each pixel.
    if bit_count > 16:
        return to_rgba
    return [to_rgba(v) for v in range(1 << bit_count)].__getitem__


def _decode_masked(data, width, height, bit_count, masks):
    if bit_count not in (8, 16, 24, 32):
        raise DdsError(f"unsupported bit count {bit_count}")
    step = bit_count // 8
    count = width * height
    if len(data) < HEADER_SIZE + count * step:
        raise DdsError("pixel data is truncated")
    if step == 3:
        values = [int.from_bytes(data[i:i + 3], "little")
                  for i in range(HEADER_SIZE, HEADER_SIZE + count * 3, 3)]
    else:
        values = struct.unpack_from(f"<{count}{'BHI'[step // 2]}", data, HEADER_SIZE)
    return b"".join(map(_pixel_converter(bit_count, masks), values))


# --- DDS -> RGBA ----------------------------------------------------------------

def decode_dds(data):
    """DDS file contents -> (width, height, rgba bytes, format name). Top mip level only."""
    width, height, pf_flags, fourcc, bit_count, masks = read_header(data)
    if pf_flags & DDPF_FOURCC:
        if fourcc not in (b"DXT1", b"DXT3", b"DXT5"):
            raise DdsError(f"unsupported compression {fourcc!r}")
        return width, height, _decode_dxt(data, width, height, fourcc), fourcc.decode()
    if pf_flags & DDPF_RGB:
        if not pf_flags & DDPF_ALPHAPIXELS:
            masks[3] = 0  # no alpha channel: opaque
        masks = tuple(masks)
        return (width, height, _decode_masked(data, width, height, bit_count, masks),
                _mask_layout(masks))
    raise DdsError(f"unsupported pixel format flags {pf_flags:#x}")


# --- PNG ------------------------------------------------------------------------

def encode_png(width, height, rgba):
    """RGBA8 PNG, no filtering."""
    stride = width * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    return (PNG_SIGNATURE
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6))
            + chunk(b"IEND", b""))


# --- Folders --------------------------------------------------------------------

def convert_file(dds_path):
    """Writes <stem>.png beside the file; returns a dict describing the conversion."""
    dds_path = pathlib.Path(dds_path)
    start = time.perf_counter()
    try:
        width, height, rgba, fmt = decode_dds(dds_path.read_bytes())
    except DdsError as e:
        raise DdsError(f"{dds_path}: {e}") from None
    dds_path.with_suffix(".png").write_bytes(encode_png(width, height, rgba))
    return {"path": str(dds_path), "width": width, "height": height, "format": fmt,
            "seconds": time.perf_counter() - start}


def convert_all(dirs):
    """Converts every .dds under the folders; returns statistics."""
    start = time.perf_counter()
    paths = sorted({pathlib.Path(root) / name
                    for d in dirs for root, _, names in os.walk(d)
                    for name in names if name.lower().endswith(".dds")})
    converted = [convert_file(p) for p in paths]
    formats = {}
    for c in converted:
        formats[c["format"]] = formats.get(c["format"], 0) + 1
    return {"files": len(converted), "formats": formats, "converted": converted,
            "seconds": time.perf_counter() - start}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+", type=pathlib.Path, help="folders searched recursively for .dds")
    args = ap.parse_args()
    try:
        stats = convert_all(args.dirs)
    except (OSError, DdsError) as e:
        sys.exit(str(e))
    for c in stats["converted"]:
        print(f"{c['path']}: {c['width']}x{c['height']} {c['format']} ({c['seconds']:.2f}s)")
    formats = ", ".join(f"{n} x {f}" for f, n in sorted(stats["formats"].items()))
    print(f"textures: {stats['files']} converted ({formats or 'none'}) in {stats['seconds']:.1f}s")


if __name__ == "__main__":
    main()
