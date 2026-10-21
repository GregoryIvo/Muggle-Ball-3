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
#include <vector>

namespace mb {

// A level ready to draw: the draw items, the physics shape wireframes and the light. Textures animate
// (update()) and whatever the physics can move follows it (follow(), followShapes()); the rest stands
// as the level saved it.
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

    std::span<const DrawItem> items() const { return items_; }
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
    std::vector<MovingLines> movingLines_;
    DirectionalLight light_;
    SceneReport report_;
};

}  // namespace mb
