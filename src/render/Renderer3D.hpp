#pragma once

#include "core/Mat4.hpp"
#include "gameplay/GameWorld.hpp"
#include "render/CarMeshGenerator.hpp"
#include "render/VulkanContext.hpp"
#include <vector>

namespace oc {

enum class GraphicsQuality : int {
    Ultra = 0,       // 2048px 16-Tap PCF Shadows + 16-bit HDR Bloom + ACES + MSAA
    High = 1,        // 2048px 16-Tap PCF Shadows + ACES Filmic + MSAA
    Performance = 2  // Direct MSAA Scene + ACES (Max Battery / FPS)
};

struct CameraState {
    Vec3 position{600.0f, 5.0f, 1030.0f};
    Vec3 target{600.0f, 2.0f, 1010.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fovyDeg = 58.0f;
    float nearZ = 0.25f;
    float farZ = 16000.0f;
};

class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void init(VulkanContext& vk, const CityMap& city);
    void shutdown(VulkanContext& vk);

    GraphicsQuality quality() const { return quality_; }
    void setQuality(GraphicsQuality q) { quality_ = q; }
    void cycleQuality();
    const char* qualityNameEn() const;
    const char* qualityNameKo() const;

    // Runs Pass 1 (2048px Shadow Map), Pass 2 (HDR+MSAA 3D Seoul World),
    // and begins Pass 3 (Post-Process HDR Bloom + ACES) leaving Pass 3 open for 2D HUD!
    void renderSceneAndBeginPost(VulkanContext& vk, const GameWorld& world,
                                 const CameraState& camera,
                                 bool firstPerson = false, float adsBlend = 0.0f,
                                 bool lookBack = false);

private:
    enum class MatId : int {
        Standard = 0,
        RoadAsphalt = 1,
        KoreanApartmentBrick = 2,
        GlassSkyscraper = 3,
        ModernConcrete = 4,
        SidewalkPavers = 5,
        HanRiverWater = 6,
        CarClearcoat = 7,
        CarGlass = 8,
        EmissiveGlow = 9,
        FoliageGrass = 10,
        FabricCloth = 11,
        SkinOrganic = 12,
        WoodBark = 13,
        GraniteRock = 14,
        HanokGiwaRoof = 15,
        KoreanAptFacade = 16
    };

    void buildStaticPrimitives(VulkanContext& vk);
    void buildSeoulMacroMeshes(VulkanContext& vk, const CityMap& city);

    Vec3 computeSunDirection(const GameWorld& world) const;
    SceneUBO buildSceneUBO(const GameWorld& world, const CameraState& camera,
                           int width, int height, const Vec3& lightDir,
                           const Mat4& lightVP, bool shadowsActive) const;

    void drawMesh(VulkanContext& vk, const GpuMesh& mesh, const Mat4& model,
                  ColorRGBA tint, float roughness, float metallic, float emissive,
                  MatId matId, bool shadowPass,
                  WorldPipelineKind pipe = WorldPipelineKind::Opaque);

    void drawBox(VulkanContext& vk, const Vec3& center, const Vec3& size, float yawRad,
                 ColorRGBA tint, float roughness, float metallic, float emissive,
                 MatId matId, bool shadowPass,
                 WorldPipelineKind pipe = WorldPipelineKind::Opaque);

    void drawSeoulTerrainAndRoads(VulkanContext& vk, const GameWorld& world,
                                  const Vec2& focus, bool shadowPass);
    void drawBuildingsAndParks(VulkanContext& vk, const GameWorld& world,
                               const Vec2& focus, bool shadowPass);
    void drawTreesAndProps(VulkanContext& vk, const GameWorld& world,
                           const Vec2& focus, const Vec3& camPos, bool shadowPass);
    void drawVehicle(VulkanContext& vk, const Vehicle& v, float groundY,
                     bool nightMode, bool shadowPass, bool alphaPass,
                     bool cockpitHoodOnly = false);

    struct WeaponPoseState {
        float aimBlend = 0.0f;       // 0 = tactical low-ready carry, 1 = shoulder/eye aim
        float recoilAnim = 0.0f;     // 0..1 shot recoil kickback
        float reloadFrac = 0.0f;     // 0 = not reloading, (0..1] = reload cycle progress
        float equipAnim = 0.0f;      // 0..1 holster draw animation
        float meleeSwingAnim = 0.0f; // 0..1 Fists jab/cross punch
        bool meleeSwingRight = true;
        float minigunSpin = 0.0f;
    };

    void drawDetailedWeapon3D(VulkanContext& vk, WeaponId weapon, const WeaponSlotState* customWep,
                              const Mat4& weaponWorldM, float muzzleFlash, float minigunSpin,
                              bool shadowPass, bool isFirstPerson = false);

    void drawArmIK(VulkanContext& vk, const Mat4& rootM, const Vec3& shoulderLocal,
                   const Vec3& handLocal, const Vec3& elbowPoleLocal,
                   ColorRGBA sleeveCol, ColorRGBA skinCol, bool shadowPass);

    void drawCharacter(VulkanContext& vk, const Vec2& pos, float groundY, float yaw,
                       float animPhase, ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                       bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                       float aimPitch, OutfitId outfit, bool shadowPass,
                       bool jumping = false, bool panicking = false,
                       bool isPolice = false, bool isSwat = false,
                       const CharacterCustomState* customChar = nullptr,
                       const WeaponSlotState* customWep = nullptr,
                       const NpcCharacter* npcPtr = nullptr,
                       bool nightMode = false,
                       const WeaponPoseState* weaponPose = nullptr);
    void drawAlphaAndEffects(VulkanContext& vk, const GameWorld& world,
                             const CameraState& camera, const Vec2& focus);
    void drawFirstPersonOverlay(VulkanContext& vk, const GameWorld& world,
                                const CameraState& camera, float adsBlend);

    bool initialized_ = false;
    GraphicsQuality quality_ = GraphicsQuality::Ultra;

    // Static reusable primitives
    GpuMesh cubeMesh_{};
    GpuMesh cylMesh_{};
    GpuMesh coneMesh_{};
    GpuMesh sphereMesh_{};
    GpuMesh skyBoxMesh_{};
    GpuMesh foliageMesh_{};
    GpuMesh pineMesh_{};
    GpuMesh ringMesh_{};
    GpuMesh giwaRoofMesh_{};
    GpuMesh capsuleLimbMesh_{};
    GpuMesh torsoMesh_{};

    // Real-Scale 30km x 25km Seoul Static Macro Meshes (built once on init!)
    GpuMesh hanRiverWaterMesh_{};
    GpuMesh hanEmbankmentMesh_{};
    GpuMesh tributaryWaterMesh_{};
    GpuMesh riversideExpresswayMesh_{};
    GpuMesh mountainForestMesh_{};
    GpuMesh mountainGraniteMesh_{};
    GpuMesh bridgeDecksMesh_{};
    GpuMesh bridgeConcreteMesh_{};
    GpuMesh bridgeSteelMesh_{};
    GpuMesh landmarkGlassMesh_{};
    GpuMesh landmarkStoneMesh_{};
    GpuMesh landmarkMetalMesh_{};
    GpuMesh landmarkGiwaMesh_{};
    GpuMesh landmarkGlowMesh_{};
    GpuMesh banpoFountainMesh_{};

    // Distant Seoul Skyline LOD massing clusters (drawn beyond 490m so streets never end in an empty void!)
    struct DistantSkylineBlock {
        Vec2 pos{};
        Vec2 size{};
        float baseY = 0.0f;
        float height = 45.0f;
        ColorRGBA col{};
        bool isGlass = false;
    };
    std::vector<DistantSkylineBlock> distantSkyline_;

    // 16 CAD-precision zero-jelly vehicles + sculpted 10-spoke wheels
    CarMeshLibrary cars_{};
    mutable std::vector<int> visIdxBuf_;
};

} // namespace oc
