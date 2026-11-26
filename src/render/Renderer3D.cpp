#include "render/Renderer3D.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace oc {

namespace {

inline std::uint32_t decodeUtf8Next(const char* s, std::size_t& idx, std::size_t len) {
    if (idx >= len) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[idx++]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && idx < len) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
    }
    if ((c0 & 0xF0) == 0xE0 && idx + 1 < len) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c2 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    }
    if ((c0 & 0xF8) == 0xF0 && idx + 2 < len) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c2 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c3 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    }
    return '?';
}

inline void fillColor4(float out[4], ColorRGBA c) {
    out[0] = static_cast<float>(c.r) / 255.0f;
    out[1] = static_cast<float>(c.g) / 255.0f;
    out[2] = static_cast<float>(c.b) / 255.0f;
    out[3] = static_cast<float>(c.a) / 255.0f;
}

inline ColorRGBA rimColorForStyle(int rimStyle) {
    switch (rimStyle) {
        case 1: return {228, 198, 128, 255}; // Champagne Gold
        case 2: return {36, 38, 44, 255};    // Gloss Black
        case 3: return {64, 218, 245, 255};  // Cyber Cyan
        case 4: return {214, 42, 54, 255};   // Crimson Track
        default: return {230, 235, 242, 255};// Machined Silver Alloy
    }
}

// Builds a curved Korean Giwa double-eave hip roof (unit 1x1x1 centered in X/Z, Y in [0..1])
GpuMesh buildGiwaRoofMesh(VulkanContext& vk) {
    MeshBuilder3D mb;
    const ColorRGBA kRoof{62, 68, 76, 255};
    const ColorRGBA kRidge{96, 102, 112, 255};
    const int steps = 5;
    for (int i = 0; i < steps; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(steps);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(steps);
        // Classic concave Korean curved roof pitch (upturned sweeping eaves!)
        const float y0 = std::pow(t0, 1.55f);
        const float y1 = std::pow(t1, 1.55f);
        const float rx0 = lerpf(0.62f, 0.28f, t0);
        const float rx1 = lerpf(0.62f, 0.28f, t1);
        const float rz0 = lerpf(0.62f, 0.04f, t0);
        const float rz1 = lerpf(0.62f, 0.04f, t1);
        // Upturn lift at outer corners
        const float lift0 = (i == 0) ? 0.07f : 0.0f;
        const float lift1 = (i == 0) ? 0.02f : 0.0f;

        mb.addQuad({-rx0, y0 + lift0,  rz0}, { rx0, y0 + lift0,  rz0},
                   { rx1, y1 + lift1,  rz1}, {-rx1, y1 + lift1,  rz1}, kRoof);
        mb.addQuad({ rx0, y0 + lift0, -rz0}, {-rx0, y0 + lift0, -rz0},
                   {-rx1, y1 + lift1, -rz1}, { rx1, y1 + lift1, -rz1}, kRoof);
        mb.addQuad({ rx0, y0 + lift0,  rz0}, { rx0, y0 + lift0, -rz0},
                   { rx1, y1 + lift1, -rz1}, { rx1, y1 + lift1,  rz1}, kRoof);
        mb.addQuad({-rx0, y0 + lift0, -rz0}, {-rx0, y0 + lift0,  rz0},
                   {-rx1, y1 + lift1,  rz1}, {-rx1, y1 + lift1, -rz1}, kRoof);
    }
    // Top roof ridge beam (용마루)
    mb.addBox({0.0f, 1.03f, 0.0f}, {0.62f, 0.09f, 0.11f}, kRidge);
    return mb.upload(vk, 26.0f);
}

} // namespace

Renderer3D::~Renderer3D() = default;

void Renderer3D::init(VulkanContext& vk, const CityMap& city,
                      const std::vector<std::uint8_t>& fontBytes,
                      const std::vector<std::uint8_t>& pbrAlbedoPng,
                      const std::vector<std::uint8_t>& pbrNormalRoughPng,
                      const std::vector<std::uint8_t>& streetAlbedoPng,
                      const std::vector<std::uint8_t>& streetNormalRoughPng,
                      const std::vector<std::uint8_t>& signs4kPng) {
    if (initialized_) return;
    if (!pbrAlbedoPng.empty() && !pbrNormalRoughPng.empty()) {
        int aw = 0, ah = 0, ac = 0;
        int nw = 0, nh = 0, nc = 0;
        stbi_uc* albPx = stbi_load_from_memory(pbrAlbedoPng.data(), static_cast<int>(pbrAlbedoPng.size()), &aw, &ah, &ac, 4);
        stbi_uc* nrmPx = stbi_load_from_memory(pbrNormalRoughPng.data(), static_cast<int>(pbrNormalRoughPng.size()), &nw, &nh, &nc, 4);
        if (albPx && nrmPx && aw > 0 && ah > 0 && nw > 0 && nh > 0) {
            vk.uploadPbrTexturesRGBA8(albPx, static_cast<std::uint32_t>(aw), static_cast<std::uint32_t>(ah),
                                      nrmPx, static_cast<std::uint32_t>(nw), static_cast<std::uint32_t>(nh));
        }
        if (albPx) stbi_image_free(albPx);
        if (nrmPx) stbi_image_free(nrmPx);
    }
    if (!streetAlbedoPng.empty() && !streetNormalRoughPng.empty()) {
        int aw = 0, ah = 0, ac = 0;
        int nw = 0, nh = 0, nc = 0;
        stbi_uc* sAlb = stbi_load_from_memory(streetAlbedoPng.data(), static_cast<int>(streetAlbedoPng.size()), &aw, &ah, &ac, 4);
        stbi_uc* sNrm = stbi_load_from_memory(streetNormalRoughPng.data(), static_cast<int>(streetNormalRoughPng.size()), &nw, &nh, &nc, 4);
        if (sAlb && sNrm && aw > 0 && ah > 0 && nw > 0 && nh > 0) {
            vk.uploadStreetPbrTexturesRGBA8(sAlb, static_cast<std::uint32_t>(aw), static_cast<std::uint32_t>(ah),
                                            sNrm, static_cast<std::uint32_t>(nw), static_cast<std::uint32_t>(nh));
        }
        if (sAlb) stbi_image_free(sAlb);
        if (sNrm) stbi_image_free(sNrm);
    }
    buildStaticPrimitives(vk);
    signAtlas4k_ = false;
    if (!signs4kPng.empty()) {
        int sw = 0, sh = 0, sc = 0;
        stbi_uc* sPx = stbi_load_from_memory(signs4kPng.data(), static_cast<int>(signs4kPng.size()), &sw, &sh, &sc, 4);
        if (sPx && sw >= 2048 && sh >= 2048) {
            if (vk.uploadSignAtlasRGBA8(sPx, static_cast<std::uint32_t>(sw), static_cast<std::uint32_t>(sh))) {
                signAtlas4k_ = true;
            }
        }
        if (sPx) stbi_image_free(sPx);
    }
    if (!signAtlas4k_) {
        buildSeoulSignAtlas(vk, fontBytes);
    }
    cars_.init(vk);
    buildSeoulMacroMeshes(vk, city);
    initialized_ = true;
}

void Renderer3D::buildSeoulSignAtlas(VulkanContext& vk, const std::vector<std::uint8_t>& fontBytes) {
    constexpr int kW = 1024;
    constexpr int kH = 1024;
    constexpr int kCellW = 256;
    constexpr int kCellH = 128;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kW) * static_cast<std::size_t>(kH) * 4u, 255u);

    auto putPx = [&](int x, int y, ColorRGBA c, float alpha) {
        if (x < 0 || x >= kW || y < 0 || y >= kH || alpha <= 0.001f) return;
        const std::size_t off = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kW) + static_cast<std::size_t>(x)) * 4u;
        const float a = std::clamp(alpha, 0.0f, 1.0f);
        rgba[off + 0] = static_cast<std::uint8_t>(lerpf(static_cast<float>(rgba[off + 0]), static_cast<float>(c.r), a));
        rgba[off + 1] = static_cast<std::uint8_t>(lerpf(static_cast<float>(rgba[off + 1]), static_cast<float>(c.g), a));
        rgba[off + 2] = static_cast<std::uint8_t>(lerpf(static_cast<float>(rgba[off + 2]), static_cast<float>(c.b), a));
        rgba[off + 3] = 255u;
    };

    auto fillRect = [&](int x0, int y0, int w, int h, ColorRGBA c) {
        const int x1 = std::min(kW, x0 + w);
        const int y1 = std::min(kH, y0 + h);
        for (int y = std::max(0, y0); y < y1; ++y) {
            for (int x = std::max(0, x0); x < x1; ++x) {
                const std::size_t off = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kW) + static_cast<std::size_t>(x)) * 4u;
                rgba[off + 0] = c.r;
                rgba[off + 1] = c.g;
                rgba[off + 2] = c.b;
                rgba[off + 3] = 255u;
            }
        }
    };

    auto drawBorder = [&](int x0, int y0, int w, int h, int t, ColorRGBA c) {
        fillRect(x0, y0, w, t, c);
        fillRect(x0, y0 + h - t, w, t, c);
        fillRect(x0, y0, t, h, c);
        fillRect(x0 + w - t, y0, t, h, c);
    };

    auto fillCircle = [&](int cx, int cy, int rad, ColorRGBA c) {
        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dx = -rad; dx <= rad; ++dx) {
                const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                const float cov = clampf(static_cast<float>(rad) + 0.5f - d, 0.0f, 1.0f);
                if (cov > 0.01f) putPx(cx + dx, cy + dy, c, cov);
            }
        }
    };

    stbtt_fontinfo font{};
    const bool hasFont = !fontBytes.empty() &&
                         stbtt_InitFont(&font, fontBytes.data(), stbtt_GetFontOffsetForIndex(fontBytes.data(), 0)) != 0;

    auto drawCenteredText = [&](int cx, int cy, int maxW, const char* text, float pxSize, ColorRGBA col, bool shadow) {
        if (!text || !text[0]) return;
        const std::size_t len = std::strlen(text);
        if (!hasFont) {
            // Crisp geometric fallback bars if no TTF font is available
            const int bw = std::min(maxW - 16, static_cast<int>(len) * 8);
            fillRect(cx - bw / 2, cy - static_cast<int>(pxSize * 0.35f), bw, std::max(4, static_cast<int>(pxSize * 0.55f)), col);
            return;
        }

        // Measure UTF-8 string width and shrink smoothly to fit inside maxW
        auto measureWidth = [&](float scale) -> float {
            std::size_t idx = 0;
            float w = 0.0f;
            while (idx < len) {
                const std::uint32_t cp = decodeUtf8Next(text, idx, len);
                if (cp == 0) break;
                int adv = 0, lsb = 0;
                stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &adv, &lsb);
                w += static_cast<float>(adv) * scale;
            }
            return w;
        };

        float scale = stbtt_ScaleForPixelHeight(&font, pxSize);
        float totalW = measureWidth(scale);
        if (totalW > static_cast<float>(maxW) && totalW > 1.0f) {
            scale *= static_cast<float>(maxW) / totalW;
            totalW = static_cast<float>(maxW);
        }

        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        const float baselineY = static_cast<float>(cy) + static_cast<float>(ascent + descent) * scale * 0.5f;
        float penX = static_cast<float>(cx) - totalW * 0.5f;

        std::size_t idx = 0;
        while (idx < len) {
            const std::uint32_t cp = decodeUtf8Next(text, idx, len);
            if (cp == 0) break;
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font, static_cast<int>(cp), &adv, &lsb);
            int gw = 0, gh = 0, xoff = 0, yoff = 0;
            unsigned char* bmp = stbtt_GetCodepointBitmap(&font, 0.0f, scale, static_cast<int>(cp), &gw, &gh, &xoff, &yoff);
            if (bmp) {
                const int gx0 = static_cast<int>(std::floor(penX)) + xoff;
                const int gy0 = static_cast<int>(std::floor(baselineY)) + yoff;
                for (int by = 0; by < gh; ++by) {
                    for (int bx = 0; bx < gw; ++bx) {
                        const float cov = static_cast<float>(bmp[by * gw + bx]) / 255.0f;
                        if (cov < 0.02f) continue;
                        if (shadow) {
                            putPx(gx0 + bx + 2, gy0 + by + 2, {10, 12, 16, 255}, cov * 0.55f);
                        }
                        putPx(gx0 + bx, gy0 + by, col, cov);
                        // Slight bold horizontal thickening (+1px) for razor-sharp street readability!
                        putPx(gx0 + bx + 1, gy0 + by, col, cov * 0.45f);
                    }
                }
                stbtt_FreeBitmap(bmp, nullptr);
            }
            penX += static_cast<float>(adv) * scale;
        }
    };

    struct SignDef {
        ColorRGBA bg;
        ColorRGBA border;
        ColorRGBA accent;
        ColorRGBA textCol;
        const char* line1;
        const char* line2;
        int style; // 0=gantry, 1=shop, 2=pharmacy/clinic cross, 3=metro, 4=bis, 5=apt, 6=busLed, 7=plate
    };

    // 32 Authentic Seoul 3D Signs packed into 4 cols x 8 rows (256x128 each -> 1024x1024 atlas)
    static constexpr SignDef kSigns[32] = {
        // Row 0 (Slots 0..3): Iconic Korean EXPRESSWAY Emerald-Green Gantries + Red/Blue Route Shields!
        {{ 12, 106,  68, 255}, {244, 248, 250, 255}, { 22,  92, 198, 255}, {252, 254, 255, 255}, "↑ 올림픽대로 · 잠실 →",  "반포 · 여의도 · 김포공항", 0},
        {{ 12, 106,  68, 255}, {244, 248, 250, 255}, { 22,  92, 198, 255}, {252, 254, 255, 255}, "↑ 경부고속도로 · 서초IC", "판교 · 수원 · 부산 방면",  0},
        {{ 12, 106,  68, 255}, {244, 248, 250, 255}, { 22,  92, 198, 255}, {252, 254, 255, 255}, "↑ 강변북로 · 성수 · 구리", "한남 · 용산 · 일산 방면", 0},
        {{ 12, 106,  68, 255}, {244, 248, 250, 255}, { 22,  92, 198, 255}, {252, 254, 255, 255}, "↑ 내부순환로 · 동부간선", "홍제 · 성산 · 강북 방면", 0},

        // Row 1 (Slots 4..7): Authentic Korean CITY BOULEVARD Royal-Blue Directional Gantries (시내 일반도로 청색 표지판)!
        {{ 18,  74, 148, 255}, {242, 246, 252, 255}, { 36, 132, 218, 255}, {252, 254, 255, 255}, "↑ 강남역 · 테헤란로 →",  "삼성역 · 코엑스 · 선릉",   8},
        {{ 18,  74, 148, 255}, {242, 246, 252, 255}, { 36, 132, 218, 255}, {252, 254, 255, 255}, "↑ 광화문 · 시청 · 종로", "경복궁 · 세종대로 · 을지로", 8},
        {{ 18,  74, 148, 255}, {242, 246, 252, 255}, { 36, 132, 218, 255}, {252, 254, 255, 255}, "↑ 반포대로 · 서초 · 여의도", "마포대교 · 63빌딩 · 홍대", 8},
        {{ 18,  74, 148, 255}, {242, 246, 252, 255}, { 36, 132, 218, 255}, {252, 254, 255, 255}, "↑ 잠실대로 · 송파 · 구로", "롯데월드 · 올림픽공원",   8},

        // Row 2..4 (Slots 8..19): Authentic Korean 1F & 2F Commercial Street Shopfronts!
        {{ 68,  42, 138, 255}, {124, 214,  88, 255}, {124, 214,  88, 255}, {252, 254, 255, 255}, "편의점 24시",            "도시락 · 커피 · ATM", 1},
        {{ 42,  36,  34, 255}, {224, 182,  92, 255}, {224, 182,  92, 255}, {248, 242, 226, 255}, "서울 커피 · 베이커리",   "ESPRESSO & BAKERY",  1},
        {{ 20, 128,  86, 255}, {238, 246, 242, 255}, {250, 252, 254, 255}, {252, 254, 255, 255}, "온누리 약국",            "처방조제 · 건강상담", 2},
        {{198,  56,  38, 255}, {248, 228, 178, 255}, {246, 186,  42, 255}, {255, 250, 238, 255}, "김밥 · 떡볶이 분식",     "포장 · 배달 가능",    1},

        {{196, 118,  26, 255}, { 64,  38,  18, 255}, {252, 232, 148, 255}, {255, 252, 242, 255}, "치킨 · 호프",            "생맥주 · 배달전문",   1},
        {{ 24,  70, 148, 255}, {228, 236, 246, 255}, { 68, 152, 232, 255}, {252, 254, 255, 255}, "한국은행 · ATM",         "365 자동화코너",      1},
        {{ 26, 104,  66, 255}, {242, 210,  76, 255}, {242, 210,  76, 255}, {252, 238, 136, 255}, "공인중개사 · 부동산",    "아파트 · 상가 · 매매",1},
        {{ 64,  48,  74, 255}, {228, 188, 198, 255}, {228, 188, 198, 255}, {252, 246, 250, 255}, "서울 헤어 · 미용실",     "컷 · 펌 · 컬러",      1},

        {{ 24, 112, 128, 255}, {234, 246, 248, 255}, {252, 254, 255, 255}, {252, 254, 255, 255}, "서울 내과 · 치과",       "건강검진 · 야간진료", 2},
        {{ 34,  82, 158, 255}, {236, 206,  92, 255}, {236, 206,  92, 255}, {252, 254, 255, 255}, "영어 · 수학 학원",       "초 · 중 · 고 입시전문",1},
        {{142,  34,  38, 255}, {232, 194, 108, 255}, {232, 194, 108, 255}, {254, 244, 222, 255}, "한우 · 삼겹살",          "참숯 화로구이 전문",  1},
        {{ 30, 122, 184, 255}, {236, 246, 254, 255}, {144, 216, 252, 255}, {252, 254, 255, 255}, "크린 세탁소",            "명품의류 · 당일세탁", 1},

        // Row 5 (Slots 20..23): Seoul Metro Subway Entrance Signboards
        {{ 32,  38,  46, 255}, { 42, 168,  78, 255}, { 42, 168,  78, 255}, {250, 252, 255, 255}, "2호선 강남역",           "SEOUL METRO LINE 2", 3},
        {{ 32,  38,  46, 255}, {192, 162,  66, 255}, {192, 162,  66, 255}, {250, 252, 255, 255}, "9호선 고속터미널",       "SEOUL METRO LINE 9", 3},
        {{ 32,  38,  46, 255}, {236, 118,  28, 255}, {236, 118,  28, 255}, {250, 252, 255, 255}, "3호선 경복궁역",         "SEOUL METRO LINE 3", 3},
        {{ 32,  38,  46, 255}, { 36,  98, 198, 255}, { 36,  98, 198, 255}, {250, 252, 255, 255}, "1호선 시청역",           "SEOUL METRO LINE 1", 3},

        // Row 6 (Slots 24..27): Seoul BIS Arrival Board, Korean Apartment Dong Panels, Blue Bus LED Header
        {{ 16,  28,  46, 255}, { 54, 168, 242, 255}, { 28, 116, 214, 255}, {255, 214,  86, 255}, "서울 버스 도착안내",     "472 곧도착 · 5511 3분", 4},
        {{ 42,  64,  98, 255}, {228, 234, 242, 255}, {198, 174, 112, 255}, {250, 252, 255, 255}, "101동",                  "래미안 · SEOUL",      5},
        {{ 54,  60,  72, 255}, {224, 228, 234, 255}, {178, 152, 108, 255}, {250, 252, 255, 255}, "105동",                  "자이 · SEOUL",        5},
        {{ 14,  18,  24, 255}, { 38,  98, 206, 255}, { 38,  98, 206, 255}, {255, 208,  64, 255}, "서울 간선 472",          "신사 · 강남 · 반포",  6},

        // Row 7 (Slots 28..31): Green Bus LED Header + Authentic Korean Vehicle License Plates!
        {{ 14,  18,  24, 255}, { 44, 152,  76, 255}, { 44, 152,  76, 255}, {126, 246, 138, 255}, "서울 지선 5511",         "서울대 · 관악 · 반포", 6},
        {{244, 246, 250, 255}, { 32,  36,  44, 255}, { 26,  86, 186, 255}, { 20,  22,  28, 255}, "123가 4568",             "",                    7},
        {{244, 186,  32, 255}, { 32,  34,  38, 255}, { 32,  34,  38, 255}, { 20,  22,  26, 255}, "서울 34바 8247",         "",                    7},
        {{244, 246, 250, 255}, { 22,  46, 104, 255}, { 22,  46, 104, 255}, { 20,  42,  98, 255}, "경찰 112",               "",                    7}
    };

    for (int slot = 0; slot < 32; ++slot) {
        const int col = slot % 4;
        const int row = slot / 4;
        const int x0 = col * kCellW;
        const int y0 = row * kCellH;
        const SignDef& sd = kSigns[slot];

        fillRect(x0, y0, kCellW, kCellH, sd.bg);

        if (sd.style == 0) {
            // Korean Expressway Gantry (녹색 고속도로 표지판): double white frame + authentic Red/Blue Expressway route shield!
            drawBorder(x0 + 5, y0 + 5, kCellW - 10, kCellH - 10, 3, sd.border);
            // Iconic Korean Expressway shield: red upper cap + royal-blue lower badge + crisp white route number!
            fillRect(x0 + 14, y0 + 70, 38, 10, {214, 38, 46, 255});
            fillRect(x0 + 14, y0 + 80, 38, 24, sd.accent);
            drawBorder(x0 + 14, y0 + 70, 38, 34, 2, sd.border);
            const char* routeNum = (slot == 0) ? "88" : ((slot == 1) ? "1" : ((slot == 2) ? "70" : "30"));
            drawCenteredText(x0 + 33, y0 + 91, 34, routeNum, 18.0f, sd.border, false);
            drawCenteredText(x0 + kCellW / 2, y0 + 42, kCellW - 26, sd.line1, 30.0f, sd.textCol, true);
            drawCenteredText(x0 + kCellW / 2 + 20, y0 + 88, kCellW - 78, sd.line2, 20.0f, {226, 242, 234, 255}, true);
        } else if (sd.style == 8) {
            // Korean City Boulevard Gantry (청색 시내 간선도로 표지판): crisp white frame + top accent divider + clear white text!
            drawBorder(x0 + 5, y0 + 5, kCellW - 10, kCellH - 10, 3, sd.border);
            fillRect(x0 + 18, y0 + 68, kCellW - 36, 3, {168, 204, 244, 255});
            drawCenteredText(x0 + kCellW / 2, y0 + 42, kCellW - 26, sd.line1, 29.0f, sd.textCol, true);
            drawCenteredText(x0 + kCellW / 2, y0 + 92, kCellW - 28, sd.line2, 20.0f, {220, 234, 250, 255}, true);
        } else if (sd.style == 1 || sd.style == 2) {
            // Korean Commercial Shopfront: clean architectural border, bottom accent strip, optional pharmacy/clinic cross!
            drawBorder(x0 + 4, y0 + 4, kCellW - 8, kCellH - 8, 3, sd.border);
            fillRect(x0 + 7, y0 + kCellH - 14, kCellW - 14, 7, sd.accent);
            int textCenterX = x0 + kCellW / 2;
            int textMaxW = kCellW - 24;
            if (sd.style == 2) {
                // Crisp medical / pharmacy cross badge on left
                fillRect(x0 + 20, y0 + 34, 32, 10, sd.accent);
                fillRect(x0 + 31, y0 + 23, 10, 32, sd.accent);
                textCenterX = x0 + kCellW / 2 + 22;
                textMaxW = kCellW - 68;
            }
            drawCenteredText(textCenterX, y0 + 46, textMaxW, sd.line1, 34.0f, sd.textCol, true);
            drawCenteredText(x0 + kCellW / 2, y0 + 89, kCellW - 28, sd.line2, 19.0f, {236, 240, 244, 255}, false);
        } else if (sd.style == 3) {
            // Seoul Metro Subway Sign: colored left circular line emblem + horizontal line bar + crisp station name!
            fillRect(x0, y0, kCellW, 12, sd.accent);
            fillRect(x0, y0 + kCellH - 10, kCellW, 10, sd.accent);
            fillCircle(x0 + 40, y0 + 62, 24, sd.accent);
            const char* numStr = (slot == 20) ? "2" : ((slot == 21) ? "9" : ((slot == 22) ? "3" : "1"));
            drawCenteredText(x0 + 40, y0 + 62, 34, numStr, 30.0f, {255, 255, 255, 255}, false);
            drawCenteredText(x0 + 152, y0 + 50, 174, sd.line1, 30.0f, sd.textCol, true);
            drawCenteredText(x0 + 152, y0 + 88, 174, sd.line2, 16.0f, {206, 214, 224, 255}, false);
        } else if (sd.style == 4) {
            // Seoul BIS Digital Bus Information Screen
            drawBorder(x0 + 4, y0 + 4, kCellW - 8, kCellH - 8, 3, sd.border);
            fillRect(x0 + 7, y0 + 7, kCellW - 14, 38, sd.accent);
            drawCenteredText(x0 + kCellW / 2, y0 + 26, kCellW - 22, sd.line1, 22.0f, {252, 254, 255, 255}, false);
            drawCenteredText(x0 + kCellW / 2, y0 + 76, kCellW - 22, sd.line2, 23.0f, sd.textCol, false);
            fillRect(x0 + 18, y0 + 104, kCellW - 36, 8, {42, 186, 124, 255});
        } else if (sd.style == 5) {
            // Korean Apartment Complex Shear-Wall Dong Number Panel (101동 / 105동)
            drawBorder(x0 + 5, y0 + 5, kCellW - 10, kCellH - 10, 3, sd.accent);
            fillRect(x0 + 14, y0 + 82, kCellW - 28, 3, sd.accent);
            drawCenteredText(x0 + kCellW / 2, y0 + 46, kCellW - 24, sd.line1, 46.0f, sd.textCol, true);
            drawCenteredText(x0 + kCellW / 2, y0 + 102, kCellW - 28, sd.line2, 21.0f, {226, 230, 238, 255}, false);
        } else if (sd.style == 6) {
            // Seoul City Bus Front LED Destination Matrix
            drawBorder(x0 + 4, y0 + 4, kCellW - 8, kCellH - 8, 4, sd.border);
            fillRect(x0 + 10, y0 + 10, 14, kCellH - 20, sd.accent);
            drawCenteredText(x0 + kCellW / 2 + 8, y0 + 48, kCellW - 36, sd.line1, 34.0f, sd.textCol, false);
            drawCenteredText(x0 + kCellW / 2 + 8, y0 + 92, kCellW - 36, sd.line2, 21.0f, {242, 246, 250, 255}, false);
        } else if (sd.style == 7) {
            // Authentic Korean License Plate (White Private + Blue KOR left band / Yellow Taxi / Police)
            drawBorder(x0 + 5, y0 + 5, kCellW - 10, kCellH - 10, 4, sd.border);
            int textCx = x0 + kCellW / 2;
            int textW = kCellW - 24;
            if (slot == 29) {
                // Modern Korean holographic blue left KOR strip!
                fillRect(x0 + 9, y0 + 9, 28, kCellH - 18, sd.accent);
                drawCenteredText(x0 + 23, y0 + 92, 24, "KOR", 13.0f, {248, 250, 254, 255}, false);
                textCx = x0 + kCellW / 2 + 14;
                textW = kCellW - 52;
            } else if (slot == 31) {
                // Seoul Police crisp blue header accent strip
                fillRect(x0 + 9, y0 + 9, kCellW - 18, 16, sd.accent);
            }
            drawCenteredText(textCx, y0 + 66, textW, sd.line1, 52.0f, sd.textCol, false);
        }
    }

    vk.uploadSignAtlasRGBA8(rgba.data(), static_cast<std::uint32_t>(kW), static_cast<std::uint32_t>(kH));
}

void Renderer3D::destroyRealChunkGpu(VulkanContext& vk, RealChunkGpu& ch) {
    vk.destroyMesh(ch.groundMesh);
    vk.destroyMesh(ch.hillsideMesh);
    vk.destroyMesh(ch.roadsMesh);
    vk.destroyMesh(ch.roadPaintMesh);
    vk.destroyMesh(ch.sidewalksMesh);
    vk.destroyMesh(ch.bridgePiersMesh);
    vk.destroyMesh(ch.bldgAptMesh);
    vk.destroyMesh(ch.bldgGlassMesh);
    vk.destroyMesh(ch.bldgGraniteMesh);
    vk.destroyMesh(ch.bldgBrickMesh);
    vk.destroyMesh(ch.bldgStuccoMesh);
    vk.destroyMesh(ch.bldgHanokMesh);
    vk.destroyMesh(ch.bldgHeritageMesh);
    vk.destroyMesh(ch.bldgRoofMesh);
}

void Renderer3D::shutdown(VulkanContext& vk) {
    if (!initialized_) return;
    vk.waitIdle();
    cars_.destroy(vk);

    for (auto& kv : realChunks_) {
        destroyRealChunkGpu(vk, kv.second);
    }
    realChunks_.clear();

    vk.destroyMesh(cubeMesh_);
    vk.destroyMesh(cylMesh_);
    vk.destroyMesh(coneMesh_);
    vk.destroyMesh(sphereMesh_);
    vk.destroyMesh(skyBoxMesh_);
    vk.destroyMesh(treeTrunkMesh_);
    vk.destroyMesh(foliageMesh_);
    vk.destroyMesh(pineMesh_);
    vk.destroyMesh(ringMesh_);
    vk.destroyMesh(giwaRoofMesh_);
    vk.destroyMesh(capsuleLimbMesh_);
    vk.destroyMesh(torsoMesh_);

    vk.destroyMesh(ddareungiRackMesh_);
    vk.destroyMesh(seoulLampMesh_);
    vk.destroyMesh(seoulDoubleLampMesh_);
    vk.destroyMesh(highwayLampMesh_);
    vk.destroyMesh(koreanSignalMesh_);
    vk.destroyMesh(busShelterFrameMesh_);
    vk.destroyMesh(busShelterGlassMesh_);
    vk.destroyMesh(subwayCanopyFrameMesh_);
    vk.destroyMesh(subwayCanopyGlassMesh_);
    vk.destroyMesh(pedGuardrailMesh_);
    vk.destroyMesh(medianFenceMesh_);
    vk.destroyMesh(bollardMesh_);
    vk.destroyMesh(benchMesh_);
    vk.destroyMesh(hydrantMesh_);
    vk.destroyMesh(trashBinMesh_);
    vk.destroyMesh(utilityBoxMesh_);
    vk.destroyMesh(streetKioskMesh_);
    vk.destroyMesh(streetKioskGlassMesh_);
    vk.destroyMesh(treeGrateMesh_);

    vk.destroyMesh(realTerrainMesh_);
    vk.destroyMesh(realWaterMesh_);
    vk.destroyMesh(realParksMesh_);
    vk.destroyMesh(hanRiverWaterMesh_);
    vk.destroyMesh(hanEmbankmentMesh_);
    vk.destroyMesh(tributaryWaterMesh_);
    vk.destroyMesh(riversideExpresswayMesh_);
    vk.destroyMesh(mountainForestMesh_);
    vk.destroyMesh(mountainGraniteMesh_);
    vk.destroyMesh(bridgeDecksMesh_);
    vk.destroyMesh(bridgeConcreteMesh_);
    vk.destroyMesh(bridgeSteelMesh_);
    vk.destroyMesh(landmarkGlassMesh_);
    vk.destroyMesh(landmarkStoneMesh_);
    vk.destroyMesh(landmarkMetalMesh_);
    vk.destroyMesh(landmarkGiwaMesh_);
    vk.destroyMesh(landmarkGlowMesh_);
    vk.destroyMesh(banpoFountainMesh_);
    distantSkyline_.clear();
    initialized_ = false;
}

void Renderer3D::setSettings(const GraphicsSettings& s) {
    settings_ = s;
    settings_.clampAndSyncPreset();
}

void Renderer3D::setQuality(GraphicsQuality q) {
    settings_.applyPreset(q);
}

void Renderer3D::cycleQuality() {
    const int cur = static_cast<int>(settings_.preset);
    const int next = (cur >= 3) ? 0 : (cur + 1);
    settings_.applyPreset(static_cast<GraphicsQuality>(next));
}

const char* Renderer3D::qualityNameEn() const {
    return GraphicsSettings::presetNameEn(settings_.preset);
}

const char* Renderer3D::qualityNameKo() const {
    return GraphicsSettings::presetNameKo(settings_.preset);
}

void Renderer3D::buildStaticPrimitives(VulkanContext& vk) {
    // Unit Cube [-0.5 .. +0.5]
    {
        MeshBuilder3D mb;
        mb.addBox({0, 0, 0}, {1, 1, 1});
        cubeMesh_ = mb.upload(vk, 15.0f);
    }
    // Unit Skybox Cube
    {
        MeshBuilder3D mb;
        mb.addBox({0, 0, 0}, {2.0f, 2.0f, 2.0f});
        skyBoxMesh_ = mb.upload(vk, 0.0f);
    }
    // Unit Cylinder (radius 0.5, height 1.0, Y in [0..1])
    {
        MeshBuilder3D mb;
        mb.addCylinderY({0, 0, 0}, 0.5f, 0.5f, 1.0f, 16);
        cylMesh_ = mb.upload(vk, 40.0f);
    }
    // Unit Cone (radius 0.5, height 1.0, Y in [0..1])
    {
        MeshBuilder3D mb;
        mb.addCylinderY({0, 0, 0}, 0.5f, 0.005f, 1.0f, 16);
        coneMesh_ = mb.upload(vk, 40.0f);
    }
    // Unit Sphere (radius 0.5)
    {
        MeshBuilder3D mb;
        mb.addSphere({0, 0, 0}, 0.5f, 10, 16);
        sphereMesh_ = mb.upload(vk, 65.0f);
    }
    // Smooth tapered anatomical limb segment (origin at top joint Y=0, extends down to Y=-1!)
    {
        MeshBuilder3D mb;
        const int slices = 12;
        const float yRings[7] = { 0.16f,  0.09f,  0.00f, -0.50f, -1.00f, -1.09f, -1.14f};
        const float rRings[7] = { 0.04f,  0.40f,  0.50f,  0.44f,  0.38f,  0.30f,  0.04f};
        for (int r = 0; r < 6; ++r) {
            for (int s = 0; s < slices; ++s) {
                const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
                const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
                const Vec3 p00{std::cos(a0) * rRings[r],     yRings[r],     std::sin(a0) * rRings[r]};
                const Vec3 p10{std::cos(a1) * rRings[r],     yRings[r],     std::sin(a1) * rRings[r]};
                const Vec3 p11{std::cos(a1) * rRings[r + 1], yRings[r + 1], std::sin(a1) * rRings[r + 1]};
                const Vec3 p01{std::cos(a0) * rRings[r + 1], yRings[r + 1], std::sin(a0) * rRings[r + 1]};
                mb.addQuad(p00, p01, p11, p10);
            }
        }
        capsuleLimbMesh_ = mb.upload(vk, 65.0f);
    }
    // Sculpted anatomical human torso (natural jacket drape over waist, broader shoulders, Y in [0..1]!)
    {
        MeshBuilder3D mb;
        const int slices = 16;
        const float yLvl[5]  = {0.00f, 0.24f, 0.58f, 0.88f, 1.00f};
        const float rxLvl[5] = {0.40f, 0.39f, 0.45f, 0.48f, 0.34f};
        const float rzLvl[5] = {0.42f, 0.41f, 0.49f, 0.54f, 0.36f};
        for (int r = 0; r < 4; ++r) {
            for (int s = 0; s < slices; ++s) {
                const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
                const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
                const Vec3 p00{std::cos(a0) * rxLvl[r],     yLvl[r],     std::sin(a0) * rzLvl[r]};
                const Vec3 p10{std::cos(a1) * rxLvl[r],     yLvl[r],     std::sin(a1) * rzLvl[r]};
                const Vec3 p11{std::cos(a1) * rxLvl[r + 1], yLvl[r + 1], std::sin(a1) * rzLvl[r + 1]};
                const Vec3 p01{std::cos(a0) * rxLvl[r + 1], yLvl[r + 1], std::sin(a0) * rzLvl[r + 1]};
                mb.addQuad(p00, p01, p11, p10);
            }
        }
        mb.addSphere({0.0f, 0.90f, 0.0f}, 0.36f, 6, 12);
        torsoMesh_ = mb.upload(vk, 60.0f);
    }
    // Multi-Branch Architectural 3D Tree Trunk (flared root collar + curved main bole + 5 structural boughs!)
    {
        MeshBuilder3D mb;
        auto addBranchSeg = [&](Vec3 a, Vec3 b, float r0, float r1, int segs) {
            const Vec3 d = b - a;
            const float len = d.length();
            if (len < 1e-4f) return;
            const Vec3 yAx = d / len;
            const Vec3 ref = (std::abs(yAx.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 xAx = Vec3{ref.y * yAx.z - ref.z * yAx.y, ref.z * yAx.x - ref.x * yAx.z, ref.x * yAx.y - ref.y * yAx.x}.normalized();
            const Vec3 zAx = Vec3{yAx.y * xAx.z - yAx.z * xAx.y, yAx.z * xAx.x - yAx.x * xAx.z, yAx.x * xAx.y - yAx.y * xAx.x}.normalized();
            for (int i = 0; i < segs; ++i) {
                const float t0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
                const float t1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
                const Vec3 o0 = xAx * std::cos(t0) + zAx * std::sin(t0);
                const Vec3 o1 = xAx * std::cos(t1) + zAx * std::sin(t1);
                mb.addQuad(a + o0 * r0, a + o1 * r0, b + o1 * r1, b + o0 * r1, {195, 185, 175, 255});
            }
        };
        // Root flare -> mid-trunk -> upper fork (height normalized 0..1)
        addBranchSeg({0.0f, 0.00f, 0.0f}, {0.02f, 0.22f, -0.01f}, 0.26f, 0.19f, 10);
        addBranchSeg({0.02f, 0.22f, -0.01f}, {-0.02f, 0.62f, 0.02f}, 0.19f, 0.14f, 10);
        addBranchSeg({-0.02f, 0.62f, 0.02f}, {0.01f, 0.98f, -0.02f}, 0.14f, 0.08f, 8);
        // 5 reaching architectural limbs fanning out into the canopy!
        addBranchSeg({-0.01f, 0.58f, 0.01f}, { 0.42f, 1.06f,  0.22f}, 0.10f, 0.045f, 7);
        addBranchSeg({-0.01f, 0.62f, 0.02f}, {-0.40f, 1.08f,  0.18f}, 0.095f, 0.042f, 7);
        addBranchSeg({ 0.00f, 0.68f, 0.00f}, { 0.16f, 1.12f, -0.42f}, 0.09f, 0.040f, 7);
        addBranchSeg({ 0.00f, 0.72f, 0.00f}, {-0.26f, 1.15f, -0.32f}, 0.085f, 0.038f, 7);
        addBranchSeg({ 0.01f, 0.76f, 0.01f}, { 0.08f, 1.16f,  0.38f}, 0.085f, 0.038f, 7);
        treeTrunkMesh_ = mb.upload(vk, 48.0f);
    }
    // Organic Multi-Layered Seoul Street Tree Crown (28 irregular dappled leaf sprays — ZERO lollipop spheres!)
    {
        MeshBuilder3D mb;
        struct LeafClump { Vec3 p; float rx, ry, rz; ColorRGBA c; };
        const LeafClump clumps[28] = {
            // Shaded lower/interior bough pads (flattened horizontally like real Ginkgo & Cherry branches!)
            {{ 0.00f,  0.04f,  0.00f}, 0.42f, 0.24f, 0.40f, {154, 174, 142, 255}},
            {{-0.52f, -0.04f,  0.24f}, 0.36f, 0.21f, 0.34f, {162, 182, 150, 255}},
            {{ 0.54f, -0.06f, -0.22f}, 0.35f, 0.20f, 0.33f, {160, 180, 148, 255}},
            {{ 0.18f, -0.02f,  0.52f}, 0.34f, 0.21f, 0.34f, {166, 186, 152, 255}},
            {{-0.24f, -0.02f, -0.50f}, 0.35f, 0.20f, 0.35f, {164, 184, 152, 255}},
            {{ 0.54f,  0.06f,  0.32f}, 0.33f, 0.22f, 0.31f, {176, 196, 162, 255}},
            {{-0.50f,  0.08f, -0.30f}, 0.34f, 0.22f, 0.32f, {178, 198, 164, 255}},
            // Mid-canopy irregular leafy lobes
            {{ 0.58f,  0.22f,  0.08f}, 0.38f, 0.25f, 0.34f, {196, 214, 178, 255}},
            {{-0.56f,  0.26f,  0.06f}, 0.39f, 0.26f, 0.35f, {202, 220, 184, 255}},
            {{ 0.08f,  0.24f,  0.56f}, 0.36f, 0.24f, 0.36f, {194, 212, 176, 255}},
            {{-0.10f,  0.28f, -0.54f}, 0.37f, 0.25f, 0.36f, {198, 216, 180, 255}},
            {{ 0.38f,  0.30f,  0.42f}, 0.34f, 0.24f, 0.32f, {208, 224, 188, 255}},
            {{-0.40f,  0.32f,  0.38f}, 0.35f, 0.24f, 0.33f, {212, 228, 192, 255}},
            {{ 0.40f,  0.28f, -0.40f}, 0.35f, 0.24f, 0.33f, {204, 220, 186, 255}},
            {{-0.38f,  0.30f, -0.40f}, 0.34f, 0.23f, 0.32f, {206, 222, 188, 255}},
            {{ 0.00f,  0.36f,  0.00f}, 0.44f, 0.28f, 0.42f, {214, 228, 194, 255}},
            // Outer reaching spray tufts (creates authentic broken organic tree silhouette against sky!)
            {{ 0.72f,  0.16f, -0.18f}, 0.26f, 0.18f, 0.24f, {188, 208, 172, 255}},
            {{-0.70f,  0.20f,  0.22f}, 0.26f, 0.18f, 0.25f, {194, 214, 178, 255}},
            {{ 0.22f,  0.18f,  0.70f}, 0.25f, 0.17f, 0.25f, {190, 210, 174, 255}},
            {{-0.24f,  0.22f, -0.68f}, 0.26f, 0.18f, 0.24f, {192, 212, 176, 255}},
            // Sunlit upper crown sprays
            {{ 0.42f,  0.48f, -0.16f}, 0.34f, 0.23f, 0.32f, {228, 240, 204, 255}},
            {{-0.38f,  0.52f,  0.20f}, 0.35f, 0.24f, 0.33f, {234, 244, 208, 255}},
            {{ 0.16f,  0.54f,  0.36f}, 0.33f, 0.23f, 0.32f, {232, 242, 206, 255}},
            {{-0.18f,  0.50f, -0.34f}, 0.34f, 0.23f, 0.32f, {226, 238, 202, 255}},
            {{ 0.22f,  0.66f,  0.04f}, 0.30f, 0.22f, 0.28f, {244, 250, 218, 255}},
            {{-0.20f,  0.68f, -0.10f}, 0.29f, 0.21f, 0.28f, {246, 252, 222, 255}},
            {{ 0.02f,  0.80f,  0.06f}, 0.28f, 0.21f, 0.26f, {252, 255, 232, 255}},
            {{-0.04f,  0.62f,  0.26f}, 0.27f, 0.20f, 0.26f, {240, 248, 214, 255}}
        };
        for (const auto& cl : clumps) {
            // Ellipsoidal leaf-pad cluster (flattened & organic)
            const int rings = 4, slices = 7;
            for (int r = 0; r < rings; ++r) {
                const float phi0 = kPi * static_cast<float>(r) / static_cast<float>(rings);
                const float phi1 = kPi * static_cast<float>(r + 1) / static_cast<float>(rings);
                for (int s = 0; s < slices; ++s) {
                    const float th0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
                    const float th1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
                    auto pt = [&](float phi, float th) -> Vec3 {
                        return {cl.p.x + std::sin(phi) * std::cos(th) * cl.rx,
                                cl.p.y + std::cos(phi) * cl.ry,
                                cl.p.z + std::sin(phi) * std::sin(th) * cl.rz};
                    };
                    mb.addQuad(pt(phi0, th0), pt(phi1, th0), pt(phi1, th1), pt(phi0, th1), cl.c);
                }
            }
        }
        foliageMesh_ = mb.upload(vk, 55.0f);
    }
    // Authentic Layered Korean Red Pine Tree (소나무 — organic spreading asymmetric needle boughs, ZERO toy cones!)
    {
        MeshBuilder3D mb;
        struct PinePad { Vec3 p; float rx, ry, rz; ColorRGBA c; };
        const PinePad pads[18] = {
            {{ 0.00f, 0.14f,  0.00f}, 0.56f, 0.18f, 0.52f, {168, 190, 170, 255}},
            {{ 0.46f, 0.10f,  0.16f}, 0.36f, 0.13f, 0.32f, {178, 200, 178, 255}},
            {{-0.48f, 0.14f, -0.18f}, 0.38f, 0.14f, 0.34f, {174, 196, 176, 255}},
            {{ 0.12f, 0.12f, -0.46f}, 0.34f, 0.13f, 0.36f, {172, 194, 174, 255}},
            {{-0.18f, 0.16f,  0.44f}, 0.34f, 0.13f, 0.35f, {176, 198, 178, 255}},
            {{ 0.00f, 0.38f,  0.00f}, 0.48f, 0.16f, 0.46f, {192, 212, 192, 255}},
            {{ 0.38f, 0.36f, -0.22f}, 0.33f, 0.12f, 0.31f, {202, 222, 200, 255}},
            {{-0.38f, 0.40f,  0.20f}, 0.34f, 0.13f, 0.32f, {206, 224, 204, 255}},
            {{ 0.22f, 0.42f,  0.34f}, 0.30f, 0.12f, 0.30f, {198, 218, 198, 255}},
            {{-0.24f, 0.38f, -0.34f}, 0.31f, 0.12f, 0.30f, {196, 216, 196, 255}},
            {{ 0.00f, 0.62f,  0.00f}, 0.40f, 0.15f, 0.38f, {218, 234, 216, 255}},
            {{ 0.28f, 0.60f,  0.14f}, 0.28f, 0.11f, 0.26f, {226, 240, 222, 255}},
            {{-0.26f, 0.64f, -0.14f}, 0.28f, 0.11f, 0.26f, {228, 242, 224, 255}},
            {{ 0.08f, 0.66f, -0.26f}, 0.25f, 0.10f, 0.26f, {224, 238, 222, 255}},
            {{-0.12f, 0.64f,  0.24f}, 0.26f, 0.11f, 0.25f, {222, 236, 220, 255}},
            {{ 0.00f, 0.84f,  0.00f}, 0.30f, 0.13f, 0.28f, {238, 248, 234, 255}},
            {{ 0.14f, 0.88f, -0.08f}, 0.22f, 0.10f, 0.20f, {244, 252, 240, 255}},
            {{-0.08f, 0.96f,  0.06f}, 0.19f, 0.10f, 0.19f, {250, 255, 246, 255}}
        };
        for (const auto& pd : pads) {
            const int rings = 4, slices = 7;
            for (int r = 0; r < rings; ++r) {
                const float phi0 = kPi * static_cast<float>(r) / static_cast<float>(rings);
                const float phi1 = kPi * static_cast<float>(r + 1) / static_cast<float>(rings);
                for (int s = 0; s < slices; ++s) {
                    const float th0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
                    const float th1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
                    auto pt = [&](float phi, float th) -> Vec3 {
                        return {pd.p.x + std::sin(phi) * std::cos(th) * pd.rx,
                                pd.p.y + std::cos(phi) * pd.ry,
                                pd.p.z + std::sin(phi) * std::sin(th) * pd.rz};
                    };
                    mb.addQuad(pt(phi0, th0), pt(phi1, th0), pt(phi1, th1), pt(phi0, th1), pd.c);
                }
            }
        }
        pineMesh_ = mb.upload(vk, 55.0f);
    }
    // Hollow Ground Ring (for POIs / Mission Checkpoints / GPS beacons)
    {
        MeshBuilder3D mb;
        const int segs = 32;
        const float rIn = 0.84f, rOut = 1.0f;
        for (int i = 0; i < segs; ++i) {
            const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
            const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            mb.addQuad({std::cos(a0) * rIn,  0.0f, std::sin(a0) * rIn},
                       {std::cos(a0) * rOut, 0.0f, std::sin(a0) * rOut},
                       {std::cos(a1) * rOut, 0.0f, std::sin(a1) * rOut},
                       {std::cos(a1) * rIn,  0.0f, std::sin(a1) * rIn});
        }
        ringMesh_ = mb.upload(vk, 30.0f);
    }
    giwaRoofMesh_ = buildGiwaRoofMesh(vk);

    // ========================================================================
    // HIGH-DETAIL SCULPTED KOREAN STREET ARCHITECTURE MESHES (v6.2.0)
    // Pre-baked once at init -> hyper-realistic silhouettes at 1 draw call each!
    // ========================================================================
    auto addTube = [](MeshBuilder3D& mb, const Vec3& a, const Vec3& b, float r0, float r1, int segs, ColorRGBA col) {
        const Vec3 d = b - a;
        const float len = d.length();
        if (len < 1e-4f) return;
        const Vec3 yAx = d / len;
        const Vec3 ref = (std::abs(yAx.y) < 0.92f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 xAx = Vec3{ref.y * yAx.z - ref.z * yAx.y, ref.z * yAx.x - ref.x * yAx.z, ref.x * yAx.y - ref.y * yAx.x}.normalized();
        const Vec3 zAx = Vec3{yAx.y * xAx.z - yAx.z * xAx.y, yAx.z * xAx.x - yAx.x * xAx.z, yAx.x * xAx.y - yAx.y * xAx.x}.normalized();
        for (int i = 0; i < segs; ++i) {
            const float t0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
            const float t1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            const Vec3 o0 = xAx * std::cos(t0) + zAx * std::sin(t0);
            const Vec3 o1 = xAx * std::cos(t1) + zAx * std::sin(t1);
            mb.addQuad(a + o0 * r0, a + o1 * r0, b + o1 * r1, b + o0 * r1, col);
        }
    };
    // Torus ring centered at `c` lying in the Y-Z plane (bicycle wheel tire & alloy rim!)
    auto addWheelYZ = [&](MeshBuilder3D& mb, const Vec3& c, float R, float rTube, int majSegs, int minSegs, ColorRGBA col) {
        for (int i = 0; i < majSegs; ++i) {
            const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(majSegs);
            const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(majSegs);
            const float ca0 = std::cos(a0), sa0 = std::sin(a0);
            const float ca1 = std::cos(a1), sa1 = std::sin(a1);
            for (int j = 0; j < minSegs; ++j) {
                const float b0 = kTwoPi * static_cast<float>(j) / static_cast<float>(minSegs);
                const float b1 = kTwoPi * static_cast<float>(j + 1) / static_cast<float>(minSegs);
                auto pt = [&](float ca, float sa, float b) -> Vec3 {
                    const float rr = R + rTube * std::cos(b);
                    return {c.x + rTube * std::sin(b), c.y + rr * sa, c.z + rr * ca};
                };
                mb.addQuad(pt(ca0, sa0, b0), pt(ca1, sa1, b0), pt(ca1, sa1, b1), pt(ca0, sa0, b1), col);
            }
        }
    };

    // 1. Sculpted 따릉이 (Seoul Public Bike-Share Station): bright white bikes + signature Seoul-Green baskets/fenders/docks + QR kiosk!
    {
        MeshBuilder3D mb;
        const ColorRGBA kSeoulGreen{46, 184, 86, 255};     // Signature 따릉이 vibrant lime-emerald green
        const ColorRGBA kGreenDark{28, 138, 66, 255};
        const ColorRGBA kBikeWhite{246, 248, 252, 255};
        const ColorRGBA kAlloy{196, 202, 210, 255};
        const ColorRGBA kSteelDark{66, 72, 82, 255};
        const ColorRGBA kTire{28, 30, 34, 255};
        const ColorRGBA kSeat{42, 44, 50, 255};
        const ColorRGBA kScreen{24, 38, 52, 255};

        // Low beveled stainless ground base rail + bolted anchor footplate
        mb.addBox({-0.10f, 0.032f, -0.52f}, {3.95f, 0.064f, 0.28f}, {182, 188, 196, 255});
        mb.addBox({-0.10f, 0.072f, -0.52f}, {3.85f, 0.024f, 0.16f}, kSteelDark);

        // Sleek Seoul Bike White & Lime-Green Info Sign / QR Kiosk Totem (x = -2.22m)
        mb.addBox({-2.22f, 0.06f, -0.45f}, {0.50f, 0.12f, 0.44f}, kSteelDark);
        mb.addBox({-2.22f, 0.98f, -0.45f}, {0.40f, 1.76f, 0.22f}, kBikeWhite);
        mb.addBox({-2.22f, 1.80f, -0.44f}, {0.42f, 0.24f, 0.26f}, kSeoulGreen);
        mb.addBox({-2.22f, 1.30f, -0.33f}, {0.28f, 0.40f, 0.03f}, kScreen);     // Recessed QR/rental display
        mb.addBox({-2.22f, 0.72f, -0.33f}, {0.30f, 0.34f, 0.02f}, kSeoulGreen); // 따릉이 green graphic panel
        mb.addBox({-2.22f, 1.94f, -0.42f}, {0.46f, 0.05f, 0.32f}, kSteelDark);  // Weather canopy hood

        // 4 Sculpted 따릉이 Bicycles docked neatly into white-and-green upright cradles!
        for (int bi = 0; bi < 4; ++bi) {
            const float bx = -1.35f + static_cast<float>(bi) * 0.90f;
            // Upright white/stainless docking bollard + signature green QR smart-lock head
            mb.addBox({bx, 0.34f, -0.52f}, {0.16f, 0.62f, 0.13f}, kBikeWhite);
            mb.addBox({bx, 0.68f, -0.50f}, {0.19f, 0.14f, 0.17f}, kSeoulGreen);
            mb.addBox({bx, 0.50f, -0.45f}, {0.10f, 0.18f, 0.02f}, kGreenDark);
            addTube(mb, {bx - 0.075f, 0.06f, -0.48f}, {bx - 0.075f, 0.46f, -0.35f}, 0.017f, 0.017f, 6, kAlloy);
            addTube(mb, {bx + 0.075f, 0.06f, -0.48f}, {bx + 0.075f, 0.46f, -0.35f}, 0.017f, 0.017f, 6, kAlloy);

            const Vec3 fwC{bx, 0.32f, -0.26f}; // Front wheel center (docked)
            const Vec3 rwC{bx, 0.32f,  0.76f}; // Rear wheel center
            const Vec3 bbC{bx, 0.30f,  0.31f}; // Bottom bracket / crankset
            const Vec3 htBot{bx, 0.64f, -0.14f};
            const Vec3 htTop{bx, 0.84f, -0.10f};
            const Vec3 stTop{bx, 0.80f,  0.41f};

            // Front & Rear tires + bright alloy rims + hubs + 6 spokes
            for (const Vec3& wc : {fwC, rwC}) {
                addWheelYZ(mb, wc, 0.295f, 0.028f, 14, 6, kTire);
                addWheelYZ(mb, wc, 0.260f, 0.014f, 14, 5, kAlloy);
                addTube(mb, {wc.x - 0.040f, wc.y, wc.z}, {wc.x + 0.040f, wc.y, wc.z}, 0.022f, 0.022f, 6, kSteelDark);
                for (int sp = 0; sp < 6; ++sp) {
                    const float ang = kPi * static_cast<float>(sp) / 6.0f;
                    const float dy = std::sin(ang) * 0.255f;
                    const float dz = std::cos(ang) * 0.255f;
                    addTube(mb, {wc.x, wc.y - dy, wc.z - dz}, {wc.x, wc.y + dy, wc.z + dz}, 0.0055f, 0.0055f, 4, kAlloy);
                }
            }
            // Iconic bright matte-white step-through curved 따릉이 frame + green logo tube!
            addTube(mb, htTop, htBot, 0.028f, 0.028f, 7, kBikeWhite);
            addTube(mb, htBot, {bx, 0.35f, 0.14f}, 0.026f, 0.026f, 7, kBikeWhite);
            addTube(mb, {bx, 0.35f, 0.14f}, bbC, 0.026f, 0.028f, 7, kBikeWhite);
            addTube(mb, {bx, 0.68f, -0.12f}, {bx, 0.45f, 0.33f}, 0.020f, 0.020f, 6, kSeoulGreen);
            addTube(mb, bbC, stTop, 0.024f, 0.022f, 7, kBikeWhite);
            for (float s : {-1.0f, 1.0f}) {
                addTube(mb, htBot + Vec3{s * 0.036f, 0.0f, 0.0f}, fwC + Vec3{s * 0.044f, 0.0f, 0.0f}, 0.015f, 0.012f, 5, kBikeWhite);
                addTube(mb, bbC + Vec3{s * 0.032f, 0.0f, 0.0f}, rwC + Vec3{s * 0.046f, 0.0f, 0.0f}, 0.014f, 0.011f, 5, kBikeWhite);
                addTube(mb, stTop + Vec3{s * 0.028f, -0.05f, 0.0f}, rwC + Vec3{s * 0.046f, 0.0f, 0.0f}, 0.012f, 0.010f, 5, kBikeWhite);
            }
            // Full white & Seoul-Green enclosed chain guard + green-accented rear mudguard!
            mb.addBox({bx + 0.046f, 0.32f, 0.53f}, {0.026f, 0.11f, 0.46f}, kBikeWhite);
            mb.addBox({bx + 0.056f, 0.32f, 0.53f}, {0.012f, 0.05f, 0.38f}, kSeoulGreen);
            addTube(mb, {bx, 0.63f, -0.45f}, {bx, 0.65f, -0.07f}, 0.028f, 0.028f, 5, kGreenDark);
            addTube(mb, {bx, 0.63f,  0.55f}, {bx, 0.63f,  0.96f}, 0.028f, 0.028f, 5, kSeoulGreen);
            // Seatpost + sculpted dark saddle + swept chrome handlebars + grips
            addTube(mb, stTop, {bx, 0.93f, 0.43f}, 0.016f, 0.015f, 6, kAlloy);
            mb.addBox({bx, 0.95f, 0.44f}, {0.17f, 0.058f, 0.26f}, kSeat);
            addTube(mb, htTop, {bx, 0.99f, -0.06f}, 0.017f, 0.016f, 6, kAlloy);
            addTube(mb, {bx - 0.17f, 1.00f, -0.05f}, {bx + 0.17f, 1.00f, -0.05f}, 0.014f, 0.014f, 6, kAlloy);
            addTube(mb, {bx - 0.17f, 1.00f, -0.05f}, {bx - 0.25f, 0.98f,  0.04f}, 0.018f, 0.018f, 6, kSeat);
            addTube(mb, {bx + 0.17f, 1.00f, -0.05f}, {bx + 0.25f, 0.98f,  0.04f}, 0.018f, 0.018f, 6, kSeat);
            // Iconic hollow open-top vibrant Seoul-Green front basket + crisp white front 따릉이 plate!
            const Vec3 bkC{bx, 0.83f, -0.32f};
            mb.addBox({bkC.x, bkC.y - 0.11f, bkC.z}, {0.32f, 0.026f, 0.24f}, kGreenDark);
            mb.addBox({bkC.x, bkC.y, bkC.z - 0.11f}, {0.32f, 0.22f, 0.024f}, kSeoulGreen);
            mb.addBox({bkC.x, bkC.y, bkC.z + 0.11f}, {0.32f, 0.22f, 0.024f}, kSeoulGreen);
            mb.addBox({bkC.x - 0.15f, bkC.y, bkC.z}, {0.024f, 0.22f, 0.24f}, kSeoulGreen);
            mb.addBox({bkC.x + 0.15f, bkC.y, bkC.z}, {0.024f, 0.22f, 0.24f}, kSeoulGreen);
            mb.addBox({bkC.x, bkC.y, bkC.z - 0.125f}, {0.20f, 0.11f, 0.014f}, kBikeWhite);
        }
        ddareungiRackMesh_ = mb.upload(vk, 35.0f);
    }

    // 2A. Sculpted 8.2m Seoul Public-Design Single-Arm LED Streetlamp (서울 표준 가로등 — 기와진회색!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kGiwaDark{68, 74, 84, 255};      // 서울 기와진회색 base plinth
        const ColorRGBA kGiwaPole{92, 98, 108, 255};     // Powder-coated metallic titanium-gray mast
        const ColorRGBA kSilverTrim{146, 154, 164, 255}; // Brushed aluminum decorative rings & head
        const ColorRGBA kTagBlue{32, 96, 186, 255};      // Seoul streetlight ID management plate
        const ColorRGBA kWhite{238, 242, 246, 255};

        // Grounded stepped octagonal cast-aluminum base pedestal + service hatch + flange collar
        mb.addCylinderY({0.0f, -0.18f, 0.0f}, 0.21f, 0.18f, 0.28f, 8, kGiwaDark);
        mb.addCylinderY({0.0f,  0.10f, 0.0f}, 0.17f, 0.135f, 0.88f, 8, kGiwaDark);
        mb.addBox({0.145f, 0.56f, 0.0f}, {0.02f, 0.36f, 0.11f}, kSilverTrim); // Inspection door plate
        mb.addCylinderY({0.0f,  0.98f, 0.0f}, 0.155f, 0.11f, 0.10f, 10, kSilverTrim);
        // Slender tapered circular steel pole (1.08m .. 7.70m) + mid decorative ring + blue management tag
        mb.addCylinderY({0.0f, 1.08f, 0.0f}, 0.098f, 0.062f, 6.62f, 10, kGiwaPole);
        mb.addCylinderY({0.0f, 2.35f, 0.0f}, 0.096f, 0.096f, 0.05f, 10, kSilverTrim);
        mb.addBox({0.088f, 2.58f, 0.0f}, {0.025f, 0.22f, 0.08f}, kTagBlue);
        mb.addBox({0.098f, 2.62f, 0.0f}, {0.012f, 0.08f, 0.065f}, kWhite);
        // Upper finial cap + sweeping curved Seoul arched cantilever arm reaching +2.35m over road!
        mb.addCylinderY({0.0f, 7.62f, 0.0f}, 0.082f, 0.082f, 0.14f, 10, kSilverTrim);
        mb.addSphere({0.0f, 7.80f, 0.0f}, 0.070f, 6, 10, kSilverTrim);
        const Vec3 arc[6] = {
            {0.00f, 7.66f, 0.0f},
            {0.34f, 8.04f, 0.0f},
            {0.86f, 8.26f, 0.0f},
            {1.42f, 8.32f, 0.0f},
            {1.94f, 8.24f, 0.0f},
            {2.22f, 8.12f, 0.0f}
        };
        for (int i = 0; i < 5; ++i) {
            const float r0 = lerpf(0.056f, 0.036f, static_cast<float>(i) / 5.0f);
            const float r1 = lerpf(0.056f, 0.036f, static_cast<float>(i + 1) / 5.0f);
            addTube(mb, arc[i], arc[i + 1], r0, r1, 8, kGiwaPole);
        }
        // Subtle architectural tension brace beneath arm root
        addTube(mb, {0.04f, 7.12f, 0.0f}, {0.82f, 8.20f, 0.0f}, 0.020f, 0.016f, 6, kSilverTrim);
        // Slim aerodynamic Seoul die-cast silver-gray LED luminaire housing + cooling fins
        mb.addBox({2.38f, 8.10f, 0.0f}, {0.78f, 0.085f, 0.30f}, kSilverTrim);
        mb.addBox({2.32f, 8.16f, 0.0f}, {0.58f, 0.045f, 0.22f}, kGiwaDark);
        seoulLampMesh_ = mb.upload(vk, 42.0f);
    }

    // 2B. Sculpted 8.6m Seoul Twin-Arm Boulevard Central-Median LED Streetlamp (테헤란로/강남대로 양팔형 가로등!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kGiwaDark{68, 74, 84, 255};
        const ColorRGBA kGiwaPole{92, 98, 108, 255};
        const ColorRGBA kSilverTrim{146, 154, 164, 255};
        mb.addCylinderY({0.0f, -0.18f, 0.0f}, 0.23f, 0.19f, 0.28f, 8, kGiwaDark);
        mb.addCylinderY({0.0f,  0.10f, 0.0f}, 0.18f, 0.145f, 0.96f, 8, kGiwaDark);
        mb.addCylinderY({0.0f,  1.06f, 0.0f}, 0.165f, 0.115f, 0.10f, 10, kSilverTrim);
        mb.addCylinderY({0.0f,  1.16f, 0.0f}, 0.108f, 0.068f, 6.95f, 10, kGiwaPole);
        mb.addCylinderY({0.0f,  8.02f, 0.0f}, 0.092f, 0.092f, 0.16f, 10, kSilverTrim);
        mb.addSphere({0.0f, 8.24f, 0.0f}, 0.075f, 6, 10, kSilverTrim);
        for (float dir : {-1.0f, 1.0f}) {
            const Vec3 arc[5] = {
                {0.00f, 8.08f, 0.0f},
                {dir * 0.45f, 8.46f, 0.0f},
                {dir * 1.08f, 8.64f, 0.0f},
                {dir * 1.70f, 8.60f, 0.0f},
                {dir * 2.15f, 8.48f, 0.0f}
            };
            for (int i = 0; i < 4; ++i) {
                const float r0 = lerpf(0.056f, 0.036f, static_cast<float>(i) / 4.0f);
                const float r1 = lerpf(0.056f, 0.036f, static_cast<float>(i + 1) / 4.0f);
                addTube(mb, arc[i], arc[i + 1], r0, r1, 8, kGiwaPole);
            }
            addTube(mb, {dir * 0.05f, 7.52f, 0.0f}, {dir * 0.92f, 8.58f, 0.0f}, 0.020f, 0.016f, 6, kSilverTrim);
            mb.addBox({dir * 2.30f, 8.46f, 0.0f}, {0.76f, 0.085f, 0.30f}, kSilverTrim);
            mb.addBox({dir * 2.24f, 8.52f, 0.0f}, {0.56f, 0.045f, 0.22f}, kGiwaDark);
        }
        seoulDoubleLampMesh_ = mb.upload(vk, 42.0f);
    }

    // 2C. Sculpted 10.5m Korean Expressway Galvanized Silver Upswept Cobra Lamp (고속도로/강변북로 가로등)
    {
        MeshBuilder3D mb;
        const ColorRGBA kPlinth{154, 158, 164, 255};
        const ColorRGBA kGalv{184, 190, 198, 255};
        const ColorRGBA kCap{124, 132, 142, 255};
        mb.addCylinderY({0.0f, -0.20f, 0.0f}, 0.26f, 0.22f, 0.95f, 8, kPlinth);
        mb.addCylinderY({0.0f,  0.75f, 0.0f}, 0.135f, 0.078f, 8.95f, 10, kGalv);
        const Vec3 arc[5] = {
            {0.00f, 9.65f, 0.0f},
            {0.55f, 10.18f, 0.0f},
            {1.35f, 10.48f, 0.0f},
            {2.18f, 10.56f, 0.0f},
            {2.78f, 10.48f, 0.0f}
        };
        for (int i = 0; i < 4; ++i) {
            addTube(mb, arc[i], arc[i + 1], lerpf(0.068f, 0.042f, static_cast<float>(i) / 4.0f),
                    lerpf(0.068f, 0.042f, static_cast<float>(i + 1) / 4.0f), 8, kGalv);
        }
        mb.addBox({2.95f, 10.46f, 0.0f}, {0.92f, 0.11f, 0.36f}, kGalv);
        mb.addBox({2.88f, 10.53f, 0.0f}, {0.68f, 0.05f, 0.26f}, kCap);
        highwayLampMesh_ = mb.upload(vk, 42.0f);
    }

    // 3. Sculpted Korean 4-Aspect Horizontal Cantilever Traffic Signal (한국 4구 가로형 신호등 + 흰색 배면판 + 보행신호 잔여시간표시기 + 음향신호기!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kPole{84, 90, 100, 255};         // 기와진회색 metallic signal pole
        const ColorRGBA kSteel{128, 134, 144, 255};      // Galvanized steel collars & brackets
        const ColorRGBA kHousing{26, 28, 32, 255};       // Matte black polycarbonate signal head
        const ColorRGBA kBackplate{242, 245, 248, 255};  // Iconic crisp Korean white backplate border
        const ColorRGBA kStreetBlue{22, 88, 178, 255};   // Royal-blue 도로명판 street-name blade
        const ColorRGBA kYellowBox{238, 190, 36, 255};   // Tactile acoustic push-button box (음향신호기)

        // Heavy 4-bolt anchor flange + tapered vertical pole (0..6.65m) + dome cap
        mb.addBox({0.0f, 0.045f, 0.0f}, {0.50f, 0.09f, 0.50f}, kPole);
        mb.addCylinderY({0.0f, -0.15f, 0.0f}, 0.20f, 0.17f, 0.72f, 10, kPole);
        mb.addCylinderY({0.0f,  0.56f, 0.0f}, 0.145f, 0.105f, 6.08f, 10, kPole);
        mb.addSphere({0.0f, 6.66f, 0.0f}, 0.115f, 6, 10, kSteel);
        // Tapered horizontal cantilever mast arm (x = 0..5.40m @ y = 6.12m) + triangular gusset + tension tie rod!
        addTube(mb, {0.0f, 6.12f, 0.0f}, {5.40f, 6.12f, 0.0f}, 0.105f, 0.066f, 10, kPole);
        addTube(mb, {0.0f, 6.56f, 0.0f}, {2.70f, 6.16f, 0.0f}, 0.032f, 0.026f, 6, kSteel);
        mb.addBox({0.16f, 6.12f, 0.0f}, {0.26f, 0.36f, 0.28f}, kSteel);
        // Twin stainless mounting hangers down to 4-aspect horizontal signal housing
        for (float hx : {2.95f, 4.25f}) {
            addTube(mb, {hx, 6.10f, 0.0f}, {hx, 5.82f, 0.0f}, 0.032f, 0.032f, 6, kSteel);
        }
        // Crisp Korean white reflective backplate border + matte black 4-lens horizontal signal housing!
        mb.addBox({3.60f, 5.64f, 0.0f}, {2.32f, 0.66f, 0.09f}, kBackplate);
        mb.addBox({3.60f, 5.64f, 0.0f}, {2.18f, 0.54f, 0.32f}, kHousing);
        // 4 individual sculpted sun-visor hoods (차양막) on BOTH front (+Z) & back (-Z) faces!
        const float lampXs[4] = {2.85f, 3.35f, 3.85f, 4.35f};
        for (float lx : lampXs) {
            for (float fz : {-1.0f, 1.0f}) {
                mb.addBox({lx, 5.83f, fz * 0.26f}, {0.36f, 0.036f, 0.26f}, kHousing);
                mb.addBox({lx - 0.17f, 5.70f, fz * 0.23f}, {0.032f, 0.25f, 0.22f}, kHousing);
                mb.addBox({lx + 0.17f, 5.70f, fz * 0.23f}, {0.032f, 0.25f, 0.22f}, kHousing);
            }
        }
        // Royal-blue Korean street-name blade sign (도로명판) with crisp white border!
        mb.addBox({1.35f, 5.74f, 0.0f}, {1.38f, 0.34f, 0.055f}, kStreetBlue);
        mb.addBox({1.35f, 5.74f, 0.0f}, {1.44f, 0.39f, 0.030f}, kBackplate);
        addTube(mb, {0.92f, 6.10f, 0.0f}, {0.92f, 5.92f, 0.0f}, 0.022f, 0.022f, 5, kSteel);
        addTube(mb, {1.78f, 6.10f, 0.0f}, {1.78f, 5.92f, 0.0f}, 0.022f, 0.022f, 5, kSteel);
        // Pole-mounted Korean Pedestrian Signal Box (보행자 신호등) + Auxiliary Countdown Unit (잔여시간표시기) @ y = 3.16m
        mb.addBox({0.0f, 3.18f, 0.24f}, {0.38f, 0.80f, 0.24f}, kHousing);
        mb.addBox({0.0f, 3.52f, 0.42f}, {0.34f, 0.032f, 0.20f}, kHousing); // Upper red-stand visor
        mb.addBox({0.0f, 3.16f, 0.42f}, {0.34f, 0.032f, 0.20f}, kHousing); // Lower green-walk visor
        // Side-mounted Korean triangular pedestrian countdown housing
        mb.addBox({0.36f, 3.18f, 0.24f}, {0.32f, 0.68f, 0.22f}, kHousing);
        mb.addBox({0.36f, 3.50f, 0.40f}, {0.28f, 0.03f, 0.16f}, kHousing);
        // Iconic Korean yellow tactile acoustic pedestrian signal push-button box (시각장애인 음향신호기) @ y = 1.15m
        mb.addBox({0.0f, 1.16f, 0.18f}, {0.16f, 0.24f, 0.10f}, kYellowBox);
        mb.addCylinderY({0.0f, 1.10f, 0.235f}, 0.03f, 0.03f, 0.04f, 8, kSteel);
        koreanSignalMesh_ = mb.upload(vk, 35.0f);
    }

    // 4. Sculpted Seoul Design Bus Shelter (서울형 가로변 승차대 — 기와진회색 프레임 + 실제 투명 강화유리 + 안전띠 + 원목벤치 + BIT!)
    {
        MeshBuilder3D mbF;
        MeshBuilder3D mbG;
        const ColorRGBA kGiwaFrame{64, 70, 80, 255};     // 서울 기와진회색 structural steel
        const ColorRGBA kStainless{192, 198, 206, 255};  // Brushed stainless base flanges & spider fittings
        const ColorRGBA kSeoulBlue{26, 108, 214, 255};   // 서울 간선버스 블루 header strip
        const ColorRGBA kWood{164, 114, 68, 255};        // Warm cedar wood slats
        const ColorRGBA kWoodAlt{150, 102, 58, 255};
        const ColorRGBA kPosterBg{238, 242, 248, 255};
        const ColorRGBA kGlass{152, 204, 224, 96};       // Real see-through low-iron tempered safety glass (alpha=96/255!)
        const ColorRGBA kRoofGlass{136, 186, 212, 118};  // Lightly tinted overhead canopy glass
        const ColorRGBA kSafetyFrit{242, 246, 250, 245}; // Iconic white ceramic safety-dot stripe at eye level!

        // 3 slim 기와진회색 structural rear columns + brushed stainless bolted base plates & glass spider clamps
        for (float cx : {-2.20f, 0.0f, 2.20f}) {
            mbF.addBox({cx, 0.04f, -0.78f}, {0.26f, 0.08f, 0.26f}, kStainless);
            mbF.addBox({cx, 1.32f, -0.78f}, {0.13f, 2.58f, 0.15f}, kGiwaFrame);
            // Sweeping cantilevered steel roof rafter arms reaching over boarding zone (+Z)
            addTube(mbF, {cx, 2.52f, -0.84f}, {cx, 2.74f, 0.96f}, 0.052f, 0.036f, 6, kGiwaFrame);
            // Stainless glass-mounting spider clamps
            for (float sy : {0.42f, 2.30f}) {
                mbF.addBox({cx, sy, -0.72f}, {0.18f, 0.06f, 0.08f}, kStainless);
            }
        }
        // Sleek floating roof perimeter fascia + iconic Seoul Bus Blue front header strip + rear rain gutter
        mbF.addBox({0.0f, 2.74f,  0.94f}, {4.96f, 0.15f, 0.12f}, kGiwaFrame);
        mbF.addBox({0.0f, 2.75f,  1.01f}, {4.88f, 0.12f, 0.035f}, kSeoulBlue);
        mbF.addBox({0.0f, 2.50f, -0.86f}, {4.96f, 0.13f, 0.12f}, kGiwaFrame);
        mbF.addBox({-2.44f, 2.62f, 0.04f}, {0.09f, 0.13f, 1.82f}, kGiwaFrame);
        mbF.addBox({ 2.44f, 2.62f, 0.04f}, {0.09f, 0.13f, 1.82f}, kGiwaFrame);
        // Single-plane translucent tempered skylight glass roof + rear & side windbreak glass panels (MatId::StreetGlass!)
        mbG.addQuad({-2.38f, 2.54f, -0.80f}, { 2.38f, 2.54f, -0.80f},
                    { 2.38f, 2.74f,  0.90f}, {-2.38f, 2.74f,  0.90f}, kRoofGlass);
        mbG.addQuad({-1.26f, 0.30f, -0.74f}, { 2.18f, 0.30f, -0.74f},
                    { 2.18f, 2.44f, -0.74f}, {-1.26f, 2.44f, -0.74f}, kGlass);
        mbG.addQuad({ 2.20f, 0.30f, -0.74f}, { 2.20f, 0.30f,  0.38f},
                    { 2.20f, 2.44f,  0.38f}, { 2.20f, 2.44f, -0.74f}, kGlass);
        // Iconic Korean frosted-white horizontal collision-safety stripe across tempered glass at eye height (y = 1.32m)!
        mbG.addQuad({-1.24f, 1.28f, -0.732f}, { 2.16f, 1.28f, -0.732f},
                    { 2.16f, 1.36f, -0.732f}, {-1.24f, 1.36f, -0.732f}, kSafetyFrit);
        mbG.addQuad({ 2.208f, 1.28f, -0.72f}, { 2.208f, 1.28f,  0.36f},
                    { 2.208f, 1.36f,  0.36f}, { 2.208f, 1.36f, -0.72f}, kSafetyFrit);
        // Horizontal stainless transom bars framing the rear tempered glass
        mbF.addBox({0.46f, 0.30f, -0.74f}, {3.48f, 0.055f, 0.075f}, kStainless);
        mbF.addBox({0.46f, 2.44f, -0.74f}, {3.48f, 0.055f, 0.075f}, kStainless);
        // Left-side illuminated Seoul bus-route map & ad lightbox cabinet (x = -1.82m)
        mbF.addBox({-1.82f, 1.34f, -0.20f}, {0.17f, 2.20f, 1.16f}, kGiwaFrame);
        mbF.addBox({-1.725f, 1.34f, -0.20f}, {0.025f, 1.94f, 0.96f}, kPosterBg);
        mbF.addBox({-1.915f, 1.34f, -0.20f}, {0.025f, 1.94f, 0.96f}, kPosterBg);
        mbF.addBox({-1.71f, 2.16f, -0.20f}, {0.015f, 0.18f, 0.92f}, kSeoulBlue); // Route-map blue header inside lightbox
        // Ergonomic 4-slat warm cedar Seoul bus bench + brushed stainless legs + center armrest hoops!
        for (float lx : {-0.96f, 0.46f, 1.88f}) {
            mbF.addBox({lx, 0.23f, -0.36f}, {0.055f, 0.46f, 0.44f}, kStainless);
            mbF.addBox({lx, 0.62f, -0.36f}, {0.04f, 0.038f, 0.40f}, kStainless);
            mbF.addBox({lx, 0.54f, -0.17f}, {0.04f, 0.18f, 0.04f}, kStainless);
            mbF.addBox({lx, 0.54f, -0.55f}, {0.04f, 0.18f, 0.04f}, kStainless);
        }
        for (int sl = 0; sl < 4; ++sl) {
            const float sz = -0.54f + static_cast<float>(sl) * 0.12f;
            mbF.addBox({0.46f, 0.47f, sz}, {3.00f, 0.042f, 0.092f}, (sl & 1) ? kWoodAlt : kWood);
        }
        // Sculpted Seoul BIT (버스정보안내단말기) freestanding digital arrival totem (x = +2.72m)
        mbF.addBox({2.72f, 0.06f, 0.16f}, {0.72f, 0.12f, 0.34f}, kStainless);
        mbF.addBox({2.72f, 1.38f, 0.16f}, {0.60f, 2.56f, 0.21f}, kGiwaFrame);
        mbF.addBox({2.72f, 2.70f, 0.16f}, {0.66f, 0.14f, 0.26f}, kSeoulBlue);
        mbF.addBox({2.72f, 1.96f, 0.27f}, {0.52f, 0.48f, 0.02f}, {18, 26, 38, 255}); // BIT LED screen recess
        mbF.addBox({2.72f, 1.24f, 0.27f}, {0.42f, 0.34f, 0.02f}, kPosterBg);         // Lower static route timetable

        busShelterFrameMesh_ = mbF.upload(vk, 30.0f);
        busShelterGlassMesh_ = mbG.upload(vk, 25.0f);
    }

    // 5. Sculpted Seoul Metro Glass-Vault Subway Entrance Canopy (서울교통공사 표준 유리 캐노피 + 화강석 계단 + 투명 유리 + 출입구 폴사인!)
    {
        MeshBuilder3D mbF;
        MeshBuilder3D mbG;
        const ColorRGBA kSteel{70, 76, 86, 255};
        const ColorRGBA kStainless{198, 204, 214, 255};
        const ColorRGBA kGranite{184, 184, 182, 255};      // Warm Pocheon granite parapet
        const ColorRGBA kGraniteCoping{198, 198, 196, 255};
        const ColorRGBA kGraniteStep{154, 156, 158, 255};
        const ColorRGBA kNosing{56, 58, 62, 255};          // Anti-slip stair nosing strip
        const ColorRGBA kDarkWell{28, 32, 38, 255};
        const ColorRGBA kGlass{152, 206, 228, 98};         // Real see-through tempered glass vault (alpha=98/255!)
        const ColorRGBA kSafetyFrit{242, 246, 250, 240};

        // Low honed-Pocheon-granite perimeter parapet walls + beveled stone coping cap (open entry on +X side!)
        mbF.addBox({0.0f, 0.43f, -1.32f}, {5.50f, 0.86f, 0.22f}, kGranite);
        mbF.addBox({0.0f, 0.43f,  1.32f}, {5.50f, 0.86f, 0.22f}, kGranite);
        mbF.addBox({-2.66f, 0.43f, 0.0f}, {0.22f, 0.86f, 2.86f}, kGranite);
        mbF.addBox({0.0f, 0.88f, -1.32f}, {5.58f, 0.06f, 0.28f}, kGraniteCoping);
        mbF.addBox({0.0f, 0.88f,  1.32f}, {5.58f, 0.06f, 0.28f}, kGraniteCoping);
        mbF.addBox({-2.66f, 0.88f, 0.0f}, {0.28f, 0.06f, 2.92f}, kGraniteCoping);

        // Visible descending granite stairwell steps with dark anti-slip nosing + dark concourse throat
        mbF.addBox({-1.65f, 0.08f, 0.0f}, {2.10f, 0.16f, 2.36f}, kDarkWell);
        for (int st = 0; st < 6; ++st) {
            const float sx = 2.25f - static_cast<float>(st) * 0.40f;
            const float sy = 0.03f - static_cast<float>(st) * 0.012f;
            mbF.addBox({sx, sy, 0.0f}, {0.38f, 0.055f, 2.30f}, (st & 1) ? kGranite : kGraniteStep);
            mbF.addBox({sx + 0.17f, sy + 0.030f, 0.0f}, {0.035f, 0.012f, 2.28f}, kNosing);
        }
        // Triple brushed-stainless descending handrails (left, center divider, right!)
        for (float hz : {-1.10f, 0.0f, 1.10f}) {
            addTube(mbF, {2.45f, 0.94f, hz}, {-1.95f, 0.46f, hz}, 0.026f, 0.026f, 6, kStainless);
            for (float px : {2.20f, 0.85f, -0.55f}) {
                const float ty = lerpf(0.94f, 0.46f, (2.45f - px) / 4.40f);
                addTube(mbF, {px, 0.02f, hz}, {px, ty, hz}, 0.022f, 0.022f, 6, kStainless);
            }
        }

        // 5 Arched Brushed-Steel Portal Ribs supporting a vaulted see-through tempered-glass roof!
        for (int ri = 0; ri < 5; ++ri) {
            const float rx = -2.60f + static_cast<float>(ri) * 1.30f;
            mbF.addBox({rx, 1.72f, -1.30f}, {0.11f, 1.70f, 0.13f}, kSteel);
            mbF.addBox({rx, 1.72f,  1.30f}, {0.11f, 1.70f, 0.13f}, kSteel);
            const Vec3 ap[5] = {
                {rx, 2.56f, -1.36f},
                {rx, 2.94f, -0.74f},
                {rx, 3.10f,  0.00f},
                {rx, 2.94f,  0.74f},
                {rx, 2.56f,  1.36f}
            };
            for (int k = 0; k < 4; ++k) {
                addTube(mbF, ap[k], ap[k + 1], 0.052f, 0.052f, 6, kSteel);
            }
        }
        // Longitudinal steel purlins + stainless top edge trim
        for (float pz : {-1.34f, -0.72f, 0.0f, 0.72f, 1.34f}) {
            const float py = (std::abs(pz) < 0.1f) ? 3.10f : ((std::abs(pz) < 0.8f) ? 2.94f : 2.56f);
            mbF.addBox({0.0f, py, pz}, {5.56f, 0.075f, 0.085f}, kSteel);
        }
        // Single-plane vaulted skylight glass roof + side tempered glass windbreaks + white safety frit stripe!
        const float zArc[5] = {-1.32f, -0.72f, 0.0f, 0.72f, 1.32f};
        const float yArc[5] = { 2.58f,  2.96f, 3.12f, 2.96f, 2.58f};
        for (int k = 0; k < 4; ++k) {
            mbG.addQuad({-2.66f, yArc[k],     zArc[k]},     { 2.66f, yArc[k],     zArc[k]},
                        { 2.66f, yArc[k + 1], zArc[k + 1]}, {-2.66f, yArc[k + 1], zArc[k + 1]}, kGlass);
        }
        mbG.addQuad({-2.62f, 0.90f, -1.28f}, { 2.62f, 0.90f, -1.28f},
                    { 2.62f, 2.52f, -1.28f}, {-2.62f, 2.52f, -1.28f}, kGlass);
        mbG.addQuad({-2.62f, 0.90f,  1.28f}, { 2.62f, 0.90f,  1.28f},
                    { 2.62f, 2.52f,  1.28f}, {-2.62f, 2.52f,  1.28f}, kGlass);
        mbG.addQuad({-2.62f, 0.90f, -1.28f}, {-2.62f, 0.90f,  1.28f},
                    {-2.62f, 2.52f,  1.28f}, {-2.62f, 2.52f, -1.28f}, kGlass);
        mbG.addQuad({-2.60f, 1.38f,  1.286f}, { 2.60f, 1.38f,  1.286f},
                    { 2.60f, 1.45f,  1.286f}, {-2.60f, 1.45f,  1.286f}, kSafetyFrit);
        mbG.addQuad({-2.60f, 1.38f, -1.286f}, { 2.60f, 1.38f, -1.286f},
                    { 2.60f, 1.45f, -1.286f}, {-2.60f, 1.45f, -1.286f}, kSafetyFrit);

        // Tall brushed-steel Seoul Metro Exit Pole Sign (출입구 폴사인, x = +3.28m)
        mbF.addCylinderY({3.28f, -0.10f, 0.0f}, 0.14f, 0.12f, 0.42f, 10, kSteel);
        mbF.addCylinderY({3.28f,  0.30f, 0.0f}, 0.072f, 0.064f, 4.10f, 10, kStainless);

        subwayCanopyFrameMesh_ = mbF.upload(vk, 30.0f);
        subwayCanopyGlassMesh_ = mbG.upload(vk, 25.0f);
    }

    // 6. Sculpted Seoul Public-Design Modular Pedestrian Safety Guardrail (서울시 보행자 방호울타리 7.2m — 기와진회색!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kGiwaMetal{84, 90, 100, 255};    // 서울 기와진회색 metallic finish
        const ColorRGBA kCollar{142, 148, 158, 255};     // Brushed stainless base flanges & joint sleeves
        const ColorRGBA kReflect{244, 248, 252, 255};    // High-visibility reflective band
        const ColorRGBA kAmber{244, 188, 38, 255};

        // Flattened architectural oval top rail (y = 0.90m), upper accent rail (y = 0.76m) & lower rail (y = 0.20m)
        mb.addBox({0.0f, 0.90f, 0.0f}, {7.24f, 0.052f, 0.068f}, kGiwaMetal);
        addTube(mb, {-3.60f, 0.76f, 0.0f}, {3.60f, 0.76f, 0.0f}, 0.018f, 0.018f, 6, kCollar);
        addTube(mb, {-3.60f, 0.20f, 0.0f}, {3.60f, 0.20f, 0.0f}, 0.022f, 0.022f, 6, kGiwaMetal);
        // 4 sturdy flanged anchor posts grounded -0.18m for sloped curbs + crisp dual reflective collars!
        for (float px : {-3.60f, -1.20f, 1.20f, 3.60f}) {
            mb.addCylinderY({px, -0.18f, 0.0f}, 0.042f, 0.040f, 1.09f, 8, kGiwaMetal);
            mb.addCylinderY({px,  0.00f, 0.0f}, 0.078f, 0.068f, 0.055f, 8, kCollar);
            mb.addCylinderY({px,  0.66f, 0.0f}, 0.045f, 0.045f, 0.055f, 8, kReflect);
            mb.addCylinderY({px,  0.59f, 0.0f}, 0.045f, 0.045f, 0.040f, 8, kAmber);
            mb.addSphere({px, 0.93f, 0.0f}, 0.042f, 5, 8, kCollar);
        }
        // Clean Seoul geometric rectangular-frame vertical balusters between lower & upper rails
        for (int bay = 0; bay < 3; ++bay) {
            const float xStart = -3.60f + static_cast<float>(bay) * 2.40f;
            for (int bi = 1; bi <= 7; ++bi) {
                const float bx = xStart + static_cast<float>(bi) * 0.30f;
                mb.addCylinderY({bx, 0.20f, 0.0f}, 0.014f, 0.014f, 0.56f, 5, kGiwaMetal);
            }
        }
        pedGuardrailMesh_ = mb.upload(vk, 35.0f);
    }

    // 6B. Sculpted Seoul Boulevard Central Median Jaywalking-Prevention Fence (강남/테헤란로 중앙분리대 무단횡단 방지펜스!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kGraniteBase{174, 174, 172, 255}; // Low dressed granite median plinth (only 16cm!)
        const ColorRGBA kPostDark{68, 74, 84, 255};       // Flexible charcoal-slate Korean median posts
        const ColorRGBA kRailMetal{92, 98, 108, 255};
        const ColorRGBA kReflectW{244, 248, 252, 255};    // Crisp white retroreflective bands
        const ColorRGBA kReflectY{244, 192, 34, 255};     // Korean amber retroreflective sleeve

        // Low sleek dressed-granite anchor plinth (grounded -0.18m on hills, rises +0.14m above median top!)
        mb.addBox({0.0f, -0.02f, 0.0f}, {6.40f, 0.32f, 0.28f}, kGraniteBase);
        // Twin slim horizontal dark-steel rails (y = 0.56m & y = 0.88m)
        mb.addBox({0.0f, 0.88f, 0.0f}, {6.36f, 0.048f, 0.062f}, kRailMetal);
        mb.addBox({0.0f, 0.56f, 0.0f}, {6.36f, 0.042f, 0.056f}, kRailMetal);
        // 5 flexible Korean tubular median posts with iconic dual reflective safety sleeves!
        for (int i = 0; i < 5; ++i) {
            const float px = -3.00f + static_cast<float>(i) * 1.50f;
            mb.addCylinderY({px, 0.14f, 0.0f}, 0.065f, 0.055f, 0.05f, 8, kRailMetal);
            mb.addCylinderY({px, 0.18f, 0.0f}, 0.040f, 0.038f, 0.76f, 8, kPostDark);
            mb.addCylinderY({px, 0.68f, 0.0f}, 0.044f, 0.044f, 0.065f, 8, kReflectW);
            mb.addCylinderY({px, 0.76f, 0.0f}, 0.044f, 0.044f, 0.065f, 8, kReflectY);
            mb.addSphere({px, 0.95f, 0.0f}, 0.040f, 5, 8, kRailMetal);
        }
        // Subtle vertical anti-glare / jaywalking-blocker slats between posts
        for (int i = 0; i < 4; ++i) {
            const float bx0 = -3.00f + static_cast<float>(i) * 1.50f;
            for (float sub : {0.38f, 0.75f, 1.12f}) {
                mb.addBox({bx0 + sub, 0.72f, 0.0f}, {0.032f, 0.32f, 0.020f}, kPostDark);
            }
        }
        medianFenceMesh_ = mb.upload(vk, 35.0f);
    }

    // 6C. Sculpted Korean KS Standard Safety Bollard (서울 충격흡수/스테인리스 볼라드 — 높이 84cm, 이중 반사띠!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kFlange{92, 98, 108, 255};
        const ColorRGBA kBody{62, 68, 76, 255};        // Shock-absorbing matte dark charcoal polyurethane body
        const ColorRGBA kStainless{184, 190, 198, 255};
        const ColorRGBA kReflectW{244, 248, 252, 255}; // Upper white reflective band
        const ColorRGBA kReflectY{246, 202, 42, 255};  // Lower Korean safety-yellow reflective band

        mb.addCylinderY({0.0f, -0.06f, 0.0f}, 0.145f, 0.135f, 0.105f, 12, kFlange);
        mb.addCylinderY({0.0f,  0.04f, 0.0f}, 0.095f, 0.092f, 0.76f, 12, kBody);
        mb.addCylinderY({0.0f,  0.54f, 0.0f}, 0.097f, 0.097f, 0.065f, 12, kReflectY);
        mb.addCylinderY({0.0f,  0.65f, 0.0f}, 0.097f, 0.097f, 0.065f, 12, kReflectW);
        mb.addCylinderY({0.0f,  0.76f, 0.0f}, 0.096f, 0.092f, 0.045f, 12, kStainless);
        mb.addSphere({0.0f, 0.80f, 0.0f}, 0.092f, 6, 12, kStainless);
        bollardMesh_ = mb.upload(vk, 38.0f);
    }

    // 7. Sculpted Seoul Public-Design Wood-Slat & Cast-Aluminum Street Bench (서울시 가로 벤치)
    {
        MeshBuilder3D mb;
        const ColorRGBA kFrame{62, 68, 78, 255};
        const ColorRGBA kFoot{136, 142, 152, 255};
        const ColorRGBA kWood{168, 118, 72, 255};
        const ColorRGBA kWoodLight{184, 134, 86, 255};
        for (float bx : {-0.84f, 0.0f, 0.84f}) {
            mb.addBox({bx, 0.02f,  0.19f}, {0.10f, 0.04f, 0.10f}, kFoot);
            mb.addBox({bx, 0.02f, -0.21f}, {0.10f, 0.04f, 0.10f}, kFoot);
            mb.addBox({bx, 0.23f,  0.19f}, {0.055f, 0.42f, 0.055f}, kFrame);
            mb.addBox({bx, 0.23f, -0.21f}, {0.055f, 0.42f, 0.055f}, kFrame);
            mb.addBox({bx, 0.42f, -0.01f}, {0.055f, 0.048f, 0.48f}, kFrame);
            addTube(mb, {bx, 0.42f, -0.23f}, {bx, 0.88f, -0.34f}, 0.026f, 0.022f, 6, kFrame);
            if (std::abs(bx) > 0.1f) {
                mb.addBox({bx, 0.63f, -0.01f}, {0.050f, 0.036f, 0.44f}, kFrame);
                mb.addBox({bx, 0.53f,  0.20f}, {0.048f, 0.20f, 0.048f}, kFrame);
            }
        }
        // 4 ergonomic warm-teak seat slats + 3 contoured backrest slats
        for (int si = 0; si < 4; ++si) {
            const float sz = 0.17f - static_cast<float>(si) * 0.115f;
            mb.addBox({0.0f, 0.46f, sz}, {1.96f, 0.040f, 0.088f}, (si & 1) ? kWood : kWoodLight);
        }
        mb.addBox({0.0f, 0.60f, -0.26f}, {1.96f, 0.095f, 0.038f}, kWood);
        mb.addBox({0.0f, 0.72f, -0.29f}, {1.96f, 0.095f, 0.038f}, kWoodLight);
        mb.addBox({0.0f, 0.84f, -0.32f}, {1.96f, 0.095f, 0.038f}, kWood);
        benchMesh_ = mb.upload(vk, 30.0f);
    }

    // 8. Sculpted Korean Standard Red Cast-Iron Ground Fire Hydrant + Yellow/Black Protective U-Rail (한국 지상식 소화전 + 보호틀!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kRed{218, 36, 36, 255};
        const ColorRGBA kRedDark{168, 26, 26, 255};
        const ColorRGBA kBrass{222, 188, 76, 255};
        const ColorRGBA kBase{172, 174, 176, 255};
        const ColorRGBA kReflectW{244, 246, 250, 255};
        const ColorRGBA kGuardY{244, 194, 34, 255};
        const ColorRGBA kGuardB{34, 36, 40, 255};

        mb.addCylinderY({0.0f, -0.06f, 0.0f}, 0.23f, 0.21f, 0.12f, 10, kBase);     // Granite plinth
        mb.addCylinderY({0.0f,  0.06f, 0.0f}, 0.185f, 0.155f, 0.06f, 10, kRedDark);// Flanged base collar
        mb.addCylinderY({0.0f,  0.12f, 0.0f}, 0.138f, 0.128f, 0.56f, 12, kRed);    // Main cast-iron barrel
        mb.addCylinderY({0.0f,  0.60f, 0.0f}, 0.136f, 0.136f, 0.055f, 12, kReflectW);// White reflective neck band
        mb.addCylinderY({0.0f,  0.68f, 0.0f}, 0.168f, 0.168f, 0.048f, 12, kRedDark);// Upper bonnet flange
        mb.addSphere({0.0f, 0.74f, 0.0f}, 0.142f, 6, 12, kRed);                    // Domed bonnet cap
        mb.addCylinderY({0.0f,  0.86f, 0.0f}, 0.044f, 0.040f, 0.07f, 6, kBrass);   // Pentagonal operating nut
        // Twin side 65mm brass hose couplings + front 100mm steamer port
        addTube(mb, {-0.22f, 0.52f, 0.0f}, {0.22f, 0.52f, 0.0f}, 0.054f, 0.054f, 8, kBrass);
        addTube(mb, {0.0f, 0.42f, 0.0f}, {0.0f, 0.42f, 0.20f}, 0.068f, 0.064f, 8, kBrass);
        // Iconic Korean low yellow-and-black protective steel U-guard rail (소화전 보호틀) on road side!
        addTube(mb, {-0.34f, -0.04f, 0.30f}, {-0.34f, 0.56f, 0.30f}, 0.026f, 0.026f, 6, kGuardY);
        addTube(mb, { 0.34f, -0.04f, 0.30f}, { 0.34f, 0.56f, 0.30f}, 0.026f, 0.026f, 6, kGuardY);
        addTube(mb, {-0.34f,  0.56f, 0.30f}, { 0.34f, 0.56f, 0.30f}, 0.026f, 0.026f, 6, kGuardY);
        mb.addBox({-0.14f, 0.56f, 0.30f}, {0.10f, 0.056f, 0.056f}, kGuardB);
        mb.addBox({ 0.14f, 0.56f, 0.30f}, {0.10f, 0.056f, 0.056f}, kGuardB);
        hydrantMesh_ = mb.upload(vk, 40.0f);
    }

    // 9. Sculpted Seoul Design Dual Stainless-Steel Recycling & Waste Bin Unit (서울시 신형 가로휴지통 2구 세트!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kStainless{154, 162, 170, 255};
        const ColorRGBA kCharcoal{66, 72, 82, 255};
        const ColorRGBA kRecycleBlue{32, 126, 220, 255}; // 재활용 (Seoul Blue)
        const ColorRGBA kWasteAmber{228, 138, 34, 255};  // 일반쓰레기 (Warm Orange-Amber)
        const ColorRGBA kSlotDark{18, 20, 24, 255};
        const ColorRGBA kBadgeW{240, 244, 248, 255};

        // Raised steel support feet + dual brushed stainless cabinets
        mb.addBox({-0.29f, 0.04f, 0.0f}, {0.42f, 0.08f, 0.42f}, kCharcoal);
        mb.addBox({ 0.29f, 0.04f, 0.0f}, {0.42f, 0.08f, 0.42f}, kCharcoal);
        mb.addBox({-0.29f, 0.48f, 0.0f}, {0.48f, 0.80f, 0.46f}, kStainless);
        mb.addBox({ 0.29f, 0.48f, 0.0f}, {0.48f, 0.80f, 0.46f}, kStainless);
        // Color-coded sloped rain hoods + bottle/can circular throat (left) & general waste slot (right)!
        mb.addBox({-0.29f, 0.96f, 0.0f}, {0.51f, 0.18f, 0.49f}, kCharcoal);
        mb.addBox({-0.29f, 1.07f, 0.0f}, {0.52f, 0.045f, 0.50f}, kRecycleBlue);
        mb.addBox({-0.29f, 0.95f, 0.252f}, {0.16f, 0.15f, 0.02f}, kSlotDark);  // Circular can/PET bottle opening
        mb.addBox({-0.29f, 0.72f, 0.238f}, {0.38f, 0.11f, 0.02f}, kRecycleBlue);
        mb.addBox({-0.29f, 0.72f, 0.250f}, {0.22f, 0.04f, 0.01f}, kBadgeW);
        mb.addBox({ 0.29f, 0.96f, 0.0f}, {0.51f, 0.18f, 0.49f}, kCharcoal);
        mb.addBox({ 0.29f, 1.07f, 0.0f}, {0.52f, 0.045f, 0.50f}, kWasteAmber);
        mb.addBox({ 0.29f, 0.95f, 0.252f}, {0.30f, 0.13f, 0.02f}, kSlotDark);  // Rectangular waste slot
        mb.addBox({ 0.29f, 0.72f, 0.238f}, {0.38f, 0.11f, 0.02f}, kWasteAmber);
        mb.addBox({ 0.29f, 0.72f, 0.250f}, {0.22f, 0.04f, 0.01f}, kBadgeW);
        trashBinMesh_ = mb.upload(vk, 25.0f);
    }

    // 10. Sculpted KEPCO Street Transformer & Signal Control Cabinet (한전 지상개폐기 / 지상변압기 — 한전 특유 세이지-그레이!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kPlinth{168, 170, 172, 255};
        const ColorRGBA kBody{128, 144, 136, 255};    // Authentic KEPCO muted sage-olive-gray powder coat!
        const ColorRGBA kRoof{84, 94, 92, 255};
        const ColorRGBA kTrim{74, 82, 82, 255};
        const ColorRGBA kWarnYellow{242, 194, 34, 255};
        const ColorRGBA kWarnBlack{28, 30, 34, 255};
        const ColorRGBA kLatch{204, 210, 218, 255};

        // Concrete pedestal plinth with recessed shadow reveal
        mb.addBox({0.0f, 0.11f, 0.0f}, {1.26f, 0.22f, 0.80f}, kPlinth);
        mb.addBox({0.0f, 0.24f, 0.0f}, {1.14f, 0.05f, 0.72f}, kTrim);
        // Main KEPCO cabinet + overhanging pitched rain cap + recessed double service door seam + T-handle latch
        mb.addBox({0.0f, 0.92f, 0.0f}, {1.08f, 1.32f, 0.66f}, kBody);
        mb.addBox({0.0f, 1.61f, 0.0f}, {1.18f, 0.08f, 0.76f}, kRoof);
        mb.addBox({0.0f, 1.66f, 0.0f}, {1.04f, 0.05f, 0.64f}, kRoof);
        mb.addBox({0.0f, 0.92f, 0.335f}, {0.024f, 1.24f, 0.015f}, kTrim);
        mb.addBox({0.09f, 0.96f, 0.35f}, {0.042f, 0.17f, 0.032f}, kLatch);
        // Louvered cooling vents along lower front doors & side panels
        for (int lv = 0; lv < 4; ++lv) {
            const float ly = 0.42f + static_cast<float>(lv) * 0.072f;
            mb.addBox({-0.29f, ly, 0.34f}, {0.36f, 0.028f, 0.020f}, kTrim);
            mb.addBox({ 0.29f, ly, 0.34f}, {0.36f, 0.028f, 0.020f}, kTrim);
            mb.addBox({-0.55f, ly, 0.0f},  {0.020f, 0.028f, 0.38f}, kTrim);
            mb.addBox({ 0.55f, ly, 0.0f},  {0.020f, 0.028f, 0.38f}, kTrim);
        }
        // Iconic Korean high-voltage yellow-black hazard bar & danger plaque (위험 고압전기)
        mb.addBox({0.0f, 1.40f, 0.34f}, {0.92f, 0.11f, 0.018f}, kWarnYellow);
        for (float sx : {-0.30f, -0.10f, 0.10f, 0.30f}) {
            mb.addBox({sx, 1.40f, 0.352f}, {0.06f, 0.11f, 0.010f}, kWarnBlack);
        }
        utilityBoxMesh_ = mb.upload(vk, 25.0f);
    }

    // 11. Sculpted Seoul Public-Design Street Kiosk (서울시 디자인 가로판매대 — 메탈릭 실버·기와진회색 + 투명 슬라이딩 유리창 + 스텐 카운터!)
    {
        MeshBuilder3D mbF;
        MeshBuilder3D mbG;
        const ColorRGBA kPlinth{68, 74, 84, 255};
        const ColorRGBA kSilverPanel{146, 152, 160, 255};  // Authentic Seoul Design champagne-silver/titanium aluminum cladding!
        const ColorRGBA kSilverLight{168, 174, 182, 255};
        const ColorRGBA kRoofDark{60, 66, 76, 255};
        const ColorRGBA kSeoulBlue{28, 110, 206, 255};
        const ColorRGBA kCounter{194, 200, 208, 255};
        const ColorRGBA kInterior{228, 212, 184, 255};     // Warm lit interior wall behind glass
        const ColorRGBA kShelf{118, 92, 66, 255};
        const ColorRGBA kGlass{162, 210, 228, 86};         // Clean see-through sales window glass (MatId::StreetGlass!)

        // Grounded dark plinth base + chamfered metallic lower kiosk body (0..0.94m)
        mbF.addBox({0.0f, 0.08f, 0.0f}, {2.84f, 0.16f, 1.72f}, kPlinth);
        mbF.addBox({0.0f, 0.54f, 0.0f}, {2.80f, 0.80f, 1.68f}, kSilverPanel);
        // Solid rear wall & upper side walls + recessed bright interior magazine/snack shelves visible through front window!
        mbF.addBox({0.0f, 1.62f, -0.76f}, {2.80f, 1.38f, 0.16f}, kSilverPanel);
        mbF.addBox({-1.32f, 1.62f, 0.0f}, {0.16f, 1.38f, 1.68f}, kSilverPanel);
        mbF.addBox({ 1.32f, 1.62f, 0.0f}, {0.16f, 1.38f, 1.68f}, kSilverPanel);
        mbF.addBox({0.0f, 1.56f, -0.62f}, {2.46f, 1.18f, 0.06f}, kInterior); // Warm lit back wall
        mbF.addBox({0.0f, 1.24f, -0.48f}, {2.36f, 0.05f, 0.24f}, kShelf);    // Display shelf 1
        mbF.addBox({0.0f, 1.66f, -0.48f}, {2.36f, 0.05f, 0.24f}, kShelf);    // Display shelf 2
        // Subtle colorful Korean kiosk display merchandise (drinks/magazines) visible behind translucent window
        mbF.addBox({-0.78f, 1.38f, -0.48f}, {0.36f, 0.22f, 0.14f}, {214, 72, 62, 255});
        mbF.addBox({-0.22f, 1.36f, -0.48f}, {0.42f, 0.18f, 0.14f}, {232, 186, 62, 255});
        mbF.addBox({ 0.42f, 1.39f, -0.48f}, {0.38f, 0.24f, 0.14f}, {56, 152, 92, 255});
        mbF.addBox({ 0.88f, 1.37f, -0.48f}, {0.26f, 0.20f, 0.14f}, {58, 128, 212, 255});
        mbF.addBox({-0.64f, 1.79f, -0.48f}, {0.46f, 0.20f, 0.14f}, {234, 224, 198, 255});
        mbF.addBox({ 0.18f, 1.80f, -0.48f}, {0.52f, 0.22f, 0.14f}, {222, 122, 54, 255});
        // Vertical brushed aluminum panel seams on front, rear & sides (authentic 360-degree Seoul Design cladding!)
        for (float px : {-0.92f, 0.0f, 0.92f}) {
            mbF.addBox({px, 0.54f,  0.85f}, {0.03f, 0.78f, 0.02f}, kPlinth);
            mbF.addBox({px, 1.24f, -0.85f}, {0.032f, 2.12f, 0.022f}, kPlinth);
        }
        // Horizontal architectural dado rail & framed rear service panel + HVAC louver vents on rear (-Z) & sides (±X)
        mbF.addBox({0.0f, 0.94f, -0.85f}, {2.78f, 0.045f, 0.026f}, kRoofDark);
        mbF.addBox({-1.41f, 0.94f, 0.0f}, {0.026f, 0.045f, 1.66f}, kRoofDark);
        mbF.addBox({ 1.41f, 0.94f, 0.0f}, {0.026f, 0.045f, 1.66f}, kRoofDark);
        mbF.addBox({-1.41f, 1.24f, 0.0f}, {0.022f, 2.12f, 0.032f}, kPlinth);
        mbF.addBox({ 1.41f, 1.24f, 0.0f}, {0.022f, 2.12f, 0.032f}, kPlinth);
        for (int lv = 0; lv < 4; ++lv) {
            const float ly = 0.34f + static_cast<float>(lv) * 0.085f;
            mbF.addBox({0.0f, ly, -0.855f}, {0.72f, 0.032f, 0.020f}, kRoofDark);
        }
        // Subtle recessed poster/city-info frame on rear wall (-Z) & side walls (±X)
        mbF.addBox({0.0f, 1.56f, -0.852f}, {1.38f, 0.96f, 0.026f}, kRoofDark);
        mbF.addBox({0.0f, 1.56f, -0.864f}, {1.26f, 0.84f, 0.014f}, kSilverLight);
        // Brushed stainless steel customer counter shelf (+Z)
        mbF.addBox({0.0f, 0.94f, 0.96f}, {2.52f, 0.075f, 0.34f}, kCounter);
        // Slim window mullions + cantilevered overhanging canopy roof + crisp Seoul Blue fascia accent!
        for (float mx : {-1.24f, -0.42f, 0.42f, 1.24f}) {
            mbF.addBox({mx, 1.58f, 0.82f}, {0.048f, 1.24f, 0.055f}, kRoofDark);
        }
        mbF.addBox({0.0f, 2.22f, 0.82f}, {2.60f, 0.08f, 0.07f}, kRoofDark);
        mbF.addBox({0.0f, 2.38f, 0.22f}, {3.18f, 0.16f, 2.28f}, kRoofDark);
        mbF.addBox({0.0f, 2.48f, 0.22f}, {3.04f, 0.07f, 2.16f}, kSilverLight);
        mbF.addBox({0.0f, 2.38f, 1.37f}, {3.10f, 0.11f, 0.03f}, kSeoulBlue);
        mbF.addBox({0.0f, 2.38f, -0.93f}, {3.10f, 0.07f, 0.024f}, kSeoulBlue);
        // Single-plane see-through sliding glass sales window (rendered in Alpha pass with MatId::StreetGlass!)
        mbG.addQuad({-1.22f, 0.98f, 0.82f}, { 1.22f, 0.98f, 0.82f},
                    { 1.22f, 2.18f, 0.82f}, {-1.22f, 2.18f, 0.82f}, kGlass);

        streetKioskMesh_ = mbF.upload(vk, 28.0f);
        streetKioskGlassMesh_ = mbG.upload(vk, 25.0f);
    }

    // 12. Sculpted Seoul Sidewalk Street-Tree Planter Curb & Perforated Cast-Iron Tree Grate (화강석 수목보호틀 + 주철 가로수 보호판 1.50x1.50m!)
    {
        MeshBuilder3D mb;
        const ColorRGBA kGranite{184, 184, 180, 255};  // Flush Pocheon granite 12cm perimeter border
        const ColorRGBA kIron{64, 68, 76, 255};        // Dark cast-iron slotted grate bars
        const ColorRGBA kSoil{72, 58, 44, 255};        // Recessed dark soil/mulch bed

        // Recessed soil bed underneath slotted iron grate
        mb.addBox({0.0f, 0.015f, 0.0f}, {1.28f, 0.03f, 1.28f}, kSoil);
        // 4-sided flush dressed Pocheon granite curb frame (1.52m x 1.52m outer, 1.26m inner opening)
        mb.addBox({0.0f, 0.032f,  0.695f}, {1.52f, 0.064f, 0.13f}, kGranite);
        mb.addBox({0.0f, 0.032f, -0.695f}, {1.52f, 0.064f, 0.13f}, kGranite);
        mb.addBox({ 0.695f, 0.032f, 0.0f}, {0.13f, 0.064f, 1.26f}, kGranite);
        mb.addBox({-0.695f, 0.032f, 0.0f}, {0.13f, 0.064f, 1.26f}, kGranite);
        // Outer & inner cast-iron square grate frames + radial & concentric bars (classic Seoul geometric grate!)
        for (float ringR : {0.59f, 0.44f, 0.28f}) {
            const float len = ringR * 2.0f + 0.045f;
            mb.addBox({0.0f, 0.048f,  ringR}, {len, 0.022f, 0.045f}, kIron);
            mb.addBox({0.0f, 0.048f, -ringR}, {len, 0.022f, 0.045f}, kIron);
            mb.addBox({ ringR, 0.048f, 0.0f}, {0.045f, 0.022f, len}, kIron);
            mb.addBox({-ringR, 0.048f, 0.0f}, {0.045f, 0.022f, len}, kIron);
        }
        // 4 cardinal + 4 diagonal cast-iron spoke ribs connecting inner trunk collar to outer frame
        mb.addBox({ 0.44f, 0.048f, 0.0f}, {0.32f, 0.022f, 0.042f}, kIron);
        mb.addBox({-0.44f, 0.048f, 0.0f}, {0.32f, 0.022f, 0.042f}, kIron);
        mb.addBox({0.0f, 0.048f,  0.44f}, {0.042f, 0.022f, 0.32f}, kIron);
        mb.addBox({0.0f, 0.048f, -0.44f}, {0.042f, 0.022f, 0.32f}, kIron);
        for (float sx : {-1.0f, 1.0f}) {
            for (float sz : {-1.0f, 1.0f}) {
                addTube(mb, {sx * 0.26f, 0.048f, sz * 0.26f}, {sx * 0.59f, 0.048f, sz * 0.59f}, 0.020f, 0.020f, 4, kIron);
            }
        }
        treeGrateMesh_ = mb.upload(vk, 25.0f);
    }
}

void Renderer3D::buildSeoulMacroMeshes(VulkanContext& vk, const CityMap& city) {
    if (city.hasRealSeoulMap()) {
        // ====================================================================
        // REAL-WORLD OPENSTREETMAP SEOUL + SRTM 30m DEM MACRO MESHES (v3.0.0)
        // ====================================================================
        const RealSeoulData& real = city.realData();
        const OcMapHeader& hdr = real.header();

        // 1. Continuous 256x204 Real SRTM 30m DEM Terrain split cleanly into:
        //    - realTerrainMesh_     (Urban valley floor subbase -> MatId::SidewalkPavers)
        //    - mountainForestMesh_  (Real Namsan, Bukhansan, Gwanaksan, Inwangsan, Bugaksan, Achasan -> MatId::FoliageGrass)
        //    - mountainGraniteMesh_ (Steep high-altitude Korean granite peaks -> MatId::GraniteRock)
        {
            constexpr int kNx = 256;
            constexpr int kNz = 204;
            const float dx = (hdr.demMaxX - hdr.demMinX) / static_cast<float>(kNx);
            const float dz = (hdr.demMaxZ - hdr.demMinZ) / static_cast<float>(kNz);

            struct GridNode {
                Vec3 pos{};
                Vec3 norm{0.0f, 1.0f, 0.0f};
                float rawH = 0.0f;
                bool inCore = false;
                bool inWater = false;
            };
            std::vector<GridNode> grid(static_cast<std::size_t>((kNx + 1) * (kNz + 1)));

            // Precompute exact minimum ground-road elevation touching every 120m macro grid corner (<3ms pass!).
            // Guaranteeing macroY <= cellMinRoadY - 4.2m makes it mathematically IMPOSSIBLE for the coarse 120m
            // realTerrainMesh_ or mountainForestMesh_ to ever bridge a valley or pierce ANY road/sidewalk anywhere in Seoul!
            std::vector<float> gridMinRoadY(static_cast<std::size_t>((kNx + 1) * (kNz + 1)), 1e9f);
            {
                const auto& roads = real.roads();
                const auto& rVerts = real.roadVerts();
                const float invDx = 1.0f / dx;
                const float invDz = 1.0f / dz;
                for (std::size_t ri = 0; ri < roads.size(); ++ri) {
                    const OsmRoadRec& r = roads[ri];
                    if (r.vertCount < 2 || (r.flags & 4u) != 0u) continue; // Skip tunnels
                    const bool isElev = ((r.flags & 2u) != 0u) || (r.layer >= 1) || ((r.flags2 & 4u) != 0u);
                    if (isElev) continue;
                    auto stampRoadPt = [&](const Vec2& p, float ry) {
                        const float demP = real.sampleDem(p);
                        if (ry - demP > 3.2f) return; // Skip elevated viaduct spans
                        const float roadLow = std::min(ry, demP);
                        const int cx0 = std::clamp(static_cast<int>(std::floor((p.x - hdr.demMinX) * invDx)), 0, kNx);
                        const int cz0 = std::clamp(static_cast<int>(std::floor((p.z - hdr.demMinZ) * invDz)), 0, kNz);
                        const int cx1 = std::min(kNx, cx0 + 1);
                        const int cz1 = std::min(kNz, cz0 + 1);
                        for (int gz = cz0; gz <= cz1; ++gz) {
                            for (int gx = cx0; gx <= cx1; ++gx) {
                                float& slot = gridMinRoadY[static_cast<std::size_t>(gz * (kNx + 1) + gx)];
                                if (roadLow < slot) slot = roadLow;
                            }
                        }
                    };
                    for (std::uint16_t vi = 0; vi < r.vertCount; ++vi) {
                        const Vec2& p0 = rVerts[r.firstVert + vi];
                        const float y0 = real.has3DRoads() ? real.roadVertY(r.firstVert + vi) : real.sampleDem(p0);
                        stampRoadPt(p0, y0);
                        if (vi + 1u < r.vertCount) {
                            const Vec2& p1 = rVerts[r.firstVert + vi + 1u];
                            const float segLen = (p1 - p0).length();
                            if (segLen > 45.0f) {
                                const float y1 = real.has3DRoads() ? real.roadVertY(r.firstVert + vi + 1u) : real.sampleDem(p1);
                                const int nSub = static_cast<int>(std::ceil(segLen / 45.0f));
                                for (int s = 1; s < nSub; ++s) {
                                    const float t = static_cast<float>(s) / static_cast<float>(nSub);
                                    stampRoadPt(p0 + (p1 - p0) * t, lerpf(y0, y1, t));
                                }
                            }
                        }
                    }
                }
            }

            for (int iz = 0; iz <= kNz; ++iz) {
                const float wz = hdr.demMinZ + static_cast<float>(iz) * dz;
                for (int ix = 0; ix <= kNx; ++ix) {
                    const float wx = hdr.demMinX + static_cast<float>(ix) * dx;
                    const Vec2 wp{wx, wz};
                    const float h = real.sampleDem(wp);
                    const bool inCore = real.isInCoreBounds(wp);
                    const bool inWater = inCore && real.isInWater(wp);

                    float y = h;
                    if (inWater) {
                        y = h - 2.2f;
                    } else if (inCore) {
                        // Sample 3x3 surrounding neighborhood DEM (+/-65m) so coarse 120m triangles never bridge valleys!
                        float minNeighH = h;
                        if (h < 220.0f) {
                            for (float oz : {-65.0f, 0.0f, 65.0f}) {
                                for (float ox : {-65.0f, 0.0f, 65.0f}) {
                                    if (ox == 0.0f && oz == 0.0f) continue;
                                    minNeighH = std::min(minNeighH, real.sampleDem({wx + ox, wz + oz}));
                                }
                            }
                        }
                        const float urbanBlend = clampf(1.0f - (h - 115.0f) / 35.0f, 0.0f, 1.0f);
                        y = lerpf(h, minNeighH, urbanBlend) - 3.80f * urbanBlend;
                    }

                    // Strict road-clearance depression: EVERY macro vertex touching any ground road cell stays >= 4.2m
                    // beneath the lowest road in that cell (eliminating 100% of 120m road/sidewalk pierces up to +23m!),
                    // while high roadless peaks (Namsan peak, Bukhansan, Gwanaksan) remain untouched!
                    const float minRdY = gridMinRoadY[static_cast<std::size_t>(iz * (kNx + 1) + ix)];
                    if (minRdY < 1e8f) {
                        y = std::min(y, minRdY - 4.20f);
                    }

                    // Finite-difference SRTM surface normal
                    const float hL = real.sampleDem({wx - 40.0f, wz});
                    const float hR = real.sampleDem({wx + 40.0f, wz});
                    const float hD = real.sampleDem({wx, wz - 40.0f});
                    const float hU = real.sampleDem({wx, wz + 40.0f});
                    const Vec3 n = Vec3{hL - hR, 80.0f, hD - hU}.normalized();

                    GridNode& gn = grid[static_cast<std::size_t>(iz * (kNx + 1) + ix)];
                    gn.pos = {wx, y, wz};
                    gn.norm = n;
                    gn.rawH = h;
                    gn.inCore = inCore;
                    gn.inWater = inWater;
                }
            }

            std::vector<Vertex3D> urbanV, forestV, rockV;
            urbanV.reserve(static_cast<std::size_t>(kNx * kNz) * 3);
            forestV.reserve(static_cast<std::size_t>(kNx * kNz) * 3);
            rockV.reserve(16384);

            auto pushTerrainTri = [](std::vector<Vertex3D>& dst, const GridNode& a, const GridNode& b,
                                     const GridNode& c, const ColorRGBA& col) {
                const float cr = static_cast<float>(col.r) / 255.0f;
                const float cg = static_cast<float>(col.g) / 255.0f;
                const float cb = static_cast<float>(col.b) / 255.0f;
                const GridNode* pts[3] = {&a, &b, &c};
                for (int k = 0; k < 3; ++k) {
                    Vertex3D v{};
                    v.px = pts[k]->pos.x; v.py = pts[k]->pos.y; v.pz = pts[k]->pos.z;
                    v.nx = pts[k]->norm.x; v.ny = pts[k]->norm.y; v.nz = pts[k]->norm.z;
                    v.u = 0.0f; v.v = 0.0f;
                    v.r = cr; v.g = cg; v.b = cb; v.a = 1.0f;
                    dst.push_back(v);
                }
            };

            for (int iz = 0; iz < kNz; ++iz) {
                const std::size_t r0 = static_cast<std::size_t>(iz) * (kNx + 1);
                const std::size_t r1 = r0 + (kNx + 1);
                for (int ix = 0; ix < kNx; ++ix) {
                    const GridNode& n00 = grid[r0 + static_cast<std::size_t>(ix)];
                    const GridNode& n10 = grid[r0 + static_cast<std::size_t>(ix + 1)];
                    const GridNode& n11 = grid[r1 + static_cast<std::size_t>(ix + 1)];
                    const GridNode& n01 = grid[r1 + static_cast<std::size_t>(ix)];

                    const float avgH = 0.25f * (n00.rawH + n10.rawH + n11.rawH + n01.rawH);
                    const float avgNy = 0.25f * (n00.norm.y + n10.norm.y + n11.norm.y + n01.norm.y);
                    const bool allCore = n00.inCore && n10.inCore && n11.inCore && n01.inCore;
                    const bool anyWater = n00.inWater || n10.inWater || n11.inWater || n01.inWater;

                    if (avgH > 265.0f && avgNy < 0.90f) {
                        const ColorRGBA cRock{150, 148, 144, 255};
                        pushTerrainTri(rockV, n00, n01, n11, cRock);
                        pushTerrainTri(rockV, n00, n11, n10, cRock);
                    } else if (allCore && avgH < 88.0f && !anyWater) {
                        // Urban Seoul ground subbase (paver/concrete gray under blocks & courtyards!)
                        const ColorRGBA cUrb{148, 150, 154, 255};
                        pushTerrainTri(urbanV, n00, n01, n11, cUrb);
                        pushTerrainTri(urbanV, n00, n11, n10, cUrb);
                    } else {
                        // Korean mountains (Namsan, Bukhansan, Gwanaksan, Achasan) & riverbeds
                        const float hf = clampf((avgH - 40.0f) / 360.0f, 0.0f, 1.0f);
                        const ColorRGBA cFor = anyWater
                            ? ColorRGBA{68, 76, 70, 255}
                            : ColorRGBA{
                                static_cast<std::uint8_t>(lerpf(62.0f, 44.0f, hf)),
                                static_cast<std::uint8_t>(lerpf(104.0f, 76.0f, hf)),
                                static_cast<std::uint8_t>(lerpf(52.0f, 46.0f, hf)),
                                255
                            };
                        pushTerrainTri(forestV, n00, n01, n11, cFor);
                        pushTerrainTri(forestV, n00, n11, n10, cFor);
                    }
                }
            }
            if (!urbanV.empty())  realTerrainMesh_     = vk.createMesh(urbanV);
            if (!forestV.empty()) mountainForestMesh_  = vk.createMesh(forestV);
            if (!rockV.empty())   mountainGraniteMesh_ = vk.createMesh(rockV);
        }

        // 2. Real OpenStreetMap Water Mesh (3,581 exact Han River, island-holed, Seokchon Lake & stream tris!)
        {
            const auto& wTris = real.waterTris();
            std::vector<Vertex3D> wv;
            wv.reserve(wTris.size() * 3);
            for (const auto& t : wTris) {
                const Vec2 pts[3] = {{t.x0, t.z0}, {t.x1, t.z1}, {t.x2, t.z2}};
                for (int k = 0; k < 3; ++k) {
                    Vertex3D v{};
                    v.px = pts[k].x;
                    v.py = std::max(0.15f, real.sampleDem(pts[k]) + 0.05f);
                    v.pz = pts[k].z;
                    v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
                    v.u = 0.0f; v.v = 0.0f;
                    v.r = 24.0f / 255.0f; v.g = 72.0f / 255.0f; v.b = 118.0f / 255.0f; v.a = 1.0f;
                    wv.push_back(v);
                }
            }
            if (!wv.empty()) realWaterMesh_ = vk.createMesh(wv);
        }

        // 3. Real OpenStreetMap Parks Mesh (17,931 real Yeouido, Seoul Forest, Yongsan, Namsan, Olympic, Hangang Park tris!)
        //    Subdivide near roads and depress/skip triangles touching OCRM roads or sidewalks (ZERO green road/sidewalk pierces!)
        {
            const auto& pTris = real.parkTris();
            std::vector<Vertex3D> pv;
            pv.reserve(pTris.size() * 6);
            auto evalParkVertY = [&](const Vec2& p, bool& outOnOcrm, bool& outNearRoad) -> float {
                const float h = real.sampleDem(p);
                float py = h - 0.06f;
                outOnOcrm = false;
                outNearRoad = false;
                if (real.hasOcrmMesh()) {
                    float oy = 0.0f;
                    bool isSw = false;
                    if (real.ocrmMesh().sampleSurfaceHeightAt(p, h, oy, &isSw) && std::abs(oy - h) < 3.6f) {
                        outOnOcrm = true;
                        outNearRoad = true;
                        py = std::min(py, oy - 0.55f);
                    }
                }
                const NearestOsmRoad nr = real.findNearestRoad(p, 16.0f, h);
                if (nr.found && !nr.isBridge && std::abs(nr.deckY - h) < 3.4f) {
                    const float roadClear = nr.dist - std::max(2.6f, nr.halfWidth);
                    if (roadClear <= 6.8f) {
                        outNearRoad = true;
                        py = std::min(py, nr.deckY - 0.55f);
                    }
                }
                Vec2 bChk = p;
                if (real.resolveCircleBuildings(bChk, 0.55f)) {
                    outNearRoad = true;
                    py = std::min(py, h - 0.65f);
                }
                return py;
            };
            auto emitParkVert = [&](const Vec2& p, float py) {
                const float hL = real.sampleDem({p.x - 16.0f, p.z});
                const float hR = real.sampleDem({p.x + 16.0f, p.z});
                const float hD = real.sampleDem({p.x, p.z - 16.0f});
                const float hU = real.sampleDem({p.x, p.z + 16.0f});
                const Vec3 n = Vec3{hL - hR, 32.0f, hD - hU}.normalized();
                Vertex3D v{};
                v.px = p.x;
                v.py = py;
                v.pz = p.z;
                v.nx = n.x; v.ny = n.y; v.nz = n.z;
                v.u = 0.0f; v.v = 0.0f;
                v.r = 76.0f / 255.0f; v.g = 118.0f / 255.0f; v.b = 60.0f / 255.0f; v.a = 1.0f;
                pv.push_back(v);
            };
            auto emitParkTriRec = [&](auto&& self, const Vec2& a, const Vec2& b, const Vec2& c, int depth) -> void {
                const float dAB = (b - a).lengthSq();
                const float dBC = (c - b).lengthSq();
                const float dCA = (a - c).lengthSq();
                const float maxE2 = std::max({dAB, dBC, dCA});
                const Vec2 mid = (a + b + c) * (1.0f / 3.0f);
                const bool nearRd = (depth < 5) && (
                    real.isOnRoad(mid, 14.0f) ||
                    real.isOnRoad(a, 10.0f) ||
                    real.isOnRoad(b, 10.0f) ||
                    real.isOnRoad(c, 10.0f));
                const float splitThresh2 = nearRd ? (10.0f * 10.0f) : (28.0f * 28.0f);
                if (depth < (nearRd ? 5 : 3) && maxE2 > splitThresh2) {
                    if (dAB >= dBC && dAB >= dCA) {
                        const Vec2 m = (a + b) * 0.5f;
                        self(self, a, m, c, depth + 1);
                        self(self, m, b, c, depth + 1);
                        return;
                    } else if (dBC >= dCA) {
                        const Vec2 m = (b + c) * 0.5f;
                        self(self, a, b, m, depth + 1);
                        self(self, a, m, c, depth + 1);
                        return;
                    } else {
                        const Vec2 m = (c + a) * 0.5f;
                        self(self, a, b, m, depth + 1);
                        self(self, m, b, c, depth + 1);
                        return;
                    }
                }
                bool oA = false, oB = false, oC = false, oM = false;
                bool nA = false, nB = false, nC = false, nM = false;
                const float yA = evalParkVertY(a, oA, nA);
                const float yB = evalParkVertY(b, oB, nB);
                const float yC = evalParkVertY(c, oC, nC);
                (void)evalParkVertY(mid, oM, nM);
                // Skip park triangles directly overlapping OCRM roads/sidewalks at centroid or edge midpoints!
                if (oM || oA || oB || oC || nM || (nA && nB && nC)) return;
                bool oAB = false, oBC = false, oCA = false, dummy = false;
                (void)evalParkVertY((a + b) * 0.5f, oAB, dummy);
                (void)evalParkVertY((b + c) * 0.5f, oBC, dummy);
                (void)evalParkVertY((c + a) * 0.5f, oCA, dummy);
                if (oAB || oBC || oCA) return;
                emitParkVert(a, yA);
                emitParkVert(b, yB);
                emitParkVert(c, yC);
            };
            for (const auto& t : pTris) {
                emitParkTriRec(emitParkTriRec, {t.x0, t.z0}, {t.x1, t.z1}, {t.x2, t.z2}, 0);
            }
            if (!pv.empty()) realParksMesh_ = vk.createMesh(pv);
        }

        // 4. Distant Real Seoul Skyline LOD (Prioritize ALL real supertall towers >= 68m, then >= 46m, with true OCM4 RGB!)
        {
            distantSkyline_.clear();
            distantSkyline_.reserve(1300);
            const auto& bldgs = real.buildings();
            for (int pass = 0; pass < 2 && distantSkyline_.size() < 1300; ++pass) {
                const float minH = (pass == 0) ? 68.0f : 46.0f;
                const float maxH = (pass == 0) ? 9999.0f : 68.0f;
                const std::size_t stride = (pass == 0) ? 1u : std::max<std::size_t>(1, bldgs.size() / 6500);
                for (std::size_t i = 0; i < bldgs.size() && distantSkyline_.size() < 1300; i += stride) {
                    const OsmBldgRec& b = bldgs[i];
                    if (b.vertCount < 3 || b.height < minH || b.height >= maxH) continue;
                    if (b.halfX < 6.0f && b.halfZ < 6.0f) continue;
                    const bool isGlass = (b.style == 1u || b.style == 2u);
                    const ColorRGBA c{b.facadeR, b.facadeG, b.facadeB, 255u};
                    const float baseLift = (b.minHeight > 0.1f) ? b.minHeight : -2.0f;
                    const float spanH = std::max(4.0f, b.height - std::max(0.0f, b.minHeight) + ((b.minHeight > 0.1f) ? 0.0f : 2.0f));
                    distantSkyline_.push_back(DistantSkylineBlock{
                        {b.cx, b.cz},
                        {std::min(b.halfX * 2.0f, 76.0f), std::min(b.halfZ * 2.0f, 76.0f)},
                        b.baseY + baseLift, spanH, c, isGlass
                    });
                }
            }
        }
    } else {
    // ------------------------------------------------------------------------
    // 1. REAL S-CURVE HAN RIVER WATER + SEOKCHON LAKE + 4 SEOUL TRIBUTARIES!
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D waterMb;
        const int xSteps = 180;
        const float xMin = CityMap::kWorldMinX - 800.0f;
        const float xMax = CityMap::kWorldMaxX + 800.0f;
        const ColorRGBA kHanWater{24, 72, 118, 255};
        const float waterY = -0.18f;

        for (int i = 0; i < xSteps; ++i) {
            const float x0 = lerpf(xMin, xMax, static_cast<float>(i) / static_cast<float>(xSteps));
            const float x1 = lerpf(xMin, xMax, static_cast<float>(i + 1) / static_cast<float>(xSteps));
            const float cz0 = city.hanRiverCenterZ(x0);
            const float cz1 = city.hanRiverCenterZ(x1);
            const float hw0 = city.hanRiverHalfWidth(x0) + 18.0f;
            const float hw1 = city.hanRiverHalfWidth(x1) + 18.0f;

            waterMb.addQuad({x0, waterY, cz0 - hw0}, {x1, waterY, cz1 - hw1},
                            {x1, waterY, cz1},       {x0, waterY, cz0}, kHanWater);
            waterMb.addQuad({x0, waterY, cz0},       {x1, waterY, cz1},
                            {x1, waterY, cz1 + hw1}, {x0, waterY, cz0 + hw0}, kHanWater);
        }
        // Seokchon Lake twin basins
        for (const auto& lake : city.lakes()) {
            waterMb.addQuad({lake.bounds.min.x, lake.waterY, lake.bounds.min.z},
                            {lake.bounds.max.x, lake.waterY, lake.bounds.min.z},
                            {lake.bounds.max.x, lake.waterY, lake.bounds.max.z},
                            {lake.bounds.min.x, lake.waterY, lake.bounds.max.z},
                            {32, 92, 138, 255});
        }
        hanRiverWaterMesh_ = waterMb.upload(vk, 30.0f);

        // 4 Real Seoul Tributaries (Anyangcheon, Jungnangcheon, Tancheon, Cheonggyecheon!)
        MeshBuilder3D tribMb;
        const ColorRGBA kTribWater{30, 86, 134, 255};
        for (const auto& trib : city.tributaries()) {
            for (std::size_t i = 1; i < trib.points.size(); ++i) {
                const Vec2 a = trib.points[i - 1];
                const Vec2 b = trib.points[i];
                const Vec2 dir = (b - a).normalized();
                const Vec2 perp{-dir.z, dir.x};
                const float hw = trib.halfWidth;
                const float wy = -0.10f;
                tribMb.addQuad({a.x - perp.x * hw, wy, a.z - perp.z * hw},
                               {a.x + perp.x * hw, wy, a.z + perp.z * hw},
                               {b.x + perp.x * hw, wy, b.z + perp.z * hw},
                               {b.x - perp.x * hw, wy, b.z - perp.z * hw}, kTribWater);
            }
        }
        tributaryWaterMesh_ = tribMb.upload(vk, 30.0f);
    }

    // ------------------------------------------------------------------------
    // 1B. REAL CURVED RIVERSIDE EXPRESSWAYS (강변북로 & 올림픽대로 — 30km!)
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D expMb;
        const ColorRGBA kExpAsphalt{66, 68, 74, 255};
        for (const auto& art : city.arterials()) {
            if (!art.isExpressway || art.points.size() < 2) continue;
            // Only build curved riverside expressways (first 2: Gangbyeonbuk-ro & Olympic-daero) as continuous ribbons
            if (art.nameEn != "Gangbyeonbuk-ro" && art.nameEn != "Olympic-daero") continue;
            for (std::size_t i = 1; i < art.points.size(); ++i) {
                const Vec2 a = art.points[i - 1];
                const Vec2 b = art.points[i];
                const Vec2 dir = (b - a).normalized();
                const Vec2 perp{-dir.z, dir.x};
                const float hw = art.halfWidth;
                const float yA = city.terrainElevationAt(a) + 0.08f;
                const float yB = city.terrainElevationAt(b) + 0.08f;
                expMb.addQuad({a.x - perp.x * hw, yA, a.z - perp.z * hw},
                              {a.x + perp.x * hw, yA, a.z + perp.z * hw},
                              {b.x + perp.x * hw, yB, b.z + perp.z * hw},
                              {b.x - perp.x * hw, yB, b.z - perp.z * hw}, kExpAsphalt);
            }
        }
        riversideExpresswayMesh_ = expMb.upload(vk, 25.0f);
    }

    // ------------------------------------------------------------------------
    // 1D. HAN RIVER CONCRETE REVETMENTS (한강 호안 석축) & OUTER SEOUL RING HIGHWAY BARRIER!
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D embMb;
        const ColorRGBA kStoneEmb{146, 148, 152, 255};
        const ColorRGBA kGuardWall{162, 166, 172, 255};
        const int embSteps = 140;
        const float xMin = CityMap::kWorldMinX - 200.0f;
        const float xMax = CityMap::kWorldMaxX + 200.0f;
        for (int i = 0; i < embSteps; ++i) {
            const float x0 = lerpf(xMin, xMax, static_cast<float>(i) / static_cast<float>(embSteps));
            const float x1 = lerpf(xMin, xMax, static_cast<float>(i + 1) / static_cast<float>(embSteps));
            const float cz0 = city.hanRiverCenterZ(x0), cz1 = city.hanRiverCenterZ(x1);
            const float hw0 = city.hanRiverHalfWidth(x0), hw1 = city.hanRiverHalfWidth(x1);
            // Sloped stone embankments on North & South banks rising cleanly from water (-0.3m) to park (+0.35m)
            embMb.addQuad({x0, -0.35f, cz0 - hw0 + 2.0f}, {x1, -0.35f, cz1 - hw1 + 2.0f},
                          {x1,  0.35f, cz1 - hw1 - 8.0f}, {x0,  0.35f, cz0 - hw0 - 8.0f}, kStoneEmb);
            embMb.addQuad({x0,  0.35f, cz0 + hw0 + 8.0f}, {x1,  0.35f, cz1 + hw1 + 8.0f},
                          {x1, -0.35f, cz1 + hw1 - 2.0f}, {x0, -0.35f, cz0 + hw0 - 2.0f}, kStoneEmb);
        }
        // Clean Seoul Ring Expressway concrete retaining wall & acoustic barrier lining all 4 outer urban borders!
        const float minX = -14320.0f, maxX = 14320.0f;
        const float minZ = -11820.0f, maxZ = 11820.0f;
        const int bSteps = 44;
        for (int i = 0; i < bSteps; ++i) {
            const float f0 = static_cast<float>(i) / static_cast<float>(bSteps);
            const float f1 = static_cast<float>(i + 1) / static_cast<float>(bSteps);
            const float bx0 = lerpf(minX, maxX, f0), bx1 = lerpf(minX, maxX, f1);
            const float bz0 = lerpf(minZ, maxZ, f0), bz1 = lerpf(minZ, maxZ, f1);
            const float midX = 0.5f * (bx0 + bx1), midZ = 0.5f * (bz0 + bz1);
            const float segX = (bx1 - bx0) + 1.0f, segZ = (bz1 - bz0) + 1.0f;
            const ColorRGBA kAcousticPanel{68, 92, 78, 255};
            // North & South Ring Expressway barriers solidly grounded at y = 0.0m!
            embMb.addBox({midX, 1.0f, minZ}, {segX, 2.4f, 2.0f}, kGuardWall);
            embMb.addBox({midX, 2.85f, minZ}, {segX, 1.4f, 0.55f}, kAcousticPanel);
            embMb.addBox({midX, 1.0f, maxZ}, {segX, 2.4f, 2.0f}, kGuardWall);
            embMb.addBox({midX, 2.85f, maxZ}, {segX, 1.4f, 0.55f}, kAcousticPanel);
            // West & East Ring Expressway barriers (cleanly bridged across Han River!)
            if (!city.isInHanRiverWater({minX, midZ}, -12.0f)) {
                embMb.addBox({minX, 1.0f, midZ}, {2.0f, 2.4f, segZ}, kGuardWall);
                embMb.addBox({minX, 2.85f, midZ}, {0.55f, 1.4f, segZ}, kAcousticPanel);
            }
            if (!city.isInHanRiverWater({maxX, midZ}, -12.0f)) {
                embMb.addBox({maxX, 1.0f, midZ}, {2.0f, 2.4f, segZ}, kGuardWall);
                embMb.addBox({maxX, 2.85f, midZ}, {0.55f, 1.4f, segZ}, kAcousticPanel);
            }
        }
        hanEmbankmentMesh_ = embMb.upload(vk, 35.0f);
    }

    // ------------------------------------------------------------------------
    // 2. 7 INNER SEOUL MOUNTAINS + 14 OUTER BASIN RING MOUNTAINS + CONTINUOUS 3D GREENBELT FOOTHILL SKIRT!
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D forestMb, graniteMb;
        auto addMountainList = [&](const std::vector<SeoulMountain>& mList, int rings, int slices, bool dipOuterSkirt) {
            for (const auto& m : mList) {
                for (int r = 0; r < rings; ++r) {
                    const float f0 = static_cast<float>(r) / static_cast<float>(rings);
                    const float f1 = static_cast<float>(r + 1) / static_cast<float>(rings);
                    auto evalMtnPt = [&](float frac, float ang) -> Vec3 {
                        const float rx = m.radiusX * frac;
                        const float rz = m.radiusZ * frac;
                        const float wx = m.center.x + std::cos(ang) * rx;
                        const float wz = m.center.z + std::sin(ang) * rz;
                        const float h = (dipOuterSkirt && frac > 0.96f) ? -2.0f : city.terrainElevationAt({wx, wz});
                        return {wx, h, wz};
                    };

                    for (int s = 0; s < slices; ++s) {
                        const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(slices);
                        const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(slices);
                        const Vec3 p00 = evalMtnPt(f0, a0);
                        const Vec3 p10 = evalMtnPt(f0, a1);
                        const Vec3 p11 = evalMtnPt(f1, a1);
                        const Vec3 p01 = evalMtnPt(f1, a0);

                        const float avgH = 0.25f * (p00.y + p10.y + p11.y + p01.y);
                        const float hFrac = avgH / std::max(1.0f, m.peakHeight);
                        if (hFrac > 0.68f && m.peakHeight > 300.0f) {
                            const ColorRGBA cRock{148, 146, 142, 255};
                            if (r == 0) graniteMb.addTri(p00, p11, p01, cRock);
                            else        graniteMb.addQuad(p00, p10, p11, p01, cRock);
                        } else {
                            const std::uint8_t gR = static_cast<std::uint8_t>(lerpf(56.0f, 42.0f, hFrac));
                            const std::uint8_t gG = static_cast<std::uint8_t>(lerpf(94.0f, 74.0f, hFrac));
                            const std::uint8_t gB = static_cast<std::uint8_t>(lerpf(48.0f, 44.0f, hFrac));
                            const ColorRGBA cFor{gR, gG, gB, 255};
                            if (r == 0) forestMb.addTri(p00, p11, p01, cFor);
                            else        forestMb.addQuad(p00, p10, p11, p01, cFor);
                        }
                    }
                }
            }
        };
        addMountainList(city.mountains(), 10, 24, true);

        // Single continuous, watertight 3D triangulated greenbelt & outer mountain ring sampling terrainElevationAt() 1:1!
        // Uses non-linear radial spacing (tight ~30m strips right behind the Ring Expressway wall) so zero trees float!
        auto addTerrainBand = [&](float xA, float xB, int nx, bool expX, bool revX,
                                  float zA, float zB, int nz, bool expZ, bool revZ) {
            auto sampleAxis = [](float a, float b, float u, bool useExp, bool rev) -> float {
                if (!useExp) return lerpf(a, b, u);
                const float t = rev ? (1.0f - std::pow(1.0f - u, 1.75f)) : std::pow(u, 1.75f);
                return lerpf(a, b, t);
            };
            for (int iz = 0; iz < nz; ++iz) {
                const float uz0 = static_cast<float>(iz) / static_cast<float>(nz);
                const float uz1 = static_cast<float>(iz + 1) / static_cast<float>(nz);
                const float z0 = sampleAxis(zA, zB, uz0, expZ, revZ);
                const float z1 = sampleAxis(zA, zB, uz1, expZ, revZ);
                for (int ix = 0; ix < nx; ++ix) {
                    const float ux0 = static_cast<float>(ix) / static_cast<float>(nx);
                    const float ux1 = static_cast<float>(ix + 1) / static_cast<float>(nx);
                    const float x0 = sampleAxis(xA, xB, ux0, expX, revX);
                    const float x1 = sampleAxis(xA, xB, ux1, expX, revX);
                    const Vec2 mid{0.5f * (x0 + x1), 0.5f * (z0 + z1)};
                    if (city.isInHanRiverWater(mid, 12.0f)) continue;
                    const float y00 = city.terrainElevationAt({x0, z0});
                    const float y10 = city.terrainElevationAt({x1, z0});
                    const float y11 = city.terrainElevationAt({x1, z1});
                    const float y01 = city.terrainElevationAt({x0, z1});
                    const float hAvg = 0.25f * (y00 + y10 + y11 + y01);
                    if (hAvg > 275.0f) {
                        const ColorRGBA cRock{146, 144, 140, 255};
                        graniteMb.addQuad({x0, y00, z0}, {x0, y01, z1}, {x1, y11, z1}, {x1, y10, z0}, cRock);
                    } else {
                        const float hf = clampf(hAvg / 220.0f, 0.0f, 1.0f);
                        const ColorRGBA col{
                            static_cast<std::uint8_t>(lerpf(62.0f, 44.0f, hf)),
                            static_cast<std::uint8_t>(lerpf(104.0f, 76.0f, hf)),
                            static_cast<std::uint8_t>(lerpf(52.0f, 45.0f, hf)),
                            255
                        };
                        // Counter-clockwise upward-facing quad (+Y normal)
                        forestMb.addQuad({x0, y00, z0}, {x0, y01, z1}, {x1, y11, z1}, {x1, y10, z0}, col);
                    }
                }
            }
        };
        addTerrainBand( 14322.0f,  18800.0f, 24, true,  false, -16200.0f,  16200.0f, 96, false, false); // East Gyeonggi / Namhansan hills
        addTerrainBand(-18800.0f, -14322.0f, 24, true,  true,  -16200.0f,  16200.0f, 96, false, false); // West Gimpo / Gyeyangsan hills
        addTerrainBand(-14322.0f,  14322.0f, 96, false, false, -16200.0f, -11822.0f, 24, true,  true);  // North Bukhansan / Dobong ridge
        addTerrainBand(-14322.0f,  14322.0f, 96, false, false,  11822.0f,  16200.0f, 24, true,  false); // South Gwanak / Cheonggye ridge

        mountainForestMesh_ = forestMb.upload(vk, 55.0f);
        mountainGraniteMesh_ = graniteMb.upload(vk, 35.0f);
    }

    // ------------------------------------------------------------------------
    // 2B. DISTANT SEOUL SKYLINE LOD CLUSTERS (eliminates empty void horizon beyond 550m!)
    // ------------------------------------------------------------------------
    {
        distantSkyline_.clear();
        distantSkyline_.reserve(420);
        Rng skRng(20262200ULL);
        for (float wz = -10200.0f; wz <= 9600.0f; wz += 680.0f) {
            for (float wx = -13200.0f; wx <= 13200.0f; wx += 740.0f) {
                const Vec2 p{wx + skRng.range(-180.0f, 180.0f), wz + skRng.range(-180.0f, 180.0f)};
                if (city.isInHanRiverWater(p, 45.0f) || city.isInHanRiverPark(p) ||
                    city.isInSeokchonLake(p) || city.isInMajorSeoulPark(p) ||
                    city.terrainElevationAt(p) > 30.0f) {
                    continue;
                }
                const DistrictType dist = city.districtAtWorld(p);
                const bool isCbd = (dist == DistrictType::GangnamTeheran || dist == DistrictType::YeouidoFinance ||
                                    dist == DistrictType::MyeongdongCityHall || dist == DistrictType::JongnoCheonggye ||
                                    dist == DistrictType::Yongsan);
                const float h = isCbd ? skRng.range(64.0f, 152.0f) : skRng.range(36.0f, 78.0f);
                const float bw = isCbd ? skRng.range(38.0f, 64.0f) : skRng.range(54.0f, 86.0f);
                const float bd = isCbd ? skRng.range(38.0f, 62.0f) : skRng.range(24.0f, 44.0f);
                const ColorRGBA cbdPal[3] = {{68, 92, 118, 255}, {84, 102, 118, 255}, {122, 126, 134, 255}};
                const ColorRGBA aptPal[3] = {{206, 200, 190, 255}, {194, 196, 200, 255}, {182, 174, 162, 255}};
                distantSkyline_.push_back(DistantSkylineBlock{
                    p, {bw, bd}, city.terrainElevationAt(p), h,
                    isCbd ? cbdPal[skRng.rangeInt(0, 2)] : aptPal[skRng.rangeInt(0, 2)],
                    isCbd
                });
            }
        }
    }

    // ------------------------------------------------------------------------
    // 3. ALL 15 REAL DRIVABLE HAN RIVER BRIDGES (Decks, Piers, Arches,
    //    Olympic Bridge Cable-Stayed Pylons & Banpo Moonlight Rainbow Fountain!)
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D deckMb, concMb, steelMb, fountMb;
        const ColorRGBA kConcrete{168, 172, 178, 255};
        const ColorRGBA kPierDark{132, 136, 142, 255};
        const ColorRGBA kSteelSilver{196, 204, 214, 255};
        const ColorRGBA kAsphalt{72, 74, 80, 255};

        for (const auto& br : city.bridges()) {
            const float zStart = br.zNorth - 72.0f;
            const float zEnd   = br.zSouth + 72.0f;
            const int segs = 28;
            for (int i = 0; i < segs; ++i) {
                const float z0 = lerpf(zStart, zEnd, static_cast<float>(i) / static_cast<float>(segs));
                const float z1 = lerpf(zStart, zEnd, static_cast<float>(i + 1) / static_cast<float>(segs));
                const float y0 = city.bridgeDeckElevationAt({br.centerX, z0});
                const float y1 = city.bridgeDeckElevationAt({br.centerX, z1});
                const float xL = br.centerX - br.halfWidth;
                const float xR = br.centerX + br.halfWidth;

                deckMb.addQuad({xL, y0, z0}, {xR, y0, z0}, {xR, y1, z1}, {xL, y1, z1}, kAsphalt);

                const float thick = 1.65f;
                concMb.addQuad({xL, y0 - thick, z0}, {xL, y0, z0}, {xL, y1, z1}, {xL, y1 - thick, z1}, kConcrete);
                concMb.addQuad({xR, y0, z0}, {xR, y0 - thick, z0}, {xR, y1 - thick, z1}, {xR, y1, z1}, kConcrete);
                for (float s : {-1.0f, 1.0f}) {
                    const float px = br.centerX + s * (br.halfWidth + 0.35f);
                    concMb.addQuad({px, y0, z0}, {px, y0 + 1.05f, z0}, {px, y1 + 1.05f, z1}, {px, y1, z1}, kConcrete);
                }
            }

            const float spanLen = br.zSouth - br.zNorth;
            const int piers = std::max(5, static_cast<int>(spanLen / 78.0f));
            for (int p = 1; p < piers; ++p) {
                const float pz = lerpf(br.zNorth, br.zSouth, static_cast<float>(p) / static_cast<float>(piers));
                const float py = city.bridgeDeckElevationAt({br.centerX, pz});
                const float pierTop = py - 1.60f;
                const float pierBot = -3.0f;
                const float pierH = std::max(1.0f, pierTop - pierBot);
                for (float s : {-1.0f, 1.0f}) {
                    concMb.addBox({br.centerX + s * (br.halfWidth * 0.58f), 0.5f * (pierBot + pierTop), pz},
                                  {2.6f, pierH, 4.2f}, kPierDark);
                }
                concMb.addBox({br.centerX, py - 2.2f, pz}, {br.halfWidth * 1.95f, 1.3f, 3.6f}, kConcrete);
                for (float s : {-1.0f, 1.0f}) {
                    steelMb.addBox({br.centerX + s * (br.halfWidth + 0.4f), py + 4.4f, pz},
                                   {0.24f, 8.6f, 0.24f}, kSteelSilver);
                }
            }

            const float midZ = 0.5f * (br.zNorth + br.zSouth);
            const float midY = br.deckHeight;

            if (br.isCableStayed) {
                const float pylonH = 74.0f;
                steelMb.addCylinderY({br.centerX, midY - 1.5f, midZ}, 3.6f, 1.4f, pylonH, 8, br.accentColor);
                steelMb.addCylinderY({br.centerX, midY + pylonH - 2.0f, midZ}, 1.8f, 3.4f, 11.0f, 8, {236, 198, 72, 255});
                for (int c = 1; c <= 12; ++c) {
                    const float frac = static_cast<float>(c) / 12.0f;
                    const float cableZOff = 24.0f + frac * 175.0f;
                    const float topY = midY + pylonH * (0.45f + 0.50f * frac);
                    for (float dirZ : {-1.0f, 1.0f}) {
                        const Vec3 a{br.centerX, topY, midZ};
                        const Vec3 bPt{br.centerX, midY + 0.6f, midZ + dirZ * cableZOff};
                        const Vec3 dir = (bPt - a).normalized();
                        steelMb.addOrientedBox((a + bPt) * 0.5f, {0.22f, 0.22f, (bPt - a).length() * 0.5f},
                                               {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, dir, kSteelSilver);
                    }
                }
            }

            if (br.isArchBridge) {
                const int archSegs = 16;
                const float archHalfZ = 145.0f;
                const float archPeakH = 34.0f;
                for (float s : {-1.0f, 1.0f}) {
                    const float ax = br.centerX + s * (br.halfWidth + 0.5f);
                    for (int k = 0; k < archSegs; ++k) {
                        const float u0 = static_cast<float>(k) / static_cast<float>(archSegs);
                        const float u1 = static_cast<float>(k + 1) / static_cast<float>(archSegs);
                        const float z0 = lerpf(midZ - archHalfZ, midZ + archHalfZ, u0);
                        const float z1 = lerpf(midZ - archHalfZ, midZ + archHalfZ, u1);
                        const float y0 = midY + std::sin(u0 * kPi) * archPeakH;
                        const float y1 = midY + std::sin(u1 * kPi) * archPeakH;
                        const Vec3 p0{ax, y0, z0}, p1{ax, y1, z1};
                        const Vec3 dir = (p1 - p0).normalized();
                        steelMb.addOrientedBox((p0 + p1) * 0.5f, {0.85f, 1.25f, (p1 - p0).length() * 0.52f},
                                               {1, 0, 0}, {0, 1, 0}, dir, br.accentColor);
                        if (k > 0 && k < archSegs && (k % 2 == 0)) {
                            steelMb.addBox({ax, 0.5f * (midY + y0), z0}, {0.26f, y0 - midY, 0.26f}, kSteelSilver);
                        }
                    }
                }
            }

            if (br.hasRainbowFountain) {
                concMb.addBox({br.centerX, 2.2f, midZ}, {br.halfWidth * 1.75f, 1.1f, spanLen * 0.90f}, kPierDark);
                const int jets = 36;
                const ColorRGBA rainbow[6] = {
                    {84, 210, 255, 145}, {122, 162, 255, 145}, {198, 118, 255, 145},
                    {255, 116, 178, 145}, {255, 198, 92, 145}, {92, 242, 196, 145}
                };
                for (int j = 0; j < jets; ++j) {
                    const float zA = lerpf(br.zNorth + 90.0f, br.zSouth - 90.0f, static_cast<float>(j) / static_cast<float>(jets));
                    const float zB = lerpf(br.zNorth + 90.0f, br.zSouth - 90.0f, static_cast<float>(j + 1) / static_cast<float>(jets));
                    const ColorRGBA rc = rainbow[j % 6];
                    for (float s : {-1.0f, 1.0f}) {
                        const float x0 = br.centerX + s * (br.halfWidth + 0.6f);
                        const float xMid = br.centerX + s * (br.halfWidth + 10.5f);
                        const float xDrop = br.centerX + s * (br.halfWidth + 18.5f);
                        fountMb.addQuad({x0, midY - 0.2f, zA}, {x0, midY - 0.2f, zB},
                                        {xMid, midY * 0.65f, zB}, {xMid, midY * 0.65f, zA}, rc);
                        fountMb.addQuad({xMid, midY * 0.65f, zA}, {xMid, midY * 0.65f, zB},
                                        {xDrop, 0.0f, zB}, {xDrop, 0.0f, zA}, rc);
                    }
                }
            }
        }
        bridgeDecksMesh_    = deckMb.upload(vk, 20.0f);
        bridgeConcreteMesh_ = concMb.upload(vk, 20.0f);
        bridgeSteelMesh_    = steelMb.upload(vk, 24.0f);
        banpoFountainMesh_  = fountMb.upload(vk, 45.0f);
    }
    } // end else (analytic macro meshes)

    // ------------------------------------------------------------------------
    // 4. ALL 12 ICONIC REAL SEOUL MACRO LANDMARKS WITH TRUE ORIENTATION!
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D glassMb, stoneMb, metalMb, giwaMb, glowMb;

        auto rotXZ = [](float cx, float cz, float dx, float dz, float yaw) -> Vec2 {
            const float c = std::cos(yaw), s = std::sin(yaw);
            return {cx + dx * c - dz * s, cz + dx * s + dz * c};
        };

        auto addRotBox = [&](MeshBuilder3D& mb, float cx, float yMid, float cz,
                             float dx, float dz, Vec3 fullSize, float yaw, ColorRGBA col) {
            const Vec2 p = rotXZ(cx, cz, dx, dz, yaw);
            const Vec3 axX{std::cos(yaw), 0.0f, std::sin(yaw)};
            const Vec3 axY{0.0f, 1.0f, 0.0f};
            const Vec3 axZ{-std::sin(yaw), 0.0f, std::cos(yaw)};
            mb.addOrientedBox({p.x, yMid, p.z}, fullSize * 0.5f, axX, axY, axZ, col);
        };

        auto addPalaceGiwaRoof = [&](float cx, float yBase, float cz, float spanX, float spanZ, float roofH) {
            const ColorRGBA kRoof{54, 60, 68, 255};
            const ColorRGBA kRidge{96, 102, 114, 255};
            const int steps = 5;
            for (int i = 0; i < steps; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(steps);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(steps);
                const float y0 = yBase + std::pow(t0, 1.55f) * roofH;
                const float y1 = yBase + std::pow(t1, 1.55f) * roofH;
                const float rx0 = lerpf(spanX * 0.56f, spanX * 0.28f, t0);
                const float rx1 = lerpf(spanX * 0.56f, spanX * 0.28f, t1);
                const float rz0 = lerpf(spanZ * 0.56f, spanZ * 0.06f, t0);
                const float rz1 = lerpf(spanZ * 0.56f, spanZ * 0.06f, t1);
                const float lift0 = (i == 0) ? (roofH * 0.14f) : 0.0f;
                const float lift1 = (i == 0) ? (roofH * 0.04f) : 0.0f;
                giwaMb.addQuad({cx - rx0, y0 + lift0, cz + rz0}, {cx + rx0, y0 + lift0, cz + rz0},
                               {cx + rx1, y1 + lift1, cz + rz1}, {cx - rx1, y1 + lift1, cz + rz1}, kRoof);
                giwaMb.addQuad({cx + rx0, y0 + lift0, cz - rz0}, {cx - rx0, y0 + lift0, cz - rz0},
                               {cx - rx1, y1 + lift1, cz - rz1}, {cx + rx1, y1 + lift1, cz - rz1}, kRoof);
                giwaMb.addQuad({cx + rx0, y0 + lift0, cz + rz0}, {cx + rx0, y0 + lift0, cz - rz0},
                               {cx + rx1, y1 + lift1, cz - rz1}, {cx + rx1, y1 + lift1, cz + rz1}, kRoof);
                giwaMb.addQuad({cx - rx0, y0 + lift0, cz - rz0}, {cx - rx0, y0 + lift0, cz + rz0},
                               {cx - rx1, y1 + lift1, cz + rz1}, {cx - rx1, y1 + lift1, cz - rz1}, kRoof);
            }
            giwaMb.addBox({cx, yBase + roofH + 0.55f, cz}, {spanX * 0.60f, 1.1f, 1.4f}, kRidge);
        };

        for (const auto& lm : city.landmarks()) {
            const float lx = lm.pos.x;
            const float lz = lm.pos.z;
            const float baseY = city.terrainElevationAt(lm.pos);
            const float yaw = lm.yawRad;

            switch (lm.kind) {
                case SeoulLandmarkKind::LotteWorldTower: {
                    // 555m Lotte World Tower rotated to real Jamsil axis (-0.42 rad) with East Lotte World Mall!
                    const int tiers = 14;
                    const ColorRGBA kLotteGlass{162, 198, 226, 255};
                    const ColorRGBA kLotteFin{212, 224, 236, 255};
                    for (int t = 0; t < tiers; ++t) {
                        const float f0 = static_cast<float>(t) / static_cast<float>(tiers);
                        const float f1 = static_cast<float>(t + 1) / static_cast<float>(tiers);
                        const float y0 = baseY + f0 * 505.0f;
                        const float y1 = baseY + f1 * 505.0f;
                        const float w0 = lerpf(34.0f, 11.5f, std::pow(f0, 0.88f));
                        const float w1 = lerpf(34.0f, 11.5f, std::pow(f1, 0.88f));
                        for (int s = 0; s < 8; ++s) {
                            const float a0 = yaw + kTwoPi * (static_cast<float>(s) + 0.5f) / 8.0f;
                            const float a1 = yaw + kTwoPi * (static_cast<float>(s + 1) + 0.5f) / 8.0f;
                            const Vec3 p00{lx + std::cos(a0) * w0, y0, lz + std::sin(a0) * w0};
                            const Vec3 p10{lx + std::cos(a1) * w0, y0, lz + std::sin(a1) * w0};
                            const Vec3 p11{lx + std::cos(a1) * w1, y1, lz + std::sin(a1) * w1};
                            const Vec3 p01{lx + std::cos(a0) * w1, y1, lz + std::sin(a0) * w1};
                            glassMb.addQuad(p00, p01, p11, p10, kLotteGlass);
                        }
                    }
                    // Iconic notched split-lantern crown oriented SW toward Seokchon Lake!
                    for (float side : {-1.0f, 1.0f}) {
                        addRotBox(metalMb, lx, baseY + 528.0f, lz, side * 6.5f, 0.0f, {5.2f, 48.0f, 16.0f}, yaw, kLotteFin);
                    }
                    addRotBox(glowMb, lx, baseY + 552.0f, lz, 0.0f, 0.0f, {16.0f, 5.5f, 16.0f}, yaw, {195, 228, 255, 255});
                    // Lotte World Mall Podium on the EAST (+X) side of the tower (real Jamsil layout!)
                    stoneMb.addBox({lx + 76.0f, baseY + 16.0f, lz - 6.0f}, {84.0f, 40.0f, 76.0f}, {222, 224, 228, 255});
                    glassMb.addBox({lx + 76.0f, baseY + 26.0f, lz - 6.0f}, {86.0f, 12.0f, 78.0f}, {148, 186, 218, 255});
                    break;
                }

                case SeoulLandmarkKind::NSeoulTower: {
                    // N Seoul Tower + Traditional Namsan Octagonal Pavilion (팔각정) + Observation Deck!
                    stoneMb.addCylinderY({lx, baseY - 6.0f, lz}, 24.0f, 22.0f, 16.0f, 8, {178, 174, 168, 255});
                    stoneMb.addCylinderY({lx, baseY + 10.0f, lz}, 6.2f, 5.4f, 138.0f, 16, {236, 238, 242, 255});
                    glassMb.addCylinderY({lx, baseY + 148.0f, lz}, 12.5f, 16.5f, 11.0f, 16, {74, 132, 188, 255});
                    metalMb.addCylinderY({lx, baseY + 159.0f, lz}, 17.5f, 17.5f, 4.0f, 16, {220, 225, 232, 255});
                    glassMb.addCylinderY({lx, baseY + 163.0f, lz}, 16.0f, 14.5f, 12.0f, 16, {82, 142, 198, 255});
                    metalMb.addCylinderY({lx, baseY + 175.0f, lz}, 14.8f, 9.5f, 6.5f, 16, {214, 218, 226, 255});
                    glowMb.addCylinderY({lx, baseY + 145.0f, lz}, 14.0f, 14.0f, 3.5f, 16, {86, 212, 255, 255});
                    glowMb.addCylinderY({lx, baseY + 181.0f, lz}, 9.8f, 9.8f, 3.0f, 16, {120, 224, 255, 255});
                    metalMb.addCylinderY({lx, baseY + 182.0f, lz}, 3.2f, 0.55f, 76.0f, 12, {226, 230, 236, 255});
                    glowMb.addSphere({lx, baseY + 259.0f, lz}, 2.4f, 8, 12, {255, 58, 52, 255});
                    // Iconic Namsan Octagonal Pavilion (팔각정) on SW terrace!
                    const float px = lx - 28.0f, pz = lz + 18.0f;
                    const float py = city.terrainElevationAt({px, pz});
                    stoneMb.addCylinderY({px, py - 4.0f, pz}, 10.0f, 9.5f, 6.0f, 8, {194, 190, 184, 255});
                    stoneMb.addCylinderY({px, py + 2.0f, pz}, 7.6f, 7.6f, 6.8f, 8, {164, 48, 40, 255});
                    addPalaceGiwaRoof(px, py + 8.6f, pz, 20.0f, 20.0f, 5.2f);
                    break;
                }

                case SeoulLandmarkKind::GyeongbokgungPalace: {
                    // Gyeongbokgung Geunjeongjeon Throne Hall + 3-Arch Gwanghwamun Gate + Gwanghwamun Square Statues (Facing South +Z!)
                    stoneMb.addBox({lx, baseY + 0.0f, lz}, {68.0f, 8.0f, 52.0f}, {206, 202, 194, 255});
                    stoneMb.addBox({lx, baseY + 5.2f, lz}, {56.0f, 2.8f, 42.0f}, {214, 210, 202, 255});
                    stoneMb.addBox({lx, baseY + 11.5f, lz}, {40.0f, 10.5f, 28.0f}, {162, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 16.8f, lz}, {42.5f, 1.8f, 30.5f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 17.6f, lz, 48.0f, 36.0f, 6.2f);
                    stoneMb.addBox({lx, baseY + 24.5f, lz}, {30.0f, 5.8f, 20.0f}, {168, 48, 40, 255});
                    stoneMb.addBox({lx, baseY + 27.6f, lz}, {32.0f, 1.4f, 22.0f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 28.3f, lz, 38.0f, 27.0f, 8.2f);

                    // Gwanghwamun Gate 142m south of Geunjeongjeon — grounded to exact local terrain elevation!
                    const float gz = lz + 142.0f;
                    const float gateY = city.terrainElevationAt({lx, gz});
                    stoneMb.addBox({lx - 22.0f, gateY + 2.5f, gz}, {14.0f, 13.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx -  6.5f, gateY + 2.5f, gz}, { 4.8f, 13.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx +  6.5f, gateY + 2.5f, gz}, { 4.8f, 13.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx + 22.0f, gateY + 2.5f, gz}, {14.0f, 13.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx, gateY + 9.2f, gz}, {58.0f, 2.8f, 14.2f}, {208, 202, 192, 255});
                    stoneMb.addBox({lx, gateY + 13.8f, gz}, {34.0f, 6.6f, 12.0f}, {166, 46, 38, 255});
                    stoneMb.addBox({lx, gateY + 17.2f, gz}, {36.0f, 1.3f, 13.5f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, gateY + 17.8f, gz, 44.0f, 19.0f, 6.4f);
                    // Palace Stone Rampart Walls (궁장) running East & West from Gwanghwamun!
                    stoneMb.addBox({lx - 86.0f, gateY + 0.8f, gz}, {116.0f, 9.6f, 2.4f}, {202, 196, 186, 255});
                    stoneMb.addBox({lx + 86.0f, gateY + 0.8f, gz}, {116.0f, 9.6f, 2.4f}, {202, 196, 186, 255});
                    // Gwanghwamun Square Iconic Statues placed on the exact renovated Sejong-daero pedestrian plaza
                    // (west of Sejong-daero traffic, clear of OSM walls & grounded flush to local square elevation!)
                    const float sjX = city.hasRealSeoulMap() ? -726.0f : lx;
                    const float sjZ = city.hasRealSeoulMap() ? -5488.0f : (gz + 205.0f);
                    const float sjY = city.terrainElevationAt({sjX, sjZ});
                    stoneMb.addBox({sjX, sjY + 0.1f, sjZ}, {7.6f, 4.2f, 6.8f}, {198, 194, 186, 255});
                    stoneMb.addBox({sjX, sjY + 2.55f, sjZ}, {5.4f, 0.85f, 4.8f}, {184, 180, 172, 255});
                    const ColorRGBA kSejongBronze{184, 144, 62, 255};
                    metalMb.addBox({sjX, sjY + 3.65f, sjZ}, {3.2f, 1.4f, 2.8f}, kSejongBronze); // Seated royal robe base
                    metalMb.addBox({sjX, sjY + 4.95f, sjZ - 0.15f}, {2.0f, 1.55f, 1.45f}, kSejongBronze); // Royal torso
                    metalMb.addSphere({sjX, sjY + 6.05f, sjZ - 0.10f}, 0.56f, 7, 10, kSejongBronze); // Head + 익선관
                    // Admiral Yi Sun-sin (충무공 이순신 장군상 — grounded to exact Gwanghwamun Square surface!)
                    const float yiX = city.hasRealSeoulMap() ? -726.0f : lx;
                    const float yiZ = city.hasRealSeoulMap() ? -5320.0f : (gz + 415.0f);
                    const float yiY = city.terrainElevationAt({yiX, yiZ});
                    stoneMb.addBox({yiX, yiY + 0.2f, yiZ}, {6.8f, 4.4f, 6.8f}, {188, 184, 176, 255});
                    stoneMb.addBox({yiX, yiY + 5.2f, yiZ}, {3.8f, 5.8f, 3.8f}, {178, 174, 168, 255});
                    const ColorRGBA kYiBronze{72, 68, 58, 255};
                    metalMb.addCylinderY({yiX, yiY + 8.1f, yiZ}, 0.85f, 0.72f, 3.6f, 10, kYiBronze);
                    metalMb.addSphere({yiX, yiY + 12.1f, yiZ}, 0.48f, 7, 10, kYiBronze);
                    break;
                }

                case SeoulLandmarkKind::Building63: {
                    // Iconic Yeouido 63 Building: TRUE smooth continuous golden tapered wedge prism rotated 27.5 deg facing Han River!
                    const ColorRGBA kGoldGlass{226, 182, 78, 255};
                    const float hTop = 249.0f;
                    const float hxBot = 28.0f, hxTop = 19.5f;
                    const float hzBot = 16.5f, hzTop =  4.8f;
                    const Vec2 b0 = rotXZ(lx, lz, -hxBot, -hzBot, yaw);
                    const Vec2 b1 = rotXZ(lx, lz,  hxBot, -hzBot, yaw);
                    const Vec2 b2 = rotXZ(lx, lz,  hxBot,  hzBot, yaw);
                    const Vec2 b3 = rotXZ(lx, lz, -hxBot,  hzBot, yaw);
                    const Vec2 t0 = rotXZ(lx, lz, -hxTop, -hzTop, yaw);
                    const Vec2 t1 = rotXZ(lx, lz,  hxTop, -hzTop, yaw);
                    const Vec2 t2 = rotXZ(lx, lz,  hxTop,  hzTop, yaw);
                    const Vec2 t3 = rotXZ(lx, lz, -hxTop,  hzTop, yaw);
                    // 4 continuous smooth sloped golden glass facades anchored -4m below grade!
                    const float yB63 = baseY - 4.0f;
                    glassMb.addQuad({b0.x, yB63, b0.z}, {t0.x, baseY + hTop, t0.z}, {t1.x, baseY + hTop, t1.z}, {b1.x, yB63, b1.z}, kGoldGlass);
                    glassMb.addQuad({b1.x, yB63, b1.z}, {t1.x, baseY + hTop, t1.z}, {t2.x, baseY + hTop, t2.z}, {b2.x, yB63, b2.z}, kGoldGlass);
                    glassMb.addQuad({b2.x, yB63, b2.z}, {t2.x, baseY + hTop, t2.z}, {t3.x, baseY + hTop, t3.z}, {b3.x, yB63, b3.z}, kGoldGlass);
                    glassMb.addQuad({b3.x, yB63, b3.z}, {t3.x, baseY + hTop, t3.z}, {t0.x, baseY + hTop, t0.z}, {b0.x, yB63, b0.z}, kGoldGlass);
                    // Roof cap & golden crown glow
                    glassMb.addQuad({t0.x, baseY + hTop, t0.z}, {t3.x, baseY + hTop, t3.z}, {t2.x, baseY + hTop, t2.z}, {t1.x, baseY + hTop, t1.z}, kGoldGlass);
                    addRotBox(glowMb, lx, baseY + hTop + 1.8f, lz, 0.0f, 0.0f, {36.0f, 3.2f, 9.2f}, yaw, {255, 224, 128, 255});
                    break;
                }

                case SeoulLandmarkKind::NationalAssembly: {
                    // National Assembly of South Korea: Oriented facing EAST-SOUTHEAST (+X/+Z, yaw=0.18) down Uisadang-daero!
                    addRotBox(stoneMb, lx, baseY + 12.0f, lz, 0.0f, 0.0f, {56.0f, 32.0f, 82.0f}, yaw, {214, 212, 206, 255});
                    // Monumental East-facing ceremonial granite staircase (+X front)!
                    for (int st = 0; st < 4; ++st) {
                        const float sf = static_cast<float>(st);
                        addRotBox(stoneMb, lx, baseY - 0.6f + sf * 1.4f, lz, 32.0f + (3.0f - sf) * 3.8f, 0.0f,
                                  {6.0f, 4.5f, 38.0f}, yaw, {222, 220, 214, 255});
                    }
                    // 24 vertical classical granite columns around perimeter (front & back face East/West!)
                    for (int c = -5; c <= 5; ++c) {
                        const float czOff = static_cast<float>(c) * 7.2f;
                        addRotBox(stoneMb, lx, baseY + 12.5f, lz,  29.2f, czOff, {2.3f, 31.5f, 2.3f}, yaw, {228, 226, 220, 255});
                        addRotBox(stoneMb, lx, baseY + 12.5f, lz, -29.2f, czOff, {2.3f, 31.5f, 2.3f}, yaw, {228, 226, 220, 255});
                    }
                    // Iconic ribbed mint-green oxidized copper dome!
                    metalMb.addCylinderY({lx, baseY + 28.0f, lz}, 24.0f, 22.0f, 8.0f, 20, {196, 194, 188, 255});
                    metalMb.addSphere({lx, baseY + 36.0f, lz}, 22.5f, 10, 20, {72, 154, 134, 255});
                    break;
                }

                case SeoulLandmarkKind::Parc1RedTower: {
                    // Yeouido Parc1 Towers (333m + 256m) rotated along Yeouido-daero with signature crimson exoskeleton!
                    addRotBox(glassMb, lx, baseY + 164.0f, lz, -28.0f, 0.0f, {34.0f, 336.0f, 34.0f}, yaw, {132, 164, 192, 255});
                    addRotBox(glassMb, lx, baseY + 126.0f, lz,  34.0f, 8.0f, {32.0f, 260.0f, 32.0f}, yaw, {136, 168, 196, 255});
                    const ColorRGBA kParcRed{208, 36, 44, 255};
                    for (float sx : {-1.0f, 1.0f}) {
                        for (float sz : {-1.0f, 1.0f}) {
                            addRotBox(metalMb, lx, baseY + 164.0f, lz, -28.0f + sx * 17.6f, sz * 17.6f, {2.8f, 338.0f, 2.8f}, yaw, kParcRed);
                            addRotBox(metalMb, lx, baseY + 126.0f, lz,  34.0f + sx * 16.6f, 8.0f + sz * 16.6f, {2.6f, 262.0f, 2.6f}, yaw, kParcRed);
                        }
                    }
                    for (int h = 1; h <= 5; ++h) {
                        addRotBox(metalMb, lx, baseY + static_cast<float>(h) * 58.0f, lz, -28.0f, 0.0f, {37.5f, 3.2f, 37.5f}, yaw, kParcRed);
                    }
                    break;
                }

                case SeoulLandmarkKind::CoexTradeTower: {
                    // Samseong-dong COEX Trade Tower (228m stepped chevron facing East onto Yeongdong-daero!) + NW COEX Mall
                    glassMb.addBox({lx, baseY + 93.0f, lz}, {34.0f, 194.0f, 38.0f}, {148, 184, 214, 255});
                    glassMb.addBox({lx, baseY + 206.0f, lz}, {26.0f, 32.0f, 28.0f}, {162, 196, 224, 255});
                    glassMb.addBox({lx, baseY + 225.0f, lz}, {18.0f, 12.0f, 20.0f}, {176, 208, 232, 255});
                    // COEX Convention Center sprawling Northwest behind Trade Tower
                    stoneMb.addBox({lx - 64.0f, baseY + 13.0f, lz - 42.0f}, {86.0f, 34.0f, 72.0f}, {198, 204, 212, 255});
                    glassMb.addBox({lx - 64.0f, baseY + 31.0f, lz - 42.0f}, {82.0f, 4.5f, 68.0f}, {132, 174, 208, 255});
                    glowMb.addBox({lx, baseY + 232.0f, lz}, {18.5f, 2.8f, 20.5f}, {122, 212, 255, 255});
                    break;
                }

                case SeoulLandmarkKind::SeoulCityHall: {
                    // Seoul City Hall facing SOUTH (+Z) over Seoul Metropolitan Library & green Seoul Plaza!
                    // Curving stepped overhang wave glass hall behind (North of) historic library
                    for (int tier = 0; tier < 5; ++tier) {
                        const float tf = static_cast<float>(tier) / 4.0f;
                        const float yMid = baseY + 6.0f + tf * 44.0f;
                        const float zPush = -20.0f + std::pow(tf, 1.45f) * 18.0f; // Iconic southward wave overhang!
                        glassMb.addBox({lx, yMid, lz + zPush}, {66.0f, 11.5f, 22.0f + tf * 10.0f}, {126, 182, 224, 255});
                    }
                    stoneMb.addBox({lx, baseY - 2.0f, lz - 20.0f}, {66.0f, 4.5f, 22.0f}, {132, 138, 146, 255});
                    // Historic stone Seoul Metropolitan Library (Old City Hall) directly South facing Seoul Plaza!
                    stoneMb.addBox({lx, baseY + 9.0f, lz + 18.0f}, {54.0f, 26.0f, 18.0f}, {208, 200, 186, 255});
                    stoneMb.addBox({lx, baseY + 25.0f, lz + 18.0f}, {16.0f, 9.0f, 14.0f}, {214, 206, 192, 255});
                    metalMb.addCylinderY({lx, baseY + 29.5f, lz + 18.0f}, 6.2f, 2.2f, 5.5f, 12, {82, 134, 122, 255});
                    break;
                }

                case SeoulLandmarkKind::SungnyemunGate: {
                    // Sungnyemun (Namdaemun): centered on Sejong-daero with N-S stone arch & watertight -4m stone base!
                    stoneMb.addBox({lx - 12.0f, baseY + 2.5f, lz}, {13.0f, 13.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx + 12.0f, baseY + 2.5f, lz}, {13.0f, 13.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx, baseY + 8.8f, lz}, {37.0f, 2.8f, 14.0f}, {192, 186, 178, 255});
                    // Short Hanyangdoseong stone wall wings extending East & West
                    stoneMb.addBox({lx - 26.0f, baseY + 0.8f, lz}, {16.0f, 9.6f, 3.6f}, {182, 176, 168, 255});
                    stoneMb.addBox({lx + 26.0f, baseY + 0.8f, lz}, {16.0f, 9.6f, 3.6f}, {182, 176, 168, 255});
                    stoneMb.addBox({lx, baseY + 13.4f, lz}, {25.0f, 6.2f, 11.0f}, {164, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 16.4f, lz}, {26.5f, 1.2f, 12.2f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 16.8f, lz, 32.0f, 16.5f, 4.8f);
                    stoneMb.addBox({lx, baseY + 22.8f, lz}, {18.5f, 4.2f, 9.0f}, {164, 46, 38, 255});
                    addPalaceGiwaRoof(lx, baseY + 24.8f, lz, 26.0f, 13.5f, 5.4f);
                    break;
                }

                case SeoulLandmarkKind::DongdaemunDDP: {
                    // Dongdaemun Design Plaza (DDP): sculpted Zaha Hadid sweeping curved brushed-aluminium pavilion
                    // with recessed dark-glass exhibition ribbons, cantilevered upper shell & glowing skylight seams!
                    const int segs = 16;
                    for (int s = 0; s < segs; ++s) {
                        const float u = static_cast<float>(s) / static_cast<float>(segs - 1);
                        const float dx = lerpf(-58.0f, 58.0f, u);
                        const float dz = std::sin(u * kPi * 1.4f) * 18.0f;
                        const float segYaw = yaw + std::cos(u * kPi * 1.4f) * 0.25f;
                        const float w = 21.0f + 10.5f * std::sin(u * kPi);
                        const float h = 15.5f + 11.0f * std::sin(u * kPi);
                        // Recessed dark granite plinth & 1F panoramic exhibition glass gallery ribbon
                        addRotBox(stoneMb, lx, baseY + 0.3f, lz, dx, dz, {12.35f, 5.6f, w * 0.935f}, segYaw, {104, 110, 118, 255});
                        addRotBox(glassMb, lx, baseY + 4.6f, lz, dx, dz, {12.50f, 3.6f, w * 0.945f}, segYaw, {34, 48, 64, 255});
                        // Sculpted silver-aluminium cantilevered upper shell with stepped horizontal panel seams
                        addRotBox(metalMb, lx, baseY + 8.6f, lz, dx, dz, {13.6f, 4.8f, w * 1.02f}, segYaw, {204, 212, 222, 255});
                        addRotBox(metalMb, lx, baseY + 11.4f, lz, dx, dz, {13.0f, 0.55f, w * 1.04f}, segYaw, {132, 140, 152, 255});
                        const float upperH = std::max(3.5f, h - 11.8f);
                        addRotBox(metalMb, lx, baseY + 11.8f + upperH * 0.5f, lz, dx, dz, {12.8f, upperH, w * 0.96f}, segYaw, {218, 224, 232, 255});
                        // Upper mezzanine ribbon window band
                        if (s >= 2 && s <= segs - 3) {
                            addRotBox(glassMb, lx, baseY + 13.8f, lz, dx, dz, {13.1f, 2.1f, w * 0.985f}, segYaw, {42, 60, 78, 255});
                        }
                        if (s % 2 == 0) {
                            addRotBox(glowMb, lx, baseY + h + 0.45f, lz, dx, dz, {11.4f, 0.75f, w * 0.70f}, segYaw, {192, 228, 255, 255});
                        }
                    }
                    break;
                }

                case SeoulLandmarkKind::JamsilStadium: {
                    // Jamsil Olympic Stadium: TRUE continuous curved oval concrete bowl (N-S major axis) + green pitch & red track!
                    const int segs = 32;
                    const float rxOut = 72.0f, rzOut = 92.0f;
                    const float rxIn  = 52.0f, rzIn  = 70.0f;
                    for (int s = 0; s < segs; ++s) {
                        const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(segs);
                        const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(segs);
                        const float h0 = 28.0f + 14.0f * std::abs(std::cos(a0)); // Iconic sweeping roof saddle!
                        const float h1 = 28.0f + 14.0f * std::abs(std::cos(a1));
                        const Vec3 o0b{lx + std::cos(a0) * rxOut, baseY - 4.0f, lz + std::sin(a0) * rzOut};
                        const Vec3 o1b{lx + std::cos(a1) * rxOut, baseY - 4.0f, lz + std::sin(a1) * rzOut};
                        const Vec3 o0t{lx + std::cos(a0) * rxOut, baseY + h0, lz + std::sin(a0) * rzOut};
                        const Vec3 o1t{lx + std::cos(a1) * rxOut, baseY + h1, lz + std::sin(a1) * rzOut};
                        const Vec3 i0t{lx + std::cos(a0) * rxIn,  baseY + h0 * 0.95f, lz + std::sin(a0) * rzIn};
                        const Vec3 i1t{lx + std::cos(a1) * rxIn,  baseY + h1 * 0.95f, lz + std::sin(a1) * rzIn};
                        stoneMb.addQuad(o0b, o0t, o1t, o1b, {212, 216, 222, 255});
                        metalMb.addQuad(o0t, i0t, i1t, o1t, {194, 200, 210, 255});
                    }
                    // Red oval running track & lush green soccer pitch inside!
                    stoneMb.addCylinderY({lx, baseY + 0.12f, lz}, 50.0f, 50.0f, 0.25f, 24, {168, 68, 54, 255});
                    stoneMb.addBox({lx, baseY + 0.42f, lz}, {56.0f, 0.22f, 92.0f}, {52, 128, 58, 255});
                    break;
                }

                case SeoulLandmarkKind::GangnamStationPlaza: {
                    if (city.hasRealSeoulMap()) break; // Real OSM Gangnam Station towers are already extruded 1:1!
                    // Gangnam Station 4-corner modern glass media skyscrapers framing Gangnam-daero x Teheran-ro!
                    glassMb.addBox({lx - 42.0f, baseY + 72.0f, lz - 36.0f}, {32.0f, 144.0f, 32.0f}, {118, 164, 210, 255});
                    glassMb.addBox({lx + 42.0f, baseY + 86.0f, lz + 36.0f}, {34.0f, 172.0f, 34.0f}, {110, 154, 204, 255});
                    glassMb.addBox({lx + 44.0f, baseY + 62.0f, lz - 40.0f}, {30.0f, 124.0f, 30.0f}, {132, 166, 198, 255});
                    glowMb.addBox({lx - 42.0f, baseY + 145.0f, lz - 36.0f}, {33.0f, 3.2f, 33.0f}, {86, 208, 255, 255});
                    glowMb.addBox({lx + 42.0f, baseY + 173.0f, lz + 36.0f}, {35.0f, 3.2f, 35.0f}, {110, 224, 255, 255});
                    break;
                }
            }
        }

        landmarkGlassMesh_ = glassMb.upload(vk, 24.0f);
        landmarkStoneMesh_ = stoneMb.upload(vk, 20.0f);
        landmarkMetalMesh_ = metalMb.upload(vk, 28.0f);
        landmarkGiwaMesh_  = giwaMb.upload(vk, 26.0f);
        landmarkGlowMesh_  = glowMb.upload(vk, 24.0f);
    }
}

namespace {

inline void pushQuadV3(std::vector<Vertex3D>& dst,
                       const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                       const Vec3& n,
                       float ua, float va, float ub, float vb, float uc, float vc, float ud, float vd,
                       const ColorRGBA& col) {
    const float cr = static_cast<float>(col.r) / 255.0f;
    const float cg = static_cast<float>(col.g) / 255.0f;
    const float cb = static_cast<float>(col.b) / 255.0f;
    const float ca = static_cast<float>(col.a) / 255.0f;
    auto mk = [&](const Vec3& p, float u, float v) -> Vertex3D {
        Vertex3D vv{};
        vv.px = p.x; vv.py = p.y; vv.pz = p.z;
        vv.nx = n.x; vv.ny = n.y; vv.nz = n.z;
        vv.u = u;    vv.v = v;
        vv.r = cr;   vv.g = cg;   vv.b = cb;   vv.a = ca;
        return vv;
    };
    const Vertex3D va3 = mk(a, ua, va);
    const Vertex3D vb3 = mk(b, ub, vb);
    const Vertex3D vc3 = mk(c, uc, vc);
    const Vertex3D vd3 = mk(d, ud, vd);
    dst.push_back(va3); dst.push_back(vb3); dst.push_back(vc3);
    dst.push_back(va3); dst.push_back(vc3); dst.push_back(vd3);
}

inline void pushBoxPiers(std::vector<Vertex3D>& dst, const Vec3& center, const Vec3& sz, const ColorRGBA& col) {
    const float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;
    const Vec3 p000{center.x - hx, center.y - hy, center.z - hz};
    const Vec3 p100{center.x + hx, center.y - hy, center.z - hz};
    const Vec3 p110{center.x + hx, center.y + hy, center.z - hz};
    const Vec3 p010{center.x - hx, center.y + hy, center.z - hz};
    const Vec3 p001{center.x - hx, center.y - hy, center.z + hz};
    const Vec3 p101{center.x + hx, center.y - hy, center.z + hz};
    const Vec3 p111{center.x + hx, center.y + hy, center.z + hz};
    const Vec3 p011{center.x - hx, center.y + hy, center.z + hz};
    pushQuadV3(dst, p001, p101, p111, p011, { 0,  0,  1}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p100, p000, p010, p110, { 0,  0, -1}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p101, p100, p110, p111, { 1,  0,  0}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p000, p001, p011, p010, {-1,  0,  0}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p011, p111, p110, p010, { 0,  1,  0}, 0,0, 1,0, 1,1, 0,1, col);
}

inline void pushOrientedBoxPiers(std::vector<Vertex3D>& dst, const Vec3& center,
                                 const Vec2& axisR, const Vec2& axisF,
                                 float fullW, float fullH, float fullL,
                                 const ColorRGBA& col) {
    const Vec3 r{axisR.x * (fullW * 0.5f), 0.0f, axisR.z * (fullW * 0.5f)};
    const Vec3 f{axisF.x * (fullL * 0.5f), 0.0f, axisF.z * (fullL * 0.5f)};
    const Vec3 u{0.0f, fullH * 0.5f, 0.0f};
    const Vec3 p000 = center - r - u - f;
    const Vec3 p100 = center + r - u - f;
    const Vec3 p101 = center + r - u + f;
    const Vec3 p001 = center - r - u + f;
    const Vec3 p010 = center - r + u - f;
    const Vec3 p110 = center + r + u - f;
    const Vec3 p111 = center + r + u + f;
    const Vec3 p011 = center - r + u + f;
    const Vec3 nR{axisR.x, 0.0f, axisR.z};
    const Vec3 nF{axisF.x, 0.0f, axisF.z};
    pushQuadV3(dst, p010, p110, p111, p011, { 0,  1,  0}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p001, p101, p100, p000, { 0, -1,  0}, 0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p100, p101, p111, p110,  nR,          0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p001, p000, p010, p011,  nR * -1.0f,  0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p101, p001, p011, p111,  nF,          0,0, 1,0, 1,1, 0,1, col);
    pushQuadV3(dst, p000, p100, p110, p010,  nF * -1.0f,  0,0, 1,0, 1,1, 0,1, col);
}

inline float cross2DXZ(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

inline bool pointInTri2DCCW(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return cross2DXZ(a, b, p) >= -1e-4f &&
           cross2DXZ(b, c, p) >= -1e-4f &&
           cross2DXZ(c, a, p) >= -1e-4f;
}

inline void pushTriV3(std::vector<Vertex3D>& dst, const Vec3& a, const Vec3& b, const Vec3& c,
                      const Vec3& n, const ColorRGBA& col) {
    const float cr = static_cast<float>(col.r) / 255.0f;
    const float cg = static_cast<float>(col.g) / 255.0f;
    const float cb = static_cast<float>(col.b) / 255.0f;
    const float ca = static_cast<float>(col.a) / 255.0f;
    auto mk = [&](const Vec3& p) -> Vertex3D {
        Vertex3D vv{};
        vv.px = p.x; vv.py = p.y; vv.pz = p.z;
        vv.nx = n.x; vv.ny = n.y; vv.nz = n.z;
        vv.u = 0.0f; vv.v = 0.0f;
        vv.r = cr; vv.g = cg; vv.b = cb; vv.a = ca;
        return vv;
    };
    dst.push_back(mk(a));
    dst.push_back(mk(b));
    dst.push_back(mk(c));
}

void earClipRoofXZ(const std::vector<Vec2>& poly, float roofY, const ColorRGBA& col,
                   std::vector<Vertex3D>& dst, bool flipDown = false) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return;
    const float cr = static_cast<float>(col.r) / 255.0f;
    const float cg = static_cast<float>(col.g) / 255.0f;
    const float cb = static_cast<float>(col.b) / 255.0f;
    const float ca = static_cast<float>(col.a) / 255.0f;
    const float ny = flipDown ? -1.0f : 1.0f;
    auto emitTri = [&](const Vec2& a, const Vec2& b, const Vec2& c) {
        const Vec2 pts[3] = {a, flipDown ? c : b, flipDown ? b : c};
        for (int k = 0; k < 3; ++k) {
            Vertex3D v{};
            v.px = pts[k].x; v.py = roofY; v.pz = pts[k].z;
            v.nx = 0.0f; v.ny = ny; v.nz = 0.0f;
            v.u = 0.0f; v.v = 0.0f;
            v.r = cr; v.g = cg; v.b = cb; v.a = ca;
            dst.push_back(v);
        }
    };

    if (n == 3) {
        emitTri(poly[0], poly[1], poly[2]);
        return;
    }

    std::vector<int> idx(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) idx[static_cast<std::size_t>(i)] = i;

    int guard = n * n + 8;
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const std::size_t m = idx.size();
        for (std::size_t i = 0; i < m; ++i) {
            const int iPrev = idx[(i + m - 1) % m];
            const int iCurr = idx[i];
            const int iNext = idx[(i + 1) % m];
            const Vec2& a = poly[static_cast<std::size_t>(iPrev)];
            const Vec2& b = poly[static_cast<std::size_t>(iCurr)];
            const Vec2& c = poly[static_cast<std::size_t>(iNext)];
            if (cross2DXZ(a, b, c) <= 1e-4f) continue;

            bool containsOther = false;
            for (std::size_t j = 0; j < m; ++j) {
                const int ij = idx[j];
                if (ij == iPrev || ij == iCurr || ij == iNext) continue;
                if (pointInTri2DCCW(poly[static_cast<std::size_t>(ij)], a, b, c)) {
                    containsOther = true;
                    break;
                }
            }
            if (!containsOther) {
                emitTri(a, b, c);
                idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) break;
    }

    if (idx.size() == 3) {
        emitTri(poly[static_cast<std::size_t>(idx[0])],
                poly[static_cast<std::size_t>(idx[1])],
                poly[static_cast<std::size_t>(idx[2])]);
    } else if (idx.size() > 3) {
        // Safe centroid fan fallback for any self-touching OSM geometry
        Vec2 ctr{0.0f, 0.0f};
        for (int ii : idx) ctr += poly[static_cast<std::size_t>(ii)];
        ctr = ctr / static_cast<float>(idx.size());
        for (std::size_t i = 0; i < idx.size(); ++i) {
            emitTri(ctr, poly[static_cast<std::size_t>(idx[i])],
                    poly[static_cast<std::size_t>(idx[(i + 1) % idx.size()])]);
        }
    }
}

} // namespace

void Renderer3D::buildRealSeoulChunkMesh(VulkanContext& vk, const CityMap& city,
                                         int cx, int cz, RealChunkGpu& out) {
    out.cx = cx;
    out.cz = cz;
    const RealSeoulData& real = city.realData();
    const OcMapHeader& hdr = real.header();
    if (cx < 0 || cz < 0 || cx >= static_cast<int>(hdr.chunksX) || cz >= static_cast<int>(hdr.chunksZ)) {
        return;
    }

    const OcChunkHdr& ch = real.chunks()[static_cast<std::size_t>(cz) * hdr.chunksX + static_cast<std::size_t>(cx)];
    const auto& roads = real.roads();
    const auto& rVerts = real.roadVerts();
    const auto& flatRoads = real.flatChunkRoads();
    const auto& bldgs = real.buildings();
    const auto& flatBldgs = real.flatChunkBldgs();

    std::vector<Vertex3D> groundV, hillsideV, roadsV, roadPaintV, sidewalksV, piersV;
    std::vector<Vertex3D> aptV, glassV, graniteV, brickV, stuccoV, hanokV, heritageV, roofV;
    groundV.reserve(32 * 32 * 6);
    hillsideV.reserve(512);
    roadsV.reserve(3072);
    roadPaintV.reserve(2048);
    sidewalksV.reserve(2560);
    piersV.reserve(512);
    aptV.reserve(2048);
    glassV.reserve(1536);
    graniteV.reserve(2048);
    brickV.reserve(2048);
    stuccoV.reserve(2048);
    roofV.reserve(2048);

    OcrmDecodedChunk ocrmCh{};
    const bool useOcrm = real.hasOcrmMesh() && real.ocrmMesh().decodeChunk(cx, cz, ocrmCh);
    out.usesOcrm = useOcrm;

    // Query all nearby roads around this chunk FIRST and bin their local edges into a 16x16 spatial grid
    // so BOTH the high-res 8m urban ground subbase (0% road poke-through!) AND curved road intersections
    // resolve in < 1.5ms/chunk!
    const float gx0 = hdr.minX + static_cast<float>(cx) * hdr.chunkSize;
    const float gz0 = hdr.minZ + static_cast<float>(cz) * hdr.chunkSize;
    const Vec2 chunkCenter{gx0 + hdr.chunkSize * 0.5f, gz0 + hdr.chunkSize * 0.5f};
    std::vector<std::uint32_t> chunkNearRoads;
    real.queryChunkRoads(chunkCenter, hdr.chunkSize * 0.72f + 28.0f, chunkNearRoads);

    struct LocalRoadSeg {
        Vec2 a{}, e{}, dir{};
        float invLen2 = 0.0f;
        float yA = 0.0f, yB = 0.0f;
        float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
        float ohw = 2.6f;
        std::uint32_t roadIdx = 0;
        std::uint8_t roadClass = 4;
        std::int8_t layer = 0;
        bool isBridge = false;
    };
    constexpr int kGridN = 16;
    const float gridPad = 42.0f;
    const float gridMinX = gx0 - gridPad;
    const float gridMinZ = gz0 - gridPad;
    const float gridSpan = hdr.chunkSize + gridPad * 2.0f;
    const float invCellSz = static_cast<float>(kGridN) / gridSpan;

    const bool has3DRoads = real.has3DRoads();
    std::vector<LocalRoadSeg> localSegs;
    localSegs.reserve(chunkNearRoads.size() * 6u);
    std::vector<std::uint32_t> gridCells[kGridN * kGridN];

    for (std::uint32_t rj : chunkNearRoads) {
        const OsmRoadRec& ro = roads[rj];
        if (ro.vertCount < 2 || (ro.flags & 4u) != 0u) continue;
        const float ohw = std::max(2.6f, ro.halfWidth);
        const bool isBridgeO = ((ro.flags & 2u) != 0u) || (ro.layer >= 1) || ((ro.flags2 & 4u) != 0u);
        for (std::uint16_t s = 0; s + 1 < ro.vertCount; ++s) {
            const Vec2 a = rVerts[ro.firstVert + s];
            const Vec2 b = rVerts[ro.firstVert + s + 1];
            const float sMinX = std::min(a.x, b.x);
            const float sMaxX = std::max(a.x, b.x);
            const float sMinZ = std::min(a.z, b.z);
            const float sMaxZ = std::max(a.z, b.z);
            if (sMaxX < gridMinX - ohw || sMinX > gridMinX + gridSpan + ohw ||
                sMaxZ < gridMinZ - ohw || sMinZ > gridMinZ + gridSpan + ohw) {
                continue;
            }
            const Vec2 e = b - a;
            const float l2 = e.lengthSq();
            if (l2 < 1e-4f) continue;
            const float yA = has3DRoads ? real.roadVertY(ro.firstVert + s) : real.sampleDem(a);
            const float yB = has3DRoads ? real.roadVertY(ro.firstVert + s + 1) : real.sampleDem(b);
            const std::uint32_t segId = static_cast<std::uint32_t>(localSegs.size());
            localSegs.push_back(LocalRoadSeg{
                a, e, e / std::sqrt(l2), 1.0f / l2,
                yA, yB,
                sMinX, sMaxX, sMinZ, sMaxZ,
                ohw, rj, ro.roadClass, ro.layer, isBridgeO
            });
            const int c0x = std::clamp(static_cast<int>(std::floor((sMinX - ohw - gridMinX) * invCellSz)), 0, kGridN - 1);
            const int c1x = std::clamp(static_cast<int>(std::floor((sMaxX + ohw - gridMinX) * invCellSz)), 0, kGridN - 1);
            const int c0z = std::clamp(static_cast<int>(std::floor((sMinZ - ohw - gridMinZ) * invCellSz)), 0, kGridN - 1);
            const int c1z = std::clamp(static_cast<int>(std::floor((sMaxZ + ohw - gridMinZ) * invCellSz)), 0, kGridN - 1);
            for (int gz = c0z; gz <= c1z; ++gz) {
                for (int gx = c0x; gx <= c1x; ++gx) {
                    gridCells[gz * kGridN + gx].push_back(segId);
                }
            }
        }
    }
    std::vector<std::uint32_t> segVisit(localSegs.size(), 0u);
    std::uint32_t curStamp = 0u;

    // ------------------------------------------------------------------------
    // 0. HIGH-RESOLUTION 4.0m ROAD & SIDEWALK-CONTOURED URBAN GROUND SUBBASE
    //    Uses exact 2D point-to-triangle OCRM road & sidewalk distance:
    //      - Inside road (<= 0.55m): buried -0.52m & culled -> 0% asphalt poke-through!
    //      - Under OCRM sidewalk/plaza: tucked cleanly beneath sidewalk (+0.06m max) -> 0% z-fighting!
    //      - Storefront/courtyard apron: flush at roadY + 0.05m -> 0% sunken pits/trenches!
    // ------------------------------------------------------------------------
    {
        constexpr int kGn = 64; // 256m / 64 = 4.0m precision!
        const float step = hdr.chunkSize / static_cast<float>(kGn);
        const float subBaseDrop = real.isOcm6() ? 0.12f : 0.14f;
        struct LocalGNode {
            Vec3 pos;
            Vec3 norm;
            float rawH;
            float minDistRoad;
            float nearRoadMinY;
            float nearRoadAvgY;
            float roadSumWY;
            float roadSumW;
            float swCapY;
            bool inWater;
            bool insideRoad;
            bool underSidewalk;
            bool onSidewalk;
        };
        std::vector<LocalGNode> lg(static_cast<std::size_t>((kGn + 1) * (kGn + 1)));
        for (int iz = 0; iz <= kGn; ++iz) {
            const float wz = gz0 + static_cast<float>(iz) * step;
            for (int ix = 0; ix <= kGn; ++ix) {
                const float wx = gx0 + static_cast<float>(ix) * step;
                const Vec2 wp{wx, wz};
                const float h = real.sampleDem(wp);
                const bool wtr = real.isInWater(wp);
                float y = wtr ? (h - 2.2f) : (h - subBaseDrop);
                bool insideRoad = false;

                // Legacy non-OCRM fallback road depression
                if (!useOcrm && !wtr && !localSegs.empty()) {
                    ++curStamp;
                    constexpr float kQRad = 18.5f;
                    const int q0x = std::clamp(static_cast<int>(std::floor((wx - kQRad - gridMinX) * invCellSz)), 0, kGridN - 1);
                    const int q1x = std::clamp(static_cast<int>(std::floor((wx + kQRad - gridMinX) * invCellSz)), 0, kGridN - 1);
                    const int q0z = std::clamp(static_cast<int>(std::floor((wz - kQRad - gridMinZ) * invCellSz)), 0, kGridN - 1);
                    const int q1z = std::clamp(static_cast<int>(std::floor((wz + kQRad - gridMinZ) * invCellSz)), 0, kGridN - 1);
                    float yCap = 1e9f;
                    for (int gz = q0z; gz <= q1z; ++gz) {
                        for (int gx = q0x; gx <= q1x; ++gx) {
                            for (std::uint32_t segId : gridCells[gz * kGridN + gx]) {
                                if (segVisit[segId] == curStamp) continue;
                                segVisit[segId] = curStamp;
                                const LocalRoadSeg& ls = localSegs[segId];
                                const float maxCheck = ls.ohw + 6.5f;
                                if (wx < ls.minX - maxCheck || wx > ls.maxX + maxCheck ||
                                    wz < ls.minZ - maxCheck || wz > ls.maxZ + maxCheck) {
                                    continue;
                                }
                                const float tm = clampf((wp - ls.a).dot(ls.e) * ls.invLen2, 0.0f, 1.0f);
                                const Vec2 cp = ls.a + ls.e * tm;
                                const float d2 = (wp - cp).lengthSq();
                                if (d2 > maxCheck * maxCheck) continue;
                                const float lsY = lerpf(ls.yA, ls.yB, tm);
                                if ((ls.isBridge || ls.layer >= 1) ? (lsY - h > 1.85f) : (lsY - h > 2.25f)) continue;
                                const float d = std::sqrt(d2);
                                if (d <= std::max(0.5f, ls.ohw - 0.25f)) insideRoad = true;
                                if (d <= ls.ohw + 2.5f) {
                                    yCap = std::min(yCap, lsY - 0.50f);
                                } else {
                                    const float tApron = clampf((d - (ls.ohw + 2.5f)) / 4.0f, 0.0f, 1.0f);
                                    yCap = std::min(yCap, lsY - lerpf(0.50f, 0.04f, tApron));
                                }
                            }
                        }
                    }
                    if (yCap < y) y = yCap;
                }

                const float hL = real.sampleDem({wx - 6.0f, wz});
                const float hR = real.sampleDem({wx + 6.0f, wz});
                const float hD = real.sampleDem({wx, wz - 6.0f});
                const float hU = real.sampleDem({wx, wz + 6.0f});
                lg[static_cast<std::size_t>(iz * (kGn + 1) + ix)] = {
                    {wx, y, wz}, Vec3{hL - hR, 12.0f, hD - hU}.normalized(),
                    h, 1e9f, 1e9f, h, 0.0f, 0.0f, 1e9f, wtr, insideRoad, false, false
                };
            }
        }

        // Exact 2D closest-point-on-triangle distance + interpolated Y contouring across ALL 3x3 neighbor OCRM chunks!
        // Uses exact barycentric/edge Y (eliminating +/-2m sloped-triangle bias on long Teheran-ro spans!) and smooth IDW!
        if (real.hasOcrmMesh()) {
            auto closestPtOnTriWithY = [](float px, float pz,
                                          const OcrmDecodedVertex& v0,
                                          const OcrmDecodedVertex& v1,
                                          const OcrmDecodedVertex& v2,
                                          float& outD2, float& outY) {
                const float ax = v1.px - v0.px, az = v1.pz - v0.pz;
                const float bx = v2.px - v0.px, bz = v2.pz - v0.pz;
                const float det = ax * bz - az * bx;
                if (std::abs(det) > 1e-6f) {
                    const float invDet = 1.0f / det;
                    const float rx = px - v0.px, rz = pz - v0.pz;
                    const float u = (rx * bz - rz * bx) * invDet;
                    const float v = (ax * rz - az * rx) * invDet;
                    const float w = 1.0f - u - v;
                    if (u >= -1e-4f && v >= -1e-4f && w >= -1e-4f) {
                        outD2 = 0.0f;
                        outY = w * v0.py + u * v1.py + v * v2.py;
                        return;
                    }
                }
                auto testEdge = [&](const OcrmDecodedVertex& a, const OcrmDecodedVertex& b, float& bestD2, float& bestY) {
                    const float ex = b.px - a.px, ez = b.pz - a.pz;
                    const float l2 = ex * ex + ez * ez;
                    const float t = (l2 > 1e-8f) ? clampf(((px - a.px) * ex + (pz - a.pz) * ez) / l2, 0.0f, 1.0f) : 0.0f;
                    const float dx = px - (a.px + ex * t);
                    const float dz = pz - (a.pz + ez * t);
                    const float d2 = dx * dx + dz * dz;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        bestY = lerpf(a.py, b.py, t);
                    }
                };
                outD2 = 1e30f;
                outY = v0.py;
                testEdge(v0, v1, outD2, outY);
                testEdge(v1, v2, outD2, outY);
                testEdge(v2, v0, outD2, outY);
            };

            const float gx1 = gx0 + hdr.chunkSize;
            const float gz1 = gz0 + hdr.chunkSize;

            const auto scanOcrmChunk = [&](const OcrmDecodedChunk& srcCh) {
                const auto scanRoadList = [&](const std::vector<std::uint16_t>& idxList) {
                    constexpr float kPadM = 20.0f;
                    constexpr float kPadSq = kPadM * kPadM;
                    for (std::size_t k = 0; k + 2 < idxList.size(); k += 3) {
                        const OcrmDecodedVertex& v0 = srcCh.verts[idxList[k]];
                        const OcrmDecodedVertex& v1 = srcCh.verts[idxList[k + 1]];
                        const OcrmDecodedVertex& v2 = srcCh.verts[idxList[k + 2]];

                        const float vMinX = std::min({v0.px, v1.px, v2.px});
                        const float vMaxX = std::max({v0.px, v1.px, v2.px});
                        const float vMinZ = std::min({v0.pz, v1.pz, v2.pz});
                        const float vMaxZ = std::max({v0.pz, v1.pz, v2.pz});
                        const float tx0 = vMinX - kPadM;
                        const float tx1 = vMaxX + kPadM;
                        const float tz0 = vMinZ - kPadM;
                        const float tz1 = vMaxZ + kPadM;
                        if (tx1 < gx0 || tx0 > gx1 || tz1 < gz0 || tz0 > gz1) continue;

                        const float cxT = (v0.px + v1.px + v2.px) * (1.0f / 3.0f);
                        const float czT = (v0.pz + v1.pz + v2.pz) * (1.0f / 3.0f);
                        const float avgYT = (v0.py + v1.py + v2.py) * (1.0f / 3.0f);
                        // Ground roads on urban Seoul hills/embankments can reach +2.8m above raw DEM;
                        // only skip true elevated bridges/viaducts (>= +3.40m above DEM)!
                        if (avgYT - real.sampleDem({cxT, czT}) > 3.40f) continue;

                        const int i0x = std::clamp(static_cast<int>(std::floor((tx0 - gx0) / step)), 0, kGn);
                        const int i1x = std::clamp(static_cast<int>(std::ceil((tx1 - gx0) / step)), 0, kGn);
                        const int i0z = std::clamp(static_cast<int>(std::floor((tz0 - gz0) / step)), 0, kGn);
                        const int i1z = std::clamp(static_cast<int>(std::ceil((tz1 - gz0) / step)), 0, kGn);
                        for (int iz = i0z; iz <= i1z; ++iz) {
                            for (int ix = i0x; ix <= i1x; ++ix) {
                                LocalGNode& nd = lg[static_cast<std::size_t>(iz * (kGn + 1) + ix)];
                                if (nd.inWater) continue;
                                const float dxB = std::max({vMinX - nd.pos.x, 0.0f, nd.pos.x - vMaxX});
                                const float dzB = std::max({vMinZ - nd.pos.z, 0.0f, nd.pos.z - vMaxZ});
                                if (dxB * dxB + dzB * dzB > kPadSq) continue;

                                float d2 = 0.0f, exactY = 0.0f;
                                closestPtOnTriWithY(nd.pos.x, nd.pos.z, v0, v1, v2, d2, exactY);
                                if (d2 > kPadSq) continue;
                                const float d = std::sqrt(d2);
                                const float invD = 1.0f / (d + 1.5f);
                                const float w = invD * invD * invD;
                                nd.roadSumWY += w * exactY;
                                nd.roadSumW += w;
                                if (d <= 3.6f && exactY < nd.nearRoadMinY) {
                                    nd.nearRoadMinY = exactY;
                                }
                                if (d < nd.minDistRoad) {
                                    nd.minDistRoad = d;
                                    nd.nearRoadAvgY = exactY;
                                    if (nd.nearRoadMinY > 1e8f) nd.nearRoadMinY = exactY;
                                }
                            }
                        }
                    }
                };
                scanRoadList(srcCh.idxUrban);
                scanRoadList(srcCh.idxHighway);
                scanRoadList(srcCh.idxInter);

                // Exact interpolated sidewalk/plaza height cap within 3.6m (> 2.83m 4m-quad circumradius!) so
                // even steep 35-deg hillside quads (Sillim/Namsan/Itaewon) NEVER bridge or poke through sidewalks!
                constexpr float kSwPad = 3.6f;
                constexpr float kSwPadSq = kSwPad * kSwPad;
                for (std::size_t k = 0; k + 2 < srcCh.idxSidewalk.size(); k += 3) {
                    const OcrmDecodedVertex& v0 = srcCh.verts[srcCh.idxSidewalk[k]];
                    const OcrmDecodedVertex& v1 = srcCh.verts[srcCh.idxSidewalk[k + 1]];
                    const OcrmDecodedVertex& v2 = srcCh.verts[srcCh.idxSidewalk[k + 2]];
                    if (std::abs(v0.ny) < 0.5f && std::abs(v1.ny) < 0.5f && std::abs(v2.ny) < 0.5f) continue; // Skip vertical curbs
                    const float vMinX = std::min({v0.px, v1.px, v2.px});
                    const float vMaxX = std::max({v0.px, v1.px, v2.px});
                    const float vMinZ = std::min({v0.pz, v1.pz, v2.pz});
                    const float vMaxZ = std::max({v0.pz, v1.pz, v2.pz});
                    const float tx0 = vMinX - kSwPad;
                    const float tx1 = vMaxX + kSwPad;
                    const float tz0 = vMinZ - kSwPad;
                    const float tz1 = vMaxZ + kSwPad;
                    if (tx1 < gx0 || tx0 > gx1 || tz1 < gz0 || tz0 > gz1) continue;
                    const float cxT = (v0.px + v1.px + v2.px) * (1.0f / 3.0f);
                    const float czT = (v0.pz + v1.pz + v2.pz) * (1.0f / 3.0f);
                    const float avgYT = (v0.py + v1.py + v2.py) * (1.0f / 3.0f);
                    if (avgYT - real.sampleDem({cxT, czT}) > 3.40f) continue; // Skip elevated bridge sidewalks

                    const int i0x = std::clamp(static_cast<int>(std::floor((tx0 - gx0) / step)), 0, kGn);
                    const int i1x = std::clamp(static_cast<int>(std::ceil((tx1 - gx0) / step)), 0, kGn);
                    const int i0z = std::clamp(static_cast<int>(std::floor((tz0 - gz0) / step)), 0, kGn);
                    const int i1z = std::clamp(static_cast<int>(std::ceil((tz1 - gz0) / step)), 0, kGn);
                    for (int iz = i0z; iz <= i1z; ++iz) {
                        for (int ix = i0x; ix <= i1x; ++ix) {
                            LocalGNode& nd = lg[static_cast<std::size_t>(iz * (kGn + 1) + ix)];
                            if (nd.inWater) continue;
                            const float dxB = std::max({vMinX - nd.pos.x, 0.0f, nd.pos.x - vMaxX});
                            const float dzB = std::max({vMinZ - nd.pos.z, 0.0f, nd.pos.z - vMaxZ});
                            if (dxB * dxB + dzB * dzB > kSwPadSq) continue;
                            float d2 = 0.0f, exactSwY = 0.0f;
                            closestPtOnTriWithY(nd.pos.x, nd.pos.z, v0, v1, v2, d2, exactSwY);
                            if (d2 <= kSwPadSq) {
                                nd.underSidewalk = true;
                                if (d2 <= 0.65f * 0.65f) nd.onSidewalk = true;
                                nd.swCapY = std::min(nd.swCapY, exactSwY - 0.10f);
                            }
                        }
                    }
                }
            };

            if (useOcrm) scanOcrmChunk(ocrmCh);
            OcrmDecodedChunk nbCh{};
            for (int dcz = -1; dcz <= 1; ++dcz) {
                for (int dcx = -1; dcx <= 1; ++dcx) {
                    if (dcx == 0 && dcz == 0) continue;
                    if (real.ocrmMesh().decodeChunk(cx + dcx, cz + dcz, nbCh)) {
                        scanOcrmChunk(nbCh);
                    }
                }
            }

            // Apply silk-smooth, watertight vertical contouring using exact interpolated Y & IDW blending!
            // On flat urban blocks, hold storefront apron flush out to 9.5m; on steep Seoul hills (|rawH - roadY| > 1m),
            // transition gracefully back to natural hillside slope right past the sidewalk (2.2m) so roads NEVER carve
            // jagged 10m vertical concrete cliff pits beneath uphill buildings!
            for (LocalGNode& nd : lg) {
                if (nd.inWater) continue;
                if (nd.minDistRoad <= 20.0f) {
                    const float smoothRdY = (nd.roadSumW > 1e-6f) ? (nd.roadSumWY / nd.roadSumW) : nd.nearRoadAvgY;
                    const float rMin = (nd.nearRoadMinY < 1e8f) ? nd.nearRoadMinY : smoothRdY;
                    const float hillBlend = clampf((std::abs(nd.rawH - smoothRdY) - 0.8f) / 2.2f, 0.0f, 1.0f);
                    const float flatShelfM = lerpf(9.5f, 2.2f, hillBlend);
                    const float transSpanM = lerpf(10.5f, 15.0f, hillBlend);
                    if (nd.minDistRoad <= 0.75f) {
                        nd.pos.y = std::min(nd.pos.y, rMin - 0.54f);
                        nd.insideRoad = true;
                    } else if (nd.onSidewalk) {
                        const float tSw = clampf((nd.minDistRoad - 0.75f) / 3.5f, 0.0f, 1.0f);
                        const float targetSw = std::min(nd.swCapY, smoothRdY + 0.115f);
                        nd.pos.y = std::min(nd.swCapY, lerpf(rMin - 0.34f, targetSw, tSw));
                    } else if (nd.minDistRoad <= flatShelfM) {
                        const float tLot = clampf((nd.minDistRoad - 0.75f) / std::max(1.2f, flatShelfM - 0.75f), 0.0f, 1.0f);
                        nd.pos.y = lerpf(rMin - 0.24f, smoothRdY + 0.115f, tLot);
                    } else {
                        const float tOut = clampf((nd.minDistRoad - flatShelfM) / transSpanM, 0.0f, 1.0f);
                        const float sOut = tOut * tOut * (3.0f - 2.0f * tOut);
                        nd.pos.y = lerpf(smoothRdY + 0.115f, nd.rawH - subBaseDrop, sOut);
                    }
                }
                if (nd.underSidewalk && nd.pos.y > nd.swCapY) {
                    nd.pos.y = nd.swCapY;
                }
            }

            // 2 passes of constrained interior Laplacian smoothing (0.00cm chunk seam drift, zero micro-steps!)
            std::vector<float> tmpY(lg.size());
            for (int pass = 0; pass < 2; ++pass) {
                for (std::size_t i = 0; i < lg.size(); ++i) tmpY[i] = lg[i].pos.y;
                for (int iz = 1; iz < kGn; ++iz) {
                    for (int ix = 1; ix < kGn; ++ix) {
                        const std::size_t idx = static_cast<std::size_t>(iz * (kGn + 1) + ix);
                        LocalGNode& nd = lg[idx];
                        if (nd.inWater || nd.insideRoad) continue;
                        const float avgNb = 0.25f * (
                            tmpY[idx - 1] + tmpY[idx + 1] +
                            tmpY[idx - static_cast<std::size_t>(kGn + 1)] +
                            tmpY[idx + static_cast<std::size_t>(kGn + 1)]);
                        float ny = lerpf(tmpY[idx], avgNb, 0.45f);
                        if (nd.underSidewalk) ny = std::min(ny, nd.swCapY);
                        if (nd.minDistRoad <= 1.25f) {
                            const float rMin = (nd.nearRoadMinY < 1e8f) ? nd.nearRoadMinY : nd.nearRoadAvgY;
                            ny = std::min(ny, rMin - 0.36f);
                        }
                        nd.pos.y = ny;
                    }
                }
            }
        }

        const ColorRGBA kGroundCol{162, 164, 168, 255};
        const ColorRGBA kSubRoadCol{36, 38, 42, 255};
        auto pushGVert = [&](std::vector<Vertex3D>& dst, const LocalGNode& n, ColorRGBA col) {
            Vertex3D v{};
            v.px = n.pos.x; v.py = n.pos.y; v.pz = n.pos.z;
            v.nx = n.norm.x; v.ny = n.norm.y; v.nz = n.norm.z;
            v.u = 0.0f; v.v = 0.0f;
            v.r = static_cast<float>(col.r) / 255.0f;
            v.g = static_cast<float>(col.g) / 255.0f;
            v.b = static_cast<float>(col.b) / 255.0f;
            v.a = 1.0f;
            dst.push_back(v);
        };
        for (int iz = 0; iz < kGn; ++iz) {
            for (int ix = 0; ix < kGn; ++ix) {
                const LocalGNode& n00 = lg[static_cast<std::size_t>(iz * (kGn + 1) + ix)];
                const LocalGNode& n10 = lg[static_cast<std::size_t>(iz * (kGn + 1) + (ix + 1))];
                const LocalGNode& n11 = lg[static_cast<std::size_t>((iz + 1) * (kGn + 1) + (ix + 1))];
                const LocalGNode& n01 = lg[static_cast<std::size_t>((iz + 1) * (kGn + 1) + ix)];
                if (n00.inWater && n10.inWater && n11.inWater && n01.inWater) continue;

                // Keep depressed road-subbase quads watertight underneath opaque asphalt (dark charcoal) so
                // twin-carriageway medians, slip-ramps & micro-gaps NEVER open holes into the blue sky/void!
                if (n00.insideRoad && n10.insideRoad && n11.insideRoad && n01.insideRoad) {
                    pushGVert(groundV, n00, kSubRoadCol); pushGVert(groundV, n01, kSubRoadCol); pushGVert(groundV, n11, kSubRoadCol);
                    pushGVert(groundV, n00, kSubRoadCol); pushGVert(groundV, n11, kSubRoadCol); pushGVert(groundV, n10, kSubRoadCol);
                    continue;
                }

                // Render ALL active-chunk elevations (0..836m) at full 4m OCRM-carved precision so there is
                // ZERO 88m cliff tear, ZERO mountain mesh piercing hillside roads, and ZERO green grass in flat storefront plazas!
                const float avgH = 0.25f * (n00.rawH + n10.rawH + n11.rawH + n01.rawH);
                const float avgNY = 0.25f * (n00.norm.y + n10.norm.y + n11.norm.y + n01.norm.y);
                const float minRoadD = std::min({n00.minDistRoad, n10.minDistRoad, n11.minDistRoad, n01.minDistRoad});
                const bool anyOnSw = n00.onSidewalk || n10.onSidewalk || n11.onSidewalk || n01.onSidewalk;
                const bool anyUnderSw = n00.underSidewalk || n10.underSidewalk || n11.underSidewalk || n01.underSidewalk;
                const float qRise = std::max({n00.pos.y, n10.pos.y, n11.pos.y, n01.pos.y})
                                  - std::min({n00.pos.y, n10.pos.y, n11.pos.y, n01.pos.y});
                if ((minRoadD > 2.4f && qRise > 1.65f) ||
                    (!anyOnSw && !anyUnderSw &&
                     ((minRoadD > 3.5f && (qRise > 1.25f || avgNY < 0.945f)) ||
                      (minRoadD > 16.0f && (avgNY < 0.968f || avgH > 54.0f))))) {
                    const float hT = clampf((avgH - 24.0f) / 140.0f, 0.0f, 1.0f);
                    const ColorRGBA hillCol{
                        static_cast<std::uint8_t>(lerpf(86.0f, 48.0f, hT)),
                        static_cast<std::uint8_t>(lerpf(126.0f, 82.0f, hT)),
                        static_cast<std::uint8_t>(lerpf(64.0f, 46.0f, hT)),
                        255u
                    };
                    pushGVert(hillsideV, n00, hillCol); pushGVert(hillsideV, n01, hillCol); pushGVert(hillsideV, n11, hillCol);
                    pushGVert(hillsideV, n00, hillCol); pushGVert(hillsideV, n11, hillCol); pushGVert(hillsideV, n10, hillCol);
                } else {
                    pushGVert(groundV, n00, kGroundCol); pushGVert(groundV, n01, kGroundCol); pushGVert(groundV, n11, kGroundCol);
                    pushGVert(groundV, n00, kGroundCol); pushGVert(groundV, n11, kGroundCol); pushGVert(groundV, n10, kGroundCol);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // 1. REAL SEOUL ROADS, JUNCTIONS, SIDEWALKS & BRIDGE STRUCTURES!
    //    v6.0.0: Prefer pre-baked OCRM v1 single-topology polygon mesh (SUMO / Korean HD Map)
    //            for 0.00% overlapping ribbons & 0.00% intersection sidewalk intrusion!
    // ------------------------------------------------------------------------
    if (useOcrm) {
        auto emitOcrmSubmesh = [&](const std::vector<std::uint16_t>& idxList,
                                   std::vector<Vertex3D>& dst,
                                   bool isSidewalk) {
            if (idxList.empty()) return;
            dst.reserve(dst.size() + idxList.size());
            if (!isSidewalk) {
                for (std::uint16_t vi : idxList) {
                    const OcrmDecodedVertex& dv = ocrmCh.verts[vi];
                    Vertex3D v{};
                    v.px = dv.px; v.py = dv.py; v.pz = dv.pz;
                    v.nx = dv.nx; v.ny = dv.ny; v.nz = dv.nz;
                    v.u  = 0.0f;  v.v  = 0.0f;
                    v.r  = static_cast<float>(dv.r) / 255.0f;
                    v.g  = static_cast<float>(dv.g) / 255.0f;
                    v.b  = static_cast<float>(dv.b) / 255.0f;
                    v.a  = static_cast<float>(dv.a) / 255.0f;
                    dst.push_back(v);
                }
                return;
            }
            for (std::size_t k = 0; k + 2 < idxList.size(); k += 3) {
                const OcrmDecodedVertex& d0 = ocrmCh.verts[idxList[k]];
                const OcrmDecodedVertex& d1 = ocrmCh.verts[idxList[k + 1]];
                const OcrmDecodedVertex& d2 = ocrmCh.verts[idxList[k + 2]];
                const bool isVertCurb = (std::abs(d0.ny) < 0.5f && std::abs(d1.ny) < 0.5f && std::abs(d2.ny) < 0.5f)
                                     || (d0.uCm < 0.0f && d1.uCm < 0.0f && d2.uCm < 0.0f);
                bool steepOuterHill = false;
                if (!isVertCurb) {
                    // Only convert outer setback-plaza triangles strictly beyond the 3.0m sidewalk/Braille strip
                    // AND truly steeper than 24 deg (absNy < 0.91f), preserving all sloped city sidewalks intact!
                    const float minU = std::min({d0.uCm, d1.uCm, d2.uCm});
                    if (minU >= 300.0f) {
                        const Vec3 e1{d1.px - d0.px, d1.py - d0.py, d1.pz - d0.pz};
                        const Vec3 e2{d2.px - d0.px, d2.py - d0.py, d2.pz - d0.pz};
                        const Vec3 fn = e1.cross(e2);
                        const float fnLen = fn.length();
                        const float absNy = (fnLen > 1e-5f) ? (std::abs(fn.y) / fnLen) : 1.0f;
                        if (absNy < 0.91f) {
                            steepOuterHill = true;
                        }
                    }
                }
                if (steepOuterHill) {
                    const float avgH = (d0.py + d1.py + d2.py) * (1.0f / 3.0f);
                    const float hT = clampf((avgH - 24.0f) / 140.0f, 0.0f, 1.0f);
                    const float hr = lerpf(86.0f, 48.0f, hT) / 255.0f;
                    const float hg = lerpf(126.0f, 82.0f, hT) / 255.0f;
                    const float hb = lerpf(64.0f, 46.0f, hT) / 255.0f;
                    for (int t = 0; t < 3; ++t) {
                        const OcrmDecodedVertex& dv = ocrmCh.verts[idxList[k + static_cast<std::size_t>(t)]];
                        Vertex3D v{};
                        v.px = dv.px; v.py = dv.py; v.pz = dv.pz;
                        v.nx = dv.nx; v.ny = dv.ny; v.nz = dv.nz;
                        v.u = 0.0f; v.v = 0.0f;
                        v.r = hr; v.g = hg; v.b = hb; v.a = 1.0f;
                        hillsideV.push_back(v);
                    }
                } else {
                    for (int t = 0; t < 3; ++t) {
                        const OcrmDecodedVertex& dv = ocrmCh.verts[idxList[k + static_cast<std::size_t>(t)]];
                        Vertex3D v{};
                        v.px = dv.px; v.py = dv.py; v.pz = dv.pz;
                        v.nx = dv.nx; v.ny = dv.ny; v.nz = dv.nz;
                        v.u  = dv.uCm * 0.01f;
                        v.v  = -1.0f; // v < -0.5 selects OCRM world-space sidewalk shader!
                        v.r  = static_cast<float>(dv.r) / 255.0f;
                        v.g  = static_cast<float>(dv.g) / 255.0f;
                        v.b  = static_cast<float>(dv.b) / 255.0f;
                        v.a  = static_cast<float>(dv.a) / 255.0f;
                        dst.push_back(v);
                    }
                }
            }
        };
        emitOcrmSubmesh(ocrmCh.idxUrban,    roadsV,     false);
        emitOcrmSubmesh(ocrmCh.idxHighway,  roadsV,     false);
        emitOcrmSubmesh(ocrmCh.idxInter,    roadsV,     false);
        emitOcrmSubmesh(ocrmCh.idxMarking,  roadPaintV, false);
        emitOcrmSubmesh(ocrmCh.idxSidewalk, sidewalksV, true);
        emitOcrmSubmesh(ocrmCh.idxBridge,   piersV,     false);
    }
    if (!useOcrm) {
    const ColorRGBA kSwTint{196, 198, 202, 255};
    const ColorRGBA kPierCol{168, 172, 178, 255};

    struct RoadSubSeg {
        Vec2 p0{}, p1{}, mid{};
        Vec2 n0{}, n1{}, segDir{}, segPerp{};
        float y0 = 0.0f, y1 = 0.0f;
        float dist0 = 0.0f, dist1 = 0.0f;
        float hwL = 2.6f, hwR = 2.6f;
        float oppGapL = 1e9f, oppOhwL = 0.0f;
        float oppGapR = 1e9f, oppOhwR = 0.0f;
        std::uint32_t oppRoadIdxL = 0xFFFFFFFFu;
        std::uint32_t oppRoadIdxR = 0xFFFFFFFFu;
        bool inChunk = false;
        bool inInter = false;
        bool crossedMajor = false;
        bool suppressLeft = false;
        bool suppressRight = false;
        bool isMedianEdgeL = false;
        bool isMedianEdgeR = false;
        bool isInterCrosswalk = false;
    };
    std::vector<RoadSubSeg> subs;
    std::vector<float> smoothTmpL, smoothTmpR;

    for (std::uint32_t k = 0; k < ch.roadLen; ++k) {
        const std::uint32_t ri = flatRoads[ch.roadStart + k];
        const OsmRoadRec& r = roads[ri];
        if (r.vertCount < 2 || (r.flags & 4u) != 0u) continue;

        const bool isExpressway = (r.roadClass <= 1u) || ((r.flags2 & 2u) != 0u);
        const bool isLink = (r.flags2 & 1u) != 0u;
        const bool isBridge = ((r.flags & 2u) != 0u) || (r.layer >= 1) || ((r.flags2 & 4u) != 0u);
        const float hw = std::max(2.6f, r.halfWidth);
        const RoadLaneLayout laneLayout = RealSeoulData::computeRoadLaneLayout(r);

        // Exact road-class lift + deterministic per-road micro-tiebreaker to eliminate
        // 100% of same-class intersection Z-fighting speckles without affecting tire physics!
        // On OCM6 v4, roadVertY is already globally C2-smoothed & transversely flush-coupled with
        // the road-carved 15m DTM, so ONLY a tiny <18mm render-only Z-bias is applied (ZERO step!).
        const bool isOcm6 = real.isOcm6();
        const float slipBias = (hw < 3.2f) ? (isOcm6 ? -0.002f : -0.005f) : 0.0f;
        const float roadTieBias = static_cast<float>(ri % 19u) * (isOcm6 ? 0.0003f : 0.0008f);
        const float classLift = isOcm6
            ? (static_cast<float>(r.roadClass < 6u ? (6u - r.roadClass) : 0u) * 0.0020f + slipBias + roadTieBias)
            : (RealSeoulData::roadClassLift(r.roadClass) + slipBias + roadTieBias);

        // Smooth C0-continuous corner elevation:
        // - OCM6 v4: razor-flush planar cross-section directly on globally C2-smoothed 3D centerline!
        //   (Eliminates all intersection step cracks & cross-slope mismatches between meeting boulevards!)
        // - Legacy OCM5/OCM4: elevated decks stay flat; ground roads hug coarse DEM cross-slope.
        auto evalCornerY = [&](const Vec2& pCenter, const Vec2& pCorner, float yBase) -> float {
            if (isOcm6) {
                return yBase + classLift;
            }
            if (!has3DRoads) {
                float y = real.sampleDem(pCorner) + classLift;
                if (isBridge) {
                    y = std::max(y, city.bridgeDeckElevationAt(pCorner) + 0.02f + roadTieBias * 0.5f);
                }
                return y;
            }
            const float demC = real.sampleDem(pCenter);
            const float clearH = yBase - demC;
            if (clearH >= 0.85f || isBridge) {
                return yBase + classLift;
            }
            const float crossBlend = clampf(1.0f - (clearH - 0.15f) / 0.70f, 0.0f, 1.0f);
            const float demCorner = real.sampleDem(pCorner);
            const float crossDelta = clampf(demCorner - demC, -0.35f, 0.35f) * crossBlend;
            return yBase + crossDelta + classLift;
        };

        auto hasSameLevelRoadAt = [&](const Vec2& pt, float refY, float margin) -> bool {
            if (has3DRoads) {
                const RoadSurfaceHit sh = real.findRoadSurfaceAt(pt, refY + 0.55f, margin);
                return sh.found && std::abs(sh.y - refY) < 2.4f;
            }
            return real.isOnRoad(pt, margin);
        };

        // Precompute smooth mitered perpendicular offsets at each polyline vertex
        std::vector<Vec2> miters(r.vertCount);
        for (std::uint16_t i = 0; i < r.vertCount; ++i) {
            Vec2 dir{1.0f, 0.0f};
            if (i == 0) {
                dir = (rVerts[r.firstVert + 1] - rVerts[r.firstVert]).normalized();
            } else if (i + 1 == r.vertCount) {
                dir = (rVerts[r.firstVert + i] - rVerts[r.firstVert + i - 1]).normalized();
            } else {
                const Vec2 dPrev = (rVerts[r.firstVert + i] - rVerts[r.firstVert + i - 1]).normalized();
                const Vec2 dNext = (rVerts[r.firstVert + i + 1] - rVerts[r.firstVert + i]).normalized();
                Vec2 avg = dPrev + dNext;
                if (avg.lengthSq() < 1e-4f) avg = dNext;
                dir = avg.normalized();
                const Vec2 nSeg{-dNext.z, dNext.x};
                const Vec2 nMit{-dir.z, dir.x};
                const float denom = std::max(0.68f, std::abs(nMit.dot(nSeg)));
                miters[i] = nMit * std::min(1.42f, 1.0f / denom);
                continue;
            }
            miters[i] = Vec2{-dir.z, dir.x};
        }

        // Pass 1: Build <=4.8m 3D sub-segments near this chunk & classify same-level intersections / medians / merges!
        subs.clear();
        float distM = 0.0f;
        const float activeRadius = hdr.chunkSize * 0.55f + 20.0f;
        for (std::uint16_t i = 0; i + 1 < r.vertCount; ++i) {
            const Vec2 edgeA = rVerts[r.firstVert + i];
            const Vec2 edgeB = rVerts[r.firstVert + i + 1];
            const float yEdgeA = has3DRoads ? real.roadVertY(r.firstVert + i) : real.sampleDem(edgeA);
            const float yEdgeB = has3DRoads ? real.roadVertY(r.firstVert + i + 1) : real.sampleDem(edgeB);
            const Vec2 edgeVec = edgeB - edgeA;
            const float edgeLen = edgeVec.length();
            if (edgeLen < 0.25f) continue;

            const Vec2 segDir = edgeVec / edgeLen;
            const Vec2 segPerp{-segDir.z, segDir.x};
            const int subSteps = std::max(1, static_cast<int>(std::ceil(edgeLen / 4.8f)));
            const float subLen = edgeLen / static_cast<float>(subSteps);

            for (int sub = 0; sub < subSteps; ++sub) {
                const float t0 = static_cast<float>(sub) / static_cast<float>(subSteps);
                const float t1 = static_cast<float>(sub + 1) / static_cast<float>(subSteps);
                const Vec2 p0 = edgeA + edgeVec * t0;
                const Vec2 p1 = edgeA + edgeVec * t1;
                const float y0Base = lerpf(yEdgeA, yEdgeB, t0);
                const float y1Base = lerpf(yEdgeA, yEdgeB, t1);
                const float yMidBase = 0.5f * (y0Base + y1Base);
                const float dist0 = distM;
                distM += subLen;
                const Vec2 mid = (p0 + p1) * 0.5f;

                if (std::abs(mid.x - chunkCenter.x) > activeRadius ||
                    std::abs(mid.z - chunkCenter.z) > activeRadius) {
                    continue;
                }

                const int mcx = std::clamp(static_cast<int>(std::floor((mid.x - hdr.minX) / hdr.chunkSize)),
                                           0, static_cast<int>(hdr.chunksX) - 1);
                const int mcz = std::clamp(static_cast<int>(std::floor((mid.z - hdr.minZ) / hdr.chunkSize)),
                                           0, static_cast<int>(hdr.chunksZ) - 1);

                RoadSubSeg ss{};
                ss.p0 = p0;
                ss.p1 = p1;
                ss.mid = mid;
                ss.n0 = (sub == 0) ? miters[i] : segPerp;
                ss.n1 = (sub + 1 == subSteps) ? miters[i + 1] : segPerp;
                ss.segDir = segDir;
                ss.segPerp = segPerp;
                ss.y0 = y0Base;
                ss.y1 = y1Base;
                ss.dist0 = dist0;
                ss.dist1 = distM;
                ss.hwL = hw;
                ss.hwR = hw;
                ss.inChunk = (mcx == cx && mcz == cz);

                // Query only the local grid cells touching this sub-segment (O(1) spatial lookup!)
                ++curStamp;
                const float qRad = hw + 10.5f;
                const int q0x = std::clamp(static_cast<int>(std::floor((mid.x - qRad - gridMinX) * invCellSz)), 0, kGridN - 1);
                const int q1x = std::clamp(static_cast<int>(std::floor((mid.x + qRad - gridMinX) * invCellSz)), 0, kGridN - 1);
                const int q0z = std::clamp(static_cast<int>(std::floor((mid.z - qRad - gridMinZ) * invCellSz)), 0, kGridN - 1);
                const int q1z = std::clamp(static_cast<int>(std::floor((mid.z + qRad - gridMinZ) * invCellSz)), 0, kGridN - 1);

                float bestOppGapL = 1e9f, bestOppOhwL = 0.0f;
                float bestOppGapR = 1e9f, bestOppOhwR = 0.0f;
                std::uint32_t bestOppIdxL = 0xFFFFFFFFu;
                std::uint32_t bestOppIdxR = 0xFFFFFFFFu;

                for (int gz = q0z; gz <= q1z; ++gz) {
                    for (int gx = q0x; gx <= q1x; ++gx) {
                        for (std::uint32_t segId : gridCells[gz * kGridN + gx]) {
                            if (segVisit[segId] == curStamp) continue;
                            segVisit[segId] = curStamp;

                            const LocalRoadSeg& ls = localSegs[segId];
                            if (ls.roadIdx == ri) continue;
                            if (!has3DRoads && ls.isBridge != isBridge) continue;
                            const float ohw = ls.ohw;
                            const float maxD = hw + ohw + 9.5f;
                            if (mid.x < ls.minX - maxD || mid.x > ls.maxX + maxD ||
                                mid.z < ls.minZ - maxD || mid.z > ls.maxZ + maxD) {
                                continue;
                            }

                            const float tm = clampf((mid - ls.a).dot(ls.e) * ls.invLen2, 0.0f, 1.0f);
                            if (has3DRoads) {
                                // Strictly same-level interaction: grade-separated overpasses (|dY| > 2.4m)
                                // NEVER punch fake intersection boxes or suppress lane markings on each other!
                                const float lsY = lerpf(ls.yA, ls.yB, tm);
                                if (std::abs(lsY - yMidBase) > 2.4f) continue;
                            }
                            const Vec2 oClosest = ls.a + ls.e * tm;
                            const float dMid2 = (mid - oClosest).lengthSq();
                            if (dMid2 > maxD * maxD) continue;
                            const float dMid = std::sqrt(dMid2);

                            const float dirDot = segDir.dot(ls.dir);
                            const float absDot = std::abs(dirDot);

                            // A) Narrow 1-lane slip / U-turn link touching any wider avenue -> clean dark asphalt!
                            if (hw < 3.2f && ohw >= 4.5f && dMid < ohw + 2.5f) {
                                ss.inInter = true;
                                continue;
                            }

                            // B) Crossing same-level intersection road (angle > ~40 deg) -> clean intersection box!
                            if (absDot < 0.76f) {
                                const bool otherSubstantial = (hw >= 6.0f)
                                    ? ((ls.roadClass <= 3u && ohw >= 4.2f) || (ls.roadClass == 4u && ohw >= 3.8f))
                                    : ((ls.roadClass <= 4u && ohw >= 3.2f) || (r.roadClass >= 4u));
                                if (otherSubstantial) {
                                    const float tp0 = clampf((p0 - ls.a).dot(ls.e) * ls.invLen2, 0.0f, 1.0f);
                                    const float tp1 = clampf((p1 - ls.a).dot(ls.e) * ls.invLen2, 0.0f, 1.0f);
                                    const float dP0_2 = (p0 - (ls.a + ls.e * tp0)).lengthSq();
                                    const float dP1_2 = (p1 - (ls.a + ls.e * tp1)).lengthSq();
                                    const float endLim = ohw + 0.7f;
                                    const float hitMargin = isExpressway ? 0.5f : 2.1f;

                                    if (dMid < ohw + hitMargin ||
                                        dP0_2 < endLim * endLim ||
                                        dP1_2 < endLim * endLim) {
                                        ss.inInter = true;
                                        if (ls.roadClass <= 3u && ohw >= 4.8f) {
                                            ss.crossedMajor = true;
                                        }
                                    }
                                } else if (!isExpressway && ls.roadClass <= 4u && ohw >= 2.8f && dMid < hw + ohw + 1.2f) {
                                    // Smaller side-street T-junction touching boulevard outer curb: break only the outer edge line!
                                    const float sideLat = (oClosest - mid).dot(segPerp);
                                    if (sideLat < -hw * 0.4f) ss.suppressLeft = true;
                                    else if (sideLat > hw * 0.4f) ss.suppressRight = true;
                                }
                                continue;
                            }

                            // C) Opposite-direction parallel twin carriageway (Teheran-ro / Gyeongbu / Naebu Circular):
                            //    Track closest opposing carriageway on each side for flush zero-overlap, zero-gap median!
                            if (dirDot < -0.82f && std::abs(static_cast<int>(r.roadClass) - static_cast<int>(ls.roadClass)) <= 1) {
                                const float sideLat = (oClosest - mid).dot(segPerp);
                                if (sideLat < -2.0f) {
                                    const float centerGap = -sideLat;
                                    if (centerGap < bestOppGapL) {
                                        bestOppGapL = centerGap;
                                        bestOppOhwL = ohw;
                                        bestOppIdxL = ls.roadIdx;
                                    }
                                } else if (sideLat > 2.0f) {
                                    const float centerGap = sideLat;
                                    if (centerGap < bestOppGapR) {
                                        bestOppGapR = centerGap;
                                        bestOppOhwR = ohw;
                                        bestOppIdxR = ls.roadIdx;
                                    }
                                }
                                continue;
                            }

                            // D) Same-direction merging / parallel road (IC ramps, turn pockets, slip lanes):
                            //    Suppress touching edge line & parapet so ramp merges flow seamlessly!
                            if (dirDot > 0.78f && dMid < hw + ohw + 0.55f) {
                                const float sideLat = (oClosest - mid).dot(segPerp);
                                if (sideLat < -0.4f) ss.suppressLeft = true;
                                else if (sideLat > 0.4f) ss.suppressRight = true;
                                if (hw < 3.6f && ohw >= 5.0f && dMid < ohw + 1.4f) {
                                    ss.inInter = true;
                                }
                            }
                        }
                    }
                }

                ss.oppGapL = bestOppGapL;
                ss.oppOhwL = bestOppOhwL;
                ss.oppGapR = bestOppGapR;
                ss.oppOhwR = bestOppOhwR;

                // Apply exact flush contact along opposite-direction twin medians (eliminates BOTH ghost overlap
                // AND tiny 15-80cm grey concrete slits between parallel Seoul carriageways!).
                if (bestOppOhwL > 0.0f && bestOppGapL < hw + bestOppOhwL + 1.85f) {
                    const float exactL = bestOppGapL * (hw / (hw + bestOppOhwL)) + 0.015f;
                    ss.hwL = clampf(exactL, 2.6f, hw + 0.95f);
                    ss.isMedianEdgeL = true;
                    ss.oppRoadIdxL = bestOppIdxL;
                    ss.suppressLeft = false; // Always preserve crisp central yellow median line!
                }
                if (bestOppOhwR > 0.0f && bestOppGapR < hw + bestOppOhwR + 1.85f) {
                    const float exactR = bestOppGapR * (hw / (hw + bestOppOhwR)) + 0.015f;
                    ss.hwR = clampf(exactR, 2.6f, hw + 0.95f);
                    ss.isMedianEdgeR = true;
                    ss.oppRoadIdxR = bestOppIdxR;
                    ss.suppressRight = false; // Always preserve crisp central yellow median line!
                }

                subs.push_back(ss);
            }
        }

        // Smooth trimmed/fused dual-carriageway edge widths across connected sub-segments (2-pass binomial)
        // so yellow median lines remain razor-straight with zero step jogs at OSM node transitions!
        if (subs.size() >= 3u) {
            smoothTmpL.resize(subs.size());
            smoothTmpR.resize(subs.size());
            for (int pass = 0; pass < 2; ++pass) {
                for (std::size_t idx = 0; idx < subs.size(); ++idx) {
                    float accL = subs[idx].hwL * 2.0f;
                    float accR = subs[idx].hwR * 2.0f;
                    float wSum = 2.0f;
                    if (idx > 0 && (subs[idx].p0 - subs[idx - 1].p1).lengthSq() < 0.25f) {
                        accL += subs[idx - 1].hwL;
                        accR += subs[idx - 1].hwR;
                        wSum += 1.0f;
                    }
                    if (idx + 1 < subs.size() && (subs[idx + 1].p0 - subs[idx].p1).lengthSq() < 0.25f) {
                        accL += subs[idx + 1].hwL;
                        accR += subs[idx + 1].hwR;
                        wSum += 1.0f;
                    }
                    smoothTmpL[idx] = accL / wSum;
                    smoothTmpR[idx] = accR / wSum;
                }
                for (std::size_t idx = 0; idx < subs.size(); ++idx) {
                    subs[idx].hwL = smoothTmpL[idx];
                    subs[idx].hwR = smoothTmpR[idx];
                }
            }
        }

        // Pass 2: Place authentic Korean zebra crosswalks ONLY at real ground-level intersection approach edges
        //         (plus spaced mid-block crosswalks on >145m unbroken city boulevards)!
        if (!isExpressway && !isBridge && !isLink && r.roadClass <= 3u && hw >= 4.8f && !subs.empty()) {
            float lastCwDist = -1e9f;
            for (std::size_t idx = 0; idx < subs.size(); ++idx) {
                if (subs[idx].inInter) continue;
                const float yMidS = 0.5f * (subs[idx].y0 + subs[idx].y1);
                if (has3DRoads && (yMidS - real.sampleDem(subs[idx].mid)) > 1.0f) continue;
                const bool prevMajor = (idx > 0 && subs[idx - 1].crossedMajor &&
                                        (subs[idx].p0 - subs[idx - 1].p1).lengthSq() < 1.0f);
                const bool nextMajor = (idx + 1 < subs.size() && subs[idx + 1].crossedMajor &&
                                        (subs[idx + 1].p0 - subs[idx].p1).lengthSq() < 1.0f);
                if ((prevMajor || nextMajor) && (subs[idx].dist0 - lastCwDist) > 22.0f) {
                    subs[idx].isInterCrosswalk = true;
                    lastCwDist = subs[idx].dist0;
                }
            }
            // Add clean mid-block zebra crossing on ultra-long unbroken straight avenues (>145m gap)
            float runStartDist = subs[0].dist0;
            for (std::size_t idx = 0; idx < subs.size(); ++idx) {
                if (subs[idx].inInter || subs[idx].isInterCrosswalk) {
                    runStartDist = subs[idx].dist1;
                } else if (subs[idx].dist0 - runStartDist > 145.0f) {
                    const float yMidS = 0.5f * (subs[idx].y0 + subs[idx].y1);
                    if (has3DRoads && (yMidS - real.sampleDem(subs[idx].mid)) > 1.0f) continue;
                    bool clearAhead = true;
                    for (std::size_t j = idx + 1; j < subs.size() && (subs[j].dist0 - subs[idx].dist1) < 30.0f; ++j) {
                        if (subs[j].inInter || subs[j].isInterCrosswalk) { clearAhead = false; break; }
                    }
                    if (clearAhead) {
                        subs[idx].isInterCrosswalk = true;
                        runStartDist = subs[idx].dist1;
                    }
                }
            }
        }

        // Pass 3: Emit watertight 3D road quads, sidewalks, expressway barriers, viaduct box girders & Korean piers!
        float nextPierDist = 18.0f;
        for (std::size_t idx = 0; idx < subs.size(); ++idx) {
            const RoadSubSeg& ss = subs[idx];
            if (!ss.inChunk) continue;

            const Vec2& p0 = ss.p0;
            const Vec2& p1 = ss.p1;
            const Vec2& mid = ss.mid;
            const Vec2& n0 = ss.n0;
            const Vec2& n1 = ss.n1;
            const float dist0 = ss.dist0;
            const float dist1 = ss.dist1;

            // Smooth shared vertex edge widths across consecutive sub-segments so trimmed medians stay 100% seamless!
            float hwL0 = ss.hwL, hwL1 = ss.hwL;
            float hwR0 = ss.hwR, hwR1 = ss.hwR;
            if (idx > 0 && (ss.p0 - subs[idx - 1].p1).lengthSq() < 0.25f) {
                hwL0 = 0.5f * (ss.hwL + subs[idx - 1].hwL);
                hwR0 = 0.5f * (ss.hwR + subs[idx - 1].hwR);
            }
            if (idx + 1 < subs.size() && (subs[idx + 1].p0 - ss.p1).lengthSq() < 0.25f) {
                hwL1 = 0.5f * (ss.hwL + subs[idx + 1].hwL);
                hwR1 = 0.5f * (ss.hwR + subs[idx + 1].hwR);
            }

            const Vec2 p0L{p0.x - n0.x * hwL0, p0.z - n0.z * hwL0};
            const Vec2 p0R{p0.x + n0.x * hwR0, p0.z + n0.z * hwR0};
            const Vec2 p1R{p1.x + n1.x * hwR1, p1.z + n1.z * hwR1};
            const Vec2 p1L{p1.x - n1.x * hwL1, p1.z - n1.z * hwL1};
            const Vec3 aL{p0L.x, evalCornerY(p0, p0L, ss.y0), p0L.z};
            const Vec3 aR{p0R.x, evalCornerY(p0, p0R, ss.y0), p0R.z};
            const Vec3 bR{p1R.x, evalCornerY(p1, p1R, ss.y1), p1R.z};
            const Vec3 bL{p1L.x, evalCornerY(p1, p1L, ss.y1), p1L.z};
            const float y0 = 0.5f * (aL.y + aR.y);
            const float y1 = 0.5f * (bL.y + bR.y);
            const float yMid = 0.5f * (y0 + y1);
            const float demMid = real.sampleDem(mid);
            const float clearMid = yMid - demMid;
            // On OCM6 v4, normal hillside Seoul streets smoothly graded up to ~2.2m over local terrain dips
            // keep authentic sidewalks & downward granite retaining curbs instead of bridge parapet walls!
            const bool isElevatedDeck = isBridge || (has3DRoads && clearMid > (isOcm6 ? 2.25f : 0.85f));

            const float effHw = 0.5f * (ss.hwL + ss.hwR);
            const float uL = -effHw;
            const float uR =  effHw;
            const float v0 = 10.0f + dist0;
            const float v1 = 10.0f + dist1;
            const std::uint8_t roadFlags =
                (ss.suppressLeft ? 1u : 0u) |
                (ss.suppressRight ? 2u : 0u) |
                (ss.isInterCrosswalk ? 4u : 0u) |
                (ss.inInter ? 8u : 0u);
            const ColorRGBA roadMeta{
                static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(effHw * 10.0f)), 24, 255)),
                static_cast<std::uint8_t>((std::clamp(laneLayout.totalLanes, 0, 15) << 4) | (roadFlags & 0x0Fu)),
                laneLayout.styleByte,
                255u
            };
            pushQuadV3(roadsV, aL, aR, bR, bL, {0, 1, 0},
                       uL, v0, uR, v0, uR, v1, uL, v1, roadMeta);

            // Flanking raised Korean granite-curb + yellow-tactile-strip sidewalks on ground-level City Streets,
            // exact-fit twin-boulevard central median islands (ZERO overhang into opposing asphalt!),
            // plus flush downward granite retaining aprons so roads/sidewalks never float!
            if (!isExpressway && !isElevatedDeck && !ss.inInter) {
                const bool allowSidewalk = (!isLink && r.roadClass <= 4u);
                const float swW = (r.roadClass <= 2u) ? 3.45f : 2.45f;
                // Authentic Korean 11cm dressed granite curbstone, lowered to 2.5cm wheelchair ramp (턱낮춤) at crosswalks!
                const float curbH = ss.isInterCrosswalk ? 0.025f : 0.11f;
                const float swV0 = 10.0f + dist0;
                const float swV1 = 10.0f + dist1;
                const Vec2 nMid = (n0 + n1).normalized();
                for (float side : {-1.0f, 1.0f}) {
                    if (side < 0.0f && (ss.isMedianEdgeL || ss.suppressLeft)) continue;
                    if (side > 0.0f && (ss.isMedianEdgeR || ss.suppressRight)) continue;
                    const float sideHw0 = (side < 0.0f) ? hwL0 : hwR0;
                    const float sideHw1 = (side < 0.0f) ? hwL1 : hwR1;
                    const float sideHwM = 0.5f * (sideHw0 + sideHw1);
                    const Vec2 s0 = n0 * side;
                    const Vec2 s1 = n1 * side;
                    const Vec2 sM = nMid * side;

                    // If an opposing twin carriageway sits 1.85..8.0m across this side (e.g. Teheran-ro EB/WB!),
                    // trim the median island width cleanly to half the actual gap so it NEVER overhangs opposing asphalt!
                    const float sideOppGap = (side < 0.0f) ? ss.oppGapL : ss.oppGapR;
                    const float sideOppOhw = (side < 0.0f) ? ss.oppOhwL : ss.oppOhwR;
                    const float rawMedGap = (sideOppOhw > 0.0f) ? (sideOppGap - sideHwM - sideOppOhw) : 1e9f;
                    const bool isTwinMedianIsland = (sideOppOhw > 0.0f && rawMedGap > 0.35f && rawMedGap < 8.5f);
                    const float sideSwW = isTwinMedianIsland
                        ? clampf(0.5f * rawMedGap + 0.08f, 0.55f, 4.25f)
                        : swW;

                    bool emittedSw = false;
                    if (allowSidewalk) {
                        const float probeMidOff = sideHwM + sideSwW * 0.48f;
                        const float probeOutOff = sideHwM + sideSwW * 0.90f;
                        const float roadMargin = isTwinMedianIsland ? 0.18f : 0.65f;
                        const Vec2 swP0 = p0 + s0 * probeMidOff;
                        const Vec2 swPM = mid + sM * probeMidOff;
                        const Vec2 swP1 = p1 + s1 * probeMidOff;
                        const Vec2 swOutM = mid + sM * probeOutOff;
                        if (!real.isInWater(swPM) &&
                            !hasSameLevelRoadAt(swP0, y0, roadMargin) &&
                            !hasSameLevelRoadAt(swPM, yMid, roadMargin) &&
                            !hasSameLevelRoadAt(swP1, y1, roadMargin) &&
                            !hasSameLevelRoadAt(swOutM, yMid, 0.18f)) {
                            const Vec2 c0P{p0.x + s0.x * sideHw0, p0.z + s0.z * sideHw0};
                            const Vec2 c1P{p1.x + s1.x * sideHw1, p1.z + s1.z * sideHw1};
                            const Vec2 o0P{p0.x + s0.x * (sideHw0 + sideSwW), p0.z + s0.z * (sideHw0 + sideSwW)};
                            const Vec2 o1P{p1.x + s1.x * (sideHw1 + sideSwW), p1.z + s1.z * (sideHw1 + sideSwW)};
                            const float yc0 = evalCornerY(p0, c0P, ss.y0);
                            const float yc1 = evalCornerY(p1, c1P, ss.y1);
                            const float demO0 = real.sampleDem(o0P);
                            const float demO1 = real.sampleDem(o1P);
                            const float yo0 = (isOcm6 && !isTwinMedianIsland)
                                ? (yc0 + clampf((demO0 - yc0) * 0.25f, -0.08f, 0.10f))
                                : yc0;
                            const float yo1 = (isOcm6 && !isTwinMedianIsland)
                                ? (yc1 + clampf((demO1 - yc1) * 0.25f, -0.08f, 0.10f))
                                : yc1;
                            const Vec3 c0Bot{c0P.x, yc0 - 0.04f, c0P.z};
                            const Vec3 c1Bot{c1P.x, yc1 - 0.04f, c1P.z};
                            const Vec3 c0Top{p0.x + s0.x * (sideHw0 + 0.11f), yc0 + curbH, p0.z + s0.z * (sideHw0 + 0.11f)};
                            const Vec3 c1Top{p1.x + s1.x * (sideHw1 + 0.11f), yc1 + curbH, p1.z + s1.z * (sideHw1 + 0.11f)};
                            const Vec3 o0Top{o0P.x, yo0 + curbH, o0P.z};
                            const Vec3 o1Top{o1P.x, yo1 + curbH, o1P.z};
                            const Vec3 curbN{-s0.x, 0.25f, -s0.z};
                            pushQuadV3(sidewalksV, c0Bot, c1Bot, c1Top, c0Top, curbN.normalized(),
                                       0.02f, swV0, 0.02f, swV1, 0.06f, swV1, 0.06f, swV0, kSwTint);
                            const float topU0 = isTwinMedianIsland ? 0.50f : 0.0f;
                            pushQuadV3(sidewalksV, c0Top, c1Top, o1Top, o0Top, {0, 1, 0},
                                       topU0, swV0, topU0, swV1, 1.0f, swV1, 1.0f, swV0, kSwTint);
                            // Watertight downward granite outer plinth skirt on OCM6 (cleanly seals against road-depressed groundMesh & twin median slopes!)
                            if (isOcm6) {
                                const Vec3 o0Bot{o0P.x, std::min(yo0 - 0.62f, demO0 - 0.62f), o0P.z};
                                const Vec3 o1Bot{o1P.x, std::min(yo1 - 0.62f, demO1 - 0.62f), o1P.z};
                                const Vec3 outN = Vec3{s0.x, 0.0f, s0.z}.normalized();
                                pushQuadV3(sidewalksV, o0Bot, o1Bot, o1Top, o0Top, outN,
                                           0.02f, swV0, 0.02f, swV1, 0.10f, swV1, 0.10f, swV0, kSwTint);
                            }
                            emittedSw = true;
                        }
                    }

                    // For hillside residential streets/alleys without sidewalks, emit a flush downward granite retaining apron
                    // (ZERO bridge wall sticking above road surface -> storefronts remain 100% open & unobstructed!)
                    if (!emittedSw && isOcm6 && clearMid > 0.15f &&
                        !hasSameLevelRoadAt(mid + sM * (sideHwM + 0.65f), yMid, 0.35f)) {
                        const Vec2 c0P{p0.x + s0.x * sideHw0, p0.z + s0.z * sideHw0};
                        const Vec2 c1P{p1.x + s1.x * sideHw1, p1.z + s1.z * sideHw1};
                        const float yc0 = evalCornerY(p0, c0P, ss.y0);
                        const float yc1 = evalCornerY(p1, c1P, ss.y1);
                        const float demC0 = real.sampleDem(c0P);
                        const float demC1 = real.sampleDem(c1P);
                        const Vec3 top0{c0P.x, yc0 + 0.03f, c0P.z};
                        const Vec3 top1{c1P.x, yc1 + 0.03f, c1P.z};
                        const Vec3 bot0{c0P.x, std::min(yc0 - 0.62f, demC0 - 0.62f), c0P.z};
                        const Vec3 bot1{c1P.x, std::min(yc1 - 0.62f, demC1 - 0.62f), c1P.z};
                        const Vec3 outN = Vec3{s0.x, 0.0f, s0.z}.normalized();
                        pushQuadV3(sidewalksV, bot0, bot1, top1, top0, outN,
                                   0.02f, swV0, 0.02f, swV1, 0.10f, swV1, 0.10f, swV0, kSwTint);
                    }
                }
            }

            auto isRoadElevatedAt = [&](std::uint32_t otherRi, const Vec2& pt, float otherY) -> bool {
                if (otherRi >= roads.size()) return false;
                const OsmRoadRec& orr = roads[otherRi];
                const bool ob = ((orr.flags & 2u) != 0u) || (orr.layer >= 1) || ((orr.flags2 & 4u) != 0u);
                return ob || (has3DRoads && (otherY - real.sampleDem(pt)) > (isOcm6 ? 2.25f : 0.85f));
            };

            // Continuous 3D Korean Highway Concrete New Jersey Barriers, Anti-Glare Medians & Urban Soundproof Walls!
            // (Ground-level expressways: 경부고속도로, 올림픽대로, 강변북로!)
            if (isExpressway && !isElevatedDeck && !ss.inInter) {
                const Vec2 nMid = (n0 + n1).normalized();
                const Vec2 segDir = ss.segDir;
                const ColorRGBA kJerseyCol{182, 186, 192, 255};
                const ColorRGBA kAntiGlareGreen{22, 118, 72, 255};
                const ColorRGBA kGuardSteel{196, 202, 212, 255};
                const ColorRGBA kSoundGlass{138, 168, 194, 255};
                const ColorRGBA kSoundFrame{54, 62, 74, 255};

                for (float side : {-1.0f, 1.0f}) {
                    if (side < 0.0f && ss.suppressLeft) continue;
                    if (side > 0.0f && ss.suppressRight) continue;
                    const float sideHw0 = (side < 0.0f) ? hwL0 : hwR0;
                    const float sideHw1 = (side < 0.0f) ? hwL1 : hwR1;
                    const float sideHwM = 0.5f * (sideHw0 + sideHw1);
                    const Vec2 s0 = n0 * side;
                    const Vec2 s1 = n1 * side;
                    const Vec2 sM = nMid * side;

                    const NearestOsmRoad oppNr = real.findNearestRoad(mid + sM * (sideHwM + 4.5f), 14.0f, yMid);
                    const bool isOppParallelExp = oppNr.found && oppNr.roadIdx != ri &&
                                                  oppNr.roadClass <= 1u &&
                                                  std::abs(oppNr.deckY - yMid) < 2.6f &&
                                                  oppNr.tangent.dot(segDir) < -0.78f;

                    const Vec2 rampProbe0 = p0 + s0 * (sideHw0 + 0.85f);
                    const Vec2 rampProbeM = mid + sM * (sideHwM + 0.85f);
                    const Vec2 rampProbe1 = p1 + s1 * (sideHw1 + 0.85f);
                    const bool tightTouching = hasSameLevelRoadAt(rampProbe0, y0, 0.25f) ||
                                               hasSameLevelRoadAt(rampProbeM, yMid, 0.25f) ||
                                               hasSameLevelRoadAt(rampProbe1, y1, 0.25f);
                    if (tightTouching) {
                        if (!isOppParallelExp) continue;
                        const bool oppIsElev = isRoadElevatedAt(oppNr.roadIdx, mid + sM * (sideHwM + 4.5f), oppNr.deckY);
                        if (oppIsElev || (ri > oppNr.roadIdx && std::abs(oppNr.deckY - yMid) < 0.55f)) continue;
                    }

                    const bool isMedianSide = isOppParallelExp || (side < 0.0f ? ss.isMedianEdgeL : ss.isMedianEdgeR) ||
                                              hasSameLevelRoadAt(mid + sM * (sideHwM + 5.2f), yMid, 2.0f);
                    const float inDelta = tightTouching ? 0.02f : 0.14f;
                    const float outDelta = tightTouching ? 0.44f : (isMedianSide ? 0.56f : 0.48f);
                    const float wallH = isMedianSide ? 1.04f : 0.90f;

                    const Vec2 i0P{p0.x + s0.x * (sideHw0 + inDelta), p0.z + s0.z * (sideHw0 + inDelta)};
                    const Vec2 i1P{p1.x + s1.x * (sideHw1 + inDelta), p1.z + s1.z * (sideHw1 + inDelta)};
                    const Vec2 o0P{p0.x + s0.x * (sideHw0 + outDelta), p0.z + s0.z * (sideHw0 + outDelta)};
                    const Vec2 o1P{p1.x + s1.x * (sideHw1 + outDelta), p1.z + s1.z * (sideHw1 + outDelta)};
                    const float yi0 = evalCornerY(p0, i0P, ss.y0);
                    const float yi1 = evalCornerY(p1, i1P, ss.y1);

                    const Vec3 i0B{i0P.x, yi0 - 0.05f, i0P.z};
                    const Vec3 i1B{i1P.x, yi1 - 0.05f, i1P.z};
                    const Vec3 i0T{i0P.x + s0.x * 0.10f, yi0 + wallH, i0P.z + s0.z * 0.10f};
                    const Vec3 i1T{i1P.x + s1.x * 0.10f, yi1 + wallH, i1P.z + s1.z * 0.10f};
                    const Vec3 o0B{o0P.x, yi0 - 0.05f, o0P.z};
                    const Vec3 o1B{o1P.x, yi1 - 0.05f, o1P.z};
                    const Vec3 o0T{o0P.x - s0.x * 0.10f, yi0 + wallH, o0P.z - s0.z * 0.10f};
                    const Vec3 o1T{o1P.x - s1.x * 0.10f, yi1 + wallH, o1P.z - s1.z * 0.10f};

                    const Vec3 inN = Vec3{-s0.x, 0.18f, -s0.z}.normalized();
                    const Vec3 outN = Vec3{s0.x, 0.18f, s0.z}.normalized();
                    pushQuadV3(piersV, i0B, i1B, i1T, i0T, inN, 0, 0, 1, 0, 1, 1, 0, 1, kJerseyCol);
                    pushQuadV3(piersV, i0T, i1T, o1T, o0T, {0, 1, 0}, 0, 0, 1, 0, 1, 1, 0, 1, kJerseyCol);
                    pushQuadV3(piersV, o1B, o0B, o0T, o1T, outN, 0, 0, 1, 0, 1, 1, 0, 1, kJerseyCol);

                    if (isMedianSide) {
                        const float midDelta = 0.5f * (inDelta + outDelta);
                        const Vec3 g0B{p0.x + s0.x * (sideHw0 + midDelta), yi0 + wallH, p0.z + s0.z * (sideHw0 + midDelta)};
                        const Vec3 g1B{p1.x + s1.x * (sideHw1 + midDelta), yi1 + wallH, p1.z + s1.z * (sideHw1 + midDelta)};
                        const Vec3 g0T{g0B.x, yi0 + wallH + 0.42f, g0B.z};
                        const Vec3 g1T{g1B.x, yi1 + wallH + 0.42f, g1B.z};
                        pushQuadV3(piersV, g0B, g1B, g1T, g0T, inN, 0, 0, 1, 0, 1, 1, 0, 1, kAntiGlareGreen);
                        pushQuadV3(piersV, g1B, g0B, g0T, g1T, outN, 0, 0, 1, 0, 1, 1, 0, 1, kAntiGlareGreen);
                    } else {
                        const Vec3 r0B{i0P.x + s0.x * 0.08f, yi0 + wallH + 0.02f, i0P.z + s0.z * 0.08f};
                        const Vec3 r1B{i1P.x + s1.x * 0.08f, yi1 + wallH + 0.02f, i1P.z + s1.z * 0.08f};
                        const Vec3 r0T{r0B.x, yi0 + wallH + 0.24f, r0B.z};
                        const Vec3 r1T{r1B.x, yi1 + wallH + 0.24f, r1B.z};
                        pushQuadV3(piersV, r0B, r1B, r1T, r0T, inN, 0, 0, 1, 0, 1, 1, 0, 1, kGuardSteel);
                        pushQuadV3(piersV, r1B, r0B, r0T, r1T, outN, 0, 0, 1, 0, 1, 1, 0, 1, kGuardSteel);

                        const bool facesWaterfront =
                            real.isInWater(mid + sM * (sideHwM + 26.0f)) ||
                            real.isInWater(mid + sM * (sideHwM + 68.0f)) ||
                            real.isInWater(mid + sM * (sideHwM + 125.0f));
                        if (!isLink && hw >= 6.8f && !facesWaterfront) {
                            const float swDelta = outDelta - 0.08f;
                            const Vec3 s0B{p0.x + s0.x * (sideHw0 + swDelta), yi0 + wallH + 0.24f, p0.z + s0.z * (sideHw0 + swDelta)};
                            const Vec3 s1B{p1.x + s1.x * (sideHw1 + swDelta), yi1 + wallH + 0.24f, p1.z + s1.z * (sideHw1 + swDelta)};
                            const Vec3 s0T{s0B.x, yi0 + wallH + 2.35f, s0B.z};
                            const Vec3 s1T{s1B.x, yi1 + wallH + 2.35f, s1B.z};
                            pushQuadV3(piersV, s0B, s1B, s1T, s0T, inN, 0, 0, 1, 0, 1, 1, 0, 1, kSoundGlass);
                            pushQuadV3(piersV, s1B, s0B, s0T, s1T, outN, 0, 0, 1, 0, 1, 1, 0, 1, kSoundGlass);
                            if ((idx & 1u) == 0u) {
                                pushBoxPiers(piersV, {s0B.x, yi0 + wallH + 1.30f, s0B.z}, {0.16f, 2.25f, 0.16f}, kSoundFrame);
                            }
                            const Vec3 cap0{s0T.x, s0T.y + 0.10f, s0T.z};
                            const Vec3 cap1{s1T.x, s1T.y + 0.10f, s1T.z};
                            pushQuadV3(piersV, s0T, s1T, cap1, cap0, inN, 0, 0, 1, 0, 1, 1, 0, 1, kSoundFrame);
                        }
                    }
                }
            }

            // Elevated Bridge & Korean Expressway Viaduct Box-Girder Deck, Watertight Soffit,
            // Seamless Ramp-Friendly Parapets, Abutment Skirts & Lower-Road-Clearing T-Head / Portal Piers!
            if (isElevatedDeck) {
                const float thick = isExpressway ? 1.42f : 1.15f;
                const float railH = isExpressway ? 0.96f : 0.72f;
                const ColorRGBA kSoffitCol{132, 136, 144, 255};
                const ColorRGBA kParapetCol{182, 186, 192, 255};
                const ColorRGBA kAntiGlareGreen{22, 118, 72, 255};

                // Low approach embankment (clearMid <= 2.35m): extend side retaining skirt cleanly into terrain!
                const bool isAbutmentRamp = (clearMid <= 2.35f) && !real.isInWater(mid);
                const float dem0 = real.sampleDem(p0);
                const float dem1 = real.sampleDem(p1);
                const float botY0 = isAbutmentRamp ? std::min(y0 - thick, dem0 - 0.65f) : (y0 - thick);
                const float botY1 = isAbutmentRamp ? std::min(y1 - thick, dem1 - 0.65f) : (y1 - thick);

                // Watertight bottom concrete box-girder underside soffit (solid when viewed from streets below!)
                if (clearMid > 1.05f || real.isInWater(mid)) {
                    const Vec3 uAL{p0L.x, y0 - thick, p0L.z};
                    const Vec3 uAR{p0R.x, y0 - thick, p0R.z};
                    const Vec3 uBR{p1R.x, y1 - thick, p1R.z};
                    const Vec3 uBL{p1L.x, y1 - thick, p1L.z};
                    pushQuadV3(piersV, uBL, uBR, uAR, uAL, {0, -1, 0},
                               0, 0, 1, 0, 1, 1, 0, 1, kSoffitCol);
                }

                // Side concrete box-girder fascia & parapets:
                // - Inside same-level intersections (ss.inInter) or where a same-level crossing/merge road touches
                //   the deck edge, NEVER emit walls above asphalt (only underside fascia below y0/y1)!
                const Vec2 nMidDeck = (n0 + n1).normalized();
                for (float side : {-1.0f, 1.0f}) {
                    const bool isSuppressed = (side < 0.0f) ? ss.suppressLeft : ss.suppressRight;
                    if (isSuppressed) continue;

                    const bool isMedianEdge = (side < 0.0f) ? ss.isMedianEdgeL : ss.isMedianEdgeR;
                    const std::uint32_t oppIdx = (side < 0.0f) ? ss.oppRoadIdxL : ss.oppRoadIdxR;
                    const float sideHw0 = (side < 0.0f) ? hwL0 : hwR0;
                    const float sideHw1 = (side < 0.0f) ? hwL1 : hwR1;
                    const float sideHwM = 0.5f * (sideHw0 + sideHw1);
                    const Vec2 s0 = n0 * side;
                    const Vec2 s1 = n1 * side;
                    const Vec2 sM = nMidDeck * side;

                    // Check if any same-level road touches/crosses right outside this deck edge!
                    const bool edgeHasSameLevelRoad =
                        ss.inInter ||
                        hasSameLevelRoadAt(p0 + s0 * (sideHw0 + 0.75f), y0, 0.35f) ||
                        hasSameLevelRoadAt(mid + sM * (sideHwM + 0.75f), yMid, 0.35f) ||
                        hasSameLevelRoadAt(p1 + s1 * (sideHw1 + 0.75f), y1, 0.35f);

                    // On twin opposing bridge/viaduct carriageways sharing a flush median:
                    // only emit ONE sleek central Jersey median + green anti-glare outside intersections!
                    if (isMedianEdge) {
                        if (ss.inInter) continue;
                        if (oppIdx != 0xFFFFFFFFu && oppIdx < roads.size()) {
                            const bool oppIsElev = isRoadElevatedAt(oppIdx, mid + sM * (sideHwM + 2.0f), yMid);
                            if (oppIsElev && ri > oppIdx) continue;
                        }
                        const float medH = isExpressway ? 0.92f : 0.42f;
                        const Vec3 mIn0B{p0.x + s0.x * (sideHw0 - 0.16f), y0 - 0.02f, p0.z + s0.z * (sideHw0 - 0.16f)};
                        const Vec3 mIn1B{p1.x + s1.x * (sideHw1 - 0.16f), y1 - 0.02f, p1.z + s1.z * (sideHw1 - 0.16f)};
                        const Vec3 mOut0B{p0.x + s0.x * (sideHw0 + 0.16f), y0 - 0.02f, p0.z + s0.z * (sideHw0 + 0.16f)};
                        const Vec3 mOut1B{p1.x + s1.x * (sideHw1 + 0.16f), y1 - 0.02f, p1.z + s1.z * (sideHw1 + 0.16f)};
                        const Vec3 mIn0T{mIn0B.x, y0 + medH, mIn0B.z};
                        const Vec3 mIn1T{mIn1B.x, y1 + medH, mIn1B.z};
                        const Vec3 mOut0T{mOut0B.x, y0 + medH, mOut0B.z};
                        const Vec3 mOut1T{mOut1B.x, y1 + medH, mOut1B.z};
                        const Vec3 inN = Vec3{-s0.x, 0.15f, -s0.z}.normalized();
                        const Vec3 outN = Vec3{s0.x, 0.15f, s0.z}.normalized();
                        pushQuadV3(piersV, mIn0B, mIn1B, mIn1T, mIn0T, inN, 0,0, 1,0, 1,1, 0,1, kParapetCol);
                        pushQuadV3(piersV, mOut1B, mOut0B, mOut0T, mOut1T, outN, 0,0, 1,0, 1,1, 0,1, kParapetCol);
                        pushQuadV3(piersV, mIn0T, mIn1T, mOut1T, mOut0T, {0,1,0}, 0,0, 1,0, 1,1, 0,1, kParapetCol);
                        if (isExpressway) {
                            const Vec3 g0B{p0.x + s0.x * sideHw0, y0 + medH, p0.z + s0.z * sideHw0};
                            const Vec3 g1B{p1.x + s1.x * sideHw1, y1 + medH, p1.z + s1.z * sideHw1};
                            const Vec3 g0T{g0B.x, y0 + medH + 0.36f, g0B.z};
                            const Vec3 g1T{g1B.x, y1 + medH + 0.36f, g1B.z};
                            pushQuadV3(piersV, g0B, g1B, g1T, g0T, inN, 0,0, 1,0, 1,1, 0,1, kAntiGlareGreen);
                            pushQuadV3(piersV, g1B, g0B, g0T, g1T, outN, 0,0, 1,0, 1,1, 0,1, kAntiGlareGreen);
                        }
                        continue;
                    }

                    if (edgeHasSameLevelRoad) {
                        // Crossing or merging same-level road touches this edge -> ZERO wall above asphalt!
                        continue;
                    }

                    const Vec3 outTop0{p0.x + s0.x * (sideHw0 + 0.32f), y0 + railH, p0.z + s0.z * (sideHw0 + 0.32f)};
                    const Vec3 outTop1{p1.x + s1.x * (sideHw1 + 0.32f), y1 + railH, p1.z + s1.z * (sideHw1 + 0.32f)};
                    const Vec3 outBot0{p0.x + s0.x * (sideHw0 + 0.32f), botY0,      p0.z + s0.z * (sideHw0 + 0.32f)};
                    const Vec3 outBot1{p1.x + s1.x * (sideHw1 + 0.32f), botY1,      p1.z + s1.z * (sideHw1 + 0.32f)};
                    const Vec3 inBot0{p0.x + s0.x * (sideHw0 + 0.02f), y0 - 0.02f, p0.z + s0.z * (sideHw0 + 0.02f)};
                    const Vec3 inBot1{p1.x + s1.x * (sideHw1 + 0.02f), y1 - 0.02f, p1.z + s1.z * (sideHw1 + 0.02f)};
                    const Vec3 inTop0{inBot0.x, y0 + railH, inBot0.z};
                    const Vec3 inTop1{inBot1.x, y1 + railH, inBot1.z};

                    const Vec3 outN = Vec3{s0.x, 0.0f, s0.z}.normalized();
                    const Vec3 inN = Vec3{-s0.x, 0.10f, -s0.z}.normalized();
                    pushQuadV3(piersV, outBot0, outBot1, outTop1, outTop0, outN, 0,0, 1,0, 1,1, 0,1, kPierCol);
                    pushQuadV3(piersV, inBot0, inBot1, inTop1, inTop0, inN, 0,0, 1,0, 1,1, 0,1, kParapetCol);
                    pushQuadV3(piersV, inTop0, inTop1, outTop1, outTop0, {0,1,0}, 0,0, 1,0, 1,1, 0,1, kParapetCol);
                }

                // Korean T-Head Viaduct Piers (T형 교각) or Portal-Frame Straddle Piers (문형 교각)
                // placed every 34m ONLY where vertical clearance > 2.35m, NEVER obstructing lower streets!
                if (dist1 >= nextPierDist) {
                    nextPierDist = dist1 + 34.0f;
                    if (clearMid > 2.35f) {
                        const float pTop = yMid - thick;
                        const float pBot = demMid - 2.8f;
                        const Vec2 nMid = (n0 + n1).normalized();
                        const ColorRGBA kPierColDark{144, 148, 156, 255};
                        const ColorRGBA kCapCol{156, 160, 168, 255};
                        const float capH = 1.05f;
                        const float colTop = pTop - capH;
                        const float colH = std::max(0.8f, colTop - pBot);

                        const bool centerOverRoad = real.isOnLowerRoadUnder(mid, yMid, 1.85f);
                        if (centerOverRoad) {
                            // Portal-Frame Straddle Pier: emit ONLY if both outer side columns clear the lower highway!
                            // Over ultra-wide 10-lane expressways (Gyeongbu!), bridges span clear without mid-highway stubs!
                            const Vec2 colL = mid - nMid * (hw + 1.45f);
                            const Vec2 colR = mid + nMid * (hw + 1.45f);
                            if (!real.isOnLowerRoadUnder(colL, yMid, 1.15f) &&
                                !real.isOnLowerRoadUnder(colR, yMid, 1.15f)) {
                                const float portalSpan = hw * 2.25f + 2.6f;
                                pushOrientedBoxPiers(piersV, {mid.x, pTop - capH * 0.5f, mid.z},
                                                     nMid, ss.segDir, portalSpan, capH, 2.2f, kCapCol);
                                pushOrientedBoxPiers(piersV, {colL.x, 0.5f * (colTop + pBot), colL.z},
                                                     nMid, ss.segDir, 1.9f, colH, 1.9f, kPierColDark);
                                pushOrientedBoxPiers(piersV, {colR.x, 0.5f * (colTop + pBot), colR.z},
                                                     nMid, ss.segDir, 1.9f, colH, 1.9f, kPierColDark);
                            }
                        } else if (hw >= 7.2f) {
                            // Wide Han River bridge / 4-5 lane expressway viaduct: cap beam + twin columns!
                            pushOrientedBoxPiers(piersV, {mid.x, pTop - capH * 0.5f, mid.z},
                                                 nMid, ss.segDir, hw * 1.80f, capH, 2.3f, kCapCol);
                            for (float side : {-0.52f, 0.52f}) {
                                const Vec2 colPos = mid + nMid * (hw * side);
                                if (!real.isOnLowerRoadUnder(colPos, yMid, 1.25f)) {
                                    pushOrientedBoxPiers(piersV, {colPos.x, 0.5f * (colTop + pBot), colPos.z},
                                                         nMid, ss.segDir, 2.3f, colH, 2.3f, kPierColDark);
                                }
                            }
                        } else {
                            // Iconic Korean T-Head Viaduct Pier (내부순환로 / 분당수서로 / IC 램프):
                            // tapered cap beam + sturdy central concrete pier shaft!
                            pushOrientedBoxPiers(piersV, {mid.x, pTop - capH * 0.5f, mid.z},
                                                 nMid, ss.segDir, hw * 1.68f, capH, 2.2f, kCapCol);
                            const float shaftW = clampf(hw * 0.48f, 1.95f, 3.2f);
                            pushOrientedBoxPiers(piersV, {mid.x, 0.5f * (colTop + pBot), mid.z},
                                                 nMid, ss.segDir, shaftW, colH, 2.1f, kPierColDark);
                        }
                    }
                }
            }
        }
    }
    } // end if (!useOcrm)

    // ------------------------------------------------------------------------
    // 2. REAL EXTRUDED 3D OCM4 SEOUL ARCHITECTURE (184,308 Real Buildings + 3D Roofs + Multi-Part Stepped Towers!)
    // ------------------------------------------------------------------------
    // Cache landmark exclusion circles once per chunk build so bespoke hero landmarks never double-render
    struct ExclZone { Vec2 pos; float rSq; };
    std::vector<ExclZone> excl;
    excl.reserve(city.landmarks().size() + 1);
    for (const auto& lm : city.landmarks()) {
        if (lm.kind == SeoulLandmarkKind::GangnamStationPlaza) continue;
        float rad = 68.0f;
        switch (lm.kind) {
            case SeoulLandmarkKind::LotteWorldTower:     rad = 128.0f; break;
            case SeoulLandmarkKind::GyeongbokgungPalace: rad = 165.0f; break;
            case SeoulLandmarkKind::JamsilStadium:       rad = 118.0f; break;
            case SeoulLandmarkKind::NationalAssembly:
            case SeoulLandmarkKind::CoexTradeTower:
            case SeoulLandmarkKind::DongdaemunDDP:
            case SeoulLandmarkKind::SeoulCityHall:       rad =  92.0f; break;
            default:                                     rad =  64.0f; break;
        }
        excl.push_back({lm.pos, rad * rad});
        if (lm.kind == SeoulLandmarkKind::GyeongbokgungPalace) {
            excl.push_back({{lm.pos.x, lm.pos.z + 142.0f}, 78.0f * 78.0f}); // Gwanghwamun Gate
            excl.push_back({{-726.0f, -5488.0f}, 20.0f * 20.0f});          // King Sejong Statue plaza
            excl.push_back({{-726.0f, -5320.0f}, 20.0f * 20.0f});          // Admiral Yi Sun-sin Statue plaza
        }
    }

    std::vector<Vec2> polyPts;
    polyPts.reserve(48);
    std::vector<Vec2> crownPts;
    crownPts.reserve(48);

    auto makeSlopeNormal = [](const Vec3& a, const Vec3& b, const Vec3& c) -> Vec3 {
        Vec3 n = (b - a).cross(c - a);
        const float l = n.length();
        if (l < 1e-5f) return {0.0f, 1.0f, 0.0f};
        n = n / l;
        if (n.y < 0.0f) n = n * -1.0f;
        return n;
    };

    for (std::uint32_t k = 0; k < ch.bldgLen; ++k) {
        const OsmBldgRec& b = bldgs[flatBldgs[ch.bldgStart + k]];
        if (b.vertCount < 3) continue;

        // Emit each building in EXACTLY ONE chunk (by centroid) -> zero duplicate faces!
        const int bcx = std::clamp(static_cast<int>(std::floor((b.cx - hdr.minX) / hdr.chunkSize)),
                                   0, static_cast<int>(hdr.chunksX) - 1);
        const int bcz = std::clamp(static_cast<int>(std::floor((b.cz - hdr.minZ) / hdr.chunkSize)),
                                   0, static_cast<int>(hdr.chunksZ) - 1);
        if (bcx != cx || bcz != cz) continue;

        bool skipHero = false;
        const Vec2 bCenter{b.cx, b.cz};
        for (const auto& ez : excl) {
            if ((bCenter - ez.pos).lengthSq() < ez.rSq) {
                skipHero = true;
                break;
            }
        }
        if (skipHero) continue;

        polyPts.clear();
        for (std::uint16_t vi = 0; vi < b.vertCount; ++vi) {
            polyPts.push_back(real.bldgWorldVert(b, vi));
        }

        // Verify CCW orientation in (x, z) so outward wall normals always point outward!
        float signed2 = 0.0f;
        for (std::size_t i = 0; i < polyPts.size(); ++i) {
            const Vec2& p0 = polyPts[i];
            const Vec2& p1 = polyPts[(i + 1) % polyPts.size()];
            signed2 += p0.x * p1.z - p1.x * p0.z;
        }
        if (signed2 < 0.0f) {
            std::reverse(polyPts.begin(), polyPts.end());
        }

        // Pack OCM4 per-building architectural metadata into vertex alpha (0..254; 255 reserved for legacy landmarks)
        const bool hasRetail1F = (b.flags & 2u) != 0u;
        const bool isDarkMetal = (b.style == 2u || b.style == 8u);
        const std::uint8_t varAlpha = static_cast<std::uint8_t>(
            (hasRetail1F ? 128u : 0u) | (isDarkMetal ? 64u : 0u) | (b.variantSeed % 63u));
        const ColorRGBA wallCol{b.facadeR, b.facadeG, b.facadeB, varAlpha};
        const ColorRGBA roofCol{b.facadeR, b.facadeG, b.facadeB, varAlpha};

        const bool isRoyalPalacePrecinct =
            (b.cx >= -915.0f && b.cx <= -585.0f && b.cz >= -6360.0f && b.cz <= -5805.0f && b.height <= 26.0f);
        std::vector<Vertex3D>* wallDst = &graniteV;
        std::vector<Vertex3D>* roofDst = &roofV;
        std::vector<Vertex3D>* slopedDst = &graniteV;
        if (isRoyalPalacePrecinct) {
            wallDst = &heritageV;
            roofDst = &hanokV;
            slopedDst = &hanokV;
        } else {
            switch (b.style) {
                case 0: wallDst = &aptV;      roofDst = &roofV;  slopedDst = &graniteV; break; // Korean High-Rise Apartment
                case 1: wallDst = &glassV;    roofDst = &roofV;  slopedDst = &glassV;   break; // Blue/Teal Glass Skyscraper
                case 2: wallDst = &glassV;    roofDst = &roofV;  slopedDst = &glassV;   break; // Dark Charcoal/Bronze Metallic Tower
                case 3: wallDst = &graniteV;  roofDst = &roofV;  slopedDst = &graniteV; break; // Honed Granite Corporate Office
                case 4: wallDst = &brickV;    roofDst = &brickV; slopedDst = &brickV;   break; // Brick Commercial (green flat / slate-terra sloped)
                case 5: wallDst = &stuccoV;   roofDst = &roofV;  slopedDst = &stuccoV;  break; // Modern Limestone/Stucco Commercial
                case 6: wallDst = &heritageV; roofDst = &hanokV; slopedDst = &hanokV;   break; // Traditional Korean Hanok (Pocheon Granite/Plaster + Giwa Roof!)
                case 7: wallDst = &graniteV;  roofDst = &roofV;  slopedDst = &graniteV; break; // School / Hospital / Civic
                case 8: wallDst = &stuccoV;   roofDst = &roofV;  slopedDst = &stuccoV;  break; // Industrial / Stadium / Station Hall
                case 9:
                default:
                    wallDst = ((b.variantSeed & 1u) != 0u) ? &brickV : &stuccoV;
                    roofDst = &brickV;
                    slopedDst = &brickV;
                    break; // Korean Multi-Family Villa / House
            }
        }

        // True minHeight extrusion for 3D multi-part stepped skyscrapers & skybridges;
        // for ground-level buildings, anchor main facade cleanly at street-level baseY (v = 0..height)
        // and extend a watertight Korean honed-granite foundation plinth skirt down to minCornerY - 3.5m!
        const bool isElevated = (b.minHeight > 0.15f);
        float minCornerY = b.baseY;
        if (!isElevated) {
            for (const Vec2& p : polyPts) {
                minCornerY = std::min(minCornerY, real.sampleDem(p));
            }
            const NearestOsmRoad nrF = real.findNearestRoad({b.cx, b.cz}, 22.0f, b.baseY);
            if (nrF.found && !nrF.isBridge && nrF.deckY < minCornerY && (minCornerY - nrF.deckY) < 18.0f) {
                minCornerY = std::min(minCornerY, nrF.deckY);
            }
        }
        const float yFoundBot = std::min(b.baseY - 3.5f, minCornerY - 3.5f);
        const float yBot = isElevated ? (b.baseY + b.minHeight) : b.baseY;
        const float yTop = b.baseY + b.height;
        const float vBot = isElevated ? b.minHeight : 0.0f;
        const float vTop = b.height;
        const ColorRGBA kFoundPlinthCol{96, 100, 106, 255};

        // Check if footprint is cleanly star-shaped around centroid (safe for ridged/pyramidal/stepped roofs)
        bool starConvex = true;
        float accumU = 0.0f;
        const bool emitPodiumCornice = !isRoyalPalacePrecinct && (b.style != 6u) &&
            ((b.flags & 4u) != 0u) && !isElevated && (b.height >= 11.5f) && (polyPts.size() <= 20u);
        const ColorRGBA podiumCol{
            static_cast<std::uint8_t>(std::max(44, static_cast<int>(b.facadeR) * 65 / 100)),
            static_cast<std::uint8_t>(std::max(46, static_cast<int>(b.facadeG) * 65 / 100)),
            static_cast<std::uint8_t>(std::max(50, static_cast<int>(b.facadeB) * 65 / 100)),
            255u
        };

        for (std::size_t i = 0; i < polyPts.size(); ++i) {
            const Vec2& pA = polyPts[i];
            const Vec2& pB = polyPts[(i + 1) % polyPts.size()];
            const float dx = pB.x - pA.x;
            const float dz = pB.z - pA.z;
            const float edgeLen = std::sqrt(dx * dx + dz * dz);
            if (edgeLen < 0.15f) continue;

            // Outward normal for CCW polygon in (x, z)
            const Vec3 outN{dz / edgeLen, 0.0f, -dx / edgeLen};
            const Vec2 mid = (pA + pB) * 0.5f;
            if ((mid.x - b.cx) * outN.x + (mid.z - b.cz) * outN.z <= 0.15f) {
                starConvex = false;
            }

            const float u0 = accumU;
            const float u1 = accumU + edgeLen;
            accumU = u1;

            pushQuadV3(*wallDst,
                       {pA.x, yBot, pA.z}, {pB.x, yBot, pB.z},
                       {pB.x, yTop, pB.z}, {pA.x, yTop, pA.z},
                       outN, u0, vBot, u1, vBot, u1, vTop, u0, vTop, wallCol);

            // Watertight honed-granite hillside foundation plinth skirt (0% floating even on 45-deg Seoul hills!)
            if (!isElevated) {
                pushQuadV3(piersV,
                           {pA.x, yFoundBot, pA.z}, {pB.x, yFoundBot, pB.z},
                           {pB.x, yBot + 0.06f, pB.z}, {pA.x, yBot + 0.06f, pA.z},
                           outN, u0 * 0.25f, 0.0f, u1 * 0.25f, 0.0f,
                           u1 * 0.25f, (yBot - yFoundBot) * 0.25f, u0 * 0.25f, (yBot - yFoundBot) * 0.25f,
                           kFoundPlinthCol);
            }

            // 3D Ground-Floor Commercial Podium Cornice Ledge (separates 1F retail glass from upper stories!)
            if (emitPodiumCornice && edgeLen >= 1.2f) {
                const float yC0 = b.baseY + 3.92f;
                const float yC1 = b.baseY + 4.24f;
                const float proj = 0.34f;
                const Vec3 iA0{pA.x, yC0, pA.z};
                const Vec3 iB0{pB.x, yC0, pB.z};
                const Vec3 iA1{pA.x, yC1, pA.z};
                const Vec3 iB1{pB.x, yC1, pB.z};
                const Vec3 oA0{pA.x + outN.x * proj, yC0, pA.z + outN.z * proj};
                const Vec3 oB0{pB.x + outN.x * proj, yC0, pB.z + outN.z * proj};
                const Vec3 oA1{pA.x + outN.x * proj, yC1, pA.z + outN.z * proj};
                const Vec3 oB1{pB.x + outN.x * proj, yC1, pB.z + outN.z * proj};
                pushQuadV3(piersV, oA0, oB0, oB1, oA1, outN, 0,0, 1,0, 1,1, 0,1, podiumCol);
                pushQuadV3(piersV, iA1, iB1, oB1, oA1, {0, 1, 0}, 0,0, 1,0, 1,1, 0,1, podiumCol);
                pushQuadV3(piersV, oA0, oB0, iB0, iA0, {0, -1, 0}, 0,0, 1,0, 1,1, 0,1, podiumCol);
            }
        }

        // Watertight underside soffit cap for elevated skybridges / stepped skyscraper upper parts
        if (b.minHeight > 0.5f) {
            earClipRoofXZ(polyPts, yBot, {124, 128, 134, 255}, piersV, true);
        }

        // --------------------------------------------------------------------
        // REAL 3D ARCHITECTURAL ROOF GEOMETRY (7 Shapes: Flat+Parapet, Gabled, Hipped, Pyramidal, Dome, Skillion, SteppedCrown!)
        // --------------------------------------------------------------------
        const float roofH = std::max(0.0f, static_cast<float>(b.roofHeightQ) * 0.25f);
        const std::uint8_t rShape = b.roofShape;
        bool handledRoof = false;

        if ((rShape == 1u || rShape == 2u) && polyPts.size() == 4u && roofH >= 0.8f) {
            // Authentic 3D Ridged Gabled (1) or Hipped (2) Roof on 4-corner footprints (Korean Villas, Hanoks, Houses, Halls!)
            std::vector<Vec2> eave = polyPts;
            if (b.style == 6u) {
                // Iconic Korean Hanok projecting curved Giwa eave overhang (처마!)
                for (Vec2& ep : eave) {
                    Vec2 dv = ep - bCenter;
                    const float dl = dv.length();
                    if (dl > 0.2f) ep = ep + dv * (0.48f / dl);
                }
            }
            const float len01 = (eave[1] - eave[0]).length() + (eave[3] - eave[2]).length();
            const float len12 = (eave[2] - eave[1]).length() + (eave[0] - eave[3]).length();
            // Arrange so (q0->q1) and (q2->q3) are the TWO LONG EAVE SIDES, (q3->q0) and (q1->q2) are the SHORT ENDS
            const int rot = (len01 >= len12) ? 0 : 1;
            const Vec2 q0 = eave[static_cast<std::size_t>((0 + rot) & 3)];
            const Vec2 q1 = eave[static_cast<std::size_t>((1 + rot) & 3)];
            const Vec2 q2 = eave[static_cast<std::size_t>((2 + rot) & 3)];
            const Vec2 q3 = eave[static_cast<std::size_t>((3 + rot) & 3)];
            const Vec2 endA = (q3 + q0) * 0.5f;
            const Vec2 endB = (q1 + q2) * 0.5f;
            const float inset = (rShape == 1u) ? 0.03f : 0.24f;
            const Vec2 rA = endA + (endB - endA) * inset;
            const Vec2 rB = endB + (endA - endB) * inset;
            const float yPeak = yTop + std::min(roofH, 7.5f);

            const Vec3 v0{q0.x, yTop, q0.z}, v1{q1.x, yTop, q1.z};
            const Vec3 v2{q2.x, yTop, q2.z}, v3{q3.x, yTop, q3.z};
            const Vec3 vrA{rA.x, yPeak, rA.z}, vrB{rB.x, yPeak, rB.z};

            // Two main long roof slopes
            const Vec3 nS1 = makeSlopeNormal(v0, v1, vrB);
            pushQuadV3(*slopedDst, v0, v1, vrB, vrA, nS1, 0,0, 1,0, 1,1, 0,1, roofCol);
            const Vec3 nS2 = makeSlopeNormal(v2, v3, vrA);
            pushQuadV3(*slopedDst, v2, v3, vrA, vrB, nS2, 0,0, 1,0, 1,1, 0,1, roofCol);

            if (rShape == 1u) {
                // Solid masonry/stucco gabled triangular end walls (zero stretched attic windows!)
                const ColorRGBA gableCol{b.facadeR, b.facadeG, b.facadeB, 255u};
                const Vec3 eA = v0 - v3;
                const float eAl = std::max(0.01f, eA.length());
                const Vec3 nEndA{eA.z / eAl, 0.0f, -eA.x / eAl};
                pushTriV3(piersV, v3, v0, vrA, nEndA, gableCol);
                const Vec3 eB = v2 - v1;
                const float eBl = std::max(0.01f, eB.length());
                const Vec3 nEndB{eB.z / eBl, 0.0f, -eB.x / eBl};
                pushTriV3(piersV, v1, v2, vrB, nEndB, gableCol);
            } else {
                // Hipped triangular roof end slopes
                pushTriV3(*slopedDst, v3, v0, vrA, makeSlopeNormal(v3, v0, vrA), roofCol);
                pushTriV3(*slopedDst, v1, v2, vrB, makeSlopeNormal(v1, v2, vrB), roofCol);
            }
            // Close underside on overhanging Hanok eaves
            if (b.style == 6u) {
                earClipRoofXZ(polyPts, yTop, roofCol, *roofDst);
            }
            handledRoof = true;
        } else if ((rShape == 1u || rShape == 2u || rShape == 3u) && starConvex && polyPts.size() <= 10u && roofH >= 0.8f) {
            // Sloped Hipped / Pyramidal apex roof for non-rectangular convex footprints
            const Vec3 apex{b.cx, yTop + std::min(roofH, 9.0f), b.cz};
            for (std::size_t i = 0; i < polyPts.size(); ++i) {
                const Vec2& pA = polyPts[i];
                const Vec2& pB = polyPts[(i + 1) % polyPts.size()];
                const Vec3 vA{pA.x, yTop, pA.z};
                const Vec3 vB{pB.x, yTop, pB.z};
                pushTriV3(*slopedDst, vA, vB, apex, makeSlopeNormal(vA, vB, apex), roofCol);
            }
            handledRoof = true;
        } else if (rShape == 4u && roofH >= 1.5f) {
            // 3D Architectural Dome (Civic Halls, Cathedrals, Observatories, Stadiums!)
            earClipRoofXZ(polyPts, yTop, roofCol, *roofDst);
            const float domeR = std::clamp(std::min(b.halfX, b.halfZ) * 0.78f, 3.0f, 38.0f);
            const float domeH = std::clamp(roofH, 3.0f, domeR * 1.15f);
            constexpr int kRings = 3;
            constexpr int kSegs = 12;
            for (int r = 0; r < kRings; ++r) {
                const float phi0 = (static_cast<float>(r) / static_cast<float>(kRings)) * (kPi * 0.5f);
                const float phi1 = (static_cast<float>(r + 1) / static_cast<float>(kRings)) * (kPi * 0.5f);
                const float rr0 = domeR * std::cos(phi0);
                const float rr1 = domeR * std::cos(phi1);
                const float yy0 = yTop + domeH * std::sin(phi0);
                const float yy1 = yTop + domeH * std::sin(phi1);
                for (int s = 0; s < kSegs; ++s) {
                    const float th0 = (static_cast<float>(s) / static_cast<float>(kSegs)) * (2.0f * kPi);
                    const float th1 = (static_cast<float>(s + 1) / static_cast<float>(kSegs)) * (2.0f * kPi);
                    const Vec3 a0{b.cx + rr0 * std::cos(th0), yy0, b.cz + rr0 * std::sin(th0)};
                    const Vec3 b0{b.cx + rr0 * std::cos(th1), yy0, b.cz + rr0 * std::sin(th1)};
                    const Vec3 b1{b.cx + rr1 * std::cos(th1), yy1, b.cz + rr1 * std::sin(th1)};
                    const Vec3 a1{b.cx + rr1 * std::cos(th0), yy1, b.cz + rr1 * std::sin(th0)};
                    const Vec3 dn = makeSlopeNormal(a0, b0, b1);
                    if (r + 1 == kRings) {
                        pushTriV3(*slopedDst, a0, b0, {b.cx, yTop + domeH, b.cz}, dn, roofCol);
                    } else {
                        pushQuadV3(*slopedDst, a0, b0, b1, a1, dn, 0,0, 1,0, 1,1, 0,1, roofCol);
                    }
                }
            }
            handledRoof = true;
        } else if (rShape == 5u && starConvex && polyPts.size() <= 12u && roofH >= 1.0f) {
            // 3D Mono-Pitch Skillion Slanted Roof + Watertight Gable Side Walls!
            const Vec2 axis = (b.halfX >= b.halfZ) ? Vec2{1.0f, 0.0f} : Vec2{0.0f, 1.0f};
            float pMin = 1e9f, pMax = -1e9f;
            for (const Vec2& p : polyPts) {
                const float pr = (p.x - b.cx) * axis.x + (p.z - b.cz) * axis.z;
                pMin = std::min(pMin, pr);
                pMax = std::max(pMax, pr);
            }
            const float span = std::max(2.0f, pMax - pMin);
            const float sH = std::min(roofH, 6.5f);
            auto evalSlantY = [&](const Vec2& p) -> float {
                const float pr = (p.x - b.cx) * axis.x + (p.z - b.cz) * axis.z;
                return yTop + ((pr - pMin) / span) * sH;
            };
            const Vec3 roofN = Vec3{-axis.x * (sH / span), 1.0f, -axis.z * (sH / span)}.normalized();
            const Vec3 ctr3{b.cx, yTop + sH * 0.5f, b.cz};
            for (std::size_t i = 0; i < polyPts.size(); ++i) {
                const Vec2& pA = polyPts[i];
                const Vec2& pB = polyPts[(i + 1) % polyPts.size()];
                const float yA = evalSlantY(pA);
                const float yB = evalSlantY(pB);
                pushTriV3(*slopedDst, ctr3, {pA.x, yA, pA.z}, {pB.x, yB, pB.z}, roofN, roofCol);
                // Close height delta along walls
                const float dx = pB.x - pA.x, dz = pB.z - pA.z;
                const float el = std::sqrt(dx * dx + dz * dz);
                if (el >= 0.15f && (yA > yTop + 0.05f || yB > yTop + 0.05f)) {
                    const Vec3 outN{dz / el, 0.0f, -dx / el};
                    pushQuadV3(*wallDst, {pA.x, yTop, pA.z}, {pB.x, yTop, pB.z},
                               {pB.x, yB, pB.z}, {pA.x, yA, pA.z},
                               outN, 0.0f, vTop, el, vTop, el, vTop + (yB - yTop), 0.0f, vTop + (yA - yTop), wallCol);
                }
            }
            handledRoof = true;
        } else if (rShape == 6u && starConvex && polyPts.size() <= 20u && b.halfX >= 5.5f && b.halfZ >= 5.5f) {
            // Iconic Seoul Skyscraper Recessed 3D Stepped Architectural Crown!
            earClipRoofXZ(polyPts, yTop, roofCol, *roofDst);
            const float crownH = std::clamp((roofH >= 2.0f) ? roofH : 4.5f, 3.0f, 11.0f);
            const float yCrown = yTop + crownH;
            crownPts.clear();
            for (const Vec2& p : polyPts) {
                crownPts.push_back(bCenter + (p - bCenter) * 0.84f);
            }
            float crownU = 0.0f;
            for (std::size_t i = 0; i < crownPts.size(); ++i) {
                const Vec2& pA = crownPts[i];
                const Vec2& pB = crownPts[(i + 1) % crownPts.size()];
                const float dx = pB.x - pA.x, dz = pB.z - pA.z;
                const float el = std::sqrt(dx * dx + dz * dz);
                if (el < 0.15f) continue;
                const Vec3 outN{dz / el, 0.0f, -dx / el};
                pushQuadV3(*wallDst, {pA.x, yTop, pA.z}, {pB.x, yTop, pB.z},
                           {pB.x, yCrown, pB.z}, {pA.x, yCrown, pA.z},
                           outN, crownU, vTop, crownU + el, vTop, crownU + el, vTop + crownH, crownU, vTop + crownH, wallCol);
                crownU += el;
            }
            earClipRoofXZ(crownPts, yCrown, roofCol, *roofDst);
            if (b.height >= 45.0f && ((b.variantSeed & 1u) == 0u)) {
                const float mastH = (b.height >= 90.0f) ? 16.0f : 9.5f;
                pushBoxPiers(piersV, {b.cx, yCrown + mastH * 0.5f, b.cz}, {0.42f, mastH, 0.42f}, {192, 198, 206, 255});
            }
            handledRoof = true;
        }

        if (!handledRoof) {
            // Flat ear-clipped polygon roof cap (handles L-shaped, U-shaped & courtyard Seoul buildings!)
            earClipRoofXZ(polyPts, yTop, roofCol, *roofDst);

            // Crisp 3D Perimeter Rooftop Parapet Wall (0.95m tall) on medium/tall flat Seoul buildings!
            if (b.style != 6u && b.height >= 12.0f && polyPts.size() <= 24u) {
                const float paraH = (b.height >= 36.0f) ? 1.15f : 0.85f;
                const ColorRGBA paraCol{
                    static_cast<std::uint8_t>(std::max(58, static_cast<int>(b.facadeR) * 82 / 100)),
                    static_cast<std::uint8_t>(std::max(60, static_cast<int>(b.facadeG) * 82 / 100)),
                    static_cast<std::uint8_t>(std::max(64, static_cast<int>(b.facadeB) * 82 / 100)),
                    255u
                };
                for (std::size_t i = 0; i < polyPts.size(); ++i) {
                    const Vec2& pA = polyPts[i];
                    const Vec2& pB = polyPts[(i + 1) % polyPts.size()];
                    const float dx = pB.x - pA.x, dz = pB.z - pA.z;
                    const float el = std::sqrt(dx * dx + dz * dz);
                    if (el < 0.25f) continue;
                    const Vec3 outN{dz / el, 0.0f, -dx / el};
                    pushQuadV3(piersV, {pA.x, yTop, pA.z}, {pB.x, yTop, pB.z},
                               {pB.x, yTop + paraH, pB.z}, {pA.x, yTop + paraH, pA.z},
                               outN, 0,0, 1,0, 1,1, 0,1, paraCol);
                }
            }

            // Baked Architectural Rooftop Superstructures (elevator penthouse, HVAC chillers, helipad & telecom masts!)
            if (!isRoyalPalacePrecinct && b.style != 6u && b.height >= 12.0f && b.halfX >= 5.5f && b.halfZ >= 5.5f) {
                const float phW = std::min(b.halfX * 0.68f, 9.5f);
                const float phD = std::min(b.halfZ * 0.68f, 9.5f);
                const float phH = (b.height >= 42.0f) ? 4.4f : 2.8f;
                const ColorRGBA phCol = (b.style == 1u || b.style == 2u)
                    ? ColorRGBA{112, 122, 134, 255}
                    : ColorRGBA{176, 174, 170, 255};
                pushBoxPiers(piersV, {b.cx, yTop + phH * 0.5f, b.cz}, {phW, phH, phD}, phCol);
                if (b.halfX >= 8.5f && b.halfZ >= 8.5f) {
                    const float ox = std::min(b.halfX * 0.44f, 6.2f);
                    pushBoxPiers(piersV, {b.cx + ox, yTop + 1.15f, b.cz - phD * 0.35f}, {2.6f, 2.3f, 2.2f}, {148, 152, 158, 255});
                    pushBoxPiers(piersV, {b.cx - ox, yTop + 0.95f, b.cz + phD * 0.35f}, {2.2f, 1.9f, 2.0f}, {136, 142, 148, 255});
                }
                // Korean high-rise rooftop helipad slab (옥상 헬기장 >= 65m)
                if (b.height >= 65.0f && b.halfX >= 10.5f && b.halfZ >= 10.5f) {
                    pushBoxPiers(piersV, {b.cx, yTop + phH + 0.16f, b.cz}, {std::min(phW * 0.92f, 8.2f), 0.32f, std::min(phD * 0.92f, 8.2f)}, {96, 102, 110, 255});
                }
                // Sleek rooftop telecom / broadcast mast on Seoul skyscrapers (>= 42m)!
                if (b.height >= 42.0f && ((b.variantSeed & 1u) == 0u)) {
                    const float mastH = (b.height >= 80.0f) ? 14.5f : 8.5f;
                    pushBoxPiers(piersV, {b.cx, yTop + phH + mastH * 0.5f, b.cz}, {0.42f, mastH, 0.42f}, {188, 194, 202, 255});
                    if (b.height >= 60.0f) {
                        // Red aviation obstruction beacon cap
                        pushBoxPiers(piersV, {b.cx, yTop + phH + mastH + 0.35f, b.cz}, {0.72f, 0.72f, 0.72f}, {228, 42, 42, 255});
                    }
                }
            }
        }
    }

    if (!groundV.empty())    out.groundMesh       = vk.createMesh(groundV);
    if (!hillsideV.empty())  out.hillsideMesh     = vk.createMesh(hillsideV);
    if (!roadsV.empty())     out.roadsMesh        = vk.createMesh(roadsV);
    if (!roadPaintV.empty()) out.roadPaintMesh    = vk.createMesh(roadPaintV);
    if (!sidewalksV.empty()) out.sidewalksMesh    = vk.createMesh(sidewalksV);
    if (!piersV.empty())     out.bridgePiersMesh  = vk.createMesh(piersV);
    if (!aptV.empty())       out.bldgAptMesh      = vk.createMesh(aptV);
    if (!glassV.empty())     out.bldgGlassMesh    = vk.createMesh(glassV);
    if (!graniteV.empty())   out.bldgGraniteMesh  = vk.createMesh(graniteV);
    if (!brickV.empty())     out.bldgBrickMesh    = vk.createMesh(brickV);
    if (!stuccoV.empty())    out.bldgStuccoMesh   = vk.createMesh(stuccoV);
    if (!hanokV.empty())     out.bldgHanokMesh    = vk.createMesh(hanokV);
    if (!heritageV.empty())  out.bldgHeritageMesh = vk.createMesh(heritageV);
    if (!roofV.empty())      out.bldgRoofMesh     = vk.createMesh(roofV);
}

void Renderer3D::prepareRealSeoulChunks(VulkanContext& vk, const CityMap& city, const Vec2& focus) {
    if (!city.hasRealSeoulMap()) return;
    const RealSeoulData& real = city.realData();
    const OcMapHeader& hdr = real.header();
    const int centerCx = std::clamp(static_cast<int>(std::floor((focus.x - hdr.minX) / hdr.chunkSize)),
                                    0, static_cast<int>(hdr.chunksX) - 1);
    const int centerCz = std::clamp(static_cast<int>(std::floor((focus.z - hdr.minZ) / hdr.chunkSize)),
                                    0, static_cast<int>(hdr.chunksZ) - 1);

    const int radius = settings_.chunkRadius(); // Dynamically 3x3 (<=400m) .. 9x9 (1600m) from Graphics Settings!
    struct PendingChunk { int cx, cz, distSq; std::int64_t key; };
    std::vector<PendingChunk> missing;
    missing.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));

    for (int dz = -radius; dz <= radius; ++dz) {
        const int cz = centerCz + dz;
        if (cz < 0 || cz >= static_cast<int>(hdr.chunksZ)) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int cx = centerCx + dx;
            if (cx < 0 || cx >= static_cast<int>(hdr.chunksX)) continue;
            const std::int64_t key = (static_cast<std::int64_t>(cz) << 16) | static_cast<std::int64_t>(cx);
            auto it = realChunks_.find(key);
            if (it == realChunks_.end()) {
                missing.push_back({cx, cz, dx * dx + dz * dz, key});
            } else {
                it->second.lastUsedFrame = frameCounter_;
            }
        }
    }

    if (!missing.empty()) {
        std::sort(missing.begin(), missing.end(), [](const PendingChunk& a, const PendingChunk& b) {
            return a.distSq < b.distSq;
        });
        // Full synchronous warmup on initial startup or fast-travel teleport; smooth frame-budgeted streaming while driving!
        const bool fullWarmup = realChunks_.empty() || (missing.size() >= 12u);
        const std::size_t budget = fullWarmup ? missing.size() : std::min<std::size_t>(missing.size(), 3u);
        for (std::size_t i = 0; i < budget; ++i) {
            const PendingChunk& pc = missing[i];
            RealChunkGpu ch{};
            ch.lastUsedFrame = frameCounter_;
            buildRealSeoulChunkMesh(vk, city, pc.cx, pc.cz, ch);
            realChunks_.emplace(pc.key, std::move(ch));
        }
    }

    // LRU evict oldest inactive chunks safely before render passes start (scales with active Render Distance!)
    const std::size_t maxResidentChunks = std::max<std::size_t>(
        49u, static_cast<std::size_t>((2 * radius + 3) * (2 * radius + 3)));
    if (realChunks_.size() > maxResidentChunks) {
        vk.waitIdle();
        while (realChunks_.size() > maxResidentChunks) {
            auto oldestIt = realChunks_.begin();
            for (auto it = realChunks_.begin(); it != realChunks_.end(); ++it) {
                if (it->second.lastUsedFrame < oldestIt->second.lastUsedFrame) {
                    oldestIt = it;
                }
            }
            if (oldestIt->second.lastUsedFrame == frameCounter_) break;
            destroyRealChunkGpu(vk, oldestIt->second);
            realChunks_.erase(oldestIt);
        }
    }
}

void Renderer3D::updateAndDrawRealSeoulChunks(VulkanContext& vk, const CityMap& city,
                                              const Vec2& focus, bool shadowPass) {
    if (!city.hasRealSeoulMap()) return;
    const RealSeoulData& real = city.realData();
    const OcMapHeader& hdr = real.header();
    const int centerCx = std::clamp(static_cast<int>(std::floor((focus.x - hdr.minX) / hdr.chunkSize)),
                                    0, static_cast<int>(hdr.chunksX) - 1);
    const int centerCz = std::clamp(static_cast<int>(std::floor((focus.z - hdr.minZ) / hdr.chunkSize)),
                                    0, static_cast<int>(hdr.chunksZ) - 1);

    const int radius = shadowPass ? ((settings_.shadowQuality >= 4) ? 2 : 1) : settings_.chunkRadius();
    const ColorRGBA kWhite{255, 255, 255, 255};
    const Mat4 id = Mat4::identity();

    for (int dz = -radius; dz <= radius; ++dz) {
        const int cz = centerCz + dz;
        if (cz < 0 || cz >= static_cast<int>(hdr.chunksZ)) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int cx = centerCx + dx;
            if (cx < 0 || cx >= static_cast<int>(hdr.chunksX)) continue;
            const std::int64_t key = (static_cast<std::int64_t>(cz) << 16) | static_cast<std::int64_t>(cx);
            auto it = realChunks_.find(key);
            if (it == realChunks_.end()) continue;
            const RealChunkGpu& ch = it->second;

            if (!shadowPass) {
                drawMesh(vk, ch.groundMesh,      id, kWhite, 0.86f, 0.00f, 0.0f, MatId::SidewalkPavers, false);
                drawMesh(vk, ch.hillsideMesh,    id, kWhite, 0.90f, 0.00f, 0.0f, MatId::FoliageGrass,   false);
                drawMesh(vk, ch.roadsMesh,       id, kWhite, 0.56f, 0.00f, 0.0f,
                         ch.usesOcrm ? MatId::RoadPoly : MatId::RoadAsphalt, false);
                if (ch.usesOcrm) {
                    drawMesh(vk, ch.roadPaintMesh, id, kWhite, 0.46f, 0.02f, 0.0f, MatId::RoadPaint, false);
                }
                drawMesh(vk, ch.sidewalksMesh,   id, kWhite, 0.78f, 0.00f, 0.0f, MatId::SidewalkPavers, false);
            }
            drawMesh(vk, ch.bridgePiersMesh, id, kWhite, 0.80f, 0.04f, 0.0f, MatId::BridgeConcrete,       shadowPass);
            drawMesh(vk, ch.bldgAptMesh,     id, kWhite, 0.78f, 0.03f, 0.0f, MatId::KoreanAptFacade,      shadowPass);
            drawMesh(vk, ch.bldgGlassMesh,   id, kWhite, 0.10f, 0.42f, 0.0f, MatId::GlassSkyscraper,      shadowPass);
            drawMesh(vk, ch.bldgGraniteMesh, id, kWhite, 0.54f, 0.12f, 0.0f, MatId::ModernConcrete,       shadowPass);
            drawMesh(vk, ch.bldgBrickMesh,   id, kWhite, 0.80f, 0.04f, 0.0f, MatId::KoreanApartmentBrick, shadowPass);
            drawMesh(vk, ch.bldgStuccoMesh,  id, kWhite, 0.82f, 0.02f, 0.0f, MatId::StuccoCommercial,     shadowPass);
            drawMesh(vk, ch.bldgHanokMesh,    id, kWhite, 0.56f, 0.08f, 0.0f, MatId::HanokGiwaRoof,        shadowPass);
            drawMesh(vk, ch.bldgHeritageMesh, id, kWhite, 0.72f, 0.04f, 0.0f, MatId::LandmarkStone,        shadowPass);
            if (!shadowPass) {
                drawMesh(vk, ch.bldgRoofMesh, id, kWhite, 0.82f, 0.03f, 0.0f, MatId::RooftopConcrete, false);
            }
        }
    }
}

Vec3 Renderer3D::computeSunDirection(const GameWorld& world) const {
    const float h = world.timeOfDayHours();
    const float sunAngle = (h - 6.0f) * (kPi / 12.0f);
    if (h >= 5.5f && h <= 18.8f) {
        // Authentic Seoul latitude (37.5 deg N): max solar elevation ~49 deg -> crisp architectural shadows all day!
        const float elev = clampf(std::sin(sunAngle) * 0.78f, 0.16f, 0.76f);
        const float horiz = std::sqrt(std::max(0.01f, 1.0f - elev * elev));
        const float az = -0.68f + (h - 12.0f) * 0.24f;
        return Vec3{-std::cos(az) * horiz, -elev, -std::sin(az) * horiz}.normalized();
    }
    // Crisp moonlight angle at night
    return Vec3{-0.38f, -0.74f, -0.55f}.normalized();
}

SceneUBO Renderer3D::buildSceneUBO(const GameWorld& world, const CameraState& camera,
                                   int width, int height, const Vec3& lightDir,
                                   const Mat4& lightVP, bool shadowsActive) const {
    SceneUBO ubo{};
    const float aspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
    // Camera-relative view matrix (origin = 0,0,0) paired with worldPos - viewPos in world.vert/sky.vert
    // eliminates float32 depth cancellation Z-fighting across the entire 30km x 25km Seoul map!
    const Vec3 relTarget = camera.target - camera.position;
    const Mat4 viewRel = Mat4::lookAt({0.0f, 0.0f, 0.0f}, relTarget, camera.up);
    const Mat4 proj = Mat4::perspectiveVulkan(camera.fovyDeg * (kPi / 180.0f), aspect, camera.nearZ, camera.farZ);
    ubo.viewProj = proj * viewRel;
    ubo.lightVP = lightVP;

    const float h = world.timeOfDayHours();
    const float dayF = clampf(std::sin(clampf((h - 5.5f) / 13.0f, 0.0f, 1.0f) * kPi) * 1.45f, 0.0f, 1.0f);
    const float nightF = 1.0f - dayF;
    const float sunset = clampf(1.0f - std::abs(h - 17.8f) * 0.55f, 0.0f, 1.0f) +
                         clampf(1.0f - std::abs(h - 6.4f) * 0.65f, 0.0f, 1.0f);
    const float wet = world.wetRoadFactor();

    // Sun / moon directional color (linear HDR!)
    Vec3 sunDay{1.18f, 1.10f, 0.99f};
    Vec3 sunSet{1.35f, 0.68f, 0.34f};
    Vec3 sunNight{0.22f, 0.30f, 0.48f};
    Vec3 sunCol = Vec3{
        lerpf(sunNight.x, lerpf(sunDay.x, sunSet.x, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(sunNight.y, lerpf(sunDay.y, sunSet.y, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(sunNight.z, lerpf(sunDay.z, sunSet.z, clampf(sunset, 0.0f, 1.0f)), dayF)
    } * (1.0f - wet * 0.42f);

    Vec3 skyAmb{
        lerpf(0.06f, lerpf(0.28f, 0.42f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.09f, lerpf(0.42f, 0.32f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.16f, lerpf(0.68f, 0.34f, clampf(sunset, 0.0f, 1.0f)), dayF)
    };
    Vec3 gndAmb{
        lerpf(0.04f, 0.14f, dayF),
        lerpf(0.05f, 0.15f, dayF),
        lerpf(0.07f, 0.17f, dayF)
    };
    Vec3 fogCol{
        lerpf(0.09f, lerpf(0.68f, 0.88f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.12f, lerpf(0.78f, 0.56f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.20f, lerpf(0.90f, 0.48f, clampf(sunset, 0.0f, 1.0f)), dayF)
    };
    Vec3 horCol{
        lerpf(0.11f, lerpf(0.78f, 0.96f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.15f, lerpf(0.86f, 0.62f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.26f, lerpf(0.96f, 0.50f, clampf(sunset, 0.0f, 1.0f)), dayF)
    };
    if (wet > 0.05f) {
        fogCol = Vec3{lerpf(fogCol.x, 0.34f * dayF + 0.08f, wet * 0.65f),
                      lerpf(fogCol.y, 0.38f * dayF + 0.10f, wet * 0.65f),
                      lerpf(fogCol.z, 0.44f * dayF + 0.14f, wet * 0.65f)};
    }

    ubo.lightDirAndShadows[0] = lightDir.x;
    ubo.lightDirAndShadows[1] = lightDir.y;
    ubo.lightDirAndShadows[2] = lightDir.z;
    ubo.lightDirAndShadows[3] = shadowsActive ? settings_.shadowPcfTaps() : 0.0f;

    ubo.sunColorAndTime[0] = sunCol.x;
    ubo.sunColorAndTime[1] = sunCol.y;
    ubo.sunColorAndTime[2] = sunCol.z;
    ubo.sunColorAndTime[3] = world.elapsedTime();

    ubo.skyAmbientAndNight[0] = skyAmb.x;
    ubo.skyAmbientAndNight[1] = skyAmb.y;
    ubo.skyAmbientAndNight[2] = skyAmb.z;
    ubo.skyAmbientAndNight[3] = nightF;

    ubo.groundAmbientAndWet[0] = gndAmb.x;
    ubo.groundAmbientAndWet[1] = gndAmb.y;
    ubo.groundAmbientAndWet[2] = gndAmb.z;
    ubo.groundAmbientAndWet[3] = wet;

    ubo.fogColorAndTexel[0] = fogCol.x;
    ubo.fogColorAndTexel[1] = fogCol.y;
    ubo.fogColorAndTexel[2] = fogCol.z;
    ubo.fogColorAndTexel[3] = 1.0f / static_cast<float>(VulkanContext::kShadowMapSize);

    ubo.horizonColorAndDay[0] = horCol.x;
    ubo.horizonColorAndDay[1] = horCol.y;
    ubo.horizonColorAndDay[2] = horCol.z;
    ubo.horizonColorAndDay[3] = dayF;

    ubo.viewPosAndHour[0] = camera.position.x;
    ubo.viewPosAndHour[1] = camera.position.y;
    ubo.viewPosAndHour[2] = camera.position.z;
    ubo.viewPosAndHour[3] = h;

    // -----------------------------------------------------------------------
    // Gather Dynamic Local Lights capped live by GraphicsSettings::maxLocalLights()!
    // -----------------------------------------------------------------------
    const int maxLights = std::min(SceneUBO::kMaxLocalLights, settings_.maxLocalLights());
    int nLights = 0;
    auto pushLocalLight = [&](const Vec3& pos, float radius, const Vec3& col,
                              bool isSpot, const Vec3& dir = {0, -1, 0}, float cosOuter = 0.72f) {
        if (nLights >= maxLights) return;
        ubo.lightPosRadius[nLights][0] = pos.x;
        ubo.lightPosRadius[nLights][1] = pos.y;
        ubo.lightPosRadius[nLights][2] = pos.z;
        ubo.lightPosRadius[nLights][3] = radius;
        ubo.lightColorType[nLights][0] = col.x;
        ubo.lightColorType[nLights][1] = col.y;
        ubo.lightColorType[nLights][2] = col.z;
        ubo.lightColorType[nLights][3] = isSpot ? 1.0f : 0.0f;
        ubo.lightDirCone[nLights][0] = dir.x;
        ubo.lightDirCone[nLights][1] = dir.y;
        ubo.lightDirCone[nLights][2] = dir.z;
        ubo.lightDirCone[nLights][3] = cosOuter;
        ++nLights;
    };

    const CityMap& city = world.city();
    const Vec2 focus2D{camera.target.x, camera.target.z};
    const auto& pl = world.player();

    // 1. Player Firearm Muzzle Flash Point Light
    if (maxLights > 0 && pl.muzzleFlashTimer > 0.001f && pl.drivingVehicleId < 0) {
        const Vec2 pf = forwardFromHeading(pl.facingYaw);
        const float gy = std::max(city.surfaceHeightAt(pl.pos, pl.posY), pl.posY);
        const float flashK = clampf(pl.muzzleFlashTimer / 0.055f, 0.25f, 1.0f);
        pushLocalLight({pl.pos.x + pf.x * 0.75f, gy + 1.38f, pl.pos.z + pf.z * 0.75f},
                       13.5f, {22.0f * flashK, 13.5f * flashK, 5.0f * flashK}, false);
    }

    // 2. Active Flashing Police / 119 Ambulance / FireTruck Roof Sirens (up to 4 nearest!)
    struct LightCand { float distSq; int idx; };
    std::vector<LightCand> sirenCands;
    sirenCands.reserve(8);
    std::vector<LightCand> headCands;
    headCands.reserve(16);
    const bool headlightsOn = (nightF > 0.10f || wet > 0.20f);
    const float headIntensity = clampf(nightF * 1.15f + wet * 0.45f, 0.0f, 1.25f);

    const auto& vehs = world.vehicles();
    if (maxLights > 0) {
        for (int vi = 0; vi < static_cast<int>(vehs.size()); ++vi) {
            const auto& v = vehs[static_cast<std::size_t>(vi)];
            if (v.destroyed) continue;
            const float d2 = (v.pos - focus2D).lengthSq();
            if (v.sirenActive && d2 < 78.0f * 78.0f) {
                sirenCands.push_back({v.isPlayerDriving ? -1.0f : d2, vi});
            }
            if (headlightsOn && d2 < 68.0f * 68.0f && (v.isPlayerDriving || v.hasNpcDriver || v.sirenActive)) {
                headCands.push_back({v.isPlayerDriving ? -1.0f : d2, vi});
            }
        }
    }
    std::sort(sirenCands.begin(), sirenCands.end(), [](const LightCand& a, const LightCand& b) { return a.distSq < b.distSq; });
    for (int i = 0; i < std::min<int>(4, static_cast<int>(sirenCands.size())); ++i) {
        const auto& v = vehs[static_cast<std::size_t>(sirenCands[static_cast<std::size_t>(i)].idx)];
        const auto& spec = getVehicleSpec(v.kind);
        const float gy = std::max(city.surfaceHeightAt(v.pos, v.posY), v.posY);
        const bool redPhase = std::sin(v.sirenPhase * 14.0f) > 0.0f;
        const float boost = lerpf(0.75f, 1.35f, nightF);
        const Vec3 sCol = redPhase
            ? Vec3{18.5f * boost, 1.1f * boost, 1.4f * boost}
            : Vec3{1.1f * boost, 6.4f * boost, 21.0f * boost};
        pushLocalLight({v.pos.x, gy + spec.chassisHeight + 0.65f, v.pos.z}, 22.0f, sCol, false);
    }

    // 3. Vehicle Twin Front Headlight Spotlights (player vehicle + up to 3 nearest traffic cars!)
    if (maxLights > 0 && headlightsOn && headIntensity > 0.05f) {
        std::sort(headCands.begin(), headCands.end(), [](const LightCand& a, const LightCand& b) { return a.distSq < b.distSq; });
        for (int i = 0; i < std::min<int>(4, static_cast<int>(headCands.size())); ++i) {
            if (nLights + 2 > maxLights) break;
            const auto& v = vehs[static_cast<std::size_t>(headCands[static_cast<std::size_t>(i)].idx)];
            const auto& spec = getVehicleSpec(v.kind);
            const float gy = std::max(city.surfaceHeightAt(v.pos, v.posY), v.posY);
            const Vec2 vf = forwardFromHeading(v.heading);
            const Vec2 vr = rightFromHeading(v.heading);
            const ColorRGBA hl = getVehicleHeadlightColorRGBA(v.headlightColorIdx, nightF > 0.35f);
            const Vec3 hlLin{
                std::pow(static_cast<float>(hl.r) / 255.0f, 2.2f) * 11.2f * headIntensity,
                std::pow(static_cast<float>(hl.g) / 255.0f, 2.2f) * 10.9f * headIntensity,
                std::pow(static_cast<float>(hl.b) / 255.0f, 2.2f) * 10.2f * headIntensity
            };
            const Vec3 spotDir = Vec3{vf.x, -0.11f, vf.z}.normalized();
            const float lampY = gy + ((v.kind == VehicleKind::SeoulBus || v.kind == VehicleKind::FireTruck) ? 1.05f : 0.78f);
            for (float side : {-1.0f, 1.0f}) {
                const Vec3 lPos{
                    v.pos.x + vf.x * (spec.halfLength + 0.14f) + vr.x * (side * spec.halfWidth * 0.62f),
                    lampY,
                    v.pos.z + vf.z * (spec.halfLength + 0.14f) + vr.z * (side * spec.halfWidth * 0.62f)
                };
                pushLocalLight(lPos, 52.0f, hlLin, true, spotDir, 0.65f);
            }
        }
    }

    // 4. Nearest Warm Seoul Streetlights illuminating night & rainy sidewalks/boulevards!
    if (nightF > 0.16f && nLights < maxLights) {
        std::vector<LightCand> lampCands;
        lampCands.reserve(24);
        const auto& props = city.streetProps();
        for (int pi = 0; pi < static_cast<int>(props.size()); ++pi) {
            const auto& p = props[static_cast<std::size_t>(pi)];
            if (p.kind != StreetPropKind::StreetLight || p.broken) continue;
            const float d2 = (p.pos - focus2D).lengthSq();
            if (d2 < 72.0f * 72.0f) {
                lampCands.push_back({d2, pi});
            }
        }
        std::sort(lampCands.begin(), lampCands.end(), [](const LightCand& a, const LightCand& b) { return a.distSq < b.distSq; });
        const float lampGain = clampf((nightF - 0.14f) / 0.65f, 0.0f, 1.0f);
        for (const auto& lc : lampCands) {
            if (nLights >= maxLights) break;
            const auto& p = props[static_cast<std::size_t>(lc.idx)];
            const float propBaseY = (p.baseY > -1e8f) ? p.baseY : city.surfaceHeightAt(p.pos);
            const float gy = propBaseY + p.posY;
            const float ax = std::cos(p.yaw) * 2.10f;
            const float az = std::sin(p.yaw) * 2.10f;
            pushLocalLight({p.pos.x + ax, gy + 7.05f, p.pos.z + az},
                           24.0f, {10.5f * lampGain, 8.2f * lampGain, 5.2f * lampGain}, false);
        }
    }

    const float fogStartM = (settings_.fogMode == 2)
        ? std::max(80.0f, static_cast<float>(settings_.renderDistanceM) * 0.25f)
        : std::max(120.0f, static_cast<float>(settings_.renderDistanceM) * 0.42f);
    const float fogDensScale = (settings_.fogMode == 0) ? 0.0f : ((settings_.fogMode == 2) ? 1.85f : 1.0f);

    ubo.localLightCount[0] = static_cast<float>(nLights);
    ubo.localLightCount[1] = static_cast<float>(settings_.reflectionParallax);
    ubo.localLightCount[2] = fogStartM;
    ubo.localLightCount[3] = fogDensScale;
    return ubo;
}

void Renderer3D::drawMesh(VulkanContext& vk, const GpuMesh& mesh, const Mat4& model,
                          ColorRGBA tint, float roughness, float metallic, float emissive,
                          MatId matId, bool shadowPass, WorldPipelineKind pipe) {
    if (!mesh.valid()) return;
    if (shadowPass) {
        vk.drawShadowMesh(mesh, model);
        return;
    }
    vk.bindWorldPipeline(pipe);
    DrawPush pc{};
    pc.model = model;
    fillColor4(pc.tint, tint);
    pc.pbrParams[0] = roughness;
    pc.pbrParams[1] = metallic;
    pc.pbrParams[2] = emissive;
    pc.pbrParams[3] = static_cast<float>(static_cast<int>(matId)) / 255.0f;
    vk.drawWorldMesh(mesh, pc);
}

void Renderer3D::drawBox(VulkanContext& vk, const Vec3& center, const Vec3& size, float yawRad,
                         ColorRGBA tint, float roughness, float metallic, float emissive,
                         MatId matId, bool shadowPass, WorldPipelineKind pipe) {
    Mat4 m = Mat4::translation(center.x, center.y, center.z);
    if (std::abs(yawRad) > 1e-4f) {
        m = m * Mat4::rotationAxis({0, 1, 0}, -yawRad);
    }
    m = m * Mat4::scale(size.x, size.y, size.z);
    drawMesh(vk, cubeMesh_, m, tint, roughness, metallic, emissive, matId, shadowPass, pipe);
}

void Renderer3D::drawSignQuad(VulkanContext& vk, const Vec3& center, const Vec3& outNormal,
                              const Vec3& upAxis, float width, float height, int atlasSlot,
                              float emissive, ColorRGBA tint) {
    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (signAtlas4k_) {
        const int slot = std::clamp(atlasSlot, 0, 63);
        const int col = slot % 4;
        const int row = slot / 4;
        u0 = (static_cast<float>(col) * 1024.0f + 6.0f) / 4096.0f;
        u1 = (static_cast<float>(col + 1) * 1024.0f - 6.0f) / 4096.0f;
        v0 = (static_cast<float>(row) * 256.0f + 4.0f) / 4096.0f;
        v1 = (static_cast<float>(row + 1) * 256.0f - 4.0f) / 4096.0f;
    } else {
        int s = std::clamp(atlasSlot, 0, 63);
        if (s <= 7)       s = s & 3;                     // Expressway gantries 0..3
        else if (s <= 15) s = 4 + ((s - 8) & 3);         // City boulevard gantries 4..7
        else if (s <= 47) s = 8 + ((s - 16) % 12);       // Korean shopfronts 8..19
        else if (s <= 55) s = 20 + ((s - 48) & 3);       // Seoul Metro canopies 20..23
        else if (s == 56) s = 24;                        // Bus BIS
        else if (s == 57) s = 25;                        // Apt emblem
        else if (s <= 59) s = 27 + (s - 58);             // Bus LED
        else if (s == 60) s = 29;                        // Korean license plate
        else if (s == 61) s = 31;                        // Police panel
        else              s = 4;                         // Fallback road sign
        const int col = s % 4;
        const int row = s / 4;
        u0 = (static_cast<float>(col) * 256.0f + 2.5f) / 1024.0f;
        u1 = (static_cast<float>(col + 1) * 256.0f - 2.5f) / 1024.0f;
        v0 = (static_cast<float>(row) * 128.0f + 2.5f) / 1024.0f;
        v1 = (static_cast<float>(row + 1) * 128.0f - 2.5f) / 1024.0f;
    }

    const Vec3 n = outNormal.normalized();
    const Vec3 up = upAxis.normalized();
    // Viewer looking AT the sign along -n sees rightAxis = cross(up, n) pointing to their right -> Hangul NEVER mirrored!
    const Vec3 right = Vec3{
        up.y * n.z - up.z * n.y,
        up.z * n.x - up.x * n.z,
        up.x * n.y - up.y * n.x
    }.normalized();
    const Vec3 hw = right * (width * 0.5f);
    const Vec3 hh = up * (height * 0.5f);

    const Vec3 tl = hh - hw;
    const Vec3 tr = hh + hw;
    const Vec3 br = hw - hh;
    const Vec3 bl = Vec3{-hw.x - hh.x, -hw.y - hh.y, -hw.z - hh.z};

    auto mkV = [&](const Vec3& p, float u, float v) -> Vertex3D {
        Vertex3D vv{};
        vv.px = p.x; vv.py = p.y; vv.pz = p.z;
        vv.nx = n.x; vv.ny = n.y; vv.nz = n.z;
        vv.u = u;    vv.v = v;
        vv.r = 1.0f; vv.g = 1.0f; vv.b = 1.0f; vv.a = 1.0f;
        return vv;
    };

    const Vertex3D quad[6] = {
        mkV(tl, u0, v0), mkV(bl, u0, v1), mkV(br, u1, v1),
        mkV(tl, u0, v0), mkV(br, u1, v1), mkV(tr, u1, v0)
    };

    DrawPush dp{};
    dp.model = Mat4::translation(center.x, center.y, center.z);
    fillColor4(dp.tint, tint);
    dp.pbrParams[0] = 0.34f;
    dp.pbrParams[1] = 0.05f;
    dp.pbrParams[2] = emissive;
    dp.pbrParams[3] = static_cast<float>(static_cast<int>(MatId::SignTextured)) / 255.0f;
    vk.drawDynamicTriangles3D(quad, 6, WorldPipelineKind::Opaque, dp);
}

void Renderer3D::drawSeoulTerrainAndRoads(VulkanContext& vk, const GameWorld& world,
                                          const Vec2& focus, bool shadowPass) {
    const CityMap& city = world.city();

    if (city.hasRealSeoulMap()) {
        if (!shadowPass) {
            const ColorRGBA kWhite{255, 255, 255, 255};
            const Mat4 id = Mat4::identity();
            // 1. Real SRTM 30m DEM Urban Floor (smooth fine granite plaza stone), Surrounding Mountains & Granite Cliffs!
            drawMesh(vk, realTerrainMesh_,     id, {168, 170, 174, 255}, 0.86f, 0.00f, 0.0f, MatId::SidewalkPavers, false);
            drawMesh(vk, mountainForestMesh_,  id, kWhite, 0.90f, 0.00f, 0.0f, MatId::FoliageGrass,   false);
            drawMesh(vk, mountainGraniteMesh_, id, kWhite, 0.85f, 0.02f, 0.0f, MatId::GraniteRock,    false);
            // 2. Real OpenStreetMap Han River (with Yeouido/Bamseom/Nodeul islands), Seokchon Lake & Tributaries!
            drawMesh(vk, realWaterMesh_,       id, kWhite, 0.06f, 0.06f, 0.0f, MatId::HanRiverWater,  false);
            // 3. Real OpenStreetMap Parks (17,931 textured organic grass tris!)
            drawMesh(vk, realParksMesh_,       id, kWhite, 0.88f, 0.00f, 0.0f, MatId::FoliageGrass,   false);

            // 4. Distant Real Seoul Skyline LOD (begins seamlessly beyond active high-detail OSM chunk ring!)
            const float skyMinD = (static_cast<float>(settings_.chunkRadius()) + 0.35f) * 256.0f;
            const float skyMaxD = std::max(skyMinD + 900.0f, static_cast<float>(settings_.renderDistanceM) * 7.2f);
            const float skyMinSq = skyMinD * skyMinD;
            const float skyMaxSq = skyMaxD * skyMaxD;
            const std::size_t skyStride = (settings_.buildingDetail <= 0) ? 3u : ((settings_.buildingDetail == 1) ? 2u : 1u);
            for (std::size_t si = 0; si < distantSkyline_.size(); si += skyStride) {
                const auto& ds = distantSkyline_[si];
                const float dSq = (ds.pos - focus).lengthSq();
                if (dSq < skyMinSq || dSq > skyMaxSq) continue;
                drawBox(vk, {ds.pos.x, ds.baseY + ds.height * 0.5f, ds.pos.z},
                        {ds.size.x, ds.height, ds.size.z}, 0.0f, ds.col,
                        ds.isGlass ? 0.22f : 0.78f, ds.isGlass ? 0.35f : 0.04f, 0.0f,
                        ds.isGlass ? MatId::GlassSkyscraper : MatId::KoreanAptFacade, false);
            }
        }
        // 5. High-Detail 5x5 Active OpenStreetMap 3D Chunks (Curved Roads, Sidewalks, Bridge Piers & Real Extruded Buildings!)
        updateAndDrawRealSeoulChunks(vk, city, focus, shadowPass);
        return;
    }

    const float pitch = CityMap::kBlockPitch;

    if (!shadowPass) {
        // 1. Seamless 40km x 34km Seoul Metropolitan Basin Floor (y = -0.52m, safely below Han River -0.18m — ZERO gap!)
        drawBox(vk, {0.0f, -0.78f, 0.0f}, {40000.0f, 0.52f, 34000.0f}, 0.0f,
                {102, 108, 102, 255}, 0.90f, 0.0f, 0.0f, MatId::SidewalkPavers, false);

        // 2. Shimmering 30km Han River S-curve + Stone Revetments + Seokchon Lake + 4 Real Seoul Tributaries!
        drawMesh(vk, hanRiverWaterMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.06f, 0.06f, 0.0f, MatId::HanRiverWater, false);
        drawMesh(vk, hanEmbankmentMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.82f, 0.04f, 0.0f, MatId::Standard, false);
        drawMesh(vk, tributaryWaterMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.06f, 0.06f, 0.0f, MatId::HanRiverWater, false);

        // 2B. Real Curved 30km 강변북로 (Gangbyeonbuk-ro) & 올림픽대로 (Olympic-daero)!
        drawMesh(vk, riversideExpresswayMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.56f, 0.0f, 0.0f, MatId::RoadAsphalt, false);

        // 3. 7 Inner Seoul Mountains + 14 Outer Basin Perimeter Ring Mountains!
        drawMesh(vk, mountainForestMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.90f, 0.0f, 0.0f, MatId::FoliageGrass, false);
        drawMesh(vk, mountainGraniteMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.85f, 0.02f, 0.0f, MatId::GraniteRock, false);

        // 3B. Distant Seoul Metropolitan Skyline Silhouettes (drawn between 520m and 5800m so avenues NEVER end in empty void!)
        for (const auto& ds : distantSkyline_) {
            const float dSq = (ds.pos - focus).lengthSq();
            if (dSq < 520.0f * 520.0f || dSq > 5800.0f * 5800.0f) continue;
            drawBox(vk, {ds.pos.x, ds.baseY + ds.height * 0.5f, ds.pos.z},
                    {ds.size.x, ds.height, ds.size.z}, 0.0f, ds.col,
                    ds.isGlass ? 0.25f : 0.78f, ds.isGlass ? 0.35f : 0.04f, 0.0f,
                    ds.isGlass ? MatId::GlassSkyscraper : MatId::KoreanAptFacade, false);
        }
    }

    // 4. All 15 Real Han River Bridges (Asphalt decks + concrete piers + steel arches/cables!)
    drawMesh(vk, bridgeDecksMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.56f, 0.0f, 0.0f, MatId::RoadAsphalt, shadowPass);
    drawMesh(vk, bridgeConcreteMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.78f, 0.05f, 0.0f, MatId::ModernConcrete, shadowPass);
    drawMesh(vk, bridgeSteelMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.34f, 0.72f, 0.0f, MatId::Standard, shadowPass);

    if (shadowPass) return;

    // 5. Crisp local 84m Korean Road Grid & Concrete Sidewalk Blocks around camera focus!
    // Automatically skips cells submerged in Han River, Tributaries, or inside Major Parks!
    const int centerRx = city.nearestRoadIndex(focus.x);
    const int centerRz = city.nearestRoadIndex(focus.z);
    const int radiusBlocks = 7;

    for (int iz = centerRz - radiusBlocks; iz <= centerRz + radiusBlocks; ++iz) {
        const float rz = city.roadCoord(iz);
        if (std::abs(rz) > 11780.0f) continue;
        for (int ix = centerRx - radiusBlocks; ix <= centerRx + radiusBlocks; ++ix) {
            const float rx = city.roadCoord(ix);
            if (std::abs(rx) > 14290.0f) continue;
            const Vec2 cellCenter{rx + pitch * 0.5f, rz + pitch * 0.5f};

            const Vec2 roadCenter{rx, rz};
            if (!city.isInHanRiverWater(roadCenter, -12.0f) &&
                !city.isInSeokchonLake(roadCenter) &&
                !city.isInTributaryWater(roadCenter, -6.0f)) {
                const float elev = city.terrainElevationAt(roadCenter);
                if (elev < 28.0f) {
                    const bool horizDry = !city.isInHanRiverWater({rx - pitch * 0.48f, rz}, -4.0f) &&
                                          !city.isInHanRiverWater({rx + pitch * 0.48f, rz}, -4.0f) &&
                                          !city.isInTributaryWater({rx - pitch * 0.48f, rz}, -2.0f) &&
                                          !city.isInTributaryWater({rx + pitch * 0.48f, rz}, -2.0f);
                    const bool vertDry  = !city.isInHanRiverWater({rx, rz - pitch * 0.48f}, -4.0f) &&
                                          !city.isInHanRiverWater({rx, rz + pitch * 0.48f}, -4.0f) &&
                                          !city.isInTributaryWater({rx, rz - pitch * 0.48f}, -2.0f) &&
                                          !city.isInTributaryWater({rx, rz + pitch * 0.48f}, -2.0f);
                    if (horizDry) {
                        drawBox(vk, {rx, elev - 0.06f, rz}, {pitch + 1.0f, 0.12f, CityMap::kRoadWidth}, 0.0f,
                                {74, 76, 82, 255}, 0.56f, 0.0f, 0.0f, MatId::RoadAsphalt, false);
                    }
                    if (vertDry) {
                        drawBox(vk, {rx, elev - 0.055f, rz}, {CityMap::kRoadWidth, 0.12f, pitch + 1.0f}, 0.0f,
                                {74, 76, 82, 255}, 0.56f, 0.0f, 0.0f, MatId::RoadAsphalt, false);
                    }
                }
            }

            if (!city.isInHanRiverWater(cellCenter, 8.0f) &&
                !city.isInSeokchonLake(cellCenter) &&
                !city.isInTributaryWater(cellCenter, 4.0f)) {
                const float bElev = city.terrainElevationAt(cellCenter);
                if (bElev < 26.0f) {
                    const float innerSpan = pitch - CityMap::kRoadWidth; // 66m block pad
                    const SeoulParkZone* majPk = city.findMajorSeoulPark(cellCenter);
                    const bool isRiverPark = city.isInHanRiverPark(cellCenter) || (majPk != nullptr);
                    const ColorRGBA padCol = majPk
                        ? majPk->grassColor
                        : (isRiverPark ? ColorRGBA{66, 108, 56, 255} : ColorRGBA{164, 166, 170, 255});
                    const MatId padMat = (isRiverPark && padCol.r < 130) ? MatId::FoliageGrass : MatId::SidewalkPavers;
                    drawBox(vk, {cellCenter.x, bElev + 0.05f, cellCenter.z},
                            {innerSpan, 0.14f, innerSpan}, 0.0f, padCol, 0.84f, 0.0f, 0.0f, padMat, false);
                }
            }
        }
    }
}

void Renderer3D::drawBuildingsAndParks(VulkanContext& vk, const GameWorld& world,
                                       const Vec2& focus, bool shadowPass) {
    const CityMap& city = world.city();
    const bool nightMode = world.isNight();

    // 1. Always-visible 11 Iconic Real Seoul Macro Landmarks!
    drawMesh(vk, landmarkGlassMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.10f, 0.32f, 0.0f, MatId::GlassSkyscraper, shadowPass);
    drawMesh(vk, landmarkStoneMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.72f, 0.06f, 0.0f, MatId::LandmarkStone, shadowPass);
    drawMesh(vk, landmarkMetalMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.28f, 0.78f, 0.0f, MatId::Standard, shadowPass);
    drawMesh(vk, landmarkGiwaMesh_,  Mat4::identity(), {255, 255, 255, 255},
             0.56f, 0.08f, 0.0f, MatId::HanokGiwaRoof, shadowPass);
    if (!shadowPass) {
        drawMesh(vk, landmarkGlowMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.20f, 0.10f, nightMode ? 1.45f : 0.42f, MatId::EmissiveGlow, false);
    }

    if (city.hasRealSeoulMap()) {
        return; // Real OSM 3D buildings & parks are rendered via updateAndDrawRealSeoulChunks + realParksMesh_!
    }

    // 2. Local Park Lawns
    if (!shadowPass) {
        for (const auto& park : city.parks()) {
            const Vec2 c = park.bounds.center();
            if ((c - focus).lengthSq() > 560.0f * 560.0f) continue;
            const Vec2 sz = park.bounds.size();
            const MatId pm = (park.grassColor.r > 130) ? MatId::SidewalkPavers : MatId::FoliageGrass;
            drawBox(vk, {c.x, park.baseY + 0.14f, c.z}, {sz.x, 0.08f, sz.z}, 0.0f,
                    park.grassColor, 0.88f, 0.0f, 0.0f, pm, false);
        }
    }

    // 3. Streamed Seoul Buildings — Orientation-Aware Storefronts, Korean Apt Shear-Walls & Rooftop Detail!
    const float queryRadius = shadowPass ? 165.0f : 580.0f;
    visIdxBuf_.clear();
    city.queryBuildingsInRadius(focus, queryRadius, visIdxBuf_);

    for (int bi : visIdxBuf_) {
        const Building& b = city.buildings()[static_cast<std::size_t>(bi)];
        if (b.isMacroLandmark) continue;

        const Vec2 c = b.footprint.center();
        const Vec2 sz = b.footprint.size();
        const float distSq = (c - focus).lengthSq();

        MatId facadeMat = MatId::ModernConcrete;
        float rough = 0.60f, metal = 0.10f;
        switch (b.style) {
            case BuildingStyle::GlassTower:
            case BuildingStyle::SupertallSpire:
            case BuildingStyle::DealershipBuilding:
                facadeMat = MatId::GlassSkyscraper;
                rough = 0.12f; metal = 0.42f;
                break;
            case BuildingStyle::KoreanApartment:
                facadeMat = MatId::KoreanAptFacade; // South-facing verandas + windowless E/W concrete shear-walls!
                rough = 0.78f; metal = 0.03f;
                break;
            case BuildingStyle::BrickCommercial:
            case BuildingStyle::ShopBuilding:
            case BuildingStyle::BoutiqueBuilding:
            case BuildingStyle::VillaHouse:
                facadeMat = MatId::KoreanApartmentBrick; // Authentic running-bond brick + granite 1F plinth!
                rough = 0.80f; metal = 0.04f;
                break;
            case BuildingStyle::HanokPalace:
                facadeMat = MatId::ModernConcrete;
                rough = 0.78f; metal = 0.04f;
                break;
            default:
                facadeMat = MatId::ModernConcrete; // Honed Korean granite panel cladding!
                rough = 0.54f; metal = 0.12f;
                break;
        }

        // Main tower shaft
        drawBox(vk, {c.x, b.baseY + b.height * 0.5f, c.z}, {sz.x, b.height, sz.z}, 0.0f,
                b.primaryColor, rough, metal, 0.0f, facadeMat, shadowPass);

        // Stepped upper architectural setback tier
        if (b.upperTierHeight > 2.0f) {
            drawBox(vk, {c.x, b.baseY + b.height + b.upperTierHeight * 0.5f, c.z},
                    {sz.x * 0.68f, b.upperTierHeight, sz.z * 0.68f}, 0.0f,
                    b.accentColor, rough, metal, 0.0f, facadeMat, shadowPass);
        }

        const float roofY = b.baseY + b.height + b.upperTierHeight;

        if (b.style == BuildingStyle::HanokPalace) {
            Mat4 rm = Mat4::translation(c.x, roofY - 0.1f, c.z) *
                      Mat4::scale(sz.x * 1.15f, std::min(5.2f, b.height * 0.45f), sz.z * 1.15f);
            drawMesh(vk, giwaRoofMesh_, rm, {255, 255, 255, 255}, 0.56f, 0.08f, 0.0f,
                     MatId::HanokGiwaRoof, shadowPass);
        } else if (b.style == BuildingStyle::VillaHouse) {
            // Korean 4~5F Multi-Family Villa: iconic Korean green urethane waterproof roof + parapet + rooftop stair penthouse (옥탑방) + water tank!
            drawBox(vk, {c.x, roofY + 0.16f, c.z}, {sz.x - 0.35f, 0.24f, sz.z - 0.35f}, 0.0f,
                    {62, 122, 86, 255}, 0.52f, 0.04f, 0.0f, MatId::Standard, shadowPass);
            drawBox(vk, {c.x, roofY + 0.42f, c.z}, {sz.x + 0.25f, 0.68f, sz.z + 0.25f}, 0.0f,
                    {192, 188, 182, 255}, 0.78f, 0.04f, 0.0f, MatId::Standard, shadowPass);
            if (!shadowPass && distSq < 220.0f * 220.0f) {
                drawBox(vk, {c.x + sz.x * 0.18f, roofY + 1.8f, c.z - sz.z * 0.12f}, {3.6f, 2.4f, 3.2f}, 0.0f,
                        b.primaryColor, 0.78f, 0.04f, 0.0f, MatId::Standard, false);
                // Classic Korean stainless rooftop water tank (옥상 물탱크)
                drawMesh(vk, cylMesh_, Mat4::translation(c.x - sz.x * 0.20f, roofY + 0.35f, c.z + sz.z * 0.16f) *
                         Mat4::scale(1.45f, 1.8f, 1.45f), {196, 204, 212, 255}, 0.26f, 0.82f, 0.0f, MatId::Standard, false);
            }
        } else if (!shadowPass && distSq < 260.0f * 260.0f) {
            // Crisp rooftop parapet coping wall + realistic Seoul elevator penthouse & HVAC units!
            drawBox(vk, {c.x, roofY + 0.42f, c.z}, {sz.x + 0.25f, 0.85f, sz.z + 0.25f}, 0.0f,
                    b.accentColor, 0.66f, 0.12f, 0.0f, MatId::Standard, false);

            if (b.style == BuildingStyle::KoreanApartment) {
                // Iconic Korean apartment twin rooftop elevator cores + angled brand crown fin + side dong-number badge!
                drawBox(vk, {c.x - sz.x * 0.24f, roofY + 2.1f, c.z}, {6.4f, 3.6f, sz.z * 0.44f}, 0.0f,
                        b.accentColor, 0.72f, 0.06f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x + sz.x * 0.24f, roofY + 2.1f, c.z}, {6.4f, 3.6f, sz.z * 0.44f}, 0.0f,
                        b.accentColor, 0.72f, 0.06f, 0.0f, MatId::Standard, false);
                // Signature Korean luxury apartment rooftop pergola / crown frame (래미안·자이·아크로 스카이라인!)
                drawBox(vk, {c.x, roofY + 4.1f, c.z}, {sz.x * 0.74f, 0.65f, sz.z * 0.56f}, 0.0f,
                        b.accentColor, 0.52f, 0.28f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x, roofY - 1.1f, c.z}, {sz.x + 0.16f, 1.4f, sz.z + 0.16f}, 0.0f,
                        b.accentColor, 0.74f, 0.06f, 0.0f, MatId::Standard, false);
                // Distinctive side shear-wall dong-number architectural accent panel (아파트 동 번호 판넬 — 101동 / 105동!)
                for (float sx : {-1.0f, 1.0f}) {
                    drawBox(vk, {c.x + sx * (sz.x * 0.5f + 0.12f), roofY - 5.2f, c.z},
                            {0.20f, 3.8f, sz.z * 0.46f}, 0.0f, b.accentColor, 0.48f, 0.12f,
                            0.0f, MatId::Standard, false);
                    if (distSq < 185.0f * 185.0f) {
                        drawSignQuad(vk, {c.x + sx * (sz.x * 0.5f + 0.24f), roofY - 5.2f, c.z},
                                     {sx, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                     std::min(sz.z * 0.44f, 6.2f), 3.45f,
                                     57, nightMode ? 0.34f : 0.04f);
                    }
                }
            } else {
                // Rooftop stairwell penthouse + silver HVAC chiller units
                drawBox(vk, {c.x - sz.x * 0.18f, roofY + 1.6f, c.z - sz.z * 0.16f},
                        {std::min(7.5f, sz.x * 0.34f), 2.8f, std::min(6.5f, sz.z * 0.34f)}, 0.0f,
                        {158, 162, 168, 255}, 0.74f, 0.10f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x + sz.x * 0.20f, roofY + 1.15f, c.z + sz.z * 0.14f},
                        {2.4f, 1.4f, 1.8f}, 0.0f, {178, 184, 192, 255}, 0.46f, 0.52f, 0.0f, MatId::Standard, false);
                if (b.hasHelipad) {
                    drawBox(vk, {c.x, roofY + 0.88f, c.z}, {sz.x * 0.46f, 0.14f, sz.z * 0.46f}, 0.0f,
                            {58, 62, 70, 255}, 0.80f, 0.05f, 0.0f, MatId::Standard, false);
                    drawBox(vk, {c.x, roofY + 0.97f, c.z}, {sz.x * 0.24f, 0.04f, 0.75f}, 0.0f,
                            {242, 206, 56, 255}, 0.40f, 0.0f, nightMode ? 0.6f : 0.0f, MatId::EmissiveGlow, false);
                }
            }

            // ORIENTATION-AWARE MULTI-BAY KOREAN 1F~3F STOREFRONTS WITH REAL HANGUL SIGNBOARDS!
            // ONLY placed on facade edges actually bordering a street (b.streetEdgeMask)!
            if (b.hasStorefront && distSq < 210.0f * 210.0f) {
                const float signY = b.baseY + 4.10f;
                const float canopyY = b.baseY + 3.28f;
                const ColorRGBA canopyCol{48, 54, 64, 255};
                const float signEm = nightMode ? 0.55f : 0.05f;
                const float hangulEm = nightMode ? 0.46f : 0.06f;
                const MatId signMat = nightMode ? MatId::EmissiveGlow : MatId::Standard;
                static constexpr ColorRGBA kAltShopSigns[8] = {
                    { 24, 134,  92, 255}, // Pharmacy / Mart Mint Green
                    { 32,  96, 186, 255}, // Convenience Store / Bank Blue
                    {196, 132,  36, 255}, // Korean Cafe Warm Ochre
                    {102,  54, 152, 255}, // Convenience Store Purple
                    {188,  52,  46, 255}, // Korean Diner Crimson
                    { 36, 118, 128, 255}, // Clinic / Academy Teal
                    { 46,  52,  66, 255}, // Modern Bakery Charcoal
                    {212,  92,  32, 255}  // Chicken / Street Food Orange
                };
                const ColorRGBA shopB = kAltShopSigns[(b.facadeVariant + 3u) & 7u];
                const ColorRGBA shop2F = kAltShopSigns[(b.facadeVariant + 5u) & 7u];
                const bool drawTextSigns = (distSq < 150.0f * 150.0f);
                const int seedIdx = std::abs(static_cast<int>(c.x * 0.13f + c.z * 0.19f)) + static_cast<int>(b.facadeVariant);

                auto drawStreetEdgeShops = [&](float edgeX, float edgeZ, float span, bool alongZ, float outSign, int edgeOrd) {
                    const float halfUnit = span * 0.40f;
                    const float signW1 = std::min(halfUnit * 0.92f, 4.65f);
                    const float signH1 = 1.24f;
                    const float off = span * 0.22f;
                    const float ox1 = alongZ ? (edgeX + outSign * 0.18f) : (edgeX - off);
                    const float oz1 = alongZ ? (edgeZ - off) : (edgeZ + outSign * 0.18f);
                    const float ox2 = alongZ ? (edgeX + outSign * 0.18f) : (edgeX + off);
                    const float oz2 = alongZ ? (edgeZ + off) : (edgeZ + outSign * 0.18f);
                    const Vec3 s1 = alongZ ? Vec3{0.34f, signH1, signW1} : Vec3{signW1, signH1, 0.34f};
                    const Vec3 sCan = alongZ ? Vec3{1.05f, 0.12f, span * 0.78f} : Vec3{span * 0.78f, 0.12f, 1.05f};
                    // 2 distinct neighboring 1F Korean shop fascia signbox housings!
                    drawBox(vk, {ox1, signY, oz1}, s1, 0.0f, b.awningColor, 0.38f, 0.12f, signEm * 0.35f, MatId::Standard, false);
                    drawBox(vk, {ox2, signY, oz2}, s1, 0.0f, shopB,         0.38f, 0.12f, signEm * 0.35f, MatId::Standard, false);
                    if (drawTextSigns) {
                        const Vec3 outN = alongZ ? Vec3{outSign, 0.0f, 0.0f} : Vec3{0.0f, 0.0f, outSign};
                        const float facePush = 0.20f;
                        const int slot1 = 16 + ((seedIdx + edgeOrd * 2) % 32);
                        const int slot2 = 16 + ((seedIdx + edgeOrd * 2 + 11) % 32);
                        drawSignQuad(vk, {ox1 + outN.x * facePush, signY, oz1 + outN.z * facePush},
                                     outN, {0.0f, 1.0f, 0.0f}, signW1 * 0.96f, signH1 * 0.94f, slot1, hangulEm);
                        drawSignQuad(vk, {ox2 + outN.x * facePush, signY, oz2 + outN.z * facePush},
                                     outN, {0.0f, 1.0f, 0.0f}, signW1 * 0.96f, signH1 * 0.94f, slot2, hangulEm);
                    }
                    // Metallic rain canopy above 1F storefronts
                    drawBox(vk, {alongZ ? (edgeX + outSign * 0.55f) : edgeX, canopyY, alongZ ? edgeZ : (edgeZ + outSign * 0.55f)},
                            sCan, 0.0f, canopyCol, 0.34f, 0.55f, 0.0f, MatId::Standard, false);
                    // 2F horizontal Korean academy/clinic fascia + vertical projecting signbox (돌출간판)!
                    if (b.height > 11.0f) {
                        const float signW2 = std::min(halfUnit * 0.86f, 4.25f);
                        const float signH2 = 1.12f;
                        const Vec3 s2F = alongZ ? Vec3{0.28f, signH2, signW2} : Vec3{signW2, signH2, 0.28f};
                        drawBox(vk, {ox2, b.baseY + 7.45f, oz2}, s2F, 0.0f, shop2F, 0.36f, 0.10f, signEm * 0.35f, MatId::Standard, false);
                        if (drawTextSigns) {
                            const Vec3 outN = alongZ ? Vec3{outSign, 0.0f, 0.0f} : Vec3{0.0f, 0.0f, outSign};
                            const int slot2F = 16 + ((seedIdx + edgeOrd + 19) % 32);
                            drawSignQuad(vk, {ox2 + outN.x * 0.175f, b.baseY + 7.45f, oz2 + outN.z * 0.175f},
                                         outN, {0.0f, 1.0f, 0.0f}, signW2 * 0.96f, signH2 * 0.94f, slot2F, hangulEm);
                        }
                        const float px = alongZ ? (edgeX + outSign * 0.58f) : (edgeX + off * 1.45f);
                        const float pz = alongZ ? (edgeZ + off * 1.45f) : (edgeZ + outSign * 0.58f);
                        const Vec3 sProj = alongZ ? Vec3{0.92f, 3.8f, 0.26f} : Vec3{0.26f, 3.8f, 0.92f};
                        drawBox(vk, {px, b.baseY + 8.1f, pz}, sProj, 0.0f,
                                (b.facadeVariant & 1) ? shopB : b.awningColor,
                                0.30f, 0.12f, nightMode ? 0.95f : 0.08f, signMat, false);
                    }
                };

                if (b.streetEdgeMask & 0x01u) drawStreetEdgeShops(b.footprint.min.x, c.z, sz.z, true,  -1.0f, 0);
                if (b.streetEdgeMask & 0x02u) drawStreetEdgeShops(b.footprint.max.x, c.z, sz.z, true,   1.0f, 1);
                if (b.streetEdgeMask & 0x04u) drawStreetEdgeShops(c.x, b.footprint.min.z, sz.x, false, -1.0f, 2);
                if (b.streetEdgeMask & 0x08u) drawStreetEdgeShops(c.x, b.footprint.max.z, sz.x, false,  1.0f, 3);
            }

            if (b.style == BuildingStyle::NightclubBuilding || b.style == BuildingStyle::DealershipBuilding) {
                const ColorRGBA neonC = (b.style == BuildingStyle::NightclubBuilding)
                    ? ColorRGBA{236, 56, 198, 255}
                    : ColorRGBA{64, 212, 255, 255};
                drawBox(vk, {c.x, b.baseY + 4.8f, c.z}, {sz.x + 0.35f, 0.34f, sz.z + 0.35f}, 0.0f,
                        neonC, 0.20f, 0.0f, nightMode ? 1.8f : 0.45f, MatId::EmissiveGlow, false);
            }
        }
    }

    // 4. Gas Station Canopies
    for (const auto& can : city.canopies()) {
        const Vec2 c = can.bounds.center();
        if ((c - focus).lengthSq() > queryRadius * queryRadius) continue;
        const Vec2 sz = can.bounds.size();
        drawBox(vk, {c.x, can.baseY + can.clearanceHeight, c.z}, {sz.x, 0.85f, sz.z}, 0.0f,
                {236, 238, 244, 255}, 0.35f, 0.25f, 0.0f, MatId::Standard, shadowPass);
        drawBox(vk, {c.x, can.baseY + can.clearanceHeight + 0.46f, c.z}, {sz.x + 0.2f, 0.28f, sz.z + 0.2f}, 0.0f,
                {24, 118, 224, 255}, 0.28f, 0.1f, nightMode ? 0.9f : 0.15f, MatId::EmissiveGlow, false);
        for (float sx : {-1.0f, 1.0f}) {
            for (float szS : {-1.0f, 1.0f}) {
                drawBox(vk, {c.x + sx * sz.x * 0.34f, can.baseY + can.clearanceHeight * 0.5f, c.z + szS * sz.z * 0.32f},
                        {0.55f, can.clearanceHeight, 0.55f}, 0.0f, {212, 216, 224, 255}, 0.42f, 0.4f, 0.0f, MatId::Standard, shadowPass);
            }
        }
    }
}

void Renderer3D::drawTreesAndProps(VulkanContext& vk, const GameWorld& world,
                                   const Vec2& focus, const Vec3& camPos, bool shadowPass) {
    const CityMap& city = world.city();
    const bool nightMode = world.isNight();
    static constexpr float kTreeRadii[4] = {135.0f, 200.0f, 280.0f, 360.0f};
    static constexpr float kPropRadii[4] = {115.0f, 175.0f, 235.0f, 315.0f};
    static constexpr float kSignRadii[4] = { 95.0f, 135.0f, 180.0f, 235.0f};
    const int pd = std::clamp(settings_.propDensity, 0, 3);
    const int bd = std::clamp(settings_.buildingDetail, 0, 3);
    const float treeRadius = shadowPass ? std::min(settings_.shadowSpanM() * 0.85f, kTreeRadii[pd]) : kTreeRadii[pd];
    const float propRadius = shadowPass ? std::min(settings_.shadowSpanM() * 0.78f, kPropRadii[pd]) : kPropRadii[pd];
    const float signRadiusSq = kSignRadii[bd] * kSignRadii[bd];
    const ColorRGBA kWhite{255, 255, 255, 255};

    // 1. Seoul Street Trees (Organic multi-cluster Ginkgo, Cherry Blossom, Korean Pine + Architectural Cast-Iron Grates!)
    visIdxBuf_.clear();
    city.queryTreesInRadius(focus, treeRadius, visIdxBuf_);
    for (int ti : visIdxBuf_) {
        if (pd == 0 && (ti & 1) != 0) continue; // Low prop density trims 50% of distant foliage for max FPS
        const TreeProp& t = city.trees()[static_cast<std::size_t>(ti)];
        const float y0 = t.baseY;

        if (t.inPlanter && !shadowPass) {
            // Road-aligned Seoul sidewalk flush Pocheon granite tree-well frame + perforated cast-iron grate (1.52x1.52m)!
            Mat4 grateM = Mat4::translation(t.pos.x, y0, t.pos.z) *
                          Mat4::rotationAxis({0, 1, 0}, -t.yaw);
            drawMesh(vk, treeGrateMesh_, grateM, kWhite, 0.56f, 0.42f, 0.0f, MatId::Standard, false);
        }
        // Multi-branch flared bark trunk with natural yaw variation per tree
        const float treeYaw = std::fmod(std::abs(t.pos.x * 0.17f + t.pos.z * 0.31f), kTwoPi);
        Mat4 trunkM = Mat4::translation(t.pos.x, y0, t.pos.z) *
                      Mat4::rotationAxis({0, 1, 0}, treeYaw) *
                      Mat4::scale(0.88f, t.trunkHeight, 0.88f);
        drawMesh(vk, treeTrunkMesh_, trunkM, {104, 78, 56, 255}, 0.90f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);

        if (t.species == TreeSpecies::KoreanPine) {
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight * 0.68f, t.pos.z) *
                      Mat4::rotationAxis({0, 1, 0}, treeYaw) *
                      Mat4::scale(t.crownRadius * 1.38f, t.crownRadius * 1.85f, t.crownRadius * 1.38f);
            drawMesh(vk, pineMesh_, cm, {48, 92, 54, 255}, 0.86f, 0.0f, 0.0f, MatId::FoliageCanopy, shadowPass);
        } else {
            static constexpr ColorRGBA kSeoulLeafPalette[4] = {
                {108, 152, 66, 255}, // Sunlit Zelkova spring green
                {124, 162, 68, 255}, // Warm sunlit Seoul plane tree
                { 96, 144, 72, 255}, // Fresh deep boulevard emerald
                {138, 164, 62, 255}  // Subtle golden-green ginkgo
            };
            const unsigned treeHash = static_cast<unsigned>(
                std::abs(static_cast<int>(t.pos.x * 3.0f) ^ static_cast<int>(t.pos.z * 7.0f)));
            const ColorRGBA crownCol = (t.species == TreeSpecies::CherryBlossom)
                ? ColorRGBA{244, 192, 210, 255}
                : kSeoulLeafPalette[treeHash & 3u];
            const float crownHScale = 0.96f + static_cast<float>((treeHash >> 2u) & 3u) * 0.05f;
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight + t.crownRadius * 0.12f, t.pos.z) *
                      Mat4::rotationAxis({0, 1, 0}, treeYaw) *
                      Mat4::scale(t.crownRadius * 1.18f, t.crownRadius * crownHScale, t.crownRadius * 1.18f);
            drawMesh(vk, foliageMesh_, cm, crownCol, 0.84f, 0.0f, 0.0f, MatId::FoliageCanopy, shadowPass);
        }
    }

    // 2. High-Detail Sculpted Korean Street Architecture & Furniture (v6.4.0)
    const int sigPhase = static_cast<int>(std::fmod(world.elapsedTime(), 18.0f));
    visIdxBuf_.clear();
    city.queryPropsInRadius(focus, propRadius, visIdxBuf_);
    for (int pi : visIdxBuf_) {
        const StreetProp& p = city.streetProps()[static_cast<std::size_t>(pi)];
        if (pd == 0 && (p.kind == StreetPropKind::Bench || p.kind == StreetPropKind::TrashBin ||
                        p.kind == StreetPropKind::Bollard || p.kind == StreetPropKind::CafeTable) &&
            (pi & 1) != 0) {
            continue;
        }
        const float propBaseY = (p.baseY > -1e8f) ? p.baseY : city.surfaceHeightAt(p.pos);
        const float gy = propBaseY + p.posY;
        const float distSq = (Vec3{p.pos.x, gy, p.pos.z} - camPos).lengthSq();
        if (!shadowPass && distSq > propRadius * propRadius) continue;

        Mat4 baseM = Mat4::translation(p.pos.x, gy, p.pos.z) *
                     Mat4::rotationAxis({0, 1, 0}, -p.yaw);
        if (p.broken) {
            baseM = baseM * Mat4::rotationAxis({1, 0, 0}, p.roll) * Mat4::rotationAxis({0, 0, 1}, p.pitch);
        }

        auto drawLocalSign = [&](const Vec3& locCenter, const Vec3& locNormal, const Vec3& locUp,
                                 float w, float h, int slot, float em) {
            const Vec3 wc = baseM.transformPoint(locCenter);
            const Vec3 wn = (baseM.transformPoint(locCenter + locNormal) - wc).normalized();
            const Vec3 wu = (baseM.transformPoint(locCenter + locUp) - wc).normalized();
            drawSignQuad(vk, wc, wn, wu, w, h, slot, em);
        };

        switch (p.kind) {
            case StreetPropKind::StreetLight: {
                if (p.variant == 3u) {
                    // Sculpted 10.5m Korean Expressway Galvanized Silver Upswept Cobra Lamp
                    drawMesh(vk, highwayLampMesh_, baseM, kWhite, 0.30f, 0.78f, 0.0f, MatId::Standard, shadowPass);
                    if (!shadowPass) {
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(2.95f, 10.39f, 0.0f) * Mat4::scale(0.80f, 0.055f, 0.28f),
                                 {246, 248, 255, 255}, 0.14f, 0.0f, nightMode ? 3.0f : 0.22f, MatId::EmissiveGlow, false);
                    }
                    break;
                }
                if (p.variant == 4u) {
                    // Iconic 8.6m Seoul Twin-Arm Boulevard Central-Median LED Streetlamp (양팔형 대로 가로등!)
                    drawMesh(vk, seoulDoubleLampMesh_, baseM, kWhite, 0.34f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                    if (!shadowPass) {
                        for (float dir : {-1.0f, 1.0f}) {
                            drawMesh(vk, cubeMesh_, baseM * Mat4::translation(dir * 2.30f, 8.40f, 0.0f) * Mat4::scale(0.66f, 0.052f, 0.24f),
                                     {255, 244, 218, 255}, 0.14f, 0.0f, nightMode ? 2.75f : 0.22f, MatId::EmissiveGlow, false);
                        }
                    }
                    break;
                }
                // Sculpted 8.2m Seoul Public-Design Single-Arm LED Streetlamp (기와진회색 + 관리번호표!)
                drawMesh(vk, seoulLampMesh_, baseM, kWhite, 0.34f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(2.38f, 8.04f, 0.0f) * Mat4::scale(0.66f, 0.052f, 0.24f),
                             {255, 244, 218, 255}, 0.14f, 0.0f, nightMode ? 2.7f : 0.22f, MatId::EmissiveGlow, false);
                    // Signature Seoul boulevard vertical district street banner with brushed stainless mounts!
                    if (p.variant == 2u && distSq < 155.0f * 155.0f) {
                        const ColorRGBA kBanners[3] = {{34, 96, 178, 255}, {28, 134, 118, 255}, {182, 68, 72, 255}};
                        const ColorRGBA bc = kBanners[static_cast<unsigned>(std::abs(static_cast<int>(p.pos.x))) % 3u];
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-0.42f, 5.25f, 0.0f) * Mat4::scale(0.76f, 0.045f, 0.045f),
                                 {178, 184, 192, 255}, 0.30f, 0.78f, 0.0f, MatId::Standard, false);
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-0.42f, 3.95f, 0.0f) * Mat4::scale(0.76f, 0.045f, 0.045f),
                                 {178, 184, 192, 255}, 0.30f, 0.78f, 0.0f, MatId::Standard, false);
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-0.44f, 4.60f, 0.0f) * Mat4::scale(0.64f, 1.24f, 0.025f),
                                 bc, 0.78f, 0.0f, 0.0f, MatId::FabricCloth, false);
                    }
                }
                break;
            }
            case StreetPropKind::TrafficLight: {
                // Sculpted Korean 4-Aspect Horizontal Cantilever Traffic Signal + White Backplate + Ped Signal + Countdown!
                drawMesh(vk, koreanSignalMesh_, baseM, kWhite, 0.36f, 0.62f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    const bool controlsNsRoad = (std::abs(std::cos(p.yaw)) >= std::abs(std::sin(p.yaw)));
                    const int localPhase = controlsNsRoad ? sigPhase : ((sigPhase + 9) % 18);
                    const bool isGreen     = (localPhase < 6);
                    const bool isLeftArrow = (localPhase >= 5 && localPhase < 7);
                    const bool isAmber     = (localPhase >= 7 && localPhase < 9);
                    const bool isRed       = (localPhase >= 9);

                    // 4 Korean horizontal signal lenses inside sculpted hoods: Red | Amber | Left-Arrow | Green
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(2.85f, 5.64f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.36f),
                             isRed ? ColorRGBA{255, 48, 48, 255} : ColorRGBA{52, 18, 18, 255},
                             0.14f, 0.0f, isRed ? 2.6f : 0.0f, isRed ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(3.35f, 5.64f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.36f),
                             isAmber ? ColorRGBA{255, 198, 36, 255} : ColorRGBA{54, 44, 14, 255},
                             0.14f, 0.0f, isAmber ? 2.4f : 0.0f, isAmber ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(3.85f, 5.64f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.36f),
                             isLeftArrow ? ColorRGBA{38, 245, 138, 255} : ColorRGBA{16, 46, 30, 255},
                             0.14f, 0.0f, isLeftArrow ? 2.5f : 0.0f, isLeftArrow ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(4.35f, 5.64f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.36f),
                             isGreen ? ColorRGBA{42, 242, 118, 255} : ColorRGBA{16, 48, 28, 255},
                             0.14f, 0.0f, isGreen ? 2.6f : 0.0f, isGreen ? MatId::EmissiveGlow : MatId::Standard, false);
                    // Pole-mounted Korean Pedestrian Walk / Don't Walk LED panels + green triangular countdown bars!
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 3.34f, 0.365f) * Mat4::scale(0.24f, 0.24f, 0.03f),
                             !isRed ? ColorRGBA{255, 58, 48, 255} : ColorRGBA{48, 16, 16, 255},
                             0.15f, 0.0f, !isRed ? 2.1f : 0.0f, !isRed ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.98f, 0.365f) * Mat4::scale(0.24f, 0.24f, 0.03f),
                             isRed ? ColorRGBA{46, 242, 124, 255} : ColorRGBA{16, 46, 28, 255},
                             0.15f, 0.0f, isRed ? 2.1f : 0.0f, isRed ? MatId::EmissiveGlow : MatId::Standard, false);
                    if (isRed) {
                        const int remBars = std::clamp(18 - localPhase, 1, 6);
                        for (int bi = 0; bi < 6; ++bi) {
                            const bool lit = (bi < remBars);
                            const float by = 2.94f + static_cast<float>(bi) * 0.085f;
                            const float bw = 0.08f + static_cast<float>(bi) * 0.028f;
                            drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.36f, by, 0.355f) * Mat4::scale(bw, 0.048f, 0.02f),
                                     lit ? ColorRGBA{46, 242, 124, 255} : ColorRGBA{18, 34, 24, 255},
                                     0.16f, 0.0f, lit ? 2.0f : 0.0f, lit ? MatId::EmissiveGlow : MatId::Standard, false);
                        }
                    }
                }
                break;
            }
            case StreetPropKind::BusShelter: {
                // Sculpted Seoul Design Bus Shelter Frame/Bench/BIT (translucent tempered glass drawn in Alpha pass!)
                drawMesh(vk, busShelterFrameMesh_, baseM, kWhite, 0.38f, 0.56f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    // Slim recessed white under-canopy LED strip illuminating waiting passengers!
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.35f, 2.62f, 0.10f) * Mat4::scale(3.60f, 0.035f, 0.14f),
                             {246, 250, 255, 255}, 0.16f, 0.0f, nightMode ? 1.85f : 0.14f, MatId::EmissiveGlow, false);
                    if (nightMode) {
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-1.71f, 1.34f, -0.20f) * Mat4::scale(0.02f, 1.90f, 0.92f),
                                 {246, 242, 232, 255}, 0.22f, 0.0f, 0.85f, MatId::EmissiveGlow, false);
                    }
                    if (distSq < signRadiusSq) {
                        drawLocalSign({2.72f, 1.96f, 0.290f}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f},
                                      0.54f, 0.44f, 56, nightMode ? 0.72f : 0.30f);
                        drawLocalSign({2.72f, 1.96f, 0.035f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f},
                                      0.54f, 0.44f, 56, nightMode ? 0.72f : 0.30f);
                    }
                }
                break;
            }
            case StreetPropKind::SubwayEntrance: {
                // Sculpted Seoul Metro Vaulted Glass-Steel Canopy Frame + Granite Stairs + Handrails (glass drawn in Alpha pass!)
                drawMesh(vk, subwayCanopyFrameMesh_, baseM, kWhite, 0.42f, 0.54f, 0.0f, MatId::Standard, shadowPass);
                static constexpr ColorRGBA kMetroLines[8] = {
                    { 42, 168,  78, 255}, // 48: Line 2 Green (강남역)
                    { 42, 168,  78, 255}, // 49: Line 2 Green (삼성역)
                    { 42, 168,  78, 255}, // 50: Line 2 Green (잠실역)
                    {192, 162,  66, 255}, // 51: Line 9 Gold  (고속터미널)
                    {236, 118,  28, 255}, // 52: Line 3 Orange (경복궁역)
                    { 36,  98, 198, 255}, // 53: Line 1 Navy  (시청역)
                    {146,  72, 178, 255}, // 54: Line 5 Purple (여의도역)
                    { 42, 168,  78, 255}  // 55: Line 2 Green (홍대입구역)
                };
                unsigned metroIdx = 0u;
                ColorRGBA lineCol = kMetroLines[0];
                bool hasExactStationSign = true;
                switch (city.districtAtWorld(p.pos)) {
                    case DistrictType::GangnamTeheran:     metroIdx = (p.pos.x > 4700.0f) ? 1u : 0u; lineCol = kMetroLines[metroIdx]; break;
                    case DistrictType::JamsilSongpa:       metroIdx = 2u; lineCol = kMetroLines[2]; break;
                    case DistrictType::SeochoBanpo:        metroIdx = 3u; lineCol = kMetroLines[3]; break;
                    case DistrictType::GwanghwamunPalace:  metroIdx = 4u; lineCol = kMetroLines[4]; break;
                    case DistrictType::MyeongdongCityHall:
                    case DistrictType::JongnoCheonggye:    metroIdx = 5u; lineCol = kMetroLines[5]; break;
                    case DistrictType::YeouidoFinance:     metroIdx = 6u; lineCol = kMetroLines[6]; break;
                    case DistrictType::HongdaeMapo:        metroIdx = 7u; lineCol = kMetroLines[7]; break;
                    case DistrictType::NamsanItaewon:      lineCol = {178,  92,  26, 255}; hasExactStationSign = false; break; // Line 6 Ochre-Brown (이태원·녹사평)
                    case DistrictType::Dongdaemun:         lineCol = { 32, 162, 224, 255}; hasExactStationSign = false; break; // Line 4 Sky-Blue (동대문역사문화공원)
                    case DistrictType::SeongsuKonkuk:      lineCol = { 42, 168,  78, 255}; hasExactStationSign = false; break; // Line 2 Green (성수·건대입구)
                    case DistrictType::Yongsan:            lineCol = { 36,  98, 198, 255}; hasExactStationSign = false; break; // Line 1 Navy (용산·남영)
                    case DistrictType::GangbukNowon:       lineCol = { 32, 162, 224, 255}; hasExactStationSign = false; break; // Line 4 Sky-Blue
                    case DistrictType::GangseoMagok:       lineCol = {192, 162,  66, 255}; hasExactStationSign = false; break; // Line 9 Gold
                    case DistrictType::GuroGwanak:
                    default:                               lineCol = { 42, 168,  78, 255}; hasExactStationSign = false; break; // Line 2 Green
                }
                // Illuminated Seoul Metro line header fascia flanking the vaulted glass canopy
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.58f,  1.38f) * Mat4::scale(5.58f, 0.44f, 0.12f),
                         lineCol, 0.24f, 0.16f, nightMode ? 0.45f : 0.06f, MatId::Standard, false);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.58f, -1.38f) * Mat4::scale(5.58f, 0.44f, 0.12f),
                         lineCol, 0.24f, 0.16f, nightMode ? 0.45f : 0.06f, MatId::Standard, false);
                if (!shadowPass) {
                    if (hasExactStationSign && distSq < signRadiusSq) {
                        const int metroSlot = 48 + static_cast<int>(metroIdx);
                        const float mEm = nightMode ? 0.58f : 0.10f;
                        drawLocalSign({0.0f, 2.58f,  1.46f}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, 3.6f, 0.68f, metroSlot, mEm);
                        drawLocalSign({0.0f, 2.58f, -1.46f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 3.6f, 0.68f, metroSlot, mEm);
                    } else {
                        // Crisp white Seoul Metro fascia stripe band when using district line color
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.58f,  1.45f) * Mat4::scale(4.6f, 0.14f, 0.03f),
                                 {240, 244, 248, 255}, 0.28f, 0.08f, nightMode ? 0.42f : 0.06f, MatId::Standard, false);
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.58f, -1.45f) * Mat4::scale(4.6f, 0.14f, 0.03f),
                                 {240, 244, 248, 255}, 0.28f, 0.08f, nightMode ? 0.42f : 0.06f, MatId::Standard, false);
                    }
                    // Iconic circular Seoul Metro Line Exit Pole Sign emblem (출입구 폴사인)!
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(3.28f, 4.50f, 0.0f) * Mat4::scale(0.92f, 0.92f, 0.22f),
                             lineCol, 0.20f, 0.12f, nightMode ? 1.55f : 0.24f,
                             nightMode ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(3.28f, 4.50f, 0.0f) * Mat4::scale(0.42f, 0.42f, 0.26f),
                             {246, 248, 252, 255}, 0.22f, 0.0f, nightMode ? 1.3f : 0.14f,
                             nightMode ? MatId::EmissiveGlow : MatId::Standard, false);
                }
                break;
            }
            case StreetPropKind::RoadGantry: {
                // Distinguish Korean GREEN Expressway Truss Gantries (bit 7 = 0x80) from Korean BLUE City Boulevard Gantries!
                const bool isExpresswayGantry = (p.variant & 0x80u) != 0u;
                const std::uint8_t rawSpan = static_cast<std::uint8_t>(p.variant & 0x7Fu);
                const float spanHalf = (rawSpan >= 60u) ? (static_cast<float>(rawSpan) * 0.1f) : 9.55f;
                const float spanFull = spanHalf * 2.0f + 0.5f;
                const ColorRGBA kSteel = isExpresswayGantry ? ColorRGBA{186, 192, 200, 255} : ColorRGBA{154, 162, 172, 255};
                const float portalH = isExpresswayGantry ? 7.65f : 6.75f;
                const float barTopY = isExpresswayGantry ? 7.36f : 6.48f;
                const float barBotY = isExpresswayGantry ? 6.38f : 5.74f;
                const float signCenterY = isExpresswayGantry ? 6.90f : 6.15f;
                const float signH = isExpresswayGantry ? 2.48f : 2.16f;

                for (float sx : {-spanHalf, spanHalf}) {
                    drawMesh(vk, cylMesh_, baseM * Mat4::translation(sx, 0.0f, 0.0f) *
                             Mat4::scale(isExpresswayGantry ? 0.48f : 0.40f, portalH, isExpresswayGantry ? 0.48f : 0.40f),
                             kSteel, 0.32f, 0.78f, 0.0f, MatId::Standard, shadowPass);
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(sx, 0.46f, 0.0f) * Mat4::scale(0.82f, 0.92f, 0.82f),
                             {156, 160, 166, 255}, 0.80f, 0.04f, 0.0f, MatId::BridgeConcrete, false);
                    if (isExpresswayGantry && !shadowPass) {
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(sx, 1.04f, 0.0f) * Mat4::scale(0.60f, 0.24f, 0.60f),
                                 {246, 194, 34, 255}, 0.36f, 0.08f, 0.12f, MatId::Standard, false);
                    }
                }
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, barTopY, 0.0f) * Mat4::scale(spanFull, 0.26f, 0.18f),
                         kSteel, 0.32f, 0.78f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, barBotY, 0.0f) * Mat4::scale(spanFull - 0.2f, 0.22f, 0.16f),
                         kSteel, 0.32f, 0.78f, 0.0f, MatId::Standard, shadowPass);
                if (isExpresswayGantry) {
                    for (float tx = -spanHalf + 1.8f; tx < spanHalf - 1.2f; tx += 2.6f) {
                        drawMesh(vk, cubeMesh_, baseM * Mat4::translation(tx, 0.5f * (barTopY + barBotY), 0.0f) *
                                 Mat4::scale(0.12f, barTopY - barBotY, 0.12f),
                                 kSteel, 0.34f, 0.76f, 0.0f, MatId::Standard, false);
                    }
                }

                int slotL = 8, slotR = 9;
                if (isExpresswayGantry) {
                    if (p.pos.x > 2100.0f && p.pos.x < 3300.0f && p.pos.z > 600.0f) {
                        slotL = 1; slotR = 7;
                    } else if (p.pos.x > 4600.0f && p.pos.z > 200.0f) {
                        slotL = 4; slotR = 5;
                    } else if (p.pos.z > 150.0f) {
                        slotL = 0; slotR = 6;
                    } else if (p.pos.z > -1600.0f) {
                        slotL = 2; slotR = 3;
                    } else {
                        slotL = 3; slotR = 5;
                    }
                } else {
                    switch (city.districtAtWorld(p.pos)) {
                        case DistrictType::GangnamTeheran:     slotL =  8; slotR = 13; break;
                        case DistrictType::SeochoBanpo:        slotL = 10; slotR = 13; break;
                        case DistrictType::GwanghwamunPalace:
                        case DistrictType::MyeongdongCityHall: slotL =  9; slotR = 14; break;
                        case DistrictType::JongnoCheonggye:    slotL = 14; slotR =  9; break;
                        case DistrictType::YeouidoFinance:     slotL = 12; slotR = 10; break;
                        case DistrictType::HongdaeMapo:
                        case DistrictType::SeongsuKonkuk:      slotL = 15; slotR = 14; break;
                        case DistrictType::JamsilSongpa:       slotL = 11; slotR =  8; break;
                        default:                               slotL =  8; slotR =  9; break;
                    }
                }
                const float gEm = nightMode ? 0.42f : 0.08f;
                const float panelW = std::min(isExpresswayGantry ? 6.8f : 6.0f, spanHalf * 0.80f);
                const float panelOff = std::min(isExpresswayGantry ? 4.1f : 3.7f, spanHalf * 0.45f);
                const float panelXs[2] = {-panelOff, panelOff};
                const int panelSlots[2] = {slotL, slotR};
                const ColorRGBA kGantryHousing = isExpresswayGantry ? ColorRGBA{46, 56, 52, 255} : ColorRGBA{52, 60, 74, 255};
                for (int k = 0; k < 2; ++k) {
                    const float px = panelXs[k];
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(px, signCenterY, 0.0f) * Mat4::scale(panelW, signH, 0.18f),
                             kGantryHousing, 0.38f, 0.65f, 0.0f, MatId::Standard, shadowPass);
                    if (!shadowPass) {
                        drawLocalSign({px, signCenterY,  0.135f}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, panelW - 0.06f, signH - 0.08f, panelSlots[k], gEm);
                        drawLocalSign({px, signCenterY, -0.135f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, panelW - 0.06f, signH - 0.08f, panelSlots[k], gEm);
                    }
                }
                break;
            }
            case StreetPropKind::PedGuardrail: {
                // Sculpted 7.2m Seoul Public-Design 기와진회색 pedestrian safety guardrail!
                drawMesh(vk, pedGuardrailMesh_, baseM, kWhite, 0.34f, 0.70f, 0.0f, MatId::Standard, shadowPass);
                break;
            }
            case StreetPropKind::ShopSign: {
                // Illuminated 3D Korean 1F-2F Commercial Fascia Signboard (32 real brands in slots 16..47) + Storefront Canopy!
                const int signSlot = (p.variant >= 16u && p.variant <= 47u)
                    ? static_cast<int>(p.variant)
                    : (16 + (static_cast<int>(p.variant) % 32));
                const float sEm = nightMode ? 0.65f : 0.10f;
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 3.85f, 0.14f) * Mat4::scale(4.45f, 1.02f, 0.26f),
                         {38, 42, 48, 255}, 0.36f, 0.68f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass && distSq < signRadiusSq) {
                    drawLocalSign({0.0f, 3.85f, 0.295f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 4.30f, 0.90f, signSlot, sEm);
                }
                static constexpr ColorRGBA kAwnCols[4] = {
                    {38, 66, 98, 255}, {44, 86, 68, 255}, {126, 86, 54, 255}, {52, 56, 66, 255}
                };
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 3.12f, 0.56f) * Mat4::scale(4.65f, 0.14f, 1.15f),
                         kAwnCols[signSlot & 3], 0.62f, 0.18f, 0.0f, MatId::Standard, shadowPass);
                static constexpr ColorRGBA kBladeCols[6] = {
                    {48, 92, 168, 255}, {36, 128, 86, 255}, {176, 68, 48, 255},
                    {192, 124, 38, 255}, {76, 56, 138, 255}, {38, 118, 136, 255}
                };
                const ColorRGBA bladeCol = kBladeCols[(signSlot - 16) % 6];
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(2.38f, 4.18f, 0.38f) * Mat4::scale(0.14f, 1.24f, 0.50f),
                         {40, 44, 52, 255}, 0.38f, 0.66f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(2.38f, 4.18f, 0.40f) * Mat4::scale(0.17f, 1.10f, 0.40f),
                         bladeCol, 0.30f, 0.12f, nightMode ? 0.68f : 0.08f,
                         nightMode ? MatId::EmissiveGlow : MatId::Standard, false);
                break;
            }
            case StreetPropKind::UtilityBox: {
                // Sculpted KEPCO sage-olive-gray louvered steel transformer / traffic-signal cabinet on concrete plinth!
                drawMesh(vk, utilityBoxMesh_, baseM, kWhite, 0.46f, 0.44f, 0.0f, MatId::Standard, shadowPass);
                break;
            }
            case StreetPropKind::StreetKiosk: {
                // Sculpted Seoul Public-Design Metallic Silver Street Kiosk (가로판매대; sliding glass drawn in Alpha pass!)
                drawMesh(vk, streetKioskMesh_, baseM, kWhite, 0.38f, 0.58f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass && nightMode) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.12f, 0.18f) * Mat4::scale(2.20f, 0.05f, 0.18f),
                             {255, 236, 196, 255}, 0.20f, 0.0f, 1.45f, MatId::EmissiveGlow, false);
                }
                break;
            }
            case StreetPropKind::DdareungiRack: {
                // Hyper-Detailed Sculpted 따릉이 Seoul Public Bike-Share Station (4 white/green bikes + QR kiosk!)
                drawMesh(vk, ddareungiRackMesh_, baseM, kWhite, 0.36f, 0.48f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-2.22f, 1.30f, -0.312f) * Mat4::scale(0.24f, 0.35f, 0.015f),
                             {156, 246, 204, 255}, 0.18f, 0.0f, nightMode ? 1.35f : 0.30f, MatId::EmissiveGlow, false);
                }
                break;
            }
            case StreetPropKind::CafeTable: {
                // Korean street-side cafe table + canvas sun umbrella + 2 bistro chairs with backrests!
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.46f, 0.05f, 0.46f),
                         {52, 56, 64, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.075f, 2.38f, 0.075f),
                         {64, 68, 76, 255}, 0.34f, 0.74f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cylMesh_, baseM * Mat4::translation(0.0f, 0.74f, 0.0f) * Mat4::scale(0.96f, 0.06f, 0.96f),
                         {166, 122, 78, 255}, 0.62f, 0.05f, 0.0f, MatId::WoodBark, shadowPass);
                const ColorRGBA umbCol = ((p.variant & 1u) == 0u) ? ColorRGBA{232, 226, 212, 255} : ColorRGBA{46, 98, 74, 255};
                drawMesh(vk, coneMesh_, baseM * Mat4::translation(0.0f, 2.15f, 0.0f) * Mat4::scale(2.28f, 0.48f, 2.28f),
                         umbCol, 0.76f, 0.0f, 0.0f, MatId::FabricCloth, shadowPass);
                for (float sx : {-0.78f, 0.78f}) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(sx, 0.44f, 0.0f) * Mat4::scale(0.42f, 0.06f, 0.42f),
                             {156, 114, 74, 255}, 0.70f, 0.05f, 0.0f, MatId::WoodBark, false);
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(sx * 1.24f, 0.68f, 0.0f) * Mat4::scale(0.05f, 0.42f, 0.40f),
                             {58, 62, 70, 255}, 0.42f, 0.68f, 0.0f, MatId::Standard, false);
                    drawMesh(vk, cylMesh_, baseM * Mat4::translation(sx, 0.0f, 0.0f) * Mat4::scale(0.08f, 0.44f, 0.08f),
                             {58, 62, 70, 255}, 0.42f, 0.68f, 0.0f, MatId::Standard, false);
                }
                break;
            }
            case StreetPropKind::MedianBarrier: {
                // Sculpted Seoul Boulevard Central Median Jaywalking-Prevention Fence (무단횡단 방지펜스 + 이중 반사띠!)
                drawMesh(vk, medianFenceMesh_, baseM, kWhite, 0.38f, 0.56f, 0.0f, MatId::Standard, shadowPass);
                break;
            }
            case StreetPropKind::Bench:
                // Sculpted wood-slat & cast-aluminum Seoul Public-Design street bench with armrests!
                drawMesh(vk, benchMesh_, baseM, kWhite, 0.64f, 0.25f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::Hydrant:
                // Sculpted Korean standard red fire hydrant + twin brass outlets + yellow/black protective U-guard!
                drawMesh(vk, hydrantMesh_, baseM, kWhite, 0.36f, 0.46f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::TrafficCone:
                drawMesh(vk, coneMesh_, baseM * Mat4::scale(0.45f, 0.75f, 0.45f),
                         {242, 106, 26, 255}, 0.55f, 0.0f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::TrashBin:
                // Sculpted Seoul Design dual stainless recycling (blue) & general waste (amber) street bin!
                drawMesh(vk, trashBinMesh_, baseM, kWhite, 0.40f, 0.56f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::FuelPump:
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.92f, 0) * Mat4::scale(0.85f, 1.84f, 0.58f),
                         {232, 236, 242, 255}, 0.36f, 0.25f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 1.65f, 0) * Mat4::scale(0.90f, 0.32f, 0.62f),
                         {28, 122, 224, 255}, 0.30f, 0.10f, nightMode ? 0.8f : 0.1f, MatId::EmissiveGlow, false);
                break;
            case StreetPropKind::BeachUmbrella:
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.10f, 2.4f, 0.10f),
                         {196, 202, 212, 255}, 0.35f, 0.65f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, coneMesh_, baseM * Mat4::translation(0, 2.15f, 0) * Mat4::scale(2.8f, 0.62f, 2.8f),
                         {52, 148, 228, 255}, 0.72f, 0.0f, 0.0f, MatId::FabricCloth, shadowPass);
                break;
            case StreetPropKind::Bollard:
                // Sculpted Korean KS Standard 84cm safety bollard with dual white/yellow reflective bands & stainless cap!
                drawMesh(vk, bollardMesh_, baseM, kWhite, 0.32f, 0.62f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::Billboard:
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.45f, 6.2f, 0.45f),
                         {74, 78, 86, 255}, 0.48f, 0.68f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 7.2f, 0) * Mat4::scale(5.4f, 2.6f, 0.28f),
                         {42, 124, 218, 255}, 0.22f, 0.10f, nightMode ? 1.15f : 0.22f, MatId::EmissiveGlow, shadowPass);
                break;
            case StreetPropKind::Crate:
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.55f, 0) * Mat4::scale(1.1f, 1.1f, 1.1f),
                         {152, 114, 74, 255}, 0.82f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);
                break;
        }
    }
}

void Renderer3D::drawVehicle(VulkanContext& vk, const Vehicle& v, float groundY,
                             bool nightMode, bool shadowPass, bool alphaPass,
                             bool cockpitHoodOnly, const PlayerState* playerDriver) {
    const CarMeshSet& cm = cars_.get(v.kind);
    const WheelMeshSet& wm = cars_.wheel();
    const auto& spec = getVehicleSpec(v.kind);

    float stanceOffsetY = 0.0f;
    if (v.stanceStyle == 1) stanceOffsetY = -0.045f;
    else if (v.stanceStyle == 2) stanceOffsetY = -0.075f;
    else if (v.stanceStyle == 3) stanceOffsetY = +0.085f;

    const float wheelBaseH = std::max(groundY, v.posY) + stanceOffsetY;
    const float bodyBaseH  = std::max(groundY - 0.12f, v.posY) + stanceOffsetY;
    const Mat4 rootPos = Mat4::translation(v.pos.x, wheelBaseH, v.pos.z) *
                         Mat4::rotationAxis({0, 1, 0}, -v.heading);
    const Mat4 bodyRootPos = Mat4::translation(v.pos.x, bodyBaseH, v.pos.z) *
                             Mat4::rotationAxis({0, 1, 0}, -v.heading);

    // Body pitch (nose up = +Z rot) & cornering roll (lean right = +X rot)
    const float avgComp = 0.25f * (v.wheelCompression[0] + v.wheelCompression[1] +
                                   v.wheelCompression[2] + v.wheelCompression[3]);
    const Mat4 bodyMat = bodyRootPos *
                         Mat4::translation(0.0f, -avgComp * 0.55f, 0.0f) *
                         Mat4::rotationAxis({0, 0, 1}, v.bodyPitch) *
                         Mat4::rotationAxis({1, 0, 0}, v.bodyRoll);

    if (alphaPass) {
        if (cockpitHoodOnly) return;
        // Soft two-tier ground contact ambient-occlusion shadow directly beneath the chassis (grounds every car on asphalt!)
        const float hoverH = std::max(0.0f, v.posY - groundY);
        if (hoverH < 0.85f) {
            const float aoFade = clampf(1.0f - hoverH / 0.85f, 0.0f, 1.0f);
            const Mat4 shBase = Mat4::translation(v.pos.x, groundY + 0.085f, v.pos.z) *
                                Mat4::rotationAxis({0, 1, 0}, -v.heading);
            drawMesh(vk, cubeMesh_, shBase * Mat4::scale(spec.halfLength * 1.94f, 0.012f, spec.halfWidth * 1.90f),
                     {6, 8, 12, static_cast<std::uint8_t>(118.0f * aoFade)}, 0.95f, 0.0f, 0.0f,
                     MatId::Standard, false, WorldPipelineKind::Alpha);
            drawMesh(vk, cubeMesh_, shBase * Mat4::translation(0.0f, 0.006f, 0.0f) *
                                    Mat4::scale(spec.halfLength * 1.62f, 0.012f, spec.halfWidth * 1.56f),
                     {4, 6, 10, static_cast<std::uint8_t>(98.0f * aoFade)}, 0.95f, 0.0f, 0.0f,
                     MatId::Standard, false, WorldPipelineKind::Alpha);
        }

        // Flush curved smoked automotive glass (matId=8 -> mirror-like Seoul reflections + dark 3D cabin & driver visible inside!)
        drawMesh(vk, cm.glass, bodyMat, {255, 255, 255, 255}, 0.03f, 0.15f, 0.0f,
                 MatId::CarGlass, false, WorldPipelineKind::Alpha);

        // Underglow LED neon if customized
        if (v.neonPreset > 0 && !v.destroyed) {
            ColorRGBA nc = getNeonPresetColor(v.neonPreset);
            nc.a = nightMode ? 170 : 95;
            const Mat4 nm = rootPos * Mat4::translation(0.0f, 0.04f, 0.0f) *
                            Mat4::scale(spec.halfLength * 1.75f, 0.03f, spec.halfWidth * 1.85f);
            drawMesh(vk, cubeMesh_, nm, nc, 0.1f, 0.0f, nightMode ? 1.8f : 0.8f,
                     MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
        }

        // Flashing Police / Emergency siren glow halo conformal to sleek LED lightbar!
        if (v.sirenActive && !v.destroyed) {
            const bool redPhase = std::sin(v.sirenPhase * 14.0f) > 0.0f;
            const ColorRGBA sc = redPhase ? ColorRGBA{255, 42, 48, 165} : ColorRGBA{46, 138, 255, 165};
            const float sideZ = redPhase ? -0.32f : 0.32f;
            const Mat4 sm = bodyMat * Mat4::translation(-0.18f, spec.chassisHeight + 0.20f, sideZ) *
                            Mat4::scale(0.28f, 0.18f, 0.56f);
            drawMesh(vk, sphereMesh_, sm, sc, 0.1f, 0.0f, 2.4f,
                     MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
        }

        // Exhaust Nitro / Upshift Backfire Flame!
        if ((v.nitroActive || v.backfireTimer > 0.01f) && !v.destroyed) {
            const ColorRGBA fc = v.nitroActive ? ColorRGBA{72, 196, 255, 210} : ColorRGBA{255, 148, 42, 200};
            const float flameLen = v.nitroActive ? 0.95f : 0.48f;
            for (float s : {-1.0f, 1.0f}) {
                const Mat4 fm = bodyMat * Mat4::translation(-spec.halfLength - flameLen * 0.45f, 0.28f, s * spec.halfWidth * 0.56f) *
                                Mat4::scale(flameLen, 0.16f, 0.16f);
                drawMesh(vk, sphereMesh_, fm, fc, 0.1f, 0.0f, 3.2f,
                         MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
            }
        }
        return;
    }

    // --- OPAQUE PASS (or Shadow Pass) ---
    ColorRGBA paint = v.destroyed ? ColorRGBA{42, 40, 42, 255} : v.paintColor;
    float bodyRough = v.destroyed ? 0.85f : (v.mattePaint ? 0.56f : (v.chromePaint ? 0.06f : 0.13f));
    float bodyMetal = v.destroyed ? 0.15f : (v.mattePaint ? 0.18f : (v.chromePaint ? 0.96f : 0.56f));

    // 1. Stamped CAD Sheet-Metal Car Body (matId=7 -> energy-conserving automotive clearcoat, ZERO JELLY!)
    drawMesh(vk, cm.body, bodyMat, paint, bodyRough, bodyMetal, 0.0f, MatId::CarClearcoat, shadowPass);

    if (cockpitHoodOnly) return;

    // 2. Dark 3D Interior Cabin + Live 3D Seated Driver holding the steering wheel!
    if (!shadowPass) {
        drawMesh(vk, cm.interior, bodyMat, {255, 255, 255, 255}, 0.80f, 0.04f, 0.0f, MatId::Standard, false);

        if (!v.destroyed && (v.hasNpcDriver || v.isPlayerDriving)) {
            Vec3 eye = driverSeatEyeLocal(v.kind);
            if (v.kind == VehicleKind::SeoulBus) {
                eye.x -= 0.14f;
            }
            if (v.kind == VehicleKind::ClassicRoadster) {
                // Open-top roadster has no roof: seat driver cleanly inside bucket seat with head & shoulders clearly visible!
                eye.x -= 0.28f;
                eye.y = std::max(eye.y, 1.14f);
            } else {
                // Clamp seated driver head strictly beneath low-slung supercar/hypercar/musclecar rooflines (zero roof clipping!)
                float maxSafeEyeY = std::max(0.74f, spec.chassisHeight - 0.24f);
                if (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype ||
                    v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::MuscleCar) {
                    maxSafeEyeY = std::min(maxSafeEyeY, 0.94f);
                }
                eye.y = std::min(eye.y, maxSafeEyeY);
            }
            ColorRGBA drvTop{54, 64, 82, 255};
            ColorRGBA drvSkin{234, 194, 162, 255};
            ColorRGBA drvHair{28, 24, 22, 255};
            bool policeCap = false;
            if (v.isPlayerDriving && playerDriver) {
                drvTop  = getPlayerTopColor(playerDriver->currentOutfit, playerDriver->characterCustom.customTopColorIdx);
                drvSkin = getPlayerSkinColor(playerDriver->characterCustom.skinToneIdx);
                drvHair = getPlayerHairColor(playerDriver->currentOutfit, playerDriver->characterCustom.hairColorIdx);
            } else if (v.isPolice || v.kind == VehicleKind::PoliceCruiser) {
                drvTop = {26, 48, 96, 255};
                drvHair = {22, 42, 88, 255};
                policeCap = true;
            } else if (v.kind == VehicleKind::SeoulBus) {
                drvTop = {138, 178, 218, 255}; // Seoul City Bus driver light-blue collared shirt!
            } else if (v.kind == VehicleKind::Taxi) {
                drvTop = {222, 226, 232, 255}; // Crisp white Seoul taxi shirt
            } else if (v.kind == VehicleKind::Ambulance || v.kind == VehicleKind::FireTruck) {
                drvTop = {216, 86, 36, 255};   // Korean 119 orange rescue jacket
            } else {
                static constexpr ColorRGBA kDrvPal[5] = {
                    {46, 56, 74, 255}, {154, 146, 134, 255}, {76, 84, 72, 255},
                    {126, 58, 56, 255}, {58, 62, 68, 255}
                };
                drvTop = kDrvPal[static_cast<unsigned>(v.id) % 5u];
            }

            // Seated driver torso slightly reclined into bucket seat + neck + head + hair/cap
            const Vec3 torsoPos{eye.x - 0.14f, eye.y - 0.54f, eye.z};
            const Mat4 drvRoot = bodyMat * Mat4::translation(torsoPos.x, torsoPos.y, torsoPos.z) *
                                 Mat4::rotationAxis({0, 0, 1}, 0.16f);
            drawMesh(vk, torsoMesh_, drvRoot * Mat4::scale(0.27f, 0.46f, 0.38f),
                     drvTop, 0.74f, 0.04f, 0.0f, MatId::FabricCloth, false);
            const Vec3 headPos{eye.x - 0.05f, eye.y - 0.04f, eye.z};
            drawMesh(vk, sphereMesh_, bodyMat * Mat4::translation(headPos.x, headPos.y, headPos.z) * Mat4::scale(0.195f, 0.22f, 0.18f),
                     drvSkin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
            drawMesh(vk, sphereMesh_, bodyMat * Mat4::translation(headPos.x - 0.02f, headPos.y + 0.045f, headPos.z) * Mat4::scale(0.205f, 0.145f, 0.19f),
                     drvHair, 0.68f, 0.04f, 0.0f, MatId::Standard, false);
            if (policeCap) {
                drawMesh(vk, cubeMesh_, bodyMat * Mat4::translation(headPos.x + 0.09f, headPos.y + 0.03f, headPos.z) * Mat4::scale(0.10f, 0.02f, 0.15f),
                         {18, 22, 28, 255}, 0.35f, 0.15f, 0.0f, MatId::Standard, false);
            }
            // Both arms gripping the steering wheel (steering hands shift subtly as front wheels turn!)
            const float wheelTilt = v.steerVisual * 0.075f;
            const Vec3 shL{eye.x - 0.10f, eye.y - 0.20f, eye.z - 0.18f};
            const Vec3 shR{eye.x - 0.10f, eye.y - 0.20f, eye.z + 0.18f};
            const Vec3 handL{eye.x + 0.22f, eye.y - 0.18f + wheelTilt, eye.z - 0.13f};
            const Vec3 handR{eye.x + 0.22f, eye.y - 0.18f - wheelTilt, eye.z + 0.13f};
            drawArmIK(vk, bodyMat, shL, handL, {eye.x + 0.02f, eye.y - 0.34f, eye.z - 0.28f}, drvTop, drvSkin, false);
            drawArmIK(vk, bodyMat, shR, handR, {eye.x + 0.02f, eye.y - 0.34f, eye.z + 0.28f}, drvTop, drvSkin, false);
        }
    }

    // 3. Matte-Black Exterior Trim (DLO frames, grilles, shutlines, wheel-well liners, mirrors, diffuser!)
    drawMesh(vk, cm.trim, bodyMat, {255, 255, 255, 255}, 0.46f, 0.18f, 0.0f, MatId::Standard, shadowPass);

    // 4. Brushed Chrome Accents (grille slats, exhaust tips, door handles, badges, license plates)
    if (!shadowPass) {
        drawMesh(vk, cm.chrome, bodyMat, {255, 255, 255, 255}, 0.15f, 0.92f, 0.0f, MatId::CarClearcoat, false);
    }

    // 5. Clean Zero-Z-Fighting Livery (Police lightbar/door band, Taxi sign, 119 Ambulance/Fire, Seoul Bus band)
    if (cm.livery.valid()) {
        const float liveryEm = (!shadowPass && (v.kind == VehicleKind::Taxi || v.sirenActive))
            ? (nightMode ? 1.4f : 0.45f) : 0.0f;
        drawMesh(vk, cm.livery, bodyMat, {255, 255, 255, 255}, 0.24f, 0.20f, liveryEm,
                 liveryEm > 0.1f ? MatId::EmissiveGlow : MatId::CarClearcoat, shadowPass);
    }

    // 6. Optional Racing Stripes
    if (v.stripeStyle > 0 && cm.stripes.valid() && !shadowPass) {
        const ColorRGBA sc = getVehicleStripeColorRGBA(v.stripeStyle, v.stripeColor);
        drawMesh(vk, cm.stripes, bodyMat, sc, 0.16f, 0.45f, 0.0f, MatId::CarClearcoat, false);
    }

    // 7. Rear Aero Spoiler / Carbon GT Wing
    if ((spec.hasSpoiler || v.customSpoiler || v.spoilerStyle > 0) && cm.spoiler.valid()) {
        drawMesh(vk, cm.spoiler, bodyMat, {255, 255, 255, 255}, 0.28f, 0.35f, 0.0f, MatId::Standard, shadowPass);
    }

    // 8. Sculpted 3D Headlamps, Taillights, Korean Hangul License Plates & Seoul Bus Front LED Header!
    if (!shadowPass && !v.destroyed) {
        const ColorRGBA hlCol = getVehicleHeadlightColorRGBA(v.headlightColorIdx, nightMode);
        const float hlEm = nightMode ? 2.4f : 0.55f;
        drawMesh(vk, cm.lampsHead, bodyMat, hlCol, 0.08f, 0.10f, hlEm, MatId::EmissiveGlow, false);

        const float tlEm = v.brakingVisual ? 2.65f : (nightMode ? 1.35f : 0.28f);
        drawMesh(vk, cm.lampsTail, bodyMat, {255, 42, 48, 255}, 0.08f, 0.10f, tlEm, MatId::EmissiveGlow, false);

        // Authentic 4K Korean Hangul License Plates (123가 4567 / 경찰 POLICE 112)
        auto drawVehSign = [&](const Vec3& locPos, const Vec3& locNorm, float w, float h, int slot, float em) {
            const Vec3 wc = bodyMat.transformPoint(locPos);
            const Vec3 wn = (bodyMat.transformPoint(locPos + locNorm) - wc).normalized();
            const Vec3 wu = (bodyMat.transformPoint(locPos + Vec3{0.0f, 1.0f, 0.0f}) - wc).normalized();
            drawSignQuad(vk, wc, wn, wu, w, h, slot, em);
        };
        const int plateSlot = (v.isPolice || v.kind == VehicleKind::PoliceCruiser) ? 61 : 60;
        const float plateW = (v.kind == VehicleKind::SeoulBus || v.kind == VehicleKind::FireTruck) ? 0.50f : 0.41f;
        const float plateH = (v.kind == VehicleKind::SeoulBus || v.kind == VehicleKind::FireTruck) ? 0.15f : 0.11f;
        const float plateEm = nightMode ? 0.14f : 0.02f;
        drawVehSign(cm.frontPlateLocal + Vec3{0.008f, 0.0f, 0.0f}, { 1.0f, 0.0f, 0.0f}, plateW, plateH, plateSlot, plateEm);
        drawVehSign(cm.rearPlateLocal  - Vec3{0.008f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, plateW, plateH, plateSlot, plateEm);

        // Seoul City Bus Front LED Destination Matrix (472 강남역·신사·시청 / 146 삼성역·잠실·테헤란로)!
        if (v.kind == VehicleKind::SeoulBus) {
            const int busSlot = (v.paintColor.b >= v.paintColor.g) ? 58 : 59;
            drawVehSign(cm.busSignLocal, {1.0f, 0.0f, 0.0f}, 1.66f, 0.31f, busSlot, nightMode ? 0.92f : 0.42f);
        }
    }

    // 9. Four Independent Articulated Wheels (Curved Rubber Tire + 10-Spoke Alloy Rim + Brake Rotor + Caliper!)
    const ColorRGBA rimTint = rimColorForStyle(v.rimStyle);
    const float camberRad = (v.stanceStyle == 2) ? 0.10f : 0.0f;
    const float wX[4] = {cm.frontAxleX, cm.frontAxleX, cm.rearAxleX, cm.rearAxleX};
    const float wZ[4] = {-cm.wheelTrackZ, cm.wheelTrackZ, -cm.wheelTrackZ, cm.wheelTrackZ};

    for (int i = 0; i < 4; ++i) {
        const bool isFront = (i < 2);
        const float sideSign = (wZ[i] < 0.0f) ? -1.0f : 1.0f;
        const float wy = cm.wheelRadius + v.wheelCompression[static_cast<std::size_t>(i)] * 0.55f;
        const float steerRad = isFront ? (-v.steerVisual * 0.48f) : 0.0f;

        const Mat4 hubMat = rootPos *
                            Mat4::translation(wX[i], wy, wZ[i]) *
                            Mat4::rotationAxis({0, 1, 0}, steerRad) *
                            Mat4::rotationAxis({1, 0, 0}, -sideSign * camberRad);
        const Mat4 spinScale = Mat4::rotationAxis({0, 0, 1}, -v.wheelSpin) *
                               Mat4::scale(cm.wheelRadius, cm.wheelRadius, cm.wheelWidth);
        const Mat4 staticScale = Mat4::scale(cm.wheelRadius, cm.wheelRadius, cm.wheelWidth);

        drawMesh(vk, wm.tire, hubMat * spinScale, {255, 255, 255, 255}, 0.82f, 0.02f, 0.0f, MatId::Standard, shadowPass);
        if (!shadowPass) {
            drawMesh(vk, wm.rim, hubMat * spinScale, rimTint, 0.18f, 0.88f, 0.0f, MatId::CarClearcoat, false);
            // Hot glowing brake rotor under heavy braking!
            const ColorRGBA discCol = (v.brakeHeat > 0.25f)
                ? ColorRGBA{static_cast<std::uint8_t>(lerpf(180.0f, 255.0f, v.brakeHeat)),
                            static_cast<std::uint8_t>(lerpf(185.0f, 92.0f, v.brakeHeat)),
                            static_cast<std::uint8_t>(lerpf(192.0f, 34.0f, v.brakeHeat)), 255}
                : ColorRGBA{182, 188, 196, 255};
            drawMesh(vk, wm.brakeDisc, hubMat * spinScale, discCol, 0.32f, 0.82f,
                     v.brakeHeat > 0.35f ? v.brakeHeat * 1.4f : 0.0f,
                     v.brakeHeat > 0.35f ? MatId::EmissiveGlow : MatId::Standard, false);
            drawMesh(vk, wm.caliper, hubMat * staticScale, {255, 255, 255, 255}, 0.26f, 0.35f, 0.0f, MatId::CarClearcoat, false);
        }
    }
}

namespace {

inline Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)};
}

inline bool isTwoHandedFirearm(WeaponId w) {
    return w == WeaponId::SMG || w == WeaponId::Shotgun || w == WeaponId::AutoShotgun ||
           w == WeaponId::AssaultRifle || w == WeaponId::SniperRifle || w == WeaponId::CombatMG ||
           w == WeaponId::Minigun || w == WeaponId::HeavyLauncher;
}

// Left-hand support grip anchor in weapon-local space (origin = right-hand pistol grip center, +X = muzzle)
inline Vec3 weaponForegripLocal(WeaponId w) {
    switch (w) {
        case WeaponId::Pistol:        return { 0.015f, -0.025f, -0.022f};
        case WeaponId::HeavyRevolver: return { 0.018f, -0.028f, -0.022f};
        case WeaponId::SMG:           return { 0.215f,  0.032f, -0.015f};
        case WeaponId::Shotgun:       return { 0.310f,  0.036f, -0.015f};
        case WeaponId::AutoShotgun:   return { 0.275f,  0.034f, -0.018f};
        case WeaponId::AssaultRifle:  return { 0.285f,  0.022f, -0.015f};
        case WeaponId::SniperRifle:   return { 0.320f,  0.028f, -0.015f};
        case WeaponId::CombatMG:      return { 0.295f,  0.012f, -0.024f};
        case WeaponId::Minigun:       return { 0.165f,  0.165f, -0.030f}; // Top chainsaw carry handle!
        case WeaponId::HeavyLauncher: return { 0.210f, -0.010f, -0.015f};
        default:                      return { 0.020f,  0.000f, -0.020f};
    }
}

// Magazine insertion well in weapon-local space for authentic left-hand reload animation
inline Vec3 weaponMagWellLocal(WeaponId w) {
    switch (w) {
        case WeaponId::Pistol:        return {-0.010f, -0.085f,  0.000f};
        case WeaponId::HeavyRevolver: return { 0.075f,  0.045f, -0.025f};
        case WeaponId::SMG:           return { 0.150f, -0.085f,  0.000f};
        case WeaponId::Shotgun:       return { 0.120f,  0.015f,  0.000f};
        case WeaponId::AutoShotgun:   return { 0.165f, -0.085f,  0.000f};
        case WeaponId::AssaultRifle:  return { 0.145f, -0.095f,  0.000f};
        case WeaponId::SniperRifle:   return { 0.145f, -0.070f,  0.000f};
        case WeaponId::CombatMG:      return { 0.155f, -0.100f,  0.000f};
        case WeaponId::Minigun:       return { 0.080f, -0.060f, -0.050f};
        case WeaponId::HeavyLauncher: return { 0.560f,  0.055f,  0.000f};
        default:                      return { 0.000f, -0.060f,  0.000f};
    }
}

// Builds a right-handed transform for capsuleLimbMesh_ (origin at Y=0, extends along -Y to Y=-1)
inline Mat4 buildCapsuleSegmentMatrix(const Vec3& a, const Vec3& b, float radius) {
    const Vec3 diff = b - a;
    const float len = std::max(0.02f, diff.length());
    const Vec3 dir = diff / len;
    const Vec3 yAxis = dir * -1.0f; // Local -Y points from a toward b
    const Vec3 ref = (std::abs(dir.y) < 0.92f) ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 xAxis = cross3(ref, yAxis).normalized();
    const Vec3 zAxis = cross3(xAxis, yAxis).normalized();

    Mat4 r{};
    r.m[0]  = xAxis.x * radius; r.m[1]  = xAxis.y * radius; r.m[2]  = xAxis.z * radius; r.m[3]  = 0.0f;
    r.m[4]  = yAxis.x * len;    r.m[5]  = yAxis.y * len;    r.m[6]  = yAxis.z * len;    r.m[7]  = 0.0f;
    r.m[8]  = zAxis.x * radius; r.m[9]  = zAxis.y * radius; r.m[10] = zAxis.z * radius; r.m[11] = 0.0f;
    r.m[12] = a.x;              r.m[13] = a.y;              r.m[14] = a.z;              r.m[15] = 1.0f;
    return r;
}

} // namespace

void Renderer3D::drawDetailedWeapon3D(VulkanContext& vk, WeaponId weapon, const WeaponSlotState* customWep,
                                      const Mat4& weaponWorldM, float muzzleFlash, float minigunSpin,
                                      bool shadowPass, bool isFirstPerson) {
    if (weapon == WeaponId::Fists) return;

    auto mat = customWep
        ? getWeaponFinishMaterial(weapon, customWep->skinIndex)
        : WeaponFinishMaterial{{86, 94, 108, 255}, {148, 158, 172, 255}, 0.24f, 0.82f, 0.0f};
    if (!customWep || customWep->skinIndex == 0) {
        // Crisp two-tone anodized gunmetal & satin titanium finish so firearms pop cleanly in both 3P and 1P!
        mat.primary   = {82, 90, 104, 255};
        mat.secondary = {144, 154, 168, 255};
        mat.roughness = 0.24f;
        mat.metallic  = 0.82f;
    }
    const bool hasSuppressor = customWep && customWep->suppressor && weaponSupportsSuppressor(weapon);
    const bool extMag = customWep && customWep->extendedMag;
    const int laserColIdx = customWep ? customWep->laserSightColor : 0;

    const ColorRGBA polymerCol{34, 38, 44, 255};
    const ColorRGBA steelCol{128, 138, 152, 255};
    const ColorRGBA darkIron{46, 52, 60, 255};
    const ColorRGBA woodCol{132, 82, 46, 255};
    const ColorRGBA oliveCol{76, 96, 66, 255};
    const ColorRGBA brassCol{212, 176, 82, 255};
    const ColorRGBA chromeBolt{186, 194, 204, 255};

    auto boxPart = [&](const Vec3& c, const Vec3& sz, ColorRGBA col,
                       float rough, float metal, float em = 0.0f, float pitchZ = 0.0f) {
        Mat4 m = weaponWorldM * Mat4::translation(c.x, c.y, c.z);
        if (std::abs(pitchZ) > 0.001f) m = m * Mat4::rotationAxis({0, 0, 1}, pitchZ);
        drawMesh(vk, cubeMesh_, m * Mat4::scale(sz.x, sz.y, sz.z),
                 col, rough, metal, em, (em > 0.1f) ? MatId::EmissiveGlow : MatId::Standard, shadowPass);
    };
    // Cylinder starting at (x0, y, z) extending +len along +X with diameter diam
    auto cylX = [&](float x0, float y, float z, float len, float diam, ColorRGBA col,
                    float rough, float metal, float em = 0.0f) {
        const Mat4 m = weaponWorldM * Mat4::translation(x0, y, z) *
                       Mat4::rotationAxis({0, 0, 1}, -kHalfPi) *
                       Mat4::scale(diam, len, diam);
        drawMesh(vk, cylMesh_, m, col, rough, metal, em,
                 (em > 0.1f) ? MatId::EmissiveGlow : MatId::Standard, shadowPass);
    };

    float muzzleX = 0.24f;
    float muzzleY = 0.068f;
    float flashScale = 0.16f;

    switch (weapon) {
        case WeaponId::Pistol: {
            // Ergonomic angled polymer pistol grip + metallic mag baseplate
            boxPart({-0.012f, -0.025f, 0.0f}, {0.044f, 0.118f, 0.032f}, polymerCol, 0.72f, 0.08f, 0.0f, -0.22f);
            if (extMag) {
                boxPart({-0.022f, -0.095f, 0.0f}, {0.038f, 0.052f, 0.028f}, steelCol, 0.36f, 0.78f, 0.0f, -0.22f);
            }
            // Lower polymer frame + trigger guard
            boxPart({0.072f, 0.040f, 0.0f}, {0.195f, 0.026f, 0.031f}, polymerCol, 0.65f, 0.12f);
            boxPart({0.052f, 0.008f, 0.0f}, {0.052f, 0.010f, 0.018f}, polymerCol, 0.65f, 0.12f);
            boxPart({0.076f, 0.022f, 0.0f}, {0.010f, 0.030f, 0.018f}, polymerCol, 0.65f, 0.12f);
            // Brushed satin metallic upper slide + stainless ejection port & barrel tip!
            boxPart({0.078f, 0.064f, 0.0f}, {0.208f, 0.032f, 0.034f}, mat.secondary, 0.20f, 0.88f, mat.emissive);
            boxPart({0.088f, 0.068f, 0.0f}, {0.046f, 0.022f, 0.036f}, chromeBolt, 0.18f, 0.94f);
            cylX(0.165f, 0.065f, 0.0f, 0.036f, 0.018f, chromeBolt, 0.22f, 0.92f);
            // Crisp white/green tritium 3-dot iron sights (very legible in 1st-person!)
            if (!shadowPass) {
                boxPart({-0.016f, 0.085f, -0.010f}, {0.012f, 0.012f, 0.007f}, {235, 240, 245, 255}, 0.2f, 0.1f, 0.35f);
                boxPart({-0.016f, 0.085f,  0.010f}, {0.012f, 0.012f, 0.007f}, {235, 240, 245, 255}, 0.2f, 0.1f, 0.35f);
                boxPart({ 0.172f, 0.086f,  0.000f}, {0.010f, 0.013f, 0.007f}, {110, 255, 130, 255}, 0.2f, 0.1f, 0.85f);
            }
            muzzleX = 0.205f;
            muzzleY = 0.066f;
            flashScale = 0.15f;
            break;
        }
        case WeaponId::HeavyRevolver: {
            // Classic walnut curved grip + polished stainless Magnum frame
            boxPart({-0.018f, -0.030f, 0.0f}, {0.046f, 0.122f, 0.034f}, woodCol, 0.56f, 0.05f, 0.0f, -0.34f);
            boxPart({0.070f, 0.042f, 0.0f}, {0.135f, 0.066f, 0.032f}, chromeBolt, 0.18f, 0.92f, mat.emissive);
            cylX(0.038f, 0.048f, 0.0f, 0.068f, 0.056f, mat.secondary, 0.20f, 0.92f);
            cylX(0.105f, 0.068f, 0.0f, 0.215f, 0.026f, chromeBolt, 0.16f, 0.94f, mat.emissive);
            boxPart({0.208f, 0.052f, 0.0f}, {0.195f, 0.022f, 0.024f}, mat.secondary, 0.22f, 0.90f);
            if (!shadowPass) {
                boxPart({0.305f, 0.088f, 0.0f}, {0.016f, 0.016f, 0.008f}, {248, 92, 48, 255}, 0.3f, 0.1f, 0.65f);
            }
            muzzleX = 0.325f;
            muzzleY = 0.068f;
            flashScale = 0.21f;
            break;
        }
        case WeaponId::SMG: {
            // Compact Korean K7 / MP5 Tactical SMG
            boxPart({-0.010f, -0.028f, 0.0f}, {0.042f, 0.115f, 0.034f}, polymerCol, 0.72f, 0.08f, 0.0f, -0.24f);
            boxPart({-0.145f, 0.052f, 0.0f}, {0.195f, 0.022f, 0.032f}, steelCol, 0.32f, 0.82f);
            boxPart({-0.238f, 0.025f, 0.0f}, {0.026f, 0.098f, 0.038f}, polymerCol, 0.74f, 0.06f);
            boxPart({0.115f, 0.056f, 0.0f}, {0.265f, 0.064f, 0.046f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.115f, 0.089f, 0.0f}, {0.210f, 0.010f, 0.030f}, steelCol, 0.28f, 0.84f);
            boxPart({0.155f, -0.065f, 0.0f}, {0.036f, extMag ? 0.22f : 0.165f, 0.028f}, darkIron, 0.38f, 0.76f, 0.0f, 0.16f);
            boxPart({0.255f, 0.048f, 0.0f}, {0.135f, 0.046f, 0.042f}, polymerCol, 0.68f, 0.14f);
            cylX(0.310f, 0.058f, 0.0f, 0.105f, 0.022f, steelCol, 0.24f, 0.90f);
            if (!shadowPass) {
                boxPart({0.095f, 0.104f, 0.0f}, {0.062f, 0.032f, 0.036f}, darkIron, 0.34f, 0.74f);
                const ColorRGBA dotCol = getOpticReticleColorRGBA(customWep ? customWep->opticColorIdx : 0, {255, 62, 62, 255});
                boxPart({0.095f, 0.107f, 0.0f}, {0.006f, 0.008f, 0.008f}, dotCol, 0.1f, 0.0f, 2.0f);
            }
            muzzleX = 0.420f;
            muzzleY = 0.058f;
            flashScale = 0.18f;
            break;
        }
        case WeaponId::Shotgun: {
            // Pump-Action Tactical Shotgun (M870 style)
            boxPart({-0.015f, -0.022f, 0.0f}, {0.046f, 0.105f, 0.038f}, polymerCol, 0.72f, 0.06f, 0.0f, -0.36f);
            boxPart({-0.165f, 0.022f, 0.0f}, {0.265f, 0.072f, 0.044f}, woodCol, 0.62f, 0.04f, 0.0f, 0.08f);
            boxPart({0.105f, 0.056f, 0.0f}, {0.225f, 0.064f, 0.048f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.135f, 0.064f, 0.024f}, {0.068f, 0.022f, 0.008f}, chromeBolt, 0.18f, 0.94f);
            cylX(0.205f, 0.072f, 0.0f, 0.485f, 0.028f, steelCol, 0.22f, 0.90f);
            cylX(0.205f, 0.040f, 0.0f, 0.425f, 0.026f, darkIron, 0.32f, 0.85f);
            boxPart({0.335f, 0.042f, 0.0f}, {0.185f, 0.048f, 0.046f}, woodCol, 0.64f, 0.06f);
            muzzleX = 0.695f;
            muzzleY = 0.072f;
            flashScale = 0.28f;
            break;
        }
        case WeaponId::AutoShotgun: {
            // Korean Daewoo USAS-12 / AA-12 Drum Combat Shotgun
            boxPart({-0.010f, -0.028f, 0.0f}, {0.044f, 0.115f, 0.038f}, polymerCol, 0.70f, 0.10f, 0.0f, -0.22f);
            boxPart({-0.165f, 0.048f, 0.0f}, {0.245f, 0.102f, 0.052f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.135f, 0.062f, 0.0f}, {0.355f, 0.086f, 0.056f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            const Mat4 drumM = weaponWorldM * Mat4::translation(0.165f, -0.058f, -0.038f) *
                               Mat4::rotationAxis({1, 0, 0}, kHalfPi) *
                               Mat4::scale(extMag ? 0.165f : 0.138f, 0.076f, extMag ? 0.165f : 0.138f);
            drawMesh(vk, cylMesh_, drumM, darkIron, 0.42f, 0.68f, 0.0f, MatId::Standard, shadowPass);
            boxPart({0.145f, 0.125f, 0.0f}, {0.225f, 0.024f, 0.032f}, polymerCol, 0.65f, 0.12f);
            boxPart({0.335f, 0.058f, 0.0f}, {0.195f, 0.054f, 0.048f}, mat.secondary, 0.34f, 0.72f);
            cylX(0.415f, 0.064f, 0.0f, 0.155f, 0.032f, steelCol, 0.24f, 0.90f);
            muzzleX = 0.575f;
            muzzleY = 0.064f;
            flashScale = 0.26f;
            break;
        }
        case WeaponId::AssaultRifle: {
            // Korean K2C1 / M4A1 Tactical Carbine with rich anodized gunmetal + satin steel two-tone finish!
            boxPart({-0.012f, -0.028f, 0.0f}, {0.044f, 0.118f, 0.036f}, polymerCol, 0.72f, 0.08f, 0.0f, -0.26f);
            // Buffer tube + angled collapsible buttstock
            cylX(-0.235f, 0.062f, 0.0f, 0.205f, 0.032f, steelCol, 0.28f, 0.84f);
            boxPart({-0.215f, 0.032f, 0.0f}, {0.115f, 0.108f, 0.042f}, polymerCol, 0.70f, 0.10f);
            // Two-tone Upper (satin gunmetal) + Lower Receiver + Chrome Bolt Carrier + Top Picatinny Rail!
            boxPart({0.105f, 0.042f, 0.0f}, {0.245f, 0.042f, 0.046f}, darkIron, 0.38f, 0.74f);
            boxPart({0.105f, 0.074f, 0.0f}, {0.250f, 0.034f, 0.048f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.135f, 0.072f, 0.024f}, {0.066f, 0.018f, 0.008f}, chromeBolt, 0.18f, 0.94f);
            boxPart({0.205f, 0.093f, 0.0f}, {0.420f, 0.011f, 0.030f}, steelCol, 0.26f, 0.86f);
            // Iconic curved 30-rd STANAG magazine (metallic gunmetal)
            boxPart({0.145f, -0.048f, 0.0f}, {0.056f, extMag ? 0.165f : 0.125f, 0.030f}, mat.secondary, 0.30f, 0.82f, 0.0f, 0.14f);
            boxPart({0.162f, -0.122f, 0.0f}, {0.052f, 0.072f, 0.028f}, steelCol, 0.34f, 0.80f, 0.0f, 0.30f);
            // Tactical M-LOK / Quad-Rail Handguard (flat dark earth / gunmetal trim) + Vertical Foregrip!
            boxPart({0.315f, 0.058f, 0.0f}, {0.225f, 0.054f, 0.048f}, {92, 86, 76, 255}, 0.56f, 0.28f);
            boxPart({0.295f, 0.004f, 0.0f}, {0.032f, 0.072f, 0.032f}, polymerCol, 0.74f, 0.08f);
            // Heavy barrel + gas block + chrome-trimmed flash hider
            cylX(0.415f, 0.062f, 0.0f, 0.185f, 0.022f, steelCol, 0.22f, 0.92f);
            cylX(0.585f, 0.062f, 0.0f, 0.048f, 0.028f, chromeBolt, 0.22f, 0.92f);
            // Holographic / EOTech combat optic on top Picatinny rail + glowing reticle window!
            if (!shadowPass) {
                boxPart({0.115f, 0.114f, 0.0f}, {0.105f, 0.038f, 0.042f}, {56, 62, 72, 255}, 0.28f, 0.78f);
                const ColorRGBA optCol = getOpticReticleColorRGBA(customWep ? customWep->opticColorIdx : 0, {72, 236, 138, 255});
                boxPart({0.115f, 0.118f, 0.0f}, {0.010f, 0.012f, 0.012f}, optCol, 0.1f, 0.0f, 2.4f);
            }
            muzzleX = 0.635f;
            muzzleY = 0.062f;
            flashScale = 0.22f;
            break;
        }
        case WeaponId::SniperRifle: {
            // Korean K14 Precision Bolt-Action Sniper Rifle
            boxPart({-0.012f, -0.028f, 0.0f}, {0.044f, 0.115f, 0.036f}, oliveCol, 0.68f, 0.10f, 0.0f, -0.24f);
            // Precision stock with cheek riser
            boxPart({-0.195f, 0.034f, 0.0f}, {0.295f, 0.084f, 0.044f}, oliveCol, 0.66f, 0.12f);
            boxPart({-0.175f, 0.086f, 0.0f}, {0.125f, 0.028f, 0.042f}, polymerCol, 0.72f, 0.06f);
            // Chassis + heavy fluted long barrel + rectangular muzzle brake
            boxPart({0.155f, 0.052f, 0.0f}, {0.365f, 0.066f, 0.050f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.145f, -0.042f, 0.0f}, {0.068f, 0.092f, 0.032f}, darkIron, 0.36f, 0.78f);
            cylX(0.320f, 0.064f, 0.0f, 0.510f, 0.026f, steelCol, 0.22f, 0.92f);
            boxPart({0.835f, 0.064f, 0.0f}, {0.062f, 0.034f, 0.042f}, darkIron, 0.28f, 0.88f);
            // High-magnification telescopic scope + twin mounts + folded bipod
            cylX(0.035f, 0.126f, 0.0f, 0.295f, 0.036f, darkIron, 0.24f, 0.84f);
            cylX(0.285f, 0.126f, 0.0f, 0.085f, 0.052f, darkIron, 0.22f, 0.86f);
            boxPart({0.105f, 0.096f, 0.0f}, {0.024f, 0.038f, 0.028f}, steelCol, 0.3f, 0.82f);
            boxPart({0.235f, 0.096f, 0.0f}, {0.024f, 0.038f, 0.028f}, steelCol, 0.3f, 0.82f);
            cylX(0.380f, 0.014f, -0.028f, 0.175f, 0.014f, darkIron, 0.36f, 0.78f);
            cylX(0.380f, 0.014f,  0.028f, 0.175f, 0.014f, darkIron, 0.36f, 0.78f);
            muzzleX = 0.875f;
            muzzleY = 0.064f;
            flashScale = 0.28f;
            break;
        }
        case WeaponId::CombatMG: {
            // Korean K3 / M249 Belt-Fed Light Machine Gun
            boxPart({-0.012f, -0.028f, 0.0f}, {0.046f, 0.118f, 0.038f}, polymerCol, 0.70f, 0.10f, 0.0f, -0.22f);
            boxPart({-0.185f, 0.045f, 0.0f}, {0.255f, 0.085f, 0.048f}, polymerCol, 0.68f, 0.14f);
            // Heavy box receiver + big olive ammo drum box + gleaming brass belt
            boxPart({0.145f, 0.068f, 0.0f}, {0.365f, 0.094f, 0.074f}, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.165f, -0.056f, 0.0f}, {0.145f, 0.132f, extMag ? 0.138f : 0.106f}, oliveCol, 0.62f, 0.18f);
            if (!shadowPass) {
                boxPart({0.165f, 0.032f, -0.050f}, {0.072f, 0.048f, 0.018f}, brassCol, 0.22f, 0.92f);
            }
            // Ventilated barrel jacket + heavy LMG barrel + top carry handle
            boxPart({0.365f, 0.062f, 0.0f}, {0.245f, 0.064f, 0.062f}, mat.secondary, 0.38f, 0.76f);
            cylX(0.475f, 0.068f, 0.0f, 0.225f, 0.030f, darkIron, 0.24f, 0.92f);
            boxPart({0.255f, 0.136f, 0.0f}, {0.145f, 0.022f, 0.028f}, steelCol, 0.36f, 0.78f);
            muzzleX = 0.705f;
            muzzleY = 0.068f;
            flashScale = 0.26f;
            break;
        }
        case WeaponId::Minigun: {
            // M134 6-Barrel Rotary Gatling Minigun!
            // Rear electric drive motor + dual trigger spade grip
            cylX(-0.085f, 0.052f, 0.0f, 0.245f, 0.138f, mat.primary, mat.roughness, mat.metallic, mat.emissive);
            boxPart({0.000f, 0.000f, 0.0f}, {0.045f, 0.115f, 0.040f}, polymerCol, 0.70f, 0.12f);
            // Iconic top chainsaw carry bar (left hand IK grips here!)
            boxPart({0.155f, 0.165f, 0.0f}, {0.205f, 0.026f, 0.032f}, steelCol, 0.34f, 0.80f);
            boxPart({0.065f, 0.125f, 0.0f}, {0.024f, 0.085f, 0.028f}, steelCol, 0.34f, 0.80f);
            boxPart({0.245f, 0.125f, 0.0f}, {0.024f, 0.085f, 0.028f}, steelCol, 0.34f, 0.80f);
            // Under-slung ammo feed chute
            boxPart({0.075f, -0.058f, -0.055f}, {0.135f, 0.085f, 0.085f}, oliveCol, 0.62f, 0.18f);
            // 6 individually rotating steel gatling barrels + 2 clamp rings!
            for (int b = 0; b < 6; ++b) {
                const float ang = minigunSpin + kTwoPi * (static_cast<float>(b) / 6.0f);
                const float by = 0.052f + std::cos(ang) * 0.042f;
                const float bz = std::sin(ang) * 0.042f;
                cylX(0.155f, by, bz, 0.620f, 0.020f, mat.secondary, 0.22f, 0.92f);
            }
            cylX(0.365f, 0.052f, 0.0f, 0.032f, 0.116f, darkIron, 0.30f, 0.86f);
            cylX(0.710f, 0.052f, 0.0f, 0.032f, 0.116f, darkIron, 0.30f, 0.86f);
            muzzleX = 0.785f;
            muzzleY = 0.052f;
            flashScale = 0.34f;
            break;
        }
        case WeaponId::HeavyLauncher: {
            // Shoulder-Fired Rocket Launcher (RPG-7 / AT4 style)
            // Main olive launch tube (extends behind shoulder & forward)
            cylX(-0.520f, 0.068f, 0.0f, 1.080f, 0.084f, oliveCol, 0.56f, 0.26f);
            // Flared rear exhaust venturi cone + protective wood heat-shield band
            cylX(-0.565f, 0.068f, 0.0f, 0.065f, 0.112f, darkIron, 0.38f, 0.78f);
            cylX(-0.080f, 0.068f, 0.0f, 0.340f, 0.096f, woodCol, 0.66f, 0.06f);
            // Twin pistol grips (right hand at origin, left hand forward)
            boxPart({0.000f, -0.015f, 0.0f}, {0.044f, 0.115f, 0.034f}, polymerCol, 0.72f, 0.08f, 0.0f, -0.18f);
            boxPart({0.210f, -0.012f, 0.0f}, {0.040f, 0.105f, 0.034f}, polymerCol, 0.72f, 0.08f, 0.0f, 0.12f);
            // Protruding tapered warhead nosecone!
            cylX(0.555f, 0.068f, 0.0f, 0.085f, 0.118f, mat.primary, 0.44f, 0.45f, mat.emissive);
            const Mat4 noseM = weaponWorldM * Mat4::translation(0.635f, 0.068f, 0.0f) *
                               Mat4::rotationAxis({0, 0, 1}, -kHalfPi) *
                               Mat4::scale(0.118f, 0.165f, 0.118f);
            drawMesh(vk, coneMesh_, noseM, mat.secondary, 0.40f, 0.48f, mat.emissive, MatId::Standard, shadowPass);
            muzzleX = 0.815f;
            muzzleY = 0.068f;
            flashScale = 0.36f;
            break;
        }
        default: break;
    }

    // Optional Tactical Suppressor Cylinder
    if (hasSuppressor) {
        const float supLen = (weapon == WeaponId::Pistol || weapon == WeaponId::HeavyRevolver) ? 0.135f : 0.175f;
        cylX(muzzleX - 0.01f, muzzleY, 0.0f, supLen, 0.036f, {24, 26, 30, 255}, 0.42f, 0.72f);
        muzzleX += supLen;
        flashScale *= 0.42f;
    }

    // Optional Laser Sight Module & Crisp Glowing Beam (1P local beam; 3P uses exact world-ray beam in drawAlphaAndEffects!)
    if (!shadowPass && laserColIdx > 0) {
        const ColorRGBA lCol = getLaserSightColorRGBA(laserColIdx);
        boxPart({muzzleX * 0.62f, muzzleY - 0.026f, 0.022f}, {0.055f, 0.022f, 0.022f}, darkIron, 0.36f, 0.74f);
        if (isFirstPerson) {
            cylX(muzzleX * 0.68f, muzzleY - 0.026f, 0.022f, 2.6f, 0.0055f, lCol, 0.1f, 0.0f, 2.8f);
        } else if (customWep != nullptr) {
            const Vec3 loc{muzzleX * 0.68f, muzzleY - 0.026f, 0.022f};
            lastPlayerLaserOrigin_ = {
                weaponWorldM.m[0] * loc.x + weaponWorldM.m[4] * loc.y + weaponWorldM.m[8]  * loc.z + weaponWorldM.m[12],
                weaponWorldM.m[1] * loc.x + weaponWorldM.m[5] * loc.y + weaponWorldM.m[9]  * loc.z + weaponWorldM.m[13],
                weaponWorldM.m[2] * loc.x + weaponWorldM.m[6] * loc.y + weaponWorldM.m[10] * loc.z + weaponWorldM.m[14]
            };
            hasPlayerLaserOrigin_ = true;
        }
    }

    // Starburst Muzzle Flash!
    if (!shadowPass && muzzleFlash > 0.01f) {
        const float s = flashScale * (0.75f + 0.55f * clampf(muzzleFlash / 0.08f, 0.0f, 1.0f));
        const Mat4 fm = weaponWorldM * Mat4::translation(muzzleX + s * 0.32f, muzzleY, 0.0f);
        drawMesh(vk, sphereMesh_, fm * Mat4::scale(s * 1.45f, s * 0.65f, s * 0.65f),
                 {255, 224, 108, 245}, 0.1f, 0.0f, 3.6f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
        drawMesh(vk, sphereMesh_, fm * Mat4::scale(s * 0.65f, s * 1.30f, s * 0.65f),
                 {255, 172, 64, 210}, 0.1f, 0.0f, 3.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
    }
}

void Renderer3D::drawArmIK(VulkanContext& vk, const Mat4& rootM, const Vec3& shoulderLocal,
                           const Vec3& handLocal, const Vec3& elbowPoleLocal,
                           ColorRGBA sleeveCol, ColorRGBA skinCol, bool shadowPass) {
    constexpr float L1 = 0.275f; // Upper arm length (m)
    constexpr float L2 = 0.265f; // Forearm length (m)

    Vec3 toHand = handLocal - shoulderLocal;
    float d = toHand.length();
    if (d < 0.04f) {
        toHand = {0.0f, -0.04f, 0.0f};
        d = 0.04f;
    }
    const float maxReach = L1 + L2 - 0.012f;
    const float minReach = std::abs(L1 - L2) + 0.04f;
    Vec3 clampedHand = handLocal;
    if (d > maxReach) {
        clampedHand = shoulderLocal + (toHand / d) * maxReach;
        d = maxReach;
    } else if (d < minReach) {
        clampedHand = shoulderLocal + (toHand / d) * minReach;
        d = minReach;
    }

    const Vec3 fwdDir = (clampedHand - shoulderLocal) / d;
    // Law of cosines for exact elbow projection along shoulder->hand + bend perpendicular toward pole
    const float a = clampf((L1 * L1 + d * d - L2 * L2) / (2.0f * d), -L1, L1);
    const float h = std::sqrt(std::max(0.0f, L1 * L1 - a * a));

    Vec3 poleVec = elbowPoleLocal - shoulderLocal;
    poleVec = poleVec - fwdDir * dot3(poleVec, fwdDir);
    if (poleVec.lengthSq() < 1e-5f) {
        poleVec = {0.0f, -1.0f, 0.0f};
        poleVec = poleVec - fwdDir * dot3(poleVec, fwdDir);
    }
    const Vec3 bendDir = poleVec.normalized();
    const Vec3 elbowLocal = shoulderLocal + fwdDir * a + bendDir * h;

    // Draw smooth upper arm + forearm + contoured hand!
    const Mat4 upperM = rootM * buildCapsuleSegmentMatrix(shoulderLocal, elbowLocal, 0.114f);
    drawMesh(vk, capsuleLimbMesh_, upperM, sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);

    const Mat4 foreM = rootM * buildCapsuleSegmentMatrix(elbowLocal, clampedHand, 0.098f);
    drawMesh(vk, capsuleLimbMesh_, foreM, sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);

    const Mat4 handM = rootM * Mat4::translation(clampedHand.x, clampedHand.y, clampedHand.z) *
                       Mat4::scale(0.095f, 0.092f, 0.086f);
    drawMesh(vk, sphereMesh_, handM, skinCol, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);
}

void Renderer3D::drawCharacter(VulkanContext& vk, const Vec2& pos, float groundY, float yaw,
                               float animPhase, ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                               bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                               float aimPitch, OutfitId outfit, bool shadowPass,
                               bool jumping, bool panicking, bool isPolice, bool isSwat,
                               const CharacterCustomState* customChar,
                               const WeaponSlotState* customWep,
                               const NpcCharacter* npcPtr, bool nightMode,
                               const WeaponPoseState* weaponPose) {
    const float hScale = npcPtr ? npcPtr->heightScale : 1.0f;
    const WeaponPoseState defPose{};
    const WeaponPoseState& pose = weaponPose ? *weaponPose : defPose;

    // Subtle combat torso turn when aiming firearms or throwing a punch (authentic athletic shooting stance!)
    const float meleeCurve = std::sin(clampf(pose.meleeSwingAnim, 0.0f, 1.0f) * kPi);
    const float aimTorsoTwist = (alive && armed && weapon != WeaponId::Fists)
        ? (-0.24f * clampf(pose.aimBlend, 0.0f, 1.0f)) : 0.0f;
    const float torsoPunchTwist = (weapon == WeaponId::Fists && meleeCurve > 0.01f)
        ? (pose.meleeSwingRight ? 0.24f : -0.24f) * meleeCurve : aimTorsoTwist;

    Mat4 root = Mat4::translation(pos.x, groundY, pos.z) *
                Mat4::rotationAxis({0, 1, 0}, -yaw) *
                Mat4::scale(hScale, hScale, hScale);

    if (!alive) {
        const float fp = npcPtr ? npcPtr->fallProgress : 1.0f;
        root = root * Mat4::translation(0.0f, 0.14f, 0.0f) * Mat4::rotationAxis({0, 0, 1}, -kHalfPi * fp);
    }

    const float stride = alive ? std::sin(animPhase) * (jumping ? 0.16f : 0.44f) : 0.0f;
    const float armSwing = alive ? -stride * 0.65f : 0.0f;
    const ColorRGBA shoeCol{28, 30, 36, 255};
    const ColorRGBA beltCol{32, 30, 28, 255};

    // 1. Articulated Two-Segment Legs (Thigh + Shin + Knee Bend + Sculpted Sneaker & Midsole!)
    for (float s : {-1.0f, 1.0f}) {
        const float hipAng = s * stride;
        const float kneeAng = alive ? std::max(0.0f, -s * std::cos(animPhase) * 0.36f) : 0.0f;
        const Mat4 hipM = root * Mat4::translation(0.0f, 0.85f, s * 0.105f) *
                          Mat4::rotationAxis({0, 0, 1}, hipAng);
        drawMesh(vk, capsuleLimbMesh_, hipM * Mat4::scale(0.155f, 0.41f, 0.155f),
                 bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        const Mat4 kneeM = hipM * Mat4::translation(0.0f, -0.41f, 0.0f) *
                           Mat4::rotationAxis({0, 0, 1}, -kneeAng);
        drawMesh(vk, capsuleLimbMesh_, kneeM * Mat4::scale(0.128f, 0.38f, 0.128f),
                 bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        drawMesh(vk, cubeMesh_, kneeM * Mat4::translation(0.045f, -0.418f, 0.0f) * Mat4::scale(0.245f, 0.032f, 0.112f),
                 {218, 222, 228, 255}, 0.72f, 0.02f, 0.0f, MatId::Standard, shadowPass);
        drawMesh(vk, sphereMesh_, kneeM * Mat4::translation(0.042f, -0.392f, 0.0f) * Mat4::scale(0.24f, 0.088f, 0.118f),
                 shoeCol, 0.56f, 0.06f, 0.0f, MatId::Standard, shadowPass);
    }

    // 2. Clean Tucked Pelvis, Slim Flush Waistband & Anatomical Draped Jacket Torso (ZERO pelvis bulge!)
    const Mat4 upperRoot = (std::abs(torsoPunchTwist) > 0.001f)
        ? (root * Mat4::rotationAxis({0, 1, 0}, torsoPunchTwist))
        : root;

    drawMesh(vk, sphereMesh_, root * Mat4::translation(0.0f, 0.82f, 0.0f) * Mat4::scale(0.175f, 0.135f, 0.295f),
             bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
    if (!shadowPass) {
        drawMesh(vk, cylMesh_, root * Mat4::translation(0.0f, 0.84f, 0.0f) * Mat4::scale(0.185f, 0.036f, 0.310f),
                 beltCol, 0.42f, 0.25f, 0.0f, MatId::Standard, false);
    }
    drawMesh(vk, torsoMesh_, upperRoot * Mat4::translation(0.0f, 0.84f, 0.0f) * Mat4::scale(0.305f, 0.635f, 0.445f),
             top, 0.74f, 0.04f, 0.0f, MatId::FabricCloth, shadowPass);
    if (!shadowPass) {
        drawMesh(vk, sphereMesh_, upperRoot * Mat4::translation(0.09f, 1.38f, 0.0f) * Mat4::scale(0.11f, 0.14f, 0.14f),
                 {228, 232, 238, 255}, 0.72f, 0.02f, 0.0f, MatId::FabricCloth, false);
    }
    if (isSwat || outfit == OutfitId::TacOpsArmor) {
        drawMesh(vk, torsoMesh_, upperRoot * Mat4::translation(0.0f, 0.96f, 0.0f) * Mat4::scale(0.34f, 0.46f, 0.47f),
                 {30, 34, 42, 255}, 0.56f, 0.18f, 0.0f, MatId::Standard, shadowPass);
    }
    // Optional player tactical backpack / duffle gear
    if (!shadowPass && customChar && customChar->backGearStyle > 0) {
        const ColorRGBA bgCol = (customChar->backGearStyle == 1) ? ColorRGBA{46, 52, 46, 255}
                              : ((customChar->backGearStyle == 2) ? ColorRGBA{32, 34, 40, 255}
                                                                  : ColorRGBA{48, 178, 224, 255});
        drawMesh(vk, cubeMesh_, upperRoot * Mat4::translation(-0.185f, 1.18f, 0.0f) * Mat4::scale(0.15f, 0.34f, 0.28f),
                 bgCol, 0.65f, 0.12f, (customChar->backGearStyle == 3) ? 0.5f : 0.0f, MatId::FabricCloth, false);
    }

    // 3. Neck, Sculpted Head, Layered Hair, Ears & Refined Facial Details (zero cartoon egg-head look!)
    drawMesh(vk, cylMesh_, upperRoot * Mat4::translation(0.0f, 1.43f, 0.0f) * Mat4::scale(0.105f, 0.135f, 0.105f),
             skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

    const float headLook = npcPtr ? npcPtr->headLookYaw : 0.0f;
    const Mat4 headM = upperRoot * Mat4::translation(0.0f, 1.63f, 0.0f) * Mat4::rotationAxis({0, 1, 0}, -headLook);
    drawMesh(vk, sphereMesh_, headM * Mat4::scale(0.205f, 0.245f, 0.188f),
             skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

    if (!shadowPass) {
        // Subtle anatomical jaw/chin + ears + nose bridge
        drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.015f, -0.048f, 0.0f) * Mat4::scale(0.155f, 0.135f, 0.135f),
                 skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
        drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.102f, -0.008f, 0.0f) * Mat4::scale(0.026f, 0.036f, 0.024f),
                 skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
        for (float es : {-1.0f, 1.0f}) {
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.005f, -0.004f, es * 0.094f) * Mat4::scale(0.032f, 0.052f, 0.024f),
                     skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
        }

        const ColorRGBA hairCol = customChar
            ? getPlayerHairColor(outfit, customChar->hairColorIdx)
            : (isPolice ? ColorRGBA{24, 42, 86, 255} : ColorRGBA{26, 22, 20, 255});
        if (isPolice || isSwat) {
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.01f, 0.058f, 0.0f) * Mat4::scale(0.232f, 0.155f, 0.214f),
                     isSwat ? ColorRGBA{28, 32, 38, 255} : ColorRGBA{22, 42, 92, 255}, 0.45f, 0.15f, 0.0f, MatId::Standard, false);
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.098f, 0.032f, 0.0f) * Mat4::scale(0.105f, 0.020f, 0.152f),
                     {20, 22, 26, 255}, 0.35f, 0.15f, 0.0f, MatId::Standard, false);
        } else {
            // Layered natural modern Korean hairstyle (crown volume + tapered nape + subtle fringe)
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.014f, 0.058f, 0.0f) * Mat4::scale(0.222f, 0.172f, 0.208f),
                     hairCol, 0.76f, 0.03f, 0.0f, MatId::Standard, false);
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.054f, -0.005f, 0.0f) * Mat4::scale(0.165f, 0.168f, 0.186f),
                     hairCol, 0.76f, 0.03f, 0.0f, MatId::Standard, false);
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.048f, 0.068f, 0.012f) * Mat4::scale(0.135f, 0.088f, 0.175f),
                     hairCol, 0.76f, 0.03f, 0.0f, MatId::Standard, false);
        }
        if ((customChar && customChar->glassesStyle > 0) || (npcPtr && npcPtr->glassesStyle > 0)) {
            for (float es : {-1.0f, 1.0f}) {
                drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.094f, 0.012f, es * 0.038f) * Mat4::scale(0.024f, 0.034f, 0.052f),
                         {14, 16, 22, 255}, 0.06f, 0.45f, 0.0f, MatId::CarGlass, false);
            }
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.099f, 0.015f, 0.0f) * Mat4::scale(0.012f, 0.010f, 0.038f),
                     {24, 26, 30, 255}, 0.30f, 0.75f, 0.0f, MatId::Standard, false);
        } else {
            for (float es : {-1.0f, 1.0f}) {
                // Natural subtle dark eyebrows + recessed eyes (never oversized cartoon dot eyes!)
                drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.091f, 0.028f, es * 0.035f) * Mat4::scale(0.012f, 0.008f, 0.030f),
                         hairCol, 0.75f, 0.0f, 0.0f, MatId::Standard, false);
                drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.092f, 0.012f, es * 0.035f) * Mat4::scale(0.012f, 0.012f, 0.018f),
                         {32, 28, 26, 255}, 0.32f, 0.0f, 0.0f, MatId::Standard, false);
            }
        }
    }

    // 4. Articulated 2-Bone Analytical IK Arms + Full Weapon-Holding & Handling Animations!
    const Vec3 shLeft {0.0f, 1.365f, -0.214f};
    const Vec3 shRight{0.0f, 1.365f,  0.214f};

    const bool handsUp = alive && (panicking || (npcPtr && npcPtr->activity == NpcActivity::HandsUp));
    const bool holdingFirearm = alive && armed && (weapon != WeaponId::Fists);

    if (handsUp) {
        drawArmIK(vk, upperRoot, shLeft,  {0.20f, 1.72f, -0.32f}, {-0.08f, 1.38f, -0.45f}, top, skin, shadowPass);
        drawArmIK(vk, upperRoot, shRight, {0.20f, 1.72f,  0.32f}, {-0.08f, 1.38f,  0.45f}, top, skin, shadowPass);
    } else if (holdingFirearm) {
        const float aimT = clampf(pose.aimBlend, 0.0f, 1.0f);
        const float smoothAim = aimT * aimT * (3.0f - 2.0f * aimT);
        const float recoil = clampf(pose.recoilAnim, 0.0f, 1.0f);
        const float equip = clampf(pose.equipAnim, 0.0f, 1.0f);
        const float reloadBell = (pose.reloadFrac > 0.001f)
            ? std::sin(clampf(pose.reloadFrac, 0.0f, 1.0f) * kPi) : 0.0f;
        const float walkBobY = std::sin(animPhase * 2.0f) * 0.014f * (1.0f - smoothAim * 0.72f);
        const float walkSwayZ = std::sin(animPhase) * 0.018f * (1.0f - smoothAim * 0.75f);

        Vec3 carryPos{}, aimPos{};
        float carryYaw = 0.0f, carryPitch = -0.35f, carryRoll = 0.0f;
        float aimYaw = 0.0f, aimRoll = 0.0f;

        if (weapon == WeaponId::Pistol || weapon == WeaponId::HeavyRevolver) {
            // Tactical right-hand low-ready clearly visible beside right hip -> raised two-handed Weaver combat stance!
            carryPos   = {0.29f, 1.06f, 0.30f};
            carryYaw   = 0.08f;
            carryPitch = -0.36f;
            aimPos     = {0.44f, 1.37f, 0.22f};
            aimYaw     = 0.20f;
        } else if (weapon == WeaponId::Minigun) {
            // Heavy hip-slung gatling carry -> braced firing stance
            carryPos   = {0.28f, 0.92f, 0.28f};
            carryYaw   = 0.14f;
            carryPitch = -0.10f;
            aimPos     = {0.32f, 0.99f, 0.26f};
            aimYaw     = 0.18f;
        } else if (weapon == WeaponId::HeavyLauncher) {
            // Over-the-right-shoulder RPG rocket tube carry!
            carryPos   = {0.18f, 1.46f, 0.25f};
            carryYaw   = 0.05f;
            carryPitch = 0.10f;
            aimPos     = {0.22f, 1.48f, 0.24f};
            aimYaw     = 0.20f;
        } else {
            // Two-handed Carbine / SMG / Shotgun / Sniper / LMG:
            // Right-flank tactical low-ready (stock, mag & barrel clearly visible!) -> shouldered combat aim!
            carryPos   = {0.28f, 1.15f, 0.26f};
            carryYaw   = 0.22f;
            carryPitch = -0.26f;
            carryRoll  = -0.08f;
            aimPos     = {0.33f, 1.36f, 0.26f};
            aimYaw     = 0.24f; // Compensates for -0.24 rad athletic torso aim twist so muzzle aims straight ahead!
        }

        // Smooth blend from carry -> aim + recoil kickback + reload inspection + holster draw sweep!
        Vec3 wPos = lerp3(carryPos, aimPos, smoothAim);
        float wYaw   = lerpf(carryYaw, aimYaw, smoothAim);
        float wPitch = lerpf(carryPitch, clampf(aimPitch, -0.58f, 0.58f), smoothAim);
        float wRoll  = lerpf(carryRoll, aimRoll, smoothAim);

        wPos.y += walkBobY;
        wPos.z += walkSwayZ;

        // Recoil kickback pushes stock back into shoulder & kicks muzzle upward!
        wPos.x -= recoil * 0.055f;
        wPos.y += recoil * 0.015f;
        wPitch += recoil * 0.19f;

        // Reload tilt elevates weapon slightly & rolls ejection port/mag well open for left hand
        if (reloadBell > 0.001f) {
            wPos.x += reloadBell * 0.04f;
            wPos.y += reloadBell * 0.06f;
            wYaw   += reloadBell * 0.34f;
            wPitch += reloadBell * 0.26f;
            wRoll  -= reloadBell * 0.52f;
        }

        // Holster draw sweep when switching weapons!
        if (equip > 0.001f) {
            const float e2 = equip * equip;
            wPos.y -= e2 * 0.32f;
            wPos.x -= e2 * 0.10f;
            wPitch -= e2 * 0.72f;
            wYaw   -= e2 * 0.35f;
        }

        constexpr float kThirdPersonWepScale = 1.26f;
        const Mat4 weaponLocalM =
            Mat4::translation(wPos.x, wPos.y, wPos.z) *
            Mat4::rotationAxis({0, 1, 0}, wYaw) *
            Mat4::rotationAxis({0, 0, 1}, wPitch) *
            Mat4::rotationAxis({1, 0, 0}, wRoll) *
            Mat4::scale(kThirdPersonWepScale, kThirdPersonWepScale, kThirdPersonWepScale);

        const Mat4 weaponWorldM = upperRoot * weaponLocalM;
        drawDetailedWeapon3D(vk, weapon, customWep, weaponWorldM, muzzleFlash, pose.minigunSpin, shadowPass, false);

        // Right hand ALWAYS locked onto the weapon's pistol grip (local origin)!
        const Vec3 rHandLocal = weaponLocalM.transformPoint({-0.010f, -0.025f, 0.006f});
        const Vec3 rPoleLocal = {wPos.x - 0.18f, wPos.y - 0.24f, 0.48f};
        drawArmIK(vk, upperRoot, shRight, rHandLocal, rPoleLocal, top, skin, shadowPass);

        // Left hand: grips foregrip (rifles/SMG/shotgun/minigun/RPG), joins two-handed pistol aim,
        // swaps magazine during reload, or swings naturally during casual pistol carry!
        Vec3 lHandLocal{};
        Vec3 lPoleLocal{-0.14f, 1.04f, -0.44f};
        const bool twoHand = isTwoHandedFirearm(weapon);
        const Vec3 naturalLeftWalk{
            0.05f + std::sin(-armSwing) * 0.36f,
            0.90f + std::abs(armSwing) * 0.04f,
            -0.25f
        };

        if (reloadBell > 0.001f) {
            // Left hand drops to hip pouch (0..0.45) then slaps fresh mag into mag well (0.45..1.0)!
            const Vec3 hipPouch{0.06f, 0.88f, -0.22f};
            const Vec3 magWell = weaponLocalM.transformPoint(weaponMagWellLocal(weapon));
            const float rf = clampf(pose.reloadFrac, 0.0f, 1.0f);
            if (rf < 0.45f) {
                const float k = std::sin((rf / 0.45f) * kHalfPi);
                lHandLocal = lerp3(weaponLocalM.transformPoint(weaponForegripLocal(weapon)), hipPouch, k);
            } else {
                const float k = std::sin(((rf - 0.45f) / 0.55f) * kPi);
                lHandLocal = lerp3(hipPouch, magWell, clampf(k * 1.15f, 0.0f, 1.0f));
            }
            lPoleLocal = {0.02f, 0.96f, -0.46f};
        } else if (twoHand) {
            lHandLocal = weaponLocalM.transformPoint(weaponForegripLocal(weapon));
            lPoleLocal = {wPos.x - 0.10f, wPos.y - 0.34f, -0.42f};
        } else {
            // Pistol / Revolver: natural left walk swing at low-ready -> snaps to two-handed Weaver grip on aim!
            const Vec3 twoHandPistolGrip = weaponLocalM.transformPoint(weaponForegripLocal(weapon));
            lHandLocal = lerp3(naturalLeftWalk, twoHandPistolGrip, smoothAim);
            lPoleLocal = lerp3({-0.12f, 1.06f, -0.38f}, {0.08f, 1.05f, -0.46f}, smoothAim);
        }
        drawArmIK(vk, upperRoot, shLeft, lHandLocal, lPoleLocal, top, skin, shadowPass);
    } else if (alive && weapon == WeaponId::Fists && meleeCurve > 0.01f) {
        // Athletic Boxing Jab / Cross Punch Animation!
        const Vec3 guardL{0.26f, 1.38f, -0.15f};
        const Vec3 guardR{0.26f, 1.38f,  0.15f};
        const Vec3 punchL{0.54f, 1.42f, -0.04f};
        const Vec3 punchR{0.54f, 1.42f,  0.04f};
        const Vec3 lHand = pose.meleeSwingRight ? guardL : lerp3(guardL, punchL, meleeCurve);
        const Vec3 rHand = pose.meleeSwingRight ? lerp3(guardR, punchR, meleeCurve) : guardR;
        drawArmIK(vk, upperRoot, shLeft,  lHand, {0.02f, 1.12f, -0.44f}, top, skin, shadowPass);
        drawArmIK(vk, upperRoot, shRight, rHand, {0.02f, 1.12f,  0.44f}, top, skin, shadowPass);
    } else {
        // Natural walking arms + NPC rain umbrella + Living Seoul Citizen 3D Held Props!
        const Vec3 lHand{
            0.04f + std::sin(-armSwing) * 0.36f,
            0.88f + std::abs(armSwing) * 0.04f,
            -0.245f
        };
        const bool npcPhone = (npcPtr && alive && !npcPtr->holdingUmbrella &&
                               (npcPtr->activity == NpcActivity::CheckingPhone || npcPtr->heldProp == NpcHeldProp::Smartphone));
        const bool npcCoffee = (npcPtr && alive && !npcPtr->holdingUmbrella && !npcPhone &&
                                npcPtr->heldProp == NpcHeldProp::CoffeeCup);
        const bool npcCarryBag = (npcPtr && alive && !npcPtr->holdingUmbrella && !npcPhone &&
                                  (npcPtr->heldProp == NpcHeldProp::Briefcase || npcPtr->heldProp == NpcHeldProp::ShoppingBag));

        Vec3 rHand{0.04f + std::sin(armSwing) * 0.36f, 0.88f + std::abs(armSwing) * 0.04f, 0.245f};
        Vec3 rPole{-0.12f, 1.08f, 0.34f};
        if (npcPtr && npcPtr->holdingUmbrella && alive) {
            rHand = {0.16f, 1.24f, 0.24f};
        } else if (npcPhone) {
            rHand = {0.27f, 1.30f, 0.11f};
            rPole = {0.06f, 1.04f, 0.36f};
        } else if (npcCoffee) {
            rHand = {0.22f, 1.15f, 0.22f};
            rPole = {0.02f, 1.02f, 0.36f};
        } else if (npcCarryBag) {
            rHand = {0.06f + std::sin(armSwing) * 0.12f, 0.86f, 0.26f};
        }
        drawArmIK(vk, upperRoot, shLeft,  lHand, {-0.12f, 1.08f, -0.34f}, top, skin, shadowPass);
        drawArmIK(vk, upperRoot, shRight, rHand, rPole, top, skin, shadowPass);

        // Render 3D Seoul Citizen Held Props cleanly attached to right hand!
        if (npcPtr && alive && !shadowPass && !npcPtr->holdingUmbrella) {
            if (npcPhone) {
                // Sleek smartphone body + glowing bright screen held in right hand!
                const Mat4 phoneM = upperRoot * Mat4::translation(rHand.x + 0.03f, rHand.y + 0.05f, rHand.z - 0.02f) *
                                    Mat4::rotationAxis({0, 0, 1}, -0.42f);
                drawMesh(vk, cubeMesh_, phoneM * Mat4::scale(0.016f, 0.14f, 0.075f),
                         {22, 24, 30, 255}, 0.26f, 0.65f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cubeMesh_, phoneM * Mat4::translation(-0.010f, 0.0f, 0.0f) * Mat4::scale(0.006f, 0.122f, 0.064f),
                         {178, 226, 255, 255}, 0.12f, 0.0f, nightMode ? 1.85f : 0.72f, MatId::EmissiveGlow, false);
            } else if (npcCoffee) {
                // Iconic Seoul Iced Americano cup + white lid + green straw!
                const Mat4 cupM = upperRoot * Mat4::translation(rHand.x + 0.02f, rHand.y - 0.06f, rHand.z);
                drawMesh(vk, cylMesh_, cupM * Mat4::scale(0.076f, 0.145f, 0.076f),
                         {62, 42, 28, 255}, 0.18f, 0.08f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cylMesh_, cupM * Mat4::translation(0.0f, 0.05f, 0.0f) * Mat4::scale(0.082f, 0.048f, 0.082f),
                         {192, 162, 126, 255}, 0.85f, 0.0f, 0.0f, MatId::Standard, false); // Cardboard cup sleeve
                drawMesh(vk, cylMesh_, cupM * Mat4::translation(0.0f, 0.145f, 0.0f) * Mat4::scale(0.082f, 0.018f, 0.082f),
                         {244, 246, 250, 255}, 0.28f, 0.05f, 0.0f, MatId::Standard, false); // White lid
                drawMesh(vk, cylMesh_, cupM * Mat4::translation(0.01f, 0.155f, 0.0f) * Mat4::scale(0.010f, 0.065f, 0.010f),
                         {32, 142, 74, 255}, 0.35f, 0.0f, 0.0f, MatId::Standard, false);     // Green straw
            } else if (npcPtr->heldProp == NpcHeldProp::Briefcase) {
                // Seoul Gangnam/Yeouido Office Executive Leather Briefcase
                const Mat4 bagM = upperRoot * Mat4::translation(rHand.x, rHand.y - 0.19f, rHand.z + 0.03f);
                drawMesh(vk, cubeMesh_, bagM * Mat4::scale(0.38f, 0.27f, 0.09f),
                         {38, 30, 26, 255}, 0.48f, 0.12f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cubeMesh_, bagM * Mat4::translation(0.0f, 0.14f, 0.0f) * Mat4::scale(0.11f, 0.04f, 0.025f),
                         {28, 24, 22, 255}, 0.42f, 0.25f, 0.0f, MatId::Standard, false);
                drawMesh(vk, cubeMesh_, bagM * Mat4::translation(0.0f, 0.04f, 0.048f) * Mat4::scale(0.048f, 0.028f, 0.010f),
                         {216, 192, 120, 255}, 0.22f, 0.88f, 0.0f, MatId::CarClearcoat, false);
            } else if (npcPtr->heldProp == NpcHeldProp::ShoppingBag) {
                // Trendy Seoul Department-Store Shopping Bag
                const ColorRGBA bagCol = ((npcPtr->id & 1) == 0)
                    ? ColorRGBA{228, 198, 154, 255}
                    : ColorRGBA{214, 152, 168, 255};
                const Mat4 bagM = upperRoot * Mat4::translation(rHand.x, rHand.y - 0.21f, rHand.z + 0.04f);
                drawMesh(vk, cubeMesh_, bagM * Mat4::scale(0.28f, 0.31f, 0.12f),
                         bagCol, 0.72f, 0.02f, 0.0f, MatId::FabricCloth, false);
                drawMesh(vk, cubeMesh_, bagM * Mat4::translation(0.0f, 0.165f, 0.0f) * Mat4::scale(0.12f, 0.055f, 0.025f),
                         {48, 44, 42, 255}, 0.65f, 0.0f, 0.0f, MatId::Standard, false);
            }
        }
    }

    // 5. Korean Rain Umbrella when raining!
    if (npcPtr && npcPtr->holdingUmbrella && alive && !shadowPass) {
        drawMesh(vk, cylMesh_, root * Mat4::translation(0.16f, 1.22f, 0.24f) * Mat4::scale(0.028f, 0.96f, 0.028f),
                 {44, 48, 54, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, false);
        drawMesh(vk, coneMesh_, root * Mat4::translation(0.16f, 2.04f, 0.24f) * Mat4::scale(1.22f, 0.32f, 1.22f),
                 npcPtr->umbrellaColor, 0.56f, 0.05f, 0.0f, MatId::FabricCloth, false);
    }
    (void)nightMode;
}

void Renderer3D::drawAlphaAndEffects(VulkanContext& vk, const GameWorld& world,
                                     const CameraState& camera, const Vec2& focus) {
    const CityMap& city = world.city();
    const bool nightMode = world.isNight();
    const float t = world.elapsedTime();

    // 1. Banpo Bridge Moonlight Rainbow Fountain translucent water curtains!
    drawMesh(vk, banpoFountainMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.10f, 0.0f, nightMode ? 1.55f : 0.35f,
             MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);

    // 2. Smoked Curved Vehicle Glass + Underglow + Sirens + Nitro Flames!
    for (const auto& v : world.vehicles()) {
        if ((v.pos - focus).lengthSq() > 340.0f * 340.0f) continue;
        const float gy = city.surfaceHeightAt(v.pos, v.posY);
        drawVehicle(vk, v, gy, nightMode, false, true, false, &world.player());
    }

    // 2B. Real Translucent Tempered Architectural Glass for Seoul Bus Shelters, Subway Canopies & Street Kiosks (v6.4.0!)
    {
        static constexpr float kPropRadii[3] = {150.0f, 225.0f, 290.0f};
        const int pd = std::clamp(static_cast<int>(settings_.propDensity), 0, 2);
        const float propRadius = kPropRadii[pd];
        const ColorRGBA kWhite{255, 255, 255, 255};
        visIdxBuf_.clear();
        city.queryPropsInRadius(focus, propRadius, visIdxBuf_);
        for (int pi : visIdxBuf_) {
            const StreetProp& p = city.streetProps()[static_cast<std::size_t>(pi)];
            if (p.kind != StreetPropKind::BusShelter &&
                p.kind != StreetPropKind::SubwayEntrance &&
                p.kind != StreetPropKind::StreetKiosk) {
                continue;
            }
            const float propBaseY = (p.baseY > -1e8f) ? p.baseY : city.surfaceHeightAt(p.pos);
            const float gy = propBaseY + p.posY;
            const float distSq = (Vec3{p.pos.x, gy, p.pos.z} - camera.position).lengthSq();
            if (distSq > propRadius * propRadius) continue;

            Mat4 baseM = Mat4::translation(p.pos.x, gy, p.pos.z) *
                         Mat4::rotationAxis({0, 1, 0}, -p.yaw);
            if (p.broken) {
                baseM = baseM * Mat4::rotationAxis({1, 0, 0}, p.roll) * Mat4::rotationAxis({0, 0, 1}, p.pitch);
            }
            if (p.kind == StreetPropKind::BusShelter) {
                drawMesh(vk, busShelterGlassMesh_, baseM, kWhite, 0.04f, 0.14f, 0.0f,
                         MatId::StreetGlass, false, WorldPipelineKind::Alpha);
            } else if (p.kind == StreetPropKind::SubwayEntrance) {
                drawMesh(vk, subwayCanopyGlassMesh_, baseM, kWhite, 0.04f, 0.14f, 0.0f,
                         MatId::StreetGlass, false, WorldPipelineKind::Alpha);
            } else {
                drawMesh(vk, streetKioskGlassMesh_, baseM, kWhite, 0.04f, 0.14f, 0.0f,
                         MatId::StreetGlass, false, WorldPipelineKind::Alpha);
            }
        }
    }

    // 3. Sleek Compact Curb POI Beacons (no immersion-breaking giant arcade rings!) + Mission / GPS Markers
    for (const auto& poi : city.pois()) {
        if ((poi.pos - focus).lengthSq() > 85.0f * 85.0f) continue;
        const float gy = city.surfaceHeightAt(poi.pos) + 0.08f;
        const float pulse = 1.0f + 0.04f * std::sin(t * 3.0f);
        ColorRGBA rc = poi.markerColor;
        rc.a = 115;
        const float r = std::min(1.55f, poi.radius * 0.32f) * pulse;
        Mat4 rm = Mat4::translation(poi.pos.x, gy, poi.pos.z) * Mat4::scale(r, 1.0f, r);
        drawMesh(vk, ringMesh_, rm, rc, 0.2f, 0.0f, 0.65f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
    }
    if (world.mission().kind != ActiveMissionKind::None) {
        const Vec2 mp = world.mission().currentObjectivePos;
        if ((mp - focus).lengthSq() < 1200.0f * 1200.0f) {
            const float gy = city.surfaceHeightAt(mp) + 0.16f;
            Mat4 rm = Mat4::translation(mp.x, gy, mp.z) * Mat4::scale(world.mission().objectiveRadius, 1.0f, world.mission().objectiveRadius);
            drawMesh(vk, ringMesh_, rm, {255, 206, 48, 210}, 0.2f, 0.0f, 1.8f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
        }
    }
    if (world.player().hasWaypoint) {
        const Vec2 wp = world.player().waypointPos;
        if ((wp - focus).lengthSq() < 2500.0f * 2500.0f) {
            const float gy = city.surfaceHeightAt(wp) + 0.20f;
            Mat4 rm = Mat4::translation(wp.x, gy, wp.z) * Mat4::scale(5.0f, 1.0f, 5.0f);
            drawMesh(vk, ringMesh_, rm, {72, 226, 255, 220}, 0.2f, 0.0f, 2.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
            drawBox(vk, {wp.x, gy + 45.0f, wp.z}, {0.8f, 90.0f, 0.8f}, 0.0f,
                    {72, 226, 255, 86}, 0.1f, 0.0f, 1.8f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
        }
    }

    // 4. World Pickups (bobbing glowing cash / health / briefcases)
    for (const auto& pk : world.pickups()) {
        if (pk.collected || (pk.pos - focus).lengthSq() > 140.0f * 140.0f) continue;
        const float gy = city.surfaceHeightAt(pk.pos) + 0.65f + 0.16f * std::sin(pk.bobPhase);
        const ColorRGBA pc = (pk.kind == PickupKind::CashBundle) ? ColorRGBA{74, 224, 118, 255}
                           : ((pk.kind == PickupKind::HealthKit) ? ColorRGBA{242, 72, 78, 255}
                                                                 : ColorRGBA{248, 206, 62, 255});
        drawBox(vk, {pk.pos.x, gy, pk.pos.z}, {0.55f, 0.36f, 0.36f}, pk.bobPhase,
                pc, 0.25f, 0.15f, 1.15f, MatId::EmissiveGlow, false, WorldPipelineKind::Opaque);
    }

    // 5. Tire Skid Marks
    for (const auto& sk : world.skidMarks()) {
        if ((sk.a - focus).lengthSq() > 150.0f * 150.0f) continue;
        const Vec2 d = sk.b - sk.a;
        const float len = d.length();
        if (len < 0.05f) continue;
        const Vec2 mid = (sk.a + sk.b) * 0.5f;
        const float gy = city.surfaceHeightAt(mid, world.player().posY) + 0.04f;
        const float yaw = std::atan2(d.z, d.x);
        drawBox(vk, {mid.x, gy, mid.z}, {len + 0.08f, 0.02f, 0.24f}, yaw,
                {18, 18, 20, static_cast<std::uint8_t>(clampf(sk.alpha, 0.0f, 1.0f) * 185.0f)},
                0.92f, 0.0f, 0.0f, MatId::Standard, false, WorldPipelineKind::Alpha);
    }

    // 6. Glowing Bullet Tracers + Optional 3rd-Person Tactical Laser Sight Beam!
    for (const auto& tr : world.tracers()) {
        const Vec3 mid = (tr.start + tr.end) * 0.5f;
        const Vec3 dir = tr.end - tr.start;
        const float len = dir.length();
        if (len < 0.05f) continue;
        const float yaw = std::atan2(dir.z, dir.x);
        drawBox(vk, mid, {len, 0.05f, 0.05f}, yaw, tr.color,
                0.1f, 0.0f, 3.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
    }
    {
        const auto& pl = world.player();
        const float gy = std::max(city.surfaceHeightAt(pl.pos, pl.posY), pl.posY);
        const bool isThirdPerson = (camera.position - Vec3{pl.pos.x, gy + 1.56f, pl.pos.z}).lengthSq() > 0.70f;
        if (isThirdPerson && pl.drivingVehicleId < 0 && !pl.wasted && pl.currentWeapon != WeaponId::Fists) {
            const auto& slot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
            if (slot.laserSightColor > 0 && (pl.weaponAimBlend > 0.25f || pl.aiming)) {
                const Vec2 pf = forwardFromHeading(pl.facingYaw);
                const Vec2 pr = rightFromHeading(pl.facingYaw);
                const Vec3 aimDir = directionFromYawPitch(pl.facingYaw, pl.aimPitch);
                const Vec3 beamStart = hasPlayerLaserOrigin_
                    ? lastPlayerLaserOrigin_
                    : Vec3{pl.pos.x + pf.x * 0.72f + pr.x * 0.26f, gy + 1.36f, pl.pos.z + pf.z * 0.72f + pr.z * 0.26f};
                Vec3 beamEnd = beamStart + aimDir * 35.0f;
                if (pl.crosshairHitDist > 1.2f && pl.crosshairHitDist < 120.0f) {
                    const Vec3 toCross = pl.crosshairPoint3D - beamStart;
                    const float cLen = toCross.length();
                    if (cLen > 1.2f && (toCross / cLen).dot(aimDir) > 0.72f) {
                        beamEnd = pl.crosshairPoint3D;
                    }
                }
                Vec3 diff = beamEnd - beamStart;
                const float rawLen = diff.length();
                if (rawLen > 0.45f) {
                    const float len = std::min(rawLen, 42.0f);
                    const Vec3 dir = diff / rawLen;
                    beamEnd = beamStart + dir * len;
                    const ColorRGBA lasCol = getLaserSightColorRGBA(slot.laserSightColor);
                    const ColorRGBA beamCol{lasCol.r, lasCol.g, lasCol.b, 155};
                    // Build clean +Y cylinder matrix from beamStart to beamEnd (cylMesh_ spans Y in [0..1])
                    const Vec3 ref = (std::abs(dir.y) < 0.92f) ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                    const Vec3 xAx = cross3(ref, dir).normalized();
                    const Vec3 zAx = cross3(dir, xAx).normalized();
                    const float rad = 0.012f;
                    Mat4 beamM{};
                    beamM.m[0]  = xAx.x * rad; beamM.m[1]  = xAx.y * rad; beamM.m[2]  = xAx.z * rad; beamM.m[3]  = 0.0f;
                    beamM.m[4]  = dir.x * len; beamM.m[5]  = dir.y * len; beamM.m[6]  = dir.z * len; beamM.m[7]  = 0.0f;
                    beamM.m[8]  = zAx.x * rad; beamM.m[9]  = zAx.y * rad; beamM.m[10] = zAx.z * rad; beamM.m[11] = 0.0f;
                    beamM.m[12] = beamStart.x; beamM.m[13] = beamStart.y; beamM.m[14] = beamStart.z; beamM.m[15] = 1.0f;
                    drawMesh(vk, cylMesh_, beamM, beamCol, 0.1f, 0.0f, 2.2f,
                             MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
                    drawMesh(vk, sphereMesh_, Mat4::translation(beamEnd.x, beamEnd.y, beamEnd.z) * Mat4::scale(0.065f, 0.065f, 0.065f),
                             lasCol, 0.1f, 0.0f, 3.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
                }
            }
        }
    }

    // 7. Particles (Spinning Brass Casings, Red 12-Gauge Shotgun Hulls, Dropped Magazines, Muzzle Smoke, Sparks & Rain Spray!)
    for (const auto& pt : world.particles()) {
        if (pt.kind == ParticleKind::BrassCasing) {
            const Mat4 cm = Mat4::translation(pt.pos.x, pt.pos.y, pt.pos.z) *
                            Mat4::rotationAxis({0, 1, 0}, pt.spinAngle) *
                            Mat4::rotationAxis({1, 0, 0}, pt.spinAngle * 1.4f) *
                            Mat4::scale(pt.size * 0.42f, pt.size * 1.65f, pt.size * 0.42f);
            drawMesh(vk, cylMesh_, cm, {236, 198, 86, 255}, 0.20f, 0.92f, 0.0f,
                     MatId::CarClearcoat, false, WorldPipelineKind::Opaque);
            continue;
        }
        if (pt.kind == ParticleKind::ShotgunShell) {
            const Mat4 sm = Mat4::translation(pt.pos.x, pt.pos.y, pt.pos.z) *
                            Mat4::rotationAxis({0, 1, 0}, pt.spinAngle) *
                            Mat4::rotationAxis({0, 0, 1}, pt.spinAngle * 1.3f);
            drawMesh(vk, cylMesh_, sm * Mat4::scale(pt.size * 0.56f, pt.size * 1.45f, pt.size * 0.56f),
                     {214, 42, 46, 255}, 0.34f, 0.18f, 0.0f, MatId::Standard, false, WorldPipelineKind::Opaque);
            drawMesh(vk, cylMesh_, sm * Mat4::translation(0.0f, -pt.size * 0.42f, 0.0f) * Mat4::scale(pt.size * 0.58f, pt.size * 0.48f, pt.size * 0.58f),
                     {234, 196, 84, 255}, 0.20f, 0.92f, 0.0f, MatId::CarClearcoat, false, WorldPipelineKind::Opaque);
            continue;
        }
        if (pt.kind == ParticleKind::DroppedMag) {
            const Mat4 mm = Mat4::translation(pt.pos.x, pt.pos.y, pt.pos.z) *
                            Mat4::rotationAxis({0, 1, 0}, pt.spinAngle * 0.7f) *
                            Mat4::rotationAxis({1, 0, 0}, pt.spinAngle) *
                            Mat4::scale(pt.size * 0.58f, pt.size * 1.75f, pt.size * 0.28f);
            drawMesh(vk, cubeMesh_, mm, {52, 56, 64, 255}, 0.34f, 0.76f, 0.0f,
                     MatId::Standard, false, WorldPipelineKind::Opaque);
            continue;
        }

        const float alphaF = clampf(pt.ttl / std::max(0.01f, pt.maxTtl), 0.0f, 1.0f);
        ColorRGBA col = pt.color;
        col.a = static_cast<std::uint8_t>(static_cast<float>(col.a) * alphaF);
        const bool additive = (pt.color.r > 210 && pt.color.g > 120 && pt.color.b < 110);
        Mat4 pm = Mat4::translation(pt.pos.x, pt.pos.y, pt.pos.z) * Mat4::scale(pt.size, pt.size, pt.size);
        drawMesh(vk, sphereMesh_, pm, col, 0.7f, 0.0f, additive ? 2.2f : 0.0f,
                 additive ? MatId::EmissiveGlow : MatId::Standard, false,
                 additive ? WorldPipelineKind::Additive : WorldPipelineKind::Alpha);
    }

    // 8. Dynamic Volumetric-Style Falling Rain Streaks in Rain/Storm weather!
    const float rainInt = world.rainIntensity();
    if (rainInt > 0.08f) {
        const int dropCount = static_cast<int>(rainInt * 180.0f);
        std::vector<Vertex3D> rainVerts;
        rainVerts.reserve(static_cast<std::size_t>(dropCount) * 6);
        const Vec3 right = Vec3{camera.target.z - camera.position.z, 0.0f, -(camera.target.x - camera.position.x)}.normalized();
        for (int i = 0; i < dropCount; ++i) {
            const float fi = static_cast<float>(i);
            const float rx = std::fmod(std::sin(fi * 12.9898f) * 43758.5453f, 1.0f);
            const float rz = std::fmod(std::sin(fi * 78.233f) * 24634.6345f, 1.0f);
            const float ry = std::fmod(std::abs(std::sin(fi * 37.719f)) + t * 2.4f, 1.0f);
            const Vec3 p{
                camera.position.x + rx * 28.0f,
                camera.position.y + 12.0f - ry * 18.0f,
                camera.position.z + rz * 28.0f
            };
            const Vec3 top = p + Vec3{0.08f, 0.65f, 0.0f};
            const Vec3 w = right * 0.014f;
            auto pushV = [&](const Vec3& vPos, float a) {
                Vertex3D vv{};
                vv.px = vPos.x; vv.py = vPos.y; vv.pz = vPos.z;
                vv.nx = 0; vv.ny = 1; vv.nz = 0;
                vv.r = 0.76f; vv.g = 0.84f; vv.b = 0.94f; vv.a = a;
                rainVerts.push_back(vv);
            };
            const float a = 0.32f * rainInt;
            pushV(p - w, a); pushV(p + w, a); pushV(top + w, a * 0.25f);
            pushV(p - w, a); pushV(top + w, a * 0.25f); pushV(top - w, a * 0.25f);
        }
        DrawPush dp{};
        dp.model = Mat4::identity();
        dp.tint[0] = dp.tint[1] = dp.tint[2] = dp.tint[3] = 1.0f;
        dp.pbrParams[0] = 0.2f; dp.pbrParams[2] = 0.25f;
        dp.pbrParams[3] = static_cast<float>(static_cast<int>(MatId::EmissiveGlow)) / 255.0f;
        vk.drawDynamicTriangles3D(rainVerts.data(), static_cast<std::uint32_t>(rainVerts.size()),
                                  WorldPipelineKind::Alpha, dp);
    }
}

void Renderer3D::drawFirstPersonOverlay(VulkanContext& vk, const GameWorld& world,
                                        const CameraState& camera, float adsBlend) {
    const auto& pl = world.player();
    if (pl.drivingVehicleId >= 0) return;

    // Clear depth cleanly so 1st-person arms & weapon never clip into Seoul walls!
    vk.clearSceneDepth();

    const Vec3 fwd = (camera.target - camera.position).normalized();
    const Vec3 right = cross3(fwd, camera.up).normalized();
    const Vec3 up = cross3(right, fwd).normalized();

    // Camera-attached local frame where +X = forward, +Y = up, +Z = right (matches drawDetailedWeapon3D!)
    Mat4 camBasis{};
    camBasis.m[0] = fwd.x;   camBasis.m[1] = fwd.y;   camBasis.m[2]  = fwd.z;   camBasis.m[3]  = 0.0f;
    camBasis.m[4] = up.x;    camBasis.m[5] = up.y;    camBasis.m[6]  = up.z;    camBasis.m[7]  = 0.0f;
    camBasis.m[8] = right.x; camBasis.m[9] = right.y; camBasis.m[10] = right.z; camBasis.m[11] = 0.0f;
    camBasis.m[12] = camera.position.x;
    camBasis.m[13] = camera.position.y;
    camBasis.m[14] = camera.position.z;
    camBasis.m[15] = 1.0f;

    const ColorRGBA sleeveCol = getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx);
    const ColorRGBA skinCol   = getPlayerSkinColor(pl.characterCustom.skinToneIdx);

    const float spdFrac = clampf(pl.velocity.length() / 7.5f, 0.0f, 1.0f);
    const float bobX = std::sin(pl.walkAnimPhase) * 0.014f * spdFrac * (1.0f - adsBlend * 0.78f);
    const float bobY = std::abs(std::cos(pl.walkAnimPhase)) * -0.015f * spdFrac * (1.0f - adsBlend * 0.78f);

    if (pl.currentWeapon == WeaponId::Fists) {
        // 1st-Person Boxing Fists & Forearms with Alternating Jab / Cross Animation!
        const float punch = std::sin(clampf(pl.meleeSwingAnim, 0.0f, 1.0f) * kPi);
        const float pL = pl.meleeSwingRight ? 0.0f : punch;
        const float pR = pl.meleeSwingRight ? punch : 0.0f;

        const Vec3 shL{-0.05f, -0.34f, -0.36f};
        const Vec3 shR{-0.05f, -0.34f,  0.36f};
        const Vec3 fL{0.34f + pL * 0.26f, -0.17f + pL * 0.08f + bobY, -0.17f + pL * 0.11f + bobX};
        const Vec3 fR{0.34f + pR * 0.26f, -0.17f + pR * 0.08f + bobY,  0.17f - pR * 0.11f + bobX};

        drawMesh(vk, capsuleLimbMesh_, camBasis * buildCapsuleSegmentMatrix(shL, fL, 0.095f),
                 sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, false);
        drawMesh(vk, capsuleLimbMesh_, camBasis * buildCapsuleSegmentMatrix(shR, fR, 0.095f),
                 sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, false);
        drawMesh(vk, sphereMesh_, camBasis * Mat4::translation(fL.x, fL.y, fL.z) * Mat4::scale(0.11f, 0.105f, 0.105f),
                 skinCol, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
        drawMesh(vk, sphereMesh_, camBasis * Mat4::translation(fR.x, fR.y, fR.z) * Mat4::scale(0.11f, 0.105f, 0.105f),
                 skinCol, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
        return;
    }

    const auto& spec = getWeaponSpec(pl.currentWeapon);
    const auto& slot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
    const float reloadFrac = (pl.reloadTimer > 0.001f && spec.reloadDuration > 0.01f)
        ? clampf(1.0f - (pl.reloadTimer / spec.reloadDuration), 0.0f, 1.0f) : 0.0f;
    const float reloadBell = (reloadFrac > 0.001f) ? std::sin(reloadFrac * kPi) : 0.0f;
    const float recoil = clampf(pl.weaponRecoilAnim, 0.0f, 1.0f);
    const float equip = clampf(pl.weaponEquipAnim, 0.0f, 1.0f);
    const float smoothAds = adsBlend * adsBlend * (3.0f - 2.0f * adsBlend);

    // Weapon-specific 1st-person hip-fire & iron-sight/optic ADS offsets
    float hipFwd = 0.42f, hipY = -0.22f, hipZ = 0.22f;
    float adsFwd = 0.34f, adsY = -0.108f, adsZ = 0.0f;
    if (pl.currentWeapon == WeaponId::Pistol || pl.currentWeapon == WeaponId::HeavyRevolver) {
        hipFwd = 0.45f; hipY = -0.19f; hipZ = 0.19f;
        adsFwd = 0.38f; adsY = -0.088f; adsZ = 0.0f;
    } else if (pl.currentWeapon == WeaponId::Minigun) {
        hipFwd = 0.42f; hipY = -0.30f; hipZ = 0.24f;
        adsFwd = 0.38f; adsY = -0.24f; adsZ = 0.15f;
    } else if (pl.currentWeapon == WeaponId::HeavyLauncher) {
        hipFwd = 0.38f; hipY = -0.17f; hipZ = 0.25f;
        adsFwd = 0.34f; adsY = -0.11f; adsZ = 0.13f;
    }

    float wx = lerpf(hipFwd, adsFwd, smoothAds) - recoil * 0.065f;
    float wy = lerpf(hipY, adsY, smoothAds) + bobY - equip * equip * 0.26f - reloadBell * 0.06f;
    float wz = lerpf(hipZ, adsZ, smoothAds) + bobX;

    const float pitchZ = recoil * 0.16f - equip * equip * 0.65f + reloadBell * 0.28f;
    const float yawY   = lerpf(0.085f, 0.0f, smoothAds) + reloadBell * 0.32f;
    const float rollX  = -reloadBell * 0.58f;

    const Mat4 wepLocalM =
        Mat4::translation(wx, wy, wz) *
        Mat4::rotationAxis({0, 1, 0}, yawY) *
        Mat4::rotationAxis({0, 0, 1}, pitchZ) *
        Mat4::rotationAxis({1, 0, 0}, rollX);
    const Mat4 wepWorldM = camBasis * wepLocalM;

    // 1. Render Detailed Multi-Part 3D Firearm Viewmodel!
    drawDetailedWeapon3D(vk, pl.currentWeapon, &slot, wepWorldM, pl.muzzleFlashTimer, pl.minigunSpin, false, true);

    // 2. Render 1st-Person Sleeves + Hands Gripping Pistol Grip & Foregrip/Mag!
    const Vec3 rAnchor = wepLocalM.transformPoint({-0.012f, -0.028f, 0.008f});
    const Vec3 rShoulder{-0.06f, -0.44f, 0.36f};
    drawMesh(vk, capsuleLimbMesh_, camBasis * buildCapsuleSegmentMatrix(rShoulder, rAnchor, 0.088f),
             sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, false);
    drawMesh(vk, sphereMesh_, camBasis * Mat4::translation(rAnchor.x, rAnchor.y, rAnchor.z) * Mat4::scale(0.088f, 0.086f, 0.082f),
             skinCol, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);

    Vec3 lAnchor = wepLocalM.transformPoint(weaponForegripLocal(pl.currentWeapon));
    if (reloadBell > 0.001f) {
        const Vec3 lowPouch{0.18f, -0.44f, -0.14f};
        const Vec3 magWell = wepLocalM.transformPoint(weaponMagWellLocal(pl.currentWeapon));
        if (reloadFrac < 0.45f) {
            lAnchor = lerp3(lAnchor, lowPouch, std::sin((reloadFrac / 0.45f) * kHalfPi));
        } else {
            lAnchor = lerp3(lowPouch, magWell, std::sin(((reloadFrac - 0.45f) / 0.55f) * kPi));
        }
    }
    const Vec3 lShoulder{-0.06f, -0.44f, -0.34f};
    drawMesh(vk, capsuleLimbMesh_, camBasis * buildCapsuleSegmentMatrix(lShoulder, lAnchor, 0.086f),
             sleeveCol, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, false);
    drawMesh(vk, sphereMesh_, camBasis * Mat4::translation(lAnchor.x, lAnchor.y, lAnchor.z) * Mat4::scale(0.086f, 0.084f, 0.080f),
             skinCol, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, false);
}

void Renderer3D::renderSceneAndBeginPost(VulkanContext& vk, const GameWorld& world,
                                         const CameraState& camera,
                                         bool firstPerson, float adsBlend, bool lookBack) {
    if (!initialized_) return;
    ++frameCounter_;
    hasPlayerLaserOrigin_ = false;

    const Vec2 focus = world.player().pos;
    prepareRealSeoulChunks(vk, world.city(), focus);

    const Vec3 lightDir = computeSunDirection(world);
    const bool shadowsOn = (settings_.shadowQuality > 0);

    // Light ortho camera snapped to shadow texel grid around focus (zero shadow swimming!)
    const float shadowSpan = std::max(85.0f, settings_.shadowSpanM());
    const float texelWorld = (shadowSpan * 2.0f) / static_cast<float>(VulkanContext::kShadowMapSize);
    const float snapX = std::floor(focus.x / texelWorld) * texelWorld;
    const float snapZ = std::floor(focus.z / texelWorld) * texelWorld;
    const Vec3 shadowTarget{snapX, world.city().surfaceHeightAt(focus, world.player().posY), snapZ};
    const Vec3 shadowEye = shadowTarget - lightDir * 220.0f;
    const Mat4 lightView = Mat4::lookAt(shadowEye, shadowTarget, {0, 1, 0});
    const Mat4 lightProj = Mat4::orthoVulkan(-shadowSpan, shadowSpan, -shadowSpan, shadowSpan, 10.0f, 480.0f);
    const Mat4 lightVP = lightProj * lightView;

    const SceneUBO ubo = buildSceneUBO(world, camera, vk.width(), vk.height(), lightDir, lightVP, shadowsOn);
    vk.updateSceneUBO(ubo);

    const bool nightMode = world.isNight();
    const float liveVehCull = clampf(static_cast<float>(settings_.renderDistanceM) * 0.45f, 160.0f, 650.0f);
    const float liveNpcCull = clampf(static_cast<float>(settings_.renderDistanceM) * 0.24f, 95.0f, 320.0f);

    auto renderWorldPass = [&](bool shadowPass) {
        drawSeoulTerrainAndRoads(vk, world, focus, shadowPass);
        drawBuildingsAndParks(vk, world, focus, shadowPass);
        drawTreesAndProps(vk, world, focus, camera.position, shadowPass);

        // Draw all CAD-precision Vehicles!
        const float vehCull = shadowPass ? std::min(shadowSpan * 0.92f, liveVehCull) : liveVehCull;
        for (const auto& v : world.vehicles()) {
            if ((v.pos - focus).lengthSq() > vehCull * vehCull) continue;
            const float gy = world.city().surfaceHeightAt(v.pos, v.posY);
            const bool hoodOnly = (!shadowPass && firstPerson && !lookBack && v.id == world.player().drivingVehicleId);
            drawVehicle(vk, v, gy, nightMode, shadowPass, false, hoodOnly, &world.player());
        }

        // Draw Player with live 3D weapon carry / shoulder-aim / recoil / reload / equip / punch animations!
        const auto& pl = world.player();
        if (pl.drivingVehicleId < 0 && (!firstPerson || shadowPass)) {
            const float gy = std::max(world.city().surfaceHeightAt(pl.pos, pl.posY), pl.posY);
            const ColorRGBA top = getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx);
            const ColorRGBA bot = getPlayerBottomColor(pl.currentOutfit, pl.characterCustom.customBottomColorIdx);
            const ColorRGBA skin = getPlayerSkinColor(pl.characterCustom.skinToneIdx);
            const auto& wepSlot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
            const auto& wepSpec = getWeaponSpec(pl.currentWeapon);

            WeaponPoseState wPose{};
            wPose.aimBlend = std::max(pl.weaponAimBlend, adsBlend);
            wPose.recoilAnim = pl.weaponRecoilAnim;
            wPose.reloadFrac = (pl.reloadTimer > 0.001f && wepSpec.reloadDuration > 0.01f)
                ? clampf(1.0f - (pl.reloadTimer / wepSpec.reloadDuration), 0.0f, 1.0f) : 0.0f;
            wPose.equipAnim = pl.weaponEquipAnim;
            wPose.meleeSwingAnim = pl.meleeSwingAnim;
            wPose.meleeSwingRight = pl.meleeSwingRight;
            wPose.minigunSpin = pl.minigunSpin;

            // Player ALWAYS visibly holds any equipped firearm (low-ready carry -> smooth combat aim)!
            const bool armed = (pl.currentWeapon != WeaponId::Fists);
            drawCharacter(vk, pl.pos, gy, pl.facingYaw, pl.walkAnimPhase, top, bot, skin,
                          !pl.wasted, armed, pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                          pl.currentOutfit, shadowPass, !pl.onGround, false, false, false,
                          &pl.characterCustom, &wepSlot, nullptr, nightMode, &wPose);
        }

        // Draw Living Seoul Citizens, Gangsters & Police Officers!
        const float npcCull = shadowPass ? std::min(shadowSpan * 0.72f, liveNpcCull) : liveNpcCull;
        for (const auto& npc : world.npcs()) {
            if ((npc.pos - focus).lengthSq() > npcCull * npcCull) continue;
            const float gy = world.city().surfaceHeightAt(npc.pos);
            const bool npcCombat = (npc.role != NpcRole::Civilian) &&
                                   (npc.behavior == NpcBehavior::CombatEngage || npc.muzzleFlashTimer > 0.01f);
            WeaponPoseState npcPose{};
            npcPose.aimBlend = npcCombat ? 1.0f : 0.0f;
            npcPose.recoilAnim = clampf(npc.muzzleFlashTimer * 8.0f, 0.0f, 1.0f);
            drawCharacter(vk, npc.pos, gy, npc.facingYaw, npc.animPhase,
                          npc.outfitTop, npc.outfitBottom, npc.skinColor,
                          npc.alive, npcCombat, npc.weapon,
                          npc.muzzleFlashTimer, 0.0f, npc.outfitStyle, shadowPass,
                          false, (npc.behavior == NpcBehavior::PanicFlee),
                          (npc.role == NpcRole::Police), npc.isSwat,
                          nullptr, nullptr, &npc, nightMode, &npcPose);
        }
    };

    // --- PASS 1: 2048x2048 Directional Shadow Map ---
    if (shadowsOn) {
        vk.beginShadowPass();
        renderWorldPass(true);
        vk.endShadowPass();
    }

    // --- PASS 2: 16-bit Linear HDR + MSAA 3D Seoul World ---
    vk.beginScenePass(ubo.fogColorAndTexel[0], ubo.fogColorAndTexel[1], ubo.fogColorAndTexel[2]);

    // Skybox centered on camera (Vulkan sky.vert + sky.frag with clouds, sun corona & distant mountain ring!)
    {
        const Mat4 skyM = Mat4::translation(camera.position.x, camera.position.y, camera.position.z) *
                          Mat4::scale(500.0f, 500.0f, 500.0f);
        drawMesh(vk, skyBoxMesh_, skyM, {255, 255, 255, 255}, 0.5f, 0.0f, 0.0f,
                 MatId::Standard, false, WorldPipelineKind::Sky);
    }

    // Opaque Seoul World + CAD Vehicles + Characters
    renderWorldPass(false);

    // Translucent Glass, Rainbow Fountain, Rain, Particles, Tracers & Sleek Curb Beacons
    drawAlphaAndEffects(vk, world, camera, focus);

    // 1st-Person Weapon Viewmodel + Forearms/Hands
    if (firstPerson && !lookBack) {
        drawFirstPersonOverlay(vk, world, camera, adsBlend);
    }

    vk.endScenePass();

    // --- PASS 3: Post-Process (HDR Kawase Bloom + ACES + sRGB) + Open for 2D UI! ---
    PostPush pp{};
    pp.params0[0] = 1.0f / static_cast<float>(std::max(1, vk.width()));
    pp.params0[1] = 1.0f / static_cast<float>(std::max(1, vk.height()));
    pp.params0[2] = nightMode ? 0.62f : 0.36f; // Bloom strength
    pp.params0[3] = nightMode ? 1.08f : 1.02f; // Exposure
    const Vehicle* pCar = world.playerVehicle();
    pp.params1[0] = (pCar && pCar->nitroActive) ? 0.85f : 0.0f;
    pp.params1[1] = settings_.bloomEnabled() ? 1.0f : 0.0f;
    vk.beginSwapchainPostAndUiPass(pp);
}

} // namespace oc
