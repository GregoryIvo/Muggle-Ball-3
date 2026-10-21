#!/usr/bin/env python3
"""Convert Data/xml/sounds.xml and musics.xml into sounds.json and musics.json.

Usage:
    python3 tools/sounds.py assets

The folder holds Data/xml/ (where the two JSON files are written), Sounds/ and
Musics/. The output schemas are documented in docs/ASSET_FORMATS.md.

The XML spells file names in any case ("ballHitBoard.ogg" for ballhitboard.ogg,
"Cowboy.ogg" for cowboy.ogg); the JSON carries the names of the files that exist,
so a case-sensitive file system finds them too. A sample whose file is missing is
left out and reported: the original shipped no sample for most "*_drop" sounds and
the game stays silent for them.
"""
import argparse
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

VERSION = 1


def _decode(xml_bytes):
    """sounds.xml has no XML declaration and its comments are Russian in the DOS
    code page (cp866), which is not UTF-8. Names and file names are ASCII."""
    try:
        return xml_bytes.decode("utf-8")
    except UnicodeDecodeError:
        return xml_bytes.decode("cp866")


def _root(xml_bytes, tag):
    root = ET.fromstring(_decode(xml_bytes))
    if root.tag != tag:
        raise ValueError(f"expected a <{tag}> root element, found <{root.tag}>")
    return root


def _resolver(files):
    """name as the XML spells it -> the file's real name, or None. An exact match
    wins over a match that ignores case."""
    exact = set(files)
    lower = {}
    for name in sorted(exact):
        lower.setdefault(name.lower(), name)
    return lambda name: name if name in exact else lower.get(name.lower())


def _number(element, name, kind, minimum):
    raw = element.get(name)
    if raw is None:
        return None
    try:
        value = kind(raw)
    except ValueError:
        raise ValueError(f"<{element.tag} {name}={raw!r}> is not a number") from None
    if value < minimum:
        raise ValueError(f"<{element.tag} {name}={raw!r}> must be at least {minimum}")
    return value


def _sample(element, resolve, sound):
    filename = element.get("filename")
    if not filename:
        raise ValueError(f"sound {sound!r}: <Sample> without a filename")
    try:
        delay = _number(element, "delay", int, 0)
        start = _number(element, "freqStart", float, 0.0)
        end = _number(element, "freqEnd", float, 0.0)
    except ValueError as e:
        raise ValueError(f"sound {sound!r}: {e}") from None
    out = {"file": resolve(filename)}
    if delay:
        out["delayMs"] = delay
    if start is not None or end is not None:
        # One bound alone fixes the pitch.
        out["freqStart"] = start if start is not None else end
        out["freqEnd"] = end if end is not None else start
    return filename, out


def parse_sounds(xml_bytes, files):
    """sounds.xml contents and the names in Sounds/ -> (the sounds.json document,
    a report with the sounds defined twice and the samples whose file is missing)."""
    resolve = _resolver(files)
    sounds, seen = [], set()
    report = {"duplicates": [], "unresolved": []}
    for element in _root(xml_bytes, "root").findall("Sound"):
        name = element.get("name")
        if not name:
            raise ValueError("<Sound> without a name")
        if name.lower() in seen:
            report["duplicates"].append(name)  # the first definition wins
            continue
        seen.add(name.lower())
        samples = []
        for sample in element.findall("Sample"):
            filename, out = _sample(sample, resolve, name)
            if out["file"] is None:
                report["unresolved"].append([name, filename])
            else:
                samples.append(out)
        sounds.append({"name": name, "samples": samples})
    return {"version": VERSION, "sounds": sounds}, report


def parse_musics(xml_bytes, files):
    """musics.xml contents and the names in Musics/ -> (the musics.json document,
    the names of the tracks whose file is missing; those are left out)."""
    resolve = _resolver(files)
    musics, unresolved = [], []
    for element in _root(xml_bytes, "root").findall("Music"):
        name = element.get("name")
        stream = element.find("Stream")
        if not name:
            raise ValueError("<Music> without a name")
        if stream is None or not stream.get("filename"):
            raise ValueError(f"music {name!r}: no <Stream filename=...>")
        file = resolve(stream.get("filename"))
        if file is None:
            unresolved.append(name)
        else:
            musics.append({"name": name, "file": file})
    return {"version": VERSION, "musics": musics}, unresolved


def _format(key, rows):
    """One entry per line: compact enough to ship, readable enough to diff."""
    lines = ",\n".join("  " + json.dumps(row) for row in rows)
    return f'{{"version": {VERSION},\n "{key}": [\n{lines}\n ]}}\n'


def _file_names(folder):
    return [p.name for p in pathlib.Path(folder).iterdir() if p.is_file()]


def convert(xml_dir, sounds_dir, musics_dir, out_dir):
    """Write sounds.json and musics.json into out_dir; returns statistics."""
    xml_dir, out_dir = pathlib.Path(xml_dir), pathlib.Path(out_dir)
    sounds, report = parse_sounds((xml_dir / "sounds.xml").read_bytes(), _file_names(sounds_dir))
    musics, missing_musics = parse_musics((xml_dir / "musics.xml").read_bytes(), _file_names(musics_dir))
    (out_dir / "sounds.json").write_text(_format("sounds", sounds["sounds"]), encoding="utf-8")
    (out_dir / "musics.json").write_text(_format("musics", musics["musics"]), encoding="utf-8")
    return {
        "sounds": len(sounds["sounds"]),
        "samples": sum(len(s["samples"]) for s in sounds["sounds"]),
        "silent": [s["name"] for s in sounds["sounds"] if not s["samples"]],
        "unresolved": report["unresolved"],
        "duplicates": report["duplicates"],
        "musics": len(musics["musics"]),
        "missingMusics": missing_musics,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("assets", type=pathlib.Path, help="converted assets folder (Data/xml, Sounds, Musics)")
    args = ap.parse_args()
    folder = args.assets
    try:
        stats = convert(folder / "Data" / "xml", folder / "Sounds", folder / "Musics", folder / "Data" / "xml")
    except (OSError, ET.ParseError, ValueError) as e:
        sys.exit(f"{folder}: {e}")
    print(f"sounds: {stats['sounds']} with {stats['samples']} samples, {len(stats['silent'])} without any "
          f"(their files are missing), {stats['musics']} musics")
    if stats["duplicates"]:
        print("defined more than once (first wins): " + ", ".join(stats["duplicates"]))
    print(f"{len(stats['unresolved'])} samples name a file that does not exist: "
          + ", ".join(sorted({file for _, file in stats["unresolved"]})))
    if stats["missingMusics"]:
        print("musics without a file (left out): " + ", ".join(stats["missingMusics"]))
    print(f"wrote {folder / 'Data' / 'xml' / 'sounds.json'} and musics.json")


if __name__ == "__main__":
    main()
