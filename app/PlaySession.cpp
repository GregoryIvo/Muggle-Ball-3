#include "PlaySession.h"

#include "mb/engine/Log.h"
#include "mb/game/Autopilot.h"
#include "mb/game/LevelLoader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

namespace mb_app {

namespace {

// From the footage of V1 (docs/GAMEPLAY.md): the picture holds for 0.1 s after the last required object is gone (a person
// or an animal has shrunk away by then), goes dark, and the next level comes up out of the dark. What the picture does
// as the game is lost was not measured: it holds 0.45 s [?].
constexpr double kFadeInSeconds = 0.6;
constexpr double kCompleteHoldSeconds = 0.1;
constexpr double kGameOverHoldSeconds = 0.45;
constexpr double kFadeOutSeconds = 0.8;
// A prize the harness drops starts at the far end of the board.
constexpr float kCueDropZ = 18.0f;
// An effect it sets going: a beam without an end runs this far along the board; an emitter emits for this long, a beam is held.
constexpr float kCueBeamLength = 10.0f;
constexpr float kCueSeconds = 3.0f;

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

}  // namespace

bool parsePrizeCue(std::string_view text, bool catchNow, PrizeCue& cue) {
    const std::size_t at = text.find('@'), comma = text.find(',');
    const std::optional<mb::PrizeKind> kind = mb::prizeByStem(text.substr(0, std::min(at, comma)));
    // strtod, not std::from_chars: Apple clang 15's library has no floating-point from_chars.
    const auto number = [](std::string_view field, auto& value) {
        const std::string text(field);
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) || end != text.c_str() + text.size()) {
            return false;
        }
        value = static_cast<std::remove_reference_t<decltype(value)>>(parsed);
        return true;
    };
    PrizeCue parsed;
    if (!kind || (at != std::string_view::npos && comma < at)) {
        return false;
    }
    parsed.kind = *kind;
    parsed.catchNow = catchNow;
    if (at != std::string_view::npos && (!number(text.substr(at + 1, comma - at - 1), parsed.seconds) || !(parsed.seconds >= 0))) {
        return false;
    }
    if (comma != std::string_view::npos) {
        float x = 0;
        if (!number(text.substr(comma + 1), x) || !std::isfinite(x)) {
            return false;
        }
        parsed.x = x;
    }
    cue = parsed;
    return true;
}

bool parseEffectCue(std::string_view text, EffectCue& cue) {
    const std::size_t at = text.find('@');
    EffectCue parsed;
    parsed.name = std::string(text.substr(0, at));
    if (parsed.name.empty()) {
        return false;
    }
    if (at != std::string_view::npos) {
        // The numbers after the @: the seconds, then x, y and z, then a beam's other end.
        std::vector<float> numbers;
        for (std::size_t start = at + 1;;) {
            const std::size_t comma = text.find(',', start);
            const std::string field(text.substr(start, comma == std::string_view::npos ? comma : comma - start));
            char* end = nullptr;
            const float value = std::strtof(field.c_str(), &end);
            if (field.empty() || std::isspace(static_cast<unsigned char>(field.front())) || end != field.c_str() + field.size() || !std::isfinite(value)) {
                return false;
            }
            numbers.push_back(value);
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        if ((numbers.size() != 1 && numbers.size() != 4 && numbers.size() != 7) || !(numbers[0] >= 0)) {
            return false;
        }
        parsed.seconds = numbers[0];
        if (numbers.size() > 1) {
            parsed.at = {numbers[1], numbers[2], numbers[3]};
        }
        if (numbers.size() > 4) {
            parsed.to = mb::Vec3{numbers[4], numbers[5], numbers[6]};
        }
    }
    cue = std::move(parsed);
    return true;
}

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

PlaySession::~PlaySession() {
    logPhysicsSummary();
}

// docs/PHYSICS.md: fixed steps taken and dropped (GameSession::droppedSteps(), not a failure, the host can drop
// steps when it stalls: its own accumulator's drops plus the world's, see Gameplay.h), the peak awake and total
// body counts the Vita profile's world size is sized from, and the step allocator's high-water mark.
void PlaySession::logPhysicsSummary() const {
    if (!session_) {
        return;
    }
    const mb::PhysicsWorld& world = session_->scene().world();
    const std::uint64_t steps = static_cast<std::uint64_t>(std::llround(session_->time() / world.settings().fixedStep));
    mb::logInfo("physics: {} ended after {} steps ({} dropped), peak {} awake, {} bodies, temp allocator peak {} bytes",
                currentLevelName_, steps, session_->droppedSteps(), peakAwakeBodies_, world.bodyCount(),
                world.tempAllocatorPeakBytes());
}

bool PlaySession::start(std::string_view levelName, const mb::GameHud& hud, const Progress& progress) {
    const std::optional<std::size_t> index = assets_.catalog.find(levelName);
    if (!index) {
        mb::logError("the campaign names the level \"{}\", which is not among the converted levels", levelName);
        return false;
    }
    logPhysicsSummary();  // the level being left, if there was one
    const mb::LevelEntry& entry = assets_.catalog.levels()[*index];
    try {
        const Clock::time_point begin = Clock::now();
        const mb::Level level = mb::loadLevelJson(entry.path);
        progress(0.2f);
        mb::SessionOptions rules;
        rules.hearts = hud.lives;
        rules.score = hud.score;
        rules.campaignLevel = hud.level;
        // What drops differs from play to play; an autoplay (the screenshots, the scripted checks) repeats.
        rules.prizeSeed = options_.autoplay ? static_cast<std::uint64_t>(hud.level) : std::random_device{}();
        rules.physicsSettings = mb::physicsSettingsFor(options_.profile, entry.name);
        const mb::PhysicsSettings& ps = rules.physicsSettings;
        mb::logInfo("physics: profile {}, {}/{}/{} at {:.4f} s, threads {}, maxSteps {}", options_.profile.name,
                    ps.velocitySteps, ps.positionSteps, ps.collisionSteps, ps.fixedStep, ps.threads, ps.maxStepsPerCall);
        auto session = std::make_unique<mb::GameSession>(level, assets_.materials, rules);
        progress(0.6f);
        auto scene = std::make_unique<mb::PlayScene>(models_, assets_.materials, assets_.textures, assets_.particles, level,
                                                     *session, save_.settings.bat);
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
    caption_ = {};
    cues_ = options_.prizes;
    effectCues_ = options_.effects;
    currentLevelName_ = entry.name;
    peakAwakeBodies_ = 0;
    return true;
}

void PlaySession::giveCues() {
    const mb::BatView& bat = session_->bat();
    std::erase_if(cues_, [&](const PrizeCue& cue) {
        if (session_->time() < cue.seconds || (cue.catchNow && bat.phase != mb::BatPhase::Present)) {
            return false;
        }
        session_->dropPrize(cue.kind, cue.catchNow ? bat.position : mb::Vec3{cue.x.value_or(bat.position.x), 0, kCueDropZ});
        return true;
    });
    std::erase_if(effectCues_, [&](const EffectCue& cue) {
        if (session_->time() < cue.seconds) {
            return false;
        }
        const mb::ParticleDef* def = assets_.particles.find(cue.name);
        if (def == nullptr) {
            mb::logError("--effect: no particle effect named {}", cue.name);
        } else if (def->type == mb::ParticleType::Laser || def->type == mb::ParticleType::Lightning) {
            scene_->particles().beam(cue.name, cue.at, cue.to.value_or(cue.at + mb::Vec3{0, 0, kCueBeamLength}), {.seconds = kCueSeconds});
        } else {
            scene_->particles().spawn(cue.name, cue.at, {.seconds = kCueSeconds});  // the emitters run for a while
        }
        return true;
    });
}

mb::LevelOutcome PlaySession::update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) {
    const Clock::time_point began = Clock::now();
    int width = 0, height = 0;
    renderer_.drawableSize(width, height);
    age_ += seconds;
    clock_ += seconds;
    giveCues();
    session_->update(seconds, options_.autoplay ? mb::Autopilot{}.decide(*session_) : player_.read(input, width, height, camera_));
    peakAwakeBodies_ = std::max(peakAwakeBodies_, session_->scene().world().activeBodyCount());

    const std::vector<mb::GameEvent> events = session_->takeEvents();
    for (const mb::GameEvent& event : events) {
        if (sounds_) {
            sounds_->play(event);
        }
        if (ending_ == mb::LevelOutcome::Playing && event.type == mb::EventType::LevelComplete) {
            ending_ = mb::LevelOutcome::Complete;
            endAt_ = age_ + kCompleteHoldSeconds;
        } else if (ending_ == mb::LevelOutcome::Playing && event.type == mb::EventType::GameOver) {
            ending_ = mb::LevelOutcome::GameOver;
            endAt_ = age_ + kGameOverHoldSeconds;
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
    caption_.update(events, seconds);
    hud.powerUps = mb::powerUpsOf(session_->activeEffects());
    hud.caption = std::string(caption_.key());
    hud.captionAlpha = caption_.alpha();
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

std::span<const mb::EffectBatch> PlaySession::effects() const {
    return scene_->effects();
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
    renderer.renderToWindow(frame(aspect), items(), {}, sprites, effects());
}

}  // namespace mb_app
