#!/usr/bin/env python3
"""Convert Magic Ball 3's DirectX .x meshes to binary glTF 2.0 (.glb).

Usage:
    python3 tools/x2glb.py --skinmeshes assets/Data/SkinMeshes \
        --textures assets/Textures --out assets/Data/Meshes

The output layout is specified in docs/ASSET_FORMATS.md ("Meshes"). Coordinates
are not converted: the files stay in Direct3D left-handed space with clockwise
front faces. Reads the .x files with tools/xfile.py.
"""
import argparse
import json
import math
import os
import pathlib
import struct
import sys
import time
from urllib.parse import quote, unquote

import xfile

GLB_MAGIC = 0x46546C67  # "glTF"
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942
FLOAT, UNSIGNED_INT = 5126, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
TRIANGLES = 4
DEFAULT_TICKS_PER_SECOND = 4800  # D3DX default when a file has no AnimTicksPerSecond
IDENTITY = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
# AnimationKey type -> (track key, values per key)
KEY_TYPES = {
    xfile.KEY_ROTATION: ("rotation", 4),
    xfile.KEY_SCALE: ("scale", 3),
    xfile.KEY_POSITION: ("position", 3),
    xfile.KEY_MATRIX: ("matrix", 16),
    xfile.KEY_MATRIX_ALT: ("matrix", 16),
}
COMPONENT_SIZE = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
TYPE_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
# The runtime decodes JPEG and PNG only; every .dds has a .png beside it.
SKIPPED_TEXTURE_EXTENSIONS = {".dds"}
PREFERRED_EXTENSIONS = {".png": 0, ".jpg": 1, ".jpeg": 1}


class ConversionError(RuntimeError):
    pass


def f32(x):
    """The shortest decimal that reads back as the same float32, so the JSON stays small and readable."""
    packed = struct.pack("<f", x)
    for digits in range(1, 10):
        short = float(f"{x:.{digits}g}")
        if struct.pack("<f", short) == packed:
            return short
    return x


class TextureResolver:
    """Finds the file a .x material names, in the converted Textures folder."""

    def __init__(self, textures_dir):
        self.root = pathlib.Path(textures_dir)
        by_relative, by_name, by_stem = {}, {}, {}
        files = [p for p in self.root.rglob("*")
                 if p.is_file() and p.suffix.lower() not in SKIPPED_TEXTURE_EXTENSIONS]
        # Files in the root first, then subfolders; for stems, .png before .jpg before the rest.
        files.sort(key=lambda p: (len(p.relative_to(self.root).parts),
                                  PREFERRED_EXTENSIONS.get(p.suffix.lower(), 2),
                                  p.relative_to(self.root).as_posix().lower()))
        for p in files:
            rel = p.relative_to(self.root).as_posix().lower()
            by_relative.setdefault(rel, p)
            by_name.setdefault(p.name.lower(), p)
            by_stem.setdefault(p.stem.lower(), p)
        self.by_relative, self.by_name, self.by_stem = by_relative, by_name, by_stem

    def find(self, name):
        """Path of the texture, or None. Case-insensitive; the extension may differ."""
        key = name.replace("\\", "/").lower()
        base = key.rsplit("/", 1)[-1]
        stem = base.rsplit(".", 1)[0] if "." in base else base
        return self.by_relative.get(key) or self.by_name.get(base) or self.by_stem.get(stem)


def _pack_floats(rows):
    flat = [c for row in rows for c in row]
    return struct.pack(f"<{len(flat)}f", *flat)


def _unit(x, y, z):
    n = math.sqrt(x * x + y * y + z * z)
    return (x / n, y / n, z / n) if n > 1e-12 else None


def _smooth_normals(mesh):
    """Area-weighted vertex normals for a mesh whose .x has none. Clockwise faces, left-handed space."""
    acc = [[0.0, 0.0, 0.0] for _ in mesh.positions]
    pos = mesh.positions
    for face in mesh.faces:
        x0, y0, z0 = pos[face[0]]
        for a, b in zip(face[1:], face[2:]):
            ax, ay, az = pos[a][0] - x0, pos[a][1] - y0, pos[a][2] - z0
            bx, by, bz = pos[b][0] - x0, pos[b][1] - y0, pos[b][2] - z0
            n = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
            for i in (face[0], a, b):
                for k in range(3):
                    acc[i][k] += n[k]
    return [_unit(*v) or (0.0, 1.0, 0.0) for v in acc]


def _corner_normals(mesh):
    """(normal index, unit normal) for every corner of every polygon."""
    units = [_unit(*n) for n in mesh.normals]
    smooth = None
    rows = []
    for f, face in enumerate(mesh.faces):
        row = []
        for k, pi in enumerate(face):
            ni = mesh.face_normals[f][k] if mesh.face_normals else pi
            vec = units[ni] if mesh.face_normals else None
            if vec is None:
                smooth = smooth or _smooth_normals(mesh)
                vec = smooth[pi]
            row.append((ni, vec))
        rows.append(row)
    return rows


class _Primitive:
    """Vertices and triangles of one material. A vertex is a unique (mesh, position, normal) triple."""

    def __init__(self, material):
        self.material = material
        self.index_of = {}
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def vertex(self, key, position, normal, uv):
        i = self.index_of.get(key)
        if i is None:
            i = self.index_of[key] = len(self.positions)
            self.positions.append(position)
            self.normals.append(normal)
            self.uvs.append(uv)
        return i


class _GlbBuilder:
    def __init__(self, xf, glb_dir, resolver):
        self.xf = xf
        self.glb_dir = glb_dir
        self.resolver = resolver
        self.nodes, self.meshes = [], []
        self.accessors, self.views = [], []
        self.binary = bytearray()
        self.images, self.textures = [], []
        self.unresolved = []
        self.warnings = list(xf.warnings)
        self.triangles = 0
        self.vertices = 0

    # -- buffers --------------------------------------------------------

    def _accessor(self, payload, count, component_type, kind, target, bounds=None):
        assert len(payload) % 4 == 0 and len(self.binary) % 4 == 0
        self.views.append({"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(payload),
                           "target": target})
        self.binary += payload
        accessor = {"bufferView": len(self.views) - 1, "componentType": component_type,
                    "count": count, "type": kind}
        if bounds:
            accessor["min"], accessor["max"] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def _primitive_json(self, prim):
        n = len(prim.positions)
        lo = [f32(min(p[k] for p in prim.positions)) for k in range(3)]
        hi = [f32(max(p[k] for p in prim.positions)) for k in range(3)]
        attributes = {
            "POSITION": self._accessor(_pack_floats(prim.positions), n, FLOAT, "VEC3", ARRAY_BUFFER, (lo, hi)),
            "NORMAL": self._accessor(_pack_floats(prim.normals), n, FLOAT, "VEC3", ARRAY_BUFFER),
            "TEXCOORD_0": self._accessor(_pack_floats(prim.uvs), n, FLOAT, "VEC2", ARRAY_BUFFER),
        }
        indices = self._accessor(struct.pack(f"<{len(prim.indices)}I", *prim.indices), len(prim.indices),
                                 UNSIGNED_INT, "SCALAR", ELEMENT_ARRAY_BUFFER)
        out = {"attributes": attributes, "indices": indices, "mode": TRIANGLES}
        if prim.material >= 0:
            out["material"] = prim.material
        self.triangles += len(prim.indices) // 3
        self.vertices += n
        return out

    # -- geometry -------------------------------------------------------

    def _add_mesh(self, meshes):
        """Merge the .x Mesh objects of one frame into a glTF mesh; its index, or None if empty."""
        prims = {}
        for ordinal, mesh in enumerate(meshes):
            normals = _corner_normals(mesh)
            for f, face in enumerate(mesh.faces):
                if len(face) < 3:
                    continue
                material = mesh.face_materials[f] if mesh.face_materials else -1
                prim = prims.setdefault(material, _Primitive(material))
                corners = [prim.vertex((ordinal, pi, ni), mesh.positions[pi], normal,
                                       mesh.uvs[pi] if mesh.uvs else (0.0, 0.0))
                           for pi, (ni, normal) in zip(face, normals[f])]
                for i in range(1, len(corners) - 1):
                    prim.indices += (corners[0], corners[i], corners[i + 1])
        if not prims:
            return None
        self.meshes.append({"primitives": [self._primitive_json(prims[m]) for m in sorted(prims)]})
        return len(self.meshes) - 1

    def _add_frame(self, frame):
        index = len(self.nodes)
        node = {}
        self.nodes.append(node)
        if frame.name:
            node["name"] = frame.name
        if frame.matrix is not None and [float(v) for v in frame.matrix] != IDENTITY:
            node["matrix"] = [f32(v) for v in frame.matrix]
        mesh = self._add_mesh(frame.meshes)
        if mesh is not None:
            node["mesh"] = mesh
        children = [self._add_frame(child) for child in frame.children]
        if children:
            node["children"] = children
        return index

    def _add_root(self, root):
        if isinstance(root, xfile.XFrame):
            return self._add_frame(root)
        # A Mesh outside any frame gets a synthetic unnamed node.
        self.nodes.append({})
        mesh = self._add_mesh([root])
        if mesh is not None:
            self.nodes[-1]["mesh"] = mesh
        return len(self.nodes) - 1

    # -- materials and animations --------------------------------------

    def _texture(self, path):
        uri = quote(os.path.relpath(path, self.glb_dir).replace(os.sep, "/"), safe="/")
        image = next((i for i, im in enumerate(self.images) if im["uri"] == uri), None)
        if image is None:
            self.images.append({"uri": uri})
            self.textures.append({"source": len(self.images) - 1})
            image = len(self.images) - 1
        return image  # textures and images are 1:1, so the indices agree

    def _material(self, m):
        pbr = {"baseColorFactor": [f32(v) for v in m.face_color], "metallicFactor": 0, "roughnessFactor": 1}
        extras = {"power": f32(m.power), "specular": [f32(v) for v in m.specular],
                  "emissive": [f32(v) for v in m.emissive]}
        if m.texture:
            extras["textureFile"] = m.texture
            path = self.resolver.find(m.texture)
            if path:
                pbr["baseColorTexture"] = {"index": self._texture(path)}
            else:
                extras["textureMissing"] = True
                self.unresolved.append(m.texture)
        out = {"pbrMetallicRoughness": pbr, "extras": extras}
        if m.name:
            out["name"] = m.name
        return out

    def _animations(self):
        ticks = self.xf.ticks_per_second or DEFAULT_TICKS_PER_SECOND
        sets = []
        for aset in self.xf.animation_sets:
            tracks = []
            for anim in aset.animations:
                track = {"node": anim.frame}
                for key_set in anim.key_sets:
                    kind = KEY_TYPES.get(key_set.key_type)
                    if kind is None or any(len(v) != kind[1] for _, v in key_set.keys):
                        self.warnings.append(f"animation '{aset.name}' on '{anim.frame}': "
                                             f"unsupported key type {key_set.key_type}, skipped")
                        continue
                    track.setdefault(kind[0], []).extend([t, *map(f32, v)] for t, v in key_set.keys)
                tracks.append(track)
            sets.append({"name": aset.name, "ticksPerSecond": ticks, "tracks": tracks})
        return sets

    # -- result ---------------------------------------------------------

    def build(self, source):
        materials = [self._material(m) for m in self.xf.materials]
        roots = [self._add_root(r) for r in self.xf.roots]
        animations = self._animations()
        gltf = {"asset": {"version": "2.0", "generator": "Muggle Ball 3 tools/x2glb.py"},
                "scene": 0, "scenes": [{"nodes": roots}], "nodes": self.nodes}
        for key, value in (("meshes", self.meshes), ("materials", materials), ("textures", self.textures),
                           ("images", self.images), ("accessors", self.accessors),
                           ("bufferViews", self.views)):
            if value:
                gltf[key] = value
        if self.binary:
            gltf["buffers"] = [{"byteLength": len(self.binary)}]
        gltf["extras"] = {"source": source, "coordinateSystem": "d3d-left-handed", "animations": animations}
        stats = {"nodes": len(self.nodes), "meshes": len(self.meshes),
                 "primitives": sum(len(m["primitives"]) for m in self.meshes),
                 "triangles": self.triangles, "vertices": self.vertices, "materials": len(materials),
                 "animation_sets": len(animations), "animation_tracks": sum(len(a["tracks"]) for a in animations)}
        return gltf, bytes(self.binary), stats


def build_glb(xf, source, glb_dir, resolver):
    """(glTF dict, binary buffer, stats, unresolved texture names, warnings) for a parsed .x file."""
    b = _GlbBuilder(xf, glb_dir, resolver)
    gltf, binary, stats = b.build(source)
    return gltf, binary, stats, b.unresolved, b.warnings


def write_glb(path, gltf, binary):
    text = json.dumps(gltf, separators=(",", ":"), allow_nan=False).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(text) + (8 + len(binary) if binary else 0)
    out = bytearray(struct.pack("<III", GLB_MAGIC, 2, total))
    out += struct.pack("<II", len(text), CHUNK_JSON) + text
    if binary:
        out += struct.pack("<II", len(binary), CHUNK_BIN) + binary
    pathlib.Path(path).write_bytes(out)


# -- validation ---------------------------------------------------------

def _read_chunks(data, problems):
    if len(data) < 20:
        problems.append("file shorter than a glb header and one chunk")
        return None, b""
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        problems.append("bad magic")
    if version != 2:
        problems.append(f"glb version {version}, expected 2")
    if length != len(data):
        problems.append(f"header length {length} but file is {len(data)} bytes")
    chunks, o = [], 12
    while o < len(data):
        if o + 8 > len(data):
            problems.append("truncated chunk header")
            break
        size, kind = struct.unpack_from("<II", data, o)
        if size % 4 or o + 8 + size > len(data):
            problems.append(f"chunk {len(chunks)}: bad length {size}")
            break
        chunks.append((kind, data[o + 8:o + 8 + size]))
        o += 8 + size
    if not chunks or chunks[0][0] != CHUNK_JSON:
        problems.append("first chunk is not JSON")
        return None, b""
    if len(chunks) > 2 or (len(chunks) == 2 and chunks[1][0] != CHUNK_BIN):
        problems.append("expected exactly a JSON chunk and an optional BIN chunk")
    try:
        doc = json.loads(chunks[0][1])
    except ValueError as e:
        problems.append(f"JSON chunk does not parse: {e}")
        return None, b""
    return doc, chunks[1][1] if len(chunks) > 1 else b""


def _check_accessors(doc, binary, problems):
    """Buffer, bufferView and accessor range and alignment checks."""
    buffers = doc.get("buffers", [])
    views = doc.get("bufferViews", [])
    if len(buffers) > 1 or (buffers and buffers[0]["byteLength"] > len(binary)):
        problems.append("buffer is larger than the BIN chunk")
    for i, v in enumerate(views):
        if v.get("buffer") != 0 or not buffers:
            problems.append(f"bufferView {i}: bad buffer")
        elif v["byteOffset"] % 4 or v["byteOffset"] + v["byteLength"] > buffers[0]["byteLength"]:
            problems.append(f"bufferView {i}: misaligned or past the end of the buffer")
    for i, a in enumerate(doc.get("accessors", [])):
        if not 0 <= a.get("bufferView", -1) < len(views):
            problems.append(f"accessor {i}: bad bufferView")
            continue
        size = COMPONENT_SIZE.get(a["componentType"])
        width = TYPE_WIDTH.get(a["type"])
        if size is None or width is None:
            problems.append(f"accessor {i}: unknown componentType or type")
            continue
        view = views[a["bufferView"]]
        offset = a.get("byteOffset", 0)
        if a["count"] < 1 or offset % size or offset + a["count"] * size * width > view["byteLength"]:
            problems.append(f"accessor {i}: out of range of its bufferView")


def _read_accessor(doc, binary, index):
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    code = {5126: "f", 5125: "I"}[a["componentType"]]
    n = a["count"] * TYPE_WIDTH[a["type"]]
    return struct.unpack_from(f"<{n}{code}", binary, view["byteOffset"] + a.get("byteOffset", 0))


def _check_primitive(doc, binary, where, prim, problems):
    attrs = prim.get("attributes", {})
    if prim.get("mode", TRIANGLES) != TRIANGLES:
        problems.append(f"{where}: not a triangle list")
    for name, kind in (("POSITION", "VEC3"), ("NORMAL", "VEC3"), ("TEXCOORD_0", "VEC2")):
        if name not in attrs:
            problems.append(f"{where}: missing {name}")
            return
        a = doc["accessors"][attrs[name]]
        if a["type"] != kind or a["componentType"] != FLOAT:
            problems.append(f"{where}: {name} is {a['type']}/{a['componentType']}")
            return
    count = doc["accessors"][attrs["POSITION"]]["count"]
    if any(doc["accessors"][attrs[n]]["count"] != count for n in attrs):
        problems.append(f"{where}: attribute counts differ")
        return
    pos = _read_accessor(doc, binary, attrs["POSITION"])
    acc = doc["accessors"][attrs["POSITION"]]
    for k in range(3):
        column = pos[k::3]
        for bound, expected in (("min", min(column)), ("max", max(column))):
            if bound not in acc or struct.pack("<f", acc[bound][k]) != struct.pack("<f", expected):
                problems.append(f"{where}: POSITION {bound}[{k}] is wrong")
    nrm = _read_accessor(doc, binary, attrs["NORMAL"])
    for v in range(count):
        x, y, z = nrm[3 * v:3 * v + 3]
        if abs(math.sqrt(x * x + y * y + z * z) - 1) > 1e-3:
            problems.append(f"{where}: normal {v} is not unit length")
            break
    if "indices" not in prim:
        problems.append(f"{where}: no indices")
    else:
        ia = doc["accessors"][prim["indices"]]
        if ia["type"] != "SCALAR" or ia["componentType"] != UNSIGNED_INT or ia["count"] % 3:
            problems.append(f"{where}: indices are not a UNSIGNED_INT triangle list")
        elif max(_read_accessor(doc, binary, prim["indices"])) >= count:
            problems.append(f"{where}: an index is past the vertex count")
    for key in ("POSITION", "NORMAL", "TEXCOORD_0"):
        if doc["bufferViews"][doc["accessors"][attrs[key]]["bufferView"]].get("target") != ARRAY_BUFFER:
            problems.append(f"{where}: {key} view lacks the ARRAY_BUFFER target")
    if "indices" in prim and doc["bufferViews"][doc["accessors"][prim["indices"]]["bufferView"]].get(
            "target") != ELEMENT_ARRAY_BUFFER:
        problems.append(f"{where}: index view lacks the ELEMENT_ARRAY_BUFFER target")
    if "material" in prim and not 0 <= prim["material"] < len(doc.get("materials", [])):
        problems.append(f"{where}: material {prim['material']} does not exist")


def _check_nodes(doc, expected_nodes, problems):
    nodes = doc.get("nodes", [])
    if expected_nodes is not None and len(nodes) != expected_nodes:
        problems.append(f"{len(nodes)} nodes, source has {expected_nodes} frames")
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            if not 0 <= c < len(nodes) or c in parent:
                problems.append(f"node {i}: bad or repeated child {c}")
            else:
                parent[c] = i
        if "mesh" in n and not 0 <= n["mesh"] < len(doc.get("meshes", [])):
            problems.append(f"node {i}: mesh {n['mesh']} does not exist")
        m = n.get("matrix")
        if m is not None and (len(m) != 16 or not all(math.isfinite(v) for v in m)):
            problems.append(f"node {i}: bad matrix")
    scene_roots = doc.get("scenes", [{}])[doc.get("scene", 0)].get("nodes", [])
    if sorted(scene_roots) != [i for i in range(len(nodes)) if i not in parent]:
        problems.append("scene roots are not exactly the nodes without a parent")
    reached, stack = set(), list(scene_roots)
    while stack:
        i = stack.pop()
        if i in reached:
            problems.append(f"node {i}: reached twice (cycle)")
            return
        reached.add(i)
        stack.extend(nodes[i].get("children", []))
    if len(reached) != len(nodes):
        problems.append("some nodes are not reachable from the scene")


def _check_materials(doc, glb_dir, problems):
    textures, images = doc.get("textures", []), doc.get("images", [])
    for i, m in enumerate(doc.get("materials", [])):
        tex = m.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if tex is None:
            continue
        if not 0 <= tex["index"] < len(textures) or not 0 <= textures[tex["index"]]["source"] < len(images):
            problems.append(f"material {i}: bad texture reference")
            continue
        uri = images[textures[tex["index"]]["source"]]["uri"]
        if not (glb_dir / unquote(uri)).is_file():
            problems.append(f"material {i}: texture file {uri} does not exist")


def validate_glb(path, expected_nodes=None):
    """Re-read a .glb and check it. Returns a list of problems; empty means valid.

    expected_nodes: the number of frames in the source .x, when known.
    """
    path = pathlib.Path(path)
    problems = []
    doc, binary = _read_chunks(path.read_bytes(), problems)
    if doc is None:
        return problems
    if doc.get("asset", {}).get("version") != "2.0":
        problems.append("asset.version is not 2.0")
    if problems:
        return problems
    try:
        _check_accessors(doc, binary, problems)
        if problems:
            return problems
        for mi, mesh in enumerate(doc.get("meshes", [])):
            for pi, prim in enumerate(mesh.get("primitives", [])):
                _check_primitive(doc, binary, f"mesh {mi} primitive {pi}", prim, problems)
        _check_nodes(doc, expected_nodes, problems)
        _check_materials(doc, path.parent, problems)
        names = {n.get("name") for n in doc.get("nodes", [])}
        for aset in doc.get("extras", {}).get("animations", []):
            for track in aset["tracks"]:
                if track["node"] not in names:
                    problems.append(f"animation '{aset['name']}' targets unknown frame '{track['node']}'")
    except (KeyError, IndexError, TypeError, ValueError) as e:
        problems.append(f"malformed glTF: {type(e).__name__} {e}")
    return problems


# -- driver -------------------------------------------------------------

def convert_file(x_path, source, glb_path, resolver):
    """Convert one .x file; returns its stats. Raises ConversionError if the result does not validate."""
    xf = xfile.parse_file(x_path)
    gltf, binary, stats, unresolved, warnings = build_glb(xf, source, pathlib.Path(glb_path).parent, resolver)
    pathlib.Path(glb_path).parent.mkdir(parents=True, exist_ok=True)
    write_glb(glb_path, gltf, binary)
    expected = sum(1 for _ in xf.frames()) + sum(isinstance(r, xfile.XMesh) for r in xf.roots)
    problems = validate_glb(glb_path, expected)
    if problems:
        raise ConversionError("; ".join(problems[:5]))
    stats.update(unresolved=unresolved, warnings=warnings, unknown_templates=xf.unknown_templates)
    return stats


def convert_all(skinmeshes_dir, textures_dir, out_dir):
    """Convert every .x under skinmeshes_dir to out_dir/<lowercased relative path>.glb.

    Returns totals (files, nodes, meshes, primitives, triangles, vertices, materials, animation_sets,
    animation_tracks), the unresolved texture names with the files that use them, the unknown
    templates seen, warnings and the elapsed seconds. Raises ConversionError after the whole run if
    any file failed to convert or validate.
    """
    start = time.perf_counter()
    skinmeshes_dir, out_dir = pathlib.Path(skinmeshes_dir), pathlib.Path(out_dir)
    resolver = TextureResolver(textures_dir)
    totals = dict.fromkeys(("files", "nodes", "meshes", "primitives", "triangles", "vertices", "materials",
                            "animation_sets", "animation_tracks"), 0)
    unresolved, unknown, warnings, failures, seen = {}, {}, [], [], {}
    sources = sorted((p for p in skinmeshes_dir.rglob("*") if p.is_file() and p.suffix.lower() == ".x"),
                     key=lambda p: p.relative_to(skinmeshes_dir).as_posix().lower())
    for x_path in sources:
        rel = x_path.relative_to(skinmeshes_dir).as_posix()
        glb_rel = rel[:-2].lower() + ".glb"
        if glb_rel in seen:
            failures.append(f"{rel}: output path {glb_rel} is also produced by {seen[glb_rel]}")
            continue
        seen[glb_rel] = rel
        try:
            stats = convert_file(x_path, f"SkinMeshes/{rel}", out_dir / glb_rel, resolver)
        except (ConversionError, ValueError, OSError) as e:  # XFileError is a ValueError
            failures.append(f"{rel}: {e}")
            continue
        totals["files"] += 1
        for key in totals:
            totals[key] += stats.get(key, 0)
        for name in stats["unresolved"]:
            unresolved.setdefault(name, []).append(rel)
        for name, n in stats["unknown_templates"].items():
            unknown[name] = unknown.get(name, 0) + n
        warnings += [f"{rel}: {w}" for w in stats["warnings"]]
    if failures:
        raise ConversionError(f"{len(failures)} of {len(sources)} meshes failed:\n  " + "\n  ".join(failures))
    return {**totals, "unresolved_textures": unresolved, "unknown_templates": unknown, "warnings": warnings,
            "seconds": time.perf_counter() - start}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skinmeshes", required=True, type=pathlib.Path)
    ap.add_argument("--textures", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()
    try:
        stats = convert_all(args.skinmeshes, args.textures, args.out)
    except ConversionError as e:
        sys.exit(str(e))
    print(f"meshes: {stats['files']} files -> {args.out} in {stats['seconds']:.1f}s")
    print("  " + ", ".join(f"{k} {stats[k]}" for k in ("nodes", "meshes", "primitives", "triangles", "vertices",
                                                       "materials", "animation_sets", "animation_tracks")))
    for name, files in sorted(stats["unresolved_textures"].items()):
        print(f"  texture not found: {name} (used by {', '.join(files)})")
    for name, n in sorted(stats["unknown_templates"].items()):
        print(f"  unknown template: {name} x{n}")
    for w in stats["warnings"]:
        print(f"  warning: {w}")


if __name__ == "__main__":
    main()
