#include "mb/game/ObjectEffects.h"

#include "mb/game/GameCamera.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mb {

namespace {

// ---- How the effects look. The sizes and times are the particle files' (Data/Particles/*.xml [D]) where they have one,
// else what the footage shows [M] or a guess [I].

// A charge of dynamite: explosebig.xml's Flash, `size` 25 for the radius 5 (five times it) and a fade of 500 ms; it swells to
// its size fast ([I]: the footage's frames at 1/30 s show it full at once). Sparks as the bricks have.
constexpr float kBlastSizePerRadius = 5.0f;
constexpr double kBlastGrowSeconds = 0.08;
constexpr double kBlastSeconds = 0.5;
constexpr int kBlastSparks = 20;
constexpr float kSparkGravity = 18.0f;

// A cannonball bursting: exploseyadro.xml, a flash of size 10 that fades in 500 ms [D]; the footage's is white and then
// orange, about 3 units wide for the bat's cannon (F48, "Weapons") [M].
constexpr float kShotBurstSize = 10.0f;
constexpr double kShotBurstSeconds = 0.5;
// The puff at a cannon's muzzle [I].
constexpr double kMuzzleSeconds = 0.5;
constexpr float kMuzzleFlashSize = 3.5f;

// A ball in a teleport: a ring on the floor and a glow where it went in or came out [I].
constexpr double kPortalSeconds = 0.35;
constexpr float kPortalRingSize = 6.0f;
constexpr float kPortalGlowSize = 4.0f;
constexpr float kPortalFloor = 0.05f;

// A firework rocket's burst: Fireworks.xml's 30 particles of radius 10 and size 3.5 that fade in 500 ms [D], thrown at
// speed 30; the footage's is a pink ball that spreads to about 25 units across in 0.3 s and is gone by 0.6 s [M].
constexpr int kBurstParticles = 30;
constexpr float kBurstSpeed = 30.0f;
constexpr float kBurstSize = 4.0f;
constexpr double kBurstSeconds = 0.6;
// The rocket's trail: the Trail particle, 30 sprites fading in 500 ms; here a few behind it.
constexpr int kTrailSprites = 8;
constexpr float kTrailSpacing = 0.7f;
constexpr float kTrailSize = 1.8f;
// The six bursts' colours (Fireworks.xml: colour1 to colour2, ARGB) [D].
constexpr std::array<std::array<std::uint32_t, 2>, 6> kBurstColours{{{0xffff0000, 0xffffff00},
                                                                       {0xffff0000, 0xffff8800},
                                                                       {0xffffff00, 0xffff88ff},
                                                                       {0xff00ff00, 0xffffff00},
                                                                       {0xff0000ff, 0xff00ffff},
                                                                       {0xffff00ff, 0xffffffff}}};

// A spring that is hit gives: it swells and shrinks back [I].
constexpr double kGiveSeconds = 0.25;
constexpr float kGiveAmount = 0.2f;

// The glow of a flicker's fire, above the cauldron's middle [I; the footage's light changes by 1.4 %, F13].
constexpr float kGlowHeight = 0.9f;
constexpr float kGlowSize = 4.5f;

constexpr float kShadowAlpha = 0.85f;
constexpr float kShadowLift = 0.04f;  // above the floor, which is at y = 0 in every level

// The camera of the game turns a quad to face it.
Mat4 facingGameCamera() {
    const Vec3 forward = normalize(kGameCameraTarget - kGameCameraEye);
    const Vec3 right = normalize(cross({0, 1, 0}, forward));
    const Vec3 up = cross(forward, right);
    Mat4 m;
    const Vec3 axes[3] = {right, up, forward};
    for (int i = 0; i < 3; ++i) {
        m.m[i][0] = axes[i].x;
        m.m[i][1] = axes[i].y;
        m.m[i][2] = axes[i].z;
    }
    return m;
}

// A level of one object with a mesh, to draw it at the origin.
Level modelLevel(ObjectType type, const char* mesh, const char* material) {
    GameObject object;
    object.type = type;
    object.mesh = mesh;
    object.material = material;
    Actor actor;
    actor.object = object;
    Level level;
    level.actors.push_back(actor);
    return level;
}

// A number in [0, 1) from two others: the same effect always scatters the same way.
float hash01(std::uint32_t seed, std::uint32_t n) {
    std::uint32_t h = seed * 2654435761u ^ (n + 1) * 40503u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFu) / 65536.0f;
}

Color argb(std::uint32_t colour, float alpha) {
    return {static_cast<float>((colour >> 16) & 0xFF) / 255.0f, static_cast<float>((colour >> 8) & 0xFF) / 255.0f,
            static_cast<float>(colour & 0xFF) / 255.0f, alpha};
}

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a};
}

std::uint32_t seedOf(Vec3 at, double time) {
    return static_cast<std::uint32_t>(std::fabs(at.x) * 977.0f + std::fabs(at.z) * 1597.0f) +
           static_cast<std::uint32_t>(time * 1000.0);
}

}  // namespace

ObjectEffects::ObjectEffects(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures)
    : rocket_(cache, materials, textures, modelLevel(ObjectType::FireRocket, "Fire-rocket.x", "Fire-rocket"), PhysicsPlan{}),
      shot_(cache, materials, textures, modelLevel(ObjectType::Bullet, "Yadro.x", "Telega1.jpg"), PhysicsPlan{}),
      quad_(cache.quad()),
      blastTexture_(cache.texture(textures.find("Oreols/02.png"))),
      burstTexture_(cache.texture(textures.find("Oreols/11.png"))),
      dotTexture_(cache.texture(textures.find("Particles/ParticleTrail(orange).png"))),
      glowTexture_(cache.texture(textures.find("Particles/PARTICLE.PNG"))),
      ringTexture_(cache.texture(textures.find("Particles/Boom.png"))),
      smokeTexture_(cache.texture(textures.find("Particles/ParticleBubble.png"))),
      facing_(facingGameCamera()) {}

void ObjectEffects::update(const GameSession& session, std::span<const GameEvent> events, double seconds, LevelScene& level) {
    time_ += seconds;
    for (const GameEvent& event : events) {
        Flash flash;
        flash.at = event.position;
        flash.seed = seedOf(event.position, event.time);
        switch (event.type) {
            case EventType::Blast:
                flash.kind = Kind::Blast;
                flash.size = event.speed;
                break;
            case EventType::ShotHit: flash.kind = Kind::ShotBurst; break;
            case EventType::CannonFired:
                flash.kind = Kind::Muzzle;
                flash.direction = normalize(event.velocity);
                break;
            case EventType::RocketBurst: flash.kind = Kind::Burst; break;
            case EventType::BallEntered:
            case EventType::BallEmerged: flash.kind = Kind::Portal; break;
            case EventType::BrickHit:
                if (event.actor != kNoActor && session.scene().plan().actors[event.actor].type == ObjectType::Spring) {
                    std::erase_if(gives_, [&](const Give& give) { return give.actor == event.actor; });
                    gives_.push_back({event.actor, 0});
                }
                continue;
            default: continue;
        }
        flashes_.push_back(flash);
    }
    for (Flash& flash : flashes_) {
        flash.age += seconds;
    }
    std::erase_if(flashes_, [](const Flash& flash) {
        switch (flash.kind) {
            case Kind::Blast: return flash.age >= kBlastSeconds;
            case Kind::ShotBurst: return flash.age >= kShotBurstSeconds;
            case Kind::Muzzle: return flash.age >= kMuzzleSeconds;
            case Kind::Portal: return flash.age >= kPortalSeconds;
            case Kind::Burst: return flash.age >= kBurstSeconds;
        }
        return true;
    });
    std::erase_if(gives_, [&](Give& give) {
        give.age += seconds;
        const bool done = give.age >= kGiveSeconds || session.actor(give.actor).removed;
        const float u = static_cast<float>(give.age / kGiveSeconds);
        level.setScale(give.actor, done ? 1.0f : 1.0f + kGiveAmount * std::sin(2 * kPi * u) * (1 - u), session.centreOf(give.actor));
        return done;
    });

    items_.clear();
    const SpecialObjects& objects = session.objects();
    for (const SpecialObjects::Shot& shot : objects.shots()) {
        addMesh(shot_, Mat4::translation(shot.position));
        addShadow({shot.position.x, 0, shot.position.z}, 2.0f);
    }
    for (const SpecialObjects::Rocket& rocket : objects.rockets()) {
        addMesh(rocket_, Mat4::translation(rocket.position));
        addShadow({rocket.position.x, 0, rocket.position.z}, 2.0f);
        for (int i = 1; i <= kTrailSprites; ++i) {
            const float u = static_cast<float>(i) / (kTrailSprites + 1);
            addBillboard(rocket.position - Vec3{0, kTrailSpacing * static_cast<float>(i), 0}, kTrailSize * (1 - 0.6f * u),
                         {1, 0.7f, 0.3f, 0.8f * (1 - u)}, dotTexture_, BlendMode::Additive);
        }
    }
    for (const ActorIndex actor : objects.flickers()) {
        if (session.actor(actor).removed) {
            continue;
        }
        // The fire's light wavers: two sines of unrelated rates, each flicker with its own phase.
        const auto phase = static_cast<float>(actor);
        const auto t = static_cast<float>(time_);
        const float wave = 0.5f + 0.3f * std::sin(11.0f * t + phase) + 0.2f * std::sin(23.7f * t + 2.1f * phase);
        addBillboard(session.centreOf(actor) + Vec3{0, kGlowHeight, 0}, kGlowSize * (0.85f + 0.3f * wave),
                     {1.0f, 0.55f, 0.15f, 0.25f + 0.3f * wave}, glowTexture_, BlendMode::Additive);
    }
    for (const Flash& flash : flashes_) {
        drawFlash(flash);
    }
}

void ObjectEffects::drawFlash(const Flash& flash) {
    switch (flash.kind) {
        case Kind::Blast: drawBlast(flash); break;
        case Kind::ShotBurst: {
            const float fade = 1 - static_cast<float>(flash.age / kShotBurstSeconds);
            addBillboard(flash.at, kShotBurstSize * (0.5f + 0.5f * fade), {1, 1, 1, fade}, burstTexture_, BlendMode::Additive);
            break;
        }
        case Kind::Muzzle: {
            const float u = static_cast<float>(flash.age / kMuzzleSeconds);
            const float flare = std::max(0.0f, 1 - 4 * u);  // the flash is over in an eighth of a second
            if (flare > 0) {
                addBillboard(flash.at + flash.direction * 0.5f, kMuzzleFlashSize * (0.6f + 0.4f * flare), {1, 1, 1, flare},
                             burstTexture_, BlendMode::Additive);
            }
            // The smoke drifts on the way the ball went and thins.
            addBillboard(flash.at + flash.direction * (1.0f + 2.5f * u), 1.5f + 2.5f * u, {0.8f, 0.8f, 0.8f, 0.55f * (1 - u)},
                         smokeTexture_, BlendMode::AlphaBlend);
            break;
        }
        case Kind::Portal: {
            const float u = static_cast<float>(flash.age / kPortalSeconds);
            addFloor({flash.at.x, kPortalFloor, flash.at.z}, kPortalRingSize * (0.25f + 0.75f * u), {0.6f, 0.8f, 1.0f, 1 - u}, ringTexture_, BlendMode::Additive);
            addBillboard(flash.at + Vec3{0, 0.5f, 0}, kPortalGlowSize * (1 - 0.5f * u), {0.6f, 0.8f, 1.0f, 0.9f * (1 - u)},
                         glowTexture_, BlendMode::Additive);
            break;
        }
        case Kind::Burst: drawBurst(flash); break;
    }
}

// A swelling flash the size of the blast and sparks thrown up and out.
void ObjectEffects::drawBlast(const Flash& flash) {
    const float fade = 1 - static_cast<float>(flash.age / kBlastSeconds);
    const float grow = static_cast<float>(std::min(1.0, flash.age / kBlastGrowSeconds));
    addBillboard(flash.at, kBlastSizePerRadius * flash.size * (0.3f + 0.7f * grow), {1, 1, 1, fade * std::sqrt(fade)}, blastTexture_,
                 BlendMode::Additive);
    const auto t = static_cast<float>(flash.age);
    for (int i = 0; i < kBlastSparks; ++i) {
        const auto n = static_cast<std::uint32_t>(i);
        const float angle = 2 * kPi * hash01(flash.seed, n * 3);
        const float out = (0.8f + 1.2f * hash01(flash.seed, n * 3 + 1)) * flash.size;  // units a second
        const float up = (0.6f + 1.0f * hash01(flash.seed, n * 3 + 2)) * flash.size;
        Vec3 at = flash.at + Vec3{std::cos(angle) * out * t, up * t - 0.5f * kSparkGravity * t * t, std::sin(angle) * out * t};
        at.y = std::max(at.y, 0.2f);
        addBillboard(at, 1.6f * fade + 0.3f, {1.0f, 0.5f, 0.1f, fade}, glowTexture_, BlendMode::Additive);
    }
}

// Thirty sparks thrown from a point in every direction, slowing as they go, in one of the six pairs of colours.
void ObjectEffects::drawBurst(const Flash& flash) {
    const auto& colours = kBurstColours[flash.seed % kBurstColours.size()];
    const float life = static_cast<float>(kBurstSeconds);
    const float t = static_cast<float>(flash.age);
    const float u = t / life;
    for (int i = 0; i < kBurstParticles; ++i) {
        const auto n = static_cast<std::uint32_t>(i);
        // A direction spread evenly over the sphere: z from -1 to 1, the angle round it from a hash.
        const float z = 2 * hash01(flash.seed, n * 3) - 1;
        const float angle = 2 * kPi * hash01(flash.seed, n * 3 + 1);
        const float ring = std::sqrt(1 - z * z);
        const float speed = kBurstSpeed * (0.5f + 0.5f * hash01(flash.seed, n * 3 + 2));
        const float reach = speed * t * (1 - 0.5f * u);
        const Vec3 at = flash.at + Vec3{std::cos(angle) * ring, z, std::sin(angle) * ring} * reach;
        const Color tint = mix(argb(colours[0], 1), argb(colours[1], 1), u);
        addBillboard(at, kBurstSize * (1 - 0.5f * u), {tint.r, tint.g, tint.b, 1 - u}, dotTexture_, BlendMode::Additive);
    }
}

void ObjectEffects::addMesh(const LevelScene& model, const Mat4& place) {
    for (DrawItem item : model.items()) {
        item.world = item.world * place;
        items_.push_back(item);
    }
}

void ObjectEffects::addShadow(Vec3 under, float size) {
    addFloor({under.x, kShadowLift, under.z}, size, {0, 0, 0, kShadowAlpha}, glowTexture_, BlendMode::AlphaBlend);
}

void ObjectEffects::addBillboard(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend) {
    DrawItem item;
    item.mesh = quad_;
    item.indexCount = 6;
    item.world = Mat4::scaling({size, size, 1}) * facing_ * Mat4::translation(at);
    MaterialParams& m = item.material;
    m.diffuse = tint;
    m.texture = texture;
    m.blend = blend;
    m.cull = CullMode::None;
    m.lit = false;
    m.depthWrite = false;
    m.depthTest = false;
    items_.push_back(item);
}

// A quad lying flat at `at`, as the balls' shadows do.
void ObjectEffects::addFloor(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend) {
    DrawItem item;
    item.mesh = quad_;
    item.indexCount = 6;
    item.world = Mat4::scaling({size, size, 1}) * Mat4::rotationX(kPi / 2) * Mat4::translation(at);
    MaterialParams& m = item.material;
    m.diffuse = tint;
    m.texture = texture;
    m.blend = blend;
    m.cull = CullMode::None;
    m.lit = false;
    m.depthWrite = false;
    items_.push_back(item);
}

}  // namespace mb
