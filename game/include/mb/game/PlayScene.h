#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/ModelCache.h"
#include "mb/game/ObjectEffects.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace mb {

// Where the bat is drawn, from the session's BatView: its model stands on the floor under the centre of its collision
// sphere. It swoops in from the lower left, spinning, and lands under the cursor; when it leaves it spins off towards
// the corner on its side of the board (docs/GAMEPLAY.md section 4: V1 52.2-52.9, 7874.2-7874.8, 7876.5-7876.7).
struct BatPose {
    bool visible = true;
    Vec3 position{};  // of the model's origin
    float yaw = 0;    // about +y, radians
    float scale = 1;
};
// `lead` is how far ahead of the session's last step the picture is (see PlayScene::update), for the bat on the move.
BatPose batPose(const BatView& bat, float lead = 0);
// How much of the ball held on the bat is shown, 0 to 1: it appears as the bat lands.
float heldBallScale(const BatView& bat);
// Where a ball is drawn, `lead` seconds past the session's last step: one in flight carries on with its velocity, one held
// on the bat goes with the bat (the lead batPose() gives it), so that it does not shimmer against the bat as it moves.
Vec3 ballPosition(const BallView& ball, const BatView& bat, float lead);

// A GameSession on screen: the level as the rules have it (what exploded or fell is gone, what was hit shows its
// damage, a person or an animal that was destroyed shrinks away), the bat, the balls and the flashes where bricks
// explode, as a list of draws. It reads the session and the events it reported; it draws nothing itself and knows no
// window.
//
//     PlayScene scene(cache, materials, textures, level, session, settings.bat);
//     // every frame, after session.update():
//     scene.update(session, session.takeEvents(), dt);
//     renderer.renderToWindow(frame, scene.items(), {}, overlay);   // with scene.light() in the frame
class PlayScene {
public:
    // `bat` is Settings::bat: 0 red, 1 blue, 2 yellow.
    PlayScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level,
              const GameSession& session, int bat);

    // Brings the picture up to the session as it stands, `seconds` after the last call, with the events it reported since
    // then (not kept). The session steps by 1/60 s and does not interpolate; `lead` is how far past its last step the
    // picture is (the time the frames have run on beyond it, under one step), and carries the balls and the bat on with
    // their velocities.
    void update(const GameSession& session, std::span<const GameEvent> events, double seconds, double lead = 0);

    std::span<const DrawItem> items() const { return items_; }
    const DirectionalLight& light() const { return level_.light(); }
    const SceneReport& report() const { return level_.report(); }

private:
    struct Roll {
        Vec3 at{};
        Mat4 turn;  // how the ball is turned, by the distance it rolled
        bool placed = false;
    };
    struct Vanishing {
        Vec3 at{}, velocity{};
        Mat4 turn;
        double age = 0;
    };
    struct Explosion {
        Vec3 at{};
        double age = 0;
        std::uint32_t seed = 0;
    };

    void addBall(Vec3 at, const Mat4& turn, float scale);
    void addExplosion(const Explosion& explosion);
    void addBillboard(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend);

    LevelScene level_;
    LevelScene bat_;  // the bat's own frames, at the origin
    MeshHandle quad_ = 0;
    TextureHandle flashTexture_ = 0, sparkTexture_ = 0;
    Mat4 facing_;  // turns a quad to face the game's camera
    double time_ = 0;
    std::unordered_map<int, Roll> rolls_;  // by BallView::id
    std::vector<Vanishing> vanishing_;
    std::vector<ActorIndex> shrinking_;  // paid for and shrinking, until the session has removed them
    std::vector<Explosion> explosions_;
    ObjectEffects effects_;  // the special objects' (dynamite, cannons, rockets, teleports ...)
    std::vector<DrawItem> items_;
};

}  // namespace mb
