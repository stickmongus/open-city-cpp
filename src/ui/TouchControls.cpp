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
    const float s = clampf(h / 760.0f, 0.72f, 1.32f);
    l.s = s;

    l.stickOuterR = 78.0f * s;
    l.stickKnobR = 30.0f * s;
    l.stickCenter = {122.0f * s, h - 122.0f * s};

    const float padW = 94.0f * s;
    const float padH = 74.0f * s;
    l.steerLeftRect  = {26.0f * s, h - padH - 36.0f * s, padW, padH};
    l.steerRightRect = {26.0f * s + padW + 16.0f * s, h - padH - 36.0f * s, padW, padH};

    l.fireR = 44.0f * s;
    l.aimR = 31.0f * s;
    l.jumpR = 31.0f * s;
    l.reloadR = 26.0f * s;
    l.interactR = 32.0f * s;
    l.shopR = 32.0f * s;
    l.fireCenter     = {w - 102.0f * s, h - 122.0f * s};
    l.aimCenter      = {w - 188.0f * s, h - 188.0f * s};
    l.jumpCenter     = {w - 190.0f * s, h - 86.0f * s};
    l.reloadCenter   = {w - 82.0f * s,  h - 216.0f * s};
    l.interactCenter = {w - 266.0f * s, h - 148.0f * s};
    l.shopCenter     = {w - 266.0f * s, h - 72.0f * s};

    l.wepPrevRect = {w - 168.0f * s, h - 252.0f * s, 44.0f * s, 32.0f * s};
    l.wepNextRect = {w - 116.0f * s, h - 252.0f * s, 44.0f * s, 32.0f * s};

    l.gasRect   = {w - 106.0f * s, h - 156.0f * s, 78.0f * s, 122.0f * s};
    l.brakeRect = {w - 202.0f * s, h - 128.0f * s, 80.0f * s, 94.0f * s};
    l.nitroRect = {w - 106.0f * s, h - 224.0f * s, 78.0f * s, 54.0f * s};
    l.driftRect = {w - 202.0f * s, h - 196.0f * s, 80.0f * s, 54.0f * s};
    l.exitCarR = 30.0f * s;
    l.hornR = 26.0f * s;
    l.lookBackR = 26.0f * s;
    l.exitCarCenter  = {w - 266.0f * s, h - 90.0f * s};
    l.hornCenter     = {w - 266.0f * s, h - 166.0f * s};
    l.lookBackCenter = {w - 332.0f * s, h - 90.0f * s};

    const float btnW = 68.0f * s;
    const float btnH = 30.0f * s;
    const float gap = 6.0f * s;
    const float totalW = btnW * 7.0f + gap * 6.0f;
    const float startX = (w - totalW) * 0.5f;
    for (int i = 0; i < 7; ++i) {
        l.topButtons[static_cast<std::size_t>(i)] = {
            startX + static_cast<float>(i) * (btnW + gap), 12.0f * s, btnW, btnH
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

    const char* topKo[7] = {"지도", firstPerson ? "1인칭" : "3인칭", "폰", "커스텀", "날씨", "시간", "품질"};
    const char* topEn[7] = {"MAP", firstPerson ? "1ST" : "3RD", "PHONE", "STYLE", "RAIN", "TIME", "GFX"};
    for (int i = 0; i < 7; ++i) {
        const RectF r = l.topButtons[static_cast<std::size_t>(i)];
        ui.drawRoundedRect(r.x, r.y, r.w, r.h, 8.0f, {12, 18, 28, 185});
        ui.drawRoundedRectOutline(r.x, r.y, r.w, r.h, 8.0f, 1.2f, i == 0 ? ColorRGBA{78, 224, 255, 235} : accent);
        ui.drawTextCentered(useKorean ? topKo[i] : topEn[i], r.x + r.w * 0.5f, r.y + 6.0f * l.s,
                            13.0f * l.s, {236, 244, 255, 245});
    }

    auto drawPad = [&](RectF r, const char* lbl, bool active, ColorRGBA actCol) {
        ui.drawRoundedRect(r.x, r.y, r.w, r.h, 12.0f,
                           active ? ColorRGBA{actCol.r, actCol.g, actCol.b, 165} : ColorRGBA{14, 20, 32, 150});
        ui.drawRoundedRectOutline(r.x, r.y, r.w, r.h, 12.0f, 2.0f, active ? actCol : ColorRGBA{176, 194, 218, 160});
        ui.drawTextCentered(lbl, r.x + r.w * 0.5f, r.y + r.h * 0.5f - 9.0f * l.s,
                            16.0f * l.s, {245, 250, 255, 245});
    };
    auto drawCircleBtn = [&](Vec2 c, float rad, const char* lbl, bool active, ColorRGBA actCol) {
        ui.drawCircle(c.x, c.z, rad,
                      active ? ColorRGBA{actCol.r, actCol.g, actCol.b, 175} : ColorRGBA{14, 20, 32, 155}, 24);
        ui.drawCircleRing(c.x, c.z, rad - 2.0f, rad, active ? actCol : ColorRGBA{186, 202, 224, 170}, 24);
        ui.drawTextCentered(lbl, c.x, c.z - 8.0f * l.s, 14.0f * l.s, {245, 250, 255, 245});
    };

    if (driving) {
        drawPad(l.steerLeftRect,  "◀", steerLeftVis_, accent);
        drawPad(l.steerRightRect, "▶", steerRightVis_, accent);
        drawPad(l.gasRect,   useKorean ? "가속" : "GAS",   gasVis_,   {58, 214, 112, 255});
        drawPad(l.brakeRect, useKorean ? "브레이크" : "BRK", brakeVis_, {242, 76, 82, 255});
        drawPad(l.nitroRect, useKorean ? "니트로" : "NOS", nitroVis_, {64, 212, 255, 255});
        drawPad(l.driftRect, useKorean ? "드리프트" : "DRF", driftVis_, {255, 198, 56, 255});
        drawCircleBtn(l.exitCarCenter,  l.exitCarR,  useKorean ? "하차" : "EXIT", false, accent);
        drawCircleBtn(l.hornCenter,     l.hornR,     useKorean ? "경적" : "HORN", false, accent);
        drawCircleBtn(l.lookBackCenter, l.lookBackR, useKorean ? "후방" : "REAR", lookBackVis_, accent);
    } else {
        ui.drawCircle(l.stickCenter.x, l.stickCenter.z, l.stickOuterR, {14, 20, 32, 130}, 28);
        ui.drawCircleRing(l.stickCenter.x, l.stickCenter.z, l.stickOuterR - 2.0f, l.stickOuterR,
                          stickSprinting_ ? ColorRGBA{72, 228, 255, 220} : ColorRGBA{176, 194, 218, 150}, 28);
        const Vec2 knob = l.stickCenter + stickVisualOffset_;
        ui.drawCircle(knob.x, knob.z, l.stickKnobR, {accent.r, accent.g, accent.b, 190}, 20);

        drawCircleBtn(l.fireCenter,     l.fireR,     useKorean ? "사격" : "FIRE", fireActiveVis_, {242, 72, 72, 255});
        drawCircleBtn(l.aimCenter,      l.aimR,      useKorean ? "조준" : "AIM",  aimToggled_, accent);
        drawCircleBtn(l.jumpCenter,     l.jumpR,     useKorean ? "점프" : "JUMP", false, accent);
        drawCircleBtn(l.reloadCenter,   l.reloadR,   useKorean ? "장전" : "RLD",  false, accent);
        drawCircleBtn(l.interactCenter, l.interactR, useKorean ? "탑승" : "CAR",  false, {82, 218, 128, 255});
        drawCircleBtn(l.shopCenter,     l.shopR,     useKorean ? "상점" : "SHOP", false, {248, 204, 68, 255});
        drawPad(l.wepPrevRect, "◀", false, accent);
        drawPad(l.wepNextRect, "▶", false, accent);
    }
}

} // namespace oc
