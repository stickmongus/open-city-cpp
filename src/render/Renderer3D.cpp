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

    vk.destroyMesh(hanRiverWaterMesh_);
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
    // Multi-lobe Ginkgo / Cherry Blossom Tree Canopy
    {
        MeshBuilder3D mb;
        mb.addSphere({0.0f, 0.25f, 0.0f}, 0.72f, 7, 11, {245, 248, 240, 255});
        mb.addSphere({0.42f, 0.05f, 0.24f}, 0.54f, 6, 10, {228, 236, 224, 255});
        mb.addSphere({-0.44f, 0.08f, -0.22f}, 0.52f, 6, 10, {236, 244, 232, 255});
        mb.addSphere({-0.16f, 0.10f, 0.44f}, 0.48f, 6, 10, {220, 232, 218, 255});
        mb.addSphere({0.18f, 0.48f, -0.28f}, 0.46f, 6, 10, {255, 255, 248, 255});
        foliageMesh_ = mb.upload(vk, 65.0f);
    }
    // Layered Korean Mountain Pine Tree
    {
        MeshBuilder3D mb;
        mb.addCylinderY({0, 0.00f, 0}, 0.54f, 0.16f, 0.55f, 10, {218, 228, 218, 255});
        mb.addCylinderY({0, 0.34f, 0}, 0.44f, 0.12f, 0.52f, 10, {234, 242, 232, 255});
        mb.addCylinderY({0, 0.66f, 0}, 0.32f, 0.02f, 0.48f, 10, {248, 255, 246, 255});
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
    // 1. REAL S-CURVE HAN RIVER WATER + SEOKCHON LAKE + CHEONGGYECHEON (30 km!)
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

            // North half & South half of river ribbon
            waterMb.addQuad({x0, waterY, cz0 - hw0}, {x1, waterY, cz1 - hw1},
                            {x1, waterY, cz1},       {x0, waterY, cz0}, kHanWater);
            waterMb.addQuad({x0, waterY, cz0},       {x1, waterY, cz1},
                            {x1, waterY, cz1 + hw1}, {x0, waterY, cz0 + hw0}, kHanWater);
        }
        // Seokchon Lake + Cheonggyecheon urban stream
        for (const auto& lake : city.lakes()) {
            waterMb.addQuad({lake.bounds.min.x, lake.waterY, lake.bounds.min.z},
                            {lake.bounds.max.x, lake.waterY, lake.bounds.min.z},
                            {lake.bounds.max.x, lake.waterY, lake.bounds.max.z},
                            {lake.bounds.min.x, lake.waterY, lake.bounds.max.z},
                            {32, 92, 138, 255});
        }
        hanRiverWaterMesh_ = waterMb.upload(vk, 30.0f);
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
                    // Exposed granite rock outcrops near peak of Bukhansan/Inwangsan/Gwanaksan!
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

                // Top asphalt driving surface (matId=1 draws Korean yellow double centerline + lanes!)
                deckMb.addQuad({xL, y0, z0}, {xR, y0, z0}, {xR, y1, z1}, {xL, y1, z1}, kAsphalt);

                // Concrete girder fascia & side safety parapet barriers
                const float thick = 1.65f;
                concMb.addQuad({xL, y0 - thick, z0}, {xL, y0, z0}, {xL, y1, z1}, {xL, y1 - thick, z1}, kConcrete);
                concMb.addQuad({xR, y0, z0}, {xR, y0 - thick, z0}, {xR, y1 - thick, z1}, {xR, y1, z1}, kConcrete);
                // Side parapets (1.05m tall)
                for (float s : {-1.0f, 1.0f}) {
                    const float px = br.centerX + s * (br.halfWidth + 0.35f);
                    concMb.addQuad({px, y0, z0}, {px, y0 + 1.05f, z0}, {px, y1 + 1.05f, z1}, {px, y1, z1}, kConcrete);
                }
            }

            // River concrete piers plunging cleanly into the Han River every ~75m!
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
                // Cross pier cap beam tucked cleanly under bridge girder!
                concMb.addBox({br.centerX, py - 2.2f, pz}, {br.halfWidth * 1.95f, 1.3f, 3.6f}, kConcrete);
                // Bridge light poles on outer curb
                for (float s : {-1.0f, 1.0f}) {
                    steelMb.addBox({br.centerX + s * (br.halfWidth + 0.4f), py + 4.4f, pz},
                                   {0.24f, 8.6f, 0.24f}, kSteelSilver);
                }
            }

            const float midZ = 0.5f * (br.zNorth + br.zSouth);
            const float midY = br.deckHeight;

            // Cable-stayed bridge monumental central A-pylon + fanned stay cables (Olympic / Seongsu!)
            if (br.isCableStayed) {
                const float pylonH = 74.0f;
                steelMb.addCylinderY({br.centerX, midY - 1.5f, midZ}, 3.6f, 1.4f, pylonH, 8, br.accentColor);
                // Torch crown (Olympic Bridge iconic flame monument!)
                steelMb.addCylinderY({br.centerX, midY + pylonH - 2.0f, midZ}, 1.8f, 3.4f, 11.0f, 8, {236, 198, 72, 255});
                for (int c = 1; c <= 12; ++c) {
                    const float frac = static_cast<float>(c) / 12.0f;
                    const float cableZOff = 24.0f + frac * 175.0f;
                    const float topY = midY + pylonH * (0.45f + 0.50f * frac);
                    for (float dirZ : {-1.0f, 1.0f}) {
                        const Vec3 a{br.centerX, topY, midZ};
                        const Vec3 bPt{br.centerX, midY + 0.6f, midZ + dirZ * cableZOff};
                        const Vec3 dir = (bPt - a).normalized();
                        const Vec3 side{1.0f, 0.0f, 0.0f};
                        const Vec3 up = {0.0f, 1.0f, 0.0f};
                        const Vec3 mid = (a + bPt) * 0.5f;
                        const float len = (bPt - a).length();
                        steelMb.addOrientedBox(mid, {0.22f, 0.22f, len * 0.5f}, side, up, dir, kSteelSilver);
                    }
                }
            }

            // Steel Arch Bridge (Seogang / Hangang iconic red-orange arches!)
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
                        // Vertical hanger cables
                        if (k > 0 && k < archSegs && (k % 2 == 0)) {
                            steelMb.addBox({ax, 0.5f * (midY + y0), z0}, {0.26f, y0 - midY, 0.26f}, kSteelSilver);
                        }
                    }
                }
            }

            // Banpo Bridge Moonlight Rainbow Fountain (반포대교 달빛무지개분수!)
            if (br.hasRainbowFountain) {
                // Lower Jamsu submerged deck beneath Banpo Bridge
                concMb.addBox({br.centerX, 2.2f, midZ}, {br.halfWidth * 1.75f, 1.1f, spanLen * 0.90f}, kPierDark);
                // Translucent colorful arcing water curtains pouring off BOTH flanks into Han River!
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
    // 4. ALL 12 ICONIC REAL SEOUL MACRO LANDMARKS (Always visible across 30km!)
    // ------------------------------------------------------------------------
    {
        MeshBuilder3D glassMb, stoneMb, metalMb, giwaMb, glowMb;

        auto addPalaceGiwaRoof = [&](float cx, float yBase, float cz, float spanX, float spanZ, float roofH) {
            const ColorRGBA kRoof{56, 62, 70, 255};
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

            switch (lm.kind) {
                case SeoulLandmarkKind::LotteWorldTower: {
                    // 555m Lotte World Tower (롯데월드타워): convex tapering silver-blue glass spire with iconic split lantern top!
                    const int tiers = 14;
                    const float totalH = 555.0f;
                    const ColorRGBA kLotteGlass{162, 198, 226, 255};
                    const ColorRGBA kLotteFin{212, 224, 236, 255};
                    for (int t = 0; t < tiers; ++t) {
                        const float f0 = static_cast<float>(t) / static_cast<float>(tiers);
                        const float f1 = static_cast<float>(t + 1) / static_cast<float>(tiers);
                        const float y0 = baseY + f0 * 505.0f;
                        const float y1 = baseY + f1 * 505.0f;
                        const float w0 = lerpf(34.0f, 11.5f, std::pow(f0, 0.88f));
                        const float w1 = lerpf(34.0f, 11.5f, std::pow(f1, 0.88f));
                        // 8-sided chamfered square profile (authentic Lotte World Tower silhouette!)
                        for (int s = 0; s < 8; ++s) {
                            const float a0 = kTwoPi * (static_cast<float>(s) + 0.5f) / 8.0f;
                            const float a1 = kTwoPi * (static_cast<float>(s + 1) + 0.5f) / 8.0f;
                            const Vec3 p00{lx + std::cos(a0) * w0, y0, lz + std::sin(a0) * w0};
                            const Vec3 p10{lx + std::cos(a1) * w0, y0, lz + std::sin(a1) * w0};
                            const Vec3 p11{lx + std::cos(a1) * w1, y1, lz + std::sin(a1) * w1};
                            const Vec3 p01{lx + std::cos(a0) * w1, y1, lz + std::sin(a0) * w1};
                            glassMb.addQuad(p00, p01, p11, p10, kLotteGlass);
                        }
                    }
                    // Iconic notched split-lantern crown (505m -> 555m!)
                    for (float side : {-1.0f, 1.0f}) {
                        metalMb.addBox({lx + side * 6.5f, baseY + 528.0f, lz}, {5.2f, 48.0f, 16.0f}, kLotteFin);
                    }
                    // Vertical glowing silver-blue architectural crown fins
                    glowMb.addBox({lx, baseY + 552.0f, lz}, {16.0f, 5.5f, 16.0f}, {195, 228, 255, 255});
                    // Lotte World Mall podium base
                    stoneMb.addBox({lx - 58.0f, baseY + 18.0f, lz + 12.0f}, {82.0f, 36.0f, 76.0f}, {222, 224, 228, 255});
                    (void)totalH;
                    break;
                }

                case SeoulLandmarkKind::NSeoulTower: {
                    // N Seoul Tower atop Namsan Peak (base ~240m + 236m tower!)
                    // Stone Namsan Plaza base & octagonal pavilion
                    stoneMb.addCylinderY({lx, baseY - 6.0f, lz}, 22.0f, 20.0f, 16.0f, 8, {178, 174, 168, 255});
                    // Tall slender white concrete shaft (142m!)
                    stoneMb.addCylinderY({lx, baseY + 10.0f, lz}, 6.2f, 5.4f, 138.0f, 16, {236, 238, 242, 255});
                    // Iconic multi-tiered glowing observation pod (Digital Observatory + N.Grill!)
                    glassMb.addCylinderY({lx, baseY + 148.0f, lz}, 12.5f, 16.5f, 11.0f, 16, {74, 132, 188, 255});
                    metalMb.addCylinderY({lx, baseY + 159.0f, lz}, 17.5f, 17.5f, 4.0f, 16, {220, 225, 232, 255});
                    glassMb.addCylinderY({lx, baseY + 163.0f, lz}, 16.0f, 14.5f, 12.0f, 16, {82, 142, 198, 255});
                    metalMb.addCylinderY({lx, baseY + 175.0f, lz}, 14.8f, 9.5f, 6.5f, 16, {214, 218, 226, 255});
                    // Famous glowing Namsan LED beacon ring (Seoul clear air blue-cyan glow!)
                    glowMb.addCylinderY({lx, baseY + 145.0f, lz}, 14.0f, 14.0f, 3.5f, 16, {86, 212, 255, 255});
                    glowMb.addCylinderY({lx, baseY + 181.0f, lz}, 9.8f, 9.8f, 3.0f, 16, {120, 224, 255, 255});
                    // Tall steel broadcast antenna spire (+76m) + red aviation beacon
                    metalMb.addCylinderY({lx, baseY + 182.0f, lz}, 3.2f, 0.55f, 76.0f, 12, {226, 230, 236, 255});
                    glowMb.addSphere({lx, baseY + 259.0f, lz}, 2.4f, 8, 12, {255, 58, 52, 255});
                    break;
                }

                case SeoulLandmarkKind::GyeongbokgungPalace: {
                    // Gyeongbokgung Geunjeongjeon Throne Hall + Gwanghwamun Gate with sweeping upturned Giwa roofs!
                    stoneMb.addBox({lx, baseY + 2.0f, lz}, {68.0f, 4.0f, 52.0f}, {206, 202, 194, 255});
                    stoneMb.addBox({lx, baseY + 5.2f, lz}, {56.0f, 2.8f, 42.0f}, {214, 210, 202, 255});
                    // Crimson timber colonnade & palace hall body
                    stoneMb.addBox({lx, baseY + 11.5f, lz}, {40.0f, 10.5f, 28.0f}, {162, 46, 38, 255});
                    // Turquoise Dancheong under-eave bracket band
                    stoneMb.addBox({lx, baseY + 16.8f, lz}, {42.5f, 1.8f, 30.5f}, {44, 136, 118, 255});
                    // Lower sweeping curved Giwa Korean tile roof
                    addPalaceGiwaRoof(lx, baseY + 17.6f, lz, 48.0f, 36.0f, 6.2f);
                    // Upper throne tier + upper curved Giwa roof!
                    stoneMb.addBox({lx, baseY + 24.5f, lz}, {30.0f, 5.8f, 20.0f}, {168, 48, 40, 255});
                    stoneMb.addBox({lx, baseY + 27.6f, lz}, {32.0f, 1.4f, 22.0f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 28.3f, lz, 38.0f, 27.0f, 8.2f);

                    // Gwanghwamun Gate 142m south of Geunjeongjeon!
                    const float gz = lz + 142.0f;
                    stoneMb.addBox({lx - 18.0f, baseY + 4.5f, gz}, {18.0f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx + 18.0f, baseY + 4.5f, gz}, {18.0f, 9.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx, baseY + 8.5f, gz}, {54.0f, 3.0f, 14.0f}, {204, 198, 188, 255});
                    stoneMb.addBox({lx, baseY + 13.2f, gz}, {34.0f, 6.8f, 12.0f}, {166, 46, 38, 255});
                    stoneMb.addBox({lx, baseY + 16.6f, gz}, {36.0f, 1.3f, 13.5f}, {44, 136, 118, 255});
                    addPalaceGiwaRoof(lx, baseY + 17.2f, gz, 44.0f, 19.0f, 6.4f);
                    break;
                }

                case SeoulLandmarkKind::Building63: {
                    // Iconic Yeouido 63 Building (249m golden-bronze tapered wedge!)
                    const ColorRGBA kGoldGlass{224, 178, 76, 255};
                    const int segs = 10;
                    for (int s = 0; s < segs; ++s) {
                        const float f0 = static_cast<float>(s) / static_cast<float>(segs);
                        const float f1 = static_cast<float>(s + 1) / static_cast<float>(segs);
                        const float y0 = baseY + f0 * 249.0f;
                        const float y1 = baseY + f1 * 249.0f;
                        const float hx0 = lerpf(24.0f, 16.0f, f0);
                        const float hx1 = lerpf(24.0f, 16.0f, f1);
                        const float hz0 = lerpf(16.0f, 6.2f, f0);
                        const float hz1 = lerpf(16.0f, 6.2f, f1);
                        glassMb.addBox({lx, 0.5f * (y0 + y1), lz}, {hx0 + hx1, (y1 - y0) + 0.5f, hz0 + hz1}, kGoldGlass);
                    }
                    glowMb.addBox({lx, baseY + 250.0f, lz}, {30.0f, 3.5f, 11.0f}, {255, 224, 128, 255});
                    break;
                }

                case SeoulLandmarkKind::NationalAssembly: {
                    // National Assembly of South Korea (국회의사당): classical colonnade + ribbed mint-copper dome!
                    stoneMb.addBox({lx, baseY + 14.0f, lz}, {78.0f, 28.0f, 54.0f}, {214, 212, 206, 255});
                    // 24 vertical perimeter classical granite columns
                    for (int c = -5; c <= 5; ++c) {
                        stoneMb.addBox({lx + static_cast<float>(c) * 6.8f, baseY + 14.5f, lz + 28.0f},
                                       {2.2f, 27.0f, 2.2f}, {228, 226, 220, 255});
                        stoneMb.addBox({lx + static_cast<float>(c) * 6.8f, baseY + 14.5f, lz - 28.0f},
                                       {2.2f, 27.0f, 2.2f}, {228, 226, 220, 255});
                    }
                    // Iconic ribbed mint-green oxidized copper dome!
                    metalMb.addCylinderY({lx, baseY + 28.0f, lz}, 24.0f, 22.0f, 8.0f, 16, {196, 194, 188, 255});
                    metalMb.addSphere({lx, baseY + 36.0f, lz}, 22.5f, 8, 16, {74, 152, 134, 255});
                    break;
                }

                case SeoulLandmarkKind::Parc1RedTower: {
                    // Yeouido Parc1 Towers (333m + 256m) with signature vivid crimson diagonal exoskeleton!
                    glassMb.addBox({lx - 26.0f, baseY + 166.0f, lz}, {34.0f, 332.0f, 34.0f}, {132, 164, 192, 255});
                    glassMb.addBox({lx + 34.0f, baseY + 128.0f, lz + 10.0f}, {32.0f, 256.0f, 32.0f}, {136, 168, 196, 255});
                    const ColorRGBA kParcRed{208, 36, 44, 255};
                    for (float sx : {-1.0f, 1.0f}) {
                        for (float sz : {-1.0f, 1.0f}) {
                            metalMb.addBox({lx - 26.0f + sx * 17.5f, baseY + 166.0f, lz + sz * 17.5f},
                                           {2.8f, 334.0f, 2.8f}, kParcRed);
                            metalMb.addBox({lx + 34.0f + sx * 16.5f, baseY + 128.0f, lz + 10.0f + sz * 16.5f},
                                           {2.6f, 258.0f, 2.6f}, kParcRed);
                        }
                    }
                    // Horizontal crimson structural belt trusses
                    for (int h = 1; h <= 5; ++h) {
                        metalMb.addBox({lx - 26.0f, baseY + static_cast<float>(h) * 58.0f, lz}, {37.0f, 3.2f, 37.0f}, kParcRed);
                    }
                    break;
                }

                case SeoulLandmarkKind::CoexTradeTower: {
                    // Samseong-dong COEX Trade Tower (228m iconic stepped chevron glass tower!)
                    glassMb.addBox({lx, baseY + 100.0f, lz}, {36.0f, 200.0f, 32.0f}, {152, 186, 214, 255});
                    glassMb.addBox({lx, baseY + 214.0f, lz}, {24.0f, 28.0f, 24.0f}, {168, 200, 226, 255});
                    stoneMb.addBox({lx + 54.0f, baseY + 16.0f, lz}, {68.0f, 32.0f, 64.0f}, {198, 204, 212, 255});
                    glowMb.addBox({lx, baseY + 229.0f, lz}, {20.0f, 3.0f, 20.0f}, {122, 212, 255, 255});
                    break;
                }

                case SeoulLandmarkKind::SeoulCityHall: {
                    // Seoul City Hall: sweeping curved wave-glass modern hall + historic stone library front
                    glassMb.addBox({lx, baseY + 22.0f, lz - 14.0f}, {62.0f, 44.0f, 26.0f}, {126, 178, 218, 255});
                    glassMb.addBox({lx, baseY + 46.0f, lz - 4.0f},  {64.0f, 10.0f, 36.0f}, {142, 194, 232, 255});
                    stoneMb.addBox({lx, baseY + 11.0f, lz + 18.0f}, {48.0f, 22.0f, 18.0f}, {204, 196, 184, 255});
                    break;
                }

                case SeoulLandmarkKind::SungnyemunGate: {
                    // Sungnyemun (Namdaemun): granite stone fortress arch + 2-story curved Giwa gatehouse
                    stoneMb.addBox({lx - 11.0f, baseY + 4.5f, lz}, {12.0f, 9.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx + 11.0f, baseY + 4.5f, lz}, {12.0f, 9.0f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx, baseY + 8.5f, lz}, {34.0f, 2.8f, 14.0f}, {188, 182, 174, 255});
                    stoneMb.addBox({lx, baseY + 13.0f, lz}, {24.0f, 6.2f, 11.0f}, {162, 46, 38, 255});
                    addPalaceGiwaRoof(lx, baseY + 16.0f, lz, 31.0f, 16.0f, 4.8f);
                    stoneMb.addBox({lx, baseY + 22.0f, lz}, {18.0f, 4.2f, 9.0f}, {162, 46, 38, 255});
                    addPalaceGiwaRoof(lx, baseY + 24.0f, lz, 25.0f, 13.0f, 5.2f);
                    break;
                }

                case SeoulLandmarkKind::DongdaemunDDP: {
                    // Dongdaemun Design Plaza (DDP): sleek silver matte-aluminum curved sculptural pavilion!
                    metalMb.addCylinderY({lx - 18.0f, baseY, lz}, 32.0f, 28.0f, 24.0f, 16, {212, 218, 226, 255});
                    metalMb.addCylinderY({lx + 24.0f, baseY, lz + 10.0f}, 26.0f, 22.0f, 20.0f, 16, {218, 224, 232, 255});
                    glowMb.addCylinderY({lx - 18.0f, baseY + 24.2f, lz}, 24.0f, 24.0f, 1.4f, 16, {188, 228, 255, 255});
                    break;
                }

                case SeoulLandmarkKind::JamsilStadium: {
                    // Jamsil Olympic Stadium: monumental sweeping curved stadium bowl ring!
                    const int segs = 24;
                    for (int s = 0; s < segs; ++s) {
                        const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(segs);
                        const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(segs);
                        const float am = 0.5f * (a0 + a1);
                        const float h = 34.0f + 12.0f * std::abs(std::cos(am));
                        const float r = 82.0f;
                        stoneMb.addBox({lx + std::cos(am) * r, baseY + h * 0.5f, lz + std::sin(am) * r},
                                       {24.0f, h, 24.0f}, {208, 212, 218, 255});
                    }
                    break;
                }

                case SeoulLandmarkKind::GangnamStationPlaza: {
                    // Gangnam Station twin modern glass media towers!
                    glassMb.addBox({lx - 42.0f, baseY + 72.0f, lz - 36.0f}, {32.0f, 144.0f, 32.0f}, {122, 168, 214, 255});
                    glassMb.addBox({lx + 42.0f, baseY + 86.0f, lz + 36.0f}, {34.0f, 172.0f, 34.0f}, {114, 158, 206, 255});
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
        const float elev = clampf(std::sin(sunAngle), 0.14f, 0.94f);
        const float horiz = std::sqrt(std::max(0.01f, 1.0f - elev * elev));
        const float az = -0.72f + (h - 12.0f) * 0.22f;
        // Direction FROM sun TOWARD scene (pointing down into ground!)
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
    Vec3 sunDay{1.15f, 1.08f, 0.98f};
    Vec3 sunSet{1.35f, 0.68f, 0.34f};
    Vec3 sunNight{0.22f, 0.30f, 0.48f};
    Vec3 sunCol = Vec3{
        lerpf(sunNight.x, lerpf(sunDay.x, sunSet.x, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(sunNight.y, lerpf(sunDay.y, sunSet.y, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(sunNight.z, lerpf(sunDay.z, sunSet.z, clampf(sunset, 0.0f, 1.0f)), dayF)
    } * (1.0f - wet * 0.42f);

    Vec3 skyAmb{
        lerpf(0.06f, lerpf(0.32f, 0.46f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.09f, lerpf(0.46f, 0.34f, clampf(sunset, 0.0f, 1.0f)), dayF),
        lerpf(0.16f, lerpf(0.72f, 0.36f, clampf(sunset, 0.0f, 1.0f)), dayF)
    };
    Vec3 gndAmb{
        lerpf(0.04f, 0.16f, dayF),
        lerpf(0.05f, 0.17f, dayF),
        lerpf(0.07f, 0.19f, dayF)
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
        // 1. Vast distant Seoul urban basin ground north & south of Han River (keeps 30km x 25km skyline grounded!)
        drawBox(vk, {0.0f, -0.45f, -7200.0f}, {32000.0f, 0.5f, 11800.0f}, 0.0f,
                {116, 120, 126, 255}, 0.88f, 0.0f, 0.0f, MatId::SidewalkPavers, false);
        drawBox(vk, {0.0f, -0.45f,  7200.0f}, {32000.0f, 0.5f, 11800.0f}, 0.0f,
                {116, 120, 126, 255}, 0.88f, 0.0f, 0.0f, MatId::SidewalkPavers, false);

        // 2. Shimmering 30km Han River S-curve + Seokchon Lake + Cheonggyecheon Water!
        drawMesh(vk, hanRiverWaterMesh_, Mat4::identity(), {255, 255, 255, 255},
                 0.06f, 0.06f, 0.0f, MatId::HanRiverWater, false);

        // 3. 7 Real Seoul Mountains (Namsan, Bukhansan, Inwangsan, Bugaksan, Gwanaksan, Achasan, Cheonggyesan)
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
    // Automatically skips cells submerged in the Han River while preserving Yeouido Island!
    const int centerRx = city.nearestRoadIndex(focus.x);
    const int centerRz = city.nearestRoadIndex(focus.z);
    const int radiusBlocks = 7;

    for (int iz = centerRz - radiusBlocks; iz <= centerRz + radiusBlocks; ++iz) {
        const float rz = city.roadCoord(iz);
        for (int ix = centerRx - radiusBlocks; ix <= centerRx + radiusBlocks; ++ix) {
            const float rx = city.roadCoord(ix);
            const Vec2 cellCenter{rx + pitch * 0.5f, rz + pitch * 0.5f};

            // Draw road tile centered on (rx, rz) if dry ground & arm endpoints stay out of Han River!
            const Vec2 roadCenter{rx, rz};
            if (!city.isInHanRiverWater(roadCenter, -12.0f) && !city.isInSeokchonLake(roadCenter)) {
                const float elev = city.terrainElevationAt(roadCenter);
                if (elev < 28.0f) {
                    const bool horizDry = !city.isInHanRiverWater({rx - pitch * 0.48f, rz}, -4.0f) &&
                                          !city.isInHanRiverWater({rx + pitch * 0.48f, rz}, -4.0f);
                    const bool vertDry  = !city.isInHanRiverWater({rx, rz - pitch * 0.48f}, -4.0f) &&
                                          !city.isInHanRiverWater({rx, rz + pitch * 0.48f}, -4.0f);
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

            // Draw raised Korean sidewalk block pad inside (ix, iz) block if not in Han River!
            if (!city.isInHanRiverWater(cellCenter, 8.0f) && !city.isInSeokchonLake(cellCenter)) {
                const float bElev = city.terrainElevationAt(cellCenter);
                if (bElev < 26.0f) {
                    const float innerSpan = pitch - CityMap::kRoadWidth; // 66m block pad
                    const bool isRiverPark = city.isInHanRiverPark(cellCenter);
                    const ColorRGBA padCol = isRiverPark
                        ? ColorRGBA{66, 108, 56, 255}
                        : ColorRGBA{164, 166, 170, 255};
                    const MatId padMat = isRiverPark ? MatId::FoliageGrass : MatId::SidewalkPavers;
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

    // 1. Always-visible 12 Real Seoul Macro Landmarks (Lotte World Tower 555m, N Seoul Tower,
    //    Gyeongbokgung Palace, 63 Building, National Assembly, Parc1, COEX, City Hall, DDP, etc.)!
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
            drawBox(vk, {c.x, park.baseY + 0.14f, c.z}, {sz.x, 0.08f, sz.z}, 0.0f,
                    park.grassColor, 0.88f, 0.0f, 0.0f, MatId::FoliageGrass, false);
        }
    }

    // 3. Streamed Seoul Buildings (Korean Apartments, Glass Skyscrapers, Brick Shops, Hanok, Villas!)
    const float queryRadius = shadowPass ? 165.0f : 580.0f;
    visIdxBuf_.clear();
    city.queryBuildingsInRadius(focus, queryRadius, visIdxBuf_);

    for (int bi : visIdxBuf_) {
        const Building& b = city.buildings()[static_cast<std::size_t>(bi)];
        if (b.isMacroLandmark) continue; // Already drawn by crisp macro landmark geometry above!

        const Vec2 c = b.footprint.center();
        const Vec2 sz = b.footprint.size();
        const float distSq = (c - focus).lengthSq();

        MatId facadeMat = MatId::ModernConcrete;
        float rough = 0.68f, metal = 0.10f;
        switch (b.style) {
            case BuildingStyle::GlassTower:
            case BuildingStyle::SupertallSpire:
            case BuildingStyle::DealershipBuilding:
                facadeMat = MatId::GlassSkyscraper;
                rough = 0.14f; metal = 0.38f;
                break;
            case BuildingStyle::KoreanApartment:
            case BuildingStyle::BrickCommercial:
            case BuildingStyle::ShopBuilding:
            case BuildingStyle::BoutiqueBuilding:
            case BuildingStyle::VillaHouse:
                facadeMat = MatId::KoreanApartmentBrick;
                rough = 0.74f; metal = 0.05f;
                break;
            case BuildingStyle::HanokPalace:
                facadeMat = MatId::ModernConcrete;
                rough = 0.78f; metal = 0.04f;
                break;
            default:
                facadeMat = MatId::ModernConcrete;
                rough = 0.66f; metal = 0.14f;
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

        // Authentic Korean Hanok curved Giwa tiled roof!
        if (b.style == BuildingStyle::HanokPalace) {
            Mat4 rm = Mat4::translation(c.x, roofY - 0.1f, c.z) *
                      Mat4::scale(sz.x * 1.15f, std::min(5.2f, b.height * 0.45f), sz.z * 1.15f);
            drawMesh(vk, giwaRoofMesh_, rm, {255, 255, 255, 255}, 0.56f, 0.08f, 0.0f,
                     MatId::HanokGiwaRoof, shadowPass);
        } else if (!shadowPass && distSq < 260.0f * 260.0f) {
            // Rooftop parapet trim + elevator/HVAC penthouse box (signature Seoul apartment & office detail!)
            drawBox(vk, {c.x, roofY + 0.35f, c.z}, {sz.x * 0.99f, 0.72f, sz.z * 0.99f}, 0.0f,
                    b.accentColor, 0.74f, 0.12f, 0.0f, MatId::ModernConcrete, false);
            if (b.style == BuildingStyle::KoreanApartment) {
                // Iconic Korean apartment rooftop elevator core + painted stripe crown!
                drawBox(vk, {c.x, roofY + 1.85f, c.z}, {sz.x * 0.28f, 3.1f, sz.z * 0.36f}, 0.0f,
                        b.accentColor, 0.72f, 0.06f, 0.0f, MatId::ModernConcrete, false);
            } else if (b.hasHelipad) {
                drawBox(vk, {c.x, roofY + 0.76f, c.z}, {sz.x * 0.46f, 0.14f, sz.z * 0.46f}, 0.0f,
                        {58, 62, 70, 255}, 0.80f, 0.05f, 0.0f, MatId::Standard, false);
                drawBox(vk, {c.x, roofY + 0.85f, c.z}, {sz.x * 0.24f, 0.04f, 0.75f}, 0.0f,
                        {242, 206, 56, 255}, 0.40f, 0.0f, nightMode ? 0.6f : 0.0f, MatId::EmissiveGlow, false);
            }
            // Storefront awning / illuminated Korean shop signage band on lower floors
            if (b.hasStorefront) {
                drawBox(vk, {c.x, b.baseY + 3.85f, c.z}, {sz.x + 0.7f, 0.55f, sz.z + 0.7f}, 0.0f,
                        b.awningColor, 0.65f, 0.05f, nightMode ? 0.55f : 0.05f, MatId::Standard, false);
            }
            // Nightclub / Dealership glowing neon architectural trim
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

    // 1. Seoul Street Trees (Ginkgo, Cherry Blossom, Korean Pine!)
    visIdxBuf_.clear();
    city.queryTreesInRadius(focus, treeRadius, visIdxBuf_);
    for (int ti : visIdxBuf_) {
        const TreeProp& t = city.trees()[static_cast<std::size_t>(ti)];
        const float y0 = t.baseY;

        // Planter curb box if roadside street tree
        if (t.inPlanter && !shadowPass) {
            drawBox(vk, {t.pos.x, y0 + 0.18f, t.pos.z}, {1.65f, 0.36f, 1.65f}, 0.0f,
                    {136, 138, 144, 255}, 0.84f, 0.0f, 0.0f, MatId::SidewalkPavers, false);
        }
        // Tapered bark trunk
        Mat4 trunkM = Mat4::translation(t.pos.x, y0, t.pos.z) *
                      Mat4::scale(0.42f, t.trunkHeight, 0.42f);
        drawMesh(vk, cylMesh_, trunkM, {94, 70, 52, 255}, 0.90f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);

        // Species-specific canopy
        if (t.species == TreeSpecies::KoreanPine) {
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight * 0.65f, t.pos.z) *
                      Mat4::scale(t.crownRadius * 1.4f, t.crownRadius * 2.1f, t.crownRadius * 1.4f);
            drawMesh(vk, pineMesh_, cm, {42, 86, 52, 255}, 0.88f, 0.0f, 0.0f, MatId::FoliageGrass, shadowPass);
        } else {
            const ColorRGBA crownCol = (t.species == TreeSpecies::CherryBlossom)
                ? ColorRGBA{242, 186, 206, 255} // Iconic Yeouido & Seokchon Cherry Blossoms!
                : ColorRGBA{84, 128, 52, 255};  // Seoul Ginkgo / Zelkova urban canopy
            Mat4 cm = Mat4::translation(t.pos.x, y0 + t.trunkHeight + t.crownRadius * 0.25f, t.pos.z) *
                      Mat4::scale(t.crownRadius, t.crownRadius * 0.92f, t.crownRadius);
            drawMesh(vk, foliageMesh_, cm, crownCol, 0.86f, 0.0f, 0.0f, MatId::FoliageGrass, shadowPass);
        }
    }

    // 2. Street Props (Seoul streetlamps, bus shelters, benches, hydrants, Han River sunshades!)
    const float propRadius = shadowPass ? 95.0f : 210.0f;
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
                drawMesh(vk, cylMesh_, baseM * Mat4::scale(0.18f, 6.8f, 0.18f),
                         {78, 84, 94, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0.85f, 6.7f, 0.0f) * Mat4::scale(1.8f, 0.14f, 0.22f),
                         {82, 88, 98, 255}, 0.38f, 0.72f, 0.0f, MatId::Standard, shadowPass);
                if (!shadowPass) {
                    drawMesh(vk, cubeMesh_, baseM * Mat4::translation(1.55f, 6.58f, 0.0f) * Mat4::scale(0.55f, 0.12f, 0.28f),
                             {255, 240, 206, 255}, 0.15f, 0.0f, nightMode ? 2.6f : 0.2f, MatId::EmissiveGlow, false);
                }
                break;
            }
            case StreetPropKind::BusShelter: {
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 2.55f, 0) * Mat4::scale(4.2f, 0.18f, 1.55f),
                         {48, 56, 68, 255}, 0.35f, 0.65f, 0.0f, MatId::Standard, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 1.25f, -0.68f) * Mat4::scale(4.0f, 2.4f, 0.10f),
                         {74, 118, 152, 255}, 0.15f, 0.25f, 0.0f, MatId::GlassSkyscraper, shadowPass);
                drawMesh(vk, cubeMesh_, baseM * Mat4::translation(0, 0.52f, -0.25f) * Mat4::scale(2.8f, 0.12f, 0.45f),
                         {132, 92, 58, 255}, 0.75f, 0.0f, 0.0f, MatId::WoodBark, shadowPass);
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

    const float stride = alive ? std::sin(animPhase) * (jumping ? 0.18f : 0.48f) : 0.0f;
    const float armSwing = alive ? -stride * 0.72f : 0.0f;
    const ColorRGBA shoeCol{30, 32, 38, 255};

    // Legs & shoes
    for (float s : {-1.0f, 1.0f}) {
        const float legAng = s * stride;
        const Mat4 legM = root * Mat4::translation(0.0f, 0.82f, s * 0.125f) *
                          Mat4::rotationAxis({0, 0, 1}, legAng);
        drawMesh(vk, cubeMesh_, legM * Mat4::translation(0.0f, -0.38f, 0.0f) * Mat4::scale(0.18f, 0.78f, 0.17f),
                 bottom, 0.78f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        drawMesh(vk, cubeMesh_, legM * Mat4::translation(0.04f, -0.78f, 0.0f) * Mat4::scale(0.24f, 0.11f, 0.15f),
                 shoeCol, 0.60f, 0.05f, 0.0f, MatId::Standard, shadowPass);
    }

    // Torso / Jacket / Vest
    drawMesh(vk, cubeMesh_, root * Mat4::translation(0.0f, 1.15f, 0.0f) * Mat4::scale(0.28f, 0.64f, 0.44f),
             top, 0.76f, 0.04f, 0.0f, MatId::FabricCloth, shadowPass);
    if (isSwat || outfit == OutfitId::TacOpsArmor) {
        drawMesh(vk, cubeMesh_, root * Mat4::translation(0.0f, 1.17f, 0.0f) * Mat4::scale(0.32f, 0.46f, 0.46f),
                 {32, 36, 44, 255}, 0.58f, 0.18f, 0.0f, MatId::Standard, shadowPass);
    }

    // Head & Hair / Cap / Police Helmet
    const float headLook = npcPtr ? npcPtr->headLookYaw : 0.0f;
    const Mat4 headM = root * Mat4::translation(0.0f, 1.62f, 0.0f) * Mat4::rotationAxis({0, 1, 0}, -headLook);
    drawMesh(vk, sphereMesh_, headM * Mat4::scale(0.27f, 0.30f, 0.25f),
             skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

    if (!shadowPass) {
        const ColorRGBA hairCol = customChar
            ? getPlayerHairColor(outfit, customChar->hairColorIdx)
            : (isPolice ? ColorRGBA{24, 42, 86, 255} : ColorRGBA{32, 28, 26, 255});
        if (isPolice || isSwat) {
            // Seoul Police peaked cap / SWAT tactical helmet
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(0, 0.08f, 0) * Mat4::scale(0.29f, 0.18f, 0.27f),
                     isSwat ? ColorRGBA{28, 32, 38, 255} : ColorRGBA{22, 42, 92, 255}, 0.45f, 0.15f, 0.0f, MatId::Standard, false);
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.14f, 0.04f, 0) * Mat4::scale(0.14f, 0.03f, 0.20f),
                     {20, 22, 26, 255}, 0.35f, 0.15f, 0.0f, MatId::Standard, false);
        } else {
            drawMesh(vk, sphereMesh_, headM * Mat4::translation(-0.02f, 0.07f, 0.0f) * Mat4::scale(0.28f, 0.20f, 0.26f),
                     hairCol, 0.72f, 0.04f, 0.0f, MatId::Standard, false);
        }
        if ((customChar && customChar->glassesStyle > 0) || (npcPtr && npcPtr->glassesStyle > 0)) {
            drawMesh(vk, cubeMesh_, headM * Mat4::translation(0.12f, 0.02f, 0.0f) * Mat4::scale(0.05f, 0.06f, 0.22f),
                     {16, 18, 24, 255}, 0.08f, 0.35f, 0.0f, MatId::CarGlass, false);
        }
    }

    // Arms + Weapon / Rain Umbrella
    for (float s : {-1.0f, 1.0f}) {
        float pitchA = s * armSwing;
        if (panicking) pitchA = -2.4f;
        else if (armed && s > 0.0f) pitchA = -kHalfPi + aimPitch;
        else if (npcPtr && npcPtr->holdingUmbrella && s > 0.0f) pitchA = -1.25f;

        const Mat4 armM = root * Mat4::translation(0.0f, 1.38f, s * 0.27f) *
                          Mat4::rotationAxis({0, 0, 1}, pitchA);
        drawMesh(vk, cubeMesh_, armM * Mat4::translation(0.0f, -0.26f, 0.0f) * Mat4::scale(0.13f, 0.56f, 0.13f),
                 top, 0.76f, 0.02f, 0.0f, MatId::FabricCloth, shadowPass);
        drawMesh(vk, sphereMesh_, armM * Mat4::translation(0.0f, -0.56f, 0.0f) * Mat4::scale(0.11f, 0.11f, 0.11f),
                 skin, 0.62f, 0.0f, 0.0f, MatId::SkinOrganic, shadowPass);

        // Held Weapon on Right Arm
        if (armed && s > 0.0f && weapon != WeaponId::Fists && !shadowPass) {
            const float gunLen = (weapon == WeaponId::Pistol || weapon == WeaponId::HeavyRevolver) ? 0.28f
                               : ((weapon == WeaponId::SniperRifle || weapon == WeaponId::HeavyLauncher) ? 0.92f : 0.62f);
            const ColorRGBA wCol = customWep ? getWeaponFinishMaterial(weapon, customWep->skinIndex).primary
                                             : ColorRGBA{38, 42, 48, 255};
            drawMesh(vk, cubeMesh_, armM * Mat4::translation(0.06f, -0.58f - gunLen * 0.35f, 0.0f) *
                     Mat4::scale(0.07f, gunLen, 0.08f), wCol, 0.32f, 0.78f, 0.0f, MatId::Standard, false);
            if (muzzleFlash > 0.01f) {
                drawMesh(vk, sphereMesh_, armM * Mat4::translation(0.06f, -0.60f - gunLen * 0.88f, 0.0f) *
                         Mat4::scale(0.24f, 0.24f, 0.24f), {255, 224, 102, 240}, 0.1f, 0.0f, 3.2f,
                         MatId::EmissiveGlow, false, WorldPipelineKind::Additive);
            }
        }
    }

    // Korean Rain Umbrella when raining!
    if (npcPtr && npcPtr->holdingUmbrella && alive && !shadowPass) {
        drawMesh(vk, cylMesh_, root * Mat4::translation(0.14f, 1.35f, 0.24f) * Mat4::scale(0.03f, 0.95f, 0.03f),
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
            drawCharacter(vk, pl.pos, gy, pl.facingYaw, pl.walkAnimPhase, top, bot, skin,
                          !pl.wasted, true, pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                          pl.currentOutfit, shadowPass, !pl.onGround, false, false, false,
                          &pl.characterCustom, &wepSlot, nullptr, nightMode);
        }

        // Draw Living Seoul Citizens, Gangsters & Police Officers!
        const float npcCull = shadowPass ? 90.0f : 185.0f;
        for (const auto& npc : world.npcs()) {
            if ((npc.pos - focus).lengthSq() > npcCull * npcCull) continue;
            const float gy = world.city().surfaceHeightAt(npc.pos);
            drawCharacter(vk, npc.pos, gy, npc.facingYaw, npc.animPhase,
                          npc.outfitTop, npc.outfitBottom, npc.skinColor,
                          npc.alive, (npc.role != NpcRole::Civilian), npc.weapon,
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
