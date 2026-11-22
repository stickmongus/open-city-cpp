#pragma once

#include "core/Math.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

#pragma pack(push, 1)
struct OcMapHeader {
    char magic[4];
    std::uint32_t version;
    float minX, maxX, minZ, maxZ, chunkSize;
    std::uint32_t chunksX, chunksZ;
    float demMinX, demMaxX, demMinZ, demMaxZ;
    std::uint32_t demW, demH;
    std::uint32_t roadCount, roadVertCount;
    std::uint32_t bldgCount, bldgVertCount;
    std::uint32_t waterTriCount, parkTriCount;
    std::uint32_t poiCount;
    std::uint32_t flatChunkRoadCount, flatChunkBldgCount, flatChunkPoiCount;
};
static_assert(sizeof(OcMapHeader) == 100, "OcMapHeader must be 100 bytes");

#pragma pack(pop)

struct OsmRoadRec {
    std::uint32_t firstVert;
    std::uint16_t vertCount;
    std::uint8_t roadClass;  // 0=motorway,1=trunk,2=primary,3=secondary,4=tertiary,5=residential,6=living
    std::uint8_t lanes;
    std::uint8_t flags;      // bit0=oneway, bit1=bridge/elevated, bit2=tunnel
    std::uint8_t nameSlot;
    std::int8_t layer;       // Real vertical grade layer (-1=underpass, 0=ground, 1..3=elevated viaduct/flyover)
    std::uint8_t flags2;     // bit0=isLink(IC ramp), bit1=isRingOrExpressway, bit2=isViaduct, bit3=isHanRiverBridge
    float halfWidth;
    float minX, minZ, maxX, maxZ;
};
static_assert(sizeof(OsmRoadRec) == 32 && alignof(OsmRoadRec) == 4, "OsmRoadRec must be naturally 4-byte aligned");

struct OsmBldgRec {
    std::uint32_t firstVert;
    std::uint16_t vertCount;
    std::uint8_t style;       // 0=Apt,1=BlueGlass,2=DarkMetalGlass,3=Granite,4=Brick,5=Stucco,6=Hanok,7=Civic,8=Industrial,9=Villa
    std::uint8_t roofShape;   // 0=Flat,1=Gabled,2=Hipped,3=Pyramidal,4=Dome,5=Skillion,6=SteppedCrown
    std::uint8_t facadeR;     // True 24-bit per-building architectural RGB
    std::uint8_t facadeG;
    std::uint8_t facadeB;
    std::uint8_t roofHeightQ; // Roof height in 0.25m units (roofH = roofHeightQ * 0.25f)
    std::uint8_t flags;       // bit0=isPart, bit1=hasRetail1F, bit2=hasPodium, bit3=hasCrownBand
    std::uint8_t variantSeed; // 0..255 per-building architectural/glass/lighting seed
    std::uint16_t pad_;
    float cx, cz;
    float baseY, minHeight, height;
    float halfX, halfZ;
};
static_assert(sizeof(OsmBldgRec) == 44 && alignof(OsmBldgRec) == 4, "OsmBldgRec must be 44 bytes and naturally 4-byte aligned");

#pragma pack(push, 1)

struct OcChunkHdr {
    std::uint32_t roadStart, roadLen;
    std::uint32_t bldgStart, bldgLen;
    std::uint32_t poiStart, poiLen;
};
static_assert(sizeof(OcChunkHdr) == 24, "OcChunkHdr must be 24 bytes");

struct OsmTri2D {
    float x0, z0, x1, z1, x2, z2;
};
static_assert(sizeof(OsmTri2D) == 24, "OsmTri2D must be 24 bytes");

struct OsmPoiRec {
    float x, z;
    std::uint16_t kind;     // 0=SubwayEntrance, 1=BusStop
    std::uint16_t nameSlot;
};
static_assert(sizeof(OsmPoiRec) == 12, "OsmPoiRec must be 12 bytes");
#pragma pack(pop)

struct OsmLocalVert {
    std::int16_t lx; // 0.05m units relative to OsmBldgRec::cx
    std::int16_t lz; // 0.05m units relative to OsmBldgRec::cz
};
static_assert(sizeof(OsmLocalVert) == 4, "OsmLocalVert must be 4 bytes");

struct NearestOsmRoad {
    bool found = false;
    std::uint32_t roadIdx = 0;
    std::uint32_t segIdx = 0;
    float dist = 1e9f;
    float halfWidth = 6.0f;
    float deckY = 0.0f;
    std::uint8_t roadClass = 5;
    std::int8_t layer = 0;
    bool isBridge = false;
    bool isOneway = false;
    bool isLink = false;
    Vec2 closestPt{};
    Vec2 tangent{1.0f, 0.0f}; // Unit direction along road segment
};

struct RoadSurfaceHit {
    bool found = false;
    float y = 0.0f;
    std::uint32_t roadIdx = 0;
    std::uint8_t roadClass = 5;
    std::int8_t layer = 0;
    bool isBridge = false;
    bool isLink = false;
    float halfWidth = 3.5f;
    Vec2 tangent{1.0f, 0.0f};
};

struct RoadLaneLayout {
    int totalLanes = 0;         // Total painted lanes across entire road width (0 = unmarked alley)
    int lanesInDir = 1;         // Number of drivable lanes for one travel direction
    float driveLeft = 0.0f;     // Left boundary of drivable lane strip along travel rightN (meters)
    float driveRight = 0.0f;    // Right boundary of drivable lane strip along travel rightN (excludes right 갓길!)
    float laneWidth = 3.25f;    // Exact width of each painted lane (meters)
    std::uint8_t styleByte = 0; // Matches world.frag styleByte encoding identically

    float laneCenterOffset(int laneIdx) const {
        if (totalLanes <= 0 || lanesInDir <= 0) return 0.0f;
        const int idx = std::clamp(laneIdx, 0, lanesInDir - 1);
        return driveLeft + (static_cast<float>(idx) + 0.5f) * laneWidth;
    }
};

class RealSeoulData {
public:
    bool loadFromBytes(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const std::string& path);
    bool tryAutoLoadDesktop();

    bool isLoaded() const { return loaded_; }
    bool has3DRoads() const { return has3DRoads_; }
    const OcMapHeader& header() const { return hdr_; }

    // Real SRTM 30m DEM terrain elevation anywhere across Greater Seoul (30.7km x 24.5km)
    float sampleDem(const Vec2& p) const;

    bool isInCoreBounds(const Vec2& p) const {
        return loaded_ && p.x >= hdr_.minX && p.x <= hdr_.maxX && p.z >= hdr_.minZ && p.z <= hdr_.maxZ;
    }

    // O(1) spatial chunk queries
    void queryChunkRoads(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const;
    void queryChunkBuildings(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const;
    void queryChunkPois(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const;

    // Exact 2D real-OSM polygon building collision & 2D/3D raycasting
    bool resolveCircleBuildings(Vec2& pos, float radius, Vec2* outNormal = nullptr, float entityY = -1e9f) const;
    float raycastBuildings2D(const Vec2& origin, const Vec2& dir, float maxDist) const;
    float raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const;

    // Exact nearest real OSM road & true 3D multi-level deck queries
    static constexpr float roadClassLift(std::uint8_t roadClass) {
        return 0.035f + static_cast<float>(roadClass < 6u ? (6u - roadClass) : 0u) * 0.008f;
    }
    static RoadLaneLayout computeRoadLaneLayout(const OsmRoadRec& r) {
        RoadLaneLayout out{};
        const float hw = std::max(2.6f, r.halfWidth);
        const bool isOneway = (r.flags & 1u) != 0u;
        const bool isBridge = (r.flags & 2u) != 0u;
        const bool isLink = (r.flags2 & 1u) != 0u;
        const bool isExpressway = (r.roadClass <= 1u) || ((r.flags2 & 2u) != 0u);
        if (isExpressway && isOneway && hw >= 2.8f) {
            // Exact match with world.frag isHighway (styleByte == 176):
            // Inner median shoulder (shL) + outer emergency shoulder 갓길 (shR) + ~3.45m Korean highway lanes!
            const float shR = (hw >= 7.0f) ? 1.15f : 0.55f;
            const float shL = (hw >= 7.0f) ? 0.58f : 0.32f;
            out.driveLeft = -hw + shL;
            out.driveRight = hw - shR;
            const float driveSpan = std::max(2.4f, out.driveRight - out.driveLeft);
            int maxByWidth = std::max(1, static_cast<int>(std::floor(driveSpan / 2.85f)));
            int laneCnt = (r.lanes >= 1u && r.lanes <= 6u)
                ? std::min<int>(static_cast<int>(r.lanes), maxByWidth)
                : std::clamp(static_cast<int>(std::lround(driveSpan / 3.45f)), 1, 6);
            if (isLink) laneCnt = std::min(laneCnt, 2);
            out.totalLanes = std::clamp(laneCnt, 1, 6);
            out.lanesInDir = out.totalLanes;
            out.laneWidth = driveSpan / static_cast<float>(out.totalLanes);
            out.styleByte = 176u;
        } else if (r.roadClass <= 4u && hw >= 3.1f) {
            const float usableH = std::max(1.8f, hw - 0.34f);
            if (isOneway) {
                const int maxByWidth = std::max(1, static_cast<int>(std::floor((2.0f * usableH) / 2.75f)));
                int laneCnt = (r.lanes >= 1u && r.lanes <= 8u)
                    ? std::min<int>(static_cast<int>(r.lanes), maxByWidth)
                    : std::clamp(static_cast<int>(std::lround((hw * 2.0f - 0.68f) / 3.25f)), 1, 8);
                if (isLink) laneCnt = std::min(laneCnt, 2);
                out.totalLanes = std::clamp(laneCnt, 1, 8);
                out.lanesInDir = out.totalLanes;
                out.driveLeft = -usableH;
                out.driveRight = usableH;
                out.laneWidth = (2.0f * usableH) / static_cast<float>(out.totalLanes);
                if (isBridge) {
                    out.styleByte = 204u; // Bridge one-way: white edge lines, NO crosswalks
                } else if (r.roadClass == 2u && !isLink && out.totalLanes >= 4) {
                    out.styleByte = 240u; // Major Seoul city boulevard with iconic blue bus-only lane!
                } else {
                    out.styleByte = 128u; // Standard city one-way multi-lane street
                }
            } else {
                const int maxPerDir = std::max(1, static_cast<int>(std::floor((usableH - 0.22f) / 2.75f)));
                int perDir = (r.lanes >= 2u && r.lanes <= 10u)
                    ? std::min<int>(static_cast<int>(r.lanes / 2u), maxPerDir)
                    : std::clamp(static_cast<int>(std::lround((hw - 0.56f) / 3.25f)), 1, 4);
                out.lanesInDir = std::clamp(perDir, 1, 4);
                out.totalLanes = out.lanesInDir * 2;
                out.driveLeft = 0.22f;
                out.driveRight = usableH;
                out.laneWidth = (usableH - 0.22f) / static_cast<float>(out.lanesInDir);
                out.styleByte = 64u; // Two-way street with double yellow centerline
            }
        }
        return out;
    }

    RoadSurfaceHit findRoadSurfaceAt(const Vec2& p, float refY = -1e9f, float extraMargin = 0.75f) const;
    NearestOsmRoad findNearestRoad(const Vec2& p, float maxSearchDist = 48.0f, float refY = -1e9f) const;
    float roadSurfaceLiftAt(const Vec2& p) const;
    bool isOnRoad(const Vec2& p, float extraMargin = 0.0f) const;
    bool isOnLowerRoadUnder(const Vec2& p, float deckY, float extraMargin = 0.8f) const;
    bool isOnBridge(const Vec2& p, float extraMargin = 0.0f) const;
    bool isInWater(const Vec2& p) const;

    // Connected road-graph junction lookup for smooth real-road AI traffic & police (respects 3D elevation!)
    bool findConnectingRoadAt(const Vec2& junctionPt, std::uint32_t excludeRoadIdx,
                              std::uint32_t& outRoadIdx, std::uint32_t& outVertIdx, int& outDir,
                              float curY = -1e9f) const;

    // Direct read-only accessors for 3D Vulkan chunk mesh streaming
    const std::vector<OsmRoadRec>& roads() const { return roads_; }
    const std::vector<Vec2>& roadVerts() const { return roadVerts_; }
    const std::vector<float>& roadVertYs() const { return roadVertY_; }
    float roadVertY(std::uint32_t idx) const { return (idx < roadVertY_.size()) ? roadVertY_[idx] : 0.0f; }
    const std::vector<OsmBldgRec>& buildings() const { return buildings_; }
    const std::vector<OsmLocalVert>& bldgVerts() const { return bldgVerts_; }
    const std::vector<OsmTri2D>& waterTris() const { return waterTris_; }
    const std::vector<OsmTri2D>& parkTris() const { return parkTris_; }
    const std::vector<OsmPoiRec>& pois() const { return pois_; }
    const std::vector<OcChunkHdr>& chunks() const { return chunks_; }
    const std::vector<std::uint32_t>& flatChunkRoads() const { return flatChunkRoads_; }
    const std::vector<std::uint32_t>& flatChunkBldgs() const { return flatChunkBldgs_; }

    Vec2 bldgWorldVert(const OsmBldgRec& b, std::uint16_t localIdx) const {
        const OsmLocalVert& lv = bldgVerts_[b.firstVert + localIdx];
        return {b.cx + static_cast<float>(lv.lx) * 0.05f, b.cz + static_cast<float>(lv.lz) * 0.05f};
    }

private:
    bool loaded_ = false;
    bool has3DRoads_ = false;
    OcMapHeader hdr_{};
    std::vector<std::int16_t> demDecimeters_;
    std::vector<OsmRoadRec> roads_;
    std::vector<Vec2> roadVerts_;
    std::vector<float> roadVertY_;
    std::vector<OsmBldgRec> buildings_;
    std::vector<OsmLocalVert> bldgVerts_;
    std::vector<OcChunkHdr> chunks_;
    std::vector<std::uint32_t> flatChunkRoads_;
    std::vector<std::uint32_t> flatChunkBldgs_;
    std::vector<std::uint32_t> flatChunkPois_;
    std::vector<OsmTri2D> waterTris_;
    std::vector<OsmTri2D> parkTris_;
    std::vector<OsmPoiRec> pois_;

    // Fast 2D grid accelerating water triangle point-in-triangle tests
    static constexpr int kWaterGridW = 128;
    static constexpr int kWaterGridH = 84;
    std::vector<std::vector<std::uint32_t>> waterGrid_;
    void buildWaterGrid();
};

} // namespace oc
