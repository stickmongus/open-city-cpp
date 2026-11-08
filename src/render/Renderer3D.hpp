#pragma once

#include "gameplay/GameWorld.hpp"
#include <raylib.h>

namespace oc {

enum class GraphicsQuality : int {
    Ultra = 0,       // 2048px PCF Soft Shadows + Procedural Sky/Clouds/Ocean + HDR Bloom + ACES
    High = 1,        // 2048px PCF Soft Shadows + Procedural Sky/Clouds/Ocean + ACES Filmic
    Performance = 2  // Direct MSAA pass + Procedural Sky/Ocean/Architecture (Max FPS)
};

class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void init();
    void shutdown();

    GraphicsQuality quality() const { return quality_; }
    void setQuality(GraphicsQuality q) { quality_ = q; }
    void cycleQuality();
    const char* qualityNameEn() const;
    const char* qualityNameKo() const;

    Color computeSkyColor(const GameWorld& world) const;

    // Executes shadow-map pass, sky + PBR-lite 3D world pass, and HDR bloom + ACES post-process
    void renderFrame(const GameWorld& world, const Camera3D& camera);
    void renderWorld(const GameWorld& world, const Camera3D& camera);

private:
    enum class MatKind : unsigned char {
        Standard = 0,
        RoadAsphalt = 1,
        SidewalkConcrete = 2,
        GlassSkyscraper = 3,
        MasonryFacade = 4,
        IndustrialMetal = 5,
        OceanWater = 6,
        CarClearcoat = 7,
        CarGlass = 8,
        EmissiveGlow = 9,
        FoliageGrass = 10
    };

    void ensureSceneRenderTarget(int width, int height);
    void setAllModelsShader(const Shader& shader);
    Vector3 computeSunDirection(const GameWorld& world) const;
    Camera3D computeLightCamera(const Vector3& lightDir, const Vec2& focus) const;
    void updateShaderUniforms(const GameWorld& world, const Camera3D& camera,
                              Color skyCol, const Vector3& lightDir,
                              const Matrix& lightVP, bool shadowsActive);

    void drawModelStyled(Model& model, Vector3 pos, Vector3 rotAxis, float rotDeg,
                         Vector3 scale, Color tint, float roughness, float metallic,
                         float emissive, MatKind matKind);

    void drawSkyDome(const GameWorld& world, const Camera3D& camera, const Vector3& lightDir);
    void drawSceneGeometry(const GameWorld& world, const Camera3D& camera, bool shadowPass);
    void drawOceanAndGround(const GameWorld& world);
    void drawRoadNetwork(const GameWorld& world, const Vec2& focus);
    void drawBuildingsAndParks(const GameWorld& world, const Vec2& focus, bool shadowPass);
    void drawTreesAndProps(const GameWorld& world, const Vec2& focus, bool shadowPass);
    void drawVehicle3D(const Vehicle& v, bool nightMode, bool shadowPass);
    void drawCharacter3D(const Vec2& pos, float groundY, float yaw, float animPhase,
                         ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                         bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                         float aimPitch, bool hasSunglasses, bool shadowPass);
    void drawPoisAndPickups(const GameWorld& world);
    void drawSkidsTracersParticles(const GameWorld& world);
    void drawGpsOverheadArrow(const GameWorld& world);

    bool initialized_ = false;
    GraphicsQuality quality_ = GraphicsQuality::Ultra;

    // 1) World PBR-lite + PCF shadow + procedural architecture/water shader
    Shader worldShader_{};
    int locLightDir_ = -1;
    int locSunColor_ = -1;
    int locSkyAmbient_ = -1;
    int locGroundAmbient_ = -1;
    int locFogColor_ = -1;
    int locHorizonColor_ = -1;
    int locViewPos_ = -1;
    int locAnimTime_ = -1;
    int locNightFactor_ = -1;
    int locEnableShadows_ = -1;
    int locLightVP_ = -1;
    int locShadowMap_ = -1;
    int locShadowTexel_ = -1;

    // 2) Fast depth-only shader for 2048x2048 directional shadow map
    Shader depthShader_{};
    RenderTexture2D shadowMap_{};
    bool shadowMapValid_ = false;
    bool shadowPassActive_ = false;

    // 3) Procedural atmospheric sky dome + solar corona + volumetric-style FBM clouds + stars
    Shader skyShader_{};
    int locSkySunDir_ = -1;
    int locSkyTimeHours_ = -1;
    int locSkyDayFactor_ = -1;
    int locSkyAnimTime_ = -1;

    // 4) Full-screen multi-ring Kawase/tent HDR Bloom + ACES Filmic Tonemapping shader
    Shader postShader_{};
    int locPostResolution_ = -1;
    int locPostBloomStrength_ = -1;
    int locPostExposure_ = -1;
    int locPostNitro_ = -1;
    int locPostEnableBloom_ = -1;
    RenderTexture2D sceneTarget_{};
    int sceneTargetW_ = 0;
    int sceneTargetH_ = 0;

    // Reusable GPU-resident procedural meshes
    Model cubeModel_{};
    Model cylModel_{};
    Model coneModel_{};
    Model sphereModel_{};
    Model skyDomeModel_{};
    Model cabinCoupeModel_{};
    Model cabinSedanModel_{};
    Model cabinSuvModel_{};
    Model hoodWedgeModel_{};
    Model wheelModel_{};
};

} // namespace oc
