// Records what Audio sends to the speakers by pointing SDL at its "disk" audio driver, which writes
// the device's samples to a file instead of playing them. No sound card needed.

#pragma once

#include "mb/engine/Audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace mb::test {

// Interleaved stereo float samples at Audio's sample rate (44100 Hz).
struct Capture {
    bool deviceOpened = false;
    std::vector<float> samples;

    std::size_t frames() const { return samples.size() / 2; }
    float left(std::size_t frame) const { return samples[2 * frame]; }
    float right(std::size_t frame) const { return samples[2 * frame + 1]; }

    // Index of the first / one past the last frame that is not silent; both 0 if all of it is.
    std::size_t firstSound() const {
        for (std::size_t i = 0; i < frames(); ++i) {
            if (left(i) != 0 || right(i) != 0) {
                return i;
            }
        }
        return 0;
    }
    std::size_t endOfSound() const {
        for (std::size_t i = frames(); i > 0; --i) {
            if (left(i - 1) != 0 || right(i - 1) != 0) {
                return i;
            }
        }
        return 0;
    }
    float peak() const {
        float peak = 0;
        for (const float s : samples) {
            peak = std::max(peak, std::fabs(s));
        }
        return peak;
    }
};

// Opens an Audio on the disk driver, calls `play`, waits until `finished` (or a timeout), closes it
// and returns the samples written.
inline Capture captureAudio(const std::filesystem::path& scratch, const std::function<void(Audio&)>& play,
                            const std::function<bool(Audio&)>& finished, int timeoutMs = 5000) {
    std::filesystem::create_directories(scratch);
    const std::filesystem::path file = scratch / "output.raw";
    std::filesystem::remove(file);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
    SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file.string().c_str());
    Capture capture;
    {
        Audio audio;
        capture.deviceOpened = audio.available();
        if (capture.deviceOpened) {
            play(audio);
            const auto start = std::chrono::steady_clock::now();
            while (!finished(audio) &&
                   std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
                SDL_Delay(5);
            }
            SDL_Delay(400);  // let the last buffers reach the file
        }
    }
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    SDL_ResetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE);
    std::ifstream in(file, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    capture.samples.resize(bytes.size() / sizeof(float));
    std::memcpy(capture.samples.data(), bytes.data(), capture.samples.size() * sizeof(float));
    return capture;
}

}  // namespace mb::test
