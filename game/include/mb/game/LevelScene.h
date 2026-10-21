#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ModelCache.h"
#include "mb/game/SceneBuilder.h"
#include "mb/game/TextureIndex.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mb {

// A level ready to draw: the static draw items, the physics shape wireframes and the light.
// Only animated textures change after construction (update()).
class LevelScene {
public:
    LevelScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
               const Level& level);

    // Advances the texture animations to the given time since the scene was shown.
    void update(double seconds);

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

    std::vector<DrawItem> items_;
    std::vector<DebugLine> shapeLines_;
    std::vector<Animation> animations_;
    DirectionalLight light_;
    SceneReport report_;
};

}  // namespace mb
