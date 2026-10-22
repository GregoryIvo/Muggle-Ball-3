#include "mb/game/WeaponScene.h"

#include "mb/engine/Log.h"
#include "mb/game/Billboard.h"

#include <cmath>

namespace mb {

namespace {

// The models of what flies are bat-bullets.x (a frame for each barrel of the gun), kernel.x, bat-rocket.x and meteor.x, each with the
// game material its object names (Data/Objects/Bullets, Interactive/meteor.XML [D]) and one animation, "Anim-1", a turn about one axis
// that the original loops (the bullets about their own length, the rocket about its length, the cannonball and the meteor tumble) [D].
struct Prop {
    const char* mesh;
    const char* frame;
    const char* material;
    float shadow;  // across, on the water below it; 0: none [I]
};
constexpr Prop kProps[] = {
    {"Bat-Bullets.x", "Left-Bullet", "bat_bullet", 0},  // a bullet is small and quick: the footage shows no shadow under it [M]
    {"Bat-Bullets.x", "Right-Bullet", "bat_bullet", 0},
    {"Kernel.x", "", "bat_kernel", 2.0f},
    {"Bat-Rocket.x", "", "bat_rocket", 2.0f},
    {"Meteor.x", "", "meteor", 2.0f},
};
constexpr const char* kSpin = "Anim-1";

// The shadow: a soft dark blob on the water below, as the balls have (the same texture and strength).
constexpr float kShadowAlpha = 0.85f;
constexpr float kShadowLift = 0.04f;

Level propsLevel() {
    Level level;
    for (const Prop& prop : kProps) {
        GameObject object;
        object.type = ObjectType::Bullet;
        object.mesh = prop.mesh;
        object.frame = prop.frame;
        object.material = prop.material;
        Actor actor;
        actor.object = object;
        level.actors.push_back(actor);
    }
    return level;
}

}  // namespace

WeaponScene::WeaponScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, ParticleSystem& particles,
                         const WeaponRules& rules)
    : props_(cache, materials, textures, propsLevel(), PhysicsPlan{}),
      effects_(particles, rules),
      quad_(cache.quad()),
      shadowTexture_(cache.texture(textures.find("Particles/PARTICLE.PNG"))) {
    for (const std::string& name : props_.report().missingMeshes) {
        logWarn("weapons: missing mesh {}", name);
    }
    for (std::size_t i = 0; i < looks_.size(); ++i) {
        const ModelCache::Entry* entry = cache.model(meshFileName(kProps[i].mesh));
        if (entry == nullptr) {
            continue;
        }
        Look& look = looks_[i];
        look.actor = static_cast<ActorIndex>(i);
        look.model = &entry->model;
        look.spin = entry->model.findAnimation(kSpin);
        look.frame = kProps[i].frame[0] != '\0' ? entry->model.findNode(kProps[i].frame)
                                                : (entry->model.roots.empty() ? -1 : entry->model.roots.front());
        look.shadow = kProps[i].shadow;
    }
}

void WeaponScene::update(const GameSession& session, std::span<const GameEvent> events, double lead) {
    effects_.update(events, session.projectiles(), lead);
    items_.clear();
    const auto ahead = static_cast<float>(lead);
    for (const Projectile& projectile : session.projectiles()) {
        const Vec3 at = projectile.position + projectile.velocity * ahead;
        switch (projectile.kind) {
            case ProjectileKind::Bullet: add(looks_[projectile.barrel == 0 ? 0 : 1], at, projectile.age + lead); break;
            case ProjectileKind::Kernel: add(looks_[2], at, projectile.age + lead); break;
            case ProjectileKind::Rocket: add(looks_[3], at, projectile.age + lead); break;
            case ProjectileKind::Meteor: add(looks_[4], at, projectile.age + lead); break;
            case ProjectileKind::Bolt: break;  // a thunderbolt is seen only as its lightning, where it strikes
        }
    }
}

// The model with its frame's origin at `at`, as far through its turn as `age` seconds of looping it.
void WeaponScene::add(const Look& look, Vec3 at, double age) {
    if (look.model == nullptr || look.frame < 0) {
        return;
    }
    static const ModelAnimation kStill;
    const ModelAnimation& turn = look.spin != nullptr ? *look.spin : kStill;
    const float time = turn.duration > 0 ? static_cast<float>(std::fmod(age, static_cast<double>(turn.duration))) : 0.0f;
    const std::vector<Mat4> poses = look.model->globalPoses(turn, time);
    const Vec3 origin = poses[static_cast<std::size_t>(look.frame)].translationPart();
    const std::size_t first = items_.size();
    props_.appendActorPosed(look.actor, poses, items_);
    for (std::size_t i = first; i < items_.size(); ++i) {
        items_[i].world = items_[i].world * Mat4::translation(at - origin);
    }
    if (look.shadow > 0) {
        items_.push_back(floorMark(quad_, {at.x, kShadowLift, at.z}, look.shadow, {0, 0, 0, kShadowAlpha}, shadowTexture_, BlendMode::AlphaBlend));
    }
}

}  // namespace mb
