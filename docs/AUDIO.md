# Sound and music

The original used BASS. Here SDL3 opens the audio device, stb_vorbis decodes the
Ogg Vorbis files and a small software mixer in `engine/` does the rest. The data
formats are in [`ASSET_FORMATS.md`](ASSET_FORMATS.md).

## Engine (`engine/`)

| Part | What it does |
|---|---|
| `Vorbis.h` | `decodeVorbis*` decodes a whole sound effect to 16-bit PCM (they total 29 MB, decoded once and kept). `openVorbisStream*` decodes a music track as it is read, from the compressed file held in memory (about 1 MB), and can `rewind()`. Mono and stereo only. |
| `Mixer.h` | Software mixer, no SDL. 32 sound-effect voices plus music, each effect with gain, pan and pitch (linear resampling, so sounds at any sample rate play at the right speed on the mixer's 44.1 kHz). Master, effects and music volumes (0 to 1, linear amplitude), music crossfades, pause. The output is soft-limited, so it never exceeds +-1; the music peaks at 1.2 once decoded and needs it. `render()` fills a buffer, so the mixer is also an offline renderer. |
| `Audio.h` | `Audio` opens the default playback device (`SDL_OpenAudioDeviceStream`, float stereo at 44.1 kHz, SDL converts to what the hardware wants) and feeds it from the mixer. It caches decoded sounds. With no audio device it logs one warning and every call does nothing (`available()` is false). `AudioPlayer` is the small interface game code plays through. |

Threads: the device calls `Mixer::render()` on its own thread. Every other
`Mixer` call, from the game thread, only queues its change under a mutex held for
a few instructions; `render()` applies the queue first thing, so a call takes
effect within one device buffer and neither side ever waits on decoding or disk.
Music is decoded inside `render()`: stb_vorbis decodes it a thousand times faster
than real time, so there is no streaming thread or ring buffer to underrun. A
sound effect is decoded on its first play, on the game thread.

Details worth knowing:

- Mono effects pan with a constant-power law (0.707 on each side in the centre);
  stereo effects and music pan as a balance (untouched in the centre).
- Gains ramp across each 256-frame block, so changing a volume, pausing, stopping
  or panning never clicks. Pause ramps down, then the voices stand still; resume
  ramps up where they stopped.
- With all voices busy, the next effect cuts one that is already stopping, then a
  one-shot before a loop, then the quietest, then the oldest; an effect quieter
  than the one it would cut is dropped.
- A new music track fades in while the old one fades out, on equal-power curves.
  A looping track repeats from the first frame of the same stream with no gap.

## Game (`game/`): `SoundLibrary`

`SoundCatalog` reads `sounds.json` and `musics.json`; `SoundLibrary` plays by
name through an `AudioPlayer` (so the tests use a recorder instead of a device).

- `play(name, gain, pan)`: one random sample, at a random pitch from its range.
  Names match ignoring case. An unknown name is warned about once; a name whose
  sample files are missing from the game (most `*_drop`) is silent without a
  warning, as in the original.
- **Replay delay.** A sample's `delay` is read as the time before its sound may
  play again, applied to the sound as a whole (the delay of the sample that just
  played counts). Evidence: the shoot and dead sounds have 100 or 500 ms, which
  stops a crowd of simultaneous hits from stacking; `menu_piratos`, which the
  stage menu plays when you click the pirate logo, is `pirat_period1.ogg` with a
  delay of 50000 ms, which can only be a lockout against clicking; nothing else in
  the data uses `delay`. The original game code is not available, so how it
  triggers the `*_period` sounds is inferred.
- **Periodic sounds** (`startPeriodic`, a material's `periodSound`): an object
  plays its idle sound now and then. Each object has its own timer, starts at a
  random point of the first delay and waits the delay of the sample it just played
  (12 to 35 s in the data). If the original in fact throttles all objects of a
  kind with the shared lockout above, the change is to call `play()` here instead.
- **Loops** (`startLoop`, a material's `loopSound`, only `spring_loop`): not held
  back by the delay; stop them with `stopLoop`.
- **Music**: `playMusicForLevel(levelName)` maps the level's file name to a
  world: `piratos-...` Pirates, `cowboys-...` Cowboys, `knight-...` Knights,
  `village-...` Village, anything else Menu. It loops, crossfades from the track
  playing and does nothing if that track is already playing.
- `update(seconds)` advances the clock that delays and periodic timers use; call it
  once per frame with the game's frame time (the app's `GameAudio` does).

## In the app (`app/GameAudio.*`)

`GameAudio` owns the `Audio` and the `SoundLibrary`, plays the menus' and each world's music, sets
the volumes, pauses and ticks the clock. Game code reaches the library through
`GameAudio::sounds()`, which is null if the sound data is missing (the volumes and the pause
then still answer, and everything else is silent). `Music: <name>` is logged each time a track
is asked for.

```cpp
// From a contact event (physics): a brick is hit, broken, dropped
sounds->play(brick.material, mb::MaterialSound::Hit, gain, pan);
// When an object with a periodSound / loopSound appears and when it goes
id = sounds->startPeriodic(material, 0.8f, pan);   sounds->stopPeriodic(id);
voice = sounds->startLoop(material);               sounds->stopLoop(voice);
// Options and pause menus (the game does these through connectAudio, below)
audio.setVolumes(music, sound);   // 0 to 1; the mixer's master volume stays 1
audio.setPaused(true);            // pause screen; false resumes
```

`gain` is a linear amplitude (1 is the file's own level) and `pan` runs from -1
(left) to 1 (right), for example from the contact's x position.

### The game

`runGame` creates the `GameAudio` right after the window (the device closes before SDL quits) and
`mb_app::connectAudio` (`app/FrontEndAudio.cpp`) hooks it to the front end:

| From | To |
|---|---|
| `MenuHooks::playSound(name)`: the `sound_mouse_in` / `sound_mouse_down` names of the forms (`menustonein`, `menustonedown`, ...) | `SoundLibrary::play(name)` |
| `MenuHooks::settingsChanged(settings)`: once at the start with the saved settings, then at every change in Options | the mixer's music volume (`musicVolume`) and effects volume (`soundVolume`), both 0 to 1 as saved; the master volume stays 1. The menus' own sounds are effects, so the sound slider is theirs too |
| `FrontEndEvents::menuShown`: at the start, and on coming back from a level | the `Menu` track |
| `FrontEndEvents::levelStarted(name)`: the Loading form has gone and play begins | `playMusicForLevel(name)`, crossfading from the menu music |
| `FrontEndEvents::paused(bool)` | `Mixer::setPaused` |
| `FrontEndEvents::frame` | `GameAudio::update()` |

**Pause.** The mixer's pause is for everything: it fades all effects and the music out and freezes them, and a sound
asked for while it lasts starts frozen, so it is heard only after the resume. That fits the pause screen, which has
no sounds of its own and no widgets, so nothing asks for one meanwhile. What has to hold is that the pause ends before
the menus come back, or their first hover sound would wait: the front end announces `paused(false)` before
`menuShown()` when Escape leaves the pause screen for the main menu. `tests/test_wiring.cpp` checks that order and that
the menus' sounds play after it, and `tests/frontend_smoke.cpp` checks the sequence through the real front end. A
sound that must play during a pause would need a mixer that pauses only the game's voices; this one cannot.

## Trying it without speakers

```sh
MuggleBall3=./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3
$MuggleBall3 --play-sound pirat_hit            # waits until it has played
$MuggleBall3 --play-music Pirates --seconds 20
# Write what would reach the speakers to a file (raw float32 stereo, 44.1 kHz):
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw $MuggleBall3 --play-sound pirat_hit
```

`tests/mb_wiring_tests` runs the game's `GameAudio` over a made-up sound list on the disk driver and checks the menus'
sounds, the music, the volumes and the pause; `tests/mb_frontend_smoke` does the same through the real front end and
the real data, with clicks on buttons and on the Options slider.

`tests/mb_audio_smoke` does this against the real data and compares the file with
the decoded samples. The disk driver runs in real time, so a test takes as long as
the sound.
