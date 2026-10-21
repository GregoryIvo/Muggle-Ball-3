// Loads every converted menu form, font and string against the real game data in $MB3_ASSETS and checks that the
// UI foundation agrees with it: every image and glyph the forms need exists, and (with a GPU) every form draws.
// Skips (exit code 77) without the assets; the drawing half is skipped alone when there is no GPU device.

#include "mb/engine/Image.h"
#include "mb/engine/Log.h"
#include "mb/engine/Renderer.h"
#include "mb/game/BitmapFont.h"
#include "mb/game/StringTable.h"
#include "mb/game/TextureIndex.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiDraw.h"
#include "mb/game/UiForm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<fs::path> jsonFiles(const fs::path& folder) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

// UTF-8 text as code points (the strings in the table are well formed).
std::vector<char32_t> codepoints(const std::string& text) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        char32_t value = length == 1 ? lead : lead & (0xFF >> (length + 1));
        for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
            value = (value << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        out.push_back(value);
        i += length;
    }
    return out;
}

// Data only: no GPU needed.
void checkData(const fs::path& root) {
    const mb::TextureIndex textures(root / "Textures");
    mb::StringTable strings = mb::StringTable::load(root / "Data" / "xml" / "languages.json");
    check(strings.languages().size() >= 2 && strings.find("FontName") != nullptr, "the string table has the languages and a FontName");

    std::vector<mb::BitmapFont> fonts;
    for (const fs::path& file : jsonFiles(root / "Data" / "Fonts")) {
        mb::BitmapFont font;
        try {
            font = mb::BitmapFont::read(file);
        } catch (const std::exception& e) {
            check(false, e.what());
            continue;
        }
        const std::optional<mb::Image> atlas = mb::loadImage(file.parent_path() / font.textureFile());
        check(atlas && atlas->width == font.atlasWidth() && atlas->height == font.atlasHeight(),
              file.filename().string() + ": the atlas PNG exists and has the size the JSON says");
        check(font.glyph(U'A') && font.glyph(U'?') && font.glyph(U' '), file.filename().string() + ": has A, ? and space");
        fonts.push_back(std::move(font));
    }
    check(fonts.size() == 5, "five font atlases");

    // Every string of every language can be shown by its language's font.
    for (const std::string& language : strings.languages()) {
        strings.setLanguage(language);
        const std::string face = strings.resolve("FontName");
        for (const mb::BitmapFont& font : fonts) {
            if (font.face() != face) {
                continue;
            }
            for (const std::string key : {"Loading", "MainMenu", "StartGame", "HighScores", "Info1", "Info2", "Info3", "End1", "End2",
                                          "End3", "Credits2", "Credits3", "Gun", "Skull", "Scores", "PressAnyKey"}) {
                const std::string* text = strings.find(key);
                check(text != nullptr, language + ": string " + key);
                for (const char32_t c : codepoints(text ? *text : "")) {
                    if (c >= 32 && c != U'?') {
                        check(font.glyph(c) != font.glyph(U'?'), language + ": " + key + " has a glyph for every character, in " + face +
                                                                    " " + std::to_string(font.size()));
                    }
                }
            }
        }
    }
    strings.setLanguage("English");

    // Every form loads; every image it names exists; every frame it asks for is in its sheet.
    const std::vector<fs::path> forms = jsonFiles(root / "Data" / "Menu");
    check(forms.size() == 19, "nineteen forms");
    std::set<std::string> formStems;
    for (const fs::path& file : forms) {
        std::string stem = file.stem().string();
        std::ranges::transform(stem, stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        formStems.insert(stem);
    }
    for (const fs::path& file : forms) {
        const std::string name = file.stem().string();
        mb::UiForm form;
        try {
            form = mb::UiForm::load(file, strings);
        } catch (const std::exception& e) {
            check(false, name + ": " + e.what());
            continue;
        }
        check(form.widgets().size() > 1 && form.widgets()[0].kind == mb::WidgetKind::Form, name + ": has widgets under the root form");
        std::set<std::string> missing;
        for (const mb::UiWidget& w : form.widgets()) {
            if (!w.image.empty() && textures.find(fs::path(w.image.name).replace_extension().generic_string()).empty()) {
                missing.insert(w.image.name);
            }
            for (const int frame : w.image.frames) {
                check(frame >= 0 && frame < w.image.columns * w.image.rows, name + ": " + w.name + " asks for a frame its sheet lacks");
            }
            check(w.fontSize > 0 && !w.fontFace.empty(), name + ": " + w.name + " has a font");
            if (w.userData == "Navigate") {  // the name of a navigation button is the form it leads to
                std::string target = w.name;
                std::ranges::transform(target, target.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                check(formStems.contains(target), name + ": the Navigate button " + w.name + " names a form");
            }
        }
        for (const std::string& image : missing) {
            check(false, name + ": no texture for " + image);
        }
    }
    // The widgets the second stage will rely on.
    mb::UiForm menu = mb::UiForm::load(root / "Data" / "Menu" / "MainMenu.json", strings);
    check(menu.name() == "MainMenu" && menu.fadeTimeMs() == 400, "MainMenu: name and fade time");
    const mb::UiWidget* start = menu.find("SelectStage");
    check(start && start->kind == mb::WidgetKind::Button && start->text == "Start Game" && start->userData == "Navigate" &&
              start->soundMouseIn == "menustonein" && start->image.frames == std::vector<int>{0, 1},
          "MainMenu: the Start Game button");
    const mb::UiWidget* logo = menu.find("Logo");
    check(logo && logo->noFading && !logo->interactive(), "MainMenu: the logo does not fade and is not a button");
    check(menu.widgets().size() == 7, "MainMenu: root, logo and five buttons");
    mb::UiForm options = mb::UiForm::load(root / "Data" / "Menu" / "Options.json", strings);
    const mb::UiWidget* music = options.find("MusicVolume");
    check(music && music->kind == mb::WidgetKind::Slider && music->rangeMax == 100 && music->step == 5, "Options: the music slider");
    const mb::UiWidget* fullscreen = options.find("Fullscreen");
    check(fullscreen && fullscreen->kind == mb::WidgetKind::Label && fullscreen->tag == "La1bel", "Options: the misspelt Fullscreen label");
    mb::UiForm select = mb::UiForm::load(root / "Data" / "Menu" / "stagepiratos.json", strings);
    int levelButtons = 0;
    for (const mb::UiWidget& w : select.widgets()) {
        levelButtons += w.userData == "SelectLevel";
    }
    check(levelButtons == 20, "stagepiratos: twenty level buttons");
}

bool differs(const mb::Image& image, const mb::Color& clear) {
    const auto to8 = [](float v) { return static_cast<int>(std::lround(v * 255.0f)); };
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (std::abs(image.rgba[i] - to8(clear.r)) > 2 || std::abs(image.rgba[i + 1] - to8(clear.g)) > 2 ||
            std::abs(image.rgba[i + 2] - to8(clear.b)) > 2) {
            return true;
        }
    }
    return false;
}

// With a GPU: the assets load into textures and every form draws something.
void checkDrawing(const fs::path& root) {
    std::unique_ptr<mb::Renderer> renderer;
    try {
        renderer = std::make_unique<mb::Renderer>();
    } catch (const std::exception& e) {
        std::printf("no GPU device (%s): skipping the drawing checks\n", e.what());
        return;
    }
    mb::UiAssets assets(*renderer, root);
    const std::vector<std::string> names = assets.formNames();
    check(names.size() == 19, "UiAssets lists nineteen forms");
    check(!assets.loadForm("mainmenu").name().empty() && !assets.loadForm("MainMenu.json").name().empty(),
          "UiAssets finds a form ignoring case and the extension");
    try {
        assets.loadForm("nope");
        check(false, "an unknown form throws");
    } catch (const std::exception& e) {
        check(std::string(e.what()).find("MainMenu") != std::string::npos, "the error for an unknown form lists the forms");
    }

    // Font choice: size N from the atlas of 1.25 N, scaled so the text comes out N.
    const auto expectFont = [&](const char* face, int fontSize, int atlasSize, float scale) {
        const mb::UiAssets::FontChoice choice = assets.font(face, fontSize);
        check(choice.font && choice.font->size() == atlasSize && std::fabs(choice.scale - scale) < 1e-4f,
              std::string(face) + " " + std::to_string(fontSize) + " uses the " + std::to_string(atlasSize) + " atlas");
    };
    expectFont("bookworm.ttf", 16, 20, 0.8f);
    expectFont("bookworm.ttf", 24, 30, 0.8f);
    expectFont("bookworm.ttf", 18, 23, 18.0f / 23);
    expectFont("BOOKWORM.TTF", 14, 20, 0.7f);
    expectFont("brushType-semiBold.ttf", 14, 17, 14.0f / 17);
    expectFont("brushType-semiBold.ttf", 16, 20, 0.8f);
    check(assets.font("no-such-face.ttf", 16).font != nullptr, "an unknown face falls back to some font");

    const mb::Color clear{0, 0, 0, 1};
    for (const std::string& name : names) {
        mb::UiForm form = assets.loadForm(name);
        form.layout(mb::UiLayout::fit(1024, 768), assets.imageSizes());
        std::vector<mb::Sprite> sprites;
        mb::drawForm(form, assets, sprites);
        bool valid = true;
        for (const mb::Sprite& s : sprites) {
            valid = valid && s.texture != 0 && s.width > 0 && s.height > 0 && std::isfinite(s.x) && std::isfinite(s.y);
        }
        check(valid, name + ": every sprite has a texture and a finite, non-empty rectangle");
        if (name != "background" && name != "Loading") {
            check(!sprites.empty(), name + ": draws something");
        }
        const mb::Image image = renderer->renderToImage(1024, 768, {}, {}, {}, sprites);
        check(!image.empty() && (sprites.empty() || differs(image, clear)), name + ": renders to an image that is not blank");
    }

    // A button at rest, hovered and pressed: the sprite sheet frame, the growth and the press offset.
    mb::UiForm menu = assets.loadForm("MainMenu");
    const auto layout = mb::UiLayout::fit(1024, 768);
    menu.layout(layout, assets.imageSizes());
    const mb::UiAssets::Image* sheet = assets.image("forms/button.dds");
    check(sheet && sheet->width == 332 && sheet->height == 152, "forms/button.dds is the 332x152 PNG");
    const auto buttonSprite = [&](std::vector<mb::Sprite>& sprites) -> const mb::Sprite* {
        const mb::UiWidget& info = *menu.find("Info");
        for (const mb::Sprite& s : sprites) {
            if (sheet && s.texture == sheet->texture && std::fabs(s.y - info.rect.y) < 6 && s.tint.r > 0.5f) {
                return &s;
            }
        }
        return nullptr;
    };
    std::vector<mb::Sprite> rest, hover, press;
    mb::drawForm(menu, assets, rest);
    const mb::UiWidget& info = *menu.find("Info");
    const mb::Sprite* restSprite = buttonSprite(rest);
    check(restSprite && std::fabs(restSprite->x - info.rect.x) < 1e-3f && std::fabs(restSprite->width - info.rect.w) < 1e-3f &&
              std::fabs(restSprite->uv0.y) < 1e-6f && std::fabs(restSprite->uv1.y - 0.5f) < 1e-6f,
          "a button at rest shows the top half of its sheet over its rectangle");
    menu.updatePointer(info.rect.center(), false);
    mb::drawForm(menu, assets, hover);
    const mb::Sprite* hoverSprite = buttonSprite(hover);
    check(hoverSprite && std::fabs(hoverSprite->uv0.y - 0.5f) < 1e-6f && std::fabs(hoverSprite->uv1.y - 1.0f) < 1e-6f &&
              std::fabs(hoverSprite->x - info.rect.x) < 1e-3f,
          "a hovered button shows the second frame (and MainMenu buttons do not grow)");
    menu.updatePointer(info.rect.center(), true);
    mb::drawForm(menu, assets, press);
    const mb::Sprite* pressSprite = buttonSprite(press);
    check(pressSprite && std::fabs(pressSprite->x - (info.rect.x + 2)) < 1e-3f && std::fabs(pressSprite->y - (info.rect.y + 2)) < 1e-3f,
          "a pressed button moves by its press offset");

    // mouse_over_offset: a hovered Options button grows by 2 reference pixels on every side.
    mb::UiForm options = assets.loadForm("Options");
    options.layout(layout, assets.imageSizes());
    const mb::UiWidget& apply = *options.find("ApplyVideo");
    const mb::UiAssets::Image* small = assets.image("forms/button-small.dds");
    options.updatePointer(apply.rect.center(), false);
    std::vector<mb::Sprite> grown;
    mb::drawForm(options, assets, grown);
    bool found = false;
    for (const mb::Sprite& s : grown) {
        found = found || (small && s.texture == small->texture && std::fabs(s.x - (apply.rect.x - 2)) < 1e-3f &&
                          std::fabs(s.width - (apply.rect.w + 4)) < 1e-3f);
    }
    check(found, "a hovered button with mouse_over_offset grows on every side");

    // Text goes where its placement says. Centred: the block is centred on the button.
    {
        const mb::UiAssets::FontChoice choice = assets.font("bookworm.ttf", 18);  // MainMenu's font_size
        std::vector<const mb::Sprite*> glyphs;
        for (const mb::Sprite& s : rest) {
            if (choice.font && s.texture == choice.font->texture() && s.y >= info.rect.y && s.y < info.rect.bottom() &&
                s.tint.g > 0.9f && s.tint.b < 0.1f) {  // yellow text of this button, not its shadow
                glyphs.push_back(&s);
            }
        }
        float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
        for (const mb::Sprite* g : glyphs) {
            left = std::min(left, g->x);
            right = std::max(right, g->x + g->width);
            top = std::min(top, g->y);
            bottom = std::max(bottom, g->y + g->height);
        }
        check(glyphs.size() == 4, "MainMenu: \"Info\" is four glyph sprites");
        check(std::fabs((left + right) / 2 - info.rect.center().x) < 1.5f && std::fabs((top + bottom) / 2 - info.rect.center().y) < 1.5f,
              "MainMenu: the button's text is centred on it");
    }

    // Multi-line text: wrapped to the rectangle, top edge down, each line inside it.
    {
        mb::UiForm story = assets.loadForm("info");
        story.layout(layout, assets.imageSizes());
        const mb::UiWidget& text = *story.find("Text");
        const mb::UiAssets::FontChoice choice = assets.font(text.fontFace, text.fontSize);
        std::vector<mb::Sprite> sprites;
        mb::drawForm(story, assets, sprites);
        std::set<float> lines;
        bool inside = true;
        for (const mb::Sprite& g : sprites) {
            if (choice.font && g.texture == choice.font->texture() && g.y >= text.rect.y - 1 && g.y < text.rect.bottom()) {
                lines.insert(g.y);
                inside = inside && g.x >= text.rect.x - 1 && g.x + g.width <= text.rect.right() + 1;
            }
        }
        check(lines.size() >= 5 && *lines.begin() == std::round(text.rect.y) && inside,
              "Info: the story wraps into several lines inside its rectangle, from the top edge");
    }

    // Text under a widget, with a negative offset: 120 reference pixels above the bottom of the splash.
    {
        mb::UiForm loading = assets.loadForm("Loading");
        loading.layout(layout, assets.imageSizes());
        mb::UiWidget& bar = *loading.find("Progress");
        bar.value = bar.rangeMin + 0.5f * (bar.rangeMax - bar.rangeMin);
        const mb::UiAssets::FontChoice choice = assets.font("bookworm.ttf", 18);
        std::vector<mb::Sprite> sprites;
        mb::drawForm(loading, assets, sprites);
        float top = 1e9f;
        const mb::Sprite* progress = nullptr;
        const mb::UiAssets::Image* dots = assets.image("progress.dds");
        for (const mb::Sprite& s : sprites) {
            if (choice.font && s.texture == choice.font->texture()) {
                top = std::min(top, s.y);
            }
            if (dots && s.texture == dots->texture) {
                progress = &s;
            }
        }
        check(std::fabs(top - (768 - 120)) < 1.5f, "Loading: the text sits 120 pixels above the bottom edge");
        check(progress && std::fabs(progress->width - bar.rect.w / 2) < 1e-3f && std::fabs(progress->uv1.x - 0.5f) < 1e-6f &&
                  std::fabs(progress->x - bar.rect.x) < 1e-3f,
              "Loading: a half-full progress bar shows the left half of its image");
    }

    // Rotation: degrees clockwise become the sprite's radians; a slider's knob is drawn after its track.
    {
        mb::UiForm levels = assets.loadForm("stagepiratos");
        levels.layout(layout, assets.imageSizes());
        std::vector<mb::Sprite> sprites;
        mb::drawForm(levels, assets, sprites);
        const mb::UiAssets::Image* arrow = assets.image("forms/arrow.dds");
        bool turned = false;
        for (const mb::Sprite& s : sprites) {
            turned = turned || (arrow && s.texture == arrow->texture && std::fabs(s.rotation - mb::radians(-37.51f)) < 1e-5f);
        }
        check(turned, "stagepiratos: an arrow with rotate=-37.51 is drawn turned by that many degrees");

        mb::UiForm settings = assets.loadForm("Options");
        settings.layout(layout, assets.imageSizes());
        mb::UiWidget& music = *settings.find("MusicVolume");
        music.value = 100;
        std::vector<mb::Sprite> drawn;
        mb::drawForm(settings, assets, drawn);
        const mb::UiAssets::Image* knob = assets.image("forms/scull.png");
        const mb::UiAssets::Image* track = assets.image("forms/slider.png");
        long knobAt = -1, trackAt = -1;
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            if (knob && drawn[i].texture == knob->texture && std::fabs(drawn[i].x - music.knobRect().x) < 1e-3f && knobAt < 0) knobAt = static_cast<long>(i);
            if (track && drawn[i].texture == track->texture && std::fabs(drawn[i].x - music.rect.x) < 1e-3f && trackAt < 0) trackAt = static_cast<long>(i);
        }
        check(knobAt >= 0 && trackAt >= 0 && knobAt > trackAt, "Options: the slider's knob, at its value, is drawn over its track");
    }

    // tiled="yes" repeats the picture at its own size across the widget. No shipped form tiles, so make one.
    {
        mb::UiForm tiled = mb::UiForm::parse(R"({"version":1,"tag":"forms","attributes":{},"children":[
            {"tag":"form","attributes":{"name":"T"},"children":[
              {"tag":"label","attributes":{"name":"Bar","pos":[0.1,0.1],"size":[0.5,0.1],"img_name":"forms/uzor.dds","tiled":true},"children":[]}]}]})",
                                           assets.strings());
        tiled.layout(layout, assets.imageSizes());
        std::vector<mb::Sprite> sprites;
        mb::drawForm(tiled, assets, sprites);
        const mb::UiAssets::Image* uzor = assets.image("forms/uzor.dds");
        check(uzor && sprites.size() == 1 && std::fabs(sprites[0].uv1.x - 512.0f / uzor->width) < 1e-4f &&
                  std::fabs(sprites[0].uv1.y - 76.8f / uzor->height) < 1e-4f,
              "a tiled image repeats at its own size across the widget");
    }

    // The fade-in: a faded form is transparent except for the widgets that do not fade (the logo).
    std::vector<mb::Sprite> faded;
    menu.updatePointer({-1, -1}, false);
    mb::UiDrawOptions half;
    half.fade = 0.5f;
    mb::drawForm(menu, assets, faded, half);
    bool logoOpaque = false, othersHalf = false;
    const mb::UiAssets::Image* logo = assets.image("forms/logo.dds");
    for (const mb::Sprite& s : faded) {
        if (logo && s.texture == logo->texture) {
            logoOpaque = logoOpaque || s.tint.a == 1.0f;
        } else if (sheet && s.texture == sheet->texture) {
            othersHalf = othersHalf || std::fabs(s.tint.a - 0.5f) < 1e-6f;
        }
    }
    check(logoOpaque && othersHalf, "fade: the logo stays opaque and the buttons are half transparent");
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_regular_file(root / "Data" / "xml" / "languages.json") || !fs::is_directory(root / "Data" / "Menu")) {
        std::printf("SKIP: converted UI data not found under %s (run tools/convert_assets.py)\n", root.string().c_str());
        return 77;
    }
    try {
        checkData(root);
        checkDrawing(root);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("all UI smoke checks passed");
    }
    return failures == 0 ? 0 : 1;
}
