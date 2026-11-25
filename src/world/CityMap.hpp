#pragma once

#include "core/Math.hpp"
#include "world/RealSeoulData.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

enum class DistrictType {
    GwanghwamunPalace,   // 광화문·경복궁
    JongnoCheonggye,     // 종로·청계천
    MyeongdongCityHall,  // 명동·시청
    NamsanItaewon,       // 남산·이태원
    Yongsan,             // 용산·한강로
    YeouidoFinance,      // 여의도 금융지구
    GangnamTeheran,      // 강남·테헤란로
    SeochoBanpo,         // 서초·반포
    JamsilSongpa,        // 잠실·송파 (롯데월드타워)
    SeongsuKonkuk,       // 성수·건대·서울숲
    HongdaeMapo,         // 홍대·신촌·마포
    Dongdaemun,          // 동대문·을지로
    GangbukNowon,        // 강북·노원·도봉
    GangseoMagok,        // 강서·마곡
    GuroGwanak,          // 구로·관악·신림
    HanRiverPark         // 한강 수변공원
};

enum class BuildingStyle {
    GlassTower,
    ModernOffice,
    KoreanApartment,     // 실제 서울 대단지 아파트 (동 번호·규칙적 발코니·필로티)
    BrickCommercial,     // 서울 근린상가·홍대/성수 벽돌 상가
    Warehouse,
    ShopBuilding,
    GarageBuilding,
    DealershipBuilding,
    BoutiqueBuilding,
    NightclubBuilding,
    CivicLandmark,       // 서울시청·국회의사당·관공서
    HanokPalace,         // 경복궁·숭례문·전통 한옥 기와/단청 건축
    VillaHouse,          // 이태원·성북·평창 고급 주택/빌라
    SupertallSpire       // 롯데월드타워 555m·63빌딩·파크원 초고층
};

struct Building {
    AABB2D footprint;
    float baseY = 0.0f;          // Ground terrain elevation underneath footprint
    float height = 20.0f;
    float upperTierHeight = 0.0f;
    ColorRGBA primaryColor{};
    ColorRGBA accentColor{};
    ColorRGBA awningColor{56, 86, 118, 255};
    BuildingStyle style = BuildingStyle::ModernOffice;
    bool litWindows = true;
    bool hasStorefront = false;
    bool hasBalconies = false;
    bool hasHelipad = false;
    bool isMacroLandmark = false;
    // Orientation metadata: which facade edges face an actual street!
    // bit 0 = West(-X), bit 1 = East(+X), bit 2 = North(-Z), bit 3 = South(+Z)
    std::uint8_t streetEdgeMask = 0x0Fu;
    std::uint8_t facadeVariant = 0; // Architectural facade sub-style & signage palette
    float facingYaw = 0.0f;         // Primary front direction in radians (+X=0, +Z=+pi/2)
};

struct GasStationCanopy {
    AABB2D bounds;
    float baseY = 0.0f;
    float clearanceHeight = 4.9f;
};

struct ParkPatch {
    AABB2D bounds;
    float baseY = 0.0f;
    ColorRGBA grassColor{62, 96, 48, 255};
};

struct LakePatch {
    AABB2D bounds;
    float waterY = 0.04f;
};

enum class TreeSpecies : std::uint8_t {
    GinkgoDeciduous = 0, // 서울 가로수 은행나무/느티나무
    CherryBlossom = 1,   // 여의도·석촌호수 벚나무
    KoreanPine = 2       // 남산·궁궐 소나무/상록수
};

struct TreeProp {
    Vec2 pos;
    float baseY = 0.0f;
    float trunkHeight = 2.8f;
    float crownRadius = 2.2f;
    bool inPlanter = false;
    TreeSpecies species = TreeSpecies::GinkgoDeciduous;
    float yaw = 0.0f;
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
    Bollard,
    TrafficLight,    // 서울 교차로 4구 신호등 캔틸레버
    SubwayEntrance,  // 서울 지하철 유리 캐노피 출입구 + 노선 폴사인
    RoadGantry,      // 한국형 녹색 도로 이정표 갠트리 (반포대로·강남대로·테헤란로 등)
    PedGuardrail,    // 서울 대로변 보행자 방호울타리
    ShopSign,        // 1~2층 한국 상가 돌출/전면 한글 간판 + 어닝 캐노피
    UtilityBox,      // 한전 배전함 / 교통신호 제어기 캐비닛
    StreetKiosk,     // 서울 대로변 가로판매대 / 구두수선대 부스
    DdareungiRack,   // 서울시 공공자전거 따릉이 대여소 거치대
    CafeTable,       // 보도변 카페 야외 파라솔 & 테이블
    MedianBarrier    // 서울 대로 중앙분리대 콘크리트 연석 & 방현 펜스
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
    std::uint8_t variant = 0; // Sign slot / width / color variant
    float baseY = -1e9f;      // Explicit 3D deck/ground elevation (falls back to surfaceHeightAt when <= -1e8)
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
    float radius = 4.8f;
    std::string nameEn;
    std::string nameKo;
    ColorRGBA markerColor{};
};

// Real Seoul Han River Bridge descriptor (15 drivable bridges spanning East-West across Han River)
struct SeoulBridge {
    std::string nameEn;
    std::string nameKo;
    float centerX = 0.0f;      // Road centerline X (aligned to 84m grid!)
    float zNorth = -500.0f;    // North bank deck start
    float zSouth =  500.0f;    // South bank deck end
    float halfWidth = 11.0f;   // Wide 6-lane bridge roadway half-width
    float deckHeight = 7.5f;   // Clearance above Han River water
    bool hasRainbowFountain = false; // 반포대교 달빛무지개분수
    bool isCableStayed = false;      // 올림픽대교/서강대교/성수대교 주탑
    bool isArchBridge = false;       // 서강대교/한강대교 아치 트러스
    ColorRGBA accentColor{176, 52, 42, 255};
};

// Iconic real Seoul macro landmarks (always visible on the 30km x 25km skyline & full-screen map)
enum class SeoulLandmarkKind {
    LotteWorldTower,     // 롯데월드타워 (555m, 잠실)
    NSeoulTower,         // N서울타워 (남산 정상)
    GyeongbokgungPalace, // 경복궁 근정전 & 광화문 광장
    Building63,          // 여의도 63빌딩
    NationalAssembly,    // 여의도 국회의사당
    Parc1RedTower,       // 여의도 파크원
    CoexTradeTower,      // 삼성동 코엑스·트레이드타워
    SeoulCityHall,       // 서울시청 & 서울광장
    SungnyemunGate,      // 숭례문 (남대문)
    DongdaemunDDP,       // 동대문 DDP
    JamsilStadium,       // 잠실종합운동장
    GangnamStationPlaza  // 강남역 사거리
};

struct SeoulLandmark {
    SeoulLandmarkKind kind = SeoulLandmarkKind::LotteWorldTower;
    Vec2 pos{};
    float height = 100.0f;
    std::string nameEn;
    std::string nameKo;
    DistrictType district = DistrictType::GangnamTeheran;
    float yawRad = 0.0f; // Authentic real-world geographic heading!
};

struct SeoulMountain {
    std::string nameEn;
    std::string nameKo;
    Vec2 center{};
    float radiusX = 1600.0f;
    float radiusZ = 1400.0f;
    float peakHeight = 240.0f;
};

struct SeoulParkZone {
    std::string nameEn;
    std::string nameKo;
    Vec2 center{};
    Vec2 halfSize{};
    float yawRad = 0.0f;
    ColorRGBA grassColor{58, 102, 50, 255};
};

struct SeoulTributary {
    std::string nameEn;
    std::string nameKo;
    std::vector<Vec2> points;
    float halfWidth = 28.0f;
};

struct SeoulArterialRoad {
    std::string nameEn;
    std::string nameKo;
    std::vector<Vec2> points;
    float halfWidth = 10.0f;
    bool isExpressway = false;
};

class CityMap {
public:
    // Real-Scale Seoul Metropolis (~30 km East-West x ~25 km North-South = ~750 km^2 bounding box!)
    // Cartographic orientation: +X = East, -Z = North, +Z = South, -X = West
    static constexpr float kWorldMinX = -15036.0f; // 179 * 84m west (Gimpo / Gangseo)
    static constexpr float kWorldMaxX =  15036.0f; // 179 * 84m east (Gangdong / Gwangnaru)
    static constexpr float kWorldMinZ = -12516.0f; // 149 * 84m north (Bukhansan / Dobong / Nowon)
    static constexpr float kWorldMaxZ =  12516.0f; // 149 * 84m south (Gwanaksan / Seocho / Songpa)
    static constexpr float kHalfCitySpanX = 15036.0f;
    static constexpr float kHalfCitySpanZ = 12516.0f;

    // 84m urban avenue grid across all 30km x 25km (359 N-S x 299 E-W avenues, streamed O(1)!)
    static constexpr float kBlockPitch = 84.0f;
    static constexpr float kRoadWidth = 18.0f;
    static constexpr float kSidewalkWidth = 3.5f;
    static constexpr float kSidewalkCenterOffset = kRoadWidth * 0.5f + kSidewalkWidth * 0.5f; // 10.75m
    static constexpr float kLaneOffset = 4.2f;
    static constexpr int kMinRoadIndexX = -178;
    static constexpr int kMaxRoadIndexX =  178;
    static constexpr int kMinRoadIndexZ = -148;
    static constexpr int kMaxRoadIndexZ =  148;

    // Deterministic streaming chunk window: each chunk = 3x3 blocks (252m x 252m), 5x5 active = 1.26km window
    static constexpr int kBlocksPerChunk = 3;
    static constexpr float kChunkSize = kBlockPitch * kBlocksPerChunk; // 252m
    static constexpr int kStreamRadiusChunks = 2; // 5x5 chunks = 25 active chunks around focus

    // Legacy compatibility aliases for existing systems
    static constexpr int kRoadCount = 357;
    static constexpr int kBlockCount = 356;

    CityMap();

    void generate(std::uint64_t seed = 20261107ULL);

    // Streams deterministic Seoul city blocks, buildings, Korean apartments, trees & props around focus
    void updateStreaming(const Vec2& focus, bool forceRebuild = false);
    Vec2 streamedCenter() const { return streamedCenter_; }

    // Infinite-range signed road & sidewalk coordinates (0 = world origin 0m)
    float roadCoord(int roadIndex) const { return static_cast<float>(roadIndex) * kBlockPitch; }
    int nearestRoadIndex(float coord) const;
    float sidewalkCoord(int sidewalkNodeIndex) const;
    int nearestSidewalkNodeIndex(float coord) const;
    static bool isCrosswalkEdge(int fromNode, int toNode);

    // Real Seoul Analytic Geography (O(1) anywhere across 30km x 25km!)
    float hanRiverCenterZ(float x) const;
    float hanRiverHalfWidth(float x) const;
    float hanRiverSlopeDzDx(float x) const;
    bool isOnYeouidoIsland(const Vec2& p) const;
    bool isInHanRiverWater(const Vec2& p, float margin = 0.0f) const;
    bool isInHanRiverPark(const Vec2& p) const;
    bool isInSeokchonLake(const Vec2& p) const;
    bool isInCheonggyecheon(const Vec2& p) const;
    bool isInTributaryWater(const Vec2& p, float margin = 0.0f) const;
    const SeoulParkZone* findMajorSeoulPark(const Vec2& p) const;
    bool isInMajorSeoulPark(const Vec2& p) const { return findMajorSeoulPark(p) != nullptr; }
    bool isOnRiversideExpressway(const Vec2& p) const;

    // Real Seoul Mountains & Bridge Elevation
    float terrainElevationAt(const Vec2& p) const;
    float terrainSlopeAlong(const Vec2& p, const Vec2& dir) const;
    const SeoulBridge* findBridgeAt(const Vec2& p, float margin = 0.0f) const;
    bool isOnBridge(const Vec2& p, float margin = 0.0f) const;
    float bridgeDeckElevationAt(const Vec2& p, float* outSlopeZ = nullptr) const;
    float surfaceHeightAt(const Vec2& p, float refY = -1e9f) const;

    bool isOnRoad(const Vec2& p, float margin = 0.0f) const;
    bool isInIntersection(const Vec2& p) const;
    bool isMajorArterialRoad(int roadIdx, bool horizontal) const;

    DistrictType districtAtWorld(const Vec2& p) const;
    static const char* districtNameEn(DistrictType d);
    static const char* districtNameKo(DistrictType d);

    // Active streamed + macro data accessors
    const std::vector<Building>& buildings() const { return buildings_; }
    const std::vector<Building>& macroBuildings() const { return macroBuildings_; }
    const std::vector<GasStationCanopy>& canopies() const { return canopies_; }
    const std::vector<ParkPatch>& parks() const { return parks_; }
    const std::vector<LakePatch>& lakes() const { return lakes_; }
    const std::vector<TreeProp>& trees() const { return trees_; }
    std::vector<StreetProp>& streetProps() { return streetProps_; }
    const std::vector<StreetProp>& streetProps() const { return streetProps_; }
    const std::vector<PointOfInterest>& pois() const { return pois_; }
    const std::vector<SeoulBridge>& bridges() const { return bridges_; }
    const std::vector<SeoulLandmark>& landmarks() const { return landmarks_; }
    const std::vector<SeoulMountain>& mountains() const { return mountains_; }
    const std::vector<SeoulMountain>& perimeterMountains() const { return perimeterMountains_; }
    const std::vector<SeoulParkZone>& majorParks() const { return majorParks_; }
    const std::vector<SeoulTributary>& tributaries() const { return tributaries_; }
    const std::vector<SeoulArterialRoad>& arterials() const { return arterials_; }

    // Fast radius queries over active streamed + macro structures
    void queryBuildingsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;
    void queryTreesInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;
    void queryPropsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const;

    const PointOfInterest* findPoi(PoiKind kind) const;
    const SeoulLandmark* findLandmark(SeoulLandmarkKind kind) const;

    // Collision & LOS raycasts against streamed + macro buildings, Han River embankments & bridges
    bool resolveCircleWorld(Vec2& pos, float radius, Vec2* outHitNormal = nullptr) const;
    bool resolveOBBBuildings(const OBB2D& obb, Vec2& outPush, float entityY = -1e9f) const;
    float raycastBuildings(const Vec2& origin, const Vec2& dir, float maxDist) const;
    float raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const;

    // Real-World OpenStreetMap + Overture + TUM + 15m DTM + 1:1 Synced Eclipse SUMO OCRM v1 Seoul GIS Loader (v6.4.0)
    bool loadRealSeoulMap(const std::uint8_t* bytes, std::size_t size);
    bool loadRealSeoulMapFromFile(const std::string& path);
    bool loadOcrmRoadMesh(const std::uint8_t* bytes, std::size_t size);
    bool loadOcrmRoadMeshFromFile(const std::string& path);
    bool hasRealSeoulMap() const { return realData_.isLoaded(); }
    bool hasOcrmRoadMesh() const { return realData_.hasOcrmMesh(); }
    const RealSeoulData& realData() const { return realData_; }

    // Default iconic Seoul spawn (Analytic Banpo-daero fallback or Real OSM Gangnam/Teheran-ro boulevard!)
    static Vec2 defaultSpawnPos() { return {599.0f, 996.0f}; }
    Vec2 spawnPos() const { return hasRealSeoulMap() ? realSpawnPos_ : defaultSpawnPos(); }
    float spawnYaw() const { return hasRealSeoulMap() ? realSpawnYaw_ : -kHalfPi; }
    Vec2 policeRespawnPos() const { return hasRealSeoulMap() ? realPoliceRespawnPos_ : Vec2{656.0f, 1000.8f}; }

private:
    void buildSeoulMacroGeography();
    void buildRealSeoulMacroGeography();
    void generateChunk(int chunkX, int chunkZ);
    void streamRealSeoulProps(const Vec2& focus);
    void rebuildActiveSpatialGrid();

    RealSeoulData realData_;
    Vec2 realSpawnPos_{3764.4f, 2845.6f};
    float realSpawnYaw_ = -0.35f;
    Vec2 realPoliceRespawnPos_{3820.0f, 2825.0f};

    std::uint64_t seed_ = 20261107ULL;
    int lastCenterChunkX_ = 999999;
    int lastCenterChunkZ_ = 999999;
    Vec2 streamedCenter_{0.0f, 0.0f};

    // Always-loaded macro Seoul structures
    std::vector<SeoulBridge> bridges_;
    std::vector<SeoulLandmark> landmarks_;
    std::vector<SeoulMountain> mountains_;
    std::vector<SeoulMountain> perimeterMountains_;
    std::vector<SeoulParkZone> majorParks_;
    std::vector<SeoulTributary> tributaries_;
    std::vector<SeoulArterialRoad> arterials_;
    std::vector<Building> macroBuildings_;
    std::vector<PointOfInterest> pois_;
    std::vector<LakePatch> lakes_;

    // Active streamed window (macroBuildings_ appended first + 5x5 local chunks)
    std::vector<Building> buildings_;
    std::vector<GasStationCanopy> canopies_;
    std::vector<ParkPatch> parks_;
    std::vector<TreeProp> trees_;
    std::vector<StreetProp> streetProps_;
};

} // namespace oc
