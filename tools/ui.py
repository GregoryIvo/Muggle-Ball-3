#!/usr/bin/env python3
"""Convert the 2D UI data to JSON: menu forms, the string table and the font atlases.

Usage:
    python3 tools/ui.py assets            # converts everything below assets/Data

What it reads and writes (formats: docs/ASSET_FORMATS.md):
    Data/Menu/*.xml          -> Data/Menu/*.json           one generic element tree per form file
    Data/xml/languages.xml   -> Data/xml/languages.json    the string tables, decoded to UTF-8
    Data/Fonts/*.crd         -> Data/Fonts/*.json          glyph rectangles of one pre-rendered atlas

The font atlases (`*.png`, converted from DDS by tools/textures.py) must already exist.
"""
import argparse
import json
import math
import pathlib
import re
import struct
import sys
import xml.etree.ElementTree as ET

VERSION = 1

# Attributes whose value is not text. Every other attribute stays a string, so
# `name="46"` and `text="1000"` are never mistaken for numbers.
COLOUR_ATTRS = {"color", "shadow_color", "font_color"}  # 0xAARRGGBB
BOOL_ATTRS = {"tiled", "no_fading", "disabled", "cycled"}  # yes / no
INT_ATTRS = {"font_size", "fade_time_msec", "show_time_msec"}
FLOAT_ATTRS = {"rotate", "text_offset", "mouse_over_offset"}
VEC2_ATTRS = {"pos", "size", "size_pos", "shadow_offset", "font_shadow", "press_offset", "img_tile",
              "img_translation"}
INT_LIST_ATTRS = {"img_params"}  # columns, rows, then one sprite-sheet frame per widget state
FLOAT_LIST_ATTRS = {"params"}  # slider: min max step; progress: min max step ...

# The string table has no declared encoding. Russian is stored as Windows-1251 bytes and the
# font atlases hold glyphs at the same byte values, so each language names its code page.
DEFAULT_CODEPAGE = "cp1252"
LANGUAGE_CODEPAGES = {"Russian": "cp1251"}

GLYPHS = 256  # one atlas holds one glyph per byte value
CRD_SIZE = GLYPHS * 4 * 4  # u0 v0 u1 v1 as little-endian float32, per glyph

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class UiError(ValueError):
    pass


def decode_xml(raw):
    """XML bytes -> (text, is_utf8). Files without an encoding declaration that are not UTF-8
    (the string table) decode byte for byte as Latin-1, which loses nothing."""
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    try:
        text, utf8 = raw.decode("utf-8"), True
    except UnicodeDecodeError:
        text, utf8 = raw.decode("latin-1"), False
    # ElementTree rejects a str that still carries an encoding declaration.
    return re.sub(r"^\s*<\?xml[^>]*\?>", "", text), utf8


def _parse_xml(raw):
    text, utf8 = decode_xml(raw)
    try:
        return ET.fromstring(text), utf8
    except ET.ParseError as e:
        raise UiError(f"not well-formed XML: {e}") from None


# --- menu forms -------------------------------------------------------------------------------


def _numbers(text, count=None):
    try:
        values = [float(t) for t in text.split()]
    except ValueError:
        return None
    if not values or not all(math.isfinite(v) for v in values):  # JSON has no nan or inf
        return None
    return values if count is None or len(values) == count else None


def parse_colour(text):
    """'0xAARRGGBB' -> [r, g, b, a] floats. Missing high digits are zero, as strtoul reads them, so
    '0x0000f0f0' is fully transparent and '0x0' is transparent black."""
    try:
        argb = int(text.strip(), 16)
    except ValueError:
        return None
    if not 0 <= argb <= 0xFFFFFFFF:
        return None
    return [round(((argb >> shift) & 0xFF) / 255.0, 6) for shift in (16, 8, 0, 24)]


def parse_attribute(name, text):
    """The typed value of a known attribute, None if its text does not parse (and `text` for any
    attribute that is plain text)."""
    if name in COLOUR_ATTRS:
        return parse_colour(text)
    if name in BOOL_ATTRS:
        return {"yes": True, "no": False, "true": True, "false": False, "1": True, "0": False}.get(
            text.strip().lower())
    if name in INT_ATTRS:
        numbers = _numbers(text, 1)
        return int(numbers[0]) if numbers and numbers[0] == int(numbers[0]) else None
    if name in FLOAT_ATTRS:
        numbers = _numbers(text, 1)
        return numbers[0] if numbers else None
    if name in VEC2_ATTRS:
        return _numbers(text, 2)
    if name in INT_LIST_ATTRS:
        numbers = _numbers(text)
        return [int(n) for n in numbers] if numbers and all(n == int(n) for n in numbers) else None
    if name in FLOAT_LIST_ATTRS:
        return _numbers(text)
    return text


def _element(node, problems):
    attributes = {}
    for name, text in node.attrib.items():
        value = parse_attribute(name, text)
        if value is None:  # keep what the file said; the loader ignores a malformed value
            problems.append(f"<{node.tag} {node.attrib.get('name', '')}> {name}={text!r} is not a valid value")
            value = text
        attributes[name] = value
    return {"tag": node.tag, "attributes": attributes, "children": [_element(c, problems) for c in node]}


def parse_forms(raw):
    """Menu XML bytes -> (document, problems). The document is the XML as a tree: every element is
    {"tag", "attributes", "children"}; known attributes are typed, the rest are strings. Unknown
    tags and attributes are kept, so the loader decides what they mean. `problems` lists known
    attributes whose text could not be parsed (they stay strings)."""
    root, _ = _parse_xml(raw)
    problems = []
    doc = {"version": VERSION}
    doc.update(_element(root, problems))
    return doc, problems


def convert_forms(menu_dir):
    """Converts every Data/Menu/*.xml next to itself; returns statistics."""
    stats = {"files": 0, "elements": 0, "tags": {}, "problems": []}
    for path in sorted(menu_dir.glob("*.xml")):
        try:
            doc, problems = parse_forms(path.read_bytes())
        except UiError as e:
            raise UiError(f"{path.name}: {e}") from None
        path.with_suffix(".json").write_text(json.dumps(doc, separators=(",", ":")) + "\n", encoding="utf-8")
        stats["files"] += 1
        stats["problems"] += [f"{path.name}: {p}" for p in problems]
        stack = [doc]
        while stack:
            node = stack.pop()
            stack += node["children"]
            stats["elements"] += 1
            stats["tags"][node["tag"]] = stats["tags"].get(node["tag"], 0) + 1
    return stats


# --- string table -----------------------------------------------------------------------------


def parse_languages(raw):
    """languages.xml bytes -> (document, duplicate keys). Strings are UTF-8; a language's bytes are
    decoded with its own code page (LANGUAGE_CODEPAGES)."""
    root, utf8 = _parse_xml(raw)
    if root.tag != "root":
        raise UiError(f"expected a <root> element, found <{root.tag}>")
    languages, duplicates = [], []
    for language in root.findall("Language"):
        name = language.get("name")
        if not name:
            raise UiError("<Language> without a name")
        codepage = LANGUAGE_CODEPAGES.get(name, DEFAULT_CODEPAGE)
        strings = {}
        for entry in language.findall("String"):
            key, value = entry.get("name"), entry.get("value", "")
            if key is None:
                raise UiError(f"<String> without a name in language {name}")
            if not utf8:
                try:
                    value = value.encode("latin-1").decode(codepage)
                except UnicodeDecodeError:
                    raise UiError(f"{name}.{key} is not valid {codepage}") from None
            if key in strings:
                duplicates.append(f"{name}.{key}")
            strings[key] = value  # a later definition replaces an earlier one
        languages.append({"name": name, "codepage": codepage, "strings": strings})
    if not languages:
        raise UiError("no <Language> elements")
    current = root.find("CurrentLanguage")
    selected = current.get("name") if current is not None else None
    return {
        "version": VERSION,
        "current": selected if selected in {lang["name"] for lang in languages} else languages[0]["name"],
        "languages": languages,
    }, duplicates


def convert_languages(xml_path):
    doc, duplicates = parse_languages(xml_path.read_bytes())
    xml_path.with_suffix(".json").write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")) + "\n",
                                             encoding="utf-8")
    return {"languages": [lang["name"] for lang in doc["languages"]],
            "strings": sum(len(lang["strings"]) for lang in doc["languages"]), "duplicates": duplicates}


# --- font atlases -----------------------------------------------------------------------------


def png_size(raw):
    if len(raw) < 24 or not raw.startswith(PNG_SIGNATURE) or raw[12:16] != b"IHDR":
        raise UiError("not a PNG file")
    return struct.unpack(">II", raw[16:24])


def codepoints(codepage):
    """The Unicode code point drawn by each glyph index, null where the code page has no character."""
    table = []
    for byte in range(GLYPHS):
        try:
            table.append(ord(bytes([byte]).decode(codepage)))
        except UnicodeDecodeError:
            table.append(None)
    return table


def parse_font(crd, atlas_size, face, size, texture, codepage):
    """A `.crd` file is 256 glyphs of u0 v0 u1 v1 (float32), the glyph's rectangle in the atlas as a
    fraction of its size, v running down from the top row like the PNG. The rectangles are whole
    pixels, so the JSON stores them as [x, y, width, height] pixels. Returns (document, indices of
    the glyphs that were cut off at the atlas edge): the shipped bookworm.ttf_23 atlas is a few
    rows too short for its first glyphs, which are control characters."""
    if len(crd) != CRD_SIZE:
        raise UiError(f"{len(crd)} bytes, expected {CRD_SIZE}")
    width, height = atlas_size
    floats = struct.unpack(f"<{GLYPHS * 4}f", crd)
    glyphs, clipped = [], []
    for i in range(GLYPHS):
        u0, v0, u1, v1 = floats[i * 4:i * 4 + 4]
        x0, y0, x1, y1 = u0 * width, v0 * height, u1 * width, v1 * height
        if max(abs(v - round(v)) for v in (x0, y0, x1, y1)) > 0.01:
            raise UiError(f"glyph {i} does not lie on whole pixels of the {width}x{height} atlas")
        x0, y0, x1, y1 = round(x0), round(y0), round(x1), round(y1)
        if x1 > width or y1 > height:
            clipped.append(i)
            x1, y1 = min(x1, width), min(y1, height)
        if x0 < 0 or y0 < 0 or x1 <= x0 or y1 <= y0:
            raise UiError(f"glyph {i} is empty or outside the {width}x{height} atlas")
        glyphs.append([x0, y0, x1 - x0, y1 - y0])
    doc = {
        "version": VERSION,
        "face": face,
        "size": size,
        "texture": texture,
        "atlas": {"width": width, "height": height},
        "lineHeight": max(g[3] for g in glyphs),
        "codepage": codepage,
        "codepoints": codepoints(codepage),
        "glyphs": glyphs,
    }
    return doc, clipped


def font_codepages(languages_doc):
    """Lowercase face name -> code page, from the FontName string of each language."""
    pages = {}
    for language in languages_doc["languages"]:
        face = language["strings"].get("FontName")
        if face:
            pages.setdefault(face.lower(), language["codepage"])
    return pages


def convert_fonts(font_dir, codepages):
    """Converts every Data/Fonts/<face>_<size>.crd next to itself; returns statistics."""
    fonts, warnings = [], []
    for path in sorted(font_dir.glob("*.crd")):
        match = re.fullmatch(r"(.+)_(\d+)", path.stem)
        if not match:
            raise UiError(f"{path.name}: the name should be <face>_<size>.crd")
        face, size = match.group(1), int(match.group(2))
        png = path.with_suffix(".png")
        try:
            doc, clipped = parse_font(path.read_bytes(), png_size(png.read_bytes()), face, size, png.name,
                                      codepages.get(face.lower(), DEFAULT_CODEPAGE))
        except (OSError, UiError) as e:
            raise UiError(f"{path.name}: {e}") from None
        path.with_suffix(".json").write_text(json.dumps(doc, separators=(",", ":")) + "\n", encoding="utf-8")
        fonts.append(f"{face} {size}px ({doc['codepage']})")
        visible = [i for i in clipped if i >= 32]
        if visible:
            warnings.append(f"{path.name}: printable glyphs {visible} are cut off at the atlas edge")
    return {"fonts": fonts, "warnings": warnings}


def convert(assets):
    """Converts all UI data below assets/Data; returns statistics."""
    data = pathlib.Path(assets) / "Data"
    languages = data / "xml" / "languages.xml"
    language_stats = convert_languages(languages)
    codepages = font_codepages(json.loads(languages.with_suffix(".json").read_text(encoding="utf-8")))
    return {"forms": convert_forms(data / "Menu"), "languages": language_stats,
            "fonts": convert_fonts(data / "Fonts", codepages)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", type=pathlib.Path, help="the assets folder")
    args = ap.parse_args()
    try:
        stats = convert(args.assets)
    except (OSError, UiError) as e:
        sys.exit(f"ui: {e}")
    forms = stats["forms"]
    print(f"forms: {forms['files']} files, {forms['elements']} elements {forms['tags']}")
    for problem in forms["problems"]:
        print(f"  warning: {problem}")
    languages = stats["languages"]
    print(f"languages: {languages['languages']}, {languages['strings']} strings")
    if languages["duplicates"]:
        print("  defined more than once (last wins): " + ", ".join(languages["duplicates"]))
    print(f"fonts: {', '.join(stats['fonts']['fonts'])}")
    for warning in stats["fonts"]["warnings"]:
        print(f"  warning: {warning}")


if __name__ == "__main__":
    main()
