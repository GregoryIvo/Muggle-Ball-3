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

// Vertex layout of the Sprite pipeline: a pixel position, a texture coordinate and a tint.
struct SpriteVertex {
    Vec2 position;
    Vec2 uv;
    Color color;
};
static_assert(sizeof(SpriteVertex) == 32);

// Everything that selects a graphics pipeline. Per-draw data (matrices, colours,
// texture) is not part of it, so a handful of pipelines cover a whole level.
struct PipelineKey {
    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool blend = false;
    BlendFactor srcBlend = BlendFactor::One;  // only meaningful when blend is set
    BlendFactor dstBlend = BlendFactor::Zero;
    CullMode cull = CullMode::Back;
    bool depthWrite = true;
    bool depthTest = true;
    bool lines = false;  // the DebugLine pipeline; blend, cull and depth flags are fixed for it
    bool sprites = false;  // the Sprite pipeline: alpha blending, no culling, no depth
    bool effects = false;  // the Effect pipeline (EffectVertex): no culling, no depth write; blend and depthTest as set

    // Resolves BlendMode::AlphaBlend and Additive to their factors, so materials that blend the same way
    // share one pipeline however they were described.
    static PipelineKey forMaterial(SDL_GPUTextureFormat colorFormat, const MaterialParams& material);
    static PipelineKey forLines(SDL_GPUTextureFormat colorFormat);
    static PipelineKey forSprites(SDL_GPUTextureFormat colorFormat);
    // `blend` is an EffectBatch's: Additive, or alpha blending for any other.
    static PipelineKey forEffects(SDL_GPUTextureFormat colorFormat, BlendMode blend, bool depthTest);

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
    SDL_GPUShader* spriteVertex_ = nullptr;
    SDL_GPUShader* spriteFragment_ = nullptr;
    SDL_GPUShader* effectVertex_ = nullptr;
    SDL_GPUShader* effectFragment_ = nullptr;
    std::unordered_map<std::uint64_t, SDL_GPUGraphicsPipeline*> pipelines_;
};

}  // namespace mb
