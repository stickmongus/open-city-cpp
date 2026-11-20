#pragma once

#include "core/Math.hpp"
#include "gameplay/Types.hpp"
#include "render/VulkanContext.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace oc {

// Reusable hard-crease 3D mesh builder for CAD cars, Seoul landmarks, bridges & primitives
class MeshBuilder3D {
public:
    void clear();
    void reserve(std::size_t triCount);

    void addTri(const Vec3& a, const Vec3& b, const Vec3& c,
                ColorRGBA col = {255, 255, 255, 255},
                Vec2 uvA = {0, 0}, Vec2 uvB = {1, 0}, Vec2 uvC = {1, 1});

    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                 ColorRGBA col = {255, 255, 255, 255});

    void addQuadGrad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     ColorRGBA colA, ColorRGBA colB, ColorRGBA colC, ColorRGBA colD);

    // Axis-aligned or oriented box with optional top taper
    void addBox(const Vec3& center, const Vec3& size,
                ColorRGBA col = {255, 255, 255, 255});

    // Rotated / oriented box (basis right X, up Y, fwd Z)
    void addOrientedBox(const Vec3& center, const Vec3& halfExtents,
                        const Vec3& axisX, const Vec3& axisY, const Vec3& axisZ,
                        ColorRGBA col = {255, 255, 255, 255});

    // Tapered frustum / pillar along Y axis
    void addCylinderY(const Vec3& bottomCenter, float rBottom, float rTop, float height,
                      int segments, ColorRGBA col = {255, 255, 255, 255}, bool capEnds = true);

    // Cylinder along Z axis (great for wheels, exhaust pipes, rollers!)
    void addCylinderZ(const Vec3& center, float radius, float length, int segments,
                      ColorRGBA col = {255, 255, 255, 255}, bool capEnds = true);

    // Cylinder along X axis (headlamp projectors, cannons, pipes)
    void addCylinderX(const Vec3& center, float radius, float length, int segments,
                      ColorRGBA col = {255, 255, 255, 255}, bool capEnds = true);

    // UV Sphere
    void addSphere(const Vec3& center, float radius, int rings, int slices,
                   ColorRGBA col = {255, 255, 255, 255});

    // Builds Vertex3D buffer with exact crease-angle vertex normal splitting (e.g., 24 deg for CAD steel!)
    std::vector<Vertex3D> buildCreasedVertices(float creaseAngleDeg = 24.0f) const;

    // Uploads directly into a device-local Vulkan GpuMesh
    GpuMesh upload(VulkanContext& vk, float creaseAngleDeg = 24.0f) const;

    bool empty() const { return tris_.empty(); }

private:
    struct RawTri {
        Vec3 p[3];
        Vec2 uv[3];
        ColorRGBA c[3];
        Vec3 faceN{};
    };
    std::vector<RawTri> tris_;
};

// Complete CAD-precision multi-material Vulkan vehicle mesh assembly
struct CarMeshSet {
    GpuMesh body;       // Painted stamped sheet-metal panels + coupled A/B/C pillars + roof
    GpuMesh glass;      // Flush curved smoked windshield, side door panes & rear glass
    GpuMesh interior;   // Dark 3D dashboard, steering wheel, bucket seats & cabin
    GpuMesh trim;       // Matte-black DLO frames, shutlines, grille slats, diffuser, mirrors, wheel wells
    GpuMesh chrome;     // Brushed metal grille surrounds, exhaust tips, badges & door handles
    GpuMesh lampsHead;  // 3D headlamp projectors + crisp LED DRL signatures
    GpuMesh lampsTail;  // 3D sculpted LED taillight bars & brake clusters
    GpuMesh livery;     // Clean flat-panel Police / Taxi / 119 / DeliveryVan markings & roof signs
    GpuMesh stripes;    // Optional racing stripes (co-planar on hood/roof/trunk, zero Z-fight!)
    GpuMesh spoiler;    // Optional integrated or GT carbon rear wing

    // Exact wheel coordinates for Renderer3D spinning/steering/suspension alignment
    float frontAxleX = 1.32f;
    float rearAxleX = -1.32f;
    float wheelTrackZ = 0.92f;
    float wheelRadius = 0.36f;
    float wheelWidth = 0.25f;

    void destroy(VulkanContext& vk);
};

// Shared high-detail tire + multi-spoke alloy rim + brake rotor assembly
struct WheelMeshSet {
    GpuMesh tire;       // Sculpted rubber tire with curved sidewall & tread shoulder
    GpuMesh rim;        // Deep-dish 10-spoke machined alloy rim
    GpuMesh brakeDisc;  // Ventilated steel brake rotor (glows hot under heavy braking!)
    GpuMesh caliper;    // Painted performance brake caliper

    void destroy(VulkanContext& vk);
};

// Generates all 16 CAD-precision non-jelly vehicles + wheel assembly
class CarMeshLibrary {
public:
    void init(VulkanContext& vk);
    void destroy(VulkanContext& vk);

    const CarMeshSet& get(VehicleKind kind) const {
        const int idx = std::clamp(static_cast<int>(kind), 0, static_cast<int>(VehicleKind::Count) - 1);
        return cars_[static_cast<std::size_t>(idx)];
    }
    const WheelMeshSet& wheel() const { return wheel_; }

private:
    std::array<CarMeshSet, static_cast<std::size_t>(VehicleKind::Count)> cars_{};
    WheelMeshSet wheel_{};
};

} // namespace oc
