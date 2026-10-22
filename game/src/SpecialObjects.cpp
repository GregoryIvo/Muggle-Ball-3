#include "mb/game/SpecialObjects.h"

#include "mb/game/Gameplay.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mb {

namespace {

// A cannonball does not strike the cannon that fired it while it is still leaving the muzzle.
constexpr double kShooterGrace = 0.1;
// A cannonball that has gone this far from the board (or this far below it) is gone.
constexpr float kShotLimit = 80.0f;
constexpr float kShotFloor = -20.0f;
// A blast radius outside this is not one the level means.
constexpr float kLeastRadius = 0.5f, kMostRadius = 30.0f;

// The distance from a point to a box, zero inside.
float distanceTo(const Bounds& box, Vec3 p) {
    const float dx = std::max({box.low.x - p.x, 0.0f, p.x - box.high.x});
    const float dy = std::max({box.low.y - p.y, 0.0f, p.y - box.high.y});
    const float dz = std::max({box.low.z - p.z, 0.0f, p.z - box.high.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 centreOfBox(const Bounds& box) {
    return (box.low + box.high) * 0.5f;
}

bool inside(Vec3 p, Vec3 half) {
    return std::fabs(p.x) <= half.x && std::fabs(p.y) <= half.y && std::fabs(p.z) <= half.z;
}

}  // namespace

SpecialObjects::SpecialObjects(const Level& level, const PhysicsPlan& plan, const SpecialObjectRules& rules)
    : rules_(rules),
      kinds_(plan.actors.size(), Kind::None),
      reach_(plan.actors.size(), 0.0f),
      staticBounds_(plan.actors.size()),
      random_(rules.seed) {
    const std::vector<ActorRef> refs = flattenActors(level);
    for (const ActorRef& ref : refs) {
        const ActorPlan& actor = plan.actors[ref.index];
        if (!actor.simulated) {
            continue;
        }
        const Bounds local = boundsOf(actor.body, Mat4::identity());
        reach_[ref.index] = length({std::max(std::fabs(local.low.x), std::fabs(local.high.x)),
                                    std::max(std::fabs(local.low.y), std::fabs(local.high.y)),
                                    std::max(std::fabs(local.low.z), std::fabs(local.high.z))});
        if (actor.motion == MotionType::Static) {
            staticBounds_[ref.index] = boundsOf(actor.body, actor.pose);
        }
        if (ref.owner == nullptr) {
            continue;
        }
        Kind& kind = kinds_[ref.index];
        switch (ref.owner->type) {
            case ObjectType::Dynamite: {
                kind = Kind::Dynamite;
                const float radius = std::bit_cast<float>(ref.owner->param);
                charges_.push_back({ref.index, radius >= kLeastRadius && radius <= kMostRadius ? radius : rules_.defaultBlastRadius});
                break;
            }
            case ObjectType::Cannon:
                kind = Kind::Cannon;
                cannons_.push_back({ref.index});
                break;
            case ObjectType::Firework:
                kind = Kind::Firework;
                crates_.push_back({ref.index});
                break;
            case ObjectType::Spring: kind = Kind::Spring; break;
            case ObjectType::Volcano:
            case ObjectType::GeneratorGood:
            case ObjectType::GeneratorBad:
            case ObjectType::Metall: kind = Kind::Inert; break;
            case ObjectType::Vibro:  // (all 23 are kinematic [D]: a body that is not cannot be moved to a pose)
                kind = Kind::Vibro;
                if (actor.motion == MotionType::Kinematic) {
                    vibros_.push_back(ref.index);
                }
                break;
            case ObjectType::Flicker: flickers_.push_back(ref.index); break;
            case ObjectType::TeleportSrc:
            case ObjectType::TeleportDest: {
                const bool source = ref.owner->type == ObjectType::TeleportSrc;
                Portal portal;
                portal.actor = ref.index;
                portal.centre = transformPoint(actor.body.massLocalPose.translationPart(), actor.pose);
                for (const ZoneBox& zone : plan.zones) {
                    if (zone.actor == ref.index && zone.trigger) {  // the source's centre box
                        portal.toBox = inverse(zone.local * actor.pose);
                        portal.half = zone.halfExtents;
                        break;
                    }
                }
                (source ? sources_ : exits_).push_back(portal);
                break;
            }
            default: break;
        }
    }
    // A source with no trigger box cannot swallow anything.
    std::erase_if(sources_, [](const Portal& portal) { return !(portal.half.x > 0); });
}

float SpecialObjects::random01() {
    // SplitMix32: a number in [0, 1) that repeats from the seed.
    random_ += 0x9E3779B9u;
    std::uint32_t z = random_;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    return static_cast<float>(z >> 8) * (1.0f / 16777216.0f);
}

// ---- Hooks

bool SpecialObjects::ballHit(GameSession& session, ActorIndex actor, const ContactEvent& contact, int ball) {
    const Kind kind = kindOf(actor);
    if (kind != Kind::Cannon && kind != Kind::Spring && kind != Kind::Inert) {
        return false;  // the ball hurts it as it hurts a brick (a firework crate too: its touch is touches()'s, not this)
    }
    if (kind == Kind::Cannon) {  // (a touch sets them off, a blow or not: touches())
        return true;
    }
    if (kind == Kind::Spring) {
        kick(session, actor, ball);
    }
    // The ball hurts none of these, but it hit them: they make their sound.
    const GameSession::Brick& brick = session.bricks_[actor];
    GameEvent event;
    event.type = EventType::BrickHit;
    event.ball = ball;
    event.actor = actor;
    event.position = contact.point;
    event.speed = contact.normalSpeed;
    event.energy = brick.energy;
    event.material = brick.material;
    session.emit(event);
    return true;
}

void SpecialObjects::destroyed(GameSession& session, ActorIndex actor, Vec3 at) {
    if (kindOf(actor) != Kind::Dynamite) {
        return;
    }
    const auto charge = std::ranges::find(charges_, actor, &Charge::actor);
    const GameMaterial* material = session.bricks_[actor].material;
    blast(session, actor, at, charge->radius, material != nullptr ? material->explosePower : 1.0f);
}

void SpecialObjects::update(GameSession& session) {
    touches(session);
    runShots(session);
    runCrates(session);
    runPortals(session);
    runVibros(session);
}

// ---- Damage

bool SpecialObjects::strike(GameSession& session, ActorIndex actor, Vec3 at, int damage, double fuse, bool dropsPrize) {
    GameSession::Brick& brick = session.bricks_[actor];
    bool hurt = false;
    for (int hit = 0; hit < damage && session.hitBrick(actor, at, 0, -1, dropsPrize); ++hit) {
        hurt = true;
    }
    if (fuse > 0 && kindOf(actor) == Kind::Dynamite && brick.exploding) {  // a charge a blast reaches goes off at the next link at the latest
        brick.explodeAt = std::min(brick.explodeAt, session.time_ + fuse);
    }
    return hurt;
}

bool SpecialObjects::weaponHit(GameSession& session, ActorIndex actor, Vec3 at, int damage, bool dropsPrize) {
    if (actor >= kinds_.size() || !session.bricks_[actor].damageable || session.bricks_[actor].removed) {
        return false;
    }
    return strike(session, actor, at, damage, 0, dropsPrize);
}

void SpecialObjects::blast(GameSession& session, ActorIndex source, Vec3 centre, float radius, float power) {
    ++stats_.blasts;
    GameEvent event;
    event.type = EventType::DynamiteBlast;
    event.actor = source;
    event.position = centre;
    event.speed = radius;
    session.emit(event);
    burst(session, source, centre, radius, power, rules_.blastDamage, true);
}

void SpecialObjects::burst(GameSession& session, ActorIndex source, Vec3 centre, float radius, float power, int damage,
                           bool dropsPrize) {
    const PhysicsScene& scene = session.scene_;
    for (ActorIndex actor = 0; actor < kinds_.size(); ++actor) {
        const ActorPlan& plan = scene.plan().actors[actor];
        if (actor == source || !plan.simulated || scene.body(actor) == 0) {
            continue;
        }
        const bool moves = plan.motion != MotionType::Static;
        const Mat4 pose = moves ? scene.actorPose(actor) : plan.pose;
        if (length(pose.translationPart() - centre) > radius + reach_[actor]) {
            continue;
        }
        const Bounds box = moves ? scene.bounds(actor) : staticBounds_[actor];
        const float distance = distanceTo(box, centre);
        if (distance > radius) {
            continue;
        }
        if (plan.motion == MotionType::Dynamic) {
            push(session, actor, centre, 1 - distance / radius, power);
        }
        if (session.bricks_[actor].damageable && !session.bricks_[actor].removed) {
            strike(session, actor, centreOfBox(box), damage, rules_.chainDelay, dropsPrize);
        }
    }
}

// Pushes a loose body away from a blast: `nearness` is 1 at the centre, 0 at the edge of the reach.
void SpecialObjects::push(GameSession& session, ActorIndex actor, Vec3 centre, float nearness, float power) {
    PhysicsWorld& world = session.scene_.world();
    const BodyId body = session.scene_.body(actor);
    const ActorPlan& plan = session.scene_.plan().actors[actor];
    const Vec3 at = session.centreOf(actor);
    Vec3 away{at.x - centre.x, 0, at.z - centre.z};
    away = length(away) > 1e-3f ? normalize(away) : Vec3{0, 0, 0};
    const float mass = std::max(plan.body.mass, 1.0f);
    const float heavy = std::min(1.0f, std::sqrt(rules_.blastReferenceMass / mass));
    const Vec3 change = (away + Vec3{0, rules_.blastLift, 0}) * (rules_.blastSpeed * power * nearness * heavy);
    world.wake(body);
    world.addImpulse(body, change * mass, at);
}

// ---- Springs

// A spring kicks the ball away from its middle, at the ball's speed (docs/GAMEPLAY.md section 17.6 [I]).
void SpecialObjects::kick(GameSession& session, ActorIndex actor, int ballId) {
    const auto ball = std::ranges::find(session.balls_, ballId, &GameSession::Ball::id);
    if (ball == session.balls_.end() || ball->body == 0) {
        return;
    }
    const Vec3 at = session.scene_.world().pose(ball->body).translationPart();
    const Vec3 spring = session.centreOf(actor);
    const Vec3 away{at.x - spring.x, 0, at.z - spring.z};
    if (!(length(away) > 1e-3f)) {
        return;
    }
    ball->heading = normalize(away);
    session.scene_.world().setLinearVelocity(ball->body, ball->heading * session.ballSpeed());
}

// ---- Cannons

void SpecialObjects::fire(GameSession& session, ActorIndex actor) {
    const auto cannon = std::ranges::find(cannons_, actor, &Cannon::actor);
    const GameSession::Brick& brick = session.bricks_[actor];
    if (cannon == cannons_.end() || brick.removed || brick.exploding || brick.shrinking ||
        session.time_ - cannon->firedAt < rules_.cannonCooldown) {
        return;
    }
    cannon->firedAt = session.time_;
    const Mat4 pose = session.scene_.actorPose(actor);
    Shot shot;
    shot.position = transformPoint(rules_.muzzle, pose);
    shot.velocity = transformVector({rules_.shotSpeed, rules_.shotLift, 0}, pose);
    shot.shooter = actor;
    shot.firedAt = session.time_;
    shots_.push_back(shot);
    ++stats_.shotsFired;

    GameEvent event;
    event.type = EventType::CannonFired;
    event.actor = actor;
    event.position = shot.position;
    event.velocity = shot.velocity;
    session.emit(event);
}

// What sets a cannon or a crate off: the ball touching it at any speed (the footage's cannons fired for a ball 1.5 to 2 units
// off, which is a graze, and a rocket came for a ball that brushed the crate: V1 6435.00 and 1013.33), and, for a cannon, a
// loose body that hits it as hard as the ball's blow (V1 6446.85, F18).
void SpecialObjects::touches(GameSession& session) {
    if (cannons_.empty() && crates_.empty()) {
        return;
    }
    const PhysicsScene& scene = session.scene_;
    const float least = session.rules_.hitFraction * session.rules_.ballSpeed;
    for (const ContactEvent& contact : scene.contacts()) {
        for (int side = 0; side < 2; ++side) {
            const std::uint64_t mine = side == 0 ? contact.userDataA : contact.userDataB;
            const std::uint64_t other = side == 0 ? contact.userDataB : contact.userDataA;
            const Kind kind = kindOf(mine < kinds_.size() ? static_cast<ActorIndex>(mine) : kNoActor);
            if (kind != Kind::Cannon && kind != Kind::Firework) {
                continue;
            }
            const bool ball = other == kGameBallData;
            const bool debris = other < kinds_.size() && contact.normalSpeed >= least &&
                                scene.plan().actors[other].motion == MotionType::Dynamic;
            if (kind == Kind::Cannon && (ball || debris)) {
                fire(session, static_cast<ActorIndex>(mine));
            } else if (kind == Kind::Firework && ball) {
                touchCrate(session, static_cast<ActorIndex>(mine));
            }
        }
    }
}

void SpecialObjects::runShots(GameSession& session) {
    const auto step = static_cast<float>(session.fixedStep_);
    const float gravity = session.scene_.plan().gravity.y;
    for (std::size_t i = 0; i < shots_.size();) {
        Shot& shot = shots_[i];
        if (shot.firedAt >= session.time_) {  // (it left the muzzle in this very step)
            ++i;
            continue;
        }
        shot.velocity.y += gravity * step;
        const Vec3 move = shot.velocity * step;
        if (const std::optional<Hit> hit = sweep(session, shot, move)) {
            land(session, shot, *hit);
            shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        shot.position = shot.position + move;
        const bool gone = session.time_ - shot.firedAt > rules_.shotLife || shot.position.y < kShotFloor ||
                          std::fabs(shot.position.x) > kShotLimit || std::fabs(shot.position.z) > kShotLimit;
        if (gone) {
            shots_.erase(shots_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

// What the ball would meet moving by `move`: five rays along it, one from the middle and four from the rim (the sphere is
// not swept; things under its radius could slip between the rays, and there are none).
std::optional<SpecialObjects::Hit> SpecialObjects::sweep(GameSession& session, const Shot& shot, Vec3 move) const {
    const float distance = length(move);
    if (!(distance > 1e-6f)) {
        return std::nullopt;
    }
    const Vec3 forward = move / distance;
    Vec3 side = cross(forward, {0, 1, 0});
    side = length(side) > 1e-3f ? normalize(side) : Vec3{1, 0, 0};
    const Vec3 up = cross(side, forward);
    const float r = rules_.shotRadius;
    const struct {
        Vec3 offset;
        float reach;
    } rays[] = {{{0, 0, 0}, distance + r}, {up * r, distance}, {up * -r, distance}, {side * r, distance}, {side * -r, distance}};

    const PhysicsWorld& world = session.scene_.world();
    std::optional<Hit> nearest;
    for (const auto& ray : rays) {
        const std::optional<RayHit> hit = world.raycast(shot.position + ray.offset, forward, ray.reach);
        if (!hit) {
            continue;
        }
        const std::uint64_t data = world.userData(hit->body);
        // The balls and the bat are no actors: the shot goes through them.
        if (data >= kinds_.size() || (data == shot.shooter && session.time_ - shot.firedAt < kShooterGrace)) {
            continue;
        }
        if (!nearest || hit->distance < nearest->distance) {
            nearest = Hit{hit->point, static_cast<ActorIndex>(data), hit->distance};
        }
    }
    return nearest;
}

// A cannonball bursts: it hurts what it hit, pushes it, and sets off a cannon (V1 6446.8, F17).
void SpecialObjects::land(GameSession& session, const Shot& shot, const Hit& hit) {
    ++stats_.shotsLanded;
    GameEvent event;
    event.type = EventType::ShotHit;
    event.actor = hit.actor;
    event.position = hit.point;
    session.emit(event);

    const GameSession::Brick& brick = session.bricks_[hit.actor];
    if (brick.damageable && !brick.removed) {
        strike(session, hit.actor, hit.point, rules_.cannonballDamage, rules_.chainDelay);
    }
    PhysicsScene& scene = session.scene_;
    const ActorPlan& target = scene.plan().actors[hit.actor];
    if (target.motion == MotionType::Dynamic && scene.body(hit.actor) != 0) {
        // The two leave together (momentum is kept), so that a light thing is not sent off faster than the cannonball was.
        const float mass = std::max(target.body.mass, 1e-3f);
        const Vec3 change = shot.velocity * (rules_.shotMass / (rules_.shotMass + mass));
        scene.world().wake(scene.body(hit.actor));
        scene.world().addImpulse(scene.body(hit.actor), change * mass, hit.point);
    }
    if (kindOf(hit.actor) == Kind::Cannon) {
        fire(session, hit.actor);
    }
}

// ---- Fireworks

void SpecialObjects::touchCrate(GameSession& session, ActorIndex actor) {
    const auto crate = std::ranges::find(crates_, actor, &Crate::actor);
    if (crate == crates_.end() || crate->waiting || crate->flying) {
        return;
    }
    crate->waiting = true;
    crate->launchAt = session.time_ + rules_.rocketDelay;
    crate->from = session.scene_.actorPose(actor).translationPart() + Vec3{0, rules_.rocketStartHeight, 0};
}

void SpecialObjects::runCrates(GameSession& session) {
    for (Crate& crate : crates_) {
        if (!crate.waiting || session.time_ < crate.launchAt) {
            continue;
        }
        crate.waiting = false;
        crate.flying = true;
        Rocket rocket;
        rocket.crate = crate.actor;
        rocket.from = rocket.position = crate.from;
        rocket.launchedAt = session.time_;
        rockets_.push_back(rocket);
        ++stats_.rocketsLaunched;
        GameEvent event;
        event.type = EventType::RocketLaunched;
        event.actor = crate.actor;
        event.position = crate.from;
        session.emit(event);
    }
    for (std::size_t i = 0; i < rockets_.size();) {
        Rocket& rocket = rockets_[i];
        const double age = session.time_ - rocket.launchedAt;
        rocket.position.y = rocket.from.y + rules_.rocketSpeed * static_cast<float>(age);
        if (age < rules_.rocketLife) {
            ++i;
            continue;
        }
        ++stats_.rocketsBurst;
        GameEvent event;
        event.type = EventType::RocketBurst;
        event.actor = rocket.crate;
        event.position = rocket.position;
        session.emit(event);
        std::ranges::find(crates_, rocket.crate, &Crate::actor)->flying = false;
        rockets_.erase(rockets_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

// ---- Teleports

void SpecialObjects::runPortals(GameSession& session) {
    if (!sources_.empty() && !exits_.empty()) {
        for (const GameSession::Ball& ball : session.balls_) {
            if (ball.held || ball.away || ball.body == 0) {
                continue;
            }
            const Vec3 at = session.scene_.world().pose(ball.body).translationPart();
            for (std::size_t i = 0; i < sources_.size(); ++i) {
                if (inside(transformPoint(at, sources_[i].toBox), sources_[i].half)) {
                    enterPortal(session, ball.id, i);
                    break;
                }
            }
        }
    }
    for (std::size_t i = 0; i < transits_.size();) {
        Transit& transit = transits_[i];
        const auto ball = std::ranges::find(session.balls_, transit.ball, &GameSession::Ball::id);
        if (ball == session.balls_.end()) {
            transits_.erase(transits_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!transit.emerged && session.time_ >= transit.emergeAt) {
            transit.emerged = true;
            GameEvent event;
            event.type = EventType::BallEmerged;
            event.ball = transit.ball;
            event.actor = transit.exitActor;
            event.position = transit.exit;
            session.emit(event);
        }
        if (session.time_ < transit.launchAt) {
            ++i;
            continue;
        }
        // It leaves where the drift has taken it, at the ball's speed.
        const Vec3 at = transit.exit + transit.heading * (rules_.portalDriftSpeed * static_cast<float>(rules_.portalDrift));
        const std::size_t index = static_cast<std::size_t>(ball - session.balls_.begin());
        const Vec3 heading = transit.heading;
        transits_.erase(transits_.begin() + static_cast<std::ptrdiff_t>(i));
        ball->away = false;
        ball->body = session.scene_.addGameBall(at, heading * session.ballSpeed(), ball->radius);
        ball->heading = heading;
        ball->anchor = at;
        ball->anchoredAt = session.time_;
        ball->bounced = false;
        if (ball->body == 0) {  // the world refused the body: the ball is lost
            session.loseBall(index, at, {});
        }
    }
}

void SpecialObjects::enterPortal(GameSession& session, int ballId, std::size_t source) {
    const auto ball = std::ranges::find(session.balls_, ballId, &GameSession::Ball::id);
    const Portal& from = sources_[source];
    const Portal& to = exits_[std::min(source, exits_.size() - 1)];  // two sources share the one exit
    const float plane = session.rules_.ballPlaneY;
    const float angle = radians(rules_.portalSpreadDegrees) * (2 * random01() - 1);

    Transit transit;
    transit.ball = ballId;
    transit.exitActor = to.actor;
    transit.from = {from.centre.x, plane, from.centre.z};
    transit.exit = {to.centre.x, plane, to.centre.z};
    transit.heading = {std::sin(angle), 0, -std::cos(angle)};  // toward the bat side
    transit.emergeAt = session.time_ + rules_.portalGone;
    transit.launchAt = transit.emergeAt + rules_.portalDrift;
    transits_.push_back(transit);
    ++stats_.teleports;

    GameEvent event;
    event.type = EventType::BallEntered;
    event.ball = ballId;
    event.actor = from.actor;
    event.position = session.scene_.world().pose(ball->body).translationPart();
    session.emit(event);
    session.scene_.world().removeBody(ball->body);
    ball->body = 0;
    ball->away = true;
}

SpecialObjects::Away SpecialObjects::away(int ball, double time) const {
    const auto transit = std::ranges::find(transits_, ball, &Transit::ball);
    if (transit == transits_.end()) {
        return {};
    }
    if (time < transit->emergeAt) {
        return {transit->from, 0};
    }
    const auto age = static_cast<float>(time - transit->emergeAt);
    return {transit->exit + transit->heading * (rules_.portalDriftSpeed * std::min(age, static_cast<float>(rules_.portalDrift))),
            std::clamp(age / static_cast<float>(rules_.portalGrow), 0.0f, 1.0f)};
}

// ---- Vibros

// A vibro bobs and tilts in place (V1 215-243, F9): its body is moved to the pose a sine gives it, so that it pushes what it
// meets like any kinematic body, and the picture follows the body.
void SpecialObjects::runVibros(GameSession& session) {
    const float phase = static_cast<float>(2 * kPi * session.time_ / rules_.vibroPeriod);
    const float tilt = radians(rules_.vibroTiltDegrees);
    for (const ActorIndex actor : vibros_) {
        const BodyId body = session.scene_.body(actor);
        if (body == 0) {
            continue;
        }
        const Mat4& rest = session.scene_.plan().actors[actor].pose;
        // Each has its own phase, from where it stands, so that a row of them does not move as one.
        const float own = std::fmod(rest.m[3][0] * 1.7f + rest.m[3][2] * 2.9f, 2 * kPi);
        const float swing = phase + own;
        const Mat4 pose = Mat4::rotationX(tilt * std::sin(swing + 1.3f)) * Mat4::rotationZ(tilt * std::sin(swing)) * rest *
                          Mat4::translation({0, rules_.vibroBob * std::sin(swing + 0.7f), 0});
        session.scene_.world().setKinematicTarget(body, pose);
    }
}

}  // namespace mb
