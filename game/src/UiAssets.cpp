#include "mb/game/UiAssets.h"

#include "StringUtil.h"
#include "mb/engine/Image.h"
#include "mb/engine/Log.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace mb {

namespace {

namespace fs = std::filesystem;

// Atlas size per unit of form font_size: see UiAssets::font.
constexpr float kAtlasPerFontSize = 1.25f;

std::string withoutExtension(std::string_view name) {
    return fs::path(name).replace_extension().generic_string();
}

}  // namespace

UiAssets::UiAssets(Renderer& renderer, const fs::path& assetRoot, std::string_view language)
    : renderer_(renderer),
      root_(assetRoot),
      strings_(StringTable::load(assetRoot / "Data" / "xml" / "languages.json")),
      textures_(assetRoot / "Textures") {
    if (!language.empty() && !strings_.setLanguage(language)) {
        logWarn("the string table has no language \"{}\", keeping {}", language, strings_.language());
    }
    std::vector<fs::path> files;
    std::error_code ec, ignored;
    for (fs::directory_iterator it(assetRoot / "Data" / "Fonts", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ignored) && lowerAscii(it->path().extension().string()) == ".json") {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    for (const fs::path& file : files) {
        fonts_.push_back(BitmapFont::load(file, renderer_));
        fontFaces_.push_back(lowerAscii(fonts_.back().face()));
    }
    if (fonts_.empty()) {
        throw std::runtime_error("no converted fonts in " + (assetRoot / "Data" / "Fonts").string());
    }
}

const UiAssets::Image* UiAssets::image(std::string_view name) {
    // The extension is dropped because a name like "forms/button.dds" also names the DDS file next to the PNG
    // converted from it, and TextureIndex would hand that one back; without it the PNG wins.
    const std::string key = lowerAscii(withoutExtension(name));
    const auto [it, inserted] = images_.try_emplace(key);
    if (inserted) {
        const fs::path path = textures_.find(key);
        if (path.empty()) {
            logWarn("UI image \"{}\" not found in {}", name, (root_ / "Textures").string());
        } else if (const std::optional<mb::Image> pixels = loadImage(path)) {
            // No mipmaps: UI images are drawn at about their own size, and a sheet's frames must not blur into each other.
            if (const TextureHandle texture = renderer_.createTexture(*pixels, false)) {
                it->second = Image{texture, pixels->width, pixels->height};
            }
        }
    }
    return it->second ? &*it->second : nullptr;
}

ImageSizeFn UiAssets::imageSizes() {
    return [this](std::string_view name) {
        const Image* found = image(name);
        return found ? Vec2{static_cast<float>(found->width), static_cast<float>(found->height)} : Vec2{};
    };
}

UiAssets::FontChoice UiAssets::font(std::string_view face, int fontSize) {
    const std::string wanted = lowerAscii(face);
    const BitmapFont* best = nullptr;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        float bestDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < fonts_.size(); ++i) {
            const BitmapFont& candidate = fonts_[i];
            if (pass == 0 && fontFaces_[i] != wanted) {
                continue;
            }
            // Ratios, not differences: 20 is as far from 30 as 10 is from 15.
            const float distance = std::fabs(std::log(static_cast<float>(candidate.size()) /
                                                      (static_cast<float>(fontSize) * kAtlasPerFontSize)));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &candidate;
            }
        }
        if (pass == 0 && !best && !warnedFontFace_) {
            warnedFontFace_ = true;
            logWarn("no font atlas for \"{}\", using another face (further cases not logged)", face);
        }
    }
    return {best, best ? static_cast<float>(fontSize) / static_cast<float>(best->size()) : 1.0f};
}

std::vector<std::string> UiAssets::formNames() const {
    std::vector<std::string> names;
    std::error_code ec, ignored;
    for (fs::directory_iterator it(root_ / "Data" / "Menu", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ignored) && lowerAscii(it->path().extension().string()) == ".json") {
            names.push_back(it->path().stem().string());
        }
    }
    std::ranges::sort(names, [](const std::string& a, const std::string& b) { return lowerAscii(a) < lowerAscii(b); });
    return names;
}

UiForm UiAssets::loadForm(std::string_view name) {
    const std::string wanted = lowerAscii(fs::path(name).stem().string());
    const std::vector<std::string> names = formNames();
    std::string available;
    for (const std::string& form : names) {
        if (lowerAscii(form) == wanted) {
            return UiForm::load(root_ / "Data" / "Menu" / (form + ".json"), strings_);
        }
        available += (available.empty() ? "" : ", ") + form;
    }
    throw std::runtime_error(
        std::format("no form named \"{}\" in {} (have: {})", name, (root_ / "Data" / "Menu").string(), available));
}

}  // namespace mb
