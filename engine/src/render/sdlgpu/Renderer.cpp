#include "mb/engine/Renderer.h"

#include "RendererImpl.h"
#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace mb {

namespace {

constexpr float kMaxAnisotropy = 8.0f;

const char* formatName(SDL_GPUTextureFormat format) {
    switch (format) {
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_D16_UNORM: return "D16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM: return "D24_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT: return "D32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT: return "D32_FLOAT_S8_UINT";
        default: return "other";
    }
}

// MB3_GPU_DEBUG=1 turns on the back end's validation layer.
bool gpuDebugRequested() {
    const char* env = SDL_getenv_unsafe("MB3_GPU_DEBUG");
    return env && *env && *env != '0';
}

#ifdef __APPLE__
// Metal reads MTL_DEBUG_LAYER once, as it creates its first device. In a windowed run that happens inside
// SDL_CreateWindow, well before the Renderer exists, so the variable is set while the program loads and
// MB3_GPU_DEBUG=1 alone enables validation in both modes. A value the user set themselves is kept.
const bool metalValidationSet = [] {
    if (gpuDebugRequested()) {
        SDL_setenv_unsafe("MTL_DEBUG_LAYER", "1", 0);
    }
    return true;
}();
#endif

}  // namespace

Renderer::Impl::~Impl() {
    if (device) {
        SDL_WaitForGPUIdle(device);
        releaseOffscreen();
        if (windowDepth.texture) {
            SDL_ReleaseGPUTexture(device, windowDepth.texture);
        }
        for (StreamBuffer* stream : {&lineBuffer, &spriteBuffer, &effectBuffer}) {
            releaseStream(*stream);
        }
        for (const auto& entry : textures) {
            SDL_ReleaseGPUTexture(device, entry.second);
        }
        for (const auto& entry : meshes) {
            SDL_ReleaseGPUBuffer(device, entry.second.vertices);
            SDL_ReleaseGPUBuffer(device, entry.second.indices);
        }
        if (whiteTexture) {
            SDL_ReleaseGPUTexture(device, whiteTexture);
        }
        if (sampler) {
            SDL_ReleaseGPUSampler(device, sampler);
        }
        pipelines.reset();
        if (windowClaimed) {
            SDL_ReleaseWindowFromGPUDevice(device, window->handle());
        }
        SDL_DestroyGPUDevice(device);
    }
    if (initedVideo) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

void Renderer::Impl::init(Window* targetWindow) {
    window = targetWindow;
    createDevice();
    if (window) {
        if (!SDL_ClaimWindowForGPUDevice(device, window->handle())) {
            throw std::runtime_error(std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError());
        }
        windowClaimed = true;
        // SDL defaults the swapchain to SDR with vsync; the format it picks is plain UNORM, never sRGB.
        swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device, window->handle());
    }
    chooseDepthFormat();
    pipelines = std::make_unique<PipelineCache>(device, depthFormat);
    createSampler();

    const Image white{1, 1, {255, 255, 255, 255}};
    whiteTexture = uploadTexture(white, false);
    if (!whiteTexture) {
        throw std::runtime_error("creating the white texture failed");
    }
    logConfiguration();
}

void Renderer::Impl::logConfiguration() const {
    logInfo("GPU: {} backend, {} shaders, swapchain {}, textures and offscreen {}, depth {}, anisotropy {}",
            SDL_GetGPUDeviceDriver(device),
            (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) ? "SPIR-V" : "MSL",
            window ? formatName(swapchainFormat) : "none", formatName(kColorFormat), formatName(depthFormat),
            anisotropy ? "on" : "off");
}

void Renderer::Impl::createDevice() {
    const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    const bool debug = gpuDebugRequested();
    device = SDL_CreateGPUDevice(formats, debug, nullptr);
    if (!device && !SDL_WasInit(SDL_INIT_VIDEO)) {
        // Metal needs no video subsystem, but Vulkan does even without a window (the headless case).
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            initedVideo = true;
            device = SDL_CreateGPUDevice(formats, debug, nullptr);
        }
    }
    if (!device) {
        throw std::runtime_error(std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError());
    }
}

void Renderer::Impl::chooseDepthFormat() {
    // A stencil format is preferred for effects that may need one later; D32_FLOAT is the fallback.
    for (const SDL_GPUTextureFormat candidate :
         {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
          SDL_GPU_TEXTUREFORMAT_D32_FLOAT}) {
        if (SDL_GPUTextureSupportsFormat(device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depthFormat = candidate;
            return;
        }
    }
    throw std::runtime_error("the GPU supports no depth format");
}

void Renderer::Impl::createSampler() {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.max_lod = 1000.0f;  // the default of 0 would pin every lookup to the largest mip level
    info.enable_anisotropy = true;
    info.max_anisotropy = kMaxAnisotropy;
    sampler = SDL_CreateGPUSampler(device, &info);
    if (!sampler) {
        // Vulkan devices without the anisotropy feature reject the sampler.
        info.enable_anisotropy = false;
        info.max_anisotropy = 1.0f;
        sampler = SDL_CreateGPUSampler(device, &info);
    }
    if (!sampler) {
        throw std::runtime_error(std::string("SDL_CreateGPUSampler failed: ") + SDL_GetError());
    }
    anisotropy = info.enable_anisotropy;
}

TransferBufferPtr Renderer::Impl::createTransferBuffer(SDL_GPUTransferBufferUsage usage, Uint32 size) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return TransferBufferPtr(SDL_CreateGPUTransferBuffer(device, &info), TransferBufferDeleter{device});
}

SDL_GPUTexture* Renderer::Impl::uploadTexture(const Image& image, bool mipmaps) {
    if (image.empty() || image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        logError("createTexture: invalid {}x{} image with {} bytes", image.width, image.height, image.rgba.size());
        return nullptr;
    }
    const Uint32 width = static_cast<Uint32>(image.width);
    const Uint32 height = static_cast<Uint32>(image.height);
    const Uint32 levels = mipmaps ? std::bit_width(std::max(width, height)) : 1;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kColorFormat;
    // Generating mipmaps renders into the lower levels, which needs the colour target usage.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = levels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (!texture) {
        logError("createTexture: SDL_CreateGPUTexture failed for {}x{}: {}", width, height, SDL_GetError());
        return nullptr;
    }

    const auto bytes = static_cast<Uint32>(image.rgba.size());
    TransferBufferPtr transfer = createTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
    void* mapped = transfer ? SDL_MapGPUTransferBuffer(device, transfer.get(), false) : nullptr;
    SDL_GPUCommandBuffer* cmd = mapped ? SDL_AcquireGPUCommandBuffer(device) : nullptr;
    if (!cmd) {
        logError("createTexture: upload setup failed: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    std::memcpy(mapped, image.rgba.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer.get());

    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer.get();
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    if (levels > 1) {
        SDL_GenerateMipmapsForGPUTexture(cmd, texture);  // must run outside any pass
    }
    SDL_SubmitGPUCommandBuffer(cmd);  // the transfer buffer is released once the GPU is done with it
    return texture;
}

MeshHandle Renderer::Impl::uploadMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    if (vertices.empty() || indices.empty()) {
        logWarn("createMesh: mesh has {} vertices and {} indices", vertices.size(), indices.size());
        return 0;
    }
    const std::size_t vertexBytes = vertices.size_bytes();
    const std::size_t indexBytes = indices.size_bytes();
    if (vertexBytes + indexBytes > std::numeric_limits<Uint32>::max()) {
        logError("createMesh: mesh of {} bytes is too large", vertexBytes + indexBytes);
        return 0;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size()) {
            logError("createMesh: index {} is out of range for {} vertices", index, vertices.size());
            return 0;
        }
    }

    GpuMesh mesh;
    mesh.indexCount = static_cast<std::uint32_t>(indices.size());
    mesh.vertexBytes = static_cast<Uint32>(vertexBytes);
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = static_cast<Uint32>(vertexBytes);
    mesh.vertices = SDL_CreateGPUBuffer(device, &bufferInfo);
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bufferInfo.size = static_cast<Uint32>(indexBytes);
    mesh.indices = SDL_CreateGPUBuffer(device, &bufferInfo);

    TransferBufferPtr transfer =
        createTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(vertexBytes + indexBytes));
    auto* mapped = static_cast<Uint8*>(transfer ? SDL_MapGPUTransferBuffer(device, transfer.get(), false) : nullptr);
    SDL_GPUCommandBuffer* cmd =
        (mesh.vertices && mesh.indices && mapped) ? SDL_AcquireGPUCommandBuffer(device) : nullptr;
    if (!cmd) {
        logError("createMesh: upload setup failed: {}", SDL_GetError());
        if (mesh.vertices) {
            SDL_ReleaseGPUBuffer(device, mesh.vertices);
        }
        if (mesh.indices) {
            SDL_ReleaseGPUBuffer(device, mesh.indices);
        }
        return 0;
    }
    std::memcpy(mapped, vertices.data(), vertexBytes);
    std::memcpy(mapped + vertexBytes, indices.data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(device, transfer.get());

    const SDL_GPUTransferBufferLocation vertexSource{transfer.get(), 0};
    const SDL_GPUBufferRegion vertexDestination{mesh.vertices, 0, static_cast<Uint32>(vertexBytes)};
    const SDL_GPUTransferBufferLocation indexSource{transfer.get(), static_cast<Uint32>(vertexBytes)};
    const SDL_GPUBufferRegion indexDestination{mesh.indices, 0, static_cast<Uint32>(indexBytes)};
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy, &vertexSource, &vertexDestination, false);
    SDL_UploadToGPUBuffer(copy, &indexSource, &indexDestination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    const MeshHandle handle = nextMesh++;
    meshes.emplace(handle, mesh);
    return handle;
}

void Renderer::Impl::rewriteMesh(MeshHandle handle, std::span<const Vertex> vertices) {
    const auto found = meshes.find(handle);
    if (found == meshes.end() || vertices.size_bytes() != found->second.vertexBytes) {
        logError("updateMesh: {} is not a mesh of {} vertices", handle, found == meshes.end() ? 0 : found->second.vertexBytes / sizeof(Vertex));
        return;
    }
    const Uint32 bytes = found->second.vertexBytes;
    TransferBufferPtr transfer = createTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
    void* mapped = transfer ? SDL_MapGPUTransferBuffer(device, transfer.get(), false) : nullptr;
    SDL_GPUCommandBuffer* cmd = mapped ? SDL_AcquireGPUCommandBuffer(device) : nullptr;
    if (!cmd) {
        logError("updateMesh: upload setup failed: {}", SDL_GetError());
        return;
    }
    std::memcpy(mapped, vertices.data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer.get());
    const SDL_GPUTransferBufferLocation source{transfer.get(), 0};
    const SDL_GPUBufferRegion destination{found->second.vertices, 0, bytes};
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);  // cycled: a frame in flight still reads the old one
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
}

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {
    impl_->init(nullptr);
}

Renderer::Renderer(Window& window) : impl_(std::make_unique<Impl>()) {
    impl_->init(&window);
}

Renderer::~Renderer() = default;

const char* Renderer::backendName() const {
    return SDL_GetGPUDeviceDriver(impl_->device);
}

MeshHandle Renderer::createMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    return impl_->uploadMesh(vertices, indices);
}

TextureHandle Renderer::createTexture(const Image& image, bool mipmaps) {
    SDL_GPUTexture* texture = impl_->uploadTexture(image, mipmaps);
    if (!texture) {
        return 0;
    }
    const TextureHandle handle = impl_->nextTexture++;
    impl_->textures.emplace(handle, texture);
    return handle;
}

void Renderer::updateMesh(MeshHandle mesh, std::span<const Vertex> vertices) {
    impl_->rewriteMesh(mesh, vertices);
}

void Renderer::destroyMesh(MeshHandle mesh) {
    const auto found = impl_->meshes.find(mesh);
    if (found != impl_->meshes.end()) {
        SDL_ReleaseGPUBuffer(impl_->device, found->second.vertices);  // freed once in-flight frames finish
        SDL_ReleaseGPUBuffer(impl_->device, found->second.indices);
        impl_->meshes.erase(found);
    }
}

void Renderer::destroyTexture(TextureHandle texture) {
    const auto found = impl_->textures.find(texture);
    if (found != impl_->textures.end()) {
        SDL_ReleaseGPUTexture(impl_->device, found->second);
        impl_->textures.erase(found);
    }
}

void Renderer::drawableSize(int& width, int& height) const {
    width = height = 0;
    if (impl_->window) {
        SDL_GetWindowSizeInPixels(impl_->window->handle(), &width, &height);
    }
}

void Renderer::renderToWindow(const FrameParams& frame, std::span<const DrawItem> items,
                              std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                              std::span<const EffectBatch> effects) {
    if (!impl_->window) {
        logError("renderToWindow called on a headless Renderer");
        return;
    }
    impl_->renderWindow(frame, items, lines, sprites, effects);
}

Image Renderer::renderToImage(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                              std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                              std::span<const EffectBatch> effects) {
    return impl_->renderOffscreen(width, height, frame, items, lines, sprites, effects);
}

}  // namespace mb
