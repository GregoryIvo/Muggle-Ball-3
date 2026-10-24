"""tools/vita_livearea.py: the placeholder LiveArea set is the right size and 8-bit indexed."""

import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import vita_livearea  # noqa: E402

COMMITTED_SET = ROOT / "platform" / "vita" / "sce_sys"


def read_png(path):
    """(width, height, bit depth, colour type, has palette) from the PNG's chunks, checking the CRCs."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos, ihdr, palette = 8, None, False
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(kind + body) & 0xFFFFFFFF != crc:
            raise ValueError(f"{path}: bad CRC in {kind}")
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = True
        pos += 12 + length
    width, height, depth, colour_type, _, _, _ = ihdr
    return width, height, depth, colour_type, palette


class LiveAreaSet(unittest.TestCase):
    def check_set(self, folder):
        for name, (width, height) in vita_livearea.LIVEAREA_FILES.items():
            with self.subTest(file=name):
                w, h, depth, colour_type, palette = read_png(folder / name)
                self.assertEqual((w, h), (width, height))
                self.assertEqual(depth, 8)
                self.assertEqual(colour_type, 3, "LiveArea PNGs must be indexed (colour type 3)")
                self.assertTrue(palette, "an indexed PNG needs a PLTE chunk")
        xml = (folder / "livearea" / "contents" / "template.xml").read_text(encoding="utf-8")
        self.assertIn("<livearea", xml)
        self.assertIn("bg.png", xml)
        self.assertIn("startup.png", xml)

    def test_generated_set(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "sce_sys"
            vita_livearea.make_set(out, "MUGGLE BALL 3", "TEST", (0, 0, 0), (255, 255, 255), (200, 100, 50))
            self.check_set(out)

    def test_committed_set(self):
        """The set the VPK is built from, so a stray RGBA export is caught."""
        self.assertTrue(COMMITTED_SET.is_dir(), f"{COMMITTED_SET} is missing")
        self.check_set(COMMITTED_SET)

    def test_text_fits_and_is_drawn(self):
        width, height = 280, 158
        pixels = vita_livearea.make_image(width, height, "MUGGLE BALL 3", "PLACEHOLDER")
        self.assertEqual(len(pixels), width * height)
        self.assertIn(vita_livearea.FOREGROUND, pixels, "the title was drawn")
        self.assertIn(vita_livearea.ACCENT, pixels, "the frame and subtitle were drawn")
        # Unknown characters fall back to a blank glyph rather than failing.
        vita_livearea.make_image(64, 64, "~#", "")

    def test_colour_parsing(self):
        self.assertEqual(vita_livearea.parse_colour("#0d0d1a"), (13, 13, 26))
        self.assertEqual(vita_livearea.parse_colour("ffffff"), (255, 255, 255))
        with self.assertRaises(ValueError):
            vita_livearea.parse_colour("#fff")


if __name__ == "__main__":
    unittest.main()
