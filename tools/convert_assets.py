#!/usr/bin/env python3
"""Build the runtime asset folder from your own copy of Magic Ball 3.

Usage:
    python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
        --extracted /path/to/MB3OUT --out assets

--game       the installed game folder (holds Textures.dat, Sounds.dat, Musics.dat)
--extracted  the folder produced by tools/molebox-dump (holds Data/ from MagicBall3.dat)
--out        destination; created if missing, never committed (see .gitignore)
"""
import argparse
import json
import pathlib
import shutil
import sys
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import materials  # noqa: E402
import mb3level  # noqa: E402
import nxconvex  # noqa: E402
import sounds  # noqa: E402
import textures  # noqa: E402
import x2glb  # noqa: E402

PLAIN_ARCHIVES = {"Textures.dat": "Textures", "Sounds.dat": "Sounds", "Musics.dat": "Musics"}


def unpack_archives(game, out):
    for name, sub in PLAIN_ARCHIVES.items():
        src = game / name
        if not src.exists():
            sys.exit(f"missing {src}")
        with zipfile.ZipFile(src) as z:
            z.extractall(out / sub)
        print(f"{name}: {len(z.namelist())} entries -> {sub}/")


def copy_data(extracted, out):
    src = extracted / "Data"
    if not src.is_dir():
        sys.exit(f"missing {src} (run tools/molebox-dump first)")
    shutil.copytree(src, out / "Data", dirs_exist_ok=True)


def convert_textures(out):
    stats = textures.convert_all([out / "Textures", out / "Data" / "Fonts"])
    print(f"textures: {stats['files']} DDS converted to PNG {stats['formats']}")


def convert_materials(out):
    folder = out / "Data" / "Materials"
    stats = materials.convert(folder / "materials.xml", folder / "materials.json")
    print(f"materials: {stats['materials']} converted, duplicates {stats['duplicates']}")


def convert_sounds(out):
    xml = out / "Data" / "xml"
    stats = sounds.convert(xml, out / "Sounds", out / "Musics", xml)
    print(f"sounds: {stats['sounds']} converted, {stats['samples']} samples, {len(stats['silent'])} sounds silent "
          f"(no file), duplicates {stats['duplicates']}; musics: {stats['musics']}, missing {stats['missingMusics']}")


def convert_meshes(out):
    stats = x2glb.convert_all(out / "Data" / "SkinMeshes", out / "Textures", out / "Data" / "Meshes")
    print(f"meshes: {stats['files']} converted to glTF, {stats['triangles']} triangles, "
          f"unresolved textures {sorted(stats['unresolved_textures'])}")


def convert_levels(out):
    levels = sorted((out / "Data" / "Levels").glob("*.mb3"), key=lambda p: p.name.lower())
    # Convex shapes embed the hull points of the meshes in Data/NxMeshes, so the runtime needs no cooked format.
    hulls = nxconvex.HullFolder(out / "Data" / "NxMeshes")
    for path in levels:
        try:
            level = mb3level.parse(path.read_bytes(), hulls)
        except (FileNotFoundError, ValueError) as e:
            sys.exit(f"{path.name}: {e}")
        if level["trailingBytes"]:
            sys.exit(f"{path.name}: {level['trailingBytes']} unparsed bytes")
        path.with_suffix(".json").write_text(json.dumps(level))
    print(f"levels: {len(levels)} converted to JSON")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, type=pathlib.Path)
    ap.add_argument("--extracted", required=True, type=pathlib.Path)
    ap.add_argument("--out", default=pathlib.Path("assets"), type=pathlib.Path)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    unpack_archives(args.game, args.out)
    copy_data(args.extracted, args.out)
    convert_textures(args.out)
    convert_materials(args.out)
    convert_sounds(args.out)
    convert_meshes(args.out)
    convert_levels(args.out)
    print(f"done: {args.out.resolve()}")


if __name__ == "__main__":
    main()
