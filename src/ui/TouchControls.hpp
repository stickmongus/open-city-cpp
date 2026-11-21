#pragma once

#include "gameplay/GameWorld.hpp"
#include "platform/PlatformApp.hpp"
#include "ui/Vulkan2D.hpp"
#include <array>

namespace oc {

struct TouchControlResult {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    bool sprintOrNitro = false;
    bool handbrake = false;

    float lookDeltaX = 0.0f;
    float lookDeltaY = 0.0f;

    bool fireHeld = false;
    bool aimToggled = false;
    bool lookBackHeld = false;

    bool jumpPressed = false;
    bool interactPressed = false;
    bool reloadPressed = false;
    bool hornPressed = false;
    int weaponCycleDelta = 0;

    bool toggleMapPressed = false;
    bool toggleViewPressed = false;
    bool togglePhonePressed = false;
    bool toggleCustomPressed = false;
    bool toggleShopPressed = false;
    bool toggleWeatherPressed = false;
    bool advanceTimePressed = false;
    bool cycleQualityPressed = false;

    bool modalTapValid = false;
    Vec2 modalTapPos{0.0f, 0.0f};
};

class TouchControls {
public:
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    void setAimToggled(bool active) { aimToggled_ = active; }
    bool isAimToggled() const { return aimToggled_; }
    void resetState();

    TouchControlResult update(const PlatformInput& input, const GameWorld& world,
                              int screenW, int screenH, RectF minimapRect,
                              bool modalOpen, bool simulateMouse, float dt);

    void render(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                bool modalOpen, bool firstPerson, bool useKorean) const;

    struct Layout {
        float s = 1.0f;
        Vec2 stickCenter{0.0f, 0.0f};
        float stickOuterR = 74.0f;
        float stickKnobR = 28.0f;

        RectF steerLeftRect{};
        RectF steerRightRect{};

        Vec2 fireCenter{0.0f, 0.0f};   float fireR = 45.0f;
        Vec2 aimCenter{0.0f, 0.0f};    float aimR = 31.0f;
        Vec2 jumpCenter{0.0f, 0.0f};   float jumpR = 31.0f;
        Vec2 reloadCenter{0.0f, 0.0f}; float reloadR = 27.0f;
        Vec2 interactCenter{0.0f, 0.0f}; float interactR = 31.0f;
        Vec2 shopCenter{0.0f, 0.0f};   float shopR = 31.0f;

        RectF wepPrevRect{};
        RectF wepNextRect{};

        RectF gasRect{};
        RectF brakeRect{};
        RectF nitroRect{};
        RectF driftRect{};
        Vec2 exitCarCenter{0.0f, 0.0f}; float exitCarR = 30.0f;
        Vec2 hornCenter{0.0f, 0.0f};    float hornR = 27.0f;
        Vec2 lookBackCenter{0.0f, 0.0f}; float lookBackR = 27.0f;

        // Top quick buttons: Map, View, Phone, Custom, Weather, Time, Quality
        std::array<RectF, 7> topButtons{};
    };

    static Layout computeLayout(int screenW, int screenH);

private:
    enum class PointerRole : int {
        None = 0,
        MoveStick,
        CameraLook,
        FireAim,
        SteerLeft,
        SteerRight,
        GasPedal,
        BrakePedal,
        DriftBtn,
        NitroBtn,
        LookBackBtn,
        Consumed
    };

    struct TrackedPointer {
        bool active = false;
        int id = -1;
        PointerRole role = PointerRole::None;
        Vec2 startPos{0.0f, 0.0f};
        Vec2 pos{0.0f, 0.0f};
        Vec2 prevPos{0.0f, 0.0f};
    };

    static bool hitCircle(Vec2 p, Vec2 c, float r);
    TrackedPointer* findPointer(int id);
    TrackedPointer* allocatePointer(int id, Vec2 pos);

    bool enabled_ = false;
    bool aimToggled_ = false;
    Vec2 stickVisualOffset_{0.0f, 0.0f};
    bool stickSprinting_ = false;
    bool fireActiveVis_ = false;
    bool steerLeftVis_ = false;
    bool steerRightVis_ = false;
    bool gasVis_ = false;
    bool brakeVis_ = false;
    bool nitroVis_ = false;
    bool driftVis_ = false;
    bool lookBackVis_ = false;
    float smoothTouchSteer_ = 0.0f;
    float smoothTouchThrottle_ = 0.0f;
    std::array<TrackedPointer, 10> pointers_{};
};

} // namespace oc
