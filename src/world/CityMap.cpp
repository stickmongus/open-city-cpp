#include "world/CityMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

float CityMap::sidewalkCoord(int sidewalkNodeIndex) const {
    const int clamped = std::max(0, std::min(kSidewalkNodeCount - 1, sidewalkNodeIndex));
    const int roadIdx = clamped / 2;
    const float side = (clamped % 2 == 0) ? -1.0f : 1.0f;
    return roadCoord(roadIdx) + side * kSidewalkCenterOffset;
}

int CityMap::nearestSidewalkNodeIndex(float coord) const {
    int bestIdx = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < kSidewalkNodeCount; ++i) {
        const float d = std::abs(coord - sidewalkCoord(i));
        if (d < bestDist) {
            bestDist = d;
            bestIdx = i;
        }
    }
    return bestIdx;
}

bool CityMap::isOnNorthBridge(const Vec2& p, float margin) const {
    return std::abs(p.x) <= (kRoadWidth * 0.5f + margin) &&
           p.z >= (kBridgeStartZ - 1.5f) && p.z <= (kBridgeEndZ + 1.5f);
}

bool CityMap::isOnNorthPeninsula(const Vec2& p) const {
    return p.z >= (kBridgeEndZ - 2.0f) && p.z <= kPeninsulaMaxZ &&
           std::abs(p.x) <= kPeninsulaHalfW;
}

float CityMap::bridgeDeckElevationAt(const Vec2& p, float* outSlopeZ) const {
    if (outSlopeZ) *outSlopeZ = 0.0f;
    if (!isOnNorthBridge(p, 1.5f)) return 0.0f;
    // Smooth architectural suspension-bridge arch cresting to +2.45m mid-span over the bay channel
    constexpr float kMaxArchH = 2.45f;
    constexpr float kRampSpan = 26.0f;
    if (p.z < kBridgeStartZ + kRampSpan) {
        const float u = clampf((p.z - kBridgeStartZ) / kRampSpan, 0.0f, 1.0f);
        if (outSlopeZ) {
            const float dudz = 1.0f / kRampSpan;
            *outSlopeZ = kMaxArchH * 6.0f * u * (1.0f - u) * dudz;
        }
        return kMaxArchH * (u * u * (3.0f - 2.0f * u));
    }
    if (p.z > kBridgeEndZ - kRampSpan) {
        const float u = clampf((kBridgeEndZ - p.z) / kRampSpan, 0.0f, 1.0f);
        if (outSlopeZ) {
            const float dudz = -1.0f / kRampSpan;
            *outSlopeZ = kMaxArchH * 6.0f * u * (1.0f - u) * dudz;
        }
        return kMaxArchH * (u * u * (3.0f - 2.0f * u));
    }
    return kMaxArchH;
}

bool CityMap::isOnRoad(const Vec2& p, float margin) const {
    if (isOnNorthBridge(p, margin)) return true;
    if (isOnNorthPeninsula(p)) {
        // Scenic paved coastal peninsula entry & lighthouse overlook loop road
        if (std::abs(p.x) <= (kRoadWidth * 0.5f + margin) && p.z <= 896.0f) return true;
        const Vec2 loopC{0.0f, 882.0f};
        const float r = (p - loopC).length();
        if (r >= (38.0f - margin) && r <= (52.0f + margin) && p.z >= 826.0f) return true;
        return false;
    }
    const float halfW = kRoadWidth * 0.5f + margin;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    const bool withinSpan = std::abs(p.x) <= (roadCoord(kRoadCount - 1) + halfW) &&
                            std::abs(p.z) <= (roadCoord(kRoadCount - 1) + halfW);
    if (!withinSpan) return false;
    return (std::abs(p.x - rx) <= halfW) || (std::abs(p.z - rz) <= halfW);
}

bool CityMap::isInIntersection(const Vec2& p) const {
    if (p.z > roadCoord(kRoadCount - 1) + kRoadWidth * 0.5f) return false;
    const float halfW = kRoadWidth * 0.5f;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    return (std::abs(p.x - rx) <= halfW) && (std::abs(p.z - rz) <= halfW);
}

DistrictType CityMap::districtAt(int bx, int bz) const {
    // 2x2 Grand Central Park Mega-Block NW of Central Crossroads (0,0)
    if (bx >= 6 && bx <= 7 && bz >= 6 && bz <= 7) {
        return DistrictType::CentralPark;
    }
    // Civic Center & Emergency Precincts (South-West of Central Crossroads)
    if ((bx == 6 || bx == 7) && bz == 9) {
        return DistrictType::CivicCenter;
    }
    if (bx == 9 && bz == 7) {
        return DistrictType::CivicCenter; // Sunset Bay General Medical Center
    }
    // Financial Downtown High-Rise Core (Central-East)
    if (bx >= 8 && bx <= 11 && bz >= 5 && bz <= 9) {
        return DistrictType::Downtown;
    }
    // Harbor & Container Port Industrial District (South-East Waterfront)
    if (bx >= 11 && bz >= 10) {
        return DistrictType::Industrial;
    }
    // Hillside Coastal Estates & Suburban Villas (North & North-East Ring)
    if (bz <= 2 || (bx >= 13 && bz <= 9)) {
        return DistrictType::SuburbanEstates;
    }
    // Sunset Beachfront Coastal Residential (West & South Shoreline)
    if (bx <= 2 || bz >= 13) {
        return DistrictType::CoastalResidential;
    }
    return DistrictType::Midtown;
}

DistrictType CityMap::districtAtWorld(const Vec2& p) const {
    if (p.z >= kBridgeStartZ) {
        return DistrictType::NorthPeninsula;
    }
    const float minCorner = roadCoord(0);
    int bx = static_cast<int>(std::floor((p.x - minCorner) / kBlockPitch));
    int bz = static_cast<int>(std::floor((p.z - minCorner) / kBlockPitch));
    bx = std::max(0, std::min(kBlockCount - 1, bx));
    bz = std::max(0, std::min(kBlockCount - 1, bz));
    return districtAt(bx, bz);
}

int CityMap::gridCellX(float x) {
    const int c = static_cast<int>(std::floor((x - kGridOriginX) / kGridCellSize));
    return std::max(0, std::min(kGridCols - 1, c));
}

int CityMap::gridCellZ(float z) {
    const int c = static_cast<int>(std::floor((z - kGridOriginZ) / kGridCellSize));
    return std::max(0, std::min(kGridRows - 1, c));
}

void CityMap::buildSpatialIndex() {
    const std::size_t cellCount = static_cast<std::size_t>(kGridCols * kGridRows);
    buildingGrid_.assign(cellCount, {});
    treeGrid_.assign(cellCount, {});
    propGrid_.assign(cellCount, {});

    for (int i = 0; i < static_cast<int>(buildings_.size()); ++i) {
        const auto& fp = buildings_[static_cast<std::size_t>(i)].footprint;
        const int x0 = gridCellX(fp.min.x);
        const int x1 = gridCellX(fp.max.x);
        const int z0 = gridCellZ(fp.min.z);
        const int z1 = gridCellZ(fp.max.z);
        for (int cz = z0; cz <= z1; ++cz) {
            for (int cx = x0; cx <= x1; ++cx) {
                buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))].push_back(i);
            }
        }
    }

    for (int i = 0; i < static_cast<int>(trees_.size()); ++i) {
        const auto& t = trees_[static_cast<std::size_t>(i)];
        const int cx = gridCellX(t.pos.x);
        const int cz = gridCellZ(t.pos.z);
        treeGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))].push_back(i);
    }

    for (int i = 0; i < static_cast<int>(streetProps_.size()); ++i) {
        const auto& sp = streetProps_[static_cast<std::size_t>(i)];
        const int cx = gridCellX(sp.homePos.x);
        const int cz = gridCellZ(sp.homePos.z);
        propGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))].push_back(i);
    }
}

void CityMap::queryBuildingsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const int x0 = gridCellX(center.x - radius);
    const int x1 = gridCellX(center.x + radius);
    const int z0 = gridCellZ(center.z - radius);
    const int z1 = gridCellZ(center.z + radius);
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            const auto& bucket = buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))];
            outIndices.insert(outIndices.end(), bucket.begin(), bucket.end());
        }
    }
}

void CityMap::queryTreesInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const int x0 = gridCellX(center.x - radius);
    const int x1 = gridCellX(center.x + radius);
    const int z0 = gridCellZ(center.z - radius);
    const int z1 = gridCellZ(center.z + radius);
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            const auto& bucket = treeGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))];
            outIndices.insert(outIndices.end(), bucket.begin(), bucket.end());
        }
    }
}

void CityMap::queryPropsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const int x0 = gridCellX(center.x - radius);
    const int x1 = gridCellX(center.x + radius);
    const int z0 = gridCellZ(center.z - radius);
    const int z1 = gridCellZ(center.z + radius);
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            const auto& bucket = propGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))];
            outIndices.insert(outIndices.end(), bucket.begin(), bucket.end());
        }
    }
}

void CityMap::generate(std::uint64_t seed) {
    buildings_.clear();
    canopies_.clear();
    parks_.clear();
    lakes_.clear();
    trees_.clear();
    streetProps_.clear();
    pois_.clear();

    Rng rng(seed);
    const float halfRoad = kRoadWidth * 0.5f;

    // Grounded realistic canvas awning colors (burgundy, navy, hunter green, slate, warm canvas, charcoal)
    static constexpr std::array<ColorRGBA, 6> kAwningPalette = {{
        {138,  44,  46, 255},
        { 38,  66, 102, 255},
        { 42,  82,  60, 255},
        {148, 108,  56, 255},
        { 82,  76,  72, 255},
        { 44,  48,  54, 255}
    }};

    for (int bz = 0; bz < kBlockCount; ++bz) {
        for (int bx = 0; bx < kBlockCount; ++bx) {
            const float x0 = roadCoord(bx) + halfRoad + kSidewalkWidth;
            const float x1 = roadCoord(bx + 1) - halfRoad - kSidewalkWidth;
            const float z0 = roadCoord(bz) + halfRoad + kSidewalkWidth;
            const float z1 = roadCoord(bz + 1) - halfRoad - kSidewalkWidth;

            const DistrictType district = districtAt(bx, bz);

            // 1) 2x2 Grand Central Park Mega-Block: blocks (6..7, 6..7) -> [-168..0, -168..0]
            if (district == DistrictType::CentralPark) {
                parks_.push_back(ParkPatch{{{x0, z0}, {x1, z1}}, {62, 92, 46, 255}});
                const float pcx = (x0 + x1) * 0.5f;
                const float pcz = (z0 + z1) * 0.5f;

                if (bx == 7 && bz == 7) {
                    // South-East Quadrant: Beaux-Arts Marble Fountain Plaza + Vintage Park Lamps + Mature Oaks
                    for (int t = 0; t < 18; ++t) {
                        const float tx = rng.range(x0 + 4.0f, x1 - 4.0f);
                        const float tz = rng.range(z0 + 4.0f, z1 - 4.0f);
                        if (Vec2{tx - pcx, tz - pcz}.length() < 10.5f) continue;
                        if (std::abs(tx - pcx) < 3.6f || std::abs(tz - pcz) < 3.6f) continue;
                        const bool cyp = rng.chance(0.22f);
                        trees_.push_back(TreeProp{
                            {tx, tz}, rng.range(3.0f, 4.4f), rng.range(2.4f, 3.6f),
                            false, false, cyp ? TreeSpecies::CypressConifer : TreeSpecies::OakDeciduous
                        });
                    }
                    streetProps_.push_back(StreetProp{{pcx - 8.2f, pcz}, StreetPropKind::Bench, kHalfPi, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx + 8.2f, pcz}, StreetPropKind::Bench, -kHalfPi, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx, pcz - 8.2f}, StreetPropKind::Bench, 0.0f, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx, pcz + 8.2f}, StreetPropKind::Bench, kPi, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx + 6.8f, pcz + 6.8f}, StreetPropKind::TrashBin, 0.0f, false, {}, 0.0f});
                } else if (bx == 6 && bz == 7) {
                    // South-West Quadrant: Reflective Grand Central Park Lake + Waterside Groves & Benches
                    lakes_.push_back(LakePatch{{{pcx - 18.0f, pcz - 14.5f}, {pcx + 18.0f, pcz + 14.5f}}, 0.04f});
                    for (int t = 0; t < 16; ++t) {
                        const float tx = rng.range(x0 + 3.5f, x1 - 3.5f);
                        const float tz = rng.range(z0 + 3.5f, z1 - 3.5f);
                        if (std::abs(tx - pcx) < 20.5f && std::abs(tz - pcz) < 17.0f) continue;
                        trees_.push_back(TreeProp{
                            {tx, tz}, rng.range(3.1f, 4.5f), rng.range(2.5f, 3.6f),
                            false, false, rng.chance(0.25f) ? TreeSpecies::CypressConifer : TreeSpecies::OakDeciduous
                        });
                    }
                    streetProps_.push_back(StreetProp{{pcx, pcz + 17.2f}, StreetPropKind::Bench, kPi, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx, pcz - 17.2f}, StreetPropKind::Bench, 0.0f, false, {}, 0.0f});
                } else {
                    // Northern Park Woodlands (blocks 6,6 & 7,6): Mature Oak & Cypress Groves + Quiet Walkways
                    for (int t = 0; t < 22; ++t) {
                        const float tx = rng.range(x0 + 4.0f, x1 - 4.0f);
                        const float tz = rng.range(z0 + 4.0f, z1 - 4.0f);
                        if (std::abs(tx - pcx) < 3.4f || std::abs(tz - pcz) < 3.4f) continue;
                        const bool cyp = rng.chance(0.30f);
                        trees_.push_back(TreeProp{
                            {tx, tz}, rng.range(3.2f, 4.8f), rng.range(2.4f, 3.8f),
                            false, false, cyp ? TreeSpecies::CypressConifer : TreeSpecies::OakDeciduous
                        });
                    }
                    streetProps_.push_back(StreetProp{{pcx + 4.2f, pcz - 4.2f}, StreetPropKind::Bench, -kHalfPi, false, {}, 0.0f});
                    streetProps_.push_back(StreetProp{{pcx - 4.2f, pcz + 4.2f}, StreetPropKind::Bench, kHalfPi, false, {}, 0.0f});
                }
                continue;
            }

            // 2) Block (8, 8): Midtown Commercial Hub (SE of 0,0) — Ammu-Tech Weapon Shop + Financial Tower
            if (bx == 8 && bz == 8) {
                buildings_.push_back(Building{
                    {{x0 + 3.0f, z0 + 12.0f}, {x0 + 26.0f, z0 + 30.0f}},
                    11.5f, 0.0f,
                    {68, 70, 76, 255}, {148, 76, 62, 255}, {132, 48, 44, 255},
                    BuildingStyle::ShopBuilding, true, true, false, false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::WeaponShop, {x0 + 14.5f, z0 + 5.5f}, 4.8f,
                    "AMMU-TECH WEAPON SHOP", "무기 & 중화기 상점", {224, 92, 66, 255}
                });
                buildings_.push_back(Building{
                    {{x0 + 31.0f, z0 + 8.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    48.0f, 14.0f,
                    {54, 72, 92, 255}, {116, 144, 170, 255}, {42, 70, 102, 255},
                    BuildingStyle::GlassTower, true, true, false, true
                });
                continue;
            }

            // 3) Block (7, 8): Pay 'n' Tune Custom Garage + Brick Apartment (SW of 0,0)
            if (bx == 7 && bz == 8) {
                buildings_.push_back(Building{
                    {{x0 + 28.0f, z0 + 14.0f}, {x1 - 4.0f, z0 + 34.0f}},
                    9.8f, 0.0f,
                    {56, 66, 78, 255}, {86, 134, 158, 255}, {46, 98, 128, 255},
                    BuildingStyle::GarageBuilding, false, true, false, false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::TuneGarage, {x1 - 16.0f, z0 + 6.5f}, 5.8f,
                    "PAY 'N' TUNE CUSTOMS", "차량 풀튜닝 & 수리 차고", {76, 184, 218, 255}
                });
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 6.0f}, {x0 + 23.0f, z1 - 5.0f}},
                    27.0f, 0.0f,
                    {136, 98, 82, 255}, {170, 158, 140, 255}, {136, 54, 48, 255},
                    BuildingStyle::BrickApartment, true, true, true, false
                });
                continue;
            }

            // 4) Block (9, 8): LUXURY AUTOS Supercar Glass Showroom (East of Central Crossroads)
            if (bx == 9 && bz == 8) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 16.0f}, {x0 + 36.0f, z0 + 38.0f}},
                    14.5f, 0.0f,
                    {38, 46, 58, 255}, {182, 156, 106, 255}, {154, 130, 82, 255},
                    BuildingStyle::DealershipBuilding, true, true, false, false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::CarDealership, {x0 + 20.0f, z0 + 7.5f}, 5.6f,
                    "LUXURY AUTOS SHOWROOM", "럭셔리 오토 슈퍼카 전시장", {224, 178, 76, 255}
                });
                buildings_.push_back(Building{
                    {{x0 + 40.0f, z0 + 6.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    72.0f, 18.0f,
                    {48, 66, 86, 255}, {112, 140, 166, 255}, {42, 52, 66, 255},
                    BuildingStyle::GlassTower, true, true, false, true
                });
                continue;
            }

            // 5) Block (8, 7): MODA BOUTIQUE Flagship (Warm Limestone & Brushed Champagne Bronze)
            if (bx == 8 && bz == 7) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z1 - 31.0f}, {x0 + 28.0f, z1 - 12.0f}},
                    13.5f, 0.0f,
                    {126, 120, 114, 255}, {168, 148, 118, 255}, {76, 68, 64, 255},
                    BuildingStyle::BoutiqueBuilding, true, true, false, false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::ClothingBoutique, {x0 + 16.0f, z1 - 5.5f}, 4.8f,
                    "MODA LUXURY BOUTIQUE", "모다 명품 의상 부티크", {218, 178, 128, 255}
                });
                buildings_.push_back(Building{
                    {{x0 + 32.0f, z0 + 5.0f}, {x1 - 3.0f, z1 - 5.0f}},
                    58.0f, 14.0f,
                    {58, 66, 78, 255}, {152, 142, 124, 255}, {48, 56, 68, 255},
                    BuildingStyle::GlassTower, true, true, false, false
                });
                continue;
            }

            // 6) Block (6, 8): GLOBE OIL Drive-Through Gas Station & Service Mart
            if (bx == 6 && bz == 8) {
                buildings_.push_back(Building{
                    {{x0 + 10.0f, z0 + 28.0f}, {x1 - 10.0f, z0 + 42.0f}},
                    7.2f, 0.0f,
                    {174, 172, 168, 255}, {178, 56, 48, 255}, {168, 52, 46, 255},
                    BuildingStyle::ShopBuilding, true, true, false, false
                });
                canopies_.push_back(GasStationCanopy{
                    {{x0 + 8.0f, z0 + 7.0f}, {x1 - 8.0f, z0 + 21.0f}}, 5.0f
                });
                const float py = z0 + 14.0f;
                streetProps_.push_back(StreetProp{{x0 + 16.0f, py - 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x0 + 16.0f, py + 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x1 - 16.0f, py - 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x1 - 16.0f, py + 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});

                pois_.push_back(PointOfInterest{
                    PoiKind::GasStation, {(x0 + x1) * 0.5f, py}, 6.2f,
                    "GLOBE OIL GAS & SERVICE", "글로브 오일 주유 & 급속정비", {224, 94, 66, 255}
                });
                continue;
            }

            // 7) Block (8, 6): PULSE Nightclub + Eclipse Sky Penthouse Landmark Tower
            if (bx == 8 && bz == 6) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 6.0f}, {x0 + 27.0f, z0 + 30.0f}},
                    15.0f, 0.0f,
                    {34, 34, 42, 255}, {126, 78, 168, 255}, {92, 54, 134, 255},
                    BuildingStyle::NightclubBuilding, false, true, false, false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::NightclubProperty, {x0 + 15.5f, z0 + 2.5f}, 4.8f,
                    "PULSE NEON NIGHTCLUB", "펄스 네온 나이트클럽", {188, 96, 236, 255}
                });
                buildings_.push_back(Building{
                    {{x0 + 31.0f, z0 + 6.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    98.0f, 24.0f,
                    {44, 62, 84, 255}, {118, 148, 174, 255}, {42, 74, 108, 255},
                    BuildingStyle::GlassTower, true, true, false, true
                });
                continue;
            }

            // 8) Civic Center Landmarks: SBPD Headquarters (7,9), Sunset Bay Medical Center (9,7), City Hall Plaza (6,9)
            if (district == DistrictType::CivicCenter) {
                if (bx == 7 && bz == 9) {
                    // SBPD Central Police Precinct HQ (granite architectural volume + forecourt bollards)
                    buildings_.push_back(Building{
                        {{x0 + 6.0f, z0 + 10.0f}, {x1 - 6.0f, z1 - 8.0f}},
                        18.5f, 4.5f,
                        {146, 148, 152, 255}, {52, 76, 114, 255}, {42, 58, 86, 255},
                        BuildingStyle::CivicLandmark, true, false, false, true
                    });
                    for (int k = 0; k < 5; ++k) {
                        const float bposX = lerpf(x0 + 12.0f, x1 - 12.0f, static_cast<float>(k) / 4.0f);
                        streetProps_.push_back(StreetProp{{bposX, z0 + 4.0f}, StreetPropKind::Bollard, 0.0f, false, {}, 0.0f});
                    }
                } else if (bx == 9 && bz == 7) {
                    // Sunset Bay General Medical Center (crisp stone & glass hospital + rooftop medical helipad)
                    buildings_.push_back(Building{
                        {{x0 + 6.0f, z0 + 10.0f}, {x1 - 6.0f, z1 - 6.0f}},
                        24.0f, 7.0f,
                        {178, 180, 182, 255}, {168, 52, 48, 255}, {58, 88, 122, 255},
                        BuildingStyle::CivicLandmark, true, false, false, true
                    });
                } else {
                    // Sunset Bay Civic Courthouse & Municipal Plaza
                    parks_.push_back(ParkPatch{{{x0 + 2.0f, z0 + 2.0f}, {x1 - 2.0f, z0 + 16.0f}}, {64, 94, 48, 255}});
                    buildings_.push_back(Building{
                        {{x0 + 8.0f, z0 + 18.0f}, {x1 - 8.0f, z1 - 6.0f}},
                        21.0f, 5.5f,
                        {168, 162, 150, 255}, {132, 126, 116, 255}, {68, 72, 78, 255},
                        BuildingStyle::CivicLandmark, true, false, false, false
                    });
                }
                continue;
            }

            // Procedural 2x2 sub-lots per regular block with authentic district architecture & green pocket parks
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
                if (district != DistrictType::Downtown && district != DistrictType::Industrial && rng.chance(0.15f)) {
                    parks_.push_back(ParkPatch{quadrants[q], {64, 94, 48, 255}});
                    const Vec2 c = quadrants[q].center();
                    const bool palm = (district == DistrictType::CoastalResidential && bx <= 2);
                    const TreeSpecies sp = palm ? TreeSpecies::PalmCoastal
                                                : (rng.chance(0.32f) ? TreeSpecies::CypressConifer : TreeSpecies::OakDeciduous);
                    trees_.push_back(TreeProp{
                        c, rng.range(2.9f, 4.0f), rng.range(2.1f, 2.9f), palm, false, sp
                    });
                    streetProps_.push_back(StreetProp{{c.x + 3.2f, c.z}, StreetPropKind::Bench, -kHalfPi, false, {}, 0.0f});
                    continue;
                }

                Building b{};
                const float insetX = rng.range(0.5f, 2.4f);
                const float insetZ = rng.range(0.5f, 2.4f);
                b.footprint = {
                    {quadrants[q].min.x + insetX, quadrants[q].min.z + insetZ},
                    {quadrants[q].max.x - insetX, quadrants[q].max.z - insetZ}
                };
                b.awningColor = kAwningPalette[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(kAwningPalette.size()) - 1))];

                switch (district) {
                    case DistrictType::Downtown: {
                        b.height = rng.range(48.0f, 96.0f);
                        b.upperTierHeight = rng.chance(0.66f) ? rng.range(10.0f, 24.0f) : 0.0f;
                        b.style = BuildingStyle::GlassTower;
                        const int pal = rng.rangeInt(0, 3);
                        if (pal == 0) {
                            b.primaryColor = {50, 66, 86, 255};
                            b.accentColor  = {110, 134, 154, 255};
                        } else if (pal == 1) {
                            b.primaryColor = {60, 64, 72, 255};
                            b.accentColor  = {142, 134, 122, 255};
                        } else if (pal == 2) {
                            b.primaryColor = {46, 70, 76, 255};
                            b.accentColor  = {106, 136, 140, 255};
                        } else {
                            b.primaryColor = {78, 76, 74, 255};
                            b.accentColor  = {146, 140, 132, 255};
                        }
                        b.litWindows = true;
                        b.hasStorefront = true;
                        b.hasHelipad = (b.upperTierHeight < 0.5f && b.height > 68.0f);
                        break;
                    }
                    case DistrictType::Midtown: {
                        b.height = rng.range(18.0f, 42.0f);
                        b.upperTierHeight = rng.chance(0.24f) ? rng.range(5.0f, 10.0f) : 0.0f;
                        b.style = rng.chance(0.48f) ? BuildingStyle::ModernOffice : BuildingStyle::BrickApartment;
                        if (b.style == BuildingStyle::ModernOffice) {
                            const int mp = rng.rangeInt(0, 1);
                            b.primaryColor = (mp == 0) ? ColorRGBA{112, 114, 118, 255} : ColorRGBA{128, 124, 118, 255};
                            b.accentColor  = {146, 148, 152, 255};
                            b.hasStorefront = true;
                        } else {
                            const int bp = rng.rangeInt(0, 2);
                            if (bp == 0) b.primaryColor = {138, 92, 74, 255};
                            else if (bp == 1) b.primaryColor = {124, 96, 82, 255};
                            else b.primaryColor = {152, 136, 118, 255};
                            b.accentColor = {166, 156, 140, 255};
                            b.hasStorefront = rng.chance(0.62f);
                            b.hasBalconies = true;
                        }
                        b.litWindows = true;
                        break;
                    }
                    case DistrictType::Industrial: {
                        if (rng.chance(0.48f)) {
                            b.height = rng.range(5.5f, 11.2f);
                            b.style = BuildingStyle::ShippingStack;
                            const int cidx = rng.rangeInt(0, 3);
                            if (cidx == 0) b.primaryColor = {144, 56, 48, 255};
                            else if (cidx == 1) b.primaryColor = {48, 82, 118, 255};
                            else if (cidx == 2) b.primaryColor = {152, 114, 48, 255};
                            else b.primaryColor = {54, 94, 72, 255};
                            b.accentColor = {48, 52, 58, 255};
                            b.litWindows = false;
                        } else {
                            b.height = rng.range(10.5f, 18.5f);
                            b.style = BuildingStyle::Warehouse;
                            b.primaryColor = {104, 100, 94, 255};
                            b.accentColor = {128, 112, 86, 255};
                            b.litWindows = false;
                        }
                        break;
                    }
                    case DistrictType::SuburbanEstates: {
                        // Low-rise Mediterranean hillside villas with lush garden yards & terracotta roofs!
                        parks_.push_back(ParkPatch{quadrants[q], {68, 96, 50, 255}});
                        const float vInset = rng.range(3.6f, 5.4f);
                        b.footprint = {
                            {quadrants[q].min.x + vInset, quadrants[q].min.z + vInset},
                            {quadrants[q].max.x - vInset, quadrants[q].max.z - vInset}
                        };
                        b.height = rng.range(6.8f, 10.6f);
                        b.style = BuildingStyle::VillaHouse;
                        const int vp = rng.rangeInt(0, 2);
                        if (vp == 0) b.primaryColor = {188, 178, 162, 255};      // Warm limestone stucco
                        else if (vp == 1) b.primaryColor = {174, 166, 152, 255}; // Sand plaster
                        else b.primaryColor = {166, 154, 142, 255};              // Warm greige
                        b.accentColor = {138, 76, 54, 255};                      // Terracotta clay tile roof
                        b.litWindows = true;
                        // Garden cypress tree in corner of villa lawn
                        trees_.push_back(TreeProp{
                            {quadrants[q].min.x + 1.8f, quadrants[q].min.z + 1.8f},
                            rng.range(2.6f, 3.6f), rng.range(1.4f, 1.9f), false, false, TreeSpecies::CypressConifer
                        });
                        break;
                    }
                    case DistrictType::CoastalResidential:
                    default: {
                        b.height = rng.range(11.0f, 24.0f);
                        b.style = BuildingStyle::BrickApartment;
                        const int pal = rng.rangeInt(0, 2);
                        if (pal == 0) b.primaryColor = {182, 170, 154, 255};
                        else if (pal == 1) b.primaryColor = {160, 170, 176, 255};
                        else b.primaryColor = {174, 146, 134, 255};
                        b.accentColor = {134, 140, 146, 255};
                        b.litWindows = true;
                        b.hasBalconies = true;
                        b.hasStorefront = rng.chance(0.42f);
                        break;
                    }
                }
                buildings_.push_back(b);
            }
        }
    }

    // Boulevard sidewalk planter trees, bus shelters, benches, trash bins across the 16x16 grid
    for (int r = 1; r <= kRoadCount - 2; ++r) {
        const float rc = roadCoord(r);
        for (int blk = 1; blk < kBlockCount - 1; ++blk) {
            const float segMid = (roadCoord(blk) + roadCoord(blk + 1)) * 0.5f;

            // Curbside boulevard trees in cast-iron sidewalk grates along avenues
            if ((r + blk) % 2 == 0) {
                const bool palm = (r <= 3 || blk <= 2 || r >= kRoadCount - 3);
                const TreeSpecies sp = palm ? TreeSpecies::PalmCoastal : TreeSpecies::OakDeciduous;
                trees_.push_back(TreeProp{{segMid - 14.0f, rc + halfRoad + 0.65f}, 3.2f, 2.05f, palm, true, sp});
                trees_.push_back(TreeProp{{segMid + 14.0f, rc - halfRoad - 0.65f}, 3.2f, 2.05f, palm, true, sp});
            }

            // Glass bus stop shelters & litter bins along main midtown/downtown transit corridors
            if ((r == 6 || r == 8 || r == 10) && (blk % 2 == 1) && blk >= 3 && blk <= 12) {
                const float shelterZ = rc + halfRoad + kSidewalkWidth - 0.35f;
                streetProps_.push_back(StreetProp{{segMid, shelterZ}, StreetPropKind::BusShelter, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{segMid + 4.2f, shelterZ}, StreetPropKind::TrashBin, 0.0f, false, {}, 0.0f});
            }
        }
    }

    // Coastal Sunset Beach palm colonnade + beach umbrellas along the full 1.36km Western shoreline
    for (int i = -16; i <= 16; ++i) {
        const float pos = static_cast<float>(i) * 40.0f;
        trees_.push_back(TreeProp{{-685.5f, pos}, 4.6f, 2.75f, true, false, TreeSpecies::PalmCoastal});
        trees_.push_back(TreeProp{{pos, -685.5f}, 4.4f, 2.65f, true, false, TreeSpecies::PalmCoastal});
        if (std::abs(pos) > 18.0f && (i % 2 == 0)) {
            streetProps_.push_back(StreetProp{{-708.0f, pos + 4.0f}, StreetPropKind::BeachUmbrella, static_cast<float>(i) * 0.5f, false, {}, 0.0f});
            streetProps_.push_back(StreetProp{{-720.0f, pos - 6.0f}, StreetPropKind::BeachUmbrella, static_cast<float>(i) * 0.9f, false, {}, 0.0f});
        }
    }

    // North Bay Headland & Lighthouse Peninsula scenic groves & cliffside villas (z in [825..940])
    parks_.push_back(ParkPatch{{{-118.0f, 822.0f}, {118.0f, 944.0f}}, {58, 86, 44, 255}});
    for (int i = 0; i < 36; ++i) {
        const float tx = rng.range(-108.0f, 108.0f);
        const float tz = rng.range(828.0f, 938.0f);
        // Keep approach road, scenic loop road & lighthouse apron clear
        if (std::abs(tx) < 13.5f && tz < 902.0f) continue;
        const float loopR = (Vec2{tx, tz} - Vec2{0.0f, 882.0f}).length();
        if (loopR > 33.0f && loopR < 57.0f) continue;
        if ((Vec2{tx, tz} - Vec2{0.0f, 926.0f}).length() < 15.0f) continue;
        trees_.push_back(TreeProp{
            {tx, tz}, rng.range(3.2f, 5.0f), rng.range(1.7f, 2.6f), false, false,
            rng.chance(0.68f) ? TreeSpecies::CypressConifer : TreeSpecies::OakDeciduous
        });
    }
    // Scenic coastal villas flanking the peninsula bay overlook
    buildings_.push_back(Building{
        {{-92.0f, 848.0f}, {-68.0f, 868.0f}}, 8.2f, 0.0f,
        {182, 174, 160, 255}, {136, 74, 52, 255}, {62, 68, 76, 255},
        BuildingStyle::VillaHouse, true, false, false, false
    });
    buildings_.push_back(Building{
        {{68.0f, 848.0f}, {92.0f, 868.0f}}, 8.6f, 0.0f,
        {176, 168, 156, 255}, {136, 74, 52, 255}, {62, 68, 76, 255},
        BuildingStyle::VillaHouse, true, false, false, false
    });

    // Commercial avenue billboards on major urban arterials (editorial style, spaced cleanly)
    const std::array<Vec2, 10> billboardSpots = {{
        {-96.0f, -96.0f}, {96.0f, -96.0f}, {-180.0f, 74.0f},
        {180.0f, -12.0f}, {96.0f, 180.0f}, {-12.0f, 180.0f},
        {348.0f, 96.0f}, {-348.0f, -96.0f}, {264.0f, -264.0f}, {-264.0f, 264.0f}
    }};
    for (std::size_t i = 0; i < billboardSpots.size(); ++i) {
        streetProps_.push_back(StreetProp{
            billboardSpots[i], StreetPropKind::Billboard,
            (i % 2 == 0) ? 0.45f : -0.45f, false, {}, 0.0f
        });
    }

    // Streetlights, fire hydrants, bollards, and occasional work-zone cones/crates across intersections
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
            if ((rx * 3 + rz) % 5 == 0) {
                streetProps_.push_back(StreetProp{{cx - corner - 1.5f, cz + corner + 1.0f}, StreetPropKind::Crate, 0.2f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{cx - corner - 0.5f, cz + corner - 0.8f}, StreetPropKind::TrafficCone, 0.0f, false, {}, 0.0f});
            }
        }
    }

    // 3 Mission Start Locations on clear downtown/midtown plazas
    pois_.push_back(PointOfInterest{
        PoiKind::MissionDelivery,
        {roadCoord(9) - halfRoad - 2.0f, roadCoord(8) - halfRoad - 2.0f},
        4.2f,
        "MISSION: VIP EXOTIC DELIVERY",
        "미션: 슈퍼카 탈취 & 긴급 배달",
        {228, 186, 62, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionRace,
        {roadCoord(8) - halfRoad - 2.0f, roadCoord(7) + halfRoad + 2.0f},
        4.2f,
        "MISSION: MIDNIGHT STREET SPRINT",
        "미션: 도심 스트리트 체크포인트 레이스",
        {96, 208, 118, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionGangRaid,
        {roadCoord(9) + halfRoad + 2.0f, roadCoord(9) - halfRoad - 2.0f},
        4.2f,
        "MISSION: HARBOR SYNDICATE RAID",
        "미션: 항구 라이벌 갱단 소탕",
        {224, 118, 56, 255}
    });

    // Post-pass spatial clearance filter: keep Sunset Pier, Ferris wheel, lifeguard towers,
    // Golden Bay Bridge approach, park lake, and POI pads 100% clear of obstructing trees & props
    auto overlapsClearanceZone = [&](const Vec2& p, bool checkPois) -> bool {
        // 1) Sunset Pier entrance & wooden ocean boardwalk (x < -682, |z| < 9.5)
        if (p.x < -682.0f && std::abs(p.z) < 9.5f) {
            return true;
        }
        // 2) Sunset Beach Ferris Wheel boarding platform (centered at {-718, -28})
        if (p.x >= -728.0f && p.x <= -708.0f && p.z >= -38.5f && p.z <= -17.5f) {
            return true;
        }
        // 3) Western Beach Lifeguard Towers
        if ((p - Vec2{-718.0f, 34.0f}).lengthSq() < 5.5f * 5.5f ||
            (p - Vec2{-718.0f, -240.0f}).lengthSq() < 5.5f * 5.5f ||
            (p - Vec2{-718.0f, 260.0f}).lengthSq() < 5.5f * 5.5f) {
            return true;
        }
        // 4) Golden Bay Suspension Bridge North Approach (x in [-14..+14], z in [670..826])
        if (std::abs(p.x) < 14.0f && p.z > 670.0f && p.z < 826.0f) {
            return true;
        }
        // 5) Grand Central Park Lake shoreline buffer
        for (const auto& lake : lakes_) {
            if (p.x >= lake.bounds.min.x - 2.2f && p.x <= lake.bounds.max.x + 2.2f &&
                p.z >= lake.bounds.min.z - 2.2f && p.z <= lake.bounds.max.z + 2.2f) {
                return true;
            }
        }
        // 6) POI zones
        if (checkPois) {
            for (const auto& poi : pois_) {
                const float r = poi.radius * 0.85f;
                if ((p - poi.pos).lengthSq() < r * r) {
                    return true;
                }
            }
        }
        return false;
    };

    trees_.erase(
        std::remove_if(trees_.begin(), trees_.end(),
                       [&](const TreeProp& t) { return overlapsClearanceZone(t.pos, true); }),
        trees_.end());
    streetProps_.erase(
        std::remove_if(streetProps_.begin(), streetProps_.end(),
                       [&](const StreetProp& sp) {
                           const bool checkPois = (sp.kind != StreetPropKind::FuelPump);
                           return overlapsClearanceZone(sp.pos, checkPois);
                       }),
        streetProps_.end());

    for (auto& sp : streetProps_) {
        sp.homePos = sp.pos;
    }

    buildSpatialIndex();
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

    // World perimeter: Main 16x16 Island + Western Sunset Beach & Pier + Golden Bay Bridge + North Bay Peninsula!
    if (pos.z > kHalfCitySpan - radius) {
        if (pos.z <= kBridgeEndZ) {
            // Crossing the Golden Bay Suspension Bridge corridor (keep strictly between steel bridge barriers!)
            const float bridgeHalfW = kRoadWidth * 0.5f - radius;
            if (std::abs(pos.x) > bridgeHalfW) {
                if (pos.z < kHalfCitySpan + 2.5f && std::abs(pos.x) > kRoadWidth * 0.5f + 1.0f) {
                    pos.z = kHalfCitySpan - radius;
                    accumNormal.z -= 1.0f;
                    hit = true;
                } else {
                    const float clampedX = clampf(pos.x, -bridgeHalfW, bridgeHalfW);
                    accumNormal.x += (pos.x > 0.0f ? -1.0f : 1.0f);
                    pos.x = clampedX;
                    hit = true;
                }
            }
        } else {
            // Driving/walking on the North Bay Lighthouse Peninsula!
            const float penX = kPeninsulaHalfW - radius;
            const float penZ = kPeninsulaMaxZ - radius;
            if (pos.x < -penX) { pos.x = -penX; accumNormal.x += 1.0f; hit = true; }
            if (pos.x >  penX) { pos.x =  penX; accumNormal.x -= 1.0f; hit = true; }
            if (pos.z >  penZ) { pos.z =  penZ; accumNormal.z -= 1.0f; hit = true; }
            if (pos.z < kBridgeEndZ + radius && std::abs(pos.x) > kRoadWidth * 0.5f) {
                pos.z = kBridgeEndZ + radius;
                accumNormal.z += 1.0f;
                hit = true;
            }
        }
    } else {
        const float minLimitX = (std::abs(pos.z) < 7.2f) ? (kWestPierOuter + 4.0f + radius) : (kWestBeachOuter + 10.0f + radius);
        const float limit = kHalfCitySpan - radius;
        if (pos.x < minLimitX) { pos.x = minLimitX; accumNormal.x += 1.0f; hit = true; }
        if (pos.x > limit)     { pos.x = limit;     accumNormal.x -= 1.0f; hit = true; }
        if (pos.z < -limit)    { pos.z = -limit;    accumNormal.z += 1.0f; hit = true; }
    }

    // Keep vehicles/pedestrians cleanly out of the reflective Park Lake water basin (except stone footbridge!)
    for (const auto& lake : lakes_) {
        if (std::abs(pos.x - lake.bounds.center().x) < 2.3f) continue; // Walkable central stone arch footbridge!
        Vec2 push{};
        if (resolveCircleAABB(pos, radius, lake.bounds, push)) {
            pos += push;
            accumNormal += push.normalized();
            hit = true;
        }
    }

    // Fast O(1) spatial-grid building collision resolution
    const int cx0 = gridCellX(pos.x - radius - 2.0f);
    const int cx1 = gridCellX(pos.x + radius + 2.0f);
    const int cz0 = gridCellZ(pos.z - radius - 2.0f);
    const int cz1 = gridCellZ(pos.z + radius + 2.0f);

    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            for (int bIdx : buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))]) {
                const auto& b = buildings_[static_cast<std::size_t>(bIdx)];
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

    if (current.center.z > kHalfCitySpan - obb.halfExtents.x) {
        if (current.center.z <= kBridgeEndZ) {
            const float bridgeHalfW = kRoadWidth * 0.5f - obb.halfExtents.z;
            if (std::abs(current.center.x) > bridgeHalfW) {
                if (current.center.z < kHalfCitySpan + 2.5f && std::abs(current.center.x) > kRoadWidth * 0.5f + 1.0f) {
                    const float targetZ = kHalfCitySpan - obb.halfExtents.x;
                    outPush.z += (targetZ - current.center.z);
                    current.center.z = targetZ;
                    anyHit = true;
                } else {
                    const float clampedX = clampf(current.center.x, -bridgeHalfW, bridgeHalfW);
                    outPush.x += (clampedX - current.center.x);
                    current.center.x = clampedX;
                    anyHit = true;
                }
            }
        } else {
            const float penX = kPeninsulaHalfW - obb.halfExtents.x;
            const float penZ = kPeninsulaMaxZ - obb.halfExtents.x;
            if (current.center.x < -penX) { outPush.x += (-penX - current.center.x); current.center.x = -penX; anyHit = true; }
            if (current.center.x >  penX) { outPush.x += ( penX - current.center.x); current.center.x =  penX; anyHit = true; }
            if (current.center.z >  penZ) { outPush.z += ( penZ - current.center.z); current.center.z =  penZ; anyHit = true; }
            if (current.center.z < kBridgeEndZ + obb.halfExtents.x && std::abs(current.center.x) > kRoadWidth * 0.5f) {
                const float tz = kBridgeEndZ + obb.halfExtents.x;
                outPush.z += (tz - current.center.z);
                current.center.z = tz;
                anyHit = true;
            }
        }
    } else {
        const float minLimitX = (std::abs(current.center.z) < 6.5f)
                                    ? (kWestPierOuter + 6.0f + obb.halfExtents.x)
                                    : (kWestBeachOuter + 12.0f + obb.halfExtents.x);
        const float limit = kHalfCitySpan - obb.halfExtents.x;
        if (current.center.x < minLimitX) { outPush.x += (minLimitX - current.center.x); current.center.x = minLimitX; anyHit = true; }
        if (current.center.x > limit)     { outPush.x += (limit - current.center.x);     current.center.x = limit;     anyHit = true; }
        if (current.center.z < -limit)    { outPush.z += (-limit - current.center.z);    current.center.z = -limit;    anyHit = true; }
    }

    for (const auto& lake : lakes_) {
        Vec2 mtv{};
        if (intersectOBB(current, aabbToOBB(lake.bounds), mtv)) {
            current.center += mtv;
            outPush += mtv;
            anyHit = true;
        }
    }

    const int cx0 = gridCellX(current.center.x - boundR);
    const int cx1 = gridCellX(current.center.x + boundR);
    const int cz0 = gridCellZ(current.center.z - boundR);
    const int cz1 = gridCellZ(current.center.z + boundR);

    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            for (int bIdx : buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))]) {
                const auto& b = buildings_[static_cast<std::size_t>(bIdx)];
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

    const int cx0 = gridCellX(minX);
    const int cx1 = gridCellX(maxX);
    const int cz0 = gridCellZ(minZ);
    const int cz1 = gridCellZ(maxZ);

    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            for (int bIdx : buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))]) {
                const auto& b = buildings_[static_cast<std::size_t>(bIdx)];
                if (b.footprint.max.x < minX || b.footprint.min.x > maxX ||
                    b.footprint.max.z < minZ || b.footprint.min.z > maxZ) {
                    continue;
                }
                const float t = intersectRayAABB(origin, dir, maxDist, b.footprint);
                if (t >= 0.0f && t < bestT) {
                    bestT = t;
                }
            }
        }
    }
    return (bestT <= maxDist) ? bestT : -1.0f;
}

float CityMap::raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const {
    float bestT = maxDist + 1.0f;
    const float endX = origin.x + dir.x * maxDist;
    const float endZ = origin.z + dir.z * maxDist;
    const float minX = std::min(origin.x, endX) - 1.0f;
    const float maxX = std::max(origin.x, endX) + 1.0f;
    const float minZ = std::min(origin.z, endZ) - 1.0f;
    const float maxZ = std::max(origin.z, endZ) + 1.0f;

    const int cx0 = gridCellX(minX);
    const int cx1 = gridCellX(maxX);
    const int cz0 = gridCellZ(minZ);
    const int cz1 = gridCellZ(maxZ);

    for (int cz = cz0; cz <= cz1; ++cz) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            for (int bIdx : buildingGrid_[static_cast<std::size_t>(gridFlatIndex(cx, cz))]) {
                const auto& b = buildings_[static_cast<std::size_t>(bIdx)];
                if (b.footprint.max.x < minX || b.footprint.min.x > maxX ||
                    b.footprint.max.z < minZ || b.footprint.min.z > maxZ) {
                    continue;
                }
                const AABB3D mainBox{
                    {b.footprint.min.x, 0.0f, b.footprint.min.z},
                    {b.footprint.max.x, b.height + 0.56f, b.footprint.max.z}
                };
                const float tMain = intersectRayAABB3D(origin, dir, maxDist, mainBox);
                if (tMain >= 0.0f && tMain < bestT) {
                    bestT = tMain;
                }

                if (b.upperTierHeight > 0.5f) {
                    const Vec2 c = b.footprint.center();
                    const Vec2 he = b.footprint.halfExtents() * 0.675f;
                    const AABB3D upperBox{
                        {c.x - he.x, b.height, c.z - he.z},
                        {c.x + he.x, b.height + b.upperTierHeight, c.z + he.z}
                    };
                    const float tUp = intersectRayAABB3D(origin, dir, maxDist, upperBox);
                    if (tUp >= 0.0f && tUp < bestT) {
                        bestT = tUp;
                    }
                }
            }
        }
    }
    return (bestT <= maxDist) ? bestT : -1.0f;
}

} // namespace oc
