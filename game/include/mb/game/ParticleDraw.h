#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/ParticleDef.h"
#include "mb/game/ParticleSystem.h"

#include <functional>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

namespace mb {

// The particles' quads as the renderer takes them: triangles in a few batches, one for each texture and way of blending, whatever
// the number of particles (Renderer::renderToWindow's `effects`). It owns the vertices that the batches point into, which stay
// valid until the next build().
class ParticleBatches {
public:
    // The image of a texture by the name the data gives it (`Particles/ParticleBubble.png`), uploaded; 0 if there is none. The
    // texture of a definition that has none to be found (the Tornado's) is `Particles/PARTICLE.PNG`'s, a soft white dot.
    using TextureLookup = std::function<TextureHandle(std::string_view)>;
    static constexpr std::string_view kFallbackTexture = "Particles/PARTICLE.PNG";

    ParticleBatches(const ParticleLibrary& library, TextureLookup lookup);

    // Replaces the batches with those of the system's particles as `view` sees them. `brightness` multiplies the colours (the night
    // dims what has no light of its own).
    void build(const ParticleSystem& system, const ParticleView& view, float brightness = 1);
    void build(std::span<const ParticleQuad> quads, float brightness = 1);

    std::span<const EffectBatch> batches() const { return batches_; }
    std::size_t quadCount() const { return vertices_.size() / 6; }

private:
    // What a batch is made of, for sorting: alpha blended first so that the glows go over the smoke, then by texture.
    struct Key {
        bool additive = false;
        bool depthTest = false;
        bool colourOnly = false;
        TextureHandle texture = 0;
        auto tie() const { return std::tie(additive, depthTest, colourOnly, texture); }
        bool operator==(const Key& other) const { return tie() == other.tie(); }
        bool operator<(const Key& other) const { return tie() < other.tie(); }
    };
    struct Run {  // the quads of one key, in the order of order_
        Key key;
        std::size_t first = 0, count = 0;
    };

    TextureHandle textureOf(const ParticleDef& def);

    TextureLookup lookup_;
    std::vector<TextureHandle> textures_;  // by definition, once looked up
    std::vector<bool> resolved_;
    TextureHandle fallback_ = 0;
    bool fallbackResolved_ = false;
    // Kept between builds to reuse their storage.
    std::vector<ParticleQuad> quads_;
    std::vector<Key> keys_;
    std::vector<std::size_t> order_;
    std::vector<Run> runs_;
    std::vector<EffectVertex> vertices_;
    std::vector<EffectBatch> batches_;
};

}  // namespace mb
