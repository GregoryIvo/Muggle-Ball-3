#pragma once

#include "mb/engine/Math.h"
#include "mb/game/ObjectType.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace mb {

using Mat3 = std::array<float, 9>;  // row-major, as stored by PhysX 2.x (NxMat33)

struct Pose {
    Mat3 rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 position{};
};

enum class ShapeType { Plane, Sphere, Box, Capsule, Convex, Mesh };

// NxShapeDesc and its subtypes. Field meanings: docs/LEVEL_FORMAT.md.
struct Shape {
    ShapeType type = ShapeType::Box;
    Pose localPose;
    Vec3 halfExtents{};  // Box
    float radius = 0;    // Sphere, Capsule
    float height = 0;    // Capsule: length of the cylinder part, along local +y
    std::string meshName;  // Convex, Mesh
    std::vector<Vec3> hullPoints;  // Convex: vertices of the cooked hull in Data/NxMeshes/<meshName>
    std::uint32_t shapeFlags = 0;   // NxShapeFlag
    std::uint16_t group = 0;        // collision group
    std::uint16_t materialIndex = 0;
    float mass = 0, density = 0;    // the level files do not store them: always 0, the actor's body gives the mass
    float skinWidth = -1;           // -1 = the scene default
};

// NxBodyDesc: present on actors that have a body (every actor in the shipped levels).
struct Body {
    Pose massLocalPose;
    Vec3 massSpaceInertia{};
    float mass = 0;
    Vec3 linearVelocity{};   // the levels were saved mid-simulation, so these are small but non-zero
    Vec3 angularVelocity{};
    float wakeUpCounter = 0.4f;
    float linearDamping = 0;
    float angularDamping = 0.05f;
    float maxAngularVelocity = 7;
    std::uint32_t flags = 0;  // NxBodyFlag, see the constants below
    float sleepLinearVelocity = 0.15f;
    float sleepAngularVelocity = 0.14f;
    std::uint32_t solverIterationCount = 4;

    static constexpr std::uint32_t kDisableGravity = 1u << 0;
    static constexpr std::uint32_t kFrozenPosX = 1u << 1, kFrozenPosY = 1u << 2, kFrozenPosZ = 1u << 3;
    static constexpr std::uint32_t kFrozenRotX = 1u << 4, kFrozenRotY = 1u << 5, kFrozenRotZ = 1u << 6;
    static constexpr std::uint32_t kKinematic = 1u << 7;

    bool kinematic() const { return (flags & kKinematic) != 0; }
};

struct Actor;

struct GameObject {
    ObjectType type = ObjectType::Unknown;
    std::uint32_t id = 0;
    std::uint32_t flags = 0;
    std::string material;
    std::string mesh;
    std::string frame;
    std::string prize;              // Prize: which power-up it drops
    std::vector<std::uint8_t> extra;   // flags & 0x200: 0x68 bytes (a D3DLIGHT8 for lights)
    std::vector<Actor> clothSegments;  // Cloth: the extra segment actors
};

struct Actor {
    Pose pose;
    std::optional<Body> body;  // absent: a static actor
    float density = 0;         // NxActorDesc::density, used when the body's mass is 0
    std::uint32_t actorFlags = 0;  // NxActorFlag
    std::uint16_t group = 0;       // actor group
    std::vector<Shape> shapes;
    std::optional<GameObject> object;
};

// PhysX 2.x NxJointType numbering. The shipped levels use Revolute, Spherical and Fixed.
enum class NxJointType : std::uint32_t {
    Prismatic = 0, Revolute = 1, Cylindrical = 2, Spherical = 3, PointOnLine = 4,
    PointInPlane = 5, Distance = 6, Pulley = 7, Fixed = 8, D6 = 9,
};

struct JointLimitDesc {
    float value = 0;        // radians
    float restitution = 0;
    float hardness = 1;
};

struct SpringDesc {
    float spring = 0, damper = 0, targetValue = 0;
};

// One serialized NxJointDesc (plus its NxRevoluteJointDesc / NxSphericalJointDesc part).
struct Joint {
    NxJointType type = NxJointType::Fixed;
    // Which actors it connects, as the file stores them; docs/LEVEL_FORMAT.md says how
    // they map to actors (and which value means the world).
    std::uint32_t actorA = 0, actorB = 0;
    Vec3 localNormal[2]{};  // per actor, in that actor's frame
    Vec3 localAxis[2]{};
    Vec3 localAnchor[2]{};
    float maxForce = 0, maxTorque = 0;  // break thresholds; FLT_MAX = unbreakable
    std::uint32_t jointFlags = 0;       // NxJointFlag: collision enabled 1, visualization 2. Bit 0 is clear in every shipped joint

    // Revolute: NX_RJF_LIMIT_ENABLED 1, NX_RJF_MOTOR_ENABLED 2, NX_RJF_SPRING_ENABLED 4.
    // Spherical: NX_SJF_TWIST_LIMIT_ENABLED 1, SWING_LIMIT 2, TWIST_SPRING 4, SWING_SPRING 8, JOINT_SPRING 16.
    std::uint32_t typeFlags = 0;
    JointLimitDesc limitLow, limitHigh;  // Revolute limit / Spherical twist limit
    JointLimitDesc swingLimit;           // Spherical
    Vec3 swingAxis{};                    // Spherical
    float motorVelTarget = 0, motorMaxForce = 0;  // Revolute
    bool motorFreeSpin = false;
    SpringDesc spring;                   // Revolute spring / Spherical joint spring
    SpringDesc twistSpring, swingSpring; // Spherical
    float projectionDistance = 0, projectionAngle = 0;
    std::uint32_t projectionMode = 0;
    std::vector<std::uint8_t> raw;       // the whole descriptor, for anything not decoded
};

struct PathPoint {
    Vec3 pos{}, tangent{}, dir{}, up{};
};

struct Path {
    std::string name;
    std::vector<PathPoint> points;
};

// The level JSON version this build reads, the one tools/mb3level.py writes (LEVEL_JSON_VERSION
// there; docs/LEVEL_FORMAT.md lists what each version changed).
inline constexpr int kLevelJsonVersion = 3;

struct Level {
    int version = kLevelJsonVersion;  // of the JSON it was loaded from; 1 when the file has none
    Vec3 gravity{};
    std::vector<Actor> actors;
    std::vector<Joint> joints;
    std::vector<Path> paths;

    std::size_t countObjects(ObjectType type) const;
};

}  // namespace mb
