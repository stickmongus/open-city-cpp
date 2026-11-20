#include "render/Renderer3D.hpp"

#include <algorithm>
#include <cmath>

namespace oc {

namespace {

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

void Renderer3D::init(VulkanContext& vk, const CityMap& city) {
    if (initialized_) return;
    buildStaticPrimitives(vk);
    cars_.init(vk);
    buildSeoulMacroMeshes(vk, city);
    initialized_ = true;
}

void Renderer3D::shutdown(VulkanContext& vk) {
    if (!initialized_) return;
    vk.waitIdle();
    cars_.destroy(vk);

    vk.destroyMesh(cubeMesh_);
    vk.destroyMesh(cylMesh_);
    vk.destroyMesh(coneMesh_);
    vk.destroyMesh(sphereMesh_);
    vk.destroyMesh(skyBoxMesh_);
    vk.destroyMesh(foliageMesh_);
    vk.destroyMesh(pineMesh_);
    vk.destroyMesh(ringMesh_);
    vk.destroyMesh(giwaRoofMesh_);
    vk.destroyMesh(capsuleLimbMesh_);
    vk.destroyMesh(torsoMesh_);

    vk.destroyMesh(hanRiverWaterMesh_);
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
    initialized_ = false;
}

void Renderer3D::cycleQuality() {
    const int next = (static_cast<int>(quality_) + 1) % 3;
    quality_ = static_cast<GraphicsQuality>(next);
}

const char* Renderer3D::qualityNameEn() const {
    switch (quality_) {
        case GraphicsQuality::Ultra: return "ULTRA (Vulkan HDR + Shadows)";
        case GraphicsQuality::High: return "HIGH (Vulkan Shadows)";
        case GraphicsQuality::Performance: return "PERFORMANCE (Vulkan Fast)";
    }
    return "ULTRA";
}

const char* Renderer3D::qualityNameKo() const {
    switch (quality_) {
        case GraphicsQuality::Ultra: return "최고 (Vulkan HDR+그림자)";
        case GraphicsQuality::High: return "높음 (Vulkan 그림자)";
        case GraphicsQuality::Performance: return "성능 우선 (Vulkan 고속)";
    }
    return "최고";
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
    // Uses compact ellipsoidal endcaps (ry=0.16) so slender limb scaling (0.11, 0.27, 0.11) forms round joints without shoulder spikes!
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
    // Sculpted anatomical human torso (broader athletic shoulders tapering cleanly to waist, Y in [0..1]!)
    {
        MeshBuilder3D mb;
        const int slices = 16;
        const float yLvl[5]  = {0.00f, 0.22f, 0.55f, 0.88f, 1.00f};
        const float rxLvl[5] = {0.36f, 0.38f, 0.44f, 0.48f, 0.34f}; // Front-back chest depth
        const float rzLvl[5] = {0.38f, 0.40f, 0.48f, 0.54f, 0.36f}; // Left-right shoulder width
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
        // Shoulder cap dome
        mb.addSphere({0.0f, 0.90f, 0.0f}, 0.36f, 6, 12);
        torsoMesh_ = mb.upload(vk, 60.0f);
    }
    // Organic Multi-Layered Seoul Street Tree Crown (12 asymmetric foliage clusters for natural Ginkgo/Cherry silhouette!)
    {
        MeshBuilder3D mb;
        struct LeafClump { Vec3 p; float r; ColorRGBA c; };
        const LeafClump clumps[12] = {
            {{ 0.00f,  0.42f,  0.00f}, 0.56f, {244, 248, 240, 255}},
            {{ 0.48f,  0.16f,  0.22f}, 0.46f, {224, 236, 218, 255}},
            {{-0.46f,  0.20f, -0.18f}, 0.48f, {234, 244, 228, 255}},
            {{ 0.14f,  0.14f,  0.52f}, 0.44f, {218, 232, 212, 255}},
            {{-0.18f,  0.18f, -0.50f}, 0.45f, {226, 238, 220, 255}},
            {{ 0.42f,  0.48f, -0.28f}, 0.40f, {250, 252, 244, 255}},
            {{-0.38f,  0.52f,  0.28f}, 0.42f, {248, 250, 242, 255}},
            {{ 0.02f,  0.74f, -0.06f}, 0.38f, {255, 255, 250, 255}},
            {{-0.54f, -0.02f,  0.26f}, 0.38f, {204, 218, 198, 255}},
            {{ 0.52f, -0.04f, -0.24f}, 0.36f, {202, 216, 196, 255}},
            {{ 0.12f,  0.64f,  0.34f}, 0.34f, {252, 254, 246, 255}},
            {{-0.16f,  0.02f,  0.32f}, 0.36f, {210, 224, 204, 255}}
        };
        for (const auto& cl : clumps) {
            mb.addSphere(cl.p, cl.r, 6, 9, cl.c);
        }
        foliageMesh_ = mb.upload(vk, 55.0f);
    }
    // Layered Korean Mountain Pine Tree (소나무)
    {
        MeshBuilder3D mb;
        mb.addCylinderY({0, 0.00f, 0}, 0.56f, 0.16f, 0.52f, 10, {210, 222, 210, 255});
        mb.addCylinderY({0, 0.32f, 0}, 0.46f, 0.12f, 0.50f, 10, {228, 238, 226, 255});
        mb.addCylinderY({0, 0.62f, 0}, 0.34f, 0.02f, 0.48f, 10, {246, 255, 244, 255});
        pineMesh_ = mb.upload(vk, 38.0f);
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
}

void Renderer3D::buildSeoulMacroMeshes(VulkanContext& vk, const CityMap& city) {
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
    // 2. 7 REAL SEOUL MOUNTAINS (Namsan, Bukhansan, Inwangsan, Bugaksan,
    //    Gwanaksan, Achasan, Cheonggyesan) with lush pine slopes & granite peaks!
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D forestMb, graniteMb;
        for (const auto& m : city.mountains()) {
            const int rings = 10;
            const int slices = 24;
            for (int r = 0; r < rings; ++r) {
                const float f0 = static_cast<float>(r) / static_cast<float>(rings);
                const float f1 = static_cast<float>(r + 1) / static_cast<float>(rings);
                auto evalMtnPt = [&](float frac, float ang) -> Vec3 {
                    const float rx = m.radiusX * frac;
                    const float rz = m.radiusZ * frac;
                    const float wx = m.center.x + std::cos(ang) * rx;
                    const float wz = m.center.z + std::sin(ang) * rz;
                    float h = (frac > 0.96f) ? -1.5f : city.terrainElevationAt({wx, wz});
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
                        if (r == 0) graniteMb.addTri(p00, p01, p11, cRock);
                        else        graniteMb.addQuad(p00, p01, p11, p10, cRock);
                    } else {
                        const std::uint8_t gR = static_cast<std::uint8_t>(lerpf(56.0f, 42.0f, hFrac));
                        const std::uint8_t gG = static_cast<std::uint8_t>(lerpf(92.0f, 74.0f, hFrac));
                        const std::uint8_t gB = static_cast<std::uint8_t>(lerpf(48.0f, 44.0f, hFrac));
                        const ColorRGBA cFor{gR, gG, gB, 255};
                        if (r == 0) forestMb.addTri(p00, p01, p11, cFor);
                        else        forestMb.addQuad(p00, p01, p11, p10, cFor);
                    }
                }
            }
        }
        mountainForestMesh_ = forestMb.upload(vk, 55.0f);
        mountainGraniteMesh_ = graniteMb.upload(vk, 35.0f);
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
                    stoneMb.addBox({lx + 76.0f, baseY + 18.0f, lz - 6.0f}, {84.0f, 36.0f, 76.0f}, {222, 224, 228, 255});
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
                    stoneMb.addCylinderY({px, py - 2.0f, pz}, 10.0f, 9.5f, 4.0f, 8, {194, 190, 184, 255});
                    stoneMb.addCylinderY({px, py + 2.0f, pz}, 7.6f, 7.6f, 6.8f, 8, {164, 48, 40, 255});
                    addPalaceGiwaRoof(px, py + 8.6f, pz, 20.0f, 20.0f, 5.2f);
                    break;
                }

                case SeoulLandmarkKind::GyeongbokgungPalace: {
                    // Gyeongbokgung Geunjeongjeon Throne Hall + 3-Arch Gwanghwamun Gate + Gwanghwamun Square Statues (Facing South +Z!)
                    stoneMb.addBox({lx, baseY + 2.0f, lz}, {68.0f, 4.0f, 52.0f}, {206, 202, 194, 255});
                    stoneMb.addBox({lx, baseY + 5.2f, lz}, {56.0f, 2.8f, 42.0f}, {214, 210, 202, 255});
                    stoneMb.addBox({lx, baseY + 11.5f, lz}, {40.0f, 10.5f, 28.0f}, {162, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 16.8f, lz}, {42.5f, 1.8f, 30.5f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 17.6f, lz, 48.0f, 36.0f, 6.2f);
                    stoneMb.addBox({lx, baseY + 24.5f, lz}, {30.0f, 5.8f, 20.0f}, {168, 48, 40, 255});
                    stoneMb.addBox({lx, baseY + 27.6f, lz}, {32.0f, 1.4f, 22.0f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 28.3f, lz, 38.0f, 27.0f, 8.2f);

                    // Gwanghwamun Gate 142m south of Geunjeongjeon (z = -5654, 3 Hongyemun stone piers + palace walls!)
                    const float gz = lz + 142.0f;
                    stoneMb.addBox({lx - 22.0f, baseY + 4.5f, gz}, {14.0f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx -  6.5f, baseY + 4.5f, gz}, { 4.8f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx +  6.5f, baseY + 4.5f, gz}, { 4.8f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx + 22.0f, baseY + 4.5f, gz}, {14.0f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx, baseY + 9.2f, gz}, {58.0f, 2.8f, 14.2f}, {208, 202, 192, 255});
                    stoneMb.addBox({lx, baseY + 13.8f, gz}, {34.0f, 6.6f, 12.0f}, {166, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 17.2f, gz}, {36.0f, 1.3f, 13.5f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 17.8f, gz, 44.0f, 19.0f, 6.4f);
                    // Palace Stone Rampart Walls (궁장) running East & West from Gwanghwamun!
                    stoneMb.addBox({lx - 86.0f, baseY + 2.8f, gz}, {116.0f, 5.6f, 2.4f}, {202, 196, 186, 255});
                    stoneMb.addBox({lx + 86.0f, baseY + 2.8f, gz}, {116.0f, 5.6f, 2.4f}, {202, 196, 186, 255});
                    // Gwanghwamun Square Iconic Statues along Sejong-daero facing South (+Z)!
                    // King Sejong the Great (세종대왕상 — seated royal bronze statue on carved granite plinth!)
                    const float sjZ = gz + 205.0f;
                    stoneMb.addBox({lx, baseY + 1.1f, sjZ}, {7.6f, 2.2f, 6.8f}, {198, 194, 186, 255});
                    stoneMb.addBox({lx, baseY + 2.55f, sjZ}, {5.4f, 0.85f, 4.8f}, {184, 180, 172, 255});
                    const ColorRGBA kSejongBronze{184, 144, 62, 255};
                    metalMb.addBox({lx, baseY + 3.65f, sjZ}, {3.2f, 1.4f, 2.8f}, kSejongBronze); // Seated royal robe base
                    metalMb.addBox({lx, baseY + 4.95f, sjZ - 0.15f}, {2.0f, 1.55f, 1.45f}, kSejongBronze); // Royal torso
                    metalMb.addSphere({lx, baseY + 6.05f, sjZ - 0.10f}, 0.56f, 7, 10, kSejongBronze); // Head + 익선관
                    // Admiral Yi Sun-sin (충무공 이순신 장군상 — tall granite pedestal + standing dark bronze statue!)
                    const float yiZ = gz + 415.0f;
                    stoneMb.addBox({lx, baseY + 1.2f, yiZ}, {6.8f, 2.4f, 6.8f}, {188, 184, 176, 255});
                    stoneMb.addBox({lx, baseY + 5.2f, yiZ}, {3.8f, 5.8f, 3.8f}, {178, 174, 168, 255});
                    const ColorRGBA kYiBronze{72, 68, 58, 255};
                    metalMb.addCylinderY({lx, baseY + 8.1f, yiZ}, 0.85f, 0.72f, 3.6f, 10, kYiBronze);
                    metalMb.addSphere({lx, baseY + 12.1f, yiZ}, 0.48f, 7, 10, kYiBronze);
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
                    // 4 continuous smooth sloped golden glass facades!
                    glassMb.addQuad({b0.x, baseY, b0.z}, {t0.x, baseY + hTop, t0.z}, {t1.x, baseY + hTop, t1.z}, {b1.x, baseY, b1.z}, kGoldGlass);
                    glassMb.addQuad({b1.x, baseY, b1.z}, {t1.x, baseY + hTop, t1.z}, {t2.x, baseY + hTop, t2.z}, {b2.x, baseY, b2.z}, kGoldGlass);
                    glassMb.addQuad({b2.x, baseY, b2.z}, {t2.x, baseY + hTop, t2.z}, {t3.x, baseY + hTop, t3.z}, {b3.x, baseY, b3.z}, kGoldGlass);
                    glassMb.addQuad({b3.x, baseY, b3.z}, {t3.x, baseY + hTop, t3.z}, {t0.x, baseY + hTop, t0.z}, {b0.x, baseY, b0.z}, kGoldGlass);
                    // Roof cap & golden crown glow
                    glassMb.addQuad({t0.x, baseY + hTop, t0.z}, {t3.x, baseY + hTop, t3.z}, {t2.x, baseY + hTop, t2.z}, {t1.x, baseY + hTop, t1.z}, kGoldGlass);
                    addRotBox(glowMb, lx, baseY + hTop + 1.8f, lz, 0.0f, 0.0f, {36.0f, 3.2f, 9.2f}, yaw, {255, 224, 128, 255});
                    break;
                }

                case SeoulLandmarkKind::NationalAssembly: {
                    // National Assembly of South Korea: Oriented facing EAST-SOUTHEAST (+X/+Z, yaw=0.18) down Uisadang-daero!
                    addRotBox(stoneMb, lx, baseY + 14.0f, lz, 0.0f, 0.0f, {56.0f, 28.0f, 82.0f}, yaw, {214, 212, 206, 255});
                    // Monumental East-facing ceremonial granite staircase (+X front)!
                    for (int st = 0; st < 4; ++st) {
                        const float sf = static_cast<float>(st);
                        addRotBox(stoneMb, lx, baseY + 0.9f + sf * 1.4f, lz, 32.0f + (3.0f - sf) * 3.8f, 0.0f,
                                  {6.0f, 1.5f, 38.0f}, yaw, {222, 220, 214, 255});
                    }
                    // 24 vertical classical granite columns around perimeter (front & back face East/West!)
                    for (int c = -5; c <= 5; ++c) {
                        const float czOff = static_cast<float>(c) * 7.2f;
                        addRotBox(stoneMb, lx, baseY + 14.5f, lz,  29.2f, czOff, {2.3f, 27.5f, 2.3f}, yaw, {228, 226, 220, 255});
                        addRotBox(stoneMb, lx, baseY + 14.5f, lz, -29.2f, czOff, {2.3f, 27.5f, 2.3f}, yaw, {228, 226, 220, 255});
                    }
                    // Iconic ribbed mint-green oxidized copper dome!
                    metalMb.addCylinderY({lx, baseY + 28.0f, lz}, 24.0f, 22.0f, 8.0f, 20, {196, 194, 188, 255});
                    metalMb.addSphere({lx, baseY + 36.0f, lz}, 22.5f, 10, 20, {72, 154, 134, 255});
                    break;
                }

                case SeoulLandmarkKind::Parc1RedTower: {
                    // Yeouido Parc1 Towers (333m + 256m) rotated along Yeouido-daero with signature crimson exoskeleton!
                    addRotBox(glassMb, lx, baseY + 166.0f, lz, -28.0f, 0.0f, {34.0f, 332.0f, 34.0f}, yaw, {132, 164, 192, 255});
                    addRotBox(glassMb, lx, baseY + 128.0f, lz,  34.0f, 8.0f, {32.0f, 256.0f, 32.0f}, yaw, {136, 168, 196, 255});
                    const ColorRGBA kParcRed{208, 36, 44, 255};
                    for (float sx : {-1.0f, 1.0f}) {
                        for (float sz : {-1.0f, 1.0f}) {
                            addRotBox(metalMb, lx, baseY + 166.0f, lz, -28.0f + sx * 17.6f, sz * 17.6f, {2.8f, 334.0f, 2.8f}, yaw, kParcRed);
                            addRotBox(metalMb, lx, baseY + 128.0f, lz,  34.0f + sx * 16.6f, 8.0f + sz * 16.6f, {2.6f, 258.0f, 2.6f}, yaw, kParcRed);
                        }
                    }
                    for (int h = 1; h <= 5; ++h) {
                        addRotBox(metalMb, lx, baseY + static_cast<float>(h) * 58.0f, lz, -28.0f, 0.0f, {37.5f, 3.2f, 37.5f}, yaw, kParcRed);
                    }
                    break;
                }

                case SeoulLandmarkKind::CoexTradeTower: {
                    // Samseong-dong COEX Trade Tower (228m stepped chevron facing East onto Yeongdong-daero!) + NW COEX Mall
                    glassMb.addBox({lx, baseY + 95.0f, lz}, {34.0f, 190.0f, 38.0f}, {148, 184, 214, 255});
                    glassMb.addBox({lx, baseY + 206.0f, lz}, {26.0f, 32.0f, 28.0f}, {162, 196, 224, 255});
                    glassMb.addBox({lx, baseY + 225.0f, lz}, {18.0f, 12.0f, 20.0f}, {176, 208, 232, 255});
                    // COEX Convention Center sprawling Northwest behind Trade Tower
                    stoneMb.addBox({lx - 64.0f, baseY + 15.0f, lz - 42.0f}, {86.0f, 30.0f, 72.0f}, {198, 204, 212, 255});
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
                    // Historic stone Seoul Metropolitan Library (Old City Hall) directly South facing Seoul Plaza!
                    stoneMb.addBox({lx, baseY + 11.0f, lz + 18.0f}, {54.0f, 22.0f, 18.0f}, {208, 200, 186, 255});
                    stoneMb.addBox({lx, baseY + 25.0f, lz + 18.0f}, {16.0f, 9.0f, 14.0f}, {214, 206, 192, 255});
                    metalMb.addCylinderY({lx, baseY + 29.5f, lz + 18.0f}, 6.2f, 2.2f, 5.5f, 12, {82, 134, 122, 255});
                    break;
                }

                case SeoulLandmarkKind::SungnyemunGate: {
                    // Sungnyemun (Namdaemun): centered on Sejong-daero (x=-924) with N-S stone arch & E-W fortress wall wings!
                    stoneMb.addBox({lx - 12.0f, baseY + 4.5f, lz}, {13.0f, 9.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx + 12.0f, baseY + 4.5f, lz}, {13.0f, 9.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx, baseY + 8.8f, lz}, {37.0f, 2.8f, 14.0f}, {192, 186, 178, 255});
                    // Short Hanyangdoseong stone wall wings extending East & West
                    stoneMb.addBox({lx - 26.0f, baseY + 2.8f, lz}, {16.0f, 5.6f, 3.6f}, {182, 176, 168, 255});
                    stoneMb.addBox({lx + 26.0f, baseY + 2.8f, lz}, {16.0f, 5.6f, 3.6f}, {182, 176, 168, 255});
                    stoneMb.addBox({lx, baseY + 13.4f, lz}, {25.0f, 6.2f, 11.0f}, {164, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 16.4f, lz}, {26.5f, 1.2f, 12.2f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 16.8f, lz, 32.0f, 16.5f, 4.8f);
                    stoneMb.addBox({lx, baseY + 22.8f, lz}, {18.5f, 4.2f, 9.0f}, {164, 46, 38, 255});
                    addPalaceGiwaRoof(lx, baseY + 24.8f, lz, 26.0f, 13.5f, 5.4f);
                    break;
                }

                case SeoulLandmarkKind::DongdaemunDDP: {
                    // Dongdaemun Design Plaza (DDP): continuous sweeping curved silver-aluminum Zaha Hadid pavilion E-W!
                    const int segs = 14;
                    for (int s = 0; s < segs; ++s) {
                        const float u = static_cast<float>(s) / static_cast<float>(segs - 1);
                        const float dx = lerpf(-58.0f, 58.0f, u);
                        const float dz = std::sin(u * kPi * 1.4f) * 18.0f;
                        const float w = 22.0f + 10.0f * std::sin(u * kPi);
                        const float h = 16.0f + 11.0f * std::sin(u * kPi);
                        addRotBox(metalMb, lx, baseY + h * 0.5f, lz, dx, dz, {14.0f, h, w}, yaw + std::cos(u * kPi * 1.4f) * 0.25f, {214, 220, 228, 255});
                        if (s % 2 == 0) {
                            addRotBox(glowMb, lx, baseY + h + 0.5f, lz, dx, dz, {12.0f, 0.9f, w * 0.72f}, yaw + std::cos(u * kPi * 1.4f) * 0.25f, {192, 228, 255, 255});
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
                        const Vec3 o0b{lx + std::cos(a0) * rxOut, baseY,      lz + std::sin(a0) * rzOut};
                        const Vec3 o1b{lx + std::cos(a1) * rxOut, baseY,      lz + std::sin(a1) * rzOut};
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
    const Mat4 view = Mat4::lookAt(camera.position, camera.target, camera.up);
    const Mat4 proj = Mat4::perspectiveVulkan(camera.fovyDeg * (kPi / 180.0f), aspect, camera.nearZ, camera.farZ);
    ubo.viewProj = proj * view;
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
    ubo.lightDirAndShadows[3] = shadowsActive ? 1.0f : 0.0f;

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

void Renderer3D::drawSeoulTerrainAndRoads(VulkanContext& vk, const GameWorld& world,
                                          const Vec2& focus, bool shadowPass) {
    const CityMap& city = world.city();
    const float pitch = CityMap::kBlockPitch;

    if (!shadowPass) {
        // 1. Vast distant Seoul urban basin ground north & south of Han River
        drawBox(vk, {0.0f, -0.45f, -7200.0f}, {32000.0f, 0.5f, 11800.0f}, 0.0f,
                {116, 120, 126, 255}, 0.88f, 0.0f, 0.0f, MatId::SidewalkPavers, false);
        drawBox(vk, {0.0f, -0.45f,  7200.0f}, {32000.0f, 0.5f, 11800.0f}, 0.0f,
                {116, 120, 126, 255}, 0.88f, 0.0f, 0.0f, MatId::SidewalkPavers, false);

        // 2. Shimmering 30km Han River S-curve + Seokchon Lake + 4 Real Seoul Tributaries!
        drawMesh(vk, hanRiverWaterMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.06f, 0.06f, 0.0f, MatId::HanRiverWater, false);
        drawMesh(vk, tributaryWaterMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.06f, 0.06f, 0.0f, MatId::HanRiverWater, false);

        // 2B. Real Curved 30km 강변북로 (Gangbyeonbuk-ro) & 올림픽대로 (Olympic-daero)!
        drawMesh(vk, riversideExpresswayMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.56f, 0.0f, 0.0f, MatId::RoadAsphalt, false);

        // 3. 7 Real Seoul Mountains
        drawMesh(vk, mountainForestMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.90f, 0.0f, 0.0f, MatId::FoliageGrass, false);
        drawMesh(vk, mountainGraniteMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.85f, 0.02f, 0.0f, MatId::GraniteRock, false);
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
        for (int ix = centerRx - radiusBlocks; ix <= centerRx + radiusBlocks; ++ix) {
            const float rx = city.roadCoord(ix);
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

    // 1. Always-visible 12 Real Seoul Macro Landmarks!
    drawMesh(vk, landmarkGlassMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.10f, 0.32f, 0.0f, MatId::GlassSkyscraper, shadowPass);
    drawMesh(vk, landmarkStoneMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.72f, 0.06f, 0.0f, MatId::ModernConcrete, shadowPass);
    drawMesh(vk, landmarkMetalMesh_, Mat4::identity(), {255, 255, 255, 255},
             0.28f, 0.78f, 0.0f, MatId::Standard, shadowPass);
    drawMesh(vk, landmarkGiwaMesh_,  Mat4::identity(), {255, 255, 255, 255},
             0.56f, 0.08f, 0.0f, MatId::HanokGiwaRoof, shadowPass);
    if (!shadowPass) {
        drawMesh(vk, landmarkGlowMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.20f, 0.10f, nightMode ? 1.45f : 0.42f, MatId::EmissiveGlow, false);
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
            // Korean 4~5F Multi-Family Villa: sloped dark charcoal slate roof + rooftop stair cube!
            drawBox(vk, {c.x, roofY + 0.35f, c.z}, {sz.x + 0.45f, 0.72f, sz.z + 0.45f}, 0.0f,
                    {64, 68, 76, 255}, 0.68f, 0.08f, 0.0f, MatId::Standard, shadowPass);
            drawBox(vk, {c.x, roofY + 1.15f, c.z}, {sz.x * 0.84f, 1.05f, sz.z * 0.84f}, 0.0f,
                    {54, 58, 66, 255}, 0.72f, 0.08f, 0.0f, MatId::Standard, shadowPass);
            if (!shadowPass && distSq < 220.0f * 220.0f) {
                drawBox(vk, {c.x + sz.x * 0.18f, roofY + 2.3f, c.z - sz.z * 0.12f}, {3.6f, 2.2f, 3.2f}, 0.0f,
                        b.primaryColor, 0.78f, 0.04f, 0.0f, MatId::Standard, false);
            }
        } else if (!shadowPass && distSq < 260.0f * 260.0f) {
            // Crisp rooftop parapet coping wall + realistic Seoul elevator penthouse & HVAC units!
            drawBox(vk, {c.x, roofY + 0.42f, c.z}, {sz.x + 0.25f, 0.85f, sz.z + 0.25f}, 0.0f,
                    b.accentColor, 0.66f, 0.12f, 0.0f, MatId::Standard, false);

            if (b.style == BuildingStyle::KoreanApartment) {
                // Iconic Korean apartment twin rooftop elevator cores + brand top color band!
                drawBox(vk, {c.x - sz.x * 0.24f, roofY + 2.1f, c.z}, {6.4f, 3.6f, sz.z * 0.44f}, 0.0f,
                        b.accentColor, 0.72f, 0.06f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x + sz.x * 0.24f, roofY + 2.1f, c.z}, {6.4f, 3.6f, sz.z * 0.44f}, 0.0f,
                        b.accentColor, 0.72f, 0.06f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x, roofY - 1.1f, c.z}, {sz.x + 0.16f, 1.4f, sz.z + 0.16f}, 0.0f,
                        b.accentColor, 0.74f, 0.06f, 0.0f, MatId::Standard, false);
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

            // ORIENTATION-AWARE STREET-FACING KOREAN STOREFRONTS & SIGNBOARDS!
            // ONLY placed on facade edges actually bordering a street (b.streetEdgeMask)!
            // Eliminates the ugly wrap-around red stripe on all 4 sides!
            if (b.hasStorefront && distSq < 195.0f * 195.0f) {
                const float signY = b.baseY + 3.92f;
                const float canopyY = b.baseY + 3.35f;
                const ColorRGBA canopyCol{48, 54, 64, 255};
                const float signEm = nightMode ? 0.72f : 0.04f;
                const MatId signMat = nightMode ? MatId::EmissiveGlow : MatId::Standard;

                // bit 0: West (-X) street edge
                if (b.streetEdgeMask & 0x01u) {
                    drawBox(vk, {b.footprint.min.x - 0.18f, signY, c.z}, {0.32f, 0.78f, sz.z * 0.84f}, 0.0f,
                            b.awningColor, 0.42f, 0.15f, signEm, signMat, false);
                    drawBox(vk, {b.footprint.min.x - 0.55f, canopyY, c.z}, {1.05f, 0.12f, sz.z * 0.62f}, 0.0f,
                            canopyCol, 0.34f, 0.55f, 0.0f, MatId::Standard, false);
                    // Vertical projecting Korean commercial signbox (돌출간판) on 2F~3F!
                    if (b.height > 12.0f && (b.facadeVariant & 1) == 0) {
                        drawBox(vk, {b.footprint.min.x - 0.52f, b.baseY + 7.8f, c.z + sz.z * 0.30f},
                                {0.85f, 3.6f, 0.26f}, 0.0f, b.awningColor, 0.32f, 0.12f, nightMode ? 1.05f : 0.06f, signMat, false);
                    }
                }
                // bit 1: East (+X) street edge
                if (b.streetEdgeMask & 0x02u) {
                    drawBox(vk, {b.footprint.max.x + 0.18f, signY, c.z}, {0.32f, 0.78f, sz.z * 0.84f}, 0.0f,
                            b.awningColor, 0.42f, 0.15f, signEm, signMat, false);
                    drawBox(vk, {b.footprint.max.x + 0.55f, canopyY, c.z}, {1.05f, 0.12f, sz.z * 0.62f}, 0.0f,
                            canopyCol, 0.34f, 0.55f, 0.0f, MatId::Standard, false);
                    if (b.height > 12.0f && (b.facadeVariant & 1) == 1) {
                        drawBox(vk, {b.footprint.max.x + 0.52f, b.baseY + 7.8f, c.z - sz.z * 0.30f},
                                {0.85f, 3.6f, 0.26f}, 0.0f, b.awningColor, 0.32f, 0.12f, nightMode ? 1.05f : 0.06f, signMat, false);
                    }
                }
                // bit 2: North (-Z) street edge
                if (b.streetEdgeMask & 0x04u) {
                    drawBox(vk, {c.x, signY, b.footprint.min.z - 0.18f}, {sz.x * 0.84f, 0.78f, 0.32f}, 0.0f,
                            b.awningColor, 0.42f, 0.15f, signEm, signMat, false);
                    drawBox(vk, {c.x, canopyY, b.footprint.min.z - 0.55f}, {sz.x * 0.62f, 0.12f, 1.05f}, 0.0f,
                            canopyCol, 0.34f, 0.55f, 0.0f, MatId::Standard, false);
                }
                // bit 3: South (+Z) street edge
                if (b.streetEdgeMask & 0x08u) {
                    drawBox(vk, {c.x, signY, b.footprint.max.z + 0.18f}, {sz.x * 0.84f, 0.78f, 0.32f}, 0.0f,
                            b.awningColor, 0.42f, 0.15f, signEm, signMat, false);
                    drawBox(vk, {c.x, canopyY, b.footprint.max.z + 0.55f}, {sz.x * 0.62f, 0.12f, 1.05f}, 0.0f,
                            canopyCol, 0.34f, 0.55f, 0.0f, MatId::Standard, false);
                }
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
    const float treeRadius = shadowPass ? 115.0f : 280.0f;

    // 1. Seoul Street Trees (Organic multi-cluster Ginkgo, Cherry Blossom, Korean Pine!)
    visIdxBuf_.clear();
    city.queryTreesInRadius(focus, treeRadius, visIdxBuf_);
    for (int ti : visIdxBuf_) {
        const TreeProp& t = city.trees()[static_cast<std::size_t>(ti)];
        const float y0 = t.baseY;

        if (t.inPlanter && !shadowPass) {
            // Crisp Korean granite tree curb surround + dark mulch soil bed
            drawBox(vk, {t.pos.x, y0 + 0.16f, t.pos.z}, {1.85f, 0.28f, 1.85f}, 0.0f,
                    {142, 144, 148, 255}, 0.82f, 0.0f, 0.0f, MatId::SidewalkPavers, false);
            drawBox(vk, {t.pos.x, y0 + 0.24f, t.pos.z}, {1.45f, 0.16f, 1.45f}, 0.0f,
                    {68, 54, 42, 255}, 0.94f, 0.0f, 0.0f, MatId::Standard, false);
        }
        // Tapered bark trunk with subtle natural yaw variation per tree
        const float treeYaw = std::fmod(std::abs(t.pos.x * 0.17f + t.pos.z * 0.31f), kTwoPi);
        Mat4 trunkM = Mat4::translation(t.pos.x, y0, t.pos.z) *
                      Mat4::scale(0.36f, t.trunkHeight, 0.36f);
        drawMesh(vk, cylMesh_, trunkM, {88, 66, 48, 255}, 0.90f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);

        if (t.species == TreeSpecies::KoreanPine) {
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight * 0.65f, t.pos.z) *
                      Mat4::rotationAxis({0, 1, 0}, treeYaw) *
                      Mat4::scale(t.crownRadius * 1.42f, t.crownRadius * 2.15f, t.crownRadius * 1.42f);
            drawMesh(vk, pineMesh_, cm, {40, 82, 48, 255}, 0.88f, 0.0f, 0.0f, MatId::FoliageGrass, shadowPass);
        } else {
            const ColorRGBA crownCol = (t.species == TreeSpecies::CherryBlossom)
                ? ColorRGBA{244, 192, 210, 255}
                : ColorRGBA{78, 122, 48, 255};
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight + t.crownRadius * 0.16f, t.pos.z) *
                      Mat4::rotationAxis({0, 1, 0}, treeYaw) *
                      Mat4::scale(t.crownRadius * 1.15f, t.crownRadius * 1.05f, t.crownRadius * 1.15f);
            drawMesh(vk, foliageMesh_, cm, crownCol, 0.86f, 0.0f, 0.0f, MatId::FoliageGrass, shadowPass);
        }
    }

    // 2. Properly Oriented Seoul Street Props (Streetlamps, 4-Aspect Korean Traffic Lights, Bus Shelters, Subway Entrances!)
    const float propRadius = shadowPass ? 95.0f : 220.0f;
    const int sigPhase = static_cast<int>(std::fmod(world.elapsedTime(), 18.0f));
    visIdxBuf_.clear();
    city.queryPropsInRadius(focus, propRadius, visIdxBuf_);
    for (int pi : visIdxBuf_) {
        const StreetProp& p = city.streetProps()[static_cast<std::size_t>(pi)];
        const float gy = city.surfaceHeightAt(p.pos) + p.posY;
        const float distSq = (Vec3{p.pos.x, gy, p.pos.z} - camPos).lengthSq();
        if (!shadowPass && distSq > propRadius * propRadius) continue;

        Mat4 baseM = Mat4::translation(p.pos.x, gy, p.pos.z) *
                     Mat4::rotationAxis({0, 1, 0}, -p.yaw);
        if (p.broken) {
            baseM = baseM * Mat4::rotationAxis({1, 0, 0}, p.roll) * Mat4::rotationAxis({0, 0, 1}, p.pitch);
        }

        switch (p.kind) {
            case StreetPropKind::StreetLight: {
                // Tapered steel Seoul streetlamp post + outward luminaire arm extending OVER the roadway (+X local)!
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.18f, 7.2f, 0.18f),
                         {82, 88, 98, 255}, 0.36f, 0.74f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(1.05f, 7.12f, 0.0f) * Mat4::scale(2.25f, 0.13f, 0.20f),
                         {86, 92, 102, 255}, 0.36f, 0.74f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(1.92f, 7.00f, 0.0f) * Mat4::scale(0.62f, 0.12f, 0.28f),
                             {255, 240, 206, 255}, 0.15f, 0.0f, nightMode ? 2.6f : 0.2f, MatId::EmissiveGlow, false);
                }
                break;
            }
            case StreetPropKind::TrafficLight: {
                // Authentic Korean horizontal 4-aspect cantilevered traffic signal extending OVER the intersection (+X local)!
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.24f, 6.2f, 0.24f),
                         {52, 56, 64, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(2.6f, 5.95f, 0.0f) * Mat4::scale(5.3f, 0.18f, 0.18f),
                         {56, 60, 68, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                // Horizontal black Korean 4-lamp signal housing
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(3.5f, 5.65f, 0.0f) * Mat4::scale(1.85f, 0.48f, 0.34f),
                         {24, 26, 30, 255}, 0.42f, 0.25f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    // Alternating live Red / Yellow / Green Korean traffic lamps!
                    const bool isGreen = (sigPhase < 9);
                    const bool isAmber = (sigPhase >= 9 && sigPhase < 11);
                    const bool isRed   = (sigPhase >= 11);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(2.85f, 5.65f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.38f),
                             isRed ? ColorRGBA{255, 48, 48, 255} : ColorRGBA{58, 20, 20, 255},
                             0.15f, 0.0f, isRed ? 2.6f : 0.0f, isRed ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(3.45f, 5.65f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.38f),
                             isAmber ? ColorRGBA{255, 198, 36, 255} : ColorRGBA{58, 48, 16, 255},
                             0.15f, 0.0f, isAmber ? 2.4f : 0.0f, isAmber ? MatId::EmissiveGlow : MatId::Standard, false);
                    drawMesh(vk, sphereMesh_, baseM * Mat4::translation(4.05f, 5.65f, 0.0f) * Mat4::scale(0.24f, 0.24f, 0.38f),
                             isGreen ? ColorRGBA{42, 242, 118, 255} : ColorRGBA{18, 52, 32, 255},
                             0.15f, 0.0f, isGreen ? 2.6f : 0.0f, isGreen ? MatId::EmissiveGlow : MatId::Standard, false);
                    // Pedestrian crossing signal box at 3.1m height on vertical pole
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 3.15f, 0.24f) * Mat4::scale(0.32f, 0.68f, 0.24f),
                             {26, 28, 32, 255}, 0.42f, 0.20f, 0.0f, MatId::Standard, false);
                }
                break;
            }
            case StreetPropKind::BusShelter: {
                // Seoul Bus Shelter: Glass back wall (-Z) on building side, Open boarding side & Bench (+Z) facing ROAD!
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 2.62f, 0) * Mat4::scale(4.6f, 0.18f, 1.65f),
                         {42, 48, 58, 255}, 0.34f, 0.68f, 0.0f, MatId::Standard, shadowPass);
                // Signature Seoul Blue Bus Route Header Strip facing the road (+Z)!
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 2.76f, 0.72f) * Mat4::scale(4.5f, 0.24f, 0.12f),
                         {32, 108, 214, 255}, 0.25f, 0.15f, nightMode ? 1.1f : 0.12f,
                         nightMode ? MatId::EmissiveGlow : MatId::Standard, false);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 1.30f, -0.72f) * Mat4::scale(4.2f, 2.45f, 0.10f),
                         {74, 118, 152, 255}, 0.12f, 0.28f, 0.0f, MatId::GlassSkyscraper, shadowPass);
                // Side pillars
                for (float sx : {-2.05f, 2.05f}) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(sx, 1.30f, -0.68f) * Mat4::scale(0.14f, 2.60f, 0.14f),
                             {48, 54, 64, 255}, 0.36f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                }
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.52f, -0.25f) * Mat4::scale(3.0f, 0.12f, 0.45f),
                         {136, 96, 60, 255}, 0.75f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);
                break;
            }
            case StreetPropKind::SubwayEntrance: {
                // Authentic Seoul Metro Glass-Canopy Subway Entrance aligned neatly along the sidewalk!
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.55f, 0.0f) * Mat4::scale(5.4f, 0.20f, 2.6f),
                         {52, 58, 68, 255}, 0.30f, 0.74f, 0.0f, MatId::Standard, shadowPass);
                // Line 2 Seoul Metro Green / Line 1 Navy illuminated header fascia
                const ColorRGBA lineCol = ((static_cast<int>(p.pos.x) & 1) == 0)
                    ? ColorRGBA{42, 166, 78, 255}  // Seoul Metro Line 2 Green
                    : ColorRGBA{36, 98, 198, 255}; // Seoul Metro Line 1 Blue
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 2.74f, 0.0f) * Mat4::scale(5.5f, 0.32f, 2.7f),
                         lineCol, 0.24f, 0.18f, nightMode ? 1.25f : 0.15f,
                         nightMode ? MatId::EmissiveGlow : MatId::Standard, false);
                // Glass side balustrades + dark stairwell opening
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 1.25f, -1.20f) * Mat4::scale(5.2f, 2.4f, 0.10f),
                         {78, 128, 162, 255}, 0.12f, 0.28f, 0.0f, MatId::GlassSkyscraper, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.0f, 1.25f,  1.20f) * Mat4::scale(5.2f, 2.4f, 0.10f),
                         {78, 128, 162, 255}, 0.12f, 0.28f, 0.0f, MatId::GlassSkyscraper, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(-1.0f, 0.35f, 0.0f) * Mat4::scale(3.0f, 0.65f, 2.1f),
                         {32, 36, 42, 255}, 0.80f, 0.0f, 0.0f, MatId::Standard, false);
                break;
            }
            case StreetPropKind::Bench:
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.48f, 0) * Mat4::scale(1.85f, 0.12f, 0.55f),
                         {142, 96, 58, 255}, 0.78f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.82f, -0.24f) * Mat4::scale(1.85f, 0.42f, 0.09f),
                         {142, 96, 58, 255}, 0.78f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);
                break;
            case StreetPropKind::Hydrant:
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.32f, 0.85f, 0.32f),
                         {216, 44, 44, 255}, 0.42f, 0.35f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::TrafficCone:
                drawMesh(vk, coneMesh_, baseM * Mat4::scale(0.45f, 0.75f, 0.45f),
                         {242, 106, 26, 255}, 0.55f, 0.0f, 0.0f, MatId::Standard, shadowPass);
                break;
            case StreetPropKind::TrashBin:
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.48f, 0.96f, 0.48f),
                         {64, 72, 82, 255}, 0.50f, 0.50f, 0.0f, MatId::Standard, shadowPass);
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
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.22f, 0.85f, 0.22f),
                         {88, 94, 102, 255}, 0.36f, 0.72f, 0.0f, MatId::Standard, shadowPass);
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
                             bool cockpitHoodOnly) {
    const CarMeshSet& cm = cars_.get(v.kind);
    const WheelMeshSet& wm = cars_.wheel();
    const auto& spec = getVehicleSpec(v.kind);

    float stanceOffsetY = 0.0f;
    if (v.stanceStyle == 1) stanceOffsetY = -0.045f;
    else if (v.stanceStyle == 2) stanceOffsetY = -0.075f;
    else if (v.stanceStyle == 3) stanceOffsetY = +0.085f;

    const float baseH = groundY + v.posY + stanceOffsetY;
    const Mat4 rootPos = Mat4::translation(v.pos.x, baseH, v.pos.z) *
                         Mat4::rotationAxis({0, 1, 0}, -v.heading);

    // Body pitch (nose up = +Z rot) & cornering roll (lean right = +X rot)
    const float avgComp = 0.25f * (v.wheelCompression[0] + v.wheelCompression[1] +
                                   v.wheelCompression[2] + v.wheelCompression[3]);
    const Mat4 bodyMat = rootPos *
                         Mat4::translation(0.0f, -avgComp * 0.65f, 0.0f) *
                         Mat4::rotationAxis({0, 0, 1}, v.bodyPitch) *
                         Mat4::rotationAxis({1, 0, 0}, v.bodyRoll);

    if (alphaPass) {
        if (cockpitHoodOnly) return;
        // Flush curved smoked automotive glass (matId=8 -> mirror-like Seoul reflections + dark 3D cabin visible inside!)
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

    // 2. Dark 3D Interior Cabin (seats, steering wheel, dashboard visible cleanly behind glass!)
    if (!shadowPass) {
        drawMesh(vk, cm.interior, bodyMat, {255, 255, 255, 255}, 0.80f, 0.04f, 0.0f, MatId::Standard, false);
    }

    // 3. Matte-Black Exterior Trim (DLO frames, grilles, shutlines, wheel-well liners, mirrors, diffuser!)
    drawMesh(vk, cm.trim, bodyMat, {255, 255, 255, 255}, 0.46f, 0.18f, 0.0f, MatId::Standard, shadowPass);

    // 4. Brushed Chrome Accents (grille slats, exhaust tips, door handles, badges, license plates)
    if (!shadowPass) {
        drawMesh(vk, cm.chrome, bodyMat, {255, 255, 255, 255}, 0.15f, 0.92f, 0.0f, MatId::CarClearcoat, false);
    }

    // 5. Clean Zero-Z-Fighting Livery (Police lightbar/door band, Taxi sign, 119 Ambulance/Fire, Van band)
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

    // 8. Sculpted 3D Headlamps & Taillights
    if (!shadowPass && !v.destroyed) {
        const ColorRGBA hlCol = getVehicleHeadlightColorRGBA(v.headlightColorIdx, nightMode);
        const float hlEm = nightMode ? 2.4f : 0.55f;
        drawMesh(vk, cm.lampsHead, bodyMat, hlCol, 0.08f, 0.10f, hlEm, MatId::EmissiveGlow, false);

        const float tlEm = v.brakingVisual ? 2.65f : (nightMode ? 1.35f : 0.28f);
        drawMesh(vk, cm.lampsTail, bodyMat, {255, 42, 48, 255}, 0.08f, 0.10f, tlEm, MatId::EmissiveGlow, false);
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

void Renderer3D::drawCharacter(VulkanContext& vk, const Vec2& pos, float groundY, float yaw,
                               float animPhase, ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                               bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                               float aimPitch, OutfitId outfit, bool shadowPass,
                               bool jumping, bool panicking, bool isPolice, bool isSwat,
                               const CharacterCustomState* customChar,
                               const WeaponSlotState* customWep,
                               const NpcCharacter* npcPtr, bool nightMode) {
    const float hScale = npcPtr ? npcPtr->heightScale : 1.0f;
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

    // 1. Articulated Two-Segment Legs (Thigh + Shin + Knee Bend + Contoured Shoe!)
    for (float s : {-1.0f, 1.0f}) {
        const float hipAng = s * stride;
        const float kneeAng = alive ? std::max(0.0f, -s * std::cos(animPhase) * 0.36f) : 0.0f;
        const Mat4 hipM = root * Mat4::translation(0.0f, 0.86f, s * 0.115f) *
                          Mat4::rotationAxis({0, 0, 1}, hipAng);
        // Upper thigh capsule
        drawMesh(vk, capsuleLimbMesh_, hipM * Mat4::scale(0.165f, 0.41f, 0.165f),
                 bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        // Lower shin capsule with natural knee articulation
        const Mat4 kneeM = hipM * Mat4::translation(0.0f, -0.41f, 0.0f) *
                           Mat4::rotationAxis({0, 0, 1}, -kneeAng);
        drawMesh(vk, capsuleLimbMesh_, kneeM * Mat4::scale(0.135f, 0.38f, 0.135f),
                 bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        // Contoured sneaker/dress shoe
        drawMesh(vk, sphereMesh_, kneeM * Mat4::translation(0.05f, -0.41f, 0.0f) * Mat4::scale(0.26f, 0.10f, 0.13f),
                 shoeCol, 0.56f, 0.06f, 0.0f, MatId::Standard, shadowPass);
    }

    // 2. Sculpted Pelvis, Belt & Anatomical Tapered Torso (Athletic Shoulders -> Waist!)
    drawMesh(vk, sphereMesh_, root * Mat4::translation(0.0f, 0.86f, 0.0f) * Mat4::scale(0.24f, 0.18f, 0.36f),
             bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
    if (!shadowPass) {
        drawMesh(vk, cylMesh_, root * Mat4::translation(0.0f, 0.88f, 0.0f) * Mat4::scale(0.245f, 0.055f, 0.365f),
                 beltCol, 0.42f, 0.25f, 0.0f, MatId::Standard, false);
    }
    drawMesh(vk, torsoMesh_, root * Mat4::translation(0.0f, 0.90f, 0.0f) * Mat4::scale(0.30f, 0.58f, 0.45f),
             top, 0.74f, 0.04f, 0.0f, MatId::FabricCloth, shadowPass);
    if (!shadowPass) {
        // Subtle inner shirt collar / lapel accent at chest V
        drawMesh(vk, sphereMesh_, root * Mat4::translation(0.09f, 1.38f, 0.0f) * Mat4::scale(0.11f, 0.14f, 0.14f),
                 {228, 232, 238, 255}, 0.72f, 0.02f, 0.0f, MatId::FabricCloth, false);
    }
    if (isSwat || outfit == OutfitId::TacOpsArmor) {
        drawMesh(vk, torsoMesh_, root * Mat4::translation(0.0f, 0.96f, 0.0f) * Mat4::scale(0.34f, 0.46f, 0.47f),
                 {30, 34, 42, 255}, 0.56f, 0.18f, 0.0f, MatId::Standard, shadowPass);
    }

    // 3. Neck, Sculpted Head, Hair & Flush Conformal Eyewear!
    drawMesh(vk, cylMesh_, root * Mat4::translation(0.0f, 1.43f, 0.0f) * Mat4::scale(0.11f, 0.13f, 0.11f),
             skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

    const float headLook = npcPtr ? npcPtr->headLookYaw : 0.0f;
    const Mat4 headM = root * Mat4::translation(0.0f, 1.63f, 0.0f) * Mat4::rotationAxis({0, 1, 0}, -headLook);
    drawMesh(vk, sphereMesh_, headM * Mat4::scale(0.22f, 0.26f, 0.20f),
             skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

    if (!shadowPass) {
        const ColorRGBA hairCol = customChar
            ? getPlayerHairColor(outfit, customChar->hairColorIdx)
            : (isPolice ? ColorRGBA{24, 42, 86, 255} : ColorRGBA{28, 24, 22, 255});
        if (isPolice || isSwat) {
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.01f, 0.06f, 0.0f) * Mat4::scale(0.24f, 0.16f, 0.22f),
                     isSwat ? ColorRGBA{28, 32, 38, 255} : ColorRGBA{22, 42, 92, 255}, 0.45f, 0.15f, 0.0f, MatId::Standard, false);
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.10f, 0.03f, 0.0f) * Mat4::scale(0.11f, 0.022f, 0.16f),
                     {20, 22, 26, 255}, 0.35f, 0.15f, 0.0f, MatId::Standard, false);
        } else {
            // Natural contoured hair crown + back hair volume (never covers eyes!)
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.02f, 0.055f, 0.0f) * Mat4::scale(0.23f, 0.18f, 0.215f),
                     hairCol, 0.72f, 0.04f, 0.0f, MatId::Standard, false);
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.055f, -0.01f, 0.0f) * Mat4::scale(0.17f, 0.16f, 0.19f),
                     hairCol, 0.72f, 0.04f, 0.0f, MatId::Standard, false);
        }
        // Subtle twin dark eyes / flush conformal twin-lens sunglasses (NEVER a floating brick!)
        if ((customChar && customChar->glassesStyle > 0) || (npcPtr && npcPtr->glassesStyle > 0)) {
            for (float es : {-1.0f, 1.0f}) {
                drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.098f, 0.014f, es * 0.042f) * Mat4::scale(0.025f, 0.038f, 0.058f),
                         {14, 16, 22, 255}, 0.06f, 0.45f, 0.0f, MatId::CarGlass, false);
            }
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.104f, 0.018f, 0.0f) * Mat4::scale(0.012f, 0.012f, 0.042f),
                     {24, 26, 30, 255}, 0.30f, 0.75f, 0.0f, MatId::Standard, false);
        } else {
            for (float es : {-1.0f, 1.0f}) {
                drawMesh(vk, sphereMesh_, headM * Mat4::translation(0.098f, 0.014f, es * 0.038f) * Mat4::scale(0.016f, 0.018f, 0.022f),
                         {22, 20, 20, 255}, 0.25f, 0.0f, 0.0f, MatId::Standard, false);
            }
        }
    }

    // 4. Articulated Two-Segment Arms (Upper Arm + Elbow + Forearm + Hand)
    for (float s : {-1.0f, 1.0f}) {
        float shoulderPitch = s * armSwing;
        float elbowBend = 0.14f;
        const bool raisingGun = (armed && s > 0.0f && weapon != WeaponId::Fists);
        if (panicking) {
            shoulderPitch = -2.35f;
            elbowBend = 0.35f;
        } else if (raisingGun) {
            shoulderPitch = -kHalfPi + aimPitch;
            elbowBend = 0.08f;
        } else if (npcPtr && npcPtr->holdingUmbrella && s > 0.0f) {
            shoulderPitch = -1.15f;
            elbowBend = 0.45f;
        }

        const Mat4 shoulderM = root * Mat4::translation(0.0f, 1.365f, s * 0.214f) *
                               Mat4::rotationAxis({0, 0, 1}, shoulderPitch);
        // Upper arm capsule
        drawMesh(vk, capsuleLimbMesh_, shoulderM * Mat4::scale(0.115f, 0.27f, 0.115f),
                 top, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        // Forearm capsule with natural elbow angle
        const Mat4 elbowM = shoulderM * Mat4::translation(0.0f, -0.27f, 0.0f) *
                            Mat4::rotationAxis({0, 0, 1}, elbowBend);
        drawMesh(vk, capsuleLimbMesh_, elbowM * Mat4::scale(0.098f, 0.25f, 0.098f),
                 top, 0.74f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        // Hand
        drawMesh(vk, sphereMesh_, elbowM * Mat4::translation(0.0f, -0.27f, 0.0f) * Mat4::scale(0.095f, 0.105f, 0.085f),
                 skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

        // Held Weapon on Right Arm (only drawn raised when aiming/firing or armed NPC!)
        if (raisingGun && !shadowPass) {
            const float gunLen = (weapon == WeaponId::Pistol || weapon == WeaponId::HeavyRevolver) ? 0.24f
                               : ((weapon == WeaponId::SniperRifle || weapon == WeaponId::HeavyLauncher) ? 0.88f : 0.58f);
            const ColorRGBA wCol = customWep ? getWeaponFinishMaterial(weapon, customWep->skinIndex).primary
                                             : ColorRGBA{38, 42, 48, 255};
            drawMesh(vk, cubeMesh_, elbowM * Mat4::translation(0.05f, -0.29f - gunLen * 0.35f, 0.0f) *
                     Mat4::scale(0.06f, gunLen, 0.065f), wCol, 0.30f, 0.78f, 0.0f, MatId::Standard, false);
            if (muzzleFlash > 0.01f) {
                drawMesh(vk, sphereMesh_, elbowM * Mat4::translation(0.05f, -0.31f - gunLen * 0.88f, 0.0f) *
                         Mat4::scale(0.22f, 0.22f, 0.22f), {255, 224, 102, 240}, 0.1f, 0.0f, 3.2f,
                         MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
            }
        }
    }

    // 5. Korean Rain Umbrella when raining!
    if (npcPtr && npcPtr->holdingUmbrella && alive && !shadowPass) {
        drawMesh(vk, cylMesh_, root * Mat4::translation(0.14f, 1.35f, 0.24f) * Mat4::scale(0.028f, 0.95f, 0.028f),
                 {44, 48, 54, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, false);
        drawMesh(vk, coneMesh_, root * Mat4::translation(0.14f, 2.15f, 0.24f) * Mat4::scale(1.22f, 0.32f, 1.22f),
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
        const float gy = city.surfaceHeightAt(v.pos);
        drawVehicle(vk, v, gy, nightMode, false, true, false);
    }

    // 3. Hollow Ground POI Rings & Active Mission / GPS Waypoint Rings!
    for (const auto& poi : city.pois()) {
        if ((poi.pos - focus).lengthSq() > 260.0f * 260.0f) continue;
        const float gy = city.surfaceHeightAt(poi.pos) + 0.14f;
        const float pulse = 1.0f + 0.06f * std::sin(t * 3.4f);
        ColorRGBA rc = poi.markerColor;
        rc.a = 195;
        Mat4 rm = Mat4::translation(poi.pos.x, gy, poi.pos.z) *
                  Mat4::scale(poi.radius * pulse, 1.0f, poi.radius * pulse);
        drawMesh(vk, ringMesh_, rm, rc, 0.2f, 0.0f, 1.35f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
    }
    if (world.mission().kind != ActiveMissionKind::None) {
        const Vec2 mp = world.mission().currentObjectivePos;
        if ((mp - focus).lengthSq() < 1200.0f * 1200.0f) {
            const float gy = city.surfaceHeightAt(mp) + 0.18f;
            Mat4 rm = Mat4::translation(mp.x, gy, mp.z) * Mat4::scale(world.mission().objectiveRadius, 1.0f, world.mission().objectiveRadius);
            drawMesh(vk, ringMesh_, rm, {255, 206, 48, 210}, 0.2f, 0.0f, 1.8f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
        }
    }
    if (world.player().hasWaypoint) {
        const Vec2 wp = world.player().waypointPos;
        if ((wp - focus).lengthSq() < 2500.0f * 2500.0f) {
            const float gy = city.surfaceHeightAt(wp) + 0.22f;
            Mat4 rm = Mat4::translation(wp.x, gy, wp.z) * Mat4::scale(5.5f, 1.0f, 5.5f);
            drawMesh(vk, ringMesh_, rm, {72, 226, 255, 220}, 0.2f, 0.0f, 2.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Alpha);
            // Tall slender GPS light pillar visible from blocks away
            drawBox(vk, {wp.x, gy + 45.0f, wp.z}, {0.9f, 90.0f, 0.9f}, 0.0f,
                    {72, 226, 255, 92}, 0.1f, 0.0f, 1.8f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
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
        const float gy = city.surfaceHeightAt(mid) + 0.04f;
        const float yaw = std::atan2(d.z, d.x);
        drawBox(vk, {mid.x, gy, mid.z}, {len + 0.08f, 0.02f, 0.24f}, yaw,
                {18, 18, 20, static_cast<std::uint8_t>(clampf(sk.alpha, 0.0f, 1.0f) * 185.0f)},
                0.92f, 0.0f, 0.0f, MatId::Standard, false, WorldPipelineKind::Alpha);
    }

    // 6. Glowing Bullet Tracers
    for (const auto& tr : world.tracers()) {
        const Vec3 mid = (tr.start + tr.end) * 0.5f;
        const Vec3 dir = tr.end - tr.start;
        const float len = dir.length();
        if (len < 0.05f) continue;
        const float yaw = std::atan2(dir.z, dir.x);
        drawBox(vk, mid, {len, 0.05f, 0.05f}, yaw, tr.color,
                0.1f, 0.0f, 3.0f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
    }

    // 7. Particles (Tire Drift Smoke, Explosions, Sparks, Rain Spray!)
    for (const auto& pt : world.particles()) {
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
    if (world.player().drivingVehicleId >= 0) return;
    if (world.player().currentWeapon == WeaponId::Fists) return;

    // Clear depth cleanly so 1st-person weapon never clips into Seoul walls!
    vk.clearSceneDepth();

    const Vec3 fwd = (camera.target - camera.position).normalized();
    const Vec3 right = Vec3{fwd.y * camera.up.z - fwd.z * camera.up.y,
                            fwd.z * camera.up.x - fwd.x * camera.up.z,
                            fwd.x * camera.up.y - fwd.y * camera.up.x}.normalized();
    const Vec3 up = Vec3{right.y * fwd.z - right.z * fwd.y,
                         right.z * fwd.x - right.x * fwd.z,
                         right.x * fwd.y - right.y * fwd.x}.normalized();

    const float sideX = lerpf(0.22f, 0.0f, adsBlend);
    const float downY = lerpf(-0.18f, -0.11f, adsBlend);
    const float fwdZ  = lerpf(0.42f, 0.34f, adsBlend);
    const Vec3 gunPos = camera.position + right * sideX + up * downY + fwd * fwdZ;

    Mat4 basis{};
    basis.m[0] = fwd.x;   basis.m[1] = fwd.y;   basis.m[2] = fwd.z;
    basis.m[4] = up.x;    basis.m[5] = up.y;    basis.m[6] = up.z;
    basis.m[8] = right.x; basis.m[9] = right.y; basis.m[10] = right.z;
    basis.m[12] = gunPos.x; basis.m[13] = gunPos.y; basis.m[14] = gunPos.z;

    const auto& slot = world.player().weapons[static_cast<std::size_t>(world.player().currentWeapon)];
    const auto mat = getWeaponFinishMaterial(world.player().currentWeapon, slot.skinIndex);

    drawMesh(vk, cubeMesh_, basis * Mat4::scale(0.42f, 0.075f, 0.055f),
             mat.primary, mat.roughness, mat.metallic, mat.emissive, MatId::Standard, false);
    drawMesh(vk, cubeMesh_, basis * Mat4::translation(0.14f, 0.025f, 0.0f) * Mat4::scale(0.32f, 0.032f, 0.032f),
             mat.secondary, mat.roughness, mat.metallic, 0.0f, MatId::Standard, false);
    if (world.player().muzzleFlashTimer > 0.01f) {
        drawMesh(vk, sphereMesh_, basis * Mat4::translation(0.34f, 0.02f, 0.0f) * Mat4::scale(0.16f, 0.16f, 0.16f),
                 {255, 224, 110, 255}, 0.1f, 0.0f, 3.2f, MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
    }
}

void Renderer3D::renderSceneAndBeginPost(VulkanContext& vk, const GameWorld& world,
                                         const CameraState& camera,
                                         bool firstPerson, float adsBlend, bool lookBack) {
    if (!initialized_) return;

    const Vec2 focus = world.player().pos;
    const Vec3 lightDir = computeSunDirection(world);
    const bool shadowsOn = (quality_ != GraphicsQuality::Performance);

    // Light ortho camera snapped to shadow texel grid around focus (zero shadow swimming!)
    const float shadowSpan = 135.0f;
    const float texelWorld = (shadowSpan * 2.0f) / static_cast<float>(VulkanContext::kShadowMapSize);
    const float snapX = std::floor(focus.x / texelWorld) * texelWorld;
    const float snapZ = std::floor(focus.z / texelWorld) * texelWorld;
    const Vec3 shadowTarget{snapX, world.city().surfaceHeightAt(focus), snapZ};
    const Vec3 shadowEye = shadowTarget - lightDir * 220.0f;
    const Mat4 lightView = Mat4::lookAt(shadowEye, shadowTarget, {0, 1, 0});
    const Mat4 lightProj = Mat4::orthoVulkan(-shadowSpan, shadowSpan, -shadowSpan, shadowSpan, 10.0f, 480.0f);
    const Mat4 lightVP = lightProj * lightView;

    const SceneUBO ubo = buildSceneUBO(world, camera, vk.width(), vk.height(), lightDir, lightVP, shadowsOn);
    vk.updateSceneUBO(ubo);

    const bool nightMode = world.isNight();

    auto renderWorldPass = [&](bool shadowPass) {
        drawSeoulTerrainAndRoads(vk, world, focus, shadowPass);
        drawBuildingsAndParks(vk, world, focus, shadowPass);
        drawTreesAndProps(vk, world, focus, camera.position, shadowPass);

        // Draw all CAD-precision Vehicles!
        const float vehCull = shadowPass ? 135.0f : 360.0f;
        for (const auto& v : world.vehicles()) {
            if ((v.pos - focus).lengthSq() > vehCull * vehCull) continue;
            const float gy = world.city().surfaceHeightAt(v.pos);
            const bool hoodOnly = (!shadowPass && firstPerson && !lookBack && v.id == world.player().drivingVehicleId);
            drawVehicle(vk, v, gy, nightMode, shadowPass, false, hoodOnly);
        }

        // Draw Player (unless 1st-person on foot or driving)
        const auto& pl = world.player();
        if (pl.drivingVehicleId < 0 && (!firstPerson || shadowPass)) {
            const float gy = world.city().surfaceHeightAt(pl.pos) + pl.posY;
            const ColorRGBA top = getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx);
            const ColorRGBA bot = getPlayerBottomColor(pl.currentOutfit, pl.characterCustom.customBottomColorIdx);
            const ColorRGBA skin = getPlayerSkinColor(pl.characterCustom.skinToneIdx);
            const auto& wepSlot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
            const bool raisingWeapon = (adsBlend > 0.04f || pl.muzzleFlashTimer > 0.01f);
            drawCharacter(vk, pl.pos, gy, pl.facingYaw, pl.walkAnimPhase, top, bot, skin,
                          !pl.wasted, raisingWeapon, pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                          pl.currentOutfit, shadowPass, !pl.onGround, false, false, false,
                          &pl.characterCustom, &wepSlot, nullptr, nightMode);
        }

        // Draw Living Seoul Citizens, Gangsters & Police Officers!
        const float npcCull = shadowPass ? 90.0f : 185.0f;
        for (const auto& npc : world.npcs()) {
            if ((npc.pos - focus).lengthSq() > npcCull * npcCull) continue;
            const float gy = world.city().surfaceHeightAt(npc.pos);
            const bool npcCombat = (npc.role != NpcRole::Civilian) &&
                                   (npc.behavior == NpcBehavior::CombatEngage || npc.muzzleFlashTimer > 0.01f);
            drawCharacter(vk, npc.pos, gy, npc.facingYaw, npc.animPhase,
                          npc.outfitTop, npc.outfitBottom, npc.skinColor,
                          npc.alive, npcCombat, npc.weapon,
                          npc.muzzleFlashTimer, 0.0f, npc.outfitStyle, shadowPass,
                          false, (npc.behavior == NpcBehavior::PanicFlee),
                          (npc.role == NpcRole::Police), npc.isSwat,
                          nullptr, nullptr, &npc, nightMode);
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

    // Translucent Glass, Rainbow Fountain, Rain, Particles, Tracers & POI Rings
    drawAlphaAndEffects(vk, world, camera, focus);

    // 1st-Person Weapon Viewmodel
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
    pp.params1[1] = (quality_ == GraphicsQuality::Ultra) ? 1.0f : 0.0f;
    vk.beginSwapchainPostAndUiPass(pp);
}

} // namespace oc
