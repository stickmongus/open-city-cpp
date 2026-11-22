#include "world/OcrmRoadMesh.hpp"

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

} // namespace oc
