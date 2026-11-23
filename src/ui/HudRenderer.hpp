#pragma once

#include "gameplay/GameWorld.hpp"
#include "platform/PlatformApp.hpp"
#include "render/Renderer3D.hpp"
#include "ui/Vulkan2D.hpp"
#include <string>

namespace oc {

struct ModalTapAction {
    int shopBuyOption = 0;      // 1..12
    int phoneTab = 0;           // 0..3
    int phoneOption = 0;        // 1..10
    int customStudioTab = 0;    // 0..3
    int customStudioOption = 0; // 1..8
    bool setWaypoint = false;
    bool fastTravel = false;
    Vec2 mapTargetWorld{0.0f, 0.0f};
    std::string mapTargetLabelKo;
};

class HudRenderer {
public:
    HudRenderer() = default;
    ~HudRenderer() = default;

    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    void init(bool koreanFontAvailable);

    void setMobileUi(bool enabled) {
        mobileUi_ = enabled;
        if (enabled) showHelp_ = false;
    }
    bool isMobileUi() const { return mobileUi_; }
    bool isUsingKorean() const { return useKorean_ && koreanFontLoaded_; }

    void toggleHelpOverlay() { showHelp_ = !showHelp_; }
    void setShowHelp(bool show) { showHelp_ = show; }
    void setShowCrosshair(bool show) { showCrosshair_ = show; }
    void toggleLanguage() { if (koreanFontLoaded_) useKorean_ = !useKorean_; }
    void setFpsStats(float fps, float frameMs) {
        fpsValue_ = fps;
        frameMs_ = frameMs;
    }

    // Interactive Full-Screen 30km x 25km Seoul Map!
    bool isFullMapOpen() const { return fullMapOpen_; }
    void setFullMapOpen(bool open, const Vec2& focusWorld = {0.0f, 0.0f});
    void toggleFullMap(const Vec2& focusWorld) { setFullMapOpen(!fullMapOpen_, focusWorld); }
    void updateFullMapInput(const PlatformInput& input, int screenW, int screenH,
                            const GameWorld& world, float dt, ModalTapAction& out);

    bool isShopMenuOpen() const { return shopMenuOpen_; }
    void setShopMenuOpen(bool open);
    bool isPhoneOpen() const { return phoneOpen_; }
    void setPhoneOpen(bool open);
    int phoneTab() const { return phoneTab_; }
    void setPhoneTab(int tab) { phoneTab_ = ((tab % 4) + 4) % 4; }
    void cyclePhoneTab(int delta) { phoneTab_ = ((phoneTab_ + delta) % 4 + 4) % 4; }

    bool isCustomStudioOpen() const { return customStudioOpen_; }
    void setCustomStudioOpen(bool open);
    int customTab() const { return customTab_; }
    void setCustomTab(int tab) { customTab_ = ((tab % 4) + 4) % 4; }
    void cycleCustomTab(int delta) { customTab_ = ((customTab_ + delta) % 4 + 4) % 4; }

    // Granular Vulkan Graphics Settings Modal (Render Distance, Shadows, Lights, Reflections, Fog, Traffic, FPS!)
    bool isGraphicsMenuOpen() const { return graphicsMenuOpen_; }
    void setGraphicsMenuOpen(bool open);
    void toggleGraphicsMenu() { setGraphicsMenuOpen(!graphicsMenuOpen_); }
    bool handleGraphicsModalTap(int screenW, int screenH, Vec2 tapPos, GraphicsSettings& ioSettings);
    bool handleGraphicsModalInput(const PlatformInput& input, GraphicsSettings& ioSettings);

    bool isAnyModalOpen() const {
        return fullMapOpen_ || shopMenuOpen_ || phoneOpen_ || customStudioOpen_ || graphicsMenuOpen_;
    }

    // Exact clickable minimap rectangle (click/tap opens Full-Screen Seoul Map!)
    RectF minimapRect(int screenW, int screenH) const;

    bool handleModalTap(const GameWorld& world, int screenW, int screenH,
                        Vec2 tapPos, ModalTapAction& out);

    void showSystemToast(const char* textEn, const char* textKo);
    void showGraphicsToast(const char* labelEn, const char* labelKo);
    void updateTimers(float dt);

    void renderHud(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                   int screenW, int screenH, float cameraYaw,
                   bool cursorLocked, bool firstPerson = false, float adsBlend = 0.0f,
                   const GraphicsSettings* gfxSettings = nullptr);

private:
    const char* pickText(const char* en, const char* ko) const {
        return (useKorean_ && koreanFontLoaded_ && ko && ko[0] != '\0') ? ko : en;
    }

    void drawRadarMinimap(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                          int screenW, int screenH, float cameraYaw);
    void drawFullScreenSeoulMap(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                                int screenW, int screenH);
    void drawTopRightStatus(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                            bool firstPerson, const GraphicsSettings* gfxSettings);
    void drawSpeedometerAndDrift(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawCrosshair(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                       bool firstPerson, float adsBlend);
    void drawContextPromptsAndMission(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawShopModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawSmartphoneModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawCustomStudioModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawGraphicsModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                           const GraphicsSettings& gfx);
    void drawBannerToast(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH);
    void drawHelpOverlay(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH, bool cursorLocked);

    bool koreanFontLoaded_ = false;
    bool useKorean_ = true;
    bool mobileUi_ = false;
    bool showHelp_ = true;
    bool showCrosshair_ = true;

    bool fullMapOpen_ = false;
    Vec2 mapCenterWorld_{600.0f, 200.0f};
    float mapZoom_ = 1.0f; // 1.0 = full 30km x 25km Seoul view, up to 12.0x street zoom!
    bool mapHasSelection_ = false;
    Vec2 mapSelectedWorld_{600.0f, 1020.0f};
    std::string mapSelectedNameKo_ = "반포·강남";
    bool mapDragging_ = false;
    Vec2 mapDragLast_{0.0f, 0.0f};
    bool mapTouchTracking_ = false;
    int mapTouchId_ = -1;
    Vec2 mapTouchStart_{0.0f, 0.0f};
    Vec2 mapTouchLast_{0.0f, 0.0f};
    bool mapTouchDragged_ = false;
    bool mapPinchActive_ = false;
    float mapPinchLastDist_ = 0.0f;

    bool shopMenuOpen_ = false;
    bool phoneOpen_ = false;
    int phoneTab_ = 0;
    bool customStudioOpen_ = false;
    int customTab_ = 0;
    bool graphicsMenuOpen_ = false;
    int graphicsSelectedRow_ = 0;

    float fpsValue_ = 60.0f;
    float frameMs_ = 16.6f;

    std::string gfxToastEn_;
    std::string gfxToastKo_;
    float gfxToastTimer_ = 0.0f;
};

} // namespace oc
