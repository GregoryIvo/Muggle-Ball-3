#!/usr/bin/env python3
"""Build small binary .x files in memory and run them through tools/xfile.py and tools/x2glb.py."""
import json
import math
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import x2glb  # noqa: E402
import xfile  # noqa: E402

OBRACE, CBRACE, SEMICOLON, COMMA = 0x0A, 0x0B, 0x14, 0x13
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


# -- a tiny token writer ------------------------------------------------

def tok(token_id):
    return struct.pack("<H", token_id)


def name(text):
    return struct.pack("<HI", xfile.TOKEN_NAME, len(text)) + text.encode()


def string(text, terminator=SEMICOLON):
    return struct.pack("<HI", xfile.TOKEN_STRING, len(text)) + text.encode() + tok(terminator)


def ints(*values):
    return struct.pack(f"<HI{len(values)}I", xfile.TOKEN_INTEGER_LIST, len(values), *values)


def floats(*values):
    return struct.pack(f"<HI{len(values)}f", xfile.TOKEN_FLOAT_LIST, len(values), *values)


def integer(value):
    return struct.pack("<HI", xfile.TOKEN_INTEGER, value)


def guid():
    return tok(xfile.TOKEN_GUID) + bytes(range(16))


def obj(template, object_name, *parts, with_guid=False):
    head = name(template) + (name(object_name) if object_name is not None else b"")
    return head + (guid() if with_guid else b"") + tok(OBRACE) + b"".join(parts) + tok(CBRACE)


def ref(target):
    return tok(OBRACE) + name(target) + tok(CBRACE)


def template(template_name):
    # template FVFData { <GUID> DWORD dwFVF; }
    return (tok(xfile.TOKEN_TEMPLATE) + name(template_name) + tok(OBRACE) + guid()
            + tok(0x29) + name("dwFVF") + tok(SEMICOLON) + tok(CBRACE))


def xfile_bytes(*parts, header=b"xof 0303bin 0032"):
    return header + b"".join(parts)


def mesh_body(positions, faces):
    flat = [c for p in positions for c in p]
    face_ints = [v for f in faces for v in (len(f), *f)]
    return [ints(len(positions)), floats(*flat), ints(len(faces)), ints(*face_ints)]


def mesh(positions, faces, *children):
    return obj("Mesh", "", *mesh_body(positions, faces), *children)


def normals(vectors, faces):
    flat = [c for v in vectors for c in v]
    face_ints = [v for f in faces for v in (len(f), *f)]
    return obj("MeshNormals", None, ints(len(vectors)), floats(*flat), ints(len(faces)), ints(*face_ints))


def uvs(coords):
    return obj("MeshTextureCoords", None, ints(len(coords)), floats(*[c for uv in coords for c in uv]))


def material_list(n_materials, face_indexes, *children):
    return obj("MeshMaterialList", None, ints(n_materials, len(face_indexes), *face_indexes), *children)


def material(material_name, rgba=(1, 1, 1, 1), power=2.0, specular=(0, 0, 0), emissive=(0, 0, 0), texture=None):
    kids = [obj("TextureFilename", None, string(texture))] if texture else []
    return obj("Material", material_name, floats(*rgba, power, *specular, *emissive), *kids)


def frame(frame_name, matrix, *children):
    parts = [obj("FrameTransformMatrix", None, floats(*matrix))] if matrix else []
    return obj("Frame", frame_name, *parts, *children)


# A quad wound clockwise as seen from -z (the viewer): up the left edge, across, down the right.
QUAD = [(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)]
TRIANGLE = [(0, 0, 0), (0, 1, 0), (1, 0, 0)]


class ParserTests(unittest.TestCase):
    def test_hierarchy_matrix_and_mesh(self):
        moved = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1]
        data = xfile_bytes(
            template("FVFData"),
            obj("Header", None, integer(1), ints(0, 1)),
            frame("Root", None,
                  frame("Child", moved,
                        mesh(QUAD, [(0, 1, 2, 3)],
                             normals([(0, 0, -1)], [(0, 0, 0, 0)]),
                             uvs([(0, 1), (0, 0), (1, 0), (1, 1)])))),
            frame("", IDENTITY))
        xf = xfile.parse(data)
        self.assertEqual([r.name for r in xf.roots], ["Root", ""])
        self.assertEqual([f.name for f in xf.frames()], ["Root", "Child", ""])
        child = xf.roots[0].children[0]
        self.assertEqual(child.matrix, [float(v) for v in moved])
        self.assertIsNone(xf.roots[0].matrix)
        m = child.meshes[0]
        self.assertEqual(m.positions, QUAD)
        self.assertEqual(m.faces, [(0, 1, 2, 3)])
        self.assertEqual(m.normals, [(0, 0, -1)])
        self.assertEqual(m.face_normals, [(0, 0, 0, 0)])
        self.assertEqual(m.uvs, [(0, 1), (0, 0), (1, 0), (1, 1)])
        self.assertEqual(m.face_materials, [])
        self.assertEqual(xf.warnings, [])

    def test_numbers_split_over_tokens_and_guids(self):
        flat = [c for p in QUAD for c in p]
        body = [ints(4), floats(*flat[:5]), floats(*flat[5:7]), floats(*flat[7:]), integer(1),
                ints(4), ints(0, 1), ints(2, 3)]
        xf = xfile.parse(xfile_bytes(obj("Mesh", "Quad", *body, with_guid=True)))
        self.assertEqual(xf.roots[0].positions, QUAD)
        self.assertEqual(xf.roots[0].faces, [(0, 1, 2, 3)])
        self.assertEqual(xf.roots[0].name, "Quad")

    def test_materials_references_inline_and_broadcast_index(self):
        data = xfile_bytes(
            material("Top", (1, 0.5, 0.25, 1), 3.5, (0.125, 0.25, 0.375), (0.5, 0.625, 0.75), "Wood.JPG"),
            frame("F", None,
                  mesh(TRIANGLE, [(0, 1, 2)], material_list(1, [0], ref("Top"))),
                  mesh(TRIANGLE, [(0, 1, 2)] * 2,
                       material_list(2, [1, 0], ref("Top"), material("Inline", texture="b.png")))),
            frame("G", None, mesh(TRIANGLE, [(0, 1, 2)] * 3, material_list(1, [0], ref("Top")))))
        xf = xfile.parse(data)
        self.assertEqual([m.name for m in xf.materials], ["Top", "Inline"])
        top = xf.materials[0]
        self.assertEqual((top.face_color, top.power, top.texture), ((1, 0.5, 0.25, 1), 3.5, "Wood.JPG"))
        self.assertEqual((top.specular, top.emissive), ((0.125, 0.25, 0.375), (0.5, 0.625, 0.75)))
        self.assertEqual(xf.materials[1].texture, "b.png")
        f, g = xf.roots
        self.assertEqual(f.meshes[0].face_materials, [0])
        self.assertEqual(f.meshes[1].face_materials, [1, 0])
        self.assertEqual(g.meshes[0].face_materials, [0, 0, 0])  # one index repeated over all faces
        self.assertTrue(any("1 material indexes for 3 faces" in w for w in xf.warnings))

    def test_unknown_material_reference_warns(self):
        xf = xfile.parse(xfile_bytes(mesh(TRIANGLE, [(0, 1, 2)], material_list(1, [0], ref("Nope")))))
        self.assertEqual(xf.roots[0].face_materials, [-1])
        self.assertTrue(any("Nope" in w for w in xf.warnings))

    def test_unknown_templates_are_skipped_with_a_warning(self):
        data = xfile_bytes(
            template("FVFData"),
            obj("FVFData", None, ints(2, 1, 2), obj("Nested", None, floats(1.0)), ref("Elsewhere")),
            obj("XSkinMeshHeader", None, ints(1, 2, 3)),
            frame("F", None, obj("FVFData", None, ints(1))))
        xf = xfile.parse(data)
        self.assertEqual(xf.unknown_templates, {"FVFData": 2, "XSkinMeshHeader": 1})
        self.assertEqual([f.name for f in xf.frames()], ["F"])
        self.assertEqual(len([w for w in xf.warnings if "FVFData" in w]), 1)

    def test_animations(self):
        key = lambda t, *v: (ints(t, len(v)), floats(*v))
        keys = [part for k in (key(0, *IDENTITY), key(480, *IDENTITY[:12], 1, 2, 3, 1)) for part in k]
        data = xfile_bytes(
            obj("AnimTicksPerSecond", None, ints(30)),
            frame("A", None),
            obj("AnimationSet", "Anim-1",
                obj("Animation", "", ref("A"), obj("AnimationKey", None, ints(4, 2), *keys),
                    obj("AnimationOptions", None, ints(1, 0))),
                obj("Animation", "", ref("A"), obj("AnimationKey", None, ints(0, 1), *key(5, 1, 0, 0, 0)))),
            obj("AnimationSet", "Empty"))
        xf = xfile.parse(data)
        self.assertEqual(xf.ticks_per_second, 30)
        self.assertEqual([s.name for s in xf.animation_sets], ["Anim-1", "Empty"])
        first, second = xf.animation_sets[0].animations
        self.assertEqual(first.frame, "A")
        self.assertEqual(first.key_sets[0].key_type, xfile.KEY_MATRIX_ALT)
        self.assertEqual([t for t, _ in first.key_sets[0].keys], [0, 480])
        self.assertEqual(first.key_sets[0].keys[1][1][12:15], (1, 2, 3))
        self.assertEqual(second.key_sets[0].keys, [(5, (1, 0, 0, 0))])
        self.assertEqual(xf.animation_sets[1].animations, [])
        self.assertEqual(xf.unknown_templates, {})

    def test_vertex_colors_and_loose_mesh(self):
        colors = obj("MeshVertexColors", None, ints(2), ints(0), floats(1, 0, 0, 1), ints(2), floats(0, 1, 0, 0.5))
        xf = xfile.parse(xfile_bytes(mesh(TRIANGLE, [(0, 1, 2)], colors)))
        self.assertIsInstance(xf.roots[0], xfile.XMesh)
        self.assertEqual(xf.roots[0].vertex_colors, [(0, 1, 0, 0, 1), (2, 0, 1, 0, 0.5)])
        self.assertEqual(list(xf.frames()), [])

    def test_mismatched_normals_and_uvs_are_dropped_with_a_warning(self):
        xf = xfile.parse(xfile_bytes(mesh(TRIANGLE, [(0, 1, 2)], normals([(0, 0, -1)], []), uvs([(0, 0)]))))
        m = xf.roots[0]
        self.assertEqual((m.normals, m.face_normals, m.uvs), ([], [], []))
        self.assertEqual(len(xf.warnings), 2)

    def test_double_precision_files(self):
        pos = struct.pack("<HI9d", xfile.TOKEN_FLOAT_LIST, 9, *[c for p in TRIANGLE for c in p])
        data = xfile_bytes(obj("Mesh", "", ints(3), pos, ints(1, 3, 0, 1, 2)), header=b"xof 0303bin 0064")
        self.assertEqual(xfile.parse(data).roots[0].positions, TRIANGLE)

    def test_malformed_files_raise(self):
        good = xfile_bytes(frame("F", IDENTITY, mesh(TRIANGLE, [(0, 1, 2)])))
        xfile.parse(good)
        bad = {
            "not an x file": b"PK\x03\x04" + bytes(20),
            "text format": b"xof 0303txt 0032" + b"Frame {}",
            "truncated": good[:-9],
            "face past vertices": xfile_bytes(mesh(TRIANGLE, [(0, 1, 3)])),
            "unknown token": xfile_bytes(tok(0x77)),
            "short matrix": xfile_bytes(frame("F", [1, 0, 0])),
            "unterminated string": xfile_bytes(obj("TextureFilename", None, string("a", terminator=OBRACE))),
        }
        for label, data in bad.items():
            with self.subTest(label), self.assertRaises(xfile.XFileError):
                xfile.parse(data)


def read_glb(path):
    data = pathlib.Path(path).read_bytes()
    json_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_len])
    return doc, data[20 + json_len + 8:]


def accessor_values(doc, binary, index):
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    width = x2glb.TYPE_WIDTH[a["type"]]
    code = "I" if a["componentType"] == x2glb.UNSIGNED_INT else "f"
    values = struct.unpack_from(f"<{a['count'] * width}{code}", binary, view["byteOffset"])
    return [values[i:i + width] if width > 1 else values[i] for i in range(0, len(values), width)]


class ConverterTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = pathlib.Path(self.tmp.name)
        self.skin, self.out = root / "SkinMeshes", root / "Meshes"
        self.textures = root / "Textures"
        (self.skin / "Sub").mkdir(parents=True)
        (self.textures / "Deep").mkdir(parents=True)
        for f in ("Wood.jpg", "Deep/only deep.png", "Both.jpg", "Deep/Both.jpg", "Stem.png", "Stem.jpg"):
            (self.textures / f).write_bytes(b"x")

    def convert(self, data, rel="a.X"):
        path = self.skin / rel
        path.write_bytes(data)
        stats = x2glb.convert_all(self.skin, self.textures, self.out)
        return stats, read_glb(self.out / (rel[:-2].lower() + ".glb"))

    def test_nodes_names_matrices_and_scene(self):
        moved = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 6, 7, 1]
        stats, (doc, binary) = self.convert(xfile_bytes(
            frame("Root", moved, frame("", IDENTITY, mesh(TRIANGLE, [(0, 1, 2)])), frame("ph_Box", None)),
            frame("Second", None)))
        self.assertEqual(stats["files"], 1)
        self.assertEqual(stats["nodes"], 4)
        names = [n.get("name") for n in doc["nodes"]]
        self.assertEqual(names, ["Root", None, "ph_Box", "Second"])
        self.assertEqual(doc["nodes"][0]["matrix"], [float(v) for v in moved])
        self.assertEqual(doc["nodes"][0]["children"], [1, 2])
        self.assertNotIn("matrix", doc["nodes"][1])  # identity is omitted
        self.assertNotIn("matrix", doc["nodes"][2])
        self.assertEqual(doc["nodes"][1]["mesh"], 0)
        self.assertNotIn("mesh", doc["nodes"][2])
        self.assertEqual(doc["scenes"][0]["nodes"], [0, 3])
        self.assertEqual(doc["extras"], {"source": "SkinMeshes/a.X", "coordinateSystem": "d3d-left-handed"})
        self.assertNotIn("animations", doc)

    def test_quad_is_fan_triangulated_with_original_winding_and_computed_normals(self):
        stats, (doc, binary) = self.convert(xfile_bytes(frame("F", None, mesh(QUAD, [(0, 1, 2, 3)]))))
        prim = doc["meshes"][0]["primitives"][0]
        self.assertEqual(prim["mode"], 4)
        self.assertNotIn("material", prim)
        attrs = prim["attributes"]
        indices = accessor_values(doc, binary, prim["indices"])
        positions = accessor_values(doc, binary, attrs["POSITION"])
        self.assertEqual(indices, [0, 1, 2, 0, 2, 3])
        self.assertEqual([tuple(positions[i]) for i in indices], [QUAD[i] for i in (0, 1, 2, 0, 2, 3)])
        self.assertEqual(set(accessor_values(doc, binary, attrs["NORMAL"])), {(0.0, 0.0, -1.0)})
        self.assertEqual(set(accessor_values(doc, binary, attrs["TEXCOORD_0"])), {(0.0, 0.0)})
        pos_accessor = doc["accessors"][attrs["POSITION"]]
        self.assertEqual((pos_accessor["min"], pos_accessor["max"]), ([0, 0, 0], [1, 1, 0]))
        self.assertEqual(doc["accessors"][prim["indices"]]["componentType"], x2glb.UNSIGNED_INT)
        self.assertEqual((stats["triangles"], stats["vertices"], stats["primitives"]), (2, 4, 1))

    def test_vertices_are_unique_position_normal_pairs(self):
        tris = [(0, 1, 2), (0, 2, 3)]
        # The two triangles share positions 0 and 2 but use different normals.
        n = normals([(0, 0, -1), (0, 1, 0)], [(0, 0, 0), (1, 1, 1)])
        stats, (doc, binary) = self.convert(xfile_bytes(frame("F", None, mesh(QUAD, tris, n, uvs(
            [(0, 1), (0, 0), (1, 0), (1, 1)])))))
        prim = doc["meshes"][0]["primitives"][0]
        self.assertEqual(stats["vertices"], 6)
        normals_out = accessor_values(doc, binary, prim["attributes"]["NORMAL"])
        self.assertEqual(sorted(set(normals_out)), [(0.0, 0.0, -1.0), (0.0, 1.0, 0.0)])
        uv = accessor_values(doc, binary, prim["attributes"]["TEXCOORD_0"])
        self.assertIn((1.0, 1.0), uv)

    def test_file_normals_are_normalized(self):
        n = normals([(0, 0, -3)], [(0, 0, 0)])
        _, (doc, binary) = self.convert(xfile_bytes(frame("F", None, mesh(TRIANGLE, [(0, 1, 2)], n))))
        out = accessor_values(doc, binary, doc["meshes"][0]["primitives"][0]["attributes"]["NORMAL"])
        self.assertEqual(set(out), {(0.0, 0.0, -1.0)})

    def test_one_primitive_per_material_and_meshes_in_a_frame_merge(self):
        data = xfile_bytes(
            material("Red", (1, 0, 0, 1)), material("Blue", (0, 0, 1, 1), texture="wood.JPG"),
            material("Unused"),
            frame("F", None,
                  mesh(QUAD, [(0, 1, 2), (0, 2, 3)], material_list(2, [1, 0], ref("Red"), ref("Blue"))),
                  mesh(TRIANGLE, [(0, 1, 2)], material_list(1, [0], ref("Blue")))))
        stats, (doc, binary) = self.convert(data)
        self.assertEqual(len(doc["meshes"]), 1)
        prims = doc["meshes"][0]["primitives"]
        self.assertEqual([p["material"] for p in prims], [0, 1])
        self.assertEqual([doc["accessors"][p["indices"]]["count"] for p in prims], [3, 6])
        self.assertEqual([m["name"] for m in doc["materials"]], ["Red", "Blue", "Unused"])
        self.assertEqual(doc["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"], [1, 0, 0, 1])
        self.assertEqual((stats["materials"], stats["primitives"]), (3, 2))

    def test_texture_resolution(self):
        cases = {
            "wood.JPG": ("../Textures/Wood.jpg", False),
            "only deep.png": ("../Textures/Deep/only%20deep.png", False),
            "Both.jpg": ("../Textures/Both.jpg", False),  # root before subfolders
            "Stem.jpg": ("../Textures/Stem.jpg", False),  # exact name wins over a same-stem file
            "stem.dds": ("../Textures/Stem.png", False),  # same stem, other extension (png preferred)
            "Gone.jpg": (None, True),
        }
        mats = [material(f"m{i}", texture=t) for i, t in enumerate(cases)]
        stats, (doc, binary) = self.convert(xfile_bytes(*mats, frame("F", None)))
        for i, (texture, (uri, missing)) in enumerate(cases.items()):
            m = doc["materials"][i]
            self.assertEqual(m["extras"]["textureFile"], texture)
            if missing:
                self.assertTrue(m["extras"]["textureMissing"])
                self.assertNotIn("baseColorTexture", m["pbrMetallicRoughness"])
            else:
                self.assertNotIn("textureMissing", m["extras"])
                image = doc["textures"][m["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]
                self.assertEqual(doc["images"][image]["uri"], uri, texture)
        self.assertEqual(stats["unresolved_textures"], {"Gone.jpg": ["a.X"]})
        self.assertEqual(len({im["uri"] for im in doc["images"]}), len(doc["images"]))

    def test_material_extras(self):
        mat = material("M", (0.5, 0.25, 0.125, 1), 3.5, (0.5, 0.5, 0.5), (0.25, 0, 0))
        _, (doc, _) = self.convert(xfile_bytes(mat, frame("F", None)))
        m = doc["materials"][0]
        self.assertEqual(m["pbrMetallicRoughness"]["metallicFactor"], 0)
        self.assertEqual(m["pbrMetallicRoughness"]["roughnessFactor"], 1)
        self.assertEqual(m["extras"], {"power": 3.5, "specular": [0.5, 0.5, 0.5], "emissive": [0.25, 0, 0]})

    def test_mesh_outside_a_frame_gets_a_synthetic_root(self):
        stats, (doc, _) = self.convert(xfile_bytes(mesh(TRIANGLE, [(0, 1, 2)])))
        self.assertEqual(doc["nodes"], [{"mesh": 0}])
        self.assertEqual(doc["scenes"][0]["nodes"], [0])
        self.assertEqual(stats["nodes"], 1)

    def animated(self, rest, sets, ticks=None):
        """The converted file of a frame "A" with the matrix `rest` and the given AnimationSets (stats in self.stats)."""
        parts = [obj("AnimTicksPerSecond", None, ints(ticks))] if ticks else []
        stats, (doc, binary) = self.convert(xfile_bytes(*parts, frame("A", rest), *sets))
        self.stats = stats
        return doc, binary

    def test_matrix_keys_become_gltf_animations_in_seconds(self):
        turned = [0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]  # a quarter turn about z
        scaled = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1]
        data = [obj("AnimationSet", "Anim-1",
                    obj("Animation", "", ref("A"),
                        obj("AnimationKey", None, ints(4, 4), ints(0, 16), floats(*IDENTITY),
                            ints(4800, 16), floats(*scaled), ints(9600, 16), floats(*turned),
                            ints(9600, 16), floats(*scaled))))]  # a tick written twice: the last one counts
        doc, binary = self.animated(scaled, data)
        self.assertEqual(len(doc["animations"]), 1)
        animation = doc["animations"][0]
        self.assertEqual(animation["name"], "Anim-1")
        node = doc["nodes"].index(next(n for n in doc["nodes"] if n.get("name") == "A"))
        channels = {c["target"]["path"]: c for c in animation["channels"]}
        self.assertEqual(sorted(channels), ["rotation", "scale", "translation"])
        self.assertTrue(all(c["target"]["node"] == node for c in channels.values()))
        values = {}
        for path, channel in channels.items():
            sampler = animation["samplers"][channel["sampler"]]
            self.assertEqual(sampler["interpolation"], "LINEAR")
            times = accessor_values(doc, binary, sampler["input"])
            values[path] = accessor_values(doc, binary, sampler["output"])
            self.assertEqual(times, [0.0, 1.0, 2.0])  # 4800 ticks a second by default
            self.assertEqual(doc["accessors"][sampler["input"]]["min"], [0.0])
            self.assertEqual(doc["accessors"][sampler["input"]]["max"], [2.0])
        self.assertEqual(values["translation"], [(0.0, 0.0, 0.0), (1.0, 2.0, 3.0), (1.0, 2.0, 3.0)])
        self.assertEqual(values["scale"], [(1.0, 1.0, 1.0), (2.0, 2.0, 2.0), (2.0, 2.0, 2.0)])
        self.assertAlmostEqual(values["rotation"][0][3], 1.0)
        self.assertAlmostEqual(values["rotation"][1][3], 1.0)  # the scaled key turns nothing
        self.assertAlmostEqual(values["rotation"][2][3], 1.0)  # the last tick was written twice: scaled counts
        self.assertEqual((self.stats["animation_sets"], self.stats["animation_tracks"]), (1, 1))
        # A node that is animated has no matrix: its own is the rest pose as translation, rotation and scale.
        self.assertNotIn("matrix", doc["nodes"][node])
        self.assertEqual(doc["nodes"][node]["translation"], [1.0, 2.0, 3.0])
        self.assertEqual(doc["nodes"][node]["scale"], [2.0, 2.0, 2.0])
        self.assertNotIn("rotation", doc["nodes"][node])

    def test_a_turn_is_a_quaternion_and_neighbours_stay_on_one_side(self):
        def about_z(degrees):
            c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
            return [c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        angles = (0, 100, 200, 300)  # w changes sign on the way round; -q is the same turn as q
        keys = [part for i, a in enumerate(angles) for part in (ints(i * 4800, 16), floats(*about_z(a)))]
        doc, binary = self.animated(IDENTITY, [obj("AnimationSet", "S", obj(
            "Animation", "", ref("A"), obj("AnimationKey", None, ints(4, len(angles)), *keys)))])
        channel = next(c for c in doc["animations"][0]["channels"] if c["target"]["path"] == "rotation")
        q = accessor_values(doc, binary, doc["animations"][0]["samplers"][channel["sampler"]]["output"])
        for a, key in zip(angles, q):
            self.assertAlmostEqual(key[2], math.sin(math.radians(a) / 2) * (1 if abs(key[3]) < 2 else -1), delta=1.0)
            self.assertAlmostEqual(math.sqrt(sum(v * v for v in key)), 1.0, places=6)
        for before, after in zip(q, q[1:]):
            self.assertGreaterEqual(sum(a * b for a, b in zip(before, after)), 0)
        self.assertAlmostEqual(q[1][2], math.sin(math.radians(50)), places=6)
        self.assertAlmostEqual(q[1][3], math.cos(math.radians(50)), places=6)

    def test_decompose_puts_the_matrix_back_together(self):
        cases = {
            "identity": IDENTITY,
            "moved and scaled": [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1],
            "turned about x, y and z": [0.36, 0.48, -0.8, 0, -0.8, 0.6, 0, 0, 0.48, 0.64, 0.6, 0, 1, 2, 3, 1],
            "a half turn": [-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
            "a mirror": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -2, 0, 0, 0, 0, 1],
        }
        for label, matrix in cases.items():
            with self.subTest(label):
                t, q, s = x2glb.decompose([float(v) for v in matrix])
                again = x2glb.compose(t, q, s)
                self.assertTrue(all(abs(a - b) < 1e-6 for a, b in zip(again, matrix)), again)
        # A shear is dropped, not refused; a flat axis or a projection cannot be decomposed.
        sheared = [1, 0, 0, 0, 0.3, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        self.assertIsNotNone(x2glb.decompose([float(v) for v in sheared]))
        self.assertIsNone(x2glb.decompose([0.0] * 5 + [1.0] + [0.0] * 4 + [1.0, 0, 0, 0, 0, 1.0]))
        self.assertIsNone(x2glb.decompose([float(v) for v in IDENTITY[:15]] + [2.0]))

    def test_ticks_per_second_sets_the_time_and_scale_and_position_keys_convert(self):
        sets = [obj("AnimationSet", "S", obj(
            "Animation", "", ref("A"),
            obj("AnimationKey", None, ints(2, 2), ints(0, 3), floats(0, 0, 0), ints(60, 3), floats(1, 2, 3)),
            obj("AnimationKey", None, ints(1, 1), ints(0, 3), floats(2, 2, 2))))]
        doc, binary = self.animated(IDENTITY, sets, ticks=30)
        channels = {c["target"]["path"]: c for c in doc["animations"][0]["channels"]}
        self.assertEqual(sorted(channels), ["scale", "translation"])
        sampler = doc["animations"][0]["samplers"][channels["translation"]["sampler"]]
        self.assertEqual(accessor_values(doc, binary, sampler["input"]), [0.0, 2.0])
        self.assertEqual(accessor_values(doc, binary, sampler["output"]), [(0.0, 0.0, 0.0), (1.0, 2.0, 3.0)])

    def test_what_cannot_be_converted_is_skipped_with_a_warning(self):
        rotation_key = obj("AnimationKey", None, ints(0, 1), ints(0, 4), floats(1, 0, 0, 0))
        flat = [0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        sets = [obj("AnimationSet", "Empty"),
                obj("AnimationSet", "Rotation", obj("Animation", "", ref("A"), rotation_key)),
                obj("AnimationSet", "Flat", obj("Animation", "", ref("A"), obj(
                    "AnimationKey", None, ints(4, 1), ints(0, 16), floats(*flat)))),
                obj("AnimationSet", "Nowhere", obj("Animation", "", ref("Gone"), obj(
                    "AnimationKey", None, ints(2, 1), ints(0, 3), floats(1, 1, 1))))]
        stats, (doc, _) = self.convert(xfile_bytes(frame("A", None), *sets))
        self.assertNotIn("animations", doc)
        self.assertEqual((stats["animation_sets"], stats["animation_tracks"]), (0, 0))
        text = " ".join(stats["warnings"])
        for needle in ("key type 0", "key has no scale", "no frame 'Gone'"):
            self.assertIn(needle, text)
        self.assertNotIn("Empty", text)

    def test_a_frame_that_cannot_be_decomposed_keeps_its_matrix_and_is_not_animated(self):
        flat = [0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1]
        key = obj("AnimationKey", None, ints(2, 1), ints(0, 3), floats(1, 1, 1))
        stats, (doc, _) = self.convert(xfile_bytes(
            frame("A", flat), obj("AnimationSet", "S", obj("Animation", "", ref("A"), key))))
        self.assertEqual(doc["nodes"][0]["matrix"], [float(v) for v in flat])
        self.assertNotIn("animations", doc)
        self.assertTrue(any("frame's matrix" in w for w in stats["warnings"]))

    def test_paths_and_unknown_templates(self):
        data = xfile_bytes(obj("Weird", None, ints(1)), frame("F", None))
        (self.skin / "Sub" / "Deep.X").write_bytes(data)
        (self.skin / "Top.x").write_bytes(data)
        stats = x2glb.convert_all(self.skin, self.textures, self.out)
        self.assertEqual(stats["files"], 2)
        self.assertEqual(stats["unknown_templates"], {"Weird": 2})
        self.assertTrue((self.out / "sub" / "deep.glb").is_file())
        self.assertTrue((self.out / "top.glb").is_file())
        self.assertEqual(read_glb(self.out / "sub" / "deep.glb")[0]["extras"]["source"], "SkinMeshes/Sub/Deep.X")

    def test_failures_are_reported_after_the_whole_run(self):
        good = xfile_bytes(frame("F", None))
        (self.skin / "good.X").write_bytes(good)
        (self.skin / "bad.X").write_bytes(good[:-3])
        with self.assertRaisesRegex(x2glb.ConversionError, "(?s)1 of 2 meshes failed.*bad.X"):
            x2glb.convert_all(self.skin, self.textures, self.out)
        self.assertTrue((self.out / "good.glb").is_file())

    def test_case_colliding_outputs_fail(self):
        good = xfile_bytes(frame("F", None))
        (self.skin / "Same.X").write_bytes(good)
        (self.skin / "same.x").write_bytes(good)
        if len(list(self.skin.glob("*.[xX]"))) < 2:
            self.skipTest("case-insensitive file system")
        with self.assertRaisesRegex(x2glb.ConversionError, "also produced"):
            x2glb.convert_all(self.skin, self.textures, self.out)


class ValidatorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = pathlib.Path(self.tmp.name)
        (root / "Textures").mkdir()
        (root / "Textures" / "t.jpg").write_bytes(b"x")
        xf = xfile.parse(xfile_bytes(
            material("M", texture="t.jpg"),
            frame("F", None, mesh(QUAD, [(0, 1, 2, 3)], material_list(1, [0], ref("M"))))))
        resolver = x2glb.TextureResolver(root / "Textures")
        gltf, binary, *_ = x2glb.build_glb(xf, "SkinMeshes/v.X", root / "Meshes", resolver)
        self.gltf, self.binary, self.path = gltf, binary, root / "Meshes" / "v.glb"
        self.path.parent.mkdir()

    def write(self, gltf=None, binary=None):
        x2glb.write_glb(self.path, gltf or self.gltf, self.binary if binary is None else binary)
        return x2glb.validate_glb(self.path, expected_nodes=1)

    def test_valid_file(self):
        self.assertEqual(self.write(), [])
        data = self.path.read_bytes()
        self.assertEqual(data[:4], b"glTF")
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], len(data))
        self.assertEqual(len(data) % 4, 0)

    def test_wrong_node_count(self):
        self.write()
        self.assertTrue(any("source has 3" in p for p in x2glb.validate_glb(self.path, expected_nodes=3)))

    def test_index_past_vertex_count(self):
        prim = self.gltf["meshes"][0]["primitives"][0]
        view = self.gltf["bufferViews"][self.gltf["accessors"][prim["indices"]]["bufferView"]]
        binary = bytearray(self.binary)
        struct.pack_into("<I", binary, view["byteOffset"], 99)
        self.assertTrue(any("past the vertex count" in p for p in self.write(binary=bytes(binary))))

    def test_non_unit_normal(self):
        prim = self.gltf["meshes"][0]["primitives"][0]
        view = self.gltf["bufferViews"][self.gltf["accessors"][prim["attributes"]["NORMAL"]]["bufferView"]]
        binary = bytearray(self.binary)
        struct.pack_into("<3f", binary, view["byteOffset"], 0, 0, -2)
        self.assertTrue(any("not unit length" in p for p in self.write(binary=bytes(binary))))

    def test_wrong_position_bounds(self):
        prim = self.gltf["meshes"][0]["primitives"][0]
        self.gltf["accessors"][prim["attributes"]["POSITION"]]["max"] = [1, 1, 5]
        self.assertTrue(any("POSITION max[2]" in p for p in self.write()))

    def test_bad_material_index(self):
        self.gltf["meshes"][0]["primitives"][0]["material"] = 7
        self.assertTrue(any("material 7" in p for p in self.write()))

    def test_accessor_past_its_view(self):
        self.gltf["accessors"][0]["count"] += 100
        self.assertTrue(any("out of range" in p for p in self.write()))

    def test_missing_texture_file(self):
        (self.path.parent.parent / "Textures" / "t.jpg").unlink()
        self.assertTrue(any("does not exist" in p for p in self.write()))

    def animation_file(self):
        """A valid file with a one-key-pair animation of node 0, to break."""
        rest = x2glb.xfile.parse(xfile_bytes(frame("F", None, mesh(QUAD, [(0, 1, 2, 3)])), obj(
            "AnimationSet", "S", obj("Animation", "", ref("F"), obj(
                "AnimationKey", None, ints(2, 2), ints(0, 3), floats(0, 0, 0), ints(4800, 3), floats(1, 0, 0))))))
        gltf, binary, *_ = x2glb.build_glb(rest, "SkinMeshes/v.X", self.path.parent, x2glb.TextureResolver(self.path.parent))
        self.gltf, self.binary = gltf, binary
        self.assertEqual(self.write(), [])
        return gltf["animations"][0], binary

    def test_animation_with_a_matrix_node(self):
        animation, _ = self.animation_file()
        self.gltf["nodes"][animation["channels"][0]["target"]["node"]]["matrix"] = x2glb.IDENTITY
        self.assertTrue(any("has a matrix" in p for p in self.write()))

    def test_animation_times_must_increase(self):
        animation, binary = self.animation_file()
        sampler = animation["samplers"][0]
        view = self.gltf["bufferViews"][self.gltf["accessors"][sampler["input"]]["bufferView"]]
        broken = bytearray(binary)
        struct.pack_into("<2f", broken, view["byteOffset"], 1.0, 1.0)
        self.assertTrue(any("strictly increase" in p for p in self.write(binary=bytes(broken))))

    def test_animation_values_must_match_the_times_and_the_path(self):
        animation, _ = self.animation_file()
        sampler = animation["samplers"][0]
        self.gltf["accessors"][sampler["output"]]["count"] -= 1
        self.assertTrue(any("2 times, 1 values" in p for p in self.write()))
        self.gltf["accessors"][sampler["output"]]["count"] += 1
        animation["channels"][0]["target"]["path"] = "rotation"
        self.assertTrue(any("rotation values are VEC3" in p for p in self.write()))

    def test_animation_with_a_bad_target(self):
        animation, _ = self.animation_file()
        animation["channels"][0]["target"]["node"] = 9
        self.assertTrue(any("bad node" in p for p in self.write()))

    def test_broken_container(self):
        self.write()
        data = self.path.read_bytes()
        for label, broken in {"magic": b"XXXX" + data[4:], "truncated": data[:-8],
                              "length": data[:8] + struct.pack("<I", 5) + data[12:]}.items():
            with self.subTest(label):
                self.path.write_bytes(broken)
                self.assertTrue(x2glb.validate_glb(self.path))


if __name__ == "__main__":
    unittest.main()
