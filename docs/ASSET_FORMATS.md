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

## Textures

JPEG and PNG files are used as they are, from `assets/Textures/` (the
contents of `Textures.dat`). Every `.dds` file (in `Textures/` and
`Data/Fonts/`) is also converted to a `.png` beside it, with the same name, by
`tools/textures.py`, so the runtime only ever decodes JPEG and PNG.
Game materials name textures without an extension (`"Pushka"`, `"01"`). The
runtime resolves them with `TextureIndex`.

## Menus, strings and fonts: `tools/ui.py`

`tools/ui.py` (run by `convert_assets.py` after the textures, because it reads the font atlas PNGs) converts the
2D UI data. Nothing is written to the original files; each JSON file sits next to its source.

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
- **`fade_time_msec`** is how long the form takes to fade in (400); elements with `no_fading="yes"` (the logo) stay
  opaque meanwhile.
- Not used: `tiled` is always `no` in the shipped files (the loader tiles the image at its own size when it is
  `yes`); the `background` form's `img_tile` and `img_translation` (a scrolling sky) are kept in the JSON only.

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
