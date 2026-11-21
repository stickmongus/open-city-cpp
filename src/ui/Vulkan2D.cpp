#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <stb_image.h>

#include "ui/Vulkan2D.hpp"
#include "ui/AndroidKoreanGlyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace oc {

namespace {

inline std::uint32_t nextUtf8Codepoint(const std::string& s, std::size_t& idx) {
    const std::size_t n = s.size();
    if (idx >= n) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[idx++]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && idx < n) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
    }
    if ((c0 & 0xF0) == 0xE0 && idx + 1 < n) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c2 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
    }
    if ((c0 & 0xF8) == 0xF0 && idx + 2 < n) {
        const unsigned char c1 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c2 = static_cast<unsigned char>(s[idx++]);
        const unsigned char c3 = static_cast<unsigned char>(s[idx++]);
        return ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
    }
    return '?';
}

} // namespace

bool Vulkan2D::init(VulkanContext& vk, const std::vector<std::uint8_t>& fontBytes) {
    glyphs_.clear();
    verts_.reserve(8192);
    fontLoaded_ = false;
    if (fontBytes.empty()) return false;

    const int fontOffset = stbtt_GetFontOffsetForIndex(fontBytes.data(), 0);
    if (fontOffset < 0) return false;

    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, fontBytes.data(), fontOffset)) return false;

    basePixelHeight_ = 44.0f;
    const float scale = stbtt_ScaleForPixelHeight(&info, basePixelHeight_);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    ascentPx_ = static_cast<float>(ascent) * scale;

    // Gather unique codepoints: ASCII + all Korean UI glyphs + geometric symbols
    std::unordered_set<std::uint32_t> cpSet;
    for (std::uint32_t c = 0x20; c <= 0x7E; ++c) cpSet.insert(c);
    for (int cp : kAndroidKoreanCodepoints) {
        if (cp > 0) cpSet.insert(static_cast<std::uint32_t>(cp));
    }
    const std::uint32_t extraSymbols[] = {
        0x00B0, 0x00B7, 0x00D7, 0x2013, 0x2014, 0x2022,
        0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25B6, 0x25BC, 0x25C0,
        0x2605, 0x2606
    };
    for (std::uint32_t sym : extraSymbols) cpSet.insert(sym);

    constexpr std::uint32_t kAtlasW = 2048;
    constexpr std::uint32_t kAtlasH = 2048;
    std::vector<std::uint8_t> atlas(static_cast<std::size_t>(kAtlasW) * kAtlasH, 0u);

    std::uint32_t penX = 2;
    std::uint32_t penY = 2;
    std::uint32_t rowH = 0;
    const float invW = 1.0f / static_cast<float>(kAtlasW);
    const float invH = 1.0f / static_cast<float>(kAtlasH);

    for (std::uint32_t cp : cpSet) {
        const int glyphIdx = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        int advW = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, glyphIdx, &advW, &lsb);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&info, glyphIdx, scale, scale, &x0, &y0, &x1, &y1);
        const int gw = std::max(0, x1 - x0);
        const int gh = std::max(0, y1 - y0);

        GlyphInfo g{};
        g.advance = static_cast<float>(advW) * scale;
        g.xoff = static_cast<float>(x0);
        g.yoff = static_cast<float>(y0);
        g.w = static_cast<float>(gw);
        g.h = static_cast<float>(gh);

        if (gw > 0 && gh > 0) {
            if (penX + static_cast<std::uint32_t>(gw) + 3u >= kAtlasW) {
                penX = 2;
                penY += rowH + 3u;
                rowH = 0;
            }
            if (penY + static_cast<std::uint32_t>(gh) + 3u < kAtlasH) {
                stbtt_MakeGlyphBitmap(&info,
                                      atlas.data() + static_cast<std::size_t>(penY) * kAtlasW + penX,
                                      gw, gh, static_cast<int>(kAtlasW), scale, scale, glyphIdx);
                g.u0 = static_cast<float>(penX) * invW;
                g.v0 = static_cast<float>(penY) * invH;
                g.u1 = static_cast<float>(penX + static_cast<std::uint32_t>(gw)) * invW;
                g.v1 = static_cast<float>(penY + static_cast<std::uint32_t>(gh)) * invH;
                penX += static_cast<std::uint32_t>(gw) + 3u;
                rowH = std::max(rowH, static_cast<std::uint32_t>(gh));
            }
        }
        glyphs_[cp] = g;
    }

    fontLoaded_ = vk.uploadFontAtlasR8(atlas.data(), kAtlasW, kAtlasH);
    return fontLoaded_;
}

bool Vulkan2D::initMapTexture(VulkanContext& vk, const std::vector<std::uint8_t>& mapPngBytes) {
    mapTexLoaded_ = false;
    if (mapPngBytes.empty()) return false;
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load_from_memory(mapPngBytes.data(),
                                            static_cast<int>(mapPngBytes.size()),
                                            &w, &h, &ch, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    mapTexLoaded_ = vk.uploadUiMapRGBA8(pixels, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    stbi_image_free(pixels);
    return mapTexLoaded_;
}

void Vulkan2D::beginBatch() {
    verts_.clear();
}

void Vulkan2D::flush(VulkanContext& vk) {
    if (verts_.empty()) return;
    vk.drawUiVertices(verts_.data(), static_cast<std::uint32_t>(verts_.size()));
    verts_.clear();
}

void Vulkan2D::pushScissor(VulkanContext& vk, int x, int y, int w, int h) {
    flush(vk);
    vk.setUiScissor(x, y, w, h);
}

void Vulkan2D::popScissor(VulkanContext& vk) {
    flush(vk);
    vk.resetUiScissor();
}

void Vulkan2D::pushTri(float x0, float y0, float u0, float v0,
                       float x1, float y1, float u1, float v1,
                       float x2, float y2, float u2, float v2,
                       ColorRGBA col) {
    const float r = static_cast<float>(col.r) / 255.0f;
    const float g = static_cast<float>(col.g) / 255.0f;
    const float b = static_cast<float>(col.b) / 255.0f;
    const float a = static_cast<float>(col.a) / 255.0f;
    verts_.push_back({x0, y0, u0, v0, r, g, b, a});
    verts_.push_back({x1, y1, u1, v1, r, g, b, a});
    verts_.push_back({x2, y2, u2, v2, r, g, b, a});
}

void Vulkan2D::drawMapImage(float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, ColorRGBA tint) {
    if (!mapTexLoaded_ || w <= 0.0f || h <= 0.0f || tint.a == 0) return;
    // ui2d.frag samples full-color mapTex (binding=1) whenever fragUv.x >= 1.5 at (fragUv.x - 2.0, fragUv.y)!
    const float mu0 = 2.0f + clampf(u0, 0.0f, 1.0f);
    const float mu1 = 2.0f + clampf(u1, 0.0f, 1.0f);
    pushTri(x, y, mu0, v0,  x + w, y, mu1, v0,  x + w, y + h, mu1, v1, tint);
    pushTri(x, y, mu0, v0,  x + w, y + h, mu1, v1,  x, y + h, mu0, v1, tint);
}

void Vulkan2D::drawRect(float x, float y, float w, float h, ColorRGBA col) {
    if (w <= 0.0f || h <= 0.0f || col.a == 0) return;
    pushTri(x, y, -1, -1,  x + w, y, -1, -1,  x + w, y + h, -1, -1, col);
    pushTri(x, y, -1, -1,  x + w, y + h, -1, -1,  x, y + h, -1, -1, col);
}

void Vulkan2D::drawRectOutline(float x, float y, float w, float h, float thick, ColorRGBA col) {
    if (w <= 0.0f || h <= 0.0f || thick <= 0.0f) return;
    drawRect(x, y, w, thick, col);
    drawRect(x, y + h - thick, w, thick, col);
    drawRect(x, y + thick, thick, std::max(0.0f, h - thick * 2.0f), col);
    drawRect(x + w - thick, y + thick, thick, std::max(0.0f, h - thick * 2.0f), col);
}

void Vulkan2D::drawRoundedRect(float x, float y, float w, float h, float radius, ColorRGBA col) {
    if (w <= 0.0f || h <= 0.0f || col.a == 0) return;
    const float r = std::min({radius, w * 0.5f, h * 0.5f});
    if (r < 1.0f) {
        drawRect(x, y, w, h, col);
        return;
    }
    // Center cross rects
    drawRect(x + r, y, w - r * 2.0f, h, col);
    drawRect(x, y + r, r, h - r * 2.0f, col);
    drawRect(x + w - r, y + r, r, h - r * 2.0f, col);

    // 4 smooth rounded corners (6 segments each)
    constexpr int segs = 6;
    const float cx[4] = {x + w - r, x + r, x + r, x + w - r};
    const float cy[4] = {y + h - r, y + h - r, y + r, y + r};
    const float aStart[4] = {0.0f, kHalfPi, kPi, kPi + kHalfPi};
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < segs; ++i) {
            const float a0 = aStart[c] + kHalfPi * static_cast<float>(i) / static_cast<float>(segs);
            const float a1 = aStart[c] + kHalfPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            pushTri(cx[c], cy[c], -1, -1,
                    cx[c] + std::cos(a0) * r, cy[c] + std::sin(a0) * r, -1, -1,
                    cx[c] + std::cos(a1) * r, cy[c] + std::sin(a1) * r, -1, -1, col);
        }
    }
}

void Vulkan2D::drawRoundedRectOutline(float x, float y, float w, float h, float radius, float thick, ColorRGBA col) {
    if (w <= 0.0f || h <= 0.0f || thick <= 0.0f || col.a == 0) return;
    const float r = std::min({radius, w * 0.5f, h * 0.5f});
    const float rIn = std::max(0.0f, r - thick);
    // 4 straight edge bars
    drawRect(x + r, y, w - r * 2.0f, thick, col);
    drawRect(x + r, y + h - thick, w - r * 2.0f, thick, col);
    drawRect(x, y + r, thick, h - r * 2.0f, col);
    drawRect(x + w - thick, y + r, thick, h - r * 2.0f, col);
    // 4 curved corner arcs
    constexpr int segs = 6;
    const float cx[4] = {x + w - r, x + r, x + r, x + w - r};
    const float cy[4] = {y + h - r, y + h - r, y + r, y + r};
    const float aStart[4] = {0.0f, kHalfPi, kPi, kPi + kHalfPi};
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < segs; ++i) {
            const float a0 = aStart[c] + kHalfPi * static_cast<float>(i) / static_cast<float>(segs);
            const float a1 = aStart[c] + kHalfPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            pushTri(cx[c] + c0 * rIn, cy[c] + s0 * rIn, -1, -1,
                    cx[c] + c0 * r,   cy[c] + s0 * r,   -1, -1,
                    cx[c] + c1 * r,   cy[c] + s1 * r,   -1, -1, col);
            pushTri(cx[c] + c0 * rIn, cy[c] + s0 * rIn, -1, -1,
                    cx[c] + c1 * r,   cy[c] + s1 * r,   -1, -1,
                    cx[c] + c1 * rIn, cy[c] + s1 * rIn, -1, -1, col);
        }
    }
}

void Vulkan2D::drawCircle(float cx, float cy, float radius, ColorRGBA col, int segments) {
    if (radius <= 0.0f || col.a == 0) return;
    segments = std::max(8, segments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        pushTri(cx, cy, -1, -1,
                cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, -1, -1,
                cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, -1, -1, col);
    }
}

void Vulkan2D::drawCircleRing(float cx, float cy, float innerR, float outerR, ColorRGBA col, int segments) {
    if (outerR <= innerR || col.a == 0) return;
    segments = std::max(10, segments);
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        pushTri(cx + c0 * innerR, cy + s0 * innerR, -1, -1,
                cx + c0 * outerR, cy + s0 * outerR, -1, -1,
                cx + c1 * outerR, cy + s1 * outerR, -1, -1, col);
        pushTri(cx + c0 * innerR, cy + s0 * innerR, -1, -1,
                cx + c1 * outerR, cy + s1 * outerR, -1, -1,
                cx + c1 * innerR, cy + s1 * innerR, -1, -1, col);
    }
}

void Vulkan2D::drawLine(float x0, float y0, float x1, float y1, float thick, ColorRGBA col) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f || thick <= 0.0f || col.a == 0) return;
    const float nx = (-dy / len) * (thick * 0.5f);
    const float ny = ( dx / len) * (thick * 0.5f);
    pushTri(x0 - nx, y0 - ny, -1, -1,  x1 - nx, y1 - ny, -1, -1,  x1 + nx, y1 + ny, -1, -1, col);
    pushTri(x0 - nx, y0 - ny, -1, -1,  x1 + nx, y1 + ny, -1, -1,  x0 + nx, y0 + ny, -1, -1, col);
}

void Vulkan2D::drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, ColorRGBA col) {
    pushTri(x0, y0, -1, -1,  x1, y1, -1, -1,  x2, y2, -1, -1, col);
}

float Vulkan2D::measureTextWidth(const std::string& utf8, float fontSizePx) const {
    if (!fontLoaded_ || utf8.empty()) return static_cast<float>(utf8.size()) * fontSizePx * 0.5f;
    const float sc = fontSizePx / basePixelHeight_;
    float w = 0.0f;
    std::size_t idx = 0;
    while (idx < utf8.size()) {
        const std::uint32_t cp = nextUtf8Codepoint(utf8, idx);
        if (cp == '\n') break;
        const auto it = glyphs_.find(cp);
        if (it != glyphs_.end()) w += it->second.advance * sc;
        else w += fontSizePx * 0.55f;
    }
    return w;
}

void Vulkan2D::drawText(const std::string& utf8, float x, float y, float fontSizePx, ColorRGBA col, bool shadow) {
    if (!fontLoaded_ || utf8.empty() || col.a == 0) return;
    const float sc = fontSizePx / basePixelHeight_;
    const float baselineY = y + ascentPx_ * sc;

    auto emitPass = [&](float offX, float offY, ColorRGBA passCol) {
        float penX = x + offX;
        std::size_t idx = 0;
        while (idx < utf8.size()) {
            const std::uint32_t cp = nextUtf8Codepoint(utf8, idx);
            if (cp == '\n') { penX = x + offX; continue; }
            auto it = glyphs_.find(cp);
            if (it == glyphs_.end()) it = glyphs_.find('?');
            if (it != glyphs_.end()) {
                const GlyphInfo& g = it->second;
                if (g.w > 0.0f && g.h > 0.0f) {
                    const float gx0 = penX + g.xoff * sc;
                    const float gy0 = baselineY + offY + g.yoff * sc;
                    const float gx1 = gx0 + g.w * sc;
                    const float gy1 = gy0 + g.h * sc;
                    pushTri(gx0, gy0, g.u0, g.v0,  gx1, gy0, g.u1, g.v0,  gx1, gy1, g.u1, g.v1, passCol);
                    pushTri(gx0, gy0, g.u0, g.v0,  gx1, gy1, g.u1, g.v1,  gx0, gy1, g.u0, g.v1, passCol);
                }
                penX += g.advance * sc;
            } else {
                penX += fontSizePx * 0.5f;
            }
        }
    };

    if (shadow) {
        const std::uint8_t sA = static_cast<std::uint8_t>(std::min(200, static_cast<int>(col.a) * 3 / 4));
        emitPass(1.4f, 1.4f, {8, 10, 14, sA});
    }
    emitPass(0.0f, 0.0f, col);
}

void Vulkan2D::drawTextCentered(const std::string& utf8, float cx, float y, float fontSizePx, ColorRGBA col, bool shadow) {
    const float w = measureTextWidth(utf8, fontSizePx);
    drawText(utf8, cx - w * 0.5f, y, fontSizePx, col, shadow);
}

} // namespace oc
