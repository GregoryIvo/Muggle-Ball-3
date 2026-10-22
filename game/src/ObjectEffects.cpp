#include "mb/game/ObjectEffects.h"

#include "mb/game/Billboard.h"
#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

// ---- How the effects look. The particle files (Data/Particles/*.xml [D]) say what they do; what is not a particle file is the sizes
// and times of the footage [M] or a guess [I].

// The charge of dynamite names its explosion (`explosebig` for radius 5, `explosesmall` for 3 [D]); a level that does not
// (a made-up one) gets the one that its radius fits.
constexpr float kBigBlastRadius = 4.0f;
constexpr std::string_view kBigBlast = "explosebig", kSmallBlast = "explosesmall";

// A cannonball bursting: exploseyadro.xml, a flash of size 10 that fades in 500 ms [D]; the footage's is white and then orange,
// about 3 units wide for the bat's cannon (F48, "Weapons") [M].
constexpr std::string_view kShotBurst = "exploseyadro";
// The puff at a cannon's muzzle: a flare for a short while and Smoke.xml's smoke along the barrel for a moment [I].
constexpr double kMuzzleSeconds = 0.5;
constexpr float kMuzzleFlashSize = 3.5f;
constexpr std::string_view kMuzzleSmoke = "Smoke";
constexpr float kMuzzleSmokeSeconds = 0.15f;

// A ball in a teleport: a ring on the floor and a glow where it went in or came out [I].
constexpr double kPortalSeconds = 0.35;
constexpr float kPortalRingSize = 6.0f;
constexpr float kPortalGlowSize = 4.0f;
constexpr float kPortalFloor = 0.05f;

// A firework rocket: its trail is Trail.xml's TrailOrange (the one with a scatter and the colours of a flame; TrailBlue is for the
// bat's rocket [I]), put out from the rocket's tail; its burst is one of the six of Fireworks.xml, chosen by where and when it
// went off [D, I].
constexpr std::string_view kRocketTrail = "Trail/TrailOrange";
constexpr std::string_view kBurstFile = "Fireworks";

// A spring that is hit gives: it swells and shrinks back [I].
constexpr double kGiveSeconds = 0.25;
constexpr float kGiveAmount = 0.2f;

// A tree that is hit shakes its leaves down, as LeafFall.xml says [D: the file is named by nothing; the trees are its
// inferred users]. The crown is above the trunk's centre of mass: the trunk is a capsule 10 units tall, the foliage round its top [I].
constexpr std::string_view kLeaves = "LeafFall";
constexpr float kCrownLift = 3.0f;

// The glow of a flicker's fire, above the cauldron's middle [I; the footage's light changes by 1.4 %, F13].
constexpr float kGlowHeight = 0.9f;
constexpr float kGlowSize = 4.5f;

constexpr float kShadowAlpha = 0.85f;
constexpr float kShadowLift = 0.04f;  // above the floor, which is at y = 0 in every level

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

std::uint32_t seedOf(Vec3 at, double time) {
    return static_cast<std::uint32_t>(std::fabs(at.x) * 977.0f + std::fabs(at.z) * 1597.0f) +
           static_cast<std::uint32_t>(time * 1000.0);
}

}  // namespace

Vec3 shotPosition(const SpecialObjects::Shot& shot, float lead) {
    return shot.position + shot.velocity * lead;
}

Vec3 rocketPosition(const SpecialObjects::Rocket& rocket, float rocketSpeed, float lead) {
    return rocket.position + Vec3{0, rocketSpeed * lead, 0};
}

ObjectEffects::ObjectEffects(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level,
                             ParticleSystem& particles)
    : rocket_(cache, materials, textures, modelLevel(ObjectType::FireRocket, "Fire-rocket.x", "Fire-rocket"), PhysicsPlan{}),
      shot_(cache, materials, textures, modelLevel(ObjectType::Bullet, "Yadro.x", "Telega1.jpg"), PhysicsPlan{}),
      particles_(particles),
      quad_(cache.quad()),
      burstTexture_(cache.texture(textures.find("Oreols/11.png"))),
      glowTexture_(cache.texture(textures.find("Particles/PARTICLE.PNG"))),
      ringTexture_(cache.texture(textures.find("Particles/Boom.png"))),
      facing_(facingGameCamera()) {
    const std::vector<ActorRef> actors = flattenActors(level);
    blastTargets_.resize(actors.size());
    fireNames_.resize(actors.size());
    for (const ActorRef& ref : actors) {
        if (ref.owner == nullptr) {
            continue;
        }
        if (ref.owner->type == ObjectType::Dynamite) {
            blastTargets_[ref.index] = ref.owner->target;
        } else if (ref.owner->type == ObjectType::Flicker) {
            fireNames_[ref.index] = ref.owner->names;
        }
    }
}

std::string_view ObjectEffects::blastEffect(const GameEvent& event) const {
    if (event.actor < blastTargets_.size() && particles_.library().find(blastTargets_[event.actor]) != nullptr) {
        return blastTargets_[event.actor];
    }
    return event.speed >= kBigBlastRadius ? kBigBlast : kSmallBlast;
}

std::string ObjectEffects::burstEffect(const GameEvent& event) const {
    const std::span<const ParticleDef> variants = particles_.library().file(kBurstFile);
    if (variants.empty()) {
        return {};
    }
    const ParticleDef& chosen = variants[seedOf(event.position, event.time) % variants.size()];
    return chosen.file + "/" + chosen.name;
}

void ObjectEffects::update(const GameSession& session, std::span<const GameEvent> events, double seconds, LevelScene& level,
                           double lead) {
    time_ += seconds;
    for (const GameEvent& event : events) {
        switch (event.type) {
            case EventType::DynamiteBlast: particles_.spawn(blastEffect(event), event.position); break;
            case EventType::ShotHit: particles_.spawn(kShotBurst, event.position); break;
            case EventType::CannonFired: {
                const Vec3 way = normalize(event.velocity);
                flashes_.push_back({Kind::Muzzle, event.position, way, 0});
                particles_.spawn(kMuzzleSmoke, event.position + way, {.direction = way, .seconds = kMuzzleSmokeSeconds});
                break;
            }
            case EventType::RocketBurst:
                particles_.spawn(burstEffect(event), event.position);
                break;
            case EventType::BallEntered:
            case EventType::BallEmerged: flashes_.push_back({Kind::Portal, event.position, {}, 0}); break;
            case EventType::BrickHit: {
                if (event.actor == kNoActor) {
                    break;
                }
                const ObjectType type = session.scene().plan().actors[event.actor].type;
                if (type == ObjectType::Spring) {
                    std::erase_if(gives_, [&](const Give& give) { return give.actor == event.actor; });
                    gives_.push_back({event.actor, 0});
                } else if (type == ObjectType::Tree) {
                    particles_.spawn(kLeaves, session.centreOf(event.actor) + Vec3{0, kCrownLift, 0});
                }
                break;
            }
            default: break;
        }
    }
    for (Flash& flash : flashes_) {
        flash.age += seconds;
    }
    std::erase_if(flashes_, [](const Flash& flash) { return flash.age >= (flash.kind == Kind::Muzzle ? kMuzzleSeconds : kPortalSeconds); });
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
        const Vec3 at = shotPosition(shot, static_cast<float>(lead));
        addMesh(shot_, Mat4::translation(at));
        addShadow({at.x, 0, at.z}, 2.0f);
    }
    for (const SpecialObjects::Rocket& rocket : objects.rockets()) {
        const Vec3 at = rocketPosition(rocket, session.rules().objects.rocketSpeed, static_cast<float>(lead));
        addMesh(rocket_, Mat4::translation(at));
        addShadow({at.x, 0, at.z}, 2.0f);
    }
    followRockets(objects, session.rules().objects.rocketSpeed, static_cast<float>(lead));
    followFires(session);
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

// Each rocket in the air leaves a trail from its tail; the trail stops when the rocket is gone, and what it left fades out.
void ObjectEffects::followRockets(const SpecialObjects& objects, float rocketSpeed, float lead) {
    for (const SpecialObjects::Rocket& rocket : objects.rockets()) {
        const Vec3 at = rocketPosition(rocket, rocketSpeed, lead);  // where the rocket is drawn
        EffectId& trail = trails_[rocket.crate];
        if (!particles_.alive(trail)) {
            trail = particles_.attach(kRocketTrail, at);
        }
        particles_.moveTo(trail, at);
    }
    std::erase_if(trails_, [&](const auto& entry) {
        const bool flying = std::ranges::any_of(objects.rockets(), [&](const SpecialObjects::Rocket& rocket) { return rocket.crate == entry.first; });
        if (!flying) {
            particles_.stop(entry.second);
        }
        return !flying;
    });
}

// A flicker burns its two particle files (the sparks and the smoke) from the top of its box; they go when it does.
void ObjectEffects::followFires(const GameSession& session) {
    for (const ActorIndex actor : session.objects().flickers()) {
        const auto fire = fires_.find(actor);
        if (session.actor(actor).removed) {
            if (fire != fires_.end()) {
                for (const EffectId effect : fire->second.effects) {
                    particles_.stop(effect);
                }
                fires_.erase(fire);
            }
            continue;
        }
        if (fire != fires_.end()) {
            for (const EffectId effect : fire->second.effects) {
                particles_.moveTo(effect, session.centreOf(actor) + Vec3{0, fire->second.lift, 0});
            }
            continue;
        }
        const ActorPlan& plan = session.scene().plan().actors[actor];
        const Vec3 centre = session.centreOf(actor);
        Fire made;
        made.lift = std::max(boundsOf(plan.body, plan.pose).high.y - centre.y, 0.0f);
        for (const std::string& name : fireNames_[actor]) {
            if (const EffectId effect = particles_.attach(name, centre + Vec3{0, made.lift, 0}); effect != kNoEffect) {
                made.effects.push_back(effect);
            }
        }
        fires_.emplace(actor, std::move(made));
    }
}

void ObjectEffects::drawFlash(const Flash& flash) {
    switch (flash.kind) {
        case Kind::Muzzle: {
            const float u = static_cast<float>(flash.age / kMuzzleSeconds);
            const float flare = std::max(0.0f, 1 - 4 * u);  // the flash is over in an eighth of a second
            if (flare > 0) {
                addBillboard(flash.at + flash.direction * 0.5f, kMuzzleFlashSize * (0.6f + 0.4f * flare), {1, 1, 1, flare},
                             burstTexture_, BlendMode::Additive);
            }
            break;
        }
        case Kind::Portal: {
            const float u = static_cast<float>(flash.age / kPortalSeconds);
            addFloor({flash.at.x, kPortalFloor, flash.at.z}, kPortalRingSize * (0.25f + 0.75f * u), {0.6f, 0.8f, 1.0f, 1 - u}, ringTexture_, BlendMode::Additive);
            addBillboard(flash.at + Vec3{0, 0.5f, 0}, kPortalGlowSize * (1 - 0.5f * u), {0.6f, 0.8f, 1.0f, 0.9f * (1 - u)},
                         glowTexture_, BlendMode::Additive);
            break;
        }
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
    items_.push_back(billboard(quad_, facing_, at, {size, size}, tint, texture, blend));
}

// A quad lying flat at `at`, as the balls' shadows do.
void ObjectEffects::addFloor(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend) {
    items_.push_back(floorMark(quad_, at, size, tint, texture, blend));
}

}  // namespace mb
