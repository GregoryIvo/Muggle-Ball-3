# Object definitions, particles and what they say about gameplay

Notes for whoever builds the bat, ball, bricks, prizes and effects. Everything
here comes from `Data/Objects/**` (converted by `tools/objects.py`, JSON format in
[ASSET_FORMATS.md](ASSET_FORMATS.md)), `Data/Particles/*`, `Data/Materials/materials.json`,
the 83 converted levels, the meshes and the sound list. There is no original
code to read, so every claim carries one of three labels:

- **data**: stated by a file, or counted over all files. Safe to build on.
- **inferred**: follows from names, numbers or correlations. Plausible, checkable
  against footage, not proven.
- **unknown**: not in the data at all. It lives in the executable, so the
  implementer chooses a value and checks it against footage.

Level JSON fields used below (`group`, body `flags`, `shapeFlags`) are level JSON
version 3, the one in `assets/`, documented on the physics branches
(`docs/LEVEL_FORMAT.md` there), not yet in this tree's copy.

## Summary

1. **The definitions are the level editor's object library, not a runtime
   rulebook.** A level stores a complete baked copy of every object (shapes,
   material name, mesh, frame, a few extras). Objects that never appear in a
   level are built from the XML while playing: the **bat, ball, wall, bullets,
   prizes, explosions** (and the rocket / meteor / thunderbolt / cannonball
   projectiles). One exception reads the XML for an object that *is* in every
   level: the sky dome's rotation `speed` (section 2). Section 2.
2. **The XML carries no tuning numbers for the player's side.** The bat and ball
   have no `<Extra>`; prizes have a name and a halo colour but no duration, speed
   or size factor; generators have no rate. Those values are hard-coded in the
   original and must be chosen from footage. What the data does give: shapes and
   sizes, bullet velocities, a per-material score / energy / explosion class, the
   sounds, and the mesh frames of the bat (its weapon variants). Sections
   3 to 6.
3. **Bricks are described by their material**, not their XML:
   `materials.json` `brick{type, energy, score, exploseType, exploseDelay,
   explosePower}`. Values and observed meaning in section 9. `brickType` is 0, 2,
   4 or 5 only; 2 means "not a brick" (ball, bullet, board, default).
4. **Which bricks must die for the level to end is not in the data.** Section 10
   gives the object classes, the counts, and the two cheapest hypotheses to test
   against footage.
5. The typos in the XML (`<b1ox>`, `<spher1e>`, `<m1esh>`, `<mesfh>`,
   `rotat1ion`) are **not** to be repaired: the levels prove the original
   ignored at least three of them (section 12).

## 1. Inventory

368 files: 348 under `Objects/` and 20 under `Particles/`, 11,639 elements. The
object files hold 1,646 `<object>` entries, of 26 distinct `type` strings (case
varies: `Prize`, `Board`).

| Folder | Files | Objects | What |
|---|---:|---:|---|
| `Balls` | 1 | 1 | `ball.xml`: the ball |
| `Bats` | 2 | 2 | `bat.xml`: the bat; `wall.XML`: the barrier a prize raises |
| `Bullets` | 4 | 5 | the bat's weapons' projectiles |
| `Prizes` | 28 | 28 | one per prize |
| `Exploses` | 1 | 1 | the explosion trigger sphere |
| `Interactive` | 18 | 30 | dynamite, fireworks, generators, metal, portals, cannon, spring, volcano, meteor, thunderbolt, cannonball |
| `Boards` | 5 | 6 | the playfield; `boardsea.xml` also holds a shadow caster |
| `BackGrounds` | 3 | 3 | the sky domes |
| `Lights`, `ShadowCasters` | 3 + 3 | 3 + 3 | marker objects for the level's lighting and shadows |
| `Castle`, `Cowboy`, `Ground`, `Live`, `Animals`, `Matr`, `new9`, `Sea`, `Trees`, `Village` | 280 | 1,564 | scenery and targets: buildings, ground tiles, trees, ships, characters, animals, props. Mostly `type="brick"`; also `tree` (6), `vibro` (2), `cloth` (19), `flicker` (2), `metall` (7 in `Matr`) |

`Interactive` types: `dynamite` 2, `firerocket` 1, `firework` 1, `generatorbad` 1,
`generatorgood` 1, `metall` 14, `bullet` 3, `teleportsrc` 1, `teleportdest` 1,
`cannon` 1, `spring` 1, `volcano` 3.

**`<extra>` is the only element with game parameters.** All of its attributes in
the shipped data (the tag is `<Extra>` or `<extra>`):

| Attribute | On | Values | Meaning |
|---|---|---|---|
| `description` | 28 prizes | `Bazooka`, `Life`, ... | the prize's id string, see section 5 |
| `oreol` | 28 prizes | `oreolblue` (21), `oreolred` (7) | halo particle file, `Particles/Oreolblue.xml` / `Oreolred.xml` |
| `type` | 5 prizes | `commonball`, `gun`, `incbat`, `life`, `magnit` | equals each file's stem and material; purpose unknown |
| `velocity` | 8 bullets, fire rocket | vector | launch velocity (units per second, in the owner's frame, **inferred**; section 4) |
| `disableGravity` | 6 bullets, fire rocket, wall | 1 | **inferred**: the PhysX body flag disable-gravity (`0x01` in level bodies) |
| `bulletType` | 5 bullets | 2, 3, 4 | projectile class, section 4 |
| `disablePrize` | fire rocket, meteor, thunderbolt | 1 | **inferred**: bricks this object destroys do not drop a prize |
| `lifeTime` | fire rocket | 2500 | milliseconds, **inferred** |
| `particles1`, `particles2` | fire rocket, meteor, flickers | particle file stems | `trail`, `trailmeteor`, `spark`, `sparkKoster`, `smoke` |
| `shootSound` | right machine-gun bullet | `s` | marks the bullet that plays the shoot sound, **inferred** |
| `explose`, `radius` | dynamite | `explosebig` 5, `explosesmall` 3 | explosion particle file and trigger radius |
| `standUp` | 64 bricks, fire rocket | 1 | keep the object upright (characters, animals, eggs, jugs, buckets, flowers, mushrooms); level object flag `0x40` |
| `light`, `Diffuse`, `Specular`, `Range`, `Flicker` | flickers, a streetlight | 1, colours `A R G B`, 20 / 50 | a point light on the object, baked into the level as a Direct3D light |
| `shadowType` | shadow casters | `mirrors`, `dxdirectional`, `dxpoint` | level object param 16 / 32 / 64 |
| `speed`, `translation` | the 3 sky domes | 0.0003, 0.0002 | sky rotation speed (units unknown) |

On `<shapes>`: `rotfrozen="1"` (22 shape lists, `0` on 2 more) means a frozen
rotation (level object flag `0x80`, body flags `0x70`); `trigger` appears only on the bat
(0) and the explosion sphere (1). `<joints>` hold `NX_JOINT_FIXED` (800),
`NX_JOINT_REVOLUTE` (55) and `NX_JOINT_SPHERICAL` (2) joints between objects of
the same file, with `breakForce` / `breakTorque` (10 to 500,000), and motors on
two windmill joints and the ferris wheel (`motor`, `freeSpin`, `maxForce`,
`velTarget`). Levels store the same joints already built (types 8, 1, 3).

Compound files (`Matr`, `Ground/trava_matrix1`, `Village/Builds/fencelong*`,
`Animals/sheeps`...) hold no objects, only `<file name>` includes with
`<moveobjects x y z>`, `<rotateobjects y>` (1.57 = a quarter turn) and one
`<fixedjointall/>`. **Inferred**: a stamp-and-shift macro, applied in document
order; the levels contain the expanded result, so the runtime never needs them.
Four of them point at `Data/Objects/Grass/Builds/`, now `Village/Builds/`.

## 2. How a level object maps to a definition

**Data.** A level object has `type`, `id`, `flags`, optional `material`, `mesh`,
`frame` strings and a few baked extras. It carries no XML file name. The link is
the pair **(mesh, frame)**, matching an XML `<mesh name=... frameNode=...>`
(or `<skinmesh>`), compared case-insensitively:

- all 13,434 level objects that have a mesh (every type except the 63
  shadow casters and 2 lights) find a definition; 1,292 distinct pairs, all found.
  The XML has 1,385 pairs, 93 of them used by no level.
- 5,903 of the objects match more than one file (ship colour variants, compounds,
  copies). Their shapes agree: for 13,208 of the 13,268 actors with a shape list,
  the level's shapes equal a matching definition's (a box's `halfExtents` is
  half the XML `dimensions`; sphere radius, capsule radius and height are the
  same numbers). The 60 exceptions are 22 cloth anchor spheres (0.5 in the
  level, 1.1 in the XML) and 38 hand-edited bricks.
- the level's `material` equals the definition's `<material name>` (case-
  insensitive), except 47 backgrounds, 2 `c-bochka.x` bricks and 1 `kotel.x`
  flicker that carry none. Boards and shadow casters have no `<material>` in
  the XML; the level gives boards `BoardMaterial`.
- the XML `type` string is the level's object type (`Prize`->prize...), for 25 of
  its 26 values. `train` (level type 26) has no definition and no instance.

**What the level bakes from `<extra>`:** dynamite `explose` and `radius`
(`target` string and the radius in `param`, which the level JSON stores as the
u32 bit pattern of a float: 1084227584 = 0x40A00000 = 5.0 on 101 `explosebig`,
1077936128 = 0x40400000 = 3.0 on 15 `explosesmall`; reinterpret it as a float); flicker `particles1` / `particles2` (`names`); `light` and its colours
(a `D3DLIGHT9` in the 0x68-byte extra, flags `0x200`: point light, range 20 or
50, attenuation 0.3 / 0.03 / 0.01, the XML's `A R G B` reordered); the shadow
type as `param` 16, 32 or 64 (**inferred** mapping: 16 `mirrors`, 20 levels, the
marker on the water plane; 32 `dxdirectional`, 41 levels, high above a corner; 64
`dxpoint`, 1 level, centred above the board; `cowboys-v-tualet` has 120);
`standUp` as object flag `0x40`. Object flag `0x80` goes with `rotfrozen` (an
attribute of `<shapes>`, not `<extra>`) only loosely: 37 level objects whose XML
has `rotfrozen="1"` lack it, and 116 flagged objects (all 63 fireworks among
them) have no `rotfrozen` in any matching XML. Use the level's own object flags
and body flags (`0x70`, frozen rotation), never values derived from the XML. Flag
`0x01` is set on the
35 sharks and birds that follow a path (they are kinematic and listed in the
level's `pathBindings`). The 2 light objects (evening and night materials) carry
directional-light extras (`D3DLIGHT9` type 3). Level `globalParam` is the
editor's next object id (61 of 83 levels equal the largest id or one more), not
a rule.

**Everything else in `<extra>` is absent from levels** (velocity, bulletType,
disableGravity, lifeTime, disablePrize, shootSound, description, oreol), because
the objects that use it never appear in a level. The exceptions are the sky
dome's `speed` and `translation`: backgrounds are in all 83 levels (mesh
`bg/bg_day.x`, `bg_night.x` or `bg_red.x`) with no rotation speed baked in, so
the speed (0.0003 or 0.0002) must be read from `BackGrounds/*.xml` at run time,
found through the skinmesh name.

**Runtime-only objects.** Of the 26 types, 7 never appear in any level:
`bat`, `ball`, `prize`, `bullet`, `wall`, `firerocket` (and `train`); and the XML
type `explose` exists only as a definition. So the game creates these from the
XML while playing. How the executable finds the file (by path, by folder, by
`description`) is **unknown**; the stems are consistent (`Prizes/<stem>.xml`
equals the material and mesh stem, the explosion names equal `Particles/` file
stems).

**Dead references.** XML materials absent from `materials.json`: `Sky.jpg`,
`Night.jpg`, `red.jpg` (the three sky domes), `Bonus-Stena.jpg` (the wall),
`Basket.jpg` (the basket brick, `Hlam/basket.XML`, one level object) and
`Brevno.jpg` (the cauldron `kotel`, the only object with `Flicker=1`; the torch
`fakel` uses `Lopata.jpg`, which exists, and has no `Flicker` attribute). Meshes: every mesh
a definition, a level or a material names exists; 7 of the 272 `.glb` files are
named by nothing in the data: `ball-explosive.x` and `ball-powerfull.x` (the
executable swaps them in for the ball), `c-holm1.x`, and four splinter meshes
(`biege`, `blue2`, `lilac2`, `red2`).

## 3. The bat, the wall and the ball

**Bat** (`Bats/bat.xml`, **data**): `<object name="BAT" simple="0" type="bat">`,
one shape, `<sphere name="ph_BoxBottom" trigger="0" radius="3." translation="0 0 0">`
(a solid sphere centred on the bat's origin, which, **inferred**, sits on the
floor plane), mesh `<skinmesh name="bat.x">`. No material, no `<extra>`: **no speed, size or
position parameter exists in the data**.

`bat.x` is one mesh holding every visual state as separate frames (names from the
`.x`, extents in the bat's space; the first column is what the name suggests,
**inferred**):

| Frame(s) | x extent | z extent | Reading |
|---|---|---|---|
| `Korpus`, `Right-Wing`, `Left-Wing`, `Shesterenka-*` ("little gear") | +-1.96 | -1.9 to 1.25 | the ship-like body |
| `Hand`, `Small-Hand` | +-1.94, +-2.15 | -0.84 to 1.76 | the arms holding the paddle |
| `Bita` ("club") | **+-2.77** | 0.94 to 3.0 | the curved paddle in front, 5.54 wide |
| `Magnit` | +-2.77 | 0.94 to 3.0 | the same arch as the magnet variant |
| `Pulemet-Left/Right`, `-Stvol-*` | +-2.5 | -1.1 to 1.8 | the two machine guns (Gun) |
| `Pushka` | tiny | -0.76 to -0.38 | the cannon (Cannon) |
| `Bazuka-Korpus`, `Bazuka-Stvol` | +-0.78 | -1.65 to 2.96 | the bazooka |
| `lazer-Korpus`, `Lazer-Stvol` | +-0.61 | -1.11 to 2.91 | the laser |

The weapon frames map one to one onto the four weapon prizes and the four
bullet definitions (section 4). The default bat of the game (`PlayScene`) is the frames `Korpus` (with its wings and little gear
below it), `Shesterenka-Left`, `Shesterenka-Right`, `Hand`, `Small-Hand` and `Bita`, which matches the footage's close-ups of
the bat without a prize; its origin stands on the floor under the centre of the collision sphere, so that the arch's apex
(z 3.0) meets the ball (radius 0.6) at its contact z. The texture is `Metall-Raketka-Red.jpg`; materials
for `Blue`, `Green` and `Yellow` variants exist in the `Bats` material group, so
the bat colour is probably chosen per stage or setting (**inferred**). The paddle
(`Bita`, half-width 2.77) fits inside the physics sphere (radius 3): the sphere
is the collision shape, **inferred** to give the ball an angle that depends on
where it hits (the saloon screenshot has the ball just above the front of the
arch). The 6-unit diameter equals the z extent of the board's group-11 box, which
is z -20 to -14, x +-22, y 0 to 30 once its rotation `(-0.5 -0.5 -0.5 0.5)` is
applied (the XML's `dimensions="6 44 30"` and the level's half extents 3, 22, 15
agree; the physics-branch note giving z -32 to -2 leaves the rotation out). That
makes the bat's centre line z = -17 (**inferred**). The lane box's purpose is not
in the data; it touches no brick in any level.

**Wall** (`Bats/wall.XML`, **data**): type `wall`, one box 41.43 x 3.22 x 0.74
(full extents) at (0.02, 0.62, -20.01), mesh `wall.x`, material `Bonus-Stena.jpg`,
`<extra disableGravity="1">`, `rotfrozen`. It is the barrier along the near edge
(z = -20) that the **Wall** prize raises (**inferred**: its shape is named
`Stenka`, "little wall", like the prize's). It is as wide as the playfield and
sits at the near edge of the bat's lane.

**Ball** (`Balls/ball.xml`, **data**): `<Object name="BALL" type="ball">`, two
identical `<Sphere radius=".6">` shapes at the origin (a duplicate; use one), mesh
`ball.x`, material `ballMaterial`. No `<extra>`. `ball.x` is a sphere of radius
0.64. The material (`BallMaterial`, first of the case-insensitive matches):
density 1, restitution 0.999, static and dynamic friction 0.5, brick
`{type 2, energy 3, score 4, exploseType 0, exploseDelay 5}`, texture `Ball`
(flagged animated, 100 ms, one frame), sounds `hit` = `ballHitBoard`, `period` =
`ballHitBall`. The material `ball.jpg` (the `.x`'s own material name) differs only in
static friction 0.01 and `exploseType 2`. `sounds.xml` also names `ballHitBat`,
`ballMagnited`, `ballRubbered`, `explosion_ball` and `bat_dead`, used by no
material, so the executable plays them (**inferred**: bat hit, magnet capture,
yo-yo string, explosive ball, ball lost).

Ball-modifying prizes need meshes the XML does not name: `ball-powerfull.x`
(radius 0.94, a polyhedron, `Hedra01`) and `ball-explosive.x` (extends to y 1.31,
probably a fuse). Big, weak and
multiball reuse `ball.x` or are not in the data (**unknown**: the size factors).

Restitution against the board: 0.999 with the board's 1.0 (`BoardMaterial`),
combine mode average (PhysX 0) in every system material, so the ball loses
almost nothing on the walls. Against a typical brick (0.5) the pair gives 0.75.

## 4. Bullets (`Bullets/*`, three in `Interactive/`)

Shapes, offsets, velocities and flags are **data**; readings are marked.
Positions are in the owner's space (the bat's, for the first four). The machine-gun, cannon and rocket offsets are all at z >= 3.07, in front
of the paddle (z 3.0); the laser's capsule is at the origin (0, 1, 0), the
mesh-less template of a bolt that the game places at the laser barrel
(`Lazer-Stvol`, z up to 2.9).

| File, object | Shapes | Spawn offset | `velocity` | Extras | Material, sounds (`period` = fired, `dead` = hit; **inferred**) |
|---|---|---|---|---|---|
| `Bullets/bullets.XML` `Left-Bullet`, `Right-Bullet` | capsule r 0.4 h 0.443 (rotated) | (-2.154, 1.198, 3.411), (+2.157, ...) = the two gun barrels | (0, 0, 30) | `disableGravity`; right bullet `shootSound` | `bat_bullet`; `bullet_shoot`, `bullet_dead` |
| `Bullets/kernel.XML` `Hedra01` | sphere r 0.868 | (0, 1.704, 3.067) | (0, 3, 40) | `bulletType` 4; **no** `disableGravity` | `bat_kernel`; `kernel_shoot`, `kernel_dead`. Mesh `Kernel.x`. Lobbed: gravity -9.8 acts |
| `Bullets/rocket.xml` `Bazuka-Stvol01` | capsule r 0.452 h 1.0 | (0.006, 1.406, 4.791) | (0, 0, 52.5) | `disableGravity`, `bulletType` 4 | `bat_rocket`; `rocket_shoot`, `rocket_dead`. Mesh `Bat-Rocket.x` |
| `Bullets/lazer.XML` (no name) | two identical capsules r 0.15 h 4.0, along z | (0, 1.0, 0) | (0, 0, 50) | `disableGravity`, `bulletType` 2 | `bat_lazer`; `lazer_shoot`, `lazer_dead`. No mesh: drawn by the `Laser` particle |
| `Interactive/yadro.XML` `Hdra01` | sphere r 0.591 at (1.986, 1.69, 0) (+ a second, typo'd sphere, section 12) | the cannon's muzzle | (30, 3, 0) | `bulletType` 4 | `Telega1.jpg` (wood: energy 2, score 22, splinters; `village_wood_*`). Fired by the level's cannon, sideways along +x |
| `Interactive/meteor.XML` `Sphere01` | sphere r 0.492 | (0, 0.8, 0) | (-10, -30, 10) | `disableGravity`, `disablePrize`, `particles1="trailmeteor"` | `meteor` (temp group: energy 1, score 250, `exploseType` 9); `meteor`, `meteor_dead` |
| `Interactive/thunderbolt.XML` `Sphere01` | sphere r 0.492 | (0, 0.8, 0) | (0, -50, 0) | `disableGravity`, `disablePrize`, `bulletType` 3 | `thunderbolt` (energy 1, score 10, `exploseType` 9); `thunderbolt`, `thunderbolt_dead`. No mesh: the `Lightning` particle |

Common material parameters of the four `bat_*` materials: density 5, friction
0.7 / 0.7, restitution 0.3, brick `{type 0, energy 3, score 4, exploseType 3,
exploseDelay 500, explosePower 6.789}`. Launch speeds are 30, 40, 50 and 52.5
units per second against a board 40 wide.

`bulletType` (**inferred**): absent = a plain bullet (machine gun), 2 laser,
3 thunderbolt, 4 explosive or heavy (kernel, rocket, cannonball). The data does
not say what each does on impact beyond the material's explosion class. Nothing
gives damage, rate of fire, ammunition or duration; the `shootSound` marker
suggests one sound per volley.

`velocity` is in the owner's frame, so the cannon's `+x` is its own forward and
the meteor's (-10, -30, 10) slants across the board while falling.

## 5. Prizes (`Prizes/*`)

All 28 are `<object type="Prize">` with one box (a pick-up volume), mesh
`Prizes/<stem>.x`, material `<stem>`, and `<extra description oreol [type]>`.
**Data**: the only per-prize numbers are the box size, the material's
`score` / `energy`, a `dead` sound, and the halo. There is **no duration, speed,
size factor, probability or magnitude anywhere**. Halo `oreolblue` is 21 prizes,
`oreolred` 7 (**inferred**: good and bad; the level has `generatorgood` and
`generatorbad` objects).

Material parameters shared by all prizes: density 5, friction 0.7, restitution
0.3, `brick{type 0, energy 3, exploseType 3, exploseDelay 500, explosePower
6.789}`, one `dead` sound named after the prize (**inferred**: played when it is
collected). `meteor` is defined
twice in `materials.json` (the temp projectile first, the prize second); with the
"first wins" rule the prize inherits the projectile's energy 1 / score 250
(**unknown** which the original used; the table gives the prize's own row).

| File | `description` | Halo | Box (full extents) | Score | Effect (**inferred** from names) | HUD icon |
|---|---|---|---|---:|---|---|
| `bazuka` | Bazooka | blue | 2 x 2 x 2 | 340 | bat weapon: rockets (`rocket.xml`) | `Bazuka.png` |
| `cannon` | Cannon | blue | 2 x 2 x 2 | 320 | bat weapon: cannonballs (`kernel.XML`) | `Cannon.png` |
| `gun` | Gun | blue | 2 x 1.74 x 2 | 300 | bat weapon: two machine guns (`bullets.XML`) | `Gun.png` |
| `lazer` | Laser | blue | 2 x 1.32 x 2 | 360 | bat weapon: laser (`lazer.XML`) | `Lazer.png` |
| `magnit` | Magnet | blue | 1.6 x 1.6 x 2.08 | 230 | the ball sticks to the bat (`ballMagnited`) | `Magnit.png` |
| `powerfull` | PowerBall | blue | 2 x 1.33 x 2 | 170 | piercing ball ("Probivnoi"), mesh `ball-powerfull.x` | `Powerful.png` |
| `bigball` | BigBall | blue | 1.32 x 2.28 x 2.2 | 180 | bigger ball | `big.png` |
| `smallball` | WeakBall | red | 1.84 x 2 x 1.64 | 240 | smaller ball | `weak.png` (probably) |
| `explosiveball` | ExplosiveBall | blue | 2 x 2 x 1.3 | 150 | ball that explodes (`ball-explosive.x`, `explosion_ball`, `exploseball`) | none |
| `commonball` | NormalBall | red | 2 x 2 x 2 | 120 | back to the normal ball | none |
| `tripple` | Multiball | blue | 2 x 1.2 x 2 | 190 | three balls | none |
| `pingpong` | YoYo | blue | 2 x 1.12 x 1.04 | 140 | ball on an elastic string (shape "Na-nitke" = on a thread; `rubber.xml`, `ballRubbered`) | `Rubber.png` (red ring) |
| `drunken` | DrunkenBall | red | 2 x 3 x 1.2 | 220 | erratic ball ("Pianiy" = drunk) | `drunken.png` |
| `speedup` | SpeedUp | red | 2.3 x 2 x 2 | 280 | faster ball | none |
| `slowdown` | SlowDown | blue | 2 x 0.88 x 2 | 120 | slower ball | none |
| `incbat` | IncBat | blue | 2 x 0.8 x 1.2 | 110 | wider bat | none |
| `decbat` | DecBat | red | 2.3 x 1.4 x 1.26 | 180 | narrower bat | none |
| `wall` | Wall | blue | 2 x 2 x 0.6 | 200 | raises `Bats/wall.XML` | `wall.png` |
| `life` | Life | blue | 2 x 1.8 x 1.42 | 777 | extra life | `Heart.png` is the lives counter |
| `bonus` | Bonus | blue | 2 x 2 x 0.6 | 777 | bonus points (sound `money`) | none |
| `scull` | Skull | red | 2 x 2.18 x 2.62 | 666 | punishment | none |
| `night` | Night | red | 1.8 x 2 x 0.5 | 280 | darkens the level (`light_night`, `bg_night.x`) | `Night.png` |
| `wind` | Wind | blue | 1.8 x 3 x 1.8 | 100 | wind; `Tornado.xml` (texture missing) | none |
| `quake` | EarthQuake | blue | 2 x 2.74 x 1.38 | 190 | shakes the field | none |
| `flash` | Thunderbolt | blue | 2 x 3 x 1.55 | 210 | lightning strikes (`thunderbolt.XML`, `Lightning.xml`) | none |
| `meteor` | MeteoriteRain | blue | 2 x 1.42 x 2 | 4 | meteors fall (`Interactive/meteor.XML`) | none |
| `endlevel` | NextLevel | blue | 2 x 2.72 x 0.58 | 120 | finish the level | none |
| `random` | Surprise | blue | 2 x 4 x 2 | 299 | a random prize | none |

The `texturePreload` list of `materials.xml` names 13 HUD icons: the 12 above and
`heart`. They are the status icons of **persistent effects** (green ring: good,
red: bad): weapons, ball modes, magnet, wall, night, drunken, rubber, weak. The
other 16 prizes have no icon, which fits instant ones (life, bonus, skull,
quake, thunder, meteor, next level, random) but also covers speed, bat width,
multi-ball, wind and the explosive ball. Whether effects time out, and after
how long, is **unknown**.

Scores 666 (skull) and 777 (life, bonus) are plainly chosen, and the skull's is
not negative: the score is probably what the prize is worth when collected or
shot, not a penalty.

## 6. Explosions

`Exploses/explose.xml` (**data**) is the whole definition:
`<Object type="explose"><Shapes><Sphere trigger="1" radius="2.5"/></Shapes></Object>`,
one **trigger** sphere of radius 2.5, no mesh and no material. **Inferred**: the
game spawns it at an exploding object, rescales it to the object's radius, and
damages or pushes whatever it touches.

- Named explosions are **particle files**. Dynamite names its own: `explose`
  `explosebig` radius 5, `explosesmall` radius 3 (the flash `size` is 5 times the
  radius: 25 and 15, **data**). Three more explosion flashes are named by nothing:
  `exploseball` (size 10), `exploseyadro` (size 10, the cannonball) and
  `explosemeteor` (size 10, texture `boom.png`): chosen by the executable
  (**inferred**: ExplosiveBall, cannon ball, meteor).
- A material's `exploseType` is **not** linked to any explosion definition by the
  data; it is an enumeration the code reads (section 9 has the observed classes).
  Only `exploseType` 8 (dynamite) and 9 (fire rocket, meteor, thunderbolt) look
  like blasts (**inferred**).
- `exploseDelay` (milliseconds) is **inferred** to be the pause between an object's
  last hit and its removal or blast, which would make neighbouring bricks go off
  one after another. `explosePower` is 6.789 for 72 materials (all 28 prizes, the
  8 `Bats` materials, 4 system ones, 30 character materials of `Cowboy` and
  `LiveObjects`, a board and a temp one) and 1.0 for the other 165: 6.789 looks
  like an editor default, so treat it as unused.

## 7. Interactive objects (`Interactive/*`)

What the game does with them is in `GAMEPLAY.md` section 15 (step 5). Everything that follows is **data** unless marked. None has game parameters in
its `<extra>` except where listed; all behaviour is in the executable. The level
holds each object as type `cannon`, `firework` and so on, with these shapes.

| Definition | Type | Mesh, material (brick params energy / score / exploseType / delay) | Shapes | Reading |
|---|---|---|---|---|
| `pushka.XML` | `cannon` | `Pushka.x`, `int_cannon` (2 / 100 / 5 / 100, density 50, sounds hit, dead) | box 2.5 x 1.98 x 2, rotation frozen | a cannon on the field. **Inferred**: fires `yadro.XML` along its local +x (sound `brick_cannon_shoot`); 21 in levels |
| `dynamite-big/small.XML` | `dynamite` | `Dynamite.x`, `Dinamit.jpg` (1 / 120 / 8 / 100) | box 1.8 or 1.4 wide, 2.61 tall | explodes when destroyed; `explose`, `radius` (section 6); 116 in levels |
| `fireworks.XML` | `firework` | `Fireworks.x`, `Fireworks` (1 / 120 / 5 / 100) | box 2 x 3.2 x 1.4 | **inferred**: when hit, launches a fire rocket; 63 in levels |
| `fire-rocket.XML` | `firerocket` | `Fire-rocket.x`, `Fire-rocket` (1 / 200 / 9 / 0) | capsule r 0.62 h 2.05 | rises at (0, 15, 0) for `lifeTime` 2500 ms, no gravity, no prize, trail particle; **inferred**: then bursts into one of the six colours of `Fireworks.xml` |
| `generator-good.XML` | `generatorgood` | `Generator-good.x`, `gen-good` (1 / 77 / 5 / 0, `period` sound) | box 2 x 2 x 2 | **inferred**: periodically releases a good (blue) prize; 17 in levels. No rate in the data |
| `generator-bad.XML` | `generatorbad` | `Generator-bad.x`, `gen-bad` (1 / 66 / 5 / 0) | box 2 x 3.66 x 2 | the same for bad (red) prizes; 26 in levels |
| `spring.XML` | `spring` | `Spring.x`, `spring` (1 / 10 / 5 / 0, sounds hit, dead, **loop**) | 3 boxes 1.42 x 1.81 x 3 turned 0, 60, 120 degrees | **inferred**: a bounce pad. Its material is not bouncier than a brick (restitution 0.5), so the kick is code; 21 in levels |
| `volcano.XML` | `volcano` | `Volcano.x`, `volcano` (1 / 10 / 5 / 0, `period` sound) | 3 objects (3 volcanoes) of 3 boxes each | **inferred**: erupts periodically (the `period` sound); 13 in levels |
| `portal-in.XML` | `teleportsrc` | `Portal-In.x`, `portal-in` (1 / 10 / 5 / 100) | box 1.6 x 2 x 2 (`BoxCenter`) + 2 side boxes | **data (levels)**: the 12 sources each have one trigger box (shape flags 15: enter, leave, stay), the centre one |
| `portal-out.XML` | `teleportdest` | `Portal-Out.x`, `portal-out` | 2 side boxes, no centre | the exit. Levels have one source and one exit, or two sources and one exit (`knight-zoo`, `village-dvorik2`): the pairing is not in the data; send every source to the exit |
| `metall-box`, `-koluchka`, `-koluchka_a`, `-zabor` | `metall` (14 objects) | `Metall-*.x`, `Metall.jpg` (1 / 50 / 6 / 100, density 20, friction 0.999, restitution 0.1, splinter `splinter-stiil.x`) | boxes; one sphere | metal bumpers and spiked fences; level collision group **12** (452 instances), which overlaps nothing in the saved poses. **Unknown** whether the ball can destroy them |
| `meteor.XML`, `thunderbolt.XML`, `yadro.XML` | `bullet` | section 4 | | |

Other typed objects with no `<extra>`: `vibro` (`Sea/Other/barrel-func.XML`: a
capsule barrel; `krug-func.xml`: a ring of 2 boxes; all 23 level instances are
kinematic. No level's `pathBindings` lists one (they bind only the sharks and
birds), and the paths named `track/krug1` and `track/krug2` that nearly every level carries are the circles the
sharks of `piratos-po-krugy` swim on, not the vibros': `LEVEL_FORMAT.md` "Path motion". How a vibro moves is
**unknown**), `cloth` (19 sails and
flags in `Sea/Ships`: one sphere 1.1, plus a `<cloth>` element, below), `tree`
(6 in `Trees`; every level trunk is `tree.x`, material `Tree`, brick type 5,
energy 5, score 130; the fruit are separate `brick` objects, apples, melons,
pumpkins and cabbages, held to the trunk by breakable `NX_JOINT_FIXED` joints (`breakForce` 10 to 20 on
the apple tree, 180 on `arbuz-tree`, 100 on `kapusta-tree`, `melon-tree` and
`tikva-tree`),
and the `LeafFall` particle is their hit effect, **inferred**), `flicker`
(`Village/Hlam/kotel.XML` cauldron, `Village/fakel.XML` torch: a point light with
a `Flicker` flag, sparks and smoke), `light` and `shadowcaster` (marker spheres of
radius 1.5, collision group 5, no collision), `background` (sphere of radius 0.1
at (0, -10, 0), the sky dome mesh `bg/bg_*.x`).

A `<cloth>` element: `cloth="1"`, `wind` (0 0 10 or 0 0 -10), `width` and `height`
(particles, 3 to 8), `fWidth`, `fWidth2` (top and bottom widths), `fHeight`,
`translation`, and the names of the objects in the same file that pin its
corners: `lu`, `ru`, `ld`, `rd` (left or right, up or down). **Inferred**. The
level holds the sail as width x height - 1 nested actors.

## 8. Particle systems (`Particles/*`)

Each file is `<particles>` with one or more `<particle name texture type ...>`
(spaces around `=` are legal). Ten `type`s. 11 files are named by nothing in the
object XML, so the executable chooses them (**inferred** users in the last
column). All textures exist except `Particles/ParticleRoket.png` (`Tornado`);
names are case-insensitive references (`Particlered.png` is `PARTICLEred.png`).

| File | `type`, key parameters | Used by |
|---|---|---|
| `Oreolblue`, `Oreolred` | two `Oreol` sprites: a halo spinning at `speed_angular` 3.5, and `OreolShadow` (`shadow_plane` y 1, 0.1 above the floor) at -1.5; textures `Oreols/02`+`07` (blue), `11`+`09` (red) | the prize halos (`oreol`) |
| `Trail` | `Trail`, 30 particles, fade 500 ms, size 2.5, colours `0x00e2e2ff` to `0x007070ff` (`TrailBlue`) or `0x00ff8040` (`TrailOrange`) | `fire-rocket` (`particles1="trail"`); which variant, and any other use, is by code |
| `TrailMeteor` | `Trail`, size 5, red | `meteor` |
| `explosebig`, `explosesmall` | `Flash`, size 25 and 15, fade 500 ms, speed 250 | dynamite |
| `exploseball`, `exploseyadro`, `explosemeteor` | `Flash`, size 10, speed 250, 150, 50 | by code: explosive ball, cannonball, meteor |
| `Flash` | `Flash`, size 30, fade 200 ms | by code |
| `Fireworks` | `Firework`, 6 variants (`SimpleFirework`, `red`, `yellow`, `green`, `blue`, `magenta`), 30 particles, radius 10, speed 30, fade 500 ms | fire-rocket bursts, firework objects |
| `Laser` | `Laser`, yellow to red, speed 150, fade 1500 ms | the laser bolt |
| `Lightning` | `Lightning`, 10 segments, `shift_coeff` 0.5, `shake_coeff` 0.75, colour `0x00eeeeFF` | Thunderbolt |
| `rubber` | `Laser`, green to blue, speed 0, fade 1000 ms | the YoYo string |
| `LeafFall` | `LeafFall`, 15 leaves, `Leaf.png`, spread 3 x 2 x 3, height 6 | trees when hit |
| `Smoke` | `Smoke`, 50 particles, height 3, `ParticleBubble.png` | flickers (`particles2`) |
| `Spark`, `SparkKoster` | `WaterBubble`, 10 particles, radius 0.5 / 2, `Particlered.png` | torch, cauldron (`particles1`) |
| `WaterBubbles` | `WaterBubble`, radius 8, `Bubble2.png` | sea levels (**inferred**) |
| `Tornado` | `Tornado`, **1,500** particles, radii 5 to 50, height 160, size 20, `ParticleRoket.png` (missing) | Wind prize |

## 9. Materials: what the columns do

`materials.json` has 237 materials (236 distinct names); levels use 180 of the
names. Lookup is by name,
case-insensitively, first definition wins (`meteor` is the only repeat).

| Column | Values (all materials) | Observed meaning |
|---|---|---|
| `brickType` | **0** 213, **4** 17, **2** 6, **5** 1 (no 1 or 3) | 2: not a brick. The six are `BallMaterial`, `ball.jpg`, `bulletMaterial`, `BoardMaterial`, `Big-Water.jpg` and `Default`: ball, bullets, board, the fallback. 5: `Tree`, the trunk (energy 5, leaf particle). 4: ground tiles (`trava`, `trava-ground`, `Ground-2*`, `plitka`), rocks (`Skala`), crates (`Box.jpg`) and the 10 animals. 0: everything else, including characters, buildings, lava and water tiles. Level usage: 0 on 10,356 objects, 4 on 2,771, 5 on 128, 2 on 83 (the boards) |
| `brickEnergy` | 1: 130, 2: 25, 3: 77, 4: 4, 5: 1 | hit points, **inferred**. Level bricks: 1 for 9,329, 3 for 2,173 (characters, animals, 3 ground types), 2 for 483, 4 for 342. How much damage a hit does is **unknown** |
| `brickScore` | 4 to 777, median 60 over the 237 materials (42 over the scored level objects) | points when destroyed. Level totals (including `vibro` objects) run from 1,422 to 18,733 (median 7,191); `piratos-ostrovok` 4,231 over 65 objects, `cowboys-saloon` 3,986 over 88 |
| `exploseType` | 5: 93, 6: 67, 3: 59, 0: 10, 2: 4, 9: 3, 8: 1 | the way the object breaks, an enumeration in code. **6**: shatters into splinters (66 of 67 have a `splinter` mesh): buildings, ground, crates, metal. **5**: no splinter (5 of 93 have one): characters, animals, trees, interactive objects (cannon, portals, generators, spring, volcano, fireworks). **3**: prizes, `bat_*` materials, ship hull and sails (11), plus a few. **8**: dynamite. **9**: fire rocket, meteor, thunderbolt. **0 / 2**: nothing (system, board, light, Default) |
| `exploseDelay` | 0 to 500 ms; 500 for the prizes and `bat_*` materials, 0 to 250 for most bricks (500 on `Oblaka.jpg`, `MillKrisha` and `MillWindow`) | section 6 |
| `explosePower` | 6.789 (72) or 1.0 (165) | section 6: unused |
| `splinter` | 15 distinct meshes on 77 materials; `splinter-brown.x` 25, `splinter.x` 15 | the debris mesh that flies out; 160 materials have none. 4 more splinter meshes exist that no material names |
| `sounds` | `dead` 214, `hit` 182, `drop` 157, `period` 46, `loop` 1 | `drop`: lands after a fall; `hit`: struck; `dead`: destroyed or (prizes) collected; `period`: periodic or fired (generators, volcano, bullets, animals); `loop`: the spring |
| `physics` | restitution 0.5 for 131, 0.3 for 61 | PhysX combine mode 0 (average) for 226 of 237; `min` for restitution on 5 (rocks, bank, safe, shark), `max` on 5 (the observation wheel, a cart wheel) |

Prefer these facts when classifying: every level `brick` has a material in
`materials.json` except `basket.jpg` (1 object) and two with none, which take
the default; `Default` is `brickType` 2, energy 1, score 10.

## 10. Bricks versus scenery

**Data.** Level object types and counts (83 levels): `brick` 12,330, `metall` 452,
`tree` 128, `dynamite` 116, `board` 83, `background` 83, `shadowcaster` 63,
`firework` 63, `generatorbad` 26, `vibro` 23, `cloth` 22, `spring` 21, `cannon` 21,
`generatorgood` 17, `flicker` 14, `volcano` 13, `teleportsrc` 12, `teleportdest` 10,
`light` 2. Per class:

- **Markers, never targets** (**inferred**): `board` (kinematic, all axes frozen,
  material `BoardMaterial`, type 2), `background`, `shadowcaster`, `light` (frozen
  marker spheres in group 5, which overlap solid shapes in the saved poses, so
  they cannot collide). `background` and `shadowcaster` have no material in
  `materials.json`; `light` names `lightMaterial` and its siblings, which carry
  placeholder values (energy 1, score 250).
- **Objects with a brick-style material** (energy, score): `brick`, `tree`,
  `dynamite`, `firework`, `cannon`, `generatorgood/bad`, `spring`, `volcano`,
  `teleport*`, `cloth`, `metall`: all of brick type 0 or 5 with a positive score.
- **Kinematic bricks**: 2,592 of the 12,330 bricks (body flag `0x80`). They are
  the *floor* (`Ground-2-ground` 686, `plitka` 521, `Lava.jpg` 415, `Ground-2.jpg`
  380, `Water-brick` 185, `Fence` 155), plus path-following sharks and birds
  (flag `0x01`, which move: `LEVEL_FORMAT.md` "Path motion"), and buildings or apples in a level or two. Kinematic is **not**
  "scenery": the sharks have score 91 and the tiles scores 10 (`Lava.jpg`) to 44.
- **Brick type 4 that is dynamic**: 1,174 objects: ground tiles that are loose
  (`Ground-2.jpg` 231), crates (175), sheep (121), rocks (98), and the animals.

**Not in the data**: the rule for finishing a level and what counts as a target.
The only classification signals are `type` (object), `brickType` (material),
the kinematic flag, `group` (2 plays, 12 metal) and `score`. They disagree
with each other as a "target" rule: lava and water tiles are type 0 while other
floors are 4; sharks are kinematic yet scored.

Two hypotheses cheap to test on footage, each a single predicate:

- **H-dyn**: a level ends when every *dynamic* `brick` (and `tree`) is destroyed;
  kinematic floors never count. Predicts that `knight-castle` ends with its 84
  kinematic `plitka` tiles and 12 water tiles still standing. Refuted by the
  footage: its natural end shows an empty board.
- **H-type**: a level ends when every `brick` whose `brickType` is 0 is
  destroyed; animals, crates and ground (type 4) are optional bonus points.
  Predicts `village-wolf-sheeps` can end with sheep and crates standing, and
  counts lava and water tiles.

Build it as one predicate `isTarget(object)` so the rule is a one-line change.
`metall` (group 12) and `dynamite`/`firework` are the next questions: whether the
ball can break metal, and whether dynamite counts.

**Footage answered it** (`GAMEPLAY.md`, "Rules settled by the second study", 3 and 7):
H-type is refuted (a level did not end before its type 4 gorilla was destroyed) and so
is H-dyn: the kinematic floors count. The required objects are every `brick` of material
type 0 and 4 and every `tree`, whatever its motion (level 23 ended with the last of its
kinematic ground blocks, level 65 with all of its tiles gone). `metall` and `teleport*`
objects stayed on the board at natural ends (and one `cannon`), so they are not required;
metal is destructible (blasts, +50 each) but most of it survives a level. `dynamite`,
`firework`, `generator*`, `spring`, `volcano`, `cloth` and `vibro` never decided an end that
was seen; they are not required here (they are not bricks), and they can still be
destroyed for their points. The game's `TargetRule::Bricks` is this predicate, and the
default.

## 11. What the data cannot tell you (code-only), and checks for footage

**Unknown** (the executable holds them): the ball's speed and its change per hit,
the bat's width, speed and starting position; prize drop chance and which prize;
every prize's duration or factor; lives at the start (the screenshots show 3 and
5); weapon rate of fire, ammunition and damage; hit damage versus `brickEnergy`;
generator and volcano rate; spring kick; how portals pair; the level-complete and
lose rules; whether shot bricks drop prizes (`disablePrize` implies they can).

Footage checks, each answered by watching one level:

1. Does the ball's rebound angle depend on where it hits the bat (a sphere r = 3)?
2. Level end: does it fire with floor tiles, crates or animals left? (section 10)
3. How many hits does a `brickEnergy` 3 character take from the ball? From a
   bullet, rocket, dynamite?
4. Which prizes show an icon and for how long; is YoYo the red `Rubber` icon?
5. Does the ball go through `metall` and how does it bounce off it?
6. Do bricks destroyed by dynamite or the thunderbolt drop prizes?
7. Does one portal exit serve two sources (`knight-zoo`)?

## 12. Data quirks and how to handle them

| What | Where | Handling |
|---|---|---|
| `<mesfh name="arbuz.x">` | the 3 `Lights/*.xml` | **ignored by the original**: the levels' 2 light objects have object flags `0x202` (material, no mesh). Ignore it |
| `<m1esh name="apple-tree.x" frameNode="Cylinder01">` | `Trees/apple-tree.XML` trunk, which already has `<mesh name="Tree.x">` | **ignored by the original**: all 128 `tree` objects in the levels are `tree.x` with no frame, including those in the three apple-tree levels |
| `rotat1ion` on the sphere | the 3 `BackGrounds/*.XML` | **ignored by the original**: all 83 level backgrounds have an identity shape rotation. Ignore it |
| `<spher1e name="Pushka-Puli_ph_Sphere01">` | `Interactive/yadro.XML`, 2nd sphere, 3 units ahead of the cannonball, copied from `kernel.XML`; the file also has a commented-out object | **probably ignored** (no level can show it). Use one sphere r 0.591 |
| `<b1ox name="c-wagon_ph_Box02" dimensions="4.8 3 3">` | `new9/c-wagon.XML`, a solid box that fills the open wagon already built from 5 boxes | **probably ignored** (the wagon is in no level) |
| two identical `<Sphere radius=".6">` | `Balls/ball.xml` | use one |
| two identical capsules, no `name`, no mesh | `Bullets/lazer.XML` | use one |
| `description="SpeedUp"oreol=` | `Prizes/speedup.xml` | lenient parse; the one syntax error (the `"` is followed directly by `oreol`) |
| Cyrillic comments in code page 866 | 4 `Sea/Ships/*` files | decoded; comments only |
| 48 of 2,629 `rotation` quaternions are not unit length | `c-saloon` (w 1.10), `osminog`, `melon-tree`, `well`, `penek` (w 4.33), `endlevel`, `life`, `night`, `fireworks`, `meteor`, `thunderbolt`... | normalise on load. The levels hold proper matrices, so (**inferred**) only the bat, ball, prize and bullet shapes are read from XML at run time |
| `dimensions` are full extents, `height` of a capsule the cap-to-cap distance | all shapes | a level box's `halfExtents` = `dimensions / 2` |
| `Extra`/`extra`, `Object`/`object`, `SkinMesh`/`mesh`, `Shapes`/`shapes` | everywhere | tags are lower-cased by the converter; look attributes up case-insensitively. `skinmesh` and `mesh` both name a file under `Data/SkinMeshes/` |
| `<file name="Data/Objects/Grass/...">` | 4 compound fence files | the folder is now `Village/Builds`; the level files do not need these |
| `Particles/ParticleRoket.png` | `Tornado.xml` | no such texture was shipped: the tornado has no sprite; pick any |
