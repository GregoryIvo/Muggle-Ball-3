#include "mb/engine/Image.h"

#include "mb/engine/Log.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cstring>
#include <fstream>
#include <iterator>

namespace mb {

std::optional<Image> loadImage(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        logWarn("image not found: {}", path.string());
        return std::nullopt;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        logWarn("cannot decode image {}: {}", path.string(), stbi_failure_reason());
        return std::nullopt;
    }
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    std::memcpy(image.rgba.data(), pixels, image.rgba.size());
    stbi_image_free(pixels);
    return image;
}

bool writePng(const std::filesystem::path& path, const Image& image) {
    if (image.empty()) {
        return false;
    }
    return stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.rgba.data(),
                          image.width * 4) != 0;
}

}  // namespace mb
