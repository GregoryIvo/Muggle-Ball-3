#pragma once

#include "Assets.h"
#include "FrontEnd.h"
#include "GameAudio.h"
#include "PlayerInput.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"
#include "mb/game/ModelCache.h"
#include "mb/game/PhysicsProfile.h"
#include "mb/game/PlayScene.h"
#include "mb/game/PlaySounds.h"
#include "mb/game/PrizeHud.h"
#include "mb/game/SaveData.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mb_app {

inline constexpr mb::Color kClearColor{0.05f, 0.05f, 0.1f, 1.0f};  // only visible where a level has no sky

// A prize the harness gives the level at a time of play, for looking at the effects without waiting for the drops (--drop-prize and
// --catch-prize; docs/GAMEPLAY.md section 16).
struct PrizeCue {
    mb::PrizeKind kind = mb::PrizeKind::Bonus;
    double seconds = 0;     // of play
    bool catchNow = false;  // it appears on the bat and is caught at once; else it falls from the far end of the board
    std::optional<float> x;  // where along the board it falls from; over the bat if not given
};
// "bazuka", "bazuka@12.5" or "bazuka@12.5,-8": a prize by the stem of its file in Data/Objects/Prizes (any case), the seconds of
// play to wait after an @ and, after a comma, where along the board it falls from. False if the text is not that.
bool parsePrizeCue(std::string_view text, bool catchNow, PrizeCue& cue);

// A particle effect the harness sets going at a time of play, for looking at the particle files without waiting for the game to
// need them (--effect; docs/GAMEPLAY.md section 19).
struct EffectCue {
    std::string name;       // a particle file, or "file/name" (ParticleLibrary::find)
    double seconds = 0;     // of play
    mb::Vec3 at{0, 2, 0};   // where it goes off, or a beam starts
    std::optional<mb::Vec3> to;  // a beam's other end (Laser, Lightning); 10 units on along the board if not given
};
// "Fireworks/red", "explosebig@5" or "Lightning@5,-8,1,0,8,1,10": a particle effect, the seconds of play to wait after an @ and,
// after a comma, where it goes off (x, y, z) and, after three more numbers, where a beam ends. False if the text is not that.
bool parseEffectCue(std::string_view text, EffectCue& cue);

struct PlayOptions {
    bool autoplay = false;  // the Autopilot plays: it moves the bat and launches (--autoplay)
    std::vector<PrizeCue> prizes;  // given to every level that is played
    std::vector<EffectCue> effects;  // likewise
    // Every level's PhysicsSettings (--physics-profile; default the platform's own, mb::defaultPhysicsProfileName()).
    mb::PhysicsProfile profile = *mb::physicsProfile(mb::defaultPhysicsProfileName());
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
    // Logs the physics summary of whatever level is still loaded, the same as start() does for the level it leaves.
    ~PlaySession() override;

    bool start(std::string_view levelName, const mb::GameHud& hud, const Progress& progress) override;
    mb::LevelOutcome update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) override;
    bool ending() const override { return ending_ != mb::LevelOutcome::Playing; }
    void resume() override { player_ = {}; }
    void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) override;

    // The picture without a window, for screenshots: what to draw (after start()) and how to look at it.
    mb::FrameParams frame(float aspect) const;
    std::span<const mb::DrawItem> items() const;
    std::span<const mb::EffectBatch> effects() const;
    // The rules, null before a level has started.
    const mb::GameSession* rules() const { return session_.get(); }
    // How dark the picture is, 0 to 1: the fades of the start and the end of a level.
    float darkness() const;
    // The milliseconds update() took since the last call (the rules and the scene, the physics in them), for a frame
    // time report.
    double takeUpdateMillis();

private:
    void giveCues();
    void logPhysicsSummary() const;  // the level in session_, if any: steps taken and dropped, peak awake, bodies

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
    mb::PrizeCaption caption_;
    std::vector<PrizeCue> cues_;  // not yet given
    std::vector<EffectCue> effectCues_;
    std::string currentLevelName_;  // of session_, for logPhysicsSummary()
    int peakAwakeBodies_ = 0;       // the most PhysicsWorld::activeBodyCount() seen this level, sampled every update()

    double age_ = 0;    // seconds since the level began
    double clock_ = 0;  // the frames' time, which runs on beyond the session's steps
    mb::LevelOutcome ending_ = mb::LevelOutcome::Playing;  // how it is going to end, once it is
    double endAt_ = 0;  // when the fade out starts, after the hold
    PlayerInput player_;
    double updateMs_ = 0;
};

}  // namespace mb_app
