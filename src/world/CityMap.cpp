#include "world/CityMap.hpp"
#include <algorithm>
#include <cmath>

namespace oc {

namespace {

inline std::uint64_t hashChunkCoords(std::uint64_t seed, int cx, int cz) {
    std::uint64_t h = seed ^ 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(cx) * 0xBF58476D1CE4E5B9ULL);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(cz) * 0x94D049BB133111EBULL);
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    return h ^ (h >> 31);
}

} // namespace

CityMap::CityMap() {
    generate();
}

int CityMap::nearestRoadIndex(float coord) const {
    const int idx = static_cast<int>(std::round(coord / kBlockPitch));
    return std::clamp(idx, kMinRoadIndexX, kMaxRoadIndexX);
}

float CityMap::sidewalkCoord(int sidewalkNodeIndex) const {
    // Even index = roadIdx * 2 -> roadCoord(roadIdx) - kSidewalkCenterOffset
    // Odd  index = roadIdx * 2 + 1 -> roadCoord(roadIdx) + kSidewalkCenterOffset
    // Use floor-division by 2 so negative node indices map cleanly!
    const int roadIdx = (sidewalkNodeIndex >= 0) ? (sidewalkNodeIndex / 2) : ((sidewalkNodeIndex - 1) / 2);
    const bool positiveSide = ((sidewalkNodeIndex & 1) != 0);
    return roadCoord(roadIdx) + (positiveSide ? kSidewalkCenterOffset : -kSidewalkCenterOffset);
}

int CityMap::nearestSidewalkNodeIndex(float coord) const {
    const int roadIdx = nearestRoadIndex(coord);
    const float rc = roadCoord(roadIdx);
    const bool posSide = (coord >= rc);
    return roadIdx * 2 + (posSide ? 1 : 0);
}

bool CityMap::isCrosswalkEdge(int fromNode, int toNode) {
    if (fromNode == toNode) return false;
    const int rA = (fromNode >= 0) ? (fromNode / 2) : ((fromNode - 1) / 2);
    const int rB = (toNode >= 0) ? (toNode / 2) : ((toNode - 1) / 2);
    return rA == rB;
}

// ============================================================================
// Real Seoul Analytic Geography (30 km x 25 km, +X = East, -Z = North)
// ============================================================================
float CityMap::hanRiverCenterZ(float x) const {
    // Iconic real Seoul Han River S-curve:
    // - West (Gangseo/Gimpo x=-12000): z ~ -2350
    // - Mapo/Yeouido (x=-4800): z ~ +180
    // - Banpo/Ichon (x=+600): z ~ +160
    // - Apgujeong/Seongsu bend (x=+4800): curves north to z ~ -780
    // - Jamsil/Gwangjin (x=+9000): z ~ -450
    // - Gangdong/Gwangnaru (x=+13500): curves north to z ~ -1650
    const float u = x / 15000.0f;
    float z = 120.0f;
    z -= 1150.0f * u * u;
    z -= 680.0f * std::sin((x - 1600.0f) * 0.00046f);
    z += 280.0f * std::cos((x + 3200.0f) * 0.00062f);
    // Gentle northern turn in far west (Gimpo) & far east (Paldang)
    if (x < -7500.0f) {
        const float t = (-7500.0f - x) / 7500.0f;
        z -= 1850.0f * t * t;
    } else if (x > 9500.0f) {
        const float t = (x - 9500.0f) / 5500.0f;
        z -= 1250.0f * t * t;
    }
    return z;
}

float CityMap::hanRiverHalfWidth(float x) const {
    // Authentic ~900m..1250m wide Han River (widest around Yeouido Island split!)
    float hw = 470.0f + 55.0f * std::sin(x * 0.00055f);
    // Widens around Yeouido Island (-6900..-2700) to cradle the island & twin channels
    const float ydDist = std::abs(x + 4800.0f);
    if (ydDist < 2600.0f) {
        const float t = 1.0f - ydDist / 2600.0f;
        hw += 360.0f * (t * t * (3.0f - 2.0f * t));
    }
    return hw;
}

float CityMap::hanRiverSlopeDzDx(float x) const {
    return (hanRiverCenterZ(x + 20.0f) - hanRiverCenterZ(x - 20.0f)) / 40.0f;
}

bool CityMap::isOnYeouidoIsland(const Vec2& p) const {
    // Real ~4.0km x 1.15km tapered river island tilted ~14 deg WNW-to-ESE inside the Han River!
    const float cx = -4800.0f;
    const float cz = hanRiverCenterZ(cx) + 165.0f;
    const float dx = p.x - cx;
    const float dz = p.z - cz;
    const float c = 0.9703f; // cos(14 deg)
    const float s = 0.2419f; // sin(14 deg)
    const float lx =  dx * c + dz * s;
    const float lz = -dx * s + dz * c;
    const float nx = lx / 1940.0f;
    const float nz = lz / 545.0f;
    return (nx * nx + nz * nz) <= 1.0f;
}

bool CityMap::isInHanRiverWater(const Vec2& p, float margin) const {
    if (realData_.isLoaded() && realData_.isInCoreBounds(p)) {
        return realData_.isInWater(p);
    }
    if (isOnYeouidoIsland(p)) return false;
    const float cz = hanRiverCenterZ(p.x);
    const float hw = hanRiverHalfWidth(p.x) - margin;
    return std::abs(p.z - cz) < hw;
}

bool CityMap::isInHanRiverPark(const Vec2& p) const {
    if (isOnYeouidoIsland(p)) {
        // Yeouido Hangang Riverside Park along the northern edge of Yeouido
        const float cz = hanRiverCenterZ(-4800.0f) + 165.0f;
        return p.z < cz - 350.0f;
    }
    const float cz = hanRiverCenterZ(p.x);
    const float hw = hanRiverHalfWidth(p.x);
    const float d = std::abs(p.z - cz);
    return (d >= hw) && (d <= hw + 115.0f);
}

bool CityMap::isInSeokchonLake(const Vec2& p) const {
    // Iconic twin-basin Seokchon Lake right south of Lotte World Tower (x=8904, z=2240), split by Songpa-daero!
    const float dx = (p.x - 8904.0f) / 480.0f;
    const float dz = (p.z - 2240.0f) / 145.0f;
    return (dx * dx + dz * dz) < 1.0f && std::abs(p.x - 8904.0f) > 22.0f;
}

bool CityMap::isInCheonggyecheon(const Vec2& p) const {
    // Scenic downtown Cheonggyecheon stream running E-W from Gwanghwamun Plaza through Jongno/Dongdaemun to Jungnangcheon
    return (p.x > -920.0f && p.x < 4850.0f && std::abs(p.z - (-4536.0f)) < 11.5f);
}

namespace {
float distToSegmentSq(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const float abLenSq = ab.lengthSq();
    if (abLenSq < 1e-6f) return (p - a).lengthSq();
    const float t = clampf(((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / abLenSq, 0.0f, 1.0f);
    const Vec2 proj{a.x + ab.x * t, a.z + ab.z * t};
    return (p - proj).lengthSq();
}
} // namespace

bool CityMap::isInTributaryWater(const Vec2& p, float margin) const {
    for (const auto& trib : tributaries_) {
        const float hw = std::max(2.0f, trib.halfWidth - margin);
        const float hwSq = hw * hw;
        for (std::size_t i = 1; i < trib.points.size(); ++i) {
            if (distToSegmentSq(p, trib.points[i - 1], trib.points[i]) <= hwSq) {
                return true;
            }
        }
    }
    return false;
}

const SeoulParkZone* CityMap::findMajorSeoulPark(const Vec2& p) const {
    for (const auto& pk : majorParks_) {
        const float dx = p.x - pk.center.x;
        const float dz = p.z - pk.center.z;
        const float c = std::cos(-pk.yawRad);
        const float s = std::sin(-pk.yawRad);
        const float lx = dx * c - dz * s;
        const float lz = dx * s + dz * c;
        if (std::abs(lx) <= pk.halfSize.x && std::abs(lz) <= pk.halfSize.z) {
            return &pk;
        }
    }
    return nullptr;
}

bool CityMap::isOnRiversideExpressway(const Vec2& p) const {
    if (isOnYeouidoIsland(p)) return false;
    const float cz = hanRiverCenterZ(p.x);
    const float hw = hanRiverHalfWidth(p.x);
    // Gangbyeonbuk-ro (North bank: cz - hw - 126m) & Olympic-daero (South bank: cz + hw + 126m)
    const float northExpZ = cz - hw - 126.0f;
    const float southExpZ = cz + hw + 126.0f;
    return (std::abs(p.z - northExpZ) <= 11.5f) || (std::abs(p.z - southExpZ) <= 11.5f);
}

float CityMap::terrainElevationAt(const Vec2& p) const {
    if (realData_.isLoaded()) {
        return realData_.sampleDem(p);
    }
    // Flat inside Han River water & immediate riverside parks/islands
    if (isInHanRiverWater(p, -60.0f) || isOnYeouidoIsland(p)) {
        return 0.0f;
    }
    auto evalMountains = [&](const std::vector<SeoulMountain>& list, float curElev) {
        for (const auto& m : list) {
            const float nx = (p.x - m.center.x) / m.radiusX;
            const float nz = (p.z - m.center.z) / m.radiusZ;
            const float r2 = nx * nx + nz * nz;
            if (r2 < 1.0f) {
                const float u = 1.0f - r2;
                const float ridge = u * u * (0.86f + 0.14f * std::sin(p.x * 0.0045f + p.z * 0.0038f));
                curElev = std::max(curElev, m.peakHeight * ridge);
            }
        }
        return curElev;
    };
    float elev = evalMountains(mountains_, 0.0f);

    // Mask outer perimeter mountains cleanly outside the Ring Expressway retaining wall (|x|>14326 / |z|>11826)
    // so all 4 expressway borders, IC gantries, acoustic walls & pine rows stay flat & solidly grounded at y=0!
    const float outX = clampf((std::abs(p.x) - 14326.0f) / 160.0f, 0.0f, 1.0f);
    const float outZ = clampf((std::abs(p.z) - 11826.0f) / 160.0f, 0.0f, 1.0f);
    const float outRaw = std::max(outX, outZ);
    if (outRaw > 0.001f) {
        const float outMask = outRaw * outRaw * (3.0f - 2.0f * outRaw);
        const float perimElev = evalMountains(perimeterMountains_, 0.0f);
        elev = std::max(elev, perimElev * outMask);
    }

    // Smooth natural Seoul outer greenbelt basin rim right outside the outer ring expressway wall (|x|>14330 / |z|>11830)
    const float edgeX = std::max(0.0f, (std::abs(p.x) - 14330.0f) / 1150.0f);
    const float edgeZ = std::max(0.0f, (std::abs(p.z) - 11830.0f) / 1150.0f);
    const float rimT = clampf(std::max(edgeX, edgeZ), 0.0f, 1.8f);
    if (rimT > 0.001f) {
        const float wave = 0.76f + 0.24f * std::sin(p.x * 0.0016f + p.z * 0.0014f);
        elev = std::max(elev, rimT * rimT * 125.0f * wave);
    }

    // Keep major urban valley corridors & Han River valley smooth and easily drivable
    const float riverDist = std::abs(p.z - hanRiverCenterZ(p.x)) - hanRiverHalfWidth(p.x);
    if (riverDist < 260.0f) {
        const float t = clampf((riverDist - 90.0f) / 170.0f, 0.0f, 1.0f);
        elev *= (t * t * (3.0f - 2.0f * t));
    }
    return elev;
}

float CityMap::terrainSlopeAlong(const Vec2& p, const Vec2& dir) const {
    const Vec2 d = dir.normalized();
    const float h0 = terrainElevationAt(p - d * 1.5f);
    const float h1 = terrainElevationAt(p + d * 1.5f);
    return (h1 - h0) / 3.0f;
}

const SeoulBridge* CityMap::findBridgeAt(const Vec2& p, float margin) const {
    for (const auto& br : bridges_) {
        if (std::abs(p.x - br.centerX) <= (br.halfWidth + margin) &&
            p.z >= (br.zNorth - 42.0f) && p.z <= (br.zSouth + 42.0f)) {
            return &br;
        }
    }
    return nullptr;
}

bool CityMap::isOnBridge(const Vec2& p, float margin) const {
    if (realData_.isLoaded() && realData_.isInCoreBounds(p)) {
        return realData_.isOnBridge(p, margin);
    }
    return findBridgeAt(p, margin) != nullptr;
}

float CityMap::bridgeDeckElevationAt(const Vec2& p, float* outSlopeZ) const {
    if (outSlopeZ) *outSlopeZ = 0.0f;
    if (realData_.isLoaded() && realData_.isInCoreBounds(p)) {
        const NearestOsmRoad nr = realData_.findNearestRoad(p, 18.0f);
        if (!nr.found || !nr.isBridge || nr.dist > nr.halfWidth + 2.2f) return 0.0f;
        const Vec2 perp{-nr.tangent.z, nr.tangent.x};
        const float demRaw = realData_.sampleDem(p);
        // Filter SRTM 30m bridge-radar spikes so twin Han River bridge carriageways share an identical level deck!
        const float waterRef = std::min({demRaw,
                                         realData_.sampleDem(p + perp * 26.0f),
                                         realData_.sampleDem(p - perp * 26.0f)});
        float wSum = 0.0f;
        constexpr float kOffsets[5] = {-64.0f, -32.0f, 0.0f, 32.0f, 64.0f};
        constexpr float kWeights[5] = {0.12f, 0.20f, 0.36f, 0.20f, 0.12f};
        for (int i = 0; i < 5; ++i) {
            if (realData_.isInWater(p + nr.tangent * kOffsets[i])) wSum += kWeights[i];
        }
        const float baseElev = lerpf(demRaw, waterRef, clampf(wSum * 1.4f, 0.0f, 1.0f));
        return baseElev + lerpf(0.06f, 9.2f, wSum);
    }

    const SeoulBridge* br = findBridgeAt(p, 1.0f);
    if (!br) return 0.0f;

    const float z0 = br->zNorth - 42.0f;
    const float z1 = br->zSouth + 42.0f;
    const float rampSpan = 95.0f;
    const float maxH = br->deckHeight;

    if (p.z < z0 || p.z > z1) return 0.0f;

    if (p.z < z0 + rampSpan) {
        const float u = clampf((p.z - z0) / rampSpan, 0.0f, 1.0f);
        const float h = maxH * (u * u * (3.0f - 2.0f * u));
        if (outSlopeZ) *outSlopeZ = (maxH / rampSpan) * (6.0f * u * (1.0f - u));
        return h;
    }
    if (p.z > z1 - rampSpan) {
        const float u = clampf((z1 - p.z) / rampSpan, 0.0f, 1.0f);
        const float h = maxH * (u * u * (3.0f - 2.0f * u));
        if (outSlopeZ) *outSlopeZ = -(maxH / rampSpan) * (6.0f * u * (1.0f - u));
        return h;
    }
    // Gentle mid-span crown (+1.25m arch at bridge center!)
    const float midSpan = (z1 - rampSpan) - (z0 + rampSpan);
    const float t = clampf((p.z - (z0 + rampSpan)) / std::max(1.0f, midSpan), 0.0f, 1.0f);
    const float arch = 1.25f * std::sin(t * kPi);
    if (outSlopeZ) *outSlopeZ = (1.25f * kPi / std::max(1.0f, midSpan)) * std::cos(t * kPi);
    return maxH + arch;
}

float CityMap::surfaceHeightAt(const Vec2& p) const {
    if (realData_.isLoaded()) {
        const float demY = realData_.sampleDem(p);
        if (realData_.isInCoreBounds(p) && realData_.isOnBridge(p, 1.5f)) {
            return std::max(demY, bridgeDeckElevationAt(p));
        }
        return demY;
    }
    const SeoulBridge* br = findBridgeAt(p, 0.6f);
    if (br != nullptr) {
        return std::max(terrainElevationAt(p), bridgeDeckElevationAt(p));
    }
    return terrainElevationAt(p);
}

bool CityMap::isMajorArterialRoad(int roadIdx, bool horizontal) const {
    if (!horizontal) {
        // N-S Major Boulevards & Han River Bridge Avenues:
        // Sejong-daero (-11), Mapo (-55), Wonhyo (-39), Hangang (-25), Banpo (+7),
        // Hannam (+25), Gangnam-daero (+29), Seongsu (+55), Yeongdong (+64/+71), Jamsil (+106)
        return roadIdx == -133 || roadIdx == -94 || roadIdx == -70 || roadIdx == -55 ||
               roadIdx == -39  || roadIdx == -25 || roadIdx == -11 || roadIdx == -10 ||
               roadIdx == 7    || roadIdx == 25  || roadIdx == 29  || roadIdx == 39  ||
               roadIdx == 55   || roadIdx == 64  || roadIdx == 71  || roadIdx == 86  ||
               roadIdx == 106  || roadIdx == 126;
    }
    // E-W Major Boulevards:
    // Jongno (-55), Euljiro (-49), Teheran-ro (+35), Bongeunsa-ro (+25), Olympic-ro (+19), Nambu (+65)
    return roadIdx == -64 || roadIdx == -55 || roadIdx == -49 || roadIdx == -38 ||
           roadIdx == -20 || roadIdx == 14  || roadIdx == 19  || roadIdx == 25  ||
           roadIdx == 35  || roadIdx == 52  || roadIdx == 65;
}

bool CityMap::isOnRoad(const Vec2& p, float margin) const {
    if (realData_.isLoaded() && realData_.isInCoreBounds(p)) {
        return realData_.isOnRoad(p, margin);
    }
    if (findBridgeAt(p, margin) != nullptr) return true;
    if (isInHanRiverWater(p, -2.0f)) return false;
    if (isOnRiversideExpressway(p)) return true;
    const float halfR = kRoadWidth * 0.5f + margin;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    return (std::abs(p.x - rx) <= halfR) || (std::abs(p.z - rz) <= halfR);
}

bool CityMap::isInIntersection(const Vec2& p) const {
    if (isInHanRiverWater(p, -2.0f)) return false;
    const float halfR = kRoadWidth * 0.5f;
    const float rx = roadCoord(nearestRoadIndex(p.x));
    const float rz = roadCoord(nearestRoadIndex(p.z));
    return (std::abs(p.x - rx) <= halfR) && (std::abs(p.z - rz) <= halfR);
}

DistrictType CityMap::districtAtWorld(const Vec2& p) const {
    if (realData_.isLoaded()) {
        if (realData_.isInWater(p)) return DistrictType::HanRiverPark;
        // Exact GPS-calibrated real Seoul district coordinates (Origin 37.5235N, 126.9850E)
        if (p.x >= -6900.0f && p.x <= -3400.0f && p.z >= -1350.0f && p.z <= 1150.0f) {
            return DistrictType::YeouidoFinance;
        }
        if (p.x >= -2200.0f && p.x <= 1200.0f && p.z <= -5100.0f && p.z >= -7500.0f) {
            return DistrictType::GwanghwamunPalace;
        }
        if (p.x >= -1500.0f && p.x <= 1400.0f && p.z > -5100.0f && p.z <= -3700.0f) {
            return (p.z < -4500.0f) ? DistrictType::JongnoCheonggye : DistrictType::MyeongdongCityHall;
        }
        if (p.x > 1400.0f && p.x <= 4200.0f && p.z <= -3400.0f && p.z >= -6000.0f) {
            return DistrictType::Dongdaemun;
        }
        if (p.x >= -1200.0f && p.x <= 2600.0f && p.z > -3700.0f && p.z <= -300.0f) {
            return DistrictType::NamsanItaewon;
        }
        if (p.x >= -3600.0f && p.x < -1200.0f && p.z > -3700.0f && p.z <= 350.0f) {
            return DistrictType::Yongsan;
        }
        if (p.x < -3600.0f && p.z < -1350.0f) {
            return DistrictType::HongdaeMapo;
        }
        if (p.x >= 3400.0f && p.x <= 9400.0f && p.z < -200.0f && p.z > -4400.0f) {
            return DistrictType::SeongsuKonkuk;
        }
        if (p.x >= 7600.0f && p.z >= -200.0f) {
            return DistrictType::JamsilSongpa;
        }
        if (p.x >= 2100.0f && p.x < 7600.0f && p.z >= 100.0f) {
            return DistrictType::GangnamTeheran;
        }
        if (p.x >= -2000.0f && p.x < 2100.0f && p.z >= 100.0f) {
            return DistrictType::SeochoBanpo;
        }
        if (p.z < -4000.0f) return DistrictType::GangbukNowon;
        if (p.x < -6200.0f) return DistrictType::GangseoMagok;
        return DistrictType::GuroGwanak;
    }

    if (isOnYeouidoIsland(p)) return DistrictType::YeouidoFinance;
    if (isInHanRiverWater(p, -10.0f) || isInHanRiverPark(p)) return DistrictType::HanRiverPark;

    const float riverZ = hanRiverCenterZ(p.x);
    if (p.z < riverZ) {
        // Gangbuk (North of Han River)
        if (p.x > -2400.0f && p.x < 600.0f && p.z < -4700.0f && p.z > -7200.0f) {
            return DistrictType::GwanghwamunPalace;
        }
        if (p.x > -1500.0f && p.x < 1200.0f && p.z <= -3600.0f && p.z >= -4700.0f) {
            return (p.z < -4200.0f) ? DistrictType::JongnoCheonggye : DistrictType::MyeongdongCityHall;
        }
        if (p.x >= 1200.0f && p.x < 3600.0f && p.z < -3000.0f && p.z > -5600.0f) {
            return DistrictType::Dongdaemun;
        }
        if (p.x > -900.0f && p.x < 2400.0f && p.z >= -3600.0f && p.z < riverZ) {
            return DistrictType::NamsanItaewon;
        }
        if (p.x <= -900.0f && p.x > -3400.0f && p.z >= -3600.0f && p.z < riverZ) {
            return DistrictType::Yongsan;
        }
        if (p.x <= -3400.0f && p.x > -8600.0f && p.z > -5800.0f) {
            return DistrictType::HongdaeMapo;
        }
        if (p.x >= 3600.0f && p.x < 9400.0f && p.z > -4400.0f) {
            return DistrictType::SeongsuKonkuk;
        }
        return DistrictType::GangbukNowon;
    }

    // Gangnam (South of Han River)
    if (p.x >= 1400.0f && p.x < 6400.0f && p.z < 6400.0f) {
        return DistrictType::GangnamTeheran;
    }
    if (p.x >= -1400.0f && p.x < 1400.0f && p.z < 6400.0f) {
        return DistrictType::SeochoBanpo;
    }
    if (p.x >= 6400.0f) {
        return DistrictType::JamsilSongpa;
    }
    if (p.x <= -6800.0f && p.z < 3400.0f) {
        return DistrictType::GangseoMagok;
    }
    return DistrictType::GuroGwanak;
}

const char* CityMap::districtNameEn(DistrictType d) {
    switch (d) {
        case DistrictType::GwanghwamunPalace:  return "Gwanghwamun · Gyeongbokgung";
        case DistrictType::JongnoCheonggye:    return "Jongno · Cheonggyecheon";
        case DistrictType::MyeongdongCityHall: return "Myeongdong · City Hall";
        case DistrictType::NamsanItaewon:      return "Namsan · Itaewon";
        case DistrictType::Yongsan:            return "Yongsan · Hangangno";
        case DistrictType::YeouidoFinance:     return "Yeouido Financial District";
        case DistrictType::GangnamTeheran:     return "Gangnam · Teheran-ro";
        case DistrictType::SeochoBanpo:        return "Seocho · Banpo";
        case DistrictType::JamsilSongpa:       return "Jamsil · Lotte World Tower";
        case DistrictType::SeongsuKonkuk:      return "Seongsu · Seoul Forest";
        case DistrictType::HongdaeMapo:        return "Hongdae · Mapo";
        case DistrictType::Dongdaemun:         return "Dongdaemun · DDP";
        case DistrictType::GangbukNowon:       return "Gangbuk · Nowon";
        case DistrictType::GangseoMagok:       return "Gangseo · Magok";
        case DistrictType::GuroGwanak:         return "Guro · Gwanak";
        case DistrictType::HanRiverPark:       return "Hangang Waterfront Park";
    }
    return "Seoul Metropolis";
}

const char* CityMap::districtNameKo(DistrictType d) {
    switch (d) {
        case DistrictType::GwanghwamunPalace:  return "광화문 · 경복궁";
        case DistrictType::JongnoCheonggye:    return "종로 · 청계천";
        case DistrictType::MyeongdongCityHall: return "명동 · 서울시청";
        case DistrictType::NamsanItaewon:      return "남산 · 이태원";
        case DistrictType::Yongsan:            return "용산 · 한강로";
        case DistrictType::YeouidoFinance:     return "여의도 금융지구";
        case DistrictType::GangnamTeheran:     return "강남 · 테헤란로";
        case DistrictType::SeochoBanpo:        return "서초 · 반포";
        case DistrictType::JamsilSongpa:       return "잠실 · 롯데월드타워";
        case DistrictType::SeongsuKonkuk:      return "성수 · 서울숲";
        case DistrictType::HongdaeMapo:        return "홍대 · 마포";
        case DistrictType::Dongdaemun:         return "동대문 · DDP";
        case DistrictType::GangbukNowon:       return "강북 · 노원";
        case DistrictType::GangseoMagok:       return "강서 · 마곡";
        case DistrictType::GuroGwanak:         return "구로 · 관악";
        case DistrictType::HanRiverPark:       return "한강 수변공원";
    }
    return "서울특별시";
}

void CityMap::buildSeoulMacroGeography() {
    bridges_.clear();
    landmarks_.clear();
    mountains_.clear();
    perimeterMountains_.clear();
    majorParks_.clear();
    tributaries_.clear();
    arterials_.clear();
    macroBuildings_.clear();
    pois_.clear();
    lakes_.clear();

    // 1) Real Seoul Surrounding Mountains & Central Namsan!
    mountains_ = {
        {"Namsan Mountain",     "남산 (262m)",   {  120.0f, -2420.0f}, 1250.0f,  920.0f, 155.0f},
        {"Bukhansan Mountain",  "북한산 (836m)", {-1900.0f, -10900.0f}, 4200.0f, 2600.0f, 480.0f},
        {"Inwangsan Mountain",  "인왕산 (338m)", {-2650.0f, -6350.0f}, 1450.0f, 1250.0f, 225.0f},
        {"Bugaksan Mountain",   "북악산 (342m)", { -850.0f, -7250.0f}, 1600.0f, 1150.0f, 240.0f},
        {"Gwanaksan Mountain",  "관악산 (632m)", {-2400.0f,  9700.0f}, 4100.0f, 2800.0f, 410.0f},
        {"Achasan Mountain",    "아차산 (295m)", { 9900.0f, -3400.0f}, 1900.0f, 1650.0f, 210.0f},
        {"Cheonggyesan",        "청계산 (540m)", { 4800.0f, 10200.0f}, 3400.0f, 2400.0f, 340.0f}
    };

    // 1B) 14 Outer Seoul Basin Ring Mountains enclosing ALL 4 map boundaries seamlessly (no flat cliff edges!)
    perimeterMountains_ = {
        // North Ridge (Dobongsan · Suraksan · Buramsan · Bongsan)
        {"Dobongsan",   "도봉산", {  2200.0f, -13400.0f}, 4600.0f, 3200.0f, 510.0f},
        {"Suraksan",    "수락산", {  7200.0f, -12900.0f}, 4400.0f, 3000.0f, 440.0f},
        {"Buramsan",    "불암산", { 11800.0f, -11600.0f}, 4200.0f, 3200.0f, 390.0f},
        {"Bongsan",     "봉산",   { -7600.0f, -12400.0f}, 4800.0f, 3200.0f, 340.0f},
        {"Deogyangsan", "덕양산", {-13600.0f,  -8800.0f}, 4000.0f, 4200.0f, 290.0f},
        // East Ridge (Yongmasan · Geomdansan · Namhansan · Yebongsan)
        {"Yongmasan",   "용마산", { 15400.0f,  -6600.0f}, 3600.0f, 4400.0f, 360.0f},
        {"Geomdansan",  "검단산", { 16200.0f,   2400.0f}, 3800.0f, 4600.0f, 420.0f},
        {"Namhansan",   "남한산", { 14200.0f,   8800.0f}, 4600.0f, 4200.0f, 430.0f},
        // South Ridge (Daemosan · Guryongsan · Samseongsan · Surisan)
        {"Daemosan",    "대모산", {  9800.0f,  12400.0f}, 4400.0f, 3200.0f, 350.0f},
        {"Samseongsan", "삼성산", { -7400.0f,  12600.0f}, 4800.0f, 3200.0f, 380.0f},
        {"Surisan",     "수리산", {-12800.0f,  11200.0f}, 4600.0f, 3600.0f, 360.0f},
        // West Hills flanking Gimpo/Gangseo & Incheon greenbelt
        {"Gyeyangsan",  "계양산", {-16200.0f,   4600.0f}, 3600.0f, 4800.0f, 330.0f},
        {"Gaehwasan",   "개화산", {-15900.0f,  -3600.0f}, 3200.0f, 3200.0f, 260.0f},
        {"Aengbongsan", "앵봉산", {-11800.0f, -12400.0f}, 4400.0f, 3200.0f, 310.0f}
    };

    // 2) Real Seoul 4 Major Tributaries (안양천, 중랑천, 탄천, 청계천)!
    tributaries_ = {
        {
            "Anyangcheon Stream", "안양천",
            {{-6680.0f, 10500.0f}, {-6820.0f, 6800.0f}, {-6940.0f, 3600.0f},
             {-7120.0f, 1400.0f}, {-7620.0f, -220.0f}},
            48.0f
        },
        {
            "Jungnangcheon Stream", "중랑천",
            {{7200.0f, -11200.0f}, {6250.0f, -7600.0f}, {5420.0f, -4400.0f},
             {4880.0f, -2250.0f}, {4480.0f, -920.0f}},
            52.0f
        },
        {
            "Tancheon Stream", "탄천",
            {{6950.0f, 9200.0f}, {6420.0f, 5400.0f}, {6140.0f, 2700.0f},
             {6090.0f, 920.0f}, {6120.0f, -220.0f}},
            46.0f
        },
        {
            "Cheonggyecheon Stream", "청계천",
            {{-920.0f, -4536.0f}, {420.0f, -4536.0f}, {1900.0f, -4536.0f},
             {3600.0f, -4480.0f}, {4960.0f, -4320.0f}},
            14.0f
        }
    };

    // 3) 9 Real Iconic Seoul Parks & Ceremonial Plazas!
    majorParks_ = {
        {"Yeouido Park",       "여의도공원",  {-4820.0f,   310.0f}, {720.0f, 130.0f},  0.24f, {58, 106, 52, 255}},
        {"Seoul Forest",       "서울숲",      { 4080.0f, -1480.0f}, {480.0f, 380.0f},  0.00f, {52, 98, 48, 255}},
        {"Olympic Park",       "올림픽공원",  {10900.0f,  2180.0f}, {680.0f, 560.0f},  0.00f, {56, 102, 48, 255}},
        {"Yongsan Park",       "용산공원",    { -840.0f, -1420.0f}, {640.0f, 480.0f},  0.00f, {54, 96, 48, 255}},
        {"Gwanghwamun Square", "광화문광장",  { -924.0f, -5090.0f}, { 46.0f, 520.0f},  0.00f, {158, 156, 150, 255}},
        {"Seoul Plaza",        "서울광장",    { -924.0f, -4220.0f}, { 96.0f,  72.0f},  0.00f, {64, 126, 58, 255}},
        {"Boramae Park",       "보라매공원",  {-3480.0f,  4620.0f}, {340.0f, 290.0f},  0.00f, {58, 102, 50, 255}},
        {"World Cup Park",     "월드컵공원",  {-9200.0f, -3350.0f}, {760.0f, 540.0f},  0.00f, {56, 100, 48, 255}},
        {"Seokchon Lake Park", "석촌호수공원",{ 8904.0f,  2240.0f}, {560.0f, 210.0f},  0.00f, {60, 108, 54, 255}}
    };

    // 4) Real Seoul Curved Riverside Expressways (강변북로 & 올림픽대로) + Named Boulevards!
    {
        SeoulArterialRoad gbb{"Gangbyeonbuk-ro", "강변북로", {}, 11.0f, true};
        SeoulArterialRoad oly{"Olympic-daero",   "올림픽대로", {}, 11.0f, true};
        const int expSteps = 72;
        for (int i = 0; i <= expSteps; ++i) {
            const float x = lerpf(kWorldMinX + 400.0f, kWorldMaxX - 400.0f,
                                  static_cast<float>(i) / static_cast<float>(expSteps));
            const float cz = hanRiverCenterZ(x);
            const float hw = hanRiverHalfWidth(x);
            gbb.points.push_back({x, cz - hw - 126.0f});
            oly.points.push_back({x, cz + hw + 126.0f});
        }
        arterials_.push_back(std::move(gbb));
        arterials_.push_back(std::move(oly));

        // Major real Seoul arterial axes (used for 3D streetscape hierarchy + crisp full-screen/mini maps!)
        arterials_.push_back({"Sejong-daero",    "세종대로",   {{-924.0f, -6000.0f}, {-924.0f, -3400.0f}}, 13.0f, false});
        arterials_.push_back({"Hangang-daero",   "한강대로",   {{-924.0f, -3400.0f}, {-2100.0f,  -400.0f}}, 11.0f, false});
        arterials_.push_back({"Jongno",          "종로",       {{-2800.0f, -4620.0f}, { 4600.0f, -4620.0f}}, 10.5f, false});
        arterials_.push_back({"Euljiro",         "을지로",     {{-1600.0f, -4116.0f}, { 3800.0f, -4116.0f}}, 10.0f, false});
        arterials_.push_back({"Gangnam-daero",   "강남대로",   {{ 2100.0f,   200.0f}, { 2436.0f,  7200.0f}}, 12.0f, false});
        arterials_.push_back({"Teheran-ro",      "테헤란로",   {{ 1200.0f,  2940.0f}, { 6600.0f,  2940.0f}}, 12.0f, false});
        arterials_.push_back({"Yeongdong-daero", "영동대로",   {{ 5964.0f,  -300.0f}, { 5964.0f,  5800.0f}}, 12.0f, false});
        arterials_.push_back({"Olympic-ro",      "올림픽로",   {{ 6300.0f,  1596.0f}, {12200.0f,  1932.0f}}, 11.5f, false});
        arterials_.push_back({"Banpo-daero",     "반포대로",   {{  588.0f,   200.0f}, {  588.0f,  6400.0f}}, 11.0f, false});
        arterials_.push_back({"Nambu Sunhwan-ro","남부순환로", {{-9800.0f,  4620.0f}, {-3200.0f,  5460.0f}, { 3400.0f,  5460.0f}, { 9800.0f,  4800.0f}}, 11.0f, true});
        arterials_.push_back({"Seobu Ganseon-ro","서부간선도로",{{-6520.0f, 10200.0f}, {-6780.0f, 3600.0f}, {-7460.0f, -100.0f}}, 10.0f, true});
    }

    // 5) 15 Real Drivable Han River Bridges aligned strictly to 84m N-S avenues!
    struct BridgeDef {
        const char* en;
        const char* ko;
        int roadIdx;
        bool rainbow;
        bool cableStayed;
        bool arch;
        ColorRGBA accent;
    };
    const BridgeDef kBridgeDefs[15] = {
        {"Gayang Bridge",    "가양대교",   -133, false, false, false, {146, 152, 160, 255}},
        {"Yanghwa Bridge",   "양화대교",    -94, false, false, true,  {158, 162, 168, 255}},
        {"Seogang Bridge",   "서강대교",    -70, false, false, true,  {172, 54, 48, 255}},
        {"Mapo Bridge",      "마포대교",    -55, false, false, false, {136, 144, 156, 255}},
        {"Wonhyo Bridge",    "원효대교",    -39, false, false, false, {148, 154, 162, 255}},
        {"Hangang Bridge",   "한강대교",    -25, false, false, true,  {132, 138, 146, 255}},
        {"Dongjak Bridge",   "동작대교",    -10, false, false, true,  {78, 126, 166, 255}},
        {"Banpo Bridge",     "반포대교",      7, true,  false, false, {142, 152, 166, 255}},
        {"Hannam Bridge",    "한남대교",     25, false, false, false, {150, 156, 164, 255}},
        {"Dongho Bridge",    "동호대교",     39, false, false, true,  {162, 166, 172, 255}},
        {"Seongsu Bridge",   "성수대교",     55, false, false, true,  {72, 136, 142, 255}},
        {"Yeongdong Bridge", "영동대교",     71, false, false, false, {146, 152, 160, 255}},
        {"Cheongdam Bridge", "청담대교",     86, false, false, false, {126, 134, 146, 255}},
        {"Jamsil Bridge",    "잠실대교",    106, false, false, false, {148, 154, 162, 255}},
        {"Olympic Bridge",   "올림픽대교",  126, false, true,  false, {202, 162, 74, 255}}
    };
    for (const auto& bd : kBridgeDefs) {
        const float cx = roadCoord(bd.roadIdx);
        const float rCenter = hanRiverCenterZ(cx);
        const float rHw = hanRiverHalfWidth(cx);
        bridges_.push_back(SeoulBridge{
            bd.en, bd.ko, cx,
            rCenter - rHw - 38.0f,
            rCenter + rHw + 38.0f,
            10.5f,
            bd.rainbow ? 6.8f : 7.8f,
            bd.rainbow, bd.cableStayed, bd.arch, bd.accent
        });
    }

    // 6) Iconic Real Seoul Macro Landmarks with TRUE Real-World Geographic Orientation!
    landmarks_ = {
        {SeoulLandmarkKind::LotteWorldTower,     { 8946.0f,  1974.0f}, 555.0f, "Lotte World Tower (555m)", "롯데월드타워 (555m)",      DistrictType::JamsilSongpa,      -0.42f},
        {SeoulLandmarkKind::NSeoulTower,         {  120.0f, -2420.0f}, 236.0f, "N Seoul Tower (Namsan)",   "N서울타워 (남산)",         DistrictType::NamsanItaewon,      0.00f},
        {SeoulLandmarkKind::GyeongbokgungPalace, { -924.0f, -5796.0f},  26.0f, "Gyeongbokgung Palace",     "경복궁 · 광화문",          DistrictType::GwanghwamunPalace,  kHalfPi}, // Faces South (+Z) down Sejong-daero!
        {SeoulLandmarkKind::Building63,          {-3318.0f,   462.0f}, 249.0f, "63 Square Tower",          "여의도 63빌딩",            DistrictType::YeouidoFinance,     0.48f},   // Angled ~27.5 deg facing Han River/Wonhyo!
        {SeoulLandmarkKind::NationalAssembly,    {-6090.0f,   210.0f},  58.0f, "National Assembly",        "국회의사당",               DistrictType::YeouidoFinance,     0.18f},   // Faces ESE (+X/+Z) down Uisadang-daero!
        {SeoulLandmarkKind::Parc1RedTower,       {-4578.0f,   294.0f}, 333.0f, "Yeouido Parc1 Tower",      "여의도 파크원",            DistrictType::YeouidoFinance,     0.24f},   // Aligned with Yeouido-daero!
        {SeoulLandmarkKind::CoexTradeTower,      { 5418.0f,  2898.0f}, 228.0f, "COEX & Trade Tower",       "삼성동 코엑스 · 무역센터", DistrictType::GangnamTeheran,     0.00f},   // Faces East onto Yeongdong-daero!
        {SeoulLandmarkKind::SeoulCityHall,       { -924.0f, -4326.0f},  72.0f, "Seoul City Hall",          "서울시청 · 서울광장",      DistrictType::MyeongdongCityHall, kHalfPi}, // Faces South (+Z) over Seoul Plaza!
        {SeoulLandmarkKind::SungnyemunGate,      { -924.0f, -3696.0f},  22.0f, "Sungnyemun Gate",          "숭례문 (남대문)",          DistrictType::MyeongdongCityHall, kHalfPi}, // Centered on Sejong-daero N-S axis!
        {SeoulLandmarkKind::DongdaemunDDP,       { 1890.0f, -4326.0f},  29.0f, "Dongdaemun DDP",           "동대문 DDP",               DistrictType::Dongdaemun,         0.12f},   // Sweeping E-W along Euljiro!
        {SeoulLandmarkKind::JamsilStadium,       { 6846.0f,  1638.0f},  44.0f, "Jamsil Olympic Stadium",   "잠실종합운동장",           DistrictType::JamsilSongpa,       0.00f},   // Major axis N-S beside Tancheon!
        {SeoulLandmarkKind::GangnamStationPlaza, { 2436.0f,  2940.0f},  95.0f, "Gangnam Station",          "강남역 사거리",            DistrictType::GangnamTeheran,     0.00f}
    };

    // Build exact physical collision footprints matching the 3D landmark meshes
    auto addMacro = [&](Vec2 center, Vec2 halfExt, float height, float upperH,
                        ColorRGBA prim, ColorRGBA acc, BuildingStyle style, bool helipad = false) {
        Building b{};
        b.footprint = {{center.x - halfExt.x, center.z - halfExt.z},
                       {center.x + halfExt.x, center.z + halfExt.z}};
        b.baseY = terrainElevationAt(center);
        b.height = height;
        b.upperTierHeight = upperH;
        b.primaryColor = prim;
        b.accentColor = acc;
        b.style = style;
        b.litWindows = true;
        b.hasHelipad = helipad;
        b.isMacroLandmark = true;
        b.streetEdgeMask = 0x0Fu;
        macroBuildings_.push_back(b);
    };

    // Lotte World Tower + East Lotte World Mall podium
    addMacro({8946.0f, 1974.0f}, {32.0f, 32.0f}, 555.0f, 0.0f,
             {162, 184, 206, 255}, {214, 224, 236, 255}, BuildingStyle::SupertallSpire, false);
    addMacro({9022.0f, 1968.0f}, {42.0f, 38.0f}, 36.0f, 0.0f,
             {218, 222, 228, 255}, {190, 198, 210, 255}, BuildingStyle::ModernOffice, false);
    // 63 Building (iconic gold-tinted Yeouido landmark)
    addMacro({-3318.0f, 462.0f}, {26.0f, 22.0f}, 249.0f, 0.0f,
             {174, 146, 84, 255}, {212, 186, 122, 255}, BuildingStyle::GlassTower, true);
    // Parc1 Red-Exoskeleton Twin Towers (Yeouido — exact match to rotated 3D mesh!)
    addMacro({-4605.0f, 287.0f}, {18.0f, 18.0f}, 333.0f, 0.0f,
             {148, 164, 184, 255}, {178, 44, 42, 255}, BuildingStyle::GlassTower, true);
    addMacro({-4547.0f, 310.0f}, {17.0f, 17.0f}, 256.0f, 0.0f,
             {148, 164, 184, 255}, {178, 44, 42, 255}, BuildingStyle::GlassTower, true);
    // National Assembly Hall (Yeouido — faces East toward Yeouido Park!)
    addMacro({-6090.0f, 210.0f}, {34.0f, 44.0f}, 36.0f, 22.0f,
             {196, 192, 184, 255}, {76, 152, 144, 255}, BuildingStyle::CivicLandmark, false);
    // COEX Trade Tower & Gangnam Twin Towers
    addMacro({5418.0f, 2898.0f}, {24.0f, 24.0f}, 228.0f, 0.0f,
             {96, 118, 142, 255}, {172, 190, 210, 255}, BuildingStyle::GlassTower, true);
    addMacro({5350.0f, 2848.0f}, {44.0f, 36.0f}, 30.0f, 0.0f,
             {194, 200, 210, 255}, {152, 166, 184, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({2478.0f, 2898.0f}, {24.0f, 22.0f}, 182.0f, 36.0f,
             {72, 92, 118, 255}, {154, 174, 196, 255}, BuildingStyle::GlassTower, true);
    // Gyeongbokgung Geunjeongjeon Palace Pavilion + Gwanghwamun Gate (exact match to 3D lz + 142!)
    addMacro({-924.0f, -5796.0f}, {34.0f, 26.0f}, 24.0f, 10.0f,
             {162, 54, 44, 255}, {58, 116, 86, 255}, BuildingStyle::HanokPalace, false);
    addMacro({-924.0f, -5654.0f}, {28.0f, 10.0f}, 18.0f, 6.0f,
             {184, 178, 168, 255}, {62, 112, 86, 255}, BuildingStyle::HanokPalace, false);
    // Seoul City Hall & Sungnyemun (aligned right along Sejong-daero x = -924!)
    addMacro({-924.0f, -4326.0f}, {34.0f, 26.0f}, 56.0f, 16.0f,
             {116, 144, 168, 255}, {198, 206, 216, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({-924.0f, -3696.0f}, {18.0f, 10.0f}, 18.0f, 6.0f,
             {166, 160, 152, 255}, {58, 110, 82, 255}, BuildingStyle::HanokPalace, false);
    // Dongdaemun DDP & Jamsil Olympic Stadium
    addMacro({1890.0f, -4326.0f}, {54.0f, 28.0f}, 26.0f, 0.0f,
             {196, 202, 210, 255}, {132, 140, 150, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({6846.0f, 1638.0f}, {66.0f, 82.0f}, 38.0f, 0.0f,
             {188, 186, 180, 255}, {122, 134, 152, 255}, BuildingStyle::CivicLandmark, false);

    // Seokchon Lake patch (split into West & East basins by Songpa-daero!)
    lakes_.push_back(LakePatch{{{8430.0f, 2100.0f}, {8882.0f, 2380.0f}}, 0.04f});
    lakes_.push_back(LakePatch{{{8926.0f, 2100.0f}, {9380.0f, 2380.0f}}, 0.04f});

    // 7) Key Interactive POIs clustered cleanly near Default Spawn (Banpo/Gangnam Hangang Entrance: {588, 1020})
    pois_.push_back(PointOfInterest{
        PoiKind::CarDealership, {630.0f, 1068.0f}, 5.8f,
        "GANGNAM EXOTIC SHOWROOM", "강남 럭셔리 슈퍼카 전시장", {226, 178, 76, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::TuneGarage, {546.0f, 1068.0f}, 5.8f,
        "SEOUL AUTO CUSTOMS", "서울 커스텀 & 튜닝 차고", {76, 184, 218, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::WeaponShop, {630.0f, 1140.0f}, 5.0f,
        "AMMU-TECH TACTICAL", "택티컬 장비 & 무기 상점", {224, 92, 66, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::ClothingBoutique, {546.0f, 1140.0f}, 5.0f,
        "CHEONGDAM LUXURY BOUTIQUE", "청담 명품 의상 부티크", {218, 178, 128, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::GasStation, {714.0f, 1050.0f}, 6.2f,
        "SEOUL ENERGY SERVICE", "서울 주유 & 급속 정비소", {224, 94, 66, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::NightclubProperty, {714.0f, 1152.0f}, 5.0f,
        "APGUJEONG LOUNGE CLUB", "압구정 라운지 클럽", {188, 96, 236, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::MissionDelivery, {602.0f, 1026.0f}, 4.5f,
        "EXOTIC DELIVERY CONTRACT", "강남 슈퍼카 긴급 배송", {238, 198, 66, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::MissionRace, {574.0f, 996.0f}, 4.5f,
        "HANGANG BRIDGE SPRINT", "한강 대교 야간 스트리트 레이스", {82, 206, 232, 255}
    });
    pois_.push_back(PointOfInterest{
        PoiKind::MissionGangRaid, {684.0f, 1006.0f}, 4.5f,
        "GANGNAM SYNDICATE RAID", "조직 거점 소탕 의뢰", {232, 84, 72, 255}
    });
}

bool CityMap::loadRealSeoulMap(const std::uint8_t* bytes, std::size_t size) {
    if (!realData_.loadFromBytes(bytes, size)) {
        return false;
    }
    buildRealSeoulMacroGeography();
    lastCenterChunkX_ = 999999;
    lastCenterChunkZ_ = 999999;
    updateStreaming(realSpawnPos_, true);
    return true;
}

bool CityMap::loadRealSeoulMapFromFile(const std::string& path) {
    if (!realData_.loadFromFile(path)) {
        return false;
    }
    buildRealSeoulMacroGeography();
    lastCenterChunkX_ = 999999;
    lastCenterChunkZ_ = 999999;
    updateStreaming(realSpawnPos_, true);
    return true;
}

void CityMap::buildRealSeoulMacroGeography() {
    // Keep mountains_, perimeterMountains_, bridges_, tributaries_, arterials_ metadata populated,
    // while snapping landmarks_, macroBuildings_, spawn, and POIs to EXACT Real-OSM GPS coordinates!
    // Origin (0,0) = 37.5235N, 126.9850E (+X=East, +Z=South).
    landmarks_ = {
        {SeoulLandmarkKind::LotteWorldTower,     {10376.0f,  1222.0f}, 555.0f, "Lotte World Tower (555m)", "롯데월드타워 (555m)",      DistrictType::JamsilSongpa,      -0.35f},
        {SeoulLandmarkKind::NSeoulTower,         {  283.0f, -3078.0f}, 236.0f, "N Seoul Tower (Namsan)",   "N서울타워 (남산)",         DistrictType::NamsanItaewon,      0.00f},
        {SeoulLandmarkKind::GyeongbokgungPalace, { -706.0f, -6235.0f},  26.0f, "Gyeongbokgung Palace",     "경복궁 · 광화문",          DistrictType::GwanghwamunPalace,  kHalfPi},
        {SeoulLandmarkKind::Building63,          {-3947.0f,   456.0f}, 249.0f, "63 Square Tower",          "여의도 63빌딩",            DistrictType::YeouidoFinance,     0.48f},
        {SeoulLandmarkKind::NationalAssembly,    {-6252.0f,  -922.0f},  58.0f, "National Assembly",        "국회의사당",               DistrictType::YeouidoFinance,     0.18f},
        {SeoulLandmarkKind::Parc1RedTower,       {-5045.0f,  -248.0f}, 333.0f, "Yeouido Parc1 Tower",      "여의도 파크원",            DistrictType::YeouidoFinance,     0.24f},
        {SeoulLandmarkKind::CoexTradeTower,      { 6587.0f,  1322.0f}, 228.0f, "COEX & Trade Tower",       "삼성동 코엑스 · 무역센터", DistrictType::GangnamTeheran,     0.00f},
        {SeoulLandmarkKind::SeoulCityHall,       { -627.0f, -4756.0f},  72.0f, "Seoul City Hall",          "서울시청 · 서울광장",      DistrictType::MyeongdongCityHall, kHalfPi},
        {SeoulLandmarkKind::SungnyemunGate,      { -845.0f, -4032.0f},  22.0f, "Sungnyemun Gate",          "숭례문 (남대문)",          DistrictType::MyeongdongCityHall, kHalfPi},
        {SeoulLandmarkKind::DongdaemunDDP,       { 2155.0f, -4812.0f},  29.0f, "Dongdaemun DDP",           "동대문 DDP",               DistrictType::Dongdaemun,         0.12f},
        {SeoulLandmarkKind::JamsilStadium,       { 7750.0f,   920.0f},  44.0f, "Jamsil Olympic Stadium",   "잠실종합운동장",           DistrictType::JamsilSongpa,       0.00f},
        {SeoulLandmarkKind::GangnamStationPlaza, { 3764.0f,  2845.0f},  95.0f, "Gangnam Station",          "강남역 · 테헤란로",        DistrictType::GangnamTeheran,    -0.35f}
    };

    macroBuildings_.clear();
    auto addMacro = [&](Vec2 center, Vec2 halfExt, float height, float upperH,
                        ColorRGBA prim, ColorRGBA acc, BuildingStyle style, bool helipad = false) {
        Building b{};
        b.footprint = {{center.x - halfExt.x, center.z - halfExt.z},
                       {center.x + halfExt.x, center.z + halfExt.z}};
        b.baseY = realData_.sampleDem(center);
        b.height = height;
        b.upperTierHeight = upperH;
        b.primaryColor = prim;
        b.accentColor = acc;
        b.style = style;
        b.litWindows = true;
        b.hasHelipad = helipad;
        b.isMacroLandmark = true;
        b.streetEdgeMask = 0x0Fu;
        macroBuildings_.push_back(b);
    };

    addMacro({10376.0f, 1222.0f}, {30.0f, 30.0f}, 555.0f, 0.0f,
             {162, 184, 206, 255}, {214, 224, 236, 255}, BuildingStyle::SupertallSpire, false);
    addMacro({-3947.0f, 456.0f}, {25.0f, 21.0f}, 249.0f, 0.0f,
             {174, 146, 84, 255}, {212, 186, 122, 255}, BuildingStyle::GlassTower, true);
    addMacro({-5045.0f, -248.0f}, {20.0f, 20.0f}, 333.0f, 0.0f,
             {148, 164, 184, 255}, {178, 44, 42, 255}, BuildingStyle::GlassTower, true);
    addMacro({-6252.0f, -922.0f}, {32.0f, 40.0f}, 36.0f, 22.0f,
             {196, 192, 184, 255}, {76, 152, 144, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({6587.0f, 1322.0f}, {24.0f, 24.0f}, 228.0f, 0.0f,
             {96, 118, 142, 255}, {172, 190, 210, 255}, BuildingStyle::GlassTower, true);
    addMacro({-706.0f, -6235.0f}, {32.0f, 24.0f}, 24.0f, 10.0f,
             {162, 54, 44, 255}, {58, 116, 86, 255}, BuildingStyle::HanokPalace, false);
    addMacro({-627.0f, -4756.0f}, {32.0f, 24.0f}, 56.0f, 16.0f,
             {116, 144, 168, 255}, {198, 206, 216, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({-845.0f, -4032.0f}, {16.0f, 10.0f}, 18.0f, 6.0f,
             {166, 160, 152, 255}, {58, 110, 82, 255}, BuildingStyle::HanokPalace, false);
    addMacro({2155.0f, -4812.0f}, {48.0f, 26.0f}, 26.0f, 0.0f,
             {196, 202, 210, 255}, {132, 140, 150, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({7750.0f, 920.0f}, {62.0f, 76.0f}, 38.0f, 0.0f,
             {188, 186, 180, 255}, {122, 134, 152, 255}, BuildingStyle::CivicLandmark, false);

    // Find guaranteed-clear sidewalk on real Gangnam Station / Teheran-ro boulevard!
    auto pickClearSidewalk = [&](const Vec2& anchor, float sideSign, float extraDist, float& outYaw) -> Vec2 {
        const NearestOsmRoad nr = realData_.findNearestRoad(anchor, 90.0f);
        if (!nr.found) {
            outYaw = -0.35f;
            return anchor;
        }
        outYaw = std::atan2(nr.tangent.z, nr.tangent.x);
        const Vec2 rightN{-nr.tangent.z, nr.tangent.x};
        const float tryOffsets[4] = {
            nr.halfWidth + extraDist,
            -(nr.halfWidth + extraDist),
            nr.halfWidth + 0.9f,
            0.0f
        };
        for (int k = 0; k < 4; ++k) {
            Vec2 cand = nr.closestPt + rightN * (tryOffsets[k] * sideSign);
            Vec2 test = cand;
            if (!realData_.isInWater(cand) && !realData_.resolveCircleBuildings(test, 1.35f)) {
                return cand;
            }
        }
        return nr.closestPt;
    };

    // Pick a wide, unobstructed mid-block lane on real Teheran-ro boulevard (Gangnam)
    // with >= 50m clear skyscraper-canyon sightline straight ahead & >= 16m behind!
    {
        std::vector<std::uint32_t> candRoads;
        const Vec2 teheranAnchor{4060.0f, 2720.0f};
        realData_.queryChunkRoads(teheranAnchor, 320.0f, candRoads);
        const auto& roads = realData_.roads();
        const auto& rVerts = realData_.roadVerts();

        bool foundBoulevard = false;
        float bestScore = 1e18f;
        Vec2 bestPos = teheranAnchor;
        float bestYaw = -0.35f;

        for (std::uint32_t ri : candRoads) {
            const OsmRoadRec& r = roads[ri];
            if (r.roadClass > 2 || r.halfWidth < 7.5f || (r.flags & 2u) != 0u || r.vertCount < 2) continue;
            for (std::uint16_t i = 0; i + 1 < r.vertCount; ++i) {
                const Vec2 p0 = rVerts[r.firstVert + i];
                const Vec2 p1 = rVerts[r.firstVert + i + 1];
                const Vec2 seg = p1 - p0;
                const float segLen = seg.length();
                if (segLen < 20.0f) continue;
                const Vec2 dir = seg / segLen;
                const Vec2 mid = (p0 + p1) * 0.5f;
                const Vec2 rightN{-dir.z, dir.x};
                const Vec2 cand = mid + rightN * 2.2f;
                Vec2 probe = cand;
                if (realData_.isInWater(cand) || realData_.resolveCircleBuildings(probe, 4.5f)) continue;
                // Prefer looking ENE down Teheran-ro toward COEX & Lotte World Tower (dir.x > 0)!
                const Vec2 lookDir = (dir.x >= 0.0f) ? dir : (dir * -1.0f);
                const float fwdClear = realData_.raycastBuildings2D(cand, lookDir, 55.0f);
                const float bwdClear = realData_.raycastBuildings2D(cand, lookDir * -1.0f, 18.0f);
                if (fwdClear >= 0.0f || bwdClear >= 0.0f) continue;

                const float dAnchor = (cand - teheranAnchor).length();
                const float score = dAnchor - segLen * 0.4f;
                if (score < bestScore) {
                    bestScore = score;
                    bestPos = cand;
                    bestYaw = std::atan2(lookDir.z, lookDir.x);
                    foundBoulevard = true;
                }
            }
        }
        if (foundBoulevard) {
            realSpawnPos_ = bestPos;
            realSpawnYaw_ = bestYaw;
        } else {
            realSpawnPos_ = pickClearSidewalk(teheranAnchor, 1.0f, 2.0f, realSpawnYaw_);
        }
    }
    float dummyYaw = 0.0f;
    realPoliceRespawnPos_ = pickClearSidewalk(realSpawnPos_ + Vec2{85.0f, -32.0f}, 1.0f, 2.0f, dummyYaw);

    // Place all 9 interactive POIs cleanly along real Gangnam / Teheran-ro boulevards near spawn!
    pois_.clear();
    const Vec2 fwd{std::cos(realSpawnYaw_), std::sin(realSpawnYaw_)};
    const Vec2 rgt{-fwd.z, fwd.x};
    auto addRealPoi = [&](PoiKind kind, Vec2 approxPos, float radius,
                          const char* en, const char* ko, ColorRGBA col) {
        float y = 0.0f;
        const Vec2 p = pickClearSidewalk(approxPos, 1.0f, 1.8f, y);
        pois_.push_back(PointOfInterest{kind, p, radius, en, ko, col});
    };

    addRealPoi(PoiKind::CarDealership,    realSpawnPos_ + fwd * 45.0f + rgt * 18.0f, 5.8f,
               "GANGNAM EXOTIC SHOWROOM", "강남 럭셔리 슈퍼카 전시장", {226, 178, 76, 255});
    addRealPoi(PoiKind::TuneGarage,       realSpawnPos_ + fwd * 45.0f - rgt * 28.0f, 5.8f,
               "SEOUL AUTO CUSTOMS", "서울 커스텀 & 튜닝 차고", {76, 184, 218, 255});
    addRealPoi(PoiKind::WeaponShop,       realSpawnPos_ + fwd * 95.0f + rgt * 18.0f, 5.0f,
               "AMMU-TECH TACTICAL", "택티컬 장비 & 무기 상점", {224, 92, 66, 255});
    addRealPoi(PoiKind::ClothingBoutique, realSpawnPos_ + fwd * 95.0f - rgt * 28.0f, 5.0f,
               "CHEONGDAM LUXURY BOUTIQUE", "청담 명품 의상 부티크", {218, 178, 128, 255});
    addRealPoi(PoiKind::GasStation,       realSpawnPos_ - fwd * 60.0f + rgt * 22.0f, 6.2f,
               "SEOUL ENERGY SERVICE", "서울 주유 & 급속 정비소", {224, 94, 66, 255});
    addRealPoi(PoiKind::NightclubProperty,realSpawnPos_ - fwd * 105.0f - rgt * 24.0f, 5.0f,
               "GANGNAM LOUNGE CLUB", "강남 라운지 클럽", {188, 96, 236, 255});
    addRealPoi(PoiKind::MissionDelivery,  realSpawnPos_ + fwd * 22.0f, 4.5f,
               "EXOTIC DELIVERY CONTRACT", "강남 슈퍼카 긴급 배송", {238, 198, 66, 255});
    addRealPoi(PoiKind::MissionRace,      realSpawnPos_ - fwd * 24.0f, 4.5f,
               "TEHERAN-RO NIGHT SPRINT", "테헤란로 야간 스트리트 레이스", {82, 206, 232, 255});
    addRealPoi(PoiKind::MissionGangRaid,  realSpawnPos_ + fwd * 140.0f + rgt * 20.0f, 4.5f,
               "GANGNAM SYNDICATE RAID", "조직 거점 소탕 의뢰", {232, 84, 72, 255});
}

void CityMap::generate(std::uint64_t seed) {
    seed_ = seed;
    buildSeoulMacroGeography();
    if (realData_.isLoaded()) {
        buildRealSeoulMacroGeography();
    }
    lastCenterChunkX_ = 999999;
    lastCenterChunkZ_ = 999999;
    updateStreaming(spawnPos(), true);
}

void CityMap::generateChunk(int chunkX, int chunkZ) {
    const int baseBx = chunkX * kBlocksPerChunk;
    const int baseBz = chunkZ * kBlocksPerChunk;
    const float halfRoad = kRoadWidth * 0.5f;

    // Diverse real Korean commercial shopfront & awning palette (NEVER identical red everywhere!)
    static constexpr ColorRGBA kKoreaShopSigns[8] = {
        { 38,  72, 104, 255}, // Deep Cafe / Bank Navy
        { 44,  92,  74, 255}, // Forest Cafe / Bakery Green
        {134,  96,  62, 255}, // Warm Oak Bistro / Coffee Brown
        { 58,  64,  74, 255}, // Matte Charcoal Modern Retail
        { 46, 104, 118, 255}, // Pharmacy / Clinic Teal
        {158, 122,  64, 255}, // Warm Ochre Bakery / Convenience
        {126,  62,  58, 255}, // Subtle Brick Terracotta Bistro
        { 76,  86, 108, 255}  // Slate Blue Korean Office Retail
    };

    for (int lbz = 0; lbz < kBlocksPerChunk; ++lbz) {
        for (int lbx = 0; lbx < kBlocksPerChunk; ++lbx) {
            const int bx = baseBx + lbx;
            const int bz = baseBz + lbz;
            if (bx < kMinRoadIndexX || bx >= kMaxRoadIndexX ||
                bz < kMinRoadIndexZ || bz >= kMaxRoadIndexZ) {
                continue;
            }

            const float x0 = roadCoord(bx) + halfRoad;
            const float x1 = roadCoord(bx + 1) - halfRoad;
            const float z0 = roadCoord(bz) + halfRoad;
            const float z1 = roadCoord(bz + 1) - halfRoad;
            const Vec2 bc{(x0 + x1) * 0.5f, (z0 + z1) * 0.5f};

            // Skip blocks submerged in Han River, Seokchon Lake, or real Seoul tributaries!
            if (isInHanRiverWater(bc, -8.0f) || isInSeokchonLake(bc) || isInTributaryWater(bc, -8.0f)) {
                continue;
            }
            // Skip if directly intersecting a bridge approach deck or riverside expressway
            if (findBridgeAt(bc, 20.0f) != nullptr || isOnRiversideExpressway(bc)) continue;

            // Outer Ring Expressway IC Gantries & Roadside Korean Pine Rows lining all 4 urban Ring Expressway borders!
            const bool inRingZ = (bz >= -140 && bz <= 139);
            const bool inRingX = (bx >= -170 && bx <= 169);
            const bool isOuterE = (bx == 169) && inRingZ;
            const bool isOuterW = (bx == -170) && inRingZ;
            const bool isOuterS = (bz == 139) && inRingX;
            const bool isOuterN = (bz == -140) && inRingX;
            if (isOuterE || isOuterW) {
                const float roadX = roadCoord(isOuterE ? 170 : -170);
                const float pineX = isOuterE ? 14313.5f : -14313.5f;
                if (!isInHanRiverWater({roadX, bc.z}, -12.0f)) {
                    if ((bz % 2) == 0) {
                        const Vec2 icPos{roadX, bc.z};
                        streetProps_.push_back(StreetProp{icPos, StreetPropKind::RoadGantry, 0.0f, false, {}, 0.0f, icPos});
                    }
                    for (int k = 0; k < 4; ++k) {
                        const float pz = lerpf(z0 + 4.0f, z1 - 4.0f, static_cast<float>(k) / 3.0f);
                        const Vec2 tp{pineX, pz};
                        trees_.push_back(TreeProp{tp, 0.0f, 4.0f, 2.9f, false, TreeSpecies::KoreanPine});
                    }
                }
            }
            if (isOuterN || isOuterS) {
                const float roadZ = roadCoord(isOuterS ? 140 : -140);
                const float pineZ = isOuterS ? 11813.5f : -11813.5f;
                if ((bx % 2) == 0) {
                    const Vec2 icPos{bc.x, roadZ};
                    streetProps_.push_back(StreetProp{icPos, StreetPropKind::RoadGantry, kHalfPi, false, {}, 0.0f, icPos});
                }
                for (int k = 0; k < 4; ++k) {
                    const float px = lerpf(x0 + 4.0f, x1 - 4.0f, static_cast<float>(k) / 3.0f);
                    const Vec2 tp{px, pineZ};
                    trees_.push_back(TreeProp{tp, 0.0f, 4.0f, 2.9f, false, TreeSpecies::KoreanPine});
                }
            }

            // Skip if overlapping any macro landmark footprint
            bool hitsMacro = false;
            for (const auto& mb : macroBuildings_) {
                if (mb.footprint.min.x < x1 + 4.0f && mb.footprint.max.x > x0 - 4.0f &&
                    mb.footprint.min.z < z1 + 4.0f && mb.footprint.max.z > z0 - 4.0f) {
                    hitsMacro = true;
                    break;
                }
            }
            if (hitsMacro) continue;

            const float groundY = terrainElevationAt(bc);
            // Outside the 4 Ring Expressway borders or on steep mountain ridges (> 34m):
            // scatter ONLY natural Korean pine trees matching terrainElevationAt (NEVER flat urban parks/buildings)!
            if (groundY > 34.0f || !inRingX || !inRingZ) {
                Rng mrng(hashChunkCoords(seed_, bx * 13 + 7, bz * 17 + 3));
                for (int t = 0; t < 7; ++t) {
                    const Vec2 tp{mrng.range(x0, x1), mrng.range(z0, z1)};
                    if (isInHanRiverWater(tp, -10.0f)) continue;
                    const float ty = terrainElevationAt(tp);
                    const float rootSink = (ty > 1.5f) ? 1.15f : 0.0f;
                    trees_.push_back(TreeProp{
                        tp, ty - rootSink, mrng.range(3.2f, 5.2f), mrng.range(2.2f, 3.4f),
                        false, TreeSpecies::KoreanPine
                    });
                }
                continue;
            }

            Rng rng(hashChunkCoords(seed_, bx, bz));
            const DistrictType district = districtAtWorld(bc);

            // National Assembly Ceremonial East Lawn (국회의사당 잔디광장) & 63 Building Riverfront Plaza!
            const bool isAssemblyLawn = (bc.x >= -6060.0f && bc.x <= -5860.0f && bc.z >= 115.0f && bc.z <= 310.0f);
            const bool is63Waterfront = (bc.x >= -3430.0f && bc.x <= -3230.0f && bc.z >= 375.0f && bc.z <= 565.0f);
            if (isAssemblyLawn || is63Waterfront) {
                parks_.push_back(ParkPatch{{{x0 + 1.0f, z0 + 1.0f}, {x1 - 1.0f, z1 - 1.0f}}, groundY, {62, 112, 54, 255}});
                for (int t = 0; t < 4; ++t) {
                    const Vec2 tp{rng.range(x0 + 6.0f, x1 - 6.0f), rng.range(z0 + 6.0f, z1 - 6.0f)};
                    if (isAssemblyLawn && std::abs(tp.z - 210.0f) < 22.0f) continue; // Keep central sightline clear!
                    trees_.push_back(TreeProp{tp, groundY, rng.range(2.6f, 3.8f), rng.range(2.0f, 2.8f), false, TreeSpecies::CherryBlossom});
                }
                continue;
            }

            // Real Seoul Major Parks & Ceremonial Plazas (여의도공원, 서울숲, 올림픽공원, 용산공원, 광화문광장, 서울광장, 보라매공원, 월드컵공원)
            if (const SeoulParkZone* majorPk = findMajorSeoulPark(bc)) {
                parks_.push_back(ParkPatch{{{x0 + 1.0f, z0 + 1.0f}, {x1 - 1.0f, z1 - 1.0f}}, groundY, majorPk->grassColor});
                const bool isStonePlaza = (majorPk->grassColor.r > 130); // Gwanghwamun stone promenade
                if (!isStonePlaza) {
                    for (int t = 0; t < 7; ++t) {
                        const Vec2 tp{rng.range(x0 + 5.0f, x1 - 5.0f), rng.range(z0 + 5.0f, z1 - 5.0f)};
                        const TreeSpecies sp = (district == DistrictType::YeouidoFinance || district == DistrictType::JamsilSongpa)
                                                   ? (rng.chance(0.55f) ? TreeSpecies::CherryBlossom : TreeSpecies::GinkgoDeciduous)
                                                   : (rng.chance(0.45f) ? TreeSpecies::KoreanPine : TreeSpecies::GinkgoDeciduous);
                        trees_.push_back(TreeProp{tp, terrainElevationAt(tp), rng.range(3.1f, 4.5f), rng.range(2.4f, 3.5f), false, sp});
                    }
                    // Park benches facing inward toward park center!
                    const Vec2 toCenter = majorPk->center - bc;
                    const float benchYaw = std::atan2(toCenter.z, toCenter.x) - kHalfPi;
                    streetProps_.push_back(StreetProp{bc, StreetPropKind::Bench, benchYaw, false, {}, 0.0f, bc});
                }
                continue;
            }

            // Han River Waterfront Parks (한강공원 — lawns, cherry blossoms & ginkgos, benches facing the Han River!)
            if (district == DistrictType::HanRiverPark || isInHanRiverPark(bc)) {
                parks_.push_back(ParkPatch{{{x0 + 1.5f, z0 + 1.5f}, {x1 - 1.5f, z1 - 1.5f}}, groundY, {60, 96, 48, 255}});
                for (int t = 0; t < 6; ++t) {
                    const Vec2 tp{rng.range(x0 + 5.0f, x1 - 5.0f), rng.range(z0 + 5.0f, z1 - 5.0f)};
                    if (isInHanRiverWater(tp, -4.0f)) continue;
                    const TreeSpecies sp = rng.chance(0.45f) ? TreeSpecies::CherryBlossom : TreeSpecies::GinkgoDeciduous;
                    trees_.push_back(TreeProp{tp, terrainElevationAt(tp), rng.range(2.9f, 4.1f), rng.range(2.4f, 3.5f), false, sp});
                }
                // Bench front (+Z local) faces directly toward the Han River water!
                const float rZ = hanRiverCenterZ(bc.x);
                const float benchYaw = (bc.z < rZ) ? 0.0f : kPi;
                streetProps_.push_back(StreetProp{{bc.x, bc.z}, StreetPropKind::Bench, benchYaw, false, {}, 0.0f, {bc.x, bc.z}});
                continue;
            }

            // Special Hero Spawn Block near Banpo/Gangnam (bx == 7, bz == 12 -> x in [597..663], z in [1017..1083])
            if (bx == 7 && bz == 12) {
                buildings_.push_back(Building{
                    {{x0 + 8.0f, z0 + 26.0f}, {x1 - 8.0f, z1 - 6.0f}},
                    groundY, 16.5f, 0.0f,
                    {38, 46, 58, 255}, {188, 160, 102, 255}, {154, 130, 82, 255},
                    BuildingStyle::DealershipBuilding, true, false, false, false, false, 0x0Du, 0, -kHalfPi
                });
                const Vec2 lp{x0 + 1.6f, z0 + 3.0f};
                streetProps_.push_back(StreetProp{lp, StreetPropKind::StreetLight, kPi, false, {}, 0.0f, lp});
                continue;
            }
            if (bx == 6 && bz == 12) {
                buildings_.push_back(Building{
                    {{x0 + 6.0f, z0 + 24.0f}, {x1 - 6.0f, z1 - 6.0f}},
                    groundY, 13.5f, 0.0f,
                    {112, 118, 126, 255}, {76, 162, 198, 255}, {44, 102, 134, 255},
                    BuildingStyle::GarageBuilding, true, false, false, false, false, 0x0Eu, 0, -kHalfPi
                });
                continue;
            }
            if (bx == 8 && bz == 12) {
                canopies_.push_back(GasStationCanopy{{{x0 + 10.0f, z0 + 18.0f}, {x1 - 10.0f, z0 + 38.0f}}, groundY, 4.9f});
                buildings_.push_back(Building{
                    {{x0 + 12.0f, z1 - 18.0f}, {x1 - 12.0f, z1 - 4.0f}},
                    groundY, 6.2f, 0.0f,
                    {196, 198, 202, 255}, {198, 62, 48, 255}, {44, 104, 184, 255},
                    BuildingStyle::ShopBuilding, true, true, false, false, false, 0x04u, 1, -kHalfPi
                });
                continue;
            }

            // Neighborhood pocket parks (~9% of urban blocks)
            if (rng.chance(0.09f)) {
                parks_.push_back(ParkPatch{{{x0 + 2.5f, z0 + 2.5f}, {x1 - 2.5f, z1 - 2.5f}}, groundY, {60, 96, 48, 255}});
                for (int t = 0; t < 7; ++t) {
                    const Vec2 tp{rng.range(x0 + 6.0f, x1 - 6.0f), rng.range(z0 + 6.0f, z1 - 6.0f)};
                    const TreeSpecies sp = (district == DistrictType::YeouidoFinance || district == DistrictType::JamsilSongpa)
                                               ? TreeSpecies::CherryBlossom
                                               : (rng.chance(0.35f) ? TreeSpecies::KoreanPine : TreeSpecies::GinkgoDeciduous);
                    trees_.push_back(TreeProp{tp, terrainElevationAt(tp), rng.range(2.9f, 4.2f), rng.range(2.3f, 3.4f), false, sp});
                }
                streetProps_.push_back(StreetProp{{bc.x, bc.z + 10.0f}, StreetPropKind::Bench, kPi, false, {}, 0.0f, {bc.x, bc.z + 10.0f}});
                continue;
            }

            // Authentic Seoul Urban Typologies per District!
            const bool isGValleyTech = (district == DistrictType::GuroGwanak && bc.x < -3900.0f && bc.z > 1600.0f && bc.z < 5600.0f);
            const bool isHighRiseCbd = (district == DistrictType::GangnamTeheran ||
                                        district == DistrictType::YeouidoFinance ||
                                        district == DistrictType::MyeongdongCityHall ||
                                        district == DistrictType::JongnoCheonggye ||
                                        district == DistrictType::Yongsan ||
                                        isGValleyTech);
            const bool isKoreanAptZone = (district == DistrictType::SeochoBanpo ||
                                          district == DistrictType::JamsilSongpa ||
                                          district == DistrictType::GangbukNowon ||
                                          district == DistrictType::GangseoMagok ||
                                          (district == DistrictType::GuroGwanak && !isGValleyTech));
            const bool isTraditionalZone = (district == DistrictType::GwanghwamunPalace);
            const bool isTrendyBrick = (district == DistrictType::HongdaeMapo ||
                                        district == DistrictType::SeongsuKonkuk ||
                                        district == DistrictType::Dongdaemun);

            const bool onMajorArterialBlock = isMajorArterialRoad(bx, false) || isMajorArterialRoad(bx + 1, false) ||
                                              isMajorArterialRoad(bz, true)  || isMajorArterialRoad(bz + 1, true);
            const bool forceBanpoShopBlock  = (bx == 7 && bz == 11) || (bx == 6 && bz == 11);

            if (isKoreanAptZone && !forceBanpoShopBlock && rng.chance(onMajorArterialBlock ? 0.34f : 0.56f)) {
                // Iconic Korean Seoul Apartment Complex (대단지 아파트 — ALWAYS South-facing 남향 E-W slabs!)
                // Outer sidewalk apron stays crisp Korean granite pavers; landscaped green courtyard sits strictly BETWEEN the two slabs!
                const float aptH = rng.range(44.0f, 82.0f);
                const ColorRGBA aptPal[5] = {
                    {208, 204, 196, 255}, // Warm Ivory Concrete (Raemian/Hillstate)
                    {194, 198, 202, 255}, // Crisp Pearl Grey
                    {214, 202, 186, 255}, // Warm Sandstone Cream
                    {188, 192, 196, 255}, // Modern Silver Stone
                    {204, 196, 188, 255}  // Soft Greige
                };
                const ColorRGBA aptAcc[5] = {
                    { 46,  78, 118, 255}, // Raemian Deep Navy
                    { 62,  72,  84, 255}, // Xi Slate Charcoal
                    {136,  72,  54, 255}, // Hillstate Burgundy-Bronze
                    { 52,  94,  82, 255}, // Prugio Deep Forest Teal
                    {102,  86,  72, 255}  // Acro Warm Bronze
                };
                const int pIdx = rng.rangeInt(0, 4);
                // Cozy central landscaped garden strictly INSIDE the complex between the two residential slabs!
                parks_.push_back(ParkPatch{{{x0 + 7.5f, z0 + 21.0f}, {x1 - 7.5f, z1 - 21.0f}}, groundY, {60, 96, 48, 255}});
                for (int s = 0; s < 2; ++s) {
                    const float sz = (s == 0) ? (z0 + 13.5f) : (z1 - 13.5f);
                    const float slabH = std::max(36.0f, aptH + (s == 0 ? 0.0f : rng.range(-6.0f, 6.0f)));
                    const AABB2D fp{{x0 + 6.5f, sz - 7.0f}, {x1 - 6.5f, sz + 7.0f}};
                    buildings_.push_back(Building{
                        fp, groundY, slabH, 0.0f,
                        aptPal[pIdx], aptAcc[pIdx], aptAcc[pIdx],
                        BuildingStyle::KoreanApartment, true, false, true, slabH > 66.0f, false,
                        static_cast<std::uint8_t>(0x0Cu), // South & North veranda facades!
                        static_cast<std::uint8_t>(pIdx), kHalfPi // Faces South (+Z)
                    });
                }
                trees_.push_back(TreeProp{{bc.x - 14.0f, bc.z - 2.0f}, groundY, 3.3f, 2.5f, false, TreeSpecies::GinkgoDeciduous});
                trees_.push_back(TreeProp{{bc.x + 14.0f, bc.z + 2.0f}, groundY, 3.3f, 2.5f, false, TreeSpecies::KoreanPine});
                trees_.push_back(TreeProp{{bc.x - 4.0f,  bc.z + 4.5f}, groundY, 2.9f, 2.2f, false, TreeSpecies::CherryBlossom});
                trees_.push_back(TreeProp{{bc.x + 5.0f,  bc.z - 4.5f}, groundY, 3.1f, 2.4f, false, TreeSpecies::KoreanPine});
                streetProps_.push_back(StreetProp{{bc.x, bc.z}, StreetPropKind::Bench, 0.0f, false, {}, 0.0f, {bc.x, bc.z}});
            } else {
                // 2x2 Quadrant Seoul Urban Parcel Layout — EVERY building knows exact streetEdgeMask & facingYaw!
                const float midX = bc.x;
                const float midZ = bc.z;
                for (int qz = 0; qz < 2; ++qz) {
                    for (int qx = 0; qx < 2; ++qx) {
                        const float px0 = (qx == 0) ? (x0 + 3.6f) : (midX + 2.8f);
                        const float px1 = (qx == 0) ? (midX - 2.8f) : (x1 - 3.6f);
                        const float pz0 = (qz == 0) ? (z0 + 3.6f) : (midZ + 2.8f);
                        const float pz1 = (qz == 0) ? (midZ - 2.8f) : (z1 - 3.6f);

                        // Exact outer road-facing edge bitmask: bit0=-X, bit1=+X, bit2=-Z, bit3=+Z
                        const std::uint8_t edgeMask = static_cast<std::uint8_t>(
                            ((qx == 0) ? 0x01u : 0x02u) |
                            ((qz == 0) ? 0x04u : 0x08u)
                        );
                        const float primaryYaw = (qz == 0) ? -kHalfPi : kHalfPi;

                        Building b{};
                        b.footprint = {{px0, pz0}, {px1, pz1}};
                        b.baseY = groundY;
                        b.litWindows = true;
                        b.streetEdgeMask = edgeMask;
                        b.facingYaw = primaryYaw;
                        b.facadeVariant = static_cast<std::uint8_t>(rng.rangeInt(0, 7));

                        if (isHighRiseCbd) {
                            const bool glass = rng.chance(isGValleyTech ? 0.72f : 0.68f);
                            b.style = glass ? BuildingStyle::GlassTower : BuildingStyle::ModernOffice;
                            b.height = glass ? rng.range(isGValleyTech ? 42.0f : 54.0f, isGValleyTech ? 92.0f : 148.0f)
                                             : rng.range(26.0f, 68.0f);
                            b.upperTierHeight = (b.height > 72.0f && rng.chance(0.55f)) ? rng.range(12.0f, 26.0f) : 0.0f;
                            b.hasStorefront = true;
                            b.hasHelipad = (b.height > 88.0f);
                            const ColorRGBA gPal[5] = {
                                { 48,  70,  96, 255}, // Sapphire Curtain Wall
                                { 54,  82,  92, 255}, // Emerald-Teal Glass
                                { 84,  96, 110, 255}, // Brushed Silver Glass
                                {128, 124, 118, 255}, // Warm Granite Office
                                { 76,  72,  68, 255}  // Architectural Bronze Glass
                            };
                            b.primaryColor = gPal[rng.rangeInt(0, 4)];
                            b.accentColor = {164, 178, 194, 255};
                            b.awningColor = kKoreaShopSigns[b.facadeVariant];
                        } else if (isTraditionalZone && rng.chance(0.58f)) {
                            b.style = BuildingStyle::HanokPalace;
                            b.height = rng.range(6.8f, 11.2f);
                            b.primaryColor = {210, 202, 186, 255};
                            b.accentColor = {68, 72, 78, 255};
                            b.awningColor = {124, 62, 46, 255};
                        } else if (district == DistrictType::NamsanItaewon && rng.chance(0.52f)) {
                            b.style = BuildingStyle::VillaHouse;
                            b.height = rng.range(10.0f, 17.5f);
                            const ColorRGBA vPal[4] = {
                                {206, 194, 176, 255}, {172, 114, 92, 255},
                                {192, 188, 180, 255}, {154, 134, 116, 255}
                            };
                            b.primaryColor = vPal[rng.rangeInt(0, 3)];
                            b.accentColor = {92, 76, 68, 255};
                            b.awningColor = kKoreaShopSigns[b.facadeVariant];
                            b.hasBalconies = true;
                            b.hasStorefront = rng.chance(0.65f);
                        } else if (isTrendyBrick) {
                            b.style = rng.chance(0.65f) ? BuildingStyle::BrickCommercial : BuildingStyle::ModernOffice;
                            b.height = rng.range(14.0f, 38.0f);
                            const ColorRGBA bPal[5] = {
                                {158,  98,  76, 255}, // Seongsu Red Brick
                                {186, 174, 156, 255}, // Warm Limestone
                                {112, 108, 104, 255}, // Exposed Concrete
                                {176, 146, 118, 255}, // Warm Sandstone
                                { 78,  84,  92, 255}  // Dark Charcoal Brick
                            };
                            b.primaryColor = bPal[rng.rangeInt(0, 4)];
                            b.accentColor = {196, 188, 176, 255};
                            b.awningColor = kKoreaShopSigns[b.facadeVariant];
                            b.hasStorefront = true;
                        } else {
                            // Authentic Varied Seoul Neighborhood Mix (Guro · Gwanak · Seocho · Gangbuk · Gangseo!):
                            // Mix of Korean 4-5F red-brick/stone Sangga-Villas, 5-9F Granite/Brick Commercial, and 10-18F Glass Offices!
                            const float roll = rng.nextFloat01();
                            if (roll < 0.30f && !forceBanpoShopBlock) {
                                // Korean 4~5F Multi-Family / Sangga Villa (다세대·상가주택: warm brick/limestone + 1F shops + green waterproof roof!)
                                b.style = BuildingStyle::VillaHouse;
                                b.height = rng.range(11.5f, 17.5f);
                                const ColorRGBA vPal[4] = {
                                    {164, 102,  82, 255}, // Classic Korean Red Brick Villa
                                    {206, 196, 178, 255}, // Cream Limestone Piloti Villa
                                    {178, 158, 136, 255}, // Warm Beige Stone Villa
                                    {132, 126, 122, 255}  // Charcoal Granite Villa
                                };
                                b.primaryColor = vPal[rng.rangeInt(0, 3)];
                                b.accentColor = {76, 80, 88, 255};
                                b.awningColor = kKoreaShopSigns[b.facadeVariant];
                                b.hasBalconies = true;
                                b.hasStorefront = rng.chance(0.68f); // Classic Seoul 1F convenience stores/cafes/laundromats!
                            } else if (roll < 0.78f) {
                                // Korean 5~9F Neighborhood Commercial Building (근린생활 상가건물 — street-facing signs only!)
                                b.style = rng.chance(0.55f) ? BuildingStyle::BrickCommercial : BuildingStyle::ModernOffice;
                                b.height = rng.range(16.5f, 34.0f);
                                const ColorRGBA cPal[6] = {
                                    {194, 184, 168, 255}, // Korean Warm Granite Beige
                                    {162, 108,  88, 255}, // Terracotta Commercial Brick
                                    {172, 176, 182, 255}, // Silver-Grey Cladding
                                    {208, 200, 186, 255}, // Cream Travertine Stone
                                    {114, 122, 132, 255}, // Slate Stone & Glass
                                    {148, 136, 122, 255}  // Warm Taupe Stone
                                };
                                b.primaryColor = cPal[rng.rangeInt(0, 5)];
                                b.accentColor = {196, 198, 202, 255};
                                b.awningColor = kKoreaShopSigns[b.facadeVariant];
                                b.hasStorefront = true;
                            } else {
                                // Local Landmark Glass Office / Hospital / Officetel Tower (11~19F)
                                b.style = BuildingStyle::GlassTower;
                                b.height = rng.range(36.0f, 64.0f);
                                b.upperTierHeight = rng.chance(0.45f) ? rng.range(7.0f, 14.0f) : 0.0f;
                                const ColorRGBA oPal[3] = {
                                    {58, 82, 108, 255}, {68, 94, 104, 255}, {92, 102, 114, 255}
                                };
                                b.primaryColor = oPal[rng.rangeInt(0, 2)];
                                b.accentColor = {174, 186, 198, 255};
                                b.awningColor = kKoreaShopSigns[b.facadeVariant];
                                b.hasStorefront = true;
                            }
                        }
                        buildings_.push_back(b);
                    }
                }
            }

            // =================================================================
            // PROPERLY ORIENTED SEOUL STREETSCAPE FURNITURE!
            // Local +X = Streetlight / TrafficLight arm direction -> rotated by -yaw = (cos(yaw), 0, sin(yaw))
            // Local +Z = BusShelter / Bench open front direction -> rotated by -yaw = (-sin(yaw), 0, cos(yaw))
            // =================================================================
            const TreeSpecies stTree = (district == DistrictType::YeouidoFinance || district == DistrictType::JamsilSongpa)
                                           ? TreeSpecies::CherryBlossom
                                           : TreeSpecies::GinkgoDeciduous;

            // 1. Outward-pointing Streetlights on curb edges (arms extend OVER the asphalt road!)
            const Vec2 lampW{x0 + 1.35f, z0 + 14.0f}; // West curb -> arm points -X (yaw = kPi)
            const Vec2 lampE{x1 - 1.35f, z1 - 14.0f}; // East curb -> arm points +X (yaw = 0)
            const Vec2 lampN{bc.x - 10.0f, z0 + 1.35f}; // North curb -> arm points -Z (yaw = -kHalfPi)
            const Vec2 lampS{bc.x + 10.0f, z1 - 1.35f}; // South curb -> arm points +Z (yaw = +kHalfPi)
            streetProps_.push_back(StreetProp{lampW, StreetPropKind::StreetLight, kPi,      false, {}, 0.0f, lampW});
            streetProps_.push_back(StreetProp{lampE, StreetPropKind::StreetLight, 0.0f,     false, {}, 0.0f, lampE});
            streetProps_.push_back(StreetProp{lampN, StreetPropKind::StreetLight, -kHalfPi, false, {}, 0.0f, lampN});
            streetProps_.push_back(StreetProp{lampS, StreetPropKind::StreetLight,  kHalfPi, false, {}, 0.0f, lampS});

            // 2. Korean 4-Aspect Cantilevered Traffic Signals at intersection corners!
            const bool onMajorX = isMajorArterialRoad(bx, false) || isMajorArterialRoad(bx + 1, false);
            const bool onMajorZ = isMajorArterialRoad(bz, true)  || isMajorArterialRoad(bz + 1, true);
            if (onMajorX || onMajorZ || ((bx + bz) & 1) == 0) {
                const Vec2 sigNW{x0 + 1.2f, z0 + 1.2f}; // Arm points -X over W avenue!
                const Vec2 sigSE{x1 - 1.2f, z1 - 1.2f}; // Arm points +X over E avenue!
                streetProps_.push_back(StreetProp{sigNW, StreetPropKind::TrafficLight, kPi,  false, {}, 0.0f, sigNW});
                streetProps_.push_back(StreetProp{sigSE, StreetPropKind::TrafficLight, 0.0f, false, {}, 0.0f, sigSE});
            }

            // 2B. Iconic Korean Dark-Green Overhead Highway Directional Gantries spanning major Seoul avenues!
            if (isMajorArterialRoad(bx, false) && ((bz % 3 == 0) || (bx == 7 && bz == 11))) {
                const Vec2 ganPos{roadCoord(bx), bc.z};
                if (!isInHanRiverWater(ganPos, -12.0f) && findBridgeAt(ganPos, 18.0f) == nullptr) {
                    // Spans across N-S avenue (local X = East-West span, facing North/South drivers -> yaw = 0!)
                    streetProps_.push_back(StreetProp{ganPos, StreetPropKind::RoadGantry, 0.0f, false, {}, 0.0f, ganPos});
                }
            }
            if (isMajorArterialRoad(bz, true) && (bx % 3 == 0)) {
                const Vec2 ganPos{bc.x, roadCoord(bz)};
                if (!isInHanRiverWater(ganPos, -12.0f) && findBridgeAt(ganPos, 18.0f) == nullptr) {
                    // Spans across E-W avenue (yaw = kHalfPi!)
                    streetProps_.push_back(StreetProp{ganPos, StreetPropKind::RoadGantry, kHalfPi, false, {}, 0.0f, ganPos});
                }
            }

            // 2C. Korean Sidewalk Steel Pedestrian Guardrails along major Seoul boulevards!
            if (isMajorArterialRoad(bx, false)) {
                const Vec2 grW{x0 + 0.55f, bc.z};
                streetProps_.push_back(StreetProp{grW, StreetPropKind::PedGuardrail, kHalfPi, false, {}, 0.0f, grW});
            }
            if (isMajorArterialRoad(bz, true)) {
                const Vec2 grN{bc.x, z0 + 0.55f};
                streetProps_.push_back(StreetProp{grN, StreetPropKind::PedGuardrail, 0.0f, false, {}, 0.0f, grN});
            }

            // 3. Seoul Bus Shelter facing OUTWARD toward the road curb!
            if (rng.chance(0.45f)) {
                // Placed on South sidewalk (z1 - 1.55), open front (+Z local) points +Z toward road -> yaw = 0!
                const Vec2 bsPos{bc.x - 8.0f, z1 - 1.55f};
                streetProps_.push_back(StreetProp{bsPos, StreetPropKind::BusShelter, 0.0f, false, {}, 0.0f, bsPos});
            }

            // 4. Seoul Metro Glass Subway Entrance + Line Pole Sign along major arterial boulevards!
            if ((onMajorX || onMajorZ) && (rng.chance(0.32f) || (bx == 7 && bz == 11))) {
                const Vec2 subPos{bc.x + 12.0f, z0 + 2.2f};
                streetProps_.push_back(StreetProp{subPos, StreetPropKind::SubwayEntrance, 0.0f, false, {}, 0.0f, subPos});
            }

            // 5. Hydrants & Sidewalk Ginkgo/Cherry Trees in stone tree planters
            if (rng.chance(0.45f)) {
                const Vec2 hydPos{x0 + 0.9f, z0 + 6.5f};
                streetProps_.push_back(StreetProp{hydPos, StreetPropKind::Hydrant, 0.0f, false, {}, 0.0f, hydPos});
            }
            trees_.push_back(TreeProp{{bc.x + 2.0f, z0 + 1.85f}, groundY, 3.2f, 2.4f, true, stTree});
            trees_.push_back(TreeProp{{x0 + 1.85f, bc.z + 2.0f}, groundY, 3.2f, 2.4f, true, stTree});
            trees_.push_back(TreeProp{{x1 - 1.85f, bc.z - 2.0f}, groundY, 3.2f, 2.4f, true, stTree});
        }
    }
}

void CityMap::streamRealSeoulProps(const Vec2& focus) {
    // Helper: ensure dense props never stack on top of each other (fast local radius check)
    auto hasPropNearby = [&](const Vec2& p, float minDist) -> bool {
        const float r2 = minDist * minDist;
        for (const auto& sp : streetProps_) {
            if ((sp.pos - p).lengthSq() < r2) return true;
        }
        return false;
    };
    auto hasTreeNearby = [&](const Vec2& p, float minDist) -> bool {
        const float r2 = minDist * minDist;
        for (const auto& tp : trees_) {
            if ((tp.pos - p).lengthSq() < r2) return true;
        }
        return false;
    };

    // 1) Stream authentic real-world Seoul Metro entrances & Bus Stops from OSM POI table!
    //    Snap each POI safely off asphalt onto the clear outer sidewalk + add adjacent Ddareungi bike dock / trash bin!
    std::vector<std::uint32_t> poiIds;
    realData_.queryChunkPois(focus, 340.0f, poiIds);
    for (std::uint32_t pid : poiIds) {
        const OsmPoiRec& pr = realData_.pois()[pid];
        Vec2 pp{pr.x, pr.z};
        if ((pp - focus).lengthSq() > 340.0f * 340.0f) continue;
        if (realData_.isInWater(pp)) continue;
        const NearestOsmRoad nr = realData_.findNearestRoad(pp, 36.0f);
        if (!nr.found) continue;
        const float roadHw = std::max(2.6f, nr.halfWidth);
        Vec2 awayRoad = pp - nr.closestPt;
        if (awayRoad.lengthSq() < 0.04f) awayRoad = {-nr.tangent.z, nr.tangent.x};
        else awayRoad = awayRoad.normalized();

        // If POI coordinate sits on asphalt, step smoothly onto the outer sidewalk curb!
        if (realData_.isOnRoad(pp, 1.35f)) {
            const Vec2 candA = nr.closestPt + awayRoad * (roadHw + 2.25f);
            const Vec2 candB = nr.closestPt - awayRoad * (roadHw + 2.25f);
            Vec2 tA = candA, tB = candB;
            if (!realData_.isOnRoad(candA, 1.15f) && !realData_.isInWater(candA) && !realData_.resolveCircleBuildings(tA, 1.15f)) {
                pp = candA;
            } else if (!realData_.isOnRoad(candB, 1.15f) && !realData_.isInWater(candB) && !realData_.resolveCircleBuildings(tB, 1.15f)) {
                pp = candB;
                awayRoad = awayRoad * -1.0f;
            } else {
                continue;
            }
        }
        Vec2 chkB = pp;
        if (realData_.resolveCircleBuildings(chkB, 1.10f) || hasPropNearby(pp, 4.5f)) continue;

        // Align open front (+Z local -> (-sin yaw, cos yaw)) directly toward road centerline!
        const Vec2 toRoad = awayRoad * -1.0f;
        const float faceRoadYaw = std::atan2(-toRoad.x, toRoad.z);
        const std::uint8_t var = static_cast<std::uint8_t>(pid & 0xFFu);
        if (pr.kind == 0) {
            streetProps_.push_back(StreetProp{pp, StreetPropKind::SubwayEntrance, faceRoadYaw, false, {}, 0.0f, pp, 0,0,0,0,0,0, var});
            // Iconic Seoul Ddareungi public bike-share rack next to subway exits!
            const Vec2 bikeP = pp + nr.tangent * 5.2f;
            Vec2 bChk = bikeP;
            if (!realData_.isInWater(bikeP) && !realData_.isOnRoad(bikeP, 1.25f) &&
                !realData_.resolveCircleBuildings(bChk, 1.0f) && !hasPropNearby(bikeP, 3.0f)) {
                streetProps_.push_back(StreetProp{bikeP, StreetPropKind::DdareungiRack, faceRoadYaw, false, {}, 0.0f, bikeP, 0,0,0,0,0,0, var});
            }
        } else {
            streetProps_.push_back(StreetProp{pp, StreetPropKind::BusShelter, faceRoadYaw, false, {}, 0.0f, pp, 0,0,0,0,0,0, var});
            const Vec2 binP = pp - nr.tangent * 3.8f;
            Vec2 bChk = binP;
            if (!realData_.isInWater(binP) && !realData_.isOnRoad(binP, 1.0f) && !realData_.resolveCircleBuildings(bChk, 0.6f)) {
                streetProps_.push_back(StreetProp{binP, StreetPropKind::TrashBin, faceRoadYaw, false, {}, 0.0f, binP, 0,0,0,0,0,0, var});
            }
        }
    }

    // 2) Dense, Precision-Aligned Korean Street Architecture along REAL curved OSM roads!
    //    (Streetlamps both sides, bridge lamps, intersection traffic signals + bollards + utility cabinets,
    //     overhead green highway gantries, boulevard pedestrian guardrails, central medians, benches,
    //     hydrants, recycling bins, street kiosks, cafe tables, Ddareungi bike racks & outer sidewalk trees!)
    std::vector<std::uint32_t> roadIds;
    realData_.queryChunkRoads(focus, 310.0f, roadIds);
    const DistrictType focusDist = districtAtWorld(focus);
    const TreeSpecies streetSpecies = (focusDist == DistrictType::YeouidoFinance || focusDist == DistrictType::JamsilSongpa)
                                          ? TreeSpecies::CherryBlossom
                                          : TreeSpecies::GinkgoDeciduous;

    int gantryCount = 0;
    std::vector<Vec2> signalPositions;
    signalPositions.reserve(64);
    auto hasSignalNear = [&](const Vec2& p, float r) -> bool {
        const float r2 = r * r;
        for (const Vec2& sp : signalPositions) {
            if ((sp - p).lengthSq() < r2) return true;
        }
        return false;
    };

    for (std::uint32_t ri : roadIds) {
        const OsmRoadRec& r = realData_.roads()[ri];
        if (r.roadClass > 4 || (r.flags & 4u) != 0u || r.vertCount < 2) continue; // Skip tunnels & tiny back alleys
        const bool isBridge = (r.flags & 2u) != 0u;
        const bool isOneway = (r.flags & 1u) != 0u;
        const float hw = std::max(2.6f, r.halfWidth);
        const auto& verts = realData_.roadVerts();

        float distAccum = 0.0f;
        float nextLamp = 10.0f + static_cast<float>(ri % 7u) * 1.8f;
        float nextTree = 7.0f + static_cast<float>((ri * 5u) % 9u) * 1.6f;
        float nextGuard = 12.0f;
        float nextMedian = 6.0f;
        float nextFurn = 15.0f + static_cast<float>((ri * 11u) % 13u) * 1.5f;
        float nextGantry = 32.0f + static_cast<float>((ri * 17u) % 60u);
        float nextSignal = 87.5f; // 10.0 + 87.5 = 97.5m -> right beside 96m shader crosswalk (sM % 96 in [3.6..7.8])!

        for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
            const Vec2 a = verts[r.firstVert + s];
            const Vec2 b = verts[r.firstVert + s + 1];
            const Vec2 e = b - a;
            const float segLen = e.length();
            if (segLen < 2.5f) {
                distAccum += segLen;
                continue;
            }
            const Vec2 tang = e / segLen;
            const Vec2 rightN{-tang.z, tang.x};
            const float segEndDist = distAccum + segLen;

            // Fast segment distance cull (check closest point on segment to focus, NOT just midpoint, so 700m Teheran-ro never culls!)
            const float tFocus = clampf((focus - a).dot(e) / (segLen * segLen), 0.0f, 1.0f);
            const Vec2 closestSeg = a + e * tFocus;
            if ((closestSeg - focus).lengthSq() > 320.0f * 320.0f) {
                distAccum = segEndDist;
                continue;
            }

            // Compute exact visible [walkStart .. walkEnd] window along long segments (saves CPU on 700m boulevards!)
            const float focusAlong = tFocus * segLen;
            const float walkMin = std::max(distAccum, distAccum + focusAlong - 260.0f);
            const float walkMax = std::min(segEndDist, distAccum + focusAlong + 260.0f);
            auto advanceToWindow = [&](float& cursor, float step) {
                if (cursor < walkMin) {
                    const float kSteps = std::ceil((walkMin - cursor) / step);
                    cursor += kSteps * step;
                }
            };

            // A) Han River Bridge Overhead Lamp Posts every 28m along outer bridge parapets!
            if (isBridge) {
                advanceToWindow(nextLamp, 28.0f);
                while (nextLamp < walkMax) {
                    const float t = clampf((nextLamp - distAccum) / segLen, 0.0f, 1.0f);
                    const Vec2 cPt = a + e * t;
                    for (float side : {-1.0f, 1.0f}) {
                        const Vec2 lp = cPt + rightN * (side * (hw + 0.15f));
                        // Place lamps only on the outer parapet (skip inner median gap between twin bridge decks)
                        const Vec2 outerProbe = cPt + rightN * (side * (hw + 2.8f));
                        if (!realData_.isOnRoad(outerProbe, 0.4f) && !hasPropNearby(lp, 4.0f)) {
                            const Vec2 armDir = rightN * (-side);
                            const float armYaw = std::atan2(armDir.z, armDir.x);
                            streetProps_.push_back(StreetProp{lp, StreetPropKind::StreetLight, armYaw, false, {}, 0.0f, lp, 0,0,0,0,0,0, 1});
                        }
                    }
                    nextLamp += 28.0f;
                }
                distAccum = segEndDist;
                continue;
            }

            // B) Overhead Korean Green Highway Directional Gantries on major boulevards (roadClass <= 2)!
            if (r.roadClass <= 2 && hw >= 6.5f && gantryCount < 22) {
                advanceToWindow(nextGantry, 155.0f);
                while (nextGantry < walkMax && gantryCount < 22) {
                    const float t = clampf((nextGantry - distAccum) / segLen, 0.0f, 1.0f);
                    const Vec2 gp = a + e * t;
                    if (!realData_.isInWater(gp) && (gp - focus).lengthSq() < 260.0f * 260.0f &&
                        !hasPropNearby(gp, 35.0f)) {
                        const float gYaw = std::atan2(rightN.z, rightN.x);
                        // variant stores encoded road span half-width in decimeters (clamped 60..115 -> 6.0..11.5m!)
                        const std::uint8_t spanDm = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround((hw + 0.6f) * 10.0f)), 60, 115));
                        streetProps_.push_back(StreetProp{gp, StreetPropKind::RoadGantry, gYaw, false, {}, 0.0f, gp, 0,0,0,0,0,0, spanDm});
                        ++gantryCount;
                    }
                    nextGantry += 155.0f;
                }
            }

            // C) Crosswalk & Intersection 4-Aspect Korean Traffic Signals + Corner Bollards + KEPCO Signal Cabinets!
            auto tryPlaceSignalAt = [&](const Vec2& jPt, float minSignalSep) {
                if ((jPt - focus).lengthSq() >= 250.0f * 250.0f || hasSignalNear(jPt, minSignalSep)) return;
                for (float side : {1.0f, -1.0f}) {
                    const Vec2 sigPos = jPt + rightN * (side * (hw + 1.15f));
                    Vec2 test = sigPos;
                    if (!realData_.isInWater(sigPos) && !realData_.isOnRoad(sigPos, 0.55f) &&
                        !realData_.resolveCircleBuildings(test, 0.75f)) {
                        const Vec2 armDir = rightN * (-side);
                        const float armYaw = std::atan2(armDir.z, armDir.x);
                        streetProps_.push_back(StreetProp{sigPos, StreetPropKind::TrafficLight, armYaw, false, {}, 0.0f, sigPos});
                        signalPositions.push_back(sigPos);

                        // Corner pedestrian safety bollards (2 crisp steel posts flanking the crosswalk curb!)
                        for (int bi : {-1, 1}) {
                            const Vec2 bp = sigPos + tang * (static_cast<float>(bi) * 1.45f) - rightN * (side * 0.30f);
                            Vec2 bChk = bp;
                            if (!realData_.isOnRoad(bp, 0.30f) && !realData_.resolveCircleBuildings(bChk, 0.35f)) {
                                streetProps_.push_back(StreetProp{bp, StreetPropKind::Bollard, armYaw, false, {}, 0.0f, bp});
                            }
                        }
                        // KEPCO traffic signal / power control cabinet neatly set back on outer sidewalk!
                        const Vec2 uPos = sigPos + rightN * (side * 1.15f) + tang * 2.4f;
                        Vec2 uChk = uPos;
                        if (!realData_.isOnRoad(uPos, 1.10f) && !realData_.resolveCircleBuildings(uChk, 0.65f) &&
                            !hasPropNearby(uPos, 2.0f)) {
                            streetProps_.push_back(StreetProp{uPos, StreetPropKind::UtilityBox, armYaw, false, {}, 0.0f, uPos});
                        }
                        break;
                    }
                }
            };

            // C1) Synchronized 96m Crosswalk Traffic Signals on major boulevards (matches shader crosswalk zebra!)
            if (r.roadClass <= 2) {
                advanceToWindow(nextSignal, 96.0f);
                while (nextSignal < walkMax) {
                    const float t = clampf((nextSignal - distAccum) / segLen, 0.0f, 1.0f);
                    tryPlaceSignalAt(a + e * t, 36.0f);
                    nextSignal += 96.0f;
                }
            }
            // C2) Road intersection endpoint signals (backed off past perpendicular crossing boulevard!)
            if (r.roadClass <= 3 && (s == 0 || s + 2 == r.vertCount)) {
                const float backOff = std::min(15.5f, segLen * 0.36f);
                const Vec2 jPt = (s == 0) ? (a + tang * backOff) : (b - tang * backOff);
                tryPlaceSignalAt(jPt, 38.0f);
            }

            // D) Dense Seoul LED Streetlamps on BOTH outer sidewalks (every 24m on boulevards, 30m on collectors)!
            const float lampStep = (r.roadClass <= 2) ? 24.0f : 30.0f;
            advanceToWindow(nextLamp, lampStep);
            while (nextLamp < walkMax) {
                const float t = clampf((nextLamp - distAccum) / segLen, 0.0f, 1.0f);
                const Vec2 cPt = a + e * t;
                for (float side : {-1.0f, 1.0f}) {
                    // On smaller roads alternate sides; on major boulevards place on BOTH outer sidewalks!
                    if (r.roadClass >= 3 && (((static_cast<int>(nextLamp / lampStep) & 1) == 0) != (side > 0.0f))) continue;
                    const Vec2 lp = cPt + rightN * (side * (hw + 0.95f));
                    Vec2 test = lp;
                    if (!realData_.isInWater(lp) && !realData_.isOnRoad(lp, 0.60f) &&
                        !realData_.resolveCircleBuildings(test, 0.65f) && !hasPropNearby(lp, 4.5f)) {
                        const Vec2 armDir = rightN * (-side);
                        const float armYaw = std::atan2(armDir.z, armDir.x);
                        // Major boulevards get decorative Korean street banner flags (variant = 2)!
                        const std::uint8_t lVar = (r.roadClass <= 2) ? 2u : 0u;
                        streetProps_.push_back(StreetProp{lp, StreetPropKind::StreetLight, armYaw, false, {}, 0.0f, lp, 0,0,0,0,0,0, lVar});
                    }
                }
                nextLamp += lampStep;
            }

            // E) Central Boulevard Median Barriers on twin one-way carriageways (NEVER stray trees on asphalt!)
            if (r.roadClass <= 2 && isOneway) {
                advanceToWindow(nextMedian, 9.0f);
                while (nextMedian < walkMax) {
                    const float t = clampf((nextMedian - distAccum) / segLen, 0.0f, 1.0f);
                    const Vec2 cPt = a + e * t;
                    // Probe the left (inner median) side (-rightN): if opposite carriageway lies 1.5..6.8m away, place median curb!
                    const Vec2 medPos = cPt - rightN * (hw + 0.55f);
                    const Vec2 oppProbe = cPt - rightN * (hw + 4.2f);
                    if (!realData_.isInWater(medPos) && !realData_.isOnRoad(medPos, 0.15f) &&
                        realData_.isOnRoad(oppProbe, 1.2f) && !hasPropNearby(medPos, 6.2f)) {
                        const float mYaw = std::atan2(tang.z, tang.x);
                        streetProps_.push_back(StreetProp{medPos, StreetPropKind::MedianBarrier, mYaw, false, {}, 0.0f, medPos});
                    }
                    nextMedian += 9.0f;
                }
            }

            // F) Continuous Modular Korean Boulevard Pedestrian Guardrails (7.5m sections along outer curb!)
            if (r.roadClass <= 2) {
                advanceToWindow(nextGuard, 10.5f);
                while (nextGuard < walkMax) {
                    const float t = clampf((nextGuard - distAccum) / segLen, 0.0f, 1.0f);
                    // Leave natural crosswalk gaps synchronized 1:1 with shader v0 = 10.0 + dist (cwMod in [0..11.5])!
                    const float cwMod = std::fmod(10.0f + nextGuard, 96.0f);
                    if (cwMod > 12.0f && cwMod < 92.0f) {
                        const Vec2 cPt = a + e * t;
                        for (float side : {-1.0f, 1.0f}) {
                            const Vec2 gp = cPt + rightN * (side * (hw + 0.58f));
                            const Vec2 gA = gp - tang * 3.5f;
                            const Vec2 gB = gp + tang * 3.5f;
                            Vec2 tG = gp;
                            if (!realData_.isInWater(gp) &&
                                !realData_.isOnRoad(gp, 0.35f) &&
                                !realData_.isOnRoad(gA, 0.35f) &&
                                !realData_.isOnRoad(gB, 0.35f) &&
                                !realData_.resolveCircleBuildings(tG, 0.55f) &&
                                !hasPropNearby(gp, 4.2f)) {
                                const float gYaw = std::atan2(tang.z, tang.x);
                                streetProps_.push_back(StreetProp{gp, StreetPropKind::PedGuardrail, gYaw, false, {}, 0.0f, gp});
                            }
                        }
                    }
                    nextGuard += 10.5f;
                }
            }

            // G) Outer Sidewalk Street Trees — STRICTLY >= 1.85m CLEAR OF ALL ASPHALT (ZERO trees on roads!)
            const float treeStep = (r.roadClass <= 2) ? 21.0f : 26.0f;
            advanceToWindow(nextTree, treeStep);
            while (nextTree < walkMax) {
                const float t = clampf((nextTree - distAccum) / segLen, 0.0f, 1.0f);
                const Vec2 cPt = a + e * t;
                for (float side : {-1.0f, 1.0f}) {
                    if (r.roadClass >= 3 && (((static_cast<int>(nextTree / treeStep) & 1) == 0) != (side < 0.0f))) continue;
                    const Vec2 tp = cPt + rightN * (side * (hw + 2.20f));
                    Vec2 test = tp;
                    if (!realData_.isInWater(tp) &&
                        !realData_.isOnRoad(tp, 1.85f) &&
                        !realData_.resolveCircleBuildings(test, 1.05f) &&
                        !hasTreeNearby(tp, 8.5f) &&
                        !hasPropNearby(tp, 2.4f)) {
                        const float gy = realData_.sampleDem(tp);
                        const float hScale = 2.85f + static_cast<float>((ri + s + static_cast<std::uint32_t>(nextTree)) % 5u) * 0.24f;
                        trees_.push_back(TreeProp{tp, gy, hScale, 2.25f, true, streetSpecies});
                    }
                }
                nextTree += treeStep;
            }

            // H) Rich Sidewalk Street Furniture Micro-Clusters (Benches, Hydrants, Dual Recycling Bins,
            //    Korean Street Kiosks, Ddareungi Bike Racks, Outdoor Cafe Tables & Utility Cabinets!)
            const float furnStep = (r.roadClass <= 2) ? 13.5f : 22.0f;
            advanceToWindow(nextFurn, furnStep);
            while (nextFurn < walkMax) {
                const float t = clampf((nextFurn - distAccum) / segLen, 0.0f, 1.0f);
                const int baseSlot = static_cast<int>(nextFurn / furnStep) + static_cast<int>(ri) * 3;
                const Vec2 cPt = a + e * t;
                for (float side : {-1.0f, 1.0f}) {
                    // Place on BOTH outer sidewalks along major Seoul boulevards, alternate on smaller collectors!
                    if (r.roadClass >= 3 && (((baseSlot & 1) == 0) != (side > 0.0f))) continue;
                    const int slot = baseSlot + (side > 0.0f ? 0 : 3);
                    const int fType = ((slot % 7) + 7) % 7;
                    const float setBack = (fType == 2 || fType == 4 || fType == 5) ? 2.95f : 1.85f;
                    const Vec2 fp = cPt + rightN * (side * (hw + setBack));
                    Vec2 chk = fp;
                    if (!realData_.isInWater(fp) &&
                        !realData_.isOnRoad(fp, 1.35f) &&
                        !realData_.resolveCircleBuildings(chk, 0.90f) &&
                        !hasPropNearby(fp, 2.8f) &&
                        !hasTreeNearby(fp, 2.1f)) {
                        const Vec2 toRoad = rightN * (-side);
                        const float faceYaw = std::atan2(-toRoad.x, toRoad.z);
                        StreetPropKind fKind = StreetPropKind::Bench;
                        switch (fType) {
                            case 0: fKind = StreetPropKind::Bench; break;
                            case 1: fKind = StreetPropKind::Hydrant; break;
                            case 2: fKind = StreetPropKind::StreetKiosk; break;
                            case 3: fKind = StreetPropKind::TrashBin; break;
                            case 4: fKind = StreetPropKind::DdareungiRack; break;
                            case 5: fKind = StreetPropKind::CafeTable; break;
                            default: fKind = StreetPropKind::UtilityBox; break;
                        }
                        streetProps_.push_back(StreetProp{fp, fKind, faceYaw, false, {}, 0.0f, fp, 0,0,0,0,0,0, static_cast<std::uint8_t>(slot & 0xFF)});
                    }
                }
                nextFurn += furnStep;
            }

            distAccum = segEndDist;
        }
    }

    // 3) Mount Real Illuminated 3D Korean Commercial Shopfront Signboards & Awnings directly onto
    //    ALL street-facing walls of nearby Real OSM buildings (Pass 0: major boulevards first, Pass 1: side streets)!
    {
        std::vector<std::uint32_t> bldgIds;
        realData_.queryChunkBuildings(focus, 195.0f, bldgIds);
        int signCount = 0;
        for (int pass = 0; pass < 2 && signCount < 180; ++pass) {
            const std::uint8_t maxRoadCls = (pass == 0) ? 3u : 5u;
            const float maxEdgeClear = (pass == 0) ? 28.0f : 16.0f;
            for (std::uint32_t bi : bldgIds) {
                if (signCount >= 180) break;
                const OsmBldgRec& b = realData_.buildings()[bi];
                if (b.vertCount < 3 || b.style == 5 || b.height < 5.5f) continue; // Skip Hanoks & tiny sheds
                if ((Vec2{b.cx, b.cz} - focus).lengthSq() > 190.0f * 190.0f) continue;

                // Compute polygon winding to guarantee outward wall normal
                float signed2 = 0.0f;
                for (std::uint16_t i = 0; i < b.vertCount; ++i) {
                    const Vec2 p0 = realData_.bldgWorldVert(b, i);
                    const Vec2 p1 = realData_.bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % b.vertCount));
                    signed2 += p0.x * p1.z - p1.x * p0.z;
                }
                const float windSign = (signed2 >= 0.0f) ? 1.0f : -1.0f;

                int wallsMounted = 0;
                for (std::uint16_t i = 0; i < b.vertCount && wallsMounted < 2 && signCount < 180; ++i) {
                    const Vec2 pA = realData_.bldgWorldVert(b, i);
                    const Vec2 pB = realData_.bldgWorldVert(b, static_cast<std::uint16_t>((i + 1) % b.vertCount));
                    const Vec2 edge = pB - pA;
                    const float eLen = edge.length();
                    if (eLen < 5.2f) continue;
                    const Vec2 outN = Vec2{edge.z / eLen, -edge.x / eLen} * windSign;
                    const Vec2 wallMid = (pA + pB) * 0.5f;
                    const NearestOsmRoad nr = realData_.findNearestRoad(wallMid, 42.0f);
                    if (!nr.found || nr.roadClass > maxRoadCls) continue;
                    if (pass == 1 && nr.roadClass <= 3u) continue; // Already handled in pass 0
                    const float edgeClear = nr.dist - std::max(2.6f, nr.halfWidth);
                    if (edgeClear < 0.25f || edgeClear > maxEdgeClear) continue;
                    const Vec2 toRoad = (nr.closestPt - wallMid).normalized();
                    if (outN.dot(toRoad) < 0.35f) continue;

                    // Local +Z faces outward normal outN -> (-sin yaw, cos yaw) = outN
                    const float signYaw = std::atan2(-outN.x, outN.z);
                    const int numMounts = (eLen >= 34.0f) ? 3 : ((eLen >= 18.0f) ? 2 : 1);
                    bool placedOnWall = false;
                    for (int mi = 0; mi < numMounts && signCount < 180; ++mi) {
                        const float frac = (static_cast<float>(mi) + 0.5f) / static_cast<float>(numMounts);
                        const Vec2 wallPt = pA + edge * frac;
                        const Vec2 mountPos = wallPt + outN * 0.28f;
                        if (!realData_.isOnRoad(mountPos, 0.25f) && !hasPropNearby(mountPos, 4.6f)) {
                            const std::uint8_t shopSlot = static_cast<std::uint8_t>(
                                8u + ((bi + static_cast<std::uint32_t>(i) * 3u + static_cast<std::uint32_t>(mi) * 5u) % 12u));
                            streetProps_.push_back(StreetProp{mountPos, StreetPropKind::ShopSign, signYaw, false, {}, 0.0f, mountPos, 0,0,0,0,0,0, shopSlot});
                            ++signCount;
                            placedOnWall = true;
                        }
                    }
                    if (placedOnWall) ++wallsMounted;
                }
            }
        }
    }

    // 4) Scatter lush natural Korean Pines & Ginkgos inside REAL OSM park triangles near focus!
    const auto& pTris = realData_.parkTris();
    for (std::size_t ti = 0; ti < pTris.size(); ++ti) {
        const OsmTri2D& tr = pTris[ti];
        const Vec2 c{(tr.x0 + tr.x1 + tr.x2) / 3.0f, (tr.z0 + tr.z1 + tr.z2) / 3.0f};
        if ((c - focus).lengthSq() > 320.0f * 320.0f) continue;
        const float area2 = std::abs((tr.x1 - tr.x0) * (tr.z2 - tr.z0) - (tr.x2 - tr.x0) * (tr.z1 - tr.z0));
        if (area2 < 90.0f) continue;
        const int count = std::min(5, 1 + static_cast<int>(area2 / 550.0f));
        Rng prng(hashChunkCoords(seed_, static_cast<int>(ti), 917));
        for (int k = 0; k < count; ++k) {
            float u = prng.nextFloat01();
            float v = prng.nextFloat01();
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            const float w = 1.0f - u - v;
            const Vec2 tp{tr.x0 * w + tr.x1 * u + tr.x2 * v, tr.z0 * w + tr.z1 * u + tr.z2 * v};
            Vec2 test = tp;
            if (!realData_.isInWater(tp) && !realData_.isOnRoad(tp, 2.0f) &&
                !realData_.resolveCircleBuildings(test, 1.25f) && !hasTreeNearby(tp, 5.5f)) {
                const float gy = realData_.sampleDem(tp);
                const TreeSpecies sp = (gy > 48.0f || prng.chance(0.45f)) ? TreeSpecies::KoreanPine : streetSpecies;
                trees_.push_back(TreeProp{tp, gy, prng.range(3.0f, 4.6f), prng.range(2.2f, 3.3f), false, sp});
            }
        }
    }
}

void CityMap::updateStreaming(const Vec2& focus, bool forceRebuild) {
    const int centerChunkX = static_cast<int>(std::floor(focus.x / kChunkSize));
    const int centerChunkZ = static_cast<int>(std::floor(focus.z / kChunkSize));

    if (!forceRebuild && centerChunkX == lastCenterChunkX_ && centerChunkZ == lastCenterChunkZ_) {
        return;
    }

    lastCenterChunkX_ = centerChunkX;
    lastCenterChunkZ_ = centerChunkZ;
    streamedCenter_ = focus;

    buildings_.clear();
    canopies_.clear();
    parks_.clear();
    trees_.clear();
    streetProps_.clear();

    // Always include macro Seoul landmarks first so collision & rendering work seamlessly everywhere
    buildings_.reserve(macroBuildings_.size() + (realData_.isLoaded() ? 8 : 320));
    for (const auto& mb : macroBuildings_) {
        buildings_.push_back(mb);
    }

    if (realData_.isLoaded()) {
        streamRealSeoulProps(focus);
        return;
    }

    for (int dz = -kStreamRadiusChunks; dz <= kStreamRadiusChunks; ++dz) {
        for (int dx = -kStreamRadiusChunks; dx <= kStreamRadiusChunks; ++dx) {
            generateChunk(centerChunkX + dx, centerChunkZ + dz);
        }
    }
}

void CityMap::queryBuildingsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const float r = radius;
    const float minX = center.x - r;
    const float maxX = center.x + r;
    const float minZ = center.z - r;
    const float maxZ = center.z + r;
    for (int i = 0; i < static_cast<int>(buildings_.size()); ++i) {
        const auto& fp = buildings_[static_cast<std::size_t>(i)].footprint;
        if (fp.max.x >= minX && fp.min.x <= maxX && fp.max.z >= minZ && fp.min.z <= maxZ) {
            outIndices.push_back(i);
        }
    }
}

void CityMap::queryTreesInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const float rSq = radius * radius;
    for (int i = 0; i < static_cast<int>(trees_.size()); ++i) {
        if ((trees_[static_cast<std::size_t>(i)].pos - center).lengthSq() <= rSq) {
            outIndices.push_back(i);
        }
    }
}

void CityMap::queryPropsInRadius(const Vec2& center, float radius, std::vector<int>& outIndices) const {
    outIndices.clear();
    const float rSq = radius * radius;
    for (int i = 0; i < static_cast<int>(streetProps_.size()); ++i) {
        if ((streetProps_[static_cast<std::size_t>(i)].pos - center).lengthSq() <= rSq) {
            outIndices.push_back(i);
        }
    }
}

const PointOfInterest* CityMap::findPoi(PoiKind kind) const {
    for (const auto& p : pois_) {
        if (p.kind == kind) return &p;
    }
    return nullptr;
}

const SeoulLandmark* CityMap::findLandmark(SeoulLandmarkKind kind) const {
    for (const auto& lm : landmarks_) {
        if (lm.kind == kind) return &lm;
    }
    return nullptr;
}

bool CityMap::resolveCircleWorld(Vec2& pos, float radius, Vec2* outHitNormal) const {
    bool hit = false;
    Vec2 accumNormal{0.0f, 0.0f};

    // 1) Outer Seoul Metropolitan Boundary (~30km x 25km)
    if (pos.x < kWorldMinX + radius) { pos.x = kWorldMinX + radius; accumNormal.x += 1.0f; hit = true; }
    if (pos.x > kWorldMaxX - radius) { pos.x = kWorldMaxX - radius; accumNormal.x -= 1.0f; hit = true; }
    if (pos.z < kWorldMinZ + radius) { pos.z = kWorldMinZ + radius; accumNormal.z += 1.0f; hit = true; }
    if (pos.z > kWorldMaxZ - radius) { pos.z = kWorldMaxZ - radius; accumNormal.z -= 1.0f; hit = true; }

    // 2) Han River Embankments & Bridge Guardrails
    if (realData_.isLoaded() && realData_.isInCoreBounds(pos)) {
        if (realData_.isInWater(pos) && !realData_.isOnBridge(pos, 2.0f)) {
            // Find nearest dry bank direction along 8 cardinal/diagonal rays
            bool escaped = false;
            for (float dist = radius + 2.0f; dist <= 28.0f && !escaped; dist += 3.0f) {
                for (int k = 0; k < 8; ++k) {
                    const float ang = static_cast<float>(k) * (kTwoPi / 8.0f);
                    const Vec2 dir{std::cos(ang), std::sin(ang)};
                    const Vec2 cand = pos + dir * dist;
                    if (!realData_.isInWater(cand)) {
                        pos = cand;
                        accumNormal += dir;
                        hit = true;
                        escaped = true;
                        break;
                    }
                }
            }
        }
        // Exact 2D Real-OSM polygon building collisions!
        Vec2 osmN{};
        if (realData_.resolveCircleBuildings(pos, radius, &osmN)) {
            accumNormal += osmN;
            hit = true;
        }
    } else {
        const SeoulBridge* br = findBridgeAt(pos, 2.5f);
        if (br != nullptr) {
            // Keep vehicle/pedestrian safely inside bridge side guardrails when over water!
            if (isInHanRiverWater(pos, -6.0f)) {
                const float railHalf = br->halfWidth - 0.8f - radius;
                const float dx = pos.x - br->centerX;
                if (dx > railHalf) {
                    pos.x = br->centerX + railHalf;
                    accumNormal.x -= 1.0f;
                    hit = true;
                } else if (dx < -railHalf) {
                    pos.x = br->centerX - railHalf;
                    accumNormal.x += 1.0f;
                    hit = true;
                }
            }
        } else if (isInHanRiverWater(pos, -radius)) {
            // Push gently back onto nearest dry Han River bank!
            const float cz = hanRiverCenterZ(pos.x);
            const float hw = hanRiverHalfWidth(pos.x) + radius;
            if (pos.z >= cz) {
                pos.z = cz + hw;
                accumNormal.z += 1.0f;
            } else {
                pos.z = cz - hw;
                accumNormal.z -= 1.0f;
            }
            hit = true;
        }
    }

    // 3) Static Streamed & Macro Building Collisions
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
    const float boundR = obb.halfExtents.length() + 1.0f;
    bool hit = false;

    // 1) World bounds
    if (obb.center.x - boundR < kWorldMinX) { outPush.x += kWorldMinX - (obb.center.x - boundR); hit = true; }
    if (obb.center.x + boundR > kWorldMaxX) { outPush.x += kWorldMaxX - (obb.center.x + boundR); hit = true; }
    if (obb.center.z - boundR < kWorldMinZ) { outPush.z += kWorldMinZ - (obb.center.z - boundR); hit = true; }
    if (obb.center.z + boundR > kWorldMaxZ) { outPush.z += kWorldMaxZ - (obb.center.z + boundR); hit = true; }

    // 2) Bridge guardrails, Han River water banks, and Real-OSM polygon building collisions
    if (realData_.isLoaded() && realData_.isInCoreBounds(obb.center)) {
        const Vec2 fwd = forwardFromHeading(obb.heading);
        const float rWheel = std::max(0.75f, obb.halfExtents.z * 0.94f);
        const float span = std::max(0.0f, obb.halfExtents.x - rWheel);
        const float offsets[3] = {0.0f, span, -span};
        for (int k = 0; k < 3; ++k) {
            Vec2 probe = obb.center + outPush + fwd * offsets[k];
            const Vec2 before = probe;
            if (realData_.resolveCircleBuildings(probe, rWheel)) {
                outPush += (probe - before);
                hit = true;
            }
        }
        Vec2 centerCheck = obb.center + outPush;
        if (realData_.isInWater(centerCheck) && !realData_.isOnBridge(centerCheck, 2.5f)) {
            const Vec2 before = centerCheck;
            if (resolveCircleWorld(centerCheck, rWheel)) {
                outPush += (centerCheck - before);
                hit = true;
            }
        }
    } else {
        const SeoulBridge* br = findBridgeAt(obb.center, 3.0f);
        if (br != nullptr) {
            if (isInHanRiverWater(obb.center, -8.0f)) {
                const float railHalf = br->halfWidth - 0.9f - obb.halfExtents.z;
                const float dx = obb.center.x - br->centerX;
                if (dx > railHalf) { outPush.x += (br->centerX + railHalf) - obb.center.x; hit = true; }
                else if (dx < -railHalf) { outPush.x += (br->centerX - railHalf) - obb.center.x; hit = true; }
            }
        } else if (isInHanRiverWater(obb.center, -obb.halfExtents.z)) {
            const float cz = hanRiverCenterZ(obb.center.x);
            const float hw = hanRiverHalfWidth(obb.center.x) + obb.halfExtents.z + 0.5f;
            if (obb.center.z >= cz) outPush.z += (cz + hw) - obb.center.z;
            else outPush.z += (cz - hw) - obb.center.z;
            hit = true;
        }
    }

    // 3) Streamed & macro buildings
    OBB2D shifted = obb;
    shifted.center += outPush;
    for (const auto& b : buildings_) {
        if (shifted.center.x + boundR < b.footprint.min.x || shifted.center.x - boundR > b.footprint.max.x ||
            shifted.center.z + boundR < b.footprint.min.z || shifted.center.z - boundR > b.footprint.max.z) {
            continue;
        }
        const OBB2D bObb{b.footprint.center(), b.footprint.halfExtents(), 0.0f};
        Vec2 mtv{};
        if (intersectOBB(shifted, bObb, mtv)) {
            outPush += mtv;
            shifted.center += mtv;
            hit = true;
        }
    }
    return hit;
}

float CityMap::raycastBuildings(const Vec2& origin, const Vec2& dir, float maxDist) const {
    float bestT = maxDist;
    bool hit = false;
    if (realData_.isLoaded()) {
        const float osmT = realData_.raycastBuildings2D(origin, dir, bestT);
        if (osmT >= 0.0f && osmT < bestT) {
            bestT = osmT;
            hit = true;
        }
    }
    const Vec2 invDir{
        (std::abs(dir.x) > 1e-6f) ? (1.0f / dir.x) : 1e30f,
        (std::abs(dir.z) > 1e-6f) ? (1.0f / dir.z) : 1e30f
    };

    for (const auto& b : buildings_) {
        const float tx1 = (b.footprint.min.x - origin.x) * invDir.x;
        const float tx2 = (b.footprint.max.x - origin.x) * invDir.x;
        const float tz1 = (b.footprint.min.z - origin.z) * invDir.z;
        const float tz2 = (b.footprint.max.z - origin.z) * invDir.z;

        const float tmin = std::max(std::min(tx1, tx2), std::min(tz1, tz2));
        const float tmax = std::min(std::max(tx1, tx2), std::max(tz1, tz2));
        if (tmax >= std::max(0.0f, tmin) && tmin < bestT) {
            bestT = std::max(0.0f, tmin);
            hit = true;
        }
    }
    return hit ? bestT : -1.0f;
}

float CityMap::raycastBuildings3D(const Vec3& origin, const Vec3& dir, float maxDist) const {
    float bestT = maxDist;
    bool hit = false;
    if (realData_.isLoaded()) {
        const float osmT = realData_.raycastBuildings3D(origin, dir, bestT);
        if (osmT >= 0.0f && osmT < bestT) {
            bestT = osmT;
            hit = true;
        }
    }
    const Vec3 invDir{
        (std::abs(dir.x) > 1e-6f) ? (1.0f / dir.x) : 1e30f,
        (std::abs(dir.y) > 1e-6f) ? (1.0f / dir.y) : 1e30f,
        (std::abs(dir.z) > 1e-6f) ? (1.0f / dir.z) : 1e30f
    };

    for (const auto& b : buildings_) {
        const float tx1 = (b.footprint.min.x - origin.x) * invDir.x;
        const float tx2 = (b.footprint.max.x - origin.x) * invDir.x;
        const float ty1 = (b.baseY - origin.y) * invDir.y;
        const float ty2 = (b.baseY + b.height + b.upperTierHeight - origin.y) * invDir.y;
        const float tz1 = (b.footprint.min.z - origin.z) * invDir.z;
        const float tz2 = (b.footprint.max.z - origin.z) * invDir.z;

        const float tmin = std::max({std::min(tx1, tx2), std::min(ty1, ty2), std::min(tz1, tz2)});
        const float tmax = std::min({std::max(tx1, tx2), std::max(ty1, ty2), std::max(tz1, tz2)});
        if (tmax >= std::max(0.0f, tmin) && tmin < bestT) {
            bestT = std::max(0.0f, tmin);
            hit = true;
        }
    }
    return hit ? bestT : -1.0f;
}

} // namespace oc
