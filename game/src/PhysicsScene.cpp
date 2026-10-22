#include "mb/game/PhysicsScene.h"

#include "mb/engine/Log.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace mb {

namespace {

// The test ball is not the original's ball (the real one comes with step 4). Bricks weigh 51 units at the
// median and 930 at the 90th percentile, so this one is heavy enough to knock the ordinary ones over and to shove
// the heavy ones; it spins as freely as a ball rolling at its speed.
constexpr float kBallMass = 500.0f;
constexpr float kBallMaxAngularVelocity = 60.0f;
constexpr float kBallFloor = -30.0f;  // below this it has left the world
constexpr double kBallMaxAge = 20.0;
constexpr float kBallVolume = 4.0f * kPi / 3.0f * kBallRadius * kBallRadius * kBallRadius;

constexpr float kLowBand = 0.3f;  // lowestPoints() gives the points this close to the lowest one
// How often a body that fell asleep with nothing under it is woken again before it is let sleep anyway, and how many
// steps pass between two looks at the sleeping bodies that rest on something.
constexpr std::uint8_t kMaxRewakes = 3;
constexpr std::uint64_t kLookEvery = 6;
// A body within this of a removed one's box may have rested on it, and is woken: the check of wakeUnsupported() does
// not reach one that a joint holds, and a joined assembly rests on the piece through whichever part touches it. That
// is the saved gap under a body (kSupportGap) and 15 cm more for pieces that lean.
constexpr float kWakeMargin = kSupportGap + 0.15f;

// The points that make up a shape in its own frame: the corners of a box, the points of a hull, the centre of a sphere,
// the ends of a capsule's axis (the last two are to be grown by the radius, see roundness()).
std::vector<Vec3> shapePoints(const PhysicsShape& shape) {
    switch (shape.type) {
        case PhysicsShapeType::Box: {
            std::vector<Vec3> corners;
            for (int i = 0; i < 8; ++i) {
                corners.push_back({(i & 1) ? shape.halfExtents.x : -shape.halfExtents.x,
                                   (i & 2) ? shape.halfExtents.y : -shape.halfExtents.y,
                                   (i & 4) ? shape.halfExtents.z : -shape.halfExtents.z});
            }
            return corners;
        }
        case PhysicsShapeType::Sphere: return {{0, 0, 0}};
        case PhysicsShapeType::Capsule: return {{0, shape.halfHeight, 0}, {0, -shape.halfHeight, 0}};
        case PhysicsShapeType::ConvexHull: return shape.points;
    }
    return {};
}

float roundness(const PhysicsShape& shape) {
    return shape.type == PhysicsShapeType::Sphere || shape.type == PhysicsShapeType::Capsule ? shape.radius : 0.0f;
}

}  // namespace

Bounds boundsOf(const BodyDesc& body, const Mat4& pose) {
    Bounds bounds;
    for (const PhysicsShape& shape : body.shapes) {
        const Mat4 world = shape.localPose * pose;
        for (const Vec3 p : shapePoints(shape)) {
            bounds.add(transformPoint(p, world), roundness(shape));
        }
    }
    return bounds;
}

std::vector<Vec3> lowestPoints(const BodyDesc& body, const Mat4& pose) {
    std::vector<Vec3> points;
    for (const PhysicsShape& shape : body.shapes) {
        const Mat4 world = shape.localPose * pose;
        for (const Vec3 p : shapePoints(shape)) {
            Vec3 w = transformPoint(p, world);
            w.y -= roundness(shape);
            points.push_back(w);
        }
    }
    if (points.empty()) {
        return points;
    }
    const float lowest = std::ranges::min_element(points, {}, &Vec3::y)->y;
    std::erase_if(points, [lowest](Vec3 p) { return p.y > lowest + kLowBand; });
    constexpr std::size_t kMostPoints = 16;
    if (points.size() > kMostPoints) {
        std::ranges::sort(points, {}, &Vec3::y);
        points.resize(kMostPoints);
    }
    return points;
}

PhysicsScene::PhysicsScene(const Level& level, const MaterialLibrary& materials, const PhysicsOptions& options,
                           const PhysicsSettings& settings)
    : plan_(planPhysics(level, materials, options)),
      world_(plan_.gravity, settings),
      paths_(level),
      breaking_(options.breaking),
      fixedStep_(world_.settings().fixedStep),
      impactSpeed_(world_.settings().bounceThreshold) {
    if (const GameMaterial* ball = materials.find("ballMaterial")) {
        ballMaterial_ = physicsMaterial(*ball);
        ballDensity_ = ball->density > 0 ? ball->density : 1.0f;
    } else {
        logWarn("the game material ballMaterial is missing; the test ball uses the default one");
    }

    bodies_.assign(plan_.actors.size(), 0);
    std::vector<bool> inSail(plan_.actors.size(), false);  // the cloth bodies come with the sails, not with the actors
    for (const ClothPlan& cloth : plan_.cloths) {
        for (const ActorIndex actor : cloth.actors) {
            inSail[actor] = true;
        }
    }
    for (std::size_t i = 0; i < plan_.actors.size(); ++i) {
        const ActorPlan& actor = plan_.actors[i];
        if (inSail[i]) {
            ++counts_.clothParticles;
            continue;
        }
        if (!actor.simulated) {
            ++counts_.withoutBody;
            continue;
        }
        if (actor.type == ObjectType::TeleportSrc || actor.type == ObjectType::TeleportDest) {
            BodyDesc portal = actor.body;  // weapons ignore teleports (docs/GAMEPLAY.md section 18.2)
            portal.group = kKindPortal;
            portal.collidesWith = ~kKindProjectile;
            bodies_[i] = world_.addBody(portal);
        } else {
            bodies_[i] = world_.addBody(actor.body);
        }
        if (bodies_[i] == 0) {
            ++counts_.withoutBody;
            continue;
        }
        switch (world_.motionType(bodies_[i])) {
            case MotionType::Dynamic: ++counts_.dynamicBodies; break;
            case MotionType::Kinematic: ++counts_.kinematicBodies; break;
            case MotionType::Static: ++counts_.staticBodies; break;
        }
    }

    cloths_.emplace(plan_, world_, bodies_, options.cloth);
    counts_.cloths = cloths_->size();

    // A path actor starts where its path has it, not where it was saved: the poses were saved a frame or two behind
    // the path, and a key can lie a few units off a path that was edited afterwards.
    for (const PathMover& mover : paths_.movers()) {
        const BodyId body = bodies_[mover.actor];
        if (body == 0 || world_.motionType(body) != MotionType::Kinematic) {
            logWarn("PhysicsScene: actor {} is bound to a path but is not a kinematic body; it stays put", mover.actor);
            continue;
        }
        world_.setPose(body, paths_.pose(mover, 0));
        movers_.push_back(mover);
    }
    counts_.pathMovers = movers_.size();
    if (paths_.unusedKeys() > 0) {
        logWarn("PhysicsScene: {} path keys name no usable path or no actor", paths_.unusedKeys());
    }

    wasAwake_.assign(plan_.actors.size(), 0);
    rewoken_.assign(plan_.actors.size(), 0);
    restsOn_.assign(plan_.actors.size(), 0);
    for (ActorIndex i = 0; i < plan_.actors.size(); ++i) {
        const BodyDesc& body = plan_.actors[i].body;
        // Only a body that gravity pulls can be left asleep in the air (one that has none, or is held at its height,
        // is meant to hover).
        if (bodies_[i] != 0 && cloths_->clothOf(i) < 0 && world_.motionType(bodies_[i]) == MotionType::Dynamic &&
            body.gravity && !body.lockTranslation[1]) {
            fallingActors_.push_back(i);
            wasAwake_[i] = world_.isAwake(bodies_[i]);
            restsOn_[i] = supported(i);
        }
    }

    jointIds_.assign(plan_.joints.size(), 0);
    jointsOfActor_.resize(plan_.actors.size());
    for (std::size_t i = 0; i < plan_.joints.size(); ++i) {
        const JointPlan& joint = plan_.joints[i];
        if (joint.toCloth) {
            ++counts_.clothJoints;
            continue;
        }
        JointDesc desc = joint.desc;
        desc.bodyA = joint.actorA == kNoActor ? 0 : bodies_[joint.actorA];
        desc.bodyB = joint.actorB == kNoActor ? 0 : bodies_[joint.actorB];
        const bool missingBody = (joint.actorA != kNoActor && desc.bodyA == 0) ||
                                 (joint.actorB != kNoActor && desc.bodyB == 0);
        if (breaking_ == JointBreaking::Impact) {
            desc.breakForce = desc.breakTorque = std::numeric_limits<float>::infinity();
        }
        const JointId id = missingBody ? 0 : world_.addJoint(desc);
        if (id == 0) {
            ++counts_.jointsRefused;
            continue;
        }
        ++counts_.joints;
        jointIds_[i] = id;
        if (planJointOfId_.size() <= id) {
            planJointOfId_.resize(id + 1);
        }
        planJointOfId_[id] = i;
        for (const ActorIndex actor : {joint.actorA, joint.actorB}) {
            if (actor != kNoActor) {
                jointsOfActor_[actor].push_back(i);
            }
        }
    }
}

Mat4 PhysicsScene::actorPose(ActorIndex actor) const {
    return bodies_[actor] != 0 && cloths_->clothOf(actor) < 0 ? world_.pose(bodies_[actor]) : plan_.actors[actor].pose;
}

Bounds PhysicsScene::bounds(ActorIndex actor) const {
    const int cloth = cloths_->clothOf(actor);
    return cloth >= 0 ? cloths_->bounds(static_cast<std::size_t>(cloth)) : boundsOf(plan_.actors[actor].body, actorPose(actor));
}

Vec3 PhysicsScene::centreOf(ActorIndex actor) const {
    // The pose is the model's origin, which can be well away from the pieces (a ship's is on the water line).
    const int cloth = cloths_->clothOf(actor);
    return cloth >= 0 ? cloths_->centre(static_cast<std::size_t>(cloth))
                      : transformPoint(plan_.actors[actor].body.massLocalPose.translationPart(), actorPose(actor));
}

bool PhysicsScene::supported(ActorIndex actor) const {
    const std::vector<Vec3> points = lowestPoints(plan_.actors[actor].body, actorPose(actor));
    if (points.empty()) {
        return true;
    }
    // Rays start under the body's lowest point, so that none of them starts inside its own shape.
    float lowest = points[0].y;
    for (const Vec3 p : points) {
        lowest = std::min(lowest, p.y);
    }
    for (const Vec3 p : points) {
        if (world_.raycast({p.x, lowest - 0.005f, p.z}, {0, -1, 0}, kSupportGap)) {
            return true;
        }
    }
    return false;
}

bool PhysicsScene::held(ActorIndex actor) const {
    return std::ranges::any_of(jointsOfActor_[actor], [this](std::size_t joint) { return jointIds_[joint] != 0; });
}

void PhysicsScene::wakeUnsupported() {
    const bool look = ++steps_ % kLookEvery == 0 || lookNow_;
    lookNow_ = false;
    for (const ActorIndex actor : fallingActors_) {
        if (!world_.isAwake(bodies_[actor])) {
            if (wasAwake_[actor]) {  // it has fallen asleep since the last step
                restsOn_[actor] = supported(actor);
                if (!restsOn_[actor] && rewoken_[actor] < kMaxRewakes && !held(actor)) {
                    world_.wake(bodies_[actor]);
                    ++rewoken_[actor];
                }
            } else if (look && restsOn_[actor] && !held(actor) && !supported(actor)) {  // its support has gone
                world_.wake(bodies_[actor]);
                restsOn_[actor] = false;
            }
        }
        wasAwake_[actor] = world_.isAwake(bodies_[actor]);
    }
}

float PhysicsScene::massOf(std::uint64_t userData, BodyId body) const {
    constexpr float kImmovable = std::numeric_limits<float>::infinity();
    if (userData == kBatData) {
        return kImmovable;
    }
    if (userData == kGameBallData) {
        return kBallVolume * ballDensity_;
    }
    if (userData == kProjectileData) {
        const auto it = std::ranges::find(projectileMasses_, body, &std::pair<BodyId, float>::first);
        return it != projectileMasses_.end() ? it->second : kBallMass;
    }
    if (!isActor(userData)) {
        return kBallMass;
    }
    const ActorPlan& actor = plan_.actors[userData];
    return actor.motion == MotionType::Dynamic ? std::max(actor.body.mass, 1e-3f) : kImmovable;
}

void PhysicsScene::breakJoint(std::size_t planJoint) {
    world_.removeJoint(jointIds_[planJoint]);
    jointBroke(planJoint);
}

void PhysicsScene::jointBroke(std::size_t planJoint) {
    releaseJoint(planJoint);
    broken_.push_back(planJoint);
}

void PhysicsScene::releaseJoint(std::size_t planJoint) {
    jointIds_[planJoint] = 0;
    // Nothing else wakes the pieces it held: a sleeping one would hang where the joint left it.
    const JointPlan& joint = plan_.joints[planJoint];
    for (const ActorIndex actor : {joint.actorA, joint.actorB}) {
        if (actor != kNoActor && bodies_[actor] != 0 && plan_.actors[actor].motion == MotionType::Dynamic) {
            world_.wake(bodies_[actor]);
        }
    }
}

void PhysicsScene::breakByHit(const ContactEvent& contact) {
    if (contact.normalSpeed < impactSpeed_) {
        return;
    }
    constexpr float kImmovable = std::numeric_limits<float>::infinity();
    const float a = massOf(contact.userDataA, contact.a), b = massOf(contact.userDataB, contact.b);
    if (a == kImmovable && b == kImmovable) {
        return;
    }
    const float effective = a == kImmovable ? b : (b == kImmovable ? a : a * b / (a + b));
    const float force = effective * contact.normalSpeed / fixedStep_;
    for (const std::uint64_t userData : {contact.userDataA, contact.userDataB}) {
        if (!isActor(userData)) {
            continue;
        }
        cloths_->holderHit(static_cast<ActorIndex>(userData), force);  // the sails it holds are joints too
        for (const std::size_t joint : jointsOfActor_[userData]) {
            if (jointIds_[joint] != 0 && force > plan_.joints[joint].desc.breakForce) {
                breakJoint(joint);
            }
        }
    }
}

void PhysicsScene::movePathActors() {
    pathTime_ += fixedStep_;
    for (const PathMover& mover : movers_) {
        if (bodies_[mover.actor] != 0) {
            world_.setKinematicTarget(bodies_[mover.actor], paths_.pose(mover, pathTime_));
        }
    }
}

void PhysicsScene::beforeFixedStep() {
    movePathActors();
    cloths_->carryPins(*this);
}

void PhysicsScene::step(float dt) {
    world_.step(dt, [this] { beforeFixedStep(); });
    wakeUnsupported();

    contacts_ = world_.takeContacts();
    for (const ContactEvent& contact : contacts_) {
        if (contact.userDataA == kTestBallData && isActor(contact.userDataB)) {
            touched_.insert(static_cast<ActorIndex>(contact.userDataB));
        } else if (contact.userDataB == kTestBallData && isActor(contact.userDataA)) {
            touched_.insert(static_cast<ActorIndex>(contact.userDataA));
        }
        if (breaking_ == JointBreaking::Impact) {
            breakByHit(contact);
        }
    }
    for (const JointId id : world_.takeBrokenJoints()) {  // only with JointBreaking::Load
        jointBroke(planJointOfId_[id]);
    }

    for (Ball& ball : balls_) {
        ball.age += dt;
    }
    std::erase_if(balls_, [this](const Ball& ball) {
        const bool gone = ball.age > kBallMaxAge || world_.pose(ball.body).translationPart().y < kBallFloor;
        if (gone) {
            world_.removeBody(ball.body);
        }
        return gone;
    });
}

void PhysicsScene::removeActor(ActorIndex actor) {
    if (bodies_[actor] == 0) {
        return;
    }
    if (const int cloth = cloths_->clothOf(actor); cloth >= 0) {  // a sail: it supports nothing, and nothing joins it
        cloths_->remove(static_cast<std::size_t>(cloth));
        bodies_[actor] = 0;
        return;
    }
    const Bounds gone = boundsOf(plan_.actors[actor].body, actorPose(actor));
    world_.removeBody(bodies_[actor]);  // its joints go with it
    bodies_[actor] = 0;
    cloths_->holderGone(actor);  // the sails it held fall free
    std::erase(fallingActors_, actor);
    for (const std::size_t joint : jointsOfActor_[actor]) {
        if (jointIds_[joint] != 0) {
            releaseJoint(joint);
        }
    }
    // What rested on it falls. The look of wakeUnsupported() skips a body that a joint holds (to its neighbours, which
    // rest on the removed piece as well), so every sleeper next to it is woken, and a woken body takes the pieces
    // joined to it along.
    for (const ActorIndex other : fallingActors_) {
        if (!world_.isAwake(bodies_[other]) &&
            boundsOf(plan_.actors[other].body, actorPose(other)).near(gone, kWakeMargin)) {
            world_.wake(bodies_[other]);
        }
    }
    lookNow_ = true;
}

Vec3 PhysicsScene::centreOfMovables() const {
    Vec3 sum{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < plan_.actors.size(); ++i) {
        if (bodies_[i] != 0 && plan_.actors[i].motion == MotionType::Dynamic) {
            sum = sum + plan_.actors[i].pose.translationPart();
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : Vec3{};
}

BodyId PhysicsScene::launchBall(Vec3 position, Vec3 velocity) {
    PhysicsShape sphere;
    sphere.type = PhysicsShapeType::Sphere;
    sphere.radius = kBallRadius;
    sphere.material = ballMaterial_;

    BodyDesc desc;
    desc.pose = Mat4::translation(position);
    desc.shapes = {sphere};
    desc.mass = kBallMass;
    desc.linearVelocity = velocity;
    desc.maxAngularVelocity = kBallMaxAngularVelocity;
    desc.continuousCollision = true;
    desc.userData = kNoActor;
    const BodyId body = world_.addBody(desc);
    if (body != 0) {
        balls_.push_back({body, 0});
    }
    return body;
}

BodyId PhysicsScene::addGameBall(Vec3 position, Vec3 velocity, float radius) {
    PhysicsShape sphere;
    sphere.type = PhysicsShapeType::Sphere;
    sphere.radius = radius;
    sphere.material = ballMaterial_;

    BodyDesc desc;
    desc.pose = Mat4::translation(position);
    desc.shapes = {sphere};
    desc.density = ballDensity_;
    desc.linearVelocity = velocity;
    desc.gravity = false;
    // In its plane it can only turn about the vertical: friction at a wall spins it, and the spin turns the next
    // bounce.
    desc.lockTranslation[1] = true;
    desc.lockRotation[0] = desc.lockRotation[2] = true;
    desc.sleepLinearVelocity = 0;  // never falls asleep
    desc.continuousCollision = true;
    desc.group = kKindBall;
    desc.userData = kGameBallData;
    return world_.addBody(desc);
}

BodyId PhysicsScene::addBat(Vec3 position, float radius) {
    PhysicsShape sphere;
    sphere.type = PhysicsShapeType::Sphere;
    sphere.radius = radius;
    // No friction: the bat puts no spin on the ball (docs/GAMEPLAY.md section 3), and a ball that friction holds
    // between the bat at its limit and the side wall does not come free (a human frees it by moving the bat).
    sphere.material.staticFriction = sphere.material.dynamicFriction = 0;
    sphere.material.frictionCombine = CombineMode::Min;

    BodyDesc desc;
    desc.pose = Mat4::translation(position);
    desc.motion = MotionType::Kinematic;
    desc.shapes = {sphere};
    desc.density = 1;
    desc.group = kKindBat;
    desc.userData = kBatData;
    return world_.addBody(desc);
}

BodyId PhysicsScene::addProjectile(const Mat4& pose, const PhysicsShape& shape, Vec3 velocity, bool gravity, float density) {
    BodyDesc desc;
    desc.pose = pose;
    desc.shapes = {shape};
    desc.density = density;
    desc.linearVelocity = velocity;
    desc.gravity = gravity;
    desc.lockRotation[0] = desc.lockRotation[1] = desc.lockRotation[2] = true;
    desc.sleepLinearVelocity = 0;  // never falls asleep
    desc.continuousCollision = true;
    desc.group = kKindProjectile;
    desc.collidesWith = kKindLevel;
    desc.userData = kProjectileData;
    const BodyId body = world_.addBody(desc);
    if (body != 0) {
        const float r = shape.radius;
        const float volume = 4.0f * kPi / 3.0f * r * r * r +
                             (shape.type == PhysicsShapeType::Capsule ? kPi * r * r * 2.0f * shape.halfHeight : 0.0f);
        projectileMasses_.emplace_back(body, std::max(volume * density, 1e-3f));
    }
    return body;
}

void PhysicsScene::removeProjectile(BodyId body) {
    std::erase_if(projectileMasses_, [body](const std::pair<BodyId, float>& entry) { return entry.first == body; });
    world_.removeBody(body);
}

}  // namespace mb
