// SoundLibrary tests: name resolution, random sample and pitch choice, replay delays, periodic and
// looping sounds, material helpers and music selection. The audio engine is replaced by a recorder,
// so no audio device and no game data are needed.

#include "mb/game/PlaySounds.h"
#include "mb/game/SoundLibrary.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Counts the warnings logged while alive.
class WarningCapture {
public:
    WarningCapture() {
        SDL_GetLogOutputFunction(&previous_, &previousData_);
        SDL_SetLogOutputFunction(&WarningCapture::onLog, this);
    }
    ~WarningCapture() { SDL_SetLogOutputFunction(previous_, previousData_); }
    WarningCapture(const WarningCapture&) = delete;
    WarningCapture& operator=(const WarningCapture&) = delete;

    int count = 0;

private:
    static void onLog(void* self, int, SDL_LogPriority priority, const char*) {
        if (priority == SDL_LOG_PRIORITY_WARN) {
            ++static_cast<WarningCapture*>(self)->count;
        }
    }

    SDL_LogOutputFunction previous_ = nullptr;
    void* previousData_ = nullptr;
};

struct Played {
    fs::path file;
    VoiceParams params;
};

// Records what the library asks of the audio engine.
class RecordingAudio final : public AudioPlayer {
public:
    VoiceId playSound(const fs::path& file, const VoiceParams& params) override {
        sounds.push_back({file, params});
        return ++lastVoice;
    }
    void stopSound(VoiceId voice) override { stopped.push_back(voice); }
    void playMusic(const fs::path& file, float fadeSeconds, bool loop) override {
        music.push_back(file);
        musicFade = fadeSeconds;
        musicLoops = loop;
    }
    void stopMusic(float) override { ++musicStops; }

    std::vector<Played> sounds;
    std::vector<VoiceId> stopped;
    std::vector<fs::path> music;
    float musicFade = -1;
    bool musicLoops = false;
    int musicStops = 0;
    VoiceId lastVoice = 0;
};

const fs::path kSounds = "assets/Sounds";
const fs::path kMusics = "assets/Musics";

constexpr const char* kSoundsJson = R"({"version": 1, "sounds": [
  {"name": "MenuStoneDown", "samples": [{"file": "menustonedown.ogg"}]},
  {"name": "rock_hit", "samples": [{"file": "h1.ogg"}, {"file": "h2.ogg"}, {"file": "h3.ogg"}, {"file": "h4.ogg"}, {"file": "h5.ogg"}]},
  {"name": "ballHitBoard", "samples": [{"file": "board.ogg", "freqStart": 1.1, "freqEnd": 0.7}]},
  {"name": "bat_dead", "samples": [{"file": "bat.ogg", "delayMs": 500}]},
  {"name": "pirat_period", "samples": [{"file": "p1.ogg", "delayMs": 30000}, {"file": "p2.ogg", "delayMs": 35000}]},
  {"name": "pirat_drop", "samples": []},
  {"name": "Meteor", "samples": [{"file": "first.ogg", "delayMs": 100}]},
  {"name": "meteor", "samples": [{"file": "second.ogg"}]},
  {"name": "tick", "samples": [{"file": "tick.ogg", "delayMs": 1000}]},
  {"name": "spring_loop", "samples": [{"file": "spring.ogg"}]}
]})";

constexpr const char* kMusicsJson = R"({"version": 1, "musics": [
  {"name": "Menu", "file": "menu.ogg"},
  {"name": "Village", "file": "village.ogg"},
  {"name": "Pirates", "file": "pirates.ogg"},
  {"name": "Cowboys", "file": "cowboy.ogg"},
  {"name": "Knights", "file": "knights.ogg"}
]})";

SoundCatalog catalog() { return SoundCatalog::parse(kSoundsJson, kMusicsJson, kSounds, kMusics); }

bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

void testCatalog() {
    const SoundCatalog c = catalog();
    const SoundDef* stone = c.findSound("menustonedown");
    check(stone && stone->name == "MenuStoneDown", "names match in any case");
    check(c.findSound("MENUSTONEDOWN") == stone, "...whichever case is asked for");
    check(stone && stone->samples.size() == 1 && stone->samples[0].file == kSounds / "menustonedown.ogg",
          "a sample's file is under the Sounds folder");
    check(c.findSound("nothing") == nullptr, "unknown name");

    const SoundDef* board = c.findSound("ballHitBoard");
    check(board && board->samples[0].pitchMin == 0.7f && board->samples[0].pitchMax == 1.1f,
          "the pitch range is ordered whichever way the XML wrote it");
    const SoundDef* bat = c.findSound("bat_dead");
    check(bat && bat->samples[0].delayMs == 500 && bat->samples[0].pitchMin == 1 && bat->samples[0].pitchMax == 1,
          "delay, and no pitch range means normal pitch");
    const SoundDef* drop = c.findSound("pirat_drop");
    check(drop && drop->samples.empty(), "a sound without a file is known, and silent");
    const SoundDef* meteor = c.findSound("meteor");
    check(meteor && meteor->samples[0].file == kSounds / "first.ogg", "the first definition of a name wins");

    check(c.findMusic("pirates") && *c.findMusic("PIRATES") == kMusics / "pirates.ogg", "music names match in any case");
    check(c.findMusic("Cowboys") && *c.findMusic("Cowboys") == kMusics / "cowboy.ogg", "music file");
    check(c.findMusic("jazz") == nullptr, "unknown music");
    check(c.musicCount() == 5, "five tracks");

    check(throwsWith([] { SoundCatalog::parse("{", kMusicsJson, kSounds, kMusics); }, "invalid sound JSON"), "bad JSON");
    check(throwsWith([] { SoundCatalog::parse(R"({"version": 2, "sounds": []})", kMusicsJson, kSounds, kMusics); },
                     "unsupported version 2"),
          "version");
    check(throwsWith([] { SoundCatalog::parse(R"({"sounds": [{"name": "x"}]})", kMusicsJson, kSounds, kMusics); },
                     "invalid sound JSON"),
          "a sound without samples");
    check(throwsWith([] { SoundCatalog::load("/nonexistent"); }, "convert_assets"), "missing files say what to run");
}

void testPlay() {
    RecordingAudio audio;
    SoundLibrary library(audio, catalog(), 1);
    WarningCapture warnings;

    check(library.play("MenuStoneDown", 0.5f, -0.25f) != 0 && audio.sounds.size() == 1, "a known sound plays");
    check(audio.sounds[0].file == kSounds / "menustonedown.ogg" && audio.sounds[0].params.gain == 0.5f &&
              audio.sounds[0].params.pan == -0.25f && !audio.sounds[0].params.loop && audio.sounds[0].params.pitch == 1,
          "with the gain, pan and normal pitch asked for");
    check(library.play("menustonedown") != 0 && audio.sounds.size() == 2, "lookups ignore case");

    check(library.play("nosuch") == 0 && library.play("NoSuch") == 0 && library.play("nosuch") == 0, "unknown sound plays nothing");
    check(warnings.count == 1, "and is warned about once");
    check(library.play("") == 0 && warnings.count == 1, "an empty name is quietly nothing");
    check(library.play("pirat_drop") == 0 && warnings.count == 1 && audio.sounds.size() == 2,
          "a known sound without a file is silent without a warning");
}

void testRandomChoice() {
    const auto choices = [](std::uint32_t seed, int plays) {
        RecordingAudio audio;
        SoundLibrary library(audio, catalog(), seed);
        std::vector<std::string> files;
        for (int i = 0; i < plays; ++i) {
            library.play("rock_hit");
            files.push_back(audio.sounds.back().file.filename().string());
        }
        return files;
    };
    const auto first = choices(7, 1000);
    std::map<std::string, int> counts;
    for (const std::string& file : first) {
        ++counts[file];
    }
    check(counts.size() == 5, "every sample gets picked");
    check(std::ranges::all_of(counts, [](const auto& entry) { return entry.second > 150 && entry.second < 250; }),
          "about equally often");
    check(choices(7, 1000) == first, "the same seed picks the same samples");
    check(choices(8, 1000) != first, "another seed picks others");

    RecordingAudio audio;
    SoundLibrary library(audio, catalog(), 3);
    float low = 9, high = 0, sum = 0;
    for (int i = 0; i < 1000; ++i) {
        library.play("ballHitBoard");
        const float pitch = audio.sounds.back().params.pitch;
        low = std::min(low, pitch);
        high = std::max(high, pitch);
        sum += pitch;
    }
    check(low >= 0.7f && high <= 1.1f && low < 0.72f && high > 1.08f, "pitch covers its range and stays inside");
    check(std::fabs(sum / 1000 - 0.9f) < 0.02f, "evenly");
}

void testReplayDelay() {
    RecordingAudio audio;
    SoundLibrary library(audio, catalog(), 5);
    check(library.play("bat_dead") != 0, "first play");
    check(library.play("bat_dead") == 0 && audio.sounds.size() == 1, "an immediate replay is ignored");
    library.update(0.4);
    check(library.play("bat_dead") == 0, "still inside the delay");
    library.update(0.11);
    check(library.play("bat_dead") != 0 && audio.sounds.size() == 2, "plays again once the delay has passed");
    check(library.play("MenuStoneDown") != 0 && library.play("MenuStoneDown") != 0, "no delay, no limit");
    library.update(-3);
    check(library.play("bat_dead") == 0, "time never runs backwards");

    // The delay of the sample that played is the one that counts.
    const bool waits = [&] {
        RecordingAudio a;
        SoundLibrary lib(a, catalog(), 11);
        lib.play("pirat_period");
        const double delay = a.sounds[0].file.filename() == "p1.ogg" ? 30.0 : 35.0;
        lib.update(delay - 0.1);
        const bool blocked = lib.play("pirat_period") == 0;
        lib.update(0.2);
        return blocked && lib.play("pirat_period") != 0;
    }();
    check(waits, "a multi-sample sound waits the delay of the sample just played");
}

void testPeriodic() {
    constexpr double kStep = 0.0625;
    {
        RecordingAudio audio;
        SoundLibrary library(audio, catalog(), 2);
        const PeriodicId id = library.startPeriodic("tick");
        check(id != 0, "a sound with a delay can repeat");
        std::size_t first = 0;
        for (int step = 1; step <= 960; ++step) {  // 60 s in steps of 1/16 s, which add up exactly
            library.update(kStep);
            if (first == 0 && !audio.sounds.empty()) {
                first = static_cast<std::size_t>(step);
            }
        }
        check(audio.sounds.size() == 60, "one play per delay");
        check(first >= 1 && first <= 16, "the first play comes within the first delay");
        check(audio.sounds[0].file == kSounds / "tick.ogg" && !audio.sounds[0].params.loop, "it plays the sample once each time");
        library.stopPeriodic(id);
        const std::size_t before = audio.sounds.size();
        for (int step = 0; step < 100; ++step) {
            library.update(kStep);
        }
        check(audio.sounds.size() == before, "stopped");
    }
    {
        // Objects do not play in chorus: their first plays are spread over the delay.
        RecordingAudio audio;
        SoundLibrary library(audio, catalog(), 4);
        for (int i = 0; i < 20; ++i) {
            library.startPeriodic("tick");
        }
        std::vector<int> stepsWithPlays;
        std::size_t seen = 0;
        for (int step = 1; step <= 16; ++step) {
            library.update(kStep);
            if (audio.sounds.size() != seen) {
                stepsWithPlays.push_back(step);
                seen = audio.sounds.size();
            }
        }
        check(seen == 20 && stepsWithPlays.size() > 5, "twenty objects, twenty different start times");
    }
    {
        RecordingAudio audio;
        SoundLibrary library(audio, catalog(), 6);
        WarningCapture warnings;
        check(library.startPeriodic("MenuStoneDown") == 0 && warnings.count == 1, "a sound without a delay cannot repeat");
        check(library.startPeriodic("pirat_drop") == 0 && warnings.count == 1, "nor a silent one, quietly");
        check(library.startPeriodic("nosuch") == 0 && warnings.count == 2, "nor an unknown one");
        library.update(100);
        check(audio.sounds.empty(), "and nothing plays");
        library.stopPeriodic(0);  // stopping nothing is fine
    }
}

void testLoopsAndMaterials() {
    RecordingAudio audio;
    SoundLibrary library(audio, catalog(), 9);
    const VoiceId a = library.startLoop("spring_loop", 0.5f, 0.25f);
    const VoiceId b = library.startLoop("spring_loop");
    check(a != 0 && b != 0 && a != b && audio.sounds.size() == 2, "loops are not held back by a delay");
    check(audio.sounds[0].params.loop && audio.sounds[0].params.gain == 0.5f && audio.sounds[0].params.pan == 0.25f,
          "a loop loops");
    library.stopLoop(a);
    check(audio.stopped == std::vector<VoiceId>{a}, "and stops by voice");
    check(library.startLoop("pirat_drop") == 0 && library.startLoop("") == 0, "no loop without a file");

    GameMaterial material;
    material.hitSound = "MenuStoneDown";
    material.deadSound = "bat_dead";
    material.periodSound = "tick";
    material.loopSound = "spring_loop";
    WarningCapture warnings;
    audio.sounds.clear();
    check(library.play(material, MaterialSound::Hit) != 0 && audio.sounds.back().file.filename() == "menustonedown.ogg",
          "a material's hit sound");
    check(library.play(material, MaterialSound::Dead) != 0 && audio.sounds.back().file.filename() == "bat.ogg",
          "its dead sound");
    check(library.play(material, MaterialSound::Drop) == 0 && warnings.count == 0, "no drop sound named: nothing, quietly");
    check(library.startPeriodic(material) != 0, "its period sound");
    check(library.startLoop(material) != 0 && audio.sounds.back().params.loop, "its loop sound");
}

// The sounds of a game's events.
void testPlaySounds() {
    constexpr const char* soundsJson = R"({"version": 1, "sounds": [
      {"name": "ballHitBat", "samples": [{"file": "hitbat.ogg"}]},
      {"name": "ballHitBoard", "samples": [{"file": "board.ogg"}]},
      {"name": "ballHitBall", "samples": [{"file": "balls.ogg"}]},
      {"name": "bat_dead", "samples": [{"file": "batdead.ogg"}]},
      {"name": "endlevel", "samples": [{"file": "end.ogg"}]},
      {"name": "box_hit", "samples": [{"file": "boxhit.ogg"}]},
      {"name": "box_dead", "samples": [{"file": "boxdead.ogg"}]},
      {"name": "night", "samples": [{"file": "night.ogg"}]},
      {"name": "money", "samples": []},
      {"name": "ballMagnited", "samples": [{"file": "magnited.ogg"}]},
      {"name": "ballRubbered", "samples": [{"file": "rubbered.ogg"}]},
      {"name": "explosion_ball", "samples": [{"file": "blast.ogg"}]}]})";
    RecordingAudio audio;
    SoundLibrary library(audio, SoundCatalog::parse(soundsJson, kMusicsJson, kSounds, kMusics), 3);
    PlaySounds sounds(library);
    GameMaterial box;
    box.hitSound = "box_hit";
    box.deadSound = "box_dead";
    GameMaterial silent;  // names no sounds

    std::optional<PrizeKind> prize;
    const auto play = [&](EventType type, float x = 0, const GameMaterial* material = nullptr, int points = 0) {
        audio.sounds.clear();
        GameEvent event;
        event.type = type;
        event.position = {x, 0.6f, 5};
        event.material = material;
        event.points = points;
        event.prize = prize;
        sounds.play(event);
    };
    const auto heard = [&]() { return audio.sounds.size() == 1 ? audio.sounds[0].file.filename().string() : std::string("-"); };

    play(EventType::BatHit);
    check(heard() == "hitbat.ogg", "the ball is hit by the bat");
    play(EventType::BallLaunched);
    check(heard() == "hitbat.ogg", "and launched by it, which is its hit");
    play(EventType::BoardHit);
    check(heard() == "board.ogg", "the ball meets a wall");
    play(EventType::BallsHit);
    check(heard() == "balls.ogg", "two balls meet");
    play(EventType::BrickHit, 0, &box);
    check(heard() == "boxhit.ogg", "a brick that is hit makes its material's hit sound");
    play(EventType::BrickDestroyed, 0, &box, 30);
    check(heard() == "boxdead.ogg", "one that is destroyed, its dead sound");
    play(EventType::ActorFell, 0, &box, 30);
    check(heard() == "boxdead.ogg", "so is one that fell off the board and was worth points");
    play(EventType::ActorFell, 0, &box, 0);
    check(audio.sounds.empty(), "but a piece worth none is not a brick and is silent");
    play(EventType::BrickHit, 0, &silent);
    check(audio.sounds.empty(), "a material with no sound makes none");
    play(EventType::BrickHit);
    check(audio.sounds.empty(), "nor does a hit that names no material");
    play(EventType::BatLeft);
    check(heard() == "batdead.ogg", "the bat flies off after the last ball");
    play(EventType::LevelComplete);
    check(heard() == "end.ogg", "the level is won");
    for (const EventType quiet : {EventType::BallLost, EventType::BallFreed, EventType::LifeLost, EventType::BatEntered, EventType::GameOver}) {
        play(quiet);
        check(audio.sounds.empty(), "these events make no sound");
    }

    // The prizes: a catch makes the prize material's dead sound, and the effects their own.
    GameMaterial nightPrize, moneyPrize;
    nightPrize.deadSound = "night";
    moneyPrize.deadSound = "money";  // a sound with no file, as the Bonus's and the Bazooka's
    prize = PrizeKind::Night;
    play(EventType::PrizeCaught, -10, &nightPrize, 280);
    check(heard() == "night.ogg" && audio.sounds[0].params.pan == -0.5f, "a prize that is caught makes its material's dead sound, panned by where it was");
    prize = PrizeKind::Bonus;
    play(EventType::PrizeCaught, 0, &moneyPrize, 777);
    check(audio.sounds.empty(), "a prize whose sound has no file is caught in silence");
    prize = PrizeKind::NextLevel;
    play(EventType::PrizeCaught, 0, &nightPrize, 120);
    check(audio.sounds.empty(), "the star's sound is the level's, which LevelComplete plays");
    prize = PrizeKind::Night;
    play(EventType::PrizeCaught);
    check(audio.sounds.empty(), "a catch that names no material is silent");
    prize.reset();
    play(EventType::BallStuck);
    check(heard() == "magnited.ogg", "the Magnet catches a ball");
    play(EventType::BallRubbered);
    check(heard() == "rubbered.ogg", "the Yo-Yo's string turns the ball back");
    play(EventType::Blast);
    check(heard() == "blast.ogg", "the Explosive Ball goes off");
    prize = PrizeKind::Magnet;
    for (const EventType quiet : {EventType::PrizeDropped, EventType::PrizeMissed, EventType::EffectStarted, EventType::EffectEnded, EventType::StrikeGranted}) {
        play(quiet, 0, &nightPrize);
        check(audio.sounds.empty(), "a prize that appears, is missed or runs out makes no sound of its own");
    }
    prize.reset();

    // A name a material uses that sounds.xml does not have (wood_hit, wood_dead, cannon_hit, cannon_dead and mikrofon_hit in the
    // real data, docs/AUDIO.md) is silent, and warned about once however often it comes up.
    GameMaterial barrel;
    barrel.hitSound = "wood_hit";
    barrel.deadSound = "wood_dead";
    {
        WarningCapture warnings;
        for (int i = 0; i < 3; ++i) {
            play(EventType::BrickHit, 0, &barrel);
            check(audio.sounds.empty(), "a hit with a sound the game does not know is silent");
            play(EventType::BrickDestroyed, 0, &barrel, 4);
            check(audio.sounds.empty(), "so is the destruction");
        }
        check(warnings.count == 2, "and each of the two names is warned about once, not every time");
    }

    play(EventType::BoardHit, -20);
    check(audio.sounds.size() == 1 && audio.sounds[0].params.pan == -1, "a sound on the left wall is all left");
    play(EventType::BoardHit, 40);
    check(audio.sounds[0].params.pan == 1, "and the pan stays within -1 and 1");
    play(EventType::BoardHit, 10);
    check(std::fabs(audio.sounds[0].params.pan - 0.5f) < 1e-6f, "half way along the board is half way over");
}

void testMusic() {
    check(SoundLibrary::musicForLevel("piratos-ostrovok") == "Pirates", "piratos");
    check(SoundLibrary::musicForLevel("Piratos-Chyu-u-dishe") == "Pirates", "...in any case");
    check(SoundLibrary::musicForLevel("cowboys-bank") == "Cowboys", "cowboys");
    check(SoundLibrary::musicForLevel("knight-castle") == "Knights", "knight");
    check(SoundLibrary::musicForLevel("Village-Cafe") == "Village", "village");
    check(SoundLibrary::musicForLevel("mainmenu") == "Menu" && SoundLibrary::musicForLevel("") == "Menu", "anything else");

    RecordingAudio audio;
    SoundLibrary library(audio, catalog(), 1);
    library.playMusicForLevel("piratos-ostrovok");
    check(audio.music.size() == 1 && audio.music[0] == kMusics / "pirates.ogg" && audio.musicLoops &&
              audio.musicFade == SoundLibrary::kMusicFadeSeconds,
          "a level's music loops, with a fade");
    library.playMusicForLevel("piratos-sklep");
    library.playMusic("PIRATES");
    check(audio.music.size() == 1, "the track already playing is left alone");
    library.playMusicForLevel("cowboys-bank", 0.25f);
    check(audio.music.size() == 2 && audio.music[1] == kMusics / "cowboy.ogg" && audio.musicFade == 0.25f,
          "another world crossfades to its track");
    library.stopMusic();
    library.playMusic("Cowboys");
    check(audio.musicStops == 1 && audio.music.size() == 3, "after a stop the same track starts again");

    WarningCapture warnings;
    library.playMusic("jazz");
    library.playMusic("jazz");
    check(warnings.count == 1 && audio.music.size() == 3, "unknown music is warned about once and changes nothing");
}

}  // namespace

int main() {
    testCatalog();
    testPlay();
    testRandomChoice();
    testReplayDelay();
    testPeriodic();
    testLoopsAndMaterials();
    testPlaySounds();
    testMusic();
    if (failures == 0) {
        std::puts("sound tests passed");
    }
    return failures == 0 ? 0 : 1;
}
