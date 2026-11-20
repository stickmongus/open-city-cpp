#pragma once

#include "core/Math.hpp"
#include "render/VulkanContext.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace oc {

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

class Vulkan2D {
public:
    Vulkan2D() = default;
    ~Vulkan2D() = default;

    // Initializes stb_truetype Korean+ASCII R8_UNORM atlas from font bytes (loaded by PlatformApp)
    bool init(VulkanContext& vk, const std::vector<std::uint8_t>& fontBytes);

    void beginBatch();
    void flush(VulkanContext& vk);

    void pushScissor(VulkanContext& vk, int x, int y, int w, int h);
    void popScissor(VulkanContext& vk);

    // 2D Primitives
    void drawRect(float x, float y, float w, float h, ColorRGBA col);
    void drawRectOutline(float x, float y, float w, float h, float thick, ColorRGBA col);
    void drawRoundedRect(float x, float y, float w, float h, float radius, ColorRGBA col);
    void drawRoundedRectOutline(float x, float y, float w, float h, float radius, float thick, ColorRGBA col);
    void drawCircle(float cx, float cy, float radius, ColorRGBA col, int segments = 24);
    void drawCircleRing(float cx, float cy, float innerR, float outerR, ColorRGBA col, int segments = 28);
    void drawLine(float x0, float y0, float x1, float y1, float thick, ColorRGBA col);
    void drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, ColorRGBA col);

    // UTF-8 Korean + English TrueType Text Rendering
    void drawText(const std::string& utf8, float x, float y, float fontSizePx, ColorRGBA col, bool shadow = true);
    void drawTextCentered(const std::string& utf8, float cx, float y, float fontSizePx, ColorRGBA col, bool shadow = true);
    float measureTextWidth(const std::string& utf8, float fontSizePx) const;

    bool hasFont() const { return fontLoaded_; }

private:
    struct GlyphInfo {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float xoff = 0, yoff = 0;
        float w = 0, h = 0;
        float advance = 16.0f;
    };

    void pushTri(float x0, float y0, float u0, float v0,
                 float x1, float y1, float u1, float v1,
                 float x2, float y2, float u2, float v2,
                 ColorRGBA col);

    bool fontLoaded_ = false;
    float basePixelHeight_ = 36.0f;
    float ascentPx_ = 29.0f;
    std::unordered_map<std::uint32_t, GlyphInfo> glyphs_;
    std::vector<Vertex2D> verts_;
};

} // namespace oc
