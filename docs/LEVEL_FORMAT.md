# `.mb3` level format ("Magic Ball 3 serialize level ver 3.0")

Little-endian. Recovered from the level loader in `game32.img` (addresses
refer to the unpacked image at base 0x400000); the fields inside the PhysX
descriptors were decoded from the data of all 83 shipped levels (13,930 actors,
4,895 joints). `tools/mb3level.py` implements it; every level parses to the
last byte and every joint resolves to its actors. The runtime reads the JSON
that tool writes, described under "Converted JSON" at the end.

```
magic        char[37]   "Magic Ball 3 serialize level ver 3.0\0"
gravity      f32[3]
actorCount   u32        includes the segment actors nested inside cloth objects
actor[...]              see Actor
globalParam  u32        stored to a global at 0x52563c: the editor's object id counter, see Joint actor references
jointCount   u32
joint[...]   u32 actorA, u32 actorB, u32 type, desc[size by type]
paths        see Paths
bindings     see Path bindings
```

Joint descriptor size by type: 1 → 0xA8, 3 → 0xC8, 6 → 0x80, 7 → 0x9C, others → 0x68.

## Actor (sub_46d740)

```
actorDesc    u8[0x5C]   PhysX 2.x NxActorDesc without its vtable, see Actor descriptor
hasBody      u8
bodyDesc     u8[0x78]   only if hasBody (NxBodyDesc), see Body descriptor
shapeCount   u32
shape[...]   u32 type, desc
object       only if userData != 0, see Game object
```

Every actor in the shipped levels has a body. The PhysX descriptors are dumps of
in-memory structs, so a few words are debug-heap fill (`0xCCCCCCCC`) or stale
pointers from the editor session that saved the level; those are noted below
and carry no meaning. Poses are PhysX's: an `NxMat33` stored row-major for
column vectors (`world = R * p + t`), see `Mat4::fromPhysX`.

Saved poses are not all rigid: 5 actors and 34 shape local poses have a
rotation whose rows or columns are not unit length (0.93 to 1.21). Orthonormalise
a rotation before handing it to a physics engine. The levels were saved from a
running simulation: bodies have small velocities (see Body descriptor) and
joints can be stretched (see Joints).

### Actor descriptor (0x5C bytes)

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x00 | `globalPose`: rotation `NxMat33` (9 × f32), then position at 0x24 (3 × f32) | |
| 0x30 | `body` pointer | null (the body follows separately) |
| 0x34 | `density` f32 | 0 for 13,913 actors, 2.0 for 17 (all of which also have a mass, which wins) |
| 0x38 | `flags` u32, `NxActorFlag` | 0 everywhere: no actor disables its collisions or gravity this way |
| 0x3C | `group` u16 (the upper half is 0xCCCC padding) | 2, 4, 5, 7 or 12, see Collision groups |
| 0x40 | `userData` | non-zero: a game object follows. The value is an editor heap address with no other meaning |
| 0x44 | `name` | null |
| 0x48 | u32 | always 1, meaning unknown |
| 0x4C | shape array (`NxArray`: 3 pointers, then an allocator word 0xCCCCCCCC) | empty |

### Body descriptor (0x78 bytes)

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x00 | `massLocalPose` `NxMat34`: rotation (9 × f32), position at 0x24 | the centre-of-mass frame relative to the actor; the inertia is diagonal in it. Rotation is the identity for 7,678 bodies, the position is non-zero for 13,617 |
| 0x30 | `massSpaceInertia` vec3 | |
| 0x3C | `mass` f32 | never 0: 0.004 to 1.04e5 |
| 0x40 | `linearVelocity` vec3 | the level was saved mid-simulation: median 4e-4, 99th percentile 0.19, at most 10.4 |
| 0x4C | `angularVelocity` vec3 | median 2e-4, 99th percentile 0.08, at most 3.9 |
| 0x58 | `wakeUpCounter` f32 | 0.4 for 13,647 bodies, 0 for 283 (asleep when saved; PhysX counts 0 as asleep) |
| 0x5C | `linearDamping` | 0 |
| 0x60 | `angularDamping` | 0.05 (12,513), 0.8 (952), 0.5 (465) |
| 0x64 | `maxAngularVelocity` | 7 |
| 0x68 | `flags` u32, `NxBodyFlag` | 0x100 (visualization, set on all), plus the flags below |
| 0x6C | `sleepLinearVelocity` | 0.15 |
| 0x70 | `sleepAngularVelocity` | 0.14 |
| 0x74 | `solverIterationCount` u32 | 4 |

`NxBodyFlag`: 0x01 disable gravity, 0x02 / 0x04 / 0x08 frozen position x / y / z,
0x10 / 0x20 / 0x40 frozen rotation x / y / z, 0x80 kinematic, 0x100
visualization. The combinations that occur (with 0x100 removed): none (dynamic),
0x80 (kinematic), 0x70 (dynamic, rotation frozen: trees, cannons, fireworks,
generators, some bricks), 0xF0 (kinematic, rotation frozen), 0x01 (disable
gravity: background markers), 0x7F (marker spheres frozen in place: lights and
shadow casters), 0xFF (the board: kinematic and everything frozen).

`mass` is what the editor computed from the actor's shapes: their volume times
the `density` of the object's material in `materials.json` (for non-overlapping
box shapes `mass / volume` equals that density exactly, for example 10, 5 or
50), so no body needs the actor `density`.

### Shapes

Shape type, then descriptor size (sub_46cd10): 0 plane 0x68, 1 sphere 0x5C,
2 box 0x64, 3 capsule 0x64, 4 convex 0x60 + string, 5 mesh 0x60 + string.
The shipped levels hold 16,645 boxes, 2,537 capsules, 1,192 spheres and 16
convex shapes; no plane or triangle mesh.

NxShapeDesc (the first 0x58 bytes, the same for every type):

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x00 | type u32 (repeats the tag before the descriptor) | |
| 0x04 | `localPose` `NxMat34`: rotation (9 × f32), position at 0x28 | relative to the actor |
| 0x34 | `shapeFlags` u32, `NxShapeFlag` | 8 (visualization) for 20,378 shapes; 15 (visualization + trigger on enter, leave and stay) for the 12 trigger boxes of the teleport sources |
| 0x38 | `group` u16 | collision group, see Collision groups |
| 0x3A | `materialIndex` u16 | a handle that only means something inside its own level, see Physics materials |
| 0x3C | `skinWidth` f32 | 0.005 everywhere |
| 0x40 | `userData`, 0x44 `name` | null |
| 0x48 | `groupsMask`, 4 × u32 | 0 |

These add up to exactly 0x58 bytes with no room for a shape `mass` or `density`,
so the levels have none; the body's mass is used. The geometry follows:

- box: half-extents, 3 × f32 (the last 12 bytes);
- sphere: radius (the last 4 bytes);
- capsule: radius, height and a u32 (flags, 0 in every shipped level), so
  radius and height are the 8 bytes before the last 4. The capsule runs along
  the shape's local +Y; `height` is the distance between the centres of its two
  caps, so it spans y in [-height/2 - radius, height/2 + radius];
- convex, mesh: a mesh pointer (a stale heap address) and `meshFlags` (0), then
  the mesh name as a string. A convex's name refers to a file in
  `Data/NxMeshes`, see Convex meshes.

`string` = u32 length + bytes (no terminator).

### Convex meshes

The 16 convex shapes (rocks `rock-06.x`, the octopus `osminog.x`, the roof cone
of `mill.x`) name `Data/NxMeshes/*.cnvx`. The names in the levels are lower
case, the files keep their capital letters, so match them case-insensitively.
Despite the PhysX origin these are not cooked streams but a plain dump written
by the content exporter (`tools/nxconvex.py`):

```
vertexCount    u32
vertex[...]    f32[3]
triangleCount  u32          absent when the file ends after the vertices (the mill's cone)
triangle[...]  u32[3]       indices into the vertices
```

| File | Points | Triangles | Bounds (min .. max) | Shape `localPose` position | Used by |
|---|---|---|---|---|---|
| `Rock-06_ph_Convex1.cnvx` | 8 | 12 | (-1.665, -2.608, -1.036) .. (1.665, 2.608, 1.036) | (-1.032, 9.851, 0) | 10 shapes of `rock-06.x` |
| `Osminog_ph_Object04.cnvx` | 30 | 56 | (-3.155, -2.136, -2.375) .. (3.155, 2.136, 2.375) | (-0.972, 15.996, -0.225) | 2 shapes of `osminog.x` |
| `Melnica01_ph_Cone01.cnvx` | 10 | none | (-2.980, 0, -2.980) .. (2.980, 6.105, 2.980) | (0, 14.648, 0) | 4 shapes of `mill.x` |

The points are in the shape's own space; the shape's `localPose` places them in
the actor. Adding the pose gives the bounding box of the part of the model the
shape belongs to, to within 0.01 (checked against the `.glb` meshes: the cone
matches the mill's roof `Krisha` at y 14.648 to 20.756, the rock's hull matches
its top mesh `Box01`, the octopus's matches `Object03`). A convex shape is the
convex hull of its points, so a physics engine can build it from them directly.
The conversion embeds the points in each convex shape as `hullPoints`, so the
runtime reads no `.cnvx`.

### Collision groups

The file stores group numbers but no filtering: no pair flags, no groups mask
bits (all zero), no `NX_AF_DISABLE_COLLISION` or `NX_SF_DISABLE_COLLISION`, and
joint flag bit 0 (collision between jointed actors) is clear everywhere. Which
groups collide with which was set by the game's code (PhysX's
`setGroupCollisionFlag`), which the level does not contain. What the data shows:

| Group | Holds | Evidence |
|---|---|---|
| 2 | everything that plays: bricks, cloth, cannons, fireworks, generators, springs, volcanoes, teleports, trees, dynamite, flickers, vibros | the default: every shape of these objects |
| 4 | the board (76 levels) | its floor and rails |
| 7 | the board of 7 levels: `knight-castle`, `knight-eggs`, `knight-river`, `knight-zoo`, `cowboys-priehali`, `piratos-birthday`, `piratos-ostrovok` | the same shapes as group 4. These are also the 7 levels whose `materialIndex` values follow the `materials.json` order |
| 11 | one box on every board: half-extents (3, 22, 15) at (0, 15, -17), a strip 6 wide and 30 deep (z -32 to -2) at the player's end | probably the bat's lane; it touches no brick in any level |
| 12 | `metall` objects (452 of them) | |
| 5 | `background`, `light`, `shadowcaster`: tiny marker spheres, frozen in place | they sit inside the board in the saved state (up to 2.1 deep in groups 4, 4.2 in group 11), so they cannot have collided with them |

Measured on the saved poses (shapes of different actors, jointed pairs left
out), pairs overlapping by more than 0.05: group 2 with 2 in 591 of 60,056 near
pairs (up to 0.69 deep), 2 with 4 in 500 of 114,041 (up to 1.36), 2 with 7 in 83
of 10,714, 4 with 5 in 33 of 179 and 5 with 11 in 8 of 26; group 12 never
overlaps anything and group 11 only group 5. The overlaps among groups 2, 4 and 7
are rare (under 1% of near pairs) and shallow, the settling of a running
simulation, so the data shows none of those pairs excluded; group 5 is the
one that plainly overlaps solid shapes, and a physics rebuild should keep it out
of all collisions. The ball, bat and bullets are created by the game and are not
in the levels.

### Physics materials

A shape's `materialIndex` is not an index into `materials.json`. Within one level
it is a consistent handle: every shape of an object, and of its cloth's
segments, shares one value, and index 0 is the default material of objects with
no material. Across levels the same material gets different numbers
(`castle.jpg` is 120 in `knight-castle` and 129 in most others), and in 3 levels
(`Piratos-Logovo`,
`piratos-bad-night`, `piratos-po-volnam`) a cloth material shares its number
with a brick material. In the 7 levels listed under group 7,
`materialIndex - 1` equals the material's position in `materials.json` for
every shape; in the others it does so for some. To find a shape's friction and
restitution use the owning object's `material` name (for a cloth segment, the
cloth's) and look it up in `materials.json`, which carries `physics` and
`density`.

## Game object

```
type    u32   1 board, 2 bat, 3 ball, 4 brick, 5 prize, 6 shadowcaster,
              7 background, 8 light, 9 bullet, 10 wall, 11 cannon, 12 firework,
              13 spring, 14 volcano, 15 teleportsrc, 16 teleportdest,
              17 generatorgood, 18 generatorbad, 19 tree, 20 cloth,
              21 firerocket, 22 vibro, 23 flicker, 24 metall, 25 dynamite, 26 train
id      u32
flags   u32
        0x001 → set on exactly the 35 objects that follow a path (see Path motion); the binding names them by id
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

## Joints

```
jointCount   u32
joint[...]   u32 actorA, u32 actorB, u32 type, desc[size by type]
```

`type` is PhysX's `NxJointType`: 0 prismatic, 1 revolute, 2 cylindrical, 3
spherical, 4 point-on-line, 5 point-in-plane, 6 distance, 7 pulley, 8 fixed. The
shipped levels hold 4,670 fixed, 118 revolute and 107 spherical joints. Every
descriptor starts with the `NxJointDesc` base; revolute and spherical joints add
their own fields. The other types are only kept as bytes.

### Joint descriptor (`NxJointDesc`, 0x68 bytes)

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x00 | `type` u32 (repeats the tag before the descriptor) | |
| 0x04, 0x08 | `actor[2]` pointers | stale editor heap addresses (null for the world); use `actorA` / `actorB` instead |
| 0x0C, 0x18 | `localNormal[2]` vec3 | |
| 0x24, 0x30 | `localAxis[2]` vec3 | |
| 0x3C, 0x48 | `localAnchor[2]` vec3 | |
| 0x54 | `maxForce` f32 | break threshold; FLT_MAX (0x7F7FFFFF) means unbreakable: 743 joints, the other 4,152 can break |
| 0x58 | `maxTorque` f32 | |
| 0x5C | `userData`, 0x60 `name` | null |
| 0x64 | `jointFlags` | the word is a stale heap pointer in every joint, so only its low two bits are read as flags: `NX_JF_COLLISION_ENABLED` (1) and `NX_JF_VISUALIZATION` (2). Bit 0 is clear in all 4,895 joints, so jointed actors do not collide with each other (if the word is flags at all) |

Each of the two actors has its own frame for the joint, in that actor's local
space: an anchor point, the axis and a normal perpendicular to it. When
`actorA` is the world (0) frame 0 is in world space. In the saved poses the two
anchors coincide to within 0.05 for 4,435 joints, are up to 0.3 apart for 415,
up to 1 for 37 and up to 11.3 for 8 (a chain in `Piratos-Kartejniki`): the
pieces had moved or stretched the joint before the level was saved. 193 joints
have both anchors at (0, 0, 0), the origins of their actors. The axes agree to
within 5 degrees in world space for all but 12 joints.

`maxForce` / `maxTorque` as stored: fixed joints 500 / 500 (1,234), 50 / 50
(1,086), 100 / 100 (863), FLT_MAX (663) and 29 further combinations (0.3 up to
2,000, 500,000 for 8 joints; 500 / 50 for 33 and 800 / 500 for 3 are the only
ones where the two differ); revolute FLT_MAX (44), 500 / 500 (28), 80 / 50 (9),
150, 10 and 400 (8 each), 500,000 (7), 100 (6); spherical 100 / 100 (71) and
FLT_MAX (36).

### Revolute joint (type 1, 0xA8 bytes: the base, then 64 bytes)

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x68 | `limit` low, then high at 0x74: each `NxJointLimitDesc` `{value, restitution, hardness}` | `{0, 0, 1}` both: never enabled |
| 0x80 | `motor` `{velTarget, maxForce, freeSpin}` (12 bytes) | a default `{FLT_MAX, 0, 0}` unless enabled, see below |
| 0x8C | `spring` `{spring, damper, targetValue}` | all 0 |
| 0x98 | `projectionDistance` | 1 |
| 0x9C | `projectionAngle` | 0.0872 (5 degrees) |
| 0xA0 | `flags` u32 | `NX_RJF_LIMIT_ENABLED` 1, `MOTOR_ENABLED` 2, `SPRING_ENABLED` 4: 0 for 109 joints, 2 for 9 |
| 0xA4 | `projectionMode` | 0 (none) |

The 118 revolute joints connect two pieces of one model (carts `telega.x`,
`c-telega.x`, `horse-telega.x`, `kareta.x`, the pirate ship, windmills
`mill.x`, the ferris wheel `koleso-obzora.x`, `kotel.x`, `kachela.x`, houses).
Nine have a motor: in each of four windmill levels the sails turn about z at
2 rad/s and the roof about y at 0.5 rad/s (motor `maxForce` 100, joint
thresholds 150), and the ferris wheel in `village-park` turns at 3 rad/s (motor
`maxForce` 80, `freeSpin` set, joint thresholds 500,000). None has a limit or a
spring. A positive `velTarget` turns actor A forwards and actor B backwards about the
axis: in the saved velocities of all nine, `(wA - wB) . axis` (the axis from A's
pose and `localAxis[0]`) equals `+velTarget` (0.500 and 1.997 in `village-dinner`; the
wheel is still spinning up, +0.136 towards +3).

### Spherical joint (type 3, 0xC8 bytes: the base, then 96 bytes)

| Offset | Field | In the shipped levels |
|---|---|---|
| 0x68 | `swingAxis` vec3 | (0, 0, 1) |
| 0x74 | `projectionDistance` | 1 |
| 0x78 | `twistLimit` low, then high at 0x84: `{value, restitution, hardness}` | `{0, 0, 1}` |
| 0x90 | `swingLimit` `{value, restitution, hardness}` | `{0, 0, 1}` |
| 0x9C | `twistSpring`, 0xA8 `swingSpring`, 0xB4 `jointSpring`: `{spring, damper, targetValue}` | all 0 |
| 0xC0 | `flags` u32 | `NX_SJF_TWIST_LIMIT_ENABLED` 1, `SWING_LIMIT_ENABLED` 2, `TWIST_SPRING_ENABLED` 4, `SWING_SPRING_ENABLED` 8, `JOINT_SPRING_ENABLED` 16: always 0 |
| 0xC4 | `projectionMode` | 0 |

So every spherical joint is a free ball joint: 32 hang the `streetlight.x`
lamps from their posts, 75 attach the corners of a cloth to a ship, a boat, a
plot or a serf (`pirateship.x`, `boat-water.x`, `plot.x`, `serf.x`).

Every spherical joint holds the same defaults in its limits and springs, so the
data cannot tell the three springs, or the twist limit from the swing limit,
apart; the order given follows the PhysX 2.x descriptor. The revolute part and
the spherical `swingAxis`, `projectionDistance` and `flags` are fixed by the data
(the revolute motor's three values and its nine users show the field order).

Fixed joints join pieces of one compound model to each other (`fence.x` to
`fence-mirror.x`, the `castle-1.x` pieces, cacti, houses, ships), a piece to the
world (570: trees, cacti, rocks, fences, `metall-*` fences and boxes, the islands
`island1.x` / `island3.x`, springs, volcanoes, the board) and a tree to its fruit
(`tree.x` to `apple-tree.x`).

### Joint actor references

`actorA` and `actorB` (the two u32 before each descriptor) are not actor
indexes. A value names an actor like this:

1. **0 is the world**: the joint is attached to the fixed world, not to an actor.
   Only `actorA` is ever 0 (570 joints, all with a null pointer in the
   descriptor); `actorB` always names an actor.
2. Otherwise, if a game object has `id` equal to the value, the value names that
   object's actor. 9,156 of the 9,790 references are of this kind.
3. Otherwise the value names a **cloth segment**: a cloth object with id N
   whose grid is `width × height` owns the ids N .. N + width × height − 1,
   in row-major order of the grid. N itself is the cloth object's own actor
   (segment 0) and N + k, k ≥ 1, is the k-th nested segment actor
   (`cloth.segments[k - 1]`). 64 references are of this kind, joining the
   corners of a sail to a mast or a boat; for example the 5 × 6 cloth with id 47
   in `Piratos-Chyu-u-dishe` has its four corners at 47, 51, 72 and 76.

An object id wins over a cloth segment id when they clash, and the earlier
object over a later one with the same id (`cowboys-v-tualet` has two objects
with id 87, a background and a dynamite, which no joint uses). No clash
involves a joint reference in the shipped levels. Every reference of all 4,895
joints resolves under these rules, and the descriptor pointers confirm it: within a
level each pointer value belongs to exactly one resolved actor and vice versa.
`tools/mb3level.py` refuses a level where a reference does not resolve
(`actor_references`).

`globalParam` is the editor's id counter: the highest id it had handed out when
saving, cloth segment ids included. It equals the highest id in the file in 50
levels and is a few above it in 32 (objects were deleted); `cowboys-v-tualet`,
with its duplicate id, is the one level where it is lower than an id in use.

## Paths (sub_449e80)

```
count u32
  name string, param u32, mask bitset, pointCount u32,
  point[...] = pos f32[3], tangent f32[3], dir f32[3], up f32[3]
```

`bitset` = u32 bitCount + (bitCount / 8 + 1) bytes, bit 0 the lowest bit of the first byte. What the fields mean is
under "Path motion" below: `param` is 1 for a loop and 0 for an open path, bit 0 of `mask` says the points carry
tangents, and `dir` and `up` are the frame of the curve at the point.

## Path bindings (sub_448de0)

```
count u32
  name string, trackCount u32
  track[...] = keyCount u32, index u32, param u32, mask bitset,
               key[keyCount] = f32[3] × 3, u32 object
```

The binding's `name` is the name of a path. A track's `param` is the bits of an `f32`, the speed; each key is the
position, direction and up (the three vectors) of the object (an object `id`, as joints name actors) that moves, as it
stands when the level begins. See "Path motion".

## Path motion

How the paths and bindings make 35 objects move: the sharks and the birds, the objects with game-object flag `0x01`.
Everything is decoded from the data of all 83 levels (the executable's code for it has not been read), so each claim is
tagged: **[D]** the data shows it, checked numerically against the 35 objects; **[I]** inferred; **[?]** not verified.
The runtime is `game/PathPlayer.h` (the curve, and where an object is on it) and `PhysicsScene::step` (moving the
bodies); `mb_path_smoke` runs it on the real levels (35 movers in 8 levels, 3 keys off their path, the starting
frames, the speeds).

### What is in a level

**The paths are the editor's library of tracks, saved with every level [I].** Whether anything uses them is another
matter: only 8 levels bind anything, and a path that nothing is bound to moves nothing **[D]**. Their names and
counts:

| Path | In | Points | Used by |
|---|---|---|---|
| `track/track` | all 83 levels, the same 4 points in each | open, explicit tangents | nothing in a level: see below |
| `track/krug1` | 82 (empty in 81, 8 points in `piratos-po-krugy`) | a loop round (0.1, 5.4), radius 14 to 17, length 98 | the 10 small sharks of `piratos-po-krugy` |
| `track/krug2` | 80, the same 8 points in each | a loop round (0, 5.7), radius 11, length 71 | the 9 sharks of `piratos-po-krugy` |
| `track/samba` | 38 (20 knights', 12 cowboys', 4 pirates', 2 village levels), the same 7 points | a loop of length 49, rising and falling by 2.5 | the 10 birds of `cowboys-samba` |
| `track/shark` | 82, in 6 variants (64 levels have the same 7 points: length 58) | loops of 3 to 7 points, length 33 to 66 | the shark of each of 6 levels |

(`piratos-bad-night` has only `track/track`.) "Krug" is Russian for a circle: the two `krug` paths are the rings the
sharks of `piratos-po-krugy` circle on **[I]**. **They have nothing to do with the `vibro` objects `krug.x`** (6 of the
23 vibros; the other 17 are `barrel.x`), flat discs of diameter 5, life rings: none is bound to a path, and their distance
from the `krug` circles of their levels runs from 0.05 to 10 with no pattern. How a vibro moves (a bobbing on the waves,
perhaps) is unknown. The 35 objects that follow a path:

| Level | Path | Objects (ids), mesh | Speed | Lap |
|---|---|---|---|---|
| `piratos-po-krugy` | `krug1` | 10 `shark-small.x` (5 to 14), about 10 units apart | 5 units/s | 19.6 s |
| `piratos-po-krugy` | `krug2` | 9 `shark.x` (32 to 40), about 8 apart | 5 | 14.2 s |
| `cowboys-samba` | `samba` | 10 `c-bird2.x` (140 to 148, 164), about 5 apart | 6 | 8.2 s |
| `piratos-ostrovok` | `shark` | `shark.x` (212) | 5 | 13.1 s |
| `piratos-birthday` | `shark` (5 points) | `shark.x` (176) | 5 | 8.4 s |
| `piratos-dva-doma` | `shark` | `shark.x` (152) | 7 | 9.3 s |
| `piratos-ribalka` | `shark` (4 points) | `shark.x` (169) | 5 | 13.2 s |
| `Piratos-Zagoraem` | `shark` (3 points) | `shark.x` (153) | 5 | 6.6 s |
| `piratos-zoloto-gorillas` | `shark` | `shark.x` (128) | 5 | 11.5 s |

The objects with the flag and the objects named by a key are the same 35 **[D]**. Each is a `brick` with a kinematic
body (flags `0x180`) and has one key: a `shark.x` is worth 91 points and one hit, a `shark-small.x` 97 and two, a bird 120
and two.

### The fields

- **Path `param`**: 1 for a path that is a loop, 0 for an open one; `track/track` is the only open one **[D]**. The
  closing segment from the last point back to the first is an ordinary segment: two birds of `cowboys-samba` stand on it,
  within 4 mm of the curve **[D]**.
- **Path `mask`** (4 bits): bit 0 is set exactly on the paths whose points carry tangents, which is `track/track` in 83
  levels and no other path **[D]**. Bits 1 and 2 vary from level to level on paths with identical points (`00`, `02`,
  `04`, `06` on `krug1`; `01`, `05`, `07` on `track/track`), so they are editor settings, not part of the path **[I]**.
- **Point `pos`, `tangent`, `dir`, `up`**: `tangent` is the Hermite tangent, non-zero only where bit 0 of the mask says
  so; `dir` and `up` are the frame of the path at the point: a unit direction and a unit up perpendicular to it. At
  every point of a loop `dir` is the curve's unit tangent to within a degree or so (a mean of 0.9 over every loop of every
  level) **[D]**; the curve does not need it.
- **Binding `name`**: the path the tracks are on **[D]** (the name of a path in the same level).
- **Track `param`**: the bits of an `f32`: **0.005**, **0.006** or **0.007**, the speed (see below) **[D]**. **`index`**
  is 0 in all 8 tracks and **`mask`** is 5 bits, `0x19`, in all of them: not understood, and not used **[?]**. Every
  binding has at most one track.
- **Key `a`, `b`, `c`**: the position, the direction (the object's +z) and the up (its +y) of the object, and
  **`object`** its `id` **[D]**.

### The curve

Each pair of neighbouring points is joined by a cubic Hermite segment, parameter 0 to 1, with the tangent at each end:
the point's own `tangent` when the path has tangents (`track/track`), else **Catmull-Rom's: half the way from the point
before to the point after** (wrapping round on a loop) **[D]**. 32 of the 35 keys lie on that curve to within 1 cm (2 mm
on most), where the same curve with centripetal or chordal parameters misses them by up to 0.5 **[D]**. The 3 that do
not belong to paths the designer edited after placing the object: the shark of `piratos-ostrovok` is 3.2 units off the
curve (and 34 degrees off its direction), two small sharks of `piratos-po-krugy` 0.3. How the original treated such a
key, whether it snapped the object to the curve or left it off, is not in the data **[?]**.

Between the points the frame follows the curve: the direction is the curve's tangent (32 keys agree to a mean of
0.34 degrees, at most 0.77), and the up is the points' ups blended along the segment (the plain blend, then made
perpendicular to the direction: a mean of 0.33 degrees from the keys, at most 2.7) **[D]**. (Blending the points'
*directions* instead misses the keys' by 4.6 degrees on average.) The paths of the sharks dive and roll (`up` leans by
up to 10 degrees, y from -1.6 to 0), so the frame is a full orientation, not a heading.

The object's pose is the rotation whose **rows are x = up × direction, y = up, z = direction**, at the position
(`NxMat33` columns are the same three vectors; `Mat4::fromPhysX`): the 35 saved rotations are that matrix to within
0.04 (the lag below) **[D]**. The mesh's forward axis is +z.

The curve is not always well behaved: the paths of `piratos-ribalka` (4 points: out to one end, back through a point in
the middle and out to the other), `piratos-birthday` (5) and `Piratos-Zagoraem` (3) turn by 110 to 155 degrees inside
one segment with a radius of curvature of 0.02 to 0.15 (the 8-point circles of `krug1` and `krug2`: 5.9),
where the speed of the curve in its parameter changes more than tenfold. So an object on them pivots on the spot at
the turns. Walked by arc length that is no problem; walked by the parameter the sharks would race along the straights
and crawl round the turns.

### The speed

**The track's `param` is a speed in world units per millisecond, along the curve, whatever its shape [D for the unit
of length, I for the millisecond].** Every one of the 35 saved poses lies behind its key, on the line of the key's
direction, by exactly `17 × param` in all but one level (0.0850 for 0.005, 0.1020 for 0.006, 0.1190 for 0.007; to 4
digits, for 19 sharks on circles of radius 11 and 15), and by `34 × param` in `piratos-ribalka` (0.1700). A path-wide
rate would give a lag that depends on the length of the segment the object is on; this does not, so `param` is world
units. And 17 and 34 are frame times: the key is where the editor's last update put the object (one step of
`param` × the frame time in milliseconds, 17 ms for a frame at 60 Hz, 34 for a long one) and the PhysX actor, saved
at the same moment, was still where the step before had put it **[I]**. That makes the speeds **5, 6 and 7 units a
second** (the ball goes 15): a shark crosses the 40 wide board in 8 seconds, and a small one of `piratos-po-krugy`
circles in 20.

**[?] Not checked against footage.** That the unit is the millisecond rests on the saved lag being one frame at 60 Hz
(17 ms), and on nothing else. If the footage shows another speed, change `kPathUnitsPerSecond` in
`game/PathPlayer.h`, the one place that has the factor.

### How an object moves

At the start of a level an object is at its key (the point of the curve nearest it, preferring the one where the
curve runs its way, so that a path that crosses itself gives the right branch) and goes on in the direction of the
points, **forward** (the keys' directions agree with increasing index, and the saved poses are behind them) **[D]**,
at its track's speed. On a loop it goes round for ever, and neither stops nor turns at the closing segment. An open
path has no data (the only one, `track/track`, has nothing bound); the runtime stops an object at its end.
Objects on one path keep their spacing, since they share a speed; they do not collide with each other (kinematic).

In the game a path actor is a kinematic body whose target `PhysicsScene` sets before every fixed step, so it pushes the
dynamic bodies it meets (the ball, bricks). It is put on the curve before the first step, not moved there: the saved
poses are a frame behind the keys, and the shark of `piratos-ostrovok` is 3.2 units off its path. A destroyed shark or
bird is removed with its body and is driven no further.

### `track/track`: the bat's path **[I]**

The same 4 points in all 83 levels, open, with tangents (mask bit 0): (-43.4, 8.7, -16.1) heading (+x, slightly -z),
(-6.7, 0, -26.0), (-1.7, 0, -25.4) and (0.25, 0, -20.9) heading +z. It starts beyond the lower left of the picture, high
above the water, runs in below the board's near edge and ends at the bat's place (x 0, z -21; the bat's line is
`GameplayRules::batZ`), 49.6 units later. That fits the bat flying in at the start of a level and after a lost ball
(docs/GAMEPLAY.md, "The life cycle on screen": "swoops in from beyond the lower left"; 0.7 s for it would be 71 units a
second), and its `up` rolls by up to 19 degrees on the way. Nothing in the data binds it, and `PlayScene`'s swoop is a
made-up one fitted to the footage, so this stays a guess; if it is right, it is what that swoop should follow.

## Converted JSON

`tools/convert_assets.py` writes `assets/Data/Levels/<name>.json` next to each
`.mb3`; `mb3level.parse()` builds it and `game/src/LevelLoader.cpp` reads it.
It mirrors the structures above field by field. Poses, shape geometry and
hull points are rounded to 5 decimals; body, joint and skin-width values are
written as the shortest decimal that reads back as the same float32 (so tiny
inertias and FLT_MAX survive). The descriptors' raw bytes are kept as hex
strings next to the decoded fields.

```
version       int       see Versions
gravity       [x, y, z]
actors        [actor]   only the top-level actors; cloth segments are nested
globalParam   u32
joints        [joint]
paths         [{name, param, mask: {count, hex}, points: [{pos, tng, dir, up}]}]
pathBindings  [{name, tracks: [{index, param, mask, keys: [{a, b, c, object}]}]}]
trailingBytes int       bytes left unparsed; 0 in every shipped level
```

A path's `param` and `mask`, and a binding's track, are as under "Path motion": a key's `a`, `b` and `c` are the
object's position, direction and up, and a track's `param` is the integer whose bits are its speed as an `f32`
(1000593162 is 0.005). The runtime reads them into `Level::paths` (`closed`, `tangents`) and `Level::pathBindings`; a
file without them has no moving objects.

An actor is `{offset, pose: {rotation[9], position[3]}, density, actorFlags,
group, body?, shapes, object?}` (`offset` is where its descriptor starts in the
`.mb3`; `body` is present for actors that have a body, all of them in the
shipped levels). A body is the Body descriptor field by field:
`{massLocalPose: {rotation[9], position[3]}, massSpaceInertia[3], mass,
linearVelocity[3], angularVelocity[3], wakeUpCounter, linearDamping,
angularDamping, maxAngularVelocity, flags, sleepLinearVelocity,
sleepAngularVelocity, solverIterationCount, raw}`.

A shape is `{type, localPose, shapeFlags, group, materialIndex, skinWidth,
raw}` plus `radius` (sphere), `halfExtents` (box), `radiusHeight`
`[radius, height]` (capsule) or `meshName` (convex, mesh). A convex shape also
has `hullPoints`, `[[x, y, z], ...]` in the shape's space, copied from the
`.cnvx` file when the level is converted.

A joint is `{actorA, actorB, type, localNormal[2][3], localAxis[2][3],
localAnchor[2][3], maxForce, maxTorque, jointFlags, raw}` (`actorA` and `actorB`
as stored, see Joint actor references; `jointFlags` is the two flag bits)
plus, by type:

- revolute (1): `limit: {low, high}`, `motor: {velTarget, maxForce, freeSpin}`,
  `spring: {spring, damper, targetValue}`, `projectionDistance`,
  `projectionAngle`, `flags`, `projectionMode`; a limit is `{value,
  restitution, hardness}`;
- spherical (3): `swingAxis[3]`, `projectionDistance`, `twistLimit: {low, high}`,
  `swingLimit`, `twistSpring`, `swingSpring`, `jointSpring`, `flags`,
  `projectionMode`.

An object is `{type, id, flags}` plus whichever of `material`, `mesh`, `frame`,
`extra` (hex), `prize`, `target`, `param`, `names` and `cloth` its type carries. A
dynamite's `target` is its explosion's particle file (`explosebig` on 101 of the 116,
`explosesmall` on 15) and its `param` the bits of an `f32`, the blast radius: 5.0 and 3.0
(`GameObject::target` and `param`; `GAMEPLAY.md` 17.2).

### Versions

The top-level `version` says which revision of the converter wrote the file. A
file without it is version 1. `LEVEL_JSON_VERSION` in `tools/mb3level.py` and
`kLevelJsonVersion` in `game/include/mb/game/Level.h` must be raised together
whenever the output changes in a way that files from before the change would be
misread; `tests/test_mb3level.py` checks that the two agree.

| Version | Change |
|---|---|
| 1 | No `version` key. The earliest files read a capsule's `radiusHeight` one float too late, so it held `[height, 0]`. |
| 2 | Capsule `radiusHeight` is `[radius, height]`. Adds `version`. |
| 3 | Decodes the descriptors. Actors gain `density`, `actorFlags`, `group`; `body` is an object (before: the descriptor as one hex string); shapes gain `shapeFlags`, `group`, `materialIndex`, `skinWidth`, and convex shapes `hullPoints`; joints gain their frames, break thresholds, flags and the revolute / spherical fields. |

The runtime loads an older file but logs one warning per load asking for
`tools/convert_assets.py` to be run again. A file converted after the capsule
fix but before `version` existed has no `version` either, so it warns too. A
version 1 or 2 file loads with its bodies as default dynamic bodies (and no
mass, shape flags, groups, hulls or joint details), so a physics step needs
version 3.
