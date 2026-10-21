# Muggle Ball 3

A native, cross-platform re-implementation of **Magic Ball 3** (Dream Dale /
Alawar, 2006), built macOS-first. The runtime is new code; the game's art,
sound, music and levels are loaded from **your own installed copy** and are
never committed to this repository.

Status: early skeleton. The window opens through the SDL3 GPU API (Metal on
macOS), and converted levels load and can be inspected. Rendering, physics and
gameplay come next.

## Layout

| Path | What it is |
|---|---|
| `engine/` | Platform-neutral runtime: window, GPU renderer, logging, paths |
| `game/` | Magic Ball rules: object types, level model, level loader |
| `app/` | Executable entry point |
| `platform/macos/` | App bundle metadata (the only per-platform code so far) |
| `tools/` | Asset pipeline: `.mb3` level converter, archive unpacker, MoleBox dumper |
| `docs/` | Architecture, level format, how the data was recovered |
| `tests/` | C++ and Python tests that need no game data |

## Build (macOS)

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

SDL3 and nlohmann/json are fetched automatically by CMake. Use
`-G Xcode` instead of Ninja to get an Xcode project.

## Game data

1. Install Magic Ball 3 and extract `MagicBall3.dat` with
   [`tools/molebox-dump`](tools/molebox-dump/README.md).
2. Convert everything into `assets/`:

   ```sh
   python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
       --extracted /path/to/MB3OUT --out assets
   ```

3. Run from the repo root, or point `MB3_ASSETS` at the folder:

   ```sh
   ./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3
   ./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3 --check assets/Data/Levels/piratos-po-krugy.json
   ```

## Legal

Magic Ball 3 and its assets are owned by Alawar Entertainment and Dream Dale.
This repository contains only original code and documentation. Do not commit
or distribute game files.
