#pragma once

#include "mb/engine/Audio.h"
#include "mb/game/MaterialLibrary.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mb {

// One <Sample> of Data/xml/sounds.xml, converted to Data/xml/sounds.json by tools/sounds.py
// (schema: docs/ASSET_FORMATS.md).
struct SoundSample {
    std::filesystem::path file;  // the .ogg, in full
    int delayMs = 0;             // how long after it starts the sound may play again; 0 means no limit
    float pitchMin = 1, pitchMax = 1;  // playback speed is drawn from this range for each play
};

struct SoundDef {
    std::string name;  // as sounds.xml spells it
    // One is picked at random. Empty when the file the XML names is not in the game's data; the
    // original plays nothing then, and so does the library (without a warning).
    std::vector<SoundSample> samples;
};

// The sound and music definitions: sounds.json and musics.json.
class SoundCatalog {
public:
    static SoundCatalog load(const std::filesystem::path& assetRoot);  // throws on error
    static SoundCatalog parse(std::string_view soundsJson, std::string_view musicsJson,
                              const std::filesystem::path& soundsDir, const std::filesystem::path& musicsDir);

    // Case-insensitive ("MenuStoneDown" in sounds.xml, "menustonedown" in the menu files); when a
    // name is defined twice the first definition wins. Null if the name is unknown.
    const SoundDef* findSound(std::string_view name) const;
    const std::filesystem::path* findMusic(std::string_view name) const;

    const std::vector<SoundDef>& sounds() const { return sounds_; }
    std::size_t musicCount() const { return musics_.size(); }

private:
    std::vector<SoundDef> sounds_;
    std::unordered_map<std::string, std::size_t> soundsByName_;  // lowercase name -> index
    std::vector<std::pair<std::string, std::filesystem::path>> musics_;
    std::unordered_map<std::string, std::size_t> musicsByName_;
};

enum class MaterialSound { Drop, Hit, Dead };

using PeriodicId = std::uint32_t;  // 0 is none

// Plays the game's named sounds and music through an AudioPlayer.
//
// A sound's replay delay applies to the sound as a whole: after a sample starts, play() ignores the
// sound until that sample's delay has passed, so a pile of bricks breaking in one frame is one crash
// rather than twenty. Time is the game's: call update() every frame.
class SoundLibrary {
public:
    static constexpr float kMusicFadeSeconds = 1.5f;

    // `seed` fixes the random choices (which sample, which pitch) for tests.
    SoundLibrary(AudioPlayer& audio, SoundCatalog catalog, std::uint32_t seed = std::random_device{}());

    const SoundCatalog& catalog() const { return catalog_; }

    // Advances the clock that replay delays and periodic sounds run on, by the seconds since the last call.
    void update(double seconds);

    // Plays one random sample of the sound at a random pitch from its range. `gain` is a linear
    // amplitude and `pan` runs from -1 (left) to 1 (right). Returns the voice, or 0 when nothing
    // played: an unknown name (warned once), a sound without a file, or one inside its replay delay.
    VoiceId play(std::string_view name, float gain = 1, float pan = 0);
    // The sound a material names for the event, if it names one.
    VoiceId play(const GameMaterial& material, MaterialSound which, float gain = 1, float pan = 0);

    // A sound that repeats until stopped (a material's loopSound). Not limited by the replay delay.
    VoiceId startLoop(std::string_view name, float gain = 1, float pan = 0);
    VoiceId startLoop(const GameMaterial& material, float gain = 1, float pan = 0);
    void stopLoop(VoiceId voice);

    // An object's idle sound (a material's periodSound): plays now and then, each time waiting the
    // delay of the sample just played. Every object has its own timer and starts at a random point
    // of it, so a level full of sheep does not bleat in chorus. Returns 0 if the sound cannot repeat
    // (unknown, no file, or samples without a delay).
    PeriodicId startPeriodic(std::string_view name, float gain = 1, float pan = 0);
    PeriodicId startPeriodic(const GameMaterial& material, float gain = 1, float pan = 0);
    void stopPeriodic(PeriodicId id);

    // Music by name (Menu, Village, Pirates, Cowboys, Knights), looping, crossfaded from the track
    // playing. Asking for the track already playing changes nothing.
    void playMusic(std::string_view name, float fadeSeconds = kMusicFadeSeconds);
    // The music of a level's world, from the level's file name: piratos-... is Pirates, cowboys-...
    // Cowboys, knight-... Knights, village-... Village; anything else Menu.
    void playMusicForLevel(std::string_view levelName, float fadeSeconds = kMusicFadeSeconds);
    void stopMusic(float fadeSeconds = kMusicFadeSeconds);
    static std::string_view musicForLevel(std::string_view levelName);
    // The track asked for last, in lower case ("menu", "pirates"); empty before the first and after stopMusic().
    const std::string& music() const { return music_; }

private:
    struct Periodic {
        const SoundDef* def;
        float gain, pan;
        double due;  // on the update() clock
    };

    const SoundDef* lookup(std::string_view name);  // warns once about a name that is not defined
    const SoundSample& pick(const SoundDef& def);
    float random01();
    VoiceId start(const SoundSample& sample, float gain, float pan, bool loop);

    AudioPlayer& audio_;
    SoundCatalog catalog_;
    std::mt19937 random_;
    double now_ = 0;
    std::unordered_map<const SoundDef*, double> readyAt_;  // when a sound may play again
    std::map<PeriodicId, Periodic> periodic_;
    PeriodicId nextPeriodic_ = 1;
    std::unordered_set<std::string> warned_;
    std::string music_;  // lowercase name of the track playing
};

}  // namespace mb
