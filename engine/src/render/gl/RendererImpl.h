#pragma once

#include "../RenderCommon.h"
#include "GlFunctions.h"
#include "GlPlatform.h"
#include "ProgramCache.h"
#include "mb/engine/Renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace mb {

class Window;

// A GPU mesh: one VBO (mb::Vertex, static) and one IBO, 16-bit when the mesh fits (the plan's §4.3 and M4a:
// every shipped mesh does, the largest has 5171 vertices) else 32-bit.
struct GlMesh {
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLenum indexType = GL_UNSIGNED_SHORT;
    std::uint32_t vertexCount = 0;  // for updateMesh's "as many as it was made with" check
    std::uint32_t indexCount = 0;
};

struct GlTexture {
    GLuint id = 0;
    bool opaqueAlpha = true;  // every alpha byte is 255: the mesh draw may then pick the discard-free program
};

// A growable stream of per-frame vertices (debug lines, sprites, effects): built into `cpu` every frame
// (RenderCommon's buildLineVertices / buildSpriteVertices / buildEffectVertices write there directly), then
// uploaded with glBufferData (orphaning: a fresh allocation keeps the GPU from stalling on a buffer the
// previous frame may still be reading). `vbo`'s capacity only grows, doubling like the SDL GPU back end's
// StreamBuffer, with the same 32 KiB floor.
template <class Vertex>
struct GlStream {
    std::vector<Vertex> cpu;
    GLuint vbo = 0;
    std::size_t capacity = 0;  // bytes actually allocated for `vbo`
};

// The render target and readback buffer for renderToImage(), reused while the size stays the same.
struct OffscreenTarget {
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    int width = 0;
    int height = 0;
};

// Which vertex layout is currently set up on the bound GL_ARRAY_BUFFER: without VAOs, glVertexAttribPointer
// must be re-issued whenever the bound buffer changes, even to the same layout, since the offsets it records
// are resolved against whichever buffer is bound at the time of the call.
enum class VertexLayout { None, Mesh, Line, Sprite, Effect };

// The renderer's state. Device/context bring-up is Platform's (GlPlatformSdl.cpp, GlPlatformVita.cpp);
// resource creation lives in Renderer.cpp, frame recording in RenderFrame.cpp.
struct Renderer::Impl : render::ResourceQuery {
    Window* window = nullptr;

    std::unique_ptr<render::gl::ProgramCache> programs;
    GLuint whiteTexture = 0;

    std::unordered_map<MeshHandle, GlMesh> meshes;
    std::unordered_map<TextureHandle, GlTexture> textures;
    MeshHandle nextMesh = 1;
    TextureHandle nextTexture = 1;

    OffscreenTarget offscreen;
    GlStream<render::LineVertex> lineStream;
    GlStream<render::SpriteVertex> spriteStream;
    GlStream<EffectVertex> effectStream;

    render::DrawCollector drawCollector;  // kept between frames to reuse its storage
    std::vector<render::SpriteRun> spriteRuns;
    std::vector<render::EffectRun> effectRuns;
    bool warnedMissingResource = false;

    // GL state tracking, so a draw only calls glUseProgram/glBindBuffer/glBindTexture/the state setters when
    // something actually changed (see THE WORK #3, "Minimise GL state changes").
    GLuint boundProgram = 0;
    GLuint boundArrayBuffer = 0;
    GLuint boundElementBuffer = 0;
    GLuint boundTexture = 0;
    VertexLayout currentLayout = VertexLayout::None;
    std::uint32_t currentStateKey = 0xffffffffu;  // RenderState::pack() never reaches this: 17 bits used
    std::uint64_t frameId = 0;
    // The frame this program's per-frame uniforms (viewProj, the light, or sprite's pixelToClip) were last
    // set: compared against frameId so a program bound more than once in a frame is not re-uploaded.
    std::unordered_map<GLuint, std::uint64_t> perFrameUniformsSetAt;

    ~Impl();
    void init(Window* targetWindow);
    void logConfiguration() const;

    // The GL object only (id 0 on failure, after logging); used both by uploadTexture (the public
    // createTexture's handle gets assigned there) and by init() for the white texture, which never gets a
    // handle of its own (handle 0 means "the white texture" throughout the public API).
    GlTexture createGlTexture(const Image& image, bool mipmaps);
    TextureHandle uploadTexture(const Image& image, bool mipmaps);
    MeshHandle uploadMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void rewriteMesh(MeshHandle handle, std::span<const Vertex> vertices);
    bool ensureOffscreen(int width, int height);
    void releaseOffscreen();

    void renderWindow(const FrameParams& frame, std::span<const DrawItem> items, std::span<const DebugLine> lines,
                      std::span<const Sprite> sprites, std::span<const EffectBatch> effects);
    Image renderOffscreen(int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                          std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                          std::span<const EffectBatch> effects);

    void collectDraws(const FrameParams& frame, std::span<const DrawItem> items);

    // Uploads the first `vertexCount` vertices of `stream.cpu` to `stream.vbo` (created on first use),
    // growing by doubling (min 32 KiB) and orphaning (a same-size glBufferData before the glBufferSubData)
    // so the GPU can keep reading the previous frame's data from the old store instead of stalling this
    // upload on a buffer it may still be drawing from. A no-op for zero vertices.
    template <class Vertex>
    void uploadStream(GlStream<Vertex>& stream, std::size_t vertexCount) {
        if (vertexCount == 0) {
            return;
        }
        if (!stream.vbo) {
            glGenBuffers(1, &stream.vbo);
        }
        glBindBuffer(GL_ARRAY_BUFFER, stream.vbo);
        const std::size_t bytes = vertexCount * sizeof(Vertex);
        if (bytes > stream.capacity) {
            stream.capacity = std::max<std::size_t>({bytes, stream.capacity * 2, std::size_t{32768}});
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(stream.capacity), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), stream.cpu.data());
        boundArrayBuffer = stream.vbo;  // already bound; bindVertexBuffer() compares against this
    }

    void drawFrame(GLuint targetFbo, int width, int height, const FrameParams& frame, std::span<const DrawItem> items,
                  std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                  std::span<const EffectBatch> effects);
    void drawMeshes(const Mat4& viewProj, const FrameParams& frame);
    void drawEffects(const Mat4& viewProj);
    void drawLines(const Mat4& viewProj, std::uint32_t vertexCount);
    void drawSprites(int width, int height, std::uint32_t vertexCount);

    // GL state helpers, each a no-op when the desired value already matches what was last applied.
    void useProgram(const render::gl::GlProgram& program);
    void bindVertexBuffer(GLuint vbo, VertexLayout layout);
    void bindTexture(GLuint id);
    void applyRenderState(const render::RenderState& state);
    // True once per frame per program: callers upload the program's per-frame uniforms only when this
    // returns true (viewProj and the light for mesh/line/effect, pixelToClip for sprite).
    bool beginFrameUniforms(GLuint program);

    GlTexture resolveTexture(TextureHandle handle) const;

    // render::ResourceQuery: what the common draw-collection and vertex-building code asks about resources.
    bool hasMesh(MeshHandle mesh) const override { return meshes.find(mesh) != meshes.end(); }
    bool hasTexture(TextureHandle texture) const override { return textures.find(texture) != textures.end(); }
    void warnMissing(const char* what, std::uint32_t handle) override;
};

}  // namespace mb
