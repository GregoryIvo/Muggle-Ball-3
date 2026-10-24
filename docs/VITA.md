# PlayStation Vita: building, installing, what was measured

The plan behind the port is [`VITA_PLAN.md`](VITA_PLAN.md); this is the platform's working document, in the
style of [`MACOS.md`](MACOS.md): how to build, what was decided and why, the numbers measured on the console,
and the device checklist. Dates are when a thing was checked. Where a measurement contradicts the plan, the
measurement wins and is recorded here.

**Status (2026-10-24):** M3b. The toolchain is installed and pinned, Jolt cross-compiles unchanged through its
"Platform Blue" hook, and a hello VPK (`mb3_vita_hello.vpk`, title ID `MUGB00003`) builds: it draws a lit triangle
with the game's mesh shader in GLSL ES over vitaGL, paces at two vblanks, probes the IME, SDL's GPU API, the audio
device, the file system, threads and clocks, steps a Jolt world and logs everything. It has not run on a console
yet (see "Device checklist"). `MB3_VITA_BUILD_ENGINE` defaults ON: `mb_engine` and `mb_game` cross-compile
unchanged (the SDL GPU renderer files included, which link because the SDK's SDL3 exports the GPU API with no
back end), with the portability fixes below, the dropped-step counter (`PhysicsWorld::droppedSteps()`) and the
"vita" `PhysicsProfile` added in M3a. A second Vita VPK now cross-compiles beside the hello: the physics
benchmark (`mb3_vita_bench.vpk`, title ID `MUGB00004`; see "Physics benchmark" below), which links `mb_engine`
and `mb_game` into an executable for the first time on this platform and draws its progress with SDL's own GXM
renderer rather than vitaGL. Its Mac twin (`mb_physics_bench`) has run pass 1 of the plan's 15-level set; the
device has not yet (see its own "Device checklist"). The app's own front-end VPK is still M5b, once M2's
renderer seam lands a vitaGL backend (M4a).

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
| `MB3_VITA_BUILD_ENGINE` | **ON** (M3a; was OFF before it) | Also cross-compile `mb_engine` and `mb_game`, which belong in every Vita build from here on; the app's VPK follows in M5b |
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

## Physics benchmark (M3b)

`app/bench/PhysicsBench.{h,cpp}` is the bench core, shared by two targets: `mb_physics_bench` on the Mac
(`app/CMakeLists.txt`) and `mb3_vita_bench.vpk` (title ID `MUGB00004`, `platform/vita/bench/`) on the console,
which compiles the same `PhysicsBench.cpp` straight in rather than linking a shared library across the two
very different build graphs. Both read the same `bench-physics.json` and write the same CSV columns, so a
host run and a device run of the same JSON line up level by level — the point of the whole milestone: the
per-level ratio of the two replaces the plan's estimated speed factor (section 4.4) with a measured one.

**What it measures.** For each level it builds a `GameSession` (the Autopilot playing it, 99 hearts so a lost
ball never ends the run early) and calls `session.update(fixedStep, input)` once per planned step, timing
each call with `steady_clock`. That is **one physics step plus the rules** (the bat easing towards the
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
collisionSteps, threads)` and prints, per level, the host and device mean/p95 step times, the two ratios and
the dropped steps, then the median and maximum ratio over every matched level — the measured scale factor —
and flags any level whose device p95 is over the plan's 8 ms per-step budget, or whose device mean is over
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
- [ ] the measured scale factor (`tools/bench_ratio.py` against the host CSV above) replaces the 70x estimate
      in this document, and the per-level estimates in section 4.4 are recomputed against it;
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
