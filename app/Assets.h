#pragma once

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/SceneBuilder.h"
#include "mb/game/TextureIndex.h"

#include <filesystem>
#include <string>

namespace mb_app {

// Everything that outlives a single level: the converted game's folder and what the levels are built from.
struct Assets {
    std::filesystem::path root = mb::assetRoot();
    mb::MaterialLibrary materials = mb::MaterialLibrary::load(root / "Data" / "Materials" / "materials.json");
    mb::TextureIndex textures{root / "Textures"};
    mb::LevelCatalog catalog{root / "Data" / "Levels"};
};

// Warns of what a level's scene could not find: meshes, frames and textures.
inline void logMissing(const std::string& level, const mb::SceneReport& report) {
    for (const std::string& name : report.missingMeshes) {
        mb::logWarn("{}: missing mesh {}", level, name);
    }
    for (const std::string& name : report.missingFrames) {
        mb::logWarn("{}: missing frame {}", level, name);
    }
    for (const std::string& name : report.missingTextures) {
        mb::logWarn("{}: missing texture {}", level, name);
    }
}

}  // namespace mb_app
