#include "mb/engine/Audio.h"

#include "mb/engine/Log.h"
#include "mb/engine/Vorbis.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>

namespace mb {

namespace {

constexpr int kSampleRate = 44100;  // what most sounds are recorded at; SDL converts to the device's rate
constexpr int kChunkFrames = 1024;

// Runs on the audio thread whenever the device wants more samples.
void SDLCALL onAudioNeeded(void* userdata, SDL_AudioStream* stream, int additionalBytes, int /*totalBytes*/) {
    auto* mixer = static_cast<Mixer*>(userdata);
    float chunk[2 * kChunkFrames];
    int frames = additionalBytes / static_cast<int>(2 * sizeof(float));
    while (frames > 0) {
        const int n = std::min(frames, kChunkFrames);
        mixer->render(chunk, n);
        SDL_PutAudioStreamData(stream, chunk, n * static_cast<int>(2 * sizeof(float)));
        frames -= n;
    }
}

}  // namespace

Audio::Audio() : mixer_(kSampleRate) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        logWarn("audio unavailable: {}", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kSampleRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, onAudioNeeded, &mixer_);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
        logWarn("audio unavailable: {}", SDL_GetError());
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    const char* device = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    logInfo("Audio: {} ({})", device ? device : "default device", SDL_GetCurrentAudioDriver());
}

Audio::~Audio() {
    if (stream_) {
        // Closes the device too; no callback runs after this returns.
        SDL_DestroyAudioStream(stream_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

std::shared_ptr<const SoundData> Audio::loadSound(const std::filesystem::path& file) {
    const auto [it, inserted] = sounds_.try_emplace(file.string());
    if (inserted) {
        try {
            it->second = decodeVorbisFile(file);
        } catch (const std::exception& e) {
            logWarn("cannot load sound: {}", e.what());
        }
    }
    return it->second;
}

VoiceId Audio::playSound(const std::filesystem::path& file, const VoiceParams& params) {
    if (!available()) {
        return 0;
    }
    const std::shared_ptr<const SoundData> sound = loadSound(file);
    return sound ? mixer_.play(sound, params) : 0;
}

void Audio::stopSound(VoiceId voice) { mixer_.stop(voice); }

void Audio::playMusic(const std::filesystem::path& file, float fadeSeconds, bool loop) {
    if (!available()) {
        return;
    }
    try {
        mixer_.playMusic(openVorbisStreamFile(file), fadeSeconds, loop);
    } catch (const std::exception& e) {
        logWarn("cannot play music: {}", e.what());
    }
}

void Audio::stopMusic(float fadeSeconds) { mixer_.stopMusic(fadeSeconds); }

}  // namespace mb
