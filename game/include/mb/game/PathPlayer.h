#pragma once

#include "mb/engine/Math.h"
#include "mb/game/Level.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mb {

// The level's paths and the objects that move along them, decoded in docs/LEVEL_FORMAT.md ("Path motion"). Everything
// here is pure: no physics world, no GPU, no clock. PhysicsScene drives the path actors from it, step by step.

// A track's speed is in world units per millisecond (docs/LEVEL_FORMAT.md, "Path motion"): this many to a second.
inline constexpr double kPathUnitsPerSecond = 1000.0;

// Where an object on a path is and how it faces: the unit direction it travels (the object's +z) and a unit up
// perpendicular to it (its +y).
struct PathFrame {
    Vec3 position{};
    Vec3 dir{0, 0, 1};
    Vec3 up{0, 1, 0};
};

// The object's world frame for a PathFrame: a rigid Mat4 (rows x, y, z, then the position) with y = up, z = dir and
// x = up x dir, which is how the saved poses of the path actors sit on their path.
Mat4 pathFrameMatrix(const PathFrame& frame);

// The curve through a Path's points, walked by distance along it. Each segment between two points is a cubic Hermite
// curve whose tangents are the points' own when the path has them (Path::tangents) and else Catmull-Rom's: half the
// way from the point before to the point after, and for the ends of an open path the way to the one neighbour. A
// closed path has a segment from its last point back to its first. The distance is arc length (a table of 32 pieces
// per segment, refined by Newton's method inside a piece), so a steady rate along the curve is a steady speed even
// where it turns sharply. The direction is the curve's tangent; the up is the points' ups blended along the segment
// and made perpendicular to it.
class PathCurve {
public:
    PathCurve() = default;
    explicit PathCurve(const Path& path);

    // At least two points, and a length.
    bool usable() const { return segments_ > 0; }
    bool closed() const { return closed_; }
    float totalLength() const { return length_; }

    // The frame at a distance from the first point: a closed path wraps round (so any distance, negative too, is on
    // it), an open one stops at its ends. Default frame for a path that is not usable.
    PathFrame at(double distance) const;

    // The distance of the point of the curve nearest `position`. A `heading` (not zero) prefers where the curve runs
    // the same way, so that the crossing of a path with itself gives the branch an object travels on.
    double nearest(Vec3 position, Vec3 heading = {}) const;

private:
    struct Node {
        Vec3 position{}, tangent{}, up{0, 1, 0};
    };
    std::vector<Node> nodes_;
    bool closed_ = false;
    std::size_t segments_ = 0;
    std::vector<float> table_;  // distance at the steps of every segment, then the length
    float length_ = 0;

    PathFrame frame(std::size_t segment, float u) const;
    void locate(double distance, std::size_t& segment, float& u) const;
};

// An actor that follows a path.
struct PathMover {
    ActorIndex actor = kNoActor;
    std::size_t path = 0;  // index into PathPlayer::curve()
    double start = 0;      // distance along the path at time 0
    double speed = 0;      // units per second along the path
};

// The path actors of a level, and where each is a given time after the level began. An actor stands at time 0 at the
// point of its path nearest its key, and goes on at its track's speed: for ever round a closed path, to the end of an
// open one.
class PathPlayer {
public:
    PathPlayer() = default;
    explicit PathPlayer(const Level& level);

    std::span<const PathMover> movers() const { return movers_; }
    // Keys of the level that move nothing: they name a path that is missing or too short, an object that is no
    // actor, a speed that is not a number, or an actor an earlier key has.
    std::size_t unusedKeys() const { return unusedKeys_; }

    const PathCurve& curve(const PathMover& mover) const { return curves_[mover.path]; }
    PathFrame frame(const PathMover& mover, double seconds) const;
    // The actor's world pose: pathFrameMatrix(frame(mover, seconds)).
    Mat4 pose(const PathMover& mover, double seconds) const;

private:
    std::vector<PathCurve> curves_;  // by Level::paths
    std::vector<PathMover> movers_;
    std::size_t unusedKeys_ = 0;
};

}  // namespace mb
