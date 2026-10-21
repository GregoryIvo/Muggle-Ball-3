#include "FormViewer.h"

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiDraw.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mb_app {

namespace {

using Clock = std::chrono::steady_clock;

// A form on screen, with the background form behind it unless it stands on its own.
class Screen {
public:
    Screen(mb::UiAssets& assets, bool background) : assets_(assets), names_(assets.formNames()), background_(background) {}

    // `query` is a file name from Data/Menu, any case.
    void show(const std::string& query) {
        form_ = assets_.loadForm(query);  // throws if there is no such form
        const auto stem = std::ranges::find_if(names_, [&](const std::string& n) { return sameName(n, query); });
        index_ = static_cast<std::size_t>(stem - names_.begin());
        withBackground_ = background_ && !sameName(file(), "Loading") && !sameName(file(), "background");
        if (withBackground_ && backdrop_.widgets().empty()) {
            backdrop_ = assets_.loadForm("background");
        }
        showcase(form_);
        shown_ = Clock::now();
    }

    // The next (or previous) form in Data/Menu, wrapping around.
    void showRelative(int step) {
        const auto count = static_cast<int>(names_.size());
        show(names_[static_cast<std::size_t>((static_cast<int>(index_) + step + count) % count)]);
    }

    const std::string& file() const { return names_[index_]; }

    // Lays the forms out for the target and draws them.
    std::vector<mb::Sprite> draw(int width, int height, mb::Vec2 pointer, bool down, bool fadeIn) {
        const mb::UiLayout layout = mb::UiLayout::fit(width, height);
        form_.layout(layout, assets_.imageSizes());
        const mb::PointerEvents events = form_.updatePointer(pointer, down);
        if (events.clicked) {
            mb::logInfo("clicked \"{}\" (user_data \"{}\")", events.clicked->name, events.clicked->userData);
        }
        mb::UiDrawOptions options;
        if (fadeIn && form_.fadeTimeMs() > 0) {
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - shown_).count();
            options.fade = static_cast<float>(std::min(elapsed / form_.fadeTimeMs(), 1.0));
        }
        std::vector<mb::Sprite> sprites;
        if (withBackground_) {
            backdrop_.layout(layout, assets_.imageSizes());
            mb::drawForm(backdrop_, assets_, sprites);
        }
        mb::drawForm(form_, assets_, sprites, options);
        return sprites;
    }

    // The centre, in pixels of a target, of the first widget with this name; empty if there is none.
    std::optional<mb::Vec2> centerOf(const std::string& widgetName, int width, int height) {
        form_.layout(mb::UiLayout::fit(width, height), assets_.imageSizes());
        const mb::UiWidget* widget = form_.find(widgetName);
        return widget ? std::optional<mb::Vec2>(widget->rect.center()) : std::nullopt;
    }

private:
    // Fills in state the game would set, so that every frame of the widgets can be seen: checkboxes alternate
    // between checked and not, sliders sit at the middle, progress bars are 60% full.
    static void showcase(mb::UiForm& form) {
        bool checked = true;
        for (mb::UiWidget& w : form.widgets()) {
            if (w.kind == mb::WidgetKind::Checkbox) {
                w.checked = checked;
                checked = !checked;
            } else if (w.kind == mb::WidgetKind::Slider) {
                w.value = (w.rangeMin + w.rangeMax) * 0.5f;
            } else if (w.kind == mb::WidgetKind::Progress) {
                w.value = w.rangeMin + 0.6f * (w.rangeMax - w.rangeMin);
            }
        }
    }

    static bool sameName(const std::string& a, const std::string& b) {
        return a.size() == b.size() &&
               std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    mb::UiAssets& assets_;
    std::vector<std::string> names_;
    bool background_;
    bool withBackground_ = false;
    std::size_t index_ = 0;
    mb::UiForm form_;
    mb::UiForm backdrop_;
    Clock::time_point shown_ = Clock::now();
};

int writeScreenshot(const FormViewerOptions& o) {
    mb::Renderer renderer;
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::UiAssets assets(renderer, mb::assetRoot(), o.language);
    Screen screen(assets, o.background);
    screen.show(o.form);
    // The pointer rests on the widget asked for, or off the screen.
    const std::string& target = !o.press.empty() ? o.press : o.hover;
    mb::Vec2 pointer{-1, -1};
    if (!target.empty()) {
        const std::optional<mb::Vec2> at = screen.centerOf(target, o.width, o.height);
        if (!at) {
            throw std::runtime_error("the form " + screen.file() + " has no widget named \"" + target + "\"");
        }
        pointer = *at;
    }
    const std::vector<mb::Sprite> sprites = screen.draw(o.width, o.height, pointer, !o.press.empty(), false);
    const mb::Image image = renderer.renderToImage(o.width, o.height, {}, {}, {}, sprites);
    if (image.empty() || !mb::writePng(o.screenshot, image)) {
        mb::logError("cannot write screenshot {}", o.screenshot);
        return 1;
    }
    mb::logInfo("wrote {} ({}x{}, {} sprites)", o.screenshot, image.width, image.height, sprites.size());
    return 0;
}

int showWindow(const FormViewerOptions& o) {
    mb::Window window("Muggle Ball 3", o.width, o.height);
    mb::Renderer renderer(window);
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::UiAssets assets(renderer, mb::assetRoot(), o.language);
    Screen screen(assets, o.background);
    screen.show(o.form);
    window.setTitle("Muggle Ball 3 - form " + screen.file());

    mb::InputEvents input;
    while (window.pollEvents(input)) {
        for (const std::uint32_t key : input.keysPressed) {
            if (key != SDLK_LEFT && key != SDLK_RIGHT) {
                continue;
            }
            try {
                screen.showRelative(key == SDLK_LEFT ? -1 : 1);
                window.setTitle("Muggle Ball 3 - form " + screen.file());
            } catch (const std::exception& e) {
                mb::logError("cannot show the next form: {}", e.what());
            }
        }
        int width = 0, height = 0;
        renderer.drawableSize(width, height);
        const std::vector<mb::Sprite> sprites =
            screen.draw(width, height, {input.mouseX, input.mouseY}, input.leftButton, true);
        renderer.renderToWindow({}, {}, {}, sprites);
    }
    return 0;
}

}  // namespace

int runFormViewer(const FormViewerOptions& options) {
    return options.screenshot.empty() ? showWindow(options) : writeScreenshot(options);
}

}  // namespace mb_app
