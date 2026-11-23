#include "ui/TouchControls.hpp"

#include <algorithm>
#include <cmath>

namespace oc {

void TouchControls::resetState() {
    for (auto& p : pointers_) p = {};
    stickVisualOffset_ = {0.0f, 0.0f};
    stickSprinting_ = false;
    fireActiveVis_ = false;
    steerLeftVis_ = false;
    steerRightVis_ = false;
    gasVis_ = false;
    brakeVis_ = false;
    nitroVis_ = false;
    driftVis_ = false;
    lookBackVis_ = false;
    smoothTouchSteer_ = 0.0f;
    smoothTouchThrottle_ = 0.0f;
}

bool TouchControls::hitCircle(Vec2 p, Vec2 c, float r) {
    const float dx = p.x - c.x;
    const float dy = p.z - c.z;
    return (dx * dx + dy * dy) <= (r * r);
}

TouchControls::TrackedPointer* TouchControls::findPointer(int id) {
    for (auto& p : pointers_) {
        if (p.active && p.id == id) return &p;
    }
    return nullptr;
}

TouchControls::TrackedPointer* TouchControls::allocatePointer(int id, Vec2 pos) {
    for (auto& p : pointers_) {
        if (!p.active) {
            p.active = true;
            p.id = id;
            p.role = PointerRole::None;
            p.startPos = pos;
            p.pos = pos;
            p.prevPos = pos;
            return &p;
        }
    }
    return nullptr;
}

TouchControls::Layout TouchControls::computeLayout(int screenW, int screenH) {
    Layout l{};
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    // Screen coordinates arrive in DPI-normalized logical dp (~940..1333 x 600+ dp).
    const float s = std::min(clampf(h / 600.0f, 0.86f, 1.15f), clampf(w / 940.0f, 0.86f, 1.15f));
    l.s = s;

    // Safe-area margins clear of Android rounded corners, punch-hole camera, and gesture bar
    const float safeX = 24.0f * s;
    const float safeB = 20.0f * s;

    // On-foot left analog thumbstick
    l.stickOuterR = 74.0f * s;
    l.stickKnobR = 28.0f * s;
    l.stickCenter = {safeX + 88.0f * s, h - safeB - 88.0f * s};

    // Driving bottom-left steering paddles (thumb-ergonomic wide pads)
    const float padW = 102.0f * s;
    const float padH = 78.0f * s;
    l.steerLeftRect  = {safeX, h - safeB - padH, padW, padH};
    l.steerRightRect = {safeX + padW + 14.0f * s, h - safeB - padH, padW, padH};

    // On-foot bottom-right action arc (verified >= 15dp clearance between all controls)
    l.fireR = 45.0f * s;
    l.aimR = 31.0f * s;
    l.jumpR = 31.0f * s;
    l.reloadR = 27.0f * s;
    l.interactR = 31.0f * s;
    l.shopR = 31.0f * s;
    l.fireCenter     = {w - safeX - 70.0f * s,  h - safeB - 94.0f * s};
    l.jumpCenter     = {w - safeX - 162.0f * s, h - safeB - 52.0f * s};
    l.aimCenter      = {w - safeX - 160.0f * s, h - safeB - 136.0f * s};
    l.reloadCenter   = {w - safeX - 58.0f * s,  h - safeB - 186.0f * s};
    l.interactCenter = {w - safeX - 240.0f * s, h - safeB - 114.0f * s};
    l.shopCenter     = {w - safeX - 240.0f * s, h - safeB - 36.0f * s};

    l.wepPrevRect = {w - safeX - 200.0f * s, h - safeB - 220.0f * s, 46.0f * s, 34.0f * s};
    l.wepNextRect = {w - safeX - 146.0f * s, h - safeB - 220.0f * s, 46.0f * s, 34.0f * s};

    // Driving bottom-right accelerator/brake/nitro/drift + utility cluster
    l.gasRect   = {w - safeX - 84.0f * s,  h - safeB - 124.0f * s, 84.0f * s, 124.0f * s};
    l.brakeRect = {w - safeX - 184.0f * s, h - safeB - 88.0f * s,  86.0f * s, 88.0f * s};
    l.nitroRect = {w - safeX - 84.0f * s,  h - safeB - 188.0f * s, 84.0f * s, 52.0f * s};
    l.driftRect = {w - safeX - 184.0f * s, h - safeB - 152.0f * s, 86.0f * s, 52.0f * s};
    l.exitCarR = 30.0f * s;
    l.hornR = 27.0f * s;
    l.lookBackR = 27.0f * s;
    l.exitCarCenter  = {w - safeX - 238.0f * s, h - safeB - 40.0f * s};
    l.hornCenter     = {w - safeX - 238.0f * s, h - safeB - 114.0f * s};
    l.lookBackCenter = {w - safeX - 304.0f * s, h - safeB - 40.0f * s};

    // Top 7-pill quick toolbar strictly centered in the corridor between minimap (<=160dp) and status card (>=w-252dp)
    const float leftBound = 166.0f;
    const float rightBound = std::max(leftBound + 380.0f, w - 258.0f);
    const float corridorW = rightBound - leftBound;
    const float gap = 6.0f * s;
    const float btnW = clampf((corridorW - gap * 6.0f) / 7.0f, 52.0f, 74.0f * s);
    const float btnH = 29.0f * s;
    const float totalW = btnW * 7.0f + gap * 6.0f;
    const float startX = leftBound + (corridorW - totalW) * 0.5f;
    for (int i = 0; i < 7; ++i) {
        l.topButtons[static_cast<std::size_t>(i)] = {
            startX + static_cast<float>(i) * (btnW + gap), 8.0f, btnW, btnH
        };
    }
    return l;
}

TouchControlResult TouchControls::update(const PlatformInput& input, const GameWorld& world,
                                         int screenW, int screenH, RectF minimapRect,
                                         bool modalOpen, bool simulateMouse, float dt) {
    TouchControlResult res{};
    if (!enabled_) return res;

    const Layout l = computeLayout(screenW, screenH);
    const bool driving = (world.player().drivingVehicleId >= 0);

    struct LivePt { int id; Vec2 pos; };
    std::array<LivePt, 10> live{};
    int liveCount = 0;
    for (int i = 0; i < input.touchCount && liveCount < 10; ++i) {
        if (input.touches[static_cast<std::size_t>(i)].active) {
            live[static_cast<std::size_t>(liveCount++)] = {
                input.touches[static_cast<std::size_t>(i)].id,
                {input.touches[static_cast<std::size_t>(i)].x, input.touches[static_cast<std::size_t>(i)].y}
            };
        }
    }
    if (liveCount == 0 && simulateMouse && input.mouseLeftDown) {
        live[static_cast<std::size_t>(liveCount++)] = {999, {input.mouseX, input.mouseY}};
    }

    for (auto& tp : pointers_) {
        if (!tp.active) continue;
        bool stillDown = false;
        for (int i = 0; i < liveCount; ++i) {
            if (live[static_cast<std::size_t>(i)].id == tp.id) { stillDown = true; break; }
        }
        if (!stillDown) tp.active = false;
    }

    stickVisualOffset_ = {0.0f, 0.0f};
    stickSprinting_ = false;
    fireActiveVis_ = false;
    steerLeftVis_ = false;
    steerRightVis_ = false;
    gasVis_ = false;
    brakeVis_ = false;
    nitroVis_ = false;
    driftVis_ = false;
    lookBackVis_ = false;

    for (int i = 0; i < liveCount; ++i) {
        const auto& lp = live[static_cast<std::size_t>(i)];
        TrackedPointer* tp = findPointer(lp.id);
        const bool justPressed = (tp == nullptr);
        if (justPressed) {
            tp = allocatePointer(lp.id, lp.pos);
            if (!tp) continue;

            if (modalOpen) {
                tp->role = PointerRole::Consumed;
                res.modalTapValid = true;
                res.modalTapPos = lp.pos;
                continue;
            }

            if (minimapRect.contains(lp.pos.x, lp.pos.z)) {
                tp->role = PointerRole::Consumed;
                res.toggleMapPressed = true;
                continue;
            }

            bool hitTop = false;
            for (int b = 0; b < 7; ++b) {
                if (l.topButtons[static_cast<std::size_t>(b)].contains(lp.pos.x, lp.pos.z)) {
                    tp->role = PointerRole::Consumed;
                    if (b == 0) res.toggleMapPressed = true;
                    else if (b == 1) res.toggleViewPressed = true;
                    else if (b == 2) res.togglePhonePressed = true;
                    else if (b == 3) res.toggleCustomPressed = true;
                    else if (b == 4) res.toggleWeatherPressed = true;
                    else if (b == 5) res.advanceTimePressed = true;
                    else if (b == 6) res.cycleQualityPressed = true;
                    hitTop = true;
                    break;
                }
            }
            if (hitTop) continue;

            if (driving) {
                if (l.steerLeftRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::SteerLeft;
                else if (l.steerRightRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::SteerRight;
                else if (l.gasRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::GasPedal;
                else if (l.brakeRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::BrakePedal;
                else if (l.nitroRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::NitroBtn;
                else if (l.driftRect.contains(lp.pos.x, lp.pos.z)) tp->role = PointerRole::DriftBtn;
                else if (hitCircle(lp.pos, l.exitCarCenter, l.exitCarR)) { tp->role = PointerRole::Consumed; res.interactPressed = true; }
                else if (hitCircle(lp.pos, l.hornCenter, l.hornR)) { tp->role = PointerRole::Consumed; res.hornPressed = true; }
                else if (hitCircle(lp.pos, l.lookBackCenter, l.lookBackR)) tp->role = PointerRole::LookBackBtn;
                else tp->role = PointerRole::CameraLook;
            } else {
                if (hitCircle(lp.pos, l.stickCenter, l.stickOuterR * 1.45f)) tp->role = PointerRole::MoveStick;
                else if (hitCircle(lp.pos, l.fireCenter, l.fireR * 1.15f)) tp->role = PointerRole::FireAim;
                else if (hitCircle(lp.pos, l.aimCenter, l.aimR)) { tp->role = PointerRole::Consumed; aimToggled_ = !aimToggled_; }
                else if (hitCircle(lp.pos, l.jumpCenter, l.jumpR)) { tp->role = PointerRole::Consumed; res.jumpPressed = true; }
                else if (hitCircle(lp.pos, l.reloadCenter, l.reloadR)) { tp->role = PointerRole::Consumed; res.reloadPressed = true; }
                else if (hitCircle(lp.pos, l.interactCenter, l.interactR)) { tp->role = PointerRole::Consumed; res.interactPressed = true; }
                else if (hitCircle(lp.pos, l.shopCenter, l.shopR)) { tp->role = PointerRole::Consumed; res.toggleShopPressed = true; }
                else if (l.wepPrevRect.contains(lp.pos.x, lp.pos.z)) { tp->role = PointerRole::Consumed; res.weaponCycleDelta = -1; }
                else if (l.wepNextRect.contains(lp.pos.x, lp.pos.z)) { tp->role = PointerRole::Consumed; res.weaponCycleDelta = +1; }
                else tp->role = PointerRole::CameraLook;
            }
        }

        tp->prevPos = tp->pos;
        tp->pos = lp.pos;
        const Vec2 delta = tp->pos - tp->prevPos;

        switch (tp->role) {
            case PointerRole::MoveStick: {
                Vec2 off = tp->pos - l.stickCenter;
                const float dist = off.length();
                if (dist > l.stickOuterR) off = off * (l.stickOuterR / dist);
                stickVisualOffset_ = off;
                res.moveRight = clampf(off.x / l.stickOuterR, -1.0f, 1.0f);
                res.moveForward = clampf(-off.z / l.stickOuterR, -1.0f, 1.0f);
                if (dist > l.stickOuterR * 0.88f && res.moveForward > 0.55f) {
                    res.sprintOrNitro = true;
                    stickSprinting_ = true;
                }
                break;
            }
            case PointerRole::CameraLook:
                res.lookDeltaX += delta.x;
                res.lookDeltaY += delta.z;
                break;
            case PointerRole::FireAim:
                res.fireHeld = true;
                fireActiveVis_ = true;
                res.lookDeltaX += delta.x * 0.72f;
                res.lookDeltaY += delta.z * 0.72f;
                break;
            case PointerRole::SteerLeft:  steerLeftVis_ = true; break;
            case PointerRole::SteerRight: steerRightVis_ = true; break;
            case PointerRole::GasPedal:   gasVis_ = true; break;
            case PointerRole::BrakePedal: brakeVis_ = true; break;
            case PointerRole::NitroBtn:   nitroVis_ = true; res.sprintOrNitro = true; break;
            case PointerRole::DriftBtn:   driftVis_ = true; res.handbrake = true; break;
            case PointerRole::LookBackBtn: lookBackVis_ = true; res.lookBackHeld = true; break;
            default: break;
        }
    }

    if (driving) {
        const float targetSteer = (steerRightVis_ ? 1.0f : 0.0f) - (steerLeftVis_ ? 1.0f : 0.0f);
        const float steerRate = (std::abs(targetSteer) > 0.05f) ? 4.8f : 7.5f;
        smoothTouchSteer_ += (targetSteer - smoothTouchSteer_) * std::min(1.0f, dt * steerRate);
        res.moveRight = smoothTouchSteer_;

        const float targetGas = (gasVis_ ? 1.0f : 0.0f) - (brakeVis_ ? 1.0f : 0.0f);
        smoothTouchThrottle_ += (targetGas - smoothTouchThrottle_) * std::min(1.0f, dt * 8.0f);
        res.moveForward = smoothTouchThrottle_;
    } else {
        smoothTouchSteer_ = 0.0f;
        smoothTouchThrottle_ = 0.0f;
    }

    res.aimToggled = aimToggled_;
    return res;
}

void TouchControls::render(Vulkan2D& ui, const GameWorld& world, int screenW, int screenH,
                           bool modalOpen, bool firstPerson, bool useKorean) const {
    if (!enabled_ || modalOpen) return;

    const Layout l = computeLayout(screenW, screenH);
    const ColorRGBA accent = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const bool driving = (world.player().drivingVehicleId >= 0);
    const bool isRaining = (world.rainIntensity() > 0.2f);
    const float hr = world.timeOfDayHours();
    const bool isNight = (hr < 6.0f || hr >= 19.5f);

    const char* topKo[7] = {"지도", firstPerson ? "1인칭" : "3인칭", "폰", "커스텀", isRaining ? "비옴" : "날씨", isNight ? "야간" : "시간", "그래픽"};
    const char* topEn[7] = {"MAP", firstPerson ? "1ST" : "3RD", "PHONE", "STYLE", isRaining ? "RAIN" : "WTHR", isNight ? "NIGHT" : "TIME", "GFX"};
    for (int i = 0; i < 7; ++i) {
        const RectF r = l.topButtons[static_cast<std::size_t>(i)];
        const bool lit = (i == 1 && firstPerson) || (i == 4 && isRaining) || (i == 5 && isNight);
        const ColorRGBA rim = (i == 0) ? ColorRGBA{78, 224, 255, 235}
                            : (i == 4 && isRaining) ? ColorRGBA{96, 198, 255, 240}
                            : (i == 5 && isNight)   ? ColorRGBA{255, 198, 86, 235}
                            : (lit ? accent : ColorRGBA{128, 152, 186, 175});
        const ColorRGBA bg = lit
            ? ColorRGBA{static_cast<std::uint8_t>(rim.r / 4), static_cast<std::uint8_t>(rim.g / 4),
                        static_cast<std::uint8_t>(rim.b / 4), 212}
            : ColorRGBA{10, 16, 26, 196};
        ui.drawRoundedRect(r.x, r.y, r.w, r.h, 9.0f, bg);
        ui.drawRoundedRectOutline(r.x, r.y, r.w, r.h, 9.0f, lit ? 1.6f : 1.2f, rim);
        const float fontSz = 13.5f * l.s;
        ui.drawTextCentered(useKorean ? topKo[i] : topEn[i], r.x + r.w * 0.5f,
                            r.y + (r.h - fontSz) * 0.5f - 1.0f, fontSz, {242, 248, 255, 250});
    }

    auto drawPad = [&](RectF r, const char* lbl, bool active, ColorRGBA actCol,
                       float fontScale = 16.5f, float fillBarFrac = -1.0f) {
        const ColorRGBA bg = active
            ? ColorRGBA{actCol.r, actCol.g, actCol.b, 175}
            : ColorRGBA{12, 18, 30, 168};
        const ColorRGBA border = active
            ? ColorRGBA{255, 255, 255, 245}
            : ColorRGBA{actCol.r, actCol.g, actCol.b, 185};
        ui.drawRoundedRect(r.x, r.y, r.w, r.h, 13.0f, bg);
        ui.drawRoundedRectOutline(r.x, r.y, r.w, r.h, 13.0f, active ? 2.4f : 1.8f, border);
        // Sleek bottom accent bar (or live gauge when fillBarFrac >= 0)
        const float barMargin = 10.0f * l.s;
        const float barMaxW = std::max(8.0f, r.w - barMargin * 2.0f);
        const float barH = 4.0f * l.s;
        const float barY = r.y + r.h - barH - 6.0f * l.s;
        if (fillBarFrac >= 0.0f) {
            ui.drawRoundedRect(r.x + barMargin, barY, barMaxW, barH, 2.0f, {26, 36, 52, 210});
            ui.drawRoundedRect(r.x + barMargin, barY, barMaxW * clampf(fillBarFrac, 0.0f, 1.0f),
                               barH, 2.0f, actCol);
        } else {
            ui.drawRoundedRect(r.x + barMargin, barY, barMaxW, barH * 0.85f, 2.0f,
                               {actCol.r, actCol.g, actCol.b, active ? static_cast<std::uint8_t>(240) : static_cast<std::uint8_t>(145)});
        }
        const float fSz = fontScale * l.s;
        ui.drawTextCentered(lbl, r.x + r.w * 0.5f, r.y + (r.h - fSz) * 0.5f - 2.5f * l.s,
                            fSz, {248, 252, 255, 252});
    };

    auto drawCircleBtn = [&](Vec2 c, float rad, const char* lbl, bool active, ColorRGBA actCol,
                             bool highlightPrompt = false, float fontScale = 14.5f) {
        const ColorRGBA bg = active
            ? ColorRGBA{actCol.r, actCol.g, actCol.b, 185}
            : (highlightPrompt
                   ? ColorRGBA{static_cast<std::uint8_t>(actCol.r / 3), static_cast<std::uint8_t>(actCol.g / 3),
                               static_cast<std::uint8_t>(actCol.b / 3), 200}
                   : ColorRGBA{12, 18, 30, 172});
        const ColorRGBA rim = active
            ? ColorRGBA{255, 255, 255, 245}
            : (highlightPrompt ? actCol : ColorRGBA{176, 196, 224, 178});
        ui.drawCircle(c.x, c.z, rad, bg, 28);
        ui.drawCircleRing(c.x, c.z, rad - (highlightPrompt || active ? 2.6f : 1.9f), rad, rim, 28);
        const float fSz = fontScale * l.s;
        ui.drawTextCentered(lbl, c.x, c.z - fSz * 0.52f, fSz, {248, 252, 255, 252});
    };

    if (driving) {
        const Vehicle* pCar = world.playerVehicle();
        const float nitroFrac = pCar ? clampf(pCar->nitroCharge, 0.0f, 1.0f) : 1.0f;
        drawPad(l.steerLeftRect,  "◀", steerLeftVis_, accent, 22.0f);
        drawPad(l.steerRightRect, "▶", steerRightVis_, accent, 22.0f);
        drawPad(l.gasRect,   useKorean ? "가속" : "GAS",       gasVis_,   {58, 218, 116, 255}, 18.0f);
        drawPad(l.brakeRect, useKorean ? "브레이크" : "BRAKE", brakeVis_, {242, 76, 82, 255},  16.0f);
        drawPad(l.nitroRect, useKorean ? "니트로" : "NITRO",   nitroVis_, {64, 212, 255, 255}, 15.0f, nitroFrac);
        drawPad(l.driftRect, useKorean ? "드리프트" : "DRIFT", driftVis_, {255, 198, 56, 255}, 15.0f);
        drawCircleBtn(l.exitCarCenter,  l.exitCarR,  useKorean ? "하차" : "EXIT", false, {248, 174, 72, 255}, false, 14.5f);
        drawCircleBtn(l.hornCenter,     l.hornR,     useKorean ? "경적" : "HORN", false, accent, false, 13.5f);
        drawCircleBtn(l.lookBackCenter, l.lookBackR, useKorean ? "후방" : "REAR", lookBackVis_, accent, false, 13.5f);
    } else {
        // Sleek analog movement stick with cardinal ticks & sprint lock glow
        ui.drawCircle(l.stickCenter.x, l.stickCenter.z, l.stickOuterR, {12, 18, 30, 148}, 32);
        ui.drawCircleRing(l.stickCenter.x, l.stickCenter.z, l.stickOuterR - 2.2f, l.stickOuterR,
                          stickSprinting_ ? ColorRGBA{72, 232, 255, 240} : ColorRGBA{176, 196, 224, 165}, 32);
        const float tickR = l.stickOuterR - 11.0f * l.s;
        const ColorRGBA tickCol{192, 210, 235, 115};
        ui.drawCircle(l.stickCenter.x, l.stickCenter.z - tickR, 2.5f * l.s, tickCol, 8);
        ui.drawCircle(l.stickCenter.x, l.stickCenter.z + tickR, 2.5f * l.s, tickCol, 8);
        ui.drawCircle(l.stickCenter.x - tickR, l.stickCenter.z, 2.5f * l.s, tickCol, 8);
        ui.drawCircle(l.stickCenter.x + tickR, l.stickCenter.z, 2.5f * l.s, tickCol, 8);
        const Vec2 knob = l.stickCenter + stickVisualOffset_;
        ui.drawCircle(knob.x, knob.z, l.stickKnobR,
                      stickSprinting_ ? ColorRGBA{72, 232, 255, 220} : ColorRGBA{accent.r, accent.g, accent.b, 205}, 24);
        ui.drawCircleRing(knob.x, knob.z, l.stickKnobR - 2.0f, l.stickKnobR, {255, 255, 255, 210}, 24);
        if (stickSprinting_) {
            ui.drawTextCentered(useKorean ? "질주" : "SPRINT",
                                l.stickCenter.x, l.stickCenter.z - l.stickOuterR - 20.0f * l.s,
                                13.0f * l.s, {96, 236, 255, 245});
        }

        const bool nearCar = (world.nearestEnterableVehicle() != nullptr);
        const bool nearPoi = (world.nearbyPoi() != nullptr);
        drawCircleBtn(l.fireCenter,     l.fireR,     useKorean ? "사격" : "FIRE", fireActiveVis_, {242, 72, 72, 255}, true, 17.5f);
        drawCircleBtn(l.aimCenter,      l.aimR,      useKorean ? "조준" : "AIM",  aimToggled_, accent, false, 14.5f);
        drawCircleBtn(l.jumpCenter,     l.jumpR,     useKorean ? "점프" : "JUMP", false, accent, false, 14.5f);
        drawCircleBtn(l.reloadCenter,   l.reloadR,   useKorean ? "장전" : "RLD",  false, accent, false, 13.5f);
        drawCircleBtn(l.interactCenter, l.interactR, useKorean ? "탑승" : "CAR",  false, {78, 222, 128, 255}, nearCar, 14.5f);
        drawCircleBtn(l.shopCenter,     l.shopR,     useKorean ? "상점" : "SHOP", false, {248, 204, 68, 255}, nearPoi, 14.5f);
        drawPad(l.wepPrevRect, "◀", false, accent, 15.5f);
        drawPad(l.wepNextRect, "▶", false, accent, 15.5f);
    }
}

} // namespace oc
