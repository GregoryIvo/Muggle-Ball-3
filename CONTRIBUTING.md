# Contributing to Muggle Ball 3

Thanks for your interest. Muggle Ball 3 is a from-scratch, cross-platform
re-implementation of the 2006 game *Magic Ball 3*. The runtime here is new code;
the game's art, sound, music and levels are **not** part of this repository and
are loaded from your own installed copy. Everything below assumes that split.

## Ground rules

- **Never commit game data.** No art, audio, levels, `.dat` / `.mb3` / `.x`
  files, the original `MagicBall3.exe`, or anything converted from them.
  `.gitignore` already excludes `assets/`, `extracted/`, `*.dat` and `*.mb3`;
  keep it that way. The assets belong to Alawar Entertainment and Dream Dale.
- **Keep changes focused.** One logical change per pull request, with a clear
  description of what it does and why.
- **Match the surrounding code.** There is no `.clang-format`; follow the style,
  naming and comment density of the file you are editing (C++20, four-space
  indent, the `mb::` namespace, `lowerCamelCase` for locals and functions,
  `UpperCamelCase` for types).

## Prerequisites

macOS is the primary target; the code also builds and is tested on Linux in CI.

```sh
brew install cmake ninja          # macOS
# Linux: cmake, ninja-build, and the SDL3 build deps listed in .github/workflows/build.yml
```

SDL3 and nlohmann/json are used from the system if present (`brew install sdl3
nlohmann-json`) and fetched by CMake otherwise; stb and cgltf are always fetched.
The compiled shaders are committed (`engine/shaders/generated/`), so a normal
build needs no shader tools.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Tests that read the game data are skipped unless `MB3_ASSETS` points at a
converted `assets/` folder (see below), so the suite passes on a clean checkout.
CI runs exactly these steps on macOS and Linux for every pull request.

## Getting the game data (needed to run it)

You need your own installed copy of Magic Ball 3. The pipeline is Python-only
(nothing to `pip install`):

1. Extract `MagicBall3.dat` with [`tools/molebox-dump`](tools/molebox-dump/README.md).
2. Convert everything into `assets/`:
   ```sh
   python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
       --extracted /path/to/MB3OUT --out assets
   ```
3. Run from the repo root, or point `MB3_ASSETS` at the folder.

The run options and the in-game / level-viewer controls are in the
[README](README.md); the converted formats are in
[`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md).

## Where things live

| Area | Start here |
|---|---|
| The big picture | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| Platform-neutral runtime (window, GPU, physics, audio) | `engine/` |
| Game rules, levels, scene building, 2D UI | `game/` |
| Executable, front end, play session, level viewer | `app/` |
| Asset pipeline (Python) | `tools/`, [`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md) |
| Levels in the physics world | [`docs/PHYSICS.md`](docs/PHYSICS.md) |
| Rules of play and the footage behind them | [`docs/GAMEPLAY.md`](docs/GAMEPLAY.md) |
| Level and object/data formats | [`docs/LEVEL_FORMAT.md`](docs/LEVEL_FORMAT.md), [`docs/OBJECTS.md`](docs/OBJECTS.md) |
| Audio, saves, macOS packaging | [`docs/AUDIO.md`](docs/AUDIO.md), [`docs/SAVES.md`](docs/SAVES.md), [`docs/MACOS.md`](docs/MACOS.md) |

## Pull requests

- Branch off `main` and open the pull request against `main`.
- Make sure it builds and the tests pass locally; CI runs the same on macOS and
  Linux. Add or update tests for behaviour you change, and update the docs when
  you change how something works.
- Shaders: if you edit `engine/shaders/*.glsl`, regenerate the committed output
  with `python3 tools/build_shaders.py` (needs `glslang` and `spirv-cross`) and
  commit it in the same change.

## Reporting bugs and ideas

Open an issue. For a bug, say what you did, what you expected, what happened,
your OS, and (if it matters) which level or menu. Please don't attach game files.
