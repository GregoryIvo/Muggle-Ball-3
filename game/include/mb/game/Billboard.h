#pragma once

#include "mb/engine/Renderer.h"

namespace mb {

// Flat things the scene draws with the quad of ModelCache::quad(): sparks, flashes and halos that face the game's camera,
// strips that join two points, and marks laid on the floor. None has a light, and none writes the depth.

// What turns the quad (in the xy plane, facing -z) to face the game's camera.
Mat4 facingGameCamera();

// A rectangle `size` across at `at`, facing the camera (`facing` is facingGameCamera()) and turned by `roll` radians about the
// line of sight. It has no depth test: it is drawn over the scene, blended as `blend` says.
DrawItem billboard(MeshHandle quad, const Mat4& facing, Vec3 at, Vec2 size, Color tint, TextureHandle texture, BlendMode blend,
                   float roll = 0);

// A strip `width` wide from `from` to `to` as the camera sees them (the segment's projection on the picture), over the scene.
DrawItem strip(MeshHandle quad, const Mat4& facing, Vec3 from, Vec3 to, float width, Color tint, TextureHandle texture,
               BlendMode blend);

// A square `size` across lying on the floor at `at`, turned by `turn` radians about the vertical. It is tested against the
// depth, so what stands on it hides it.
DrawItem floorMark(MeshHandle quad, Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend, float turn = 0);

}  // namespace mb
