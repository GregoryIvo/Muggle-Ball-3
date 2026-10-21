#pragma once

#include "mb/engine/Mixer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace mb {

// Ogg Vorbis decoding with stb_vorbis. Mono and stereo only. All of these throw std::runtime_error
// for a missing file or data that is not decodable.

// Decodes everything up front: for short sound effects, which the audio code then keeps in memory.
std::shared_ptr<const SoundData> decodeVorbis(std::span<const std::uint8_t> ogg);
std::shared_ptr<const SoundData> decodeVorbisFile(const std::filesystem::path& file);

// Decodes on the fly as frames are read, from the compressed bytes held in memory: for music, which
// would take tens of megabytes decoded. rewind() restarts the stream, so a looping track repeats
// without a gap.
std::unique_ptr<FrameSource> openVorbisStream(std::vector<std::uint8_t> ogg);
std::unique_ptr<FrameSource> openVorbisStreamFile(const std::filesystem::path& file);

}  // namespace mb
