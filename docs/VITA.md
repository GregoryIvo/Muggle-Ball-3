# PlayStation Vita: building, installing, what was measured

The plan behind the port is [`VITA_PLAN.md`](VITA_PLAN.md); this is the platform's working document, in the
style of [`MACOS.md`](MACOS.md): how to build, what was decided and why, the numbers measured on the console,
and the device checklist. Dates are when a thing was checked. Where a measurement contradicts the plan, the
measurement wins and is recorded here.

**Status (2026-10-23):** M1a. The toolchain is installed and pinned, Jolt cross-compiles unchanged through its
"Platform Blue" hook, and a hello VPK (`mb3_vita_hello.vpk`, title ID `MUGB00003`) builds: it draws a lit triangle
with the game's mesh shader in GLSL ES over vitaGL, paces at two vblanks, probes the IME, SDL's GPU API, the audio
device, the file system, threads and clocks, steps a Jolt world and logs everything. It has not run on a console
yet (see "Device checklist"). With `MB3_VITA_BUILD_ENGINE=ON`, `mb_engine` and `mb_game` already cross-compile
unchanged (67 objects, the SDL GPU renderer files included, which link because the SDK's SDL3 exports the GPU
API with no back end); the portability fixes, the dropped-step counter and the physics profile are M3a.

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
export VITASDK=/Users/greg/Documents/MagicBall3/vitasdk PATH=$VITASDK/bin:$PATH
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
export VITASDK=/Users/greg/Documents/MagicBall3/vitasdk PATH=$VITASDK/bin:$PATH
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
| `MB3_VITA_BUILD_ENGINE` | OFF | Also cross-compile `mb_engine` and `mb_game` (work in progress until M3a) |
| `MB3_BUILD_TESTS` | OFF on Vita | A cross build cannot run them |
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
bubble). `tests/test_vita_livearea.py` checks the committed files' sizes and colour type. A personal set made
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
   (the statistics are written and reset); Select writes the statistics and the memory pools now.
4. Send back `log.txt` and `probe.txt` (and a photo if the triangle looks wrong).

Memory settings in the hello: newlib heap 160 MiB, main thread stack 1 MiB, default pthread stack 512 KiB
(without it a `std::thread` gets 4 KiB), vitaGL RAM threshold 16 MiB. The game's values are decided in M6b.

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
  back end behind it: SDL has `d3d12`, `metal`, `vulkan` and `xr` only). So the engine's SDL GPU renderer
  *links* on Vita and fails at run time; M3a can cross-compile `mb_engine` unchanged, and the vitaGL back end
  (M4a) replaces it through the renderer seam (M2).
- **SDL3 on Vita** (its `main` branch, read 2026-10-23): the video driver touches no GXM, so vitaGL can own the
  display while SDL does events, joystick, touch and audio. SDL never calls `sceCommonDialogUpdate`: the app
  presents with `vglSwapBuffers(GL_TRUE)` while `SDL_ScreenKeyboardShown` is true. The IME is the one-shot
  `sceImeDialog` (the whole string in one `TEXT_INPUT`, then Return if confirmed; a cancel sends nothing). Audio
  is S16 only; below 48 kHz SDL opens the BGM port at the asked rate, unresampled. `SDL_GetPrefPath("",
  "MuggleBall3")` is `ux0:/data/MuggleBall3/`; the base path is `app0:/`. The joystick is positional (0
  Triangle, 1 Circle, 2 Cross, 3 Square, 4 L1, 5 R1, 6 Select, 7 Start; the d-pad is hat 0), SDL reads no
  enter-button setting, and whether a gamepad mapping exists is logged by the hello.
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

## Legal

The game data is the user's own copy, converted locally and never committed; a VPK that contains it is for the
user's device only and is never uploaded. CI builds data-free VPKs. No Sony file (firmware, `libshacccg.suprx`,
SDK pieces under NDA) is committed or bundled. vitaGL is LGPL-3.0: the app's source and build scripts are in this
repository, so the app can be relinked against a modified vitaGL.
