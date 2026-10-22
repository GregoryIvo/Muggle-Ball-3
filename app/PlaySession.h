#pragma once

#include "Assets.h"
#include "FrontEnd.h"
#include "GameAudio.h"
#include "PlayerInput.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"
#include "mb/game/ModelCache.h"
#include "mb/game/PlayScene.h"
#include "mb/game/PlaySounds.h"
#include "mb/game/SaveData.h"

#include <memory>
#include <optional>
#include <span>

namespace mb_app {

inline constexpr mb::Color kClearColor{0.05f, 0.05f, 0.1f, 1.0f};  // only visible where a level has no sky

struct PlayOptions {
    bool autoplay = false;  // the Autopilot plays: it moves the bat and launches (--autoplay)
};

// The game's level, for the front end: builds the scene and the rules of a level, plays it and draws it. The cursor is
// the bat (the point of the board under it, as the camera sees it), a click or Space launches the ball; the events
// of the rules become the sounds and the flashes. When the level ends, won or lost, the picture fades out and update()
// reports how (the front end moves on from there); a level starts with a fade in. The hearts and points the level opens
// with come in `hud`, and it keeps them there for the front end and for the next level.
class PlaySession : public LevelSession {
public:
    // `save` is read for the player's bat; `audio` may be null (no sound). Everything must outlive the session.
    PlaySession(mb::Renderer& renderer, Assets& assets, const mb::SaveData& save, mb::GameAudio* audio,
                const PlayOptions& options);

    bool start(std::string_view levelName, const mb::GameHud& hud, const Progress& progress) override;
    mb::LevelOutcome update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) override;
    bool ending() const override { return ending_ != mb::LevelOutcome::Playing; }
    void resume() override { player_ = {}; }
    void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) override;

    // The picture without a window, for screenshots: what to draw (after start()) and how to look at it.
    mb::FrameParams frame(float aspect) const;
    std::span<const mb::DrawItem> items() const;
    // The rules, null before a level has started.
    const mb::GameSession* rules() const { return session_.get(); }
    // How dark the picture is, 0 to 1: the fades of the start and the end of a level.
    float darkness() const;
    // The milliseconds update() took since the last call (the rules and the scene, the physics in them), for a frame
    // time report.
    double takeUpdateMillis();

private:
    mb::Renderer& renderer_;
    Assets& assets_;
    const mb::SaveData& save_;
    mb::GameAudio* audio_;
    PlayOptions options_;
    mb::ModelCache models_;
    mb::Camera camera_;
    std::unique_ptr<mb::GameSession> session_;
    std::unique_ptr<mb::PlayScene> scene_;
    std::optional<mb::PlaySounds> sounds_;

    double age_ = 0;    // seconds since the level began
    double clock_ = 0;  // the frames' time, which runs on beyond the session's steps
    mb::LevelOutcome ending_ = mb::LevelOutcome::Playing;  // how it is going to end, once it is
    double endAt_ = 0;  // when the fade out starts, after the hold
    PlayerInput player_;
    double updateMs_ = 0;
};

}  // namespace mb_app
