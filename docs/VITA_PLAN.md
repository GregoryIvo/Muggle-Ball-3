# Handoff brief: PlayStation Vita port of Muggle Ball 3

You are Claude Opus in Claude Code, working in `/Users/greg/Documents/MagicBall3`. You remember nothing of the conversation that produced this brief. The user asked for a PlayStation Vita (homebrew) port of Muggle Ball 3, planned and built end to end. It will take many sessions.

The research below is from 2026-10-22, so you don't need to repeat it. Anything marked **(verify)** is an inference or unconfirmed, so check it before you build on it. Version numbers and URLs change quickly in the Vita scene. If a measurement contradicts this brief, trust the measurement, say so, and update the docs.

## 0. Start of every session

1. Read `/Users/greg/.claude/projects/-Users-greg-Documents-MagicBall3/memory/MEMORY.md` and every file it links.
2. **First session only:**
   - Create the memory file `vita-port.md` in that memory folder. Link it from `MEMORY.md` using the same one-line format as the other entries. It holds:
     - the status of each milestone;
     - the user's answers to §3.1;
     - pinned versions: the SDK channel, the output of `arm-vita-eabi-g++ --version`, the vitaGL commit and its flag line, and the CI container digest;
     - open requests to the user for device runs;
     - live worktrees and branches.
   - Create the M1 worktree (§2) and commit this brief, unchanged, as `docs/VITA_PLAN.md` on that branch. Until M1a merges, `vita-port.md` records where the brief lives.
3. **Later sessions:** read `vita-port.md`, `docs/VITA_PLAN.md` (in main, or in the M1 worktree until it merges) and `docs/VITA.md`. Then resume from the first milestone that is not done. §2 defines "done".
4. In the repo `/Users/greg/Documents/MagicBall3/Muggle-Ball-3`, read:
   - `docs/ARCHITECTURE.md`, `docs/PHYSICS.md` (especially "The solver"), `docs/AUDIO.md`, `docs/SAVES.md`, `docs/ASSET_FORMATS.md`, `docs/MACOS.md`;
   - `engine/include/mb/engine/Renderer.h`, `engine/src/RendererImpl.h`, `engine/include/mb/engine/Window.h`, `engine/include/mb/engine/Physics.h`.
5. Update `vita-port.md` whenever a milestone, an answer, a pinned version or a device request changes.

## 1. Goal and definition of done

The port is done when all of the following are true.

1. **The VPK works on hardware.** A `.vpk` installs through VitaShell on a Vita or PSTV running HENkaku, Enso or h-encore with taiHEN. It plays the whole game:
   - all 83 levels, the campaign map, the menus and the pause screen;
   - options, high-score name entry, and saves that persist;
   - full play with the front touch screen alone (Vita);
   - full play with the physical buttons and sticks alone (Vita and PSTV). A PSTV has no touch screen, so every screen must work without touch.

2. **Frame rate.** The target is 30 fps locked to every second vblank, with the ARM at 444 MHz. It is measured by the on-device game benchmark (built in M6b, run in M7): 60 s of Autopilot play on each of the 83 levels, excluding the first 2 s after each level load.

   Definitions:
   - **Presented interval:** the change in `sceDisplayGetVcount` between consecutive swaps. vitaGL presents from a display queue, so time measured on the main thread does not show this interval.
   - **Work time:** time from the start of the frame to the return of `vglSwapBuffers`, minus any time spent blocked waiting for a free display buffer or a vblank. Measure it with `sceKernelGetProcessTimeWide`. If the wait cannot be separated out, measure from frame start to the call of `vglSwapBuffers`, and say in `docs/VITA.md` which method you used.
   - **Dropped steps:** whole fixed physics steps thrown away because of `maxStepsPerCall`, counted by the new `droppedSteps()` (M3a).

   Each level must show:
   - at least 97 % of presented intervals at exactly 2 vblanks, and none longer than 4;
   - mean work time of 25 ms or less, and p95 work time of 30 ms or less;
   - zero dropped steps.

   Report both the vblank numbers and the work times: a GPU-bound frame shows up only as a missed vblank.

   Menus may run at 30 or 60 fps. If the M3b device benchmark shows the target can't be met without breaking how levels behave, stop and agree a new target with the user. Do not quietly lower it.

3. **CI.** A new `vita` job in `.github/workflows/build.yml` cross-compiles the code and uploads a VPK artifact that contains no game data. The existing `build` (macos-14, ubuntu-24.04) and `package-macos` jobs stay green. All ctest targets pass (43 today, plus any you add), and so do the Python tests.

4. **Desktop untouched.** The default configuration on macOS and Linux behaves exactly as before:
   - same SDL GPU renderer, same dependencies;
   - `tests/render_test.cpp` gives identical results;
   - no new desktop requirements.

   The gamepad and controller mapping is compiled only for Vita (`MB3_PLATFORM_VITA`). Desktop gains no gamepad behaviour unless the user asks for it.

5. **Docs.** `docs/VITA.md` is the platform doc, in the style of `docs/MACOS.md`. It covers:
   - building, installing and copying assets;
   - controls and settings;
   - known limits and the measured device numbers;
   - a "Device checklist" section (§2).

   Update ARCHITECTURE, PHYSICS, SAVES, AUDIO, ASSET_FORMATS and README wherever the port changes what they describe.

6. **Suspend and resume.** The PS button, sleep and wake all work. Audio and rendering come back, and no saves are lost.

**Physics budget** (this definition applies everywhere in this brief): at most 16 ms of physics at p95 in each 33.3 ms frame. That is two 1/60 s steps of at most 8 ms each.

## 2. Ground rules

### User and project conventions (follow exactly)

1. **Roles.** Opus orchestrates. Sonnet agents (`model: 'sonnet'`) implement in parallel git worktrees. Use one branch and one worktree per (sub-)milestone:
   - branch `claude/vita-mN-<slug>`, matching the repo's existing `claude/<slug>` branches;
   - worktree `/Users/greg/Documents/MagicBall3/mb3-worktrees/vita-mN-<slug>`.

   The user's git identity (GitHub GregoryIvo) is already configured. Don't change it.
2. **Contracts first.** Write the shared contracts yourself before you hand work out: headers, CMake options and format docs.
3. **Two independent reviewers** check every piece of work, each with one lens:
   - **fidelity:** does it match desktop and original behaviour?
   - **robustness:** failure paths, memory, portability, the CI rules.

   Then fix the findings, re-check, and open the PR.
4. **One PR per milestone.** A large milestone is split into sub-milestones (M1a, M1b, …), and each sub-milestone is one PR. §6 suggests the splits.
5. **Merging.** The user has given standing permission to merge a PR once its CI is green. Never merge a PR with red CI.
   - Merge once CI is green and the host checks pass. Device acceptance items do not hold up the merge. They go into the "Device checklist" section of `docs/VITA.md`, and a later docs PR ticks them when the user reports back.
   - A milestone is **done** only when its device items are ticked.
   - The next milestone may start once the previous PR merges. There are two exceptions: M7 needs M3b's device numbers, and M8 needs everything else to be done.
6. **Commit trailers:**
   - your commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`;
   - Sonnet agents' commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
7. **PR bodies** end with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
8. **Never commit game data or anything made from it.** `assets/` is gitignored. It is converted from the user's own copy:
   - original install: `/Users/greg/Documents/MagicBall3/Magic Ball 3`;
   - MoleBox-dumped data: `/Users/greg/Documents/MagicBall3/Extracted/MB3OUT`;
   - converter: `tools/convert_assets.py`.

   The rule also covers LiveArea art made from game images, shader dumps that contain Sony code, and Sony files of any kind.
9. **Keep the macOS and Linux builds and all tests green at every step.**
10. **Code style:**
    - match the surrounding code;
    - sparse comments in plain English that explain why;
    - named constants;
    - docs updated in the same PR.
11. **CI portability rules** still apply to shared code:
    - Apple clang 15 / libc++ 17 has no float `from_chars`, no `ranges::to`, no `views::enumerate`/`zip` and no `std::print`;
    - GCC 13 can't take `const T& = {}` aggregate default arguments.
12. **Pre-fetched dependencies.** CMake dependencies are already downloaded to `/Users/greg/Documents/MagicBall3/mb3-deps` (cgltf, jolt, nlohmann_json, stb). Pass them with `FETCHCONTENT_SOURCE_DIR_<NAME>`; the names are `CGLTF`, `JOLTPHYSICS`, `NLOHMANN_JSON` and `STB`. Cross builds reuse them too. Never modify `mb3-deps`, because desktop builds share it. Locally, SDL3 3.4.16 comes from Homebrew for desktop builds only.

### Disk (the hardest constraint)

About 2.8 GB was free on 2026-10-22.

1. Run `df -h /System/Volumes/Data` before every install, build or emulator step.
2. A host build directory takes about 200 MB (the current `build/` is 204 MB; worktree builds have reached 150-650 MB). Use one build directory per worktree.
3. Run at most two worktrees with build directories at once. Reviewers count toward that limit, so either:
   - run reviewers one after another; or
   - let the robustness reviewer build in the implementer's worktree after the implementer finishes, while the fidelity reviewer reads the diff without building.
4. Delete a worktree as soon as its branch merges. Review agents delete any build directory they create.
5. Scratch files go in the scratchpad directory named in your system prompt, under a subfolder `vita/<label>/`. Delete them afterwards.
6. Use no Docker locally. The vitasdk image is 0.76-1.6 GB, and a full disk has already corrupted Docker Desktop once.
7. Never delete the user's own caches or downloads, and never reset Docker data, without asking.

### Shell

Every Bash call starts a fresh shell, and subagents' working directories reset between calls. Always use absolute paths. Start every command that needs the SDK with this line:

```
export VITASDK=/Users/greg/Documents/MagicBall3/vitasdk PATH=/Users/greg/Documents/MagicBall3/vitasdk/bin:$PATH;
```

Put the same line in every agent prompt that touches the SDK.

### Consent

Ask before you download or install anything: the toolchain, vdpm packages, the vitaGL source, the emulator, {fmt}, or any fresh dependency clone. Also ask before anything that talks to the user's Vita over the network. Never download Sony firmware or `libshacccg.suprx` yourself. The user does that.

## 3. First actions

### 3.1 Questions for the user

Ask all of these in one message. Install nothing until the user has answered.

1. Do you have a PS Vita or a PSTV? Which model, which firmware (3.60, 3.65, 3.74 …), and which hack (HENkaku, Enso or h-encore)? Are taiHEN and VitaShell installed, and how much memory-card space is free?
2. Is `libshacccg.suprx` already extracted to `ur0:data/`? (vitaGL also looks in `ur0:data/external/`.) The recommended graphics path compiles shaders on the device with it. If it isn't there, are you willing to extract it? Guide: https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx
3. How should builds and logs move between the Mac and the Vita?
   - **Option A:** you install vitacompanion (FTP on port 1337, commands on port 1338), give me the Vita's IP address, and allow me to push builds and pull log files over your LAN.
   - **Option B:** you copy files by hand using VitaShell's FTP or USB mode, into a folder I name, such as `/Users/greg/Documents/MagicBall3/vita-reports/`.
4. Do you also want testing in the Vita3K emulator on this Mac? It is optional, because real hardware is the reference. It needs about 60 MB for the app, plus Sony firmware that you download and install through Vita3K yourself (roughly 0.3-0.5 GB, verify).
5. Disk and location. The plan needs about 1.0 GB without Vita3K and about 1.5 GB with it (§3.4).
   - Without Vita3K, this fits in today's 2.8 GB while keeping at least 1 GB free.
   - With Vita3K, you would need to free about 0.3 GB.

   I propose installing VitaSDK at `/Users/greg/Documents/MagicBall3/vitasdk`, next to `mb3-deps` and outside the repo. Is that location OK?
6. Preliminary preference only; the real decision comes after the device physics benchmark (M3b). When things trade off, would you rather have:
   - a locked 30 fps with slightly softer physics, or the original solver settings with a lower frame rate on heavy levels?
   - Is 30 Hz physics acceptable if it turns out to be the only way to reach 30 fps? It changes how the game feels and needs re-tuning.
7. Asset delivery. Should your personal VPK bundle the game data inside it (one install, about 55 MB)? Or should the data be copied once to `ux0:data/MuggleBall3/assets`, with code-only VPKs (faster to update)? The plan supports both: code-only VPKs for development, plus an opt-in personal bundle.
8. Title IDs and display name. The format is four letters followed by five digits. I suggest:
   - game `MUGB00003`, named "Muggle Ball 3";
   - physics bench `MUGB00004`;
   - render tests `MUGB00005`.
9. May the app set the ARM clock to 444 MHz (and GPU and bus to 222 MHz) itself? It costs battery and may need Wi-Fi turned off (verify).

### 3.2 While waiting for answers

- Do §0 step 2.
- Write the M2 contract and start M2 (Agent A) straight away. M2 needs no install and no device.
- Optionally, start M6a (`tools/vita_pack.py`). It is Python only and needs no build directory.

### 3.3 If the answers change the scope

- **No Vita:** finish M2, then stop and re-scope with the user. For example: test only in Vita3K, with no performance target.
- **PSTV only:** drop the touch criterion on hardware. Test touch only in Vita3K, and only if the user opted in.

### 3.4 Disk budget

| Item | Size | Notes |
|---|---|---|
| VitaSDK core + vdpm packages (sdl3, vitaShaRK, libmathneon, taihen, SceShaccCgExt) | ~0.42 GB | Delete the bootstrap tarballs after install |
| vitaGL built from source at a pinned commit | ~0.05 GB (verify) | Installed into `$VITASDK` |
| One Vita cross build directory | ~0.15-0.4 GB (verify; Jolt is most of it) | One at a time; reuses `mb3-deps` |
| Vita3K (optional) | ~0.06 GB app + 0.3-0.5 GB firmware (verify) | Only if the user says yes |
| **Total** | **~1.0 GB without Vita3K, ~1.5 GB with it** | Each host worktree build adds ~0.2 GB |

## 4. Context: what you are porting

### 4.1 Repository and current state

- **Repo:** `GregoryIvo/Muggle-Ball-3` is private; main is at `4e675b3`. The full game is playable on macOS. All eight roadmap steps in memory are done, except Developer ID signing (step 8, which needs the user's Apple credentials) and reading the exact rules from the original exe (blocked on disk space). Neither blocks the Vita port.
- **Language and build:** C++20 (`CMAKE_CXX_EXTENSIONS OFF`), CMake 3.24 or later.
- **`engine/`** holds:
  - the SDL3 GPU renderer on Metal. Its GLSL 450 shaders are compiled offline by `tools/build_shaders.py` (glslangValidator + spirv-cross) into committed headers in `engine/shaders/generated/`;
  - Jolt 5.6.0;
  - cgltf, stb and nlohmann_json;
  - SDL3 window, input and audio.
- **`game/`** holds the rules, physics scene, draw-list builders, particles, menus, HUD and saves. It is platform-neutral apart from `std::filesystem` and `SDLK_*` keycodes.
- **`app/`** holds:
  - `main.cpp` (767 lines), mostly a developer harness. The game itself is `runGame` (lines 420-449);
  - `FrontEnd.cpp`, `PlaySession.cpp`, the audio glue and `PlayerInput.cpp`.
- **`tools/`** holds the Python converter (`convert_assets.py`, `textures.py`, `x2glb.py`, …). These tools use only the Python standard library; `textures.py` writes PNGs with `zlib` and `struct`. CI installs no Pillow.
- **`tests/`** has 43 ctest targets. About 24 are CPU-only (they need no GPU and no data). `tests/render_test.cpp` (1,169 lines, 19 tests, headless `Renderer()` + `renderToImage`) is the renderer conformance suite.
- **Data:** the converted data lives in `Muggle-Ball-3/assets` (gitignored; 77 MB, of which 51 MB is read at runtime). `MB3_ASSETS` points tests at it.

### 4.2 Platform-specific code (file:line)

| Area | Where | What the port must do |
|---|---|---|
| Renderer (SDL GPU) | `engine/src/{Renderer.cpp 400, RenderFrame.cpp 605, PipelineCache.cpp 314, PipelineCache.h 82, RendererImpl.h 159}` = 1,560 lines. Nothing else touches SDL GPU except a stray `struct SDL_GPUDevice;` at `Renderer.h:11`. | SDL GPU has **no Vita backend** (only Metal, D3D12 and Vulkan), so write a second `Renderer::Impl` (pimpl, chosen at compile time). About 240-300 of the 1,560 lines are backend-neutral and can be factored out: `collectDraws` (RenderFrame.cpp:136-183), sprite/effect vertex building (:259-349), PipelineKey/pack/forMaterial (PipelineCache.cpp:104-176). |
| Shaders | `engine/shaders/{mesh,line,sprite,effect}.{vert,frag}` (~155 lines, GLSL 450, std140 UBOs), `tools/build_shaders.py` | Rewrite as GLSL ES 1.00 for vitaGL (§5.3). |
| Window and input | `engine/src/Window.cpp` (pollEvents :81-193; the only `__APPLE__` ifdefs are :31 and `Renderer.cpp:45`), `Window.h:21-43` `InputEvents` | Fill `InputEvents` from touch and buttons (§5.8). Gamepad events are initialised but never read today. |
| Game input | `app/PlayerInput.cpp:9-24` (launch on every left-button press edge, or Space); `game/src/Gameplay.cpp:218` `batTargetX` (ray from the cursor to the board plane); `app/FrontEnd.cpp:247-271` (keys), `:100-104` (text input starts only when `wantsText` changes); `game/src/Menu.cpp:376-392` `pressKey` (Enter only submits a name; buttons and sliders respond only to the pointer, and sliders need a held button); `game/src/GameFlow.cpp:46-60` (while paused: any key except Escape, or a mouse press, resumes; Escape quits to the main menu) | The bat follows the pointer. Launch is left click or Space. Escape means back, or pause. Name entry needs text. |
| Options | `app/FrontEnd.cpp:21-31` (video modes), `game/src/Menu.cpp:150-185` | Hide the Fullscreen box and the window sizes on Vita. Today `setVideoModes({})` hides only the size list. |
| Audio | `engine/src/Audio.cpp:33-51` (F32, 2 ch, 44.1 kHz SDL stream with a callback), `Mixer.cpp` (32 voices, linear resampling of every voice), `Vorbis.cpp` (stb_vorbis; music streamed from in-memory Ogg; effects decoded fully on first play and cached forever, `Audio.cpp:61-71`) | Measure the cost on the A9; prewarm sounds. |
| Paths and files | `engine/src/Paths.cpp:10-30` (assetRoot: `MB3_ASSETS` → `SDL_GetBasePath()/assets` → `current_path()/assets` → `userDataDir()/assets`); `:32-45` (`SDL_GetPrefPath("", "MuggleBall3")`, empty org on purpose); `engine/src/Files.cpp` (`writeFileAtomic`: write `.tmp`, then `fs::rename`) | The base path is `app0:/` (read-only). Check what the pref path looks like with an empty org (verify). Check whether `rename` overwrites an existing file on Vita (verify). `current_path()` may fail, so use the `error_code` overloads. |
| Directory scans at startup | `game/src/TextureIndex.cpp:45-47` (recursive), `UiAssets.cpp:39,109`, `LevelCatalog.cpp:12,33`, `ParticleDef.cpp:151` | Time them on the device. |
| Threads | `engine/src/Physics.cpp:98-106` makeJobSystem (`threads = -1` becomes `clamp(int(hw) - 1, 0, 3)`); `Physics.cpp:261` passes 0 body mutexes, so Jolt derives the count from `hw` | `hardware_concurrency()` very likely returns 0 on Vita. That already gives `JobSystemSingleThreaded` and 1 body mutex, which is harmless. The Vita profile sets threads and body mutexes explicitly so that workers can be tested. |
| Randomness | `std::random_device` at `game/include/mb/game/SoundLibrary.h:69` (default argument), `app/PlaySession.cpp:145`, `app/main.cpp:481` | vitasdk newlib ships `getentropy`, so it may work. Replace it (with one helper seeded from `SDL_GetPerformanceCounter`) only if the device probe shows that constructing or calling it throws. |
| stb | `engine/src/StbImpl.cpp` | On Vita, define `STBI_NO_THREAD_LOCALS`, because TLS there is emulated. |
| Logging | `engine/include/mb/engine/Log.h` (wraps `std::format_string`/`std::format`; 163 call sites) | On Vita, also write to `ux0:data/MuggleBall3/log.txt` and to `sceClibPrintf`. |
| Build | `CMakeLists.txt:11-26`: runs before `project()` on purpose, so that `CMAKE_OSX_ARCHITECTURES` and the deployment target are set before compiler detection. It is keyed on `CMAKE_HOST_APPLE` and does nothing unless `MB3_UNIVERSAL` or `MB3_FETCH_SDL3` is set. Also `cmake/Dependencies.cmake` (Jolt options :42-55), `app/CMakeLists.txt` (`APPLE` bundle branch :4-41), `.github/workflows/build.yml` | Guard the pre-`project()` block with `if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE)`. Do all target detection (`VITA` → `MB3_PLATFORM_VITA`) after `project()`; the toolchain file is only read inside `project()`. Add a Vita branch, and set `MB3_BUILD_TESTS=OFF` for the cross build. |
| Entry point | `app/main.cpp:70` `SDL_MAIN_HANDLED`; `:634` `main()` → `runGame` with no arguments. `runGame` depends on helpers in main.cpp's anonymous namespace (Options, Assets, openSave, windowSize, saveWindowState, FrameStats). | M5a moves `runGame` and those helpers into `app/Game.cpp`. Vita gets its own thin entry point that calls `runGame`. |

### 4.3 Renderer contract a new backend must reproduce

- **Vertex formats:**
  - mesh `Vertex`: 32 B (pos3, normal3, uv2; D3D uv origin at the top left), 32-bit indices;
  - line: 28 B;
  - sprite: 32 B (pixel pos2, uv2, colour4; 6 vertices per sprite, no index buffer);
  - `EffectVertex`: 36 B (pos3, uv2, colour4), three per triangle, in world space.
- **Matrices:** `mb::Mat4` is row-major with row vectors (D3D style) and is uploaded unchanged; GLSL reads it in column-vector form. The projection is D3D-style and left-handed, with **clip z in 0..1** (`Math.h:285-297`; near 1, far 500). A GL backend must remap z with z' = 2z − w. Fold this into viewProj on the CPU, in one place.
- **Faces and depth:** front faces are **clockwise**. The depth compare is LESS_OR_EQUAL. Depth is written only when depthTest and depthWrite are both on. Depth clears to 1.0. There is no stencil.
- **Colour writes:** RGB only. Framebuffer alpha stays at the clear alpha (1.0), which keeps the DestAlpha blend factors well defined. Clear with the full colour mask, then mask alpha off.
- **Blending:**
  - the blend op is always ADD;
  - AlphaBlend = SrcAlpha / InvSrcAlpha;
  - Additive = SrcAlpha / One;
  - Custom = any of the 10 D3DBLEND factors, numbered 1-10: Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha, InvDestAlpha, DestColor, InvDestColor;
  - in the shipped data only Water uses Custom (SrcAlpha, SrcColor). Lazer uses AlphaBlend. The other 235 of the 237 materials are Opaque.
- **Mesh shader:** per-pixel D3D8 fixed function with one directional light:
  - lit: `rgb = tex.rgb * clamp(emissive + ambient*lightAmbient + diffuse*lightDiffuse*max(dot(n,-L),0), 0, 1)`;
  - unlit: `rgb = tex.rgb * diffuse.rgb`;
  - alpha = `tex.a * diffuse.a`;
  - discard the pixel if the alpha-test flag is set (exactly when BlendMode::Opaque) and alpha < 0.5;
  - no fog, no specular, no vertex colours, no skinning;
  - the normal matrix is a separate uniform (see M4a).
- **Other shaders:**
  - effect: `colourOnly ? (vtx.rgb, tex.a*vtx.a) : tex*vtx`;
  - sprite: pixel to clip as (x·2/w − 1, 1 − y·2/h), then `tex * vertexColour`;
  - line: flat colour.
- **Sampling:**
  - RGBA8 UNORM, never sRGB;
  - one sampler: linear min, linear mag, linear mip, **REPEAT** on every axis; drop anisotropy;
  - `createTexture(mipmaps=true)` builds a full mip chain for any size, including NPOT. 42 PNGs and 5 JPGs are NPOT, mostly UI;
  - UI sprites rely on REPEAT with NPOT textures: tiled forms, and the scrolling 1024x380 `Textures/Forms/Sky.jpg` (`game/src/UiDraw.cpp:57-75`);
  - UI and font textures use `mipmaps=false` and must not blur.
- **Frame order:**
  1. clear;
  2. opaque meshes, stable-sorted by (layer, pipeline, texture, mesh);
  3. blended meshes, far to near by squared distance;
  4. effect batches, in the order given;
  5. debug lines (viewer only);
  6. sprites, in submission order, with no depth; consecutive sprites with the same texture are merged.
- **Dynamic data:** streamed buffers for lines, sprites and effects. `updateMesh` (cloth sails, every frame) rewrites a whole vertex buffer.
- **`renderToImage`** (offscreen RGBA8 readback, rows top to bottom) is used only by tests and screenshots. Implement it anyway: it is how you test on the device. vitaGL flips FBOs and compensates the culling internally, so a plain row flip after `glReadPixels` is enough. Don't change the winding.

### 4.4 Measured numbers

All measurements come from an Apple M3 Pro running a Release build.

**Physics:**
- Settings: fixed step 1/60 s, 100 velocity iterations, 1 position iteration, 1 collision step, `maxStepsPerCall` 8.
- Mean step time is 0.072-0.076 ms single-threaded, over 83 levels. Three Jolt workers give **no** gain in wall time and use 2.4x the CPU.
- Cost is linear in awake bodies: `step_ms ≈ 0.025 + awake × (0.0012 + 0.000083 × velocitySteps)`. That is 9.5 µs per awake body at 100 iterations, 3.6 µs at 30 and 1.9 µs at 10.
- The profile is 86 % velocity-constraint solving.
- Heaviest levels by average step (M3 ms): knight-miss-1457 0.70 (p99 2.6), village-tennis 0.47, piratos-dom-na-vode 0.45, knight-eggs 0.41, village-dinner 0.21, piratos-dva-doma 0.20, village-logovo-vedmi 0.20, knight-shashki 0.18, knight-v-yablochko 0.12, piratos-gorillas 0.12, knight-river 0.12. Peak awake bodies in these levels: 100-222.
- `docs/PHYSICS.md`, "The solver": on the four windmill levels (village-dinner, village-na-melnice, village-rinok, Village-Dvorik), 240 s with no ball:
  - 40/1/1 and 60/1/1 (velocity/position/collision) failed: bodies moved 66,053 and 24 units;
  - the only stable alternatives cost more than 100/1/1 (0.12 ms): 30/1/2 at 0.14 ms and 10/1/4 at 0.21 ms;
  - Jolt's default 10/2 also makes ordinary brick stacks sink.
- Sails are cheap: 22 in the game, at most 142 particles per level, 0.008-0.026 ms per step.
- The Jolt world takes 33.5 MB per level, 16 MB of it the TempAllocator. maxBodies is 32768, against at most 373 bodies actually used.
- Autopilot finishes only 71 of the 83 levels within 900 s, and some runs end in game over.

**Vita estimates (not measurements; M3b replaces them):**
- The Vita is estimated at **45-110x slower than the M3, 70x central**, at 444 MHz. At the stock 333 MHz it is 1.33x worse again. Jolt has no NEON path on ARM32, so it runs scalar code.
- Against the 8 ms per-step budget at 70x, derived from the M3 per-level table:
  - **100/1/1:** 11 levels average over budget (the 11 listed above). The worst, knight-miss-1457, is about 49-52 ms. The p99 step is over budget on most levels.
  - **30/1/1** (unstable on the windmill levels): 5 levels average over budget; worst about 21 ms.
  - **10/1/1** (same caveat): 4 levels average over budget; worst about 10.4 ms average and 38 ms at p99.
- Per-body view: at 70x the budget allows about 9 awake bodies at 100 iterations, 24 at 30 and 44 at 10. Heavy levels reach 100-222, so limiting awake bodies matters more than the iteration count.
- **30 Hz physics** (one step per frame, at most 16 ms): at 30 iterations, the worst level averages about 17 ms.
- Fewer iterations change the trajectories, so per-level numbers are noisy across settings.

**Rendering:**
- Draw items per frame: mean 153, peak 410. Blended items: at most 34. Effect batches: at most 5-6.
- Triangles per frame: mean 15k, peak 56k (knight-garden).
- Textures bound per frame: 19 mean, 39 peak. Texture memory per level, with mips: 5.2 MB mean, 8.6 MB max.
- Opaque surfaces cover 100 % of the screen with 2.8x overdraw, and all of them are alpha-tested.
- Particles: 146 at the peak in natural play, 233 in a stress run.
- vitaGL costs about 2.2-4 µs of CPU per draw on the device.

**Memory and loading:**
- Peak RSS on macOS is 106-112 MB, Metal included. A level adds 8-15 MB.
- The font atlases (8.4 MB in RGBA8, but effectively one channel) load eagerly.
- `ModelCache` never evicts anything and keeps a CPU copy of every model.
- Decoded sound effects can reach 29 MB and are cached forever.
- Music Ogg bytes (4.6 MB) stay in RAM.
- Level JSON parses in 5-10 ms on the Mac. Expect about 0.3-1 s on the Vita.

**Assets:**
- Meshes total 154k vertices. The largest single mesh has 5,171 vertices, so 16-bit indices are safe.
- 3D textures are all JPEG, 256x256 or smaller.
- The `.x`, `.mb3`, `.dds`, `.xml` and `.icns` files (26 MB) are never read at runtime.

### 4.5 Vita ecosystem facts

**VitaSDK**
- The stable channel is "2026.08", reported as GCC 15.2, binutils 2.46.1 and Newlib 4.1.
- Target: armv7-a, cortex-a9, NEON, hard-float, Thumb. Static libraries only, `--enable-threads=posix`, emulated TLS.
- Run `arm-vita-eabi-g++ --version` first. The plan assumes GCC 13 or newer, for `<format>`.
- A native Apple Silicon build exists. Check `file $VITASDK/bin/arm-vita-eabi-gcc`; if it reports x86_64, Rosetta is needed, so ask the user.
- Install:
  1. `git clone https://github.com/vitasdk/vdpm`;
  2. `./bootstrap-vitasdk.sh` with `VITASDK=/Users/greg/Documents/MagicBall3/vitasdk` and `--install-dir`;
  3. `vdpm install sdl3 vitaShaRK libmathneon SceShaccCgExt taihen`.
- **Never install vdpm's `vitaGL`** (§5.1).
- The 2026 restructure made the old guides (install-all.sh, the GCC 10 tarballs) obsolete.
- Toolchain quirks: `__vita__` is predefined, `char` is signed, `int32_t` is `int`.

**Newlib and pthreads**
- `posix_memalign` is probably missing: it is declared in `stdlib.h` but not implemented. `memalign` and `aligned_alloc` exist. Jolt calls `posix_memalign` (`Jolt/Core/Memory.cpp:51`). Check with `arm-vita-eabi-nm $VITASDK/arm-vita-eabi/lib/libc.a | grep posix_memalign`.
- `getentropy` exists.
- `sysconf` has no `_SC_NPROCESSORS_ONLN`.
- Threads created by `std::thread` (which includes Jolt's workers) get `_pthread_stack_default_user` if the app defines it, and otherwise a tiny minimum stack (`PTHREAD_STACK_MIN`, 4 KiB). Define it before running anything with worker threads.

**CMake**
- Configure with `-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`. It sets `VITA`, `CMAKE_SYSTEM_NAME Generic` and `CMAKE_SYSTEM_PROCESSOR armv7-a`. Verify the processor value, because Jolt's `Build/CMakeLists.txt:266` uses it unquoted.
- Then call `include("$ENV{VITASDK}/share/vita.cmake")`, `vita_create_self(eboot.bin app UNSAFE)` and `vita_create_vpk(app.vpk TITLEID eboot.bin VERSION 01.00 NAME "Muggle Ball 3" FILE sce_sys sce_sys …)`.
- Recent mksfoex may default ATTRIBUTE2 to MEM109 (verify). Set `VITA_MKSFOEX_FLAGS` explicitly.

**SDL3 in the cross build**
- Require the SDK's SDL3: `find_package(SDL3 CONFIG REQUIRED)` with `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY`, or `SDL3_DIR=$VITASDK/arm-vita-eabi/lib/cmake/SDL3` (verify the path).
- Never use the FetchContent fallback, which would clone release-3.2.10, and never pick up Homebrew's `/opt/homebrew` SDL3. If SDL3 is not found, fail with a clear message.

**LiveArea images**
- Sizes: `sce_sys/icon0.png` 128x128, `pic0.png` 960x544, `livearea/contents/bg.png` 840x500, `startup.png` 280x158, plus `template.xml`. Copy the layout of https://github.com/vitasdk/samples/tree/master/pretty_livearea
- All four PNGs must be **8-bit indexed** (PNG colour type 3, a 256-colour palette), as in that sample. RGBA PNGs can fail to install or show a blank bubble.

**SDL3 on Vita** (supported since 3.2.0; the vdpm package is 3.4.16)
- One 960x544 window.
- Front and back touch arrive as separate devices. Hints: `SDL_HINT_VITA_ENABLE_FRONT_TOUCH`, `SDL_HINT_VITA_ENABLE_BACK_TOUCH`, `SDL_HINT_TOUCH_MOUSE_EVENTS` (SDL's Vita README recommends 0).
- Gamepad buttons are positional, and SDL does not read the system's enter-button (Circle/Cross) setting.
- **Audio:** S16 only. The main port needs 48 kHz; any lower rate goes to the BGM port, so 44.1 kHz output is not resampled by SDL.
- **IME:** `SDL_StartTextInput` opens sceImeDialog. It is one-shot: it opens empty, sends the whole string as one TEXT_INPUT event, then sends an automatic Return if the user confirmed, then closes. A cancel sends nothing. SDL never calls `sceCommonDialogUpdate`, so with vitaGL the app must present with `vglSwapBuffers(GL_TRUE)` while the dialog is up, or the dialog never appears or the app hangs.
- `SDL_ShowMessageBox` brings up its own GXM context. Never call it on Vita.
- `SDL_GetBasePath()` is `app0:/`. `SDL_GetPrefPath` is `ux0:/data/<org>/<app>/`.
- SDL sends no background/foreground events on Vita (verify whether `SDL_EVENT_WILL_ENTER_BACKGROUND` ever arrives).
- While the screensaver is disabled (SDL's default), SDL's event pump calls `sceKernelPowerTick`, so the console only auto-sleeps if events stop being pumped.
- SDL's Vita video init does not touch GXM. SDL3 for events and audio plus vitaGL owning the display should therefore work together, but no SDL3+vitaGL port is known (verify in M1a).
- vdpm's sdl3 uses default CMake options, so `SDL_gpu.c` is probably compiled in with no drivers (verify with nm in M1a).

**vitaGL** (https://github.com/Rinnegatamante/vitaGL, LGPL-3.0, actively developed)
- It translates GLSL ES 1.00 (`attribute`/`varying`, `gl_FragColor`, `texture2D`, no UBOs) to Cg and compiles it **at runtime** with Sony's `libshacccg.suprx`. It looks at the standard `ur0:data/` path, then `ur0:data/external/libshacccg.suprx`. vitaGL also accepts Cg source directly. Its fixed-function path also compiles at runtime.
- Runtime compile costs 330-365 ms per program, or 165-182 ms with `HAVE_SHADER_CACHE=1`.
- Precompiled GXP can be dumped with `vglGetShaderBinary` and loaded with `glShaderBinary` in 0.05 ms. Whether that works without libshacccg present is untested on hardware.
- Build flags (confirm names at the pinned commit): `NO_DEBUG=1`, `HAVE_SHADER_CACHE=1`, `NO_SPLASHSCREEN=1`. GLSL input and runtime compile are gated behind Makefile flags (believed to be `HAVE_GLSL_SUPPORT` and `HAVE_SHARK`; verify). Keep `HAVE_UNFLIPPED_FBOS` off. Use `HAVE_VITA3K_SUPPORT=1` only in a separate emulator build. Try `DRAW_SPEEDHACK` and `CIRCULAR_POOL_SPEEDHACK` only after measuring.
- **Init:** `vglInitExtended(int pool_size, int w, int h, int ram_threshold, SceGxmMultisampleMode)` takes all of CDRAM and PHYCONT, plus the free RAM minus the threshold. Plain `vglInit` uses a 16 MiB threshold and 4x MSAA, so use `vglInitExtended(0, 960, 544, threshold, SCE_GXM_MULTISAMPLE_NONE)`.
- **Swap interval:** `eglSwapInterval(…, 2)` sets vitaGL's vsync interval, and its display callback then calls `sceDisplayWaitVblankStartMulti(2)` (source/egl.c, source/gxm.c; verify at the pinned commit). `vglWaitVblankStart(GLboolean)` only switches between intervals 1 and 0.
- **Swap call:** `vglSwapBuffers(GLboolean has_commondialog)`.
- **Textures:**
  - uncompressed RGBA uploads become LINEAR GXM textures; only compressed formats are swizzled;
  - the mip generator truncates odd dimensions and stops once the smaller side reaches 1, so NPOT mip chains differ from the desktop's;
  - supported compressed formats: DXT1/3/5, PVRTC, ETC1.

**Jolt 5.6.0**
- `Jolt/Core/Core.h` stops with `#error Unknown platform` on Vita (around line 459).
- **Preferred fix, no patch:** Core.h checks `JPH_PLATFORM_BLUE` first (line 74) and then includes `<Jolt/Core/PlatformBlue.h>`. That file is not in the Jolt tree; Jolt expects you to provide it.
  - For Vita, add `target_compile_definitions(Jolt PUBLIC JPH_PLATFORM_BLUE)` and `target_include_directories(Jolt BEFORE PUBLIC ${CMAKE_SOURCE_DIR}/platform/vita/jolt)`.
  - In that directory, commit `Jolt/Core/PlatformBlue.h`. It defines:
    - `JPH_BREAKPOINT` as `__builtin_trap()`;
    - `JPH_PLATFORM_BLUE_GET_TICKS` from `sceKernelGetProcessTimeWide`;
    - the `JPH_PLATFORM_BLUE_MUTEX*`, `_RWLOCK*` and `_SEMAPHORE*` macros, mapped to `std::mutex`, `std::shared_mutex` and mutex plus condition_variable (or to SceKernelLwMutex/Sema). The macros used are listed in Core.h:442-447, Mutex.h, Semaphore.h/.cpp and TickCounter.h.
  - The settings must be PUBLIC, so that the engine's own `Physics*.cpp` files (which include Jolt headers) see the same macro.
  - This route keeps using `mb3-deps/jolt` unchanged.
- **Fallback:** a patch applied to a copy of Jolt inside the build tree, made idempotent with `git apply --reverse --check … || git apply …`. Never patch `mb3-deps`. Never rely on `PATCH_COMMAND` while `FETCHCONTENT_SOURCE_DIR_JOLTPHYSICS` is set: FetchContent then skips the patch step.
- Do **not** define `JPH_PLATFORM_LINUX`. It pulls in `<sys/prctl.h>` and POSIX semaphores, which the Vita lacks.
- **Never compile Jolt or `engine/src/Physics*.cpp` with `-ffast-math`, `-funsafe-math-optimizations` or `-ffinite-math-only`.** Jolt's build warns against it and relies on IEEE behaviour. The reference ports add `-ffast-math` globally, so don't copy their global flags. If the renderer or mixer wants fast-math, set it on those targets only.
- Pass thread and body-mutex counts explicitly. Don't use `CROSS_COMPILE_ARM`; it is clang-only.

**Vita3K**
- A native arm64 macOS build exists. It needs Sony firmware, which the user installs.
- `Vita3K --frontend sdl app.vpk` installs and boots the app and returns its exit code. It still needs a window and Vulkan (MoltenVK), so it can't run in CI.
- Known vitaGL gaps on the emulator: avoid `glScissor` and hardware ETC1.
- Vita3K accepts plain REPEAT on linear textures (it rejects only mirror and border modes). Confirm on the device anyway.

**Hardware**
- CPU: 4x Cortex-A9. Apps get 3 cores (`SCE_KERNEL_CPU_MASK_USER_0/1/2`).
- Clock: 333 MHz by default, 444 MHz via `scePowerSetArmClockFrequency(444)`, plus bus and GPU at 222 MHz and xbar at 166 MHz (verify).
- GPU: SGX543MP4+. Screen: 960x544.
- Memory budgets: MAIN 256 MiB (365 MiB with ATTRIBUTE2=12), CDRAM 112 MiB, PHYCONT 26 MiB.
- The newlib heap is one fixed block: 128 MiB by default, set with `_newlib_heap_size_user`. Also set `sceUserMainThreadStackSize` and `_pthread_stack_default_user` (for example `512 * 1024`) explicitly.
- Thread stacks come from MAIN, outside the newlib heap. Keep vitaGL's `ram_threshold` large enough for them, plus SDL's audio thread and the IME.

**Reference ports**
- https://github.com/Rinnegatamante/ldhewm3-rearmed: flags, memory, NEON, clocks.
- https://github.com/Rinnegatamante/StaticJK: CMake, UNSAFE, ATTRIBUTE2=12.
- https://github.com/sharkwouter/oceanpop: SDL3, one CMake for every platform, a Vita CI job.
- https://github.com/vitasdk/samples: `.github/workflows/build.yml` uses `container: vitasdk/vitasdk:<tag>`.

## 5. Key decisions (recommendation first, then fallback)

### 5.1 Graphics API

**Recommended:** a new `Renderer::Impl` built on **vitaGL**, using only the programmable GLSL ES 1.00 path. Never use vitaGL's fixed-function path.
- Select the backend with the CMake option `MB3_RENDERER=SDLGPU|VITAGL` (plus `GL` and `NONE` if they are needed, see §5.2 and M2). The default is SDLGPU; Vita forces VITAGL.
- Build vitaGL from source at a pinned commit and `make install` it into `$VITASDK`. Before the M1a hello, read the Makefile and README at that commit. Write the full flag line into `docs/VITA.md` and confirm that GLSL input and runtime compile are on. Use the identical flag line in CI.

**Fallback 1:** PVR_PSP2 GLES2 through SDL's `VIDEO_VITA_PVR`. It is less proven, needs stub headers and driver modules, and has no vdpm package.

**Fallback 2:** raw GXM. It is not viable today without Sony's shader compiler.

### 5.2 Desktop GL variant (for testing)

Consider writing the backend as "GL" rather than "vitaGL only": the same code compiled against desktop OpenGL 2.1 through an SDL GL context, behind a non-default `MB3_RENDERER=GL`. Then `tests/render_test.cpp` can check it on the Mac before any device run.
- Put the platform differences in a small `GlPlatform` file: vglInit/vglSwapBuffers on one side, SDL_GL on the other.
- **Before writing any backend code**, check with a 20-line SDL probe that this Mac (Darwin 27) still offers an OpenGL 2.1 context. If it doesn't, drop this option and test on the device only.
- The shader bodies are shared, but each target gets a generated preamble:
  - `#version 100` plus precision qualifiers for vitaGL;
  - `#version 120` plus empty `#define lowp`/`mediump`/`highp` for desktop GL 2.1, which does not accept GLSL ES.
- If you keep the variant, add a compile-only `-DMB3_RENDERER=GL` configure and build step to the ubuntu CI job (`apt install libgl-dev`). Optionally run `render_test` there on Mesa llvmpipe (EGL or xvfb).
- Desktop GL supports NPOT textures fully, so it will not reveal Vita texture limits (§5.7).

### 5.3 Shaders

- Hand-write GLSL ES 1.00 for the four programs, plus a **discard-free mesh variant**. The backend picks that variant when a texture has no alpha below 255 (computed once in `createTexture`) and `diffuse.a == 1`. The pixels are identical, and the PowerVR GPU can keep its hidden-surface removal. The JPEG world textures never needed the alpha test.
- Embed the shader text, with its per-target preamble, through `tools/build_shaders.py` into generated headers, so the C++ build needs no tools. Add a glslangValidator ES check where the tool exists.
- Rely on runtime compile with `HAVE_SHADER_CACHE`, which needs libshacccg on the device.
- If vitaGL's GLSL translator rejects a shader, write that program in Cg, which vitaGL accepts directly.
- **Upgrade path:** GXP dumps loaded with `glShaderBinary`. Dumps are compiler output, so keep them out of the repo unless you have checked that they carry no Sony code.

### 5.4 C++ library gaps

Find out what is actually missing with two probes. Fix only what fails.

**(a) Compile-only probe**, on the host right after the SDK install:
- `<format>` including `{:.2f}`;
- `<ranges>` algorithms, `<span>`, `std::bit_cast`;
- the `<filesystem>` headers and link;
- `thread_local`, exceptions, RTTI;
- presence of `posix_memalign` and `aligned_alloc`, checked with nm on `libc.a`.

**(b) Runtime probe**, in the same device batch as the M1a hello:
- filesystem operations on `app0:/` and `ux0:` paths: `directory_iterator`, `recursive_directory_iterator`, `create_directories`, `rename` over an existing file, `remove`, `exists`, `current_path`;
- constructing and calling `std::random_device`;
- `std::thread` default stack size and core affinity, and the value of `hardware_concurrency()`;
- `steady_clock` resolution.

**Fallbacks:**
- **`<format>`:** route every use through `mb::format` / `mb::format_string` in one header. That covers Log.h's `std::format_string` wrappers (163 call sites) and the 46 direct `std::format(` calls in engine/ and game/ (Model.cpp, Physics*.cpp, PipelineCache.cpp, Vorbis.cpp, BitmapFont.cpp, UiAssets.cpp, StringTable.cpp, MaterialLibrary.cpp, Gameplay.cpp, Campaign.cpp, UiForm.cpp, SoundLibrary.cpp, Menu.cpp). Only these specs are used: `{}`, `{:.0f}`, `{:.1f}`, `{:.2f}`, `{:<14}`, `{:8.3f}`. Ask before downloading {fmt}.
- **Filesystem operations:** a thin `mb::fs` over `SDL_EnumerateDirectory`, `SDL_GetPathInfo`, `SDL_RenamePath` and `SDL_CreateDirectory`, keeping `std::filesystem::path` as the type.
- **`posix_memalign`:** if it is missing, add a Vita-only source file containing `extern "C" int posix_memalign(void** p, size_t a, size_t n) { *p = memalign(a, n); return *p ? 0 : ENOMEM; }`.

### 5.5 Physics profile

Read `docs/PHYSICS.md`, "The solver", first.

Use a named **physics profile** selected through `PhysicsSettings` / `SessionOptions::physicsSettings` (`Gameplay.h:199`), with a per-level override table in code. Host smoke tests take `--physics-profile <name>`, so stability is checked on the Mac with real data. The starting Vita profile:

- **Fixed step:** keep 1/60 s. The rules and hit forces (`PhysicsScene.cpp:330`) are tuned to it.
- **Threads:** single-threaded (`threads = 0`). Use one or two workers pinned to other cores only if the device benchmark shows a gain, and only after `_pthread_stack_default_user` is set. Set the body-mutex count explicitly.
- **Iterations:**
  - Fewer velocity iterations with 1 collision step are only a candidate for **non-windmill** levels. Any such setting must pass the 240 s no-ball stability check from `docs/PHYSICS.md` on all 83 levels.
  - For the four windmill levels, the only stable settings that were measured cost more than 100/1/1. Their override is therefore either 100/1/1 or kinematic machine bodies, as `docs/PHYSICS.md` suggests. Budget them at 100 iterations.
  - Also benchmark 30/1/2.
- **Awake bodies:** cap them. They dominate the cost tails. Sleep earlier, and deactivate debris that has fallen off the board.
- **`maxStepsPerCall`:** 4 in the Vita profile, which is two 30 fps frames' worth. A frame that misses a vblank then still runs all its steps. Count every dropped step and log the count. Lower the limit only if the measured spiral risk demands it, and only with the user's agreement, because that changes the §1 criterion.
- **Jolt world size**, in the Vita profile only: about 1024 bodies, 4096 pairs, 2048 contacts and a 2-4 MB TempAllocator. Size these from high-water marks measured on the host across all 83 levels.

**Fallback**, only if the user agrees: 30 Hz physics. `PlaySession` already draws ahead of the last step, but the rules would need re-validating.

### 5.6 Asset delivery

- **Development:** a code-only VPK. The data is copied once to `ux0:data/MuggleBall3/assets`, which `assetRoot()` already searches.
- **Personal convenience:** an opt-in `MB3_VITA_BUNDLE_ASSETS=ON` (mirroring `MB3_BUNDLE_ASSETS` on macOS) puts the stripped data at `app0:/assets`.
- CI never sees the data.
- A new `tools/vita_pack.py` copies only the runtime files (json, glb, ogg, jpg, png), about 51 MB.

### 5.7 Textures

- Keep PNG/JPG in RGBA8 at first. Capacity is not the problem: 5-9 MB per level against 112 MB of CDRAM.
- **NPOT UI textures** (no mips) stay as they are and use REPEAT on linear textures. Confirm with `render_test` on the device.
- **NPOT textures that go through `ModelCache` with mipmaps** (for example the 192x192 and 128x74 JPEGs; list them all with a host script) are resampled to the next power of two, by `vita_pack.py` or at load on Vita. Their mip chains are then well defined. UVs are normalised, so nothing else changes.
- **Later, only when measurement calls for it:**
  - Linear RGBA textures are the slow case for the SGX texture cache. If the M7 GPU timing shows texture bandwidth matters, the first step is DXT1 for the opaque JPEG world textures (vitaGL keeps S3TC swizzled).
  - A8/LA8 font atlases (8.4 MB → 2.1 MB).
  - An index file to replace the startup directory scans.

### 5.8 Input

All of this fills `InputEvents` in `Window.cpp` and is compiled only for Vita. `game/` doesn't change, except that `FrontEnd` may need to expose the flow's mode.

- **Confirm and back.** At startup, call `sceAppUtilInit`, then `sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON)`. Confirm is Cross, or Circle when the call returns `SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE`. Back is the other button. Any on-screen hints show the right glyph.
- **Confirm = the left mouse button at the virtual cursor**, held for as long as the button is held. It launches the ball in play (via PlayerInput's press edge), clicks in menus, drags sliders and resumes from pause. While Window's text input is active, confirm sends Enter instead.
- **Back = Escape.** It pauses in play and goes back in menus. Pressed again while paused, it quits to the main menu, as on desktop.
- **Start = Escape** as well. Because pressing Start twice abandons the level, confirm this behaviour with the user in M5b, or remap Start-while-paused to resume (which needs the flow's mode).
- **Left stick and D-pad:** move the bat in play and move a drawn virtual cursor in menus. The bat target comes from the cursor through `batTargetX`.
- **Front touch in menus:** a tap is a click at the finger.
- **Front touch in play:** a drag sets the pointer position only and never produces a left-button press.
  - Launch is the confirm button, or a short tap: released within 200 ms, moved less than about 12 px, and not part of a drag.
  - Handle touch events in `Window.cpp` yourself, with `SDL_HINT_TOUCH_MOUSE_EVENTS=0`. SDL's touch-to-mouse synthesis would press the button on every touch, which would launch the ball whenever a finger lands.
  - `Window::setCursorHidden(true)` already tells Window that a level is playing (verify that this is enough).
  - Optional: a relative-drag bat mode, so the finger doesn't cover the bat.
- **Back touch:** set `SDL_HINT_VITA_ENABLE_BACK_TOUCH=0`, so palms on the rear pad cause no input.
- **No touch device (PSTV):** every screen works with buttons and the stick cursor alone.
- **Name entry:**
  - Use the IME through `SDL_StartTextInput`.
  - While `SDL_ScreenKeyboardShown(window)` is true, present with `vglSwapBuffers(GL_TRUE)`; otherwise use `GL_FALSE`.
  - Confirm, or a tap on the name field, reopens the dialog with Stop then Start. This also covers a cancelled dialog, which otherwise can't be reopened, because FrontEnd starts text input only when `wantsText` changes.
  - Expect the whole name at once, followed by an automatic Return.
  - Drop characters that the BitmapFont glyph table lacks.
  - Never call `SDL_ShowMessageBox` on Vita.
  - Fallback: an on-screen letter picker.
- **Fallback if SDL3 and vitaGL fight over the display:** a Vita `Window` built on `sceCtrl` and `sceTouch` (about 150 lines), with SDL kept for audio only.

### 5.9 Frame pacing and presentation

- Lock to 30 fps with `eglSwapInterval(…, 2)`, not with `vglWaitVblankStart`.
- If that doesn't work at the pinned commit, pace frames in the app with vsync on (or call `sceDisplayWaitVblankStartMulti(2)`), and record the method in `docs/VITA.md`.
- Measure the presented interval and the work time as §1 item 2 defines them, and log both per level.

## 6. Milestones

Each (sub-)milestone is one PR. Each lists its code, its host acceptance (needed to merge) and its device checklist (needed to call it done).

### M1a: toolchain, Vita CMake branch, hello VPK (Opus does the spikes)

**Spikes**, after the user consents. Record every result in a new `docs/VITA.md`.
1. Install the SDK and the vdpm packages listed in §4.5. Delete the tarballs. Check `file` and `--version`. Pin the channel.
2. Build vitaGL at a pinned commit with the documented flag line (§5.1), and find the swap-interval method (§5.9).
3. Run the compile-only probe (§5.4a), including the `posix_memalign` check.
4. Run `arm-vita-eabi-nm` on `libSDL3.a` and check that `SDL_CreateGPUDevice` is exported. The answer decides M3a's renderer question (§7).
5. Cross-compile Jolt 5.6.0 from `mb3-deps/jolt` through the PlatformBlue route, with no fast-math.
6. Build the hello VPK (title ID `MUGB00003`). It:
   - opens the SDL window and handles events;
   - reads the enter-button setting;
   - calls `vglInitExtended(0, 960, 544, threshold, SCE_GXM_MULTISAMPLE_NONE)`;
   - clears to `kClearColor` (0.05, 0.05, 0.1);
   - draws one triangle with the real GLSL ES mesh shader;
   - uses swap interval 2 and logs vcount deltas;
   - tests the IME: open, confirm and cancel, with the text written to the log;
   - links one `SDL_CreateGPUDevice` call, which is expected to fail at runtime;
   - runs the runtime probe (§5.4b);
   - logs to `ux0:data/MuggleBall3/log.txt` and to `sceClibPrintf`;
   - sets `_newlib_heap_size_user`, `sceUserMainThreadStackSize` and `_pthread_stack_default_user`;
   - exits on the back button.

**Code:**
- `docs/VITA_PLAN.md` and `docs/VITA.md`, with a "Device checklist" section.
- `MB3_PLATFORM_VITA`, detected from `VITA` after `project()`.
- The pre-project guard `if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE)`.
- `cmake/Vita.cmake`:
  - SDL3 from the SDK only;
  - `mb3-deps` reused for nlohmann_json, stb, cgltf and Jolt;
  - `platform/vita/jolt/Jolt/Core/PlatformBlue.h`;
  - the `posix_memalign` shim if needed;
  - no fast-math on physics;
  - explicit `VITA_MKSFOEX_FLAGS`;
  - `vita_create_self` / `vita_create_vpk`.
- A committed script that makes a placeholder LiveArea set from text and a plain colour, with no game art. It writes 8-bit indexed PNGs with the standard library only. A Python unit test asserts each file's size and colour type 3.

**Host acceptance:** the desktop jobs are unchanged and green, the Python tests pass, and the hello cross-builds locally.

**Device checklist** (or the Vita3K equivalent):
- the hello installs and shows its icon;
- the triangle draws;
- vcount deltas show a 2-vblank cadence;
- the IME opens, confirms and cancels without hanging;
- buttons respond and the app exits cleanly;
- the probe result file comes back.

### M1b: `vita` CI job

1. `container: vitasdk/vitasdk:2026.08` (verify the tag exists), pinned by digest.
2. Check for the needed packages and `vdpm install` any that are missing (sdl3, vitaShaRK, libmathneon, SceShaccCgExt, taihen).
3. Restore or build vitaGL at the pinned commit with the recorded flag line, using `actions/cache` keyed on the commit plus the flags.
4. Configure: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DMB3_BUILD_TESTS=OFF`. Verify that ninja is in the image; otherwise use the default generator.
5. Build.
6. Assert that there is no `assets/` directory in the tree.
7. Upload `MuggleBall3.vpk` (from M3b on, also the bench VPK; from M4a on, also the render-test VPK).

**Accept:** the vita job is green, the artifact contains no data, and the desktop jobs are green.

### M2: renderer seam (desktop only, no behaviour change). Starts at once (§3.2).

**Contract** (Opus writes it): the `MB3_RENDERER` option, the new file layout, and the `RenderCommon` interface. M2 owns `engine/CMakeLists.txt`'s renderer source list and every renderer file.

**Code:**
- Move the SDL GPU implementation to `engine/src/render/sdlgpu/`.
- Factor the backend-neutral parts into `engine/src/render/RenderCommon.{h,cpp}`: draw collection and sorting, sprite/effect vertex building, the render-state key and the blend mapping.
- Add the `MB3_RENDERER` option.
- Remove the stray `SDL_GPUDevice` declaration.
- If M1a found that `SDL_CreateGPUDevice` is not exported on Vita, also add `MB3_RENDERER=NONE`: a stub Impl whose methods fail cleanly.

**Accept:** all ctest targets pass, `render_test` is pixel-identical before and after (compare dumps), and `package-macos` is green.

### M3a: cross-compile engine and game, portability fixes, dropped-step counter, profile plumbing

**Contract** (Opus writes it before fan-out):
- `PhysicsWorld` gains `std::uint64_t droppedSteps() const`. It is incremented by floor(accumulator / fixedStep) at the point where `Physics.cpp:549` drops time, and exposed through the session as needed. A new case in `mb_physics_test` covers it, and the bench and the game log it per level.
- A physics profile type with the "desktop" profile (today's values) and the "vita" profile, plus the per-level override table. `mb_physics_smoke`, `mb_gameplay_smoke` and `mb_cloth_smoke` accept `--physics-profile <name>`.

**Code:**
- Cross-compile `mb_engine` unchanged, **including the SDL GPU files**, and `mb_game`. If `SDL_CreateGPUDevice` is not exported, the engine build waits for M2's `NONE` stub; the rest of M3a proceeds. M3a must not touch `engine/src/Renderer*.cpp`, `RenderFrame.cpp`, `PipelineCache.*`, `engine/src/render/` or the renderer source list. M2 owns them.
- The fixes the probes call for.
- `STBI_NO_THREAD_LOCALS` on Vita.
- The `random_device` replacement, only if the probe showed it throws.
- Explicit thread and body-mutex counts in the Vita profile.
- `error_code` filesystem calls.
- An atomic-save check: if `rename` over an existing file fails, remove then rename, keeping the backup.

**Accept:** the host suite is green, including the new test, and the vita CI job builds both libraries.

### M3b: physics bench VPK (title ID `MUGB00004`)

**Code:**
- The bench sets the clocks (444/222/222/166) and `_pthread_stack_default_user`, and draws its progress with SDL_Renderer and `SDL_RenderDebugText`.
- It reads `ux0:data/MuggleBall3/bench-physics.json`, which gives the levels, the **simulated** seconds, the velocity/position/collision steps, the thread count and an optional profile name.
- It runs Autopilot sessions with `mb::autoplay` at fixed 1/60 s steps, as fast as it can (not in real time).
- After every run it appends one CSV line to `bench-physics.csv`, so an interrupted run keeps its data. Each line holds:
  - the mean, p95, p99 and maximum step time;
  - peak and mean awake bodies;
  - dropped steps;
  - level read and parse time, and physics build time.
- The same bench logic runs on the Mac (as a host target or a smoke option), so the per-level ratio of device time to host time gives the measured scale factor.

**Level set (15):**
- the 10 heaviest: knight-miss-1457, village-tennis, piratos-dom-na-vode, knight-eggs, village-dinner, piratos-dva-doma, village-logovo-vedmi, knight-shashki, knight-v-yablochko, piratos-gorillas;
- the other three windmill levels: village-na-melnice, village-rinok, Village-Dvorik;
- knight-garden (peak triangles) and village-love (a median level).

**Pass 1:** 100/1/1, threads 0, all 15 levels, 20 simulated seconds each. Before handing it over, estimate the wall time (host ms per step × 70 × steps, plus loads) and keep each user run under 45 minutes.

**Pass 2:** only the combinations that pass 1 leaves open. For example: 30/1/1, 20/1/1 and 10/1/1 on non-windmill levels; 30/1/2 and 10/1/4; threads 1 and 2. Optionally, `-marm` against the default Thumb-2 build, for Jolt only.

**Accept:** the vita CI job also builds the bench VPK, and the host suite is green.

**Device checklist:** the CSVs from pass 1 and the agreed pass 2 come back.

**Then:**
1. Replace the 70x estimate in `docs/VITA.md` with the measured factor, and recompute the §4.4 estimates against the physics budget.
2. **Stop and agree the frame-rate target and the physics profile with the user** before M7.

### M4a: vitaGL backend and device render tests (title ID `MUGB00005`)

D owns `engine/src/render/` and the shaders.

**Code:**
- The full `Renderer` API, including `renderToImage` through an FBO and `glReadPixels` with a row flip (§4.3).
- Shaders with per-target preambles, the z remap, CW winding, the RGB colour mask and all 10 blend factors.
- 16-bit indices when the vertex count is 65535 or less.
- Streaming vertex buffers, and the cloth `updateMesh` path.
- The discard-free mesh variant.
- NPOT handling as in §5.7.
- **Normal matrix:** the cofactor (adjugate-transpose) of the upper 3x3 of `world`, multiplied by sign(det). It is exact for any invertible affine transform, needs no division, and the shader already normalises. Billboards already use non-uniform scale, and nobody has checked the .x frames or the level poses. Alternatively, first add a host check over all 83 levels that every lit DrawItem's 3x3 is orthogonal times a uniform scale, and use a cheaper form only if it passes.
- `tests/render_test.cpp` built as a device VPK that writes pass/fail results and failing images to `ux0:data/MuggleBall3/render-test/`.

**Accept:** desktop default untouched (`render_test` dumps identical); the GL variant compiles in CI if you kept it, and passes the 19 tests there if run.

**Device checklist:** all 19 render tests pass. Document any tolerance.

### M4b: level screenshot comparison

**Scenes:**
- knight-garden (peak triangles);
- knight-miss-1457 (heaviest physics);
- piratos-ostrovok (reference screenshot; it also has 3 sails);
- a level that uses the Water material (find it with grep over `assets/Data/Levels`);
- a second sail level, such as Piratos-Kartejniki;
- the main menu.

**Method:**
- Camera: each level's start camera at time 0, rendered at 960x544.
- The macOS references come from `renderToImage` at 960x544.
- A diff script in `tools/`, with a Python test.
- Tolerance: per-channel difference of 8 or less on at least 99 % of pixels.

**Device checklist:** every scene is within tolerance.

### M5a: move `runGame` (no behaviour change)

Move `runGame` and its helpers (Options, Assets, openSave, windowSize, saveWindowState, FrameStats) from `main.cpp`'s anonymous namespace into `app/Game.cpp`.

**Accept:** host suite green, and the behaviour is identical.

### M5b: Vita front end

E owns `Window.cpp` and `app/`.

**Code:**
- **Vita entry point:**
  - clocks;
  - `sceAppUtilInit` and the enter-button read;
  - `_newlib_heap_size_user`, `sceUserMainThreadStackSize` and `_pthread_stack_default_user`;
  - the log file;
  - a fixed user directory `ux0:data/MuggleBall3/`.
- Input as in §5.8: the virtual cursor sprite and IME name entry.
- Hide Fullscreen and the window sizes on Vita.
- Decide with the user, on the device, whether the 960x544 layout needs a larger HUD or text scale. The UI scale is 0.708 inside a 725x544 4:3 area.
- **Suspend and resume:**
  - First test on the device with no handling at all: PS button, sleep, and wake in the middle of a level. Add handling only for what breaks; likely candidates are flushing saves and restarting the audio stream.
  - Poll `sceAppMgrReceiveSystemEvent` every frame. On `SCE_APPMGR_SYSTEMEVENT_ON_RESUME`, open the pause screen and reset the PlaySession clock. The 0.1 s dt clamp already absorbs the steady_clock jump.
  - Never call `SDL_EnableScreenSaver`.
  - Pump SDL events (or call `sceKernelPowerTick`) from the level-load progress callbacks.
  - Saves stay write-on-change: closing the app from LiveArea kills the process without notice.

The input work can start on the host, against `mb_window_tests` and `mb_player_input_tests`, while M4 is still running.

**Accept:** host tests pass.

**Device checklist:**
- two levels played with touch only, and two with buttons only;
- touching to move the bat does not launch the ball;
- everything works with the enter button set to Circle;
- pause, resume and quit work, including the agreed Start-while-paused behaviour;
- name entry works, including cancelling and reopening the keyboard;
- changed options persist across a restart;
- PS button, sleep and wake work in the middle of a level.

### M6a: `tools/vita_pack.py` (can start any time; Python only)

Copies only the runtime files and resamples the NPOT mipmapped textures (§5.7). Comes with Python tests.

### M6b: memory and the game bench mode

**Code:**
- On Vita, clear the `ModelCache` (GPU handles and CPU model copies) on level change, keeping the UI. Desktop memory behaviour stays the same unless the user agrees otherwise.
- Prewarm and cap the sound cache.
- Size the Jolt world for the Vita profile.
- A memory telemetry line per level in the log: newlib heap use (via `mallinfo`, verify it on newlib) and vitaGL free RAM and CDRAM (`vglMemFree`).
- **Game bench mode:** if `ux0:data/MuggleBall3/bench-game.json` exists, the game runs `PlaySession` with `options.autoplay` through the real FrontEnd and the vitaGL renderer, in real time, level by level.
  - If a level ends early, it reloads the level and continues until 60 s of play have accumulated.
  - It excludes the 2 s after each load.
  - It reuses FrameStats.
  - As each level finishes, it appends one line per level to `ux0:data/MuggleBall3/bench-frames.csv`: the presented-interval histogram, work-time mean and p95, physics p95 per frame, dropped steps and memory.

**Accept:** host suite green.

**Device checklist:** over 10 consecutive level loads, including knight-garden:
- the newlib heap high-water mark is at most 80 % of `_newlib_heap_size_user`;
- vitaGL free RAM and free CDRAM stay at 20 % or more at the peak;
- memory does not grow from one load to the next.

Record the chosen heap size in `docs/VITA.md`.

### M7: performance to target (needs M3b's agreed profile)

**Code:**
- **Physics profile and per-level overrides,** validated on the host with real data and `--physics-profile vita`: `mb_physics_smoke`, `mb_gameplay_smoke --seconds 120`, `mb_cloth_smoke`, plus the 240 s no-ball stability check on all 83 levels. Paste the results into the PR.
- **Renderer CPU work:** fewer state changes, cached uniforms.
- **Audio:** keep the F32 Mixer. Measure both options:
  - keep 44.1 kHz (SDL opens the BGM port and does no resampling);
  - run the Mixer at 48 kHz to use the main port (about 9 % more mixing).

  Check on the device that BGM-port audio isn't muted by the system music player (verify). Put the audio thread and any Jolt workers on other cores.

**Starting budgets** for a 30 fps frame (33.3 ms):

| Part | Budget |
|---|---|
| Physics | ≤ 16 ms p95 per frame (two steps of ≤ 8 ms) |
| Render submission | ≤ 8 ms |
| Rules + scene build | ≤ 2 ms |
| Audio | on its own core |
| Slack | about 5 ms |

**Device checklist:** the full 83-level game bench meets §1 item 2. It can run in chunks, because results are appended per level. Publish the table in `docs/VITA.md`.

### M8: packaging and docs

**Code:**
- The final LiveArea and title ID.
- `MB3_VITA_BUNDLE_ASSETS`.
- An opt-in personal LiveArea art script that builds from the user's assets, the way `make_icon.py` does. Its output is 8-bit indexed and never committed.
- `docs/VITA.md` completed, plus updates to README, ARCHITECTURE, SAVES, PHYSICS and AUDIO.
- A release checklist.

**Device checklist:** a clean install from the VPK works, and saves survive reinstalling the VPK.

## 7. Parallel work and agent assignments

1. **Now, while waiting for answers:** Opus writes the M2 contract. Agent A does M2 in `vita-m2-renderer-seam`. Optionally, Agent C does M6a (Python only, no build directory).
2. **After the answers and the install:** Opus does the M1a spikes personally, because they decide everything after them. Then M1a, then M1b.
3. **After M1a merges:** Agent B does M3a, then M3b. It needs the SDK, so watch disk: Agent A's host build plus Agent B's cross build are the two build directories allowed.
4. **After M2 and M1a merge:** Agent D does M4a and then M4b, owning `engine/src/render/` and the shaders. Agent E does M5a and then M5b, owning `Window.cpp` and `app/`. Keep their file sets apart.
5. **Later:** M6b and M7 need the device data from M3b and M4a. M8 comes last. Update the docs in every PR.
6. **Every agent prompt states:**
   - the files the agent owns and the files it must not touch;
   - its acceptance tests;
   - the CI portability rules and the fast-math rule;
   - the disk rules: one build directory, scratch deleted afterwards, reviewers count toward the limit;
   - the SDK `export` line from §2;
   - its commit trailer.
7. **Reviewers** get the diff, §4.3 (the renderer contract) and their lens (fidelity or robustness).

## 8. Risks and how to catch them early

| Risk | Detect by | Response |
|---|---|---|
| SDL3 window/events and vitaGL fight over the display | M1a hello on the device | Vita `Window` on sceCtrl/sceTouch (§5.8) |
| IME never appears, or the app hangs | M1a hello (open, confirm, cancel) | `vglSwapBuffers(GL_TRUE)` while the IME is shown; letter-picker fallback |
| vitaGL's GLSL translator rejects a shader | Compile all 4 real programs in M1a and M4a | Simplify the GLSL, write that program in Cg, check vitaGL issues |
| libshacccg missing on the device | First thing in M1a | User extracts it, or the GXP route |
| Jolt platform hook fails | M1a Jolt spike | Patched copy in the build tree (never `mb3-deps`) |
| `posix_memalign` missing | M1a nm check | Vita-only shim (§5.4) |
| Thread stacks too small (random corruption with workers) | Set `_pthread_stack_default_user` before any run with threads ≥ 1 | Raise it; keep the vitaGL threshold large enough |
| Fast-math reaching Jolt or the physics code | Check compile flags in M1a and M3a | Per-target flags only |
| LiveArea PNGs rejected, or a blank bubble | Python test (size, colour type 3); M1a install | Indexed PNGs |
| Physics far over budget | M3b bench, before any renderer work | §5.5 options; renegotiate the target |
| C++ library gaps (format, filesystem, thread_local) | M1a probes | Shims (§5.4) |
| `random_device` throws | M1a runtime probe | One seeded helper |
| `hardware_concurrency()` == 0 | M1a probe | Harmless (single-threaded); the Vita profile sets counts explicitly |
| Touch launches the ball every time a finger lands | M5b checklist | Drag never presses; tap or button launches (§5.8) |
| Circle/Cross enter-button swap ignored | M5b checklist with the enter button set to Circle | Read the system parameter (§5.8) |
| NPOT + REPEAT + mips misbehave | `render_test` on the device | Resample (§5.7) |
| `rename` over an existing file fails, so saves are lost | M1a runtime probe, M3a | Remove, then rename, keeping the backup |
| Memory exhaustion over a campaign | M6b telemetry | Eviction; A8 fonts; ATTRIBUTE2=12 as a last resort |
| Load stalls (JSON parse, directory scans, sound decode) | M3b load times | Index file, prewarm, smaller JSON |
| Auto-sleep or a hang during long loads | M5b checklist | Pump events during loads |
| Desktop regressions | Full host suite, `render_test` dumps, package-macos in every PR | Revert |
| Toolchain churn | Pinned container digest, pinned vitaGL commit and flags, SDK version in `docs/VITA.md` and `vita-port.md` | Update deliberately |

## 9. Verification ladder

1. **Host, every PR:** all ctest targets, `python3 -m unittest discover -s tests -p 'test_*.py'`, and the desktop CI jobs. Run the data-dependent smokes locally with `MB3_ASSETS` and paste the results into the PR.
2. **Cross-compile:** the local SDK build and the `vita` CI job.
3. **Vita3K** (if the user opted in): boot, the menu renders, and a scripted smoke test returns an exit code. Emulator-only bugs are not blockers.
4. **Device:** these steps are the user's. Batch your requests, and give exact steps plus the list of files to send back (logs, bench CSVs, screenshots). Never ask the user to run something you haven't built and checked on the host first. Track open requests in `vita-port.md`, and record results in the "Device checklist" section of `docs/VITA.md`.

## 10. Legal and personal use

- The game data is the user's own copy of Magic Ball 3. It is converted locally and never committed.
- A VPK that contains data is for the user's own device only. Never upload one: not to GitHub releases, not as a CI artifact, not anywhere else. CI builds only data-free VPKs. The repo is private, and the rule still applies.
- Never commit or bundle Sony files: firmware, `libshacccg.suprx`, or SDK pieces under NDA.
- vitaGL is LGPL-3.0. Keep the app's source and build scripts available (they are in the repo) so the app can be relinked, and note this in `docs/VITA.md`.
- LiveArea art built from game images stays local, like the macOS icon.

## 11. Reporting back

- After each (sub-)milestone, give the user a short status:
  - what merged, with the PR link;
  - what was measured on the device;
  - what is next;
  - any decision that needs them.
- Keep `docs/VITA.md` up to date as the single place for decisions, measured numbers, open questions and the device checklist. Keep `vita-port.md` up to date for resuming.
- If a measurement contradicts this brief, the measurement wins. Say so, and update the docs.
