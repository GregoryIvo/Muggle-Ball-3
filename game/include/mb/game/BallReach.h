#pragma once

#include "mb/game/PhysicsScene.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mb {

// Where a level's ball can go. The ball is a sphere that stays on one plane; what stops it is the immovable parts of the
// level (the board's walls and floor, a raised platform, a pinned building), which the caller picks out of the physics
// plan. The ground it can be on is the part of the plane that is joined to where the bat is without going nearer to
// a wall than its own radius: a pocket it would not fit into, the far side of a wall and anything beyond the board's
// edge are not in it. The plane is cut into a grid of small cells and the cells flood-filled from the start.
//
// GameSession asks it which targets the ball can still get at (SessionOptions::ignoreUnreachable; docs/GAMEPLAY.md section 12).
class BallReach {
public:
    struct Options {
        float planeY = 0.6f;     // the plane the ball's centre stays on
        float radius = 0.6f;     // the ball's
        float cell = 0.2f;       // the size of a grid cell, units: a gap a ball fits through is a few cells wide
        Vec3 low{-45, 0, -20};   // the part of the plane that is looked at (x, z): from the board's near edge, below which
        Vec3 high{45, 0, 45};    // the ball is lost; a level with no walls ends at the sides
    };

    // `blocks(actor)` says whether a body of the plan (only static ones are asked about) is a permanent obstacle: one
    // the ball cannot get rid of, so not a brick. `start` is where the ball begins (the bat's end of the board); a
    // start that is blocked is moved to the nearest ground.
    BallReach(const PhysicsPlan& plan, const std::function<bool(ActorIndex)>& blocks, Vec3 start, const Options& options);

    // Whether a ball can be centred at (x, z).
    bool contains(float x, float z) const;
    // Whether the ball can be centred anywhere in the rectangle.
    bool touches(float lowX, float lowZ, float highX, float highZ) const;

    // How much of the plane is ground, in cells, and the plane's size in cells (for tests and logs).
    std::size_t groundCells() const;
    std::size_t cells() const { return width_ * depth_; }

private:
    float cell_;
    float lowX_, lowZ_;
    std::size_t width_ = 0, depth_ = 0;
    std::vector<std::uint8_t> ground_;  // by cell: 1 where the ball can be

    // The cell a coordinate is in, which is outside the grid (negative, or from its size on) beyond its edge.
    std::ptrdiff_t column(float x) const;
    std::ptrdiff_t row(float z) const;
};

}  // namespace mb
