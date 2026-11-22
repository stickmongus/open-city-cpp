#include "gameplay/GameWorld.hpp"
#include "physics/PhysicsBridge.hpp"
#include "render/GeneratedRealisticAssets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace oc {

namespace {

constexpr std::array<VehicleSpec, static_cast<std::size_t>(VehicleKind::Count)> kVehicleSpecs = {{
    {VehicleKind::SportsCoupe,     "APEX GT",           "에이펙스 GT 스포츠카",    2.28f, 1.03f, 1.14f, 44.5f, 15.0f, 28.0f, 36.0f, 2.75f, 10.2f, 2.25f, 1340.0f, 175.0f, true,   4800},
    {VehicleKind::MuscleCar,       "DOMINATOR V8",      "도미네이터 V8 머슬카",    2.38f, 1.04f, 1.18f, 41.0f, 14.0f, 26.5f, 32.0f, 2.65f, 8.4f,  1.85f, 1540.0f, 195.0f, false,  3800},
    {VehicleKind::SUV,             "CAVALCADE XL",      "캐벌케이드 XL SUV",       2.48f, 1.12f, 1.54f, 35.0f, 13.0f, 21.5f, 30.0f, 2.35f, 9.2f,  2.50f, 2150.0f, 255.0f, false,  3200},
    {VehicleKind::Taxi,            "SEOUL HAECHI TAXI", "서울 해치 택시",          2.26f, 1.00f, 1.26f, 36.5f, 13.5f, 22.0f, 32.0f, 2.60f, 9.4f,  2.20f, 1450.0f, 175.0f, false,     0},
    {VehicleKind::Compact,         "BLISTA HATCH",      "블리스타 핫해치",         1.98f, 0.96f, 1.22f, 35.0f, 13.5f, 23.5f, 33.0f, 2.95f, 9.8f,  2.15f, 1180.0f, 150.0f, false,  1200},
    {VehicleKind::DeliveryVan,     "SEOUL CARGO VAN",   "서울 카고 밴",            2.68f, 1.14f, 1.86f, 29.5f, 11.0f, 17.5f, 28.0f, 2.05f, 8.8f,  2.75f, 2550.0f, 290.0f, false,     0},
    {VehicleKind::Hypercar,        "ZENTORNO X",        "젠토르노 X 하이퍼카",     2.36f, 1.08f, 1.05f, 54.5f, 16.5f, 34.5f, 41.0f, 2.90f, 11.4f, 2.35f, 1280.0f, 210.0f, true,  22000},
    {VehicleKind::ArmoredSuv,      "INSURGENT VIP",     "인서전트 VIP 방탄차",     2.58f, 1.16f, 1.66f, 37.5f, 14.0f, 24.5f, 34.0f, 2.30f, 9.6f,  2.40f, 3400.0f, 440.0f, false, 12000},
    {VehicleKind::LuxurySedan,     "GENESIS G90 S",     "제네시스 럭셔리 세단",    2.46f, 1.04f, 1.24f, 43.5f, 14.5f, 26.5f, 35.0f, 2.55f, 10.0f, 2.15f, 1780.0f, 225.0f, false,  6500},
    {VehicleKind::PoliceCruiser,   "SEOUL POLICE PATROL","서울 경찰 순찰차",       2.38f, 1.04f, 1.24f, 40.5f, 15.0f, 25.5f, 36.0f, 2.65f, 10.0f, 2.25f, 1650.0f, 230.0f, false,     0},
    {VehicleKind::ClassicRoadster, "COQUETTE ROADSTER", "코케트 오픈 로드스터",    2.22f, 1.02f, 1.02f, 46.5f, 15.5f, 29.5f, 37.0f, 2.95f, 10.6f, 2.20f, 1220.0f, 165.0f, false,  5600},
    {VehicleKind::OffroadPickup,   "SANDKING 4X4",      "샌드킹 4x4 픽업트럭",     2.72f, 1.15f, 1.68f, 39.5f, 14.5f, 25.5f, 33.0f, 2.45f, 9.7f,  2.30f, 2480.0f, 320.0f, false,  4400},
    {VehicleKind::JdmTuner,        "SULTAN RS TUNER",   "설탄 RS 드리프트 튜너",   2.26f, 1.05f, 1.16f, 48.5f, 15.5f, 31.0f, 38.0f, 3.05f, 10.1f, 1.98f, 1290.0f, 185.0f, true,   7800},
    {VehicleKind::TrackPrototype,  "VALKYRIE LMP-R",    "발키리 LMP 트랙카",       2.44f, 1.10f, 1.00f, 58.5f, 17.5f, 37.5f, 45.0f, 3.10f, 12.6f, 2.45f, 1160.0f, 225.0f, true,  34000},
    {VehicleKind::Ambulance,       "SEOUL 119 EMS",     "서울 119 구급차",         2.76f, 1.15f, 1.90f, 38.0f, 13.0f, 22.5f, 32.0f, 2.20f, 9.2f,  2.60f, 2750.0f, 330.0f, false,     0},
    {VehicleKind::FireTruck,       "SEOUL 119 FIRE",    "서울 119 소방차",         3.08f, 1.18f, 1.96f, 36.5f, 12.0f, 21.0f, 31.0f, 2.00f, 9.0f,  2.70f, 4200.0f, 520.0f, false,     0},
    {VehicleKind::SeoulBus,        "SEOUL CITY BUS",    "서울 시내버스",           4.55f, 1.24f, 2.48f, 27.5f, 10.5f, 14.5f, 24.0f, 1.72f, 8.2f,  2.85f, 9800.0f, 640.0f, false,     0}
}};

constexpr std::array<WeaponSpec, static_cast<std::size_t>(WeaponId::Count)> kWeaponSpecs = {{
    {WeaponId::Fists,         "Fists",            "맨주먹 격투",       0.36f,   2.4f,  28.0f, 1, 0.080f,   0, 0.00f, false, false},
    {WeaponId::Pistol,        "9mm Pistol",       "9mm 권총",          0.22f,  68.0f,  35.0f, 1, 0.010f,  12, 1.05f, false, false},
    {WeaponId::HeavyRevolver, ".44 Magnum",       "44 매그넘 리볼버",  0.42f,  84.0f,  88.0f, 1, 0.006f,   6, 1.45f, false, false},
    {WeaponId::SMG,           "Tactical SMG",     "전술 기관단총",     0.085f, 58.0f,  23.0f, 1, 0.030f,  30, 1.25f, true,  false},
    {WeaponId::Shotgun,       "Pump Shotgun",     "펌프 샷건",         0.68f,  36.0f,  20.0f, 7, 0.095f,   8, 1.55f, false, false},
    {WeaponId::AutoShotgun,   "Assault Shotgun",  "전자동 드럼 샷건",  0.23f,  38.0f,  18.0f, 6, 0.082f,  16, 1.75f, true,  false},
    {WeaponId::AssaultRifle,  "Carbine Rifle",    "돌격 카빈소총",     0.105f, 88.0f,  32.0f, 1, 0.015f,  30, 1.40f, true,  false},
    {WeaponId::SniperRifle,   "Marksman Sniper",  "전술 저격소총",     0.68f, 195.0f, 148.0f, 1, 0.0012f,  8, 1.80f, false, false},
    {WeaponId::CombatMG,      "Combat MG",        "컴뱃 중기관총",     0.078f, 96.0f,  40.0f, 1, 0.018f,  80, 1.85f, true,  false},
    {WeaponId::Minigun,       "Vulcan Minigun",   "6열 개틀링 미니건", 0.042f,105.0f,  26.0f, 1, 0.022f, 200, 2.35f, true,  false},
    {WeaponId::HeavyLauncher, "Tactical Launcher","전술 폭발 런처",    0.95f, 115.0f, 185.0f, 1, 0.006f,   4, 2.10f, false, true}
}};

constexpr std::array<PropertySpec, static_cast<std::size_t>(PropertyId::Count)> kPropertySpecs = {{
    {PropertyId::SunsetCondo,      "Sunset Beach Condo",   "선셋 비치 오션뷰 콘도",   "Max HP +25",        "최대 체력 +25",           2500,  220, {-620.0f, -120.0f}},
    {PropertyId::GlobeOilGas,      "Globe Oil Station",    "글로브 오일 주유소",      "2x Nitro Regen",    "니트로 충전속도 2배",      5000,  480, {-126.0f,   26.5f}},
    {PropertyId::PayNTuneFranchise,"Pay 'n' Tune Customs", "페이 앤 튠 튜닝 본점",    "50% Off Tuning",    "차고 튜닝·수리 50% 할인",  8500,  850, { -28.5f,   19.0f}},
    {PropertyId::PulseNightclub,   "Pulse Neon Nightclub", "펄스 네온 나이트클럽",    "+25% Mission Cash", "미션 보상금 +25% 보너스", 15000, 1550, {  28.0f, -153.0f}},
    {PropertyId::LuxuryShowroom,   "Luxury Autos Stake",   "럭셔리 오토 전시장 지분", "+30% Car Armor",    "호출 차량 내구도 +30%",   25000, 2700, { 116.5f,   20.0f}},
    {PropertyId::EclipsePenthouse, "Eclipse Penthouse",    "이클립스 스카이 펜트하우스","HP/Armor 150",     "최대체력·방탄 150 & 무료치료", 45000, 5200, {  52.0f, -132.0f}}
}};

constexpr std::array<OutfitSpec, static_cast<std::size_t>(OutfitId::Count)> kOutfitSpecs = {{
    {OutfitId::StreetCasual,   "Street Jacket",    "스트리트 캐주얼 재킷",   0, { 44,  64,  92, 255}, { 34,  38,  48, 255}, {214, 218, 224, 255}, true,  false, false, false, 1.00f},
    {OutfitId::ViceWhiteSuit,  "Vice White Suit",  "바이스 화이트 수트",   800, {226, 228, 232, 255}, {212, 214, 220, 255}, { 46, 158, 194, 255}, true,  true,  false, false, 0.96f},
    {OutfitId::MidnightTuxedo, "Executive Tuxedo", "다운타운 블랙 턱시도",1500, { 24,  26,  32, 255}, { 20,  22,  28, 255}, {178,  44,  54, 255}, true,  true,  false, false, 0.92f},
    {OutfitId::RacerLeather,   "Carbon Racer",     "레이서 레더 재킷",    1200, {168,  38,  44, 255}, { 26,  28,  34, 255}, {218, 178,  58, 255}, true,  false, false, false, 0.94f},
    {OutfitId::TacOpsArmor,    "Spec-Ops Tactical","특수부대 전술 방탄복",2400, { 52,  60,  52, 255}, { 34,  38,  36, 255}, {176, 156, 102, 255}, true,  false, true,  false, 0.82f},
    {OutfitId::GoldBossCoat,   "VIP Boss Coat",    "VIP 골드 보스 코트",  5000, { 34,  30,  40, 255}, { 24,  22,  28, 255}, {228, 182,  62, 255}, true,  true,  false, true,  0.86f}
}};

struct PaintPreset {
    ColorRGBA paint;
    ColorRGBA stripe;
    bool matte;
    bool chrome;
    const char* nameEn;
    const char* nameKo;
};

constexpr std::array<PaintPreset, 12> kCustomPaints = {{
    {{184,  32,  44, 255}, {238, 240, 244, 255}, false, false, "Candy Crimson",   "캔디 크림슨 메탈릭"},
    {{ 32,  92, 178, 255}, {236, 240, 246, 255}, false, false, "Sapphire Blue",   "사파이어 블루"},
    {{ 28,  30,  36, 255}, {222, 176,  54, 255}, true,  false, "Stealth Matte",   "스텔스 매트 블랙"},
    {{226, 228, 232, 255}, { 34,  38,  46, 255}, false, false, "Pearl White",     "펄 화이트"},
    {{214, 162,  42, 255}, { 28,  30,  36, 255}, false, false, "Solar Gold",      "솔라 골드 메탈릭"},
    {{ 28, 118,  78, 255}, {232, 236, 234, 255}, false, false, "Emerald GT",      "에메랄드 그린"},
    {{ 98,  46, 158, 255}, { 72, 202, 224, 255}, false, false, "Ultraviolet",     "울트라바이올렛"},
    {{ 64,  70,  78, 255}, {212,  68,  46, 255}, true,  false, "Titanium Satin",  "티타늄 새틴 그레이"},
    {{212, 220, 230, 255}, { 34,  42,  54, 255}, false, true,  "Mirror Chrome",   "미러 실버 크롬"},
    {{232, 188,  64, 255}, { 26,  28,  34, 255}, false, true,  "24K Gold Chrome", "24K 로얄 골드 크롬"},
    {{ 34, 158, 192, 255}, {238, 242, 246, 255}, false, false, "Cyber Cyan",      "사이버 네온 시안"},
    {{156,  42,  76, 255}, {236, 238, 242, 255}, false, false, "Vice Rose",       "바이스 로즈 메탈릭"}
}};

ColorRGBA randomTrafficPaint(Rng& rng, VehicleKind kind) {
    if (kind == VehicleKind::PoliceCruiser) {
        return {244, 246, 250, 255}; // Authentic Seoul Metropolitan Police Crisp White!
    }
    if (kind == VehicleKind::Taxi) {
        return {244, 126, 24, 255};  // Authentic Seoul Haechi Orange Taxi!
    }
    if (kind == VehicleKind::Ambulance) {
        return {232, 234, 238, 255};
    }
    if (kind == VehicleKind::FireTruck) {
        return {184, 28, 34, 255};
    }
    if (kind == VehicleKind::SeoulBus) {
        // Authentic Seoul Blue Trunk Bus (파랑 간선버스) & Seoul Green Branch Bus (초록 지선버스)
        return rng.chance(0.65f) ? ColorRGBA{26, 86, 178, 255} : ColorRGBA{42, 134, 68, 255};
    }
    if (kind == VehicleKind::DeliveryVan) {
        return rng.chance(0.55f) ? ColorRGBA{214, 218, 222, 255} : ColorRGBA{58, 76, 98, 255};
    }
    if (kind == VehicleKind::ArmoredSuv) {
        return {40, 44, 50, 255};
    }
    if (kind == VehicleKind::LuxurySedan) {
        return rng.chance(0.58f) ? ColorRGBA{26, 30, 38, 255} : ColorRGBA{196, 200, 206, 255};
    }
    if (kind == VehicleKind::ClassicRoadster) {
        return rng.chance(0.55f) ? ColorRGBA{176, 34, 42, 255} : ColorRGBA{36, 78, 62, 255};
    }
    if (kind == VehicleKind::OffroadPickup) {
        return rng.chance(0.5f) ? ColorRGBA{168, 86, 34, 255} : ColorRGBA{52, 74, 56, 255};
    }
    if (kind == VehicleKind::JdmTuner) {
        return rng.chance(0.55f) ? ColorRGBA{36, 86, 168, 255} : ColorRGBA{218, 222, 228, 255};
    }
    if (kind == VehicleKind::TrackPrototype) {
        return rng.chance(0.5f) ? ColorRGBA{46, 62, 78, 255} : ColorRGBA{186, 38, 42, 255};
    }
    // Authentic real-world OEM automotive metallic & pearl finishes (Obsidian, Gunmetal, Titanium, Pearl, Rosso, Navy, British Racing Green, Satin Bronze, Burnt Copper)
    static constexpr std::array<ColorRGBA, 10> kPalette = {{
        { 32,  35,  42, 255}, // Obsidian Black
        { 76,  82,  92, 255}, // Gunmetal Grey
        {172, 178, 186, 255}, // Titanium Silver
        {222, 224, 228, 255}, // Pearl White
        {174,  34,  40, 255}, // Rosso Corsa Crimson
        { 34,  58,  96, 255}, // Midnight Navy
        { 34,  68,  52, 255}, // British Racing Green
        {126, 108,  84, 255}, // Satin Champagne Bronze
        { 68,  94, 122, 255}, // Slate Steel Blue
        {162,  76,  38, 255}  // Burnt Copper Metallic
    }};
    return kPalette[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(kPalette.size()) - 1))];
}

} // namespace

const VehicleSpec& getVehicleSpec(VehicleKind kind) {
    const int idx = std::clamp(static_cast<int>(kind), 0, static_cast<int>(VehicleKind::Count) - 1);
    return kVehicleSpecs[static_cast<std::size_t>(idx)];
}

const WeaponSpec& getWeaponSpec(WeaponId id) {
    const int idx = std::clamp(static_cast<int>(id), 0, static_cast<int>(WeaponId::Count) - 1);
    return kWeaponSpecs[static_cast<std::size_t>(idx)];
}

const PropertySpec& getPropertySpec(PropertyId id) {
    const int idx = std::clamp(static_cast<int>(id), 0, static_cast<int>(PropertyId::Count) - 1);
    return kPropertySpecs[static_cast<std::size_t>(idx)];
}

const OutfitSpec& getOutfitSpec(OutfitId id) {
    const int idx = std::clamp(static_cast<int>(id), 0, static_cast<int>(OutfitId::Count) - 1);
    return kOutfitSpecs[static_cast<std::size_t>(idx)];
}

ColorRGBA getNeonPresetColor(int neonPreset) {
    switch (neonPreset) {
        case 1: return { 42, 228, 255, 255}; // Cyber Cyan
        case 2: return {192,  64, 255, 255}; // Neon Purple
        case 3: return {255,  46,  72, 255}; // Crimson Red
        case 4: return { 52, 248, 128, 255}; // Emerald Green
        case 5: return {255, 204,  52, 255}; // Solar Gold
        default: return {0, 0, 0, 0};
    }
}

ColorRGBA getPlayerSkinColor(int skinToneIdx) {
    switch (std::clamp(skinToneIdx, 0, 4)) {
        case 0: return {246, 214, 192, 255}; // Warm Ivory
        case 1: return {238, 194, 162, 255}; // Natural Beige
        case 2: return {212, 160, 120, 255}; // Golden Tan
        case 3: return {174, 124,  88, 255}; // Olive Bronze
        case 4: return {108,  72,  52, 255}; // Deep Espresso
        default: return {238, 194, 162, 255};
    }
}

ColorRGBA getPlayerTopColor(OutfitId outfit, int customTopColorIdx) {
    switch (customTopColorIdx) {
        case 1: return {218,  38,  52, 255}; // Crimson Red
        case 2: return { 38, 122, 238, 255}; // Sapphire Blue
        case 3: return { 26,  28,  36, 255}; // Stealth Black
        case 4: return {242, 244, 248, 255}; // Arctic White
        case 5: return { 34, 184, 118, 255}; // Emerald Green
        case 6: return {238, 186,  46, 255}; // Royal Gold
        case 7: return { 38, 206, 238, 255}; // Cyber Cyan
        case 8: return {144,  58, 232, 255}; // Neon Violet
        default: return getOutfitSpec(outfit).top;
    }
}

ColorRGBA getPlayerBottomColor(OutfitId outfit, int customBottomColorIdx) {
    switch (customBottomColorIdx) {
        case 1: return { 22,  24,  30, 255}; // Jet Black
        case 2: return { 42,  68, 112, 255}; // Indigo Denim
        case 3: return {232, 236, 242, 255}; // Arctic White
        case 4: return {126, 114,  86, 255}; // Tactical Khaki
        case 5: return {168,  34,  44, 255}; // Crimson Track
        case 6: return { 56,  62,  72, 255}; // Charcoal Grey
        default: return getOutfitSpec(outfit).bottom;
    }
}

ColorRGBA getPlayerHairColor(OutfitId outfit, int hairColorIdx) {
    switch (hairColorIdx) {
        case 1: return { 96,  56,  34, 255}; // Chestnut Brown
        case 2: return {236, 224, 188, 255}; // Platinum Blonde
        case 3: return {214,  38,  52, 255}; // Crimson Red
        case 4: return { 44, 224, 252, 255}; // Cyber Cyan
        case 5: return {248, 198,  54, 255}; // Royal Gold
        default:
            return (outfit == OutfitId::GoldBossCoat)
                       ? ColorRGBA{242, 202, 78, 255}
                       : ColorRGBA{28, 24, 22, 255};
    }
}

WeaponFinishMaterial getWeaponFinishMaterial(WeaponId weapon, int skinIndex) {
    (void)weapon;
    switch (std::clamp(skinIndex, 0, 5)) {
        case 1: return {{218, 226, 236, 255}, {154, 166, 182, 255}, 0.06f, 0.96f, 0.0f};  // Brushed Chrome
        case 2: return {{248, 198,  56, 255}, { 38,  34,  26, 255}, 0.08f, 0.95f, 0.06f}; // 24K Royal Gold
        case 3: return {{212,  34,  48, 255}, { 26,  28,  34, 255}, 0.20f, 0.74f, 0.04f}; // Crimson Carbon
        case 4: return {{ 38, 222, 252, 255}, {172,  56, 248, 255}, 0.14f, 0.86f, 0.42f}; // Cyber Neon
        case 5: return {{240, 244, 250, 255}, { 54,  62,  74, 255}, 0.22f, 0.56f, 0.0f};  // Arctic Whiteout
        default: return {{ 34,  38,  46, 255}, { 66,  72,  84, 255}, 0.28f, 0.80f, 0.0f}; // Factory Black
    }
}

ColorRGBA getLaserSightColorRGBA(int laserSightColor) {
    switch (laserSightColor) {
        case 1: return {255,  42,  52, 235}; // Tactical Red
        case 2: return { 54, 255, 108, 235}; // Spec-Ops Green
        case 3: return { 46, 232, 255, 235}; // Cyber Cyan
        default: return {0, 0, 0, 0};
    }
}

ColorRGBA getCustomTracerColorRGBA(WeaponId weapon, int tracerColorIdx) {
    switch (tracerColorIdx) {
        case 1: return {255, 214,  72, 255}; // Solar Gold
        case 2: return { 58, 236, 255, 255}; // Cyber Cyan
        case 3: return {255,  56,  68, 255}; // Crimson Laser
        case 4: return {206,  84, 255, 255}; // Plasma Purple
        case 5: return { 66, 252, 134, 255}; // Emerald Green
        default:
            if (weapon == WeaponId::SniperRifle) return {182, 242, 255, 255};
            if (weapon == WeaponId::HeavyRevolver) return {255, 206, 88, 255};
            if (weapon == WeaponId::Minigun) return {255, 174, 64, 255};
            return {255, 232, 142, 255};
    }
}

ColorRGBA getOpticReticleColorRGBA(int opticColorIdx, ColorRGBA defaultColor) {
    switch (opticColorIdx) {
        case 1: return {255,  58,  58, 255}; // Red
        case 2: return { 68, 252, 128, 255}; // Green
        case 3: return { 64, 236, 255, 255}; // Cyan
        case 4: return {255, 212,  64, 255}; // Gold
        default: return defaultColor;
    }
}

ColorRGBA getVehicleStripeColorRGBA(int stripeStyle, ColorRGBA fallback) {
    switch (stripeStyle) {
        case 1: return {246, 248, 252, 255}; // White Dual GT
        case 2: return {248, 198,  56, 255}; // Royal Gold
        case 3: return { 48, 228, 255, 255}; // Cyber Cyan
        case 4: return { 24,  26,  32, 255}; // Stealth Carbon Black
        default: return fallback;
    }
}

ColorRGBA getVehicleHeadlightColorRGBA(int headlightColorIdx, bool nightMode) {
    switch (headlightColorIdx) {
        case 1: return { 64, 236, 255, 255}; // Cyber Cyan
        case 2: return {255, 214,  54, 255}; // GT Endurance Yellow
        case 3: return {198,  86, 255, 255}; // UV Neon Purple
        case 4: return {255,  52,  64, 255}; // Demon-Eye Red
        default:
            return nightMode ? ColorRGBA{255, 250, 228, 255} : ColorRGBA{236, 242, 252, 255};
    }
}

ColorRGBA getDriftSmokeColorRGBA(int driftSmokeColorIdx) {
    switch (driftSmokeColorIdx) {
        case 1: return { 64, 232, 255, 215}; // Cyber Cyan
        case 2: return {252,  68,  82, 215}; // Crimson Red
        case 3: return {255, 206,  68, 215}; // Royal Gold
        case 4: return {196,  84, 255, 215}; // Violet Purple
        case 5: return { 68, 246, 138, 215}; // Emerald Green
        default: return {225, 228, 232, 185};
    }
}

ColorRGBA getCrosshairColorRGBA(int crosshairColorIdx) {
    switch (crosshairColorIdx) {
        case 1: return { 72, 255, 124, 240}; // Neon Green
        case 2: return { 58, 236, 255, 240}; // Cyber Cyan
        case 3: return {255, 214,  64, 240}; // Solar Gold
        case 4: return {255,  78, 212, 240}; // Hot Magenta
        case 5: return {255,  68,  72, 240}; // Crimson Red
        default: return {245, 248, 255, 235}; // Pure White
    }
}

ColorRGBA getHudThemeColorRGBA(int hudThemeIdx) {
    switch (hudThemeIdx) {
        case 1: return {255,  96, 204, 255}; // Vice Pink
        case 2: return {255, 206,  72, 255}; // VIP Gold
        case 3: return { 76, 242, 148, 255}; // Emerald Green
        default: return { 82, 218, 255, 255}; // Cyber Cyan
    }
}

GameWorld::GameWorld(std::uint64_t seed)
    : seed_(seed), rng_(seed), city_() {
    city_.generate(seed_);
    reset();
}

bool GameWorld::loadRealSeoulMap(const std::uint8_t* bytes, std::size_t size) {
    if (!city_.loadRealSeoulMap(bytes, size)) {
        return false;
    }
    reset();
    return true;
}

bool GameWorld::loadRealSeoulMapFromFile(const std::string& path) {
    if (!city_.loadRealSeoulMapFromFile(path)) {
        return false;
    }
    reset();
    return true;
}

void GameWorld::reset() {
    rng_ = Rng(seed_);
    vehicles_.clear();
    npcs_.clear();
    pickups_.clear();
    tracers_.clear();
    skidMarks_.clear();
    particles_.clear();
    audioEvents_.clear();
    nextVehicleId_ = 1;
    nextNpcId_ = 1;

    player_ = PlayerState{};
    player_.pos = city_.spawnPos();
    player_.policeLastKnownPos = player_.pos;
    player_.facingYaw = city_.spawnYaw();
    player_.cash = 2500;
    player_.health = 100.0f;
    player_.maxHealth = 100.0f;
    player_.armor = 35.0f;
    player_.maxArmor = 100.0f;
    player_.currentWeapon = WeaponId::Pistol;

    for (auto& ws : player_.weapons) {
        ws = WeaponSlotState{};
    }
    player_.weapons[static_cast<std::size_t>(WeaponId::Fists)].owned = true;
    player_.weapons[static_cast<std::size_t>(WeaponId::Pistol)].owned = true;
    player_.weapons[static_cast<std::size_t>(WeaponId::Pistol)].magAmmo = 12;
    player_.weapons[static_cast<std::size_t>(WeaponId::Pistol)].reserveAmmo = 84;

    // Initialize default personal garage colors + starter owned hatchback (clean OEM factory finishes)
    player_.garage[static_cast<std::size_t>(VehicleKind::Compact)].owned = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::Compact)].paintColor = {44, 96, 154, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::MuscleCar)].paintColor = {178, 56, 36, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::SportsCoupe)].paintColor = {176, 32, 40, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::SportsCoupe)].customSpoiler = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::SportsCoupe)].spoilerStyle = 1;
    player_.garage[static_cast<std::size_t>(VehicleKind::LuxurySedan)].paintColor = {28, 32, 40, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::ArmoredSuv)].paintColor = {50, 54, 60, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::ArmoredSuv)].mattePaint = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::Hypercar)].paintColor = {206, 152, 34, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::Hypercar)].customSpoiler = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::Hypercar)].spoilerStyle = 1;
    player_.garage[static_cast<std::size_t>(VehicleKind::Hypercar)].rimStyle = 1;
    player_.garage[static_cast<std::size_t>(VehicleKind::OffroadPickup)].paintColor = {166, 76, 32, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::OffroadPickup)].rimStyle = 2;
    player_.garage[static_cast<std::size_t>(VehicleKind::ClassicRoadster)].paintColor = {172, 34, 42, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::ClassicRoadster)].stripeColor = {236, 236, 240, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::JdmTuner)].paintColor = {34, 82, 168, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::JdmTuner)].customSpoiler = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::JdmTuner)].spoilerStyle = 1;
    player_.garage[static_cast<std::size_t>(VehicleKind::JdmTuner)].rimStyle = 1;
    player_.garage[static_cast<std::size_t>(VehicleKind::TrackPrototype)].paintColor = {206, 212, 220, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::TrackPrototype)].stripeColor = {184, 34, 42, 255};
    player_.garage[static_cast<std::size_t>(VehicleKind::TrackPrototype)].customSpoiler = true;
    player_.garage[static_cast<std::size_t>(VehicleKind::TrackPrototype)].spoilerStyle = 2;
    player_.garage[static_cast<std::size_t>(VehicleKind::TrackPrototype)].rimStyle = 2;

    player_.ownedOutfits[static_cast<std::size_t>(OutfitId::StreetCasual)] = true;
    player_.currentOutfit = OutfitId::StreetCasual;

    mission_ = MissionState{};
    raceCheckpoints_.clear();
    banner_ = BannerNotification{};
    activePoi_ = nullptr;
    timeOfDayHours_ = 14.5f;
    trafficSignalTimer_ = 0.0f;
    policeSpawnCooldown_ = 0.0f;
    panicScreamCooldown_ = 0.0f;
    rainWeather_ = false;

    for (auto& sp : city_.streetProps()) {
        if (sp.homePos.lengthSq() > 0.001f) {
            sp.pos = sp.homePos;
        } else {
            sp.homePos = sp.pos;
        }
        sp.broken = false;
        sp.velocity = {0.0f, 0.0f};
        sp.brokenTimer = 0.0f;
        sp.posY = 0.0f;
        sp.velY = 0.0f;
        sp.pitch = 0.0f;
        sp.roll = 0.0f;
        sp.angVelPitch = 0.0f;
        sp.angVelRoll = 0.0f;
    }

    city_.updateStreaming(player_.pos, true);
    player_.posY = surfaceHeightAt(player_.pos);
    player_.onGround = true;

    spawnInitialWorld();
    pushBanner(city_.hasRealSeoulMap() ? "REAL SEOUL ULTRA-SMOOTH 4K v5.0" : "WELCOME TO SEOUL (VULKAN)",
               city_.hasRealSeoulMap() ? "실제 서울 초정밀 도로·거리 4K v5.0에 오신 것을 환영합니다" : "서울 메트로폴리스에 오신 것을 환영합니다",
#if defined(__ANDROID__)
               "Tap Minimap: Full Seoul Map  |  Top Bar: Custom · Weather · Camera",
               "미니맵 터치: 서울 전체 지도 · 상단 버튼: 커스텀 · 날씨 · 시점 전환",
#else
               "[M] Full Seoul Map  |  [K] Custom Studio  |  [Y] Rain  |  [V] 1st/3rd Person",
               "미니맵 클릭/[M] 서울 전체 지도 · [K] 커스텀 · [Y] 날씨 · [V] 1·3인칭",
#endif
               {118, 192, 232, 255}, 5.2f);
}

void GameWorld::spawnParkedShowroomCars() {
    auto addParked = [&](VehicleKind kind, Vec2 pos, float heading, ColorRGBA paint, int neon = 0, int rims = 0, bool matte = false) {
        const auto& spec = getVehicleSpec(kind);
        Vehicle v{};
        v.id = nextVehicleId_++;
        v.kind = kind;
        v.pos = pos;
        v.posY = surfaceHeightAt(pos);
        v.heading = heading;
        v.health = spec.maxHealth;
        v.maxHealth = spec.maxHealth;
        v.paintColor = paint;
        v.customSpoiler = spec.hasSpoiler;
        v.spoilerStyle = (kind == VehicleKind::TrackPrototype) ? 2 : (spec.hasSpoiler ? 1 : 0);
        v.neonPreset = neon;
        v.rimStyle = rims;
        v.mattePaint = matte;
        v.isParked = true;
        v.hasNpcDriver = false;
        vehicles_.push_back(v);
    };

    if (city_.hasRealSeoulMap()) {
        // Park curated starter & showroom cars cleanly centered inside the outer 3.25m curb lane on Teheran-ro!
        const NearestOsmRoad nr = city_.realData().findNearestRoad(player_.pos, 60.0f);
        if (nr.found) {
            const OsmRoadRec& rr = city_.realData().roads()[nr.roadIdx];
            const RoadLaneLayout layout = RealSeoulData::computeRoadLaneLayout(rr);
            const float yaw = std::atan2(nr.tangent.z, nr.tangent.x);
            const Vec2 fwd = nr.tangent;
            const Vec2 rightN{-fwd.z, fwd.x};
            const float rightCurbLaneOff = layout.laneCenterOffset(std::max(0, layout.lanesInDir - 1));
            struct CurbSlot { VehicleKind kind; float along; float side; ColorRGBA paint; int rims; bool police; };
            const CurbSlot kSlots[] = {
                {VehicleKind::SportsCoupe,      12.0f,  1.0f, {168,  24,  34, 255}, 1, false},
                {VehicleKind::Hypercar,         23.0f,  1.0f, { 26,  30,  38, 255}, 1, false},
                {VehicleKind::LuxurySedan,      34.0f,  1.0f, { 34,  42,  54, 255}, 1, false},
                {VehicleKind::Taxi,            -14.0f,  1.0f, {232, 114,  34, 255}, 0, false},
                {VehicleKind::JdmTuner,        -26.0f,  1.0f, { 32,  76, 158, 255}, 1, false},
                {VehicleKind::PoliceCruiser,    46.0f,  1.0f, {240, 244, 248, 255}, 2, true},
                {VehicleKind::ClassicRoadster,  58.0f, -1.0f, {154,  28,  36, 255}, 0, false},
                {VehicleKind::SUV,             -38.0f, -1.0f, { 54,  62,  72, 255}, 1, false},
                {VehicleKind::Ambulance,        70.0f, -1.0f, {242, 244, 248, 255}, 0, false},
                {VehicleKind::PoliceCruiser,   -50.0f, -1.0f, {240, 244, 248, 255}, 2, true}
            };
            for (const auto& sl : kSlots) {
                const bool useOppSide = (!nr.isOneway && sl.side < 0.0f);
                const float latOff = useOppSide ? (-rightCurbLaneOff) : rightCurbLaneOff;
                const float carYaw = useOppSide ? wrapAngle(yaw + kPi) : yaw;
                Vec2 p = nr.closestPt + fwd * sl.along + rightN * latOff;
                Vec2 test = p;
                if (!city_.realData().isInWater(p) && !city_.realData().resolveCircleBuildings(test, 1.55f)) {
                    addParked(sl.kind, p, carYaw, sl.paint, 0, sl.rims);
                    if (sl.police) {
                        vehicles_.back().isPolice = true;
                        vehicles_.back().stripeColor = {24, 54, 126, 255};
                    }
                }
            }
        }
        return;
    }

    // Starter CAD-sculpted cars parked cleanly along Banpo/Gangnam Boulevard ({588, 1008} crossroads!)
    addParked(VehicleKind::SportsCoupe,     {605.5f, 1015.2f},  0.0f,    {168,  24,  34, 255}, 0, 1);
    addParked(VehicleKind::LuxurySedan,     {595.2f, 1030.5f},  kHalfPi, { 26,  30,  38, 255}, 0, 1);
    addParked(VehicleKind::MuscleCar,       {572.0f, 1000.8f},  kPi,     { 28,  68, 128, 255}, 0, 0);
    addParked(VehicleKind::ClassicRoadster, {614.0f, 1000.8f},  kPi,     {154,  28,  36, 255}, 0, 0);
    addParked(VehicleKind::JdmTuner,        {574.0f, 1022.5f},  0.0f,    { 32,  76, 158, 255}, 0, 1);
    addParked(VehicleKind::SUV,             {580.8f, 1044.0f}, -kHalfPi, { 54,  62,  72, 255}, 0, 1);
    addParked(VehicleKind::Taxi,            {595.2f,  986.0f}, -kHalfPi, {232, 114,  34, 255}, 0, 0);

    // Authentic Korean National Police (경찰 POLICE: Pearl White body + Navy/Yellow accents) & Seoul 119 EMS/Fire
    auto addPoliceParked = [&](Vec2 pos, float heading) {
        const auto& pspec = getVehicleSpec(VehicleKind::PoliceCruiser);
        Vehicle cruiser{};
        cruiser.id = nextVehicleId_++;
        cruiser.kind = VehicleKind::PoliceCruiser;
        cruiser.pos = pos;
        cruiser.posY = surfaceHeightAt(pos);
        cruiser.heading = heading;
        cruiser.health = pspec.maxHealth;
        cruiser.maxHealth = pspec.maxHealth;
        cruiser.paintColor = {240, 244, 248, 255};
        cruiser.stripeColor = {24, 54, 126, 255};
        cruiser.rimStyle = 2;
        cruiser.isParked = true;
        cruiser.isPolice = true;
        vehicles_.push_back(cruiser);
    };
    addPoliceParked({656.0f, 1000.8f}, 0.0f);
    addPoliceParked({520.0f, 1000.8f}, kPi);
    addParked(VehicleKind::Ambulance, {595.2f, 1068.0f}, -kHalfPi, {242, 244, 248, 255}, 0, 0);
    addParked(VehicleKind::FireTruck, {518.0f, 1015.2f},  0.0f,    {184,  28,  34, 255}, 0, 0);

    // GANGNAM EXOTIC SHOWROOM forecourt display lineup (x in [612..654], z ~ 1034, facing Banpo Blvd!)
    addParked(VehicleKind::TrackPrototype,  {614.0f, 1034.0f}, -kHalfPi * 0.85f, {210, 216, 224, 255}, 0, 2, false);
    addParked(VehicleKind::Hypercar,        {624.5f, 1034.0f}, -kHalfPi,         {198, 146,  32, 255}, 0, 1, false);
    addParked(VehicleKind::ClassicRoadster, {635.0f, 1034.0f}, -kHalfPi * 1.10f, { 28,  82,  68, 255}, 0, 0, false);
    addParked(VehicleKind::ArmoredSuv,      {645.5f, 1034.0f}, -kHalfPi * 1.18f, { 46,  50,  56, 255}, 0, 2, true);
}

void GameWorld::spawnInitialWorld() {
    spawnParkedShowroomCars();
    maintainTrafficPopulation();
    maintainCivilianPopulation();
    maintainPolicePopulation(0.0f);

    // Ambient Gangnam / Seocho back-alley syndicate guards & bonus cash pickups
    const std::array<Vec2, 4> turfSpots = {{
        {760.0f, 1180.0f},
        {768.0f, 1176.0f},
        {842.0f, 1184.0f},
        {762.0f, 1256.0f}
    }};
    for (const Vec2& p : turfSpots) {
        NpcCharacter g{};
        g.id = nextNpcId_++;
        g.role = NpcRole::Gangster;
        g.behavior = NpcBehavior::Stroll;
        g.pos = p;
        g.homeAnchor = p;
        g.walkSpeed = 1.45f;
        g.facingYaw = rng_.range(-kPi, kPi);
        g.health = 85.0f;
        g.maxHealth = 85.0f;
        g.outfitTop = {138, 36, 44, 255};
        g.outfitBottom = {32, 34, 40, 255};
        npcs_.push_back(g);
    }

    pickups_.push_back(WorldPickup{PickupKind::CashBundle, { -42.0f, -42.0f}, 350, 0.0f, false});
    pickups_.push_back(WorldPickup{PickupKind::CashBundle, {-748.0f,   0.0f}, 500, 0.7f, false}); // Sunset Pier end bonus
    pickups_.push_back(WorldPickup{PickupKind::CashBundle, {   0.0f, 916.0f}, 750, 1.4f, false}); // North Bay Lighthouse bonus
    pickups_.push_back(WorldPickup{PickupKind::HealthKit,  {  24.0f,  20.0f},  50, 1.2f, false});
}

Vec2 GameWorld::playerWorldPos() const {
    if (const Vehicle* car = playerVehicle()) {
        return car->pos;
    }
    return player_.pos;
}

const Vehicle* GameWorld::findVehicle(int id) const {
    for (const auto& v : vehicles_) {
        if (v.id == id) return &v;
    }
    return nullptr;
}

Vehicle* GameWorld::findVehicleMutable(int id) {
    for (auto& v : vehicles_) {
        if (v.id == id) return &v;
    }
    return nullptr;
}

const Vehicle* GameWorld::playerVehicle() const {
    if (player_.drivingVehicleId < 0) return nullptr;
    return findVehicle(player_.drivingVehicleId);
}

float GameWorld::daylightFactor() const {
    const float phase = (timeOfDayHours_ - 13.0f) / 24.0f * kTwoPi;
    const float raw = 0.5f + 0.5f * std::cos(phase);
    return 0.14f + 0.86f * raw;
}

bool GameWorld::isTrafficGreenForHorizontal() const {
    return trafficSignalTimer_ >= 0.0f && trafficSignalTimer_ < 10.5f;
}

bool GameWorld::isTrafficGreenForVertical() const {
    return trafficSignalTimer_ >= 12.0f && trafficSignalTimer_ < 22.5f;
}

void GameWorld::advanceTimeOfDayHours(float hours) {
    timeOfDayHours_ = std::fmod(timeOfDayHours_ + hours, 24.0f);
    if (timeOfDayHours_ < 0.0f) timeOfDayHours_ += 24.0f;
}

std::vector<AudioEvent> GameWorld::consumeAudioEvents() {
    std::vector<AudioEvent> out;
    out.swap(audioEvents_);
    return out;
}

int GameWorld::passiveIncomePerCycle() const {
    int total = 0;
    for (int i = 0; i < static_cast<int>(PropertyId::Count); ++i) {
        if (player_.ownedProperties[static_cast<std::size_t>(i)]) {
            total += getPropertySpec(static_cast<PropertyId>(i)).incomePerCycle;
        }
    }
    return total;
}

int GameWorld::ownedPropertyCount() const {
    int cnt = 0;
    for (bool owned : player_.ownedProperties) {
        if (owned) ++cnt;
    }
    return cnt;
}

float GameWorld::tuningDiscountScale() const {
    return player_.ownedProperties[static_cast<std::size_t>(PropertyId::PayNTuneFranchise)] ? 0.5f : 1.0f;
}

void GameWorld::toggleRainWeather() {
    rainWeather_ = !rainWeather_;
    for (auto& npc : npcs_) {
        if (npc.role == NpcRole::Civilian) {
            npc.holdingUmbrella = rainWeather_ && ((npc.id * 37) % 100) < 62;
        }
    }
    pushAudio(AudioEventType::ShopBuy, 0.78f);
    pushBanner(rainWeather_ ? "COASTAL RAIN & WET ASPHALT" : "CLEAR SUNSET BAY SKY",
               rainWeather_ ? "우천 날씨 & 젖은 아스팔트 노면 ON" : "맑은 날씨 모드 (건조 노면)",
               rainWeather_ ? "Wet tarmac reflections, raindrops & reduced tire grip"
                            : "Full dry-tarmac tire grip restored",
               rainWeather_ ? "빗물 웅덩이 반사·빗소리 및 빗길 제동거리 증가 활성화"
                            : "건조 아스팔트 노면 최대 타이어 그립 복구",
               rainWeather_ ? ColorRGBA{108, 178, 224, 255} : ColorRGBA{232, 192, 92, 255}, 2.4f);
}

float GameWorld::surfaceHeightAt(const Vec2& pos, float refY) const {
    if (city_.hasRealSeoulMap()) {
        return city_.surfaceHeightAt(pos, refY);
    }
    if (city_.isOnBridge(pos, 1.5f)) {
        return std::max(city_.terrainElevationAt(pos), city_.bridgeDeckElevationAt(pos));
    }
    const float base = city_.terrainElevationAt(pos);
    return base + (city_.isOnRoad(pos, 0.1f) ? 0.0f : 0.08f);
}

void GameWorld::setWaypoint(const Vec2& pos, const std::string& labelKo) {
    player_.hasWaypoint = true;
    player_.waypointPos = {
        clampf(pos.x, CityMap::kWorldMinX + 40.0f, CityMap::kWorldMaxX - 40.0f),
        clampf(pos.z, CityMap::kWorldMinZ + 40.0f, CityMap::kWorldMaxZ - 40.0f)
    };
    player_.waypointLabelKo = labelKo;
    const DistrictType d = city_.districtAtWorld(player_.waypointPos);
    const std::string subKo = labelKo.empty()
        ? (std::string(CityMap::districtNameKo(d)) + " GPS 목적지 설정 완료")
        : (labelKo + " GPS 경로 안내 시작");
    pushBanner("GPS WAYPOINT SET", "목적지 GPS 설정 완료",
               CityMap::districtNameEn(d), subKo,
               {92, 214, 248, 255}, 2.4f);
}

void GameWorld::clearWaypoint() {
    player_.hasWaypoint = false;
    player_.waypointLabelKo.clear();
}

bool GameWorld::fastTravelTo(const Vec2& targetPos, const std::string& labelKo) {
    if (player_.wasted) return false;
    Vec2 dest{
        clampf(targetPos.x, CityMap::kWorldMinX + 90.0f, CityMap::kWorldMaxX - 90.0f),
        clampf(targetPos.z, CityMap::kWorldMinZ + 90.0f, CityMap::kWorldMaxZ - 90.0f)
    };
    if (city_.hasRealSeoulMap() && city_.realData().isInCoreBounds(dest)) {
        // Snap smoothly onto the nearest real drivable OpenStreetMap road if inside water, building, or off-road plaza!
        Vec2 probe = dest;
        const bool blocked = (city_.realData().isInWater(dest) && !city_.realData().isOnBridge(dest, 2.0f)) ||
                             city_.realData().resolveCircleBuildings(probe, 2.0f) ||
                             !city_.realData().isOnRoad(dest, 6.0f);
        if (blocked) {
            const NearestOsmRoad nr = city_.realData().findNearestRoad(dest, 240.0f);
            if (nr.found) {
                const OsmRoadRec& rr = city_.realData().roads()[nr.roadIdx];
                const RoadLaneLayout layout = RealSeoulData::computeRoadLaneLayout(rr);
                const Vec2 rightN{-nr.tangent.z, nr.tangent.x};
                dest = nr.closestPt + rightN * layout.laneCenterOffset(layout.lanesInDir / 2);
            }
        }
    } else {
        const int rx = city_.nearestRoadIndex(dest.x);
        const int rz = city_.nearestRoadIndex(dest.z);
        if (city_.isInHanRiverWater(dest, -12.0f) && !city_.isOnBridge(dest, 10.0f)) {
            const float rCenter = city_.hanRiverCenterZ(dest.x);
            const float rHw = city_.hanRiverHalfWidth(dest.x);
            dest.z = (dest.z >= rCenter) ? (rCenter + rHw + 60.0f) : (rCenter - rHw - 60.0f);
        } else if (!city_.isOnBridge(dest, 2.0f)) {
            const float candX = city_.roadCoord(rx);
            const float candZ = city_.roadCoord(rz);
            if (std::abs(dest.x - candX) <= std::abs(dest.z - candZ)) {
                dest.x = candX + CityMap::kLaneOffset;
            } else {
                dest.z = candZ + CityMap::kLaneOffset;
            }
        }
    }

    city_.updateStreaming(dest, true);
    city_.resolveCircleWorld(dest, 2.2f);
    const float gy = surfaceHeightAt(dest);

    if (player_.drivingVehicleId >= 0) {
        if (Vehicle* car = findVehicleMutable(player_.drivingVehicleId)) {
            car->pos = dest;
            car->posY = gy;
            car->velocity = {0.0f, 0.0f};
            car->velY = 0.0f;
            car->angularVelocity = 0.0f;
            car->airborne = false;
        }
    }
    player_.pos = dest;
    player_.posY = gy;
    player_.velocity = {0.0f, 0.0f};
    player_.verticalVel = 0.0f;
    player_.onGround = true;

    const DistrictType d = city_.districtAtWorld(dest);
    const std::string koTitle = labelKo.empty() ? std::string(CityMap::districtNameKo(d)) : labelKo;
    pushBanner("FAST TRAVEL ARRIVED", "빠른 이동 완료: " + koTitle,
               CityMap::districtNameEn(d), std::string(CityMap::districtNameKo(d)) + " 도착",
               {102, 232, 168, 255}, 2.8f);
    return true;
}

float GameWorld::computePlayerSupportHeight(const Vec2& pos, float currentY, Vec2* platformCarryVel) const {
    float supportY = surfaceHeightAt(pos, currentY);
    if (platformCarryVel) {
        *platformCarryVel = {0.0f, 0.0f};
    }

    const OBB2D pBox{pos, {0.36f, 0.36f}, 0.0f};
    for (const auto& v : vehicles_) {
        if (v.destroyed || v.id == player_.drivingVehicleId || v.speed() > 14.0f) continue;
        if ((v.pos - pos).lengthSq() > 16.0f) continue;
        Vec2 mtv{};
        if (intersectOBB(pBox, v.obb(), mtv)) {
            const float topY = v.posY + 0.24f + getVehicleSpec(v.kind).chassisHeight;
            if (currentY >= topY - 0.38f && topY > supportY) {
                supportY = topY;
                if (platformCarryVel) {
                    *platformCarryVel = v.velocity;
                }
            }
        }
    }

    for (const auto& prop : city_.streetProps()) {
        if (prop.broken) continue;
        const float propBase = (prop.baseY > -1e8f) ? prop.baseY : surfaceHeightAt(prop.pos, currentY);
        float r = 0.0f;
        float topY = 0.0f;
        if (prop.kind == StreetPropKind::Crate) {
            r = 0.58f;
            topY = propBase + 0.92f;
        } else if (prop.kind == StreetPropKind::Bench) {
            r = 0.72f;
            topY = propBase + 0.56f;
        } else {
            continue;
        }
        if ((pos - prop.pos).lengthSq() <= r * r && currentY >= topY - 0.34f && topY > supportY) {
            supportY = topY;
        }
    }

    return supportY;
}

float GameWorld::requiredEvadeDuration() const {
    if (player_.wantedLevel <= 0) return 0.0f;
    return 9.5f + 2.5f * static_cast<float>(player_.wantedLevel);
}

void GameWorld::clearWantedLevel(bool showToast) {
    const bool hadWanted = (player_.wantedLevel > 0);
    player_.wantedLevel = 0;
    player_.wantedHeat = 0.0f;
    player_.wantedEvadeTimer = 0.0f;
    player_.policeLineOfSight = false;
    player_.policeSearching = false;

    for (auto& v : vehicles_) {
        if (v.isPolice && !v.isPlayerDriving) {
            v.sirenActive = false;
        }
    }
    for (auto& npc : npcs_) {
        if (npc.alive && npc.role == NpcRole::Police) {
            npc.behavior = NpcBehavior::Stroll;
        }
    }

    if (hadWanted && showToast) {
        pushAudio(AudioEventType::MissionComplete, 0.85f);
        pushBanner("WANTED EVADED!", "경찰 따돌림 성공!",
                   "Seoul Police called off the search", "경찰 수색망을 완전히 따돌렸습니다!",
                   {98, 242, 154, 255}, 3.0f);
    }
}

void GameWorld::reportCrime(float heatDelta, int minStars, const Vec2& crimePos, bool directWitness) {
    if (player_.wasted) return;

    bool policeWitness = directWitness;
    bool civilianWitness = directWitness;

    for (const auto& npc : npcs_) {
        if (!npc.alive) continue;
        const float dSq = (npc.pos - crimePos).lengthSq();
        if (npc.role == NpcRole::Police && dSq <= 58.0f * 58.0f) {
            policeWitness = true;
        } else if (npc.role == NpcRole::Civilian && dSq <= 42.0f * 42.0f) {
            civilianWitness = true;
        }
    }
    for (const auto& v : vehicles_) {
        if (!v.destroyed && v.isPolice && v.hasNpcDriver && (v.pos - crimePos).lengthSq() <= 66.0f * 66.0f) {
            policeWitness = true;
            break;
        }
    }

    // Minor 1-star offenses with zero witnesses go unreported
    if (!policeWitness && !civilianWitness && player_.wantedLevel == 0 && minStars <= 1 && heatDelta < 2.2f) {
        return;
    }

    player_.wantedHeat = std::min(26.0f, player_.wantedHeat + heatDelta);
    int computedStars = 0;
    if (player_.wantedHeat >= 18.0f) computedStars = 5;
    else if (player_.wantedHeat >= 11.5f) computedStars = 4;
    else if (player_.wantedHeat >= 6.5f) computedStars = 3;
    else if (player_.wantedHeat >= 3.0f) computedStars = 2;
    else if (player_.wantedHeat >= 0.95f) computedStars = 1;

    const int prevStars = player_.wantedLevel;
    const int nextStars = std::clamp(std::max({prevStars, minStars, computedStars}), 0, 5);
    player_.wantedLevel = nextStars;
    player_.wantedEvadeTimer = 0.0f;
    player_.policeLineOfSight = true;
    player_.policeSearching = false;
    player_.policeLastKnownPos = crimePos;

    for (auto& npc : npcs_) {
        if (npc.alive && npc.role == NpcRole::Police && (npc.pos - crimePos).lengthSq() <= 68.0f * 68.0f) {
            npc.behavior = NpcBehavior::CombatEngage;
        }
    }
    for (auto& v : vehicles_) {
        if (!v.destroyed && v.isPolice && v.hasNpcDriver && (v.pos - crimePos).lengthSq() <= 95.0f * 95.0f) {
            v.sirenActive = true;
        }
    }

    if (nextStars > prevStars) {
        pushAudio(AudioEventType::PoliceDispatch, 0.92f);
        if (nextStars >= 4 && prevStars < 4) {
            pushBanner("SEOUL SWAT ALERT!", "서울 경찰특공대 긴급 투입!",
                       "Break line-of-sight behind buildings to evade",
                       "건물 뒤로 시야를 끊고 수색망을 벗어나 도주하세요!",
                       {255, 76, 86, 255}, 3.0f);
        } else if (prevStars == 0) {
            pushBanner("WANTED BY SEOUL POLICE!", "서울 경찰 수배 발령!",
                       "Break line of sight & hide to shake off pursuit",
                       "골목·건물 뒤로 경찰 시야를 벗어나면 도주할 수 있습니다",
                       {88, 178, 255, 255}, 2.9f);
        }
    }
}

void GameWorld::recalculatePlayerPropertyPerks() {
    float maxHp = 100.0f;
    float maxAr = 100.0f;
    if (player_.ownedProperties[static_cast<std::size_t>(PropertyId::SunsetCondo)]) {
        maxHp += 25.0f;
    }
    if (player_.ownedProperties[static_cast<std::size_t>(PropertyId::EclipsePenthouse)]) {
        maxHp = 150.0f;
        maxAr = 150.0f;
    }
    player_.maxHealth = maxHp;
    player_.maxArmor = maxAr;
}

void GameWorld::syncPersonalTuningFromVehicle(const Vehicle& car) {
    auto& slot = player_.garage[static_cast<std::size_t>(car.kind)];
    slot.paintColor = car.paintColor;
    slot.stripeColor = car.stripeColor;
    slot.engineUpgradeLevel = car.engineUpgradeLevel;
    slot.armorUpgradeLevel = car.armorUpgradeLevel;
    slot.neonPreset = car.neonPreset;
    slot.rimStyle = car.rimStyle;
    slot.customSpoiler = car.customSpoiler || (car.spoilerStyle > 0);
    slot.mattePaint = car.mattePaint;
    slot.chromePaint = car.chromePaint;
    slot.paintPresetIndex = car.paintPresetIndex;
    slot.stripeStyle = car.stripeStyle;
    slot.spoilerStyle = car.spoilerStyle;
    slot.stanceStyle = car.stanceStyle;
    slot.headlightColorIdx = car.headlightColorIdx;
    slot.driftSmokeColorIdx = car.driftSmokeColorIdx;
}

void GameWorld::applyPersonalTuningToVehicle(Vehicle& car, const PersonalVehicleTuning& tune) {
    const auto& spec = getVehicleSpec(car.kind);
    const float showroomBonus = player_.ownedProperties[static_cast<std::size_t>(PropertyId::LuxuryShowroom)] ? 1.30f : 1.0f;
    const float armorMult = (1.0f + 0.35f * static_cast<float>(tune.armorUpgradeLevel)) * showroomBonus;
    car.paintColor = tune.paintColor;
    car.stripeColor = tune.stripeColor;
    car.engineUpgradeLevel = tune.engineUpgradeLevel;
    car.armorUpgradeLevel = tune.armorUpgradeLevel;
    car.neonPreset = tune.neonPreset;
    car.rimStyle = tune.rimStyle;
    car.spoilerStyle = (tune.spoilerStyle > 0) ? tune.spoilerStyle : (tune.customSpoiler ? 1 : 0);
    car.customSpoiler = (car.spoilerStyle > 0);
    car.mattePaint = tune.mattePaint;
    car.chromePaint = tune.chromePaint;
    car.paintPresetIndex = tune.paintPresetIndex;
    car.stripeStyle = tune.stripeStyle;
    car.stanceStyle = tune.stanceStyle;
    car.headlightColorIdx = tune.headlightColorIdx;
    car.driftSmokeColorIdx = tune.driftSmokeColorIdx;
    car.maxHealth = spec.maxHealth * armorMult;
    car.health = car.maxHealth;
    car.nitroCharge = 1.0f;
    car.isPersonalCar = true;
}

bool GameWorld::isPlayerInWeaponShopZone() const {
    const auto* poi = city_.findPoi(PoiKind::WeaponShop);
    if (!poi || player_.drivingVehicleId >= 0) return false;
    return (player_.pos - poi->pos).length() <= poi->radius + 1.4f;
}

bool GameWorld::isPlayerInGarageZone() const {
    const auto* poi = city_.findPoi(PoiKind::TuneGarage);
    if (!poi) return false;
    return (playerWorldPos() - poi->pos).length() <= poi->radius + 2.4f;
}

bool GameWorld::isPlayerInDealershipZone() const {
    const auto* poi = city_.findPoi(PoiKind::CarDealership);
    if (!poi) return false;
    return (playerWorldPos() - poi->pos).length() <= poi->radius + 2.2f;
}

bool GameWorld::isPlayerInBoutiqueZone() const {
    const auto* poi = city_.findPoi(PoiKind::ClothingBoutique);
    if (!poi || player_.drivingVehicleId >= 0) return false;
    return (player_.pos - poi->pos).length() <= poi->radius + 1.6f;
}

bool GameWorld::isPlayerInGasStationZone() const {
    const auto* poi = city_.findPoi(PoiKind::GasStation);
    if (!poi) return false;
    return (playerWorldPos() - poi->pos).length() <= poi->radius + 2.6f;
}

bool GameWorld::isPlayerInNightclubZone() const {
    const auto* poi = city_.findPoi(PoiKind::NightclubProperty);
    if (!poi) return false;
    return (playerWorldPos() - poi->pos).length() <= poi->radius + 2.0f;
}

bool GameWorld::isPlayerInAnyShopZone() const {
    return isPlayerInWeaponShopZone() || isPlayerInGarageZone() ||
           isPlayerInDealershipZone() || isPlayerInBoutiqueZone() ||
           isPlayerInGasStationZone() || isPlayerInNightclubZone();
}

const Vehicle* GameWorld::nearestEnterableVehicle(float maxRange) const {
    if (player_.drivingVehicleId >= 0) return nullptr;
    const Vehicle* best = nullptr;
    float bestDist = maxRange;
    for (const auto& v : vehicles_) {
        if (v.destroyed) continue;
        if (std::abs(v.posY - player_.posY) > 2.2f) continue;
        const float d = (v.pos - player_.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    return best;
}

Vehicle* GameWorld::customizationTargetVehicle() {
    if (player_.drivingVehicleId >= 0) {
        Vehicle* driven = findVehicleMutable(player_.drivingVehicleId);
        if (driven && !driven->destroyed) return driven;
    }
    if (player_.activePersonalVehicleId >= 0) {
        Vehicle* personal = findVehicleMutable(player_.activePersonalVehicleId);
        if (personal && !personal->destroyed && (personal->pos - player_.pos).length() <= 14.0f) {
            return personal;
        }
    }
    Vehicle* best = nullptr;
    float bestDist = 9.5f;
    for (auto& v : vehicles_) {
        if (v.destroyed) continue;
        const float d = (v.pos - player_.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    return best;
}

const Vehicle* GameWorld::customizationTargetVehicle() const {
    if (player_.drivingVehicleId >= 0) {
        const Vehicle* driven = findVehicle(player_.drivingVehicleId);
        if (driven && !driven->destroyed) return driven;
    }
    if (player_.activePersonalVehicleId >= 0) {
        const Vehicle* personal = findVehicle(player_.activePersonalVehicleId);
        if (personal && !personal->destroyed && (personal->pos - player_.pos).length() <= 14.0f) {
            return personal;
        }
    }
    const Vehicle* best = nullptr;
    float bestDist = 9.5f;
    for (const auto& v : vehicles_) {
        if (v.destroyed) continue;
        const float d = (v.pos - player_.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    return best;
}

void GameWorld::maintainTrafficPopulation() {
    constexpr int kTargetTraffic = 28;
    int activeTraffic = 0;
    const Vec2 center = playerWorldPos();

    // Despawn far-away non-mission civilian AI cars
    vehicles_.erase(
        std::remove_if(vehicles_.begin(), vehicles_.end(), [&](const Vehicle& v) {
            if (v.isPlayerDriving || v.isMissionTarget || v.isPolice) return false;
            if (v.destroyed && v.destroyedTimer > 8.0f) return true;
            if (v.isParked) return false;
            return (v.pos - center).length() > 290.0f;
        }),
        vehicles_.end());

    for (const auto& v : vehicles_) {
        if (v.hasNpcDriver && !v.destroyed && !v.isPolice) ++activeTraffic;
    }

    int attempts = 0;
    std::vector<std::uint32_t> osmRoadPool;
    if (city_.hasRealSeoulMap() && city_.realData().isInCoreBounds(center)) {
        std::vector<std::uint32_t> rawRoads;
        city_.realData().queryChunkRoads(center, 235.0f, rawRoads);
        for (std::uint32_t ri : rawRoads) {
            const OsmRoadRec& r = city_.realData().roads()[ri];
            if (r.roadClass <= 4 && r.vertCount >= 2 && (r.flags & 4u) == 0u) {
                osmRoadPool.push_back(ri);
            }
        }
    }
    const int centerRx = city_.nearestRoadIndex(center.x);
    const int centerRz = city_.nearestRoadIndex(center.z);
    while (activeTraffic < kTargetTraffic && attempts < 80) {
        ++attempts;
        bool horiz = true;
        int roadIdx = 0;
        int dir = 1;
        float heading = 0.0f;
        Vec2 spawnPos{};
        float spawnY = -1e9f;
        std::uint32_t osmRi = UINT32_MAX;
        std::uint32_t osmVi = 0;
        int osmD = 1;

        if (!osmRoadPool.empty()) {
            osmRi = osmRoadPool[static_cast<std::size_t>(rng_.rangeInt(0, static_cast<int>(osmRoadPool.size()) - 1))];
            const OsmRoadRec& r = city_.realData().roads()[osmRi];
            const std::uint32_t seg = static_cast<std::uint32_t>(rng_.rangeInt(0, static_cast<int>(r.vertCount) - 2));
            const Vec2 a = city_.realData().roadVerts()[r.firstVert + seg];
            const Vec2 b = city_.realData().roadVerts()[r.firstVert + seg + 1];
            const Vec2 e = b - a;
            if (e.lengthSq() < 4.0f) continue;
            const bool isOneway = (r.flags & 1u) != 0u;
            osmD = isOneway ? 1 : (rng_.chance(0.5f) ? 1 : -1);
            const Vec2 tang = (osmD > 0 ? e : (e * -1.0f)).normalized();
            const Vec2 rightN{-tang.z, tang.x};
            heading = std::atan2(tang.z, tang.x);
            const RoadLaneLayout layout = RealSeoulData::computeRoadLaneLayout(r);
            const int laneIdx = (layout.lanesInDir > 1) ? rng_.rangeInt(0, layout.lanesInDir - 1) : 0;
            const float laneOff = layout.laneCenterOffset(laneIdx);
            const float frac = rng_.range(0.15f, 0.85f);
            spawnPos = a + e * frac + rightN * laneOff;
            osmVi = (osmD > 0) ? (seg + 1) : seg;
            const bool isElev = ((r.flags & 2u) != 0u) || (r.layer >= 1) || ((r.flags2 & 4u) != 0u);
            if (city_.realData().isInWater(spawnPos) && !isElev) continue;
            if (city_.realData().has3DRoads()) {
                spawnY = lerpf(city_.realData().roadVertY(r.firstVert + seg),
                               city_.realData().roadVertY(r.firstVert + seg + 1), frac) +
                         RealSeoulData::roadClassLift(r.roadClass);
            }
            if (!isElev) {
                Vec2 chk = spawnPos;
                if (city_.realData().resolveCircleBuildings(chk, 1.5f)) continue;
            }
        } else {
            horiz = rng_.chance(0.5f);
            const int baseRoad = horiz ? centerRz : centerRx;
            const int minR = horiz ? -140 : -170;
            const int maxR = horiz ?  140 :  170;
            roadIdx = std::clamp(baseRoad + rng_.rangeInt(-3, 3), minR, maxR);
            dir = rng_.chance(0.5f) ? 1 : -1;
            const float axisBase = horiz ? center.x : center.z;
            const float axisMin = horiz ? -14280.0f : -11760.0f;
            const float axisMax = horiz ?  14280.0f :  11760.0f;
            const float along = clampf(axisBase + rng_.range(-240.0f, 240.0f), axisMin, axisMax);
            const float rCoord = city_.roadCoord(roadIdx);

            if (horiz) {
                heading = (dir > 0) ? 0.0f : kPi;
                const Vec2 right = rightFromHeading(heading);
                spawnPos = {along, rCoord + right.z * CityMap::kLaneOffset};
            } else {
                heading = (dir > 0) ? kHalfPi : -kHalfPi;
                const Vec2 right = rightFromHeading(heading);
                spawnPos = {rCoord + right.x * CityMap::kLaneOffset, along};
            }
            if (city_.isInHanRiverWater(spawnPos, -6.0f) && !city_.isOnBridge(spawnPos, 0.0f)) continue;
            if (city_.terrainElevationAt(spawnPos) > 42.0f) continue;
        }

        const float distPlayer = (spawnPos - center).length();
        if (distPlayer < 32.0f || distPlayer > 265.0f) continue;

        const float resolvedSpawnY = (spawnY > -1e8f) ? spawnY : surfaceHeightAt(spawnPos);
        bool tooClose = false;
        for (const auto& other : vehicles_) {
            if (std::abs(other.posY - resolvedSpawnY) < 2.2f && (other.pos - spawnPos).length() < 11.5f) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Weighted realistic city traffic distribution across all civilian, performance & emergency classes
        static constexpr std::array<VehicleKind, 24> kTrafficPool = {{
            VehicleKind::Compact,         VehicleKind::Compact,
            VehicleKind::Taxi,            VehicleKind::Taxi,
            VehicleKind::SUV,             VehicleKind::SUV,
            VehicleKind::LuxurySedan,     VehicleKind::LuxurySedan,
            VehicleKind::SportsCoupe,     VehicleKind::SportsCoupe,
            VehicleKind::MuscleCar,       VehicleKind::MuscleCar,
            VehicleKind::OffroadPickup,   VehicleKind::OffroadPickup,
            VehicleKind::ClassicRoadster, VehicleKind::ClassicRoadster,
            VehicleKind::JdmTuner,        VehicleKind::JdmTuner,
            VehicleKind::DeliveryVan,     VehicleKind::Hypercar,
            VehicleKind::Ambulance,       VehicleKind::FireTruck,
            VehicleKind::SeoulBus,        VehicleKind::SeoulBus
        }};
        const VehicleKind kind = kTrafficPool[static_cast<std::size_t>(rng_.rangeInt(0, static_cast<int>(kTrafficPool.size()) - 1))];
        const auto& spec = getVehicleSpec(kind);
        Vehicle v{};
        v.id = nextVehicleId_++;
        v.kind = kind;
        v.pos = spawnPos;
        v.posY = resolvedSpawnY;
        v.heading = heading;
        v.velocity = forwardFromHeading(heading) * 14.5f;
        v.health = spec.maxHealth;
        v.maxHealth = spec.maxHealth;
        v.paintColor = randomTrafficPaint(rng_, kind);
        v.customSpoiler = spec.hasSpoiler;
        v.spoilerStyle = spec.hasSpoiler ? 1 : 0;
        v.hasNpcDriver = true;
        v.aiHorizontalRoad = horiz;
        v.aiRoadIndex = roadIdx;
        v.aiDirection = dir;
        v.osmRoadIdx = osmRi;
        v.osmVertIdx = osmVi;
        v.osmDir = osmD;
        vehicles_.push_back(v);
        ++activeTraffic;
    }
}

void GameWorld::maintainCivilianPopulation() {
    constexpr int kTargetCivilians = 44;
    const Vec2 center = playerWorldPos();

    npcs_.erase(
        std::remove_if(npcs_.begin(), npcs_.end(), [&](const NpcCharacter& n) {
            if (n.missionTarget) return !n.alive && n.corpseTimer > 6.0f;
            if (!n.alive && n.corpseTimer > 5.0f) return true;
            if (n.role == NpcRole::Gangster) return false;
            if (n.role == NpcRole::Police) {
                const float maxDist = (player_.wantedLevel > 0) ? 145.0f : 210.0f;
                return (n.pos - center).length() > maxDist;
            }
            return (n.pos - center).length() > 210.0f;
        }),
        npcs_.end());

    int civCount = 0;
    for (const auto& n : npcs_) {
        if (n.alive && n.role == NpcRole::Civilian) ++civCount;
    }

    std::vector<std::uint32_t> osmPedRoads;
    if (city_.hasRealSeoulMap() && city_.realData().isInCoreBounds(center)) {
        std::vector<std::uint32_t> rawRoads;
        city_.realData().queryChunkRoads(center, 185.0f, rawRoads);
        for (std::uint32_t ri : rawRoads) {
            const OsmRoadRec& r = city_.realData().roads()[ri];
            // Strictly exclude motorways/expressways (roadClass <= 1) & bridges/tunnels from pedestrian spawning!
            if (r.roadClass >= 2 && r.roadClass <= 5 && r.vertCount >= 2 && (r.flags & 6u) == 0u) {
                osmPedRoads.push_back(ri);
            }
        }
    }

    int attempts = 0;
    const int minU = -170 * 2 + 1;
    const int maxU =  170 * 2 - 2;
    const int minV = -140 * 2 + 1;
    const int maxV =  140 * 2 - 2;
    const int centerNodeU = std::clamp(city_.nearestSidewalkNodeIndex(center.x), minU, maxU);
    const int centerNodeV = std::clamp(city_.nearestSidewalkNodeIndex(center.z), minV, maxV);
    while (civCount < kTargetCivilians && attempts < 280) {
        ++attempts;
        int fromU = 0, fromV = 0, toU = 0, toV = 0;
        float edgeYaw = 0.0f;
        Vec2 p{};
        std::uint32_t osmRi = UINT32_MAX;
        std::uint32_t osmVi = 0;
        int osmD = 1;
        float osmSide = 1.0f;

        if (!osmPedRoads.empty()) {
            osmRi = osmPedRoads[static_cast<std::size_t>(rng_.rangeInt(0, static_cast<int>(osmPedRoads.size()) - 1))];
            const OsmRoadRec& r = city_.realData().roads()[osmRi];
            const std::uint32_t seg = static_cast<std::uint32_t>(rng_.rangeInt(0, static_cast<int>(r.vertCount) - 2));
            const Vec2 a = city_.realData().roadVerts()[r.firstVert + seg];
            const Vec2 b = city_.realData().roadVerts()[r.firstVert + seg + 1];
            const Vec2 e = b - a;
            const float segLen = e.length();
            if (segLen < 2.0f) continue;
            // Sample around the closest point to `center` so long 700m boulevards spawn nearby civilians reliably!
            const float tNear = clampf((center - a).dot(e) / (segLen * segLen), 0.0f, 1.0f);
            const float tSpan = std::min(0.45f, 150.0f / std::max(segLen, 1.0f));
            const float tMin = std::max(0.05f, tNear - tSpan);
            const float tMax = std::min(0.95f, tNear + tSpan);
            const float tPick = (tMax > tMin) ? rng_.range(tMin, tMax) : tNear;

            osmD = rng_.chance(0.5f) ? 1 : -1;
            const float firstSide = rng_.chance(0.5f) ? 1.0f : -1.0f;
            const Vec2 tang = (osmD > 0 ? e : (e * -1.0f)) / segLen;
            const Vec2 rightN{-tang.z, tang.x};
            edgeYaw = std::atan2(tang.z, tang.x);
            const float hw = std::max(2.6f, r.halfWidth);
            osmVi = (osmD > 0) ? (seg + 1) : seg;

            bool foundClearSide = false;
            for (float trySide : {firstSide, -firstSide}) {
                const Vec2 cand = a + e * tPick + rightN * (trySide * (hw + 2.05f));
                Vec2 chk = cand;
                if (!city_.realData().isInWater(cand) &&
                    !city_.realData().isOnRoad(cand, 0.85f) &&
                    !city_.realData().resolveCircleBuildings(chk, 0.65f)) {
                    p = cand;
                    osmSide = trySide;
                    foundClearSide = true;
                    break;
                }
            }
            if (!foundClearSide) continue;
        } else {
            const bool horiz = rng_.chance(0.5f);
            const int uNear = std::clamp(centerNodeU + rng_.rangeInt(-5, 5), minU, maxU);
            const int vNear = std::clamp(centerNodeV + rng_.rangeInt(-5, 5), minV, maxV);
            const int rawIdx = horiz ? uNear : vNear;
            const int blk = (rawIdx >= 1) ? ((rawIdx - 1) / 2) : ((rawIdx - 2) / 2);
            const int sideLine = horiz ? vNear : uNear;
            const bool fwdOrder = rng_.chance(0.5f);

            int n0 = 2 * blk + 1;
            int n1 = 2 * blk + 2;
            if (!fwdOrder) std::swap(n0, n1);

            fromU = horiz ? n0 : sideLine;
            fromV = horiz ? sideLine : n0;
            toU   = horiz ? n1 : sideLine;
            toV   = horiz ? sideLine : n1;

            const Vec2 a{city_.sidewalkCoord(fromU), city_.sidewalkCoord(fromV)};
            const Vec2 b{city_.sidewalkCoord(toU), city_.sidewalkCoord(toV)};
            const Vec2 segDir = (b - a).normalized();
            edgeYaw = std::atan2(segDir.z, segDir.x);
            const Vec2 laneOffset = rightFromHeading(edgeYaw) * 0.45f;
            const float frac = rng_.range(0.15f, 0.85f);
            p = a + (b - a) * frac + laneOffset;
            const Vec2 mid = (a + b) * 0.5f;
            const Vec2 q1 = a + (b - a) * 0.25f;
            const Vec2 q3 = a + (b - a) * 0.75f;
            if (city_.isInHanRiverWater(p, -8.0f) || city_.isInTributaryWater(p, 2.0f) ||
                city_.isOnRiversideExpressway(p) || city_.isOnRiversideExpressway(a) ||
                city_.isOnRiversideExpressway(b) || city_.isOnRiversideExpressway(mid) ||
                city_.isOnRiversideExpressway(q1) || city_.isOnRiversideExpressway(q3) ||
                city_.terrainElevationAt(p) > 36.0f) continue;
        }

        const float d = (p - center).length();
        if (d < 16.0f || d > 185.0f) continue;

        bool tooCloseNpc = false;
        for (const auto& other : npcs_) {
            if (other.alive && (other.pos - p).lengthSq() < 2.2f) {
                tooCloseNpc = true;
                break;
            }
        }
        if (tooCloseNpc) continue;

        NpcCharacter npc{};
        npc.id = nextNpcId_++;
        npc.role = NpcRole::Civilian;
        npc.behavior = NpcBehavior::Stroll;
        npc.pos = p;
        npc.facingYaw = edgeYaw;
        npc.navFromU = fromU;
        npc.navFromV = fromV;
        npc.navToU = toU;
        npc.navToV = toV;
        npc.osmRoadIdx = osmRi;
        npc.osmVertIdx = osmVi;
        npc.osmDir = osmD;
        npc.osmSideSign = osmSide;

        // 5 natural skin tones matching getPlayerSkinColor palette
        static constexpr std::array<ColorRGBA, 5> kNpcSkinTones = {{
            {242, 212, 188, 255},
            {232, 192, 158, 255},
            {210, 162, 118, 255},
            {174, 126,  86, 255},
            {122,  82,  56, 255}
        }};
        static constexpr std::array<ColorRGBA, 6> kUmbrellaPalette = {{
            { 34,  44,  62, 255}, // Classic Navy
            { 30,  32,  38, 255}, // Executive Charcoal Black
            {138,  38,  46, 255}, // Deep Burgundy
            { 42,  76,  64, 255}, // Forest Green
            {168, 134,  76, 255}, // Warm Ochre
            { 74,  92, 114, 255}  // Slate Blue
        }};
        npc.skinColor = kNpcSkinTones[static_cast<std::size_t>(rng_.rangeInt(0, 4))];
        npc.umbrellaColor = kUmbrellaPalette[static_cast<std::size_t>(rng_.rangeInt(0, 5))];
        npc.heightScale = rng_.range(0.93f, 1.07f);
        npc.hairColorIdx = rng_.chance(0.86f) ? rng_.rangeInt(0, 2) : rng_.rangeInt(0, 5);

        const float archRoll = rng_.range(0.0f, 1.0f);
        if (archRoll < 0.22f) {
            // Downtown Executive / Business Commuter
            npc.archetype = NpcArchetype::Business;
            npc.outfitStyle = rng_.chance(0.24f) ? OutfitId::ViceWhiteSuit : OutfitId::MidnightTuxedo;
            npc.outfitTop = (npc.outfitStyle == OutfitId::ViceWhiteSuit)
                                ? ColorRGBA{204, 202, 196, 255}
                                : (rng_.chance(0.55f) ? ColorRGBA{30, 34, 42, 255} : ColorRGBA{42, 52, 68, 255});
            npc.outfitBottom = (npc.outfitStyle == OutfitId::ViceWhiteSuit)
                                   ? ColorRGBA{186, 184, 178, 255}
                                   : ColorRGBA{24, 26, 34, 255};
            npc.hairStyle = rng_.chance(0.5f) ? 1 : 0;
            npc.glassesStyle = rng_.chance(0.38f) ? 2 : (rng_.chance(0.25f) ? 1 : 0);
            npc.heldProp = rng_.chance(0.60f) ? NpcHeldProp::Briefcase : NpcHeldProp::CoffeeCup;
            npc.walkSpeed = rng_.range(1.62f, 2.02f);
        } else if (archRoll < 0.36f) {
            // Park & Sunset Beach Fitness Jogger
            npc.archetype = NpcArchetype::Jogger;
            npc.outfitStyle = OutfitId::StreetCasual;
            static constexpr std::array<ColorRGBA, 4> kSportTops = {{
                { 46, 118,  98, 255}, {178,  84,  48, 255},
                { 56,  98, 152, 255}, {148,  52,  76, 255}
            }};
            npc.outfitTop = kSportTops[static_cast<std::size_t>(rng_.rangeInt(0, 3))];
            npc.outfitBottom = {32, 36, 44, 255};
            npc.hairStyle = rng_.chance(0.45f) ? 3 : 0;
            npc.glassesStyle = rng_.chance(0.30f) ? 1 : 0;
            npc.heldProp = NpcHeldProp::None;
            npc.walkSpeed = rng_.range(2.95f, 3.55f);
        } else if (archRoll < 0.52f) {
            // Coastal Tourist / Boutique Shopper
            npc.archetype = NpcArchetype::Tourist;
            npc.outfitStyle = OutfitId::StreetCasual;
            static constexpr std::array<ColorRGBA, 5> kCoastalTops = {{
                {194, 184, 166, 255}, {112, 138, 156, 255},
                {166, 122,  98, 255}, {118, 138, 108, 255}, {182, 164, 134, 255}
            }};
            npc.outfitTop = kCoastalTops[static_cast<std::size_t>(rng_.rangeInt(0, 4))];
            npc.outfitBottom = {
                static_cast<std::uint8_t>(rng_.rangeInt(124, 162)),
                static_cast<std::uint8_t>(rng_.rangeInt(114, 148)),
                static_cast<std::uint8_t>(rng_.rangeInt(92, 124)),
                255
            };
            npc.hairStyle = rng_.chance(0.55f) ? 3 : 1;
            npc.glassesStyle = rng_.chance(0.65f) ? 1 : 2;
            npc.backGearStyle = rng_.chance(0.42f) ? 1 : 0;
            npc.heldProp = rng_.chance(0.55f) ? NpcHeldProp::ShoppingBag : NpcHeldProp::CoffeeCup;
            npc.walkSpeed = rng_.range(1.26f, 1.66f);
        } else if (archRoll < 0.68f) {
            // Street Style / Smartphone Tech Commuter
            npc.archetype = NpcArchetype::StreetTech;
            npc.outfitStyle = rng_.chance(0.42f) ? OutfitId::RacerLeather : OutfitId::StreetCasual;
            npc.outfitTop = rng_.chance(0.5f) ? ColorRGBA{44, 48, 56, 255} : ColorRGBA{128, 44, 52, 255};
            npc.outfitBottom = {32, 36, 44, 255};
            npc.hairStyle = rng_.chance(0.20f) ? 2 : (rng_.chance(0.5f) ? 3 : 1);
            npc.glassesStyle = rng_.chance(0.35f) ? 1 : 0;
            npc.backGearStyle = rng_.chance(0.50f) ? 1 : 0;
            npc.heldProp = NpcHeldProp::Smartphone;
            npc.walkSpeed = rng_.range(1.42f, 1.82f);
        } else if (archRoll < 0.80f) {
            // City Crew / High-Vis Worker
            npc.archetype = NpcArchetype::Worker;
            npc.outfitStyle = OutfitId::StreetCasual;
            npc.outfitTop = rng_.chance(0.55f) ? ColorRGBA{196, 152, 44, 255} : ColorRGBA{186, 98, 42, 255};
            npc.outfitBottom = {44, 52, 64, 255};
            npc.hairStyle = rng_.chance(0.65f) ? 3 : 0;
            npc.glassesStyle = rng_.chance(0.25f) ? 1 : 0;
            npc.heldProp = rng_.chance(0.35f) ? NpcHeldProp::CoffeeCup : NpcHeldProp::None;
            npc.walkSpeed = rng_.range(1.48f, 1.86f);
        } else {
            // Realistic Urban Casual Pedestrian (denim, wool, olive, camel, heather grey, navy, burgundy)
            npc.archetype = NpcArchetype::Casual;
            npc.outfitStyle = OutfitId::StreetCasual;
            static constexpr std::array<ColorRGBA, 8> kUrbanTops = {{
                { 52,  66,  86, 255}, { 88,  92,  96, 255},
                { 66,  78,  62, 255}, {148, 126,  98, 255},
                {118,  52,  56, 255}, {178, 174, 166, 255},
                { 42,  46,  52, 255}, { 96,  78,  64, 255}
            }};
            static constexpr std::array<ColorRGBA, 4> kUrbanBottoms = {{
                { 38,  48,  66, 255}, { 32,  34,  40, 255},
                { 78,  72,  60, 255}, { 54,  58,  64, 255}
            }};
            npc.outfitTop = kUrbanTops[static_cast<std::size_t>(rng_.rangeInt(0, 7))];
            npc.outfitBottom = kUrbanBottoms[static_cast<std::size_t>(rng_.rangeInt(0, 3))];
            npc.hairStyle = rng_.rangeInt(0, 1);
            npc.glassesStyle = rng_.chance(0.22f) ? 1 : 0;
            npc.backGearStyle = rng_.chance(0.20f) ? 1 : 0;
            npc.heldProp = rng_.chance(0.32f) ? NpcHeldProp::Smartphone
                         : (rng_.chance(0.28f) ? NpcHeldProp::CoffeeCup : NpcHeldProp::None);
            npc.walkSpeed = rng_.range(1.38f, 1.92f);
        }

        npc.holdingUmbrella = rainWeather_ && (npc.archetype != NpcArchetype::Jogger);
        npcs_.push_back(npc);
        ++civCount;
    }
}

void GameWorld::spawnPoliceOfficerNear(const Vec2& pos, bool swat) {
    Vec2 spawnP = pos;
    city_.resolveCircleWorld(spawnP, 0.45f);

    const int minU = CityMap::kMinRoadIndexX * 2 + 1;
    const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
    const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
    const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
    const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(spawnP.x), minU, maxU);
    const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(spawnP.z), minV, maxV);
    const int partnerU = ((nearU & 1) != 0) ? (nearU + 1) : (nearU - 1);

    NpcCharacter cop{};
    cop.id = nextNpcId_++;
    cop.role = NpcRole::Police;
    cop.isSwat = swat;
    cop.behavior = (player_.wantedLevel > 0 && player_.policeLineOfSight) ? NpcBehavior::CombatEngage : NpcBehavior::Stroll;
    cop.pos = spawnP;
    cop.homeAnchor = spawnP;
    cop.navFromU = nearU;
    cop.navFromV = nearV;
    cop.navToU = partnerU;
    cop.navToV = nearV;
    cop.facingYaw = std::atan2(player_.policeLastKnownPos.z - spawnP.z, player_.policeLastKnownPos.x - spawnP.x);
    cop.fireCooldown = rng_.range(0.38f, 0.78f);

    if (swat) {
        cop.health = 140.0f;
        cop.maxHealth = 140.0f;
        cop.walkSpeed = 1.72f;
        cop.weapon = (player_.wantedLevel >= 5 && rng_.chance(0.40f)) ? WeaponId::CombatMG : WeaponId::AssaultRifle;
        cop.outfitTop = {26, 32, 42, 255};
        cop.outfitBottom = {20, 24, 32, 255};
    } else {
        cop.health = 92.0f;
        cop.maxHealth = 92.0f;
        cop.walkSpeed = 1.60f;
        cop.weapon = (player_.wantedLevel >= 3)
                         ? (rng_.chance(0.45f) ? WeaponId::Shotgun : WeaponId::SMG)
                         : ((player_.wantedLevel >= 2 && rng_.chance(0.35f)) ? WeaponId::SMG : WeaponId::Pistol);
        cop.outfitTop = {22, 44, 86, 255};
        cop.outfitBottom = {18, 28, 52, 255};
    }

    npcs_.push_back(cop);
}

void GameWorld::maintainPolicePopulation(float dt) {
    if (policeSpawnCooldown_ > 0.0f) {
        policeSpawnCooldown_ = std::max(0.0f, policeSpawnCooldown_ - dt);
    }

    const Vec2 center = playerWorldPos();
    const bool wanted = (player_.wantedLevel > 0);
    const bool searchingOutOfSight = wanted && !player_.policeLineOfSight;

    // Keep active pursuing cruisers up to 380m so outrunning them genuinely creates distance
    // (instead of deleting at 165m and teleport-respawning right in front of the player!)
    vehicles_.erase(
        std::remove_if(vehicles_.begin(), vehicles_.end(), [&](const Vehicle& v) {
            if (!v.isPolice || v.isPlayerDriving || v.isMissionTarget) return false;
            if (v.destroyed && v.destroyedTimer > 8.0f) return true;
            if (v.isParked) return false;
            const float maxDist = wanted ? 380.0f : 310.0f;
            return (v.pos - center).length() > maxDist;
        }),
        vehicles_.end());

    int activeCruisers = 0;
    for (const auto& v : vehicles_) {
        if (v.isPolice && v.hasNpcDriver && !v.destroyed) ++activeCruisers;
    }

    int activeOfficers = 0;
    for (const auto& n : npcs_) {
        if (n.alive && n.role == NpcRole::Police) ++activeOfficers;
    }

    // While player has broken line of sight & is evading, NEVER teleport fresh cops around the player!
    if (searchingOutOfSight) {
        return;
    }

    const int targetCruisers = wanted ? std::min(4, 1 + (player_.wantedLevel + 1) / 2) : 2;
    const int targetOfficers = wanted ? std::min(6, 1 + player_.wantedLevel) : 3;

    if (wanted && policeSpawnCooldown_ > 0.0f && activeCruisers > 0) {
        return;
    }

    bool spawnedWantedWave = false;
    const Vec2 dispatchAnchor = wanted ? player_.policeLastKnownPos : center;
    const int centerRx = city_.nearestRoadIndex(dispatchAnchor.x);
    const int centerRz = city_.nearestRoadIndex(dispatchAnchor.z);

    int attempts = 0;
    while (activeCruisers < targetCruisers && attempts < 48) {
        ++attempts;
        const bool horiz = rng_.chance(0.5f);
        const int nearIdx = horiz ? centerRz : centerRx;
        const int minR = horiz ? -140 : -170;
        const int maxR = horiz ?  140 :  170;
        const int spread = wanted ? 2 : 3;
        const int roadIdx = std::clamp(nearIdx + rng_.rangeInt(-spread, spread), minR, maxR);
        const float rCoord = city_.roadCoord(roadIdx);
        // Spawn reinforcements realistically down the boulevard (115m..225m away — zero popup ambush!)
        const float distOff = wanted ? (rng_.chance(0.5f) ? rng_.range(115.0f, 215.0f) : rng_.range(-215.0f, -115.0f))
                                     : rng_.range(-230.0f, 230.0f);
        const float axisMin = horiz ? -14280.0f : -11760.0f;
        const float axisMax = horiz ?  14280.0f :  11760.0f;
        const float along = clampf((horiz ? dispatchAnchor.x : dispatchAnchor.z) + distOff, axisMin, axisMax);

        int dir = rng_.chance(0.5f) ? 1 : -1;
        if (wanted) {
            dir = ((horiz ? dispatchAnchor.x : dispatchAnchor.z) >= along) ? 1 : -1;
        }

        float heading = 0.0f;
        Vec2 spawnPos{};
        if (horiz) {
            heading = (dir > 0) ? 0.0f : kPi;
            const Vec2 right = rightFromHeading(heading);
            spawnPos = {along, rCoord + right.z * CityMap::kLaneOffset};
        } else {
            heading = (dir > 0) ? kHalfPi : -kHalfPi;
            const Vec2 right = rightFromHeading(heading);
            spawnPos = {rCoord + right.x * CityMap::kLaneOffset, along};
        }

        if (city_.isInHanRiverWater(spawnPos, -6.0f) && !city_.isOnBridge(spawnPos, 0.0f)) continue;
        if (city_.terrainElevationAt(spawnPos) > 40.0f) continue;

        const float distPlayer = (spawnPos - center).length();
        if (wanted) {
            if (distPlayer < 105.0f || distPlayer > 245.0f) continue;
        } else {
            if (distPlayer < 45.0f || distPlayer > 250.0f) continue;
        }

        bool tooClose = false;
        for (const auto& other : vehicles_) {
            if ((other.pos - spawnPos).length() < 12.0f) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        const bool swatTruck = wanted && player_.wantedLevel >= 4 && rng_.chance(0.38f);
        const VehicleKind kind = swatTruck ? VehicleKind::ArmoredSuv : VehicleKind::PoliceCruiser;
        const auto& spec = getVehicleSpec(kind);

        Vehicle cruiser{};
        cruiser.id = nextVehicleId_++;
        cruiser.kind = kind;
        cruiser.pos = spawnPos;
        cruiser.posY = surfaceHeightAt(spawnPos);
        cruiser.heading = heading;
        cruiser.velocity = forwardFromHeading(heading) * (wanted ? 18.0f : 13.5f);
        cruiser.health = spec.maxHealth;
        cruiser.maxHealth = spec.maxHealth;
        // Authentic Korean National Police livery (White + Navy stripe) or dark Tactical SWAT SUV
        cruiser.paintColor = swatTruck ? ColorRGBA{24, 28, 36, 255} : ColorRGBA{240, 244, 248, 255};
        cruiser.stripeColor = swatTruck ? ColorRGBA{220, 224, 232, 255} : ColorRGBA{24, 54, 126, 255};
        cruiser.mattePaint = swatTruck;
        cruiser.rimStyle = 2;
        cruiser.engineUpgradeLevel = 0; // Fair stock cruiser performance — no unfair AI speed cheats!
        cruiser.armorUpgradeLevel = swatTruck ? 2 : 0;
        cruiser.hasNpcDriver = true;
        cruiser.isPolice = true;
        cruiser.sirenActive = wanted;
        cruiser.sirenPhase = rng_.range(0.0f, 1.0f);
        cruiser.aiHorizontalRoad = horiz;
        cruiser.aiRoadIndex = roadIdx;
        cruiser.aiDirection = dir;
        vehicles_.push_back(cruiser);
        ++activeCruisers;
        if (wanted) spawnedWantedWave = true;
    }

    attempts = 0;
    const int minU = -170 * 2 + 1;
    const int maxU =  170 * 2 - 2;
    const int minV = -140 * 2 + 1;
    const int maxV =  140 * 2 - 2;
    const int centerNodeU = std::clamp(city_.nearestSidewalkNodeIndex(dispatchAnchor.x), minU, maxU);
    const int centerNodeV = std::clamp(city_.nearestSidewalkNodeIndex(dispatchAnchor.z), minV, maxV);
    while (activeOfficers < targetOfficers && attempts < 48) {
        ++attempts;
        const bool horiz = rng_.chance(0.5f);
        const int nodeSpread = wanted ? 3 : 5;
        const int nearNodeU = std::clamp(centerNodeU + rng_.rangeInt(-nodeSpread, nodeSpread), minU, maxU);
        const int nearNodeV = std::clamp(centerNodeV + rng_.rangeInt(-nodeSpread, nodeSpread), minV, maxV);
        const int rawIdx = horiz ? nearNodeU : nearNodeV;
        const int blk = (rawIdx >= 1) ? ((rawIdx - 1) / 2) : ((rawIdx - 2) / 2);
        const int sideLine = horiz ? nearNodeV : nearNodeU;

        const int n0 = 2 * blk + 1;
        const int n1 = 2 * blk + 2;
        const int fromU = horiz ? n0 : sideLine;
        const int fromV = horiz ? sideLine : n0;
        const int toU   = horiz ? n1 : sideLine;
        const int toV   = horiz ? sideLine : n1;

        const Vec2 a{city_.sidewalkCoord(fromU), city_.sidewalkCoord(fromV)};
        const Vec2 b{city_.sidewalkCoord(toU), city_.sidewalkCoord(toV)};
        const Vec2 segDir = (b - a).normalized();
        const float edgeYaw = std::atan2(segDir.z, segDir.x);
        const Vec2 p = a + (b - a) * rng_.range(0.2f, 0.8f) + rightFromHeading(edgeYaw) * 0.45f;
        const Vec2 mid = (a + b) * 0.5f;
        const Vec2 q1 = a + (b - a) * 0.25f;
        const Vec2 q3 = a + (b - a) * 0.75f;
        if (city_.isInHanRiverWater(p, -8.0f) || city_.isInTributaryWater(p, 2.0f) ||
            city_.isOnRiversideExpressway(p) || city_.isOnRiversideExpressway(a) ||
            city_.isOnRiversideExpressway(b) || city_.isOnRiversideExpressway(mid) ||
            city_.isOnRiversideExpressway(q1) || city_.isOnRiversideExpressway(q3) ||
            city_.terrainElevationAt(p) > 36.0f) continue;

        const float d = (p - center).length();
        if (wanted) {
            if (d < 55.0f || d > 135.0f) continue;
        } else {
            if (d < 32.0f || d > 180.0f) continue;
        }

        bool tooCloseNpc = false;
        for (const auto& other : npcs_) {
            if (other.alive && (other.pos - p).lengthSq() < 2.5f) {
                tooCloseNpc = true;
                break;
            }
        }
        if (tooCloseNpc) continue;

        const bool swat = wanted && player_.wantedLevel >= 4 && rng_.chance(0.50f);
        spawnPoliceOfficerNear(p, swat);
        ++activeOfficers;
        if (wanted) spawnedWantedWave = true;
    }

    if (spawnedWantedWave) {
        policeSpawnCooldown_ = 5.5f; // Realistic dispatch delay instead of 1.45s spam!
    }
}

void GameWorld::updateWantedSystem(float dt) {
    if (player_.wantedLevel <= 0) {
        player_.wantedEvadeTimer = 0.0f;
        player_.policeLineOfSight = false;
        player_.policeSearching = false;
        return;
    }

    const Vec2 pPos = playerWorldPos();
    const float weatherVis = rainWeather_ ? 0.76f : 1.0f;
    const float nightVis = isNightTime() ? 0.80f : 1.0f;
    const float visScale = weatherVis * nightVis;

    const float officerSightRange = (48.0f + ((player_.wantedLevel >= 4) ? 6.0f : 0.0f)) * visScale;
    const float cruiserSightRange = (74.0f + ((player_.wantedLevel >= 4) ? 10.0f : 0.0f)) * visScale;

    bool hasLos = false;
    for (const auto& npc : npcs_) {
        if (!npc.alive || npc.role != NpcRole::Police) continue;
        const Vec2 delta = pPos - npc.pos;
        const float dist = delta.length();
        if (dist <= officerSightRange) {
            const Vec2 dir = delta / std::max(dist, 0.001f);
            const float facingDot = forwardFromHeading(npc.facingYaw).dot(dir);
            // Realistic vision cone: full range ahead, shorter peripheral/rear awareness (18m)
            const float effRange = (facingDot > 0.15f) ? officerSightRange : std::min(officerSightRange, 18.0f);
            if (dist <= effRange && (dist < 6.0f || city_.raycastBuildings(npc.pos, dir, dist) < 0.0f)) {
                hasLos = true;
                break;
            }
        }
    }

    if (!hasLos) {
        for (const auto& v : vehicles_) {
            if (v.destroyed || !v.isPolice || !v.hasNpcDriver) continue;
            const Vec2 delta = pPos - v.pos;
            const float dist = delta.length();
            if (dist <= cruiserSightRange) {
                const Vec2 dir = delta / std::max(dist, 0.001f);
                const float facingDot = forwardFromHeading(v.heading).dot(dir);
                const float effRange = (facingDot > 0.20f) ? cruiserSightRange : std::min(cruiserSightRange, 24.0f);
                if (dist <= effRange && (dist < 8.0f || city_.raycastBuildings(v.pos, dir, dist) < 0.0f)) {
                    hasLos = true;
                    break;
                }
            }
        }
    }

    if (hasLos) {
        player_.policeLineOfSight = true;
        player_.policeSearching = false;
        player_.policeLastKnownPos = pPos;
        player_.wantedEvadeTimer = std::max(0.0f, player_.wantedEvadeTimer - dt * 1.15f);
    } else {
        player_.policeLineOfSight = false;
        player_.policeSearching = true;
        // Stealth bonus when hiding slow/parked out of sight or away from LKP!
        const float pSpd = (playerVehicle() != nullptr) ? playerVehicle()->speed() : player_.velocity.length();
        const float distLkp = (pPos - player_.policeLastKnownPos).length();
        float evadeRate = 1.0f;
        if (pSpd < 3.5f && distLkp > 35.0f) {
            evadeRate = 1.55f; // Hiding quietly in alley/park accelerates escape!
        } else if (distLkp > 140.0f) {
            evadeRate = 1.35f; // Outran search zone!
        }
        player_.wantedEvadeTimer += dt * evadeRate;
        if (player_.wantedEvadeTimer >= requiredEvadeDuration()) {
            clearWantedLevel(true);
        }
    }
}

bool GameWorld::tryEnterOrExitVehicle() {
    if (player_.wasted) return false;

    // Exit current vehicle
    if (player_.drivingVehicleId >= 0) {
        Vehicle* car = findVehicleMutable(player_.drivingVehicleId);
        player_.drivingVehicleId = -1;
        if (!car) return true;

        car->isPlayerDriving = false;
        car->nitroActive = false;
        car->isDrifting = false;
        const auto& spec = getVehicleSpec(car->kind);
        const Vec2 right = rightFromHeading(car->heading);

        // Try left (driver) door first, then right door, then rear
        Vec2 exitPos = car->pos - right * (spec.halfWidth + 0.95f);
        if (city_.resolveCircleWorld(exitPos, 0.45f)) {
            exitPos = car->pos + right * (spec.halfWidth + 0.95f);
            city_.resolveCircleWorld(exitPos, 0.45f);
        }

        player_.pos = exitPos;
        const float exitSurfY = surfaceHeightAt(exitPos, car->posY);
        player_.posY = std::max(exitSurfY, car->posY);
        player_.verticalVel = (player_.posY > exitSurfY + 0.05f) ? car->velY : 0.0f;
        player_.onGround = (player_.posY <= exitSurfY + 0.05f);
        player_.velocity = car->velocity * 0.25f;
        player_.facingYaw = car->heading;
        pushAudio(AudioEventType::CarDoor, 0.85f);
        return true;
    }

    // Find closest vehicle to enter or carjack
    Vehicle* best = nullptr;
    float bestDist = 4.6f;
    for (auto& v : vehicles_) {
        if (v.destroyed) continue;
        if (std::abs(v.posY - player_.posY) > 2.2f) continue;
        const float d = (v.pos - player_.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    if (!best) return false;

    // If an NPC is driving, pull them out onto the street!
    if (best->hasNpcDriver) {
        best->hasNpcDriver = false;
        const auto& spec = getVehicleSpec(best->kind);
        Vec2 driverSide = best->pos - rightFromHeading(best->heading) * (spec.halfWidth + 0.95f);
        city_.resolveCircleWorld(driverSide, 0.45f);
        const int minU = CityMap::kMinRoadIndexX * 2 + 1;
        const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
        const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
        const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
        const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(driverSide.x), minU, maxU);
        const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(driverSide.z), minV, maxV);
        const int partnerU = ((nearU & 1) != 0) ? (nearU + 1) : (nearU - 1);

        if (best->isPolice) {
            spawnPoliceOfficerNear(driverSide, best->kind == VehicleKind::ArmoredSuv);
            triggerCivilianPanicNear(best->pos, 22.0f);
            reportCrime(3.4f, 2, best->pos, true);
        } else {
            NpcCharacter ejected{};
            ejected.id = nextNpcId_++;
            ejected.role = NpcRole::Civilian;
            ejected.behavior = NpcBehavior::PanicFlee;
            ejected.stateTimer = 9.5f;
            ejected.panicOrigin = best->pos;
            ejected.pos = driverSide;
            ejected.navFromU = nearU;
            ejected.navFromV = nearV;
            ejected.navToU = partnerU;
            ejected.navToV = nearV;
            ejected.walkSpeed = 1.75f;
            ejected.facingYaw = wrapAngle(best->heading + kPi);
            ejected.outfitTop = {235, 180, 75, 255};
            npcs_.push_back(ejected);

            triggerCivilianPanicNear(best->pos, 18.0f);
            reportCrime(1.35f, 1, best->pos, false);
        }
    } else if (best->isPolice) {
        reportCrime(3.2f, 2, best->pos, true);
    }

    best->isParked = false;
    best->isPlayerDriving = true;
    best->driverPanicked = false;
    best->rollVel -= 1.65f;   // Tactile driver-side suspension compression on boarding!
    best->pitchVel += 0.45f;
    player_.drivingVehicleId = best->id;
    player_.reloadTimer = 0.0f;
    player_.onGround = true;
    player_.verticalVel = 0.0f;
    pushAudio(AudioEventType::CarDoor, 1.0f);

    if (best->kind == VehicleKind::Ambulance && best->medkitAvailable) {
        if (player_.health < player_.maxHealth - 0.5f) {
            player_.health = std::min(player_.maxHealth, player_.health + 35.0f);
            best->medkitAvailable = false;
            pushAudio(AudioEventType::CashPickup, 0.9f);
            pushBanner("PARAMEDIC MEDKIT (+35 HP)", "119 구급차 응급 키트 사용 (+35 체력 회복!)",
                       "Press [G] to toggle EMS Siren", "[G] 키로 구급 사이렌을 켤 수 있습니다",
                       {98, 242, 132, 255}, 2.4f);
        } else if (player_.armor < player_.maxArmor - 0.5f) {
            player_.armor = std::min(player_.maxArmor, player_.armor + 25.0f);
            best->medkitAvailable = false;
            pushAudio(AudioEventType::CashPickup, 0.9f);
            pushBanner("EMS TRAUMA KIT (+25 ARMOR)", "119 구급차 외상 방호키트 (+25 방탄 보충!)",
                       "Press [G] to toggle EMS Siren", "[G] 키로 구급 사이렌을 켤 수 있습니다",
                       {92, 212, 255, 255}, 2.4f);
        }
    }
    return true;
}

void GameWorld::applyDamageToPlayer(float rawDamage) {
    float scale = getOutfitSpec(player_.currentOutfit).damageTakenScale;
    if (player_.characterCustom.hairOrHatStyle == 4) {
        scale *= 0.92f; // Full-Face Aero Moto Helmet -8% damage reduction
    }
    const float dmg = rawDamage * scale;
    const float absorbed = std::min(player_.armor, dmg * 0.65f);
    player_.armor -= absorbed;
    player_.health -= (dmg - absorbed);
}

bool GameWorld::buyOrSummonVehicle(VehicleKind kind) {
    const auto& spec = getVehicleSpec(kind);
    auto& slot = player_.garage[static_cast<std::size_t>(kind)];

    bool justPurchased = false;
    if (!slot.owned) {
        if (spec.dealershipPrice <= 0 || player_.cash < spec.dealershipPrice) {
            return false;
        }
        player_.cash -= spec.dealershipPrice;
        slot.owned = true;
        justPurchased = true;
    }

    // Remove previous summoned personal vehicle (unless currently driving it)
    if (player_.activePersonalVehicleId >= 0 && player_.activePersonalVehicleId != player_.drivingVehicleId) {
        vehicles_.erase(
            std::remove_if(vehicles_.begin(), vehicles_.end(), [&](const Vehicle& v) {
                return v.id == player_.activePersonalVehicleId && !v.isMissionTarget;
            }),
            vehicles_.end());
        player_.activePersonalVehicleId = -1;
    }

    // Choose a clean curbside road position right beside the player!
    const Vec2 pPos = playerWorldPos();
    const float nearRx = city_.roadCoord(city_.nearestRoadIndex(pPos.x));
    const float nearRz = city_.roadCoord(city_.nearestRoadIndex(pPos.z));

    Vec2 spawnPos{};
    float spawnHeading = 0.0f;
    if (std::abs(pPos.x - nearRx) < std::abs(pPos.z - nearRz)) {
        const float zOff = (pPos.z < 0.0f) ? 9.5f : -9.5f;
        spawnPos = {nearRx + CityMap::kLaneOffset, clampf(pPos.z + zOff, -320.0f, 320.0f)};
        spawnHeading = kHalfPi;
    } else {
        const float xOff = (pPos.x < 0.0f) ? 9.5f : -9.5f;
        spawnPos = {clampf(pPos.x + xOff, -320.0f, 320.0f), nearRz + CityMap::kLaneOffset};
        spawnHeading = 0.0f;
    }

    // Push away any non-player traffic car occupying the exact delivery spot
    for (auto& other : vehicles_) {
        if (!other.isPlayerDriving && !other.isMissionTarget && (other.pos - spawnPos).length() < 6.5f) {
            other.pos += forwardFromHeading(spawnHeading) * 9.0f;
        }
    }

    Vehicle delivered{};
    delivered.id = nextVehicleId_++;
    delivered.kind = kind;
    delivered.pos = spawnPos;
    delivered.posY = surfaceHeightAt(spawnPos);
    delivered.heading = spawnHeading;
    delivered.isParked = true;
    delivered.hasNpcDriver = false;
    applyPersonalTuningToVehicle(delivered, slot);

    vehicles_.push_back(delivered);
    player_.activePersonalVehicleId = delivered.id;

    pushAudio(justPurchased ? AudioEventType::ShopBuy : AudioEventType::CarHorn, 0.95f);
    if (justPurchased) {
        pushBanner("NEW VEHICLE PURCHASED!", "신차 구매 및 즉시 배달 완료!",
                   spec.nameEn, spec.nameKo, {255, 208, 64, 255}, 3.0f);
    } else {
        pushBanner("PERSONAL VEHICLE DELIVERED", "내 소유 차량이 도로변에 도착했습니다!",
                   spec.nameEn, spec.nameKo, {88, 224, 255, 255}, 2.4f);
    }
    return true;
}

bool GameWorld::buyProperty(PropertyId id) {
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= player_.ownedProperties.size() || player_.ownedProperties[idx]) {
        return false;
    }
    const auto& spec = getPropertySpec(id);
    if (player_.cash < spec.price) {
        return false;
    }

    player_.cash -= spec.price;
    player_.ownedProperties[idx] = true;
    recalculatePlayerPropertyPerks();
    player_.health = player_.maxHealth;
    player_.armor = std::max(player_.armor, player_.maxArmor * 0.5f);
    if (id == PropertyId::EclipsePenthouse) {
        player_.armor = player_.maxArmor;
    }

    pushAudio(AudioEventType::MissionComplete, 1.0f);
    char subEn[96];
    char subKo[128];
    std::snprintf(subEn, sizeof(subEn), "+$%d / 25s Income  |  %s", spec.incomePerCycle, spec.perkEn);
    std::snprintf(subKo, sizeof(subKo), "25초마다 +$%d 수익  |  %s", spec.incomePerCycle, spec.perkKo);
    pushBanner("PROPERTY ACQUIRED!", "부동산·사업장 매입 완료!",
               subEn, subKo, {108, 245, 146, 255}, 3.6f);
    return true;
}

bool GameWorld::buyOrEquipOutfit(OutfitId id) {
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= player_.ownedOutfits.size()) {
        return false;
    }
    const auto& spec = getOutfitSpec(id);
    bool purchased = false;
    if (!player_.ownedOutfits[idx]) {
        if (player_.cash < spec.price) {
            return false;
        }
        player_.cash -= spec.price;
        player_.ownedOutfits[idx] = true;
        purchased = true;
    }
    player_.currentOutfit = id;
    pushAudio(AudioEventType::ShopBuy, 0.9f);
    pushBanner(purchased ? "OUTFIT PURCHASED" : "OUTFIT EQUIPPED",
               purchased ? "신규 의상 구매 및 착용 완료" : "보유 의상 착용 완료",
               spec.nameEn, spec.nameKo, {246, 136, 232, 255}, 2.2f);
    return true;
}

bool GameWorld::executePhoneAction(int tabIndex, int optionOneBased) {
    const int maxOpt = (tabIndex == 0 || tabIndex == 3) ? 10 : 6;
    if (optionOneBased < 1 || optionOneBased > maxOpt) return false;
    const int idx = optionOneBased - 1;

    // Tab 0: AUTOS (10 Personal Garage Vehicles: Summon & Luxury Dealership Purchase)
    if (tabIndex == 0) {
        return buyOrSummonVehicle(kDealershipCatalog[static_cast<std::size_t>(idx)]);
    }
    // Tab 1: REALTY (6 City Properties & Passive Income Businesses)
    if (tabIndex == 1) {
        return buyProperty(static_cast<PropertyId>(idx));
    }
    // Tab 2: STYLE (6 Boutique Character Outfits)
    if (tabIndex == 2) {
        return buyOrEquipOutfit(static_cast<OutfitId>(idx));
    }
    // Tab 3: SUPPLY (Express Tactical Drop + SBPD Bribe Broker + New Heavy Weapons)
    if (tabIndex == 3) {
        auto buyQuickWeapon = [&](WeaponId wid, int addAmmo, int price, const char* en, const char* ko) {
            if (player_.cash < price) return false;
            player_.cash -= price;
            auto& slot = player_.weapons[static_cast<std::size_t>(wid)];
            if (!slot.owned) {
                slot.owned = true;
                slot.magAmmo = effectiveMagazineSize(wid, slot.extendedMag);
                slot.reserveAmmo = addAmmo;
            } else {
                slot.reserveAmmo += addAmmo;
            }
            player_.currentWeapon = wid;
            pushAudio(AudioEventType::ShopBuy, 1.0f);
            pushBanner(en, ko, "Express Supply Delivered", "스마트폰 긴급 보급 완료", {255, 168, 72, 255}, 2.2f);
            return true;
        };

        switch (optionOneBased) {
            case 1: {
                if (player_.cash < 250 || player_.armor >= player_.maxArmor - 0.5f) return false;
                player_.cash -= 250;
                player_.armor = player_.maxArmor;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("ARMOR DELIVERED", "긴급 방탄복 100% 보급", "Armor Maxed", "방탄복 충전 완료", {82, 196, 255, 255}, 2.0f);
                return true;
            }
            case 2: {
                if (player_.cash < 150 || player_.health >= player_.maxHealth - 0.5f) return false;
                player_.cash -= 150;
                player_.health = player_.maxHealth;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("MEDKIT DELIVERED", "긴급 의료 키트 100% 회복", "Health Maxed", "체력 완전히 회복됨", {98, 242, 122, 255}, 2.0f);
                return true;
            }
            case 3: {
                if (player_.cash < 200) return false;
                player_.cash -= 200;
                for (int i = 1; i < static_cast<int>(WeaponId::Count); ++i) {
                    const WeaponId wid = static_cast<WeaponId>(i);
                    auto& s = player_.weapons[static_cast<std::size_t>(i)];
                    if (s.owned) {
                        s.reserveAmmo += effectiveMagazineSize(wid, s.extendedMag) * 4;
                    }
                }
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("AMMO DROP RECEIVED", "전 무기 탄약 보급 완료", "+4 Magazines", "+탄창 4개 분량 보충", {255, 214, 72, 255}, 2.0f);
                return true;
            }
            case 4: return buyQuickWeapon(WeaponId::AssaultRifle, 150, 900, "CARBINE RIFLE DELIVERED", "돌격 카빈소총 보급 완료");
            case 5: return buyQuickWeapon(WeaponId::CombatMG, 240, 2800, "COMBAT MG DELIVERED", "컴뱃 중기관총 보급 완료");
            case 6: return buyQuickWeapon(WeaponId::HeavyLauncher, 12, 6500, "TACTICAL LAUNCHER UNLOCKED", "전술 폭발 런처 보급 완료");
            case 7: {
                if (player_.wantedLevel <= 0) return false;
                const int bribeCost = player_.wantedLevel * 400;
                if (player_.cash < bribeCost) return false;
                player_.cash -= bribeCost;
                clearWantedLevel(false);
                pushAudio(AudioEventType::MissionComplete, 0.95f);
                pushBanner("POLICE RECORDS WIPED", "브로커 매수로 경찰 수배 즉시 해제!",
                           "SBPD dispatch called off", "모든 경찰 수배 별이 초기화되었습니다",
                           {98, 242, 154, 255}, 2.8f);
                return true;
            }
            case 8:  return buyQuickWeapon(WeaponId::AutoShotgun, 64, 1700, "ASSAULT SHOTGUN DELIVERED", "전자동 드럼 샷건 보급 완료");
            case 9:  return buyQuickWeapon(WeaponId::SniperRifle, 32, 2500, "MARKSMAN SNIPER DELIVERED", "전술 저격소총 보급 완료");
            case 10: return buyQuickWeapon(WeaponId::Minigun, 600, 5400, "VULCAN MINIGUN DELIVERED", "6열 개틀링 미니건 보급 완료");
            default: return false;
        }
    }
    return false;
}

bool GameWorld::executeCustomStudioAction(int tabIndex, int optionOneBased) {
    if (tabIndex < 0 || tabIndex > 3 || optionOneBased < 1 || optionOneBased > 8) return false;

    const float disc = tuningDiscountScale();
    auto costOf = [&](int basePrice) {
        return std::max(20, static_cast<int>(std::round(static_cast<float>(basePrice) * disc)));
    };

    // Tab 0: CHARACTER STUDIO (8 options)
    if (tabIndex == 0) {
        auto& cc = player_.characterCustom;
        switch (optionOneBased) {
            case 1: {
                cc.hairOrHatStyle = (cc.hairOrHatStyle + 1) % 7;
                static constexpr std::array<const char*, 7> kEn = {{
                    "Classic Short", "Slick Pompadour", "Spiky Mohawk", "Street Snapback",
                    "Aero Moto Helmet (-8% DMG)", "Cyber Neon Visor", "24K VIP Gold Crown"
                }};
                static constexpr std::array<const char*, 7> kKo = {{
                    "클래식 숏컷", "슬릭 포마드", "스트리트 모호크", "스트리트 스냅백 캡",
                    "풀페이스 헬멧 (피해 -8%)", "사이버 네온 바이저", "24K VIP 골드 크라운"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("HEADGEAR UPDATED", "헤어·헤드기어 스타일 변경",
                           kEn[static_cast<std::size_t>(cc.hairOrHatStyle)],
                           kKo[static_cast<std::size_t>(cc.hairOrHatStyle)],
                           {96, 226, 255, 255}, 1.8f);
                return true;
            }
            case 2: {
                cc.hairColorIdx = (cc.hairColorIdx + 1) % 6;
                static constexpr std::array<const char*, 6> kEn = {{
                    "Jet Black", "Chestnut Brown", "Platinum Blonde", "Crimson Red", "Cyber Cyan", "Royal Gold"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "제트 블랙", "체스트넛 브라운", "플래티넘 블론드", "크림슨 레드", "사이버 시안", "로얄 골드"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("HAIR / HAT COLOR", "헤어·모자 컬러 변경",
                           kEn[static_cast<std::size_t>(cc.hairColorIdx)],
                           kKo[static_cast<std::size_t>(cc.hairColorIdx)],
                           getPlayerHairColor(player_.currentOutfit, cc.hairColorIdx), 1.8f);
                return true;
            }
            case 3: {
                cc.glassesStyle = (cc.glassesStyle + 1) % 4;
                static constexpr std::array<const char*, 4> kEn = {{
                    "None", "Dark Shades", "Gold Aviator", "Cyan Tactical Goggles"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "착용 안 함", "다크 선글라스", "골드 에비에이터", "사이버 전술 고글"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("EYEWEAR UPDATED", "아이웨어 변경",
                           kEn[static_cast<std::size_t>(cc.glassesStyle)],
                           kKo[static_cast<std::size_t>(cc.glassesStyle)],
                           {88, 224, 255, 255}, 1.8f);
                return true;
            }
            case 4: {
                cc.skinToneIdx = (cc.skinToneIdx + 1) % 5;
                static constexpr std::array<const char*, 5> kEn = {{
                    "Warm Ivory", "Natural Beige", "Golden Tan", "Olive Bronze", "Deep Espresso"
                }};
                static constexpr std::array<const char*, 5> kKo = {{
                    "웜 아이보리", "내추럴 베이지", "골든 태닝", "올리브 브론즈", "딥 에스프레소"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("SKIN TONE UPDATED", "피부 톤 변경 (1인칭 손 반영)",
                           kEn[static_cast<std::size_t>(cc.skinToneIdx)],
                           kKo[static_cast<std::size_t>(cc.skinToneIdx)],
                           getPlayerSkinColor(cc.skinToneIdx), 1.8f);
                return true;
            }
            case 5: {
                cc.customTopColorIdx = (cc.customTopColorIdx + 1) % 9;
                static constexpr std::array<const char*, 9> kEn = {{
                    "Outfit Default", "Crimson Red", "Sapphire Blue", "Stealth Black",
                    "Arctic White", "Emerald Green", "Royal Gold", "Cyber Cyan", "Neon Violet"
                }};
                static constexpr std::array<const char*, 9> kKo = {{
                    "의상 기본 컬러", "크림슨 레드", "사파이어 블루", "스텔스 블랙",
                    "아틱 화이트", "에메랄드 그린", "로얄 골드", "사이버 시안", "네온 바이올렛"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("JACKET / TOP COLOR", "상의·소매 커스텀 컬러 변경",
                           kEn[static_cast<std::size_t>(cc.customTopColorIdx)],
                           kKo[static_cast<std::size_t>(cc.customTopColorIdx)],
                           getPlayerTopColor(player_.currentOutfit, cc.customTopColorIdx), 1.8f);
                return true;
            }
            case 6: {
                cc.customBottomColorIdx = (cc.customBottomColorIdx + 1) % 7;
                static constexpr std::array<const char*, 7> kEn = {{
                    "Outfit Default", "Jet Black", "Indigo Denim", "Arctic White",
                    "Tactical Khaki", "Crimson Track", "Charcoal Grey"
                }};
                static constexpr std::array<const char*, 7> kKo = {{
                    "의상 기본 컬러", "제트 블랙", "인디고 데님", "아틱 화이트",
                    "택티컬 카키", "크림슨 트랙", "차콜 그레이"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("PANTS COLOR", "하의 커스텀 컬러 변경",
                           kEn[static_cast<std::size_t>(cc.customBottomColorIdx)],
                           kKo[static_cast<std::size_t>(cc.customBottomColorIdx)],
                           getPlayerBottomColor(player_.currentOutfit, cc.customBottomColorIdx), 1.8f);
                return true;
            }
            case 7: {
                cc.backGearStyle = (cc.backGearStyle + 1) % 4;
                static constexpr std::array<const char*, 4> kEn = {{
                    "None", "Tactical Backpack", "Heist Duffle Bag", "Cyber Neon Wing Pack"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "착용 안 함", "전술 백팩", "하이스트 더플백", "사이버 네온 윙팩"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("BACK GEAR EQUIPPED", "등 액세서리 변경",
                           kEn[static_cast<std::size_t>(cc.backGearStyle)],
                           kKo[static_cast<std::size_t>(cc.backGearStyle)],
                           {112, 232, 255, 255}, 1.8f);
                return true;
            }
            case 8: {
                const int count = static_cast<int>(OutfitId::Count);
                const int cur = static_cast<int>(player_.currentOutfit);
                for (int step = 1; step < count; ++step) {
                    const int cand = (cur + step) % count;
                    if (player_.ownedOutfits[static_cast<std::size_t>(cand)]) {
                        return buyOrEquipOutfit(static_cast<OutfitId>(cand));
                    }
                }
                const int nextIdx = (cur + 1) % count;
                return buyOrEquipOutfit(static_cast<OutfitId>(nextIdx));
            }
            default:
                return false;
        }
    }

    // Tab 1: WEAPON GUNSMITH (6 options, per-weapon)
    if (tabIndex == 1) {
        if (optionOneBased > 6) return false;
        if (player_.currentWeapon == WeaponId::Fists &&
            player_.weapons[static_cast<std::size_t>(WeaponId::Pistol)].owned) {
            player_.currentWeapon = WeaponId::Pistol;
        }
        const WeaponId wid = player_.currentWeapon;
        if (wid == WeaponId::Fists) return false;
        auto& slot = player_.weapons[static_cast<std::size_t>(wid)];
        const auto& spec = getWeaponSpec(wid);

        switch (optionOneBased) {
            case 1: {
                slot.skinIndex = (slot.skinIndex + 1) % 6;
                static constexpr std::array<const char*, 6> kEn = {{
                    "Factory Black", "Brushed Chrome", "24K Royal Gold",
                    "Crimson Carbon", "Cyber Neon", "Arctic Whiteout"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "팩토리 블랙", "브러시드 실버 크롬", "24K 로얄 골드",
                    "크림슨 카본", "사이버 네온", "아틱 화이트아웃"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.80f);
                pushBanner("WEAPON FINISH APPLIED", "총기 도색·피니시 스킨 장착",
                           kEn[static_cast<std::size_t>(slot.skinIndex)],
                           kKo[static_cast<std::size_t>(slot.skinIndex)],
                           getWeaponFinishMaterial(wid, slot.skinIndex).primary, 1.9f);
                return true;
            }
            case 2: {
                if (!weaponSupportsSuppressor(wid)) {
                    pushBanner("INCOMPATIBLE BARREL", "해당 중화기는 소음기 장착 불가",
                               spec.nameEn, spec.nameKo, {235, 92, 82, 255}, 1.9f);
                    return false;
                }
                if (!slot.suppressorOwned) {
                    const int price = costOf(250);
                    if (player_.cash < price) return false;
                    player_.cash -= price;
                    slot.suppressorOwned = true;
                    slot.suppressor = true;
                } else {
                    slot.suppressor = !slot.suppressor;
                }
                pushAudio(AudioEventType::ShopBuy, 0.88f);
                pushBanner(slot.suppressor ? "SUPPRESSOR ATTACHED" : "SUPPRESSOR REMOVED",
                           slot.suppressor ? "전술 소음기 장착 (은밀 사격·반동 감소)" : "전술 소음기 탈거",
                           spec.nameEn, spec.nameKo, {92, 238, 178, 255}, 2.0f);
                return true;
            }
            case 3: {
                if (spec.magazineSize <= 0) return false;
                if (!slot.extendedMagOwned) {
                    const int price = costOf(200);
                    if (player_.cash < price) return false;
                    player_.cash -= price;
                    slot.extendedMagOwned = true;
                    slot.extendedMag = true;
                    slot.magAmmo = effectiveMagazineSize(wid, true);
                } else {
                    slot.extendedMag = !slot.extendedMag;
                    const int newCap = effectiveMagazineSize(wid, slot.extendedMag);
                    if (slot.magAmmo > newCap) {
                        slot.reserveAmmo += (slot.magAmmo - newCap);
                        slot.magAmmo = newCap;
                    } else if (slot.extendedMag && slot.magAmmo < newCap && slot.reserveAmmo > 0) {
                        const int add = std::min(newCap - slot.magAmmo, slot.reserveAmmo);
                        slot.magAmmo += add;
                        slot.reserveAmmo -= add;
                    }
                }
                pushAudio(AudioEventType::ShopBuy, 0.88f);
                char subKo[80];
                std::snprintf(subKo, sizeof(subKo), "%s (%d발 탄창)", spec.nameKo,
                              effectiveMagazineSize(wid, slot.extendedMag));
                pushBanner(slot.extendedMag ? "EXTENDED MAG EQUIPPED" : "STANDARD MAG EQUIPPED",
                           slot.extendedMag ? "대용량 확장 탄창 장착 (+50% 장탄수)" : "기본 탄창 장착",
                           spec.nameEn, subKo, {255, 210, 76, 255}, 2.0f);
                return true;
            }
            case 4: {
                slot.laserSightColor = (slot.laserSightColor + 1) % 4;
                static constexpr std::array<const char*, 4> kEn = {{
                    "Laser OFF", "Tactical Red Laser (+30% Hip Accuracy)",
                    "Spec-Ops Green Laser (+30% Hip Accuracy)", "Cyber Cyan Laser (+30% Hip Accuracy)"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "레이저 해제", "전술 레드 레이저 (지향사격 집탄 +30%)",
                    "특수작전 그린 레이저 (지향사격 집탄 +30%)", "사이버 시안 레이저 (지향사격 집탄 +30%)"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                const ColorRGBA lCol = (slot.laserSightColor > 0)
                                           ? getLaserSightColorRGBA(slot.laserSightColor)
                                           : ColorRGBA{176, 186, 202, 255};
                pushBanner("TACTICAL LASER SIGHT", "3D 전술 레이저 사이트 설정",
                           kEn[static_cast<std::size_t>(slot.laserSightColor)],
                           kKo[static_cast<std::size_t>(slot.laserSightColor)],
                           lCol, 1.9f);
                return true;
            }
            case 5: {
                slot.tracerColorIdx = (slot.tracerColorIdx + 1) % 6;
                static constexpr std::array<const char*, 6> kEn = {{
                    "Standard Brass", "Solar Gold Tracer", "Cyber Cyan Tracer",
                    "Crimson Laser Tracer", "Plasma Purple Tracer", "Emerald Green Tracer"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "기본 예광탄", "솔라 골드 예광탄", "사이버 시안 예광탄",
                    "크림슨 레드 예광탄", "플라즈마 퍼플 예광탄", "에메랄드 그린 예광탄"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("BULLET TRACER COLOR", "예광탄 궤적 컬러 변경",
                           kEn[static_cast<std::size_t>(slot.tracerColorIdx)],
                           kKo[static_cast<std::size_t>(slot.tracerColorIdx)],
                           getCustomTracerColorRGBA(wid, slot.tracerColorIdx), 1.8f);
                return true;
            }
            case 6: {
                slot.opticColorIdx = (slot.opticColorIdx + 1) % 5;
                static constexpr std::array<const char*, 5> kEn = {{
                    "Default Reticle", "Crimson Red Optic", "Emerald Green Optic",
                    "Cyber Cyan Optic", "Solar Gold Optic"
                }};
                static constexpr std::array<const char*, 5> kKo = {{
                    "기본 도트·스코프", "레드 도트·스코프", "그린 도트·스코프",
                    "시안 도트·스코프", "골드 도트·스코프"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("OPTIC RETICLE TINT", "1인칭 도트·저격 스코프 색상 변경",
                           kEn[static_cast<std::size_t>(slot.opticColorIdx)],
                           kKo[static_cast<std::size_t>(slot.opticColorIdx)],
                           getOpticReticleColorRGBA(slot.opticColorIdx, {92, 232, 255, 255}), 1.8f);
                return true;
            }
            default:
                return false;
        }
    }

    // Tab 2: VEHICLE FULL CUSTOM STUDIO (8 options)
    if (tabIndex == 2) {
        Vehicle* car = customizationTargetVehicle();
        if (!car) {
            pushBanner("NO VEHICLE IN RANGE", "커스텀할 차량이 근처에 없습니다",
                       "Enter a car or summon via [P] Phone", "차량에 탑승하거나 [P] 스마트폰으로 호출하세요",
                       {242, 96, 86, 255}, 2.2f);
            return false;
        }

        switch (optionOneBased) {
            case 1: {
                car->paintPresetIndex = (car->paintPresetIndex + 1) % static_cast<int>(kCustomPaints.size());
                const auto& preset = kCustomPaints[static_cast<std::size_t>(car->paintPresetIndex)];
                car->paintColor = preset.paint;
                if (car->stripeStyle == 0) {
                    car->stripeColor = preset.stripe;
                }
                car->mattePaint = preset.matte;
                car->chromePaint = preset.chrome;
                syncPersonalTuningFromVehicle(*car);

                bool clearedWanted = false;
                if (player_.wantedLevel > 0 && player_.cash >= costOf(100)) {
                    bool copClose = false;
                    const Vec2 gPos = playerWorldPos();
                    for (const auto& n : npcs_) {
                        if (n.alive && n.role == NpcRole::Police && (n.pos - gPos).lengthSq() < 16.0f * 16.0f) {
                            copClose = true;
                            break;
                        }
                    }
                    if (!copClose) {
                        player_.cash -= costOf(100);
                        clearWantedLevel(false);
                        clearedWanted = true;
                    }
                }
                pushAudio(AudioEventType::ShopBuy, 0.85f);
                pushBanner(clearedWanted ? "RESPRAYED & WANTED CLEARED" : "STUDIO PAINT & WRAP",
                           clearedWanted ? "도색 위장 성공! 경찰 수배 해제" : "차체 도색·크롬 랩핑 변경",
                           preset.nameEn, preset.nameKo, car->paintColor, 2.0f);
                return true;
            }
            case 2: {
                car->stripeStyle = (car->stripeStyle + 1) % 5;
                if (car->stripeStyle > 0) {
                    car->stripeColor = getVehicleStripeColorRGBA(car->stripeStyle, car->stripeColor);
                }
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 5> kEn = {{
                    "No Stripes", "White Dual GT Stripes", "Royal Gold Stripes",
                    "Cyber Cyan Stripes", "Stealth Carbon Stripes"
                }};
                static constexpr std::array<const char*, 5> kKo = {{
                    "스트라이프 없음", "화이트 듀얼 GT 데칼", "로얄 골드 스트라이프",
                    "사이버 시안 스트라이프", "스텔스 카본 블랙 데칼"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("RACING LIVERY", "레이싱 스트라이프 데칼 변경",
                           kEn[static_cast<std::size_t>(car->stripeStyle)],
                           kKo[static_cast<std::size_t>(car->stripeStyle)],
                           (car->stripeStyle > 0) ? car->stripeColor : ColorRGBA{190, 198, 212, 255}, 1.9f);
                return true;
            }
            case 3: {
                car->rimStyle = (car->rimStyle + 1) % 5;
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 5> kEn = {{
                    "Silver Alloy", "Champagne Gold", "Gloss Black", "Cyber Cyan Forged", "Crimson Track"
                }};
                static constexpr std::array<const char*, 5> kKo = {{
                    "실버 알로이 휠", "샴페인 골드 단조 휠", "글로스 블랙 휠", "사이버 시안 단조 휠", "크림슨 트랙 휠"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("FORGED WHEELS", "커스텀 단조 휠 변경",
                           kEn[static_cast<std::size_t>(car->rimStyle)],
                           kKo[static_cast<std::size_t>(car->rimStyle)],
                           {238, 212, 112, 255}, 1.9f);
                return true;
            }
            case 4: {
                car->spoilerStyle = (car->spoilerStyle + 1) % 3;
                car->customSpoiler = (car->spoilerStyle > 0);
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 3> kEn = {{
                    "Stock Deck (No Wing)", "Carbon GT Wing (+2% Grip)", "Swan-Neck Time-Attack Wing (+6% Grip)"
                }};
                static constexpr std::array<const char*, 3> kKo = {{
                    "순정 (스포일러 탈거)", "카본 GT 스포일러 (그립 +2%)", "스완넥 타임어택 대형 윙 (그립 +6%)"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.80f);
                pushBanner("REAR AERO WING", "리어 에어로 윙 튜닝",
                           kEn[static_cast<std::size_t>(car->spoilerStyle)],
                           kKo[static_cast<std::size_t>(car->spoilerStyle)],
                           {255, 204, 72, 255}, 1.9f);
                return true;
            }
            case 5: {
                car->stanceStyle = (car->stanceStyle + 1) % 4;
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 4> kEn = {{
                    "Stock Ride Height", "Street Lowered (-45mm)",
                    "Slammed Negative Camber (-82mm)", "Off-Road Rally Lift (+95mm)"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "순정 서스펜션 차고", "스트리트 로우다운 (-45mm)",
                    "슬램드 마이너스 캠버 (-82mm)", "오프로드 리프트업 (+95mm)"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.80f);
                pushBanner("SUSPENSION & CAMBER", "차고·캠버 서스펜션 세팅",
                           kEn[static_cast<std::size_t>(car->stanceStyle)],
                           kKo[static_cast<std::size_t>(car->stanceStyle)],
                           {96, 228, 255, 255}, 1.9f);
                return true;
            }
            case 6: {
                car->headlightColorIdx = (car->headlightColorIdx + 1) % 5;
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 5> kEn = {{
                    "Xenon Pure White", "Cyber Cyan LED", "GT Endurance Yellow",
                    "UV Neon Purple", "Demon-Eye Crimson"
                }};
                static constexpr std::array<const char*, 5> kKo = {{
                    "제논 화이트 라이트", "사이버 시안 헤드라이트", "GT 옐로우 레이싱 라이트",
                    "UV 퍼플 네온 라이트", "데몬아이 레드 헤드라이트"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("HEADLIGHT BEAM TINT", "헤드라이트·야간 조사광 컬러 변경",
                           kEn[static_cast<std::size_t>(car->headlightColorIdx)],
                           kKo[static_cast<std::size_t>(car->headlightColorIdx)],
                           getVehicleHeadlightColorRGBA(car->headlightColorIdx, true), 1.9f);
                return true;
            }
            case 7: {
                car->neonPreset = (car->neonPreset + 1) % 6;
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 6> kEn = {{
                    "Underglow OFF", "Cyber Cyan Neon", "Ultraviolet Neon",
                    "Crimson Red Neon", "Emerald Green Neon", "Solar Gold Neon"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "네온 언더글로우 해제", "사이버 시안 네온", "울트라 퍼플 네온",
                    "크림슨 레드 네온", "에메랄드 그린 네온", "솔라 골드 네온"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                const ColorRGBA nCol = (car->neonPreset > 0)
                                           ? getNeonPresetColor(car->neonPreset)
                                           : ColorRGBA{185, 195, 210, 255};
                pushBanner("LED UNDERGLOW", "차량 하부 네온 언더글로우 설정",
                           kEn[static_cast<std::size_t>(car->neonPreset)],
                           kKo[static_cast<std::size_t>(car->neonPreset)],
                           nCol, 1.9f);
                return true;
            }
            case 8: {
                car->driftSmokeColorIdx = (car->driftSmokeColorIdx + 1) % 6;
                syncPersonalTuningFromVehicle(*car);
                static constexpr std::array<const char*, 6> kEn = {{
                    "Classic White Smoke", "Cyber Cyan Tire Smoke", "Crimson Red Tire Smoke",
                    "Royal Gold Tire Smoke", "Violet Tire Smoke", "Emerald Tire Smoke"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "클래식 화이트 타이어 연기", "사이버 시안 드리프트 연기", "크림슨 레드 드리프트 연기",
                    "로얄 골드 드리프트 연기", "바이올렛 드리프트 연기", "에메랄드 드리프트 연기"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.78f);
                pushBanner("DRIFT TIRE SMOKE", "드리프트 타이어 스모크 컬러 변경",
                           kEn[static_cast<std::size_t>(car->driftSmokeColorIdx)],
                           kKo[static_cast<std::size_t>(car->driftSmokeColorIdx)],
                           getDriftSmokeColorRGBA(car->driftSmokeColorIdx), 1.9f);
                return true;
            }
            default:
                return false;
        }
    }

    // Tab 3: CROSSHAIR & HUD STUDIO (6 options)
    if (tabIndex == 3) {
        if (optionOneBased > 6) return false;
        auto& hc = player_.hudCustom;
        switch (optionOneBased) {
            case 1: {
                hc.crosshairStyle = (hc.crosshairStyle + 1) % 4;
                static constexpr std::array<const char*, 4> kEn = {{
                    "Dynamic Cross + Dot", "Pinpoint Center Dot", "Tactical Circle + Dot", "Spec-Ops Chevron"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "다이나믹 십자선 + 도트", "핀포인트 정밀 도트", "택티컬 서클 + 도트", "특수전 쉐브론 조준선"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.72f);
                pushBanner("CROSSHAIR STYLE", "조준선 모양 변경",
                           kEn[static_cast<std::size_t>(hc.crosshairStyle)],
                           kKo[static_cast<std::size_t>(hc.crosshairStyle)],
                           getCrosshairColorRGBA(hc.crosshairColorIdx), 1.7f);
                return true;
            }
            case 2: {
                hc.crosshairColorIdx = (hc.crosshairColorIdx + 1) % 6;
                static constexpr std::array<const char*, 6> kEn = {{
                    "Pure White", "Neon Green", "Cyber Cyan", "Solar Gold", "Hot Magenta", "Crimson Red"
                }};
                static constexpr std::array<const char*, 6> kKo = {{
                    "퓨어 화이트", "네온 그린", "사이버 시안", "솔라 골드", "핫 마젠타", "크림슨 레드"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.72f);
                pushBanner("CROSSHAIR COLOR", "조준선 컬러 변경",
                           kEn[static_cast<std::size_t>(hc.crosshairColorIdx)],
                           kKo[static_cast<std::size_t>(hc.crosshairColorIdx)],
                           getCrosshairColorRGBA(hc.crosshairColorIdx), 1.7f);
                return true;
            }
            case 3: {
                hc.crosshairScaleIdx = (hc.crosshairScaleIdx + 1) % 3;
                static constexpr std::array<const char*, 3> kEn = {{"Compact (80%)", "Standard (100%)", "Large (130%)"}};
                static constexpr std::array<const char*, 3> kKo = {{"소형 (80%)", "표준 (100%)", "대형 (130%)"}};
                pushAudio(AudioEventType::ShopBuy, 0.72f);
                pushBanner("CROSSHAIR SCALE", "조준선 크기 배율 변경",
                           kEn[static_cast<std::size_t>(hc.crosshairScaleIdx)],
                           kKo[static_cast<std::size_t>(hc.crosshairScaleIdx)],
                           getCrosshairColorRGBA(hc.crosshairColorIdx), 1.7f);
                return true;
            }
            case 4: {
                hc.crosshairOutline = !hc.crosshairOutline;
                pushAudio(AudioEventType::ShopBuy, 0.72f);
                pushBanner("CROSSHAIR OUTLINE", "조준선 외곽 그림자 테두리",
                           hc.crosshairOutline ? "Outline ON" : "Outline OFF",
                           hc.crosshairOutline ? "외곽 테두리 켜짐" : "외곽 테두리 꺼짐",
                           {192, 224, 255, 255}, 1.7f);
                return true;
            }
            case 5: {
                hc.useMph = !hc.useMph;
                pushAudio(AudioEventType::ShopBuy, 0.72f);
                pushBanner("SPEEDOMETER UNIT", "속도계 단위 변경",
                           hc.useMph ? "MPH (Miles / Hour)" : "KM/H (Kilometers / Hour)",
                           hc.useMph ? "MPH (마일 속도계)" : "KM/H (킬로미터 속도계)",
                           getHudThemeColorRGBA(hc.hudThemeIdx), 1.7f);
                return true;
            }
            case 6: {
                hc.hudThemeIdx = (hc.hudThemeIdx + 1) % 4;
                static constexpr std::array<const char*, 4> kEn = {{
                    "Cyber Cyan", "Vice Neon Pink", "VIP Royal Gold", "Emerald Matrix"
                }};
                static constexpr std::array<const char*, 4> kKo = {{
                    "사이버 시안 테마", "바이스 핑크 테마", "VIP 로얄 골드 테마", "에메랄드 그린 테마"
                }};
                pushAudio(AudioEventType::ShopBuy, 0.75f);
                pushBanner("HUD ACCENT THEME", "인터페이스 HUD 테마 컬러 변경",
                           kEn[static_cast<std::size_t>(hc.hudThemeIdx)],
                           kKo[static_cast<std::size_t>(hc.hudThemeIdx)],
                           getHudThemeColorRGBA(hc.hudThemeIdx), 1.8f);
                return true;
            }
            default:
                return false;
        }
    }
    return false;
}

bool GameWorld::purchaseShopItem(int optionOneBased) {
    if (isPlayerInWeaponShopZone()) {
        auto buyWeapon = [&](WeaponId wid, int addAmmo, int price, const char* labelEn, const char* labelKo) {
            if (player_.cash < price) return false;
            player_.cash -= price;
            auto& slot = player_.weapons[static_cast<std::size_t>(wid)];
            if (!slot.owned) {
                slot.owned = true;
                slot.magAmmo = effectiveMagazineSize(wid, slot.extendedMag);
                slot.reserveAmmo = addAmmo;
            } else {
                slot.reserveAmmo += addAmmo;
            }
            player_.currentWeapon = wid;
            pushAudio(AudioEventType::ShopBuy, 1.0f);
            pushBanner(labelEn, labelKo, "Purchased at Ammu-Tech", "무기 상점 구매 완료!", {255, 138, 72, 255}, 2.2f);
            return true;
        };

        switch (optionOneBased) {
            case 1: return buyWeapon(WeaponId::SMG, 120, 350, "TACTICAL SMG ACQUIRED", "전술 기관단총 구매 완료");
            case 2: return buyWeapon(WeaponId::Shotgun, 32, 500, "PUMP SHOTGUN ACQUIRED", "펌프 샷건 구매 완료");
            case 3: return buyWeapon(WeaponId::AssaultRifle, 150, 850, "CARBINE RIFLE ACQUIRED", "돌격 카빈소총 구매 완료");
            case 4: {
                if (player_.cash < 150) return false;
                player_.cash -= 150;
                for (int i = 1; i < static_cast<int>(WeaponId::Count); ++i) {
                    const WeaponId wid = static_cast<WeaponId>(i);
                    auto& s = player_.weapons[static_cast<std::size_t>(i)];
                    if (s.owned) {
                        s.reserveAmmo += effectiveMagazineSize(wid, s.extendedMag) * 4;
                    }
                }
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("AMMO REFILLED", "보유 무기 탄약 보충 완료", "+4 Magazines", "+탄창 4개 분량 추가", {255, 214, 72, 255}, 2.0f);
                return true;
            }
            case 5: {
                if (player_.cash < 200 || player_.armor >= player_.maxArmor - 0.5f) return false;
                player_.cash -= 200;
                player_.armor = player_.maxArmor;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("HEAVY BODY ARMOR", "방탄복 100% 장착", "Armor Restored", "방탄복 내구도 최대", {82, 196, 255, 255}, 2.0f);
                return true;
            }
            case 6: {
                if (player_.cash < 100 || player_.health >= player_.maxHealth - 0.5f) return false;
                player_.cash -= 100;
                player_.health = player_.maxHealth;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("HEALTH RESTORED", "체력 100% 회복", "Medkit Used", "응급 치료 완료", {98, 242, 122, 255}, 2.0f);
                return true;
            }
            case 7:  return buyWeapon(WeaponId::CombatMG, 240, 2800, "COMBAT MG ACQUIRED", "컴뱃 중기관총 구매 완료");
            case 8:  return buyWeapon(WeaponId::HeavyLauncher, 12, 6500, "TACTICAL LAUNCHER ACQUIRED", "전술 폭발 런처 구매 완료");
            case 9:  return buyWeapon(WeaponId::HeavyRevolver, 36, 650, ".44 MAGNUM ACQUIRED", "44 매그넘 리볼버 구매 완료");
            case 10: return buyWeapon(WeaponId::AutoShotgun, 64, 1600, "ASSAULT SHOTGUN ACQUIRED", "전자동 드럼 샷건 구매 완료");
            case 11: return buyWeapon(WeaponId::SniperRifle, 32, 2400, "MARKSMAN SNIPER ACQUIRED", "전술 저격소총 구매 완료");
            case 12: return buyWeapon(WeaponId::Minigun, 600, 5200, "VULCAN MINIGUN ACQUIRED", "6열 개틀링 미니건 구매 완료");
            default:
                return false;
        }
    }

    if (isPlayerInGarageZone()) {
        Vehicle* car = findVehicleMutable(player_.drivingVehicleId);
        if (!car) {
            const auto* garagePoi = city_.findPoi(PoiKind::TuneGarage);
            for (auto& v : vehicles_) {
                if (!v.destroyed && garagePoi && (v.pos - garagePoi->pos).length() <= garagePoi->radius + 3.0f) {
                    car = &v;
                    break;
                }
            }
        }
        if (!car) return false;

        const float disc = tuningDiscountScale();
        auto costOf = [&](int basePrice) {
            return std::max(20, static_cast<int>(std::round(static_cast<float>(basePrice) * disc)));
        };
        auto canDisguiseWanted = [&]() {
            if (player_.wantedLevel <= 0) return false;
            const Vec2 gPos = playerWorldPos();
            for (const auto& n : npcs_) {
                if (n.alive && n.role == NpcRole::Police && (n.pos - gPos).lengthSq() < 16.0f * 16.0f) {
                    return false;
                }
            }
            return true;
        };

        switch (optionOneBased) {
            case 1: {
                const int price = costOf(120);
                const bool hasDent = (car->frontDamage + car->rearDamage + car->leftDamage + car->rightDamage) > 0.02f;
                if (player_.cash < price || (car->health >= car->maxHealth - 0.5f && !hasDent && player_.wantedLevel <= 0)) return false;
                player_.cash -= price;
                car->health = car->maxHealth;
                car->frontDamage = 0.0f;
                car->rearDamage = 0.0f;
                car->leftDamage = 0.0f;
                car->rightDamage = 0.0f;
                const bool clearedWanted = canDisguiseWanted();
                if (clearedWanted) clearWantedLevel(false);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner(clearedWanted ? "REPAIRED & WANTED CLEARED" : "VEHICLE REPAIRED",
                           clearedWanted ? "차량 판금·수리 및 경찰 수배 위장 해제!" : "차량 차체 판금 & 완벽 수리 완료",
                           "Chassis & Bodywork 100%", "내구도 및 차체 외형 100% 복구", {72, 232, 255, 255}, 2.4f);
                return true;
            }
            case 2: {
                const int price = costOf(450);
                if (player_.cash < price || car->engineUpgradeLevel >= 3) return false;
                player_.cash -= price;
                car->engineUpgradeLevel += 1;
                car->health = car->maxHealth;
                syncPersonalTuningFromVehicle(*car);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                char subEn[64];
                char subKo[64];
                std::snprintf(subEn, sizeof(subEn), "Turbo Stage %d Installed!", car->engineUpgradeLevel);
                std::snprintf(subKo, sizeof(subKo), "터보 엔진 스테이지 %d 장착!", car->engineUpgradeLevel);
                pushBanner("ENGINE UPGRADED", "터보 엔진 업그레이드 완료!", subEn, subKo, {72, 232, 255, 255}, 2.4f);
                return true;
            }
            case 3: {
                const int price = costOf(600);
                if (player_.cash < price || car->armorUpgradeLevel >= 3) return false;
                player_.cash -= price;
                car->armorUpgradeLevel += 1;
                const auto& spec = getVehicleSpec(car->kind);
                const float showroomBonus = player_.ownedProperties[static_cast<std::size_t>(PropertyId::LuxuryShowroom)] ? 1.30f : 1.0f;
                car->maxHealth = spec.maxHealth * (1.0f + 0.35f * static_cast<float>(car->armorUpgradeLevel)) * showroomBonus;
                car->health = car->maxHealth;
                syncPersonalTuningFromVehicle(*car);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                char subKo[64];
                std::snprintf(subKo, sizeof(subKo), "방탄 차체·타이어 레벨 %d 강화!", car->armorUpgradeLevel);
                pushBanner("ARMOR PLATING INSTALLED", "차체 방탄 장갑 업그레이드!", "Reinforced Chassis", subKo, {108, 242, 168, 255}, 2.4f);
                return true;
            }
            case 4: {
                const int price = costOf(350);
                if (player_.cash < price) return false;
                player_.cash -= price;
                car->neonPreset = (car->neonPreset + 1) % 6;
                syncPersonalTuningFromVehicle(*car);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                const ColorRGBA ncol = (car->neonPreset > 0) ? getNeonPresetColor(car->neonPreset) : ColorRGBA{180, 190, 205, 255};
                pushBanner("NEON UNDERGLOW KIT",
                           (car->neonPreset > 0) ? "LED 네온 언더글로우 장착!" : "네온 언더글로우 해제",
                           "Chassis LED Kit", "하부 네온 조명 색상 변경 완료", ncol, 2.0f);
                return true;
            }
            case 5: {
                const int price = costOf(300);
                if (player_.cash < price) return false;
                player_.cash -= price;
                car->spoilerStyle = (car->spoilerStyle + 1) % 3;
                car->customSpoiler = (car->spoilerStyle > 0);
                syncPersonalTuningFromVehicle(*car);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("GT AERO SPOILER",
                           (car->spoilerStyle == 2) ? "스완넥 타임어택 대형 윙 장착!"
                                                    : (car->customSpoiler ? "카본 GT 에어로 스포일러 장착!" : "리어 스포일러 탈거 완료"),
                           "Custom Rear Wing", "리어 에어로 파츠 튜닝 완료", {255, 202, 72, 255}, 2.0f);
                return true;
            }
            case 6: {
                const int price = costOf(250);
                if (player_.cash < price) return false;
                player_.cash -= price;
                car->rimStyle = (car->rimStyle + 1) % 5;
                syncPersonalTuningFromVehicle(*car);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                static constexpr std::array<const char*, 5> kRimKo = {{
                    "실버 알로이 휠 장착", "샴페인 골드 단조 휠 장착",
                    "글로스 블랙 스포츠 휠 장착", "사이버 시안 단조 휠 장착", "크림슨 트랙 단조 휠 장착"
                }};
                pushBanner("FORGED ALLOY RIMS", "커스텀 단조 휠 교체 완료", "Forged Wheel Set",
                           kRimKo[static_cast<std::size_t>(car->rimStyle)], {235, 212, 118, 255}, 2.0f);
                return true;
            }
            case 7: {
                const int price = costOf(150);
                if (player_.cash < price) return false;
                player_.cash -= price;
                car->paintPresetIndex = (car->paintPresetIndex + 1) % static_cast<int>(kCustomPaints.size());
                const auto& preset = kCustomPaints[static_cast<std::size_t>(car->paintPresetIndex)];
                car->paintColor = preset.paint;
                if (car->stripeStyle == 0) {
                    car->stripeColor = preset.stripe;
                }
                car->mattePaint = preset.matte;
                car->chromePaint = preset.chrome;
                car->health = car->maxHealth;
                syncPersonalTuningFromVehicle(*car);
                const bool clearedWanted = canDisguiseWanted();
                if (clearedWanted) clearWantedLevel(false);
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner(clearedWanted ? "RESPRAYED & WANTED CLEARED" : "CUSTOM RESPRAY",
                           clearedWanted ? "커스텀 도색 및 경찰 수배 위장 해제!" : "프리미엄 커스텀 도색 완료",
                           preset.nameEn, preset.nameKo, car->paintColor, 2.3f);
                return true;
            }
            case 8: {
                const int price = costOf(80);
                if (player_.cash < price || car->nitroCharge >= 0.99f) return false;
                player_.cash -= price;
                car->nitroCharge = 1.0f;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("NITROUS REFILLED", "니트로 탱크 100% 충전", "Hold [Shift] to Boost", "주행 중 [Shift] 부스트", {88, 195, 255, 255}, 2.0f);
                return true;
            }
            default:
                return false;
        }
    }

    if (isPlayerInDealershipZone()) {
        if (optionOneBased >= 1 && optionOneBased <= static_cast<int>(kDealershipCatalog.size())) {
            return buyOrSummonVehicle(kDealershipCatalog[static_cast<std::size_t>(optionOneBased - 1)]);
        }
        if (optionOneBased == static_cast<int>(kDealershipCatalog.size()) + 1) {
            return buyProperty(PropertyId::LuxuryShowroom);
        }
        return false;
    }

    if (isPlayerInBoutiqueZone()) {
        if (optionOneBased >= 1 && optionOneBased <= 6) {
            return buyOrEquipOutfit(static_cast<OutfitId>(optionOneBased - 1));
        }
        return false;
    }

    if (isPlayerInGasStationZone()) {
        if (optionOneBased == 1) {
            Vehicle* car = findVehicleMutable(player_.drivingVehicleId);
            if (!car || player_.cash < 60) return false;
            player_.cash -= 60;
            car->health = car->maxHealth;
            car->nitroCharge = 1.0f;
            car->frontDamage = 0.0f;
            car->rearDamage = 0.0f;
            car->leftDamage = 0.0f;
            car->rightDamage = 0.0f;
            pushAudio(AudioEventType::ShopBuy, 1.0f);
            pushBanner("FULL TANK & PIT SERVICE", "고급유 주유·니트로 충전 및 급속 판금정비 완료!",
                       "Vehicle 100% Ready", "차량 내구도·차체 및 니트로 100% 복구", {255, 118, 72, 255}, 2.2f);
            return true;
        }
        if (optionOneBased == 2) {
            return buyProperty(PropertyId::GlobeOilGas);
        }
        return false;
    }

    if (isPlayerInNightclubZone()) {
        if (optionOneBased == 1) {
            return buyProperty(PropertyId::PulseNightclub);
        }
        if (optionOneBased == 2) {
            return buyProperty(PropertyId::EclipsePenthouse);
        }
        return false;
    }

    return false;
}

bool GameWorld::startMissionFromPoi(PoiKind poiKind) {
    if (mission_.kind != ActiveMissionKind::None) return false;

    const int tierBonus = mission_.completedCount * 250;
    const float vipMult = player_.ownedProperties[static_cast<std::size_t>(PropertyId::PulseNightclub)] ? 1.25f : 1.0f;

    if (poiKind == PoiKind::MissionDelivery) {
        Vehicle exo{};
        exo.id = nextVehicleId_++;
        exo.kind = VehicleKind::Hypercar;
        exo.pos = {756.0f + 7.2f, 1092.0f};
        exo.posY = surfaceHeightAt(exo.pos);
        exo.heading = kHalfPi;
        exo.maxHealth = 230.0f;
        exo.health = 230.0f;
        exo.paintColor = {216, 168, 38, 255};
        exo.customSpoiler = true;
        exo.rimStyle = 1;
        exo.isParked = true;
        exo.isMissionTarget = true;
        exo.engineUpgradeLevel = 1;
        vehicles_.push_back(exo);

        for (int i = 0; i < 2; ++i) {
            NpcCharacter guard{};
            guard.id = nextNpcId_++;
            guard.role = NpcRole::Gangster;
            guard.behavior = NpcBehavior::Stroll;
            guard.pos = exo.pos + Vec2{3.2f, static_cast<float>(i * 4 - 2)};
            guard.homeAnchor = guard.pos;
            guard.walkSpeed = 1.45f;
            guard.health = 80.0f;
            guard.maxHealth = 80.0f;
            guard.outfitTop = {192, 42, 58, 255};
            npcs_.push_back(guard);
        }

        mission_.kind = ActiveMissionKind::ExoticDelivery;
        mission_.stage = 0;
        mission_.timeRemaining = 115.0f;
        mission_.rewardCash = static_cast<int>(std::round(static_cast<float>(1800 + tierBonus) * vipMult));
        mission_.targetVehicleId = exo.id;
        mission_.currentObjectivePos = exo.pos;
        mission_.objectiveRadius = 6.5f;
        mission_.objectiveEn = "Steal the golden VIP Zentorno X in Gangnam!";
        mission_.objectiveKo = "강남 구역의 황금색 VIP 젠토르노 X 하이퍼카를 탈취하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("VIP EXOTIC DELIVERY", "미션 시작: 하이퍼카 탈취 & 배달",
                   "Steal the marked Golden Zentorno X!", "미니맵의 노란 마커 하이퍼카를 탈취하세요!",
                   {255, 214, 52, 255}, 3.5f);
        return true;
    }

    if (poiKind == PoiKind::MissionRace) {
        if (player_.drivingVehicleId < 0) {
            Vehicle racer{};
            racer.id = nextVehicleId_++;
            racer.kind = VehicleKind::SportsCoupe;
            racer.pos = {588.0f, 1008.0f + CityMap::kLaneOffset};
            racer.posY = surfaceHeightAt(racer.pos);
            racer.heading = 0.0f;
            racer.maxHealth = 190.0f;
            racer.health = 190.0f;
            racer.paintColor = {44, 152, 92, 255};
            racer.customSpoiler = true;
            racer.isPlayerDriving = true;
            vehicles_.push_back(racer);
            player_.drivingVehicleId = racer.id;
        }

        raceCheckpoints_ = {
            {672.0f, 1008.0f},
            {756.0f, 1008.0f},
            {756.0f, 1092.0f},
            {756.0f, 1176.0f},
            {672.0f, 1176.0f},
            {588.0f, 1176.0f},
            {588.0f, 1008.0f}
        };

        mission_.kind = ActiveMissionKind::StreetSprint;
        mission_.stage = 0;
        mission_.timeRemaining = 58.0f;
        mission_.rewardCash = static_cast<int>(std::round(static_cast<float>(2200 + tierBonus) * vipMult));
        mission_.checkpointsCleared = 0;
        mission_.totalCheckpoints = static_cast<int>(raceCheckpoints_.size());
        mission_.currentObjectivePos = raceCheckpoints_.front();
        mission_.objectiveRadius = 9.5f;
        mission_.objectiveEn = "Hit Checkpoint 1 / 7 before time runs out!";
        mission_.objectiveKo = "제한 시간 내에 체크포인트 1 / 7 지점을 통과하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("GANGNAM STREET SPRINT", "미션 시작: 강남 스트리트 레이스",
                   "Hit all 7 checkpoints! +8s per gate", "7개 체크포인트를 질주하세요! (통과 시 +8초)",
                   {112, 255, 118, 255}, 3.5f);
        return true;
    }

    if (poiKind == PoiKind::MissionGangRaid) {
        const Vec2 raidCenter{756.0f, 1176.0f};
        const std::array<Vec2, 5> offsets = {{
            {-7.0f, -5.0f}, {7.0f, -5.0f}, {-8.0f, 6.0f}, {8.0f, 6.0f}, {0.0f, -9.0f}
        }};
        for (const Vec2& off : offsets) {
            Vec2 spawnP = raidCenter + off;
            city_.resolveCircleWorld(spawnP, 0.6f);
            NpcCharacter g{};
            g.id = nextNpcId_++;
            g.role = NpcRole::Gangster;
            g.behavior = NpcBehavior::CombatEngage;
            g.pos = spawnP;
            g.homeAnchor = spawnP;
            g.walkSpeed = 1.55f;
            g.health = 90.0f;
            g.maxHealth = 90.0f;
            g.outfitTop = {215, 58, 38, 255};
            g.outfitBottom = {25, 28, 34, 255};
            g.missionTarget = true;
            npcs_.push_back(g);
        }

        mission_.kind = ActiveMissionKind::GangRaid;
        mission_.stage = 0;
        mission_.timeRemaining = 140.0f;
        mission_.rewardCash = static_cast<int>(std::round(static_cast<float>(3000 + tierBonus) * vipMult));
        mission_.enemiesRemaining = static_cast<int>(offsets.size());
        mission_.currentObjectivePos = raidCenter;
        mission_.objectiveRadius = 12.0f;
        mission_.objectiveEn = "Eliminate the 5 Syndicate enforcers!";
        mission_.objectiveKo = "강남 조직 거점의 신디케이트 조직원 5명을 소탕하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("SEOUL SYNDICATE RAID", "미션 시작: 조직 거점 소탕",
                   "Wipe out the armed crew & secure the cash briefcase!",
                   "무장 조직원을 제압하고 현금 가방을 회수하세요!",
                   {255, 132, 48, 255}, 3.5f);
        return true;
    }

    return false;
}

void GameWorld::update(float dt, const PlayerInput& input) {
    dt = std::clamp(dt, 0.0f, 0.05f);
    elapsedTime_ += dt;
    const float targetWet = rainWeather_ ? 0.92f : 0.0f;
    wetRoadFactor_ += (targetWet - wetRoadFactor_) * std::min(1.0f, dt * (rainWeather_ ? 0.45f : 0.08f));

    // 1 game hour = 25 real seconds (~10 min full 24h cycle)
    advanceTimeOfDayHours(dt / 25.0f);
    trafficSignalTimer_ = std::fmod(trafficSignalTimer_ + dt, 24.0f);

    if (banner_.timer > 0.0f) {
        banner_.timer = std::max(0.0f, banner_.timer - dt);
    }
    if (player_.hitMarkerTimer > 0.0f) {
        player_.hitMarkerTimer = std::max(0.0f, player_.hitMarkerTimer - dt);
    }
    if (panicScreamCooldown_ > 0.0f) {
        panicScreamCooldown_ = std::max(0.0f, panicScreamCooldown_ - dt);
    }

    // Real estate & business passive income deposit every 25 seconds!
    const int cycleIncome = passiveIncomePerCycle();
    if (cycleIncome > 0) {
        player_.propertyPayoutTimer += dt;
        if (player_.propertyPayoutTimer >= 25.0f) {
            player_.propertyPayoutTimer -= 25.0f;
            player_.cash += cycleIncome;
            pushAudio(AudioEventType::CashPickup, 0.75f);
            if (banner_.timer <= 0.1f) {
                char subEn[64];
                char subKo[80];
                std::snprintf(subEn, sizeof(subEn), "+$%d deposited from %d properties", cycleIncome, ownedPropertyCount());
                std::snprintf(subKo, sizeof(subKo), "보유 부동산 %d곳에서 +$%d 수익 입금!", ownedPropertyCount(), cycleIncome);
                pushBanner("BUSINESS PAYOUT", "부동산·사업장 수익 입금", subEn, subKo, {98, 242, 138, 255}, 2.3f);
            }
        }
    }

    // Handle player wasted / busted respawn state
    if (player_.wasted) {
        player_.crosshairOnTarget = false;
        player_.wastedTimer -= dt;
        if (player_.wastedTimer <= 0.0f) {
            const bool wasBusted = player_.lastDeathWasBusted;
            player_.wasted = false;
            player_.lastDeathWasBusted = false;
            player_.drivingVehicleId = -1;
            player_.pos = wasBusted ? city_.policeRespawnPos() : city_.spawnPos();
            city_.updateStreaming(player_.pos, true);
            player_.posY = surfaceHeightAt(player_.pos);
            player_.verticalVel = 0.0f;
            player_.onGround = true;
            player_.coyoteTimer = 0.0f;
            player_.velocity = {0.0f, 0.0f};
            player_.health = player_.maxHealth;
            clearWantedLevel(false);

            const bool vipPerk = player_.ownedProperties[static_cast<std::size_t>(PropertyId::EclipsePenthouse)];
            if (wasBusted) {
                if (!vipPerk) {
                    player_.cash = std::max(0, player_.cash - 150);
                }
                pushBanner("RELEASED ON BAIL",
                           vipPerk ? "VIP 변호인단 보석 석방 ($0)" : "경찰서 보석 석방 완료 (-$150)",
                           "Wanted record cleared", "수배 기록이 초기화되었습니다",
                           {96, 198, 255, 255}, 2.8f);
            } else {
                if (!vipPerk) {
                    player_.cash = std::max(0, player_.cash - 100);
                }
                pushBanner("DISCHARGED FROM MEDICAL",
                           vipPerk ? "VIP 무료 병원 치료 완료 ($0)" : "병원 치료 완료 (-$100)",
                           "Back on the streets", "다시 거리로 복귀했습니다",
                           {112, 238, 148, 255}, 2.8f);
            }
        }
        updateVehicles(dt);
        updateNpcs(dt);
        updateEffects(dt);
        return;
    }

    // Stream real-scale 30km x 25km Seoul chunks smoothly around the player
    const Vec2 pPos = playerWorldPos();
    city_.updateStreaming(pPos, false);

    // Clear custom Seoul Map GPS waypoint upon arrival
    if (player_.hasWaypoint && (pPos - player_.waypointPos).length() < 26.0f) {
        const std::string lbl = player_.waypointLabelKo.empty()
            ? std::string(CityMap::districtNameKo(city_.districtAtWorld(pPos)))
            : player_.waypointLabelKo;
        clearWaypoint();
        pushBanner("DESTINATION REACHED", "목적지 도착: " + lbl,
                   "GPS route completed", "설정한 목적지에 도착했습니다",
                   {102, 236, 168, 255}, 2.4f);
    }

    // Detect nearby POI (shops & mission coronas)
    activePoi_ = nullptr;
    for (const auto& poi : city_.pois()) {
        if ((pPos - poi.pos).length() <= poi.radius + 1.5f) {
            activePoi_ = &poi;
            break;
        }
    }

    // Shop purchase input (options 1..12)
    if (input.shopBuyOption >= 1 && input.shopBuyOption <= 12) {
        purchaseShopItem(input.shopBuyOption);
    }

    // Smartphone purchase / summon / equip / bribe input (options 1..10)
    if (input.phoneTab >= 0 && input.phoneTab <= 3 && input.phoneOption >= 1 && input.phoneOption <= 10) {
        executePhoneAction(input.phoneTab, input.phoneOption);
    }

    // Custom Studio [K] character / gunsmith / vehicle / HUD input (options 1..8)
    if (input.customStudioTab >= 0 && input.customStudioTab <= 3 &&
        input.customStudioOption >= 1 && input.customStudioOption <= 8) {
        executeCustomStudioAction(input.customStudioTab, input.customStudioOption);
    }

    // Weapon slot selection (slots 0..10)
    if (input.weaponSelectSlot >= 0 && input.weaponSelectSlot < static_cast<int>(WeaponId::Count)) {
        if (player_.weapons[static_cast<std::size_t>(input.weaponSelectSlot)].owned) {
            const WeaponId nextW = static_cast<WeaponId>(input.weaponSelectSlot);
            if (nextW != player_.currentWeapon) {
                player_.currentWeapon = nextW;
                player_.reloadTimer = 0.0f;
                player_.weaponEquipAnim = 1.0f;
                player_.weaponRecoilAnim = 0.0f;
                player_.weaponCombatLinger = 0.0f;
            }
        }
    }

    if (input.abortMissionPressed && mission_.kind != ActiveMissionKind::None) {
        mission_.kind = ActiveMissionKind::None;
        pushAudio(AudioEventType::MissionFail, 0.8f);
        pushBanner("MISSION ABORTED", "미션 중단됨", "", "", {235, 92, 82, 255}, 2.2f);
    }

    // Interact [F / E]: Mission start corona takes priority when idle, otherwise enter/exit car
    if (input.interactPressed) {
        bool handledMission = false;
        if (mission_.kind == ActiveMissionKind::None && activePoi_) {
            if (activePoi_->kind == PoiKind::MissionDelivery ||
                activePoi_->kind == PoiKind::MissionRace ||
                activePoi_->kind == PoiKind::MissionGangRaid) {
                handledMission = startMissionFromPoi(activePoi_->kind);
            }
        }
        if (!handledMission) {
            tryEnterOrExitVehicle();
        }
    }

    if (Vehicle* drivenCar = findVehicleMutable(player_.drivingVehicleId)) {
        player_.crosshairOnTarget = false;
        if (drivenCar->destroyed) {
            player_.drivingVehicleId = -1;
            player_.pos = drivenCar->pos + Vec2{2.2f, 2.2f};
            city_.resolveCircleWorld(player_.pos, 0.45f);
            player_.posY = surfaceHeightAt(player_.pos);
            player_.onGround = true;
            player_.verticalVel = 0.0f;
        } else {
            updatePlayerDriving(dt, input, *drivenCar);
        }
    } else {
        player_.drivingVehicleId = -1;
        updatePlayerOnFoot(dt, input);
        updateWeaponCombat(dt, input);
    }

    // Bank street drift cash bonus once straight
    if (player_.activeDriftScore > 0.0f) {
        const Vehicle* car = playerVehicle();
        const bool stillDrifting = (car != nullptr && car->isDrifting);
        if (!stillDrifting) {
            player_.driftBankTimer += dt;
            if (player_.driftBankTimer >= 0.95f) {
                const int bonus = std::clamp(static_cast<int>(player_.activeDriftScore * 0.45f), 25, 800);
                player_.cash += bonus;
                player_.activeDriftScore = 0.0f;
                player_.driftBankTimer = 0.0f;
                pushAudio(AudioEventType::CashPickup, 0.85f);
                char sub[64];
                std::snprintf(sub, sizeof(sub), "+$%d Street Drift Bonus", bonus);
                pushBanner("CLEAN DRIFT!", "드리프트 보너스 획득!", sub, sub, {92, 245, 196, 255}, 1.9f);
            }
        } else {
            player_.driftBankTimer = 0.0f;
        }
    }

    updateVehicles(dt);
    resolveVehicleCollisions(dt);
    updateNpcs(dt);
    updatePickupsAndProps(dt);
    updateMission(dt);
    updateWantedSystem(dt);
    updateEffects(dt);

    maintainTrafficPopulation();
    maintainCivilianPopulation();
    maintainPolicePopulation(dt);

    if (player_.health <= 0.0f && !player_.wasted) {
        player_.health = 0.0f;
        player_.wasted = true;
        player_.wastedTimer = 3.2f;
        player_.activeDriftScore = 0.0f;
        if (mission_.kind != ActiveMissionKind::None) {
            mission_.kind = ActiveMissionKind::None;
        }

        bool policeNearby = false;
        if (player_.wantedLevel > 0) {
            const Vec2 deathPos = playerWorldPos();
            for (const auto& n : npcs_) {
                if (n.alive && n.role == NpcRole::Police && (n.pos - deathPos).lengthSq() <= 24.0f * 24.0f) {
                    policeNearby = true;
                    break;
                }
            }
            if (!policeNearby) {
                for (const auto& v : vehicles_) {
                    if (!v.destroyed && v.isPolice && v.hasNpcDriver && (v.pos - deathPos).lengthSq() <= 28.0f * 28.0f) {
                        policeNearby = true;
                        break;
                    }
                }
            }
        }

        player_.lastDeathWasBusted = policeNearby;
        pushAudio(AudioEventType::MissionFail, 1.0f);
        if (policeNearby) {
            pushBanner("BUSTED", "경찰에 체포됨 (BUSTED)",
                       "SBPD officers apprehended you...", "경찰에게 제압되어 연행 중입니다...",
                       {82, 168, 255, 255}, 3.2f);
        } else {
            pushBanner("WASTED", "행동 불능 (WASTED)",
                       "Emergency medics dispatched...", "응급 후송 중입니다...",
                       {242, 68, 68, 255}, 3.2f);
        }
    }
}

void GameWorld::updatePlayerOnFoot(float dt, const PlayerInput& input) {
    if (player_.onGround) {
        player_.coyoteTimer = 0.11f;
    } else {
        player_.coyoteTimer = std::max(0.0f, player_.coyoteTimer - dt);
    }

    if (input.jumpPressed && (player_.onGround || player_.coyoteTimer > 0.0f)) {
        player_.verticalVel = 7.35f; // ~1.46m peak jump arc — clears car hoods/roofs, crates & benches!
        player_.onGround = false;
        player_.coyoteTimer = 0.0f;
        pushAudio(AudioEventType::Jump, 0.88f);
    }

    const Vec2 camFwd = forwardFromHeading(input.cameraYaw);
    const Vec2 camRight = rightFromHeading(input.cameraYaw);

    Vec2 wishDir = camFwd * input.moveForward + camRight * input.moveRight;
    const float wishLen = wishDir.length();
    if (wishLen > 1e-4f) {
        wishDir = wishDir / wishLen;
    }

    player_.aiming = input.aimHeld;
    bool sprinting = input.sprintOrNitro && wishLen > 0.1f && !input.aimHeld && player_.stamina > 0.05f;
    if (sprinting) {
        player_.stamina = std::max(0.0f, player_.stamina - dt * 0.24f);
    } else {
        player_.stamina = std::min(1.0f, player_.stamina + dt * 0.32f);
    }

    const float targetSpeed = (wishLen > 0.05f) ? (sprinting ? 8.8f : 5.4f) : 0.0f;
    const Vec2 targetVel = wishDir * targetSpeed;
    const float accelRate = player_.onGround ? ((wishLen > 0.05f) ? 16.0f : 18.0f) : 9.5f;
    player_.velocity.x = lerpf(player_.velocity.x, targetVel.x, dt * accelRate);
    player_.velocity.z = lerpf(player_.velocity.z, targetVel.z, dt * accelRate);

    player_.pos += player_.velocity * dt;
    city_.resolveCircleWorld(player_.pos, 0.44f);

    // Integrate 3D vertical velocity & gravity
    const bool wasGrounded = player_.onGround;
    player_.verticalVel -= 18.5f * dt;
    player_.posY += player_.verticalVel * dt;

    // Push player cleanly out of vehicle sides UNLESS high enough to land/stand on the vehicle roof
    // or cleanly underneath an airborne jumping vehicle!
    for (const auto& v : vehicles_) {
        const auto& spec = getVehicleSpec(v.kind);
        const float carTopY = v.posY + 0.24f + spec.chassisHeight;
        if (player_.posY + 1.65f < v.posY) {
            continue; // Airborne vehicle flying safely overhead!
        }
        const OBB2D playerBox{player_.pos, {0.42f, 0.42f}, 0.0f};
        Vec2 mtv{};
        if (intersectOBB(playerBox, v.obb(), mtv)) {
            if (!v.destroyed && v.speed() <= 14.0f && player_.posY >= carTopY - 0.38f) {
                continue; // Standing or landing atop vehicle roof!
            }
            player_.pos += mtv;
            if (v.speed() > 11.0f) {
                applyDamageToPlayer(v.speed() * 1.35f);
                player_.velocity += v.velocity * 0.55f;
            }
        }
    }

    Vec2 carryVel{0.0f, 0.0f};
    const float supportY = computePlayerSupportHeight(player_.pos, player_.posY, &carryVel);
    const float snapMargin = (wasGrounded && player_.verticalVel <= 0.0f) ? 0.14f : 0.02f;
    if (player_.verticalVel <= 0.0f && player_.posY <= supportY + snapMargin) {
        player_.posY = supportY;
        player_.verticalVel = 0.0f;
        player_.onGround = true;
        if (carryVel.lengthSq() > 0.01f) {
            player_.pos += carryVel * dt;
            city_.resolveCircleWorld(player_.pos, 0.44f);
        }
    } else {
        player_.onGround = false;
    }

    if (input.firstPerson || input.aimHeld || input.fireHeld) {
        player_.facingYaw = wrapAngle(input.aimYaw);
    } else if (player_.velocity.length() > 0.4f) {
        const float moveYaw = std::atan2(player_.velocity.z, player_.velocity.x);
        player_.facingYaw = moveTowardsAngle(player_.facingYaw, moveYaw, dt * 12.0f);
    }

    const float spd = player_.velocity.length();
    if (spd > 0.35f && player_.onGround) {
        player_.walkAnimPhase = std::fmod(player_.walkAnimPhase + dt * spd * 1.95f, kTwoPi);
    } else if (player_.onGround) {
        player_.walkAnimPhase = lerpf(player_.walkAnimPhase, 0.0f, dt * 8.0f);
    }
}

void GameWorld::updatePlayerDriving(float dt, const PlayerInput& input, Vehicle& car) {
    if (input.hornPressed && car.hornCooldown <= 0.0f) {
        car.hornCooldown = 0.35f;
        if (isEmergencySirenVehicle(car)) {
            car.sirenActive = !car.sirenActive;
            pushAudio(AudioEventType::CarHorn, 0.85f);
            if (car.sirenActive) {
                triggerCivilianPanicNear(car.pos, 15.0f);
            }
        } else {
            pushAudio(AudioEventType::CarHorn, 0.9f);
            triggerCivilianPanicNear(car.pos, 12.5f);
        }
    }

    if (car.sirenActive) {
        car.sirenPhase = std::fmod(car.sirenPhase + dt * 2.6f, 1.0f);
    }

    stepVehiclePhysics(dt, car, input.moveForward, input.moveRight, input.handbrake, input.sprintOrNitro);

    player_.pos = car.pos;
    player_.posY = car.posY;
    player_.onGround = !car.airborne;
    player_.verticalVel = car.velY;
    player_.facingYaw = car.heading;

    // Reckless sidewalk driving, speeding near-misses, or active siren scatters nearby pedestrians!
    if (car.speed() > 13.0f) {
        if (!city_.isOnRoad(car.pos, 0.35f)) {
            triggerCivilianPanicNear(car.pos, 14.5f);
        } else {
            triggerCivilianPanicNear(car.pos, car.sirenActive ? 12.0f : 5.2f);
        }
    }

    if (car.isDrifting) {
        const float driftRate = (car.kind == VehicleKind::JdmTuner) ? 14.5f : 11.5f;
        player_.activeDriftScore += car.speed() * dt * driftRate;
        car.nitroCharge = std::min(1.0f, car.nitroCharge + dt * 0.16f);
    }
}

GameWorld::RayHit3D GameWorld::traceWorldRay3D(const Vec3& origin, const Vec3& dir, float maxDist) {
    RayHit3D result{};
    result.dist = maxDist;
    result.point = origin + dir * maxDist;

    // 1. Ground plane y = 0
    if (dir.y < -1e-5f && origin.y > 0.0f) {
        const float tGround = -origin.y / dir.y;
        if (tGround >= 0.0f && tGround < result.dist) {
            result.dist = tGround;
            result.kind = RayHit3D::Kind::Ground;
        }
    }

    // 2. 3D Buildings (main volume + upper skyscraper tiers)
    const float tBld = city_.raycastBuildings3D(origin, dir, result.dist);
    if (tBld >= 0.0f && tBld < result.dist) {
        result.dist = tBld;
        result.kind = RayHit3D::Kind::Building;
    }

    // 3. 3D NPC upright cylinders (feet y=0.04 to head y=1.75, headshot >= 1.40)
    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        float hitY = 0.0f;
        const float tNpc = intersectRayCylinderY(origin, dir, result.dist, npc.pos, 0.04f, 1.75f, 0.46f, &hitY);
        if (tNpc >= 0.0f && tNpc < result.dist) {
            result.dist = tNpc;
            result.kind = RayHit3D::Kind::Npc;
            result.npc = &npc;
            result.headshot = (hitY >= 1.40f);
            result.vehicle = nullptr;
            result.prop = nullptr;
        }
    }

    // 4. 3D Vehicle OBBs (elevated by v.posY on ramps, pier, and mid-air stunt jumps!)
    for (auto& v : vehicles_) {
        if (v.id == player_.drivingVehicleId) continue;
        const auto& vSpec = getVehicleSpec(v.kind);
        const float tVeh = intersectRayOBB3D(origin, dir, result.dist, v.obb(),
                                             v.posY + 0.12f, v.posY + 0.26f + vSpec.chassisHeight);
        if (tVeh >= 0.0f && tVeh < result.dist) {
            result.dist = tVeh;
            result.kind = RayHit3D::Kind::Vehicle;
            result.vehicle = &v;
            result.npc = nullptr;
            result.headshot = false;
            result.prop = nullptr;
        }
    }

    // 5. Shootable street props (skip heavy structural streetlights, bus shelters, billboards, and fuel pumps)
    for (auto& prop : city_.streetProps()) {
        if (prop.broken ||
            prop.kind == StreetPropKind::StreetLight ||
            prop.kind == StreetPropKind::BusShelter ||
            prop.kind == StreetPropKind::Billboard ||
            prop.kind == StreetPropKind::FuelPump) {
            continue;
        }
        float radius = 0.28f;
        float topY = 0.85f;
        if (prop.kind == StreetPropKind::Crate) {
            radius = 0.52f;
            topY = 0.92f;
        } else if (prop.kind == StreetPropKind::TrafficCone) {
            radius = 0.26f;
            topY = 0.70f;
        } else if (prop.kind == StreetPropKind::Bench) {
            radius = 0.72f;
            topY = 0.88f;
        } else if (prop.kind == StreetPropKind::TrashBin) {
            radius = 0.34f;
            topY = 0.95f;
        } else if (prop.kind == StreetPropKind::BeachUmbrella) {
            radius = 0.65f;
            topY = 2.10f;
        }
        const float tProp = intersectRayCylinderY(origin, dir, result.dist, prop.pos, 0.0f, topY, radius, nullptr);
        if (tProp >= 0.0f && tProp < result.dist) {
            result.dist = tProp;
            result.kind = RayHit3D::Kind::Prop;
            result.prop = &prop;
            result.npc = nullptr;
            result.headshot = false;
            result.vehicle = nullptr;
        }
    }

    result.point = origin + dir * result.dist;
    return result;
}

void GameWorld::updateWeaponCombat(float dt, const PlayerInput& input) {
    if (player_.fireCooldown > 0.0f) player_.fireCooldown -= dt;
    if (player_.muzzleFlashTimer > 0.0f) player_.muzzleFlashTimer -= dt;
    if (player_.weaponRecoilAnim > 0.0f) player_.weaponRecoilAnim = std::max(0.0f, player_.weaponRecoilAnim - dt * 7.5f);
    if (player_.weaponEquipAnim > 0.0f) player_.weaponEquipAnim = std::max(0.0f, player_.weaponEquipAnim - dt * 3.4f);
    if (player_.weaponCombatLinger > 0.0f) player_.weaponCombatLinger = std::max(0.0f, player_.weaponCombatLinger - dt);
    if (player_.meleeSwingAnim > 0.0f) player_.meleeSwingAnim = std::max(0.0f, player_.meleeSwingAnim - dt * 3.8f);

    const bool wantAimStance = (player_.currentWeapon != WeaponId::Fists) &&
                               (player_.reloadTimer <= 0.0f) &&
                               (input.aimHeld || input.fireHeld || player_.weaponCombatLinger > 0.0f);
    player_.aiming = input.aimHeld;
    player_.weaponAimBlend = lerpf(player_.weaponAimBlend, wantAimStance ? 1.0f : 0.0f, std::min(1.0f, dt * 11.5f));

    // Spin 6-barrel rotary Minigun cluster when pre-spooling (ADS) or firing
    const bool spinBarrels = (player_.currentWeapon == WeaponId::Minigun &&
                              player_.reloadTimer <= 0.0f &&
                              (input.fireHeld || input.aimHeld));
    const float targetSpin = spinBarrels ? (input.fireHeld ? 42.0f : 16.0f) : 0.0f;
    player_.minigunSpinSpeed = lerpf(player_.minigunSpinSpeed, targetSpin, std::min(1.0f, dt * 10.0f));
    player_.minigunSpin = std::fmod(player_.minigunSpin + player_.minigunSpinSpeed * dt, kTwoPi);

    auto& slot = player_.weapons[static_cast<std::size_t>(player_.currentWeapon)];
    const auto& spec = getWeaponSpec(player_.currentWeapon);

    // Resolve exact 3D world point under the screen-center crosshair
    Vec3 traceOrigin{};
    Vec3 traceDir{};
    if (input.hasAimRay3D && input.aimRayDir.lengthSq() > 0.25f) {
        traceDir = input.aimRayDir.normalized();
        if (input.firstPerson) {
            const Vec3 fpEye{player_.pos.x, player_.posY + 1.62f, player_.pos.z};
            traceOrigin = fpEye + traceDir * 0.10f;
        } else {
            const Vec3 playerShoulderCenter{player_.pos.x, player_.posY + 1.42f, player_.pos.z};
            const float skipBehind = std::max(0.0f, (playerShoulderCenter - input.aimRayOrigin).dot(traceDir) + 0.25f);
            traceOrigin = input.aimRayOrigin + traceDir * skipBehind;
        }
    } else {
        traceDir = directionFromYawPitch(input.aimYaw, input.aimPitch).normalized();
        const Vec2 fwd2D = forwardFromHeading(input.aimYaw);
        traceOrigin = {player_.pos.x + fwd2D.x * 0.52f, player_.posY + 1.38f, player_.pos.z + fwd2D.z * 0.52f};
    }

    const float previewRange = std::max(spec.range + 15.0f, 210.0f);
    const RayHit3D crosshairHit = traceWorldRay3D(traceOrigin, traceDir, previewRange);
    const Vec3 aimPoint3D = crosshairHit.point;
    player_.crosshairPoint3D = aimPoint3D;

    const float distFromPlayer = (aimPoint3D - Vec3{player_.pos.x, player_.posY + 1.35f, player_.pos.z}).length();
    player_.crosshairHitDist = distFromPlayer;
    player_.crosshairOnTarget =
        (distFromPlayer <= spec.range + 0.8f) &&
        ((crosshairHit.kind == RayHit3D::Kind::Npc) ||
         (crosshairHit.kind == RayHit3D::Kind::Vehicle && crosshairHit.vehicle && !crosshairHit.vehicle->destroyed));

    // Align character facing & arm elevation directly toward the 3D crosshair target when aiming/firing
    if (input.firstPerson || input.aimHeld || input.fireHeld) {
        if (input.firstPerson) {
            player_.facingYaw = wrapAngle(input.aimYaw);
            player_.aimPitch = clampf(input.aimPitch, -1.15f, 1.20f);
        } else {
            const Vec2 toTargetXZ{aimPoint3D.x - player_.pos.x, aimPoint3D.z - player_.pos.z};
            if (toTargetXZ.lengthSq() > 0.04f) {
                player_.facingYaw = std::atan2(toTargetXZ.z, toTargetXZ.x);
            } else {
                player_.facingYaw = wrapAngle(input.aimYaw);
            }
            const Vec3 fromShoulder = (aimPoint3D - Vec3{player_.pos.x, player_.posY + 1.34f, player_.pos.z}).normalized();
            player_.aimPitch = clampf(std::asin(clampf(fromShoulder.y, -0.98f, 0.98f)), -1.15f, 1.20f);
        }

        // Brandishing / aiming a firearm at civilians or traffic drivers causes immediate gunpoint surrender or panic!
        if (input.aimHeld && player_.currentWeapon != WeaponId::Fists) {
            if (crosshairHit.kind == RayHit3D::Kind::Npc && crosshairHit.npc && crosshairHit.dist < 34.0f) {
                if (crosshairHit.npc->role == NpcRole::Civilian) {
                    if (crosshairHit.dist < 11.5f &&
                        (crosshairHit.npc->behavior != NpcBehavior::PanicFlee ||
                         crosshairHit.npc->activity == NpcActivity::HandsUp)) {
                        crosshairHit.npc->activity = NpcActivity::HandsUp;
                        crosshairHit.npc->activityTimer = 0.32f;
                        crosshairHit.npc->holdingUmbrella = false;
                        crosshairHit.npc->facingYaw = std::atan2(player_.pos.z - crosshairHit.npc->pos.z,
                                                                 player_.pos.x - crosshairHit.npc->pos.x);
                    }
                    triggerCivilianPanicNear(crosshairHit.npc->pos, 13.0f);
                } else if (crosshairHit.npc->role == NpcRole::Police && crosshairHit.dist < 18.0f) {
                    reportCrime(1.1f, 1, player_.pos, true);
                }
            } else if (crosshairHit.kind == RayHit3D::Kind::Vehicle && crosshairHit.vehicle &&
                       crosshairHit.vehicle->hasNpcDriver && !crosshairHit.vehicle->isPolice &&
                       crosshairHit.dist < 28.0f) {
                crosshairHit.vehicle->driverPanicked = true;
                crosshairHit.vehicle->driverPanicTimer = 9.0f;
            }
            const Vec2 aimFwd = forwardFromHeading(player_.facingYaw);
            for (const auto& n : npcs_) {
                if (!n.alive || n.role != NpcRole::Civilian) continue;
                const Vec2 dn = n.pos - player_.pos;
                if (dn.lengthSq() < 11.5f * 11.5f && dn.dot(aimFwd) > 0.5f) {
                    triggerCivilianPanicNear(player_.pos, 12.5f);
                    break;
                }
            }
        }
    } else {
        player_.aimPitch = lerpf(player_.aimPitch, clampf(input.aimPitch * 0.35f, -0.45f, 0.45f), dt * 10.0f);
    }

    const int magCap = effectiveMagazineSize(player_.currentWeapon, slot.extendedMag);

    if (player_.reloadTimer > 0.0f) {
        player_.reloadTimer -= dt;
        if (player_.reloadTimer <= 0.0f && magCap > 0) {
            const int needed = std::max(0, magCap - slot.magAmmo);
            const int transfer = std::min(needed, slot.reserveAmmo);
            slot.magAmmo += transfer;
            slot.reserveAmmo -= transfer;
        }
        return;
    }

    auto emitDroppedMagazine = [&]() {
        if (particles_.size() >= 780) return;
        const WeaponId wid = player_.currentWeapon;
        if (wid == WeaponId::Fists || wid == WeaponId::HeavyRevolver ||
            wid == WeaponId::Shotgun || wid == WeaponId::HeavyLauncher || wid == WeaponId::Minigun) {
            return;
        }
        const Vec2 fwd2 = forwardFromHeading(player_.facingYaw);
        const Vec2 right2 = rightFromHeading(player_.facingYaw);
        Particle mag{};
        mag.kind = ParticleKind::DroppedMag;
        mag.pos = {player_.pos.x + fwd2.x * 0.32f + right2.x * 0.16f,
                   std::max(surfaceHeightAt(player_.pos, player_.posY), player_.posY) + 1.18f,
                   player_.pos.z + fwd2.z * 0.32f + right2.z * 0.16f};
        mag.vel = {right2.x * rng_.range(-0.45f, 0.35f) + fwd2.x * rng_.range(0.15f, 0.55f),
                   rng_.range(-0.35f, 0.30f),
                   right2.z * rng_.range(-0.45f, 0.35f) + fwd2.z * rng_.range(0.15f, 0.55f)};
        mag.color = {58, 62, 68, 255};
        mag.size = (wid == WeaponId::Pistol) ? 0.085f : ((wid == WeaponId::CombatMG || wid == WeaponId::AutoShotgun) ? 0.135f : 0.115f);
        mag.ttl = 3.2f;
        mag.maxTtl = 3.2f;
        mag.spinAngle = rng_.range(0.0f, kTwoPi);
        mag.spinSpeed = rng_.range(7.0f, 13.0f) * (rng_.chance(0.5f) ? 1.0f : -1.0f);
        particles_.push_back(mag);
    };

    if (input.reloadPressed && magCap > 0 &&
        slot.magAmmo < magCap && slot.reserveAmmo > 0) {
        player_.reloadTimer = spec.reloadDuration;
        emitDroppedMagazine();
        return;
    }

    if (!input.fireHeld || player_.fireCooldown > 0.0f) {
        return;
    }

    if (magCap > 0) {
        if (slot.magAmmo <= 0) {
            if (slot.reserveAmmo > 0) {
                player_.reloadTimer = spec.reloadDuration;
                emitDroppedMagazine();
            }
            return;
        }
        slot.magAmmo -= 1;
        if (slot.magAmmo == 0 && slot.reserveAmmo > 0) {
            player_.reloadTimer = spec.reloadDuration;
            emitDroppedMagazine();
        }
    }

    const bool silenced = slot.suppressor && weaponSupportsSuppressor(player_.currentWeapon);
    player_.fireCooldown = spec.fireInterval;
    player_.muzzleFlashTimer = (player_.currentWeapon == WeaponId::Fists || silenced) ? 0.0f : 0.055f;
    if (player_.currentWeapon == WeaponId::Fists) {
        player_.meleeSwingAnim = 1.0f;
        player_.meleeSwingRight = !player_.meleeSwingRight;
    } else {
        player_.weaponRecoilAnim = 1.0f;
        player_.weaponCombatLinger = 1.20f;
    }

    Vec3 muzzle3D{};
    if (input.firstPerson && input.hasAimRay3D) {
        Vec3 worldUp{0.0f, 1.0f, 0.0f};
        if (std::abs(traceDir.y) > 0.96f) worldUp = {1.0f, 0.0f, 0.0f};
        const Vec3 camRight = traceDir.cross(worldUp).normalized();
        const Vec3 camUp = camRight.cross(traceDir).normalized();
        const float sideOff = input.aimHeld ? 0.0f : 0.16f;
        const float downOff = input.aimHeld ? 0.055f : 0.125f;
        const float fwdOff = std::min(0.54f, std::max(0.14f, crosshairHit.dist * 0.42f));
        muzzle3D = input.aimRayOrigin + traceDir * fwdOff + camRight * sideOff - camUp * downOff;
    } else {
        const Vec2 right2D = rightFromHeading(player_.facingYaw);
        const Vec3 shoulder3D{player_.pos.x + right2D.x * 0.28f, player_.posY + 1.34f, player_.pos.z + right2D.z * 0.28f};
        const Vec3 toAim = aimPoint3D - shoulder3D;
        const float toAimLen = toAim.length();
        const Vec3 baseDir = (toAimLen > 1e-4f) ? (toAim / toAimLen) : traceDir;
        const float barrelOffset = std::min(0.68f, std::max(0.12f, toAimLen * 0.45f));
        muzzle3D = shoulder3D + baseDir * barrelOffset;
    }

    fireWeapon(player_.currentWeapon, muzzle3D, aimPoint3D, input.aimHeld);
}

void GameWorld::triggerExplosiveBlast3D(const Vec3& center, float radius, float baseDamage) {
    const Vec2 center2D{center.x, center.z};
    pushAudio(AudioEventType::Explosion, 1.0f);
    emitParticles({center.x, std::max(0.45f, center.y), center.z}, 36, {255, 146, 44, 255}, 14.5f, 0.58f, 0.85f);
    emitParticles({center.x, std::max(0.75f, center.y + 0.3f), center.z}, 22, {74, 70, 66, 235}, 8.8f, 0.68f, 1.25f);
    triggerCivilianPanicNear(center2D, 48.0f);
    reportCrime(3.6f, 2, center2D, true);

    bool hitAnything = false;

    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        const Vec2 d = npc.pos - center2D;
        const float len = d.length();
        if (len <= radius) {
            const float falloff = std::max(0.22f, 1.0f - len / radius);
            if (npc.role == NpcRole::Police) {
                reportCrime(3.8f, 3, npc.pos, true);
            }
            damageNpc(npc, baseDamage * falloff, (len > 1e-3f) ? (d / len) : Vec2{1.0f, 0.0f});
            hitAnything = true;
        }
    }

    for (auto& v : vehicles_) {
        if (v.destroyed) continue;
        const Vec2 d = v.pos - center2D;
        const float len = d.length();
        if (len <= radius + 1.8f) {
            const float falloff = std::max(0.25f, 1.0f - len / (radius + 1.8f));
            const float armorScale = 1.0f / (1.0f + 0.28f * static_cast<float>(v.armorUpgradeLevel));
            v.health -= baseDamage * falloff * armorScale;
            v.velocity += ((len > 1e-3f) ? (d / len) : Vec2{1.0f, 0.0f}) * (9.5f * falloff);
            v.isParked = false;
            hitAnything = true;
            if (v.health <= 0.0f) {
                explodeVehicle(v);
            }
        }
    }

    for (auto& prop : city_.streetProps()) {
        if (prop.broken ||
            prop.kind == StreetPropKind::BusShelter ||
            prop.kind == StreetPropKind::Billboard ||
            prop.kind == StreetPropKind::FuelPump) {
            continue;
        }
        const Vec2 d = prop.pos - center2D;
        const float len = d.length();
        if (len <= radius) {
            prop.broken = true;
            prop.brokenTimer = 0.0f;
            prop.velocity = ((len > 1e-3f) ? (d / len) : Vec2{1.0f, 0.0f}) * 7.5f;
            prop.velY = 4.5f;
            prop.angVelPitch = 8.2f;
            prop.angVelRoll = 6.4f;
        }
    }

    // Mild self-splash if firing right at feet
    if (player_.drivingVehicleId < 0) {
        const float selfDist = (player_.pos - center2D).length();
        if (selfDist < 4.2f) {
            applyDamageToPlayer((1.0f - selfDist / 4.2f) * 32.0f);
        }
    }

    if (hitAnything) {
        player_.hitMarkerTimer = 0.20f;
        player_.hitMarkerCritical = true;
    }
}

void GameWorld::fireWeapon(WeaponId weaponId, const Vec3& muzzle3D, const Vec3& target3D, bool precisionAim) {
    const auto& spec = getWeaponSpec(weaponId);
    const auto& slot = player_.weapons[static_cast<std::size_t>(weaponId)];
    const bool silenced = slot.suppressor && weaponSupportsSuppressor(weaponId);

    if (silenced) {
        pushAudio(AudioEventType::SuppressedShot, 0.72f);
    } else {
        switch (weaponId) {
            case WeaponId::Fists:         pushAudio(AudioEventType::MeleeSwing, 0.8f); break;
            case WeaponId::Pistol:        pushAudio(AudioEventType::PistolShot, 0.9f); break;
            case WeaponId::HeavyRevolver: pushAudio(AudioEventType::HeavyPistolShot, 1.0f); break;
            case WeaponId::SMG:           pushAudio(AudioEventType::SmgShot, 0.8f); break;
            case WeaponId::Shotgun:       pushAudio(AudioEventType::ShotgunShot, 1.0f); break;
            case WeaponId::AutoShotgun:   pushAudio(AudioEventType::ShotgunShot, 0.92f); break;
            case WeaponId::AssaultRifle:  pushAudio(AudioEventType::RifleShot, 0.95f); break;
            case WeaponId::SniperRifle:   pushAudio(AudioEventType::SniperShot, 1.0f); break;
            case WeaponId::CombatMG:      pushAudio(AudioEventType::RifleShot, 1.0f); break;
            case WeaponId::Minigun:       pushAudio(AudioEventType::MinigunShot, 0.78f); break;
            case WeaponId::HeavyLauncher: pushAudio(AudioEventType::ShotgunShot, 1.0f); break;
            default: break;
        }
    }

    if (weaponId == WeaponId::Fists) {
        triggerCivilianPanicNear({muzzle3D.x, muzzle3D.z}, 8.5f);
    } else if (silenced) {
        triggerCivilianPanicNear({muzzle3D.x, muzzle3D.z}, 8.0f);
        reportCrime(0.08f, 0, {muzzle3D.x, muzzle3D.z}, false);
    } else {
        triggerCivilianPanicNear({muzzle3D.x, muzzle3D.z}, 36.0f);
        reportCrime(0.52f, 0, {muzzle3D.x, muzzle3D.z}, false);
    }

    Vec3 baseDir = (target3D - muzzle3D).normalized();
    if (baseDir.lengthSq() < 0.25f) {
        baseDir = directionFromYawPitch(player_.facingYaw, player_.aimPitch).normalized();
    }

    // Build orthonormal tangent basis around baseDir for isotropic 3D cone spread
    Vec3 worldUp{0.0f, 1.0f, 0.0f};
    if (std::abs(baseDir.y) > 0.96f) {
        worldUp = {1.0f, 0.0f, 0.0f};
    }
    const Vec3 basisRight = baseDir.cross(worldUp).normalized();
    const Vec3 basisUp = basisRight.cross(baseDir).normalized();

    // Eject spinning 3D brass casing (or red 12-gauge shotgun hull) to the right of the chamber!
    if (weaponId != WeaponId::Fists && weaponId != WeaponId::HeavyRevolver && weaponId != WeaponId::HeavyLauncher &&
        particles_.size() < 780) {
        const bool isShotgun = (weaponId == WeaponId::Shotgun || weaponId == WeaponId::AutoShotgun);
        Particle casing{};
        casing.kind = isShotgun ? ParticleKind::ShotgunShell : ParticleKind::BrassCasing;
        casing.pos = muzzle3D - baseDir * 0.22f + basisRight * 0.06f;
        const Vec3 ejectDir = basisRight * rng_.range(1.65f, 2.55f)
                            + basisUp * rng_.range(1.35f, 2.15f)
                            - baseDir * rng_.range(0.25f, 0.85f);
        casing.vel = ejectDir;
        casing.color = isShotgun ? ColorRGBA{214, 42, 46, 255} : ColorRGBA{236, 198, 86, 255};
        casing.size = isShotgun ? 0.058f : ((weaponId == WeaponId::SniperRifle || weaponId == WeaponId::CombatMG || weaponId == WeaponId::Minigun) ? 0.052f : 0.042f);
        casing.ttl = rng_.range(1.35f, 1.95f);
        casing.maxTtl = casing.ttl;
        casing.spinAngle = rng_.range(0.0f, kTwoPi);
        casing.spinSpeed = rng_.range(14.0f, 26.0f) * (rng_.chance(0.5f) ? 1.0f : -1.0f);
        particles_.push_back(casing);
    }
    // Subtle translucent muzzle gunsmoke wisps drifting out of the barrel
    if (weaponId != WeaponId::Fists && particles_.size() < 775) {
        const int smokeN = silenced ? 1 : 2;
        for (int s = 0; s < smokeN; ++s) {
            Particle puff{};
            puff.kind = ParticleKind::WaterSpray; // Expands softly with air drag like smoke!
            puff.pos = muzzle3D + baseDir * rng_.range(0.06f, 0.22f);
            puff.vel = baseDir * rng_.range(1.2f, 2.6f)
                     + basisUp * rng_.range(0.45f, 1.05f)
                     + basisRight * rng_.range(-0.45f, 0.45f);
            puff.color = silenced ? ColorRGBA{196, 202, 210, 82} : ColorRGBA{218, 214, 206, 112};
            puff.size = (weaponId == WeaponId::Shotgun || weaponId == WeaponId::AutoShotgun || weaponId == WeaponId::HeavyLauncher) ? 0.16f : 0.11f;
            puff.ttl = rng_.range(0.24f, 0.42f);
            puff.maxTtl = puff.ttl;
            particles_.push_back(puff);
        }
    }

    float spreadScale = (weaponId == WeaponId::SniperRifle && precisionAim)
                            ? 0.008f
                            : ((precisionAim && spec.pellets == 1) ? 0.045f : (precisionAim ? 0.68f : 1.0f));
    if (silenced) {
        spreadScale *= 0.85f;
    }
    if (!precisionAim && slot.laserSightColor > 0) {
        spreadScale *= 0.70f;
    }
    const float maxSpread = spec.spreadRadians * spreadScale;

    for (int p = 0; p < spec.pellets; ++p) {
        const float jitterX = rng_.range(-maxSpread, maxSpread);
        const float jitterY = rng_.range(-maxSpread, maxSpread);
        const Vec3 shotDir = (baseDir + basisRight * jitterX + basisUp * jitterY).normalized();

        const RayHit3D hit = traceWorldRay3D(muzzle3D, shotDir, spec.range);
        const Vec3 end3D = hit.point;
        const Vec2 flatDir = Vec2{shotDir.x, shotDir.z}.normalized();

        if (spec.explosive) {
            const ColorRGBA expTr = (slot.tracerColorIdx > 0)
                                        ? getCustomTracerColorRGBA(weaponId, slot.tracerColorIdx)
                                        : ColorRGBA{255, 148, 56, 255};
            tracers_.push_back(BulletTracer{
                muzzle3D,
                end3D,
                expTr,
                0.16f,
                0.16f
            });
            triggerExplosiveBlast3D(end3D, 8.2f, spec.damage);
            continue;
        }

        const ColorRGBA trCol = getCustomTracerColorRGBA(weaponId, slot.tracerColorIdx);
        if (weaponId != WeaponId::Fists) {
            float trTtl = 0.065f;
            if (weaponId == WeaponId::SniperRifle) {
                trTtl = 0.14f;
            } else if (weaponId == WeaponId::HeavyRevolver) {
                trTtl = 0.09f;
            } else if (weaponId == WeaponId::Minigun) {
                trTtl = 0.055f;
            }
            if (slot.tracerColorIdx > 0) {
                trTtl = std::max(trTtl, 0.095f);
            }
            tracers_.push_back(BulletTracer{
                muzzle3D,
                end3D,
                trCol,
                trTtl,
                trTtl
            });
            triggerCivilianPanicNear({end3D.x, end3D.z}, silenced ? 5.5f : 15.0f);
        }

        if (hit.kind == RayHit3D::Kind::Npc && hit.npc) {
            if (hit.npc->role == NpcRole::Police) {
                reportCrime(3.8f, 3, hit.npc->pos, true);
            } else if (hit.npc->role == NpcRole::Civilian) {
                reportCrime(silenced ? 0.85f : 1.55f, 1, hit.npc->pos, false);
            } else {
                reportCrime(silenced ? 0.25f : 0.55f, 0, hit.npc->pos, false);
            }
            const float dmg = hit.headshot ? (spec.damage * 1.85f) : spec.damage;
            damageNpc(*hit.npc, dmg, flatDir);
            player_.hitMarkerTimer = 0.16f;
            player_.hitMarkerCritical = hit.headshot;
            emitParticles(end3D, hit.headshot ? 8 : 5,
                          hit.headshot ? ColorRGBA{255, 72, 48, 255} : ColorRGBA{225, 48, 48, 255},
                          hit.headshot ? 5.4f : 4.2f, 0.14f, 0.32f);
        } else if (hit.kind == RayHit3D::Kind::Vehicle && hit.vehicle) {
            if (!hit.vehicle->destroyed) {
                if (hit.vehicle->isPolice) {
                    reportCrime(2.6f, 2, hit.vehicle->pos, true);
                } else if (hit.vehicle->hasNpcDriver) {
                    hit.vehicle->driverPanicked = true;
                    hit.vehicle->driverPanicTimer = 11.0f;
                    reportCrime(silenced ? 0.55f : 1.15f, 1, hit.vehicle->pos, false);
                }
                const float armorScale = 1.0f / (1.0f + 0.28f * static_cast<float>(hit.vehicle->armorUpgradeLevel));
                hit.vehicle->health -= spec.damage * armorScale;
                player_.hitMarkerTimer = 0.12f;
                player_.hitMarkerCritical = false;
                emitParticles(end3D, 6, (slot.tracerColorIdx > 0) ? trCol : ColorRGBA{255, 198, 72, 255}, 6.0f, 0.15f, 0.28f);
                if (hit.vehicle->health <= 0.0f) {
                    explodeVehicle(*hit.vehicle);
                }
            }
        } else if (hit.kind == RayHit3D::Kind::Prop && hit.prop) {
            hit.prop->broken = true;
            hit.prop->brokenTimer = 0.0f;
            hit.prop->velocity = flatDir * 5.2f;
            hit.prop->velY = 3.4f;
            hit.prop->angVelPitch = 7.5f;
            hit.prop->angVelRoll = 5.4f;
            emitParticles(end3D, 5, {215, 215, 220, 255}, 4.8f, 0.15f, 0.32f);
        } else if (hit.kind == RayHit3D::Kind::Building || hit.kind == RayHit3D::Kind::Ground) {
            if (weaponId != WeaponId::Fists) {
                emitParticles(end3D, 4, (slot.tracerColorIdx > 0) ? trCol : ColorRGBA{220, 212, 185, 255}, 4.2f, 0.11f, 0.24f);
            }
        }
    }
}

void GameWorld::stepVehiclePhysics(float dt, Vehicle& v, float throttleInput, float steerInput,
                                   bool handbrake, bool nitro) {
    const auto& spec = getVehicleSpec(v.kind);
    const float upgradeMult = 1.0f + 0.09f * static_cast<float>(v.engineUpgradeLevel);
    const float aeroGripBonus = (v.spoilerStyle == 2) ? 1.06f : ((v.customSpoiler || v.spoilerStyle == 1) ? 1.02f : 1.0f);
    const float nitroRechargeRate =
        (v.isPlayerDriving && player_.ownedProperties[static_cast<std::size_t>(PropertyId::GlobeOilGas)])
            ? 0.11f
            : 0.055f;

    const Vec2 prevPos = v.pos;
    const Vec2 fwdBefore = forwardFromHeading(v.heading);
    const Vec2 rightBefore = rightFromHeading(v.heading);
    const Vec2 lookPos = v.pos + v.velocity * dt;

    auto samplePointGroundH = [&](const Vec2& p, float* outSlope) -> float {
        if (outSlope) *outSlope = 0.0f;
        if (city_.hasRealSeoulMap() && city_.realData().has3DRoads()) {
            const float sh = city_.surfaceHeightAt(p, v.posY);
            if (outSlope) {
                const float shF = city_.surfaceHeightAt(p + fwdBefore * 1.5f, v.posY);
                const float shB = city_.surfaceHeightAt(p - fwdBefore * 1.5f, v.posY);
                *outSlope = std::atan(clampf((shF - shB) / 3.0f, -0.35f, 0.35f));
            }
            return sh;
        }
        if (city_.isOnBridge(p, 1.5f)) {
            float slopeZ = 0.0f;
            const float bh = city_.bridgeDeckElevationAt(p, &slopeZ);
            const float th = city_.terrainElevationAt(p);
            if (bh >= th) {
                if (outSlope) {
                    *outSlope = std::atan(slopeZ * fwdBefore.z);
                }
                return bh;
            }
        }
        const float sh = city_.surfaceHeightAt(p);
        if (outSlope && sh > 0.01f) {
            *outSlope = std::atan(city_.terrainSlopeAlong(p, fwdBefore));
        }
        return sh + (city_.isOnRoad(p, 0.1f) ? 0.0f : 0.08f);
    };

    float rampSlopePitch = 0.0f;
    const float groundH = samplePointGroundH(lookPos, &rampSlopePitch);

    // Sample all 4 wheel contact patches (FL, FR, RL, RR) for true independent curb/bridge suspension!
    const float axleX = spec.halfLength * 0.63f;
    const float axleZ = spec.halfWidth * 0.86f;
    const Vec2 wheelWorld[4] = {
        lookPos + fwdBefore * axleX - rightBefore * axleZ, // FL
        lookPos + fwdBefore * axleX + rightBefore * axleZ, // FR
        lookPos - fwdBefore * axleX - rightBefore * axleZ, // RL
        lookPos - fwdBefore * axleX + rightBefore * axleZ  // RR
    };

    RsVehicleSpecParams rsSpec{};
    rsSpec.mass = spec.mass;
    rsSpec.max_forward_speed = spec.maxForwardSpeed;
    rsSpec.max_reverse_speed = spec.maxReverseSpeed;
    rsSpec.acceleration = spec.acceleration;
    rsSpec.braking = spec.braking;
    rsSpec.turn_rate = spec.turnRate;
    rsSpec.normal_grip = spec.normalGrip;
    rsSpec.drift_grip = spec.driftGrip;
    rsSpec.half_length = spec.halfLength;
    rsSpec.half_width = spec.halfWidth;
    rsSpec.chassis_height = spec.chassisHeight;

    RsVehicleControlInput rsInput{};
    rsInput.throttle = throttleInput;
    rsInput.steer = steerInput;
    rsInput.handbrake = handbrake ? 1 : 0;
    rsInput.nitro = nitro ? 1 : 0;
    rsInput.upgrade_mult = upgradeMult;
    rsInput.aero_grip_bonus = aeroGripBonus;
    rsInput.nitro_recharge_rate = nitroRechargeRate;
    rsInput.ground_height = groundH;
    rsInput.ramp_slope_pitch = rampSlopePitch;
    rsInput.wet_road_factor = rainWeather_ ? 1.0f : 0.0f;
    for (int w = 0; w < 4; ++w) {
        rsInput.wheel_ground_h[w] = samplePointGroundH(wheelWorld[w], nullptr);
    }

    RsVehiclePhysicsState rsState{};
    rsState.pos_x = v.pos.x;
    rsState.pos_y = v.posY;
    rsState.pos_z = v.pos.z;
    rsState.vel_x = v.velocity.x;
    rsState.vel_y = v.velY;
    rsState.vel_z = v.velocity.z;
    rsState.heading = v.heading;
    rsState.angular_velocity = v.angularVelocity;
    rsState.steer_visual = v.steerVisual;
    rsState.wheel_spin = v.wheelSpin;
    rsState.body_pitch = v.bodyPitch;
    rsState.body_roll = v.bodyRoll;
    rsState.pitch_vel = v.pitchVel;
    rsState.roll_vel = v.rollVel;
    for (int i = 0; i < 4; ++i) {
        rsState.wheel_comp[i] = v.wheelCompression[static_cast<std::size_t>(i)];
    }
    rsState.nitro_charge = v.nitroCharge;
    rsState.engine_rpm = v.engineRpm;
    rsState.current_gear = v.currentGear;
    rsState.braking_visual = v.brakingVisual ? 1 : 0;
    rsState.nitro_active = v.nitroActive ? 1 : 0;
    rsState.is_drifting = v.isDrifting ? 1 : 0;
    rsState.airborne = v.airborne ? 1 : 0;
    rsState.air_time = v.airTime;
    rsState.landed_air_time = 0.0f;
    rsState.landed_impact_speed = 0.0f;
    rsState.turbo_spool = v.turboSpool;
    rsState.wheel_slip = v.wheelSlip;
    rsState.shift_cut_timer = v.shiftCutTimer;
    rsState.shift_backfire = 0;
    rsState.brake_heat = v.brakeHeat;

    // Call into the native Rust physics kernel!
    oc_rs_step_vehicle_dynamics(dt, &rsState, &rsSpec, &rsInput);

    v.pos = {rsState.pos_x, rsState.pos_z};
    v.posY = rsState.pos_y;
    v.velocity = {rsState.vel_x, rsState.vel_z};
    v.velY = rsState.vel_y;
    v.heading = rsState.heading;
    v.angularVelocity = rsState.angular_velocity;
    v.steerVisual = rsState.steer_visual;
    v.wheelSpin = rsState.wheel_spin;
    v.bodyPitch = rsState.body_pitch;
    v.bodyRoll = rsState.body_roll;
    v.pitchVel = rsState.pitch_vel;
    v.rollVel = rsState.roll_vel;
    for (int i = 0; i < 4; ++i) {
        v.wheelCompression[static_cast<std::size_t>(i)] = rsState.wheel_comp[i];
    }
    v.nitroCharge = rsState.nitro_charge;
    v.engineRpm = rsState.engine_rpm;
    v.currentGear = rsState.current_gear;
    v.brakingVisual = (rsState.braking_visual != 0);
    v.nitroActive = (rsState.nitro_active != 0);
    v.isDrifting = (rsState.is_drifting != 0);
    v.airborne = (rsState.airborne != 0);
    v.airTime = rsState.air_time;
    v.turboSpool = rsState.turbo_spool;
    v.wheelSlip = rsState.wheel_slip;
    v.shiftCutTimer = rsState.shift_cut_timer;
    v.brakeHeat = rsState.brake_heat;
    v.backfireTimer = std::max(0.0f, v.backfireTimer - dt);
    v.turnSignalPhase = std::fmod(v.turnSignalPhase + dt * 3.4f, 1.0f);

    const Vec2 fwd = forwardFromHeading(v.heading);
    const Vec2 right = rightFromHeading(v.heading);

    // High-RPM upshift / turbo lift-off twin-exhaust backfire flash & ember pop!
    if (rsState.shift_backfire != 0 && !v.destroyed &&
        (spec.maxForwardSpeed >= 38.0f || v.engineUpgradeLevel >= 1)) {
        v.backfireTimer = 0.14f;
        const float exZ = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype)
                              ? (spec.halfWidth * 0.20f)
                              : (spec.halfWidth * 0.54f);
        const Vec2 rearCenter = v.pos - fwd * (spec.halfLength + 0.08f);
        for (int s = -1; s <= 1; s += 2) {
            const Vec2 tip = rearCenter + right * (static_cast<float>(s) * exZ);
            emitParticles({tip.x, v.posY + 0.30f, tip.z}, 2, {255, 162, 54, 255}, 3.6f, 0.13f, 0.16f);
        }
    }

    // Hard chassis bottom-out suspension thud & sparks on steep crests
    if (rsState.landed_air_time >= 0.36f) {
        emitParticles({v.pos.x, v.posY + 0.22f, v.pos.z}, 8, {255, 206, 82, 255}, 4.8f, 0.12f, 0.26f);
        if (v.isPlayerDriving) {
            pushAudio(AudioEventType::CarCrash, clampf(rsState.landed_impact_speed / 18.0f, 0.25f, 0.75f));
        }
    }

    // Tire skid marks & smoke for drifting OR high-torque 1st-gear launch wheelspin!
    if (v.isDrifting || (v.wheelSlip > 0.28f && !v.airborne && !v.destroyed)) {
        const Vec2 rearAxlePrev = prevPos - fwd * (spec.halfLength * 0.72f);
        const Vec2 rearAxleNow = v.pos - fwd * (spec.halfLength * 0.72f);
        const Vec2 wheelOff = right * (spec.halfWidth * 0.82f);
        const float skidAlpha = v.isDrifting ? 0.60f : clampf(v.wheelSlip * 0.52f, 0.18f, 0.48f);
        if ((rearAxleNow - rearAxlePrev).lengthSq() > 0.002f) {
            pushSkidMark(rearAxlePrev - wheelOff, rearAxleNow - wheelOff, skidAlpha);
            pushSkidMark(rearAxlePrev + wheelOff, rearAxleNow + wheelOff, skidAlpha);
        }

        const float smokeProb = v.isDrifting ? (v.driftSmokeColorIdx > 0 ? 0.82f : 0.65f) : (v.wheelSlip * 0.58f);
        if (rng_.chance(smokeProb)) {
            const ColorRGBA smokeCol = getDriftSmokeColorRGBA(v.driftSmokeColorIdx);
            const float smokeSize = (v.driftSmokeColorIdx > 0) ? 0.42f : (v.isDrifting ? 0.34f : 0.27f);
            emitParticles({rearAxleNow.x - wheelOff.x, v.posY + 0.18f, rearAxleNow.z - wheelOff.z},
                          1, smokeCol, 1.85f, smokeSize, 0.48f);
            emitParticles({rearAxleNow.x + wheelOff.x, v.posY + 0.18f, rearAxleNow.z + wheelOff.z},
                          1, smokeCol, 1.85f, smokeSize, 0.48f);
        }
    }

    // Wet-road raindrop tire spray mist behind rear wheels at highway speeds!
    const float curSpeed = v.speed();
    if (rainWeather_ && !v.airborne && !v.destroyed && curSpeed > 11.0f && particles_.size() < 760) {
        const float sprayChance = clampf((curSpeed - 9.0f) / 22.0f, 0.25f, 0.85f);
        if (rng_.chance(sprayChance)) {
            const Vec2 rearAxle = v.pos - fwd * (spec.halfLength * 0.78f);
            const Vec2 wheelOff = right * (spec.halfWidth * 0.84f);
            for (int s = -1; s <= 1; s += 2) {
                const Vec2 wp = rearAxle + wheelOff * static_cast<float>(s);
                Particle spray{};
                spray.kind = ParticleKind::WaterSpray;
                spray.pos = {wp.x, v.posY + 0.20f, wp.z};
                spray.vel = {
                    -fwd.x * curSpeed * 0.16f + right.x * static_cast<float>(s) * rng_.range(0.4f, 1.3f),
                    rng_.range(0.85f, 2.05f),
                    -fwd.z * curSpeed * 0.16f + right.z * static_cast<float>(s) * rng_.range(0.4f, 1.3f)
                };
                spray.color = {188, 214, 236, 138};
                spray.size = rng_.range(0.24f, 0.44f);
                spray.ttl = rng_.range(0.24f, 0.42f);
                spray.maxTtl = spray.ttl;
                particles_.push_back(spray);
            }
        }
    }

    // Engine damage smoke + electrical sparks when below 42% / 25% HP
    if (!v.destroyed && v.health < v.maxHealth * 0.42f && rng_.chance(0.45f)) {
        const Vec2 hood = v.pos + fwd * (spec.halfLength * 0.68f);
        const std::uint8_t shade = (v.health < v.maxHealth * 0.2f) ? 45 : 125;
        emitParticles({hood.x, v.posY + 0.85f, hood.z}, 1, {shade, shade, shade, 200}, 1.6f, 0.38f, 0.55f);
        if (v.health < v.maxHealth * 0.25f && rng_.chance(0.35f)) {
            emitParticles({hood.x, v.posY + 0.82f, hood.z}, 2, {255, 198, 72, 255}, 3.6f, 0.10f, 0.24f);
        }
    }
}

void GameWorld::updateTrafficAi(float dt, Vehicle& v) {
    if (v.driverPanicked) {
        v.driverPanicTimer -= dt;
        if (v.driverPanicTimer <= 0.0f) {
            v.driverPanicked = false;
            v.driverPanicTimer = 0.0f;
        }
    }

    Vec2 targetPoint{};
    bool useOsmNav = false;
    if (city_.hasRealSeoulMap() && v.osmRoadIdx < city_.realData().roads().size()) {
        useOsmNav = true;
        const auto& rd = city_.realData();
        const OsmRoadRec& r = rd.roads()[v.osmRoadIdx];
        if (r.vertCount >= 2) {
            v.osmVertIdx = std::min<std::uint32_t>(v.osmVertIdx, r.vertCount - 1);
            const std::uint32_t prevVi = (v.osmDir > 0)
                                             ? ((v.osmVertIdx > 0) ? (v.osmVertIdx - 1) : 0)
                                             : ((v.osmVertIdx + 1 < r.vertCount) ? (v.osmVertIdx + 1) : (r.vertCount - 1));
            const Vec2 a = rd.roadVerts()[r.firstVert + prevVi];
            const Vec2 b = rd.roadVerts()[r.firstVert + v.osmVertIdx];
            const Vec2 seg = b - a;
            const Vec2 tang = (seg.lengthSq() > 0.01f) ? seg.normalized() : forwardFromHeading(v.heading);
            const Vec2 rightN{-tang.z, tang.x};
            const RoadLaneLayout layout = RealSeoulData::computeRoadLaneLayout(r);
            const int nLanes = std::max(1, layout.lanesInDir);
            // Buses naturally prefer the outer right Seoul Bus-Only lane (still strictly inside drivable lanes, never in highway 갓길!)
            const int laneIdx = (v.kind == VehicleKind::SeoulBus) ? (nLanes - 1) : (v.id % nLanes);
            const float laneOff = layout.laneCenterOffset(laneIdx);
            targetPoint = b + rightN * laneOff;

            if ((v.pos - targetPoint).lengthSq() < 7.5f * 7.5f) {
                const int nextIdx = static_cast<int>(v.osmVertIdx) + v.osmDir;
                if (nextIdx >= 0 && nextIdx < static_cast<int>(r.vertCount)) {
                    v.osmVertIdx = static_cast<std::uint32_t>(nextIdx);
                } else {
                    std::uint32_t connR = 0, connV = 0;
                    int connD = 1;
                    if (rd.findConnectingRoadAt(b, v.osmRoadIdx, connR, connV, connD, v.posY)) {
                        v.osmRoadIdx = connR;
                        v.osmVertIdx = connV;
                        v.osmDir = connD;
                    } else {
                        v.osmDir = -v.osmDir;
                        v.osmVertIdx = static_cast<std::uint32_t>(
                            std::clamp(static_cast<int>(v.osmVertIdx) + v.osmDir, 0, static_cast<int>(r.vertCount) - 1));
                    }
                }
            }
        }
    }

    const int nearRx = city_.nearestRoadIndex(v.pos.x);
    const int nearRz = city_.nearestRoadIndex(v.pos.z);
    const Vec2 interCenter{city_.roadCoord(nearRx), city_.roadCoord(nearRz)};
    if (!useOsmNav) {
        const float roadCenter = city_.roadCoord(v.aiRoadIndex);
        float desiredHeading = 0.0f;
        if (v.aiHorizontalRoad) {
            desiredHeading = (v.aiDirection > 0) ? 0.0f : kPi;
        } else {
            desiredHeading = (v.aiDirection > 0) ? kHalfPi : -kHalfPi;
        }

        // Check intersection turning across signed 30km x 25km Seoul avenues (avoid driving into Han River!)
        const int interKey = nearRz * 1024 + nearRx;

        if ((v.pos - interCenter).length() < 3.6f && v.lastTurnIntersectionKey != interKey) {
            const Vec2 probeAhead = interCenter + forwardFromHeading(desiredHeading) * (CityMap::kBlockPitch * 0.75f);
            const bool waterAhead = city_.isInHanRiverWater(probeAhead, -6.0f) && !city_.isOnBridge(probeAhead, 0.0f);
            const bool atCityEdge = waterAhead ||
                                    (v.aiHorizontalRoad && ((v.aiDirection > 0 && nearRx >= 170) ||
                                                            (v.aiDirection < 0 && nearRx <= -170))) ||
                                    (!v.aiHorizontalRoad && ((v.aiDirection > 0 && nearRz >= 140) ||
                                                             (v.aiDirection < 0 && nearRz <= -140)));
            if (atCityEdge || rng_.chance(0.28f)) {
                v.aiHorizontalRoad = !v.aiHorizontalRoad;
                v.aiRoadIndex = v.aiHorizontalRoad ? nearRz : nearRx;
                if (v.aiHorizontalRoad) {
                    if (nearRx <= -170) v.aiDirection = 1;
                    else if (nearRx >= 170) v.aiDirection = -1;
                    else v.aiDirection = rng_.chance(0.5f) ? 1 : -1;
                } else {
                    if (nearRz <= -140) v.aiDirection = 1;
                    else if (nearRz >= 140) v.aiDirection = -1;
                    else v.aiDirection = rng_.chance(0.5f) ? 1 : -1;
                }
                v.lastTurnIntersectionKey = interKey;
            } else {
                v.lastTurnIntersectionKey = interKey;
            }
        }

        if (v.aiHorizontalRoad) {
            desiredHeading = (v.aiDirection > 0) ? 0.0f : kPi;
        } else {
            desiredHeading = (v.aiDirection > 0) ? kHalfPi : -kHalfPi;
        }

        const Vec2 laneRight = rightFromHeading(desiredHeading);
        targetPoint = v.pos + forwardFromHeading(desiredHeading) * 10.0f;
        if (v.aiHorizontalRoad) {
            targetPoint.z = city_.roadCoord(v.aiRoadIndex) + laneRight.z * CityMap::kLaneOffset;
        } else {
            targetPoint.x = city_.roadCoord(v.aiRoadIndex) + laneRight.x * CityMap::kLaneOffset;
        }
        (void)roadCenter;
    }

    const float toTargetYaw = std::atan2(targetPoint.z - v.pos.z, targetPoint.x - v.pos.x);
    const float headingErr = angleDiff(v.heading, toTargetYaw);
    const float steer = clampf(headingErr * 1.65f, -1.0f, 1.0f);

    // Lookahead braking for cars, red intersection signals, crossing pedestrians, or player in lane
    bool obstacleAhead = false;
    const Vec2 fwd = forwardFromHeading(v.heading);
    const Vec2 right = rightFromHeading(v.heading);

    // Obey red traffic signal unless the driver is fleeing in panic!
    if (!v.driverPanicked && !useOsmNav) {
        const float aheadInter = (interCenter - v.pos).dot(fwd);
        const float sideInter = std::abs((interCenter - v.pos).dot(right));
        if (aheadInter >= 10.2f && aheadInter <= 17.5f && sideInter < 8.5f) {
            const bool greenForAxis = v.aiHorizontalRoad ? isTrafficGreenForHorizontal() : isTrafficGreenForVertical();
            if (!greenForAxis) {
                obstacleAhead = true;
            }
        }
    }

    const auto& vSpec = getVehicleSpec(v.kind);
    const float vSpd = v.speed();
    const float stopGap = v.driverPanicked ? 4.2f : (5.8f + vSpd * 0.52f);
    for (const auto& other : vehicles_) {
        if (other.id == v.id) continue;
        const auto& oSpec = getVehicleSpec(other.kind);
        const Vec2 delta = other.pos - v.pos;
        const float maxR = stopGap + vSpec.halfLength + oSpec.halfLength + 6.0f;
        if (delta.lengthSq() > maxR * maxR) continue;

        const Vec2 oFwd = forwardFromHeading(other.heading);
        const Vec2 oRight = rightFromHeading(other.heading);
        const float oProjFwd  = std::abs(oFwd.dot(fwd))   * oSpec.halfLength + std::abs(oRight.dot(fwd))   * oSpec.halfWidth;
        const float oProjSide = std::abs(oFwd.dot(right)) * oSpec.halfLength + std::abs(oRight.dot(right)) * oSpec.halfWidth;

        const float ahead = delta.dot(fwd);
        const float side = std::abs(delta.dot(right));
        const float nearEdgeAhead = ahead - oProjFwd - vSpec.halfLength;
        const float lateralBound = vSpec.halfWidth + oProjSide + 0.55f;

        if (nearEdgeAhead > -0.45f && nearEdgeAhead < stopGap && side < lateralBound) {
            // Deadlock tie-breaker when two crossing vehicles stop nose-to-side inside an intersection
            const bool crossing = (std::abs(fwd.dot(oFwd)) < 0.45f);
            if (crossing && vSpd < 1.1f && other.speed() < 1.1f && v.id < other.id) {
                continue;
            }
            obstacleAhead = true;
            break;
        }

        // Predictive cross-traffic yield: brake smoothly before entering a crossing bus/car's path!
        if (!v.driverPanicked && std::abs(fwd.dot(oFwd)) < 0.45f && other.speed() > 1.4f) {
            const Vec2 futureOther = other.pos + other.velocity * 0.85f;
            const Vec2 fDelta = futureOther - v.pos;
            const float fAhead = fDelta.dot(fwd) - oProjFwd - vSpec.halfLength;
            const float fSide = std::abs(fDelta.dot(right));
            if (fAhead > 0.4f && fAhead < stopGap + 3.5f && fSide < lateralBound + 0.6f) {
                // Only yield if the other vehicle is already closer to the crossing point than we are
                const float otherAheadUs = (v.pos - other.pos).dot(oFwd);
                if (ahead > otherAheadUs + 1.0f || v.id > other.id) {
                    obstacleAhead = true;
                    break;
                }
            }
        }
    }

    // Yield to alive pedestrians crossing in front of the vehicle (calm drivers only)
    if (!obstacleAhead && !v.driverPanicked) {
        for (const auto& npc : npcs_) {
            if (!npc.alive) continue;
            const Vec2 delta = npc.pos - v.pos;
            const float ahead = delta.dot(fwd);
            const float side = std::abs(delta.dot(right));
            if (ahead > 0.8f && ahead < 14.0f && side < 3.4f) {
                obstacleAhead = true;
                break;
            }
        }
    }

    if (!obstacleAhead && !v.driverPanicked && player_.drivingVehicleId < 0) {
        const Vec2 delta = player_.pos - v.pos;
        const float ahead = delta.dot(fwd);
        const float side = std::abs(delta.dot(right));
        if (ahead > 1.0f && ahead < 11.0f && side < 2.4f) {
            obstacleAhead = true;
            if (v.hornCooldown <= 0.0f && rng_.chance(0.04f)) {
                v.hornCooldown = 2.5f;
                pushAudio(AudioEventType::CarHorn, 0.45f);
            }
        }
    }

    float throttle = 0.0f;
    if (obstacleAhead) {
        throttle = (v.speed() > 0.6f) ? -0.95f : 0.0f;
        if (v.speed() <= 0.65f) {
            v.velocity = {0.0f, 0.0f};
        }
    } else if (v.driverPanicked) {
        const float panicSpeed = 29.0f;
        throttle = (v.speed() < panicSpeed) ? 0.94f : 0.20f;
        if (v.hornCooldown <= 0.0f && rng_.chance(0.025f)) {
            v.hornCooldown = 2.2f;
            pushAudio(AudioEventType::CarHorn, 0.55f);
        }
    } else {
        float cruiseSpeed = 15.5f;
        if (useOsmNav && v.osmRoadIdx < city_.realData().roads().size()) {
            const std::uint8_t rcls = city_.realData().roads()[v.osmRoadIdx].roadClass;
            if (rcls <= 1u)      cruiseSpeed = 25.0f; // 90 km/h authentic Korean expressway cruise!
            else if (rcls == 2u) cruiseSpeed = 15.5f; // 56 km/h major city boulevard
            else if (rcls == 3u) cruiseSpeed = 13.0f; // 47 km/h secondary road
            else                 cruiseSpeed = 10.0f; // 36 km/h local street
        }
        throttle = (v.speed() < cruiseSpeed) ? ((cruiseSpeed > 20.0f) ? 0.78f : 0.58f) : 0.0f;
    }

    stepVehiclePhysics(dt, v, throttle, steer, false, false);
    if (obstacleAhead) {
        v.brakingVisual = true;
    }
}

void GameWorld::updatePoliceVehicleAi(float dt, Vehicle& v) {
    if (player_.wantedLevel <= 0) {
        v.sirenActive = false;
        v.dismountedOfficer = false;
        v.aiStuckTimer = 0.0f;
        v.aiReverseTimer = 0.0f;
        updateTrafficAi(dt, v);
        return;
    }

    v.sirenActive = true;
    v.sirenPhase = std::fmod(v.sirenPhase + dt * 2.65f, 1.0f);

    const bool hasVisual = player_.policeLineOfSight;
    const Vec2 livePos = playerWorldPos();
    const Vec2 pVel = (playerVehicle() != nullptr) ? playerVehicle()->velocity : player_.velocity;

    // When out of sight (Searching), police ONLY know policeLastKnownPos — ZERO psychic wall-hack tracking!
    const Vec2 chasePos = hasVisual ? livePos : player_.policeLastKnownPos;
    const Vec2 toTarget = chasePos - v.pos;
    const float dist = toTarget.length();

    // Once a searching cruiser reaches the Last Known Position without re-spotting the player,
    // switch into normal avenue patrol searching instead of hovering magically on top of the player!
    if (!hasVisual && dist < 24.0f) {
        updateTrafficAi(dt, v);
        return;
    }

    // Scatter pedestrians directly ahead of a screaming siren cruiser
    if (v.speed() > 8.0f) {
        const Vec2 fwdNow = forwardFromHeading(v.heading);
        triggerCivilianPanicNear(v.pos + fwdNow * 6.5f, 8.5f);
    }

    // Tactical stop & officer dismount ONLY when visually confirming a slow/on-foot suspect
    const bool suspectSlow = (player_.drivingVehicleId < 0) || (pVel.length() < 5.5f);
    if (hasVisual && suspectSlow && dist < 17.5f) {
        const float toYaw = std::atan2(toTarget.z, toTarget.x);
        const float steer = clampf(angleDiff(v.heading, toYaw) * 1.6f, -1.0f, 1.0f);
        const bool hardStop = (dist < 12.5f);
        stepVehiclePhysics(dt, v, hardStop ? 0.0f : -0.55f, steer, hardStop, false);
        if (!v.dismountedOfficer && v.speed() < 7.0f) {
            v.dismountedOfficer = true;
            const auto& spec = getVehicleSpec(v.kind);
            const Vec2 doorPos = v.pos + rightFromHeading(v.heading) * (spec.halfWidth + 1.05f);
            spawnPoliceOfficerNear(doorPos, v.kind == VehicleKind::ArmoredSuv || player_.wantedLevel >= 4);
        }
        return;
    }

    // Choose pursuit waypoint: direct intercept when visually clear, or avenue-grid routing around city blocks
    Vec2 navTarget = hasVisual ? (chasePos + pVel * clampf(dist * 0.015f, 0.0f, 0.55f)) : chasePos;
    if (dist > 20.0f && city_.raycastBuildings(v.pos, toTarget / std::max(0.01f, dist), std::min(dist, 42.0f)) >= 0.0f) {
        const int nearRx = city_.nearestRoadIndex(v.pos.x);
        const int nearRz = city_.nearestRoadIndex(v.pos.z);
        const float rx = city_.roadCoord(nearRx);
        const float rz = city_.roadCoord(nearRz);
        const bool onVertAvenue = std::abs(v.pos.x - rx) < std::abs(v.pos.z - rz);
        if (onVertAvenue && std::abs(chasePos.z - v.pos.z) > 13.0f) {
            const float stepZ = (chasePos.z >= v.pos.z) ? 26.0f : -26.0f;
            const float laneX = rx + ((chasePos.z >= v.pos.z) ? -CityMap::kLaneOffset : CityMap::kLaneOffset);
            navTarget = {laneX, v.pos.z + stepZ};
        } else if (!onVertAvenue && std::abs(chasePos.x - v.pos.x) > 13.0f) {
            const float stepX = (chasePos.x >= v.pos.x) ? 26.0f : -26.0f;
            const float laneZ = rz + ((chasePos.x >= v.pos.x) ? CityMap::kLaneOffset : -CityMap::kLaneOffset);
            navTarget = {v.pos.x + stepX, laneZ};
        } else {
            navTarget = {rx, rz};
        }
    }

    // Unstick reverse maneuver if pinned against a building or traffic pileup
    if (v.aiReverseTimer > 0.0f) {
        v.aiReverseTimer -= dt;
        const float toYaw = std::atan2(navTarget.z - v.pos.z, navTarget.x - v.pos.x);
        const float invSteer = -clampf(angleDiff(v.heading, toYaw) * 1.5f, -1.0f, 1.0f);
        stepVehiclePhysics(dt, v, -0.88f, invSteer, false, false);
        return;
    }

    if (v.speed() < 1.8f && dist > 16.0f) {
        v.aiStuckTimer += dt;
        if (v.aiStuckTimer > 1.10f) {
            v.aiReverseTimer = 0.82f;
            v.aiStuckTimer = 0.0f;
            return;
        }
    } else {
        v.aiStuckTimer = std::max(0.0f, v.aiStuckTimer - dt * 2.0f);
    }

    const float desiredYaw = std::atan2(navTarget.z - v.pos.z, navTarget.x - v.pos.x);
    const float yawErr = angleDiff(v.heading, desiredYaw);
    float steer = clampf(yawErr * 1.85f, -1.0f, 1.0f);

    // Slight lane swerve around civilian traffic cars directly ahead
    const Vec2 fwd = forwardFromHeading(v.heading);
    const Vec2 right = rightFromHeading(v.heading);
    for (const auto& other : vehicles_) {
        if (other.id == v.id || other.isPlayerDriving) continue;
        const Vec2 delta = other.pos - v.pos;
        const float ahead = delta.dot(fwd);
        const float side = delta.dot(right);
        if (ahead > 1.0f && ahead < 13.0f && std::abs(side) < 2.6f) {
            steer = clampf(steer + ((side >= 0.0f) ? -0.75f : 0.75f), -1.0f, 1.0f);
            break;
        }
    }

    // Fair realistic pursuit speed (moderate search speed when out of sight, zero unfair AI nitro!)
    const float targetSpeed = hasVisual
        ? (22.5f + 2.2f * static_cast<float>(player_.wantedLevel))
        : 16.5f;
    const bool sharpTurn = std::abs(yawErr) > 0.72f && v.speed() > 15.0f;
    const float throttle = sharpTurn ? -0.35f : ((v.speed() < targetSpeed) ? 0.90f : 0.18f);

    stepVehiclePhysics(dt, v, throttle, steer, sharpTurn && v.speed() > 21.0f, false);
}

void GameWorld::updateVehicles(float dt) {
    for (auto& v : vehicles_) {
        if (v.hornCooldown > 0.0f) v.hornCooldown -= dt;

        if (v.destroyed) {
            v.destroyedTimer += dt;
            v.sirenActive = false;
            v.velocity *= std::max(0.0f, 1.0f - 3.5f * dt);
            v.pos += v.velocity * dt;
            const float groundY = surfaceHeightAt(v.pos, v.posY);
            if (v.posY > groundY || v.velY != 0.0f) {
                v.velY -= 19.5f * dt;
                v.posY += v.velY * dt;
                if (v.posY <= groundY) {
                    v.posY = groundY;
                    v.velY = (std::abs(v.velY) > 2.2f) ? (-v.velY * 0.25f) : 0.0f;
                    v.airborne = false;
                } else {
                    v.airborne = true;
                }
            }
            if (v.destroyedTimer < 5.5f && rng_.chance(0.4f)) {
                emitParticles({v.pos.x, v.posY + 1.0f, v.pos.z}, 1, {65, 62, 60, 210}, 2.2f, 0.48f, 0.65f);
            }
            continue;
        }

        if (v.isPlayerDriving) {
            continue; // Updated in updatePlayerDriving
        }

        if (v.hasNpcDriver) {
            if (v.isPolice) {
                updatePoliceVehicleAi(dt, v);
            } else {
                updateTrafficAi(dt, v);
            }
        } else {
            if (v.sirenActive) {
                v.sirenPhase = std::fmod(v.sirenPhase + dt * 2.6f, 1.0f);
            }
            // Parked / unoccupied car still glides to rest with tire friction
            stepVehiclePhysics(dt, v, 0.0f, 0.0f, true, false);
            if (v.speed() < 0.25f) {
                v.velocity = {0.0f, 0.0f};
                v.isParked = true;
            }
        }
    }
}

void GameWorld::resolveVehicleCollisions(float dt) {
    (void)dt;
    // 1. Vehicle vs Building static OBB collisions (with 4-quadrant crumple & off-center yaw torque)
    for (auto& v : vehicles_) {
        Vec2 push{};
        if (city_.resolveOBBBuildings(v.obb(), push, v.posY)) {
            v.pos += push;
            const Vec2 n = push.normalized();
            const float intoWall = -v.velocity.dot(n);
            if (intoWall > 0.0f) {
                const Vec2 fwd = forwardFromHeading(v.heading);
                const Vec2 right = rightFromHeading(v.heading);
                const float wallFwdAlign = -n.dot(fwd);
                const float wallSideAlign = -n.dot(right);
                v.velocity += n * (intoWall * 1.32f);
                v.velocity *= 0.82f;
                // Glancing corner impact induces realistic yaw spin; head-on deceleration dives the nose down (-pitchVel)
                v.angularVelocity = clampf(v.angularVelocity + wallFwdAlign * wallSideAlign * intoWall * 0.085f, -3.5f, 3.5f);
                v.pitchVel -= clampf(wallFwdAlign * intoWall * 0.03f, -0.45f, 0.45f);

                if (intoWall > 6.0f) {
                    const float deform = clampf((intoWall - 5.5f) * 0.032f, 0.0f, 0.55f);
                    if (wallFwdAlign > 0.30f) v.frontDamage = clampf(v.frontDamage + deform, 0.0f, 1.0f);
                    else if (wallFwdAlign < -0.30f) v.rearDamage = clampf(v.rearDamage + deform, 0.0f, 1.0f);
                    if (wallSideAlign < -0.30f) v.leftDamage = clampf(v.leftDamage + deform, 0.0f, 1.0f);
                    else if (wallSideAlign > 0.30f) v.rightDamage = clampf(v.rightDamage + deform, 0.0f, 1.0f);
                }

                if (intoWall > 8.5f) {
                    const float armorScale = 1.0f / (1.0f + 0.28f * static_cast<float>(v.armorUpgradeLevel));
                    const float crashDmg = (intoWall - 7.0f) * 1.65f * armorScale;
                    v.health -= crashDmg;
                    emitParticles({v.pos.x - n.x * 1.5f, v.posY + 0.65f, v.pos.z - n.z * 1.5f},
                                  7, {255, 198, 72, 255}, 5.5f, 0.16f, 0.30f);
                    if (v.isPlayerDriving) {
                        player_.activeDriftScore = 0.0f; // Crash breaks drift combo
                        pushAudio(AudioEventType::CarCrash, clampf(intoWall / 24.0f, 0.3f, 1.0f));
                    }
                    if (!v.destroyed && v.health <= 0.0f) {
                        explodeVehicle(v);
                    }
                }
            }
        }
    }

    auto closestPointOnVehicleOBB = [](const Vehicle& v, const Vec2& target) -> Vec2 {
        const auto& sp = getVehicleSpec(v.kind);
        const Vec2 fwd = forwardFromHeading(v.heading);
        const Vec2 right = rightFromHeading(v.heading);
        const Vec2 d = target - v.pos;
        const float lx = clampf(d.dot(fwd), -sp.halfLength, sp.halfLength);
        const float lz = clampf(d.dot(right), -sp.halfWidth, sp.halfWidth);
        return v.pos + fwd * lx + right * lz;
    };

    // 2. Vehicle vs Vehicle OBB collisions via Native Rust 2-Pass Rotational + Tangential Friction Solver
    for (int iter = 0; iter < 2; ++iter) {
        for (std::size_t i = 0; i < vehicles_.size(); ++i) {
            for (std::size_t j = i + 1; j < vehicles_.size(); ++j) {
                Vehicle& a = vehicles_[i];
                Vehicle& b = vehicles_[j];
                if ((a.pos - b.pos).lengthSq() > 42.0f) continue;
                if (std::abs(a.posY - b.posY) > 1.85f) continue;

                Vec2 mtv{};
                if (!intersectOBB(a.obb(), b.obb(), mtv)) continue;

                const auto& specA = getVehicleSpec(a.kind);
                const auto& specB = getVehicleSpec(b.kind);
                const float pen = mtv.length();
                const Vec2 n = (pen > 1e-5f) ? (mtv / pen) : Vec2{1.0f, 0.0f};
                const Vec2 contactPt = (closestPointOnVehicleOBB(a, b.pos) + closestPointOnVehicleOBB(b, a.pos)) * 0.5f;

                RsRigidBody2D rbA{
                    a.pos.x, a.pos.z, a.velocity.x, a.velocity.z,
                    a.heading, a.angularVelocity, specA.mass, specA.halfLength, specA.halfWidth,
                    a.frontDamage, a.rearDamage, a.leftDamage, a.rightDamage
                };
                RsRigidBody2D rbB{
                    b.pos.x, b.pos.z, b.velocity.x, b.velocity.z,
                    b.heading, b.angularVelocity, specB.mass, specB.halfLength, specB.halfWidth,
                    b.frontDamage, b.rearDamage, b.leftDamage, b.rightDamage
                };

                const float impactSpeed = oc_rs_resolve_rigid_collision_2d(
                    &rbA, &rbB, n.x, n.z, pen, contactPt.x, contactPt.z, 0.35f);

                a.pos = {rbA.pos_x, rbA.pos_z};
                b.pos = {rbB.pos_x, rbB.pos_z};
                a.velocity = {rbA.vel_x, rbA.vel_z};
                b.velocity = {rbB.vel_x, rbB.vel_z};
                a.angularVelocity = rbA.angular_velocity;
                b.angularVelocity = rbB.angular_velocity;
                a.frontDamage = rbA.front_damage;
                a.rearDamage = rbA.rear_damage;
                a.leftDamage = rbA.left_damage;
                a.rightDamage = rbA.right_damage;
                b.frontDamage = rbB.front_damage;
                b.rearDamage = rbB.rear_damage;
                b.leftDamage = rbB.left_damage;
                b.rightDamage = rbB.right_damage;

                if (iter == 0 && impactSpeed > 0.01f) {
                    a.isParked = false;
                    b.isParked = false;

                    // Excite realistic chassis suspension pitch/roll recoil on collisions (+pitch = nose up, -pitch = nose dive)
                    if (impactSpeed > 2.5f) {
                        const float shock = clampf(impactSpeed * 0.025f, 0.04f, 0.48f);
                        const Vec2 fwdA = forwardFromHeading(a.heading);
                        const Vec2 rgtA = rightFromHeading(a.heading);
                        a.pitchVel = clampf(a.pitchVel + n.dot(fwdA) * shock, -1.6f, 1.6f);
                        a.rollVel  = clampf(a.rollVel  - n.dot(rgtA) * shock, -1.6f, 1.6f);
                        const Vec2 fwdB = forwardFromHeading(b.heading);
                        const Vec2 rgtB = rightFromHeading(b.heading);
                        b.pitchVel = clampf(b.pitchVel - n.dot(fwdB) * shock, -1.6f, 1.6f);
                        b.rollVel  = clampf(b.rollVel  + n.dot(rgtB) * shock, -1.6f, 1.6f);
                    }

                    // Metallic fender-scrape sparks even on moderate side-swipes
                    if (impactSpeed > 4.2f && impactSpeed <= 7.5f) {
                        emitParticles({contactPt.x, std::max(a.posY, b.posY) + 0.58f, contactPt.z},
                                      4, {255, 214, 108, 255}, 4.5f, 0.12f, 0.22f);
                    }

                    if (impactSpeed > 7.5f) {
                        const float dmg = (impactSpeed - 6.0f) * 1.15f;
                        a.health -= dmg / (1.0f + 0.28f * static_cast<float>(a.armorUpgradeLevel));
                        b.health -= dmg / (1.0f + 0.28f * static_cast<float>(b.armorUpgradeLevel));
                        emitParticles({contactPt.x, std::max(a.posY, b.posY) + 0.7f, contactPt.z},
                                      9, {255, 206, 84, 255}, 6.2f, 0.16f, 0.34f);
                        if (a.isPlayerDriving || b.isPlayerDriving) {
                            pushAudio(AudioEventType::CarCrash, clampf(impactSpeed / 22.0f, 0.3f, 1.0f));
                            Vehicle& other = a.isPlayerDriving ? b : a;
                            if (other.isPolice && !other.isPlayerDriving && impactSpeed > 8.5f && player_.wantedLevel == 0) {
                                reportCrime(2.4f, 2, other.pos, true);
                            } else if (other.hasNpcDriver && !other.isPolice && impactSpeed > 11.5f) {
                                other.driverPanicked = true;
                                other.driverPanicTimer = 8.5f;
                                if (impactSpeed > 18.0f) {
                                    reportCrime(1.05f, 1, other.pos, false);
                                }
                            }
                        }
                        if (!a.destroyed && a.health <= 0.0f) explodeVehicle(a);
                        if (!b.destroyed && b.health <= 0.0f) explodeVehicle(b);
                    }
                }
            }
        }
    }
}

void GameWorld::explodeVehicle(Vehicle& v) {
    if (v.destroyed) return;
    v.destroyed = true;
    v.health = 0.0f;
    v.hasNpcDriver = false;
    v.sirenActive = false;
    v.paintColor = {42, 40, 38, 255};
    v.stripeColor = {28, 28, 28, 255};

    if (v.isPlayerDriving) {
        v.isPlayerDriving = false;
        player_.drivingVehicleId = -1;
        player_.pos = v.pos - rightFromHeading(v.heading) * 2.4f;
        city_.resolveCircleWorld(player_.pos, 0.45f);
        const float ejSurf = surfaceHeightAt(player_.pos, v.posY);
        player_.posY = std::max(ejSurf, v.posY);
        player_.onGround = (player_.posY <= ejSurf + 0.05f);
        player_.verticalVel = player_.onGround ? 0.0f : v.velY;
        applyDamageToPlayer(45.0f);
    }

    if (v.isPolice) {
        reportCrime(4.2f, 3, v.pos, true);
    }

    pushAudio(AudioEventType::Explosion, 1.0f);
    emitParticles({v.pos.x, v.posY + 1.1f, v.pos.z}, 34, {255, 132, 36, 255}, 13.5f, 0.55f, 0.85f);
    emitParticles({v.pos.x, v.posY + 1.6f, v.pos.z}, 20, {72, 68, 65, 235}, 8.0f, 0.65f, 1.25f);
    triggerCivilianPanicNear(v.pos, 42.0f);

    // 3D blast radius damage to nearby NPCs (takes vehicle elevation v.posY into account)
    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        const Vec2 d = npc.pos - v.pos;
        const float npcGroundY = surfaceHeightAt(npc.pos);
        const float dist3D = std::hypot(d.length(), v.posY - npcGroundY);
        if (dist3D < 9.5f) {
            damageNpc(npc, (1.0f - dist3D / 9.5f) * 125.0f, d.normalized());
        }
    }
}

bool GameWorld::isCrosswalkSafeForPedestrian(int fromU, int fromV, int toU, int toV) const {
    const bool crossVerticalRoad = CityMap::isCrosswalkEdge(fromU, toU);   // walking E-W across N-S avenue
    const bool crossHorizontalRoad = CityMap::isCrosswalkEdge(fromV, toV); // walking N-S across E-W avenue
    if (!crossVerticalRoad && !crossHorizontalRoad) {
        return true; // Pure concrete sidewalk segment
    }

    if (crossVerticalRoad) {
        // Walk when N-S vehicle signal is RED with clearance margin
        if (!(trafficSignalTimer_ >= 0.2f && trafficSignalTimer_ < 9.0f)) {
            return false;
        }
    } else if (crossHorizontalRoad) {
        // Walk when E-W vehicle signal is RED with clearance margin
        if (!(trafficSignalTimer_ >= 12.2f && trafficSignalTimer_ < 21.0f)) {
            return false;
        }
    }

    // Also verify no fast vehicle is barreling through the crosswalk
    const Vec2 a{city_.sidewalkCoord(fromU), city_.sidewalkCoord(fromV)};
    const Vec2 b{city_.sidewalkCoord(toU), city_.sidewalkCoord(toV)};
    const Vec2 mid = (a + b) * 0.5f;

    for (const auto& v : vehicles_) {
        if (v.destroyed || v.speed() < 4.0f) continue;
        const Vec2 vFwd = forwardFromHeading(v.heading);
        const Vec2 vRight = rightFromHeading(v.heading);
        const Vec2 toCross = mid - v.pos;
        const float ahead = toCross.dot(vFwd);
        const float side = std::abs(toCross.dot(vRight));
        if (ahead > -2.5f && ahead < 18.0f && side < 6.2f) {
            return false;
        }
    }

    return true;
}

void GameWorld::chooseNextSidewalkTarget(NpcCharacter& npc, bool fleeing, const Vec2& dangerPos) {
    const int minU = CityMap::kMinRoadIndexX * 2 + 1;
    const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
    const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
    const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
    const int curU = std::clamp(npc.navToU, minU, maxU);
    const int curV = std::clamp(npc.navToV, minV, maxV);
    const int prevU = std::clamp(npc.navFromU, minU, maxU);
    const int prevV = std::clamp(npc.navFromV, minV, maxV);

    const int inDu = curU - prevU;
    const int inDv = curV - prevV;
    const Vec2 curWorld{city_.sidewalkCoord(curU), city_.sidewalkCoord(curV)};
    const Vec2 awayDir = (curWorld - dangerPos).normalized();

    struct Candidate {
        int u = 8;
        int v = 8;
        float weight = 0.0f;
    };
    std::array<Candidate, 4> cands{};
    int candCount = 0;

    static constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (const auto& [du, dv] : kDirs) {
        const int nu = curU + du;
        const int nv = curV + dv;
        if (nu < minU || nu > maxU || nv < minV || nv > maxV) continue;
        const Vec2 probe{city_.sidewalkCoord(nu), city_.sidewalkCoord(nv)};
        const Vec2 mid = (curWorld + probe) * 0.5f;
        const Vec2 q1 = curWorld + (probe - curWorld) * 0.25f;
        const Vec2 q3 = curWorld + (probe - curWorld) * 0.75f;
        if (city_.isInHanRiverWater(probe, -6.0f) || city_.isInTributaryWater(probe, 2.0f) ||
            city_.isOnRiversideExpressway(probe) || city_.isOnRiversideExpressway(mid) ||
            city_.isOnRiversideExpressway(q1) || city_.isOnRiversideExpressway(q3)) continue;

        const bool isCrosswalk = CityMap::isCrosswalkEdge(curU, nu) || CityMap::isCrosswalkEdge(curV, nv);
        const bool safeEdge = isCrosswalkSafeForPedestrian(curU, curV, nu, nv);

        if (fleeing) {
            const Vec2 nextWorld{city_.sidewalkCoord(nu), city_.sidewalkCoord(nv)};
            const Vec2 stepDir = (nextWorld - curWorld).normalized();
            float score = 2.0f + stepDir.dot(awayDir) * 2.2f;
            if (!isCrosswalk) score += 1.4f;
            else if (safeEdge) score += 0.4f;
            else score -= 1.2f;
            score += rng_.range(0.0f, 0.35f);
            cands[static_cast<std::size_t>(candCount++)] = {nu, nv, std::max(0.05f, score)};
        } else {
            const bool isStraight = (inDu != 0 || inDv != 0) && (du == inDu && dv == inDv);
            const bool isUTurn = (inDu != 0 || inDv != 0) && (du == -inDu && dv == -inDv);

            float w = 1.0f;
            if (isUTurn) {
                w = 0.04f;
            } else if (isStraight) {
                w = (!isCrosswalk) ? 5.2f : (safeEdge ? 3.8f : 0.65f);
            } else {
                w = (!isCrosswalk) ? 3.4f : (safeEdge ? 2.4f : 0.40f);
            }
            cands[static_cast<std::size_t>(candCount++)] = {nu, nv, w};
        }
    }

    if (candCount == 0) {
        npc.navFromU = curU;
        npc.navFromV = curV;
        npc.navToU = curU;
        npc.navToV = curV;
        return;
    }

    int chosenIdx = 0;
    if (fleeing) {
        float bestW = -1e9f;
        for (int i = 0; i < candCount; ++i) {
            if (cands[static_cast<std::size_t>(i)].weight > bestW) {
                bestW = cands[static_cast<std::size_t>(i)].weight;
                chosenIdx = i;
            }
        }
    } else {
        float totalW = 0.0f;
        for (int i = 0; i < candCount; ++i) {
            totalW += cands[static_cast<std::size_t>(i)].weight;
        }
        float pick = rng_.range(0.0f, totalW);
        for (int i = 0; i < candCount; ++i) {
            pick -= cands[static_cast<std::size_t>(i)].weight;
            if (pick <= 0.0f) {
                chosenIdx = i;
                break;
            }
        }
    }

    const auto& chosen = cands[static_cast<std::size_t>(chosenIdx)];
    npc.navFromU = curU;
    npc.navFromV = curV;
    npc.navToU = chosen.u;
    npc.navToV = chosen.v;
    npc.waitingAtCrosswalk = false;

    const bool nextIsCrosswalk = CityMap::isCrosswalkEdge(curU, chosen.u) || CityMap::isCrosswalkEdge(curV, chosen.v);
    if (!fleeing && !nextIsCrosswalk && rng_.chance(0.12f)) {
        npc.idlePauseTimer = rng_.range(0.9f, 2.2f);
    }
}

void GameWorld::triggerCivilianPanicNear(const Vec2& origin, float radius) {
    const float rSq = radius * radius;
    bool newlyPanicked = false;

    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        if ((npc.pos - origin).lengthSq() > rSq) continue;
        if (npc.role == NpcRole::Civilian) {
            if (npc.behavior != NpcBehavior::PanicFlee) {
                newlyPanicked = true;
            }
            npc.behavior = NpcBehavior::PanicFlee;
            npc.stateTimer = rng_.range(7.5f, 10.5f);
            npc.panicOrigin = origin;
            npc.idlePauseTimer = 0.0f;
            npc.waitingAtCrosswalk = false;
        } else if (npc.role == NpcRole::Gangster) {
            npc.behavior = NpcBehavior::CombatEngage;
        }
    }

    const float driverRsq = (radius * 0.72f) * (radius * 0.72f);
    for (auto& v : vehicles_) {
        if (!v.destroyed && v.hasNpcDriver && !v.isPolice && (v.pos - origin).lengthSq() <= driverRsq) {
            v.driverPanicked = true;
            v.driverPanicTimer = std::max(v.driverPanicTimer, 7.5f);
        }
    }

    if (newlyPanicked && panicScreamCooldown_ <= 0.0f) {
        pushAudio(AudioEventType::NpcScream, 0.72f);
        panicScreamCooldown_ = 1.65f;
    }
}

void GameWorld::damageNpc(NpcCharacter& npc, float dmg, const Vec2& hitDir) {
    if (!npc.alive) return;
    npc.health -= dmg;
    npc.holdingUmbrella = false;
    npc.activity = NpcActivity::None;
    npc.activityTimer = 0.0f;
    const float kbMag = clampf(dmg * 0.085f, 1.4f, 11.5f);
    npc.knockbackVel += hitDir * kbMag;
    npc.pos += hitDir * 0.18f;
    city_.resolveCircleWorld(npc.pos, 0.42f);

    if (npc.role == NpcRole::Gangster || npc.role == NpcRole::Police) {
        npc.behavior = NpcBehavior::CombatEngage;
    } else {
        if (npc.behavior != NpcBehavior::PanicFlee && panicScreamCooldown_ <= 0.0f) {
            pushAudio(AudioEventType::NpcScream, 0.80f);
            panicScreamCooldown_ = 1.45f;
        }
        npc.behavior = NpcBehavior::PanicFlee;
        npc.stateTimer = 9.5f;
        npc.panicOrigin = playerWorldPos();
        npc.idlePauseTimer = 0.0f;
        npc.waitingAtCrosswalk = false;
    }

    if (npc.health <= 0.0f) {
        npc.alive = false;
        npc.health = 0.0f;
        npc.corpseTimer = 0.0f;
        npc.fallProgress = 0.05f;
        int dropCash = rng_.rangeInt(20, 55);
        if (npc.role == NpcRole::Gangster) {
            dropCash = rng_.rangeInt(90, 190);
        } else if (npc.role == NpcRole::Police) {
            dropCash = rng_.rangeInt(85, 165);
            if (player_.wantedLevel > 0) {
                reportCrime(1.8f, 3, npc.pos, true);
            }
        }
        pickups_.push_back(WorldPickup{PickupKind::CashBundle, npc.pos, dropCash, 0.0f, false});
    }
}

void GameWorld::updateNpcs(float dt) {
    const Vec2 pPos = playerWorldPos();

    for (std::size_t idx = 0; idx < npcs_.size(); ++idx) {
        auto& npc = npcs_[idx];
        if (!npc.alive) {
            npc.corpseTimer += dt;
            npc.fallProgress = std::min(1.0f, npc.fallProgress + dt * 3.4f);
            if (npc.knockbackVel.lengthSq() > 0.02f) {
                npc.pos += npc.knockbackVel * dt;
                npc.knockbackVel *= std::exp(-6.5f * dt);
                city_.resolveCircleWorld(npc.pos, 0.42f);
            }
            continue;
        }
        if (npc.muzzleFlashTimer > 0.0f) {
            npc.muzzleFlashTimer = std::max(0.0f, npc.muzzleFlashTimer - dt);
        }
        if (npc.dodgeCooldown > 0.0f) {
            npc.dodgeCooldown = std::max(0.0f, npc.dodgeCooldown - dt);
        }
        if (npc.knockbackVel.lengthSq() > 0.02f) {
            npc.pos += npc.knockbackVel * dt;
            npc.knockbackVel *= std::exp(-8.5f * dt);
            city_.resolveCircleWorld(npc.pos, 0.42f);
        }

        // Rain weather umbrella auto-open for calm non-jogger civilians; fold/drop when panicking!
        if (npc.role == NpcRole::Civilian) {
            const bool wantUmbrella = rainWeather_ && (npc.behavior == NpcBehavior::Stroll) &&
                                      (npc.activity != NpcActivity::HandsUp) &&
                                      (npc.archetype != NpcArchetype::Jogger);
            npc.holdingUmbrella = wantUmbrella;
        } else {
            npc.holdingUmbrella = false;
        }

        // Vehicle run-over check + smart oncoming vehicle side-step dodge for alert civilians!
        for (const auto& v : vehicles_) {
            const float vSpd = v.speed();
            if (vSpd < 5.5f) continue;
            if (v.isPolice && !v.isPlayerDriving && npc.role == NpcRole::Police) continue;
            const Vec2 fromCar = npc.pos - v.pos;
            const float dSq = fromCar.lengthSq();
            if (dSq > 135.0f) continue;
            if (v.posY - surfaceHeightAt(npc.pos) > 1.75f) continue;

            // Early danger dodge: if a fast vehicle is barreling right toward a calm civilian, dive sideways!
            if (npc.role == NpcRole::Civilian && npc.dodgeCooldown <= 0.0f && dSq > 4.0f && dSq < 121.0f) {
                const Vec2 vDir = v.velocity / vSpd;
                const float ahead = fromCar.dot(vDir);
                const float lat = fromCar.dot(Vec2{-vDir.z, vDir.x});
                const auto& vSpec = getVehicleSpec(v.kind);
                if (ahead > 1.5f && ahead < 11.0f && std::abs(lat) < vSpec.halfWidth + 1.15f) {
                    const float sideSign = (lat >= 0.0f) ? 1.0f : -1.0f;
                    const Vec2 dodgeDir = Vec2{-vDir.z, vDir.x} * sideSign;
                    npc.knockbackVel += dodgeDir * 5.8f;
                    npc.facingYaw = std::atan2(dodgeDir.z, dodgeDir.x);
                    npc.behavior = NpcBehavior::PanicFlee;
                    npc.stateTimer = std::max(npc.stateTimer, 4.5f);
                    npc.panicOrigin = v.pos;
                    npc.holdingUmbrella = false;
                    npc.activity = NpcActivity::None;
                    npc.dodgeCooldown = 1.8f;
                    if (panicScreamCooldown_ <= 0.0f) {
                        pushAudio(AudioEventType::NpcScream, 0.55f);
                        panicScreamCooldown_ = 1.6f;
                    }
                }
            }

            if (vSpd < 6.5f || dSq > 14.0f) continue;
            const OBB2D npcBox{npc.pos, {0.42f, 0.42f}, 0.0f};
            Vec2 mtv{};
            if (intersectOBB(npcBox, v.obb(), mtv)) {
                if (v.isPlayerDriving) {
                    if (npc.role == NpcRole::Police) {
                        reportCrime(3.8f, 3, npc.pos, true);
                    } else if (npc.role == NpcRole::Civilian) {
                        reportCrime(1.65f, 1, npc.pos, false);
                    }
                }
                const Vec2 hitDir = v.velocity.normalized();
                npc.knockbackVel += hitDir * clampf(vSpd * 0.65f, 5.0f, 16.0f);
                damageNpc(npc, vSpd * 5.2f, hitDir);
                emitParticles({npc.pos.x, 1.0f, npc.pos.z}, 6, {225, 52, 52, 255}, 5.0f, 0.16f, 0.35f);
                triggerCivilianPanicNear(npc.pos, 22.0f);
                break;
            }
        }
        if (!npc.alive) continue;

        // Brief close-range gunpoint Hands-Up surrender freeze before sprinting away
        if (npc.role == NpcRole::Civilian && npc.activity == NpcActivity::HandsUp) {
            npc.activityTimer -= dt;
            npc.velocity = {0.0f, 0.0f};
            npc.headLookYaw = lerpf(npc.headLookYaw, 0.0f, dt * 8.0f);
            npc.animPhase = lerpf(npc.animPhase, 0.0f, dt * 10.0f);
            if (npc.activityTimer <= 0.0f) {
                npc.activity = NpcActivity::None;
            } else {
                continue;
            }
        }

        // 1. Seoul Police & SWAT Officers (Engage when Visual LOS active, Sidewalk Search/Beat Patrol otherwise)
        if (npc.role == NpcRole::Police && player_.wantedLevel > 0 && player_.policeLineOfSight) {
            npc.behavior = NpcBehavior::CombatEngage;
            const Vec2 deltaP = pPos - npc.pos;
            const float distToPlayer = deltaP.length();
            const Vec2 dirToPlayer = (distToPlayer > 1e-3f) ? (deltaP / distToPlayer) : Vec2{1.0f, 0.0f};
            const bool blockedByBld = city_.raycastBuildings(npc.pos, dirToPlayer, std::min(distToPlayer, 44.0f)) >= 0.0f;

            const float idealRange = (npc.weapon == WeaponId::Shotgun) ? 10.5f : 14.5f;
            if (blockedByBld || distToPlayer > idealRange) {
                Vec2 moveDir = dirToPlayer;
                if (blockedByBld) {
                    const int minU = CityMap::kMinRoadIndexX * 2 + 1;
                    const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
                    const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
                    const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
                    const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.x), minU, maxU);
                    const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.z), minV, maxV);
                    const Vec2 corner{city_.sidewalkCoord(nearU), city_.sidewalkCoord(nearV)};
                    const Vec2 toCorner = corner - npc.pos;
                    if (toCorner.lengthSq() > 1.2f) {
                        moveDir = toCorner.normalized();
                    } else {
                        const Vec2 perp{-dirToPlayer.z, dirToPlayer.x};
                        moveDir = ((npc.id % 2 == 0) ? perp : (perp * -1.0f));
                    }
                }
                const float runSpd = npc.isSwat ? 5.25f : 4.85f;
                npc.velocity = moveDir * runSpd;
                npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(moveDir.z, moveDir.x), dt * 11.0f);
            } else {
                npc.facingYaw = std::atan2(dirToPlayer.z, dirToPlayer.x);
                const Vec2 strafe = rightFromHeading(npc.facingYaw) * ((npc.id % 2 == 0) ? 2.2f : -2.2f);
                npc.velocity = strafe;
            }

            npc.fireCooldown -= dt;
            const float maxFireRange = npc.isSwat ? 46.0f : 38.0f;
            if (npc.fireCooldown <= 0.0f && distToPlayer < maxFireRange && !blockedByBld) {
                float shotDmg = 7.5f;
                float spreadAmt = 0.075f;
                AudioEventType sfx = AudioEventType::PistolShot;
                if (npc.weapon == WeaponId::SMG) {
                    npc.fireCooldown = rng_.range(0.24f, 0.40f);
                    shotDmg = 5.2f;
                    spreadAmt = 0.085f;
                    sfx = AudioEventType::SmgShot;
                } else if (npc.weapon == WeaponId::Shotgun) {
                    npc.fireCooldown = rng_.range(0.85f, 1.25f);
                    shotDmg = 12.5f;
                    spreadAmt = 0.11f;
                    sfx = AudioEventType::ShotgunShot;
                } else if (npc.weapon == WeaponId::AssaultRifle || npc.weapon == WeaponId::CombatMG) {
                    npc.fireCooldown = rng_.range(0.20f, 0.36f);
                    shotDmg = 6.8f;
                    spreadAmt = 0.065f;
                    sfx = AudioEventType::RifleShot;
                } else {
                    npc.fireCooldown = rng_.range(0.62f, 1.05f);
                }

                npc.facingYaw = std::atan2(dirToPlayer.z, dirToPlayer.x);
                npc.muzzleFlashTimer = 0.06f;
                const float spread = rng_.range(-spreadAmt, spreadAmt);
                const Vec2 shotDir = forwardFromHeading(npc.facingYaw + spread);
                const Vec2 end2D = npc.pos + shotDir * distToPlayer;
                const Vehicle* pCarRef = playerVehicle();
                const float targetY = (pCarRef != nullptr) ? (pCarRef->posY + 0.92f) : (player_.posY + 1.12f);

                tracers_.push_back(BulletTracer{
                    {npc.pos.x, 1.32f, npc.pos.z},
                    {end2D.x, targetY, end2D.z},
                    {195, 228, 255, 255},
                    0.07f,
                    0.07f
                });
                pushAudio(sfx, 0.38f);

                if (Vehicle* pCar = findVehicleMutable(player_.drivingVehicleId)) {
                    if (intersectRayOBB(npc.pos, shotDir, distToPlayer + 3.0f, pCar->obb()) >= 0.0f) {
                        pCar->health -= shotDmg / (1.0f + 0.28f * static_cast<float>(pCar->armorUpgradeLevel));
                        if (pCar->health <= 0.0f) explodeVehicle(*pCar);
                    }
                } else if (intersectRayCircle(npc.pos, shotDir, distToPlayer + 1.5f, player_.pos, 0.56f) >= 0.0f) {
                    // Jumping mid-air slightly reduces grazing hit probability
                    if (player_.onGround || rng_.chance(0.65f)) {
                        applyDamageToPlayer(shotDmg);
                    }
                }
            }

            npc.pos += npc.velocity * dt;
            city_.resolveCircleWorld(npc.pos, 0.42f);
        } else if (npc.role == NpcRole::Gangster) {
            const float distToPlayer = (pPos - npc.pos).length();
            if (distToPlayer < 32.0f) {
                npc.behavior = NpcBehavior::CombatEngage;
            }

            if (npc.behavior == NpcBehavior::CombatEngage && distToPlayer < 44.0f) {
                const Vec2 toPlayer = (pPos - npc.pos).normalized();
                npc.facingYaw = std::atan2(toPlayer.z, toPlayer.x);

                // Strafe/advance unless within ideal firing range
                if (distToPlayer > 15.0f) {
                    npc.velocity = toPlayer * 3.8f;
                } else {
                    const Vec2 strafe = rightFromHeading(npc.facingYaw) * ((npc.id % 2 == 0) ? 2.1f : -2.1f);
                    npc.velocity = strafe;
                }

                npc.fireCooldown -= dt;
                if (npc.fireCooldown <= 0.0f && distToPlayer < 36.0f) {
                    npc.fireCooldown = rng_.range(0.65f, 1.25f);
                    const float bldBlock = city_.raycastBuildings(npc.pos, toPlayer, distToPlayer);
                    if (bldBlock < 0.0f) {
                        npc.muzzleFlashTimer = 0.06f;
                        const float spread = rng_.range(-0.085f, 0.085f);
                        const Vec2 shotDir = forwardFromHeading(npc.facingYaw + spread);
                        const Vec2 end2D = npc.pos + shotDir * distToPlayer;
                        const Vehicle* pCarRef = playerVehicle();
                        const float targetY = (pCarRef != nullptr) ? (pCarRef->posY + 0.95f) : (player_.posY + 1.15f);
                        tracers_.push_back(BulletTracer{
                            {npc.pos.x, 1.28f, npc.pos.z},
                            {end2D.x, targetY, end2D.z},
                            {255, 145, 92, 255},
                            0.07f,
                            0.07f
                        });
                        pushAudio(AudioEventType::PistolShot, 0.35f);

                        // Hit check against player car or player body
                        if (Vehicle* pCar = findVehicleMutable(player_.drivingVehicleId)) {
                            if (intersectRayOBB(npc.pos, shotDir, distToPlayer + 3.0f, pCar->obb()) >= 0.0f) {
                                pCar->health -= 8.0f / (1.0f + 0.28f * static_cast<float>(pCar->armorUpgradeLevel));
                                if (pCar->health <= 0.0f) explodeVehicle(*pCar);
                            }
                        } else if (intersectRayCircle(npc.pos, shotDir, distToPlayer + 1.5f, player_.pos, 0.58f) >= 0.0f) {
                            applyDamageToPlayer(8.5f);
                        }
                    }
                }
            } else {
                // Smooth turf guard patrol around homeAnchor when idle
                if (npc.idlePauseTimer > 0.0f) {
                    npc.idlePauseTimer -= dt;
                    npc.velocity = {0.0f, 0.0f};
                } else {
                    const Vec2 toHome = npc.homeAnchor - npc.pos;
                    if (toHome.length() > 5.5f) {
                        const float homeYaw = std::atan2(toHome.z, toHome.x);
                        npc.facingYaw = moveTowardsAngle(npc.facingYaw, homeYaw, dt * 3.2f);
                    } else if (rng_.chance(dt * 0.35f)) {
                        npc.idlePauseTimer = rng_.range(1.2f, 2.8f);
                        npc.facingYaw = wrapAngle(npc.facingYaw + rng_.range(-1.1f, 1.1f));
                    }
                    npc.velocity = forwardFromHeading(npc.facingYaw) * npc.walkSpeed;
                }
            }

            npc.pos += npc.velocity * dt;
            Vec2 wallNormal{};
            if (city_.resolveCircleWorld(npc.pos, 0.42f, &wallNormal)) {
                npc.facingYaw = wrapAngle(std::atan2(wallNormal.z, wallNormal.x));
            }
        } else if (npc.role == NpcRole::Civilian && npc.behavior == NpcBehavior::PanicFlee) {
            // 2. Fast Radial Panic Sprint Escape + Safe Sidewalk Recovery!
            npc.stateTimer -= dt;

            const int minU = CityMap::kMinRoadIndexX * 2 + 1;
            const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
            const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
            const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
            const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.x), minU, maxU);
            const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.z), minV, maxV);
            const float sideX = city_.sidewalkCoord(nearU);
            const float sideZ = city_.sidewalkCoord(nearV);
            const float dx = std::abs(npc.pos.x - sideX);
            const float dz = std::abs(npc.pos.z - sideZ);
            const bool onRoadAsphalt = city_.isOnRoad(npc.pos, -0.35f);

            if (npc.stateTimer <= 0.0f && !onRoadAsphalt && std::min(dx, dz) <= 2.25f) {
                if (dz <= dx) {
                    const int blkX = std::clamp(static_cast<int>(std::floor(npc.pos.x / CityMap::kBlockPitch)),
                                                CityMap::kMinRoadIndexX, CityMap::kMaxRoadIndexX - 1);
                    npc.navFromU = 2 * blkX + 1;
                    npc.navToU   = 2 * blkX + 2;
                    npc.navFromV = nearV;
                    npc.navToV   = nearV;
                } else {
                    const int blkZ = std::clamp(static_cast<int>(std::floor(npc.pos.z / CityMap::kBlockPitch)),
                                                CityMap::kMinRoadIndexZ, CityMap::kMaxRoadIndexZ - 1);
                    npc.navFromU = nearU;
                    npc.navToU   = nearU;
                    npc.navFromV = 2 * blkZ + 1;
                    npc.navToV   = 2 * blkZ + 2;
                }
                npc.behavior = NpcBehavior::Stroll;
                npc.velocity = {0.0f, 0.0f};
            } else {
                Vec2 escapeDir{};
                if (npc.stateTimer <= 0.0f) {
                    // Calm down timer finished while on asphalt: jog straight onto nearest sidewalk curb!
                    const Vec2 curbTarget = (dx < dz) ? Vec2{sideX, npc.pos.z} : Vec2{npc.pos.x, sideZ};
                    const Vec2 toCurb = curbTarget - npc.pos;
                    escapeDir = (toCurb.lengthSq() > 1e-4f) ? toCurb.normalized() : forwardFromHeading(npc.facingYaw);
                } else {
                    Vec2 threat = npc.panicOrigin;
                    if ((pPos - npc.pos).lengthSq() < 26.0f * 26.0f &&
                        (pPos - npc.pos).lengthSq() <= (threat - npc.pos).lengthSq() + 16.0f) {
                        threat = pPos;
                    }
                    Vec2 away = npc.pos - threat;
                    escapeDir = (away.lengthSq() > 1e-4f) ? away.normalized() : forwardFromHeading(npc.facingYaw);

                    // Gently bias toward sidewalk axis so fleeing crowds naturally stream down sidewalks
                    const Vec2 toNearestSidewalk = (dx < dz) ? Vec2{sideX - npc.pos.x, 0.0f} : Vec2{0.0f, sideZ - npc.pos.z};
                    if (toNearestSidewalk.lengthSq() > 0.25f) {
                        escapeDir = (escapeDir + toNearestSidewalk.normalized() * 0.32f).normalized();
                    }

                    // Slide smoothly along building facades instead of running into walls
                    if (city_.raycastBuildings(npc.pos, escapeDir, 2.8f) >= 0.0f) {
                        const Vec2 t1{-escapeDir.z, escapeDir.x};
                        const Vec2 t2{escapeDir.z, -escapeDir.x};
                        const bool t1Clear = city_.raycastBuildings(npc.pos, t1, 2.8f) < 0.0f;
                        const bool t2Clear = city_.raycastBuildings(npc.pos, t2, 2.8f) < 0.0f;
                        if (t1Clear && !t2Clear) escapeDir = t1;
                        else if (t2Clear && !t1Clear) escapeDir = t2;
                        else escapeDir = (((npc.pos + t1 - threat).lengthSq() >= (npc.pos + t2 - threat).lengthSq()) ? t1 : t2);
                    }
                }

                // Dodge nearby vehicles
                for (const auto& v : vehicles_) {
                    const Vec2 dv = npc.pos - v.pos;
                    const float dSq = dv.lengthSq();
                    if (dSq > 1e-4f && dSq < 5.2f * 5.2f) {
                        escapeDir = (escapeDir + dv.normalized() * 1.45f).normalized();
                    }
                }

                const float sprintSpd = 6.55f + static_cast<float>(npc.id % 5) * 0.18f;
                npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(escapeDir.z, escapeDir.x), dt * 14.0f);
                npc.velocity = escapeDir * sprintSpd;

                Vec2 sep{0.0f, 0.0f};
                for (std::size_t j = 0; j < npcs_.size(); ++j) {
                    if (j == idx || !npcs_[j].alive) continue;
                    const Vec2 diff = npc.pos - npcs_[j].pos;
                    const float dSq = diff.lengthSq();
                    if (dSq > 1e-4f && dSq < 0.85f * 0.85f) {
                        const float d = std::sqrt(dSq);
                        sep += (diff / d) * ((0.85f - d) * 2.8f);
                    }
                }

                npc.pos += (npc.velocity + sep) * dt;
                Vec2 wallNormal{};
                if (city_.resolveCircleWorld(npc.pos, 0.42f, &wallNormal)) {
                    const Vec2 slide{-wallNormal.z, wallNormal.x};
                    npc.facingYaw = std::atan2(slide.z, slide.x);
                }
            }
        } else {
            // 3. Calm Civilian & Calm Beat-Police Structured Sidewalk & Crosswalk Navigation
            npc.behavior = NpcBehavior::Stroll;

            if (city_.hasRealSeoulMap() && npc.osmRoadIdx < city_.realData().roads().size()) {
                const auto& rd = city_.realData();
                const OsmRoadRec& r = rd.roads()[npc.osmRoadIdx];
                const bool wasOnRoad = rd.isOnRoad(npc.pos, 0.30f);
                if (wasOnRoad) {
                    // Gently recover any pedestrian who stepped or got pushed onto crossing asphalt back to the nearest clear sidewalk!
                    Vec2 bestCurb = npc.pos;
                    bool foundCurb = false;
                    float bestDistSq = 1e9f;
                    for (int ai = 0; ai < 16; ++ai) {
                        const float ang = static_cast<float>(ai) * (kTwoPi / 16.0f);
                        const Vec2 dir{std::cos(ang), std::sin(ang)};
                        for (float probeD : {3.5f, 7.0f, 11.5f, 16.5f, 22.5f, 29.0f, 36.0f}) {
                            const Vec2 cand = npc.pos + dir * probeD;
                            Vec2 chk = cand;
                            if (!rd.isOnRoad(cand, 0.70f) && !rd.isInWater(cand) &&
                                !rd.resolveCircleBuildings(chk, 0.55f)) {
                                const float dSq = probeD * probeD;
                                if (dSq < bestDistSq) {
                                    bestDistSq = dSq;
                                    bestCurb = cand;
                                    foundCurb = true;
                                }
                                break;
                            }
                        }
                    }
                    if (foundCurb) {
                        const Vec2 dir = (bestCurb - npc.pos).normalized();
                        npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(dir.z, dir.x), dt * 11.0f);
                        npc.velocity = dir * 4.6f;
                        npc.pos += npc.velocity * dt;
                    } else {
                        npc.velocity = {0.0f, 0.0f};
                    }
                } else if (r.vertCount >= 2) {
                    npc.osmVertIdx = std::min<std::uint32_t>(npc.osmVertIdx, r.vertCount - 1);
                    const std::uint32_t prevVi = (npc.osmDir > 0)
                                                     ? ((npc.osmVertIdx > 0) ? (npc.osmVertIdx - 1) : 0)
                                                     : ((npc.osmVertIdx + 1 < r.vertCount) ? (npc.osmVertIdx + 1) : (r.vertCount - 1));
                    const Vec2 a = rd.roadVerts()[r.firstVert + prevVi];
                    const Vec2 b = rd.roadVerts()[r.firstVert + npc.osmVertIdx];
                    const Vec2 seg = b - a;
                    const float segLen = seg.length();
                    const Vec2 tang = (segLen > 0.1f) ? (seg / segLen) : forwardFromHeading(npc.facingYaw);
                    const Vec2 rightN{-tang.z, tang.x};
                    const float hw = std::max(2.6f, r.halfWidth);

                    // Back intersection endpoints off along the block so pedestrians stay cleanly on the sidewalk corner!
                    const bool atEndpoint = (npc.osmVertIdx == 0 || npc.osmVertIdx + 1 == r.vertCount);
                    const float backOff = atEndpoint ? std::min(14.5f, segLen * 0.35f) : 0.0f;
                    const Vec2 cornerPt = b - tang * backOff;
                    Vec2 targetSidewalk = cornerPt + rightN * (npc.osmSideSign * (hw + 2.15f));
                    if (rd.isOnRoad(targetSidewalk, 0.65f)) {
                        // Try stepping slightly further outward onto the wider boulevard sidewalk, else turn back cleanly
                        const Vec2 outerCand = cornerPt + rightN * (npc.osmSideSign * (hw + 4.4f));
                        if (!rd.isOnRoad(outerCand, 0.65f)) {
                            targetSidewalk = outerCand;
                        } else {
                            npc.osmDir = -npc.osmDir;
                            npc.osmVertIdx = prevVi;
                            targetSidewalk = a + rightN * (npc.osmSideSign * (hw + 2.15f));
                        }
                    }

                    const Vec2 toTgt = targetSidewalk - npc.pos;
                    if (toTgt.lengthSq() < 2.4f * 2.4f) {
                        const int nextIdx = static_cast<int>(npc.osmVertIdx) + npc.osmDir;
                        if (nextIdx >= 0 && nextIdx < static_cast<int>(r.vertCount)) {
                            npc.osmVertIdx = static_cast<std::uint32_t>(nextIdx);
                        } else {
                            // Reverse smoothly along the same clean sidewalk block instead of cutting across 8-lane asphalt
                            npc.osmDir = -npc.osmDir;
                            npc.osmVertIdx = static_cast<std::uint32_t>(
                                std::clamp(static_cast<int>(npc.osmVertIdx) + npc.osmDir, 0, static_cast<int>(r.vertCount) - 1));
                        }
                    } else {
                        const Vec2 dir = toTgt.normalized();
                        const Vec2 nextPos = npc.pos + dir * (npc.walkSpeed * dt);
                        // Hard curb boundary: NEVER step off a clear sidewalk into crossing road asphalt!
                        if (rd.isOnRoad(nextPos, 0.40f)) {
                            npc.osmDir = -npc.osmDir;
                            npc.osmVertIdx = prevVi;
                            npc.velocity = dir * (-npc.walkSpeed * 0.6f);
                        } else {
                            npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(dir.z, dir.x), dt * 8.0f);
                            npc.velocity = dir * npc.walkSpeed;
                        }
                    }
                    const Vec2 stepped = npc.pos + npc.velocity * dt;
                    if (!rd.isOnRoad(stepped, 0.35f)) {
                        npc.pos = stepped;
                    }
                }
                city_.resolveCircleWorld(npc.pos, 0.42f);
            } else if (npc.role == NpcRole::Police && city_.isOnRoad(npc.pos, -0.35f) && !city_.isInIntersection(npc.pos)) {
                const int minU = CityMap::kMinRoadIndexX * 2 + 1;
                const int maxU = CityMap::kMaxRoadIndexX * 2 - 2;
                const int minV = CityMap::kMinRoadIndexZ * 2 + 1;
                const int maxV = CityMap::kMaxRoadIndexZ * 2 - 2;
                const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.x), minU, maxU);
                const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(npc.pos.z), minV, maxV);
                const Vec2 curb{city_.sidewalkCoord(nearU), city_.sidewalkCoord(nearV)};
                const Vec2 dir = (curb - npc.pos).normalized();
                npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(dir.z, dir.x), dt * 9.0f);
                npc.velocity = dir * 2.8f;
                npc.pos += npc.velocity * dt;
                city_.resolveCircleWorld(npc.pos, 0.42f);
            } else {
                const Vec2 fromNode{city_.sidewalkCoord(npc.navFromU), city_.sidewalkCoord(npc.navFromV)};
                const Vec2 toNode{city_.sidewalkCoord(npc.navToU), city_.sidewalkCoord(npc.navToV)};

                if (npc.navFromU == npc.navToU && npc.navFromV == npc.navToV) {
                    const Vec2 toCorner = toNode - npc.pos;
                    const float distCorner = toCorner.length();
                    if (distCorner < 0.85f) {
                        chooseNextSidewalkTarget(npc, false, pPos);
                    } else {
                        const Vec2 dir = toCorner / distCorner;
                        npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(dir.z, dir.x), dt * 8.0f);
                        npc.velocity = dir * npc.walkSpeed;
                    }
                } else {
                    const Vec2 edgeVec = toNode - fromNode;
                    const float edgeLen = std::max(0.01f, edgeVec.length());
                    const Vec2 edgeDir = edgeVec / edgeLen;
                    const float edgeYaw = std::atan2(edgeDir.z, edgeDir.x);
                    const Vec2 laneOffset = rightFromHeading(edgeYaw) * 0.45f;
                    const Vec2 laneStart = fromNode + laneOffset;
                    const Vec2 laneEnd = toNode + laneOffset;

                    const float progress = (npc.pos - laneStart).dot(edgeDir);

                    if ((npc.pos - laneEnd).length() < 0.72f || progress >= edgeLen - 0.30f) {
                        chooseNextSidewalkTarget(npc, false, pPos);
                    }

                    // Recompute current edge after potential corner transition
                    const Vec2 curFrom{city_.sidewalkCoord(npc.navFromU), city_.sidewalkCoord(npc.navFromV)};
                    const Vec2 curTo{city_.sidewalkCoord(npc.navToU), city_.sidewalkCoord(npc.navToV)};
                    const Vec2 curVec = curTo - curFrom;
                    const float curLen = std::max(0.01f, curVec.length());
                    const Vec2 curDir = curVec / curLen;
                    const float curYaw = std::atan2(curDir.z, curDir.x);
                    const Vec2 curLaneStart = curFrom + rightFromHeading(curYaw) * 0.45f;
                    const float curProgress = (npc.pos - curLaneStart).dot(curDir);
                    const bool curIsCrosswalk = CityMap::isCrosswalkEdge(npc.navFromU, npc.navToU) ||
                                                CityMap::isCrosswalkEdge(npc.navFromV, npc.navToV);

                    // Curb-side pedestrian signal check before stepping onto crosswalk asphalt
                    if (curIsCrosswalk && curProgress < 1.35f) {
                        if (!isCrosswalkSafeForPedestrian(npc.navFromU, npc.navFromV, npc.navToU, npc.navToV)) {
                            npc.waitingAtCrosswalk = true;
                        } else {
                            npc.waitingAtCrosswalk = false;
                        }
                    } else {
                        npc.waitingAtCrosswalk = false;
                    }

                    if (npc.waitingAtCrosswalk) {
                        const Vec2 waitSpot = curLaneStart + curDir * 0.85f;
                        const Vec2 toWait = waitSpot - npc.pos;
                        if (toWait.length() > 0.22f) {
                            npc.velocity = toWait.normalized() * (npc.walkSpeed * 0.75f);
                        } else {
                            npc.velocity = {0.0f, 0.0f};
                        }
                        npc.facingYaw = moveTowardsAngle(npc.facingYaw, curYaw, dt * 7.0f);
                    } else if (!curIsCrosswalk && npc.idlePauseTimer > 0.0f) {
                        npc.idlePauseTimer = std::max(0.0f, npc.idlePauseTimer - dt);
                        npc.velocity = {0.0f, 0.0f};
                        if (npc.role == NpcRole::Civilian && npc.activity == NpcActivity::None) {
                            npc.activity = (npc.heldProp == NpcHeldProp::Smartphone || rng_.chance(0.45f))
                                               ? NpcActivity::CheckingPhone
                                               : NpcActivity::LookingAround;
                            npc.activityTimer = npc.idlePauseTimer;
                        }
                        if (npc.idlePauseTimer <= 0.0f) {
                            npc.activity = NpcActivity::None;
                        }
                    } else {
                        if (npc.role == NpcRole::Civilian && npc.activity != NpcActivity::HandsUp) {
                            if (npc.activityTimer > 0.0f) {
                                npc.activityTimer = std::max(0.0f, npc.activityTimer - dt);
                                if (npc.activityTimer <= 0.0f) npc.activity = NpcActivity::None;
                            } else if (!curIsCrosswalk && npc.heldProp == NpcHeldProp::Smartphone &&
                                       npc.archetype != NpcArchetype::Jogger && rng_.chance(dt * 0.14f)) {
                                // StreetTech & casual pedestrians occasionally glance down at their smartphone while walking!
                                npc.activity = NpcActivity::CheckingPhone;
                                npc.activityTimer = rng_.range(2.2f, 4.5f);
                            }
                        }

                        const float clampedS = clampf(curProgress, 0.0f, curLen);
                        const float lookS = std::min(curLen, clampedS + 2.2f);
                        const Vec2 steerPoint = curLaneStart + curDir * lookS;
                        const Vec2 desiredDir = (steerPoint - npc.pos).normalized();

                        float moveSpd = npc.walkSpeed;
                        if (curIsCrosswalk) {
                            moveSpd = npc.walkSpeed * 1.28f; // Brisk crosswalk pace
                        } else if (npc.activity == NpcActivity::CheckingPhone) {
                            moveSpd = npc.walkSpeed * 0.72f; // Slower pace while texting
                        }

                        const float targetYaw = (desiredDir.lengthSq() > 0.01f)
                                                    ? std::atan2(desiredDir.z, desiredDir.x)
                                                    : curYaw;
                        npc.facingYaw = moveTowardsAngle(npc.facingYaw, targetYaw, dt * 9.0f);
                        npc.velocity = desiredDir * moveSpd;
                    }
                }

                // Gentle personal-space separation between nearby pedestrians
                Vec2 sep{0.0f, 0.0f};
                for (std::size_t j = 0; j < npcs_.size(); ++j) {
                    if (j == idx || !npcs_[j].alive) continue;
                    const Vec2 diff = npc.pos - npcs_[j].pos;
                    const float dSq = diff.lengthSq();
                    if (dSq > 1e-4f && dSq < 0.78f * 0.78f) {
                        const float d = std::sqrt(dSq);
                        sep += (diff / d) * ((0.78f - d) * 2.2f);
                    }
                }
                npc.pos += (npc.velocity + sep) * dt;
                city_.resolveCircleWorld(npc.pos, 0.42f);
            }
        }

        // Natural head-turning curiosity (look at nearby player/supercar, or check traffic at crosswalks!)
        float targetHeadYaw = 0.0f;
        if (npc.behavior == NpcBehavior::Stroll) {
            const Vec2 toPlayer = pPos - npc.pos;
            const float pDistSq = toPlayer.lengthSq();
            if (pDistSq > 1.2f && pDistSq < 11.5f * 11.5f) {
                const float relYaw = wrapAngle(std::atan2(toPlayer.z, toPlayer.x) - npc.facingYaw);
                if (std::abs(relYaw) < 1.35f) {
                    targetHeadYaw = clampf(relYaw * 0.72f, -0.78f, 0.78f);
                }
            } else if (npc.waitingAtCrosswalk || npc.activity == NpcActivity::LookingAround) {
                targetHeadYaw = std::sin(trafficSignalTimer_ * (kTwoPi / 4.0f) + static_cast<float>(npc.id) * 1.7f) * 0.58f;
            }
        } else if (npc.behavior == NpcBehavior::PanicFlee) {
            // Brief fearful glance back over shoulder while sprinting away
            const Vec2 toThreat = npc.panicOrigin - npc.pos;
            if (toThreat.lengthSq() > 1.0f) {
                const float relYaw = wrapAngle(std::atan2(toThreat.z, toThreat.x) - npc.facingYaw);
                targetHeadYaw = clampf(relYaw * 0.35f, -0.65f, 0.65f);
            }
        }
        npc.headLookYaw = lerpf(npc.headLookYaw, targetHeadYaw, dt * 6.5f);

        const float spd = npc.velocity.length();
        if (spd > 0.2f) {
            const float phaseScale = (npc.behavior == NpcBehavior::PanicFlee) ? 2.55f
                                   : ((npc.archetype == NpcArchetype::Jogger) ? 2.40f : 2.2f);
            npc.animPhase = std::fmod(npc.animPhase + dt * spd * phaseScale, kTwoPi);
        } else {
            npc.animPhase = lerpf(npc.animPhase, 0.0f, dt * 8.0f);
        }
    }
}

void GameWorld::updatePickupsAndProps(float dt) {
    const Vec2 pPos = playerWorldPos();
    const float collectRadius = (player_.drivingVehicleId >= 0) ? 3.2f : 1.85f;

    for (auto& pk : pickups_) {
        if (pk.collected) continue;
        pk.bobPhase = std::fmod(pk.bobPhase + dt * 3.4f, kTwoPi);
        if ((pk.pos - pPos).length() <= collectRadius) {
            pk.collected = true;
            if (pk.kind == PickupKind::CashBundle) {
                player_.cash += pk.amount;
                pushAudio(AudioEventType::CashPickup, 0.9f);
            } else if (pk.kind == PickupKind::HealthKit) {
                player_.health = std::min(player_.maxHealth, player_.health + static_cast<float>(pk.amount));
                pushAudio(AudioEventType::ShopBuy, 0.9f);
            } else if (pk.kind == PickupKind::MissionBriefcase) {
                pushAudio(AudioEventType::CashPickup, 1.0f);
            }
        }
    }

    pickups_.erase(
        std::remove_if(pickups_.begin(), pickups_.end(), [](const WorldPickup& p) { return p.collected; }),
        pickups_.end());

    // Destructible 3D tumbling street props & pressurized fire hydrant water geysers!
    for (auto& prop : city_.streetProps()) {
        if (prop.kind == StreetPropKind::BusShelter ||
            prop.kind == StreetPropKind::Billboard ||
            prop.kind == StreetPropKind::FuelPump ||
            prop.kind == StreetPropKind::SubwayEntrance ||
            prop.kind == StreetPropKind::RoadGantry ||
            prop.kind == StreetPropKind::TrafficLight ||
            prop.kind == StreetPropKind::PedGuardrail ||
            prop.kind == StreetPropKind::ShopSign ||
            prop.kind == StreetPropKind::UtilityBox ||
            prop.kind == StreetPropKind::StreetKiosk ||
            prop.kind == StreetPropKind::DdareungiRack ||
            prop.kind == StreetPropKind::MedianBarrier) {
            continue;
        }
        if (prop.broken) {
            prop.brokenTimer += dt;
            if (prop.brokenTimer >= 14.0f && (prop.homePos - pPos).lengthSq() > 14.0f * 14.0f) {
                prop.broken = false;
                prop.brokenTimer = 0.0f;
                prop.pos = prop.homePos;
                prop.posY = 0.0f;
                prop.velocity = {0.0f, 0.0f};
                prop.velY = 0.0f;
                prop.pitch = 0.0f;
                prop.roll = 0.0f;
                prop.angVelPitch = 0.0f;
                prop.angVelRoll = 0.0f;
                continue;
            }
            prop.pos += prop.velocity * dt;
            prop.velocity *= std::max(0.0f, 1.0f - 3.4f * dt);

            // 3D ballistic arc, pitch/roll tumbling & asphalt bounce
            if (prop.posY > 0.0f || prop.velY != 0.0f) {
                prop.velY -= 16.5f * dt;
                prop.posY += prop.velY * dt;
                prop.pitch = wrapAngle(prop.pitch + prop.angVelPitch * dt);
                prop.roll = wrapAngle(prop.roll + prop.angVelRoll * dt);
                if (prop.posY <= 0.0f) {
                    prop.posY = 0.0f;
                    if (std::abs(prop.velY) > 1.35f) {
                        prop.velY = -prop.velY * 0.36f;
                        prop.angVelPitch *= 0.55f;
                        prop.angVelRoll *= 0.55f;
                    } else {
                        prop.velY = 0.0f;
                        prop.angVelPitch = 0.0f;
                        prop.angVelRoll = 0.0f;
                        prop.pitch = lerpf(prop.pitch, kHalfPi * 0.92f, dt * 8.0f);
                    }
                }
            }

            // Broken Fire Hydrant erupts a 4.5m tall pressurized water geyser at its base pipe (homePos)!
            if (prop.kind == StreetPropKind::Hydrant && prop.brokenTimer < 14.0f) {
                const Vec2 geyserPos = (prop.homePos.lengthSq() > 0.001f) ? prop.homePos : prop.pos;
                if (rng_.chance(0.75f)) {
                    emitParticles({geyserPos.x, 1.2f + rng_.range(0.0f, 2.6f), geyserPos.z},
                                  2, {138, 224, 255, 225}, 6.8f, 0.24f, 0.48f);
                }
                // Upward hydro-jet lift on any vehicle or player passing through the 4.5m hydrant column!
                for (auto& v : vehicles_) {
                    if (!v.destroyed && v.posY < 4.5f && (v.pos - geyserPos).lengthSq() < 2.1f * 2.1f) {
                        v.velY = std::max(v.velY, 6.2f);
                        v.airborne = true;
                        v.pitchVel += 0.18f;
                    }
                }
                if (player_.drivingVehicleId < 0 && player_.posY < 4.5f &&
                    (player_.pos - geyserPos).lengthSq() < 1.35f * 1.35f) {
                    player_.verticalVel = std::max(player_.verticalVel, 8.2f);
                    player_.onGround = false;
                }
            }
            continue;
        }

        const float minBreakSpeed = (prop.kind == StreetPropKind::StreetLight) ? 11.0f : 4.5f;
        const float maxPropClearance = (prop.kind == StreetPropKind::StreetLight) ? 3.2f : 1.35f;
        const float propBaseY = (prop.baseY > -1e8f) ? prop.baseY : surfaceHeightAt(prop.pos);
        for (auto& v : vehicles_) {
            if (v.speed() < minBreakSpeed) continue;
            if (std::abs(v.posY - propBaseY) > maxPropClearance) continue;
            if ((v.pos - prop.pos).lengthSq() < 6.2f) {
                prop.broken = true;
                prop.brokenTimer = 0.0f;
                prop.velocity = v.velocity * ((prop.kind == StreetPropKind::StreetLight) ? 0.45f : 0.78f);
                prop.velY = std::clamp(v.speed() * 0.24f, 2.8f, 6.4f);
                prop.angVelPitch = rng_.range(5.5f, 10.5f);
                prop.angVelRoll = rng_.range(-8.0f, 8.0f);
                if (prop.kind == StreetPropKind::StreetLight) {
                    v.frontDamage = clampf(v.frontDamage + 0.08f, 0.0f, 1.0f);
                }
                emitParticles({prop.pos.x, 0.7f, prop.pos.z}, 6,
                              (prop.kind == StreetPropKind::Hydrant) ? ColorRGBA{135, 224, 255, 240} : ColorRGBA{255, 210, 92, 255},
                              5.2f, 0.18f, 0.38f);
                break;
            }
        }
    }
}

void GameWorld::updateMission(float dt) {
    if (mission_.kind == ActiveMissionKind::None) return;

    mission_.timeRemaining -= dt;
    if (mission_.timeRemaining <= 0.0f) {
        mission_.kind = ActiveMissionKind::None;
        pushAudio(AudioEventType::MissionFail, 1.0f);
        pushBanner("MISSION FAILED", "미션 실패 (시간 초과)",
                   "Out of time!", "제한 시간이 종료되었습니다!",
                   {242, 78, 72, 255}, 3.4f);
        return;
    }

    if (mission_.kind == ActiveMissionKind::ExoticDelivery) {
        const Vehicle* targetCar = findVehicle(mission_.targetVehicleId);
        if (!targetCar || targetCar->destroyed) {
            mission_.kind = ActiveMissionKind::None;
            pushAudio(AudioEventType::MissionFail, 1.0f);
            pushBanner("MISSION FAILED", "미션 실패 (목표 차량 파손)",
                       "The VIP Apex GT was wrecked!", "목표 슈퍼카가 파괴되었습니다!",
                       {242, 78, 72, 255}, 3.4f);
            return;
        }

        if (mission_.stage == 0) {
            mission_.currentObjectivePos = targetCar->pos;
            if (player_.drivingVehicleId == targetCar->id) {
                mission_.stage = 1;
                mission_.currentObjectivePos = {1008.0f, 1176.0f}; // Gangnam Teheran VIP drop-off
                mission_.objectiveRadius = 7.5f;
                mission_.objectiveEn = "Deliver the VIP Hypercar to Gangnam Drop-Off!";
                mission_.objectiveKo = "탈취한 VIP 하이퍼카를 강남 인도 지점까지 배달하세요!";
            }
        } else if (mission_.stage == 1) {
            if (player_.drivingVehicleId == targetCar->id &&
                (targetCar->pos - mission_.currentObjectivePos).length() <= mission_.objectiveRadius) {
                const int integrityBonus = static_cast<int>((targetCar->health / targetCar->maxHealth) * 400.0f);
                const int totalReward = mission_.rewardCash + integrityBonus;
                player_.cash += totalReward;
                mission_.completedCount += 1;
                mission_.kind = ActiveMissionKind::None;

                char sub[64];
                std::snprintf(sub, sizeof(sub), "REWARD +$%d", totalReward);
                pushAudio(AudioEventType::MissionComplete, 1.0f);
                pushBanner("MISSION PASSED!", "미션 성공! (MISSION PASSED)",
                           sub, sub, {255, 214, 52, 255}, 4.5f);
            }
        }
        return;
    }

    if (mission_.kind == ActiveMissionKind::StreetSprint) {
        const Vec2 pPos = playerWorldPos();
        if ((pPos - mission_.currentObjectivePos).length() <= mission_.objectiveRadius) {
            mission_.checkpointsCleared += 1;
            pushAudio(AudioEventType::CashPickup, 0.95f);

            if (mission_.checkpointsCleared >= mission_.totalCheckpoints) {
                player_.cash += mission_.rewardCash;
                mission_.completedCount += 1;
                const int won = mission_.rewardCash;
                mission_.kind = ActiveMissionKind::None;

                char sub[64];
                std::snprintf(sub, sizeof(sub), "REWARD +$%d", won);
                pushAudio(AudioEventType::MissionComplete, 1.0f);
                pushBanner("MISSION PASSED!", "스트리트 레이스 우승!",
                           sub, sub, {112, 255, 118, 255}, 4.5f);
            } else {
                mission_.timeRemaining += 8.0f;
                mission_.currentObjectivePos = raceCheckpoints_[static_cast<std::size_t>(mission_.checkpointsCleared)];
                char objEn[96];
                char objKo[128];
                std::snprintf(objEn, sizeof(objEn), "Hit Checkpoint %d / %d (+8s bonus!)",
                              mission_.checkpointsCleared + 1, mission_.totalCheckpoints);
                std::snprintf(objKo, sizeof(objKo), "체크포인트 %d / %d 지점을 통과하세요! (+8초 추가)",
                              mission_.checkpointsCleared + 1, mission_.totalCheckpoints);
                mission_.objectiveEn = objEn;
                mission_.objectiveKo = objKo;
            }
        }
        return;
    }

    if (mission_.kind == ActiveMissionKind::GangRaid) {
        if (mission_.stage == 0) {
            int aliveTargets = 0;
            Vec2 lastPos = mission_.currentObjectivePos;
            for (const auto& n : npcs_) {
                if (n.missionTarget && n.alive) {
                    ++aliveTargets;
                    lastPos = n.pos;
                }
            }
            mission_.enemiesRemaining = aliveTargets;
            if (aliveTargets > 0) {
                mission_.currentObjectivePos = lastPos;
                char objEn[96];
                char objKo[128];
                std::snprintf(objEn, sizeof(objEn), "Eliminate Syndicate Enforcers (%d left)", aliveTargets);
                std::snprintf(objKo, sizeof(objKo), "신디케이트 조직원을 소탕하세요 (%d명 남음)", aliveTargets);
                mission_.objectiveEn = objEn;
                mission_.objectiveKo = objKo;
            } else {
                mission_.stage = 1;
                const Vec2 casePos{840.0f, 1176.0f};
                pickups_.push_back(WorldPickup{PickupKind::MissionBriefcase, casePos, mission_.rewardCash, 0.0f, false});
                mission_.currentObjectivePos = casePos;
                mission_.objectiveRadius = 2.6f;
                mission_.objectiveEn = "Collect the Contraband Cash Briefcase!";
                mission_.objectiveKo = "드랍된 신디케이트 현금 가방을 회수하세요!";
            }
        } else if (mission_.stage == 1) {
            bool briefcaseStillThere = false;
            for (const auto& pk : pickups_) {
                if (pk.kind == PickupKind::MissionBriefcase && !pk.collected) {
                    briefcaseStillThere = true;
                    break;
                }
            }
            if (!briefcaseStillThere) {
                player_.cash += mission_.rewardCash;
                mission_.completedCount += 1;
                const int won = mission_.rewardCash;
                mission_.kind = ActiveMissionKind::None;

                char sub[64];
                std::snprintf(sub, sizeof(sub), "REWARD +$%d", won);
                pushAudio(AudioEventType::MissionComplete, 1.0f);
                pushBanner("MISSION PASSED!", "항구 갱단 소탕 완수!",
                           sub, sub, {255, 168, 52, 255}, 4.5f);
            }
        }
    }
}

void GameWorld::updateEffects(float dt) {
    for (auto& tr : tracers_) {
        tr.ttl -= dt;
    }
    tracers_.erase(
        std::remove_if(tracers_.begin(), tracers_.end(), [](const BulletTracer& t) { return t.ttl <= 0.0f; }),
        tracers_.end());

    for (auto& p : particles_) {
        p.ttl -= dt;
        p.pos = p.pos + p.vel * dt;
        if (p.kind == ParticleKind::BrassCasing || p.kind == ParticleKind::ShotgunShell || p.kind == ParticleKind::DroppedMag) {
            p.vel.y -= 9.8f * dt;
            p.spinAngle += p.spinSpeed * dt;
            const float restH = surfaceHeightAt({p.pos.x, p.pos.z}) +
                                ((p.kind == ParticleKind::DroppedMag) ? 0.045f : 0.03f);
            if (p.pos.y < restH) {
                p.pos.y = restH;
                p.vel.y = (std::abs(p.vel.y) > 0.65f) ? (-p.vel.y * 0.32f) : 0.0f;
                p.vel.x *= 0.68f;
                p.vel.z *= 0.68f;
                p.spinSpeed *= 0.52f;
            }
        } else if (p.kind == ParticleKind::WaterSpray) {
            p.vel.y -= 2.4f * dt;
            p.vel.x *= std::max(0.0f, 1.0f - 2.6f * dt);
            p.vel.z *= std::max(0.0f, 1.0f - 2.6f * dt);
            p.size += 0.55f * dt;
            const float floorH = surfaceHeightAt({p.pos.x, p.pos.z}) + 0.04f;
            if (p.pos.y < floorH) p.pos.y = floorH;
        } else {
            p.vel.y -= 5.2f * dt;
            const float floorH = surfaceHeightAt({p.pos.x, p.pos.z}) + 0.05f;
            if (p.pos.y < floorH) {
                p.pos.y = floorH;
                p.vel.y *= -0.25f;
            }
        }
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.ttl <= 0.0f; }),
        particles_.end());
}

void GameWorld::pushBanner(const std::string& titleEn, const std::string& titleKo,
                           const std::string& subEn, const std::string& subKo,
                           ColorRGBA accent, float duration) {
    banner_.titleEn = titleEn;
    banner_.titleKo = titleKo;
    banner_.subEn = subEn;
    banner_.subKo = subKo;
    banner_.accent = accent;
    banner_.timer = duration;
}

void GameWorld::emitParticles(const Vec3& origin, int count, ColorRGBA color, float speed, float size, float ttl) {
    if (particles_.size() > 750) return;
    for (int i = 0; i < count; ++i) {
        const float yaw = rng_.range(0.0f, kTwoPi);
        const float horiz = rng_.range(0.25f, 1.0f) * speed;
        Particle p{};
        p.pos = origin;
        p.vel = {std::cos(yaw) * horiz, rng_.range(0.35f, 1.05f) * speed, std::sin(yaw) * horiz};
        p.color = color;
        p.size = size * rng_.range(0.75f, 1.25f);
        p.ttl = ttl * rng_.range(0.8f, 1.2f);
        p.maxTtl = p.ttl;
        particles_.push_back(p);
    }
}

void GameWorld::pushSkidMark(const Vec2& a, const Vec2& b, float alpha) {
    if ((a - b).lengthSq() < 0.002f) return;
    if (skidMarks_.size() >= 650) {
        skidMarks_.pop_front();
    }
    skidMarks_.push_back(SkidMark{a, b, alpha});
}

void GameWorld::pushAudio(AudioEventType type, float volume) {
    if (audioEvents_.size() < 24) {
        audioEvents_.push_back(AudioEvent{type, clampf(volume, 0.05f, 1.0f)});
    }
}

} // namespace oc
