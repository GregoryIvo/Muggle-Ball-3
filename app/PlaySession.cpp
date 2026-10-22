#include "PlaySession.h"

#include "mb/engine/Log.h"
#include "mb/game/Autopilot.h"
#include "mb/game/LevelLoader.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace mb_app {

namespace {

// From the footage of V1 (144.4-145.5): the picture holds for a moment after the last brick, goes dark, and the next
// level comes up out of the dark.
constexpr double kFadeInSeconds = 0.6;
constexpr double kEndDelaySeconds = 0.3;
constexpr double kFadeOutSeconds = 0.8;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

}  // namespace

PlaySession::PlaySession(mb::Renderer& renderer, Assets& assets, const mb::SaveData& save, mb::GameAudio* audio,
                         const PlayOptions& options)
    : renderer_(renderer),
      assets_(assets),
      save_(save),
      audio_(audio),
      options_(options),
      models_(renderer, assets.root / "Data" / "Meshes") {
    if (audio_ && audio_->sounds()) {
        sounds_.emplace(*audio_->sounds());
    }
}

bool PlaySession::start(std::string_view levelName, const mb::GameHud& hud, const Progress& progress) {
    const std::optional<std::size_t> index = assets_.catalog.find(levelName);
    if (!index) {
        mb::logError("the campaign names the level \"{}\", which is not among the converted levels", levelName);
        return false;
    }
    const mb::LevelEntry& entry = assets_.catalog.levels()[*index];
    try {
        const Clock::time_point begin = Clock::now();
        const mb::Level level = mb::loadLevelJson(entry.path);
        progress(0.2f);
        mb::SessionOptions rules;
        rules.hearts = hud.lives;
        rules.score = hud.score;
        auto session = std::make_unique<mb::GameSession>(level, assets_.materials, rules);
        progress(0.6f);
        auto scene = std::make_unique<mb::PlayScene>(models_, assets_.materials, assets_.textures, level, *session,
                                                     save_.settings.bat);
        mb::logInfo("{}: {} targets; {:.0f} ms to build", entry.name, session->targetsLeft(session->levelEnd()),
                    millisecondsSince(begin));
        logMissing(entry.name, scene->report());
        scene_ = std::move(scene);
        session_ = std::move(session);
    } catch (const std::exception& e) {
        mb::logError("cannot play level {}: {}", entry.name, e.what());
        return false;
    }
    age_ = clock_ = 0;
    ending_ = mb::LevelOutcome::Playing;
    player_ = {};
    return true;
}

mb::LevelOutcome PlaySession::update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) {
    const Clock::time_point began = Clock::now();
    int width = 0, height = 0;
    renderer_.drawableSize(width, height);
    age_ += seconds;
    clock_ += seconds;
    session_->update(seconds, options_.autoplay ? mb::Autopilot{}.decide(*session_) : player_.read(input, width, height, camera_));

    const std::vector<mb::GameEvent> events = session_->takeEvents();
    for (const mb::GameEvent& event : events) {
        if (sounds_) {
            sounds_->play(event);
        }
        if (ending_ == mb::LevelOutcome::Playing && event.type == mb::EventType::LevelComplete) {
            ending_ = mb::LevelOutcome::Complete;
            endAt_ = age_ + kEndDelaySeconds;
        } else if (ending_ == mb::LevelOutcome::Playing && event.type == mb::EventType::GameOver) {
            ending_ = mb::LevelOutcome::GameOver;
            endAt_ = age_ + kEndDelaySeconds;
        }
    }

    // The session steps by a fixed time and does not interpolate: the picture runs on beyond its last step.
    const double step = session_->scene().world().settings().fixedStep;
    if (clock_ - session_->time() > 2 * step) {  // a stall the session did not catch up with
        clock_ = session_->time();
    }
    scene_->update(*session_, events, seconds, std::clamp(clock_ - session_->time(), 0.0, step));

    hud.lives = session_->hearts();
    hud.score = static_cast<int>(session_->score());
    updateMs_ += millisecondsSince(began);
    return ending_ != mb::LevelOutcome::Playing && age_ >= endAt_ + kFadeOutSeconds ? ending_ : mb::LevelOutcome::Playing;
}

double PlaySession::takeUpdateMillis() {
    return std::exchange(updateMs_, 0.0);
}

float PlaySession::darkness() const {
    float dark = static_cast<float>(1.0 - std::clamp(age_ / kFadeInSeconds, 0.0, 1.0));
    if (ending_ != mb::LevelOutcome::Playing) {
        dark = std::max(dark, static_cast<float>(std::clamp((age_ - endAt_) / kFadeOutSeconds, 0.0, 1.0)));
    }
    return dark;
}

mb::FrameParams PlaySession::frame(float aspect) const {
    mb::FrameParams params;
    params.view = camera_.view();
    params.projection = camera_.projection(aspect);
    params.eye = camera_.eye();
    params.light = scene_->light();
    params.clear = kClearColor;
    return params;
}

std::span<const mb::DrawItem> PlaySession::items() const {
    return scene_->items();
}

void PlaySession::render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) {
    int width = 0, height = 0;
    renderer.drawableSize(width, height);
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 4.0f / 3.0f;
    // The dark is under the HUD and the pause screen: the points stay in sight as the level fades, as in the original.
    std::vector<mb::Sprite> sprites;
    if (const float dark = darkness(); dark > 0) {
        mb::Sprite cover;  // texture 0 draws the tint as a plain colour
        cover.width = static_cast<float>(width);
        cover.height = static_cast<float>(height);
        cover.tint = {0, 0, 0, dark};
        sprites.push_back(cover);
    }
    sprites.insert(sprites.end(), overlay.begin(), overlay.end());
    renderer.renderToWindow(frame(aspect), items(), {}, sprites);
}

}  // namespace mb_app
