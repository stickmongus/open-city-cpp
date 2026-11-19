#include "gameplay/GameWorld.hpp"
#include "physics/PhysicsBridge.hpp"
#include "render/GeneratedRealisticAssets.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
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

    // Central crossroads (0,0) must be on road and in intersection
    ASSERT_TRUE(city.isOnRoad({0.0f, 0.0f}));
    ASSERT_TRUE(city.isInIntersection({0.0f, 0.0f}));

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
    ASSERT_TRUE(victim->health < hpBefore - 55.0f);

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
    const oc::Vec3 streetOrigin{tall->footprint.min.x - 10.0f, 1.5f, center.z};
    const oc::Vec3 midTowerTarget{center.x, tall->height * 0.75f, center.z};
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
    world.player().pos = {-84.0f, 12.0f};
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
    world.player().pos = {84.0f, 12.0f};
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

    // 5. Realistic City Landmarks, Storefronts & Western Sunset Beach / Ocean Pier
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

    // Verify player can walk onto Sunset Beach (-360, 40) and Sunset Bay Pier (-410, 0)
    oc::Vec2 beachPos{-360.0f, 40.0f};
    city.resolveCircleWorld(beachPos, 0.5f);
    ASSERT_TRUE(std::abs(beachPos.x - (-360.0f)) < 0.01f);

    oc::Vec2 pierPos{-410.0f, 0.0f};
    city.resolveCircleWorld(pierPos, 0.5f);
    ASSERT_TRUE(std::abs(pierPos.x - (-410.0f)) < 0.01f);

    ++gTestsPassed;
}

void testPlayerJumpAndVehicleRoofClimbing() {
    oc::GameWorld world(5050ULL);
    world.player().pos = {14.5f, 14.5f};
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
    sedan->pos = {18.2f, 14.5f};
    sedan->heading = 0.0f;
    sedan->velocity = {0.0f, 0.0f};

    world.player().pos = {15.0f, 14.5f};
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

    // 2. Escalate to 4-Star Wanted Level -> verify SWAT ArmoredSuv & active cruiser sirens spawn!
    world.reportCrime(12.0f, 4, world.player().pos, true);
    ASSERT_TRUE(world.player().wantedLevel >= 4);

    for (int i = 0; i < 30; ++i) {
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

    // 5. Verify BUSTED arrest & Downtown Precinct respawn when incapacitated near police while Wanted!
    ASSERT_TRUE(world.tryEnterOrExitVehicle());
    world.reportCrime(3.5f, 2, world.player().pos, true);
    world.update(1.0f / 60.0f, {});
    ASSERT_TRUE(world.player().wantedLevel >= 2);

    // Place player right next to an active police officer with low health so officer arrests/neutralizes player
    for (const auto& n : world.npcs()) {
        if (n.role == oc::NpcRole::Police && n.alive) {
            world.player().pos = n.pos + oc::Vec2{3.0f, 0.0f};
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

    // Advance past respawn timer and verify clean respawn at Downtown Precinct (76.5, -14.5)
    for (int i = 0; i < 200; ++i) {
        world.update(1.0f / 60.0f, {});
    }
    ASSERT_TRUE(!world.player().wasted);
    ASSERT_TRUE(world.player().wantedLevel == 0);
    ASSERT_TRUE((world.player().pos - oc::Vec2{76.5f, -14.5f}).length() < 2.0f);

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
        ASSERT_TRUE(std::abs(eye.x) < spec.halfLength * 0.65f);
    }

    // 2. Verify 1st-person strafing locks player.facingYaw 1:1 to input.aimYaw without 3rd-person hip-turn drift
    oc::GameWorld world(20261108ULL);
    world.player().pos = {0.0f, 0.0f};
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
    world.player().pos = {0.0f, 0.0f};
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
        if (std::abs(v.pos.z) < 6.0f && v.pos.x > -2.0f && v.pos.x < 45.0f) {
            v.pos.z = 26.0f;
        }
    }
    for (auto& n : world.npcs()) {
        if (!n.alive) continue;
        if (targetId < 0 && n.role == oc::NpcRole::Gangster) {
            targetId = n.id;
            n.pos = {36.0f, 0.0f};
            n.velocity = {0.0f, 0.0f};
            n.health = 200.0f;
        } else if (std::abs(n.pos.z) < 4.0f && n.pos.x > 0.0f && n.pos.x < 42.0f) {
            n.pos.z = 18.0f;
        }
    }
    ASSERT_TRUE(targetId >= 0);

    const oc::Vec3 fpEye{0.0f, 1.62f, 0.0f};
    const oc::Vec3 headTarget{36.0f, 1.56f, 0.0f};
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

    // 1. Verify expanded roster sizes and valid cockpit eye coordinates for all 16 vehicles & 11 weapons
    ASSERT_TRUE(static_cast<int>(oc::VehicleKind::Count) == 16);
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
    world.player().pos = {0.0f, 0.0f};
    world.player().posY = 0.0f;
    world.player().velocity = {0.0f, 0.0f};
    world.player().currentWeapon = oc::WeaponId::SniperRifle;
    world.player().fireCooldown = 0.0f;

    for (auto& v : world.vehicles()) {
        if (std::abs(v.pos.z) < 7.0f && v.pos.x > -4.0f && v.pos.x < 175.0f) {
            v.pos.z = 28.0f;
        }
    }
    int sniperTargetId = -1;
    for (auto& n : world.npcs()) {
        if (!n.alive) continue;
        if (sniperTargetId < 0 && n.role == oc::NpcRole::Gangster) {
            sniperTargetId = n.id;
            n.pos = {160.0f, 0.0f};
            n.velocity = {0.0f, 0.0f};
            n.health = 260.0f;
        } else if (std::abs(n.pos.z) < 5.0f && n.pos.x > 0.0f && n.pos.x < 172.0f) {
            n.pos.z = 22.0f;
        }
    }
    ASSERT_TRUE(sniperTargetId >= 0);

    const oc::Vec3 sniperEye{0.0f, 1.62f, 0.0f};
    const oc::Vec3 sniperHead{160.0f, 1.56f, 0.0f};
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
        w.player().pos = {0.0f, 0.0f};
        w.player().posY = 0.0f;
        w.player().armor = 0.0f;
        w.player().health = 100.0f;
        w.player().characterCustom.hairOrHatStyle = helmetStyle;
        for (auto& v : w.vehicles()) {
            v.pos = {120.0f, 120.0f};
            v.velocity = {0.0f, 0.0f};
        }
        w.vehicles()[0].destroyed = false;
        w.vehicles()[0].pos = {0.1f, 0.0f};
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
    oc::Vehicle* tuner = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(tuner != nullptr);
    world.player().pos = {tuner->pos.x + 2.0f, tuner->pos.z};
    ASSERT_TRUE(world.customizationTargetVehicle() == tuner);

    // Cycle Paint 9 times (0 -> 9 = 24K Gold Chrome, chromePaint == true; or 8 times for Mirror Chrome)
    tuner->paintPresetIndex = 7;
    sendStudio(2, 1); // -> index 8 (Mirror Chrome!)
    ASSERT_TRUE(tuner->chromePaint);

    sendStudio(2, 2);
    sendStudio(2, 2); // Stripe -> 2 (Royal Gold)
    tuner->rimStyle = 0;
    sendStudio(2, 3);
    sendStudio(2, 3);
    sendStudio(2, 3); // Rim -> 3 (Cyber Cyan)
    tuner->spoilerStyle = 0;
    sendStudio(2, 4);
    sendStudio(2, 4); // Spoiler -> 2 (Swan-Neck Time-Attack Wing)
    sendStudio(2, 5);
    sendStudio(2, 5); // Stance -> 2 (Slammed Negative Camber)
    sendStudio(2, 6); // Headlights -> 1 (Cyber Cyan)
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
    car->pos = {-150.0f, 0.0f};
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

    // 2. Verify Rust 3D Stunt Ramp Launch, Airborne Flight & Stunt Jump Cash Bonus
    // North Bridge Approach Ramp (#3): center = (0.0, 46.0), dir = (0, +1), length = 12.0, height = 2.65
    const int cashBeforeStunt = world.player().cash;
    car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    car->pos = {0.0f, 40.5f};
    car->posY = 0.0f;
    car->velY = 0.0f;
    car->airborne = false;
    car->heading = oc::kHalfPi; // Facing +Z up the ramp
    car->velocity = {0.0f, 30.0f};
    car->angularVelocity = 0.0f;

    bool sawRampElevation = false;
    bool sawAirborne = false;
    oc::PlayerInput stuntIn{};
    stuntIn.moveForward = 1.0f;
    stuntIn.sprintOrNitro = true;
    for (int i = 0; i < 140; ++i) {
        world.update(1.0f / 60.0f, stuntIn);
        car = world.findVehicleMutable(carId);
        ASSERT_TRUE(car != nullptr);
        if (car->posY > 1.2f) sawRampElevation = true;
        if (car->airborne) sawAirborne = true;
    }
    ASSERT_TRUE(sawRampElevation);
    ASSERT_TRUE(sawAirborne);
    ASSERT_TRUE(world.player().cash > cashBeforeStunt);
    ASSERT_TRUE(world.stuntJumpsCompleted() >= 1);

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

    car->pos = {0.0f, 0.0f};
    car->posY = 0.0f;
    car->velY = 0.0f;
    car->airborne = false;
    car->heading = 0.0f;
    car->velocity = {26.0f, 0.0f};
    car->angularVelocity = 0.0f;
    car->frontDamage = 0.0f;
    car->health = car->maxHealth;

    // Position targetCar slightly ahead and offset laterally so impact hits off-center (r x J != 0)
    targetCar->pos = {3.6f, 0.85f};
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
    std::size_t propIdx = 0;
    for (std::size_t i = 0; i < world.city().streetProps().size(); ++i) {
        const auto k = world.city().streetProps()[i].kind;
        if (k == oc::StreetPropKind::Hydrant || k == oc::StreetPropKind::TrafficCone || k == oc::StreetPropKind::TrashBin) {
            propIdx = i;
            break;
        }
    }
    auto& prop = world.city().streetProps()[propIdx];
    prop.broken = false;
    prop.pos = prop.homePos;
    prop.posY = 0.0f;
    prop.brokenTimer = 0.0f;

    car = world.findVehicleMutable(carId);
    ASSERT_TRUE(car != nullptr);
    car->pos = prop.homePos - oc::Vec2{1.0f, 0.0f};
    car->posY = 0.0f;
    car->airborne = false;
    car->heading = 0.0f;
    car->velocity = {18.0f, 0.0f};
    world.update(1.0f / 60.0f, ramIn);
    ASSERT_TRUE(prop.broken);
    ASSERT_TRUE(prop.posY > 0.02f || prop.velY > 0.5f);

    // Move car away and advance simulation past 14s respawn timer
    for (int i = 0; i < 340; ++i) {
        car = world.findVehicleMutable(carId);
        if (car) {
            car->pos = {prop.homePos.x - 100.0f, prop.homePos.z - 100.0f};
            car->velocity = {0.0f, 0.0f};
        }
        oc::PlayerInput waitIn{};
        world.update(0.05f, waitIn);
    }
    ASSERT_TRUE(!prop.broken);
    ASSERT_TRUE(std::abs(prop.pos.x - prop.homePos.x) < 0.01f);
    ASSERT_TRUE(std::abs(prop.posY) < 0.01f);

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

    // 1. Verify ZERO trees or street props overlap any of the 6 Stunt Ramps or Sunset Pier walkway
    auto overlapsAnyRampOrPier = [](const oc::Vec2& p) -> bool {
        if (p.x < -342.0f && std::abs(p.z) < 8.2f) return true;
        for (const auto& r : oc::generated::kCityStuntRamps) {
            const float dx = p.x - r.centerX;
            const float dz = p.z - r.centerZ;
            const float along = dx * r.dirX + dz * r.dirZ;
            const float across = -dx * r.dirZ + dz * r.dirX;
            if (std::abs(along) <= r.length * 0.5f + 1.8f &&
                std::abs(across) <= r.width * 0.5f + 1.2f) {
                return true;
            }
        }
        return false;
    };
    for (const auto& t : world.city().trees()) {
        ASSERT_TRUE(!overlapsAnyRampOrPier(t.pos));
    }
    for (const auto& sp : world.city().streetProps()) {
        ASSERT_TRUE(!overlapsAnyRampOrPier(sp.pos));
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
        car->pos = {22.0f, 0.0f};
        car->posY = 2.4f;
        car->velY = 0.0f;
        car->airborne = true;
        car->heading = 0.0f;
        car->velocity = {0.0f, 0.0f};
    }

    world.player().pos = {0.0f, 0.0f};
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
    lowShot.aimRayOrigin = {0.0f, 0.50f, 0.0f};
    lowShot.aimRayDir = {1.0f, 0.0f, 0.0f};
    lowShot.aimYaw = 0.0f;
    lowShot.aimPitch = 0.0f;
    world.update(1.0f / 60.0f, lowShot);
    ASSERT_TRUE(getCar() != nullptr && getCar()->health >= 199.9f);

    // Shot B: straight at y = 2.95m through the elevated car chassis -> MUST HIT!
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {22.0f, 0.0f};
        car->posY = 2.4f;
        car->velY = 0.0f;
    }
    world.player().fireCooldown = 0.0f;
    oc::PlayerInput highShot = lowShot;
    highShot.aimRayOrigin = {0.0f, 2.95f, 0.0f};
    world.update(1.0f / 60.0f, highShot);
    ASSERT_TRUE(getCar() != nullptr && getCar()->health < 199.0f);
    ASSERT_TRUE(world.player().hitMarkerTimer > 0.0f);

    // 3. Verify player roof landing support height uses elevated vehicle's v.posY
    const auto& carSpec = oc::getVehicleSpec(oc::VehicleKind::SportsCoupe);
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {0.0f, 0.0f};
        car->posY = 1.50f;
        car->velY = 0.0f;
        car->airborne = true;
        car->velocity = {0.0f, 0.0f};

        world.player().pos = {0.0f, 0.0f};
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
            n.pos = {10.0f, 0.0f};
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
        hydrant.pos = {10.0f, 0.0f};
        hydrant.homePos = {10.0f, 0.0f};
        hydrant.posY = 0.0f;
    }

    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->pos = {9.8f, 0.0f};
        car->posY = 2.6f;
        car->velY = 2.0f;
        car->airborne = true;
        car->velocity = {25.0f, 0.0f};
        car->heading = 0.0f;
    }
    world.player().pos = {-40.0f, -40.0f};
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
        car->pos = {0.0f, 0.0f};
        car->posY = 3.2f;
        car->velocity = {0.0f, 0.0f};
    }
    world.player().pos = {0.0f, 0.0f};
    world.player().posY = 0.0f;
    ASSERT_TRUE(world.nearestEnterableVehicle() == nullptr);

    // 5. Verify a destroyed airborne vehicle falls under gravity back to the road surface
    {
        oc::Vehicle* car = getCar();
        ASSERT_TRUE(car != nullptr);
        car->destroyed = true;
        car->pos = {0.0f, 0.0f};
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
    // 1. Confirm physics kernel version 1200 (Rust FFI & C++20 fallback parity)
    ASSERT_TRUE(oc_rs_physics_version() == 1200);

    oc::GameWorld world(20261201ULL);
    world.player().cash = 250000;
    ASSERT_TRUE(world.buyOrSummonVehicle(oc::VehicleKind::Hypercar));
    oc::Vehicle* car = world.findVehicleMutable(world.player().activePersonalVehicleId);
    ASSERT_TRUE(car != nullptr);

    // Clear traffic along straight +X boulevard for clean launch test
    for (auto& v : world.vehicles()) {
        if (v.id != car->id && std::abs(v.pos.z) < 12.0f) {
            v.pos.z += 40.0f;
        }
    }
    car->pos = {-150.0f, 0.0f};
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
    targetNpc->pos = {10.0f, 0.0f};
    targetNpc->velocity = {0.0f, 0.0f};
    targetNpc->dodgeCooldown = 0.0f;
    targetNpc->behavior = oc::NpcBehavior::Stroll;

    // Stage a moving car heading straight toward the NPC at 14 m/s from 6m away
    oc::Vehicle& car = world.vehicles()[0];
    car.destroyed = false;
    car.pos = {4.0f, 0.0f};
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
        if (n.id == targetId && (n.dodgeCooldown > 0.0f || std::abs(n.pos.z) > 0.15f)) {
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
    world.player().pos = {0.0f, 0.0f};
    world.player().posY = 0.0f;
    world.player().drivingVehicleId = -1;
    world.player().currentWeapon = oc::WeaponId::Pistol;
    auto& pistol = world.player().weapons[static_cast<std::size_t>(oc::WeaponId::Pistol)];
    pistol.owned = true;
    pistol.magAmmo = 12;

    // Clear vehicles along +X and stage a calm civilian with an open umbrella 6.5m ahead
    for (auto& v : world.vehicles()) {
        if (std::abs(v.pos.z) < 8.0f && v.pos.x > -4.0f && v.pos.x < 16.0f) {
            v.pos.z += 25.0f;
        }
    }
    int civId = -1;
    for (auto& n : world.npcs()) {
        if (n.alive && n.role == oc::NpcRole::Civilian) {
            civId = n.id;
            n.pos = {6.5f, 0.0f};
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

    std::cout << "[PASS] All " << gTestsPassed << " Open City C++ simulation test suites succeeded!\n";
    return 0;
}

