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

// A blend factor, numbered like Direct3D's D3DBLEND (1 = ZERO ... 10 = INVDESTCOLOR) so game data converts 1:1.
// The destination alpha is the frame's clear alpha: the alpha channel is never written.
enum class BlendFactor {
    Zero = 1,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DestAlpha,
    InvDestAlpha,
    DestColor,
    InvDestColor,
};

// Everything but Opaque draws after the opaque geometry, sorted back to front.
enum class BlendMode {
    Opaque,      // no blending; fragments with alpha < 0.5 are discarded (Direct3D alpha test)
    AlphaBlend,  // src * a + dst * (1 - a)
    Additive,    // src * a + dst
    Custom,      // src * srcBlend + dst * dstBlend, with MaterialParams::srcBlend and dstBlend
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
    BlendFactor srcBlend = BlendFactor::SrcAlpha;  // used by BlendMode::Custom only
    BlendFactor dstBlend = BlendFactor::InvSrcAlpha;
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

// A screen-space textured quad for 2D overlays (menus, text, the HUD).
//
// Coordinates are pixels of the target the frame is drawn into: the window's drawable area
// (Renderer::drawableSize) or the image of renderToImage. The origin is the top-left corner,
// +x runs right and +y runs down. Sprites are drawn after the 3D scene and the debug lines, in
// the order they are submitted (later ones on top), alpha-blended (straight alpha, as the
// meshes) and without any depth test, so they work with or without a scene.
struct Sprite {
    float x = 0;       // top-left corner of the unrotated quad
    float y = 0;
    float width = 0;
    float height = 0;
    TextureHandle texture = 0;  // 0 draws the tint as a solid colour
    // Texture coordinates of the quad's top-left and bottom-right corners; (0, 0) is the texture's
    // top-left pixel, as in Direct3D. Swapping two values mirrors the image; values outside
    // 0..1 repeat it (the sampler wraps), which is how tiled and scrolling images are drawn.
    Vec2 uv0{0, 0};
    Vec2 uv1{1, 1};
    Color tint{1, 1, 1, 1};  // multiplied into every texel, alpha included
    float rotation = 0;      // radians, clockwise on screen, about the quad's centre
};

// One corner of an effect triangle: a world position, a texture coordinate and a colour, straight alpha as the meshes.
struct EffectVertex {
    Vec3 position;
    Vec2 uv;
    Color color;
};
static_assert(sizeof(EffectVertex) == 36);

// Triangles of one texture and one way of blending, drawn with a single call: the particles of the game's effects (sparks,
// flashes, smoke, trails, beams), which a mesh draw each would be too many calls for. The vertices come three to a
// triangle, already in world space, so a batch holds camera-facing quads, strips and marks lying on the floor alike. They
// are drawn after the meshes and before the debug lines and the sprites, in the order the batches are given, with no
// culling and no depth write.
struct EffectBatch {
    std::span<const EffectVertex> vertices;
    TextureHandle texture = 0;  // 0 binds a 1x1 white texture
    BlendMode blend = BlendMode::Additive;  // AlphaBlend or Additive; Opaque and Custom draw as AlphaBlend
    bool depthTest = false;  // tested against what the meshes wrote (marks on the floor are hidden by what stands on them)
    // The vertex colour replaces the texture's colour and the texture only shapes the sprite (rgb = vertex rgb,
    // a = texture a * vertex a), where the default multiplies them (as the meshes' unlit materials do).
    bool colourOnly = false;
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
    // draw first (by layer), then blended items back to front, then the effect batches in the order
    // given, then debug lines, then the sprites in submission order. With no items the frame is just the
    // clear colour and the sprites, which is how the menus draw.
    void renderToWindow(const FrameParams& frame, std::span<const DrawItem> items,
                        std::span<const DebugLine> lines = {}, std::span<const Sprite> sprites = {},
                        std::span<const EffectBatch> effects = {});

    // Draws one frame into an offscreen target of the given size and reads it back.
    Image renderToImage(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                        std::span<const DebugLine> lines = {}, std::span<const Sprite> sprites = {},
                        std::span<const EffectBatch> effects = {});

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mb
