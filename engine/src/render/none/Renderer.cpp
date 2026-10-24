// The MB3_RENDERER=NONE back end: for a build with no GPU API (SDL GPU has no Vita back end). Every
// Renderer is unusable (both constructors throw before any resource exists), so the methods below exist
// only to satisfy the class's interface; the one warning they share covers the case where calling code
// somehow runs anyway.
#include "mb/engine/Renderer.h"

#include "mb/engine/Log.h"

#include <stdexcept>

namespace mb {

struct Renderer::Impl {};

namespace {

bool warnedOnce = false;

void warnNoRenderer(const char* what) {
    if (!warnedOnce) {
        warnedOnce = true;
        logError("{}: built with MB3_RENDERER=NONE, which has no renderer", what);
    }
}

}  // namespace

Renderer::Renderer() {
    throw std::runtime_error("no renderer: built with MB3_RENDERER=NONE");
}

Renderer::Renderer(Window&) {
    throw std::runtime_error("no renderer: built with MB3_RENDERER=NONE");
}

Renderer::~Renderer() = default;

const char* Renderer::backendName() const { return "none"; }

MeshHandle Renderer::createMesh(std::span<const Vertex>, std::span<const std::uint32_t>) {
    warnNoRenderer("createMesh");
    return 0;
}

TextureHandle Renderer::createTexture(const Image&, bool) {
    warnNoRenderer("createTexture");
    return 0;
}

void Renderer::updateMesh(MeshHandle, std::span<const Vertex>) { warnNoRenderer("updateMesh"); }

void Renderer::destroyMesh(MeshHandle) { warnNoRenderer("destroyMesh"); }

void Renderer::destroyTexture(TextureHandle) { warnNoRenderer("destroyTexture"); }

void Renderer::drawableSize(int& width, int& height) const { width = height = 0; }

void Renderer::renderToWindow(const FrameParams&, std::span<const DrawItem>, std::span<const DebugLine>,
                              std::span<const Sprite>, std::span<const EffectBatch>) {
    warnNoRenderer("renderToWindow");
}

Image Renderer::renderToImage(int, int, const FrameParams&, std::span<const DrawItem>, std::span<const DebugLine>,
                              std::span<const Sprite>, std::span<const EffectBatch>) {
    warnNoRenderer("renderToImage");
    return {};
}

}  // namespace mb
