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

struct Shape {
    ShapeType type = ShapeType::Box;
    Pose localPose;
    Vec3 halfExtents{};  // Box
    float radius = 0;    // Sphere, Capsule
    float height = 0;    // Capsule
    std::string meshName;  // Convex, Mesh
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
    std::vector<Actor> clothSegments;  // Cloth: the extra segment actors
};

struct Actor {
    Pose pose;
    bool dynamic = false;  // has a PhysX body descriptor
    std::vector<Shape> shapes;
    std::optional<GameObject> object;
};

struct PathPoint {
    Vec3 pos{}, tangent{}, dir{}, up{};
};

struct Path {
    std::string name;
    std::vector<PathPoint> points;
};

struct Level {
    Vec3 gravity{};
    std::vector<Actor> actors;
    std::vector<Path> paths;

    std::size_t countObjects(ObjectType type) const;
};

}  // namespace mb
