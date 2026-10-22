#pragma once

#include "mb/engine/Model.h"
#include "mb/engine/Renderer.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace mb {

// Loads each converted model and each texture file once and uploads it to the GPU,
// however many levels and actors use it.
class ModelCache {
public:
    struct Entry {
        Model model;
        MeshHandle mesh = 0;
    };

    // meshesDir is assets/Data/Meshes.
    ModelCache(Renderer& renderer, std::filesystem::path meshesDir);

    // meshFile is relative to meshesDir, lowercase (see meshFileName()). Returns nullptr
    // when the file is missing or unreadable; that is logged once per file.
    const Entry* model(const std::string& meshFile);

    // Returns 0 (the renderer's white texture) when the image cannot be decoded; logged once.
    TextureHandle texture(const std::filesystem::path& file);

    // A unit square in the xy plane, centred on the origin, facing -z (towards the viewer of the game's camera), the
    // texture over all of it: what flashes and shadows are drawn on. Made on first use.
    MeshHandle quad();

private:
    Renderer& renderer_;
    std::filesystem::path meshesDir_;
    std::unordered_map<std::string, std::unique_ptr<Entry>> models_;  // null entries are failures
    std::unordered_map<std::string, TextureHandle> textures_;         // keyed by normalised path
    MeshHandle quad_ = 0;
};

}  // namespace mb
