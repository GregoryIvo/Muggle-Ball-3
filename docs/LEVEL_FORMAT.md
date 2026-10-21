# `.mb3` level format ("Magic Ball 3 serialize level ver 3.0")

Little-endian. Recovered from the level loader in `game32.img` (addresses
refer to the unpacked image at base 0x400000). `tools/mb3level.py` implements
it; all 83 shipped levels parse to the last byte.

```
magic        char[37]   "Magic Ball 3 serialize level ver 3.0\0"
gravity      f32[3]
actorCount   u32        includes the segment actors nested inside cloth objects
actor[...]              see Actor
globalParam  u32        stored to a global at 0x52563c, meaning unknown
jointCount   u32
joint[...]   u32 actorA, u32 actorB, u32 type, desc[size by type]
paths        see Paths
bindings     see Path bindings
```

Joint descriptor size by type: 1 → 0xA8, 3 → 0xC8, 6 → 0x80, 7 → 0x9C, others → 0x68.

## Actor (sub_46d740)

```
actorDesc    u8[0x5C]   PhysX 2.x NxActorDesc without its vtable.
                        +0x00 NxMat33 rotation (row-major), +0x24 position,
                        +0x40 userData: non-zero means a game object follows
hasBody      u8
bodyDesc     u8[0x78]   only if hasBody (NxBodyDesc)
shapeCount   u32
shape[...]   u32 type, desc
object       only if userData != 0, see Game object
```

Shape type, then descriptor size (sub_46cd10): 0 plane 0x68, 1 sphere 0x5C,
2 box 0x64, 3 capsule 0x64, 4 convex 0x60 + string, 5 mesh 0x60 + string.
Each descriptor starts with its type again, then the local pose (NxMat34).
Box half-extents are the last 12 bytes, sphere radius the last 4. A capsule
ends with radius, height and a u32 that is 0 in every shipped level (flags),
so its radius and height are the 8 bytes before the last 4. The capsule runs
along the shape's local +Y; `height` is the distance between the centres of
its two caps, so it spans y in [-height/2 - radius, height/2 + radius].

`string` = u32 length + bytes (no terminator).

## Game object

```
type    u32   1 board, 2 bat, 3 ball, 4 brick, 5 prize, 6 shadowcaster,
              7 background, 8 light, 9 bullet, 10 wall, 11 cannon, 12 firework,
              13 spring, 14 volcano, 15 teleportsrc, 16 teleportdest,
              17 generatorgood, 18 generatorbad, 19 tree, 20 cloth,
              21 firerocket, 22 vibro, 23 flicker, 24 metall, 25 dynamite, 26 train
id      u32
flags   u32
        0x002 → string material
        0x004 → string mesh
        0x008 → string frame
        0x200 → u8[0x68] extra
```

Type-specific extras after the base record:

| Type | Extra |
|---|---|
| prize | string prize |
| dynamite | string target, u32 |
| shadowcaster | u32 |
| flicker | u32 n, n × string |
| cloth | u32 width, u32 height, u32 × 3, then (width × height − 1) nested Actors; if flags & 0x800: u32 + f32[3] |

## Paths (sub_449e80)

```
count u32
  name string, param u32, mask bitset, pointCount u32,
  point[...] = pos f32[3], tangent f32[3], dir f32[3], up f32[3]
```

`bitset` = u32 bitCount + (bitCount / 8 + 1) bytes.

## Path bindings (sub_448de0)

```
count u32
  name string, trackCount u32
  track[...] = keyCount u32, index u32, param u32, mask bitset,
               key[keyCount] = f32[3] × 3, u32 object
```
