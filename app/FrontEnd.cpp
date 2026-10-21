#include "FrontEnd.h"

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/game/GameFlow.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <string>

namespace mb_app {

namespace {

using Clock = std::chrono::steady_clock;

// Window sizes the video list offers: the original's 4:3 shapes, those that fit on the display.
std::vector<mb::VideoMode> videoModes(const mb::Window& window) {
    int displayWidth = 0, displayHeight = 0;
    const bool known = window.displaySize(displayWidth, displayHeight);
    std::vector<mb::VideoMode> modes;
    for (const mb::VideoMode mode : {mb::VideoMode{800, 600}, {1024, 768}, {1152, 864}, {1280, 960}, {1600, 1200}, {2048, 1536}}) {
        if (!known || (mode.width <= displayWidth && mode.height <= displayHeight)) {
            modes.push_back(mode);
        }
    }
    return modes;
}

void applyVideo(mb::Window& window, const mb::Settings& settings) {
    window.setFullscreen(settings.fullscreen);
    if (!settings.fullscreen) {
        window.setSize(settings.windowWidth, settings.windowHeight);
    }
}

class FrontEnd {
public:
    FrontEnd(mb::Window& window, mb::Renderer& renderer, LevelSession& session, const FrontEndOptions& options)
        : window_(window),
          renderer_(renderer),
          session_(session),
          assets_(renderer, mb::assetRoot(), options.language),
          campaign_(mb::Campaign::load(assets_.root() / "Data" / "xml" / "levels.json")),
          menu_([this](std::string_view name) { return assets_.loadForm(name); }, campaign_, hooksFor(window, options.hooks)),
          flow_(menu_, campaign_) {
        menu_.setVideoModes(videoModes(window));
        if (options.hooks.loadProfile) {
            applyVideo(window, menu_.settings());  // the window the player chose last time
        }
        menu_.open("MainMenu");
    }

    int run() {
        window_.setEscapeQuits(false);  // the menus and the pause screen use Escape themselves
        window_.setTitle("Muggle Ball 3");
        Clock::time_point last = Clock::now();
        mb::InputEvents input;
        bool textInput = false;
        while (window_.pollEvents(input)) {
            const Clock::time_point now = Clock::now();
            // A long frame (a level loading) must not make the next one jump through a fade.
            const double seconds = std::min(std::chrono::duration<double>(now - last).count(), 0.1);
            last = now;

            int width = 0, height = 0;
            renderer_.drawableSize(width, height);
            const mb::GameFlow::Step step =
                flow_.update(toUiInput(input), seconds, mb::UiLayout::fit(width, height), assets_.imageSizes());
            if (step.quit) {
                break;
            }
            if (step.loadLevel) {
                loadLevel(*step.loadLevel);
            }

            const bool wantsText = flow_.mode() == mb::GameFlow::Mode::Menu && menu_.wantsText();
            if (wantsText != textInput) {
                window_.setTextInput(wantsText);
                textInput = wantsText;
            }
            if (flow_.mode() == mb::GameFlow::Mode::Playing) {
                session_.update(input, seconds, flow_.hud());
            }
            draw(width, height);
        }
        return 0;
    }

private:
    // The menu hooks as set by the caller, plus the window ones.
    static mb::MenuHooks hooksFor(mb::Window& window, mb::MenuHooks hooks) {
        hooks.applyVideo = [&window, caller = std::move(hooks.applyVideo)](const mb::Settings& settings) {
            applyVideo(window, settings);
            if (caller) {
                caller(settings);
            }
        };
        return hooks;
    }

    void loadLevel(int index) {
        const std::string* name = campaign_.levelName(index);
        bool ok = name != nullptr;
        if (ok) {
            mb::logInfo("starting level {}: {}", index + 1, *name);
            window_.setTitle("Muggle Ball 3 - " + *name);
            ok = session_.start(*name, [this](float fraction) {
                menu_.setProgress(fraction);
                drawMenu();
            });
        }
        if (!ok) {
            window_.setTitle("Muggle Ball 3");
        }
        flow_.loaded(ok);
    }

    void drawMenu() {
        std::vector<mb::Sprite> sprites;
        menu_.draw(assets_, sprites);
        renderer_.renderToWindow({}, {}, {}, sprites);
    }

    void draw(int width, int height) {
        const mb::GameFlow::Mode mode = flow_.mode();
        if (mode == mb::GameFlow::Mode::Menu || mode == mb::GameFlow::Mode::Loading) {
            drawMenu();
            return;
        }
        std::vector<mb::Sprite> overlay;
        mb::drawHud(assets_, flow_.hud(), width, height, overlay);
        if (mode == mb::GameFlow::Mode::Paused) {
            mb::drawPause(assets_, width, height, overlay);
        }
        session_.render(renderer_, overlay);
    }

    mb::Window& window_;
    mb::Renderer& renderer_;
    LevelSession& session_;
    mb::UiAssets assets_;
    mb::Campaign campaign_;
    mb::Menu menu_;
    mb::GameFlow flow_;
};

}  // namespace

int runFrontEnd(mb::Window& window, mb::Renderer& renderer, LevelSession& session, const FrontEndOptions& options) {
    return FrontEnd(window, renderer, session, options).run();
}

mb::UiInput toUiInput(const mb::InputEvents& input) {
    mb::UiInput ui;
    for (const mb::PointerSample& sample : input.pointer) {
        ui.pointer.push_back({{sample.x, sample.y}, sample.leftDown});
    }
    for (const std::uint32_t key : input.keysPressed) {
        switch (key) {
            case SDLK_ESCAPE:
                ui.keys.push_back(mb::UiKey::Escape);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                ui.keys.push_back(mb::UiKey::Enter);
                break;
            case SDLK_BACKSPACE:
                ui.keys.push_back(mb::UiKey::Backspace);
                break;
            default:
                ui.keys.push_back(mb::UiKey::Other);
                break;
        }
    }
    ui.text = input.text;
    return ui;
}

bool parseHudState(std::string_view text, mb::GameHud& hud) {
    std::vector<std::string_view> fields;
    for (std::size_t start = 0;;) {
        const std::size_t comma = text.find(',', start);
        fields.push_back(text.substr(start, comma == std::string_view::npos ? comma : comma - start));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    if (fields.size() < 3) {
        return false;
    }
    int numbers[3] = {};
    for (int i = 0; i < 3; ++i) {
        const std::string_view field = fields[static_cast<std::size_t>(i)];
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), numbers[i]);
        if (field.empty() || error != std::errc() || end != field.data() + field.size() || numbers[i] < 0) {
            return false;
        }
    }
    hud = {};
    hud.lives = numbers[0];
    hud.score = numbers[1];
    hud.level = numbers[2];
    for (std::size_t i = 3; i < fields.size(); ++i) {
        if (fields[i].empty()) {
            return false;
        }
        hud.powerUps.emplace_back(fields[i]);
    }
    return true;
}

HudOverlay::HudOverlay(mb::Renderer& renderer, const HudOptions& options) : fixed_(options.state) {
    if (options.enabled) {
        assets_.emplace(renderer, mb::assetRoot());
        campaign_ = mb::Campaign::load(assets_->root() / "Data" / "xml" / "levels.json");
    }
}

std::vector<mb::Sprite> HudOverlay::sprites(int width, int height, std::string_view levelName) {
    std::vector<mb::Sprite> out;
    if (!assets_) {
        return out;
    }
    mb::GameHud hud;
    if (fixed_) {
        hud = *fixed_;
    } else if (const std::optional<int> index = campaign_->indexOf(levelName)) {
        hud.level = *index + 1;
    }
    mb::drawHud(*assets_, hud, width, height, out);
    return out;
}

}  // namespace mb_app
