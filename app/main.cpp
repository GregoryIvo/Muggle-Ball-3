// Muggle Ball 3: entry point.
//
//   MuggleBall3                              open the game window on the first level
//   MuggleBall3 --level <name|path.json>     ... on this level (file stem or name, any case, or a path)
//   MuggleBall3 --screenshot out.png [--size WxH] [--level ...] [--shapes] [--camera ex,ey,ez,tx,ty,tz,fov]
//                                            render one frame without a window, write it as PNG
//                                            (--size, --shapes and --camera also apply to the window)
//   MuggleBall3 --check <level.json>         load a converted level JSON, print a summary, exit
//
// In the window: Left/Right (or PageUp/PageDown) = previous/next level, F1 = physics wireframes,
// left-drag = orbit, right-drag = pan, wheel = zoom, R = back to the game camera, Esc = quit.

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/GameCamera.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/LevelScene.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ModelCache.h"
#include "mb/game/TextureIndex.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 768;
constexpr mb::Color kClearColor{0.05f, 0.05f, 0.1f, 1.0f};  // only visible where a level has no sky

struct Options {
    std::string level;       // --level; empty means the first one
    std::string screenshot;  // --screenshot; empty means open a window
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    bool shapes = false;
    std::optional<std::array<float, 7>> camera;  // eye, target, vertical fov in degrees
};

int checkLevel(const char* path) {
    const mb::Level level = mb::loadLevelJson(path);
    mb::logInfo("{}: {} actors, {} paths", path, level.actors.size(), level.paths.size());
    for (int t = 1; t <= 26; ++t) {
        const auto type = static_cast<mb::ObjectType>(t);
        if (const std::size_t n = level.countObjects(type)) {
            mb::logInfo("  {:<14} {}", mb::objectTypeName(type), n);
        }
    }
    return 0;
}

// Everything that outlives a single level.
struct Assets {
    fs::path root = mb::assetRoot();
    mb::MaterialLibrary materials = mb::MaterialLibrary::load(root / "Data" / "Materials" / "materials.json");
    mb::TextureIndex textures{root / "Textures"};
    mb::LevelCatalog catalog{root / "Data" / "Levels"};
};

// Builds a scene for one catalog entry at a time and keeps the camera.
class Viewer {
public:
    Viewer(mb::Renderer& renderer, Assets& assets, const Options& options)
        : assets_(assets), models_(renderer, assets.root / "Data" / "Meshes") {
        if (options.camera) {
            const auto& c = *options.camera;
            camera.lookAt({c[0], c[1], c[2]}, {c[3], c[4], c[5]}, c[6]);
        }
    }

    // Loads the level; on failure logs why and keeps the previous scene.
    bool show(std::size_t index) {
        const mb::LevelEntry& entry = assets_.catalog.levels()[index];
        try {
            const auto start = Clock::now();
            const mb::Level level = mb::loadLevelJson(entry.path);
            auto next = std::make_unique<mb::LevelScene>(models_, assets_.materials, assets_.textures, level);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            mb::logInfo("{}: {} draw items, {} shape lines, loaded in {:.0f} ms", entry.name, next->items().size(),
                        next->shapeLines().size(), ms);
            logMissing(entry.name, next->report());
            scene_ = std::move(next);
            index_ = index;
            shown_ = Clock::now();
            return true;
        } catch (const std::exception& e) {
            mb::logError("cannot show level {}: {}", entry.name, e.what());
            return false;
        }
    }

    // Moves `step` levels (wrapping) from the current one, skipping any that fail to load.
    bool showRelative(int step) {
        const auto count = static_cast<int>(assets_.catalog.levels().size());
        for (int tried = 1; tried <= count; ++tried) {
            const int next = ((static_cast<int>(index_) + step * tried) % count + count) % count;
            if (show(static_cast<std::size_t>(next))) {
                return true;
            }
        }
        return false;
    }

    const std::string& levelName() const { return assets_.catalog.levels()[index_].name; }

    mb::FrameParams frame(float aspect) {
        scene_->update(std::chrono::duration<double>(Clock::now() - shown_).count());
        mb::FrameParams params;
        params.view = camera.view();
        params.projection = camera.projection(aspect);
        params.eye = camera.eye();
        params.light = scene_->light();
        params.clear = kClearColor;
        return params;
    }

    const mb::LevelScene& scene() const { return *scene_; }

    mb::Camera camera;

private:
    static void logMissing(const std::string& level, const mb::SceneReport& report) {
        for (const std::string& name : report.missingMeshes) {
            mb::logWarn("{}: missing mesh {}", level, name);
        }
        for (const std::string& name : report.missingFrames) {
            mb::logWarn("{}: missing frame {}", level, name);
        }
        for (const std::string& name : report.missingTextures) {
            mb::logWarn("{}: missing texture {}", level, name);
        }
    }

    Assets& assets_;
    mb::ModelCache models_;
    std::unique_ptr<mb::LevelScene> scene_;
    std::size_t index_ = 0;
    Clock::time_point shown_ = Clock::now();
};

std::size_t startLevel(Assets& assets, const Options& options) {
    if (assets.catalog.levels().empty()) {
        throw std::runtime_error("no levels found in " + (assets.root / "Data" / "Levels").string());
    }
    if (options.level.empty()) {
        return 0;
    }
    if (const std::optional<std::size_t> index = assets.catalog.find(options.level)) {
        return *index;
    }
    throw std::runtime_error("unknown level \"" + options.level + "\"");
}

int runScreenshot(const Options& options) {
    mb::Renderer renderer;
    mb::logInfo("GPU backend: {}", renderer.backendName());
    Assets assets;
    Viewer viewer(renderer, assets, options);
    if (!viewer.show(startLevel(assets, options))) {
        return 1;
    }
    const mb::FrameParams frame = viewer.frame(static_cast<float>(options.width) / static_cast<float>(options.height));
    const mb::Image image = renderer.renderToImage(options.width, options.height, frame, viewer.scene().items(),
                                                   options.shapes ? viewer.scene().shapeLines()
                                                                  : std::span<const mb::DebugLine>());
    if (image.empty() || !mb::writePng(options.screenshot, image)) {
        mb::logError("cannot write screenshot {}", options.screenshot);
        return 1;
    }
    mb::logInfo("wrote {} ({}x{})", options.screenshot, image.width, image.height);
    return 0;
}

// Average time per frame, logged every few seconds and when the window closes.
class FrameStats {
public:
    void frame() {
        const Clock::time_point now = Clock::now();
        if (last_ != Clock::time_point{}) {
            totalMs_ += std::chrono::duration<double, std::milli>(now - last_).count();
            ++frames_;
        }
        last_ = now;
        if (totalMs_ >= kReportMs) {
            report();
        }
    }

    void report() {
        if (frames_ > 0) {
            mb::logInfo("average frame time {:.2f} ms ({:.0f} fps) over {} frames", totalMs_ / frames_,
                        1000.0 * frames_ / totalMs_, frames_);
        }
        totalMs_ = 0;
        frames_ = 0;
    }

private:
    static constexpr double kReportMs = 5000;
    Clock::time_point last_{};
    double totalMs_ = 0;
    int frames_ = 0;
};

int runWindow(const Options& options) {
    // Resolve the assets and the level first, so a bad argument fails before a window flashes up.
    Assets assets;
    const std::size_t first = startLevel(assets, options);

    mb::Window window("Muggle Ball 3", options.width, options.height);
    mb::Renderer renderer(window);
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::logInfo("Asset root: {}", assets.root.string());

    Viewer viewer(renderer, assets, options);
    // A level asked for by name must load; without one, the first level that does will do.
    if (!viewer.show(first) && (!options.level.empty() || !viewer.showRelative(1))) {
        return 1;
    }
    window.setTitle("Muggle Ball 3 - " + viewer.levelName());

    bool shapes = options.shapes;
    FrameStats stats;
    mb::InputEvents input;
    while (window.pollEvents(input)) {
        int step = 0;
        for (const std::uint32_t key : input.keysPressed) {
            switch (key) {
                case SDLK_LEFT:
                case SDLK_PAGEUP:
                    step = -1;
                    break;
                case SDLK_RIGHT:
                case SDLK_PAGEDOWN:
                    step = 1;
                    break;
                case SDLK_F1:
                    shapes = !shapes;
                    break;
                case SDLK_R:
                    viewer.camera.reset();
                    break;
                default:
                    break;
            }
        }
        if (step != 0 && viewer.showRelative(step)) {
            window.setTitle("Muggle Ball 3 - " + viewer.levelName());
        }

        if (input.leftButton) {
            viewer.camera.orbit(input.mouseDX, input.mouseDY);
        } else if (input.rightButton) {
            viewer.camera.pan(input.mouseDX, input.mouseDY);
        }
        viewer.camera.zoom(input.wheel);

        int width = 0, height = 0;
        renderer.drawableSize(width, height);
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
        renderer.renderToWindow(viewer.frame(aspect), viewer.scene().items(),
                                shapes ? viewer.scene().shapeLines() : std::span<const mb::DebugLine>());
        stats.frame();
    }
    stats.report();
    return 0;
}

// "1024x768"
bool parseSize(std::string_view text, int& width, int& height) {
    const std::size_t x = text.find('x');
    if (x == std::string_view::npos) {
        return false;
    }
    const std::string w(text.substr(0, x)), h(text.substr(x + 1));
    char* endW = nullptr;
    char* endH = nullptr;
    const long wl = std::strtol(w.c_str(), &endW, 10);
    const long hl = std::strtol(h.c_str(), &endH, 10);
    if (w.empty() || h.empty() || *endW || *endH || wl <= 0 || hl <= 0 || wl > 16384 || hl > 16384) {
        return false;
    }
    width = static_cast<int>(wl);
    height = static_cast<int>(hl);
    return true;
}

// "ex,ey,ez,tx,ty,tz,fov"
bool parseCamera(const std::string& text, std::array<float, 7>& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::size_t comma = text.find(',', start);
        if ((comma == std::string::npos) != (i + 1 == out.size())) {
            return false;  // too few or too many numbers
        }
        const std::string field = text.substr(start, comma == std::string::npos ? comma : comma - start);
        char* end = nullptr;
        out[i] = std::strtof(field.c_str(), &end);
        if (field.empty() || *end || !std::isfinite(out[i])) {
            return false;
        }
        start = comma + 1;
    }
    // Looking from the target at itself, or through a degenerate lens, draws nothing.
    const bool eyeAtTarget = out[0] == out[3] && out[1] == out[4] && out[2] == out[5];
    return !eyeAtTarget && out[6] > 0 && out[6] < 180;
}

void printUsage() {
    std::fputs(
        "usage: MuggleBall3 [--level <name|path.json>]\n"
        "       MuggleBall3 --screenshot out.png [--size WxH] [--level ...] [--shapes] [--camera ex,ey,ez,tx,ty,tz,fov]\n"
        "       MuggleBall3 --check <level.json>\n",
        stderr);
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            const bool hasValue = i + 1 < argc && argv[i + 1][0] != '\0';
            if (arg == "--check" && hasValue) {
                return checkLevel(argv[i + 1]);
            } else if (arg == "--level" && hasValue) {
                options.level = argv[++i];
            } else if (arg == "--screenshot" && hasValue) {
                options.screenshot = argv[++i];
            } else if (arg == "--size" && hasValue && parseSize(argv[i + 1], options.width, options.height)) {
                ++i;
            } else if (arg == "--shapes") {
                options.shapes = true;
            } else if (arg == "--camera" && hasValue) {
                std::array<float, 7> values{};
                if (!parseCamera(argv[i + 1], values)) {
                    printUsage();
                    return 2;
                }
                options.camera = values;
                ++i;
            } else {
                printUsage();
                return 2;
            }
        }
        return options.screenshot.empty() ? runWindow(options) : runScreenshot(options);
    } catch (const std::exception& e) {
        mb::logError("fatal: {}", e.what());
        return 1;
    }
}
