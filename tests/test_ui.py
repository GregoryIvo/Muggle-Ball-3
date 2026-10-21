#!/usr/bin/env python3
"""Convert synthetic menu forms, string tables and font atlases with tools/ui.py and check the JSON exactly."""
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import ui as U  # noqa: E402

FORMS = b"""<forms>
    <!-- a comment is not an element -->
    <form name="Test" color="0xffffffff" font_size="18" font_shadow="2.0 2.0" pos="0. .5" size="1. .5"
          fade_time_msec = "400" tiled="no">
        <label name="Panel" pos="0.080 0.110" size="0.830 0.600" color="0xfff0f0f0" shadow_color="0x8f000000"
               img_name="forms/Panel.dds" no_fading="yes" rotate="-37.51" text="1000"/>
        <button name="46" img_params="1 2 0 1 " press_offset="2.00 2.00" mouse_over_offset="3." disabled="yes"
                user_data="Navigate" text_offset="-120.00">
            <La1bel name="typo" color="0x0000f0f0"/>
        </button>
        <slider name="Volume" params="0.00 100.00 5.00" size_pos="0.03 0.06" color="0x0"/>
    </form>
</forms>"""


def forms(xml=FORMS):
    doc, problems = U.parse_forms(xml)
    return doc, problems


class FormsTest(unittest.TestCase):
    def test_tree_shape(self):
        doc, problems = forms()
        self.assertEqual(problems, [])
        self.assertEqual((doc["version"], doc["tag"], doc["attributes"]), (1, "forms", {}))
        (form,) = doc["children"]
        self.assertEqual(form["tag"], "form")
        self.assertEqual([c["tag"] for c in form["children"]], ["label", "button", "slider"])
        self.assertEqual(form["children"][0]["children"], [])
        # A tag the game does not know is kept as it was written, nested where it was.
        self.assertEqual(form["children"][1]["children"][0]["tag"], "La1bel")

    def test_typed_attributes(self):
        form = forms()[0]["children"][0]
        self.assertEqual(form["attributes"], {
            "name": "Test",
            "color": [1.0, 1.0, 1.0, 1.0],
            "font_size": 18,
            "font_shadow": [2.0, 2.0],
            "pos": [0.0, 0.5],  # "0." and ".5" are valid in the original files
            "size": [1.0, 0.5],
            "fade_time_msec": 400,
            "tiled": False,
        })
        self.assertIsInstance(form["attributes"]["font_size"], int)
        panel = form["children"][0]["attributes"]
        self.assertEqual(panel["color"], [round(0xF0 / 255, 6)] * 3 + [1.0])
        self.assertEqual(panel["shadow_color"], [0.0, 0.0, 0.0, round(0x8F / 255, 6)])
        self.assertIs(panel["no_fading"], True)
        self.assertEqual(panel["rotate"], -37.51)

    def test_text_and_names_stay_strings(self):
        panel, button, _ = forms()[0]["children"][0]["children"]
        self.assertEqual(panel["attributes"]["text"], "1000")
        self.assertEqual(panel["attributes"]["img_name"], "forms/Panel.dds")
        self.assertEqual(button["attributes"]["name"], "46")
        self.assertEqual(button["attributes"]["user_data"], "Navigate")

    def test_lists_and_scalars(self):
        _, button, slider = forms()[0]["children"][0]["children"]
        self.assertEqual(button["attributes"]["img_params"], [1, 2, 0, 1])  # the trailing space is harmless
        self.assertEqual(button["attributes"]["press_offset"], [2.0, 2.0])
        self.assertEqual(button["attributes"]["mouse_over_offset"], 3.0)
        self.assertEqual(button["attributes"]["text_offset"], -120.0)
        self.assertIs(button["attributes"]["disabled"], True)
        self.assertEqual(slider["attributes"]["params"], [0.0, 100.0, 5.0])
        self.assertEqual(slider["attributes"]["size_pos"], [0.03, 0.06])

    def test_colours_with_missing_digits_read_like_strtoul(self):
        self.assertEqual(U.parse_colour("0x0"), [0.0, 0.0, 0.0, 0.0])
        self.assertEqual(U.parse_colour("0x0000f0f0"), [0.0, round(0xF0 / 255, 6), round(0xF0 / 255, 6), 0.0])
        self.assertEqual(U.parse_colour("0x80ff0000"), [1.0, 0.0, 0.0, round(0x80 / 255, 6)])
        self.assertIsNone(U.parse_colour("red"))
        self.assertIsNone(U.parse_colour("0x1ffffffff"))

    def test_bad_values_stay_text_and_are_reported(self):
        doc, problems = forms(b'<forms><form pos="left" font_size="1.5" disabled="maybe" color="red" '
                              b'img_params="1 x" size="nan 1" name="n"/></forms>')
        attributes = doc["children"][0]["attributes"]
        self.assertEqual(attributes["pos"], "left")
        self.assertEqual(attributes["font_size"], "1.5")
        self.assertEqual(attributes["img_params"], "1 x")
        self.assertEqual(attributes["size"], "nan 1")  # JSON cannot hold nan or inf
        self.assertEqual(len(problems), 6)
        self.assertIn("pos='left'", problems[0])

    def test_odd_encodings_and_declarations(self):
        doc, _ = forms('﻿<?xml version="1.0" encoding="utf-8"?><forms><form name="Café"/></forms>'.encode())
        self.assertEqual(doc["children"][0]["attributes"]["name"], "Café")
        # Not UTF-8 and no declaration: every byte still comes through as one character.
        doc, _ = forms(b'<forms><form name="caf\xe9"/></forms>')
        self.assertEqual(doc["children"][0]["attributes"]["name"], "café")

    def test_malformed_xml_is_an_error(self):
        with self.assertRaises(U.UiError):
            forms(b"<forms><form></forms>")

    def test_convert_forms_writes_json_next_to_the_xml(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            (folder / "Test.xml").write_bytes(FORMS)
            (folder / "other.txt").write_text("ignored")
            stats = U.convert_forms(folder)
            self.assertEqual((stats["files"], stats["elements"]), (1, 6))
            self.assertEqual(stats["tags"], {"forms": 1, "form": 1, "label": 1, "button": 1, "La1bel": 1, "slider": 1})
            self.assertEqual(json.loads((folder / "Test.json").read_text())["children"][0]["tag"], "form")
            (folder / "Broken.xml").write_bytes(b"<forms>")
            with self.assertRaisesRegex(U.UiError, "Broken.xml"):
                U.convert_forms(folder)


LANGUAGES = (b'<root>\r\n  <Language name="English">\r\n'
             b'    <String name="FontName" value="bookworm.ttf" />\r\n'
             b'    <String name="Hello" value="Hello, \'world\'" />\r\n'
             b'    <String name="Hello" value="Hi" />\r\n'
             b'    <String name="Quote" value="Alexey &apos;SCIA&apos;" />\r\n  </Language>\r\n'
             b'  <Language name="Russian">\r\n'
             b'    <String name="FontName" value="brush.ttf" />\r\n'
             # "Zagruzka" and "pulemyot" (with yo, 0xB8) in Windows-1251
             b'    <String name="Loading" value="\xc7\xe0\xe3\xf0\xf3\xe7\xea\xe0..." />\r\n'
             b'    <String name="Gun" value="\xcf\xf3\xeb\xe5\xec\xb8\xf2" />\r\n  </Language>\r\n'
             b'  <CurrentLanguage name="Russian" />\r\n</root>')


class LanguagesTest(unittest.TestCase):
    def test_strings_are_decoded_per_language(self):
        doc, duplicates = U.parse_languages(LANGUAGES)
        self.assertEqual(doc["current"], "Russian")
        english, russian = doc["languages"]
        self.assertEqual((english["name"], english["codepage"]), ("English", "cp1252"))
        self.assertEqual((russian["name"], russian["codepage"]), ("Russian", "cp1251"))
        self.assertEqual(english["strings"]["Quote"], "Alexey 'SCIA'")
        self.assertEqual(russian["strings"]["Loading"], "Загрузка...")
        self.assertEqual(russian["strings"]["Gun"], "Пулемёт")

    def test_duplicates_keep_the_last_definition_and_are_reported(self):
        doc, duplicates = U.parse_languages(LANGUAGES)
        self.assertEqual(doc["languages"][0]["strings"]["Hello"], "Hi")
        self.assertEqual(duplicates, ["English.Hello"])
        self.assertEqual(list(doc["languages"][0]["strings"]), ["FontName", "Hello", "Quote"])  # file order

    def test_utf8_files_are_taken_as_they_are(self):
        doc, _ = U.parse_languages('<root><Language name="Russian"><String name="a" value="Привет"/></Language></root>'
                                   .encode())
        self.assertEqual(doc["languages"][0]["strings"]["a"], "Привет")

    def test_current_language_falls_back_to_the_first(self):
        doc, _ = U.parse_languages(b'<root><Language name="A"/><Language name="B"/><CurrentLanguage name="C"/></root>')
        self.assertEqual(doc["current"], "A")

    def test_errors(self):
        for xml in (b"<root/>", b"<forms/>", b'<root><Language/></root>', b'<root><Language name="A"><String/></Language></root>'):
            with self.assertRaises(U.UiError, msg=xml):
                U.parse_languages(xml)

    def test_convert_languages_writes_utf8(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "languages.xml"
            path.write_bytes(LANGUAGES)
            stats = U.convert_languages(path)
            self.assertEqual((stats["languages"], stats["strings"], stats["duplicates"]),
                             (["English", "Russian"], 6, ["English.Hello"]))
            self.assertIn("Загрузка...", path.with_suffix(".json").read_text(encoding="utf-8"))


def png_header(width, height):
    """A PNG with an IHDR chunk; the converter reads nothing else."""
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (U.PNG_SIGNATURE + struct.pack(">I", len(ihdr)) + b"IHDR" + ihdr +
            struct.pack(">I", zlib.crc32(b"IHDR" + ihdr)))


def crd(rects, width, height):
    """The .crd bytes for pixel rectangles (x, y, w, h); the rest of the 256 glyphs repeat the last one."""
    rects = rects + [rects[-1]] * (U.GLYPHS - len(rects))
    return struct.pack(f"<{U.GLYPHS * 4}f", *[v for x, y, w, h in rects for v in
                                                (x / width, y / height, (x + w) / width, (y + h) / height)])


class FontTest(unittest.TestCase):
    def test_png_size(self):
        self.assertEqual(U.png_size(png_header(512, 1024)), (512, 1024))
        for junk in (b"", b"GIF89a" + bytes(30), U.PNG_SIGNATURE + bytes(8) + b"IDAT" + bytes(10)):
            with self.assertRaises(U.UiError):
                U.png_size(junk)

    def test_glyph_rectangles_become_pixels(self):
        rects = [(256, 0, 8, 16), (0, 16, 5, 16)]
        doc, clipped = U.parse_font(crd(rects, 512, 64), (512, 64), "face.ttf", 20, "face.ttf_20.png", "cp1252")
        self.assertEqual(clipped, [])
        self.assertEqual(doc["glyphs"][:2], [[256, 0, 8, 16], [0, 16, 5, 16]])
        self.assertEqual(len(doc["glyphs"]), 256)
        self.assertEqual((doc["version"], doc["face"], doc["size"], doc["texture"]), (1, "face.ttf", 20, "face.ttf_20.png"))
        self.assertEqual(doc["atlas"], {"width": 512, "height": 64})
        self.assertEqual(doc["lineHeight"], 16)
        self.assertEqual(doc["codepage"], "cp1252")

    def test_code_page_tables(self):
        latin = U.codepoints("cp1252")
        self.assertEqual((len(latin), latin[65], latin[0xE9], latin[0x80], latin[0x81]), (256, 65, 0xE9, 0x20AC, None))
        cyrillic = U.codepoints("cp1251")
        self.assertEqual((cyrillic[65], cyrillic[0xC0], cyrillic[0xFF], cyrillic[0xB8], cyrillic[0x98]),
                         (65, 0x410, 0x44F, 0x451, None))

    def test_a_glyph_past_the_atlas_edge_is_clipped(self):
        rects = [(0, 56, 8, 16)] + [(0, 0, 8, 16)] * 255
        doc, clipped = U.parse_font(crd(rects, 64, 64), (64, 64), "f", 1, "f.png", "cp1252")
        self.assertEqual(clipped, [0])
        self.assertEqual(doc["glyphs"][0], [0, 56, 8, 8])

    def test_bad_glyph_tables_are_errors(self):
        good = crd([(0, 0, 8, 8)], 64, 64)
        with self.assertRaisesRegex(U.UiError, "bytes"):
            U.parse_font(good[:-4], (64, 64), "f", 1, "f.png", "cp1252")
        with self.assertRaisesRegex(U.UiError, "whole pixels"):
            U.parse_font(good, (63, 64), "f", 1, "f.png", "cp1252")
        with self.assertRaisesRegex(U.UiError, "empty"):
            U.parse_font(crd([(8, 8, 0, 8)], 64, 64), (64, 64), "f", 1, "f.png", "cp1252")

    def test_font_codepages_come_from_the_languages(self):
        doc, _ = U.parse_languages(LANGUAGES)
        self.assertEqual(U.font_codepages(doc), {"bookworm.ttf": "cp1252", "brush.ttf": "cp1251"})

    def test_convert_fonts(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            (folder / "Face.ttf_17.png").write_bytes(png_header(64, 64))
            (folder / "Face.ttf_17.crd").write_bytes(crd([(0, 0, 8, 8)], 64, 64))
            stats = U.convert_fonts(folder, {"face.ttf": "cp1251"})
            self.assertEqual(stats["fonts"], ["Face.ttf 17px (cp1251)"])
            doc = json.loads((folder / "Face.ttf_17.json").read_text())
            self.assertEqual((doc["face"], doc["size"], doc["codepage"]), ("Face.ttf", 17, "cp1251"))
            (folder / "Other.crd").write_bytes(b"")
            with self.assertRaisesRegex(U.UiError, "Other.crd"):
                U.convert_fonts(folder, {})
            (folder / "Other.crd").unlink()
            (folder / "Lost.ttf_9.crd").write_bytes(crd([(0, 0, 8, 8)], 64, 64))
            with self.assertRaisesRegex(U.UiError, "Lost.ttf_9.crd"):  # no atlas PNG beside it
                U.convert_fonts(folder, {})

    def test_convert_everything(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = pathlib.Path(tmp) / "Data"
            for sub in ("Menu", "xml", "Fonts"):
                (data / sub).mkdir(parents=True)
            (data / "Menu" / "Test.xml").write_bytes(FORMS)
            (data / "xml" / "languages.xml").write_bytes(LANGUAGES)
            (data / "Fonts" / "brush.ttf_20.png").write_bytes(png_header(64, 64))
            (data / "Fonts" / "brush.ttf_20.crd").write_bytes(crd([(0, 0, 8, 8)], 64, 64))
            stats = U.convert(tmp)
            self.assertEqual(stats["forms"]["files"], 1)
            self.assertEqual(stats["languages"]["languages"], ["English", "Russian"])
            font = json.loads((data / "Fonts" / "brush.ttf_20.json").read_text())
            self.assertEqual(font["codepage"], "cp1251")  # the Russian language names brush.ttf


if __name__ == "__main__":
    unittest.main()
