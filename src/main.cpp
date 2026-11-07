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
    DisableCursor();

    float camYaw = -oc::kHalfPi;
    float camPitch = 0.28f;
    float manualCamHoldTimer = 0.0f;
    float smoothedFov = 58.0f;
    oc::Vec3 smoothedCamPos{14.5f, 4.5f, 22.0f};
    oc::Vec3 smoothedCamTarget{14.5f, 1.5f, 14.5f};

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

        // Mouse look orbit camera
        if (cursorLocked && !hud.isShopMenuOpen()) {
            const Vector2 md = GetMouseDelta();
            if (std::abs(md.x) > 0.01f || std::abs(md.y) > 0.01f) {
                camYaw = oc::wrapAngle(camYaw + md.x * 0.0030f);
                camPitch = oc::clampf(camPitch + md.y * 0.0026f, 0.06f, 1.05f);
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
                camPitch = oc::lerpf(camPitch, 0.24f, frameDt * 3.0f);
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
        input.cameraYaw = camYaw;
        input.aimYaw = camYaw;

        // Step simulation
        world.update(frameDt, input);

        // Process synthesized audio events & engine/drift loops
        audio.playEvents(world.consumeAudioEvents());
        if (const oc::Vehicle* car = world.playerVehicle()) {
            const float ratio = car->speed() / oc::getVehicleSpec(car->kind).maxForwardSpeed;
            audio.updateVehicleLoops(true, ratio, car->isDrifting, car->nitroActive);
        } else {
            audio.updateVehicleLoops(false, 0.0f, false, false);
        }

        // Compute 3rd-person camera (over-the-shoulder aim vs on-foot orbit vs vehicle chase)
        oc::Vec3 desiredTarget{};
        float camDist = 6.2f;
        float targetFov = 58.0f;

        if (const oc::Vehicle* car = world.playerVehicle()) {
            desiredTarget = {car->pos.x, 1.35f, car->pos.z};
            camDist = 8.4f + oc::clampf(car->speed() * 0.065f, 0.0f, 2.8f);
            targetFov = 58.0f + oc::clampf(car->speed() * 0.32f, 0.0f, 14.0f) + (car->nitroActive ? 5.0f : 0.0f);
        } else {
            const oc::Vec2 rightOff = oc::rightFromHeading(renderYaw) * (input.aimHeld ? 0.55f : 0.22f);
            desiredTarget = {
                world.player().pos.x + rightOff.x,
                1.48f,
                world.player().pos.z + rightOff.z
            };
            camDist = input.aimHeld ? 3.35f : 5.45f;
            targetFov = input.aimHeld ? 46.0f : 58.0f;
        }

        const oc::Vec2 camHorizBack = oc::forwardFromHeading(renderYaw) * (-std::cos(camPitch) * camDist);
        oc::Vec2 camXZ{desiredTarget.x + camHorizBack.x, desiredTarget.z + camHorizBack.z};

        // Prevent camera clipping inside skyscraper footprints
        const oc::Vec2 focus2D{desiredTarget.x, desiredTarget.z};
        const oc::Vec2 toCam = camXZ - focus2D;
        const float toCamLen = toCam.length();
        if (toCamLen > 0.2f) {
            const float hitT = world.city().raycastBuildings(focus2D, toCam / toCamLen, toCamLen);
            if (hitT >= 0.0f && hitT < toCamLen) {
                const float safeDist = std::max(1.2f, hitT - 0.45f);
                camXZ = focus2D + (toCam / toCamLen) * safeDist;
            }
        }

        const oc::Vec3 desiredCamPos{
            camXZ.x,
            std::max(0.65f, desiredTarget.y + std::sin(camPitch) * camDist),
            camXZ.z
        };

        const float camSmooth = std::min(1.0f, frameDt * 14.0f);
        smoothedCamPos = smoothedCamPos + (desiredCamPos - smoothedCamPos) * camSmooth;
        smoothedCamTarget = smoothedCamTarget + (desiredTarget - smoothedCamTarget) * camSmooth;
        smoothedFov = oc::lerpf(smoothedFov, targetFov, frameDt * 8.0f);

        Camera3D camera{};
        camera.position = {smoothedCamPos.x, smoothedCamPos.y, smoothedCamPos.z};
        camera.target = {smoothedCamTarget.x, smoothedCamTarget.y, smoothedCamTarget.z};
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
