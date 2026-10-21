#include "RendererImpl.h"

#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace mb {

namespace {

// These mirror the uniform blocks in engine/shaders/mesh.vert and mesh.frag (std140).
struct VertexUniforms {
    Mat4 viewProj;
    Mat4 world;
    Mat4 normalMatrix;
};
static_assert(sizeof(VertexUniforms) == 192);

struct FragmentUniforms {
    Color diffuse;
    Color ambient;
    Color emissive;
    Vec4 lightDirection;
    Color lightDiffuse;
    Color lightAmbient;
    Vec4 flags;  // x: lit, y: alpha test
};
static_assert(sizeof(FragmentUniforms) == 112);

}  // namespace

SDL_GPUTexture* Renderer::Impl::createDepthTexture(Uint32 width, Uint32 height) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = depthFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (!texture) {
        logError("creating a {}x{} depth buffer failed: {}", width, height, SDL_GetError());
    }
    return texture;
}

bool Renderer::Impl::ensureWindowDepth(Uint32 width, Uint32 height) {
    if (windowDepth.texture && windowDepth.width == width && windowDepth.height == height) {
        return true;
    }
    if (windowDepth.texture) {
        SDL_ReleaseGPUTexture(device, windowDepth.texture);
        windowDepth = {};
    }
    windowDepth.texture = createDepthTexture(width, height);
    windowDepth.width = width;
    windowDepth.height = height;
    return windowDepth.texture != nullptr;
}

void Renderer::Impl::releaseOffscreen() {
    if (offscreen.color) {
        SDL_ReleaseGPUTexture(device, offscreen.color);
    }
    if (offscreen.depth) {
        SDL_ReleaseGPUTexture(device, offscreen.depth);
    }
    if (offscreen.readback) {
        SDL_ReleaseGPUTransferBuffer(device, offscreen.readback);
    }
    offscreen = {};
}

bool Renderer::Impl::ensureOffscreen(Uint32 width, Uint32 height) {
    if (offscreen.color && offscreen.width == width && offscreen.height == height) {
        return true;
    }
    releaseOffscreen();

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kColorFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    offscreen.color = SDL_CreateGPUTexture(device, &info);
    if (!offscreen.color) {
        logError("renderToImage: creating a {}x{} target failed: {}", width, height, SDL_GetError());
        return false;
    }
    offscreen.depth = createDepthTexture(width, height);
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    if (!offscreen.depth || bytes > std::numeric_limits<Uint32>::max()) {
        releaseOffscreen();
        return false;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = static_cast<Uint32>(bytes);
    offscreen.readback = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!offscreen.readback) {
        logError("renderToImage: creating the readback buffer for {}x{} failed: {}", width, height, SDL_GetError());
        releaseOffscreen();
        return false;
    }
    offscreen.width = width;
    offscreen.height = height;
    return true;
}

void Renderer::Impl::warnMissing(const char* what, std::uint32_t handle) {
    if (!warnedMissingResource) {
        warnedMissingResource = true;
        logWarn("draw item uses {} handle {}, which was never created or has been destroyed (further cases not logged)",
                what, handle);
    }
}

void Renderer::Impl::collectDraws(SDL_GPUTextureFormat colorFormat, const FrameParams& frame,
                                  std::span<const DrawItem> items) {
    opaqueDraws.clear();
    blendedDraws.clear();
    for (const DrawItem& item : items) {
        const auto mesh = meshes.find(item.mesh);
        if (mesh == meshes.end()) {
            warnMissing("mesh", item.mesh);
            continue;
        }
        const MaterialParams& material = item.material;
        SDL_GPUTexture* texture = whiteTexture;
        if (material.texture != 0) {
            if (const auto found = textures.find(material.texture); found != textures.end()) {
                texture = found->second;
            } else {
                warnMissing("texture", material.texture);
            }
        }

        PendingDraw draw;
        draw.item = &item;
        draw.mesh = &mesh->second;
        draw.texture = texture;
        draw.pipeline = PipelineKey::forMaterial(colorFormat, material);
        if (material.blend == BlendMode::Opaque) {
            opaqueDraws.push_back(draw);
        } else {
            const Vec3 toItem = item.world.translationPart() - frame.eye;
            draw.distanceSq = dot(toItem, toItem);
            blendedDraws.push_back(draw);
        }
    }

    // Opaque: by layer, then grouped by pipeline, texture and mesh so state changes are rare.
    // stable_sort keeps submission order among equals, so frames are reproducible.
    std::stable_sort(opaqueDraws.begin(), opaqueDraws.end(), [](const PendingDraw& a, const PendingDraw& b) {
        return std::make_tuple(a.item->layer, a.pipeline.pack(), a.item->material.texture, a.item->mesh) <
               std::make_tuple(b.item->layer, b.pipeline.pack(), b.item->material.texture, b.item->mesh);
    });
    // Blended: by layer, then far to near, which is what alpha blending needs to look right.
    std::stable_sort(blendedDraws.begin(), blendedDraws.end(), [](const PendingDraw& a, const PendingDraw& b) {
        if (a.item->layer != b.item->layer) {
            return a.item->layer < b.item->layer;
        }
        return a.distanceSq > b.distanceSq;
    });
}

void Renderer::Impl::releaseStream(StreamBuffer& buffer) {
    if (buffer.vertices) {
        SDL_ReleaseGPUBuffer(device, buffer.vertices);
    }
    if (buffer.transfer) {
        SDL_ReleaseGPUTransferBuffer(device, buffer.transfer);
    }
    buffer = {};
}

// Grows the buffer if needed and maps its transfer buffer for writing `bytes` bytes. Null (after logging) if
// that fails or `bytes` is zero or does not fit.
void* Renderer::Impl::mapStream(StreamBuffer& buffer, std::size_t bytes, const char* what) {
    constexpr std::size_t kMaxBytes = std::numeric_limits<Uint32>::max();
    if (bytes == 0 || bytes > kMaxBytes) {
        return nullptr;
    }
    if (bytes > buffer.capacity) {
        // Doubling keeps a fluctuating count from reallocating every frame.
        const auto capacity =
            static_cast<Uint32>(std::min(kMaxBytes, std::max({bytes, std::size_t{buffer.capacity} * 2, std::size_t{32768}})));
        releaseStream(buffer);
        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = capacity;
        buffer.vertices = SDL_CreateGPUBuffer(device, &bufferInfo);
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = capacity;
        buffer.transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (!buffer.vertices || !buffer.transfer) {
            logError("creating the {} buffer failed: {}", what, SDL_GetError());
            releaseStream(buffer);
            return nullptr;
        }
        buffer.capacity = capacity;
    }
    // cycle = true lets the GPU keep reading last frame's vertices while this frame's are written.
    void* mapped = SDL_MapGPUTransferBuffer(device, buffer.transfer, true);
    if (!mapped) {
        logError("mapping the {} buffer failed: {}", what, SDL_GetError());
    }
    return mapped;
}

// Ends the write started by mapStream and queues the copy of its first `bytes` bytes to the GPU buffer.
void Renderer::Impl::uploadStream(SDL_GPUCommandBuffer* cmd, StreamBuffer& buffer, Uint32 bytes) {
    SDL_UnmapGPUTransferBuffer(device, buffer.transfer);
    if (bytes == 0) {
        return;
    }
    const SDL_GPUTransferBufferLocation source{buffer.transfer, 0};
    const SDL_GPUBufferRegion destination{buffer.vertices, 0, bytes};
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
}

Uint32 Renderer::Impl::uploadLines(SDL_GPUCommandBuffer* cmd, std::span<const DebugLine> lines) {
    auto* out = static_cast<LineVertex*>(mapStream(lineBuffer, lines.size() * 2 * sizeof(LineVertex), "debug line"));
    if (!out) {
        return 0;
    }
    for (const DebugLine& line : lines) {
        *out++ = {line.a, line.color};
        *out++ = {line.b, line.color};
    }
    const auto vertexCount = static_cast<Uint32>(lines.size() * 2);
    uploadStream(cmd, lineBuffer, vertexCount * static_cast<Uint32>(sizeof(LineVertex)));
    return vertexCount;
}

// Writes the sprites as triangles (six vertices each) and groups them into runs of one texture. Returns the
// vertex count; the runs are left in spriteRuns.
Uint32 Renderer::Impl::uploadSprites(SDL_GPUCommandBuffer* cmd, std::span<const Sprite> sprites) {
    spriteRuns.clear();
    constexpr std::size_t kVerticesPerSprite = 6;
    SpriteVertex* const first =
        static_cast<SpriteVertex*>(mapStream(spriteBuffer, sprites.size() * kVerticesPerSprite * sizeof(SpriteVertex), "sprite"));
    if (!first) {
        return 0;
    }
    SpriteVertex* out = first;
    for (const Sprite& sprite : sprites) {
        if (sprite.width <= 0 || sprite.height <= 0) {
            continue;
        }
        SDL_GPUTexture* texture = whiteTexture;
        if (sprite.texture != 0) {
            const auto found = textures.find(sprite.texture);
            if (found == textures.end()) {
                warnMissing("texture", sprite.texture);
                continue;
            }
            texture = found->second;
        }

        // Corners in the order top-left, top-right, bottom-right, bottom-left. Without rotation they are the
        // rectangle's own edges, so axis-aligned sprites land on exact pixel boundaries.
        Vec2 corner[4] = {{sprite.x, sprite.y},
                          {sprite.x + sprite.width, sprite.y},
                          {sprite.x + sprite.width, sprite.y + sprite.height},
                          {sprite.x, sprite.y + sprite.height}};
        if (sprite.rotation != 0) {
            const float cx = sprite.x + sprite.width * 0.5f, cy = sprite.y + sprite.height * 0.5f;
            const float cosine = std::cos(sprite.rotation), sine = std::sin(sprite.rotation);
            for (Vec2& c : corner) {
                const float dx = c.x - cx, dy = c.y - cy;
                c = {cx + dx * cosine - dy * sine, cy + dx * sine + dy * cosine};  // clockwise on a y-down screen
            }
        }
        const SpriteVertex tl{corner[0], {sprite.uv0.x, sprite.uv0.y}, sprite.tint};
        const SpriteVertex tr{corner[1], {sprite.uv1.x, sprite.uv0.y}, sprite.tint};
        const SpriteVertex br{corner[2], {sprite.uv1.x, sprite.uv1.y}, sprite.tint};
        const SpriteVertex bl{corner[3], {sprite.uv0.x, sprite.uv1.y}, sprite.tint};
        const auto vertex = static_cast<Uint32>(out - first);
        if (spriteRuns.empty() || spriteRuns.back().texture != texture) {
            spriteRuns.push_back({texture, vertex, 0});
        }
        spriteRuns.back().vertexCount += kVerticesPerSprite;
        for (const SpriteVertex& v : {tl, tr, br, tl, br, bl}) {
            *out++ = v;
        }
    }
    const auto vertexCount = static_cast<Uint32>(out - first);
    uploadStream(cmd, spriteBuffer, vertexCount * static_cast<Uint32>(sizeof(SpriteVertex)));
    return vertexCount;
}

void Renderer::Impl::drawMeshes(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& viewProj,
                                const FrameParams& frame) {
    VertexUniforms vertexUniforms;
    vertexUniforms.viewProj = viewProj;

    FragmentUniforms fragmentUniforms;
    const Vec3 lightDirection = normalize(frame.light.direction);
    fragmentUniforms.lightDirection = {lightDirection.x, lightDirection.y, lightDirection.z, 0};
    fragmentUniforms.lightDiffuse = frame.light.diffuse;
    fragmentUniforms.lightAmbient = frame.light.ambient;

    SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
    SDL_GPUBuffer* boundVertices = nullptr;
    SDL_GPUTexture* boundTexture = nullptr;

    const auto drawAll = [&](const std::vector<PendingDraw>& draws) {
        for (const PendingDraw& draw : draws) {
            const DrawItem& item = *draw.item;
            const GpuMesh& mesh = *draw.mesh;
            const std::uint32_t first = std::min(item.firstIndex, mesh.indexCount);
            const std::uint32_t available = mesh.indexCount - first;
            const std::uint32_t count = item.indexCount == 0 ? available : std::min(item.indexCount, available);
            SDL_GPUGraphicsPipeline* pipeline = count ? pipelines->get(draw.pipeline) : nullptr;
            if (!pipeline) {
                continue;
            }

            if (pipeline != boundPipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
                boundVertices = nullptr;  // rebind everything after a pipeline change
                boundTexture = nullptr;
            }
            if (mesh.vertices != boundVertices) {
                const SDL_GPUBufferBinding vertexBinding{mesh.vertices, 0};
                const SDL_GPUBufferBinding indexBinding{mesh.indices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                boundVertices = mesh.vertices;
            }
            if (draw.texture != boundTexture) {
                const SDL_GPUTextureSamplerBinding textureBinding{draw.texture, sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
                boundTexture = draw.texture;
            }

            // The normal matrix is the inverse-transpose of the world matrix (row-vector convention).
            vertexUniforms.world = item.world;
            vertexUniforms.normalMatrix = transpose(inverse(item.world));
            SDL_PushGPUVertexUniformData(cmd, 0, &vertexUniforms, sizeof(vertexUniforms));

            const MaterialParams& material = item.material;
            fragmentUniforms.diffuse = material.diffuse;
            fragmentUniforms.ambient = material.ambient;
            fragmentUniforms.emissive = material.emissive;
            fragmentUniforms.flags = {material.lit ? 1.0f : 0.0f, material.blend == BlendMode::Opaque ? 1.0f : 0.0f,
                                      0, 0};
            SDL_PushGPUFragmentUniformData(cmd, 0, &fragmentUniforms, sizeof(fragmentUniforms));

            SDL_DrawGPUIndexedPrimitives(pass, count, 1, first, 0, 0);
        }
    };
    drawAll(opaqueDraws);
    drawAll(blendedDraws);
}

void Renderer::Impl::drawSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                 SDL_GPUTextureFormat colorFormat, Uint32 width, Uint32 height) {
    SDL_GPUGraphicsPipeline* pipeline = pipelines->get(PipelineKey::forSprites(colorFormat));
    if (!pipeline) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    const float pixelToClip[4] = {2.0f / static_cast<float>(width), 2.0f / static_cast<float>(height), 0, 0};
    SDL_PushGPUVertexUniformData(cmd, 0, pixelToClip, sizeof(pixelToClip));
    const SDL_GPUBufferBinding binding{spriteBuffer.vertices, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_GPUTexture* bound = nullptr;
    for (const SpriteRun& run : spriteRuns) {
        if (run.texture != bound) {
            const SDL_GPUTextureSamplerBinding textureBinding{run.texture, sampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
            bound = run.texture;
        }
        SDL_DrawGPUPrimitives(pass, run.vertexCount, 1, run.firstVertex, 0);
    }
}

void Renderer::Impl::drawFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color, SDL_GPUTextureFormat colorFormat,
                               SDL_GPUTexture* depth, Uint32 width, Uint32 height, const FrameParams& frame,
                               std::span<const DrawItem> items, std::span<const DebugLine> lines,
                               std::span<const Sprite> sprites) {
    collectDraws(colorFormat, frame, items);
    const Uint32 lineVertexCount = uploadLines(cmd, lines);  // copy passes cannot run inside the render pass
    const Uint32 spriteVertexCount = uploadSprites(cmd, sprites);

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = color;
    colorTarget.clear_color = SDL_FColor{frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = depth;
    depthTarget.clear_depth = 1.0f;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;  // nothing uses the stencil yet
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle = true;  // lets a new frame start while the GPU still reads the last one's depth

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget);
    if (!pass) {
        logError("SDL_BeginGPURenderPass failed: {}", SDL_GetError());
        return;
    }
    const SDL_GPUViewport viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &viewport);
    const SDL_Rect scissor{0, 0, static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUScissor(pass, &scissor);

    const Mat4 viewProj = frame.view * frame.projection;
    drawMeshes(pass, cmd, viewProj, frame);

    if (lineVertexCount > 0) {
        if (SDL_GPUGraphicsPipeline* pipeline = pipelines->get(PipelineKey::forLines(colorFormat))) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
            const SDL_GPUBufferBinding binding{lineBuffer.vertices, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, lineVertexCount, 1, 0, 0);
        }
    }
    if (spriteVertexCount > 0) {
        drawSprites(pass, cmd, colorFormat, width, height);
    }
    SDL_EndGPURenderPass(pass);
}

void Renderer::Impl::renderWindow(const FrameParams& frame, std::span<const DrawItem> items,
                                  std::span<const DebugLine> lines, std::span<const Sprite> sprites) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        logError("SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window->handle(), &swapchain, &width, &height)) {
        if (!swapchainFailed) {
            logError("acquiring the swapchain texture failed: {}", SDL_GetError());
            swapchainFailed = true;  // log once per run of failures, not every frame
        }
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    swapchainFailed = false;

    if (!swapchain || width == 0 || height == 0) {
        // Minimized or hidden: there is nothing to draw into. Sleeping keeps the caller's loop from spinning.
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_Delay(16);
        return;
    }
    if (ensureWindowDepth(width, height)) {
        drawFrame(cmd, swapchain, swapchainFormat, windowDepth.texture, width, height, frame, items, lines, sprites);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

Image Renderer::Impl::renderOffscreen(int width, int height, const FrameParams& frame,
                                      std::span<const DrawItem> items, std::span<const DebugLine> lines,
                                      std::span<const Sprite> sprites) {
    if (width <= 0 || height <= 0) {
        logError("renderToImage: invalid size {}x{}", width, height);
        return {};
    }
    const auto w = static_cast<Uint32>(width);
    const auto h = static_cast<Uint32>(height);
    if (!ensureOffscreen(w, h)) {
        return {};
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) {
        logError("SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return {};
    }
    drawFrame(cmd, offscreen.color, kColorFormat, offscreen.depth, w, h, frame, items, lines, sprites);

    SDL_GPUTextureRegion source{};
    source.texture = offscreen.color;
    source.w = w;
    source.h = h;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = offscreen.readback;
    destination.pixels_per_row = w;
    destination.rows_per_layer = h;
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);

    // Not SDL_SubmitGPUCommandBufferAndAcquireFence: SDL 3.4's Metal back end does not recycle a command
    // buffer submitted that way when its fence is waited on and released, so each call kept a new set of
    // uniform buffers and a new cycled depth texture (about 4 MB at 1024x768). A plain submit plus an
    // idle wait recycles them.
    if (!SDL_SubmitGPUCommandBuffer(cmd) || !SDL_WaitForGPUIdle(device)) {
        logError("renderToImage: submit failed: {}", SDL_GetError());
        return {};
    }

    const void* pixels = SDL_MapGPUTransferBuffer(device, offscreen.readback, false);
    if (!pixels) {
        logError("renderToImage: mapping the readback buffer failed: {}", SDL_GetError());
        return {};
    }
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    std::memcpy(image.rgba.data(), pixels, image.rgba.size());
    SDL_UnmapGPUTransferBuffer(device, offscreen.readback);
    return image;
}

}  // namespace mb
