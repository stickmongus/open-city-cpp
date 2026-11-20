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

bool CityMap::isOnYeouidoIsland(const Vec2& p) const {
    // Real ~4.0km x 1.2km tapered river island centered at (-4800, hanRiverCenterZ(-4800) + 190)
    const float cx = -4800.0f;
    const float cz = hanRiverCenterZ(cx) + 165.0f;
    const float nx = (p.x - cx) / 1920.0f;
    const float nz = (p.z - cz) / 560.0f;
    return (nx * nx + nz * nz) <= 1.0f;
}

bool CityMap::isInHanRiverWater(const Vec2& p, float margin) const {
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
    // Iconic twin-basin Seokchon Lake right south of Lotte World Tower (x=8904, z=2180)
    const float dx = (p.x - 8904.0f) / 460.0f;
    const float dz = (p.z - 2240.0f) / 145.0f;
    return (dx * dx + dz * dz) < 1.0f && std::abs(p.x - 8904.0f) > 22.0f;
}

bool CityMap::isInCheonggyecheon(const Vec2& p) const {
    // Scenic downtown Cheonggyecheon stream running E-W through Jongno/Euljiro
    return (p.x > -960.0f && p.x < 2400.0f && std::abs(p.z - (-4536.0f)) < 12.0f);
}

float CityMap::terrainElevationAt(const Vec2& p) const {
    // Flat inside Han River water & immediate riverside parks/islands
    if (isInHanRiverWater(p, -60.0f) || isOnYeouidoIsland(p)) {
        return 0.0f;
    }
    float elev = 0.0f;
    for (const auto& m : mountains_) {
        const float nx = (p.x - m.center.x) / m.radiusX;
        const float nz = (p.z - m.center.z) / m.radiusZ;
        const float r2 = nx * nx + nz * nz;
        if (r2 < 1.0f) {
            const float u = 1.0f - r2;
            const float ridge = u * u * (0.86f + 0.14f * std::sin(p.x * 0.0045f + p.z * 0.0038f));
            elev = std::max(elev, m.peakHeight * ridge);
        }
    }
    // Keep major urban valley corridors smooth and easily drivable
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

float CityMap::bridgeDeckElevationAt(const Vec2& p, float* outSlopeZ) const {
    if (outSlopeZ) *outSlopeZ = 0.0f;
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
    // Gentle mid-span crown (+1.2m arch at bridge center!)
    const float midSpan = (z1 - rampSpan) - (z0 + rampSpan);
    const float t = clampf((p.z - (z0 + rampSpan)) / std::max(1.0f, midSpan), 0.0f, 1.0f);
    const float arch = 1.25f * std::sin(t * kPi);
    if (outSlopeZ) *outSlopeZ = (1.25f * kPi / std::max(1.0f, midSpan)) * std::cos(t * kPi);
    return maxH + arch;
}

float CityMap::surfaceHeightAt(const Vec2& p) const {
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
        // Hannam (+25), Gangnam-daero (+29), Seongsu (+55), Yeongdong (+71), Jamsil (+106)
        return roadIdx == -133 || roadIdx == -94 || roadIdx == -70 || roadIdx == -55 ||
               roadIdx == -39  || roadIdx == -25 || roadIdx == -11 || roadIdx == -10 ||
               roadIdx == 7    || roadIdx == 25  || roadIdx == 29  || roadIdx == 39  ||
               roadIdx == 55   || roadIdx == 71  || roadIdx == 86  || roadIdx == 106 ||
               roadIdx == 126;
    }
    // E-W Major Boulevards:
    // Jongno (-55), Euljiro (-49), Teheran-ro (+35), Bongeunsa-ro (+25), Olympic-ro (+19), Nambu (+65)
    return roadIdx == -64 || roadIdx == -55 || roadIdx == -49 || roadIdx == -38 ||
           roadIdx == -20 || roadIdx == 14  || roadIdx == 19  || roadIdx == 25  ||
           roadIdx == 35  || roadIdx == 52  || roadIdx == 65;
}

bool CityMap::isOnRoad(const Vec2& p, float margin) const {
    if (findBridgeAt(p, margin) != nullptr) return true;
    if (isInHanRiverWater(p, -2.0f)) return false;
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

    // 2) 15 Real Drivable Han River Bridges aligned strictly to 84m N-S avenues!
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

    // 3) Iconic Real Seoul Macro Landmarks (Always visible on skyline & interactive Seoul Map!)
    landmarks_ = {
        {SeoulLandmarkKind::LotteWorldTower,     { 8946.0f,  1974.0f}, 555.0f, "Lotte World Tower (555m)", "롯데월드타워 (555m)",   DistrictType::JamsilSongpa},
        {SeoulLandmarkKind::NSeoulTower,         {  120.0f, -2420.0f}, 236.0f, "N Seoul Tower (Namsan)",   "N서울타워 (남산)",      DistrictType::NamsanItaewon},
        {SeoulLandmarkKind::GyeongbokgungPalace, { -924.0f, -5796.0f},  26.0f, "Gyeongbokgung Palace",     "경복궁 · 광화문",       DistrictType::GwanghwamunPalace},
        {SeoulLandmarkKind::Building63,          {-3318.0f,   462.0f}, 249.0f, "63 Square Tower",          "여의도 63빌딩",         DistrictType::YeouidoFinance},
        {SeoulLandmarkKind::NationalAssembly,    {-6090.0f,   210.0f},  58.0f, "National Assembly",        "국회의사당",            DistrictType::YeouidoFinance},
        {SeoulLandmarkKind::Parc1RedTower,       {-4578.0f,   294.0f}, 333.0f, "Yeouido Parc1 Tower",      "여의도 파크원",         DistrictType::YeouidoFinance},
        {SeoulLandmarkKind::CoexTradeTower,      { 5418.0f,  2898.0f}, 228.0f, "COEX & Trade Tower",       "삼성동 코엑스 · 무역센터", DistrictType::GangnamTeheran},
        {SeoulLandmarkKind::SeoulCityHall,       { -882.0f, -4326.0f},  72.0f, "Seoul City Hall",          "서울시청 · 서울광장",   DistrictType::MyeongdongCityHall},
        {SeoulLandmarkKind::SungnyemunGate,      {-1050.0f, -3696.0f},  22.0f, "Sungnyemun Gate",          "숭례문 (남대문)",       DistrictType::MyeongdongCityHall},
        {SeoulLandmarkKind::DongdaemunDDP,       { 1890.0f, -4326.0f},  29.0f, "Dongdaemun DDP",           "동대문 DDP",            DistrictType::Dongdaemun},
        {SeoulLandmarkKind::JamsilStadium,       { 6846.0f,  1638.0f},  44.0f, "Jamsil Olympic Stadium",   "잠실종합운동장",        DistrictType::JamsilSongpa},
        {SeoulLandmarkKind::GangnamStationPlaza, { 2436.0f,  2940.0f},  95.0f, "Gangnam Station",          "강남역 사거리",         DistrictType::GangnamTeheran}
    };

    // Build physical collision footprints for macro skyscrapers & landmarks
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
        macroBuildings_.push_back(b);
    };

    // Lotte World Tower (555m iconic pale silver-blue tapered skyscraper)
    addMacro({8946.0f, 1974.0f}, {28.0f, 28.0f}, 555.0f, 0.0f,
             {162, 184, 206, 255}, {214, 224, 236, 255}, BuildingStyle::SupertallSpire, false);
    // 63 Building (iconic gold-tinted Yeouido landmark)
    addMacro({-3318.0f, 462.0f}, {22.0f, 16.0f}, 249.0f, 0.0f,
             {174, 146, 84, 255}, {212, 186, 122, 255}, BuildingStyle::GlassTower, true);
    // Parc1 Red-Exoskeleton Twin Towers (Yeouido)
    addMacro({-4578.0f, 294.0f}, {24.0f, 22.0f}, 333.0f, 0.0f,
             {148, 164, 184, 255}, {178, 44, 42, 255}, BuildingStyle::GlassTower, true);
    addMacro({-4494.0f, 294.0f}, {22.0f, 20.0f}, 256.0f, 0.0f,
             {148, 164, 184, 255}, {178, 44, 42, 255}, BuildingStyle::GlassTower, true);
    // National Assembly Hall (Yeouido)
    addMacro({-6090.0f, 210.0f}, {44.0f, 32.0f}, 36.0f, 22.0f,
             {196, 192, 184, 255}, {76, 152, 144, 255}, BuildingStyle::CivicLandmark, false);
    // COEX Trade Tower & Gangnam Twin Towers
    addMacro({5418.0f, 2898.0f}, {24.0f, 24.0f}, 228.0f, 0.0f,
             {96, 118, 142, 255}, {172, 190, 210, 255}, BuildingStyle::GlassTower, true);
    addMacro({2478.0f, 2898.0f}, {24.0f, 22.0f}, 182.0f, 36.0f,
             {72, 92, 118, 255}, {154, 174, 196, 255}, BuildingStyle::GlassTower, true);
    // Gyeongbokgung Geunjeongjeon Palace Pavilion + Gwanghwamun Gate
    addMacro({-924.0f, -5796.0f}, {32.0f, 24.0f}, 18.0f, 8.0f,
             {162, 54, 44, 255}, {58, 116, 86, 255}, BuildingStyle::HanokPalace, false);
    addMacro({-924.0f, -5460.0f}, {24.0f, 9.0f}, 14.0f, 6.0f,
             {184, 178, 168, 255}, {62, 112, 86, 255}, BuildingStyle::HanokPalace, false);
    // Seoul City Hall & Sungnyemun
    addMacro({-882.0f, -4326.0f}, {34.0f, 24.0f}, 56.0f, 16.0f,
             {116, 144, 168, 255}, {198, 206, 216, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({-1050.0f, -3696.0f}, {16.0f, 10.0f}, 16.0f, 6.0f,
             {166, 160, 152, 255}, {58, 110, 82, 255}, BuildingStyle::HanokPalace, false);
    // Dongdaemun DDP & Jamsil Olympic Stadium
    addMacro({1890.0f, -4326.0f}, {44.0f, 26.0f}, 26.0f, 0.0f,
             {196, 202, 210, 255}, {132, 140, 150, 255}, BuildingStyle::CivicLandmark, false);
    addMacro({6846.0f, 1638.0f}, {62.0f, 52.0f}, 38.0f, 0.0f,
             {188, 186, 180, 255}, {122, 134, 152, 255}, BuildingStyle::CivicLandmark, false);

    // Seokchon Lake patch
    lakes_.push_back(LakePatch{{{8450.0f, 2095.0f}, {9358.0f, 2385.0f}}, 0.04f});

    // 4) Key Interactive POIs clustered cleanly near Default Spawn (Banpo/Gangnam Hangang Entrance: {588, 1020})
    //    so shops, showroom, garage, boutique, gas station & missions are immediately accessible!
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

void CityMap::generate(std::uint64_t seed) {
    seed_ = seed;
    buildSeoulMacroGeography();
    lastCenterChunkX_ = 999999;
    lastCenterChunkZ_ = 999999;
    updateStreaming(defaultSpawnPos(), true);
}

void CityMap::generateChunk(int chunkX, int chunkZ) {
    const int baseBx = chunkX * kBlocksPerChunk;
    const int baseBz = chunkZ * kBlocksPerChunk;
    const float halfRoad = kRoadWidth * 0.5f;

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

            // Skip blocks submerged in Han River or Seokchon Lake
            if (isInHanRiverWater(bc, -8.0f) || isInSeokchonLake(bc)) continue;
            // Skip if directly intersecting a bridge approach deck
            if (findBridgeAt(bc, 20.0f) != nullptr) continue;

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
            // High mountain ridges (> 34m elevation): dense Korean pine forests instead of city blocks!
            if (groundY > 34.0f) {
                Rng mrng(hashChunkCoords(seed_, bx * 13 + 7, bz * 17 + 3));
                parks_.push_back(ParkPatch{{{x0 - 4.0f, z0 - 4.0f}, {x1 + 4.0f, z1 + 4.0f}}, groundY, {48, 78, 42, 255}});
                for (int t = 0; t < 7; ++t) {
                    const Vec2 tp{mrng.range(x0, x1), mrng.range(z0, z1)};
                    trees_.push_back(TreeProp{
                        tp, terrainElevationAt(tp), mrng.range(3.2f, 5.2f), mrng.range(2.2f, 3.4f),
                        false, TreeSpecies::KoreanPine
                    });
                }
                continue;
            }

            Rng rng(hashChunkCoords(seed_, bx, bz));
            const DistrictType district = districtAtWorld(bc);

            // Han River Waterfront Parks (한강공원 — lawns, cherry blossoms & ginkgos, riverside benches!)
            if (district == DistrictType::HanRiverPark || isInHanRiverPark(bc)) {
                parks_.push_back(ParkPatch{{{x0 + 1.5f, z0 + 1.5f}, {x1 - 1.5f, z1 - 1.5f}}, groundY, {60, 96, 48, 255}});
                for (int t = 0; t < 6; ++t) {
                    const Vec2 tp{rng.range(x0 + 5.0f, x1 - 5.0f), rng.range(z0 + 5.0f, z1 - 5.0f)};
                    if (isInHanRiverWater(tp, -4.0f)) continue;
                    const TreeSpecies sp = rng.chance(0.45f) ? TreeSpecies::CherryBlossom : TreeSpecies::GinkgoDeciduous;
                    trees_.push_back(TreeProp{tp, terrainElevationAt(tp), rng.range(2.9f, 4.1f), rng.range(2.4f, 3.5f), false, sp});
                }
                streetProps_.push_back(StreetProp{{bc.x, bc.z}, StreetPropKind::Bench, 0.0f, false, {}, 0.0f, {bc.x, bc.z}});
                continue;
            }

            // Special Hero Spawn Block near Banpo/Gangnam (bx == 7, bz == 12 -> x in [597..663], z in [1017..1083])
            if (bx == 7 && bz == 12) {
                // Gangnam Exotic Supercar Glass Showroom
                buildings_.push_back(Building{
                    {{x0 + 8.0f, z0 + 26.0f}, {x1 - 8.0f, z1 - 6.0f}},
                    groundY, 16.5f, 0.0f,
                    {38, 46, 58, 255}, {188, 160, 102, 255}, {154, 130, 82, 255},
                    BuildingStyle::DealershipBuilding, true, false, false, false, false
                });
                streetProps_.push_back(StreetProp{{x0 + 3.0f, z0 + 3.0f}, StreetPropKind::StreetLight, 0.0f, false, {}, 0.0f, {x0 + 3.0f, z0 + 3.0f}});
                continue;
            }
            if (bx == 6 && bz == 12) {
                // Seoul Auto Customs Garage
                buildings_.push_back(Building{
                    {{x0 + 6.0f, z0 + 24.0f}, {x1 - 6.0f, z1 - 6.0f}},
                    groundY, 13.5f, 0.0f,
                    {112, 118, 126, 255}, {76, 162, 198, 255}, {44, 102, 134, 255},
                    BuildingStyle::GarageBuilding, true, false, false, false, false
                });
                continue;
            }
            if (bx == 8 && bz == 12) {
                // Seoul Energy Gas Station Canopy
                canopies_.push_back(GasStationCanopy{{{x0 + 10.0f, z0 + 18.0f}, {x1 - 10.0f, z0 + 38.0f}}, groundY, 4.9f});
                buildings_.push_back(Building{
                    {{x0 + 12.0f, z1 - 18.0f}, {x1 - 12.0f, z1 - 4.0f}},
                    groundY, 6.2f, 0.0f,
                    {192, 194, 198, 255}, {198, 62, 48, 255}, {198, 62, 48, 255},
                    BuildingStyle::ShopBuilding, true, true, false, false, false
                });
                continue;
            }

            // Neighborhood pocket parks (~8% of urban blocks)
            if (rng.chance(0.08f)) {
                parks_.push_back(ParkPatch{{{x0 + 2.5f, z0 + 2.5f}, {x1 - 2.5f, z1 - 2.5f}}, groundY, {62, 94, 48, 255}});
                for (int t = 0; t < 7; ++t) {
                    const Vec2 tp{rng.range(x0 + 6.0f, x1 - 6.0f), rng.range(z0 + 6.0f, z1 - 6.0f)};
                    const TreeSpecies sp = (district == DistrictType::YeouidoFinance || district == DistrictType::JamsilSongpa)
                                               ? TreeSpecies::CherryBlossom
                                               : (rng.chance(0.35f) ? TreeSpecies::KoreanPine : TreeSpecies::GinkgoDeciduous);
                    trees_.push_back(TreeProp{tp, terrainElevationAt(tp), rng.range(2.9f, 4.2f), rng.range(2.3f, 3.4f), false, sp});
                }
                continue;
            }

            // Authentic Seoul Urban Typologies per District!
            const bool isHighRiseCbd = (district == DistrictType::GangnamTeheran ||
                                        district == DistrictType::YeouidoFinance ||
                                        district == DistrictType::MyeongdongCityHall ||
                                        district == DistrictType::JongnoCheonggye ||
                                        district == DistrictType::Yongsan);
            const bool isKoreanAptZone = (district == DistrictType::SeochoBanpo ||
                                          district == DistrictType::JamsilSongpa ||
                                          district == DistrictType::GangbukNowon ||
                                          district == DistrictType::GangseoMagok ||
                                          district == DistrictType::GuroGwanak);
            const bool isTraditionalZone = (district == DistrictType::GwanghwamunPalace);
            const bool isTrendyBrick = (district == DistrictType::HongdaeMapo ||
                                        district == DistrictType::SeongsuKonkuk ||
                                        district == DistrictType::Dongdaemun);

            if (isKoreanAptZone && rng.chance(0.66f)) {
                // Iconic Korean Seoul Apartment Complex (대단지 아파트 — twin parallel tall slab buildings + landscaped courtyard!)
                const bool eastWest = rng.chance(0.55f);
                const float aptH = rng.range(42.0f, 78.0f);
                const ColorRGBA aptPal[4] = {
                    {192, 190, 184, 255}, {178, 182, 188, 255},
                    {198, 188, 174, 255}, {170, 174, 178, 255}
                };
                const ColorRGBA aptAcc[4] = {
                    {58, 86, 122, 255}, {132, 68, 56, 255},
                    {76, 102, 82, 255}, {108, 92, 74, 255}
                };
                const int pIdx = rng.rangeInt(0, 3);
                parks_.push_back(ParkPatch{{{x0 + 3.0f, z0 + 3.0f}, {x1 - 3.0f, z1 - 3.0f}}, groundY, {64, 92, 50, 255}});
                for (int s = 0; s < 2; ++s) {
                    AABB2D fp{};
                    if (eastWest) {
                        const float sz = (s == 0) ? (z0 + 14.0f) : (z1 - 14.0f);
                        fp = {{x0 + 7.0f, sz - 7.5f}, {x1 - 7.0f, sz + 7.5f}};
                    } else {
                        const float sx = (s == 0) ? (x0 + 14.0f) : (x1 - 14.0f);
                        fp = {{sx - 7.5f, z0 + 7.0f}, {sx + 7.5f, z1 - 7.0f}};
                    }
                    buildings_.push_back(Building{
                        fp, groundY, aptH, 0.0f,
                        aptPal[pIdx], aptAcc[pIdx], aptAcc[pIdx],
                        BuildingStyle::KoreanApartment, true, false, true, aptH > 64.0f, false
                    });
                }
                trees_.push_back(TreeProp{{bc.x - 9.0f, bc.z}, groundY, 3.2f, 2.5f, false, TreeSpecies::GinkgoDeciduous});
                trees_.push_back(TreeProp{{bc.x + 9.0f, bc.z}, groundY, 3.2f, 2.5f, false, TreeSpecies::KoreanPine});
            } else {
                // 2x2 Quadrant Seoul Commercial / Office / Hanok / Villa Parcel Layout
                const float midX = bc.x;
                const float midZ = bc.z;
                for (int qz = 0; qz < 2; ++qz) {
                    for (int qx = 0; qx < 2; ++qx) {
                        const float px0 = (qx == 0) ? (x0 + 3.5f) : (midX + 2.2f);
                        const float px1 = (qx == 0) ? (midX - 2.2f) : (x1 - 3.5f);
                        const float pz0 = (qz == 0) ? (z0 + 3.5f) : (midZ + 2.2f);
                        const float pz1 = (qz == 0) ? (midZ - 2.2f) : (z1 - 3.5f);

                        Building b{};
                        b.footprint = {{px0, pz0}, {px1, pz1}};
                        b.baseY = groundY;
                        b.litWindows = true;

                        if (isHighRiseCbd) {
                            const bool glass = rng.chance(0.65f);
                            b.style = glass ? BuildingStyle::GlassTower : BuildingStyle::ModernOffice;
                            b.height = glass ? rng.range(48.0f, 138.0f) : rng.range(26.0f, 68.0f);
                            b.upperTierHeight = (b.height > 78.0f && rng.chance(0.50f)) ? rng.range(12.0f, 26.0f) : 0.0f;
                            b.hasStorefront = true;
                            b.hasHelipad = (b.height > 88.0f);
                            const ColorRGBA gPal[4] = {
                                {48, 66, 88, 255}, {58, 74, 90, 255}, {76, 86, 98, 255}, {112, 118, 126, 255}
                            };
                            b.primaryColor = gPal[rng.rangeInt(0, 3)];
                            b.accentColor = {142, 162, 184, 255};
                            b.awningColor = {42, 66, 92, 255};
                        } else if (isTraditionalZone && rng.chance(0.55f)) {
                            b.style = BuildingStyle::HanokPalace;
                            b.height = rng.range(7.0f, 12.0f);
                            b.primaryColor = {206, 198, 182, 255};
                            b.accentColor = {68, 72, 78, 255};
                            b.awningColor = {138, 64, 48, 255};
                        } else if (district == DistrictType::NamsanItaewon && rng.chance(0.50f)) {
                            b.style = BuildingStyle::VillaHouse;
                            b.height = rng.range(9.5f, 16.5f);
                            b.primaryColor = {196, 186, 170, 255};
                            b.accentColor = {144, 74, 52, 255};
                            b.hasBalconies = true;
                        } else if (isTrendyBrick) {
                            b.style = rng.chance(0.65f) ? BuildingStyle::BrickCommercial : BuildingStyle::ModernOffice;
                            b.height = rng.range(16.0f, 44.0f);
                            const ColorRGBA bPal[4] = {
                                {146, 96, 78, 255}, {126, 118, 110, 255}, {162, 148, 132, 255}, {84, 90, 98, 255}
                            };
                            b.primaryColor = bPal[rng.rangeInt(0, 3)];
                            b.accentColor = {184, 174, 160, 255};
                            b.awningColor = rng.chance(0.5f) ? ColorRGBA{46, 86, 72, 255} : ColorRGBA{142, 56, 48, 255};
                            b.hasStorefront = true;
                        } else {
                            b.style = rng.chance(0.45f) ? BuildingStyle::ModernOffice : BuildingStyle::BrickCommercial;
                            b.height = rng.range(18.0f, 46.0f);
                            b.primaryColor = {138, 140, 144, 255};
                            b.accentColor = {174, 178, 184, 255};
                            b.hasStorefront = true;
                        }
                        buildings_.push_back(b);
                    }
                }
            }

            // Seoul Streetscape: Ginkgo / Cherry Blossom street trees + streetlights along block corners!
            const TreeSpecies stTree = (district == DistrictType::YeouidoFinance || district == DistrictType::JamsilSongpa)
                                           ? TreeSpecies::CherryBlossom
                                           : TreeSpecies::GinkgoDeciduous;
            const Vec2 cornerA{x0 + 1.8f, z0 + 1.8f};
            const Vec2 cornerB{x1 - 1.8f, z1 - 1.8f};
            streetProps_.push_back(StreetProp{cornerA, StreetPropKind::StreetLight, 0.0f, false, {}, 0.0f, cornerA});
            streetProps_.push_back(StreetProp{cornerB, StreetPropKind::StreetLight, kPi, false, {}, 0.0f, cornerB});
            const Vec2 hydPos{x0 + 0.9f, z0 + 5.5f};
            if (rng.chance(0.45f)) {
                streetProps_.push_back(StreetProp{hydPos, StreetPropKind::Hydrant, 0.0f, false, {}, 0.0f, hydPos});
            }
            trees_.push_back(TreeProp{{bc.x, z0 + 1.9f}, groundY, 3.1f, 2.3f, true, stTree});
            trees_.push_back(TreeProp{{x0 + 1.9f, bc.z}, groundY, 3.1f, 2.3f, true, stTree});
            if (rng.chance(0.42f)) {
                const Vec2 bsPos{bc.x + 12.0f, z1 - 1.8f};
                streetProps_.push_back(StreetProp{bsPos, StreetPropKind::BusShelter, kPi, false, {}, 0.0f, bsPos});
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
    buildings_.reserve(macroBuildings_.size() + 320);
    for (const auto& mb : macroBuildings_) {
        buildings_.push_back(mb);
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

    // 2) Bridge guardrails & Han River water banks
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
