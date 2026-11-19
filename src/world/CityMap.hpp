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
    CentralPark,
    CivicCenter,
    SuburbanEstates,
    NorthPeninsula
};

enum class BuildingStyle {
    GlassTower,
    ModernOffice,
    BrickApartment,
    Warehouse,
    ShippingStack,
    ShopBuilding,
    GarageBuilding,
    DealershipBuilding,
    BoutiqueBuilding,
    NightclubBuilding,
    CivicLandmark,
    VillaHouse
};

struct Building {
    AABB2D footprint;
    float height = 20.0f;
    float upperTierHeight = 0.0f; // optional stepped skyscraper upper tier
    ColorRGBA primaryColor{};
    ColorRGBA accentColor{};
    ColorRGBA awningColor{148, 52, 48, 255};
    BuildingStyle style = BuildingStyle::ModernOffice;
    bool litWindows = true;
    bool hasStorefront = false;
    bool hasBalconies = false;
    bool hasHelipad = false;
};

struct GasStationCanopy {
    AABB2D bounds; // Drive-through overhead roof (vehicles pass freely underneath)
    float clearanceHeight = 4.9f;
};

struct ParkPatch {
    AABB2D bounds;
    ColorRGBA grassColor{62, 92, 46, 255};
};

struct LakePatch {
    AABB2D bounds;
    float waterY = -0.02f;
};

enum class TreeSpecies : std::uint8_t {
    OakDeciduous = 0,
    PalmCoastal = 1,
    CypressConifer = 2
};

struct TreeProp {
    Vec2 pos;
    float trunkHeight = 2.8f;
    float crownRadius = 2.2f;
    bool palmStyle = false;
    bool inPlanter = false;
    TreeSpecies species = TreeSpecies::OakDeciduous;
};

enum class StreetPropKind {
    StreetLight,
    Hydrant,
    Crate,
    TrafficCone,
    BusShelter,
    Bench,
    TrashBin,
    Billboard,
    FuelPump,
    BeachUmbrella,
    Bollard
};

struct StreetProp {
    Vec2 pos;
    StreetPropKind kind = StreetPropKind::StreetLight;
    float yaw = 0.0f;
    bool broken = false;
    Vec2 velocity{0.0f, 0.0f};
    float brokenTimer = 0.0f;
    Vec2 homePos{0.0f, 0.0f};
    float posY = 0.0f;
    float velY = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float angVelPitch = 0.0f;
    float angVelRoll = 0.0f;
};

enum class PoiKind {
    WeaponShop,
    TuneGarage,
    CarDealership,
    ClothingBoutique,
    GasStation,
    NightclubProperty,
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
    // Expanded 16x16 City Metropolis (~1.45km x 1.65km drivable land incl. North Bay Lighthouse Peninsula!)
    static constexpr int kRoadCount = 17;          // 17 N-S & 17 E-W avenues -> 16x16 city blocks (4x area!)
    static constexpr int kBlockCount = 16;
    static constexpr int kSidewalkNodeCount = kRoadCount * 2; // 34 sidewalk lines (0..33)
    static constexpr float kBlockPitch = 84.0f;    // center-to-center road spacing
    static constexpr float kRoadWidth = 18.0f;     // 4-lane avenue width
    static constexpr float kSidewalkWidth = 3.5f;
    static constexpr float kSidewalkCenterOffset = kRoadWidth * 0.5f + kSidewalkWidth * 0.5f; // 10.75m
    static constexpr float kLaneOffset = 4.2f;     // center of travel lane from road centerline
    static constexpr float kHalfCitySpan = 702.0f; // Main island concrete seawall promenade extent
    static constexpr float kWestBeachOuter = -746.0f;
    static constexpr float kWestPierOuter  = -796.0f;
    static constexpr float kBridgeStartZ   = 681.0f;
    static constexpr float kBridgeEndZ     = 815.0f;
    static constexpr float kPeninsulaMaxZ  = 952.0f;
    static constexpr float kPeninsulaHalfW = 128.0f;
    static constexpr float kOceanBoundary  = 990.0f;

    // Spatial acceleration grid aligned to 84m city blocks for O(1) collision & rendering queries
    static constexpr int kGridCols = 20;           // x in [-840, +840]
    static constexpr int kGridRows = 22;           // z in [-840, +1008]
    static constexpr float kGridOriginX = -840.0f;
    static constexpr float kGridOriginZ = -840.0f;
    static constexpr float kGridCellSize = 84.0f;

    CityMap();

    void generate(std::uint64_t seed = 20261107ULL);

    float roadCoord(int roadIndex) const;
    int nearestRoadIndex(float coord) const;
    float sidewalkCoord(int sidewalkNodeIndex) const;
    int nearestSidewalkNodeIndex(float coord) const;
    static bool isCrosswalkEdge(int fromNode, int toNode) {
        return (fromNode != toNode) && ((fromNode / 2) == (toNode / 2));
    }
    bool isOnRoad(const Vec2& p, float margin = 0.0f) const;
    bool isInIntersection(const Vec2& p) const;
    bool isOnNorthBridge(const Vec2& p, float margin = 0.0f) const;
    bool isOnNorthPeninsula(const Vec2& p) const;
    float bridgeDeckElevationAt(const Vec2& p, float* outSlopeZ = nullptr) const;

    DistrictType districtAt(int bx, int bz) const;
    DistrictType districtAtWorld(const Vec2& p) const;

    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<GasStationCanopy>& canopies() const { return canopies_; }
    const std::vector<ParkPatch>& parks() const { return parks_; }
    const std::vector<LakePatch>& lakes() const { return lakes_; }
    const std::vector<TreeProp>& trees() const { return trees_; }
    std::vector<StreetProp>& streetProps() { return streetProps_; }
    const std::vector<StreetProp>& streetProps() const { return streetProps_; }
    const std::vector<PointOfInterest>& pois() const { return pois_; }

    // Fast spatial-grid queries for rendering & AI (returns indices into respective vectors)
    void queryBuildingsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;
    void queryTreesInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;
    void queryPropsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;

    const PointOfInterest* findPoi(PoiKind kind) const;

    // Collision resolution against static buildings, park lake shoreline, bridge rails & ocean seawalls
    bool resolveCircleWorld(Vec2& pos, float radius, Vec2* outHitNormal = nullptr) const;
    bool resolveOBBBuildings(const OBB2D& obb, Vec2& outPush) const;
    float raycastBuildings(const Vec2& origin, const Vec2& dir, float maxDist) const;
    float raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const;

private:
    void buildSpatialIndex();
    static int gridCellX(float x);
    static int gridCellZ(float z);
    static int gridFlatIndex(int cx, int cz) { return cz * kGridCols + cx; }

    std::vector<Building> buildings_;
    std::vector<GasStationCanopy> canopies_;
    std::vector<ParkPatch> parks_;
    std::vector<LakePatch> lakes_;
    std::vector<TreeProp> trees_;
    std::vector<StreetProp> streetProps_;
    std::vector<PointOfInterest> pois_;

    std::vector<std::vector<int>> buildingGrid_;
    std::vector<std::vector<int>> treeGrid_;
    std::vector<std::vector<int>> propGrid_;
};

} // namespace oc
