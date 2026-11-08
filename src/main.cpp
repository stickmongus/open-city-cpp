#include "audio/SynthAudio.hpp"
#include "gameplay/GameWorld.hpp"
#include "render/Renderer3D.hpp"
#include "ui/HudRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <raylib.h>

int main(int argc, char** argv) {
    bool smokeTestMode = false;
    const char* screenshotPath = nullptr;
    float initialTimeOverride = -1.0f;
    float initialPitchOverride = -999.0f;
    float initialYawOverride = -999.0f;
    bool hasPosOverride = false;
    float posOverrideX = 0.0f;
    float posOverrideZ = 0.0f;
    int openPhoneTabOverride = -1;
    bool openShopOverride = false;
    int cashOverride = -1;
    bool driveHypercarDemo = false;
    int driveKindOverride = -1;
    bool hasSteerOverride = false;
    float demoSteerOverride = 0.0f;
    int demoNeonPreset = 0;
    bool hideHelpInSmoke = false;
    int wantedOverride = 0;
    bool jumpPanicDemo = false;
    bool startFirstPerson = false;
    bool forceAdsDemo = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTestMode = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
            smokeTestMode = true;
        } else if (std::strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            initialTimeOverride = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) {
            initialPitchOverride = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            initialYawOverride = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "--pos") == 0 && i + 2 < argc) {
            hasPosOverride = true;
            posOverrideX = std::strtof(argv[++i], nullptr);
            posOverrideZ = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "--open-phone") == 0 && i + 1 < argc) {
            openPhoneTabOverride = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--open-shop") == 0) {
            openShopOverride = true;
        } else if (std::strcmp(argv[i], "--cash") == 0 && i + 1 < argc) {
            cashOverride = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--drive-hypercar") == 0) {
            driveHypercarDemo = true;
            driveKindOverride = static_cast<int>(oc::VehicleKind::Hypercar);
        } else if (std::strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
            driveKindOverride = std::clamp(std::atoi(argv[++i]), 0, 9);
        } else if (std::strcmp(argv[i], "--steer") == 0 && i + 1 < argc) {
            hasSteerOverride = true;
            demoSteerOverride = oc::clampf(std::strtof(argv[++i], nullptr), -1.0f, 1.0f);
        } else if (std::strcmp(argv[i], "--neon") == 0 && i + 1 < argc) {
            demoNeonPreset = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-help") == 0) {
            hideHelpInSmoke = true;
        } else if (std::strcmp(argv[i], "--wanted") == 0 && i + 1 < argc) {
            wantedOverride = std::clamp(std::atoi(argv[++i]), 1, 5);
        } else if (std::strcmp(argv[i], "--jump-demo") == 0) {
            jumpPanicDemo = true;
        } else if (std::strcmp(argv[i], "--first-person") == 0) {
            startFirstPerson = true;
        } else if (std::strcmp(argv[i], "--ads") == 0) {
            forceAdsDemo = true;
        }
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 860, "Open City C++ v0.6.0 - 3D Open-World Sandbox");
    SetExitKey(KEY_NULL); // Keep ESC free for menu/cursor release instead of instant quit
    SetTargetFPS(60);

    oc::GameWorld world(20261107ULL);
    oc::Renderer3D renderer;
    oc::HudRenderer hud;
    oc::SynthAudio audio;

    renderer.init();
    hud.init();
    audio.init();

    if (cashOverride >= 0) {
        world.player().cash = cashOverride;
    }
    if (hasPosOverride) {
        world.player().pos = {posOverrideX, posOverrideZ};
        world.player().posY = world.surfaceHeightAt(world.player().pos);
    }
    if (driveKindOverride >= 0 || driveHypercarDemo) {
        const auto targetKind = static_cast<oc::VehicleKind>(
            driveKindOverride >= 0 ? driveKindOverride : static_cast<int>(oc::VehicleKind::Hypercar));
        oc::Vehicle* chosenCar = nullptr;
        if (oc::getVehicleSpec(targetKind).dealershipPrice > 0 || targetKind == oc::VehicleKind::SportsCoupe) {
            world.player().cash = std::max(world.player().cash, 75000);
            world.buyOrSummonVehicle(targetKind);
            chosenCar = world.findVehicleMutable(world.player().activePersonalVehicleId);
        } else {
            for (auto& v : world.vehicles()) {
                if (!v.destroyed && v.kind == targetKind) {
                    chosenCar = world.findVehicleMutable(v.id);
                    break;
                }
            }
        }
        if (chosenCar != nullptr) {
            if (hasPosOverride) {
                chosenCar->pos = {posOverrideX, posOverrideZ};
            }
            if (initialYawOverride > -100.0f) {
                chosenCar->heading = initialYawOverride;
            }
            chosenCar->neonPreset = demoNeonPreset;
            if (targetKind == oc::VehicleKind::Hypercar || targetKind == oc::VehicleKind::SportsCoupe) {
                chosenCar->customSpoiler = true;
            }
            chosenCar->engineUpgradeLevel = 2;
            chosenCar->armorUpgradeLevel = 2;
            chosenCar->isPlayerDriving = true;
            chosenCar->isParked = false;
            if (chosenCar->isPolice || targetKind == oc::VehicleKind::PoliceCruiser) {
                chosenCar->sirenActive = true;
            }
            if (hasSteerOverride) {
                chosenCar->steerVisual = demoSteerOverride;
            }
            const oc::Vec2 cfwd = oc::forwardFromHeading(chosenCar->heading);
            chosenCar->velocity = cfwd * (smokeTestMode ? 18.5f : 0.0f);
            world.player().drivingVehicleId = chosenCar->id;
            world.player().pos = chosenCar->pos;
        }
    }
    if (wantedOverride > 0) {
        const float starHeat[6] = {0.0f, 1.2f, 3.5f, 7.0f, 12.2f, 19.0f};
        world.reportCrime(starHeat[wantedOverride], wantedOverride, world.player().pos, true);
        world.player().currentWeapon = oc::WeaponId::AssaultRifle;
        world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)].owned = true;
        world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)].magAmmo = 30;
        // Advance pursuit simulation briefly and stage two police units in clear camera view for showcase
        oc::PlayerInput idleIn{};
        idleIn.firstPerson = startFirstPerson;
        idleIn.aimYaw = (initialYawOverride > -100.0f) ? initialYawOverride : -oc::kHalfPi;
        idleIn.cameraYaw = idleIn.aimYaw;
        for (int s = 0; s < 35; ++s) {
            world.update(1.0f / 60.0f, idleIn);
        }
        const oc::Vec2 anchor = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
        const float lookYaw = (initialYawOverride > -100.0f) ? initialYawOverride : -oc::kHalfPi;
        const oc::Vec2 fwd = oc::forwardFromHeading(lookYaw);
        const oc::Vec2 right = oc::rightFromHeading(lookYaw);
        int cruiserStageId = -1;
        int secondStageId = -1;
        for (const auto& v : world.vehicles()) {
            if (!v.isPolice || v.isPlayerDriving || v.destroyed) continue;
            if (cruiserStageId < 0 && v.kind == oc::VehicleKind::PoliceCruiser) {
                cruiserStageId = v.id;
            } else if (secondStageId < 0 && (wantedOverride < 4 || v.kind == oc::VehicleKind::ArmoredSuv)) {
                secondStageId = v.id;
            }
        }
        if (cruiserStageId >= 0) {
            if (oc::Vehicle* pv = world.findVehicleMutable(cruiserStageId)) {
                pv->pos = anchor + fwd * 14.5f - right * 2.6f;
                pv->heading = oc::wrapAngle(lookYaw + oc::kPi - 0.38f);
                pv->velocity = {0.0f, 0.0f};
                pv->sirenActive = true;
                pv->sirenPhase = 0.18f;
            }
        }
        if (secondStageId >= 0) {
            if (oc::Vehicle* pv = world.findVehicleMutable(secondStageId)) {
                pv->pos = anchor + fwd * 21.0f + right * 4.2f;
                pv->heading = oc::wrapAngle(lookYaw + oc::kPi + 0.42f);
                pv->velocity = {0.0f, 0.0f};
                pv->sirenActive = true;
                pv->sirenPhase = 0.68f;
            }
        }
    }
    if (jumpPanicDemo) {
        world.player().currentWeapon = oc::WeaponId::AssaultRifle;
        world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)].owned = true;
        world.player().weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)].magAmmo = 30;
        const float lookYaw = (initialYawOverride > -100.0f) ? initialYawOverride : 0.0f;
        const oc::Vec2 fwd = oc::forwardFromHeading(lookYaw);
        const oc::Vec2 right = oc::rightFromHeading(lookYaw);
        // Place existing starter SportsCoupe right under/ahead of the jump trajectory
        for (auto& v : world.vehicles()) {
            if (!v.destroyed && v.kind == oc::VehicleKind::SportsCoupe) {
                v.pos = world.player().pos + fwd * 1.55f - right * 0.25f;
                v.heading = oc::wrapAngle(lookYaw + 0.28f);
                v.velocity = {0.0f, 0.0f};
                break;
            }
        }
        // Gather 4 pedestrians onto the sidewalk ahead so the warning shot sends them sprinting away hands-up!
        const oc::Vec2 pedOffsets[4] = {
            fwd * 6.8f + right * 1.6f,
            fwd * 8.2f - right * 1.9f,
            fwd * 9.8f + right * 3.1f,
            fwd * 11.2f + right * 0.5f,
        };
        int placedPeds = 0;
        for (auto& n : world.npcs()) {
            if (n.alive && n.role == oc::NpcRole::Civilian && placedPeds < 4) {
                n.pos = world.player().pos + pedOffsets[placedPeds];
                ++placedPeds;
            }
        }
        // Fire a warning shot in the air to trigger nearby civilians' hands-up PanicFlee sprint, then jump!
        oc::PlayerInput panicIn{};
        panicIn.cameraYaw = lookYaw;
        panicIn.aimYaw = lookYaw;
        panicIn.aimPitch = 0.35f;
        panicIn.fireHeld = true;
        world.update(1.0f / 60.0f, panicIn);

        oc::PlayerInput jumpIn{};
        jumpIn.cameraYaw = lookYaw;
        jumpIn.aimYaw = lookYaw;
        jumpIn.moveForward = 1.0f;
        jumpIn.sprintOrNitro = true;
        jumpIn.jumpPressed = true;
        for (int s = 0; s < 15; ++s) {
            world.update(1.0f / 60.0f, jumpIn);
            jumpIn.jumpPressed = false;
        }
    }
    if (initialTimeOverride >= 0.0f) {
        const float deltaH = initialTimeOverride - world.timeOfDayHours();
        world.advanceTimeOfDayHours(deltaH >= 0.0f ? deltaH : (deltaH + 24.0f));
    }
    if (openPhoneTabOverride >= 0) {
        hud.setPhoneOpen(true);
        hud.setPhoneTab(openPhoneTabOverride);
    }
    if (openShopOverride && world.isPlayerInAnyShopZone()) {
        hud.setShopMenuOpen(true);
    }
    if (hideHelpInSmoke) {
        hud.toggleHelpOverlay();
    }

    bool cursorLocked = true;
    bool invertMouseY = false;
    bool firstPersonMode = startFirstPerson;
    float mouseSensScale = 1.0f;
    int skipMouseFrames = 2;
    DisableCursor();

    float camYaw = (initialYawOverride > -100.0f) ? initialYawOverride : -oc::kHalfPi;
    float camPitch = (initialPitchOverride > -100.0f)
                         ? initialPitchOverride
                         : (startFirstPerson ? -0.04f : -0.10f);
    float cockpitRelYaw = 0.0f;
    float manualCamHoldTimer = (initialYawOverride > -100.0f || initialPitchOverride > -100.0f) ? 5.0f : 0.0f;
    float adsBlend = forceAdsDemo ? 1.0f : 0.0f;
    float smoothedFov = startFirstPerson ? (forceAdsDemo ? 57.0f : 68.0f) : 60.0f;
    bool camInitialized = false;
    oc::Vec3 smoothedAnchor{world.player().pos.x, world.player().posY + 1.54f, world.player().pos.z};
    float smoothedBoomDist = 4.60f;
    float smoothedEyeY = world.player().posY + 1.62f;
    int prevDrivingId = world.player().drivingVehicleId;

    constexpr int kWeaponSlotCount = static_cast<int>(oc::WeaponId::Count);

    while (!WindowShouldClose()) {
        const float frameDt = std::min(GetFrameTime(), 0.05f);

        // Window / cursor / modal hotkeys
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (hud.isShopMenuOpen()) {
                hud.setShopMenuOpen(false);
            } else if (hud.isPhoneOpen()) {
                hud.setPhoneOpen(false);
            } else {
                cursorLocked = !cursorLocked;
                if (cursorLocked) {
                    DisableCursor();
                    skipMouseFrames = 2;
                } else {
                    EnableCursor();
                }
            }
        }
        if (IsKeyPressed(KEY_TAB)) {
            cursorLocked = !cursorLocked;
            if (cursorLocked) {
                DisableCursor();
                skipMouseFrames = 2;
            } else {
                EnableCursor();
            }
        }
        if (!cursorLocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hud.isShopMenuOpen()) {
            cursorLocked = true;
            DisableCursor();
            skipMouseFrames = 2;
        }
        if (IsKeyPressed(KEY_F11)) {
            ToggleFullscreen();
        }
        if (IsKeyPressed(KEY_H)) {
            hud.toggleHelpOverlay();
        }
        if (IsKeyPressed(KEY_L)) {
            hud.toggleLanguage();
        }
        if (IsKeyPressed(KEY_I)) {
            invertMouseY = !invertMouseY;
            hud.showSystemToast(invertMouseY ? "MOUSE Y-AXIS: INVERTED" : "MOUSE Y-AXIS: STANDARD",
                                invertMouseY ? "마우스 상하 반전: 켜짐" : "마우스 상하 반전: 기본");
        }
        // [V] or Middle Mouse Button: Toggle 1st-Person (FPS / Cockpit) <-> 3rd-Person Camera!
        if (IsKeyPressed(KEY_V) || (!hud.isShopMenuOpen() && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))) {
            firstPersonMode = !firstPersonMode;
            camInitialized = false;
            if (const oc::Vehicle* car = world.playerVehicle()) {
                cockpitRelYaw = 0.0f;
                camYaw = car->heading;
                camPitch = firstPersonMode ? -0.04f : -0.14f;
            } else if (firstPersonMode) {
                camPitch = oc::clampf(camPitch, -1.05f, 1.05f);
            }
            hud.showSystemToast(firstPersonMode ? "CAMERA: 1ST PERSON (FPS / COCKPIT)" : "CAMERA: 3RD PERSON",
                                firstPersonMode ? "시점 전환: 1인칭 시점 (FPS · 운전석)" : "시점 전환: 3인칭 숄더 시점");
        }
        // [F3]: Cycle graphics quality preset (High / Medium / Low)
        if (IsKeyPressed(KEY_F3)) {
            renderer.cycleQuality();
            hud.showGraphicsToast(renderer.qualityNameEn(), renderer.qualityNameKo());
        }
        // [ / ]: Live mouse sensitivity tuning (30% .. 250%) to suit any DPI without menu diving
        if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
            const float delta = IsKeyPressed(KEY_RIGHT_BRACKET) ? 0.10f : -0.10f;
            mouseSensScale = oc::clampf(std::round((mouseSensScale + delta) * 10.0f) / 10.0f, 0.30f, 2.50f);
            const int pct = static_cast<int>(std::round(mouseSensScale * 100.0f));
            char sensEn[64];
            char sensKo[80];
            std::snprintf(sensEn, sizeof(sensEn), "MOUSE SENSITIVITY: %d%%  ([ / ])", pct);
            std::snprintf(sensKo, sizeof(sensKo), "마우스 감도: %d%%  ([ / ] 조절)", pct);
            hud.showSystemToast(sensEn, sensKo);
        }
        if (IsKeyPressed(KEY_T)) {
            world.advanceTimeOfDayHours(3.0f);
        }
        if (IsKeyPressed(KEY_P)) {
            hud.setPhoneOpen(!hud.isPhoneOpen());
        }
        if (IsKeyPressed(KEY_B)) {
            if (world.isPlayerInAnyShopZone()) {
                hud.setShopMenuOpen(!hud.isShopMenuOpen());
            } else {
                hud.setShopMenuOpen(false);
            }
        }

        const bool wantPrecisionAim =
            (smokeTestMode && forceAdsDemo) ||
            (cursorLocked && !hud.isShopMenuOpen() && world.player().drivingVehicleId < 0 &&
             IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
        adsBlend = (smokeTestMode && forceAdsDemo)
                       ? 1.0f
                       : oc::lerpf(adsBlend, wantPrecisionAim ? 1.0f : 0.0f, std::min(1.0f, frameDt * 12.0f));

        // 1:1 Synchronous Mouse Look (0ms angular lag, cursor-warp spike guard, automatic ADS slowdown)
        if (cursorLocked && !hud.isShopMenuOpen()) {
            const Vector2 md = GetMouseDelta();
            if (skipMouseFrames > 0) {
                --skipMouseFrames;
            } else {
                const float dx = oc::clampf(md.x, -120.0f, 120.0f);
                const float dy = oc::clampf(md.y, -120.0f, 120.0f);
                if (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f) {
                    const float sens = 0.00175f * mouseSensScale * oc::lerpf(1.0f, 0.58f, adsBlend);
                    const float ySign = invertMouseY ? 1.0f : -1.0f;
                    if (firstPersonMode && world.playerVehicle() != nullptr) {
                        cockpitRelYaw = oc::clampf(cockpitRelYaw + dx * sens, -1.55f, 1.55f);
                        camPitch = oc::clampf(camPitch + ySign * dy * sens, -0.68f, 0.62f);
                        camYaw = oc::wrapAngle(world.playerVehicle()->heading + cockpitRelYaw);
                    } else {
                        camYaw = oc::wrapAngle(camYaw + dx * sens);
                        camPitch = oc::clampf(camPitch + ySign * dy * (sens * 0.94f), -1.12f, 1.18f);
                    }
                    manualCamHoldTimer = 1.6f;
                }
            }
        } else {
            (void)GetMouseDelta();
        }
        if (manualCamHoldTimer > 0.0f) {
            manualCamHoldTimer = std::max(0.0f, manualCamHoldTimer - frameDt);
        }

        // Gather gameplay inputs
        oc::PlayerInput input{};
        if (!hud.isShopMenuOpen()) {
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    input.moveForward += 1.0f;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  input.moveForward -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) input.moveRight += 1.0f;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  input.moveRight -= 1.0f;

            input.sprintOrNitro = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            input.handbrake = IsKeyDown(KEY_SPACE);
            input.jumpPressed = IsKeyPressed(KEY_SPACE);
            input.interactPressed = IsKeyPressed(KEY_F) || (!hud.isPhoneOpen() && IsKeyPressed(KEY_E));
            input.aimHeld = wantPrecisionAim;
            input.fireHeld = cursorLocked && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            input.reloadPressed = IsKeyPressed(KEY_R);
            input.hornPressed = IsKeyPressed(KEY_G);
            input.abortMissionPressed = IsKeyPressed(KEY_BACKSPACE);

            if (hud.isPhoneOpen()) {
                if (IsKeyPressed(KEY_Q)) hud.cyclePhoneTab(-1);
                if (IsKeyPressed(KEY_E)) hud.cyclePhoneTab(1);
                const int maxOpt = (hud.phoneTab() == 3) ? 7 : 6;
                for (int opt = 1; opt <= maxOpt; ++opt) {
                    if (IsKeyPressed(KEY_ONE + (opt - 1))) {
                        input.phoneTab = hud.phoneTab();
                        input.phoneOption = opt;
                    }
                }
            } else {
                for (int k = 0; k < kWeaponSlotCount; ++k) {
                    if (IsKeyPressed(KEY_ONE + k)) {
                        input.weaponSelectSlot = k;
                    }
                }

                const float wheel = GetMouseWheelMove();
                if (std::abs(wheel) > 0.1f && world.player().drivingVehicleId < 0) {
                    int cur = static_cast<int>(world.player().currentWeapon);
                    const int step = (wheel > 0.0f) ? 1 : -1;
                    for (int tries = 0; tries < kWeaponSlotCount; ++tries) {
                        cur = (cur + step + kWeaponSlotCount) % kWeaponSlotCount;
                        if (world.player().weapons[static_cast<std::size_t>(cur)].owned) {
                            input.weaponSelectSlot = cur;
                            break;
                        }
                    }
                }
            }
        } else {
            for (int opt = 1; opt <= 8; ++opt) {
                if (IsKeyPressed(KEY_ONE + (opt - 1))) {
                    input.shopBuyOption = opt;
                }
            }
        }

        const bool lookBack = IsKeyDown(KEY_C);
        const bool activeFirstPerson = firstPersonMode && !lookBack;

        // Pre-step aim ray for exact 1:1 hitscan alignment inside world.update()
        const float preAimYaw =
            (activeFirstPerson && world.playerVehicle() != nullptr)
                ? oc::wrapAngle(world.playerVehicle()->heading + cockpitRelYaw)
                : (lookBack ? oc::wrapAngle(camYaw + oc::kPi) : camYaw);
        const float preAimPitch = lookBack ? -0.12f : camPitch;
        const oc::Vec3 preViewDir = oc::directionFromYawPitch(preAimYaw, preAimPitch).normalized();

        oc::Vec3 preRayOrigin{};
        if (activeFirstPerson) {
            preRayOrigin = {world.player().pos.x, world.player().posY + 1.62f, world.player().pos.z};
        } else {
            const oc::Vec2 rOff = oc::rightFromHeading(preAimYaw) * 0.40f;
            const oc::Vec3 piv{
                smoothedAnchor.x + rOff.x,
                smoothedAnchor.y,
                smoothedAnchor.z + rOff.z
            };
            preRayOrigin = piv - preViewDir * smoothedBoomDist;
            preRayOrigin.y = std::max(0.38f, preRayOrigin.y);
        }

        input.firstPerson = activeFirstPerson;
        input.cameraYaw = camYaw;
        input.aimYaw = preAimYaw;
        input.aimPitch = preAimPitch;
        input.hasAimRay3D = !lookBack;
        input.aimRayOrigin = preRayOrigin;
        input.aimRayDir = preViewDir;

        // Step simulation BEFORE evaluating final camera pose so 1st-person cockpit/eye has ZERO 1-frame position lag!
        if (!(smokeTestMode && jumpPanicDemo)) {
            world.update(frameDt, input);
        }
        if (smokeTestMode && hasSteerOverride) {
            if (oc::Vehicle* dcar = world.findVehicleMutable(world.player().drivingVehicleId)) {
                dcar->steerVisual = demoSteerOverride;
            }
        }

        // Detect vehicle enter/exit transitions so camera anchor resets cleanly without swooping
        if (world.player().drivingVehicleId != prevDrivingId) {
            camInitialized = false;
            cockpitRelYaw = 0.0f;
            if (const oc::Vehicle* entered = world.playerVehicle()) {
                if (firstPersonMode) {
                    camYaw = entered->heading;
                    camPitch = -0.04f;
                }
            }
            prevDrivingId = world.player().drivingVehicleId;
        }

        // Evaluate post-step Camera3D (1st-Person Cockpit / 1st-Person FPS / Stabilized 3rd-Person Orbit)
        float renderYaw = camYaw;
        float renderPitch = camPitch;
        oc::Vec3 viewDir{};
        oc::Vec3 finalCamPos{};
        float targetFov = 60.0f;

        if (activeFirstPerson && world.playerVehicle() != nullptr) {
            // 1st-Person Vehicle Cockpit: locked to left-hand driver seat, chassis turns head naturally
            const oc::Vehicle* car = world.playerVehicle();
            if (manualCamHoldTimer <= 0.0f && car->speed() > 1.5f) {
                cockpitRelYaw = oc::lerpf(cockpitRelYaw, car->steerVisual * 0.14f, std::min(1.0f, frameDt * 3.8f));
                camPitch = oc::lerpf(camPitch, -0.04f, std::min(1.0f, frameDt * 3.5f));
            }
            renderYaw = oc::wrapAngle(car->heading + cockpitRelYaw);
            camYaw = renderYaw;
            renderPitch = oc::clampf(camPitch, -0.68f, 0.62f);
            viewDir = oc::directionFromYawPitch(renderYaw, renderPitch).normalized();

            const oc::Vec3 eyeLocal = oc::driverSeatEyeLocal(car->kind);
            const oc::Vec2 carFwd = oc::forwardFromHeading(car->heading);
            const oc::Vec2 carRight = oc::rightFromHeading(car->heading);
            const float seatY = eyeLocal.y + eyeLocal.x * std::sin(car->bodyPitch) - eyeLocal.z * std::sin(car->bodyRoll);
            finalCamPos = {
                car->pos.x + carFwd.x * eyeLocal.x + carRight.x * eyeLocal.z,
                seatY,
                car->pos.z + carFwd.z * eyeLocal.x + carRight.z * eyeLocal.z
            };
            targetFov = 65.0f + oc::clampf(car->speed() * 0.22f, 0.0f, 9.0f) + (car->nitroActive ? 3.5f : 0.0f);
            if (!camInitialized) {
                smoothedFov = targetFov;
                camInitialized = true;
            }
        } else if (activeFirstPerson) {
            // 1st-Person On-Foot FPS: rock-steady horizon (zero head-bob; curb step gently filtered on Y only)
            const auto& pl = world.player();
            renderYaw = camYaw;
            renderPitch = camPitch;
            viewDir = oc::directionFromYawPitch(renderYaw, renderPitch).normalized();

            const float desiredEyeY = pl.posY + 1.62f;
            if (!camInitialized || !pl.onGround) {
                smoothedEyeY = desiredEyeY;
                if (!camInitialized) {
                    smoothedFov = oc::lerpf(68.0f, 57.0f, adsBlend);
                    camInitialized = true;
                }
            } else {
                smoothedEyeY = oc::lerpf(smoothedEyeY, desiredEyeY, std::min(1.0f, frameDt * 20.0f));
            }
            finalCamPos = {pl.pos.x, smoothedEyeY, pl.pos.z};
            targetFov = oc::lerpf(68.0f, 57.0f, adsBlend);
        } else {
            // Stabilized 3rd-Person Camera: 1:1 zero-lag viewDir + smoothed anchor ONLY + constant shoulder offset
            if (const oc::Vehicle* car = world.playerVehicle()) {
                if (!lookBack && manualCamHoldTimer <= 0.0f && car->speed() > 2.0f) {
                    const oc::Vec2 fwd = oc::forwardFromHeading(car->heading);
                    const float desiredYaw = (car->velocity.dot(fwd) < -2.0f)
                                                 ? oc::wrapAngle(car->heading + oc::kPi)
                                                 : car->heading;
                    camYaw = oc::moveTowardsAngle(camYaw, desiredYaw, frameDt * 2.8f);
                    camPitch = oc::lerpf(camPitch, -0.16f, frameDt * 3.0f);
                }
            }

            renderYaw = lookBack ? oc::wrapAngle(camYaw + oc::kPi) : camYaw;
            renderPitch = lookBack ? -0.12f : camPitch;
            viewDir = oc::directionFromYawPitch(renderYaw, renderPitch).normalized();

            oc::Vec3 rawAnchor{};
            float desiredDist = 4.60f;
            oc::Vec2 shoulderOff{0.0f, 0.0f};

            if (const oc::Vehicle* car = world.playerVehicle()) {
                rawAnchor = {car->pos.x, 1.48f, car->pos.z};
                desiredDist = 7.8f + oc::clampf(car->speed() * 0.065f, 0.0f, 2.6f);
                targetFov = 58.0f + oc::clampf(car->speed() * 0.28f, 0.0f, 12.0f) + (car->nitroActive ? 4.0f : 0.0f);
            } else {
                const auto& pl = world.player();
                const float footElev = std::max(0.0f, pl.posY - 0.08f) * 0.92f;
                rawAnchor = {pl.pos.x, 1.54f + footElev, pl.pos.z};
                // Constant 0.40m right shoulder offset + gentle 0.70m ADS focus (eliminates dolly-zoom vertigo!)
                shoulderOff = oc::rightFromHeading(renderYaw) * 0.40f;
                desiredDist = oc::lerpf(4.60f, 3.90f, adsBlend);
                targetFov = oc::lerpf(60.0f, 54.0f, adsBlend);
            }

            if (!camInitialized) {
                smoothedAnchor = rawAnchor;
                smoothedBoomDist = desiredDist;
                smoothedFov = targetFov;
                camInitialized = true;
            } else {
                const float xzSmooth = std::min(1.0f, frameDt * 32.0f);
                const float ySmooth = std::min(1.0f, frameDt * 15.0f);
                smoothedAnchor.x = oc::lerpf(smoothedAnchor.x, rawAnchor.x, xzSmooth);
                smoothedAnchor.z = oc::lerpf(smoothedAnchor.z, rawAnchor.z, xzSmooth);
                smoothedAnchor.y = oc::lerpf(smoothedAnchor.y, rawAnchor.y, ySmooth);
            }

            const oc::Vec3 pivot{
                smoothedAnchor.x + shoulderOff.x,
                smoothedAnchor.y,
                smoothedAnchor.z + shoulderOff.z
            };

            float clearDist = desiredDist;
            if (viewDir.y > 0.01f) {
                const float maxGroundDist = (pivot.y - 0.42f) / viewDir.y;
                clearDist = std::min(clearDist, std::max(0.82f, maxGroundDist));
            }
            const oc::Vec3 backDir = viewDir * -1.0f;
            const float hitBld = world.city().raycastBuildings3D(pivot, backDir, clearDist);
            if (hitBld >= 0.0f && hitBld < clearDist) {
                clearDist = std::max(0.75f, hitBld - 0.38f);
            }

            // Asymmetric boom smoothing: fast pull-in on wall obstacle, gentle ease-out to prevent rubber-banding
            const float boomRate = (clearDist < smoothedBoomDist) ? 24.0f : 7.0f;
            smoothedBoomDist = oc::lerpf(smoothedBoomDist, clearDist, std::min(1.0f, frameDt * boomRate));

            finalCamPos = pivot - viewDir * smoothedBoomDist;
            finalCamPos.y = std::max(0.38f, finalCamPos.y);
        }

        smoothedFov = oc::lerpf(smoothedFov, targetFov, std::min(1.0f, frameDt * 11.0f));

        // Process synthesized audio events, vehicle engine/drift loops & distance-attenuated police sirens
        audio.playEvents(world.consumeAudioEvents());
        if (const oc::Vehicle* car = world.playerVehicle()) {
            const float ratio = car->speed() / oc::getVehicleSpec(car->kind).maxForwardSpeed;
            audio.updateVehicleLoops(true, ratio, car->isDrifting, car->nitroActive);
        } else {
            audio.updateVehicleLoops(false, 0.0f, false, false);
        }

        const oc::Vec2 listenerPos = (world.playerVehicle() != nullptr)
                                         ? world.playerVehicle()->pos
                                         : world.player().pos;
        float closestSirenDist = 9999.0f;
        for (const auto& v : world.vehicles()) {
            if (v.destroyed || !v.sirenActive) continue;
            closestSirenDist = std::min(closestSirenDist, (v.pos - listenerPos).length());
        }
        const float sirenIntensity = (closestSirenDist < 135.0f)
                                         ? std::pow(oc::clampf(1.0f - closestSirenDist / 135.0f, 0.0f, 1.0f), 0.72f)
                                         : 0.0f;
        audio.updatePoliceSiren(sirenIntensity);

        const oc::Vec3 camTarget = finalCamPos + viewDir * 35.0f;

        Camera3D camera{};
        camera.position = {finalCamPos.x, finalCamPos.y, finalCamPos.z};
        camera.target = {camTarget.x, camTarget.y, camTarget.z};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = smoothedFov;
        camera.projection = CAMERA_PERSPECTIVE;

        // Render full 3D pipeline (Shadow Map -> Sky/PBR-lite City -> 1st-Person Viewmodel/Cockpit -> HDR Bloom + ACES)
        BeginDrawing();
        renderer.renderFrame(world, camera, firstPersonMode, adsBlend, lookBack);
        hud.renderHud(world, renderYaw, cursorLocked, activeFirstPerson, adsBlend);
        EndDrawing();

        if (smokeTestMode) {
            static int framesRendered = 0;
            if (++framesRendered >= 6) {
                if (screenshotPath != nullptr) {
                    TakeScreenshot(screenshotPath);
                }
                break;
            }
        }
    }

    audio.shutdown();
    hud.shutdown();
    renderer.shutdown();
    CloseWindow();
    return 0;
}
