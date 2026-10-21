// Checks the converted sounds and music in $MB3_ASSETS: every file decodes and is not silent, the
// music streams and loops, the names materials use resolve, every level has a track, and sounds and
// music come out of the audio device (SDL's disk driver) undistorted. Prints the totals. Skips (exit
// code 77) without the assets.

#include "AudioCapture.h"

#include "mb/engine/Audio.h"
#include "mb/engine/Vorbis.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/SoundLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;
using Clock = std::chrono::steady_clock;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

// Decodes every sample the catalog names.
void checkSamples(const SoundCatalog& catalog) {
    const auto start = Clock::now();
    std::set<fs::path> files;
    for (const SoundDef& def : catalog.sounds()) {
        for (const SoundSample& sample : def.samples) {
            files.insert(sample.file);
        }
    }
    std::size_t bytes = 0;
    double seconds = 0;
    int mono = 0;
    bool streamedMono = false;
    for (const fs::path& file : files) {
        try {
            const auto sound = decodeVorbisFile(file);
            if (sound->channels == 1 && !streamedMono) {
                // A mono file read as a stream is spread over both channels, the same samples as the full decode.
                streamedMono = true;
                auto stream = openVorbisStreamFile(file);
                std::vector<float> frames(2 * sound->frames());
                float worst = 0;
                check(stream->mono() && stream->read(frames.data(), static_cast<int>(sound->frames())) ==
                                            static_cast<int>(sound->frames()),
                      file.filename().string() + " streams as mono");
                for (std::size_t i = 0; i < sound->frames(); ++i) {
                    const float expected = static_cast<float>(sound->samples[i]) / 32768.0f;
                    worst = std::max({worst, std::fabs(frames[2 * i] - expected), std::fabs(frames[2 * i + 1] - expected)});
                }
                check(worst < 1e-4f, file.filename().string() + " streams the decoded samples");
            }
            bytes += sound->samples.size() * sizeof(std::int16_t);
            seconds += static_cast<double>(sound->frames()) / sound->sampleRate;
            mono += sound->channels == 1;
            int peak = 0;
            for (const std::int16_t s : sound->samples) {
                peak = std::max(peak, std::abs(static_cast<int>(s)));
            }
            check(sound->frames() > 0 && peak > 100, file.filename().string() + " is silent or empty");
            check(sound->sampleRate == 44100, file.filename().string() + " is not 44100 Hz");
        } catch (const std::exception& e) {
            check(false, std::string("cannot decode: ") + e.what());
        }
    }
    std::printf("sounds: %zu files, %d mono, %.1f s, %.1f MB decoded, in %.2f s\n", files.size(), mono, seconds,
                static_cast<double>(bytes) / 1e6, since(start));
    check(files.size() > 150, "expected 165 sample files");
}

// Reads a whole stream; returns its frames and peak.
struct StreamStats {
    std::size_t frames = 0;
    float peak = 0;
    std::vector<float> head;  // first 8192 frames
};

StreamStats readAll(FrameSource& stream) {
    StreamStats stats;
    std::vector<float> chunk(2 * 4096);
    for (int n; (n = stream.read(chunk.data(), 4096)) > 0;) {
        for (int i = 0; i < 2 * n; ++i) {
            stats.peak = std::max(stats.peak, std::fabs(chunk[static_cast<std::size_t>(i)]));
        }
        for (int i = 0; i < 2 * n && stats.head.size() < 2 * 8192; ++i) {
            stats.head.push_back(chunk[static_cast<std::size_t>(i)]);
        }
        stats.frames += static_cast<std::size_t>(n);
    }
    return stats;
}

void checkMusic(const fs::path& assets) {
    const SoundCatalog catalog = SoundCatalog::load(assets);
    for (const char* name : {"Menu", "Village", "Pirates", "Cowboys", "Knights"}) {
        const fs::path* file = catalog.findMusic(name);
        check(file != nullptr, std::string("no music named ") + name);
        if (!file) {
            continue;
        }
        const auto start = Clock::now();
        auto stream = openVorbisStreamFile(*file);
        const StreamStats first = readAll(*stream);
        const double seconds = static_cast<double>(first.frames) / stream->sampleRate();
        std::printf("music %-8s %s: %d Hz, %.1f s, peak %.2f, decoded in %.2f s\n", name,
                    file->filename().string().c_str(), stream->sampleRate(), seconds, first.peak, since(start));
        check(seconds > 30 && first.peak > 0.05f, std::string(name) + " plays");
        check(stream->rewind(), std::string(name) + " rewinds");
        const StreamStats second = readAll(*stream);
        check(second.frames == first.frames && second.head == first.head && second.peak == first.peak,
              std::string(name) + " plays the same on the second pass");
    }
}

void checkNames(const fs::path& assets, const SoundCatalog& catalog) {
    const MaterialLibrary materials = MaterialLibrary::load(assets / "Data" / "Materials" / "materials.json");
    std::map<std::string, int> kinds;
    std::set<std::string> unknown;
    for (const GameMaterial& m : materials.all()) {
        for (const std::string* name : {&m.dropSound, &m.periodSound, &m.hitSound, &m.deadSound, &m.loopSound}) {
            if (name->empty()) {
                continue;
            }
            const SoundDef* def = catalog.findSound(*name);
            ++kinds[!def ? "unknown" : def->samples.empty() ? "silent" : "playable"];
            if (!def) {
                unknown.insert(*name);
            }
        }
    }
    std::printf("material sound names: %d playable, %d silent (no file in the game), %d unknown (%zu distinct)\n",
                kinds["playable"], kinds["silent"], kinds["unknown"], unknown.size());
    check(kinds["playable"] > 300, "most material sounds should resolve");

    LevelCatalog levels(assets / "Data" / "Levels");
    std::map<std::string, int> worlds;
    for (const LevelEntry& level : levels.levels()) {
        const std::string track(SoundLibrary::musicForLevel(level.name));
        ++worlds[track];
        check(catalog.findMusic(track) != nullptr, level.name + " has no track");
    }
    std::printf("levels per track:");
    for (const auto& [track, count] : worlds) {
        std::printf(" %s %d", track.c_str(), count);
    }
    std::printf("\n");
    check(worlds["Menu"] == 0 && levels.levels().size() == 83, "every level belongs to a world");
}

// A sound through SoundLibrary, Audio and the disk driver comes out as the decoded samples.
void checkSoundOnDevice(const fs::path& assets, const fs::path& scratch) {
    const SoundCatalog probe = SoundCatalog::load(assets);
    const SoundDef* def = probe.findSound("MenuStoneIn");
    check(def && def->samples.size() == 1, "MenuStoneIn");
    if (!def || def->samples.size() != 1) {
        return;
    }
    const auto reference = decodeVorbisFile(def->samples[0].file);

    const test::Capture capture = test::captureAudio(
        scratch,
        [&](Audio& audio) {
            SoundLibrary library(audio, SoundCatalog::load(assets));
            library.play("MenuStoneIn");
        },
        [](Audio& audio) { return audio.mixer().idle(); });
    check(capture.deviceOpened, "device");
    const std::size_t begin = capture.firstSound(), end = capture.endOfSound();
    // A mono sound in the centre is 0.7071 on each side (constant power); a stereo one is untouched.
    const std::size_t channels = static_cast<std::size_t>(reference->channels);
    const float scale = (channels == 1 ? std::sqrt(0.5f) : 1.0f) / 32768.0f;
    const auto sampleAt = [&](std::size_t frame, std::size_t channel) {
        return static_cast<float>(reference->samples[frame * channels + std::min(channel, channels - 1)]);
    };
    // The file may begin and end with digital silence, which the capture cannot tell from the rest.
    std::size_t refBegin = 0, refEnd = reference->frames();
    while (refBegin < refEnd && sampleAt(refBegin, 0) == 0 && sampleAt(refBegin, 1) == 0) {
        ++refBegin;
    }
    while (refEnd > refBegin && sampleAt(refEnd - 1, 0) == 0 && sampleAt(refEnd - 1, 1) == 0) {
        --refEnd;
    }
    float worst = 0;
    for (std::size_t i = 0; i < refEnd - refBegin && begin + i < capture.frames(); ++i) {
        worst = std::max({worst, std::fabs(capture.left(begin + i) - sampleAt(refBegin + i, 0) * scale),
                          std::fabs(capture.right(begin + i) - sampleAt(refBegin + i, 1) * scale)});
    }
    std::printf("MenuStoneIn on the device: %zu frames (decoded %zu), peak %.3f, worst error %.6f\n", end - begin,
                refEnd - refBegin, capture.peak(), worst);
    check(end - begin == refEnd - refBegin, "duration on the device");
    check(worst < 2e-4f, "the device gets the decoded samples, undistorted");
}

// Music through Audio and the disk driver: fades in, then matches the stream.
void checkMusicOnDevice(const fs::path& assets, const fs::path& scratch) {
    const SoundCatalog catalog = SoundCatalog::load(assets);
    const fs::path file = *catalog.findMusic("Menu");
    constexpr double kFade = 0.5, kSeconds = 2.0;
    const auto start = Clock::now();
    const test::Capture capture = test::captureAudio(
        scratch, [&](Audio& audio) { audio.playMusic(file, static_cast<float>(kFade), true); },
        [&](Audio&) { return since(start) > kSeconds; });
    check(capture.deviceOpened, "device for music");
    const std::size_t begin = capture.firstSound();
    check(capture.frames() > begin + 44100, "a second or more of music");

    auto stream = openVorbisStreamFile(file);
    std::vector<float> reference(2 * 66150);  // 1.5 s
    check(stream->read(reference.data(), 66150) == 66150, "reference");

    // Past the fade (0.5 s), the device gets the stream's samples.
    float worst = 0, rmsStart = 0, rmsLater = 0;
    int compared = 0;
    for (std::size_t j = 22100 + 2000; j < 66150 && begin + j < capture.frames(); ++j) {
        for (std::size_t c = 0; c < 2; ++c) {
            const float expected = reference[2 * j + c];
            if (std::fabs(expected) < 0.79f) {  // the limiter leaves quieter samples alone
                worst = std::max(worst, std::fabs(capture.samples[2 * (begin + j) + c] - expected));
                ++compared;
            }
        }
    }
    for (std::size_t j = 0; j < 4410; ++j) {
        rmsStart += capture.left(begin + j) * capture.left(begin + j);
        rmsLater += capture.left(begin + 30000 + j) * capture.left(begin + 30000 + j);
    }
    std::printf("Menu music on the device: %zu frames, peak %.2f, fade-in rms %.4f -> %.4f, worst error after the fade %.5f (%d samples)\n",
                capture.frames(), capture.peak(), std::sqrt(rmsStart / 4410), std::sqrt(rmsLater / 4410), worst, compared);
    check(rmsLater > 0 && rmsStart < rmsLater * 0.5f, "the music fades in");
    check(compared > 50000 && worst < 2e-4f, "music arrives undistorted");
    check(capture.peak() <= 1.0f, "never clips");
}

}  // namespace

int main(int argc, char** argv) {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path assets = env;
    if (!fs::is_regular_file(assets / "Data" / "xml" / "sounds.json") || !fs::is_directory(assets / "Sounds")) {
        std::printf("SKIP: converted sounds not found under %s (run tools/convert_assets.py)\n", assets.string().c_str());
        return 77;
    }
    const fs::path scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "mb3-audio-smoke";
    try {
        const SoundCatalog catalog = SoundCatalog::load(assets);
        std::printf("catalog: %zu sounds, %zu musics\n", catalog.sounds().size(), catalog.musicCount());
        check(catalog.sounds().size() >= 285 && catalog.musicCount() == 5, "catalog size");
        checkSamples(catalog);
        checkMusic(assets);
        checkNames(assets, catalog);
        checkSoundOnDevice(assets, scratch);
        checkMusicOnDevice(assets, scratch);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("audio smoke passed");
    }
    return failures == 0 ? 0 : 1;
}
