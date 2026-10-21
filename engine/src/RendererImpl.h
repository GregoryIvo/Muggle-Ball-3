#pragma once

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

// Growable GPU vertex buffer plus transfer buffer that debug lines are streamed through each frame.
struct LineBuffer {
    SDL_GPUBuffer* vertices = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    Uint32 capacity = 0;  // in vertices
};

// A draw item after its handles are resolved, ready to sort.
struct PendingDraw {
    const DrawItem* item = nullptr;
    const GpuMesh* mesh = nullptr;
    SDL_GPUTexture* texture = nullptr;
    PipelineKey pipeline;
    float distanceSq = 0;  // from the eye, for back-to-front sorting
};

// The renderer's state. Device, resource creation and the public API live in Renderer.cpp;
// render targets and frame recording live in RenderFrame.cpp.
struct Renderer::Impl {
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
    LineBuffer lineBuffer;

    std::vector<PendingDraw> opaqueDraws;   // kept between frames to reuse their storage
    std::vector<PendingDraw> blendedDraws;
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
    SDL_GPUTexture* createDepthTexture(Uint32 width, Uint32 height);
    bool ensureWindowDepth(Uint32 width, Uint32 height);
    bool ensureOffscreen(Uint32 width, Uint32 height);
    void releaseOffscreen();

    void renderWindow(const FrameParams& frame, std::span<const DrawItem> items, std::span<const DebugLine> lines);
    Image renderOffscreen(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                          std::span<const DebugLine> lines);

    void collectDraws(SDL_GPUTextureFormat colorFormat, const FrameParams& frame, std::span<const DrawItem> items);
    Uint32 uploadLines(SDL_GPUCommandBuffer* cmd, std::span<const DebugLine> lines);
    void drawFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color, SDL_GPUTextureFormat colorFormat,
                   SDL_GPUTexture* depth, Uint32 width, Uint32 height, const FrameParams& frame,
                   std::span<const DrawItem> items, std::span<const DebugLine> lines);
    void drawMeshes(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& viewProj,
                    const FrameParams& frame);
    void warnMissing(const char* what, std::uint32_t handle);
};

}  // namespace mb
