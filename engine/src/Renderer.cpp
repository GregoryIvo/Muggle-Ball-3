#include "mb/engine/Renderer.h"

#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace mb {

Renderer::Renderer(Window& window) : window_(window) {
    const SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB |
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
    device_ = SDL_CreateGPUDevice(formats, /*debug_mode=*/false, nullptr);
    if (!device_) {
        throw std::runtime_error(std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError());
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window_.handle())) {
        throw std::runtime_error(std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError());
    }
}

Renderer::~Renderer() {
    if (device_) {
        SDL_ReleaseWindowFromGPUDevice(device_, window_.handle());
        SDL_DestroyGPUDevice(device_);
    }
}

const char* Renderer::backendName() const {
    return SDL_GetGPUDeviceDriver(device_);
}

void Renderer::frame(Color clear) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) {
        return;
    }
    SDL_GPUTexture* swapchain = nullptr;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_.handle(), &swapchain, nullptr, nullptr) &&
        swapchain) {
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.clear_color = SDL_FColor{clear.r, clear.g, clear.b, clear.a};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

}  // namespace mb
