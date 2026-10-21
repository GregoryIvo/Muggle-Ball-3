// Describes every converted level against the real meshes, materials and textures in $MB3_ASSETS
// (no GPU needed) and fails if anything cannot be found. Skips (exit code 77) without the assets.

#include "mb/engine/Model.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/SceneBuilder.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <string>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path assets = env;
    const fs::path levels = assets / "Data" / "Levels";
    const fs::path materialsJson = assets / "Data" / "Materials" / "materials.json";
    if (!fs::is_directory(levels) || !fs::is_directory(assets / "Textures") || !fs::is_regular_file(materialsJson)) {
        std::printf("SKIP: converted levels, textures or materials not found under %s\n", assets.string().c_str());
        return 77;
    }

    const mb::MaterialLibrary materials = mb::MaterialLibrary::load(materialsJson);
    const mb::TextureIndex textures(assets / "Textures");
    mb::LevelCatalog catalog(levels);

    std::map<std::string, std::unique_ptr<mb::Model>> models;  // null: could not be loaded
    const mb::ModelLookup lookup = [&](const std::string& file) -> const mb::Model* {
        auto it = models.find(file);
        if (it == models.end()) {
            std::unique_ptr<mb::Model> model;
            try {
                model = std::make_unique<mb::Model>(mb::loadModel(assets / "Data" / "Meshes" / file));
            } catch (const std::exception& e) {
                std::printf("  cannot load %s: %s\n", file.c_str(), e.what());
            }
            it = models.emplace(file, std::move(model)).first;
        }
        return it->second.get();
    };

    int failed = 0;
    std::size_t draws = 0;
    for (const mb::LevelEntry& entry : catalog.levels()) {
        try {
            const mb::Level level = mb::loadLevelJson(entry.path);
            const mb::SceneDescription scene = mb::describeScene(level, lookup, materials, textures);
            draws += scene.draws.size();
            bool sky = false;
            for (const mb::SceneDraw& draw : scene.draws) {
                sky = sky || draw.item.layer == mb::kBackgroundLayer;
            }
            bool ok = !scene.draws.empty() && sky;
            for (const std::string& name : scene.report.missingMeshes) {
                std::printf("%s: missing mesh %s\n", entry.name.c_str(), name.c_str());
                ok = false;
            }
            for (const std::string& name : scene.report.missingFrames) {
                std::printf("%s: missing frame %s\n", entry.name.c_str(), name.c_str());
                ok = false;
            }
            for (const std::string& name : scene.report.missingTextures) {
                std::printf("%s: missing texture %s\n", entry.name.c_str(), name.c_str());
                ok = false;
            }
            if (!ok) {
                std::printf("FAIL %s (%zu draws, sky %s)\n", entry.name.c_str(), scene.draws.size(), sky ? "yes" : "no");
                ++failed;
            }
        } catch (const std::exception& e) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("scenes: %zu levels, %zu draws, %zu models, %d failed\n", catalog.levels().size(), draws, models.size(),
                failed);
    return failed == 0 && !catalog.levels().empty() ? 0 : 1;
}
