#pragma once

#include "core/Math.hpp"
#include <string>
#include <vector>

namespace oc {

enum class DistrictType {
    Downtown,
    Midtown,
    Industrial,
    CoastalResidential,
    CentralPark
};

enum class BuildingStyle {
    GlassTower,
    ModernOffice,
    BrickApartment,
    Warehouse,
    ShippingStack,
    ShopBuilding,
    GarageBuilding
};

struct Building {
    AABB2D footprint;
    float height = 20.0f;
    float upperTierHeight = 0.0f; // optional stepped skyscraper upper tier
    ColorRGBA primaryColor{};
    ColorRGBA accentColor{};
    BuildingStyle style = BuildingStyle::ModernOffice;
    bool litWindows = true;
};

struct ParkPatch {
    AABB2D bounds;
    ColorRGBA grassColor{58, 128, 66, 255};
};

struct TreeProp {
    Vec2 pos;
    float trunkHeight = 2.8f;
    float crownRadius = 2.2f;
    bool palmStyle = false;
};

enum class StreetPropKind {
    StreetLight,
    Hydrant,
    Crate,
    TrafficCone
};

struct StreetProp {
    Vec2 pos;
    StreetPropKind kind = StreetPropKind::StreetLight;
    float yaw = 0.0f;
    bool broken = false;
    Vec2 velocity{0.0f, 0.0f};
    float brokenTimer = 0.0f;
};

enum class PoiKind {
    WeaponShop,
    TuneGarage,
    MissionDelivery,
    MissionRace,
    MissionGangRaid
};

struct PointOfInterest {
    PoiKind kind = PoiKind::WeaponShop;
    Vec2 pos;
    float radius = 4.5f;
    std::string nameEn;
    std::string nameKo;
    ColorRGBA markerColor{};
};

class CityMap {
public:
    static constexpr int kRoadCount = 9;          // 9 N-S & 9 E-W avenues -> 8x8 city blocks
    static constexpr int kBlockCount = 8;
    static constexpr float kBlockPitch = 84.0f;   // center-to-center road spacing
    static constexpr float kRoadWidth = 18.0f;    // 4-lane avenue width
    static constexpr float kSidewalkWidth = 3.5f;
    static constexpr float kLaneOffset = 4.2f;    // center of travel lane from road centerline
    static constexpr float kHalfCitySpan = 365.0f;
    static constexpr float kOceanBoundary = 395.0f;

    CityMap();

    void generate(std::uint64_t seed = 20261107ULL);

    float roadCoord(int roadIndex) const;
    int nearestRoadIndex(float coord) const;
    bool isOnRoad(const Vec2& p, float margin = 0.0f) const;
    bool isInIntersection(const Vec2& p) const;

    DistrictType districtAt(int bx, int bz) const;
    DistrictType districtAtWorld(const Vec2& p) const;

    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<ParkPatch>& parks() const { return parks_; }
    const std::vector<TreeProp>& trees() const { return trees_; }
    std::vector<StreetProp>& streetProps() { return streetProps_; }
    const std::vector<StreetProp>& streetProps() const { return streetProps_; }
    const std::vector<PointOfInterest>& pois() const { return pois_; }

    const PointOfInterest* findPoi(PoiKind kind) const;

    // Collision resolution against static buildings and ocean walls
    bool resolveCircleWorld(Vec2& pos, float radius, Vec2* outHitNormal = nullptr) const;
    bool resolveOBBBuildings(const OBB2D& obb, Vec2& outPush) const;
    float raycastBuildings(const Vec2& origin, const Vec2& dir, float maxDist) const;

private:
    std::vector<Building> buildings_;
    std::vector<ParkPatch> parks_;
    std::vector<TreeProp> trees_;
    std::vector<StreetProp> streetProps_;
    std::vector<PointOfInterest> pois_;
};

} // namespace oc
