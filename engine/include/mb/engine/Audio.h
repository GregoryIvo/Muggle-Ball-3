#pragma once

#include "mb/engine/Mixer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

struct SDL_AudioStream;

namespace mb {

// What game code needs from the audio engine, so it can be tested against a stand-in.
class AudioPlayer {
public:
    virtual ~AudioPlayer() = default;

    // Plays an Ogg Vorbis file as a sound effect. Returns 0 if nothing plays: no audio device, or a
    // file that cannot be read.
    virtual VoiceId playSound(const std::filesystem::path& file, const VoiceParams& params) = 0;
    virtual void stopSound(VoiceId voice) = 0;

    // Streams an Ogg Vorbis file as the music, crossfading from the track playing. Looping repeats it
    // seamlessly; otherwise it ends after one pass.
    virtual void playMusic(const std::filesystem::path& file, float fadeSeconds, bool loop) = 0;
    virtual void stopMusic(float fadeSeconds) = 0;
};

// The audio device (SDL3) driving a Mixer. Sound effects are decoded once and kept; music is
// decoded as it plays. Without an audio device, which happens on headless machines, everything is a
// quiet no-op: available() is false and no call fails.
//
// Sound effects load on the first play, on the calling thread. Call from the game thread only.
class Audio final : public AudioPlayer {
public:
    Audio();
    ~Audio() override;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool available() const { return stream_ != nullptr; }

    VoiceId playSound(const std::filesystem::path& file, const VoiceParams& params) override;
    void stopSound(VoiceId voice) override;
    void playMusic(const std::filesystem::path& file, float fadeSeconds, bool loop) override;
    void stopMusic(float fadeSeconds) override;

    // Volumes, pause, and what is playing.
    Mixer& mixer() { return mixer_; }
    const Mixer& mixer() const { return mixer_; }

private:
    // The decoded sound for a file, cached; null (with a warning, once) if it cannot be loaded.
    std::shared_ptr<const SoundData> loadSound(const std::filesystem::path& file);

    Mixer mixer_;
    SDL_AudioStream* stream_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<const SoundData>> sounds_;  // null for a file that failed
};

}  // namespace mb
