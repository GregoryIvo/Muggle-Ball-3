#pragma once

#include "mb/engine/Math.h"

namespace mb {

// The one vertex layout every mesh uses (32 bytes, interleaved). Matches the
// vertex shaders' input attributes 0, 1 and 2.
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;  // Direct3D convention: (0, 0) is the top-left of the texture
};
static_assert(sizeof(Vertex) == 32);

}  // namespace mb
