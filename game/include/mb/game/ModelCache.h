#pragma once

#include "mb/engine/Model.h"
#include "mb/engine/Renderer.h"

#include <filesystem>
#include <memory>
#include <span>
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

    // A mesh of the caller's own that changes shape (a sail): made from these vertices, drawn with these indices, and rewritten by
    // updateMesh() with as many vertices as it began with. It is the caller's to keep; the cache does not know it.
    MeshHandle dynamicMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void updateMesh(MeshHandle mesh, std::span<const Vertex> vertices);
    // Gives a dynamicMesh() back: the GPU's buffers are freed once the frames in flight are done with them.
    void destroyMesh(MeshHandle mesh);

private:
    Renderer& renderer_;
    std::filesystem::path meshesDir_;
    std::unordered_map<std::string, std::unique_ptr<Entry>> models_;  // null entries are failures
    std::unordered_map<std::string, TextureHandle> textures_;         // keyed by normalised path
    MeshHandle quad_ = 0;
};

}  // namespace mb
