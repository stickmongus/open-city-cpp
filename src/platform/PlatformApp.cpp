#if defined(__ANDROID__)
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#endif

#include "platform/PlatformApp.hpp"

#include <volk.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace oc {

namespace {
double monotonicSeconds() {
    using Clock = std::chrono::steady_clock;
    static const auto kStart = Clock::now();
    const auto now = Clock::now();
    return std::chrono::duration<double>(now - kStart).count();
}

std::vector<std::uint8_t> readFileBytes(const char* path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return {};
    const std::streamsize sz = ifs.tellg();
    if (sz <= 0) return {};
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    if (!ifs.read(reinterpret_cast<char*>(buf.data()), sz)) return {};
    return buf;
}
} // namespace

struct PlatformApp::Impl {
#if defined(__ANDROID__)
    struct android_app* app = nullptr;
    bool windowReady = false;
    bool surfaceDirty = false;
    bool resizedFlag = false;
    bool backKeyPending = false;
    std::array<TouchPointState, 8> liveTouches{};

    static void onAndroidAppCmd(struct android_app* app, int32_t cmd) {
        auto* self = static_cast<Impl*>(app->userData);
        if (!self) return;
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                if (app->window != nullptr) {
                    self->windowReady = true;
                    self->surfaceDirty = true;
                    self->resizedFlag = true;
                }
                break;
            case APP_CMD_TERM_WINDOW:
                self->windowReady = false;
                self->surfaceDirty = true;
                break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
                self->resizedFlag = true;
                break;
            default:
                break;
        }
    }

    static int32_t onAndroidInputEvent(struct android_app* app, AInputEvent* event) {
        auto* self = static_cast<Impl*>(app->userData);
        if (!self || !event) return 0;

        const int32_t type = AInputEvent_getType(event);
        if (type == AINPUT_EVENT_TYPE_KEY) {
            const int32_t key = AKeyEvent_getKeyCode(event);
            const int32_t action = AKeyEvent_getAction(event);
            if (key == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN) {
                self->backKeyPending = true;
                return 1;
            }
            return 0;
        }

        if (type == AINPUT_EVENT_TYPE_MOTION) {
            const int32_t actionFull = AMotionEvent_getAction(event);
            const int32_t action = actionFull & AMOTION_EVENT_ACTION_MASK;
            const int32_t actionIdx = (actionFull & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                      >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            const size_t ptrCount = AMotionEvent_getPointerCount(event);

            for (auto& t : self->liveTouches) t.active = false;
            int outSlot = 0;
            for (size_t i = 0; i < ptrCount && outSlot < 8; ++i) {
                if ((action == AMOTION_EVENT_ACTION_UP ||
                     action == AMOTION_EVENT_ACTION_POINTER_UP ||
                     action == AMOTION_EVENT_ACTION_CANCEL) &&
                    static_cast<int32_t>(i) == actionIdx) {
                    continue;
                }
                if (action == AMOTION_EVENT_ACTION_CANCEL) continue;
                self->liveTouches[static_cast<std::size_t>(outSlot)].id = AMotionEvent_getPointerId(event, i);
                self->liveTouches[static_cast<std::size_t>(outSlot)].x = AMotionEvent_getX(event, i);
                self->liveTouches[static_cast<std::size_t>(outSlot)].y = AMotionEvent_getY(event, i);
                self->liveTouches[static_cast<std::size_t>(outSlot)].active = true;
                ++outSlot;
            }
            return 1;
        }
        return 0;
    }
#else
    GLFWwindow* window = nullptr;
    float scrollAccumY = 0.0f;
    bool resizedFlag = false;
#endif
};

PlatformApp::PlatformApp() : impl_(new Impl{}) {
    lastTime_ = monotonicSeconds();
}

PlatformApp::~PlatformApp() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

double PlatformApp::timeSeconds() const {
    return monotonicSeconds();
}

bool PlatformApp::isWindowReady() const {
#if defined(__ANDROID__)
    return impl_ != nullptr && impl_->windowReady && impl_->app != nullptr && impl_->app->window != nullptr;
#else
    return impl_ != nullptr && impl_->window != nullptr && screenW_ > 0 && screenH_ > 0;
#endif
}

bool PlatformApp::consumeSurfaceRecreated() {
    const bool v = surfaceRecreated_;
    surfaceRecreated_ = false;
    return v;
}

bool PlatformApp::consumeResize() {
    const bool v = resized_;
    resized_ = false;
    return v;
}

#if defined(__ANDROID__)

bool PlatformApp::initAndroid(struct android_app* app) {
    impl_->app = app;
    app->userData = impl_;
    app->onAppCmd = &Impl::onAndroidAppCmd;
    app->onInputEvent = &Impl::onAndroidInputEvent;
    lastTime_ = monotonicSeconds();
    if (app->window != nullptr) {
        impl_->windowReady = true;
    }

    // Wait briefly for initial ANativeWindow before Vulkan surface creation
    while (!impl_->windowReady && app->destroyRequested == 0) {
        int events = 0;
        struct android_poll_source* source = nullptr;
        while (ALooper_pollOnce(16, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested != 0) return false;
            if (impl_->windowReady) break;
        }
    }
    if (app->window) {
        screenW_ = std::max(1, static_cast<int>(ANativeWindow_getWidth(app->window)));
        screenH_ = std::max(1, static_cast<int>(ANativeWindow_getHeight(app->window)));
    }
    // Initial Vulkan init will create the surface & swapchain directly
    impl_->surfaceDirty = false;
    impl_->resizedFlag = false;
    return impl_->windowReady;
}

void PlatformApp::shutdown() {}

bool PlatformApp::pollEvents() {
    if (!impl_ || !impl_->app) return false;
    int events = 0;
    struct android_poll_source* source = nullptr;
    const int timeoutMs = impl_->windowReady ? 0 : 16;
    while (ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(impl_->app, source);
        if (impl_->app->destroyRequested != 0) {
            shouldClose_ = true;
            return false;
        }
    }

    if (impl_->app->window) {
        const int w = std::max(1, static_cast<int>(ANativeWindow_getWidth(impl_->app->window)));
        const int h = std::max(1, static_cast<int>(ANativeWindow_getHeight(impl_->app->window)));
        if (w != screenW_ || h != screenH_) {
            screenW_ = w;
            screenH_ = h;
            resized_ = true;
        }
    }
    if (impl_->surfaceDirty) {
        surfaceRecreated_ = true;
        impl_->surfaceDirty = false;
    }
    if (impl_->resizedFlag) {
        resized_ = true;
        impl_->resizedFlag = false;
    }

    const double now = monotonicSeconds();
    dt_ = std::clamp(static_cast<float>(now - lastTime_), 0.001f, 0.05f);
    lastTime_ = now;

    input_.keyDown.fill(false);
    input_.keyPressed.fill(false);
    input_.backPressed = impl_->backKeyPending;
    impl_->backKeyPending = false;
    input_.scrollDeltaY = 0.0f;
    input_.mouseDeltaX = 0.0f;
    input_.mouseDeltaY = 0.0f;

    input_.touchCount = 0;
    for (int i = 0; i < 8; ++i) {
        input_.touches[static_cast<std::size_t>(i)] = impl_->liveTouches[static_cast<std::size_t>(i)];
        if (impl_->liveTouches[static_cast<std::size_t>(i)].active) {
            ++input_.touchCount;
        }
    }
    // Mirror primary touch to mouse coordinates for unified modal click handling
    const bool primaryDown = (input_.touchCount > 0);
    if (primaryDown) {
        input_.mouseX = input_.touches[0].x;
        input_.mouseY = input_.touches[0].y;
    }
    input_.mouseLeftPressed = primaryDown && !prevMouseLeft_;
    input_.mouseLeftDown = primaryDown;
    prevMouseLeft_ = primaryDown;

    return !shouldClose_;
}

void PlatformApp::setCursorLocked(bool locked) { cursorLocked_ = locked; }

std::vector<const char*> PlatformApp::getVulkanInstanceExtensions() const {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
}

bool PlatformApp::createVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface) const {
    if (!impl_ || !impl_->app || !impl_->app->window || !vkCreateAndroidSurfaceKHR) return false;
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = impl_->app->window;
    return vkCreateAndroidSurfaceKHR(instance, &ci, nullptr, outSurface) == VK_SUCCESS;
}

std::vector<std::uint8_t> PlatformApp::loadAssetOrSystemFont() const {
    if (impl_ && impl_->app && impl_->app->activity && impl_->app->activity->assetManager) {
        AAsset* asset = AAssetManager_open(impl_->app->activity->assetManager,
                                           "fonts/korean.ttf", AASSET_MODE_BUFFER);
        if (asset) {
            const off_t len = AAsset_getLength(asset);
            std::vector<std::uint8_t> data(static_cast<std::size_t>(len));
            AAsset_read(asset, data.data(), static_cast<size_t>(len));
            AAsset_close(asset);
            if (!data.empty()) return data;
        }
    }
    static constexpr const char* kAndroidFonts[] = {
        "/system/fonts/NotoSansKR-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/Roboto-Regular.ttf"
    };
    for (const char* p : kAndroidFonts) {
        auto b = readFileBytes(p);
        if (!b.empty()) return b;
    }
    return {};
}

#else // Desktop (Linux / Windows via GLFW 3.4)

namespace {
int toGlfwKey(KeyCode k) {
    switch (k) {
        case KeyCode::W: return GLFW_KEY_W;
        case KeyCode::A: return GLFW_KEY_A;
        case KeyCode::S: return GLFW_KEY_S;
        case KeyCode::D: return GLFW_KEY_D;
        case KeyCode::Up: return GLFW_KEY_UP;
        case KeyCode::Down: return GLFW_KEY_DOWN;
        case KeyCode::Left: return GLFW_KEY_LEFT;
        case KeyCode::Right: return GLFW_KEY_RIGHT;
        case KeyCode::Space: return GLFW_KEY_SPACE;
        case KeyCode::Shift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::Ctrl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::E: return GLFW_KEY_E;
        case KeyCode::F: return GLFW_KEY_F;
        case KeyCode::G: return GLFW_KEY_G;
        case KeyCode::H: return GLFW_KEY_H;
        case KeyCode::K: return GLFW_KEY_K;
        case KeyCode::M: return GLFW_KEY_M;
        case KeyCode::P: return GLFW_KEY_P;
        case KeyCode::Q: return GLFW_KEY_Q;
        case KeyCode::R: return GLFW_KEY_R;
        case KeyCode::T: return GLFW_KEY_T;
        case KeyCode::U: return GLFW_KEY_U;
        case KeyCode::V: return GLFW_KEY_V;
        case KeyCode::Y: return GLFW_KEY_Y;
        case KeyCode::Z: return GLFW_KEY_Z;
        case KeyCode::Tab: return GLFW_KEY_TAB;
        case KeyCode::Enter: return GLFW_KEY_ENTER;
        case KeyCode::Escape: return GLFW_KEY_ESCAPE;
        case KeyCode::Backspace: return GLFW_KEY_BACKSPACE;
        case KeyCode::Num0: return GLFW_KEY_0;
        case KeyCode::Num1: return GLFW_KEY_1;
        case KeyCode::Num2: return GLFW_KEY_2;
        case KeyCode::Num3: return GLFW_KEY_3;
        case KeyCode::Num4: return GLFW_KEY_4;
        case KeyCode::Num5: return GLFW_KEY_5;
        case KeyCode::Num6: return GLFW_KEY_6;
        case KeyCode::Num7: return GLFW_KEY_7;
        case KeyCode::Num8: return GLFW_KEY_8;
        case KeyCode::Num9: return GLFW_KEY_9;
        case KeyCode::Minus: return GLFW_KEY_MINUS;
        case KeyCode::Equal: return GLFW_KEY_EQUAL;
        case KeyCode::F1: return GLFW_KEY_F1;
        case KeyCode::F2: return GLFW_KEY_F2;
        case KeyCode::F3: return GLFW_KEY_F3;
        case KeyCode::F4: return GLFW_KEY_F4;
        default: return GLFW_KEY_UNKNOWN;
    }
}
} // namespace

bool PlatformApp::initDesktop(int width, int height, const char* title) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    impl_->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!impl_->window) {
        glfwTerminate();
        return false;
    }
    glfwSetWindowUserPointer(impl_->window, impl_);
    glfwSetFramebufferSizeCallback(impl_->window, [](GLFWwindow* w, int, int) {
        if (auto* st = static_cast<PlatformApp::Impl*>(glfwGetWindowUserPointer(w))) {
            st->resizedFlag = true;
        }
    });
    glfwSetScrollCallback(impl_->window, [](GLFWwindow* w, double, double yoff) {
        if (auto* st = static_cast<PlatformApp::Impl*>(glfwGetWindowUserPointer(w))) {
            st->scrollAccumY += static_cast<float>(yoff);
        }
    });

    int fbw = width, fbh = height;
    glfwGetFramebufferSize(impl_->window, &fbw, &fbh);
    screenW_ = std::max(1, fbw);
    screenH_ = std::max(1, fbh);
    lastTime_ = monotonicSeconds();
    return true;
}

void PlatformApp::shutdown() {
    if (impl_ && impl_->window) {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
        glfwTerminate();
    }
}

bool PlatformApp::pollEvents() {
    if (!impl_ || !impl_->window) return false;
    glfwPollEvents();
    if (glfwWindowShouldClose(impl_->window)) {
        shouldClose_ = true;
        return false;
    }

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(impl_->window, &fbw, &fbh);
    if (fbw > 0 && fbh > 0 && (fbw != screenW_ || fbh != screenH_)) {
        screenW_ = fbw;
        screenH_ = fbh;
        resized_ = true;
    }
    if (impl_->resizedFlag) {
        resized_ = true;
        impl_->resizedFlag = false;
    }

    const double now = monotonicSeconds();
    dt_ = std::clamp(static_cast<float>(now - lastTime_), 0.001f, 0.05f);
    lastTime_ = now;

    for (std::size_t i = 1; i < static_cast<std::size_t>(KeyCode::Count); ++i) {
        const int gk = toGlfwKey(static_cast<KeyCode>(i));
        const bool down = (gk != GLFW_KEY_UNKNOWN) && (glfwGetKey(impl_->window, gk) == GLFW_PRESS);
        input_.keyPressed[i] = down && !prevKeyDown_[i];
        input_.keyDown[i] = down;
        prevKeyDown_[i] = down;
    }
    input_.backPressed = input_.isPressed(KeyCode::Escape);

    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(impl_->window, &mx, &my);
    if (hasPrevMouse_) {
        input_.mouseDeltaX = static_cast<float>(mx - prevMouseX_);
        input_.mouseDeltaY = static_cast<float>(my - prevMouseY_);
    } else {
        input_.mouseDeltaX = 0.0f;
        input_.mouseDeltaY = 0.0f;
        hasPrevMouse_ = true;
    }
    prevMouseX_ = mx;
    prevMouseY_ = my;
    input_.mouseX = static_cast<float>(mx);
    input_.mouseY = static_cast<float>(my);

    const bool ml = (glfwGetMouseButton(impl_->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    const bool mr = (glfwGetMouseButton(impl_->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    input_.mouseLeftPressed = ml && !prevMouseLeft_;
    input_.mouseRightPressed = mr && !prevMouseRight_;
    input_.mouseLeftDown = ml;
    input_.mouseRightDown = mr;
    prevMouseLeft_ = ml;
    prevMouseRight_ = mr;

    input_.scrollDeltaY = impl_->scrollAccumY;
    impl_->scrollAccumY = 0.0f;
    input_.touchCount = 0;

    return !shouldClose_;
}

void PlatformApp::setCursorLocked(bool locked) {
    cursorLocked_ = locked;
    if (impl_ && impl_->window) {
        glfwSetInputMode(impl_->window, GLFW_CURSOR,
                         locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        hasPrevMouse_ = false;
    }
}

std::vector<const char*> PlatformApp::getVulkanInstanceExtensions() const {
    uint32_t count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> res;
    for (uint32_t i = 0; i < count; ++i) res.push_back(exts[i]);
    return res;
}

bool PlatformApp::createVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface) const {
    if (!impl_ || !impl_->window) return false;
    return glfwCreateWindowSurface(instance, impl_->window, nullptr, outSurface) == VK_SUCCESS;
}

std::vector<std::uint8_t> PlatformApp::loadAssetOrSystemFont() const {
    static constexpr const char* kDesktopFonts[] = {
        "assets/fonts/korean.ttf",
        "android/assets/fonts/korean.ttf",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/truetype/nanum/NanumBarunGothic.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansKR-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\malgun.ttf",
        "C:\\Windows\\Fonts\\malgunbd.ttf",
        "C:\\Windows\\Fonts\\gulim.ttc",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    for (const char* p : kDesktopFonts) {
        auto b = readFileBytes(p);
        if (!b.empty()) return b;
    }
    return {};
}

#endif

} // namespace oc
