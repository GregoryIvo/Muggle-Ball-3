#include "mb/game/ParticleSystem.h"

#include "mb/engine/Log.h"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

// ---- How the types behave (docs/GAMEPLAY.md section 19). The files give counts, sizes, speeds, radii and fade times; what they leave
// out is chosen here, each choice tagged [I] there.

constexpr float kDefaultFade = 0.5f;  // a definition with no `fade_time_msec` fades in this long
constexpr float kShortestLife = 0.01f;  // no particle lives less: a Firework of radius 0 would otherwise live for no time at all
constexpr float kFlatLift = 0.1f;     // a Flash lying on the floor is raised this far, as the prizes' marks are

// Firework: the sparks fly out along random directions and slow to a stop at their reach, between kSparkSlowest of `radius` and all of it, so
// that each lives 2 * reach / `speed` (the speed it leaves at, slowing evenly); `direction` is the way they droop, with this pull.
constexpr float kSparkSlowest = 0.5f;
constexpr float kSparkDroop = 5.0f;  // units/s^2
constexpr float kSparkShrink = 0.5f;  // how much smaller a spark is when it is gone

constexpr float kTrailShrink = 0.6f;  // a trail's sprite loses this much of its size over its life

constexpr float kSmokeSlowest = 0.7f;  // a puff rises at this share of `speed` to all of it
constexpr float kSmokeSmallest = 0.4f; // and starts this share of its size, growing to the whole
constexpr float kSmokeAlpha = 0.4f;
constexpr float kDefaultSmokeLife = 1.5f;

constexpr float kBubbleLife = 1.5f;    // WaterBubble has no height, so no life of its own
constexpr float kBubbleSway = 0.15f;   // units to either side
constexpr float kBubbleSwayRate = 0.8f;  // turns a second

constexpr float kLeafSlowest = 0.7f;  // a leaf falls at this share of `speed` to 1.3 times it
constexpr float kLeafFastest = 1.3f;
constexpr float kLeafFloor = 0.15f;   // the leaves lie here when they have reached the floor
constexpr float kLeafFlutterRate = 1.5f;  // turns a second
constexpr float kDefaultLeafLife = 3.0f;

constexpr float kTornadoAlpha = 0.35f;
constexpr float kDefaultTornadoLife = 6.0f;

constexpr float kBoltShakeRate = 30.0f;  // a Lightning takes a new shape this many times a second
// ParticleLaser1.png is three tiles side by side: the round end on the left, the body, the round end on the right.
constexpr float kTileLeft = 1.0f / 3.0f, kTileRight = 2.0f / 3.0f;

constexpr float kTiny = 1e-4f;

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

Color lerp(Color a, Color b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

// The share of full strength of something that has lived `age` of `life`, fading over the last `fade` seconds (all of it if it
// is shorter than that).
float fadeOut(float age, float life, float fade) {
    return std::clamp((life - age) / std::max(std::min(fade, life), kTiny), 0.0f, 1.0f);
}

float fadeOf(const ParticleDef& def) {
    return def.fadeTime > 0 ? def.fadeTime : kDefaultFade;
}

// How long a beam takes to fade out once it starts to.
float beamFade(float asked, const ParticleDef& def) {
    return asked > 0 ? asked : fadeOf(def);
}

// A number in [0, 1) from three others: stateless, so the same bolt shakes the same way.
float hash01(std::uint32_t seed, std::uint32_t a, std::uint32_t b) {
    std::uint32_t h = seed * 2654435761u ^ (a + 1) * 40503u ^ (b + 1) * 2246822519u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// Two unit vectors that, with `axis`, make a frame.
void frameOf(Vec3 axis, Vec3& e1, Vec3& e2) {
    const Vec3 helper = std::fabs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    e1 = normalize(cross(helper, axis));
    e2 = cross(axis, e1);
}

// A unit vector within `degrees` of `axis`, evenly over the cap, from two draws in [0, 1).
Vec3 inCone(Vec3 axis, float degrees, float u, float v) {
    Vec3 e1, e2;
    frameOf(axis, e1, e2);
    const float cosine = 1 - u * (1 - std::cos(radians(degrees)));
    const float sine = std::sqrt(std::max(0.0f, 1 - cosine * cosine));
    const float turn = 2 * kPi * v;
    return axis * cosine + (e1 * std::cos(turn) + e2 * std::sin(turn)) * sine;
}

// A unit vector evenly over the sphere.
Vec3 onSphere(float u, float v) {
    const float z = 2 * u - 1, turn = 2 * kPi * v, ring = std::sqrt(std::max(0.0f, 1 - z * z));
    return {std::cos(turn) * ring, z, std::sin(turn) * ring};
}

// A point evenly over the disc of `radius` perpendicular to `axis`.
Vec3 onDisc(Vec3 axis, float radius, float u, float v) {
    Vec3 e1, e2;
    frameOf(axis, e1, e2);
    const float turn = 2 * kPi * v, reach = radius * std::sqrt(u);
    return e1 * (std::cos(turn) * reach) + e2 * (std::sin(turn) * reach);
}

// The colours a definition runs between: `colour1` to `colour2`, or its one `colour`, or white (the texture's own).
struct Colours {
    Color from{1, 1, 1, 1}, to{1, 1, 1, 1};
    bool given = false;
};

Colours coloursOf(const ParticleDef& def) {
    Colours colours;
    if (def.colour1 || def.colour2) {
        colours.from = def.colour1 ? *def.colour1 : *def.colour2;
        colours.to = def.colour2 ? *def.colour2 : *def.colour1;
        colours.given = true;
    } else if (def.colour) {
        colours.from = colours.to = *def.colour;
        colours.given = true;
    }
    // The files' alpha byte (0x00... on the trails and the bolt) is not "invisible": the fade is the particle's own.
    colours.from.a = colours.to.a = 1;
    return colours;
}

BlendMode blendOf(ParticleType type) {
    switch (type) {
        case ParticleType::Flash:
        case ParticleType::Smoke:
        case ParticleType::LeafFall:
        case ParticleType::WaterBubble:
        case ParticleType::Tornado:
        case ParticleType::Oreol:
            return BlendMode::AlphaBlend;
        default:
            return BlendMode::Additive;
    }
}

ParticleQuad styled(const ParticleDef& def) {
    ParticleQuad quad;
    quad.def = &def;
    quad.blend = blendOf(def.type);
    quad.colourOnly = !def.modulateColour && coloursOf(def).given;
    return quad;
}

// A quad `side` across that faces the camera, turned by `roll` radians about the line of sight.
void faceCamera(ParticleQuad& quad, const ParticleView& view, float side, float roll) {
    const float c = std::cos(roll) * side * 0.5f, s = std::sin(roll) * side * 0.5f;
    quad.halfRight = view.right * c + view.up * s;
    quad.halfUp = view.up * c - view.right * s;
}

// The sideways direction of a segment as the camera sees it: perpendicular to it in the picture, so that a strip along it
// shows its whole width.
Vec3 acrossOf(Vec3 along, const ParticleView& view) {
    const float x = dot(along, view.right), y = dot(along, view.up), size = std::hypot(x, y);
    if (size < kTiny) {
        return view.right;
    }
    return view.right * (-y / size) + view.up * (x / size);
}

}  // namespace

ParticleView particleViewOf(const Mat4& facing) {
    ParticleView view;
    view.right = {facing.m[0][0], facing.m[0][1], facing.m[0][2]};
    view.up = {facing.m[1][0], facing.m[1][1], facing.m[1][2]};
    view.forward = {facing.m[2][0], facing.m[2][1], facing.m[2][2]};
    return view;
}

float particleLife(const ParticleDef& def) {
    float life = fadeOf(def);
    switch (def.type) {
        case ParticleType::Smoke:
            life = def.speed > 0 && def.height > 0 ? def.height / def.speed : kDefaultSmokeLife;
            break;
        case ParticleType::LeafFall:
            life = def.speed > 0 && def.height > 0 ? def.height / def.speed : kDefaultLeafLife;
            break;
        case ParticleType::Tornado:
            life = def.speed > 0 && def.height > 0 ? def.height / def.speed : kDefaultTornadoLife;
            break;
        case ParticleType::WaterBubble:
            life = kBubbleLife;
            break;
        case ParticleType::Firework:
            life = def.speed > 0 ? 2 * def.radius / def.speed : fadeOf(def);
            break;
        default:
            break;
    }
    return std::max(life, kShortestLife);
}

ParticleSystem::ParticleSystem(const ParticleLibrary& library, std::uint32_t seed) : library_(library), seed_(seed) {}

ParticleSystem::Effect* ParticleSystem::find(EffectId id) {
    const auto found = std::ranges::lower_bound(effects_, id, {}, &Effect::id);  // ids only grow, so the list is sorted
    return found != effects_.end() && found->id == id ? &*found : nullptr;
}

const ParticleSystem::Effect* ParticleSystem::find(EffectId id) const {
    const auto found = std::ranges::lower_bound(effects_, id, {}, &Effect::id);
    return found != effects_.end() && found->id == id ? &*found : nullptr;
}

float ParticleSystem::draw(Effect& effect) {
    return hash01(effect.seed, effect.draws++, 0x51ED);
}

EffectId ParticleSystem::make(const ParticleDef* def, Vec3 at, const EffectOptions& options) {
    Effect effect;
    effect.id = nextId_++;
    effect.def = def;
    effect.at = effect.previous = at;
    effect.scale = std::max(options.scale, 0.0f);
    effect.holdSeconds = std::max(options.seconds, 0.0f);
    effect.fadeSeconds = std::max(options.fade, 0.0f);
    effect.growSeconds = std::max(options.grow, 0.0f);
    effect.direction = normalize(length(options.direction) > 0 ? options.direction : def->direction.value_or(Vec3{}));
    seed_ = seed_ * 1664525u + 1013904223u;
    effect.seed = options.seed != 0 ? options.seed : seed_ ^ (effect.id * 0x9E3779B9u);
    effects_.push_back(effect);
    return effect.id;
}

EffectId ParticleSystem::spawn(std::string_view effect, Vec3 at) {
    return spawn(effect, at, EffectOptions{});
}

EffectId ParticleSystem::attach(std::string_view effect, Vec3 at) {
    return attach(effect, at, EffectOptions{});
}

EffectId ParticleSystem::beam(std::string_view effect, Vec3 from, Vec3 to) {
    return beam(effect, from, to, EffectOptions{});
}

EffectId ParticleSystem::spawn(std::string_view effect, Vec3 at, const EffectOptions& options) {
    const ParticleDef* def = library_.find(effect);
    if (def == nullptr) {
        logWarn("particles: no effect named {}", effect);
        return kNoEffect;
    }
    switch (def->type) {
        case ParticleType::Oreol:
        case ParticleType::Laser:
        case ParticleType::Lightning:
            return kNoEffect;
        case ParticleType::Flash:
        case ParticleType::Firework:
        case ParticleType::LeafFall: {
            const EffectId id = make(def, at, options);
            burst(*find(id));
            return id;
        }
        default: {
            const EffectId id = make(def, at, options);
            Effect& emitter = *find(id);
            if (options.seconds > 0) {
                emitter.emitting = true;
                emitter.emitLeft = options.seconds;
            } else {
                burst(emitter);
            }
            return id;
        }
    }
}

EffectId ParticleSystem::attach(std::string_view effect, Vec3 at, const EffectOptions& options) {
    const ParticleDef* def = library_.find(effect);
    if (def == nullptr) {
        logWarn("particles: no effect named {}", effect);
        return kNoEffect;
    }
    switch (def->type) {
        case ParticleType::Trail:
        case ParticleType::Smoke:
        case ParticleType::WaterBubble:
        case ParticleType::Tornado: {
            const EffectId id = make(def, at, options);
            Effect& emitter = *find(id);
            emitter.emitting = true;
            emitter.emitLeft = options.seconds > 0 ? options.seconds : -1.0f;
            return id;
        }
        default:
            return kNoEffect;
    }
}

EffectId ParticleSystem::beam(std::string_view effect, Vec3 from, Vec3 to, const EffectOptions& options) {
    const ParticleDef* def = library_.find(effect);
    if (def == nullptr) {
        logWarn("particles: no effect named {}", effect);
        return kNoEffect;
    }
    if (def->type != ParticleType::Laser && def->type != ParticleType::Lightning) {
        return kNoEffect;
    }
    const EffectId id = make(def, from, options);
    Effect& made = *find(id);
    made.from = from;
    made.to = to;
    made.holdLeft = made.holdSeconds;
    return id;
}

void ParticleSystem::moveTo(EffectId effect, Vec3 at) {
    if (Effect* found = find(effect)) {
        found->at = at;
    }
}

void ParticleSystem::aim(EffectId effect, Vec3 from, Vec3 to) {
    if (Effect* found = find(effect)) {
        found->from = from;
        found->to = to;
        found->held = true;
        found->holdLeft = found->holdSeconds;
        found->fading = 0;
    }
}

void ParticleSystem::stop(EffectId effect) {
    if (Effect* found = find(effect)) {
        found->emitting = false;
        found->held = false;
        found->holdLeft = 0;
    }
}

bool ParticleSystem::alive(EffectId effect) const {
    return find(effect) != nullptr;
}

// ---- Starting particles

ParticleSystem::Particle ParticleSystem::start(Effect& effect, Vec3 at, float age) {
    const ParticleDef& def = *effect.def;
    Particle p;
    p.def = &def;
    p.effect = effect.id;
    p.origin = at;
    p.age = age;
    p.scale = effect.scale;
    p.size = def.size * effect.scale;
    for (float& r : p.r) {
        r = draw(effect);
    }
    const Vec3 up{0, 1, 0};
    const Vec3 axis = length(effect.direction) > 0 ? effect.direction : up;
    switch (def.type) {
        case ParticleType::Flash:
            p.life = effect.holdSeconds > 0 ? effect.holdSeconds : fadeOf(def);
            p.velocity = effect.direction;  // the plane it lies in is perpendicular to this; none: it faces the camera
            break;
        case ParticleType::Firework: {
            const float reach = def.radius * effect.scale * lerp(kSparkSlowest, 1.0f, p.r[2]);
            p.life = std::max(def.speed > 0 && effect.scale > 0 ? 2 * reach / (def.speed * effect.scale) : fadeOf(def), kShortestLife);
            p.velocity = onSphere(p.r[0], p.r[1]) * reach;
            p.accel = effect.direction * (kSparkDroop * effect.scale);
            break;
        }
        case ParticleType::Trail: {
            p.life = fadeOf(def);
            const float u = draw(effect), v = draw(effect), within = draw(effect);  // (the order of the draws is the order they are written)
            p.origin = at + onSphere(u, v) * (def.radius * effect.scale * std::cbrt(within));
            break;
        }
        case ParticleType::Smoke: {
            p.life = particleLife(def);
            p.velocity = inCone(axis, def.angle, p.r[0], p.r[1]) * (def.speed * effect.scale * lerp(kSmokeSlowest, 1.0f, p.r[3]));
            const float u = draw(effect), v = draw(effect);
            p.origin = at + onDisc(axis, def.width * effect.scale, u, v);
            break;
        }
        case ParticleType::WaterBubble: {
            p.life = kBubbleLife;
            p.velocity = axis * (def.speed * effect.scale);
            const float u = draw(effect), v = draw(effect);
            p.origin = at + onDisc(axis, def.radius * effect.scale, u, v);
            break;
        }
        case ParticleType::LeafFall: {
            const Vec3 box = def.dimension.value_or(Vec3{}) * effect.scale;
            p.origin = at + Vec3{(draw(effect) - 0.5f) * box.x, (draw(effect) - 0.5f) * box.y, (draw(effect) - 0.5f) * box.z};
            const float fall = def.speed * effect.scale * lerp(kLeafSlowest, kLeafFastest, p.r[3]);
            p.life = fall > 0 && def.height > 0 ? def.height * effect.scale / fall : kDefaultLeafLife;
            p.velocity = {0, -fall, 0};
            // Each leaf drifts to one side as it comes down, up to `radius` away by the time it is gone.
            const float turn = 2 * kPi * p.r[0];
            p.accel = Vec3{std::cos(turn), 0, std::sin(turn)} * (def.radius * effect.scale * p.r[1] / p.life);
            break;
        }
        case ParticleType::Tornado:
            p.life = particleLife(def);
            break;
        default:
            break;
    }
    return p;
}

void ParticleSystem::emit(Effect& effect, int count, float stepSeconds) {
    for (int i = 0; i < count; ++i) {
        // Spread over the step, so that a moving emitter leaves an even line, and what was put out early has lived a little.
        const float share = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        particles_.push_back(start(effect, lerp(effect.previous, effect.at, share), stepSeconds * (1 - share)));
        ++effect.live;
    }
}

void ParticleSystem::burst(Effect& effect) {
    const int count = effect.def->type == ParticleType::Flash ? 1 : std::max(effect.def->count, 1);
    emit(effect, count, 0);
}

// ---- Time

void ParticleSystem::update(double seconds) {
    const auto dt = static_cast<float>(seconds);
    for (Particle& p : particles_) {
        p.age += dt;
    }
    std::erase_if(particles_, [&](const Particle& p) {
        if (p.age < p.life) {
            return false;
        }
        --find(p.effect)->live;
        return true;
    });
    for (Effect& effect : effects_) {
        effect.age += dt;
        const ParticleDef& def = *effect.def;
        if (def.type == ParticleType::Laser || def.type == ParticleType::Lightning) {
            if (effect.held) {
                effect.held = false;
            } else if (effect.holdLeft > 0) {
                effect.holdLeft -= dt;
            } else {
                effect.fading += dt;
            }
        } else if (effect.emitting) {
            float emitFor = dt;
            if (effect.emitLeft >= 0) {
                emitFor = std::min(dt, effect.emitLeft);
                effect.emitLeft -= emitFor;
                effect.emitting = effect.emitLeft > 0;
            }
            effect.owed += static_cast<double>(def.count) / particleLife(def) * emitFor;
            const auto due = static_cast<int>(effect.owed);
            effect.owed -= due;
            emit(effect, due, dt);
        }
        effect.previous = effect.at;
    }
    std::erase_if(effects_, [](const Effect& effect) {
        const bool beam = effect.def->type == ParticleType::Laser || effect.def->type == ParticleType::Lightning;
        return beam ? effect.fading >= beamFade(effect.fadeSeconds, *effect.def) : !effect.emitting && effect.live == 0;
    });
}

void ParticleSystem::clear() {
    effects_.clear();
    particles_.clear();
}

// ---- Quads

void ParticleSystem::appendQuads(const ParticleView& view, std::vector<ParticleQuad>& out) const {
    for (const Particle& particle : particles_) {
        quadsOf(particle, view, out);
    }
    for (const Effect& effect : effects_) {
        if (effect.def->type == ParticleType::Laser || effect.def->type == ParticleType::Lightning) {
            quadsOf(effect, view, out);
        }
    }
}

void ParticleSystem::quadsOf(const Particle& p, const ParticleView& view, std::vector<ParticleQuad>& out) const {
    const ParticleDef& def = *p.def;
    const float u = std::clamp(p.age / p.life, 0.0f, 1.0f);
    const Colours colours = coloursOf(def);
    ParticleQuad quad = styled(def);
    Vec3 at = p.origin;
    float side = p.size, alpha = 1, roll = 0;
    Color colour = colours.from;
    switch (def.type) {
        case ParticleType::Flash: {
            // It swells at `speed` units a second to its size, and fades over its life.
            side = def.speed > 0 ? std::min(p.size, def.speed * p.scale * p.age) : p.size;
            alpha = 1 - u;
            if (length(p.velocity) > 0) {
                Vec3 e1, e2;
                frameOf(p.velocity, e1, e2);
                quad.depthTest = true;
                quad.centre = at + p.velocity * kFlatLift;
                quad.halfRight = e1 * (side * 0.5f);
                quad.halfUp = e2 * (side * 0.5f);
                quad.colourBegin = quad.colourEnd = {1, 1, 1, alpha};
                out.push_back(quad);
                return;
            }
            roll = 2 * kPi * p.r[1];
            break;
        }
        case ParticleType::Firework: {
            // Out along its way, fast at first and not at all at its reach; and drooping.
            at = p.origin + p.velocity * (2 * u - u * u) + p.accel * (0.5f * p.age * p.age);
            side = p.size * (1 - kSparkShrink * u);
            alpha = fadeOut(p.age, p.life, fadeOf(def));
            colour = lerp(colours.from, colours.to, u);
            break;
        }
        case ParticleType::Trail:
            side = p.size * (1 - kTrailShrink * u);
            alpha = 1 - u;
            colour = lerp(colours.from, colours.to, u);
            break;
        case ParticleType::Smoke:
            at = p.origin + p.velocity * p.age;
            side = p.size * lerp(kSmokeSmallest, 1.0f, u);
            alpha = kSmokeAlpha * fadeOut(p.age, p.life, fadeOf(def));
            roll = 2 * kPi * p.r[2];
            break;
        case ParticleType::WaterBubble:
            at = p.origin + p.velocity * p.age +
                 view.right * (kBubbleSway * p.scale * std::sin(2 * kPi * (kBubbleSwayRate * p.age + p.r[2])));
            alpha = std::clamp(std::min(p.age, p.life - p.age) / std::max(fadeOf(def), kTiny), 0.0f, 1.0f);
            break;
        case ParticleType::LeafFall: {
            at = p.origin + p.velocity * p.age + p.accel * (p.age * p.age * 0.5f);
            at.y = std::max(at.y, kLeafFloor);
            alpha = fadeOut(p.age, p.life, fadeOf(def));
            roll = radians(def.angle) * std::sin(2 * kPi * (kLeafFlutterRate * p.age + p.r[2]));
            break;
        }
        case ParticleType::Tornado: {
            const float height = def.height * p.scale * u;
            const float reach = lerp(def.radiusSmall, def.radiusBig, u) * p.scale;
            const float turn = 2 * kPi * p.r[0] + def.speedAngular * p.age * (def.inverseRotation ? -1.0f : 1.0f);
            at = p.origin + Vec3{std::cos(turn) * reach, height, std::sin(turn) * reach};
            alpha = kTornadoAlpha * std::min(1.0f, p.age / std::max(fadeOf(def), kTiny)) * fadeOut(p.age, p.life, fadeOf(def));
            roll = 2 * kPi * p.r[2];
            break;
        }
        default:
            return;
    }
    quad.centre = at;
    faceCamera(quad, view, side, roll);
    colour.a = alpha;
    quad.colourBegin = quad.colourEnd = colour;
    out.push_back(quad);
}

void ParticleSystem::quadsOf(const Effect& effect, const ParticleView& view, std::vector<ParticleQuad>& out) const {
    const ParticleDef& def = *effect.def;
    const Colours colours = coloursOf(def);
    const float alpha = 1 - std::min(effect.fading / beamFade(effect.fadeSeconds, def), 1.0f);
    const float width = std::max(def.size, 0.0f) * effect.scale;
    const Vec3 span = effect.to - effect.from;
    const float length_ = length(span);
    if (length_ < kTiny || alpha <= 0) {
        return;
    }
    ParticleQuad base = styled(def);

    // A strip along a to b, a `width` wide as the camera sees it, over the middle tile of the texture; its colour runs from
    // `from` to `to`. The round end of each piece goes on its ends so that a bend in a bolt has no gap.
    const auto strip = [&](Vec3 a, Vec3 b, Color from, Color to) {
        ParticleQuad body = base;
        const Vec3 along = normalize(b - a);
        body.centre = (a + b) * 0.5f;
        body.halfRight = (b - a) * 0.5f;
        body.halfUp = acrossOf(along, view) * (width * 0.5f);
        body.uv0 = {kTileLeft, 0};
        body.uv1 = {kTileRight, 1};
        body.colourBegin = from;
        body.colourEnd = to;
        out.push_back(body);
    };
    const auto cap = [&](Vec3 at, Vec3 along, bool right, Color colour) {
        ParticleQuad round = base;
        round.centre = at;
        round.halfRight = along * (width * 0.5f);  // as long as the beam is wide
        round.halfUp = acrossOf(along, view) * (width * 0.5f);
        round.uv0 = {right ? kTileRight : 0.0f, 0};
        round.uv1 = {right ? 1.0f : kTileLeft, 1};
        round.colourBegin = round.colourEnd = colour;
        out.push_back(round);
    };
    const auto shade = [&](float t) {
        Color c = lerp(colours.from, colours.to, t);
        c.a = alpha;
        return c;
    };

    const Vec3 along = span / length_;
    if (def.type == ParticleType::Laser) {
        // The front runs out from `from` in `grow` seconds, else at `speed`; none (the yo-yo's string) is whole at once. The
        // colours run over the part that is out: the tip is the far colour all the way, as it is in the footage.
        float shown = length_;
        if (effect.growSeconds > 0) {
            shown = std::min(length_, length_ * effect.age / effect.growSeconds);
        } else if (def.speed > 0) {
            shown = std::min(length_, def.speed * effect.scale * effect.age);
        }
        const Vec3 front = effect.from + along * shown;
        strip(effect.from, front, shade(0), shade(1));
        cap(effect.from, along, false, shade(0));
        cap(front, along, true, shade(1));
        return;
    }

    // Lightning: `num` pieces between the ends, the points between them thrown sideways by up to `shift_coeff` of a piece's length;
    // `shake_coeff` of that throw is drawn anew each time the shape shakes, the rest is fixed.
    const int pieces = std::max(def.count, 1);
    const float piece = length_ / static_cast<float>(pieces);
    Vec3 e1, e2;
    frameOf(along, e1, e2);
    const auto step = static_cast<std::uint32_t>(effect.age * kBoltShakeRate);
    const auto throwOf = [&](std::uint32_t k, std::uint32_t when) {
        const float turn = 2 * kPi * hash01(effect.seed, k, when * 2);
        const float reach = hash01(effect.seed, k, when * 2 + 1);
        return (e1 * std::cos(turn) + e2 * std::sin(turn)) * reach;
    };
    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(pieces) + 1);
    for (int k = 0; k <= pieces; ++k) {
        Vec3 point = lerp(effect.from, effect.to, static_cast<float>(k) / static_cast<float>(pieces));
        if (k > 0 && k < pieces) {
            const auto index = static_cast<std::uint32_t>(k);
            const Vec3 thrown = throwOf(index, 0) * (1 - def.shakeCoeff) + throwOf(index, step + 1) * def.shakeCoeff;
            point = point + thrown * (def.shiftCoeff * piece);
        }
        points.push_back(point);
    }
    for (int k = 0; k < pieces; ++k) {
        const auto index = static_cast<std::size_t>(k);
        strip(points[index], points[index + 1], shade(0), shade(0));
    }
    for (int k = 0; k <= pieces; ++k) {
        const auto index = static_cast<std::size_t>(k);
        cap(points[index], k < pieces ? normalize(points[index + 1] - points[index]) : normalize(points[index] - points[index - 1]), k == pieces, shade(0));
    }
}

}  // namespace mb
