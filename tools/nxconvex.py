#!/usr/bin/env python3
"""Read the convex-hull files in Data/NxMeshes/*.cnvx.

Each file is what the game's content exporter wrote for one PhysX convex shape,
not a cooked NXS stream:

    u32 vertexCount
    f32[3] vertex[vertexCount]
    u32 triangleCount                 optional: absent when the file ends after the vertices
    u32[3] triangle[triangleCount]    indices into the vertices

The vertices are in the space of the shape (the level's shape localPose places
them in the actor). The levels' convex shapes name these files in lowercase;
the files on disk keep their original capitalisation.

Usage: python3 tools/nxconvex.py file.cnvx...   prints the point count and bounds
"""
import pathlib
import struct
import sys


def parse(data):
    """Returns {"points": [[x, y, z], ...], "triangles": [[a, b, c], ...]}."""
    if len(data) < 4:
        raise ValueError("convex mesh file is empty")
    count = struct.unpack_from("<I", data, 0)[0]
    end = 4 + 12 * count
    if count == 0 or end > len(data):
        raise ValueError(f"convex mesh declares {count} vertices but holds {len(data)} bytes")
    points = [list(struct.unpack_from("<3f", data, 4 + 12 * i)) for i in range(count)]
    triangles = []
    if end < len(data):
        if end + 4 > len(data):
            raise ValueError("convex mesh ends inside its triangle count")
        tris = struct.unpack_from("<I", data, end)[0]
        if end + 4 + 12 * tris != len(data):
            raise ValueError(f"convex mesh declares {tris} triangles but {len(data) - end - 4} bytes follow")
        triangles = [list(struct.unpack_from("<3I", data, end + 4 + 12 * i)) for i in range(tris)]
        if any(i >= count for tri in triangles for i in tri):
            raise ValueError("convex mesh triangle refers to a vertex that does not exist")
    return {"points": points, "triangles": triangles}


class HullFolder:
    """The NxMeshes folder, looked up by file name without regard to case."""

    def __init__(self, folder):
        self.folder = pathlib.Path(folder)
        self.files = {p.name.lower(): p for p in self.folder.glob("*.cnvx")} if self.folder.is_dir() else {}
        self.cache = {}

    def points(self, name):
        key = name.lower()
        if key not in self.cache:
            path = self.files.get(key)
            if path is None:
                raise FileNotFoundError(f"convex mesh {name} not found in {self.folder}")
            self.cache[key] = parse(path.read_bytes())["points"]
        return self.cache[key]


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        hull = parse(pathlib.Path(arg).read_bytes())
        pts = hull["points"]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        print(f"{arg}: {len(pts)} points, {len(hull['triangles'])} triangles, "
              f"bounds {[round(v, 3) for v in lo]} .. {[round(v, 3) for v in hi]}")
