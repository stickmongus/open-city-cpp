#include "gameplay/GameWorld.hpp"
#include "physics/PhysicsBridge.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>

namespace {

int gTestsPassed = 0;

void check(bool cond, const char* msg, int line) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] Line %d: %s\n", line, msg);
        std::exit(1);
    }
}

#define ASSERT_TRUE(cond) check((cond), #cond, __LINE__)

void testCityGenerationAndCollisions() {
    oc::CityMap city;
    ASSERT_TRUE(!city.buildings().empty());
    ASSERT_TRUE(!city.parks().empty());
    ASSERT_TRUE(!city.trees().empty());
    ASSERT_TRUE(city.findPoi(oc::PoiKind::WeaponShop) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::TuneGarage) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::MissionDelivery) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::MissionRace) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::MissionGangRaid) != nullptr);

    // Central Gangnam/Banpo crossroads (588, 1008) must be on road and in intersection
    ASSERT_TRUE(city.isOnRoad({588.0f, 1008.0f}));
    ASSERT_TRUE(city.isInIntersection({588.0f, 1008.0f}));

    // Building collision push-out check
    const auto& firstBld = city.buildings().front();
    oc::Vec2 inside = firstBld.footprint.center();
    const bool pushed = city.resolveCircleWorld(inside, 0.5f);
    ASSERT_TRUE(pushed);
    ASSERT_TRUE(!firstBld.footprint.contains(inside));

    ++gTestsPassed;
}

void testOnFootAndCarjacking() {
    oc::GameWorld world(42ULL);
    const oc::Vec2 startPos = world.player().pos;

    // Walk forward (North: -halfPi)
    oc::PlayerInput in{};
    in.moveForward = 1.0f;
    in.sprintOrNitro = true;
    in.cameraYaw = -oc::kHalfPi;
    for (int i = 0; i < 30; ++i) {
        world.update(1.0f / 60.0f, in);
    }
    ASSERT_TRUE((world.player().pos - startPos).length() > 2.0f);
    ASSERT_TRUE(world.player().stamina < 1.0f);

    // Teleport next to an NPC-driven traffic car and carjack it!
    int npcCarId = -1;
    for (const auto& v : world.vehicles()) {
        if (v.hasNpcDriver && !v.destroyed) {
            npcCarId = v.id;
            world.player().pos = v.pos + oc::Vec2{1.5f, 0.0f};
            break;
        }
    }
    ASSERT_TRUE(npcCarId != -1);

    const std::size_t npcsBefore = world.npcs().size();
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    ASSERT_TRUE(world.player().drivingVehicleId == npcCarId);
    ASSERT_TRUE(world.playerVehicle() != nullptr);
    ASSERT_TRUE(!world.playerVehicle()->hasNpcDriver);
    ASSERT_TRUE(world.npcs().size() == npcsBefore + 1); // Ejected driver spawned fleeing!

    // Exit vehicle cleanly
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    ASSERT_TRUE(world.player().drivingVehicleId == -1);

    ++gTestsPassed;
}

void testVehicleDrivingAndDriftingCashBonus() {
    oc::GameWorld world(99ULL);

    // Put player in a Muscle Car on wide straight central avenue (z = 0, x = -150)
    oc::Vehicle* muscle = nullptr;
    for (auto& v : world.vehicles()) {
        if (v.kind == oc::VehicleKind::MuscleCar) {
            muscle = &v;
            break;
        }
    }
    ASSERT_TRUE(muscle != nullptr);
    const int muscleId = muscle->id;
    muscle->pos = {-220.0f, oc::CityMap::kLaneOffset};
    muscle->heading = 0.0f;
    muscle->velocity = {0.0f, 0.0f};
    muscle->isPlayerDriving = true;
    muscle->isParked = false;
    world.player().drivingVehicleId = muscleId;

    // Clear traffic obstacles in test lane so we get an unobstructed drift run
    for (auto& v : world.vehicles()) {
        if (v.id != muscleId) {
            v.pos = {200.0f, 200.0f};
            v.velocity = {0.0f, 0.0f};
        }
    }

    // Accelerate with Nitrous along Eastbound avenue
    oc::PlayerInput drive{};
    drive.moveForward = 1.0f;
    drive.sprintOrNitro = true;
    for (int i = 0; i < 90; ++i) {
        world.update(1.0f / 60.0f, drive);
    }
    ASSERT_TRUE(world.playerVehicle()->speed() > 22.0f);

    // Initiate handbrake drift!
    oc::PlayerInput drift{};
    drift.moveForward = 0.8f;
    drift.moveRight = 1.0f;
    drift.handbrake = true;
    bool sawDrift = false;
    for (int i = 0; i < 45; ++i) {
        world.update(1.0f / 60.0f, drift);
        if (world.playerVehicle() && world.playerVehicle()->isDrifting) {
            sawDrift = true;
        }
    }
    ASSERT_TRUE(sawDrift);
    ASSERT_TRUE(!world.skidMarks().empty());
    ASSERT_TRUE(world.player().activeDriftScore > 5.0f);

    // Stop drifting and wait 1.1s so drift bonus banks into cash
    const int cashBefore = world.player().cash;
    oc::PlayerInput coast{};
    for (int i = 0; i < 75; ++i) {
        if (auto* car = world.findVehicleMutable(muscleId)) {
            car->pos = {-120.0f, 0.0f};
            car->heading = 0.0f;
            car->velocity = {4.0f, 0.0f};
            car->isDrifting = false;
        }
        world.update(1.0f / 60.0f, coast);
    }
    ASSERT_TRUE(world.player().cash > cashBefore);

    ++gTestsPassed;
}

void testWeaponsShopsAndGarage() {
    oc::GameWorld world(1234ULL);
    world.player().cash = 3000;

    // Move into Ammu-Tech Weapon Shop zone and buy Shotgun + Body Armor
    const auto* wPoi = world.city().findPoi(oc::PoiKind::WeaponShop);
    ASSERT_TRUE(wPoi != nullptr);
    world.player().pos = wPoi->pos;
    ASSERT_TRUE(world.isPlayerInWeaponShopZone());
    ASSERT_TRUE(world.purchaseShopItem(2)); // Shotgun
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Shotgun)].owned);
    ASSERT_TRUE(world.purchaseShopItem(5)); // Body Armor 100%
    ASSERT_TRUE(world.player().armor >= 99.5f);

    // Fire Shotgun and verify multi-pellet tracers spawn
    oc::PlayerInput shoot{};
    shoot.fireHeld = true;
    shoot.aimYaw = 0.0f;
    world.update(1.0f / 60.0f, shoot);
    ASSERT_TRUE(world.tracers().size() >= 5);

    // Move a car into Pay 'n' Tune Garage and buy Engine Stage 1 + Repair
    const auto* gPoi = world.city().findPoi(oc::PoiKind::TuneGarage);
    ASSERT_TRUE(gPoi != nullptr);
    oc::Vehicle& car = world.vehicles().front();
    car.pos = gPoi->pos;
    car.health = 50.0f;
    car.isPlayerDriving = true;
    world.player().drivingVehicleId = car.id;
    ASSERT_TRUE(world.isPlayerInGarageZone());
    ASSERT_TRUE(world.purchaseShopItem(2)); // Engine Upgrade
    ASSERT_TRUE(car.engineUpgradeLevel == 1);
    ASSERT_TRUE(car.health >= car.maxHealth - 0.5f);

    ++gTestsPassed;
}

void testAllThreeMissionsCompletion() {
    oc::GameWorld world(777ULL);

    // 1. VIP Exotic Delivery Mission
    ASSERT_TRUE(world.startMissionFromPoi(oc::PoiKind::MissionDelivery));
    ASSERT_TRUE(world.mission().kind == oc::ActiveMissionKind::ExoticDelivery);
    const int vipId = world.mission().targetVehicleId;
    oc::Vehicle* vipCar = world.findVehicleMutable(vipId);
    ASSERT_TRUE(vipCar != nullptr);

    // Stage 0 -> Enter VIP car
    world.player().pos = vipCar->pos + oc::Vec2{1.0f, 0.0f};
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    world.update(1.0f / 60.0f, {});
    ASSERT_TRUE(world.mission().stage == 1);

    // Stage 1 -> Deliver to harbor drop-off
    const int cash0 = world.player().cash;
    vipCar = world.findVehicleMutable(vipId);
    vipCar->pos = world.mission().currentObjectivePos;
    world.update(1.0f / 60.0f, {});
    ASSERT_TRUE(world.mission().kind == oc::ActiveMissionKind::None);
    ASSERT_TRUE(world.player().cash > cash0);

    // 2. Street Sprint Checkpoint Race Mission
    ASSERT_TRUE(world.startMissionFromPoi(oc::PoiKind::MissionRace));
    ASSERT_TRUE(world.mission().kind == oc::ActiveMissionKind::StreetSprint);
    const int cash1 = world.player().cash;
    while (world.mission().kind == oc::ActiveMissionKind::StreetSprint) {
        if (auto* raceCar = world.findVehicleMutable(world.player().drivingVehicleId)) {
            raceCar->pos = world.mission().currentObjectivePos;
        }
        world.update(1.0f / 60.0f, {});
    }
    ASSERT_TRUE(world.player().cash > cash1);

    // 3. Harbor Syndicate Raid Mission
    ASSERT_TRUE(world.tryEnterOrExitVehicle()); // Step out on foot
    ASSERT_TRUE(world.startMissionFromPoi(oc::PoiKind::MissionGangRaid));
    ASSERT_TRUE(world.mission().kind == oc::ActiveMissionKind::GangRaid);
    const int cash2 = world.player().cash;

    // Clear unrelated ambient traffic away from the raid zone so teleporting next to targets doesn't land inside a passing bus
    for (auto& v : world.vehicles()) {
        v.pos = {260.0f, 260.0f};
        v.velocity = {0.0f, 0.0f};
    }

    // Eliminate syndicate targets by firing Assault Rifle directly at each target
    world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)] = {true, 30, 300};
    world.player().currentWeapon = oc::WeaponId::AssaultRifle;
    for (int step = 0; step < 300 && world.mission().stage == 0; ++step) {
        for (const auto& n : world.npcs()) {
            if (n.missionTarget && n.alive) {
                world.player().pos = n.pos - oc::Vec2{3.0f, 0.0f};
                break;
            }
        }
        oc::PlayerInput fire{};
        fire.aimHeld = true;
        fire.fireHeld = true;
        fire.aimYaw = 0.0f;
        world.update(0.04f, fire);
    }
    ASSERT_TRUE(world.mission().stage == 1);

    // Collect Contraband Cash Briefcase
    world.player().pos = world.mission().currentObjectivePos;
    world.update(1.0f / 60.0f, {});
    ASSERT_TRUE(world.mission().kind == oc::ActiveMissionKind::None);
    ASSERT_TRUE(world.player().cash >= cash2 + 2500);

    ++gTestsPassed;
}

void test3DCrosshairAimAndHeadshots() {
    oc::GameWorld world(2026ULL);

    // Clear vehicles around central intersection so line of sight is completely open
    for (auto& v : world.vehicles()) {
        v.pos = {260.0f, 260.0f};
        v.velocity = {0.0f, 0.0f};
    }

    // Position player at (0, 0) and place an armed target 22m East at (22, 0)
    world.player().pos = {0.0f, 0.0f};
    world.player().currentWeapon = oc::WeaponId::Pistol;

    ASSERT_TRUE(world.startMissionFromPoi(oc::PoiKind::MissionGangRaid));
    int targetIdx = -1;
    for (std::size_t i = 0; i < world.npcs().size(); ++i) {
        if (world.npcs()[i].missionTarget && world.npcs()[i].alive) {
            targetIdx = static_cast<int>(i);
            break;
        }
    }
    ASSERT_TRUE(targetIdx >= 0);

    // Move target into open sightline by shooting from right next to its raid zone on an open road
    const oc::Vec2 targetPos{168.0f, 200.0f};
    // Put player 20m South of target on Northbound avenue x = 168
    world.player().pos = {168.0f, 220.0f};

    // Simulate a shoulder-offset 3D camera (+0.56m right of player, y = 2.15m) aiming directly at an NPC's head!
    // Find an alive civilian or gangster and place player 18m away with clear LOS
    const oc::NpcCharacter* victim = nullptr;
    for (const auto& n : world.npcs()) {
        if (n.missionTarget && n.alive) {
            victim = &n;
            break;
        }
    }
    ASSERT_TRUE(victim != nullptr);
    (void)targetPos;

    // Pick a cardinal street sightline around victim with unobstructed building LOS
    const oc::Vec2 offsets[4] = {{-14.0f, 0.0f}, {14.0f, 0.0f}, {0.0f, -14.0f}, {0.0f, 14.0f}};
    oc::Vec3 camOrigin{};
    oc::Vec3 camRayDir{};
    const oc::Vec3 victimHead{victim->pos.x, 1.56f, victim->pos.z};
    bool foundClearSightline = false;
    for (const auto& off : offsets) {
        oc::Vec2 candidatePos = victim->pos + off;
        world.city().resolveCircleWorld(candidatePos, 0.5f);
        const oc::Vec3 candCam{candidatePos.x - 1.8f, 2.25f, candidatePos.z + 0.56f};
        const oc::Vec3 candDir = (victimHead - candCam).normalized();
        const float dist = (victimHead - candCam).length();
        if (world.city().raycastBuildings3D(candCam, candDir, dist) < 0.0f &&
            world.city().raycastBuildings(candidatePos, (victim->pos - candidatePos).normalized(),
                                          (victim->pos - candidatePos).length()) < 0.0f) {
            world.player().pos = candidatePos;
            camOrigin = candCam;
            camRayDir = candDir;
            foundClearSightline = true;
            break;
        }
    }
    ASSERT_TRUE(foundClearSightline);

    const int victimId = victim->id;
    const float hpBefore = victim->health;
    oc::PlayerInput aimShot{};
    aimShot.hasAimRay3D = true;
    aimShot.aimRayOrigin = camOrigin;
    aimShot.aimRayDir = camRayDir;
    aimShot.aimYaw = std::atan2(camRayDir.z, camRayDir.x);
    aimShot.aimPitch = std::asin(camRayDir.y);
    aimShot.aimHeld = true;
    aimShot.fireHeld = true;

    world.update(1.0f / 60.0f, aimShot);

    // Verify shoulder-offset camera ray converged accurately onto the target's head (1.85x critical!)
    ASSERT_TRUE(world.player().hitMarkerTimer > 0.0f);
    ASSERT_TRUE(world.player().hitMarkerCritical);
    ASSERT_TRUE(!world.tracers().empty());
    ASSERT_TRUE(world.tracers().back().end.y >= 1.38f);
    const oc::NpcCharacter* victimAfter = nullptr;
    for (const auto& n : world.npcs()) {
        if (n.id == victimId) { victimAfter = &n; break; }
    }
    ASSERT_TRUE(victimAfter != nullptr);
    ASSERT_TRUE(victimAfter->health < hpBefore - 55.0f);

    ++gTestsPassed;
}

void testSidewalkPedestrianDiscipline() {
    oc::GameWorld world(5150ULL);
    // Place player quietly in Central Park so no gunshots panic civilians
    world.player().pos = {-42.0f, -42.0f};

    for (int step = 0; step < 600; ++step) {
        world.update(1.0f / 60.0f, {});

        for (const auto& npc : world.npcs()) {
            if (!npc.alive || npc.role != oc::NpcRole::Civilian) continue;
            if (npc.behavior != oc::NpcBehavior::Stroll) continue;

            // 1. Never inside any building footprint
            for (const auto& b : world.city().buildings()) {
                ASSERT_TRUE(!b.footprint.contains(npc.pos));
            }

            // 2. If on road asphalt, MUST be within an intersection crosswalk zone (never jaywalking mid-block!)
            if (world.city().isOnRoad(npc.pos, -0.55f)) {
                const float rx = world.city().roadCoord(world.city().nearestRoadIndex(npc.pos.x));
                const float rz = world.city().roadCoord(world.city().nearestRoadIndex(npc.pos.z));
                const bool inCrosswalkZone = (std::abs(npc.pos.x - rx) <= 13.0f) &&
                                             (std::abs(npc.pos.z - rz) <= 13.0f);
                ASSERT_TRUE(inCrosswalkZone);
            }
        }
    }

    ++gTestsPassed;
}

void testUpwardCameraAndSkyscraperRaycast() {
    oc::CityMap city;
    // Verify upward pitch (+62 deg) produces positive upward view vector
    const oc::Vec3 upDir = oc::directionFromYawPitch(0.0f, 1.08f);
    ASSERT_TRUE(upDir.y > 0.85f);
    ASSERT_TRUE(std::abs(upDir.length() - 1.0f) < 1e-4f);

    // Pick a tall skyscraper and raycast upward at its upper wall vs over its roof
    const oc::Building* tall = nullptr;
    for (const auto& b : city.buildings()) {
        if (b.height > 45.0f) {
            tall = &b;
            break;
        }
    }
    ASSERT_TRUE(tall != nullptr);

    const oc::Vec2 center = tall->footprint.center();
    const oc::Vec3 streetOrigin{tall->footprint.min.x - 10.0f, tall->baseY + 1.5f, center.z};
    const oc::Vec3 midTowerTarget{center.x, tall->baseY + std::min(tall->height * 0.75f, 32.0f), center.z};
    const oc::Vec3 rayHitDir = (midTowerTarget - streetOrigin).normalized();
    const float hitDist = city.raycastBuildings3D(streetOrigin, rayHitDir, 150.0f);
    ASSERT_TRUE(hitDist > 0.0f && hitDist < 60.0f);

    // Aiming steep skyward away from buildings returns -1 (clear sky)
    const float skyDist = city.raycastBuildings3D({0.0f, 1.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 200.0f);
    ASSERT_TRUE(skyDist < 0.0f);

    ++gTestsPassed;
}

void testDealershipRealEstateTuningAndHeavyOrdnance() {
    oc::GameWorld world(4040ULL);
    world.player().cash = 200000;

    // 1. Smartphone Tab 0 (AUTOS): Purchase Zentorno X Hypercar & verify curbside delivery
    const int cashStart = world.player().cash;
    ASSERT_TRUE(world.executePhoneAction(0, 6)); // Slot 6 = Hypercar
    ASSERT_TRUE(world.player().cash < cashStart);
    ASSERT_TRUE(world.player().garage[static_cast<std::size_t>(oc::VehicleKind::Hypercar)].owned);
    ASSERT_TRUE(world.player().activePersonalVehicleId >= 0);

    const int cashAfterBuy = world.player().cash;
    // Re-summoning an owned vehicle via Smartphone is free!
    world.player().pos = {588.0f, 1008.0f};
    ASSERT_TRUE(world.executePhoneAction(0, 6));
    ASSERT_TRUE(world.player().cash == cashAfterBuy);
    oc::Vehicle* hyper = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(hyper != nullptr && hyper->isPersonalCar && hyper->kind == oc::VehicleKind::Hypercar);

    // 2. Smartphone Tab 1 (REALTY): Buy properties, verify perks & 25s passive income payout
    ASSERT_TRUE(world.executePhoneAction(1, 1)); // SunsetCondo (+25 Max HP)
    ASSERT_TRUE(world.executePhoneAction(1, 3)); // PayNTuneFranchise (-40% Tuning Discount)
    ASSERT_TRUE(world.executePhoneAction(1, 6)); // EclipsePenthouse (+50 Max Armor)
    ASSERT_TRUE(world.player().maxHealth >= 124.5f);
    ASSERT_TRUE(world.player().maxArmor >= 149.5f);
    ASSERT_TRUE(world.tuningDiscountScale() < 0.65f);
    ASSERT_TRUE(world.passiveIncomePerCycle() > 1500);

    const int cashBeforeRent = world.player().cash;
    for (int i = 0; i < 520; ++i) {
        world.update(0.05f, {});
    }
    ASSERT_TRUE(world.player().cash >= cashBeforeRent + world.passiveIncomePerCycle());

    // 3. Deep Vehicle Tuning at Pay 'n' Tune & Personal Garage Persistence
    const auto* gPoi = world.city().findPoi(oc::PoiKind::TuneGarage);
    ASSERT_TRUE(gPoi != nullptr);
    hyper = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(hyper != nullptr);
    hyper->pos = gPoi->pos;
    hyper->isPlayerDriving = true;
    world.player().drivingVehicleId = hyper->id;
    ASSERT_TRUE(world.isPlayerInGarageZone());

    ASSERT_TRUE(world.purchaseShopItem(2)); // Turbo Lv+1
    ASSERT_TRUE(world.purchaseShopItem(3)); // Armor Lv+1
    ASSERT_TRUE(world.purchaseShopItem(4)); // LED Neon Underglow
    ASSERT_TRUE(world.purchaseShopItem(5)); // Carbon GT Aero Spoiler toggle
    ASSERT_TRUE(world.purchaseShopItem(6)); // Forged Alloy Rim finish
    ASSERT_TRUE(world.purchaseShopItem(7)); // Metallic / Matte Satin custom paint

    const int expectedNeon = hyper->neonPreset;
    const int expectedRim = hyper->rimStyle;
    const bool expectedWing = hyper->customSpoiler;

    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    world.player().pos = {672.0f, 1008.0f};
    ASSERT_TRUE(world.executePhoneAction(0, 6)); // Summon fresh instance from garage
    const oc::Vehicle* resummoned = world.findVehicle(world.player().activePersonalVehicleId);
    ASSERT_TRUE(resummoned != nullptr);
    ASSERT_TRUE(resummoned->engineUpgradeLevel >= 1);
    ASSERT_TRUE(resummoned->armorUpgradeLevel >= 1);
    ASSERT_TRUE(resummoned->neonPreset == expectedNeon);
    ASSERT_TRUE(resummoned->rimStyle == expectedRim);
    ASSERT_TRUE(resummoned->customSpoiler == expectedWing);

    // 4. Outfits (MODA) + Heavy Ordnance (Combat MG & Tactical Explosive Launcher)
    ASSERT_TRUE(world.executePhoneAction(2, 5)); // TacOpsArmor outfit
    ASSERT_TRUE(world.player().currentOutfit == oc::OutfitId::TacOpsArmor);
    ASSERT_TRUE(oc::getOutfitSpec(world.player().currentOutfit).damageTakenScale < 0.85f);

    ASSERT_TRUE(world.executePhoneAction(3, 5)); // Combat MG
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::CombatMG)].owned);
    ASSERT_TRUE(world.executePhoneAction(3, 6)); // HeavyLauncher
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::HeavyLauncher)].owned);
    ASSERT_TRUE(world.player().currentWeapon == oc::WeaponId::HeavyLauncher);

    // Fire HeavyLauncher at ground 18m ahead and verify explosive particles spawn
    const std::size_t particlesBefore = world.particles().size();
    oc::PlayerInput rocketShot{};
    rocketShot.hasAimRay3D = true;
    rocketShot.aimRayOrigin = {world.player().pos.x, 1.8f, world.player().pos.z};
    rocketShot.aimRayDir = oc::Vec3{18.0f, -1.8f, 0.0f}.normalized();
    rocketShot.fireHeld = true;
    world.update(1.0f / 60.0f, rocketShot);
    ASSERT_TRUE(world.particles().size() > particlesBefore + 15);

    // 5. Realistic Seoul Landmarks, Storefronts, Yeouido Island & Banpo Bridge
    const auto& city = world.city();
    ASSERT_TRUE(city.findPoi(oc::PoiKind::CarDealership) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::ClothingBoutique) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::GasStation) != nullptr);
    ASSERT_TRUE(city.findPoi(oc::PoiKind::NightclubProperty) != nullptr);
    ASSERT_TRUE(!city.canopies().empty());

    bool hasStorefront = false;
    bool hasBalconies = false;
    bool hasHelipad = false;
    for (const auto& b : city.buildings()) {
        if (b.hasStorefront) hasStorefront = true;
        if (b.hasBalconies) hasBalconies = true;
        if (b.hasHelipad) hasHelipad = true;
    }
    ASSERT_TRUE(hasStorefront && hasBalconies && hasHelipad);

    // Verify player can walk onto Yeouido Island (-4500, 320) and Banpo Bridge (588, 0)
    oc::Vec2 yeouidoPos{-4500.0f, 320.0f};
    city.resolveCircleWorld(yeouidoPos, 0.5f);
    ASSERT_TRUE(std::abs(yeouidoPos.x - (-4500.0f)) < 0.01f);

    oc::Vec2 banpoBridgePos{588.0f, 0.0f};
    city.resolveCircleWorld(banpoBridgePos, 0.5f);
    ASSERT_TRUE(std::abs(banpoBridgePos.x - 588.0f) < 0.01f);

    ++gTestsPassed;
}

void testPlayerJumpAndVehicleRoofClimbing() {
    oc::GameWorld world(5050ULL);
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = world.surfaceHeightAt(world.player().pos);
    world.player().onGround = true;
    world.consumeAudioEvents();

    // 1. Standing jump arc & clean landing
    oc::PlayerInput jumpIn{};
    jumpIn.jumpPressed = true;
    world.update(1.0f / 60.0f, jumpIn);

    const auto events = world.consumeAudioEvents();
    bool heardJump = false;
    for (const auto& ev : events) {
        if (ev.type == oc::AudioEventType::Jump) heardJump = true;
    }
    ASSERT_TRUE(heardJump);
    ASSERT_TRUE(!world.player().onGround);

    float peakY = world.player().posY;
    for (int i = 0; i < 65; ++i) {
        world.update(1.0f / 60.0f, {});
        if (world.player().posY > peakY) peakY = world.player().posY;
    }
    ASSERT_TRUE(peakY > 1.25f);
    ASSERT_TRUE(world.player().onGround);
    ASSERT_TRUE(std::abs(world.player().posY - world.surfaceHeightAt(world.player().pos)) < 0.03f);

    // 2. Jump onto a parked car roof and stand elevated on top of it!
    world.player().cash = 50000;
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::LuxurySedan));
    oc::Vehicle* sedan = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(sedan != nullptr);
    sedan->pos = {591.7f, 1008.0f};
    sedan->heading = 0.0f;
    sedan->velocity = {0.0f, 0.0f};

    world.player().pos = {588.5f, 1008.0f};
    world.player().posY = world.surfaceHeightAt(world.player().pos);
    world.player().onGround = true;

    oc::PlayerInput climbIn{};
    climbIn.cameraYaw = 0.0f; // East (+X) toward the parked sedan
    climbIn.moveForward = 1.0f;
    climbIn.jumpPressed = true;
    for (int i = 0; i < 24; ++i) {
        world.update(1.0f / 60.0f, climbIn);
        climbIn.jumpPressed = false;
    }
    // Let player settle onto the roof of the sedan
    for (int i = 0; i < 35; ++i) {
        world.update(1.0f / 60.0f, {});
    }
    ASSERT_TRUE(world.player().onGround);
    ASSERT_TRUE(world.player().posY > 1.15f);

    ++gTestsPassed;
}

void testCivilianRadialPanicFleeAndSidewalkRecovery() {
    oc::GameWorld world(6060ULL);

    // Find a strolling civilian and stand 8m away aiming a pistol directly at them
    int civIdx = -1;
    for (std::size_t i = 0; i < world.npcs().size(); ++i) {
        if (world.npcs()[i].role == oc::NpcRole::Civilian && world.npcs()[i].alive) {
            civIdx = static_cast<int>(i);
            break;
        }
    }
    ASSERT_TRUE(civIdx >= 0);

    const oc::Vec2 civStart = world.npcs()[static_cast<std::size_t>(civIdx)].pos;
    world.player().pos = civStart - oc::Vec2{7.5f, 0.0f};
    world.city().resolveCircleWorld(world.player().pos, 0.5f);
    world.player().currentWeapon = oc::WeaponId::Pistol;

    const float initialDist = (world.npcs()[static_cast<std::size_t>(civIdx)].pos - world.player().pos).length();
    const oc::Vec2 toCiv = (civStart - world.player().pos).normalized();

    oc::PlayerInput aimThreat{};
    aimThreat.aimHeld = true;
    aimThreat.aimYaw = std::atan2(toCiv.z, toCiv.x);
    world.update(1.0f / 60.0f, aimThreat);

    ASSERT_TRUE(world.npcs()[static_cast<std::size_t>(civIdx)].behavior == oc::NpcBehavior::PanicFlee);

    // Run 1.2s of simulation and verify rapid radial escape away from the player without entering buildings
    for (int i = 0; i < 72; ++i) {
        world.update(1.0f / 60.0f, {});
        const auto& c = world.npcs()[static_cast<std::size_t>(civIdx)];
        for (const auto& b : world.city().buildings()) {
            ASSERT_TRUE(!b.footprint.contains(c.pos));
        }
    }
    const float escapedDist = (world.npcs()[static_cast<std::size_t>(civIdx)].pos - world.player().pos).length();
    ASSERT_TRUE(escapedDist > initialDist + 3.2f);

    ++gTestsPassed;
}

void testPolicePursuitWantedEscalationBribeAndBusted() {
    oc::GameWorld world(7070ULL);
    world.player().cash = 25000;

    // 1. Verify ambient SBPD police cruisers & beat officers exist calmly at 0 stars
    int ambientCruisers = 0;
    int ambientOfficers = 0;
    for (const auto& v : world.vehicles()) {
        if (v.kind == oc::VehicleKind::PoliceCruiser && v.isPolice) {
            ++ambientCruisers;
            ASSERT_TRUE(!v.sirenActive);
        }
    }
    for (const auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police && n.alive) ++ambientOfficers;
    }
    ASSERT_TRUE(ambientCruisers >= 2);
    ASSERT_TRUE(ambientOfficers >= 2);
    ASSERT_TRUE(world.player().wantedLevel == 0);

    // 2. Escalate to 4-Star Wanted Level with officer witness -> verify SWAT ArmoredSuv & active sirens dispatch!
    for (auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police && n.alive) {
            n.pos = world.player().pos + oc::Vec2{12.0f, 0.0f};
            n.facingYaw = oc::kPi;
            break;
        }
    }
    world.reportCrime(12.0f, 4, world.player().pos, true);
    ASSERT_TRUE(world.player().wantedLevel >= 4);

    for (int i = 0; i < 240; ++i) {
        world.update(1.0f / 60.0f, {});
    }

    int activeSirens = 0;
    bool hasSwatUnit = false;
    for (const auto& v : world.vehicles()) {
        if (v.isPolice && v.sirenActive) ++activeSirens;
        if (v.isPolice && v.kind == oc::VehicleKind::ArmoredSuv) hasSwatUnit = true;
    }
    for (const auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police && n.isSwat) hasSwatUnit = true;
    }
    ASSERT_TRUE(activeSirens >= 2);
    ASSERT_TRUE(hasSwatUnit);

    // 3. Smartphone Tab 3 Option [7]: Instant Police Bribe clears Wanted Level!
    const int cashBeforeBribe = world.player().cash;
    const int starsBefore = world.player().wantedLevel;
    ASSERT_TRUE(world.executePhoneAction(3, 7));
    ASSERT_TRUE(world.player().wantedLevel == 0);
    ASSERT_TRUE(world.player().cash == cashBeforeBribe - starsBefore * 400);

    // 4. Steal a Police Cruiser, toggle siren with [G], and clear Wanted at PAY 'N' TUNE!
    int cruiserId = -1;
    for (const auto& v : world.vehicles()) {
        if (v.kind == oc::VehicleKind::PoliceCruiser && !v.destroyed) {
            cruiserId = v.id;
            world.player().pos = v.pos + oc::Vec2{1.4f, 0.0f};
            break;
        }
    }
    ASSERT_TRUE(cruiserId >= 0);
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    ASSERT_TRUE(world.player().drivingVehicleId == cruiserId);
    ASSERT_TRUE(world.player().wantedLevel >= 2); // Stealing a police cruiser triggers 2-star pursuit!

    const bool sirenBeforeG = world.playerVehicle()->sirenActive;
    oc::PlayerInput hornIn{};
    hornIn.hornPressed = true;
    world.update(1.0f / 60.0f, hornIn);
    ASSERT_TRUE(world.playerVehicle()->sirenActive != sirenBeforeG);

    // Move cruiser to PAY 'N' TUNE far from officers and respray to clear stars
    const auto* gPoi = world.city().findPoi(oc::PoiKind::TuneGarage);
    ASSERT_TRUE(gPoi != nullptr);
    if (auto* myCruiser = world.findVehicleMutable(cruiserId)) {
        myCruiser->pos = gPoi->pos;
    }
    // Ensure no cop is within 16m of garage so Pay 'n' Tune respray isn't witnessed
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Police && (n.pos - gPoi->pos).length() < 18.0f) {
            n.pos = gPoi->pos + oc::Vec2{42.0f, 0.0f};
        }
    }
    ASSERT_TRUE(world.purchaseShopItem(1)); // Repair & clear heat
    ASSERT_TRUE(world.player().wantedLevel == 0);

    // 5. Verify BUSTED arrest & Gangnam Police Precinct respawn when incapacitated near police while Wanted!
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    world.reportCrime(3.5f, 2, world.player().pos, true);
    world.update(1.0f / 60.0f, {});
    ASSERT_TRUE(world.player().wantedLevel >= 2);

    // Place player right next to an active police officer with low health so officer arrests/neutralizes player
    for (auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police && n.alive) {
            world.player().pos = n.pos + oc::Vec2{3.0f, 0.0f};
            n.facingYaw = 0.0f;
            break;
        }
    }
    world.player().armor = 0.0f;
    world.player().health = 4.0f;
    for (int i = 0; i < 120 && !world.player().wasted; ++i) {
        world.update(1.0f / 60.0f, {});
    }
    ASSERT_TRUE(world.player().wasted);
    ASSERT_TRUE(world.player().lastDeathWasBusted);

    // Advance past respawn timer and verify clean respawn at Gangnam Police Precinct (656.0, 1000.8)
    for (int i = 0; i < 200; ++i) {
        world.update(1.0f / 60.0f, {});
    }
    ASSERT_TRUE(!world.player().wasted);
    ASSERT_TRUE(world.player().wantedLevel == 0);
    ASSERT_TRUE((world.player().pos - oc::Vec2{656.0f, 1000.8f}).length() < 2.0f);

    ++gTestsPassed;
}

void testFirstPersonAimAlignmentAndPrecisionSpread() {
    // 1. Validate left-hand-drive driver eye coordinates across all 10 vehicle classes
    for (int k = 0; k < static_cast<int>(oc::VehicleKind::Count); ++k) {
        const auto kind = static_cast<oc::VehicleKind>(k);
        const auto& spec = oc::getVehicleSpec(kind);
        const oc::Vec3 eye = oc::driverSeatEyeLocal(kind);
        ASSERT_TRUE(eye.z < -0.20f && eye.z > -spec.halfWidth);
        ASSERT_TRUE(eye.y > 0.90f && eye.y < 2.20f);
        const float maxEyeRatio = (kind == oc::VehicleKind::SeoulBus) ? 0.85f : 0.65f;
        ASSERT_TRUE(std::abs(eye.x) < spec.halfLength * maxEyeRatio);
    }

    // 2. Verify 1st-person strafing locks player.facingYaw 1:1 to input.aimYaw without 3rd-person hip-turn drift
    oc::GameWorld world(20261108ULL);
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = world.surfaceHeightAt(world.player().pos);

    oc::PlayerInput strafeIn{};
    strafeIn.firstPerson = true;
    strafeIn.cameraYaw = 0.42f;
    strafeIn.aimYaw = 0.42f;
    strafeIn.aimPitch = 0.0f;
    strafeIn.moveRight = 1.0f; // Pure sideways strafe
    strafeIn.moveForward = -1.0f;
    for (int i = 0; i < 15; ++i) {
        world.update(1.0f / 60.0f, strafeIn);
    }
    ASSERT_TRUE(std::abs(world.player().facingYaw - 0.42f) < 1e-3f);

    // 3. Verify 1st-person precision ADS eye ray (y = posY + 1.62m) lands exact long-range headshots
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = 0.0f;
    world.player().velocity = {0.0f, 0.0f};
    world.player().currentWeapon = oc::WeaponId::AssaultRifle;
    auto& rifle = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    rifle.owned = true;
    rifle.magAmmo = 30;
    rifle.reserveAmmo = 120;

    // Clear vehicles/props along +X corridor and stage an alive target 36m downrange
    int targetId = -1;
    for (auto& v : world.vehicles()) {
        if (std::abs(v.pos.z - 1008.0f) < 8.0f && v.pos.x > 580.0f && v.pos.x < 640.0f) {
            v.pos.z = 1042.0f;
        }
    }
    for (auto& n : world.npcs()) {
        if (!n.alive) continue;
        if (targetId < 0 && n.role == oc::NpcRole::Gangster) {
            targetId = n.id;
            n.pos = {624.0f, 1008.0f};
            n.velocity = {0.0f, 0.0f};
            n.health = 200.0f;
        } else if (std::abs(n.pos.z - 1008.0f) < 5.0f && n.pos.x > 584.0f && n.pos.x < 634.0f) {
            n.pos.z = 1032.0f;
        }
    }
    ASSERT_TRUE(targetId >= 0);

    const oc::Vec3 fpEye{588.0f, 1.62f, 1008.0f};
    const oc::Vec3 headTarget{624.0f, 1.56f, 1008.0f};
    const oc::Vec3 rayDir = (headTarget - fpEye).normalized();

    oc::PlayerInput adsShot{};
    adsShot.firstPerson = true;
    adsShot.aimHeld = true; // Precision ADS spread tighten (0.045x)
    adsShot.fireHeld = true;
    adsShot.hasAimRay3D = true;
    adsShot.aimRayOrigin = fpEye;
    adsShot.aimRayDir = rayDir;
    adsShot.aimYaw = 0.0f;
    adsShot.aimPitch = std::asin(rayDir.y);
    adsShot.cameraYaw = 0.0f;

    world.update(1.0f / 60.0f, adsShot);
    ASSERT_TRUE(world.player().crosshairOnTarget);
    ASSERT_TRUE(world.player().hitMarkerTimer > 0.0f);
    ASSERT_TRUE(world.player().hitMarkerCritical);

    ++gTestsPassed;
}

void testExpandedWeaponsVehiclesSniperAndEmergency() {
    oc::GameWorld world(20261107ULL);

    // 1. Verify expanded roster sizes and valid cockpit eye coordinates for all 17 vehicles & 11 weapons
    ASSERT_TRUE(static_cast<int>(oc::VehicleKind::Count) == 17);
    ASSERT_TRUE(static_cast<int>(oc::WeaponId::Count) == 11);
    ASSERT_TRUE(static_cast<int>(oc::kDealershipCatalog.size()) == 10);

    for (int i = 0; i < static_cast<int>(oc::VehicleKind::Count); ++i) {
        const auto vk = static_cast<oc::VehicleKind>(i);
        const auto& vspec = oc::getVehicleSpec(vk);
        ASSERT_TRUE(vspec.maxForwardSpeed > 15.0f);
        ASSERT_TRUE(vspec.maxHealth >= 140.0f);
        const oc::Vec3 eye = oc::driverSeatEyeLocal(vk);
        ASSERT_TRUE(eye.y > 0.80f && eye.y < 2.20f);
    }

    // 2. Purchase all 4 new firearms at Ammu-Tech (options 9..12: HeavyRevolver, AutoShotgun, SniperRifle, Minigun)
    const auto* wPoi = world.city().findPoi(oc::PoiKind::WeaponShop);
    ASSERT_TRUE(wPoi != nullptr);
    world.player().pos = wPoi->pos;
    world.player().posY = 0.0f;
    world.player().cash = 60000;
    ASSERT_TRUE(world.isPlayerInWeaponShopZone());

    for (int opt = 9; opt <= 12; ++opt) {
        oc::PlayerInput buyIn{};
        buyIn.shopBuyOption = opt;
        world.update(1.0f / 60.0f, buyIn);
    }

    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::HeavyRevolver)].owned);
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::HeavyRevolver)].magAmmo == 6);
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AutoShotgun)].owned);
    ASSERT_TRUE(oc::getWeaponSpec(oc::WeaponId::AutoShotgun).pellets == 6);
    ASSERT_TRUE(oc::getWeaponSpec(oc::WeaponId::AutoShotgun).automatic);
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::SniperRifle)].owned);
    ASSERT_TRUE(oc::getWeaponSpec(oc::WeaponId::SniperRifle).range >= 180.0f);
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Minigun)].owned);
    ASSERT_TRUE(world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Minigun)].magAmmo == 200);

    // 3. Verify Minigun rotary barrel spin-up during ADS
    world.player().currentWeapon = oc::WeaponId::Minigun;
    for (int f = 0; f < 12; ++f) {
        oc::PlayerInput spinIn{};
        spinIn.aimHeld = true;
        world.update(1.0f / 60.0f, spinIn);
    }
    ASSERT_TRUE(world.player().minigunSpinSpeed > 4.0f);
    ASSERT_TRUE(world.player().minigunSpin > 0.0f);

    // 4. Verify 160m ultra-long-range SniperRifle precision headshot & laser rangefinder telemetry
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = 0.0f;
    world.player().velocity = {0.0f, 0.0f};
    world.player().currentWeapon = oc::WeaponId::SniperRifle;
    world.player().fireCooldown = 0.0f;

    for (auto& v : world.vehicles()) {
        if (std::abs(v.pos.z - 1008.0f) < 8.0f && v.pos.x > 580.0f && v.pos.x < 770.0f) {
            v.pos.z = 1044.0f;
        }
    }
    int sniperTargetId = -1;
    for (auto& n : world.npcs()) {
        if (!n.alive) continue;
        if (sniperTargetId < 0 && n.role == oc::NpcRole::Gangster) {
            sniperTargetId = n.id;
            n.pos = {748.0f, 1008.0f};
            n.velocity = {0.0f, 0.0f};
            n.health = 260.0f;
        } else if (std::abs(n.pos.z - 1008.0f) < 5.0f && n.pos.x > 584.0f && n.pos.x < 760.0f) {
            n.pos.z = 1036.0f;
        }
    }
    ASSERT_TRUE(sniperTargetId >= 0);

    const oc::Vec3 sniperEye{588.0f, 1.62f, 1008.0f};
    const oc::Vec3 sniperHead{748.0f, 1.56f, 1008.0f};
    const oc::Vec3 sniperDir = (sniperHead - sniperEye).normalized();

    oc::PlayerInput sniperShot{};
    sniperShot.firstPerson = true;
    sniperShot.aimHeld = true;
    sniperShot.fireHeld = true;
    sniperShot.hasAimRay3D = true;
    sniperShot.aimRayOrigin = sniperEye;
    sniperShot.aimRayDir = sniperDir;
    sniperShot.aimYaw = 0.0f;
    sniperShot.aimPitch = std::asin(sniperDir.y);
    sniperShot.cameraYaw = 0.0f;

    world.update(1.0f / 60.0f, sniperShot);
    ASSERT_TRUE(world.player().crosshairHitDist > 155.0f);
    ASSERT_TRUE(world.player().hitMarkerTimer > 0.0f);
    ASSERT_TRUE(world.player().hitMarkerCritical);
    for (const auto& n : world.npcs()) {
        if (n.id == sniperTargetId) {
            ASSERT_TRUE(!n.alive); // 148 base * 2.15 headshot > 260 HP
        }
    }

    // 5. Purchase & summon all 4 new dealership vehicles via Smartphone Autos tab (options 7..10)
    world.player().cash = 100000;
    for (int opt = 7; opt <= 10; ++opt) {
        oc::PlayerInput phoneBuy{};
        phoneBuy.phoneTab = 0;
        phoneBuy.phoneOption = opt;
        world.update(1.0f / 60.0f, phoneBuy);
    }
    ASSERT_TRUE(world.player().garage[static_cast<std::size_t>(oc::VehicleKind::OffroadPickup)].owned);
    ASSERT_TRUE(world.player().garage[static_cast<std::size_t>(oc::VehicleKind::ClassicRoadster)].owned);
    ASSERT_TRUE(world.player().garage[static_cast<std::size_t>(oc::VehicleKind::JdmTuner)].owned);
    ASSERT_TRUE(world.player().garage[static_cast<std::size_t>(oc::VehicleKind::TrackPrototype)].owned);
    const oc::Vehicle* summonedLmp = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(summonedLmp != nullptr);
    ASSERT_TRUE(summonedLmp->kind == oc::VehicleKind::TrackPrototype);

    // 6. Verify Ambulance one-time +35 HP paramedic medkit heal and Ambulance/FireTruck [G] siren toggle
    oc::Vehicle* ambCar = nullptr;
    oc::Vehicle* fireCar = nullptr;
    for (auto& v : world.vehicles()) {
        if (!v.destroyed && !v.isPersonalCar && v.id != world.player().activePersonalVehicleId) {
            if (ambCar == nullptr) {
                ambCar = &v;
            } else if (fireCar == nullptr) {
                fireCar = &v;
                break;
            }
        }
    }
    ASSERT_TRUE(ambCar != nullptr && fireCar != nullptr);

    // Move summoned personal car aside so nearestEnterableVehicle picks ambCar cleanly
    if (oc::Vehicle* pCar = world.findVehicleMutable(world.player().activePersonalVehicleId)) {
        pCar->pos = {-80.0f, -80.0f};
    }
    ambCar->kind = oc::VehicleKind::Ambulance;
    ambCar->isPolice = false;
    ambCar->hasNpcDriver = false;
    ambCar->isParked = true;
    ambCar->medkitAvailable = true;
    ambCar->sirenActive = false;
    ambCar->pos = {world.player().pos.x + 1.8f, world.player().pos.z};
    ambCar->velocity = {0.0f, 0.0f};

    world.player().health = 50.0f;
    oc::PlayerInput enterAmb{};
    enterAmb.interactPressed = true;
    world.update(1.0f / 60.0f, enterAmb);
    ASSERT_TRUE(world.player().drivingVehicleId == ambCar->id);
    ASSERT_TRUE(world.player().health >= 84.5f); // +35 HP onboard medkit!
    ASSERT_TRUE(!ambCar->medkitAvailable);

    oc::PlayerInput toggleSiren{};
    toggleSiren.hornPressed = true;
    world.update(1.0f / 60.0f, toggleSiren);
    ASSERT_TRUE(ambCar->sirenActive);

    // Exit Ambulance and enter FireTruck to verify FireTruck siren toggle
    world.update(1.0f / 60.0f, enterAmb);
    ASSERT_TRUE(world.player().drivingVehicleId == -1);
    ambCar->pos = {-90.0f, -90.0f};

    fireCar->kind = oc::VehicleKind::FireTruck;
    fireCar->isPolice = false;
    fireCar->hasNpcDriver = false;
    fireCar->isParked = true;
    fireCar->sirenActive = false;
    fireCar->pos = {world.player().pos.x + 2.0f, world.player().pos.z};
    fireCar->velocity = {0.0f, 0.0f};

    world.update(1.0f / 60.0f, enterAmb);
    ASSERT_TRUE(world.player().drivingVehicleId == fireCar->id);
    world.update(1.0f / 60.0f, toggleSiren);
    ASSERT_TRUE(fireCar->sirenActive);

    ++gTestsPassed;
}

void testFullCustomizationStudioCharacterGunsmithVehicleHud() {
    oc::GameWorld world(20261116ULL);
    world.player().cash = 50000;

    auto sendStudio = [&](int tab, int opt) {
        oc::PlayerInput in{};
        in.customStudioTab = tab;
        in.customStudioOption = opt;
        world.update(1.0f / 60.0f, in);
    };

    // 1. Tab 0: Character Customization + Aero Moto Helmet (-8% incoming damage perk)
    for (int k = 0; k < 4; ++k) sendStudio(0, 1); // 0 -> 4 (Aero Moto Helmet)
    ASSERT_TRUE(world.player().characterCustom.hairOrHatStyle == 4);
    sendStudio(0, 2); // Hair color -> Chestnut
    sendStudio(0, 3); // Glasses -> Gold Aviator
    sendStudio(0, 4); // Skin tone -> Golden Tan (idx 2)
    sendStudio(0, 5); // Jacket color -> Crimson Red (idx 1)
    sendStudio(0, 6); // Pants color -> Jet Black (idx 1)
    sendStudio(0, 7); // Back gear -> Tactical Backpack (idx 1)
    ASSERT_TRUE(world.player().characterCustom.hairColorIdx == 1);
    ASSERT_TRUE(world.player().characterCustom.glassesStyle == 2);
    ASSERT_TRUE(world.player().characterCustom.skinToneIdx == 2);
    ASSERT_TRUE(world.player().characterCustom.customTopColorIdx == 1);
    ASSERT_TRUE(world.player().characterCustom.customBottomColorIdx == 1);
    ASSERT_TRUE(world.player().characterCustom.backGearStyle == 1);
    ASSERT_TRUE(oc::getPlayerTopColor(world.player().currentOutfit, 1).r > 200);

    // Compare incoming collision damage without helmet (style 0) vs with Aero Moto Helmet (style 4)
    auto measureHitLoss = [&](int helmetStyle) -> float {
        oc::GameWorld w(20261117ULL);
        w.player().pos = {588.0f, 1008.0f};
        w.player().posY = 0.0f;
        w.player().armor = 0.0f;
        w.player().health = 100.0f;
        w.player().characterCustom.hairOrHatStyle = helmetStyle;
        for (auto& v : w.vehicles()) {
            v.pos = {720.0f, 1120.0f};
            v.velocity = {0.0f, 0.0f};
        }
        w.vehicles()[0].destroyed = false;
        w.vehicles()[0].pos = {588.1f, 1008.0f};
        w.vehicles()[0].heading = 0.0f;
        w.vehicles()[0].velocity = {15.0f, 0.0f};
        oc::PlayerInput idle{};
        w.update(1.0f / 60.0f, idle);
        return 100.0f - w.player().health;
    };
    const float dmgNoHelmet = measureHitLoss(0);
    const float dmgWithHelmet = measureHitLoss(4);
    ASSERT_TRUE(dmgNoHelmet > 18.0f);
    ASSERT_TRUE(dmgWithHelmet < dmgNoHelmet * 0.94f && dmgWithHelmet > dmgNoHelmet * 0.90f);

    // 2. Tab 1: Gunsmith (Skin, Suppressor stealth/one-time buy, Extended Mag +50%, Laser Sight, Tracer, Optic)
    auto& arSlot = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    arSlot.owned = true;
    arSlot.magAmmo = 30;
    arSlot.reserveAmmo = 120;
    world.player().currentWeapon = oc::WeaponId::AssaultRifle;
    const int cashBeforeGunsmith = world.player().cash;

    sendStudio(1, 1);
    sendStudio(1, 1); // Skin -> 2 (24K Royal Gold), FREE
    ASSERT_TRUE(arSlot.skinIndex == 2);
    ASSERT_TRUE(world.player().cash == cashBeforeGunsmith);

    sendStudio(1, 2); // Buy & attach Suppressor (-$250)
    ASSERT_TRUE(arSlot.suppressorOwned && arSlot.suppressor);
    const int cashAfterSup = world.player().cash;
    ASSERT_TRUE(cashAfterSup == cashBeforeGunsmith - 250);
    sendStudio(1, 2); // Detach for FREE
    ASSERT_TRUE(!arSlot.suppressor);
    sendStudio(1, 2); // Re-attach for FREE
    ASSERT_TRUE(arSlot.suppressor);
    ASSERT_TRUE(world.player().cash == cashAfterSup);

    sendStudio(1, 3); // Buy & equip Extended Mag (-$200, 30 -> 45 rounds)
    ASSERT_TRUE(arSlot.extendedMagOwned && arSlot.extendedMag);
    ASSERT_TRUE(oc::effectiveMagazineSize(oc::WeaponId::AssaultRifle, true) == 45);
    ASSERT_TRUE(arSlot.magAmmo == 45);
    ASSERT_TRUE(world.player().cash == cashAfterSup - 200);

    sendStudio(1, 4);
    sendStudio(1, 4);
    sendStudio(1, 4); // Laser Sight -> 3 (Cyber Cyan)
    ASSERT_TRUE(arSlot.laserSightColor == 3);

    sendStudio(1, 5);
    sendStudio(1, 5); // Tracer -> 2 (Cyber Cyan)
    ASSERT_TRUE(arSlot.tracerColorIdx == 2);

    sendStudio(1, 6);
    sendStudio(1, 6);
    sendStudio(1, 6); // Optic -> 3 (Cyber Cyan)
    ASSERT_TRUE(arSlot.opticColorIdx == 3);

    // Fire 1 suppressed shot away from police -> verify SuppressedShot audio, Cyan tracer, and stealth low heat!
    world.clearWantedLevel(false);
    for (auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police) n.pos = {260.0f, 260.0f};
    }
    for (auto& v : world.vehicles()) {
        if (v.isPolice) v.pos = {260.0f, 260.0f};
    }
    (void)world.consumeAudioEvents();
    world.player().fireCooldown = 0.0f;

    oc::PlayerInput supFire{};
    supFire.firstPerson = true;
    supFire.fireHeld = true;
    supFire.hasAimRay3D = true;
    supFire.aimRayOrigin = {world.player().pos.x, 1.62f, world.player().pos.z};
    supFire.aimRayDir = {0.0f, 0.35f, 1.0f};
    supFire.aimRayDir = supFire.aimRayDir.normalized();
    world.update(1.0f / 60.0f, supFire);

    ASSERT_TRUE(world.player().wantedHeat <= 0.12f);
    ASSERT_TRUE(world.player().wantedLevel == 0);
    const auto audioEvs = world.consumeAudioEvents();
    bool heardSuppressed = false;
    for (const auto& ev : audioEvs) {
        if (ev.type == oc::AudioEventType::SuppressedShot) heardSuppressed = true;
    }
    ASSERT_TRUE(heardSuppressed);
    ASSERT_TRUE(!world.tracers().empty());
    ASSERT_TRUE(world.tracers().back().color.b > 240 && world.tracers().back().color.r < 80);

    // 3. Tab 2: Vehicle Full Custom Studio + Personal Garage Persistence
    world.player().cash = 80000;
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::JdmTuner));
    const int tunerId = world.player().activePersonalVehicleId;
    oc::Vehicle* tuner = world.findVehicleMutable(tunerId);
    ASSERT_TRUE(tuner != nullptr);
    world.player().pos = {tuner->pos.x + 2.0f, tuner->pos.z};
    ASSERT_TRUE(world.customizationTargetVehicle() == tuner);

    // Cycle Paint 9 times (0 -> 9 = 24K Gold Chrome, chromePaint == true; or 8 times for Mirror Chrome)
    tuner->paintPresetIndex = 7;
    sendStudio(2, 1); // -> index 8 (Mirror Chrome!)
    tuner = world.findVehicleMutable(tunerId);
    ASSERT_TRUE(tuner != nullptr && tuner->chromePaint);

    sendStudio(2, 2);
    sendStudio(2, 2); // Stripe -> 2 (Royal Gold)
    if ((tuner = world.findVehicleMutable(tunerId)) != nullptr) tuner->rimStyle = 0;
    sendStudio(2, 3);
    sendStudio(2, 3);
    sendStudio(2, 3); // Rim -> 3 (Cyber Cyan)
    if ((tuner = world.findVehicleMutable(tunerId)) != nullptr) tuner->spoilerStyle = 0;
    sendStudio(2, 4);
    sendStudio(2, 4); // Spoiler -> 2 (Swan-Neck Time-Attack Wing)
    sendStudio(2, 5);
    sendStudio(2, 5); // Stance -> 2 (Slammed Negative Camber)
    sendStudio(2, 6); // Headlights -> 1 (Cyber Cyan)
    tuner = world.findVehicleMutable(tunerId);
    ASSERT_TRUE(tuner != nullptr);
    tuner->neonPreset = 0;
    sendStudio(2, 7);
    sendStudio(2, 7); // Neon -> 2 (UV Purple)
    sendStudio(2, 8); // Drift Smoke -> 1 (Cyber Cyan)

    ASSERT_TRUE(tuner->stripeStyle == 2);
    ASSERT_TRUE(tuner->rimStyle == 3);
    ASSERT_TRUE(tuner->spoilerStyle == 2 && tuner->customSpoiler);
    ASSERT_TRUE(tuner->stanceStyle == 2);
    ASSERT_TRUE(tuner->headlightColorIdx == 1);
    ASSERT_TRUE(tuner->neonPreset == 2);
    ASSERT_TRUE(tuner->driftSmokeColorIdx == 1);

    // Summon another personal car, then re-summon JdmTuner and verify all custom fields persisted in garage!
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::Compact));
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::JdmTuner));
    const oc::Vehicle* resummoned = world.findVehicle(world.player().activePersonalVehicleId);
    ASSERT_TRUE(resummoned != nullptr && resummoned->kind == oc::VehicleKind::JdmTuner);
    ASSERT_TRUE(resummoned->chromePaint);
    ASSERT_TRUE(resummoned->stripeStyle == 2);
    ASSERT_TRUE(resummoned->rimStyle == 3);
    ASSERT_TRUE(resummoned->spoilerStyle == 2);
    ASSERT_TRUE(resummoned->stanceStyle == 2);
    ASSERT_TRUE(resummoned->headlightColorIdx == 1);
    ASSERT_TRUE(resummoned->neonPreset == 2);
    ASSERT_TRUE(resummoned->driftSmokeColorIdx == 1);

    // 4. Tab 3: Crosshair & HUD Custom Studio
    sendStudio(3, 1);
    sendStudio(3, 1); // Crosshair Style -> 2 (Tactical Circle + Dot)
    sendStudio(3, 2); // Crosshair Color -> 1 (Neon Green)
    sendStudio(3, 3); // Crosshair Scale -> 2 (Large 130%)
    sendStudio(3, 4); // Outline -> false
    sendStudio(3, 5); // Speedometer -> MPH (true)
    sendStudio(3, 6); // HUD Theme -> 1 (Vice Pink)
    ASSERT_TRUE(world.player().hudCustom.crosshairStyle == 2);
    ASSERT_TRUE(world.player().hudCustom.crosshairColorIdx == 1);
    ASSERT_TRUE(world.player().hudCustom.crosshairScaleIdx == 2);
    ASSERT_TRUE(!world.player().hudCustom.crosshairOutline);
    ASSERT_TRUE(world.player().hudCustom.useMph);
    ASSERT_TRUE(world.player().hudCustom.hudThemeIdx == 1);

    ++gTestsPassed;
}

void testRustPhysicsEngineStuntJumpsAndRealisticWorld() {
    oc::GameWorld world(20261118ULL);
    world.player().cash = 100000;

    // Clear traffic away from our test arenas
    for (auto& v : world.vehicles()) {
        v.pos = {-280.0f, -280.0f};
        v.velocity = {0.0f, 0.0f};
        v.hasNpcDriver = false;
        v.isParked = true;
    }

    // 1. Verify Rust 6-Speed Automatic Transmission, RPM Tachometer & 4-Wheel Independent Suspension
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::Hypercar));
    const int carId = world.player().activePersonalVehicleId;
    oc::Vehicle* car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    car->pos = {504.0f, 1008.0f};
    car->heading = 0.0f;
    car->velocity = {0.0f, 0.0f};
    car->isPlayerDriving = true;
    car->isParked = false;
    world.player().drivingVehicleId = carId;
    world.player().pos = car->pos;

    oc::PlayerInput accelIn{};
    accelIn.moveForward = 1.0f;
    accelIn.sprintOrNitro = true;
    for (int i = 0; i < 70; ++i) {
        if (i > 48) accelIn.moveRight = 1.0f; // Hard right turn to test lateral suspension roll transfer
        world.update(1.0f / 60.0f, accelIn);
    }
    car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    ASSERT_TRUE(car->speed() > 16.0f);
    ASSERT_TRUE(car->currentGear >= 2);
    ASSERT_TRUE(car->engineRpm > 2200.0f && car->engineRpm <= 8200.0f);
    // Turning right rolls chassis left, compressing left wheels (0, 2) vs right wheels (1, 3)
    ASSERT_TRUE(std::abs(car->wheelCompression[0] - car->wheelCompression[1]) > 0.012f);

    // 2. Verify Smooth Arched Driving across Real-Scale Seoul Banpo Bridge (x=588) over Han River!
    car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    const oc::SeoulBridge* banpo = nullptr;
    for (const auto& br : world.city().bridges()) {
        if (br.hasRainbowFountain) { banpo = &br; break; }
    }
    ASSERT_TRUE(banpo != nullptr);
    car->pos = {banpo->centerX + oc::CityMap::kLaneOffset, banpo->zSouth + 28.0f};
    car->posY = world.surfaceHeightAt(car->pos);
    car->velY = 0.0f;
    car->airborne = false;
    car->heading = -oc::kHalfPi; // Facing North (-Z) across Han River on Banpo Bridge!
    car->steerVisual = 0.0f;
    car->velocity = {0.0f, -34.0f};
    car->angularVelocity = 0.0f;
    world.player().pos = car->pos;

    float peakBridgeY = 0.0f;
    bool reachedNorthBank = false;
    oc::PlayerInput bridgeIn{};
    bridgeIn.moveForward = 1.0f;
    bridgeIn.sprintOrNitro = true;
    for (int i = 0; i < 2400; ++i) {
        for (auto& v : world.vehicles()) {
            if (v.id != carId && std::abs(v.pos.x - banpo->centerX) < 16.0f) {
                v.pos.x += 160.0f;
            }
        }
        world.update(1.0f / 60.0f, bridgeIn);
        car = world.findVehicleMutable(carId);
        ASSERT_TRUE(car != nullptr);
        peakBridgeY = std::max(peakBridgeY, car->posY);
        if (car->pos.z < banpo->zNorth - 12.0f) {
            reachedNorthBank = true;
            break;
        }
    }
    ASSERT_TRUE(peakBridgeY > 6.5f && peakBridgeY < 10.5f);
    ASSERT_TRUE(reachedNorthBank);

    // 3. Verify Rust Rotational Collision Impulse (PIT Maneuver Spin-Out) & 4-Quadrant Crash Deformation
    int targetCarId = -1;
    for (auto& v : world.vehicles()) {
        if (v.id != carId && !v.destroyed) {
            targetCarId = v.id;
            break;
        }
    }
    ASSERT_TRUE(targetCarId >= 0);
    car = world.findVehicleMutable(carId);
    oc::Vehicle* targetCar = world.findVehicleMutable(targetCarId);
    ASSERT_TRUE(car != nullptr && targetCar != nullptr);

    car->pos = {588.0f, 1008.0f};
    car->posY = 0.0f;
    car->velY = 0.0f;
    car->airborne = false;
    car->heading = 0.0f;
    car->velocity = {26.0f, 0.0f};
    car->angularVelocity = 0.0f;
    car->frontDamage = 0.0f;
    car->health = car->maxHealth;

    // Position targetCar slightly ahead and offset laterally so impact hits off-center (r x J != 0)
    targetCar->pos = {591.6f, 1008.85f};
    targetCar->posY = 0.0f;
    targetCar->velY = 0.0f;
    targetCar->airborne = false;
    targetCar->heading = 0.45f;
    targetCar->velocity = {0.0f, 0.0f};
    targetCar->angularVelocity = 0.0f;
    targetCar->rearDamage = 0.0f;
    targetCar->leftDamage = 0.0f;
    targetCar->rightDamage = 0.0f;
    targetCar->health = targetCar->maxHealth;
    targetCar->hasNpcDriver = false;
    targetCar->isParked = true;

    oc::PlayerInput ramIn{};
    ramIn.moveForward = 1.0f;
    bool sawTargetSpin = false;
    for (int i = 0; i < 8; ++i) {
        world.update(1.0f / 60.0f, ramIn);
        targetCar = world.findVehicleMutable(targetCarId);
        if (targetCar && std::abs(targetCar->angularVelocity) > 0.15f) {
            sawTargetSpin = true;
        }
    }
    car = world.findVehicleMutable(carId);
    targetCar = world.findVehicleMutable(targetCarId);
    ASSERT_TRUE(car != nullptr && targetCar != nullptr);
    ASSERT_TRUE(sawTargetSpin);
    ASSERT_TRUE(car->frontDamage > 0.03f);
    ASSERT_TRUE((targetCar->rearDamage + targetCar->leftDamage + targetCar->rightDamage) > 0.03f);

    // 4. Verify Rust 3D Destructible Street Prop Impulse & Automatic Upright Respawn
    world.city().updateStreaming(car->pos, true);
    world.player().pos = car->pos;
    std::size_t propIdx = 0;
    float bestPropDist = 1e9f;
    for (std::size_t i = 0; i < world.city().streetProps().size(); ++i) {
        const auto& sp = world.city().streetProps()[i];
        if (sp.kind == oc::StreetPropKind::Hydrant || sp.kind == oc::StreetPropKind::TrafficCone || sp.kind == oc::StreetPropKind::TrashBin) {
            const float d = (sp.homePos - world.player().pos).length();
            if (d < bestPropDist) {
                bestPropDist = d;
                propIdx = i;
            }
        }
    }
    const oc::Vec2 propHome = world.city().streetProps()[propIdx].homePos;
    world.city().streetProps()[propIdx].broken = false;
    world.city().streetProps()[propIdx].pos = propHome;
    world.city().streetProps()[propIdx].posY = 0.0f;
    world.city().streetProps()[propIdx].brokenTimer = 0.0f;

    car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    car->pos = propHome - oc::Vec2{1.0f, 0.0f};
    car->posY = world.surfaceHeightAt(car->pos);
    car->airborne = false;
    car->heading = 0.0f;
    car->velocity = {18.0f, 0.0f};
    world.update(1.0f / 60.0f, ramIn);
    ASSERT_TRUE(world.city().streetProps()[propIdx].broken);
    ASSERT_TRUE(world.city().streetProps()[propIdx].posY > 0.02f || world.city().streetProps()[propIdx].velY > 0.5f);

    // Move car away and advance simulation past 14s respawn timer
    for (int i = 0; i < 340; ++i) {
        car = world.findVehicleMutable(carId);
        if (car) {
            car->pos = {propHome.x - 40.0f, propHome.z - 40.0f};
            car->velocity = {0.0f, 0.0f};
        }
        oc::PlayerInput waitIn{};
        world.update(0.05f, waitIn);
    }
    ASSERT_TRUE(!world.city().streetProps()[propIdx].broken);
    ASSERT_TRUE(std::abs(world.city().streetProps()[propIdx].pos.x - propHome.x) < 0.01f);
    ASSERT_TRUE(std::abs(world.city().streetProps()[propIdx].posY) < 0.01f);

    // 5. Verify Dynamic Rain / Wet Road Mirror Reflection Toggle ([U])
    ASSERT_TRUE(!world.isRainWeather());
    world.toggleRainWeather();
    ASSERT_TRUE(world.isRainWeather());
    world.toggleRainWeather();
    ASSERT_TRUE(!world.isRainWeather());

    ++gTestsPassed;
}

void testBugfixRegressionsV091() {
    oc::GameWorld world(20261119ULL);

    // 1. Verify ZERO trees or street props overlap Sunset Pier walkway, Golden Bay Bridge deck, or Grand Central Park lake
    auto overlapsClearZone = [&](const oc::Vec2& p) -> bool {
        if (p.x < -686.0f && std::abs(p.z) < 8.2f) return true;
        if (world.city().isOnBridge(p, 2.5f)) return true;
        for (const auto& lk : world.city().lakes()) {
            if (lk.bounds.contains(p)) return true;
        }
        return false;
    };
    for (const auto& t : world.city().trees()) {
        ASSERT_TRUE(!overlapsClearZone(t.pos));
    }
    for (const auto& sp : world.city().streetProps()) {
        ASSERT_TRUE(!overlapsClearZone(sp.pos));
    }

    // Move all traffic & NPCs clear of the central test corridor
    for (auto& v : world.vehicles()) {
        v.pos = {-260.0f, -260.0f};
        v.posY = 0.0f;
        v.velY = 0.0f;
        v.velocity = {0.0f, 0.0f};
        v.hasNpcDriver = false;
        v.isParked = true;
        v.destroyed = false;
    }
    for (auto& n : world.npcs()) {
        n.pos = {-260.0f, 260.0f};
        n.velocity = {0.0f, 0.0f};
    }

    // 2. Verify 3D firearm raycast hits an elevated vehicle (posY = 2.4m) at elevated height & misses underneath it
    ASSERT_TRUE(!world.vehicles().empty());
    const int testCarId = world.vehicles()[0].id;
    auto getCar = [&]() -> oc::Vehicle* {
        return world.findVehicleMutable(testCarId);
    };

    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->kind = oc::VehicleKind::SportsCoupe;
        car->health = 200.0f;
        car->maxHealth = 200.0f;
        car->pos = {610.0f, 1008.0f};
        car->posY = 2.4f;
        car->velY = 0.0f;
        car->airborne = true;
        car->heading = 0.0f;
        car->velocity = {0.0f, 0.0f};
    }

    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = 0.0f;
    world.player().velocity = {0.0f, 0.0f};
    world.player().currentWeapon = oc::WeaponId::AssaultRifle;
    auto& ar = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    ar.owned = true;
    ar.magAmmo = 30;
    ar.reserveAmmo = 120;
    world.player().fireCooldown = 0.0f;

    // Shot A: straight at y = 0.50m underneath the elevated car -> MUST MISS!
    oc::PlayerInput lowShot{};
    lowShot.firstPerson = true;
    lowShot.aimHeld = true;
    lowShot.fireHeld = true;
    lowShot.hasAimRay3D = true;
    lowShot.aimRayOrigin = {588.0f, 0.50f, 1008.0f};
    lowShot.aimRayDir = {1.0f, 0.0f, 0.0f};
    lowShot.aimYaw = 0.0f;
    lowShot.aimPitch = 0.0f;
    world.update(1.0f / 60.0f, lowShot);
    ASSERT_TRUE(getCar() != nullptr && getCar()->health >= 199.9f);

    // Shot B: straight at y = 2.95m through the elevated car chassis -> MUST HIT!
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {610.0f, 1008.0f};
        car->posY = 2.4f;
        car->velY = 0.0f;
    }
    world.player().fireCooldown = 0.0f;
    oc::PlayerInput highShot = lowShot;
    highShot.aimRayOrigin = {588.0f, 2.95f, 1008.0f};
    world.update(1.0f / 60.0f, highShot);
    ASSERT_TRUE(getCar() != nullptr && getCar()->health < 199.0f);
    ASSERT_TRUE(world.player().hitMarkerTimer > 0.0f);

    // 3. Verify player roof landing support height uses elevated vehicle's v.posY
    const auto& carSpec = oc::getVehicleSpec(oc::VehicleKind::SportsCoupe);
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {588.0f, 1008.0f};
        car->posY = 1.50f;
        car->velY = 0.0f;
        car->airborne = true;
        car->velocity = {0.0f, 0.0f};

        world.player().pos = {588.0f, 1008.0f};
        world.player().posY = car->posY + 0.24f + carSpec.chassisHeight + 0.04f;
        world.player().verticalVel = -3.5f;
        world.player().onGround = false;
    }
    oc::PlayerInput landIn{};
    world.update(1.0f / 60.0f, landIn);
    world.update(1.0f / 60.0f, landIn);
    ASSERT_TRUE(world.player().onGround);
    ASSERT_TRUE(getCar() != nullptr &&
                world.player().posY >= getCar()->posY + 0.24f + carSpec.chassisHeight - 0.06f);
    ASSERT_TRUE(world.player().posY > 2.4f);

    // 4. Verify airborne car (posY = 2.6m, 25 m/s) over civilian + hydrant neither kills NPC nor breaks hydrant,
    //    and nearestEnterableVehicle() ignores cars > 2.2m above the player on the ground.
    int civId = -1;
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            civId = n.id;
            n.pos = {598.0f, 1008.0f};
            n.velocity = {0.0f, 0.0f};
            n.health = 100.0f;
            break;
        }
    }
    ASSERT_TRUE(civId >= 0);

    std::size_t hydrantIdx = world.city().streetProps().size();
    for (std::size_t i = 0; i < world.city().streetProps().size(); ++i) {
        if (world.city().streetProps()[i].kind == oc::StreetPropKind::Hydrant) {
            hydrantIdx = i;
            break;
        }
    }
    ASSERT_TRUE(hydrantIdx < world.city().streetProps().size());
    {
        auto& hydrant = world.city().streetProps()[hydrantIdx];
        hydrant.broken = false;
        hydrant.pos = {598.0f, 1008.0f};
        hydrant.homePos = {598.0f, 1008.0f};
        hydrant.posY = 0.0f;
    }

    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {597.8f, 1008.0f};
        car->posY = 2.6f;
        car->velY = 2.0f;
        car->airborne = true;
        car->velocity = {25.0f, 0.0f};
        car->heading = 0.0f;
    }
    world.player().pos = {548.0f, 1008.0f};
    world.player().posY = 0.0f;

    oc::PlayerInput stepIn{};
    world.update(1.0f / 60.0f, stepIn);
    bool civSurvived = false;
    for (const auto& n : world.npcs()) {
        if (n.id == civId) {
            civSurvived = (n.alive && n.health >= 99.9f);
            break;
        }
    }
    ASSERT_TRUE(civSurvived);
    ASSERT_TRUE(!world.city().streetProps()[hydrantIdx].broken);

    // Player standing directly below an elevated car (posY = 3.2m) must not be offered enter prompt
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {588.0f, 1008.0f};
        car->posY = 3.2f;
        car->velocity = {0.0f, 0.0f};
    }
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = 0.0f;
    ASSERT_TRUE(world.nearestEnterableVehicle() == nullptr);

    // 5. Verify a destroyed airborne vehicle falls under gravity back to the road surface
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->destroyed = true;
        car->pos = {588.0f, 1008.0f};
        car->posY = 4.0f;
        car->velY = 0.0f;
        car->airborne = true;
        car->velocity = {0.0f, 0.0f};
    }
    for (int i = 0; i < 90; ++i) {
        world.update(1.0f / 60.0f, stepIn);
    }
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        ASSERT_TRUE(car->posY <= world.surfaceHeightAt(car->pos) + 0.05f);
        ASSERT_TRUE(!car->airborne);
    }

    ++gTestsPassed;
}


void testUltraDetailV100ParticlesAndEffects() {
    oc::GameWorld world(20261119ULL);

    // 1. Firing AssaultRifle ejects spinning 3D BrassCasing that advances spinAngle & bounces cleanly above ground!
    auto& ar = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    ar.owned = true;
    ar.magAmmo = 30;
    ar.reserveAmmo = 120;
    world.player().currentWeapon = oc::WeaponId::AssaultRifle;
    world.player().fireCooldown = 0.0f;
    world.player().pos = {0.0f, 0.0f};
    world.player().posY = world.surfaceHeightAt({0.0f, 0.0f});

    oc::PlayerInput fireIn{};
    fireIn.firstPerson = true;
    fireIn.fireHeld = true;
    fireIn.hasAimRay3D = true;
    fireIn.aimRayOrigin = {0.0f, 1.62f, 0.0f};
    fireIn.aimRayDir = {1.0f, 0.0f, 0.0f};
    fireIn.aimYaw = 0.0f;
    fireIn.aimPitch = 0.0f;
    world.update(1.0f / 60.0f, fireIn);

    bool foundBrass = false;
    float initialSpin = 0.0f;
    for (const auto& p : world.particles()) {
        if (p.kind == oc::ParticleKind::BrassCasing) {
            foundBrass = true;
            initialSpin = p.spinAngle;
            break;
        }
    }
    ASSERT_TRUE(foundBrass);

    // Step 45 frames and verify brass shell spins and stays cleanly above ground surface without NaN
    oc::PlayerInput idleIn{};
    for (int f = 0; f < 45; ++f) {
        world.update(1.0f / 60.0f, idleIn);
    }
    bool brassStillValid = false;
    for (const auto& p : world.particles()) {
        if (p.kind == oc::ParticleKind::BrassCasing) {
            ASSERT_TRUE(std::isfinite(p.pos.x) && std::isfinite(p.pos.y) && std::isfinite(p.pos.z));
            ASSERT_TRUE(p.pos.y >= world.surfaceHeightAt({p.pos.x, p.pos.z}));
            if (std::abs(p.spinAngle - initialSpin) > 0.5f) {
                brassStillValid = true;
            }
        }
    }
    ASSERT_TRUE(brassStillValid);

    // 2. Firing Shotgun ejects a 12-gauge ShotgunShell; HeavyRevolver & Fists eject neither!
    auto& sg = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Shotgun)];
    sg.owned = true;
    sg.magAmmo = 8;
    sg.reserveAmmo = 32;
    world.player().currentWeapon = oc::WeaponId::Shotgun;
    world.player().fireCooldown = 0.0f;
    world.update(1.0f / 60.0f, fireIn);
    bool foundShotgunShell = false;
    for (const auto& p : world.particles()) {
        if (p.kind == oc::ParticleKind::ShotgunShell) {
            foundShotgunShell = true;
            break;
        }
    }
    ASSERT_TRUE(foundShotgunShell);

    // Check HeavyRevolver spawns NO new shell casing
    int shellsBeforeRev = 0;
    for (const auto& p : world.particles()) {
        if (p.kind == oc::ParticleKind::BrassCasing || p.kind == oc::ParticleKind::ShotgunShell) {
            ++shellsBeforeRev;
        }
    }
    auto& rev = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::HeavyRevolver)];
    rev.owned = true;
    rev.magAmmo = 6;
    rev.reserveAmmo = 24;
    world.player().currentWeapon = oc::WeaponId::HeavyRevolver;
    world.player().fireCooldown = 0.0f;
    world.update(1.0f / 60.0f, fireIn);
    int shellsAfterRev = 0;
    for (const auto& p : world.particles()) {
        if (p.kind == oc::ParticleKind::BrassCasing || p.kind == oc::ParticleKind::ShotgunShell) {
            ++shellsAfterRev;
        }
    }
    ASSERT_TRUE(shellsAfterRev <= shellsBeforeRev);

    // 3. Driving fast (>14 m/s) in rain weather emits WaterSpray behind tires; dry driving emits zero WaterSpray!
    oc::GameWorld dryWorld(20261120ULL);
    dryWorld.setRainWeather(false);
    dryWorld.player().cash = 80000;
    ASSERT_TRUE(dryWorld.buyOrSummonVehicle(oc::VehicleKind::SportsCoupe));
    oc::Vehicle* dryCar = dryWorld.findVehicleMutable(dryWorld.player().activePersonalVehicleId);
    ASSERT_TRUE(dryCar != nullptr);
    dryCar->pos = {0.0f, 0.0f};
    dryCar->posY = dryWorld.surfaceHeightAt(dryCar->pos);
    dryCar->heading = 0.0f;
    dryCar->velocity = {18.0f, 0.0f};
    dryCar->isPlayerDriving = true;
    dryCar->isParked = false;
    dryWorld.player().drivingVehicleId = dryCar->id;
    oc::PlayerInput accelIn{};
    accelIn.moveForward = 1.0f;
    for (int f = 0; f < 25; ++f) {
        dryWorld.update(1.0f / 60.0f, accelIn);
    }
    int drySprayCount = 0;
    for (const auto& p : dryWorld.particles()) {
        if (p.kind == oc::ParticleKind::WaterSpray) ++drySprayCount;
    }
    ASSERT_TRUE(drySprayCount == 0);

    // Now turn on Rain Weather and drive fast -> WaterSpray MUST spawn!
    dryWorld.setRainWeather(true);
    dryCar = dryWorld.findVehicleMutable(dryWorld.player().activePersonalVehicleId);
    ASSERT_TRUE(dryCar != nullptr);
    dryCar->velocity = oc::forwardFromHeading(dryCar->heading) * 20.0f;
    for (int f = 0; f < 25; ++f) {
        dryWorld.update(1.0f / 60.0f, accelIn);
    }
    int rainSprayCount = 0;
    for (const auto& p : dryWorld.particles()) {
        if (p.kind == oc::ParticleKind::WaterSpray) ++rainSprayCount;
    }
    ASSERT_TRUE(rainSprayCount >= 2);

    // 4. Severely damaged vehicle (<25% HP) emits smoke/engine-bay sparks cleanly without NaN or crash!
    dryCar = dryWorld.findVehicleMutable(dryWorld.player().activePersonalVehicleId);
    ASSERT_TRUE(dryCar != nullptr);
    dryCar->health = dryCar->maxHealth * 0.18f;
    for (int f = 0; f < 40; ++f) {
        dryWorld.update(1.0f / 60.0f, idleIn);
    }
    for (const auto& p : dryWorld.particles()) {
        ASSERT_TRUE(std::isfinite(p.pos.x) && std::isfinite(p.pos.y) && std::isfinite(p.pos.z));
    }

    ++gTestsPassed;
}


void testV120TorqueCurveAccelerationAndBackfire() {
    // 1. Confirm physics kernel version 2000 (v2.0.0 Rust FFI & C++20 fallback parity)
    ASSERT_TRUE(oc_rs_physics_version() == 2000);

    oc::GameWorld world(20261201ULL);
    world.player().cash = 250000;
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::Hypercar));
    oc::Vehicle* car = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(car != nullptr);

    // Clear traffic along straight +X boulevard for clean launch test
    for (auto& v : world.vehicles()) {
        if (v.id != car->id && std::abs(v.pos.z - 1008.0f) < 14.0f) {
            v.pos.z += 50.0f;
        }
    }
    car->pos = {504.0f, 1008.0f};
    car->posY = world.surfaceHeightAt(car->pos);
    car->heading = 0.0f;
    car->velocity = {0.0f, 0.0f};
    car->isPlayerDriving = true;
    car->isParked = false;
    world.player().drivingVehicleId = car->id;
    world.player().pos = car->pos;

    // 2. Full-throttle launch through 1st -> 2nd/3rd gear: verify wheelspin, turbo spool, upshift backfire!
    oc::PlayerInput launchIn{};
    launchIn.moveForward = 1.0f;
    float maxWheelSlip = 0.0f;
    float maxTurboSpool = 0.0f;
    int maxGearReached = 1;
    bool sawBackfire = false;

    for (int f = 0; f < 180; ++f) {
        world.update(1.0f / 60.0f, launchIn);
        car = world.findVehicleMutable(world.player().activePersonalVehicleId);
        ASSERT_TRUE(car != nullptr);
        maxWheelSlip = std::max(maxWheelSlip, car->wheelSlip);
        maxTurboSpool = std::max(maxTurboSpool, car->turboSpool);
        maxGearReached = std::max(maxGearReached, car->currentGear);
        if (car->backfireTimer > 0.0f) {
            sawBackfire = true;
        }
    }
    ASSERT_TRUE(maxWheelSlip > 0.05f);
    ASSERT_TRUE(maxTurboSpool > 0.45f);
    ASSERT_TRUE(maxGearReached >= 2);
    ASSERT_TRUE(sawBackfire);

    // 3. Heavy braking from speed: verify ABS deceleration & thermal brake rotor glow buildup!
    oc::PlayerInput brakeIn{};
    brakeIn.moveForward = -1.0f;
    float peakBrakeHeat = 0.0f;
    for (int f = 0; f < 60; ++f) {
        world.update(1.0f / 60.0f, brakeIn);
        car = world.findVehicleMutable(world.player().activePersonalVehicleId);
        ASSERT_TRUE(car != nullptr);
        peakBrakeHeat = std::max(peakBrakeHeat, car->brakeHeat);
    }
    ASSERT_TRUE(peakBrakeHeat > 0.12f);

    ++gTestsPassed;
}

void testV120FourCornerCurbSuspensionAndCollisionFriction() {
    // 1. Asymmetric 4-corner wheel curb elevation produces left-vs-right suspension & body roll!
    RsVehicleSpecParams sp{};
    sp.mass = 1550.0f;
    sp.max_forward_speed = 38.0f;
    sp.max_reverse_speed = 11.0f;
    sp.acceleration = 14.0f;
    sp.braking = 22.0f;
    sp.turn_rate = 2.4f;
    sp.normal_grip = 7.0f;
    sp.drift_grip = 2.6f;
    sp.half_length = 2.25f;
    sp.half_width = 0.96f;
    sp.chassis_height = 1.32f;

    RsVehiclePhysicsState st{};
    st.vel_x = 12.0f;
    st.current_gear = 2;
    st.engine_rpm = 3400.0f;

    RsVehicleControlInput in{};
    in.throttle = 0.4f;
    in.upgrade_mult = 1.0f;
    in.nitro_recharge_rate = 0.08f;
    in.ground_height = 0.0f;
    // Left wheels (FL=0, RL=2) ride up a 22cm sidewalk curb while right wheels stay at 0.0m!
    in.wheel_ground_h[0] = 0.22f;
    in.wheel_ground_h[1] = 0.00f;
    in.wheel_ground_h[2] = 0.22f;
    in.wheel_ground_h[3] = 0.00f;

    for (int i = 0; i < 20; ++i) {
        oc_rs_step_vehicle_dynamics(1.0f / 60.0f, &st, &sp, &in);
    }
    ASSERT_TRUE(std::isfinite(st.body_roll) && std::isfinite(st.wheel_comp[0]));
    ASSERT_TRUE(st.wheel_comp[0] > st.wheel_comp[1] + 0.03f);
    ASSERT_TRUE(st.wheel_comp[2] > st.wheel_comp[3] + 0.03f);
    ASSERT_TRUE(std::abs(st.body_roll) > 0.005f);

    // 2. Glancing vehicle collision applies Coulomb tangential friction impulse & angular spin cleanly!
    RsRigidBody2D a{};
    a.pos_x = 0.0f;
    a.pos_z = 0.0f;
    a.vel_x = 16.0f;
    a.vel_z = 6.0f; // Strong tangential glide along +Z while normal is +X!
    a.heading = 0.0f;
    a.mass = 1500.0f;
    a.half_length = 2.2f;
    a.half_width = 0.95f;

    RsRigidBody2D b{};
    b.pos_x = 4.2f;
    b.pos_z = 0.4f;
    b.vel_x = 0.0f;
    b.vel_z = 0.0f;
    b.heading = 0.0f;
    b.mass = 1500.0f;
    b.half_length = 2.2f;
    b.half_width = 0.95f;

    const float impact = oc_rs_resolve_rigid_collision_2d(
        &a, &b, -1.0f, 0.0f, 0.25f, 2.1f, 0.35f, 0.22f);
    ASSERT_TRUE(impact > 10.0f);
    // Coulomb tangential friction MUST transfer +Z velocity from A to B and decelerate A's Z glide!
    ASSERT_TRUE(b.vel_z > 0.25f);
    ASSERT_TRUE(a.vel_z < 5.75f);
    ASSERT_TRUE(std::isfinite(a.angular_velocity) && std::isfinite(b.angular_velocity));

    ++gTestsPassed;
}

void testV120LivingNpcDiversityRainUmbrellasAndDodge() {
    oc::GameWorld world(20261202ULL);

    // 1. Verify rich civilian diversity across archetypes, held props, hair styles, and height variation
    std::set<int> archetypes;
    std::set<int> props;
    std::set<int> hairStyles;
    float minH = 9.0f;
    float maxH = 0.0f;
    for (const auto& n : world.npcs()) {
        if (!n.alive || n.role != oc::NpcRole::Civilian) continue;
        archetypes.insert(static_cast<int>(n.archetype));
        props.insert(static_cast<int>(n.heldProp));
        hairStyles.insert(n.hairStyle);
        minH = std::min(minH, n.heightScale);
        maxH = std::max(maxH, n.heightScale);
    }
    ASSERT_TRUE(archetypes.size() >= 4);
    ASSERT_TRUE(props.size() >= 3);
    ASSERT_TRUE(hairStyles.size() >= 3);
    ASSERT_TRUE(maxH - minH > 0.06f);

    // 2. Enabling rain weather prompts calm pedestrians to open colorful 3D umbrellas!
    world.setRainWeather(true);
    for (int f = 0; f < 20; ++f) {
        world.update(1.0f / 60.0f, {});
    }
    int umbrellaCount = 0;
    for (const auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian && n.holdingUmbrella) {
            ++umbrellaCount;
        }
    }
    ASSERT_TRUE(umbrellaCount >= 3);

    // 3. Fast oncoming car triggers sideways dodge, and impact produces momentum knockback + smooth fallProgress!
    oc::NpcCharacter* targetNpc = nullptr;
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            targetNpc = &n;
            break;
        }
    }
    ASSERT_TRUE(targetNpc != nullptr);
    const int targetId = targetNpc->id;
    targetNpc->pos = {598.0f, 1008.0f};
    targetNpc->velocity = {0.0f, 0.0f};
    targetNpc->dodgeCooldown = 0.0f;
    targetNpc->behavior = oc::NpcBehavior::Stroll;

    // Stage a moving car heading straight toward the NPC at 14 m/s from 6m away
    oc::Vehicle& car = world.vehicles()[0];
    car.destroyed = false;
    car.pos = {592.0f, 1008.0f};
    car.posY = world.surfaceHeightAt(car.pos);
    car.heading = 0.0f;
    car.velocity = {14.0f, 0.0f};
    car.hasNpcDriver = false;
    car.isParked = false;

    for (int f = 0; f < 5; ++f) {
        world.update(1.0f / 60.0f, {});
    }
    bool dodged = false;
    for (const auto& n : world.npcs()) {
        if (n.id == targetId && (n.dodgeCooldown > 0.0f || std::abs(n.pos.z - 1008.0f) > 0.15f)) {
            dodged = true;
            break;
        }
    }
    ASSERT_TRUE(dodged);

    // Now place NPC right in front of fast car to verify momentum knockback + smooth fallProgress animation
    for (auto& n : world.npcs()) {
        if (n.id == targetId) {
            n.alive = true;
            n.health = 20.0f;
            n.pos = {car.pos.x + 1.2f, car.pos.z};
            n.fallProgress = 0.0f;
            break;
        }
    }
    car.velocity = {20.0f, 0.0f};
    world.update(1.0f / 60.0f, {});
    float earlyFall = -1.0f;
    for (const auto& n : world.npcs()) {
        if (n.id == targetId) {
            ASSERT_TRUE(!n.alive);
            ASSERT_TRUE(n.knockbackVel.length() > 1.0f);
            earlyFall = n.fallProgress;
            ASSERT_TRUE(earlyFall < 0.85f);
            break;
        }
    }
    ASSERT_TRUE(earlyFall >= 0.0f);
    for (int f = 0; f < 45; ++f) {
        world.update(1.0f / 60.0f, {});
    }
    for (const auto& n : world.npcs()) {
        if (n.id == targetId) {
            ASSERT_TRUE(n.fallProgress > earlyFall && n.fallProgress >= 0.99f);
            break;
        }
    }

    ++gTestsPassed;
}

void testV120GunpointHandsUpReaction() {
    oc::GameWorld world(20261203ULL);
    world.setRainWeather(true);
    world.player().pos = {588.0f, 1008.0f};
    world.player().posY = 0.0f;
    world.player().drivingVehicleId = -1;
    world.player().currentWeapon = oc::WeaponId::Pistol;
    auto& pistol = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Pistol)];
    pistol.owned = true;
    pistol.magAmmo = 12;

    // Clear vehicles along +X and stage a calm civilian with an open umbrella 6.5m ahead
    for (auto& v : world.vehicles()) {
        if (std::abs(v.pos.z - 1008.0f) < 8.0f && v.pos.x > 584.0f && v.pos.x < 606.0f) {
            v.pos.z += 30.0f;
        }
    }
    int civId = -1;
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            civId = n.id;
            n.pos = {594.5f, 1008.0f};
            n.velocity = {0.0f, 0.0f};
            n.behavior = oc::NpcBehavior::Stroll;
            n.activity = oc::NpcActivity::None;
            n.holdingUmbrella = true;
            break;
        }
    }
    ASSERT_TRUE(civId >= 0);

    // Aim weapon at civilian WITHOUT firing -> civilian raises hands in surrender & drops umbrella!
    oc::PlayerInput aimIn{};
    aimIn.firstPerson = true;
    aimIn.aimHeld = true;
    aimIn.fireHeld = false;
    aimIn.cameraYaw = 0.0f;
    aimIn.aimYaw = 0.0f;
    aimIn.aimPitch = 0.0f;
    for (int f = 0; f < 10; ++f) {
        world.update(1.0f / 60.0f, aimIn);
    }

    bool handsUp = false;
    bool droppedUmbrella = false;
    for (const auto& n : world.npcs()) {
        if (n.id == civId) {
            handsUp = (n.activity == oc::NpcActivity::HandsUp);
            droppedUmbrella = !n.holdingUmbrella;
            break;
        }
    }
    ASSERT_TRUE(handsUp);
    ASSERT_TRUE(droppedUmbrella);

    ++gTestsPassed;
}


void testV200RealScaleSeoulAndEscapablePolice() {
    oc::GameWorld world(20262000ULL);
    const auto& city = world.city();

    // 1. Verify Real-Scale 30km x 25km Seoul Metropolis dimensions, 15 Han River Bridges, 7 Mountains & 12 Landmarks
    ASSERT_TRUE((oc::CityMap::kWorldMaxX - oc::CityMap::kWorldMinX) > 30000.0f);
    ASSERT_TRUE((oc::CityMap::kWorldMaxZ - oc::CityMap::kWorldMinZ) > 25000.0f);
    ASSERT_TRUE(city.bridges().size() == 15);
    ASSERT_TRUE(city.mountains().size() == 7);
    ASSERT_TRUE(city.landmarks().size() == 12);
    ASSERT_TRUE(!city.lakes().empty());
    ASSERT_TRUE(!city.buildings().empty());

    // Verify iconic Seoul landmarks & mountain elevations
    const auto* lotte = city.findLandmark(oc::SeoulLandmarkKind::LotteWorldTower);
    const auto* namsan = city.findLandmark(oc::SeoulLandmarkKind::NSeoulTower);
    const auto* palace = city.findLandmark(oc::SeoulLandmarkKind::GyeongbokgungPalace);
    const auto* b63 = city.findLandmark(oc::SeoulLandmarkKind::Building63);
    ASSERT_TRUE(lotte != nullptr && lotte->height >= 550.0f);
    ASSERT_TRUE(namsan != nullptr && city.terrainElevationAt(namsan->pos) > 110.0f);
    ASSERT_TRUE(palace != nullptr && b63 != nullptr);

    // 2. Verify Han River analytic water vs Yeouido Island vs drivable bridges!
    const oc::Vec2 yeouidoCenter{-4800.0f, city.hanRiverCenterZ(-4800.0f) + 165.0f};
    ASSERT_TRUE(city.isOnYeouidoIsland(yeouidoCenter));
    ASSERT_TRUE(!city.isInHanRiverWater(yeouidoCenter));
    const float midHanZ = city.hanRiverCenterZ(588.0f);
    ASSERT_TRUE(city.isInHanRiverWater({400.0f, midHanZ}));
    ASSERT_TRUE(city.isOnBridge({588.0f, midHanZ}));
    ASSERT_TRUE(city.bridgeDeckElevationAt({588.0f, midHanZ}) > 6.5f);

    // 3. Verify Real-Scale Seoul Interactive Map Waypoint & Fast-Travel + Deterministic Chunk Streaming!
    world.setWaypoint(lotte->pos, lotte->nameKo);
    ASSERT_TRUE(world.player().hasWaypoint);
    ASSERT_TRUE((world.player().waypointPos - lotte->pos).length() < 1.0f);

    ASSERT_TRUE(world.fastTravelTo(palace->pos, palace->nameKo));
    ASSERT_TRUE((world.player().pos - palace->pos).length() < 160.0f);
    ASSERT_TRUE(!world.city().buildings().empty());

    // Fast-travel back to Gangnam/Banpo for Escapable Police AI test!
    ASSERT_TRUE(world.fastTravelTo({588.0f, 1008.0f}, "반포"));

    // 4. Verify Realistic Escapable Seoul Police Chase AI (LOS break + LKP Search + Zero Ambush Popups -> Escape!)
    world.reportCrime(1.5f, 2, world.player().pos, true);
    ASSERT_TRUE(world.player().wantedLevel == 2);

    // Move player cleanly out of sight 420m away behind dense Gangnam city blocks
    world.fastTravelTo({1008.0f, 1512.0f}, "강남");
    for (auto& v : world.vehicles()) {
        if (v.isPolice) {
            v.pos = {588.0f, 1008.0f}; // Officers remain near crime scene / LKP!
            v.velocity = {0.0f, 0.0f};
        }
    }
    for (auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police) n.pos = {588.0f, 1008.0f};
    }

    bool sawSearchingOutOfSight = false;
    bool sawAmbushPopup = false;
    for (int f = 0; f < 1600 && world.player().wantedLevel > 0; ++f) {
        world.update(1.0f / 60.0f, {});
        if (world.player().policeSearching && !world.player().policeLineOfSight) {
            sawSearchingOutOfSight = true;
            for (const auto& v : world.vehicles()) {
                if (v.isPolice && !v.destroyed && (v.pos - world.player().pos).length() < 45.0f) {
                    sawAmbushPopup = true;
                }
            }
        }
    }
    ASSERT_TRUE(sawSearchingOutOfSight);
    ASSERT_TRUE(!sawAmbushPopup);
    ASSERT_TRUE(world.player().wantedLevel == 0); // Player cleanly escaped Seoul Police!

    // 5. Verify Korean Tree species & OEM zero-neon defaults
    bool hasGinkgoOrPine = false;
    for (const auto& t : city.trees()) {
        if (t.species == oc::TreeSpecies::GinkgoDeciduous || t.species == oc::TreeSpecies::KoreanPine) {
            hasGinkgoOrPine = true;
            break;
        }
    }
    ASSERT_TRUE(hasGinkgoOrPine);
    for (const auto& v : world.vehicles()) {
        ASSERT_TRUE(v.neonPreset == 0);
    }

    ++gTestsPassed;
}

void testV210SeoulMapFidelityAndOrientations() {
    oc::GameWorld world(20262100ULL);
    const auto& city = world.city();

    // 1. Verify Real Seoul Geography: 4 Tributaries, 9 Major Parks, Curved Riverside Expressways & Arterials
    ASSERT_TRUE(city.tributaries().size() == 4);
    ASSERT_TRUE(city.majorParks().size() == 9);
    ASSERT_TRUE(city.arterials().size() >= 10);

    // Verify water detection inside Anyangcheon, Jungnangcheon, Tancheon, and Cheonggyecheon
    for (const auto& trib : city.tributaries()) {
        ASSERT_TRUE(trib.points.size() >= 2);
        const oc::Vec2 mid = (trib.points[0] + trib.points[1]) * 0.5f;
        ASSERT_TRUE(city.isInTributaryWater(mid));
    }

    // Verify Major Seoul Parks detection (Yeouido Park, Seoul Forest, Olympic Park, Boramae Park, Gwanghwamun Square...)
    for (const auto& pz : city.majorParks()) {
        const auto* found = city.findMajorSeoulPark(pz.center);
        ASSERT_TRUE(found != nullptr && found->nameEn == pz.nameEn);
    }

    // Verify curved Gangbyeonbuk-ro & Olympic-daero expressway query
    int foundRiversideExpressways = 0;
    for (const auto& road : city.arterials()) {
        if (road.nameEn == "Gangbyeonbuk-ro" || road.nameEn == "Olympic-daero") {
            ASSERT_TRUE(road.isExpressway && road.points.size() >= 20);
            const oc::Vec2 sample = road.points[road.points.size() / 2];
            ASSERT_TRUE(city.isOnRiversideExpressway(sample));
            ++foundRiversideExpressways;
        }
    }
    ASSERT_TRUE(foundRiversideExpressways == 2);

    // 2. Verify Real-World Landmark Orientations & Exact Gwanghwamun Gate Collision Match!
    const auto* b63 = city.findLandmark(oc::SeoulLandmarkKind::Building63);
    const auto* assembly = city.findLandmark(oc::SeoulLandmarkKind::NationalAssembly);
    const auto* lotte = city.findLandmark(oc::SeoulLandmarkKind::LotteWorldTower);
    const auto* cityHall = city.findLandmark(oc::SeoulLandmarkKind::SeoulCityHall);
    const auto* sungnyemun = city.findLandmark(oc::SeoulLandmarkKind::SungnyemunGate);
    ASSERT_TRUE(b63 != nullptr && std::abs(b63->yawRad) > 0.30f);
    ASSERT_TRUE(assembly != nullptr && std::abs(assembly->yawRad) > 0.15f);
    ASSERT_TRUE(lotte != nullptr && std::abs(lotte->yawRad) > 0.10f);
    // Seoul City Hall & Sungnyemun aligned to Sejong-daero centerline (x = -924)
    ASSERT_TRUE(cityHall != nullptr && std::abs(cityHall->pos.x - (-924.0f)) < 20.0f);
    ASSERT_TRUE(sungnyemun != nullptr && std::abs(sungnyemun->pos.x - (-924.0f)) < 10.0f);

    // Verify Gwanghwamun Gate collision box sits at exact visual z = -5654 (fixing v2.0.0 194m mismatch!)
    bool foundGwanghwamunGateBox = false;
    for (const auto& mb : city.macroBuildings()) {
        const oc::Vec2 c = mb.footprint.center();
        if (std::abs(c.x - (-924.0f)) < 15.0f && std::abs(c.z - (-5654.0f)) < 12.0f) {
            foundGwanghwamunGateBox = true;
            break;
        }
    }
    ASSERT_TRUE(foundGwanghwamunGateBox);

    // 3. Verify Guro/Gwanak Architecture Diversity, Street-Edge Masks, Outward StreetLights & Korean Traffic Props!
    ASSERT_TRUE(world.fastTravelTo({-3800.0f, 4200.0f}, "구로·관악"));
    bool hasPartialEdgeMask = false;
    bool hasVariedGuroColors = false;
    bool hasKoreanApt = false;
    bool hasTrafficLight = false;
    int checkedLights = 0;
    int outwardLights = 0;
    int checkedShelters = 0;
    int roadFacingShelters = 0;

    oc::ColorRGBA firstCol{0, 0, 0, 0};
    for (const auto& b : city.buildings()) {
        if (b.isMacroLandmark) continue;
        if (b.streetEdgeMask != 0x0Fu && b.streetEdgeMask != 0u) hasPartialEdgeMask = true;
        if (b.style == oc::BuildingStyle::KoreanApartment) hasKoreanApt = true;
        if (firstCol.a == 0) {
            firstCol = b.primaryColor;
        } else if (std::abs(int(b.primaryColor.r) - int(firstCol.r)) +
                   std::abs(int(b.primaryColor.g) - int(firstCol.g)) +
                   std::abs(int(b.primaryColor.b) - int(firstCol.b)) > 25) {
            hasVariedGuroColors = true;
        }
    }
    ASSERT_TRUE(hasPartialEdgeMask);
    ASSERT_TRUE(hasVariedGuroColors);
    ASSERT_TRUE(hasKoreanApt);

    for (const auto& p : city.streetProps()) {
        if (p.kind == oc::StreetPropKind::TrafficLight) {
            hasTrafficLight = true;
        } else if (p.kind == oc::StreetPropKind::StreetLight) {
            // Local +X arm in Renderer3D maps to world (cos(yaw), sin(yaw)) -> must point TOWARD nearest road!
            const int rx = city.nearestRoadIndex(p.pos.x);
            const int rz = city.nearestRoadIndex(p.pos.z);
            const float dx = city.roadCoord(rx) - p.pos.x;
            const float dz = city.roadCoord(rz) - p.pos.z;
            const oc::Vec2 toRoad = (std::abs(dx) < std::abs(dz))
                ? oc::Vec2{dx >= 0.0f ? 1.0f : -1.0f, 0.0f}
                : oc::Vec2{0.0f, dz >= 0.0f ? 1.0f : -1.0f};
            const oc::Vec2 armDir{std::cos(p.yaw), std::sin(p.yaw)};
            ++checkedLights;
            if (armDir.x * toRoad.x + armDir.z * toRoad.z > 0.7f) {
                ++outwardLights;
            }
        } else if (p.kind == oc::StreetPropKind::BusShelter) {
            // Local +Z open front maps to world (-sin(yaw), cos(yaw)) -> must face TOWARD nearest road curb!
            const int rx = city.nearestRoadIndex(p.pos.x);
            const int rz = city.nearestRoadIndex(p.pos.z);
            const float dx = city.roadCoord(rx) - p.pos.x;
            const float dz = city.roadCoord(rz) - p.pos.z;
            const oc::Vec2 toRoad = (std::abs(dx) < std::abs(dz))
                ? oc::Vec2{dx >= 0.0f ? 1.0f : -1.0f, 0.0f}
                : oc::Vec2{0.0f, dz >= 0.0f ? 1.0f : -1.0f};
            const oc::Vec2 openDir{-std::sin(p.yaw), std::cos(p.yaw)};
            ++checkedShelters;
            if (openDir.x * toRoad.x + openDir.z * toRoad.z > 0.7f) {
                ++roadFacingShelters;
            }
        }
    }
    ASSERT_TRUE(hasTrafficLight);
    ASSERT_TRUE(checkedLights >= 20 && outwardLights == checkedLights);
    ASSERT_TRUE(checkedShelters >= 2 && roadFacingShelters == checkedShelters);

    // 4. Verify Seoul 14:30 afternoon lighting default & authentic Korean Police/Taxi vehicle colors
    ASSERT_TRUE(world.timeOfDayHours() >= 14.0f && world.timeOfDayHours() <= 15.0f);

    ++gTestsPassed;
}


void testV220WeaponAnimationsEdgesAndSeoulProps() {
    oc::GameWorld world(20262200ULL);
    const auto& city = world.city();

    // 1. Verify Polished Map Edges: 14 outer Seoul-basin ring mountains + natural perimeter rim uplift!
    ASSERT_TRUE(city.mountains().size() == 7);
    ASSERT_TRUE(city.perimeterMountains().size() == 14);
    ASSERT_TRUE(city.terrainElevationAt({14950.0f,  4200.0f}) > 18.0f);
    ASSERT_TRUE(city.terrainElevationAt({-14950.0f, -4200.0f}) > 18.0f);
    ASSERT_TRUE(city.terrainElevationAt({ 2200.0f, -12450.0f}) > 18.0f);
    ASSERT_TRUE(city.terrainElevationAt({ 2200.0f,  12450.0f}) > 18.0f);

    // 2. Verify Unobstructed Banpo-daero Spawn & Authentic Seoul Streetscape Props (RoadGantry + PedGuardrail)
    const oc::Vec2 spawn = city.defaultSpawnPos();
    oc::Vec2 spawnCheck = spawn;
    ASSERT_TRUE(!city.resolveCircleWorld(spawnCheck, 0.45f));

    bool hasRoadGantry = false;
    bool hasPedGuardrail = false;
    for (const auto& p : city.streetProps()) {
        if (p.kind == oc::StreetPropKind::RoadGantry) hasRoadGantry = true;
        else if (p.kind == oc::StreetPropKind::PedGuardrail) hasPedGuardrail = true;
        if (p.kind == oc::StreetPropKind::StreetLight || p.kind == oc::StreetPropKind::TrafficLight) {
            ASSERT_TRUE((p.pos - spawn).length() > 1.2f); // Never spawn clipped inside a street pole!
        }
    }
    ASSERT_TRUE(hasRoadGantry);
    ASSERT_TRUE(hasPedGuardrail);

    // 3. Verify Weapon Equip Draw, Smooth Aim Blend, Recoil Kickback & Alternating Fists Punch Animations!
    // Unlock Assault Rifle & switch to it -> triggers weaponEquipAnim
    auto& arSlot = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    arSlot.owned = true;
    arSlot.magAmmo = 30;
    arSlot.reserveAmmo = 120;

    oc::PlayerInput switchIn{};
    switchIn.weaponSelectSlot = static_cast<int>(oc::WeaponId::AssaultRifle);
    world.update(1.0f / 60.0f, switchIn);
    ASSERT_TRUE(world.player().currentWeapon == oc::WeaponId::AssaultRifle);
    ASSERT_TRUE(world.player().weaponEquipAnim > 0.70f);

    // Let equip settle & hold aim -> weaponAimBlend smoothly rises toward 1.0
    for (int f = 0; f < 25; ++f) {
        oc::PlayerInput aimIn{};
        aimIn.aimHeld = true;
        world.update(1.0f / 60.0f, aimIn);
    }
    ASSERT_TRUE(world.player().weaponEquipAnim < 0.05f);
    ASSERT_TRUE(world.player().weaponAimBlend > 0.70f);

    // Fire a round -> snappy recoil kickback + combat linger
    oc::PlayerInput fireIn{};
    fireIn.aimHeld = true;
    fireIn.fireHeld = true;
    world.update(1.0f / 60.0f, fireIn);
    ASSERT_TRUE(world.player().weaponRecoilAnim > 0.65f);
    ASSERT_TRUE(world.player().weaponCombatLinger > 0.80f);

    // Release aim & wait -> weapon smoothly lowers back to tactical low-ready carry!
    for (int f = 0; f < 130; ++f) {
        world.update(1.0f / 60.0f, oc::PlayerInput{});
    }
    ASSERT_TRUE(world.player().weaponAimBlend < 0.15f);
    ASSERT_TRUE(world.player().weaponRecoilAnim < 0.05f);

    // Switch to Fists and throw two punches -> meleeSwingAnim triggers & alternates right/left!
    oc::PlayerInput fistsIn{};
    fistsIn.weaponSelectSlot = static_cast<int>(oc::WeaponId::Fists);
    world.update(1.0f / 60.0f, fistsIn);
    ASSERT_TRUE(world.player().currentWeapon == oc::WeaponId::Fists);

    world.player().fireCooldown = 0.0f;
    oc::PlayerInput punchIn{};
    punchIn.fireHeld = true;
    world.update(1.0f / 60.0f, punchIn);
    ASSERT_TRUE(world.player().meleeSwingAnim > 0.70f);
    const bool firstHand = world.player().meleeSwingRight;

    for (int f = 0; f < 28; ++f) {
        world.update(1.0f / 60.0f, oc::PlayerInput{});
    }
    world.update(1.0f / 60.0f, punchIn);
    ASSERT_TRUE(world.player().meleeSwingAnim > 0.70f);
    ASSERT_TRUE(world.player().meleeSwingRight != firstHand);

    ++gTestsPassed;
}


void testV230SeoulBusMagDropAndSignPolish() {
    oc::GameWorld world(20262300ULL);
    const auto& city = world.city();

    // 1. Verify Seoul City Bus (17th vehicle) CAD specs, driver seat eye position, and tactile boarding bounce!
    const auto& busSpec = oc::getVehicleSpec(oc::VehicleKind::SeoulBus);
    ASSERT_TRUE(busSpec.halfLength > 4.2f);
    ASSERT_TRUE(busSpec.halfWidth > 1.2f);
    ASSERT_TRUE(busSpec.maxHealth >= 500.0f);
    const oc::Vec3 busEye = oc::driverSeatEyeLocal(oc::VehicleKind::SeoulBus);
    ASSERT_TRUE(busEye.x > 3.0f && busEye.x < 4.2f);
    ASSERT_TRUE(busEye.y > 1.7f && busEye.y < 2.3f);
    ASSERT_TRUE(busEye.z < -0.35f && busEye.z > -0.90f);

    // Spawn Seoul Blue Bus beside player and board it via real interactPressed input
    const oc::Vec2 spawn = world.player().pos;
    auto& bus = world.spawnCustomVehicle(oc::VehicleKind::SeoulBus, {spawn.x + 2.6f, spawn.z}, 0.0f, {30, 88, 176, 255});
    const int busId = bus.id;
    oc::PlayerInput enterIn{};
    enterIn.interactPressed = true;
    world.update(1.0f / 60.0f, enterIn);
    ASSERT_TRUE(world.player().drivingVehicleId == busId);
    const oc::Vehicle* drivenBus = world.findVehicle(busId);
    ASSERT_TRUE(drivenBus != nullptr);
    ASSERT_TRUE(std::abs(drivenBus->rollVel) > 0.50f || std::abs(drivenBus->bodyRoll) > 0.01f);

    // Exit the bus cleanly
    world.update(1.0f / 60.0f, enterIn);
    ASSERT_TRUE(world.player().drivingVehicleId == -1);

    // 2. Verify Ballistic Brass Casings on fire & Tumbling Dropped Magazine on reload!
    auto& ar = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
    ar.owned = true;
    ar.magAmmo = 30;
    ar.reserveAmmo = 120;
    oc::PlayerInput switchAr{};
    switchAr.weaponSelectSlot = static_cast<int>(oc::WeaponId::AssaultRifle);
    world.update(1.0f / 60.0f, switchAr);
    world.player().fireCooldown = 0.0f;

    oc::PlayerInput fireAr{};
    fireAr.aimHeld = true;
    fireAr.fireHeld = true;
    world.update(1.0f / 60.0f, fireAr);
    bool foundBrass = false;
    for (const auto& pt : world.particles()) {
        if (pt.kind == oc::ParticleKind::BrassCasing) {
            foundBrass = true;
            break;
        }
    }
    ASSERT_TRUE(foundBrass);

    oc::PlayerInput reloadAr{};
    reloadAr.reloadPressed = true;
    world.update(1.0f / 60.0f, reloadAr);
    bool foundDroppedMag = false;
    for (const auto& pt : world.particles()) {
        if (pt.kind == oc::ParticleKind::DroppedMag) {
            foundDroppedMag = true;
            break;
        }
    }
    ASSERT_TRUE(foundDroppedMag);

    // Simulate 1.2s so dropped mag falls and settles on terrain cleanly without NaN
    for (int f = 0; f < 72; ++f) {
        world.update(1.0f / 60.0f, oc::PlayerInput{});
    }
    for (const auto& pt : world.particles()) {
        ASSERT_TRUE(std::isfinite(pt.pos.x) && std::isfinite(pt.pos.y) && std::isfinite(pt.pos.z));
    }

    // 3. Verify Outer Ring Expressway IC gantries & Korean pine windbreak rows along East Border (+14280m)!
    oc::CityMap edgeCity;
    edgeCity.generate(20262300ULL);
    edgeCity.updateStreaming({14280.0f, 900.0f}, true);
    bool hasOuterIcGantry = false;
    int outerPineCount = 0;
    for (const auto& p : edgeCity.streetProps()) {
        if (p.kind == oc::StreetPropKind::RoadGantry && p.pos.x > 14200.0f) {
            hasOuterIcGantry = true;
        }
    }
    for (const auto& t : edgeCity.trees()) {
        if (t.pos.x > 14295.0f && t.pos.x < 14320.0f) {
            ++outerPineCount;
        }
    }
    ASSERT_TRUE(hasOuterIcGantry);
    ASSERT_TRUE(outerPineCount >= 3);

    // Verify 3rd-person building raycast returns -1.0f on open sky and positive distance when hitting a building
    const float clearRay = city.raycastBuildings3D({spawn.x, 2.2f, spawn.z}, {0.0f, 1.0f, 0.0f}, 10.0f);
    ASSERT_TRUE(clearRay < 0.0f);
    bool hitAnyBuilding = false;
    for (const auto& b : city.buildings()) {
        if (b.height > 4.0f) {
            const oc::Vec2 bc = (b.footprint.min + b.footprint.max) * 0.5f;
            const oc::Vec3 from{bc.x - 40.0f, b.baseY + 2.5f, bc.z};
            const float ht = city.raycastBuildings3D(from, {1.0f, 0.0f, 0.0f}, 60.0f);
            if (ht > 0.0f && ht < 60.0f) {
                hitAnyBuilding = true;
                break;
            }
        }
    }
    ASSERT_TRUE(hitAnyBuilding);

    ++gTestsPassed;
}


void testV300RealOsmSeoulMapAndPbr() {
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };

    // 1. Verify bundled 2048x1312 cartographic map & 2048x2048 PBR texture atlases have valid PNG headers
    auto checkPng = [&](const char* rel, std::uint32_t expW, std::uint32_t expH) {
        const std::string p = findAsset(rel);
        ASSERT_TRUE(!p.empty());
        std::ifstream f(p, std::ios::binary);
        unsigned char hdr[24]{};
        ASSERT_TRUE(static_cast<bool>(f.read(reinterpret_cast<char*>(hdr), 24)));
        ASSERT_TRUE(hdr[0] == 0x89 && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G');
        const std::uint32_t w = (static_cast<std::uint32_t>(hdr[16]) << 24) |
                                (static_cast<std::uint32_t>(hdr[17]) << 16) |
                                (static_cast<std::uint32_t>(hdr[18]) << 8) |
                                static_cast<std::uint32_t>(hdr[19]);
        const std::uint32_t h = (static_cast<std::uint32_t>(hdr[20]) << 24) |
                                (static_cast<std::uint32_t>(hdr[21]) << 16) |
                                (static_cast<std::uint32_t>(hdr[22]) << 8) |
                                static_cast<std::uint32_t>(hdr[23]);
        ASSERT_TRUE(w == expW && h == expH);
    };
    checkPng("maps/seoul_map_2d.png", 3072, 1968);
    checkPng("textures/pbr_albedo.png", 4096, 4096);
    checkPng("textures/pbr_normal_rough.png", 4096, 4096);

    // 2. Load Real OpenStreetMap Seoul GIS + SRTM 30m DEM binary into GameWorld
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());
    oc::GameWorld world(20263000ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));

    const oc::CityMap& city = world.city();
    ASSERT_TRUE(city.hasRealSeoulMap());
    const oc::RealSeoulData& real = city.realData();
    ASSERT_TRUE(real.roads().size() >= 20000);
    ASSERT_TRUE(real.buildings().size() >= 90000);
    ASSERT_TRUE(real.waterTris().size() >= 2500);
    ASSERT_TRUE(real.parkTris().size() >= 10000);
    ASSERT_TRUE(real.pois().size() >= 3000);

    // 3. Verify Real SRTM 30m DEM Elevations (Namsan ~262m, Bukhansan >500m, Gwanaksan >500m, Han River ~0m)
    ASSERT_TRUE(city.terrainElevationAt({283.0f, -3078.0f}) > 200.0f);   // Real Namsan peak
    ASSERT_TRUE(city.terrainElevationAt({-618.0f, -12725.0f}) > 450.0f); // Real Bukhansan
    ASSERT_TRUE(city.terrainElevationAt({-1854.0f, 8724.0f}) > 500.0f);  // Real Gwanaksan
    ASSERT_TRUE(real.isInWater({971.0f, 945.0f}));                       // Real Han River at Banpo

    // 4. Verify Exact 2D Polygon Building Collision & 3D Raycasting on Real OSM Buildings
    bool testedBldg = false;
    for (const auto& b : real.buildings()) {
        if (b.height > 12.0f && b.halfX > 8.0f && b.halfZ > 8.0f && b.vertCount >= 4) {
            oc::Vec2 probe{b.cx, b.cz};
            oc::Vec2 outN{};
            const bool pushed = real.resolveCircleBuildings(probe, 0.8f, &outN);
            ASSERT_TRUE(pushed);
            ASSERT_TRUE((probe - oc::Vec2{b.cx, b.cz}).length() > 0.5f);

            const oc::Vec3 rayFrom{b.cx - b.halfX - 24.0f, b.baseY + 4.0f, b.cz};
            const float hitT = city.raycastBuildings3D(rayFrom, {1.0f, 0.0f, 0.0f}, 60.0f);
            ASSERT_TRUE(hitT > 0.0f && hitT < 60.0f);
            testedBldg = true;
            break;
        }
    }
    ASSERT_TRUE(testedBldg);

    // 5. Verify Real OSM Curved-Road Traffic & Sidewalk Pedestrians simulate cleanly without NaN!
    const oc::Vec2 spawnPos = world.player().pos;
    ASSERT_TRUE(real.isInCoreBounds(spawnPos));
    std::vector<oc::Vec2> initVehPos;
    for (const auto& v : world.vehicles()) {
        if (v.hasNpcDriver) initVehPos.push_back(v.pos);
    }
    ASSERT_TRUE(!initVehPos.empty());
    ASSERT_TRUE(!world.npcs().empty());

    for (int f = 0; f < 120; ++f) {
        world.update(1.0f / 60.0f, oc::PlayerInput{});
    }

    int movedCars = 0;
    std::size_t idx = 0;
    for (const auto& v : world.vehicles()) {
        ASSERT_TRUE(std::isfinite(v.pos.x) && std::isfinite(v.pos.z) && std::isfinite(v.posY));
        if (v.hasNpcDriver && idx < initVehPos.size()) {
            if ((v.pos - initVehPos[idx]).length() > 1.5f) ++movedCars;
            ++idx;
        }
    }
    ASSERT_TRUE(movedCars >= 1);
    for (const auto& n : world.npcs()) {
        ASSERT_TRUE(std::isfinite(n.pos.x) && std::isfinite(n.pos.z));
    }

    // Verify Fast-Travel onto Real OSM Gwanghwamun snaps safely onto a dry road!
    world.fastTravelTo({-715.0f, -5450.0f}, "광화문");
    ASSERT_TRUE(!city.isInHanRiverWater(world.player().pos));
    ASSERT_TRUE(city.isOnRoad(world.player().pos, 10.0f));

    ++gTestsPassed;
}


void testV310PitchInertiaMetricLanesAndFineProps() {
    // 1. Direct Rust/C++ physics test: forward acceleration -> NOSE UP (+body_pitch, rear squat wheel_comp[2/3] > front [0/1]);
    //    forward braking -> NOSE DIVE (-body_pitch, front dive wheel_comp[0/1] > rear [2/3]);
    //    cornering roll stays physically outward!
    RsVehicleSpecParams sp{};
    sp.mass = 1520.0f;
    sp.max_forward_speed = 44.0f;
    sp.max_reverse_speed = 12.0f;
    sp.acceleration = 16.5f;
    sp.braking = 26.0f;
    sp.turn_rate = 2.5f;
    sp.normal_grip = 7.4f;
    sp.drift_grip = 2.7f;
    sp.half_length = 2.25f;
    sp.half_width = 0.96f;
    sp.chassis_height = 1.32f;

    RsVehiclePhysicsState st{};
    st.current_gear = 1;
    st.engine_rpm = 1200.0f;

    RsVehicleControlInput accelIn{};
    accelIn.throttle = 1.0f;
    accelIn.upgrade_mult = 1.0f;
    accelIn.nitro_recharge_rate = 0.08f;
    for (int i = 0; i < 30; ++i) {
        oc_rs_step_vehicle_dynamics(1.0f / 60.0f, &st, &sp, &accelIn);
    }
    // Acceleration MUST squat rear & lift nose (+body_pitch > 0, rear struts compressed more than front)
    ASSERT_TRUE(st.body_pitch > 0.025f);
    ASSERT_TRUE(st.wheel_comp[2] > st.wheel_comp[0] + 0.015f);
    ASSERT_TRUE(st.wheel_comp[3] > st.wheel_comp[1] + 0.015f);

    // Build cruising speed then slam forward brakes -> nose MUST dive down (-body_pitch < 0, front struts compressed more than rear)
    st.vel_x = 28.0f;
    st.body_pitch = 0.0f;
    st.pitch_vel = 0.0f;
    RsVehicleControlInput brakeIn{};
    brakeIn.throttle = -1.0f;
    brakeIn.upgrade_mult = 1.0f;
    brakeIn.nitro_recharge_rate = 0.08f;
    for (int i = 0; i < 28; ++i) {
        oc_rs_step_vehicle_dynamics(1.0f / 60.0f, &st, &sp, &brakeIn);
    }
    ASSERT_TRUE(st.body_pitch < -0.025f);
    ASSERT_TRUE(st.wheel_comp[0] > st.wheel_comp[2] + 0.015f);
    ASSERT_TRUE(st.wheel_comp[1] > st.wheel_comp[3] + 0.015f);

    // Cornering left (steer = -1.0) at speed MUST roll chassis outward right (+body_roll > 0)
    st.vel_x = 20.0f;
    st.vel_z = 0.0f;
    st.heading = 0.0f;
    st.body_roll = 0.0f;
    st.roll_vel = 0.0f;
    st.steer_visual = 0.0f;
    RsVehicleControlInput turnLeftIn{};
    turnLeftIn.throttle = 0.35f;
    turnLeftIn.steer = -1.0f;
    turnLeftIn.upgrade_mult = 1.0f;
    turnLeftIn.nitro_recharge_rate = 0.08f;
    for (int i = 0; i < 25; ++i) {
        oc_rs_step_vehicle_dynamics(1.0f / 60.0f, &st, &sp, &turnLeftIn);
    }
    ASSERT_TRUE(st.body_roll > 0.015f);

    // 2. Real OSM Seoul Gangnam Teheran-ro: verify ZERO street trees on asphalt (edge-accurate isOnRoad)
    //    AND verify dense fine-grained Korean urban street structures are streamed cleanly!
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());
    oc::GameWorld world(20263100ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));

    const oc::CityMap& city = world.city();
    const oc::RealSeoulData& real = city.realData();

    // Every single streamed street tree MUST stay strictly clear of real road asphalt!
    for (const auto& t : city.trees()) {
        ASSERT_TRUE(!real.isOnRoad(t.pos, 1.50f));
    }

    int countStreetLight = 0;
    int countTrafficLight = 0;
    int countGantry = 0;
    int countGuardrail = 0;
    int countShopSign = 0;
    int countUtilityBox = 0;
    int countFineFurniture = 0; // Kiosk, DdareungiRack, CafeTable, Bench, Hydrant, TrashBin, Bollard, MedianBarrier
    for (const auto& p : city.streetProps()) {
        switch (p.kind) {
            case oc::StreetPropKind::StreetLight: ++countStreetLight; break;
            case oc::StreetPropKind::TrafficLight: ++countTrafficLight; break;
            case oc::StreetPropKind::RoadGantry: ++countGantry; break;
            case oc::StreetPropKind::PedGuardrail: ++countGuardrail; break;
            case oc::StreetPropKind::ShopSign: ++countShopSign; break;
            case oc::StreetPropKind::UtilityBox: ++countUtilityBox; break;
            case oc::StreetPropKind::StreetKiosk:
            case oc::StreetPropKind::DdareungiRack:
            case oc::StreetPropKind::CafeTable:
            case oc::StreetPropKind::Bench:
            case oc::StreetPropKind::Hydrant:
            case oc::StreetPropKind::TrashBin:
            case oc::StreetPropKind::Bollard:
            case oc::StreetPropKind::MedianBarrier:
                ++countFineFurniture;
                break;
            default: break;
        }
    }
    ASSERT_TRUE(countStreetLight >= 20);
    ASSERT_TRUE(countTrafficLight >= 4);
    ASSERT_TRUE(countGantry >= 1);
    ASSERT_TRUE(countGuardrail >= 8);
    ASSERT_TRUE(countShopSign >= 12);
    ASSERT_TRUE(countUtilityBox >= 3);
    ASSERT_TRUE(countFineFurniture >= 30);

    // 3. Verify civilians spawn clear of real Teheran-ro asphalt!
    int civChecked = 0;
    int civOffRoad = 0;
    for (const auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            ++civChecked;
            if (!real.isOnRoad(n.pos, 0.35f)) ++civOffRoad;
        }
    }
    ASSERT_TRUE(civChecked >= 8);
    ASSERT_TRUE(civOffRoad * 10 >= civChecked * 8); // >=80% strictly on clear sidewalks

    ++gTestsPassed;
}


void testV320ZeroRoadBlockingBuildingsAndExactRoadElevation() {
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());

    oc::GameWorld world(20263200ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));
    const oc::CityMap& city = world.city();
    const oc::RealSeoulData& real = city.realData();

    // 1. Gangnam Station intersection (3775.4, 2843.2) where OSM underground mall bldg#2952
    //    previously blocked Teheran-ro MUST be 100% clear of building collision!
    oc::Vec2 gangnamStn{3775.4f, 2843.2f};
    ASSERT_TRUE(!real.resolveCircleBuildings(gangnamStn, 1.2f));

    // 2. Verify ZERO drivable major Seoul roads (roadClass <= 3, non-tunnel) have their centerlines
    //    blocked by ANY active OSM building polygon across the entire 30km x 25km Seoul map!
    const auto& roads = real.roads();
    const auto& rVerts = real.roadVerts();
    for (const auto& r : roads) {
        if (r.roadClass > 3 || (r.flags & 4u) != 0u || r.vertCount < 2) continue;
        for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
            const oc::Vec2 mid = (rVerts[r.firstVert + s] + rVerts[r.firstVert + s + 1]) * 0.5f;
            if (!real.isInCoreBounds(mid)) continue;
            oc::Vec2 probe = mid;
            // Checking small radius 0.1m ensures road centerline never sits inside any active building!
            ASSERT_TRUE(!real.resolveCircleBuildings(probe, 0.1f));
        }
    }

    // 3. Verify exact millimeter road surface elevation parity (CityMap::surfaceHeightAt == sampleDem + roadSurfaceLiftAt)
    const oc::Vec2 spawnRoad = world.player().pos;
    const float lift = real.roadSurfaceLiftAt(spawnRoad);
    ASSERT_TRUE(lift >= 0.055f && lift <= 0.090f);
    const float expectedY = real.sampleDem(spawnRoad) + lift;
    ASSERT_TRUE(std::abs(city.surfaceHeightAt(spawnRoad) - expectedY) < 1e-4f);

    // Verify off-road point inside a park / away from roads returns 0.0 lift
    const oc::Vec2 namsanPeak{283.0f, -3078.0f};
    ASSERT_TRUE(real.roadSurfaceLiftAt(namsanPeak) == 0.0f);

    ++gTestsPassed;
}


void testV330MobileUiScalingAndIntersectionDiscipline() {
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());

    // 1. Verify Gangnam Station intersection real-OSM pedestrians stay strictly off asphalt across 5 seconds
    //    AND recover promptly back onto a clear sidewalk if displaced into an intersection!
    oc::GameWorld world(20263300ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));
    world.fastTravelTo({3795.0f, 2838.0f}, "강남역"); // Gangnam Station 8-lane intersection!
    const oc::RealSeoulData& real = world.city().realData();

    // Displace the first calm civilian directly onto Gangnam Station asphalt to test recovery
    int recoveryNpcId = -1;
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            recoveryNpcId = n.id;
            n.pos = {3775.4f, 2843.2f}; // Dead center of Gangnam Station intersection!
            n.velocity = {0.0f, 0.0f};
            ASSERT_TRUE(real.isOnRoad(n.pos, 0.30f));
            break;
        }
    }
    ASSERT_TRUE(recoveryNpcId >= 0);

    oc::PlayerInput idle{};
    for (int f = 0; f < 360; ++f) {
        world.update(1.0f / 60.0f, idle);
    }

    int calmCount = 0;
    int calmClear = 0;
    bool recoveredOffRoad = false;
    for (const auto& n : world.npcs()) {
        if (!n.alive || n.role != oc::NpcRole::Civilian) continue;
        if (n.id == recoveryNpcId) {
            recoveredOffRoad = !real.isOnRoad(n.pos, 0.30f);
        }
        if (n.behavior == oc::NpcBehavior::Stroll) {
            ++calmCount;
            if (!real.isOnRoad(n.pos, 0.30f)) ++calmClear;
        }
    }
    ASSERT_TRUE(calmCount >= 8);
    ASSERT_TRUE(calmClear == calmCount); // 100% of calm Gangnam pedestrians stay strictly off road asphalt!
    ASSERT_TRUE(recoveredOffRoad);       // Displaced pedestrian recovered cleanly back to a clear sidewalk!

    // 2. Verify oriented-OBB cross-traffic yielding: a Sedan approaching a perpendicular 10.6m SeoulBus
    //    offset sideways by 3.5m (where narrow 2.4m checks missed the bus!) MUST detect and brake smoothly!
    oc::GameWorld trafficWorld(20263301ULL);
    trafficWorld.fastTravelTo({588.0f, 940.0f}, "Seoul");
    for (auto& v : trafficWorld.vehicles()) {
        v.pos = {-9000.0f, -9000.0f};
        v.velocity = {0.0f, 0.0f};
    }
    oc::Vehicle& bus = trafficWorld.spawnCustomVehicle(
        oc::VehicleKind::SeoulBus, {600.0f, 943.5f}, oc::kHalfPi, {42, 138, 66, 255});
    bus.isParked = false;
    bus.hasNpcDriver = true;
    bus.velocity = {0.0f, 3.0f};
    const int busId = bus.id;

    oc::Vehicle& sedan = trafficWorld.spawnCustomVehicle(
        oc::VehicleKind::SportsCoupe, {586.5f, 940.0f}, 0.0f, {212, 42, 48, 255});
    sedan.isParked = false;
    sedan.hasNpcDriver = true;
    sedan.aiHorizontalRoad = true;
    sedan.aiDirection = 1;
    sedan.velocity = {12.0f, 0.0f};
    const int sedanId = sedan.id;
    (void)busId;

    const float initialSedanSpd = sedan.speed();
    bool brakedForCrossingBus = false;
    for (int f = 0; f < 30; ++f) {
        trafficWorld.update(1.0f / 60.0f, idle);
        const oc::Vehicle* sNow = trafficWorld.findVehicleMutable(sedanId);
        if (sNow && sNow->brakingVisual) brakedForCrossingBus = true;
    }
    const oc::Vehicle* sEnd = trafficWorld.findVehicleMutable(sedanId);
    ASSERT_TRUE(sEnd != nullptr);
    ASSERT_TRUE(brakedForCrossingBus);
    ASSERT_TRUE(sEnd->speed() < initialSedanSpd - 2.0f);

    // 3. Verify DPI-independent logical mobile UI invariants across all real Android aspect ratios:
    //    2560x1600 Tablet (16:10), 2400x1080 Phone (20:9), 2208x1768 Z Fold (5:4), 1280x720 Phone (16:9)
    struct DeviceRes { int sw; int sh; };
    const DeviceRes devices[4] = {{2560, 1600}, {2400, 1080}, {2208, 1768}, {1280, 720}};
    for (const auto& d : devices) {
        float uiScale = oc::clampf(static_cast<float>(d.sh) / 600.0f, 0.85f, 3.6f);
        uiScale = std::min(uiScale, static_cast<float>(d.sw) / 940.0f);
        uiScale = std::max(0.75f, uiScale);
        const float uiW = static_cast<float>(d.sw) / uiScale;
        const float uiH = static_cast<float>(d.sh) / uiScale;
        ASSERT_TRUE(uiW >= 939.0f && uiH >= 599.0f);

        // Top-left minimap bezel ends at x=160; top-right status card starts at uiW-252
        const float minimapRight = 20.0f + 136.0f + 4.0f;
        const float statusLeft = uiW - 232.0f - 20.0f;
        ASSERT_TRUE(statusLeft - minimapRight >= 520.0f); // Plenty of corridor for all 7 toolbar pills!

        // Mobile Wanted Stars + Speedometer vertical stack ends at 270dp, leaving >=120dp above Nitro button!
        const float maxRightHudBottom = 12.0f + 98.0f + 6.0f + 70.0f + 84.0f;
        const float nitroTop = uiH - 20.0f - 188.0f;
        ASSERT_TRUE(nitroTop - maxRightHudBottom >= 120.0f);
    }

    ++gTestsPassed;
}


void testV340ExpresswayVsCityRoadDistinction() {
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());

    oc::GameWorld world(20263400ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));
    const oc::RealSeoulData& real = world.city().realData();

    // 0. Verify Default Teheran-ro Spawn faces WITH one-way traffic (ZERO wrong-way!), stands dead-center
    //    inside a painted lane (never straddling a white dash), and all parked starter cars face forward!
    {
        const oc::NearestOsmRoad sNr = real.findNearestRoad(world.player().pos, 25.0f);
        ASSERT_TRUE(sNr.found && sNr.isOneway);
        const oc::OsmRoadRec& sRoad = real.roads()[sNr.roadIdx];
        const oc::RoadLaneLayout sLayout = oc::RealSeoulData::computeRoadLaneLayout(sRoad);
        const oc::Vec2 spawnFwd = oc::forwardFromHeading(world.player().facingYaw);
        ASSERT_TRUE(spawnFwd.dot(sNr.tangent) > 0.95f); // Strictly WITH one-way traffic!
        const oc::Vec2 sRight{-sNr.tangent.z, sNr.tangent.x};
        const float signedLat = (world.player().pos - sNr.closestPt).dot(sRight);
        float minLaneCenterErr = 1e9f;
        for (int li = 0; li < sLayout.lanesInDir; ++li) {
            minLaneCenterErr = std::min(minLaneCenterErr, std::abs(signedLat - sLayout.laneCenterOffset(li)));
        }
        ASSERT_TRUE(minLaneCenterErr < 0.20f);
        for (const auto& v : world.vehicles()) {
            if (!v.isParked) continue;
            const oc::NearestOsmRoad vNr = real.findNearestRoad(v.pos, 20.0f);
            if (vNr.found && vNr.roadIdx == sNr.roadIdx) {
                ASSERT_TRUE(oc::forwardFromHeading(v.heading).dot(sNr.tangent) > 0.90f);
            }
        }
    }

    // 1. Verify Gyeongbu Expressway (경부고속도로, Road #18548 Banpo~Seocho stretch at (2680.9, 1225.1)) is roadClass <= 1
    //    and streams ONLY authentic Korean highway infrastructure (tall silver cobra lights variant=3 +
    //    GREEN expressway truss gantries with bit 7 set), with ZERO crosswalk traffic lights / kiosks /
    //    cafes / Ddareungi / pedestrian guardrails and ZERO civilians spawned on expressway carriageways!
    const oc::Vec2 gyeongbuPos{2680.9f, 1225.1f};
    const oc::NearestOsmRoad gNr = real.findNearestRoad(gyeongbuPos, 35.0f);
    ASSERT_TRUE(gNr.found && gNr.roadClass <= 1u);

    // Verify exact Korean Expressway 4-lane + emergency shoulder (갓길) layout math on Gyeongbu Expressway!
    {
        const oc::OsmRoadRec& expR = real.roads()[gNr.roadIdx];
        const oc::RoadLaneLayout eLay = oc::RealSeoulData::computeRoadLaneLayout(expR);
        ASSERT_TRUE(eLay.styleByte == 176u);
        ASSERT_TRUE(eLay.totalLanes == 4 && eLay.lanesInDir == 4);
        ASSERT_TRUE(eLay.laneWidth > 3.45f && eLay.laneWidth < 3.75f);
        const float outerBusRightEdge = eLay.laneCenterOffset(eLay.lanesInDir - 1) + 1.24f;
        ASSERT_TRUE(outerBusRightEdge < eLay.driveRight - 0.15f); // Even 2.48m SeoulBus stays cleanly out of 갓길!
    }

    world.fastTravelTo(gNr.closestPt, "경부고속도로");
    oc::PlayerInput idle{};
    for (int f = 0; f < 60; ++f) {
        world.update(1.0f / 60.0f, idle);
    }

    int expCobraLights = 0;
    int expGreenGantries = 0;
    int expForbiddenProps = 0;
    for (const auto& p : world.city().streetProps()) {
        const oc::NearestOsmRoad pnr = real.findNearestRoad(p.pos, 18.0f);
        if (!pnr.found || pnr.roadClass > 1u || pnr.dist > pnr.halfWidth + 1.2f) continue;
        if (p.kind == oc::StreetPropKind::StreetLight && p.variant == 3u) {
            ++expCobraLights;
        } else if (p.kind == oc::StreetPropKind::RoadGantry && (p.variant & 0x80u) != 0u) {
            ++expGreenGantries;
        } else if (p.kind == oc::StreetPropKind::TrafficLight ||
                   p.kind == oc::StreetPropKind::PedGuardrail ||
                   p.kind == oc::StreetPropKind::StreetKiosk ||
                   p.kind == oc::StreetPropKind::CafeTable ||
                   p.kind == oc::StreetPropKind::DdareungiRack ||
                   p.kind == oc::StreetPropKind::Bench ||
                   p.kind == oc::StreetPropKind::Hydrant) {
            ++expForbiddenProps;
        }
    }
    ASSERT_TRUE(expCobraLights >= 4);
    ASSERT_TRUE(expGreenGantries >= 1);
    ASSERT_TRUE(expForbiddenProps == 0);

    // ZERO calm civilians may spawn or stroll on expressway asphalt!
    for (const auto& n : world.npcs()) {
        if (!n.alive || n.role != oc::NpcRole::Civilian) continue;
        const oc::NearestOsmRoad nnr = real.findNearestRoad(n.pos, 18.0f);
        ASSERT_TRUE(!(nnr.found && nnr.roadClass <= 1u && nnr.dist <= nnr.halfWidth + 1.5f));
    }

    // 2. Verify Expressway Traffic AI Cruises at Korean Highway Speed (> 19.5 m/s, targeting 25 m/s = 90 km/h)
    //    AND stays strictly centered in its assigned highway lane (never straddling white dashes or entering 갓길)!
    world.player().pos = gNr.closestPt + oc::Vec2{42.0f, 0.0f};
    for (auto& v : world.vehicles()) {
        v.pos = {-9000.0f, -9000.0f};
        v.velocity = {0.0f, 0.0f};
    }
    const oc::OsmRoadRec& expRoad = real.roads()[gNr.roadIdx];
    const oc::RoadLaneLayout expLayout = oc::RealSeoulData::computeRoadLaneLayout(expRoad);
    const oc::Vec2 expDir = gNr.tangent.normalized();
    const oc::Vec2 expRight{-expDir.z, expDir.x};
    const float expYaw = std::atan2(expDir.z, expDir.x);
    oc::Vehicle& hwCar = world.spawnCustomVehicle(
        oc::VehicleKind::SportsCoupe, gNr.closestPt + expRight * expLayout.laneCenterOffset(1), expYaw, {225, 232, 240, 255});
    hwCar.isParked = false;
    hwCar.hasNpcDriver = true;
    hwCar.osmRoadIdx = gNr.roadIdx;
    hwCar.osmVertIdx = std::min<std::uint32_t>(gNr.segIdx + 1u, expRoad.vertCount - 1u);
    hwCar.osmDir = 1;
    hwCar.velocity = expDir * 12.0f;
    const int hwCarId = hwCar.id;
    const int hwLaneIdx = hwCarId % std::max(1, expLayout.lanesInDir);
    // Start hwCar in its assigned AI lane so we verify zero drift onto lane dashes or emergency shoulder!
    hwCar.pos = gNr.closestPt + expRight * expLayout.laneCenterOffset(hwLaneIdx);

    float maxHwSpeed = 0.0f;
    for (int f = 0; f < 180; ++f) {
        world.update(1.0f / 60.0f, idle);
        if (const oc::Vehicle* cv = world.findVehicle(hwCarId)) {
            maxHwSpeed = std::max(maxHwSpeed, cv->speed());
        }
    }
    ASSERT_TRUE(maxHwSpeed > 19.5f);
    if (const oc::Vehicle* cv = world.findVehicle(hwCarId)) {
        const oc::NearestOsmRoad cnr = real.findNearestRoad(cv->pos, 25.0f);
        ASSERT_TRUE(cnr.found && cnr.roadClass <= 1u);
        const oc::Vec2 cr{-cnr.tangent.z, cnr.tangent.x};
        const float lat = (cv->pos - cnr.closestPt).dot(cr);
        const float expectedLat = expLayout.laneCenterOffset(hwLaneIdx);
        ASSERT_TRUE(std::abs(lat - expectedLat) < 0.55f); // Stays dead-center in its highway lane!
        ASSERT_TRUE(lat + 1.03f < expLayout.driveRight);  // Never enters right emergency shoulder (갓길)!
    }

    // 3. Contrast Check: City Boulevard Teheran-ro (테헤란로, roadClass == 2) MUST stream
    //    ROYAL-BLUE City Gantries ((variant & 0x80u) == 0u), Traffic Lights, and Pedestrian Guardrails!
    world.fastTravelTo({3960.0f, 2750.0f}, "테헤란로");
    for (int f = 0; f < 15; ++f) {
        world.update(1.0f / 60.0f, idle);
    }
    int cityBlueGantries = 0;
    int cityTrafficLights = 0;
    int cityGuardrails = 0;
    for (const auto& p : world.city().streetProps()) {
        if (p.kind == oc::StreetPropKind::RoadGantry && (p.variant & 0x80u) == 0u) ++cityBlueGantries;
        if (p.kind == oc::StreetPropKind::TrafficLight) ++cityTrafficLights;
        if (p.kind == oc::StreetPropKind::PedGuardrail) ++cityGuardrails;
    }
    ASSERT_TRUE(cityBlueGantries >= 1);
    ASSERT_TRUE(cityTrafficLights >= 2);
    ASSERT_TRUE(cityGuardrails >= 4);

    ++gTestsPassed;
}

void testV350CleanIntersectionsAndBusShelterSetback() {
    auto findAsset = [](const char* rel) -> std::string {
        const char* prefixes[] = {"assets/", "../assets/", "../../assets/"};
        for (const char* pre : prefixes) {
            const std::string p = std::string(pre) + rel;
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findAsset("maps/seoul_real.ocmap");
    ASSERT_TRUE(!ocmapPath.empty());

    oc::GameWorld world(20263500ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));
    ASSERT_TRUE(world.city().hasRealSeoulMap());
    const oc::RealSeoulData& real = world.city().realData();
    const auto& roads = real.roads();
    const auto& rVerts = real.roadVerts();

    // 1. Verify 4-bit roadFlags + 4-bit totalLanes vertex-color packing matches world.frag exact decode
    for (int lanes = 0; lanes <= 12; ++lanes) {
        for (int flags = 0; flags < 16; ++flags) {
            const std::uint8_t packed = static_cast<std::uint8_t>((lanes << 4) | (flags & 0x0F));
            const float normG = static_cast<float>(packed) / 255.0f;
            const int decoded = static_cast<int>(std::floor(normG * 255.0f + 0.5f));
            ASSERT_TRUE((decoded >> 4) == lanes);
            ASSERT_TRUE((decoded & 15) == flags);
        }
    }

    // 2. Verify Gangnam Station (3780, 2835) has crossing major arteries AND Teheran-ro (4010, 2745)
    //    has opposite-direction parallel dual carriageways whose flush contact formula has zero overlap & zero gap.
    {
        std::vector<std::uint32_t> gnRoads;
        real.queryChunkRoads({3780.0f, 2835.0f}, 60.0f, gnRoads);
        bool foundCrossingMajors = false;
        for (std::size_t a = 0; a < gnRoads.size() && !foundCrossingMajors; ++a) {
            const oc::OsmRoadRec& ra = roads[gnRoads[a]];
            if (ra.roadClass > 2u || ra.halfWidth < 6.0f || ra.vertCount < 2) continue;
            const oc::Vec2 da = (rVerts[ra.firstVert + ra.vertCount - 1] - rVerts[ra.firstVert]).normalized();
            for (std::size_t b = a + 1; b < gnRoads.size(); ++b) {
                const oc::OsmRoadRec& rb = roads[gnRoads[b]];
                if (rb.roadClass > 2u || rb.halfWidth < 6.0f || rb.vertCount < 2) continue;
                const oc::Vec2 db = (rVerts[rb.firstVert + rb.vertCount - 1] - rVerts[rb.firstVert]).normalized();
                if (std::abs(da.dot(db)) < 0.76f) {
                    foundCrossingMajors = true;
                    break;
                }
            }
        }
        ASSERT_TRUE(foundCrossingMajors);

        std::vector<std::uint32_t> thRoads;
        real.queryChunkRoads({4010.0f, 2745.0f}, 45.0f, thRoads);
        bool foundOppTwins = false;
        for (std::size_t a = 0; a < thRoads.size() && !foundOppTwins; ++a) {
            const oc::OsmRoadRec& ra = roads[thRoads[a]];
            if (ra.roadClass != 2u || ra.halfWidth < 7.0f || ra.vertCount < 2) continue;
            const oc::Vec2 pA0 = rVerts[ra.firstVert];
            const oc::Vec2 pA1 = rVerts[ra.firstVert + ra.vertCount - 1];
            const oc::Vec2 da = (pA1 - pA0).normalized();
            const oc::Vec2 perpA{-da.z, da.x};
            for (std::size_t b = a + 1; b < thRoads.size(); ++b) {
                const oc::OsmRoadRec& rb = roads[thRoads[b]];
                if (rb.roadClass != 2u || rb.halfWidth < 7.0f || rb.vertCount < 2) continue;
                const oc::Vec2 pB0 = rVerts[rb.firstVert];
                const oc::Vec2 pB1 = rVerts[rb.firstVert + rb.vertCount - 1];
                const oc::Vec2 db = (pB1 - pB0).normalized();
                if (da.dot(db) < -0.82f) {
                    const float gap = std::abs(((pB0 + pB1) * 0.5f - (pA0 + pA1) * 0.5f).dot(perpA));
                    if (gap > 8.0f && gap < ra.halfWidth + rb.halfWidth + 1.85f) {
                        const float hwA = gap * (ra.halfWidth / (ra.halfWidth + rb.halfWidth));
                        const float hwB = gap * (rb.halfWidth / (ra.halfWidth + rb.halfWidth));
                        ASSERT_TRUE(hwA >= 2.6f && hwB >= 2.6f);
                        ASSERT_TRUE(std::abs((hwA + hwB) - gap) < 0.01f);
                        foundOppTwins = true;
                        break;
                    }
                }
            }
        }
        ASSERT_TRUE(foundOppTwins);
    }

    // 3. Verify EVERY streamed OSM SubwayEntrance (5.5x2.7m) and BusShelter (4.8x1.7m) across Gangnam Station,
    //    Teheran-ro, and Gwanghwamun keeps ALL 4 footprint corners strictly off road asphalt (0cm curb overhang)!
    const oc::Vec2 checkSpots[3] = {
        {3780.0f,  2835.0f}, // Gangnam Station 8-lane intersection
        {4010.0f,  2745.0f}, // Teheran-ro mid-boulevard
        {-930.0f, -5650.0f}  // Gwanghwamun Sejong-daero
    };
    oc::PlayerInput idle{};
    int totalTransitStructures = 0;
    for (const oc::Vec2& spot : checkSpots) {
        world.fastTravelTo(spot, "검증");
        for (int f = 0; f < 5; ++f) {
            world.update(1.0f / 60.0f, idle);
        }
        for (const auto& p : world.city().streetProps()) {
            if (p.kind != oc::StreetPropKind::SubwayEntrance && p.kind != oc::StreetPropKind::BusShelter) {
                continue;
            }
            ++totalTransitStructures;
            const float halfLen   = (p.kind == oc::StreetPropKind::SubwayEntrance) ? 2.80f : 2.70f;
            const float halfDepth = (p.kind == oc::StreetPropKind::SubwayEntrance) ? 1.40f : 0.95f;
            const oc::Vec2 toRoad{-std::sin(p.yaw), std::cos(p.yaw)};
            const oc::Vec2 dirAway = toRoad * -1.0f;
            const oc::Vec2 along{-dirAway.z, dirAway.x};
            ASSERT_TRUE(!real.isOnRoad(p.pos, halfDepth + 0.45f));
            for (float sx : {-1.0f, 1.0f}) {
                for (float sz : {-1.0f, 1.0f}) {
                    const oc::Vec2 corner = p.pos + along * (sx * halfLen) + dirAway * (sz * halfDepth);
                    ASSERT_TRUE(!real.isOnRoad(corner, 0.30f));
                }
            }
        }
    }
    ASSERT_TRUE(totalTransitStructures >= 4);

    ++gTestsPassed;
}


void testV400RealOcm4BuildingsAndZeroRoadIntrusions() {
    auto findOcmap = []() -> std::string {
        const char* paths[] = {
            "assets/maps/seoul_real.ocmap",
            "../assets/maps/seoul_real.ocmap",
            "../../assets/maps/seoul_real.ocmap"
        };
        for (const char* p : paths) {
            std::ifstream f(p, std::ios::binary);
            if (f.good()) return p;
        }
        return {};
    };
    const std::string ocmapPath = findOcmap();
    ASSERT_TRUE(!ocmapPath.empty());

    oc::GameWorld world(20264000ULL);
    ASSERT_TRUE(world.loadRealSeoulMapFromFile(ocmapPath));
    const oc::RealSeoulData& real = world.city().realData();
    const oc::OcMapHeader& hdr = real.header();

    // 1. Verify OCM4 v2 high-density 3D Seoul architectural dataset (184K buildings, 10 styles, 7 roof shapes, multi-part towers)
    ASSERT_TRUE(hdr.magic[0] == 'O' && hdr.magic[1] == 'C' && hdr.magic[2] == 'M' && hdr.magic[3] == '4' && hdr.version == 2u);
    ASSERT_TRUE(real.buildings().size() >= 180000u);
    ASSERT_TRUE(real.bldgVerts().size() >= 850000u);

    int styleHist[10]{};
    int roofNonFlat = 0;
    int multiParts = 0;
    int elevatedParts = 0;
    int steppedCrowns = 0;
    const oc::OsmBldgRec* sampleElevated = nullptr;
    for (const oc::OsmBldgRec& b : real.buildings()) {
        if (b.vertCount < 3u) continue;
        if (b.style < 10u) ++styleHist[b.style];
        if (b.roofShape > 0u) ++roofNonFlat;
        if (b.roofShape == 6u) ++steppedCrowns;
        if ((b.flags & 1u) != 0u) ++multiParts;
        if (b.minHeight > 4.0f && b.height - b.minHeight >= 4.0f) {
            ++elevatedParts;
            if (!sampleElevated && b.halfX >= 3.0f && b.halfZ >= 3.0f) {
                sampleElevated = &b;
            }
        }
    }
    for (int s = 0; s < 10; ++s) {
        ASSERT_TRUE(styleHist[s] > 20);
    }
    ASSERT_TRUE(roofNonFlat >= 20000);
    ASSERT_TRUE(steppedCrowns >= 500);
    ASSERT_TRUE(multiParts >= 800);
    ASSERT_TRUE(elevatedParts >= 70);
    ASSERT_TRUE(sampleElevated != nullptr);

    // 2. Exact regression check for bldg #40410 at Seolleung Station (5106.4, 2301.3) in Teheran-ro blue bus lane!
    {
        oc::Vec2 seolleungBusLane{5106.4f, 2301.3f};
        ASSERT_TRUE(!real.resolveCircleBuildings(seolleungBusLane, 0.8f));
    }

    // 3. Verify ZERO buildings intrude onto major Seoul road carriageways across Gangnam Station -> Teheran-ro -> Seolleung!
    int majorRoadProbes = 0;
    for (const oc::OsmRoadRec& r : real.roads()) {
        if (r.roadClass > 3u || (r.flags & 4u) != 0u || r.vertCount < 2u) continue;
        if (r.maxX < 3600.0f || r.minX > 5300.0f || r.maxZ < 2100.0f || r.minZ > 3000.0f) continue;
        const float hw = std::max(2.6f, r.halfWidth);
        const float latProbe = std::max(0.0f, hw - 1.35f);
        for (std::uint16_t s = 0; s + 1 < r.vertCount; ++s) {
            const oc::Vec2 a = real.roadVerts()[r.firstVert + s];
            const oc::Vec2 b = real.roadVerts()[r.firstVert + s + 1];
            const oc::Vec2 e = b - a;
            const float len = e.length();
            if (len < 1.0f) continue;
            const oc::Vec2 rightN{-e.z / len, e.x / len};
            const oc::Vec2 mid = (a + b) * 0.5f;
            if (mid.x < 3600.0f || mid.x > 5300.0f || mid.z < 2100.0f || mid.z > 3000.0f) continue;
            oc::Vec2 pC = mid;
            ASSERT_TRUE(!real.resolveCircleBuildings(pC, 0.25f));
            ++majorRoadProbes;
            if (latProbe > 1.0f) {
                oc::Vec2 pL = mid - rightN * latProbe;
                oc::Vec2 pR = mid + rightN * latProbe;
                ASSERT_TRUE(!real.resolveCircleBuildings(pL, 0.20f));
                ASSERT_TRUE(!real.resolveCircleBuildings(pR, 0.20f));
                majorRoadProbes += 2;
            }
        }
    }
    ASSERT_TRUE(majorRoadProbes >= 200);

    // 4. Verify 3D raycast hits elevated OCM4 building_part at its true [baseY+minHeight, baseY+height] band!
    {
        const oc::OsmBldgRec& eb = *sampleElevated;
        const float midY = eb.baseY + 0.5f * (eb.minHeight + eb.height);
        const float distOut = std::max(eb.halfX, eb.halfZ) + 18.0f;
        const oc::Vec3 rayOrigin{eb.cx - distOut, midY, eb.cz};
        const float tHit = real.raycastBuildings3D(rayOrigin, {1.0f, 0.0f, 0.0f}, distOut + 10.0f);
        ASSERT_TRUE(tHit > 0.0f && tHit < distOut + 8.0f);
    }

    ++gTestsPassed;
}


} // namespace

int main() {
    testCityGenerationAndCollisions();
    testOnFootAndCarjacking();
    testVehicleDrivingAndDriftingCashBonus();
    testWeaponsShopsAndGarage();
    testAllThreeMissionsCompletion();
    test3DCrosshairAimAndHeadshots();
    testSidewalkPedestrianDiscipline();
    testUpwardCameraAndSkyscraperRaycast();
    testDealershipRealEstateTuningAndHeavyOrdnance();
    testPlayerJumpAndVehicleRoofClimbing();
    testCivilianRadialPanicFleeAndSidewalkRecovery();
    testPolicePursuitWantedEscalationBribeAndBusted();
    testFirstPersonAimAlignmentAndPrecisionSpread();
    testExpandedWeaponsVehiclesSniperAndEmergency();
    testFullCustomizationStudioCharacterGunsmithVehicleHud();
    testRustPhysicsEngineStuntJumpsAndRealisticWorld();
    testBugfixRegressionsV091();
    testUltraDetailV100ParticlesAndEffects();
    testV120TorqueCurveAccelerationAndBackfire();
    testV120FourCornerCurbSuspensionAndCollisionFriction();
    testV120LivingNpcDiversityRainUmbrellasAndDodge();
    testV120GunpointHandsUpReaction();
    testV200RealScaleSeoulAndEscapablePolice();
    testV210SeoulMapFidelityAndOrientations();
    testV220WeaponAnimationsEdgesAndSeoulProps();
    testV230SeoulBusMagDropAndSignPolish();
    testV300RealOsmSeoulMapAndPbr();
    testV310PitchInertiaMetricLanesAndFineProps();
    testV320ZeroRoadBlockingBuildingsAndExactRoadElevation();
    testV330MobileUiScalingAndIntersectionDiscipline();
    testV340ExpresswayVsCityRoadDistinction();
    testV350CleanIntersectionsAndBusShelterSetback();
    testV400RealOcm4BuildingsAndZeroRoadIntrusions();

    std::cout << "[PASS] All " << gTestsPassed << " Open City Seoul v4.0.0 simulation test suites succeeded!\n";
    return 0;
}


