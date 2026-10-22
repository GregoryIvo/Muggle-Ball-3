#include "mb/game/Weapons.h"

#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mb {

namespace {

// The materials of the projectiles, by the names the data gives them (Data/Materials/materials.json): their `period` sound is the
// shot's and their `dead` sound the impact's. "meteor" names two materials, the projectile's (group temp) first and the prize's.
constexpr const char* kMaterialNames[] = {"bat_bullet", "bat_kernel", "bat_rocket", "thunderbolt", "meteor"};
constexpr const char* kLaserMaterial = "bat_lazer";

// A shot comes out of the bat's two barrels, the Machine Gun's pair of bullets.
constexpr int kBarrels = 2;

// The camera's way to the top edge of the picture. A point (P - eye) . w = 0 is on the plane through the eye that touches the top
// edge, w being the camera's up minus its forward times tan(fov / 2); the window's aspect does not enter.
struct TopEdge {
    Vec3 eye;
    Vec3 w;
};

TopEdge topEdge() {
    const Vec3 forward = normalize(kGameCameraTarget - kGameCameraEye);
    const Vec3 right = normalize(cross({0, 1, 0}, forward));
    const Vec3 up = cross(forward, right);
    return {kGameCameraEye, up - forward * std::tan(radians(kGameCameraFovDegrees) / 2)};
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

float screenTopHeight(Vec3 below) {
    const TopEdge edge = topEdge();
    return below.y - dot(below - edge.eye, edge.w) / edge.w.y;
}

Vec3 screenTopPoint(Vec3 target, Vec3 direction) {
    const TopEdge edge = topEdge();
    const float behind = dot(target - edge.eye, edge.w) / dot(direction, edge.w);
    return target - direction * behind;
}

Weapons::Weapons(const MaterialLibrary& materials, const WeaponRules& rules, std::uint64_t seed)
    : rules_(rules), random_(seed) {
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        materials_[i] = materials.find(kMaterialNames[i]);
    }
    laserMaterial_ = materials.find(kLaserMaterial);
}

const ProjectileRules& Weapons::rulesOf(ProjectileKind kind) const {
    switch (kind) {
        case ProjectileKind::Bullet: return rules_.bullet;
        case ProjectileKind::Kernel: return rules_.kernel;
        case ProjectileKind::Rocket: return rules_.rocket;
        case ProjectileKind::Bolt: return rules_.bolt;
        case ProjectileKind::Meteor: return rules_.meteor;
    }
    return rules_.bullet;
}

// ---- Firing

bool Weapons::fire(GameSession& session) {
    // One shot of what the bat holds, when the balls are in play and the bat is there: not while it swoops in with a ball on it (the click
    // launches that), after the last ball was lost or once the level is won.
    if (over_ || session.phase_ != SessionPhase::Playing || session.batBody_ == 0 || session.bat_.phase != BatPhase::Present ||
        session.time_ - lastShot_ < rules_.shotInterval - session.fixedStep_ * 1e-3) {
        return false;
    }
    const std::optional<WeaponState> armed = session.weapon();
    if (!armed) {
        return false;
    }
    const PrizeKind prize = prizeOf(armed->type);
    const auto place = [&](Vec3 offset) { return onBat(session.rules_.ballPlaneY, session.bat_.position, session.batScale_, offset); };

    GameEvent event;
    event.type = EventType::ShotFired;
    event.prize = prize;
    event.count = armed->shots - 1;
    int made = 0;
    switch (armed->type) {
        case WeaponType::Gun: {
            for (int barrel = 0; barrel < kBarrels; ++barrel) {
                Vec3 offset = rules_.bullet.spawn;
                offset.x = barrel == 0 ? -offset.x : offset.x;
                made += launch(session, ProjectileKind::Bullet, prize, place(offset), rules_.bullet.velocity, barrel) ? 1 : 0;
            }
            event.position = place({0, rules_.bullet.spawn.y, rules_.bullet.spawn.z});
            event.velocity = rules_.bullet.velocity;
            event.material = materialOf(ProjectileKind::Bullet);
            break;
        }
        case WeaponType::Cannon:
            made = launch(session, ProjectileKind::Kernel, prize, place(rules_.kernel.spawn), rules_.kernel.velocity) ? 1 : 0;
            event.position = place(rules_.kernel.spawn);
            event.velocity = rules_.kernel.velocity;
            event.material = materialOf(ProjectileKind::Kernel);
            break;
        case WeaponType::Bazooka:
            made = launch(session, ProjectileKind::Rocket, prize, place(rules_.rocket.spawn), rules_.rocket.velocity) ? 1 : 0;
            event.position = place(rules_.rocket.spawn);
            event.velocity = rules_.rocket.velocity;
            event.material = materialOf(ProjectileKind::Rocket);
            break;
        case WeaponType::Laser:
            made = 1;
            event.position = place(rules_.laserSpawn);
            event.material = laserMaterial_;
            break;
    }
    if (made == 0) {  // the world refused the bodies: the click is lost and no shot is spent
        return false;
    }
    lastShot_ = session.time_;
    ++stats_.shots;
    session.emit(event);
    if (armed->type == WeaponType::Laser) {
        fireLaser(session, event.position);
    }
    session.useShot();
    return true;
}

bool Weapons::launch(GameSession& session, ProjectileKind kind, PrizeKind prize, Vec3 position, Vec3 velocity, int barrel) {
    const ProjectileRules& spec = rulesOf(kind);
    PhysicsShape shape;
    shape.type = spec.halfHeight > 0 ? PhysicsShapeType::Capsule : PhysicsShapeType::Sphere;
    shape.radius = spec.radius;
    shape.halfHeight = spec.halfHeight;
    if (spec.halfHeight > 0) {
        shape.localPose = Mat4::rotationX(kPi / 2);  // a capsule's axis is its y: lay it along z, the way it flies
    }
    shape.material.staticFriction = shape.material.dynamicFriction = spec.friction;
    shape.material.restitution = spec.restitution;
    const BodyId body = session.scene_.addProjectile(Mat4::translation(position), shape, velocity, spec.gravity, spec.density);
    if (body == 0) {
        return false;
    }
    Projectile projectile;
    projectile.id = nextId_++;
    projectile.kind = kind;
    projectile.prize = prize;
    projectile.position = position;
    projectile.velocity = velocity;
    projectile.bornAt = session.time_;
    projectile.barrel = barrel;
    projectile.body = body;
    projectiles_.push_back(projectile);
    ++stats_.projectiles;
    return true;
}

// The beam is a line, resolved at the shot: it goes up the board at the height of the barrel, through everything it can hurt (one
// hit for each, whatever number of shapes it crosses), and stops at the first thing it cannot: a wall, the board, a solid
// object that nothing damages. The teleports are not there for it. Three rays make up its width.
void Weapons::fireLaser(GameSession& session, Vec3 from) {
    struct Reach {
        ActorIndex actor;
        float distance;
        Vec3 point;
    };
    std::vector<Reach> reached;
    const PhysicsWorld& world = session.scene_.world();
    const std::size_t actors = session.scene_.plan().actors.size();
    for (const float side : {-rules_.laserRadius, 0.0f, rules_.laserRadius}) {
        for (const RayHit& hit : world.raycastAll(from + Vec3{side, 0, 0}, {0, 0, 1}, rules_.laserRange)) {
            const std::uint64_t data = world.userData(hit.body);
            if (data >= actors) {  // the balls and the bat are no actors: the beam goes through them
                continue;
            }
            const auto actor = static_cast<ActorIndex>(data);
            const ObjectType type = session.scene_.plan().actors[actor].type;
            if (type == ObjectType::TeleportSrc || type == ObjectType::TeleportDest) {
                continue;
            }
            // What is already going (exploding, shrinking away) is hurt no more, and the beam goes through it as it does through the teleports.
            const GameSession::Brick& brick = session.bricks_[actor];
            if (brick.damageable && (brick.removed || brick.exploding || brick.shrinking)) {
                continue;
            }
            const auto seen = std::ranges::find(reached, actor, &Reach::actor);
            if (seen == reached.end()) {
                reached.push_back({actor, hit.distance, hit.point});
            } else if (hit.distance < seen->distance) {
                *seen = {actor, hit.distance, hit.point};
            }
        }
    }
    std::ranges::sort(reached, [](const Reach& a, const Reach& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.actor < b.actor;
    });

    Vec3 end = from + Vec3{0, 0, rules_.laserRange};
    std::size_t count = reached.size();
    for (std::size_t i = 0; i < reached.size(); ++i) {
        if (!session.bricks_[reached[i].actor].damageable) {  // it stops here: this is the last the beam reaches
            end = {from.x, from.y, reached[i].point.z};
            count = i + 1;
            break;
        }
    }
    LaserBeam beam;
    beam.from = from;
    beam.to = end;
    beam.firedAt = session.time_;
    beam.hits = static_cast<int>(count);
    beams_.push_back(beam);
    ++stats_.beams;
    stats_.beamReached += beam.hits;

    GameEvent event;
    event.type = EventType::LaserBeam;
    event.prize = PrizeKind::Laser;
    event.position = from;
    event.end = end;
    event.count = beam.hits;
    event.material = laserMaterial_;
    session.emit(event);

    for (std::size_t i = 0; i < count; ++i) {
        GameEvent hit;
        hit.type = EventType::ProjectileHit;
        hit.prize = PrizeKind::Laser;
        hit.actor = reached[i].actor;
        hit.position = reached[i].point;
        hit.material = laserMaterial_;
        session.emit(hit);
        if (session.bricks_[reached[i].actor].damageable) {
            hurt(session, reached[i].actor, reached[i].point, rules_.laserDamage, true, PrizeKind::Laser);
        }
    }
}

// ---- Strikes

void Weapons::startStrike(GameSession& session, PrizeKind kind) {
    if (over_) {
        return;
    }
    const double now = session.time_;
    if (kind == PrizeKind::Thunderbolt) {
        const int span = rules_.boltBurstMost - rules_.boltBurstLeast + 1;
        const int burst = rules_.boltBurstLeast + std::min(span - 1, static_cast<int>(random_.uniform() * span));
        const int number = nextBurst_++;
        for (int i = 0; i < burst; ++i) {
            pending_.push_back({now + i * rules_.boltGap, kind, number});
        }
        for (int i = 0; i < rules_.boltsLater; ++i) {
            const double later = rules_.boltLaterFrom + random_.uniform() * (rules_.boltLaterUntil - rules_.boltLaterFrom);
            pending_.push_back({now + later, kind, -1});
        }
    } else if (kind == PrizeKind::Meteor && rules_.meteors > 0) {
        const double slot = rules_.meteorSpan / rules_.meteors;
        for (int i = 0; i < rules_.meteors; ++i) {
            pending_.push_back({now + (i + random_.uniform()) * slot, kind, -1});
        }
    }
}

// A bolt falls on a standing brick or tree picked at random (nothing else is a target of the level, and metal is spared: the footage's
// bolts left the Metall wall of level 31 alone), straight down from the top edge of the screen above it. The bolts of a burst strike
// different places [M], so one that a bolt of its burst struck a moment ago is left out, unless it is all that stands.
void Weapons::throwBolt(GameSession& session, int burst) {
    candidates_.clear();
    for (ActorIndex actor = 0; actor < session.bricks_.size(); ++actor) {
        const GameSession::Brick& brick = session.bricks_[actor];
        if ((brick.type == ObjectType::Brick || brick.type == ObjectType::Tree) && brick.damageable && !brick.removed &&
            !brick.exploding && !brick.shrinking) {
            candidates_.push_back(actor);
        }
    }
    // The ones to pick among come first: those the burst has not struck, or all of them if it has struck every one.
    std::size_t pool = candidates_.size();
    if (burst >= 0) {
        const auto fresh = std::stable_partition(candidates_.begin(), candidates_.end(), [&](ActorIndex actor) {
            return std::ranges::find(burstTargets_, std::pair{burst, actor}) == burstTargets_.end();
        });
        pool = fresh != candidates_.begin() ? static_cast<std::size_t>(fresh - candidates_.begin()) : candidates_.size();
    }
    Vec3 target;
    if (pool == 0) {  // nothing to strike: the board, anywhere in the meteors' field
        target = {random_.between(-rules_.meteorX, rules_.meteorX), 0, random_.between(rules_.meteorZLow, rules_.meteorZHigh)};
    } else {
        const auto pick = std::min(pool - 1, static_cast<std::size_t>(random_.uniform() * static_cast<double>(pool)));
        target = session.centreOf(candidates_[pick]);
        if (burst >= 0) {
            burstTargets_.emplace_back(burst, candidates_[pick]);
        }
    }
    const Vec3 from{target.x, screenTopHeight(target) + rules_.offscreen, target.z};
    if (launch(session, ProjectileKind::Bolt, PrizeKind::Thunderbolt, from, rules_.bolt.velocity)) {
        ++stats_.bolts;
    }
}

// A meteor comes down along its own slant on a point of the board, from the top edge of the screen. The point is where its sphere touches
// the water: the line it flies along goes through the point one radius up.
void Weapons::throwMeteor(GameSession& session) {
    const Vec3 aim{random_.between(-rules_.meteorX, rules_.meteorX), 0, random_.between(rules_.meteorZLow, rules_.meteorZHigh)};
    const Vec3 velocity = rules_.meteor.velocity;
    const Vec3 from = screenTopPoint(aim + Vec3{0, rules_.meteor.radius, 0}, velocity) - normalize(velocity) * rules_.offscreen;
    if (!launch(session, ProjectileKind::Meteor, PrizeKind::Meteor, from, velocity)) {
        return;
    }
    ++stats_.meteors;
    GameEvent event;
    event.type = EventType::MeteorFalling;
    event.prize = PrizeKind::Meteor;
    event.position = from;
    event.velocity = velocity;
    event.end = aim;
    event.material = materialOf(ProjectileKind::Meteor);
    session.emit(event);
}

// ---- Each fixed step

void Weapons::update(GameSession& session) {
    if (session.phase_ == SessionPhase::Complete || session.phase_ == SessionPhase::GameOver) {
        clearAll(session);
        return;
    }
    const double epsilon = session.fixedStep_ * 1e-3;
    for (std::size_t i = 0; i < pending_.size();) {
        if (pending_[i].at > session.time_ + epsilon) {
            ++i;
            continue;
        }
        const Strike strike = pending_[i];
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        if (strike.kind == PrizeKind::Thunderbolt) {
            throwBolt(session, strike.burst);
        } else {
            throwMeteor(session);
        }
    }
    std::erase_if(burstTargets_, [&](const std::pair<int, ActorIndex>& target) {  // a burst that is all thrown is forgotten
        return std::ranges::none_of(pending_, [&](const Strike& strike) { return strike.burst == target.first; });
    });

    // A projectile's first touch of an actor is its end: it is taken out of the world so that the rest of its contacts of the step
    // find nobody.
    const std::size_t actors = session.scene_.plan().actors.size();
    for (const ContactEvent& contact : session.scene_.contacts()) {
        const bool first = contact.userDataA == kProjectileData, second = contact.userDataB == kProjectileData;
        if (first == second) {
            continue;
        }
        const std::uint64_t other = first ? contact.userDataB : contact.userDataA;
        const auto it = std::ranges::find(projectiles_, first ? contact.a : contact.b, &Projectile::body);
        if (it == projectiles_.end() || other >= actors) {
            continue;
        }
        land(session, static_cast<std::size_t>(it - projectiles_.begin()), static_cast<ActorIndex>(other), contact.point);
    }

    const PhysicsWorld& world = session.scene_.world();
    for (std::size_t i = 0; i < projectiles_.size();) {
        Projectile& projectile = projectiles_[i];
        projectile.position = world.pose(projectile.body).translationPart();
        projectile.velocity = world.linearVelocity(projectile.body);
        projectile.age = session.time_ - projectile.bornAt;
        const bool gone = !finite(projectile.position) || std::fabs(projectile.position.x) > rules_.farLimit ||
                          std::fabs(projectile.position.z) > rules_.farLimit || projectile.position.y < rules_.lowLimit ||
                          projectile.age > rules_.projectileLife;
        if (!gone) {
            ++i;
            continue;
        }
        GameEvent event;
        event.type = EventType::ProjectileGone;
        event.prize = projectile.prize;
        event.position = projectile.position;
        event.velocity = projectile.velocity;
        event.material = materialOf(projectile.kind);
        session.emit(event);
        session.scene_.removeProjectile(projectile.body);
        projectiles_.erase(projectiles_.begin() + static_cast<std::ptrdiff_t>(i));
        ++stats_.gone;
    }
    std::erase_if(beams_, [&](const LaserBeam& beam) { return session.time_ - beam.firedAt >= rules_.laserLife; });
}

void Weapons::land(GameSession& session, std::size_t index, ActorIndex actor, Vec3 at) {
    const Projectile projectile = projectiles_[index];
    const ProjectileRules& spec = rulesOf(projectile.kind);
    session.scene_.removeProjectile(projectile.body);
    projectiles_.erase(projectiles_.begin() + static_cast<std::ptrdiff_t>(index));
    ++stats_.hits;

    GameEvent event;
    event.type = projectile.kind == ProjectileKind::Bolt ? EventType::BoltStruck
                 : projectile.kind == ProjectileKind::Meteor ? EventType::MeteorImpact
                                                             : EventType::ProjectileHit;
    event.prize = projectile.prize;
    event.actor = actor;
    event.position = at;
    event.velocity = projectile.velocity;
    if (projectile.kind == ProjectileKind::Bolt) {
        event.end = {at.x, screenTopHeight(at), at.z};  // the lightning is drawn from here to the target
    }
    event.material = materialOf(projectile.kind);
    session.emit(event);

    hurt(session, actor, at, spec.damage, spec.dropsPrize, projectile.prize);
    if (spec.blastRadius > 0) {
        session.objects_.burst(session, actor, at, spec.blastRadius, spec.blastPower, spec.blastDamage, spec.dropsPrize, projectile.prize);
        GameEvent burst;
        burst.type = EventType::ProjectileExploded;
        burst.prize = projectile.prize;
        burst.position = at;
        burst.speed = spec.blastRadius;
        burst.material = materialOf(projectile.kind);
        session.emit(burst);
    }
}

bool Weapons::hurt(GameSession& session, ActorIndex actor, Vec3 at, int damage, bool dropsPrize, PrizeKind by) {
    const bool done = session.objects_.weaponHit(session, actor, at, damage, dropsPrize, by);
    stats_.hurt += done ? 1 : 0;
    return done;
}

// The level is won or the game is over: what is in the air is taken out, and what was to come does not.
void Weapons::clearAll(GameSession& session) {
    over_ = true;
    burstTargets_.clear();
    for (const Projectile& projectile : projectiles_) {
        GameEvent event;
        event.type = EventType::ProjectileGone;
        event.prize = projectile.prize;
        event.position = projectile.position;
        event.velocity = projectile.velocity;
        event.material = materialOf(projectile.kind);
        session.emit(event);
        session.scene_.removeProjectile(projectile.body);
        ++stats_.gone;
    }
    projectiles_.clear();
    pending_.clear();
    beams_.clear();
}

}  // namespace mb
