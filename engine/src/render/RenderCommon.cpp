#include "RenderCommon.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <tuple>

namespace mb::render {

RenderState RenderState::forMaterial(const MaterialParams& material) {
    RenderState state;
    state.cull = material.cull;
    state.depthWrite = material.depthWrite;
    state.depthTest = material.depthTest;
    switch (material.blend) {
        case BlendMode::Opaque:
            break;
        case BlendMode::AlphaBlend:
            state.blend = true;
            state.srcBlend = BlendFactor::SrcAlpha;
            state.dstBlend = BlendFactor::InvSrcAlpha;
            break;
        case BlendMode::Additive:
            state.blend = true;
            state.srcBlend = BlendFactor::SrcAlpha;
            state.dstBlend = BlendFactor::One;
            break;
        case BlendMode::Custom:
            state.blend = true;
            state.srcBlend = material.srcBlend;
            state.dstBlend = material.dstBlend;
            break;
    }
    return state;
}

RenderState RenderState::forLines() {
    RenderState state;
    state.program = Program::Line;
    state.cull = CullMode::None;
    state.depthWrite = false;
    return state;
}

RenderState RenderState::forSprites() {
    RenderState state;
    state.program = Program::Sprite;
    state.blend = true;
    state.srcBlend = BlendFactor::SrcAlpha;
    state.dstBlend = BlendFactor::InvSrcAlpha;
    state.cull = CullMode::None;
    state.depthWrite = false;
    state.depthTest = false;
    return state;
}

RenderState RenderState::forEffects(BlendMode blend, bool depthTest) {
    RenderState state;
    state.program = Program::Effect;
    state.blend = true;
    state.srcBlend = BlendFactor::SrcAlpha;
    state.dstBlend = blend == BlendMode::Additive ? BlendFactor::One : BlendFactor::InvSrcAlpha;
    state.cull = CullMode::None;
    state.depthWrite = false;
    state.depthTest = depthTest;
    return state;
}

std::uint32_t RenderState::pack() const {
    // The blend factors are D3DBLEND numbers, 1 to 10, which fit four bits each.
    const std::uint32_t factors =
        blend ? (static_cast<std::uint32_t>(srcBlend) << 4) | static_cast<std::uint32_t>(dstBlend) : 0;
    return (static_cast<std::uint32_t>(program) << 13) | (static_cast<std::uint32_t>(depthTest) << 12) |
           (static_cast<std::uint32_t>(depthWrite) << 11) | (static_cast<std::uint32_t>(cull) << 9) |
           (static_cast<std::uint32_t>(blend) << 8) | factors;
}

void DrawCollector::collect(const FrameParams& frame, std::span<const DrawItem> items, ResourceQuery& resources) {
    opaque_.clear();
    blended_.clear();
    for (const DrawItem& item : items) {
        if (!resources.hasMesh(item.mesh)) {
            resources.warnMissing("mesh", item.mesh);
            continue;
        }
        // A missing texture only warns: the draw still happens, with the back end's white texture.
        const MaterialParams& material = item.material;
        if (material.texture != 0 && !resources.hasTexture(material.texture)) {
            resources.warnMissing("texture", material.texture);
        }

        PendingDraw draw;
        draw.item = &item;
        draw.state = RenderState::forMaterial(material);
        if (material.blend == BlendMode::Opaque) {
            opaque_.push_back(draw);
        } else {
            const Vec3 toItem = item.world.translationPart() - frame.eye;
            draw.distanceSq = dot(toItem, toItem);
            blended_.push_back(draw);
        }
    }

    // Opaque: by layer, then grouped by state, texture and mesh so state changes are rare.
    // stable_sort keeps submission order among equals, so frames are reproducible.
    std::stable_sort(opaque_.begin(), opaque_.end(), [](const PendingDraw& a, const PendingDraw& b) {
        return std::make_tuple(a.item->layer, a.state.pack(), a.item->material.texture, a.item->mesh) <
               std::make_tuple(b.item->layer, b.state.pack(), b.item->material.texture, b.item->mesh);
    });
    // Blended: by layer, then far to near, which is what alpha blending needs to look right.
    std::stable_sort(blended_.begin(), blended_.end(), [](const PendingDraw& a, const PendingDraw& b) {
        if (a.item->layer != b.item->layer) {
            return a.item->layer < b.item->layer;
        }
        return a.distanceSq > b.distanceSq;
    });
}

std::uint32_t buildLineVertices(std::span<const DebugLine> lines, LineVertex* out) {
    LineVertex* const first = out;
    for (const DebugLine& line : lines) {
        *out++ = {line.a, line.color};
        *out++ = {line.b, line.color};
    }
    return static_cast<std::uint32_t>(out - first);
}

std::uint32_t buildSpriteVertices(std::span<const Sprite> sprites, SpriteVertex* out, std::vector<SpriteRun>& runs,
                                  ResourceQuery& resources) {
    runs.clear();
    constexpr std::uint32_t kVerticesPerSprite = 6;
    SpriteVertex* const first = out;
    for (const Sprite& sprite : sprites) {
        if (sprite.width <= 0 || sprite.height <= 0) {
            continue;
        }
        TextureHandle texture = 0;
        if (sprite.texture != 0) {
            if (!resources.hasTexture(sprite.texture)) {
                resources.warnMissing("texture", sprite.texture);
                continue;
            }
            texture = sprite.texture;
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
        const auto vertex = static_cast<std::uint32_t>(out - first);
        if (runs.empty() || runs.back().texture != texture) {
            runs.push_back({texture, vertex, 0});
        }
        runs.back().vertexCount += kVerticesPerSprite;
        for (const SpriteVertex& v : {tl, tr, br, tl, br, bl}) {
            *out++ = v;
        }
    }
    return static_cast<std::uint32_t>(out - first);
}

std::size_t effectVertexCapacity(std::span<const EffectBatch> effects) {
    std::size_t total = 0;
    for (const EffectBatch& batch : effects) {
        total += batch.vertices.size() / 3 * 3;  // whole triangles only
    }
    return total;
}

std::uint32_t buildEffectVertices(std::span<const EffectBatch> effects, EffectVertex* out, std::vector<EffectRun>& runs,
                                  ResourceQuery& resources) {
    runs.clear();
    EffectVertex* const first = out;
    for (const EffectBatch& batch : effects) {
        const std::size_t count = batch.vertices.size() / 3 * 3;
        if (count == 0) {
            continue;
        }
        TextureHandle texture = 0;
        if (batch.texture != 0) {
            if (!resources.hasTexture(batch.texture)) {
                resources.warnMissing("texture", batch.texture);
                continue;
            }
            texture = batch.texture;
        }
        runs.push_back({texture, batch.blend, batch.depthTest, batch.colourOnly,
                        static_cast<std::uint32_t>(out - first), static_cast<std::uint32_t>(count)});
        std::memcpy(out, batch.vertices.data(), count * sizeof(EffectVertex));
        out += count;
    }
    return static_cast<std::uint32_t>(out - first);
}

IndexRange indexRange(const DrawItem& item, std::uint32_t meshIndexCount) {
    const std::uint32_t first = std::min(item.firstIndex, meshIndexCount);
    const std::uint32_t available = meshIndexCount - first;
    const std::uint32_t count = item.indexCount == 0 ? available : std::min(item.indexCount, available);
    return {first, count};
}

Mat4 normalMatrix(const Mat4& world) { return transpose(inverse(world)); }

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height) {
    return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
}

}  // namespace mb::render
