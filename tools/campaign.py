#!/usr/bin/env python3
"""Convert the level list Data/Levels/levels.xml into Data/xml/levels.json.

Usage:
    python3 tools/campaign.py assets          # reads assets/Data/Levels/levels.xml

levels.xml lists every level file in the order the game's menus number them, with the music of its stage. The
output groups the levels into stages (a run of levels with the same music); the format is documented in
docs/ASSET_FORMATS.md. It lives in Data/xml, not Data/Levels, where every .json is taken for a level.
"""
import argparse
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

VERSION = 1


class CampaignError(ValueError):
    pass


def parse_levels(raw):
    """levels.xml bytes -> the document: {"version", "stages": [{"name", "levels": [file stems]}]}. A stage is a run
    of levels with the same musicName; a level's name is its file name without the .mb3 extension, in the case
    the XML writes it."""
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as e:
        raise CampaignError(f"not well-formed XML: {e}") from None
    if root.tag != "root":
        raise CampaignError(f"expected a <root> element, found <{root.tag}>")
    stages, seen = [], set()
    for level in root.findall("level"):
        file_name, music = level.get("fileName"), level.get("musicName")
        if not file_name or not music:
            raise CampaignError("a <level> needs a fileName and a musicName")
        stem, dot, extension = file_name.rpartition(".")
        if not dot or extension.lower() != "mb3" or not stem:
            raise CampaignError(f"{file_name!r} is not a .mb3 file name")
        if stem.lower() in seen:
            raise CampaignError(f"{stem} is listed twice")
        seen.add(stem.lower())
        if not stages or stages[-1]["name"] != music:
            stages.append({"name": music, "levels": []})
        stages[-1]["levels"].append(stem)
    if not stages:
        raise CampaignError("no <level> elements")
    return {"version": VERSION, "stages": stages}


def convert(assets):
    """Writes Data/xml/levels.json from Data/Levels/levels.xml; returns the number of levels and the stages'."""
    source = pathlib.Path(assets) / "Data" / "Levels" / "levels.xml"
    try:
        doc = parse_levels(source.read_bytes())
    except CampaignError as e:
        raise CampaignError(f"{source}: {e}") from None
    target = pathlib.Path(assets) / "Data" / "xml" / "levels.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(doc, separators=(",", ":")) + "\n", encoding="utf-8")
    return {"levels": sum(len(s["levels"]) for s in doc["stages"]), "stages": [s["name"] for s in doc["stages"]]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", type=pathlib.Path)
    args = ap.parse_args()
    try:
        stats = convert(args.assets)
    except (OSError, CampaignError) as e:
        sys.exit(str(e))
    print(f"{stats['levels']} levels in {len(stats['stages'])} stages: {', '.join(stats['stages'])}")


if __name__ == "__main__":
    main()
