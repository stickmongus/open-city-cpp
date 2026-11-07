#pragma once

#include "core/Math.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace oc {

enum class VehicleKind : int {
    SportsCoupe = 0,
    MuscleCar = 1,
    SUV = 2,
    Taxi = 3,
    Compact = 4,
    DeliveryVan = 5,
    Count = 6
};

struct VehicleSpec {
    VehicleKind kind = VehicleKind::SportsCoupe;
    const char* nameEn = "APEX GT";
    const char* nameKo = "에이펙스 GT 스포츠카";
    float halfLength = 2.25f;
    float halfWidth = 1.02f;
    float chassisHeight = 1.18f;
    float maxForwardSpeed = 42.0f;
    float maxReverseSpeed = 14.0f;
    float acceleration = 24.0f;
    float braking = 34.0f;
    float turnRate = 2.55f;
    float normalGrip = 9.5f;
    float driftGrip = 2.15f;
    float mass = 1350.0f;
    float maxHealth = 160.0f;
    bool hasSpoiler = false;
};

const VehicleSpec& getVehicleSpec(VehicleKind kind);

struct Vehicle {
    int id = 0;
    VehicleKind kind = VehicleKind::Compact;
    Vec2 pos{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    float heading = 0.0f;
    float steerVisual = 0.0f;    // [-1, 1] front wheel turn visual
    float wheelSpin = 0.0f;      // accumulated wheel rotation radians
    float bodyPitch = 0.0f;      // acceleration/braking pitch
    float bodyRoll = 0.0f;       // cornering/drift roll
    float health = 160.0f;
    float maxHealth = 160.0f;
    ColorRGBA paintColor{220, 48, 52, 255};
    ColorRGBA stripeColor{245, 245, 250, 255};

    bool hasNpcDriver = false;
    bool isPlayerDriving = false;
    bool isParked = false;
    bool isDrifting = false;
    bool nitroActive = false;
    bool brakingVisual = false;
    bool destroyed = false;
    float destroyedTimer = 0.0f;

    int engineUpgradeLevel = 0;  // 0..3 from Tune Garage
    float nitroCharge = 1.0f;    // [0, 1]

    // Traffic AI lane-following state
    bool aiHorizontalRoad = true;
    int aiRoadIndex = 4;
    int aiDirection = 1;         // +1 or -1 along axis
    int lastTurnIntersectionKey = -1;
    float hornCooldown = 0.0f;

    // Mission tag
    bool isMissionTarget = false;

    OBB2D obb() const {
        const auto& spec = getVehicleSpec(kind);
        return OBB2D{pos, {spec.halfLength, spec.halfWidth}, heading};
    }

    float speed() const { return velocity.length(); }
};

enum class WeaponId : int {
    Fists = 0,
    Pistol = 1,
    SMG = 2,
    Shotgun = 3,
    AssaultRifle = 4,
    Count = 5
};

struct WeaponSpec {
    WeaponId id = WeaponId::Pistol;
    const char* nameEn = "Pistol";
    const char* nameKo = "9mm 권총";
    float fireInterval = 0.24f;
    float range = 58.0f;
    float damage = 34.0f;
    int pellets = 1;
    float spreadRadians = 0.025f;
    int magazineSize = 12;
    float reloadDuration = 1.15f;
    bool automatic = false;
};

const WeaponSpec& getWeaponSpec(WeaponId id);

struct WeaponSlotState {
    bool owned = false;
    int magAmmo = 0;
    int reserveAmmo = 0;
};

struct PlayerState {
    Vec2 pos{15.5f, 15.5f};
    Vec2 velocity{0.0f, 0.0f};
    float facingYaw = -kHalfPi;
    float walkAnimPhase = 0.0f;
    float health = 100.0f;
    float maxHealth = 100.0f;
    float armor = 25.0f;
    float maxArmor = 100.0f;
    float stamina = 1.0f;
    int cash = 750;

    int drivingVehicleId = -1; // -1 when on foot
    WeaponId currentWeapon = WeaponId::Pistol;
    std::array<WeaponSlotState, static_cast<std::size_t>(WeaponId::Count)> weapons{};
    float fireCooldown = 0.0f;
    float reloadTimer = 0.0f;
    float muzzleFlashTimer = 0.0f;
    float aimPitch = 0.0f;
    float hitMarkerTimer = 0.0f;
    bool hitMarkerCritical = false;
    bool crosshairOnTarget = false;

    bool wasted = false;
    float wastedTimer = 0.0f;

    // Live street drift combo scoring
    float activeDriftScore = 0.0f;
    float driftBankTimer = 0.0f;
};

enum class NpcRole {
    Civilian,
    Gangster
};

enum class NpcBehavior {
    Stroll,
    PanicFlee,
    CombatEngage
};

struct NpcCharacter {
    int id = 0;
    NpcRole role = NpcRole::Civilian;
    NpcBehavior behavior = NpcBehavior::Stroll;
    Vec2 pos{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    float facingYaw = 0.0f;
    float animPhase = 0.0f;
    float health = 65.0f;
    float maxHealth = 65.0f;
    float fireCooldown = 0.8f;
    float stateTimer = 0.0f;
    ColorRGBA outfitTop{120, 140, 180, 255};
    ColorRGBA outfitBottom{45, 50, 62, 255};
    ColorRGBA skinColor{232, 192, 158, 255};
    bool alive = true;
    float corpseTimer = 0.0f;
    bool missionTarget = false;

    // Sidewalk & crosswalk lattice navigation / turf patrol state
    int navFromU = 8;
    int navFromV = 8;
    int navToU = 9;
    int navToV = 8;
    float walkSpeed = 1.65f;
    float idlePauseTimer = 0.0f;
    Vec2 homeAnchor{0.0f, 0.0f};
    bool waitingAtCrosswalk = false;
};

enum class PickupKind {
    CashBundle,
    HealthKit,
    MissionBriefcase
};

struct WorldPickup {
    PickupKind kind = PickupKind::CashBundle;
    Vec2 pos{0.0f, 0.0f};
    int amount = 100;
    float bobPhase = 0.0f;
    bool collected = false;
};

struct BulletTracer {
    Vec3 start{};
    Vec3 end{};
    ColorRGBA color{255, 232, 135, 255};
    float ttl = 0.08f;
    float maxTtl = 0.08f;
};

struct SkidMark {
    Vec2 a{};
    Vec2 b{};
    float alpha = 0.65f;
};

struct Particle {
    Vec3 pos{};
    Vec3 vel{};
    ColorRGBA color{};
    float size = 0.25f;
    float ttl = 0.5f;
    float maxTtl = 0.5f;
};

enum class ActiveMissionKind {
    None = 0,
    ExoticDelivery,
    StreetSprint,
    GangRaid
};

struct MissionState {
    ActiveMissionKind kind = ActiveMissionKind::None;
    int stage = 0;
    float timeRemaining = 0.0f;
    int rewardCash = 0;
    int targetVehicleId = -1;
    int checkpointsCleared = 0;
    int totalCheckpoints = 0;
    int enemiesRemaining = 0;
    Vec2 currentObjectivePos{0.0f, 0.0f};
    float objectiveRadius = 6.0f;
    std::string objectiveEn;
    std::string objectiveKo;
    int completedCount = 0;
};

struct BannerNotification {
    std::string titleEn;
    std::string titleKo;
    std::string subEn;
    std::string subKo;
    ColorRGBA accent{255, 215, 64, 255};
    float timer = 0.0f;
};

enum class AudioEventType {
    PistolShot,
    SmgShot,
    ShotgunShot,
    RifleShot,
    MeleeSwing,
    CarDoor,
    CarHorn,
    CarCrash,
    Explosion,
    CashPickup,
    ShopBuy,
    MissionStart,
    MissionComplete,
    MissionFail
};

struct AudioEvent {
    AudioEventType type = AudioEventType::PistolShot;
    float volume = 1.0f;
};

struct PlayerInput {
    float moveForward = 0.0f;   // [-1, 1] W/S
    float moveRight = 0.0f;     // [-1, 1] D/A
    float cameraYaw = 0.0f;     // Horizontal camera yaw radians
    float aimYaw = 0.0f;        // World aim direction yaw on XZ plane
    float aimPitch = 0.0f;      // World aim elevation radians (-down, +up)
    bool hasAimRay3D = false;   // True when 3D camera crosshair ray is supplied
    Vec3 aimRayOrigin{0.0f, 0.0f, 0.0f};
    Vec3 aimRayDir{1.0f, 0.0f, 0.0f};
    bool sprintOrNitro = false; // Left Shift
    bool handbrake = false;     // Space
    bool interactPressed = false; // F or E
    bool fireHeld = false;      // LMB
    bool aimHeld = false;       // RMB
    bool reloadPressed = false; // R
    bool hornPressed = false;   // G
    bool abortMissionPressed = false; // Backspace
    int weaponSelectSlot = -1;  // 0..4 when 1..5 pressed (outside shop menu)
    int shopBuyOption = -1;     // 1..6 when inside shop menu
};

} // namespace oc
