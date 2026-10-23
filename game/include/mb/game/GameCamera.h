#pragma once

#include "mb/engine/Math.h"

namespace mb {

// The original's fixed camera, behind the bat and looking down at the board (about 35 degrees
// below the horizon, 55 from the vertical). These are the exact hard literals the original resets
// its camera to on every level load, recovered from its binary (game32, unpacked from the
// ASProtect + Alawar-wrapper + MoleBox layers): the level-load camera ctor at VA 0x454840 loads
// eye.y/eye.z from [0x4F18E8]/[0x4F18E4] and target.y/target.z from [0x4F18E0]/[0x4F18DC]; the
// projection builder at 0x492611 (a D3DXMatrixPerspectiveFovLH equivalent) takes fovY from
// [0x4F3328] = 0.5817764 rad and zf from [0x4F05A0]. (The earlier values here were a least-squares
// fit to original-level1-pirates.jpg; the fit had the ~35 degree view angle right but the eye,
// target and fov wrong, so it is replaced wholesale.) Every level uses this camera; the original's
// Options menu has a "SmartCamera" switch (yaw +-45 deg, pitch -11.25..+22.5 deg) that moves it,
// which is not implemented.
inline constexpr Vec3 kGameCameraEye{0.0f, 42.816f, -57.534f};
inline constexpr Vec3 kGameCameraTarget{0.0f, 4.217f, -3.314f};
inline constexpr float kGameCameraFovDegrees = 33.33333f;  // vertical, 0.5817764 rad = 100/3 deg
inline constexpr float kCameraNear = 1.0f;
inline constexpr float kCameraFar = 1000.0f;

// A look-at camera that can be moved around its target, starting from the game camera.
class Camera {
public:
    Camera() { reset(); }

    void reset();
    void lookAt(Vec3 eye, Vec3 target, float fovDegrees);

    // Pointer motion in window points.
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    // Scroll ticks, positive moves the camera closer to the target.
    void zoom(float ticks);

    Vec3 eye() const;
    Mat4 view() const;
    Mat4 projection(float aspect) const;

private:
    Vec3 target_;
    float distance_ = 1;
    float yaw_ = 0;    // radians around +y; 0 puts the eye on the target's -z side
    float pitch_ = 0;  // radians above the horizontal
    float fovDegrees_ = kGameCameraFovDegrees;
};

}  // namespace mb
