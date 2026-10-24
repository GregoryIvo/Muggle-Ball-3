#!/usr/bin/env python3
"""Build the PlayStation Vita asset pack: a mirror of the files the runtime actually opens.

Usage:
    python3 tools/vita_pack.py --out /path/to/pack [--assets DIR] [--no-resample] [--dry-run] [--quiet]

--assets  the converted asset folder (default: $MB3_ASSETS, else ./assets; the output of
          tools/convert_assets.py)
--out     where to write the pack; created if missing. Refused if it is --assets itself, sits inside
          --assets or has --assets inside it, sits inside this repository (docs/VITA_PLAN.md 5.6: the
          pack is copied to ux0:data/MuggleBall3/assets or bundled read-only at app0:/assets, never
          committed), is an existing file rather than a folder, or already has unrelated content in it
          (this tool only ever prunes a folder carrying its own vita_pack.json manifest)
--no-resample  copy every file as it is, even non-power-of-two mipmapped textures
--dry-run      print what would happen; write nothing (not even --out itself)
--quiet        only print the final summary line

What gets copied -- the runtime's own file-open calls, nothing more (folder under --assets, whether its
subfolders are searched too, the extensions it opens, any case, and the loader that proves it; see
RUNTIME_LOCATIONS below):

    Textures/**/*.jpg,*.jpeg,*.png   game/src/TextureIndex.cpp (recursive scan of <assets>/Textures),
                                      decoded by engine/src/Image.cpp for both ModelCache.cpp and
                                      UiAssets.cpp/BitmapFont.cpp
    Data/Fonts/*.json, *.png         game/src/UiAssets.cpp (directory scan for the .json) and
                                      game/src/BitmapFont.cpp (the atlas named inside each .json)
    Data/Levels/*.json               game/src/LevelCatalog.cpp (directory scan), game/src/LevelLoader.cpp
    Data/Materials/*.json            game/src/MaterialLibrary.cpp (opens materials.json by name; no other
                                      .json ever lives in this folder, so matching by extension is the
                                      same rule)
    Data/Menu/*.json                 game/src/UiAssets.cpp formNames() / loadForm()
    Data/Particles/*.json            game/src/ParticleDef.cpp ParticleLibrary::load (directory scan)
    Data/xml/*.json                  languages.json: game/src/StringTable.cpp; levels.json:
                                      game/src/Campaign.cpp; sounds.json/musics.json:
                                      game/src/SoundLibrary.cpp (each opened by name, as Data/Materials
                                      above)
    Sounds/*.ogg, Musics/*.ogg       game/src/SoundLibrary.cpp builds these paths; decoded by
                                      engine/src/Vorbis.cpp
    Data/Meshes/**/*.glb             game/src/ModelCache.cpp model(); parsed by engine/src/Model.cpp

Everything else under --assets is produced by tools/convert_assets.py as intermediate or source data and
is never opened at process time, so it is never copied -- in particular:
  - Data/Objects/**/*.json (348 files): despite the extension, nothing under "Objects" is ever opened at
    runtime (grep game/, engine/, app/ for the literal "Objects" and see for yourself). These are the
    level editor's object *templates*, converted from Data/Objects/**/*.xml and *.XML for documentation
    (docs/ASSET_FORMATS.md); the concrete per-level data they describe already lives inside each level's
    own Data/Levels/*.json, baked in by tools/mb3level.py when the .mb3 binary was parsed. A handful of
    values (icons, drop weights...) were hand-copied from these templates into game/include/mb/game/
    Prizes.h and Weapons.h during reverse engineering -- also not a runtime file read.
  - Data/SkinMeshes/**/*.X, *.x: the original meshes, superseded by Data/Meshes/**/*.glb.
  - Data/NxMeshes/**/*.cnvx: convex hull points, baked into each level's JSON by tools/nxconvex.py.
  - *.mb3 (the original compiled levels), *.xml/*.XML (every converter's own source), *.dds (every one
    has a *.png sibling that always wins -- TextureIndex.cpp ranks .png before .jpg before anything else,
    so a .dds is never the file that actually gets opened), Data/Fonts/*.crd and *.ttf (baked into the
    font JSON and PNG atlas by tools/ui.py), Data/MB3cursor.ani, and AppIcon.icns (macOS packaging only).

Mipmapped (3D) vs UI textures, for the resampling rule (docs/VITA_PLAN.md 5.7 "Textures"): vitaGL's mip
generator misbehaves on a non-power-of-two (NPOT) texture, so a texture that ModelCache.cpp loads WITH
mipmaps (createTexture()'s default, mipmaps=true) needs to be power-of-two; one that UiAssets.cpp /
BitmapFont.cpp load WITHOUT them (createTexture(image, false)) does not, and some of those rely on being
their exact original size (the scrolling Textures/Forms/Sky.jpg tiles at its own pixel size), so they are
left alone. A texture under Textures/ is UI when it sits in Textures/Forms/ or Textures/Icons/ (both are
entirely menu and HUD art in the shipped data), or when some Data/Menu/*.json form's img_name attribute
names it (the one other way a root-level file is UI art despite not living in either folder, e.g. the
splash screen and the loading bar). Every other file under Textures/ -- the world materials, plus
Textures/Oreols/ and Textures/Particles/, which the particle and prize-pickup effects load through
ModelCache.cpp, not UiAssets.cpp -- is a resampling candidate when its width or height is not a power of
two.

One exception, confirmed harmless in the shipped data rather than guessed: Data/Materials/materials.json
has a System-group material "Lazer.png" with textures ["lazer"], which TextureIndex::find() resolves to
Textures/Icons/Lazer.png (76x76, NPOT) because no root-level lazer.* exists -- a mipmapped (cache.texture)
material living inside the "always UI" Icons/ folder. Nothing reaches it at runtime: Weapons.cpp's laser
uses the "bat_lazer" material (textures ["Metall-Raketka-Red"]) and the "Laser" particle
(Particles/ParticleLaser1.png, which this tool does resample); no Data/Levels object and no glb extras
name "Lazer.png"; the only JSON naming it is the never-read template Data/Objects/Bullets/lazer.json. So
treating Icons/ as entirely UI stays correct for the shipped data. If a future change ever makes a level,
glb or particle reach that material, the folder shortcut stops being accurate and the resample set should
instead be derived from the materials/particles/glb names that actually get drawn.

Dimensions are read from the file's own header (PNG IHDR; JPEG SOF0/SOF2, or any other SOFn marker) with
the standard library, never by decoding the image. A Textures/ file whose header this parser cannot read
is still copied, but listed in the manifest's unreadableTextures (never silently dropped from every
report, since the manifest's whole purpose is being the device-side fallback list). Resampling (stretch
to the next power of two in each dimension, never pad -- the UVs are normalised, so nothing else about
how a texture is used changes) is tried with Pillow (LANCZOS; JPEG quality 95, PNG as PNG) if it is
importable, else with the macOS `sips` command-line tool if it is on PATH, else the file is left as it
was copied and listed in the manifest as unresampled, so a device-side fallback (generating mips by hand
for that one texture, or simply not mipmapping it) knows which textures still need it. A resample attempt
that raises (a header that lied about the body, or a failing `sips`) is caught per file: the file is
copied as is, logged, and listed as unresampled too -- one bad texture never aborts the whole pack.

Already-up-to-date files are skipped, so a second run is quick: same mtime as --out's copy, and -- for a
file this run would resample -- --out's own header already reports the resampled size (not just a
matching mtime, which a prior --no-resample run or a copy/sips/Pillow-unavailable fallback could share
with a file that still needs resampling), or, for every other file, the same size as the source. Any
file --out has that --assets no longer does is removed, so the pack mirrors the source exactly -- except
when --out is not empty and was not itself built by this tool (no vita_pack.json inside it), in which
case the whole run is refused rather than risk deleting someone else's files.
"""
import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST_NAME = "vita_pack.json"
RULE_VERSION = 1  # bump when RUNTIME_LOCATIONS or the UI/mipmap rule below changes what gets copied/resampled

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PackError(ValueError):
    """A reason the pack cannot be built (bad --out, missing --assets); not a bug in a single file."""


class HeaderError(ValueError):
    """A texture file's header could not be read; the file is still copied, just never resampled."""


# --- Header parsing (PNG IHDR, JPEG SOFn) ---------------------------------------------------------------

def png_size(data):
    """(width, height) from a PNG's IHDR chunk, the first 8 bytes of which are always width then height."""
    if len(data) < 24 or data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        raise HeaderError("not a PNG file")
    return struct.unpack(">II", data[16:24])


# Start-of-frame markers that carry a frame header (width/height); C4/C8/CC are DHT/JPG/DAC, not a frame.
_SOF_MARKERS = frozenset({0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF})
# Markers with no length field or payload: SOI, EOI, TEM and the 8 restart markers.
_MARKERS_WITHOUT_PAYLOAD = frozenset({0xD8, 0xD9, 0x01} | set(range(0xD0, 0xD8)))


def jpeg_size(data):
    """(width, height) from a JPEG's first SOF0/SOF2 (or other SOFn) marker segment."""
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        raise HeaderError("not a JPEG file")
    offset = 2
    while offset + 2 <= len(data):
        if data[offset] != 0xFF:
            raise HeaderError("malformed JPEG marker")
        # ITU-T.81 B.1.1.2: any number of 0xFF fill bytes may precede a marker's code byte.
        while offset + 1 < len(data) and data[offset + 1] == 0xFF:
            offset += 1
        if offset + 1 >= len(data):
            raise HeaderError("truncated JPEG marker")
        marker = data[offset + 1]
        if marker in _MARKERS_WITHOUT_PAYLOAD:
            offset += 2
            continue
        if offset + 4 > len(data):
            break
        length = struct.unpack(">H", data[offset + 2:offset + 4])[0]
        if marker in _SOF_MARKERS:
            if offset + 9 > len(data):
                raise HeaderError("truncated SOF segment")
            height, width = struct.unpack(">HH", data[offset + 5:offset + 9])
            return width, height
        offset += 2 + length
    raise HeaderError("no SOF marker found")


# The biggest texture side the Vita's GPU takes. A header claiming more is a broken file, not a texture:
# resampling it would ask Pillow or sips for an absurd image (Pillow refuses with its decompression-bomb
# error, which is not an OSError), so such a file is treated as unreadable and copied as it is.
MAX_TEXTURE_SIDE = 4096


def read_texture_size(path, relpath):
    """(width, height) of a texture file, or None if its header cannot be read or claims a size no Vita
    texture can have (reported by the caller, not fatal: the file is still copied, just never a resampling
    candidate)."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    try:
        width, height = png_size(data) if relpath.lower().endswith(".png") else jpeg_size(data)
    except HeaderError:
        return None
    if not (0 < width <= MAX_TEXTURE_SIDE and 0 < height <= MAX_TEXTURE_SIDE):
        return None
    return width, height


# --- Power-of-two math -----------------------------------------------------------------------------------

def next_pot(n):
    """The smallest power of two >= n (1 if n <= 1)."""
    return 1 if n <= 1 else 1 << (n - 1).bit_length()


# --- UI vs mipmapped texture classification --------------------------------------------------------------

# Entirely menu/HUD art in the shipped data (see the module docstring): UiAssets.cpp image(), Hud.cpp.
UI_TEXTURE_FOLDERS = frozenset({"forms", "icons"})


def _texture_key(name):
    """A texture name reduced to how TextureIndex::find() matches it: lowercase, '/' separators, no
    extension (so "Forms/sky.jpg" and a form's img_name="forms/Sky.dds" compare equal)."""
    normalized = name.strip().lower().replace("\\", "/")
    stem, _ext = os.path.splitext(normalized)
    return stem


def _collect_img_names(node, keys):
    if not isinstance(node, dict):
        return
    name = node.get("attributes", {}).get("img_name")
    if isinstance(name, str) and name:
        keys.add(_texture_key(name))
    for child in node.get("children", ()):
        _collect_img_names(child, keys)


def ui_texture_keys(menu_dir):
    """The normalised (_texture_key) img_name of every widget in every Data/Menu/*.json form: the other
    way a texture is UI art despite not sitting in Textures/Forms or Textures/Icons (the splash screen,
    the loading bar)."""
    keys = set()
    if not menu_dir.is_dir():
        return keys
    for form_path in sorted(menu_dir.glob("*.json")):
        try:
            document = json.loads(form_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            continue
        _collect_img_names(document, keys)
    return keys


def is_ui_texture(textures_relpath, ui_keys):
    """True for a Textures/-relative path (posix, e.g. "Icons/Heart.png") that the game draws without
    mipmaps rather than loading through ModelCache.cpp; see the module docstring."""
    parts = textures_relpath.lower().split("/")
    if len(parts) > 1 and parts[0] in UI_TEXTURE_FOLDERS:
        return True
    return _texture_key(textures_relpath) in ui_keys or _texture_key(parts[-1]) in ui_keys


# --- Which files the runtime reads -----------------------------------------------------------------------

class RuntimeLocation:
    def __init__(self, folder, recursive, extensions):
        self.folder = folder
        self.recursive = recursive
        self.extensions = extensions  # lowercase, with the leading dot


# See the module docstring for the loader that justifies each line.
RUNTIME_LOCATIONS = [
    RuntimeLocation("Textures", True, (".jpg", ".jpeg", ".png")),
    RuntimeLocation("Data/Fonts", False, (".json", ".png")),
    RuntimeLocation("Data/Levels", False, (".json",)),
    RuntimeLocation("Data/Materials", False, (".json",)),
    RuntimeLocation("Data/Menu", False, (".json",)),
    RuntimeLocation("Data/Particles", False, (".json",)),
    RuntimeLocation("Data/xml", False, (".json",)),
    RuntimeLocation("Sounds", False, (".ogg",)),
    RuntimeLocation("Musics", False, (".ogg",)),
    RuntimeLocation("Data/Meshes", True, (".glb",)),
]


def runtime_files(assets_dir):
    """Maps every file the runtime reads to its real path, keyed by its path relative to assets_dir
    (posix separators, original case)."""
    found = {}
    for location in RUNTIME_LOCATIONS:
        base = assets_dir / location.folder
        if not base.is_dir():
            continue
        paths = base.rglob("*") if location.recursive else base.iterdir()
        for path in paths:
            if path.is_file() and path.name.lower().endswith(location.extensions):
                found[path.relative_to(assets_dir).as_posix()] = path
    return found


# --- Resampling --------------------------------------------------------------------------------------------

def _pillow_image_module():
    """The PIL.Image module, or None if Pillow is not installed. A function (not a module-level constant)
    so a test can monkeypatch it to exercise the sips/copy fallbacks even where Pillow is importable."""
    try:
        from PIL import Image
        return Image
    except ImportError:
        return None


def resample_texture(src, dst, target_width, target_height):
    """Writes src resampled to target_width x target_height at dst. Returns the method used: "pillow",
    "sips", or "copy" (Pillow is not installed and sips is not on PATH, so the file is left as it is,
    and the caller lists it as unresampled)."""
    image_module = _pillow_image_module()
    if image_module is not None:
        resample_filter = getattr(image_module, "Resampling", image_module).LANCZOS
        is_jpeg = dst.suffix.lower() in (".jpg", ".jpeg")
        with image_module.open(src) as source_image:
            resized = source_image.convert("RGB" if is_jpeg else "RGBA").resize(
                (target_width, target_height), resample_filter)
            if is_jpeg:
                resized.save(dst, "JPEG", quality=95)
            else:
                resized.save(dst, "PNG")
        return "pillow"
    if shutil.which("sips"):
        subprocess.run(["sips", "-z", str(target_height), str(target_width), str(src), "--out", str(dst)],
                       check=True, capture_output=True)
        return "sips"
    shutil.copy2(src, dst)
    return "copy"


# --- Mirroring ----------------------------------------------------------------------------------------------

def _is_inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def is_up_to_date(src_stat, dst, rel, target):
    """Whether dst already holds src's content: same whole-second mtime (some filesystems keep no finer
    resolution) and, for a file this run would resample (target is the (width, height) it should end up
    at), dst's own header already reports exactly that size -- a matching mtime alone is not enough,
    since a prior --no-resample run, or a run where neither Pillow nor sips was available, sets dst's
    mtime to the source's while leaving dst's content non-power-of-two. For every other file (target is
    None), the same size as the source is enough."""
    if not dst.exists():
        return False
    dst_stat = dst.stat()
    if int(src_stat.st_mtime) != int(dst_stat.st_mtime):
        return False
    if target is not None:
        return read_texture_size(dst, rel) == target
    return src_stat.st_size == dst_stat.st_size


def _prune_empty_dirs(root):
    for dirpath, dirnames, filenames in os.walk(root, topdown=False):
        if pathlib.Path(dirpath) != root and not dirnames and not filenames:
            os.rmdir(dirpath)


def remove_stale(out_dir, keep_relpaths, dry_run, log):
    """Deletes every file under out_dir that is not in keep_relpaths (so the pack mirrors --assets
    exactly), except the manifest itself. Returns the relative paths removed (or, in --dry-run, that
    would be)."""
    if not out_dir.is_dir():
        return []
    removed = []
    for path in sorted(out_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(out_dir).as_posix()
        if rel == MANIFEST_NAME or rel in keep_relpaths:
            continue
        removed.append(rel)
        log(f"remove: {rel}")
        if not dry_run:
            path.unlink()
    if not dry_run:
        _prune_empty_dirs(out_dir)
    return removed


def _resize_target(rel, src, ui_keys):
    """(target, unreadable) for `rel`: target is the (width, height) to resample it to, or None if it is
    not a Textures/ file, is UI art (whose header is never even read -- it is left alone regardless of
    its size), or is already power-of-two; unreadable is True when `rel` IS a Textures/, non-UI file
    whose header this parser could not read, which the caller must report (the file is still copied, just
    as it is, and is never a resize candidate -- see read_texture_size)."""
    if not rel.startswith("Textures/"):
        return None, False
    if is_ui_texture(rel[len("Textures/"):], ui_keys):
        return None, False
    dims = read_texture_size(src, rel)
    if dims is None:
        return None, True
    target = (next_pot(dims[0]), next_pot(dims[1]))
    return (None, False) if target == dims else (target, False)


def npot_texture_report(files, ui_keys, out_dir, dry_run, resample):
    """[(relpath, (width, height))] of every mipmapped texture that is still not power-of-two in the pack
    as it stands right now (ground truth: whatever is actually on disk, or, in --dry-run where nothing
    was written, whatever --assets has) -- except in --dry-run with resample on, where a real run would
    resample every candidate, so none is reported as left unresampled."""
    report = []
    for rel in sorted(files):
        src = files[rel]
        target, _unreadable = _resize_target(rel, src, ui_keys)
        if target is None:
            continue
        if dry_run and resample:
            continue
        dst = out_dir / rel
        current = read_texture_size(dst, rel) if (not dry_run and dst.exists()) else read_texture_size(src, rel)
        if current and (next_pot(current[0]), next_pot(current[1])) != current:
            report.append((rel, current))
    return report


def build_pack(assets_dir, out_dir, resample=True, dry_run=False, log=lambda message: None):
    """Builds (or previews, with dry_run) the pack and returns a report dict; raises PackError if --out
    is unsafe. See the module docstring for exactly what is copied and resampled."""
    assets_dir = pathlib.Path(assets_dir)
    out_dir = pathlib.Path(out_dir)
    if not assets_dir.is_dir():
        raise PackError(f"no such assets folder: {assets_dir}")
    assets_resolved = assets_dir.resolve()
    out_resolved = out_dir.resolve()
    if out_resolved == assets_resolved:
        raise PackError("--out must not be the same folder as --assets")
    if _is_inside(out_resolved, REPO_ROOT):
        raise PackError(f"--out must not be inside the repository ({REPO_ROOT}); pick a path outside it")
    if _is_inside(out_resolved, assets_resolved):
        raise PackError(f"--out must not be inside --assets ({assets_dir}); pick a separate folder")
    if _is_inside(assets_resolved, out_resolved):
        raise PackError(f"--out must not contain --assets ({assets_dir}); pick a separate folder, not "
                         "one that --assets already sits inside")
    if out_resolved.exists() and not out_resolved.is_dir():
        raise PackError(f"--out is not a folder: {out_dir}")
    if (out_resolved.is_dir() and any(out_resolved.iterdir())
            and not (out_resolved / MANIFEST_NAME).is_file()):
        raise PackError(f"--out exists and was not made by this tool (no {MANIFEST_NAME} in it); "
                         "pick an empty folder, or one this tool already built the pack into")

    files = runtime_files(assets_dir)
    ui_keys = ui_texture_keys(assets_dir / "Data" / "Menu")

    if not dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)
    removed = remove_stale(out_dir, set(files), dry_run, log)

    copied_files = copied_bytes = skipped_files = 0
    resampled = []
    unreadable_textures = []

    for rel in sorted(files):
        src = files[rel]
        dst = out_dir / rel
        src_stat = src.stat()
        target, unreadable = _resize_target(rel, src, ui_keys)
        if unreadable:
            unreadable_textures.append(rel)
            log(f"unreadable image header, copied as is: {rel}")
        will_resample = target is not None and resample

        if is_up_to_date(src_stat, dst, rel, target if will_resample else None):
            skipped_files += 1
            continue

        if dry_run:
            if target is not None:
                dims = read_texture_size(src, rel)
                if resample:
                    resampled.append({"path": rel, "originalSize": list(dims), "newSize": list(target),
                                       "method": "planned"})
                    log(f"would resample: {rel} {dims[0]}x{dims[1]} -> {target[0]}x{target[1]}")
                else:
                    log(f"would copy (NPOT, --no-resample): {rel}")
            else:
                log(f"would copy: {rel}")
            copied_files += 1
            copied_bytes += src_stat.st_size
            continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        if will_resample:
            dims = read_texture_size(src, rel)
            try:
                method = resample_texture(src, dst, *target)
            except Exception as error:  # noqa: BLE001 -- one bad texture must never abort the pack
                # Pillow raises its own classes for a corrupt body (DecompressionBombError derives from
                # Exception, not OSError), sips reports through SubprocessError, and a huge image can end
                # in MemoryError; whatever it was, the file is copied as it is and reported.
                if dst.exists():
                    dst.unlink()
                shutil.copy2(src, dst)
                log(f"could not resample ({error}): {rel} stays {dims[0]}x{dims[1]}, not a power of two")
            else:
                if method == "copy":
                    log(f"leaving as copied (no Pillow and no sips): {rel} is {dims[0]}x{dims[1]}, "
                        f"not a power of two")
                else:
                    resampled.append({"path": rel, "originalSize": list(dims), "newSize": list(target),
                                       "method": method})
                    log(f"resample: {rel} {dims[0]}x{dims[1]} -> {target[0]}x{target[1]} ({method})")
        else:
            shutil.copy2(src, dst)
        os.utime(dst, (src_stat.st_mtime, src_stat.st_mtime))
        copied_files += 1
        copied_bytes += dst.stat().st_size

    unresampled = npot_texture_report(files, ui_keys, out_dir, dry_run, resample)
    total_bytes = sum((out_dir / rel).stat().st_size if (not dry_run and (out_dir / rel).exists())
                       else src.stat().st_size
                       for rel, src in files.items())

    report = {
        "ruleVersion": RULE_VERSION,
        "totalFiles": len(files),
        "totalBytes": total_bytes,
        "filesCopied": copied_files,
        "bytesCopied": copied_bytes,
        "skippedFiles": skipped_files,
        "unreadableTextures": sorted(unreadable_textures),
        "removedFiles": removed,
        "resampledTextures": resampled,
        "unresampledNpotTextures": [{"path": rel, "size": list(size)} for rel, size in unresampled],
    }
    if not dry_run:
        (out_dir / MANIFEST_NAME).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                             encoding="utf-8")
    return report


# --- CLI ------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", type=pathlib.Path, default=pathlib.Path(os.environ.get("MB3_ASSETS", "assets")))
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--no-resample", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    def log(message):
        if not args.quiet:
            print(message)

    try:
        report = build_pack(args.assets, args.out, resample=not args.no_resample, dry_run=args.dry_run, log=log)
    except PackError as e:
        sys.exit(f"vita_pack: {e}")

    megabytes = report["totalBytes"] / 1_000_000
    print(f"vita_pack: {report['totalFiles']} files, {megabytes:.1f} MB; {report['filesCopied']} copied, "
          f"{report['skippedFiles']} skipped, {len(report['resampledTextures'])} resampled, "
          f"{len(report['unresampledNpotTextures'])} NPOT texture(s) left unresampled, "
          f"{len(report['unreadableTextures'])} texture(s) with an unreadable header")


if __name__ == "__main__":
    main()
