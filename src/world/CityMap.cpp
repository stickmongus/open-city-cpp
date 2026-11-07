#include "world/CityMap.hpp"

#include <cmath>

namespace oc {

CityMap::CityMap() {
    generate();
}

float CityMap::roadCoord(int roadIndex) const {
    return static_cast<float>(roadIndex - (kRoadCount / 2)) * kBlockPitch;
}

int CityMap::nearestRoadIndex(float coord) const {
    const float raw = (coord / kBlockPitch) + static_cast<float>(kRoadCount / 2);
    const int idx = static_cast<int>(std::round(raw));
    return std::max(0, std::min(kRoadCount - 1, idx));
}

bool CityMap::isOnRoad(const Vec2& p, float margin) const {
    const float halfW = kRoadWidth * 0.5f + margin;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    const bool withinSpan = std::abs(p.x) <= (roadCoord(kRoadCount - 1) + halfW) &&
                            std::abs(p.z) <= (roadCoord(kRoadCount - 1) + halfW);
    if (!withinSpan) return false;
    return (std::abs(p.x - rx) <= halfW) || (std::abs(p.z - rz) <= halfW);
}

bool CityMap::isInIntersection(const Vec2& p) const {
    const float halfW = kRoadWidth * 0.5f;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    return (std::abs(p.x - rx) <= halfW) && (std::abs(p.z - rz) <= halfW);
}

DistrictType CityMap::districtAt(int bx, int bz) const {
    if (bx == 3 && bz == 3) {
        return DistrictType::CentralPark;
    }
    if (bx >= 4 && bx <= 5 && bz >= 2 && bz <= 4) {
        return DistrictType::Downtown;
    }
    if (bx >= 5 && bz >= 5) {
        return DistrictType::Industrial;
    }
    if (bx <= 1 || bz <= 1) {
        return DistrictType::CoastalResidential;
    }
    return DistrictType::Midtown;
}

DistrictType CityMap::districtAtWorld(const Vec2& p) const {
    const float minCorner = roadCoord(0);
    int bx = static_cast<int>(std::floor((p.x - minCorner) / kBlockPitch));
    int bz = static_cast<int>(std::floor((p.z - minCorner) / kBlockPitch));
    bx = std::max(0, std::min(kBlockCount - 1, bx));
    bz = std::max(0, std::min(kBlockCount - 1, bz));
    return districtAt(bx, bz);
}

void CityMap::generate(std::uint64_t seed) {
    buildings_.clear();
    parks_.clear();
    trees_.clear();
    streetProps_.clear();
    pois_.clear();

    Rng rng(seed);
    const float halfRoad = kRoadWidth * 0.5f;

    for (int bz = 0; bz < kBlockCount; ++bz) {
        for (int bx = 0; bx < kBlockCount; ++bx) {
            const float x0 = roadCoord(bx) + halfRoad + kSidewalkWidth;
            const float x1 = roadCoord(bx + 1) - halfRoad - kSidewalkWidth;
            const float z0 = roadCoord(bz) + halfRoad + kSidewalkWidth;
            const float z1 = roadCoord(bz + 1) - halfRoad - kSidewalkWidth;

            const DistrictType district = districtAt(bx, bz);

            // Special block (3, 3): Central Park Plaza right NW of (0,0)
            if (district == DistrictType::CentralPark) {
                parks_.push_back(ParkPatch{{{x0, z0}, {x1, z1}}, {56, 134, 68, 255}});
                for (int t = 0; t < 16; ++t) {
                    const float tx = rng.range(x0 + 4.0f, x1 - 4.0f);
                    const float tz = rng.range(z0 + 4.0f, z1 - 4.0f);
                    if (Vec2{tx - (x0 + x1) * 0.5f, tz - (z0 + z1) * 0.5f}.length() < 8.0f) {
                        continue;
                    }
                    trees_.push_back(TreeProp{{tx, tz}, rng.range(2.6f, 3.8f), rng.range(2.2f, 3.3f), false});
                }
                continue;
            }

            // Special block (4, 4): Midtown Commercial Hub with Ammu-Tech Weapon Shop near (0,0)
            if (bx == 4 && bz == 4) {
                buildings_.push_back(Building{
                    {{x0 + 3.0f, z0 + 12.0f}, {x0 + 26.0f, z0 + 30.0f}},
                    11.5f,
                    0.0f,
                    {68, 72, 82, 255},
                    {235, 78, 52, 255},
                    BuildingStyle::ShopBuilding,
                    true
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::WeaponShop,
                    {x0 + 14.5f, z0 + 5.5f},
                    4.8f,
                    "AMMU-TECH WEAPON SHOP",
                    "무기 & 방탄복 상점",
                    {255, 92, 64, 255}
                });

                // Second tower in SE half of block (4,4)
                buildings_.push_back(Building{
                    {{x0 + 31.0f, z0 + 8.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    42.0f,
                    12.0f,
                    {62, 88, 118, 255},
                    {120, 195, 255, 255},
                    BuildingStyle::GlassTower,
                    true
                });
                continue;
            }

            // Special block (3, 4): Pay 'n' Tune Garage SW of (0,0) with wide drive-in forecourt
            if (bx == 3 && bz == 4) {
                buildings_.push_back(Building{
                    {{x0 + 28.0f, z0 + 14.0f}, {x1 - 4.0f, z0 + 34.0f}},
                    9.5f,
                    0.0f,
                    {52, 76, 98, 255},
                    {48, 215, 255, 255},
                    BuildingStyle::GarageBuilding,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::TuneGarage,
                    {x1 - 16.0f, z0 + 6.5f},
                    5.8f,
                    "PAY 'N' TUNE GARAGE",
                    "차량 수리 & 튜닝 차고",
                    {56, 212, 255, 255}
                });

                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 6.0f}, {x0 + 23.0f, z1 - 5.0f}},
                    26.0f,
                    0.0f,
                    {142, 112, 94, 255},
                    {225, 196, 152, 255},
                    BuildingStyle::BrickApartment,
                    true
                });
                continue;
            }

            // Procedural 2x2 sub-lots per regular block with alleyway gap
            const float midX = (x0 + x1) * 0.5f;
            const float midZ = (z0 + z1) * 0.5f;
            const float alley = 3.2f;

            const AABB2D quadrants[4] = {
                {{x0 + 2.0f, z0 + 2.0f}, {midX - alley, midZ - alley}},
                {{midX + alley, z0 + 2.0f}, {x1 - 2.0f, midZ - alley}},
                {{x0 + 2.0f, midZ + alley}, {midX - alley, z1 - 2.0f}},
                {{midX + alley, midZ + alley}, {x1 - 2.0f, z1 - 2.0f}}
            };

            for (int q = 0; q < 4; ++q) {
                // Leave occasional open courtyard / parking plaza with trees
                if (district != DistrictType::Downtown && rng.chance(0.16f)) {
                    parks_.push_back(ParkPatch{quadrants[q], {64, 126, 72, 255}});
                    const Vec2 c = quadrants[q].center();
                    trees_.push_back(TreeProp{
                        c,
                        rng.range(2.8f, 3.8f),
                        rng.range(2.0f, 2.8f),
                        district == DistrictType::CoastalResidential
                    });
                    continue;
                }

                Building b{};
                const float insetX = rng.range(0.5f, 2.5f);
                const float insetZ = rng.range(0.5f, 2.5f);
                b.footprint = {
                    {quadrants[q].min.x + insetX, quadrants[q].min.z + insetZ},
                    {quadrants[q].max.x - insetX, quadrants[q].max.z - insetZ}
                };

                switch (district) {
                    case DistrictType::Downtown: {
                        b.height = rng.range(46.0f, 92.0f);
                        b.upperTierHeight = rng.chance(0.65f) ? rng.range(10.0f, 22.0f) : 0.0f;
                        b.style = BuildingStyle::GlassTower;
                        const int pal = rng.rangeInt(0, 2);
                        if (pal == 0) {
                            b.primaryColor = {54, 78, 108, 255};
                            b.accentColor = {115, 205, 255, 255};
                        } else if (pal == 1) {
                            b.primaryColor = {66, 72, 86, 255};
                            b.accentColor = {255, 196, 105, 255};
                        } else {
                            b.primaryColor = {48, 88, 96, 255};
                            b.accentColor = {98, 245, 218, 255};
                        }
                        b.litWindows = true;
                        break;
                    }
                    case DistrictType::Midtown: {
                        b.height = rng.range(20.0f, 44.0f);
                        b.upperTierHeight = rng.chance(0.25f) ? rng.range(5.0f, 10.0f) : 0.0f;
                        b.style = rng.chance(0.5f) ? BuildingStyle::ModernOffice : BuildingStyle::BrickApartment;
                        if (b.style == BuildingStyle::ModernOffice) {
                            b.primaryColor = {118, 126, 138, 255};
                            b.accentColor = {185, 212, 235, 255};
                        } else {
                            b.primaryColor = {152, 104, 86, 255};
                            b.accentColor = {232, 208, 175, 255};
                        }
                        b.litWindows = true;
                        break;
                    }
                    case DistrictType::Industrial: {
                        if (rng.chance(0.45f)) {
                            b.height = rng.range(5.5f, 11.0f);
                            b.style = BuildingStyle::ShippingStack;
                            const int cidx = rng.rangeInt(0, 2);
                            if (cidx == 0) b.primaryColor = {178, 62, 52, 255};
                            else if (cidx == 1) b.primaryColor = {48, 106, 168, 255};
                            else b.primaryColor = {196, 146, 44, 255};
                            b.accentColor = {40, 44, 48, 255};
                            b.litWindows = false;
                        } else {
                            b.height = rng.range(11.0f, 19.0f);
                            b.style = BuildingStyle::Warehouse;
                            b.primaryColor = {112, 108, 102, 255};
                            b.accentColor = {188, 142, 72, 255};
                            b.litWindows = false;
                        }
                        break;
                    }
                    case DistrictType::CoastalResidential:
                    default: {
                        b.height = rng.range(11.0f, 24.0f);
                        b.style = BuildingStyle::BrickApartment;
                        const int pal = rng.rangeInt(0, 2);
                        if (pal == 0) b.primaryColor = {218, 204, 184, 255};
                        else if (pal == 1) b.primaryColor = {192, 216, 224, 255};
                        else b.primaryColor = {228, 188, 176, 255};
                        b.accentColor = {82, 168, 196, 255};
                        b.litWindows = true;
                        break;
                    }
                }
                buildings_.push_back(b);
            }
        }
    }

    // Add coastal palm trees along outer North & West boulevards
    for (int i = -4; i <= 4; ++i) {
        const float pos = static_cast<float>(i) * 72.0f;
        trees_.push_back(TreeProp{{-352.0f, pos}, 4.2f, 2.6f, true});
        trees_.push_back(TreeProp{{pos, -352.0f}, 4.2f, 2.6f, true});
    }

    // Streetlights, hydrants, and destructible crates/cones along blocks
    for (int rz = 0; rz < kRoadCount; ++rz) {
        for (int rx = 0; rx < kRoadCount; ++rx) {
            const float cx = roadCoord(rx);
            const float cz = roadCoord(rz);
            const float corner = halfRoad + 1.4f;

            streetProps_.push_back(StreetProp{{cx + corner, cz + corner}, StreetPropKind::StreetLight, kPi * 1.25f, false, {}, 0.0f});
            streetProps_.push_back(StreetProp{{cx - corner, cz - corner}, StreetPropKind::StreetLight, kPi * 0.25f, false, {}, 0.0f});

            if ((rx + rz) % 2 == 0) {
                streetProps_.push_back(StreetProp{{cx + corner + 1.2f, cz - corner}, StreetPropKind::Hydrant, 0.0f, false, {}, 0.0f});
            }
            if ((rx * 3 + rz) % 4 == 0) {
                streetProps_.push_back(StreetProp{{cx - corner - 1.5f, cz + corner + 1.0f}, StreetPropKind::Crate, 0.2f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{cx - corner - 0.5f, cz + corner - 0.8f}, StreetPropKind::TrafficCone, 0.0f, false, {}, 0.0f});
            }
        }
    }

    // 3 GTA Mission Start Coronas on clear sidewalks/plazas near central avenues
    pois_.push_back(PointOfInterest{
        PoiKind::MissionDelivery,
        {roadCoord(5) - halfRoad - 2.0f, roadCoord(4) - halfRoad - 2.0f}, // (73, -11) Downtown edge
        4.2f,
        "MISSION: VIP EXOTIC DELIVERY",
        "미션: 슈퍼카 탈취 & 긴급 배달",
        {255, 214, 52, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionRace,
        {roadCoord(4) - halfRoad - 2.0f, roadCoord(3) + halfRoad + 2.0f}, // (-11, -73) Park North avenue
        4.2f,
        "MISSION: MIDNIGHT STREET SPRINT",
        "미션: 도심 스트리트 체크포인트 레이스",
        {112, 255, 118, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionGangRaid,
        {roadCoord(5) + halfRoad + 2.0f, roadCoord(5) - halfRoad - 2.0f}, // (95, 73) Industrial gateway
        4.2f,
        "MISSION: HARBOR SYNDICATE RAID",
        "미션: 항구 라이벌 갱단 소탕",
        {255, 128, 48, 255}
    });
}

const PointOfInterest* CityMap::findPoi(PoiKind kind) const {
    for (const auto& poi : pois_) {
        if (poi.kind == kind) return &poi;
    }
    return nullptr;
}

bool CityMap::resolveCircleWorld(Vec2& pos, float radius, Vec2* outHitNormal) const {
    bool hit = false;
    Vec2 accumNormal{0.0f, 0.0f};

    // World coastal wall boundary
    const float limit = kHalfCitySpan - radius;
    if (pos.x < -limit) { pos.x = -limit; accumNormal.x += 1.0f; hit = true; }
    if (pos.x > limit)  { pos.x = limit;  accumNormal.x -= 1.0f; hit = true; }
    if (pos.z < -limit) { pos.z = -limit; accumNormal.z += 1.0f; hit = true; }
    if (pos.z > limit)  { pos.z = limit;  accumNormal.z -= 1.0f; hit = true; }

    for (const auto& b : buildings_) {
        // Broadphase cull
        if (pos.x + radius < b.footprint.min.x || pos.x - radius > b.footprint.max.x ||
            pos.z + radius < b.footprint.min.z || pos.z - radius > b.footprint.max.z) {
            continue;
        }
        Vec2 push{};
        if (resolveCircleAABB(pos, radius, b.footprint, push)) {
            pos += push;
            accumNormal += push.normalized();
            hit = true;
        }
    }

    if (hit && outHitNormal) {
        *outHitNormal = accumNormal.normalized();
    }
    return hit;
}

bool CityMap::resolveOBBBuildings(const OBB2D& obb, Vec2& outPush) const {
    outPush = {0.0f, 0.0f};
    bool anyHit = false;
    const float boundR = obb.halfExtents.length() + 1.0f;
    OBB2D current = obb;

    // World boundary clamp
    const float limit = kHalfCitySpan - obb.halfExtents.x;
    if (current.center.x < -limit) { outPush.x += (-limit - current.center.x); current.center.x = -limit; anyHit = true; }
    if (current.center.x > limit)  { outPush.x += (limit - current.center.x);  current.center.x = limit;  anyHit = true; }
    if (current.center.z < -limit) { outPush.z += (-limit - current.center.z); current.center.z = -limit; anyHit = true; }
    if (current.center.z > limit)  { outPush.z += (limit - current.center.z);  current.center.z = limit;  anyHit = true; }

    for (const auto& b : buildings_) {
        if (current.center.x + boundR < b.footprint.min.x || current.center.x - boundR > b.footprint.max.x ||
            current.center.z + boundR < b.footprint.min.z || current.center.z - boundR > b.footprint.max.z) {
            continue;
        }
        Vec2 mtv{};
        if (intersectOBB(current, aabbToOBB(b.footprint), mtv)) {
            current.center += mtv;
            outPush += mtv;
            anyHit = true;
        }
    }
    return anyHit;
}

float CityMap::raycastBuildings(const Vec2& origin, const Vec2& dir, float maxDist) const {
    float bestT = maxDist + 1.0f;
    const float minX = std::min(origin.x, origin.x + dir.x * maxDist) - 1.0f;
    const float maxX = std::max(origin.x, origin.x + dir.x * maxDist) + 1.0f;
    const float minZ = std::min(origin.z, origin.z + dir.z * maxDist) - 1.0f;
    const float maxZ = std::max(origin.z, origin.z + dir.z * maxDist) + 1.0f;

    for (const auto& b : buildings_) {
        if (b.footprint.max.x < minX || b.footprint.min.x > maxX ||
            b.footprint.max.z < minZ || b.footprint.min.z > maxZ) {
            continue;
        }
        const float t = intersectRayAABB(origin, dir, maxDist, b.footprint);
        if (t >= 0.0f && t < bestT) {
            bestT = t;
        }
    }
    return (bestT <= maxDist) ? bestT : -1.0f;
}

} // namespace oc
