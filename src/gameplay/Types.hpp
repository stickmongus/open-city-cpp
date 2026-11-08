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
    Hypercar = 6,
    ArmoredSuv = 7,
    LuxurySedan = 8,
    PoliceCruiser = 9,
    Count = 10
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
    int dealershipPrice = 0; // 0 = non-dealership commercial vehicle
};

const VehicleSpec& getVehicleSpec(VehicleKind kind);

struct PersonalVehicleTuning {
    bool owned = false;
    ColorRGBA paintColor{228, 42, 58, 255};
    ColorRGBA stripeColor{245, 245, 250, 255};
    int engineUpgradeLevel = 0; // 0..3
    int armorUpgradeLevel = 0;  // 0..3
    int neonPreset = 0;         // 0=Off, 1=Cyan, 2=Purple, 3=Crimson, 4=Emerald, 5=Gold
    int rimStyle = 0;           // 0=Silver Alloy, 1=Champagne Gold, 2=Gloss Black
    bool customSpoiler = false;
    bool mattePaint = false;
    int paintPresetIndex = 0;
};

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
    int armorUpgradeLevel = 0;   // 0..3 reinforced armor & bulletproof tires
    int neonPreset = 0;          // 0..5 LED underglow color
    int rimStyle = 0;            // 0=Alloy, 1=Gold, 2=Black
    bool customSpoiler = false;
    bool mattePaint = false;
    bool isPersonalCar = false;
    int paintPresetIndex = 0;
    float nitroCharge = 1.0f;    // [0, 1]

    // Traffic AI lane-following state
    bool aiHorizontalRoad = true;
    int aiRoadIndex = 4;
    int aiDirection = 1;         // +1 or -1 along axis
    int lastTurnIntersectionKey = -1;
    float hornCooldown = 0.0f;

    // Mission / Law-Enforcement / Driver panic tags
    bool isMissionTarget = false;
    bool isPolice = false;
    bool sirenActive = false;
    float sirenPhase = 0.0f;
    float aiStuckTimer = 0.0f;
    float aiReverseTimer = 0.0f;
    bool dismountedOfficer = false;
    bool driverPanicked = false;
    float driverPanicTimer = 0.0f;

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
    CombatMG = 5,
    HeavyLauncher = 6,
    Count = 7
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
    bool explosive = false;
};

const WeaponSpec& getWeaponSpec(WeaponId id);

enum class PropertyId : int {
    SunsetCondo = 0,
    GlobeOilGas = 1,
    PayNTuneFranchise = 2,
    PulseNightclub = 3,
    LuxuryShowroom = 4,
    EclipsePenthouse = 5,
    Count = 6
};

struct PropertySpec {
    PropertyId id = PropertyId::SunsetCondo;
    const char* nameEn = "Sunset Beach Condo";
    const char* nameKo = "선셋 비치 오션뷰 콘도";
    const char* perkEn = "Max HP +25";
    const char* perkKo = "최대 체력 +25";
    int price = 2500;
    int incomePerCycle = 220; // Paid automatically every 25s
    Vec2 worldPos{-210.0f, -126.0f};
};

const PropertySpec& getPropertySpec(PropertyId id);

enum class OutfitId : int {
    StreetCasual = 0,
    ViceWhiteSuit = 1,
    MidnightTuxedo = 2,
    RacerLeather = 3,
    TacOpsArmor = 4,
    GoldBossCoat = 5,
    Count = 6
};

struct OutfitSpec {
    OutfitId id = OutfitId::StreetCasual;
    const char* nameEn = "Street Jacket";
    const char* nameKo = "스트리트 캐주얼 재킷";
    int price = 0;
    ColorRGBA top{48, 126, 225, 255};
    ColorRGBA bottom{36, 40, 52, 255};
    ColorRGBA accent{230, 236, 245, 255};
    bool sunglasses = true;
    bool suitTie = false;
    bool tacticalVest = false;
    bool longCoat = false;
    float damageTakenScale = 1.0f;
};

const OutfitSpec& getOutfitSpec(OutfitId id);

// 6 Dealership / Garage personal vehicle slots
inline constexpr std::array<VehicleKind, 6> kDealershipCatalog = {{
    VehicleKind::Compact,
    VehicleKind::MuscleCar,
    VehicleKind::SportsCoupe,
    VehicleKind::LuxurySedan,
    VehicleKind::ArmoredSuv,
    VehicleKind::Hypercar
}};

ColorRGBA getNeonPresetColor(int neonPreset);

struct WeaponSlotState {
    bool owned = false;
    int magAmmo = 0;
    int reserveAmmo = 0;
};

struct PlayerState {
    Vec2 pos{15.5f, 15.5f};
    float posY = 0.0f;          // 3D vertical height above sea/road datum (jump & roof climbing!)
    float verticalVel = 0.0f;   // Upward jump velocity (m/s)
    bool onGround = true;
    float coyoteTimer = 0.0f;
    Vec2 velocity{0.0f, 0.0f};
    float facingYaw = -kHalfPi;
    float walkAnimPhase = 0.0f;
    float health = 100.0f;
    float maxHealth = 100.0f;
    float armor = 25.0f;
    float maxArmor = 100.0f;
    float stamina = 1.0f;
    int cash = 2500;

    // SBPD Wanted Level (0..5 stars) & Pursuit Evasion State
    int wantedLevel = 0;
    float wantedHeat = 0.0f;
    float wantedEvadeTimer = 0.0f;
    bool policeLineOfSight = false;
    bool lastDeathWasBusted = false;

    int drivingVehicleId = -1; // -1 when on foot
    int activePersonalVehicleId = -1;
    WeaponId currentWeapon = WeaponId::Pistol;
    std::array<WeaponSlotState, static_cast<std::size_t>(WeaponId::Count)> weapons{};
    std::array<PersonalVehicleTuning, static_cast<std::size_t>(VehicleKind::Count)> garage{};
    std::array<bool, static_cast<std::size_t>(PropertyId::Count)> ownedProperties{};
    std::array<bool, static_cast<std::size_t>(OutfitId::Count)> ownedOutfits{};
    OutfitId currentOutfit = OutfitId::StreetCasual;

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

    // Passive business / real-estate income tracking
    float propertyPayoutTimer = 0.0f;
};

enum class NpcRole {
    Civilian,
    Gangster,
    Police
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
    float muzzleFlashTimer = 0.0f;
    float stateTimer = 0.0f;
    WeaponId weapon = WeaponId::Pistol;
    bool isSwat = false;
    Vec2 panicOrigin{0.0f, 0.0f};
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
    MissionFail,
    Jump,
    NpcScream,
    PoliceDispatch
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
    bool handbrake = false;     // Space (held: vehicle handbrake)
    bool jumpPressed = false;   // Space (pressed: on-foot 3D jump)
    bool interactPressed = false; // F or E
    bool fireHeld = false;      // LMB
    bool aimHeld = false;       // RMB
    bool reloadPressed = false; // R
    bool hornPressed = false;   // G
    bool abortMissionPressed = false; // Backspace
    int weaponSelectSlot = -1;  // 0..6 when 1..7 pressed (outside shop/phone menu)
    int shopBuyOption = -1;     // 1..8 when inside shop menu
    int phoneTab = -1;          // 0..3 when Smartphone action triggered
    int phoneOption = -1;       // 1..7 when Smartphone action triggered
};

} // namespace oc
