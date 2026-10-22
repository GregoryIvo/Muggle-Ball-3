#!/usr/bin/env python3
"""Parse Magic Ball 3 level files (*.mb3, "serialize level ver 3.0") into JSON.

The format is the game's raw dump of PhysX 2.x descriptor structs plus a small
object header per actor. Layout recovered from the level loader in game32.img
(sub_472710 and the functions it calls), and the fields of the descriptors
decoded from the data itself; docs/LEVEL_FORMAT.md has the details.
"""
import functools
import json
import pathlib
import struct
import sys

import nxconvex

MAGIC = b"Magic Ball 3 serialize level ver 3.0\0"

# Version of the JSON this tool writes, stored as the top-level "version". Raise it together with
# kLevelJsonVersion in game/include/mb/game/Level.h whenever files written before a change would
# be misread. A file without "version" is version 1.
#   1  no "version" key; the earliest files read capsule radius and height from the wrong bytes
#   2  capsule radius and height fixed; adds "version"
#   3  body, actor, shape and joint descriptors decoded; convex shapes carry their hull points
LEVEL_JSON_VERSION = 3

OBJECT_TYPES = {
    1: "board", 2: "bat", 3: "ball", 4: "brick", 5: "prize", 6: "shadowcaster",
    7: "background", 8: "light", 9: "bullet", 10: "wall", 11: "cannon",
    12: "firework", 13: "spring", 14: "volcano", 15: "teleportsrc",
    16: "teleportdest", 17: "generatorgood", 18: "generatorbad", 19: "tree",
    20: "cloth", 21: "firerocket", 22: "vibro", 23: "flicker", 24: "metall",
    25: "dynamite", 26: "train",
}
SHAPE_TYPES = {0: "plane", 1: "sphere", 2: "box", 3: "capsule", 4: "convex", 5: "mesh"}
# Bytes read after the shape type tag (the descriptor minus its vtable pointer).
SHAPE_DESC_SIZE = {0: 0x68, 1: 0x5C, 2: 0x64, 3: 0x64, 4: 0x60, 5: 0x60}
# NxShapeDesc's own fields end here; the geometry (radius, half extents, ...) follows.
SHAPE_GEOMETRY = 0x58
# Joint descriptor size by joint type.
JOINT_DESC_SIZE = {0: 0x68, 1: 0xA8, 2: 0x68, 3: 0xC8, 4: 0x68, 5: 0x68, 6: 0x80, 7: 0x9C, 8: 0x68}
JOINT_REVOLUTE = 1
JOINT_SPHERICAL = 3
# NxJointDesc's last word is NX_JF_COLLISION_ENABLED (1) and NX_JF_VISUALIZATION (2) in its low bits;
# the rest of the word is a stale heap pointer in every shipped level.
JOINT_FLAGS_MASK = 0x3

ACTOR_DESC_SIZE = 0x5C
BODY_DESC_SIZE = 0x78

FLAG_MATERIAL = 0x2
FLAG_MESH = 0x4
FLAG_FRAME = 0x8
FLAG_EXTRA = 0x200
FLAG_CLOTH_ANCHOR = 0x800


class Reader:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def take(self, n):
        if self.o + n > len(self.d):
            raise EOFError(f"read {n} at {self.o:#x} past end {len(self.d):#x}")
        b = self.d[self.o:self.o + n]
        self.o += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def f32s(self, n):
        return list(struct.unpack(f"<{n}f", self.take(4 * n)))

    def string(self):
        return self.take(self.u32()).decode("latin-1")

    def bits(self):
        nbits = self.u32()
        raw = self.take((nbits >> 3) + 1) if nbits else b""
        return {"count": nbits, "hex": raw.hex()}


def r(v):
    return [round(x, 5) for x in v]


@functools.lru_cache(maxsize=1 << 16)
def f32(x):
    """The shortest decimal that reads back as the same float32, so tiny inertias and FLT_MAX survive the JSON."""
    if x != x or x in (float("inf"), float("-inf")):
        raise ValueError("level holds a float that is not finite")
    for digits in (7, 8, 9):
        text = float("%.*g" % (digits, x))
        if struct.unpack("<f", struct.pack("<f", text))[0] == x:
            return text
    return x


def floats(raw, offset, n):
    return [f32(x) for x in struct.unpack_from(f"<{n}f", raw, offset)]


def real(raw, offset):
    return f32(struct.unpack_from("<f", raw, offset)[0])


def u32_at(raw, offset):
    return struct.unpack_from("<I", raw, offset)[0]


def u16_at(raw, offset):
    return struct.unpack_from("<H", raw, offset)[0]


def read_pose(raw):
    m = struct.unpack("<12f", raw[:48])
    return {"rotation": r(m[:9]), "position": r(m[9:12])}


def read_actor_desc(raw):
    """NxActorDesc minus its vtable, 0x5C bytes.

    0x00 globalPose, 0x30 body pointer (always null: the body follows separately), 0x34 density,
    0x38 flags (NxActorFlag), 0x3C group (u16, the upper half is 0xCCCC padding), 0x40 userData
    (non-zero: a game object follows), 0x44 name (null), 0x48 always 1, 0x4C the shape array
    (empty, its allocator word 0xCCCCCCCC).
    """
    return {"pose": read_pose(raw), "density": real(raw, 0x34), "actorFlags": u32_at(raw, 0x38),
            "group": u16_at(raw, 0x3C)}


def read_body(raw):
    """NxBodyDesc, 0x78 bytes: the actor's mass properties and initial motion."""
    return {
        "massLocalPose": {"rotation": floats(raw, 0x00, 9), "position": floats(raw, 0x24, 3)},
        "massSpaceInertia": floats(raw, 0x30, 3),
        "mass": real(raw, 0x3C),
        "linearVelocity": floats(raw, 0x40, 3),
        "angularVelocity": floats(raw, 0x4C, 3),
        "wakeUpCounter": real(raw, 0x58),
        "linearDamping": real(raw, 0x5C),
        "angularDamping": real(raw, 0x60),
        "maxAngularVelocity": real(raw, 0x64),
        "flags": u32_at(raw, 0x68),
        "sleepLinearVelocity": real(raw, 0x6C),
        "sleepAngularVelocity": real(raw, 0x70),
        "solverIterationCount": u32_at(raw, 0x74),
        "raw": raw.hex(),
    }


def read_shape(rd, hulls):
    stype = rd.u32()
    raw = rd.take(SHAPE_DESC_SIZE[stype])
    # NxShapeDesc: 0x00 type, 0x04 localPose, 0x34 shapeFlags (NxShapeFlag), 0x38 group (u16) and
    # materialIndex (u16), 0x3C skinWidth, 0x40 userData and 0x44 name (null), 0x48 groupsMask (4 x u32, zero).
    shape = {
        "type": SHAPE_TYPES.get(stype, stype),
        "localPose": read_pose(raw[4:52]),
        "shapeFlags": u32_at(raw, 0x34),
        "group": u16_at(raw, 0x38),
        "materialIndex": u16_at(raw, 0x3A),
        "skinWidth": real(raw, 0x3C),
    }
    if stype == 1:
        shape["radius"] = round(struct.unpack_from("<f", raw, SHAPE_GEOMETRY)[0], 5)
    elif stype == 2:
        shape["halfExtents"] = r(struct.unpack_from("<3f", raw, SHAPE_GEOMETRY))
    elif stype == 3:
        # NxCapsuleShapeDesc ends with radius, height and a u32 (flags, always 0 in the shipped levels).
        shape["radiusHeight"] = r(struct.unpack_from("<2f", raw, SHAPE_GEOMETRY))
    if stype in (4, 5):
        shape["meshName"] = rd.string()
        if stype == 4 and hulls is not None:
            shape["hullPoints"] = [r(p) for p in hulls.points(shape["meshName"])]
    shape["raw"] = raw.hex()
    return shape


def read_object(rd, otype, hulls):
    obj = {"type": OBJECT_TYPES.get(otype, otype), "id": rd.u32()}
    flags = rd.u32()
    obj["flags"] = flags
    if flags & FLAG_MATERIAL:
        obj["material"] = rd.string()
    if flags & FLAG_MESH:
        obj["mesh"] = rd.string()
    if flags & FLAG_FRAME:
        obj["frame"] = rd.string()
    if flags & FLAG_EXTRA:
        obj["extra"] = rd.take(0x68).hex()

    name = obj["type"]
    if name == "prize":
        obj["prize"] = rd.string()
    elif name == "dynamite":
        obj["target"] = rd.string()
        obj["param"] = rd.u32()
    elif name == "shadowcaster":
        obj["param"] = rd.u32()
    elif name == "flicker":
        obj["names"] = [rd.string() for _ in range(rd.u32())]
    elif name == "cloth":
        w, h = rd.u32(), rd.u32()
        obj["cloth"] = {"width": w, "height": h, "params": [rd.u32() for _ in range(3)]}
        obj["cloth"]["segments"] = [read_actor(rd, hulls) for _ in range(max(w * h - 1, 0))]
        if flags & FLAG_CLOTH_ANCHOR:
            obj["cloth"]["anchor"] = {"index": rd.u32(), "offset": rd.f32s(3)}
    return obj


def read_actor(rd, hulls):
    start = rd.o
    raw = rd.take(ACTOR_DESC_SIZE)
    actor = {"offset": start, **read_actor_desc(raw)}
    user_data = u32_at(raw, 0x40)
    if rd.u8():
        actor["body"] = read_body(rd.take(BODY_DESC_SIZE))
    actor["shapes"] = [read_shape(rd, hulls) for _ in range(rd.u32())]
    if user_data:
        actor["object"] = read_object(rd, rd.u32(), hulls)
    return actor


def limit(raw, offset):
    """NxJointLimitDesc."""
    return {"value": real(raw, offset), "restitution": real(raw, offset + 4), "hardness": real(raw, offset + 8)}


def limit_pair(raw, offset):
    return {"low": limit(raw, offset), "high": limit(raw, offset + 12)}


def spring(raw, offset):
    """NxSpringDesc."""
    return {"spring": real(raw, offset), "damper": real(raw, offset + 4), "targetValue": real(raw, offset + 8)}


def read_joint_desc(jtype, raw):
    """NxJointDesc and the revolute / spherical part that follows it. Offsets are into the descriptor."""
    joint = {
        "localNormal": [floats(raw, 0x0C, 3), floats(raw, 0x18, 3)],
        "localAxis": [floats(raw, 0x24, 3), floats(raw, 0x30, 3)],
        "localAnchor": [floats(raw, 0x3C, 3), floats(raw, 0x48, 3)],
        "maxForce": real(raw, 0x54),
        "maxTorque": real(raw, 0x58),
        "jointFlags": u32_at(raw, 0x64) & JOINT_FLAGS_MASK,
    }
    if jtype == JOINT_REVOLUTE:
        joint.update({
            "limit": limit_pair(raw, 0x68),
            "motor": {"velTarget": real(raw, 0x80), "maxForce": real(raw, 0x84),
                      "freeSpin": u32_at(raw, 0x88) != 0},
            "spring": spring(raw, 0x8C),
            "projectionDistance": real(raw, 0x98),
            "projectionAngle": real(raw, 0x9C),
            "flags": u32_at(raw, 0xA0),
            "projectionMode": u32_at(raw, 0xA4),
        })
    elif jtype == JOINT_SPHERICAL:
        joint.update({
            "swingAxis": floats(raw, 0x68, 3),
            "projectionDistance": real(raw, 0x74),
            "twistLimit": limit_pair(raw, 0x78),
            "swingLimit": limit(raw, 0x90),
            "twistSpring": spring(raw, 0x9C),
            "swingSpring": spring(raw, 0xA8),
            "jointSpring": spring(raw, 0xB4),
            "flags": u32_at(raw, 0xC0),
            "projectionMode": u32_at(raw, 0xC4),
        })
    return joint


def read_joint(rd):
    a, b, jtype = rd.u32(), rd.u32(), rd.u32()
    raw = rd.take(JOINT_DESC_SIZE[jtype])
    return {"actorA": a, "actorB": b, "type": jtype, **read_joint_desc(jtype, raw), "raw": raw.hex()}


def read_paths(rd):
    # param: 1 closed loop, 0 open; mask bit 0: the points carry tangents (docs/LEVEL_FORMAT.md, "Path motion")
    paths = []
    for _ in range(rd.u32()):
        p = {"name": rd.string(), "param": rd.u32(), "mask": rd.bits(), "points": []}
        for _ in range(rd.u32()):
            pos, tng, d, up = (rd.f32s(3) for _ in range(4))
            p["points"].append({"pos": r(pos), "tng": r(tng), "dir": r(d), "up": r(up)})
        paths.append(p)
    return paths


def read_path_bindings(rd):
    # A track's param is the bits of an f32, the speed in world units per millisecond; a key is the position, direction
    # and up of the object it names (a, b, c) and its object id.
    out = []
    for _ in range(rd.u32()):
        b = {"name": rd.string(), "tracks": []}
        for _ in range(rd.u32()):
            nkeys = rd.u32()
            t = {"index": rd.u32(), "param": rd.u32(), "mask": rd.bits(), "keys": []}
            for _ in range(nkeys):
                a, c, e = rd.f32s(3), rd.f32s(3), rd.f32s(3)
                t["keys"].append({"a": r(a), "b": r(c), "c": r(e), "object": rd.u32()})
            b["tracks"].append(t)
        out.append(b)
    return out


def actor_references(actors):
    """Maps each value a joint can name an actor by to that actor.

    A game object's actor is named by the object's id. The segments of a cloth with id N are named
    N + 1 .. N + width * height - 1 (the cloth's own actor is segment 0, id N). When two names clash an
    object id wins over a segment, and the earlier object over the later. The value 0 names no actor:
    it is the world, which a joint attaches to when it holds no actor.
    """
    refs = {}
    for actor in actors:
        if "object" in actor:
            refs.setdefault(actor["object"]["id"], actor)
    for actor in actors:
        obj = actor.get("object")
        if obj and "cloth" in obj:
            for k, segment in enumerate(obj["cloth"]["segments"], 1):
                refs.setdefault(obj["id"] + k, segment)
    return refs


def check_joint_references(level):
    refs = actor_references(level["actors"])
    for n, joint in enumerate(level["joints"]):
        if joint["actorB"] == 0:
            raise ValueError(f"joint {n} is attached to the world on both sides")
        for key in ("actorA", "actorB"):
            if joint[key] and joint[key] not in refs:
                raise ValueError(f"joint {n} {key} {joint[key]} names no actor")


def parse(data, hulls=None):
    """Parses one level. `hulls` (nxconvex.HullFolder) supplies the points of convex shapes; without it they are left out."""
    if not data.startswith(MAGIC):
        raise ValueError("not a Magic Ball 3 level")
    rd = Reader(data)
    rd.take(len(MAGIC))
    level = {"version": LEVEL_JSON_VERSION, "gravity": r(rd.f32s(3))}
    # The stored actor count includes the segments of cloth objects, which are
    # read nested inside their cloth's record.
    remaining = rd.u32()
    level["actors"] = []
    while remaining:
        actor = read_actor(rd, hulls)
        level["actors"].append(actor)
        remaining -= 1 + len(actor.get("object", {}).get("cloth", {}).get("segments", []))
    level["globalParam"] = rd.u32()
    level["joints"] = [read_joint(rd) for _ in range(rd.u32())]
    check_joint_references(level)
    level["paths"] = read_paths(rd)
    level["pathBindings"] = read_path_bindings(rd)
    level["trailingBytes"] = len(data) - rd.o
    return level


if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        # Levels live in Data/Levels, the convex meshes in Data/NxMeshes next to it.
        hulls = nxconvex.HullFolder(pathlib.Path(path).resolve().parent.parent / "NxMeshes")
        try:
            lvl = parse(data, hulls)
        except FileNotFoundError as e:
            sys.exit(f"{path}: {e}")
        out = path.rsplit(".", 1)[0] + ".json"
        with open(out, "w") as f:
            json.dump(lvl, f, indent=1)
        print(f"{path}: {len(lvl['actors'])} actors, {len(lvl['joints'])} joints, "
              f"{len(lvl['paths'])} paths, trailing={lvl['trailingBytes']}")
