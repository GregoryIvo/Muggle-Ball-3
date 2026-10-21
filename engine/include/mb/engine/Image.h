#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace mb {

// 8-bit RGBA pixels, rows top to bottom (the same origin as Direct3D and SDL GPU textures).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4 bytes

    bool empty() const { return width <= 0 || height <= 0; }
};

// Decodes JPEG, PNG, TGA or BMP. Returns nothing (and logs a warning) if the file
// is missing or cannot be decoded. DDS is converted to PNG by tools/, never loaded here.
std::optional<Image> loadImage(const std::filesystem::path& path);

// Writes a PNG; returns false on failure.
bool writePng(const std::filesystem::path& path, const Image& image);

}  // namespace mb
