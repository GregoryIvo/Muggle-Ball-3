#!/usr/bin/env python3
"""Round-trip small synthetic .mb3 files through tools/mb3level.py and the level step of convert_assets.py."""
import contextlib
import io
import json
import pathlib
import re
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import convert_assets  # noqa: E402
import mb3level as M  # noqa: E402
import nxconvex  # noqa: E402

IDENTITY_POSE = struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)
FLT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]


def string(s):
    b = s.encode()
    return struct.pack("<I", len(b)) + b


def actor_desc(position, has_object, density=0.0, flags=0, group=2):
    desc = bytearray(M.ACTOR_DESC_SIZE)
    desc[:48] = struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, *position)
    struct.pack_into("<f", desc, 0x34, density)
    struct.pack_into("<I", desc, 0x38, flags)
    struct.pack_into("<I", desc, 0x3C, 0xCCCC0000 | group)  # the upper half is uninitialised padding
    struct.pack_into("<I", desc, 0x40, 0x0CCCD6A8 if has_object else 0)
    return bytes(desc)


def body_desc(mass=12.5, flags=0x100):
    raw = bytearray(M.BODY_DESC_SIZE)
    struct.pack_into("<12f", raw, 0x00, 0, 1, 0, -1, 0, 0, 0, 0, 1, 0.5, 1.5, -2.5)  # massLocalPose
    struct.pack_into("<3f", raw, 0x30, 4.5, 5.5, 6.5)       # massSpaceInertia
    struct.pack_into("<f", raw, 0x3C, mass)
    struct.pack_into("<3f", raw, 0x40, 0.25, -0.5, 0.75)    # linearVelocity
    struct.pack_into("<3f", raw, 0x4C, 1e-9, 2.0, 3.0)      # angularVelocity
    struct.pack_into("<ffff", raw, 0x58, 0.4, 0.1, 0.8, 7.0)  # wakeUp, linearDamping, angularDamping, maxAngularVelocity
    struct.pack_into("<I", raw, 0x68, flags)
    struct.pack_into("<ffI", raw, 0x6C, 0.15, 0.14, 4)      # sleep velocities, solverIterationCount
    return bytes(raw)


def shape_raw(stype, geometry=b"", flags=8, group=2, material=0x9D, skin=0.005, pose=IDENTITY_POSE):
    raw = bytearray(M.SHAPE_DESC_SIZE[stype])
    struct.pack_into("<I", raw, 0, stype)
    raw[4:52] = pose
    struct.pack_into("<I", raw, 0x34, flags)
    struct.pack_into("<HH", raw, 0x38, group, material)
    struct.pack_into("<f", raw, 0x3C, skin)
    raw[0x58:0x58 + len(geometry)] = geometry
    return struct.pack("<I", stype) + bytes(raw)


def box_shape(half, **kw):
    return shape_raw(2, struct.pack("<3f", *half), **kw)


def sphere_shape(radius, **kw):
    return shape_raw(1, struct.pack("<f", radius), **kw)


def capsule_shape(radius, height, **kw):
    return shape_raw(3, struct.pack("<2fI", radius, height, 0), **kw)


def convex_shape(mesh_name, **kw):
    return shape_raw(4, **kw) + string(mesh_name)


def board_actor():
    return (actor_desc((0, 0, 0), True, group=4) + b"\x00" + struct.pack("<I", 2) + box_shape((3, 22, 15))
            + capsule_shape(0.91, 9.8)
            + struct.pack("<III", 1, 2, M.FLAG_MATERIAL | M.FLAG_MESH)
            + string("boardmaterial") + string("boardsea.x"))


def segment_actor(x):
    return actor_desc((x, 0, 0), False) + b"\x01" + bytes(M.BODY_DESC_SIZE) + struct.pack("<I", 0)


def cloth_actor(object_id=9):
    # 2x2 cloth: the main actor plus 3 nested segment actors.
    return (actor_desc((5, 0, 0), True) + b"\x00" + struct.pack("<I", 0)
            + struct.pack("<III", 20, object_id, 0) + struct.pack("<5I", 2, 2, 0, 0, 0)
            + b"".join(segment_actor(i) for i in range(3)))


def path():
    point = struct.pack("<12f", 1, 2, 3, 0, 0, 0, 1, 0, 0, 0, 1, 0)
    return (struct.pack("<I", 1) + string("track/shark") + struct.pack("<I", 7)
            + struct.pack("<I", 8) + b"\xff\x00" + struct.pack("<I", 1) + point)


def level_bytes(actors=None, actor_count=5, joints=None):
    """A level with the given actor records, the number of actors the header declares, and joint records."""
    if actors is None:
        actors = board_actor() + cloth_actor()
    if joints is None:
        joints = [joint_record(0, 2, 2, bytes(M.JOINT_DESC_SIZE[2]))]  # world to the board (object id 2)
    return (M.MAGIC + struct.pack("<3f", 0, -9.81, 0)
            + struct.pack("<I", actor_count)
            + actors
            + struct.pack("<I", 0)  # global parameter
            + struct.pack("<I", len(joints)) + b"".join(joints)
            + path()
            + struct.pack("<I", 0))  # path bindings


def joint_record(actor_a, actor_b, jtype, desc):
    return struct.pack("<III", actor_a, actor_b, jtype) + desc


def joint_desc(jtype, max_force=FLT_MAX, max_torque=FLT_MAX, flags=2):
    desc = bytearray(M.JOINT_DESC_SIZE[jtype])
    struct.pack_into("<I", desc, 0, jtype)
    struct.pack_into("<I", desc, 0x04, 0x0C3EE700)  # stale actor pointers
    struct.pack_into("<I", desc, 0x08, 0x0B0B2080)
    struct.pack_into("<6f", desc, 0x0C, 1, 0, 0, 0, 1, 0)           # localNormal[0..1]
    struct.pack_into("<6f", desc, 0x24, 0, 0, 1, 0, 0, -1)          # localAxis[0..1]
    struct.pack_into("<6f", desc, 0x3C, 1.5, 2.5, 3.5, -4.5, 5.5, 6.5)  # localAnchor[0..1]
    struct.pack_into("<2f", desc, 0x54, max_force, max_torque)
    struct.pack_into("<I", desc, 0x64, 0x0C405E00 | flags)  # a stale pointer whose low bits are the flags
    return desc


def revolute_desc():
    desc = joint_desc(1, 150, 120)
    struct.pack_into("<6f", desc, 0x68, 0.5, 0.25, 0.75, 1.5, 0.125, 0.5)       # limit low, high
    struct.pack_into("<ffI", desc, 0x80, 3.0, 80.0, 1)                           # motor: velTarget, maxForce, freeSpin
    struct.pack_into("<3f", desc, 0x8C, 10.0, 2.0, 0.5)                          # spring
    struct.pack_into("<ff", desc, 0x98, 1.0, 0.0872)                             # projection distance, angle
    struct.pack_into("<II", desc, 0xA0, 7, 1)                                      # flags, projectionMode
    return desc


def spherical_desc():
    desc = joint_desc(3, 100, 100)
    struct.pack_into("<3f", desc, 0x68, 0, 0, 1)                                 # swingAxis
    struct.pack_into("<f", desc, 0x74, 1.0)                                      # projectionDistance
    struct.pack_into("<6f", desc, 0x78, -0.5, 0.1, 0.9, 0.5, 0.2, 0.8)           # twistLimit low, high
    struct.pack_into("<3f", desc, 0x90, 0.7, 0.3, 0.6)                           # swingLimit
    struct.pack_into("<3f", desc, 0x9C, 1, 2, 3)                                 # twistSpring
    struct.pack_into("<3f", desc, 0xA8, 4, 5, 6)                                 # swingSpring
    struct.pack_into("<3f", desc, 0xB4, 7, 8, 9)                                 # jointSpring
    struct.pack_into("<II", desc, 0xC0, 31, 0)                                   # flags, projectionMode
    return desc


def hull_file(points, triangles=None):
    data = struct.pack("<I", len(points)) + b"".join(struct.pack("<3f", *p) for p in points)
    if triangles is not None:
        data += struct.pack("<I", len(triangles)) + b"".join(struct.pack("<3I", *t) for t in triangles)
    return data


TETRA = [(0, 0, 0), (2, 0, 0), (0, 3, 0), (0, 0, 4)]


class HullStub:
    def points(self, name):
        return [list(p) for p in TETRA]


def physics_level_bytes():
    """Brick 10 (body, box + convex), cloth 20 with segments 21..23, and one joint of each kind."""
    brick = (actor_desc((1, 2, 3), True, density=2.0, flags=0, group=2)
             + b"\x01" + body_desc() + struct.pack("<I", 2)
             + box_shape((0.5, 1.0, 1.5), flags=8, group=2, material=0x9D)
             + convex_shape("Rock_ph.cnvx", flags=15, group=5, material=7, skin=-1.0)
             + struct.pack("<III", 4, 10, M.FLAG_MESH) + string("rock.x"))
    joints = [
        joint_record(0, 10, 8, joint_desc(8, 500, 400)),     # fixed, brick to the world
        joint_record(10, 20, 1, revolute_desc()),            # revolute, brick to the cloth's own actor
        joint_record(22, 10, 3, spherical_desc()),           # spherical, cloth segment 2 to the brick
    ]
    return level_bytes(brick + cloth_actor(object_id=20), actor_count=5, joints=joints)


class Mb3LevelTest(unittest.TestCase):
    def test_parse(self):
        level = M.parse(level_bytes())
        self.assertEqual(level["version"], 3)
        self.assertEqual(level["trailingBytes"], 0)
        self.assertEqual(len(level["actors"]), 2)
        board = level["actors"][0]
        self.assertEqual(board["object"]["type"], "board")
        self.assertEqual(board["object"]["mesh"], "boardsea.x")
        self.assertEqual(board["shapes"][0]["halfExtents"], [3, 22, 15])
        self.assertEqual(board["shapes"][1]["type"], "capsule")
        self.assertEqual(board["shapes"][1]["radiusHeight"], [0.91, 9.8])
        cloth = level["actors"][1]["object"]["cloth"]
        self.assertEqual(len(cloth["segments"]), 3)
        self.assertIn("body", cloth["segments"][0])
        self.assertEqual(level["joints"][0]["type"], 2)
        self.assertEqual(level["paths"][0]["points"][0]["pos"], [1, 2, 3])
        self.assertEqual(level["paths"][0]["points"][0]["up"], [0, 1, 0])

    def test_actor_descriptor(self):
        level = M.parse(physics_level_bytes(), HullStub())
        brick = level["actors"][0]
        self.assertEqual(brick["density"], 2.0)
        self.assertEqual(brick["actorFlags"], 0)
        self.assertEqual(brick["group"], 2)  # the padding in the upper half is not part of it
        self.assertEqual(brick["pose"]["position"], [1, 2, 3])
        board = M.parse(level_bytes())["actors"][0]
        self.assertEqual(board["group"], 4)
        self.assertNotIn("body", board)

    def test_body_descriptor(self):
        body = M.parse(physics_level_bytes(), HullStub())["actors"][0]["body"]
        self.assertEqual(body["massLocalPose"]["rotation"], [0, 1, 0, -1, 0, 0, 0, 0, 1])
        self.assertEqual(body["massLocalPose"]["position"], [0.5, 1.5, -2.5])
        self.assertEqual(body["massSpaceInertia"], [4.5, 5.5, 6.5])
        self.assertEqual(body["mass"], 12.5)
        self.assertEqual(body["linearVelocity"], [0.25, -0.5, 0.75])
        self.assertAlmostEqual(body["angularVelocity"][0], 1e-9, places=15)  # tiny values keep their precision
        self.assertEqual(body["angularVelocity"][1:], [2.0, 3.0])
        self.assertEqual(body["wakeUpCounter"], 0.4)
        self.assertEqual(body["linearDamping"], 0.1)
        self.assertEqual(body["angularDamping"], 0.8)
        self.assertEqual(body["maxAngularVelocity"], 7.0)
        self.assertEqual(body["flags"], 0x100)
        self.assertEqual(body["sleepLinearVelocity"], 0.15)
        self.assertEqual(body["sleepAngularVelocity"], 0.14)
        self.assertEqual(body["solverIterationCount"], 4)
        self.assertEqual(bytes.fromhex(body["raw"]), body_desc())
        # A cloth segment's all-zero descriptor decodes to zeros, not to an error.
        segment = M.parse(physics_level_bytes(), HullStub())["actors"][1]["object"]["cloth"]["segments"][0]["body"]
        self.assertEqual(segment["mass"], 0)

    def test_shape_descriptor(self):
        shapes = M.parse(physics_level_bytes(), HullStub())["actors"][0]["shapes"]
        box, convex = shapes
        self.assertEqual((box["shapeFlags"], box["group"], box["materialIndex"]), (8, 2, 0x9D))
        self.assertEqual(box["skinWidth"], 0.005)
        self.assertEqual(box["halfExtents"], [0.5, 1.0, 1.5])
        self.assertEqual((convex["shapeFlags"], convex["group"], convex["materialIndex"]), (15, 5, 7))
        self.assertEqual(convex["skinWidth"], -1.0)
        self.assertEqual(len(bytes.fromhex(box["raw"])), M.SHAPE_DESC_SIZE[2])

    def test_convex_hull_points(self):
        level = M.parse(physics_level_bytes(), HullStub())
        convex = level["actors"][0]["shapes"][1]
        self.assertEqual(convex["meshName"], "Rock_ph.cnvx")
        self.assertEqual(convex["hullPoints"], [list(map(float, p)) for p in TETRA])
        # Without a mesh folder the shape keeps its name and has no points.
        self.assertNotIn("hullPoints", M.parse(physics_level_bytes())["actors"][0]["shapes"][1])

    def test_convex_mesh_files(self):
        points = [(0.5, -1.25, 2.0), (1, 0, 0), (0, 1, 0), (0, 0, 1)]
        plain = nxconvex.parse(hull_file(points))
        self.assertEqual(plain["points"], [list(p) for p in points])
        self.assertEqual(plain["triangles"], [])
        with_tris = nxconvex.parse(hull_file(points, [(0, 1, 2), (0, 2, 3)]))
        self.assertEqual(with_tris["triangles"], [[0, 1, 2], [0, 2, 3]])
        for bad in (b"", struct.pack("<I", 0), hull_file(points)[:-1], hull_file(points) + b"\0",
                    hull_file(points, [(0, 1, 9)]), hull_file(points, [(0, 1, 2)])[:-4]):
            with self.assertRaises(ValueError):
                nxconvex.parse(bad)

    def test_convex_mesh_folder_ignores_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "Rock_PH.cnvx").write_bytes(hull_file(TETRA, [(0, 1, 2)]))
            folder = nxconvex.HullFolder(tmp)
            self.assertEqual(folder.points("rock_ph.cnvx"), [list(map(float, p)) for p in TETRA])
            with self.assertRaises(FileNotFoundError):
                folder.points("other.cnvx")
            with self.assertRaises(FileNotFoundError):
                M.parse(physics_level_bytes(), nxconvex.HullFolder(pathlib.Path(tmp) / "missing"))

    def test_fixed_joint(self):
        joint = M.parse(physics_level_bytes(), HullStub())["joints"][0]
        self.assertEqual((joint["actorA"], joint["actorB"], joint["type"]), (0, 10, 8))
        self.assertEqual(joint["localNormal"], [[1, 0, 0], [0, 1, 0]])
        self.assertEqual(joint["localAxis"], [[0, 0, 1], [0, 0, -1]])
        self.assertEqual(joint["localAnchor"], [[1.5, 2.5, 3.5], [-4.5, 5.5, 6.5]])
        self.assertEqual((joint["maxForce"], joint["maxTorque"]), (500, 400))
        self.assertEqual(joint["jointFlags"], 2)  # only the flag bits of the last word
        self.assertNotIn("limit", joint)
        self.assertEqual(bytes.fromhex(joint["raw"]), bytes(joint_desc(8, 500, 400)))

    def test_unbreakable_joint_keeps_float_max(self):
        text = json.dumps(M.parse(level_bytes(joints=[joint_record(0, 2, 8, joint_desc(8))])))
        joint = json.loads(text)["joints"][0]
        for key in ("maxForce", "maxTorque"):
            self.assertEqual(struct.unpack("<f", struct.pack("<f", joint[key]))[0], FLT_MAX)

    def test_revolute_joint(self):
        joint = M.parse(physics_level_bytes(), HullStub())["joints"][1]
        self.assertEqual((joint["actorA"], joint["actorB"], joint["type"]), (10, 20, 1))
        self.assertEqual((joint["maxForce"], joint["maxTorque"]), (150, 120))
        self.assertEqual(joint["limit"]["low"], {"value": 0.5, "restitution": 0.25, "hardness": 0.75})
        self.assertEqual(joint["limit"]["high"], {"value": 1.5, "restitution": 0.125, "hardness": 0.5})
        self.assertEqual(joint["motor"], {"velTarget": 3.0, "maxForce": 80.0, "freeSpin": True})
        self.assertEqual(joint["spring"], {"spring": 10.0, "damper": 2.0, "targetValue": 0.5})
        self.assertEqual(joint["projectionDistance"], 1.0)
        self.assertEqual(joint["projectionAngle"], 0.0872)
        self.assertEqual((joint["flags"], joint["projectionMode"]), (7, 1))
        self.assertEqual(len(bytes.fromhex(joint["raw"])), M.JOINT_DESC_SIZE[1])

    def test_spherical_joint(self):
        joint = M.parse(physics_level_bytes(), HullStub())["joints"][2]
        self.assertEqual((joint["actorA"], joint["actorB"], joint["type"]), (22, 10, 3))
        self.assertEqual(joint["swingAxis"], [0, 0, 1])
        self.assertEqual(joint["projectionDistance"], 1.0)
        self.assertEqual(joint["twistLimit"]["low"], {"value": -0.5, "restitution": 0.1, "hardness": 0.9})
        self.assertEqual(joint["twistLimit"]["high"], {"value": 0.5, "restitution": 0.2, "hardness": 0.8})
        self.assertEqual(joint["swingLimit"], {"value": 0.7, "restitution": 0.3, "hardness": 0.6})
        self.assertEqual(joint["twistSpring"], {"spring": 1, "damper": 2, "targetValue": 3})
        self.assertEqual(joint["swingSpring"], {"spring": 4, "damper": 5, "targetValue": 6})
        self.assertEqual(joint["jointSpring"], {"spring": 7, "damper": 8, "targetValue": 9})
        self.assertEqual((joint["flags"], joint["projectionMode"]), (31, 0))
        self.assertEqual(len(bytes.fromhex(joint["raw"])), M.JOINT_DESC_SIZE[3])

    def test_joint_references(self):
        level = M.parse(physics_level_bytes(), HullStub())
        refs = M.actor_references(level["actors"])
        brick, cloth = level["actors"]
        segments = cloth["object"]["cloth"]["segments"]
        self.assertIs(refs[10], brick)
        self.assertIs(refs[20], cloth)  # the cloth's own actor is segment 0
        self.assertEqual([refs[21], refs[22], refs[23]], segments)
        self.assertNotIn(0, refs)  # 0 is the world, never an actor
        self.assertNotIn(24, refs)

    def test_object_id_beats_cloth_segment_id(self):
        level = M.parse(level_bytes(joints=[]))
        # Cloth 9 owns 10..12; an object that also has id 10 keeps the name.
        level["actors"].append({"object": {"id": 10}, "shapes": []})
        refs = M.actor_references(level["actors"])
        self.assertIs(refs[10], level["actors"][-1])
        self.assertIs(refs[11], level["actors"][1]["object"]["cloth"]["segments"][1])

    def test_unresolved_joint_reference_is_rejected(self):
        for a, b in ((0, 99), (99, 2), (0, 0)):
            data = level_bytes(joints=[joint_record(a, b, 8, joint_desc(8))])
            with self.assertRaises(ValueError, msg=(a, b)):
                M.parse(data)

    def test_float32_text_round_trips(self):
        for bits in (0x7F7FFFFF, 0x3DCCCCCD, 0x00000001, 0xC1200000, 0x3F800001):
            x = struct.unpack("<f", struct.pack("<I", bits))[0]
            self.assertEqual(struct.pack("<f", M.f32(x)), struct.pack("<f", x))
        self.assertEqual(M.f32(struct.unpack("<f", struct.pack("<I", 0x3DCCCCCD))[0]), 0.1)
        with self.assertRaises(ValueError):
            M.f32(float("inf"))

    def test_version_matches_runtime(self):
        header = (ROOT / "game" / "include" / "mb" / "game" / "Level.h").read_text()
        match = re.search(r"kLevelJsonVersion\s*=\s*(\d+)", header)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1)), M.LEVEL_JSON_VERSION)

    def test_converted_json_carries_the_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            levels = pathlib.Path(tmp) / "Data" / "Levels"
            levels.mkdir(parents=True)
            (levels / "test.mb3").write_bytes(level_bytes())
            with contextlib.redirect_stdout(io.StringIO()):
                convert_assets.convert_levels(pathlib.Path(tmp))
            converted = json.loads((levels / "test.json").read_text())
        self.assertEqual(converted["version"], M.LEVEL_JSON_VERSION)
        self.assertEqual(converted["actors"][0]["shapes"][1]["radiusHeight"], [0.91, 9.8])

    def test_conversion_embeds_convex_hulls(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "Data" / "Levels").mkdir(parents=True)
            (root / "Data" / "NxMeshes").mkdir()
            (root / "Data" / "Levels" / "test.mb3").write_bytes(physics_level_bytes())
            (root / "Data" / "NxMeshes" / "Rock_ph.cnvx").write_bytes(hull_file(TETRA, [(0, 1, 2)]))
            with contextlib.redirect_stdout(io.StringIO()):
                convert_assets.convert_levels(root)
            converted = json.loads((root / "Data" / "Levels" / "test.json").read_text())
            self.assertEqual(converted["actors"][0]["shapes"][1]["hullPoints"][3], [0, 0, 4])
            (root / "Data" / "NxMeshes" / "Rock_ph.cnvx").unlink()
            with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
                convert_assets.convert_levels(root)

    def test_rejects_other_files(self):
        with self.assertRaises(ValueError):
            M.parse(b"not a level")


if __name__ == "__main__":
    unittest.main()
