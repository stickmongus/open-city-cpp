#include "audio/SynthAudio.hpp"
#include "gameplay/GameWorld.hpp"
#include "render/Renderer3D.hpp"
#include "ui/HudRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <raylib.h>

int main(int argc, char** argv) {
    bool smokeTestMode = false;
    const char* screenshotPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTestMode = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
            smokeTestMode = true;
        }
    }
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 860, "Open City C++ - 3D Open-World Sandbox");
    SetExitKey(KEY_NULL); // Keep ESC free for menu/cursor release instead of instant quit
    SetTargetFPS(60);

    oc::GameWorld world(20261107ULL);
    oc::Renderer3D renderer;
    oc::HudRenderer hud;
    oc::SynthAudio audio;

    renderer.init();
    hud.init();
    audio.init();

    bool cursorLocked = true;
    bool invertMouseY = false;
    DisableCursor();

    float camYaw = -oc::kHalfPi;
    float camPitch = -0.14f; // View elevation radians: negative looks down, positive looks up into sky!
    float smoothedPitch = camPitch;
    float manualCamHoldTimer = 0.0f;
    float smoothedFov = 58.0f;
    bool camInitialized = false;
    oc::Vec3 smoothedCamPos{14.5f, 2.4f, 19.5f};

    while (!WindowShouldClose()) {
        const float frameDt = std::min(GetFrameTime(), 0.05f);

        // Window / cursor / utility hotkeys
        if (IsKeyPressed(KEY_TAB) || (IsKeyPressed(KEY_ESCAPE) && !hud.isShopMenuOpen())) {
            cursorLocked = !cursorLocked;
            if (cursorLocked) DisableCursor();
            else EnableCursor();
        }
        if (IsKeyPressed(KEY_ESCAPE) && hud.isShopMenuOpen()) {
            hud.setShopMenuOpen(false);
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
        if (IsKeyPressed(KEY_T)) {
            world.advanceTimeOfDayHours(3.0f);
        }
        if (IsKeyPressed(KEY_B)) {
            if (world.isPlayerInWeaponShopZone() || world.isPlayerInGarageZone()) {
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
                camPitch = oc::lerpf(camPitch, -0.18f, frameDt * 3.0f);
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
            input.interactPressed = IsKeyPressed(KEY_F) || IsKeyPressed(KEY_E);
            input.aimHeld = cursorLocked && IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
            input.fireHeld = cursorLocked && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            input.reloadPressed = IsKeyPressed(KEY_R);
            input.hornPressed = IsKeyPressed(KEY_G);
            input.abortMissionPressed = IsKeyPressed(KEY_BACKSPACE);

            for (int k = 0; k < 5; ++k) {
                if (IsKeyPressed(KEY_ONE + k)) {
                    input.weaponSelectSlot = k;
                }
            }

            const float wheel = GetMouseWheelMove();
            if (std::abs(wheel) > 0.1f && world.player().drivingVehicleId < 0) {
                int cur = static_cast<int>(world.player().currentWeapon);
                const int step = (wheel > 0.0f) ? 1 : -1;
                for (int tries = 0; tries < 5; ++tries) {
                    cur = (cur + step + 5) % 5;
                    if (world.player().weapons[static_cast<std::size_t>(cur)].owned) {
                        input.weaponSelectSlot = cur;
                        break;
                    }
                }
            }
        } else {
            for (int opt = 1; opt <= 6; ++opt) {
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
                boomDist = 8.3f + oc::clampf(car->speed() * 0.065f, 0.0f, 2.8f);
                outTargetFov = 58.0f + oc::clampf(car->speed() * 0.32f, 0.0f, 14.0f) + (car->nitroActive ? 5.0f : 0.0f);
            } else {
                const oc::Vec2 rightOff = oc::rightFromHeading(renderYaw) * (input.aimHeld ? 0.56f : 0.32f);
                pivot = {
                    world.player().pos.x + rightOff.x,
                    input.aimHeld ? 1.52f : 1.56f,
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

        // Step simulation (uses exact optical center ray identical to Camera3D below!)
        world.update(frameDt, input);

        // Process synthesized audio events & engine/drift loops
        audio.playEvents(world.consumeAudioEvents());
        if (const oc::Vehicle* car = world.playerVehicle()) {
            const float ratio = car->speed() / oc::getVehicleSpec(car->kind).maxForwardSpeed;
            audio.updateVehicleLoops(true, ratio, car->isDrifting, car->nitroActive);
        } else {
            audio.updateVehicleLoops(false, 0.0f, false, false);
        }

        const oc::Vec3 camTarget = smoothedCamPos + viewDir * 35.0f;

        Camera3D camera{};
        camera.position = {smoothedCamPos.x, smoothedCamPos.y, smoothedCamPos.z};
        camera.target = {camTarget.x, camTarget.y, camTarget.z};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = smoothedFov;
        camera.projection = CAMERA_PERSPECTIVE;

        // Render frame
        BeginDrawing();
        ClearBackground(renderer.computeSkyColor(world));

        BeginMode3D(camera);
        renderer.renderWorld(world, camera);
        EndMode3D();

        hud.renderHud(world, renderYaw, cursorLocked);
        EndDrawing();

        if (smokeTestMode) {
            static int framesRendered = 0;
            if (++framesRendered >= 5) {
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
