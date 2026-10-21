# Architecture

Goal: native on macOS first, then other platforms without rewriting the game.

| Layer | Choice | Notes |
|---|---|---|
| Language and build | C++20, CMake | CMake also generates Xcode projects. |
| Window, input, audio device | SDL3 | Same API on macOS, Windows, Linux, iOS and consoles. |
| Rendering | SDL3 GPU API | Metal on macOS, Vulkan or Direct3D 12 elsewhere. Shaders are written once and cross-compiled per backend. |
| Physics | Jolt Physics (planned) | Replaces PhysX 2.x. Levels store PhysX boxes, spheres, capsules, convexes and joints, all of which map directly. |
| Audio decode | stb_vorbis (planned) | All sounds and music are Ogg Vorbis. |
| XML | pugixml (planned) | Object definitions, menus, materials, sounds. |
| Text | SDL3_ttf (planned) | Original fonts `Bookworm.ttf`, `brushType-SemiBold.TTF`. |
| Meshes | DirectX `.x` binary loader or offline `.x` → glTF (planned) | 272 meshes, all `xof 0303bin 0032`. |

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
