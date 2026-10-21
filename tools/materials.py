#!/usr/bin/env python3
"""Convert Data/Materials/materials.xml into materials.json.

Usage:
    python3 tools/materials.py materials.xml [materials.json]

The output schema is documented in docs/ASSET_FORMATS.md. Defaults for attributes
the XML leaves out match GameMaterial in game/include/mb/game/MaterialLibrary.h.
"""
import argparse
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

VERSION = 1

SOUND_KEYS = {
    "dropSound": "drop",
    "periodSound": "period",
    "hitSound": "hit",
    "deadSound": "dead",
    "loopSound": "loop",
}

DEFAULT_D3D = {
    "Diffuse": [1.0, 1.0, 1.0, 1.0],
    "Ambient": [1.0, 1.0, 1.0, 1.0],
    "Emissive": [0.0, 0.0, 0.0, 0.0],
    "Specular": [0.0, 0.0, 0.0, 0.0],
}


def _number(element, name, default, kind):
    raw = None if element is None else element.get(name)
    if raw is None:
        return default
    try:
        return kind(raw)
    except ValueError:
        raise ValueError(f"<{element.tag} {name}={raw!r}> is not a number") from None


def _colour(element, name):
    raw = None if element is None else element.get(name)
    if raw is None:
        return list(DEFAULT_D3D[name])
    try:
        a, r, g, b = (float(v) for v in raw.split())
    except ValueError:
        raise ValueError(f"<D3DMaterial {name}={raw!r}> is not four numbers") from None
    # The XML stores A R G B; the JSON uses RGBA.
    return [r, g, b, a]


def _material(element, group):
    name = element.get("name")
    if not name:
        raise ValueError("<Material> without a name")
    try:
        nx = element.find("NxMaterial")
        d3d = element.find("D3DMaterial")
        extra = element.find("Extra")

        merged = {}
        for s in element.findall("Sounds"):
            merged.update(s.attrib)  # later elements override earlier ones
        sounds = {key: merged[attr] for attr, key in SOUND_KEYS.items() if merged.get(attr)}

        textures = []
        for t in element.findall("Texture"):
            if not t.get("texture"):
                raise ValueError("<Texture> without a texture name")
            textures.append(t.get("texture"))

        out = {
            "name": name,
            "group": group,
            "textures": textures,
            "animated": bool(_number(element, "animated", 0, int)),
            "animTimeMs": _number(element, "animTime", 0, int),
            "density": _number(element, "density", 0.0, float),
        }
        if element.get("splinter"):
            out["splinter"] = element.get("splinter")
        out["d3d"] = {
            "diffuse": _colour(d3d, "Diffuse"),
            "ambient": _colour(d3d, "Ambient"),
            "emissive": _colour(d3d, "Emissive"),
            "specular": _colour(d3d, "Specular"),
            "power": _number(d3d, "Power", 0.0, float),
        }
        out["physics"] = {
            "staticFriction": _number(nx, "staticFriction", 0.5, float),
            "dynamicFriction": _number(nx, "dynamicFriction", 0.5, float),
            "restitution": _number(nx, "restitution", 0.5, float),
            "frictionCombineMode": _number(nx, "frictionCombineMode", 0, int),
            "restitutionCombineMode": _number(nx, "restitutionCombineMode", 0, int),
        }
        out["brick"] = {
            "type": _number(extra, "brickType", 0, int),
            "energy": _number(extra, "brickEnergy", 1, int),
            "score": _number(extra, "brickScore", 0, int),
            "exploseType": _number(extra, "exploseType", 0, int),
            "exploseDelay": _number(extra, "exploseDelay", 0, int),
            "explosePower": _number(extra, "explosePower", 0.0, float),
        }
        if sounds:
            out["sounds"] = sounds
        states = [[_number(r, "state", 0, int), _number(r, "value", 0, int)]
                  for r in element.findall("RenderState")]
        if states:
            out["renderStates"] = states
        return out
    except ValueError as e:
        raise ValueError(f"material {name!r}: {e}") from None


def _walk(group_element, path, materials, preload):
    for child in group_element:
        if child.tag == "Materials":
            name = child.get("name")
            _walk(child, path + [name] if name else path, materials, preload)
        elif child.tag == "Material":
            materials.append(_material(child, "/".join(path)))
        elif child.tag == "TexturePreload":
            preload.append(child.get("name"))


def parse(xml_bytes):
    """materials.xml contents -> the materials.json document (a dict)."""
    root = ET.fromstring(xml_bytes)
    if root.tag != "Materials":
        raise ValueError(f"expected a <Materials> root element, found <{root.tag}>")
    materials, preload = [], []
    # The root group is named "Materials" too; group paths start below it.
    _walk(root, [], materials, preload)
    return {"version": VERSION, "texturePreload": preload, "materials": materials}


def format_json(doc):
    """One material per line: compact enough to ship, readable enough to diff."""
    rows = ",\n".join("  " + json.dumps(m) for m in doc["materials"])
    return (f'{{"version": {doc["version"]},\n'
            f' "texturePreload": {json.dumps(doc["texturePreload"])},\n'
            f' "materials": [\n{rows}\n ]}}\n')


def statistics(doc):
    materials = doc["materials"]
    first_spelling, count = {}, {}
    for m in materials:
        key = m["name"].lower()
        first_spelling.setdefault(key, m["name"])
        count[key] = count.get(key, 0) + 1
    groups = []
    for m in materials:
        if m["group"] and m["group"] not in groups:
            groups.append(m["group"])
    return {
        "materials": len(materials),
        "groups": groups,
        "texturePreload": len(doc["texturePreload"]),
        # Names defined more than once (case-insensitive), as first spelled.
        "duplicates": [first_spelling[key] for key, n in count.items() if n > 1],
        "animated": [m["name"] for m in materials if m["animated"]],
        "renderStates": {m["name"]: m["renderStates"] for m in materials if "renderStates" in m},
    }


def convert(materials_xml_path, out_json_path):
    """Write materials.json next to (or at) out_json_path; returns statistics."""
    doc = parse(pathlib.Path(materials_xml_path).read_bytes())
    pathlib.Path(out_json_path).write_text(format_json(doc), encoding="utf-8")
    return statistics(doc)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xml", type=pathlib.Path, help="Data/Materials/materials.xml")
    ap.add_argument("json", nargs="?", type=pathlib.Path,
                    help="output path (default: materials.json beside the XML)")
    args = ap.parse_args()
    out = args.json or args.xml.with_name("materials.json")
    try:
        stats = convert(args.xml, out)
    except (OSError, ET.ParseError, ValueError) as e:
        sys.exit(f"{args.xml}: {e}")
    print(f"materials: {stats['materials']} in {len(stats['groups'])} groups, "
          f"{stats['texturePreload']} preloaded textures")
    if stats["duplicates"]:
        print("defined more than once (first wins): " + ", ".join(stats["duplicates"]))
    print("animated: " + ", ".join(stats["animated"]))
    for name, states in stats["renderStates"].items():
        print(f"render states {name}: {states}")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
