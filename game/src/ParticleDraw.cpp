#include "mb/game/ParticleDraw.h"

#include "mb/engine/Log.h"

#include <algorithm>

namespace mb {

ParticleBatches::ParticleBatches(const ParticleLibrary& library, TextureLookup lookup)
    : library_(library),
      lookup_(std::move(lookup)),
      textures_(library.all().size(), 0),
      resolved_(library.all().size(), false),
      missing_(library.all().size(), false),
      said_(library.all().size(), false) {}

void ParticleBatches::resolve(const ParticleDef& def) {
    if (resolved_[def.id]) {
        return;
    }
    resolved_[def.id] = true;
    textures_[def.id] = def.texture.empty() ? 0 : lookup_(def.texture);
    if (textures_[def.id] == 0) {
        if (!fallbackResolved_) {
            fallbackResolved_ = true;
            fallback_ = lookup_(kFallbackTexture);
        }
        missing_[def.id] = true;
        textures_[def.id] = fallback_;
    }
}

void ParticleBatches::preload() {
    for (const ParticleDef& def : library_.all()) {
        if (def.type != ParticleType::Oreol) {
            resolve(def);
        }
    }
}

TextureHandle ParticleBatches::textureOf(const ParticleDef& def) {
    if (def.id >= textures_.size()) {
        return 0;
    }
    resolve(def);
    if (missing_[def.id] && !said_[def.id]) {
        said_[def.id] = true;
        logWarn("particles: no texture {} for {}/{}, drawn with {}", def.texture, def.file, def.name, kFallbackTexture);
    }
    return textures_[def.id];
}

void ParticleBatches::build(const ParticleSystem& system, const ParticleView& view, float brightness) {
    quads_.clear();
    system.appendQuads(view, quads_);
    build(quads_, brightness);
}

void ParticleBatches::build(std::span<const ParticleQuad> quads, float brightness) {
    batches_.clear();
    vertices_.clear();
    runs_.clear();
    keys_.clear();
    order_.resize(quads.size());
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const ParticleQuad& quad = quads[i];
        keys_.push_back({quad.blend == BlendMode::Additive, quad.depthTest, quad.colourOnly, textureOf(*quad.def)});
        order_[i] = i;
    }
    // Stable: within a batch the quads stay in the order the system gave them, oldest first.
    std::ranges::stable_sort(order_, [&](std::size_t a, std::size_t b) { return keys_[a] < keys_[b]; });

    vertices_.reserve(quads.size() * 6);
    for (const std::size_t index : order_) {
        const ParticleQuad& quad = quads[index];
        if (runs_.empty() || !(runs_.back().key == keys_[index])) {
            runs_.push_back({keys_[index], vertices_.size(), 0});
        }
        const auto shade = [brightness](Color c) { return Color{c.r * brightness, c.g * brightness, c.b * brightness, c.a}; };
        const Color begin = shade(quad.colourBegin), end = shade(quad.colourEnd);
        const EffectVertex tl{quad.centre - quad.halfRight + quad.halfUp, {quad.uv0.x, quad.uv0.y}, begin};
        const EffectVertex tr{quad.centre + quad.halfRight + quad.halfUp, {quad.uv1.x, quad.uv0.y}, end};
        const EffectVertex br{quad.centre + quad.halfRight - quad.halfUp, {quad.uv1.x, quad.uv1.y}, end};
        const EffectVertex bl{quad.centre - quad.halfRight - quad.halfUp, {quad.uv0.x, quad.uv1.y}, begin};
        for (const EffectVertex& v : {tl, tr, br, tl, br, bl}) {
            vertices_.push_back(v);
        }
        runs_.back().count += 6;
    }
    // The spans are made once the vertices stop growing.
    for (const Run& run : runs_) {
        EffectBatch batch;
        batch.vertices = std::span<const EffectVertex>(vertices_).subspan(run.first, run.count);
        batch.texture = run.key.texture;
        batch.blend = run.key.additive ? BlendMode::Additive : BlendMode::AlphaBlend;
        batch.depthTest = run.key.depthTest;
        batch.colourOnly = run.key.colourOnly;
        batches_.push_back(batch);
    }
}

}  // namespace mb
