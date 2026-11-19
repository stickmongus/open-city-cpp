#pragma once

#include "gameplay/GameWorld.hpp"
#include <array>
#include <functional>
#include <raylib.h>

namespace oc {

struct TouchControlResult {
    // Analog movement (on-foot) or steering / throttle (vehicle)
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    bool sprintOrNitro = false;
    bool handbrake = false;

    // Direct touch-drag look deltas in screen pixels
    float lookDeltaX = 0.0f;
    float lookDeltaY = 0.0f;

    // Continuous combat / camera states
    bool fireHeld = false;
    bool aimToggled = false;
    bool lookBackHeld = false;

    // One-shot button events
    bool jumpPressed = false;
    bool interactPressed = false;
    bool reloadPressed = false;
    bool hornPressed = false;
    int weaponCycleDelta = 0;

    // Top toolbar quick actions
    bool toggleViewPressed = false;
    bool togglePhonePressed = false;
    bool toggleCustomPressed = false;
    bool toggleShopPressed = false;
    bool toggleWeatherPressed = false;
    bool advanceTimePressed = false;
    bool cycleQualityPressed = false;

    // Tap inside an active modal (shop, smartphone, custom studio)
    bool modalTapValid = false;
    Vector2 modalTapPos{0.0f, 0.0f};
};

class TouchControls {
public:
    using TextDrawFn = std::function<void(const char* text, float x, float y, float fontSize, Color color)>;
    using TextMeasureFn = std::function<float(const char* text, float fontSize)>;

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    void setAimToggled(bool active) { aimToggled_ = active; }
    bool isAimToggled() const { return aimToggled_; }
    void resetState();

    // Processes multi-touch pointers (plus mouse tap/drag when simulateMouse is true on desktop preview)
    TouchControlResult update(const GameWorld& world, int screenW, int screenH,
                              bool modalOpen, bool studioOpen, bool simulateMouse);

    // Renders translucent ergonomic mobile controls matching current HUD theme & language
    void render(const GameWorld& world, int screenW, int screenH,
                bool modalOpen, bool firstPerson, bool useKorean,
                const TextDrawFn& drawText, const TextMeasureFn& measureText) const;

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
        Vector2 startPos{0.0f, 0.0f};
        Vector2 pos{0.0f, 0.0f};
        Vector2 prevPos{0.0f, 0.0f};
    };

    struct Layout {
        float s = 1.0f;
        // On-foot left joystick
        Vector2 stickCenter{0.0f, 0.0f};
        float stickOuterR = 82.0f;
        float stickKnobR = 32.0f;

        // Driving left steering pads
        Rectangle steerLeftRect{};
        Rectangle steerRightRect{};

        // On-foot right action circles
        Vector2 fireCenter{0.0f, 0.0f};
        float fireR = 46.0f;
        Vector2 aimCenter{0.0f, 0.0f};
        float aimR = 33.0f;
        Vector2 jumpCenter{0.0f, 0.0f};
        float jumpR = 32.0f;
        Vector2 reloadCenter{0.0f, 0.0f};
        float reloadR = 27.0f;
        Vector2 interactCenter{0.0f, 0.0f};
        float interactR = 33.0f;
        Vector2 shopCenter{0.0f, 0.0f};
        float shopR = 33.0f;

        // Weapon prev / next compact buttons (on-foot)
        Rectangle wepPrevRect{};
        Rectangle wepNextRect{};

        // Driving right pedals & action buttons
        Rectangle gasRect{};
        Rectangle brakeRect{};
        Rectangle nitroRect{};
        Rectangle driftRect{};
        Vector2 exitCarCenter{0.0f, 0.0f};
        float exitCarR = 32.0f;
        Vector2 hornCenter{0.0f, 0.0f};
        float hornR = 28.0f;
        Vector2 lookBackCenter{0.0f, 0.0f};
        float lookBackR = 28.0f;

        // Top-center utility quick pills: View, Phone, Custom, Weather, Time, Quality
        std::array<Rectangle, 6> topButtons{};
    };

    static Layout computeLayout(int screenW, int screenH, bool wantedActive);
    static bool hitCircle(Vector2 p, Vector2 c, float r);
    static bool hitRect(Vector2 p, Rectangle r);
    TrackedPointer* findPointer(int id);
    TrackedPointer* allocatePointer(int id, Vector2 pos);

    bool enabled_ = false;
    bool aimToggled_ = false;
    Vector2 stickVisualOffset_{0.0f, 0.0f};
    bool stickSprinting_ = false;
    bool fireActiveVis_ = false;
    bool steerLeftVis_ = false;
    bool steerRightVis_ = false;
    bool gasVis_ = false;
    bool brakeVis_ = false;
    bool nitroVis_ = false;
    bool driftVis_ = false;
    bool lookBackVis_ = false;
    std::array<TrackedPointer, 10> pointers_{};
};

} // namespace oc
