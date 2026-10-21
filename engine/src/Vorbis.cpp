#include "mb/engine/Vorbis.h"

#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

namespace mb {

namespace {

const char* describe(int error) {
    switch (error) {
        case VORBIS_missing_capture_pattern:
        case VORBIS_invalid_stream_structure_version:
        case VORBIS_unexpected_eof:
            return "not an Ogg Vorbis stream";
        case VORBIS_feature_not_supported:
        case VORBIS_invalid_setup:
        case VORBIS_invalid_first_page:
        case VORBIS_bad_packet_type:
            return "unsupported or corrupt Vorbis headers";
        case VORBIS_outofmem:
            return "out of memory";
        default:
            return "cannot decode";
    }
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + file.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class VorbisStream final : public FrameSource {
public:
    explicit VorbisStream(std::vector<std::uint8_t> ogg) : data_(std::move(ogg)) {
        int error = 0;
        vorbis_ = stb_vorbis_open_memory(data_.data(), static_cast<int>(data_.size()), &error, nullptr);
        if (!vorbis_) {
            throw std::runtime_error(describe(error));
        }
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        channels_ = info.channels;
        sampleRate_ = static_cast<int>(info.sample_rate);
        if (channels_ < 1 || channels_ > 2) {
            stb_vorbis_close(vorbis_);
            throw std::runtime_error(std::format("{} channels (only mono and stereo are supported)", channels_));
        }
    }
    ~VorbisStream() override { stb_vorbis_close(vorbis_); }
    VorbisStream(const VorbisStream&) = delete;
    VorbisStream& operator=(const VorbisStream&) = delete;

    int sampleRate() const override { return sampleRate_; }
    bool mono() const override { return channels_ == 1; }

    int read(float* stereo, int frames) override {
        const int got = stb_vorbis_get_samples_float_interleaved(vorbis_, channels_, stereo, frames * channels_);
        if (channels_ == 1) {
            // Spread the mono frames over both channels in place, last to first.
            for (int i = got - 1; i >= 0; --i) {
                const float sample = stereo[i];
                stereo[2 * i] = sample;
                stereo[2 * i + 1] = sample;
            }
        }
        return got;
    }

    bool rewind() override { return stb_vorbis_seek_start(vorbis_) != 0; }

private:
    std::vector<std::uint8_t> data_;  // stb_vorbis reads straight from it
    stb_vorbis* vorbis_ = nullptr;
    int channels_ = 0;
    int sampleRate_ = 0;
};

}  // namespace

std::shared_ptr<const SoundData> decodeVorbis(std::span<const std::uint8_t> ogg) {
    int channels = 0, sampleRate = 0;
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &channels, &sampleRate,
                                                &decoded);
    if (frames < 0) {
        throw std::runtime_error(frames == -2 ? "out of memory" : "not decodable Ogg Vorbis");
    }
    const std::unique_ptr<short, decltype(&std::free)> samples(decoded, &std::free);
    if (channels < 1 || channels > 2) {
        throw std::runtime_error(std::format("{} channels (only mono and stereo are supported)", channels));
    }
    auto sound = std::make_shared<SoundData>();
    sound->channels = channels;
    sound->sampleRate = sampleRate;
    sound->samples.assign(decoded, decoded + static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
    return sound;
}

std::shared_ptr<const SoundData> decodeVorbisFile(const std::filesystem::path& file) {
    try {
        return decodeVorbis(readBytes(file));
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(file.string() + ": " + e.what());
    }
}

std::unique_ptr<FrameSource> openVorbisStream(std::vector<std::uint8_t> ogg) {
    return std::make_unique<VorbisStream>(std::move(ogg));
}

std::unique_ptr<FrameSource> openVorbisStreamFile(const std::filesystem::path& file) {
    try {
        return openVorbisStream(readBytes(file));
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(file.string() + ": " + e.what());
    }
}

}  // namespace mb
