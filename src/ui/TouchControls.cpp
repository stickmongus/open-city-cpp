#include "ui/TouchControls.hpp"

#include "core/Math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace oc {

namespace {

Color toTouchColor(ColorRGBA c, unsigned char alpha) {
    return Color{c.r, c.g, c.b, alpha};
}

void drawRoundButton(Vector2 center, float radius, bool active, bool highlight,
                     Color accent, const char* label, float fontSize,
                     const TouchControls::TextDrawFn& drawText,
                     const TouchControls::TextMeasureFn& measureText) {
    const Color fill = active
                           ? Color{accent.r, accent.g, accent.b, 185}
                           : (highlight ? Color{
                                              static_cast<unsigned char>(10 + accent.r / 3),
                                              static_cast<unsigned char>(16 + accent.g / 3),
                                              static_cast<unsigned char>(26 + accent.b / 3),
                                              208}
                                        : Color{12, 18, 28, 178});
    const Color rim = active
                          ? Color{255, 255, 255, 248}
                          : (highlight ? Color{accent.r, accent.g, accent.b, 245}
                                       : Color{accent.r, accent.g, accent.b, 185});

    DrawCircleV(center, radius, fill);
    DrawRing(center, radius - 2.4f, radius, 0.0f, 360.0f, 28, rim);
    if (active) {
        DrawCircleV(center, radius * 0.78f, Color{255, 255, 255, 38});
    }

    if (label && label[0] != '\0') {
        const float tw = measureText(label, fontSize);
        drawText(label, center.x - tw * 0.5f, center.y - fontSize * 0.50f, fontSize,
                 active ? Color{255, 255, 255, 255} : Color{240, 246, 255, 250});
    }
}

void drawPillButton(Rectangle rect, bool active, bool highlight, Color accent,
                    const char* label, float fontSize,
                    const TouchControls::TextDrawFn& drawText,
                    const TouchControls::TextMeasureFn& measureText) {
    const Color fill = active
                           ? Color{accent.r, accent.g, accent.b, 192}
                           : (highlight ? Color{
                                              static_cast<unsigned char>(10 + accent.r / 3),
                                              static_cast<unsigned char>(16 + accent.g / 3),
                                              static_cast<unsigned char>(26 + accent.b / 3),
                                              214}
                                        : Color{12, 18, 28, 182});
    const Color rim = active
                          ? Color{255, 255, 255, 248}
                          : Color{accent.r, accent.g, accent.b, highlight ? static_cast<unsigned char>(245) : static_cast<unsigned char>(180)};
    DrawRectangleRounded(rect, 0.28f, 6, fill);
    DrawRectangleRoundedLinesEx(rect, 0.28f, 6, active ? 2.4f : 1.7f, rim);

    if (label && label[0] != '\0') {
        const float tw = measureText(label, fontSize);
        drawText(label, rect.x + (rect.width - tw) * 0.5f,
                 rect.y + (rect.height - fontSize) * 0.5f,
                 fontSize, active ? Color{255, 255, 255, 255} : Color{240, 246, 255, 250});
    }
}

} // namespace

void TouchControls::resetState() {
    for (auto& p : pointers_) {
        p = TrackedPointer{};
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
}

bool TouchControls::hitCircle(Vector2 p, Vector2 c, float r) {
    const float dx = p.x - c.x;
    const float dy = p.y - c.y;
    return (dx * dx + dy * dy) <= (r * r);
}

bool TouchControls::hitRect(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x <= (r.x + r.width) && p.y >= r.y && p.y <= (r.y + r.height);
}

TouchControls::Layout TouchControls::computeLayout(int screenW, int screenH, bool wantedActive) {
    Layout l{};
    const float sw = static_cast<float>(std::max(320, screenW));
    const float sh = static_cast<float>(std::max(240, screenH));
    // Scale proportionally with viewport height while ensuring comfortable thumb ergonomics on phones & tablets
    const float s = clampf(sh / 820.0f, 0.70f, 1.38f);
    l.s = s;

    // Safe horizontal margin for punch-hole cameras / rounded phone corners
    const float edgeX = std::max(26.0f * s, sw * 0.022f);
    const float edgeY = 22.0f * s;

    // Left virtual analog stick (on-foot)
    l.stickOuterR = 86.0f * s;
    l.stickKnobR = 34.0f * s;
    l.stickCenter = {edgeX + l.stickOuterR + 18.0f * s, sh - edgeY - l.stickOuterR - 14.0f * s};

    // Driving left/right steering pads (bottom-left)
    const float steerW = 94.0f * s;
    const float steerH = 88.0f * s;
    const float steerGap = 16.0f * s;
    const float steerY = sh - edgeY - steerH - 12.0f * s;
    l.steerLeftRect = {edgeX + 10.0f * s, steerY, steerW, steerH};
    l.steerRightRect = {edgeX + 10.0f * s + steerW + steerGap, steerY, steerW, steerH};

    // On-foot right action cluster (ergonomic right-thumb arc)
    l.fireR = 52.0f * s;
    l.fireCenter = {sw - edgeX - 88.0f * s, sh - edgeY - 96.0f * s};

    l.aimR = 35.0f * s;
    l.aimCenter = {l.fireCenter.x - 104.0f * s, l.fireCenter.y - 16.0f * s};

    l.jumpR = 34.0f * s;
    l.jumpCenter = {l.fireCenter.x + 12.0f * s, l.fireCenter.y - 114.0f * s};

    l.reloadR = 29.0f * s;
    l.reloadCenter = {l.fireCenter.x - 84.0f * s, l.fireCenter.y - 102.0f * s};

    l.interactR = 35.0f * s;
    l.interactCenter = {l.fireCenter.x - 178.0f * s, l.fireCenter.y + 8.0f * s};

    l.shopR = 35.0f * s;
    l.shopCenter = {l.fireCenter.x - 178.0f * s, l.fireCenter.y - 84.0f * s};

    // Weapon cycle pills snugly below top-right weapon HUD box (dynamically clears Wanted card!)
    const float wepBtnW = 104.0f * s;
    const float wepBtnH = 34.0f * s;
    const float wepY = wantedActive ? 236.0f : 168.0f;
    l.wepNextRect = {sw - 24.0f - wepBtnW, wepY, wepBtnW, wepBtnH};
    l.wepPrevRect = {l.wepNextRect.x - 8.0f * s - wepBtnW, wepY, wepBtnW, wepBtnH};

    // Driving right pedals & boost/drift/exit cluster
    const float gasW = 84.0f * s;
    const float gasH = 122.0f * s;
    const float brakeW = 78.0f * s;
    const float brakeH = 98.0f * s;
    l.gasRect = {sw - edgeX - gasW - 8.0f * s, sh - edgeY - gasH - 10.0f * s, gasW, gasH};
    l.brakeRect = {l.gasRect.x - brakeW - 14.0f * s, sh - edgeY - brakeH - 10.0f * s, brakeW, brakeH};

    const float auxW = 84.0f * s;
    const float auxH = 44.0f * s;
    l.nitroRect = {l.gasRect.x, l.gasRect.y - auxH - 14.0f * s, auxW, auxH};
    l.driftRect = {l.brakeRect.x - 4.0f * s, l.brakeRect.y - auxH - 14.0f * s, auxW, auxH};

    l.exitCarR = 33.0f * s;
    l.exitCarCenter = {l.brakeRect.x - 54.0f * s, l.brakeRect.y + 46.0f * s};

    l.hornR = 28.0f * s;
    l.hornCenter = {l.nitroRect.x + auxW * 0.5f, l.nitroRect.y - 44.0f * s};

    l.lookBackR = 28.0f * s;
    l.lookBackCenter = {l.driftRect.x + auxW * 0.5f, l.driftRect.y - 44.0f * s};

    // Top-center utility quick bar (6 sleek translucent pills safely between top-left minimap & top-right cash/wanted)
    const float topW = 74.0f * s;
    const float topH = 34.0f * s;
    const float topGap = 8.0f * s;
    const float totalTopW = 6.0f * topW + 5.0f * topGap;
    const float minX = 196.0f * s;
    const float maxX = sw - 292.0f;
    float startX = (sw - totalTopW) * 0.5f;
    if (startX < minX) startX = minX;
    if (startX + totalTopW > maxX && maxX > minX + totalTopW) {
        startX = maxX - totalTopW;
    }
    const float topY = 14.0f * s;
    for (int i = 0; i < 6; ++i) {
        l.topButtons[static_cast<std::size_t>(i)] = {
            startX + static_cast<float>(i) * (topW + topGap), topY, topW, topH
        };
    }

    return l;
}

TouchControls::TrackedPointer* TouchControls::findPointer(int id) {
    for (auto& p : pointers_) {
        if (p.active && p.id == id) return &p;
    }
    return nullptr;
}

TouchControls::TrackedPointer* TouchControls::allocatePointer(int id, Vector2 pos) {
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

TouchControlResult TouchControls::update(const GameWorld& world, int screenW, int screenH,
                                         bool modalOpen, bool studioOpen, bool simulateMouse) {
    TouchControlResult out{};
    if (!enabled_) {
        resetState();
        return out;
    }

    const Layout l = computeLayout(screenW, screenH, world.player().wantedLevel > 0);
    const bool inVehicle = (world.player().drivingVehicleId >= 0);
    if (inVehicle) {
        aimToggled_ = false;
    }

    // Collect currently active touch points from raylib (plus simulated desktop mouse if enabled)
    struct RawTouch {
        int id;
        Vector2 pos;
    };
    std::vector<RawTouch> raw;
    raw.reserve(10);

    const int touchCount = std::min(GetTouchPointCount(), 10);
    for (int i = 0; i < touchCount; ++i) {
        const int tid = GetTouchPointId(i);
        const Vector2 tpos = GetTouchPosition(i);
        if (tpos.x >= 0.0f && tpos.y >= 0.0f) {
            raw.push_back({tid, tpos});
        }
    }
    if (simulateMouse && raw.empty() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        raw.push_back({9999, GetMousePosition()});
    }

    // Mark lifted pointers inactive
    for (auto& p : pointers_) {
        if (!p.active) continue;
        bool stillDown = false;
        for (const auto& rt : raw) {
            if (rt.id == p.id) {
                stillDown = true;
                break;
            }
        }
        if (!stillDown) {
            p.active = false;
            p.id = -1;
            p.role = PointerRole::None;
        }
    }

    const bool nearShop = world.isPlayerInAnyShopZone();
    const PointOfInterest* nearPoi = world.activePoiNearPlayer();
    const bool nearMission = (nearPoi != nullptr && world.mission().kind == ActiveMissionKind::None &&
                              (nearPoi->kind == PoiKind::MissionDelivery ||
                               nearPoi->kind == PoiKind::MissionRace ||
                               nearPoi->kind == PoiKind::MissionGangRaid));
    const bool showInteract = !inVehicle && (world.nearestEnterableVehicle() != nullptr || nearMission);

    // Process new & continuing pointers
    for (const auto& rt : raw) {
        TrackedPointer* ptr = findPointer(rt.id);
        const bool isNew = (ptr == nullptr);
        if (isNew) {
            ptr = allocatePointer(rt.id, rt.pos);
            if (!ptr) continue;
        } else {
            ptr->prevPos = ptr->pos;
            ptr->pos = rt.pos;
        }

        if (isNew) {
            // When any modal is open, route taps to modal handler (and allow 3D orbit drag outside Custom Studio!)
            if (modalOpen) {
                if (studioOpen && rt.pos.x > 490.0f) {
                    ptr->role = PointerRole::CameraLook;
                } else {
                    out.modalTapValid = true;
                    out.modalTapPos = rt.pos;
                    ptr->role = PointerRole::Consumed;
                }
                continue;
            }

            // 1) Top-center utility bar buttons
            bool hitTop = false;
            for (int i = 0; i < 6; ++i) {
                if (hitRect(rt.pos, l.topButtons[static_cast<std::size_t>(i)])) {
                    switch (i) {
                        case 0: out.toggleViewPressed = true; break;
                        case 1: out.togglePhonePressed = true; break;
                        case 2: out.toggleCustomPressed = true; break;
                        case 3: out.toggleWeatherPressed = true; break;
                        case 4: out.advanceTimePressed = true; break;
                        case 5: out.cycleQualityPressed = true; break;
                    }
                    ptr->role = PointerRole::Consumed;
                    hitTop = true;
                    break;
                }
            }
            if (hitTop) continue;

            if (inVehicle) {
                // Driving controls assignment
                if (hitRect(rt.pos, l.steerLeftRect)) {
                    ptr->role = PointerRole::SteerLeft;
                } else if (hitRect(rt.pos, l.steerRightRect)) {
                    ptr->role = PointerRole::SteerRight;
                } else if (hitRect(rt.pos, l.gasRect)) {
                    ptr->role = PointerRole::GasPedal;
                } else if (hitRect(rt.pos, l.brakeRect)) {
                    ptr->role = PointerRole::BrakePedal;
                } else if (hitRect(rt.pos, l.nitroRect)) {
                    ptr->role = PointerRole::NitroBtn;
                } else if (hitRect(rt.pos, l.driftRect)) {
                    ptr->role = PointerRole::DriftBtn;
                } else if (hitCircle(rt.pos, l.exitCarCenter, l.exitCarR * 1.15f)) {
                    out.interactPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (hitCircle(rt.pos, l.hornCenter, l.hornR * 1.15f)) {
                    out.hornPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (hitCircle(rt.pos, l.lookBackCenter, l.lookBackR * 1.15f)) {
                    ptr->role = PointerRole::LookBackBtn;
                } else if (nearShop && hitCircle(rt.pos, l.shopCenter, l.shopR * 1.15f)) {
                    out.toggleShopPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else {
                    ptr->role = PointerRole::CameraLook;
                }
            } else {
                // On-foot controls assignment
                if (hitCircle(rt.pos, l.fireCenter, l.fireR * 1.16f)) {
                    ptr->role = PointerRole::FireAim;
                } else if (hitCircle(rt.pos, l.aimCenter, l.aimR * 1.18f)) {
                    aimToggled_ = !aimToggled_;
                    ptr->role = PointerRole::Consumed;
                } else if (hitCircle(rt.pos, l.jumpCenter, l.jumpR * 1.18f)) {
                    out.jumpPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (hitCircle(rt.pos, l.reloadCenter, l.reloadR * 1.20f)) {
                    out.reloadPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (showInteract && hitCircle(rt.pos, l.interactCenter, l.interactR * 1.18f)) {
                    out.interactPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (nearShop && hitCircle(rt.pos, l.shopCenter, l.shopR * 1.18f)) {
                    out.toggleShopPressed = true;
                    ptr->role = PointerRole::Consumed;
                } else if (hitRect(rt.pos, l.wepPrevRect)) {
                    out.weaponCycleDelta = -1;
                    ptr->role = PointerRole::Consumed;
                } else if (hitRect(rt.pos, l.wepNextRect)) {
                    out.weaponCycleDelta = 1;
                    ptr->role = PointerRole::Consumed;
                } else if (hitCircle(rt.pos, l.stickCenter, l.stickOuterR * 1.38f) ||
                           (rt.pos.x < static_cast<float>(screenW) * 0.36f &&
                            rt.pos.y > static_cast<float>(screenH) * 0.44f)) {
                    ptr->role = PointerRole::MoveStick;
                } else {
                    ptr->role = PointerRole::CameraLook;
                }
            }
        }
    }

    // Aggregate continuous inputs & visual feedback flags
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

    for (const auto& p : pointers_) {
        if (!p.active) continue;
        switch (p.role) {
            case PointerRole::MoveStick: {
                if (!inVehicle && !modalOpen) {
                    const float dx = p.pos.x - l.stickCenter.x;
                    const float dy = p.pos.y - l.stickCenter.y;
                    const float dist = std::hypot(dx, dy);
                    const float maxR = l.stickOuterR;
                    const float clamped = std::min(dist, maxR);
                    const float nx = (dist > 0.001f) ? (dx / dist) : 0.0f;
                    const float ny = (dist > 0.001f) ? (dy / dist) : 0.0f;
                    stickVisualOffset_ = {nx * clamped, ny * clamped};

                    const float mag = clamped / maxR;
                    if (mag > 0.12f) {
                        const float scaled = (mag - 0.12f) / 0.88f;
                        out.moveRight = clampf(nx * scaled, -1.0f, 1.0f);
                        out.moveForward = clampf(-ny * scaled, -1.0f, 1.0f);
                        if (out.moveForward > 0.82f && mag > 0.86f) {
                            out.sprintOrNitro = true;
                            stickSprinting_ = true;
                        }
                    }
                }
                break;
            }
            case PointerRole::FireAim: {
                if (!inVehicle && !modalOpen) {
                    out.fireHeld = true;
                    fireActiveVis_ = true;
                    // Drag-to-aim while holding Fire button!
                    out.lookDeltaX += (p.pos.x - p.prevPos.x);
                    out.lookDeltaY += (p.pos.y - p.prevPos.y);
                }
                break;
            }
            case PointerRole::CameraLook: {
                out.lookDeltaX += (p.pos.x - p.prevPos.x);
                out.lookDeltaY += (p.pos.y - p.prevPos.y);
                break;
            }
            case PointerRole::SteerLeft: {
                if (inVehicle && !modalOpen) {
                    out.moveRight -= 1.0f;
                    steerLeftVis_ = true;
                }
                break;
            }
            case PointerRole::SteerRight: {
                if (inVehicle && !modalOpen) {
                    out.moveRight += 1.0f;
                    steerRightVis_ = true;
                }
                break;
            }
            case PointerRole::GasPedal: {
                if (inVehicle && !modalOpen) {
                    out.moveForward += 1.0f;
                    gasVis_ = true;
                }
                break;
            }
            case PointerRole::BrakePedal: {
                if (inVehicle && !modalOpen) {
                    out.moveForward -= 1.0f;
                    brakeVis_ = true;
                }
                break;
            }
            case PointerRole::NitroBtn: {
                if (inVehicle && !modalOpen) {
                    out.sprintOrNitro = true;
                    out.moveForward = std::max(out.moveForward, 1.0f);
                    nitroVis_ = true;
                }
                break;
            }
            case PointerRole::DriftBtn: {
                if (inVehicle && !modalOpen) {
                    out.handbrake = true;
                    driftVis_ = true;
                }
                break;
            }
            case PointerRole::LookBackBtn: {
                if (inVehicle && !modalOpen) {
                    out.lookBackHeld = true;
                    lookBackVis_ = true;
                }
                break;
            }
            case PointerRole::None:
            case PointerRole::Consumed:
                break;
        }
    }

    out.moveForward = clampf(out.moveForward, -1.0f, 1.0f);
    out.moveRight = clampf(out.moveRight, -1.0f, 1.0f);
    out.aimToggled = aimToggled_ && !inVehicle && !modalOpen;
    return out;
}

void TouchControls::render(const GameWorld& world, int screenW, int screenH,
                           bool modalOpen, bool firstPerson, bool useKorean,
                           const TextDrawFn& drawText, const TextMeasureFn& measureText) const {
    if (!enabled_ || modalOpen) return;

    const Layout l = computeLayout(screenW, screenH, world.player().wantedLevel > 0);
    const float s = l.s;
    const bool inVehicle = (world.player().drivingVehicleId >= 0);
    const ColorRGBA themeRgba = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    const Color themeCol = toTouchColor(themeRgba, 255);

    auto txt = [&](const char* en, const char* ko) -> const char* {
        return useKorean ? ko : en;
    };

    // 1) Top-center sleek utility toolbar
    const char* topLabels[6] = {
        firstPerson ? txt("1ST", "1인칭") : txt("3RD", "3인칭"),
        txt("PHONE", "폰"),
        txt("CUSTOM", "커스텀"),
        world.isRainWeather() ? txt("RAIN", "우천") : txt("CLEAR", "맑음"),
        txt("TIME", "시간"),
        txt("GFX", "화질")
    };
    const bool topHighlights[6] = {
        firstPerson,
        false,
        false,
        world.isRainWeather(),
        false,
        false
    };
    for (int i = 0; i < 6; ++i) {
        drawPillButton(l.topButtons[static_cast<std::size_t>(i)], false, topHighlights[i],
                       themeCol, topLabels[i], 14.5f * s, drawText, measureText);
    }

    const bool nearShop = world.isPlayerInAnyShopZone();

    if (!inVehicle) {
        // 2) Left 360-degree Analog Virtual Joystick (On-Foot)
        const Color stickRingCol = stickSprinting_ ? Color{86, 232, 255, 230} : toTouchColor(themeRgba, 165);
        DrawCircleV(l.stickCenter, l.stickOuterR, Color{10, 15, 24, 118});
        DrawRing(l.stickCenter, l.stickOuterR - 2.6f, l.stickOuterR, 0.0f, 360.0f, 36, stickRingCol);
        // Subtle 4-way cardinal tick marks inside joystick base
        const float tickIn = l.stickOuterR * 0.72f;
        const float tickOut = l.stickOuterR * 0.88f;
        const Color tickCol = Color{210, 224, 240, 90};
        DrawLineEx({l.stickCenter.x, l.stickCenter.y - tickOut}, {l.stickCenter.x, l.stickCenter.y - tickIn}, 2.2f, tickCol);
        DrawLineEx({l.stickCenter.x, l.stickCenter.y + tickIn}, {l.stickCenter.x, l.stickCenter.y + tickOut}, 2.2f, tickCol);
        DrawLineEx({l.stickCenter.x - tickOut, l.stickCenter.y}, {l.stickCenter.x - tickIn, l.stickCenter.y}, 2.2f, tickCol);
        DrawLineEx({l.stickCenter.x + tickIn, l.stickCenter.y}, {l.stickCenter.x + tickOut, l.stickCenter.y}, 2.2f, tickCol);

        // Sprint top arc hint
        const char* sprintLbl = txt("SPRINT", "질주");
        const float slw = measureText(sprintLbl, 12.0f * s);
        drawText(sprintLbl, l.stickCenter.x - slw * 0.5f, l.stickCenter.y - l.stickOuterR - 18.0f * s,
                 12.0f * s, stickSprinting_ ? Color{102, 242, 255, 255} : Color{186, 204, 222, 168});

        const Vector2 knobPos{l.stickCenter.x + stickVisualOffset_.x, l.stickCenter.y + stickVisualOffset_.y};
        DrawCircleV(knobPos, l.stickKnobR, stickSprinting_ ? Color{72, 222, 255, 195} : Color{226, 238, 252, 168});
        DrawRing(knobPos, l.stickKnobR - 2.4f, l.stickKnobR, 0.0f, 360.0f, 24, Color{255, 255, 255, 232});

        // 3) Right-side Combat & Action Cluster
        drawRoundButton(l.fireCenter, l.fireR, fireActiveVis_, true, Color{242, 74, 88, 255},
                        txt("FIRE", "발사"), 19.5f * s, drawText, measureText);
        drawRoundButton(l.aimCenter, l.aimR, aimToggled_, aimToggled_, Color{86, 226, 255, 255},
                        txt("AIM", "조준"), 15.5f * s, drawText, measureText);
        drawRoundButton(l.jumpCenter, l.jumpR, false, false, Color{112, 238, 168, 255},
                        txt("JUMP", "점프"), 15.5f * s, drawText, measureText);
        drawRoundButton(l.reloadCenter, l.reloadR, false, false, Color{246, 204, 88, 255},
                        txt("RLD", "장전"), 14.0f * s, drawText, measureText);

        // Context-sensitive Enter Vehicle / Start Mission button
        const bool nearCar = (world.nearestEnterableVehicle() != nullptr);
        const PointOfInterest* nearPoi = world.activePoiNearPlayer();
        const bool nearMission = (nearPoi != nullptr && world.mission().kind == ActiveMissionKind::None &&
                                  (nearPoi->kind == PoiKind::MissionDelivery ||
                                   nearPoi->kind == PoiKind::MissionRace ||
                                   nearPoi->kind == PoiKind::MissionGangRaid));
        if (nearCar || nearMission) {
            const Color ctxCol = nearCar ? Color{92, 236, 162, 255} : Color{255, 210, 74, 255};
            const char* ctxLbl = nearCar ? txt("DRIVE", "탑승") : txt("JOB", "미션");
            drawRoundButton(l.interactCenter, l.interactR, false, true, ctxCol,
                            ctxLbl, 16.0f * s, drawText, measureText);
        }
        if (nearShop) {
            drawRoundButton(l.shopCenter, l.shopR, false, true, Color{255, 206, 74, 255},
                            txt("SHOP", "매장"), 16.0f * s, drawText, measureText);
        }

        // Compact weapon cycle pills below top-right weapon card
        drawPillButton(l.wepPrevRect, false, false, themeCol, txt("< WEP", "◀ 무기"), 14.0f * s, drawText, measureText);
        drawPillButton(l.wepNextRect, false, false, themeCol, txt("WEP >", "무기 ▶"), 14.0f * s, drawText, measureText);
    } else {
        // 4) Driving Layout: Wide Left/Right Steering Pads + Gas/Brake/Nitro/Drift/Exit/Horn/Rear
        drawPillButton(l.steerLeftRect, steerLeftVis_, false, themeCol, "◀", 30.0f * s, drawText, measureText);
        drawPillButton(l.steerRightRect, steerRightVis_, false, themeCol, "▶", 30.0f * s, drawText, measureText);

        drawPillButton(l.gasRect, gasVis_, true, Color{72, 234, 142, 255},
                       txt("GAS", "가속"), 19.0f * s, drawText, measureText);
        drawPillButton(l.brakeRect, brakeVis_, false, Color{246, 98, 86, 255},
                       txt("BRK", "브레이크"), 16.0f * s, drawText, measureText);
        drawPillButton(l.nitroRect, nitroVis_, true, Color{74, 212, 255, 255},
                       txt("NITRO", "니트로"), 15.0f * s, drawText, measureText);
        drawPillButton(l.driftRect, driftVis_, false, Color{216, 128, 255, 255},
                       txt("DRIFT", "드리프트"), 14.5f * s, drawText, measureText);

        drawRoundButton(l.exitCarCenter, l.exitCarR, false, true, Color{255, 198, 76, 255},
                        txt("EXIT", "하차"), 15.5f * s, drawText, measureText);
        drawRoundButton(l.hornCenter, l.hornR, false, false, Color{138, 198, 255, 255},
                        txt("HORN", "경적"), 13.5f * s, drawText, measureText);
        drawRoundButton(l.lookBackCenter, l.lookBackR, lookBackVis_, false, Color{186, 206, 230, 255},
                        txt("REAR", "후방"), 13.5f * s, drawText, measureText);

        if (nearShop) {
            drawRoundButton(l.shopCenter, l.shopR, false, true, Color{255, 206, 74, 255},
                            txt("SHOP", "매장"), 16.0f * s, drawText, measureText);
        }
    }
}

} // namespace oc
