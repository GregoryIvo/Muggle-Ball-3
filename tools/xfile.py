#!/usr/bin/env python3
"""Parse binary DirectX .x files ("xof 0303bin 0032") into a plain Python model.

Only the templates Magic Ball 3 uses are interpreted (Frame, Mesh and its
children, Material, AnimationSet and friends). Everything else is read as a
generic object and skipped with a warning, so an unexpected template never
stops a conversion.

Binary .x files are a stream of tokens, each starting with a little-endian u16
id. Numbers belonging to one data object may be split over any number of
INTEGER_LIST / FLOAT_LIST tokens, so an object's numbers are collected into one
flat list and read back by the template's layout.

Run it on files to print a one-line summary of each: python3 tools/xfile.py a.X b.X
"""
import struct
from dataclasses import dataclass, field

TOKEN_NAME = 1
TOKEN_STRING = 2
TOKEN_INTEGER = 3
TOKEN_GUID = 5
TOKEN_INTEGER_LIST = 6
TOKEN_FLOAT_LIST = 7
TOKEN_OBRACE = 0x0A
TOKEN_CBRACE = 0x0B
TOKEN_COMMA = 0x13
TOKEN_SEMICOLON = 0x14
TOKEN_TEMPLATE = 0x1F
# Tokens that carry no payload: braces, brackets, parentheses, angles, '.', ',', ';',
# 'template', the type keywords (WORD ... cstring) and 'array'.
PAYLOADLESS = frozenset(range(0x0A, 0x15)) | {0x1F} | frozenset(range(0x28, 0x35))

KEY_ROTATION, KEY_SCALE, KEY_POSITION, KEY_MATRIX, KEY_MATRIX_ALT = 0, 1, 2, 3, 4

KNOWN_TEMPLATES = frozenset({
    "Header", "Frame", "FrameTransformMatrix", "Mesh", "MeshNormals", "MeshTextureCoords",
    "MeshMaterialList", "MeshVertexColors", "Material", "TextureFilename", "AnimationSet",
    "Animation", "AnimationKey", "AnimationOptions", "AnimTicksPerSecond",
})


class XFileError(ValueError):
    pass


@dataclass
class XMaterial:
    name: str
    face_color: tuple  # r, g, b, a
    power: float
    specular: tuple  # r, g, b
    emissive: tuple  # r, g, b
    texture: str = ""  # TextureFilename as written in the file, "" if none


@dataclass
class XMesh:
    name: str
    positions: list  # (x, y, z) per vertex
    faces: list  # tuple of position indices per polygon
    normals: list = field(default_factory=list)  # (x, y, z); empty if the mesh has none
    face_normals: list = field(default_factory=list)  # tuple of normal indices per polygon
    uvs: list = field(default_factory=list)  # (u, v) per vertex; empty if none
    # Index into XFile.materials per polygon, -1 for none; empty if the mesh has no material list.
    face_materials: list = field(default_factory=list)
    vertex_colors: list = field(default_factory=list)  # (vertex, r, g, b, a)


@dataclass
class XFrame:
    name: str  # "" if unnamed
    matrix: list = None  # FrameTransformMatrix, 16 floats in file order; None if absent
    meshes: list = field(default_factory=list)  # XMesh directly inside this frame
    children: list = field(default_factory=list)  # XFrame


@dataclass
class XAnimationKey:
    key_type: int  # KEY_ROTATION ... KEY_MATRIX_ALT
    keys: list  # (tick, values tuple)


@dataclass
class XAnimation:
    frame: str  # name of the animated frame
    key_sets: list = field(default_factory=list)  # XAnimationKey


@dataclass
class XAnimationSet:
    name: str
    animations: list = field(default_factory=list)  # XAnimation


@dataclass
class XFile:
    roots: list = field(default_factory=list)  # top-level XFrame and XMesh, in file order
    materials: list = field(default_factory=list)  # every Material object, in file order
    animation_sets: list = field(default_factory=list)
    ticks_per_second: int = None  # AnimTicksPerSecond, None if the file has none
    unknown_templates: dict = field(default_factory=dict)  # template name -> object count
    warnings: list = field(default_factory=list)

    def frames(self):
        """Every XFrame, depth first in file order (the order glTF nodes are written in)."""
        stack = [r for r in reversed(self.roots) if isinstance(r, XFrame)]
        while stack:
            frame = stack.pop()
            yield frame
            stack.extend(reversed(frame.children))


class _Node:
    """A raw object from the token stream: its numbers, strings and nested objects."""

    __slots__ = ("template", "name", "values", "children", "is_reference")

    def __init__(self, template, name, is_reference=False):
        self.template = template
        self.name = name
        self.values = []
        self.children = []
        self.is_reference = is_reference


class _Tokens:
    def __init__(self, data, float_size):
        self.d = data
        self.o = 16
        self.float_size = float_size
        self.pending = None

    def _take(self, n):
        if self.o + n > len(self.d):
            raise XFileError(f"truncated token at {self.o:#x}")
        b = self.d[self.o:self.o + n]
        self.o += n
        return b

    def _u32(self):
        return struct.unpack("<I", self._take(4))[0]

    def at_end(self):
        return self.pending is None and self.o >= len(self.d)

    def peek(self):
        if self.pending is None:
            self.pending = self._read()
        return self.pending

    def next(self):
        tok = self.peek()
        self.pending = None
        return tok

    def _read(self):
        start = self.o
        tid = struct.unpack("<H", self._take(2))[0]
        if tid in PAYLOADLESS:
            return tid, None
        if tid == TOKEN_NAME:
            return tid, self._take(self._u32()).decode("latin-1")
        if tid == TOKEN_STRING:
            text = self._take(self._u32()).decode("latin-1")
            term = struct.unpack("<H", self._take(2))[0]
            if term not in (TOKEN_COMMA, TOKEN_SEMICOLON):
                raise XFileError(f"string at {start:#x} not terminated (token {term:#x})")
            return tid, text
        if tid == TOKEN_INTEGER:
            return tid, [self._u32()]
        if tid == TOKEN_GUID:
            self._take(16)
            return tid, None
        if tid == TOKEN_INTEGER_LIST:
            n = self._u32()
            return tid, list(struct.unpack(f"<{n}I", self._take(4 * n)))
        if tid == TOKEN_FLOAT_LIST:
            n = self._u32()
            code = "f" if self.float_size == 4 else "d"
            return tid, list(struct.unpack(f"<{n}{code}", self._take(self.float_size * n)))
        raise XFileError(f"unknown token {tid:#x} at {start:#x}")


class _Cursor:
    """Reads an object's flat value list by template layout."""

    def __init__(self, node):
        self.node = node
        self.v = node.values
        self.i = 0

    def take(self, n):
        if self.i + n > len(self.v):
            raise XFileError(f"{self.node.template} '{self.node.name}': needs {n} values at {self.i}, "
                             f"only {len(self.v) - self.i} left")
        out = self.v[self.i:self.i + n]
        self.i += n
        return out

    def count(self, limit):
        n = self.take(1)[0]
        if not isinstance(n, int) or n > limit:
            raise XFileError(f"{self.node.template} '{self.node.name}': implausible count {n}")
        return n

    def vectors(self, n, width):
        flat = self.take(n * width)
        return [tuple(flat[k:k + width]) for k in range(0, n * width, width)]

    def faces(self, n):
        out = []
        for _ in range(n):
            out.append(tuple(self.take(self.count(1 << 16))))
        return out


# No real mesh is anywhere near this large; it stops a corrupt count from allocating gigabytes.
MAX_ELEMENTS = 1 << 24


def _parse_object(tokens, template):
    name = ""
    tid, val = tokens.next()
    if tid == TOKEN_NAME:
        name = val
        tid, val = tokens.next()
    if tid == TOKEN_GUID:
        tid, val = tokens.next()
    if tid != TOKEN_OBRACE:
        raise XFileError(f"{template}: expected '{{' before offset {tokens.o:#x}, got token {tid:#x}")
    node = _Node(template, name)
    while True:
        tid, val = tokens.next()
        if tid == TOKEN_CBRACE:
            return node
        if tid in (TOKEN_INTEGER, TOKEN_INTEGER_LIST, TOKEN_FLOAT_LIST):
            node.values.extend(val)
        elif tid == TOKEN_STRING:
            node.values.append(val)
        elif tid == TOKEN_NAME:
            node.children.append(_parse_object(tokens, val))
        elif tid == TOKEN_OBRACE:
            node.children.append(_parse_reference(tokens))
        elif tid in (TOKEN_COMMA, TOKEN_SEMICOLON):
            continue
        else:
            raise XFileError(f"{template} '{name}': unexpected token {tid:#x} at {tokens.o:#x}")


def _parse_reference(tokens):
    tid, name = tokens.next()
    if tid != TOKEN_NAME:
        raise XFileError(f"reference without a name at {tokens.o:#x}")
    tid, _ = tokens.next()
    if tid == TOKEN_GUID:
        tid, _ = tokens.next()
    if tid != TOKEN_CBRACE:
        raise XFileError(f"reference '{name}' not closed at {tokens.o:#x}")
    return _Node("", name, is_reference=True)


def _skip_template(tokens):
    tid, _ = tokens.next()
    if tid != TOKEN_NAME:
        raise XFileError(f"template without a name at {tokens.o:#x}")
    tid, _ = tokens.next()
    if tid != TOKEN_OBRACE:
        raise XFileError(f"template body expected at {tokens.o:#x}")
    while tokens.next()[0] != TOKEN_CBRACE:
        pass


def _read_tree(data):
    if len(data) < 16 or data[:4] != b"xof ":
        raise XFileError("not a DirectX .x file")
    fmt = data[8:12]
    if fmt != b"bin ":
        raise XFileError(f"only binary .x files are supported, this one is '{fmt.decode('latin-1')}'")
    float_bits = data[12:16]
    if float_bits not in (b"0032", b"0064"):
        raise XFileError(f"unknown float size '{float_bits.decode('latin-1')}'")
    tokens = _Tokens(data, 4 if float_bits == b"0032" else 8)
    top = []
    while not tokens.at_end():
        tid, val = tokens.next()
        if tid == TOKEN_TEMPLATE:
            _skip_template(tokens)
        elif tid == TOKEN_NAME:
            top.append(_parse_object(tokens, val))
        else:
            raise XFileError(f"unexpected top-level token {tid:#x} at {tokens.o:#x}")
    return top


class _Builder:
    def __init__(self, top):
        self.top = top
        self.out = XFile()
        self.material_ids = {}  # id(_Node) -> index in out.materials
        self.material_names = {}  # name -> index of the first material with it
        self._collect_materials(top)

    def warn(self, text):
        self.out.warnings.append(text)

    def _collect_materials(self, nodes):
        for node in nodes:
            if node.is_reference:
                continue
            if node.template == "Material":
                self.material_ids[id(node)] = len(self.out.materials)
                self.material_names.setdefault(node.name, len(self.out.materials))
                self.out.materials.append(self._material(node))
            self._collect_materials(node.children)

    def _material(self, node):
        c = _Cursor(node)
        face = tuple(c.take(4))
        power = c.take(1)[0]
        specular = tuple(c.take(3))
        emissive = tuple(c.take(3))
        texture = ""
        for child in node.children:
            if child.template == "TextureFilename" and not child.is_reference:
                strings = [v for v in child.values if isinstance(v, str)]
                texture = strings[0] if strings else ""
        return XMaterial(node.name, face, power, specular, emissive, texture)

    def build(self):
        for node in self.top:
            self._top_level(node)
        return self.out

    def _count_unknown(self, node):
        if node.is_reference or node.template in KNOWN_TEMPLATES:
            return
        self.out.unknown_templates[node.template] = self.out.unknown_templates.get(node.template, 0) + 1
        if self.out.unknown_templates[node.template] == 1:
            self.warn(f"skipping unknown template '{node.template}'")

    def _top_level(self, node):
        t = node.template
        if t == "Frame":
            self.out.roots.append(self._frame(node))
        elif t == "Mesh":
            self.out.roots.append(self._mesh(node))
        elif t == "AnimationSet":
            self.out.animation_sets.append(self._animation_set(node))
        elif t == "AnimTicksPerSecond":
            self.out.ticks_per_second = _Cursor(node).take(1)[0]
        elif t not in ("Material", "Header"):
            self._count_unknown(node)

    def _frame(self, node):
        frame = XFrame(node.name)
        for child in node.children:
            if child.is_reference:
                continue
            if child.template == "FrameTransformMatrix":
                frame.matrix = list(_Cursor(child).take(16))
            elif child.template == "Frame":
                frame.children.append(self._frame(child))
            elif child.template == "Mesh":
                frame.meshes.append(self._mesh(child))
            else:
                self._count_unknown(child)
        return frame

    def _mesh(self, node):
        c = _Cursor(node)
        positions = c.vectors(c.count(MAX_ELEMENTS), 3)
        faces = c.faces(c.count(MAX_ELEMENTS))
        for f, face in enumerate(faces):
            if any(i >= len(positions) for i in face):
                raise XFileError(f"Mesh '{node.name}': face {f} indexes past {len(positions)} vertices")
        mesh = XMesh(node.name, positions, faces)
        for child in node.children:
            if child.is_reference:
                continue
            if child.template == "MeshNormals":
                self._normals(mesh, child)
            elif child.template == "MeshTextureCoords":
                cc = _Cursor(child)
                mesh.uvs = cc.vectors(cc.count(MAX_ELEMENTS), 2)
                if len(mesh.uvs) != len(positions):
                    self.warn(f"Mesh '{node.name}': {len(mesh.uvs)} UVs for {len(positions)} vertices, ignoring")
                    mesh.uvs = []
            elif child.template == "MeshMaterialList":
                self._material_list(mesh, child)
            elif child.template == "MeshVertexColors":
                cc = _Cursor(child)
                for _ in range(cc.count(MAX_ELEMENTS)):
                    index, *rgba = cc.take(5)
                    mesh.vertex_colors.append((index, *rgba))
            else:
                self._count_unknown(child)
        return mesh

    def _normals(self, mesh, node):
        c = _Cursor(node)
        normals = c.vectors(c.count(MAX_ELEMENTS), 3)
        face_normals = c.faces(c.count(MAX_ELEMENTS))
        ok = (len(face_normals) == len(mesh.faces)
              and all(len(fn) == len(f) and all(i < len(normals) for i in fn)
                      for fn, f in zip(face_normals, mesh.faces)))
        if ok:
            mesh.normals, mesh.face_normals = normals, face_normals
        else:
            self.warn(f"Mesh '{mesh.name}': MeshNormals do not match the faces, normals will be computed")

    def _material_list(self, mesh, node):
        c = _Cursor(node)
        n_materials = c.count(MAX_ELEMENTS)
        n_indexes = c.count(MAX_ELEMENTS)
        indexes = list(c.take(n_indexes))
        slots = []
        for child in node.children:
            if child.is_reference:
                found = self.material_names.get(child.name)
                if found is None:
                    self.warn(f"Mesh '{mesh.name}': unknown material '{child.name}'")
                slots.append(-1 if found is None else found)
            elif child.template == "Material":
                slots.append(self.material_ids[id(child)])
            else:
                self._count_unknown(child)
        if len(slots) != n_materials:
            self.warn(f"Mesh '{mesh.name}': material list declares {n_materials} materials, has {len(slots)}")
        if not indexes:
            return
        if n_indexes != len(mesh.faces):
            # D3DX repeats the last index (typically a single 0) over the remaining faces.
            self.warn(f"Mesh '{mesh.name}': {n_indexes} material indexes for {len(mesh.faces)} faces")
            indexes = (indexes + indexes[-1:] * len(mesh.faces))[:len(mesh.faces)]
        face_materials = []
        for i in indexes:
            if i >= len(slots):
                self.warn(f"Mesh '{mesh.name}': material index {i} out of range")
                face_materials.append(-1)
            else:
                face_materials.append(slots[i])
        mesh.face_materials = face_materials

    def _animation_set(self, node):
        result = XAnimationSet(node.name)
        for child in node.children:
            if child.is_reference:
                continue
            if child.template == "Animation":
                anim = self._animation(child)
                if anim:
                    result.animations.append(anim)
            else:
                self._count_unknown(child)
        return result

    def _animation(self, node):
        frame = next((c.name for c in node.children if c.is_reference), None)
        if frame is None:
            self.warn(f"Animation '{node.name}' has no frame reference, skipping")
            return None
        anim = XAnimation(frame)
        for child in node.children:
            if child.is_reference:
                continue
            if child.template == "AnimationKey":
                c = _Cursor(child)
                key_type = c.take(1)[0]
                keys = []
                for _ in range(c.count(MAX_ELEMENTS)):
                    tick = c.take(1)[0]
                    keys.append((tick, tuple(c.take(c.count(16)))))
                anim.key_sets.append(XAnimationKey(key_type, keys))
            elif child.template != "AnimationOptions":
                self._count_unknown(child)
        return anim


def parse(data):
    """Parse the bytes of a binary .x file into an XFile. Raises XFileError if malformed."""
    return _Builder(_read_tree(data)).build()


def parse_file(path):
    with open(path, "rb") as f:
        return parse(f.read())


if __name__ == "__main__":
    import sys

    for arg in sys.argv[1:]:
        xf = parse_file(arg)
        print(f"{arg}: {sum(1 for _ in xf.frames())} frames, {len(xf.materials)} materials, "
              f"{len(xf.animation_sets)} animation sets, unknown {xf.unknown_templates}")
        for w in xf.warnings:
            print(f"  warning: {w}")
