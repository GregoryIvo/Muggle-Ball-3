// Loads every converted mesh and the material library from $MB3_ASSETS and prints totals.
// Skips (exit code 77) when the converted assets are not available.

#include "mb/engine/Model.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/TextureIndex.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path assets = env;
    const fs::path meshes = assets / "Data" / "Meshes";
    const fs::path materialsJson = assets / "Data" / "Materials" / "materials.json";
    if (!fs::is_directory(meshes) || !fs::is_regular_file(materialsJson)) {
        std::printf("SKIP: converted meshes or materials.json not found under %s\n", assets.string().c_str());
        return 77;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(meshes)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glb") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);

    int failed = 0;
    std::size_t nodes = 0, vertices = 0, triangles = 0, materials = 0, resolved = 0, missing = 0;
    std::set<fs::path> distinctTextures;
    std::set<std::string> missingNames;
    for (const fs::path& file : files) {
        try {
            const mb::Model model = mb::loadModel(file);
            nodes += model.nodes.size();
            vertices += model.vertices.size();
            triangles += model.indices.size() / 3;
            materials += model.materials.size();
            for (const mb::ModelMaterial& m : model.materials) {
                if (!m.texturePath.empty()) {
                    ++resolved;
                    distinctTextures.insert(m.texturePath);
                } else if (!m.textureFile.empty()) {
                    ++missing;
                    missingNames.insert(m.textureFile);
                }
            }
        } catch (const std::exception& e) {
            std::printf("FAIL %s\n", e.what());
            ++failed;
        }
    }
    std::printf("meshes: %zu files (%d failed), %zu nodes, %zu vertices, %zu triangles, %zu materials\n",
                files.size(), failed, nodes, vertices, triangles, materials);
    std::printf("mesh textures: %zu resolved (%zu distinct files), %zu missing\n", resolved, distinctTextures.size(),
                missing);
    for (const std::string& name : missingNames) {
        std::printf("  missing mesh texture: %s\n", name.c_str());
    }

    try {
        const mb::MaterialLibrary library = mb::MaterialLibrary::load(materialsJson);
        std::set<std::string> names;
        std::size_t blended = 0, additive = 0;
        for (const mb::GameMaterial& m : library.all()) {
            names.insert(m.name);
            blended += m.blendMode() == mb::BlendMode::AlphaBlend;
            additive += m.blendMode() == mb::BlendMode::Additive;
        }
        std::printf("materials.json: %zu materials (%zu distinct names), %zu alpha-blended, %zu additive\n",
                    library.size(), names.size(), blended, additive);

        const fs::path textures = assets / "Textures";
        if (fs::is_directory(textures)) {
            const mb::TextureIndex index(textures);
            std::size_t found = 0, notFound = 0;
            std::set<std::string> unresolved;
            for (const mb::GameMaterial& m : library.all()) {
                for (const std::string& name : m.textures) {
                    if (index.find(name).empty()) {
                        ++notFound;
                        unresolved.insert(name);
                    } else {
                        ++found;
                    }
                }
            }
            std::printf("textures: %zu indexed names; material textures %zu resolved, %zu missing (%zu distinct)\n",
                        index.size(), found, notFound, unresolved.size());
            for (const std::string& name : unresolved) {
                std::printf("  unresolved: %s\n", name.c_str());
            }
        }
    } catch (const std::exception& e) {
        std::printf("FAIL %s\n", e.what());
        ++failed;
    }
    return failed == 0 ? 0 : 1;
}
