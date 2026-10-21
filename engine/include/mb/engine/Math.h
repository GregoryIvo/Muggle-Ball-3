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
