#!/usr/bin/env python3
"""tools/image_diff.py over tiny synthetic PNGs (encoded here with the standard library, the
mirror image of the decoder under test)."""
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import image_diff as D  # noqa: E402


def write_png(path, width, height, channels, pixels):
    """A minimal, filter-0 (None) PNG encoder: just enough to round-trip through decode_png."""
    color_type = {1: 0, 2: 4, 3: 2, 4: 6}[channels]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    stride = width * channels
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0: None
        raw += pixels[y * stride:(y + 1) * stride]
    idat = zlib.compress(bytes(raw))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    with open(path, "wb") as f:
        f.write(D.PNG_SIGNATURE)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def solid_rgb(width, height, color):
    return bytes(color) * (width * height)


class ImageDiffTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def path(self, name):
        return str(pathlib.Path(self.tmp.name) / name)

    def test_identical_images_match_fully(self):
        a, b = self.path("a.png"), self.path("b.png")
        write_png(a, 4, 4, 3, solid_rgb(4, 4, (100, 150, 200)))
        write_png(b, 4, 4, 3, solid_rgb(4, 4, (100, 150, 200)))
        fraction, worst_diff, _, _ = D.compare(a, b, tolerance=8)
        self.assertEqual(fraction, 1.0)
        self.assertEqual(worst_diff, 0)

    def test_difference_within_tolerance_still_matches(self):
        a, b = self.path("a.png"), self.path("b.png")
        write_png(a, 2, 2, 3, solid_rgb(2, 2, (100, 100, 100)))
        write_png(b, 2, 2, 3, solid_rgb(2, 2, (107, 93, 108)))  # off by 7, 7, 8
        fraction, worst_diff, _, _ = D.compare(a, b, tolerance=8)
        self.assertEqual(fraction, 1.0)
        self.assertEqual(worst_diff, 8)

    def test_one_outlier_pixel_is_found_and_counted(self):
        a, b = self.path("a.png"), self.path("b.png")
        pixels_a = bytearray(solid_rgb(3, 1, (0, 0, 0)))
        pixels_b = bytearray(solid_rgb(3, 1, (0, 0, 0)))
        pixels_b[3:6] = bytes((255, 255, 255))  # the middle pixel is wildly different
        write_png(a, 3, 1, 3, bytes(pixels_a))
        write_png(b, 3, 1, 3, bytes(pixels_b))
        fraction, worst_diff, worst_at, worst_pixels = D.compare(a, b, tolerance=8)
        self.assertAlmostEqual(fraction, 2 / 3)
        self.assertEqual(worst_diff, 255)
        self.assertEqual(worst_at, (1, 0))
        self.assertEqual(worst_pixels, ((0, 0, 0), (255, 255, 255)))

    def test_rgba_ignores_alpha(self):
        a, b = self.path("a.png"), self.path("b.png")
        write_png(a, 1, 1, 4, bytes((10, 20, 30, 255)))
        write_png(b, 1, 1, 4, bytes((10, 20, 30, 0)))  # alpha differs wildly, RGB does not
        fraction, worst_diff, _, _ = D.compare(a, b, tolerance=0)
        self.assertEqual(fraction, 1.0)
        self.assertEqual(worst_diff, 0)

    def test_size_mismatch_raises(self):
        a, b = self.path("a.png"), self.path("b.png")
        write_png(a, 2, 2, 3, solid_rgb(2, 2, (0, 0, 0)))
        write_png(b, 3, 3, 3, solid_rgb(3, 3, (0, 0, 0)))
        with self.assertRaises(ValueError):
            D.compare(a, b, tolerance=8)

    def test_every_filter_type_round_trips(self):
        # A gradient so Sub/Up/Average/Paeth all do real work when zlib picks a representation;
        # since write_png always uses filter 0, this instead checks decode_png directly against
        # each filter type by building the filtered bytes by hand.
        width, height, channels = 4, 3, 3
        base = bytearray()
        for y in range(height):
            for x in range(width):
                base += bytes(((x * 17 + y * 5) & 0xFF, (x * 3) & 0xFF, (y * 11) & 0xFF))
        for filter_type in range(5):
            raw = bytearray()
            stride = width * channels
            previous = bytearray(stride)
            for y in range(height):
                line = bytearray(base[y * stride:(y + 1) * stride])
                filtered = bytearray(stride)
                for x in range(stride):
                    left = line[x - channels] if x >= channels else 0
                    above = previous[x]
                    above_left = previous[x - channels] if x >= channels else 0
                    if filter_type == 0:
                        filtered[x] = line[x]
                    elif filter_type == 1:
                        filtered[x] = (line[x] - left) & 0xFF
                    elif filter_type == 2:
                        filtered[x] = (line[x] - above) & 0xFF
                    elif filter_type == 3:
                        filtered[x] = (line[x] - (left + above) // 2) & 0xFF
                    else:
                        filtered[x] = (line[x] - D._paeth_predictor(left, above, above_left)) & 0xFF
                raw.append(filter_type)
                raw += filtered
                previous = line
            unfiltered = D._unfilter(bytes(raw), width, height, channels)
            self.assertEqual(unfiltered, bytes(base), f"filter type {filter_type} did not round-trip")


if __name__ == "__main__":
    unittest.main()
