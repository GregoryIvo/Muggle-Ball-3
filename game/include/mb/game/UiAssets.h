#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/BitmapFont.h"
#include "mb/game/StringTable.h"
#include "mb/game/TextureIndex.h"
#include "mb/game/UiForm.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mb {

// Everything the 2D UI draws with, found below the converted assets folder: the string table, the font atlases,
// the images the forms name, and the forms themselves. Images are uploaded to the GPU on first use. Like all GPU
// resources they live until the Renderer is destroyed, which must outlive this object.
class UiAssets {
public:
    // Reads Data/xml/languages.json and every Data/Fonts/*.json (and their atlases) below `assetRoot`; throws
    // std::runtime_error if they are missing (run tools/convert_assets.py). `language` picks one of the string
    // table's languages; empty keeps the file's own choice.
    UiAssets(Renderer& renderer, const std::filesystem::path& assetRoot, std::string_view language = {});

    Renderer& renderer() { return renderer_; }
    const std::filesystem::path& root() const { return root_; }
    StringTable& strings() { return strings_; }
    const StringTable& strings() const { return strings_; }

    struct Image {
        TextureHandle texture = 0;
        int width = 0;   // of the whole file, which may hold several frames
        int height = 0;
    };
    // An image as a form names it: "forms/button.dds", "splash.jpg", "Forms/Panel.dds". Case, folders and the
    // extension (a DDS is found as the PNG converted from it) follow TextureIndex. Null if there is no such
    // texture, which is logged once.
    const Image* image(std::string_view name);

    // A function for UiForm::layout that answers with the sizes image() knows.
    ImageSizeFn imageSizes();

    struct FontChoice {
        const BitmapFont* font = nullptr;  // null only if there are no fonts at all
        float scale = 1;  // reference pixels per atlas pixel: multiply by UiLayout::scale() for target pixels
    };
    // The atlas to draw text of a form's font_size from. The shipped atlases were rendered for a 1280x960 screen,
    // 1.25 times the 1024x768 the forms are laid out for, so font_size N has an atlas of size 1.25 N (16 and 20,
    // 18 and 23, 24 and 30) and is drawn at scale 0.8. This picks the atlas of the face (case-insensitive, as
    // named by the string table's FontName) nearest to that size, and returns the scale that makes the text come
    // out the size the form asks for when the atlas is not exactly 1.25 N. A face with no atlas falls back to any.
    FontChoice font(std::string_view face, int fontSize);

    // The names (file stems) of the forms in Data/Menu, sorted ignoring case.
    std::vector<std::string> formNames() const;

    // Loads Data/Menu/<name>.json; the name is case-insensitive and may carry the .json or .xml extension.
    // Throws std::runtime_error, listing the forms there are, if there is none.
    UiForm loadForm(std::string_view name);

private:
    Renderer& renderer_;
    std::filesystem::path root_;
    StringTable strings_;
    TextureIndex textures_;
    std::unordered_map<std::string, std::optional<Image>> images_;  // by lowercase name without extension
    std::vector<BitmapFont> fonts_;
    std::vector<std::string> fontFaces_;  // fonts_[i].face() in lowercase
    bool warnedFontFace_ = false;
};

}  // namespace mb
