#pragma once

#include "mb/engine/Math.h"

namespace mb {

// The original's fixed camera, behind the bat and looking down at the board (about 35 degrees
// below the horizon, 55 from the vertical). These numbers were fitted to screenshots of the
// original game (800x600 downscales of 1024x768), not recovered from its binary: a least-squares
// fit of the sea board's rim in original-level1-pirates.jpg, then checked against the saloon
// screenshot, where objects land within one pixel of the original at 1024x768. Every level
// uses this camera; the original's Options menu has a "SmartCamera" switch that moves it, which
// is not implemented.
inline constexpr Vec3 kGameCameraEye{0.0f, 42.7f, -58.5f};
inline constexpr Vec3 kGameCameraTarget{0.0f, 0.0f, 2.7f};
inline constexpr float kGameCameraFovDegrees = 32.8f;  // vertical
inline constexpr float kCameraNear = 1.0f;
inline constexpr float kCameraFar = 500.0f;

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
