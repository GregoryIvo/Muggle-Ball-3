#include "mb/game/BallReach.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace mb {

namespace {

// Closer than this to an obstacle is blocked: a ball that only touches an edge is not stopped by it.
constexpr float kTouching = 1e-3f;

// What stops the ball, in world space: a shape of a static body.
struct Obstacle {
    PhysicsShapeType type = PhysicsShapeType::Box;
    Mat4 toLocal;        // box and hull: the world to the box's frame
    Vec3 half{};         // box and hull: its half extents (a hull is taken as the box round its points)
    Vec3 a{}, b{};       // sphere (a) and capsule (a, b): the centre and the ends of the axis
    float radius = 0;
    Bounds bounds;       // in world space

    // The distance from a point to the surface, negative inside.
    float distance(Vec3 p) const {
        switch (type) {
            case PhysicsShapeType::Sphere:
                return length(p - a) - radius;
            case PhysicsShapeType::Capsule: {
                const Vec3 axis = b - a;
                const float t = std::clamp(dot(p - a, axis) / std::max(dot(axis, axis), 1e-12f), 0.0f, 1.0f);
                return length(p - (a + axis * t)) - radius;
            }
            case PhysicsShapeType::Box:
            case PhysicsShapeType::ConvexHull: {
                const Vec3 q = transformPoint(p, toLocal);
                const Vec3 outside{std::max(std::fabs(q.x) - half.x, 0.0f), std::max(std::fabs(q.y) - half.y, 0.0f),
                                   std::max(std::fabs(q.z) - half.z, 0.0f)};
                const float inside = std::max({std::fabs(q.x) - half.x, std::fabs(q.y) - half.y, std::fabs(q.z) - half.z});
                return inside < 0 ? inside : length(outside);
            }
        }
        return 1e30f;
    }
};

std::vector<Obstacle> obstaclesOf(const BodyDesc& body, const Mat4& pose) {
    std::vector<Obstacle> out;
    for (const PhysicsShape& shape : body.shapes) {
        Obstacle o;
        o.type = shape.type;
        const Mat4 world = shape.localPose * pose;
        BodyDesc one;
        one.shapes = {shape};
        o.bounds = boundsOf(one, pose);
        switch (shape.type) {
            case PhysicsShapeType::Box:
                o.half = shape.halfExtents;
                o.toLocal = inverse(world);
                break;
            case PhysicsShapeType::Sphere:
                o.a = transformPoint({0, 0, 0}, world);
                o.radius = shape.radius;
                break;
            case PhysicsShapeType::Capsule:
                o.a = transformPoint({0, -shape.halfHeight, 0}, world);
                o.b = transformPoint({0, shape.halfHeight, 0}, world);
                o.radius = shape.radius;
                break;
            case PhysicsShapeType::ConvexHull: {
                if (shape.points.empty()) {
                    continue;
                }
                Vec3 low = shape.points.front(), high = low;
                for (const Vec3 p : shape.points) {
                    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                }
                o.half = (high - low) * 0.5f;
                o.toLocal = inverse(Mat4::translation((high + low) * 0.5f) * world);
                break;
            }
        }
        out.push_back(o);
    }
    return out;
}

}  // namespace

BallReach::BallReach(const PhysicsPlan& plan, const std::function<bool(ActorIndex)>& blocks, Vec3 start,
                     const Options& options)
    : cell_(options.cell), lowX_(options.low.x), lowZ_(options.low.z) {
    width_ = static_cast<std::size_t>(std::ceil((options.high.x - options.low.x) / cell_));
    depth_ = static_cast<std::size_t>(std::ceil((options.high.z - options.low.z) / cell_));
    std::vector<std::uint8_t> blocked(width_ * depth_, 0);
    const auto centreOf = [&](std::size_t i) {
        return Vec3{lowX_ + (static_cast<float>(i % width_) + 0.5f) * cell_, options.planeY,
                    lowZ_ + (static_cast<float>(i / width_) + 0.5f) * cell_};
    };

    // Only what the ball's centre plane, a ball's radius to either side, passes through can stop it.
    const float lowPlane = options.planeY - options.radius, highPlane = options.planeY + options.radius;
    const float reach = options.radius;
    for (ActorIndex i = 0; i < plan.actors.size(); ++i) {
        const ActorPlan& actor = plan.actors[i];
        if (!actor.simulated || actor.motion != MotionType::Static || !blocks(i)) {
            continue;
        }
        for (const Obstacle& o : obstaclesOf(actor.body, actor.pose)) {
            if (o.bounds.low.y >= highPlane - kTouching || o.bounds.high.y <= lowPlane + kTouching) {
                continue;  // over or under the ball
            }
            const std::ptrdiff_t x0 = std::max<std::ptrdiff_t>(column(o.bounds.low.x - reach), 0);
            const std::ptrdiff_t x1 = std::min(column(o.bounds.high.x + reach), static_cast<std::ptrdiff_t>(width_) - 1);
            const std::ptrdiff_t z0 = std::max<std::ptrdiff_t>(row(o.bounds.low.z - reach), 0);
            const std::ptrdiff_t z1 = std::min(row(o.bounds.high.z + reach), static_cast<std::ptrdiff_t>(depth_) - 1);
            for (std::ptrdiff_t z = z0; z <= z1; ++z) {
                for (std::ptrdiff_t x = x0; x <= x1; ++x) {
                    const std::size_t at = static_cast<std::size_t>(z) * width_ + static_cast<std::size_t>(x);
                    if (!blocked[at] && o.distance(centreOf(at)) < options.radius - kTouching) {
                        blocked[at] = 1;
                    }
                }
            }
        }
    }

    // Flood fill from the start, or from the open cell nearest to it.
    ground_.assign(width_ * depth_, 0);
    std::size_t first = blocked.size();
    const std::ptrdiff_t startX = column(start.x), startZ = row(start.z);
    if (startX >= 0 && startZ >= 0 && startX < static_cast<std::ptrdiff_t>(width_) && startZ < static_cast<std::ptrdiff_t>(depth_) &&
        !blocked[static_cast<std::size_t>(startZ) * width_ + static_cast<std::size_t>(startX)]) {
        first = static_cast<std::size_t>(startZ) * width_ + static_cast<std::size_t>(startX);
    } else {
        float best = 1e30f;
        for (std::size_t i = 0; i < blocked.size(); ++i) {
            const Vec3 away = centreOf(i) - start;
            if (!blocked[i] && away.x * away.x + away.z * away.z < best) {
                best = away.x * away.x + away.z * away.z;
                first = i;
            }
        }
    }
    if (first >= ground_.size()) {
        return;
    }
    std::deque<std::size_t> queue{first};
    ground_[first] = 1;
    while (!queue.empty()) {
        const std::size_t i = queue.front();
        queue.pop_front();
        const auto x = static_cast<std::ptrdiff_t>(i % width_), z = static_cast<std::ptrdiff_t>(i / width_);
        for (std::ptrdiff_t dz = -1; dz <= 1; ++dz) {
            for (std::ptrdiff_t dx = -1; dx <= 1; ++dx) {
                const std::ptrdiff_t nx = x + dx, nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= static_cast<std::ptrdiff_t>(width_) || nz >= static_cast<std::ptrdiff_t>(depth_)) {
                    continue;
                }
                const std::size_t n = static_cast<std::size_t>(nz) * width_ + static_cast<std::size_t>(nx);
                if (!blocked[n] && !ground_[n]) {
                    ground_[n] = 1;
                    queue.push_back(n);
                }
            }
        }
    }
}

std::ptrdiff_t BallReach::column(float x) const {
    return static_cast<std::ptrdiff_t>(std::floor((x - lowX_) / cell_));
}

std::ptrdiff_t BallReach::row(float z) const {
    return static_cast<std::ptrdiff_t>(std::floor((z - lowZ_) / cell_));
}

bool BallReach::contains(float x, float z) const {
    const std::ptrdiff_t c = column(x), r = row(z);
    return c >= 0 && r >= 0 && c < static_cast<std::ptrdiff_t>(width_) && r < static_cast<std::ptrdiff_t>(depth_) &&
           ground_[static_cast<std::size_t>(r) * width_ + static_cast<std::size_t>(c)] != 0;
}

bool BallReach::touches(float lowX, float lowZ, float highX, float highZ) const {
    const std::ptrdiff_t x0 = std::max<std::ptrdiff_t>(column(lowX), 0);
    const std::ptrdiff_t x1 = std::min(column(highX), static_cast<std::ptrdiff_t>(width_) - 1);
    const std::ptrdiff_t z0 = std::max<std::ptrdiff_t>(row(lowZ), 0);
    const std::ptrdiff_t z1 = std::min(row(highZ), static_cast<std::ptrdiff_t>(depth_) - 1);
    for (std::ptrdiff_t z = z0; z <= z1; ++z) {
        for (std::ptrdiff_t x = x0; x <= x1; ++x) {
            if (ground_[static_cast<std::size_t>(z) * width_ + static_cast<std::size_t>(x)]) {
                return true;
            }
        }
    }
    return false;
}

std::size_t BallReach::groundCells() const {
    return static_cast<std::size_t>(std::ranges::count(ground_, std::uint8_t{1}));
}

}  // namespace mb
