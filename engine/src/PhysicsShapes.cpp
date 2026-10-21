// Turns a BodyDesc into Jolt shapes, mass properties and motion settings.

#include "PhysicsImpl.h"

#include <Jolt/Physics/Body/MassProperties.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace mb {

namespace {

bool positive(float v) {
    return std::isfinite(v) && v > 0;
}

// Collision detection shrinks a convex shape by its convex radius and grows it back, which
// rounds the edges by that much. Jolt's default would swallow the thin plates some bricks are
// made of, so the radius is also capped at a quarter of the smallest half extent.
float convexRadius(float smallestHalfExtent) {
    return std::min(JPH::cDefaultConvexRadius, 0.25f * smallestHalfExtent);
}

JPH::RefConst<JPH::Shape> create(JPH::ShapeSettings& settings, std::string& error) {
    const JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        error = result.GetError().c_str();
        return nullptr;
    }
    return result.Get();
}

JPH::RefConst<JPH::Shape> makeConvex(const PhysicsShape& shape, float density, std::string& error) {
    const PhysicsMaterial& m = shape.material;
    if (!std::isfinite(m.staticFriction) || !std::isfinite(m.dynamicFriction) || !std::isfinite(m.restitution)) {
        error = "the material has a value that is not finite";
        return nullptr;
    }
    JPH::Ref<JPH::PhysicsMaterial> material = new Material(m);

    switch (shape.type) {
        case PhysicsShapeType::Box: {
            const Vec3 h = shape.halfExtents;
            if (!positive(h.x) || !positive(h.y) || !positive(h.z)) {
                error = "a box needs positive, finite half extents";
                return nullptr;
            }
            JPH::BoxShapeSettings settings(toJolt(h), convexRadius(std::min({h.x, h.y, h.z})), material);
            settings.mDensity = density;
            return create(settings, error);
        }
        case PhysicsShapeType::Sphere: {
            if (!positive(shape.radius)) {
                error = "a sphere needs a positive, finite radius";
                return nullptr;
            }
            JPH::SphereShapeSettings settings(shape.radius, material);
            settings.mDensity = density;
            return create(settings, error);
        }
        case PhysicsShapeType::Capsule: {
            // A capsule without a cylinder part is a sphere; Jolt makes that substitution itself.
            if (!positive(shape.radius) || !std::isfinite(shape.halfHeight) || shape.halfHeight < 0) {
                error = "a capsule needs a positive, finite radius and a finite half height of zero or more";
                return nullptr;
            }
            JPH::CapsuleShapeSettings settings(shape.halfHeight, shape.radius, material);
            settings.mDensity = density;
            return create(settings, error);
        }
        case PhysicsShapeType::ConvexHull: {
            if (shape.points.size() < 4) {
                error = std::format("a convex hull needs at least 4 points, got {}", shape.points.size());
                return nullptr;
            }
            JPH::Array<JPH::Vec3> points;
            points.reserve(shape.points.size());
            Vec3 low = shape.points[0], high = shape.points[0];
            for (const Vec3& p : shape.points) {
                if (!isFinite(p)) {
                    error = "a convex hull point is not finite";
                    return nullptr;
                }
                points.push_back(toJolt(p));
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
            const Vec3 size = high - low;
            JPH::ConvexHullShapeSettings settings(points, convexRadius(0.5f * std::min({size.x, size.y, size.z})),
                                                  material);
            settings.mDensity = density;
            JPH::RefConst<JPH::Shape> hull = create(settings, error);
            if (hull != nullptr && !(hull->GetVolume() > 0)) {
                error = "a convex hull needs volume (are its points all in one plane?)";
                return nullptr;
            }
            return hull;
        }
    }
    error = "unknown shape type";
    return nullptr;
}

bool isIdentity(const JoltPose& pose) {
    return pose.position.LengthSq() < 1e-12f && JPH::Vec3(pose.rotation.GetXYZ()).LengthSq() < 1e-12f;
}

JPH::EAllowedDOFs allowedDofs(const BodyDesc& desc) {
    constexpr JPH::EAllowedDOFs translation[3] = {JPH::EAllowedDOFs::TranslationX, JPH::EAllowedDOFs::TranslationY,
                                                  JPH::EAllowedDOFs::TranslationZ};
    constexpr JPH::EAllowedDOFs rotation[3] = {JPH::EAllowedDOFs::RotationX, JPH::EAllowedDOFs::RotationY,
                                               JPH::EAllowedDOFs::RotationZ};
    JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::All;
    for (int axis = 0; axis < 3; ++axis) {
        if (desc.lockTranslation[axis]) {
            dofs &= ~translation[axis];
        }
        if (desc.lockRotation[axis]) {
            dofs &= ~rotation[axis];
        }
    }
    return dofs;
}

JPH::EMotionType toJolt(MotionType motion) {
    switch (motion) {
        case MotionType::Static: return JPH::EMotionType::Static;
        case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
        case MotionType::Dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

}  // namespace

bool fullyLocked(const BodyDesc& desc) {
    return std::all_of(std::begin(desc.lockTranslation), std::end(desc.lockTranslation), [](bool b) { return b; }) &&
           std::all_of(std::begin(desc.lockRotation), std::end(desc.lockRotation), [](bool b) { return b; });
}

bool makeBodySettings(const BodyDesc& desc, JPH::BodyCreationSettings& out, std::string& error) {
    if (desc.shapes.empty()) {
        error = "a body needs at least one shape";
        return false;
    }
    JoltPose pose;
    if (!toJolt(desc.pose, pose)) {
        error = "the pose is not a finite rotation plus translation";
        return false;
    }
    if (!isFinite(desc.linearVelocity) || !isFinite(desc.angularVelocity)) {
        error = "the initial velocity is not finite";
        return false;
    }
    if (!(desc.linearDamping >= 0) || !(desc.angularDamping >= 0) || !std::isfinite(desc.linearDamping) ||
        !std::isfinite(desc.angularDamping) || !positive(desc.maxAngularVelocity)) {
        error = "damping must be zero or more and the maximum angular velocity positive";
        return false;
    }
    if (!std::isfinite(desc.mass) || desc.mass < 0 || !std::isfinite(desc.density) || !isFinite(desc.inertia) ||
        !std::isfinite(desc.sleepLinearVelocity)) {
        error = "mass, density, inertia or the sleep threshold is not usable";
        return false;
    }
    // A body that may not move or turn on any axis is a static one; Jolt cannot make a dynamic or
    // kinematic body of it (it divides by the zero inverse mass).
    const bool locked = fullyLocked(desc);
    const MotionType motion = locked ? MotionType::Static : desc.motion;
    const bool moves = motion != MotionType::Static;
    if (desc.mass == 0 && !positive(desc.density) && moves) {
        error = "a moving body needs a mass or a positive density";
        return false;
    }
    // Static bodies may still become dynamic later, so they get mass properties too.
    const float density = positive(desc.density) ? desc.density : 1.0f;

    std::vector<JPH::RefConst<JPH::Shape>> parts;
    std::vector<JoltPose> placements;
    for (std::size_t i = 0; i < desc.shapes.size(); ++i) {
        JoltPose placement;
        if (!toJolt(desc.shapes[i].localPose, placement)) {
            error = std::format("shape {}: the local pose is not a finite rotation plus translation", i);
            return false;
        }
        JPH::RefConst<JPH::Shape> part = makeConvex(desc.shapes[i], density, error);
        if (part == nullptr) {
            error = std::format("shape {}: {}", i, error);
            return false;
        }
        parts.push_back(std::move(part));
        placements.push_back(placement);
    }

    JPH::RefConst<JPH::Shape> shape;
    if (parts.size() == 1 && isIdentity(placements[0])) {
        shape = parts[0];
    } else if (parts.size() == 1) {
        JPH::RotatedTranslatedShapeSettings settings(JPH::Vec3(placements[0].position), placements[0].rotation,
                                                     parts[0]);
        shape = create(settings, error);
    } else {
        JPH::StaticCompoundShapeSettings settings;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            settings.AddShape(JPH::Vec3(placements[i].position), placements[i].rotation, parts[i]);
        }
        shape = create(settings, error);
    }
    if (shape == nullptr) {
        return false;
    }

    JPH::EOverrideMassProperties massMode = JPH::EOverrideMassProperties::CalculateMassAndInertia;
    JPH::MassProperties mass;
    if (desc.mass > 0) {
        JoltPose massFrame;
        if (!toJolt(desc.massLocalPose, massFrame)) {
            error = "massLocalPose is not a finite rotation plus translation";
            return false;
        }
        // The centre of mass goes where massLocalPose says, wherever the shapes would put it.
        const JPH::Vec3 offset = JPH::Vec3(massFrame.position) - shape->GetCenterOfMass();
        if (offset.LengthSq() > 1e-12f) {
            JPH::OffsetCenterOfMassShapeSettings settings(offset, shape);
            shape = create(settings, error);
            if (shape == nullptr) {
                return false;
            }
        }
        mass.mMass = desc.mass;
        if (desc.inertia.x > 0 && desc.inertia.y > 0 && desc.inertia.z > 0) {
            // PhysX gives the diagonal in the mass frame; Jolt wants the whole tensor in the body's axes.
            const JPH::Mat44 r = rotationToJolt(desc.massLocalPose);
            mass.mInertia = r * JPH::Mat44::sScale(toJolt(desc.inertia)) * r.Transposed();
            mass.mInertia(3, 3) = 1;
            massMode = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        } else {
            massMode = JPH::EOverrideMassProperties::CalculateInertia;  // a zero inertia means: from the shapes
        }
    }

    out = JPH::BodyCreationSettings(shape, pose.position, pose.rotation, toJolt(motion),
                                    moves ? layers::moving : layers::nonMoving);
    out.mAllowDynamicOrKinematic = !locked;  // setMotionType may turn any other body into any other kind
    out.mAllowedDOFs = locked ? JPH::EAllowedDOFs::All : allowedDofs(desc);
    out.mOverrideMassProperties = massMode;
    out.mMassPropertiesOverride = mass;
    // Jolt does not allow a body that is asleep to have a velocity (it would be moving).
    if (!desc.startAsleep) {
        out.mLinearVelocity = toJolt(desc.linearVelocity);
        out.mAngularVelocity = toJolt(desc.angularVelocity);
    }
    out.mLinearDamping = desc.linearDamping;
    out.mAngularDamping = desc.angularDamping;
    out.mMaxAngularVelocity = desc.maxAngularVelocity;
    out.mGravityFactor = desc.gravity ? 1.0f : 0.0f;
    out.mMotionQuality = desc.continuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    out.mAllowSleeping = desc.sleepLinearVelocity > 0;
    return true;
}

}  // namespace mb
