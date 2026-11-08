#include "gameplay/GameWorld.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>

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
    muscle->pos = {-220.0f, oc::CityMap::kLaneOffset};
    muscle->heading = 0.0f;
    muscle->velocity = {0.0f, 0.0f};
    muscle->isPlayerDriving = true;
    muscle->isParked = false;
    world.player().drivingVehicleId = muscle->id;

    // Clear traffic obstacles in test lane so we get an unobstructed drift run
    for (auto& v : world.vehicles()) {
        if (v.id != muscle->id) {
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
        if (auto* car = world.findVehicleMutable(muscle->id)) {
            car->pos = {-120.0f, 0.0f};
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

    std::cout << "[PASS] All " << gTestsPassed << " Open City C++ simulation test suites succeeded!\n";
    return 0;
}
