#pragma once

// Helpers for the tests that run the menus against the converted game data ($MB3_ASSETS): forms loaded from files and
// the sizes of the images they name, read without a GPU.

#include "mb/engine/Image.h"
#include "mb/game/StringTable.h"
#include "mb/game/TextureIndex.h"
#include "mb/game/UiForm.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace realdata {

inline std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The sizes of the whole image files forms name (the layout of a widget without a size needs them).
class ImageSizes {
public:
    explicit ImageSizes(const std::filesystem::path& root) : textures_(root / "Textures") {}

    mb::Vec2 operator()(std::string_view name) {
        const std::string key = lower(std::filesystem::path(name).replace_extension().generic_string());
        const auto [it, inserted] = sizes_.try_emplace(key);
        if (inserted) {
            const std::filesystem::path path = textures_.find(key);
            if (const std::optional<mb::Image> image = path.empty() ? std::nullopt : mb::loadImage(path)) {
                it->second = {static_cast<float>(image->width), static_cast<float>(image->height)};
            }
        }
        return it->second;
    }

private:
    mb::TextureIndex textures_;
    std::map<std::string, mb::Vec2> sizes_;
};

// Data/Menu/<name>.json, the name in any case.
inline mb::UiForm loadForm(const std::filesystem::path& root, const mb::StringTable& strings, std::string_view name) {
    const std::string wanted = lower(std::string(name));
    for (const auto& entry : std::filesystem::directory_iterator(root / "Data" / "Menu")) {
        if (entry.path().extension() == ".json" && lower(entry.path().stem().string()) == wanted) {
            return mb::UiForm::load(entry.path(), strings);
        }
    }
    throw std::runtime_error("no form " + std::string(name));
}

}  // namespace realdata
