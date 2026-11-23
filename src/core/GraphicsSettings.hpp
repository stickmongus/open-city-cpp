#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace oc {

enum class GraphicsQuality : int {
    Ultra = 0,       // 울트라 (최고 화질 · 16-Tap Soft PCF + HDR Bloom + 850m 렌더)
    High = 1,        // 높음 (권장 · 8-Tap PCF + 600m 렌더)
    Medium = 2,      // 보통 (균형 · 4-Tap PCF + 400m 렌더)
    Performance = 3, // 성능 우선 (고프레임 · 그림자 끄기 + 250m 렌더)
    Custom = 4       // 사용자 지정
};

struct GraphicsSettings {
    static constexpr int kRenderDistPresets[7] = {250, 400, 600, 850, 1100, 1400, 1600};

    GraphicsQuality preset = GraphicsQuality::Ultra;
    int renderDistanceM = 850;  // 250 .. 1600 m (active 3D chunks + skyline + fog start)
    int buildingDetail = 3;     // 0=낮음, 1=보통, 2=높음, 3=최고
    int propDensity = 2;        // 0=낮음(40%), 1=보통(70%), 2=높음(100%), 3=최고(130%)
    int shadowQuality = 4;      // 0=끄기, 1=낮음(1-tap), 2=보통(4-tap), 3=높음(8-tap), 4=최고(16-tap Soft PCF)
    int lightingQuality = 3;    // 0=끄기, 1=보통(8광원), 2=높음(16광원), 3=최고(20광원+HDR블룸)
    int reflectionParallax = 2; // 0=끄기, 1=기본 반사, 2=최고 (보로노이+3D실내)
    int fogMode = 1;            // 0=끄기, 1=자연스럽게, 2=짙게
    int trafficDensity = 2;     // 0=적음, 1=보통, 2=많음, 3=혼잡
    bool showFps = true;        // Live FPS & frame-time ms overlay

    inline void applyPreset(GraphicsQuality q) {
        switch (q) {
            case GraphicsQuality::Ultra:
                preset = GraphicsQuality::Ultra;
                renderDistanceM = 850;
                buildingDetail = 3;
                propDensity = 2;
                shadowQuality = 4;
                lightingQuality = 3;
                reflectionParallax = 2;
                fogMode = 1;
                trafficDensity = 2;
                break;
            case GraphicsQuality::High:
                preset = GraphicsQuality::High;
                renderDistanceM = 600;
                buildingDetail = 2;
                propDensity = 2;
                shadowQuality = 3;
                lightingQuality = 2;
                reflectionParallax = 2;
                fogMode = 1;
                trafficDensity = 2;
                break;
            case GraphicsQuality::Medium:
                preset = GraphicsQuality::Medium;
                renderDistanceM = 400;
                buildingDetail = 1;
                propDensity = 1;
                shadowQuality = 2;
                lightingQuality = 1;
                reflectionParallax = 1;
                fogMode = 1;
                trafficDensity = 1;
                break;
            case GraphicsQuality::Performance:
                preset = GraphicsQuality::Performance;
                renderDistanceM = 250;
                buildingDetail = 0;
                propDensity = 0;
                shadowQuality = 0;
                lightingQuality = 0;
                reflectionParallax = 0;
                fogMode = 1;
                trafficDensity = 0;
                break;
            case GraphicsQuality::Custom:
                preset = GraphicsQuality::Custom;
                clampAndSyncPreset();
                break;
        }
    }

    inline int renderDistanceIndex() const {
        int bestIdx = 3;
        int bestDiff = 100000;
        for (int i = 0; i < 7; ++i) {
            const int d = std::abs(renderDistanceM - kRenderDistPresets[i]);
            if (d < bestDiff) {
                bestDiff = d;
                bestIdx = i;
            }
        }
        return bestIdx;
    }

    inline void stepRenderDistance(int delta) {
        const int idx = std::clamp(renderDistanceIndex() + delta, 0, 6);
        renderDistanceM = kRenderDistPresets[idx];
        clampAndSyncPreset();
    }

    inline void clampAndSyncPreset() {
        renderDistanceM = kRenderDistPresets[renderDistanceIndex()];
        buildingDetail = std::clamp(buildingDetail, 0, 3);
        propDensity = std::clamp(propDensity, 0, 3);
        shadowQuality = std::clamp(shadowQuality, 0, 4);
        lightingQuality = std::clamp(lightingQuality, 0, 3);
        reflectionParallax = std::clamp(reflectionParallax, 0, 2);
        fogMode = std::clamp(fogMode, 0, 2);
        trafficDensity = std::clamp(trafficDensity, 0, 3);

        auto matches = [&](GraphicsQuality q) -> bool {
            GraphicsSettings ref{};
            ref.applyPreset(q);
            return renderDistanceM == ref.renderDistanceM &&
                   buildingDetail == ref.buildingDetail &&
                   propDensity == ref.propDensity &&
                   shadowQuality == ref.shadowQuality &&
                   lightingQuality == ref.lightingQuality &&
                   reflectionParallax == ref.reflectionParallax &&
                   fogMode == ref.fogMode &&
                   trafficDensity == ref.trafficDensity;
        };
        if (matches(GraphicsQuality::Ultra)) preset = GraphicsQuality::Ultra;
        else if (matches(GraphicsQuality::High)) preset = GraphicsQuality::High;
        else if (matches(GraphicsQuality::Medium)) preset = GraphicsQuality::Medium;
        else if (matches(GraphicsQuality::Performance)) preset = GraphicsQuality::Performance;
        else preset = GraphicsQuality::Custom;
    }

    inline int chunkRadius() const {
        if (renderDistanceM <= 400) return 1;
        if (renderDistanceM <= 850) return 2;
        if (renderDistanceM <= 1400) return 3;
        return 4;
    }

    inline int maxLocalLights() const {
        static constexpr int kCaps[4] = {0, 8, 16, 20};
        return kCaps[std::clamp(lightingQuality, 0, 3)];
    }

    inline float shadowPcfTaps() const {
        static constexpr float kTaps[5] = {0.0f, 1.0f, 4.0f, 8.0f, 16.0f};
        return kTaps[std::clamp(shadowQuality, 0, 4)];
    }

    inline float shadowSpanM() const {
        static constexpr float kSpans[5] = {0.0f, 90.0f, 115.0f, 135.0f, 160.0f};
        return kSpans[std::clamp(shadowQuality, 0, 4)];
    }

    inline bool bloomEnabled() const { return lightingQuality >= 3; }

    static inline const char* presetNameEn(GraphicsQuality q) {
        switch (q) {
            case GraphicsQuality::Ultra:       return "ULTRA";
            case GraphicsQuality::High:        return "HIGH";
            case GraphicsQuality::Medium:      return "MEDIUM";
            case GraphicsQuality::Performance: return "PERFORMANCE";
            case GraphicsQuality::Custom:      return "CUSTOM";
        }
        return "ULTRA";
    }

    static inline const char* presetNameKo(GraphicsQuality q) {
        switch (q) {
            case GraphicsQuality::Ultra:       return "울트라";
            case GraphicsQuality::High:        return "높음";
            case GraphicsQuality::Medium:      return "보통";
            case GraphicsQuality::Performance: return "성능 우선";
            case GraphicsQuality::Custom:      return "사용자 지정";
        }
        return "울트라";
    }
};

} // namespace oc
