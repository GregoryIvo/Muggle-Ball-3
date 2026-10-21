#pragma once

#include "mb/engine/Image.h"
#include "mb/engine/Math.h"
#include "mb/engine/Vertex.h"

#include <cstdint>
#include <memory>
#include <span>

struct SDL_GPUDevice;

namespace mb {

class Window;

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

// Opaque handles; 0 means "none".
using MeshHandle = std::uint32_t;
using TextureHandle = std::uint32_t;

enum class BlendMode {
    Opaque,      // no blending; fragments with alpha < 0.5 are discarded (Direct3D alpha test)
    AlphaBlend,  // src * a + dst * (1 - a), drawn after opaque geometry, sorted back to front
    Additive,    // src * a + dst, drawn after opaque geometry
};

// Front faces are clockwise, as in Direct3D (its default cull mode culls counter-clockwise).
enum class CullMode { Back, None, Front };

// Direct3D 8 fixed-function material, evaluated per pixel:
//   rgb = texture.rgb * saturate(emissive + ambient * light.ambient
//                                + diffuse * light.diffuse * max(dot(n, -light.direction), 0))
//   a   = texture.a * diffuse.a
// With lit == false: rgb = texture.rgb * diffuse.rgb.
struct MaterialParams {
    Color diffuse{1, 1, 1, 1};
    Color ambient{1, 1, 1, 1};
    Color emissive{0, 0, 0, 0};
    TextureHandle texture = 0;  // 0 binds a 1x1 white texture
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    bool lit = true;
    bool depthWrite = true;
    bool depthTest = true;
};

// One indexed draw of part of a mesh.
struct DrawItem {
    MeshHandle mesh = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;  // 0 draws every index from firstIndex to the end
    Mat4 world;
    MaterialParams material;
    int layer = 1;  // lower layers draw first (0 = sky/background, 1 = world)
};

struct DirectionalLight {
    Vec3 direction{0.0f, -0.66f, 0.75f};  // the direction the light travels, world space
    Color diffuse{1, 1, 1, 1};
    Color ambient{0.5f, 0.5f, 0.5f, 1};
};

struct FrameParams {
    Mat4 view;
    Mat4 projection;
    Vec3 eye;  // camera position in world space, for back-to-front sorting
    DirectionalLight light;
    Color clear{0, 0, 0, 1};
};

struct DebugLine {
    Vec3 a;
    Vec3 b;
    Color color{0, 1, 0, 1};
};

// The SDL3 GPU API underneath picks Metal on macOS and Vulkan or Direct3D 12
// elsewhere, so nothing above this layer is platform-specific. Shaders are
// GLSL compiled ahead of time (tools/build_shaders.py) to SPIR-V and MSL.
class Renderer {
public:
    // Headless: a GPU device with no window, for renderToImage() (screenshots, tests).
    Renderer();
    // Presents to the window.
    explicit Renderer(Window& window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    const char* backendName() const;

    // GPU resources live until the Renderer is destroyed (or destroy*() is called).
    MeshHandle createMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    TextureHandle createTexture(const Image& image, bool mipmaps = true);
    void destroyMesh(MeshHandle mesh);
    void destroyTexture(TextureHandle texture);

    // Size of the window's drawable area in pixels (0 x 0 when headless).
    void drawableSize(int& width, int& height) const;

    // Draws one frame into the window's swapchain and presents it. Opaque items
    // draw first (by layer), then blended items back to front, then debug lines.
    void renderToWindow(const FrameParams& frame, std::span<const DrawItem> items,
                        std::span<const DebugLine> lines = {});

    // Draws one frame into an offscreen target of the given size and reads it back.
    Image renderToImage(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                        std::span<const DebugLine> lines = {});

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mb
