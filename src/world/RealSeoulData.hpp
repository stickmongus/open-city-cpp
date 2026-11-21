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
    std::uint8_t flags;      // bit0=oneway, bit1=bridge, bit2=tunnel
    std::uint8_t nameSlot;
    std::uint16_t pad_;
    float halfWidth;
    float minX, minZ, maxX, maxZ;
};
static_assert(sizeof(OsmRoadRec) == 32 && alignof(OsmRoadRec) == 4, "OsmRoadRec must be naturally 4-byte aligned");

#pragma pack(push, 1)

struct OsmBldgRec {
    std::uint32_t firstVert;
    std::uint16_t vertCount;
    std::uint8_t style;      // 0=KoreanApt,1=GlassTower,2=GraniteOffice,3=BrickComm,4=StuccoComm,5=Hanok,6=Civic
    std::uint8_t colorIdx;
    float cx, cz;
    float baseY, height;
    float halfX, halfZ;
};
static_assert(sizeof(OsmBldgRec) == 32, "OsmBldgRec must be 32 bytes");

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
    std::uint8_t roadClass = 5;
    bool isBridge = false;
    bool isOneway = false;
    Vec2 closestPt{};
    Vec2 tangent{1.0f, 0.0f}; // Unit direction along road segment
};

class RealSeoulData {
public:
    bool loadFromBytes(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const std::string& path);
    bool tryAutoLoadDesktop();

    bool isLoaded() const { return loaded_; }
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
    bool resolveCircleBuildings(Vec2& pos, float radius, Vec2* outNormal = nullptr) const;
    float raycastBuildings2D(const Vec2& origin, const Vec2& dir, float maxDist) const;
    float raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const;

    // Exact nearest real OSM road & bridge / water queries
    static constexpr float roadClassLift(std::uint8_t roadClass) {
        return 0.035f + static_cast<float>(roadClass < 6u ? (6u - roadClass) : 0u) * 0.008f;
    }
    NearestOsmRoad findNearestRoad(const Vec2& p, float maxSearchDist = 48.0f) const;
    float roadSurfaceLiftAt(const Vec2& p) const;
    bool isOnRoad(const Vec2& p, float extraMargin = 0.0f) const;
    bool isOnBridge(const Vec2& p, float extraMargin = 0.0f) const;
    bool isInWater(const Vec2& p) const;

    // Connected road-graph junction lookup for smooth real-road AI traffic & police!
    bool findConnectingRoadAt(const Vec2& junctionPt, std::uint32_t excludeRoadIdx,
                              std::uint32_t& outRoadIdx, std::uint32_t& outVertIdx, int& outDir) const;

    // Direct read-only accessors for 3D Vulkan chunk mesh streaming
    const std::vector<OsmRoadRec>& roads() const { return roads_; }
    const std::vector<Vec2>& roadVerts() const { return roadVerts_; }
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
    OcMapHeader hdr_{};
    std::vector<std::int16_t> demDecimeters_;
    std::vector<OsmRoadRec> roads_;
    std::vector<Vec2> roadVerts_;
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
