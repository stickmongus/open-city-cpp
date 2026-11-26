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
    bool openGraphics = false;
    int renderDistM = 0;
    bool mobileUi = false;
    bool noHelp = false;
    bool firstPerson = false;
    int carHeroKind = -1; // >=0 spawns & frames that exact VehicleKind at 3/4 hero angle!
    bool fleetLineup = false;
    int weaponId = -1;    // >=0 unlocks & equips that exact WeaponId
    bool forceAim = false;
    bool forceSiren = false;
    bool forceLaser = false;
    int windowW = 1440;
    int windowH = 864;
    int wantedLevel = 0;
};

#if !defined(__ANDROID__)
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
        else if (a == "--open-graphics") o.openGraphics = true;
        else if (a == "--render-dist" && i + 1 < argc) o.renderDistM = std::atoi(argv[++i]);
        else if (a == "--mobile-ui") o.mobileUi = true;
        else if (a == "--no-help") o.noHelp = true;
        else if (a == "--fps") o.firstPerson = true;
        else if (a == "--car-hero" && i + 1 < argc) o.carHeroKind = std::atoi(argv[++i]);
        else if (a == "--fleet-lineup") o.fleetLineup = true;
        else if (a == "--weapon" && i + 1 < argc) o.weaponId = std::atoi(argv[++i]);
        else if (a == "--aim") o.forceAim = true;
        else if (a == "--siren") o.forceSiren = true;
        else if (a == "--laser") o.forceLaser = true;
        else if (a == "--size" && i + 2 < argc) {
            o.windowW = std::clamp(std::atoi(argv[++i]), 640, 3840);
            o.windowH = std::clamp(std::atoi(argv[++i]), 400, 2160);
        } else if (a == "--wanted" && i + 1 < argc) {
            o.wantedLevel = std::clamp(std::atoi(argv[++i]), 0, 5);
        }
    }
    return o;
}
#endif

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

    // Load Real-World OpenStreetMap + Overture + TUM Seoul GIS + 15m DTM + 1:1 Synced Eclipse SUMO OCRM v1 (v6.7.0)
    const auto ocmapBytes = app.loadAssetBytes("maps/seoul_real.ocmap");
    if (!ocmapBytes.empty()) {
        world.loadRealSeoulMap(ocmapBytes.data(), ocmapBytes.size());
    }
    const auto ocrmBytes = app.loadAssetBytes("maps/seoul_roads_hd.ocrm");
    if (!ocrmBytes.empty()) {
        world.loadOcrmRoadMesh(ocrmBytes.data(), ocrmBytes.size());
    }

    // Apply CLI test/screenshot overrides before streaming & rendering
    if (cli.hasTime) world.setTimeOfDayHours(cli.timeHours);
    if (cli.rain) world.setRainWeather(true);
    else if (cli.wetRoad) {
        world.setWetRoadFactor(0.92f);
    }

    if (cli.hasPos) {
        world.fastTravelTo(cli.pos, "Seoul");
    }

    if (cli.weaponId >= 0 && cli.weaponId < static_cast<int>(WeaponId::Count)) {
        const auto wid = static_cast<WeaponId>(cli.weaponId);
        auto& slot = world.player().weapons[static_cast<std::size_t>(wid)];
        slot.owned = true;
        if (cli.forceLaser) slot.laserSightColor = 1; // Tactical Red Laser
        const auto& spec = getWeaponSpec(wid);
        if (spec.magazineSize > 0) {
            slot.magAmmo = spec.magazineSize;
            slot.reserveAmmo = spec.magazineSize * 4;
        }
        world.player().currentWeapon = wid;
        world.player().weaponEquipAnim = 0.0f;
    }
    if (cli.forceAim) {
        world.player().weaponAimBlend = 1.0f;
        world.player().aiming = true;
    }

    const float defaultYaw = world.city().hasRealSeoulMap() ? world.city().spawnYaw() : -kHalfPi;

    if (cli.drive) {
        VehicleKind vkKind = cli.forceSiren ? VehicleKind::PoliceCruiser
                           : (cli.driveHypercar ? VehicleKind::Hypercar : VehicleKind::SportsCoupe);
        ColorRGBA pCol = cli.forceSiren ? ColorRGBA{244, 246, 250, 255}
                       : (cli.driveHypercar ? ColorRGBA{22, 138, 224, 255} : ColorRGBA{218, 36, 48, 255});
        Vehicle& car = world.spawnCustomVehicle(vkKind, world.player().pos, cli.hasYaw ? (cli.yawDeg * kPi / 180.0f) : defaultYaw, pCol, world.player().posY);
        car.stripeStyle = (cli.driveHypercar && !cli.forceSiren) ? 1 : 0;
        if (cli.forceSiren) {
            car.isPolice = true;
            car.sirenActive = true;
            car.sirenPhase = 0.08f;
        }
        world.enterVehicle(car.id);
    }
    if (cli.wantedLevel > 0) {
        world.player().wantedLevel = cli.wantedLevel;
        world.player().policeLineOfSight = true;
        world.player().policeLastKnownPos = world.player().pos;
    }

    // Optional automotive showcase lineup / single-vehicle hero close-up for crisp visual verification
    Vec3 heroCamPos{};
    Vec3 heroCamTarget{};
    bool useHeroCam = false;
    if (cli.carHeroKind >= 0 && cli.carHeroKind < static_cast<int>(VehicleKind::Count)) {
        const Vec2 basePos = world.city().hasRealSeoulMap() ? world.city().spawnPos() : Vec2{588.0f, 940.0f};
        const float heroYaw = world.city().hasRealSeoulMap() ? world.city().spawnYaw() : -kHalfPi;
        const Vec2 hFwd{std::cos(heroYaw), std::sin(heroYaw)};
        const Vec2 hRt{-hFwd.z, hFwd.x};
        world.fastTravelTo(basePos + hRt * 14.0f + hFwd * 12.0f, "Seoul");
        const auto kind = static_cast<VehicleKind>(cli.carHeroKind);
        ColorRGBA paint{206, 28, 38, 255};
        if (kind == VehicleKind::DeliveryVan) paint = {236, 240, 246, 255};
        else if (kind == VehicleKind::PoliceCruiser) paint = {244, 246, 250, 255};
        else if (kind == VehicleKind::Taxi) paint = {246, 126, 24, 255};
        else if (kind == VehicleKind::Hypercar) paint = {24, 118, 214, 255};
        else if (kind == VehicleKind::SUV) paint = {44, 52, 66, 255};
        else if (kind == VehicleKind::Ambulance) paint = {246, 248, 252, 255};
        else if (kind == VehicleKind::FireTruck) paint = {216, 28, 34, 255};
        else if (kind == VehicleKind::SeoulBus) paint = {26, 86, 178, 255}; // Iconic Seoul Blue Trunk Bus!

        Vehicle& hero = world.spawnCustomVehicle(kind, basePos, heroYaw, paint);
        hero.steerVisual = 0.28f; // Slight dramatic showroom front-wheel turn!
        hero.hasNpcDriver = true; // Show live 3D seated driver behind the steering wheel!
        hero.sirenActive = cli.forceSiren || (kind == VehicleKind::PoliceCruiser || kind == VehicleKind::Ambulance || kind == VehicleKind::FireTruck);
        const float gy = world.city().surfaceHeightAt(basePos);
        if (kind == VehicleKind::SeoulBus) {
            const Vec2 cp = basePos + hFwd * 6.8f + hRt * 6.9f;
            heroCamPos    = {cp.x, gy + 2.35f, cp.z};
            heroCamTarget = {basePos.x + hFwd.x * 0.6f, gy + 1.45f, basePos.z + hFwd.z * 0.6f};
        } else {
            const Vec2 cp = basePos + hFwd * 3.95f + hRt * 4.35f;
            heroCamPos    = {cp.x, gy + 1.50f, cp.z};
            heroCamTarget = {basePos.x - hFwd.x * 0.15f, gy + 0.80f, basePos.z - hFwd.z * 0.15f};
        }
        useHeroCam = true;
    } else if (cli.fleetLineup) {
        const Vec2 basePos = world.city().hasRealSeoulMap() ? world.city().spawnPos() : Vec2{588.0f, 960.0f};
        const float heroYaw = world.city().hasRealSeoulMap() ? world.city().spawnYaw() : -kHalfPi;
        const Vec2 hFwd{std::cos(heroYaw), std::sin(heroYaw)};
        const Vec2 hRt{-hFwd.z, hFwd.x};
        world.fastTravelTo(basePos + hRt * 18.0f + hFwd * 24.0f, "Seoul");
        const VehicleKind lineup[5] = {
            VehicleKind::SportsCoupe, VehicleKind::PoliceCruiser,
            VehicleKind::Taxi, VehicleKind::SeoulBus, VehicleKind::Hypercar
        };
        const ColorRGBA colors[5] = {
            {208, 28, 38, 255}, {244, 246, 250, 255},
            {246, 126, 24, 255}, {26, 86, 178, 255}, {24, 124, 226, 255}
        };
        for (int i = 0; i < 5; ++i) {
            const Vec2 vp = basePos + hRt * (-10.0f + static_cast<float>(i) * 5.0f);
            Vehicle& c = world.spawnCustomVehicle(lineup[i], vp, heroYaw + 0.32f, colors[i]);
            c.steerVisual = 0.22f;
            c.hasNpcDriver = true;
        }
        const float gy = world.city().surfaceHeightAt(basePos);
        const Vec2 cp = basePos + hFwd * 12.8f + hRt * 8.5f;
        heroCamPos    = {cp.x, gy + 3.2f, cp.z};
        heroCamTarget = {basePos.x, gy + 0.9f, basePos.z};
        useHeroCam = true;
    }

    if (!cli.screenshotPath.empty()) {
        world.clearBanner();
    }

    const auto fontBytes              = app.loadAssetOrSystemFont();
    const auto pbrAlbedoBytes         = app.loadAssetBytes("textures/pbr_albedo.png");
    const auto pbrNormalRoughBytes    = app.loadAssetBytes("textures/pbr_normal_rough.png");
    const auto streetAlbedoBytes      = app.loadAssetBytes("textures/street_albedo.png");
    const auto streetNormalRoughBytes = app.loadAssetBytes("textures/street_normal_rough.png");
    const auto signs4kBytes           = app.loadAssetBytes("textures/seoul_signs_4k.png");
    const auto map2dBytes             = app.loadAssetBytes("maps/seoul_map_2d.png");

    Renderer3D renderer;
    renderer.init(vk, world.city(), fontBytes,
                  pbrAlbedoBytes, pbrNormalRoughBytes,
                  streetAlbedoBytes, streetNormalRoughBytes,
                  signs4kBytes);

    Vulkan2D ui2d;
    const bool hasFont = ui2d.init(vk, fontBytes);
    if (!map2dBytes.empty()) {
        ui2d.initMapTexture(vk, map2dBytes);
    }

    HudRenderer hud;
    hud.init(hasFont);
    const bool useMobileUi = app.isAndroid() || cli.mobileUi;
    hud.setMobileUi(useMobileUi);
    if (cli.noHelp || !cli.screenshotPath.empty()) hud.setShowHelp(false);
    hud.setShowCrosshair(!useHeroCam && (cli.screenshotPath.empty() || cli.firstPerson || cli.forceAim));
    if (cli.renderDistM > 0) {
        renderer.mutableSettings().renderDistanceM = cli.renderDistM;
        renderer.mutableSettings().clampAndSyncPreset();
    }
    if (cli.openMap) hud.setFullMapOpen(true, world.player().pos);
    if (cli.openGraphics) hud.setGraphicsMenuOpen(true);

    TouchControls touch;
    touch.setEnabled(useMobileUi);

    SynthAudio audio;
    if (cli.screenshotPath.empty()) audio.init();

    float camYaw = cli.hasYaw ? (cli.yawDeg * kPi / 180.0f) : defaultYaw;
    float camPitch = cli.hasPitch ? (cli.pitchDeg * kPi / 180.0f) : -0.14f;
    if (cli.hasYaw && !cli.drive) {
        world.player().facingYaw = camYaw;
    }
    bool firstPerson = cli.firstPerson;
    float adsBlend = cli.forceAim ? 1.0f : 0.0f;
    float smoothCamArmScale = 1.0f; // Smooth 3rd-person spring-arm building collision pull-in!
    float smoothCamY = 0.0f;        // Critically-smoothed vertical camera tracking (zero vertical jolt!)
    bool smoothCamYInit = false;
    Vec2 lastCamFocusXZ{};
    bool lastCamDriving = false;
    float smoothKeySteer = 0.0f;
    float smoothKeyThrottle = 0.0f;
    float smoothFps = 60.0f;
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

        const float rawDt = app.deltaTime();
        const float dt = std::clamp(rawDt, 0.001f, 0.05f);
        if (rawDt > 0.0005f && rawDt < 1.0f) {
            const float instFps = clampf(1.0f / rawDt, 1.0f, 240.0f);
            smoothFps += (instFps - smoothFps) * std::min(1.0f, dt * 6.0f);
        }
        hud.setFpsStats(smoothFps, 1000.0f / std::max(1.0f, smoothFps));
        const PlatformInput& pi = app.input();
        const int sw = vk.width();
        const int sh = vk.height();

        // DPI-independent logical UI canvas (makes Android HiDPI tablets/foldables/phones crisp & thumb-ergonomic!)
        float uiScale = 1.0f;
        if (useMobileUi) {
            uiScale = clampf(static_cast<float>(sh) / 600.0f, 0.85f, 3.6f);
            uiScale = std::min(uiScale, static_cast<float>(sw) / 940.0f);
            uiScale = std::max(0.75f, uiScale);
        } else {
            uiScale = clampf(static_cast<float>(sh) / 864.0f, 1.0f, 2.5f);
        }
        const float invUiScale = 1.0f / uiScale;
        const int uiW = std::max(1, static_cast<int>(std::lround(static_cast<float>(sw) * invUiScale)));
        const int uiH = std::max(1, static_cast<int>(std::lround(static_cast<float>(sh) * invUiScale)));
        const float inputScaleX = static_cast<float>(uiW) / std::max(1.0f, static_cast<float>(app.screenWidth()));
        const float inputScaleY = static_cast<float>(uiH) / std::max(1.0f, static_cast<float>(app.screenHeight()));
        PlatformInput uiPi = pi;
        uiPi.mouseX *= inputScaleX;
        uiPi.mouseY *= inputScaleY;
        uiPi.mouseDeltaX *= inputScaleX;
        uiPi.mouseDeltaY *= inputScaleY;
        for (int ti = 0; ti < uiPi.touchCount; ++ti) {
            uiPi.touches[static_cast<std::size_t>(ti)].x *= inputScaleX;
            uiPi.touches[static_cast<std::size_t>(ti)].y *= inputScaleY;
        }

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
                hud.setGraphicsMenuOpen(false);
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
            hud.toggleGraphicsMenu();
            if (!useMobileUi) app.setCursorLocked(!hud.isAnyModalOpen());
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
        const RectF mmRect = hud.minimapRect(uiW, uiH);
        if (!useMobileUi && !hud.isAnyModalOpen() && !app.isCursorLocked() &&
            uiPi.mouseLeftPressed && mmRect.contains(uiPi.mouseX, uiPi.mouseY)) {
            hud.setFullMapOpen(true, world.player().pos);
        }

        // Update TouchControls (Android & desktop --mobile-ui preview) in logical dp space
        TouchControlResult tr = touch.update(uiPi, world, uiW, uiH, mmRect, hud.isAnyModalOpen(),
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
            hud.toggleGraphicsMenu();
        }

        // Process Modal / Full-Screen Seoul Map / Graphics Settings Interactions
        ModalTapAction modalAct{};
        if (hud.isFullMapOpen()) {
            hud.updateFullMapInput(uiPi, uiW, uiH, world, dt, modalAct);
        } else if (hud.isGraphicsMenuOpen()) {
            bool gfxChanged = hud.handleGraphicsModalInput(pi, renderer.mutableSettings());
            if (tr.modalTapValid) {
                gfxChanged = hud.handleGraphicsModalTap(uiW, uiH, tr.modalTapPos, renderer.mutableSettings()) || gfxChanged;
            } else if (!useMobileUi && uiPi.mouseLeftPressed) {
                gfxChanged = hud.handleGraphicsModalTap(uiW, uiH, {uiPi.mouseX, uiPi.mouseY}, renderer.mutableSettings()) || gfxChanged;
            }
            if (gfxChanged) {
                world.setTrafficDensity(renderer.settings().trafficDensity);
            }
            if (!hud.isGraphicsMenuOpen() && !useMobileUi) {
                app.setCursorLocked(true);
            }
        } else if (tr.modalTapValid) {
            hud.handleModalTap(world, uiW, uiH, tr.modalTapPos, modalAct);
        } else if (!useMobileUi && hud.isAnyModalOpen() && uiPi.mouseLeftPressed) {
            hud.handleModalTap(world, uiW, uiH, {uiPi.mouseX, uiPi.mouseY}, modalAct);
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
            // Smooth camera look (mouse or DPI-normalized touch drag)
            if (app.isCursorLocked() && !useMobileUi) {
                camYaw += pi.mouseDeltaX * 0.0026f;
                camPitch = std::clamp(camPitch - pi.mouseDeltaY * 0.0022f, -0.78f, 0.72f);
            } else if (useMobileUi) {
                camYaw += tr.lookDeltaX * 0.0056f;
                camPitch = std::clamp(camPitch - tr.lookDeltaY * 0.0046f, -0.75f, 0.70f);
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
            gameIn.aimHeld = (app.isCursorLocked() && pi.mouseRightDown) || tr.aimToggled || cli.forceAim;
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

        // Compute Camera Pose (3rd-person with spring-arm building collision, 1st-person cockpit/FPS, or hero cam)
        CameraState cam{};
        cam.fovyDeg = firstPerson ? (64.0f - adsBlend * 18.0f) : (58.0f - adsBlend * 12.0f);
        auto applySpringArmCollision = [&](const Vec3& anchor, Vec3& desiredPos, float minArmDist) {
            const Vec3 armVec = desiredPos - anchor;
            const float armLen = armVec.length();
            if (armLen < 0.1f) return;
            const Vec3 armDir = armVec / armLen;
            const float hitT = world.city().raycastBuildings3D(anchor, armDir, armLen + 0.35f);
            float targetScale = 1.0f;
            if (hitT > 0.0f && hitT < armLen) {
                const float safeDist = std::max(minArmDist, hitT - 0.42f);
                targetScale = clampf(safeDist / armLen, minArmDist / armLen, 1.0f);
            }
            // Pull in immediately to prevent clipping into walls; glide back out smoothly!
            if (targetScale < smoothCamArmScale) {
                smoothCamArmScale = targetScale;
            } else {
                smoothCamArmScale += (targetScale - smoothCamArmScale) * std::min(1.0f, dt * 7.5f);
            }
            desiredPos = anchor + armDir * (armLen * smoothCamArmScale);
            const float terrainFloor = world.city().surfaceHeightAt({desiredPos.x, desiredPos.z}, anchor.y) + 0.55f;
            if (desiredPos.y < terrainFloor) desiredPos.y = terrainFloor;
        };

        if (useHeroCam) {
            cam.position = heroCamPos;
            cam.target = heroCamTarget;
        } else {
            const auto& pl = world.player();
            const Vehicle* pCar = world.playerVehicle();
            const bool drivingNow = (pCar != nullptr);
            const Vec2 curFocusXZ = drivingNow ? pCar->pos : pl.pos;
            const float rawSurfY = drivingNow
                ? world.city().surfaceHeightAt(pCar->pos, pCar->posY)
                : world.city().surfaceHeightAt(pl.pos, pl.posY);
            const float rawGy = drivingNow
                ? std::max(rawSurfY - 0.12f, pCar->posY)
                : std::max(rawSurfY, pl.posY);

            // Critically-smoothed vertical camera tracking eliminates any remaining micro-jolt
            // over grades, bridge joints & intersections while snapping cleanly on spawn/teleport!
            if (!smoothCamYInit || drivingNow != lastCamDriving ||
                (curFocusXZ - lastCamFocusXZ).lengthSq() > 64.0f) {
                smoothCamY = rawGy;
                smoothCamYInit = true;
            } else {
                const float trackRate = drivingNow ? 13.5f : 18.0f;
                smoothCamY += (rawGy - smoothCamY) * std::min(1.0f, dt * trackRate);
                smoothCamY = clampf(smoothCamY, rawGy - 0.34f, rawGy + 0.34f);
            }
            lastCamFocusXZ = curFocusXZ;
            lastCamDriving = drivingNow;
            const float gy = smoothCamY;

            if (pCar) {
                // Smooth auto-follow yaw behind driving car when moving
                if (pCar->speed() > 3.5f && std::abs(pi.mouseDeltaX) < 0.5f && std::abs(tr.lookDeltaX) < 0.5f && !cli.hasYaw) {
                    const float diff = wrapAngle(pCar->heading - camYaw);
                    camYaw += diff * std::min(1.0f, dt * 3.6f);
                }
                const float viewYaw = lookBack ? (camYaw + kPi) : camYaw;
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
                    applySpringArmCollision({pCar->pos.x, gy + spec.chassisHeight * 0.78f, pCar->pos.z}, cam.position, 2.2f);
                }
            } else {
                if (firstPerson) {
                    cam.position = {pl.pos.x, gy + 1.66f, pl.pos.z};
                    cam.target = cam.position + Vec3{std::cos(camYaw) * std::cos(camPitch),
                                                     std::sin(camPitch),
                                                     std::sin(camYaw) * std::cos(camPitch)} * 20.0f;
                } else {
                    const float dist = 4.35f - adsBlend * 1.75f;
                    const Vec2 right = rightFromHeading(camYaw);
                    const float shoulderOff = lerpf(0.50f, 0.60f, adsBlend);
                    const float upLift = std::max(0.0f, camPitch) * dist * 0.92f;
                    cam.target = {pl.pos.x + right.x * shoulderOff, gy + 1.48f + upLift, pl.pos.z + right.z * shoulderOff};
                    cam.position = {
                        pl.pos.x + right.x * shoulderOff - std::cos(camYaw) * dist * std::cos(camPitch),
                        std::max(gy + 0.65f, gy + 1.48f + 0.62f - std::sin(camPitch) * dist * 0.75f),
                        pl.pos.z + right.z * shoulderOff - std::sin(camYaw) * dist * std::cos(camPitch)
                    };
                    applySpringArmCollision({pl.pos.x, gy + 1.48f, pl.pos.z}, cam.position, 1.05f);
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

        // Pass 3 continued: DPI-Scaled 2D Vulkan HUD + Interactive Full-Screen Seoul Map + Graphics Settings + Mobile Touch UI!
        vk.setUiLogicalSize(static_cast<float>(uiW), static_cast<float>(uiH));
        ui2d.beginBatch();
        hud.renderHud(ui2d, vk, world, uiW, uiH, camYaw, app.isCursorLocked(), firstPerson, adsBlend, &renderer.settings());
        touch.render(ui2d, world, uiW, uiH, hud.isAnyModalOpen(), firstPerson, hud.isUsingKorean());
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
    if (!app.initDesktop(cli.windowW, cli.windowH, "Open City Seoul v6.5 — Whole-Seoul Grade-Separated Overpasses, T-Piers & Zero-Intrusion Streetscape")) {
        return 1;
    }
    const int rc = oc::runApplication(app, cli);
    app.shutdown();
    return rc;
}
#endif
