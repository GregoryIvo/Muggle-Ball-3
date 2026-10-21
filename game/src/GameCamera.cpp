#include "mb/game/GameCamera.h"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

constexpr float kOrbitRadiansPerPoint = 0.005f;
constexpr float kZoomPerTick = 0.1f;  // fraction of the distance
constexpr float kMaxPitch = radians(89.0f);
constexpr float kMinDistance = 1.0f;
constexpr float kMaxDistance = 400.0f;

}  // namespace

void Camera::reset() {
    lookAt(kGameCameraEye, kGameCameraTarget, kGameCameraFovDegrees);
}

void Camera::lookAt(Vec3 eye, Vec3 target, float fovDegrees) {
    const Vec3 offset = eye - target;
    target_ = target;
    distance_ = std::max(length(offset), 1e-3f);
    pitch_ = std::asin(std::clamp(offset.y / distance_, -1.0f, 1.0f));
    // The eye sits at -z of the target when yaw is 0, so the horizontal direction is (sin, -cos).
    yaw_ = std::atan2(offset.x, -offset.z);
    fovDegrees_ = fovDegrees;
}

void Camera::orbit(float dx, float dy) {
    yaw_ -= dx * kOrbitRadiansPerPoint;
    pitch_ = std::clamp(pitch_ + dy * kOrbitRadiansPerPoint, -kMaxPitch, kMaxPitch);
}

void Camera::pan(float dx, float dy) {
    const Vec3 forward = normalize(target_ - eye());
    const Vec3 right = normalize(cross({0, 1, 0}, forward));
    const Vec3 up = cross(forward, right);
    // Scaled by distance so the scene follows the pointer at any zoom level.
    const float scale = distance_ * 0.0015f;
    target_ = target_ - right * (dx * scale) + up * (dy * scale);
}

void Camera::zoom(float ticks) {
    distance_ = std::clamp(distance_ * std::exp(-ticks * kZoomPerTick), kMinDistance, kMaxDistance);
}

Vec3 Camera::eye() const {
    const float horizontal = std::cos(pitch_);
    return target_ + Vec3{std::sin(yaw_) * horizontal, std::sin(pitch_), -std::cos(yaw_) * horizontal} * distance_;
}

Mat4 Camera::view() const {
    return lookAtLH(eye(), target_, {0, 1, 0});
}

Mat4 Camera::projection(float aspect) const {
    return perspectiveFovLH(radians(fovDegrees_), aspect, kCameraNear, kCameraFar);
}

}  // namespace mb
