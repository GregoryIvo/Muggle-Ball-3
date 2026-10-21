#!/usr/bin/env python3
"""Decode synthetic DDS files with tools/textures.py and check every pixel exactly."""
import os
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import textures as T  # noqa: E402

try:
    from PIL import Image
except ImportError:
    Image = None

FOURCC, RGB, ALPHA = T.DDPF_FOURCC, T.DDPF_RGB, T.DDPF_ALPHAPIXELS


def dds_file(width, height, payload, pf_flags, fourcc=b"", bits=0, masks=(0, 0, 0, 0)):
    header = struct.pack("<4s7I", b"DDS ", 124, 0x1007, height, width, 0, 0, 0) + bytes(44)
    header += struct.pack("<8I", 32, pf_flags, int.from_bytes(fourcc.ljust(4, b"\0"), "little"),
                          bits, *masks)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    assert len(header) == T.HEADER_SIZE
    return header + payload


def colour_block(c0, c1, index_rows):
    """Two RGB565 endpoints and a 4x4 grid of 2-bit palette indices."""
    return struct.pack("<HH4B", c0, c1, *[sum(i << (2 * k) for k, i in enumerate(r)) for r in index_rows])


def alpha_block_dxt3(nibble_rows):
    return struct.pack("<4H", *[sum(n << (4 * k) for k, n in enumerate(r)) for r in nibble_rows])


def alpha_block_dxt5(a0, a1, indices):
    bits = sum(i << (3 * p) for p, i in enumerate(indices))
    return struct.pack("<BB", a0, a1) + bits.to_bytes(6, "little")


def pixel_rows(width, rgba):
    pixels = [tuple(rgba[i:i + 4]) for i in range(0, len(rgba), 4)]
    return [pixels[y * width:(y + 1) * width] for y in range(len(pixels) // width)]


def decode(data):
    width, height, rgba, fmt = T.decode_dds(data)
    return pixel_rows(width, rgba), (width, height), fmt


class Dxt1Test(unittest.TestCase):
    def test_four_colour_block_is_opaque(self):
        # c0 > c1: entries 2 and 3 are the thirds between red and blue.
        indices = [[0, 1, 2, 3], [3, 2, 1, 0], [0, 0, 1, 1], [2, 2, 3, 3]]
        data = dds_file(4, 4, colour_block(0xF800, 0x001F, indices), FOURCC, b"DXT1")
        palette = [(255, 0, 0, 255), (0, 0, 255, 255), (170, 0, 85, 255), (85, 0, 170, 255)]
        rows, size, fmt = decode(data)
        self.assertEqual((size, fmt), ((4, 4), "DXT1"))
        self.assertEqual(rows, [[palette[i] for i in r] for r in indices])

    def test_one_bit_alpha_mode_when_c0_is_not_greater_than_c1(self):
        a = [[0, 1, 2, 3], [3, 3, 3, 3], [2, 2, 0, 0], [1, 3, 1, 3]]
        b = [[0, 1, 2, 3], [3, 3, 3, 3], [3, 3, 3, 3], [2, 3, 2, 3]]
        payload = colour_block(0x001F, 0xF800, a) + colour_block(0x07E0, 0x07E0, b)  # c0 < c1, c0 == c1
        # Entry 2 is the midpoint (integer division) and entry 3 is transparent black.
        pal_a = [(0, 0, 255, 255), (255, 0, 0, 255), (127, 0, 127, 255), (0, 0, 0, 0)]
        pal_b = [(0, 255, 0, 255)] * 3 + [(0, 0, 0, 0)]
        rows, size, _ = decode(dds_file(8, 4, payload, FOURCC, b"DXT1"))
        self.assertEqual(size, (8, 4))
        self.assertEqual(rows, [[pal_a[i] for i in ra] + [pal_b[i] for i in rb] for ra, rb in zip(a, b)])

    def test_size_not_a_multiple_of_four_is_cropped(self):
        zero = [[0] * 4] * 4
        payload = colour_block(0xF800, 0x0000, zero) + colour_block(0x07E0, 0x0000, zero)
        rows, size, _ = decode(dds_file(6, 2, payload, FOURCC, b"DXT1"))
        self.assertEqual(size, (6, 2))
        self.assertEqual(rows, [[(255, 0, 0, 255)] * 4 + [(0, 255, 0, 255)] * 2] * 2)


class Dxt3Test(unittest.TestCase):
    def test_explicit_four_bit_alpha(self):
        nibbles = [[4 * r + c for c in range(4)] for r in range(4)]  # 0..15
        payload = alpha_block_dxt3(nibbles) + colour_block(0x001F, 0xF800, [[0] * 4] * 4)
        rows, size, fmt = decode(dds_file(4, 4, payload, FOURCC, b"DXT3"))
        self.assertEqual((size, fmt), ((4, 4), "DXT3"))
        # c0 < c1, but only DXT1 treats that as a transparent mode: this is plain blue.
        self.assertEqual(rows, [[(0, 0, 255, 17 * n) for n in row] for row in nibbles])


class Dxt5Test(unittest.TestCase):
    def test_interpolated_alpha_blocks(self):
        # a0 > a1: eight-step ramp. a0 <= a1: six steps plus 0 and 255.
        ramp8 = [255, 0, 218, 182, 145, 109, 72, 36]
        ramp6 = [10, 200, 48, 86, 124, 162, 0, 255]
        idx_a = [p % 8 for p in range(16)]
        idx_b = [7 - p % 8 for p in range(16)]
        white = colour_block(0xFFFF, 0xFFFF, [[0] * 4] * 4)
        # c0 < c1 with every pixel on entry 3: (c0 + 2 * c1) / 3 = (170, 0, 85), opaque in DXT5.
        mixed = colour_block(0x001F, 0xF800, [[3] * 4] * 4)
        payload = (alpha_block_dxt5(255, 0, idx_a) + white + alpha_block_dxt5(10, 200, idx_b) + mixed)
        rows, size, fmt = decode(dds_file(8, 4, payload, FOURCC, b"DXT5"))
        self.assertEqual((size, fmt), ((8, 4), "DXT5"))
        expected = [[(255, 255, 255, ramp8[idx_a[4 * y + x]]) for x in range(4)]
                    + [(170, 0, 85, ramp6[idx_b[4 * y + x]]) for x in range(4)] for y in range(4)]
        self.assertEqual(rows, expected)


class UncompressedTest(unittest.TestCase):
    def test_a4r4g4b4(self):
        pixels = [0x8F31, 0x0000, 0xFFFF, 0x1234]
        data = dds_file(2, 2, struct.pack("<4H", *pixels), RGB | ALPHA, bits=16,
                        masks=(0x0F00, 0x00F0, 0x000F, 0xF000))
        rows, size, fmt = decode(data)
        self.assertEqual((size, fmt), ((2, 2), "A4R4G4B4"))
        self.assertEqual(rows, [[(255, 0x33, 0x11, 0x88), (0, 0, 0, 0)],
                                [(255, 255, 255, 255), (0x22, 0x33, 0x44, 0x11)]])

    def test_r5g6b5_without_alpha_replicates_bits(self):
        # R=16, G=32, B=1 -> (16 << 3 | 16 >> 2, 32 << 2 | 32 >> 4, 1 << 3 | 1 >> 2)
        data = dds_file(2, 1, struct.pack("<2H", 0x8401, 0x07E0), RGB, bits=16,
                        masks=(0xF800, 0x07E0, 0x001F, 0))
        rows, _, fmt = decode(data)
        self.assertEqual(fmt, "R5G6B5")
        self.assertEqual(rows, [[(132, 130, 8, 255), (0, 255, 0, 255)]])

    def test_32_bit_with_and_without_alpha(self):
        masks = (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        raw = struct.pack("<I", 0x80112233)
        with_alpha = dds_file(1, 1, raw, RGB | ALPHA, bits=32, masks=masks)
        self.assertEqual(decode(with_alpha)[0], [[(0x11, 0x22, 0x33, 0x80)]])
        # Without the ALPHAPIXELS flag the fourth mask is ignored (it is just padding).
        opaque = dds_file(1, 1, raw, RGB, bits=32, masks=masks)
        self.assertEqual(decode(opaque)[0], [[(0x11, 0x22, 0x33, 255)]])

    def test_24_bit(self):
        data = dds_file(2, 1, bytes([0x33, 0x22, 0x11, 0x66, 0x55, 0x44]), RGB, bits=24,
                        masks=(0xFF0000, 0x00FF00, 0x0000FF, 0))
        rows, _, fmt = decode(data)
        self.assertEqual(fmt, "R8G8B8")
        self.assertEqual(rows, [[(0x11, 0x22, 0x33, 255), (0x44, 0x55, 0x66, 255)]])


class RejectTest(unittest.TestCase):
    def test_bad_files(self):
        good = dds_file(4, 4, colour_block(0xF800, 0x001F, [[0] * 4] * 4), FOURCC, b"DXT1")
        with self.assertRaises(T.DdsError):
            T.decode_dds(b"nope")
        with self.assertRaises(T.DdsError):
            T.decode_dds(b"XXXX" + good[4:])
        with self.assertRaisesRegex(T.DdsError, "truncated"):
            T.decode_dds(good[:-1])
        with self.assertRaisesRegex(T.DdsError, "unsupported"):
            T.decode_dds(dds_file(4, 4, bytes(16), FOURCC, b"DX10"))
        with self.assertRaisesRegex(T.DdsError, "truncated"):
            T.decode_dds(dds_file(2, 2, bytes(7), RGB, bits=16, masks=(0xF800, 0x07E0, 0x1F, 0)))


def decode_png(test, data):
    """Independent PNG reader: checks the structure and CRCs, returns (width, height, rgba)."""
    test.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
    pos, chunks = 8, []
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])
        test.assertEqual(zlib.crc32(tag + body), crc)
        chunks.append((tag, body))
        pos += 12 + length
    test.assertEqual([t for t, _ in chunks], [b"IHDR", b"IDAT", b"IEND"])
    width, height, depth, colour_type, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunks[0][1])
    test.assertEqual((depth, colour_type, compression, filtering, interlace), (8, 6, 0, 0, 0))
    raw = zlib.decompress(chunks[1][1])
    test.assertEqual(len(raw), height * (1 + width * 4))
    rgba = bytearray()
    for y in range(height):
        row = raw[y * (1 + width * 4):(y + 1) * (1 + width * 4)]
        test.assertEqual(row[0], 0)  # filter type 0
        rgba += row[1:]
    return width, height, bytes(rgba)


class PngTest(unittest.TestCase):
    def test_round_trip(self):
        rgba = bytes(range(24))  # 3x2, every channel distinct
        self.assertEqual(decode_png(self, T.encode_png(3, 2, rgba)), (3, 2, rgba))


class ConvertAllTest(unittest.TestCase):
    def test_writes_png_beside_each_dds(self):
        zero = [[0] * 4] * 4
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "sub").mkdir()
            (root / "sub" / "one.dds").write_bytes(
                dds_file(4, 4, colour_block(0xF800, 0x0000, zero), FOURCC, b"DXT1"))
            (root / "two.DDS").write_bytes(
                dds_file(2, 1, struct.pack("<2H", 0xF00F, 0x0F00), RGB | ALPHA, bits=16,
                         masks=(0x0F00, 0x00F0, 0x000F, 0xF000)))
            (root / "notes.txt").write_text("not a texture")
            stats = T.convert_all([root])

            self.assertEqual(stats["files"], 2)
            self.assertEqual(stats["formats"], {"DXT1": 1, "A4R4G4B4": 1})
            self.assertEqual(sorted(p.name for p in root.rglob("*.png")), ["one.png", "two.png"])
            self.assertEqual(decode_png(self, (root / "sub" / "one.png").read_bytes()),
                             (4, 4, bytes([255, 0, 0, 255]) * 16))
            self.assertEqual(decode_png(self, (root / "two.png").read_bytes()),
                             (2, 1, bytes([0, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0])))

    def test_reports_the_failing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "bad.dds").write_bytes(b"not a dds")
            with self.assertRaisesRegex(T.DdsError, "bad.dds"):
                T.convert_all([tmp])


ASSETS = pathlib.Path(os.environ.get("MB3_ASSETS", REPO / "assets"))


@unittest.skipIf(Image is None, "Pillow is not installed")
class PillowCrossCheck(unittest.TestCase):
    TOLERANCE = 2  # per channel; the decoders may round the DXT colour blends differently

    def test_real_files_match_pillow(self):
        files = sorted(p for d in (ASSETS / "Textures", ASSETS / "Data" / "Fonts") if d.is_dir()
                       for p in d.rglob("*.dds"))
        if not files:
            self.skipTest(f"no .dds files under {ASSETS}")
        for path in files:
            with self.subTest(path.name):
                width, height, rgba, _ = T.decode_dds(path.read_bytes())
                with Image.open(path) as im:
                    ref = im.convert("RGBA")
                self.assertEqual(ref.size, (width, height))
                expected = ref.tobytes()
                if rgba != expected:
                    self.assertLessEqual(max(abs(a - b) for a, b in zip(rgba, expected)), self.TOLERANCE)


if __name__ == "__main__":
    unittest.main()
