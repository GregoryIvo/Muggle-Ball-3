# Muggle Ball 3

A native, cross-platform re-implementation of **Magic Ball 3** (Dream Dale /
Alawar, 2006), built macOS-first. The runtime is new code; the game's art,
sound, music and levels are loaded from **your own installed copy** and are
never committed to this repository.

Status: early. Step 2, drawing a level standing still, is done: the window opens
through the SDL3 GPU API (Metal on macOS) and shows any of the 83 converted
levels from the original's fixed camera, with models, textures (animated ones
included), sky and the level's own light, and wireframes of the physics shapes on
request. Sound and music are in: the game plays the music of each level's world, and the
sound library is ready for physics to play hits (see [`docs/AUDIO.md`](docs/AUDIO.md)).
Not there yet: sails (cloth), shadows and reflections, model animation, physics,
gameplay and menus.

## Layout

| Path | What it is |
|---|---|
| `engine/` | Platform-neutral runtime: window and input, GPU renderer and shaders, model and image loading, audio (Ogg Vorbis decoding, mixer, device), logging, paths |
| `game/` | Magic Ball rules: object types, level model and loader, materials, scene building, camera, sound library |
| `app/` | Executable entry point: the level viewer, with music |
| `platform/macos/` | App bundle metadata (the only per-platform code so far) |
| `tools/` | Asset pipeline: `convert_assets.py` runs the converters for levels (`mb3level.py`), meshes (`x2glb.py`), materials, sounds and music (`sounds.py`) and textures; also the MoleBox dumper and the shader compiler |
| `docs/` | Architecture, level format, converted asset formats (`ASSET_FORMATS.md`), sound and music (`AUDIO.md`), how the data was recovered |
| `tests/` | C++ and Python tests; the ones that read the game data are skipped unless `MB3_ASSETS` is set |

## Build (macOS)

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
python3 -m unittest discover -s tests -p 'test_*.py'
```

SDL3 and nlohmann/json come from Homebrew if installed (`brew install sdl3
nlohmann-json`) and are fetched by CMake otherwise; stb and cgltf are always
fetched. Use `-G Xcode` instead of Ninja to get an Xcode project.

The compiled shaders are committed (`engine/shaders/generated/`), so building
needs no shader tools. After editing a shader, run `python3 tools/build_shaders.py`
to regenerate them; that is the only thing that needs `glslangValidator` and
`spirv-cross` (`brew install glslang spirv-cross`).

## Game data

1. Install Magic Ball 3 and extract `MagicBall3.dat` with
   [`tools/molebox-dump`](tools/molebox-dump/README.md).
2. Convert everything into `assets/` (Python 3, nothing else to install; the
   formats are described in [`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md)):

   ```sh
   python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
       --extracted /path/to/MB3OUT --out assets
   ```

3. Run from the repo root, or point `MB3_ASSETS` at the folder:

   ```sh
   MuggleBall3=./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3
   $MuggleBall3                                  # first level
   $MuggleBall3 --level piratos-ostrovok         # a level by name (any case) or by .json path
   $MuggleBall3 --screenshot out.png --level cowboys-napadenie [--size 1024x768] [--shapes]
   $MuggleBall3 --check assets/Data/Levels/piratos-po-krugy.json
   $MuggleBall3 --play-sound pirat_hit           # play a named sound (sounds.xml), wait, exit
   $MuggleBall3 --play-music Pirates --seconds 20  # one pass of Menu, Village, Pirates, Cowboys or Knights
   ```

   In the window: Left/Right (or PageUp/PageDown) switch level, F1 shows the
   physics shapes, left-drag orbits, right-drag pans, the wheel zooms, R returns
   to the game camera, Esc quits; the music follows the level's world (a machine
   without an audio device plays silently). `--screenshot` renders one frame without a
   window; `--camera ex,ey,ez,tx,ty,tz,fov` overrides the camera and `--size`
   sets the window or image size. `MB3_GPU_DEBUG=1` turns on the GPU back end's
   validation layer. A level JSON written by an older `tools/mb3level.py` still
   loads but logs a warning (old files drew wrong capsules with F1 / `--shapes`):
   run `convert_assets.py` again to refresh it.

## Legal

Magic Ball 3 and its assets are owned by Alawar Entertainment and Dream Dale.
This repository contains only original code and documentation. Do not commit
or distribute game files.
