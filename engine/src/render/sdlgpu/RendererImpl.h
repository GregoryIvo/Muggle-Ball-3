#pragma once

#include "../RenderCommon.h"
#include "PipelineCache.h"
#include "mb/engine/Renderer.h"

#include <SDL3/SDL_gpu.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace mb {

class Window;

// Textures and the offscreen target are plain 8-bit linear colour, as in the 2006
// game, which did no gamma correction: nothing here is sRGB.
inline constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

struct TransferBufferDeleter {
    SDL_GPUDevice* device;
    void operator()(SDL_GPUTransferBuffer* buffer) const { SDL_ReleaseGPUTransferBuffer(device, buffer); }
};
using TransferBufferPtr = std::unique_ptr<SDL_GPUTransferBuffer, TransferBufferDeleter>;

struct GpuMesh {
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUBuffer* indices = nullptr;
    std::uint32_t indexCount = 0;
    Uint32 vertexBytes = 0;
};

// A depth buffer that follows the window's size.
struct SizedTexture {
    SDL_GPUTexture* texture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
};

// The render target and readback buffer for renderToImage(), reused while the size stays the same.
struct OffscreenTarget {
    SDL_GPUTexture* color = nullptr;
    SDL_GPUTexture* depth = nullptr;
    SDL_GPUTransferBuffer* readback = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
};

// Growable GPU vertex buffer plus transfer buffer that per-frame vertices (debug lines, sprites) are streamed
// through.
struct StreamBuffer {
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    Uint32 capacity = 0;  // in bytes
};

// The renderer's state. Device, resource creation and the public API live in Renderer.cpp;
// render targets and frame recording live in RenderFrame.cpp.
struct Renderer::Impl : render::ResourceQuery {
    Window* window = nullptr;
    bool windowClaimed = false;
    bool initedVideo = false;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTextureFormat swapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

    std::unique_ptr<PipelineCache> pipelines;
    SDL_GPUSampler* sampler = nullptr;
    bool anisotropy = false;
    SDL_GPUTexture* whiteTexture = nullptr;

    std::unordered_map<MeshHandle, GpuMesh> meshes;
    std::unordered_map<TextureHandle, SDL_GPUTexture*> textures;
    MeshHandle nextMesh = 1;
    TextureHandle nextTexture = 1;

    SizedTexture windowDepth;
    OffscreenTarget offscreen;
    StreamBuffer lineBuffer;
    StreamBuffer spriteBuffer;
    StreamBuffer effectBuffer;

    render::DrawCollector drawCollector;   // kept between frames to reuse its storage
    std::vector<render::SpriteRun> spriteRuns;
    std::vector<render::EffectRun> effectRuns;
    bool warnedMissingResource = false;
    bool swapchainFailed = false;

    ~Impl();

    void init(Window* targetWindow);
    void createDevice();
    void chooseDepthFormat();
    void createSampler();
    void logConfiguration() const;

    TransferBufferPtr createTransferBuffer(SDL_GPUTransferBufferUsage usage, Uint32 size);
    SDL_GPUTexture* uploadTexture(const Image& image, bool mipmaps);
    MeshHandle uploadMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void rewriteMesh(MeshHandle handle, std::span<const Vertex> vertices);
    SDL_GPUTexture* createDepthTexture(Uint32 width, Uint32 height);
    bool ensureWindowDepth(Uint32 width, Uint32 height);
    bool ensureOffscreen(Uint32 width, Uint32 height);
    void releaseOffscreen();

    void renderWindow(const FrameParams& frame, std::span<const DrawItem> items, std::span<const DebugLine> lines,
                      std::span<const Sprite> sprites, std::span<const EffectBatch> effects);
    Image renderOffscreen(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                          std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                          std::span<const EffectBatch> effects);

    void collectDraws(const FrameParams& frame, std::span<const DrawItem> items);
    void releaseStream(StreamBuffer& buffer);
    void* mapStream(StreamBuffer& buffer, std::size_t bytes, const char* what);
    void uploadStream(SDL_GPUCommandBuffer* cmd, StreamBuffer& buffer, Uint32 bytes);
    Uint32 uploadLines(SDL_GPUCommandBuffer* cmd, std::span<const DebugLine> lines);
    Uint32 uploadSprites(SDL_GPUCommandBuffer* cmd, std::span<const Sprite> sprites);
    Uint32 uploadEffects(SDL_GPUCommandBuffer* cmd, std::span<const EffectBatch> effects);
    void drawFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color, SDL_GPUTextureFormat colorFormat,
                   SDL_GPUTexture* depth, Uint32 width, Uint32 height, const FrameParams& frame,
                   std::span<const DrawItem> items, std::span<const DebugLine> lines,
                   std::span<const Sprite> sprites, std::span<const EffectBatch> effects);
    void drawMeshes(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUTextureFormat colorFormat,
                    const Mat4& viewProj, const FrameParams& frame);
    void drawEffects(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUTextureFormat colorFormat,
                     const Mat4& viewProj);
    void drawSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUTextureFormat colorFormat,
                     Uint32 width, Uint32 height);
    // Resolves a texture handle to its GPU texture, falling back to the white texture (handle 0, or one that
    // does not exist); one lookup per sprite or effect run, not per sprite or triangle.
    SDL_GPUTexture* resolveTexture(TextureHandle handle) const;

    // render::ResourceQuery: what the common draw-collection and vertex-building code asks about resources.
    bool hasMesh(MeshHandle mesh) const override { return meshes.find(mesh) != meshes.end(); }
    bool hasTexture(TextureHandle texture) const override { return textures.find(texture) != textures.end(); }
    void warnMissing(const char* what, std::uint32_t handle) override;
};

}  // namespace mb
