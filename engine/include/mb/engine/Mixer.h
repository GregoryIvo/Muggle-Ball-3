#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mb {

// A decoded sound effect: 16-bit PCM, shared by every voice that plays it.
struct SoundData {
    int channels = 1;  // 1 or 2
    int sampleRate = 44100;
    std::vector<std::int16_t> samples;  // interleaved

    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
};

// Frames of audio read in order: a decoded sound, a music stream.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual int sampleRate() const = 0;
    // A mono source pans as a point (constant power); a stereo one pans as a balance. read() writes
    // stereo frames either way.
    virtual bool mono() const = 0;
    // Writes up to `frames` interleaved stereo frames and returns how many; fewer means the end.
    virtual int read(float* stereo, int frames) = 0;
    // Back to the first frame, for looping; false if the source cannot.
    virtual bool rewind() = 0;
};

using VoiceId = std::uint32_t;  // 0 is no voice

struct VoiceParams {
    float gain = 1;   // linear amplitude
    float pan = 0;    // -1 left, 0 centre, +1 right
    float pitch = 1;  // playback speed: 2 is an octave up and half the length
    bool loop = false;
};

// Software mixer: up to maxVoices sound effects plus music, mixed to interleaved stereo float at a
// fixed sample rate with per-voice gain, pan and pitch (linear resampling), master / effects / music
// volumes, music crossfades and pause. Output is soft-limited, so it never exceeds +-1.
//
// Threads: render() is for the audio callback; every other method may be called from the game thread.
// They only queue the change, which render() applies at its start, so a call takes effect at the
// next callback and never waits for audio. Voices are numbered by the caller's thread, so play()
// returns at once.
class Mixer {
public:
    static constexpr int kDefaultVoices = 32;
    static constexpr float kMinPitch = 0.1f;
    static constexpr float kMaxPitch = 4.0f;

    explicit Mixer(int sampleRate = 44100, int maxVoices = kDefaultVoices);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    int sampleRate() const;

    // Sound effects. When every voice is taken the least important one is cut: one already stopping,
    // then a one-shot before a loop, then the quietest, then the oldest. A new voice quieter than the
    // one that would be cut is dropped instead (its id is never playing).
    VoiceId play(std::shared_ptr<const SoundData> sound, const VoiceParams& params = {});
    VoiceId play(std::unique_ptr<FrameSource> source, const VoiceParams& params = {});
    // Gain, pan and pitch of a playing voice; changes ramp over a block, so they do not click.
    void setVoice(VoiceId voice, float gain, float pan, float pitch);
    void stop(VoiceId voice);  // short fade-out, then the voice is gone
    void stopAll();

    // Music: the new track fades in while the playing one fades out (equal power), over fadeSeconds.
    // A looping track repeats seamlessly.
    void playMusic(std::unique_ptr<FrameSource> source, float fadeSeconds, bool loop);
    void stopMusic(float fadeSeconds);

    // Linear amplitude, 0 to 1. Music and effects each get master * their own volume.
    void setMasterVolume(float volume);
    void setSfxVolume(float volume);
    void setMusicVolume(float volume);
    float masterVolume() const;
    float sfxVolume() const;
    float musicVolume() const;

    // Pause fades everything out and freezes it; resume fades back in where it left off.
    void setPaused(bool paused);
    bool paused() const;

    // As of the last render(), plus voices queued since.
    bool isPlaying(VoiceId voice) const;
    int activeVoices() const;  // sound effects only
    bool idle() const { return activeVoices() == 0; }
    bool musicPlaying() const;

    // Fills `out` with `frames` interleaved stereo frames. Audio thread only.
    void render(float* out, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mb
