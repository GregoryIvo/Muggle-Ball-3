#include "FormViewer.h"

#include "FrontEnd.h"
#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/Menu.h"
#include "mb/game/UiAssets.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mb_app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDemoScore = 1234;

// A table to show on the high-score form: made up, for looking at the screen only.
mb::Profile demoProfile() {
    mb::Profile profile;
    int score = 48500;
    for (const char* name : {"Ivan", "Alexey", "Sergey", "Lena", "Artem", "Igor", "Andrew", "Maria", "Olga", "Nikolay"}) {
        profile.highScores.push_back({name, score});
        score -= 2750;
    }
    return profile;
}

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// The menus on their own, with the sounds and the saving written to the log.
class Viewer {
public:
    Viewer(mb::UiAssets& assets, bool background)
        : assets_(assets),
          names_(assets.formNames()),
          campaign_(mb::Campaign::load(assets.root() / "Data" / "xml" / "levels.json")),
          menu_([this](std::string_view name) { return assets_.loadForm(name); }, campaign_, hooks()) {
        menu_.setBackground(background);
        menu_.setVideoModes({{800, 600}, {1024, 768}, {1280, 960}});
    }

    // `query` is a file name from Data/Menu, any case.
    void show(const std::string& query) {
        menu_.setScore(kDemoScore);
        menu_.open(query);  // throws if there is no such form
        // loadForm also takes "MainMenu.json" or "MainMenu.xml"; names_ holds bare names.
        const std::filesystem::path asPath(query);
        const std::string ext = asPath.extension().string();
        const std::string bare = sameName(ext, ".json") || sameName(ext, ".xml") ? asPath.stem().string() : query;
        const auto found = std::ranges::find_if(names_, [&](const std::string& n) { return sameName(n, bare); });
        index_ = found == names_.end() ? 0 : static_cast<std::size_t>(found - names_.begin());
        if (sameName(file(), "Loading")) {
            menu_.setProgress(0.6f);
        }
    }

    // The next (or previous) form in Data/Menu, wrapping around.
    void showRelative(int step) {
        const auto count = static_cast<int>(names_.size());
        show(names_[static_cast<std::size_t>((static_cast<int>(index_) + step + count) % count)]);
    }

    const std::string& file() const { return names_[index_]; }
    mb::Menu& menu() { return menu_; }

    // Runs a frame of the menus; true while the player has not asked to leave.
    bool update(const mb::UiInput& input, double seconds, int width, int height) {
        const mb::MenuResult result = menu_.update(input, seconds, mb::UiLayout::fit(width, height), assets_.imageSizes());
        if (result.kind == mb::MenuResult::Kind::StartLevel) {
            const std::string* name = campaign_.levelName(result.level);
            mb::logInfo("would start level {} ({})", result.level, name ? *name : "?");
        }
        return result.kind != mb::MenuResult::Kind::Quit;
    }

    std::vector<mb::Sprite> draw() {
        std::vector<mb::Sprite> sprites;
        menu_.draw(assets_, sprites);
        return sprites;
    }

private:
    static mb::MenuHooks hooks() {
        mb::MenuHooks hooks;
        hooks.playSound = [](std::string_view sound) { mb::logInfo("sound \"{}\"", sound); };
        hooks.loadProfile = demoProfile;
        hooks.settingsChanged = [](const mb::Settings& s) {
            mb::logInfo("settings: music {} sound {} smart camera {} freeze sky {} shadows {} mirrors {} fullscreen {} bat {}", s.musicVolume,
                        s.soundVolume, s.smartCamera, s.freezeSky, s.shadows, s.mirrors, s.fullscreen, s.bat);
        };
        hooks.applyVideo = [](const mb::Settings& s) { mb::logInfo("video: {}x{}, fullscreen {}", s.windowWidth, s.windowHeight, s.fullscreen); };
        hooks.saveProfile = [](const mb::Profile& p) { mb::logInfo("profile saved ({} scores)", p.highScores.size()); };
        return hooks;
    }

    mb::UiAssets& assets_;
    std::vector<std::string> names_;
    mb::Campaign campaign_;
    mb::Menu menu_;
    std::size_t index_ = 0;
};

int writeScreenshot(const FormViewerOptions& o) {
    mb::Renderer renderer;
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::UiAssets assets(renderer, mb::assetRoot(), o.language);
    Viewer viewer(assets, o.background);
    viewer.show(o.form);
    // The form has faded in by the time the picture is taken.
    viewer.update({}, 1.0, o.width, o.height);

    // The pointer rests on the widget asked for, or off the screen.
    const std::string& target = !o.press.empty() ? o.press : o.hover;
    if (!target.empty()) {
        const mb::UiWidget* widget = viewer.menu().form().find(target);
        if (!widget) {
            throw std::runtime_error("the form " + viewer.file() + " has no widget named \"" + target + "\"");
        }
        const mb::Vec2 at = widget->rect.center();
        mb::UiInput input;
        input.pointer.push_back({at, false});
        if (!o.press.empty()) {
            input.pointer.push_back({at, true});
        }
        viewer.update(input, 0, o.width, o.height);
    }
    const std::vector<mb::Sprite> sprites = viewer.draw();
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
    Viewer viewer(assets, o.background);
    viewer.show(o.form);
    window.setTitle("Muggle Ball 3 - form " + viewer.file());

    mb::InputEvents input;
    bool textInput = false;
    bool running = true;
    Clock::time_point last = Clock::now();
    while (running && window.pollEvents(input)) {
        for (const std::uint32_t key : input.keysPressed) {
            if (key != SDLK_LEFT && key != SDLK_RIGHT) {
                continue;
            }
            try {
                viewer.showRelative(key == SDLK_LEFT ? -1 : 1);
                window.setTitle("Muggle Ball 3 - form " + viewer.file());
            } catch (const std::exception& e) {
                mb::logError("cannot show the next form: {}", e.what());
            }
        }
        const Clock::time_point now = Clock::now();
        int width = 0, height = 0;
        renderer.drawableSize(width, height);
        running = viewer.update(toUiInput(input), std::chrono::duration<double>(now - last).count(), width, height);
        last = now;
        if (viewer.menu().wantsText() != textInput) {
            textInput = viewer.menu().wantsText();
            window.setTextInput(textInput);
        }
        renderer.renderToWindow({}, {}, {}, viewer.draw());
    }
    return 0;
}

}  // namespace

int runFormViewer(const FormViewerOptions& options) {
    return options.screenshot.empty() ? showWindow(options) : writeScreenshot(options);
}

}  // namespace mb_app
