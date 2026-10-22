#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ModelCache.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/SceneBuilder.h"
#include "mb/game/TextureIndex.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace mb {

// A level ready to draw: the draw items, the physics shape wireframes and the light. Textures animate
// (update()) and whatever the physics can move follows it (follow(), followShapes()); the rest stands
// as the level saved it. A game also takes bricks out of the picture (hide()), shows them damaged (setDamage()) and
// shrinking away (setScale()).
class LevelScene {
public:
    // `physics` says which actors can move; it is not kept.
    LevelScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level,
               const PhysicsPlan& physics);

    // Advances the texture animations to the given time since the scene was shown.
    void update(double seconds);

    // Puts every draw of an actor that can move where the physics has the actor (draw world = node transform *
    // actor pose), and the test balls where they are. Call after stepping the physics, before drawing.
    void follow(const PhysicsScene& physics);
    // The same for the shape wireframes (including the balls'), which only need it while they are shown.
    void followShapes(const PhysicsScene& physics);

    // Takes an actor out of the picture for good: a brick that exploded, a piece that fell off the board.
    void hide(ActorIndex actor);
    // Draws an actor at `scale` of its size about the world point `about`, like a person who is shrinking away. It goes on
    // following the physics, so a moving actor needs the call again with where it has got to; 1 draws it whole.
    void setScale(ActorIndex actor, float scale, Vec3 about);
    // Shows an actor in its damage state `index` (damageTextureIndex() in Gameplay.h): each of its draws that has damage
    // textures (SceneDraw::damageTextures) takes the one at that place. Other draws and states whose texture is
    // missing stay as they are.
    void setDamage(ActorIndex actor, int index);

    // Every draw of the level as it stands, those of hidden actors included, then the test balls' after follow(): what
    // the level viewer, which hides nothing, draws.
    std::span<const DrawItem> items() const { return items_; }
    // Appends what is to be shown: the draws of the actors that are not hidden, then the test balls'.
    void appendDrawn(std::vector<DrawItem>& out) const;
    // The draws of one ball at the origin (the game's ball.x), for a game that places its own balls.
    std::span<const DrawItem> ballItems() const { return ballItems_; }
    std::span<const DebugLine> shapeLines() const { return shapeLines_; }
    const DirectionalLight& light() const { return light_; }
    const SceneReport& report() const { return report_; }

private:
    struct Animation {
        std::size_t item = 0;  // index into items_
        std::vector<TextureHandle> frames;
        int frameTimeMs = 0;
    };
    struct Follower {
        std::size_t item = 0;  // index into items_
        ActorIndex actor = kNoActor;
        Mat4 local;
    };
    struct MovingLines {
        ActorIndex actor = kNoActor;
        std::size_t first = 0;            // index into shapeLines_
        std::vector<DebugLine> local;     // in the actor's frame
    };

    std::vector<DrawItem> items_;  // the level's, then the balls' after the last follow()
    std::size_t levelItems_ = 0;
    std::vector<DrawItem> ballItems_;  // one ball at the origin
    std::vector<DebugLine> shapeLines_;
    std::size_t levelLines_ = 0;
    std::vector<DebugLine> ballLines_;
    std::vector<Animation> animations_;
    std::vector<Follower> followers_;
    // The level's draws by actor (a table of offsets into actorItems_), what a hidden actor does not show, and the
    // textures of the damage states of the draws that have some.
    std::vector<std::uint32_t> actorStart_, actorItems_;
    std::vector<bool> hidden_;  // by item
    std::unordered_map<std::size_t, Mat4> scaled_;  // by item: what setScale() does to the draw after it is placed
    std::unordered_map<std::size_t, std::vector<TextureHandle>> damage_;  // by item
    std::vector<MovingLines> movingLines_;
    DirectionalLight light_;
    SceneReport report_;
};

}  // namespace mb
