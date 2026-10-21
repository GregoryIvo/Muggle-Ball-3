#include "mb/engine/Mixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <numbers>
#include <tuple>

namespace mb {

namespace {

constexpr int kBlockFrames = 256;  // gains ramp over a block, so changes never click
constexpr int kMaxMusicTracks = 3;  // the one playing and those still fading out
constexpr float kLimiterKnee = 0.8f;
constexpr float kHalfPi = std::numbers::pi_v<float> / 2;
constexpr float kSilence[2] = {0, 0};

float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

float clamp01(float value) { return std::clamp(finiteOr(value, 1), 0.0f, 1.0f); }

// Left and right gain for a pan position in -1..1.
void panGains(bool mono, float pan, float& left, float& right) {
    pan = std::clamp(finiteOr(pan, 0), -1.0f, 1.0f);
    if (mono) {
        // Constant power: 0.707 each side in the centre, 1 on one side at the extreme.
        const float angle = (pan + 1) * (kHalfPi / 2);
        left = std::cos(angle);
        right = std::sin(angle);
    } else {
        // Balance: untouched in the centre, the far side fades out.
        left = std::min(1.0f, 1 - pan);
        right = std::min(1.0f, 1 + pan);
    }
}

// Transparent below the knee, then bends smoothly towards +-1 (same slope at the knee).
float softLimit(float x) {
    const float magnitude = std::fabs(x);
    if (magnitude <= kLimiterKnee) {
        return x;
    }
    const float room = 1 - kLimiterKnee;
    return std::copysign(kLimiterKnee + room * std::tanh((magnitude - kLimiterKnee) / room), x);
}

class PcmSource final : public FrameSource {
public:
    explicit PcmSource(std::shared_ptr<const SoundData> data) : data_(std::move(data)) {}

    int sampleRate() const override { return data_->sampleRate; }
    bool mono() const override { return data_->channels == 1; }

    int read(float* stereo, int frames) override {
        const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(frames), data_->frames() - position_);
        const std::int16_t* in = data_->samples.data() + position_ * static_cast<std::size_t>(data_->channels);
        constexpr float kScale = 1.0f / 32768.0f;
        for (std::size_t i = 0; i < count; ++i) {
            if (data_->channels == 1) {
                stereo[2 * i] = stereo[2 * i + 1] = static_cast<float>(in[i]) * kScale;
            } else {
                stereo[2 * i] = static_cast<float>(in[2 * i]) * kScale;
                stereo[2 * i + 1] = static_cast<float>(in[2 * i + 1]) * kScale;
            }
        }
        position_ += count;
        return static_cast<int>(count);
    }

    bool rewind() override {
        position_ = 0;
        return data_->frames() > 0;
    }

private:
    std::shared_ptr<const SoundData> data_;
    std::size_t position_ = 0;
};

}  // namespace

struct Mixer::Impl {
    struct Voice {
        VoiceId id = 0;
        std::unique_ptr<FrameSource> source;
        float gain = 1, pan = 0, pitch = 1;
        bool loop = false;
        bool stopping = false;  // fading out to nothing
        bool finished = false;  // to be removed after this block
        std::uint64_t order = 0;

        // Music fades: fadeLevel moves to fadeTarget by fadeStep per frame, and the gain follows an
        // equal-power curve of it.
        float fadeLevel = 1, fadeTarget = 1, fadeStep = 0;

        // Gains reached at the end of the previous block; this block ramps from them.
        float lastLeft = 0, lastRight = 0;
        bool started = false;  // false until lastLeft/lastRight hold something

        // Source frames not yet passed, and where the next output frame falls among them.
        std::vector<float> buffer;  // stereo frames
        int buffered = 0;
        double phase = 0;
        bool ended = false;  // the source has no more frames
    };

    struct Command {
        enum class Type { Play, SetVoice, Stop, StopAll, PlayMusic, StopMusic };
        Type type = Type::Play;
        VoiceId id = 0;
        std::unique_ptr<Voice> voice;
        float gain = 1, pan = 0, pitch = 1;
        float fadeSeconds = 0;
    };

    Impl(int rate, int maxVoices) : sampleRate(rate), maxSfxVoices(maxVoices) {
        sfxVoices.reserve(static_cast<std::size_t>(maxVoices));
    }

    // --- game thread ---------------------------------------------------------------------------

    std::unique_ptr<Voice> makeVoice(std::unique_ptr<FrameSource> source, const VoiceParams& params) const {
        auto voice = std::make_unique<Voice>();
        voice->gain = std::max(0.0f, finiteOr(params.gain, 0));
        voice->pan = std::clamp(finiteOr(params.pan, 0), -1.0f, 1.0f);
        voice->pitch = std::clamp(finiteOr(params.pitch, 1), Mixer::kMinPitch, Mixer::kMaxPitch);
        voice->loop = params.loop;
        const double step = static_cast<double>(voice->pitch) * source->sampleRate() / sampleRate;
        voice->buffer.resize(2 * (static_cast<std::size_t>(kBlockFrames * step) + 8));
        voice->source = std::move(source);
        return voice;
    }

    VoiceId enqueue(Command command) {
        std::lock_guard lock(mutex);
        VoiceId id = 0;
        if (command.voice) {
            id = command.voice->id = nextId++;
            (command.type == Command::Type::Play ? pendingPlays : pendingMusic) += 1;
        }
        queue.push_back(std::move(command));
        return id;
    }

    // --- audio thread --------------------------------------------------------------------------

    void takeCommands() {
        std::lock_guard lock(mutex);
        work.swap(queue);
        // Count what is about to start as playing, so idle() and isPlaying() have no gap.
        for (const Command& command : work) {
            if (command.type == Command::Type::Play) {
                publishedIds.push_back(command.voice->id);
            }
        }
        publishedVoices += pendingPlays;
        publishedMusic += pendingMusic;
        pendingPlays = pendingMusic = 0;
    }

    void publish() {
        std::lock_guard lock(mutex);
        publishedVoices = static_cast<int>(sfxVoices.size());
        publishedIds.clear();
        for (const Voice& voice : sfxVoices) {
            publishedIds.push_back(voice.id);
        }
        publishedMusic = static_cast<int>(std::ranges::count_if(
            musicVoices, [](const Voice& v) { return !v.stopping && v.fadeTarget > 0; }));
    }

    Voice* find(VoiceId id) {
        const auto it = std::ranges::find(sfxVoices, id, &Voice::id);
        return it == sfxVoices.end() ? nullptr : &*it;
    }

    // A stopped voice fades out over the next block; one that is already silent just goes.
    void stopVoice(Voice& voice) {
        voice.stopping = true;
        voice.finished = voice.finished || frozen;
    }

    void fadeOut(Voice& voice, float seconds) {
        if (seconds > 0) {
            voice.fadeTarget = 0;
            voice.fadeStep = 1 / (seconds * static_cast<float>(sampleRate));
        } else {
            stopVoice(voice);
        }
    }

    void addSfx(Voice&& voice) {
        if (static_cast<int>(sfxVoices.size()) >= maxSfxVoices) {
            // Least important first: already stopping, then one-shots, then quiet, then old.
            const auto victim = std::ranges::min_element(sfxVoices, [](const Voice& a, const Voice& b) {
                const auto key = [](const Voice& v) { return std::tuple(!v.stopping, v.loop, v.gain, v.order); };
                return key(a) < key(b);
            });
            if (!victim->stopping && voice.gain < victim->gain) {
                return;  // quieter than everything playing: not worth cutting anything
            }
            sfxVoices.erase(victim);
        }
        voice.order = ++orderCounter;
        sfxVoices.push_back(std::move(voice));
    }

    void addMusic(Voice&& voice, float fadeSeconds) {
        for (Voice& playing : musicVoices) {
            fadeOut(playing, fadeSeconds);
        }
        if (static_cast<int>(musicVoices.size()) >= kMaxMusicTracks) {
            musicVoices.erase(musicVoices.begin());
        }
        if (fadeSeconds > 0) {
            voice.fadeLevel = 0;
            voice.fadeStep = 1 / (fadeSeconds * static_cast<float>(sampleRate));
            voice.started = true;  // fades in from silence
        }
        voice.order = ++orderCounter;
        musicVoices.push_back(std::move(voice));
    }

    void apply(Command& command) {
        switch (command.type) {
            case Command::Type::Play:
                addSfx(std::move(*command.voice));
                break;
            case Command::Type::SetVoice:
                if (Voice* voice = find(command.id)) {
                    voice->gain = command.gain;
                    voice->pan = command.pan;
                    voice->pitch = command.pitch;
                }
                break;
            case Command::Type::Stop:
                if (Voice* voice = find(command.id)) {
                    stopVoice(*voice);
                }
                break;
            case Command::Type::StopAll:
                for (Voice& voice : sfxVoices) {
                    stopVoice(voice);
                }
                break;
            case Command::Type::PlayMusic:
                addMusic(std::move(*command.voice), command.fadeSeconds);
                break;
            case Command::Type::StopMusic:
                for (Voice& voice : musicVoices) {
                    fadeOut(voice, command.fadeSeconds);
                }
                break;
        }
    }

    // Reads up to `frames` source frames, starting over at the end when the voice loops.
    static int readSource(Voice& voice, float* out, int frames) {
        int got = 0;
        bool rewound = false;
        while (got < frames) {
            const int n = voice.source->read(out + 2 * got, frames - got);
            got += n;
            // An empty source (nothing after a rewind) ends the voice instead of spinning.
            if (got == frames || !voice.loop || (n == 0 && rewound) || !voice.source->rewind()) {
                break;
            }
            rewound = true;
        }
        return got;
    }

    // Resamples the voice into `into` (n stereo frames, no gain) by linear interpolation; returns how
    // many frames it could produce, fewer than n once the source has run out.
    int produce(Voice& voice, float* into, int n, double step) {
        const int passed = std::min(static_cast<int>(voice.phase), voice.buffered);
        if (passed > 0) {
            std::copy(voice.buffer.begin() + 2 * passed, voice.buffer.begin() + 2 * voice.buffered,
                      voice.buffer.begin());
            voice.buffered -= passed;
            voice.phase -= passed;
        }
        // The last frame this block reads, plus its successor to interpolate with.
        const int needed = static_cast<int>(voice.phase + (n - 1) * step) + 2;
        if (!voice.ended && voice.buffered < needed) {
            if (voice.buffer.size() < 2 * static_cast<std::size_t>(needed)) {
                voice.buffer.resize(2 * static_cast<std::size_t>(needed));
            }
            const int want = needed - voice.buffered;
            const int got = readSource(voice, voice.buffer.data() + 2 * voice.buffered, want);
            voice.buffered += got;
            voice.ended = got < want;
        }
        int produced = 0;
        for (; produced < n; ++produced) {
            const int index = static_cast<int>(voice.phase);
            if (index >= voice.buffered) {
                break;
            }
            const float* a = &voice.buffer[2 * static_cast<std::size_t>(index)];
            const float* b = index + 1 < voice.buffered ? a + 2 : kSilence;  // the end fades to silence
            const auto t = static_cast<float>(voice.phase - index);
            into[2 * produced] = a[0] + (b[0] - a[0]) * t;
            into[2 * produced + 1] = a[1] + (b[1] - a[1]) * t;
            voice.phase += step;
        }
        return produced;
    }

    // Adds one block of the voice to `mix`. `groupGain` is master * effects or music volume, and 0 while pausing.
    void mixVoice(Voice& voice, int n, float groupGain) {
        float level = voice.fadeLevel;
        if (voice.fadeTarget < level) {
            level = std::max(voice.fadeTarget, level - voice.fadeStep * static_cast<float>(n));
        } else if (voice.fadeTarget > level) {
            level = std::min(voice.fadeTarget, level + voice.fadeStep * static_cast<float>(n));
        }
        voice.fadeLevel = level;
        const float curve = level >= 1 ? 1.0f : std::sin(level * kHalfPi);

        float panLeft = 0, panRight = 0;
        panGains(voice.source->mono(), voice.pan, panLeft, panRight);
        const float amplitude = voice.stopping ? 0.0f : voice.gain * groupGain * curve;
        const float targetLeft = amplitude * panLeft, targetRight = amplitude * panRight;
        if (!voice.started) {
            voice.lastLeft = targetLeft;
            voice.lastRight = targetRight;
            voice.started = true;
        }

        const double step = static_cast<double>(voice.pitch) * voice.source->sampleRate() / sampleRate;
        const int produced = produce(voice, dry, n, step);
        const float inverse = 1.0f / static_cast<float>(n);
        for (int i = 0; i < produced; ++i) {
            const float t = static_cast<float>(i + 1) * inverse;
            mix[2 * i] += dry[2 * i] * (voice.lastLeft + (targetLeft - voice.lastLeft) * t);
            mix[2 * i + 1] += dry[2 * i + 1] * (voice.lastRight + (targetRight - voice.lastRight) * t);
        }
        voice.lastLeft = targetLeft;
        voice.lastRight = targetRight;
        voice.finished = voice.finished || produced < n || voice.stopping || (voice.fadeTarget == 0 && level == 0);
    }

    void renderBlock(float* out, int n) {
        const bool pausing = paused.load(std::memory_order_relaxed);
        if (pausing && frozen) {
            std::fill(out, out + 2 * n, 0.0f);
            return;
        }
        const float master = clamp01(masterVolume.load(std::memory_order_relaxed));
        const float pauseGain = pausing ? 0.0f : 1.0f;
        const float sfxGain = master * clamp01(sfxVolume.load(std::memory_order_relaxed)) * pauseGain;
        const float musicGain = master * clamp01(musicVolume.load(std::memory_order_relaxed)) * pauseGain;

        std::fill(mix, mix + 2 * n, 0.0f);
        for (Voice& voice : sfxVoices) {
            mixVoice(voice, n, sfxGain);
        }
        for (Voice& voice : musicVoices) {
            mixVoice(voice, n, musicGain);
        }
        for (int i = 0; i < 2 * n; ++i) {
            out[i] = softLimit(mix[i]);
        }
        removeFinished();
        frozen = pausing;  // this block faded to silence; from the next one everything stands still
    }

    void removeFinished() {
        const auto done = [](const Voice& voice) { return voice.finished; };
        std::erase_if(sfxVoices, done);
        std::erase_if(musicVoices, done);
    }

    const int sampleRate;
    const int maxSfxVoices;

    // Shared with the game thread, under the mutex.
    mutable std::mutex mutex;
    std::vector<Command> queue;
    VoiceId nextId = 1;
    int pendingPlays = 0, pendingMusic = 0;           // queued, not yet started
    int publishedVoices = 0, publishedMusic = 0;      // as of the last render, plus what it just took
    std::vector<VoiceId> publishedIds;

    std::atomic<float> masterVolume{1}, sfxVolume{1}, musicVolume{1};
    std::atomic<bool> paused{false};

    // Audio thread only.
    std::vector<Voice> sfxVoices, musicVoices;
    std::vector<Command> work;
    std::uint64_t orderCounter = 0;
    bool frozen = false;  // paused and already faded out
    float mix[2 * kBlockFrames] = {};
    float dry[2 * kBlockFrames] = {};
};

Mixer::Mixer(int sampleRate, int maxVoices)
    : impl_(std::make_unique<Impl>(sampleRate, std::max(1, maxVoices))) {}

Mixer::~Mixer() = default;

int Mixer::sampleRate() const { return impl_->sampleRate; }

VoiceId Mixer::play(std::shared_ptr<const SoundData> sound, const VoiceParams& params) {
    if (!sound || sound->frames() == 0 || sound->sampleRate <= 0 || (sound->channels != 1 && sound->channels != 2)) {
        return 0;
    }
    return play(std::make_unique<PcmSource>(std::move(sound)), params);
}

VoiceId Mixer::play(std::unique_ptr<FrameSource> source, const VoiceParams& params) {
    if (!source || source->sampleRate() <= 0) {
        return 0;
    }
    Impl::Command command;
    command.type = Impl::Command::Type::Play;
    command.voice = impl_->makeVoice(std::move(source), params);
    return impl_->enqueue(std::move(command));
}

void Mixer::setVoice(VoiceId voice, float gain, float pan, float pitch) {
    Impl::Command command;
    command.type = Impl::Command::Type::SetVoice;
    command.id = voice;
    command.gain = std::max(0.0f, finiteOr(gain, 0));
    command.pan = std::clamp(finiteOr(pan, 0), -1.0f, 1.0f);
    command.pitch = std::clamp(finiteOr(pitch, 1), kMinPitch, kMaxPitch);
    impl_->enqueue(std::move(command));
}

void Mixer::stop(VoiceId voice) {
    Impl::Command command;
    command.type = Impl::Command::Type::Stop;
    command.id = voice;
    impl_->enqueue(std::move(command));
}

void Mixer::stopAll() {
    Impl::Command command;
    command.type = Impl::Command::Type::StopAll;
    impl_->enqueue(std::move(command));
}

void Mixer::playMusic(std::unique_ptr<FrameSource> source, float fadeSeconds, bool loop) {
    if (!source || source->sampleRate() <= 0) {
        return;
    }
    Impl::Command command;
    command.type = Impl::Command::Type::PlayMusic;
    VoiceParams params;
    params.loop = loop;
    command.voice = impl_->makeVoice(std::move(source), params);
    command.fadeSeconds = std::max(0.0f, finiteOr(fadeSeconds, 0));
    impl_->enqueue(std::move(command));
}

void Mixer::stopMusic(float fadeSeconds) {
    Impl::Command command;
    command.type = Impl::Command::Type::StopMusic;
    command.fadeSeconds = std::max(0.0f, finiteOr(fadeSeconds, 0));
    impl_->enqueue(std::move(command));
}

void Mixer::setMasterVolume(float volume) { impl_->masterVolume = clamp01(volume); }
void Mixer::setSfxVolume(float volume) { impl_->sfxVolume = clamp01(volume); }
void Mixer::setMusicVolume(float volume) { impl_->musicVolume = clamp01(volume); }
float Mixer::masterVolume() const { return impl_->masterVolume; }
float Mixer::sfxVolume() const { return impl_->sfxVolume; }
float Mixer::musicVolume() const { return impl_->musicVolume; }

void Mixer::setPaused(bool paused) { impl_->paused = paused; }
bool Mixer::paused() const { return impl_->paused; }

bool Mixer::isPlaying(VoiceId voice) const {
    std::lock_guard lock(impl_->mutex);
    if (std::ranges::find(impl_->publishedIds, voice) != impl_->publishedIds.end()) {
        return true;
    }
    return std::ranges::any_of(impl_->queue, [voice](const Impl::Command& command) {
        return command.type == Impl::Command::Type::Play && command.voice->id == voice;
    });
}

int Mixer::activeVoices() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->publishedVoices + impl_->pendingPlays;
}

bool Mixer::musicPlaying() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->publishedMusic + impl_->pendingMusic > 0;
}

void Mixer::render(float* out, int frames) {
    impl_->takeCommands();
    for (Impl::Command& command : impl_->work) {
        impl_->apply(command);
    }
    impl_->work.clear();
    impl_->removeFinished();  // voices stopped while everything stood still
    while (frames > 0) {
        const int n = std::min(frames, kBlockFrames);
        impl_->renderBlock(out, n);
        out += 2 * n;
        frames -= n;
    }
    impl_->publish();
}

}  // namespace mb
