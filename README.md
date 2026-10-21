# Muggle Ball 3

A native, cross-platform re-implementation of **Magic Ball 3** (Dream Dale /
Alawar, 2006), built macOS-first. The runtime is new code; the game's art,
sound, music and levels are loaded from **your own installed copy** and are
never committed to this repository.

Status: early. Step 2, drawing a level standing still, is done: the window opens
through the SDL3 GPU API (Metal on macOS) and shows any of the 83 converted
levels from the original's fixed camera, with models, textures (animated ones
included), sky and the level's own light, and wireframes of the physics shapes on
request. Step 3 puts the levels in a physics world (Jolt, standing in for PhysX):
every actor is a rigid body with its real shapes, mass, material and joints, the
level stands as designed until something hits it, and a test ball can be shot in to
knock things over. Not there yet: the real ball and bat, bricks that die and score,
sails (cloth), moving sharks and trains, cannons, shadows and reflections, model
animation, sound and menus.

## Layout

| Path | What it is |
|---|---|
| `engine/` | Platform-neutral runtime: window and input, GPU renderer and shaders, model and image loading, logging, paths |
| `game/` | Magic Ball rules: object types, level model and loader, materials, scene building, level physics (`PhysicsScene`), camera |
| `app/` | Executable entry point: the level viewer |
| `platform/macos/` | App bundle metadata (the only per-platform code so far) |
| `tools/` | Asset pipeline: `convert_assets.py` runs the converters for levels (`mb3level.py`), meshes (`x2glb.py`), materials and textures; also the MoleBox dumper and the shader compiler |
| `docs/` | Architecture, level format, converted asset formats (`ASSET_FORMATS.md`), levels in the physics world (`PHYSICS.md`), how the data was recovered |
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
   $MuggleBall3 --screenshot out.png --level piratos-ostrovok --simulate 3 --ball -5,0   # a test ball, 3 s of physics
   $MuggleBall3 --check assets/Data/Levels/piratos-po-krugy.json
   ```

   In the window: Left/Right (or PageUp/PageDown) switch level, Space shoots a test
   ball, P pauses the physics, L reloads the level (with fresh physics), F1 shows
   the physics shapes (where the bodies are now), left-drag orbits, right-drag
   pans, the wheel zooms, R returns to the game camera, Esc quits. `--screenshot`
   renders one frame without a window; `--simulate <seconds>` runs the physics that
   long first, and `--ball [x,degrees]` shoots a test ball at t = 0 (also in the
   window): from `(x, 1.2, -15)`, that many degrees off straight ahead, or without
   the argument at the middle of the movable bodies. `--camera ex,ey,ez,tx,ty,tz,fov`
   overrides the camera and `--size` sets the window or image size.
   `MB3_GPU_DEBUG=1` turns on the GPU back end's validation layer. A level JSON
   written by an older `tools/mb3level.py` still loads but logs a warning (old
   files drew wrong capsules with F1 / `--shapes`, and have no physics fields):
   run `convert_assets.py` again to refresh it.

## Legal

Magic Ball 3 and its assets are owned by Alawar Entertainment and Dream Dale.
This repository contains only original code and documentation. Do not commit
or distribute game files.
