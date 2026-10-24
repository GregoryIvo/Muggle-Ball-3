#include "RendererImpl.h"

#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#if defined(MB3_RENDERER_VITAGL)
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mb {

namespace {

// The D3D-to-GL clip remap (docs/VITA_PLAN.md §4.3): D3D clips z in 0..1, GL in -w..w, so z' = 2z - w.
// Folded into viewProj once per frame, on the CPU, the one place either back end does it (the hello does
// the same thing, see platform/vita/hello/main.cpp's d3dToGlClip).
Mat4 clipRemap() {
    Mat4 d;
    d.m[2][2] = 2;
    d.m[3][2] = -1;
    return d;
}

GLenum toGlBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColor: return GL_SRC_COLOR;
        case BlendFactor::InvSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DestAlpha: return GL_DST_ALPHA;
        case BlendFactor::InvDestAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::DestColor: return GL_DST_COLOR;
        case BlendFactor::InvDestColor: return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

}  // namespace

void Renderer::Impl::useProgram(const render::gl::GlProgram& program) {
    if (boundProgram != program.program) {
        glUseProgram(program.program);
        boundProgram = program.program;
    }
}

bool Renderer::Impl::beginFrameUniforms(GLuint program) {
    std::uint64_t& stamp = perFrameUniformsSetAt[program];
    if (stamp == frameId) {
        return false;
    }
    stamp = frameId;
    return true;
}

void Renderer::Impl::bindVertexBuffer(GLuint vbo, VertexLayout layout) {
    if (boundArrayBuffer == vbo && currentLayout == layout) {
        return;  // the pointers already set for this buffer are still valid (see uploadStream's comment)
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // Attributes 0 and 1 are enabled for the life of the context (every layout uses them); attribute 2 is
    // not (Line is 2 wide, everything else is 3), so it must be toggled per layout instead of staying enabled
    // with whatever pointer the previous layout left it at — a stale pointer into a buffer that may since have
    // been deleted (GL_INVALID_OPERATION on a validating driver) or resized shorter than this draw.
    const bool needsAttrib2 = layout != VertexLayout::Line;
    if (needsAttrib2 != attrib2Enabled) {
        if (needsAttrib2) {
            glEnableVertexAttribArray(2);
        } else {
            glDisableVertexAttribArray(2);
        }
        attrib2Enabled = needsAttrib2;
    }
    switch (layout) {
        case VertexLayout::Mesh:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, position)));
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, normal)));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, uv)));
            break;
        case VertexLayout::Line:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(render::LineVertex),
                                  reinterpret_cast<const void*>(offsetof(render::LineVertex, position)));
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(render::LineVertex),
                                  reinterpret_cast<const void*>(offsetof(render::LineVertex, color)));
            break;
        case VertexLayout::Sprite:
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(render::SpriteVertex),
                                  reinterpret_cast<const void*>(offsetof(render::SpriteVertex, position)));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(render::SpriteVertex),
                                  reinterpret_cast<const void*>(offsetof(render::SpriteVertex, uv)));
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(render::SpriteVertex),
                                  reinterpret_cast<const void*>(offsetof(render::SpriteVertex, color)));
            break;
        case VertexLayout::Effect:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(EffectVertex, position)));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(EffectVertex, uv)));
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(EffectVertex),
                                  reinterpret_cast<const void*>(offsetof(EffectVertex, color)));
            break;
        case VertexLayout::None:
            break;
    }
    boundArrayBuffer = vbo;
    currentLayout = layout;
}

void Renderer::Impl::bindTexture(GLuint id) {
    if (boundTexture != id) {
        glBindTexture(GL_TEXTURE_2D, id);
        boundTexture = id;
    }
}

void Renderer::Impl::applyRenderState(const render::RenderState& state) {
    const std::uint32_t key = state.pack();
    if (key == currentStateKey) {
        return;
    }
    currentStateKey = key;

    switch (state.cull) {
        case CullMode::Back: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
        case CullMode::Front: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
        case CullMode::None: glDisable(GL_CULL_FACE); break;
    }
    if (state.depthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(state.depthTest && state.depthWrite ? GL_TRUE : GL_FALSE);
    if (state.blend) {
        glEnable(GL_BLEND);
        glBlendFunc(toGlBlendFactor(state.srcBlend), toGlBlendFactor(state.dstBlend));
    } else {
        glDisable(GL_BLEND);
    }
}

void Renderer::Impl::collectDraws(const FrameParams& frame, std::span<const DrawItem> items) {
    drawCollector.collect(frame, items, *this);
}

void Renderer::Impl::releaseOffscreen() {
    if (offscreen.fbo) {
        render::gl::deleteFramebuffers(1, &offscreen.fbo);
    }
    if (offscreen.colorTexture) {
        glDeleteTextures(1, &offscreen.colorTexture);
    }
    if (offscreen.depthRenderbuffer) {
        render::gl::deleteRenderbuffers(1, &offscreen.depthRenderbuffer);
    }
    offscreen = {};
}

bool Renderer::Impl::ensureOffscreen(int width, int height) {
    if (offscreen.fbo && offscreen.width == width && offscreen.height == height) {
        return true;
    }
    releaseOffscreen();

    glGenTextures(1, &offscreen.colorTexture);
    glBindTexture(GL_TEXTURE_2D, offscreen.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    boundTexture = 0;

    // GL_DEPTH_COMPONENT24: vitaGL's glRenderbufferStorage accepts it (it only uses the call to learn
    // whether a stencil was asked for; the actual depth buffer it allocates, at the colour attachment's
    // size, is always its own float format - see vitaGL's source/gxm.c, read for this milestone).
    render::gl::genRenderbuffers(1, &offscreen.depthRenderbuffer);
    render::gl::bindRenderbuffer(offscreen.depthRenderbuffer);
    render::gl::renderbufferStorage(GL_DEPTH_COMPONENT24, width, height);

    render::gl::genFramebuffers(1, &offscreen.fbo);
    render::gl::bindFramebuffer(offscreen.fbo);
    render::gl::framebufferTexture2D(GL_COLOR_ATTACHMENT0, offscreen.colorTexture);
    render::gl::framebufferRenderbuffer(GL_DEPTH_ATTACHMENT, offscreen.depthRenderbuffer);

    const GLenum status = render::gl::checkFramebufferStatus();
    render::gl::bindFramebuffer(0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        logError("renderToImage: creating a {}x{} target failed: framebuffer status 0x{:x}", width, height,
                static_cast<unsigned>(status));
        releaseOffscreen();
        return false;
    }
    offscreen.width = width;
    offscreen.height = height;
    return true;
}

void Renderer::Impl::drawMeshes(const Mat4& viewProj, const FrameParams& frame) {
    const Vec3 lightDirection = normalize(frame.light.direction);

    const auto drawAll = [&](std::span<const render::PendingDraw> draws) {
        for (const render::PendingDraw& draw : draws) {
            const DrawItem& item = *draw.item;
            // The mesh was validated when the draw was collected, so this always finds it.
            const auto meshIt = meshes.find(item.mesh);
            if (meshIt == meshes.end()) {
                continue;
            }
            const GlMesh& mesh = meshIt->second;
            const render::IndexRange range = render::indexRange(item, mesh.indexCount);
            if (range.count == 0) {
                continue;
            }

            // Resolved by handle, not carried on the draw: 0 (and a destroyed handle) is the white texture.
            const GlTexture texture = resolveTexture(item.material.texture);
            // The discard-free variant is picked when the pixels would be identical anyway: no texel below
            // full alpha, and the material itself draws fully opaque.
            const bool discardFree = texture.opaqueAlpha && item.material.diffuse.a >= 1.0f &&
                                     item.material.blend == BlendMode::Opaque;
            const render::gl::GlProgram& program = programs->mesh(discardFree);

            applyRenderState(draw.state);
            useProgram(program);
            if (beginFrameUniforms(program.program)) {
                glUniformMatrix4fv(program.viewProj, 1, GL_FALSE, &viewProj.m[0][0]);
                glUniform4f(program.lightDirection, lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
                glUniform4f(program.lightDiffuse, frame.light.diffuse.r, frame.light.diffuse.g,
                           frame.light.diffuse.b, frame.light.diffuse.a);
                glUniform4f(program.lightAmbient, frame.light.ambient.r, frame.light.ambient.g,
                           frame.light.ambient.b, frame.light.ambient.a);
            }

            bindVertexBuffer(mesh.vbo, VertexLayout::Mesh);
            if (boundElementBuffer != mesh.ibo) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
                boundElementBuffer = mesh.ibo;
            }
            bindTexture(texture.id);

            const Mat4 normalMatrix = render::normalMatrix(item.world);
            glUniformMatrix4fv(program.world, 1, GL_FALSE, &item.world.m[0][0]);
            glUniformMatrix4fv(program.normalMatrix, 1, GL_FALSE, &normalMatrix.m[0][0]);

            const MaterialParams& material = item.material;
            glUniform4f(program.diffuse, material.diffuse.r, material.diffuse.g, material.diffuse.b,
                       material.diffuse.a);
            glUniform4f(program.ambient, material.ambient.r, material.ambient.g, material.ambient.b,
                       material.ambient.a);
            glUniform4f(program.emissive, material.emissive.r, material.emissive.g, material.emissive.b,
                       material.emissive.a);
            glUniform4f(program.flags, material.lit ? 1.0f : 0.0f,
                       material.blend == BlendMode::Opaque ? 1.0f : 0.0f, 0.0f, 0.0f);

            const std::size_t indexSize = mesh.indexType == GL_UNSIGNED_SHORT ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
            const auto offset = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(range.first) * indexSize);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(range.count), mesh.indexType, offset);
        }
    };
    drawAll(drawCollector.opaque());
    drawAll(drawCollector.blended());
}

void Renderer::Impl::drawEffects(const Mat4& viewProj) {
    const render::gl::GlProgram& program = programs->effect();
    for (const render::EffectRun& run : effectRuns) {
        if (run.vertexCount == 0) {
            continue;
        }
        applyRenderState(render::RenderState::forEffects(run.blend, run.depthTest));
        useProgram(program);
        if (beginFrameUniforms(program.program)) {
            glUniformMatrix4fv(program.viewProj, 1, GL_FALSE, &viewProj.m[0][0]);
        }
        bindVertexBuffer(effectStream.vbo, VertexLayout::Effect);
        bindTexture(resolveTexture(run.texture).id);
        glUniform4f(program.flags, run.colourOnly ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.firstVertex), static_cast<GLsizei>(run.vertexCount));
    }
}

void Renderer::Impl::drawLines(const Mat4& viewProj, std::uint32_t vertexCount) {
    if (vertexCount == 0) {
        return;
    }
    const render::gl::GlProgram& program = programs->line();
    applyRenderState(render::RenderState::forLines());
    useProgram(program);
    if (beginFrameUniforms(program.program)) {
        glUniformMatrix4fv(program.viewProj, 1, GL_FALSE, &viewProj.m[0][0]);
    }
    bindVertexBuffer(lineStream.vbo, VertexLayout::Line);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
}

void Renderer::Impl::drawSprites(int width, int height, std::uint32_t vertexCount) {
    if (vertexCount == 0) {
        return;
    }
    const render::gl::GlProgram& program = programs->sprite();
    applyRenderState(render::RenderState::forSprites());
    useProgram(program);
    if (beginFrameUniforms(program.program)) {
        const float pixelToClip[4] = {2.0f / static_cast<float>(width), 2.0f / static_cast<float>(height), 0.0f,
                                      0.0f};
        glUniform4fv(program.pixelToClip, 1, pixelToClip);
    }
    bindVertexBuffer(spriteStream.vbo, VertexLayout::Sprite);
    for (const render::SpriteRun& run : spriteRuns) {
        bindTexture(resolveTexture(run.texture).id);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.firstVertex), static_cast<GLsizei>(run.vertexCount));
    }
}

void Renderer::Impl::drawFrame(GLuint targetFbo, int width, int height, const FrameParams& frame,
                               std::span<const DrawItem> items, std::span<const DebugLine> lines,
                               std::span<const Sprite> sprites, std::span<const EffectBatch> effects) {
    ++frameId;

    // Reset every per-draw GL bind cache to "nothing bound" before touching the context. Two reasons this
    // must happen every frame, not just once at construction:
    //  1. the clear below forces glDepthMask(GL_TRUE) (and a full glColorMask) outside applyRenderState, so
    //     if this frame's first draw happens to pack to the same RenderState key as the *previous* frame's
    //     last draw, applyRenderState's "skip if unchanged" early return would leave that override in force
    //     and the draw would write depth (or write alpha) when its own state says not to;
    //  2. GlPlatform.h: "there is one GL context per process" — if more than one Renderer is alive (or was,
    //     and freed its GL objects) in this process, another Renderer's draws may have rebound the program,
    //     a buffer or a texture, or changed GL state these caches don't know about.
    // The cost is a handful of redundant rebinds on the first draw of each kind this frame.
    boundProgram = 0;
    boundArrayBuffer = 0;
    boundElementBuffer = 0;
    boundTexture = 0;
    currentLayout = VertexLayout::None;
    currentStateKey = kInvalidStateKey;
    // Re-synced directly, not just invalidated: unlike the object-id caches above (where 0 is never a real
    // name, so the next bind always re-issues the real call), a stale *boolean* here could read as already
    // correct by coincidence while the real GL enable state is something else.
    // Unconditionally: another Renderer on the same context may have disabled the array (its line draws do), and
    // this one's flag would still read true. One redundant enable per frame, like the other caches' reset.
    glEnableVertexAttribArray(2);
    attrib2Enabled = true;

    collectDraws(frame, items);

    lineStream.cpu.resize(lines.size() * 2);
    const std::uint32_t lineVertexCount = render::buildLineVertices(lines, lineStream.cpu.data());

    spriteStream.cpu.resize(render::spriteVertexCapacity(sprites));
    const std::uint32_t spriteVertexCount =
        render::buildSpriteVertices(sprites, spriteStream.cpu.data(), spriteRuns, *this);

    effectStream.cpu.resize(render::effectVertexCapacity(effects));
    const std::uint32_t effectVertexCount =
        render::buildEffectVertices(effects, effectStream.cpu.data(), effectRuns, *this);

    uploadStream(lineStream, lineVertexCount);
    uploadStream(spriteStream, spriteVertexCount);
    uploadStream(effectStream, effectVertexCount);

    render::gl::bindFramebuffer(targetFbo);
    glViewport(0, 0, width, height);
    // glClear respects both masks: a previous frame's last draw may have left either one off (a blended or
    // effect item's depthWrite is often false), which would make the matching half of this clear a no-op.
    // Both are forced on for the clear; the state-cache reset above (currentStateKey = kInvalidStateKey)
    // guarantees applyRenderState cannot mistake this override for the real state and skip re-applying the
    // real masks on the first draw below.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // The alpha channel is never written by a draw (see applyRenderState's callers): it keeps the clear
    // value, so the DestAlpha blend factors are well defined and screenshots stay opaque.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

    const Mat4 viewProj = frame.view * frame.projection * clipRemap();

    drawMeshes(viewProj, frame);
    if (effectVertexCount > 0) {
        drawEffects(viewProj);
    }
    drawLines(viewProj, lineVertexCount);
    if (spriteVertexCount > 0) {
        drawSprites(width, height, spriteVertexCount);
    }
    drainGlErrors("drawFrame");
}

void Renderer::Impl::renderWindow(const FrameParams& frame, std::span<const DrawItem> items,
                                  std::span<const DebugLine> lines, std::span<const Sprite> sprites,
                                  std::span<const EffectBatch> effects) {
    int width = 0, height = 0;
    render::gl::Platform::drawableSize(width, height);
    if (width <= 0 || height <= 0) {
        return;  // minimised or hidden: nothing to draw, as the SDL GPU back end also skips
    }
    drawFrame(0, width, height, frame, items, lines, sprites, effects);

    bool commonDialogShown = false;
#if defined(MB3_RENDERER_VITAGL)
    // SDL never calls sceCommonDialogUpdate itself, so the IME dialog needs this to be true while it is up.
    commonDialogShown = SDL_ScreenKeyboardShown(window->handle());
#endif
    render::gl::Platform::present(commonDialogShown);
}

Image Renderer::Impl::renderOffscreen(int width, int height, const FrameParams& frame,
                                      std::span<const DrawItem> items, std::span<const DebugLine> lines,
                                      std::span<const Sprite> sprites, std::span<const EffectBatch> effects) {
    if (width <= 0 || height <= 0) {
        logError("renderToImage: invalid size {}x{}", width, height);
        return {};
    }
    if (!ensureOffscreen(width, height)) {
        drainGlErrors("renderToImage");
        return {};
    }
    drawFrame(offscreen.fbo, width, height, frame, items, lines, sprites, effects);

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    render::gl::bindFramebuffer(0);

    // GL reads rows bottom-up; vitaGL's FBOs are flipped internally to match GL, so the same flip is right
    // on both targets (§4.3). Image rows are top to bottom.
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
    for (int y = 0; y < height / 2; ++y) {
        std::uint8_t* top = &image.rgba[static_cast<std::size_t>(y) * width * 4];
        std::uint8_t* bottom = &image.rgba[static_cast<std::size_t>(height - 1 - y) * width * 4];
        std::copy(top, top + row.size(), row.begin());
        std::copy(bottom, bottom + row.size(), top);
        std::copy(row.begin(), row.end(), bottom);
    }
    return image;
}

}  // namespace mb
