// Tests for the 2D UI foundation that need neither a GPU nor game data: layout maths, sprite-sheet frames, the
// string table, bitmap font layout and the form model with its hit testing. The forms, fonts and string table are
// written here in the format tools/ui.py produces.

#include "mb/game/BitmapFont.h"
#include "mb/game/StringTable.h"
#include "mb/game/UiForm.h"
#include "mb/game/UiLayout.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using nlohmann::json;
using namespace mb;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec2 a, Vec2 b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance);
}

bool near(const Rect& a, const Rect& b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.w, b.w, tolerance) &&
           near(a.h, b.h, tolerance);
}

bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    } catch (...) {
    }
    return false;
}

// ---------------------------------------------------------------------------
// Layout

void testLayout() {
    const UiLayout native = UiLayout::fit(1024, 768);
    check(near(native.screen(), {0, 0, 1024, 768}) && near(native.scale(), 1), "layout: 1024x768 is the reference screen");

    const UiLayout twice = UiLayout::fit(2048, 1536);
    check(near(twice.screen(), {0, 0, 2048, 1536}) && near(twice.scale(), 2), "layout: a 2x target scales by 2");

    // Wider than 4:3: the height decides, the 4:3 area is centred with bars left and right.
    const UiLayout wide = UiLayout::fit(1600, 768);
    check(near(wide.screen(), {288, 0, 1024, 768}) && near(wide.scale(), 1), "layout: pillarbox");
    const UiLayout wideScaled = UiLayout::fit(1600, 700);
    const float s = 700.0f / 768.0f;
    check(near(wideScaled.scale(), s) && near(wideScaled.screen(), {(1600 - 1024 * s) / 2, 0, 1024 * s, 700}),
          "layout: pillarbox of a smaller height");

    // Taller than 4:3: the width decides, with bars above and below.
    const UiLayout tall = UiLayout::fit(640, 800);
    check(near(tall.scale(), 0.625f) && near(tall.screen(), {0, 160, 640, 480}), "layout: letterbox");

    const UiLayout degenerate = UiLayout::fit(0, 0);
    check(std::isfinite(degenerate.scale()) && degenerate.scale() > 0, "layout: a zero-sized target does not divide by zero");

    // The data's convention: pos is the bottom-left corner as a fraction of the screen, y up.
    const Rect button = native.rect({0.33f, 0.5f}, {0.34f, 0.098f});
    check(near(button, {337.92f, 384 - 75.264f, 348.16f, 75.264f}), "layout: MainMenu button rectangle at 1024x768");
    check(near(native.point({0, 0}), {0, 768}) && near(native.point({1, 1}), {1024, 0}), "layout: y runs up in the data");
    check(near(native.point({0.5f, 0.5f}), {512, 384}), "layout: the centre");
    check(near(wide.point({0, 1}), {288, 0}) && near(wide.point({1, 0}), {1312, 768}),
          "layout: the 4:3 area's corners on a wide target");
    check(near(tall.rect({0, 0}, {1, 1}), tall.screen()), "layout: the unit square is the whole screen area");
    check(near(twice.rect({0.5f, 0.5f}, {0.25f, 0.25f}), {1024, 384, 512, 384}), "layout: sizes scale with the target");

    const Rect r{10, 20, 30, 40};
    check(r.contains({10, 20}) && r.contains({39.9f, 59.9f}) && !r.contains({40, 30}) && !r.contains({20, 60}) &&
              !r.contains({9.9f, 30}) && !r.contains({20, 19.9f}),
          "rect: left and top edges are inside, right and bottom are not");
    check(near(r.center(), {25, 40}) && r.right() == 40 && r.bottom() == 60, "rect: centre and edges");
}

// ---------------------------------------------------------------------------
// Sprite sheets

void testFrames() {
    using S = UiState;
    const std::vector<int> normalHover{0, 1};
    check(sheetFrame(normalHover, S::Normal) == 0 && sheetFrame(normalHover, S::Hover) == 1, "frames: normal and hover");
    check(sheetFrame(normalHover, S::Pressed) == 1 && sheetFrame(normalHover, S::Disabled) == 1,
          "frames: missing states repeat the last frame listed");
    const std::vector<int> levelDot{1, 2, 2, 0};
    check(sheetFrame(levelDot, S::Normal) == 1 && sheetFrame(levelDot, S::Hover) == 2 &&
              sheetFrame(levelDot, S::Pressed) == 2 && sheetFrame(levelDot, S::Disabled) == 0,
          "frames: all four states");
    check(sheetFrame({}, S::Pressed) == 0 && sheetFrame({}, S::Normal) == 0, "frames: no frames listed means frame 0");
    check(sheetFrame({1}, S::Disabled) == 1, "frames: one frame for every state");

    // A 2 x 3 sheet (the bats): frames run down the first column, then down the second.
    check(near(sheetFrameUv(2, 3, 0).uv0, {0, 0}) && near(sheetFrameUv(2, 3, 0).uv1, {0.5f, 1.0f / 3}),
          "uv: 2x3 sheet, frame 0 is top-left");
    check(near(sheetFrameUv(2, 3, 2).uv0, {0, 2.0f / 3}) && near(sheetFrameUv(2, 3, 2).uv1, {0.5f, 1}),
          "uv: 2x3 sheet, frame 2 is the bottom of the first column");
    check(near(sheetFrameUv(2, 3, 3).uv0, {0.5f, 0}) && near(sheetFrameUv(2, 3, 3).uv1, {1, 1.0f / 3}),
          "uv: 2x3 sheet, frame 3 is the top of the second column");
    check(near(sheetFrameUv(2, 3, 5).uv0, {0.5f, 2.0f / 3}) && near(sheetFrameUv(2, 3, 5).uv1, {1, 1}),
          "uv: 2x3 sheet, frame 5 is bottom-right");
    check(near(sheetFrameUv(1, 2, 1).uv0, {0, 0.5f}) && near(sheetFrameUv(1, 2, 1).uv1, {1, 1}),
          "uv: 1x2 sheet (the main menu button), frame 1 is the lower half");
    check(near(sheetFrameUv(2, 1, 1).uv0, {0.5f, 0}) && near(sheetFrameUv(2, 1, 1).uv1, {1, 1}),
          "uv: 2x1 sheet (the checkbox), frame 1 is the right half");
    check(near(sheetFrameUv(1, 1, 0).uv0, {0, 0}) && near(sheetFrameUv(1, 1, 0).uv1, {1, 1}),
          "uv: a single picture is the whole texture");
    check(near(sheetFrameUv(2, 3, 99).uv1, {1, 1}) && near(sheetFrameUv(2, 3, -4).uv0, {0, 0}),
          "uv: a frame number outside the sheet is clamped into it");
}

// ---------------------------------------------------------------------------
// String table

constexpr const char* kStrings = R"({
  "version": 1, "current": "Second",
  "languages": [
    {"name": "First", "codepage": "cp1252", "strings": {"FontName": "face.ttf", "Hello": "Hello", "OnlyFirst": "one"}},
    {"name": "Second", "codepage": "cp1251", "strings": {"FontName": "other.ttf", "Hello": "Привет"}}
  ]})";

void testStrings() {
    StringTable table = StringTable::parse(kStrings);
    check(table.language() == "Second", "strings: the file's current language is used");
    check(table.languages() == std::vector<std::string>{"First", "Second"}, "strings: languages in file order");
    check(table.find("Hello") && *table.find("Hello") == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
          "strings: UTF-8 text");
    check(table.find("OnlyFirst") && *table.find("OnlyFirst") == "one", "strings: a missing key falls back to the first language");
    check(!table.find("Nothing") && !table.find("hello"), "strings: unknown keys, and keys are case-sensitive");

    check(table.setLanguage("FIRST") && table.language() == "First", "strings: languages are picked ignoring case");
    check(*table.find("Hello") == "Hello", "strings: the new language's text");
    check(!table.setLanguage("Third") && table.language() == "First", "strings: an unknown language changes nothing");

    check(table.resolve("Hello") == "Hello" && table.resolve("FontName") == "face.ttf", "strings: a key resolves to its text");
    check(table.resolve("Quick Start") == "Quick Start" && table.resolve("1000") == "1000" && table.resolve("") == "",
          "strings: anything else is literal text");

    check(StringTable::parse(R"({"languages":[{"name":"A","strings":{}}]})").language() == "A",
          "strings: no current language means the first");
    check(StringTable::parse(R"({"current":"Nope","languages":[{"name":"A","strings":{}}]})").language() == "A",
          "strings: an unknown current language means the first");
    check(throwsWith([] { StringTable::parse("nope"); }, "invalid string table JSON"), "strings: bad JSON throws");
    check(throwsWith([] { StringTable::parse(R"({"languages":[]})"); }, "no languages"), "strings: no languages throws");
    check(throwsWith([] { StringTable::parse(R"({"version":2,"languages":[]})"); }, "version"), "strings: unknown version throws");
    check(throwsWith([] { StringTable::load("/nonexistent/languages.json"); }, "languages.json"),
          "strings: a missing file throws with its path");
}

// ---------------------------------------------------------------------------
// Bitmap font

// A 64x64 atlas of 16-pixel-high cells. Glyph 'A' is 10 wide at (0,0), 'B' 8 wide at (10,0), ' ' 5 wide at
// (18,0), '?' 6 wide at (24,0), 'W' 12 wide at (30,0); the code point U+0416 is glyph 200, 7 wide at (42,0).
// Every other glyph is the '?' cell, and glyph indices 0..127 stand for the same code points.
json fontJson(bool withQuestionMark = true) {
    json glyphs = json::array(), codepoints = json::array();
    for (int i = 0; i < 256; ++i) {
        glyphs.push_back({24, 0, 6, 16});
        codepoints.push_back(i < 128 ? json(i) : json(nullptr));
    }
    glyphs[65] = {0, 0, 10, 16};
    glyphs[66] = {10, 0, 8, 16};
    glyphs[32] = {18, 0, 5, 16};
    glyphs[87] = {30, 0, 12, 16};
    glyphs[200] = {42, 0, 7, 16};
    codepoints[200] = 0x416;
    if (!withQuestionMark) {
        codepoints[63] = nullptr;
    }
    return {{"version", 1},
            {"face", "test.ttf"},
            {"size", 20},
            {"texture", "test.ttf_20.png"},
            {"atlas", {{"width", 64}, {"height", 64}}},
            {"lineHeight", 16},
            {"codepage", "cp1252"},
            {"codepoints", codepoints},
            {"glyphs", glyphs}};
}

BitmapFont testFont(bool withQuestionMark = true) {
    return BitmapFont::parse(fontJson(withQuestionMark).dump());
}

void testFontData() {
    const BitmapFont font = testFont();
    check(font.face() == "test.ttf" && font.size() == 20 && font.lineHeight() == 16 && font.atlasWidth() == 64 &&
              font.atlasHeight() == 64 && font.textureFile() == "test.ttf_20.png" && font.texture() == 0,
          "font: header fields; the texture is not set by parse()");
    const BitmapFont::Glyph* a = font.glyph(U'A');
    check(a && a->x == 0 && a->y == 0 && a->w == 10 && a->h == 16, "font: glyph rectangle");
    check(font.glyph(0x416) && font.glyph(0x416)->w == 7, "font: a code point maps to its glyph through the table");
    check(font.glyph(0x4FF) == font.glyph(U'?') && font.glyph(U'?')->w == 6, "font: an unknown code point is drawn as '?'");
    check(testFont(false).glyph(0x4FF) == nullptr, "font: ... and as nothing if the font has no '?'");

    check(throwsWith([] { BitmapFont::parse("{"); }, "invalid font JSON"), "font: bad JSON throws");
    json bad = fontJson();
    bad["glyphs"][3] = {60, 0, 10, 16};
    check(throwsWith([&] { BitmapFont::parse(bad.dump()); }, "glyph 3"), "font: a glyph outside the atlas throws");
    bad = fontJson();
    bad["glyphs"].erase(bad["glyphs"].begin());
    check(throwsWith([&] { BitmapFont::parse(bad.dump()); }, "256"), "font: a short glyph table throws");
    bad = fontJson();
    bad["version"] = 2;
    check(throwsWith([&] { BitmapFont::parse(bad.dump()); }, "version"), "font: an unknown version throws");
    bad = fontJson();
    bad["size"] = 0;
    check(throwsWith([&] { BitmapFont::parse(bad.dump()); }, "size"), "font: a zero size throws");
}

void testFontWidthAndWrap() {
    const BitmapFont font = testFont();
    check(near(font.width(""), 0) && near(font.width("A"), 10) && near(font.width("AB"), 18) &&
              near(font.width("A B"), 23),
          "font: a line is as wide as its glyph cells");
    check(near(font.width("\xD0\x96"), 7) && near(font.width("A\xD0\x96"), 17), "font: UTF-8 two-byte code points");
    check(near(font.width("\xFF"), 6) && near(font.width("\xD0"), 6) && near(font.width("A\xE2\x82"), 22),
          "font: bad UTF-8 is one '?' per bad byte");
    check(near(font.width("\xE2\x82\xAC"), 6), "font: a three-byte code point the font lacks is one '?'");
    check(near(font.width("A\x01" "B"), 18), "font: control characters have no width");

    using Lines = std::vector<std::string>;
    check(font.wrap("AB AB AB", 0) == Lines{"AB AB AB"}, "wrap: a zero width never wraps");
    check(font.wrap("AB AB AB", 40) == Lines{"AB", "AB", "AB"} && font.wrap("AB AB AB", 41) == Lines{"AB AB", "AB"},
          "wrap: words fill a line up to the limit, inclusive");
    check(font.wrap("AB AB AB", 1000) == Lines{"AB AB AB"}, "wrap: text that fits stays on one line");
    check(font.wrap("A\nB", 0) == Lines{"A", "B"} && font.wrap("A\nB", 1000) == Lines{"A", "B"},
          "wrap: a newline always breaks");
    check(font.wrap("A\n\nB", 1000) == Lines{"A", "", "B"} && font.wrap("A\n", 0) == Lines{"A", ""},
          "wrap: empty lines are kept");
    check(font.wrap("", 100) == Lines{""} && font.wrap("", 0) == Lines{""}, "wrap: no text is one empty line");
    check(font.wrap("ABABAB AB", 20) == Lines{"ABABAB", "AB"}, "wrap: a word wider than the limit gets its own line");
    check(font.wrap("AB  AB", 100) == Lines{"AB  AB"}, "wrap: runs of spaces inside a line are kept");
    check(font.wrap("AB  AB", 20) == Lines{"AB", "AB"}, "wrap: a break at a run of spaces leaves no stray space");
    check(font.wrap("AB AB", 18) == Lines{"AB", "AB"}, "wrap: the space at a break is dropped");

    TextParams params;
    params.scale = 0.5f;
    check(near(font.measure("AB AB", params), {20.5f, 8}), "measure: width and height scale");
    params.scale = 2;
    params.maxWidth = 80;  // 40 atlas pixels: "AB AB" does not fit
    check(near(font.measure("AB AB", params), {36, 64}), "measure: wrapped text is as wide as its widest line and two lines high");
}

void testFontDraw() {
    const BitmapFont font = testFont();
    std::vector<Sprite> out;
    TextParams params;
    params.color = {1, 0.5f, 0.25f, 0.75f};
    font.draw(out, "AB A", {100, 50}, params);
    check(out.size() == 3, "draw: one sprite per glyph, none for the space");
    if (out.size() == 3) {
        check(near(out[0].x, 100) && near(out[0].y, 50) && near(out[0].width, 10) && near(out[0].height, 16),
              "draw: first glyph at the anchor");
        check(near(out[1].x, 110) && near(out[1].width, 8), "draw: the next glyph starts where the last ended");
        check(near(out[2].x, 123) && near(out[2].width, 10), "draw: a space advances the pen without a sprite");
        check(near(out[0].uv0, {0, 0}) && near(out[0].uv1, {10.0f / 64, 16.0f / 64}), "draw: UVs are the atlas rectangle");
        check(near(out[1].uv0, {10.0f / 64, 0}) && near(out[1].uv1, {18.0f / 64, 16.0f / 64}), "draw: second glyph's UVs");
        check(out[0].tint.r == 1 && out[0].tint.g == 0.5f && out[0].tint.b == 0.25f && out[0].tint.a == 0.75f &&
                  out[0].rotation == 0,
              "draw: the text colour is the tint");
    }

    // Alignment moves the block, not the anchor's meaning: centre and right put the anchor in the middle and at
    // the end of each line.
    params.align = HAlign::Center;
    out.clear();
    font.draw(out, "AB", {100, 0}, params);
    check(out.size() == 2 && near(out[0].x, 91) && near(out[1].x, 101), "draw: centred text");
    params.align = HAlign::Right;
    out.clear();
    font.draw(out, "AB", {100, 0}, params);
    check(out.size() == 2 && near(out[0].x, 82) && near(out[1].x, 92), "draw: right-aligned text");
    params.align = HAlign::Center;
    out.clear();
    font.draw(out, "AB\nA", {100, 0}, params);
    check(out.size() == 3 && near(out[0].x, 91) && near(out[0].y, 0) && near(out[2].x, 95) && near(out[2].y, 16),
          "draw: each line is aligned on its own and the next sits one line height lower");

    // Wrapping at maxWidth (target pixels) and the drop shadow, which comes first and moves right and down.
    params = {};
    params.maxWidth = 20;
    params.shadowOffset = {2, 3};
    params.shadowColor = {0, 0, 0, 0.5f};
    out.clear();
    font.draw(out, "A B", {10, 10}, params);
    check(out.size() == 4, "draw: a shadow doubles the sprites");
    if (out.size() == 4) {
        check(near(out[0].x, 12) && near(out[0].y, 13) && out[0].tint.a == 0.5f && out[0].tint.r == 0,
              "draw: shadow sprites come first, offset, in the shadow colour");
        check(near(out[1].x, 12) && near(out[1].y, 29), "draw: the shadow wraps like the text");
        check(near(out[2].x, 10) && near(out[2].y, 10) && out[2].tint.a == 1 && near(out[3].y, 26),
              "draw: then the text itself");
    }

    // Scaling: edges snap to whole pixels independently, so neighbours meet without gaps or overlaps.
    params = {};
    params.scale = 0.8f;
    out.clear();
    font.draw(out, "AAAA", {0.3f, 0.4f}, params);
    bool snapped = out.size() == 4;
    for (std::size_t i = 0; i < out.size(); ++i) {
        snapped = snapped && out[i].x == std::round(out[i].x) && out[i].width == std::round(out[i].width) &&
                  out[i].y == std::round(out[i].y) && out[i].height == std::round(out[i].height);
        if (i > 0) {
            snapped = snapped && near(out[i].x, out[i - 1].x + out[i - 1].width);
        }
    }
    check(snapped, "draw: glyph quads are whole pixels and tile exactly");
    check(out.size() == 4 && near(out[0].height, 13) && near(out[3].x + out[3].width, std::round(0.3f + 32)),
          "draw: the scale applies to widths and heights");

    out.clear();
    font.draw(out, "", {0, 0}, {});
    font.draw(out, "  ", {0, 0}, {});
    check(out.empty(), "draw: no text, no sprites");
}

// ---------------------------------------------------------------------------
// Forms

// The converter's output for a small form (see tests/test_ui.py for the XML it comes from): a form with an
// inherited font and a Panel holding a button, a disabled button, a checkbox, a slider with a track child, a
// textless label and a text label.
json element(const char* tag, json attributes, json children = json::array()) {
    return {{"tag", tag}, {"attributes", std::move(attributes)}, {"children", std::move(children)}};
}

json formJson() {
    const json button = element("button", {{"name", "Play"},
                                           {"pos", {0.1, 0.5}},
                                           {"size", {0.2, 0.1}},
                                           {"img_name", "forms/button.dds"},
                                           {"img_params", {1, 2, 0, 1}},
                                           {"text", "Hello"},
                                           {"font_size", 24},
                                           {"mouse_over_offset", 3.0},
                                           {"press_offset", {2.0, 2.0}},
                                           {"user_data", "Navigate"},
                                           {"sound_mouse_in", "in"},
                                           {"sound_mouse_down", "down"}});
    const json locked = element("button", {{"name", "Locked"},
                                           {"pos", {0.1, 0.3}},
                                           {"size", {0.2, 0.1}},
                                           {"img_name", "forms/dot.dds"},
                                           {"img_params", {1, 3, 1, 2, 2, 0}},
                                           {"disabled", true}});
    const json box = element("checkbox", {{"name", "Sound"},
                                          {"pos", {0.5, 0.5}},
                                          {"size", {0.05, 0.05}},
                                          {"img_name", "forms/checkbox.png"},
                                          {"img_params", {2, 1, 1, 0}},
                                          {"text", "Quick Start"},
                                          {"text_align", "right"},
                                          {"text_offset", 1.0}});
    const json track = element("label", {{"name", "SliderBg"}, {"pos", {0.4, 0.2}}, {"size", {0.15, 0.05}}, {"img_name", "forms/slider.png"}});
    const json slider = element("slider", {{"name", "Volume"},
                                           {"pos", {0.4, 0.2}},
                                           {"size", {0.1, 0.05}},
                                           {"size_pos", {0.03, 0.05}},
                                           {"img_name", "forms/knob.png"},
                                           {"params", {0.0, 100.0, 5.0}}},
                                    json::array({track}));
    const json natural = element("La1bel", {{"name", "Natural"}, {"pos", {0.7, 0.1}}, {"size", {0.0, 0.0}}, {"img_name", "forms/two.png"}, {"img_params", {2, 1, 1, 0}}});
    const json unknown = element("blink", {{"name", "Mystery"}}, json::array({element("label", {{"name", "Inside"}})}));
    const json caption = element("label", {{"name", "Caption"}, {"pos", {0.5, 0.9}}, {"text", "NoSuchKey"}, {"text_align", "center"},
                                           {"font_name", "other.ttf"}, {"font_color", {1, 0, 0, 1}}, {"font_shadow", {1, 1}},
                                           {"shadow_color", {0, 0, 0, 0.5}}, {"rotate", 30.0}, {"no_fading", true}});
    const json list = element("list", {{"name", "Modes"}, {"text", "%s"}});
    const json panel = element("label", {{"name", "Panel"}, {"pos", {0.0, 0.0}}, {"size", {1.0, 1.0}}, {"img_name", "forms/panel.dds"}},
                               json::array({button, locked, box, slider, natural, unknown, caption, list}));
    const json form = element("form", {{"name", "Test"},
                                       {"font_name", "FontName"},
                                       {"font_size", 16},
                                       {"font_color", {1, 1, 0, 1}},
                                       {"font_shadow", {2.0, 2.0}},
                                       {"fade_time_msec", 400}},
                              json::array({panel}));
    return {{"version", 1}, {"tag", "forms"}, {"attributes", json::object()}, {"children", json::array({form})}};
}

// Image sizes for layout: the button sheet is 332x152 (1x2), the dot sheet 36x108 (1x3), the checkbox 88x40 (2x1).
Vec2 imageSizes(std::string_view name) {
    if (name == "forms/button.dds") return {332, 152};
    if (name == "forms/dot.dds") return {36, 108};
    if (name == "forms/checkbox.png") return {88, 40};
    if (name == "forms/two.png") return {88, 40};
    if (name == "forms/knob.png") return {32, 44};
    return {};
}

UiWidget* widget(UiForm& form, const char* name) {
    UiWidget* w = form.find(name);
    check(w != nullptr, name);
    return w ? w : &form.widgets().front();
}

void testFormParse() {
    const StringTable strings = StringTable::parse(kStrings);  // current language "Second": FontName is other.ttf
    UiForm form = UiForm::parse(formJson().dump(), strings);
    check(form.name() == "Test" && form.fadeTimeMs() == 400, "form: name and fade time");
    const char* order[] = {"Test", "Panel", "Play", "Locked", "Sound", "Volume", "SliderBg", "Natural", "Caption", "Modes"};
    bool inOrder = form.widgets().size() == 10;  // the unknown <blink> and the label inside it are not widgets
    for (std::size_t i = 0; i < 10 && inOrder; ++i) {
        inOrder = form.widgets()[i].name == order[i];
    }
    check(inOrder, "form: widgets in document order (the drawing order), parents before children");

    UiWidget& root = form.widgets()[0];
    check(root.kind == WidgetKind::Form && root.parent == -1 && root.end == static_cast<int>(form.widgets().size()),
          "form: the root widget covers everything");
    UiWidget& panel = *widget(form, "Panel");
    check(panel.kind == WidgetKind::Label && panel.parent == 0 && panel.image.name == "forms/panel.dds" &&
              panel.image.columns == 1 && panel.image.rows == 1 && panel.image.frames.empty(),
          "form: a label with a plain image");
    UiWidget& play = *widget(form, "Play");
    check(play.kind == WidgetKind::Button && play.parent == 1 && play.end == 3 && panel.end == 10,
          "form: parent and subtree ranges: a button inside the panel");
}

void testFormAttributes() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    UiWidget& play = *widget(form, "Play");
    check(near(play.pos, {0.1f, 0.5f}) && near(play.size, {0.2f, 0.1f}), "attributes: pos and size");
    check(play.image.columns == 1 && play.image.rows == 2 && play.image.frames == std::vector<int>{0, 1},
          "attributes: img_params are the grid then the frames");
    check(play.textKey == "Hello" && play.text == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
          "attributes: text is resolved through the string table");
    check(near(play.mouseOverOffset, 3) && near(play.pressOffset, {2, 2}) && play.userData == "Navigate" &&
              play.soundMouseIn == "in" && play.soundMouseDown == "down",
          "attributes: offsets, user data and sounds");
    check(play.enabled && play.visible && !play.hovered && !play.pressed && !play.checked,
          "attributes: a button starts enabled and idle");
    check(play.textPlacement == TextPlacement::Center && play.textOffset == 0, "attributes: text placement defaults to centred");

    // Fonts pass down from the form; a widget's own settings override them and pass on to its children.
    check(play.fontFace == "other.ttf" && play.fontSize == 24 && near(play.fontColor.r, 1) && near(play.fontColor.b, 0) &&
              near(play.fontShadow, {2, 2}),
          "attributes: font settings: size overridden, face, colour and shadow inherited");
    check(widget(form, "Panel")->fontSize == 16 && widget(form, "Panel")->fontFace == "other.ttf",
          "attributes: font_name \"FontName\" is looked up in the string table");
    UiWidget& caption = *widget(form, "Caption");
    check(caption.text == "NoSuchKey" && caption.fontFace == "other.ttf" && near(caption.fontColor.g, 0) &&
              near(caption.fontShadow, {1, 1}) && near(caption.shadowColor.a, 0.5f) && near(caption.rotation, 30) &&
              caption.noFading,
          "attributes: literal text, a literal font name, colours, rotation and no_fading");

    UiWidget& locked = *widget(form, "Locked");
    check(!locked.enabled && locked.image.frames == std::vector<int>{1, 2, 2, 0}, "attributes: disabled and four frames");
    UiWidget& box = *widget(form, "Sound");
    check(box.kind == WidgetKind::Checkbox && box.textPlacement == TextPlacement::Right && near(box.textOffset, 1) &&
              box.text == "Quick Start",
          "attributes: checkbox text on the right");
    UiWidget& natural = *widget(form, "Natural");
    check(natural.kind == WidgetKind::Label && natural.tag == "La1bel", "attributes: the misspelt tag is a label, kept as written");
    UiWidget& slider = *widget(form, "Volume");
    check(slider.kind == WidgetKind::Slider && near(slider.rangeMin, 0) && near(slider.rangeMax, 100) &&
              near(slider.step, 5) && near(slider.value, 0) && near(slider.sizePos, {0.03f, 0.05f}),
          "attributes: slider range, step and knob size");
    check(widget(form, "Modes")->kind == WidgetKind::List && widget(form, "Modes")->text == "%s", "attributes: a list keeps its format text");
    check(slider.end == 7 && widget(form, "SliderBg")->parent == 5, "attributes: a slider's subtree is itself and its track");
    check(form.find("Inside") == nullptr && form.find("Mystery") == nullptr, "attributes: an unknown element is skipped with its contents");
    check(form.find("Nope") == nullptr, "attributes: find returns null for an unknown name");

    const UiForm& constForm = form;
    check(constForm.find("Play") == &play, "attributes: find on a const form");
}

void testFormErrors() {
    const StringTable strings = StringTable::parse(kStrings);
    check(throwsWith([&] { UiForm::parse("not json", strings); }, "invalid form JSON"), "form: bad JSON throws");
    check(throwsWith([&] { UiForm::parse(R"({"version":1,"tag":"page","attributes":{},"children":[]})", strings); }, "<forms>"),
          "form: a document that is not <forms> throws");
    check(throwsWith([&] { UiForm::parse(R"({"version":1,"tag":"forms","attributes":{},"children":[]})", strings); }, "<form>"),
          "form: an empty <forms> throws");
    check(throwsWith([&] { UiForm::parse(R"({"version":9,"tag":"forms","attributes":{},"children":[]})", strings); }, "version"),
          "form: an unknown version throws");
    check(throwsWith([&] { UiForm::load("/nonexistent/Form.json", strings); }, "Form.json"), "form: a missing file throws with its path");

    // A value the converter could not type stays a string; the loader uses the default and carries on.
    json odd = formJson();
    odd["children"][0]["children"][0]["children"][0]["attributes"]["pos"] = "left";
    odd["children"][0]["children"][0]["children"][0]["attributes"]["img_params"] = "1 x";
    UiForm form = UiForm::parse(odd.dump(), strings);
    check(near(widget(form, "Play")->pos, {0, 0}) && widget(form, "Play")->image.frames.empty() &&
              widget(form, "Play")->image.columns == 1,
          "form: malformed values fall back to the defaults");
}

void testFormLayout() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    form.layout(UiLayout::fit(1024, 768), imageSizes);
    UiWidget& play = *widget(form, "Play");
    check(near(play.rect, {102.4f, 384 - 76.8f, 204.8f, 76.8f}), "layout: a button's rectangle");
    check(near(widget(form, "Panel")->rect, {0, 0, 1024, 768}), "layout: the panel covers the screen");

    // A widget with an image and no size is one frame of the image, its bottom-left corner on `pos`.
    UiWidget& natural = *widget(form, "Natural");
    check(near(natural.rect, {716.8f, 768 * 0.9f - 40, 44, 40}), "layout: no size means the size of one frame");
    check(near(widget(form, "Caption")->rect, {512, 768 * 0.1f, 0, 0}), "layout: a widget with no size and no image is a point");

    // The slider's rectangle is the knob's run; the knob is size_pos wide, and stays inside the track.
    UiWidget& slider = *widget(form, "Volume");
    check(near(slider.rect, {409.6f, 768 * 0.8f - 38.4f, 102.4f, 38.4f}) && near(slider.knobSize, {30.72f, 38.4f}),
          "layout: slider rectangle and knob size");
    slider.value = 0;
    check(near(slider.knobRect(), {409.6f, 768 * 0.8f - 38.4f, 30.72f, 38.4f}), "layout: the knob at the minimum");
    slider.value = 100;
    check(near(slider.knobRect(), {512, 768 * 0.8f - 38.4f, 30.72f, 38.4f}), "layout: the knob at the maximum");
    slider.value = 50;
    check(near(slider.fraction(), 0.5f) && near(slider.knobRect().x, 460.8f), "layout: the knob at the middle");
    check(near(slider.hitRect(), {409.6f, 768 * 0.8f - 38.4f, 102.4f + 30.72f, 38.4f}), "layout: the slider's hit area includes the knob");

    // Another target scales and moves everything with the screen.
    form.layout(UiLayout::fit(1600, 768), imageSizes);
    check(near(play.rect, {288 + 102.4f, 384 - 76.8f, 204.8f, 76.8f}), "layout: pillarbox shifts a widget right");
    form.layout(UiLayout::fit(512, 384), imageSizes);
    check(near(play.rect, {51.2f, 192 - 38.4f, 102.4f, 38.4f}) && near(natural.rect.w, 22) && near(slider.knobSize, {15.36f, 19.2f}),
          "layout: half size halves everything, natural sizes included");
    check(near(form.screen().scale(), 0.5f), "layout: the form remembers the screen it was laid out for");

    // Without image sizes (images not found) a natural-size widget has no area.
    form.layout(UiLayout::fit(1024, 768), [](std::string_view) { return Vec2{}; });
    check(widget(form, "Natural")->rect.w == 0, "layout: an unknown image leaves a sizeless widget empty");
}

void testWidgetState() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    UiWidget& play = *widget(form, "Play");
    check(play.state() == UiState::Normal && play.frame() == 0, "state: normal frame");
    play.hovered = true;
    check(play.state() == UiState::Hover && play.frame() == 1, "state: hover frame");
    play.pressed = true;
    check(play.state() == UiState::Pressed && play.frame() == 1, "state: pressed repeats the last frame of a two-frame sheet");
    play.enabled = false;
    check(play.state() == UiState::Disabled && play.frame() == 1, "state: disabled wins over pressed");

    UiWidget& locked = *widget(form, "Locked");
    check(locked.state() == UiState::Disabled && locked.frame() == 0, "state: a disabled level dot shows frame 0");
    locked.enabled = true;
    check(locked.frame() == 1, "state: enabled, the green frame");
    locked.hovered = true;
    check(locked.frame() == 2, "state: hover, the highlighted frame");

    UiWidget& box = *widget(form, "Sound");
    check(box.frame() == 1, "state: an unchecked box shows the first frame listed");
    box.checked = true;
    check(box.frame() == 0, "state: a checked box shows the second");
    box.hovered = true;
    check(box.frame() == 0, "state: a checkbox ignores hover");

    check(play.interactive() && box.interactive() && widget(form, "Volume")->interactive() &&
              !widget(form, "Panel")->interactive() && !widget(form, "Modes")->interactive() &&
              !widget(form, "SliderBg")->interactive(),
          "state: buttons, checkboxes and sliders can be touched; labels and lists cannot");
}

void testSliderValues() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    form.layout(UiLayout::fit(1024, 768), imageSizes);
    const UiWidget& slider = *widget(form, "Volume");
    const float left = slider.rect.x, knob = slider.knobSize.x;
    check(near(slider.valueAt(left + knob / 2), 0), "slider: the knob's centre at the left end means the minimum");
    check(near(slider.valueAt(left - 50), 0) && near(slider.valueAt(left + 1000), 100), "slider: clamped to the range");
    check(near(slider.valueAt(left + knob / 2 + slider.rect.w), 100), "slider: the knob's centre at the far end means the maximum");
    check(near(slider.valueAt(left + knob / 2 + slider.rect.w * 0.5f), 50), "slider: the middle");
    check(near(slider.valueAt(left + knob / 2 + slider.rect.w * 0.27f), 25), "slider: values snap to the step (27% is 25)");
    check(near(slider.valueAt(left + knob / 2 + slider.rect.w * 0.33f), 35), "slider: ... and round to the nearest one (33% is 35)");

    UiWidget bar;
    bar.kind = WidgetKind::Progress;
    bar.rangeMin = 0;
    bar.rangeMax = 10;
    bar.value = 3;
    check(near(bar.fraction(), 0.3f), "progress: fraction of the range");
    bar.value = 12;
    check(near(bar.fraction(), 1), "progress: clamped");
    bar.rangeMax = 0;
    check(near(bar.fraction(), 0), "progress: an empty range is zero");
}

void testHitTesting() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    form.layout(UiLayout::fit(1024, 768), imageSizes);
    UiWidget& play = *widget(form, "Play");
    UiWidget& locked = *widget(form, "Locked");
    const Vec2 onPlay = play.rect.center();

    check(form.hitTest(onPlay) == &play, "hit: a point on the button finds it, not the panel behind");
    check(form.hitTest({1000, 10}) == nullptr, "hit: labels are never found");
    check(form.hitTest({play.rect.x, play.rect.y}) == &play && form.hitTest({play.rect.right(), play.rect.y}) == nullptr,
          "hit: the left and top edges are inside, the right edge is not");
    check(form.hitTest(locked.rect.center()) == nullptr, "hit: a disabled widget is not found");
    locked.enabled = true;
    check(form.hitTest(locked.rect.center()) == &locked, "hit: ... until it is enabled");
    play.visible = false;
    check(form.hitTest(onPlay) == nullptr, "hit: an invisible widget is not found");
    play.visible = true;
    widget(form, "Panel")->visible = false;
    check(form.hitTest(onPlay) == nullptr, "hit: ... nor anything inside an invisible widget");
    widget(form, "Panel")->visible = true;

    // Overlapping widgets: the later one is on top.
    UiWidget& box = *widget(form, "Sound");
    box.rect = play.rect;
    check(form.hitTest(onPlay) == &box, "hit: the later widget in drawing order wins where they overlap");

    UiWidget& slider = *widget(form, "Volume");
    slider.value = 100;
    check(form.hitTest({slider.rect.right() + 20, slider.rect.center().y}) == &slider,
          "hit: the slider answers over the knob's overhang past its run");
}

void testPointer() {
    const StringTable strings = StringTable::parse(kStrings);
    UiForm form = UiForm::parse(formJson().dump(), strings);
    form.layout(UiLayout::fit(1024, 768), imageSizes);
    UiWidget& play = *widget(form, "Play");
    UiWidget& box = *widget(form, "Sound");
    const Vec2 onPlay = play.rect.center(), onBox = box.rect.center(), nowhere{1000, 10};

    PointerEvents e = form.updatePointer(nowhere, false);
    check(!e.entered && !e.pressed && !e.clicked && !play.hovered, "pointer: nothing under the pointer, nothing happens");
    e = form.updatePointer(onPlay, false);
    check(e.entered == &play && play.hovered && !play.pressed && !e.pressed && !e.clicked, "pointer: moving onto a button enters it");
    e = form.updatePointer(onPlay, false);
    check(!e.entered && play.hovered, "pointer: staying on it enters nothing more");
    e = form.updatePointer(onPlay, true);
    check(e.pressed == &play && play.pressed && play.hovered && !e.clicked && play.state() == UiState::Pressed,
          "pointer: the button going down on it presses it");
    e = form.updatePointer(onPlay, true);
    check(!e.pressed && play.pressed, "pointer: holding it presses it only once");
    e = form.updatePointer(onPlay, false);
    check(e.clicked == &play && !play.pressed && play.hovered, "pointer: releasing over the pressed widget clicks it");

    // Press, drag off, release elsewhere: no click; the press shows only while the pointer is over the widget.
    form.updatePointer(onPlay, true);
    e = form.updatePointer(nowhere, true);
    check(!play.pressed && !play.hovered && !e.clicked && !e.entered, "pointer: dragging off un-presses it");
    e = form.updatePointer(onPlay, true);
    check(play.pressed && play.hovered && e.entered == &play && !e.pressed,
          "pointer: dragging back on shows it pressed again, without a new press");
    form.updatePointer(nowhere, true);
    e = form.updatePointer(nowhere, false);
    check(!e.clicked && !play.pressed, "pointer: releasing away from it clicks nothing");

    // Press on one widget, release over another: neither is clicked.
    form.updatePointer(onPlay, true);
    form.updatePointer(onBox, true);
    e = form.updatePointer(onBox, false);
    check(!e.clicked && !box.pressed, "pointer: releasing over a different widget clicks nothing");

    // Pressing while already holding down elsewhere (drag onto a button) does not press it.
    form.updatePointer(nowhere, false);
    form.updatePointer(nowhere, true);
    e = form.updatePointer(onPlay, true);
    check(!e.pressed && !play.pressed && e.entered == &play, "pointer: dragging onto a button with the button held presses nothing");
    e = form.updatePointer(onPlay, false);
    check(!e.clicked, "pointer: ... and releasing there is no click");

    // Moving between widgets leaves the old one.
    form.updatePointer(onPlay, false);
    e = form.updatePointer(onBox, false);
    check(!play.hovered && box.hovered && e.entered == &box, "pointer: moving to another widget hovers that one");
    form.updatePointer(onPlay, false);
    check(play.hovered, "pointer: hovering a button");
    play.enabled = false;
    form.updatePointer(onPlay, false);
    check(!play.hovered, "pointer: disabling the hovered widget un-hovers it");
}

}  // namespace

int main() {
    testLayout();
    testFrames();
    testStrings();
    testFontData();
    testFontWidthAndWrap();
    testFontDraw();
    testFormParse();
    testFormAttributes();
    testFormErrors();
    testFormLayout();
    testWidgetState();
    testSliderValues();
    testHitTesting();
    testPointer();
    if (failures == 0) {
        std::puts("all UI tests passed");
    }
    return failures == 0 ? 0 : 1;
}
