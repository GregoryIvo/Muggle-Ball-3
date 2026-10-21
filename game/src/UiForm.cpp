#include "mb/game/UiForm.h"

#include "StringUtil.h"
#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

using nlohmann::json;

// The font settings a widget inherits from the widget around it.
struct FontStyle {
    std::string nameKey = "FontName";
    int size = 16;
    Color color{1, 1, 1, 1};
    Vec2 shadow;
};

// frames[index], repeating the last entry for an index past the end; 0 for no frames.
int frameAt(const std::vector<int>& frames, std::size_t index) {
    return frames.empty() ? 0 : frames[std::min(index, frames.size() - 1)];
}

std::optional<WidgetKind> kindOf(const std::string& tag) {
    const std::string lower = lowerAscii(tag);
    if (lower == "form") return WidgetKind::Form;
    if (lower == "label" || lower == "la1bel") return WidgetKind::Label;  // "La1bel" is the data's one misspelt tag
    if (lower == "button") return WidgetKind::Button;
    if (lower == "checkbox") return WidgetKind::Checkbox;
    if (lower == "slider") return WidgetKind::Slider;
    if (lower == "list") return WidgetKind::List;
    if (lower == "textedit") return WidgetKind::TextEdit;
    if (lower == "progress") return WidgetKind::Progress;
    return std::nullopt;
}

TextPlacement placementOf(const std::string& name) {
    if (name == "center_multiline") return TextPlacement::CenterMultiline;
    if (name == "right") return TextPlacement::Right;
    if (name == "above") return TextPlacement::Above;
    if (name == "under") return TextPlacement::Under;
    if (name == "bottom") return TextPlacement::Bottom;
    if (name != "center") {
        logWarn("unknown text_align \"{}\", centring the text", name);
    }
    return TextPlacement::Center;
}

// Reads the typed attributes written by tools/ui.py. A value of the wrong type (the converter keeps text it could
// not parse) is reported and replaced by the default.
class Attributes {
public:
    Attributes(const json& element, std::string_view form) : attributes_(element.at("attributes")), form_(form) {
        name_ = string("name", "");
    }

    std::string string(const char* key, std::string fallback) const {
        const auto it = attributes_.find(key);
        return it != attributes_.end() && it->is_string() ? it->get<std::string>() : fallback;
    }

    float number(const char* key, float fallback) const {
        const auto it = attributes_.find(key);
        if (it == attributes_.end()) return fallback;
        if (!it->is_number()) return bad(key, fallback);
        return it->get<float>();
    }

    bool boolean(const char* key, bool fallback) const {
        const auto it = attributes_.find(key);
        if (it == attributes_.end()) return fallback;
        if (!it->is_boolean()) return bad(key, fallback);
        return it->get<bool>();
    }

    Vec2 vec2(const char* key, Vec2 fallback) const {
        const auto it = attributes_.find(key);
        if (it == attributes_.end()) return fallback;
        if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
            return bad(key, fallback);
        }
        return {(*it)[0].get<float>(), (*it)[1].get<float>()};
    }

    Color color(const char* key, Color fallback) const {
        const auto it = attributes_.find(key);
        if (it == attributes_.end()) return fallback;
        if (!it->is_array() || it->size() != 4) return bad(key, fallback);
        for (const json& channel : *it) {
            if (!channel.is_number()) return bad(key, fallback);
        }
        return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
    }

    std::vector<float> numbers(const char* key) const {
        const auto it = attributes_.find(key);
        if (it == attributes_.end()) return {};
        if (!it->is_array()) return bad(key, std::vector<float>{});
        for (const json& value : *it) {
            if (!value.is_number()) return bad(key, std::vector<float>{});
        }
        return it->get<std::vector<float>>();
    }

    const std::string& name() const { return name_; }

private:
    template <class T>
    T bad(const char* key, T fallback) const {
        logWarn("{}: {} of \"{}\" is not valid, using the default", form_, key, name_);
        return fallback;
    }

    const json& attributes_;
    std::string_view form_;
    std::string name_;
};

SpriteSheet parseSheet(const Attributes& attributes) {
    SpriteSheet sheet;
    sheet.name = attributes.string("img_name", "");
    sheet.tiled = attributes.boolean("tiled", false);
    const std::vector<float> params = attributes.numbers("img_params");
    if (params.size() >= 2 && params[0] >= 1 && params[1] >= 1) {
        sheet.columns = static_cast<int>(params[0]);
        sheet.rows = static_cast<int>(params[1]);
        for (std::size_t i = 2; i < params.size(); ++i) {
            sheet.frames.push_back(static_cast<int>(params[i]));
        }
    } else if (!params.empty()) {
        logWarn("img_params of \"{}\" need a grid size, using one frame", attributes.name());
    }
    return sheet;
}

class Builder {
public:
    Builder(std::vector<UiWidget>& widgets, const StringTable& strings, std::string_view form)
        : widgets_(widgets), strings_(strings), form_(form) {}

    // Adds the element and everything inside it; unknown elements are skipped with their contents.
    void add(const json& element, int parent, FontStyle font) {
        const std::string tag = element.at("tag").get<std::string>();
        const std::optional<WidgetKind> kind = kindOf(tag);
        if (!kind) {
            logWarn("{}: skipping the unknown element <{}>", form_, tag);
            return;
        }
        const Attributes a(element, form_);

        UiWidget w;
        w.kind = *kind;
        w.tag = tag;
        w.name = a.name();
        w.parent = parent;
        w.pos = a.vec2("pos", {});
        w.size = a.vec2("size", {});
        w.sizePos = a.vec2("size_pos", {});
        w.color = a.color("color", w.color);
        w.shadowColor = a.color("shadow_color", w.shadowColor);
        w.shadowOffset = a.vec2("shadow_offset", {});
        w.pressOffset = a.vec2("press_offset", {});
        w.mouseOverOffset = a.number("mouse_over_offset", 0);
        w.rotation = a.number("rotate", 0);
        w.image = parseSheet(a);
        w.textKey = a.string("text", "");
        w.text = strings_.resolve(w.textKey);
        w.textPlacement = placementOf(a.string("text_align", "center"));
        w.textOffset = a.number("text_offset", 0);
        w.userData = a.string("user_data", "");
        w.soundMouseIn = a.string("sound_mouse_in", "");
        w.soundMouseDown = a.string("sound_mouse_down", "");
        w.noFading = a.boolean("no_fading", false);
        w.cycled = a.boolean("cycled", false);
        w.enabled = !a.boolean("disabled", false);

        // Font settings pass down to everything inside.
        font.nameKey = a.string("font_name", font.nameKey);
        font.size = static_cast<int>(a.number("font_size", static_cast<float>(font.size)));
        font.color = a.color("font_color", font.color);
        font.shadow = a.vec2("font_shadow", font.shadow);
        w.fontFace = strings_.resolve(font.nameKey);
        w.fontSize = font.size;
        w.fontColor = font.color;
        w.fontShadow = font.shadow;

        const std::vector<float> params = a.numbers("params");
        if (params.size() >= 2) {
            w.rangeMin = params[0];
            w.rangeMax = params[1];
            w.step = params.size() >= 3 ? params[2] : 0.0f;
        }
        w.value = w.rangeMin;

        const int index = static_cast<int>(widgets_.size());
        widgets_.push_back(std::move(w));
        for (const json& child : element.at("children")) {
            add(child, index, font);
        }
        widgets_[static_cast<std::size_t>(index)].end = static_cast<int>(widgets_.size());
    }

private:
    std::vector<UiWidget>& widgets_;
    const StringTable& strings_;
    std::string_view form_;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open form: " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

}  // namespace

int sheetFrame(const std::vector<int>& frames, UiState state) {
    return frameAt(frames, static_cast<std::size_t>(state));
}

FrameUv sheetFrameUv(int columns, int rows, int frame) {
    columns = std::max(columns, 1);
    rows = std::max(rows, 1);
    frame = std::clamp(frame, 0, columns * rows - 1);
    const float column = static_cast<float>(frame / rows);
    const float row = static_cast<float>(frame % rows);
    const float cols = static_cast<float>(columns), rws = static_cast<float>(rows);
    return {{column / cols, row / rws}, {(column + 1) / cols, (row + 1) / rws}};
}

UiState UiWidget::state() const {
    if (!enabled) return UiState::Disabled;
    if (pressed) return UiState::Pressed;
    if (hovered) return UiState::Hover;
    return UiState::Normal;
}

int UiWidget::frame() const {
    if (kind == WidgetKind::Checkbox) {
        return frameAt(image.frames, checked ? 1 : 0);
    }
    return sheetFrame(image.frames, state());
}

bool UiWidget::interactive() const {
    return kind == WidgetKind::Button || kind == WidgetKind::Checkbox || kind == WidgetKind::Slider ||
           kind == WidgetKind::TextEdit;
}

float UiWidget::fraction() const {
    if (rangeMax <= rangeMin) {
        return 0;
    }
    return std::clamp((value - rangeMin) / (rangeMax - rangeMin), 0.0f, 1.0f);
}

Rect UiWidget::knobRect() const {
    return {rect.x + fraction() * rect.w, rect.center().y - knobSize.y * 0.5f, knobSize.x, knobSize.y};
}

float UiWidget::valueAt(float pixelX) const {
    const float t = rect.w > 0 ? std::clamp((pixelX - knobSize.x * 0.5f - rect.x) / rect.w, 0.0f, 1.0f) : 0.0f;
    float v = rangeMin + t * (rangeMax - rangeMin);
    if (step > 0) {
        v = rangeMin + std::round((v - rangeMin) / step) * step;
    }
    return std::clamp(v, rangeMin, std::max(rangeMin, rangeMax));
}

Rect UiWidget::hitRect() const {
    if (kind != WidgetKind::Slider) {
        return rect;
    }
    return {rect.x, std::min(rect.y, knobRect().y), rect.w + knobSize.x, std::max(rect.h, knobSize.y)};
}

UiForm UiForm::load(const std::filesystem::path& path, const StringTable& strings) {
    try {
        return parse(readFile(path), strings);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path.string(), e.what()));
    }
}

UiForm UiForm::parse(std::string_view text, const StringTable& strings) {
    UiForm form;
    try {
        const json root = json::parse(text);
        if (const int version = root.value("version", 1); version != 1) {
            throw std::runtime_error(std::format("unsupported form version {}", version));
        }
        if (root.at("tag") != "forms" || root.at("children").empty() || root.at("children")[0].at("tag") != "form") {
            throw std::runtime_error("expected a <forms> element holding a <form>");
        }
        const json& element = root.at("children")[0];
        form.name_ = Attributes(element, "").name();
        Builder(form.widgets_, strings, form.name_).add(element, -1, {});
        form.fadeTimeMs_ = static_cast<int>(Attributes(element, form.name_).number("fade_time_msec", 0));
        if (root.at("children").size() > 1) {
            logWarn("{}: only the first of {} forms in the file is used", form.name_, root.at("children").size());
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid form JSON: {}", e.what()));
    }
    return form;
}

UiWidget* UiForm::find(std::string_view name) {
    const auto it = std::ranges::find_if(widgets_, [&](const UiWidget& w) { return w.name == name; });
    return it == widgets_.end() ? nullptr : &*it;
}

const UiWidget* UiForm::find(std::string_view name) const {
    const auto it = std::ranges::find_if(widgets_, [&](const UiWidget& w) { return w.name == name; });
    return it == widgets_.end() ? nullptr : &*it;
}

void UiForm::layout(const UiLayout& screen, const ImageSizeFn& imageSizes) {
    screen_ = screen;
    for (UiWidget& w : widgets_) {
        w.rect = screen.rect(w.pos, w.size);
        const bool slider = w.kind == WidgetKind::Slider;
        if (slider) {
            // The slider's own rectangle is the run of the knob, whose size is `size_pos`.
            w.knobSize = {w.sizePos.x * screen.screen().w, w.sizePos.y * screen.screen().h};
        }
        // Without a size of its own a widget is as big as one frame of its image.
        const bool unsized = slider ? w.knobSize.x == 0 || w.knobSize.y == 0 : w.size.x == 0 && w.size.y == 0;
        if (w.image.empty() || !unsized) {
            continue;
        }
        const Vec2 whole = imageSizes(w.image.name);
        const Vec2 frame{whole.x / static_cast<float>(w.image.columns) * screen.scale(),
                         whole.y / static_cast<float>(w.image.rows) * screen.scale()};
        if (slider) {
            w.knobSize = frame;
        } else if (frame.x > 0) {
            w.rect = {w.rect.x, w.rect.y - frame.y, frame.x, frame.y};  // pos is the bottom-left corner
        }
    }
}

UiWidget* UiForm::hitTest(Vec2 pixel) {
    for (int i = static_cast<int>(widgets_.size()) - 1; i >= 0; --i) {
        UiWidget& w = widgets_[static_cast<std::size_t>(i)];
        if (!w.interactive() || !w.enabled || !w.hitRect().contains(pixel)) {
            continue;
        }
        bool shown = true;
        for (int p = i; p >= 0 && shown; p = widgets_[static_cast<std::size_t>(p)].parent) {
            shown = widgets_[static_cast<std::size_t>(p)].visible;
        }
        if (shown) {
            return &w;
        }
    }
    return nullptr;
}

PointerEvents UiForm::updatePointer(Vec2 pixel, bool buttonDown) {
    PointerEvents events;
    const auto indexOf = [&](const UiWidget* w) { return w ? static_cast<int>(w - widgets_.data()) : -1; };

    UiWidget* over = hitTest(pixel);
    const int overIndex = indexOf(over);
    if (overIndex != hovered_) {
        if (hovered_ >= 0) {
            widgets_[static_cast<std::size_t>(hovered_)].hovered = false;
        }
        if (over) {
            over->hovered = true;
            events.entered = over;
        }
        hovered_ = overIndex;
    }

    if (buttonDown && !buttonDown_ && over) {
        pressed_ = overIndex;
        events.pressed = over;
    }
    if (!buttonDown && buttonDown_ && pressed_ >= 0) {
        if (pressed_ == overIndex) {
            events.clicked = over;
        }
        pressed_ = -1;
    }
    buttonDown_ = buttonDown;
    for (std::size_t i = 0; i < widgets_.size(); ++i) {
        widgets_[i].pressed = static_cast<int>(i) == pressed_ && pressed_ == hovered_;
    }
    return events;
}

}  // namespace mb
