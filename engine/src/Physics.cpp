#include "PhysicsImpl.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <thread>

namespace mb {

namespace {

std::mutex runtimeMutex;
int runtimeUsers = 0;

void joltTrace(const char* format, ...) {
    char text[512];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    logWarn("Jolt: {}", text);
}

// PhysX's defaults for how long, and how slowly, a body must move before it falls asleep.
constexpr float kTimeBeforeSleep = 0.4f;
constexpr float kDefaultSleepVelocity = 0.15f;

// Jolt lets touching bodies sink into each other by 2 cm so that contacts do not flicker on and off,
// which is a lot for bricks and plates down to 6.5 cm thick. 5 mm is still more than gravity moves
// a resting body in one fixed step (2.7 mm), so contacts keep holding.
constexpr float kPenetrationSlop = 0.005f;

// Jolt undoes an overlap by at most this much per solver iteration. Its 0.2 shoves bodies that a level saved
// overlapping (piles of logs, pieces that were joined and never collided) apart so hard that they fly, or are
// pushed into a thin floor; with 0.05 they part slowly.
constexpr float kMaxPenetrationRecovery = 0.05f;

// Bodies added since the last time the broad phase tree was rebuilt before that is worth doing
// again; a level loads hundreds of bodies at once, a spawned prize is one.
constexpr std::size_t kOptimizeAfterAdds = 64;

constexpr std::size_t kTempBytes = 16u << 20;

// Jolt's TempAllocatorImpl aborts the process when its block runs out; this one goes on from
// the heap instead, so a scene with more contacts than expected only gets slower.
class FallbackTempAllocator final : public JPH::TempAllocator {
public:
    explicit FallbackTempAllocator(std::size_t size)
        : size_(size), block_(static_cast<std::uint8_t*>(JPH::AlignedAllocate(size, JPH_RVECTOR_ALIGNMENT))) {}
    ~FallbackTempAllocator() override { JPH::AlignedFree(block_); }

    void* Allocate(JPH::uint bytes) override {
        if (bytes == 0) {
            return nullptr;
        }
        const std::size_t padded = JPH::AlignUp(static_cast<std::size_t>(bytes), JPH_RVECTOR_ALIGNMENT);
        if (top_ + padded > size_) {
            return JPH::AlignedAllocate(bytes, JPH_RVECTOR_ALIGNMENT);
        }
        void* address = block_ + top_;
        top_ += padded;
        return address;
    }

    void Free(void* address, JPH::uint) override {
        if (address == nullptr) {
            return;
        }
        auto* p = static_cast<std::uint8_t*>(address);
        if (p >= block_ && p < block_ + size_) {
            top_ = static_cast<std::size_t>(p - block_);  // Jolt frees in reverse order of allocating
        } else {
            JPH::AlignedFree(address);
        }
    }

private:
    std::size_t size_;
    std::uint8_t* block_;
    std::size_t top_ = 0;
};

// A step is a few tenths of a millisecond; with more than this many workers the time spent waking and waiting for
// them is more than they save (9 workers took twice as long as 3 on the biggest level, and used 9 cores).
constexpr int kMostAutomaticThreads = 3;

std::unique_ptr<JPH::JobSystem> makeJobSystem(int threads) {
    if (threads < 0) {
        threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 0, kMostAutomaticThreads);
    }
    if (threads == 0) {
        return std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    }
    return std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
}

PhysicsSettings sanitized(PhysicsSettings s) {
    if (!(s.fixedStep > 0) || !std::isfinite(s.fixedStep)) {
        logWarn("PhysicsWorld: fixedStep {} is not usable, using 1/60", s.fixedStep);
        s.fixedStep = 1.0f / 60.0f;
    }
    s.maxStepsPerCall = std::max(1, s.maxStepsPerCall);
    s.collisionSteps = std::max(1, s.collisionSteps);
    s.velocitySteps = std::max(1, s.velocitySteps);
    s.positionSteps = std::max(1, s.positionSteps);
    if (!(s.bounceThreshold >= 0)) {
        s.bounceThreshold = 0;
    }
    s.maxBodies = std::max(1, s.maxBodies);
    s.maxBodyPairs = std::max(1, s.maxBodyPairs);
    s.maxContactConstraints = std::max(1, s.maxContactConstraints);
    return s;
}

// PhysX combines each property by the higher-numbered of the two materials' modes.
float combine(CombineMode a, CombineMode b, float x, float y) {
    switch (std::max(a, b)) {
        case CombineMode::Average: return 0.5f * (x + y);
        case CombineMode::Min: return std::min(x, y);
        case CombineMode::Multiply: return x * y;
        case CombineMode::Max: return std::max(x, y);
    }
    return 0.5f * (x + y);
}

const PhysicsMaterial& materialOf(const JPH::Body& body, const JPH::SubShapeID& subShape) {
    static const PhysicsMaterial fallback;
    const auto* material = dynamic_cast<const Material*>(body.GetShape()->GetMaterial(subShape));
    return material != nullptr ? material->values : fallback;
}

// A cloth's particles ask for their contacts' materials with no sub shape, which a compound body cannot answer. So a pair with a
// cloth in it gets the cloth's own friction and restitution (ClothDesc::friction; none bounces), whatever the other body is.
const JPH::Body* clothOf(const JPH::Body& a, const JPH::Body& b) {
    return a.IsSoftBody() ? &a : (b.IsSoftBody() ? &b : nullptr);
}

// Jolt has a single friction coefficient per contact, so the dynamic one is used and the static
// one is ignored: resting bodies are held by the same coefficient that slows sliding ones.
float combinedFriction(const JPH::Body& a, const JPH::SubShapeID& subA, const JPH::Body& b,
                       const JPH::SubShapeID& subB) {
    if (const JPH::Body* cloth = clothOf(a, b)) {
        return cloth->GetFriction();
    }
    const PhysicsMaterial& x = materialOf(a, subA);
    const PhysicsMaterial& y = materialOf(b, subB);
    return std::max(0.0f, combine(x.frictionCombine, y.frictionCombine, x.dynamicFriction, y.dynamicFriction));
}

float combinedRestitution(const JPH::Body& a, const JPH::SubShapeID& subA, const JPH::Body& b,
                          const JPH::SubShapeID& subB) {
    if (const JPH::Body* cloth = clothOf(a, b)) {
        return cloth->GetRestitution();
    }
    const PhysicsMaterial& x = materialOf(a, subA);
    const PhysicsMaterial& y = materialOf(b, subB);
    return std::max(0.0f, combine(x.restitutionCombine, y.restitutionCombine, x.restitution, y.restitution));
}

std::uint64_t pairKey(BodyId a, BodyId b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

Vec3 toVec3(JPH::RVec3Arg v) {
    return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}

MotionType motionFromJolt(JPH::EMotionType motion) {
    switch (motion) {
        case JPH::EMotionType::Static: return MotionType::Static;
        case JPH::EMotionType::Kinematic: return MotionType::Kinematic;
        case JPH::EMotionType::Dynamic: return MotionType::Dynamic;
    }
    return MotionType::Static;
}

}  // namespace

JoltRuntime::JoltRuntime() {
    std::lock_guard lock(runtimeMutex);
    if (runtimeUsers++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = joltTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

JoltRuntime::~JoltRuntime() {
    std::lock_guard lock(runtimeMutex);
    if (--runtimeUsers == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

bool isFinite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool toJolt(const Mat4& pose, JoltPose& out) {
    const float(&m)[4][4] = pose.m;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!std::isfinite(m[i][j])) {
                return false;
            }
        }
    }
    // Rows of the 3x3 are the images of the axes: they must be orthonormal and right-handed.
    constexpr float tolerance = 1e-3f;
    const Vec3 r0{m[0][0], m[0][1], m[0][2]}, r1{m[1][0], m[1][1], m[1][2]}, r2{m[2][0], m[2][1], m[2][2]};
    if (std::fabs(dot(r0, r0) - 1) > tolerance || std::fabs(dot(r1, r1) - 1) > tolerance ||
        std::fabs(dot(r2, r2) - 1) > tolerance || std::fabs(dot(r0, r1)) > tolerance ||
        std::fabs(dot(r0, r2)) > tolerance || std::fabs(dot(r1, r2)) > tolerance || dot(cross(r0, r1), r2) < 0) {
        return false;
    }
    out.rotation = rotationToJolt(pose).GetQuaternion().Normalized();
    out.position = JPH::RVec3(m[3][0], m[3][1], m[3][2]);
    return true;
}

JPH::Mat44 rotationToJolt(const Mat4& pose) {
    const float(&m)[4][4] = pose.m;
    return JPH::Mat44(JPH::Vec4(m[0][0], m[0][1], m[0][2], 0), JPH::Vec4(m[1][0], m[1][1], m[1][2], 0),
                      JPH::Vec4(m[2][0], m[2][1], m[2][2], 0), JPH::Vec4(0, 0, 0, 1));
}

Mat4 fromJolt(JPH::RVec3Arg position, JPH::QuatArg rotation) {
    const JPH::Mat44 r = JPH::Mat44::sRotation(rotation);
    Mat4 out;
    for (int i = 0; i < 3; ++i) {
        const JPH::Vec3 axis = r.GetColumn3(i);
        out.m[i][0] = axis.GetX();
        out.m[i][1] = axis.GetY();
        out.m[i][2] = axis.GetZ();
    }
    out.m[3][0] = static_cast<float>(position.GetX());
    out.m[3][1] = static_cast<float>(position.GetY());
    out.m[3][2] = static_cast<float>(position.GetZ());
    return out;
}

PhysicsWorld::Impl::Impl(Vec3 gravity, const PhysicsSettings& requested)
    : settings(sanitized(requested)),
      tempAllocator(std::make_unique<FallbackTempAllocator>(kTempBytes)),
      jobSystem(makeJobSystem(settings.threads)),
      bodies(system.GetBodyInterfaceNoLock()) {
    system.Init(static_cast<JPH::uint>(settings.maxBodies), 0, static_cast<JPH::uint>(settings.maxBodyPairs),
                static_cast<JPH::uint>(settings.maxContactConstraints), broadPhaseLayers, objectVsBroadPhase,
                objectPairs);
    JPH::PhysicsSettings joltSettings = system.GetPhysicsSettings();
    joltSettings.mNumVelocitySteps = static_cast<JPH::uint>(settings.velocitySteps);
    joltSettings.mNumPositionSteps = static_cast<JPH::uint>(settings.positionSteps);
    joltSettings.mMinVelocityForRestitution = settings.bounceThreshold;
    joltSettings.mPenetrationSlop = kPenetrationSlop;
    joltSettings.mMaxPenetrationDistance = kMaxPenetrationRecovery;
    joltSettings.mTimeBeforeSleep = kTimeBeforeSleep;
    joltSettings.mPointVelocitySleepThreshold = kDefaultSleepVelocity;
    system.SetPhysicsSettings(joltSettings);
    system.SetGravity(toJolt(gravity));
    system.SetContactListener(this);
    system.SetSoftBodyContactListener(this);
    system.SetBodyActivationListener(this);
    system.SetCombineFriction(combinedFriction);
    system.SetCombineRestitution(combinedRestitution);
}

PhysicsWorld::Impl::~Impl() {
    for (auto& [id, joint] : joints) {
        system.RemoveConstraint(joint.constraint);
    }
    joints.clear();
    std::vector<JPH::BodyID> ids;
    ids.reserve(records.size());
    for (const auto& [id, record] : records) {
        ids.push_back(record.id);
    }
    if (!ids.empty()) {
        bodies.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
        bodies.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
    }
}

const BodyRecord* PhysicsWorld::Impl::find(BodyId id, const char* what) const {
    const auto it = records.find(id);
    if (it != records.end()) {
        return &it->second;
    }
    if (warnedBodies.size() < 64 && warnedBodies.insert(id).second) {
        logWarn("PhysicsWorld::{}: body {} does not exist (never added, or removed); ignored", what, id);
    }
    return nullptr;
}

JPH::Body* PhysicsWorld::Impl::joltBody(const BodyRecord& record) const {
    return system.GetBodyLockInterfaceNoLock().TryGetBody(record.id);
}

void PhysicsWorld::Impl::updateSleepThreshold() {
    // Jolt's sleep test is one global limit on the speed of three points of a body (its centre of
    // mass and two points out on its extent), not a per-body one. So the smallest
    // sleepLinearVelocity of any body applies to all of them; sleepAngularVelocity has no
    // counterpart and the spin of a body is judged by the speed of those outer points instead.
    // A body that asks for zero or less is never allowed to sleep (mAllowSleeping).
    const float wanted = sleepThresholds.empty() ? kDefaultSleepVelocity : *sleepThresholds.begin();
    JPH::PhysicsSettings joltSettings = system.GetPhysicsSettings();
    if (joltSettings.mPointVelocitySleepThreshold != wanted) {
        joltSettings.mPointVelocitySleepThreshold = wanted;
        system.SetPhysicsSettings(joltSettings);
    }
}

void PhysicsWorld::Impl::addNoCollide(BodyId a, BodyId b) {
    ++noCollide[pairKey(a, b)];
}

void PhysicsWorld::Impl::removeNoCollide(BodyId a, BodyId b) {
    const auto it = noCollide.find(pairKey(a, b));
    if (it != noCollide.end() && --it->second == 0) {
        noCollide.erase(it);
    }
}

void PhysicsWorld::Impl::applyKinematicTargets() {
    // What moved in the last step and has no new target stops where it is, instead of coasting
    // on the velocity that carried it to the previous target.
    for (BodyId id : kinematicMoving) {
        const auto it = records.find(id);
        if (it != records.end() && !kinematicTargets.contains(id) &&
            bodies.GetMotionType(it->second.id) == JPH::EMotionType::Kinematic) {
            bodies.SetLinearAndAngularVelocity(it->second.id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }
    }
    kinematicMoving.clear();
    for (const auto& [id, target] : kinematicTargets) {
        bodies.MoveKinematic(records.at(id).id, target.position, target.rotation, settings.fixedStep);
        kinematicMoving.push_back(id);
    }
    kinematicTargets.clear();
}

void PhysicsWorld::Impl::flushActivations() {
    // Jolt treats a sleeping body on the end of a constraint as immovable, so whatever is joined
    // to a body that woke up has to wake with it, and so on down the chain.
    std::vector<BodyId> pending;
    for (;;) {
        {
            std::lock_guard lock(queueMutex);
            pending.swap(activated);
            activated.clear();
        }
        if (pending.empty()) {
            return;
        }
        for (BodyId id : pending) {
            const auto it = records.find(id);
            if (it == records.end()) {
                continue;
            }
            for (JointId jointId : it->second.joints) {
                const JointRecord& joint = joints.at(jointId);
                const auto other = records.find(joint.bodyA == id ? joint.bodyB : joint.bodyA);
                if (other != records.end() && !bodies.IsActive(other->second.id) &&
                    bodies.GetMotionType(other->second.id) == JPH::EMotionType::Dynamic) {
                    bodies.ActivateBody(other->second.id);  // lands in OnBodyActivated, so the chain goes on
                }
            }
        }
        pending.clear();
    }
}

void PhysicsWorld::Impl::fixedStep() {
    if (addedSinceOptimize >= kOptimizeAfterAdds) {
        system.OptimizeBroadPhase();
        addedSinceOptimize = 0;
    }
    flushActivations();
    applyKinematicTargets();
    driveCloths();

    const std::size_t firstNew = contacts.size();
    const JPH::EPhysicsUpdateError error =
        system.Update(settings.fixedStep, settings.collisionSteps, tempAllocator.get(), jobSystem.get());
    if (error != JPH::EPhysicsUpdateError::None && !warnedUpdateError) {
        warnedUpdateError = true;
        logWarn("PhysicsWorld: Jolt ran out of room (error flags {}); raise maxBodyPairs / maxContactConstraints",
                static_cast<int>(error));
    }
    guardCloths();
    reportClothTouches();
    // Worker threads report in any order; sort what this step produced so runs compare equal.
    std::stable_sort(contacts.begin() + static_cast<std::ptrdiff_t>(firstNew), contacts.end(),
                     [](const ContactEvent& x, const ContactEvent& y) {
                         return x.a != y.a ? x.a < y.a : x.b < y.b;
                     });
    checkBreaks();
}

JPH::ValidateResult PhysicsWorld::Impl::OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg,
                                                          const JPH::CollideShapeResult&) {
    if (!noCollide.empty() &&
        noCollide.contains(pairKey(static_cast<BodyId>(a.GetUserData()), static_cast<BodyId>(b.GetUserData())))) {
        return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    }
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void PhysicsWorld::Impl::OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                                        JPH::ContactSettings&) {
    const auto count = static_cast<JPH::uint>(manifold.mRelativeContactPointsOn1.size());
    if (count == 0) {
        return;
    }
    // The middle of the contact patch, halfway between the two surfaces when they overlap.
    JPH::Vec3 sum = JPH::Vec3::sZero();
    for (JPH::uint i = 0; i < count; ++i) {
        sum += manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i];
    }
    const JPH::RVec3 point = manifold.mBaseOffset + sum / (2.0f * static_cast<float>(count));
    const JPH::Vec3 velocityA = a.GetPointVelocity(point);  // zero for a static body
    const JPH::Vec3 velocityB = b.GetPointVelocity(point);

    ContactEvent event;
    event.a = static_cast<BodyId>(a.GetUserData());
    event.b = static_cast<BodyId>(b.GetUserData());
    if (const auto it = records.find(event.a); it != records.end()) {
        event.userDataA = it->second.userData;
    }
    if (const auto it = records.find(event.b); it != records.end()) {
        event.userDataB = it->second.userData;
    }
    event.point = toVec3(point);
    event.normal = fromJolt(manifold.mWorldSpaceNormal);
    event.normalSpeed = (velocityA - velocityB).Dot(manifold.mWorldSpaceNormal);

    std::lock_guard lock(queueMutex);
    if (contacts.size() < kMaxQueuedContacts) {
        contacts.push_back(event);
    } else if (!contactsDropped) {
        contactsDropped = true;
        logWarn("PhysicsWorld: {} contact events are waiting to be taken; dropping new ones", kMaxQueuedContacts);
    }
}

void PhysicsWorld::Impl::OnBodyActivated(const JPH::BodyID&, JPH::uint64 userData) {
    if (joints.empty()) {
        return;
    }
    const auto it = records.find(static_cast<BodyId>(userData));
    if (it != records.end() && !it->second.joints.empty()) {
        std::lock_guard lock(queueMutex);
        activated.push_back(it->first);
    }
}

PhysicsWorld::PhysicsWorld(Vec3 gravity, const PhysicsSettings& settings)
    : impl_(std::make_unique<Impl>(gravity, settings)) {}

PhysicsWorld::~PhysicsWorld() = default;

BodyId PhysicsWorld::addBody(const BodyDesc& desc) {
    Impl& w = *impl_;
    JPH::BodyCreationSettings settings;
    std::string error;
    if (!makeBodySettings(desc, settings, error)) {
        logWarn("PhysicsWorld::addBody: {}", error);
        return 0;
    }
    const BodyId id = w.nextBody;
    settings.mUserData = id;  // Jolt's own user data holds our id; the caller's lives in the record
    JPH::Body* body = w.bodies.CreateBody(settings);
    if (body == nullptr) {
        logWarn("PhysicsWorld::addBody: out of bodies (PhysicsSettings::maxBodies is {})", w.settings.maxBodies);
        return 0;
    }
    ++w.nextBody;
    const bool moves = settings.mMotionType != JPH::EMotionType::Static;
    w.bodies.AddBody(body->GetID(),
                     moves && !desc.startAsleep ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    ++w.addedSinceOptimize;

    BodyRecord record;
    record.id = body->GetID();
    record.userData = desc.userData;
    record.locked = fullyLocked(desc);
    if (moves && desc.sleepLinearVelocity > 0) {
        record.sleepThreshold = desc.sleepLinearVelocity;
        w.sleepThresholds.insert(record.sleepThreshold);
        w.updateSleepThreshold();
    }
    w.records.emplace(id, std::move(record));
    return id;
}

void PhysicsWorld::removeBody(BodyId body) {
    Impl& w = *impl_;
    BodyRecord* record = w.find(body, "removeBody");
    if (record == nullptr) {
        return;
    }
    while (!record->joints.empty()) {
        w.destroyJoint(record->joints.back());
    }
    w.kinematicTargets.erase(body);
    w.cloths.erase(body);
    if (record->sleepThreshold > 0) {
        w.sleepThresholds.erase(w.sleepThresholds.find(record->sleepThreshold));
        w.updateSleepThreshold();
    }
    w.bodies.RemoveBody(record->id);
    w.bodies.DestroyBody(record->id);
    w.records.erase(body);
}

int PhysicsWorld::step(float dt, const std::function<void()>& beforeStep) {
    Impl& w = *impl_;
    if (!(dt > 0) || !std::isfinite(dt)) {
        return 0;
    }
    // A step that falls short of the fixed one by rounding only should still happen.
    const double fixed = w.settings.fixedStep;
    const double epsilon = fixed * 1e-5;
    w.accumulator += dt;
    int steps = 0;
    while (w.accumulator + epsilon >= fixed && steps < w.settings.maxStepsPerCall) {
        if (beforeStep) {
            beforeStep();
        }
        w.fixedStep();
        w.accumulator = std::max(0.0, w.accumulator - fixed);
        ++steps;
    }
    if (w.accumulator + epsilon >= fixed) {
        w.accumulator = std::fmod(w.accumulator, fixed);  // too far behind: drop whole steps, keep the remainder
    }
    return steps;
}

Mat4 PhysicsWorld::pose(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "pose");
    if (record == nullptr) {
        return {};
    }
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->bodies.GetPositionAndRotation(record->id, position, rotation);
    return fromJolt(position, rotation);
}

Vec3 PhysicsWorld::linearVelocity(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "linearVelocity");
    return record != nullptr ? fromJolt(impl_->bodies.GetLinearVelocity(record->id)) : Vec3{};
}

Vec3 PhysicsWorld::angularVelocity(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "angularVelocity");
    return record != nullptr ? fromJolt(impl_->bodies.GetAngularVelocity(record->id)) : Vec3{};
}

void PhysicsWorld::setLinearVelocity(BodyId body, Vec3 velocity) {
    const BodyRecord* record = impl_->find(body, "setLinearVelocity");
    if (record == nullptr) {
        return;
    }
    if (!isFinite(velocity)) {
        logWarn("PhysicsWorld::setLinearVelocity: velocity is not finite; ignored");
        return;
    }
    impl_->bodies.SetLinearVelocity(record->id, toJolt(velocity));
    impl_->flushActivations();
}

void PhysicsWorld::setAngularVelocity(BodyId body, Vec3 velocity) {
    const BodyRecord* record = impl_->find(body, "setAngularVelocity");
    if (record == nullptr) {
        return;
    }
    if (!isFinite(velocity)) {
        logWarn("PhysicsWorld::setAngularVelocity: velocity is not finite; ignored");
        return;
    }
    impl_->bodies.SetAngularVelocity(record->id, toJolt(velocity));
    impl_->flushActivations();
}

void PhysicsWorld::addImpulse(BodyId body, Vec3 impulse, Vec3 worldPoint) {
    const BodyRecord* record = impl_->find(body, "addImpulse");
    if (record == nullptr) {
        return;
    }
    if (!isFinite(impulse) || !isFinite(worldPoint)) {
        logWarn("PhysicsWorld::addImpulse: impulse or point is not finite; ignored");
        return;
    }
    impl_->bodies.AddImpulse(record->id, toJolt(impulse), JPH::RVec3(worldPoint.x, worldPoint.y, worldPoint.z));
    impl_->flushActivations();
}

void PhysicsWorld::setPose(BodyId body, const Mat4& pose) {
    const BodyRecord* record = impl_->find(body, "setPose");
    if (record == nullptr) {
        return;
    }
    JoltPose target;
    if (!toJolt(pose, target)) {
        logWarn("PhysicsWorld::setPose: the pose is not a finite rotation plus translation; ignored");
        return;
    }
    impl_->bodies.SetPositionAndRotation(record->id, target.position, target.rotation, JPH::EActivation::Activate);
    impl_->flushActivations();
}

void PhysicsWorld::setKinematicTarget(BodyId body, const Mat4& pose) {
    Impl& w = *impl_;
    const BodyRecord* record = w.find(body, "setKinematicTarget");
    if (record == nullptr) {
        return;
    }
    JoltPose target;
    if (!toJolt(pose, target)) {
        logWarn("PhysicsWorld::setKinematicTarget: the pose is not a finite rotation plus translation; ignored");
        return;
    }
    if (w.bodies.GetMotionType(record->id) != JPH::EMotionType::Kinematic) {
        logWarn("PhysicsWorld::setKinematicTarget: body {} is not kinematic; ignored", body);
        return;
    }
    w.kinematicTargets[body] = {target.position, target.rotation};
}

MotionType PhysicsWorld::motionType(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "motionType");
    return record != nullptr ? motionFromJolt(impl_->bodies.GetMotionType(record->id)) : MotionType::Static;
}

void PhysicsWorld::setMotionType(BodyId body, MotionType motion) {
    Impl& w = *impl_;
    const BodyRecord* record = w.find(body, "setMotionType");
    if (record == nullptr || motion == motionFromJolt(w.bodies.GetMotionType(record->id))) {
        return;
    }
    if (record->locked) {
        logWarn("PhysicsWorld::setMotionType: body {} is locked on every axis, so it stays static; ignored", body);
        return;
    }
    if (motion == MotionType::Static) {
        w.bodies.SetMotionType(record->id, JPH::EMotionType::Static, JPH::EActivation::DontActivate);
        w.bodies.SetObjectLayer(record->id, layers::nonMoving);
    } else {
        w.bodies.SetObjectLayer(record->id, layers::moving);
        w.bodies.SetMotionType(record->id,
                               motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                               : JPH::EMotionType::Dynamic,
                               JPH::EActivation::Activate);
    }
    if (motion == MotionType::Kinematic) {
        // Jolt keeps the velocity a body had as a dynamic one, and a kinematic body would drift on it.
        w.bodies.SetLinearAndAngularVelocity(record->id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    } else {
        w.kinematicTargets.erase(body);
    }
    w.flushActivations();
}

bool PhysicsWorld::isAwake(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "isAwake");
    return record != nullptr && impl_->bodies.IsActive(record->id);
}

void PhysicsWorld::wake(BodyId body) {
    const BodyRecord* record = impl_->find(body, "wake");
    if (record == nullptr) {
        return;
    }
    impl_->bodies.ActivateBody(record->id);
    impl_->flushActivations();
}

std::uint64_t PhysicsWorld::userData(BodyId body) const {
    const BodyRecord* record = impl_->find(body, "userData");
    return record != nullptr ? record->userData : 0;
}

std::vector<ContactEvent> PhysicsWorld::takeContacts() {
    std::lock_guard lock(impl_->queueMutex);
    std::vector<ContactEvent> taken;
    taken.swap(impl_->contacts);
    impl_->contactsDropped = false;
    return taken;
}

std::vector<JointId> PhysicsWorld::takeBrokenJoints() {
    std::vector<JointId> taken;
    taken.swap(impl_->brokenJoints);
    return taken;
}

std::optional<RayHit> PhysicsWorld::raycast(Vec3 origin, Vec3 direction, float maxDistance) const {
    const Impl& w = *impl_;
    const float directionLength = length(direction);
    if (!isFinite(origin) || !isFinite(direction) || !(directionLength > 0) || !(maxDistance > 0) ||
        !std::isfinite(maxDistance)) {
        return std::nullopt;
    }
    const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                            toJolt(direction * (maxDistance / directionLength)));
    JPH::RayCastResult hit;
    if (!w.system.GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return std::nullopt;
    }
    const JPH::BodyLockRead lock(w.system.GetBodyLockInterfaceNoLock(), hit.mBodyID);
    if (!lock.Succeeded()) {
        return std::nullopt;
    }
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    RayHit result;
    result.body = static_cast<BodyId>(lock.GetBody().GetUserData());
    result.point = toVec3(point);
    result.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    result.distance = hit.mFraction * maxDistance;
    return result;
}

const PhysicsSettings& PhysicsWorld::settings() const {
    return impl_->settings;
}

std::size_t PhysicsWorld::bodyCount() const {
    return impl_->records.size();
}

std::size_t PhysicsWorld::awakeBodyCount() const {
    return impl_->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

std::size_t PhysicsWorld::jointCount() const {
    return impl_->joints.size();
}

}  // namespace mb
