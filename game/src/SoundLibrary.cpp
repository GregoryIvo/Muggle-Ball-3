#include "mb/game/SoundLibrary.h"

#include "StringUtil.h"

#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error(std::format("cannot open {} (run tools/convert_assets.py)", file.string()));
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void checkVersion(const json& root) {
    if (const int version = root.value("version", 1); version != 1) {
        throw std::runtime_error(std::format("unsupported version {}", version));
    }
}

SoundDef parseSound(const json& j, const fs::path& soundsDir) {
    SoundDef def;
    def.name = j.at("name").get<std::string>();
    for (const json& s : j.at("samples")) {
        SoundSample sample;
        sample.file = soundsDir / s.at("file").get<std::string>();
        sample.delayMs = s.value("delayMs", 0);
        const float a = s.value("freqStart", 1.0f), b = s.value("freqEnd", 1.0f);
        sample.pitchMin = std::min(a, b);
        sample.pitchMax = std::max(a, b);
        def.samples.push_back(std::move(sample));
    }
    return def;
}

}  // namespace

SoundCatalog SoundCatalog::load(const fs::path& assetRoot) {
    const fs::path xml = assetRoot / "Data" / "xml";
    return parse(readText(xml / "sounds.json"), readText(xml / "musics.json"), assetRoot / "Sounds",
                 assetRoot / "Musics");
}

SoundCatalog SoundCatalog::parse(std::string_view soundsJson, std::string_view musicsJson, const fs::path& soundsDir,
                                 const fs::path& musicsDir) {
    SoundCatalog catalog;
    try {
        const json sounds = json::parse(soundsJson);
        checkVersion(sounds);
        for (const json& entry : sounds.at("sounds")) {
            SoundDef def = parseSound(entry, soundsDir);
            // emplace keeps the first definition of a repeated name.
            catalog.soundsByName_.emplace(lowerAscii(def.name), catalog.sounds_.size());
            catalog.sounds_.push_back(std::move(def));
        }
        const json musics = json::parse(musicsJson);
        checkVersion(musics);
        for (const json& entry : musics.at("musics")) {
            std::string name = entry.at("name").get<std::string>();
            catalog.musicsByName_.emplace(lowerAscii(name), catalog.musics_.size());
            catalog.musics_.emplace_back(std::move(name), musicsDir / entry.at("file").get<std::string>());
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid sound JSON: {}", e.what()));
    }
    return catalog;
}

const SoundDef* SoundCatalog::findSound(std::string_view name) const {
    const auto it = soundsByName_.find(lowerAscii(name));
    return it == soundsByName_.end() ? nullptr : &sounds_[it->second];
}

const fs::path* SoundCatalog::findMusic(std::string_view name) const {
    const auto it = musicsByName_.find(lowerAscii(name));
    return it == musicsByName_.end() ? nullptr : &musics_[it->second].second;
}

SoundLibrary::SoundLibrary(AudioPlayer& audio, SoundCatalog catalog, std::uint32_t seed)
    : audio_(audio), catalog_(std::move(catalog)), random_(seed) {}

float SoundLibrary::random01() {
    // 24 bits fill a float's mantissa exactly. (The standard distributions differ between libraries.)
    return static_cast<float>(random_() >> 8) * (1.0f / 16777216.0f);
}

const SoundSample& SoundLibrary::pick(const SoundDef& def) {
    return def.samples[random_() % def.samples.size()];
}

const SoundDef* SoundLibrary::lookup(std::string_view name) {
    const SoundDef* def = catalog_.findSound(name);
    if (!def && !name.empty() && warned_.insert(lowerAscii(name)).second) {
        logWarn("unknown sound \"{}\"", name);
    }
    return def;
}

VoiceId SoundLibrary::start(const SoundSample& sample, float gain, float pan, bool loop) {
    VoiceParams params;
    params.gain = gain;
    params.pan = pan;
    params.pitch = sample.pitchMin + (sample.pitchMax - sample.pitchMin) * random01();
    params.loop = loop;
    return audio_.playSound(sample.file, params);
}

void SoundLibrary::update(double seconds) {
    now_ += std::max(0.0, seconds);
    for (auto& [id, p] : periodic_) {
        if (now_ >= p.due) {
            const SoundSample& sample = pick(*p.def);
            start(sample, p.gain, p.pan, false);
            p.due = now_ + sample.delayMs / 1000.0;
        }
    }
}

VoiceId SoundLibrary::play(std::string_view name, float gain, float pan) {
    const SoundDef* def = lookup(name);
    if (!def || def->samples.empty()) {
        return 0;
    }
    if (const auto it = readyAt_.find(def); it != readyAt_.end() && now_ < it->second) {
        return 0;
    }
    const SoundSample& sample = pick(*def);
    if (sample.delayMs > 0) {
        readyAt_[def] = now_ + sample.delayMs / 1000.0;
    }
    return start(sample, gain, pan, false);
}

VoiceId SoundLibrary::play(const GameMaterial& material, MaterialSound which, float gain, float pan) {
    switch (which) {
        case MaterialSound::Drop: return play(material.dropSound, gain, pan);
        case MaterialSound::Hit: return play(material.hitSound, gain, pan);
        case MaterialSound::Dead: return play(material.deadSound, gain, pan);
    }
    return 0;
}

VoiceId SoundLibrary::startLoop(std::string_view name, float gain, float pan) {
    const SoundDef* def = lookup(name);
    return def && !def->samples.empty() ? start(pick(*def), gain, pan, true) : 0;
}

VoiceId SoundLibrary::startLoop(const GameMaterial& material, float gain, float pan) {
    return startLoop(material.loopSound, gain, pan);
}

void SoundLibrary::stopLoop(VoiceId voice) { audio_.stopSound(voice); }

PeriodicId SoundLibrary::startPeriodic(std::string_view name, float gain, float pan) {
    const SoundDef* def = lookup(name);
    if (!def || def->samples.empty()) {
        return 0;
    }
    if (std::ranges::any_of(def->samples, [](const SoundSample& s) { return s.delayMs <= 0; })) {
        if (warned_.insert(lowerAscii(name)).second) {
            logWarn("sound \"{}\" has no delay, so it cannot repeat", name);
        }
        return 0;
    }
    const PeriodicId id = nextPeriodic_++;
    periodic_[id] = {def, gain, pan, now_ + static_cast<double>(random01()) * pick(*def).delayMs / 1000.0};
    return id;
}

PeriodicId SoundLibrary::startPeriodic(const GameMaterial& material, float gain, float pan) {
    return startPeriodic(material.periodSound, gain, pan);
}

void SoundLibrary::stopPeriodic(PeriodicId id) { periodic_.erase(id); }

void SoundLibrary::playMusic(std::string_view name, float fadeSeconds) {
    const std::string key = lowerAscii(name);
    if (key == music_) {
        return;
    }
    const fs::path* file = catalog_.findMusic(name);
    if (!file) {
        if (warned_.insert("music:" + key).second) {
            logWarn("unknown music \"{}\"", name);
        }
        return;
    }
    audio_.playMusic(*file, fadeSeconds, true);
    music_ = key;
    logInfo("Music: {}", name);
}

void SoundLibrary::playMusicForLevel(std::string_view levelName, float fadeSeconds) {
    playMusic(musicForLevel(levelName), fadeSeconds);
}

void SoundLibrary::stopMusic(float fadeSeconds) {
    audio_.stopMusic(fadeSeconds);
    music_.clear();
}

std::string_view SoundLibrary::musicForLevel(std::string_view levelName) {
    const std::string name = lowerAscii(levelName);
    if (name.starts_with("piratos")) {
        return "Pirates";
    }
    if (name.starts_with("cowboys")) {
        return "Cowboys";
    }
    if (name.starts_with("knight")) {
        return "Knights";
    }
    if (name.starts_with("village")) {
        return "Village";
    }
    return "Menu";
}

}  // namespace mb
