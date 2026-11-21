#include "render/CarMeshGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace oc {

namespace {

inline Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline ColorRGBA shadeCol(ColorRGBA c, float f) {
    return {
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.r * f), 0, 255)),
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.g * f), 0, 255)),
        static_cast<std::uint8_t>(std::clamp(static_cast<int>(c.b * f), 0, 255)),
        c.a
    };
}

struct PosKey {
    std::int32_t x, y, z;
    bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct PosKeyHash {
    std::size_t operator()(const PosKey& k) const noexcept {
        std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.x));
        h = (h * 0x9E3779B97F4A7C15ULL) ^ static_cast<std::uint32_t>(k.y);
        h = (h * 0x9E3779B97F4A7C15ULL) ^ static_cast<std::uint32_t>(k.z);
        return static_cast<std::size_t>(h ^ (h >> 32));
    }
};

inline PosKey quantizePos(const Vec3& p) {
    return {
        static_cast<std::int32_t>(std::llround(p.x * 2000.0f)),
        static_cast<std::int32_t>(std::llround(p.y * 2000.0f)),
        static_cast<std::int32_t>(std::llround(p.z * 2000.0f))
    };
}

} // namespace

// ============================================================================
// MeshBuilder3D
// ============================================================================

void MeshBuilder3D::clear() { tris_.clear(); }
void MeshBuilder3D::reserve(std::size_t triCount) { tris_.reserve(triCount); }

void MeshBuilder3D::addTri(const Vec3& a, const Vec3& b, const Vec3& c,
                           ColorRGBA col, Vec2 uvA, Vec2 uvB, Vec2 uvC) {
    Vec3 e1 = b - a;
    Vec3 e2 = c - a;
    Vec3 n = cross3(e1, e2);
    float len = n.length();
    if (len < 1e-7f) return;
    RawTri t{};
    t.p[0] = a; t.p[1] = b; t.p[2] = c;
    t.uv[0] = uvA; t.uv[1] = uvB; t.uv[2] = uvC;
    t.c[0] = col; t.c[1] = col; t.c[2] = col;
    t.faceN = n / len;
    tris_.push_back(t);
}

void MeshBuilder3D::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                            ColorRGBA col) {
    addTri(a, b, c, col, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f});
    addTri(a, c, d, col, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
}

void MeshBuilder3D::addQuadGrad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                                ColorRGBA colA, ColorRGBA colB, ColorRGBA colC, ColorRGBA colD) {
    auto pushT = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2,
                     ColorRGBA c0, ColorRGBA c1, ColorRGBA c2,
                     Vec2 u0, Vec2 u1, Vec2 u2) {
        Vec3 n = cross3(p1 - p0, p2 - p0);
        float len = n.length();
        if (len < 1e-7f) return;
        RawTri t{};
        t.p[0] = p0; t.p[1] = p1; t.p[2] = p2;
        t.uv[0] = u0; t.uv[1] = u1; t.uv[2] = u2;
        t.c[0] = c0; t.c[1] = c1; t.c[2] = c2;
        t.faceN = n / len;
        tris_.push_back(t);
    };
    pushT(a, b, c, colA, colB, colC, {0, 0}, {1, 0}, {1, 1});
    pushT(a, c, d, colA, colC, colD, {0, 0}, {1, 1}, {0, 1});
}

void MeshBuilder3D::addBox(const Vec3& center, const Vec3& size, ColorRGBA col) {
    const Vec3 h = size * 0.5f;
    const float x0 = center.x - h.x, x1 = center.x + h.x;
    const float y0 = center.y - h.y, y1 = center.y + h.y;
    const float z0 = center.z - h.z, z1 = center.z + h.z;

    // +X (Front)
    addQuad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, col);
    // -X (Back)
    addQuad({x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, col);
    // +Y (Top)
    addQuad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, col);
    // -Y (Bottom)
    addQuad({x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, col);
    // +Z (Right)
    addQuad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, col);
    // -Z (Left)
    addQuad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, col);
}

void MeshBuilder3D::addOrientedBox(const Vec3& center, const Vec3& h,
                                   const Vec3& ax, const Vec3& ay, const Vec3& az,
                                   ColorRGBA col) {
    auto pt = [&](float sx, float sy, float sz) -> Vec3 {
        return center + ax * (sx * h.x) + ay * (sy * h.y) + az * (sz * h.z);
    };
    const Vec3 p000 = pt(-1, -1, -1), p100 = pt(1, -1, -1);
    const Vec3 p010 = pt(-1,  1, -1), p110 = pt(1,  1, -1);
    const Vec3 p001 = pt(-1, -1,  1), p101 = pt(1, -1,  1);
    const Vec3 p011 = pt(-1,  1,  1), p111 = pt(1,  1,  1);

    addQuad(p100, p110, p111, p101, col);
    addQuad(p001, p011, p010, p000, col);
    addQuad(p010, p011, p111, p110, col);
    addQuad(p001, p000, p100, p101, col);
    addQuad(p001, p101, p111, p011, col);
    addQuad(p100, p000, p010, p110, col);
}

void MeshBuilder3D::addCylinderY(const Vec3& bottomCenter, float rBottom, float rTop,
                                 float height, int segments, ColorRGBA col, bool capEnds) {
    segments = std::max(6, segments);
    const Vec3 topCenter = {bottomCenter.x, bottomCenter.y + height, bottomCenter.z};
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const Vec3 b0 = {bottomCenter.x + c0 * rBottom, bottomCenter.y, bottomCenter.z + s0 * rBottom};
        const Vec3 b1 = {bottomCenter.x + c1 * rBottom, bottomCenter.y, bottomCenter.z + s1 * rBottom};
        const Vec3 t0 = {topCenter.x + c0 * rTop, topCenter.y, topCenter.z + s0 * rTop};
        const Vec3 t1 = {topCenter.x + c1 * rTop, topCenter.y, topCenter.z + s1 * rTop};
        addQuad(b0, t0, t1, b1, col);
        if (capEnds) {
            if (rTop > 1e-4f) addTri(topCenter, t1, t0, col);
            if (rBottom > 1e-4f) addTri(bottomCenter, b0, b1, col);
        }
    }
}

void MeshBuilder3D::addCylinderZ(const Vec3& center, float radius, float length,
                                 int segments, ColorRGBA col, bool capEnds) {
    segments = std::max(6, segments);
    const float z0 = center.z - length * 0.5f;
    const float z1 = center.z + length * 0.5f;
    const Vec3 c0Pt{center.x, center.y, z0};
    const Vec3 c1Pt{center.x, center.y, z1};
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float x0 = center.x + std::cos(a0) * radius;
        const float y0 = center.y + std::sin(a0) * radius;
        const float x1 = center.x + std::cos(a1) * radius;
        const float y1 = center.y + std::sin(a1) * radius;
        addQuad({x0, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y0, z1}, col);
        if (capEnds) {
            addTri(c1Pt, {x0, y0, z1}, {x1, y1, z1}, col);
            addTri(c0Pt, {x1, y1, z0}, {x0, y0, z0}, col);
        }
    }
}

void MeshBuilder3D::addCylinderX(const Vec3& center, float radius, float length,
                                 int segments, ColorRGBA col, bool capEnds) {
    segments = std::max(6, segments);
    const float x0 = center.x - length * 0.5f;
    const float x1 = center.x + length * 0.5f;
    const Vec3 c0Pt{x0, center.y, center.z};
    const Vec3 c1Pt{x1, center.y, center.z};
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float y0 = center.y + std::cos(a0) * radius;
        const float z0 = center.z + std::sin(a0) * radius;
        const float y1 = center.y + std::cos(a1) * radius;
        const float z1 = center.z + std::sin(a1) * radius;
        addQuad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z1}, {x0, y1, z1}, col);
        if (capEnds) {
            addTri(c1Pt, {x1, y0, z0}, {x1, y1, z1}, col);
            addTri(c0Pt, {x0, y1, z1}, {x0, y0, z0}, col);
        }
    }
}

void MeshBuilder3D::addSphere(const Vec3& center, float radius, int rings, int slices, ColorRGBA col) {
    rings = std::max(4, rings);
    slices = std::max(6, slices);
    for (int r = 0; r < rings; ++r) {
        const float phi0 = -kHalfPi + kPi * static_cast<float>(r) / static_cast<float>(rings);
        const float phi1 = -kHalfPi + kPi * static_cast<float>(r + 1) / static_cast<float>(rings);
        const float y0 = center.y + radius * std::sin(phi0);
        const float y1 = center.y + radius * std::sin(phi1);
        const float rr0 = radius * std::cos(phi0);
        const float rr1 = radius * std::cos(phi1);
        for (int s = 0; s < slices; ++s) {
            const float th0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
            const float th1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
            const Vec3 p00{center.x + rr0 * std::cos(th0), y0, center.z + rr0 * std::sin(th0)};
            const Vec3 p10{center.x + rr0 * std::cos(th1), y0, center.z + rr0 * std::sin(th1)};
            const Vec3 p01{center.x + rr1 * std::cos(th0), y1, center.z + rr1 * std::sin(th0)};
            const Vec3 p11{center.x + rr1 * std::cos(th1), y1, center.z + rr1 * std::sin(th1)};
            if (r > 0) addTri(p00, p01, p10, col);
            if (r < rings - 1) addTri(p10, p01, p11, col);
        }
    }
}

std::vector<Vertex3D> MeshBuilder3D::buildCreasedVertices(float creaseAngleDeg) const {
    std::vector<Vertex3D> out;
    if (tris_.empty()) return out;
    out.reserve(tris_.size() * 3);

    const float cosThresh = std::cos(creaseAngleDeg * (kPi / 180.0f));
    std::unordered_map<PosKey, std::vector<std::size_t>, PosKeyHash> adj;
    adj.reserve(tris_.size() * 2);

    for (std::size_t ti = 0; ti < tris_.size(); ++ti) {
        for (int k = 0; k < 3; ++k) {
            adj[quantizePos(tris_[ti].p[k])].push_back(ti);
        }
    }

    for (std::size_t ti = 0; ti < tris_.size(); ++ti) {
        const auto& tri = tris_[ti];
        for (int k = 0; k < 3; ++k) {
            Vec3 nSum = tri.faceN;
            if (creaseAngleDeg > 0.5f) {
                const auto it = adj.find(quantizePos(tri.p[k]));
                if (it != adj.end()) {
                    for (std::size_t other : it->second) {
                        if (other == ti) continue;
                        if (dot3(tri.faceN, tris_[other].faceN) >= cosThresh) {
                            nSum = nSum + tris_[other].faceN;
                        }
                    }
                }
            }
            const float nLen = nSum.length();
            const Vec3 nFinal = (nLen > 1e-6f) ? (nSum / nLen) : tri.faceN;

            Vertex3D v{};
            v.px = tri.p[k].x; v.py = tri.p[k].y; v.pz = tri.p[k].z;
            v.nx = nFinal.x;   v.ny = nFinal.y;   v.nz = nFinal.z;
            v.u = tri.uv[k].x; v.v = tri.uv[k].z;
            v.r = static_cast<float>(tri.c[k].r) / 255.0f;
            v.g = static_cast<float>(tri.c[k].g) / 255.0f;
            v.b = static_cast<float>(tri.c[k].b) / 255.0f;
            v.a = static_cast<float>(tri.c[k].a) / 255.0f;
            out.push_back(v);
        }
    }
    return out;
}

GpuMesh MeshBuilder3D::upload(VulkanContext& vk, float creaseAngleDeg) const {
    if (tris_.empty()) return {};
    auto verts = buildCreasedVertices(creaseAngleDeg);
    return vk.createMesh(verts);
}

// ============================================================================
// CAD-Precision Automotive Silhouette & Zero-Jelly Builder (All 16 Vehicles!)
// ============================================================================

namespace {

enum class CarBodyType {
    CoupeFastback,   // SportsCoupe, Hypercar, TrackPrototype, JdmTuner
    MuscleCoupe,     // MuscleCar
    SedanFourDoor,   // LuxurySedan, Taxi, PoliceCruiser
    HatchCompact,    // Compact
    SuvWagon,        // SUV, ArmoredSuv
    PickupTruck,     // OffroadPickup
    RoadsterOpen,    // ClassicRoadster
    CargoVan,        // DeliveryVan, Ambulance
    FirePumper,      // FireTruck
    CityBus          // SeoulBus
};

struct CarBlueprint {
    CarBodyType bodyType = CarBodyType::SedanFourDoor;
    float halfL = 2.25f;
    float halfW = 0.96f;
    float groundClear = 0.20f;
    float frontAxleX = 1.38f;
    float rearAxleX = -1.36f;
    float wheelR = 0.35f;
    float wheelW = 0.24f;

    // Longitudinal CAD keypoints (+X front -> -X rear)
    float xFrontNose = 2.25f;
    float xHoodStart = 2.08f;
    float xCowl = 0.85f;
    float xRoofFront = 0.25f;
    float xBpillar = -0.22f;
    float xRoofRear = -1.05f;
    float xRearDeck = -1.78f;
    float xRearTip = -2.25f;

    // Clean crisp vertical elevations (ZERO wavy arch bulge!)
    float yNose = 0.66f;
    float yCowl = 0.84f;
    float yBeltRear = 0.89f;
    float yRearDeck = 0.86f;
    float yRoof = 1.34f;
    float yRoofCrown = 0.032f;

    // Lateral tumblehome widths
    float beltHalfW = 0.92f;
    float roofHalfW = 0.70f;
    float noseTaper = 0.91f;
    float rearTaper = 0.93f;
    float characterCreaseH = 0.62f; // fraction between groundClear & beltY

    bool hasRearDoor = true;
    bool hasQuarterGlass = false;
    bool openTop = false;
    bool openBed = false;
    bool isHighBox = false;
    bool integratedSpoiler = false;
    bool tallGtWing = false;
    bool isPolice = false;
    bool isTaxi = false;
    bool isAmbulance = false;
    bool isFireTruck = false;
    bool isDeliveryVan = false;
    bool isSeoulBus = false;
};

CarBlueprint makeBlueprint(VehicleKind kind) {
    const auto& spec = getVehicleSpec(kind);
    CarBlueprint b{};
    b.halfL = spec.halfLength;
    b.halfW = spec.halfWidth;
    b.xFrontNose = b.halfL;
    b.xRearTip = -b.halfL;
    b.xHoodStart = b.halfL - 0.16f;
    b.beltHalfW = b.halfW * 0.955f;

    switch (kind) {
        case VehicleKind::SportsCoupe:
            b.bodyType = CarBodyType::CoupeFastback;
            b.groundClear = 0.16f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.60f;
            b.wheelR = 0.345f; b.wheelW = 0.25f;
            b.xHoodStart = b.halfL - 0.24f;
            b.xCowl = b.halfL * 0.28f;
            b.xRoofFront = -b.halfL * 0.02f;
            b.xBpillar = -b.halfL * 0.22f;
            b.xRoofRear = -b.halfL * 0.50f;
            b.xRearDeck = -b.halfL * 0.86f;
            b.yNose = 0.66f; b.yCowl = 0.83f; b.yBeltRear = 0.89f; b.yRearDeck = 0.88f;
            b.yRoof = 1.26f; b.roofHalfW = b.halfW * 0.68f;
            b.noseTaper = 0.89f; b.rearTaper = 0.92f;
            b.hasRearDoor = false;
            b.integratedSpoiler = true;
            break;

        case VehicleKind::Hypercar:
            b.bodyType = CarBodyType::CoupeFastback;
            b.groundClear = 0.135f;
            b.frontAxleX = b.halfL * 0.61f;
            b.rearAxleX = -b.halfL * 0.59f;
            b.wheelR = 0.335f; b.wheelW = 0.26f;
            b.xHoodStart = b.halfL - 0.25f;
            b.xCowl = b.halfL * 0.36f;
            b.xRoofFront = b.halfL * 0.04f;
            b.xBpillar = -b.halfL * 0.19f;
            b.xRoofRear = -b.halfL * 0.44f;
            b.xRearDeck = -b.halfL * 0.88f;
            b.yNose = 0.66f; b.yCowl = 0.80f; b.yBeltRear = 0.88f; b.yRearDeck = 0.89f;
            b.yRoof = 1.18f; b.roofHalfW = b.halfW * 0.63f;
            b.noseTaper = 0.88f; b.rearTaper = 0.95f;
            b.hasRearDoor = false;
            b.tallGtWing = true;
            break;

        case VehicleKind::TrackPrototype:
            b.bodyType = CarBodyType::CoupeFastback;
            b.groundClear = 0.125f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.60f;
            b.wheelR = 0.335f; b.wheelW = 0.27f;
            b.xHoodStart = b.halfL - 0.25f;
            b.xCowl = b.halfL * 0.34f;
            b.xRoofFront = b.halfL * 0.05f;
            b.xBpillar = -b.halfL * 0.18f;
            b.xRoofRear = -b.halfL * 0.44f;
            b.xRearDeck = -b.halfL * 0.90f;
            b.yNose = 0.64f; b.yCowl = 0.78f; b.yBeltRear = 0.86f; b.yRearDeck = 0.87f;
            b.yRoof = 1.15f; b.roofHalfW = b.halfW * 0.60f;
            b.noseTaper = 0.88f; b.rearTaper = 0.96f;
            b.hasRearDoor = false;
            b.tallGtWing = true;
            break;

        case VehicleKind::JdmTuner:
            b.bodyType = CarBodyType::CoupeFastback;
            b.groundClear = 0.155f;
            b.frontAxleX = b.halfL * 0.61f;
            b.rearAxleX = -b.halfL * 0.59f;
            b.wheelR = 0.340f; b.wheelW = 0.25f;
            b.xHoodStart = b.halfL - 0.22f;
            b.xCowl = b.halfL * 0.30f;
            b.xRoofFront = 0.05f;
            b.xBpillar = -b.halfL * 0.22f;
            b.xRoofRear = -b.halfL * 0.52f;
            b.xRearDeck = -b.halfL * 0.80f;
            b.yNose = 0.66f; b.yCowl = 0.82f; b.yBeltRear = 0.88f; b.yRearDeck = 0.87f;
            b.yRoof = 1.28f; b.roofHalfW = b.halfW * 0.71f;
            b.noseTaper = 0.89f; b.rearTaper = 0.93f;
            b.hasRearDoor = false;
            b.tallGtWing = true;
            break;

        case VehicleKind::MuscleCar:
            b.bodyType = CarBodyType::MuscleCoupe;
            b.groundClear = 0.18f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.58f;
            b.wheelR = 0.365f; b.wheelW = 0.26f;
            b.xHoodStart = b.halfL - 0.16f;
            b.xCowl = b.halfL * 0.22f;
            b.xRoofFront = -b.halfL * 0.04f;
            b.xBpillar = -b.halfL * 0.24f;
            b.xRoofRear = -b.halfL * 0.54f;
            b.xRearDeck = -b.halfL * 0.84f;
            b.yNose = 0.73f; b.yCowl = 0.88f; b.yBeltRear = 0.93f; b.yRearDeck = 0.91f;
            b.yRoof = 1.32f; b.roofHalfW = b.halfW * 0.72f;
            b.noseTaper = 0.93f; b.rearTaper = 0.95f;
            b.hasRearDoor = false;
            b.integratedSpoiler = true;
            break;

        case VehicleKind::ClassicRoadster:
            b.bodyType = CarBodyType::RoadsterOpen;
            b.groundClear = 0.17f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.58f;
            b.wheelR = 0.335f; b.wheelW = 0.23f;
            b.xCowl = b.halfL * 0.24f;
            b.xRoofFront = -b.halfL * 0.02f;
            b.xBpillar = -b.halfL * 0.35f;
            b.xRoofRear = -b.halfL * 0.46f;
            b.xRearDeck = -b.halfL * 0.52f;
            b.yNose = 0.66f; b.yCowl = 0.80f; b.yBeltRear = 0.84f; b.yRearDeck = 0.84f;
            b.yRoof = 1.19f; b.roofHalfW = b.halfW * 0.74f;
            b.openTop = true;
            b.hasRearDoor = false;
            break;

        case VehicleKind::LuxurySedan:
        case VehicleKind::Taxi:
        case VehicleKind::PoliceCruiser:
            b.bodyType = CarBodyType::SedanFourDoor;
            b.groundClear = 0.19f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.60f;
            b.wheelR = 0.355f; b.wheelW = 0.24f;
            b.xCowl = b.halfL * 0.34f;
            b.xRoofFront = b.halfL * 0.10f;
            b.xBpillar = -b.halfL * 0.12f;
            b.xRoofRear = -b.halfL * 0.52f;
            b.xRearDeck = -b.halfL * 0.78f;
            b.yNose = 0.71f; b.yCowl = 0.88f; b.yBeltRear = 0.93f; b.yRearDeck = 0.91f;
            b.yRoof = 1.41f; b.roofHalfW = b.halfW * 0.75f;
            b.hasRearDoor = true;
            b.isPolice = (kind == VehicleKind::PoliceCruiser);
            b.isTaxi = (kind == VehicleKind::Taxi);
            break;

        case VehicleKind::Compact:
            b.bodyType = CarBodyType::HatchCompact;
            b.groundClear = 0.18f;
            b.frontAxleX = b.halfL * 0.61f;
            b.rearAxleX = -b.halfL * 0.62f;
            b.wheelR = 0.325f; b.wheelW = 0.21f;
            b.xCowl = b.halfL * 0.38f;
            b.xRoofFront = b.halfL * 0.14f;
            b.xBpillar = -b.halfL * 0.12f;
            b.xRoofRear = -b.halfL * 0.72f;
            b.xRearDeck = -b.halfL * 0.94f;
            b.yNose = 0.68f; b.yCowl = 0.86f; b.yBeltRear = 0.91f; b.yRearDeck = 0.90f;
            b.yRoof = 1.43f; b.roofHalfW = b.halfW * 0.77f;
            b.hasRearDoor = true;
            break;

        case VehicleKind::SUV:
        case VehicleKind::ArmoredSuv:
            b.bodyType = CarBodyType::SuvWagon;
            b.groundClear = 0.27f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.60f;
            b.wheelR = 0.415f; b.wheelW = 0.27f;
            b.xHoodStart = b.halfL - 0.13f;
            b.xCowl = b.halfL * 0.38f;
            b.xRoofFront = b.halfL * 0.16f;
            b.xBpillar = -b.halfL * 0.08f;
            b.xRoofRear = -b.halfL * 0.84f;
            b.xRearDeck = -b.halfL * 0.96f;
            b.yNose = 0.92f; b.yCowl = 1.08f; b.yBeltRear = 1.12f; b.yRearDeck = 1.10f;
            b.yRoof = 1.78f; b.roofHalfW = b.halfW * 0.82f;
            b.noseTaper = 0.94f; b.rearTaper = 0.96f;
            b.hasRearDoor = true;
            b.hasQuarterGlass = true;
            break;

        case VehicleKind::OffroadPickup:
            b.bodyType = CarBodyType::PickupTruck;
            b.groundClear = 0.31f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.58f;
            b.wheelR = 0.44f; b.wheelW = 0.29f;
            b.xHoodStart = b.halfL - 0.12f;
            b.xCowl = b.halfL * 0.42f;
            b.xRoofFront = b.halfL * 0.22f;
            b.xBpillar = -b.halfL * 0.02f;
            b.xRoofRear = -b.halfL * 0.18f;
            b.xRearDeck = -b.halfL * 0.22f;
            b.yNose = 0.98f; b.yCowl = 1.14f; b.yBeltRear = 1.14f; b.yRearDeck = 1.14f;
            b.yRoof = 1.82f; b.roofHalfW = b.halfW * 0.83f;
            b.noseTaper = 0.95f; b.rearTaper = 0.98f;
            b.hasRearDoor = true;
            b.openBed = true;
            break;

        case VehicleKind::DeliveryVan:
        case VehicleKind::Ambulance:
            b.bodyType = CarBodyType::CargoVan;
            b.groundClear = 0.27f;
            b.frontAxleX = b.halfL * 0.60f;
            b.rearAxleX = -b.halfL * 0.56f;
            b.wheelR = 0.40f; b.wheelW = 0.26f;
            b.xHoodStart = b.halfL - 0.12f;
            b.xCowl = b.halfL * 0.66f;
            b.xRoofFront = b.halfL * 0.42f;
            b.xBpillar = b.halfL * 0.14f;
            b.xRoofRear = -b.halfL * 0.96f;
            b.xRearDeck = -b.halfL * 0.99f;
            b.yNose = 0.96f; b.yCowl = 1.18f; b.yBeltRear = 1.18f; b.yRearDeck = 1.18f;
            b.yRoof = 2.18f; b.roofHalfW = b.halfW * 0.91f;
            b.noseTaper = 0.96f; b.rearTaper = 0.98f;
            b.isHighBox = true;
            b.hasRearDoor = false;
            b.isDeliveryVan = (kind == VehicleKind::DeliveryVan);
            b.isAmbulance = (kind == VehicleKind::Ambulance);
            break;

        case VehicleKind::FireTruck:
            b.bodyType = CarBodyType::FirePumper;
            b.groundClear = 0.34f;
            b.frontAxleX = b.halfL * 0.62f;
            b.rearAxleX = -b.halfL * 0.54f;
            b.wheelR = 0.46f; b.wheelW = 0.30f;
            b.xHoodStart = b.halfL - 0.08f;
            b.xCowl = b.halfL * 0.88f;
            b.xRoofFront = b.halfL * 0.78f;
            b.xBpillar = b.halfL * 0.34f;
            b.xRoofRear = -b.halfL * 0.98f;
            b.xRearDeck = -b.halfL * 0.99f;
            b.yNose = 1.18f; b.yCowl = 1.32f; b.yBeltRear = 1.32f; b.yRearDeck = 1.32f;
            b.yRoof = 2.52f; b.roofHalfW = b.halfW * 0.95f;
            b.noseTaper = 0.98f; b.rearTaper = 0.99f;
            b.isHighBox = true;
            b.hasRearDoor = false;
            b.isFireTruck = true;
            break;

        case VehicleKind::SeoulBus:
            b.bodyType = CarBodyType::CityBus;
            b.groundClear = 0.30f;
            b.frontAxleX = b.halfL * 0.60f;
            b.rearAxleX = -b.halfL * 0.54f;
            b.wheelR = 0.49f; b.wheelW = 0.31f;
            b.xHoodStart = b.halfL - 0.06f;
            b.xCowl = b.halfL * 0.94f;
            b.xRoofFront = b.halfL * 0.87f;
            b.xBpillar = b.halfL * 0.64f;
            b.xRoofRear = -b.halfL * 0.98f;
            b.xRearDeck = -b.halfL * 0.99f;
            b.yNose = 1.16f; b.yCowl = 1.24f; b.yBeltRear = 1.24f; b.yRearDeck = 1.24f;
            b.yRoof = 2.86f; b.yRoofCrown = 0.045f; b.roofHalfW = b.halfW * 0.96f;
            b.noseTaper = 0.98f; b.rearTaper = 0.99f;
            b.isHighBox = true;
            b.hasRearDoor = false;
            b.isSeoulBus = true;
            break;

        default:
            break;
    }
    return b;
}

// Evaluates the EXACT lateral half-width taper at any longitudinal X (smooth crisp plan view)
float planWidthScale(const CarBlueprint& b, float x) {
    if (x > b.xCowl) {
        const float t = std::clamp((x - b.xCowl) / std::max(0.05f, b.xFrontNose - b.xCowl), 0.0f, 1.0f);
        return lerpf(1.0f, b.noseTaper, t * t);
    }
    if (x < b.xRoofRear) {
        const float t = std::clamp((b.xRoofRear - x) / std::max(0.05f, b.xRoofRear - b.xRearTip), 0.0f, 1.0f);
        return lerpf(1.0f, b.rearTaper, t * t);
    }
    return 1.0f;
}

// Evaluates the EXACT clean beltline elevation at longitudinal X (straight & crisp — ZERO wavy arch bumps!)
float beltYAt(const CarBlueprint& b, float x) {
    if (x >= b.xCowl) {
        const float t = std::clamp((x - b.xCowl) / std::max(0.05f, b.xHoodStart - b.xCowl), 0.0f, 1.0f);
        return lerpf(b.yCowl, b.yNose, std::pow(t, 1.25f));
    }
    const float t = std::clamp((b.xCowl - x) / std::max(0.05f, b.xCowl - b.xRearTip), 0.0f, 1.0f);
    return lerpf(b.yCowl, b.yBeltRear, t);
}

// Evaluates the EXACT roof-edge elevation at longitudinal X inside greenhouse [xRoofRear .. xRoofFront]
float roofSideYAt(const CarBlueprint& b, float x) {
    const float span = std::max(0.05f, b.xRoofFront - b.xRoofRear);
    const float u = std::clamp((x - b.xRoofRear) / span, 0.0f, 1.0f);
    // Subtle authentic automotive roofline arch (highest slightly forward of center, tapering gently at A & C headers)
    const float arch = std::sin(u * kPi) * (b.isHighBox ? 0.012f : 0.036f);
    return b.yRoof - 0.032f + arch;
}

// Evaluates the EXACT coupled greenhouse outer surface point for side s (-1=left, +1=right)
// t in [0..1] goes from beltline shoulder (t=0) up to roof cant rail (t=1)!
// Both the stamped A/B/C pillars AND the curved side glass share THIS EXACT formula -> 0mm gap!
Vec3 greenhouseSurfacePt(const CarBlueprint& b, float x, float t, float sideSign, float insetMeters = 0.0f) {
    const float y0 = beltYAt(b, x);
    const float y1 = roofSideYAt(b, x);
    const float y = lerpf(y0, y1, t);

    // Real curved automotive glass tumblehome (smooth convex bow, matches beltHalfW at t=0 & roofHalfW at t=1)
    const float wScale = planWidthScale(b, x);
    const float zBase = b.beltHalfW * wScale;
    const float zTop = b.roofHalfW * lerpf(1.0f, wScale, 0.45f);
    const float convexBow = b.isHighBox ? (std::sin(t * kPi) * 0.016f) : (std::sin(t * kPi) * 0.044f);
    const float z = lerpf(zBase, zTop, std::pow(t, 1.08f)) + convexBow - insetMeters;
    return {x, y, sideSign * z};
}

// Builds a curved strip along the greenhouse surface between xBot(t)..xTop(t) or pillar width
void addCurvedGreenhousePanel(MeshBuilder3D& mb, const CarBlueprint& b,
                              float xFrontBot, float xFrontTop,
                              float xBackBot, float xBackTop,
                              float tMin, float tMax, int vSteps,
                              float sideSign, float insetMeters,
                              ColorRGBA col, bool outwardFace = true) {
    for (int i = 0; i < vSteps; ++i) {
        const float ta = lerpf(tMin, tMax, static_cast<float>(i) / static_cast<float>(vSteps));
        const float tb = lerpf(tMin, tMax, static_cast<float>(i + 1) / static_cast<float>(vSteps));
        const float xaF = lerpf(xFrontBot, xFrontTop, ta);
        const float xbF = lerpf(xFrontBot, xFrontTop, tb);
        const float xaB = lerpf(xBackBot, xBackTop, ta);
        const float xbB = lerpf(xBackBot, xBackTop, tb);

        const Vec3 p00 = greenhouseSurfacePt(b, xaB, ta, sideSign, insetMeters);
        const Vec3 p10 = greenhouseSurfacePt(b, xaF, ta, sideSign, insetMeters);
        const Vec3 p11 = greenhouseSurfacePt(b, xbF, tb, sideSign, insetMeters);
        const Vec3 p01 = greenhouseSurfacePt(b, xbB, tb, sideSign, insetMeters);

        // Ensure outward normal points along sideSign * +Z
        if ((sideSign > 0.0f) == outwardFace) {
            mb.addQuad(p00, p10, p11, p01, col);
        } else {
            mb.addQuad(p10, p00, p01, p11, col);
        }
    }
}

// Extrudes a solid 3D structural pillar (outer painted/black face + front/back beveled jambs + inner edge)
void addSolidPillar(MeshBuilder3D& mb, const CarBlueprint& b,
                    float xFrontBot, float xFrontTop,
                    float xBackBot, float xBackTop,
                    float tMin, float tMax, int vSteps,
                    float sideSign, float outerInset, float thickness, ColorRGBA col) {
    // Outer curved face
    addCurvedGreenhousePanel(mb, b, xFrontBot, xFrontTop, xBackBot, xBackTop,
                             tMin, tMax, vSteps, sideSign, outerInset, col, true);
    // Inner face
    addCurvedGreenhousePanel(mb, b, xFrontBot, xFrontTop, xBackBot, xBackTop,
                             tMin, tMax, vSteps, sideSign, outerInset + thickness, shadeCol(col, 0.65f), false);
    // Front & back window-channel jamb walls (gives real 3D window frame depth!)
    for (int i = 0; i < vSteps; ++i) {
        const float ta = lerpf(tMin, tMax, static_cast<float>(i) / static_cast<float>(vSteps));
        const float tb = lerpf(tMin, tMax, static_cast<float>(i + 1) / static_cast<float>(vSteps));
        const float xaF = lerpf(xFrontBot, xFrontTop, ta);
        const float xbF = lerpf(xFrontBot, xFrontTop, tb);
        const float xaB = lerpf(xBackBot, xBackTop, ta);
        const float xbB = lerpf(xBackBot, xBackTop, tb);

        const Vec3 fOut0 = greenhouseSurfacePt(b, xaF, ta, sideSign, outerInset);
        const Vec3 fOut1 = greenhouseSurfacePt(b, xbF, tb, sideSign, outerInset);
        const Vec3 fIn0  = greenhouseSurfacePt(b, xaF, ta, sideSign, outerInset + thickness);
        const Vec3 fIn1  = greenhouseSurfacePt(b, xbF, tb, sideSign, outerInset + thickness);
        if (sideSign > 0.0f) mb.addQuad(fOut0, fIn0, fIn1, fOut1, shadeCol(col, 0.78f));
        else                 mb.addQuad(fIn0, fOut0, fOut1, fIn1, shadeCol(col, 0.78f));

        const Vec3 bOut0 = greenhouseSurfacePt(b, xaB, ta, sideSign, outerInset);
        const Vec3 bOut1 = greenhouseSurfacePt(b, xbB, tb, sideSign, outerInset);
        const Vec3 bIn0  = greenhouseSurfacePt(b, xaB, ta, sideSign, outerInset + thickness);
        const Vec3 bIn1  = greenhouseSurfacePt(b, xbB, tb, sideSign, outerInset + thickness);
        if (sideSign > 0.0f) mb.addQuad(bIn0, bOut0, bOut1, bIn1, shadeCol(col, 0.78f));
        else                 mb.addQuad(bOut0, bIn0, bIn1, bOut1, shadeCol(col, 0.78f));
    }
}

// Evaluates the EXACT sculpted lateral Z of the lower body flank at any (x, y)
// so circular wheel arches hug the fender shoulder to the millimeter with ZERO protruding shelf!
float flankZAt(const CarBlueprint& b, float x, float y) {
    const float w = planWidthScale(b, x);
    const float yBelt = beltYAt(b, x);
    const float yBot = b.groundClear;
    const float yCrease = lerpf(yBot, yBelt, b.characterCreaseH);
    const float yMid = lerpf(yBot, yCrease, 0.52f);

    const float zSill = b.halfW * w * 0.945f;
    const float zMid  = b.halfW * w * 0.980f;
    const float zPeak = b.halfW * w * 1.005f;
    const float zBelt = b.beltHalfW * w;

    if (y <= yMid) {
        const float t = std::clamp((y - yBot) / std::max(0.01f, yMid - yBot), 0.0f, 1.0f);
        return lerpf(zSill, zMid, t);
    }
    if (y <= yCrease) {
        const float t = std::clamp((y - yMid) / std::max(0.01f, yCrease - yMid), 0.0f, 1.0f);
        return lerpf(zMid, zPeak, t);
    }
    const float t = std::clamp((y - yCrease) / std::max(0.01f, yBelt - yCrease), 0.0f, 1.0f);
    return lerpf(zPeak, zBelt, t);
}

// Builds a flush circular 3D wheel-arch liner & crisp dark charcoal inner trim hugging exact flankZAt(x,y)!
void addWheelArchAndWell(MeshBuilder3D& bodyMb, MeshBuilder3D& trimMb, const CarBlueprint& b,
                         float axleX, float sideSign) {
    (void)bodyMb;
    const float centerY = b.wheelR + 0.015f;
    const float maxArchR = std::max(b.wheelR + 0.030f, beltYAt(b, axleX) - centerY - 0.036f);
    const float archR = std::min(b.wheelR + 0.068f, maxArchR);
    const float outerR = archR + 0.020f;
    const float wScaleAxle = planWidthScale(b, axleX);
    const float zFlankAxle = b.halfW * wScaleAxle;
    const float zWellBase = std::max(0.28f, zFlankAxle - b.wheelW - 0.13f);

    const int segs = 18;
    const float angStart = -0.04f;
    const float angEnd = kPi + 0.04f;
    const ColorRGBA kWellDark{18, 20, 24, 255};
    const ColorRGBA kArchLip{26, 28, 34, 255};

    for (int i = 0; i < segs; ++i) {
        const float a0 = lerpf(angStart, angEnd, static_cast<float>(i) / static_cast<float>(segs));
        const float a1 = lerpf(angStart, angEnd, static_cast<float>(i + 1) / static_cast<float>(segs));

        const float xIn0 = std::clamp(axleX + std::cos(a0) * archR, b.xRearTip + 0.06f, b.xFrontNose - 0.08f);
        const float xIn1 = std::clamp(axleX + std::cos(a1) * archR, b.xRearTip + 0.06f, b.xFrontNose - 0.08f);
        const float xOut0 = std::clamp(axleX + std::cos(a0) * outerR, b.xRearTip + 0.05f, b.xFrontNose - 0.07f);
        const float xOut1 = std::clamp(axleX + std::cos(a1) * outerR, b.xRearTip + 0.05f, b.xFrontNose - 0.07f);

        // Clamp strictly below beltline AT EACH VERTEX's exact longitudinal X
        const float maxYIn0  = beltYAt(b, xIn0)  - 0.048f;
        const float maxYIn1  = beltYAt(b, xIn1)  - 0.048f;
        const float maxYOut0 = beltYAt(b, xOut0) - 0.040f;
        const float maxYOut1 = beltYAt(b, xOut1) - 0.040f;

        const float yIn0  = std::clamp(centerY + std::sin(a0) * archR,  b.groundClear, maxYIn0);
        const float yIn1  = std::clamp(centerY + std::sin(a1) * archR,  b.groundClear, maxYIn1);
        const float yOut0 = std::clamp(centerY + std::sin(a0) * outerR, b.groundClear, maxYOut0);
        const float yOut1 = std::clamp(centerY + std::sin(a1) * outerR, b.groundClear, maxYOut1);

        const float zOut0 = flankZAt(b, xOut0, yOut0) + 0.002f;
        const float zOut1 = flankZAt(b, xOut1, yOut1) + 0.002f;
        const float zLip0 = flankZAt(b, xIn0,  yIn0)  + 0.003f;
        const float zLip1 = flankZAt(b, xIn1,  yIn1)  + 0.003f;
        const float zWellIn0 = std::min(zWellBase, zLip0 - 0.055f);
        const float zWellIn1 = std::min(zWellBase, zLip1 - 0.055f);

        const Vec3 pOut0{xOut0, yOut0, sideSign * zOut0};
        const Vec3 pOut1{xOut1, yOut1, sideSign * zOut1};
        const Vec3 pLip0{xIn0,  yIn0,  sideSign * zLip0};
        const Vec3 pLip1{xIn1,  yIn1,  sideSign * zLip1};
        const Vec3 pRet0{xIn0,  yIn0,  sideSign * (zLip0 - 0.034f)};
        const Vec3 pRet1{xIn1,  yIn1,  sideSign * (zLip1 - 0.034f)};

        if (sideSign > 0.0f) {
            trimMb.addQuad(pOut0, pLip0, pLip1, pOut1, kArchLip);
            trimMb.addQuad(pLip0, pRet0, pRet1, pLip1, kWellDark);
        } else {
            trimMb.addQuad(pLip0, pOut0, pOut1, pLip1, kArchLip);
            trimMb.addQuad(pRet0, pLip0, pLip1, pRet1, kWellDark);
        }

        // Deep matte-black wheel-house arch liner & back wall (strictly tucked inside flank!)
        const Vec3 wOuter0{xIn0, yIn0, sideSign * (zLip0 - 0.032f)};
        const Vec3 wOuter1{xIn1, yIn1, sideSign * (zLip1 - 0.032f)};
        const Vec3 wInner0{xIn0, yIn0, sideSign * zWellIn0};
        const Vec3 wInner1{xIn1, yIn1, sideSign * zWellIn1};
        const Vec3 wCenter{axleX, centerY, sideSign * std::min(zWellIn0, zWellIn1)};

        if (sideSign > 0.0f) {
            trimMb.addQuad(wOuter0, wInner0, wInner1, wOuter1, kWellDark);
            trimMb.addTri(wCenter, wInner1, wInner0, kWellDark);
        } else {
            trimMb.addQuad(wInner0, wOuter0, wOuter1, wInner1, kWellDark);
            trimMb.addTri(wCenter, wInner0, wInner1, kWellDark);
        }
    }
}

// Helper: computes clean circular wheel-well bottom cutoff Y at longitudinal coordinate X
float wheelCutoutBottomY(const CarBlueprint& b, float x) {
    const float archR = b.wheelR + 0.068f;
    const float centerY = b.wheelR + 0.015f;
    float y = b.groundClear;
    for (float ax : {b.frontAxleX, b.rearAxleX}) {
        const float dx = std::abs(x - ax);
        if (dx < archR) {
            const float arcY = centerY + std::sqrt(std::max(0.0f, archR * archR - dx * dx));
            y = std::max(y, arcY);
        }
    }
    return y;
}

// Generates a complete CAD-precision multi-part CarMeshSet for any VehicleKind!
CarMeshSet buildCadCar(VulkanContext& vk, VehicleKind kind) {
    const CarBlueprint b = makeBlueprint(kind);
    MeshBuilder3D bodyMb, glassMb, interiorMb, trimMb, chromeMb, headMb, tailMb, liveryMb, stripeMb, spoilerMb;

    const ColorRGBA kWhite{255, 255, 255, 255};
    const ColorRGBA kTrimBlack{26, 28, 34, 255};
    const ColorRGBA kTrimDark{16, 18, 22, 255};
    const ColorRGBA kDloGloss{14, 16, 20, 255};
    const ColorRGBA kChromeCol{232, 236, 242, 255};
    const ColorRGBA kInteriorDark{32, 34, 40, 255};
    const ColorRGBA kInteriorSeat{44, 46, 54, 255};
    const ColorRGBA kGlassTint{14, 22, 34, 152}; // Deep smoked luxury automotive glass (clear enough to see 3D seated driver!)
    const ColorRGBA kHeadLight{250, 252, 255, 255};
    const ColorRGBA kTailRed{235, 28, 34, 255};

    // ------------------------------------------------------------------------
    // 1. LOWER STAMPED SHEET-METAL BODY SIDES (Crisp 4-band sculpted door profile
    //    with TRUE circular wheel-well cutouts and ZERO wavy hood bulge!)
    // ------------------------------------------------------------------------
    std::vector<float> xSamples;
    auto pushX = [&](float x) {
        if (x >= b.xRearTip && x <= b.xFrontNose) xSamples.push_back(x);
    };
    // Key structural longitudinal stations
    pushX(b.xRearTip);
    pushX(b.xRearTip + 0.14f);
    pushX(b.xRearDeck);
    pushX(b.xRoofRear);
    pushX(b.xBpillar);
    pushX(b.xRoofFront);
    pushX(b.xCowl);
    pushX(b.xHoodStart);
    pushX(b.xFrontNose);
    // Dense circular stations around BOTH wheel arches so the cutout is razor-round!
    const float archR = b.wheelR + 0.068f;
    for (float ax : {b.frontAxleX, b.rearAxleX}) {
        pushX(ax - archR - 0.02f);
        pushX(ax + archR + 0.02f);
        for (int k = 0; k <= 12; ++k) {
            const float ang = kPi * static_cast<float>(k) / 12.0f;
            pushX(ax + std::cos(ang) * archR);
        }
    }
    // Even intermediate stations along hood & doors
    for (int i = 1; i < 16; ++i) {
        pushX(lerpf(b.xRearTip, b.xFrontNose, static_cast<float>(i) / 16.0f));
    }
    std::sort(xSamples.begin(), xSamples.end());
    // Deduplicate stations closer than 8mm
    std::vector<float> xs;
    xs.reserve(xSamples.size());
    for (float x : xSamples) {
        if (xs.empty() || (x - xs.back()) > 0.008f) xs.push_back(x);
    }

    // Build BOTH left (-1) and right (+1) sculpted lower body flanks
    for (float s : {-1.0f, 1.0f}) {
        for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
            const float x0 = xs[i];
            const float x1 = xs[i + 1];
            const float xMid = 0.5f * (x0 + x1);

            const float w0 = planWidthScale(b, x0);
            const float w1 = planWidthScale(b, x1);
            const float yBelt0 = beltYAt(b, x0);
            const float yBelt1 = beltYAt(b, x1);
            const float yBot0 = std::min(yBelt0 - 0.04f, wheelCutoutBottomY(b, x0));
            const float yBot1 = std::min(yBelt1 - 0.04f, wheelCutoutBottomY(b, x1));

            // 4 crisp vertical bands per flank (Sill -> Lower Door -> Tornado Crease -> Beltline Shoulder)
            const float yCrease0 = std::max(yBot0 + 0.015f, lerpf(b.groundClear, yBelt0, b.characterCreaseH));
            const float yCrease1 = std::max(yBot1 + 0.015f, lerpf(b.groundClear, yBelt1, b.characterCreaseH));
            const float yMid0 = lerpf(yBot0, yCrease0, 0.52f);
            const float yMid1 = lerpf(yBot1, yCrease1, 0.52f);

            const float zSill0 = b.halfW * w0 * 0.945f;
            const float zSill1 = b.halfW * w1 * 0.945f;
            const float zMid0  = b.halfW * w0 * 0.980f;
            const float zMid1  = b.halfW * w1 * 0.980f;
            const float zPeak0 = b.halfW * w0 * 1.005f; // Crisp stamped character crease apex!
            const float zPeak1 = b.halfW * w1 * 1.005f;
            const float zBelt0 = b.beltHalfW * w0;      // Matches greenhouseSurfacePt(..., t=0) EXACTLY!
            const float zBelt1 = b.beltHalfW * w1;

            auto quadSide = [&](const Vec3& a, const Vec3& bb, const Vec3& c, const Vec3& d, ColorRGBA col) {
                if (s > 0.0f) bodyMb.addQuad(a, bb, c, d, col);
                else          bodyMb.addQuad(bb, a, d, c, col);
            };

            // Slight vertical AO darkening at rocker sill -> realistic stamped metal grounding
            const ColorRGBA cSill = shadeCol(kWhite, 0.86f);
            const ColorRGBA cMid  = shadeCol(kWhite, 0.95f);

            quadSide({x0, yBot0,    s * zSill0}, {x1, yBot1,    s * zSill1},
                     {x1, yMid1,    s * zMid1},  {x0, yMid0,    s * zMid0}, cSill);
            quadSide({x0, yMid0,    s * zMid0},  {x1, yMid1,    s * zMid1},
                     {x1, yCrease1, s * zPeak1}, {x0, yCrease0, s * zPeak0}, cMid);
            quadSide({x0, yCrease0, s * zPeak0}, {x1, yCrease1, s * zPeak1},
                     {x1, yBelt1,   s * zBelt1}, {x0, yBelt0,   s * zBelt0}, kWhite);

            // Lower matte-black rocker skirt aero blade between wheel wells!
            const bool betweenAxles = (xMid > b.rearAxleX + archR + 0.02f && xMid < b.frontAxleX - archR - 0.02f);
            if (betweenAxles) {
                const Vec3 r0{x0, b.groundClear - 0.025f, s * (zSill0 + 0.012f)};
                const Vec3 r1{x1, b.groundClear - 0.025f, s * (zSill1 + 0.012f)};
                const Vec3 r2{x1, b.groundClear + 0.045f, s * (zSill1 + 0.012f)};
                const Vec3 r3{x0, b.groundClear + 0.045f, s * (zSill0 + 0.012f)};
                if (s > 0.0f) trimMb.addQuad(r0, r1, r2, r3, kTrimBlack);
                else          trimMb.addQuad(r1, r0, r3, r2, kTrimBlack);
            }
        }

        // Sculpted 3D circular wheel arches + deep black inner wheel wells
        addWheelArchAndWell(bodyMb, trimMb, b, b.frontAxleX, s);
        addWheelArchAndWell(bodyMb, trimMb, b, b.rearAxleX, s);
    }

    // Solid dark underbody floor pan (seals underside cleanly)
    trimMb.addBox({0.0f, b.groundClear + 0.04f, 0.0f},
                  {b.halfL * 1.92f, 0.08f, b.halfW * 1.66f}, kTrimDark);

    // ------------------------------------------------------------------------
    // 2. TAUT STAMPED HOOD WITH POWER-DOME CHARACTER RIDGES (ZERO WAVY BUMPS!)
    // ------------------------------------------------------------------------
    {
        const int hSteps = 8;
        const float xStart = b.xCowl;
        const float xEnd = b.xHoodStart;
        for (int i = 0; i < hSteps; ++i) {
            const float u0 = static_cast<float>(i) / static_cast<float>(hSteps);
            const float u1 = static_cast<float>(i + 1) / static_cast<float>(hSteps);
            const float x0 = lerpf(xStart, xEnd, u0);
            const float x1 = lerpf(xStart, xEnd, u1);
            const float y0 = beltYAt(b, x0);
            const float y1 = beltYAt(b, x1);
            const float w0 = b.beltHalfW * planWidthScale(b, x0);
            const float w1 = b.beltHalfW * planWidthScale(b, x1);

            // Clean 5-point symmetric hood profile: -outer, -crease, center, +crease, +outer
            const float domeH = b.isHighBox ? 0.016f : 0.042f;
            const float ridgeH = b.isHighBox ? 0.014f : 0.034f;
            const float cFrac = 0.52f;
            const float z0[5] = {-w0, -w0 * cFrac, 0.0f, w0 * cFrac, w0};
            const float z1[5] = {-w1, -w1 * cFrac, 0.0f, w1 * cFrac, w1};
            const float dy0[5] = {0.0f, ridgeH * (1.0f - u0 * 0.35f), domeH * (1.0f - u0 * 0.25f), ridgeH * (1.0f - u0 * 0.35f), 0.0f};
            const float dy1[5] = {0.0f, ridgeH * (1.0f - u1 * 0.35f), domeH * (1.0f - u1 * 0.25f), ridgeH * (1.0f - u1 * 0.35f), 0.0f};

            for (int j = 0; j < 4; ++j) {
                bodyMb.addQuad(
                    {x0, y0 + dy0[j],     z0[j]},
                    {x0, y0 + dy0[j + 1], z0[j + 1]},
                    {x1, y1 + dy1[j + 1], z1[j + 1]},
                    {x1, y1 + dy1[j],     z1[j]},
                    kWhite);
            }
        }
        // Subtle recessed black windshield wiper cowl panel at xCowl
        const float wCowl = b.beltHalfW * planWidthScale(b, b.xCowl);
        trimMb.addQuad(
            {b.xCowl - 0.06f, b.yCowl + 0.008f, -wCowl * 0.94f},
            {b.xCowl - 0.06f, b.yCowl + 0.008f,  wCowl * 0.94f},
            {b.xCowl + 0.04f, b.yCowl + 0.010f,  wCowl * 0.94f},
            {b.xCowl + 0.04f, b.yCowl + 0.010f, -wCowl * 0.94f},
            kTrimDark);
    }

    // ------------------------------------------------------------------------
    // 3. SLEEK SCULPTED CURVED FRONT NOSE, WRAP-AROUND LED HEADLAMPS & CLASS GRILLE!
    // ------------------------------------------------------------------------
    {
        const float xCorner = b.xFrontNose;
        const float xHood = b.xHoodStart;
        const float yHoodEdge = beltYAt(b, xHood);
        const float yBeltEnd  = beltYAt(b, xCorner);
        const float yCreaseEnd = lerpf(b.groundClear, yBeltEnd, b.characterCreaseH);
        const float yMidEnd    = lerpf(b.groundClear, yCreaseEnd, 0.52f);
        const float wHood = b.beltHalfW * planWidthScale(b, xHood);
        const float wBeltEnd = b.beltHalfW * b.noseTaper;
        const float wPeakEnd = b.halfW * b.noseTaper * 1.005f;
        const float wMidEnd  = b.halfW * b.noseTaper * 0.980f;
        const float wSillEnd = b.halfW * b.noseTaper * 0.945f;

        const bool isSportAero = (b.bodyType == CarBodyType::CoupeFastback ||
                                  b.bodyType == CarBodyType::RoadsterOpen);
        const float bowMax = b.isHighBox ? 0.040f : (isSportAero ? 0.135f : 0.095f);
        const float domeTip = b.isHighBox ? 0.010f : 0.030f;

        // Smooth curved 6-column automotive front nose surface (joins flanks at |u|=1 with ZERO seam!)
        const int cols = 6;
        auto frontColX = [&](float uAbs, float rowScale) {
            return xCorner + bowMax * (1.0f - uAbs * uAbs) * rowScale;
        };

        // Row 0: hood tip (xHood) -> Row 1: upper nose brow -> Row 2: bumper crest -> Row 3: lower air dam -> Row 4: chin bottom
        for (int i = 0; i < cols; ++i) {
            const float u0 = lerpf(-1.0f, 1.0f, static_cast<float>(i) / static_cast<float>(cols));
            const float u1 = lerpf(-1.0f, 1.0f, static_cast<float>(i + 1) / static_cast<float>(cols));
            const float ua0 = std::abs(u0), ua1 = std::abs(u1);

            const float dyH0 = domeTip * (1.0f - ua0 * ua0);
            const float dyH1 = domeTip * (1.0f - ua1 * ua1);

            const Vec3 r0a{xHood, yHoodEdge + dyH0, u0 * wHood};
            const Vec3 r0b{xHood, yHoodEdge + dyH1, u1 * wHood};

            const float yBrow0 = yBeltEnd - (isSportAero ? 0.052f : 0.025f) * (1.0f - ua0 * ua0);
            const float yBrow1 = yBeltEnd - (isSportAero ? 0.052f : 0.025f) * (1.0f - ua1 * ua1);
            const Vec3 r1a{frontColX(ua0, 0.86f), yBrow0, u0 * wBeltEnd};
            const Vec3 r1b{frontColX(ua1, 0.86f), yBrow1, u1 * wBeltEnd};

            const Vec3 r2a{frontColX(ua0, 1.02f), yCreaseEnd, u0 * wPeakEnd};
            const Vec3 r2b{frontColX(ua1, 1.02f), yCreaseEnd, u1 * wPeakEnd};

            const Vec3 r3a{frontColX(ua0, 0.96f), yMidEnd, u0 * wMidEnd};
            const Vec3 r3b{frontColX(ua1, 0.96f), yMidEnd, u1 * wMidEnd};

            const Vec3 r4a{frontColX(ua0, 0.82f), b.groundClear + 0.01f, u0 * wSillEnd};
            const Vec3 r4b{frontColX(ua1, 0.82f), b.groundClear + 0.01f, u1 * wSillEnd};

            bodyMb.addQuad(r0a, r0b, r1b, r1a, kWhite);
            bodyMb.addQuad(r1a, r1b, r2b, r2a, kWhite);
            bodyMb.addQuad(r2a, r2b, r3b, r3a, shadeCol(kWhite, 0.96f));
            bodyMb.addQuad(r3a, r3b, r4b, r4a, shadeCol(kWhite, 0.86f));
        }

        const float xTipCenter = xCorner + bowMax;
        const float wNose = wPeakEnd;

        if (isSportAero) {
            // MODERN SPORTS COUPE / HYPERCAR / ROADSTER FRONT:
            // Sleek sculpted body-colored nose bridge + wide low aggressive dark honeycomb air dam + twin aero brake ducts!
            const float damBot = b.groundClear + 0.045f;
            const float damTop = lerpf(b.groundClear, yBeltEnd, 0.48f);
            const float damW = wNose * 0.56f;
            // Deep wide lower honeycomb intake cavity
            trimMb.addBox({xTipCenter - 0.025f, 0.5f * (damBot + damTop), 0.0f},
                          {0.085f, damTop - damBot, damW * 1.96f}, kTrimDark);
            // Horizontal glossy dark aero intake splitter fins
            for (int f = 1; f <= 2; ++f) {
                const float fy = lerpf(damBot, damTop, static_cast<float>(f) / 3.0f);
                trimMb.addBox({xTipCenter + 0.004f, fy, 0.0f}, {0.030f, 0.015f, damW * 1.92f}, kTrimBlack);
            }
            // Sculpted outer side brake-cooling vents flanking the main air dam (strictly flush inside curved bumper!)
            for (float s : {-1.0f, 1.0f}) {
                const float vx = frontColX(0.70f, 0.96f);
                const float vz = s * wNose * 0.68f;
                const Vec3 fwd{std::cos(s * 0.26f), 0.0f, std::sin(s * 0.26f)};
                const Vec3 lat{-fwd.z, 0.0f, fwd.x};
                trimMb.addOrientedBox({vx - 0.022f, 0.5f * (damBot + damTop) + 0.01f, vz},
                                      {0.030f, (damTop - damBot) * 0.42f, wNose * 0.125f},
                                      fwd, {0, 1, 0}, lat, kTrimDark);
                // Sleek carbon/gloss-black aero vane inside side intake
                trimMb.addOrientedBox({vx - 0.004f, 0.5f * (damBot + damTop) + 0.01f, vz},
                                      {0.012f, 0.010f, wNose * 0.115f},
                                      fwd, {0, 1, 0}, lat, kDloGloss);
            }
            // Sleek small metallic hood crest badge on curved nose bridge
            chromeMb.addBox({frontColX(0.0f, 0.94f) + 0.008f, yBeltEnd - 0.045f, 0.0f},
                            {0.016f, 0.036f, 0.056f}, kChromeCol);
        } else {
            // MODERN KOREAN SEDAN / POLICE / TAXI / SUV / PICKUP / VAN GRILLE:
            // Sleek curved dark-mesh Genesis/Hyundai-style grille + subtle satin chrome bezel!
            const float gBot = lerpf(b.groundClear, yBeltEnd, 0.28f);
            const float gTop = yBeltEnd - 0.065f;
            const float gW = wNose * 0.46f;
            trimMb.addBox({xTipCenter - 0.025f, 0.5f * (gBot + gTop), 0.0f},
                          {0.080f, gTop - gBot, gW * 1.96f}, kTrimDark);
            const int slats = b.isHighBox ? 5 : 4;
            for (int i = 1; i < slats; ++i) {
                const float sy = lerpf(gBot, gTop, static_cast<float>(i) / static_cast<float>(slats));
                chromeMb.addBox({xTipCenter + 0.006f, sy, 0.0f}, {0.018f, 0.011f, gW * 1.88f}, kChromeCol);
            }
            chromeMb.addBox({xTipCenter + 0.010f, gTop, 0.0f}, {0.018f, 0.016f, gW * 2.00f}, kChromeCol);
            chromeMb.addBox({xTipCenter + 0.010f, gBot, 0.0f}, {0.018f, 0.015f, gW * 1.92f}, kChromeCol);
            chromeMb.addBox({xTipCenter + 0.016f, 0.5f * (gBot + gTop) + 0.015f, 0.0f},
                            {0.014f, 0.052f, 0.082f}, kChromeCol);
            // Lower fog-lamp corner air vents
            for (float s : {-1.0f, 1.0f}) {
                const float vx = frontColX(0.76f, 0.96f);
                trimMb.addBox({vx - 0.012f, b.groundClear + 0.14f, s * wNose * 0.72f},
                              {0.048f, 0.095f, wNose * 0.24f}, kTrimDark);
            }
        }

        // Sleek sculpted carbon/matte-black front aero chin splitter following curved bumper!
        for (int i = 0; i < cols; ++i) {
            const float u0 = lerpf(-1.0f, 1.0f, static_cast<float>(i) / static_cast<float>(cols));
            const float u1 = lerpf(-1.0f, 1.0f, static_cast<float>(i + 1) / static_cast<float>(cols));
            const float x0 = frontColX(std::abs(u0), 0.90f) + 0.03f;
            const float x1 = frontColX(std::abs(u1), 0.90f) + 0.03f;
            const float z0 = u0 * wSillEnd * 1.01f;
            const float z1 = u1 * wSillEnd * 1.01f;
            const float yLo = b.groundClear - 0.012f;
            const float yHi = b.groundClear + 0.026f;
            trimMb.addQuad({x0, yLo, z0}, {x1, yLo, z1}, {x1, yHi, z1}, {x0, yHi, z0}, kTrimBlack);
            trimMb.addQuad({x0 - 0.12f, yHi, z0}, {x1 - 0.12f, yHi, z1}, {x1, yHi, z1}, {x0, yHi, z0}, kTrimBlack);
        }

        // Korean white front license plate cleanly mounted on lower center bumper
        const float plateY = b.groundClear + (isSportAero ? 0.145f : 0.19f);
        trimMb.addBox({xTipCenter + 0.010f, plateY, 0.0f}, {0.014f, 0.11f, 0.42f}, kTrimDark);
        chromeMb.addBox({xTipCenter + 0.017f, plateY, 0.0f}, {0.008f, 0.092f, 0.39f}, {244, 246, 250, 255});

        // SLEEK SWEPT-BACK WRAP-AROUND 3D LED HEADLAMPS (Angled cleanly along curved fender corners!)
        const float hlY = yBeltEnd - (b.isHighBox ? 0.14f : (isSportAero ? 0.085f : 0.11f));
        const float hlH = b.isHighBox ? 0.14f : (isSportAero ? 0.068f : 0.088f);
        const float hlZ = wNose * 0.71f;
        const float hlW = wNose * 0.34f;
        for (float s : {-1.0f, 1.0f}) {
            const float hx = frontColX(0.71f, 0.94f);
            const float sweepAng = s * (b.isHighBox ? 0.10f : 0.34f);
            const Vec3 ax{std::cos(sweepAng), 0.0f, std::sin(sweepAng)};
            const Vec3 az{-ax.z, 0.0f, ax.x};
            const Vec3 ay{0.0f, 1.0f, 0.0f};
            // Dark smoked internal headlamp reflector housing
            trimMb.addOrientedBox({hx - 0.008f, hlY, s * hlZ},
                                  {0.044f, hlH * 0.56f, hlW * 0.52f}, ax, ay, az, kTrimDark);
            // Crisp twin LED projector lenses inside housing (cleanly proud of reflector bucket on all 16 classes!)
            headMb.addOrientedBox({hx + 0.022f, hlY + 0.004f, s * (hlZ - 0.055f)},
                                  {0.024f, hlH * 0.36f, hlW * 0.20f}, ax, ay, az, kHeadLight);
            headMb.addOrientedBox({hx + 0.015f, hlY + 0.004f, s * (hlZ + 0.055f)},
                                  {0.024f, hlH * 0.36f, hlW * 0.20f}, ax, ay, az, kHeadLight);
            // Razor-sharp modern LED DRL light-blade sweeping around the corner!
            headMb.addOrientedBox({hx + 0.026f, hlY - hlH * 0.36f, s * (hlZ + 0.015f)},
                                  {0.020f, 0.014f, hlW * 0.52f}, ax, ay, az, {226, 242, 255, 255});
        }
    }

    // ------------------------------------------------------------------------
    // 4. REAR DECKLID / OPEN PICKUP BED / CARGO VAN BOX + CURVED REAR BUMPER & LED TAILLIGHTS
    // ------------------------------------------------------------------------
    {
        const float xRear = b.xRearTip;
        const float yDeck = beltYAt(b, xRear);
        const float wRear = b.halfW * b.rearTaper;
        const float rearBow = b.isHighBox ? 0.020f : 0.068f;

        if (!b.isHighBox && !b.openBed) {
            // Crisp flat-crowned trunk decklid from xRearDeck to xRearTip
            const float x0 = b.xRearDeck;
            const float x1 = xRear;
            const float y0 = beltYAt(b, x0);
            const float y1 = yDeck;
            const float w0 = b.beltHalfW * planWidthScale(b, x0);
            const float w1 = b.beltHalfW * planWidthScale(b, x1);
            bodyMb.addQuad({x0, y0, -w0}, {x0, y0 + 0.028f, 0.0f}, {x1 - rearBow * 0.7f, y1 + 0.022f, 0.0f}, {x1, y1, -w1}, kWhite);
            bodyMb.addQuad({x0, y0 + 0.028f, 0.0f}, {x0, y0, w0}, {x1, y1, w1}, {x1 - rearBow * 0.7f, y1 + 0.022f, 0.0f}, kWhite);
        } else if (b.openBed) {
            // Authentic Pickup Truck open cargo bed with matte-black bedliner & stamped bed walls!
            const float bedFront = b.xRearDeck - 0.04f;
            const float bedBack = xRear + 0.12f;
            const float bedFloorY = b.groundClear + 0.34f;
            const float bedTopY = b.yRearDeck;
            const float bedInW = b.beltHalfW * 0.84f;
            // Matte-black bedliner floor + inner walls
            trimMb.addBox({0.5f * (bedFront + bedBack), bedFloorY, 0.0f},
                          {bedFront - bedBack, 0.06f, bedInW * 2.0f}, kTrimDark);
            trimMb.addBox({bedFront, 0.5f * (bedFloorY + bedTopY), 0.0f},
                          {0.08f, bedTopY - bedFloorY, bedInW * 2.0f}, kTrimDark);
            for (float s : {-1.0f, 1.0f}) {
                trimMb.addBox({0.5f * (bedFront + bedBack), 0.5f * (bedFloorY + bedTopY), s * bedInW},
                              {bedFront - bedBack, bedTopY - bedFloorY, 0.06f}, kTrimDark);
                // Painted top rail cap of bed walls
                bodyMb.addBox({0.5f * (bedFront + bedBack), bedTopY - 0.02f, s * 0.5f * (bedInW + b.beltHalfW)},
                              {bedFront - bedBack + 0.08f, 0.045f, b.beltHalfW - bedInW}, kWhite);
            }
            // Tailgate top cap + black chrome rollbar
            bodyMb.addBox({xRear + 0.06f, bedTopY - 0.02f, 0.0f}, {0.12f, 0.05f, b.beltHalfW * 1.94f}, kWhite);
            chromeMb.addCylinderY({bedFront - 0.12f, bedTopY, -bedInW * 0.92f}, 0.036f, 0.036f, b.yRoof - bedTopY - 0.04f, 10, kChromeCol);
            chromeMb.addCylinderY({bedFront - 0.12f, bedTopY,  bedInW * 0.92f}, 0.036f, 0.036f, b.yRoof - bedTopY - 0.04f, 10, kChromeCol);
            chromeMb.addCylinderZ({bedFront - 0.12f, b.yRoof - 0.04f, 0.0f}, 0.036f, bedInW * 1.88f, 10, kChromeCol);
        }

        // Gently curved rear fascia panel (meets flanks seamlessly at |u|=1 and stops cleanly at rear diffuser top!)
        const float diffH = b.isHighBox ? 0.16f : 0.20f;
        const float rearFasciaBotY = b.groundClear + diffH - 0.01f;
        const float rearPanelTopY = b.isHighBox ? (b.yRoof - 0.03f) : yDeck;
        const float rearTopW = b.isHighBox ? b.roofHalfW : (b.beltHalfW * planWidthScale(b, xRear));
        const int rCols = 6;
        for (int i = 0; i < rCols; ++i) {
            const float u0 = lerpf(-1.0f, 1.0f, static_cast<float>(i) / static_cast<float>(rCols));
            const float u1 = lerpf(-1.0f, 1.0f, static_cast<float>(i + 1) / static_cast<float>(rCols));
            const float rx0 = xRear - rearBow * (1.0f - u0 * u0);
            const float rx1 = xRear - rearBow * (1.0f - u1 * u1);
            const float rxTop0 = xRear - rearBow * 0.7f * (1.0f - u0 * u0);
            const float rxTop1 = xRear - rearBow * 0.7f * (1.0f - u1 * u1);
            bodyMb.addQuad({rx1, rearFasciaBotY, u1 * wRear * 0.96f},
                           {rxTop1, rearPanelTopY,      u1 * rearTopW},
                           {rxTop0, rearPanelTopY,      u0 * rearTopW},
                           {rx0, rearFasciaBotY, u0 * wRear * 0.96f}, kWhite);
        }

        const float xRearCtr = xRear - rearBow;

        // Matte-black rear aero diffuser + vertical strakes + twin brushed-chrome exhaust tips!
        trimMb.addBox({xRearCtr + 0.04f, b.groundClear + diffH * 0.5f, 0.0f},
                      {0.12f, diffH, wRear * 1.86f}, kTrimBlack);
        for (int f = -2; f <= 2; ++f) {
            trimMb.addBox({xRearCtr - 0.005f, b.groundClear + diffH * 0.42f, static_cast<float>(f) * wRear * 0.24f},
                          {0.05f, diffH * 0.65f, 0.022f}, kTrimDark);
        }
        for (float s : {-1.0f, 1.0f}) {
            chromeMb.addCylinderX({xRearCtr - 0.015f, b.groundClear + 0.08f, s * wRear * 0.56f}, 0.046f, 0.10f, 12, kChromeCol);
            trimMb.addCylinderX({xRearCtr - 0.035f, b.groundClear + 0.08f, s * wRear * 0.56f}, 0.034f, 0.07f, 10, kTrimDark);
        }

        // Rear recessed Korean license plate pocket
        const float plateY = b.isHighBox ? (b.groundClear + 0.42f) : (b.groundClear + 0.35f);
        trimMb.addBox({xRearCtr - 0.004f, plateY, 0.0f}, {0.020f, 0.125f, 0.45f}, kTrimDark);
        chromeMb.addBox({xRearCtr - 0.011f, plateY, 0.0f}, {0.010f, 0.102f, 0.41f}, {244, 246, 250, 255});

        // Sculpted 3D wrap-around LED taillight bar + bright brake clusters!
        const float tlY = b.isHighBox ? (b.groundClear + 0.52f) : (yDeck - 0.105f);
        const float tlH = b.isHighBox ? 0.22f : 0.082f;
        const float tlZ = wRear * 0.68f;
        const float tlW = wRear * 0.40f;
        for (float s : {-1.0f, 1.0f}) {
            const float tx = xRear - rearBow * (1.0f - 0.68f * 0.68f);
            const float ang = -s * (b.isHighBox ? 0.05f : 0.22f);
            const Vec3 ax{std::cos(ang), 0.0f, std::sin(ang)};
            const Vec3 az{-ax.z, 0.0f, ax.x};
            trimMb.addOrientedBox({tx + 0.010f, tlY, s * tlZ}, {0.032f, tlH * 0.58f, tlW * 0.53f}, ax, {0, 1, 0}, az, kTrimDark);
            tailMb.addOrientedBox({tx - 0.010f, tlY, s * tlZ}, {0.022f, tlH * 0.48f, tlW * 0.48f}, ax, {0, 1, 0}, az, kTailRed);
        }
        // Sleek full-width LED connecting lightbar on modern Sports/Hyper/Luxury/SUV!
        if (b.bodyType == CarBodyType::CoupeFastback || b.bodyType == CarBodyType::SedanFourDoor ||
            b.bodyType == CarBodyType::SuvWagon) {
            tailMb.addBox({xRearCtr - 0.004f, tlY + 0.008f, 0.0f}, {0.032f, 0.024f, wRear * 1.44f}, shadeCol(kTailRed, 0.90f));
        }
    }

    // ------------------------------------------------------------------------
    // 5. MATHEMATICALLY COUPLED GREENHOUSE: 3D A/B/C/D PILLARS + ROOF + FLUSH CURVED GLASS!
    //    (Zero floating glass, zero zigzag weatherstrip, 100% seamless fit!)
    // ------------------------------------------------------------------------
    if (b.openTop) {
        // Classic Roadster: sleek raked chrome-framed windshield + open leather cockpit + chrome roll hoops!
        const int wSegs = 6;
        const int vSegs = 4;
        const float xBot = b.xCowl;
        const float xTop = b.xRoofFront;
        // Chrome A-pillars & top header bar
        for (float s : {-1.0f, 1.0f}) {
            addSolidPillar(chromeMb, b, xBot + 0.04f, xTop + 0.04f, xBot - 0.04f, xTop - 0.04f,
                           0.0f, 0.92f, vSegs, s, 0.0f, 0.045f, kChromeCol);
        }
        const Vec3 topL = greenhouseSurfacePt(b, xTop, 0.92f, -1.0f, 0.0f);
        const Vec3 topR = greenhouseSurfacePt(b, xTop, 0.92f,  1.0f, 0.0f);
        chromeMb.addBox({xTop, topL.y + 0.015f, 0.0f}, {0.055f, 0.042f, (topR.z - topL.z) + 0.04f}, kChromeCol);
        // Curved flush windshield glass
        for (int iv = 0; iv < vSegs; ++iv) {
            const float t0 = lerpf(0.02f, 0.90f, static_cast<float>(iv) / static_cast<float>(vSegs));
            const float t1 = lerpf(0.02f, 0.90f, static_cast<float>(iv + 1) / static_cast<float>(vSegs));
            const float x0 = lerpf(xBot, xTop, t0);
            const float x1 = lerpf(xBot, xTop, t1);
            const Vec3 l0 = greenhouseSurfacePt(b, x0, t0, -1.0f, 0.010f);
            const Vec3 l1 = greenhouseSurfacePt(b, x1, t1, -1.0f, 0.010f);
            for (int iw = 0; iw < wSegs; ++iw) {
                const float u0 = static_cast<float>(iw) / static_cast<float>(wSegs);
                const float u1 = static_cast<float>(iw + 1) / static_cast<float>(wSegs);
                const float bow0 = std::sin(u0 * kPi) * 0.06f;
                const float bow1 = std::sin(u1 * kPi) * 0.06f;
                const Vec3 a{x0 + bow0, l0.y + bow0 * 0.18f, lerpf(l0.z, -l0.z, u0)};
                const Vec3 bb{x0 + bow1, l0.y + bow1 * 0.18f, lerpf(l0.z, -l0.z, u1)};
                const Vec3 c{x1 + bow1, l1.y + bow1 * 0.18f, lerpf(l1.z, -l1.z, u1)};
                const Vec3 d{x1 + bow0, l1.y + bow0 * 0.18f, lerpf(l1.z, -l1.z, u0)};
                glassMb.addQuad(a, bb, c, d, kGlassTint);
            }
        }
        // Twin polished chrome roll hoops behind seats
        for (float s : {-1.0f, 1.0f}) {
            chromeMb.addCylinderY({b.xRearDeck + 0.08f, b.yRearDeck, s * 0.36f}, 0.026f, 0.026f, 0.28f, 10, kChromeCol);
            chromeMb.addCylinderZ({b.xRearDeck + 0.08f, b.yRearDeck + 0.28f, s * 0.36f}, 0.026f, 0.18f, 10, kChromeCol);
        }
    } else {
        // CLOSED CABIN GREENHOUSE (All Sedans, Coupes, Hypercars, SUVs, Pickups, Vans & Fire Trucks!)
        const int vSegs = 5;
        const float aWidth = b.isHighBox ? 0.105f : 0.090f;
        const float bWidth = 0.090f;
        const float cBotWidth = b.isHighBox ? 0.12f : (b.bodyType == CarBodyType::CoupeFastback ? 0.26f : 0.22f);
        const float cTopWidth = b.isHighBox ? 0.12f : (b.bodyType == CarBodyType::CoupeFastback ? 0.19f : 0.18f);

        for (float s : {-1.0f, 1.0f}) {
            // A-Pillar (painted body structural beam along exact greenhouse curvature!)
            addSolidPillar(bodyMb, b,
                           b.xCowl + 0.02f, b.xRoofFront + 0.02f,
                           b.xCowl - aWidth, b.xRoofFront - aWidth,
                           0.0f, 1.0f, vSegs, s, 0.0f, 0.065f, kWhite);

            // Roof Cant Rail (solid painted header rail joining A-pillar to C/D-pillar along top of windows!)
            addSolidPillar(bodyMb, b,
                           b.xRoofFront + 0.02f, b.xRoofFront + 0.02f,
                           b.xRoofRear - 0.02f, b.xRoofRear - 0.02f,
                           0.90f, 1.0f, 2, s, 0.0f, 0.075f, kWhite);

            // Continuous Gloss-Black Beltline Weatherstrip Molding (sits cleanly at t ∈ [0.00, 0.055], ZERO zigzag!)
            const float beltEndX = (b.isHighBox && !b.isSeoulBus) ? b.xBpillar : (b.xRearDeck + 0.04f);
            addCurvedGreenhousePanel(trimMb, b,
                                     b.xCowl - aWidth + 0.02f, b.xCowl - aWidth + 0.02f,
                                     beltEndX, beltEndX,
                                     0.0f, 0.055f, 1, s, 0.002f, kDloGloss, true);
            // Gloss-Black Upper DLO Trim Strip just under roof rail (t ∈ [0.875, 0.91])
            addCurvedGreenhousePanel(trimMb, b,
                                     b.xRoofFront - aWidth * 0.6f, b.xRoofFront - aWidth * 0.6f,
                                     (b.isHighBox && !b.isSeoulBus) ? b.xBpillar : (b.xRoofRear + 0.02f),
                                     (b.isHighBox && !b.isSeoulBus) ? b.xBpillar : (b.xRoofRear + 0.02f),
                                     0.875f, 0.912f, 1, s, 0.003f, kDloGloss, true);

            if (b.isSeoulBus) {
                // Authentic Seoul Low-Floor City Bus (현대 일렉시티 / 뉴슈퍼에어로시티):
                // Solid painted rear corner pillar + full-length panoramic tinted passenger windows with crisp dark mullions!
                addSolidPillar(bodyMb, b,
                               b.xRoofRear + 0.34f, b.xRoofRear + 0.34f,
                               b.xRearTip, b.xRearTip,
                               0.0f, 1.0f, 4, s, 0.0f, 0.075f, kWhite);
                const float busWinFrontBot = b.xCowl - aWidth + 0.020f;
                const float busWinFrontTop = b.xRoofFront - aWidth + 0.020f;
                const float busWinRear     = b.xRoofRear + 0.30f;
                // Continuous panoramic tinted side glass spanning the whole passenger cabin
                addCurvedGreenhousePanel(glassMb, b,
                                         busWinFrontBot, busWinFrontTop,
                                         busWinRear, busWinRear,
                                         0.03f, 0.895f, vSegs, s, 0.012f, kGlassTint, true);
                // 5 crisp gloss-black vertical window mullions dividing the panoramic bus windows
                for (int m = 1; m <= 5; ++m) {
                    const float mx = lerpf(busWinFrontTop - 0.12f, busWinRear + 0.12f, static_cast<float>(m) / 6.0f);
                    addSolidPillar(trimMb, b,
                                   mx + 0.045f, mx + 0.045f,
                                   mx - 0.045f, mx - 0.045f,
                                   0.03f, 0.90f, vSegs, s, 0.004f, 0.045f, kDloGloss);
                }
                // Right-side (+Z sidewalk) twin Seoul transit folding doors (front boarding + center exit!)
                if (s > 0.0f) {
                    const float doorCenters[2] = {b.halfL * 0.72f, b.halfL * 0.04f};
                    for (float dx : doorCenters) {
                        const float doorH = (b.yRoof - 0.16f) - (b.groundClear + 0.06f);
                        const float doorY = (b.groundClear + 0.06f) + doorH * 0.5f;
                        const float doorZ = b.beltHalfW + 0.008f;
                        // Dark brushed aluminum door frame + vertical twin-leaf seam
                        trimMb.addBox({dx, doorY, doorZ}, {1.06f, doorH, 0.018f}, kDloGloss);
                        // Full-height tinted glass leaves
                        for (float leaf : {-0.24f, 0.24f}) {
                            glassMb.addBox({dx + leaf, doorY + 0.12f, doorZ + 0.006f},
                                           {0.44f, doorH * 0.82f, 0.016f}, kGlassTint);
                        }
                        // Safety yellow grab bar inside door
                        liveryMb.addBox({dx, doorY - 0.18f, doorZ + 0.012f}, {0.032f, 0.72f, 0.014f}, {244, 198, 36, 255});
                    }
                }
            } else if (b.isHighBox) {
                // Commercial Van / Ambulance / FireTruck:
                // Front cab window between A-pillar and B-pillar, then SOLID crisp flat-panel cargo box behind B-pillar!
                addSolidPillar(bodyMb, b,
                               b.xBpillar + 0.04f, b.xBpillar + 0.04f,
                               b.xRearTip, b.xRearTip,
                               0.0f, 1.0f, 4, s, 0.0f, 0.07f, kWhite);
                // Flush cab side door glass (tucked 11mm inside A/B frame with 18mm jamb overlap -> ZERO slits!)
                addCurvedGreenhousePanel(glassMb, b,
                                         b.xCowl - aWidth + 0.022f, b.xRoofFront - aWidth + 0.022f,
                                         b.xBpillar + 0.018f, b.xBpillar + 0.018f,
                                         0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);
                // Crisp embossed horizontal body rib on flat van cargo sides (real stamped van steel!)
                const float ribY = lerpf(b.yBeltRear, b.yRoof, 0.34f);
                const float ribZ = b.beltHalfW * 0.992f;
                bodyMb.addBox({0.5f * (b.xBpillar - 0.14f + b.xRearTip + 0.14f), ribY, s * ribZ},
                              {(b.xBpillar - b.xRearTip) - 0.30f, 0.055f, 0.028f}, shadeCol(kWhite, 0.94f));
            } else {
                // Passenger Cars / Coupes / SUVs / Pickups:
                // Gloss-Black B-Pillar divider flush between front & rear side windows!
                addSolidPillar(trimMb, b,
                               b.xBpillar + bWidth * 0.5f, b.xBpillar + bWidth * 0.5f,
                               b.xBpillar - bWidth * 0.5f, b.xBpillar - bWidth * 0.5f,
                               0.03f, 0.91f, vSegs, s, 0.004f, 0.055f, kDloGloss);

                // Solid sculpted painted C-Pillar (and D-pillar on SUV/Wagon!)
                if (b.hasQuarterGlass) {
                    const float xCbot = lerpf(b.xBpillar, b.xRearDeck, 0.56f);
                    const float xCtop = lerpf(b.xBpillar, b.xRoofRear, 0.52f);
                    // C-Pillar (gloss-black / body pillar between rear door & cargo quarter glass)
                    addSolidPillar(trimMb, b,
                                   xCbot + 0.055f, xCtop + 0.055f,
                                   xCbot - 0.055f, xCtop - 0.055f,
                                   0.03f, 0.91f, vSegs, s, 0.004f, 0.06f, kDloGloss);
                    // D-Pillar (solid painted rear corner pillar)
                    addSolidPillar(bodyMb, b,
                                   b.xRearDeck + 0.22f, b.xRoofRear + 0.20f,
                                   b.xRearDeck - 0.02f, b.xRoofRear - 0.02f,
                                   0.0f, 1.0f, vSegs, s, 0.0f, 0.08f, kWhite);

                    // 3 flush curved side glass panes overlapping pillar jambs -> ZERO micro-gaps!
                    addCurvedGreenhousePanel(glassMb, b,
                                             b.xCowl - aWidth + 0.022f, b.xRoofFront - aWidth + 0.022f,
                                             b.xBpillar + bWidth * 0.30f, b.xBpillar + bWidth * 0.30f,
                                             0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);
                    addCurvedGreenhousePanel(glassMb, b,
                                             b.xBpillar - bWidth * 0.30f, b.xBpillar - bWidth * 0.30f,
                                             xCbot + 0.030f, xCtop + 0.030f,
                                             0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);
                    addCurvedGreenhousePanel(glassMb, b,
                                             xCbot - 0.030f, xCtop - 0.030f,
                                             b.xRearDeck + 0.18f, b.xRoofRear + 0.16f,
                                             0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);
                } else {
                    // Solid painted C-Pillar
                    addSolidPillar(bodyMb, b,
                                   b.xRearDeck + cBotWidth, b.xRoofRear + cTopWidth,
                                   b.xRearDeck - 0.02f, b.xRoofRear - 0.02f,
                                   0.0f, 1.0f, vSegs, s, 0.0f, 0.085f, kWhite);

                    // Flush curved Front Door Side Glass (overlaps A & B pillar channels cleanly -> ZERO gaps!)
                    addCurvedGreenhousePanel(glassMb, b,
                                             b.xCowl - aWidth + 0.022f, b.xRoofFront - aWidth + 0.022f,
                                             b.xBpillar + bWidth * 0.30f, b.xBpillar + bWidth * 0.30f,
                                             0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);

                    // Flush curved Rear Door / Fastback Quarter Glass between B & C pillars (100% sealed!)
                    const float rearWinFront = b.xBpillar - bWidth * 0.30f;
                    const float rearWinBackBot = std::min(b.xRearDeck + cBotWidth - 0.020f, rearWinFront - 0.04f);
                    const float rearWinBackTop = std::min(b.xRoofRear + cTopWidth - 0.020f, rearWinFront - 0.04f);
                    if (rearWinFront > rearWinBackBot + 0.02f) {
                        addCurvedGreenhousePanel(glassMb, b,
                                                 rearWinFront, rearWinFront,
                                                 rearWinBackBot, rearWinBackTop,
                                                 0.03f, 0.90f, vSegs, s, 0.011f, kGlassTint, true);
                    }
                }
            }
        }

        // Stamped Crowned Roof Panel joining Left & Right Roof Cant Rails seamlessly!
        {
            const int rSteps = 6;
            for (int i = 0; i < rSteps; ++i) {
                const float u0 = static_cast<float>(i) / static_cast<float>(rSteps);
                const float u1 = static_cast<float>(i + 1) / static_cast<float>(rSteps);
                const float x0 = lerpf(b.xRoofRear, b.xRoofFront, u0);
                const float x1 = lerpf(b.xRoofRear, b.xRoofFront, u1);
                const Vec3 l0 = greenhouseSurfacePt(b, x0, 1.0f, -1.0f, 0.0f);
                const Vec3 r0 = greenhouseSurfacePt(b, x0, 1.0f,  1.0f, 0.0f);
                const Vec3 l1 = greenhouseSurfacePt(b, x1, 1.0f, -1.0f, 0.0f);
                const Vec3 r1 = greenhouseSurfacePt(b, x1, 1.0f,  1.0f, 0.0f);
                const Vec3 m0{x0, l0.y + b.yRoofCrown, 0.0f};
                const Vec3 m1{x1, l1.y + b.yRoofCrown, 0.0f};
                bodyMb.addQuad(l0, m0, m1, l1, kWhite);
                bodyMb.addQuad(m0, r0, r1, m1, kWhite);
            }
            // Subtle glossy-black panoramic sunroof conforming 100% flush to 2-slope roof crown (ZERO holes or clipping!)
            if ((b.bodyType == CarBodyType::CoupeFastback || b.bodyType == CarBodyType::SedanFourDoor ||
                 b.bodyType == CarBodyType::SuvWagon) && b.yRoof >= 1.22f) {
                const float sunF = lerpf(b.xRoofRear, b.xRoofFront, 0.78f);
                const float sunB = lerpf(b.xRoofRear, b.xRoofFront, 0.28f);
                const float sunFrac = 0.62f;
                const float sunW = b.roofHalfW * sunFrac;
                const float ySideF = roofSideYAt(b, sunF) + b.yRoofCrown * (1.0f - sunFrac) + 0.005f;
                const float ySideB = roofSideYAt(b, sunB) + b.yRoofCrown * (1.0f - sunFrac) + 0.005f;
                const float yMidF  = roofSideYAt(b, sunF) + b.yRoofCrown + 0.005f;
                const float yMidB  = roofSideYAt(b, sunB) + b.yRoofCrown + 0.005f;
                trimMb.addQuad({sunB, ySideB, -sunW}, {sunB, yMidB, 0.0f},
                               {sunF, yMidF, 0.0f},   {sunF, ySideF, -sunW}, kDloGloss);
                trimMb.addQuad({sunB, yMidB, 0.0f},   {sunB, ySideB, sunW},
                               {sunF, ySideF, sunW},  {sunF, yMidF, 0.0f},   kDloGloss);
            }
        }

        // Flush Curved Front Windshield (coupled to EXACT A-pillar & roof header coordinates!)
        {
            const int wSegs = 6;
            const int vS = 4;
            for (int iv = 0; iv < vS; ++iv) {
                const float t0 = lerpf(0.02f, 0.96f, static_cast<float>(iv) / static_cast<float>(vS));
                const float t1 = lerpf(0.02f, 0.96f, static_cast<float>(iv + 1) / static_cast<float>(vS));
                const float x0 = lerpf(b.xCowl, b.xRoofFront, t0);
                const float x1 = lerpf(b.xCowl, b.xRoofFront, t1);
                const Vec3 l0 = greenhouseSurfacePt(b, x0, t0, -1.0f, 0.012f);
                const Vec3 l1 = greenhouseSurfacePt(b, x1, t1, -1.0f, 0.012f);
                for (int iw = 0; iw < wSegs; ++iw) {
                    const float u0 = static_cast<float>(iw) / static_cast<float>(wSegs);
                    const float u1 = static_cast<float>(iw + 1) / static_cast<float>(wSegs);
                    const float bow0 = std::sin(u0 * kPi) * (b.isHighBox ? 0.032f : 0.068f);
                    const float bow1 = std::sin(u1 * kPi) * (b.isHighBox ? 0.032f : 0.068f);
                    const float crown0 = std::sin(u0 * kPi) * b.yRoofCrown * t0;
                    const float crown1 = std::sin(u1 * kPi) * b.yRoofCrown * t0;
                    const float crown0b = std::sin(u0 * kPi) * b.yRoofCrown * t1;
                    const float crown1b = std::sin(u1 * kPi) * b.yRoofCrown * t1;
                    const Vec3 a{x0 + bow0, l0.y + crown0, lerpf(l0.z, -l0.z, u0)};
                    const Vec3 bb{x0 + bow1, l0.y + crown1, lerpf(l0.z, -l0.z, u1)};
                    const Vec3 c{x1 + bow1, l1.y + crown1b, lerpf(l1.z, -l1.z, u1)};
                    const Vec3 d{x1 + bow0, l1.y + crown0b, lerpf(l1.z, -l1.z, u0)};
                    glassMb.addQuad(a, bb, c, d, kGlassTint);
                }
            }
            // Gloss-black ceramic frit header strip at top of windshield
            const Vec3 hl = greenhouseSurfacePt(b, b.xRoofFront, 0.96f, -1.0f, 0.008f);
            trimMb.addBox({b.xRoofFront + 0.01f, hl.y + 0.01f, 0.0f}, {0.06f, 0.04f, std::abs(hl.z) * 2.0f}, kDloGloss);
        }

        // Flush Curved Rear Window Glass (coupled to EXACT C-pillar & roof rear coordinates!)
        if (!b.isHighBox) {
            const int wSegs = 6;
            const int vS = 4;
            for (int iv = 0; iv < vS; ++iv) {
                const float t0 = lerpf(0.03f, 0.95f, static_cast<float>(iv) / static_cast<float>(vS));
                const float t1 = lerpf(0.03f, 0.95f, static_cast<float>(iv + 1) / static_cast<float>(vS));
                const float x0 = lerpf(b.xRearDeck, b.xRoofRear, t0);
                const float x1 = lerpf(b.xRearDeck, b.xRoofRear, t1);
                const Vec3 l0 = greenhouseSurfacePt(b, x0, t0, -1.0f, 0.014f);
                const Vec3 l1 = greenhouseSurfacePt(b, x1, t1, -1.0f, 0.014f);
                const float zIn0 = std::abs(l0.z) * 0.90f;
                const float zIn1 = std::abs(l1.z) * 0.90f;
                for (int iw = 0; iw < wSegs; ++iw) {
                    const float u0 = static_cast<float>(iw) / static_cast<float>(wSegs);
                    const float u1 = static_cast<float>(iw + 1) / static_cast<float>(wSegs);
                    const float bow0 = -std::sin(u0 * kPi) * 0.045f;
                    const float bow1 = -std::sin(u1 * kPi) * 0.045f;
                    const float c0 = std::sin(u0 * kPi) * b.yRoofCrown * t0;
                    const float c1 = std::sin(u1 * kPi) * b.yRoofCrown * t0;
                    const float c0b = std::sin(u0 * kPi) * b.yRoofCrown * t1;
                    const float c1b = std::sin(u1 * kPi) * b.yRoofCrown * t1;
                    const Vec3 a{x0 + bow0, l0.y + c0, lerpf(-zIn0, zIn0, u0)};
                    const Vec3 bb{x0 + bow1, l0.y + c1, lerpf(-zIn0, zIn0, u1)};
                    const Vec3 c{x1 + bow1, l1.y + c1b, lerpf(-zIn1, zIn1, u1)};
                    const Vec3 d{x1 + bow0, l1.y + c0b, lerpf(-zIn1, zIn1, u0)};
                    glassMb.addQuad(bb, a, d, c, kGlassTint);
                }
            }
        } else if (b.isSeoulBus) {
            // Wide Seoul Bus Rear Panoramic Window + Rear Engine Grille Louvers
            const float winY = lerpf(b.yBeltRear, b.yRoof, 0.56f);
            const float winH = (b.yRoof - b.yBeltRear) * 0.56f;
            glassMb.addBox({b.xRearTip - 0.010f, winY, 0.0f}, {0.022f, winH, b.roofHalfW * 1.62f}, kGlassTint);
            trimMb.addBox({b.xRearTip - 0.004f, winY, 0.0f}, {0.016f, winH + 0.06f, b.roofHalfW * 1.68f}, kDloGloss);
            // Rear engine compartment air vents
            for (int lv = 0; lv < 4; ++lv) {
                const float ly = b.groundClear + 0.28f + static_cast<float>(lv) * 0.14f;
                trimMb.addBox({b.xRearTip - 0.010f, ly, 0.0f}, {0.020f, 0.045f, b.halfW * 1.24f}, kTrimDark);
            }
        } else {
            // Commercial Van / Ambulance rear twin window panes + vertical center door seam!
            const float winY = lerpf(b.yBeltRear, b.yRoof, 0.56f);
            const float winH = (b.yRoof - b.yBeltRear) * 0.52f;
            if (!b.isFireTruck) {
                for (float s : {-1.0f, 1.0f}) {
                    glassMb.addBox({b.xRearTip - 0.008f, winY, s * b.roofHalfW * 0.44f},
                                   {0.02f, winH, b.roofHalfW * 0.68f}, kGlassTint);
                    trimMb.addBox({b.xRearTip - 0.003f, winY, s * b.roofHalfW * 0.44f},
                                  {0.016f, winH + 0.04f, b.roofHalfW * 0.72f}, kDloGloss);
                }
                // Rear vertical double-door shutline & chrome handle
                trimMb.addBox({b.xRearTip - 0.006f, 0.5f * (b.groundClear + b.yRoof), 0.0f},
                              {0.015f, (b.yRoof - b.groundClear) * 0.88f, 0.018f}, kTrimDark);
                chromeMb.addBox({b.xRearTip - 0.016f, b.yBeltRear - 0.08f, 0.08f},
                                {0.024f, 0.11f, 0.030f}, kChromeCol);
            }
        }
    }

    // ------------------------------------------------------------------------
    // 6. REAL 3D DARK LUXURY INTERIOR CABIN (Dashboard, Steering Wheel, Bucket Seats!)
    //    (Strictly clamped below roofline — ZERO headrest or seat roof clipping!)
    // ------------------------------------------------------------------------
    {
        const float cabY = b.groundClear + 0.12f;
        const float maxSeatTop = std::max(cabY + 0.36f, b.yRoof - 0.18f);
        const float dashX = b.xCowl - 0.16f;
        const float seatX = lerpf(b.xCowl, b.xBpillar, 0.68f);
        const float seatZ = b.beltHalfW * 0.38f;
        const float dashW = b.beltHalfW * 1.56f;
        // Sculpted dark dashboard & instrument binnacle
        interiorMb.addBox({dashX, std::min(b.yCowl - 0.10f, maxSeatTop - 0.16f), 0.0f}, {0.34f, 0.22f, dashW}, kInteriorDark);
        interiorMb.addBox({dashX - 0.06f, std::min(b.yCowl + 0.01f, maxSeatTop - 0.08f), -seatZ}, {0.16f, 0.08f, 0.32f}, kTrimDark);
        // Center infotainment screen + center console tunnel
        interiorMb.addBox({dashX - 0.12f, std::min(b.yCowl, maxSeatTop - 0.09f), 0.0f}, {0.05f, 0.12f, 0.22f}, {32, 46, 62, 255});
        interiorMb.addBox({seatX, cabY + 0.16f, 0.0f}, {1.20f, 0.28f, 0.24f}, kInteriorDark);
        // Left-hand drive steering column & rim
        interiorMb.addCylinderX({dashX - 0.20f, std::min(b.yCowl - 0.03f, maxSeatTop - 0.12f), -seatZ}, 0.13f, 0.04f, 12, kTrimDark);
        // Twin front bucket seats + bolsters + headrests (strictly below maxSeatTop!)
        const float backH = std::min(0.46f, std::max(0.24f, (maxSeatTop - 0.14f) - (cabY + 0.16f)));
        const float backCenterY = cabY + 0.16f + backH * 0.5f;
        const float headCenterY = std::min(cabY + 0.16f + backH + 0.065f, maxSeatTop - 0.065f);
        for (float s : {-1.0f, 1.0f}) {
            if (b.isSeoulBus && s > 0.0f) continue; // Right side is front boarding aisle on Seoul Bus!
            interiorMb.addBox({seatX, cabY + 0.12f, s * seatZ}, {0.44f, 0.12f, 0.42f}, kInteriorSeat);
            interiorMb.addBox({seatX - 0.17f, backCenterY, s * seatZ}, {0.13f, backH, 0.40f}, kInteriorSeat);
            interiorMb.addBox({seatX - 0.19f, headCenterY, s * seatZ}, {0.10f, 0.13f, 0.20f}, kInteriorDark);
        }
        // Passenger rows & yellow grab poles inside Seoul City Bus (visible through panoramic glass!)
        if (b.isSeoulBus) {
            const ColorRGBA kBusSeatCol{48, 72, 118, 255};
            for (int row = 0; row < 6; ++row) {
                const float rx = lerpf(b.halfL * 0.48f, -b.halfL * 0.78f, static_cast<float>(row) / 5.0f);
                for (float s : {-1.0f, 1.0f}) {
                    if (s > 0.0f && row == 2) continue; // Center exit doorway gap
                    interiorMb.addBox({rx, cabY + 0.36f, s * (b.beltHalfW * 0.58f)}, {0.42f, 0.12f, 0.54f}, kBusSeatCol);
                    interiorMb.addBox({rx - 0.18f, cabY + 0.70f, s * (b.beltHalfW * 0.58f)}, {0.10f, 0.58f, 0.52f}, kBusSeatCol);
                }
            }
        }
        // Rear bench seat (ONLY for 4-door sedans/SUVs/Compacts with full rear headroom — NEVER low-slung 2-seat supercars!)
        if (!b.openTop && !b.isHighBox && !b.openBed && b.yRoof >= 1.24f) {
            const float rearSeatX = lerpf(b.xBpillar, b.xRearDeck, 0.40f);
            const float rMaxTop = std::max(cabY + 0.34f, b.yRoof - 0.22f);
            const float rBackH = std::min(0.42f, std::max(0.22f, (rMaxTop - 0.12f) - (cabY + 0.15f)));
            interiorMb.addBox({rearSeatX, cabY + 0.12f, 0.0f}, {0.40f, 0.14f, dashW * 0.84f}, kInteriorSeat);
            interiorMb.addBox({rearSeatX - 0.15f, cabY + 0.15f + rBackH * 0.5f, 0.0f}, {0.13f, rBackH, dashW * 0.82f}, kInteriorSeat);
            for (float s : {-1.0f, 1.0f}) {
                interiorMb.addBox({rearSeatX - 0.17f, std::min(cabY + 0.15f + rBackH + 0.06f, rMaxTop - 0.06f), s * seatZ},
                                  {0.09f, 0.12f, 0.19f}, kInteriorDark);
            }
        }
    }

    // ------------------------------------------------------------------------
    // 7. AERODYNAMIC WING MIRRORS, FLUSH DOOR HANDLES & CONFORMAL 4.5mm DOOR SHUTLINES
    // ------------------------------------------------------------------------
    for (float s : {-1.0f, 1.0f}) {
        const float mirX = b.xCowl - 0.12f;
        const float mirY = beltYAt(b, mirX) + 0.04f;
        const float mirZ = b.beltHalfW * planWidthScale(b, mirX);
        // Matte-black mirror stalk + painted aero cap + glass mirror face
        trimMb.addBox({mirX, mirY, s * (mirZ + 0.055f)}, {0.06f, 0.032f, 0.11f}, kTrimBlack);
        bodyMb.addBox({mirX + 0.01f, mirY + 0.025f, s * (mirZ + 0.125f)}, {0.11f, 0.088f, 0.145f}, kWhite);
        trimMb.addBox({mirX - 0.042f, mirY + 0.025f, s * (mirZ + 0.125f)}, {0.015f, 0.072f, 0.125f}, {44, 52, 64, 255});

        // Flush sculpted door handles (front + rear if 4-door)
        if (!b.isSeoulBus) {
            const float hFrontX = b.xBpillar + 0.18f;
            const float hY = beltYAt(b, hFrontX) - 0.11f;
            const float hZ = b.halfW * planWidthScale(b, hFrontX) * 0.992f;
            chromeMb.addBox({hFrontX, hY, s * (hZ + 0.012f)}, {0.15f, 0.032f, 0.026f}, kChromeCol);
            if (b.hasRearDoor) {
                const float hRearX = lerpf(b.xBpillar, b.xRearDeck, 0.42f);
                const float hRY = beltYAt(b, hRearX) - 0.11f;
                const float hRZ = b.halfW * planWidthScale(b, hRearX) * 0.992f;
                chromeMb.addBox({hRearX, hRY, s * (hRZ + 0.012f)}, {0.14f, 0.032f, 0.026f}, kChromeCol);
            }
        }

        // Ultra-crisp 4.5mm conformal door shutlines hugging all 3 sculpted flank bands cleanly!
        auto addDoorSeam = [&](float seamX) {
            if (seamX <= b.rearAxleX + archR + 0.04f || seamX >= b.frontAxleX - archR - 0.04f) return;
            const float wS = planWidthScale(b, seamX);
            const float yBeltS = beltYAt(b, seamX) - 0.008f;
            const float yBotS  = b.groundClear + 0.055f;
            const float yCreaseS = lerpf(b.groundClear, yBeltS, b.characterCreaseH);
            const float yMidS  = lerpf(yBotS, yCreaseS, 0.52f);
            const float ys[4] = {yBotS, yMidS, yCreaseS, yBeltS};
            const float zs[4] = {
                b.halfW * wS * 0.945f + 0.003f,
                b.halfW * wS * 0.980f + 0.003f,
                b.halfW * wS * 1.005f + 0.003f,
                b.beltHalfW * wS + 0.003f
            };
            const float hw = 0.0024f;
            for (int band = 0; band < 3; ++band) {
                const Vec3 a{seamX - hw, ys[band],     s * zs[band]};
                const Vec3 bb{seamX + hw, ys[band],    s * zs[band]};
                const Vec3 c{seamX + hw, ys[band + 1], s * zs[band + 1]};
                const Vec3 d{seamX - hw, ys[band + 1], s * zs[band + 1]};
                if (s > 0.0f) trimMb.addQuad(a, bb, c, d, kTrimDark);
                else          trimMb.addQuad(bb, a, d, c, kTrimDark);
            }
        };
        if (!b.isSeoulBus) {
            addDoorSeam(b.xCowl - 0.08f);
            addDoorSeam(b.xBpillar);
            if (b.hasRearDoor) addDoorSeam(b.rearAxleX + archR + 0.06f);
        }
    }

    // ------------------------------------------------------------------------
    // 8. CLEAN ZERO-Z-FIGHTING SPECIAL VEHICLE LIVERIES, ROOF BEACONS & WINGS
    // ------------------------------------------------------------------------
    if (b.isPolice) {
        // Authentic Seoul Metropolitan Police (서울 경찰) sleek low-profile LED lightbar + clean door insignia
        const float barY = b.yRoof + b.yRoofCrown + 0.055f;
        const float barX = b.xBpillar + 0.06f;
        trimMb.addBox({barX, barY - 0.035f, 0.0f}, {0.18f, 0.045f, b.roofHalfW * 1.48f}, kTrimDark);
        // Left Red + Center White + Right Blue LED pods (rendered cleanly!)
        liveryMb.addBox({barX, barY + 0.008f, -b.roofHalfW * 0.42f}, {0.16f, 0.068f, b.roofHalfW * 0.58f}, {235, 34, 46, 255});
        liveryMb.addBox({barX, barY + 0.008f,  0.0f},                {0.16f, 0.068f, b.roofHalfW * 0.24f}, {240, 245, 252, 255});
        liveryMb.addBox({barX, barY + 0.008f,  b.roofHalfW * 0.42f}, {0.16f, 0.068f, b.roofHalfW * 0.58f}, {32, 112, 248, 255});
        // Crisp navy Seoul Police door band (proud +0.012m off planar mid-door -> zero Z-fight!)
        for (float s : {-1.0f, 1.0f}) {
            const float x0 = b.rearAxleX + archR + 0.08f;
            const float x1 = b.frontAxleX - archR - 0.08f;
            const float bandY = lerpf(b.groundClear, b.yCowl, 0.48f);
            liveryMb.addBox({0.5f * (x0 + x1), bandY, s * (b.halfW * 1.008f)},
                            {x1 - x0, 0.16f, 0.016f}, {22, 46, 98, 255});
            liveryMb.addBox({0.5f * (x0 + x1), bandY + 0.105f, s * (b.halfW * 1.010f)},
                            {x1 - x0, 0.040f, 0.016f}, {244, 198, 42, 255});
        }
        // Front push-bar bumper guard
        trimMb.addBox({b.xFrontNose + 0.06f, b.groundClear + 0.30f, 0.0f}, {0.06f, 0.055f, 0.86f}, kTrimBlack);
        for (float s : {-1.0f, 1.0f}) {
            trimMb.addBox({b.xFrontNose + 0.05f, b.groundClear + 0.28f, s * 0.34f}, {0.09f, 0.38f, 0.055f}, kTrimBlack);
        }
    } else if (b.isTaxi) {
        // Authentic Seoul Haechi Taxi illuminated roof beacon!
        const float signY = b.yRoof + b.yRoofCrown + 0.075f;
        trimMb.addBox({b.xBpillar + 0.12f, signY - 0.045f, 0.0f}, {0.16f, 0.03f, 0.46f}, kTrimDark);
        liveryMb.addBox({b.xBpillar + 0.12f, signY + 0.01f, 0.0f}, {0.15f, 0.10f, 0.44f}, {255, 244, 210, 255});
    } else if (b.isAmbulance) {
        // Authentic Korean 119 Ambulance roof emergency lightbars + crisp red medical stripe (on strictly flat cargo box!)
        const float barY = b.yRoof + 0.07f;
        liveryMb.addBox({b.xRoofFront - 0.16f, barY, -b.roofHalfW * 0.45f}, {0.18f, 0.09f, b.roofHalfW * 0.65f}, {238, 36, 42, 255});
        liveryMb.addBox({b.xRoofFront - 0.16f, barY,  b.roofHalfW * 0.45f}, {0.18f, 0.09f, b.roofHalfW * 0.65f}, {238, 36, 42, 255});
        for (float s : {-1.0f, 1.0f}) {
            const float xC = 0.5f * (b.xBpillar - 0.08f + b.xRearTip + 0.08f);
            const float len = (b.xBpillar - b.xRearTip) - 0.20f;
            liveryMb.addBox({xC, b.yBeltRear + 0.14f, s * (b.beltHalfW + 0.010f)}, {len, 0.14f, 0.016f}, {218, 34, 42, 255});
            // Red medical cross emblem
            liveryMb.addBox({xC, b.yBeltRear + 0.52f, s * (b.beltHalfW + 0.012f)}, {0.14f, 0.42f, 0.016f}, {218, 34, 42, 255});
            liveryMb.addBox({xC, b.yBeltRear + 0.52f, s * (b.beltHalfW + 0.012f)}, {0.42f, 0.14f, 0.016f}, {218, 34, 42, 255});
        }
    } else if (b.isFireTruck) {
        // Authentic Seoul 119 Fire Pumper: silver side equipment roller shutters + roof aluminum extension ladder + lightbar!
        const float barY = b.yRoof + 0.08f;
        liveryMb.addBox({b.xRoofFront - 0.18f, barY, 0.0f}, {0.20f, 0.11f, b.roofHalfW * 1.65f}, {242, 36, 42, 255});
        for (float s : {-1.0f, 1.0f}) {
            // Two brushed aluminum equipment shutters on each flank (proud +0.014m off flat pump body!)
            for (int bay = 0; bay < 2; ++bay) {
                const float bx = lerpf(b.xBpillar - 0.65f, b.xRearTip + 0.85f, static_cast<float>(bay));
                chromeMb.addBox({bx, lerpf(b.groundClear, b.yRoof, 0.54f), s * (b.beltHalfW + 0.012f)},
                                {1.15f, 0.96f, 0.022f}, {198, 204, 212, 255});
            }
        }
        // Aluminum roof extension ladder rails & rungs
        const float ladLen = b.halfL * 1.48f;
        const float ladX = -b.halfL * 0.16f;
        const float ladY = b.yRoof + 0.14f;
        for (float s : {-1.0f, 1.0f}) {
            chromeMb.addBox({ladX, ladY, s * 0.34f}, {ladLen, 0.10f, 0.06f}, kChromeCol);
        }
        for (int r = 0; r < 11; ++r) {
            const float rx = lerpf(ladX - ladLen * 0.46f, ladX + ladLen * 0.46f, static_cast<float>(r) / 10.0f);
            chromeMb.addBox({rx, ladY, 0.0f}, {0.04f, 0.04f, 0.66f}, kChromeCol);
        }
    } else if (b.isSeoulBus) {
        // Authentic Seoul City Bus (파랑 간선버스 / 초록 지선버스):
        // Crisp white lower Seoul livery beltline wave + roof twin white HVAC / electric battery pods + front LED route bezel!
        for (float s : {-1.0f, 1.0f}) {
            const float bandY = b.yBeltRear - 0.11f;
            liveryMb.addBox({0.0f, bandY, s * (b.halfW + 0.010f)}, {b.halfL * 1.84f, 0.16f, 0.018f}, {244, 246, 250, 255});
            liveryMb.addBox({0.0f, bandY - 0.105f, s * (b.halfW + 0.010f)}, {b.halfL * 1.84f, 0.045f, 0.016f}, {214, 224, 238, 255});
        }
        // Front crisp white accent fascia strip + dark upper LED destination sign housing
        liveryMb.addBox({b.xFrontNose + 0.010f, b.yNose - 0.14f, 0.0f}, {0.022f, 0.14f, b.halfW * 1.76f}, {244, 246, 250, 255});
        trimMb.addBox({b.xRoofFront + 0.045f, b.yRoof - 0.16f, 0.0f}, {0.14f, 0.34f, b.roofHalfW * 1.56f}, kDloGloss);
        // Twin white rooftop HVAC / electric battery pods
        liveryMb.addBox({ 0.85f, b.yRoof + 0.18f, 0.0f}, {2.45f, 0.28f, b.roofHalfW * 1.34f}, {238, 242, 246, 255});
        liveryMb.addBox({-2.05f, b.yRoof + 0.16f, 0.0f}, {2.25f, 0.24f, b.roofHalfW * 1.28f}, {234, 238, 244, 255});
        trimMb.addBox({ 0.85f, b.yRoof + 0.32f, 0.0f}, {1.85f, 0.03f, b.roofHalfW * 0.86f}, kTrimDark);
        trimMb.addBox({-2.05f, b.yRoof + 0.28f, 0.0f}, {1.65f, 0.03f, b.roofHalfW * 0.82f}, kTrimDark);
    } else if (b.isDeliveryVan) {
        // Clean flat-panel modern logistics accent trim (proud +0.012m off strictly planar cargo wall — ZERO Z-fighting!)
        for (float s : {-1.0f, 1.0f}) {
            const float xC = 0.5f * (b.xBpillar - 0.16f + b.xRearTip + 0.16f);
            const float len = (b.xBpillar - b.xRearTip) - 0.36f;
            liveryMb.addBox({xC, lerpf(b.yBeltRear, b.yRoof, 0.58f), s * (b.beltHalfW + 0.010f)},
                            {len, 0.13f, 0.014f}, {32, 92, 186, 255});
        }
    }

    // Optional clean co-planar twin racing stripes (for garage customization!)
    if (!b.isHighBox && !b.openBed) {
        const float sw = 0.135f;
        const float gap = 0.175f;
        for (float s : {-1.0f, 1.0f}) {
            // Hood stripe (conforms cleanly to hood angle + 0.008m above)
            const Vec3 h0{b.xCowl,      beltYAt(b, b.xCowl) + 0.046f,      s * (gap - sw * 0.5f)};
            const Vec3 h1{b.xCowl,      beltYAt(b, b.xCowl) + 0.046f,      s * (gap + sw * 0.5f)};
            const Vec3 h2{b.xHoodStart, beltYAt(b, b.xHoodStart) + 0.036f, s * (gap + sw * 0.5f)};
            const Vec3 h3{b.xHoodStart, beltYAt(b, b.xHoodStart) + 0.036f, s * (gap - sw * 0.5f)};
            if (s > 0.0f) stripeMb.addQuad(h0, h1, h2, h3, kWhite);
            else          stripeMb.addQuad(h1, h0, h3, h2, kWhite);

            if (!b.openTop) {
                const float rY = b.yRoof + b.yRoofCrown + 0.009f;
                const Vec3 r0{b.xRoofRear,  rY, s * (gap - sw * 0.5f)};
                const Vec3 r1{b.xRoofRear,  rY, s * (gap + sw * 0.5f)};
                const Vec3 r2{b.xRoofFront, rY, s * (gap + sw * 0.5f)};
                const Vec3 r3{b.xRoofFront, rY, s * (gap - sw * 0.5f)};
                if (s > 0.0f) stripeMb.addQuad(r0, r1, r2, r3, kWhite);
                else          stripeMb.addQuad(r1, r0, r3, r2, kWhite);
            }
        }
    }

    // Sculpted Rear Aero Spoiler / Swan-Neck Carbon GT Wing!
    if (b.integratedSpoiler || b.tallGtWing) {
        const float wingX = b.xRearTip + 0.14f;
        const float deckY = beltYAt(b, wingX);
        const float wingW = b.halfW * 1.72f;
        if (b.tallGtWing) {
            const float wingY = deckY + 0.24f;
            // Carbon wing blade + sculpted endplates + twin swan-neck pylons
            spoilerMb.addBox({wingX, wingY, 0.0f}, {0.24f, 0.034f, wingW}, kTrimBlack);
            for (float s : {-1.0f, 1.0f}) {
                spoilerMb.addBox({wingX, wingY + 0.01f, s * wingW * 0.5f}, {0.28f, 0.14f, 0.022f}, kTrimBlack);
                spoilerMb.addBox({wingX + 0.04f, deckY + 0.12f, s * wingW * 0.26f}, {0.10f, 0.24f, 0.026f}, kTrimDark);
            }
        } else {
            // Sleek integrated ducktail / lip spoiler
            spoilerMb.addBox({wingX, deckY + 0.075f, 0.0f}, {0.17f, 0.032f, wingW * 0.90f}, kTrimBlack);
            for (float s : {-1.0f, 1.0f}) {
                spoilerMb.addBox({wingX, deckY + 0.035f, s * wingW * 0.34f}, {0.09f, 0.075f, 0.045f}, kTrimDark);
            }
        }
    }

    CarMeshSet set{};
    // 24-degree hard-crease split gives RAZOR-SHARP stamped automotive steel character lines (ZERO JELLY!)
    set.body      = bodyMb.upload(vk, 24.0f);
    set.glass     = glassMb.upload(vk, 48.0f);   // Smooth curved automotive laminated glass
    set.interior  = interiorMb.upload(vk, 18.0f);
    set.trim      = trimMb.upload(vk, 18.0f);
    set.chrome    = chromeMb.upload(vk, 24.0f);
    set.lampsHead = headMb.upload(vk, 18.0f);
    set.lampsTail = tailMb.upload(vk, 18.0f);
    set.livery    = liveryMb.upload(vk, 18.0f);
    set.stripes   = stripeMb.upload(vk, 24.0f);
    set.spoiler   = spoilerMb.upload(vk, 20.0f);

    set.frontAxleX = b.frontAxleX;
    set.rearAxleX  = b.rearAxleX;
    set.wheelTrackZ = b.halfW * 0.96f - b.wheelW * 0.48f;
    set.wheelRadius = b.wheelR;
    set.wheelWidth  = b.wheelW;

    const bool isSportAero = (b.bodyType == CarBodyType::CoupeFastback ||
                              b.bodyType == CarBodyType::RoadsterOpen);
    const float bowMax = b.isHighBox ? 0.040f : (isSportAero ? 0.135f : 0.095f);
    const float rearBow = b.isHighBox ? 0.020f : 0.068f;
    set.frontPlateLocal = {b.xFrontNose + bowMax + 0.024f, b.groundClear + (isSportAero ? 0.145f : 0.19f), 0.0f};
    set.rearPlateLocal  = {b.xRearTip - rearBow - 0.018f,  b.isHighBox ? (b.groundClear + 0.42f) : (b.groundClear + 0.35f), 0.0f};
    set.busSignLocal    = {b.xRoofFront + 0.125f, b.yRoof - 0.16f, 0.0f};
    return set;
}

// Builds the shared unit-radius (R=1.0, width=1.0) sculpted tire + 10-spoke alloy rim + brake rotor!
// Renderer3D scales cleanly by (wheelRadius, wheelRadius, wheelWidth) for every vehicle!
WheelMeshSet buildSharedWheel(VulkanContext& vk) {
    MeshBuilder3D tireMb, rimMb, discMb, calMb;

    const int segs = 22;
    const ColorRGBA kRubber{28, 30, 34, 255};
    const ColorRGBA kSidewall{36, 38, 42, 255};
    const ColorRGBA kRimCol{236, 240, 246, 255};
    const ColorRGBA kDiscCol{175, 182, 192, 255};
    const ColorRGBA kCaliperRed{218, 34, 42, 255};

    // Sculpted curved-shoulder rubber tire profile (unit radius = 1.0, rim bead = 0.66, half-width = 0.5)
    struct ProfilePt { float r; float z; ColorRGBA col; };
    const ProfilePt prof[8] = {
        {0.66f, -0.44f, kSidewall},
        {0.88f, -0.50f, kSidewall},
        {0.97f, -0.46f, kRubber},
        {1.00f, -0.34f, kRubber},
        {1.00f,  0.34f, kRubber},
        {0.97f,  0.46f, kRubber},
        {0.88f,  0.50f, kSidewall},
        {0.66f,  0.44f, kSidewall}
    };
    for (int i = 0; i < segs; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        for (int p = 0; p < 7; ++p) {
            const Vec3 v00{c0 * prof[p].r,     s0 * prof[p].r,     prof[p].z};
            const Vec3 v10{c1 * prof[p].r,     s1 * prof[p].r,     prof[p].z};
            const Vec3 v11{c1 * prof[p + 1].r, s1 * prof[p + 1].r, prof[p + 1].z};
            const Vec3 v01{c0 * prof[p + 1].r, s0 * prof[p + 1].r, prof[p + 1].z};
            tireMb.addQuad(v00, v10, v11, v01, prof[p].col);
        }
    }

    // Deep-dish 10-spoke machined alloy rim + center lug hub!
    const float rRim = 0.67f;
    // Outer barrel + inner lip
    for (int i = 0; i < segs; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        // Outer metallic lip ring (both sides so left/right wheels both look crisp!)
        for (float side : {-1.0f, 1.0f}) {
            const float zLip = side * 0.44f;
            const float zIn  = side * 0.36f;
            rimMb.addQuad({c0 * rRim, s0 * rRim, zLip}, {c1 * rRim, s1 * rRim, zLip},
                          {c1 * 0.59f, s1 * 0.59f, zIn}, {c0 * 0.59f, s0 * 0.59f, zIn}, kRimCol);
        }
        // Dark barrel inside rim
        rimMb.addQuad({c0 * 0.59f, s0 * 0.59f, -0.36f}, {c1 * 0.59f, s1 * 0.59f, -0.36f},
                      {c1 * 0.59f, s1 * 0.59f,  0.36f}, {c0 * 0.59f, s0 * 0.59f,  0.36f}, shadeCol(kRimCol, 0.55f));
    }
    // Center lug hub cap
    rimMb.addCylinderZ({0.0f, 0.0f, 0.0f}, 0.17f, 0.78f, 12, kRimCol);
    // 5 crisp twin-Y performance spokes (10 spokes!)
    for (int sp = 0; sp < 5; ++sp) {
        const float baseA = kTwoPi * static_cast<float>(sp) / 5.0f;
        for (float dA : {-0.14f, 0.14f}) {
            const float ang = baseA + dA;
            const Vec3 dir{std::cos(ang), std::sin(ang), 0.0f};
            const Vec3 tan{-dir.y, dir.x, 0.0f};
            for (float side : {-1.0f, 1.0f}) {
                const Vec3 c = dir * 0.38f + Vec3{0.0f, 0.0f, side * 0.34f};
                rimMb.addOrientedBox(c, {0.25f, 0.036f, 0.045f}, dir, tan, {0, 0, 1}, kRimCol);
            }
        }
    }

    // Ventilated Brake Rotor Disc + Performance Caliper (visible right behind the 10-spoke rim!)
    discMb.addCylinderZ({0.0f, 0.0f, 0.0f}, 0.52f, 0.18f, 18, kDiscCol);
    calMb.addBox({0.36f, 0.22f, 0.0f}, {0.24f, 0.34f, 0.30f}, kCaliperRed);

    WheelMeshSet w{};
    w.tire      = tireMb.upload(vk, 42.0f);
    w.rim       = rimMb.upload(vk, 24.0f);
    w.brakeDisc = discMb.upload(vk, 30.0f);
    w.caliper   = calMb.upload(vk, 20.0f);
    return w;
}

} // namespace

void CarMeshSet::destroy(VulkanContext& vk) {
    vk.destroyMesh(body);
    vk.destroyMesh(glass);
    vk.destroyMesh(interior);
    vk.destroyMesh(trim);
    vk.destroyMesh(chrome);
    vk.destroyMesh(lampsHead);
    vk.destroyMesh(lampsTail);
    vk.destroyMesh(livery);
    vk.destroyMesh(stripes);
    vk.destroyMesh(spoiler);
}

void WheelMeshSet::destroy(VulkanContext& vk) {
    vk.destroyMesh(tire);
    vk.destroyMesh(rim);
    vk.destroyMesh(brakeDisc);
    vk.destroyMesh(caliper);
}

void CarMeshLibrary::init(VulkanContext& vk) {
    wheel_ = buildSharedWheel(vk);
    for (int i = 0; i < static_cast<int>(VehicleKind::Count); ++i) {
        cars_[static_cast<std::size_t>(i)] = buildCadCar(vk, static_cast<VehicleKind>(i));
    }
}

void CarMeshLibrary::destroy(VulkanContext& vk) {
    wheel_.destroy(vk);
    for (auto& c : cars_) c.destroy(vk);
}

} // namespace oc
