#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

#if defined(__ANDROID__)
struct android_app;
#endif

namespace oc {

enum class KeyCode : int {
    Unknown = 0,
    W, A, S, D,
    Up, Down, Left, Right,
    Space, Shift, Ctrl,
    E, F, G, H, K, M, P, Q, R, T, U, V, Y, Z,
    Tab, Enter, Escape, Backspace,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Minus, Equal,
    F1, F2, F3, F4,
    Count
};

struct TouchPointState {
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
};

struct PlatformInput {
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> keyDown{};
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> keyPressed{};

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float scrollDeltaY = 0.0f;
    bool mouseLeftDown = false;
    bool mouseLeftPressed = false;
    bool mouseRightDown = false;
    bool mouseRightPressed = false;

    std::array<TouchPointState, 8> touches{};
    int touchCount = 0;

    bool backPressed = false;

    bool isDown(KeyCode k) const { return keyDown[static_cast<std::size_t>(k)]; }
    bool isPressed(KeyCode k) const { return keyPressed[static_cast<std::size_t>(k)]; }
};

class PlatformApp {
public:
    PlatformApp();
    ~PlatformApp();

#if defined(__ANDROID__)
    bool initAndroid(struct android_app* app);
#else
    bool initDesktop(int width, int height, const char* title);
#endif
    void shutdown();

    // Pumps OS events, returns false when app should exit
    bool pollEvents();
    bool isWindowReady() const;
    bool consumeSurfaceRecreated();
    bool consumeResize();

    int screenWidth() const { return screenW_; }
    int screenHeight() const { return screenH_; }
    double timeSeconds() const;
    float deltaTime() const { return dt_; }

    const PlatformInput& input() const { return input_; }
    void setCursorLocked(bool locked);
    bool isCursorLocked() const { return cursorLocked_; }

    std::vector<const char*> getVulkanInstanceExtensions() const;
    bool createVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface) const;

    // Reads bundled font/asset bytes (AAssetManager on Android, filesystem on Desktop)
    std::vector<std::uint8_t> loadAssetOrSystemFont() const;

    bool isAndroid() const {
#if defined(__ANDROID__)
        return true;
#else
        return false;
#endif
    }

private:
    int screenW_ = 1440;
    int screenH_ = 860;
    float dt_ = 1.0f / 60.0f;
    double lastTime_ = 0.0;
    bool cursorLocked_ = false;
    bool resized_ = false;
    bool surfaceRecreated_ = false;
    bool shouldClose_ = false;
    PlatformInput input_{};
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> prevKeyDown_{};
    bool prevMouseLeft_ = false;
    bool prevMouseRight_ = false;
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;
    bool hasPrevMouse_ = false;

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace oc
