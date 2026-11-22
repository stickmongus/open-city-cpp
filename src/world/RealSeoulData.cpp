#include "world/RealSeoulData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace oc {

namespace {

bool pointInTri2D(const Vec2& p, const OsmTri2D& t) {
    const float v0x = t.x2 - t.x0, v0z = t.z2 - t.z0;
    const float v1x = t.x1 - t.x0, v1z = t.z1 - t.z0;
    const float v2x = p.x  - t.x0, v2z = p.z  - t.z0;
    const float dot00 = v0x * v0x + v0z * v0z;
    const float dot01 = v0x * v1x + v0z * v1z;
    const float dot02 = v0x * v2x + v0z * v2z;
    const float dot11 = v1x * v1x + v1z * v1z;
    const float dot12 = v1x * v2x + v1z * v2z;
    const float denom = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denom) < 1e-6f) return false;
    const float inv = 1.0f / denom;
    const float u = (dot11 * dot02 - dot01 * dot12) * inv;
    const float v = (dot00 * dot12 - dot01 * dot02) * inv;
    return (u >= -1e-4f) && (v >= -1e-4f) && (u + v <= 1.0001f);
}

Vec2 closestPointOnSeg(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const float l2 = ab.lengthSq();
    if (l2 < 1e-6f) return a;
    const float t = clampf((p - a).dot(ab) / l2, 0.0f, 1.0f);
    return a + ab * t;
}

bool raySegmentIntersect2D(const Vec2& o, const Vec2& d, const Vec2& a, const Vec2& b, float& outT) {
    const Vec2 e = b - a;
    const float denom = d.x * e.z - d.z * e.x;
    if (std::abs(denom) < 1e-7f) return false;
    const Vec2 ao = a - o;
    const float t = (ao.x * e.z - ao.z * e.x) / denom;
    const float u = (ao.x * d.z - ao.z * d.x) / denom;
    if (t >= 0.0f && u >= 0.0f && u <= 1.0f) {
        outT = t;
        return true;
    }
    return false;
}

} // namespace

bool RealSeoulData::loadFromBytes(const std::uint8_t* data, std::size_t size) {
    loaded_ = false;
    has3DRoads_ = false;
    isOcm6_ = false;
    demScale_ = 0.1f;
    if (!data || size < sizeof(OcMapHeader)) return false;

    std::size_t off = 0;
    auto readBlock = [&](void* dst, std::size_t bytes) -> bool {
        if (off + bytes > size) return false;
        if (bytes > 0) std::memcpy(dst, data + off, bytes);
        off += bytes;
        return true;
    };

    if (!readBlock(&hdr_, sizeof(OcMapHeader))) return false;
    const bool isOcm6 = (std::memcmp(hdr_.magic, "OCM6", 4) == 0 && hdr_.version == 4);
    const bool isOcm5 = (std::memcmp(hdr_.magic, "OCM5", 4) == 0 && hdr_.version == 3);
    const bool isOcm4 = (std::memcmp(hdr_.magic, "OCM4", 4) == 0 && hdr_.version == 2);
    const bool isOcm3 = (std::memcmp(hdr_.magic, "OCM3", 4) == 0 && hdr_.version == 1);
    if (!isOcm6 && !isOcm5 && !isOcm4 && !isOcm3) return false;
    if (hdr_.chunksX == 0 || hdr_.chunksZ == 0 || hdr_.demW < 2 || hdr_.demH < 2) return false;
    isOcm6_ = isOcm6;
    demScale_ = isOcm6 ? 0.04f : 0.1f;
    const bool is3DRoadsFmt = isOcm6 || isOcm5;

    const std::size_t demCount = static_cast<std::size_t>(hdr_.demW) * static_cast<std::size_t>(hdr_.demH);
    const std::size_t numChunks = static_cast<std::size_t>(hdr_.chunksX) * static_cast<std::size_t>(hdr_.chunksZ);

    demDecimeters_.resize(demCount);
    roads_.resize(hdr_.roadCount);
    roadVerts_.resize(hdr_.roadVertCount);
    roadVertY_.resize(hdr_.roadVertCount, 0.0f);
    buildings_.resize(hdr_.bldgCount);
    bldgVerts_.resize(hdr_.bldgVertCount);
    chunks_.resize(numChunks);
    flatChunkRoads_.resize(hdr_.flatChunkRoadCount);
    flatChunkBldgs_.resize(hdr_.flatChunkBldgCount);
    flatChunkPois_.resize(hdr_.flatChunkPoiCount);
    waterTris_.resize(hdr_.waterTriCount);
    parkTris_.resize(hdr_.parkTriCount);
    pois_.resize(hdr_.poiCount);

    if (!readBlock(demDecimeters_.data(), demCount * sizeof(std::int16_t))) return false;
    if (is3DRoadsFmt) {
        if (!readBlock(roads_.data(), roads_.size() * sizeof(OsmRoadRec))) return false;
        std::vector<Vec3> verts3(hdr_.roadVertCount);
        if (!readBlock(verts3.data(), verts3.size() * sizeof(Vec3))) return false;
        for (std::size_t i = 0; i < verts3.size(); ++i) {
            roadVerts_[i] = {verts3[i].x, verts3[i].z};
            roadVertY_[i] = verts3[i].y;
        }
        has3DRoads_ = true;
    } else {
        #pragma pack(push, 1)
        struct OsmRoadRecDisk {
            std::uint32_t firstVert;
            std::uint16_t vertCount;
            std::uint8_t roadClass;
            std::uint8_t lanes;
            std::uint8_t flags;
            std::uint8_t nameSlot;
            float halfWidth;
            float minX, minZ, maxX, maxZ;
        };
        #pragma pack(pop)
        static_assert(sizeof(OsmRoadRecDisk) == 30, "OsmRoadRecDisk must be 30 bytes");
        std::vector<OsmRoadRecDisk> diskRoads(hdr_.roadCount);
        if (!readBlock(diskRoads.data(), diskRoads.size() * sizeof(OsmRoadRecDisk))) return false;
        for (std::size_t i = 0; i < diskRoads.size(); ++i) {
            const OsmRoadRecDisk& d = diskRoads[i];
            OsmRoadRec& r = roads_[i];
            r.firstVert = d.firstVert;
            r.vertCount = d.vertCount;
            r.roadClass = d.roadClass;
            r.lanes = d.lanes;
            r.flags = d.flags;
            r.nameSlot = d.nameSlot;
            r.layer = ((d.flags & 2u) != 0u) ? 1 : 0;
            r.flags2 = (d.roadClass <= 1u) ? 2u : 0u;
            r.halfWidth = d.halfWidth;
            r.minX = d.minX;
            r.minZ = d.minZ;
            r.maxX = d.maxX;
            r.maxZ = d.maxZ;
        }
        if (!readBlock(roadVerts_.data(), roadVerts_.size() * sizeof(Vec2))) return false;
    }
    if (is3DRoadsFmt || isOcm4) {
        if (!readBlock(buildings_.data(), buildings_.size() * sizeof(OsmBldgRec))) return false;
    } else {
        #pragma pack(push, 1)
        struct OsmBldgRecV3 {
            std::uint32_t firstVert;
            std::uint16_t vertCount;
            std::uint8_t style;
            std::uint8_t colorIdx;
            float cx, cz;
            float baseY, height;
            float halfX, halfZ;
        };
        #pragma pack(pop)
        static_assert(sizeof(OsmBldgRecV3) == 32, "OsmBldgRecV3 must be 32 bytes");
        std::vector<OsmBldgRecV3> v3(hdr_.bldgCount);
        if (!readBlock(v3.data(), v3.size() * sizeof(OsmBldgRecV3))) return false;
        for (std::size_t i = 0; i < v3.size(); ++i) {
            const OsmBldgRecV3& s = v3[i];
            OsmBldgRec& b = buildings_[i];
            b.firstVert = s.firstVert;
            b.vertCount = s.vertCount;
            // Remap legacy OCM3 style (0..6) to OCM4 style (0..9)
            static constexpr std::uint8_t kMap[7] = {0, 1, 3, 4, 5, 6, 7};
            b.style = (s.style < 7u) ? kMap[s.style] : 3u;
            b.roofShape = (b.style == 6u) ? 2u : 0u;
            b.facadeR = 204u;
            b.facadeG = 200u;
            b.facadeB = 194u;
            b.roofHeightQ = (b.style == 6u) ? 8u : 0u;
            b.flags = (b.style == 3u || b.style == 4u || b.style == 5u) ? 2u : 0u;
            b.variantSeed = static_cast<std::uint8_t>((s.colorIdx * 61u + static_cast<std::uint32_t>(i) * 37u) & 255u);
            b.pad_ = 0u;
            b.cx = s.cx;
            b.cz = s.cz;
            b.baseY = s.baseY;
            b.minHeight = 0.0f;
            b.height = s.height;
            b.halfX = s.halfX;
            b.halfZ = s.halfZ;
        }
    }
    if (!readBlock(bldgVerts_.data(), bldgVerts_.size() * sizeof(OsmLocalVert))) return false;
    if (!readBlock(chunks_.data(), chunks_.size() * sizeof(OcChunkHdr))) return false;
    if (!readBlock(flatChunkRoads_.data(), flatChunkRoads_.size() * sizeof(std::uint32_t))) return false;
    if (!readBlock(flatChunkBldgs_.data(), flatChunkBldgs_.size() * sizeof(std::uint32_t))) return false;
    if (!readBlock(flatChunkPois_.data(), flatChunkPois_.size() * sizeof(std::uint32_t))) return false;
    if (!readBlock(waterTris_.data(), waterTris_.size() * sizeof(OsmTri2D))) return false;
    if (!readBlock(parkTris_.data(), parkTris_.size() * sizeof(OsmTri2D))) return false;
    if (!readBlock(pois_.data(), pois_.size() * sizeof(OsmPoiRec))) return false;

    // Filter SRTM C-band skyscraper radar spikes for legacy OCM3/OCM4 maps (OCM5/OCM6 already bake bare-earth DTM!)
    if (!is3DRoadsFmt) {
        const int W = static_cast<int>(hdr_.demW);
        const int H = static_cast<int>(hdr_.demH);
        const float invDemX = static_cast<float>(W - 1) / (hdr_.demMaxX - hdr_.demMinX);
        const float invDemZ = static_cast<float>(H - 1) / (hdr_.demMaxZ - hdr_.demMinZ);
        std::vector<std::uint8_t> urbanMask(demCount, 0u);
        auto markCell = [&](float x, float z) {
            const int ix = std::clamp(static_cast<int>(std::lround((x - hdr_.demMinX) * invDemX)), 0, W - 1);
            const int iz = std::clamp(static_cast<int>(std::lround((z - hdr_.demMinZ) * invDemZ)), 0, H - 1);
            const std::size_t idx = static_cast<std::size_t>(iz * W + ix);
            if (demDecimeters_[idx] < 1350) { // Keep Namsan/Bukhansan/Gwanaksan peaks (>= 135m) untouched!
                urbanMask[idx] = 1u;
            }
        };
        for (const auto& r : roads_) {
            if (r.roadClass > 3) continue;
            for (std::uint16_t vi = 0; vi < r.vertCount; ++vi) {
                const Vec2& p = roadVerts_[r.firstVert + vi];
                markCell(p.x, p.z);
            }
        }
        for (const auto& b : buildings_) {
            if (b.height >= 26.0f) {
                markCell(b.cx, b.cz);
            }
        }
        // Dilate urban CBD mask by 1 DEM cell (~80m) so slopes beside skyscrapers smooth cleanly too
        std::vector<std::uint8_t> dilated = urbanMask;
        for (int iz = 1; iz + 1 < H; ++iz) {
            for (int ix = 1; ix + 1 < W; ++ix) {
                if (urbanMask[static_cast<std::size_t>(iz * W + ix)]) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const std::size_t ni = static_cast<std::size_t>((iz + dz) * W + (ix + dx));
                            if (demDecimeters_[ni] < 1350) dilated[ni] = 1u;
                        }
                    }
                }
            }
        }
        // Clamp skyscraper-radar domes to 9x9 (~720m) 15th-percentile bare-earth baseline + 9.0m
        std::vector<std::int16_t> filtered = demDecimeters_;
        std::int16_t winBuf[81];
        for (int iz = 0; iz < H; ++iz) {
            for (int ix = 0; ix < W; ++ix) {
                const std::size_t idx = static_cast<std::size_t>(iz * W + ix);
                if (!dilated[idx]) continue;
                int cnt = 0;
                for (int dz = -4; dz <= 4; ++dz) {
                    const int rz = std::clamp(iz + dz, 0, H - 1);
                    for (int dx = -4; dx <= 4; ++dx) {
                        const int rx = std::clamp(ix + dx, 0, W - 1);
                        winBuf[cnt++] = demDecimeters_[static_cast<std::size_t>(rz * W + rx)];
                    }
                }
                std::nth_element(winBuf, winBuf + 12, winBuf + cnt); // ~15th percentile bare-earth floor
                const std::int16_t capDm = static_cast<std::int16_t>(std::min<int>(32000, static_cast<int>(winBuf[12]) + 90));
                if (filtered[idx] > capDm) {
                    filtered[idx] = capDm;
                }
            }
        }
        // 2 gentle 3x3 smoothing passes on masked urban boulevard cells for silky realistic road grades
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<std::int16_t> next = filtered;
            for (int iz = 1; iz + 1 < H; ++iz) {
                for (int ix = 1; ix + 1 < W; ++ix) {
                    const std::size_t idx = static_cast<std::size_t>(iz * W + ix);
                    if (!dilated[idx]) continue;
                    int sum = 0;
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            sum += filtered[static_cast<std::size_t>((iz + dz) * W + (ix + dx))];
                        }
                    }
                    const int avg = (sum + 4) / 9;
                    next[idx] = static_cast<std::int16_t>((static_cast<int>(filtered[idx]) + avg * 2) / 3);
                }
            }
            filtered.swap(next);
        }
        demDecimeters_.swap(filtered);
    }

    buildWaterGrid();
    loaded_ = true;

    if (!is3DRoadsFmt) {
        // Legacy OCM3/OCM4: re-snap buildings & synthesize roadVertY_ from bare-earth DTM
        for (auto& b : buildings_) {
            b.baseY = sampleDem({b.cx, b.cz});
        }
        for (const auto& r : roads_) {
            const float lift = roadClassLift(r.roadClass);
            for (std::uint16_t vi = 0; vi < r.vertCount; ++vi) {
                const std::uint32_t idx = r.firstVert + vi;
                roadVertY_[idx] = sampleDem(roadVerts_[idx]) + lift;
            }
        }
    }

    // Cull any ground-level building footprint illegally sitting directly across drivable major ground roads
    // (Skip elevated viaducts / bridges so buildings beside/under viaducts are preserved cleanly!)
    {
        auto pointInBldgPoly = [&](const OsmBldgRec& b, const Vec2& pt) -> bool {
            if (b.vertCount < 3) return false;
            if (std::abs(pt.x - b.cx) > b.halfX + 0.5f || std::abs(pt.z - b.cz) > b.halfZ + 0.5f) return false;
            bool inside = false;
            const std::uint16_t n = b.vertCount;
            for (std::uint16_t i = 0; i < n; ++i) {
                const Vec2 a = bldgWorldVert(b, i);
                const Vec2 c = bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % n));
                if (((a.z > pt.z) != (c.z > pt.z)) &&
                    (pt.x < (c.x - a.x) * (pt.z - a.z) / ((c.z - a.z) + 1e-7f) + a.x)) {
                    inside = !inside;
                }
            }
            return inside;
        };

        auto cullAtPoint = [&](const Vec2& pt) {
            if (!isInCoreBounds(pt)) return;
            const int cx = std::clamp(static_cast<int>(std::floor((pt.x - hdr_.minX) / hdr_.chunkSize)),
                                      0, static_cast<int>(hdr_.chunksX) - 1);
            const int cz = std::clamp(static_cast<int>(std::floor((pt.z - hdr_.minZ) / hdr_.chunkSize)),
                                      0, static_cast<int>(hdr_.chunksZ) - 1);
            const OcChunkHdr& ch = chunks_[static_cast<std::size_t>(cz) * hdr_.chunksX + static_cast<std::size_t>(cx)];
            for (std::uint32_t j = 0; j < ch.bldgLen; ++j) {
                OsmBldgRec& bldg = buildings_[flatChunkBldgs_[ch.bldgStart + j]];
                if (bldg.vertCount < 3 || bldg.minHeight >= 4.5f) continue; // Keep elevated skybridges intact!
                if (pointInBldgPoly(bldg, pt)) {
                    bldg.vertCount = 0;
                    bldg.height = 0.0f;
                    bldg.halfX = 0.0f;
                    bldg.halfZ = 0.0f;
                }
            }
        };

        for (const OsmRoadRec& r : roads_) {
            if (r.roadClass > 4 || (r.flags & 4u) != 0u || (r.flags & 2u) != 0u || r.layer >= 1 || r.vertCount < 2) {
                continue; // Skip tunnels, elevated viaducts/bridges & narrow back alleys
            }
            const float hw = std::max(2.6f, r.halfWidth);
            const float probeLat = (r.roadClass <= 3u && hw >= 4.5f) ? (hw - 1.0f) : 0.0f;
            for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                const Vec2 a = roadVerts_[r.firstVert + s];
                const Vec2 b = roadVerts_[r.firstVert + s + 1];
                const Vec2 e = b - a;
                const float segLen = e.length();
                const Vec2 rightN = (segLen > 1e-4f) ? Vec2{-e.z / segLen, e.x / segLen} : Vec2{0.0f, 1.0f};
                const int steps = std::max(1, static_cast<int>(std::ceil(segLen / 5.5f)));
                for (int k = 0; k <= steps; ++k) {
                    const float t = static_cast<float>(k) / static_cast<float>(steps);
                    const Vec2 pt = a + e * t;
                    cullAtPoint(pt);
                    if (probeLat > 0.5f) {
                        cullAtPoint(pt - rightN * probeLat);
                        cullAtPoint(pt + rightN * probeLat);
                    }
                }
            }
        }
    }
    return true;
}

bool RealSeoulData::loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize sz = f.tellg();
    if (sz <= 0) return false;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(buf.data()), sz)) return false;
    return loadFromBytes(buf.data(), buf.size());
}

bool RealSeoulData::tryAutoLoadDesktop() {
    static constexpr const char* kCandidates[] = {
        "assets/maps/seoul_real.ocmap",
        "../assets/maps/seoul_real.ocmap",
        "../../assets/maps/seoul_real.ocmap",
        "maps/seoul_real.ocmap"
    };
    for (const char* p : kCandidates) {
        if (loadFromFile(p)) return true;
    }
    return false;
}

void RealSeoulData::buildWaterGrid() {
    waterGrid_.assign(static_cast<std::size_t>(kWaterGridW * kWaterGridH), {});
    const float invW = static_cast<float>(kWaterGridW) / (hdr_.maxX - hdr_.minX);
    const float invH = static_cast<float>(kWaterGridH) / (hdr_.maxZ - hdr_.minZ);
    for (std::uint32_t i = 0; i < waterTris_.size(); ++i) {
        const OsmTri2D& t = waterTris_[i];
        const float tx0 = std::min({t.x0, t.x1, t.x2});
        const float tx1 = std::max({t.x0, t.x1, t.x2});
        const float tz0 = std::min({t.z0, t.z1, t.z2});
        const float tz1 = std::max({t.z0, t.z1, t.z2});
        const int gx0 = std::clamp(static_cast<int>((tx0 - hdr_.minX) * invW), 0, kWaterGridW - 1);
        const int gx1 = std::clamp(static_cast<int>((tx1 - hdr_.minX) * invW), 0, kWaterGridW - 1);
        const int gz0 = std::clamp(static_cast<int>((tz0 - hdr_.minZ) * invH), 0, kWaterGridH - 1);
        const int gz1 = std::clamp(static_cast<int>((tz1 - hdr_.minZ) * invH), 0, kWaterGridH - 1);
        for (int gz = gz0; gz <= gz1; ++gz) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                waterGrid_[static_cast<std::size_t>(gz * kWaterGridW + gx)].push_back(i);
            }
        }
    }
}

float RealSeoulData::sampleDem(const Vec2& p) const {
    if (!loaded_ || demDecimeters_.empty()) return 0.0f;
    const int W = static_cast<int>(hdr_.demW);
    const int H = static_cast<int>(hdr_.demH);
    const float fx = clampf((p.x - hdr_.demMinX) / (hdr_.demMaxX - hdr_.demMinX) * static_cast<float>(W - 1),
                            0.0f, static_cast<float>(W - 1) - 0.001f);
    const float fz = clampf((p.z - hdr_.demMinZ) / (hdr_.demMaxZ - hdr_.demMinZ) * static_cast<float>(H - 1),
                            0.0f, static_cast<float>(H - 1) - 0.001f);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = fx - static_cast<float>(ix);
    const float tz = fz - static_cast<float>(iz);

    auto at = [&](int x, int z) -> float {
        const int cx = std::clamp(x, 0, W - 1);
        const int cz = std::clamp(z, 0, H - 1);
        return static_cast<float>(demDecimeters_[static_cast<std::size_t>(cz) * hdr_.demW + static_cast<std::size_t>(cx)]) * demScale_;
    };
    auto catmull = [](float v0, float v1, float v2, float v3, float t) -> float {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * v1) +
                       (-v0 + v2) * t +
                       (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * t2 +
                       (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * t3);
    };

    float rows[4];
    for (int j = 0; j < 4; ++j) {
        const int rz = iz + j - 1;
        rows[j] = catmull(at(ix - 1, rz), at(ix, rz), at(ix + 1, rz), at(ix + 2, rz), tx);
    }
    return catmull(rows[0], rows[1], rows[2], rows[3], tz);
}

void RealSeoulData::queryChunkRoads(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const {
    outIds.clear();
    if (!loaded_) return;
    const int cx0 = std::clamp(static_cast<int>(std::floor((center.x - radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((center.x + radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((center.z - radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((center.z + radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                outIds.push_back(flatChunkRoads_[ch.roadStart + k]);
            }
        }
    }
    std::sort(outIds.begin(), outIds.end());
    outIds.erase(std::unique(outIds.begin(), outIds.end()), outIds.end());
}

void RealSeoulData::queryChunkBuildings(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const {
    outIds.clear();
    if (!loaded_) return;
    const int cx0 = std::clamp(static_cast<int>(std::floor((center.x - radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((center.x + radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((center.z - radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((center.z + radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.bldgLen; ++k) {
                outIds.push_back(flatChunkBldgs_[ch.bldgStart + k]);
            }
        }
    }
    std::sort(outIds.begin(), outIds.end());
    outIds.erase(std::unique(outIds.begin(), outIds.end()), outIds.end());
}

void RealSeoulData::queryChunkPois(const Vec2& center, float radius, std::vector<std::uint32_t>& outIds) const {
    outIds.clear();
    if (!loaded_) return;
    const int cx0 = std::clamp(static_cast<int>(std::floor((center.x - radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((center.x + radius - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((center.z - radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((center.z + radius - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.poiLen; ++k) {
                outIds.push_back(flatChunkPois_[ch.poiStart + k]);
            }
        }
    }
}

bool RealSeoulData::resolveCircleBuildings(Vec2& pos, float radius, Vec2* outNormal, float entityY) const {
    if (!loaded_ || !isInCoreBounds(pos)) return false;
    const int cx = std::clamp(static_cast<int>(std::floor((pos.x - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz = std::clamp(static_cast<int>(std::floor((pos.z - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const OcChunkHdr& ch = chunks_[static_cast<std::size_t>(cz) * hdr_.chunksX + static_cast<std::size_t>(cx)];

    const bool checkY = (entityY > -1e8f);
    bool collided = false;
    Vec2 accumNormal{0.0f, 0.0f};
    const float r2 = radius * radius;

    for (std::uint32_t k = 0; k < ch.bldgLen; ++k) {
        const OsmBldgRec& b = buildings_[flatChunkBldgs_[ch.bldgStart + k]];
        if (!checkY && b.minHeight > 3.0f) continue; // Allow cars & pedestrians cleanly beneath skybridges / upper cantilevers!
        if (checkY) {
            const float bTop = b.baseY + b.height + static_cast<float>(b.roofHeightQ) * 0.25f;
            const float bBot = b.baseY + b.minHeight;
            if (entityY > bTop - 0.35f || entityY + 2.4f < bBot) continue;
        }
        if (std::abs(pos.x - b.cx) > b.halfX + radius + 0.2f ||
            std::abs(pos.z - b.cz) > b.halfZ + radius + 0.2f) {
            continue;
        }
        const std::uint16_t n = b.vertCount;
        if (n < 3) continue;

        // Exact point-in-polygon parity test + closest edge distance
        bool inside = false;
        float bestDistSq = 1e18f;
        Vec2 bestClosest{};
        Vec2 bestOutN{1.0f, 0.0f};

        for (std::uint16_t i = 0; i < n; ++i) {
            const Vec2 a = bldgWorldVert(b, i);
            const Vec2 c = bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % n));
            if (((a.z > pos.z) != (c.z > pos.z)) &&
                (pos.x < (c.x - a.x) * (pos.z - a.z) / ((c.z - a.z) + 1e-7f) + a.x)) {
                inside = !inside;
            }
            const Vec2 q = closestPointOnSeg(pos, a, c);
            const float d2 = (pos - q).lengthSq();
            if (d2 < bestDistSq) {
                bestDistSq = d2;
                bestClosest = q;
                // Polygon is CCW in (x, z), so outward edge normal is (dz, -dx) normalized
                const Vec2 e = c - a;
                const float el = e.length();
                if (el > 1e-5f) {
                    bestOutN = {e.z / el, -e.x / el};
                }
            }
        }

        if (inside) {
            // Push straight out past the nearest polygon wall!
            const float dist = std::sqrt(bestDistSq);
            Vec2 pushDir = (dist > 1e-4f) ? ((bestClosest - pos) / dist) : bestOutN;
            // Ensure pushDir points outward (aligned with bestOutN)
            if (pushDir.dot(bestOutN) < 0.0f) pushDir = bestOutN;
            pos = bestClosest + pushDir * (radius + 0.02f);
            accumNormal = accumNormal + pushDir;
            collided = true;
        } else if (bestDistSq < r2) {
            const float dist = std::sqrt(bestDistSq);
            const Vec2 nrm = (dist > 1e-4f) ? ((pos - bestClosest) / dist) : bestOutN;
            pos = bestClosest + nrm * (radius + 0.01f);
            accumNormal = accumNormal + nrm;
            collided = true;
        }
    }

    if (collided && outNormal) {
        *outNormal = (accumNormal.lengthSq() > 1e-6f) ? accumNormal.normalized() : Vec2{1.0f, 0.0f};
    }
    return collided;
}

float RealSeoulData::raycastBuildings2D(const Vec2& origin, const Vec2& dir, float maxDist) const {
    if (!loaded_) return -1.0f;
    const Vec2 endPt = origin + dir * maxDist;
    const float rx0 = std::min(origin.x, endPt.x) - 2.0f;
    const float rx1 = std::max(origin.x, endPt.x) + 2.0f;
    const float rz0 = std::min(origin.z, endPt.z) - 2.0f;
    const float rz1 = std::max(origin.z, endPt.z) + 2.0f;
    const int cx0 = std::clamp(static_cast<int>(std::floor((rx0 - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((rx1 - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((rz0 - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((rz1 - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);

    float bestT = maxDist;
    bool hit = false;
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.bldgLen; ++k) {
                const OsmBldgRec& b = buildings_[flatChunkBldgs_[ch.bldgStart + k]];
                if (b.minHeight > 3.0f || b.vertCount < 3) continue;
                if (b.cx + b.halfX < rx0 || b.cx - b.halfX > rx1 ||
                    b.cz + b.halfZ < rz0 || b.cz - b.halfZ > rz1) {
                    continue;
                }
                const std::uint16_t n = b.vertCount;
                for (std::uint16_t i = 0; i < n; ++i) {
                    const Vec2 a = bldgWorldVert(b, i);
                    const Vec2 c = bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % n));
                    float t = 0.0f;
                    if (raySegmentIntersect2D(origin, dir, a, c, t) && t < bestT) {
                        bestT = t;
                        hit = true;
                    }
                }
            }
        }
    }
    return hit ? bestT : -1.0f;
}

float RealSeoulData::raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const {
    if (!loaded_) return -1.0f;
    const Vec2 o2{origin.x, origin.z};
    const Vec2 d2{dir.x, dir.z};
    const float d2Len = d2.length();
    const Vec2 end2 = o2 + d2 * maxDist;
    const float rx0 = std::min(o2.x, end2.x) - 2.0f;
    const float rx1 = std::max(o2.x, end2.x) + 2.0f;
    const float rz0 = std::min(o2.z, end2.z) - 2.0f;
    const float rz1 = std::max(o2.z, end2.z) + 2.0f;
    const int cx0 = std::clamp(static_cast<int>(std::floor((rx0 - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((rx1 - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((rz0 - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((rz1 - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);

    float bestT = maxDist;
    bool hit = false;
    const Vec2 d2Norm = (d2Len > 1e-6f) ? (d2 / d2Len) : Vec2{1.0f, 0.0f};

    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.bldgLen; ++k) {
                const OsmBldgRec& b = buildings_[flatChunkBldgs_[ch.bldgStart + k]];
                if (b.vertCount < 3) continue;
                if (b.cx + b.halfX < rx0 || b.cx - b.halfX > rx1 ||
                    b.cz + b.halfZ < rz0 || b.cz - b.halfZ > rz1) {
                    continue;
                }
                const float yMin = (b.minHeight > 0.1f) ? (b.baseY + b.minHeight) : (b.baseY - 2.0f);
                const float yMax = b.baseY + b.height + static_cast<float>(b.roofHeightQ) * 0.25f;
                if (d2Len > 1e-5f) {
                    const std::uint16_t n = b.vertCount;
                    for (std::uint16_t i = 0; i < n; ++i) {
                        const Vec2 a = bldgWorldVert(b, i);
                        const Vec2 c = bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % n));
                        float t2d = 0.0f;
                        if (raySegmentIntersect2D(o2, d2Norm, a, c, t2d)) {
                            const float t3d = t2d / d2Len;
                            if (t3d >= 0.0f && t3d < bestT) {
                                const float hitY = origin.y + dir.y * t3d;
                                if (hitY >= yMin && hitY <= yMax) {
                                    bestT = t3d;
                                    hit = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return hit ? bestT : -1.0f;
}

RoadSurfaceHit RealSeoulData::findRoadSurfaceAt(const Vec2& p, float refY, float extraMargin) const {
    RoadSurfaceHit best{};
    if (!loaded_ || !isInCoreBounds(p)) return best;

    const bool hasRefY = (refY > -1e8f);
    const bool overWater = !hasRefY && isInWater(p);
    const float demY = sampleDem(p);
    const float searchR = std::max(22.0f, 22.0f + extraMargin);
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);

    struct RoadCand {
        std::uint32_t roadIdx;
        float deckY;
        float latDist;
        float longExcess;
        float hw;
        float effHw;
        std::uint8_t roadClass;
        std::int8_t layer;
        bool isBridge;
        bool isLink;
        Vec2 tangent;
    };
    RoadCand cands[24];
    int candCount = 0;
    constexpr float kEndcapM = 1.35f;

    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const std::uint32_t ri = flatChunkRoads_[ch.roadStart + k];
                const OsmRoadRec& r = roads_[ri];
                if (r.vertCount < 2 || (r.flags & 4u) != 0u) continue;
                const float baseHw = std::max(2.6f, r.halfWidth);
                const float effHw = baseHw + extraMargin;
                if (effHw <= 0.0f) continue;
                if (p.x < r.minX - effHw || p.x > r.maxX + effHw ||
                    p.z < r.minZ - effHw || p.z > r.maxZ + effHw) {
                    continue;
                }

                // Deduplicate roads straddling multiple chunks
                bool dup = false;
                for (int ci = 0; ci < candCount; ++ci) {
                    if (cands[ci].roadIdx == ri) { dup = true; break; }
                }
                if (dup) continue;

                // CRITICAL: Project perpendicularly onto the rectangular road ribbon (kEndcapM = 1.35m endcap,
                // NOT a giant 11m radius circle!) and pick the SINGLE TRUE CLOSEST segment along this polyline!
                float bestScore2 = 1e30f;
                float bestLat = 1e9f;
                float bestLong = 1e9f;
                std::uint16_t bestS = 0u;
                float bestT = 0.0f;
                Vec2 bestTang{1.0f, 0.0f};
                Vec2 bestQ{};

                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 ab = b - a;
                    const float l2 = ab.lengthSq();
                    if (l2 < 1e-6f) continue;
                    const float segL = std::sqrt(l2);
                    const float invL = 1.0f / segL;
                    const Vec2 dir{ab.x * invL, ab.z * invL};
                    const Vec2 dp = p - a;
                    const float along = dp.x * dir.x + dp.z * dir.z;
                    const float longEx = std::max(0.0f, std::max(-along, along - segL));
                    if (longEx > kEndcapM) continue;
                    const float lat = std::abs(-dp.x * dir.z + dp.z * dir.x);
                    if (lat > effHw) continue;

                    const float score2 = lat * lat + (longEx * 2.5f) * (longEx * 2.5f);
                    if (score2 < bestScore2) {
                        bestScore2 = score2;
                        bestLat = lat;
                        bestLong = longEx;
                        bestS = s;
                        bestT = clampf(along * invL, 0.0f, 1.0f);
                        bestQ = a + ab * bestT;
                        bestTang = dir;
                    }
                }
                if (bestScore2 >= 1e29f) continue;

                const float baseDeckY = has3DRoads_
                    ? lerpf(roadVertY_[r.firstVert + bestS], roadVertY_[r.firstVert + bestS + 1], bestT)
                    : sampleDem(bestQ);
                // OCM6 bakes the exact unified 3D asphalt elevation directly into roadVertY_ (zero double-lift!).
                const float deckY = isOcm6_ ? baseDeckY : (baseDeckY + roadClassLift(r.roadClass));
                if (hasRefY && deckY > refY + 2.4f) {
                    continue;
                }

                if (candCount < 24) {
                    cands[candCount++] = RoadCand{
                        ri, deckY, bestLat, bestLong, baseHw, effHw,
                        r.roadClass, r.layer,
                        (r.flags & 2u) != 0u || (r.layer >= 1) || ((r.flags2 & 4u) != 0u),
                        (r.flags2 & 1u) != 0u,
                        bestTang
                    };
                }
            }
        }
    }

    if (candCount == 0) return best;

    // Step 1: Select primary physical vertical level without snapping onto adjacent elevated ramps!
    int primIdx = 0;
    if (hasRefY) {
        float bestLevelScore = 1e30f;
        for (int i = 0; i < candCount; ++i) {
            const RoadCand& c = cands[i];
            const float dy = std::abs(c.deckY - refY);
            const float outPen = std::max(0.0f, c.latDist - c.hw) * 2.8f + c.longExcess * 1.8f;
            const float score = dy + outPen + (c.latDist / c.effHw) * 0.12f;
            if (score < bestLevelScore) {
                bestLevelScore = score;
                primIdx = i;
            }
        }
    } else if (overWater) {
        for (int i = 1; i < candCount; ++i) {
            if (cands[i].deckY > cands[primIdx].deckY + 0.35f ||
                (std::abs(cands[i].deckY - cands[primIdx].deckY) <= 0.35f &&
                 (cands[i].latDist / cands[i].effHw) < (cands[primIdx].latDist / cands[primIdx].effHw))) {
                primIdx = i;
            }
        }
    } else {
        for (int i = 1; i < candCount; ++i) {
            const RoadCand& c = cands[i];
            const RoadCand& cur = cands[primIdx];
            if (c.deckY < cur.deckY - 1.25f) {
                primIdx = i;
            } else if (std::abs(c.deckY - cur.deckY) <= 1.25f) {
                const bool cIn = (c.latDist <= c.hw && c.longExcess <= 0.15f);
                const bool curIn = (cur.latDist <= cur.hw && cur.longExcess <= 0.15f);
                if (cIn != curIn) {
                    if (cIn) primIdx = i;
                } else if ((c.latDist / c.effHw) < (cur.latDist / cur.effHw)) {
                    primIdx = i;
                }
            }
        }
    }

    // Step 2: C1 Smoothstep lateral + longitudinal + vertical bell blending across same-level overlaps
    // (ZERO hard cutoff steps anywhere in Seoul!)
    auto smooth01 = [](float t) -> float {
        const float u = clampf(t, 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    };
    const RoadCand& prim = cands[primIdx];
    float sumW = 0.0f;
    float sumWY = 0.0f;
    float maxShoulderCov = 0.0f;
    int bestMetaIdx = primIdx;
    float bestMetaWeight = -1.0f;

    for (int i = 0; i < candCount; ++i) {
        const RoadCand& c = cands[i];
        const float dv = std::abs(c.deckY - prim.deckY);
        if (dv >= 0.85f) continue;

        const float wV = smooth01((0.85f - dv) / 0.67f);
        const float latSpan = std::max(0.35f, c.effHw - c.hw * 0.45f);
        const float wLat = smooth01((c.effHw - c.latDist) / latSpan);
        const float wLon = smooth01((kEndcapM - c.longExcess) / kEndcapM);
        const float clsBoost = (c.roadClass <= 2u) ? 1.45f : ((c.roadClass <= 3u) ? 1.20f : 1.0f);
        const float bw = std::max(1e-5f, wV * wLat * wLon) * clsBoost;
        sumW += bw;
        sumWY += bw * c.deckY;

        const float shLat = (extraMargin > 0.05f)
            ? clampf((c.effHw - c.latDist) / extraMargin, 0.0f, 1.0f)
            : ((c.latDist <= c.hw) ? 1.0f : 0.0f);
        const float shLon = clampf((kEndcapM - c.longExcess) / kEndcapM, 0.0f, 1.0f);
        maxShoulderCov = std::max(maxShoulderCov, std::min(shLat, shLon));

        if (bw > bestMetaWeight) {
            bestMetaWeight = bw;
            bestMetaIdx = i;
        }
    }

    float finalY = (sumW > 1e-6f) ? (sumWY / sumW) : prim.deckY;
    const RoadCand& m = cands[bestMetaIdx];
    if (!m.isBridge && !overWater && maxShoulderCov < 1.0f && std::abs(finalY - demY) < 1.15f) {
        finalY = lerpf(demY, finalY, smooth01(maxShoulderCov));
    }

    best.found = true;
    best.y = finalY;
    best.roadIdx = m.roadIdx;
    best.roadClass = m.roadClass;
    best.layer = m.layer;
    best.isBridge = m.isBridge;
    best.isLink = m.isLink;
    best.halfWidth = m.hw;
    best.tangent = m.tangent;
    return best;
}

NearestOsmRoad RealSeoulData::findNearestRoad(const Vec2& p, float maxSearchDist, float refY) const {
    NearestOsmRoad res{};
    if (!loaded_ || !isInCoreBounds(p)) return res;

    const bool hasRefY = has3DRoads_ && (refY > -1e8f);
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - maxSearchDist - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + maxSearchDist - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - maxSearchDist - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + maxSearchDist - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);

    const float maxDistSq = maxSearchDist * maxSearchDist;
    float bestScore = 1e30f;
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const std::uint32_t ri = flatChunkRoads_[ch.roadStart + k];
                const OsmRoadRec& r = roads_[ri];
                if (r.vertCount < 2 || (r.flags & 4u) != 0u) continue;
                if (p.x < r.minX - r.halfWidth - maxSearchDist || p.x > r.maxX + r.halfWidth + maxSearchDist ||
                    p.z < r.minZ - r.halfWidth - maxSearchDist || p.z > r.maxZ + r.halfWidth + maxSearchDist) {
                    continue;
                }
                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 ab = b - a;
                    const float l2 = ab.lengthSq();
                    const float t = (l2 > 1e-6f) ? clampf((p - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
                    const Vec2 q = a + ab * t;
                    const float d2 = (p - q).lengthSq();
                    if (d2 >= maxDistSq) continue;

                    const float baseDeckY = has3DRoads_
                        ? lerpf(roadVertY_[r.firstVert + s], roadVertY_[r.firstVert + s + 1], t)
                        : sampleDem(q);
                    const float deckY = isOcm6_ ? baseDeckY : (baseDeckY + roadClassLift(r.roadClass));
                    float score = d2;
                    if (hasRefY) {
                        const float dy = std::abs(deckY - refY);
                        score += (dy * 3.5f) * (dy * 3.5f);
                    }
                    if (score < bestScore) {
                        bestScore = score;
                        res.found = true;
                        res.roadIdx = ri;
                        res.segIdx = s;
                        res.dist = std::sqrt(d2);
                        res.halfWidth = r.halfWidth;
                        res.deckY = deckY;
                        res.roadClass = r.roadClass;
                        res.layer = r.layer;
                        res.isBridge = (r.flags & 2u) != 0u || (r.layer >= 1) || ((r.flags2 & 4u) != 0u);
                        res.isOneway = (r.flags & 1u) != 0u;
                        res.isLink = (r.flags2 & 1u) != 0u;
                        res.closestPt = q;
                        res.tangent = (l2 > 1e-6f) ? (ab / std::sqrt(l2)) : Vec2{1.0f, 0.0f};
                    }
                }
            }
        }
    }
    return res;
}

float RealSeoulData::roadSurfaceLiftAt(const Vec2& p) const {
    if (!loaded_ || !isInCoreBounds(p)) return 0.0f;
    const float searchR = 18.5f;
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    float bestLift = 0.0f;
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const OsmRoadRec& r = roads_[flatChunkRoads_[ch.roadStart + k]];
                if ((r.flags & 4u) != 0u || r.layer >= 1) continue;
                const float lift = roadClassLift(r.roadClass);
                if (lift <= bestLift) continue;
                const float hw = std::max(2.6f, r.halfWidth) + 0.25f;
                if (p.x < r.minX - hw || p.x > r.maxX + hw ||
                    p.z < r.minZ - hw || p.z > r.maxZ + hw) {
                    continue;
                }
                const float hw2 = hw * hw;
                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 q = closestPointOnSeg(p, a, b);
                    if ((p - q).lengthSq() <= hw2) {
                        bestLift = lift;
                        break;
                    }
                }
            }
        }
    }
    return bestLift;
}

bool RealSeoulData::isOnRoad(const Vec2& p, float extraMargin) const {
    if (!loaded_ || !isInCoreBounds(p)) return false;
    const float searchR = std::max(18.0f, 18.0f + extraMargin);
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const OsmRoadRec& r = roads_[flatChunkRoads_[ch.roadStart + k]];
                if ((r.flags & 4u) != 0u) continue;
                const float hw = std::max(2.6f, r.halfWidth) + extraMargin;
                if (hw <= 0.0f) continue;
                if (p.x < r.minX - hw || p.x > r.maxX + hw ||
                    p.z < r.minZ - hw || p.z > r.maxZ + hw) {
                    continue;
                }
                const float hw2 = hw * hw;
                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 q = closestPointOnSeg(p, a, b);
                    if ((p - q).lengthSq() <= hw2) return true;
                }
            }
        }
    }
    return false;
}

bool RealSeoulData::isOnLowerRoadUnder(const Vec2& p, float deckY, float extraMargin) const {
    if (!loaded_ || !isInCoreBounds(p)) return false;
    const float searchR = std::max(18.0f, 18.0f + extraMargin);
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const OsmRoadRec& r = roads_[flatChunkRoads_[ch.roadStart + k]];
                if (r.vertCount < 2 || (r.flags & 4u) != 0u) continue;
                const float hw = std::max(2.6f, r.halfWidth) + extraMargin;
                if (p.x < r.minX - hw || p.x > r.maxX + hw ||
                    p.z < r.minZ - hw || p.z > r.maxZ + hw) {
                    continue;
                }
                const float hw2 = hw * hw;
                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 ab = b - a;
                    const float l2 = ab.lengthSq();
                    const float t = (l2 > 1e-6f) ? clampf((p - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
                    const Vec2 q = a + ab * t;
                    if ((p - q).lengthSq() > hw2) continue;
                    const float ry = has3DRoads_
                        ? lerpf(roadVertY_[r.firstVert + s], roadVertY_[r.firstVert + s + 1], t)
                        : sampleDem(q);
                    if (ry < deckY - 2.2f) return true;
                }
            }
        }
    }
    return false;
}

bool RealSeoulData::isOnBridge(const Vec2& p, float extraMargin) const {
    if (!loaded_ || !isInCoreBounds(p)) return false;
    const float searchR = std::max(18.0f, 18.0f + extraMargin);
    const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cx1 = std::clamp(static_cast<int>(std::floor((p.x + searchR - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const int cz1 = std::clamp(static_cast<int>(std::floor((p.z + searchR - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    for (int cz = cz0; cz <= cz1; ++cz) {
        const std::size_t row = static_cast<std::size_t>(cz) * hdr_.chunksX;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const OcChunkHdr& ch = chunks_[row + static_cast<std::size_t>(cx)];
            for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
                const OsmRoadRec& r = roads_[flatChunkRoads_[ch.roadStart + k]];
                if ((r.flags & 2u) == 0u && r.layer < 1 && (r.flags2 & 4u) == 0u) continue;
                const float hw = std::max(2.6f, r.halfWidth) + extraMargin;
                if (p.x < r.minX - hw || p.x > r.maxX + hw || p.z < r.minZ - hw || p.z > r.maxZ + hw) continue;
                const float hw2 = hw * hw;
                for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
                    const Vec2 a = roadVerts_[r.firstVert + s];
                    const Vec2 b = roadVerts_[r.firstVert + s + 1];
                    const Vec2 q = closestPointOnSeg(p, a, b);
                    if ((p - q).lengthSq() <= hw2) return true;
                }
            }
        }
    }
    return false;
}

bool RealSeoulData::isInWater(const Vec2& p) const {
    if (!loaded_ || !isInCoreBounds(p)) return false;
    const int gx = std::clamp(static_cast<int>((p.x - hdr_.minX) / (hdr_.maxX - hdr_.minX) * kWaterGridW), 0, kWaterGridW - 1);
    const int gz = std::clamp(static_cast<int>((p.z - hdr_.minZ) / (hdr_.maxZ - hdr_.minZ) * kWaterGridH), 0, kWaterGridH - 1);
    const auto& cell = waterGrid_[static_cast<std::size_t>(gz * kWaterGridW + gx)];
    for (std::uint32_t ti : cell) {
        if (pointInTri2D(p, waterTris_[ti])) return true;
    }
    return false;
}

bool RealSeoulData::findConnectingRoadAt(const Vec2& junctionPt, std::uint32_t excludeRoadIdx,
                                         std::uint32_t& outRoadIdx, std::uint32_t& outVertIdx, int& outDir,
                                         float curY) const {
    if (!loaded_ || !isInCoreBounds(junctionPt)) return false;
    const bool checkY = has3DRoads_ && (curY > -1e8f);
    const int cx = std::clamp(static_cast<int>(std::floor((junctionPt.x - hdr_.minX) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksX) - 1);
    const int cz = std::clamp(static_cast<int>(std::floor((junctionPt.z - hdr_.minZ) / hdr_.chunkSize)), 0, static_cast<int>(hdr_.chunksZ) - 1);
    const OcChunkHdr& ch = chunks_[static_cast<std::size_t>(cz) * hdr_.chunksX + static_cast<std::size_t>(cx)];

    float bestD2 = 14.0f * 14.0f;
    bool found = false;
    for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
        const std::uint32_t ri = flatChunkRoads_[ch.roadStart + k];
        if (ri == excludeRoadIdx) continue;
        const OsmRoadRec& r = roads_[ri];
        if (r.roadClass > 4 || (r.flags & 4u) != 0u) continue; // Keep traffic smoothly on drivable arterial/collector roads!
        if (junctionPt.x < r.minX - 15.0f || junctionPt.x > r.maxX + 15.0f ||
            junctionPt.z < r.minZ - 15.0f || junctionPt.z > r.maxZ + 15.0f) continue;
        for (std::uint16_t vi = 0; vi < r.vertCount; ++vi) {
            if (checkY && std::abs(roadVertY_[r.firstVert + vi] - curY) > 2.8f) continue;
            const float d2 = (roadVerts_[r.firstVert + vi] - junctionPt).lengthSq();
            if (d2 < bestD2) {
                bestD2 = d2;
                outRoadIdx = ri;
                outVertIdx = vi;
                if (vi == 0) outDir = 1;
                else if (vi + 1 >= r.vertCount) outDir = ((r.flags & 1u) != 0u) ? 1 : -1;
                else outDir = ((r.flags & 1u) != 0u) ? 1 : ((ri & 1u) ? 1 : -1);
                found = true;
            }
        }
    }
    return found;
}

} // namespace oc
