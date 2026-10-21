// Joints: PhysX's per-body local frames become Jolt constraints, and constraints that carry more
// than their break thresholds are taken out again.

#include "PhysicsImpl.h"

#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace mb {

namespace {

// One end of a joint, in world space as the bodies stand when the joint is made. Jolt turns
// these into the centre-of-mass space of each body itself, which is where a body whose centre
// of mass is not at its frame's origin would otherwise go wrong.
struct JointEnd {
    JPH::Body* body = nullptr;  // Body::sFixedToWorld for the world
    JPH::RVec3 point = JPH::RVec3::sZero();
    JPH::Vec3 axis = JPH::Vec3::sAxisX();
    JPH::Vec3 normal = JPH::Vec3::sAxisY();
};

bool makeEnd(const PhysicsWorld::Impl& w, BodyId id, Vec3 anchor, Vec3 axis, Vec3 normal, JointEnd& out,
             std::string& error) {
    if (!isFinite(anchor) || !isFinite(axis) || !isFinite(normal) || !(length(axis) > 1e-6f)) {
        error = "a joint frame needs a finite anchor and a non-zero axis";
        return false;
    }
    JPH::RVec3 origin = JPH::RVec3::sZero();
    JPH::Quat rotation = JPH::Quat::sIdentity();
    if (id == 0) {
        out.body = &JPH::Body::sFixedToWorld;
    } else {
        const auto it = w.records.find(id);
        if (it == w.records.end()) {
            error = std::format("body {} does not exist", id);
            return false;
        }
        out.body = w.joltBody(it->second);
        origin = out.body->GetPosition();
        rotation = out.body->GetRotation();
    }
    out.point = origin + rotation * toJolt(anchor);
    out.axis = rotation * toJolt(axis).Normalized();
    // PhysX does not insist that the normal is square to the axis; Jolt does.
    JPH::Vec3 n = toJolt(normal);
    n -= toJolt(axis).Normalized() * n.Dot(toJolt(axis).Normalized());
    n = n.LengthSq() > 1e-8f ? n.Normalized() : toJolt(axis).Normalized().GetNormalizedPerpendicular();
    out.normal = rotation * n;
    return true;
}

// A [low, high] range in radians, ordered and kept inside +-pi.
struct AngleRange {
    float low = -JPH::JPH_PI, high = JPH::JPH_PI;
};

bool makeRange(bool enabled, const JointLimit& lowLimit, const JointLimit& highLimit, AngleRange& out,
               std::string& error) {
    out = {};
    if (!enabled) {
        return true;
    }
    if (!std::isfinite(lowLimit.value) || !std::isfinite(highLimit.value)) {
        error = "a joint limit is not finite";
        return false;
    }
    out.low = std::clamp(std::min(lowLimit.value, highLimit.value), -JPH::JPH_PI, JPH::JPH_PI);
    out.high = std::clamp(std::max(lowLimit.value, highLimit.value), -JPH::JPH_PI, JPH::JPH_PI);
    return true;
}

JPH::Ref<JPH::TwoBodyConstraint> makeHinge(const JointDesc& d, const JointEnd& a, const JointEnd& b,
                                           std::string& error) {
    AngleRange range;
    if (!makeRange(d.limitEnabled, d.limitLow, d.limitHigh, range, error)) {
        return nullptr;
    }
    if (!std::isfinite(d.motorVelocity) || std::isnan(d.motorMaxForce) || !std::isfinite(d.spring) ||
        !std::isfinite(d.damper) || !std::isfinite(d.springTarget)) {
        error = "a hinge motor or spring value is not finite";
        return nullptr;
    }
    // PhysX measures the angle from the normals and lets the limits sit anywhere in +-pi. Jolt
    // wants zero inside them, so the second normal is turned until the middle of the range is
    // zero and everything given as an angle (limits, spring target) moves by the same amount.
    const float shift = (range.low > 0 || range.high < 0) ? 0.5f * (range.low + range.high) : 0.0f;

    JPH::HingeConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = a.point;
    settings.mHingeAxis1 = a.axis;
    settings.mNormalAxis1 = a.normal;
    settings.mPoint2 = b.point;
    settings.mHingeAxis2 = b.axis;
    settings.mNormalAxis2 = JPH::Quat::sRotation(b.axis, -shift) * b.normal;
    // Jolt has no bounce off a limit, so JointLimit::restitution is not used.
    settings.mLimitsMin = range.low - shift;
    settings.mLimitsMax = range.high - shift;

    const float torqueLimit = std::clamp(d.motorMaxForce, 0.0f, std::numeric_limits<float>::max());
    if (d.motorEnabled) {
        settings.mMotorSettings.SetTorqueLimit(torqueLimit);
    } else if (d.springEnabled) {
        settings.mMotorSettings.mSpringSettings = JPH::SpringSettings(
            JPH::ESpringMode::StiffnessAndDamping, std::max(d.spring, 0.0f), std::max(d.damper, 0.0f));
        settings.mMotorSettings.SetTorqueLimit(std::numeric_limits<float>::max());
    }
    auto* hinge = static_cast<JPH::HingeConstraint*>(settings.Create(*a.body, *b.body));
    // Jolt's hinge has one motor: a velocity motor if asked for, else the spring as a position motor.
    if (d.motorEnabled) {
        hinge->SetMotorState(JPH::EMotorState::Velocity);
        hinge->SetTargetAngularVelocity(d.motorVelocity);
    } else if (d.springEnabled) {
        hinge->SetMotorState(JPH::EMotorState::Position);
        hinge->SetTargetAngle(d.springTarget - shift);
    }
    return hinge;
}

JPH::Ref<JPH::TwoBodyConstraint> makeBall(const JointDesc& d, const JointEnd& a, const JointEnd& b,
                                          std::string& error) {
    if (!d.limitEnabled && !d.swingLimitEnabled) {
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = a.point;
        settings.mPoint2 = b.point;
        return settings.Create(*a.body, *b.body);
    }
    AngleRange twist;
    if (!makeRange(d.limitEnabled, d.limitLow, d.limitHigh, twist, error)) {
        return nullptr;
    }
    float swing = JPH::JPH_PI;
    if (d.swingLimitEnabled) {
        if (!std::isfinite(d.swingLimit.value)) {
            error = "the swing limit is not finite";
            return nullptr;
        }
        swing = std::clamp(d.swingLimit.value, 0.0f, JPH::JPH_PI);
    }
    JPH::SwingTwistConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPosition1 = a.point;
    settings.mTwistAxis1 = a.axis;
    settings.mPlaneAxis1 = a.normal;
    settings.mPosition2 = b.point;
    settings.mTwistAxis2 = b.axis;
    settings.mPlaneAxis2 = b.normal;
    settings.mSwingType = JPH::ESwingType::Cone;
    settings.mNormalHalfConeAngle = swing;
    settings.mPlaneHalfConeAngle = swing;
    settings.mTwistMinAngle = twist.low;
    settings.mTwistMaxAngle = twist.high;
    return settings.Create(*a.body, *b.body);
}

JPH::Ref<JPH::TwoBodyConstraint> makeFixed(const JointEnd& a, const JointEnd& b) {
    JPH::FixedConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = false;
    settings.mPoint1 = a.point;
    settings.mAxisX1 = a.axis;
    settings.mAxisY1 = a.normal;
    settings.mPoint2 = b.point;
    settings.mAxisX2 = b.axis;
    settings.mAxisY2 = b.normal;
    return settings.Create(*a.body, *b.body);
}

JPH::Ref<JPH::TwoBodyConstraint> makeConstraint(const PhysicsWorld::Impl& w, const JointDesc& d, std::string& error) {
    if (d.bodyA == d.bodyB) {
        error = d.bodyA == 0 ? "a joint needs at least one body" : "a joint cannot connect a body to itself";
        return nullptr;
    }
    JointEnd a, b;
    if (!makeEnd(w, d.bodyA, d.localAnchor[0], d.localAxis[0], d.localNormal[0], a, error) ||
        !makeEnd(w, d.bodyB, d.localAnchor[1], d.localAxis[1], d.localNormal[1], b, error)) {
        return nullptr;
    }
    switch (d.type) {
        case JointType::Fixed: return makeFixed(a, b);
        case JointType::Hinge: return makeHinge(d, a, b, error);
        case JointType::Ball: return makeBall(d, a, b, error);
    }
    error = "unknown joint type";
    return nullptr;
}

// What the constraint pushed with in the last solve, as an impulse (force times time) and an
// angular impulse. Motors are left out: they are the joint working, not the joint straining.
void appliedImpulse(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) {
    linear = angular = 0;
    switch (constraint.GetSubType()) {
        case JPH::EConstraintSubType::Fixed: {
            const auto& c = static_cast<const JPH::FixedConstraint&>(constraint);
            linear = c.GetTotalLambdaPosition().Length();
            angular = c.GetTotalLambdaRotation().Length();
            break;
        }
        case JPH::EConstraintSubType::Point:
            linear = static_cast<const JPH::PointConstraint&>(constraint).GetTotalLambdaPosition().Length();
            break;
        case JPH::EConstraintSubType::Hinge: {
            const auto& c = static_cast<const JPH::HingeConstraint&>(constraint);
            const auto rotation = c.GetTotalLambdaRotation();
            const float limit = c.GetTotalLambdaRotationLimits();
            linear = c.GetTotalLambdaPosition().Length();
            angular = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] + limit * limit);
            break;
        }
        case JPH::EConstraintSubType::SwingTwist: {
            const auto& c = static_cast<const JPH::SwingTwistConstraint&>(constraint);
            const float twist = c.GetTotalLambdaTwist(), y = c.GetTotalLambdaSwingY(), z = c.GetTotalLambdaSwingZ();
            linear = c.GetTotalLambdaPosition().Length();
            angular = std::sqrt(twist * twist + y * y + z * z);
            break;
        }
        default: break;
    }
}

bool finiteLimit(float v) {
    return v < std::numeric_limits<float>::max();  // false for FLT_MAX, infinity and NaN: those never break
}

}  // namespace

void PhysicsWorld::Impl::invalidateContacts(BodyId a, BodyId b) {
    // Jolt reuses the last result for a pair of bodies that have not moved relative to each other,
    // so a pair that starts or stops being allowed to collide has to be told to look again.
    for (BodyId body : {a, b}) {
        bodies.InvalidateContactCache(records.at(body).id);
    }
}

void PhysicsWorld::Impl::destroyJoint(JointId id) {
    const auto it = joints.find(id);
    JointRecord& joint = it->second;
    system.RemoveConstraint(joint.constraint);
    for (BodyId body : {joint.bodyA, joint.bodyB}) {
        if (const auto record = records.find(body); record != records.end()) {
            std::erase(record->second.joints, id);
        }
    }
    if (joint.noCollide) {
        removeNoCollide(joint.bodyA, joint.bodyB);
        invalidateContacts(joint.bodyA, joint.bodyB);
    }
    joints.erase(it);
}

void PhysicsWorld::Impl::checkBreaks() {
    // Impulses are per solver step, which is what the collision step divides the fixed one into.
    const float dt = settings.fixedStep / static_cast<float>(settings.collisionSteps);
    std::vector<JointId> broken;
    for (const auto& [id, joint] : joints) {
        if (!joint.breakable || !joint.constraint->IsActive()) {
            continue;
        }
        float linear, angular;
        appliedImpulse(*joint.constraint, linear, angular);
        if (linear > joint.breakForce * dt || angular > joint.breakTorque * dt) {
            broken.push_back(id);
        }
    }
    std::sort(broken.begin(), broken.end());  // the map's order is arbitrary
    for (JointId id : broken) {
        destroyJoint(id);
        brokenJoints.push_back(id);
    }
}

JointId PhysicsWorld::addJoint(const JointDesc& desc) {
    Impl& w = *impl_;
    std::string error;
    JPH::Ref<JPH::TwoBodyConstraint> constraint = makeConstraint(w, desc, error);
    if (constraint == nullptr) {
        logWarn("PhysicsWorld::addJoint: {}", error);
        return 0;
    }
    w.system.AddConstraint(constraint);

    const JointId id = w.nextJoint++;
    JointRecord record;
    record.constraint = constraint;
    record.bodyA = desc.bodyA;
    record.bodyB = desc.bodyB;
    record.breakForce = desc.breakForce;
    record.breakTorque = desc.breakTorque;
    record.breakable = finiteLimit(desc.breakForce) || finiteLimit(desc.breakTorque);
    record.noCollide = !desc.collideConnected && desc.bodyA != 0 && desc.bodyB != 0;
    w.joints.emplace(id, std::move(record));
    for (BodyId body : {desc.bodyA, desc.bodyB}) {
        if (const auto it = w.records.find(body); it != w.records.end()) {
            it->second.joints.push_back(id);
        }
    }
    if (desc.bodyA != 0 && desc.bodyB != 0 && !desc.collideConnected) {
        w.addNoCollide(desc.bodyA, desc.bodyB);
        w.invalidateContacts(desc.bodyA, desc.bodyB);
    }
    // A joint between an awake body and a sleeping one pulls the sleeper in.
    {
        std::lock_guard lock(w.queueMutex);
        for (BodyId body : {desc.bodyA, desc.bodyB}) {
            if (const auto it = w.records.find(body); it != w.records.end() && w.bodies.IsActive(it->second.id)) {
                w.activated.push_back(body);
            }
        }
    }
    w.flushActivations();
    return id;
}

void PhysicsWorld::removeJoint(JointId joint) {
    Impl& w = *impl_;
    if (!w.joints.contains(joint)) {
        if (w.warnedJoints.size() < 64 && w.warnedJoints.insert(joint).second) {
            logWarn("PhysicsWorld::removeJoint: joint {} does not exist (never added, broken or removed); ignored",
                    joint);
        }
        return;
    }
    w.destroyJoint(joint);
}

}  // namespace mb
