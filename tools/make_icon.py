#!/usr/bin/env python3
"""Build the macOS app icon (AppIcon.icns) from the game's own icon.

Usage:
    python3 tools/make_icon.py --game "/path/to/Magic Ball 3" --out assets/AppIcon.icns
    python3 tools/make_icon.py --ico /path/to/some.ico --out assets/AppIcon.icns

--game  the installed game folder; the icon is read from the PE resources of its MagicBall3.exe
        (RT_GROUP_ICON "IDI_APPICON" and its RT_ICON images)
--ico   an .ico file instead (for an executable whose resources cannot be read)
--out   the .icns to write (default assets/AppIcon.icns); needs macOS, for iconutil

The game's icon holds 48, 32, 24 and 16 pixel images only. Sizes it has are used as they are; the
larger ones the .icns wants (up to 1024) are resampled from the biggest image with a Lanczos filter.
CMake copies assets/AppIcon.icns into the app bundle when it exists. The icon is the game's artwork,
so it stays in the gitignored assets folder like every other converted file.
"""
import argparse
import math
import pathlib
import platform
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import textures  # noqa: E402  (its PNG encoder)

RT_ICON = 3
RT_GROUP_ICON = 14

# .iconset file names and the pixel size each holds.
ICONSET = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]
LANCZOS_LOBES = 3


class IconError(ValueError):
    pass


# --- Icon images ----------------------------------------------------------------

class Image:
    """RGBA8, rows top to bottom."""

    def __init__(self, width, height, rgba, bit_count):
        self.width, self.height, self.rgba, self.bit_count = width, height, rgba, bit_count


def decode_dib(data):
    """An icon image as stored in .ico files and RT_ICON resources: a BITMAPINFOHEADER whose height
    counts the colour rows plus the 1-bit AND mask, then the palette, the bottom-up colour rows
    and the mask rows. Handles 1, 4, 8, 24 and 32 bits per pixel, uncompressed."""
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        raise IconError("PNG-compressed icon images are not supported")
    if len(data) < 40:
        raise IconError("truncated icon image")
    header_size, width, height2, planes, bits, compression, _size, _xp, _yp, colors_used, _imp = \
        struct.unpack_from("<IiiHHIIiiII", data, 0)
    if header_size < 40 or compression != 0 or bits not in (1, 4, 8, 24, 32):
        raise IconError(f"unsupported icon image (header {header_size}, {bits} bpp, compression {compression})")
    height = height2 // 2
    if width <= 0 or height <= 0 or width > 256 or height > 256:
        raise IconError(f"bad icon image size {width}x{height2}")
    pos = header_size
    palette = []
    if bits <= 8:
        count = colors_used or 1 << bits
        palette = [data[pos + 4 * i:pos + 4 * i + 3] for i in range(count)]  # B, G, R
        pos += 4 * count
    stride = (width * bits + 31) // 32 * 4
    mask_stride = (width + 31) // 32 * 4
    mask_pos = pos + stride * height
    if len(data) < mask_pos:
        raise IconError("truncated icon image")
    has_mask = len(data) >= mask_pos + mask_stride * height

    out = bytearray(width * height * 4)
    for y in range(height):
        row = pos + stride * (height - 1 - y)  # rows are stored bottom-up
        mask_row = mask_pos + mask_stride * (height - 1 - y)
        for x in range(width):
            if bits >= 24:
                step = bits // 8
                b, g, r = data[row + x * step:row + x * step + 3]
                a = data[row + x * 4 + 3] if bits == 32 else 255
            else:
                per_byte = 8 // bits
                shift = 8 - bits * (x % per_byte + 1)
                index = data[row + x // per_byte] >> shift & (1 << bits) - 1
                if index >= len(palette):
                    raise IconError("palette index out of range")
                b, g, r = palette[index]
                a = 255
            if has_mask and bits < 32 and data[mask_row + x // 8] >> (7 - x % 8) & 1:
                a = 0
            out[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes((r, g, b, a))
    if bits == 32 and has_mask and not any(out[3::4]):
        # 32-bit images from before alpha existed carry their shape in the mask alone.
        for y in range(height):
            mask_row = mask_pos + mask_stride * (height - 1 - y)
            for x in range(width):
                if not data[mask_row + x // 8] >> (7 - x % 8) & 1:
                    out[(y * width + x) * 4 + 3] = 255
    return Image(width, height, bytes(out), bits)


def read_ico(data):
    """The images of an .ico file, as a list of Image (entries that cannot be decoded are skipped)."""
    if len(data) < 6 or struct.unpack_from("<HH", data, 0) != (0, 1):
        raise IconError("not an .ico file")
    try:
        count = struct.unpack_from("<H", data, 4)[0]
        blobs = []
        for i in range(count):
            size, offset = struct.unpack_from("<II", data, 6 + 16 * i + 8)
            blobs.append(data[offset:offset + size])
    except struct.error:
        raise IconError("damaged .ico directory") from None
    return _decode_all(blobs)


def _decode_all(blobs):
    images, errors = [], []
    for blob in blobs:
        try:
            images.append(decode_dib(blob))
        except IconError as e:
            errors.append(str(e))
    if not images:
        raise IconError("no usable icon image" + (f" ({errors[0]})" if errors else ""))
    return images


# --- PE resources ---------------------------------------------------------------

class PeResources:
    """Reads resources from a PE32 or PE32+ image: the section table maps the RVAs the resource
    directory uses to file offsets. Packers often move the data of a resource into another section,
    so every RVA goes through the table."""

    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ" or len(data) < 0x40:
            raise IconError("not a PE file")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise IconError("not a PE file")
        sections, optional_size = struct.unpack_from("<2xH12xH", data, pe + 4)  # COFF header
        optional = pe + 24
        directories = optional + {0x10B: 96, 0x20B: 112}.get(struct.unpack_from("<H", data, optional)[0], 0)
        if directories == optional:
            raise IconError("unknown PE optional header")
        self.rsrc_rva = struct.unpack_from("<I", data, directories + 2 * 8)[0]  # data directory 2
        self.sections = []  # (virtual address, size, file offset)
        for i in range(sections):
            _name, virtual_size, rva, raw_size, raw = struct.unpack_from("<8sIIII", data, optional + optional_size + 40 * i)
            self.sections.append((rva, max(virtual_size, raw_size), raw))
        if not self.rsrc_rva:
            raise IconError("the executable has no resources")
        self.root = self.offset(self.rsrc_rva)

    def offset(self, rva):
        for start, size, raw in self.sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise IconError(f"RVA {rva:#x} is outside every section")

    def entries(self, directory):
        """(name or id, is a subdirectory, offset) of each entry of a resource directory; `directory`
        is an offset from the start of the resource section."""
        base = self.root + directory
        named, ids = struct.unpack_from("<12xHH", self.data, base)
        for i in range(named + ids):
            name, target = struct.unpack_from("<II", self.data, base + 16 + 8 * i)
            if name & 0x80000000:
                at = self.root + (name & 0x7FFFFFFF)
                name = self.data[at + 2:at + 2 + 2 * struct.unpack_from("<H", self.data, at)[0]].decode("utf-16le")
            yield name, bool(target & 0x80000000), target & 0x7FFFFFFF

    def first_resource(self, directory):
        """The data of the first leaf below a directory (the first language of a resource)."""
        for _name, is_dir, target in self.entries(directory):
            if is_dir:
                return self.first_resource(target)
            rva, size = struct.unpack_from("<II", self.data, self.root + target)
            at = self.offset(rva)
            return self.data[at:at + size]
        raise IconError("empty resource directory")

    def resources(self, kind):
        """[(name or id, data)] for every resource of a type, in directory order."""
        for name, is_dir, target in self.entries(0):
            if name == kind and is_dir:
                return [(n, self.first_resource(t)) for n, d, t in self.entries(target) if d]
        return []


def read_pe_icons(data):
    """The images of the first icon group of an executable (the one Explorer shows), as a list of Image."""
    try:
        return _read_pe_icons(data)
    except (struct.error, IndexError, UnicodeDecodeError):
        raise IconError("damaged resource directory") from None


def _read_pe_icons(data):
    pe = PeResources(data)
    groups = pe.resources(RT_GROUP_ICON)
    if not groups:
        raise IconError("the executable has no RT_GROUP_ICON resource")
    group = groups[0][1]
    reserved, kind, count = struct.unpack_from("<HHH", group, 0)
    if (reserved, kind) != (0, 1):
        raise IconError("bad icon group")
    wanted = {struct.unpack_from("<H", group, 6 + 14 * i + 12)[0] for i in range(count)}
    blobs = [blob for ident, blob in pe.resources(RT_ICON) if ident in wanted]
    return _decode_all(blobs)


# --- Resampling -----------------------------------------------------------------

def _lanczos(x):
    if x == 0:
        return 1.0
    if abs(x) >= LANCZOS_LOBES:
        return 0.0
    px = math.pi * x
    return LANCZOS_LOBES * math.sin(px) * math.sin(px / LANCZOS_LOBES) / (px * px)


def _weights(source, target):
    """For each target pixel: (first source pixel, normalised weights). The filter widens when
    shrinking, so every source pixel counts."""
    scale = source / target
    width = max(scale, 1.0)
    out = []
    for i in range(target):
        center = (i + 0.5) * scale
        first = max(int(center - LANCZOS_LOBES * width + 0.5), 0)
        last = min(int(center + LANCZOS_LOBES * width + 0.5), source)
        weights = [_lanczos((j + 0.5 - center) / width) for j in range(first, last)]
        total = sum(weights)
        out.append((first, [w / total for w in weights]))
    return out


def resize(image, size):
    """The image scaled to size x size with a Lanczos filter. Colour is weighted by alpha, so
    transparent pixels do not bleed their colour into the edges."""
    w, h = image.width, image.height
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = image.rgba[(y * w + x) * 4:(y * w + x) * 4 + 4]
            row += (r * a / 255, g * a / 255, b * a / 255, float(a))
        rows.append(row)

    horizontal = []
    for row in rows:
        out = []
        for first, weights in _weights(w, size):
            for c in range(4):
                out.append(sum(wt * row[(first + k) * 4 + c] for k, wt in enumerate(weights)))
        horizontal.append(out)

    out = bytearray()
    for first, weights in _weights(h, size):
        acc = [0.0] * (size * 4)
        for wt, row in zip(weights, horizontal[first:first + len(weights)]):
            acc = [s + wt * v for s, v in zip(acc, row)]
        for i in range(0, size * 4, 4):
            a = min(max(acc[i + 3], 0.0), 255.0)
            if a < 0.5:
                out += b"\0\0\0\0"
                continue
            scale = 255 / a
            out += bytes((round(min(max(acc[i + c], 0.0), a) * scale) for c in range(3))) + bytes((round(a),))
    return Image(size, size, bytes(out), 32)


# --- .iconset and .icns ---------------------------------------------------------

def pick_source(images):
    """The biggest image, preferring more colour depth at equal size."""
    return max(images, key=lambda im: (im.width * im.height, im.bit_count))


def square_image(images, size):
    """An image of exactly size x size: one the icon holds, or the biggest resampled."""
    exact = [im for im in images if im.width == im.height == size]
    if exact:
        return max(exact, key=lambda im: im.bit_count)
    source = pick_source(images)
    if source.width != source.height:
        raise IconError(f"the biggest icon image is not square ({source.width}x{source.height})")
    return resize(source, size)


def write_iconset(images, folder):
    folder = pathlib.Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    by_size = {}
    for name, size in ICONSET:
        if size not in by_size:
            by_size[size] = textures.encode_png(size, size, square_image(images, size).rgba)
        (folder / name).write_bytes(by_size[size])


def make_icns(images, out):
    """Writes the .icns with iconutil (macOS only)."""
    if platform.system() != "Darwin" or not shutil.which("iconutil"):
        raise IconError("iconutil is needed to build an .icns, and it only exists on macOS")
    out = pathlib.Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        iconset = pathlib.Path(tmp) / "AppIcon.iconset"  # iconutil insists on the extension
        write_iconset(images, iconset)
        done = subprocess.run(["iconutil", "-c", "icns", "-o", str(out), str(iconset)],
                              capture_output=True, text=True)
    if done.returncode != 0:
        raise IconError(f"iconutil failed: {done.stderr.strip() or done.stdout.strip()}")


def build_app_icon(game, out):
    """Reads the icon from <game>/MagicBall3.exe and writes the .icns; returns the images' sizes."""
    exe = pathlib.Path(game) / "MagicBall3.exe"
    if not exe.is_file():
        raise IconError(f"missing {exe}")
    images = read_pe_icons(exe.read_bytes())
    make_icns(images, out)
    return sorted(im.width for im in images)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--game", type=pathlib.Path)
    source.add_argument("--ico", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("assets/AppIcon.icns"))
    ap.add_argument("--iconset", type=pathlib.Path,
                    help="also keep the .iconset folder here (the PNGs iconutil reads)")
    args = ap.parse_args()
    try:
        if args.ico:
            images = read_ico(args.ico.read_bytes())
            where = args.ico
        else:
            where = args.game / "MagicBall3.exe"
            images = read_pe_icons(where.read_bytes())
        if args.iconset:
            write_iconset(images, args.iconset)
        make_icns(images, args.out)
    except (OSError, IconError, struct.error) as e:
        sys.exit(f"make_icon: {e}")
    sizes = ", ".join(f"{im.width}x{im.height}" for im in sorted(images, key=lambda im: im.width))
    print(f"icon: {where.name} holds {sizes}; wrote {args.out}")


if __name__ == "__main__":
    main()
