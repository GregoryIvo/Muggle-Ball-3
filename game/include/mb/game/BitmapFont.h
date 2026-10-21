#pragma once

#include "mb/engine/Renderer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mb {

enum class HAlign { Left, Center, Right };

// How a block of text is laid out and drawn. Lengths are in target pixels.
struct TextParams {
    float scale = 1;     // target pixels per atlas pixel
    float maxWidth = 0;  // wrap lines wider than this at spaces; 0 never wraps
    HAlign align = HAlign::Left;
    Color color{1, 1, 1, 1};
    Vec2 shadowOffset{0, 0};  // a drop shadow of the whole text, this far right and down; (0, 0) draws none
    Color shadowColor{0, 0, 0, 1};
};

// One pre-rendered font atlas: a PNG with 256 glyph cells and a table of their rectangles, converted from the
// game's `<face>_<size>.crd` by tools/ui.py (Data/Fonts/<face>_<size>.json). The atlas was rendered at one size,
// so text drawn from it at another scale is resampled.
//
// Every glyph cell is `lineHeight()` tall and as wide as the glyph's advance, so text is laid out by placing
// the cells side by side; there is no kerning. Text is UTF-8; each code point is looked up through the table
// that maps it to the font's code page (Windows-1252 or 1251), and one the font cannot show is drawn as '?'.
class BitmapFont {
public:
    // A glyph's rectangle in the atlas, in pixels.
    struct Glyph {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    static BitmapFont parse(std::string_view json);  // throws std::runtime_error; the texture is left unset (0)
    static BitmapFont read(const std::filesystem::path& json);  // parse() of a file, naming the file in errors
    // read() and then upload the PNG atlas named in the JSON, which sits next to the JSON file.
    static BitmapFont load(const std::filesystem::path& json, Renderer& renderer);

    const std::string& face() const { return face_; }  // e.g. "bookworm.ttf"
    int size() const { return size_; }                 // the size the atlas was rendered at: the number in its file name
    int lineHeight() const { return lineHeight_; }     // atlas pixels
    TextureHandle texture() const { return texture_; }
    int atlasWidth() const { return atlasWidth_; }
    int atlasHeight() const { return atlasHeight_; }
    const std::string& textureFile() const { return textureFile_; }

    // The glyph drawn for a code point (a '?' if the font has none for it), null if even that is missing.
    const Glyph* glyph(char32_t codepoint) const;

    // Width of one line of text, in atlas pixels (the sum of the glyphs' widths).
    float width(std::string_view line) const;

    // Splits text into lines at '\n' and, when maxWidth (atlas pixels) is above zero, at spaces so that no line
    // is wider than that: a word wider than the limit gets a line to itself. A space at a break is dropped.
    std::vector<std::string> wrap(std::string_view text, float maxWidth) const;

    // The size of the text block in target pixels: the widest line by lines times the line height.
    Vec2 measure(std::string_view text, const TextParams& params) const;

    // Appends one sprite per visible glyph, the drop shadow's (if any) before the text's. `anchor` is where the
    // block sits horizontally according to params.align: its left edge, centre or right edge; and its top edge
    // vertically. Glyph quads snap to whole target pixels.
    void draw(std::vector<Sprite>& out, std::string_view text, Vec2 anchor, const TextParams& params) const;

private:
    void drawLines(std::vector<Sprite>& out, const std::vector<std::string>& lines, Vec2 anchor, const TextParams& params,
                   Color color) const;

    std::string face_;
    int size_ = 0;
    int lineHeight_ = 0;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;
    std::string textureFile_;
    TextureHandle texture_ = 0;
    std::array<Glyph, 256> glyphs_{};
    std::unordered_map<char32_t, std::uint8_t> glyphOf_;  // code point -> glyph index
};

}  // namespace mb
