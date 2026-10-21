#pragma once

struct SDL_GPUDevice;

namespace mb {

class Window;

struct Color {
    float r, g, b, a;
};

// Thin wrapper over the SDL3 GPU API. SDL picks Metal on macOS and Vulkan or
// Direct3D 12 on other platforms, so nothing above this layer is
// platform-specific. Shaders will be authored once and cross-compiled per
// backend (MSL / SPIR-V / DXIL) by the asset pipeline.
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    const char* backendName() const;

    // Clears the swapchain image and presents it. Geometry submission comes next.
    void frame(Color clear);

private:
    Window& window_;
    SDL_GPUDevice* device_ = nullptr;
};

}  // namespace mb
