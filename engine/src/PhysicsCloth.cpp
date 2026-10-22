// Cloth: a grid of particles as a Jolt soft body, the pins that hold it, the wind that blows it and the touches it reports.

#include "PhysicsImpl.h"

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

namespace mb {

namespace {

// The numbers below are docs/GAMEPLAY.md section 20.5 [I]: the engine's limits and the cloth's own care.
constexpr int kMostClothParticles = 4096;
// How far a pin has to be from its particle to wake a cloth that has gone to sleep, and how near a body that moves has to come to
// the cloth's box (a particle's radius, and a step of the fastest thing there is: the ball's quarter of a unit).
constexpr float kPinWakes = 1e-3f;
constexpr float kWakeReach = 1.0f;
// A particle this far from the origin has been sent off by the solver: a level is 100 units across, and what falls out of the world
// is taken out at 30 below (a sail that fell off the board is taken out by the rules long before: GameplayRules::fallLimitY).
// The third time it happens the cloth is held where it is.
constexpr float kClothReach = 500.0f;
constexpr int kMostResets = 3;
// A spring this short has no direction to hold.
constexpr float kLeastSpringLength = 1e-4f;

JPH::SoftBodyMotionProperties& particlesOf(JPH::Body& body) {
    return *static_cast<JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
}

Vec3 fromJoltPoint(JPH::RVec3Arg v) {
    return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}

bool finiteAndAtLeast(float value, float least) {
    return std::isfinite(value) && value >= least;
}

// Why a description cannot be made into a cloth; empty if it can.
std::string clothFault(const ClothDesc& desc) {
    if (desc.width < 2 || desc.height < 2 || desc.width * desc.height > kMostClothParticles) {
        return std::format("a cloth needs 2 to {} particles each way, in all at most {}, not {} by {}",
                           kMostClothParticles / 2, kMostClothParticles, desc.width, desc.height);
    }
    if (desc.positions.size() != static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height)) {
        return std::format("a cloth of {} by {} needs {} positions, got {}", desc.width, desc.height,
                           desc.width * desc.height, desc.positions.size());
    }
    if (!std::ranges::all_of(desc.positions, [](Vec3 p) { return isFinite(p); })) {
        return "a position is not finite";
    }
    if (!finiteAndAtLeast(desc.particleMass, 1e-6f) || !finiteAndAtLeast(desc.particleRadius, 0) ||
        !finiteAndAtLeast(desc.stretchCompliance, 0) || !finiteAndAtLeast(desc.shearCompliance, 0) ||
        !std::isfinite(desc.bendCompliance) || !finiteAndAtLeast(desc.slack, -0.5f) || desc.slack > 0.5f ||
        !finiteAndAtLeast(desc.slackSeconds, 0) || !finiteAndAtLeast(desc.damping, 0) ||
        !finiteAndAtLeast(desc.friction, 0) || !finiteAndAtLeast(desc.restitution, 0) || desc.iterations < 1) {
        return "the mass, radius, compliances, slack, damping, friction, restitution or iterations are not usable";
    }
    for (int r = 0; r < desc.height; ++r) {
        for (int c = 0; c < desc.width; ++c) {
            const Vec3 p = desc.positions[static_cast<std::size_t>(r * desc.width + c)];
            const bool shortAcross = c + 1 < desc.width &&
                                     length(desc.positions[static_cast<std::size_t>(r * desc.width + c + 1)] - p) < kLeastSpringLength;
            const bool shortUp = r + 1 < desc.height &&
                                 length(desc.positions[static_cast<std::size_t>((r + 1) * desc.width + c)] - p) < kLeastSpringLength;
            if (shortAcross || shortUp) {
                return "two neighbouring particles are in the same place";
            }
        }
    }
    return {};
}

}  // namespace

BodyId PhysicsWorld::addCloth(const ClothDesc& desc) {
    Impl& w = *impl_;
    if (const std::string fault = clothFault(desc); !fault.empty()) {
        logWarn("PhysicsWorld::addCloth: {}", fault);
        return 0;
    }

    JPH::Ref<JPH::SoftBodySharedSettings> shared = new JPH::SoftBodySharedSettings;
    ClothRecord record;
    record.width = desc.width;
    record.height = desc.height;
    const float invMass = 1.0f / desc.particleMass;
    for (const Vec3 p : desc.positions) {
        shared->mVertices.push_back(JPH::SoftBodySharedSettings::Vertex(JPH::Float3(p.x, p.y, p.z), JPH::Float3(0, 0, 0), invMass));
        record.invMass.push_back(invMass);
    }
    for (int r = 0; r + 1 < desc.height; ++r) {
        for (int c = 0; c + 1 < desc.width; ++c) {
            const auto at = [&](int row, int column) { return static_cast<std::uint32_t>(row * desc.width + column); };
            for (const std::array<std::uint32_t, 3> face : {std::array{at(r, c), at(r, c + 1), at(r + 1, c + 1)},
                                                             std::array{at(r, c), at(r + 1, c + 1), at(r + 1, c)}}) {
                shared->mFaces.push_back(JPH::SoftBodySharedSettings::Face(face[0], face[1], face[2]));
                record.faces.push_back(face);
            }
        }
    }
    const bool folds = desc.bendCompliance >= 0;
    const JPH::SoftBodySharedSettings::VertexAttributes attributes(desc.stretchCompliance, desc.shearCompliance,
                                                                   folds ? desc.bendCompliance : FLT_MAX);
    shared->CreateConstraints(&attributes, 1,
                              folds ? JPH::SoftBodySharedSettings::EBendType::Distance
                                    : JPH::SoftBodySharedSettings::EBendType::None);
    shared->Optimize();
    for (const JPH::SoftBodySharedSettings::Edge& edge : shared->mEdgeConstraints) {
        record.restLength.push_back(edge.mRestLength);
    }
    record.shared = shared;
    record.slack = desc.slack;
    record.slackSeconds = desc.slackSeconds;
    record.slackGrown = desc.slack == 0;
    for (const Vec3 p : desc.positions) {
        record.sound.push_back(toJolt(p));
    }

    // The body stays at the origin and the particles are in world space: nothing that moves the body (a pose, a recentring)
    // could shift them.
    JPH::SoftBodyCreationSettings settings(shared, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), layers::cloth);
    const BodyId id = w.nextBody;
    settings.mUserData = id;
    settings.mNumIterations = static_cast<JPH::uint32>(desc.iterations);
    settings.mLinearDamping = desc.damping;
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mVertexRadius = desc.particleRadius;
    settings.mUpdatePosition = false;
    settings.mAllowSleeping = record.slackGrown;  // a cloth lies still when the wind and the pins have settled it; not while it is still filling out
    settings.mFacesDoubleSided = true;  // rays hit it from either side
    JPH::Body* body = w.bodies.CreateSoftBody(settings);
    if (body == nullptr) {
        logWarn("PhysicsWorld::addCloth: out of bodies (PhysicsSettings::maxBodies is {})", w.settings.maxBodies);
        return 0;
    }
    ++w.nextBody;
    w.bodies.AddBody(body->GetID(), JPH::EActivation::Activate);
    ++w.addedSinceOptimize;

    BodyRecord bodyRecord;
    bodyRecord.id = body->GetID();
    bodyRecord.userData = desc.userData;
    w.records.emplace(id, std::move(bodyRecord));
    w.cloths.emplace(id, std::move(record));
    return id;
}

std::vector<Vec3> PhysicsWorld::clothPositions(BodyId cloth) const {
    const Impl& w = *impl_;
    const auto found = w.cloths.find(cloth);
    if (found == w.cloths.end()) {
        return {};
    }
    JPH::Body* body = w.joltBody(w.records.at(cloth));
    std::vector<Vec3> positions;
    for (const JPH::SoftBodyVertex& v : particlesOf(*body).GetVertices()) {
        positions.push_back(fromJoltPoint(body->GetCenterOfMassTransform() * v.mPosition));
    }
    return positions;
}

void PhysicsWorld::pinClothParticle(BodyId cloth, int particle, Vec3 position) {
    Impl& w = *impl_;
    const auto found = w.cloths.find(cloth);
    if (found == w.cloths.end() || particle < 0 || particle >= static_cast<int>(found->second.invMass.size()) ||
        !isFinite(position)) {
        logWarn("PhysicsWorld::pinClothParticle: body {} is no cloth, or particle {} or the position is not usable", cloth,
                particle);
        return;
    }
    ClothRecord& record = found->second;
    if (record.frozen) {
        return;
    }
    const auto [pin, isNew] = record.pins.try_emplace(particle);
    pin->second.position = position;
    if (isNew) {
        JPH::SoftBodyVertex& vertex = particlesOf(*w.joltBody(w.records.at(cloth))).GetVertex(static_cast<JPH::uint>(particle));
        vertex.mInvMass = 0;
        vertex.mVelocity = JPH::Vec3::sZero();
        w.bodies.ActivateBody(w.records.at(cloth).id);
    }
}

void PhysicsWorld::releaseClothParticle(BodyId cloth, int particle) {
    Impl& w = *impl_;
    const auto found = w.cloths.find(cloth);
    if (found == w.cloths.end()) {
        return;
    }
    const auto pin = found->second.pins.find(particle);
    if (pin == found->second.pins.end()) {
        return;
    }
    JPH::SoftBodyVertex& vertex = particlesOf(*w.joltBody(w.records.at(cloth))).GetVertex(static_cast<JPH::uint>(particle));
    vertex.mInvMass = found->second.invMass[static_cast<std::size_t>(particle)];
    vertex.mVelocity = toJolt(pin->second.velocity);
    found->second.pins.erase(pin);
    w.bodies.ActivateBody(w.records.at(cloth).id);  // it falls
}

void PhysicsWorld::setClothWind(BodyId cloth, Vec3 acceleration) {
    const auto found = impl_->cloths.find(cloth);
    if (found == impl_->cloths.end() || !isFinite(acceleration)) {
        logWarn("PhysicsWorld::setClothWind: body {} is no cloth, or the acceleration is not finite", cloth);
        return;
    }
    found->second.wind = acceleration;
}

void PhysicsWorld::pushCloth(BodyId cloth, Vec3 velocity) {
    Impl& w = *impl_;
    const auto found = w.cloths.find(cloth);
    if (found == w.cloths.end() || !isFinite(velocity)) {
        logWarn("PhysicsWorld::pushCloth: body {} is no cloth, or the velocity is not finite", cloth);
        return;
    }
    if (found->second.frozen) {
        return;
    }
    JPH::Body& body = *w.joltBody(w.records.at(cloth));
    for (JPH::SoftBodyVertex& vertex : particlesOf(body).GetVertices()) {
        if (vertex.mInvMass > 0) {
            vertex.mVelocity += toJolt(velocity);
        }
    }
    w.bodies.ActivateBody(body.GetID());
}

void PhysicsWorld::clothIgnores(BodyId cloth, BodyId other) {
    const auto found = impl_->cloths.find(cloth);
    if (found != impl_->cloths.end()) {
        found->second.ignored.insert(other);
    }
}

void PhysicsWorld::clothIgnoresNothing(BodyId cloth) {
    const auto found = impl_->cloths.find(cloth);
    if (found != impl_->cloths.end()) {
        found->second.ignored.clear();
    }
}

// Whether a body that moves (is awake, and not part of what the cloth hangs from) has its box within a particle's reach of the cloth's.
bool PhysicsWorld::Impl::movingBodyNear(const JPH::Body& cloth, const ClothRecord& record) const {
    struct Collector final : JPH::CollideShapeBodyCollector {
        Collector(const PhysicsWorld::Impl& w, const ClothRecord& record) : world(w), record(record) {}
        void AddHit(const JPH::BodyID& id) override {
            const JPH::Body* other = world.system.GetBodyLockInterfaceNoLock().TryGetBody(id);
            if (other != nullptr && other->IsActive() && !record.ignored.contains(static_cast<BodyId>(other->GetUserData()))) {
                found = true;
                ForceEarlyOut();
            }
        }
        const PhysicsWorld::Impl& world;
        const ClothRecord& record;
        bool found = false;
    } collector(*this, record);
    JPH::AABox reach = cloth.GetWorldSpaceBounds();
    reach.ExpandBy(JPH::Vec3::sReplicate(kWakeReach));
    system.GetBroadPhaseQuery().CollideAABox(reach, collector, JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(layers::moving)),
                                             JPH::SpecifiedObjectLayerFilter(layers::moving));
    return collector.found;
}

void PhysicsWorld::Impl::driveCloths() {
    const float dt = settings.fixedStep;
    for (auto& [id, cloth] : cloths) {
        if (cloth.frozen) {
            continue;
        }
        JPH::Body& body = *joltBody(records.at(id));
        JPH::Array<JPH::SoftBodyVertex>& vertices = particlesOf(body).GetVertices();
        // The slack grows in, a straight line from none to all of it.
        if (!cloth.slackGrown) {
            cloth.age += dt;
            const float grown = cloth.slackSeconds > 0 ? std::min(cloth.age / cloth.slackSeconds, 1.0f) : 1.0f;
            for (std::size_t i = 0; i < cloth.restLength.size(); ++i) {
                cloth.shared->mEdgeConstraints[i].mRestLength = cloth.restLength[i] * (1 + cloth.slack * grown);
            }
            cloth.slackGrown = grown >= 1;
            body.SetAllowSleeping(cloth.slackGrown);
        }
        if (!bodies.IsActive(body.GetID())) {
            // Asleep it stays, wind and all, until a pin goes somewhere else or something that moves comes near.
            const bool pinMoved = std::ranges::any_of(cloth.pins, [&](const auto& pin) {
                return length(pin.second.position - fromJolt(vertices[static_cast<JPH::uint>(pin.first)].mPosition)) > kPinWakes;
            });
            if (!pinMoved && !movingBodyNear(body, cloth)) {
                continue;
            }
            bodies.ActivateBody(body.GetID());
        }
        // A pinned particle is a kinematic one in Jolt's terms: it goes where its velocity takes it, by the end of the step,
        // and the springs follow.
        for (auto& [particle, pin] : cloth.pins) {
            JPH::SoftBodyVertex& vertex = vertices[static_cast<JPH::uint>(particle)];
            vertex.mVelocity = (toJolt(pin.position) - vertex.mPosition) / dt;
            pin.velocity = fromJolt(vertex.mVelocity);
        }
        if (!(length(cloth.wind) > 0)) {
            continue;
        }
        // Each face takes the part of the wind that is square to it (its normal times the wind along the normal) and gives it
        // to its corners; a corner takes the mean of its faces', so that the sheet's speed does not depend on how finely it is cut.
        const JPH::Vec3 wind = toJolt(cloth.wind);
        JPH::Array<JPH::Vec3> sum(vertices.size(), JPH::Vec3::sZero());
        JPH::Array<int> faces(vertices.size(), 0);
        for (const std::array<std::uint32_t, 3>& face : cloth.faces) {
            const JPH::Vec3 a = vertices[face[0]].mPosition, b = vertices[face[1]].mPosition, c = vertices[face[2]].mPosition;
            const JPH::Vec3 across = (b - a).Cross(c - a);
            const float area2 = across.Length();
            if (!(area2 > 1e-9f)) {
                continue;
            }
            const JPH::Vec3 normal = across / area2;
            const JPH::Vec3 push = normal * (wind.Dot(normal) * dt);
            for (const std::uint32_t corner : face) {
                sum[corner] += push;
                ++faces[corner];
            }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (vertices[i].mInvMass > 0 && faces[i] > 0) {
                vertices[i].mVelocity += sum[i] / static_cast<float>(faces[i]);
            }
        }
    }
}

JPH::SoftBodyValidateResult PhysicsWorld::Impl::OnSoftBodyContactValidate(const JPH::Body& cloth, const JPH::Body& other,
                                                                          JPH::SoftBodyContactSettings& contact) {
    const auto found = cloths.find(static_cast<BodyId>(cloth.GetUserData()));
    if (found != cloths.end() && found->second.ignored.contains(static_cast<BodyId>(other.GetUserData()))) {
        return JPH::SoftBodyValidateResult::RejectContact;
    }
    // A particle of a few grams moves what it meets by nothing, but Jolt would wake the sleeping brick it brushed to give it that
    // nothing, and a brick woken settles where the level saved it hanging. So the cloth is the only one pushed.
    contact.mInvMassScale2 = 0;
    contact.mInvInertiaScale2 = 0;
    return JPH::SoftBodyValidateResult::AcceptContact;
}

void PhysicsWorld::Impl::OnSoftBodyContactAdded(const JPH::Body& cloth, const JPH::SoftBodyManifold& manifold) {
    const JPH::BodyLockInterfaceNoLock& locks = system.GetBodyLockInterfaceNoLock();
    std::vector<ClothTouch> found;
    for (const JPH::SoftBodyVertex& vertex : manifold.GetVertices()) {
        if (!manifold.HasContact(vertex)) {
            continue;
        }
        const JPH::Body* other = locks.TryGetBody(manifold.GetContactBodyID(vertex));
        if (other == nullptr) {
            continue;
        }
        const JPH::Vec3 towards = manifold.GetContactNormal(vertex);  // from the particle into what it touches
        const JPH::RVec3 point = cloth.GetCenterOfMassTransform() * manifold.GetLocalContactPoint(vertex);
        ClothTouch touch;
        touch.cloth = static_cast<BodyId>(cloth.GetUserData());
        touch.other = static_cast<BodyId>(other->GetUserData());
        touch.point = fromJoltPoint(point);
        touch.normal = fromJolt(towards);
        // The closing speed is the other body's along the normal, the particle counted as still: by the time Jolt reports the touch,
        // the particle has been pushed out of it and has taken its speed.
        touch.speed = std::max(0.0f, -other->GetPointVelocity(point).Dot(towards));
        const auto same = std::ranges::find(found, touch.other, &ClothTouch::other);
        if (same == found.end()) {
            found.push_back(touch);
        } else if (touch.speed > same->speed) {
            *same = touch;
        }
    }
    std::lock_guard lock(queueMutex);
    clothTouches.insert(clothTouches.end(), found.begin(), found.end());
}

int PhysicsWorld::clothResets(BodyId cloth) const {
    const auto found = impl_->cloths.find(cloth);
    return found == impl_->cloths.end() ? 0 : found->second.resets;
}

// Jolt's soft body solver is stable for what a cloth does in a game, but not for everything that is done to it: a pin that is dragged
// out of the world, a holder thrown at the speed limit. Whatever the cause, a particle that has left the world is the sign. The
// cloth goes back to where it last was whole, at rest; and one that keeps going is held where it is, which is a sail that no longer
// moves, instead of one that takes the level's frame time (or its NaN) with it.
void PhysicsWorld::Impl::guardCloths() {
    for (auto& [id, cloth] : cloths) {
        JPH::Array<JPH::SoftBodyVertex>& vertices = particlesOf(*joltBody(records.at(id))).GetVertices();
        const bool gone = std::ranges::any_of(vertices, [](const JPH::SoftBodyVertex& v) {
            return v.mPosition.IsNaN() || !(v.mPosition.Length() < kClothReach);
        });
        if (!gone) {
            for (std::size_t i = 0; i < vertices.size(); ++i) {
                cloth.sound[i] = vertices[i].mPosition;
            }
            continue;
        }
        if (++cloth.resets == 1) {
            logWarn("PhysicsWorld: cloth {} flew apart; its particles are put back where they were", id);
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            JPH::SoftBodyVertex& v = vertices[i];
            v.mPosition = v.mPreviousPosition = cloth.sound[i];
            v.mVelocity = JPH::Vec3::sZero();
            v.mInvMass = cloth.resets >= kMostResets ? 0.0f : v.mInvMass;
        }
        if (cloth.resets >= kMostResets) {
            cloth.frozen = true;
            cloth.pins.clear();
        }
    }
}

void PhysicsWorld::Impl::reportClothTouches() {
    std::vector<ClothTouch> touches;
    {
        std::lock_guard lock(queueMutex);
        touches.swap(clothTouches);
    }
    // Worker threads report in any order.
    std::ranges::sort(touches, [](const ClothTouch& x, const ClothTouch& y) {
        return x.cloth != y.cloth ? x.cloth < y.cloth : x.other < y.other;
    });
    for (auto& [id, cloth] : cloths) {
        std::set<BodyId> now;
        for (const ClothTouch& touch : touches) {
            if (touch.cloth != id) {
                continue;
            }
            now.insert(touch.other);
            if (cloth.touching.contains(touch.other) || contacts.size() >= kMaxQueuedContacts) {
                continue;
            }
            ContactEvent event;
            event.a = id;
            event.b = touch.other;
            event.userDataA = records.at(id).userData;
            if (const auto it = records.find(touch.other); it != records.end()) {
                event.userDataB = it->second.userData;
            }
            event.point = touch.point;
            event.normal = touch.normal;
            event.normalSpeed = touch.speed;
            std::lock_guard lock(queueMutex);
            contacts.push_back(event);
        }
        cloth.touching = std::move(now);
    }
}

}  // namespace mb
