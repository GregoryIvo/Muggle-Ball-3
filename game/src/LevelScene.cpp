#include "mb/game/LevelScene.h"

#include <algorithm>
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
    const std::vector<ActorRef> actors = flattenActors(level);
    actorStart_.assign(actors.size() + 1, 0);
    for (const SceneDraw& draw : scene.draws) {
        ++actorStart_[draw.actor + 1];
    }
    for (std::size_t i = 1; i < actorStart_.size(); ++i) {
        actorStart_[i] += actorStart_[i - 1];
    }
    actorItems_.resize(scene.draws.size());
    std::vector<std::uint32_t> filled(actorStart_.begin(), actorStart_.end() - 1);
    for (const SceneDraw& draw : scene.draws) {
        auto [item, animation] = prepare(draw);
        actorItems_[filled[draw.actor]++] = static_cast<std::uint32_t>(items_.size());
        if (animation) {
            animation->item = items_.size();
            animations_.push_back(std::move(*animation));
        }
        if (!draw.damageTextures.empty()) {
            std::vector<TextureHandle>& states = damage_[items_.size()];
            for (const std::filesystem::path& file : draw.damageTextures) {
                states.push_back(file.empty() ? 0 : cache.texture(file));
            }
        }
        const ActorPlan* actor = draw.actor < physics.actors.size() ? &physics.actors[draw.actor] : nullptr;
        if (actor && actor->simulated && actor->motion != MotionType::Static) {
            followers_.push_back({items_.size(), draw.actor, draw.local});
        }
        items_.push_back(item);
    }
    levelItems_ = items_.size();
    hidden_.assign(levelItems_, false);

    // The ball's texture is a single frame, so there is nothing to animate.
    for (const SceneDraw& draw : describeScene(ballLevel(), lookup, materials, textures).draws) {
        ballItems_.push_back(prepare(draw).first);
    }

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
    ballLines_ = sphereLines(kBallRadius * 1.03f, {1.0f, 0.9f, 0.1f, 1});  // a little outside the ball model, which would hide it
    light_ = levelLight(level, materials);
}

void LevelScene::update(double seconds) {
    const auto milliseconds = static_cast<std::int64_t>(seconds * 1000.0);
    for (const Animation& animation : animations_) {
        const std::size_t frame = animationFrame(milliseconds, animation.frameTimeMs, animation.frames.size());
        items_[animation.item].material.texture = animation.frames[frame];
    }
}

void LevelScene::hide(ActorIndex actor) {
    if (actor >= actorStart_.size() - 1) {
        return;
    }
    for (std::uint32_t i = actorStart_[actor]; i < actorStart_[actor + 1]; ++i) {
        hidden_[actorItems_[i]] = true;
    }
}

void LevelScene::setDamage(ActorIndex actor, int index) {
    if (actor >= actorStart_.size() - 1 || index < 0) {
        return;
    }
    for (std::uint32_t i = actorStart_[actor]; i < actorStart_[actor + 1]; ++i) {
        const auto states = damage_.find(actorItems_[i]);
        if (states == damage_.end()) {
            continue;
        }
        const TextureHandle texture = states->second[std::min<std::size_t>(static_cast<std::size_t>(index), states->second.size() - 1)];
        if (texture != 0) {
            items_[actorItems_[i]].material.texture = texture;
        }
    }
}

void LevelScene::appendDrawn(std::vector<DrawItem>& out) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i >= levelItems_ || !hidden_[i]) {
            out.push_back(items_[i]);
        }
    }
}

void LevelScene::follow(const PhysicsScene& physics) {
    for (const Follower& follower : followers_) {
        items_[follower.item].world = placedOnActor(follower.local, physics.actorPose(follower.actor));
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
