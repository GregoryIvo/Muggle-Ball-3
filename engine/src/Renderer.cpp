// STUB: clears the window only. Replaced by the real SDL GPU renderer.
#include "mb/engine/Renderer.h"

#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace mb {

struct Renderer::Impl {
    Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
};

namespace {

SDL_GPUDevice* createDevice() {
    const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV;
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, /*debug_mode=*/false, nullptr);
    if (!device) {
        throw std::runtime_error(std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError());
    }
    return device;
}

}  // namespace

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {
    impl_->device = createDevice();
}

Renderer::Renderer(Window& window) : impl_(std::make_unique<Impl>()) {
    impl_->window = &window;
    impl_->device = createDevice();
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, window.handle())) {
        throw std::runtime_error(std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError());
    }
}

Renderer::~Renderer() {
    if (impl_->device) {
        if (impl_->window) {
            SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window->handle());
        }
        SDL_DestroyGPUDevice(impl_->device);
    }
}

const char* Renderer::backendName() const {
    return SDL_GetGPUDeviceDriver(impl_->device);
}

MeshHandle Renderer::createMesh(std::span<const Vertex>, std::span<const std::uint32_t>) { return 0; }
TextureHandle Renderer::createTexture(const Image&, bool) { return 0; }
void Renderer::destroyMesh(MeshHandle) {}
void Renderer::destroyTexture(TextureHandle) {}

void Renderer::drawableSize(int& width, int& height) const {
    width = height = 0;
    if (impl_->window) {
        SDL_GetWindowSizeInPixels(impl_->window->handle(), &width, &height);
    }
}

void Renderer::renderToWindow(const FrameParams& frame, std::span<const DrawItem>, std::span<const DebugLine>) {
    if (!impl_->window) {
        return;
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (!cmd) {
        return;
    }
    SDL_GPUTexture* swapchain = nullptr;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, impl_->window->handle(), &swapchain, nullptr, nullptr) &&
        swapchain) {
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.clear_color = SDL_FColor{frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

Image Renderer::renderToImage(int, int, const FrameParams&, std::span<const DrawItem>, std::span<const DebugLine>) {
    return {};
}

}  // namespace mb
