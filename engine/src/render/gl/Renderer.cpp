#include "mb/engine/Renderer.h"

#include "RendererImpl.h"
#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace mb {

namespace {

bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }

int nextPowerOfTwo(int n) {
    int p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

// One bilinearly-sampled channel of `image` at a fractional pixel position, clamped to the image's edges.
std::uint8_t sampleChannel(const Image& image, int x0, int x1, int y0, int y1, float fx, float fy, int channel) {
    const auto at = [&](int x, int y) {
        return static_cast<float>(image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4 + channel]);
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return static_cast<std::uint8_t>(std::lround(top + (bottom - top) * fy));
}

// A bilinear resample to the next power-of-two size in each dimension: the fallback for a mipmapped texture
// on a target that cannot build a correct mip chain for an NPOT size (the Vita; see
// render::gl::Platform::npotMipmaps and docs/VITA.md's note on vitaGL's mip generator). The game's UVs are
// normalised, so nothing else about a draw changes; tools/vita_pack.py does the same offline.
Image resampleToPowerOfTwo(const Image& image) {
    const int dstW = nextPowerOfTwo(image.width);
    const int dstH = nextPowerOfTwo(image.height);
    Image out;
    out.width = dstW;
    out.height = dstH;
    out.rgba.resize(static_cast<std::size_t>(dstW) * dstH * 4);
    for (int y = 0; y < dstH; ++y) {
        const float srcY = (static_cast<float>(y) + 0.5f) * image.height / dstH - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, image.height - 1);
        const int y1 = std::clamp(y0 + 1, 0, image.height - 1);
        const float fy = std::clamp(srcY - static_cast<float>(y0), 0.0f, 1.0f);
        for (int x = 0; x < dstW; ++x) {
            const float srcX = (static_cast<float>(x) + 0.5f) * image.width / dstW - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, image.width - 1);
            const int x1 = std::clamp(x0 + 1, 0, image.width - 1);
            const float fx = std::clamp(srcX - static_cast<float>(x0), 0.0f, 1.0f);
            std::uint8_t* dst = &out.rgba[(static_cast<std::size_t>(y) * dstW + x) * 4];
            for (int c = 0; c < 4; ++c) {
                dst[c] = sampleChannel(image, x0, x1, y0, y1, fx, fy, c);
            }
        }
    }
    return out;
}

}  // namespace

Renderer::Impl::~Impl() {
    programs.reset();
    if (whiteTexture) {
        glDeleteTextures(1, &whiteTexture);
    }
    for (const auto& entry : textures) {
        glDeleteTextures(1, &entry.second.id);
    }
    for (const auto& entry : meshes) {
        glDeleteBuffers(1, &entry.second.vbo);
        glDeleteBuffers(1, &entry.second.ibo);
    }
    for (GLuint* vbo : {&lineStream.vbo, &spriteStream.vbo, &effectStream.vbo}) {
        if (*vbo) {
            glDeleteBuffers(1, vbo);
        }
    }
    releaseOffscreen();
    // The GL context itself is process-global (GlPlatform.h: "there is one GL context per process on both
    // targets") and outlives any one Renderer, so it is never torn down here.
}

void Renderer::Impl::init(Window* targetWindow) {
    window = targetWindow;
    if (!render::gl::Platform::init(window)) {
        throw std::runtime_error(std::string("initialising the GL back end (") + render::gl::Platform::name() +
                                 ") failed");
    }
    glFrontFace(GL_CW);       // front faces are clockwise, as in Direct3D
    glDepthFunc(GL_LEQUAL);   // Direct3D's default depth compare
    // Attributes 0, 1 and 2 are enabled for the life of the context: with no VAOs, bindVertexBuffer() only
    // ever changes what glVertexAttribPointer points them at, never whether they are enabled, and a program
    // that reads fewer than three (line.vert) simply does not use the others.
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    programs = std::make_unique<render::gl::ProgramCache>();

    const Image white{1, 1, {255, 255, 255, 255}};
    const GlTexture whiteTex = createGlTexture(white, false);
    if (whiteTex.id == 0) {
        throw std::runtime_error("creating the white texture failed");
    }
    whiteTexture = whiteTex.id;

    logConfiguration();
}

void Renderer::Impl::logConfiguration() const {
    logInfo("GL: {} backend, NPOT mipmaps {}, max texture size {}", render::gl::Platform::name(),
            render::gl::Platform::npotMipmaps() ? "on" : "off", render::gl::Platform::maxTextureSize());
}

GlTexture Renderer::Impl::createGlTexture(const Image& image, bool mipmaps) {
    if (image.empty() || image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        logError("createTexture: invalid {}x{} image with {} bytes", image.width, image.height, image.rgba.size());
        return {0, true};
    }

    bool opaqueAlpha = true;
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 255) {
            opaqueAlpha = false;
            break;
        }
    }

    const Image* source = &image;
    Image resampled;
    const bool needsPowerOfTwo = mipmaps && !render::gl::Platform::npotMipmaps() &&
                                 (!isPowerOfTwo(image.width) || !isPowerOfTwo(image.height));
    if (needsPowerOfTwo) {
        resampled = resampleToPowerOfTwo(image);
        source = &resampled;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source->width, source->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                source->rgba.data());
    // One sampler for every texture: linear mag, REPEAT on both axes, no anisotropy (see §4.3).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mipmaps) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        render::gl::generateMipmap();
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    boundTexture = 0;  // the draw loop's cache must not assume this texture is already bound

    return {id, opaqueAlpha};
}

TextureHandle Renderer::Impl::uploadTexture(const Image& image, bool mipmaps) {
    const GlTexture texture = createGlTexture(image, mipmaps);
    if (texture.id == 0) {
        return 0;
    }
    const TextureHandle handle = nextTexture++;
    textures.emplace(handle, texture);
    return handle;
}

MeshHandle Renderer::Impl::uploadMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    if (vertices.empty() || indices.empty()) {
        logWarn("createMesh: mesh has {} vertices and {} indices", vertices.size(), indices.size());
        return 0;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size()) {
            logError("createMesh: index {} is out of range for {} vertices", index, vertices.size());
            return 0;
        }
    }

    GlMesh mesh;
    mesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
    mesh.indexCount = static_cast<std::uint32_t>(indices.size());
    // 16-bit indices when the mesh fits (every shipped mesh does: the largest has 5171 vertices), else 32-bit.
    mesh.indexType = vertices.size() <= 65535 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    if (mesh.indexType == GL_UNSIGNED_SHORT) {
        std::vector<std::uint16_t> narrow(indices.size());
        for (std::size_t i = 0; i < indices.size(); ++i) {
            narrow[i] = static_cast<std::uint16_t>(indices[i]);
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(narrow.size() * sizeof(std::uint16_t)),
                    narrow.data(), GL_STATIC_DRAW);
    } else {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(),
                    GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    boundArrayBuffer = 0;
    boundElementBuffer = 0;

    const MeshHandle handle = nextMesh++;
    meshes.emplace(handle, mesh);
    return handle;
}

void Renderer::Impl::rewriteMesh(MeshHandle handle, std::span<const Vertex> vertices) {
    const auto found = meshes.find(handle);
    if (found == meshes.end() || vertices.size() != found->second.vertexCount) {
        logError("updateMesh: {} is not a mesh of {} vertices", handle,
                found == meshes.end() ? 0u : found->second.vertexCount);
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, found->second.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    boundArrayBuffer = 0;
}

void Renderer::Impl::warnMissing(const char* what, std::uint32_t handle) {
    if (!warnedMissingResource) {
        warnedMissingResource = true;
        logWarn("draw item uses {} handle {}, which was never created or has been destroyed (further cases not logged)",
                what, handle);
    }
}

GlTexture Renderer::Impl::resolveTexture(TextureHandle handle) const {
    if (handle != 0) {
        if (const auto found = textures.find(handle); found != textures.end()) {
            return found->second;
        }
    }
    return {whiteTexture, true};
}

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {
    impl_->init(nullptr);
}

Renderer::Renderer(Window& window) : impl_(std::make_unique<Impl>()) {
    impl_->init(&window);
}

Renderer::~Renderer() = default;

const char* Renderer::backendName() const { return render::gl::Platform::name(); }

MeshHandle Renderer::createMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    return impl_->uploadMesh(vertices, indices);
}

TextureHandle Renderer::createTexture(const Image& image, bool mipmaps) {
    return impl_->uploadTexture(image, mipmaps);
}

void Renderer::updateMesh(MeshHandle mesh, std::span<const Vertex> vertices) {
    impl_->rewriteMesh(mesh, vertices);
}

void Renderer::destroyMesh(MeshHandle mesh) {
    const auto found = impl_->meshes.find(mesh);
    if (found != impl_->meshes.end()) {
        glDeleteBuffers(1, &found->second.vbo);
        glDeleteBuffers(1, &found->second.ibo);
        impl_->meshes.erase(found);
        impl_->boundArrayBuffer = 0;
        impl_->boundElementBuffer = 0;
    }
}

void Renderer::destroyTexture(TextureHandle texture) {
    const auto found = impl_->textures.find(texture);
    if (found != impl_->textures.end()) {
        glDeleteTextures(1, &found->second.id);
        impl_->textures.erase(found);
        impl_->boundTexture = 0;
    }
}

void Renderer::drawableSize(int& width, int& height) const {
    if (impl_->window) {
        render::gl::Platform::drawableSize(width, height);
    } else {
        width = height = 0;
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
