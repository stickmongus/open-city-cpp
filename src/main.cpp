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
    int demoNeonPreset = 0;
    bool hideHelpInSmoke = false;
    int wantedOverride = 0;
    bool jumpPanicDemo = false;

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
        } else if (std::strcmp(argv[i], "--neon") == 0 && i + 1 < argc) {
            demoNeonPreset = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-help") == 0) {
            hideHelpInSmoke = true;
        } else if (std::strcmp(argv[i], "--wanted") == 0 && i + 1 < argc) {
            wantedOverride = std::clamp(std::atoi(argv[++i]), 1, 5);
        } else if (std::strcmp(argv[i], "--jump-demo") == 0) {
            jumpPanicDemo = true;
        }
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 860, "Open City C++ v0.5.0 - 3D Open-World Sandbox");
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
    if (driveHypercarDemo) {
        world.player().cash = std::max(world.player().cash, 75000);
        world.buyOrSummonVehicle(oc::VehicleKind::Hypercar);
        if (oc::Vehicle* pcar = world.findVehicleMutable(world.player().activePersonalVehicleId)) {
            if (hasPosOverride) {
                pcar->pos = {posOverrideX, posOverrideZ};
            }
            if (initialYawOverride > -100.0f) {
                pcar->heading = initialYawOverride;
            }
            pcar->neonPreset = demoNeonPreset;
            pcar->customSpoiler = true;
            pcar->engineUpgradeLevel = 2;
            pcar->armorUpgradeLevel = 2;
            pcar->isPlayerDriving = true;
            pcar->isParked = false;
            world.player().drivingVehicleId = pcar->id;
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
    DisableCursor();

    float camYaw = (initialYawOverride > -100.0f) ? initialYawOverride : -oc::kHalfPi;
    float camPitch = (initialPitchOverride > -100.0f) ? initialPitchOverride : -0.10f;
    float smoothedPitch = camPitch;
    float manualCamHoldTimer = (initialYawOverride > -100.0f || initialPitchOverride > -100.0f) ? 5.0f : 0.0f;
    float smoothedFov = 58.0f;
    bool camInitialized = false;
    oc::Vec3 smoothedCamPos{14.5f, 2.4f, 19.5f};

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
                if (cursorLocked) DisableCursor();
                else EnableCursor();
            }
        }
        if (IsKeyPressed(KEY_TAB)) {
            cursorLocked = !cursorLocked;
            if (cursorLocked) DisableCursor();
            else EnableCursor();
        }
        if (!cursorLocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hud.isShopMenuOpen()) {
            cursorLocked = true;
            DisableCursor();
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
        }
        if (IsKeyPressed(KEY_V)) {
            renderer.cycleQuality();
            hud.showGraphicsToast(renderer.qualityNameEn(), renderer.qualityNameKo());
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

        // Full 3D mouse look orbit camera (-64 deg downward to +68 deg upward into skyscrapers/sky)
        if (cursorLocked && !hud.isShopMenuOpen()) {
            const Vector2 md = GetMouseDelta();
            if (std::abs(md.x) > 0.01f || std::abs(md.y) > 0.01f) {
                const float ySign = invertMouseY ? 1.0f : -1.0f;
                camYaw = oc::wrapAngle(camYaw + md.x * 0.0030f);
                camPitch = oc::clampf(camPitch + ySign * md.y * 0.0028f, -1.12f, 1.18f);
                manualCamHoldTimer = 1.6f;
            }
        }
        if (manualCamHoldTimer > 0.0f) {
            manualCamHoldTimer = std::max(0.0f, manualCamHoldTimer - frameDt);
        }

        // Smooth vehicle chase camera auto-alignment when driving and not moving mouse
        if (const oc::Vehicle* car = world.playerVehicle()) {
            if (manualCamHoldTimer <= 0.0f && car->speed() > 2.0f) {
                const oc::Vec2 fwd = oc::forwardFromHeading(car->heading);
                const float desiredYaw = (car->velocity.dot(fwd) < -2.0f)
                                             ? oc::wrapAngle(car->heading + oc::kPi)
                                             : car->heading;
                camYaw = oc::moveTowardsAngle(camYaw, desiredYaw, frameDt * 2.8f);
                camPitch = oc::lerpf(camPitch, -0.16f, frameDt * 3.0f);
            }
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
            input.aimHeld = cursorLocked && IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
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
        const float renderYaw = lookBack ? oc::wrapAngle(camYaw + oc::kPi) : camYaw;
        const float renderPitch = lookBack ? -0.12f : camPitch;
        smoothedPitch = camInitialized
                            ? oc::lerpf(smoothedPitch, renderPitch, std::min(1.0f, frameDt * 22.0f))
                            : renderPitch;
        const oc::Vec3 viewDir = oc::directionFromYawPitch(renderYaw, smoothedPitch).normalized();

        // Helper to compute collision-safe 3rd-person camera boom for any current player/car pose
        auto computeDesiredCamera = [&](float& outTargetFov) -> oc::Vec3 {
            oc::Vec3 pivot{};
            float boomDist = 5.25f;
            outTargetFov = 58.0f;

            if (const oc::Vehicle* car = world.playerVehicle()) {
                pivot = {car->pos.x, 1.48f, car->pos.z};
                boomDist = 7.8f + oc::clampf(car->speed() * 0.065f, 0.0f, 2.8f);
                outTargetFov = 58.0f + oc::clampf(car->speed() * 0.32f, 0.0f, 14.0f) + (car->nitroActive ? 5.0f : 0.0f);
            } else {
                const oc::Vec2 rightOff = oc::rightFromHeading(renderYaw) * (input.aimHeld ? 0.56f : 0.32f);
                const float footElev = std::max(0.0f, world.player().posY - 0.08f) * 0.88f;
                pivot = {
                    world.player().pos.x + rightOff.x,
                    (input.aimHeld ? 1.52f : 1.56f) + footElev,
                    world.player().pos.z + rightOff.z
                };
                boomDist = input.aimHeld ? 3.15f : 5.15f;
                outTargetFov = input.aimHeld ? 45.0f : 58.0f;
            }

            // Slide camera smoothly closer to shoulder when looking steeply UP so camPos stays above street y >= 0.42m
            float actualDist = boomDist;
            if (viewDir.y > 0.01f) {
                const float maxGroundDist = (pivot.y - 0.42f) / viewDir.y;
                actualDist = std::min(actualDist, std::max(0.82f, maxGroundDist));
            }

            // 3D raycast from shoulder pivot to camera to prevent clipping inside buildings
            const oc::Vec3 backDir = viewDir * -1.0f;
            const float hitBld = world.city().raycastBuildings3D(pivot, backDir, actualDist);
            if (hitBld >= 0.0f && hitBld < actualDist) {
                actualDist = std::max(0.75f, hitBld - 0.38f);
            }

            oc::Vec3 pos = pivot - viewDir * actualDist;
            pos.y = std::max(0.38f, pos.y);
            return pos;
        };

        float targetFov = 58.0f;
        const oc::Vec3 desiredCamPos = computeDesiredCamera(targetFov);
        if (!camInitialized) {
            smoothedCamPos = desiredCamPos;
            camInitialized = true;
        } else {
            const float posSmooth = input.aimHeld ? std::min(1.0f, frameDt * 24.0f) : std::min(1.0f, frameDt * 18.0f);
            smoothedCamPos = smoothedCamPos + (desiredCamPos - smoothedCamPos) * posSmooth;
        }
        smoothedFov = oc::lerpf(smoothedFov, targetFov, frameDt * 9.0f);

        input.cameraYaw = camYaw;
        input.aimYaw = renderYaw;
        input.aimPitch = smoothedPitch;
        input.hasAimRay3D = !lookBack;
        input.aimRayOrigin = smoothedCamPos;
        input.aimRayDir = viewDir;

        // Step simulation (stopped during smoke-test final capture if jumpPanicDemo already posed mid-air!)
        if (!(smokeTestMode && jumpPanicDemo)) {
            world.update(frameDt, input);
        }

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

        const oc::Vec3 camTarget = smoothedCamPos + viewDir * 35.0f;

        Camera3D camera{};
        camera.position = {smoothedCamPos.x, smoothedCamPos.y, smoothedCamPos.z};
        camera.target = {camTarget.x, camTarget.y, camTarget.z};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = smoothedFov;
        camera.projection = CAMERA_PERSPECTIVE;

        // Render full 3D pipeline (Shadow Map -> Sky/PBR-lite City -> HDR Bloom + ACES) + crisp 2D HUD
        BeginDrawing();
        renderer.renderFrame(world, camera);
        hud.renderHud(world, renderYaw, cursorLocked);
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
