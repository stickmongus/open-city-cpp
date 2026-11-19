#include "audio/SynthAudio.hpp"
#include "gameplay/GameWorld.hpp"
#include "render/Renderer3D.hpp"
#include "ui/HudRenderer.hpp"
#include "ui/TouchControls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <raylib.h>

int main(int argc, char** argv) {
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    constexpr bool kIsAndroid = true;
#else
    constexpr bool kIsAndroid = false;
#endif

    bool smokeTestMode = false;
    bool mobileUiMode = kIsAndroid;
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
    int weaponOverride = -1;
    bool fleetShowcaseDemo = false;
    int openCustomTabOverride = -1;
    bool charPresetDemo = false;
    bool gunPresetDemo = false;
    bool carPresetDemo = false;
    bool wetRoadDemo = false;
    bool stuntShotDemo = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTestMode = true;
        } else if (std::strcmp(argv[i], "--mobile-ui") == 0) {
            mobileUiMode = true;
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
        } else if (std::strcmp(argv[i], "--open-custom") == 0 && i + 1 < argc) {
            openCustomTabOverride = std::clamp(std::atoi(argv[++i]), 0, 3);
        } else if (std::strcmp(argv[i], "--char-preset") == 0) {
            charPresetDemo = true;
        } else if (std::strcmp(argv[i], "--gun-preset") == 0) {
            gunPresetDemo = true;
        } else if (std::strcmp(argv[i], "--car-preset") == 0) {
            carPresetDemo = true;
        } else if (std::strcmp(argv[i], "--wet-road") == 0) {
            wetRoadDemo = true;
        } else if (std::strcmp(argv[i], "--stunt-shot") == 0) {
            stuntShotDemo = true;
        } else if (std::strcmp(argv[i], "--open-shop") == 0) {
            openShopOverride = true;
        } else if (std::strcmp(argv[i], "--cash") == 0 && i + 1 < argc) {
            cashOverride = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--drive-hypercar") == 0) {
            driveHypercarDemo = true;
            driveKindOverride = static_cast<int>(oc::VehicleKind::Hypercar);
        } else if (std::strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
            driveKindOverride = std::clamp(std::atoi(argv[++i]), 0, static_cast<int>(oc::VehicleKind::Count) - 1);
        } else if (std::strcmp(argv[i], "--weapon") == 0 && i + 1 < argc) {
            weaponOverride = std::clamp(std::atoi(argv[++i]), 0, static_cast<int>(oc::WeaponId::Count) - 1);
        } else if (std::strcmp(argv[i], "--fleet-demo") == 0) {
            fleetShowcaseDemo = true;
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

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(0, 0, "Open City v1.2.0");
#else
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 860, "Open City C++ + Rust v1.2.0 - 3D Open-World Sandbox");
#endif
    SetExitKey(KEY_NULL); // Keep ESC / Android Back free for closing modals instead of instant quit
    SetTargetFPS(60);

    oc::GameWorld world(20261107ULL);
    oc::Renderer3D renderer;
    oc::HudRenderer hud;
    oc::TouchControls touch;
    oc::SynthAudio audio;

    renderer.init();
    hud.init();
    hud.setMobileUi(mobileUiMode);
    touch.setEnabled(mobileUiMode);
    audio.init();
    if (mobileUiMode) {
        world.pushBanner("WELCOME TO SUNSET BAY", "선셋 베이 시티에 오신 것을 환영합니다",
                         "Left Stick Move  |  Drag Right to Aim  |  Top Bar: 1P / Custom / Phone / Rain",
                         "좌측 스틱 이동 · 우측 드래그 조준 · 상단 바 1·3인칭 / 커스텀 / 폰 / 날씨",
                         {92, 224, 255, 255}, 4.8f);
    }

    if (wetRoadDemo) {
        world.setRainWeather(true);
    }
    if (cashOverride >= 0) {
        world.player().cash = cashOverride;
    }
    if (hasPosOverride) {
        world.player().pos = {posOverrideX, posOverrideZ};
        world.player().posY = world.surfaceHeightAt(world.player().pos);
    }
    if (weaponOverride >= 0) {
        const auto wid = static_cast<oc::WeaponId>(weaponOverride);
        const auto& wspec = oc::getWeaponSpec(wid);
        auto& slot = world.player().weapons[static_cast<std::size_t>(wid)];
        slot.owned = true;
        slot.magAmmo = wspec.magazineSize;
        slot.reserveAmmo = wspec.magazineSize * 4;
        world.player().currentWeapon = wid;
        if (wid == oc::WeaponId::Minigun) {
            world.player().minigunSpinSpeed = 26.0f;
            world.player().minigunSpin = 0.65f;
        }
    }
    if (driveKindOverride >= 0 || driveHypercarDemo) {
        const auto targetKind = static_cast<oc::VehicleKind>(
            driveKindOverride >= 0 ? driveKindOverride : static_cast<int>(oc::VehicleKind::Hypercar));
        oc::Vehicle* chosenCar = nullptr;
        if (oc::getVehicleSpec(targetKind).dealershipPrice > 0 || targetKind == oc::VehicleKind::SportsCoupe) {
            world.player().cash = std::max(world.player().cash, 85000);
            world.buyOrSummonVehicle(targetKind);
            chosenCar = world.findVehicleMutable(world.player().activePersonalVehicleId);
        } else {
            for (auto& v : world.vehicles()) {
                if (!v.destroyed && v.kind == targetKind) {
                    chosenCar = world.findVehicleMutable(v.id);
                    break;
                }
            }
            if (chosenCar == nullptr) {
                for (auto& v : world.vehicles()) {
                    if (!v.destroyed && !v.isPersonalCar) {
                        chosenCar = world.findVehicleMutable(v.id);
                        if (chosenCar != nullptr) {
                            chosenCar->kind = targetKind;
                            chosenCar->maxHealth = oc::getVehicleSpec(targetKind).maxHealth;
                            chosenCar->health = chosenCar->maxHealth;
                            chosenCar->isPolice = (targetKind == oc::VehicleKind::PoliceCruiser);
                            break;
                        }
                    }
                }
            }
        }
        if (chosenCar != nullptr) {
            if (hasPosOverride) {
                chosenCar->pos = {posOverrideX, posOverrideZ};
                chosenCar->posY = world.surfaceHeightAt(chosenCar->pos);
            }
            if (initialYawOverride > -100.0f) {
                chosenCar->heading = initialYawOverride;
            }
            chosenCar->neonPreset = demoNeonPreset;
            if (targetKind == oc::VehicleKind::Hypercar || targetKind == oc::VehicleKind::SportsCoupe ||
                targetKind == oc::VehicleKind::JdmTuner || targetKind == oc::VehicleKind::TrackPrototype) {
                chosenCar->customSpoiler = true;
            }
            chosenCar->engineUpgradeLevel = 2;
            chosenCar->armorUpgradeLevel = 2;
            chosenCar->isPlayerDriving = true;
            chosenCar->isParked = false;
            if (oc::isEmergencySirenVehicle(*chosenCar)) {
                chosenCar->sirenActive = true;
            }
            if (hasSteerOverride) {
                chosenCar->steerVisual = demoSteerOverride;
            }
            const oc::Vec2 cfwd = oc::forwardFromHeading(chosenCar->heading);
            chosenCar->velocity = cfwd * (smokeTestMode ? 18.5f : 0.0f);
            if (stuntShotDemo) {
                chosenCar->posY = 3.25f;
                chosenCar->velY = 4.6f;
                chosenCar->airborne = true;
                chosenCar->bodyPitch = 0.24f;
                chosenCar->nitroActive = true;
                chosenCar->currentGear = 5;
                chosenCar->engineRpm = 7450.0f;
                chosenCar->velocity = cfwd * 32.0f;
            }

            world.player().drivingVehicleId = chosenCar->id;
            world.player().pos = chosenCar->pos;
        }
    }
    if (fleetShowcaseDemo) {
        const float lookYaw = (initialYawOverride > -100.0f) ? initialYawOverride : 0.0f;
        const oc::Vec2 fwd = oc::forwardFromHeading(lookYaw);
        const oc::Vec2 right = oc::rightFromHeading(lookYaw);
        const oc::Vec2 base = world.player().pos;
        // Clear existing street vehicles near showcase zone first
        for (auto& v : world.vehicles()) {
            if ((v.pos - (base + fwd * 14.0f)).length() < 34.0f) {
                v.pos = base - fwd * 65.0f;
                v.velocity = {0.0f, 0.0f};
            }
        }
        struct ShowcaseEntry {
            oc::VehicleKind kind;
            float fwdDist;
            float rightDist;
            float yawOff;
            oc::ColorRGBA paint;
            bool matte;
            bool spoiler;
            int neon;
            bool siren;
        };
        const ShowcaseEntry entries[6] = {
            {oc::VehicleKind::TrackPrototype,  9.2f, -3.1f,  0.52f, {34, 42, 54, 255},    false, true,  1, false},
            {oc::VehicleKind::ClassicRoadster, 9.6f,  3.2f, -0.54f, {214, 28, 36, 255},   false, false, 0, false},
            {oc::VehicleKind::JdmTuner,       15.0f, -5.6f,  0.46f, {24, 98, 228, 255},   false, true,  2, false},
            {oc::VehicleKind::OffroadPickup,  15.6f,  5.6f, -0.44f, {226, 128, 34, 255},  false, false, 0, false},
            {oc::VehicleKind::Ambulance,      22.2f, -3.4f,  0.34f, {246, 248, 252, 255}, false, false, 0, true},
            {oc::VehicleKind::FireTruck,      23.5f,  4.2f, -0.36f, {206, 26, 28, 255},   false, false, 0, true},
        };
        int idx = 0;
        for (auto& v : world.vehicles()) {
            if (v.destroyed || v.isPersonalCar) continue;
            if (idx >= 6) break;
            const auto& e = entries[idx++];
            v.kind = e.kind;
            v.maxHealth = oc::getVehicleSpec(e.kind).maxHealth;
            v.health = v.maxHealth;
            v.pos = base + fwd * e.fwdDist + right * e.rightDist;
            v.heading = oc::wrapAngle(lookYaw + oc::kPi + e.yawOff);
            v.velocity = {0.0f, 0.0f};
            v.isParked = true;
            v.hasNpcDriver = false;
            v.isPolice = false;
            v.paintColor = e.paint;
            v.stripeColor = {245, 248, 252, 255};
            v.mattePaint = e.matte;
            v.customSpoiler = e.spoiler;
            v.neonPreset = e.neon;
            v.rimStyle = idx % 3;
            v.sirenActive = e.siren;
            v.sirenPhase = (idx == 5) ? 0.18f : 0.68f;
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
    if (charPresetDemo) {
        auto& pl = world.player();
        pl.cash = std::max(pl.cash, 48500);
        pl.ownedOutfits[static_cast<std::size_t>(oc::OutfitId::GoldBossCoat)] = true;
        pl.currentOutfit = oc::OutfitId::GoldBossCoat;
        pl.characterCustom.hairOrHatStyle = 6;      // 24K VIP Gold Crown
        pl.characterCustom.hairColorIdx = 5;        // Royal Gold
        pl.characterCustom.glassesStyle = 3;        // Cyan Tactical Goggles
        pl.characterCustom.skinToneIdx = 2;         // Golden Tan
        pl.characterCustom.customTopColorIdx = 1;   // Crimson Red Coat
        pl.characterCustom.customBottomColorIdx = 1;// Jet Black Pants
        pl.characterCustom.backGearStyle = 3;       // Cyber Neon Wing Pack
        if (initialYawOverride > -100.0f) {
            pl.facingYaw = initialYawOverride;
        }
        auto& ar = pl.weapons[static_cast<std::size_t>(oc::WeaponId::AssaultRifle)];
        ar.owned = true;
        ar.skinIndex = 2; // 24K Royal Gold
        ar.suppressorOwned = true;
        ar.suppressor = true;
        ar.extendedMagOwned = true;
        ar.extendedMag = true;
        ar.magAmmo = oc::effectiveMagazineSize(oc::WeaponId::AssaultRifle, true);
        ar.reserveAmmo = 180;
        ar.laserSightColor = 3; // Cyber Cyan Laser
        pl.currentWeapon = oc::WeaponId::AssaultRifle;
        pl.hudCustom.hudThemeIdx = 2; // VIP Royal Gold
    }
    if (gunPresetDemo) {
        auto& pl = world.player();
        pl.cash = std::max(pl.cash, 36000);
        const oc::WeaponId wid = (weaponOverride >= 0)
                                     ? static_cast<oc::WeaponId>(weaponOverride)
                                     : oc::WeaponId::AssaultRifle;
        auto& slot = pl.weapons[static_cast<std::size_t>(wid)];
        slot.owned = true;
        slot.skinIndex = 2; // 24K Royal Gold
        if (oc::weaponSupportsSuppressor(wid)) {
            slot.suppressorOwned = true;
            slot.suppressor = true;
        }
        slot.extendedMagOwned = true;
        slot.extendedMag = true;
        slot.magAmmo = oc::effectiveMagazineSize(wid, true);
        slot.reserveAmmo = slot.magAmmo * 5;
        slot.laserSightColor = 3; // Cyber Cyan 3D Laser Beam
        slot.tracerColorIdx = 2;  // Cyber Cyan Tracer
        slot.opticColorIdx = 3;   // Cyber Cyan Optic
        pl.currentWeapon = wid;
        pl.characterCustom.skinToneIdx = 2;
        pl.characterCustom.customTopColorIdx = 1;
        pl.hudCustom.crosshairStyle = 2;    // Tactical Circle + Dot
        pl.hudCustom.crosshairColorIdx = 2; // Cyber Cyan
        pl.hudCustom.hudThemeIdx = 0;       // Cyber Cyan
    }
    if (carPresetDemo) {
        auto& pl = world.player();
        pl.cash = std::max(pl.cash, 92000);
        const auto targetKind = (driveKindOverride >= 0)
                                    ? static_cast<oc::VehicleKind>(driveKindOverride)
                                    : oc::VehicleKind::JdmTuner;
        auto& gslot = pl.garage[static_cast<std::size_t>(targetKind)];
        gslot.owned = true;
        gslot.paintPresetIndex = 8; // Mirror Chrome
        gslot.paintColor = {226, 234, 244, 255};
        gslot.stripeStyle = 2;      // Royal Gold Dual GT Stripes
        gslot.stripeColor = {248, 198, 56, 255};
        gslot.mattePaint = false;
        gslot.chromePaint = true;
        gslot.rimStyle = 3;         // Cyber Cyan Forged Rims
        gslot.spoilerStyle = 2;     // Swan-Neck Time-Attack Wing (+6% Grip)
        gslot.customSpoiler = true;
        gslot.stanceStyle = 2;      // Slammed Negative Camber (-82mm)
        gslot.headlightColorIdx = 1;// Cyber Cyan Xenon Beams
        gslot.neonPreset = 2;       // UV Neon Purple Underglow
        gslot.driftSmokeColorIdx = 1;
        gslot.engineUpgradeLevel = 2;
        gslot.armorUpgradeLevel = 2;
        world.buyOrSummonVehicle(targetKind);
        if (oc::Vehicle* pv = world.findVehicleMutable(pl.activePersonalVehicleId)) {
            const float showYaw = (initialYawOverride > -100.0f) ? initialYawOverride : 0.0f;
            if (hasPosOverride) {
                pv->pos = {posOverrideX, posOverrideZ};
            }
            pv->heading = showYaw;
            pv->velocity = {0.0f, 0.0f};
            pv->steerVisual = hasSteerOverride ? demoSteerOverride : 0.34f;
            pv->isParked = true;
            const oc::Vec2 rDir = oc::rightFromHeading(showYaw);
            pl.pos = pv->pos - rDir * 2.8f;
            pl.posY = world.surfaceHeightAt(pl.pos);
            // Clear nearby street cars so the showroom shot is unobstructed
            for (auto& other : world.vehicles()) {
                if (other.id != pv->id && (other.pos - pv->pos).length() < 26.0f) {
                    other.pos = pv->pos - oc::forwardFromHeading(showYaw) * 58.0f;
                    other.velocity = {0.0f, 0.0f};
                }
            }
        }
        world.clearWantedLevel(false);
    }
    if (initialTimeOverride >= 0.0f) {
        const float deltaH = initialTimeOverride - world.timeOfDayHours();
        world.advanceTimeOfDayHours(deltaH >= 0.0f ? deltaH : (deltaH + 24.0f));
    }
    if (openPhoneTabOverride >= 0) {
        hud.setPhoneOpen(true);
        hud.setPhoneTab(openPhoneTabOverride);
    }
    if (openCustomTabOverride >= 0) {
        hud.setCustomStudioOpen(true);
        hud.setCustomTab(openCustomTabOverride);
    }
    if (openShopOverride && world.isPlayerInAnyShopZone()) {
        hud.setShopMenuOpen(true);
    }
    if (hideHelpInSmoke && !mobileUiMode) {
        hud.toggleHelpOverlay();
    }

    bool cursorLocked = !mobileUiMode;
    bool invertMouseY = false;
    bool firstPersonMode = startFirstPerson;
    float mouseSensScale = 1.0f;
    int skipMouseFrames = 2;
    if (cursorLocked) {
        DisableCursor();
    }

    float camYaw = (initialYawOverride > -100.0f) ? initialYawOverride : -oc::kHalfPi;
    float camPitch = (initialPitchOverride > -100.0f)
                         ? initialPitchOverride
                         : (startFirstPerson ? -0.04f : -0.10f);
    float cockpitRelYaw = 0.0f;
    float manualCamHoldTimer = (initialYawOverride > -100.0f || initialPitchOverride > -100.0f) ? 5.0f : 0.0f;
    float adsBlend = forceAdsDemo ? 1.0f : 0.0f;
    const bool initSniperAds = forceAdsDemo && world.player().currentWeapon == oc::WeaponId::SniperRifle;
    float smoothedFov = initSniperAds ? 24.0f : (startFirstPerson ? (forceAdsDemo ? 57.0f : 68.0f) : 60.0f);
    bool camInitialized = false;
    oc::Vec3 smoothedAnchor{world.player().pos.x, world.player().posY + 1.54f, world.player().pos.z};
    float smoothedBoomDist = 4.60f;
    float smoothedEyeY = world.player().posY + 1.62f;
    int prevDrivingId = world.player().drivingVehicleId;

    constexpr int kWeaponSlotCount = static_cast<int>(oc::WeaponId::Count);

    auto readMenuOptionKey = []() -> int {
        for (int k = 0; k < 9; ++k) {
            if (IsKeyPressed(KEY_ONE + k)) return k + 1;
        }
        if (IsKeyPressed(KEY_ZERO)) return 10;
        if (IsKeyPressed(KEY_MINUS)) return 11;
        if (IsKeyPressed(KEY_EQUAL)) return 12;
        return 0;
    };

    float backExitTimer = 0.0f;

    while (!WindowShouldClose()) {
        const float frameDt = std::min(GetFrameTime(), 0.05f);
        if (backExitTimer > 0.0f) {
            backExitTimer = std::max(0.0f, backExitTimer - frameDt);
        }
        if (kIsAndroid && !IsWindowFocused()) {
            touch.resetState();
            audio.updateVehicleLoops(false, 0.0f, false, false);
            audio.updatePoliceSiren(0.0f);
            BeginDrawing();
            ClearBackground(BLACK);
            EndDrawing();
            continue;
        }
        const int screenW = GetScreenWidth();
        const int screenH = GetScreenHeight();
        const bool anyModalOpen = hud.isShopMenuOpen() || hud.isPhoneOpen() || hud.isCustomStudioOpen();

        // Multi-touch controller update (plus mouse-click simulation in desktop --mobile-ui preview)
        const oc::TouchControlResult tRes =
            touch.update(world, screenW, screenH, anyModalOpen, hud.isCustomStudioOpen(), !kIsAndroid);

        oc::ModalTapAction modalTap{};
        if (tRes.modalTapValid && anyModalOpen) {
            hud.handleModalTap(world, screenW, screenH, tRes.modalTapPos, modalTap);
        }

        // Window / cursor / modal hotkeys (plus Android hardware/gesture Back key)
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK)) {
            if (hud.isShopMenuOpen()) {
                hud.setShopMenuOpen(false);
                backExitTimer = 0.0f;
            } else if (hud.isPhoneOpen()) {
                hud.setPhoneOpen(false);
                backExitTimer = 0.0f;
            } else if (hud.isCustomStudioOpen()) {
                hud.setCustomStudioOpen(false);
                backExitTimer = 0.0f;
            } else if (kIsAndroid && IsKeyPressed(KEY_BACK)) {
                if (backExitTimer > 0.0f) {
                    break;
                }
                backExitTimer = 2.4f;
                hud.showSystemToast("Press Back again to exit", "한 번 더 누르면 게임이 종료됩니다");
            } else if (!mobileUiMode && IsKeyPressed(KEY_ESCAPE)) {
                cursorLocked = !cursorLocked;
                if (cursorLocked) {
                    DisableCursor();
                    skipMouseFrames = 2;
                } else {
                    EnableCursor();
                }
            }
        }
        if (!mobileUiMode && IsKeyPressed(KEY_TAB)) {
            cursorLocked = !cursorLocked;
            if (cursorLocked) {
                DisableCursor();
                skipMouseFrames = 2;
            } else {
                EnableCursor();
            }
        }
        if (!mobileUiMode && !cursorLocked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hud.isShopMenuOpen()) {
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
        // [V] or Middle Mouse Button or Mobile Top Bar: Toggle 1st-Person (FPS / Cockpit) <-> 3rd-Person Camera!
        if (IsKeyPressed(KEY_V) || (!mobileUiMode && !hud.isShopMenuOpen() && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) ||
            tRes.toggleViewPressed) {
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
        // [F3] or Mobile Top Bar: Cycle graphics quality preset (Ultra / High / Performance)
        if (IsKeyPressed(KEY_F3) || tRes.cycleQualityPressed) {
            renderer.cycleQuality();
            hud.showGraphicsToast(renderer.qualityNameEn(), renderer.qualityNameKo());
        }
        // [ / ]: Live mouse sensitivity tuning (30% .. 250%) to suit any DPI without menu diving
        if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
            const float delta = IsKeyPressed(KEY_RIGHT_BRACKET) ? 0.10f : -0.10f;
            mouseSensScale = oc::clampf(std::round((mouseSensScale + delta) * 10.0f) / 10.0f, 0.30f, 2.50f);
            const int pct = static_cast<int>(std::round(mouseSensScale * 10.0f) * 10);
            char sensEn[64];
            char sensKo[80];
            std::snprintf(sensEn, sizeof(sensEn), "MOUSE SENSITIVITY: %d%%  ([ / ])", pct);
            std::snprintf(sensKo, sizeof(sensKo), "마우스 감도: %d%%  ([ / ] 조절)", pct);
            hud.showSystemToast(sensEn, sensKo);
        }
        if (IsKeyPressed(KEY_T) || tRes.advanceTimePressed) {
            world.advanceTimeOfDayHours(3.0f);
        }
        if (IsKeyPressed(KEY_U) || tRes.toggleWeatherPressed) {
            world.toggleRainWeather();
            hud.showSystemToast(
                world.isRainWeather() ? "WEATHER: RAIN & WET ROAD REFLECTIONS" : "WEATHER: CLEAR SKY",
                world.isRainWeather() ? "날씨 전환: 우천 및 젖은 아스팔트 거울 반사" : "날씨 전환: 맑음");
        }
        if (IsKeyPressed(KEY_P) || tRes.togglePhonePressed) {
            hud.setPhoneOpen(!hud.isPhoneOpen());
        }
        if (IsKeyPressed(KEY_K) || tRes.toggleCustomPressed) {
            const bool opening = !hud.isCustomStudioOpen();
            hud.setCustomStudioOpen(opening);
            if (opening) {
                if (world.playerVehicle() != nullptr) {
                    hud.setCustomTab(2);
                    camYaw = world.playerVehicle()->heading;
                } else {
                    camYaw = world.player().facingYaw;
                }
            }
        }
        if (IsKeyPressed(KEY_B) || tRes.toggleShopPressed) {
            if (world.isPlayerInAnyShopZone()) {
                hud.setShopMenuOpen(!hud.isShopMenuOpen());
            } else {
                hud.setShopMenuOpen(false);
            }
        }

        const bool wantPrecisionAim =
            (smokeTestMode && forceAdsDemo) ||
            (cursorLocked && !hud.isShopMenuOpen() && world.player().drivingVehicleId < 0 &&
             IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) ||
            tRes.aimToggled;
        adsBlend = (smokeTestMode && forceAdsDemo)
                       ? 1.0f
                       : oc::lerpf(adsBlend, wantPrecisionAim ? 1.0f : 0.0f, std::min(1.0f, frameDt * 12.0f));

        const bool sniperEquipped =
            (world.player().drivingVehicleId < 0 && world.player().currentWeapon == oc::WeaponId::SniperRifle);

        // 1:1 Synchronous Mouse Look (0ms angular lag, cursor-warp spike guard, automatic ADS / Scope slowdown)
        if (cursorLocked && !hud.isShopMenuOpen()) {
            const Vector2 md = GetMouseDelta();
            if (skipMouseFrames > 0) {
                --skipMouseFrames;
            } else {
                const float dx = oc::clampf(md.x, -120.0f, 120.0f);
                const float dy = oc::clampf(md.y, -120.0f, 120.0f);
                if (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f) {
                    const float adsSensScale = sniperEquipped ? 0.28f : 0.58f;
                    const float sens = 0.00175f * mouseSensScale * oc::lerpf(1.0f, adsSensScale, adsBlend);
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
        // Multi-touch drag camera look & 3D Studio showcase orbit
        if (std::abs(tRes.lookDeltaX) > 0.01f || std::abs(tRes.lookDeltaY) > 0.01f) {
            const float tdx = oc::clampf(tRes.lookDeltaX, -140.0f, 140.0f);
            const float tdy = oc::clampf(tRes.lookDeltaY, -140.0f, 140.0f);
            const float adsSensScale = sniperEquipped ? 0.30f : 0.60f;
            const float tSens = 0.0036f * oc::lerpf(1.0f, adsSensScale, adsBlend);
            const float ySign = invertMouseY ? 1.0f : -1.0f;
            if (firstPersonMode && world.playerVehicle() != nullptr) {
                cockpitRelYaw = oc::clampf(cockpitRelYaw + tdx * tSens, -1.55f, 1.55f);
                camPitch = oc::clampf(camPitch + ySign * tdy * tSens, -0.68f, 0.62f);
                camYaw = oc::wrapAngle(world.playerVehicle()->heading + cockpitRelYaw);
            } else {
                camYaw = oc::wrapAngle(camYaw + tdx * tSens);
                camPitch = oc::clampf(camPitch + ySign * tdy * (tSens * 0.90f), -1.12f, 1.18f);
            }
            manualCamHoldTimer = 1.6f;
        }
        if (manualCamHoldTimer > 0.0f) {
            manualCamHoldTimer = std::max(0.0f, manualCamHoldTimer - frameDt);
        }

        // Gather gameplay inputs (keyboard/mouse + mobile multi-touch merged cleanly)
        oc::PlayerInput input{};
        const int menuKey = readMenuOptionKey();
        if (!hud.isShopMenuOpen()) {
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    input.moveForward += 1.0f;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  input.moveForward -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) input.moveRight += 1.0f;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  input.moveRight -= 1.0f;

            input.moveForward = oc::clampf(input.moveForward + tRes.moveForward, -1.0f, 1.0f);
            input.moveRight = oc::clampf(input.moveRight + tRes.moveRight, -1.0f, 1.0f);

            input.sprintOrNitro = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) || tRes.sprintOrNitro;
            input.handbrake = IsKeyDown(KEY_SPACE) || tRes.handbrake;
            input.jumpPressed = IsKeyPressed(KEY_SPACE) || tRes.jumpPressed;
            input.interactPressed =
                IsKeyPressed(KEY_F) || (!hud.isPhoneOpen() && !hud.isCustomStudioOpen() && IsKeyPressed(KEY_E)) ||
                tRes.interactPressed;
            input.aimHeld = wantPrecisionAim;
            const bool suppressModalFire =
                hud.isPhoneOpen() ||
                (hud.isCustomStudioOpen() && hud.customTab() != 1 && hud.customTab() != 3);
            input.fireHeld =
                (cursorLocked && !suppressModalFire && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                (!suppressModalFire && tRes.fireHeld);
            input.reloadPressed = IsKeyPressed(KEY_R) || tRes.reloadPressed;
            input.hornPressed = IsKeyPressed(KEY_G) || tRes.hornPressed;
            input.abortMissionPressed = IsKeyPressed(KEY_BACKSPACE);

            auto handleWeaponCycleKeys = [&]() {
                const float wheel = GetMouseWheelMove();
                int cycleStep = tRes.weaponCycleDelta;
                if (cycleStep == 0) {
                    if (std::abs(wheel) > 0.1f) {
                        cycleStep = (wheel > 0.0f) ? 1 : -1;
                    } else if (IsKeyPressed(KEY_X)) {
                        cycleStep = 1;
                    } else if (IsKeyPressed(KEY_Z)) {
                        cycleStep = -1;
                    }
                }
                if (cycleStep != 0 && world.player().drivingVehicleId < 0) {
                    int cur = static_cast<int>(world.player().currentWeapon);
                    for (int tries = 0; tries < kWeaponSlotCount; ++tries) {
                        cur = (cur + cycleStep + kWeaponSlotCount) % kWeaponSlotCount;
                        if (world.player().weapons[static_cast<std::size_t>(cur)].owned) {
                            input.weaponSelectSlot = cur;
                            break;
                        }
                    }
                }
            };

            if (hud.isPhoneOpen()) {
                if (IsKeyPressed(KEY_Q)) hud.cyclePhoneTab(-1);
                if (IsKeyPressed(KEY_E)) hud.cyclePhoneTab(1);
                const int maxOpt = (hud.phoneTab() == 0 || hud.phoneTab() == 3) ? 10 : 6;
                if (modalTap.phoneOption >= 1 && modalTap.phoneOption <= maxOpt) {
                    input.phoneTab = modalTap.phoneTab;
                    input.phoneOption = modalTap.phoneOption;
                } else if (menuKey >= 1 && menuKey <= maxOpt) {
                    input.phoneTab = hud.phoneTab();
                    input.phoneOption = menuKey;
                }
            } else if (hud.isCustomStudioOpen()) {
                if (IsKeyPressed(KEY_Q)) hud.cycleCustomTab(-1);
                if (IsKeyPressed(KEY_E)) hud.cycleCustomTab(1);
                const int maxOpt = (hud.customTab() == 0 || hud.customTab() == 2) ? 8 : 6;
                if (modalTap.customStudioOption >= 1 && modalTap.customStudioOption <= maxOpt) {
                    input.customStudioTab = modalTap.customStudioTab;
                    input.customStudioOption = modalTap.customStudioOption;
                } else if (menuKey >= 1 && menuKey <= maxOpt) {
                    input.customStudioTab = hud.customTab();
                    input.customStudioOption = menuKey;
                }
                handleWeaponCycleKeys();
            } else {
                if (menuKey >= 1 && menuKey <= kWeaponSlotCount) {
                    input.weaponSelectSlot = menuKey - 1;
                }
                handleWeaponCycleKeys();
            }
        } else {
            if (modalTap.shopBuyOption >= 1 && modalTap.shopBuyOption <= 12) {
                input.shopBuyOption = modalTap.shopBuyOption;
            } else if (menuKey >= 1 && menuKey <= 12) {
                input.shopBuyOption = menuKey;
            }
        }

        const bool lookBack = IsKeyDown(KEY_C) || tRes.lookBackHeld;
        const bool sniperScoped = !lookBack && sniperEquipped && adsBlend > 0.72f;
        const bool activeFirstPerson = (firstPersonMode || sniperScoped) && !lookBack;

        const bool studioAvatarShowcasePre =
            hud.isCustomStudioOpen() && !activeFirstPerson && !lookBack &&
            world.player().drivingVehicleId < 0 && (hud.customTab() == 0 || hud.customTab() == 1) &&
            !input.fireHeld && !input.aimHeld &&
            std::abs(input.moveForward) < 0.01f && std::abs(input.moveRight) < 0.01f;

        // Pre-step aim ray for exact 1:1 hitscan alignment inside world.update()
        const float preAimYaw =
            studioAvatarShowcasePre
                ? world.player().facingYaw
                : ((activeFirstPerson && world.playerVehicle() != nullptr)
                       ? oc::wrapAngle(world.playerVehicle()->heading + cockpitRelYaw)
                       : (lookBack ? oc::wrapAngle(camYaw + oc::kPi) : camYaw));
        const float preAimPitch = studioAvatarShowcasePre ? -0.03f : (lookBack ? -0.12f : camPitch);
        const oc::Vec3 preViewDir = oc::directionFromYawPitch(preAimYaw, preAimPitch).normalized();

        oc::Vec3 preRayOrigin{};
        if (studioAvatarShowcasePre) {
            preRayOrigin = {world.player().pos.x, world.player().posY + 1.38f, world.player().pos.z};
        } else if (activeFirstPerson) {
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
        if (!(smokeTestMode && (jumpPanicDemo || fleetShowcaseDemo || stuntShotDemo))) {
            world.update(frameDt, input);
        } else if (smokeTestMode && (fleetShowcaseDemo || stuntShotDemo)) {
            // Still update siren strobe phases for emergency showcase vehicles
            for (auto& v : world.vehicles()) {
                if (v.sirenActive) {
                    v.sirenPhase = std::fmod(v.sirenPhase + frameDt * 3.1f, 1.0f);
                }
            }
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

        const bool postSniperEquipped =
            (world.player().drivingVehicleId < 0 && world.player().currentWeapon == oc::WeaponId::SniperRifle);
        const bool postSniperScoped = !lookBack && postSniperEquipped && adsBlend > 0.72f;
        const bool postFirstPerson = (firstPersonMode || postSniperScoped) && !lookBack;

        // Evaluate post-step Camera3D (1st-Person Cockpit / 1st-Person FPS & Sniper Scope / Studio Showcase / 3rd-Person)
        float renderYaw = camYaw;
        float renderPitch = camPitch;
        oc::Vec3 viewDir{};
        oc::Vec3 finalCamPos{};
        float targetFov = 60.0f;

        const oc::Vehicle* studioTargetCar =
            (hud.isCustomStudioOpen() && !postFirstPerson && !lookBack && hud.customTab() == 2)
                ? world.customizationTargetVehicle()
                : nullptr;
        const bool useStudioCarShowcase = (studioTargetCar != nullptr && studioTargetCar->speed() < 1.5f);
        const bool useStudioCharShowcase =
            hud.isCustomStudioOpen() && !postFirstPerson && !lookBack &&
            world.playerVehicle() == nullptr && (hud.customTab() == 0 || hud.customTab() == 1) &&
            world.player().velocity.lengthSq() < 0.4f && !input.fireHeld && !input.aimHeld;

        if (postFirstPerson && world.playerVehicle() != nullptr) {
            // 1st-Person Vehicle Cockpit: locked to left-hand driver seat, chassis turns head naturally
            const oc::Vehicle* car = world.playerVehicle();
            if (manualCamHoldTimer <= 0.0f && car->speed() > 1.5f) {
                cockpitRelYaw = oc::lerpf(cockpitRelYaw, car->steerVisual * 0.14f, std::min(1.0f, frameDt * 3.8f));
                camPitch = oc::lerpf(camPitch, -0.04f, std::min(1.0f, frameDt * 3.5f));
            }
            renderYaw = oc::wrapAngle(car->heading + cockpitRelYaw);
            camYaw = renderYaw;
            renderPitch = oc::clampf(camPitch + car->bodyPitch, -0.68f, 0.78f);
            viewDir = oc::directionFromYawPitch(renderYaw, renderPitch).normalized();

            const oc::Vec3 eyeLocal = oc::driverSeatEyeLocal(car->kind);
            const oc::Vec2 carFwd = oc::forwardFromHeading(car->heading);
            const oc::Vec2 carRight = oc::rightFromHeading(car->heading);
            const float stanceOffsetY = car->destroyed ? 0.0f
                                      : ((car->stanceStyle == 1) ? -0.032f
                                      : ((car->stanceStyle == 2) ? -0.048f
                                      : ((car->stanceStyle == 3) ?  0.095f : 0.0f)));
            const float cRoll = std::cos(car->bodyRoll);
            const float sRoll = std::sin(car->bodyRoll);
            const float cPitch = std::cos(car->bodyPitch);
            const float sPitch = std::sin(car->bodyPitch);
            const float y1 = eyeLocal.y * cRoll - eyeLocal.z * sRoll;
            const float z1 = eyeLocal.y * sRoll + eyeLocal.z * cRoll;
            const float x2 = eyeLocal.x * cPitch - y1 * sPitch;
            const float y2 = eyeLocal.x * sPitch + y1 * cPitch;
            const float z2 = z1;
            finalCamPos = {
                car->pos.x + carFwd.x * x2 + carRight.x * z2,
                car->posY + stanceOffsetY + y2,
                car->pos.z + carFwd.z * x2 + carRight.z * z2
            };
            targetFov = 65.0f + oc::clampf(car->speed() * 0.22f, 0.0f, 9.0f) +
                        car->turboSpool * 2.4f - car->shiftCutTimer * 7.5f +
                        (car->nitroActive ? 3.5f : 0.0f);
            if (!camInitialized) {
                smoothedFov = targetFov;
                camInitialized = true;
            }
        } else if (postFirstPerson) {
            // 1st-Person On-Foot FPS & Optical Sniper Scope: rock-steady horizon (zero head-bob)
            const auto& pl = world.player();
            renderYaw = camYaw;
            renderPitch = camPitch;
            viewDir = oc::directionFromYawPitch(renderYaw, renderPitch).normalized();

            const float desiredEyeY = pl.posY + 1.62f;
            const float baseHipFov = firstPersonMode ? 68.0f : 60.0f;
            targetFov = postSniperEquipped
                            ? oc::lerpf(baseHipFov, 24.0f, adsBlend)
                            : oc::lerpf(68.0f, 57.0f, adsBlend);
            if (!camInitialized || !pl.onGround) {
                smoothedEyeY = desiredEyeY;
                if (!camInitialized) {
                    smoothedFov = targetFov;
                    camInitialized = true;
                }
            } else {
                smoothedEyeY = oc::lerpf(smoothedEyeY, desiredEyeY, std::min(1.0f, frameDt * 20.0f));
            }
            finalCamPos = {pl.pos.x, smoothedEyeY, pl.pos.z};
        } else if (useStudioCharShowcase) {
            // Front-3/4 Character & Gunsmith Studio Showcase (framed on right half, clear of left Studio panel!)
            const auto& pl = world.player();
            const float orbitAng = oc::wrapAngle(camYaw + 0.44f);
            const oc::Vec2 oDir = oc::forwardFromHeading(orbitAng);
            const oc::Vec3 subj{pl.pos.x, pl.posY + 1.02f, pl.pos.z};
            finalCamPos = {subj.x + oDir.x * 2.92f, pl.posY + 1.32f, subj.z + oDir.z * 2.92f};

            const oc::Vec2 d2 = (oc::Vec2{subj.x - finalCamPos.x, subj.z - finalCamPos.z}).normalized();
            const oc::Vec2 scrRight{-d2.z, d2.x};
            const oc::Vec3 lookPt{subj.x - scrRight.x * 0.62f, subj.y, subj.z - scrRight.z * 0.62f};
            viewDir = (lookPt - finalCamPos).normalized();
            renderYaw = std::atan2(viewDir.z, viewDir.x);
            renderPitch = std::asin(oc::clampf(viewDir.y, -0.95f, 0.95f));
            targetFov = 46.0f;
            if (!camInitialized) {
                smoothedFov = targetFov;
                camInitialized = true;
            }
        } else if (useStudioCarShowcase) {
            // Showroom 3/4 Vehicle Custom Studio Camera (framed on right half, clear of left Studio panel!)
            const float orbitAng = oc::wrapAngle(camYaw + 0.64f);
            const oc::Vec2 oDir = oc::forwardFromHeading(orbitAng);
            const oc::Vec3 subj{studioTargetCar->pos.x, studioTargetCar->posY + 0.70f, studioTargetCar->pos.z};
            finalCamPos = {subj.x + oDir.x * 5.65f, studioTargetCar->posY + 1.52f, subj.z + oDir.z * 5.65f};

            const oc::Vec2 d2 = (oc::Vec2{subj.x - finalCamPos.x, subj.z - finalCamPos.z}).normalized();
            const oc::Vec2 scrRight{-d2.z, d2.x};
            const oc::Vec3 lookPt{subj.x - scrRight.x * 1.24f, subj.y, subj.z - scrRight.z * 1.24f};
            viewDir = (lookPt - finalCamPos).normalized();
            renderYaw = std::atan2(viewDir.z, viewDir.x);
            renderPitch = std::asin(oc::clampf(viewDir.y, -0.95f, 0.95f));
            targetFov = 48.0f;
            if (!camInitialized) {
                smoothedFov = targetFov;
                camInitialized = true;
            }
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
                rawAnchor = {car->pos.x, 1.48f + car->posY, car->pos.z};
                desiredDist = 7.8f + oc::clampf(car->speed() * 0.065f, 0.0f, 2.6f) + car->turboSpool * 0.35f;
                targetFov = 58.0f + oc::clampf(car->speed() * 0.28f, 0.0f, 12.0f) +
                            car->turboSpool * 2.8f - car->shiftCutTimer * 8.0f +
                            (car->nitroActive ? 4.0f : 0.0f);
            } else {
                const auto& pl = world.player();
                const float footElev = std::max(0.0f, pl.posY - 0.08f) * 0.92f;
                rawAnchor = {pl.pos.x, 1.54f + footElev, pl.pos.z};
                // Constant 0.40m right shoulder offset + gentle 0.70m ADS focus (eliminates dolly-zoom vertigo!)
                shoulderOff = oc::rightFromHeading(renderYaw) * 0.40f;
                desiredDist = oc::lerpf(4.60f, 3.90f, adsBlend);
                targetFov = postSniperEquipped
                                ? oc::lerpf(60.0f, 24.0f, adsBlend)
                                : oc::lerpf(60.0f, 54.0f, adsBlend);
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

        // Process synthesized audio events, vehicle engine/drift loops & distance-attenuated emergency sirens
        audio.playEvents(world.consumeAudioEvents());
        if (const oc::Vehicle* car = world.playerVehicle()) {
            const float speedRatio = oc::clampf(car->speed() / std::max(1.0f, oc::getVehicleSpec(car->kind).maxForwardSpeed), 0.0f, 1.0f);
            const float rpmRatio = oc::clampf((car->engineRpm - 880.0f) / 7270.0f, 0.0f, 1.0f);
            const float enginePitchRatio = oc::clampf(rpmRatio * 0.80f + speedRatio * 0.20f, 0.0f, 1.0f);
            audio.updateVehicleLoops(true, enginePitchRatio, car->isDrifting, car->nitroActive);
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

        const bool renderAsFirstPerson = firstPersonMode || postSniperScoped;

        // Render full 3D pipeline (Shadow Map -> Sky/PBR-lite City -> 1st-Person Viewmodel/Cockpit -> HDR Bloom + ACES)
        BeginDrawing();
        renderer.renderFrame(world, camera, renderAsFirstPerson, adsBlend, lookBack);
        hud.renderHud(world, renderYaw, cursorLocked, postFirstPerson, adsBlend);
        if (touch.isEnabled()) {
            const bool modalNow = hud.isShopMenuOpen() || hud.isPhoneOpen() || hud.isCustomStudioOpen();
            touch.render(
                world, GetScreenWidth(), GetScreenHeight(), modalNow, postFirstPerson, hud.isUsingKorean(),
                [&](const char* text, float x, float y, float fontSize, Color color) {
                    hud.drawUiText(text, x, y, fontSize, color);
                },
                [&](const char* text, float fontSize) {
                    return hud.measureUiText(text, fontSize);
                });
        }
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
