#include "audio/SynthAudio.hpp"
#include "gameplay/GameWorld.hpp"
#include "platform/PlatformApp.hpp"
#include "render/Renderer3D.hpp"
#include "render/VulkanContext.hpp"
#include "ui/HudRenderer.hpp"
#include "ui/TouchControls.hpp"
#include "ui/Vulkan2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#endif

namespace oc {

namespace {

struct CliOptions {
    std::string screenshotPath;
    bool hasPos = false;
    Vec2 pos{600.0f, 1020.0f};
    bool hasYaw = false;
    float yawDeg = -90.0f;
    bool hasPitch = false;
    float pitchDeg = -8.0f;
    bool hasTime = false;
    float timeHours = 14.0f;
    bool drive = false;
    bool driveHypercar = false;
    bool wetRoad = false;
    bool rain = false;
    bool openMap = false;
    bool mobileUi = false;
    bool noHelp = false;
    bool firstPerson = false;
    int carHeroKind = -1; // >=0 spawns & frames that exact VehicleKind at 3/4 hero angle!
    bool fleetLineup = false;
};

CliOptions parseArgs(int argc, char** argv) {
    CliOptions o{};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) o.screenshotPath = argv[++i];
        else if (a == "--pos" && i + 2 < argc) {
            o.hasPos = true;
            o.pos.x = std::strtof(argv[++i], nullptr);
            o.pos.z = std::strtof(argv[++i], nullptr);
        } else if (a == "--yaw" && i + 1 < argc) {
            o.hasYaw = true;
            o.yawDeg = std::strtof(argv[++i], nullptr);
        } else if (a == "--pitch" && i + 1 < argc) {
            o.hasPitch = true;
            o.pitchDeg = std::strtof(argv[++i], nullptr);
        } else if (a == "--time" && i + 1 < argc) {
            o.hasTime = true;
            o.timeHours = std::strtof(argv[++i], nullptr);
        } else if (a == "--drive") o.drive = true;
        else if (a == "--drive-hypercar") { o.drive = true; o.driveHypercar = true; }
        else if (a == "--wet-road") o.wetRoad = true;
        else if (a == "--rain") { o.rain = true; o.wetRoad = true; }
        else if (a == "--open-map") o.openMap = true;
        else if (a == "--mobile-ui") o.mobileUi = true;
        else if (a == "--no-help") o.noHelp = true;
        else if (a == "--fps") o.firstPerson = true;
        else if (a == "--car-hero" && i + 1 < argc) o.carHeroKind = std::atoi(argv[++i]);
        else if (a == "--fleet-lineup") o.fleetLineup = true;
    }
    return o;
}

int runApplication(PlatformApp& app, const CliOptions& cli) {
    // Wait until surface/window is ready (especially important on Android lifecycle!)
    while (!app.isWindowReady()) {
        if (!app.pollEvents()) return 0;
    }

    VulkanContext vk;
    if (!vk.init(app)) {
        std::fprintf(stderr, "Failed to initialize Native Vulkan 1.0 context.\n");
        return 1;
    }

    GameWorld world(20261107ULL);

    // Apply CLI test/screenshot overrides before streaming & rendering
    if (cli.hasTime) world.setTimeOfDayHours(cli.timeHours);
    if (cli.rain) world.setRainWeather(true);
    else if (cli.wetRoad) {
        world.setWetRoadFactor(0.92f);
    }

    if (cli.hasPos) {
        world.fastTravelTo(cli.pos, "Seoul");
    }

    if (cli.drive) {
        VehicleKind vkKind = cli.driveHypercar ? VehicleKind::Hypercar : VehicleKind::SportsCoupe;
        Vehicle& car = world.spawnCustomVehicle(vkKind, world.player().pos, cli.hasYaw ? (cli.yawDeg * kPi / 180.0f) : -kHalfPi,
                                                cli.driveHypercar ? ColorRGBA{22, 138, 224, 255} : ColorRGBA{218, 36, 48, 255});
        car.stripeStyle = cli.driveHypercar ? 1 : 0;
        world.enterVehicle(car.id);
    }

    // Optional automotive showcase lineup / single-vehicle hero close-up for crisp visual verification
    Vec3 heroCamPos{};
    Vec3 heroCamTarget{};
    bool useHeroCam = false;
    if (cli.carHeroKind >= 0 && cli.carHeroKind < static_cast<int>(VehicleKind::Count)) {
        const Vec2 basePos{588.0f, 940.0f}; // Right on Banpo Boulevard facing north toward Banpo Bridge & Namsan!
        world.fastTravelTo(basePos + Vec2{16.0f, -14.0f}, "Banpo");
        const auto kind = static_cast<VehicleKind>(cli.carHeroKind);
        ColorRGBA paint{206, 28, 38, 255};
        if (kind == VehicleKind::DeliveryVan) paint = {236, 240, 246, 255};
        else if (kind == VehicleKind::PoliceCruiser) paint = {244, 246, 250, 255};
        else if (kind == VehicleKind::Taxi) paint = {246, 126, 24, 255};
        else if (kind == VehicleKind::Hypercar) paint = {24, 118, 214, 255};
        else if (kind == VehicleKind::SUV) paint = {44, 52, 66, 255};
        else if (kind == VehicleKind::Ambulance) paint = {246, 248, 252, 255};
        else if (kind == VehicleKind::FireTruck) paint = {216, 28, 34, 255};

        Vehicle& hero = world.spawnCustomVehicle(kind, basePos, -kHalfPi, paint);
        hero.steerVisual = 0.28f; // Slight dramatic showroom front-wheel turn!
        hero.sirenActive = (kind == VehicleKind::PoliceCruiser || kind == VehicleKind::Ambulance || kind == VehicleKind::FireTruck);
        const float gy = world.city().surfaceHeightAt(basePos);
        heroCamPos    = {basePos.x + 4.35f, gy + 1.50f, basePos.z - 3.95f};
        heroCamTarget = {basePos.x,         gy + 0.80f, basePos.z + 0.15f};
        useHeroCam = true;
    } else if (cli.fleetLineup) {
        const Vec2 basePos{588.0f, 960.0f};
        world.fastTravelTo(basePos + Vec2{18.0f, -24.0f}, "Banpo");
        const VehicleKind lineup[5] = {
            VehicleKind::SportsCoupe, VehicleKind::PoliceCruiser,
            VehicleKind::Taxi, VehicleKind::DeliveryVan, VehicleKind::Hypercar
        };
        const ColorRGBA colors[5] = {
            {208, 28, 38, 255}, {244, 246, 250, 255},
            {246, 126, 24, 255}, {234, 238, 244, 255}, {24, 124, 226, 255}
        };
        for (int i = 0; i < 5; ++i) {
            const Vec2 vp{basePos.x - 10.0f + static_cast<float>(i) * 5.0f, basePos.z};
            Vehicle& c = world.spawnCustomVehicle(lineup[i], vp, -kHalfPi + 0.32f, colors[i]);
            c.steerVisual = 0.22f;
        }
        const float gy = world.city().surfaceHeightAt(basePos);
        heroCamPos    = {basePos.x + 8.5f, gy + 3.2f, basePos.z - 12.8f};
        heroCamTarget = {basePos.x,        gy + 0.9f, basePos.z};
        useHeroCam = true;
    }

    if (!cli.screenshotPath.empty()) {
        world.clearBanner();
    }

    Renderer3D renderer;
    renderer.init(vk, world.city());

    Vulkan2D ui2d;
    const auto fontBytes = app.loadAssetOrSystemFont();
    const bool hasFont = ui2d.init(vk, fontBytes);

    HudRenderer hud;
    hud.init(hasFont);
    const bool useMobileUi = app.isAndroid() || cli.mobileUi;
    hud.setMobileUi(useMobileUi);
    if (cli.noHelp || !cli.screenshotPath.empty()) hud.setShowHelp(false);
    hud.setShowCrosshair(!useHeroCam && (cli.screenshotPath.empty() || cli.firstPerson));
    if (cli.openMap) hud.setFullMapOpen(true, world.player().pos);

    TouchControls touch;
    touch.setEnabled(useMobileUi);

    SynthAudio audio;
    if (cli.screenshotPath.empty()) audio.init();

    float camYaw = cli.hasYaw ? (cli.yawDeg * kPi / 180.0f) : -kHalfPi;
    float camPitch = cli.hasPitch ? (cli.pitchDeg * kPi / 180.0f) : -0.14f;
    bool firstPerson = cli.firstPerson;
    float adsBlend = 0.0f;
    float smoothKeySteer = 0.0f;
    float smoothKeyThrottle = 0.0f;
    int screenshotWarmupFrames = cli.screenshotPath.empty() ? 0 : 3;

    if (!useMobileUi && cli.screenshotPath.empty() && !hud.isAnyModalOpen()) {
        app.setCursorLocked(true);
    }

    while (app.pollEvents()) {
        if (app.consumeSurfaceRecreated()) {
            vk.recreateSurfaceAndSwapchain(app);
        } else if (app.consumeResize()) {
            vk.recreateSwapchain(app);
        }
        if (!app.isWindowReady() || !vk.isReady()) continue;

        const float dt = std::clamp(app.deltaTime(), 0.001f, 0.05f);
        const PlatformInput& pi = app.input();
        const int sw = vk.width();
        const int sh = vk.height();

        hud.updateTimers(dt);

        // Toggle Full-Screen Seoul Map via [M] key or clicking Minimap!
        if (pi.isPressed(KeyCode::M)) {
            hud.toggleFullMap(world.player().pos);
            if (!useMobileUi) app.setCursorLocked(!hud.isAnyModalOpen());
        }
        if (pi.isPressed(KeyCode::Escape) || pi.backPressed) {
            if (hud.isAnyModalOpen()) {
                hud.setFullMapOpen(false);
                hud.setShopMenuOpen(false);
                hud.setPhoneOpen(false);
                hud.setCustomStudioOpen(false);
                if (!useMobileUi) app.setCursorLocked(true);
            } else if (!app.isAndroid()) {
                break;
            }
        }
        if (pi.isPressed(KeyCode::Tab) && !useMobileUi && !hud.isAnyModalOpen()) {
            app.setCursorLocked(!app.isCursorLocked());
        }
        if (pi.isPressed(KeyCode::H)) hud.toggleHelpOverlay();
        if (pi.isPressed(KeyCode::V)) firstPerson = !firstPerson;
        if (pi.isPressed(KeyCode::P)) {
            hud.setPhoneOpen(!hud.isPhoneOpen());
            if (!useMobileUi) app.setCursorLocked(!hud.isAnyModalOpen());
        }
        if (pi.isPressed(KeyCode::K)) {
            hud.setCustomStudioOpen(!hud.isCustomStudioOpen());
            if (!useMobileUi) app.setCursorLocked(!hud.isAnyModalOpen());
        }
        if (pi.isPressed(KeyCode::Y)) {
            world.toggleRainWeather();
            hud.showSystemToast(world.weatherNameEn(), world.weatherNameKo());
        }
        if (pi.isPressed(KeyCode::T)) {
            world.advanceTimeOfDayHours(2.0f);
        }
        if (pi.isPressed(KeyCode::U)) {
            renderer.cycleQuality();
            hud.showGraphicsToast(renderer.qualityNameEn(), renderer.qualityNameKo());
        }
        if (pi.isPressed(KeyCode::E) && !hud.isFullMapOpen()) {
            if (hud.isShopMenuOpen()) {
                hud.setShopMenuOpen(false);
                if (!useMobileUi) app.setCursorLocked(true);
            } else if (world.nearbyPoi() != nullptr) {
                hud.setShopMenuOpen(true);
                if (!useMobileUi) app.setCursorLocked(false);
            }
        }

        // Click minimap with desktop mouse (when unlocked) to open Full-Screen Seoul Map!
        const RectF mmRect = hud.minimapRect(sw, sh);
        if (!useMobileUi && !hud.isAnyModalOpen() && !app.isCursorLocked() &&
            pi.mouseLeftPressed && mmRect.contains(pi.mouseX, pi.mouseY)) {
            hud.setFullMapOpen(true, world.player().pos);
        }

        // Update TouchControls (Android & desktop --mobile-ui preview)
        TouchControlResult tr = touch.update(pi, world, sw, sh, mmRect, hud.isAnyModalOpen(),
                                             cli.mobileUi, dt);
        if (tr.toggleMapPressed) hud.toggleFullMap(world.player().pos);
        if (tr.toggleViewPressed) firstPerson = !firstPerson;
        if (tr.togglePhonePressed) hud.setPhoneOpen(!hud.isPhoneOpen());
        if (tr.toggleCustomPressed) hud.setCustomStudioOpen(!hud.isCustomStudioOpen());
        if (tr.toggleShopPressed && world.nearbyPoi() != nullptr) hud.setShopMenuOpen(!hud.isShopMenuOpen());
        if (tr.toggleWeatherPressed) {
            world.toggleRainWeather();
            hud.showSystemToast(world.weatherNameEn(), world.weatherNameKo());
        }
        if (tr.advanceTimePressed) world.advanceTimeOfDayHours(2.0f);
        if (tr.cycleQualityPressed) {
            renderer.cycleQuality();
            hud.showGraphicsToast(renderer.qualityNameEn(), renderer.qualityNameKo());
        }

        // Process Modal / Full-Screen Seoul Map Interactions
        ModalTapAction modalAct{};
        if (hud.isFullMapOpen()) {
            hud.updateFullMapInput(pi, sw, sh, world, dt, modalAct);
        }
        if (tr.modalTapValid) {
            hud.handleModalTap(world, sw, sh, tr.modalTapPos, modalAct);
        } else if (!useMobileUi && hud.isAnyModalOpen() && !hud.isFullMapOpen() && pi.mouseLeftPressed) {
            hud.handleModalTap(world, sw, sh, {pi.mouseX, pi.mouseY}, modalAct);
        }
        if (modalAct.setWaypoint) {
            world.setWaypoint(modalAct.mapTargetWorld, modalAct.mapTargetLabelKo);
            hud.showSystemToast("GPS Waypoint Set", "GPS 목적지가 설정되었습니다");
        }
        if (modalAct.fastTravel) {
            world.fastTravelTo(modalAct.mapTargetWorld, modalAct.mapTargetLabelKo);
            if (!useMobileUi) app.setCursorLocked(true);
        }

        // Build PlayerInput for GameWorld simulation
        PlayerInput gameIn{};
        bool lookBack = tr.lookBackHeld;

        if (!hud.isAnyModalOpen()) {
            // Smooth camera look (mouse or touch drag)
            if (app.isCursorLocked() && !useMobileUi) {
                camYaw += pi.mouseDeltaX * 0.0026f;
                camPitch = std::clamp(camPitch - pi.mouseDeltaY * 0.0022f, -0.78f, 0.72f);
            } else if (useMobileUi) {
                camYaw += tr.lookDeltaX * 0.0046f;
                camPitch = std::clamp(camPitch - tr.lookDeltaY * 0.0038f, -0.75f, 0.70f);
            }

            const float rawFwd = (pi.isDown(KeyCode::W) || pi.isDown(KeyCode::Up) ? 1.0f : 0.0f) -
                                 (pi.isDown(KeyCode::S) || pi.isDown(KeyCode::Down) ? 1.0f : 0.0f);
            const float rawRt  = (pi.isDown(KeyCode::D) || pi.isDown(KeyCode::Right) ? 1.0f : 0.0f) -
                                 (pi.isDown(KeyCode::A) || pi.isDown(KeyCode::Left) ? 1.0f : 0.0f);

            const bool isDriving = (world.player().drivingVehicleId >= 0);
            if (isDriving) {
                // Smooth progressive steering & throttle filtering for authentic weighted car feel!
                const float steerRate = (std::abs(rawRt) > 0.05f) ? 5.6f : 8.8f;
                smoothKeySteer += (rawRt - smoothKeySteer) * std::min(1.0f, dt * steerRate);
                smoothKeyThrottle += (rawFwd - smoothKeyThrottle) * std::min(1.0f, dt * 9.5f);
                gameIn.moveForward = std::clamp(smoothKeyThrottle + tr.moveForward, -1.0f, 1.0f);
                gameIn.moveRight = std::clamp(smoothKeySteer + tr.moveRight, -1.0f, 1.0f);
            } else {
                smoothKeySteer = 0.0f;
                smoothKeyThrottle = 0.0f;
                gameIn.moveForward = std::clamp(rawFwd + tr.moveForward, -1.0f, 1.0f);
                gameIn.moveRight = std::clamp(rawRt + tr.moveRight, -1.0f, 1.0f);
            }

            gameIn.sprintOrNitro = pi.isDown(KeyCode::Shift) || tr.sprintOrNitro;
            gameIn.handbrake = pi.isDown(KeyCode::Space) || tr.handbrake;
            gameIn.jumpPressed = pi.isPressed(KeyCode::Space) || tr.jumpPressed;
            gameIn.interactPressed = pi.isPressed(KeyCode::F) || tr.interactPressed;
            gameIn.fireHeld = (app.isCursorLocked() && pi.mouseLeftDown) || tr.fireHeld;
            gameIn.aimHeld = (app.isCursorLocked() && pi.mouseRightDown) || tr.aimToggled;
            gameIn.reloadPressed = pi.isPressed(KeyCode::R) || tr.reloadPressed;
            gameIn.hornPressed = pi.isPressed(KeyCode::G) || tr.hornPressed;
            gameIn.abortMissionPressed = pi.isPressed(KeyCode::Backspace);
            if (pi.isDown(KeyCode::Q)) lookBack = true;

            if (tr.weaponCycleDelta != 0) {
                const int nextW = (static_cast<int>(world.player().currentWeapon) + tr.weaponCycleDelta +
                                   static_cast<int>(WeaponId::Count)) % static_cast<int>(WeaponId::Count);
                gameIn.weaponSelectSlot = nextW;
            }
            for (int k = 0; k < 10; ++k) {
                const KeyCode kc = static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + k);
                if (pi.isPressed(kc)) gameIn.weaponSelectSlot = (k == 0) ? 9 : (k - 1);
            }
        } else {
            // Number hotkeys inside Shop / Smartphone / Studio modals
            for (int k = 1; k <= 9; ++k) {
                const KeyCode kc = static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + k);
                if (pi.isPressed(kc)) {
                    if (hud.isShopMenuOpen()) gameIn.shopBuyOption = k;
                    else if (hud.isPhoneOpen()) { gameIn.phoneTab = hud.phoneTab(); gameIn.phoneOption = k; }
                    else if (hud.isCustomStudioOpen()) { gameIn.customStudioTab = hud.customTab(); gameIn.customStudioOption = k; }
                }
            }
        }

        if (modalAct.shopBuyOption > 0) gameIn.shopBuyOption = modalAct.shopBuyOption;
        if (modalAct.phoneOption > 0) { gameIn.phoneTab = modalAct.phoneTab; gameIn.phoneOption = modalAct.phoneOption; }
        if (modalAct.customStudioOption > 0) { gameIn.customStudioTab = modalAct.customStudioTab; gameIn.customStudioOption = modalAct.customStudioOption; }

        adsBlend += ((gameIn.aimHeld ? 1.0f : 0.0f) - adsBlend) * std::min(1.0f, dt * 12.0f);

        // Compute Camera Pose (3rd-person, 1st-person cockpit/FPS, or showcase hero cam)
        CameraState cam{};
        cam.fovyDeg = firstPerson ? (64.0f - adsBlend * 18.0f) : (58.0f - adsBlend * 12.0f);
        if (useHeroCam) {
            cam.position = heroCamPos;
            cam.target = heroCamTarget;
        } else {
            const auto& pl = world.player();
            const Vehicle* pCar = world.playerVehicle();
            if (pCar) {
                // Smooth auto-follow yaw behind driving car when moving
                if (pCar->speed() > 3.5f && std::abs(pi.mouseDeltaX) < 0.5f && std::abs(tr.lookDeltaX) < 0.5f && !cli.hasYaw) {
                    const float diff = wrapAngle(pCar->heading - camYaw);
                    camYaw += diff * std::min(1.0f, dt * 3.6f);
                }
                const float viewYaw = lookBack ? (camYaw + kPi) : camYaw;
                const float gy = world.city().surfaceHeightAt(pCar->pos) + pCar->posY;
                if (firstPerson && !lookBack) {
                    const Vec3 eyeLoc = driverSeatEyeLocal(pCar->kind);
                    const float ch = std::cos(pCar->heading), shH = std::sin(pCar->heading);
                    cam.position = {
                        pCar->pos.x + eyeLoc.x * ch - eyeLoc.z * shH,
                        gy + eyeLoc.y,
                        pCar->pos.z + eyeLoc.x * shH + eyeLoc.z * ch
                    };
                    cam.target = cam.position + Vec3{std::cos(pCar->heading) * 20.0f, -0.8f, std::sin(pCar->heading) * 20.0f};
                } else {
                    const auto& spec = getVehicleSpec(pCar->kind);
                    const float dist = 5.4f + spec.halfLength * 1.15f;
                    const float elev = 2.15f + spec.chassisHeight * 0.42f;
                    const float upLift = std::max(0.0f, camPitch) * dist * 0.92f;
                    cam.target = {pCar->pos.x, gy + spec.chassisHeight * 0.72f + upLift, pCar->pos.z};
                    cam.position = {
                        pCar->pos.x - std::cos(viewYaw) * dist * std::cos(camPitch),
                        std::max(gy + 0.75f, gy + spec.chassisHeight * 0.72f + elev - std::sin(camPitch) * dist * 0.65f),
                        pCar->pos.z - std::sin(viewYaw) * dist * std::cos(camPitch)
                    };
                }
            } else {
                const float gy = world.city().surfaceHeightAt(pl.pos) + pl.posY;
                if (firstPerson) {
                    cam.position = {pl.pos.x, gy + 1.66f, pl.pos.z};
                    cam.target = cam.position + Vec3{std::cos(camYaw) * std::cos(camPitch),
                                                     std::sin(camPitch),
                                                     std::sin(camYaw) * std::cos(camPitch)} * 20.0f;
                } else {
                    const float dist = 4.6f - adsBlend * 1.8f;
                    const Vec2 right = rightFromHeading(camYaw);
                    const float upLift = std::max(0.0f, camPitch) * dist * 0.92f;
                    cam.target = {pl.pos.x + right.x * 0.42f, gy + 1.48f + upLift, pl.pos.z + right.z * 0.42f};
                    cam.position = {
                        pl.pos.x + right.x * 0.42f - std::cos(camYaw) * dist * std::cos(camPitch),
                        std::max(gy + 0.65f, gy + 1.48f + 0.65f - std::sin(camPitch) * dist * 0.75f),
                        pl.pos.z + right.z * 0.42f - std::sin(camYaw) * dist * std::cos(camPitch)
                    };
                }
            }
        }

        // Supply 3D crosshair aim ray into simulation
        gameIn.cameraYaw = camYaw;
        gameIn.aimYaw = camYaw;
        gameIn.aimPitch = camPitch;
        gameIn.firstPerson = firstPerson;
        gameIn.hasAimRay3D = true;
        gameIn.aimRayOrigin = cam.position;
        gameIn.aimRayDir = (cam.target - cam.position).normalized();

        world.update(dt, gameIn);

        // Update real-time miniaudio synthesizer
        audio.playEvents(world.consumeAudioEvents());
        const Vehicle* pCar = world.playerVehicle();
        const float maxSpd = pCar ? getVehicleSpec(pCar->kind).maxForwardSpeed : 40.0f;
        audio.updateVehicleLoops(pCar != nullptr, pCar ? (pCar->speed() / maxSpd) : 0.0f,
                                 pCar && pCar->isDrifting, pCar && pCar->nitroActive);
        audio.updatePoliceSiren(world.nearestPoliceSirenIntensity());
        audio.updateWeatherAmbience(world.rainIntensity() > 0.25f);

        // --- NATIVE VULKAN 1.0+ RENDER FRAME ---
        if (!vk.beginFrame()) {
            vk.recreateSwapchain(app);
            continue;
        }

        // Pass 1 (Shadows) + Pass 2 (HDR+MSAA 3D Seoul) + Begin Pass 3 (Post-Process Bloom+ACES)
        renderer.renderSceneAndBeginPost(vk, world, cam, firstPerson, adsBlend, lookBack);

        // Pass 3 continued: Crisp 2D Vulkan HUD + Interactive Full-Screen Seoul Map + Mobile Touch UI!
        ui2d.beginBatch();
        hud.renderHud(ui2d, vk, world, sw, sh, camYaw, app.isCursorLocked(), firstPerson, adsBlend);
        touch.render(ui2d, world, sw, sh, hud.isAnyModalOpen(), firstPerson, hud.isUsingKorean());
        ui2d.flush(vk);

        vk.endSwapchainPostAndUiPass();

        const bool captureNow = (!cli.screenshotPath.empty() && screenshotWarmupFrames <= 1);
        const std::string* capPtr = captureNow ? &cli.screenshotPath : nullptr;
        if (!vk.endFrameAndPresent(capPtr)) {
            vk.recreateSwapchain(app);
        }

        if (!cli.screenshotPath.empty()) {
            --screenshotWarmupFrames;
            if (screenshotWarmupFrames <= 0) break;
        }
    }

    audio.shutdown();
    renderer.shutdown(vk);
    vk.shutdown();
    return 0;
}

} // namespace

} // namespace oc

#if defined(__ANDROID__)
extern "C" void android_main(struct android_app* state) {
    oc::PlatformApp app;
    if (!app.initAndroid(state)) return;
    oc::CliOptions cli{};
    oc::runApplication(app, cli);
    app.shutdown();
}
#else
int main(int argc, char** argv) {
    const oc::CliOptions cli = oc::parseArgs(argc, argv);
    oc::PlatformApp app;
    if (!app.initDesktop(1440, 864, "Open City Seoul v2.0 — Native Vulkan 3D")) {
        return 1;
    }
    const int rc = oc::runApplication(app, cli);
    app.shutdown();
    return rc;
}
#endif
