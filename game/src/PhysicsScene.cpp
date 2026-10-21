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

// How far below its lowest points a body may have the surface it rests on, and how often a body that has none is
// woken again before it is let sleep anyway.
constexpr float kSupportGap = 0.15f;
constexpr float kLowBand = 0.3f;  // lowestPoints() gives the points this close to the lowest one
constexpr std::uint8_t kMaxRewakes = 3;

}  // namespace

std::vector<Vec3> lowestPoints(const BodyDesc& body, const Mat4& pose) {
    std::vector<Vec3> points;
    for (const PhysicsShape& shape : body.shapes) {
        const Mat4 world = shape.localPose * pose;
        std::vector<Vec3> local;
        switch (shape.type) {
            case PhysicsShapeType::Box:
                for (int i = 0; i < 8; ++i) {
                    local.push_back({(i & 1) ? shape.halfExtents.x : -shape.halfExtents.x,
                                     (i & 2) ? shape.halfExtents.y : -shape.halfExtents.y,
                                     (i & 4) ? shape.halfExtents.z : -shape.halfExtents.z});
                }
                break;
            case PhysicsShapeType::Sphere:
                local.push_back({0, 0, 0});
                break;
            case PhysicsShapeType::Capsule:
                local = {{0, shape.halfHeight, 0}, {0, -shape.halfHeight, 0}};
                break;
            case PhysicsShapeType::ConvexHull:
                local = shape.points;
                break;
        }
        const bool round = shape.type == PhysicsShapeType::Sphere || shape.type == PhysicsShapeType::Capsule;
        for (const Vec3 p : local) {
            Vec3 w = transformPoint(p, world);
            w.y -= round ? shape.radius : 0.0f;
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
      breaking_(options.breaking),
      fixedStep_(settings.fixedStep),
      impactSpeed_(settings.bounceThreshold) {
    if (const GameMaterial* ball = materials.find("ballMaterial")) {
        ballMaterial_ = physicsMaterial(*ball);
    } else {
        logWarn("the game material ballMaterial is missing; the test ball uses the default one");
    }

    bodies_.assign(plan_.actors.size(), 0);
    for (std::size_t i = 0; i < plan_.actors.size(); ++i) {
        const ActorPlan& actor = plan_.actors[i];
        if (!actor.simulated) {
            ++counts_.withoutBody;
            continue;
        }
        bodies_[i] = world_.addBody(actor.body);
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

    wasAwake_.assign(plan_.actors.size(), 0);
    rewoken_.assign(plan_.actors.size(), 0);
    for (ActorIndex i = 0; i < plan_.actors.size(); ++i) {
        const BodyDesc& body = plan_.actors[i].body;
        // Only a body that gravity pulls can be left asleep in the air (one that has none, or is held at its height,
        // is meant to hover).
        if (bodies_[i] != 0 && world_.motionType(bodies_[i]) == MotionType::Dynamic && body.gravity &&
            !body.lockTranslation[1]) {
            fallingActors_.push_back(i);
            wasAwake_[i] = world_.isAwake(bodies_[i]);
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
    return bodies_[actor] != 0 ? world_.pose(bodies_[actor]) : plan_.actors[actor].pose;
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

void PhysicsScene::wakeUnsupported() {
    for (const ActorIndex actor : fallingActors_) {
        const bool awake = world_.isAwake(bodies_[actor]);
        if (wasAwake_[actor] && !awake && rewoken_[actor] < kMaxRewakes) {
            const bool held = std::ranges::any_of(jointsOfActor_[actor], [this](std::size_t joint) {
                return jointIds_[joint] != 0;
            });
            if (!held && !supported(actor)) {
                world_.wake(bodies_[actor]);
                ++rewoken_[actor];
            }
        }
        wasAwake_[actor] = world_.isAwake(bodies_[actor]);
    }
}

float PhysicsScene::massOf(std::uint64_t userData) const {
    constexpr float kImmovable = std::numeric_limits<float>::infinity();
    if (userData == kNoActor) {
        return kBallMass;
    }
    const ActorPlan& actor = plan_.actors[userData];
    return actor.motion == MotionType::Dynamic ? std::max(actor.body.mass, 1e-3f) : kImmovable;
}

void PhysicsScene::breakJoint(std::size_t planJoint) {
    world_.removeJoint(jointIds_[planJoint]);
    jointIds_[planJoint] = 0;
    broken_.push_back(planJoint);
}

void PhysicsScene::breakByHit(const ContactEvent& contact) {
    if (contact.normalSpeed < impactSpeed_) {
        return;
    }
    constexpr float kImmovable = std::numeric_limits<float>::infinity();
    const float a = massOf(contact.userDataA), b = massOf(contact.userDataB);
    if (a == kImmovable && b == kImmovable) {
        return;
    }
    const float effective = a == kImmovable ? b : (b == kImmovable ? a : a * b / (a + b));
    const float force = effective * contact.normalSpeed / fixedStep_;
    for (const std::uint64_t userData : {contact.userDataA, contact.userDataB}) {
        if (userData == kNoActor) {
            continue;
        }
        for (const std::size_t joint : jointsOfActor_[userData]) {
            if (jointIds_[joint] != 0 && force > plan_.joints[joint].desc.breakForce) {
                breakJoint(joint);
            }
        }
    }
}

void PhysicsScene::step(float dt) {
    world_.step(dt);
    wakeUnsupported();

    for (const ContactEvent& contact : world_.takeContacts()) {
        // Only the ball has no actor: every body of the level carries its ActorIndex.
        if (contact.userDataA == kNoActor && contact.userDataB != kNoActor) {
            touched_.insert(static_cast<ActorIndex>(contact.userDataB));
        } else if (contact.userDataB == kNoActor && contact.userDataA != kNoActor) {
            touched_.insert(static_cast<ActorIndex>(contact.userDataA));
        }
        if (breaking_ == JointBreaking::Impact) {
            breakByHit(contact);
        }
    }
    for (const JointId id : world_.takeBrokenJoints()) {  // only with JointBreaking::Load
        jointIds_[planJointOfId_[id]] = 0;
        broken_.push_back(planJointOfId_[id]);
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

}  // namespace mb
