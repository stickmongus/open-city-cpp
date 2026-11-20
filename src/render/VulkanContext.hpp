#pragma once

#include "core/Mat4.hpp"
#include "core/Math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

namespace oc {

class PlatformApp;

struct Vertex3D {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    float u = 0.0f, v = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};
static_assert(sizeof(Vertex3D) == 48, "Vertex3D must match world.vert 48-byte stride");

struct Vertex2D {
    float x = 0.0f, y = 0.0f;
    float u = -1.0f, v = -1.0f; // u < 0 => solid untextured color
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};
static_assert(sizeof(Vertex2D) == 32, "Vertex2D must match ui2d.vert 32-byte stride");

struct SceneUBO {
    Mat4 viewProj{};
    Mat4 lightVP{};
    float lightDirAndShadows[4]{-0.45f, -0.78f, -0.42f, 1.0f};
    float sunColorAndTime[4]{1.0f, 0.96f, 0.88f, 0.0f};
    float skyAmbientAndNight[4]{0.36f, 0.50f, 0.72f, 0.0f};
    float groundAmbientAndWet[4]{0.16f, 0.17f, 0.19f, 0.0f};
    float fogColorAndTexel[4]{0.66f, 0.78f, 0.90f, 1.0f / 2048.0f};
    float horizonColorAndDay[4]{0.78f, 0.85f, 0.94f, 1.0f};
    float viewPosAndHour[4]{0.0f, 5.0f, 0.0f, 13.5f};
};
static_assert(sizeof(SceneUBO) == 240, "SceneUBO must match std140 layout");

struct DrawPush {
    Mat4 model{};
    float tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float pbrParams[4]{0.5f, 0.0f, 0.0f, 0.0f}; // x=rough, y=metal, z=emissive, w=matId/255
};
static_assert(sizeof(DrawPush) == 96, "DrawPush must be <= 128 bytes Vulkan 1.0 limit");

struct PostPush {
    float params0[4]{1.0f / 1440.0f, 1.0f / 860.0f, 0.48f, 1.0f};
    float params1[4]{0.0f, 1.0f, 0.0f, 0.0f};
};
static_assert(sizeof(PostPush) == 32, "PostPush size");

struct UiPush {
    float screenScale[4]{2.0f / 1440.0f, 2.0f / 860.0f, 0.0f, 0.0f};
};
static_assert(sizeof(UiPush) == 16, "UiPush size");

struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;

    bool valid() const { return vertexBuffer != VK_NULL_HANDLE && (indexCount > 0 || vertexCount > 0); }
};

enum class WorldPipelineKind {
    Opaque = 0,
    Alpha = 1,
    Additive = 2,
    Sky = 3
};

class VulkanContext {
public:
    static constexpr int kMaxFramesInFlight = 1;
    static constexpr std::uint32_t kShadowMapSize = 2048;

    VulkanContext();
    ~VulkanContext();

    bool init(const PlatformApp& app);
    void shutdown();

    bool recreateSwapchain(const PlatformApp& app);
    bool recreateSurfaceAndSwapchain(const PlatformApp& app);
    void waitIdle();

    bool isReady() const;
    int width() const;
    int height() const;
    const std::string& deviceName() const;
    bool usingHdr16() const;
    int msaaSamples() const;

    // Mesh allocation & release (device-local via staging buffer)
    GpuMesh createMesh(const std::vector<Vertex3D>& vertices,
                       const std::vector<std::uint32_t>& indices = {});
    void destroyMesh(GpuMesh& mesh);

    // Font atlas upload (R8_UNORM texture for Vulkan2D text rendering)
    bool uploadFontAtlasR8(const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h);

    // Frame lifecycle
    bool beginFrame();
    void updateSceneUBO(const SceneUBO& ubo);

    // Pass 1: 2048x2048 shadow map pass
    void beginShadowPass();
    void drawShadowMesh(const GpuMesh& mesh, const Mat4& model);
    void endShadowPass();

    // Pass 2: HDR + MSAA 3D scene pass
    void beginScenePass(float clearR, float clearG, float clearB);
    void bindWorldPipeline(WorldPipelineKind kind);
    void clearSceneDepth(); // Clears depth buffer cleanly for 1st-person viewmodels
    void drawWorldMesh(const GpuMesh& mesh, const DrawPush& push);
    void drawDynamicTriangles3D(const Vertex3D* verts, std::uint32_t vertCount,
                                WorldPipelineKind kind, const DrawPush& push);
    void endScenePass();

    // Pass 3: Post-process (linear HDR Bloom + ACES + sRGB) + 2D UI overlay
    void beginSwapchainPostAndUiPass(const PostPush& postPush);
    void setUiScissor(int x, int y, int w, int h);
    void resetUiScissor();
    void drawUiVertices(const Vertex2D* verts, std::uint32_t vertCount);
    void endSwapchainPostAndUiPass();

    // Submits command buffer, optionally reads back PNG screenshot, and presents
    bool endFrameAndPresent(const std::string* screenshotPngPath = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace oc
