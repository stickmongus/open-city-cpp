#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace oc {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

struct ColorRGBA {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct Vec2 {
    float x = 0.0f;
    float z = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float z_) : x(x_), z(z_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, z + o.z}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, z - o.z}; }
    constexpr Vec2 operator*(float s) const { return {x * s, z * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, z / s}; }

    Vec2& operator+=(const Vec2& o) {
        x += o.x;
        z += o.z;
        return *this;
    }
    Vec2& operator-=(const Vec2& o) {
        x -= o.x;
        z -= o.z;
        return *this;
    }
    Vec2& operator*=(float s) {
        x *= s;
        z *= s;
        return *this;
    }

    float lengthSq() const { return x * x + z * z; }
    float length() const { return std::sqrt(lengthSq()); }

    Vec2 normalized() const {
        const float len = length();
        if (len <= 1e-6f) {
            return {0.0f, 0.0f};
        }
        return {x / len, z / len};
    }

    constexpr float dot(const Vec2& o) const { return x * o.x + z * o.z; }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        const float len = length();
        if (len <= 1e-6f) {
            return {0.0f, 0.0f, 0.0f};
        }
        return {x / len, y / len, z / len};
    }
};

// Right-handed XZ ground plane convention (Y is Up):
// heading = 0 -> East (+X, 0)
// heading = +pi/2 -> South (0, +Z)
// heading = -pi/2 -> North (0, -Z)
// Turning right increases heading; rightDir points to driver's right-hand side.
inline Vec2 forwardFromHeading(float heading) {
    return {std::cos(heading), std::sin(heading)};
}

inline Vec2 rightFromHeading(float heading) {
    return {-std::sin(heading), std::cos(heading)};
}

inline float wrapAngle(float angle) {
    while (angle > kPi) angle -= kTwoPi;
    while (angle < -kPi) angle += kTwoPi;
    return angle;
}

inline float angleDiff(float from, float to) {
    return wrapAngle(to - from);
}

inline float moveTowardsAngle(float current, float target, float maxDelta) {
    const float diff = angleDiff(current, target);
    if (std::abs(diff) <= maxDelta) {
        return wrapAngle(target);
    }
    return wrapAngle(current + (diff > 0.0f ? maxDelta : -maxDelta));
}

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * clampf(t, 0.0f, 1.0f);
}

struct AABB2D {
    Vec2 min;
    Vec2 max;

    bool contains(const Vec2& p) const {
        return p.x >= min.x && p.x <= max.x && p.z >= min.z && p.z <= max.z;
    }

    Vec2 center() const {
        return {(min.x + max.x) * 0.5f, (min.z + max.z) * 0.5f};
    }

    Vec2 halfExtents() const {
        return {(max.x - min.x) * 0.5f, (max.z - min.z) * 0.5f};
    }
};

// Circle vs AABB minimum translation vector (pushes circle out of AABB).
inline bool resolveCircleAABB(const Vec2& center, float radius, const AABB2D& box, Vec2& outPush) {
    const float closestX = clampf(center.x, box.min.x, box.max.x);
    const float closestZ = clampf(center.z, box.min.z, box.max.z);
    const float dx = center.x - closestX;
    const float dz = center.z - closestZ;
    const float distSq = dx * dx + dz * dz;

    if (distSq >= radius * radius) {
        return false;
    }

    const float dist = std::sqrt(distSq);
    if (dist > 1e-5f) {
        const float overlap = radius - dist;
        outPush = {dx / dist * overlap, dz / dist * overlap};
        return true;
    }

    // Circle center is inside the AABB: push out along nearest face.
    const float leftPen = (center.x - box.min.x) + radius;
    const float rightPen = (box.max.x - center.x) + radius;
    const float topPen = (center.z - box.min.z) + radius;
    const float bottomPen = (box.max.z - center.z) + radius;
    const float minPen = std::min({leftPen, rightPen, topPen, bottomPen});

    if (minPen == leftPen) outPush = {-leftPen, 0.0f};
    else if (minPen == rightPen) outPush = {rightPen, 0.0f};
    else if (minPen == topPen) outPush = {0.0f, -topPen};
    else outPush = {0.0f, bottomPen};
    return true;
}

struct OBB2D {
    Vec2 center;
    Vec2 halfExtents; // x = halfLength (forward), z = halfWidth (right)
    float heading = 0.0f;

    std::array<Vec2, 2> axes() const {
        return {forwardFromHeading(heading), rightFromHeading(heading)};
    }
};

// Separating Axis Theorem (SAT) collision between two 2D OBBs.
// outMtv pushes box A away from box B.
inline bool intersectOBB(const OBB2D& a, const OBB2D& b, Vec2& outMtv) {
    const auto axesA = a.axes();
    const auto axesB = b.axes();
    const std::array<Vec2, 4> testAxes = {axesA[0], axesA[1], axesB[0], axesB[1]};

    float minOverlap = std::numeric_limits<float>::max();
    Vec2 bestAxis{1.0f, 0.0f};

    const Vec2 delta = a.center - b.center;

    for (const Vec2& axis : testAxes) {
        const float projA =
            std::abs(axis.dot(axesA[0])) * a.halfExtents.x +
            std::abs(axis.dot(axesA[1])) * a.halfExtents.z;
        const float projB =
            std::abs(axis.dot(axesB[0])) * b.halfExtents.x +
            std::abs(axis.dot(axesB[1])) * b.halfExtents.z;
        const float centerDist = std::abs(delta.dot(axis));
        const float overlap = (projA + projB) - centerDist;
        if (overlap <= 0.0f) {
            return false;
        }
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis = (delta.dot(axis) >= 0.0f) ? axis : (axis * -1.0f);
        }
    }

    outMtv = bestAxis * minOverlap;
    return true;
}

inline OBB2D aabbToOBB(const AABB2D& box) {
    return OBB2D{box.center(), box.halfExtents(), 0.0f};
}

// Ray vs Circle on XZ plane; returns hit distance in [0, maxDist] or -1.
inline float intersectRayCircle(const Vec2& origin, const Vec2& dir, float maxDist,
                                const Vec2& center, float radius) {
    const Vec2 ocVec = origin - center;
    const float b = ocVec.dot(dir);
    const float c = ocVec.dot(ocVec) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    const float t = -b - std::sqrt(disc);
    if (t >= 0.0f && t <= maxDist) return t;
    return -1.0f;
}

// Ray vs AABB on XZ plane; returns hit distance in [0, maxDist] or -1.
inline float intersectRayAABB(const Vec2& origin, const Vec2& dir, float maxDist, const AABB2D& box) {
    float tmin = 0.0f;
    float tmax = maxDist;

    if (std::abs(dir.x) < 1e-6f) {
        if (origin.x < box.min.x || origin.x > box.max.x) return -1.0f;
    } else {
        float t1 = (box.min.x - origin.x) / dir.x;
        float t2 = (box.max.x - origin.x) / dir.x;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return -1.0f;
    }

    if (std::abs(dir.z) < 1e-6f) {
        if (origin.z < box.min.z || origin.z > box.max.z) return -1.0f;
    } else {
        float t1 = (box.min.z - origin.z) / dir.z;
        float t2 = (box.max.z - origin.z) / dir.z;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return -1.0f;
    }

    return (tmin >= 0.0f && tmin <= maxDist) ? tmin : -1.0f;
}

// Ray vs OBB on XZ plane by transforming ray into OBB local space.
inline float intersectRayOBB(const Vec2& origin, const Vec2& dir, float maxDist, const OBB2D& obb) {
    const Vec2 f = forwardFromHeading(obb.heading);
    const Vec2 r = rightFromHeading(obb.heading);
    const Vec2 rel = origin - obb.center;
    const Vec2 localOrigin{rel.dot(f), rel.dot(r)};
    const Vec2 localDir{dir.dot(f), dir.dot(r)};
    const AABB2D localBox{{-obb.halfExtents.x, -obb.halfExtents.z},
                          {obb.halfExtents.x, obb.halfExtents.z}};
    return intersectRayAABB(localOrigin, localDir, maxDist, localBox);
}

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0xC0FFEE123456789ULL)
        : state_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

    std::uint64_t nextU64() {
        std::uint64_t x = state_;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state_ = x;
        return x * 0x2545F4914F6CDD1DULL;
    }

    float nextFloat01() {
        return static_cast<float>((nextU64() >> 40) & 0xFFFFFFULL) / 16777216.0f;
    }

    float range(float lo, float hi) {
        return lo + (hi - lo) * nextFloat01();
    }

    int rangeInt(int lo, int hiInclusive) {
        if (hiInclusive <= lo) return lo;
        const std::uint64_t span = static_cast<std::uint64_t>(hiInclusive - lo + 1);
        return lo + static_cast<int>(nextU64() % span);
    }

    bool chance(float probability) {
        return nextFloat01() < probability;
    }

private:
    std::uint64_t state_;
};

} // namespace oc
