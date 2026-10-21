#include "mb/game/LevelScene.h"

namespace mb {

LevelScene::LevelScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
                       const Level& level) {
    const ModelLookup lookup = [&cache](const std::string& meshFile) -> const Model* {
        const ModelCache::Entry* entry = cache.model(meshFile);
        return entry ? &entry->model : nullptr;
    };
    SceneDescription scene = describeScene(level, lookup, materials, textures);
    report_ = std::move(scene.report);

    items_.reserve(scene.draws.size());
    for (const SceneDraw& draw : scene.draws) {
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
            animation.item = items_.size();
            animation.frameTimeMs = draw.animTimeMs;
            animations_.push_back(std::move(animation));
        }
        items_.push_back(item);
    }

    shapeLines_ = mb::shapeLines(level);
    light_ = levelLight(level, materials);
}

void LevelScene::update(double seconds) {
    const auto milliseconds = static_cast<std::int64_t>(seconds * 1000.0);
    for (const Animation& animation : animations_) {
        const auto frame = (milliseconds / animation.frameTimeMs) % static_cast<std::int64_t>(animation.frames.size());
        items_[animation.item].material.texture = animation.frames[static_cast<std::size_t>(frame)];
    }
}

}  // namespace mb
