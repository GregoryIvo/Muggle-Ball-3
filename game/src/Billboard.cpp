#include "mb/game/Billboard.h"

#include "mb/game/GameCamera.h"

#include <cmath>

namespace mb {

Mat4 facingGameCamera() {
    const Vec3 forward = normalize(kGameCameraTarget - kGameCameraEye);
    const Vec3 right = normalize(cross({0, 1, 0}, forward));
    const Vec3 up = cross(forward, right);
    Mat4 m;
    const Vec3 axes[3] = {right, up, forward};
    for (int i = 0; i < 3; ++i) {
        m.m[i][0] = axes[i].x;
        m.m[i][1] = axes[i].y;
        m.m[i][2] = axes[i].z;
    }
    return m;
}

namespace {

DrawItem flat(MeshHandle quad, const Mat4& world, Color tint, TextureHandle texture, BlendMode blend) {
    DrawItem item;
    item.mesh = quad;
    item.indexCount = 6;
    item.world = world;
    MaterialParams& m = item.material;
    m.diffuse = tint;
    m.texture = texture;
    m.blend = blend;
    m.cull = CullMode::None;
    m.lit = false;
    m.depthWrite = false;
    return item;
}

}  // namespace

DrawItem billboard(MeshHandle quad, const Mat4& facing, Vec3 at, Vec2 size, Color tint, TextureHandle texture, BlendMode blend,
                   float roll) {
    DrawItem item = flat(quad, Mat4::scaling({size.x, size.y, 1}) * Mat4::rotationZ(roll) * facing * Mat4::translation(at), tint,
                         texture, blend);
    item.material.depthTest = false;
    return item;
}

DrawItem strip(MeshHandle quad, const Mat4& facing, Vec3 from, Vec3 to, float width, Color tint, TextureHandle texture,
               BlendMode blend) {
    // The rows of `facing` are the picture's right, up and forward: the strip lies along the segment's right and up parts.
    const Vec3 right{facing.m[0][0], facing.m[0][1], facing.m[0][2]}, up{facing.m[1][0], facing.m[1][1], facing.m[1][2]};
    const Vec3 along = to - from;
    const float x = dot(along, right), y = dot(along, up);
    return billboard(quad, facing, (from + to) * 0.5f, {std::hypot(x, y), width}, tint, texture, blend, std::atan2(y, x));
}

DrawItem floorMark(MeshHandle quad, Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend, float turn) {
    return flat(quad, Mat4::scaling({size, size, 1}) * Mat4::rotationX(kPi / 2) * Mat4::rotationY(turn) * Mat4::translation(at),
                tint, texture, blend);
}

}  // namespace mb
