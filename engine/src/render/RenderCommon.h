#pragma once

// The part of the renderer that every back end shares: the vertex layouts of the streamed primitives, the
// render state a draw needs, the collection and sorting of a frame's draw items, and the building of the
// line, sprite and effect vertices. Nothing here names a graphics API. The back ends (engine/src/render/<name>/)
// own the resources and the API calls, and turn RenderState into their pipelines.
//
// The frame order every back end reproduces (see docs/VITA_PLAN.md section 4.3):
//   1. clear (colour to FrameParams::clear, depth to 1.0);
//   2. opaque meshes, stable-sorted by (layer, state, texture, mesh);
//   3. blended meshes, by layer, then far to near;
//   4. effect batches, in the order given;
//   5. debug lines;
//   6. sprites, in submission order, with no depth test; consecutive sprites of one texture are one draw.

#include "mb/engine/Renderer.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mb::render {

// Vertex layout of the debug lines (the meshes use mb::Vertex).
struct LineVertex {
    Vec3 position;
    Color color;
};
static_assert(sizeof(LineVertex) == 28);

// Vertex layout of the sprites: a pixel position, a texture coordinate and a tint.
struct SpriteVertex {
    Vec2 position;
    Vec2 uv;
    Color color;
};
static_assert(sizeof(SpriteVertex) == 32);

// Which shader program a draw uses.
enum class Program : std::uint8_t { Mesh, Line, Sprite, Effect };

// Everything that selects the fixed-function state of a draw. Per-draw data (matrices, colours, the
// texture) is not part of it, so a handful of states cover a whole level. A back end adds what its API
// needs on top (the SDL GPU back end adds the colour target's format) and maps the D3D blend factors
// to its own.
struct RenderState {
    Program program = Program::Mesh;
    bool blend = false;
    BlendFactor srcBlend = BlendFactor::One;  // only meaningful when blend is set
    BlendFactor dstBlend = BlendFactor::Zero;
    CullMode cull = CullMode::Back;
    bool depthWrite = true;  // depth is written only when depthTest and depthWrite are both set
    bool depthTest = true;

    // Resolves BlendMode::AlphaBlend (SrcAlpha / InvSrcAlpha), Additive (SrcAlpha / One) and Custom
    // (the material's own factors) so that materials that blend the same way share one state.
    static RenderState forMaterial(const MaterialParams& material);
    // Lines: no blending, no culling, depth tested but not written.
    static RenderState forLines();
    // Sprites: alpha blending, no culling, no depth test or write.
    static RenderState forSprites();
    // Effects: SrcAlpha / One for Additive, SrcAlpha / InvSrcAlpha for anything else; no culling, no depth write.
    static RenderState forEffects(BlendMode blend, bool depthTest);

    // Unique per distinct state, so draws with equal states sort together. Fits 16 bits: the blend factors
    // are D3DBLEND numbers 1 to 10 (four bits each) and the rest are flags and small enums.
    std::uint32_t pack() const;
    friend bool operator==(const RenderState& a, const RenderState& b) { return a.pack() == b.pack(); }
};

// A draw item after its state is known, ready to draw. The back end looks its mesh and texture up by handle
// when it draws (`item->mesh`, `item->material.texture`; texture 0 is the 1x1 white texture).
struct PendingDraw {
    const DrawItem* item = nullptr;
    RenderState state;
    float distanceSq = 0;  // squared distance from the eye, for the blended items' back-to-front order
};

// Consecutive sprites that share a texture, drawn with one call. `texture` is the handle the sprites named
// (0 for the white texture); `firstVertex` and `vertexCount` index the SpriteVertex stream.
struct SpriteRun {
    TextureHandle texture = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
};

// One effect batch's place in the EffectVertex stream, with how it is drawn.
struct EffectRun {
    TextureHandle texture = 0;
    BlendMode blend = BlendMode::Additive;
    bool depthTest = false;
    bool colourOnly = false;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
};

// What the common code asks a back end about its resources while it collects a frame. `warnMissing` is
// called for a handle that names no live resource, with `what` "mesh" or "texture"; the back end decides
// how often to log (today: once per Renderer, then silently).
class ResourceQuery {
public:
    virtual ~ResourceQuery() = default;
    virtual bool hasMesh(MeshHandle mesh) const = 0;
    virtual bool hasTexture(TextureHandle texture) const = 0;
    virtual void warnMissing(const char* what, std::uint32_t handle) = 0;
};

// Sorts a frame's draw items into the opaque and the blended lists. An item whose mesh does not exist is
// left out after warnMissing; an item whose texture does not exist is kept after warnMissing and drawn
// with the white texture (as for handle 0); unlike sprites and effect batches below, which drop on a
// missing texture. The lists keep their storage between frames; call collect() once per frame and read
// the results until the next call.
class DrawCollector {
public:
    void collect(const FrameParams& frame, std::span<const DrawItem> items, ResourceQuery& resources);

    // Opaque: by layer, then grouped by state, texture and mesh so state changes are rare. The sort is stable,
    // so submission order decides among equals and frames are reproducible.
    std::span<const PendingDraw> opaque() const { return opaque_; }
    // Blended: by layer, then far to near, which is what alpha blending needs to look right.
    std::span<const PendingDraw> blended() const { return blended_; }

private:
    std::vector<PendingDraw> opaque_;
    std::vector<PendingDraw> blended_;
};

// Two LineVertex per line, in order. `out` has room for 2 * lines.size() vertices. Returns the vertex count.
std::uint32_t buildLineVertices(std::span<const DebugLine> lines, LineVertex* out);

// The most SpriteVertex buildSpriteVertices() can write for these sprites: six per sprite.
inline std::size_t spriteVertexCapacity(std::span<const Sprite> sprites) { return sprites.size() * 6; }

// Writes the sprites as triangles (six vertices each: two triangles of the quad, corners top-left,
// top-right, bottom-right, bottom-left, rotated clockwise about the centre when `rotation` is set) and
// groups consecutive sprites of one texture into `runs`. A sprite of zero or negative size, or one whose
// texture does not exist (after warnMissing), writes nothing. Returns the vertex count written.
std::uint32_t buildSpriteVertices(std::span<const Sprite> sprites, SpriteVertex* out, std::vector<SpriteRun>& runs,
                                  ResourceQuery& resources);

// The most EffectVertex buildEffectVertices() can write: every batch's whole triangles.
std::size_t effectVertexCapacity(std::span<const EffectBatch> effects);

// Copies every batch's whole triangles (a trailing partial triangle is dropped) into one stream, in order,
// and notes each batch's place and blend in `runs`. A batch with no whole triangle, or whose texture does
// not exist (after warnMissing), writes nothing. Returns the vertex count written.
std::uint32_t buildEffectVertices(std::span<const EffectBatch> effects, EffectVertex* out, std::vector<EffectRun>& runs,
                                  ResourceQuery& resources);

// The clipped index range of a draw: `first` clamped to the mesh's index count, and `count` either the
// item's indexCount (0 = to the end) or what is left after `first`, whichever is smaller.
struct IndexRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};
IndexRange indexRange(const DrawItem& item, std::uint32_t meshIndexCount);

// The normal matrix of a world matrix: its inverse-transpose, in the row-vector convention the shaders
// read (see engine/shaders/mesh.vert).
Mat4 normalMatrix(const Mat4& world);

// The number of mip levels a texture of this size gets when mipmaps are asked for: a full chain down to 1x1.
std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height);

}  // namespace mb::render
