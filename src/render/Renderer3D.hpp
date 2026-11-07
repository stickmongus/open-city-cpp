#pragma once

#include "gameplay/GameWorld.hpp"
#include <raylib.h>

namespace oc {

class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void init();
    void shutdown();

    Color computeSkyColor(const GameWorld& world) const;
    void renderWorld(const GameWorld& world, const Camera3D& camera);

private:
    void updateShaderUniforms(const GameWorld& world, const Camera3D& camera, Color skyCol);
    void drawOceanAndGround(const GameWorld& world);
    void drawRoadNetwork(const GameWorld& world, const Vec2& focus);
    void drawBuildingsAndParks(const GameWorld& world, const Vec2& focus);
    void drawTreesAndProps(const GameWorld& world, const Vec2& focus);
    void drawVehicle3D(const Vehicle& v, bool nightMode);
    void drawCharacter3D(const Vec2& pos, float yaw, float animPhase,
                         ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                         bool alive, bool armed, WeaponId weapon, float muzzleFlash);
    void drawPoisAndPickups(const GameWorld& world);
    void drawSkidsTracersParticles(const GameWorld& world);
    void drawGpsOverheadArrow(const GameWorld& world);

    bool initialized_ = false;
    Shader worldShader_{};
    int locLightDir_ = -1;
    int locSunColor_ = -1;
    int locAmbientColor_ = -1;
    int locFogColor_ = -1;
    int locViewPos_ = -1;
    int locNightEmissive_ = -1;

    Model unitCubeModel_{};
    Model windowBuildingModel_{};
    Texture2D buildingDayTex_{};
    Texture2D buildingNightTex_{};
};

} // namespace oc
