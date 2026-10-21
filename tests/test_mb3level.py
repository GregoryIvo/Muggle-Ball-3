#!/usr/bin/env python3
"""Round-trip a small synthetic .mb3 file through tools/mb3level.py."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import mb3level as M  # noqa: E402

IDENTITY_POSE = struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)


def string(s):
    b = s.encode()
    return struct.pack("<I", len(b)) + b


def actor_desc(position, has_object):
    desc = bytearray(M.ACTOR_DESC_SIZE)
    desc[:48] = struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, *position)
    struct.pack_into("<I", desc, 0x40, 0x0CCCD6A8 if has_object else 0)
    return bytes(desc)


def box_shape(half):
    raw = bytearray(M.SHAPE_DESC_SIZE[2])
    struct.pack_into("<I", raw, 0, 2)
    raw[4:52] = IDENTITY_POSE
    struct.pack_into("<3f", raw, len(raw) - 12, *half)
    return struct.pack("<I", 2) + bytes(raw)


def capsule_shape(radius, height):
    raw = bytearray(M.SHAPE_DESC_SIZE[3])
    struct.pack_into("<I", raw, 0, 3)
    raw[4:52] = IDENTITY_POSE
    struct.pack_into("<2fI", raw, len(raw) - 12, radius, height, 0)
    return struct.pack("<I", 3) + bytes(raw)


def board_actor():
    return (actor_desc((0, 0, 0), True) + b"\x00" + struct.pack("<I", 2) + box_shape((3, 22, 15))
            + capsule_shape(0.91, 9.8)
            + struct.pack("<III", 1, 2, M.FLAG_MATERIAL | M.FLAG_MESH)
            + string("boardmaterial") + string("boardsea.x"))


def segment_actor(x):
    return actor_desc((x, 0, 0), False) + b"\x01" + bytes(M.BODY_DESC_SIZE) + struct.pack("<I", 0)


def cloth_actor():
    # 2x2 cloth: the main actor plus 3 nested segment actors.
    return (actor_desc((5, 0, 0), True) + b"\x00" + struct.pack("<I", 0)
            + struct.pack("<III", 20, 9, 0) + struct.pack("<5I", 2, 2, 0, 0, 0)
            + b"".join(segment_actor(i) for i in range(3)))


def path():
    point = struct.pack("<12f", 1, 2, 3, 0, 0, 0, 1, 0, 0, 0, 1, 0)
    return (struct.pack("<I", 1) + string("track/shark") + struct.pack("<I", 7)
            + struct.pack("<I", 8) + b"\xff\x00" + struct.pack("<I", 1) + point)


def level_bytes():
    return (M.MAGIC + struct.pack("<3f", 0, -9.81, 0)
            + struct.pack("<I", 5)  # 2 top-level actors + 3 cloth segments
            + board_actor() + cloth_actor()
            + struct.pack("<I", 0)  # global parameter
            + struct.pack("<I", 1) + struct.pack("<III", 0, 1, 2) + bytes(M.JOINT_DESC_SIZE[2])
            + path()
            + struct.pack("<I", 0))  # path bindings


class Mb3LevelTest(unittest.TestCase):
    def test_parse(self):
        level = M.parse(level_bytes())
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

    def test_rejects_other_files(self):
        with self.assertRaises(ValueError):
            M.parse(b"not a level")


if __name__ == "__main__":
    unittest.main()
