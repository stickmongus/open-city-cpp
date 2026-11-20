#pragma once

#include "core/Math.hpp"
#include <array>
#include <cmath>

namespace oc {

// Column-major 4x4 matrix tailored for Vulkan clip space (Y-down or negated viewport, Z in [0, 1])
struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    static Mat4 identity() { return Mat4{}; }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r{};
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    static Mat4 scale(float x, float y, float z) {
        Mat4 r{};
        r.m[0] = x;
        r.m[5] = y;
        r.m[10] = z;
        return r;
    }

    static Mat4 rotationAxis(Vec3 axis, float rad) {
        const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (len < 1e-6f) return identity();
        const float x = axis.x / len;
        const float y = axis.y / len;
        const float z = axis.z / len;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const float t = 1.0f - c;

        Mat4 r{};
        r.m[0]  = t * x * x + c;
        r.m[1]  = t * x * y + s * z;
        r.m[2]  = t * x * z - s * y;
        r.m[3]  = 0.0f;

        r.m[4]  = t * x * y - s * z;
        r.m[5]  = t * y * y + c;
        r.m[6]  = t * y * z + s * x;
        r.m[7]  = 0.0f;

        r.m[8]  = t * x * z + s * y;
        r.m[9]  = t * y * z - s * x;
        r.m[10] = t * z * z + c;
        r.m[11] = 0.0f;

        r.m[12] = 0.0f;
        r.m[13] = 0.0f;
        r.m[14] = 0.0f;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[k * 4 + row] * b.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    Mat4 operator*(const Mat4& rhs) const { return multiply(*this, rhs); }

    Vec3 transformPoint(const Vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }

    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        const Vec3 f = (target - eye).normalized();
        Vec3 s{
            f.y * up.z - f.z * up.y,
            f.z * up.x - f.x * up.z,
            f.x * up.y - f.y * up.x
        };
        const float sLen = s.length();
        if (sLen > 1e-6f) s = s / sLen;
        const Vec3 u{
            s.y * f.z - s.z * f.y,
            s.z * f.x - s.x * f.z,
            s.x * f.y - s.y * f.x
        };

        Mat4 r{};
        r.m[0]  =  s.x; r.m[4]  =  s.y; r.m[8]  =  s.z; r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
        r.m[1]  =  u.x; r.m[5]  =  u.y; r.m[9]  =  u.z; r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
        r.m[2]  = -f.x; r.m[6]  = -f.y; r.m[10] = -f.z; r.m[14] =  (f.x * eye.x + f.y * eye.y + f.z * eye.z);
        r.m[3]  = 0.0f; r.m[7]  = 0.0f; r.m[11] = 0.0f; r.m[15] = 1.0f;
        return r;
    }

    // Vulkan clip-space perspective: Y flipped (-1/tanHalfFov), depth mapped to [0, 1]
    static Mat4 perspectiveVulkan(float fovyRad, float aspect, float nearZ, float farZ) {
        const float f = 1.0f / std::tan(fovyRad * 0.5f);
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
        r.m[0]  = f / std::max(aspect, 0.01f);
        r.m[5]  = -f; // Vulkan NDC +Y points downward!
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (nearZ * farZ) / (nearZ - farZ);
        return r;
    }

    // Vulkan clip-space orthographic: Y flipped, depth mapped to [0, 1]
    static Mat4 orthoVulkan(float left, float right, float bottom, float top, float nearZ, float farZ) {
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
        r.m[0]  =  2.0f / (right - left);
        r.m[5]  = -2.0f / (top - bottom); // Vulkan NDC +Y points downward!
        r.m[10] =  1.0f / (nearZ - farZ);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] =  (top + bottom) / (top - bottom);
        r.m[14] =  nearZ / (nearZ - farZ);
        r.m[15] =  1.0f;
        return r;
    }
};

} // namespace oc
