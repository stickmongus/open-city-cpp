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

    bool loadRealSeoulMap(const std::uint8_t* bytes, std::size_t size);
    bool loadRealSeoulMapFromFile(const std::string& path);
    bool loadOcrmRoadMesh(const std::uint8_t* bytes, std::size_t size);
    bool loadOcrmRoadMeshFromFile(const std::string& path);

    void reset();
    void update(float dt, const PlayerInput& input);

    // Direct helpers (also handy for automated testing & UI actions)
    bool tryEnterOrExitVehicle();
    bool purchaseShopItem(int optionOneBased);
    bool executePhoneAction(int tabIndex, int optionOneBased);
    bool executeCustomStudioAction(int tabIndex, int optionOneBased);
    bool buyOrSummonVehicle(VehicleKind kind);
    bool buyProperty(PropertyId id);
    bool buyOrEquipOutfit(OutfitId id);
    bool startMissionFromPoi(PoiKind poiKind);
    void advanceTimeOfDayHours(float hours);

    int passiveIncomePerCycle() const;
    int ownedPropertyCount() const;
    float tuningDiscountScale() const;

    // 3D Ground, Seoul Mountains & Real Grade-Separated 3D Bridges/Viaducts Elevation + Realistic Escapable Police AI
    float surfaceHeightAt(const Vec2& pos, float refY = -1e9f) const;
    float computePlayerSupportHeight(const Vec2& pos, float currentY, Vec2* platformCarryVel = nullptr) const;
    void reportCrime(float heatDelta, int minStars, const Vec2& crimePos, bool directWitness = false);
    void clearWantedLevel(bool showToast = true);
    float requiredEvadeDuration() const;

    // Interactive Full-Screen Seoul Map Waypoint & Fast Travel
    void setWaypoint(const Vec2& pos, const std::string& labelKo = "");
    void clearWaypoint();
    bool fastTravelTo(const Vec2& targetPos, const std::string& labelKo = "");

    const CityMap& city() const { return city_; }
    CityMap& city() { return city_; }

    const PlayerState& player() const { return player_; }
    PlayerState& player() { return player_; }

    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    std::vector<Vehicle>& vehicles() { return vehicles_; }
    const Vehicle* findVehicle(int id) const;
    Vehicle* findVehicleMutable(int id);
    const Vehicle* playerVehicle() const;
    Vehicle* customizationTargetVehicle();
    const Vehicle* customizationTargetVehicle() const;

    const std::vector<NpcCharacter>& npcs() const { return npcs_; }
    std::vector<NpcCharacter>& npcs() { return npcs_; }
    const std::vector<WorldPickup>& pickups() const { return pickups_; }
    const std::vector<BulletTracer>& tracers() const { return tracers_; }
    const std::deque<SkidMark>& skidMarks() const { return skidMarks_; }
    const std::vector<Particle>& particles() const { return particles_; }

    const MissionState& mission() const { return mission_; }
    const BannerNotification& banner() const { return banner_; }
    void clearBanner() { banner_.timer = 0.0f; }

    // Time of day in [0, 24), starting at golden morning 10:00 + Dynamic Rain/Wet-Asphalt state
    float timeOfDayHours() const { return timeOfDayHours_; }
    void setTimeOfDayHours(float h) {
        timeOfDayHours_ = std::fmod(h, 24.0f);
        if (timeOfDayHours_ < 0.0f) timeOfDayHours_ += 24.0f;
    }
    bool isNightTime() const { return timeOfDayHours_ < 6.2f || timeOfDayHours_ >= 19.3f; }
    bool isNight() const { return isNightTime(); }
    float daylightFactor() const;
    float elapsedTime() const { return elapsedTime_; }
    float wetRoadFactor() const { return wetRoadFactor_; }
    float rainIntensity() const { return rainWeather_ ? 0.85f : 0.0f; }
    void setWetRoadFactor(float w) { wetRoadFactor_ = std::clamp(w, 0.0f, 1.0f); }
    bool isRainWeather() const { return rainWeather_; }
    const char* weatherNameEn() const { return rainWeather_ ? "Rain" : (wetRoadFactor_ > 0.35f ? "Wet" : "Clear"); }
    const char* weatherNameKo() const { return rainWeather_ ? "비" : (wetRoadFactor_ > 0.35f ? "젖은 노면" : "맑음"); }
    void setRainWeather(bool rain) {
        rainWeather_ = rain;
        if (rain) wetRoadFactor_ = std::max(wetRoadFactor_, 0.85f);
        for (auto& npc : npcs_) {
            if (npc.role == NpcRole::Civilian) {
                npc.holdingUmbrella = rainWeather_ && ((npc.id * 37) % 100) < 62;
            }
        }
    }
    void toggleRainWeather();
    float nearestPoliceSirenIntensity() const {
        float best = 0.0f;
        const Vec2 p = player_.pos;
        for (const auto& v : vehicles_) {
            if (!v.sirenActive || v.destroyed) continue;
            const float d = (v.pos - p).length();
            if (d < 140.0f) best = std::max(best, 1.0f - d / 140.0f);
        }
        return best;
    }

    Vehicle& spawnCustomVehicle(VehicleKind kind, const Vec2& pos, float heading, ColorRGBA paint, float refY = -1e9f) {
        Vehicle v{};
        v.id = nextVehicleId_++;
        v.kind = kind;
        const auto& sp = getVehicleSpec(kind);
        v.pos = pos;
        v.posY = city_.surfaceHeightAt(pos, refY);
        v.heading = heading;
        v.paintColor = paint;
        v.health = sp.maxHealth;
        v.maxHealth = sp.maxHealth;
        v.isParked = true;
        v.isPolice = (kind == VehicleKind::PoliceCruiser);
        vehicles_.push_back(v);
        return vehicles_.back();
    }

    bool enterVehicle(int vehicleId) {
        Vehicle* v = findVehicleMutable(vehicleId);
        if (!v || v->destroyed) return false;
        if (player_.drivingVehicleId >= 0) {
            if (Vehicle* old = findVehicleMutable(player_.drivingVehicleId)) old->isPlayerDriving = false;
        }
        v->isPlayerDriving = true;
        v->hasNpcDriver = false;
        v->isParked = false;
        player_.drivingVehicleId = v->id;
        player_.pos = v->pos;
        player_.posY = v->posY;
        return true;
    }

    // City-wide traffic & pedestrian signal phases (24s cycle)
    bool isTrafficGreenForHorizontal() const;
    bool isTrafficGreenForVertical() const;

    // Context prompts near shops / vehicles / missions
    const PointOfInterest* activePoiNearPlayer() const { return activePoi_; }
    const PointOfInterest* nearbyPoi() const { return activePoi_; }
    bool isPlayerInWeaponShopZone() const;
    bool isPlayerInGarageZone() const;
    bool isPlayerInDealershipZone() const;
    bool isPlayerInBoutiqueZone() const;
    bool isPlayerInGasStationZone() const;
    bool isPlayerInNightclubZone() const;
    bool isPlayerInAnyShopZone() const;
    const Vehicle* nearestEnterableVehicle(float maxRange = 4.6f) const;

    std::vector<AudioEvent> consumeAudioEvents();
    void pushBanner(const std::string& titleEn, const std::string& titleKo,
                    const std::string& subEn, const std::string& subKo,
                    ColorRGBA accent, float duration = 3.6f);

    // Live Graphics Settings: Traffic & Pedestrian Crowd Density (0=Low, 1=Medium, 2=High, 3=Crowded)
    void setTrafficDensity(int level);
    int trafficDensity() const { return trafficDensity_; }

private:
    struct RayHit3D {
        enum class Kind {
            None,
            Ground,
            Building,
            Npc,
            Vehicle,
            Prop
        };

        Kind kind = Kind::None;
        float dist = 0.0f;
        Vec3 point{};
        NpcCharacter* npc = nullptr;
        bool headshot = false;
        Vehicle* vehicle = nullptr;
        StreetProp* prop = nullptr;
    };

    void spawnInitialWorld();
    void spawnParkedShowroomCars();
    void maintainTrafficPopulation();
    void maintainCivilianPopulation();
    void maintainPolicePopulation(float dt);
    void updateWantedSystem(float dt);
    void updatePoliceVehicleAi(float dt, Vehicle& v);
    void spawnPoliceOfficerNear(const Vec2& pos, bool swat);

    void recalculatePlayerPropertyPerks();
    void syncPersonalTuningFromVehicle(const Vehicle& car);
    void applyPersonalTuningToVehicle(Vehicle& car, const PersonalVehicleTuning& tune);
    void triggerExplosiveBlast3D(const Vec3& center, float radius, float baseDamage);
    void applyDamageToPlayer(float rawDamage);

    void updatePlayerOnFoot(float dt, const PlayerInput& input);
    void updatePlayerDriving(float dt, const PlayerInput& input, Vehicle& car);
    void updateWeaponCombat(float dt, const PlayerInput& input);
    RayHit3D traceWorldRay3D(const Vec3& origin, const Vec3& dir, float maxDist);
    void fireWeapon(WeaponId weaponId, const Vec3& muzzle3D, const Vec3& target3D, bool precisionAim);

    void updateVehicles(float dt);
    void updateTrafficAi(float dt, Vehicle& v);
    void stepVehiclePhysics(float dt, Vehicle& v, float throttleInput, float steerInput,
                            bool handbrake, bool nitro);
    void resolveVehicleCollisions(float dt);
    void explodeVehicle(Vehicle& v);

    void updateNpcs(float dt);
    bool isCrosswalkSafeForPedestrian(int fromU, int fromV, int toU, int toV) const;
    void chooseNextSidewalkTarget(NpcCharacter& npc, bool fleeing, const Vec2& dangerPos);
    void triggerCivilianPanicNear(const Vec2& origin, float radius);
    void damageNpc(NpcCharacter& npc, float dmg, const Vec2& hitDir);

    void updatePickupsAndProps(float dt);
    void updateEffects(float dt);
    void updateMission(float dt);

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

    float timeOfDayHours_ = 14.5f; // 14:30 crisp afternoon Seoul sunlight & shadows
    float elapsedTime_ = 0.0f;
    float wetRoadFactor_ = 0.0f;
    float trafficSignalTimer_ = 0.0f; // [0, 24) intersection traffic/crosswalk cycle
    float policeSpawnCooldown_ = 0.0f;
    float panicScreamCooldown_ = 0.0f;
    bool rainWeather_ = false;
    int trafficDensity_ = 2; // 0=Low, 1=Medium, 2=High (default), 3=Crowded

    void resyncWorldHeightsAfterOcrmLoad();
};

} // namespace oc
