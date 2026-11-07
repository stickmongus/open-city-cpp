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

} // namespace

int main() {
    testCityGenerationAndCollisions();
    testOnFootAndCarjacking();
    testVehicleDrivingAndDriftingCashBonus();
    testWeaponsShopsAndGarage();
    testAllThreeMissionsCompletion();

    std::cout << "[PASS] All " << gTestsPassed << " Open City C++ simulation test suites succeeded!\n";
    return 0;
}
