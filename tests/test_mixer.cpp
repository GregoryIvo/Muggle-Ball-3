// Mixer tests: gain, pan, pitch and resampling, soft limiting, voice stealing, looping, music
// crossfades, pause and threading, all on synthetic PCM rendered offline. No audio device, no game data.

#include "mb/engine/Mixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <thread>
#include <utility>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }

constexpr int kRate = 44100;

// Quantised the way the mixer reads it: 16-bit PCM scaled by 1/32768.
std::int16_t pcm(float value) { return static_cast<std::int16_t>(std::lround(value * 32768.0f)); }

// A stereo sound made of (left, right) pairs.
std::shared_ptr<SoundData> stereo(const std::vector<std::pair<float, float>>& frames, int rate = kRate) {
    auto sound = std::make_shared<SoundData>();
    sound->channels = 2;
    sound->sampleRate = rate;
    for (const auto& [l, r] : frames) {
        sound->samples.push_back(pcm(l));
        sound->samples.push_back(pcm(r));
    }
    return sound;
}

// The same value in both channels, repeated.
std::shared_ptr<SoundData> constant(float value, int frames, int rate = kRate) {
    return stereo(std::vector<std::pair<float, float>>(static_cast<std::size_t>(frames), {value, value}), rate);
}

std::shared_ptr<SoundData> monoConstant(float value, int frames) {
    auto sound = std::make_shared<SoundData>();
    sound->channels = 1;
    sound->sampleRate = kRate;
    sound->samples.assign(static_cast<std::size_t>(frames), pcm(value));
    return sound;
}

// Frame k holds k / 32768 in both channels: a straight line, which linear resampling reproduces exactly.
std::shared_ptr<SoundData> line(int frames, int rate = kRate) {
    auto sound = std::make_shared<SoundData>();
    sound->channels = 2;
    sound->sampleRate = rate;
    for (int k = 0; k < frames; ++k) {
        sound->samples.push_back(static_cast<std::int16_t>(k));
        sound->samples.push_back(static_cast<std::int16_t>(k));
    }
    return sound;
}

std::vector<float> render(Mixer& mixer, int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    mixer.render(out.data(), frames);
    return out;
}

float left(const std::vector<float>& out, int frame) { return out[2 * static_cast<std::size_t>(frame)]; }
float right(const std::vector<float>& out, int frame) { return out[2 * static_cast<std::size_t>(frame) + 1]; }

// A source that counts up forever, to show music streams just like sounds.
class CountingSource final : public FrameSource {
public:
    CountingSource(int length, int rate) : length_(length), rate_(rate) {}
    int sampleRate() const override { return rate_; }
    bool mono() const override { return false; }
    int read(float* stereo, int frames) override {
        int n = 0;
        for (; n < frames && position_ < length_; ++n, ++position_) {
            stereo[2 * n] = stereo[2 * n + 1] = static_cast<float>(position_ + 1) / 100.0f;
        }
        return n;
    }
    bool rewind() override {
        position_ = 0;
        return true;
    }

private:
    int length_, rate_, position_ = 0;
};

// ---------------------------------------------------------------------------

void testGainAndStereo() {
    Mixer mixer(kRate);
    VoiceParams params;
    params.gain = 0.5f;
    mixer.play(stereo({{0.5f, -0.25f}, {0.5f, -0.25f}}), params);
    const auto out = render(mixer, 4);
    check(near(left(out, 0), 0.25f) && near(right(out, 0), -0.125f), "gain scales a stereo sound, channels stay apart");
    check(near(left(out, 1), 0.25f) && near(right(out, 1), -0.125f), "second frame");
    check(left(out, 2) == 0 && right(out, 3) == 0, "silence after the sound ends");
    check(mixer.idle(), "no voice left after the sound ends");
}

void testPan() {
    const float centre = std::cos(std::numbers::pi_v<float> / 4);
    for (const float pan : {-1.0f, -0.5f, 0.0f, 0.3f, 1.0f}) {
        Mixer mixer(kRate);
        VoiceParams params;
        params.pan = pan;
        mixer.play(monoConstant(0.5f, 8), params);
        const auto out = render(mixer, 1);
        // Constant power: the two sides always carry the sound's full power.
        check(near(left(out, 0) * left(out, 0) + right(out, 0) * right(out, 0), 0.25f), "mono pan keeps constant power");
        if (pan == -1.0f) {
            check(near(left(out, 0), 0.5f) && near(right(out, 0), 0), "mono hard left");
        } else if (pan == 1.0f) {
            check(near(left(out, 0), 0) && near(right(out, 0), 0.5f), "mono hard right");
        } else if (pan == 0.0f) {
            check(near(left(out, 0), 0.5f * centre) && near(right(out, 0), 0.5f * centre), "mono centre is -3 dB a side");
        }
    }
    {
        // A stereo sound pans as a balance: the near side keeps its level, the far side fades.
        Mixer mixer(kRate);
        VoiceParams params;
        params.pan = -0.5f;
        mixer.play(constant(0.5f, 8), params);
        const auto out = render(mixer, 1);
        check(near(left(out, 0), 0.5f) && near(right(out, 0), 0.25f), "stereo balance");
    }
    {
        Mixer mixer(kRate);
        VoiceParams params;
        params.pan = 7;  // out of range clamps
        mixer.play(monoConstant(0.5f, 8), params);
        const auto out = render(mixer, 1);
        check(near(left(out, 0), 0) && near(right(out, 0), 0.5f), "pan clamps to hard right");
    }
}

void testPitchAndResampling() {
    {
        Mixer mixer(kRate);
        VoiceParams params;
        params.pitch = 2;
        mixer.play(line(100), params);
        const auto out = render(mixer, 80);
        bool exact = true;
        for (int i = 0; i < 50; ++i) {
            exact = exact && near(left(out, i), static_cast<float>(2 * i) / 32768.0f);
        }
        check(exact, "pitch 2 reads every other frame");
        check(left(out, 50) == 0 && mixer.idle(), "an octave up lasts half as long");
    }
    {
        Mixer mixer(kRate);
        VoiceParams params;
        params.pitch = 0.5f;
        mixer.play(line(100), params);
        const auto out = render(mixer, 220);
        bool exact = true;
        for (int i = 0; i < 198; ++i) {
            exact = exact && near(left(out, i), static_cast<float>(i) * 0.5f / 32768.0f);
        }
        check(exact, "pitch 0.5 interpolates between frames");
        check(mixer.idle() && left(out, 210) == 0, "an octave down lasts twice as long");
    }
    {
        // A sound recorded at half the mixer's rate plays at the right speed.
        Mixer mixer(kRate);
        mixer.play(line(100, kRate / 2));
        const auto out = render(mixer, 220);
        check(near(left(out, 20), 10.0f / 32768.0f) && near(left(out, 21), 10.5f / 32768.0f), "22 kHz sound upsampled");
        check(left(out, 205) == 0, "and takes twice as many frames");
    }
    {
        // The position stays exact across many blocks: 1000 frames at 1.5x.
        Mixer mixer(kRate);
        VoiceParams params;
        params.pitch = 1.5f;
        mixer.play(line(20000), params);
        const auto out = render(mixer, 1000);
        float worst = 0;
        for (int i = 0; i < 1000; ++i) {
            worst = std::max(worst, std::fabs(left(out, i) - static_cast<float>(i) * 1.5f / 32768.0f));
        }
        check(worst < 1e-6f, "no drift at pitch 1.5 over several blocks");
    }
    {
        Mixer mixer(kRate);
        VoiceParams params;
        params.pitch = 1000;  // clamped to the maximum, so the sound is not skipped over
        mixer.play(line(4000), params);
        const auto out = render(mixer, 4);
        check(near(left(out, 1), 4.0f / 32768.0f), "pitch clamps to the maximum");
    }
}

void testSoftLimiter() {
    {
        Mixer mixer(kRate);
        mixer.play(constant(0.5f, 16));
        check(left(render(mixer, 1), 0) == 0.5f, "a normal level passes through untouched");
    }
    {
        // Ten voices at 0.5 would sum to 5; the output stays below 1 and is not squashed to nothing.
        Mixer mixer(kRate);
        for (int i = 0; i < 10; ++i) {
            mixer.play(constant(0.5f, 16));
        }
        const auto out = render(mixer, 8);
        check(std::ranges::all_of(out, [](float s) { return s <= 1.0f; }), "output never exceeds full scale");
        check(left(out, 0) > 0.95f, "and stays loud");
    }
    {
        // Monotonic and continuous across the knee.
        float previous = 0;
        bool monotonic = true, bounded = true, smooth = true;
        for (int step = 1; step <= 400; ++step) {
            Mixer mixer(kRate);
            VoiceParams params;
            params.gain = static_cast<float>(step) * 0.01f;
            mixer.play(constant(0.9999f, 4), params);
            const float value = left(render(mixer, 1), 0);
            monotonic = monotonic && value >= previous;
            bounded = bounded && value <= 1.0f;
            smooth = smooth && value - previous < 0.011f;
            previous = value;
        }
        check(monotonic && bounded && smooth, "limiter is monotonic, bounded and continuous");
    }
    {
        Mixer mixer(kRate);
        mixer.play(constant(-0.9999f, 16));
        VoiceParams params;
        params.gain = 4;
        mixer.play(constant(-0.9999f, 16), params);
        const auto out = render(mixer, 1);
        check(left(out, 0) >= -1.0f && left(out, 0) < -0.95f, "negative peaks are limited too");
    }
}

void testVoiceStealing() {
    VoiceParams loud;
    {
        // The 5th voice cuts the oldest of equals.
        Mixer mixer(kRate, 4);
        VoiceId ids[5];
        for (VoiceId& id : ids) {
            id = mixer.play(constant(0.1f, 1000), loud);
        }
        render(mixer, 4);
        check(!mixer.isPlaying(ids[0]) && mixer.isPlaying(ids[1]) && mixer.isPlaying(ids[4]), "oldest voice cut");
        check(mixer.activeVoices() == 4, "never more than maxVoices");
    }
    {
        // It cuts the quietest first.
        Mixer mixer(kRate, 4);
        const float gains[4] = {0.9f, 0.3f, 0.7f, 0.8f};
        VoiceId ids[4];
        for (int i = 0; i < 4; ++i) {
            VoiceParams p;
            p.gain = gains[i];
            ids[i] = mixer.play(constant(0.1f, 1000), p);
        }
        VoiceParams p;
        p.gain = 0.6f;
        const VoiceId fresh = mixer.play(constant(0.1f, 1000), p);
        render(mixer, 4);
        check(!mixer.isPlaying(ids[1]) && mixer.isPlaying(ids[0]) && mixer.isPlaying(ids[2]) &&
                  mixer.isPlaying(ids[3]) && mixer.isPlaying(fresh),
              "quietest voice cut");
    }
    {
        // A newcomer quieter than everything playing is dropped instead.
        Mixer mixer(kRate, 2);
        const VoiceId a = mixer.play(constant(0.1f, 1000), loud);
        const VoiceId b = mixer.play(constant(0.1f, 1000), loud);
        VoiceParams quiet;
        quiet.gain = 0.1f;
        const VoiceId dropped = mixer.play(constant(0.1f, 1000), quiet);
        render(mixer, 4);
        check(mixer.isPlaying(a) && mixer.isPlaying(b) && !mixer.isPlaying(dropped), "a quieter newcomer is dropped");
    }
    {
        // One-shots go before loops, even quiet loops.
        Mixer mixer(kRate, 3);
        VoiceParams loop;
        loop.gain = 0.1f;
        loop.loop = true;
        const VoiceId looping = mixer.play(constant(0.1f, 1000), loop);
        const VoiceId first = mixer.play(constant(0.1f, 1000), loud);
        const VoiceId second = mixer.play(constant(0.1f, 1000), loud);
        const VoiceId fresh = mixer.play(constant(0.1f, 1000), loud);
        render(mixer, 4);
        check(mixer.isPlaying(looping) && !mixer.isPlaying(first) && mixer.isPlaying(second) && mixer.isPlaying(fresh),
              "a loop outlives one-shots");
    }
    {
        // A voice that is already being stopped is the first to go.
        Mixer mixer(kRate, 3);
        const VoiceId a = mixer.play(constant(0.1f, 1000), loud);
        VoiceParams louder;
        louder.gain = 2;
        const VoiceId b = mixer.play(constant(0.1f, 1000), louder);
        const VoiceId c = mixer.play(constant(0.1f, 1000), loud);
        render(mixer, 1);
        mixer.stop(b);
        const VoiceId fresh = mixer.play(constant(0.1f, 1000), loud);
        render(mixer, 4);
        check(mixer.isPlaying(a) && mixer.isPlaying(c) && mixer.isPlaying(fresh) && !mixer.isPlaying(b),
              "a stopping voice is the one replaced");
    }
    {
        Mixer mixer(kRate);
        for (int i = 0; i < 100; ++i) {
            mixer.play(constant(0.01f, 5000));
        }
        render(mixer, 64);
        check(mixer.activeVoices() == Mixer::kDefaultVoices, "the default is 32 voices");
    }
}

void testLooping() {
    {
        // A 7-frame loop across block boundaries (256 is not a multiple of 7): the sequence never breaks.
        auto sound = std::make_shared<SoundData>();
        sound->channels = 2;
        sound->sampleRate = kRate;
        for (int k = 0; k < 7; ++k) {
            sound->samples.push_back(static_cast<std::int16_t>(1000 * (k + 1)));
            sound->samples.push_back(static_cast<std::int16_t>(-1000 * (k + 1)));
        }
        Mixer mixer(kRate);
        VoiceParams params;
        params.loop = true;
        mixer.play(sound, params);
        const auto out = render(mixer, 1500);
        bool seamless = true;
        for (int i = 0; i < 1500; ++i) {
            seamless = seamless && near(left(out, i), 1000.0f * (i % 7 + 1) / 32768.0f) &&
                       near(right(out, i), -1000.0f * (i % 7 + 1) / 32768.0f);
        }
        check(seamless, "a loop repeats without a gap or a repeated frame");
        check(!mixer.idle(), "a loop keeps playing");
    }
    {
        // Three frames 0.2 0.4 0.6 looping at half speed: ... 0.6 | 0.4 (mean of 0.6 and 0.2) | 0.2 | 0.3 ...
        Mixer mixer(kRate);
        VoiceParams params;
        params.loop = true;
        params.pitch = 0.5f;
        mixer.play(stereo({{0.2f, 0.2f}, {0.4f, 0.4f}, {0.6f, 0.6f}}), params);
        const auto out = render(mixer, 14);
        const float expected[14] = {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.4f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.4f, 0.2f, 0.3f};
        bool ok = true;
        for (int i = 0; i < 14; ++i) {
            ok = ok && near(left(out, i), expected[i], 1e-4f);
        }
        check(ok, "interpolation continues across the loop seam");
    }
}

void testStopAndGainRamps() {
    {
        Mixer mixer(kRate);
        const VoiceId id = mixer.play(constant(0.5f, 5000));
        render(mixer, 300);
        mixer.stop(id);
        const auto out = render(mixer, 300);
        check(left(out, 0) < 0.5f && left(out, 0) > 0.45f, "a stop fades out rather than cutting");
        check(near(left(out, 255), 0, 1e-6f) && left(out, 256) == 0, "silent by the end of the block");
        check(mixer.idle() && !mixer.isPlaying(id), "and the voice is gone");
    }
    {
        // Changing the gain ramps over a block: no step bigger than the slope allows.
        Mixer mixer(kRate);
        const VoiceId id = mixer.play(constant(0.5f, 5000));
        render(mixer, 256);
        mixer.setVoice(id, 0, 0, 1);
        const auto out = render(mixer, 256);
        float biggestStep = 0, previous = 0.5f;
        for (int i = 0; i < 256; ++i) {
            biggestStep = std::max(biggestStep, std::fabs(left(out, i) - previous));
            previous = left(out, i);
        }
        check(biggestStep < 0.5f / 256.0f + 1e-5f && near(left(out, 255), 0), "gain changes ramp over a block");
    }
    {
        Mixer mixer(kRate);
        mixer.play(constant(0.5f, 5000));
        mixer.play(constant(0.5f, 5000));
        render(mixer, 8);
        mixer.stopAll();
        render(mixer, 300);
        check(mixer.idle(), "stopAll");
    }
}

void testVolumesAndNonFiniteInput() {
    Mixer mixer(kRate);
    mixer.setMasterVolume(0.5f);
    mixer.setSfxVolume(0.5f);
    mixer.setMusicVolume(0.25f);
    mixer.play(constant(0.8f, 8));
    mixer.playMusic(std::make_unique<CountingSource>(100, kRate), 0, true);
    const auto out = render(mixer, 1);
    // sfx 0.8 * (0.5 * 0.5) + music (1/100) * (0.5 * 0.25)
    check(near(left(out, 0), 0.8f * 0.25f + 0.01f * 0.125f, 1e-4f), "master, effects and music volumes");
    mixer.setMasterVolume(7);
    check(mixer.masterVolume() == 1, "volumes clamp to 0..1");
    mixer.setSfxVolume(std::nanf(""));
    check(mixer.sfxVolume() == 1, "NaN becomes full volume");

    Mixer odd(kRate);
    VoiceParams params;
    params.gain = std::nanf("");
    params.pan = INFINITY;
    params.pitch = std::nanf("");
    odd.play(constant(0.5f, 8), params);
    const auto silent = render(odd, 4);
    check(std::ranges::all_of(silent, [](float s) { return std::isfinite(s); }), "non-finite parameters do not poison the mix");
    check(odd.play(std::shared_ptr<const SoundData>()) == 0, "no sound, no voice");
    check(odd.play(std::make_shared<SoundData>()) == 0, "an empty sound plays nothing");
}

void testMusic() {
    constexpr int kMusicRate = 8000;
    {
        // A looping stream repeats without a gap.
        Mixer mixer(kMusicRate);
        mixer.playMusic(std::make_unique<CountingSource>(5, kMusicRate), 0, true);
        const auto out = render(mixer, 700);
        bool seamless = true;
        for (int i = 0; i < 700; ++i) {
            seamless = seamless && near(left(out, i), static_cast<float>(i % 5 + 1) / 100.0f);
        }
        check(seamless, "looping music has no seam");
        check(mixer.musicPlaying(), "music keeps playing");
    }
    {
        // Without looping, the track ends and the mixer says so.
        Mixer mixer(kMusicRate);
        mixer.playMusic(std::make_unique<CountingSource>(5, kMusicRate), 0, false);
        check(mixer.musicPlaying(), "music queued counts as playing");
        const auto out = render(mixer, 20);
        check(near(left(out, 4), 0.05f) && left(out, 5) == 0, "one pass");
        check(!mixer.musicPlaying(), "and then it is over");
    }
}

void testCrossfade() {
    constexpr int kMusicRate = 8000;
    class Level final : public FrameSource {
    public:
        explicit Level(float value) : value_(value) {}
        int sampleRate() const override { return kMusicRate; }
        bool mono() const override { return false; }
        int read(float* stereo, int frames) override {
            std::fill(stereo, stereo + 2 * frames, value_);
            return frames;
        }
        bool rewind() override { return true; }

    private:
        float value_;
    };
    Mixer mixer(kMusicRate);
    mixer.playMusic(std::make_unique<Level>(0.5f), 0, true);
    check(near(left(render(mixer, 256), 255), 0.5f), "the first track starts at once without a fade");
    mixer.playMusic(std::make_unique<Level>(0.25f), 0.5f, true);  // 4000 frames
    bool curve = true;
    for (int block = 1; block <= 15; ++block) {
        const auto out = render(mixer, 256);
        const float t = static_cast<float>(block * 256) / 4000.0f;
        const float expected = 0.5f * std::sin((1 - t) * std::numbers::pi_v<float> / 2) +
                               0.25f * std::sin(t * std::numbers::pi_v<float> / 2);
        curve = curve && near(left(out, 255), expected, 2e-4f);
    }
    check(curve, "the tracks cross over on equal-power curves");
    const auto rest = render(mixer, 512);
    check(near(left(rest, 300), 0.25f, 1e-5f), "the old track is gone and the new one at full level");
    check(mixer.musicPlaying(), "the new track plays on");

    mixer.stopMusic(0.25f);  // 2000 frames
    const auto fade = render(mixer, 2048);
    check(near(left(fade, 255), 0.25f * std::sin((1 - 256.0f / 2000.0f) * std::numbers::pi_v<float> / 2), 2e-4f), "fade out");
    check(left(fade, 2047) == 0 && !mixer.musicPlaying(), "and then silence");
}

void testPause() {
    Mixer mixer(kRate);
    mixer.play(line(100000));
    render(mixer, 512);
    mixer.setPaused(true);
    check(mixer.paused(), "paused flag");
    const auto fade = render(mixer, 256);
    check(left(fade, 0) > left(fade, 255) && near(left(fade, 255), 0, 1e-6f), "pausing fades out");
    const auto still = render(mixer, 1000);
    check(std::ranges::all_of(still, [](float s) { return s == 0; }), "silence while paused");
    check(!mixer.idle(), "the voice is still there");
    mixer.setPaused(false);
    const auto back = render(mixer, 512);
    // 512 frames played, 256 more during the fade: the line resumes at frame 768. The first block ramps
    // from silence; the second runs at full gain.
    check(near(left(back, 0), 768.0f / 32768.0f / 256.0f), "resuming fades in from silence");
    check(near(left(back, 255), 1023.0f / 32768.0f) && near(left(back, 256), 1024.0f / 32768.0f),
          "playback resumes where it stopped");
}

void testThreads() {
    // A game thread hammering the mixer while the audio thread renders: no crash, output in range.
    Mixer mixer(kRate);
    std::atomic<bool> done{false};
    std::thread game([&] {
        auto sound = constant(0.3f, 3000);
        VoiceId last = 0;
        for (int i = 0; !done && i < 20000; ++i) {
            VoiceParams params;
            params.gain = static_cast<float>(i % 7) / 3.0f;
            params.pan = static_cast<float>(i % 5) / 2.0f - 1.0f;
            params.loop = i % 11 == 0;
            last = mixer.play(sound, params);
            mixer.setVoice(last, 1, 0, 1 + static_cast<float>(i % 3) / 4.0f);
            if (i % 13 == 0) {
                mixer.stop(last);
            }
            if (i % 101 == 0) {
                mixer.stopAll();
            }
            if (i % 50 == 0) {
                mixer.playMusic(std::make_unique<CountingSource>(3000, kRate), 0.01f, true);
            }
            if (i % 70 == 0) {
                mixer.setPaused(i % 140 == 0);
            }
            mixer.setMasterVolume(static_cast<float>(i % 10) / 9.0f);
            (void)mixer.isPlaying(last);
            (void)mixer.activeVoices();
        }
        mixer.setPaused(false);
    });
    bool inRange = true;
    std::vector<float> out(2 * 512);
    for (int i = 0; i < 400; ++i) {
        mixer.render(out.data(), 512);
        inRange = inRange && std::ranges::all_of(out, [](float s) { return std::fabs(s) <= 1.0f; });
    }
    done = true;
    game.join();
    check(inRange, "output stays in range under concurrent control");
}

}  // namespace

int main() {
    testGainAndStereo();
    testPan();
    testPitchAndResampling();
    testSoftLimiter();
    testVoiceStealing();
    testLooping();
    testStopAndGainRamps();
    testVolumesAndNonFiniteInput();
    testMusic();
    testCrossfade();
    testPause();
    testThreads();
    if (failures == 0) {
        std::puts("mixer tests passed");
    }
    return failures == 0 ? 0 : 1;
}
