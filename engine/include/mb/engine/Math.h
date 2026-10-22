#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace mb {

// Conventions (the same as the original Direct3D 8 game, so its data needs no conversion):
//   - left-handed world: +x right, +y up, +z away from the player;
//   - row vectors: p' = p * M, so a * b applies a first, then b;
//   - Mat4 is stored row-major, translation in row 3 (m[3][0..2]);
//   - clip-space depth runs 0..1 (Direct3D, Metal, and SDL GPU on every backend).

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr float& operator[](std::size_t i) { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float operator[](std::size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(float s, Vec3 a) { return a * s; }
constexpr Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 0 ? a / len : Vec3{};
}

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static constexpr Mat4 identity() { return {}; }

    // 16 floats in row-major order: exactly how a .x FrameTransformMatrix is stored.
    static constexpr Mat4 fromRowMajor(const float* v) {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = v[i * 4 + j];
            }
        }
        return r;
    }

    static constexpr Mat4 translation(Vec3 t) {
        Mat4 r;
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    static constexpr Mat4 scaling(Vec3 s) {
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }

    // Same as D3DXMatrixRotationX/Y/Z (left-handed, row vectors).
    static Mat4 rotationX(float radians) {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r;
        r.m[1][1] = c;
        r.m[1][2] = s;
        r.m[2][1] = -s;
        r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationY(float radians) {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r;
        r.m[0][0] = c;
        r.m[0][2] = -s;
        r.m[2][0] = s;
        r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationZ(float radians) {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r;
        r.m[0][0] = c;
        r.m[0][1] = s;
        r.m[1][0] = -s;
        r.m[1][1] = c;
        return r;
    }

    // D3DXMatrixRotationAxis: about the unit vector `axis`, turning the way rotationX/Y/Z do for a positive angle
    // (about z, x towards y).
    static Mat4 rotationAxis(Vec3 axis, float radians) {
        const float c = std::cos(radians), s = std::sin(radians), t = 1 - c;
        const Vec3 a = axis;
        Mat4 r;
        r.m[0][0] = c + t * a.x * a.x;
        r.m[0][1] = t * a.x * a.y + s * a.z;
        r.m[0][2] = t * a.x * a.z - s * a.y;
        r.m[1][0] = t * a.y * a.x - s * a.z;
        r.m[1][1] = c + t * a.y * a.y;
        r.m[1][2] = t * a.y * a.z + s * a.x;
        r.m[2][0] = t * a.z * a.x + s * a.y;
        r.m[2][1] = t * a.z * a.y - s * a.x;
        r.m[2][2] = c + t * a.z * a.z;
        return r;
    }

    // A PhysX 2.x pose: NxMat33 stored row-major for column vectors (world = R * p + t).
    // The row-vector form is the transpose, which is what the game got from
    // NxActor::getGlobalPose().getColumnMajor44() before handing it to Direct3D.
    static constexpr Mat4 fromPhysX(const std::array<float, 9>& rot, Vec3 pos) {
        Mat4 r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                r.m[i][j] = rot[j * 3 + i];
            }
        }
        r.m[3][0] = pos.x;
        r.m[3][1] = pos.y;
        r.m[3][2] = pos.z;
        return r;
    }

    constexpr Vec3 translationPart() const { return {m[3][0], m[3][1], m[3][2]}; }
};

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] +
                        a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

constexpr Vec3 transformPoint(Vec3 p, const Mat4& a) {
    return {p.x * a.m[0][0] + p.y * a.m[1][0] + p.z * a.m[2][0] + a.m[3][0],
            p.x * a.m[0][1] + p.y * a.m[1][1] + p.z * a.m[2][1] + a.m[3][1],
            p.x * a.m[0][2] + p.y * a.m[1][2] + p.z * a.m[2][2] + a.m[3][2]};
}

constexpr Vec3 transformVector(Vec3 v, const Mat4& a) {
    return {v.x * a.m[0][0] + v.y * a.m[1][0] + v.z * a.m[2][0],
            v.x * a.m[0][1] + v.y * a.m[1][1] + v.z * a.m[2][1],
            v.x * a.m[0][2] + v.y * a.m[1][2] + v.z * a.m[2][2]};
}

constexpr Vec4 transform(Vec4 p, const Mat4& a) {
    return {p.x * a.m[0][0] + p.y * a.m[1][0] + p.z * a.m[2][0] + p.w * a.m[3][0],
            p.x * a.m[0][1] + p.y * a.m[1][1] + p.z * a.m[2][1] + p.w * a.m[3][1],
            p.x * a.m[0][2] + p.y * a.m[1][2] + p.z * a.m[2][2] + p.w * a.m[3][2],
            p.x * a.m[0][3] + p.y * a.m[1][3] + p.z * a.m[2][3] + p.w * a.m[3][3]};
}

constexpr Mat4 transpose(const Mat4& a) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[j][i];
        }
    }
    return r;
}

// General inverse (cofactor expansion). Returns identity for a singular matrix.
inline Mat4 inverse(const Mat4& a) {
    const float* s = &a.m[0][0];
    float inv[16];
    inv[0] = s[5] * s[10] * s[15] - s[5] * s[11] * s[14] - s[9] * s[6] * s[15] + s[9] * s[7] * s[14] +
             s[13] * s[6] * s[11] - s[13] * s[7] * s[10];
    inv[4] = -s[4] * s[10] * s[15] + s[4] * s[11] * s[14] + s[8] * s[6] * s[15] - s[8] * s[7] * s[14] -
             s[12] * s[6] * s[11] + s[12] * s[7] * s[10];
    inv[8] = s[4] * s[9] * s[15] - s[4] * s[11] * s[13] - s[8] * s[5] * s[15] + s[8] * s[7] * s[13] +
             s[12] * s[5] * s[11] - s[12] * s[7] * s[9];
    inv[12] = -s[4] * s[9] * s[14] + s[4] * s[10] * s[13] + s[8] * s[5] * s[14] - s[8] * s[6] * s[13] -
              s[12] * s[5] * s[10] + s[12] * s[6] * s[9];
    inv[1] = -s[1] * s[10] * s[15] + s[1] * s[11] * s[14] + s[9] * s[2] * s[15] - s[9] * s[3] * s[14] -
             s[13] * s[2] * s[11] + s[13] * s[3] * s[10];
    inv[5] = s[0] * s[10] * s[15] - s[0] * s[11] * s[14] - s[8] * s[2] * s[15] + s[8] * s[3] * s[14] +
             s[12] * s[2] * s[11] - s[12] * s[3] * s[10];
    inv[9] = -s[0] * s[9] * s[15] + s[0] * s[11] * s[13] + s[8] * s[1] * s[15] - s[8] * s[3] * s[13] -
             s[12] * s[1] * s[11] + s[12] * s[3] * s[9];
    inv[13] = s[0] * s[9] * s[14] - s[0] * s[10] * s[13] - s[8] * s[1] * s[14] + s[8] * s[2] * s[13] +
              s[12] * s[1] * s[10] - s[12] * s[2] * s[9];
    inv[2] = s[1] * s[6] * s[15] - s[1] * s[7] * s[14] - s[5] * s[2] * s[15] + s[5] * s[3] * s[14] +
             s[13] * s[2] * s[7] - s[13] * s[3] * s[6];
    inv[6] = -s[0] * s[6] * s[15] + s[0] * s[7] * s[14] + s[4] * s[2] * s[15] - s[4] * s[3] * s[14] -
             s[12] * s[2] * s[7] + s[12] * s[3] * s[6];
    inv[10] = s[0] * s[5] * s[15] - s[0] * s[7] * s[13] - s[4] * s[1] * s[15] + s[4] * s[3] * s[13] +
              s[12] * s[1] * s[7] - s[12] * s[3] * s[5];
    inv[14] = -s[0] * s[5] * s[14] + s[0] * s[6] * s[13] + s[4] * s[1] * s[14] - s[4] * s[2] * s[13] -
              s[12] * s[1] * s[6] + s[12] * s[2] * s[5];
    inv[3] = -s[1] * s[6] * s[11] + s[1] * s[7] * s[10] + s[5] * s[2] * s[11] - s[5] * s[3] * s[10] -
             s[9] * s[2] * s[7] + s[9] * s[3] * s[6];
    inv[7] = s[0] * s[6] * s[11] - s[0] * s[7] * s[10] - s[4] * s[2] * s[11] + s[4] * s[3] * s[10] +
             s[8] * s[2] * s[7] - s[8] * s[3] * s[6];
    inv[11] = -s[0] * s[5] * s[11] + s[0] * s[7] * s[9] + s[4] * s[1] * s[11] - s[4] * s[3] * s[9] -
              s[8] * s[1] * s[7] + s[8] * s[3] * s[5];
    inv[15] = s[0] * s[5] * s[10] - s[0] * s[6] * s[9] - s[4] * s[1] * s[10] + s[4] * s[2] * s[9] +
              s[8] * s[1] * s[6] - s[8] * s[2] * s[5];
    const float det = s[0] * inv[0] + s[1] * inv[4] + s[2] * inv[8] + s[3] * inv[12];
    if (det == 0) {
        return Mat4::identity();
    }
    Mat4 r;
    for (int i = 0; i < 16; ++i) {
        (&r.m[0][0])[i] = inv[i] / det;
    }
    return r;
}

// The rotation nearest to the upper-left 3x3 of `a` (its polar factor, by Newton iteration), with the
// translation kept. A matrix that is already a rotation to within 1e-6 comes back as it is. One that
// is singular or mirrors has no rotation near it and gives the identity rotation.
inline Mat4 orthonormalised(const Mat4& a) {
    Vec3 q[3];
    for (int i = 0; i < 3; ++i) {
        q[i] = {a.m[i][0], a.m[i][1], a.m[i][2]};
    }
    constexpr float kTolerance = 1e-6f;
    const auto isRotation = [&] {
        return std::fabs(dot(q[0], q[0]) - 1) < kTolerance && std::fabs(dot(q[1], q[1]) - 1) < kTolerance &&
               std::fabs(dot(q[2], q[2]) - 1) < kTolerance && std::fabs(dot(q[0], q[1])) < kTolerance &&
               std::fabs(dot(q[0], q[2])) < kTolerance && std::fabs(dot(q[1], q[2])) < kTolerance &&
               dot(cross(q[0], q[1]), q[2]) > 0;
    };
    for (int iteration = 0; iteration < 32 && !isRotation(); ++iteration) {
        const float det = dot(q[0], cross(q[1], q[2]));
        if (!(det > 1e-6f)) {  // also false for NaN
            Mat4 r;
            r.m[3][0] = a.m[3][0];
            r.m[3][1] = a.m[3][1];
            r.m[3][2] = a.m[3][2];
            return r;
        }
        // Q <- (Q + Q^-T) / 2; the rows of Q^-T are the cofactor rows over the determinant.
        const Vec3 inverseTransposed[3] = {cross(q[1], q[2]) / det, cross(q[2], q[0]) / det,
                                           cross(q[0], q[1]) / det};
        for (int i = 0; i < 3; ++i) {
            q[i] = (q[i] + inverseTransposed[i]) * 0.5f;
        }
    }
    Mat4 r = a;
    for (int i = 0; i < 3; ++i) {
        r.m[i][0] = q[i].x;
        r.m[i][1] = q[i].y;
        r.m[i][2] = q[i].z;
    }
    return r;
}

// D3DXMatrixLookAtLH.
inline Mat4 lookAtLH(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 z = normalize(target - eye);
    const Vec3 x = normalize(cross(up, z));
    const Vec3 y = cross(z, x);
    Mat4 r;
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x; r.m[0][3] = 0;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y; r.m[1][3] = 0;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z; r.m[2][3] = 0;
    r.m[3][0] = -dot(x, eye); r.m[3][1] = -dot(y, eye); r.m[3][2] = -dot(z, eye); r.m[3][3] = 1;
    return r;
}

// D3DXMatrixPerspectiveFovLH: vertical field of view, depth mapped to 0..1.
inline Mat4 perspectiveFovLH(float fovYRadians, float aspect, float zNear, float zFar) {
    const float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
    const float xScale = yScale / aspect;
    Mat4 r;
    r.m[0][0] = xScale;
    r.m[1][1] = yScale;
    r.m[2][2] = zFar / (zFar - zNear);
    r.m[2][3] = 1;
    r.m[3][2] = -zNear * zFar / (zFar - zNear);
    r.m[3][3] = 0;
    return r;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float radians(float degrees) { return degrees * kPi / 180.0f; }

}  // namespace mb
