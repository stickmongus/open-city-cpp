#include "ui/HudRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace oc {

namespace {

std::string fmtCash(int cash) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "$%d", cash);
    return buf;
}

std::string fmtClock(float hours) {
    const int totalMin = static_cast<int>(std::fmod(hours + 24.0f, 24.0f) * 60.0f);
    const int hh = (totalMin / 60) % 24;
    const int mm = totalMin % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    return buf;
}

} // namespace

void HudRenderer::init(bool koreanFontAvailable) {
    koreanFontLoaded_ = koreanFontAvailable;
    useKorean_ = koreanFontAvailable;
}

void HudRenderer::setFullMapOpen(bool open, const Vec2& focusWorld) {
    fullMapOpen_ = open;
    if (open) {
        shopMenuOpen_ = false;
        phoneOpen_ = false;
        customStudioOpen_ = false;
        graphicsMenuOpen_ = false;
        mapCenterWorld_ = {1920.0f, -1280.0f}; // Centered squarely over Real-OSM Seoul (Yeouido..Jamsil, Gwanghwamun..Gangnam)!
        mapZoom_ = 1.0f;
        mapSelectedWorld_ = focusWorld;
        mapHasSelection_ = false;
        mapDragging_ = false;
        mapTouchTracking_ = false;
        mapTouchId_ = -1;
        mapTouchDragged_ = false;
        mapPinchActive_ = false;
        mapPinchLastDist_ = 0.0f;
    }
}

void HudRenderer::setShopMenuOpen(bool open) {
    shopMenuOpen_ = open;
    if (open) { fullMapOpen_ = false; phoneOpen_ = false; customStudioOpen_ = false; graphicsMenuOpen_ = false; }
}

void HudRenderer::setPhoneOpen(bool open) {
    phoneOpen_ = open;
    if (open) { fullMapOpen_ = false; shopMenuOpen_ = false; customStudioOpen_ = false; graphicsMenuOpen_ = false; }
}

void HudRenderer::setCustomStudioOpen(bool open) {
    customStudioOpen_ = open;
    if (open) { fullMapOpen_ = false; shopMenuOpen_ = false; phoneOpen_ = false; graphicsMenuOpen_ = false; }
}

void HudRenderer::setGraphicsMenuOpen(bool open) {
    graphicsMenuOpen_ = open;
    if (open) {
        fullMapOpen_ = false;
        shopMenuOpen_ = false;
        phoneOpen_ = false;
        customStudioOpen_ = false;
    }
}

void HudRenderer::showSystemToast(const char* textEn, const char* textKo) {
    gfxToastEn_ = textEn ? textEn : "";
    gfxToastKo_ = textKo ? textKo : "";
    gfxToastTimer_ = 3.2f;
}

void HudRenderer::showGraphicsToast(const char* labelEn, const char* labelKo) {
    gfxToastEn_ = std::string("Vulkan Graphics: ") + (labelEn ? labelEn : "");
    gfxToastKo_ = std::string("Vulkan 그래픽: ") + (labelKo ? labelKo : "");
    gfxToastTimer_ = 3.0f;
}

void HudRenderer::updateTimers(float dt) {
    if (gfxToastTimer_ > 0.0f) gfxToastTimer_ = std::max(0.0f, gfxToastTimer_ - dt);
}

RectF HudRenderer::minimapRect(int screenW, int screenH) const {
    if (mobileUi_) {
        // DPI-normalized logical dp space (~940..1333 x 600+ dp): crisp 136x136 top-left radar
        return {20.0f, 12.0f, 136.0f, 136.0f};
    }
    const float size = 204.0f;
    (void)screenW;
    return {20.0f, static_cast<float>(screenH) - size - 22.0f, size, size};
}

// Helper: compute layout rects for Full-Screen Interactive Seoul Map
namespace {
struct FullMapLayout {
    RectF panel{};
    RectF canvas{};
    RectF sidebar{};
    RectF btnClose{};
    RectF btnZoomIn{};
    RectF btnZoomOut{};
    RectF btnRecenter{};
    RectF btnSetGps{};
    RectF btnFastTravel{};
};

FullMapLayout computeFullMapLayout(int screenW, int screenH) {
    FullMapLayout l{};
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    const float pad = std::max(12.0f, h * 0.024f);
    l.panel = {pad, pad, w - pad * 2.0f, h - pad * 2.0f};
    const float headerH = 48.0f;
    const float sideW = std::clamp(w * 0.245f, 224.0f, 308.0f);
    l.canvas = {l.panel.x + 12.0f, l.panel.y + headerH + 8.0f,
                l.panel.w - sideW - 32.0f, l.panel.h - headerH - 22.0f};
    l.sidebar = {l.canvas.x + l.canvas.w + 10.0f, l.canvas.y, sideW, l.canvas.h};

    l.btnClose    = {l.panel.x + l.panel.w - 110.0f, l.panel.y + 8.0f, 96.0f, 34.0f};
    l.btnZoomIn   = {l.canvas.x + 12.0f, l.canvas.y + 12.0f, 40.0f, 36.0f};
    l.btnZoomOut  = {l.canvas.x + 58.0f, l.canvas.y + 12.0f, 40.0f, 36.0f};
    l.btnRecenter = {l.canvas.x + 104.0f, l.canvas.y + 12.0f, 96.0f, 36.0f};

    const float actY = l.sidebar.y + l.sidebar.h - 94.0f;
    l.btnSetGps     = {l.sidebar.x + 10.0f, actY,         l.sidebar.w - 20.0f, 38.0f};
    l.btnFastTravel = {l.sidebar.x + 10.0f, actY + 46.0f, l.sidebar.w - 20.0f, 40.0f};
    return l;
}

void mapWorldBounds(const CityMap& city, float& minX, float& maxX, float& minZ, float& maxZ) {
    if (city.hasRealSeoulMap()) {
        const OcMapHeader& h = city.realData().header();
        minX = h.minX; maxX = h.maxX;
        minZ = h.minZ; maxZ = h.maxZ;
    } else {
        minX = CityMap::kWorldMinX; maxX = CityMap::kWorldMaxX;
        minZ = CityMap::kWorldMinZ; maxZ = CityMap::kWorldMaxZ;
    }
}

Vec2 mapWorldToScreen(const FullMapLayout& l, const CityMap& city, const Vec2& worldPos, const Vec2& centerWorld, float zoom) {
    float minX, maxX, minZ, maxZ;
    mapWorldBounds(city, minX, maxX, minZ, maxZ);
    const float baseSpanX = maxX - minX;
    const float baseSpanZ = maxZ - minZ;
    const float scale = std::min(l.canvas.w / baseSpanX, l.canvas.h / baseSpanZ) * zoom;
    const float cx = l.canvas.x + l.canvas.w * 0.5f;
    const float cy = l.canvas.y + l.canvas.h * 0.5f;
    return {
        cx + (worldPos.x - centerWorld.x) * scale,
        cy + (worldPos.z - centerWorld.z) * scale
    };
}

Vec2 mapScreenToWorld(const FullMapLayout& l, const CityMap& city, const Vec2& screenPos, const Vec2& centerWorld, float zoom) {
    float minX, maxX, minZ, maxZ;
    mapWorldBounds(city, minX, maxX, minZ, maxZ);
    const float baseSpanX = maxX - minX;
    const float baseSpanZ = maxZ - minZ;
    const float scale = std::min(l.canvas.w / baseSpanX, l.canvas.h / baseSpanZ) * zoom;
    const float cx = l.canvas.x + l.canvas.w * 0.5f;
    const float cy = l.canvas.y + l.canvas.h * 0.5f;
    return {
        clampf(centerWorld.x + (screenPos.x - cx) / std::max(1e-5f, scale), minX, maxX),
        clampf(centerWorld.z + (screenPos.z - cy) / std::max(1e-5f, scale), minZ, maxZ)
    };
}
} // namespace

void HudRenderer::updateFullMapInput(const PlatformInput& input, int screenW, int screenH,
                                     const GameWorld& world, float dt, ModalTapAction& out) {
    if (!fullMapOpen_) return;
    const FullMapLayout l = computeFullMapLayout(screenW, screenH);
    const CityMap& city = world.city();
    float minX, maxX, minZ, maxZ;
    mapWorldBounds(city, minX, maxX, minZ, maxZ);

    // Keyboard pan & zoom
    const float panSpd = (4800.0f / mapZoom_) * dt;
    if (input.isDown(KeyCode::A) || input.isDown(KeyCode::Left))  mapCenterWorld_.x -= panSpd;
    if (input.isDown(KeyCode::D) || input.isDown(KeyCode::Right)) mapCenterWorld_.x += panSpd;
    if (input.isDown(KeyCode::W) || input.isDown(KeyCode::Up))    mapCenterWorld_.z -= panSpd;
    if (input.isDown(KeyCode::S) || input.isDown(KeyCode::Down))  mapCenterWorld_.z += panSpd;
    if (input.isPressed(KeyCode::Equal)) mapZoom_ = clampf(mapZoom_ * 1.35f, 0.9f, 14.0f);
    if (input.isPressed(KeyCode::Minus)) mapZoom_ = clampf(mapZoom_ / 1.35f, 0.9f, 14.0f);
    if (std::abs(input.scrollDeltaY) > 0.01f) {
        mapZoom_ = clampf(mapZoom_ * (input.scrollDeltaY > 0.0f ? 1.22f : (1.0f / 1.22f)), 0.9f, 14.0f);
    }

    // Mobile multi-touch: 2-finger pinch-to-zoom + 1-finger drag pan + clean tap-to-select!
    int activeTouchCount = 0;
    Vec2 activePts[2]{};
    int activeIds[2]{-1, -1};
    for (int i = 0; i < input.touchCount && activeTouchCount < 2; ++i) {
        const auto& tp = input.touches[static_cast<std::size_t>(i)];
        if (tp.active) {
            activePts[activeTouchCount] = {tp.x, tp.y};
            activeIds[activeTouchCount] = tp.id;
            ++activeTouchCount;
        }
    }
    if (activeTouchCount == 0 && mobileUi_ && input.mouseLeftDown) {
        activePts[0] = {input.mouseX, input.mouseY};
        activeIds[0] = 999;
        activeTouchCount = 1;
    }

    if (activeTouchCount >= 2) {
        const float dist = (activePts[0] - activePts[1]).length();
        if (mapPinchActive_ && mapPinchLastDist_ > 12.0f && dist > 12.0f) {
            mapZoom_ = clampf(mapZoom_ * (dist / mapPinchLastDist_), 0.9f, 14.0f);
        }
        mapPinchActive_ = true;
        mapPinchLastDist_ = dist;
        mapTouchTracking_ = false;
        mapTouchDragged_ = true;
    } else if (activeTouchCount == 1) {
        mapPinchActive_ = false;
        const Vec2 pt = activePts[0];
        const int pid = activeIds[0];
        const bool isCanvasControl = l.btnZoomIn.contains(pt.x, pt.z) ||
                                     l.btnZoomOut.contains(pt.x, pt.z) ||
                                     l.btnRecenter.contains(pt.x, pt.z);
        if (!mapTouchTracking_ || mapTouchId_ != pid) {
            mapTouchTracking_ = true;
            mapTouchId_ = pid;
            mapTouchStart_ = pt;
            mapTouchLast_ = pt;
            mapTouchDragged_ = false;
            // Immediate tactile response on buttons & sidebar rows; canvas waits for drag vs tap
            if (!l.canvas.contains(pt.x, pt.z) || isCanvasControl) {
                handleModalTap(world, screenW, screenH, pt, out);
                mapTouchDragged_ = true; // Prevent duplicate tap on release
            }
        } else if (l.canvas.contains(mapTouchStart_.x, mapTouchStart_.z) && !isCanvasControl) {
            const float moved = (pt - mapTouchStart_).length();
            if (moved > 8.0f) mapTouchDragged_ = true;
            if (mapTouchDragged_) {
                const float scale = std::min(l.canvas.w / (maxX - minX), l.canvas.h / (maxZ - minZ)) * mapZoom_;
                mapCenterWorld_.x -= (pt.x - mapTouchLast_.x) / std::max(1e-5f, scale);
                mapCenterWorld_.z -= (pt.z - mapTouchLast_.z) / std::max(1e-5f, scale);
            }
            mapTouchLast_ = pt;
        }
    } else {
        if (mapTouchTracking_ && !mapTouchDragged_) {
            handleModalTap(world, screenW, screenH, mapTouchLast_, out);
        }
        mapTouchTracking_ = false;
        mapTouchId_ = -1;
        mapTouchDragged_ = false;
        mapPinchActive_ = false;
    }

    // Desktop right-mouse drag panning & left-click selection
    if (!mobileUi_) {
        if (input.mouseRightDown && l.canvas.contains(input.mouseX, input.mouseY)) {
            if (!mapDragging_) {
                mapDragging_ = true;
                mapDragLast_ = {input.mouseX, input.mouseY};
            } else {
                const float scale = std::min(l.canvas.w / (maxX - minX), l.canvas.h / (maxZ - minZ)) * mapZoom_;
                mapCenterWorld_.x -= (input.mouseX - mapDragLast_.x) / std::max(1e-5f, scale);
                mapCenterWorld_.z -= (input.mouseY - mapDragLast_.z) / std::max(1e-5f, scale);
                mapDragLast_ = {input.mouseX, input.mouseY};
            }
        } else {
            mapDragging_ = false;
        }
        if (input.mouseLeftPressed) {
            handleModalTap(world, screenW, screenH, {input.mouseX, input.mouseY}, out);
        }
    }

    mapCenterWorld_.x = clampf(mapCenterWorld_.x, minX, maxX);
    mapCenterWorld_.z = clampf(mapCenterWorld_.z, minZ, maxZ);

    if (input.isPressed(KeyCode::Enter) && mapHasSelection_) {
        out.fastTravel = true;
        out.mapTargetWorld = mapSelectedWorld_;
        out.mapTargetLabelKo = mapSelectedNameKo_;
        fullMapOpen_ = false;
    }
}

bool HudRenderer::handleModalTap(const GameWorld& world, int screenW, int screenH,
                                 Vec2 tapPos, ModalTapAction& out) {
    if (fullMapOpen_) {
        const FullMapLayout l = computeFullMapLayout(screenW, screenH);
        const CityMap& city = world.city();
        if (l.btnClose.contains(tapPos.x, tapPos.z)) {
            fullMapOpen_ = false;
            return true;
        }
        if (l.btnZoomIn.contains(tapPos.x, tapPos.z)) {
            mapZoom_ = clampf(mapZoom_ * 1.45f, 0.9f, 14.0f);
            return true;
        }
        if (l.btnZoomOut.contains(tapPos.x, tapPos.z)) {
            mapZoom_ = clampf(mapZoom_ / 1.45f, 0.9f, 14.0f);
            return true;
        }
        if (l.btnRecenter.contains(tapPos.x, tapPos.z)) {
            mapCenterWorld_ = world.player().pos;
            mapZoom_ = std::max(mapZoom_, 2.4f);
            return true;
        }
        if (l.btnSetGps.contains(tapPos.x, tapPos.z)) {
            out.setWaypoint = true;
            out.mapTargetWorld = mapHasSelection_ ? mapSelectedWorld_ : world.player().pos;
            out.mapTargetLabelKo = mapHasSelection_ ? mapSelectedNameKo_ : "목적지";
            return true;
        }
        if (l.btnFastTravel.contains(tapPos.x, tapPos.z)) {
            out.fastTravel = true;
            out.mapTargetWorld = mapHasSelection_ ? mapSelectedWorld_ : mapCenterWorld_;
            out.mapTargetLabelKo = mapHasSelection_ ? mapSelectedNameKo_ : "서울";
            fullMapOpen_ = false;
            return true;
        }

        // Sidebar landmark quick-select list
        const float listTop = l.sidebar.y + 38.0f;
        const float listBottom = l.btnSetGps.y - 10.0f;
        const auto& landmarks = city.landmarks();
        const float rowH = std::min(32.0f, (listBottom - listTop) / std::max(1.0f, static_cast<float>(landmarks.size())));
        for (std::size_t i = 0; i < landmarks.size(); ++i) {
            RectF row{l.sidebar.x + 8.0f, listTop + static_cast<float>(i) * rowH, l.sidebar.w - 16.0f, rowH - 3.0f};
            if (row.contains(tapPos.x, tapPos.z)) {
                mapSelectedWorld_ = landmarks[i].pos;
                mapSelectedNameKo_ = landmarks[i].nameKo;
                mapHasSelection_ = true;
                mapCenterWorld_ = landmarks[i].pos;
                mapZoom_ = std::max(mapZoom_, 2.2f);
                out.setWaypoint = true;
                out.mapTargetWorld = landmarks[i].pos;
                out.mapTargetLabelKo = landmarks[i].nameKo;
                return true;
            }
        }

        // Direct tap on the Seoul Map Canvas -> select landmark if near, else exact world coordinate!
        if (l.canvas.contains(tapPos.x, tapPos.z)) {
            Vec2 clickedWorld = mapScreenToWorld(l, city, tapPos, mapCenterWorld_, mapZoom_);
            std::string chosenName = CityMap::districtNameKo(city.districtAtWorld(clickedWorld));
            // Snap to nearby iconic landmark pin within 18px
            for (const auto& lm : landmarks) {
                const Vec2 sp = mapWorldToScreen(l, city, lm.pos, mapCenterWorld_, mapZoom_);
                if ((sp - tapPos).length() < 18.0f) {
                    clickedWorld = lm.pos;
                    chosenName = lm.nameKo;
                    break;
                }
            }
            mapSelectedWorld_ = clickedWorld;
            mapSelectedNameKo_ = chosenName;
            mapHasSelection_ = true;
            out.setWaypoint = true;
            out.mapTargetWorld = clickedWorld;
            out.mapTargetLabelKo = chosenName;
            return true;
        }
        return false;
    }

    // Smartphone / Shop / Custom Studio quick close & item taps
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    const float mw = std::min(580.0f, w * 0.84f);
    const float mh = std::min(520.0f, h * 0.86f);
    const RectF box{(w - mw) * 0.5f, (h - mh) * 0.5f, mw, mh};
    if (!box.contains(tapPos.x, tapPos.z)) {
        shopMenuOpen_ = false;
        phoneOpen_ = false;
        customStudioOpen_ = false;
        return true;
    }
    const RectF closeR{box.x + box.w - 86.0f, box.y + 10.0f, 74.0f, 30.0f};
    if (closeR.contains(tapPos.x, tapPos.z)) {
        shopMenuOpen_ = false;
        phoneOpen_ = false;
        customStudioOpen_ = false;
        return true;
    }
    if (phoneOpen_ || customStudioOpen_) {
        const float tabW = (box.w - 28.0f) / 4.0f;
        for (int t = 0; t < 4; ++t) {
            RectF tr{box.x + 14.0f + static_cast<float>(t) * tabW, box.y + 48.0f, tabW - 4.0f, 32.0f};
            if (tr.contains(tapPos.x, tapPos.z)) {
                if (phoneOpen_) phoneTab_ = t;
                else customTab_ = t;
                return true;
            }
        }
    }
    const float itemTop = (phoneOpen_ || customStudioOpen_) ? (box.y + 92.0f) : (box.y + 58.0f);
    const int maxRows = shopMenuOpen_ ? 10 : (phoneOpen_ ? 8 : 8);
    const float rowH = std::min(42.0f, (box.y + box.h - 20.0f - itemTop) / static_cast<float>(maxRows));
    for (int i = 0; i < maxRows; ++i) {
        RectF rr{box.x + 16.0f, itemTop + static_cast<float>(i) * rowH, box.w - 32.0f, rowH - 5.0f};
        if (rr.contains(tapPos.x, tapPos.z)) {
            if (shopMenuOpen_) out.shopBuyOption = i + 1;
            else if (phoneOpen_) { out.phoneTab = phoneTab_; out.phoneOption = i + 1; }
            else if (customStudioOpen_) { out.customStudioTab = customTab_; out.customStudioOption = i + 1; }
            return true;
        }
    }
    return false;
}

namespace {
struct GraphicsModalLayout {
    static constexpr int kRowCount = 9;
    RectF box{};
    RectF btnClose{};
    RectF presetBtns[4]{};
    RectF rowRects[kRowCount]{};
    RectF rowMinus[kRowCount]{};
    RectF rowBar[kRowCount]{};
    RectF rowPlus[kRowCount]{};
};

GraphicsModalLayout computeGraphicsModalLayout(int screenW, int screenH) {
    GraphicsModalLayout l{};
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    const float mw = std::min(684.0f, w * 0.90f);
    const float mh = std::min(556.0f, h * 0.91f);
    l.box = {(w - mw) * 0.5f, (h - mh) * 0.5f, mw, mh};
    l.btnClose = {l.box.x + l.box.w - 88.0f, l.box.y + 10.0f, 74.0f, 30.0f};

    const float tabW = (l.box.w - 28.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        l.presetBtns[i] = {l.box.x + 14.0f + static_cast<float>(i) * tabW, l.box.y + 48.0f, tabW - 5.0f, 32.0f};
    }

    const float listTop = l.box.y + 92.0f;
    const float footerH = 30.0f;
    const float rowH = std::max(34.0f, (l.box.y + l.box.h - footerH - listTop) / static_cast<float>(GraphicsModalLayout::kRowCount));
    const float ctrlW = std::min(308.0f, l.box.w * 0.50f);
    const float stepW = 36.0f;
    const float barW = std::max(100.0f, ctrlW - stepW * 2.0f - 10.0f);
    const float ctrlRight = l.box.x + l.box.w - 18.0f;
    const float ctrlLeft = ctrlRight - ctrlW;

    for (int i = 0; i < GraphicsModalLayout::kRowCount; ++i) {
        const float ry = listTop + static_cast<float>(i) * rowH;
        const float rh = rowH - 5.0f;
        l.rowRects[i] = {l.box.x + 14.0f, ry, l.box.w - 28.0f, rh};
        l.rowMinus[i] = {ctrlLeft, ry + 3.0f, stepW, rh - 6.0f};
        l.rowBar[i]   = {ctrlLeft + stepW + 5.0f, ry + 3.0f, barW, rh - 6.0f};
        l.rowPlus[i]  = {ctrlRight - stepW, ry + 3.0f, stepW, rh - 6.0f};
    }
    return l;
}

void adjustGraphicsRow(GraphicsSettings& s, int row, int delta) {
    switch (row) {
        case 0: s.stepRenderDistance(delta); return;
        case 1: s.buildingDetail = std::clamp(s.buildingDetail + delta, 0, 3); break;
        case 2: s.propDensity = std::clamp(s.propDensity + delta, 0, 3); break;
        case 3: s.shadowQuality = std::clamp(s.shadowQuality + delta, 0, 4); break;
        case 4: s.lightingQuality = std::clamp(s.lightingQuality + delta, 0, 3); break;
        case 5: s.reflectionParallax = std::clamp(s.reflectionParallax + delta, 0, 2); break;
        case 6: s.fogMode = std::clamp(s.fogMode + delta, 0, 2); break;
        case 7: s.trafficDensity = std::clamp(s.trafficDensity + delta, 0, 3); break;
        case 8: s.showFps = !s.showFps; return;
        default: return;
    }
    s.clampAndSyncPreset();
}

void setGraphicsRowFrac(GraphicsSettings& s, int row, float frac) {
    const float f = clampf(frac, 0.0f, 1.0f);
    switch (row) {
        case 0: s.renderDistanceM = GraphicsSettings::kRenderDistPresets[std::clamp(static_cast<int>(std::lround(f * 6.0f)), 0, 6)]; break;
        case 1: s.buildingDetail = std::clamp(static_cast<int>(std::lround(f * 3.0f)), 0, 3); break;
        case 2: s.propDensity = std::clamp(static_cast<int>(std::lround(f * 3.0f)), 0, 3); break;
        case 3: s.shadowQuality = std::clamp(static_cast<int>(std::lround(f * 4.0f)), 0, 4); break;
        case 4: s.lightingQuality = std::clamp(static_cast<int>(std::lround(f * 3.0f)), 0, 3); break;
        case 5: s.reflectionParallax = std::clamp(static_cast<int>(std::lround(f * 2.0f)), 0, 2); break;
        case 6: s.fogMode = std::clamp(static_cast<int>(std::lround(f * 2.0f)), 0, 2); break;
        case 7: s.trafficDensity = std::clamp(static_cast<int>(std::lround(f * 3.0f)), 0, 3); break;
        case 8: s.showFps = !s.showFps; return;
        default: return;
    }
    s.clampAndSyncPreset();
}
} // namespace

bool HudRenderer::handleGraphicsModalTap(int screenW, int screenH, Vec2 tapPos, GraphicsSettings& ioSettings) {
    if (!graphicsMenuOpen_) return false;
    const GraphicsModalLayout l = computeGraphicsModalLayout(screenW, screenH);
    if (!l.box.contains(tapPos.x, tapPos.z) || l.btnClose.contains(tapPos.x, tapPos.z)) {
        graphicsMenuOpen_ = false;
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (l.presetBtns[i].contains(tapPos.x, tapPos.z)) {
            ioSettings.applyPreset(static_cast<GraphicsQuality>(i));
            showGraphicsToast(GraphicsSettings::presetNameEn(ioSettings.preset),
                              GraphicsSettings::presetNameKo(ioSettings.preset));
            return true;
        }
    }
    for (int i = 0; i < GraphicsModalLayout::kRowCount; ++i) {
        if (l.rowMinus[i].contains(tapPos.x, tapPos.z)) {
            graphicsSelectedRow_ = i;
            adjustGraphicsRow(ioSettings, i, -1);
            return true;
        }
        if (l.rowPlus[i].contains(tapPos.x, tapPos.z)) {
            graphicsSelectedRow_ = i;
            adjustGraphicsRow(ioSettings, i, +1);
            return true;
        }
        if (l.rowBar[i].contains(tapPos.x, tapPos.z)) {
            graphicsSelectedRow_ = i;
            const float frac = (tapPos.x - l.rowBar[i].x) / std::max(1.0f, l.rowBar[i].w);
            setGraphicsRowFrac(ioSettings, i, frac);
            return true;
        }
        if (l.rowRects[i].contains(tapPos.x, tapPos.z)) {
            graphicsSelectedRow_ = i;
            if (i == 8) {
                ioSettings.showFps = !ioSettings.showFps;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HudRenderer::handleGraphicsModalInput(const PlatformInput& input, GraphicsSettings& ioSettings) {
    if (!graphicsMenuOpen_) return false;
    bool changed = false;
    if (input.isPressed(KeyCode::Up) || input.isPressed(KeyCode::W)) {
        graphicsSelectedRow_ = (graphicsSelectedRow_ + GraphicsModalLayout::kRowCount - 1) % GraphicsModalLayout::kRowCount;
    }
    if (input.isPressed(KeyCode::Down) || input.isPressed(KeyCode::S)) {
        graphicsSelectedRow_ = (graphicsSelectedRow_ + 1) % GraphicsModalLayout::kRowCount;
    }
    if (input.isPressed(KeyCode::Left) || input.isPressed(KeyCode::A)) {
        adjustGraphicsRow(ioSettings, graphicsSelectedRow_, -1);
        changed = true;
    }
    if (input.isPressed(KeyCode::Right) || input.isPressed(KeyCode::D)) {
        adjustGraphicsRow(ioSettings, graphicsSelectedRow_, +1);
        changed = true;
    }
    for (int k = 0; k < 4; ++k) {
        const KeyCode kc = static_cast<KeyCode>(static_cast<int>(KeyCode::Num1) + k);
        if (input.isPressed(kc)) {
            ioSettings.applyPreset(static_cast<GraphicsQuality>(k));
            showGraphicsToast(GraphicsSettings::presetNameEn(ioSettings.preset),
                              GraphicsSettings::presetNameKo(ioSettings.preset));
            changed = true;
        }
    }
    return changed;
}

void HudRenderer::drawRadarMinimap(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                                   int screenW, int screenH, float cameraYaw) {
    const RectF r = minimapRect(screenW, screenH);
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const CityMap& city = world.city();
    const Vec2 pPos = world.player().pos;

    // Outer sleek dark bezel
    ui.drawRoundedRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 12.0f, {10, 14, 22, 220});
    ui.drawRoundedRectOutline(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 12.0f, 2.0f, accent);

    // Clip radar contents strictly inside minimap box
    ui.pushScissor(vk, static_cast<int>(r.x), static_cast<int>(r.y),
                   static_cast<int>(r.w), static_cast<int>(r.h));

    ui.drawRect(r.x, r.y, r.w, r.h, {20, 26, 36, 235});

    // Local 900m radar span around player (North-up for intuitive Seoul geography!)
    const float spanMeters = 960.0f;
    const float pxPerM = r.w / spanMeters;
    const float cx = r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;
    auto toMap = [&](const Vec2& wp) -> Vec2 {
        return {cx + (wp.x - pPos.x) * pxPerM, cy + (wp.z - pPos.z) * pxPerM};
    };

    if (city.hasRealSeoulMap() && ui.hasMapTexture()) {
        // Real 2048x1312 OpenStreetMap Seoul Cartographic Radar Map (1:1 world-to-pixel exact!)
        const OcMapHeader& hdr = city.realData().header();
        const Vec2 mTL = toMap({hdr.minX, hdr.minZ});
        const Vec2 mBR = toMap({hdr.maxX, hdr.maxZ});
        ui.drawMapImage(mTL.x, mTL.z, mBR.x - mTL.x, mBR.z - mTL.z, 0.0f, 0.0f, 1.0f, 1.0f, {255, 255, 255, 245});
    } else {
        // 1. Draw Han River ribbon across radar!
        const int riverSegs = 16;
        for (int i = 0; i < riverSegs; ++i) {
            const float wx0 = pPos.x - spanMeters * 0.6f + spanMeters * 1.2f * (static_cast<float>(i) / riverSegs);
            const float wx1 = pPos.x - spanMeters * 0.6f + spanMeters * 1.2f * (static_cast<float>(i + 1) / riverSegs);
            const float cz0 = city.hanRiverCenterZ(wx0);
            const float cz1 = city.hanRiverCenterZ(wx1);
            const float hw0 = city.hanRiverHalfWidth(wx0);
            const float hw1 = city.hanRiverHalfWidth(wx1);
            const Vec2 a = toMap({wx0, cz0 - hw0});
            const Vec2 b = toMap({wx1, cz1 - hw1});
            const Vec2 c = toMap({wx1, cz1 + hw1});
            const Vec2 d = toMap({wx0, cz0 + hw0});
            ui.drawTriangle(a.x, a.z, b.x, b.z, c.x, c.z, {26, 82, 146, 232});
            ui.drawTriangle(a.x, a.z, c.x, c.z, d.x, d.z, {26, 82, 146, 232});
        }

        // 1B. Draw 4 Real Seoul Tributaries & Seokchon Lake on minimap!
        for (const auto& trib : city.tributaries()) {
            const float wPx = std::max(2.0f, trib.halfWidth * 2.0f * pxPerM);
            for (std::size_t i = 1; i < trib.points.size(); ++i) {
                const Vec2 p0 = toMap(trib.points[i - 1]);
                const Vec2 p1 = toMap(trib.points[i]);
                ui.drawLine(p0.x, p0.z, p1.x, p1.z, wPx, {30, 96, 162, 225});
            }
        }
        for (const auto& lake : city.lakes()) {
            const Vec2 p0 = toMap(lake.bounds.min);
            const Vec2 p1 = toMap(lake.bounds.max);
            ui.drawRect(p0.x, p0.z, std::max(2.0f, p1.x - p0.x), std::max(2.0f, p1.z - p0.z), {34, 112, 184, 235});
        }

        // 1C. Draw Parks & Green Spaces on minimap!
        for (const auto& park : city.parks()) {
            const Vec2 c = park.bounds.center();
            if (std::abs(c.x - pPos.x) > spanMeters * 0.65f || std::abs(c.z - pPos.z) > spanMeters * 0.65f) continue;
            const Vec2 p0 = toMap(park.bounds.min);
            const Vec2 p1 = toMap(park.bounds.max);
            ui.drawRect(p0.x, p0.z, std::max(2.0f, p1.x - p0.x), std::max(2.0f, p1.z - p0.z), {38, 86, 52, 180});
        }

        // 1D. Draw actual streamed Seoul Building Footprints on minimap (Naver Map / GTA V style!)
        for (const auto& b : city.buildings()) {
            const Vec2 bc = b.footprint.center();
            if (std::abs(bc.x - pPos.x) > spanMeters * 0.56f || std::abs(bc.z - pPos.z) > spanMeters * 0.56f) continue;
            const Vec2 p0 = toMap(b.footprint.min);
            const Vec2 p1 = toMap(b.footprint.max);
            const ColorRGBA bCol = b.isMacroLandmark
                ? ColorRGBA{176, 154, 92, 225}
                : (b.style == BuildingStyle::GlassTower ? ColorRGBA{58, 76, 98, 210}
                : (b.style == BuildingStyle::KoreanApartment ? ColorRGBA{72, 78, 90, 205}
                                                             : ColorRGBA{54, 62, 74, 195}));
            ui.drawRect(p0.x, p0.z, std::max(2.0f, p1.x - p0.x), std::max(2.0f, p1.z - p0.z), bCol);
        }

        // 2. Draw local road grid lines + real curved expressways (강변북로 / 올림픽대로)!
        const int rxC = city.nearestRoadIndex(pPos.x);
        const int rzC = city.nearestRoadIndex(pPos.z);
        for (int ix = rxC - 7; ix <= rxC + 7; ++ix) {
            const float rx = city.roadCoord(ix);
            const float sx = cx + (rx - pPos.x) * pxPerM;
            const bool major = city.isMajorArterialRoad(ix, false);
            ui.drawLine(sx, r.y, sx, r.y + r.h, major ? 2.6f : 1.0f, major ? ColorRGBA{112, 126, 148, 205} : ColorRGBA{52, 60, 74, 130});
        }
        for (int iz = rzC - 7; iz <= rzC + 7; ++iz) {
            const float rz = city.roadCoord(iz);
            const float sy = cy + (rz - pPos.z) * pxPerM;
            const bool major = city.isMajorArterialRoad(iz, true);
            ui.drawLine(r.x, sy, r.x + r.w, sy, major ? 2.6f : 1.0f, major ? ColorRGBA{112, 126, 148, 205} : ColorRGBA{52, 60, 74, 130});
        }
        for (const auto& art : city.arterials()) {
            if (!art.isExpressway) continue;
            for (std::size_t i = 1; i < art.points.size(); ++i) {
                const Vec2 p0 = toMap(art.points[i - 1]);
                const Vec2 p1 = toMap(art.points[i]);
                ui.drawLine(p0.x, p0.z, p1.x, p1.z, 3.2f, {46, 212, 134, 235});
            }
        }

        // 3. Bridges crossing Han River
        for (const auto& br : city.bridges()) {
            if (std::abs(br.centerX - pPos.x) > spanMeters * 0.7f) continue;
            const Vec2 p0 = toMap({br.centerX, br.zNorth});
            const Vec2 p1 = toMap({br.centerX, br.zSouth});
            ui.drawLine(p0.x, p0.z, p1.x, p1.z, 4.0f, {236, 194, 88, 240});
        }
    }

    // Police Search Zone & Active Cruisers
    if (world.player().wantedLevel > 0) {
        if (world.player().policeSearching) {
            const Vec2 lp = toMap(world.player().policeLastKnownPos);
            ui.drawCircleRing(lp.x, lp.z, 130.0f * pxPerM, 138.0f * pxPerM, {245, 72, 82, 165}, 24);
        }
        for (const auto& v : world.vehicles()) {
            if (!v.isPolice || v.destroyed) continue;
            const Vec2 vp = toMap(v.pos);
            const bool redB = std::sin(world.elapsedTime() * 11.0f) > 0.0f;
            ui.drawCircle(vp.x, vp.z, 4.2f, redB ? ColorRGBA{255, 58, 66, 255} : ColorRGBA{64, 148, 255, 255}, 10);
        }
    }

    // POIs & GPS Waypoint
    for (const auto& poi : city.pois()) {
        const Vec2 sp = toMap(poi.pos);
        if (r.contains(sp.x, sp.z)) ui.drawCircle(sp.x, sp.z, 3.6f, poi.markerColor, 10);
    }
    if (world.player().hasWaypoint) {
        const Vec2 wp = toMap(world.player().waypointPos);
        ui.drawLine(cx, cy, wp.x, wp.z, 1.8f, {72, 224, 255, 170});
        ui.drawCircle(clampf(wp.x, r.x + 7, r.x + r.w - 7),
                      clampf(wp.z, r.y + 7, r.y + r.h - 7), 5.0f, {72, 224, 255, 255}, 12);
    }

    // Player heading chevron in center
    const float yaw = world.player().drivingVehicleId >= 0 && world.playerVehicle()
        ? world.playerVehicle()->heading : world.player().facingYaw;
    const Vec2 fwd{std::cos(yaw), std::sin(yaw)};
    const Vec2 rt{-fwd.z, fwd.x};
    ui.drawTriangle(cx + fwd.x * 8.5f, cy + fwd.z * 8.5f,
                    cx - fwd.x * 6.0f + rt.x * 5.8f, cy - fwd.z * 6.0f + rt.z * 5.8f,
                    cx - fwd.x * 6.0f - rt.x * 5.8f, cy - fwd.z * 6.0f - rt.z * 5.8f,
                    {255, 255, 255, 255});

    ui.popScissor(vk);

    // District badge + clickable map hint pill + live Road Class / Expressway Corridor Badge!
    const DistrictType dist = city.districtAtWorld(pPos);
    const std::string distLabel = pickText(CityMap::districtNameEn(dist), CityMap::districtNameKo(dist));
    const float barH = mobileUi_ ? 22.0f : 24.0f;
    const float distFont = mobileUi_ ? 12.0f : 13.5f;
    ui.drawRoundedRect(r.x, r.y + r.h - barH, r.w, barH, 4.0f, {10, 14, 22, 212});
    ui.drawText(distLabel, r.x + 6.0f, r.y + r.h - barH + 3.5f, distFont, {235, 242, 252, 255});

    const std::string mapHint = mobileUi_
        ? pickText("MAP", "전체지도")
        : pickText("[M] MAP", "[M] 전체지도");
    const float hintFont = mobileUi_ ? 11.5f : 12.0f;
    const float hw = ui.measureTextWidth(mapHint, hintFont) + 12.0f;
    ui.drawRoundedRect(r.x + r.w - hw - 4.0f, r.y + 4.0f, hw, 20.0f, 5.0f, {12, 22, 36, 218});
    ui.drawRoundedRectOutline(r.x + r.w - hw - 4.0f, r.y + 4.0f, hw, 20.0f, 5.0f, 1.0f, accent);
    ui.drawText(mapHint, r.x + r.w - hw + 2.0f, r.y + 7.0f, hintFont, accent);

    // Live Korean Road Type Pill at top-left of minimap (instantly distinguishes Expressway / Ring Road / Bridge / City Avenue!)
    if (city.hasRealSeoulMap()) {
        const NearestOsmRoad nr = city.realData().findNearestRoad(pPos, 28.0f, world.player().posY);
        if (nr.found && nr.dist <= nr.halfWidth + 3.5f) {
            const char* rLblEn = "LOCAL";
            const char* rLblKo = "일반도로";
            ColorRGBA rCol{168, 184, 206, 245};
            ColorRGBA rBg{16, 22, 34, 220};
            if (nr.roadClass <= 1u) {
                if (pPos.x > 2050.0f && pPos.x < 3400.0f && pPos.z > 600.0f) {
                    rLblEn = mobileUi_ ? "GYEONGBU" : "GYEONGBU EXP";
                    rLblKo = mobileUi_ ? "경부고속" : "경부고속도로";
                } else if (pPos.z < -2600.0f) {
                    rLblEn = mobileUi_ ? "NAEBU RING" : "NAEBU CIRCULAR";
                    rLblKo = mobileUi_ ? "내부순환" : "내부순환로";
                } else if (pPos.x > 6400.0f && pPos.z > 800.0f) {
                    rLblEn = mobileUi_ ? "DONGBU EXP" : "BUNDANG-SUSEO";
                    rLblKo = mobileUi_ ? "분당수서" : "분당수서로";
                } else if (pPos.z > 150.0f && pPos.z < 2050.0f) {
                    rLblEn = mobileUi_ ? "OLYMPIC" : "OLYMPIC EXP";
                    rLblKo = mobileUi_ ? "올림픽로" : "올림픽대로";
                } else if (pPos.z > -1650.0f && pPos.z <= 150.0f) {
                    rLblEn = mobileUi_ ? "GANGBYEON" : "GANGBYEON EXP";
                    rLblKo = "강변북로";
                } else {
                    rLblEn = mobileUi_ ? "HIGHWAY" : "EXPRESSWAY";
                    rLblKo = mobileUi_ ? "고속도로" : "고속화도로";
                }
                rCol = {82, 242, 162, 255};
                rBg  = {12, 54, 38, 232};
            } else if (nr.isBridge) {
                const bool nearHan = (pPos.z > -1850.0f && pPos.z < 1250.0f);
                rLblEn = nearHan ? "HAN BRIDGE" : "BRIDGE";
                rLblKo = nearHan ? (mobileUi_ ? "한강대교" : "한강 대교") : (mobileUi_ ? "교량·고가" : "교량 / 고가");
                rCol = {255, 214, 88, 255};
                rBg  = {48, 38, 14, 226};
            } else if (nr.roadClass == 2u) {
                rLblEn = "ARTERIAL";
                rLblKo = "간선대로";
                rCol = {112, 198, 255, 255};
                rBg  = {14, 34, 62, 226};
            }
            const std::string roadTag = pickText(rLblEn, rLblKo);
            const float rFont = mobileUi_ ? 10.5f : 11.5f;
            const float rw = ui.measureTextWidth(roadTag, rFont) + 10.0f;
            const float maxRw = r.w - hw - 10.0f;
            if (rw <= maxRw) {
                ui.drawRoundedRect(r.x + 4.0f, r.y + 4.0f, rw, 20.0f, 5.0f, rBg);
                ui.drawRoundedRectOutline(r.x + 4.0f, r.y + 4.0f, rw, 20.0f, 5.0f, 1.0f, rCol);
                ui.drawText(roadTag, r.x + 9.0f, r.y + 7.5f, rFont, rCol);
            }
        }
    }
    (void)cameraYaw;
}

void HudRenderer::drawFullScreenSeoulMap(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                                         int screenW, int screenH) {
    const FullMapLayout l = computeFullMapLayout(screenW, screenH);
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const CityMap& city = world.city();

    // Backdrop dim + sleek dark command panel
    ui.drawRect(0, 0, static_cast<float>(screenW), static_cast<float>(screenH), {6, 10, 18, 212});
    ui.drawRoundedRect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, 14.0f, {12, 18, 28, 248});
    ui.drawRoundedRectOutline(l.panel.x, l.panel.y, l.panel.w, l.panel.h, 14.0f, 2.0f, accent);

    // Header title bar (concise on mobile so it never crowds the Close button)
    std::string title;
    if (mobileUi_) {
        title = city.hasRealSeoulMap()
            ? pickText("SEOUL REAL GIS MAP — Drag: Pan · Tap: GPS / Fast-Travel",
                       "서울 실제 지도 (OSM + 고도) — 드래그: 이동 · 탭: 목적지 설정")
            : pickText("SEOUL FULL MAP — Drag: Pan · Tap: GPS / Fast-Travel",
                       "서울 전체 지도 — 드래그: 이동 · 탭: 목적지 설정");
    } else {
        title = city.hasRealSeoulMap()
            ? pickText(
                "SEOUL REAL-WORLD GIS MAP (OpenStreetMap + SRTM 30m DEM) — Click map for GPS or Fast-Travel",
                "서울특별시 실제 지도 (OpenStreetMap + SRTM 30m 고도) — 지도를 클릭해 목적지 설정 또는 빠른 이동")
            : pickText(
                "SEOUL METROPOLIS FULL MAP (30km x 25km) — Click map to set GPS or Fast-Travel",
                "서울특별시 전체 지도 (실제 크기 30km × 25km) — 지도를 클릭해 목적지 설정 또는 빠른 이동");
    }
    ui.drawText(title, l.panel.x + 18.0f, l.panel.y + 13.0f, mobileUi_ ? 16.5f : 19.0f, {242, 248, 255, 255});

    // Close button
    ui.drawRoundedRect(l.btnClose.x, l.btnClose.y, l.btnClose.w, l.btnClose.h, 8.0f, {182, 42, 52, 235});
    ui.drawTextCentered(mobileUi_ ? pickText("CLOSE", "닫기") : pickText("CLOSE [M]", "닫기 [M]"),
                        l.btnClose.x + l.btnClose.w * 0.5f, l.btnClose.y + 8.0f, 14.5f, {255, 255, 255, 255});

    // --- MAP CANVAS (clipped cleanly!) ---
    ui.drawRoundedRect(l.canvas.x, l.canvas.y, l.canvas.w, l.canvas.h, 10.0f, {18, 24, 34, 255});
    ui.pushScissor(vk, static_cast<int>(l.canvas.x), static_cast<int>(l.canvas.y),
                   static_cast<int>(l.canvas.w), static_cast<int>(l.canvas.h));

    auto toScr = [&](const Vec2& wp) -> Vec2 {
        return mapWorldToScreen(l, city, wp, mapCenterWorld_, mapZoom_);
    };

    if (city.hasRealSeoulMap() && ui.hasMapTexture()) {
        // Real 2048x1312 OpenStreetMap Seoul Cartographic Map (exact 1:1 GPS alignment with 3D world!)
        const OcMapHeader& hdr = city.realData().header();
        const Vec2 tl = toScr({hdr.minX, hdr.minZ});
        const Vec2 br = toScr({hdr.maxX, hdr.maxZ});
        ui.drawMapImage(tl.x, tl.z, br.x - tl.x, br.z - tl.z, 0.0f, 0.0f, 1.0f, 1.0f);

        // Crisp Seoul District Watermark Labels aligned to real GPS coordinates!
        struct DistLbl { Vec2 pos; const char* en; const char* ko; };
        const DistLbl kRealDistLbls[10] = {
            {{-5550.0f, -3520.0f}, "Hongdae · Mapo",    "홍대 · 마포"},
            {{-5480.0f,   -40.0f}, "Yeouido Finance",   "여의도"},
            {{ -940.0f, -5300.0f}, "Gwanghwamun",       "광화문 · 종로"},
            {{ -320.0f, -1520.0f}, "Yongsan",           "용산"},
            {{  280.0f, -3100.0f}, "Namsan",            "남산"},
            {{ 2480.0f, -4550.0f}, "Dongdaemun",        "동대문"},
            {{ 5200.0f, -2380.0f}, "Seongsu · Forest",  "성수 · 서울숲"},
            {{ 1350.0f,  2380.0f}, "Seocho · Banpo",    "서초 · 반포"},
            {{ 4250.0f,  2450.0f}, "Gangnam · Teheran", "강남 · 테헤란로"},
            {{ 9350.0f,  1580.0f}, "Jamsil · Songpa",   "잠실 · 송파"}
        };
        for (const auto& dl : kRealDistLbls) {
            const Vec2 sp = toScr(dl.pos);
            ui.drawTextCentered(pickText(dl.en, dl.ko), sp.x, sp.z - 6.0f, 12.0f, {212, 224, 240, 175});
        }
    } else {
        // 1. Subtle 3km cartographic coordinate grid
        for (float gx = -15000.0f; gx <= 15000.0f; gx += 3000.0f) {
            const Vec2 a = toScr({gx, CityMap::kWorldMinZ});
            const Vec2 b = toScr({gx, CityMap::kWorldMaxZ});
            ui.drawLine(a.x, a.z, b.x, b.z, 1.0f, {30, 40, 54, 95});
        }
        for (float gz = -12000.0f; gz <= 12000.0f; gz += 3000.0f) {
            const Vec2 a = toScr({CityMap::kWorldMinX, gz});
            const Vec2 b = toScr({CityMap::kWorldMaxX, gz});
            ui.drawLine(a.x, a.z, b.x, b.z, 1.0f, {30, 40, 54, 95});
        }

        // 1B. Authentic Seoul Special City (서울특별시) 25-gu Municipal Basin Fill & Border + Ring Expressway!
        {
            static constexpr Vec2 kSeoulBorder[24] = {
                {-14400.0f,  -2200.0f}, {-12800.0f,  -4600.0f}, {-9600.0f,  -5600.0f}, {-6400.0f,  -7200.0f},
                { -3600.0f, -10400.0f}, {  -400.0f, -11800.0f}, { 3400.0f, -11600.0f}, { 6800.0f, -10200.0f},
                {  9200.0f,  -7200.0f}, { 11200.0f,  -4600.0f}, {13600.0f,  -3200.0f}, {14500.0f,   -600.0f},
                { 14200.0f,   2800.0f}, { 12600.0f,   6200.0f}, { 9800.0f,   8600.0f}, { 6400.0f,  10400.0f},
                {  2200.0f,  10600.0f}, { -1800.0f,   9600.0f}, {-5600.0f,   9400.0f}, {-8800.0f,   7600.0f},
                {-11600.0f,   5600.0f}, {-13600.0f,   3200.0f}, {-14400.0f,    800.0f}, {-14400.0f,  -2200.0f}
            };
            const Vec2 seoulCenter = toScr({0.0f, 0.0f});
            for (int i = 0; i < 23; ++i) {
                const Vec2 a = toScr(kSeoulBorder[i]);
                const Vec2 b = toScr(kSeoulBorder[i + 1]);
                ui.drawTriangle(seoulCenter.x, seoulCenter.z, a.x, a.z, b.x, b.z, {28, 36, 48, 205});
            }
            for (const auto& pm : city.perimeterMountains()) {
                const Vec2 c = toScr(pm.center);
                const Vec2 edge = toScr({pm.center.x + pm.radiusX, pm.center.z});
                const float rPx = std::max(10.0f, std::abs(edge.x - c.x));
                ui.drawCircle(c.x, c.z, rPx, {26, 56, 38, 120}, 20);
                ui.drawCircle(c.x, c.z, rPx * 0.56f, {36, 74, 48, 145}, 16);
                if (mapZoom_ >= 1.25f) {
                    ui.drawTextCentered(pickText(pm.nameEn.c_str(), pm.nameKo.c_str()), c.x, c.z - 6.0f, 10.5f, {140, 196, 152, 195});
                }
            }
            for (int i = 0; i < 23; ++i) {
                const Vec2 a = toScr(kSeoulBorder[i]);
                const Vec2 b = toScr(kSeoulBorder[i + 1]);
                ui.drawLine(a.x, a.z, b.x, b.z, 2.2f, {118, 168, 218, 195});
                const Vec2 oa = toScr(kSeoulBorder[i] * 1.045f);
                const Vec2 ob = toScr(kSeoulBorder[i + 1] * 1.045f);
                ui.drawLine(oa.x, oa.z, ob.x, ob.z, 1.4f, {202, 154, 72, 125});
            }
        }

        // 2. 7 Inner Seoul Mountains (topographic green contour ovals!)
        for (const auto& m : city.mountains()) {
            const Vec2 c = toScr(m.center);
            const Vec2 edge = toScr({m.center.x + m.radiusX, m.center.z});
            const float rPx = std::max(12.0f, std::abs(edge.x - c.x));
            ui.drawCircle(c.x, c.z, rPx, {34, 72, 46, 130}, 24);
            ui.drawCircle(c.x, c.z, rPx * 0.65f, {46, 94, 58, 155}, 20);
            ui.drawCircle(c.x, c.z, rPx * 0.30f, {70, 116, 76, 175}, 16);
            ui.drawTextCentered(pickText(m.nameEn.c_str(), m.nameKo.c_str()), c.x, c.z - 7.0f, 12.5f, {168, 224, 178, 230});
        }

        // 2B. 9 Real Iconic Seoul Parks & Plazas
        for (const auto& pk : city.majorParks()) {
            const Vec2 p0 = toScr(pk.center - pk.halfSize);
            const Vec2 p1 = toScr(pk.center + pk.halfSize);
            const float pw = std::max(6.0f, p1.x - p0.x);
            const float ph = std::max(5.0f, p1.z - p0.z);
            const ColorRGBA col = (pk.grassColor.r > 130) ? ColorRGBA{128, 134, 146, 190} : ColorRGBA{44, 112, 62, 185};
            ui.drawRoundedRect(p0.x, p0.z, pw, ph, 4.0f, col);
            const bool isCenterPlaza = (pk.halfSize.x < 120.0f);
            const bool isLakePark = (pk.nameEn == "Seokchon Lake Park");
            if (!isCenterPlaza && (!isLakePark || mapZoom_ >= 1.75f) && (mapZoom_ >= 1.45f || pk.halfSize.x >= 320.0f)) {
                const Vec2 sc = toScr(pk.center);
                ui.drawTextCentered(pickText(pk.nameEn.c_str(), pk.nameKo.c_str()), sc.x, sc.z - 6.0f, 11.0f, {168, 234, 176, 225});
            }
        }

        // 3. Real S-Curve Han River (한강) + 4 Real Seoul Tributaries!
        for (const auto& trib : city.tributaries()) {
            const float lw = std::max(2.2f, (trib.halfWidth * 0.06f) * mapZoom_);
            for (std::size_t i = 1; i < trib.points.size(); ++i) {
                const Vec2 a = toScr(trib.points[i - 1]);
                const Vec2 b = toScr(trib.points[i]);
                ui.drawLine(a.x, a.z, b.x, b.z, lw, {34, 108, 178, 225});
            }
            if (trib.points.size() >= 2) {
                const Vec2 mid = toScr(trib.points[trib.points.size() / 2]);
                ui.drawTextCentered(pickText(trib.nameEn.c_str(), trib.nameKo.c_str()), mid.x, mid.z - 6.0f, 11.0f, {132, 198, 250, 220});
            }
        }

        const int riverSteps = 90;
        for (int i = 0; i < riverSteps; ++i) {
            const float x0 = CityMap::kWorldMinX + (CityMap::kWorldMaxX - CityMap::kWorldMinX) * (static_cast<float>(i) / riverSteps);
            const float x1 = CityMap::kWorldMinX + (CityMap::kWorldMaxX - CityMap::kWorldMinX) * (static_cast<float>(i + 1) / riverSteps);
            const float cz0 = city.hanRiverCenterZ(x0);
            const float cz1 = city.hanRiverCenterZ(x1);
            const float hw0 = city.hanRiverHalfWidth(x0);
            const float hw1 = city.hanRiverHalfWidth(x1);
            const Vec2 a = toScr({x0, cz0 - hw0});
            const Vec2 b = toScr({x1, cz1 - hw1});
            const Vec2 c = toScr({x1, cz1 + hw1});
            const Vec2 d = toScr({x0, cz0 + hw0});
            ui.drawTriangle(a.x, a.z, b.x, b.z, c.x, c.z, {26, 94, 168, 240});
            ui.drawTriangle(a.x, a.z, c.x, c.z, d.x, d.z, {26, 94, 168, 240});
        }

        // Tilted Yeouido Island
        {
            const float cxW = -4800.0f;
            const float czW = city.hanRiverCenterZ(cxW) + 165.0f;
            const Vec2 yC = toScr({cxW, czW});
            const int segs = 16;
            const float cA = 0.9703f, sA = 0.2419f;
            for (int i = 0; i < segs; ++i) {
                const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
                const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
                const float lx0 = std::cos(a0) * 1920.0f, lz0 = std::sin(a0) * 535.0f;
                const float lx1 = std::cos(a1) * 1920.0f, lz1 = std::sin(a1) * 535.0f;
                const Vec2 p0 = toScr({cxW + lx0 * cA - lz0 * sA, czW + lx0 * sA + lz0 * cA});
                const Vec2 p1 = toScr({cxW + lx1 * cA - lz1 * sA, czW + lx1 * sA + lz1 * cA});
                ui.drawTriangle(yC.x, yC.z, p0.x, p0.z, p1.x, p1.z, {58, 68, 82, 250});
            }
            const Vec2 yp0 = toScr({-5420.0f, 160.0f});
            const Vec2 yp1 = toScr({-4180.0f, 460.0f});
            ui.drawLine(yp0.x, yp0.z, yp1.x, yp1.z, std::max(3.0f, 4.5f * mapZoom_), {56, 126, 68, 220});
        }

        for (const auto& lake : city.lakes()) {
            const Vec2 p0 = toScr(lake.bounds.min);
            const Vec2 p1 = toScr(lake.bounds.max);
            ui.drawRect(p0.x, p0.z, std::max(4.0f, p1.x - p0.x), std::max(3.0f, p1.z - p0.z), {42, 134, 212, 235});
        }

        // 4. Expressways & Named Boulevards
        for (const auto& art : city.arterials()) {
            const ColorRGBA col = art.isExpressway ? ColorRGBA{46, 212, 134, 225} : ColorRGBA{232, 204, 112, 195};
            const float lw = art.isExpressway ? 2.8f : 2.0f;
            for (std::size_t i = 1; i < art.points.size(); ++i) {
                const Vec2 sA = toScr(art.points[i - 1]);
                const Vec2 sB = toScr(art.points[i]);
                ui.drawLine(sA.x, sA.z, sB.x, sB.z, lw, col);
            }
        }

        // 4B. District Watermark Labels
        struct DistLbl { Vec2 pos; const char* en; const char* ko; };
        const DistLbl kDistLbls[12] = {
            {{-9800.0f,  -400.0f}, "Gangseo · Magok",  "강서 · 마곡"},
            {{-6600.0f, -2600.0f}, "Mapo · Hongdae",   "마포 · 홍대"},
            {{-5200.0f,  4600.0f}, "Guro · G-Valley",  "구로 · 디지털단지"},
            {{-2100.0f,  5900.0f}, "Gwanak · Sillim",  "관악 · 신림"},
            {{-1200.0f, -1500.0f}, "Yongsan",          "용산"},
            {{  200.0f,  3600.0f}, "Seocho · Banpo",   "서초 · 반포"},
            {{ 3600.0f,  4400.0f}, "Gangnam",          "강남 · 테헤란로"},
            {{ 4200.0f, -2400.0f}, "Seongsu · Konkuk", "성수 · 건대"},
            {{ 9200.0f,  3800.0f}, "Songpa · Jamsil",  "송파 · 잠실"},
            {{ 3200.0f, -8400.0f}, "Nowon · Gangbuk",  "강북 · 노원"},
            {{ 2800.0f, -4900.0f}, "Dongdaemun",       "동대문"},
            {{11800.0f,  -400.0f}, "Gangdong",         "강동"}
        };
        for (const auto& dl : kDistLbls) {
            const Vec2 sp = toScr(dl.pos);
            ui.drawTextCentered(pickText(dl.en, dl.ko), sp.x, sp.z - 6.0f, 12.0f, {146, 164, 192, 165});
        }

        // 5. All 15 Real Drivable Han River Bridges!
        for (const auto& br : city.bridges()) {
            const Vec2 a = toScr({br.centerX, br.zNorth - 40.0f});
            const Vec2 b = toScr({br.centerX, br.zSouth + 40.0f});
            ui.drawLine(a.x, a.z, b.x, b.z, 3.6f, {242, 198, 86, 245});
            if (mapZoom_ >= 1.55f || br.hasRainbowFountain || br.isCableStayed) {
                ui.drawTextCentered(pickText(br.nameEn.c_str(), br.nameKo.c_str()),
                                    a.x, 0.5f * (a.z + b.z) - 6.0f, 11.0f, {255, 232, 152, 240});
            }
        }
    }

    // 6. All 12 Iconic Real Seoul Landmarks (clickable gold/cyan pins with overlap-free staggered labels!)
    for (const auto& lm : city.landmarks()) {
        const Vec2 sp = toScr(lm.pos);
        const bool isSel = mapHasSelection_ && (lm.pos - mapSelectedWorld_).length() < 60.0f;
        const ColorRGBA pinCol = isSel ? ColorRGBA{72, 232, 255, 255} : ColorRGBA{255, 206, 62, 255};
        ui.drawCircle(sp.x, sp.z, isSel ? 7.5f : 5.5f, pinCol, 14);
        ui.drawCircleRing(sp.x, sp.z, isSel ? 9.5f : 7.0f, isSel ? 12.0f : 8.5f, pinCol, 16);

        float dx = 0.0f, dy = -25.0f;
        switch (lm.kind) {
            case SeoulLandmarkKind::NationalAssembly:    dx =   8.0f; dy = -25.0f; break;
            case SeoulLandmarkKind::Parc1RedTower:       dx =  -4.0f; dy =  12.0f; break;
            case SeoulLandmarkKind::Building63:          dx =  34.0f; dy =  14.0f; break;
            case SeoulLandmarkKind::SeoulCityHall:       dx = -46.0f; dy = -21.0f; break;
            case SeoulLandmarkKind::SungnyemunGate:      dx = -44.0f; dy =   9.0f; break;
            case SeoulLandmarkKind::DongdaemunDDP:       dx =  32.0f; dy = -23.0f; break;
            case SeoulLandmarkKind::NSeoulTower:         dx =  44.0f; dy = -12.0f; break;
            case SeoulLandmarkKind::JamsilStadium:       dx = -34.0f; dy = -25.0f; break;
            case SeoulLandmarkKind::LotteWorldTower:     dx = -16.0f; dy = -26.0f; break;
            case SeoulLandmarkKind::CoexTradeTower:      dx = -10.0f; dy =  14.0f; break;
            case SeoulLandmarkKind::GangnamStationPlaza: dx = -34.0f; dy =  12.0f; break;
            case SeoulLandmarkKind::GyeongbokgungPalace: dx =   0.0f; dy = -26.0f; break;
        }
        const std::string lbl = pickText(lm.nameEn.c_str(), lm.nameKo.c_str());
        const float lw = ui.measureTextWidth(lbl, 12.0f) + 8.0f;
        const float minLx = l.canvas.x + lw * 0.5f + 6.0f;
        const float maxLx = l.canvas.x + l.canvas.w - lw * 0.5f - 6.0f;
        const float lx = (minLx < maxLx) ? clampf(sp.x + dx, minLx, maxLx) : (sp.x + dx);
        const float ly = sp.z + dy;
        if (std::abs(lx - sp.x) > 10.0f) {
            ui.drawLine(sp.x, sp.z, lx, ly + 8.0f, 1.1f, {255, 206, 62, 135});
        }
        ui.drawRoundedRect(lx - lw * 0.5f, ly, lw, 16.5f, 4.0f, {10, 16, 26, 224});
        ui.drawTextCentered(lbl, lx, ly + 1.8f, 12.0f, {245, 248, 255, 255});
    }

    // 7. Active Police Last-Known Search Zone & Cruisers
    if (world.player().wantedLevel > 0) {
        if (world.player().policeSearching) {
            const Vec2 lp = toScr(world.player().policeLastKnownPos);
            const Vec2 re = toScr({world.player().policeLastKnownPos.x + 280.0f, world.player().policeLastKnownPos.z});
            const float rad = std::max(16.0f, std::abs(re.x - lp.x));
            ui.drawCircle(lp.x, lp.z, rad, {235, 52, 62, 58}, 24);
            ui.drawCircleRing(lp.x, lp.z, rad - 2.0f, rad + 1.5f, {255, 76, 86, 215}, 24);
            ui.drawTextCentered(pickText("POLICE SEARCH", "경찰 수색 구역"), lp.x, lp.z - rad - 16.0f, 12.0f, {255, 118, 124, 255});
        }
        for (const auto& v : world.vehicles()) {
            if (!v.isPolice || v.destroyed) continue;
            const Vec2 vp = toScr(v.pos);
            ui.drawCircle(vp.x, vp.z, 4.5f, {255, 62, 72, 255}, 10);
        }
    }

    // 8. Selected GPS Waypoint Pin & Route Line
    if (world.player().hasWaypoint || mapHasSelection_) {
        const Vec2 targetW = mapHasSelection_ ? mapSelectedWorld_ : world.player().waypointPos;
        const Vec2 pScr = toScr(world.player().pos);
        const Vec2 tScr = toScr(targetW);
        ui.drawLine(pScr.x, pScr.z, tScr.x, tScr.z, 2.2f, {72, 228, 255, 190});
        ui.drawCircle(tScr.x, tScr.z, 7.0f, {72, 228, 255, 255}, 14);
        ui.drawCircleRing(tScr.x, tScr.z, 10.0f, 13.0f, {72, 228, 255, 230}, 18);
    }

    // 9. Live Player Location Beacon (bright white-cyan chevron!)
    {
        const Vec2 ps = toScr(world.player().pos);
        const float yaw = world.player().drivingVehicleId >= 0 && world.playerVehicle()
            ? world.playerVehicle()->heading : world.player().facingYaw;
        const Vec2 fwd{std::cos(yaw), std::sin(yaw)};
        const Vec2 rt{-fwd.z, fwd.x};
        ui.drawCircle(ps.x, ps.z, 12.0f, {64, 212, 255, 65}, 16);
        ui.drawTriangle(ps.x + fwd.x * 11.0f, ps.z + fwd.z * 11.0f,
                        ps.x - fwd.x * 7.5f + rt.x * 7.2f, ps.z - fwd.z * 7.5f + rt.z * 7.2f,
                        ps.x - fwd.x * 7.5f - rt.x * 7.2f, ps.z - fwd.z * 7.5f - rt.z * 7.2f,
                        {255, 255, 255, 255});
        ui.drawTextCentered(pickText("YOU", "내 위치"), ps.x, ps.z + 13.0f, 12.5f, {118, 238, 255, 255});
    }

    ui.popScissor(vk);
    ui.drawRoundedRectOutline(l.canvas.x, l.canvas.y, l.canvas.w, l.canvas.h, 10.0f, 1.5f, {64, 82, 112, 210});

    // Zoom & Recenter Floating Controls inside Map Canvas
    ui.drawRoundedRect(l.btnZoomIn.x, l.btnZoomIn.y, l.btnZoomIn.w, l.btnZoomIn.h, 7.0f, {24, 34, 52, 235});
    ui.drawTextCentered("+", l.btnZoomIn.x + l.btnZoomIn.w * 0.5f, l.btnZoomIn.y + 5.0f, 22.0f, {242, 248, 255, 255});
    ui.drawRoundedRect(l.btnZoomOut.x, l.btnZoomOut.y, l.btnZoomOut.w, l.btnZoomOut.h, 7.0f, {24, 34, 52, 235});
    ui.drawTextCentered("-", l.btnZoomOut.x + l.btnZoomOut.w * 0.5f, l.btnZoomOut.y + 5.0f, 22.0f, {242, 248, 255, 255});
    ui.drawRoundedRect(l.btnRecenter.x, l.btnRecenter.y, l.btnRecenter.w, l.btnRecenter.h, 7.0f, {24, 34, 52, 235});
    ui.drawTextCentered(pickText("MY POS", "내 위치"), l.btnRecenter.x + l.btnRecenter.w * 0.5f, l.btnRecenter.y + 8.0f, 14.0f, accent);

    // Compact Korean Road Hierarchy Legend pill at top-right of map canvas (Expressway vs Arterial vs Local)
    {
        struct LegItem { const char* en; const char* ko; ColorRGBA col; ColorRGBA casing; };
        const LegItem kLeg[3] = {
            {"Expressway", "고속도로", {46, 212, 134, 255}, {14, 78, 52, 255}},
            {"Arterial",   "간선대로", {232, 204, 112, 255}, {68, 58, 28, 255}},
            {"Local",      "일반도로", {148, 160, 180, 255}, {44, 50, 62, 255}}
        };
        const float lf = mobileUi_ ? 11.5f : 12.5f;
        const float sw = 16.0f;
        const float itemGap = 12.0f;
        float totalW = 14.0f;
        float itemW[3]{};
        for (int i = 0; i < 3; ++i) {
            itemW[i] = sw + 5.0f + ui.measureTextWidth(pickText(kLeg[i].en, kLeg[i].ko), lf);
            totalW += itemW[i] + (i < 2 ? itemGap : 0.0f);
        }
        totalW += 10.0f;
        const float lx = l.canvas.x + l.canvas.w - totalW - 12.0f;
        const float ly = l.canvas.y + 12.0f;
        const float lh = 28.0f;
        if (lx > l.btnRecenter.x + l.btnRecenter.w + 8.0f) {
            ui.drawRoundedRect(lx, ly, totalW, lh, 7.0f, {10, 16, 26, 224});
            ui.drawRoundedRectOutline(lx, ly, totalW, lh, 7.0f, 1.0f, {58, 78, 108, 210});
            float cxCur = lx + 12.0f;
            for (int i = 0; i < 3; ++i) {
                const float midY = ly + lh * 0.5f;
                ui.drawRoundedRect(cxCur, midY - 3.5f, sw, 7.0f, 3.0f, kLeg[i].casing);
                ui.drawRoundedRect(cxCur + 1.0f, midY - 2.0f, sw - 2.0f, 4.0f, 2.0f, kLeg[i].col);
                ui.drawText(pickText(kLeg[i].en, kLeg[i].ko), cxCur + sw + 5.0f, ly + (mobileUi_ ? 7.5f : 6.8f),
                            lf, {226, 236, 250, 250});
                cxCur += itemW[i] + itemGap;
            }
        }
    }

    // Map scale bar & zoom readout at bottom-left of canvas
    char zBuf[160];
    std::snprintf(zBuf, sizeof(zBuf), "%s  |  %.1fx  |  %s",
                  city.hasRealSeoulMap()
                      ? pickText("Real OSM Seoul + SRTM 30m", "서울 실제 지도 (OSM + SRTM 30m)")
                      : pickText("30km x 25km Real-Scale Seoul", "서울 실측 30km × 25km"),
                  mapZoom_,
                  mobileUi_
                      ? pickText("Drag: Pan · Pinch/+/-: Zoom", "드래그: 이동 · 핀치/+/-: 확대")
                      : pickText("Drag RMB/WASD: Pan", "우클릭 드래그/WASD: 이동"));
    ui.drawRoundedRect(l.canvas.x + 12.0f, l.canvas.y + l.canvas.h - 32.0f,
                       ui.measureTextWidth(zBuf, 13.0f) + 20.0f, 24.0f, 6.0f, {10, 16, 26, 215});
    ui.drawText(zBuf, l.canvas.x + 22.0f, l.canvas.y + l.canvas.h - 28.0f, 13.0f, {212, 224, 242, 255});

    // --- RIGHT SIDEBAR: SEOUL LANDMARKS & FAST-TRAVEL ---
    ui.drawRoundedRect(l.sidebar.x, l.sidebar.y, l.sidebar.w, l.sidebar.h, 10.0f, {16, 22, 34, 245});
    ui.drawRoundedRectOutline(l.sidebar.x, l.sidebar.y, l.sidebar.w, l.sidebar.h, 10.0f, 1.5f, {58, 76, 104, 210});
    ui.drawText(mobileUi_
                    ? pickText("SEOUL LANDMARKS (TAP)", "서울 주요 랜드마크 (탭 선택)")
                    : pickText("SEOUL LANDMARKS", "서울 주요 랜드마크 (클릭 선택)"),
                l.sidebar.x + 12.0f, l.sidebar.y + 10.0f, 14.0f, accent);

    const float listTop = l.sidebar.y + 38.0f;
    const float listBottom = l.btnSetGps.y - 10.0f;
    const auto& landmarks = city.landmarks();
    const float rowH = std::min(32.0f, (listBottom - listTop) / std::max(1.0f, static_cast<float>(landmarks.size())));
    for (std::size_t i = 0; i < landmarks.size(); ++i) {
        const auto& lm = landmarks[i];
        const bool isSel = mapHasSelection_ && (lm.pos - mapSelectedWorld_).length() < 60.0f;
        const RectF row{l.sidebar.x + 8.0f, listTop + static_cast<float>(i) * rowH, l.sidebar.w - 16.0f, rowH - 3.0f};
        ui.drawRoundedRect(row.x, row.y, row.w, row.h, 6.0f,
                           isSel ? ColorRGBA{32, 86, 128, 235} : ColorRGBA{24, 32, 46, 210});
        const float distKm = (lm.pos - world.player().pos).length() / 1000.0f;
        char dBuf[24];
        std::snprintf(dBuf, sizeof(dBuf), "%.1fkm", distKm);
        ui.drawText(pickText(lm.nameEn.c_str(), lm.nameKo.c_str()), row.x + 8.0f, row.y + 6.0f, 13.0f,
                    isSel ? ColorRGBA{255, 255, 255, 255} : ColorRGBA{222, 232, 246, 255});
        const float dw = ui.measureTextWidth(dBuf, 12.0f);
        ui.drawText(dBuf, row.x + row.w - dw - 8.0f, row.y + 7.0f, 12.0f, {146, 178, 212, 230});
    }

    // Set GPS Waypoint + Fast-Travel Buttons!
    ui.drawRoundedRect(l.btnSetGps.x, l.btnSetGps.y, l.btnSetGps.w, l.btnSetGps.h, 8.0f, {32, 86, 132, 240});
    ui.drawTextCentered(pickText("SET GPS WAYPOINT", "GPS 목적지 설정"),
                        l.btnSetGps.x + l.btnSetGps.w * 0.5f, l.btnSetGps.y + 10.0f, 15.0f, {245, 250, 255, 255});

    ui.drawRoundedRect(l.btnFastTravel.x, l.btnFastTravel.y, l.btnFastTravel.w, l.btnFastTravel.h, 8.0f, {34, 148, 92, 245});
    const std::string ftLbl = mapHasSelection_
        ? (std::string(pickText("FAST TRAVEL: ", "빠른 이동: ")) + mapSelectedNameKo_)
        : (mobileUi_ ? std::string(pickText("FAST TRAVEL", "선택 지점으로 빠른 이동"))
                     : std::string(pickText("FAST TRAVEL [ENTER]", "선택 지점으로 빠른 이동")));
    ui.drawTextCentered(ftLbl, l.btnFastTravel.x + l.btnFastTravel.w * 0.5f, l.btnFastTravel.y + 11.0f,
                        14.5f, {255, 255, 255, 255});
}

void HudRenderer::drawTopRightStatus(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                                     bool firstPerson, const GraphicsSettings* gfxSettings) {
    const auto& pl = world.player();
    const ColorRGBA accent = getHudThemeColorRGBA(pl.hudCustom.hudThemeIdx);
    const float panelW = mobileUi_ ? 232.0f : 294.0f;
    const float panelH = mobileUi_ ? 98.0f : 120.0f;
    const float x = static_cast<float>(screenW) - panelW - (mobileUi_ ? 20.0f : 18.0f);
    const float y = mobileUi_ ? 12.0f : 16.0f;

    ui.drawRoundedRect(x, y, panelW, panelH, 11.0f, {10, 14, 22, 212});
    ui.drawRoundedRectOutline(x, y, panelW, panelH, 11.0f, 1.5f, accent);

    // Cash & Clock & Weather
    ui.drawText(fmtCash(pl.cash), x + 12.0f, y + (mobileUi_ ? 7.0f : 8.0f),
                mobileUi_ ? 20.0f : 22.5f, {92, 238, 136, 255});
    const std::string clk = fmtClock(world.timeOfDayHours()) + "  " +
        pickText(world.weatherNameEn(), world.weatherNameKo());
    const float clkFont = mobileUi_ ? 13.0f : 14.0f;
    const float cw = ui.measureTextWidth(clk, clkFont);
    ui.drawText(clk, x + panelW - cw - 12.0f, y + (mobileUi_ ? 11.0f : 12.0f), clkFont, {214, 226, 244, 245});

    // HP Bar & Armor Bar
    const float barW = panelW - 24.0f;
    const float hpF = clampf(pl.health / std::max(1.0f, pl.maxHealth), 0.0f, 1.0f);
    const float hpY = y + (mobileUi_ ? 36.0f : 40.0f);
    const float hpH = mobileUi_ ? 11.0f : 13.0f;
    ui.drawRoundedRect(x + 12.0f, hpY, barW, hpH, 5.0f, {34, 38, 48, 230});
    ui.drawRoundedRect(x + 12.0f, hpY, barW * hpF, hpH, 5.0f, {62, 206, 108, 255});

    const float armF = clampf(pl.armor / std::max(1.0f, pl.maxArmor), 0.0f, 1.0f);
    const float armY = y + (mobileUi_ ? 52.0f : 58.0f);
    const float armH = mobileUi_ ? 8.5f : 10.0f;
    ui.drawRoundedRect(x + 12.0f, armY, barW, armH, 4.0f, {34, 38, 48, 230});
    ui.drawRoundedRect(x + 12.0f, armY, barW * armF, armH, 4.0f, {68, 172, 248, 255});

    // Current Weapon & Ammo (+ Desktop Vulkan/Camera/FPS tag)
    const auto& ws = getWeaponSpec(pl.currentWeapon);
    const auto& slot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
    char wBuf[96];
    if (pl.currentWeapon == WeaponId::Fists) {
        std::snprintf(wBuf, sizeof(wBuf), "%s", pickText(ws.nameEn, ws.nameKo));
    } else {
        std::snprintf(wBuf, sizeof(wBuf), "%s  %d / %d", pickText(ws.nameEn, ws.nameKo), slot.magAmmo, slot.reserveAmmo);
    }
    ui.drawText(wBuf, x + 12.0f, y + (mobileUi_ ? 69.0f : 75.0f), mobileUi_ ? 14.5f : 15.5f, {244, 248, 255, 255});

    const bool showFps = gfxSettings ? gfxSettings->showFps : false;
    const ColorRGBA fpsCol = (fpsValue_ >= 45.0f) ? ColorRGBA{102, 240, 148, 255}
                           : ((fpsValue_ >= 25.0f) ? ColorRGBA{255, 214, 86, 255}
                                                   : ColorRGBA{255, 106, 98, 255});
    if (!mobileUi_) {
        std::string tag = std::string("Vulkan  |  ") +
                          (firstPerson ? pickText("1ST", "1인칭") : pickText("3RD", "3인칭"));
        if (gfxSettings) {
            char rBuf[32];
            std::snprintf(rBuf, sizeof(rBuf), "  |  %dm", gfxSettings->renderDistanceM);
            tag += rBuf;
        }
        ui.drawText(tag, x + 12.0f, y + 97.0f, 12.5f, {158, 188, 224, 230});
        if (showFps) {
            char fBuf[32];
            std::snprintf(fBuf, sizeof(fBuf), "%.0f FPS", fpsValue_);
            const float fw = ui.measureTextWidth(fBuf, 12.5f);
            ui.drawText(fBuf, x + panelW - fw - 12.0f, y + 97.0f, 12.5f, fpsCol);
        }
    } else if (showFps) {
        // Sleek FPS & Render Distance pill neatly tucked below the top-left minimap (zero overlap!)
        const RectF mm = minimapRect(screenW, screenH);
        const float py = mm.y + mm.h + 8.0f;
        ui.drawRoundedRect(mm.x, py, mm.w, 24.0f, 7.0f, {10, 16, 26, 206});
        ui.drawRoundedRectOutline(mm.x, py, mm.w, 24.0f, 7.0f, 1.0f, {64, 86, 118, 195});
        char fBuf[48];
        std::snprintf(fBuf, sizeof(fBuf), "%.0f FPS · %dm", fpsValue_,
                      gfxSettings ? gfxSettings->renderDistanceM : 850);
        ui.drawTextCentered(fBuf, mm.x + mm.w * 0.5f, py + 4.5f, 12.0f, fpsCol);
    }

    // --- REALISTIC ESCAPABLE SEOUL POLICE WANTED & EVASION HUD ---
    if (pl.wantedLevel > 0) {
        const float wy = y + panelH + 6.0f;
        const bool flash = std::sin(world.elapsedTime() * 9.0f) > 0.0f;
        const ColorRGBA badgeBg = pl.policeLineOfSight
            ? (flash ? ColorRGBA{128, 22, 32, 228} : ColorRGBA{22, 48, 128, 228})
            : ColorRGBA{16, 28, 46, 228};
        ui.drawRoundedRect(x, wy, panelW, 64.0f, 10.0f, badgeBg);
        ui.drawRoundedRectOutline(x, wy, panelW, 64.0f, 10.0f, 1.5f,
                                  pl.policeLineOfSight ? ColorRGBA{255, 78, 86, 255} : ColorRGBA{78, 212, 255, 255});

        // 5 Stars
        std::string stars;
        for (int i = 0; i < 5; ++i) stars += (i < pl.wantedLevel) ? "★ " : "☆ ";
        ui.drawText(stars, x + 12.0f, wy + 6.0f, mobileUi_ ? 18.5f : 20.0f, {255, 212, 62, 255});

        if (pl.policeLineOfSight) {
            ui.drawText(mobileUi_
                            ? pickText("IN SIGHT! Hide behind buildings", "경찰 추격! 건물 뒤로 숨으세요")
                            : pickText("CHASE: IN SIGHT! Break LOS behind buildings",
                                       "경찰 추격 중 — 시야 노출! 건물 뒤로 숨으세요"),
                        x + 12.0f, wy + 34.0f, mobileUi_ ? 12.0f : 13.0f, {255, 214, 214, 255});
        } else {
            const float evadeTarget = 11.5f + static_cast<float>(pl.wantedLevel) * 4.5f;
            const float pct = clampf(pl.wantedEvadeTimer / std::max(1.0f, evadeTarget), 0.0f, 1.0f);
            char eBuf[96];
            std::snprintf(eBuf, sizeof(eBuf), "%s %d%%",
                          mobileUi_
                              ? pickText("ESCAPING POLICE", "경찰 수색 중 — 도주")
                              : pickText("HIDING — ESCAPING POLICE", "경찰 수색 중 (시야 이탈) — 도주"),
                          static_cast<int>(pct * 100.0f));
            ui.drawText(eBuf, x + 12.0f, wy + 31.0f, mobileUi_ ? 12.0f : 13.0f, {122, 236, 255, 255});
            ui.drawRoundedRect(x + 12.0f, wy + 50.0f, barW, 8.0f, 4.0f, {26, 36, 52, 230});
            ui.drawRoundedRect(x + 12.0f, wy + 50.0f, barW * pct, 8.0f, 4.0f, {72, 228, 255, 255});
        }
    }
}

void HudRenderer::drawSpeedometerAndDrift(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH) {
    const Vehicle* car = world.playerVehicle();
    if (!car) return;

    const auto& spec = getVehicleSpec(car->kind);
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const float w = 232.0f;
    const float h = mobileUi_ ? 84.0f : 104.0f;
    const float x = static_cast<float>(screenW) - w - (mobileUi_ ? 20.0f : 22.0f);
    // Mobile speedometer dynamically stacks below status card (+ Wanted card when wanted) — ZERO overlap!
    const float mobileStackY = 12.0f + 98.0f + 6.0f + ((world.player().wantedLevel > 0) ? 70.0f : 0.0f);
    const float y = mobileUi_ ? mobileStackY : (static_cast<float>(screenH) - h - 22.0f);

    ui.drawRoundedRect(x, y, w, h, 11.0f, {10, 14, 22, 212});
    ui.drawRoundedRectOutline(x, y, w, h, 11.0f, 1.5f, accent);

    const float spdMs = car->speed();
    const bool useMph = world.player().hudCustom.useMph;
    const int dispSpd = static_cast<int>(spdMs * (useMph ? 2.23694f : 3.6f));
    char sBuf[32];
    std::snprintf(sBuf, sizeof(sBuf), "%3d", dispSpd);
    ui.drawText(sBuf, x + 12.0f, y + (mobileUi_ ? 6.0f : 10.0f), mobileUi_ ? 31.0f : 36.0f, {248, 252, 255, 255});
    ui.drawText(useMph ? "MPH" : "KM/H", x + (mobileUi_ ? 86.0f : 98.0f),
                y + (mobileUi_ ? 20.0f : 26.0f), mobileUi_ ? 12.5f : 14.0f, accent);

    // Live Gear Indicator (R / 1..6)
    char gBuf[16];
    if (car->currentGear == 0) std::snprintf(gBuf, sizeof(gBuf), "R");
    else std::snprintf(gBuf, sizeof(gBuf), "G%d", car->currentGear);
    const float gearH = mobileUi_ ? 26.0f : 30.0f;
    ui.drawRoundedRect(x + w - 46.0f, y + (mobileUi_ ? 9.0f : 12.0f), 34.0f, gearH, 6.0f, {24, 34, 52, 235});
    ui.drawTextCentered(gBuf, x + w - 29.0f, y + (mobileUi_ ? 13.0f : 17.0f), mobileUi_ ? 15.5f : 17.0f, {255, 214, 72, 255});

    // RPM Bar + Nitro Bar
    const float rpmFrac = clampf((car->engineRpm - 880.0f) / 7200.0f, 0.0f, 1.0f);
    const ColorRGBA rpmCol = (rpmFrac > 0.84f) ? ColorRGBA{248, 62, 62, 255} : accent;
    const float rpmY = y + (mobileUi_ ? 44.0f : 54.0f);
    const float rpmH = mobileUi_ ? 8.0f : 10.0f;
    ui.drawRoundedRect(x + 12.0f, rpmY, w - 24.0f, rpmH, 4.0f, {30, 36, 48, 230});
    ui.drawRoundedRect(x + 12.0f, rpmY, (w - 24.0f) * rpmFrac, rpmH, 4.0f, rpmCol);

    const float nosY = y + (mobileUi_ ? 55.0f : 69.0f);
    const float nosH = mobileUi_ ? 5.5f : 7.0f;
    ui.drawRoundedRect(x + 12.0f, nosY, w - 24.0f, nosH, 3.0f, {30, 36, 48, 230});
    ui.drawRoundedRect(x + 12.0f, nosY, (w - 24.0f) * clampf(car->nitroCharge, 0.0f, 1.0f),
                       nosH, 3.0f, {72, 206, 255, 255});

    ui.drawText(pickText(spec.nameEn, spec.nameKo), x + 12.0f, y + (mobileUi_ ? 64.0f : 82.0f),
                mobileUi_ ? 12.0f : 13.0f, {196, 212, 232, 235});

    if (world.player().activeDriftScore > 25.0f) {
        char dBuf[48];
        std::snprintf(dBuf, sizeof(dBuf), "DRIFT +%d", static_cast<int>(world.player().activeDriftScore));
        const float driftX = mobileUi_ ? (static_cast<float>(screenW) * 0.5f) : (x + w * 0.5f);
        const float driftY = mobileUi_ ? 92.0f : (y - 28.0f);
        ui.drawTextCentered(dBuf, driftX, driftY, 19.0f, {255, 208, 62, 255});
    }
}

void HudRenderer::drawCrosshair(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                                bool /*firstPerson*/, float adsBlend) {
    const auto& pl = world.player();
    if (pl.drivingVehicleId >= 0 || isAnyModalOpen()) return;
    const float cx = static_cast<float>(screenW) * 0.5f;
    const float cy = static_cast<float>(screenH) * 0.5f;
    const ColorRGBA col = pl.crosshairOnTarget
        ? ColorRGBA{255, 68, 72, 255}
        : getCrosshairColorRGBA(pl.hudCustom.crosshairColorIdx);
    const float gap = 5.0f * (1.0f - adsBlend * 0.45f);
    const float len = 8.0f;
    ui.drawLine(cx - gap - len, cy, cx - gap, cy, 2.0f, col);
    ui.drawLine(cx + gap, cy, cx + gap + len, cy, 2.0f, col);
    ui.drawLine(cx, cy - gap - len, cx, cy - gap, 2.0f, col);
    ui.drawLine(cx, cy + gap, cx, cy + gap + len, 2.0f, col);
    ui.drawCircle(cx, cy, 2.0f, col, 10);

    if (pl.hitMarkerTimer > 0.01f) {
        const ColorRGBA hc = pl.hitMarkerCritical ? ColorRGBA{255, 62, 62, 255} : ColorRGBA{255, 255, 255, 255};
        ui.drawLine(cx - 12, cy - 12, cx - 5, cy - 5, 2.2f, hc);
        ui.drawLine(cx + 12, cy - 12, cx + 5, cy - 5, 2.2f, hc);
        ui.drawLine(cx - 12, cy + 12, cx - 5, cy + 5, 2.2f, hc);
        ui.drawLine(cx + 12, cy + 12, cx + 5, cy + 5, 2.2f, hc);
    }
}

void HudRenderer::drawContextPromptsAndMission(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH) {
    const float cx = static_cast<float>(screenW) * 0.5f;
    // Clear the mobile top quick toolbar (y=12..46) cleanly!
    const float pillY = mobileUi_ ? 54.0f : 16.0f;
    if (world.mission().kind != ActiveMissionKind::None) {
        const std::string obj = pickText(world.mission().objectiveEn.c_str(), world.mission().objectiveKo.c_str());
        const float ow = ui.measureTextWidth(obj, 15.0f) + 28.0f;
        ui.drawRoundedRect(cx - ow * 0.5f, pillY, ow, 30.0f, 8.0f, {14, 22, 36, 215});
        ui.drawTextCentered(obj, cx, pillY + 7.0f, 15.0f, {255, 218, 86, 255});
    } else if (world.player().hasWaypoint) {
        const float distKm = (world.player().waypointPos - world.player().pos).length() / 1000.0f;
        char wBuf[96];
        std::snprintf(wBuf, sizeof(wBuf), "GPS: %s (%.2f km)",
                      world.player().waypointLabelKo.empty() ? "목적지" : world.player().waypointLabelKo.c_str(),
                      distKm);
        const float ww = ui.measureTextWidth(wBuf, 14.0f) + 26.0f;
        ui.drawRoundedRect(cx - ww * 0.5f, pillY, ww, 28.0f, 8.0f, {12, 24, 40, 215});
        ui.drawTextCentered(wBuf, cx, pillY + 6.0f, 14.0f, {86, 228, 255, 255});
    }

    if (const PointOfInterest* poi = world.nearbyPoi()) {
        const std::string msg = mobileUi_
            ? (std::string(pickText("TAP SHOP: ", "상점 버튼: ")) + pickText(poi->nameEn.c_str(), poi->nameKo.c_str()))
            : (std::string("[E] ") + pickText(poi->nameEn.c_str(), poi->nameKo.c_str()));
        const float mw = ui.measureTextWidth(msg, 15.0f) + 28.0f;
        const float py = static_cast<float>(screenH) - (mobileUi_ ? 58.0f : 108.0f);
        ui.drawRoundedRect(cx - mw * 0.5f, py, mw, 32.0f, 8.0f, {14, 20, 32, 220});
        ui.drawTextCentered(msg, cx, py + 7.0f, 15.0f, poi->markerColor);
    }
}

void HudRenderer::drawShopModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH) {
    const float w = static_cast<float>(screenW), h = static_cast<float>(screenH);
    const float mw = std::min(580.0f, w * 0.84f);
    const float mh = std::min(520.0f, h * 0.86f);
    const RectF box{(w - mw) * 0.5f, (h - mh) * 0.5f, mw, mh};
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);

    ui.drawRoundedRect(box.x, box.y, box.w, box.h, 12.0f, {12, 18, 28, 244});
    ui.drawRoundedRectOutline(box.x, box.y, box.w, box.h, 12.0f, 2.0f, accent);
    const auto* poi = world.nearbyPoi();
    ui.drawText(poi ? pickText(poi->nameEn.c_str(), poi->nameKo.c_str()) : pickText("SHOP", "상점"),
                box.x + 18.0f, box.y + 14.0f, 20.0f, accent);
    ui.drawRoundedRect(box.x + box.w - 86.0f, box.y + 10.0f, 74.0f, 30.0f, 6.0f, {178, 44, 52, 235});
    ui.drawTextCentered(pickText("CLOSE", "닫기"), box.x + box.w - 49.0f, box.y + 16.0f, 14.0f, {255, 255, 255, 255});

    const char* itemsKo[8] = {
        "[1] 체력/방탄복 보급 ($250)", "[2] 9mm 권총/탄약 ($350)",
        "[3] 기관단총 SMG ($950)", "[4] 전술 샷건 ($1400)",
        "[5] 돌격소총 AR ($2200)", "[6] 저격소총 ($3400)",
        "[7] 차량 수리/엔진 튜닝 ($600)", "[8] 니트로/네온 커스텀 ($450)"
    };
    const float top = box.y + 58.0f;
    const float rowH = (box.y + box.h - 20.0f - top) / 8.0f;
    for (int i = 0; i < 8; ++i) {
        ui.drawRoundedRect(box.x + 16.0f, top + static_cast<float>(i) * rowH, box.w - 32.0f, rowH - 6.0f, 7.0f, {22, 32, 48, 225});
        ui.drawText(itemsKo[i], box.x + 28.0f, top + static_cast<float>(i) * rowH + 10.0f, 15.0f, {236, 244, 255, 255});
    }
}

void HudRenderer::drawSmartphoneModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH) {
    const float w = static_cast<float>(screenW), h = static_cast<float>(screenH);
    const float mw = std::min(580.0f, w * 0.84f);
    const float mh = std::min(520.0f, h * 0.86f);
    const RectF box{(w - mw) * 0.5f, (h - mh) * 0.5f, mw, mh};
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);

    ui.drawRoundedRect(box.x, box.y, box.w, box.h, 12.0f, {12, 18, 28, 244});
    ui.drawRoundedRectOutline(box.x, box.y, box.w, box.h, 12.0f, 2.0f, accent);
    ui.drawText(mobileUi_ ? pickText("SEOUL SMARTPHONE", "서울 스마트폰")
                          : pickText("SEOUL SMARTPHONE [P]", "서울 스마트폰 [P]"),
                box.x + 18.0f, box.y + 14.0f, 20.0f, accent);
    ui.drawRoundedRect(box.x + box.w - 86.0f, box.y + 10.0f, 74.0f, 30.0f, 6.0f, {178, 44, 52, 235});
    ui.drawTextCentered(pickText("CLOSE", "닫기"), box.x + box.w - 49.0f, box.y + 16.0f, 14.0f, {255, 255, 255, 255});

    const char* tabs[4] = {"차고 호출", "부동산", "긴급 지원", "서울 미션"};
    const float tabW = (box.w - 28.0f) / 4.0f;
    for (int t = 0; t < 4; ++t) {
        const float tx = box.x + 14.0f + static_cast<float>(t) * tabW;
        ui.drawRoundedRect(tx, box.y + 48.0f, tabW - 4.0f, 32.0f, 6.0f,
                           (phoneTab_ == t) ? accent : ColorRGBA{26, 36, 52, 230});
        ui.drawTextCentered(tabs[t], tx + (tabW - 4.0f) * 0.5f, box.y + 55.0f, 14.0f,
                            (phoneTab_ == t) ? ColorRGBA{12, 16, 22, 255} : ColorRGBA{222, 232, 245, 255});
    }

    const float top = box.y + 92.0f;
    const float rowH = (box.y + box.h - 20.0f - top) / 8.0f;
    for (int i = 0; i < 8; ++i) {
        char lblBuf[160];
        lblBuf[0] = '\0';
        if (phoneTab_ == 0 && i < static_cast<int>(kDealershipCatalog.size())) {
            const auto& sp = getVehicleSpec(kDealershipCatalog[static_cast<std::size_t>(i)]);
            std::snprintf(lblBuf, sizeof(lblBuf), "[%d] %s", i + 1, pickText(sp.nameEn, sp.nameKo));
        } else if (phoneTab_ == 1 && i < static_cast<int>(PropertyId::Count)) {
            const auto& pr = getPropertySpec(static_cast<PropertyId>(i));
            std::snprintf(lblBuf, sizeof(lblBuf), "[%d] %s ($%d)", i + 1, pickText(pr.nameEn, pr.nameKo), pr.price);
        }
        std::string label = lblBuf;
        if (phoneTab_ == 2) {
            const char* srv[8] = {
                "[1] 수배 해제 (뇌물 $600)", "[2] 현장 차량 수리 ($250)",
                "[3] 119 응급 구급키트 ($200)", "[4] 맑은 날씨로 변경",
                "[5] 비 오는 서울 날씨", "[6] 시간 +2시간 이동",
                "[7] 서울 해치 택시 호출", "[8] 내 차로 즉시 복귀"
            };
            label = srv[i];
        } else if (phoneTab_ == 3) {
            const char* mis[8] = {
                "[1] 강남 슈퍼카 배달 시작", "[2] 한강변 스트리트 레이스",
                "[3] 갱단 아지트 소탕 작전", "[4] 현재 미션 포기",
                "", "", "", ""
            };
            label = mis[i];
        }
        if (label.empty()) continue;
        ui.drawRoundedRect(box.x + 16.0f, top + static_cast<float>(i) * rowH, box.w - 32.0f, rowH - 5.0f, 7.0f, {22, 32, 48, 225});
        ui.drawText(label, box.x + 28.0f, top + static_cast<float>(i) * rowH + 9.0f, 14.5f, {236, 244, 255, 255});
    }
}

void HudRenderer::drawCustomStudioModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH) {
    const float w = static_cast<float>(screenW), h = static_cast<float>(screenH);
    const float mw = std::min(580.0f, w * 0.84f);
    const float mh = std::min(520.0f, h * 0.86f);
    const RectF box{(w - mw) * 0.5f, (h - mh) * 0.5f, mw, mh};
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);

    ui.drawRoundedRect(box.x, box.y, box.w, box.h, 12.0f, {12, 18, 28, 244});
    ui.drawRoundedRectOutline(box.x, box.y, box.w, box.h, 12.0f, 2.0f, accent);
    ui.drawText(mobileUi_ ? pickText("CUSTOM STUDIO", "커스텀 스튜디오")
                          : pickText("CUSTOM STUDIO [K]", "커스텀 스튜디오 [K]"),
                box.x + 18.0f, box.y + 14.0f, 20.0f, accent);
    ui.drawRoundedRect(box.x + box.w - 86.0f, box.y + 10.0f, 74.0f, 30.0f, 6.0f, {178, 44, 52, 235});
    ui.drawTextCentered(pickText("CLOSE", "닫기"), box.x + box.w - 49.0f, box.y + 16.0f, 14.0f, {255, 255, 255, 255});

    const char* tabs[4] = {"차량 커스텀", "캐릭터", "무기 도색", "HUD 테마"};
    const float tabW = (box.w - 28.0f) / 4.0f;
    for (int t = 0; t < 4; ++t) {
        const float tx = box.x + 14.0f + static_cast<float>(t) * tabW;
        ui.drawRoundedRect(tx, box.y + 48.0f, tabW - 4.0f, 32.0f, 6.0f,
                           (customTab_ == t) ? accent : ColorRGBA{26, 36, 52, 230});
        ui.drawTextCentered(tabs[t], tx + (tabW - 4.0f) * 0.5f, box.y + 55.0f, 14.0f,
                            (customTab_ == t) ? ColorRGBA{12, 16, 22, 255} : ColorRGBA{222, 232, 245, 255});
    }
    const char* opts[4][6] = {
        {"[1] 메탈릭 컬러 변경", "[2] 레이싱 스트라이프", "[3] 카본 GT 윙", "[4] 휠 림 스타일", "[5] 서스펜션 스탠스", "[6] 무광/크롬 전환"},
        {"[1] 헤어/헬멧 변경", "[2] 헤어 컬러", "[3] 선글라스/고글", "[4] 의상 변경", "[5] 재킷 컬러", "[6] 백팩 장비"},
        {"[1] 무기 스킨 변경", "[2] 소음기 탈착", "[3] 대용량 탄창", "[4] 레이저 사이트", "[5] 예광탄 컬러", "[6] 조준경 컬러"},
        {"[1] 조준선 모양", "[2] 조준선 색상", "[3] KM/H ↔ MPH 전환", "[4] HUD 강조 테마", "", ""}
    };
    const float top = box.y + 92.0f;
    const float rowH = (box.y + box.h - 20.0f - top) / 6.0f;
    for (int i = 0; i < 6; ++i) {
        if (opts[customTab_][i][0] == '\0') continue;
        ui.drawRoundedRect(box.x + 16.0f, top + static_cast<float>(i) * rowH, box.w - 32.0f, rowH - 6.0f, 7.0f, {22, 32, 48, 225});
        ui.drawText(opts[customTab_][i], box.x + 28.0f, top + static_cast<float>(i) * rowH + 10.0f, 15.0f, {236, 244, 255, 255});
    }
}

void HudRenderer::drawGraphicsModal(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                                    const GraphicsSettings& gfx) {
    const GraphicsModalLayout l = computeGraphicsModalLayout(screenW, screenH);
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);

    // Subtle dim backdrop so settings stay crisp while keeping live 3D Seoul visible behind!
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenW), static_cast<float>(screenH), {4, 8, 14, 118});

    ui.drawRoundedRect(l.box.x, l.box.y, l.box.w, l.box.h, 13.0f, {11, 17, 27, 246});
    ui.drawRoundedRectOutline(l.box.x, l.box.y, l.box.w, l.box.h, 13.0f, 2.0f, accent);

    // Header Title + Active Preset/FPS Badge + Close button
    ui.drawText(mobileUi_ ? pickText("GRAPHICS SETTINGS", "그래픽 상세 설정")
                          : pickText("GRAPHICS SETTINGS [U]", "그래픽 상세 설정 [U]"),
                l.box.x + 18.0f, l.box.y + 13.0f, 19.5f, accent);

    char badgeBuf[80];
    std::snprintf(badgeBuf, sizeof(badgeBuf), "%s · %dm · %.0f FPS",
                  pickText(GraphicsSettings::presetNameEn(gfx.preset),
                           GraphicsSettings::presetNameKo(gfx.preset)),
                  gfx.renderDistanceM, fpsValue_);
    const float bw = ui.measureTextWidth(badgeBuf, 12.5f) + 16.0f;
    const float bx = l.btnClose.x - bw - 10.0f;
    ui.drawRoundedRect(bx, l.box.y + 13.0f, bw, 24.0f, 6.0f, {22, 34, 52, 235});
    ui.drawTextCentered(badgeBuf, bx + bw * 0.5f, l.box.y + 18.0f, 12.5f, {168, 224, 255, 255});

    ui.drawRoundedRect(l.btnClose.x, l.btnClose.y, l.btnClose.w, l.btnClose.h, 6.0f, {178, 44, 52, 238});
    ui.drawTextCentered(pickText("CLOSE", "닫기"), l.btnClose.x + l.btnClose.w * 0.5f, l.btnClose.y + 7.0f,
                        14.0f, {255, 255, 255, 255});

    // Top 4 Master Preset Quick-Select Pills
    const GraphicsQuality kPresets[4] = {
        GraphicsQuality::Ultra, GraphicsQuality::High, GraphicsQuality::Medium, GraphicsQuality::Performance
    };
    for (int i = 0; i < 4; ++i) {
        const RectF& pr = l.presetBtns[i];
        const bool active = (gfx.preset == kPresets[i]);
        ui.drawRoundedRect(pr.x, pr.y, pr.w, pr.h, 7.0f,
                           active ? ColorRGBA{16, 128, 168, 245} : ColorRGBA{24, 34, 50, 230});
        ui.drawRoundedRectOutline(pr.x, pr.y, pr.w, pr.h, 7.0f,
                                  active ? 2.0f : 1.0f,
                                  active ? accent : ColorRGBA{58, 76, 104, 190});
        ui.drawTextCentered(pickText(GraphicsSettings::presetNameEn(kPresets[i]),
                                     GraphicsSettings::presetNameKo(kPresets[i])),
                            pr.x + pr.w * 0.5f, pr.y + 7.5f, 14.0f,
                            active ? ColorRGBA{255, 255, 255, 255} : ColorRGBA{214, 228, 244, 255});
    }

    // 9 Granular Interactive Rows with ◀ | Visual Bar + Label | ▶
    struct RowSpec {
        const char* nameEn;
        const char* nameKo;
        std::string val;
        float frac;
        bool activeGlow;
    };

    char distVal[32];
    std::snprintf(distVal, sizeof(distVal), "%d m (%dx%d)", gfx.renderDistanceM,
                  gfx.chunkRadius() * 2 + 1, gfx.chunkRadius() * 2 + 1);

    const char* kDetailEn[4]  = {"Low", "Medium", "High", "Ultra"};
    const char* kDetailKo[4]  = {"낮음", "보통", "높음", "최고"};
    const char* kPropEn[4]    = {"Low (40%)", "Medium (70%)", "High (100%)", "Ultra (130%)"};
    const char* kPropKo[4]    = {"낮음 (40%)", "보통 (70%)", "높음 (100%)", "최고 (130%)"};
    const char* kShadEn[5]    = {"Off", "Low (1-Tap)", "Med (4-Tap)", "High (8-Tap)", "Ultra (16-PCF)"};
    const char* kShadKo[5]    = {"끄기", "낮음 (1-Tap)", "보통 (4-Tap)", "높음 (8-Tap)", "최고 (16-PCF)"};
    const char* kLightEn[4]   = {"Off", "Med (8 Lights)", "High (16 Lights)", "Ultra (20+Bloom)"};
    const char* kLightKo[4]   = {"끄기", "보통 (8광원)", "높음 (16광원)", "최고 (20광원+블룸)"};
    const char* kReflEn[3]    = {"Off", "Basic Refl", "Ultra (Voronoi+3D)"};
    const char* kReflKo[3]    = {"끄기", "기본 반사", "최고 (보로노이+실내)"};
    const char* kFogEn[3]     = {"Off", "Natural", "Dense"};
    const char* kFogKo[3]     = {"끄기", "자연스럽게", "짙게"};
    const char* kTrafEn[4]    = {"Low", "Medium", "High", "Crowded"};
    const char* kTrafKo[4]    = {"적음", "보통", "많음", "혼잡"};

    const RowSpec rows[GraphicsModalLayout::kRowCount] = {
        {"Render Distance",         "렌더 거리 (시야 범위)",      distVal,
         static_cast<float>(gfx.renderDistanceIndex()) / 6.0f, true},
        {"Building & Skyline",      "건물·스카이라인 디테일",     pickText(kDetailEn[gfx.buildingDetail], kDetailKo[gfx.buildingDetail]),
         static_cast<float>(gfx.buildingDetail) / 3.0f, gfx.buildingDetail > 0},
        {"Props & Street Trees",    "거리 구조물·가로수 밀도",    pickText(kPropEn[gfx.propDensity], kPropKo[gfx.propDensity]),
         static_cast<float>(gfx.propDensity) / 3.0f, gfx.propDensity > 0},
        {"Shadow Quality",          "그림자 품질",                pickText(kShadEn[gfx.shadowQuality], kShadKo[gfx.shadowQuality]),
         static_cast<float>(gfx.shadowQuality) / 4.0f, gfx.shadowQuality > 0},
        {"Dynamic Lights & Bloom",  "동적 광원 & HDR 블룸",       pickText(kLightEn[gfx.lightingQuality], kLightKo[gfx.lightingQuality]),
         static_cast<float>(gfx.lightingQuality) / 3.0f, gfx.lightingQuality > 0},
        {"Reflections & Parallax",  "노면 반사 & 3D 실내",        pickText(kReflEn[gfx.reflectionParallax], kReflKo[gfx.reflectionParallax]),
         static_cast<float>(gfx.reflectionParallax) / 2.0f, gfx.reflectionParallax > 0},
        {"Atmospheric Fog",         "대기 안개·원근감",           pickText(kFogEn[gfx.fogMode], kFogKo[gfx.fogMode]),
         static_cast<float>(gfx.fogMode) / 2.0f, gfx.fogMode > 0},
        {"Traffic & Crowd Density", "교통량 & 보행자 밀도",       pickText(kTrafEn[gfx.trafficDensity], kTrafKo[gfx.trafficDensity]),
         static_cast<float>(gfx.trafficDensity) / 3.0f, gfx.trafficDensity > 0},
        {"Live FPS Counter",        "실시간 FPS 표시",            gfx.showFps ? pickText("ON", "켜기") : pickText("OFF", "끄기"),
         gfx.showFps ? 1.0f : 0.0f, gfx.showFps}
    };

    for (int i = 0; i < GraphicsModalLayout::kRowCount; ++i) {
        const RectF& rr = l.rowRects[i];
        const RectF& rm = l.rowMinus[i];
        const RectF& rb = l.rowBar[i];
        const RectF& rp = l.rowPlus[i];
        const bool sel = (i == graphicsSelectedRow_);

        ui.drawRoundedRect(rr.x, rr.y, rr.w, rr.h, 7.0f,
                           sel ? ColorRGBA{26, 40, 62, 236} : ColorRGBA{18, 26, 40, 222});
        if (sel) {
            ui.drawRoundedRectOutline(rr.x, rr.y, rr.w, rr.h, 7.0f, 1.3f, accent);
        }
        const float lblY = rr.y + (rr.h - 14.5f) * 0.5f;
        ui.drawText(pickText(rows[i].nameEn, rows[i].nameKo), rr.x + 12.0f, lblY, 14.2f,
                    sel ? ColorRGBA{255, 255, 255, 255} : ColorRGBA{222, 234, 248, 245});

        // Minus ◀ button
        ui.drawRoundedRect(rm.x, rm.y, rm.w, rm.h, 6.0f, {30, 42, 62, 235});
        ui.drawTextCentered("◀", rm.x + rm.w * 0.5f, rm.y + (rm.h - 14.0f) * 0.5f, 14.0f, {226, 238, 252, 255});

        // Visual progress bar + centered crisp setting label
        ui.drawRoundedRect(rb.x, rb.y, rb.w, rb.h, 6.0f, {14, 20, 32, 240});
        const float fillW = rb.w * clampf(rows[i].frac, 0.0f, 1.0f);
        if (fillW > 2.0f) {
            const ColorRGBA barCol = rows[i].activeGlow
                ? ColorRGBA{static_cast<std::uint8_t>(accent.r * 3 / 5),
                            static_cast<std::uint8_t>(accent.g * 3 / 5),
                            static_cast<std::uint8_t>(accent.b * 3 / 5), 220}
                : ColorRGBA{68, 78, 96, 210};
            ui.drawRoundedRect(rb.x, rb.y, fillW, rb.h, 6.0f, barCol);
        }
        ui.drawRoundedRectOutline(rb.x, rb.y, rb.w, rb.h, 6.0f, 1.0f, {60, 80, 110, 190});
        ui.drawTextCentered(rows[i].val, rb.x + rb.w * 0.5f, rb.y + (rb.h - 13.5f) * 0.5f, 13.2f, {248, 252, 255, 255});

        // Plus ▶ button
        ui.drawRoundedRect(rp.x, rp.y, rp.w, rp.h, 6.0f, {30, 42, 62, 235});
        ui.drawTextCentered("▶", rp.x + rp.w * 0.5f, rp.y + (rp.h - 14.0f) * 0.5f, 14.0f, {226, 238, 252, 255});
    }

    // Footer tip bar
    const char* tip = mobileUi_
        ? pickText("All graphics settings apply instantly in real time.", "모든 그래픽 설정은 즉시 실시간으로 반영됩니다.")
        : pickText("Live preview · [↑↓] Select  [←→] Adjust  [1-4] Preset  [ESC/U] Close",
                   "실시간 즉시 반영 · [↑↓] 항목  [←→] 조절  [1~4] 프리셋  [ESC/U] 닫기");
    ui.drawTextCentered(tip, l.box.x + l.box.w * 0.5f, l.box.y + l.box.h - 23.0f, 12.2f, {162, 186, 216, 235});
}

void HudRenderer::drawBannerToast(Vulkan2D& ui, const GameWorld& world, int screenW, int /*screenH*/) {
    const float cx = static_cast<float>(screenW) * 0.5f;
    const float bannerY = mobileUi_ ? 90.0f : 64.0f;
    const float toastY  = mobileUi_ ? 156.0f : 130.0f;
    if (world.banner().timer > 0.01f) {
        const std::string t = pickText(world.banner().titleEn.c_str(), world.banner().titleKo.c_str());
        const std::string s = pickText(world.banner().subEn.c_str(), world.banner().subKo.c_str());
        const float bw = std::max(ui.measureTextWidth(t, 20.0f), ui.measureTextWidth(s, 14.0f)) + 44.0f;
        ui.drawRoundedRect(cx - bw * 0.5f, bannerY, bw, 58.0f, 10.0f, {10, 16, 26, 222});
        ui.drawRoundedRectOutline(cx - bw * 0.5f, bannerY, bw, 58.0f, 10.0f, 1.5f, world.banner().accent);
        ui.drawTextCentered(t, cx, bannerY + 7.0f, 20.0f, world.banner().accent);
        ui.drawTextCentered(s, cx, bannerY + 34.0f, 14.0f, {236, 242, 252, 255});
    }
    if (gfxToastTimer_ > 0.01f) {
        const std::string msg = pickText(gfxToastEn_.c_str(), gfxToastKo_.c_str());
        const float tw = ui.measureTextWidth(msg, 14.5f) + 28.0f;
        ui.drawRoundedRect(cx - tw * 0.5f, toastY, tw, 30.0f, 8.0f, {12, 20, 32, 218});
        ui.drawTextCentered(msg, cx, toastY + 6.0f, 14.5f, {118, 228, 255, 255});
    }
}

void HudRenderer::drawHelpOverlay(Vulkan2D& ui, const GameWorld& world, int screenW, int /*screenH*/, bool /*cursorLocked*/) {
    if (!showHelp_ || mobileUi_ || isAnyModalOpen()) return;
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const float w = 316.0f, h = 172.0f;
    ui.drawRoundedRect(18.0f, 16.0f, w, h, 10.0f, {10, 14, 22, 198});
    ui.drawRoundedRectOutline(18.0f, 16.0f, w, h, 10.0f, 1.2f, accent);
    ui.drawText(pickText("OPEN CITY SEOUL v6.2 (VULKAN)", "오픈 시티 서울 v6.2 (Vulkan 엔진)"), 28.0f, 24.0f, 14.5f, accent);
    const char* lines[7] = {
        "WASD: 이동/운전 | Space: 점프/핸드브레이크",
        "Shift: 달리기/니트로 | F/E: 차량 탑승/상점",
        "1~0: 무기 교체 | 우클릭: 조준 | R: 재장전",
        "M 또는 미니맵 클릭: 서울 전체 지도 & 빠른이동",
        "V: 1인칭/3인칭 | P: 스마트폰 | K: 커스텀",
        "Y: 날씨 변경 | T: 시간 이동 | U: 그래픽 설정",
        "H: 도움말 숨기기 | Tab: 마우스 잠금 해제"
    };
    for (int i = 0; i < 7; ++i) {
        ui.drawText(lines[i], 28.0f, 47.0f + static_cast<float>(i) * 18.2f, 12.5f, {214, 226, 242, 235});
    }
    (void)screenW;
}

void HudRenderer::renderHud(Vulkan2D& ui, VulkanContext& vk, const GameWorld& world,
                            int screenW, int screenH, float cameraYaw,
                            bool cursorLocked, bool firstPerson, float adsBlend,
                            const GraphicsSettings* gfxSettings) {
    if (fullMapOpen_) {
        drawFullScreenSeoulMap(ui, vk, world, screenW, screenH);
        return;
    }

    drawRadarMinimap(ui, vk, world, screenW, screenH, cameraYaw);
    drawTopRightStatus(ui, world, screenW, screenH, firstPerson, gfxSettings);
    drawSpeedometerAndDrift(ui, world, screenW, screenH);
    if (showCrosshair_) drawCrosshair(ui, world, screenW, screenH, firstPerson, adsBlend);
    drawContextPromptsAndMission(ui, world, screenW, screenH);
    drawBannerToast(ui, world, screenW, screenH);
    drawHelpOverlay(ui, world, screenW, screenH, cursorLocked);

    if (shopMenuOpen_) drawShopModal(ui, world, screenW, screenH);
    else if (phoneOpen_) drawSmartphoneModal(ui, world, screenW, screenH);
    else if (customStudioOpen_) drawCustomStudioModal(ui, world, screenW, screenH);
    else if (graphicsMenuOpen_ && gfxSettings) drawGraphicsModal(ui, world, screenW, screenH, *gfxSettings);

    // Wasted / Busted Screen Overlay
    if (world.player().wasted) {
        ui.drawRect(0, 0, static_cast<float>(screenW), static_cast<float>(screenH), {24, 6, 10, 145});
        const char* bigTxt = world.player().lastDeathWasBusted
            ? pickText("BUSTED — SEOUL POLICE", "체포됨 — 서울 경찰")
            : pickText("WASTED — RECOVERING AT HOSPITAL", "기절함 — 병원 이송 중");
        ui.drawTextCentered(bigTxt, static_cast<float>(screenW) * 0.5f,
                            static_cast<float>(screenH) * 0.44f, 34.0f, {255, 84, 88, 255});
    }
}

} // namespace oc
