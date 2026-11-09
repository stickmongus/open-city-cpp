#include "ui/HudRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace oc {

namespace {

const char* neonPresetNameEn(int preset) {
    switch (preset) {
        case 1: return "Cyber Cyan";
        case 2: return "UV Purple";
        case 3: return "Crimson Red";
        case 4: return "Emerald";
        case 5: return "Solar Gold";
        default: return "OFF";
    }
}

const char* neonPresetNameKo(int preset) {
    switch (preset) {
        case 1: return "사이버 시안";
        case 2: return "네온 퍼플";
        case 3: return "크림슨 레드";
        case 4: return "에메랄드";
        case 5: return "솔라 골드";
        default: return "꺼짐";
    }
}

const char* rimStyleNameEn(int rimStyle) {
    switch (rimStyle) {
        case 1: return "Gold";
        case 2: return "Gloss Black";
        case 3: return "Cyber Cyan";
        case 4: return "Crimson Track";
        default: return "Silver Alloy";
    }
}

const char* rimStyleNameKo(int rimStyle) {
    switch (rimStyle) {
        case 1: return "샴페인 골드";
        case 2: return "글로스 블랙";
        case 3: return "사이버 시안";
        case 4: return "크림슨 레드";
        default: return "실버 알로이";
    }
}

Color toRayColor(ColorRGBA c, unsigned char alphaOverride = 255) {
    return Color{c.r, c.g, c.b, alphaOverride};
}

void drawFivePointStar2D(Vector2 center, float outerR, float innerR, Color col) {
    Vector2 pts[10];
    for (int k = 0; k < 10; ++k) {
        // Decreasing angle in screen space (+Y down) produces CCW triangles for raylib DrawTriangle
        const float ang = -kHalfPi - static_cast<float>(k) * (kPi / 5.0f);
        const float r = (k % 2 == 0) ? outerR : innerR;
        pts[k] = {center.x + std::cos(ang) * r, center.y + std::sin(ang) * r};
    }
    for (int k = 0; k < 10; ++k) {
        DrawTriangle(center, pts[k], pts[(k + 1) % 10], col);
    }
}

} // namespace

HudRenderer::~HudRenderer() {
    shutdown();
}

Font HudRenderer::loadCompleteKoreanFont(const char* ttfPath, int fontSize) {
    int dataSize = 0;
    unsigned char* fileData = LoadFileData(ttfPath, &dataSize);
    if (!fileData || dataSize <= 0) {
        if (fileData) UnloadFileData(fileData);
        return Font{};
    }

    // If the font file is a TrueType/OpenType Collection (.ttc, magic "ttcf"),
    // promote its SFNT OffsetTable to byte 0 in-memory (all table record offsets
    // in a .ttc are already relative to byte 0 of the file).
    if (dataSize > 16 &&
        fileData[0] == 't' && fileData[1] == 't' && fileData[2] == 'c' && fileData[3] == 'f') {
        auto readU32BE = [&](std::size_t off) -> std::uint32_t {
            return (static_cast<std::uint32_t>(fileData[off]) << 24U) |
                   (static_cast<std::uint32_t>(fileData[off + 1]) << 16U) |
                   (static_cast<std::uint32_t>(fileData[off + 2]) << 8U) |
                   static_cast<std::uint32_t>(fileData[off + 3]);
        };
        auto readU16BE = [&](std::size_t off) -> std::uint16_t {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(fileData[off]) << 8U) |
                static_cast<std::uint16_t>(fileData[off + 1]));
        };
        const std::uint32_t numFonts = readU32BE(8);
        if (numFonts > 0 && 12ULL + static_cast<std::size_t>(numFonts) * 4ULL <= static_cast<std::size_t>(dataSize)) {
            const std::uint32_t subIdx = (numFonts > 1U) ? 1U : 0U; // Index 1 = KR in NotoSansCJK.ttc
            const std::uint32_t sfntOff = readU32BE(12ULL + static_cast<std::size_t>(subIdx) * 4ULL);
            if (static_cast<std::size_t>(sfntOff) + 12ULL <= static_cast<std::size_t>(dataSize)) {
                const std::uint16_t numTables = readU16BE(static_cast<std::size_t>(sfntOff) + 4ULL);
                const std::size_t dirBytes = 12ULL + static_cast<std::size_t>(numTables) * 16ULL;
                if (static_cast<std::size_t>(sfntOff) + dirBytes <= static_cast<std::size_t>(dataSize)) {
                    std::uint32_t minTableOff = 0xFFFFFFFFU;
                    for (std::uint16_t t = 0; t < numTables; ++t) {
                        const std::uint32_t tOff = readU32BE(
                            static_cast<std::size_t>(sfntOff) + 12ULL + static_cast<std::size_t>(t) * 16ULL + 8ULL);
                        if (tOff < minTableOff) minTableOff = tOff;
                    }
                    if (minTableOff >= dirBytes) {
                        std::memmove(fileData, fileData + sfntOff, dirBytes);
                    }
                }
            }
        }
    }

    // Complete codepoint table:
    // - Printable ASCII (0x0020..0x007E)
    // - Hangul Compatibility Jamo (0x3131..0x3163)
    // - ALL 11,172 Modern Hangul Syllables (0xAC00..0xD7A3) so zero Korean text can ever show '?'
    // - Common UI punctuation & symbols
    std::vector<int> codepoints;
    codepoints.reserve(11400);
    for (int cp = 0x0020; cp <= 0x007E; ++cp) {
        codepoints.push_back(cp);
    }
    const int uiSymbols[] = {
        0x00B0, 0x00B7, 0x00D7, 0x2013, 0x2014, 0x2022, 0x2026,
        0x20A9, 0x2190, 0x2191, 0x2192, 0x2193,
        0x25B2, 0x25BC, 0x25C0, 0x25B6, 0x2605, 0x2606
    };
    for (int cp : uiSymbols) {
        codepoints.push_back(cp);
    }
    for (int cp = 0x3131; cp <= 0x3163; ++cp) {
        codepoints.push_back(cp);
    }
    for (int cp = 0xAC00; cp <= 0xD7A3; ++cp) {
        codepoints.push_back(cp);
    }

    std::sort(codepoints.begin(), codepoints.end());
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
    const int count = static_cast<int>(codepoints.size());

    GlyphInfo* glyphs = LoadFontData(fileData, dataSize, fontSize, codepoints.data(), count, FONT_DEFAULT);
    UnloadFileData(fileData);
    if (!glyphs) {
        return Font{};
    }

    // Verify that the candidate TTF genuinely contains Hangul glyphs (e.g. '가', '료', '힣')
    auto hasRenderedGlyph = [&](int targetCp) -> bool {
        auto it = std::lower_bound(codepoints.begin(), codepoints.end(), targetCp);
        if (it == codepoints.end() || *it != targetCp) return false;
        const std::size_t idx = static_cast<std::size_t>(it - codepoints.begin());
        return glyphs[idx].image.data != nullptr && glyphs[idx].image.width > 0 && glyphs[idx].image.height > 0;
    };
    if (!hasRenderedGlyph(0xAC00) || !hasRenderedGlyph(0xB8CC) || !hasRenderedGlyph(0xD7A3)) {
        UnloadFontData(glyphs, count);
        return Font{};
    }

    // Overflow-proof 2-pass exact row packer (avoids raylib GenImageFontAtlas height-halving overflow)
    constexpr int kAtlasWidth = 4096;
    constexpr int kPadding = 2;

    int curX = kPadding;
    int curY = kPadding;
    int rowMaxH = 0;
    for (int i = 0; i < count; ++i) {
        const int gw = glyphs[i].image.width;
        const int gh = glyphs[i].image.height;
        if (gw <= 0 || gh <= 0 || glyphs[i].image.data == nullptr) continue;
        if (curX + gw + kPadding > kAtlasWidth) {
            curX = kPadding;
            curY += rowMaxH + kPadding * 2;
            rowMaxH = 0;
        }
        curX += gw + kPadding * 2;
        if (gh > rowMaxH) rowMaxH = gh;
    }

    const int usedHeight = curY + rowMaxH + kPadding * 2;
    int atlasHeight = 512;
    while (atlasHeight < usedHeight) {
        atlasHeight *= 2;
    }

    auto* recs = static_cast<Rectangle*>(
        MemAlloc(static_cast<unsigned int>(static_cast<std::size_t>(count) * sizeof(Rectangle))));
    const std::size_t pixelCount = static_cast<std::size_t>(kAtlasWidth) * static_cast<std::size_t>(atlasHeight);
    auto* atlasBytes = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(pixelCount * 2U)));
    if (!recs || !atlasBytes) {
        if (recs) MemFree(recs);
        if (atlasBytes) MemFree(atlasBytes);
        UnloadFontData(glyphs, count);
        return Font{};
    }

    for (std::size_t p = 0; p < pixelCount; ++p) {
        atlasBytes[p * 2U] = 255;
        atlasBytes[p * 2U + 1U] = 0;
    }

    curX = kPadding;
    curY = kPadding;
    rowMaxH = 0;
    for (int i = 0; i < count; ++i) {
        const int gw = glyphs[i].image.width;
        const int gh = glyphs[i].image.height;
        if (gw <= 0 || gh <= 0 || glyphs[i].image.data == nullptr) {
            recs[i] = Rectangle{static_cast<float>(kPadding), static_cast<float>(kPadding), 0.0f, 0.0f};
            continue;
        }
        if (curX + gw + kPadding > kAtlasWidth) {
            curX = kPadding;
            curY += rowMaxH + kPadding * 2;
            rowMaxH = 0;
        }

        const auto* src = static_cast<const unsigned char*>(glyphs[i].image.data);
        for (int y = 0; y < gh; ++y) {
            unsigned char* dstRow = atlasBytes +
                (static_cast<std::size_t>(curY + y) * static_cast<std::size_t>(kAtlasWidth) +
                 static_cast<std::size_t>(curX)) * 2U;
            const unsigned char* srcRow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(gw);
            for (int x = 0; x < gw; ++x) {
                dstRow[static_cast<std::size_t>(x) * 2U] = 255;
                dstRow[static_cast<std::size_t>(x) * 2U + 1U] = srcRow[x];
            }
        }

        recs[i] = Rectangle{
            static_cast<float>(curX),
            static_cast<float>(curY),
            static_cast<float>(gw),
            static_cast<float>(gh)
        };
        curX += gw + kPadding * 2;
        if (gh > rowMaxH) rowMaxH = gh;

        // Release per-glyph CPU bitmap now that it is safely blitted into the GPU atlas image
        UnloadImage(glyphs[i].image);
        glyphs[i].image = Image{};
    }

    Image atlasImg{};
    atlasImg.data = atlasBytes;
    atlasImg.width = kAtlasWidth;
    atlasImg.height = atlasHeight;
    atlasImg.mipmaps = 1;
    atlasImg.format = PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA;

    Font font{};
    font.baseSize = fontSize;
    font.glyphCount = count;
    font.glyphPadding = kPadding;
    font.texture = LoadTextureFromImage(atlasImg);
    font.recs = recs;
    font.glyphs = glyphs;
    UnloadImage(atlasImg);

    if (font.texture.id == 0) {
        MemFree(recs);
        UnloadFontData(glyphs, count);
        return Font{};
    }
    return font;
}

void HudRenderer::init() {
    std::vector<std::string> koreanTtfCandidates;
    if (const char* envFont = std::getenv("OPEN_CITY_FONT_PATH")) {
        if (envFont[0] != '\0') {
            koreanTtfCandidates.emplace_back(envFont);
        }
    }
    koreanTtfCandidates.emplace_back("C:/Windows/Fonts/malgunbd.ttf");
    koreanTtfCandidates.emplace_back("C:/Windows/Fonts/malgun.ttf");
    koreanTtfCandidates.emplace_back("C:/Windows/Fonts/gulim.ttc");
    koreanTtfCandidates.emplace_back("/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf");
    koreanTtfCandidates.emplace_back("/usr/share/fonts/truetype/nanum/NanumGothic.ttf");
    koreanTtfCandidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc");
    koreanTtfCandidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");

    const Font defaultFont = GetFontDefault();

    for (const std::string& path : koreanTtfCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        Font candidate = loadCompleteKoreanFont(path.c_str(), 26);
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
        Font candidate = LoadFontEx(path, 26, nullptr, 95);
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
        DrawTextEx(uiFont_, text, {x + 1.0f, y + 1.0f}, fontSize, 0.5f,
                   Color{0, 0, 0, static_cast<unsigned char>(color.a * 0.80f)});
        DrawTextEx(uiFont_, text, {x, y}, fontSize, 0.5f, color);
    } else {
        const int sz = std::max(10, static_cast<int>(fontSize));
        DrawText(text, static_cast<int>(x) + 1, static_cast<int>(y) + 1, sz,
                 Color{0, 0, 0, static_cast<unsigned char>(color.a * 0.80f)});
        DrawText(text, static_cast<int>(x), static_cast<int>(y), sz, color);
    }
}

float HudRenderer::measureUiText(const char* text, float fontSize) const {
    if (customFontLoaded_) {
        return MeasureTextEx(uiFont_, text, fontSize, 0.5f).x;
    }
    return static_cast<float>(MeasureText(text, std::max(10, static_cast<int>(fontSize))));
}

void HudRenderer::showSystemToast(const char* textEn, const char* textKo) {
    gfxToastEn_ = textEn ? textEn : "";
    gfxToastKo_ = textKo ? textKo : "";
    gfxToastTimer_ = 2.2f;
}

void HudRenderer::showGraphicsToast(const char* labelEn, const char* labelKo) {
    gfxToastEn_ = std::string("GRAPHICS: ") + (labelEn ? labelEn : "");
    gfxToastKo_ = std::string("그래픽 품질: ") + (labelKo ? labelKo : "");
    gfxToastTimer_ = 2.4f;
}

void HudRenderer::renderHud(const GameWorld& world, float cameraYaw, bool cursorLocked,
                            bool firstPerson, float adsBlend) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (gfxToastTimer_ > 0.0f) {
        gfxToastTimer_ = std::max(0.0f, gfxToastTimer_ - GetFrameTime());
    }

    // Automatically close shop modal if player walked/drove away from shop zone
    if (shopMenuOpen_ && !world.isPlayerInAnyShopZone()) {
        shopMenuOpen_ = false;
    }

    drawCrosshair(world, sw, sh, firstPerson, adsBlend);
    drawRadarMinimap(world, cameraYaw, sh);
    drawTopRightStatus(world, sw, firstPerson);
    drawSpeedometerAndDrift(world, sw, sh);
    drawContextPromptsAndMission(world, sw, sh);

    if (shopMenuOpen_) {
        drawShopModal(world, sw, sh);
    } else if (phoneOpen_) {
        drawSmartphoneModal(world, sw, sh);
    } else if (customStudioOpen_) {
        drawCustomStudioModal(world, sw, sh);
    }

    drawBannerToast(world, sw, sh);
    if (!customStudioOpen_) {
        drawHelpOverlay(world, sw, sh, cursorLocked);
    }
}

void HudRenderer::drawRadarMinimap(const GameWorld& world, float cameraYaw, int screenH) {
    const float radius = 92.0f;
    const float cx = 26.0f + radius;
    const float cy = static_cast<float>(screenH) - 42.0f - radius;
    const float worldRange = 155.0f;
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
    DrawCircleV({cx, cy}, radius, Color{24, 34, 46, 228});

    // Western Sunset Beach sand & Sunset Bay Pier markers on radar
    for (float sz = -320.0f; sz <= 320.0f; sz += 24.0f) {
        const Vector2 ps = projectToRadar({-348.0f, sz}, false);
        if (ps.x > -1000.0f) {
            DrawCircleV(ps, 5.5f, Color{198, 178, 126, 155});
        }
    }
    for (float px = -345.0f; px >= -420.0f; px -= 15.0f) {
        const Vector2 pp = projectToRadar({px, 0.0f}, false);
        if (pp.x > -1000.0f) {
            DrawCircleV(pp, 3.2f, Color{168, 128, 86, 215});
        }
    }

    // Nearby buildings on radar
    for (const auto& b : world.city().buildings()) {
        const Vec2 bc = b.footprint.center();
        if ((bc - centerWorld).lengthSq() > (worldRange + 28.0f) * (worldRange + 28.0f)) continue;
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
            if (pv.x > -1000.0f) DrawCircleV(pv, 2.3f, Color{88, 98, 112, 190});
            const Vector2 ph = projectToRadar({s, rc}, false);
            if (ph.x > -1000.0f) DrawCircleV(ph, 2.3f, Color{88, 98, 112, 190});
        }
    }

    // Nearby vehicles, pursuing SBPD cruisers & personal summoned car highlight
    const int wanted = world.player().wantedLevel;
    for (const auto& v : world.vehicles()) {
        if (v.isPlayerDriving || v.destroyed) continue;
        const bool isPersonal = v.isPersonalCar && (v.id == world.player().activePersonalVehicleId);
        const bool activePolice = v.isPolice && (wanted > 0 || v.sirenActive);
        const Vector2 pv = projectToRadar(v.pos, isPersonal || activePolice);
        if (pv.x > -1000.0f) {
            if (v.isMissionTarget) {
                DrawCircleV(pv, 4.8f, Color{255, 218, 52, 255});
            } else if (activePolice) {
                const bool redPhase = (v.sirenPhase < 0.5f);
                const Color pCol = redPhase ? Color{255, 58, 68, 255} : Color{56, 148, 255, 255};
                if ((v.pos - centerWorld).lengthSq() < worldRange * worldRange) {
                    DrawCircleV(pv, 9.5f, Color{pCol.r, pCol.g, pCol.b, 48});
                }
                DrawCircleV(pv, 5.2f, Color{12, 16, 22, 255});
                DrawCircleV(pv, 3.9f, pCol);
            } else if (v.isPolice) {
                DrawCircleV(pv, 3.6f, Color{92, 168, 255, 225});
            } else if (isPersonal) {
                DrawCircleV(pv, 5.2f, Color{14, 18, 24, 255});
                DrawCircleV(pv, 3.8f, Color{76, 228, 255, 255});
            } else {
                DrawCircleV(pv, 2.4f, Color{185, 200, 215, 200});
            }
        }
    }

    const bool copBlinkRed = (std::fmod(GetTime() * 3.6, 1.0) < 0.5);
    for (const auto& npc : world.npcs()) {
        if (!npc.alive) continue;
        if (npc.role == NpcRole::Gangster) {
            const Vector2 pn = projectToRadar(npc.pos, false);
            if (pn.x > -1000.0f) {
                DrawCircleV(pn, 3.3f, Color{255, 72, 68, 245});
            }
        } else if (npc.role == NpcRole::Police) {
            const Vector2 pn = projectToRadar(npc.pos, wanted > 0);
            if (pn.x > -1000.0f) {
                const Color cCol = (wanted > 0 && copBlinkRed)
                                       ? Color{255, 68, 76, 250}
                                       : Color{64, 162, 255, 250};
                DrawCircleV(pn, 4.3f, Color{10, 14, 20, 255});
                DrawCircleV(pn, 3.1f, cCol);
            }
        }
    }

    // POIs (Shops, Dealership, Boutique, Gas Station, Nightclub, Mission Coronas)
    for (const auto& poi : world.city().pois()) {
        if (world.mission().kind != ActiveMissionKind::None &&
            (poi.kind == PoiKind::MissionDelivery ||
             poi.kind == PoiKind::MissionRace ||
             poi.kind == PoiKind::MissionGangRaid)) {
            continue;
        }
        const bool clamp = (poi.kind == PoiKind::WeaponShop ||
                            poi.kind == PoiKind::TuneGarage ||
                            poi.kind == PoiKind::CarDealership);
        const Vector2 pp = projectToRadar(poi.pos, clamp);
        if (pp.x > -1000.0f) {
            const Color pc{poi.markerColor.r, poi.markerColor.g, poi.markerColor.b, 255};
            DrawCircleV(pp, 4.8f, Color{12, 14, 18, 255});
            DrawCircleV(pp, 3.5f, pc);
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

    // Radar ring border (pulses Red/Blue during active police pursuit!) + North [N] compass badge
    Color ringCol{210, 220, 232, 235};
    if (wanted > 0) {
        if (world.player().policeLineOfSight) {
            ringCol = copBlinkRed ? Color{255, 62, 72, 250} : Color{58, 152, 255, 250};
        } else {
            const auto evA = static_cast<unsigned char>(155.0 + 90.0 * (0.5 + 0.5 * std::sin(GetTime() * 8.0)));
            ringCol = Color{130, 205, 255, evA};
        }
    }
    DrawRing({cx, cy}, radius, radius + 3.8f, 0.0f, 360.0f, 48, ringCol);

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

void HudRenderer::drawTopRightStatus(const GameWorld& world, int screenW, bool firstPerson) {
    const float rightEdge = static_cast<float>(screenW) - 24.0f;

    // Time of day + District + Camera View Mode ([V] 1st / 3rd Person)
    const int hour = static_cast<int>(world.timeOfDayHours()) % 24;
    const int minute = static_cast<int>((world.timeOfDayHours() - std::floor(world.timeOfDayHours())) * 60.0f);
    const Vec2 pPos = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;

    const char* distName = "MIDTOWN";
    if (pPos.x < -332.0f) {
        distName = pickText("SUNSET OCEAN PIER", "선셋 오션 비치·피어");
    } else {
        switch (world.city().districtAtWorld(pPos)) {
            case DistrictType::Downtown:           distName = pickText("DOWNTOWN", "다운타운 금융가"); break;
            case DistrictType::Midtown:            distName = pickText("MIDTOWN", "미드타운 상업지구"); break;
            case DistrictType::Industrial:         distName = pickText("HARBOR DOCKS", "항구 공업단지"); break;
            case DistrictType::CoastalResidential: distName = pickText("SUNSET BEACH", "선셋 해변지구"); break;
            case DistrictType::CentralPark:        distName = pickText("CENTRAL PARK", "센트럴 파크"); break;
        }
    }

    const char* viewBadge = firstPerson
                                ? pickText("[V] 1ST PERSON", "[V] 1인칭 시점")
                                : pickText("[V] 3RD PERSON", "[V] 3인칭 시점");
    char clockBuf[144];
    std::snprintf(clockBuf, sizeof(clockBuf), "%s  |  %02d:%02d  |  %s", viewBadge, hour, minute, distName);
    const float clockW = measureUiText(clockBuf, 17.5f);
    drawUiText(clockBuf, rightEdge - clockW, 18.0f, 17.5f,
               firstPerson ? Color{128, 228, 255, 250} : Color{225, 232, 242, 245});

    // Classic GTA green cash counter
    char cashBuf[48];
    std::snprintf(cashBuf, sizeof(cashBuf), "$%08d", world.player().cash);
    const float cashW = measureUiText(cashBuf, 32.0f);
    drawUiText(cashBuf, rightEdge - cashW, 42.0f, 32.0f, Color{92, 232, 112, 255});

    // Passive income / Smartphone + Custom Studio quick hint line
    const auto& pl = world.player();
    const ColorRGBA themeRgba = getHudThemeColorRGBA(pl.hudCustom.hudThemeIdx);
    const Color themeCol = toRayColor(themeRgba, 245);

    char assetBuf[160];
    const int propCount = world.ownedPropertyCount();
    if (propCount > 0) {
        std::snprintf(assetBuf, sizeof(assetBuf),
                      pickText("INCOME +$%d / 25s (%d/6)  |  [P] Phone  ·  [K] Studio",
                               "수익 +$%d / 25초 (자산 %d/6)  |  [P] 스마트폰  ·  [K] 커스텀"),
                      world.passiveIncomePerCycle(), propCount);
    } else {
        std::snprintf(assetBuf, sizeof(assetBuf), "%s",
                      pickText("[P] Smartphone  |  [K] Custom Studio (Char · Gun · Car · HUD)",
                               "[P] 스마트폰  |  [K] 커스텀 스튜디오 (캐릭터 · 총기 · 차량 · HUD)"));
    }
    const Color assetCol = (propCount > 0) ? Color{255, 218, 92, 250} : toRayColor(themeRgba, 232);
    const float assetW = measureUiText(assetBuf, 16.0f);
    drawUiText(assetBuf, rightEdge - assetW, 78.0f, 16.0f, assetCol);

    float weaponCardY = 102.0f;

    // 1-5 Star SBPD Wanted Level HUD + Evasion Progress Bar!
    if (pl.wantedLevel > 0) {
        const float boxW = 248.0f;
        const float boxH = pl.policeLineOfSight ? 52.0f : 62.0f;
        const float boxX = rightEdge - boxW;
        const float boxY = 100.0f;
        const bool redBeat = (std::fmod(GetTime() * 4.2, 1.0) < 0.5);

        DrawRectangleRounded({boxX, boxY, boxW, boxH}, 0.22f, 6, Color{12, 17, 26, 228});
        const Color borderCol = pl.policeLineOfSight
                                    ? (redBeat ? Color{255, 64, 74, 230} : Color{56, 148, 255, 230})
                                    : Color{118, 206, 255, 185};
        DrawRectangleRoundedLinesEx({boxX, boxY, boxW, boxH}, 0.22f, 6, 1.8f, borderCol);

        const float blinkMul = pl.policeLineOfSight
                                   ? 1.0f
                                   : static_cast<float>(0.34 + 0.66 * (0.5 + 0.5 * std::sin(GetTime() * 9.0)));

        for (int s = 0; s < 5; ++s) {
            const Vector2 sc{boxX + 28.0f + static_cast<float>(s) * 34.0f, boxY + 21.0f};
            drawFivePointStar2D({sc.x + 1.2f, sc.y + 1.4f}, 12.5f, 5.2f, Color{6, 8, 12, 210});
            if (s < pl.wantedLevel) {
                const auto starAlpha = static_cast<unsigned char>(clampf(255.0f * blinkMul, 70.0f, 255.0f));
                const Color litCol = pl.policeLineOfSight
                                         ? Color{255, 214, 58, starAlpha}
                                         : Color{176, 228, 255, starAlpha};
                drawFivePointStar2D(sc, 12.2f, 5.0f, litCol);
                drawFivePointStar2D(sc, 6.2f, 2.6f, Color{255, 248, 205, starAlpha});
            } else {
                drawFivePointStar2D(sc, 11.2f, 4.6f, Color{54, 64, 78, 185});
            }
        }

        // Badge label on right of stars + status/evasion bar underneath
        const char* unitTag = (pl.wantedLevel >= 4) ? "SWAT" : "SBPD";
        const float uw = measureUiText(unitTag, 15.0f);
        drawUiText(unitTag, boxX + boxW - uw - 12.0f, boxY + 13.0f, 15.0f,
                   redBeat ? Color{255, 96, 102, 255} : Color{102, 186, 255, 255});

        if (pl.policeLineOfSight) {
            const char* pTxt = (pl.wantedLevel >= 4)
                                   ? pickText("SWAT PURSUIT ACTIVE", "특수기동대 무장 추격 중")
                                   : pickText("POLICE IN PURSUIT", "경찰 순찰대 추격 중");
            drawUiText(pTxt, boxX + 14.0f, boxY + 34.0f, 14.0f, Color{255, 198, 196, 245});
        } else {
            const float needed = 11.5f + static_cast<float>(pl.wantedLevel) * 2.5f;
            const float evFrac = clampf(pl.wantedEvadeTimer / needed, 0.0f, 1.0f);
            drawUiText(pickText("EVADING SIGHT...", "시야 이탈 도주 중"), boxX + 14.0f, boxY + 35.0f, 13.5f,
                       Color{152, 226, 255, 245});
            DrawRectangleRounded({boxX + 132.0f, boxY + 40.0f, boxW - 144.0f, 8.0f}, 0.4f, 4, Color{28, 38, 52, 240});
            DrawRectangleRounded({boxX + 132.0f, boxY + 40.0f, (boxW - 144.0f) * evFrac, 8.0f},
                                 0.4f, 4, Color{84, 224, 255, 255});
        }

        weaponCardY = boxY + boxH + 8.0f;
    }

    // Equipped weapon & ammo card (with HUD theme accent + Gunsmith attachment badges)
    const auto& wspec = getWeaponSpec(pl.currentWeapon);
    const auto& wslot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];

    DrawRectangleRounded({rightEdge - 264.0f, weaponCardY, 264.0f, 54.0f}, 0.22f, 6, Color{14, 20, 28, 218});
    DrawRectangleRoundedLinesEx({rightEdge - 264.0f, weaponCardY, 264.0f, 54.0f},
                                0.22f, 6, 1.2f, toRayColor(themeRgba, 135));
    char wTitleBuf[96];
    std::snprintf(wTitleBuf, sizeof(wTitleBuf), "[%d/%d] %s",
                  static_cast<int>(pl.currentWeapon) + 1,
                  static_cast<int>(WeaponId::Count),
                  pickText(wspec.nameEn, wspec.nameKo));
    drawUiText(wTitleBuf, rightEdge - 250.0f, weaponCardY + 6.0f, 17.5f, Color{242, 246, 252, 255});

    char ammoBuf[64];
    if (pl.reloadTimer > 0.0f) {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "%s", pickText("RELOADING...", "재장전 중..."));
    } else if (wspec.magazineSize > 0) {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "%d / %d", wslot.magAmmo, wslot.reserveAmmo);
    } else {
        std::snprintf(ammoBuf, sizeof(ammoBuf), "%s", pickText("MELEE [1-0,- · Z/X]", "근접 격투 [1~0,- · Z/X]"));
    }
    drawUiText(ammoBuf, rightEdge - 250.0f, weaponCardY + 28.0f, 16.5f, Color{255, 214, 92, 255});

    if (pl.currentWeapon != WeaponId::Fists) {
        char modBuf[48];
        int modLen = 0;
        if (wslot.suppressor) modLen += std::snprintf(modBuf + modLen, sizeof(modBuf) - modLen, "SUP ");
        if (wslot.extendedMag) modLen += std::snprintf(modBuf + modLen, sizeof(modBuf) - modLen, "EXT ");
        if (wslot.laserSightColor > 0) modLen += std::snprintf(modBuf + modLen, sizeof(modBuf) - modLen, "LSR");
        if (modLen > 0) {
            const float mw = measureUiText(modBuf, 13.5f);
            drawUiText(modBuf, rightEdge - mw - 12.0f, weaponCardY + 31.0f, 13.5f, themeCol);
        }
    }
}

void HudRenderer::drawSpeedometerAndDrift(const GameWorld& world, int screenW, int screenH) {
    const Vehicle* car = world.playerVehicle();
    if (!car) return;

    const auto& spec = getVehicleSpec(car->kind);
    const auto& hc = world.player().hudCustom;
    const ColorRGBA themeRgba = getHudThemeColorRGBA(hc.hudThemeIdx);
    const float phoneShift = phoneOpen_ ? 342.0f : 0.0f;
    const float rx = static_cast<float>(screenW) - 252.0f - phoneShift;
    const float ry = static_cast<float>(screenH) - 138.0f;

    DrawRectangleRounded({rx, ry, 228.0f, 112.0f}, 0.20f, 8, Color{14, 18, 26, 224});
    DrawRectangleRoundedLinesEx({rx, ry, 228.0f, 112.0f}, 0.20f, 8, 1.2f, toRayColor(themeRgba, 128));

    const char* vName = pickText(spec.nameEn, spec.nameKo);
    drawUiText(vName, rx + 14.0f, ry + 9.0f, 17.0f,
               car->isPersonalCar ? toRayColor(themeRgba, 255) : Color{182, 216, 250, 255});

    const int dispSpd = static_cast<int>(std::round(car->speed() * (hc.useMph ? 2.23694f : 3.6f)));
    char spdBuf[32];
    std::snprintf(spdBuf, sizeof(spdBuf), "%3d", dispSpd);
    drawUiText(spdBuf, rx + 14.0f, ry + 30.0f, 36.0f,
               car->nitroActive ? Color{92, 224, 255, 255} : Color{255, 255, 255, 255});
    drawUiText(hc.useMph ? "MPH" : "KM/H", rx + 92.0f, ry + 34.0f, 16.0f, Color{175, 185, 198, 255});

    if (car->engineUpgradeLevel > 0 || car->armorUpgradeLevel > 0) {
        char tuneBuf[48];
        std::snprintf(tuneBuf, sizeof(tuneBuf), "T%d · A%d", car->engineUpgradeLevel, car->armorUpgradeLevel);
        drawUiText(tuneBuf, rx + 92.0f, ry + 52.0f, 15.0f, Color{255, 212, 88, 255});
    }

    // Nitro & Vehicle Integrity bars
    drawUiText("NITRO", rx + 14.0f, ry + 76.0f, 14.0f, Color{118, 212, 255, 255});
    DrawRectangleRounded({rx + 70.0f, ry + 79.0f, 142.0f, 8.0f}, 0.4f, 4, Color{32, 40, 52, 255});
    DrawRectangleRounded({rx + 70.0f, ry + 79.0f, 142.0f * clampf(car->nitroCharge, 0.0f, 1.0f), 8.0f},
                         0.4f, 4, Color{68, 205, 255, 255});

    drawUiText("BODY", rx + 14.0f, ry + 92.0f, 14.0f, Color{235, 196, 98, 255});
    DrawRectangleRounded({rx + 70.0f, ry + 95.0f, 142.0f, 8.0f}, 0.4f, 4, Color{32, 40, 52, 255});
    const float hpFrac = clampf(car->health / car->maxHealth, 0.0f, 1.0f);
    const Color hpCol = (hpFrac < 0.35f) ? Color{245, 76, 68, 255} : Color{98, 232, 132, 255};
    DrawRectangleRounded({rx + 70.0f, ry + 95.0f, 142.0f * hpFrac, 8.0f}, 0.4f, 4, hpCol);

    // Live Drift Combo Meter
    if (world.player().activeDriftScore > 8.0f) {
        char driftBuf[64];
        std::snprintf(driftBuf, sizeof(driftBuf), "DRIFT  %d PTS",
                      static_cast<int>(world.player().activeDriftScore));
        const float dw = measureUiText(driftBuf, 24.0f);
        drawUiText(driftBuf, rx + 114.0f - dw * 0.5f, ry - 34.0f, 24.0f, Color{255, 208, 56, 255});
    }
}

void HudRenderer::drawCrosshair(const GameWorld& world, int screenW, int screenH,
                                bool firstPerson, float adsBlend) {
    if (shopMenuOpen_) return;
    if (world.player().drivingVehicleId >= 0 && !(customStudioOpen_ && customTab_ == 3)) return;
    if (customStudioOpen_ && !firstPerson && (customTab_ == 0 || customTab_ == 2)) return;

    const float cx = static_cast<float>(screenW) * 0.5f;
    const float cy = static_cast<float>(screenH) * 0.5f;

    const auto& pl = world.player();
    const auto& hc = pl.hudCustom;
    const auto& wslot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
    const float ads = clampf(adsBlend, 0.0f, 1.0f);

    // Illuminated 3.2x Optical Mil-Dot Sniper Scope & Laser Rangefinder HUD (tinted by Gunsmith Optic Reticle!)
    if (pl.currentWeapon == WeaponId::SniperRifle && ads > 0.72f) {
        const float scopeR = std::min(static_cast<float>(screenW), static_cast<float>(screenH)) * 0.415f;
        const float outerR = std::hypot(cx, cy) + 48.0f;
        const ColorRGBA optRgba = getOpticReticleColorRGBA(wslot.opticColorIdx, {106, 236, 255, 255});

        DrawRing({cx, cy}, scopeR, outerR, 0.0f, 360.0f, 96, Color{5, 8, 13, 240});
        DrawRing({cx, cy}, scopeR - 5.0f, scopeR + 4.5f, 0.0f, 360.0f, 96, Color{24, 32, 44, 250});
        DrawRing({cx, cy}, scopeR - 6.8f, scopeR - 5.0f, 0.0f, 360.0f, 96, toRayColor(optRgba, 195));
        DrawRing({cx, cy}, scopeR * 0.86f, scopeR - 6.8f, 0.0f, 360.0f, 96, toRayColor(optRgba, 22));

        const float postOuter = scopeR - 5.5f;
        const float postInner = scopeR * 0.48f;
        const Color postCol{12, 18, 26, 238};
        DrawLineEx({cx - postOuter, cy}, {cx - postInner, cy}, 3.2f, postCol);
        DrawLineEx({cx + postInner, cy}, {cx + postOuter, cy}, 3.2f, postCol);
        DrawLineEx({cx, cy - postOuter}, {cx, cy - postInner}, 3.2f, postCol);
        DrawLineEx({cx, cy + postInner}, {cx, cy + postOuter}, 3.2f, postCol);

        const Color hairCol = toRayColor(optRgba, 195);
        DrawLineEx({cx - postInner, cy}, {cx - 7.0f, cy}, 1.3f, hairCol);
        DrawLineEx({cx + 7.0f, cy}, {cx + postInner, cy}, 1.3f, hairCol);
        DrawLineEx({cx, cy - postInner}, {cx, cy - 7.0f}, 1.3f, hairCol);
        DrawLineEx({cx, cy + 7.0f}, {cx, cy + postInner}, 1.3f, hairCol);

        const float milStep = scopeR * 0.092f;
        for (int k = -4; k <= 4; ++k) {
            if (k == 0) continue;
            const float off = static_cast<float>(k) * milStep;
            const float tickH = (std::abs(k) % 2 == 0) ? 5.5f : 3.2f;
            DrawLineEx({cx + off, cy - tickH}, {cx + off, cy + tickH}, 1.2f, hairCol);
            DrawLineEx({cx - tickH, cy + off}, {cx + tickH, cy + off}, 1.2f, hairCol);
            DrawCircleV({cx + off, cy}, 1.8f, toRayColor(optRgba, 225));
            if (k > 0) {
                DrawCircleV({cx, cy + off}, 1.8f, toRayColor(optRgba, 225));
            }
        }

        const Color centerCol = pl.crosshairOnTarget ? Color{255, 72, 68, 255} : toRayColor(optRgba, 250);
        DrawCircleV({cx, cy}, 3.2f, Color{8, 12, 18, 185});
        DrawCircleV({cx, cy}, 2.1f, centerCol);

        drawUiText("OPTIC 3.2X  ·  MIL-DOT", cx - scopeR * 0.54f, cy - scopeR * 0.48f, 15.0f,
                   toRayColor(optRgba, 225));
        char rngBuf[64];
        std::snprintf(rngBuf, sizeof(rngBuf), "%s %5.1f M",
                      pickText("RNG", "거리"), pl.crosshairHitDist);
        drawUiText(rngBuf, cx + scopeR * 0.16f, cy + scopeR * 0.36f, 16.5f,
                   pl.crosshairOnTarget ? Color{255, 214, 92, 245} : toRayColor(optRgba, 235));
        if (pl.crosshairOnTarget) {
            drawUiText(pickText("TARGET ACQUIRED", "표적 정밀 포착"),
                       cx + scopeR * 0.16f, cy + scopeR * 0.43f, 15.0f, Color{255, 88, 82, 248});
        }
    } else {
        // Customizable 4-style steady reticle (Cross+Dot / Pinpoint Dot / Circle+Dot / Tactical Chevron)
        const float scale = (hc.crosshairScaleIdx == 0) ? 0.80f : ((hc.crosshairScaleIdx == 2) ? 1.32f : 1.0f);
        const float moveSpread = clampf(pl.velocity.length() * 0.16f, 0.0f, 1.4f) * (1.0f - ads * 0.65f);
        const float gap = (lerpf(5.6f, 3.2f, ads) + moveSpread) * scale;
        const float armLen = lerpf(7.2f, 5.4f, ads) * scale;

        float tickFade = 1.0f;
        if (firstPerson && pl.currentWeapon != WeaponId::Fists && ads > 0.55f) {
            tickFade = clampf((0.88f - ads) / 0.33f, 0.0f, 1.0f);
        }

        const ColorRGBA customRgba = getCrosshairColorRGBA(hc.crosshairColorIdx);
        const Color col = pl.crosshairOnTarget
                              ? Color{255, 78, 72, 248}
                              : Color{customRgba.r, customRgba.g, customRgba.b, 238};
        const bool drawOut = hc.crosshairOutline;

        if (tickFade > 0.02f && hc.crosshairStyle != 1) {
            const unsigned char shA = static_cast<unsigned char>(175.0f * tickFade);
            const unsigned char fgA = static_cast<unsigned char>(static_cast<float>(col.a) * tickFade);
            const Color shadowCol{10, 14, 20, shA};
            const Color tickCol{col.r, col.g, col.b, fgA};

            if (hc.crosshairStyle == 0) {
                // Style 0: Dynamic Cross + Dot
                if (drawOut) {
                    DrawLineEx({cx - gap - armLen - 1.0f, cy}, {cx - gap + 1.0f, cy}, 3.0f * scale, shadowCol);
                    DrawLineEx({cx + gap - 1.0f, cy}, {cx + gap + armLen + 1.0f, cy}, 3.0f * scale, shadowCol);
                    DrawLineEx({cx, cy - gap - armLen - 1.0f}, {cx, cy - gap + 1.0f}, 3.0f * scale, shadowCol);
                    DrawLineEx({cx, cy + gap - 1.0f}, {cx, cy + gap + armLen + 1.0f}, 3.0f * scale, shadowCol);
                }
                DrawLineEx({cx - gap - armLen, cy}, {cx - gap, cy}, 1.7f * scale, tickCol);
                DrawLineEx({cx + gap, cy}, {cx + gap + armLen, cy}, 1.7f * scale, tickCol);
                DrawLineEx({cx, cy - gap - armLen}, {cx, cy - gap}, 1.7f * scale, tickCol);
                DrawLineEx({cx, cy + gap}, {cx, cy + gap + armLen}, 1.7f * scale, tickCol);
            } else if (hc.crosshairStyle == 2) {
                // Style 2: Tactical Circle + Dot + 4 Micro Compass Ticks
                const float ringR = gap + 4.8f * scale;
                if (drawOut) {
                    DrawRing({cx, cy}, ringR - 1.7f, ringR + 1.7f, 0.0f, 360.0f, 40, shadowCol);
                }
                DrawRing({cx, cy}, ringR - 0.85f, ringR + 0.85f, 0.0f, 360.0f, 40, tickCol);
                const float tk = 3.2f * scale;
                DrawLineEx({cx - ringR - tk, cy}, {cx - ringR + 0.5f, cy}, 1.5f * scale, tickCol);
                DrawLineEx({cx + ringR - 0.5f, cy}, {cx + ringR + tk, cy}, 1.5f * scale, tickCol);
                DrawLineEx({cx, cy - ringR - tk}, {cx, cy - ringR + 0.5f}, 1.5f * scale, tickCol);
                DrawLineEx({cx, cy + ringR - 0.5f}, {cx, cy + ringR + tk}, 1.5f * scale, tickCol);
            } else if (hc.crosshairStyle == 3) {
                // Style 3: Spec-Ops Inverted V Chevron ^
                const float chevW = 7.4f * scale;
                const float chevH = 8.4f * scale;
                if (drawOut) {
                    DrawLineEx({cx, cy}, {cx - chevW, cy + chevH}, 3.2f * scale, shadowCol);
                    DrawLineEx({cx, cy}, {cx + chevW, cy + chevH}, 3.2f * scale, shadowCol);
                    DrawLineEx({cx - gap - 5.5f * scale, cy}, {cx - gap - 1.5f * scale, cy}, 2.8f * scale, shadowCol);
                    DrawLineEx({cx + gap + 1.5f * scale, cy}, {cx + gap + 5.5f * scale, cy}, 2.8f * scale, shadowCol);
                }
                DrawLineEx({cx, cy}, {cx - chevW, cy + chevH}, 1.8f * scale, tickCol);
                DrawLineEx({cx, cy}, {cx + chevW, cy + chevH}, 1.8f * scale, tickCol);
                DrawLineEx({cx - gap - 5.2f * scale, cy}, {cx - gap - 1.8f * scale, cy}, 1.5f * scale, tickCol);
                DrawLineEx({cx + gap + 1.8f * scale, cy}, {cx + gap + 5.2f * scale, cy}, 1.5f * scale, tickCol);
            }
        }

        // Crisp center pinpoint dot (for Styles 0, 1, 2; Style 3 uses Chevron apex!)
        if (hc.crosshairStyle != 3) {
            const float baseDotR = (hc.crosshairStyle == 1) ? 2.35f : 1.9f;
            const float dotR = ((firstPerson && ads > 0.65f && pl.currentWeapon != WeaponId::Fists)
                                    ? 1.35f
                                    : baseDotR) * scale;
            if (drawOut) {
                DrawCircleV({cx, cy}, dotR + 0.8f, Color{10, 14, 20, 175});
            }
            DrawCircleV({cx, cy}, dotR, col);
        }
    }

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
        DrawRectangleRounded({midX - 280.0f, 16.0f, 560.0f, 58.0f}, 0.22f, 8, Color{14, 18, 26, 225});

        const char* objText = pickText(m.objectiveEn.c_str(), m.objectiveKo.c_str());
        const float ow = measureUiText(objText, 19.0f);
        drawUiText(objText, midX - ow * 0.5f, 22.0f, 19.0f, Color{255, 224, 78, 255});

        const int sec = std::max(0, static_cast<int>(std::ceil(m.timeRemaining)));
        char timerBuf[96];
        std::snprintf(timerBuf, sizeof(timerBuf), "%s %02d:%02d   |   [Backspace] %s",
                      pickText("TIME", "남은 시간"), sec / 60, sec % 60,
                      pickText("Abort", "미션 취소"));
        const float tw = measureUiText(timerBuf, 17.0f);
        const Color tCol = (sec <= 15) ? Color{255, 88, 82, 255} : Color{210, 220, 232, 245};
        drawUiText(timerBuf, midX - tw * 0.5f, 48.0f, 17.0f, tCol);
    }

    // Bottom-center interaction prompt (Shops / Mission Coronas / Vehicles)
    std::string prompt;
    if (!shopMenuOpen_) {
        if (world.isPlayerInWeaponShopZone()) {
            prompt = pickText("Press [B] to open Ammu-Tech Weapon & Heavy Ordnance Shop",
                              "[B] 키를 눌러 AMMU-TECH 중화기 · 방탄복 상점 열기");
        } else if (world.isPlayerInGarageZone()) {
            prompt = pickText("Press [B] to open Pay 'n' Tune Full Custom Shop",
                              "[B] 키를 눌러 PAY 'N' TUNE 풀튜닝 · 네온 공방 열기");
        } else if (world.isPlayerInDealershipZone()) {
            prompt = pickText("Press [B] to browse Luxury Autos Supercar Showroom",
                              "[B] 키를 눌러 LUXURY AUTOS 슈퍼카 전시장 열기");
        } else if (world.isPlayerInBoutiqueZone()) {
            prompt = pickText("Press [B] to enter Moda Designer Clothing Boutique",
                              "[B] 키를 눌러 MODA 프리미엄 의상 부티크 열기");
        } else if (world.isPlayerInGasStationZone()) {
            prompt = pickText("Press [B] for Globe Oil Pit Service & Property Investment",
                              "[B] 키를 눌러 GLOBE OIL 급속 주유 · 주유소 매입 열기");
        } else if (world.isPlayerInNightclubZone()) {
            prompt = pickText("Press [B] to manage Pulse Nightclub & Eclipse Penthouse",
                              "[B] 키를 눌러 PULSE 나이트클럽 · 펜트하우스 매입 열기");
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
        const float pw = measureUiText(prompt.c_str(), 20.0f);
        const float py = static_cast<float>(screenH) - 60.0f;
        DrawRectangleRounded({midX - pw * 0.5f - 18.0f, py - 8.0f, pw + 36.0f, 36.0f},
                             0.30f, 6, Color{12, 16, 24, 228});
        drawUiText(prompt.c_str(), midX - pw * 0.5f, py, 20.0f, Color{255, 242, 152, 255});
    }
}

void HudRenderer::drawShopModal(const GameWorld& world, int screenW, int screenH) {
    if (!world.isPlayerInAnyShopZone()) return;

    auto optKeyLabel = [](int opt1) -> const char* {
        static const char* kKeys[13] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="};
        return (opt1 >= 1 && opt1 <= 12) ? kKeys[opt1] : "?";
    };

    struct ShopRow {
        std::string labelEn;
        std::string labelKo;
        std::string priceTag;
        Color tagColor{102, 238, 122, 255};
    };

    std::vector<ShopRow> rows;
    std::string titleEn;
    std::string titleKo;
    Color accent{88, 226, 255, 255};

    const auto& pl = world.player();

    if (world.isPlayerInWeaponShopZone()) {
        titleEn = "AMMU-TECH WEAPONS & HEAVY ORDNANCE";
        titleKo = "AMMU-TECH 무기 · 중화기 전문점";
        accent = Color{255, 116, 76, 255};
        rows = {
            {"[1] Tactical SMG (+120 Ammo)",          "[1] 전술 기관단총 (+120발)",           "$350"},
            {"[2] Pump Shotgun (+32 Shells)",         "[2] 펌프 샷건 (+32발)",                "$500"},
            {"[3] Carbine Rifle (+150 Ammo)",         "[3] 돌격 카빈소총 (+150발)",           "$850"},
            {"[4] Full Ammo Pack (+4 Mags)",          "[4] 보유 무기 전체 탄약 보충",         "$150"},
            {"[5] Heavy Body Armor (100%)",           "[5] 중형 방탄복 100% 장착",            "$200"},
            {"[6] Emergency Medkit (100%)",           "[6] 응급 의료 키트 (체력 100%)",       "$100"},
            {"[7] Combat MG (+240 Rounds)",           "[7] 컴뱃 중기관총 (+240발 대용량)",    "$2,800"},
            {"[8] Tactical Explosive Launcher (+12)", "[8] 전술 폭발 런처 (+12발 광역폭발)",  "$6,500"},
            {"[9] .44 Magnum Revolver (+36 Rds)",     "[9] 44 매그넘 리볼버 (+36발 고화력)",  "$650"},
            {"[0] Assault Drum Shotgun (+48 Shells)", "[0] 전자동 드럼 샷건 (+48발 연사)",    "$1,600"},
            {"[-] Marksman Sniper Rifle (+24 Rds)",   "[-] 전술 저격소총 (+24발 스코프)",     "$2,400"},
            {"[=] Vulcan 6-Barrel Minigun (+400)",    "[=] 6열 개틀링 미니건 (+400발 초고속)", "$5,200"}
        };
    } else if (world.isPlayerInGarageZone()) {
        const float disc = world.tuningDiscountScale();
        auto costStr = [&](int basePrice) {
            const int p = std::max(20, static_cast<int>(std::round(static_cast<float>(basePrice) * disc)));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "$%d", p);
            return std::string(buf);
        };

        const Vehicle* car = world.playerVehicle();
        if (!car) {
            if (const auto* gPoi = world.city().findPoi(PoiKind::TuneGarage)) {
                for (const auto& v : world.vehicles()) {
                    if (!v.destroyed && (v.pos - gPoi->pos).length() <= gPoi->radius + 3.0f) {
                        car = &v;
                        break;
                    }
                }
            }
        }

        titleEn = (disc < 0.99f) ? "PAY 'N' TUNE  (-50% FRANCHISE VIP)" : "PAY 'N' TUNE FULL CUSTOM GARAGE";
        titleKo = (disc < 0.99f) ? "PAY 'N' TUNE 커스텀 공방 (-50% 본점 할인중)" : "PAY 'N' TUNE 차량 수리 · 풀튜닝 공방";
        accent = Color{68, 224, 255, 255};

        const int engLv = car ? car->engineUpgradeLevel : 0;
        const int armLv = car ? car->armorUpgradeLevel : 0;
        const int neonId = car ? car->neonPreset : 0;
        const int rimId = car ? car->rimStyle : 0;
        const bool wingOn = car ? car->customSpoiler : false;

        char eEn[80], eKo[96], aEn[80], aKo[96], nEn[80], nKo[96], wEn[80], wKo[96], rEn[80], rKo[96];
        std::snprintf(eEn, sizeof(eEn), "[2] Turbo Engine Upgrade (Lv %d/3)", engLv);
        std::snprintf(eKo, sizeof(eKo), "[2] 터보 엔진 스테이지 업그레이드 (Lv %d/3)", engLv);
        std::snprintf(aEn, sizeof(aEn), "[3] Armor & Bulletproof Tires (Lv %d/3)", armLv);
        std::snprintf(aKo, sizeof(aKo), "[3] 방탄 차체 · 방탄 타이어 보강 (Lv %d/3)", armLv);
        std::snprintf(nEn, sizeof(nEn), "[4] LED Neon Underglow (%s)", neonPresetNameEn(neonId));
        std::snprintf(nKo, sizeof(nKo), "[4] 하부 LED 네온 언더글로우 (%s)", neonPresetNameKo(neonId));
        std::snprintf(wEn, sizeof(wEn), "[5] Carbon GT Aero Spoiler (%s)", wingOn ? "ON" : "OFF");
        std::snprintf(wKo, sizeof(wKo), "[5] 카본 GT 에어로 스포일러 (%s)", wingOn ? "장착됨" : "미장착");
        std::snprintf(rEn, sizeof(rEn), "[6] Forged Alloy Rims (%s)", rimStyleNameEn(rimId));
        std::snprintf(rKo, sizeof(rKo), "[6] 커스텀 단조 휠 교체 (%s)", rimStyleNameKo(rimId));

        rows = {
            {"[1] Full Chassis & Engine Repair",          "[1] 차체 및 엔진 100% 완벽 수리",         costStr(120)},
            {eEn,                                         eKo,                                       (engLv >= 3) ? "MAX" : costStr(450)},
            {aEn,                                         aKo,                                       (armLv >= 3) ? "MAX" : costStr(600)},
            {nEn,                                         nKo,                                       costStr(350)},
            {wEn,                                         wKo,                                       costStr(300)},
            {rEn,                                         rKo,                                       costStr(250)},
            {"[7] Cycle Metallic & Matte Satin Paint",    "[7] 메탈릭 · 무광 새틴 프리미엄 도색",    costStr(150)},
            {"[8] Nitrous Oxide Tank 100% Refill",        "[8] 니트로 부스트 탱크 100% 충전",        costStr(80)}
        };
    } else if (world.isPlayerInDealershipZone()) {
        titleEn = "LUXURY AUTOS SUPERCAR SHOWROOM";
        titleKo = "LUXURY AUTOS 프리미엄 슈퍼카 전시장";
        accent = Color{255, 206, 72, 255};
        const int catalogCount = static_cast<int>(kDealershipCatalog.size());
        for (int i = 0; i < catalogCount; ++i) {
            const VehicleKind vk = kDealershipCatalog[static_cast<std::size_t>(i)];
            const auto& spec = getVehicleSpec(vk);
            const bool owned = pl.garage[static_cast<std::size_t>(vk)].owned;
            char lEn[96], lKo[112], pBuf[48];
            std::snprintf(lEn, sizeof(lEn), "[%s] %s", optKeyLabel(i + 1), spec.nameEn);
            std::snprintf(lKo, sizeof(lKo), "[%s] %s", optKeyLabel(i + 1), spec.nameKo);
            if (owned) {
                std::snprintf(pBuf, sizeof(pBuf), "%s", pickText("SUMMON FREE", "보유중 · 즉시호출"));
            } else {
                std::snprintf(pBuf, sizeof(pBuf), "$%d", spec.dealershipPrice);
            }
            rows.push_back({lEn, lKo, pBuf, owned ? Color{92, 228, 255, 255} : Color{102, 238, 122, 255}});
        }
        const auto& prop = getPropertySpec(PropertyId::LuxuryShowroom);
        const bool propOwned = pl.ownedProperties[static_cast<std::size_t>(PropertyId::LuxuryShowroom)];
        char pTag[48], fEn[96], fKo[128];
        if (propOwned) {
            std::snprintf(pTag, sizeof(pTag), "%s", pickText("OWNED", "매입 완료"));
        } else {
            std::snprintf(pTag, sizeof(pTag), "$%d", prop.price);
        }
        std::snprintf(fEn, sizeof(fEn), "[%s] Buy Showroom Stake (+$%d/25s, %s)",
                      optKeyLabel(catalogCount + 1), prop.incomePerCycle, prop.perkEn);
        std::snprintf(fKo, sizeof(fKo), "[%s] 전시장 지분 인수 (+$%d/25초 · %s)",
                      optKeyLabel(catalogCount + 1), prop.incomePerCycle, prop.perkKo);
        rows.push_back({
            fEn, fKo, pTag,
            propOwned ? Color{180, 205, 230, 255} : Color{255, 214, 78, 255}
        });
    } else if (world.isPlayerInBoutiqueZone()) {
        titleEn = "MODA DESIGNER CLOTHING BOUTIQUE";
        titleKo = "MODA 하이엔드 의상 · 방탄 수트 부티크";
        accent = Color{246, 128, 224, 255};
        for (int i = 0; i < 6; ++i) {
            const auto oid = static_cast<OutfitId>(i);
            const auto& ospec = getOutfitSpec(oid);
            const bool owned = pl.ownedOutfits[static_cast<std::size_t>(i)];
            const bool equipped = (pl.currentOutfit == oid);
            const int drPct = static_cast<int>(std::round((1.0f - ospec.damageTakenScale) * 100.0f));
            char lEn[112], lKo[128], pTag[48];
            if (drPct > 0) {
                std::snprintf(lEn, sizeof(lEn), "[%d] %s  (-%d%% Dmg)", i + 1, ospec.nameEn, drPct);
                std::snprintf(lKo, sizeof(lKo), "[%d] %s  (피해 -%d%%)", i + 1, ospec.nameKo, drPct);
            } else {
                std::snprintf(lEn, sizeof(lEn), "[%d] %s", i + 1, ospec.nameEn);
                std::snprintf(lKo, sizeof(lKo), "[%d] %s", i + 1, ospec.nameKo);
            }
            if (equipped) {
                std::snprintf(pTag, sizeof(pTag), "%s", pickText("EQUIPPED", "착용중"));
            } else if (owned) {
                std::snprintf(pTag, sizeof(pTag), "%s", pickText("WEAR", "보유중 · 착용"));
            } else {
                std::snprintf(pTag, sizeof(pTag), "$%d", ospec.price);
            }
            rows.push_back({
                lEn, lKo, pTag,
                equipped ? Color{255, 218, 92, 255}
                         : (owned ? Color{96, 226, 255, 255} : Color{102, 238, 122, 255})
            });
        }
    } else if (world.isPlayerInGasStationZone()) {
        titleEn = "GLOBE OIL EXPRESS PIT & INVESTMENT";
        titleKo = "GLOBE OIL 주유소 정비 · 사업장 매입";
        accent = Color{255, 148, 68, 255};
        const auto& prop = getPropertySpec(PropertyId::GlobeOilGas);
        const bool owned = pl.ownedProperties[static_cast<std::size_t>(PropertyId::GlobeOilGas)];
        char pTag[48], gEn[96], gKo[128];
        if (owned) std::snprintf(pTag, sizeof(pTag), "%s", pickText("OWNED", "매입 완료"));
        else std::snprintf(pTag, sizeof(pTag), "$%d", prop.price);
        std::snprintf(gEn, sizeof(gEn), "[2] Buy Globe Oil Station (+$%d/25s, %s)", prop.incomePerCycle, prop.perkEn);
        std::snprintf(gKo, sizeof(gKo), "[2] 글로브 오일 주유소 인수 (+$%d/25초 · %s)", prop.incomePerCycle, prop.perkKo);

        rows = {
            {"[1] Premium Fuel, Full Repair & Nitro (In Car)", "[1] 고급유 주유 · 차체 수리 · 니트로 충전 (탑승시)", "$60"},
            {gEn, gKo, pTag, owned ? Color{180, 205, 230, 255} : Color{255, 214, 78, 255}}
        };
    } else if (world.isPlayerInNightclubZone()) {
        titleEn = "PULSE NIGHTCLUB & ECLIPSE PENTHOUSE";
        titleKo = "PULSE 나이트클럽 · 이클립스 펜트하우스";
        accent = Color{216, 108, 255, 255};
        const auto& club = getPropertySpec(PropertyId::PulseNightclub);
        const auto& pent = getPropertySpec(PropertyId::EclipsePenthouse);
        const bool clubOwned = pl.ownedProperties[static_cast<std::size_t>(PropertyId::PulseNightclub)];
        const bool pentOwned = pl.ownedProperties[static_cast<std::size_t>(PropertyId::EclipsePenthouse)];
        char cTag[48], eTag[48], cEn[96], cKo[128], pEn[96], pKo[128];
        if (clubOwned) std::snprintf(cTag, sizeof(cTag), "%s", pickText("OWNED", "매입 완료"));
        else std::snprintf(cTag, sizeof(cTag), "$%d", club.price);
        if (pentOwned) std::snprintf(eTag, sizeof(eTag), "%s", pickText("OWNED", "매입 완료"));
        else std::snprintf(eTag, sizeof(eTag), "$%d", pent.price);
        std::snprintf(cEn, sizeof(cEn), "[1] Buy Pulse Nightclub (+$%d/25s, %s)", club.incomePerCycle, club.perkEn);
        std::snprintf(cKo, sizeof(cKo), "[1] 펄스 나이트클럽 인수 (+$%d/25초 · %s)", club.incomePerCycle, club.perkKo);
        std::snprintf(pEn, sizeof(pEn), "[2] Buy Eclipse Penthouse (+$%d/25s, %s)", pent.incomePerCycle, pent.perkEn);
        std::snprintf(pKo, sizeof(pKo), "[2] 이클립스 펜트하우스 매입 (+$%d/25초 · %s)", pent.incomePerCycle, pent.perkKo);

        rows = {
            {cEn, cKo, cTag, clubOwned ? Color{180, 205, 230, 255} : Color{255, 214, 78, 255}},
            {pEn, pKo, eTag, pentOwned ? Color{180, 205, 230, 255} : Color{255, 214, 78, 255}}
        };
    }

    if (rows.empty()) return;

    const bool dense = (rows.size() >= 10);
    const float rowStep = dense ? 31.0f : 35.0f;
    const float rowBoxH = dense ? 27.0f : 30.0f;
    const float fontSz = dense ? 16.5f : 17.5f;
    const float mw = 576.0f;
    const float mh = (dense ? 92.0f : 96.0f) + static_cast<float>(rows.size()) * rowStep;
    const float mx = (static_cast<float>(screenW) - mw) * 0.5f;
    const float my = (static_cast<float>(screenH) - mh) * 0.5f;

    DrawRectangleRounded({mx, my, mw, mh}, 0.06f, 8, Color{12, 18, 28, 244});
    DrawRectangleRoundedLinesEx({mx, my, mw, mh}, 0.06f, 8, 2.4f, accent);

    drawUiText(pickText(titleEn.c_str(), titleKo.c_str()), mx + 22.0f, my + 16.0f, 21.5f, accent);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float ry = my + 52.0f + static_cast<float>(i) * rowStep;
        DrawRectangleRounded({mx + 18.0f, ry, mw - 36.0f, rowBoxH}, 0.24f, 4, Color{24, 33, 46, 228});
        drawUiText(pickText(rows[i].labelEn.c_str(), rows[i].labelKo.c_str()),
                   mx + 28.0f, ry + 4.5f, fontSz, Color{238, 243, 250, 255});
        const float pw = measureUiText(rows[i].priceTag.c_str(), fontSz);
        drawUiText(rows[i].priceTag.c_str(), mx + mw - 28.0f - pw, ry + 4.5f, fontSz, rows[i].tagColor);
    }

    char hintEn[112], hintKo[128];
    if (rows.size() == 12) {
        std::snprintf(hintEn, sizeof(hintEn), "Keys [1-9, 0, -, =] Select   |   [B] or [ESC] Close");
        std::snprintf(hintKo, sizeof(hintKo), "키 [1~9, 0, -, =] 구매·선택   |   [B] 또는 [ESC] 닫기");
    } else if (rows.size() == 11) {
        std::snprintf(hintEn, sizeof(hintEn), "Keys [1-9, 0, -] Select   |   [B] or [ESC] Close");
        std::snprintf(hintKo, sizeof(hintKo), "키 [1~9, 0, -] 구매·호출   |   [B] 또는 [ESC] 닫기");
    } else {
        std::snprintf(hintEn, sizeof(hintEn), "Press [1-%zu] to select   |   [B] or [ESC] Close", rows.size());
        std::snprintf(hintKo, sizeof(hintKo), "숫자키 [1~%zu] 구매·선택   |   [B] 또는 [ESC] 닫기", rows.size());
    }
    drawUiText(pickText(hintEn, hintKo), mx + 22.0f, my + mh - 29.0f, 16.0f, Color{182, 194, 210, 255});
}

void HudRenderer::drawSmartphoneModal(const GameWorld& world, int screenW, int screenH) {
    auto optKeyLabel = [](int opt1) -> const char* {
        static const char* kKeys[13] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="};
        return (opt1 >= 1 && opt1 <= 12) ? kKeys[opt1] : "?";
    };

    const int rowCount = (phoneTab_ == 0 || phoneTab_ == 3) ? 10 : 6;
    const bool densePhone = (rowCount >= 10);
    const float pw = 342.0f;
    const float ph = densePhone ? 530.0f : 456.0f;
    const float px = static_cast<float>(screenW) - pw - 22.0f;
    const float py = static_cast<float>(screenH) - ph - 20.0f;

    // Sleek metallic smartphone chassis + glass OLED screen
    DrawRectangleRounded({px - 5.0f, py - 5.0f, pw + 10.0f, ph + 10.0f}, 0.09f, 10, Color{38, 46, 62, 248});
    DrawRectangleRounded({px, py, pw, ph}, 0.08f, 10, Color{11, 16, 25, 246});

    const Color tabAccents[4] = {
        Color{76, 222, 255, 255},
        Color{104, 242, 148, 255},
        Color{246, 132, 228, 255},
        Color{255, 184, 72, 255}
    };
    const Color activeAccent = tabAccents[phoneTab_];
    DrawRectangleRoundedLinesEx({px, py, pw, ph}, 0.08f, 10, 2.0f, activeAccent);

    // Top speaker notch & status bar
    DrawRectangleRounded({px + pw * 0.5f - 36.0f, py + 7.0f, 72.0f, 5.0f}, 0.5f, 4, Color{42, 52, 68, 255});
    drawUiText(pickText("iFRUIT OS  |  [Q/E] Switch App", "iFRUIT 스마트폰  |  [Q/E] 앱 전환"),
               px + 16.0f, py + 17.0f, 15.2f, Color{198, 212, 228, 245});

    // 4 App Tab Pills
    const char* tabEn[4] = {"AUTOS", "REALTY", "STYLE", "SUPPLY"};
    const char* tabKo[4] = {"차량호출", "부동산", "의상실", "긴급보급"};
    const float tabW = (pw - 28.0f - 12.0f) / 4.0f;
    for (int t = 0; t < 4; ++t) {
        const float tx = px + 14.0f + static_cast<float>(t) * (tabW + 4.0f);
        const float ty = py + 41.0f;
        const bool sel = (t == phoneTab_);
        DrawRectangleRounded({tx, ty, tabW, 27.0f}, 0.30f, 6,
                             sel ? Color{activeAccent.r, activeAccent.g, activeAccent.b, 56}
                                 : Color{22, 30, 42, 230});
        if (sel) {
            DrawRectangleRoundedLinesEx({tx, ty, tabW, 27.0f}, 0.30f, 6, 1.5f, activeAccent);
        }
        const char* tLabel = pickText(tabEn[t], tabKo[t]);
        const float tw = measureUiText(tLabel, 14.8f);
        drawUiText(tLabel, tx + (tabW - tw) * 0.5f, ty + 5.5f, 14.8f,
                   sel ? activeAccent : Color{168, 180, 198, 235});
    }

    const auto& pl = world.player();
    const float listTop = py + (densePhone ? 75.0f : 80.0f);
    const float rowH = densePhone ? 39.0f : 54.0f;
    const float rowGap = densePhone ? 3.0f : 4.0f;

    for (int i = 0; i < rowCount; ++i) {
        const float ry = listTop + static_cast<float>(i) * (rowH + rowGap);
        const bool isBribeRow = (phoneTab_ == 3 && i == 6);
        DrawRectangleRounded({px + 12.0f, ry, pw - 24.0f, rowH}, 0.20f, 6,
                             (isBribeRow && pl.wantedLevel > 0) ? Color{28, 34, 54, 242} : Color{20, 28, 40, 235});
        if (isBribeRow && pl.wantedLevel > 0) {
            DrawRectangleRoundedLinesEx({px + 12.0f, ry, pw - 24.0f, rowH}, 0.20f, 6, 1.2f, Color{96, 178, 255, 210});
        }

        char line1En[96] = {};
        char line1Ko[112] = {};
        char line2En[96] = {};
        char line2Ko[112] = {};
        char rightTag[48] = {};
        Color rightCol{102, 238, 122, 255};

        if (phoneTab_ == 0) {
            const VehicleKind vk = kDealershipCatalog[static_cast<std::size_t>(i)];
            const auto& spec = getVehicleSpec(vk);
            const auto& slot = pl.garage[static_cast<std::size_t>(vk)];
            std::snprintf(line1En, sizeof(line1En), "[%s] %s", optKeyLabel(i + 1), spec.nameEn);
            std::snprintf(line1Ko, sizeof(line1Ko), "[%s] %s", optKeyLabel(i + 1), spec.nameKo);
            std::snprintf(line2En, sizeof(line2En), "Top %d km/h  |  Turbo Lv%d · Armor Lv%d",
                          static_cast<int>(std::round(spec.maxForwardSpeed * 3.6f)),
                          slot.engineUpgradeLevel, slot.armorUpgradeLevel);
            std::snprintf(line2Ko, sizeof(line2Ko), "최고 %d km/h  |  터보 Lv%d · 방탄 Lv%d",
                          static_cast<int>(std::round(spec.maxForwardSpeed * 3.6f)),
                          slot.engineUpgradeLevel, slot.armorUpgradeLevel);
            if (slot.owned) {
                std::snprintf(rightTag, sizeof(rightTag), "%s", pickText("SUMMON", "즉시 호출"));
                rightCol = Color{84, 228, 255, 255};
            } else {
                std::snprintf(rightTag, sizeof(rightTag), "$%d", spec.dealershipPrice);
            }
        } else if (phoneTab_ == 1) {
            const auto pid = static_cast<PropertyId>(i);
            const auto& pspec = getPropertySpec(pid);
            const bool owned = pl.ownedProperties[static_cast<std::size_t>(i)];
            std::snprintf(line1En, sizeof(line1En), "[%d] %s", i + 1, pspec.nameEn);
            std::snprintf(line1Ko, sizeof(line1Ko), "[%d] %s", i + 1, pspec.nameKo);
            std::snprintf(line2En, sizeof(line2En), "+$%d / 25s  |  %s", pspec.incomePerCycle, pspec.perkEn);
            std::snprintf(line2Ko, sizeof(line2Ko), "+$%d / 25초  |  %s", pspec.incomePerCycle, pspec.perkKo);
            if (owned) {
                std::snprintf(rightTag, sizeof(rightTag), "%s", pickText("OWNED", "보유 완료"));
                rightCol = Color{108, 242, 152, 255};
            } else {
                std::snprintf(rightTag, sizeof(rightTag), "$%d", pspec.price);
            }
        } else if (phoneTab_ == 2) {
            const auto oid = static_cast<OutfitId>(i);
            const auto& ospec = getOutfitSpec(oid);
            const bool owned = pl.ownedOutfits[static_cast<std::size_t>(i)];
            const bool eq = (pl.currentOutfit == oid);
            const int drPct = static_cast<int>(std::round((1.0f - ospec.damageTakenScale) * 100.0f));
            std::snprintf(line1En, sizeof(line1En), "[%d] %s", i + 1, ospec.nameEn);
            std::snprintf(line1Ko, sizeof(line1Ko), "[%d] %s", i + 1, ospec.nameKo);
            if (drPct > 0) {
                std::snprintf(line2En, sizeof(line2En), "Perk: -%d%% Incoming Damage", drPct);
                std::snprintf(line2Ko, sizeof(line2Ko), "착용 효과: 피격 데미지 -%d%% 감소", drPct);
            } else {
                std::snprintf(line2En, sizeof(line2En), "Classic Street Style");
                std::snprintf(line2Ko, sizeof(line2Ko), "기본 스트리트 스타일");
            }
            if (eq) {
                std::snprintf(rightTag, sizeof(rightTag), "%s", pickText("WORN", "착용중"));
                rightCol = Color{255, 218, 92, 255};
            } else if (owned) {
                std::snprintf(rightTag, sizeof(rightTag), "%s", pickText("WEAR", "착용하기"));
                rightCol = Color{92, 226, 255, 255};
            } else {
                std::snprintf(rightTag, sizeof(rightTag), "$%d", ospec.price);
            }
        } else if (i < 6) {
            struct SupplyRow { const char* nEn; const char* nKo; const char* dEn; const char* dKo; const char* cost; };
            const SupplyRow srows[6] = {
                {"[1] Heavy Armor Drop",    "[1] 긴급 방탄복 보급",      "Restore Armor 100%",   "방탄복 100% 즉시 충전",      "$250"},
                {"[2] Field Medkit Drop",   "[2] 긴급 의료 키트 보급",   "Restore Health 100%",  "체력 100% 즉시 회복",        "$150"},
                {"[3] Full Ammo Resupply",  "[3] 전 무기 탄약 보급",     "+4 Mags All Weapons",  "보유 무기 +4탄창 보충",      "$200"},
                {"[4] Carbine Rifle",       "[4] 돌격 카빈소총 보급",    "Auto Rifle + 150 Rds", "연사 소총 + 150발 즉시 지급", "$900"},
                {"[5] Combat MG",           "[5] 컴뱃 중기관총 보급",    "Heavy MG + 240 Rds",   "100발 탄창 중기관총 지급",   "$2,800"},
                {"[6] Tactical Launcher",   "[6] 전술 폭발 런처 보급",   "Explosive + 12 Rds",   "광역 폭발 런처 + 12발 지급", "$6,500"}
            };
            std::snprintf(line1En, sizeof(line1En), "%s", srows[i].nEn);
            std::snprintf(line1Ko, sizeof(line1Ko), "%s", srows[i].nKo);
            std::snprintf(line2En, sizeof(line2En), "%s", srows[i].dEn);
            std::snprintf(line2Ko, sizeof(line2Ko), "%s", srows[i].dKo);
            std::snprintf(rightTag, sizeof(rightTag), "%s", srows[i].cost);
        } else if (i == 6) {
            // Option [7]: SBPD Police Bribe / Dispatch Record Wipe!
            std::snprintf(line1En, sizeof(line1En), "[7] Clear Wanted Level");
            std::snprintf(line1Ko, sizeof(line1Ko), "[7] 경찰 수배 즉시 해제 브로커");
            if (pl.wantedLevel > 0) {
                std::snprintf(line2En, sizeof(line2En), "Wipe %d-Star SBPD Dispatch", pl.wantedLevel);
                std::snprintf(line2Ko, sizeof(line2Ko), "현재 수배 %d단계 기록 즉시 말소", pl.wantedLevel);
                std::snprintf(rightTag, sizeof(rightTag), "$%d", pl.wantedLevel * 400);
                rightCol = Color{102, 224, 255, 255};
            } else {
                std::snprintf(line2En, sizeof(line2En), "$400 per Wanted Star");
                std::snprintf(line2Ko, sizeof(line2Ko), "수배 별 1개당 $400 즉시 해제");
                std::snprintf(rightTag, sizeof(rightTag), "%s", pickText("CLEAN", "수배 없음"));
                rightCol = Color{154, 170, 190, 230};
            }
        } else {
            struct ExtraSupplyRow { const char* nEn; const char* nKo; const char* dEn; const char* dKo; const char* cost; };
            const ExtraSupplyRow erows[3] = {
                {"[8] Assault Shotgun", "[8] 전자동 드럼 샷건 보급", "Auto Drum + 48 Shells", "16발 드럼 연사샷건 지급",   "$1,700"},
                {"[9] Marksman Sniper", "[9] 전술 저격소총 보급",   "3.2x Scope + 24 Rds",   "정밀 저격총 + 스코프 지급", "$2,500"},
                {"[0] Vulcan Minigun",  "[0] 6열 개틀링 미니건 보급", "6-Barrel + 400 Rds",   "초고속 개틀링 + 400발 지급", "$5,400"}
            };
            const int idx = i - 7;
            std::snprintf(line1En, sizeof(line1En), "%s", erows[idx].nEn);
            std::snprintf(line1Ko, sizeof(line1Ko), "%s", erows[idx].nKo);
            std::snprintf(line2En, sizeof(line2En), "%s", erows[idx].dEn);
            std::snprintf(line2Ko, sizeof(line2Ko), "%s", erows[idx].dKo);
            std::snprintf(rightTag, sizeof(rightTag), "%s", erows[idx].cost);
        }

        const float y1Off = densePhone ? 3.5f : 7.0f;
        const float y2Off = densePhone ? 21.0f : 29.0f;
        const float f1 = densePhone ? 14.5f : 15.5f;
        const float f2 = densePhone ? 12.6f : 13.8f;
        drawUiText(pickText(line1En, line1Ko), px + 20.0f, ry + y1Off, f1, Color{240, 245, 252, 255});
        drawUiText(pickText(line2En, line2Ko), px + 20.0f, ry + y2Off, f2, Color{164, 180, 200, 240});

        const float tw = measureUiText(rightTag, f1);
        drawUiText(rightTag, px + pw - 20.0f - tw, ry + y1Off + 0.5f, f1, rightCol);
    }

    if (phoneTab_ == 0) {
        drawUiText(pickText("Keys [1-9, 0] Select   |   [P] Close Phone",
                            "숫자 [1~9, 0] 구매·호출   |   [P] 닫기"),
                   px + 18.0f, py + ph - 23.0f, 14.5f, Color{186, 198, 214, 245});
    } else if (phoneTab_ == 3) {
        drawUiText(pickText("Keys [1-9, 0] Select   |   [P] Close Phone",
                            "숫자 [1~9, 0] 보급·수배해제   |   [P] 닫기"),
                   px + 18.0f, py + ph - 23.0f, 14.5f, Color{186, 198, 214, 245});
    } else {
        drawUiText(pickText("Keys [1-6] Select   |   [P] Close Phone",
                            "숫자 [1~6] 구매·호출   |   [P] 휴대폰 닫기"),
                   px + 18.0f, py + ph - 24.0f, 15.0f, Color{186, 198, 214, 245});
    }
}

void HudRenderer::drawCustomStudioModal(const GameWorld& world, int screenW, int screenH) {
    (void)screenW;
    (void)screenH;

    const auto& pl = world.player();
    const auto& cc = pl.characterCustom;
    const auto& hc = pl.hudCustom;
    const ColorRGBA themeRgba = getHudThemeColorRGBA(hc.hudThemeIdx);
    const Color themeCol = toRayColor(themeRgba, 255);

    const float px = 22.0f;
    const float py = 18.0f;
    const float pw = 452.0f;
    const float ph = 504.0f;

    // Studio glass backdrop & themed neon bezel
    DrawRectangleRounded({px - 4.0f, py - 4.0f, pw + 8.0f, ph + 8.0f}, 0.05f, 8, Color{6, 9, 15, 238});
    DrawRectangleRounded({px, py, pw, ph}, 0.05f, 8, Color{13, 19, 30, 236});
    DrawRectangleRoundedLinesEx({px, py, pw, ph}, 0.05f, 8, 1.8f, toRayColor(themeRgba, 195));

    // Title bar
    drawUiText(pickText("CUSTOM STUDIO", "커스텀 스튜디오"), px + 16.0f, py + 11.0f, 21.0f, themeCol);
    const char* topHint = pickText("[Q/E] Tab  ·  [K] Close", "[Q/E] 탭 전환  ·  [K] 닫기");
    const float thw = measureUiText(topHint, 14.5f);
    drawUiText(topHint, px + pw - 16.0f - thw, py + 15.0f, 14.5f, Color{196, 210, 228, 240});

    // 4 Studio Category Tabs
    const char* tabsEn[4] = {"CHARACTER", "GUNSMITH", "VEHICLE", "HUD / AIM"};
    const char* tabsKo[4] = {"캐릭터", "총기 건스미스", "차량 커스텀", "조준·HUD"};
    const float tabGap = 6.0f;
    const float tabW = (pw - 32.0f - tabGap * 3.0f) * 0.25f;
    for (int t = 0; t < 4; ++t) {
        const float tx = px + 16.0f + static_cast<float>(t) * (tabW + tabGap);
        const bool active = (customTab_ == t);
        DrawRectangleRounded({tx, py + 42.0f, tabW, 29.0f}, 0.26f, 6,
                             active ? toRayColor(themeRgba, 235) : Color{26, 35, 50, 230});
        const char* tLabel = pickText(tabsEn[t], tabsKo[t]);
        const float lw = measureUiText(tLabel, 14.5f);
        drawUiText(tLabel, tx + (tabW - lw) * 0.5f, py + 48.0f, 14.5f,
                   active ? Color{10, 14, 20, 255} : Color{205, 216, 232, 240});
    }

    // Target / Context status strip
    DrawRectangleRounded({px + 16.0f, py + 79.0f, pw - 32.0f, 30.0f}, 0.22f, 6, Color{20, 29, 44, 232});
    char ctxBuf[144];
    Color ctxCol = Color{212, 228, 246, 248};

    const WeaponId gunWid =
        (pl.currentWeapon == WeaponId::Fists && pl.weapons[static_cast<std::size_t>(WeaponId::Pistol)].owned)
            ? WeaponId::Pistol
            : pl.currentWeapon;
    const auto& gunSpec = getWeaponSpec(gunWid);
    const auto& gunSlot = pl.weapons[static_cast<std::size_t>(gunWid)];
    const Vehicle* targetCar = world.customizationTargetVehicle();

    if (customTab_ == 0) {
        const auto& ospec = getOutfitSpec(pl.currentOutfit);
        if (cc.hairOrHatStyle == 4) {
            std::snprintf(ctxBuf, sizeof(ctxBuf), "%s  |  %s",
                          pickText(ospec.nameEn, ospec.nameKo),
                          pickText("Helmet Armor -8% DMG", "풀페이스 헬멧 피해 -8% 적용"));
            ctxCol = Color{112, 245, 162, 255};
        } else {
            std::snprintf(ctxBuf, sizeof(ctxBuf), "%s  |  %s",
                          pickText("Live 1st & 3rd Person Avatar", "1·3인칭 캐릭터 실시간 반영"),
                          pickText(ospec.nameEn, ospec.nameKo));
        }
    } else if (customTab_ == 1) {
        std::snprintf(ctxBuf, sizeof(ctxBuf), "%s: %s  (%d %s)",
                      pickText("TARGET", "대상 총기"),
                      pickText(gunSpec.nameEn, gunSpec.nameKo),
                      effectiveMagazineSize(gunWid, gunSlot.extendedMag),
                      pickText("rds", "발 탄창"));
        ctxCol = Color{255, 218, 98, 255};
    } else if (customTab_ == 2) {
        if (targetCar != nullptr) {
            const auto& vspec = getVehicleSpec(targetCar->kind);
            std::snprintf(ctxBuf, sizeof(ctxBuf), "%s  |  %s",
                          pickText(vspec.nameEn, vspec.nameKo),
                          targetCar->isPersonalCar
                              ? pickText("Saved to Personal Garage", "내 차고 영구 저장됨")
                              : pickText("Live Street Custom", "현장 즉시 풀 커스텀"));
            ctxCol = Color{108, 236, 255, 255};
        } else {
            std::snprintf(ctxBuf, sizeof(ctxBuf), "%s",
                          pickText("Enter a car or stand within 9.5m (or [P] Summon)",
                                   "차량 탑승 또는 근처(9.5m) 이동 시 개조 가능 ([P] 호출)"));
            ctxCol = Color{255, 168, 88, 255};
        }
    } else {
        std::snprintf(ctxBuf, sizeof(ctxBuf), "%s",
                      pickText("Live Reticle, Sniper Optic, Speedometer & Theme",
                               "실시간 조준선 · 속도계 단위 · 인터페이스 테마"));
    }
    drawUiText(ctxBuf, px + 26.0f, py + 86.0f, 14.2f, ctxCol);

    const float disc = world.tuningDiscountScale();
    auto discounted = [&](int basePrice) {
        return std::max(20, static_cast<int>(std::round(static_cast<float>(basePrice) * disc)));
    };

    const int rowCount = (customTab_ == 0 || customTab_ == 2) ? 8 : 6;
    const float rowStep = (rowCount == 8) ? 43.0f : 54.0f;
    const float rowH = (rowCount == 8) ? 38.5f : 46.0f;

    for (int i = 0; i < rowCount; ++i) {
        const float ry = py + 116.0f + static_cast<float>(i) * rowStep;
        DrawRectangleRounded({px + 16.0f, ry, pw - 32.0f, rowH}, 0.20f, 6, Color{22, 31, 46, 224});
        DrawRectangleRoundedLinesEx({px + 16.0f, ry, pw - 32.0f, rowH}, 0.20f, 6, 1.0f, Color{54, 74, 102, 130});

        char titleEn[96] = "";
        char titleKo[96] = "";
        char valEn[96] = "";
        char valKo[96] = "";
        char badge[40] = "";
        Color badgeCol = Color{124, 236, 162, 245};
        bool hasSwatch = false;
        Color swatchCol = WHITE;

        if (customTab_ == 0) {
            std::snprintf(badge, sizeof(badge), "%s", pickText("FREE", "무료"));
            switch (i) {
                case 0: {
                    static constexpr const char* kEn[7] = {
                        "Classic Short", "Slick Pompadour", "Spiky Mohawk", "Street Snapback",
                        "Aero Moto Helmet (-8% DMG)", "Cyber Neon Visor", "24K VIP Gold Crown"
                    };
                    static constexpr const char* kKo[7] = {
                        "클래식 숏컷", "슬릭 포마드", "스트리트 모호크", "스트리트 스냅백 캡",
                        "풀페이스 헬멧 (피해 -8%)", "사이버 네온 바이저", "24K VIP 골드 크라운"
                    };
                    std::snprintf(titleEn, sizeof(titleEn), "[1] Hair & Headgear (7 Models)");
                    std::snprintf(titleKo, sizeof(titleKo), "[1] 헤어 · 헤드기어 (7종)");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.hairOrHatStyle % 7]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.hairOrHatStyle % 7]);
                    break;
                }
                case 1: {
                    static constexpr const char* kEn[6] = {
                        "Jet Black", "Chestnut Brown", "Platinum Blonde", "Crimson Red", "Cyber Cyan", "Royal Gold"
                    };
                    static constexpr const char* kKo[6] = {
                        "제트 블랙", "체스트넛 브라운", "플래티넘 블론드", "크림슨 레드", "사이버 시안", "로얄 골드"
                    };
                    std::snprintf(titleEn, sizeof(titleEn), "[2] Hair / Cap Accent Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[2] 헤어 · 모자 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.hairColorIdx % 6]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.hairColorIdx % 6]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getPlayerHairColor(pl.currentOutfit, cc.hairColorIdx));
                    break;
                }
                case 2: {
                    static constexpr const char* kEn[4] = {"None", "Dark Shades", "Gold Aviator", "Cyan Tactical Goggles"};
                    static constexpr const char* kKo[4] = {"착용 안 함", "다크 선글라스", "골드 에비에이터", "사이버 전술 고글"};
                    std::snprintf(titleEn, sizeof(titleEn), "[3] Eyewear & Goggles");
                    std::snprintf(titleKo, sizeof(titleKo), "[3] 선글라스 · 전술 고글");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.glassesStyle % 4]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.glassesStyle % 4]);
                    break;
                }
                case 3: {
                    static constexpr const char* kEn[5] = {"Warm Ivory", "Natural Beige", "Golden Tan", "Olive Bronze", "Deep Espresso"};
                    static constexpr const char* kKo[5] = {"웜 아이보리", "내추럴 베이지", "골든 태닝", "올리브 브론즈", "딥 에스프레소"};
                    std::snprintf(titleEn, sizeof(titleEn), "[4] Skin Tone (1st & 3rd Person)");
                    std::snprintf(titleKo, sizeof(titleKo), "[4] 피부 톤 (1인칭 손 포함)");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.skinToneIdx % 5]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.skinToneIdx % 5]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getPlayerSkinColor(cc.skinToneIdx));
                    break;
                }
                case 4: {
                    static constexpr const char* kEn[9] = {
                        "Outfit Default", "Crimson Red", "Sapphire Blue", "Stealth Black",
                        "Arctic White", "Emerald Green", "Royal Gold", "Cyber Cyan", "Neon Violet"
                    };
                    static constexpr const char* kKo[9] = {
                        "의상 기본 컬러", "크림슨 레드", "사파이어 블루", "스텔스 블랙",
                        "아틱 화이트", "에메랄드 그린", "로얄 골드", "사이버 시안", "네온 바이올렛"
                    };
                    std::snprintf(titleEn, sizeof(titleEn), "[5] Jacket / Top Custom Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[5] 재킷 · 상의 커스텀 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.customTopColorIdx % 9]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.customTopColorIdx % 9]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getPlayerTopColor(pl.currentOutfit, cc.customTopColorIdx));
                    break;
                }
                case 5: {
                    static constexpr const char* kEn[7] = {
                        "Outfit Default", "Jet Black", "Indigo Denim", "Arctic White",
                        "Tactical Khaki", "Crimson Track", "Charcoal Grey"
                    };
                    static constexpr const char* kKo[7] = {
                        "의상 기본 컬러", "제트 블랙", "인디고 데님", "아틱 화이트",
                        "택티컬 카키", "크림슨 트랙", "차콜 그레이"
                    };
                    std::snprintf(titleEn, sizeof(titleEn), "[6] Pants Custom Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[6] 바지 · 하의 커스텀 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.customBottomColorIdx % 7]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.customBottomColorIdx % 7]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getPlayerBottomColor(pl.currentOutfit, cc.customBottomColorIdx));
                    break;
                }
                case 6: {
                    static constexpr const char* kEn[4] = {"None", "Tactical Backpack", "Heist Duffle Bag", "Cyber Neon Wing Pack"};
                    static constexpr const char* kKo[4] = {"착용 안 함", "전술 백팩", "하이스트 더플백", "사이버 네온 윙팩"};
                    std::snprintf(titleEn, sizeof(titleEn), "[7] 3D Back Gear Accessory");
                    std::snprintf(titleKo, sizeof(titleKo), "[7] 등 액세서리 (백팩·윙팩)");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[cc.backGearStyle % 4]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[cc.backGearStyle % 4]);
                    break;
                }
                case 7: {
                    const auto& ospec = getOutfitSpec(pl.currentOutfit);
                    std::snprintf(titleEn, sizeof(titleEn), "[8] Cycle Owned Base Outfit");
                    std::snprintf(titleKo, sizeof(titleKo), "[8] 베이스 의상 프리셋 교체");
                    std::snprintf(valEn, sizeof(valEn), "%s", ospec.nameEn);
                    std::snprintf(valKo, sizeof(valKo), "%s", ospec.nameKo);
                    std::snprintf(badge, sizeof(badge), "%s", pickText("CYCLE", "교체"));
                    badgeCol = themeCol;
                    break;
                }
            }
        } else if (customTab_ == 1) {
            switch (i) {
                case 0: {
                    static constexpr const char* kEn[6] = {
                        "Factory Black", "Brushed Chrome", "24K Royal Gold",
                        "Crimson Carbon", "Cyber Neon", "Arctic Whiteout"
                    };
                    static constexpr const char* kKo[6] = {
                        "팩토리 블랙", "브러시드 실버 크롬", "24K 로얄 골드",
                        "크림슨 카본", "사이버 네온", "아틱 화이트아웃"
                    };
                    std::snprintf(titleEn, sizeof(titleEn), "[1] Weapon Finish & Skin");
                    std::snprintf(titleKo, sizeof(titleKo), "[1] 총기 메탈릭 도색 · 스킨");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[gunSlot.skinIndex % 6]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[gunSlot.skinIndex % 6]);
                    std::snprintf(badge, sizeof(badge), "%s", pickText("FREE", "무료"));
                    hasSwatch = true;
                    swatchCol = toRayColor(getWeaponFinishMaterial(gunWid, gunSlot.skinIndex).primary);
                    break;
                }
                case 1: {
                    std::snprintf(titleEn, sizeof(titleEn), "[2] Tactical Barrel Suppressor");
                    std::snprintf(titleKo, sizeof(titleKo), "[2] 전술 소음기 (은밀 사격·집탄 향상)");
                    if (!weaponSupportsSuppressor(gunWid)) {
                        std::snprintf(valEn, sizeof(valEn), "Not supported on heavy ordnance");
                        std::snprintf(valKo, sizeof(valKo), "중화기 장착 불가");
                        std::snprintf(badge, sizeof(badge), "N/A");
                        badgeCol = Color{150, 160, 175, 220};
                    } else if (!gunSlot.suppressorOwned) {
                        std::snprintf(valEn, sizeof(valEn), "Stealth audio + -80%% police heat");
                        std::snprintf(valKo, sizeof(valKo), "소음 사격 · 경찰 수배열기 -80%% 감소");
                        std::snprintf(badge, sizeof(badge), "$%d", discounted(250));
                        badgeCol = Color{255, 214, 82, 255};
                    } else {
                        std::snprintf(valEn, sizeof(valEn), "%s", gunSlot.suppressor ? "Equipped (Stealth Active)" : "Detached (Loud)");
                        std::snprintf(valKo, sizeof(valKo), "%s", gunSlot.suppressor ? "장착됨 (은밀 소음 사격)" : "탈거됨 (일반 격발)");
                        std::snprintf(badge, sizeof(badge), "%s", gunSlot.suppressor ? "ON" : "OFF");
                        badgeCol = gunSlot.suppressor ? Color{96, 245, 168, 255} : Color{178, 190, 208, 240};
                    }
                    break;
                }
                case 2: {
                    std::snprintf(titleEn, sizeof(titleEn), "[3] Extended Magazine (+50%% Ammo)");
                    std::snprintf(titleKo, sizeof(titleKo), "[3] 대용량 확장 탄창 (+50%% 장탄수)");
                    const int extCap = effectiveMagazineSize(gunWid, true);
                    if (!gunSlot.extendedMagOwned) {
                        std::snprintf(valEn, sizeof(valEn), "Boost magazine %d -> %d rounds", gunSpec.magazineSize, extCap);
                        std::snprintf(valKo, sizeof(valKo), "탄창 용량 %d발 -> %d발 확장", gunSpec.magazineSize, extCap);
                        std::snprintf(badge, sizeof(badge), "$%d", discounted(200));
                        badgeCol = Color{255, 214, 82, 255};
                    } else {
                        std::snprintf(valEn, sizeof(valEn), "%s (%d rds)",
                                      gunSlot.extendedMag ? "Extended Drum/Box" : "Standard Mag",
                                      effectiveMagazineSize(gunWid, gunSlot.extendedMag));
                        std::snprintf(valKo, sizeof(valKo), "%s (%d발)",
                                      gunSlot.extendedMag ? "대용량 탄창 장착됨" : "기본 탄창 장착됨",
                                      effectiveMagazineSize(gunWid, gunSlot.extendedMag));
                        std::snprintf(badge, sizeof(badge), "%s", gunSlot.extendedMag ? "ON" : "OFF");
                        badgeCol = gunSlot.extendedMag ? Color{96, 245, 168, 255} : Color{178, 190, 208, 240};
                    }
                    break;
                }
                case 3: {
                    static constexpr const char* kEn[4] = {"OFF", "Tactical Red (+30% Hip)", "Spec-Ops Green (+30% Hip)", "Cyber Cyan (+30% Hip)"};
                    static constexpr const char* kKo[4] = {"꺼짐", "레드 레이저 (지향집탄 +30%)", "그린 레이저 (지향집탄 +30%)", "시안 레이저 (지향집탄 +30%)"};
                    std::snprintf(titleEn, sizeof(titleEn), "[4] 3D Tactical Laser Sight");
                    std::snprintf(titleKo, sizeof(titleKo), "[4] 3D 전술 레이저 사이트");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[gunSlot.laserSightColor % 4]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[gunSlot.laserSightColor % 4]);
                    std::snprintf(badge, sizeof(badge), "%s", (gunSlot.laserSightColor > 0) ? "ON" : "OFF");
                    if (gunSlot.laserSightColor > 0) {
                        hasSwatch = true;
                        swatchCol = toRayColor(getLaserSightColorRGBA(gunSlot.laserSightColor));
                    } else {
                        badgeCol = Color{178, 190, 208, 240};
                    }
                    break;
                }
                case 4: {
                    static constexpr const char* kEn[6] = {"Standard", "Solar Gold", "Cyber Cyan", "Crimson Red", "Plasma Purple", "Emerald Green"};
                    static constexpr const char* kKo[6] = {"기본 예광탄", "솔라 골드", "사이버 시안", "크림슨 레드", "플라즈마 퍼플", "에메랄드 그린"};
                    std::snprintf(titleEn, sizeof(titleEn), "[5] Bullet Tracer Beam Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[5] 사격 예광탄 궤적 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[gunSlot.tracerColorIdx % 6]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[gunSlot.tracerColorIdx % 6]);
                    std::snprintf(badge, sizeof(badge), "%s", pickText("FREE", "무료"));
                    hasSwatch = true;
                    swatchCol = toRayColor(getCustomTracerColorRGBA(gunWid, gunSlot.tracerColorIdx));
                    break;
                }
                case 5: {
                    static constexpr const char* kEn[5] = {"Default", "Crimson Red", "Emerald Green", "Cyber Cyan", "Solar Gold"};
                    static constexpr const char* kKo[5] = {"기본 조준경", "레드 도트·스코프", "그린 도트·스코프", "시안 도트·스코프", "골드 도트·스코프"};
                    std::snprintf(titleEn, sizeof(titleEn), "[6] 1st-Person Optic & Scope Tint");
                    std::snprintf(titleKo, sizeof(titleKo), "[6] 1인칭 도트 · 저격 스코프 색상");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[gunSlot.opticColorIdx % 5]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[gunSlot.opticColorIdx % 5]);
                    std::snprintf(badge, sizeof(badge), "%s", pickText("FREE", "무료"));
                    hasSwatch = true;
                    swatchCol = toRayColor(getOpticReticleColorRGBA(gunSlot.opticColorIdx, {96, 236, 255, 255}));
                    break;
                }
            }
        } else if (customTab_ == 2) {
            std::snprintf(badge, sizeof(badge), "%s", pickText("FREE", "무료"));
            static constexpr const char* kPaintEn[12] = {
                "Candy Crimson", "Sapphire Blue", "Stealth Matte", "Pearl White",
                "Solar Gold", "Emerald GT", "Ultraviolet", "Titanium Satin",
                "Mirror Chrome", "24K Gold Chrome", "Cyber Cyan", "Vice Rose"
            };
            static constexpr const char* kPaintKo[12] = {
                "캔디 크림슨", "사파이어 블루", "스텔스 매트 블랙", "펄 화이트",
                "솔라 골드", "에메랄드 그린", "울트라바이올렛", "티타늄 새틴",
                "미러 실버 크롬", "24K 골드 크롬", "사이버 네온 시안", "바이스 로즈"
            };
            const int pIdx = targetCar ? (targetCar->paintPresetIndex % 12) : 0;
            const int stIdx = targetCar ? (targetCar->stripeStyle % 5) : 0;
            const int rmIdx = targetCar ? (targetCar->rimStyle % 5) : 0;
            const int spIdx = targetCar ? (targetCar->spoilerStyle % 3) : 0;
            const int scIdx = targetCar ? (targetCar->stanceStyle % 4) : 0;
            const int hlIdx = targetCar ? (targetCar->headlightColorIdx % 5) : 0;
            const int nnIdx = targetCar ? (targetCar->neonPreset % 6) : 0;
            const int smIdx = targetCar ? (targetCar->driftSmokeColorIdx % 6) : 0;

            switch (i) {
                case 0: {
                    std::snprintf(titleEn, sizeof(titleEn), "[1] Body Paint & Chrome Wrap (12)");
                    std::snprintf(titleKo, sizeof(titleKo), "[1] 차체 도색 · 미러 크롬 랩핑 (12종)");
                    std::snprintf(valEn, sizeof(valEn), "%s", targetCar ? kPaintEn[pIdx] : "-");
                    std::snprintf(valKo, sizeof(valKo), "%s", targetCar ? kPaintKo[pIdx] : "차량 없음");
                    if (targetCar) {
                        hasSwatch = true;
                        swatchCol = toRayColor(targetCar->paintColor);
                    }
                    break;
                }
                case 1: {
                    static constexpr const char* kEn[5] = {"None", "White Dual GT", "Royal Gold GT", "Cyber Cyan GT", "Stealth Carbon"};
                    static constexpr const char* kKo[5] = {"없음", "화이트 듀얼 GT", "로얄 골드 스트라이프", "사이버 시안 스트라이프", "스텔스 카본 블랙"};
                    std::snprintf(titleEn, sizeof(titleEn), "[2] Racing Stripe Livery");
                    std::snprintf(titleKo, sizeof(titleKo), "[2] 듀얼 GT 레이싱 스트라이프");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[stIdx]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[stIdx]);
                    if (targetCar && stIdx > 0) {
                        hasSwatch = true;
                        swatchCol = toRayColor(getVehicleStripeColorRGBA(stIdx, targetCar->stripeColor));
                    }
                    break;
                }
                case 2: {
                    std::snprintf(titleEn, sizeof(titleEn), "[3] Forged Alloy Rims (5 Styles)");
                    std::snprintf(titleKo, sizeof(titleKo), "[3] 커스텀 단조 휠 컬러 (5종)");
                    std::snprintf(valEn, sizeof(valEn), "%s", rimStyleNameEn(rmIdx));
                    std::snprintf(valKo, sizeof(valKo), "%s", rimStyleNameKo(rmIdx));
                    break;
                }
                case 3: {
                    static constexpr const char* kEn[3] = {"Stock (No Wing)", "Carbon GT Wing (+2% Grip)", "Swan-Neck Time-Attack (+6% Grip)"};
                    static constexpr const char* kKo[3] = {"순정 (스포일러 없음)", "카본 GT 윙 (그립 +2%)", "스완넥 타임어택 대형 윙 (그립 +6%)"};
                    std::snprintf(titleEn, sizeof(titleEn), "[4] Rear Aero Wing & Downforce");
                    std::snprintf(titleKo, sizeof(titleKo), "[4] 리어 에어로 스포일러 · 대형 윙");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[spIdx]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[spIdx]);
                    break;
                }
                case 4: {
                    static constexpr const char* kEn[4] = {"Stock Height", "Street Lowered (-45mm)", "Slammed Camber (-82mm)", "Rally Lift (+95mm)"};
                    static constexpr const char* kKo[4] = {"순정 차고", "스트리트 로우다운 (-45mm)", "슬램드 마이너스 캠버 (-82mm)", "오프로드 리프트업 (+95mm)"};
                    std::snprintf(titleEn, sizeof(titleEn), "[5] Suspension Stance & Camber");
                    std::snprintf(titleKo, sizeof(titleKo), "[5] 서스펜션 차고 · 마이너스 캠버");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[scIdx]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[scIdx]);
                    break;
                }
                case 5: {
                    static constexpr const char* kEn[5] = {"Xenon White", "Cyber Cyan", "GT Yellow", "UV Purple", "Demon-Eye Red"};
                    static constexpr const char* kKo[5] = {"제논 화이트", "사이버 시안", "GT 레이싱 옐로우", "UV 네온 퍼플", "데몬아이 레드"};
                    std::snprintf(titleEn, sizeof(titleEn), "[6] Headlight & Night Beam Tint");
                    std::snprintf(titleKo, sizeof(titleKo), "[6] 헤드라이트 · 야간 빔 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[hlIdx]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[hlIdx]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getVehicleHeadlightColorRGBA(hlIdx, true));
                    break;
                }
                case 6: {
                    std::snprintf(titleEn, sizeof(titleEn), "[7] LED Neon Underglow");
                    std::snprintf(titleKo, sizeof(titleKo), "[7] 하부 LED 네온 언더글로우");
                    std::snprintf(valEn, sizeof(valEn), "%s", neonPresetNameEn(nnIdx));
                    std::snprintf(valKo, sizeof(valKo), "%s", neonPresetNameKo(nnIdx));
                    if (nnIdx > 0) {
                        hasSwatch = true;
                        swatchCol = toRayColor(getNeonPresetColor(nnIdx));
                    }
                    break;
                }
                case 7: {
                    static constexpr const char* kEn[6] = {"Classic White", "Cyber Cyan", "Crimson Red", "Royal Gold", "Violet", "Emerald"};
                    static constexpr const char* kKo[6] = {"클래식 화이트", "사이버 시안", "크림슨 레드", "로얄 골드", "바이올렛", "에메랄드"};
                    std::snprintf(titleEn, sizeof(titleEn), "[8] Drift Tire Smoke Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[8] 드리프트 타이어 연기 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[smIdx]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[smIdx]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getDriftSmokeColorRGBA(smIdx));
                    break;
                }
            }
        } else {
            std::snprintf(badge, sizeof(badge), "%s", pickText("SET", "적용"));
            badgeCol = themeCol;
            switch (i) {
                case 0: {
                    static constexpr const char* kEn[4] = {"Dynamic Cross + Dot", "Pinpoint Dot", "Tactical Circle + Dot", "Spec-Ops Chevron"};
                    static constexpr const char* kKo[4] = {"다이나믹 십자선 + 도트", "핀포인트 정밀 도트", "택티컬 서클 + 도트", "특수전 쉐브론 (^)"};
                    std::snprintf(titleEn, sizeof(titleEn), "[1] Crosshair Reticle Shape");
                    std::snprintf(titleKo, sizeof(titleKo), "[1] 조준선 모양 스타일 (4종)");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[hc.crosshairStyle % 4]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[hc.crosshairStyle % 4]);
                    break;
                }
                case 1: {
                    static constexpr const char* kEn[6] = {"Pure White", "Neon Green", "Cyber Cyan", "Solar Gold", "Hot Magenta", "Crimson Red"};
                    static constexpr const char* kKo[6] = {"퓨어 화이트", "네온 그린", "사이버 시안", "솔라 골드", "핫 마젠타", "크림슨 레드"};
                    std::snprintf(titleEn, sizeof(titleEn), "[2] Crosshair Color");
                    std::snprintf(titleKo, sizeof(titleKo), "[2] 조준선 컬러 (6종)");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[hc.crosshairColorIdx % 6]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[hc.crosshairColorIdx % 6]);
                    hasSwatch = true;
                    swatchCol = toRayColor(getCrosshairColorRGBA(hc.crosshairColorIdx));
                    break;
                }
                case 2: {
                    static constexpr const char* kEn[3] = {"Compact (80%)", "Standard (100%)", "Large (130%)"};
                    static constexpr const char* kKo[3] = {"소형 (80%)", "표준 (100%)", "대형 (130%)"};
                    std::snprintf(titleEn, sizeof(titleEn), "[3] Crosshair Size Scale");
                    std::snprintf(titleKo, sizeof(titleKo), "[3] 조준선 크기 배율");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[hc.crosshairScaleIdx % 3]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[hc.crosshairScaleIdx % 3]);
                    break;
                }
                case 3: {
                    std::snprintf(titleEn, sizeof(titleEn), "[4] High-Contrast Shadow Outline");
                    std::snprintf(titleKo, sizeof(titleKo), "[4] 조준선 명암 외곽 테두리");
                    std::snprintf(valEn, sizeof(valEn), "%s", hc.crosshairOutline ? "Enabled" : "Disabled");
                    std::snprintf(valKo, sizeof(valKo), "%s", hc.crosshairOutline ? "켜짐 (시인성 강화)" : "꺼짐 (미니멀)");
                    std::snprintf(badge, sizeof(badge), "%s", hc.crosshairOutline ? "ON" : "OFF");
                    break;
                }
                case 4: {
                    std::snprintf(titleEn, sizeof(titleEn), "[5] Speedometer Unit");
                    std::snprintf(titleKo, sizeof(titleKo), "[5] 디지털 속도계 단위");
                    std::snprintf(valEn, sizeof(valEn), "%s", hc.useMph ? "MPH (Miles / Hour)" : "KM/H (Kilometers / Hour)");
                    std::snprintf(valKo, sizeof(valKo), "%s", hc.useMph ? "MPH (마일 단위)" : "KM/H (킬로미터 단위)");
                    std::snprintf(badge, sizeof(badge), "%s", hc.useMph ? "MPH" : "KM/H");
                    break;
                }
                case 5: {
                    static constexpr const char* kEn[4] = {"Cyber Cyan", "Vice Neon Pink", "VIP Royal Gold", "Emerald Matrix"};
                    static constexpr const char* kKo[4] = {"사이버 시안", "바이스 네온 핑크", "VIP 로얄 골드", "에메랄드 그린"};
                    std::snprintf(titleEn, sizeof(titleEn), "[6] HUD Interface Accent Theme");
                    std::snprintf(titleKo, sizeof(titleKo), "[6] 인터페이스 HUD 테마 컬러");
                    std::snprintf(valEn, sizeof(valEn), "%s", kEn[hc.hudThemeIdx % 4]);
                    std::snprintf(valKo, sizeof(valKo), "%s", kKo[hc.hudThemeIdx % 4]);
                    hasSwatch = true;
                    swatchCol = themeCol;
                    break;
                }
            }
        }

        const float tOffY = (rowCount == 8) ? 3.5f : 6.0f;
        const float vOffY = (rowCount == 8) ? 20.5f : 25.0f;
        const float fTitle = (rowCount == 8) ? 14.5f : 15.2f;
        const float fVal = (rowCount == 8) ? 12.8f : 13.6f;

        drawUiText(pickText(titleEn, titleKo), px + 26.0f, ry + tOffY, fTitle, Color{242, 246, 252, 255});
        drawUiText(pickText(valEn, valKo), px + 28.0f, ry + vOffY, fVal, Color{166, 206, 238, 244});

        const float bw = measureUiText(badge, 14.0f);
        const float bx = px + pw - 26.0f - bw;
        drawUiText(badge, bx, ry + (rowH - 15.0f) * 0.5f, 14.0f, badgeCol);

        if (hasSwatch) {
            const Vector2 sc{bx - 16.0f, ry + rowH * 0.5f};
            DrawCircleV(sc, 7.2f, Color{8, 12, 18, 245});
            DrawCircleV(sc, 5.6f, swatchCol);
        }
    }

    const char* footTxt = (rowCount == 8)
                              ? pickText("Keys [1-8] Customize  |  [V] 1st/3rd View  |  Mouse Orbit",
                                         "숫자 [1~8] 즉시 적용  |  [V] 1·3인칭 전환  |  마우스 회전")
                              : pickText("Keys [1-6] Customize  |  [V] 1st/3rd View  |  Mouse Orbit",
                                         "숫자 [1~6] 즉시 적용  |  [V] 1·3인칭 전환  |  마우스 회전");
    drawUiText(footTxt, px + 18.0f, py + ph - 23.0f, 14.0f, Color{190, 204, 222, 245});
}

void HudRenderer::drawBannerToast(const GameWorld& world, int screenW, int screenH) {
    const float midX = static_cast<float>(screenW) * 0.5f;

    // Cinematic WASTED / BUSTED letterbox bar when incapacitated or arrested
    if (world.player().wasted) {
        const bool busted = world.player().lastDeathWasBusted;
        const bool redPulse = (std::fmod(GetTime() * 4.0, 1.0) < 0.5);
        const Color overlayCol = busted
                                     ? (redPulse ? Color{18, 36, 78, 128} : Color{68, 18, 34, 128})
                                     : Color{56, 10, 14, 132};
        DrawRectangle(0, 0, screenW, screenH, overlayCol);
        DrawRectangle(0, static_cast<int>(screenH * 0.12f), screenW, 92, Color{8, 12, 18, 210});
    }

    if (gfxToastTimer_ > 0.0f) {
        const float ga = clampf(gfxToastTimer_ * 2.0f, 0.0f, 1.0f);
        const char* gtxt = pickText(gfxToastEn_.c_str(), gfxToastKo_.c_str());
        const float gw = measureUiText(gtxt, 19.0f);
        const float gy = static_cast<float>(screenH) - 112.0f;
        DrawRectangleRounded({midX - gw * 0.5f - 16.0f, gy - 6.0f, gw + 32.0f, 32.0f},
                             0.35f, 6, Color{10, 16, 26, static_cast<unsigned char>(215.0f * ga)});
        DrawRectangleRoundedLinesEx({midX - gw * 0.5f - 16.0f, gy - 6.0f, gw + 32.0f, 32.0f},
                                    0.35f, 6, 1.4f, Color{86, 212, 255, static_cast<unsigned char>(220.0f * ga)});
        drawUiText(gtxt, midX - gw * 0.5f, gy, 19.0f, Color{128, 228, 255, static_cast<unsigned char>(255.0f * ga)});
    }

    const auto& b = world.banner();
    if (b.timer <= 0.0f) return;

    const float alpha = clampf(b.timer * 1.8f, 0.0f, 1.0f);
    const unsigned char aByte = static_cast<unsigned char>(alpha * 255.0f);
    const float y = static_cast<float>(screenH) * 0.145f;

    const char* title = pickText(b.titleEn.c_str(), b.titleKo.c_str());
    const float tw = measureUiText(title, 33.0f);
    drawUiText(title, midX - tw * 0.5f, y, 33.0f,
               Color{b.accent.r, b.accent.g, b.accent.b, aByte});

    const char* sub = pickText(b.subEn.c_str(), b.subKo.c_str());
    if (sub && sub[0] != '\0') {
        const float sw = measureUiText(sub, 20.0f);
        drawUiText(sub, midX - sw * 0.5f, y + 39.0f, 20.0f, Color{240, 244, 250, aByte});
    }
}

void HudRenderer::drawHelpOverlay(const GameWorld& world, int screenW, int screenH, bool cursorLocked) {
    (void)screenW;
    (void)screenH;
    const ColorRGBA themeRgba = getHudThemeColorRGBA(world.player().hudCustom.hudThemeIdx);
    if (!showHelp_) {
        drawUiText(pickText("[H] Controls  |  [K] Custom Studio", "[H] 조작키 도움말  |  [K] 커스텀 스튜디오"),
                   20.0f, 18.0f, 17.0f, Color{200, 214, 232, 215});
        return;
    }

    DrawRectangleRounded({18.0f, 16.0f, 462.0f, 290.0f}, 0.08f, 6, Color{10, 15, 24, 198});
    DrawRectangleRoundedLinesEx({18.0f, 16.0f, 462.0f, 290.0f}, 0.08f, 6, 1.2f, toRayColor(themeRgba, 125));
    drawUiText(pickText("OPEN CITY C++ v0.8 - CONTROLS", "OPEN CITY C++ v0.8 - 조작 안내"),
               30.0f, 24.0f, 18.5f, toRayColor(themeRgba, 255));

    const char* linesEn[11] = {
        "WASD : Walk / Drive & Steer (16 Vehicles)",
        "Mouse : 1:1 Aim (RMB Precision / Scope) & Shoot (LMB)",
        "V (or MMB) : Toggle 1st-Person / 3rd-Person View",
        "K : Custom Studio (Character / Gunsmith / Car / HUD)",
        "P : Smartphone (10 Cars / Realty / Style / Supply)",
        "B : Enter Shop (11 Weapons / Tune / Luxury Autos)",
        "F or E : Enter / Carjack / Exit / Start Mission",
        "Space : Jump (On-Foot) / Drift (Car) | Shift : Sprint",
        "1-0,- / Z·X / Wheel : 11 Weapons   |   R : Reload",
        "[ / ] : Mouse Sensitivity   |   I : Invert Y",
        "C : Rear | G : Police/EMS/Fire Siren | T : Time | F3 : GFX"
    };
    const char* linesKo[11] = {
        "WASD : 이동 / 16종 차량 주행 및 조향",
        "마우스 : 1:1 정밀조준 · 저격스코프(우클릭) · 사격(좌클릭)",
        "V (또는 휠클릭) : 1인칭(FPS·운전석) / 3인칭 시점 전환",
        "K : 커스텀 스튜디오 (캐릭터 · 총기 · 차량 · 조준선·HUD)",
        "P : 스마트폰 (10종 차량호출 · 부동산 · 중화기보급)",
        "B : 매장 이용 (11종 무기점 · 풀튜닝 공방 · 슈퍼카 전시장)",
        "F / E : 차량 탑승·탈취·하차 / 미션 시작",
        "Space : 점프(도보) · 드리프트(차량) | Shift : 질주·니트로",
        "1~0,- · Z/X · 휠 : 11종 무기교체  |  R : 재장전",
        "[ / ] : 마우스 감도 조절  |  I : 마우스 상하 반전",
        "C : 후방 | G : 경찰·구급·소방 사이렌 | T : 시간 | F3 : 그래픽"
    };

    for (int i = 0; i < 11; ++i) {
        const Color lineCol = (i == 3) ? toRayColor(themeRgba, 255) : Color{226, 232, 242, 242};
        drawUiText(pickText(linesEn[i], linesKo[i]),
                   30.0f, 50.0f + static_cast<float>(i) * 21.6f, 15.0f, lineCol);
    }

    if (!cursorLocked) {
        drawUiText(pickText("Click window or press [Tab] to lock mouse camera",
                            "화면을 클릭하거나 [Tab]을 누르면 마우스 시점이 활성화됩니다"),
                   30.0f, 312.0f, 16.5f, Color{255, 214, 88, 255});
    }
}

} // namespace oc
