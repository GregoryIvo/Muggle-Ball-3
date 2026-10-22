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
- **Animations** are glTF animations (`animations`), one per `.x` `AnimationSet` that moves a frame: `name` is the set's
  name (`Anim-1` in all but `bat.x`, whose sets are `Common`, `Hit`, `Right` and `Left`), and each `Animation` of the set
  is a channel for the frame's translation, rotation and scale (`LINEAR` samplers sharing the key times). Times are in
  seconds: the keys' ticks over `AnimTicksPerSecond`, or 4800 (the D3DX default, 160 ticks a frame at 30 frames a
  second) if the file has none, which none of the shipped files has. Their keys are all `matrix` keys; each is taken
  apart as D3DX does when it loads one (the scale is the length of each axis, the rotation what is left once the axes
  are made square to one another, so a shear is dropped: the cannon's `Pushka` recoils with one, and the shipped files'
  worst key is 0.4 off in one element of a matrix with a scale of 10). A rotation is stored as a quaternion `x, y, z,
  w` in the same handedness as everything else, neighbours on one side so that the interpolation takes the short way.
  `scale` and `position` keys convert too; a `rotation` key (a quaternion in the .x's own convention) is skipped with a
  warning: no shipped file has one. A tick written twice keeps its last key. The frames an animation moves have
  `translation`, `rotation` and `scale` instead of a `matrix` (glTF wants that; the transform is the same to within the
  shear of the largest, 0.01 in one element, in `Prizes/scull.X`), and a frame whose matrix cannot be taken apart (a
  flat axis) is left alone with its animation skipped. 184 of the 264 sets in the shipped files have no tracks and are
  not written; 80 are, in 77 files. The engine reads them as `Model::animations` (`ARCHITECTURE.md`).
- Root `extras`: `{"source": "SkinMeshes/cannon.X", "coordinateSystem": "d3d-left-handed"}`.

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

## Objects and particles: `assets/Data/Objects/**/*.json`, `assets/Data/Particles/*.json`

Every `.xml` / `.XML` under `Data/Objects/` (348 files: bricks, prizes, bullets,
the bat and ball, interactive objects...) and `Data/Particles/` (20 files) is
converted by `tools/objects.py` into a `.json` file with the same name beside it.
What the definitions mean for gameplay is in [OBJECTS.md](OBJECTS.md); this
section only describes the JSON.

The JSON is a *generic element tree*, not a schema. The XML was written by hand
and by 3ds Max scripts and has no fixed grammar, so nothing is interpreted
beyond typing the attribute values:

```json
{
  "version": 1,
  "source": "Objects/Prizes/speedup.xml",
  "encoding": "utf-8",
  "repairs":  [{"line": 9, "kind": "missing-whitespace", "detail": "..."}],
  "suspects": [{"line": 6, "kind": "tag-typo", "detail": "..."}],
  "comments": ["<object name=\"GeoSphere01\" ...>"],
  "root": {"tag": "root", "children": [
    {"tag": "objects", "children": [
      {"tag": "object", "attrs": {"name": "speedup", "type": "Prize"}, "children": [
        {"tag": "shapes", "attrs": {"id": "1968554979773748886"}, "children": [
          {"tag": "box", "attrs": {"name": "Fast-1_ph_Box01", "dimensions": [2.3, 2.0, 2.0],
                                   "rotation": [-0.0, -0.0, -0.0, 1.0], "translation": [0.0, 1.0, 0.0]}}]},
        {"tag": "mesh", "attrs": {"name": "Prizes/speedup.x"}},
        {"tag": "extra", "attrs": {"description": "SpeedUp", "oreol": "oreolred"}}]}]}]}
}
```

- `version` is `OBJECTS_JSON_VERSION` in `tools/objects.py`. `source` is the
  XML's path relative to `Data/`, with its original spelling. `encoding` is how
  the bytes were decoded. `repairs`, `suspects` and `comments` are present only
  when non-empty.
- `root` is the document element, whatever it is (`root`, `particles`, or
  `object` in `Exploses/explose.xml`). An element is
  `{"tag", "attrs"?, "raw"?, "text"?, "typoOf"?, "attrTypoOf"?, "children"?}`,
  children in document order. Order matters in the compound files (see
  OBJECTS.md, `file`).
- **Tags are lower-cased** (`<Object>`, `<object>` and `<OBJECT>` are one tag;
  the shipped files mix them, and the game must have read them alike).
  **Attribute names keep their case** (`disableGravity`, `Diffuse`, `frameNode`):
  look them up case-insensitively. Text is kept in `text`, stripped, only when
  an element has any (no shipped file does). Comments are not part of the tree;
  they are listed in `comments` because `yadro.XML` hides a whole disabled
  object in one.
- **Attribute values are typed where that cannot be wrong**, otherwise they stay
  strings, byte for byte (entities decoded, tabs and newlines turned into
  spaces):
  - a number, `"3"` becomes `3`, `".6"` becomes `0.6`, `"12."` becomes `12.0`;
    text with a leading zero (`"007"`) or more than 15 digits stays a string;
  - two or more numbers separated by spaces become a list, `"0 0 -10"` becomes
    `[0, 0, -10]`;
  - `rotation` (a quaternion, stored `[x, y, z, w]`), `translation`, `dimensions`,
    `velocity`, `axis`, `anchor`, `direction`, `shadow_plane` and
    `shadow_position` are always lists of floats when they hold the right count
    of numbers (4 for `rotation`, 3 for the others). Otherwise they are typed
    like any value and the converter lists a `vector-size` suspect;
  - `yes` / `no` / `true` / `false` (any case) become booleans, and `0x...`
    becomes an integer. These two are the lossy cases: the original text goes in
    the element's `raw` map, `{"modulate_colour": "no", "colour1": "0x00e2e2ff"}`.
    `0` and `1` flags stay numbers;
  - the names `name`, `id`, `type`, `texture`, `data`, `frameNode`, `description`,
    `oreol`, `explose`, `shadowType`, `particles1`, `particles2`, `shootSound`,
    `object1`, `object2`, `lu`, `ld`, `ru` and `rd` always stay strings. `id` on
    `<shapes>` is a 64-bit number that does not fit a JSON double.
- `-0.0` appears in rotations (the XML writes `-0.00000`). It is a zero.

**Repairs** are things the parser had to fix to read the file. Every shipped
file converts; there are five repairs in all:

| Kind | Where | What was done |
|---|---|---|
| `encoding` | the four `Sea/Ships` files with a Russian comment (`boat-water`, `pirateship`, `-green`, `-white`) | not UTF-8; decoded as code page 866 (the comments say "Флаги", "Низ корабля", "Нос корабля"). Only comments hold the non-ASCII bytes |
| `missing-whitespace` | `Prizes/speedup.xml` line 9, `description="SpeedUp"oreol="oreolred"` | a space is assumed between the attributes |

The parser also copes with unquoted or valueless attributes, duplicate
attributes (the last wins), mismatched, stray or missing end tags (open
children are closed, a stray end tag is ignored), unterminated tags, comments
and quotes, text outside the root, several top-level elements (wrapped in a
`document` element), BOMs, declared encodings and `&amp;`-style entities. Each
case is reported under `repairs` with its line; the unit tests
(`tests/test_objects.py`) cover them with synthetic XML.

**Suspects** are things that look wrong but are *kept as written*, because
fixing them would change behaviour the original most likely had:

- `tag-typo` / `attribute-typo`: a name that becomes a known one when one
  character is deleted: `<b1ox>`, `<spher1e>`, `<m1esh>`, `<mesfh>`,
  `rotat1ion`. The element keeps its tag and gets `"typoOf": "box"` (or
  `"attrTypoOf": {"rotat1ion": "rotation"}`) so a loader can tell. In every case
  the shipped data is more consistent when the original ignored the element
  (OBJECTS.md, "Data quirks"), so a loader that only reads known tags behaves
  like the game.
- `unresolved-file`: a `<file name="...">` that matches no shipped file. The
  four `Village/Builds/fencelong*.XML` files point at `Data/Objects/Grass/Builds/`,
  which is now `Village/Builds/`.
- `vector-size`: a vector attribute without the right number of components.

`python3 tools/objects.py DATA_DIR` runs the conversion on its own and prints
every repair and suspect. `tools/convert_assets.py` runs it last.

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
in the XML. The runtime treats all of them as silent (`AUDIO.md` lists the five a
hit or a destruction would play, and why none is mapped to a near name).

## Textures

JPEG and PNG files are used as they are, from `assets/Textures/` (the
contents of `Textures.dat`). Every `.dds` file (in `Textures/` and
`Data/Fonts/`) is also converted to a `.png` beside it, with the same name, by
`tools/textures.py`, so the runtime only ever decodes JPEG and PNG.
Game materials name textures without an extension (`"Pushka"`, `"01"`). The
runtime resolves them with `TextureIndex`.

## Menus, strings and fonts: `tools/ui.py`

`tools/ui.py` (run by `convert_assets.py` after the textures, because it reads the font atlas PNGs) converts the
2D UI data. Nothing is written to the original files; each JSON file sits next to its source. The level list,
`tools/campaign.py`, is described after the menu forms.

### Menu forms: `assets/Data/Menu/<Name>.json`

One file per `Data/Menu/*.xml` (19 of them): the XML as a generic element tree. The root `<forms>` holds one
`<form>`, which holds `label`, `button`, `checkbox`, `slider`, `list`, `textedit` and `progress` elements, nested
(a panel holds its buttons, a slider its track and its - and + buttons). Elements are written exactly as they were,
unknown tags and attributes included (`options.xml` has one misspelt `<La1bel>`); what they mean is the loader's
job (`game/include/mb/game/UiForm.h`).

```json
{"version": 1, "tag": "forms", "attributes": {}, "children": [
  {"tag": "form", "attributes": {"name": "MainMenu", "font_size": 18, "font_color": [1, 1, 0, 1], "fade_time_msec": 400},
   "children": [
     {"tag": "button", "attributes": {"name": "Info", "pos": [0.33, 0.4], "size": [0.34, 0.098],
                                      "img_name": "forms/button.dds", "img_params": [1, 2, 0, 1], "text": "Info"},
      "children": []}]}]}
```

Attribute values are typed where the type is certain, by attribute name; everything else stays a string, so
`name="46"` and `text="1000"` are never numbers:

| Attributes | JSON value |
|---|---|
| `color`, `shadow_color`, `font_color` | `[r, g, b, a]`, floats 0 to 1, from `0xAARRGGBB`. Missing high digits are zero, as `strtoul` reads them: `0x0000f0f0` is transparent, `0x0` is transparent black |
| `tiled`, `no_fading`, `disabled`, `cycled` | boolean (`yes` / `no`) |
| `font_size`, `fade_time_msec`, `show_time_msec` | integer |
| `rotate`, `text_offset`, `mouse_over_offset` | number |
| `pos`, `size`, `size_pos`, `shadow_offset`, `font_shadow`, `press_offset`, `img_tile`, `img_translation` | `[x, y]` numbers (`"0. .5"` is valid in the files) |
| `img_params` | list of integers |
| `params` | list of numbers |

A value of a typed attribute that does not parse stays the original string, and `convert_assets.py` prints a
warning (the shipped files have none).

### What the attributes mean

Worked out from the data and the textures; the UI code follows it.

- **Units.** `pos` and `size` are fractions of a 4:3 screen (the game ran at 1024x768), **y counting up from the
  bottom**: `pos` is the bottom-left corner. They are absolute screen positions, not relative to the parent element:
  a panel's children are simply drawn after it. Offsets and font sizes (`shadow_offset`, `font_shadow`,
  `press_offset`, `mouse_over_offset`, `text_offset`, `font_size`) are in pixels of that 1024x768 screen.
  A `size` of `0 0` means one frame of the element's image, or no area for a label that is only text.
- **Drawing order** is document order, so an element is over its parent and over every earlier sibling. A slider's
  `img_name` is its knob, which goes on top of the track label inside it.
- **`img_name`** names a texture as the original did, `forms/button.dds`; the file is the PNG converted from it
  (`Textures/Forms/Button.png`), matched ignoring case and extension. A few forms use plain names (`splash.jpg`,
  `progress.dds`).
- **`img_params`** is `columns rows` of a sprite sheet, then one frame per state. Frames count down each column
  before the next one (`frame = column * rows + row`, which is how the 2x3 bat sheet `raketki` reads). States
  are normal, hover, pressed, disabled. `1 2 0 1` on a button is two rows: normal, then hover. `1 3 1 2 2 0` on a
  level button: normal 1, hover 2, pressed 2, disabled 0. A checkbox lists two: unchecked, checked
  (`2 1 1 0`: the cross frame, then the tick). With fewer frames than states the last repeats (an assumption: no
  shipped sheet needs it except pressed on the two-row buttons). The `Logo` labels of the stage forms are a single frame
  (`2 1 1`): the carved version.
- **Colours.** `color` tints the image; `font_color` colours the text and is not affected by `color`.
  `shadow_color` is the colour of the drop shadow of both: an image has one when `shadow_offset` is not zero, text
  when `font_shadow` is not zero. `font_name`, `font_size`, `font_color` and `font_shadow` are inherited from
  the element around, so the `<form>` sets them for everything.
- **`text`** is a key of the string table (`StartGame`) or literal text (`Quick Start`, `1000`, `Loading...`).
  `font_name` is one too: `FontName` is `bookworm.ttf` in English and `brushType-semiBold.ttf` in Russian.
  `text_align` puts the text relative to the element's rectangle: `center`, `center_multiline` (wrapped to the
  rectangle's width, from its top), `right` (beside the right edge), `above`, `under` (below the bottom edge; a negative
  `text_offset` moves it up). `text_offset` is the gap in pixels. `bottom` appears only on elements without text.
  A `list` does not draw its own `text` (`%s`); it is the format for its child label.
- **`mouse_over_offset`** is taken as how much the image grows on every side while the pointer is over it, and
  **`press_offset`** as how far the image and text move while it is pressed. **`rotate`** is degrees about the
  centre, positive clockwise on screen (checked against the arrows of the level maps, which then follow the paths).
  **`slider`**: `pos`/`size` is the run of the knob's left edge, `size_pos` the knob's size, `params` is
  minimum, maximum, step. **`progress`**: `params` is `0 10 1 0.3`; the image is shown from the left up to the
  fraction done (the fourth number is not understood and not used). `disabled="yes"` on the red bat marks it as
  the selected one.
- **`user_data`** says what the game does with the element: `Navigate` (the element's `name` is the form to show:
  the file of that name in `Data/Menu`, ignoring case), `SelectLevel` (the `name` is the level's index, 0 to 82 over
  the four stage forms), `SelectBat`, `ApplyVideo`, `ApplyRecord`, `OnlineScore`, `FinishGame`.
  `sound_mouse_in` and `sound_mouse_down` name `<Sound>` entries of `Data/xml/sounds.xml`, ignoring case.
  What each button does and how the screens lead into each other is in the "Menus and screens" part of
  `docs/ARCHITECTURE.md`. Buttons with no `user_data` are told apart by `name`: `Exit`, `QuickStart`, `LocalScores`,
  `PrevPage`, `NextPage`, the `-` and `+` of a slider or list, and the stage emblem `Logo` (which only plays its sound).
- **`fade_time_msec`** is how long the form takes to fade in (400); elements with `no_fading="yes"` (the logo) stay
  opaque meanwhile. **`show_time_msec`** (0 on the two forms that have it) is taken as the least time the form stays up.
- **`img_tile`** and **`img_translation`** are on the `background` form's sky only (`1 1` and `3. 0.`): the picture
  repeats `img_tile` times across the element and slides by `img_translation`; taken as hundredths of the picture's
  width and height a second (the unit is a guess: 3 gives a full turn in about 33 seconds). `tiled` is always `no` in
  the shipped files (the loader tiles the image at its own size when it is `yes`).
- **`<La1bel name="Fullscreen">`**, the one misspelt tag, is Options' fullscreen switch: it has a checkbox's sprite sheet
  and sounds, so it is loaded as a checkbox (the original most likely skipped it as an unknown tag).

### Level list: `assets/Data/xml/levels.json`

`tools/campaign.py` (run by `convert_assets.py` last) converts `Data/Levels/levels.xml`, the game's list of its 83
levels, to JSON. It is the only file that says which levels exist in which stage and in which order the menus number
them; it sits in `Data/xml`, not `Data/Levels`, where every `.json` is taken for a level.

```json
{"version": 1, "stages": [
  {"name": "Pirates", "levels": ["piratos-ostrovok", "piratos-birthday", "Piratos-Chyu-u-dishe", "..."]},
  {"name": "Village", "levels": ["village-apple", "..."]}, {"name": "Cowboys", "levels": ["..."]}, {"name": "Knights", "levels": ["..."]}]}
```

- A stage is a run of levels with the same `musicName` (a `<Music name>` of `musics.xml`): Pirates 20 levels, Village
  26, Cowboys 17, Knights 20. The order of the file is kept; a level's **campaign index** is its place in the whole
  list, 0 to 82, and is what the stage forms' level buttons are named with (`stagepiratos` has 0 to 19, `stagefarm`
  20 to 45, `stagecowboys` 46 to 62, `stageknights` 63 to 82).
- A level is its file name without `.mb3`, in the case the XML writes it (`piratos-SOS`, where the file is
  `Piratos-SOS.mb3`): look it up ignoring case.
- That is all the data says about the order. It is not the order the original plays in: the screenshot of the
  Saloon in `reference/` shows `LEVEL : 62`, while the saloon is the twelfth cowboys level here, number 58. Within a
  stage the file is alphabetical, except that the first pirates level, `piratos-ostrovok`, is first (it is `LEVEL : 1`
  in the other screenshot).

### String table: `assets/Data/xml/languages.json`

```json
{"version": 1, "current": "English", "languages": [
  {"name": "English", "codepage": "cp1252", "strings": {"FontName": "bookworm.ttf", "Loading": "Loading...", "...": "..."}},
  {"name": "Russian", "codepage": "cp1251", "strings": {"Loading": "Загрузка...", "...": "..."}}]}
```

From `Data/xml/languages.xml`. The XML declares no encoding and holds Russian as Windows-1251 bytes, so each
language's strings are decoded with its code page (`codepage`; Russian is `cp1251`, everything else `cp1252`)
and written as UTF-8. `current` is the XML's `CurrentLanguage`. Keys are case-sensitive and may contain spaces
(`Drunken Ball`); strings keep their printf placeholders (`POINTS : %d`). A key defined twice keeps the last
definition (`convert_assets.py` reports duplicates; the shipped file has none).

### Font atlases: `assets/Data/Fonts/<face>_<size>.json`

The game cached its fonts as pre-rendered atlases, `<face>_<size>.crd` plus `.dds` (the DDS becomes
`<face>_<size>.png` as for any texture): five of them, bookworm 20, 23 and 30 and brushType-semiBold 17 and
20. A `.crd` is 4096 bytes: 256 glyphs, each four little-endian float32 `u0 v0 u1 v1`, the glyph's cell in the
atlas as fractions of its width and height with v running **down** from the top row like the PNG. The cells
are whole pixels, tightly packed (no gaps or padding), all as tall as the font's line height and as wide as the
glyph's advance. The glyphs were laid out in reverse order, which is why the atlas looks back to front;
the table is what matters.

```json
{"version": 1, "face": "bookworm.ttf", "size": 20, "texture": "bookworm.ttf_20.png",
 "atlas": {"width": 512, "height": 512}, "lineHeight": 34, "codepage": "cp1252",
 "codepoints": [0, 1, "...", 126, 127, 8364, null, 8218, "..."],
 "glyphs": [[x, y, width, height], "... 256 rectangles in pixels ..."]}
```

`glyphs[i]` is the cell of glyph `i`, the byte value `i` of the font's code page. `codepoints[i]` is the Unicode
code point that byte stands for (null where the code page has none), so the runtime needs no code page tables:
`codepage` is cp1251 for the face the Russian language names (`brushType-semiBold.ttf`) and cp1252 otherwise, which
matches the atlases (the brush atlases hold the Cyrillic letters at bytes 0xC0 to 0xFF). `size` is the number in
the file name. The sizes line up with the forms' `font_size` N at 1.25 N (16 and 20, 18 and 23, 24 and 30, and 14 and
17 for the brush face), as if the atlases were cached by a run at 1280x960 instead of the 1024x768 the forms are laid
out for; so the UI draws N from the atlas nearest 1.25 N at scale N / size (0.8 for an exact match). The cell height
is about 1.7 times `size` for bookworm and 1.5 times for brushType. `bookworm.ttf_23`'s atlas is a few rows too short
for its first, unprintable glyphs, which the converter cuts off at the edge.

## App icon: `assets/AppIcon.icns`

macOS only, and not read by the game: `tools/make_icon.py` builds it, `tools/convert_assets.py` runs that
last (skipped off macOS or if the icon cannot be read), and CMake copies it into the app bundle as
`Contents/Resources/AppIcon.icns` (with `CFBundleIconFile`) when the file exists. Without it the app has
the default icon.

The icon is the one in `MagicBall3.exe`: PE resources, `RT_GROUP_ICON` "IDI_APPICON" (the first icon group)
and the `RT_ICON` images it lists, which are 48, 32, 24 and 16 pixels, 32-bit with alpha. The resource
directory is intact even though the executable is Alawar's trial wrapper, and the icon data sits in a
different section than the directory, so every RVA goes through the section table. The `.ico` files in the
install folder are Alawar's, Gamextazy's and the installer's logos, not the game's; `--ico` reads one if an
executable's resources ever cannot be parsed.

`make_icon.py` writes an `.iconset` (16 to 512 pixels at 1x and 2x) and runs `iconutil -c icns`. Sizes the
icon has (16 and 32) are used as they are. The rest (64 to 1024) are resampled from the 48 pixel image with a
Lanczos filter on alpha-weighted colour, so the 1024 pixel icon is soft: there is nothing sharper in the
game's files.
