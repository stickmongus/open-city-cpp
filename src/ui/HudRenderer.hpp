#pragma once

#include "gameplay/GameWorld.hpp"
#include <raylib.h>

namespace oc {

class HudRenderer {
public:
    HudRenderer() = default;
    ~HudRenderer();

    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    void init();
    void shutdown();

    void toggleHelpOverlay() { showHelp_ = !showHelp_; }
    void toggleLanguage() { if (koreanFontLoaded_) useKorean_ = !useKorean_; }
    bool isShopMenuOpen() const { return shopMenuOpen_; }
    void setShopMenuOpen(bool open) { shopMenuOpen_ = open; }

    void showGraphicsToast(const char* labelEn, const char* labelKo);
    void renderHud(const GameWorld& world, float cameraYaw, bool cursorLocked);

private:
    const char* pickText(const char* en, const char* ko) const {
        return (useKorean_ && koreanFontLoaded_ && ko && ko[0] != '\0') ? ko : en;
    }

    void drawUiText(const char* text, float x, float y, float fontSize, Color color) const;
    float measureUiText(const char* text, float fontSize) const;

    void drawRadarMinimap(const GameWorld& world, float cameraYaw, int screenH);
    void drawTopRightStatus(const GameWorld& world, int screenW);
    void drawSpeedometerAndDrift(const GameWorld& world, int screenW, int screenH);
    void drawCrosshair(const GameWorld& world, int screenW, int screenH);
    void drawContextPromptsAndMission(const GameWorld& world, int screenW, int screenH);
    void drawShopModal(const GameWorld& world, int screenW, int screenH);
    void drawBannerToast(const GameWorld& world, int screenW, int screenH);
    void drawHelpOverlay(int screenW, int screenH, bool cursorLocked);

    Font uiFont_{};
    bool customFontLoaded_ = false;
    bool koreanFontLoaded_ = false;
    bool useKorean_ = true;
    bool showHelp_ = true;
    bool shopMenuOpen_ = false;
    std::string gfxToastEn_;
    std::string gfxToastKo_;
    float gfxToastTimer_ = 0.0f;
};

} // namespace oc
