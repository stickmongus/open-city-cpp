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
    void setShopMenuOpen(bool open) {
        shopMenuOpen_ = open;
        if (open) phoneOpen_ = false;
    }

    bool isPhoneOpen() const { return phoneOpen_; }
    void setPhoneOpen(bool open) {
        phoneOpen_ = open;
        if (open) shopMenuOpen_ = false;
    }
    int phoneTab() const { return phoneTab_; }
    void setPhoneTab(int tab) { phoneTab_ = ((tab % 4) + 4) % 4; }
    void cyclePhoneTab(int delta) { phoneTab_ = ((phoneTab_ + delta) % 4 + 4) % 4; }

    void showGraphicsToast(const char* labelEn, const char* labelKo);
    void renderHud(const GameWorld& world, float cameraYaw, bool cursorLocked);

private:
    static Font loadCompleteKoreanFont(const char* ttfPath, int fontSize);

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
    void drawSmartphoneModal(const GameWorld& world, int screenW, int screenH);
    void drawBannerToast(const GameWorld& world, int screenW, int screenH);
    void drawHelpOverlay(int screenW, int screenH, bool cursorLocked);

    Font uiFont_{};
    bool customFontLoaded_ = false;
    bool koreanFontLoaded_ = false;
    bool useKorean_ = true;
    bool showHelp_ = true;
    bool shopMenuOpen_ = false;
    bool phoneOpen_ = false;
    int phoneTab_ = 0;
    std::string gfxToastEn_;
    std::string gfxToastKo_;
    float gfxToastTimer_ = 0.0f;
};

} // namespace oc
