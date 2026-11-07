#include "ui/HudRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace oc {

namespace {

// Master Hangul glyph corpus covering all HUD, vehicle, weapon, shop, and mission strings
const char* kKoreanGlyphCorpus =
    "가각간갈감갑강개객거리격견결경계고골공과관광구국군굴권귀규균그극근글금급기긴길김"
    "나난날남납내낼냉너널네넥노녹논놀농뇌누눈뉴느늘능니닌닐님"
    "다단달담답당대댁더덕던덜덤도독돈돌동돼되된두둔둘뒤드득든들등디"
    "라락란랄람랑래랜램러럭런럴럼럽레렌로록론롤롱뢰루룰룸류륙륜률르른름리린림립링"
    "마막만말맘망매맥맨맵머먹먼멀멈메멘멜며면멸명모목몬몰몸무묵묶문물뭇므미민밀및"
    "바박반발밤밥방배백밴밸버번벌범법베벤벨벼벽변별병보복본볼봉부북분불붕브블비빈빌빔"
    "사삭산살삼삽상새색샌생서석선설섬섭성세센셀소속손솔송쇄쇠수숙순술숨쉬슈스슬슴습시식신실심십싱"
    "아악안알암압앙애액앤야약양어억언얼엄업에엔엘여역연열염엽영예오옥온올옮옵옹와완왕외요욕용우욱운울움워원월위유육윤율으은을음응의이익인일임입있잉"
    "자작잔잘잠잡장재쟁저적전절점접정제젠젤조족존졸종좌죄주죽준줄중즉즐즘증지직진질짐집징"
    "차착찬찰참창찾채책챔처척천철첨첩청체첸초촉촌총촬최추축춘출춤충취츠측층치칙친칠침칭"
    "카칸칼캄캐캠커컨컬컴컵케켄코콘콜콤콩쾌쿠쿨쿵쿼퀘퀴크큰클큼키킨킬킴"
    "타탁탄탈탐탑탕태택탠터턱턴털텀테텐텔토톡톤톨통퇴투툭툰툴퉁튀튜트특튼틀틈티틱틴틸팀팅"
    "파팍판팔팜팝팡패팩팬퍼퍽펀펄펌페펜펠펴편펼평폐포폭폰폴폼표푸푹푼풀품풍프픈플픔피픽핀필핍핑"
    "하학한할함합항해핵핸햄햇행향허헌험헤헬혀혁현혈혐협형혜호혹혼홀홈홍화확환활황회획횟효후훈훌훔훨휘휴흉흐흑흔흘흠흡흥희흰히힌힐힘"
    "★·—";

} // namespace

HudRenderer::~HudRenderer() {
    shutdown();
}

void HudRenderer::init() {
    const std::vector<const char*> koreanTtfCandidates = {
        "C:/Windows/Fonts/malgunbd.ttf",
        "C:/Windows/Fonts/malgun.ttf",
        "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"
    };

    std::vector<int> codepoints;
    for (int c = 32; c <= 126; ++c) {
        codepoints.push_back(c);
    }
    int extraCount = 0;
    int* extra = LoadCodepoints(kKoreanGlyphCorpus, &extraCount);
    for (int i = 0; i < extraCount; ++i) {
        codepoints.push_back(extra[i]);
    }
    UnloadCodepoints(extra);

    std::sort(codepoints.begin(), codepoints.end());
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());

    const Font defaultFont = GetFontDefault();

    for (const char* path : koreanTtfCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        Font candidate = LoadFontEx(path, 28, codepoints.data(), static_cast<int>(codepoints.size()));
        if (candidate.texture.id != 0 && candidate.texture.id != defaultFont.texture.id) {
            uiFont_ = candidate;
            SetTextureFilter(uiFont_.texture, TEXTURE_FILTER_BILINEAR);
            customFontLoaded_ = true;
            koreanFontLoaded_ = true;
            useKorean_ = true;
            return;
        }
    }

    // Crisp anti-aliased Latin TTF fallback if no Korean .ttf is installed
    useKorean_ = false;
    const std::vector<const char*> latinTtfCandidates = {
        "C:/Windows/Fonts/segoeuib.ttf",
        "/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF/Roboto-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
    };
    for (const char* path : latinTtfCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        Font candidate = LoadFontEx(path, 28, nullptr, 95);
        if (candidate.texture.id != 0 && candidate.texture.id != defaultFont.texture.id) {
            uiFont_ = candidate;
            SetTextureFilter(uiFont_.texture, TEXTURE_FILTER_BILINEAR);
            customFontLoaded_ = true;
            return;
        }
    }
}

void HudRenderer::shutdown() {
    if (customFontLoaded_ && uiFont_.texture.id != 0) {
        UnloadFont(uiFont_);
        uiFont_ = Font{};
        customFontLoaded_ = false;
        koreanFontLoaded_ = false;
    }
}

void HudRenderer::drawUiText(const char* text, float x, float y, float fontSize, Color color) const {
    if (customFontLoaded_) {
        DrawTextEx(uiFont_, text, {x + 1.0f, y + 1.0f}, fontSize, 0.6f, Color{0, 0, 0, static_cast<unsigned char>(color.a * 0.78f)});
        DrawTextEx(uiFont_, text, {x, y}, fontSize, 0.6f, color);
    } else {
        const int sz = std::max(10, static_cast<int>(fontSize));
        DrawText(text, static_cast<int>(x) + 1, static_cast<int>(y) + 1, sz, Color{0, 0, 0, static_cast<unsigned char>(color.a * 0.78f)});
        DrawText(text, static_cast<int>(x), static_cast<int>(y), sz, color);
    }
}

float HudRenderer::measureUiText(const char* text, float fontSize) const {
    if (customFontLoaded_) {
        return MeasureTextEx(uiFont_, text, fontSize, 0.6f).x;
    }
    return static_cast<float>(MeasureText(text, std::max(10, static_cast<int>(fontSize))));
}

void HudRenderer::renderHud(const GameWorld& world, float cameraYaw, bool cursorLocked) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    // Automatically close shop modal if player walked/drove away from shop zone
    if (shopMenuOpen_ && !world.isPlayerInWeaponShopZone() && !world.isPlayerInGarageZone()) {
        shopMenuOpen_ = false;
    }

    drawRadarMinimap(world, cameraYaw, sh);
    drawTopRightStatus(world, sw);
    drawSpeedometerAndDrift(world, sw, sh);
    drawCrosshair(world, sw, sh);
    drawContextPromptsAndMission(world, sw, sh);

    if (shopMenuOpen_) {
        drawShopModal(world, sw, sh);
    }

    drawBannerToast(world, sw, sh);
    drawHelpOverlay(sw, sh, cursorLocked);
}

void HudRenderer::drawRadarMinimap(const GameWorld& world, float cameraYaw, int screenH) {
    const float radius = 92.0f;
    const float cx = 26.0f + radius;
    const float cy = static_cast<float>(screenH) - 42.0f - radius;
    const float worldRange = 145.0f;
    const float scale = radius / worldRange;

    const Vec2 centerWorld = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
    const Vec2 fwd = forwardFromHeading(cameraYaw);
    const Vec2 right = rightFromHeading(cameraYaw);

    auto projectToRadar = [&](const Vec2& wpos, bool clampEdge) -> Vector2 {
        const Vec2 d = wpos - centerWorld;
        float rx = d.dot(right) * scale;
        float ry = -d.dot(fwd) * scale;
        const float len = std::sqrt(rx * rx + ry * ry);
        if (len > radius - 5.0f) {
            if (!clampEdge) return {-9999.0f, -9999.0f};
            const float k = (radius - 6.0f) / len;
            rx *= k;
            ry *= k;
        }
        return {cx + rx, cy + ry};
    };

    // Outer dark bezel + radar disc
    DrawCircleV({cx, cy}, radius + 5.0f, Color{12, 16, 22, 230});
    DrawCircleV({cx, cy}, radius, Color{26, 34, 44, 225});

    // Nearby buildings on radar
    for (const auto& b : world.city().buildings()) {
        const Vec2 bc = b.footprint.center();
        if ((bc - centerWorld).lengthSq() > (worldRange + 25.0f) * (worldRange + 25.0f)) continue;
        const Vector2 p = projectToRadar(bc, false);
        if (p.x < -1000.0f) continue;
        const float bw = std::max(3.0f, b.footprint.halfExtents().x * scale * 1.4f);
        DrawRectanglePro({p.x, p.y, bw, bw}, {bw * 0.5f, bw * 0.5f},
                         -cameraYaw * kRadToDeg, Color{58, 70, 86, 210});
    }

    // Road grid lines clipped inside radar disc
    for (int i = 0; i < CityMap::kRoadCount; ++i) {
        const float rc = world.city().roadCoord(i);
        for (float s = -320.0f; s <= 320.0f; s += 16.0f) {
            const Vector2 pv = projectToRadar({rc, s}, false);
            if (pv.x > -1000.0f) DrawCircleV(pv, 2.4f, Color{88, 98, 112, 190});
            const Vector2 ph = projectToRadar({s, rc}, false);
            if (ph.x > -1000.0f) DrawCircleV(ph, 2.4f, Color{88, 98, 112, 190});
        }
    }

    // Nearby vehicles & gang enemies
    for (const auto& v : world.vehicles()) {
        if (v.isPlayerDriving || v.destroyed) continue;
        const Vector2 pv = projectToRadar(v.pos, false);
        if (pv.x > -1000.0f) {
            const Color col = v.isMissionTarget ? Color{255, 218, 52, 255} : Color{185, 200, 215, 210};
            DrawCircleV(pv, v.isMissionTarget ? 4.8f : 2.5f, col);
        }
    }

    for (const auto& npc : world.npcs()) {
        if (!npc.alive || npc.role != NpcRole::Gangster) continue;
        const Vector2 pn = projectToRadar(npc.pos, false);
        if (pn.x > -1000.0f) {
            DrawCircleV(pn, 3.3f, Color{255, 72, 68, 245});
        }
    }

    // POIs (Weapon Shop, Garage, Mission Coronas)
    for (const auto& poi : world.city().pois()) {
        if (world.mission().kind != ActiveMissionKind::None &&
            (poi.kind == PoiKind::MissionDelivery ||
             poi.kind == PoiKind::MissionRace ||
             poi.kind == PoiKind::MissionGangRaid)) {
            continue;
        }
        const bool clamp = (poi.kind == PoiKind::WeaponShop || poi.kind == PoiKind::TuneGarage);
        const Vector2 pp = projectToRadar(poi.pos, clamp);
        if (pp.x > -1000.0f) {
            const Color pc{poi.markerColor.r, poi.markerColor.g, poi.markerColor.b, 255};
            DrawCircleV(pp, 4.6f, Color{12, 14, 18, 255});
            DrawCircleV(pp, 3.4f, pc);
        }
    }

    // Active Mission GPS line + yellow objective blip
    if (world.mission().kind != ActiveMissionKind::None) {
        const Vector2 objP = projectToRadar(world.mission().currentObjectivePos, true);
        DrawLineEx({cx, cy}, objP, 2.5f, Color{255, 218, 56, 210});
        DrawCircleV(objP, 6.2f, Color{18, 18, 18, 255});
        DrawCircleV(objP, 4.6f, Color{255, 224, 58, 255});
    }

    // Player center arrow
    DrawTriangle({cx, cy - 6.5f}, {cx - 5.0f, cy + 5.5f}, {cx + 5.0f, cy + 5.5f}, Color{255, 255, 255, 255});

    // Radar ring border + North [N] compass badge
    DrawRing({cx, cy}, radius, radius + 3.5f, 0.0f, 360.0f, 48, Color{210, 220, 232, 235});

    const Vec2 northWorld{0.0f, -1.0f};
    const float nx = cx + northWorld.dot(right) * (radius + 1.0f);
    const float ny = cy - northWorld.dot(fwd) * (radius + 1.0f);
    DrawCircleV({nx, ny}, 9.5f, Color{22, 26, 34, 245});
    drawUiText("N", nx - 5.0f, ny - 8.0f, 16.0f, Color{255, 96, 88, 255});

    // GTA Health (green) & Armor (blue) bars directly below radar
    const float barW = (radius * 2.0f - 6.0f) * 0.5f;
    const float barY = cy + radius + 12.0f;
    const float leftX = cx - radius;

    DrawRectangleRounded({leftX, barY, barW, 11.0f}, 0.35f, 4, Color{22, 28, 34, 225});
    const float hpFrac = clampf(world.player().health / world.player().maxHealth, 0.0f, 1.0f);
    if (hpFrac > 0.0f) {
        DrawRectangleRounded({leftX + 1.5f, barY + 1.5f, (barW - 3.0f) * hpFrac, 8.0f}, 0.35f, 4, Color{72, 222, 106, 255});
    }

    const float armorX = leftX + barW + 6.0f;
    DrawRectangleRounded({armorX, barY, barW, 11.0f}, 0.35f, 4, Color{22, 28, 34, 225});
    const float arFrac = clampf(world.player().armor / world.player().maxArmor, 0.0f, 1.0f);
    if (arFrac > 0.0f) {
        DrawRectangleRounded({armorX + 1.5f, barY + 1.5f, (barW - 3.0f) * arFrac, 8.0f}, 0.35f, 4, Color{68, 178, 255, 255});
    }
}

void HudRenderer::drawTopRightStatus(const GameWorld& world, int screenW) {
    const float rightEdge = static_cast<float>(screenW) - 24.0f;

    // Time of day + District card
    const int hour = static_cast<int>(world.timeOfDayHours()) % 24;
    const int minute = static_cast<int>((world.timeOfDayHours() - std::floor(world.timeOfDayHours())) * 60.0f);
    const Vec2 pPos = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
    const DistrictType dist = world.city().districtAtWorld(pPos);

    const char* distName = "MIDTOWN";
    switch (dist) {
        case DistrictType::Downtown:           distName = pickText("DOWNTOWN", "다운타운 금융가"); break;
        case DistrictType::Midtown:            distName = pickText("MIDTOWN", "미드타운 상업지구"); break;
        case DistrictType::Industrial:         distName = pickText("HARBOR DOCKS", "항구 공업단지"); break;
        case DistrictType::CoastalResidential: distName = pickText("SUNSET BEACH", "선셋 해변지구"); break;
        case DistrictType::CentralPark:        distName = pickText("CENTRAL PARK", "센트럴 파크"); break;
    }

    char clockBuf[96];
    std::snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d  |  %s", hour, minute, distName);
    const float clockW = measureUiText(clockBuf, 19.0f);
    drawUiText(clockBuf, rightEdge - clockW, 18.0f, 19.0f, Color{225, 232, 242, 245});

    // Classic GTA green cash counter
    char cashBuf[48];
    std::snprintf(cashBuf, sizeof(cashBuf), "$%08d", world.player().cash);
    const float cashW = measureUiText(cashBuf, 32.0f);
    drawUiText(cashBuf, rightEdge - cashW, 44.0f, 32.0f, Color{92, 232, 112, 255});

    // Equipped weapon & ammo card
    const auto& pl = world.player();
    const auto& wspec = getWeaponSpec(pl.currentWeapon);
    const auto& wslot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];

    DrawRectangleRounded({rightEdge - 220.0f, 84.0f, 220.0f, 54.0f}, 0.22f, 6, Color{16, 22, 30, 205});
    const char* wName = pickText(wspec.nameEn, wspec.nameKo);
    drawUiText(wName, rightEdge - 206.0f, 90.0f, 19.0f, Color{242, 246, 252, 255});

    char ammoBuf[64];
    if (pl.reloadTimer > 0.0f) {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "%s", pickText("RELOADING...", "재장전 중..."));
    } else if (wspec.magazineSize > 0) {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "%d / %d", wslot.magAmmo, wslot.reserveAmmo);
    } else {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "MELEE [1-5]");
    }
    drawUiText(ammoBuf, rightEdge - 206.0f, 112.0f, 18.0f, Color{255, 214, 92, 255});
}

void HudRenderer::drawSpeedometerAndDrift(const GameWorld& world, int screenW, int screenH) {
    const Vehicle* car = world.playerVehicle();
    if (!car) return;

    const auto& spec = getVehicleSpec(car->kind);
    const float rx = static_cast<float>(screenW) - 246.0f;
    const float ry = static_cast<float>(screenH) - 134.0f;

    DrawRectangleRounded({rx, ry, 222.0f, 108.0f}, 0.20f, 8, Color{14, 18, 26, 220});

    const char* vName = pickText(spec.nameEn, spec.nameKo);
    drawUiText(vName, rx + 14.0f, ry + 10.0f, 18.0f, Color{168, 215, 255, 255});

    const int kmh = static_cast<int>(std::round(car->speed() * 3.6f));
    char spdBuf[32];
    std::snprintf(spdBuf, sizeof(spdBuf), "%3d", kmh);
    drawUiText(spdBuf, rx + 14.0f, ry + 32.0f, 36.0f,
               car->nitroActive ? Color{92, 224, 255, 255} : Color{255, 255, 255, 255});
    drawUiText("KM/H", rx + 92.0f, ry + 48.0f, 17.0f, Color{175, 185, 198, 255});

    // Nitro & Vehicle Integrity bars
    drawUiText("NITRO", rx + 14.0f, ry + 74.0f, 14.0f, Color{118, 212, 255, 255});
    DrawRectangleRounded({rx + 68.0f, ry + 77.0f, 138.0f, 8.0f}, 0.4f, 4, Color{32, 40, 52, 255});
    DrawRectangleRounded({rx + 68.0f, ry + 77.0f, 138.0f * clampf(car->nitroCharge, 0.0f, 1.0f), 8.0f},
                         0.4f, 4, Color{68, 205, 255, 255});

    drawUiText("BODY", rx + 14.0f, ry + 90.0f, 14.0f, Color{235, 196, 98, 255});
    DrawRectangleRounded({rx + 68.0f, ry + 93.0f, 138.0f, 8.0f}, 0.4f, 4, Color{32, 40, 52, 255});
    const float hpFrac = clampf(car->health / car->maxHealth, 0.0f, 1.0f);
    const Color hpCol = (hpFrac < 0.35f) ? Color{245, 76, 68, 255} : Color{98, 232, 132, 255};
    DrawRectangleRounded({rx + 68.0f, ry + 93.0f, 138.0f * hpFrac, 8.0f}, 0.4f, 4, hpCol);

    // Live Drift Combo Meter!
    if (world.player().activeDriftScore > 8.0f) {
        char driftBuf[64];
        std::snprintf(driftBuf, sizeof(driftBuf), "DRIFT  %d PTS",
                      static_cast<int>(world.player().activeDriftScore));
        const float dw = measureUiText(driftBuf, 24.0f);
        drawUiText(driftBuf, rx + 111.0f - dw * 0.5f, ry - 34.0f, 24.0f, Color{255, 208, 56, 255});
    }
}

void HudRenderer::drawCrosshair(const GameWorld& world, int screenW, int screenH) {
    if (world.player().drivingVehicleId >= 0 || shopMenuOpen_) return;

    // Exact optical center of the 3D perspective projection
    const float cx = static_cast<float>(screenW) * 0.5f;
    const float cy = static_cast<float>(screenH) * 0.5f;

    const auto& pl = world.player();
    const bool precisionAim = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    const float moveSpread = clampf(pl.velocity.length() * 0.32f, 0.0f, 2.8f);
    const float shotSpread = (pl.muzzleFlashTimer > 0.0f) ? 2.6f : 0.0f;
    const float gap = (precisionAim ? 3.4f : 5.4f) + moveSpread + shotSpread;
    const float armLen = precisionAim ? 6.2f : 7.4f;

    const Color shadowCol{10, 14, 20, 175};
    const Color col = pl.crosshairOnTarget ? Color{255, 78, 72, 245} : Color{245, 250, 255, 230};

    // Subtle dark backing lines for high contrast against bright sky & sunlit walls
    DrawCircleV({cx, cy}, 2.6f, shadowCol);
    DrawLineEx({cx - gap - armLen - 1.0f, cy}, {cx - gap + 1.0f, cy}, 3.2f, shadowCol);
    DrawLineEx({cx + gap - 1.0f, cy}, {cx + gap + armLen + 1.0f, cy}, 3.2f, shadowCol);
    DrawLineEx({cx, cy - gap - armLen - 1.0f}, {cx, cy - gap + 1.0f}, 3.2f, shadowCol);
    DrawLineEx({cx, cy + gap - 1.0f}, {cx, cy + gap + armLen + 1.0f}, 3.2f, shadowCol);

    DrawCircleV({cx, cy}, 1.9f, col);
    DrawLineEx({cx - gap - armLen, cy}, {cx - gap, cy}, 1.8f, col);
    DrawLineEx({cx + gap, cy}, {cx + gap + armLen, cy}, 1.8f, col);
    DrawLineEx({cx, cy - gap - armLen}, {cx, cy - gap}, 1.8f, col);
    DrawLineEx({cx, cy + gap}, {cx, cy + gap + armLen}, 1.8f, col);

    // Tactical 'X' hitmarker feedback (gold-crimson on critical headshot!)
    if (pl.hitMarkerTimer > 0.0f) {
        const float alpha = clampf(pl.hitMarkerTimer / 0.16f, 0.0f, 1.0f);
        const unsigned char aByte = static_cast<unsigned char>(alpha * 255.0f);
        const Color hmCol = pl.hitMarkerCritical ? Color{255, 196, 52, aByte} : Color{255, 255, 255, aByte};
        const float r0 = 4.5f;
        const float r1 = pl.hitMarkerCritical ? 11.5f : 9.5f;
        DrawLineEx({cx - r1, cy - r1}, {cx - r0, cy - r0}, 2.2f, hmCol);
        DrawLineEx({cx + r0, cy - r0}, {cx + r1, cy - r1}, 2.2f, hmCol);
        DrawLineEx({cx - r1, cy + r1}, {cx - r0, cy + r0}, 2.2f, hmCol);
        DrawLineEx({cx + r0, cy + r0}, {cx + r1, cy + r1}, 2.2f, hmCol);
    }
}

void HudRenderer::drawContextPromptsAndMission(const GameWorld& world, int screenW, int screenH) {
    const float midX = static_cast<float>(screenW) * 0.5f;

    // Active Mission top-center objective bar
    if (world.mission().kind != ActiveMissionKind::None) {
        const auto& m = world.mission();
        DrawRectangleRounded({midX - 270.0f, 16.0f, 540.0f, 58.0f}, 0.22f, 8, Color{14, 18, 26, 225});

        const char* objText = pickText(m.objectiveEn.c_str(), m.objectiveKo.c_str());
        const float ow = measureUiText(objText, 20.0f);
        drawUiText(objText, midX - ow * 0.5f, 22.0f, 20.0f, Color{255, 224, 78, 255});

        const int sec = std::max(0, static_cast<int>(std::ceil(m.timeRemaining)));
        char timerBuf[96];
        std::snprintf(timerBuf, sizeof(timerBuf), "%s %02d:%02d   |   [Backspace] %s",
                      pickText("TIME", "남은 시간"), sec / 60, sec % 60,
                      pickText("Abort", "미션 취소"));
        const float tw = measureUiText(timerBuf, 17.0f);
        const Color tCol = (sec <= 15) ? Color{255, 88, 82, 255} : Color{210, 220, 232, 245};
        drawUiText(timerBuf, midX - tw * 0.5f, 48.0f, 17.0f, tCol);
    }

    // Bottom-center interaction prompt (Shop / Mission Corona / Vehicle Enter)
    std::string prompt;
    if (!shopMenuOpen_) {
        if (world.isPlayerInWeaponShopZone()) {
            prompt = pickText("Press [B] to open Ammu-Tech Weapon & Armor Shop",
                              "[B] 키를 눌러 무기 & 방탄복 상점 열기");
        } else if (world.isPlayerInGarageZone()) {
            prompt = pickText("Press [B] to open Pay 'n' Tune Garage",
                              "[B] 키를 눌러 차량 수리 & 커스텀 튜닝 차고 열기");
        } else if (const auto* poi = world.activePoiNearPlayer()) {
            if (world.mission().kind == ActiveMissionKind::None &&
                (poi->kind == PoiKind::MissionDelivery ||
                 poi->kind == PoiKind::MissionRace ||
                 poi->kind == PoiKind::MissionGangRaid)) {
                char buf[196];
                std::snprintf(buf, sizeof(buf), "[F] %s",
                              pickText(poi->nameEn.c_str(), poi->nameKo.c_str()));
                prompt = buf;
            }
        } else if (const Vehicle* nearCar = world.nearestEnterableVehicle()) {
            const auto& spec = getVehicleSpec(nearCar->kind);
            char buf[196];
            if (nearCar->hasNpcDriver) {
                std::snprintf(buf, sizeof(buf), "[F] %s (%s)",
                              pickText("Carjack Vehicle", "주행 차량 탈취하기"),
                              pickText(spec.nameEn, spec.nameKo));
            } else {
                std::snprintf(buf, sizeof(buf), "[F] %s (%s)",
                              pickText("Drive Vehicle", "차량 탑승하기"),
                              pickText(spec.nameEn, spec.nameKo));
            }
            prompt = buf;
        }
    }

    if (!prompt.empty()) {
        const float pw = measureUiText(prompt.c_str(), 21.0f);
        const float py = static_cast<float>(screenH) - 62.0f;
        DrawRectangleRounded({midX - pw * 0.5f - 18.0f, py - 8.0f, pw + 36.0f, 38.0f},
                             0.30f, 6, Color{12, 16, 24, 225});
        drawUiText(prompt.c_str(), midX - pw * 0.5f, py, 21.0f, Color{255, 242, 152, 255});
    }
}

void HudRenderer::drawShopModal(const GameWorld& world, int screenW, int screenH) {
    const bool weaponShop = world.isPlayerInWeaponShopZone();
    const bool garageShop = world.isPlayerInGarageZone();
    if (!weaponShop && !garageShop) return;

    const float mw = 460.0f;
    const float mh = 320.0f;
    const float mx = (static_cast<float>(screenW) - mw) * 0.5f;
    const float my = (static_cast<float>(screenH) - mh) * 0.5f;

    DrawRectangleRounded({mx, my, mw, mh}, 0.08f, 8, Color{14, 20, 30, 242});
    DrawRectangleRoundedLinesEx({mx, my, mw, mh}, 0.08f, 8, 2.5f,
                                weaponShop ? Color{255, 102, 68, 255} : Color{58, 216, 255, 255});

    const char* title = weaponShop
                            ? pickText("AMMU-TECH WEAPON & ARMOR", "AMMU-TECH 무기 & 방탄복 상점")
                            : pickText("PAY 'N' TUNE CUSTOM GARAGE", "PAY 'N' TUNE 차량 수리 & 튜닝");
    drawUiText(title, mx + 22.0f, my + 16.0f, 24.0f,
               weaponShop ? Color{255, 132, 92, 255} : Color{88, 226, 255, 255});

    struct Row { const char* en; const char* ko; const char* price; };
    const Row weaponRows[6] = {
        {"[1] Tactical SMG (+120 Ammo)",  "[1] 전술 기관단총 (+120발)",   "$350"},
        {"[2] Pump Shotgun (+32 Shells)", "[2] 펌프 샷건 (+32발)",        "$500"},
        {"[3] Carbine Rifle (+150 Ammo)", "[3] 돌격 카빈소총 (+150발)",   "$850"},
        {"[4] Full Ammo Pack (+4 Mags)",  "[4] 보유 무기 전체 탄약 보충", "$150"},
        {"[5] Heavy Body Armor (100%)",   "[5] 중형 방탄복 100% 장착",    "$200"},
        {"[6] Emergency Medkit (100%)",   "[6] 응급 회복 키트 (체력 100%)", "$100"}
    };
    const Row garageRows[6] = {
        {"[1] Full Chassis & Engine Repair", "[1] 차체 및 엔진 100% 수리",      "$120"},
        {"[2] Turbo Engine Upgrade (Lv+1)",  "[2] 터보 엔진 업그레이드 (+속도)", "$450"},
        {"[3] Respray: Neon Crimson",        "[3] 커스텀 도색: 네온 크림슨",     "$100"},
        {"[4] Respray: Cyber Cyan",          "[4] 커스텀 도색: 사이버 시안",     "$100"},
        {"[5] Respray: Midnight Gold",       "[5] 커스텀 도색: 미드나잇 골드",   "$100"},
        {"[6] Nitrous Oxide Tank Refill",    "[6] 니트로 부스트 100% 충전",      "$80"}
    };

    const Row* rows = weaponShop ? weaponRows : garageRows;
    for (int i = 0; i < 6; ++i) {
        const float ry = my + 62.0f + static_cast<float>(i) * 35.0f;
        DrawRectangleRounded({mx + 18.0f, ry, mw - 36.0f, 30.0f}, 0.25f, 4, Color{26, 34, 48, 225});
        drawUiText(pickText(rows[i].en, rows[i].ko), mx + 28.0f, ry + 5.0f, 18.0f, Color{236, 242, 248, 255});
        const float pw = measureUiText(rows[i].price, 18.0f);
        drawUiText(rows[i].price, mx + mw - 28.0f - pw, ry + 5.0f, 18.0f, Color{102, 238, 122, 255});
    }

    const char* closeHint = pickText("Press [1-6] to purchase   |   [B] Close Menu",
                                     "숫자키 [1~6] 구매   |   [B] 상점 닫기");
    drawUiText(closeHint, mx + 22.0f, my + mh - 32.0f, 17.0f, Color{180, 192, 208, 255});
}

void HudRenderer::drawBannerToast(const GameWorld& world, int screenW, int screenH) {
    const auto& b = world.banner();
    if (b.timer <= 0.0f) return;

    const float alpha = clampf(b.timer * 1.8f, 0.0f, 1.0f);
    const unsigned char aByte = static_cast<unsigned char>(alpha * 255.0f);
    const float midX = static_cast<float>(screenW) * 0.5f;
    const float y = static_cast<float>(screenH) * 0.17f;

    const char* title = pickText(b.titleEn.c_str(), b.titleKo.c_str());
    const float tw = measureUiText(title, 34.0f);
    drawUiText(title, midX - tw * 0.5f, y, 34.0f,
               Color{b.accent.r, b.accent.g, b.accent.b, aByte});

    const char* sub = pickText(b.subEn.c_str(), b.subKo.c_str());
    if (sub && sub[0] != '\0') {
        const float sw = measureUiText(sub, 20.0f);
        drawUiText(sub, midX - sw * 0.5f, y + 40.0f, 20.0f, Color{240, 244, 250, aByte});
    }
}

void HudRenderer::drawHelpOverlay(int screenW, int screenH, bool cursorLocked) {
    (void)screenW;
    (void)screenH;
    if (!showHelp_) {
        drawUiText(pickText("[H] Controls", "[H] 조작키 도움말"), 20.0f, 18.0f, 17.0f, Color{200, 210, 225, 200});
        return;
    }

    DrawRectangleRounded({18.0f, 16.0f, 386.0f, 228.0f}, 0.08f, 6, Color{12, 16, 24, 198});
    drawUiText(pickText("OPEN CITY C++ - CONTROLS", "OPEN CITY C++ - 조작 안내"),
               30.0f, 24.0f, 19.0f, Color{98, 220, 255, 255});

    const char* linesEn[8] = {
        "WASD : Walk / Drive & Steer",
        "Mouse : Camera / Aim (RMB) & Shoot (LMB)",
        "F or E : Enter / Carjack / Exit / Start Mission",
        "Space : Handbrake Drift   |   Shift : Sprint / Nitro",
        "1-5 : Switch Weapons   |   R : Reload   |   G : Horn",
        "B : Weapon Shop / Tune Garage Menu",
        "T : Skip Time (+3h)  |  C : Rear View  |  I : Invert Y",
        "Tab : Mouse Lock   |   L : Lang   |   F11 : Full   |   H : Hide"
    };
    const char* linesKo[8] = {
        "WASD : 이동 / 차량 주행 및 조향",
        "마우스 : 상하좌우 자유 시점 / 우클릭 조준 · 좌클릭 사격",
        "F / E : 차량 탑승·탈취·하차 / 미션 시작",
        "Space : 핸드브레이크 드리프트  |  Shift : 달리기·니트로",
        "1~5 : 무기 교체   |   R : 재장전   |   G : 경적",
        "B : 무기 상점 / 튜닝 차고 메뉴 열기",
        "T : 낮/밤 변경 (+3시간) | C : 후방 시점 | I : 상하반전",
        "Tab : 마우스 잠금  |  L : 한/영 전환  |  H : 안내 숨김"
    };

    for (int i = 0; i < 8; ++i) {
        drawUiText(pickText(linesEn[i], linesKo[i]),
                   30.0f, 52.0f + static_cast<float>(i) * 22.0f, 16.0f, Color{226, 232, 242, 240});
    }

    if (!cursorLocked) {
        drawUiText(pickText("Click window or press [Tab] to lock mouse camera",
                            "화면을 클릭하거나 [Tab]을 누르면 마우스 시점이 활성화됩니다"),
                   30.0f, 246.0f, 17.0f, Color{255, 214, 88, 255});
    }
}

} // namespace oc
