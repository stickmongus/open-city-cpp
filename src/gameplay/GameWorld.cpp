#include "gameplay/GameWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace oc {

namespace {

constexpr std::array<VehicleSpec, static_cast<std::size_t>(VehicleKind::Count)> kVehicleSpecs = {{
    {VehicleKind::SportsCoupe, "APEX GT",      "에이펙스 GT 스포츠카", 2.28f, 1.02f, 1.12f, 44.0f, 15.0f, 27.5f, 36.0f, 2.75f, 10.2f, 2.25f, 1320.0f, 165.0f, true},
    {VehicleKind::MuscleCar,   "V8 DOMINATOR", "V8 도미네이터 머슬카", 2.36f, 1.04f, 1.18f, 40.0f, 14.0f, 26.0f, 32.0f, 2.65f, 8.4f,  1.85f, 1520.0f, 185.0f, false},
    {VehicleKind::SUV,         "TITAN 4X4",    "타이탄 4X4 SUV",       2.48f, 1.12f, 1.54f, 34.5f, 13.0f, 21.0f, 30.0f, 2.35f, 9.2f,  2.50f, 2150.0f, 250.0f, false},
    {VehicleKind::Taxi,        "METRO CAB",    "메트로 시티 택시",     2.24f, 1.00f, 1.26f, 35.5f, 13.5f, 22.0f, 32.0f, 2.60f, 9.4f,  2.20f, 1450.0f, 175.0f, false},
    {VehicleKind::Compact,     "COMET HATCH",  "코멧 해치백",          1.96f, 0.95f, 1.22f, 34.0f, 13.5f, 23.0f, 33.0f, 2.95f, 9.8f,  2.15f, 1180.0f, 145.0f, false},
    {VehicleKind::DeliveryVan, "CARGO RUNNER", "카고 러너 밴",         2.68f, 1.14f, 1.86f, 29.5f, 11.0f, 17.5f, 28.0f, 2.05f, 8.8f,  2.75f, 2550.0f, 290.0f, false}
}};

constexpr std::array<WeaponSpec, static_cast<std::size_t>(WeaponId::Count)> kWeaponSpecs = {{
    {WeaponId::Fists,        "Fists",         "맨주먹 격투", 0.36f,  2.4f,  28.0f, 1, 0.08f,   0, 0.0f,  false},
    {WeaponId::Pistol,       "9mm Pistol",    "9mm 권총",    0.22f, 68.0f,  35.0f, 1, 0.010f, 12, 1.05f, false},
    {WeaponId::SMG,          "Tactical SMG",  "전술 기관단총", 0.085f, 58.0f, 23.0f, 1, 0.030f, 30, 1.25f, true},
    {WeaponId::Shotgun,      "Pump Shotgun",  "펌프 샷건",   0.68f, 36.0f,  20.0f, 7, 0.095f,  8, 1.55f, false},
    {WeaponId::AssaultRifle, "Carbine Rifle", "돌격 카빈소총", 0.105f, 88.0f, 32.0f, 1, 0.015f, 30, 1.40f, true}
}};

ColorRGBA randomTrafficPaint(Rng& rng, VehicleKind kind) {
    if (kind == VehicleKind::Taxi) {
        return {244, 196, 36, 255};
    }
    if (kind == VehicleKind::DeliveryVan) {
        return rng.chance(0.5f) ? ColorRGBA{228, 232, 238, 255} : ColorRGBA{68, 108, 164, 255};
    }
    static constexpr std::array<ColorRGBA, 9> kPalette = {{
        {218, 48, 56, 255},
        {46, 118, 222, 255},
        {236, 238, 242, 255},
        {42, 46, 54, 255},
        {44, 178, 116, 255},
        {238, 126, 38, 255},
        {132, 72, 212, 255},
        {172, 180, 192, 255},
        {28, 168, 196, 255}
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

GameWorld::GameWorld(std::uint64_t seed)
    : seed_(seed), rng_(seed), city_() {
    city_.generate(seed_);
    reset();
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
    player_.pos = {14.5f, 14.5f};
    player_.facingYaw = -kHalfPi;
    player_.cash = 750;
    player_.health = 100.0f;
    player_.armor = 25.0f;
    player_.currentWeapon = WeaponId::Pistol;

    player_.weapons[static_cast<std::size_t>(WeaponId::Fists)] = {true, 0, 0};
    player_.weapons[static_cast<std::size_t>(WeaponId::Pistol)] = {true, 12, 72};
    player_.weapons[static_cast<std::size_t>(WeaponId::SMG)] = {false, 0, 0};
    player_.weapons[static_cast<std::size_t>(WeaponId::Shotgun)] = {false, 0, 0};
    player_.weapons[static_cast<std::size_t>(WeaponId::AssaultRifle)] = {false, 0, 0};

    mission_ = MissionState{};
    raceCheckpoints_.clear();
    banner_ = BannerNotification{};
    activePoi_ = nullptr;
    timeOfDayHours_ = 10.0f;
    trafficSignalTimer_ = 0.0f;

    spawnInitialWorld();
    pushBanner("WELCOME TO SUNSET BAY", "선셋 베이 시티에 오신 것을 환영합니다",
               "Steal rides [F], drift [Space], visit shops [B], or hit missions!",
               "[F] 차량 탑승 · [Space] 드리프트 · [B] 상점/튜닝 · 미션 마커 방문!",
               {92, 224, 255, 255}, 4.8f);
}

void GameWorld::spawnParkedShowroomCars() {
    auto addParked = [&](VehicleKind kind, Vec2 pos, float heading, ColorRGBA paint) {
        const auto& spec = getVehicleSpec(kind);
        Vehicle v{};
        v.id = nextVehicleId_++;
        v.kind = kind;
        v.pos = pos;
        v.heading = heading;
        v.health = spec.maxHealth;
        v.maxHealth = spec.maxHealth;
        v.paintColor = paint;
        v.isParked = true;
        v.hasNpcDriver = false;
        vehicles_.push_back(v);
    };

    // Starter exotic & muscle cars parked right along Central Crossroads curb
    addParked(VehicleKind::SportsCoupe, {22.5f, 7.2f}, 0.0f, {228, 42, 58, 255});
    addParked(VehicleKind::MuscleCar,   {7.2f, 24.5f}, kHalfPi, {38, 108, 232, 255});
    addParked(VehicleKind::SUV,         {-23.0f, -7.2f}, kPi, {44, 48, 56, 255});
    addParked(VehicleKind::SportsCoupe, {-14.5f, 76.0f}, -kHalfPi, {42, 214, 168, 255});
    addParked(VehicleKind::MuscleCar,   {92.0f, -7.2f}, 0.0f, {245, 148, 36, 255});
}

void GameWorld::spawnInitialWorld() {
    spawnParkedShowroomCars();
    maintainTrafficPopulation();
    maintainCivilianPopulation();

    // Ambient Harbor Industrial gang turf guards & bonus cash pickups
    const std::array<Vec2, 4> turfSpots = {{
        {210.0f, 210.0f},
        {216.0f, 206.0f},
        {292.0f, 212.0f},
        {212.0f, 292.0f}
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
        g.outfitTop = {175, 36, 48, 255};
        g.outfitBottom = {28, 30, 36, 255};
        npcs_.push_back(g);
    }

    pickups_.push_back(WorldPickup{PickupKind::CashBundle, {-42.0f, -42.0f}, 250, 0.0f, false});
    pickups_.push_back(WorldPickup{PickupKind::HealthKit, {24.0f, 20.0f}, 50, 1.2f, false});
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
    // Smooth cosine curve peaking at 13:00 (1.0) and dipping to 0.14 at 01:00
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

bool GameWorld::isPlayerInWeaponShopZone() const {
    const auto* poi = city_.findPoi(PoiKind::WeaponShop);
    if (!poi || player_.drivingVehicleId >= 0) return false;
    return (player_.pos - poi->pos).length() <= poi->radius + 1.2f;
}

bool GameWorld::isPlayerInGarageZone() const {
    const auto* poi = city_.findPoi(PoiKind::TuneGarage);
    if (!poi) return false;
    return (playerWorldPos() - poi->pos).length() <= poi->radius + 2.2f;
}

const Vehicle* GameWorld::nearestEnterableVehicle(float maxRange) const {
    if (player_.drivingVehicleId >= 0) return nullptr;
    const Vehicle* best = nullptr;
    float bestDist = maxRange;
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

    // Despawn far-away non-mission AI cars
    vehicles_.erase(
        std::remove_if(vehicles_.begin(), vehicles_.end(), [&](const Vehicle& v) {
            if (v.isPlayerDriving || v.isMissionTarget) return false;
            if (v.destroyed && v.destroyedTimer > 8.0f) return true;
            if (v.isParked) return false;
            return (v.pos - center).length() > 290.0f;
        }),
        vehicles_.end());

    for (const auto& v : vehicles_) {
        if (v.hasNpcDriver && !v.destroyed) ++activeTraffic;
    }

    int attempts = 0;
    while (activeTraffic < kTargetTraffic && attempts < 45) {
        ++attempts;
        const bool horiz = rng_.chance(0.5f);
        const int roadIdx = rng_.rangeInt(1, CityMap::kRoadCount - 2);
        const int dir = rng_.chance(0.5f) ? 1 : -1;
        const float along = rng_.range(-280.0f, 280.0f);
        const float rCoord = city_.roadCoord(roadIdx);

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

        const float distPlayer = (spawnPos - center).length();
        if (distPlayer < 32.0f || distPlayer > 265.0f) continue;

        bool tooClose = false;
        for (const auto& other : vehicles_) {
            if ((other.pos - spawnPos).length() < 11.5f) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        const VehicleKind kind = static_cast<VehicleKind>(rng_.rangeInt(0, static_cast<int>(VehicleKind::Count) - 1));
        const auto& spec = getVehicleSpec(kind);
        Vehicle v{};
        v.id = nextVehicleId_++;
        v.kind = kind;
        v.pos = spawnPos;
        v.heading = heading;
        v.velocity = forwardFromHeading(heading) * 14.5f;
        v.health = spec.maxHealth;
        v.maxHealth = spec.maxHealth;
        v.paintColor = randomTrafficPaint(rng_, kind);
        v.hasNpcDriver = true;
        v.aiHorizontalRoad = horiz;
        v.aiRoadIndex = roadIdx;
        v.aiDirection = dir;
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
            return (n.pos - center).length() > 210.0f;
        }),
        npcs_.end());

    int civCount = 0;
    for (const auto& n : npcs_) {
        if (n.alive && n.role == NpcRole::Civilian) ++civCount;
    }

    int attempts = 0;
    while (civCount < kTargetCivilians && attempts < 65) {
        ++attempts;
        // Pick a pure block-front sidewalk segment (never mid-crosswalk) in lattice [1..16]
        const int blk = rng_.rangeInt(1, CityMap::kBlockCount - 2);
        const int sideLine = rng_.rangeInt(1, CityMap::kSidewalkNodeCount - 2);
        const bool horiz = rng_.chance(0.5f);
        const bool fwdOrder = rng_.chance(0.5f);

        int n0 = 2 * blk + 1;
        int n1 = 2 * blk + 2;
        if (!fwdOrder) std::swap(n0, n1);

        const int fromU = horiz ? n0 : sideLine;
        const int fromV = horiz ? sideLine : n0;
        const int toU   = horiz ? n1 : sideLine;
        const int toV   = horiz ? sideLine : n1;

        const Vec2 a{city_.sidewalkCoord(fromU), city_.sidewalkCoord(fromV)};
        const Vec2 b{city_.sidewalkCoord(toU), city_.sidewalkCoord(toV)};
        const Vec2 segDir = (b - a).normalized();
        const float edgeYaw = std::atan2(segDir.z, segDir.x);
        const Vec2 laneOffset = rightFromHeading(edgeYaw) * 0.45f;
        const float frac = rng_.range(0.15f, 0.85f);
        Vec2 p = a + (b - a) * frac + laneOffset;

        const float d = (p - center).length();
        if (d < 20.0f || d > 185.0f) continue;

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
        npc.walkSpeed = rng_.range(1.38f, 1.92f);
        npc.outfitTop = {
            static_cast<std::uint8_t>(rng_.rangeInt(70, 235)),
            static_cast<std::uint8_t>(rng_.rangeInt(70, 235)),
            static_cast<std::uint8_t>(rng_.rangeInt(85, 240)),
            255
        };
        npc.outfitBottom = {
            static_cast<std::uint8_t>(rng_.rangeInt(32, 90)),
            static_cast<std::uint8_t>(rng_.rangeInt(36, 95)),
            static_cast<std::uint8_t>(rng_.rangeInt(45, 115)),
            255
        };
        npcs_.push_back(npc);
        ++civCount;
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
        const float d = (v.pos - player_.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    if (!best) return false;

    // If an NPC is driving, pull them out onto the street and make them flee onto nearest sidewalk!
    if (best->hasNpcDriver) {
        best->hasNpcDriver = false;
        const auto& spec = getVehicleSpec(best->kind);
        const Vec2 driverSide = best->pos - rightFromHeading(best->heading) * (spec.halfWidth + 0.95f);
        const int nearU = std::clamp(city_.nearestSidewalkNodeIndex(driverSide.x), 1, CityMap::kSidewalkNodeCount - 2);
        const int nearV = std::clamp(city_.nearestSidewalkNodeIndex(driverSide.z), 1, CityMap::kSidewalkNodeCount - 2);

        NpcCharacter ejected{};
        ejected.id = nextNpcId_++;
        ejected.role = NpcRole::Civilian;
        ejected.behavior = NpcBehavior::PanicFlee;
        ejected.stateTimer = 9.0f;
        ejected.pos = driverSide;
        ejected.navFromU = nearU;
        ejected.navFromV = nearV;
        ejected.navToU = nearU;
        ejected.navToV = nearV;
        ejected.walkSpeed = 1.75f;
        ejected.facingYaw = wrapAngle(best->heading + kPi);
        ejected.outfitTop = {235, 180, 75, 255};
        npcs_.push_back(ejected);

        triggerCivilianPanicNear(best->pos, 18.0f);
    }

    best->isParked = false;
    best->isPlayerDriving = true;
    player_.drivingVehicleId = best->id;
    player_.reloadTimer = 0.0f;
    pushAudio(AudioEventType::CarDoor, 1.0f);
    return true;
}

bool GameWorld::purchaseShopItem(int optionOneBased) {
    if (isPlayerInWeaponShopZone()) {
        auto buyWeapon = [&](WeaponId wid, int addAmmo, int price, const char* labelEn, const char* labelKo) {
            if (player_.cash < price) return false;
            player_.cash -= price;
            auto& slot = player_.weapons[static_cast<std::size_t>(wid)];
            const auto& spec = getWeaponSpec(wid);
            if (!slot.owned) {
                slot.owned = true;
                slot.magAmmo = spec.magazineSize;
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
                    auto& s = player_.weapons[static_cast<std::size_t>(i)];
                    if (s.owned) {
                        s.reserveAmmo += getWeaponSpec(static_cast<WeaponId>(i)).magazineSize * 4;
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
            default:
                return false;
        }
    }

    if (isPlayerInGarageZone()) {
        Vehicle* car = findVehicleMutable(player_.drivingVehicleId);
        if (!car) {
            // Also allow tuning a car parked right next to the player inside the garage bay
            const auto* garagePoi = city_.findPoi(PoiKind::TuneGarage);
            for (auto& v : vehicles_) {
                if (!v.destroyed && garagePoi && (v.pos - garagePoi->pos).length() <= garagePoi->radius + 3.0f) {
                    car = &v;
                    break;
                }
            }
        }
        if (!car) return false;

        switch (optionOneBased) {
            case 1: {
                if (player_.cash < 120 || car->health >= car->maxHealth - 0.5f) return false;
                player_.cash -= 120;
                car->health = car->maxHealth;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("VEHICLE REPAIRED", "차량 완벽 수리 완료", "Chassis 100%", "내구도 100% 복구", {72, 232, 255, 255}, 2.2f);
                return true;
            }
            case 2: {
                if (player_.cash < 450 || car->engineUpgradeLevel >= 3) return false;
                player_.cash -= 450;
                car->engineUpgradeLevel += 1;
                car->health = car->maxHealth;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                char subEn[64];
                std::snprintf(subEn, sizeof(subEn), "Turbo Stage %d Installed!", car->engineUpgradeLevel);
                pushBanner("ENGINE UPGRADED", "터보 엔진 스테이지 업그레이드!", subEn, "최고속도 및 가속력 증가!", {72, 232, 255, 255}, 2.4f);
                return true;
            }
            case 3:
            case 4:
            case 5: {
                if (player_.cash < 100) return false;
                player_.cash -= 100;
                if (optionOneBased == 3) car->paintColor = {238, 36, 72, 255};
                if (optionOneBased == 4) car->paintColor = {32, 212, 248, 255};
                if (optionOneBased == 5) car->paintColor = {245, 192, 42, 255};
                car->health = car->maxHealth;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("CUSTOM RESPRAY", "커스텀 메탈릭 도색 완료", "Fresh Paint + Polish", "도색 및 외형 복원 완료", car->paintColor, 2.0f);
                return true;
            }
            case 6: {
                if (player_.cash < 80 || car->nitroCharge >= 0.99f) return false;
                player_.cash -= 80;
                car->nitroCharge = 1.0f;
                pushAudio(AudioEventType::ShopBuy, 1.0f);
                pushBanner("NITROUS REFILLED", "니트로 탱크 100% 충전", "Hold [Shift] to Boost", "주행 중 [Shift] 부스트", {88, 195, 255, 255}, 2.0f);
                return true;
            }
            default:
                return false;
        }
    }

    return false;
}

bool GameWorld::startMissionFromPoi(PoiKind poiKind) {
    if (mission_.kind != ActiveMissionKind::None) return false;

    const int tierBonus = mission_.completedCount * 250;

    if (poiKind == PoiKind::MissionDelivery) {
        Vehicle exo{};
        exo.id = nextVehicleId_++;
        exo.kind = VehicleKind::SportsCoupe;
        exo.pos = {-168.0f + 7.2f, -150.0f};
        exo.heading = kHalfPi;
        exo.maxHealth = 210.0f;
        exo.health = 210.0f;
        exo.paintColor = {250, 202, 44, 255};
        exo.isParked = true;
        exo.isMissionTarget = true;
        exo.engineUpgradeLevel = 1;
        vehicles_.push_back(exo);

        // Two rival gang bodyguards watching the VIP car
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
        mission_.rewardCash = 1500 + tierBonus;
        mission_.targetVehicleId = exo.id;
        mission_.currentObjectivePos = exo.pos;
        mission_.objectiveRadius = 6.5f;
        mission_.objectiveEn = "Steal the golden VIP Apex GT in Sunset District!";
        mission_.objectiveKo = "선셋 구역의 황금색 VIP 에이펙스 GT를 탈취하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("VIP EXOTIC DELIVERY", "미션 시작: 슈퍼카 탈취 & 배달",
                   "Steal the marked Golden Apex GT!", "미니맵의 노란 마커 차량을 탈취하세요!",
                   {255, 214, 52, 255}, 3.5f);
        return true;
    }

    if (poiKind == PoiKind::MissionRace) {
        // Ensure player has a car right away if they triggered on foot
        if (player_.drivingVehicleId < 0) {
            Vehicle racer{};
            racer.id = nextVehicleId_++;
            racer.kind = VehicleKind::SportsCoupe;
            racer.pos = {0.0f, -84.0f + CityMap::kLaneOffset};
            racer.heading = 0.0f;
            racer.maxHealth = 180.0f;
            racer.health = 180.0f;
            racer.paintColor = {68, 238, 118, 255};
            racer.isPlayerDriving = true;
            vehicles_.push_back(racer);
            player_.drivingVehicleId = racer.id;
        }

        raceCheckpoints_ = {
            {84.0f, -84.0f},
            {168.0f, -84.0f},
            {168.0f, 0.0f},
            {168.0f, 84.0f},
            {84.0f, 84.0f},
            {0.0f, 84.0f},
            {0.0f, 0.0f}
        };

        mission_.kind = ActiveMissionKind::StreetSprint;
        mission_.stage = 0;
        mission_.timeRemaining = 58.0f;
        mission_.rewardCash = 1800 + tierBonus;
        mission_.checkpointsCleared = 0;
        mission_.totalCheckpoints = static_cast<int>(raceCheckpoints_.size());
        mission_.currentObjectivePos = raceCheckpoints_.front();
        mission_.objectiveRadius = 9.5f;
        mission_.objectiveEn = "Hit Checkpoint 1 / 7 before time runs out!";
        mission_.objectiveKo = "제한 시간 내에 체크포인트 1 / 7 지점을 통과하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("MIDNIGHT STREET SPRINT", "미션 시작: 도심 스트리트 레이스",
                   "Hit all 7 checkpoints! +8s per gate", "7개 체크포인트를 질주하세요! (통과 시 +8초)",
                   {112, 255, 118, 255}, 3.5f);
        return true;
    }

    if (poiKind == PoiKind::MissionGangRaid) {
        const Vec2 raidCenter{252.0f, 224.0f};
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
        mission_.rewardCash = 2500 + tierBonus;
        mission_.enemiesRemaining = static_cast<int>(offsets.size());
        mission_.currentObjectivePos = raidCenter;
        mission_.objectiveRadius = 12.0f;
        mission_.objectiveEn = "Eliminate the 5 Harbor Syndicate enforcers!";
        mission_.objectiveKo = "항구 구역의 신디케이트 조직원 5명을 소탕하세요!";

        pushAudio(AudioEventType::MissionStart, 1.0f);
        pushBanner("HARBOR SYNDICATE RAID", "미션 시작: 항구 갱단 소탕",
                   "Wipe out the armed crew & secure the cash briefcase!",
                   "무장 갱단을 제압하고 현금 가방을 회수하세요!",
                   {255, 132, 48, 255}, 3.5f);
        return true;
    }

    return false;
}

void GameWorld::update(float dt, const PlayerInput& input) {
    dt = std::clamp(dt, 0.0f, 0.05f);

    // 1 game hour = 25 real seconds (~10 min full 24h cycle)
    advanceTimeOfDayHours(dt / 25.0f);
    trafficSignalTimer_ = std::fmod(trafficSignalTimer_ + dt, 24.0f);

    if (banner_.timer > 0.0f) {
        banner_.timer = std::max(0.0f, banner_.timer - dt);
    }
    if (player_.hitMarkerTimer > 0.0f) {
        player_.hitMarkerTimer = std::max(0.0f, player_.hitMarkerTimer - dt);
    }

    // Handle player wasted / hospital respawn state
    if (player_.wasted) {
        player_.crosshairOnTarget = false;
        player_.wastedTimer -= dt;
        if (player_.wastedTimer <= 0.0f) {
            player_.wasted = false;
            player_.drivingVehicleId = -1;
            player_.pos = {14.5f, 14.5f};
            player_.velocity = {0.0f, 0.0f};
            player_.health = player_.maxHealth;
            player_.cash = std::max(0, player_.cash - 100);
            pushBanner("DISCHARGED FROM MEDICAL", "병원 치료 완료 (-$100)",
                       "Back on the streets", "다시 거리로 복귀했습니다",
                       {112, 238, 148, 255}, 2.8f);
        }
        updateVehicles(dt);
        updateNpcs(dt);
        updateEffects(dt);
        return;
    }

    // Detect nearby POI (shops & mission coronas)
    activePoi_ = nullptr;
    const Vec2 pPos = playerWorldPos();
    for (const auto& poi : city_.pois()) {
        if ((pPos - poi.pos).length() <= poi.radius + 1.5f) {
            activePoi_ = &poi;
            break;
        }
    }

    // Shop purchase input
    if (input.shopBuyOption >= 1 && input.shopBuyOption <= 6) {
        purchaseShopItem(input.shopBuyOption);
    }

    // Weapon slot selection
    if (input.weaponSelectSlot >= 0 && input.weaponSelectSlot < static_cast<int>(WeaponId::Count)) {
        if (player_.weapons[static_cast<std::size_t>(input.weaponSelectSlot)].owned) {
            player_.currentWeapon = static_cast<WeaponId>(input.weaponSelectSlot);
            player_.reloadTimer = 0.0f;
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
                const int bonus = std::clamp(static_cast<int>(player_.activeDriftScore * 0.35f), 20, 600);
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
    updateEffects(dt);

    maintainTrafficPopulation();
    maintainCivilianPopulation();

    if (player_.health <= 0.0f && !player_.wasted) {
        player_.health = 0.0f;
        player_.wasted = true;
        player_.wastedTimer = 3.2f;
        player_.activeDriftScore = 0.0f;
        if (mission_.kind != ActiveMissionKind::None) {
            mission_.kind = ActiveMissionKind::None;
        }
        pushAudio(AudioEventType::MissionFail, 1.0f);
        pushBanner("WASTED", "행동 불능 (WASTED)",
                   "Emergency medics dispatched...", "응급 후송 중입니다...",
                   {242, 68, 68, 255}, 3.2f);
    }
}

void GameWorld::updatePlayerOnFoot(float dt, const PlayerInput& input) {
    const Vec2 camFwd = forwardFromHeading(input.cameraYaw);
    const Vec2 camRight = rightFromHeading(input.cameraYaw);

    Vec2 wishDir = camFwd * input.moveForward + camRight * input.moveRight;
    const float wishLen = wishDir.length();
    if (wishLen > 1e-4f) {
        wishDir = wishDir / wishLen;
    }

    bool sprinting = input.sprintOrNitro && wishLen > 0.1f && !input.aimHeld && player_.stamina > 0.05f;
    if (sprinting) {
        player_.stamina = std::max(0.0f, player_.stamina - dt * 0.24f);
    } else {
        player_.stamina = std::min(1.0f, player_.stamina + dt * 0.32f);
    }

    const float targetSpeed = (wishLen > 0.05f) ? (sprinting ? 8.8f : 5.4f) : 0.0f;
    const Vec2 targetVel = wishDir * targetSpeed;
    const float accelRate = (wishLen > 0.05f) ? 16.0f : 18.0f;
    player_.velocity.x = lerpf(player_.velocity.x, targetVel.x, dt * accelRate);
    player_.velocity.z = lerpf(player_.velocity.z, targetVel.z, dt * accelRate);

    player_.pos += player_.velocity * dt;
    city_.resolveCircleWorld(player_.pos, 0.44f);

    // Push player cleanly out of parked/stopped vehicle OBBs
    for (const auto& v : vehicles_) {
        const auto& spec = getVehicleSpec(v.kind);
        const OBB2D playerBox{player_.pos, {0.42f, 0.42f}, 0.0f};
        Vec2 mtv{};
        if (intersectOBB(playerBox, v.obb(), mtv)) {
            player_.pos += mtv;
            if (v.speed() > 11.0f) {
                const float hitDmg = v.speed() * 1.35f;
                const float absorbed = std::min(player_.armor, hitDmg * 0.6f);
                player_.armor -= absorbed;
                player_.health -= (hitDmg - absorbed);
                player_.velocity += v.velocity * 0.55f;
            }
        }
        (void)spec;
    }

    if (input.aimHeld || input.fireHeld) {
        player_.facingYaw = wrapAngle(input.aimYaw);
    } else if (player_.velocity.length() > 0.4f) {
        const float moveYaw = std::atan2(player_.velocity.z, player_.velocity.x);
        player_.facingYaw = moveTowardsAngle(player_.facingYaw, moveYaw, dt * 12.0f);
    }

    const float spd = player_.velocity.length();
    if (spd > 0.35f) {
        player_.walkAnimPhase = std::fmod(player_.walkAnimPhase + dt * spd * 1.95f, kTwoPi);
    } else {
        player_.walkAnimPhase = lerpf(player_.walkAnimPhase, 0.0f, dt * 8.0f);
    }
}

void GameWorld::updatePlayerDriving(float dt, const PlayerInput& input, Vehicle& car) {
    player_.pos = car.pos;
    player_.facingYaw = car.heading;

    if (input.hornPressed && car.hornCooldown <= 0.0f) {
        car.hornCooldown = 0.45f;
        pushAudio(AudioEventType::CarHorn, 0.9f);
        triggerCivilianPanicNear(car.pos, 11.0f);
    }

    stepVehiclePhysics(dt, car, input.moveForward, input.moveRight, input.handbrake, input.sprintOrNitro);

    if (car.isDrifting) {
        player_.activeDriftScore += car.speed() * dt * 11.5f;
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

    // 4. 3D Vehicle OBBs
    for (auto& v : vehicles_) {
        if (v.id == player_.drivingVehicleId) continue;
        const auto& vSpec = getVehicleSpec(v.kind);
        const float tVeh = intersectRayOBB3D(origin, dir, result.dist, v.obb(), 0.12f, 0.26f + vSpec.chassisHeight);
        if (tVeh >= 0.0f && tVeh < result.dist) {
            result.dist = tVeh;
            result.kind = RayHit3D::Kind::Vehicle;
            result.vehicle = &v;
            result.npc = nullptr;
            result.headshot = false;
            result.prop = nullptr;
        }
    }

    // 5. Shootable street props (hydrants, crates, cones)
    for (auto& prop : city_.streetProps()) {
        if (prop.broken || prop.kind == StreetPropKind::StreetLight) continue;
        float radius = 0.28f;
        float topY = 0.85f;
        if (prop.kind == StreetPropKind::Crate) {
            radius = 0.52f;
            topY = 0.92f;
        } else if (prop.kind == StreetPropKind::TrafficCone) {
            radius = 0.26f;
            topY = 0.70f;
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

    auto& slot = player_.weapons[static_cast<std::size_t>(player_.currentWeapon)];
    const auto& spec = getWeaponSpec(player_.currentWeapon);

    // Resolve exact 3D world point under the screen-center crosshair
    Vec3 traceOrigin{};
    Vec3 traceDir{};
    if (input.hasAimRay3D && input.aimRayDir.lengthSq() > 0.25f) {
        traceDir = input.aimRayDir.normalized();
        const Vec3 playerShoulderCenter{player_.pos.x, 1.42f, player_.pos.z};
        const float skipBehind = std::max(0.0f, (playerShoulderCenter - input.aimRayOrigin).dot(traceDir) + 0.25f);
        traceOrigin = input.aimRayOrigin + traceDir * skipBehind;
    } else {
        traceDir = directionFromYawPitch(input.aimYaw, input.aimPitch).normalized();
        const Vec2 fwd2D = forwardFromHeading(input.aimYaw);
        traceOrigin = {player_.pos.x + fwd2D.x * 0.52f, 1.38f, player_.pos.z + fwd2D.z * 0.52f};
    }

    const float previewRange = std::max(spec.range + 10.0f, 130.0f);
    const RayHit3D crosshairHit = traceWorldRay3D(traceOrigin, traceDir, previewRange);
    const Vec3 aimPoint3D = crosshairHit.point;

    const float distFromPlayer = (aimPoint3D - Vec3{player_.pos.x, 1.35f, player_.pos.z}).length();
    player_.crosshairOnTarget =
        (distFromPlayer <= spec.range + 0.8f) &&
        ((crosshairHit.kind == RayHit3D::Kind::Npc) ||
         (crosshairHit.kind == RayHit3D::Kind::Vehicle && crosshairHit.vehicle && !crosshairHit.vehicle->destroyed));

    // Align character facing & arm elevation directly toward the 3D crosshair target when aiming/firing
    if (input.aimHeld || input.fireHeld) {
        const Vec2 toTargetXZ{aimPoint3D.x - player_.pos.x, aimPoint3D.z - player_.pos.z};
        if (toTargetXZ.lengthSq() > 0.04f) {
            player_.facingYaw = std::atan2(toTargetXZ.z, toTargetXZ.x);
        } else {
            player_.facingYaw = wrapAngle(input.aimYaw);
        }
        const Vec3 fromShoulder = (aimPoint3D - Vec3{player_.pos.x, 1.34f, player_.pos.z}).normalized();
        player_.aimPitch = clampf(std::asin(clampf(fromShoulder.y, -0.98f, 0.98f)), -1.15f, 1.20f);
    } else {
        player_.aimPitch = lerpf(player_.aimPitch, clampf(input.aimPitch * 0.35f, -0.45f, 0.45f), dt * 10.0f);
    }

    if (player_.reloadTimer > 0.0f) {
        player_.reloadTimer -= dt;
        if (player_.reloadTimer <= 0.0f && spec.magazineSize > 0) {
            const int needed = spec.magazineSize - slot.magAmmo;
            const int transfer = std::min(needed, slot.reserveAmmo);
            slot.magAmmo += transfer;
            slot.reserveAmmo -= transfer;
        }
        return;
    }

    if (input.reloadPressed && spec.magazineSize > 0 &&
        slot.magAmmo < spec.magazineSize && slot.reserveAmmo > 0) {
        player_.reloadTimer = spec.reloadDuration;
        return;
    }

    if (!input.fireHeld || player_.fireCooldown > 0.0f) {
        return;
    }

    if (spec.magazineSize > 0) {
        if (slot.magAmmo <= 0) {
            if (slot.reserveAmmo > 0) {
                player_.reloadTimer = spec.reloadDuration;
            }
            return;
        }
        slot.magAmmo -= 1;
        if (slot.magAmmo == 0 && slot.reserveAmmo > 0) {
            player_.reloadTimer = spec.reloadDuration;
        }
    }

    player_.fireCooldown = spec.fireInterval;
    player_.muzzleFlashTimer = (player_.currentWeapon == WeaponId::Fists) ? 0.0f : 0.055f;

    const Vec2 right2D = rightFromHeading(player_.facingYaw);
    const Vec3 shoulder3D{player_.pos.x + right2D.x * 0.28f, 1.34f, player_.pos.z + right2D.z * 0.28f};
    const Vec3 toAim = aimPoint3D - shoulder3D;
    const float toAimLen = toAim.length();
    const Vec3 baseDir = (toAimLen > 1e-4f) ? (toAim / toAimLen) : traceDir;
    const float barrelOffset = std::min(0.68f, std::max(0.12f, toAimLen * 0.45f));
    const Vec3 muzzle3D = shoulder3D + baseDir * barrelOffset;

    fireWeapon(player_.currentWeapon, muzzle3D, aimPoint3D, input.aimHeld);
}

void GameWorld::fireWeapon(WeaponId weaponId, const Vec3& muzzle3D, const Vec3& target3D, bool precisionAim) {
    const auto& spec = getWeaponSpec(weaponId);

    switch (weaponId) {
        case WeaponId::Fists:        pushAudio(AudioEventType::MeleeSwing, 0.8f); break;
        case WeaponId::Pistol:       pushAudio(AudioEventType::PistolShot, 0.9f); break;
        case WeaponId::SMG:          pushAudio(AudioEventType::SmgShot, 0.8f); break;
        case WeaponId::Shotgun:      pushAudio(AudioEventType::ShotgunShot, 1.0f); break;
        case WeaponId::AssaultRifle: pushAudio(AudioEventType::RifleShot, 0.95f); break;
        default: break;
    }

    if (weaponId != WeaponId::Fists) {
        triggerCivilianPanicNear({muzzle3D.x, muzzle3D.z}, 34.0f);
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

    const float spreadScale = (precisionAim && spec.pellets == 1) ? 0.14f : (precisionAim ? 0.72f : 1.0f);
    const float maxSpread = spec.spreadRadians * spreadScale;

    for (int p = 0; p < spec.pellets; ++p) {
        const float jitterX = rng_.range(-maxSpread, maxSpread);
        const float jitterY = rng_.range(-maxSpread, maxSpread);
        const Vec3 shotDir = (baseDir + basisRight * jitterX + basisUp * jitterY).normalized();

        const RayHit3D hit = traceWorldRay3D(muzzle3D, shotDir, spec.range);
        const Vec3 end3D = hit.point;
        const Vec2 flatDir = Vec2{shotDir.x, shotDir.z}.normalized();

        if (weaponId != WeaponId::Fists) {
            tracers_.push_back(BulletTracer{
                muzzle3D,
                end3D,
                {255, 232, 142, 255},
                0.065f,
                0.065f
            });
        }

        if (hit.kind == RayHit3D::Kind::Npc && hit.npc) {
            const float dmg = hit.headshot ? (spec.damage * 1.85f) : spec.damage;
            damageNpc(*hit.npc, dmg, flatDir);
            player_.hitMarkerTimer = 0.16f;
            player_.hitMarkerCritical = hit.headshot;
            emitParticles(end3D, hit.headshot ? 8 : 5,
                          hit.headshot ? ColorRGBA{255, 72, 48, 255} : ColorRGBA{225, 48, 48, 255},
                          hit.headshot ? 5.4f : 4.2f, 0.14f, 0.32f);
        } else if (hit.kind == RayHit3D::Kind::Vehicle && hit.vehicle) {
            if (!hit.vehicle->destroyed) {
                hit.vehicle->health -= spec.damage;
                player_.hitMarkerTimer = 0.12f;
                player_.hitMarkerCritical = false;
                emitParticles(end3D, 6, {255, 198, 72, 255}, 6.0f, 0.15f, 0.28f);
                if (hit.vehicle->health <= 0.0f) {
                    explodeVehicle(*hit.vehicle);
                }
            }
        } else if (hit.kind == RayHit3D::Kind::Prop && hit.prop) {
            hit.prop->broken = true;
            hit.prop->brokenTimer = 0.0f;
            hit.prop->velocity = flatDir * 5.2f;
            emitParticles(end3D, 5, {215, 215, 220, 255}, 4.8f, 0.15f, 0.32f);
        } else if (hit.kind == RayHit3D::Kind::Building || hit.kind == RayHit3D::Kind::Ground) {
            if (weaponId != WeaponId::Fists) {
                emitParticles(end3D, 4, {220, 212, 185, 255}, 4.2f, 0.11f, 0.24f);
            }
        }
    }
}

void GameWorld::stepVehiclePhysics(float dt, Vehicle& v, float throttleInput, float steerInput,
                                   bool handbrake, bool nitro) {
    const auto& spec = getVehicleSpec(v.kind);
    const float upgradeMult = 1.0f + 0.09f * static_cast<float>(v.engineUpgradeLevel);

    Vec2 fwd = forwardFromHeading(v.heading);
    Vec2 right = rightFromHeading(v.heading);

    float forwardSpd = v.velocity.dot(fwd);
    float lateralSpd = v.velocity.dot(right);

    v.brakingVisual = false;
    v.nitroActive = false;

    const float maxFwd = spec.maxForwardSpeed * upgradeMult * (nitro && v.nitroCharge > 0.04f ? 1.28f : 1.0f);

    if (throttleInput > 0.05f) {
        if (forwardSpd < -0.5f) {
            forwardSpd = std::min(0.0f, forwardSpd + spec.braking * dt);
            v.brakingVisual = true;
        } else {
            float accel = spec.acceleration * upgradeMult * throttleInput;
            if (nitro && v.nitroCharge > 0.03f) {
                accel *= 1.65f;
                v.nitroCharge = std::max(0.0f, v.nitroCharge - dt * 0.38f);
                v.nitroActive = true;
            }
            forwardSpd = std::min(maxFwd, forwardSpd + accel * dt);
        }
    } else if (throttleInput < -0.05f) {
        if (forwardSpd > 1.2f) {
            forwardSpd = std::max(0.0f, forwardSpd - spec.braking * (-throttleInput) * dt);
            v.brakingVisual = true;
        } else {
            forwardSpd = std::max(-spec.maxReverseSpeed, forwardSpd + spec.acceleration * 0.68f * throttleInput * dt);
        }
    } else {
        // Natural rolling resistance
        const float drag = 4.2f * dt;
        if (std::abs(forwardSpd) <= drag) forwardSpd = 0.0f;
        else forwardSpd -= (forwardSpd > 0.0f ? drag : -drag);
    }

    if (!v.nitroActive) {
        v.nitroCharge = std::min(1.0f, v.nitroCharge + dt * 0.055f);
    }

    if (handbrake) {
        v.brakingVisual = true;
        const float hbDecel = 11.5f * dt;
        if (std::abs(forwardSpd) <= hbDecel) forwardSpd = 0.0f;
        else forwardSpd -= (forwardSpd > 0.0f ? hbDecel : -hbDecel);
    }

    // Speed-sensitive steering + reverse steering inversion + handbrake drift yaw boost
    v.steerVisual = lerpf(v.steerVisual, steerInput, dt * 10.0f);
    const float absFwd = std::abs(forwardSpd);
    if (absFwd > 0.45f) {
        const float lowSpeedFactor = clampf(absFwd / 6.5f, 0.0f, 1.0f);
        const float highSpeedDamp = 1.0f / (1.0f + absFwd * 0.018f);
        const float dirSign = (forwardSpd >= 0.0f) ? 1.0f : -1.0f;
        const float driftBoost = handbrake ? 1.38f : 1.0f;
        v.heading = wrapAngle(v.heading + steerInput * spec.turnRate * lowSpeedFactor * highSpeedDamp * driftBoost * dirSign * dt);
    }

    // Reproject onto updated heading so turning transfers momentum into lateral slip!
    fwd = forwardFromHeading(v.heading);
    right = rightFromHeading(v.heading);
    const Vec2 prevWorldVel = v.velocity;
    if (prevWorldVel.lengthSq() > 0.2f && std::abs(steerInput) > 0.05f) {
        // Blend rotated velocity with inertial velocity to induce realistic cornering/drift slip
        const Vec2 rotatedVel = fwd * forwardSpd + right * lateralSpd;
        const float slipBlend = handbrake ? 0.68f : 0.24f;
        const Vec2 mixed = rotatedVel * (1.0f - slipBlend) + prevWorldVel * slipBlend;
        forwardSpd = mixed.dot(fwd);
        lateralSpd = mixed.dot(right);
    }

    const float grip = handbrake ? spec.driftGrip : spec.normalGrip;
    lateralSpd *= std::max(0.0f, 1.0f - grip * dt);

    v.velocity = fwd * forwardSpd + right * lateralSpd;

    const Vec2 prevPos = v.pos;
    v.pos += v.velocity * dt;
    v.wheelSpin = std::fmod(v.wheelSpin + forwardSpd * dt * 2.4f, kTwoPi);

    // Visual body pitch & roll
    const float targetPitch = clampf(-throttleInput * 0.035f + (v.brakingVisual ? 0.045f : 0.0f), -0.07f, 0.07f);
    const float targetRoll = clampf(-lateralSpd * 0.014f, -0.11f, 0.11f);
    v.bodyPitch = lerpf(v.bodyPitch, targetPitch, dt * 8.0f);
    v.bodyRoll = lerpf(v.bodyRoll, targetRoll, dt * 8.0f);

    // Drift detection, skid marks & tire smoke
    const float speed = v.velocity.length();
    v.isDrifting = (speed > 7.5f) && (std::abs(lateralSpd) > 3.1f || (handbrake && std::abs(steerInput) > 0.35f && speed > 9.0f));
    if (v.isDrifting) {
        const Vec2 rearAxlePrev = prevPos - fwd * (spec.halfLength * 0.72f);
        const Vec2 rearAxleNow = v.pos - fwd * (spec.halfLength * 0.72f);
        const Vec2 wheelOff = right * (spec.halfWidth * 0.82f);
        pushSkidMark(rearAxlePrev - wheelOff, rearAxleNow - wheelOff, 0.58f);
        pushSkidMark(rearAxlePrev + wheelOff, rearAxleNow + wheelOff, 0.58f);

        if (rng_.chance(0.65f)) {
            emitParticles({rearAxleNow.x - wheelOff.x, 0.18f, rearAxleNow.z - wheelOff.z},
                          1, {225, 228, 232, 180}, 1.8f, 0.34f, 0.42f);
            emitParticles({rearAxleNow.x + wheelOff.x, 0.18f, rearAxleNow.z + wheelOff.z},
                          1, {225, 228, 232, 180}, 1.8f, 0.34f, 0.42f);
        }
    }

    // Engine damage smoke when below 42% HP
    if (!v.destroyed && v.health < v.maxHealth * 0.42f && rng_.chance(0.45f)) {
        const Vec2 hood = v.pos + fwd * (spec.halfLength * 0.68f);
        const std::uint8_t shade = (v.health < v.maxHealth * 0.2f) ? 45 : 125;
        emitParticles({hood.x, 0.85f, hood.z}, 1, {shade, shade, shade, 200}, 1.6f, 0.38f, 0.55f);
    }
}

void GameWorld::updateTrafficAi(float dt, Vehicle& v) {
    const float roadCenter = city_.roadCoord(v.aiRoadIndex);
    float desiredHeading = 0.0f;
    if (v.aiHorizontalRoad) {
        desiredHeading = (v.aiDirection > 0) ? 0.0f : kPi;
    } else {
        desiredHeading = (v.aiDirection > 0) ? kHalfPi : -kHalfPi;
    }

    // Check intersection turning
    const int nearRx = city_.nearestRoadIndex(v.pos.x);
    const int nearRz = city_.nearestRoadIndex(v.pos.z);
    const Vec2 interCenter{city_.roadCoord(nearRx), city_.roadCoord(nearRz)};
    const int interKey = nearRz * 16 + nearRx;

    if ((v.pos - interCenter).length() < 3.6f && v.lastTurnIntersectionKey != interKey) {
        const bool atCityEdge = (v.aiHorizontalRoad && ((v.aiDirection > 0 && nearRx >= CityMap::kRoadCount - 1) ||
                                                        (v.aiDirection < 0 && nearRx <= 0))) ||
                                (!v.aiHorizontalRoad && ((v.aiDirection > 0 && nearRz >= CityMap::kRoadCount - 1) ||
                                                         (v.aiDirection < 0 && nearRz <= 0)));
        if (atCityEdge || rng_.chance(0.28f)) {
            v.aiHorizontalRoad = !v.aiHorizontalRoad;
            v.aiRoadIndex = v.aiHorizontalRoad ? nearRz : nearRx;
            if (v.aiHorizontalRoad) {
                if (nearRx <= 0) v.aiDirection = 1;
                else if (nearRx >= CityMap::kRoadCount - 1) v.aiDirection = -1;
                else v.aiDirection = rng_.chance(0.5f) ? 1 : -1;
            } else {
                if (nearRz <= 0) v.aiDirection = 1;
                else if (nearRz >= CityMap::kRoadCount - 1) v.aiDirection = -1;
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
    Vec2 targetPoint = v.pos + forwardFromHeading(desiredHeading) * 10.0f;
    if (v.aiHorizontalRoad) {
        targetPoint.z = city_.roadCoord(v.aiRoadIndex) + laneRight.z * CityMap::kLaneOffset;
    } else {
        targetPoint.x = city_.roadCoord(v.aiRoadIndex) + laneRight.x * CityMap::kLaneOffset;
    }
    (void)roadCenter;

    const float toTargetYaw = std::atan2(targetPoint.z - v.pos.z, targetPoint.x - v.pos.x);
    const float headingErr = angleDiff(v.heading, toTargetYaw);
    const float steer = clampf(headingErr * 1.65f, -1.0f, 1.0f);

    // Lookahead braking for cars, red intersection signals, crossing pedestrians, or player in lane
    bool obstacleAhead = false;
    const Vec2 fwd = forwardFromHeading(v.heading);
    const Vec2 right = rightFromHeading(v.heading);

    // Obey red traffic signal before entering the intersection crosswalk
    const float aheadInter = (interCenter - v.pos).dot(fwd);
    const float sideInter = std::abs((interCenter - v.pos).dot(right));
    if (aheadInter >= 10.2f && aheadInter <= 17.5f && sideInter < 8.5f) {
        const bool greenForAxis = v.aiHorizontalRoad ? isTrafficGreenForHorizontal() : isTrafficGreenForVertical();
        if (!greenForAxis) {
            obstacleAhead = true;
        }
    }

    for (const auto& other : vehicles_) {
        if (other.id == v.id) continue;
        const Vec2 delta = other.pos - v.pos;
        const float ahead = delta.dot(fwd);
        const float side = std::abs(delta.dot(right));
        if (ahead > 1.5f && ahead < 13.5f && side < 2.5f) {
            obstacleAhead = true;
            break;
        }
    }

    // Yield to alive pedestrians crossing in front of the vehicle
    if (!obstacleAhead) {
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

    if (!obstacleAhead && player_.drivingVehicleId < 0) {
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
            v.brakingVisual = true;
        }
    } else {
        const float cruiseSpeed = 15.5f;
        throttle = (v.speed() < cruiseSpeed) ? 0.58f : 0.0f;
    }

    stepVehiclePhysics(dt, v, throttle, steer, false, false);
}

void GameWorld::updateVehicles(float dt) {
    for (auto& v : vehicles_) {
        if (v.hornCooldown > 0.0f) v.hornCooldown -= dt;

        if (v.destroyed) {
            v.destroyedTimer += dt;
            v.velocity *= std::max(0.0f, 1.0f - 3.5f * dt);
            v.pos += v.velocity * dt;
            if (v.destroyedTimer < 5.5f && rng_.chance(0.4f)) {
                emitParticles({v.pos.x, 1.0f, v.pos.z}, 1, {65, 62, 60, 210}, 2.2f, 0.48f, 0.65f);
            }
            continue;
        }

        if (v.isPlayerDriving) {
            continue; // Updated in updatePlayerDriving
        }

        if (v.hasNpcDriver) {
            updateTrafficAi(dt, v);
        } else {
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
    // 1. Vehicle vs Building static OBB collisions
    for (auto& v : vehicles_) {
        Vec2 push{};
        if (city_.resolveOBBBuildings(v.obb(), push)) {
            v.pos += push;
            const Vec2 n = push.normalized();
            const float intoWall = -v.velocity.dot(n);
            if (intoWall > 0.0f) {
                v.velocity += n * (intoWall * 1.32f);
                v.velocity *= 0.82f;
                if (intoWall > 8.5f) {
                    const float crashDmg = (intoWall - 7.0f) * 1.65f;
                    v.health -= crashDmg;
                    emitParticles({v.pos.x - n.x * 1.5f, 0.65f, v.pos.z - n.z * 1.5f},
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

    // 2. Vehicle vs Vehicle OBB collisions
    for (std::size_t i = 0; i < vehicles_.size(); ++i) {
        for (std::size_t j = i + 1; j < vehicles_.size(); ++j) {
            Vehicle& a = vehicles_[i];
            Vehicle& b = vehicles_[j];
            if ((a.pos - b.pos).lengthSq() > 42.0f) continue;

            Vec2 mtv{};
            if (!intersectOBB(a.obb(), b.obb(), mtv)) continue;

            const float mA = getVehicleSpec(a.kind).mass;
            const float mB = getVehicleSpec(b.kind).mass;
            const float invTotal = 1.0f / (mA + mB);

            a.pos += mtv * (mB * invTotal);
            b.pos -= mtv * (mA * invTotal);

            const Vec2 n = mtv.normalized();
            const Vec2 relVel = a.velocity - b.velocity;
            const float sepVel = relVel.dot(n);
            if (sepVel < 0.0f) {
                const float restitution = 0.35f;
                const float jImpulse = -(1.0f + restitution) * sepVel / (1.0f / mA + 1.0f / mB);
                a.velocity += n * (jImpulse / mA);
                b.velocity -= n * (jImpulse / mB);
                a.isParked = false;
                b.isParked = false;

                const float impactSpeed = -sepVel;
                if (impactSpeed > 7.5f) {
                    const float dmg = (impactSpeed - 6.0f) * 1.15f;
                    a.health -= dmg;
                    b.health -= dmg;
                    const Vec2 mid = (a.pos + b.pos) * 0.5f;
                    emitParticles({mid.x, 0.7f, mid.z}, 8, {255, 206, 84, 255}, 6.0f, 0.16f, 0.32f);
                    if (a.isPlayerDriving || b.isPlayerDriving) {
                        pushAudio(AudioEventType::CarCrash, clampf(impactSpeed / 22.0f, 0.3f, 1.0f));
                    }
                    if (!a.destroyed && a.health <= 0.0f) explodeVehicle(a);
                    if (!b.destroyed && b.health <= 0.0f) explodeVehicle(b);
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
    v.paintColor = {42, 40, 38, 255};
    v.stripeColor = {28, 28, 28, 255};

    if (v.isPlayerDriving) {
        v.isPlayerDriving = false;
        player_.drivingVehicleId = -1;
        player_.pos = v.pos - rightFromHeading(v.heading) * 2.4f;
        city_.resolveCircleWorld(player_.pos, 0.45f);
        player_.health -= 45.0f;
    }

    pushAudio(AudioEventType::Explosion, 1.0f);
    emitParticles({v.pos.x, 1.1f, v.pos.z}, 34, {255, 132, 36, 255}, 13.5f, 0.55f, 0.85f);
    emitParticles({v.pos.x, 1.6f, v.pos.z}, 20, {72, 68, 65, 235}, 8.0f, 0.65f, 1.25f);
    triggerCivilianPanicNear(v.pos, 42.0f);

    // Blast radius damage to nearby NPCs
    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        const Vec2 d = npc.pos - v.pos;
        const float len = d.length();
        if (len < 9.5f) {
            damageNpc(npc, (1.0f - len / 9.5f) * 125.0f, d.normalized());
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
    const int minIdx = 1;
    const int maxIdx = CityMap::kSidewalkNodeCount - 2; // 16
    const int curU = std::clamp(npc.navToU, minIdx, maxIdx);
    const int curV = std::clamp(npc.navToV, minIdx, maxIdx);
    const int prevU = std::clamp(npc.navFromU, minIdx, maxIdx);
    const int prevV = std::clamp(npc.navFromV, minIdx, maxIdx);

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
        if (nu < minIdx || nu > maxIdx || nv < minIdx || nv > maxIdx) continue;

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
    for (auto& npc : npcs_) {
        if (!npc.alive) continue;
        if ((npc.pos - origin).lengthSq() > rSq) continue;
        if (npc.role == NpcRole::Civilian) {
            npc.behavior = NpcBehavior::PanicFlee;
            npc.stateTimer = 8.0f;
            npc.idlePauseTimer = 0.0f;
            npc.waitingAtCrosswalk = false;

            // If walking along a sidewalk toward the danger source, turn around on the sidewalk!
            const bool onCrosswalk = CityMap::isCrosswalkEdge(npc.navFromU, npc.navToU) ||
                                     CityMap::isCrosswalkEdge(npc.navFromV, npc.navToV);
            if (!onCrosswalk && (npc.navFromU != npc.navToU || npc.navFromV != npc.navToV)) {
                const Vec2 a{city_.sidewalkCoord(npc.navFromU), city_.sidewalkCoord(npc.navFromV)};
                const Vec2 b{city_.sidewalkCoord(npc.navToU), city_.sidewalkCoord(npc.navToV)};
                const Vec2 edgeDir = (b - a).normalized();
                if (edgeDir.dot(origin - npc.pos) > 0.15f) {
                    std::swap(npc.navFromU, npc.navToU);
                    std::swap(npc.navFromV, npc.navToV);
                }
            }
        } else if (npc.role == NpcRole::Gangster) {
            npc.behavior = NpcBehavior::CombatEngage;
        }
    }
}

void GameWorld::damageNpc(NpcCharacter& npc, float dmg, const Vec2& hitDir) {
    if (!npc.alive) return;
    npc.health -= dmg;
    npc.pos += hitDir * 0.25f;
    city_.resolveCircleWorld(npc.pos, 0.42f);

    if (npc.role == NpcRole::Gangster) {
        npc.behavior = NpcBehavior::CombatEngage;
    } else {
        npc.behavior = NpcBehavior::PanicFlee;
        npc.stateTimer = 8.0f;
        npc.idlePauseTimer = 0.0f;
        npc.waitingAtCrosswalk = false;
    }

    if (npc.health <= 0.0f) {
        npc.alive = false;
        npc.health = 0.0f;
        npc.corpseTimer = 0.0f;
        const int dropCash = (npc.role == NpcRole::Gangster) ? rng_.rangeInt(90, 190) : rng_.rangeInt(20, 55);
        pickups_.push_back(WorldPickup{PickupKind::CashBundle, npc.pos, dropCash, 0.0f, false});
    }
}

void GameWorld::updateNpcs(float dt) {
    const Vec2 pPos = playerWorldPos();

    for (std::size_t idx = 0; idx < npcs_.size(); ++idx) {
        auto& npc = npcs_[idx];
        if (!npc.alive) {
            npc.corpseTimer += dt;
            continue;
        }

        // Vehicle run-over check
        for (const auto& v : vehicles_) {
            if (v.speed() < 6.5f) continue;
            if ((v.pos - npc.pos).lengthSq() > 14.0f) continue;
            const OBB2D npcBox{npc.pos, {0.42f, 0.42f}, 0.0f};
            Vec2 mtv{};
            if (intersectOBB(npcBox, v.obb(), mtv)) {
                damageNpc(npc, v.speed() * 5.2f, v.velocity.normalized());
                emitParticles({npc.pos.x, 1.0f, npc.pos.z}, 6, {225, 52, 52, 255}, 5.0f, 0.16f, 0.35f);
                triggerCivilianPanicNear(npc.pos, 22.0f);
                break;
            }
        }
        if (!npc.alive) continue;

        if (npc.role == NpcRole::Gangster) {
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
                        const float spread = rng_.range(-0.085f, 0.085f);
                        const Vec2 shotDir = forwardFromHeading(npc.facingYaw + spread);
                        const Vec2 end2D = npc.pos + shotDir * distToPlayer;
                        tracers_.push_back(BulletTracer{
                            {npc.pos.x, 1.28f, npc.pos.z},
                            {end2D.x, 1.15f, end2D.z},
                            {255, 145, 92, 255},
                            0.07f,
                            0.07f
                        });
                        pushAudio(AudioEventType::PistolShot, 0.35f);

                        // Hit check against player car or player body
                        if (Vehicle* pCar = findVehicleMutable(player_.drivingVehicleId)) {
                            if (intersectRayOBB(npc.pos, shotDir, distToPlayer + 3.0f, pCar->obb()) >= 0.0f) {
                                pCar->health -= 8.0f;
                                if (pCar->health <= 0.0f) explodeVehicle(*pCar);
                            }
                        } else if (intersectRayCircle(npc.pos, shotDir, distToPlayer + 1.5f, player_.pos, 0.58f) >= 0.0f) {
                            const float shotDmg = 8.5f;
                            const float absorbed = std::min(player_.armor, shotDmg * 0.65f);
                            player_.armor -= absorbed;
                            player_.health -= (shotDmg - absorbed);
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
        } else {
            // Civilian structured sidewalk & crosswalk navigation
            const bool fleeing = (npc.behavior == NpcBehavior::PanicFlee);
            if (fleeing) {
                npc.stateTimer -= dt;
                if (npc.stateTimer <= 0.0f) {
                    npc.behavior = NpcBehavior::Stroll;
                }
            }

            const Vec2 fromNode{city_.sidewalkCoord(npc.navFromU), city_.sidewalkCoord(npc.navFromV)};
            const Vec2 toNode{city_.sidewalkCoord(npc.navToU), city_.sidewalkCoord(npc.navToV)};

            if (npc.navFromU == npc.navToU && npc.navFromV == npc.navToV) {
                const Vec2 toCorner = toNode - npc.pos;
                const float distCorner = toCorner.length();
                if (distCorner < 0.85f) {
                    chooseNextSidewalkTarget(npc, fleeing, pPos);
                } else {
                    const Vec2 dir = toCorner / distCorner;
                    npc.facingYaw = moveTowardsAngle(npc.facingYaw, std::atan2(dir.z, dir.x), dt * 8.0f);
                    npc.velocity = dir * (fleeing ? 4.8f : npc.walkSpeed);
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
                    chooseNextSidewalkTarget(npc, fleeing, pPos);
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
                if (!fleeing && curIsCrosswalk && curProgress < 1.35f) {
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
                } else if (!fleeing && !curIsCrosswalk && npc.idlePauseTimer > 0.0f) {
                    npc.idlePauseTimer = std::max(0.0f, npc.idlePauseTimer - dt);
                    npc.velocity = {0.0f, 0.0f};
                } else {
                    const float clampedS = clampf(curProgress, 0.0f, curLen);
                    const float lookS = std::min(curLen, clampedS + 2.2f);
                    const Vec2 steerPoint = curLaneStart + curDir * lookS;
                    const Vec2 desiredDir = (steerPoint - npc.pos).normalized();

                    float moveSpd = npc.walkSpeed;
                    if (fleeing) {
                        moveSpd = 4.75f;
                    } else if (curIsCrosswalk) {
                        moveSpd = npc.walkSpeed * 1.28f; // Brisk crosswalk pace
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

        const float spd = npc.velocity.length();
        if (spd > 0.2f) {
            npc.animPhase = std::fmod(npc.animPhase + dt * spd * 2.2f, kTwoPi);
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

    // Destructible street props (hydrants spray water geysers when knocked over!)
    for (auto& prop : city_.streetProps()) {
        if (prop.broken) {
            prop.brokenTimer += dt;
            prop.pos += prop.velocity * dt;
            prop.velocity *= std::max(0.0f, 1.0f - 4.0f * dt);
            if (prop.kind == StreetPropKind::Hydrant && prop.brokenTimer < 4.5f && rng_.chance(0.55f)) {
                emitParticles({prop.pos.x, 0.5f, prop.pos.z}, 1, {120, 215, 255, 220}, 6.5f, 0.22f, 0.45f);
            }
            continue;
        }

        for (const auto& v : vehicles_) {
            if (v.speed() < 4.5f) continue;
            if ((v.pos - prop.pos).lengthSq() < 6.2f) {
                prop.broken = true;
                prop.brokenTimer = 0.0f;
                prop.velocity = v.velocity * 0.7f;
                emitParticles({prop.pos.x, 0.7f, prop.pos.z}, 5, {215, 215, 220, 255}, 4.5f, 0.16f, 0.35f);
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
                mission_.currentObjectivePos = {252.0f, 168.0f + 7.0f}; // Harbor pier drop-off
                mission_.objectiveRadius = 7.5f;
                mission_.objectiveEn = "Deliver the VIP Apex GT to Harbor Drop-Off!";
                mission_.objectiveKo = "탈취한 VIP 슈퍼카를 항구 인도 지점까지 배달하세요!";
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
                const Vec2 casePos{252.0f, 224.0f};
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
        p.vel.y -= 5.2f * dt;
        if (p.pos.y < 0.05f) {
            p.pos.y = 0.05f;
            p.vel.y *= -0.25f;
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
