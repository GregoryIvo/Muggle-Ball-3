// Muggle Ball 3: entry point.
//
//   MuggleBall3                              open the game: the main menu, from which levels are played
//   MuggleBall3 --level <name|path.json>     open the level viewer on this level (file stem or name, any case, or a path)
//   MuggleBall3 --screenshot out.png [--size WxH] [--level ...] [--shapes] [--camera ex,ey,ez,tx,ty,tz,fov]
//                                    [--simulate seconds] [--ball [x,degrees]]
//                                            render one frame without a window, write it as PNG
//                                            (--size, --shapes and --camera also apply to the viewer window)
//                                            --simulate runs the physics that long first; --ball shoots a test ball
//                                            at t = 0, from (x, 1.2, -15) at that angle off +z (default: at the
//                                            middle of everything that can move)
//   ... [--hud] [--hud-state lives,score,level[,icon...]]
//                                            draw the in-game HUD over the level (screenshot or viewer)
//   MuggleBall3 --check <level.json>         load a converted level JSON, print a summary, exit
//   MuggleBall3 --form <name> [--screenshot out.png] [--size WxH] [--language L] [--hover W | --press W] [--no-background]
//                                            show a menu form alone (FormViewer.h); Left/Right switch form in the window
//
// In the level viewer: Left/Right (or PageUp/PageDown) = previous/next level, Space = shoot a test ball, P = pause
// the physics, L = reload the level, F1 = physics wireframes, left-drag = orbit, right-drag = pan, wheel = zoom,
// R = back to the game camera, Esc = quit.

#include "FormViewer.h"
#include "FrontEnd.h"
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
#include "mb/game/PhysicsScene.h"
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
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 768;
constexpr mb::Color kClearColor{0.05f, 0.05f, 0.1f, 1.0f};  // only visible where a level has no sky

// Where the test ball is shot from and at what angle (the bat's end of the board, away from the player).
constexpr float kBallStartY = 1.2f;
constexpr float kBallStartZ = -15.0f;
constexpr float kBallSpeed = 25.0f;
constexpr float kPhysicsStep = 1.0f / 60.0f;
constexpr float kMaxFrameSeconds = 0.1f;  // a longer frame (a stall) does not make the physics catch up

struct BallShot {
    float x = 0;                   // where along the bat's end
    std::optional<float> degrees;  // off straight ahead (+z); aimed at the middle of the movables when not given
};

struct Options {
    std::string level;       // --level; empty means the first one
    std::string screenshot;  // --screenshot; empty means open a window
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    bool shapes = false;
    std::optional<std::array<float, 7>> camera;  // eye, target, vertical fov in degrees
    mb_app::HudOptions hud;                      // --hud
    mb_app::FormViewerOptions form;              // --form: the other fields are its options
    bool viewer = false;                         // an option for the level viewer was given: no menu
    float simulate = 0;                          // --simulate: seconds of physics before the screenshot
    std::optional<BallShot> ball;                // --ball: a test ball at t = 0
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

// Builds the scene and its physics for one catalog entry at a time and keeps the camera.
class Viewer {
public:
    Viewer(mb::Renderer& renderer, Assets& assets, const Options& options)
        : assets_(assets), models_(renderer, assets.root / "Data" / "Meshes") {
        if (options.camera) {
            const auto& c = *options.camera;
            camera.lookAt({c[0], c[1], c[2]}, {c[3], c[4], c[5]}, c[6]);
        }
    }

    // Loads the level with fresh physics; on failure logs why and keeps the previous scene.
    bool show(std::size_t index) {
        const mb::LevelEntry& entry = assets_.catalog.levels()[index];
        try {
            const auto start = Clock::now();
            const mb::Level level = mb::loadLevelJson(entry.path);
            const auto loaded = Clock::now();
            auto physics = std::make_unique<mb::PhysicsScene>(level, assets_.materials);
            const auto simulated = Clock::now();
            auto next = std::make_unique<mb::LevelScene>(models_, assets_.materials, assets_.textures, level,
                                                         physics->plan());
            const auto built = Clock::now();
            const auto ms = [](Clock::time_point from, Clock::time_point to) {
                return std::chrono::duration<double, std::milli>(to - from).count();
            };
            const mb::PhysicsScene::Counts& counts = physics->counts();
            mb::logInfo("{}: {} draw items, {} shape lines; physics: {} dynamic ({} awake: machines), {} kinematic and "
                        "{} static bodies ({} of them welded to the world; {} actors without one), {} joints ({} on "
                        "cloth left out); load {:.0f} ms, physics build {:.0f} ms, scene {:.0f} ms",
                        entry.name, next->items().size(), next->shapeLines().size(), counts.dynamicBodies,
                        physics->world().awakeBodyCount(), counts.kinematicBodies, counts.staticBodies,
                        physics->plan().pinnedBodies, counts.withoutBody, counts.joints, counts.clothJoints,
                        ms(start, loaded), ms(loaded, simulated), ms(simulated, built));
            logMissing(entry.name, next->report());
            logUnbuilt(entry.name, *physics);
            scene_ = std::move(next);
            physics_ = std::move(physics);
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
    std::size_t index() const { return index_; }

    // Shoots a test ball: from (x, 1.2, -15) at an angle off straight ahead, or at the middle of the movables.
    void launchBall(const BallShot& shot) {
        float angle = 0;
        if (shot.degrees) {
            angle = mb::radians(*shot.degrees);
        } else {
            const mb::Vec3 centre = physics_->centreOfMovables();
            angle = std::atan2(centre.x - shot.x, centre.z - kBallStartZ);
        }
        physics_->launchBall({shot.x, kBallStartY, kBallStartZ},
                             {std::sin(angle) * kBallSpeed, 0, std::cos(angle) * kBallSpeed});
    }

    // Runs the physics for `seconds`; returns the time that took in milliseconds.
    double stepPhysics(float seconds) {
        const auto start = Clock::now();
        physics_->step(seconds);
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    // Puts the scene where the physics has everything.
    void follow(bool shapes) {
        scene_->follow(*physics_);
        if (shapes) {
            scene_->followShapes(*physics_);
        }
    }

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

    static void logUnbuilt(const std::string& level, const mb::PhysicsScene& physics) {
        const mb::PhysicsPlan& plan = physics.plan();
        const mb::PhysicsScene::Counts& counts = physics.counts();
        if (plan.unresolvedJoints + plan.unsupportedJoints + plan.jointsWithoutBody + counts.jointsRefused +
                plan.unsupportedShapes > 0) {
            mb::logWarn("{}: physics left out {} joints that name no actor, {} of a type not supported, {} on an "
                        "actor without a body, {} the world refused, and {} shapes not supported",
                        level, plan.unresolvedJoints, plan.unsupportedJoints, plan.jointsWithoutBody,
                        counts.jointsRefused, plan.unsupportedShapes);
        }
    }

    Assets& assets_;
    mb::ModelCache models_;
    std::unique_ptr<mb::PhysicsScene> physics_;
    std::unique_ptr<mb::LevelScene> scene_;
    std::size_t index_ = 0;
    Clock::time_point shown_ = Clock::now();
};

// The front end's way into the scene code: levels are built by the viewer and drawn from the game camera. The
// per-frame work of a level (physics, the rules, the HUD's numbers) goes into update().
class ViewerSession : public mb_app::LevelSession {
public:
    ViewerSession(Viewer& viewer, Assets& assets) : viewer_(viewer), assets_(assets) {}

    bool start(std::string_view levelName, const Progress&) override {
        const std::optional<std::size_t> index = assets_.catalog.find(levelName);
        if (!index) {
            mb::logError("the campaign names the level \"{}\", which is not among the converted levels", levelName);
            return false;
        }
        return viewer_.show(*index);
    }

    void update(const mb::InputEvents&, double seconds, mb::GameHud&) override {
        viewer_.stepPhysics(std::min(static_cast<float>(seconds), kMaxFrameSeconds));
        viewer_.follow(false);
    }

    void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) override {
        int width = 0, height = 0;
        renderer.drawableSize(width, height);
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
        renderer.renderToWindow(viewer_.frame(aspect), viewer_.scene().items(), {}, overlay);
    }

private:
    Viewer& viewer_;
    Assets& assets_;
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
    if (options.ball) {
        viewer.launchBall(*options.ball);
    }
    const int steps = static_cast<int>(std::lround(options.simulate / kPhysicsStep));
    for (int i = 0; i < steps; ++i) {
        viewer.stepPhysics(kPhysicsStep);
    }
    viewer.follow(options.shapes);
    mb_app::HudOverlay hud(renderer, options.hud);
    const mb::FrameParams frame = viewer.frame(static_cast<float>(options.width) / static_cast<float>(options.height));
    const mb::Image image = renderer.renderToImage(options.width, options.height, frame, viewer.scene().items(),
                                                   options.shapes ? viewer.scene().shapeLines()
                                                                  : std::span<const mb::DebugLine>(),
                                                   hud.sprites(options.width, options.height, viewer.levelName()));
    if (image.empty() || !mb::writePng(options.screenshot, image)) {
        mb::logError("cannot write screenshot {}", options.screenshot);
        return 1;
    }
    mb::logInfo("wrote {} ({}x{})", options.screenshot, image.width, image.height);
    return 0;
}

// The game: the main menu on a window, levels played from it.
int runGame(const Options& options) {
    Assets assets;
    mb::Window window("Muggle Ball 3", options.width, options.height);
    mb::Renderer renderer(window);
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::logInfo("Asset root: {}", assets.root.string());
    Viewer viewer(renderer, assets, options);
    ViewerSession session(viewer, assets);
    mb_app::FrontEndOptions frontEnd;
    frontEnd.language = options.form.language;
    return mb_app::runFrontEnd(window, renderer, session, frontEnd);
}

// Average time per frame (and how much of it the physics took), logged every few seconds and when the window closes.
class FrameStats {
public:
    void physics(double ms) { physicsMs_ += ms; }

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
            mb::logInfo("average frame time {:.2f} ms ({:.0f} fps) over {} frames, physics {:.2f} ms",
                        totalMs_ / frames_, 1000.0 * frames_ / totalMs_, frames_, physicsMs_ / frames_);
        }
        totalMs_ = 0;
        physicsMs_ = 0;
        frames_ = 0;
    }

private:
    static constexpr double kReportMs = 5000;
    Clock::time_point last_{};
    double totalMs_ = 0;
    double physicsMs_ = 0;
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
    if (options.ball) {
        viewer.launchBall(*options.ball);
    }

    mb_app::HudOverlay hud(renderer, options.hud);
    bool shapes = options.shapes;
    bool paused = false;
    FrameStats stats;
    std::mt19937 random{std::random_device{}()};
    std::uniform_real_distribution<float> sideways(-5.0f, 5.0f);  // degrees off straight ahead
    Clock::time_point lastFrame = Clock::now();
    const auto titleFor = [&] { return "Muggle Ball 3 - " + viewer.levelName() + (paused ? " (paused)" : ""); };
    mb::InputEvents input;
    while (window.pollEvents(input)) {
        int step = 0;
        bool reload = false;
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
                case SDLK_SPACE:
                    viewer.launchBall({.x = 0.0f, .degrees = sideways(random)});
                    break;
                case SDLK_P:
                    paused = !paused;
                    window.setTitle(titleFor());
                    break;
                case SDLK_L:
                    reload = true;
                    break;
                default:
                    break;
            }
        }
        if (step != 0 && viewer.showRelative(step)) {
            paused = false;
            window.setTitle(titleFor());
        } else if (reload && viewer.show(viewer.index())) {
            paused = false;
            window.setTitle(titleFor());
        }

        const Clock::time_point now = Clock::now();
        const float seconds = std::min(std::chrono::duration<float>(now - lastFrame).count(), kMaxFrameSeconds);
        lastFrame = now;
        if (!paused) {
            stats.physics(viewer.stepPhysics(seconds));
        }
        viewer.follow(shapes);

        viewer.camera.orbit(input.leftDragDX, input.leftDragDY);
        viewer.camera.pan(input.rightDragDX, input.rightDragDY);
        viewer.camera.zoom(input.wheel);

        int width = 0, height = 0;
        renderer.drawableSize(width, height);
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
        renderer.renderToWindow(viewer.frame(aspect), viewer.scene().items(),
                                shapes ? viewer.scene().shapeLines() : std::span<const mb::DebugLine>(),
                                hud.sprites(width, height, viewer.levelName()));
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

// "x,degrees"
bool parseBallShot(const std::string& text, BallShot& out) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    char* endX = nullptr;
    char* endDegrees = nullptr;
    const std::string x = text.substr(0, comma), degrees = text.substr(comma + 1);
    const float xValue = std::strtof(x.c_str(), &endX);
    const float degreesValue = std::strtof(degrees.c_str(), &endDegrees);
    if (x.empty() || degrees.empty() || *endX || *endDegrees || !std::isfinite(xValue) ||
        !std::isfinite(degreesValue) || std::fabs(degreesValue) >= 90) {
        return false;
    }
    out.x = xValue;
    out.degrees = degreesValue;
    return true;
}

void printUsage() {
    std::fputs(
        "usage: MuggleBall3 [--level <name|path.json>] [--ball [x,degrees]]\n"
        "       MuggleBall3 --screenshot out.png [--size WxH] [--level ...] [--shapes] [--camera ex,ey,ez,tx,ty,tz,fov]\n"
        "                   [--simulate seconds] [--ball [x,degrees]]\n"
        "       MuggleBall3 [--level ...] [--hud] [--hud-state lives,score,level[,icon...]]\n"
        "       MuggleBall3 --check <level.json>\n"
        "       MuggleBall3 --form <name> [--screenshot out.png] [--size WxH] [--language L] [--hover W | --press W]"
        " [--no-background]\n",
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
                options.viewer = true;
            } else if (arg == "--screenshot" && hasValue) {
                options.screenshot = argv[++i];
            } else if (arg == "--size" && hasValue && parseSize(argv[i + 1], options.width, options.height)) {
                ++i;
            } else if (arg == "--form" && hasValue) {
                options.form.form = argv[++i];
            } else if (arg == "--language" && hasValue) {
                options.form.language = argv[++i];
            } else if (arg == "--hover" && hasValue) {
                options.form.hover = argv[++i];
            } else if (arg == "--press" && hasValue) {
                options.form.press = argv[++i];
            } else if (arg == "--no-background") {
                options.form.background = false;
            } else if (arg == "--hud") {
                options.hud.enabled = true;
                options.viewer = true;
            } else if (arg == "--hud-state" && hasValue) {
                mb::GameHud state;
                if (!mb_app::parseHudState(argv[i + 1], state)) {
                    printUsage();
                    return 2;
                }
                options.hud = {true, state};
                options.viewer = true;
                ++i;
            } else if (arg == "--shapes") {
                options.shapes = true;
                options.viewer = true;
            } else if (arg == "--simulate" && hasValue) {
                char* end = nullptr;
                options.simulate = std::strtof(argv[i + 1], &end);
                if (*end || !(options.simulate >= 0) || options.simulate > 600) {
                    printUsage();
                    return 2;
                }
                options.viewer = true;
                ++i;
            } else if (arg == "--ball") {
                BallShot shot;
                // The angle is optional: a following argument that is not an option is "x,degrees".
                if (hasValue && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
                    if (!parseBallShot(argv[i + 1], shot)) {
                        printUsage();
                        return 2;
                    }
                    ++i;
                }
                options.ball = shot;
                options.viewer = true;
            } else if (arg == "--camera" && hasValue) {
                std::array<float, 7> values{};
                if (!parseCamera(argv[i + 1], values)) {
                    printUsage();
                    return 2;
                }
                options.camera = values;
                options.viewer = true;
                ++i;
            } else {
                printUsage();
                return 2;
            }
        }
        if (!options.form.form.empty()) {
            options.form.screenshot = options.screenshot;
            options.form.width = options.width;
            options.form.height = options.height;
            return mb_app::runFormViewer(options.form);
        }
        if (options.simulate > 0 && options.screenshot.empty()) {  // the window runs the physics all the time
            printUsage();
            return 2;
        }
        if (!options.screenshot.empty()) {
            return runScreenshot(options);
        }
        return options.viewer ? runWindow(options) : runGame(options);
    } catch (const std::exception& e) {
        mb::logError("fatal: {}", e.what());
        return 1;
    }
}
