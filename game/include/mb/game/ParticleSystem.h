#pragma once

#include "mb/engine/Math.h"
#include "mb/engine/Renderer.h"
#include "mb/game/ParticleDef.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace mb {

// The particle effects of Data/Particles (docs/GAMEPLAY.md section 19, docs/OBJECTS.md section 8): the explosions' flashes, the
// firework bursts, the rocket's trail, the smoke and sparks of the flickers, the falling leaves, the beams and bolts of the
// weapons. No GPU and no window: the system keeps the state of the particles, steps it, and says what quads they are as seen from
// a camera (appendQuads); ParticleBatches (ParticleDraw.h) turns those into what the renderer draws.
//
// An effect is made by name and set going, and then looks after itself:
//
//     ParticleSystem particles(library);
//     particles.spawn("explosebig", at);                                   // goes off, fades, and is gone
//     const EffectId trail = particles.attach("Trail/TrailOrange", rocketAt);   // leaves a trail while it is moved
//     particles.moveTo(trail, rocketAt);  // every frame
//     particles.stop(trail);              // no more; what it left fades out
//     const EffectId bolt = particles.beam("Lightning", from, to, {.seconds = 0.2f});
//     particles.update(dt);               // once a frame
//
// Everything is a function of the particle's age, so a run repeats from its seed and a test can ask for any moment.

// 0 is no effect: a name the library does not have, or a type the system does not draw, gives it.
using EffectId = std::uint32_t;
inline constexpr EffectId kNoEffect = 0;

struct EffectOptions {
    // The way it goes: the plane a Flash lies in (it is perpendicular to it; none faces the camera), the way Smoke and WaterBubble
    // rise. Zero: the definition's own `direction`, else straight up. The other types ignore it.
    Vec3 direction{};
    float scale = 1;  // of the sizes and distances the definition gives, not of the times
    // spawn(): how long an emitter (Trail, Smoke, WaterBubble, Tornado) keeps emitting before it stops by itself; 0 puts all of its
    // `num` particles out at once; how long a Flash lives (it swells for the first part and fades over all of it), 0: its
    // `fade_time_msec`. beam(): how long it stays at full strength, after it is made and after each aim(), before it fades.
    float seconds = 0;
    // beam(): how long it takes to fade out, for one that must be gone sooner (or later) than the definition's `fade_time_msec`
    // says; 0: the definition's.
    float fade = 0;
    std::uint32_t seed = 0;  // 0: the next of the system's own sequence
};

// Where the camera looks from: unit vectors, in the world, of its right, up and forward.
struct ParticleView {
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
};
// The view of a Mat4 whose rows are those three (facingGameCamera()).
ParticleView particleViewOf(const Mat4& facing);

// One quad as it is drawn: a parallelogram from its centre and the two half-edges, texture coordinates over it, a colour at each
// end of the `halfRight` axis (beams fade from one to the other), and how it is blended.
struct ParticleQuad {
    const ParticleDef* def = nullptr;  // for its texture
    Vec3 centre{};
    Vec3 halfRight{}, halfUp{};  // the corner (u0, v0), the picture's top left, is centre - halfRight + halfUp
    Vec2 uv0{0, 0}, uv1{1, 1};
    Color colourBegin{1, 1, 1, 1}, colourEnd{1, 1, 1, 1};  // at u0 and at u1; alpha is straight
    BlendMode blend = BlendMode::Additive;
    bool depthTest = false;
    bool colourOnly = false;  // EffectBatch::colourOnly
};

class ParticleSystem {
public:
    // The library must outlive the system.
    explicit ParticleSystem(const ParticleLibrary& library, std::uint32_t seed = 0x9E3779B9u);

    const ParticleLibrary& library() const { return library_; }

    // An effect by its name ("explosebig", "Fireworks/red"; ParticleLibrary::find) at a point. A Flash, a Firework and a LeafFall go
    // off at once; an emitter emits for `options.seconds` (or puts its `num` particles out at once). Returns kNoEffect if there is
    // no such definition, or it is an Oreol or a beam (which the prizes draw, and beam() makes).
    EffectId spawn(std::string_view effect, Vec3 at);
    EffectId spawn(std::string_view effect, Vec3 at, const EffectOptions& options);
    // An emitter that goes on until stop(): move it with moveTo() and the particles come out where it was, however fast it went
    // and however long the frames.
    EffectId attach(std::string_view effect, Vec3 at);
    EffectId attach(std::string_view effect, Vec3 at, const EffectOptions& options);
    // A beam (Laser) or a bolt (Lightning) between two points, at full strength for `options.seconds` and then fading over the
    // definition's `fade_time_msec`. A Laser's front runs on from `from` at its `speed`; a Lightning's shape shakes.
    EffectId beam(std::string_view effect, Vec3 from, Vec3 to);
    EffectId beam(std::string_view effect, Vec3 from, Vec3 to, const EffectOptions& options);

    // Where an attached effect is now. The particles it puts out from here on come out along the way from where it was.
    void moveTo(EffectId effect, Vec3 at);
    // Where a beam is now; it is at full strength again for its `seconds`, so one aimed every frame never fades.
    void aim(EffectId effect, Vec3 from, Vec3 to);
    // No more particles from an emitter; the ones out live their lives. A beam starts to fade at once.
    void stop(EffectId effect);
    // Whether it is still emitting or has particles, or is a beam that has not faded out.
    bool alive(EffectId effect) const;

    // Brings every effect on by `seconds`: emits, ages, and drops what is over.
    void update(double seconds);
    void clear();

    // The quads of everything alive, as `view` sees it, oldest effect first.
    void appendQuads(const ParticleView& view, std::vector<ParticleQuad>& out) const;
    std::size_t particleCount() const { return particles_.size(); }
    std::size_t effectCount() const { return effects_.size(); }

private:
    struct Particle {
        const ParticleDef* def = nullptr;
        EffectId effect = kNoEffect;
        Vec3 origin{};
        Vec3 velocity{};  // a Firework's: the way and how far it flies; the others': the way it goes and how fast
        Vec3 accel{};
        float age = 0;
        float life = 0;
        float size = 0;   // at its biggest, scale included
        float scale = 1;
        float r[4]{};     // draws in [0, 1) that do not change: colour, spin, sway, speed
    };
    struct Effect {
        EffectId id = kNoEffect;
        const ParticleDef* def = nullptr;
        Vec3 at{}, previous{};     // where an emitter is, and where it was a step ago
        Vec3 from{}, to{};         // a beam's ends
        Vec3 direction{};
        float scale = 1;
        float age = 0;
        float holdSeconds = 0;     // a beam's
        float fadeSeconds = 0;     // and how long it takes to fade out, 0: its definition's
        float holdLeft = 0;
        float fading = 0;          // how long a beam has been fading
        bool held = true;          // aimed since the last update
        bool emitting = false;
        float emitLeft = -1;       // seconds of emitting left; below 0: until stop()
        double owed = 0;           // particles due and not yet out
        int live = 0;              // its particles still alive
        std::uint32_t seed = 0;
        std::uint32_t draws = 0;   // how many numbers of the seed's sequence are used
    };

    EffectId make(const ParticleDef* def, Vec3 at, const EffectOptions& options);
    Effect* find(EffectId id);
    const Effect* find(EffectId id) const;
    float draw(Effect& effect);  // the next number of its sequence, in [0, 1)
    void emit(Effect& effect, int count, float stepSeconds);
    void burst(Effect& effect);
    Particle start(Effect& effect, Vec3 at, float age);
    void quadsOf(const Particle& particle, const ParticleView& view, std::vector<ParticleQuad>& out) const;
    void quadsOf(const Effect& effect, const ParticleView& view, std::vector<ParticleQuad>& out) const;

    const ParticleLibrary& library_;
    std::uint32_t seed_;
    EffectId nextId_ = 1;
    std::vector<Effect> effects_;
    std::vector<Particle> particles_;
};

// How long a particle of this definition lives and how many are alive at once at best: the steady state of an emitter puts out
// count / life of them each second. Exposed for the tests and for docs/GAMEPLAY.md section 19.
float particleLife(const ParticleDef& def);

}  // namespace mb
