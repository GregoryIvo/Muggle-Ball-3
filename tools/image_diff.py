#!/usr/bin/env python3
"""Compare two PNG screenshots: the fraction of pixels whose colour channels agree within a
tolerance (docs/VITA.md's "GL back end" M4b comparison between the SDL GPU and GL back ends).

    python3 tools/image_diff.py reference.png candidate.png [--tolerance 8] [--fraction 0.99]

Exits 0 if at least --fraction of pixels are within --tolerance per channel (RGB; alpha is
decoded but not compared, since the renderer always leaves it at the clear value), 1 otherwise.
Prints the percentage and the worst pixel (its location and both images' values there).

Decodes PNG itself (IHDR/IDAT, zlib inflate, the five scanline filters) for 8-bit, non-interlaced
RGB or RGBA images, so the tool needs no third-party package and runs in CI's plain Python; the
PNGs this project writes (stb_image_write, in engine/src/Image.cpp) are exactly that shape.
"""
import argparse
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS_BY_COLOR_TYPE = {0: 1, 2: 3, 4: 2, 6: 4}  # grey, RGB, grey+alpha, RGBA


def _paeth_predictor(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter(raw, width, height, channels):
    """Reverses the PNG scanline filters (0 None, 1 Sub, 2 Up, 3 Average, 4 Paeth)."""
    stride = width * channels
    out = bytearray(stride * height)
    previous = bytearray(stride)
    pos = 0
    for y in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filter_type == 0:
            pass
        elif filter_type == 1:  # Sub: byte + the byte `channels` back in this row
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif filter_type == 2:  # Up: byte + the byte above
            for x in range(stride):
                line[x] = (line[x] + previous[x]) & 0xFF
        elif filter_type == 3:  # Average of the left and above bytes
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                above = previous[x]
                line[x] = (line[x] + (left + above) // 2) & 0xFF
        elif filter_type == 4:  # Paeth predictor over left, above and above-left
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                above = previous[x]
                above_left = previous[x - channels] if x >= channels else 0
                line[x] = (line[x] + _paeth_predictor(left, above, above_left)) & 0xFF
        else:
            raise ValueError(f"unknown PNG filter type {filter_type}")
        out[y * stride:(y + 1) * stride] = line
        previous = line
    return bytes(out)


def decode_png(path):
    """Returns (width, height, channels, pixel_bytes) for an 8-bit, non-interlaced PNG."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path}: not a PNG file")

    width = height = bit_depth = color_type = None
    idat = bytearray()
    pos = 8
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        chunk_type = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 8 + length + 4  # length field + type + data + the CRC that follows
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", chunk)
            if compression != 0 or filter_method != 0:
                raise ValueError(f"{path}: unsupported PNG compression or filter method")
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNGs are not supported")
            if bit_depth != 8:
                raise ValueError(f"{path}: only 8-bit PNGs are supported, not {bit_depth}-bit")
        elif chunk_type == b"IDAT":
            idat += chunk
        elif chunk_type == b"IEND":
            break
    if width is None:
        raise ValueError(f"{path}: no IHDR chunk")
    channels = CHANNELS_BY_COLOR_TYPE.get(color_type)
    if channels is None:
        raise ValueError(f"{path}: unsupported PNG colour type {color_type} (palette images are not supported)")

    raw = zlib.decompress(bytes(idat))
    pixels = _unfilter(raw, width, height, channels)
    return width, height, channels, pixels


def compare(path_a, path_b, tolerance):
    """(fraction_within_tolerance, worst_channel_diff, (x, y), (pixel_a, pixel_b)) comparing the
    RGB channels (the first three, however many a PNG carries) of every pixel."""
    width_a, height_a, channels_a, pixels_a = decode_png(path_a)
    width_b, height_b, channels_b, pixels_b = decode_png(path_b)
    if (width_a, height_a) != (width_b, height_b):
        raise ValueError(f"size mismatch: {path_a} is {width_a}x{height_a}, {path_b} is {width_b}x{height_b}")

    compared = min(channels_a, channels_b, 3)
    total = width_a * height_a
    within = 0
    worst_diff = -1
    worst_at = (0, 0)
    worst_pixels = ((), ())
    for i in range(total):
        offset_a = i * channels_a
        offset_b = i * channels_b
        pixel_max = 0
        for c in range(compared):
            diff = abs(pixels_a[offset_a + c] - pixels_b[offset_b + c])
            pixel_max = max(pixel_max, diff)
        if pixel_max <= tolerance:
            within += 1
        if pixel_max > worst_diff:
            worst_diff = pixel_max
            worst_at = (i % width_a, i // width_a)
            worst_pixels = (tuple(pixels_a[offset_a:offset_a + channels_a]),
                            tuple(pixels_b[offset_b:offset_b + channels_b]))
    fraction = within / total if total else 1.0
    return fraction, worst_diff, worst_at, worst_pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("image_a")
    parser.add_argument("image_b")
    parser.add_argument("--tolerance", type=int, default=8, help="max per-channel difference allowed (default 8)")
    parser.add_argument("--fraction", type=float, default=0.99,
                        help="fraction of pixels that must be within tolerance (default 0.99)")
    args = parser.parse_args()

    try:
        fraction, worst_diff, worst_at, worst_pixels = compare(args.image_a, args.image_b, args.tolerance)
    except (ValueError, OSError, zlib.error) as error:
        sys.exit(f"image_diff: {error}")

    ok = fraction >= args.fraction
    print(f"{fraction * 100:.3f}% of pixels within {args.tolerance} per channel "
          f"(need {args.fraction * 100:.1f}%): {'ok' if ok else 'FAIL'}")
    print(f"worst pixel at {worst_at}, max channel diff {worst_diff}: {worst_pixels[0]} vs {worst_pixels[1]}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
