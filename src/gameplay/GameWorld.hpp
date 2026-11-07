#pragma once

#include "gameplay/Types.hpp"
#include "world/CityMap.hpp"
#include <deque>
#include <optional>
#include <vector>

namespace oc {

class GameWorld {
public:
    explicit GameWorld(std::uint64_t seed = 20261107ULL);

    void reset();
    void update(float dt, const PlayerInput& input);

    // Direct helpers (also handy for automated testing)
    bool tryEnterOrExitVehicle();
    bool purchaseShopItem(int optionOneBased);
    bool startMissionFromPoi(PoiKind poiKind);
    void advanceTimeOfDayHours(float hours);

    const CityMap& city() const { return city_; }
    CityMap& city() { return city_; }

    const PlayerState& player() const { return player_; }
    PlayerState& player() { return player_; }

    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    std::vector<Vehicle>& vehicles() { return vehicles_; }
    const Vehicle* findVehicle(int id) const;
    Vehicle* findVehicleMutable(int id);
    const Vehicle* playerVehicle() const;

    const std::vector<NpcCharacter>& npcs() const { return npcs_; }
    const std::vector<WorldPickup>& pickups() const { return pickups_; }
    const std::vector<BulletTracer>& tracers() const { return tracers_; }
    const std::deque<SkidMark>& skidMarks() const { return skidMarks_; }
    const std::vector<Particle>& particles() const { return particles_; }

    const MissionState& mission() const { return mission_; }
    const BannerNotification& banner() const { return banner_; }

    // Time of day in [0, 24), starting at golden morning 09:30
    float timeOfDayHours() const { return timeOfDayHours_; }
    bool isNightTime() const { return timeOfDayHours_ < 6.2f || timeOfDayHours_ >= 19.3f; }
    float daylightFactor() const;

    // Context prompts near shops / vehicles / missions
    const PointOfInterest* activePoiNearPlayer() const { return activePoi_; }
    bool isPlayerInWeaponShopZone() const;
    bool isPlayerInGarageZone() const;
    const Vehicle* nearestEnterableVehicle(float maxRange = 4.6f) const;

    std::vector<AudioEvent> consumeAudioEvents();

private:
    void spawnInitialWorld();
    void spawnParkedShowroomCars();
    void maintainTrafficPopulation();
    void maintainCivilianPopulation();

    void updatePlayerOnFoot(float dt, const PlayerInput& input);
    void updatePlayerDriving(float dt, const PlayerInput& input, Vehicle& car);
    void updateWeaponCombat(float dt, const PlayerInput& input);
    void fireWeapon(WeaponId weaponId, const Vec2& origin, float aimYaw);

    void updateVehicles(float dt);
    void updateTrafficAi(float dt, Vehicle& v);
    void stepVehiclePhysics(float dt, Vehicle& v, float throttleInput, float steerInput,
                            bool handbrake, bool nitro);
    void resolveVehicleCollisions(float dt);
    void explodeVehicle(Vehicle& v);

    void updateNpcs(float dt);
    void triggerCivilianPanicNear(const Vec2& origin, float radius);
    void damageNpc(NpcCharacter& npc, float dmg, const Vec2& hitDir);

    void updatePickupsAndProps(float dt);
    void updateEffects(float dt);
    void updateMission(float dt);

    void pushBanner(const std::string& titleEn, const std::string& titleKo,
                    const std::string& subEn, const std::string& subKo,
                    ColorRGBA accent, float duration = 3.6f);
    void emitParticles(const Vec3& origin, int count, ColorRGBA color, float speed, float size, float ttl);
    void pushSkidMark(const Vec2& a, const Vec2& b, float alpha = 0.65f);
    void pushAudio(AudioEventType type, float volume = 1.0f);

    Vec2 playerWorldPos() const;

    std::uint64_t seed_ = 20261107ULL;
    Rng rng_;
    CityMap city_;
    PlayerState player_{};

    int nextVehicleId_ = 1;
    int nextNpcId_ = 1;
    std::vector<Vehicle> vehicles_;
    std::vector<NpcCharacter> npcs_;
    std::vector<WorldPickup> pickups_;
    std::vector<BulletTracer> tracers_;
    std::deque<SkidMark> skidMarks_;
    std::vector<Particle> particles_;
    std::vector<AudioEvent> audioEvents_;

    MissionState mission_{};
    std::vector<Vec2> raceCheckpoints_;
    BannerNotification banner_{};
    const PointOfInterest* activePoi_ = nullptr;

    float timeOfDayHours_ = 10.0f; // 10:00 AM bright sunny start
};

} // namespace oc
