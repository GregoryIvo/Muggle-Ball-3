#pragma once

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ParticleDef.h"
#include "mb/game/SceneBuilder.h"
#include "mb/game/TextureIndex.h"

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace mb_app {

// mb::assetRoot(), or an error that says where the converted game files go if it has none (instead of the first file
// deep inside that cannot be opened).
inline std::filesystem::path requireAssetRoot() {
    std::filesystem::path root = mb::assetRoot();
    std::error_code ec;
    if (!std::filesystem::is_directory(root / "Data", ec)) {
        throw std::runtime_error(std::format(
            "The game's files were not found (there is no Data folder in {}).\n\nConvert them from your copy of Magic "
            "Ball 3 with tools/convert_assets.py, then put the assets folder it makes in {}, or set MB3_ASSETS to it.",
            root.string(), mb::userDataDir().string()));
    }
    return root;
}

// Everything that outlives a single level: the converted game's folder and what the levels are built from.
struct Assets {
    std::filesystem::path root = requireAssetRoot();
    mb::MaterialLibrary materials = mb::MaterialLibrary::load(root / "Data" / "Materials" / "materials.json");
    mb::TextureIndex textures{root / "Textures"};
    mb::LevelCatalog catalog{root / "Data" / "Levels"};
    mb::ParticleLibrary particles = mb::ParticleLibrary::load(root / "Data" / "Particles");
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
