#include "world/OcrmRoadMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace oc {

bool OcrmRoadMesh::decompressLz4Block(const std::uint8_t* src, std::size_t srcSize,
                                      std::uint8_t* dst, std::size_t dstSize) {
    if (!src || !dst) return false;
    std::size_t ip = 0;
    std::size_t op = 0;

    while (ip < srcSize) {
        const std::uint8_t token = src[ip++];
        std::size_t litLen = static_cast<std::size_t>(token >> 4);
        if (litLen == 15u) {
            std::uint8_t s = 255u;
            while (s == 255u) {
                if (ip >= srcSize) return false;
                s = src[ip++];
                litLen += s;
                if (litLen > dstSize) return false;
            }
        }
        if (ip + litLen > srcSize || op + litLen > dstSize) return false;
        if (litLen > 0u) {
            std::memcpy(dst + op, src + ip, litLen);
            ip += litLen;
            op += litLen;
        }
        if (ip >= srcSize) break;

        if (ip + 2u > srcSize) return false;
        const std::uint32_t offset = static_cast<std::uint32_t>(src[ip]) |
                                     (static_cast<std::uint32_t>(src[ip + 1u]) << 8);
        ip += 2u;
        if (offset == 0u || offset > op) return false;

        std::size_t matchLen = static_cast<std::size_t>(token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            std::uint8_t s = 255u;
            while (s == 255u) {
                if (ip >= srcSize) return false;
                s = src[ip++];
                matchLen += s;
                if (matchLen > dstSize) return false;
            }
        }
        if (op + matchLen > dstSize) return false;
        std::size_t ref = op - offset;
        for (std::size_t i = 0; i < matchLen; ++i) {
            dst[op++] = dst[ref++];
        }
    }
    return op == dstSize;
}

void OcrmRoadMesh::clear() {
    loaded_ = false;
    hdr_ = {};
    activeChunkCount_ = 0;
    directory_.clear();
    rawFile_.clear();
    surfaceCache_.clear();
    cacheClock_ = 0;
}

bool OcrmRoadMesh::loadFromBytes(const std::uint8_t* data, std::size_t size) {
    clear();
    if (!data || size < sizeof(OcrmHeader)) return false;

    OcrmHeader h{};
    std::memcpy(&h, data, sizeof(OcrmHeader));
    if (std::memcmp(h.magic, "OCRM", 4) != 0 || h.version != 1u) return false;
    if (h.chunksX == 0u || h.chunksZ == 0u || h.chunksX > 512u || h.chunksZ > 512u || h.chunkSize <= 1.0f) {
        return false;
    }

    const std::size_t nChunks = static_cast<std::size_t>(h.chunksX) * static_cast<std::size_t>(h.chunksZ);
    const std::size_t dirBytes = nChunks * sizeof(OcrmChunkEntry);
    if (sizeof(OcrmHeader) + dirBytes > size) return false;

    std::vector<OcrmChunkEntry> dir(nChunks);
    std::memcpy(dir.data(), data + sizeof(OcrmHeader), dirBytes);

    std::size_t active = 0;
    for (const auto& e : dir) {
        if (e.compSize == 0u || e.uncompSize == 0u) continue;
        if (static_cast<std::size_t>(e.offset) + static_cast<std::size_t>(e.compSize) > size) {
            return false;
        }
        if (e.uncompSize < sizeof(OcrmChunkBlobHeader) || e.uncompSize > 32u * 1024u * 1024u) {
            return false;
        }
        ++active;
    }

    hdr_ = h;
    activeChunkCount_ = active;
    directory_ = std::move(dir);
    rawFile_.assign(data, data + size);
    loaded_ = true;
    return true;
}

bool OcrmRoadMesh::loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff len = f.tellg();
    if (len <= 0) return false;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(buf.data()), len)) return false;
    return loadFromBytes(buf.data(), buf.size());
}

bool OcrmRoadMesh::hasChunk(int cx, int cz) const {
    if (!loaded_ || cx < 0 || cz < 0 ||
        cx >= static_cast<int>(hdr_.chunksX) || cz >= static_cast<int>(hdr_.chunksZ)) {
        return false;
    }
    const OcrmChunkEntry& e = directory_[static_cast<std::size_t>(cz) * hdr_.chunksX + static_cast<std::size_t>(cx)];
    return e.offset > 0u && e.compSize > 0u && e.uncompSize >= sizeof(OcrmChunkBlobHeader);
}

bool OcrmRoadMesh::decodeChunk(int cx, int cz, OcrmDecodedChunk& out) const {
    out.verts.clear();
    out.idxUrban.clear();
    out.idxHighway.clear();
    out.idxInter.clear();
    out.idxSidewalk.clear();
    out.idxMarking.clear();
    out.idxBridge.clear();

    if (!hasChunk(cx, cz)) return false;
    const OcrmChunkEntry& e = directory_[static_cast<std::size_t>(cz) * hdr_.chunksX + static_cast<std::size_t>(cx)];
    if (static_cast<std::size_t>(e.offset) + static_cast<std::size_t>(e.compSize) > rawFile_.size()) {
        return false;
    }

    std::vector<std::uint8_t> decomp;
    const std::uint8_t* payload = nullptr;
    std::size_t payloadSize = 0;
    if ((hdr_.flags & 1u) != 0u) {
        decomp.resize(e.uncompSize);
        if (!decompressLz4Block(rawFile_.data() + e.offset, e.compSize, decomp.data(), decomp.size())) {
            return false;
        }
        payload = decomp.data();
        payloadSize = decomp.size();
    } else {
        if (e.compSize != e.uncompSize) return false;
        payload = rawFile_.data() + e.offset;
        payloadSize = e.uncompSize;
    }

    if (payloadSize < sizeof(OcrmChunkBlobHeader)) return false;
    OcrmChunkBlobHeader bh{};
    std::memcpy(&bh, payload, sizeof(OcrmChunkBlobHeader));

    const std::size_t totalIdx = static_cast<std::size_t>(bh.nIdxUrban) +
                                 static_cast<std::size_t>(bh.nIdxHighway) +
                                 static_cast<std::size_t>(bh.nIdxInter) +
                                 static_cast<std::size_t>(bh.nIdxSidewalk) +
                                 static_cast<std::size_t>(bh.nIdxMarking) +
                                 static_cast<std::size_t>(bh.nIdxBridge);
    const std::size_t vertBytes = static_cast<std::size_t>(bh.numVerts) * sizeof(OcrmPackedVertex);
    const std::size_t idxBytes = totalIdx * sizeof(std::uint16_t);
    if (sizeof(OcrmChunkBlobHeader) + vertBytes + idxBytes > payloadSize) {
        return false;
    }

    const float centerX = hdr_.minX + (static_cast<float>(cx) + 0.5f) * hdr_.chunkSize;
    const float centerZ = hdr_.minZ + (static_cast<float>(cz) + 0.5f) * hdr_.chunkSize;
    const bool isSoaDelta = (hdr_.flags & 4u) != 0u;

    out.verts.resize(bh.numVerts);
    const std::uint8_t* vPtr = payload + sizeof(OcrmChunkBlobHeader);
    const std::size_t nv = static_cast<std::size_t>(bh.numVerts);

    if (isSoaDelta) {
        const std::uint8_t* pQx = vPtr;
        const std::uint8_t* pQy = pQx + nv * 2u;
        const std::uint8_t* pQz = pQy + nv * 2u;
        const std::uint8_t* pUq = pQz + nv * 2u;
        const std::uint8_t* pNx = pUq + nv * 2u;
        const std::uint8_t* pNy = pNx + nv;
        const std::uint8_t* pNz = pNy + nv;
        const std::uint8_t* pFl = pNz + nv;
        const std::uint8_t* pR  = pFl + nv;
        const std::uint8_t* pG  = pR  + nv;
        const std::uint8_t* pB  = pG  + nv;
        const std::uint8_t* pA  = pB  + nv;

        std::uint16_t accX = 0u, accY = 0u, accZ = 0u, accU = 0u;
        for (std::size_t i = 0; i < nv; ++i) {
            std::uint16_t dx = 0u, dy = 0u, dz = 0u, du = 0u;
            std::memcpy(&dx, pQx + i * 2u, 2u);
            std::memcpy(&dy, pQy + i * 2u, 2u);
            std::memcpy(&dz, pQz + i * 2u, 2u);
            std::memcpy(&du, pUq + i * 2u, 2u);
            accX = static_cast<std::uint16_t>(accX + dx);
            accY = static_cast<std::uint16_t>(accY + dy);
            accZ = static_cast<std::uint16_t>(accZ + dz);
            accU = static_cast<std::uint16_t>(accU + du);

            const std::int16_t qx = static_cast<std::int16_t>(accX);
            const std::int16_t qy = static_cast<std::int16_t>(accY);
            const std::int16_t qz = static_cast<std::int16_t>(accZ);
            const std::int16_t uq = static_cast<std::int16_t>(accU);

            float nx = static_cast<float>(static_cast<std::int8_t>(pNx[i])) / 127.0f;
            float ny = static_cast<float>(static_cast<std::int8_t>(pNy[i])) / 127.0f;
            float nz = static_cast<float>(static_cast<std::int8_t>(pNz[i])) / 127.0f;
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl > 1e-4f) {
                nx /= nl;
                ny /= nl;
                nz /= nl;
            } else {
                nx = 0.0f;
                ny = 1.0f;
                nz = 0.0f;
            }
            out.verts[i] = OcrmDecodedVertex{
                centerX + static_cast<float>(qx) * 0.005f,
                static_cast<float>(qy) * 0.01f,
                centerZ + static_cast<float>(qz) * 0.005f,
                nx, ny, nz,
                static_cast<float>(uq),
                pFl[i],
                pR[i], pG[i], pB[i], pA[i]
            };
        }
    } else {
        for (std::size_t i = 0; i < nv; ++i) {
            OcrmPackedVertex pv{};
            std::memcpy(&pv, vPtr + i * sizeof(OcrmPackedVertex), sizeof(OcrmPackedVertex));
            float nx = static_cast<float>(pv.nx) / 127.0f;
            float ny = static_cast<float>(pv.ny) / 127.0f;
            float nz = static_cast<float>(pv.nz) / 127.0f;
            const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl > 1e-4f) {
                nx /= nl;
                ny /= nl;
                nz /= nl;
            } else {
                nx = 0.0f;
                ny = 1.0f;
                nz = 0.0f;
            }
            out.verts[i] = OcrmDecodedVertex{
                centerX + static_cast<float>(pv.qx) * 0.005f,
                static_cast<float>(pv.qy) * 0.01f,
                centerZ + static_cast<float>(pv.qz) * 0.005f,
                nx, ny, nz,
                static_cast<float>(pv.uq),
                pv.flags,
                pv.r, pv.g, pv.b, pv.a
            };
        }
    }

    const std::uint8_t* iPtr = vPtr + vertBytes;
    std::uint16_t idxAcc = 0u;
    auto readIdx = [&](std::uint32_t count, std::vector<std::uint16_t>& dst) -> bool {
        dst.resize(count);
        if (count > 0u) {
            std::memcpy(dst.data(), iPtr, static_cast<std::size_t>(count) * sizeof(std::uint16_t));
            iPtr += static_cast<std::size_t>(count) * sizeof(std::uint16_t);
            if (isSoaDelta) {
                for (std::uint16_t& val : dst) {
                    idxAcc = static_cast<std::uint16_t>(idxAcc + val);
                    val = idxAcc;
                    if (val >= bh.numVerts) return false;
                }
            } else {
                for (std::uint16_t idx : dst) {
                    if (idx >= bh.numVerts) return false;
                }
            }
        }
        return true;
    };

    if (!readIdx(bh.nIdxUrban, out.idxUrban) ||
        !readIdx(bh.nIdxHighway, out.idxHighway) ||
        !readIdx(bh.nIdxInter, out.idxInter) ||
        !readIdx(bh.nIdxSidewalk, out.idxSidewalk) ||
        !readIdx(bh.nIdxMarking, out.idxMarking) ||
        !readIdx(bh.nIdxBridge, out.idxBridge)) {
        out = {};
        return false;
    }

    return true;
}

const OcrmRoadMesh::CachedSurfaceChunk* OcrmRoadMesh::getOrBuildSurfaceChunk(int cx, int cz) const {
    if (!hasChunk(cx, cz)) return nullptr;
    ++cacheClock_;
    for (auto& entry : surfaceCache_) {
        if (entry.cx == cx && entry.cz == cz) {
            entry.lastUse = cacheClock_;
            return &entry;
        }
    }

    OcrmDecodedChunk decoded{};
    if (!decodeChunk(cx, cz, decoded)) return nullptr;

    constexpr std::size_t kMaxCache = 16;
    if (surfaceCache_.capacity() < kMaxCache) surfaceCache_.reserve(kMaxCache);
    CachedSurfaceChunk* slot = nullptr;
    if (surfaceCache_.size() < kMaxCache) {
        surfaceCache_.emplace_back();
        slot = &surfaceCache_.back();
    } else {
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < surfaceCache_.size(); ++i) {
            if (surfaceCache_[i].lastUse < surfaceCache_[oldest].lastUse) oldest = i;
        }
        slot = &surfaceCache_[oldest];
    }

    slot->cx = cx;
    slot->cz = cz;
    slot->lastUse = cacheClock_;
    constexpr float kOverhangM = 12.0f;
    const float span = hdr_.chunkSize + 2.0f * kOverhangM;
    slot->minX = hdr_.minX + static_cast<float>(cx) * hdr_.chunkSize - kOverhangM;
    slot->minZ = hdr_.minZ + static_cast<float>(cz) * hdr_.chunkSize - kOverhangM;
    slot->invCellSize = static_cast<float>(CachedSurfaceChunk::kGridN) / std::max(1.0f, span);
    slot->tris.clear();
    slot->cells.assign(static_cast<std::size_t>(CachedSurfaceChunk::kGridN * CachedSurfaceChunk::kGridN), {});

    const std::size_t estTris = (decoded.idxUrban.size() + decoded.idxHighway.size() +
                                 decoded.idxInter.size() + decoded.idxSidewalk.size()) / 3u;
    slot->tris.reserve(estTris);

    auto ingestList = [&](const std::vector<std::uint16_t>& idxList, bool isSidewalk) {
        for (std::size_t k = 0; k + 2 < idxList.size(); k += 3) {
            const OcrmDecodedVertex& v0 = decoded.verts[idxList[k]];
            const OcrmDecodedVertex& v1 = decoded.verts[idxList[k + 1]];
            const OcrmDecodedVertex& v2 = decoded.verts[idxList[k + 2]];

            // Reject vertical curb risers / parapet walls (only walkable & drivable horizontal surfaces!)
            const float ax = v1.px - v0.px, ay = v1.py - v0.py, az = v1.pz - v0.pz;
            const float bx = v2.px - v0.px, by = v2.py - v0.py, bz = v2.pz - v0.pz;
            const float nx = ay * bz - az * by;
            const float ny = az * bx - ax * bz;
            const float nz = ax * by - ay * bx;
            const float lenSq = nx * nx + ny * ny + nz * nz;
            if (lenSq < 1e-6f) continue;
            if ((ny * ny) < 0.25f * lenSq) continue; // |normal.y| < 0.5 -> vertical curb wall!

            const std::uint32_t ti = static_cast<std::uint32_t>(slot->tris.size());
            slot->tris.push_back(SurfTri{
                v0.px, v0.py, v0.pz,
                v1.px, v1.py, v1.pz,
                v2.px, v2.py, v2.pz,
                isSidewalk
            });

            const float tMinX = std::min({v0.px, v1.px, v2.px});
            const float tMaxX = std::max({v0.px, v1.px, v2.px});
            const float tMinZ = std::min({v0.pz, v1.pz, v2.pz});
            const float tMaxZ = std::max({v0.pz, v1.pz, v2.pz});
            const int g0x = std::clamp(static_cast<int>(std::floor((tMinX - slot->minX) * slot->invCellSize)),
                                       0, CachedSurfaceChunk::kGridN - 1);
            const int g1x = std::clamp(static_cast<int>(std::floor((tMaxX - slot->minX) * slot->invCellSize)),
                                       0, CachedSurfaceChunk::kGridN - 1);
            const int g0z = std::clamp(static_cast<int>(std::floor((tMinZ - slot->minZ) * slot->invCellSize)),
                                       0, CachedSurfaceChunk::kGridN - 1);
            const int g1z = std::clamp(static_cast<int>(std::floor((tMaxZ - slot->minZ) * slot->invCellSize)),
                                       0, CachedSurfaceChunk::kGridN - 1);
            for (int gz = g0z; gz <= g1z; ++gz) {
                for (int gx = g0x; gx <= g1x; ++gx) {
                    slot->cells[static_cast<std::size_t>(gz * CachedSurfaceChunk::kGridN + gx)].push_back(ti);
                }
            }
        }
    };

    ingestList(decoded.idxUrban, false);
    ingestList(decoded.idxHighway, false);
    ingestList(decoded.idxInter, false);
    ingestList(decoded.idxSidewalk, true);
    return slot;
}

bool OcrmRoadMesh::sampleSurfaceHeightAt(const Vec2& p, float refY, float& outY,
                                         bool* outIsSidewalk) const {
    if (!loaded_ || hdr_.chunkSize <= 1.0f) return false;
    const float fx = (p.x - hdr_.minX) / hdr_.chunkSize;
    const float fz = (p.z - hdr_.minZ) / hdr_.chunkSize;
    const int baseCx = static_cast<int>(std::floor(fx));
    const int baseCz = static_cast<int>(std::floor(fz));
    if (baseCx < -1 || baseCz < -1 ||
        baseCx > static_cast<int>(hdr_.chunksX) || baseCz > static_cast<int>(hdr_.chunksZ)) {
        return false;
    }

    // Also probe immediate neighbor chunk(s) when within 8m of a chunk boundary so overhanging
    // border triangles stay 100% seamless across every chunk seam!
    const float localX = (fx - static_cast<float>(baseCx)) * hdr_.chunkSize;
    const float localZ = (fz - static_cast<float>(baseCz)) * hdr_.chunkSize;
    int cxList[2] = {baseCx, -999};
    int czList[2] = {baseCz, -999};
    int nCx = 1, nCz = 1;
    if (localX < 8.0f) cxList[nCx++] = baseCx - 1;
    else if (localX > hdr_.chunkSize - 8.0f) cxList[nCx++] = baseCx + 1;
    if (localZ < 8.0f) czList[nCz++] = baseCz - 1;
    else if (localZ > hdr_.chunkSize - 8.0f) czList[nCz++] = baseCz + 1;

    struct HitCand {
        float y;
        bool isSidewalk;
    };
    HitCand hits[16];
    int nHits = 0;

    for (int iz = 0; iz < nCz; ++iz) {
        const int cz = czList[iz];
        if (cz < 0 || cz >= static_cast<int>(hdr_.chunksZ)) continue;
        for (int ix = 0; ix < nCx; ++ix) {
            const int cx = cxList[ix];
            if (cx < 0 || cx >= static_cast<int>(hdr_.chunksX)) continue;
            const CachedSurfaceChunk* sc = getOrBuildSurfaceChunk(cx, cz);
            if (!sc || sc->tris.empty()) continue;

            const int gx = static_cast<int>(std::floor((p.x - sc->minX) * sc->invCellSize));
            const int gz = static_cast<int>(std::floor((p.z - sc->minZ) * sc->invCellSize));
            if (gx < 0 || gx >= CachedSurfaceChunk::kGridN || gz < 0 || gz >= CachedSurfaceChunk::kGridN) continue;

            const auto& cell = sc->cells[static_cast<std::size_t>(gz * CachedSurfaceChunk::kGridN + gx)];
            for (std::uint32_t ti : cell) {
                const SurfTri& t = sc->tris[ti];
                const float v0x = t.x1 - t.x0, v0z = t.z1 - t.z0;
                const float v1x = t.x2 - t.x0, v1z = t.z2 - t.z0;
                const float denom = v0x * v1z - v0z * v1x;
                if (std::abs(denom) < 1e-6f) continue;
                const float invD = 1.0f / denom;
                const float px = p.x - t.x0, pz = p.z - t.z0;
                const float u = (px * v1z - pz * v1x) * invD;
                const float v = (v0x * pz - v0z * px) * invD;
                const float w = 1.0f - u - v;
                if (u >= -1e-3f && v >= -1e-3f && w >= -1e-3f) {
                    const float y = w * t.y0 + u * t.y1 + v * t.y2;
                    if (nHits < 16) {
                        hits[nHits++] = {y, t.isSidewalk};
                    }
                }
            }
        }
    }

    if (nHits == 0) return false;

    int bestIdx = 0;
    if (refY > -1e7f) {
        // Multi-level aware selection: prefer highest surface within [-1.85m .. +1.35m] of refY
        // so raised sidewalks (+0.125m) NEVER let entities sink to road lip while bridges stay separated!
        int bestInWin = -1;
        for (int i = 0; i < nHits; ++i) {
            const float dy = hits[i].y - refY;
            if (dy >= -1.85f && dy <= 1.35f) {
                if (bestInWin < 0 || hits[i].y > hits[bestInWin].y) {
                    bestInWin = i;
                }
            }
        }
        if (bestInWin >= 0) {
            bestIdx = bestInWin;
        } else {
            float bestAbs = std::abs(hits[0].y - refY);
            for (int i = 1; i < nHits; ++i) {
                const float d = std::abs(hits[i].y - refY);
                if (d < bestAbs) {
                    bestAbs = d;
                    bestIdx = i;
                }
            }
        }
    } else {
        // Ground-level default query (spawns / street props): pick lowest level, then highest surface
        // within +0.65m of that lowest level (picks ground sidewalk over buried road lip, ignores +6m viaduct!).
        float minY = hits[0].y;
        for (int i = 1; i < nHits; ++i) {
            if (hits[i].y < minY) minY = hits[i].y;
        }
        bestIdx = -1;
        for (int i = 0; i < nHits; ++i) {
            if (hits[i].y <= minY + 0.65f) {
                if (bestIdx < 0 || hits[i].y > hits[bestIdx].y) {
                    bestIdx = i;
                }
            }
        }
        if (bestIdx < 0) bestIdx = 0;
    }

    outY = hits[bestIdx].y;
    if (outIsSidewalk) *outIsSidewalk = hits[bestIdx].isSidewalk;
    return true;
}

} // namespace oc
