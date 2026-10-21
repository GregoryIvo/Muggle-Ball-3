#include "mb/game/BitmapFont.h"

#include "mb/engine/Image.h"
#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// The next code point of UTF-8 text, advancing `text` past it. A malformed sequence gives one replacement
// character per bad byte.
char32_t nextCodepoint(std::string_view& text) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(0);
    std::size_t length = 1;
    char32_t value = lead;
    if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        value = lead & 0x07;
    } else if (lead >= 0xE0 && lead < 0xF0) {
        length = 3;
        value = lead & 0x0F;
    } else if (lead >= 0xC2 && lead < 0xE0) {
        length = 2;
        value = lead & 0x1F;
    } else if (lead >= 0x80) {
        text.remove_prefix(1);  // a stray continuation byte or a lead byte that is never valid
        return kReplacement;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if (i >= text.size() || (byte(i) & 0xC0) != 0x80) {
            text.remove_prefix(1);
            return kReplacement;
        }
        value = (value << 6) | (byte(i) & 0x3F);
    }
    text.remove_prefix(length);
    return value;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open font: " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

}  // namespace

BitmapFont BitmapFont::parse(std::string_view text) {
    using nlohmann::json;
    BitmapFont font;
    try {
        const json root = json::parse(text);
        if (const int version = root.value("version", 1); version != 1) {
            throw std::runtime_error(std::format("unsupported font version {}", version));
        }
        font.face_ = root.at("face").get<std::string>();
        font.size_ = root.at("size").get<int>();
        font.textureFile_ = root.at("texture").get<std::string>();
        font.atlasWidth_ = root.at("atlas").at("width").get<int>();
        font.atlasHeight_ = root.at("atlas").at("height").get<int>();
        font.lineHeight_ = root.at("lineHeight").get<int>();
        if (font.size_ <= 0 || font.atlasWidth_ <= 0 || font.atlasHeight_ <= 0 || font.lineHeight_ <= 0) {
            throw std::runtime_error("the font has no size");
        }
        const json& rects = root.at("glyphs");
        const json& codepoints = root.at("codepoints");
        if (rects.size() != font.glyphs_.size() || codepoints.size() != font.glyphs_.size()) {
            throw std::runtime_error("a font has 256 glyphs");
        }
        for (std::size_t i = 0; i < font.glyphs_.size(); ++i) {
            Glyph& glyph = font.glyphs_[i];
            glyph = {rects[i].at(0).get<int>(), rects[i].at(1).get<int>(), rects[i].at(2).get<int>(),
                     rects[i].at(3).get<int>()};
            if (glyph.x < 0 || glyph.y < 0 || glyph.w <= 0 || glyph.h <= 0 || glyph.x + glyph.w > font.atlasWidth_ ||
                glyph.y + glyph.h > font.atlasHeight_) {
                throw std::runtime_error(std::format("glyph {} lies outside the atlas", i));
            }
            if (!codepoints[i].is_null()) {
                font.glyphOf_.emplace(codepoints[i].get<char32_t>(), static_cast<std::uint8_t>(i));
            }
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid font JSON: {}", e.what()));
    }
    return font;
}

BitmapFont BitmapFont::read(const std::filesystem::path& jsonPath) {
    try {
        return parse(readFile(jsonPath));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", jsonPath.string(), e.what()));
    }
}

BitmapFont BitmapFont::load(const std::filesystem::path& jsonPath, Renderer& renderer) {
    BitmapFont font = read(jsonPath);
    const std::filesystem::path atlas = jsonPath.parent_path() / font.textureFile_;
    const std::optional<Image> image = loadImage(atlas);
    if (!image || image->width != font.atlasWidth_ || image->height != font.atlasHeight_) {
        throw std::runtime_error(std::format("{}: the atlas {} is missing or not {}x{}", jsonPath.string(),
                                             atlas.string(), font.atlasWidth_, font.atlasHeight_));
    }
    font.texture_ = renderer.createTexture(*image, false);
    if (font.texture_ == 0) {
        throw std::runtime_error("cannot create the texture of " + atlas.string());
    }
    return font;
}

const BitmapFont::Glyph* BitmapFont::glyph(char32_t codepoint) const {
    auto found = glyphOf_.find(codepoint);
    if (found == glyphOf_.end()) {
        found = glyphOf_.find(U'?');
    }
    return found == glyphOf_.end() ? nullptr : &glyphs_[found->second];
}

float BitmapFont::width(std::string_view line) const {
    float total = 0;
    while (!line.empty()) {
        const char32_t codepoint = nextCodepoint(line);
        if (codepoint >= 32) {
            if (const Glyph* g = glyph(codepoint)) {
                total += static_cast<float>(g->w);
            }
        }
    }
    return total;
}

std::vector<std::string> BitmapFont::wrap(std::string_view text, float maxWidth) const {
    std::vector<std::string> lines;
    std::size_t paragraph = 0;
    while (paragraph <= text.size()) {
        const std::size_t end = std::min(text.find('\n', paragraph), text.size());
        const std::string_view source = text.substr(paragraph, end - paragraph);
        paragraph = end + 1;
        if (maxWidth <= 0) {
            lines.emplace_back(source);
            continue;
        }

        // Greedy fill. Splitting on single spaces keeps runs of spaces inside a line, and an empty word at the
        // start of a line (the leftover of a break) is skipped.
        std::string line;
        std::size_t start = 0;
        while (start <= source.size()) {
            const std::size_t space = std::min(source.find(' ', start), source.size());
            const std::string_view word = source.substr(start, space - start);
            start = space + 1;
            if (line.empty()) {
                line = word;
            } else if (width(line) + width(" ") + width(word) <= maxWidth) {
                line += ' ';
                line += word;
            } else {
                lines.push_back(std::move(line));
                line = word;
            }
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

Vec2 BitmapFont::measure(std::string_view text, const TextParams& params) const {
    const std::vector<std::string> lines = wrap(text, params.maxWidth / params.scale);
    float widest = 0;
    for (const std::string& line : lines) {
        widest = std::max(widest, width(line));
    }
    return {widest * params.scale, static_cast<float>(lines.size()) * static_cast<float>(lineHeight_) * params.scale};
}

void BitmapFont::drawLines(std::vector<Sprite>& out, const std::vector<std::string>& lines, Vec2 anchor,
                           const TextParams& params, Color color) const {
    const float lineHeight = static_cast<float>(lineHeight_) * params.scale;
    float y = anchor.y;
    for (const std::string& line : lines) {
        const float lineWidth = width(line) * params.scale;
        float pen = anchor.x;
        if (params.align == HAlign::Center) {
            pen -= lineWidth * 0.5f;
        } else if (params.align == HAlign::Right) {
            pen -= lineWidth;
        }
        const float top = std::round(y);
        std::string_view rest = line;
        while (!rest.empty()) {
            const char32_t codepoint = nextCodepoint(rest);
            const Glyph* g = codepoint >= 32 ? glyph(codepoint) : nullptr;
            if (!g) {
                continue;
            }
            const float advance = static_cast<float>(g->w) * params.scale;
            if (codepoint != U' ') {
                // Edges snap to whole pixels independently, so neighbouring cells meet without gaps.
                const float left = std::round(pen);
                Sprite sprite;
                sprite.x = left;
                sprite.y = top;
                sprite.width = std::round(pen + advance) - left;
                sprite.height = std::round(y + lineHeight) - top;
                sprite.texture = texture_;
                sprite.uv0 = {static_cast<float>(g->x) / static_cast<float>(atlasWidth_),
                              static_cast<float>(g->y) / static_cast<float>(atlasHeight_)};
                sprite.uv1 = {static_cast<float>(g->x + g->w) / static_cast<float>(atlasWidth_),
                              static_cast<float>(g->y + g->h) / static_cast<float>(atlasHeight_)};
                sprite.tint = color;
                out.push_back(sprite);
            }
            pen += advance;
        }
        y += lineHeight;
    }
}

void BitmapFont::draw(std::vector<Sprite>& out, std::string_view text, Vec2 anchor, const TextParams& params) const {
    const std::vector<std::string> lines = wrap(text, params.maxWidth / params.scale);
    if (params.shadowOffset.x != 0 || params.shadowOffset.y != 0) {
        drawLines(out, lines, {anchor.x + params.shadowOffset.x, anchor.y + params.shadowOffset.y}, params,
                  params.shadowColor);
    }
    drawLines(out, lines, anchor, params, params.color);
}

}  // namespace mb
