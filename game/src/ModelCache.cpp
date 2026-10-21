#include "mb/game/ModelCache.h"

#include "mb/engine/Image.h"
#include "mb/engine/Log.h"

#include <exception>
#include <stdexcept>

namespace mb {

ModelCache::ModelCache(Renderer& renderer, std::filesystem::path meshesDir)
    : renderer_(renderer), meshesDir_(std::move(meshesDir)) {}

const ModelCache::Entry* ModelCache::model(const std::string& meshFile) {
    if (const auto found = models_.find(meshFile); found != models_.end()) {
        return found->second.get();
    }
    std::unique_ptr<Entry> entry;
    try {
        entry = std::make_unique<Entry>();
        entry->model = loadModel(meshesDir_ / meshFile);
        entry->mesh = renderer_.createMesh(entry->model.vertices, entry->model.indices);
        if (entry->mesh == 0) {
            throw std::runtime_error("uploading the mesh to the GPU failed");
        }
    } catch (const std::exception& e) {
        logWarn("cannot load mesh {}: {}", meshFile, e.what());
        entry.reset();
    }
    return models_.emplace(meshFile, std::move(entry)).first->second.get();
}

TextureHandle ModelCache::texture(const std::filesystem::path& file) {
    // Paths from a model are normalised and those from the texture index are not, so the same
    // file can arrive spelled two ways (for example under a relative asset root).
    const std::string key = file.lexically_normal().string();
    if (const auto found = textures_.find(key); found != textures_.end()) {
        return found->second;
    }
    TextureHandle handle = 0;
    if (const std::optional<Image> image = loadImage(file)) {  // logs its own warning
        handle = renderer_.createTexture(*image);
    }
    textures_.emplace(key, handle);
    return handle;
}

}  // namespace mb
