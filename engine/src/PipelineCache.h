#pragma once

#include "mb/engine/Renderer.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <unordered_map>

namespace mb {

// Vertex layout of the DebugLine pipeline (the mesh pipelines use mb::Vertex).
struct LineVertex {
    Vec3 position;
    Color color;
};
static_assert(sizeof(LineVertex) == 28);

// Everything that selects a graphics pipeline. Per-draw data (matrices, colours,
// texture) is not part of it, so a handful of pipelines cover a whole level.
struct PipelineKey {
    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    bool depthWrite = true;
    bool depthTest = true;
    bool lines = false;  // the DebugLine pipeline; blend, cull and depth flags are fixed for it

    static PipelineKey forLines(SDL_GPUTextureFormat colorFormat);

    // Unique per distinct key; also used to sort draws so equal pipelines sit together.
    std::uint64_t pack() const;
};

// Owns the compiled shaders and creates graphics pipelines lazily, one per key.
class PipelineCache {
public:
    // Throws std::runtime_error if the shaders cannot be created on this device.
    PipelineCache(SDL_GPUDevice* device, SDL_GPUTextureFormat depthFormat);
    ~PipelineCache();
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    // Null (after logging the error once) if the pipeline cannot be created.
    SDL_GPUGraphicsPipeline* get(const PipelineKey& key);

private:
    SDL_GPUGraphicsPipeline* create(const PipelineKey& key);
    void releaseShaders();

    SDL_GPUDevice* device_;
    SDL_GPUTextureFormat depthFormat_;
    SDL_GPUShader* meshVertex_ = nullptr;
    SDL_GPUShader* meshFragment_ = nullptr;
    SDL_GPUShader* lineVertex_ = nullptr;
    SDL_GPUShader* lineFragment_ = nullptr;
    std::unordered_map<std::uint64_t, SDL_GPUGraphicsPipeline*> pipelines_;
};

}  // namespace mb
