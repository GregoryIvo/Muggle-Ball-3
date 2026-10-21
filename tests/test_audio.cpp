// Ogg Vorbis decoding and the Audio device, against a short synthetic sine wave (tests/data) and
// SDL's disk audio driver. No sound card and no game data needed.

#include "AudioCapture.h"

#include "mb/engine/Audio.h"
#include "mb/engine/Vorbis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
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

// tests/data/sine440-stereo.ogg: 0.1 s of a 440 Hz sine in both channels at 22050 Hz, peak about 0.36,
// made with: ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=0.1,volume=4"
//                   -ac 2 -c:a vorbis -strict -2 -b:a 64k sine440-stereo.ogg
const fs::path kSine = fs::path(MB3_TEST_DATA_DIR) / "sine440-stereo.ogg";

bool throws(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// Sign changes in the left channel of interleaved stereo float frames.
int zeroCrossings(const std::vector<float>& stereo, std::size_t first, std::size_t end) {
    int crossings = 0;
    for (std::size_t i = first + 1; i < end; ++i) {
        crossings += (stereo[2 * i] < 0) != (stereo[2 * (i - 1)] < 0);
    }
    return crossings;
}

void testDecode() {
    const auto sound = decodeVorbisFile(kSine);
    check(sound->channels == 2 && sound->sampleRate == 22050, "format");
    const std::size_t frames = sound->frames();
    check(frames >= 2205 && frames <= 3200, "about a tenth of a second");
    int peak = 0;
    bool same = true;
    for (std::size_t i = 0; i < frames; ++i) {
        peak = std::max(peak, std::abs(static_cast<int>(sound->samples[2 * i])));
        same = same && sound->samples[2 * i] == sound->samples[2 * i + 1];
    }
    const float level = static_cast<float>(peak) / 32768.0f;
    check(level > 0.3f && level < 0.4f, "the sine's level");
    check(same, "both channels carry the same wave");
    std::vector<float> floats;
    for (const std::int16_t s : sound->samples) {
        floats.push_back(static_cast<float>(s));
    }
    const int crossings = zeroCrossings(floats, 0, frames);
    check(crossings >= 80 && crossings <= 96, "440 Hz: about 88 zero crossings in 0.1 s");
}

void testStream() {
    const auto sound = decodeVorbisFile(kSine);
    const std::size_t frames = sound->frames();
    auto stream = openVorbisStreamFile(kSine);
    check(stream->sampleRate() == 22050 && !stream->mono(), "stream format");

    std::vector<float> first(2 * (frames + 64));
    int got = 0;
    for (int n; (n = stream->read(first.data() + 2 * got, 100)) > 0;) {
        got += n;
    }
    check(static_cast<std::size_t>(got) == frames, "the stream is as long as the full decode");
    float worst = 0;
    for (std::size_t i = 0; i < 2 * frames; ++i) {
        worst = std::max(worst, std::fabs(first[i] - static_cast<float>(sound->samples[i]) / 32768.0f));
    }
    check(worst < 1e-4f, "and holds the same samples");
    check(stream->read(first.data(), 10) == 0, "the end stays the end");

    check(stream->rewind(), "rewinds");
    std::vector<float> again(first.size());
    got = 0;
    for (int n; (n = stream->read(again.data() + 2 * got, 333)) > 0;) {
        got += n;
    }
    check(static_cast<std::size_t>(got) == frames &&
              std::equal(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(2 * frames), again.begin()),
          "a second pass is identical, whatever the read size");
}

void testLoopedMusicThroughTheMixer() {
    // The stream looped by the mixer repeats the decode exactly: no gap, no click at the seam.
    const auto sound = decodeVorbisFile(kSine);
    const int frames = static_cast<int>(sound->frames());
    Mixer mixer(22050);
    mixer.playMusic(openVorbisStreamFile(kSine), 0, true);
    std::vector<float> out(2 * static_cast<std::size_t>(frames) * 3);
    mixer.render(out.data(), frames * 3);
    float worst = 0;
    for (int i = 0; i < frames * 3; ++i) {
        const std::size_t k = static_cast<std::size_t>(i % frames);
        worst = std::max(worst, std::fabs(out[2 * static_cast<std::size_t>(i)] - static_cast<float>(sound->samples[2 * k]) / 32768.0f));
    }
    check(worst < 1e-4f, "a looped stream is the decode repeated");
}

void testBadData() {
    check(throws([] { decodeVorbisFile("/nonexistent/sound.ogg"); }), "missing file");
    check(throws([] { openVorbisStreamFile("/nonexistent/music.ogg"); }), "missing music file");
    check(throws([] { decodeVorbis(std::vector<std::uint8_t>(100, 0x42)); }), "garbage is not Vorbis");
    check(throws([] { openVorbisStream(std::vector<std::uint8_t>(100, 0x42)); }), "garbage is not a stream");
    check(throws([] { openVorbisStream({}); }), "nothing is not a stream");
    std::vector<std::uint8_t> head;
    {
        std::ifstream in(kSine, std::ios::binary);
        head.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        head.resize(1000);  // cut inside the headers
    }
    check(throws([&] { decodeVorbis(head); }), "a file cut inside its headers");
    try {
        decodeVorbisFile("/nonexistent/sound.ogg");
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()).find("/nonexistent/sound.ogg") != std::string::npos, "errors name the file");
    }
}

void testNoDevice() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "no-such-driver");
    {
        Audio audio;
        check(!audio.available(), "no audio driver, no device");
        check(audio.playSound(kSine, {}) == 0, "playing a sound does nothing");
        audio.playMusic(kSine, 1, true);
        audio.stopMusic(1);
        audio.stopSound(5);
        audio.mixer().setMasterVolume(0.5f);
        check(audio.mixer().idle() && !audio.mixer().musicPlaying(), "and nothing plays");
    }
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}

void testSoundOnTheDevice(const fs::path& scratch) {
    VoiceId voice = 0;
    const test::Capture capture = test::captureAudio(
        scratch,
        [&](Audio& audio) {
            VoiceParams params;
            params.pan = -1;  // hard left
            voice = audio.playSound(kSine, params);
        },
        [](Audio& audio) { return audio.mixer().idle(); });
    check(capture.deviceOpened, "the disk driver opens");
    check(voice != 0, "the sound started");
    const std::size_t begin = capture.firstSound(), end = capture.endOfSound();
    // 22050 Hz sound on a 44100 Hz mixer: twice the frames, and twice 0.1 s of 440 Hz cycles is the same cycles.
    const auto sound = decodeVorbisFile(kSine);
    const double seconds = static_cast<double>(end - begin) / 44100.0;
    check(seconds > 0.09 && seconds < 0.08 + static_cast<double>(sound->frames()) / 22050.0, "duration");
    check(capture.peak() > 0.3f && capture.peak() < 0.4f, "level");
    float rightPeak = 0;
    for (std::size_t i = begin; i < end; ++i) {
        rightPeak = std::max(rightPeak, std::fabs(capture.right(i)));
    }
    check(rightPeak == 0, "panned hard left");
    const int crossings = zeroCrossings(capture.samples, begin, end);
    check(crossings >= 80 && crossings <= 96, "pitch: 440 Hz");
    check(capture.samples.size() % 2 == 0 && begin < capture.frames(), "stereo frames");
}

void testMusicOnTheDevice(const fs::path& scratch) {
    const test::Capture capture = test::captureAudio(
        scratch, [](Audio& audio) { audio.playMusic(kSine, 0, false); },
        [](Audio& audio) { return !audio.mixer().musicPlaying(); });
    check(capture.deviceOpened, "device for music");
    const std::size_t begin = capture.firstSound(), end = capture.endOfSound();
    check(end > begin && capture.peak() > 0.3f && capture.peak() < 0.4f, "music reaches the device");
    const int crossings = zeroCrossings(capture.samples, begin, end);
    check(crossings >= 80 && crossings <= 96, "music pitch");
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / ("mb3-audio-test-" + std::to_string(std::random_device{}()));
    testDecode();
    testStream();
    testLoopedMusicThroughTheMixer();
    testBadData();
    testNoDevice();
    testSoundOnTheDevice(scratch);
    testMusicOnTheDevice(scratch);
    if (failures == 0) {
        std::puts("audio tests passed");
    }
    return failures == 0 ? 0 : 1;
}
