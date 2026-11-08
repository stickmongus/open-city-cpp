#include "world/CityMap.hpp"

#include <array>
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
    canopies_.clear();
    parks_.clear();
    trees_.clear();
    streetProps_.clear();
    pois_.clear();

    Rng rng(seed);
    const float halfRoad = kRoadWidth * 0.5f;

    static constexpr std::array<ColorRGBA, 6> kAwningPalette = {{
        {196, 48, 52, 255},
        {34, 106, 178, 255},
        {36, 138, 92, 255},
        {205, 132, 42, 255},
        {86, 62, 148, 255},
        {42, 48, 58, 255}
    }};

    for (int bz = 0; bz < kBlockCount; ++bz) {
        for (int bx = 0; bx < kBlockCount; ++bx) {
            const float x0 = roadCoord(bx) + halfRoad + kSidewalkWidth;
            const float x1 = roadCoord(bx + 1) - halfRoad - kSidewalkWidth;
            const float z0 = roadCoord(bz) + halfRoad + kSidewalkWidth;
            const float z1 = roadCoord(bz + 1) - halfRoad - kSidewalkWidth;

            const DistrictType district = districtAt(bx, bz);

            // 1) Block (3, 3): Central Park Plaza right NW of (0,0)
            if (district == DistrictType::CentralPark) {
                parks_.push_back(ParkPatch{{{x0, z0}, {x1, z1}}, {56, 134, 68, 255}});
                const float pcx = (x0 + x1) * 0.5f;
                const float pcz = (z0 + z1) * 0.5f;
                for (int t = 0; t < 18; ++t) {
                    const float tx = rng.range(x0 + 4.0f, x1 - 4.0f);
                    const float tz = rng.range(z0 + 4.0f, z1 - 4.0f);
                    if (Vec2{tx - pcx, tz - pcz}.length() < 9.5f) {
                        continue;
                    }
                    trees_.push_back(TreeProp{{tx, tz}, rng.range(2.8f, 4.1f), rng.range(2.3f, 3.4f), false, false});
                }
                // Park plaza benches & bins around central fountain
                streetProps_.push_back(StreetProp{{pcx - 7.8f, pcz}, StreetPropKind::Bench, kHalfPi, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{pcx + 7.8f, pcz}, StreetPropKind::Bench, -kHalfPi, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{pcx, pcz - 7.8f}, StreetPropKind::Bench, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{pcx, pcz + 7.8f}, StreetPropKind::Bench, kPi, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{pcx + 6.8f, pcz + 6.8f}, StreetPropKind::TrashBin, 0.0f, false, {}, 0.0f});
                continue;
            }

            // 2) Block (4, 4): Midtown Commercial Hub — Ammu-Tech Weapon Shop + Financial Tower
            if (bx == 4 && bz == 4) {
                buildings_.push_back(Building{
                    {{x0 + 3.0f, z0 + 12.0f}, {x0 + 26.0f, z0 + 30.0f}},
                    11.5f,
                    0.0f,
                    {68, 72, 82, 255},
                    {235, 78, 52, 255},
                    {215, 58, 46, 255},
                    BuildingStyle::ShopBuilding,
                    true,
                    true,
                    false,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::WeaponShop,
                    {x0 + 14.5f, z0 + 5.5f},
                    4.8f,
                    "AMMU-TECH WEAPON SHOP",
                    "무기 & 중화기 상점",
                    {255, 92, 64, 255}
                });

                buildings_.push_back(Building{
                    {{x0 + 31.0f, z0 + 8.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    46.0f,
                    14.0f,
                    {58, 86, 118, 255},
                    {120, 195, 255, 255},
                    {38, 108, 178, 255},
                    BuildingStyle::GlassTower,
                    true,
                    true,
                    false,
                    true
                });
                continue;
            }

            // 3) Block (3, 4): Pay 'n' Tune Custom Garage + Balcony Apartment
            if (bx == 3 && bz == 4) {
                buildings_.push_back(Building{
                    {{x0 + 28.0f, z0 + 14.0f}, {x1 - 4.0f, z0 + 34.0f}},
                    9.8f,
                    0.0f,
                    {52, 76, 98, 255},
                    {48, 215, 255, 255},
                    {38, 168, 224, 255},
                    BuildingStyle::GarageBuilding,
                    false,
                    true,
                    false,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::TuneGarage,
                    {x1 - 16.0f, z0 + 6.5f},
                    5.8f,
                    "PAY 'N' TUNE CUSTOMS",
                    "차량 풀튜닝 & 수리 차고",
                    {56, 212, 255, 255}
                });

                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 6.0f}, {x0 + 23.0f, z1 - 5.0f}},
                    27.0f,
                    0.0f,
                    {148, 110, 92, 255},
                    {225, 196, 152, 255},
                    {188, 58, 52, 255},
                    BuildingStyle::BrickApartment,
                    true,
                    true,
                    true,
                    false
                });
                continue;
            }

            // 4) Block (5, 4): LUXURY AUTOS Supercar Glass Showroom (East of Downtown Crossroads)
            if (bx == 5 && bz == 4) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 16.0f}, {x0 + 36.0f, z0 + 38.0f}},
                    14.5f,
                    0.0f,
                    {38, 52, 72, 255},
                    {255, 198, 68, 255},
                    {242, 182, 54, 255},
                    BuildingStyle::DealershipBuilding,
                    true,
                    true,
                    false,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::CarDealership,
                    {x0 + 20.0f, z0 + 7.5f},
                    5.6f,
                    "LUXURY AUTOS SHOWROOM",
                    "럭셔리 오토 슈퍼카 전시장",
                    {255, 198, 58, 255}
                });

                buildings_.push_back(Building{
                    {{x0 + 40.0f, z0 + 6.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    68.0f,
                    18.0f,
                    {52, 78, 108, 255},
                    {105, 212, 255, 255},
                    {42, 52, 66, 255},
                    BuildingStyle::GlassTower,
                    true,
                    true,
                    false,
                    true
                });
                continue;
            }

            // 5) Block (4, 3): MODA BOUTIQUE Fashion Flagship (NE of Central Crossroads)
            if (bx == 4 && bz == 3) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z1 - 31.0f}, {x0 + 28.0f, z1 - 12.0f}},
                    13.5f,
                    0.0f,
                    {82, 68, 96, 255},
                    {248, 128, 224, 255},
                    {212, 76, 168, 255},
                    BuildingStyle::BoutiqueBuilding,
                    true,
                    true,
                    false,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::ClothingBoutique,
                    {x0 + 16.0f, z1 - 5.5f},
                    4.8f,
                    "MODA LUXURY BOUTIQUE",
                    "모다 명품 의상 부티크",
                    {246, 122, 228, 255}
                });

                buildings_.push_back(Building{
                    {{x0 + 32.0f, z0 + 5.0f}, {x1 - 3.0f, z1 - 5.0f}},
                    54.0f,
                    14.0f,
                    {64, 74, 92, 255},
                    {255, 202, 112, 255},
                    {48, 56, 72, 255},
                    BuildingStyle::GlassTower,
                    true,
                    true,
                    false,
                    false
                });
                continue;
            }

            // 6) Block (2, 4): GLOBE OIL Drive-Through Gas Station & Convenience Mart
            if (bx == 2 && bz == 4) {
                // Rear convenience store building
                buildings_.push_back(Building{
                    {{x0 + 10.0f, z0 + 28.0f}, {x1 - 10.0f, z0 + 42.0f}},
                    7.2f,
                    0.0f,
                    {218, 220, 226, 255},
                    {228, 52, 48, 255},
                    {224, 48, 44, 255},
                    BuildingStyle::ShopBuilding,
                    true,
                    true,
                    false,
                    false
                });
                // Overhead illuminated gas canopy (vehicles drive underneath cleanly!)
                canopies_.push_back(GasStationCanopy{
                    {{x0 + 8.0f, z0 + 7.0f}, {x1 - 8.0f, z0 + 21.0f}},
                    5.0f
                });
                // 4 Fuel Pump dispensers under the canopy
                const float py = z0 + 14.0f;
                streetProps_.push_back(StreetProp{{x0 + 16.0f, py - 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x0 + 16.0f, py + 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x1 - 16.0f, py - 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{x1 - 16.0f, py + 2.6f}, StreetPropKind::FuelPump, 0.0f, false, {}, 0.0f});

                pois_.push_back(PointOfInterest{
                    PoiKind::GasStation,
                    {(x0 + x1) * 0.5f, py},
                    6.2f,
                    "GLOBE OIL GAS & SERVICE",
                    "글로브 오일 주유 & 급속정비",
                    {255, 84, 62, 255}
                });
                continue;
            }

            // 7) Block (4, 2): PULSE Neon Nightclub + Eclipse Sky Penthouse Landmark Tower
            if (bx == 4 && bz == 2) {
                buildings_.push_back(Building{
                    {{x0 + 4.0f, z0 + 6.0f}, {x0 + 27.0f, z0 + 30.0f}},
                    15.0f,
                    0.0f,
                    {28, 26, 42, 255},
                    {192, 68, 255, 255},
                    {162, 48, 238, 255},
                    BuildingStyle::NightclubBuilding,
                    false,
                    true,
                    false,
                    false
                });
                pois_.push_back(PointOfInterest{
                    PoiKind::NightclubProperty,
                    {x0 + 15.5f, z0 + 2.5f},
                    4.8f,
                    "PULSE NEON NIGHTCLUB",
                    "펄스 네온 나이트클럽",
                    {198, 82, 255, 255}
                });

                // Tallest landmark residential/penthouse skyscraper with Rooftop Helipad
                buildings_.push_back(Building{
                    {{x0 + 31.0f, z0 + 6.0f}, {x1 - 3.0f, z1 - 4.0f}},
                    94.0f,
                    22.0f,
                    {46, 74, 106, 255},
                    {118, 224, 255, 255},
                    {38, 98, 168, 255},
                    BuildingStyle::GlassTower,
                    true,
                    true,
                    false,
                    true
                });
                continue;
            }

            // Procedural 2x2 sub-lots per regular block with realistic alleys & plazas
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
                if (district != DistrictType::Downtown && rng.chance(0.16f)) {
                    parks_.push_back(ParkPatch{quadrants[q], {64, 126, 72, 255}});
                    const Vec2 c = quadrants[q].center();
                    trees_.push_back(TreeProp{
                        c,
                        rng.range(2.8f, 3.8f),
                        rng.range(2.0f, 2.8f),
                        district == DistrictType::CoastalResidential,
                        false
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
                        b.height = rng.range(48.0f, 92.0f);
                        b.upperTierHeight = rng.chance(0.68f) ? rng.range(10.0f, 22.0f) : 0.0f;
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
                        b.hasStorefront = true;
                        b.hasHelipad = (b.upperTierHeight < 0.5f && b.height > 64.0f);
                        break;
                    }
                    case DistrictType::Midtown: {
                        b.height = rng.range(20.0f, 44.0f);
                        b.upperTierHeight = rng.chance(0.25f) ? rng.range(5.0f, 10.0f) : 0.0f;
                        b.style = rng.chance(0.5f) ? BuildingStyle::ModernOffice : BuildingStyle::BrickApartment;
                        if (b.style == BuildingStyle::ModernOffice) {
                            b.primaryColor = {118, 126, 138, 255};
                            b.accentColor = {185, 212, 235, 255};
                            b.hasStorefront = true;
                        } else {
                            b.primaryColor = {152, 104, 86, 255};
                            b.accentColor = {232, 208, 175, 255};
                            b.hasStorefront = rng.chance(0.65f);
                            b.hasBalconies = true;
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
                        b.height = rng.range(12.0f, 25.0f);
                        b.style = BuildingStyle::BrickApartment;
                        const int pal = rng.rangeInt(0, 2);
                        if (pal == 0) b.primaryColor = {222, 208, 188, 255};
                        else if (pal == 1) b.primaryColor = {192, 218, 228, 255};
                        else b.primaryColor = {232, 192, 178, 255};
                        b.accentColor = {82, 168, 196, 255};
                        b.litWindows = true;
                        b.hasBalconies = true;
                        b.hasStorefront = rng.chance(0.45f);
                        break;
                    }
                }
                buildings_.push_back(b);
            }
        }
    }

    // Boulevard sidewalk planter trees, bus shelters, benches, trash bins & billboards
    for (int r = 1; r <= kRoadCount - 2; ++r) {
        const float rc = roadCoord(r);
        for (int blk = 1; blk < kBlockCount - 1; ++blk) {
            const float segMid = (roadCoord(blk) + roadCoord(blk + 1)) * 0.5f;

            // Curbside boulevard trees in concrete planters along major avenues (outside pedestrian lanes)
            if ((r + blk) % 2 == 0) {
                const bool palm = (r <= 2 || blk <= 1);
                trees_.push_back(TreeProp{{segMid - 14.0f, rc + halfRoad + 0.65f}, 3.1f, 2.0f, palm, true});
                trees_.push_back(TreeProp{{segMid + 14.0f, rc - halfRoad - 0.65f}, 3.1f, 2.0f, palm, true});
            }

            // Realistic glass bus stop shelters & benches on avenues r = 3, 4, 5
            if ((r == 3 || r == 4 || r == 5) && (blk % 2 == 1)) {
                const float shelterZ = rc + halfRoad + kSidewalkWidth - 0.35f;
                streetProps_.push_back(StreetProp{{segMid, shelterZ}, StreetPropKind::BusShelter, 0.0f, false, {}, 0.0f});
                streetProps_.push_back(StreetProp{{segMid + 4.2f, shelterZ}, StreetPropKind::TrashBin, 0.0f, false, {}, 0.0f});
            }
        }
    }

    // Coastal Sunset Beach palm colonnade + colorful beach umbrellas on the Western sandy beach
    for (int i = -8; i <= 8; ++i) {
        const float pos = static_cast<float>(i) * 38.0f;
        trees_.push_back(TreeProp{{-349.5f, pos}, 4.5f, 2.7f, true, false});
        trees_.push_back(TreeProp{{pos, -349.5f}, 4.4f, 2.6f, true, false});
        if (std::abs(pos) > 16.0f && (i % 2 == 0)) {
            streetProps_.push_back(StreetProp{{-366.0f, pos + 4.0f}, StreetPropKind::BeachUmbrella, static_cast<float>(i) * 0.5f, false, {}, 0.0f});
            streetProps_.push_back(StreetProp{{-374.0f, pos - 6.0f}, StreetPropKind::BeachUmbrella, static_cast<float>(i) * 0.9f, false, {}, 0.0f});
        }
    }

    // Commercial avenue billboards on outer corners
    const std::array<Vec2, 6> billboardSpots = {{
        {-96.0f, -96.0f}, {96.0f, -96.0f}, {-180.0f, 74.0f},
        {180.0f, -12.0f}, {96.0f, 180.0f}, {-12.0f, 180.0f}
    }};
    for (std::size_t i = 0; i < billboardSpots.size(); ++i) {
        streetProps_.push_back(StreetProp{
            billboardSpots[i],
            StreetPropKind::Billboard,
            (i % 2 == 0) ? 0.45f : -0.45f,
            false, {}, 0.0f
        });
    }

    // Streetlights, hydrants, and destructible crates/cones along intersections
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
        {roadCoord(5) - halfRoad - 2.0f, roadCoord(4) - halfRoad - 2.0f},
        4.2f,
        "MISSION: VIP EXOTIC DELIVERY",
        "미션: 슈퍼카 탈취 & 긴급 배달",
        {255, 214, 52, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionRace,
        {roadCoord(4) - halfRoad - 2.0f, roadCoord(3) + halfRoad + 2.0f},
        4.2f,
        "MISSION: MIDNIGHT STREET SPRINT",
        "미션: 도심 스트리트 체크포인트 레이스",
        {112, 255, 118, 255}
    });

    pois_.push_back(PointOfInterest{
        PoiKind::MissionGangRaid,
        {roadCoord(5) + halfRoad + 2.0f, roadCoord(5) - halfRoad - 2.0f},
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

    // Allow walking/driving out onto Sunset Beach & Wooden Pier on the West coast (x down to -382m)
    const float minLimitX = (std::abs(pos.z) < 7.2f) ? (-424.0f + radius) : (-382.0f + radius);
    const float limit = kHalfCitySpan - radius;
    if (pos.x < minLimitX) { pos.x = minLimitX; accumNormal.x += 1.0f; hit = true; }
    if (pos.x > limit)     { pos.x = limit;     accumNormal.x -= 1.0f; hit = true; }
    if (pos.z < -limit)    { pos.z = -limit;    accumNormal.z += 1.0f; hit = true; }
    if (pos.z > limit)     { pos.z = limit;     accumNormal.z -= 1.0f; hit = true; }

    for (const auto& b : buildings_) {
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

    const float minLimitX = (std::abs(current.center.z) < 6.5f) ? (-422.0f + obb.halfExtents.x) : (-380.0f + obb.halfExtents.x);
    const float limit = kHalfCitySpan - obb.halfExtents.x;
    if (current.center.x < minLimitX) { outPush.x += (minLimitX - current.center.x); current.center.x = minLimitX; anyHit = true; }
    if (current.center.x > limit)     { outPush.x += (limit - current.center.x);     current.center.x = limit;     anyHit = true; }
    if (current.center.z < -limit)    { outPush.z += (-limit - current.center.z);    current.center.z = -limit;    anyHit = true; }
    if (current.center.z > limit)     { outPush.z += (limit - current.center.z);     current.center.z = limit;     anyHit = true; }

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

float CityMap::raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const {
    float bestT = maxDist + 1.0f;
    const float endX = origin.x + dir.x * maxDist;
    const float endZ = origin.z + dir.z * maxDist;
    const float minX = std::min(origin.x, endX) - 1.0f;
    const float maxX = std::max(origin.x, endX) + 1.0f;
    const float minZ = std::min(origin.z, endZ) - 1.0f;
    const float maxZ = std::max(origin.z, endZ) + 1.0f;

    for (const auto& b : buildings_) {
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
    return (bestT <= maxDist) ? bestT : -1.0f;
}

} // namespace oc
