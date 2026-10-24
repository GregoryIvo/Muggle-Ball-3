# PlayStation Vita: building, installing, what was measured

The plan behind the port is [`VITA_PLAN.md`](VITA_PLAN.md); this is the platform's working document, in the
style of [`MACOS.md`](MACOS.md): how to build, what was decided and why, the numbers measured on the console,
and the device checklist. Dates are when a thing was checked. Where a measurement contradicts the plan, the
measurement wins and is recorded here.

**Status (2026-10-24):** M1a, M3a, M4a, M5a and M5b are built; nothing has run on a console yet (see "Device
checklist"). The toolchain is installed and pinned and Jolt cross-compiles unchanged through its "Platform Blue"
hook. A hello VPK (`mb3_vita_hello.vpk`, title ID `MUGB00009`, so it can sit on the LiveArea beside the game) draws
a lit triangle with the game's mesh shader in
GLSL ES over vitaGL, paces at two vblanks, probes the IME, SDL's GPU API, the audio device, the file system, threads
and clocks, steps a Jolt world and logs everything. `mb_engine` and `mb_game` cross-compile with the portability
fixes, the dropped-step counter (`PhysicsWorld::droppedSteps()`, `GameSession::droppedSteps()`) and the "vita"
`PhysicsProfile` of M3a (`MB3_VITA_BUILD_ENGINE` defaults ON). The engine has a renderer that runs on the console:
`MB3_RENDERER=VITAGL` (the Vita's default) and its desktop twin `GL` implement the full `Renderer` API over
OpenGL 2.1 / GLSL ES from one source (see "GL back end"), and the device render-test VPK
(`mb3_vita_render_test.vpk`, title ID `MUGB00005`) cross-compiles with no warning from this project's own files.
The game's entry point is `runGame` in `app/Game.cpp` (M5a). **M5b (this milestone)** is the front end that makes
the whole game playable with buttons and the stick alone, or with the front touch screen alone: `ConsoleInput`
(`engine/include/mb/engine/ConsoleInput.h`, platform-neutral so its rules are unit-tested on the host,
`tests/test_console_input.cpp`) turns the joystick's 16 buttons, its stick and the front touch screen into the
same `InputEvents` the mouse and keyboard already fill; `Window.cpp` (`MB3_PLATFORM_VITA`) feeds it from SDL and
reads the system's enter-button setting; a small drawn crosshair stands in for the (non-existent) mouse cursor in
the menus; the Options screen drops the window-size list and the Fullscreen box (one fixed 960x544 display, no
window to resize); and `platform/vita/game/` is the console's own entry point, producing `MuggleBall3.vpk` (title
ID `MUGB00003`, "Muggle Ball 3", no game data: `unzip -l` shows only `eboot.bin`, `param.sfo` and the LiveArea
set). It cross-compiles with no warning from this project's own files, and `arm-vita-eabi-nm` shows the three
memory globals as `D` symbols and zero undefined symbols.
The physics benchmark (M3b) is a third VPK beside those: `mb3_vita_bench.vpk` (title ID `MUGB00004`; see "Physics
benchmark" below) draws its progress with SDL's own GXM renderer rather than vitaGL; its Mac twin
(`mb_physics_bench`) has run pass 1 of the plan's 15-level set, the device has not yet.

## Toolchain (pinned)

| Piece | Version | How it was checked |
|---|---|---|
| VitaSDK | release **2026.08**, sequence 2026101770, package `vitasdk-core 2026.08.1-1`, native `arm64-apple-darwin` | `vdpm status`; `file $VITASDK/bin/arm-vita-eabi-gcc` says Mach-O arm64 |
| Compiler | `arm-vita-eabi-g++ (GNU Tools for ARM Embedded Processors) 15.2.0`, built 2026-08-25 | `--version` |
| newlib / vita-headers / vita-toolchain | `64aa7aa3` / `ebc8f4f7` / `c527abce` | `$VITASDK/version_info.txt` |
| vdpm packages | `sdl3 3.4.16`, `vitaShaRK 1.7`, `libmathneon r11 (0faab81)`, `SceShaccCgExt 1.0.1`, `taihen 0.11` | `vdpm install sdl3 vitaShaRK libmathneon SceShaccCgExt taihen` |
| vitaGL | commit **`76f43e6fbe1513cee44118da7ff2c16728c4fed2`** (2026-10-22, "Don't cache failed shader compilations"), built from source, **not** vdpm's package | `make -j8 NO_DEBUG=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1 install` |
| CI container | `vitasdk/vitasdk:2026.08` (Ubuntu 24.04, multi-arch); frozen tag `2026.08-20261015.2` | Docker Hub, 2026-10-23; the digest is pinned in the workflow (M1b) |

Install (what was done on the Mac, 2026-10-23; the SDK lives beside the repo, outside it):

```sh
git clone https://github.com/vitasdk/vdpm && cd vdpm
VITASDK=/Users/greg/Documents/MagicBall3/vitasdk ./bootstrap-vitasdk.sh --install-dir /Users/greg/Documents/MagicBall3/vitasdk
export VITASDK=/Users/greg/Documents/MagicBall3/vitasdk
export PATH="$VITASDK/bin:$PATH"   # two statements: in one, $VITASDK would still be its old value
VDPM_NONINTERACTIVE=1 vdpm install sdl3 vitaShaRK libmathneon SceShaccCgExt taihen
git clone https://github.com/Rinnegatamante/vitaGL && cd vitaGL && git checkout 76f43e6fbe1513cee44118da7ff2c16728c4fed2
make -j8 NO_DEBUG=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1 install
```

The bootstrap needs `curl` (or `wget`) and refuses an install directory that already exists. The default
channel is the newest "supported" series, 2026.08; `nightly` is the development channel. The SDK takes 455 MB
before vitaGL. vitaGL's own Makefile compiles it with `-ffast-math`; that is its business (it is the renderer's
library), and none of our physics code is built that way.

The vitaGL flags: `NO_DEBUG` drops its error handling (`SKIP_ERROR_HANDLING`), `HAVE_SHADER_CACHE` caches the
compiled shaders under `ux0:data/shader_cache/<TITLEID>/` so a second start skips the 300 ms per program,
`NO_SPLASHSCREEN` removes its boot splash. The GLSL translator and the runtime compiler are always compiled in
(there is no `HAVE_GLSL_SUPPORT` or `HAVE_SHARK` flag; the plan guessed there might be). The compiler itself is
Sony's `libshacccg.suprx`, which the console must have at `ur0:data/libshacccg.suprx` (or
`ur0:data/external/`); the hello checks for it and logs the result.

## Building

```sh
export VITASDK=/Users/greg/Documents/MagicBall3/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build-vita -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
cmake --build build-vita
ls build-vita/platform/vita/hello/mb3_vita_hello.vpk
```

The toolchain file is read inside `project()` and sets `VITA`; the top-level `CMakeLists.txt` then includes
`cmake/Vita.cmake`, which sets `MB3_PLATFORM_VITA` and the options below. The macOS block that runs before
`project()` is skipped for any cross build (`CMAKE_TOOLCHAIN_FILE` set). A Vita build takes SDL3 from the SDK
only (`find_package`, which the toolchain limits to the SDK) and never fetches one; nlohmann_json, stb, cgltf and
Jolt come from the usual `FetchContent` declarations, so `FETCHCONTENT_SOURCE_DIR_*` pointing at a shared
checkout works as on the desktop. The cross build directory is about 320 MB (Jolt is most of it).

| Option | Default | Effect |
|---|---|---|
| `MB3_VITA_TITLE_ID` | `MUGB00003` | Title ID of the game's VPK (four letters, five digits) |
| `MB3_VITA_APP_NAME` | `Muggle Ball 3` | Name under the LiveArea bubble |
| `MB3_VITA_APP_VERSION` | `01.00` | `APP_VER` in param.sfo |
| `MB3_VITA_BUILD_ENGINE` | ON (since M3a; OFF before it) | Also cross-compile `mb_engine`, `mb_game` and the device render-test VPK (`platform/vita/render_test/`); the engine has a renderer that runs on the console (M4a), so this stays on unless you only want the hello |
| `MB3_BUILD_TESTS` | OFF on Vita | A cross build cannot run them |
| `MB3_RENDERER` | `VITAGL` on the Vita (see `engine/CMakeLists.txt`) | `VITAGL` (vitaGL, the console) or `SDLGPU` (compiles, M3a-style, since the SDK's SDL3 exports the GPU API with no back end behind it, but never runs); `GL` and `NONE` are desktop-only |
| `VITA_MKSFOEX_FLAGS` | `-d ATTRIBUTE2=0` | Set explicitly so the tool's defaults cannot change the app; 12 would ask for the 365 MiB memory budget, kept as a last resort |

What `cmake/Vita.cmake` provides: `mb3_vitagl` (vitaGL and the libraries under it: libmathneon, vitaShaRK,
SceShaccCgExt, taihen and the Sce stubs), `mb3_vita_compat` (what newlib lacks, today `posix_memalign`) and
`mb3_vita_vpk(target TITLE_ID id NAME "name" VERSION 01.00)`, which makes `<target>.self` (UNSAFE: vitaGL loads
the shader compiler module from `ur0:` and the app sets the CPU clock) and `<target>.vpk` with the LiveArea set.

### Jolt

Jolt's `Core.h` knows no Vita and stops with `#error Unknown platform`, but it checks `JPH_PLATFORM_BLUE` first
and then includes `<Jolt/Core/PlatformBlue.h>`, a file the platform is meant to provide. Ours is
`platform/vita/jolt/Jolt/Core/PlatformBlue.h`: `JPH_BREAKPOINT` as `__builtin_trap()`, ticks from
`sceKernelGetProcessTimeWide`, the mutex and rwlock macros on `std::mutex` and `std::shared_mutex`, the
semaphore on a mutex and a condition variable (Jolt's own semaphore keeps an atomic count and only waits on
this one), plus two things the hook does not mention: `<alloca.h>` for `JPH_STACK_ALLOC`, and the declaration
of `posix_memalign`, which newlib hides in strict ISO mode (`-std=c++17`, which Jolt uses) and does not
implement at all; `platform/vita/src/posix_memalign.c` supplies it over `memalign`. The settings are PUBLIC on
the Jolt target so the engine's `Physics*.cpp` see the same configuration, and `mb3-deps/jolt` stays unpatched.
Jolt 5.6.0 then compiles unchanged (2026-10-23), with no fast-math anywhere near it; the Vita toolchain's
Release flags are `-O3 -DNDEBUG`, and Jolt adds `-ffp-contract=on`, which does nothing on a Cortex-A9 (VFPv3 has
no fused multiply-add). On 32-bit ARM Jolt has no NEON path and 8-byte vector alignment: it runs scalar code.

Linking: the toolchain's `-pthread` expands to `--whole-archive -lpthread --no-whole-archive`; Jolt carries it, so
an executable must not add `-lpthread` itself or every pthread symbol is defined twice.

### LiveArea

`platform/vita/sce_sys/` holds a placeholder set (icon0 128x128, pic0 960x544, bg 840x500, startup 280x158,
`template.xml`) made by `tools/vita_livearea.py` from text and plain colours with the standard library: 8-bit
indexed PNGs (colour type 3), which is what the LiveArea needs (RGBA PNGs can fail to install or show a blank
bubble). The file layout and the image sizes are those of vitasdk's `pretty_livearea` sample; the XML is the
minimal background-plus-gate layout (`style="a1"`), not the sample's framed one. `tests/test_vita_livearea.py`
checks the committed files' sizes and colour type, and that short titles still fit. A personal set made
from the game's images is a later, opt-in tool (M8), and its output is never committed, like the macOS icon.

## Installing and running the hello

1. Copy `mb3_vita_hello.vpk` to the console (VitaShell's FTP or USB) and install it from VitaShell. The bubble
   is named "MB3 Hello" (title ID `MUGB00009`), separate from the game's own `MuggleBall3.vpk` (`MUGB00003`,
   "Muggle Ball 3"), so both install and sit on the LiveArea at once.
2. Start it. It sets the clocks to 444/222/222/166 MHz, draws a slowly turning orange triangle on a dark blue
   background, and writes:
   - `ux0:data/MuggleBall3/log.txt`: versions, clocks, the enter-button setting, vitaGL's memory pools, the shader
     compile result and time, the Jolt timings, every button, touch and IME event, and frame statistics every
     600 frames and at exit;
   - `ux0:data/MuggleBall3/probe.txt`: the runtime probe (file system on `app0:` and `ux0:`, `rename` over an
     existing file, `random_device`, `hardware_concurrency`, a worker thread's affinity and a 200 KiB stack,
     clock resolution, `mallinfo`, SDL's base and pref paths, the audio device's formats at 44.1 and 48 kHz, and
     `SDL_CreateGPUDevice`, which is expected to fail).
3. Controls: the system's enter button (Cross, or Circle when the system says so) confirms; the other one exits.
   Triangle opens the IME (type something and confirm, or cancel); Start toggles the swap interval between 2 and 1
   (the statistics are written and reset); Select writes the statistics and the memory pools now; the d-pad is
   logged.
4. Send back `log.txt` and `probe.txt` (and a photo if the triangle looks wrong).

Memory settings in the hello: newlib heap 160 MiB (newlib's default is 128 MiB), main thread stack 1 MiB, default
pthread stack 512 KiB (without it a `std::thread` gets 32 KiB, `libpthread.a`'s default), vitaGL RAM threshold
16 MiB. The game's values are decided in M6b.

## GL back end

M4a. `MB3_RENDERER=GL|VITAGL` is one source, `engine/src/render/gl/`, built in two flavours:

- **`GL`**: desktop OpenGL 2.1, compatibility profile, through an SDL GL context. Not meant to ship; it is
  how the back end is tested on a Mac (`tests/render_test.cpp`, a real window) before any device run, since
  Darwin 27's `GL_VERSION` is still `2.1 Metal - 91.7` with GLSL 1.20, NPOT and `GL_EXT_framebuffer_object`.
- **`VITAGL`**: the same renderer over vitaGL on the console. The Vita's default (`cmake/Vita.cmake` caches
  `MB3_RENDERER=VITAGL` before the engine is added); needs `MB3_PLATFORM_VITA` (a `FATAL_ERROR` otherwise).

Both compile against the GLSL ES 1.00 / GL 2.1 subset the two targets share: no fixed function, no UBOs, no
VAOs, no sRGB, no instancing.

**File layout:**

| File | What |
|---|---|
| `engine/src/render/gl/GlPlatform.h` | The given contract: the `Platform` struct (context bring-up, present, the preamble, NPOT support) and the FBO/mipmap wrappers (core names on vitaGL, EXT names on desktop). |
| `GlPlatformSdl.cpp` / `GlPlatformVita.cpp` | `Platform`'s two implementations. |
| `GlFunctions.h` | Beside `GlPlatform.h`, not part of it: GL 1.5/2.0 entry points (buffers, shaders, programs, vertex attributes) that vitaGL.h declares directly but SDL's GL headers only give as typedefs (`GL_GLEXT_PROTOTYPES` is never defined, the same reason `GlPlatform.h`'s framebuffer group needs `SDL_GL_GetProcAddress`); `GlPlatformSdl.cpp` loads them the same way. |
| `ProgramCache.{h,cpp}` | Compiles the five programs (the four pairs plus the discard-free mesh fragment) from `engine/shaders/generated/gl_shaders.h` with `Platform::preamble()`, and caches every uniform location. |
| `RendererImpl.h`, `Renderer.cpp`, `RenderFrame.cpp` | The `Renderer` API: resource creation (`Renderer.cpp`) and frame recording (`RenderFrame.cpp`), mirroring the SDL GPU back end's split. |
| `engine/shaders/gl/*.{vert,frag}` | The GLSL ES 1.00 bodies, no preamble of their own (see `mesh.frag`'s `ALPHA_TEST_BEGIN`/`END` markers). |
| `engine/shaders/generated/gl_shaders.h` | Generated by `tools/build_shaders.py`'s GL half: each body as a `gl_<name>_<stage>` string, plus `gl_mesh_frag_nodiscard`; committed, so the C++ build needs no tool. |
| `platform/vita/src/VitaProcess.cpp` | The three memory globals (same values as the hello) every Vita executable that links `mb_engine` must list in its own sources. |
| `platform/vita/render_test/` | The device render-test VPK (below). |

**The contract, as implemented** (docs/VITA_PLAN.md §4.3):

- The clip remap (`z' = 2z - w`, D3D's 0..1 to GL's -w..w) is a matrix with `m[2][2] = 2`, `m[3][2] = -1`,
  folded into `viewProj = frame.view * frame.projection * remap` once per frame, uploaded with
  `glUniformMatrix4fv(..., GL_FALSE, ...)`, exactly like the hello.
- `glFrontFace(GL_CW)` and `glDepthFunc(GL_LEQUAL)` are set once, at `Platform::init`; per draw,
  `applyRenderState` only touches cull/depth/blend when the packed `RenderState` differs from the last one
  applied (minimising state changes, as the SDL GPU back end's pipeline cache does by construction).
- Colour writes are RGB only (`glColorMask(1,1,1,0)`) for every draw; the clear gets the full mask. **Finding:**
  `glClear` only clears the planes its *current* write masks allow, so if the previous frame's last draw left
  `glDepthMask(GL_FALSE)` (any blended or effect item does), clearing the depth buffer at the start of the next
  frame is silently a no-op, and whatever was in the depth buffer before stays — this failed `render_test`'s
  mipmap test (a tiny quad, drawn right after other tests, simply did not appear) until both masks are forced
  on immediately before the clear. **Second finding, from review:** forcing the masks for the clear is not by
  itself enough — `applyRenderState` skips re-applying cull/depth/blend whenever the packed `RenderState`
  matches the last one it applied, and that cached key persists across frames, so if a frame's first draw
  happens to pack to the same key as the *previous* frame's last draw, the clear's forced `glDepthMask(GL_TRUE)`
  stays in effect for that draw (depth gets written even though the state says `depthWrite = false`). Fixed by
  resetting the cache (`currentStateKey`, and the other GL bind caches alongside it) to an impossible sentinel
  at the top of every `drawFrame`, not just once at construction — see `RenderFrame.cpp`'s comment there, which
  also covers the (currently unreached) case of more than one `Renderer` sharing this process's one GL context.
- Textures: `GL_RGBA` (the unsized format; the driver picks the internal storage), `GL_REPEAT`, linear
  filtering, no anisotropy; `opaqueAlpha` (every alpha byte 255) is
  computed once in `createTexture`, and the mesh draw picks the discard-free program when that holds and
  `material.diffuse.a >= 1` and `blend == Opaque`. NPOT: when mipmaps are asked for and
  `!Platform::npotMipmaps()` (the Vita; desktop GL 2.1 here has `GL_ARB_texture_non_power_of_two`, so never),
  the image is bilinearly resampled to the next power of two before upload (`Renderer.cpp`,
  `resampleToPowerOfTwo`), mirroring `tools/vita_pack.py`'s offline fallback.
- **vitaGL's depth buffer** (read at the pinned commit, `source/framebuffers.c` and `source/gxm.c`, deleted
  after): `glRenderbufferStorage`'s `internalformat` only decides whether a stencil is wanted
  (`DEPTHBUFFER_WANTS_STENCIL` vs `DEPTHBUFFER_MISSING`); the width and height it is given are unused. The
  actual depth buffer (`DF32M`, or `DF32M_S8` with a stencil) is allocated lazily, sized to the colour
  attachment, the first time the FBO is drawn into. So `GL_DEPTH_COMPONENT24` never gets "rejected" in favour
  of 16 — there is nothing to reject — and the back end just always asks for 24 on both targets.
- Meshes: 16-bit indices when the vertex count is 65535 or less (every shipped mesh qualifies: the largest has
  5171), else 32-bit. Streams (lines, sprites, effects) are grown by doubling (32 KiB floor) and uploaded with
  `glBufferData(..., GL_STREAM_DRAW)` every frame (orphaning: a same-size reallocation before the
  `glBufferSubData`, so the GPU can keep reading the old store instead of stalling on one it may still be
  reading).
- `renderToImage` draws into an FBO (a `GL_RGBA` 8-bit colour texture, a `GL_DEPTH_COMPONENT24` depth renderbuffer),
  reused while the size is unchanged, then `glReadPixels` and a row flip (GL reads bottom-up; vitaGL's FBOs are
  flipped internally to match GL, so the same flip is right on both targets).
- `createTexture` rejects (logs and returns handle 0, like the SDL GPU back end) an image wider or taller than
  `Platform::maxTextureSize()` (4096 on the Vita) after the NPOT resample, instead of handing back a handle
  `glTexImage2D` silently refused (`GL_INVALID_VALUE` left pending, the texture unloadable, draws with it
  black). No shipped asset is this large today.
- Vertex attribute 2 (`inUv`/`inColor`, depending on the layout) is enabled or disabled per draw in
  `bindVertexBuffer` to match the layout (every one but `Line`, which is 2 wide), instead of being left enabled
  for the life of the context with whatever pointer the previous layout's buffer set: with no VAOs, a stale
  enabled array pointing into a since-deleted buffer is undefined per the GL spec even though nothing currently
  reads it during a line draw.
- `MB3_GL_DEBUG=1` drains `glGetError` after `Platform::init` and at the end of every `drawFrame`, logging
  each distinct code once — the same idea as the SDL GPU back end's `MB3_GPU_DEBUG`, since neither vitaGL nor
  this desktop build has a validation layer of its own.

**Desktop-only finding (not a device concern):** `SDL_GL_SetSwapInterval(1)` is what the contract asks for and
is what `GlPlatformSdl.cpp` sets, but on a Mac whose display is asleep, SDL's Cocoa GL back end's vsync wait
(`Cocoa_GL_SwapWindow` → a condition variable tied to the display's refresh) blocks indefinitely, since no
refresh ever happens to signal it — `mb_frontend_smoke` (which opens a real window) hung on exactly this until
the display was kept awake (confirmed both ways: interval 0 does not hang with the display asleep, and interval
1 does not hang once the display is awake, `caffeinate -u`). vitaGL's own swap (`vglSwapBuffers`,
`sceDisplayWaitVblankStartMulti`) does not go through SDL's Cocoa path at all, so this is specific to testing
the desktop `GL` variant interactively, not something the Vita build can hit.

### Device render-test VPK

`platform/vita/render_test/` wraps `tests/render_test.cpp` (unchanged from the host build) into
`mb3_vita_render_test.vpk` (title ID `MUGB00005`): `main.cpp` creates `ux0:data/MuggleBall3/render-test/`,
`freopen`s stdout and stderr to `results.txt` there (the test's own `save()` writes its per-check PNGs to the
same directory, via `MB3_TEST_OUTPUT_DIR`), calls the test (renamed from `main` to `render_test_main` on that
one source file, since the compiler's no-mangling treatment of `main` applies only to that exact spelling — the
renamed function is an ordinary C++ symbol, and the forward declaration must not be `extern "C"` or it will not
link), appends the exit code as the file's last line, and exits. **Finding, from review:** opening stdout `"w"`
and stderr `"a"` gives them two independently tracked offsets into the same file; once either stream's newlib
buffer flushes (both start fully buffered at 1 KiB, well under the ~3 KiB the 19 tests print), the next flush
of the other stream overwrites what the first appended, and a crash loses whatever was never flushed at all —
exactly the case this shim exists to diagnose. Fixed: remove the file first, then open both streams `"a"` (one
shared, always-current offset), stdout line-buffered and stderr fully unbuffered, so text reaches the file as
it's produced instead of sitting in a buffer a crash never flushes.

1. Copy `mb3_vita_render_test.vpk` to the console and install it from VitaShell. The bubble is "MB3 Render Test".
2. Start it; it runs the 19 checks with no window of its own and exits.
3. Send back `ux0:data/MuggleBall3/render-test/results.txt` and the directory's PNGs (particularly any whose
   check failed — the file names say which: `01_clear.png`, `02_checker_quad.png`, and so on through
   `12c_effect_crowd.png`). Also send back `ux0:/data/SDL_Log.txt`: SDL3 on the Vita writes `SDL_Log`/
   `SDL_LogError` there, not to stderr, so it is the only place `mb::logError`/`logWarn` lines from the engine
   (as opposed to the test's own `printf` output, which is in `results.txt`) show up.

### Desktop results (2026-10-24, Apple M3 Pro, Darwin 27, `GL_VERSION 2.1 Metal - 91.7`)

- `mb_render_test` against `MB3_RENDERER=GL`: **all 19 checks pass**, with no adapted tolerance and no changed
  sampling point — the `glClear` finding above was the only real defect, fixed in the back end, not in the test.
- The full suite with `MB3_RENDERER=GL` (`ctest`, `MB3_ASSETS` set): 43 of 43, one skipped
  (`mb_window_tests`, SDL's dummy driver, as on every other back end). `python3 -m unittest discover -s tests -p
  'test_*.py'`: 202 tests, 4 skipped, 0 failed.
- M4b-style screenshot comparison (`tools/image_diff.py`, SDL GPU on Metal vs. `GL`, 960x544, each level's start
  camera at time 0, `renderToImage`): a knight-garden (peak triangles), knight-miss-1457 (heaviest physics),
  piratos-ostrovok with `--hud` (reference screenshot, 3 sails), piratos-bul-bul (a Water-material level: its
  `boardsea.x` mesh has a primitive whose embedded texture file is `water.png`, which `SceneBuilder`'s
  `ownTexture` matches against the materials library by name — see `game/src/SceneBuilder.cpp`), a second sail
  level (Piratos-Kartejniki), and the main menu form:

  | Scene | % within tolerance 8 | Worst pixel (location, SDL GPU vs. GL, max channel diff) |
  |---|---|---|
  | knight-garden | 95.5% | (434,287): (16,104,4) vs (117,56,11), diff 101 |
  | knight-miss-1457 | 94.6% | (439,256): (132,17,68) vs (187,0,182), diff 114 |
  | piratos-ostrovok (`--hud`) | 95.7% | (598,215): (73,81,2) vs (216,201,4), diff 143 |
  | piratos-bul-bul (Water material) | 96.2% | (459,107): (89,91,126) vs (222,140,81), diff 133 |
  | Piratos-Kartejniki | 95.6% | (507,298): (2,89,207) vs (202,202,202), diff 200 |
  | MainMenu form | 100.0% | (455,245): diff 4 |

  All six are below M4b's 99% bar except the menu. **Correction, from review:** the numbers above are right,
  but an earlier version of this section explained them as rasteriser edge placement only ("not a shading,
  blending or sampling bug") — that claim does not survive measurement. Of piratos-ostrovok's 22502 pixels
  differing by more than 8, 6347 (28%) sit inside otherwise-smooth regions (no neighbour differs by more than
  48) with diffs of 9-16, and whole textured surfaces disagree well above the edges: a 100x60 sand region is
  19.5% over tolerance (mean diff 5.8), the ship hull 25.5% (mean 6.4), 8.7% of the whole frame is over
  tolerance-4 — while the flat water plane (0.00%), the background sprite (0.00%) and the HUD text (mean 0.01)
  match almost exactly, and 4x crops show the GL hull's planks and ornament band visibly softer than SDL GPU's.
  The real cause: the SDL GPU back end's one sampler (`sdlgpu/Renderer.cpp`'s `createSampler`, 8x anisotropy)
  samples minified, slanted surfaces anisotropically; this GL back end's one sampler is isotropic trilinear,
  exactly as §4.3 asks ("drop anisotropy") — so the mismatch is the two back ends disagreeing, by design, on
  how a minified texture blurs, not a rasteriser edge-coverage artefact. (The *edges*-vs-*fill* framing was not
  wrong about edges existing at thin high-contrast geometry; it was wrong to say that was the whole story, and
  wrong to rule out sampling.) This predicts the same ~4-5% gap on the real Vita against the macOS references
  for every scene with 3D geometry, independent of anything device-specific — the only scene with no 3D edges
  or minified surfaces at all (the menu, sprites only) is exactly the one that matches 100.0%. No code change
  follows from this on the GL side (§4.3 is explicit that it wants isotropic filtering); re-measure once on the
  device to confirm the gap is this and not something new, and record M4b's own tolerance against it knowing
  the ~95% baseline this comparison already set, not a hoped-for ~99%.

## Physics benchmark (M3b)

`app/bench/PhysicsBench.{h,cpp}` is the bench core, shared by two targets: `mb_physics_bench` on the Mac
(`app/CMakeLists.txt`) and `mb3_vita_bench.vpk` (title ID `MUGB00004`, `platform/vita/bench/`) on the console,
which compiles the same `PhysicsBench.cpp` straight in rather than linking a shared library across the two
very different build graphs. Both read the same `bench-physics.json` and write the same CSV columns, so a
host run and a device run of the same JSON line up level by level — the point of the whole milestone: the
per-level ratio of the two replaces the plan's estimated speed factor (section 4.4) with a measured one.

**What it measures.** For each level it builds a `GameSession` (the Autopilot playing it, 99 hearts so a lost
ball never ends the run early) and calls `session.update(fixedStep, input)` once per planned step, timing
each call with `steady_clock` on the Mac and the kernel's own process-time counter on the Vita
(`PhysicsBench.cpp`'s `VitaProcessClock`, `sceKernelGetProcessTimeWide`): this SDK's libstdc++ has no
`CLOCK_MONOTONIC`, so `steady_clock` there falls back to `gettimeofday`, a wall clock with no monotonicity
guarantee — not what a step timer wants. `mb3_vita_bench` also logs `steady_clock`'s own period and a 50 ms
delay measured against both clocks once at start-up, so `bench.log` validates that gap for itself rather than
taking this paragraph's word for it. That is **one physics step plus the rules** (the bat easing towards the
Autopilot's target, the ball's speed and angle correction, hit detection, bricks exploding, prizes) — not the
bare `PhysicsScene::step()` that section 4.4's desktop numbers measured. The two are not directly comparable:
section 4.4's per-level averages (knight-miss-1457 0.70 ms, and so on) are physics alone over a much longer
(900 s) Autoplay run; the numbers below are a 20 s window of physics *and* rules. Expect the bench's own
numbers to be the baseline from here on, not section 4.4's.

Per level, a run also tracks the peak and mean of `PhysicsWorld::activeBodyCount()` (sampled every step),
`PhysicsWorld::bodyCount()` once after the build, `tempAllocatorPeakBytes()` and `GameSession::droppedSteps()`
at the end, plus how long reading the level's JSON and building the `GameSession` took. A level stops early
(and says so, as `endedBy`) when the session reaches `SessionPhase::Complete` or `GameOver`, or when a
caller's progress callback asks it to (the Vita's "press a button to stop"); otherwise it runs for the
configured simulated seconds and `endedBy` is `"time"`.

**Running it on the Mac:**

```sh
./build/app/mb_physics_bench platform/vita/bench/bench-physics.pass1.json --out /tmp/host-pass1.csv
# or, with your own converted assets found some other way:
MB3_ASSETS=/path/to/assets ./build/app/mb_physics_bench bench-physics.json [--out csv] [--platform host]
```

It prints one progress line and the CSV line per level to stdout as it goes, exits 0 if every level in the
JSON produced a CSV line, 1 if any did not (a level not in the catalogue, say), 2 for a bad command line.

**Running it on the console:** copy `bench-physics.json` (start from `platform/vita/bench/bench-physics.pass1.json`)
to `ux0:data/MuggleBall3/bench-physics.json` (next to the converted `assets/` folder, as the other milestones'
checklists already ask for) and start `mb3_vita_bench` from VitaShell. It sets the clocks to 444/222/222/166
MHz, like the hello, and draws its progress with SDL's own GXM renderer (`SDL_render_vita_gxm`; this VPK never
touches vitaGL, because nothing here needs a picture of the game) — the config's label, which level is
running and of how many, the step count, the previous level's mean/p95, elapsed time, and "press a button to
stop". Pressing a button stops the whole run (not just the current level) once the current level's partial
result is written, so an interrupted run still keeps every level it finished plus the one it was on. If
`bench-physics.json` is missing, or the asset pack is not at `ux0:data/MuggleBall3/assets`, it says so on
screen (and in `ux0:data/MuggleBall3/bench.log`) and waits for a button instead of crashing. At the end it
shows every level's mean/p95 and waits for a button before exiting. Send back `bench-physics.csv` and
`bench.log`.

**`bench-physics.json` keys** (`app/bench/PhysicsBench.h` has the authoritative comment):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `levels` | array of strings | required | level ids (the file stem under `Data/Levels`), any case |
| `seconds` | number | 20 | simulated seconds per level |
| `profile` | string | `"desktop"` | the `PhysicsProfile` the settings start from (`"desktop"` or `"vita"`) |
| `velocitySteps`, `positionSteps`, `collisionSteps`, `threads` | integer | the profile's own | each, if given, replaces the profile's value (and its per-level override's) for every level |
| `label` | string | `""` | free text copied into every CSV line, for telling passes apart |

Unknown keys are ignored, so a file can carry notes (the committed `bench-physics.pass1.json` has one).

**CSV columns:** `platform,label,level,profile,velocitySteps,positionSteps,collisionSteps,threads,`
`simulatedSeconds,steps,endedBy,loadMs,buildMs,stepMeanMs,stepP95Ms,stepP99Ms,stepMaxMs,bodies,peakAwake,`
`meanAwake,droppedSteps,tempAllocatorPeakBytes,score,wallSeconds`. The four solver columns are the settings
the level was *actually* simulated with (`settingsFor()`: the profile's per-level override, if any, then the
JSON's explicit values on top) — not just whatever `bench-physics.json` happened to say — so a host CSV and a
device CSV of the same JSON always agree on what they are comparing. The file is appended to, with the header
written only once (when it is new or empty), so an interrupted run keeps whatever it already measured.

**`tools/bench_ratio.py`** joins a host and a device CSV by `(level, profile, velocitySteps, positionSteps,
collisionSteps, threads)` and prints, per level, the host and device mean/p95 step times, the two ratios, how
many steps each side actually ran and the dropped steps; a row whose steps or `endedBy` differ between the
two sides (a different window of the same noisy Autopilot run) is flagged and left out of the medians below,
rather than averaged in as if it were comparable. It then prints the median and maximum **mean-time** ratio
over every remaining matched level — this is the measured scale factor that replaces the plan's 70x, which
section 4.4 itself derives from mean step times, not p95 — with the p95-time ratio reported alongside it as
the device's own tail (cache misses, the newlib allocator) on top of that mean, not folded into the headline.
It also flags any level whose device p95 is over the plan's 8 ms per-step budget, or whose device mean is over
half the 16 ms per-frame budget (also 8 ms, section 1):

```sh
python3 tools/bench_ratio.py host-pass1.csv device-pass1.csv
```

### Pass 1: the Mac baseline (2026-10-24, `bench-physics.pass1.json`: profile "vita", 100/1/1, threads 0, 20 s each)

All 15 levels ran the full 20 s (`endedBy` "time") with zero dropped steps. Times are milliseconds per
`GameSession::update()` call (physics plus the rules), not section 4.4's physics-only numbers:

| Level | Steps | stepMean | p95 | p99 | max | peakAwake | dropped |
|---|---|---|---|---|---|---|---|
| knight-miss-1457 | 1200 | 0.182 | 0.446 | 2.819 | 3.396 | 220 | 0 |
| village-tennis | 1200 | 0.359 | 1.711 | 1.831 | 1.971 | 167 | 0 |
| piratos-dom-na-vode | 1200 | 0.619 | 2.183 | 2.273 | 2.460 | 140 | 0 |
| knight-eggs | 1200 | 0.567 | 2.104 | 2.510 | 7.503 | 111 | 0 |
| village-dinner | 1200 | 0.403 | 0.827 | 0.967 | 1.149 | 94 | 0 |
| piratos-dva-doma | 1200 | 0.019 | 0.064 | 0.086 | 0.139 | 15 | 0 |
| village-logovo-vedmi | 1200 | 0.037 | 0.125 | 0.553 | 0.663 | 70 | 0 |
| knight-shashki | 1200 | 0.179 | 1.468 | 1.827 | 2.295 | 80 | 0 |
| knight-v-yablochko | 1200 | 0.107 | 0.408 | 0.506 | 0.928 | 60 | 0 |
| piratos-gorillas | 1200 | 0.085 | 0.594 | 0.677 | 0.996 | 52 | 0 |
| village-na-melnice | 1200 | 0.152 | 0.390 | 0.484 | 0.557 | 103 | 0 |
| village-rinok | 1200 | 0.124 | 0.207 | 0.271 | 0.513 | 50 | 0 |
| Village-Dvorik | 1200 | 0.133 | 0.296 | 0.333 | 0.801 | 51 | 0 |
| knight-garden | 1200 | 0.171 | 0.599 | 0.688 | 0.834 | 176 | 0 |
| village-love | 1200 | 0.047 | 0.226 | 0.276 | 0.333 | 38 | 0 |

Sum of `stepMeanMs x steps` over all 15 levels: 3.82 s; sum of `loadMs + buildMs`: 95 ms. At the plan's three
estimates of the device's speed (section 4.4: 45-110x, 70x central), scaling both sums by the same factor:

| Factor | Estimated Vita wall time for pass 1 |
|---|---|
| 45x (low) | ~176 s (2.9 min) |
| 70x (central) | ~274 s (4.6 min) |
| 110x (high) | ~431 s (7.2 min) |

All three stay far under the plan's 45-minute rule (section 6, "M3b"): even the pessimistic 110x estimate
leaves a 6x margin. **Pass 1's 20 s per level needs no shortening.** If anything, there is enough headroom
that a future pass could afford more simulated seconds per level for a steadier p95/p99 (the 20 s window
here is short enough that a level with a slow start, such as a level the Autopilot does not reach a brick in
for a few seconds, can bias its own mean down) — left to a later pass, since the committed `bench-physics.pass1.json`
is this milestone's given file, not something to change here.

### Pass 2 (planned; device numbers decide which are worth running)

Only the combinations pass 1 leaves open, per the plan (section 6, "M3b", and section 5.5): on the
**non-windmill** levels only (the windmills — village-dinner, village-na-melnice, village-rinok, Village-Dvorik —
keep 100 velocity iterations; docs/PHYSICS.md "The solver" found nothing cheaper stable), 30/1/1, 20/1/1 and
10/1/1; separately, 30/1/2 and 10/1/4 (docs/PHYSICS.md's two measured-stable alternatives for the windmills
themselves, so these two are worth trying on *all* 15 levels); and threads 1 and 2 (today's "vita" profile is
single-threaded; the plan's section 4.4 found three Jolt workers gave no wall-time gain and 2.4x the CPU on
the Mac, but that was never checked on the Vita's four-core, three-usable-core ARM). Each needs its own
`bench-physics.json` (same 15 levels, same `seconds`; a `velocitySteps`/`positionSteps`/`collisionSteps`/
`threads` override and a distinct `label`) and its own device run, agreed with the user once pass 1's own
device numbers are in (section 7, "Stop and agree the frame-rate target and the physics profile with the
user before M7").

### Device checklist (M3b)

- [ ] the bench VPK installs and runs; the pass-1 CSV (`bench-physics.csv`) comes back;
- [ ] the measured scale factor (`tools/bench_ratio.py` against the host CSV above; its mean-time ratio is
      the number that replaces the 70x, since section 4.4 derives 70x from mean step times, not p95) replaces
      the 70x estimate in this document, and the per-level estimates in section 4.4 are recomputed against it;
- [ ] pass 2's combinations (above) are agreed with the user and, once run, their device CSVs come back too;
- [ ] the frame-rate target and physics profile are agreed with the user before M7 starts.

## Decisions and findings

Host-side checks, 2026-10-23, against the pinned toolchain:

- **C++20 library:** a compile-and-link probe of `<format>` (`{}`, `{:.0f}`, `{:.1f}`, `{:.2f}`, `{:<14}`,
  `{:8.3f}`), `<ranges>`, `<span>`, `std::bit_cast`, `<filesystem>` (iterators, `create_directories`, `rename`,
  `remove`, `current_path`), `thread_local`, exceptions, RTTI, `std::random_device`, `std::thread`,
  `std::shared_mutex`, `std::condition_variable` and `steady_clock` passes with no missing symbol. No `{fmt}` and no
  `mb::fs` shim are needed. (Runtime behaviour is what the probe on the console checks.)
- **`posix_memalign`:** not in `libc.a` (`memalign`, `aligned_alloc`, `getentropy`, `mallinfo`, `sysconf` are);
  shimmed, see "Jolt". Also not declared in strict ISO mode.
- **`SDL_CreateGPUDevice` is exported** by the SDK's `libSDL3.a` (the GPU API's dispatcher is compiled with no
  back end behind it: SDL 3.4.16 has `d3d12`, `metal` and `vulkan` only). So the engine's SDL GPU renderer
  *links* on Vita and fails at run time; M3a can cross-compile `mb_engine` unchanged, and the vitaGL back end
  (M4a) replaces it through the renderer seam (M2).
- **SDL3 on Vita** (its `main` branch, read 2026-10-23): the video driver touches no GXM, so vitaGL can own the
  display while SDL does events, joystick, touch and audio (its video driver imports only `sceImeDialog*` and
  `sceKernelPowerTick`; the GXM imports in `libSDL3.a` belong to SDL's own 2D `SDL_Renderer` back end, which must
  never be created beside vitaGL). SDL's video driver and event pump never call `sceCommonDialogUpdate`: the app
  presents with `vglSwapBuffers(GL_TRUE)` while `SDL_ScreenKeyboardShown` is true. The IME is the one-shot
  `sceImeDialog`: when it closes, SDL 3.4.16 sends whatever is in its buffer as one `TEXT_INPUT`, and a Return
  only if it was confirmed, so a cancel after typing may still deliver text (the plan's reading of SDL's `main`
  said a cancel sends nothing; the device run decides). SDL shows the IME only when it sees no physical keyboard,
  unless `SDL_HINT_ENABLE_SCREEN_KEYBOARD` is "1", which the hello sets. Audio
  is S16 only; below 48 kHz SDL opens the BGM port at the asked rate, unresampled. `SDL_GetPrefPath("",
  "MuggleBall3")` is `ux0:/data/MuggleBall3/`; the base path is `app0:/`. The joystick is positional with 16
  buttons and no hat (checked in the SDK's SDL 3.4.16 source and in its built-in "PSVita Controller" mapping):
  0 Triangle, 1 Circle, 2 Cross, 3 Square, 4 L1, 5 R1, 6 d-pad down, 7 left, 8 up, 9 right, 10 Select, 11 Start,
  12 to 15 L2, R2, L3, R3 (a PSTV controller only). SDL reads no enter-button setting. Whether SDL has a gamepad
  mapping for it is logged by the hello (the mapping string is in the library, so it should).
- **Swap interval:** `eglSwapInterval(EGL_NO_DISPLAY, 2)` sets vitaGL's `vsync_interval`, and its display-queue
  callback calls `sceDisplayWaitVblankStartMulti(vsync_interval)` (read in `source/egl.c` and `source/gxm.c` at
  the pinned commit). The hello uses it and records the presented intervals from `sceDisplayGetVcount`.
- **Work time** is measured from the frame start to the call of `vglSwapBuffers` with
  `sceKernelGetProcessTimeWide` (the plan's second method: vitaGL presents from a display queue, so the wait is
  not on the main thread).
- **Clip space:** the engine's projection is D3D-style with clip z in 0..1; GL wants -w..w. The hello multiplies
  `viewProj` on the right by a matrix with `m[2][2] = 2`, `m[3][2] = -1` (z' = 2z − w), which is the one place the
  renderer will do it too.
- **Desktop OpenGL 2.1** is still offered on this Mac (Darwin 27, Apple M3 Pro: `GL_VERSION 2.1 Metal - 91.7`,
  GLSL 1.20, NPOT and FBO extensions present), so the plan's optional `MB3_RENDERER=GL` test variant of the
  vitaGL back end is viable and `tests/render_test.cpp` can check it on the Mac before a device run (M4a).
- **mksfoex** writes `ATTRIBUTE2 = 0` by default (not 12) and `ATTRIBUTE = 32768`; the flags are set explicitly
  anyway.
- **vitaGL's mip generation** (`gpu_utils.c`): uncompressed textures stay linear at their real size; levels are
  laid out at power-of-two strides, odd sizes are rounded down, and the chain stops when either side reaches 1,
  so NPOT mip chains differ from the desktop's: the plan's resampling of NPOT mipmapped textures (M6a,
  `tools/vita_pack.py`) stands. `GL_REPEAT` is forwarded to GXM for any texture; whether the hardware honours it
  on linear NPOT textures is a device check (M4a's render tests).
- **`std::thread::hardware_concurrency()` is 0** with this toolchain: libstdc++ asks `sysconf(_SC_NPROCESSORS_ONLN)`
  and newlib's `sysconf` has no such name. The engine's `makeJobSystem` clamps `hardware_concurrency() - 1` into
  0..3, so its default of -1 worker threads is the single-threaded job system there, which is what the Vita
  profile wants anyway. Jolt's own `JobSystemThreadPool` given -1 threads would wrap the count and crash: always
  give it a number.
- **vitaGL has no shutdown call**; the process exit releases GXM.
- **The engine and the game compile for the Vita as they are** (2026-10-23, `MB3_VITA_BUILD_ENGINE=ON`): no
  source change was needed for GCC 15.2 and newlib, and no file is built with fast-math. What they do at run
  time (`std::filesystem` on `ux0:`, `random_device`, `hardware_concurrency`, the SDL GPU device creation that
  must fail) is what the hello's probe measures first.

### M3a: portability fixes, the dropped-step counter, the physics profile (2026-10-23)

With `MB3_VITA_BUILD_ENGINE` now defaulting ON, `mb_engine` and `mb_game` stay cross-compiling unchanged after
every fix below (confirmed by building `build-vita` fresh; see the device checklist for what still needs a
console). None of these change desktop behaviour: every one is guarded by `__vita__` or (where the code tests
the build, not the compiler) `MB3_PLATFORM_VITA`, which `cmake/Vita.cmake` now defines for the whole Vita build
right next to the `MB3_PLATFORM_VITA` CMake variable it has always set.

- **stb** (`engine/src/StbImpl.cpp`): `STBI_NO_THREAD_LOCALS` on `__vita__`, because TLS there is emulated, not
  real, and stb's thread-local error buffer does not need it (nothing here decodes off the main thread).
- **Filesystem** (`engine/src/Paths.cpp` and an audit of `engine/src`, `game/src`, `app`): `assetRoot()`'s
  `current_path()` candidate now uses the `error_code` overload and is skipped, not thrown, when there is no
  working directory (`app0:/` is a read-only sandbox with no notion of one); its last-resort fallback order
  (local candidate, then the user data directory, then a bare `"assets"`) is unchanged from the desktop's, so a
  "not found under ..." message still names the same folder it always did there. One more throwing call was found
  and fixed on the way: `ParticleLibrary::load` (`game/src/ParticleDef.cpp`) iterated a `directory_iterator`
  with a range-`for`, whose implicit `operator++()` throws on a read error partway through the folder; it now
  increments through the `error_code` overload like every other directory walk in the codebase (`LevelCatalog`,
  `TextureIndex`, `UiAssets`). Everywhere else already used the non-throwing overloads.
- **Saves** (`engine/src/Files.cpp`, `writeFileAtomic`): if `rename(temp, path)` refuses to replace an existing
  *regular file* outright (some file systems do), the target is moved aside with `keepAsBackup()` and the rename
  retried; if that also fails, the backup is put back. The fallback is gated on `path` being a regular file so
  that it cannot replace something that is not a save (`path` a directory, which gets `EISDIR` on this host,
  fails cleanly instead and is left untouched; `tests/test_save.cpp` exercises both). Whether the Vita's file
  system (exFAT via `sceIo`) ever refuses an ordinary save file's rename this way is unmeasured — the hello's
  probe already checks a plain `rename` over an existing file, and this fallback is the answer if that or a
  real save ever needs it. See `docs/SAVES.md`, "Writes".
- **Logging** (`engine/include/mb/engine/Log.h`, `Log.cpp`): `setLogFile(path)` appends every message to a file
  from then on (opened once, flushed per message, failures silenced after one `SDL_Log` warning), creating the
  file's parent directory first if it does not exist yet — a fresh `ux0:data/MuggleBall3/` has none, so without
  this the very first run would get no log at all; on `__vita__`, `logMessage` also calls `sceClibPrintf`,
  because `SDL_Log` has nowhere useful to go there. Neither changes desktop behaviour unless `setLogFile` is
  called. The hello already writes its own log by hand
  (`sceIoOpen`/`write`); wiring the game's own entry point through `setLogFile` to `ux0:data/MuggleBall3/log.txt`
  is for the Vita front end (M5b).
- **Threads** (`engine/src/Physics.cpp`, `makeJobSystem`): unchanged. `hardware_concurrency()` returning 0 (very
  likely on the Vita; unverified until the hello's probe runs there) already takes the existing clamp,
  `std::clamp(0 - 1, 0, 3)` with the subtraction in `int` before the clamp, to 0 workers, which is
  `JobSystemSingleThreaded` — the single-threaded job system a `threads = 0` setting also asks for. So the
  existing code needed no change for this; the Vita profile sets `threads = 0` explicitly anyway, rather than
  leaving it to chance.
- **`random_device`**: left as is, per the plan (§5.4): the device probe showing whether it throws on the Vita
  has not run yet. `game/include/mb/game/SoundLibrary.h:69`, `app/PlaySession.cpp:145` and `app/main.cpp` are
  the call sites; a replacement seeded from `SDL_GetPerformanceCounter` is ready to drop in if the probe shows
  it is needed.
- **The physics profile** (`game/include/mb/game/PhysicsProfile.h`, `game/src/PhysicsProfile.cpp`): "desktop" is
  `PhysicsSettings{}` unchanged (verified identical trajectories, below). "vita" keeps the fixed step and solver
  iterations the desktop uses (docs/PHYSICS.md, "The solver"; a lighter solver waits for M3b's device benchmark),
  runs single-threaded with one explicit body-mutex stripe, caps `maxStepsPerCall` at 4 (two 30 fps frames), and
  sizes the world from measurement rather than the desktop's 32768-body headroom: `mb_physics_smoke --measure`,
  extended to track peak `bodyCount()`/`activeBodyCount()`/`tempAllocatorPeakBytes()`, found **374 bodies, 372 of
  them awake at once** over all 83 levels and every starting configuration (both on `knight-miss-1457`). The rule
  (docs/PHYSICS.md, "The Vita profile") gives `maxBodies` 1024 (next power of two at or above 2x374),
  `maxBodyPairs` 8192, `maxContactConstraints` 4096. `tempAllocatorBytes` needed its own measurement run with
  `--physics-profile vita`, because the step allocator's peak is mostly Jolt's per-step contact-constraint buffer,
  488 bytes per `maxContactConstraints`, so it must be measured with the profile's own constraint count in force:
  the peak then is **2,187,184 bytes**, giving **5 MiB** (next whole MiB at or above 2x that). A bare `--measure`
  (the desktop's 32768 constraints) measures the same levels at **16,179,120 bytes**, exactly (32768 - 4096) x 488
  more; the thread count makes no difference (checked with `--threads 0` and `3` under both profiles). The two
  profiles play identically (`droppedSteps()` is zero either way). Re-measure if `maxContactConstraints` changes.
  5 MiB is close to the 2-4 MiB the plan guessed (section 5.5) before anything measured it. The four windmill
  levels keep their own override at 100 velocity iterations, pinned separately from the base settings so a later
  retune of the other 79 levels cannot quietly drop them too. `--physics-profile <name>` (default
  `defaultPhysicsProfileName()`: "vita" under `MB3_PLATFORM_VITA`, else "desktop") is now a flag of `MuggleBall3`
  (including the level viewer) and all three physics smokes; desktop runs that never pass it are unaffected
  (checked below). The profile table itself is a function-local static built the first time anything calls
  `physicsProfile()`, not a plain global: a global here would have its construction order relative to other
  translation units' globals left unspecified, which the three smokes' own `gProfile` globals hit as a
  `bad_alloc` before this fix (`game/src/PhysicsProfile.cpp`). `GameSession` gained its own `droppedSteps()`
  (its `update()` accumulator's drops plus the world's), because a session's accumulator can itself drop whole
  steps on a long pause the same way `PhysicsWorld::step()` does; `PlaySession` and `mb_gameplay_smoke` report
  that total, not just the world's own count.
- **Behaviour unchanged on the desktop, verified**: `mb_physics_smoke --shots 4 --seconds 10` against the main
  branch's own build and this worktree's gives identical per-level numbers (same seeds, same default settings,
  same trajectories) — see the PR description / M3a report for the pasted comparison. All three physics smokes
  also pass with `--physics-profile vita` on the host at their default lengths, with zero dropped steps (they
  step at a fixed dt, which never exceeds `maxStepsPerCall` at 4); see the M3a report for the pasted summaries
  and temp allocator peaks. (A 20 s `mb_physics_smoke --physics-profile vita` run separately fails on
  `village-ogorod` and `village-park` — the same failure the desktop profile already has at 20 s, pre-existing
  and out of scope here, not a Vita-profile regression.)

### M5b: the Vita front end (2026-10-24)

`ConsoleInput` (`engine/include/mb/engine/ConsoleInput.h`/`.cpp`) is the platform-neutral piece docs/VITA_PLAN.md
5.8 describes: it owns a virtual cursor and turns the joystick's 16 positional buttons (confirm, back, Start, the
d-pad), the left stick and the front touch screen into the same `InputEvents` the mouse and keyboard already
fill, so `game/` needs no Vita-specific code at all. It is compiled everywhere (so `tests/test_console_input.cpp`
covers every rule on the host with no window, SDL events or device) but only *instantiated* on the Vita;
`Window.cpp` (`MB3_PLATFORM_VITA`) is the only caller. Deviations from the header's comments, each small:

- **mouseX/Y and a plugged-in USB mouse.** The header says `Window.cpp` sets `input.mouseX/Y` from the cursor;
  in fact `ConsoleInput::frame()` sets them directly (one less place to get it wrong), and `Window.cpp` simply
  does not call `SDL_GetMouseState` on the Vita, instead of setting the same value twice. One real consequence:
  a USB mouse on a PSTV still produces clicks and drags (its `PointerSample`s go into `input.pointer` exactly as
  on desktop), but its absolute position does not drive the virtual cursor — only the stick, the d-pad and touch
  do. Buttons and touch are the device checklist's actual target; a mouse is a bonus "still works" path the plan
  never asked to be pixel-accurate.
- **`input.leftButton` vs. the pointer samples' `leftDown`.** `leftButton` (the whole-frame flag) is
  `confirmHeld_ || touchHeld_` unconditionally, including while text input is on — the header's rule for it says
  exactly that, so a field editing the name still reports confirm as "held". Individual `PointerSample::leftDown`
  values are different: `press()`/`release()` write an explicit true/false (mirroring `Window.cpp`'s own
  mouse-button events, which never feed `mouseDX`/`leftDragDX`), so a confirm press while text input is on sends
  `SDLK_RETURN` and no press sample at all, and the "release the pointer" edge case (confirm held, then text
  input turns on) emits an explicit release even though `leftButton` itself would still read true.
- **A touch "landing" sample.** A finger touching down always moves the cursor there first (so a later motion is
  continuous, and `cursorX()`/`cursorY()` are right immediately), whether or not it turns out to be a menu press
  or a play-mode tap. A play-mode tap is therefore three `PointerSample`s (the landing move, then the tap's press
  and release at the lift point), not two: `test_console_input.cpp`'s
  `testPlayModeTapPressesAndReleasesAtTheLiftPoint` checks that order precisely, rather than assuming two.
- **Start-while-paused** still sends Escape (quits to the main menu on a second press), as the header says to do
  until this milestone settles it with the user — see "Open questions" below.

**The virtual cursor.** The menus need a visible pointer wherever the system doesn't offer one. No cursor-shaped
asset survived conversion: `MB3cursor.ani` is not converted, and a grep of `Data/Menu/*.json` for anything
cursor- or pointer-like, and of `Textures/Forms` and `Textures/Icons` for a wired-in pointer image, found
nothing (`Textures/Forms/Arrow.png`, `Big-Point.png` and `Small-point.png` exist but are not referenced by any
menu form, so their actual appearance and intended use are unknown — using them blind risked a worse result than
the documented fallback). `FrontEnd.cpp` therefore draws the plan's fallback: a small crosshair built from tinted
`Sprite`s (texture 0 draws a plain colour, as the fade-to-black cover already does), a dark ~5 px outline under a
light 1 px cross about 16 px across, appended after the menu's own sprites so it is always on top. It shows
whenever the mode is not `Playing` (the menus, Loading, and the pause screen), at `input.mouseX/Y`.

**The Options screen.** `menu_.setVideoModes({})` already hid the window-size list (there is nothing to resize to
besides the one 960x544 display); `Menu::setFullscreenAvailable(false)` is the new, parallel way to hide the
Fullscreen check box, the same way (`UiWidget::visible = false`), called from `FrontEnd` under
`MB3_PLATFORM_VITA`. `tests/test_menu.cpp` mirrors the existing "no video modes" test for it.

**The entry point and the VPK.** `platform/vita/game/main.cpp` sets the clocks (444/222/222/166 MHz, logged),
opens the log at `ux0:data/MuggleBall3/log.txt`, logs the SDL version and `mb::assetRoot()` (vitaGL has no
version number of its own to log; its `GL_VERSION`/`GL_RENDERER`/GLSL string, the pinned commit of which is
above, comes from the renderer's own start-up a moment later), logs a clear line if neither `app0:/assets` nor
`ux0:data/MuggleBall3/assets` has `Data/Levels`, then calls `mb_app::runGame(mb_app::GameOptions{})` in a
try/catch and exits with `sceKernelExitProcess`. `app/CMakeLists.txt` now builds the six sources `runGame`
needs (`FrontEnd`, `FrontEndAudio`, `GameAudio`, `PlayerInput`, `PlaySession`, `Game.cpp` itself) as a static
library, `mb_app`, so the desktop `MuggleBall3` (with `main.cpp`'s harness and `FormViewer.cpp`) and the Vita's
own executable both link it instead of each listing the same six files. `cmake/Vita.cmake`'s `mb3_vita_vpk`
needed no change: naming the Vita executable target `MuggleBall3` is what makes its VPK
`MuggleBall3.vpk` — the name the CI job and the plan upload.

Cross-build result (2026-10-24, host `build-vita`, deleted afterward per the disk rule): `mb_engine`, `mb_game`,
`mb_app`, the hello, the render test and `MuggleBall3.vpk` all build with **zero warnings and zero errors** from
this project's own files (every "note:" GCC prints is its standard "parameter passing changed" ABI note, from
`<format>`, `nlohmann::json` and `std::stable_sort`'s iterator types in SDK and dependency headers, not from
anything this milestone wrote). `unzip -l MuggleBall3.vpk`: `param.sfo` (`TITLE_ID MUGB00003`, `TITLE`/`STITLE`
"Muggle Ball 3"), `eboot.bin`, the five LiveArea files — no data file. `arm-vita-eabi-nm MuggleBall3` shows
`_newlib_heap_size_user`, `sceUserMainThreadStackSize` and `_pthread_stack_default_user` as `D` symbols and
**zero** lines with ` U ` (no undefined symbols). The hello's own VPK now carries title ID `MUGB00009` ("MB3
Hello") instead of the game's `MUGB00003`, so both install and sit on the LiveArea at once.

## Controls

- **Confirm** is Cross, or Circle when the system's own enter-button setting says so (read once at start-up with
  `sceAppUtilSystemParamGetInt`, exactly as the hello does): the left mouse button at the drawn cursor, held for
  as long as it is held. It clicks menu widgets, drags sliders, launches the ball (the press edge, same rule as
  a mouse click or Space) and resumes from the pause screen. While a text field has the keyboard, it sends Enter
  instead (the on-screen field's own Enter, not the IME's — the IME sends its own Return when confirmed).
- **Back** is the other of the two buttons: Escape. It pauses a level, goes back a menu screen, and quits to the
  main menu when pressed again while paused.
- **Start** is Escape too. Because pressing it twice while paused abandons the level (quits to the main menu),
  rather than, say, resuming play, **this needs the user's confirmation** — see "Open questions" below; nothing
  device-side depends on the answer, since both readings are a single `SDLK_ESCAPE`, and changing which one
  "wins" only needs `GameFlow.cpp`'s pause handling, not `ConsoleInput`.
- **The left stick and the d-pad** move a small drawn crosshair (there is no system cursor): the stick scaled
  from a dead zone so full deflection reaches 900 px/s, the d-pad always at that speed; in play, the crosshair
  is the bat's target exactly as the mouse cursor is on desktop (`batTargetX`).
- **The front touch screen in the menus**: a tap is a click where the finger is; dragging a finger drags
  whatever is under it (a slider). **In play**: a finger moving only sets the bat's target, so a hand landing on
  the screen never launches the ball; a short tap — released within 0.2 s, moved less than about 12 px — both
  presses and releases at the point it lifted at (not where it landed; the two differ by less than 12 px), which
  is what launches. A drag, or a touch held too long, never presses.
  The rear touch pad is disabled (`SDL_HINT_VITA_ENABLE_BACK_TOUCH=0`), so a palm there does nothing.
- **No touch screen (PSTV)**: every screen works with the buttons and the stick alone — there is nothing in the
  menus, the pause screen or play that touch does and the stick/d-pad/confirm cannot also do.
- **Name entry** opens the system's IME (`sceImeDialog`, through `SDL_StartTextInput`) the first time the high
  score or Game Over form's text field gets the keyboard. The dialog is one-shot: it opens empty, and once it
  closes (confirmed, which also sends an automatic Return, or cancelled) nothing SDL does reopens it on its own.
  **To reopen it** — after a cancel, or to retype the name — press confirm again while the field still has the
  keyboard and no IME is currently shown; `Window.cpp` notices and does `SDL_StopTextInput` then
  `SDL_StartTextInput`, and that press is *not* also forwarded to `ConsoleInput` as the Enter that would
  otherwise submit the field's current (possibly empty) text in the very press meant to reopen the dialog — the
  two are mutually exclusive, decided before either happens. Characters the bitmap font's glyph table does not
  have are silently dropped, as on desktop.
- **The PS button, sleep and wake**: SDL sends no background/foreground events on the Vita, so this plays out
  through `sceAppMgrReceiveSystemEvent` instead — on `SCE_APPMGR_SYSTEMEVENT_ON_RESUME` while a level is playing
  (the cursor hidden), `Window.cpp` pushes one `SDLK_ESCAPE`, which pauses the level exactly as if the player had
  pressed Back; the frame's dt is separately clamped to 0.1 s, so the clock jump the sleep caused does not drive
  a burst of dropped physics steps. See the device checklist for what the real hardware does here.

## Installing the game

1. Copy `MuggleBall3.vpk` to the console (VitaShell's FTP or USB) and install it from VitaShell. The bubble is
   named "Muggle Ball 3" (title ID `MUGB00003`); `mb3_vita_hello.vpk` (`MUGB00009`, "MB3 Hello") can be installed
   alongside it.
2. The VPK carries no game data (a code-only build, docs/VITA_PLAN.md 5.6): copy the converted asset pack —
   `tools/vita_pack.py`'s output from your own copy of the game — to `ux0:data/MuggleBall3/assets` on the
   console (VitaShell's FTP or USB). `mb::assetRoot()` looks there (after `app0:/assets`, which only a personal
   `MB3_VITA_BUNDLE_ASSETS` build has); the game logs a clear line at start-up if it finds `Data/Levels` in
   neither place.
3. The log (`ux0:data/MuggleBall3/log.txt`) and the save files (settings, progress, high scores) live under
   `ux0:data/MuggleBall3/`, next to the asset pack above (`SDL_GetPrefPath("", "MuggleBall3")`).
4. Send back `log.txt`, and anything from the device checklist below that needs a description, a photo or a
   video (the touch-only and buttons-only runs especially).

## Open questions for the user

- **Start while paused.** It currently sends Escape, the same as Back, so pressing it a second time while
  paused quits to the main menu (abandoning the level) rather than resuming play. Is that the behaviour you
  want, or should Start resume instead (leaving Back as the only way to quit from the pause screen)?
- **UI scale at 960x544.** The menus and the HUD are laid out and drawn exactly as they are on desktop
  (`UiLayout::fit`), just at a smaller target; nothing has been measured yet for whether text, button hit
  targets or the drawn cursor read comfortably from a Vita's or a PSTV's typical viewing distance. Flag anything
  that looks too small (or too large) once you have it on hardware, and this can be tuned in a later pass.

## Known limits and open questions

- Nothing has run on a console yet; everything under "Device checklist" is open.
- The enter-button setting, the IME, touch-as-touch, the BGM audio port's behaviour under the system music player,
  `rename` over an existing file, `random_device`, the thread affinity call and the real speed of Jolt are all
  answered by the hello's log and probe.
- The user's answers to the plan's questions (section 3.1) are pending: the console model and firmware, whether
  `libshacccg.suprx` is extracted, how files move, Vita3K, asset delivery, title IDs, and whether the app may set
  the clocks.

## Device checklist

M1a (tick with the date and paste the relevant log lines):

- [ ] the hello installs from the VPK and shows its icon and name on the LiveArea;
- [ ] the triangle draws (orange, lit, turning; the back face disappears as it turns away);
- [ ] the log shows the shader compiled (status 1) and how long it took, and the vitaGL memory pools;
- [ ] vcount deltas show a two-vblank cadence (the "at 2 vblanks" percentage);
- [ ] the IME opens, confirms (text logged) and cancels (nothing logged, no hang);
- [ ] buttons and the touch screen respond as logged, and the back button exits cleanly;
- [ ] `probe.txt` comes back; its findings are recorded above.

M4a (tick with the date and paste the relevant log lines):

- [ ] `mb3_vita_render_test.vpk` installs and shows its icon and name ("MB3 Render Test") on the LiveArea;
- [ ] all 19 render tests pass ("all checks passed" is the last line before the exit code);
- [ ] `results.txt` comes back, from `ux0:data/MuggleBall3/render-test/`, and `SDL_Log.txt` from `ux0:/data/`;
- [ ] if any check failed, its PNG (same directory) comes back too, and the failure is recorded above;
- [ ] the main menu's tiled/scrolling UI sprites (the `Sky.jpg` background, any tiled form) are not blurred or
      crawling at their tile seams — review finding: vitaGL's fragment preamble is `mediump float`, and if its
      GLSL translator treats that as fp16, a `mediump` UV has only about half a texel of precision at a
      texture coordinate near 1.0, which REPEAT and NPOT tiling (handoff §4.3) reach constantly; fixed to
      `highp` in the three fragment shaders' `vUv`, but worth confirming on the real GPU translator.

M5b (tick with the date and a short note once tried on hardware or in Vita3K):

- [ ] two levels played with touch only (no buttons, no stick): the bat follows a dragging finger, a tap
      launches the ball, nothing else does;
- [ ] two levels played with buttons and the stick only (no touch, as on a PSTV): the stick or d-pad moves the
      bat, confirm launches;
- [ ] touching to move the bat during play never launches the ball by itself (only a tap, or confirm, does);
- [ ] everything above, and the menus, work the same with the system's enter button set to Circle instead of
      Cross (so back/confirm are swapped);
- [ ] pause (Back or Start while playing) and resume (confirm, or a touch press on the pause screen — moving the
      stick or d-pad alone does **not** resume: `GameFlow`'s Paused case only takes a key other than Escape or a
      pointer press edge, and a stick/d-pad move is neither) both work from buttons and touch; quit to the main
      menu (Back or Start again while paused) too;
- [ ] high-score name entry: the IME opens, typing and confirming enters the name; cancelling, then pressing
      confirm again on the name field, reopens the IME (that same press does not also submit the field's current
      text — the two are mutually exclusive, see "Controls" above); an empty name still enters as "Player";
      **unverified**: whether sceCtrl still delivers button data to the app while the IME dialog is on screen —
      if it does, Circle inside the dialog would also reach ConsoleInput as Escape and `Menu::back()` would leave
      the name form underneath it;
- [ ] Options' changes (volumes, Smart Camera, Freeze Sky, Shadows, Mirrors) persist across a quit and restart
      (there is no window size or Fullscreen box to check: confirm they are indeed hidden);
- [ ] the PS button, sleep and wake mid-level: the game pauses (per "Controls" above) and nothing hangs or loses
      the save;
- [ ] the drawn crosshair is visible and reads clearly against the lightest and darkest menu backgrounds.

## Legal

The game data is the user's own copy, converted locally and never committed; a VPK that contains it is for the
user's device only and is never uploaded. CI builds data-free VPKs. No Sony file (firmware, `libshacccg.suprx`,
SDK pieces under NDA) is committed or bundled. vitaGL is LGPL-3.0: the app's source and build scripts are in this
repository, so the app can be relinked against a modified vitaGL.
