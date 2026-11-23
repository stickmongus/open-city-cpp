#pragma once

#include "core/Math.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oc {

#pragma pack(push, 1)
struct OcrmHeader {
    char magic[4];            // "OCRM"
    std::uint32_t version;    // 1
    std::uint32_t flags;      // bit0=LZ4 raw block compressed, bit1=Korea Official HD Map / 실폭도로 present
    float minX, maxX, minZ, maxZ, chunkSize;
    std::uint32_t chunksX, chunksZ;
    std::uint32_t totalRoadTris;
    std::uint32_t totalSidewalkTris;
};
static_assert(sizeof(OcrmHeader) == 48, "OcrmHeader must be 48 bytes");

struct OcrmChunkEntry {
    std::uint32_t offset;
    std::uint32_t compSize;
    std::uint32_t uncompSize;
};
static_assert(sizeof(OcrmChunkEntry) == 12, "OcrmChunkEntry must be 12 bytes");

struct OcrmPackedVertex {
    std::int16_t qx; // (wx - chunkCenterX) * 200.0 (5mm precision)
    std::int16_t qy; // wy * 100.0 (1cm precision)
    std::int16_t qz; // (wz - chunkCenterZ) * 200.0 (5mm precision)
    std::int16_t uq; // curb distance in cm (or -100 for vertical curbs / pure granite median islands)
    std::int8_t nx;
    std::int8_t ny;
    std::int8_t nz;
    std::uint8_t flags;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};
static_assert(sizeof(OcrmPackedVertex) == 16, "OcrmPackedVertex must be 16 bytes");

struct OcrmChunkBlobHeader {
    std::uint32_t numVerts;
    std::uint32_t nIdxUrban;
    std::uint32_t nIdxHighway;
    std::uint32_t nIdxInter;
    std::uint32_t nIdxSidewalk;
    std::uint32_t nIdxMarking;
    std::uint32_t nIdxBridge;
};
static_assert(sizeof(OcrmChunkBlobHeader) == 28, "OcrmChunkBlobHeader must be 28 bytes");
#pragma pack(pop)

struct OcrmDecodedVertex {
    float px, py, pz;
    float nx, ny, nz;
    float uCm;
    std::uint8_t flags;
    std::uint8_t r, g, b, a;
};

struct OcrmDecodedChunk {
    std::vector<OcrmDecodedVertex> verts;
    std::vector<std::uint16_t> idxUrban;
    std::vector<std::uint16_t> idxHighway;
    std::vector<std::uint16_t> idxInter;
    std::vector<std::uint16_t> idxSidewalk;
    std::vector<std::uint16_t> idxMarking;
    std::vector<std::uint16_t> idxBridge;
};

// Zero-dependency C++20 loader & on-demand LZ4 chunk decoder for OCRM v1 topological 3D road meshes
// (compiled by Eclipse SUMO netconvert OR Korean Government NGII HD Map / TL_SPRD_RW SHP importer).
class OcrmRoadMesh {
public:
    bool loadFromBytes(const std::uint8_t* data, std::size_t size);
    bool loadFromFile(const std::string& path);
    void clear();

    bool isLoaded() const { return loaded_; }
    bool isKoreaHdMap() const { return loaded_ && ((hdr_.flags & 2u) != 0u); }
    const OcrmHeader& header() const { return hdr_; }
    std::size_t activeChunkCount() const { return activeChunkCount_; }

    bool hasChunk(int cx, int cz) const;
    bool decodeChunk(int cx, int cz, OcrmDecodedChunk& out) const;

    // Exact 3D barycentric height query against decoded OCRM road & sidewalk triangles
    // (0.000mm visual-vs-physics mismatch across curbs, yellow tactile strips, plazas & bridges!).
    bool sampleSurfaceHeightAt(const Vec2& p, float refY, float& outY,
                               bool* outIsSidewalk = nullptr) const;

    // Bounds-checked zero-dependency LZ4 raw block decompressor (exposed for unit tests)
    static bool decompressLz4Block(const std::uint8_t* src, std::size_t srcSize,
                                   std::uint8_t* dst, std::size_t dstSize);

private:
    struct SurfTri {
        float x0, y0, z0;
        float x1, y1, z1;
        float x2, y2, z2;
        bool isSidewalk;
    };

    struct CachedSurfaceChunk {
        static constexpr int kGridN = 16;
        int cx = -1;
        int cz = -1;
        std::uint64_t lastUse = 0;
        float minX = 0.0f;
        float minZ = 0.0f;
        float invCellSize = 1.0f;
        std::vector<SurfTri> tris;
        std::vector<std::vector<std::uint32_t>> cells; // kGridN * kGridN
    };

    const CachedSurfaceChunk* getOrBuildSurfaceChunk(int cx, int cz) const;

    bool loaded_ = false;
    OcrmHeader hdr_{};
    std::size_t activeChunkCount_ = 0;
    std::vector<OcrmChunkEntry> directory_;
    std::vector<std::uint8_t> rawFile_;
    mutable std::vector<CachedSurfaceChunk> surfaceCache_;
    mutable std::uint64_t cacheClock_ = 0;
};

} // namespace oc
