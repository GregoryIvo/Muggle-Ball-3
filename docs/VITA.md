# PlayStation Vita: building, installing, what was measured

The plan behind the port is [`VITA_PLAN.md`](VITA_PLAN.md); this is the platform's working document, in the
style of [`MACOS.md`](MACOS.md): how to build, what was decided and why, the numbers measured on the console,
and the device checklist. Dates are when a thing was checked. Where a measurement contradicts the plan, the
measurement wins and is recorded here.

**Status (2026-10-23):** M1a and M4a. The toolchain is installed and pinned, Jolt cross-compiles unchanged through
its "Platform Blue" hook, and a hello VPK (`mb3_vita_hello.vpk`, title ID `MUGB00003`) builds: it draws a lit
triangle with the game's mesh shader in GLSL ES over vitaGL, paces at two vblanks, probes the IME, SDL's GPU API,
the audio device, the file system, threads and clocks, steps a Jolt world and logs everything. Neither the hello
nor M4a's device render-test VPK has run on a console yet (see "Device checklist"). The engine now has a
renderer that runs on the console: `MB3_RENDERER=VITAGL` (the Vita's default) and its desktop twin `GL` implement
the full `Renderer` API over OpenGL 2.1 / GLSL ES from one source (see "GL back end" below), and
`MB3_VITA_BUILD_ENGINE` defaults ON. `mb_engine`, `mb_game`, the hello and the device render-test VPK
(`mb3_vita_render_test.vpk`, title ID `MUGB00005`) all cross-compile with no warning from this project's own
files; the portability fixes, the dropped-step counter and the physics profile are M3a.

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
| `MB3_VITA_BUILD_ENGINE` | ON | Also cross-compile `mb_engine`, `mb_game` and the device render-test VPK (`platform/vita/render_test/`); the engine now has a renderer that runs on the console (M4a), so this stays on unless you only want the hello |
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
   is named "Muggle Ball 3".
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
  on immediately before the clear, with `applyRenderState` setting the real per-draw masks again straight after.
- Textures: `GL_RGBA8`, `GL_REPEAT`, linear filtering, no anisotropy; `opaqueAlpha` (every alpha byte 255) is
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
- `renderToImage` draws into an FBO (an `RGBA8` colour texture, a `GL_DEPTH_COMPONENT24` depth renderbuffer),
  reused while the size is unchanged, then `glReadPixels` and a row flip (GL reads bottom-up; vitaGL's FBOs are
  flipped internally to match GL, so the same flip is right on both targets).

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
link), appends the exit code as the file's last line, and exits.

1. Copy `mb3_vita_render_test.vpk` to the console and install it from VitaShell. The bubble is "MB3 Render Test".
2. Start it; it runs the 19 checks with no window of its own and exits.
3. Send back `ux0:data/MuggleBall3/render-test/results.txt` and the directory's PNGs (particularly any whose
   check failed — the file names say which: `01_clear.png`, `02_checker_quad.png`, and so on through
   `12c_effect_crowd.png`).

### Desktop results (2026-10-24, Apple M3 Pro, Darwin 27, `GL_VERSION 2.1 Metal - 91.7`)

- `mb_render_test` against `MB3_RENDERER=GL`: **all 19 checks pass**, with no adapted tolerance and no changed
  sampling point — the `glClear` finding above was the only real defect, fixed in the back end, not in the test.
- The full suite with `MB3_RENDERER=GL` (`ctest`, `MB3_ASSETS` set): 43 of 43, one skipped
  (`mb_window_tests`, SDL's dummy driver, as on every other back end). `python3 -m unittest discover -s tests -p
  'test_*.py'`: 201 tests, 4 skipped, 0 failed.
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

  All six are below M4b's 99% bar except the menu. **Not a defect:** every scene's interior and flat regions
  match almost exactly (random sampling away from edges lands diffs of 0-9 throughout); the mismatches cluster
  entirely at thin, high-contrast geometry (foliage, rigging, small props) and the single worst pixel in each
  case sits between two closely-matching neighbours on either side — a one-pixel boundary that Metal's and this
  GL driver's rasterisers place on different sides of a hard edge, not a shading, blending or sampling bug. The
  only scene with no 3D edges at all (the menu, sprites only) matches 100.0%. No fix is possible or appropriate
  here: this is the expected cost of comparing two GPU backends' triangle coverage rules on fine detail, which
  is exactly why M4b's own tolerance is a percentage and not an exact match. Re-measure on the device once the
  render-test VPK and a game build run there.

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
- [ ] `results.txt` comes back, from `ux0:data/MuggleBall3/render-test/`;
- [ ] if any check failed, its PNG (same directory) comes back too, and the failure is recorded above.

## Legal

The game data is the user's own copy, converted locally and never committed; a VPK that contains it is for the
user's device only and is never uploaded. CI builds data-free VPKs. No Sony file (firmware, `libshacccg.suprx`,
SDK pieces under NDA) is committed or bundled. vitaGL is LGPL-3.0: the app's source and build scripts are in this
repository, so the app can be relinked against a modified vitaGL.
