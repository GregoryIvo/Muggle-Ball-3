#!/usr/bin/env python3
"""tools/make_icon.py: icon image decoding, PE resource parsing, resampling and the .iconset, all on
synthetic data (no game files). Building the .icns itself needs macOS's iconutil and is skipped elsewhere."""
import contextlib
import io
import pathlib
import platform
import shutil
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import convert_assets  # noqa: E402
import make_icon as M  # noqa: E402


# --- Synthetic icon images ------------------------------------------------------

def dib(width, height, bits, rows, palette=(), mask=None):
    """An icon image: `rows` are the colour rows top to bottom, each a list of pixels (BGRA tuples for
    32 bits, BGR for 24, palette indices otherwise); `mask` is rows of 0/1 (1 = transparent)."""
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, bits, 0, 0, 0, 0, len(palette), 0)
    palette_bytes = b"".join(bytes(c) + b"\0" for c in palette)
    stride = (width * bits + 31) // 32 * 4
    colour = b""
    for row in reversed(rows):
        if bits >= 24:
            packed = b"".join(bytes(p) for p in row)
        else:
            packed = bytearray(stride)
            for x, index in enumerate(row):
                packed[x * bits // 8] |= index << (8 - bits - (x * bits) % 8)
            packed = bytes(packed)
        colour += packed.ljust(stride, b"\0")
    mask = mask or [[0] * width for _ in range(height)]
    mask_stride = (width + 31) // 32 * 4
    mask_bytes = b""
    for row in reversed(mask):
        packed = bytearray(mask_stride)
        for x, bit in enumerate(row):
            packed[x // 8] |= bit << (7 - x % 8)
        mask_bytes += bytes(packed)
    return header + palette_bytes + colour + mask_bytes


def solid32(size, bgra):
    return dib(size, size, 32, [[bgra] * size for _ in range(size)])


def ico_file(blobs):
    head = struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries = b""
    for blob in blobs:
        width, height = struct.unpack_from("<ii", blob, 4)
        entries += struct.pack("<BBBBHHII", width % 256, height // 2 % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return head + entries + b"".join(blobs)


def group_icon(entries):
    """entries: (width, height, bits, resource id)"""
    body = struct.pack("<HHH", 0, 1, len(entries))
    for width, height, bits, ident in entries:
        body += struct.pack("<BBBBHHIH", width, height, 0, 0, 1, bits, 0, ident)
    return body


# --- Synthetic PE ---------------------------------------------------------------

RSRC_RVA, RSRC_RAW = 0x2000, 0x400
DATA_RVA, DATA_RAW = 0x6000, 0x1400  # the resource data sits in another section, as in the real exe


def resource_directory(tree, data_rva):
    """tree: {type: {id: {language: bytes}}}; a str key makes a named entry. Returns the .rsrc bytes (the
    root table first, the names after the tables) and the payload bytes that belong at data_rva."""
    names = sorted({k for node in _walk(tree) for k in node if isinstance(k, str)})
    strings, name_at = b"", {}
    for name in names:
        name_at[name] = len(strings)
        strings += struct.pack("<H", len(name)) + name.encode("utf-16le")

    def build(names_start):
        payload = bytearray()

        def emit(node, base):
            entries = sorted(node.items(), key=lambda kv: (not isinstance(kv[0], str), kv[0]))
            named = sum(isinstance(k, str) for k, _ in entries)
            head = struct.pack("<IIHHHH", 0, 0, 0, 0, named, len(entries) - named)
            body = b""
            offset = base + 16 + 8 * len(entries)
            for key, sub in entries:
                ident = (0x80000000 | names_start + name_at[key]) if isinstance(key, str) else key
                if isinstance(sub, dict):
                    child = emit(sub, offset)
                    head += struct.pack("<II", ident, 0x80000000 | offset)
                else:
                    child = struct.pack("<IIII", data_rva + len(payload), len(sub), 0, 0)
                    payload.extend(sub.ljust(len(sub) + (-len(sub)) % 4, b"\0"))
                    head += struct.pack("<II", ident, offset)
                body += child
                offset += len(child)
            return head + body

        return emit(tree, 0), bytes(payload)

    tables, _ = build(0)
    tables, payload = build(len(tables))
    return tables + strings, payload


def _walk(node):
    if isinstance(node, dict):
        yield node
        for child in node.values():
            yield from _walk(child)


def pe_file(tree, plus=False, with_resources=True):
    rsrc, payload = resource_directory(tree, DATA_RVA)
    optional = bytearray(240 if plus else 224)
    struct.pack_into("<H", optional, 0, 0x20B if plus else 0x10B)
    directories = 112 if plus else 96
    if with_resources:
        struct.pack_into("<II", optional, directories + 16, RSRC_RVA, len(rsrc))
    sections = [(b".rsrc", RSRC_RVA, RSRC_RAW, rsrc), (b".data", DATA_RVA, DATA_RAW, payload)]
    header = b"MZ".ljust(0x3C, b"\0") + struct.pack("<I", 0x80)
    header = header.ljust(0x80, b"\0") + b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664 if plus else 0x14C, len(sections), 0, 0, 0,
                                                                  len(optional), 0x102) + bytes(optional)
    for name, rva, raw, body in sections:
        header += struct.pack("<8sIIIIIIHHI", name, len(body), rva, len(body), raw, 0, 0, 0, 0, 0)
    out = bytearray(header.ljust(DATA_RAW + len(payload), b"\0"))
    out[RSRC_RAW:RSRC_RAW + len(rsrc)] = rsrc
    out[DATA_RAW:DATA_RAW + len(payload)] = payload
    return bytes(out)


def icon_tree(images, group=None):
    """RT_ICON ids 1.. for the images and a named group that lists them."""
    entries = [(im[0], im[1], 32, i + 1) for i, im in enumerate(images)]
    return {
        M.RT_ICON: {i + 1: {0x409: im[2]} for i, im in enumerate(images)},
        M.RT_GROUP_ICON: {"IDI_APPICON": {0x409: group or group_icon(entries)},
                          "ZZ_OTHER": {0x409: group_icon([(1, 1, 32, 99)])}},
    }


# --- Tests ----------------------------------------------------------------------

class DibTest(unittest.TestCase):
    def test_32_bit_rows_are_flipped_and_swizzled(self):
        rows = [[(1, 2, 3, 200), (4, 5, 6, 255)], [(7, 8, 9, 0), (10, 11, 12, 128)]]
        image = M.decode_dib(dib(2, 2, 32, rows))
        self.assertEqual((image.width, image.height, image.bit_count), (2, 2, 32))
        # BGRA in the file, RGBA out, top row first
        self.assertEqual(image.rgba, bytes([3, 2, 1, 200, 6, 5, 4, 255, 9, 8, 7, 0, 12, 11, 10, 128]))

    def test_24_bit_uses_the_mask_for_transparency(self):
        rows = [[(10, 20, 30), (40, 50, 60)], [(70, 80, 90), (100, 110, 120)]]
        image = M.decode_dib(dib(2, 2, 24, rows, mask=[[0, 1], [1, 0]]))
        self.assertEqual(image.rgba, bytes([30, 20, 10, 255, 60, 50, 40, 0, 90, 80, 70, 0, 120, 110, 100, 255]))

    def test_paletted_images(self):
        palette = [(0, 0, 255), (0, 255, 0), (255, 0, 0), (9, 9, 9)]  # BGR
        cases = {1: [[0, 1, 1, 0, 1, 0, 0, 1, 1]], 4: [[0, 1, 2, 3, 3, 2, 1, 0, 1]], 8: [[3, 2, 1, 0, 1, 2, 3, 0, 1]]}
        for bits, rows in cases.items():
            with self.subTest(bits=bits):
                table = palette[:2] if bits == 1 else palette
                image = M.decode_dib(dib(9, 1, bits, rows, palette=table))
                got = [tuple(image.rgba[i * 4:i * 4 + 3]) for i in range(9)]
                want = [tuple(reversed(table[index])) for index in rows[0]]
                self.assertEqual(got, want)

    def test_32_bit_without_alpha_takes_its_shape_from_the_mask(self):
        rows = [[(1, 2, 3, 0), (4, 5, 6, 0)]]
        image = M.decode_dib(dib(2, 1, 32, rows, mask=[[0, 1]]))
        self.assertEqual([image.rgba[3], image.rgba[7]], [255, 0])

    def test_unsupported_and_broken_images_are_rejected(self):
        with self.assertRaisesRegex(M.IconError, "PNG"):
            M.decode_dib(b"\x89PNG\r\n\x1a\n" + bytes(40))
        with self.assertRaisesRegex(M.IconError, "truncated"):
            M.decode_dib(solid32(4, (1, 2, 3, 4))[:100])
        with self.assertRaisesRegex(M.IconError, "unsupported"):
            M.decode_dib(struct.pack("<IiiHHIIiiII", 40, 4, 8, 1, 16, 0, 0, 0, 0, 0, 0) + bytes(100))
        with self.assertRaisesRegex(M.IconError, "palette"):
            M.decode_dib(dib(1, 1, 8, [[5]], palette=[(0, 0, 0)] * 2))


class IcoTest(unittest.TestCase):
    def test_reads_every_image(self):
        images = M.read_ico(ico_file([solid32(16, (1, 2, 3, 255)), solid32(32, (4, 5, 6, 255))]))
        self.assertEqual([im.width for im in images], [16, 32])

    def test_skips_images_it_cannot_decode(self):
        images = M.read_ico(ico_file([b"\x89PNG\r\n\x1a\n" + bytes(40), solid32(16, (1, 2, 3, 255))]))
        self.assertEqual([im.width for im in images], [16])

    def test_not_an_ico(self):
        with self.assertRaises(M.IconError):
            M.read_ico(b"MZ" + bytes(100))
        with self.assertRaisesRegex(M.IconError, "no usable"):
            M.read_ico(ico_file([b"\x89PNG\r\n\x1a\n" + bytes(40)]))


class PeTest(unittest.TestCase):
    images = [(16, 16, solid32(16, (10, 20, 30, 255))), (32, 32, solid32(32, (40, 50, 60, 255))),
              (48, 48, solid32(48, (70, 80, 90, 255)))]

    def test_reads_the_first_icon_group_through_the_section_table(self):
        for plus in (False, True):
            with self.subTest(pe32_plus=plus):
                icons = M.read_pe_icons(pe_file(icon_tree(self.images), plus=plus))
                self.assertEqual([im.width for im in icons], [16, 32, 48])
                self.assertEqual(icons[2].rgba[:4], bytes([90, 80, 70, 255]))

    def test_only_the_images_of_the_group_are_used(self):
        tree = icon_tree(self.images, group=group_icon([(32, 32, 32, 2)]))
        self.assertEqual([im.width for im in M.read_pe_icons(pe_file(tree))], [32])

    def test_errors(self):
        with self.assertRaisesRegex(M.IconError, "not a PE"):
            M.read_pe_icons(b"hello")
        with self.assertRaisesRegex(M.IconError, "no resources"):
            M.read_pe_icons(pe_file(icon_tree(self.images), with_resources=False))
        with self.assertRaisesRegex(M.IconError, "RT_GROUP_ICON"):
            M.read_pe_icons(pe_file({M.RT_ICON: {1: {0x409: self.images[0][2]}}}))


class ResizeTest(unittest.TestCase):
    @staticmethod
    def image(size, pixel):
        return M.Image(size, size, bytes(pixel) * size * size, 32)

    def test_flat_colour_stays_flat_at_any_scale(self):
        for source, target in ((8, 64), (64, 8), (5, 7)):
            with self.subTest(source=source, target=target):
                out = M.resize(self.image(source, (200, 100, 50, 255)), target)
                self.assertEqual((out.width, out.height, len(out.rgba)), (target, target, target * target * 4))
                self.assertEqual(set(out.rgba[i:i + 4] for i in range(0, len(out.rgba), 4)),
                                 {bytes((200, 100, 50, 255))})

    def test_transparent_pixels_do_not_tint_the_edge(self):
        # opaque red on the left, fully transparent (but green) on the right
        pixels = b"".join(bytes((255, 0, 0, 255)) if x < 4 else bytes((0, 255, 0, 0)) for _y in range(8) for x in range(8))
        out = M.resize(M.Image(8, 8, pixels, 32), 32)
        for i in range(0, len(out.rgba), 4):
            r, g, b, a = out.rgba[i:i + 4]
            if a > 0:
                self.assertEqual((g, b), (0, 0), "colour of a transparent pixel leaked in")

    def test_downscale_averages(self):
        pixels = bytes((0, 0, 0, 255, 255, 255, 255, 255) * 2)  # 2x2: black, white / black, white
        out = M.resize(M.Image(2, 2, pixels, 32), 1)
        self.assertTrue(120 <= out.rgba[0] <= 135 and out.rgba[3] == 255, out.rgba)


class IconsetTest(unittest.TestCase):
    def test_exact_sizes_are_used_as_they_are(self):
        small = M.decode_dib(solid32(16, (10, 20, 30, 255)))
        big = M.decode_dib(solid32(48, (70, 80, 90, 255)))
        self.assertIs(M.square_image([small, big], 16), small)
        self.assertEqual(M.square_image([small, big], 64).width, 64)

    def test_prefers_more_colour_depth(self):
        eight = M.Image(16, 16, bytes(16 * 16 * 4), 8)
        full = M.Image(16, 16, bytes(16 * 16 * 4), 32)
        self.assertIs(M.square_image([eight, full], 16), full)

    def test_writes_every_name_at_its_size(self):
        images = [M.decode_dib(solid32(16, (1, 2, 3, 255))), M.decode_dib(solid32(32, (1, 2, 3, 255)))]
        with tempfile.TemporaryDirectory() as tmp:
            M.write_iconset(images, pathlib.Path(tmp) / "AppIcon.iconset")
            for name, size in M.ICONSET:
                data = (pathlib.Path(tmp) / "AppIcon.iconset" / name).read_bytes()
                self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n", name)
                self.assertEqual(struct.unpack_from(">II", data, 16), (size, size), name)
                self.assertTrue(zlib.decompress(data[data.index(b"IDAT") + 4:-16]), name)

    def test_non_square_source_is_refused(self):
        wide = M.Image(8, 4, bytes(8 * 4 * 4), 32)
        with self.assertRaisesRegex(M.IconError, "not square"):
            M.square_image([wide], 64)

    @unittest.skipUnless(platform.system() == "Darwin" and shutil.which("iconutil"), "needs macOS iconutil")
    def test_iconutil_accepts_the_iconset(self):
        images = [M.decode_dib(solid32(16, (1, 2, 3, 255))), M.decode_dib(solid32(32, (200, 100, 50, 255)))]
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "out" / "AppIcon.icns"
            M.make_icns(images, out)
            self.assertEqual(out.read_bytes()[:4], b"icns")


class ConvertAssetsHookTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.game = pathlib.Path(self.tmp.name) / "game"
        self.game.mkdir()
        self.out = pathlib.Path(self.tmp.name) / "assets"
        self.out.mkdir()

    def convert(self, system):
        printed = io.StringIO()
        with mock.patch.object(convert_assets.platform, "system", return_value=system), \
                contextlib.redirect_stdout(printed):
            convert_assets.convert_icon(self.game, self.out)
        return printed.getvalue()

    def test_skipped_off_macos(self):
        self.assertIn("skipped", self.convert("Linux"))
        self.assertFalse((self.out / "AppIcon.icns").exists())

    def test_a_missing_executable_only_warns(self):
        self.assertIn("missing", self.convert("Darwin"))
        self.assertFalse((self.out / "AppIcon.icns").exists())

    @unittest.skipUnless(platform.system() == "Darwin" and shutil.which("iconutil"), "needs macOS iconutil")
    def test_builds_the_icon_from_the_executable(self):
        images = [(16, 16, solid32(16, (1, 2, 3, 255))), (32, 32, solid32(32, (4, 5, 6, 255)))]
        (self.game / "MagicBall3.exe").write_bytes(pe_file(icon_tree(images)))
        self.assertIn("built", self.convert("Darwin"))
        self.assertEqual((self.out / "AppIcon.icns").read_bytes()[:4], b"icns")


if __name__ == "__main__":
    unittest.main()
