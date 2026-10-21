#include "mb/game/LevelScene.h"

#include <optional>
#include <utility>

namespace mb {

namespace {

// What a test ball looks like: the game's own ball, drawn like any object of the game.
Level ballLevel() {
    GameObject object;
    object.type = ObjectType::Ball;
    object.mesh = "ball.x";
    object.material = "ballMaterial";
    Actor actor;
    actor.object = object;
    Level level;
    level.actors.push_back(actor);
    return level;
}

}  // namespace

LevelScene::LevelScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
                       const Level& level, const PhysicsPlan& physics) {
    const ModelLookup lookup = [&cache](const std::string& meshFile) -> const Model* {
        const ModelCache::Entry* entry = cache.model(meshFile);
        return entry ? &entry->model : nullptr;
    };
    SceneDescription scene = describeScene(level, lookup, materials, textures);
    report_ = std::move(scene.report);

    // Fills in the GPU handles of a draw. A draw whose texture animates also gets an entry in animations_.
    const auto prepare = [&](const SceneDraw& draw) {
        DrawItem item = draw.item;
        item.mesh = cache.model(draw.meshFile)->mesh;  // describeScene only emits draws for models the lookup found

        Animation animation;
        for (const std::filesystem::path& file : draw.textures) {
            animation.frames.push_back(cache.texture(file));
        }
        if (!animation.frames.empty()) {
            item.material.texture = animation.frames.front();
        }
        if (draw.animTimeMs > 0 && animation.frames.size() > 1) {
            animation.frameTimeMs = draw.animTimeMs;
            return std::pair{item, std::optional(std::move(animation))};
        }
        return std::pair{item, std::optional<Animation>()};
    };

    items_.reserve(scene.draws.size());
    for (const SceneDraw& draw : scene.draws) {
        auto [item, animation] = prepare(draw);
        if (animation) {
            animation->item = items_.size();
            animations_.push_back(std::move(*animation));
        }
        const ActorPlan* actor = draw.actor < physics.actors.size() ? &physics.actors[draw.actor] : nullptr;
        if (actor && actor->simulated && actor->motion != MotionType::Static) {
            followers_.push_back({items_.size(), draw.actor, draw.local});
        }
        items_.push_back(item);
    }
    levelItems_ = items_.size();

    // The ball's texture is a single frame, so there is nothing to animate.
    for (const SceneDraw& draw : describeScene(ballLevel(), lookup, materials, textures).draws) {
        ballItems_.push_back(prepare(draw).first);
    }

    const std::vector<ActorRef> actors = flattenActors(level);
    for (ActorShapeLines& entry : actorShapeLines(level)) {
        const Mat4 pose = rigidTransform(actors[entry.actor].actor->pose);
        const ActorPlan* actor = entry.actor < physics.actors.size() ? &physics.actors[entry.actor] : nullptr;
        if (actor && actor->simulated && actor->motion != MotionType::Static) {
            movingLines_.push_back({entry.actor, shapeLines_.size(), entry.lines});
        }
        for (const DebugLine& line : entry.lines) {
            shapeLines_.push_back({transformPoint(line.a, pose), transformPoint(line.b, pose), line.color});
        }
    }
    levelLines_ = shapeLines_.size();
    ballLines_ = sphereLines(kBallRadius, {1.0f, 0.9f, 0.1f, 1});
    light_ = levelLight(level, materials);
}

void LevelScene::update(double seconds) {
    const auto milliseconds = static_cast<std::int64_t>(seconds * 1000.0);
    for (const Animation& animation : animations_) {
        const std::size_t frame = animationFrame(milliseconds, animation.frameTimeMs, animation.frames.size());
        items_[animation.item].material.texture = animation.frames[frame];
    }
}

void LevelScene::follow(const PhysicsScene& physics) {
    for (const Follower& follower : followers_) {
        items_[follower.item].world = follower.local * physics.actorPose(follower.actor);
    }
    items_.resize(levelItems_);
    for (const PhysicsScene::Ball& ball : physics.balls()) {
        const Mat4 pose = physics.world().pose(ball.body);
        for (DrawItem item : ballItems_) {
            item.world = item.world * pose;
            items_.push_back(item);
        }
    }
}

void LevelScene::followShapes(const PhysicsScene& physics) {
    for (const MovingLines& moving : movingLines_) {
        const Mat4 pose = physics.actorPose(moving.actor);
        for (std::size_t i = 0; i < moving.local.size(); ++i) {
            const DebugLine& line = moving.local[i];
            shapeLines_[moving.first + i] = {transformPoint(line.a, pose), transformPoint(line.b, pose), line.color};
        }
    }
    shapeLines_.resize(levelLines_);
    for (const PhysicsScene::Ball& ball : physics.balls()) {
        const Mat4 pose = physics.world().pose(ball.body);
        for (const DebugLine& line : ballLines_) {
            shapeLines_.push_back({transformPoint(line.a, pose), transformPoint(line.b, pose), line.color});
        }
    }
}

}  // namespace mb
