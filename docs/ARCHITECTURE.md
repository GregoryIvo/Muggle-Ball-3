# Architecture

Goal: native on macOS first, then other platforms without rewriting the game.

| Layer | Choice | Notes |
|---|---|---|
| Language and build | C++20, CMake | CMake also generates Xcode projects. |
| Window, input, audio device | SDL3 | Same API on macOS, Windows, Linux, iOS and consoles. |
| Rendering | SDL3 GPU API | Metal on macOS, Vulkan elsewhere. Shaders are GLSL, compiled ahead of time to SPIR-V and MSL by `tools/build_shaders.py` (glslang + SPIRV-Cross); the generated headers are committed, so building needs neither tool. |
| Physics | Jolt Physics (planned) | Replaces PhysX 2.x. Levels store PhysX boxes, spheres, capsules, convexes and joints, all of which map directly. |
| Audio decode | stb_vorbis (planned) | All sounds and music are Ogg Vorbis. |
| XML | Converted offline (planned for objects, sounds) | `materials.xml` is converted to JSON by `tools/materials.py`; the menus and the string table by `tools/ui.py`. |
| Text | The original's pre-rendered font atlases | Five `.crd` + `.dds` atlases of `Bookworm.ttf` and `brushType-SemiBold.TTF` ship with the game and are drawn as sprites (`BitmapFont`). SDL3_ttf stays an option for sizes they lack. |
| 2D UI | Sprites on the renderer, forms in `game/` | See below. |
| Meshes | Offline `.x` → binary glTF, loaded with cgltf | 272 meshes, all `xof 0303bin 0032`. See `ASSET_FORMATS.md`. |
| Images | stb_image | JPEG and PNG; the 25 DDS files are converted to PNG offline. |

## 2D UI

Menus, text and (later) the HUD are drawn as screen-space quads after the 3D scene:

| Layer | What |
|---|---|
| `engine/` `Sprite` | A textured, tinted, optionally rotated quad in target pixels. `Renderer::renderToWindow` and `renderToImage` take a sprite list after the debug lines; it is drawn alpha-blended without depth, in submission order, with or without a scene (`engine/shaders/sprite.*`). |
| `game/` `BitmapFont` | A font atlas and its glyph table (`tools/ui.py` output): measures, word-wraps, aligns and turns a string into sprites, with an optional drop shadow. |
| `game/` `StringTable` | The text of every language (`languages.json`). |
| `game/` `UiForm` | A menu form (`Data/Menu/*.json`): a tree of widgets with their data, state and pixel rectangles; maps the original's 4:3 layout onto any target (`UiLayout`, pillarboxed or letterboxed); hit testing and pointer events (hover, press, click). |
| `game/` `UiAssets` | Finds and uploads the images, fonts and strings below the assets folder; loads forms by name. |
| `game/` `UiDraw` | `drawForm`: a form to sprites, with each widget's state frame, shadows, hover growth, press offset, text and fade. |

The per-frame use, for the code that drives a menu (this is what `MuggleBall3 --form <name>` does):

```cpp
UiAssets assets(renderer, mb::assetRoot());           // once
UiForm form = assets.loadForm("MainMenu");            // per screen
...
int w, h; renderer.drawableSize(w, h);
form.layout(UiLayout::fit(w, h), assets.imageSizes());
PointerEvents e = form.updatePointer({input.mouseX, input.mouseY}, input.leftButton);
// react to e.entered / e.pressed / e.clicked (widget->userData says what a button does)
std::vector<Sprite> sprites;
drawForm(form, assets, sprites, {.fade = fade});
renderer.renderToWindow(frame, items, lines, sprites);  // items and lines are empty for a menu
```

## Rules

- `engine/` and `game/` never include platform headers. Anything OS-specific
  goes behind an engine interface or into `platform/<os>/`.
- The runtime reads converted assets only. Original formats are handled by
  the Python tools, so the runtime stays small and portable.
- Game data is never committed. `tools/convert_assets.py` builds it from the
  player's own copy.

## Original game, for reference

| Original | Replacement |
|---|---|
| Direct3D 8 | SDL3 GPU (Metal) |
| Ageia PhysX 2.x (`NxPhysics.dll`) | Jolt |
| BASS audio | SDL3 audio + stb_vorbis |
| HTMLayout (trial wrapper) | dropped |
| ddScores.dll (online scores, dead server) | local high scores |
