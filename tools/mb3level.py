#!/usr/bin/env python3
"""Parse Magic Ball 3 level files (*.mb3, "serialize level ver 3.0") into JSON.

The format is the game's raw dump of PhysX 2.x descriptor structs plus a small
object header per actor. Layout recovered from the level loader in game32.img
(sub_472710 and the functions it calls).
"""
import json
import struct
import sys

MAGIC = b"Magic Ball 3 serialize level ver 3.0\0"

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
# Joint descriptor size by joint type.
JOINT_DESC_SIZE = {0: 0x68, 1: 0xA8, 2: 0x68, 3: 0xC8, 4: 0x68, 5: 0x68, 6: 0x80, 7: 0x9C, 8: 0x68}

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


def read_pose(raw):
    m = struct.unpack("<12f", raw[:48])
    return {"rotation": r(m[:9]), "position": r(m[9:12])}


def read_shape(rd):
    stype = rd.u32()
    raw = rd.take(SHAPE_DESC_SIZE[stype])
    shape = {"type": SHAPE_TYPES.get(stype, stype), "localPose": read_pose(raw[4:52])}
    if stype == 1:
        shape["radius"] = round(struct.unpack_from("<f", raw, SHAPE_DESC_SIZE[1] - 4)[0], 5)
    elif stype == 2:
        shape["halfExtents"] = r(struct.unpack_from("<3f", raw, SHAPE_DESC_SIZE[2] - 12))
    elif stype == 3:
        shape["radiusHeight"] = r(struct.unpack_from("<2f", raw, SHAPE_DESC_SIZE[3] - 8))
    if stype in (4, 5):
        shape["meshName"] = rd.string()
    shape["raw"] = raw.hex()
    return shape


def read_object(rd, otype):
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
        obj["cloth"]["segments"] = [read_actor(rd) for _ in range(max(w * h - 1, 0))]
        if flags & FLAG_CLOTH_ANCHOR:
            obj["cloth"]["anchor"] = {"index": rd.u32(), "offset": rd.f32s(3)}
    return obj


def read_actor(rd):
    start = rd.o
    raw = rd.take(ACTOR_DESC_SIZE)
    actor = {"offset": start, "pose": read_pose(raw)}
    user_data = struct.unpack_from("<I", raw, 0x40)[0]
    if rd.u8():
        actor["body"] = rd.take(BODY_DESC_SIZE).hex()
    actor["shapes"] = [read_shape(rd) for _ in range(rd.u32())]
    if user_data:
        actor["object"] = read_object(rd, rd.u32())
    return actor


def read_paths(rd):
    paths = []
    for _ in range(rd.u32()):
        p = {"name": rd.string(), "param": rd.u32(), "mask": rd.bits(), "points": []}
        for _ in range(rd.u32()):
            pos, tng, d, up = (rd.f32s(3) for _ in range(4))
            p["points"].append({"pos": r(pos), "tng": r(tng), "dir": r(d), "up": r(up)})
        paths.append(p)
    return paths


def read_path_bindings(rd):
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


def parse(data):
    if not data.startswith(MAGIC):
        raise ValueError("not a Magic Ball 3 level")
    rd = Reader(data)
    rd.take(len(MAGIC))
    level = {"gravity": r(rd.f32s(3))}
    # The stored actor count includes the segments of cloth objects, which are
    # read nested inside their cloth's record.
    remaining = rd.u32()
    level["actors"] = []
    while remaining:
        actor = read_actor(rd)
        level["actors"].append(actor)
        remaining -= 1 + len(actor.get("object", {}).get("cloth", {}).get("segments", []))
    level["globalParam"] = rd.u32()
    joints = []
    for _ in range(rd.u32()):
        a, b, jt = rd.u32(), rd.u32(), rd.u32()
        joints.append({"actorA": a, "actorB": b, "type": jt, "raw": rd.take(JOINT_DESC_SIZE[jt]).hex()})
    level["joints"] = joints
    level["paths"] = read_paths(rd)
    level["pathBindings"] = read_path_bindings(rd)
    level["trailingBytes"] = len(data) - rd.o
    return level


if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            lvl = parse(f.read())
        out = path.rsplit(".", 1)[0] + ".json"
        with open(out, "w") as f:
            json.dump(lvl, f, indent=1)
        print(f"{path}: {len(lvl['actors'])} actors, {len(lvl['joints'])} joints, "
              f"{len(lvl['paths'])} paths, trailing={lvl['trailingBytes']}")
