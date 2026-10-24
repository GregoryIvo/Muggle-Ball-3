#pragma once

#include "../RenderCommon.h"
#include "mb/engine/Renderer.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <unordered_map>

namespace mb {

// Everything that selects a graphics pipeline: the backend-neutral render::RenderState plus the one
// SDL GPU detail it needs, the colour target's format.
struct PipelineKey {
    render::RenderState state;
    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

    static PipelineKey forMaterial(SDL_GPUTextureFormat colorFormat, const MaterialParams& material) {
        return {render::RenderState::forMaterial(material), colorFormat};
    }
    static PipelineKey forLines(SDL_GPUTextureFormat colorFormat) {
        return {render::RenderState::forLines(), colorFormat};
    }
    static PipelineKey forSprites(SDL_GPUTextureFormat colorFormat) {
        return {render::RenderState::forSprites(), colorFormat};
    }
    static PipelineKey forEffects(SDL_GPUTextureFormat colorFormat, BlendMode blend, bool depthTest) {
        return {render::RenderState::forEffects(blend, depthTest), colorFormat};
    }

    // Unique per distinct key; also used to sort draws so equal pipelines sit together.
    std::uint64_t pack() const { return (static_cast<std::uint64_t>(colorFormat) << 32) | state.pack(); }
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
    SDL_GPUShader* spriteVertex_ = nullptr;
    SDL_GPUShader* spriteFragment_ = nullptr;
    SDL_GPUShader* effectVertex_ = nullptr;
    SDL_GPUShader* effectFragment_ = nullptr;
    std::unordered_map<std::uint64_t, SDL_GPUGraphicsPipeline*> pipelines_;
};

}  // namespace mb
