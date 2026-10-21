# Converted asset formats

The runtime never reads the original DirectX `.x` meshes, `materials.xml` or DDS
textures. `tools/convert_assets.py` turns them into the formats below, inside
the `assets/` folder built from your own copy of the game.

All coordinates stay in the original Direct3D 8 space: left-handed, +y up,
+z away from the player, row vectors (`p' = p * M`). Nothing is mirrored or
flipped, so level poses, physics shapes and meshes line up as they did in the
original. (A generic glTF viewer, which assumes right-handed space, therefore
shows the meshes mirrored along z. That is expected.)

## Meshes: `assets/Data/Meshes/**/*.glb`

One binary glTF 2.0 file per `.x` file in `Data/SkinMeshes/`. The path is the
`.x` path relative to `SkinMeshes/`, lowercased, with `.glb` instead of `.x`:

| Original | Converted | Level `mesh` string |
|---|---|---|
| `SkinMeshes/cannon.X` | `Meshes/cannon.glb` | `cannon.x` |
| `SkinMeshes/bg/bg_day.X` | `Meshes/bg/bg_day.glb` | `bg/bg_day.x` |
| `SkinMeshes/Prizes/cannon.X` | `Meshes/prizes/cannon.glb` | `prizes/cannon.x` |

So the runtime finds a level's mesh by lowercasing its `mesh` string and
replacing the extension.

Written by `tools/x2glb.py` (parser: `tools/xfile.py`):

- **Nodes**: one node per `.x` `Frame`, same hierarchy and order. `name` is the
  frame name (omitted if the frame is unnamed). `matrix` is the 16 floats of
  `FrameTransformMatrix` copied in file order (omitted when identity). That
  order is glTF's column-major layout of the same transform, so the only
  difference from standard glTF is handedness. A `Mesh` outside any frame gets
  a synthetic unnamed root node (only `melon.X` does this). The scene's root
  nodes are the top-level frames in file order. Frame names are not unique:
  eight files repeat a `ph_*` name (`house3.X` has seven `ph_Box02`), so a
  lookup by name finds the first.
- **Meshes**: a frame's mesh is one glTF mesh. It has one primitive per
  material used, and every `Mesh` object directly inside the frame is merged
  into it.
- **Primitives**: `mode` 4 (triangles), `indices` always `UNSIGNED_INT`,
  attributes `POSITION`, `NORMAL` (unit length) and `TEXCOORD_0`. UVs keep the
  Direct3D convention, (0, 0) at the texture's top-left, which is the same as
  glTF's. Polygons with more than three corners are fan-triangulated
  `(v0, vi, vi+1)`, keeping the original winding: **clockwise front faces**.
  `.x` files index normals separately from positions, so a vertex is a unique
  (position index, normal index) pair. Missing normals are computed; missing
  UVs are zero. A primitive's `material` is omitted when the `.x` mesh has no
  material list.
- **Materials**: one per `.x` `Material`, with the same `name`.
  `pbrMetallicRoughness.baseColorFactor` is the `faceColor` (RGBA),
  `metallicFactor` is 0 and `roughnessFactor` is 1. When the `.x` names a
  texture, `baseColorTexture` points to an image whose `uri` is the texture
  file's path relative to the `.glb`, URI-encoded, e.g.
  `../../Textures/Pushka.jpg`. The file is found case-insensitively in
  `assets/Textures/`, root first, then subfolders, falling back to the same
  stem with another extension (`.png` preferred; `.dds` is never chosen, the
  runtime decodes JPEG and PNG only). `extras`:
  `{"power": f, "specular": [r,g,b], "emissive": [r,g,b], "textureFile": "Pushka.jpg"}`,
  plus `"textureMissing": true` if the file wasn't found (and then no
  `baseColorTexture`). Three names in the shipped meshes match no file:
  `Ground-2.jpg` (ten ground meshes; `materials.json` has a material of that
  name whose textures are `Ground-2-0` to `Ground-2-2`), `derewo fon2.jpg`
  (`c-trees.X`; the file is `derewo_fon2.jpg`) and `w.jpg` (`water.X`).
  `house2.X` has two different materials called `Material__1`; its meshes
  refer to the first.
- **Animations** are not glTF animations. They are kept losslessly in the
  root `extras.animations` for later use:
  `[{"name": set name, "ticksPerSecond": n, "tracks": [{"node": frame name,
  "rotation": [[tick, w, x, y, z], ...], "scale": [[tick, x, y, z], ...],
  "position": [[tick, x, y, z], ...], "matrix": [[tick, m0..m15], ...]}]}]`.
  Only the key types present are written. `ticksPerSecond` comes from
  `AnimTicksPerSecond`, or 4800 (the D3DX default) if the file has none. In
  the shipped files no mesh has `AnimTicksPerSecond`, every track holds
  `matrix` keys only, and 184 of the 264 sets have no tracks at all.
- Root `extras`: `{"source": "SkinMeshes/cannon.X", "coordinateSystem": "d3d-left-handed", "animations": [...]}`.
  `animations` is always present, `[]` when the file has no animation sets.

## Materials: `assets/Data/Materials/materials.json`

`Data/Materials/materials.xml`, converted by `tools/materials.py`:

```json
{
  "version": 1,
  "texturePreload": ["Icons/bazuka.png", "..."],
  "materials": [
    {
      "name": "shark.jpg",
      "group": "Levels/Sea",
      "textures": ["shark"],
      "animated": false,
      "animTimeMs": 500,
      "density": 0.01,
      "splinter": "splinter-blue1.x",
      "d3d": {"diffuse": [1, 1, 1, 1], "ambient": [0.75, 0.75, 0.75, 1],
              "emissive": [0, 0, 0, 1], "specular": [0, 0, 0, 1], "power": 0},
      "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.001,
                  "frictionCombineMode": 0, "restitutionCombineMode": 1},
      "brick": {"type": 0, "energy": 1, "score": 91, "exploseType": 3,
                "exploseDelay": 250, "explosePower": 1.0},
      "sounds": {"drop": "shark_drop", "hit": "shark_hit", "dead": "shark_dead"},
      "renderStates": [[19, 5], [20, 6], [27, 1]]
    }
  ]
}
```

- Materials appear in file order. Names may repeat; the runtime uses the
  first one.
- `group` joins the enclosing `<Materials name="...">` names (minus the
  outermost "Materials") with `/`.
- Colours are RGBA. The XML stores them as `A R G B`.
- Optional keys are omitted when absent: `splinter`, `sounds`, `renderStates`
  and keys inside `sounds` (`drop`, `period`, `hit`, `dead`, `loop`). When a
  material has several `<Sounds>` elements, their attributes are merged and
  later ones override.

## Sounds and music

`assets/Sounds/*.ogg` and `assets/Musics/*.ogg` (the contents of `Sounds.dat` and
`Musics.dat`) are Ogg Vorbis and are used as they are: 165 sound files (127 mono,
38 stereo, all 44.1 kHz) and 5 music tracks (stereo; `menu`, `village` and
`pirates` at 44.1 kHz, `cowboy` and `knights` at 32 kHz). Some file names are
upper case (`BALLhitbat.OGG`) while the XML spells them in other cases.

`Data/xml/sounds.xml` and `musics.xml` are converted by `tools/sounds.py` into
`Data/xml/sounds.json` and `musics.json`:

```json
{"version": 1,
 "sounds": [
  {"name": "pirat_period", "samples": [{"file": "pirat_period1.ogg", "delayMs": 30000},
                                       {"file": "pirat_period2.ogg", "delayMs": 35000}]},
  {"name": "ballHitBoard", "samples": [{"file": "ballhitboard.ogg", "freqStart": 0.7, "freqEnd": 1.1}]},
  {"name": "pirat_drop", "samples": []}
 ]}
```

```json
{"version": 1, "musics": [{"name": "Cowboys", "file": "cowboy.ogg"}]}
```

- `sounds` appear in file order; `name` keeps the XML's spelling, and the runtime
  matches names without regard to case (menus say `menustonedown`, the XML
  `MenuStoneDown`). A name defined twice (`meteor` and `thunderbolt_dead`, the
  second time without a delay) keeps its first definition.
- A sound plays one of its `samples`, picked at random. `file` is the name of
  the file in `Sounds/` as it is spelled on disk (found ignoring case). `delayMs`
  (omitted when 0) is how long after that sample starts the sound may play again.
  `freqStart` and `freqEnd` (both or neither; the XML's single bound is copied
  to the other) are the playback speed multiplier's range, drawn at random for
  each play; 1 is normal pitch, 2 an octave up.
- Samples whose file does not exist are left out and reported. 114 of the 315
  samples in `sounds.xml` name a file that was never shipped (almost every
  `*_drop`, many `*_hit`, `bullet_dead`, `rocket_dead`, `thunderbolt`, `money`,
  ...). A sound left with no sample is still listed, with `"samples": []`: the
  original knew the name and played nothing, and so does the runtime.
- `musics` are in file order. A track whose `Stream` file is missing would be
  left out and reported (all five exist).
- `sounds.xml` has no XML declaration and its comments are Russian in code page
  866, which is not UTF-8; the converter reads it that way.

Materials name sounds by plain `sounds.json` names (`shark_hit`, `knight_period`);
there are no prefixes to extend with a number: the `1` and `2` in
`knight_period1.ogg` and `knight_period2.ogg` belong to the two samples of
`knight_period`. 23 of the 600 sound names in `materials.json` (18 different
ones) are not in `sounds.xml` at all, for example `train_period`, `wood_hit`,
`cannon_hit` and `jaba_drop`; `village_roof_drop` is spelled `village_roof_frop`
in the XML. The runtime treats all of them as silent.

## Textures

JPEG and PNG files are used as they are, from `assets/Textures/` (the
contents of `Textures.dat`). Every `.dds` file (in `Textures/` and
`Data/Fonts/`) is also converted to a `.png` beside it, with the same name, by
`tools/textures.py`, so the runtime only ever decodes JPEG and PNG.
Game materials name textures without an extension (`"Pushka"`, `"01"`). The
runtime resolves them with `TextureIndex`.
