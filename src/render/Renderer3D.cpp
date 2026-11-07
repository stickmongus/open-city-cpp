#include "render/Renderer3D.hpp"

#include <cmath>
#include <rlgl.h>

namespace oc {

namespace {

const char* kWorldVertShader = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main() {
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kWorldFragShader = R"GLSL(
#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform vec3 fogColor;
uniform vec3 viewPos;
uniform float nightEmissive;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 base = texel * colDiffuse * fragColor;

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-lightDir);
    float ndotl = max(dot(N, L), 0.0);

    // Hemisphere sky bounce + directional sun/moon diffuse
    float hemi = 0.5 + 0.5 * N.y;
    vec3 lighting = ambientColor * mix(0.68, 1.12, hemi) + sunColor * ndotl;

    // Subtle specular sheen on glass towers
    vec3 V = normalize(viewPos - fragPosition);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 28.0) * 0.18;

    // Lit office/apartment windows glow warmly/coolly when nightEmissive > 0
    float windowMask = step(0.88, max(texel.r, texel.b)) * nightEmissive * (1.0 - abs(N.y));
    vec3 litRgb = base.rgb * lighting + sunColor * spec + texel.rgb * windowMask * 0.78;

    // Smooth atmospheric distance fog
    float dist = length(viewPos - fragPosition);
    float fogFactor = clamp((dist - 115.0) / 260.0, 0.0, 0.88);
    vec3 colorWithFog = mix(litRgb, fogColor, fogFactor);

    finalColor = vec4(colorWithFog, base.a);
}
)GLSL";

Color toRayColor(ColorRGBA c, float brightness = 1.0f) {
    return Color{
        static_cast<unsigned char>(clampf(static_cast<float>(c.r) * brightness, 0.0f, 255.0f)),
        static_cast<unsigned char>(clampf(static_cast<float>(c.g) * brightness, 0.0f, 255.0f)),
        static_cast<unsigned char>(clampf(static_cast<float>(c.b) * brightness, 0.0f, 255.0f)),
        c.a
    };
}

Texture2D buildWindowFacadeTexture(bool nightVariant) {
    constexpr int W = 64;
    constexpr int H = 128;
    Image img = GenImageColor(W, H, Color{195, 202, 212, 255});

    Rng rng(nightVariant ? 9091ULL : 4041ULL);
    for (int wy = 0; wy < 16; ++wy) {
        for (int wx = 0; wx < 8; ++wx) {
            const int px = wx * 8 + 1;
            const int py = wy * 8 + 1;
            Color paneCol{128, 148, 172, 255};
            if (nightVariant && rng.chance(0.42f)) {
                paneCol = rng.chance(0.7f) ? Color{255, 226, 152, 255} : Color{168, 224, 255, 255};
            } else if (nightVariant) {
                paneCol = Color{52, 62, 78, 255};
            }
            ImageDrawRectangle(&img, px, py, 5, 5, paneCol);
        }
    }

    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    return tex;
}

} // namespace

Renderer3D::~Renderer3D() {
    shutdown();
}

void Renderer3D::init() {
    worldShader_ = LoadShaderFromMemory(kWorldVertShader, kWorldFragShader);
    locLightDir_ = GetShaderLocation(worldShader_, "lightDir");
    locSunColor_ = GetShaderLocation(worldShader_, "sunColor");
    locAmbientColor_ = GetShaderLocation(worldShader_, "ambientColor");
    locFogColor_ = GetShaderLocation(worldShader_, "fogColor");
    locViewPos_ = GetShaderLocation(worldShader_, "viewPos");
    locNightEmissive_ = GetShaderLocation(worldShader_, "nightEmissive");
    worldShader_.locs[SHADER_LOC_VECTOR_VIEW] = locViewPos_;

    buildingDayTex_ = buildWindowFacadeTexture(false);
    buildingNightTex_ = buildWindowFacadeTexture(true);

    Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    unitCubeModel_ = LoadModelFromMesh(cubeMesh);
    unitCubeModel_.materials[0].shader = worldShader_;

    Mesh winMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    windowBuildingModel_ = LoadModelFromMesh(winMesh);
    windowBuildingModel_.materials[0].shader = worldShader_;
    windowBuildingModel_.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = buildingDayTex_;

    initialized_ = true;
}

void Renderer3D::shutdown() {
    if (!initialized_) return;
    UnloadModel(unitCubeModel_);
    UnloadModel(windowBuildingModel_);
    UnloadTexture(buildingDayTex_);
    UnloadTexture(buildingNightTex_);
    UnloadShader(worldShader_);
    initialized_ = false;
}

Color Renderer3D::computeSkyColor(const GameWorld& world) const {
    const float h = world.timeOfDayHours();
    // Dawn (5.5..7.5), Day (7.5..17.0), Sunset (17.0..19.5), Night (19.5..5.5)
    if (h >= 7.5f && h < 17.0f) {
        return Color{108, 178, 242, 255};
    }
    if (h >= 17.0f && h < 19.5f) {
        const float t = (h - 17.0f) / 2.5f;
        if (t < 0.5f) {
            const float k = t * 2.0f;
            return Color{
                static_cast<unsigned char>(lerpf(108.0f, 242.0f, k)),
                static_cast<unsigned char>(lerpf(178.0f, 126.0f, k)),
                static_cast<unsigned char>(lerpf(242.0f, 108.0f, k)),
                255
            };
        }
        const float k = (t - 0.5f) * 2.0f;
        return Color{
            static_cast<unsigned char>(lerpf(242.0f, 18.0f, k)),
            static_cast<unsigned char>(lerpf(126.0f, 24.0f, k)),
            static_cast<unsigned char>(lerpf(108.0f, 46.0f, k)),
            255
        };
    }
    if (h >= 5.2f && h < 7.5f) {
        const float k = (h - 5.2f) / 2.3f;
        return Color{
            static_cast<unsigned char>(lerpf(18.0f, 108.0f, k)),
            static_cast<unsigned char>(lerpf(24.0f, 178.0f, k)),
            static_cast<unsigned char>(lerpf(46.0f, 242.0f, k)),
            255
        };
    }
    return Color{16, 22, 42, 255};
}

void Renderer3D::updateShaderUniforms(const GameWorld& world, const Camera3D& camera, Color skyCol) {
    const float day = world.daylightFactor();
    const float sunAngle = (world.timeOfDayHours() / 24.0f) * kTwoPi - kHalfPi;
    const float lightDir[3] = {
        -std::cos(sunAngle) * 0.65f,
        -std::max(0.28f, std::abs(std::sin(sunAngle)) * 0.85f + 0.25f),
        -0.42f
    };

    const bool dusk = (world.timeOfDayHours() >= 16.8f && world.timeOfDayHours() <= 19.3f);
    const float sunCol[3] = {
        dusk ? 0.98f : lerpf(0.20f, 0.92f, day),
        dusk ? 0.62f : lerpf(0.24f, 0.90f, day),
        dusk ? 0.45f : lerpf(0.40f, 0.84f, day)
    };
    const float ambCol[3] = {
        lerpf(0.18f, 0.52f, day),
        lerpf(0.20f, 0.55f, day),
        lerpf(0.32f, 0.62f, day)
    };
    const float fogCol[3] = {
        static_cast<float>(skyCol.r) / 255.0f,
        static_cast<float>(skyCol.g) / 255.0f,
        static_cast<float>(skyCol.b) / 255.0f
    };
    const float viewPos[3] = {camera.position.x, camera.position.y, camera.position.z};
    const float nightEmit = world.isNightTime() ? 1.0f : 0.0f;

    SetShaderValue(worldShader_, locLightDir_, lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locSunColor_, sunCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locAmbientColor_, ambCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locFogColor_, fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locViewPos_, viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locNightEmissive_, &nightEmit, SHADER_UNIFORM_FLOAT);

    windowBuildingModel_.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        world.isNightTime() ? buildingNightTex_ : buildingDayTex_;
}

void Renderer3D::renderWorld(const GameWorld& world, const Camera3D& camera) {
    const Color skyCol = computeSkyColor(world);
    updateShaderUniforms(world, camera, skyCol);

    const Vec2 focus{camera.target.x, camera.target.z};
    const bool nightMode = world.isNightTime();
    const float shadeScale = 0.42f + 0.58f * world.daylightFactor();

    drawOceanAndGround(world);
    drawRoadNetwork(world, focus);
    drawBuildingsAndParks(world, focus);
    drawTreesAndProps(world, focus);

    // Vehicles
    for (const auto& v : world.vehicles()) {
        if ((v.pos - focus).lengthSq() > 265.0f * 265.0f) continue;
        drawVehicle3D(v, nightMode);
    }

    // Player character when on foot
    if (world.player().drivingVehicleId < 0) {
        const auto& pl = world.player();
        drawCharacter3D(pl.pos, pl.facingYaw, pl.walkAnimPhase,
                        {38, 128, 232, 255}, {32, 36, 46, 255}, {236, 196, 162, 255},
                        !pl.wasted, pl.currentWeapon != WeaponId::Fists,
                        pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch);
    }

    // Pedestrians & Gangsters
    for (const auto& npc : world.npcs()) {
        if ((npc.pos - focus).lengthSq() > 175.0f * 175.0f) continue;
        drawCharacter3D(npc.pos, npc.facingYaw, npc.animPhase,
                        npc.outfitTop, npc.outfitBottom, npc.skinColor,
                        npc.alive, npc.role == NpcRole::Gangster,
                        WeaponId::Pistol, 0.0f);
        (void)shadeScale;
    }

    drawPoisAndPickups(world);
    drawSkidsTracersParticles(world);
    drawGpsOverheadArrow(world);
}

void Renderer3D::drawOceanAndGround(const GameWorld& world) {
    const Color oceanCol = world.isNightTime() ? Color{14, 38, 68, 255} : Color{28, 108, 168, 255};
    DrawModelEx(unitCubeModel_, {0.0f, -1.6f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                {1400.0f, 1.0f, 1400.0f}, oceanCol);

    // Sandy beach perimeter around city island
    DrawModelEx(unitCubeModel_, {0.0f, -0.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                {CityMap::kHalfCitySpan * 2.06f, 0.9f, CityMap::kHalfCitySpan * 2.06f},
                Color{216, 198, 156, 255});

    // Concrete city base slab (sidewalk elevation)
    DrawModelEx(unitCubeModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                {CityMap::kHalfCitySpan * 2.0f, 0.44f, CityMap::kHalfCitySpan * 2.0f},
                Color{146, 150, 156, 255});
}

void Renderer3D::drawRoadNetwork(const GameWorld& world, const Vec2& focus) {
    const auto& city = world.city();
    const float span = (city.roadCoord(CityMap::kRoadCount - 1) - city.roadCoord(0)) + CityMap::kRoadWidth;
    const Color asphalt{46, 49, 56, 255};
    const Color yellowLine{228, 186, 52, 255};
    const Color whiteDash{220, 224, 230, 255};

    // N-S and E-W main asphalt strips
    for (int i = 0; i < CityMap::kRoadCount; ++i) {
        const float c = city.roadCoord(i);
        DrawModelEx(unitCubeModel_, {c, 0.01f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {CityMap::kRoadWidth, 0.04f, span}, asphalt);
        DrawModelEx(unitCubeModel_, {0.0f, 0.012f, c}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {span, 0.04f, CityMap::kRoadWidth}, asphalt);
    }

    // Center yellow dividers & lane dashes within visible radius of camera focus
    for (int i = 0; i < CityMap::kRoadCount; ++i) {
        const float rc = city.roadCoord(i);
        for (int seg = 0; seg < CityMap::kBlockCount; ++seg) {
            const float s0 = city.roadCoord(seg) + CityMap::kRoadWidth * 0.5f + 2.0f;
            const float s1 = city.roadCoord(seg + 1) - CityMap::kRoadWidth * 0.5f - 2.0f;
            const float mid = (s0 + s1) * 0.5f;
            const float len = s1 - s0;

            // Vertical road segment (x = rc, z in [s0, s1])
            if (std::abs(rc - focus.x) < 220.0f && std::abs(mid - focus.z) < 220.0f) {
                DrawCube({rc, 0.04f, mid}, 0.22f, 0.02f, len, yellowLine);
                for (float d = s0 + 4.0f; d < s1 - 3.0f; d += 9.0f) {
                    DrawCube({rc - CityMap::kLaneOffset, 0.04f, d}, 0.16f, 0.02f, 3.6f, whiteDash);
                    DrawCube({rc + CityMap::kLaneOffset, 0.04f, d}, 0.16f, 0.02f, 3.6f, whiteDash);
                }
            }

            // Horizontal road segment (z = rc, x in [s0, s1])
            if (std::abs(mid - focus.x) < 220.0f && std::abs(rc - focus.z) < 220.0f) {
                DrawCube({mid, 0.04f, rc}, len, 0.02f, 0.22f, yellowLine);
                for (float d = s0 + 4.0f; d < s1 - 3.0f; d += 9.0f) {
                    DrawCube({d, 0.04f, rc - CityMap::kLaneOffset}, 3.6f, 0.02f, 0.16f, whiteDash);
                    DrawCube({d, 0.04f, rc + CityMap::kLaneOffset}, 3.6f, 0.02f, 0.16f, whiteDash);
                }
            }
        }
    }

    // Crosswalk stripes at nearby intersections
    for (int rz = 0; rz < CityMap::kRoadCount; ++rz) {
        for (int rx = 0; rx < CityMap::kRoadCount; ++rx) {
            const float cx = city.roadCoord(rx);
            const float cz = city.roadCoord(rz);
            if (std::abs(cx - focus.x) > 150.0f || std::abs(cz - focus.z) > 150.0f) continue;

            const float edge = CityMap::kRoadWidth * 0.5f + 0.8f;
            for (int s = -3; s <= 3; ++s) {
                const float off = static_cast<float>(s) * 2.1f;
                DrawCube({cx + off, 0.042f, cz - edge}, 0.9f, 0.02f, 2.2f, whiteDash);
                DrawCube({cx + off, 0.042f, cz + edge}, 0.9f, 0.02f, 2.2f, whiteDash);
            }
        }
    }
}

void Renderer3D::drawBuildingsAndParks(const GameWorld& world, const Vec2& focus) {
    const auto& city = world.city();

    for (const auto& park : city.parks()) {
        const Vec2 c = park.bounds.center();
        if ((c - focus).lengthSq() > 310.0f * 310.0f) continue;
        const Vec2 he = park.bounds.halfExtents();
        DrawModelEx(unitCubeModel_, {c.x, 0.05f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {he.x * 2.0f, 0.12f, he.z * 2.0f}, toRayColor(park.grassColor));
    }

    // Central Park Plaza fountain monument
    if (const auto& parks = city.parks(); !parks.empty()) {
        const Vec2 fc = parks.front().bounds.center();
        if ((fc - focus).lengthSq() < 240.0f * 240.0f) {
            DrawCylinder({fc.x, 0.06f, fc.z}, 5.4f, 5.8f, 0.55f, 16, Color{185, 190, 198, 255});
            DrawCylinder({fc.x, 0.45f, fc.z}, 4.6f, 4.6f, 0.25f, 16, Color{64, 188, 238, 255});
            DrawCylinder({fc.x, 0.55f, fc.z}, 0.9f, 1.3f, 3.2f, 12, Color{212, 216, 224, 255});
        }
    }

    for (const auto& b : city.buildings()) {
        const Vec2 c = b.footprint.center();
        if ((c - focus).lengthSq() > 340.0f * 340.0f) continue;
        const Vec2 he = b.footprint.halfExtents();

        Model& mdl = b.litWindows ? windowBuildingModel_ : unitCubeModel_;
        DrawModelEx(mdl, {c.x, b.height * 0.5f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {he.x * 2.0f, b.height, he.z * 2.0f}, toRayColor(b.primaryColor));

        // Rooftop trim / neon cornice
        DrawModelEx(unitCubeModel_, {c.x, b.height + 0.28f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {he.x * 2.04f, 0.56f, he.z * 2.04f}, toRayColor(b.accentColor));

        if (b.upperTierHeight > 0.5f) {
            const float uy = b.height + b.upperTierHeight * 0.5f;
            DrawModelEx(mdl, {c.x, uy, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 1.35f, b.upperTierHeight, he.z * 1.35f}, toRayColor(b.primaryColor));
            // Spire antenna
            DrawCylinder({c.x, b.height + b.upperTierHeight, c.z}, 0.18f, 0.32f, 6.5f, 8, Color{210, 215, 225, 255});
        }

        // Neon storefront signs for Weapon Shop & Tune Garage
        if (b.style == BuildingStyle::ShopBuilding || b.style == BuildingStyle::GarageBuilding) {
            const Color signCol = (b.style == BuildingStyle::ShopBuilding)
                                      ? Color{255, 88, 58, 255}
                                      : Color{48, 218, 255, 255};
            DrawCube({c.x, 5.2f, b.footprint.min.z - 0.25f}, he.x * 1.5f, 1.4f, 0.35f, signCol);
        }
    }
}

void Renderer3D::drawTreesAndProps(const GameWorld& world, const Vec2& focus) {
    const float dayShade = 0.48f + 0.52f * world.daylightFactor();

    for (const auto& t : world.city().trees()) {
        if ((t.pos - focus).lengthSq() > 210.0f * 210.0f) continue;
        DrawCylinder({t.pos.x, 0.0f, t.pos.z}, 0.28f, 0.38f, t.trunkHeight, 7,
                     toRayColor({112, 78, 52, 255}, dayShade));
        const ColorRGBA leafCol = t.palmStyle ? ColorRGBA{72, 168, 78, 255} : ColorRGBA{52, 146, 68, 255};
        DrawSphere({t.pos.x, t.trunkHeight + t.crownRadius * 0.65f, t.pos.z},
                   t.crownRadius, toRayColor(leafCol, dayShade));
    }

    const bool night = world.isNightTime();
    for (const auto& prop : world.city().streetProps()) {
        if ((prop.pos - focus).lengthSq() > 165.0f * 165.0f) continue;
        const float tilt = prop.broken ? 78.0f : 0.0f;

        rlPushMatrix();
        rlTranslatef(prop.pos.x, 0.0f, prop.pos.z);
        if (prop.broken) {
            rlRotatef(tilt, 1.0f, 0.0f, 0.3f);
        }

        switch (prop.kind) {
            case StreetPropKind::StreetLight: {
                DrawCylinder({0.0f, 0.0f, 0.0f}, 0.11f, 0.15f, 6.2f, 6, Color{82, 88, 96, 255});
                DrawCube({0.65f, 6.15f, 0.0f}, 1.4f, 0.16f, 0.22f, Color{92, 98, 108, 255});
                if (!prop.broken) {
                    // Intersection traffic signal housing + live E-W / N-S signal lamps
                    const bool ewGreen = world.isTrafficGreenForHorizontal();
                    const bool nsGreen = world.isTrafficGreenForVertical();
                    const Color ewCol = ewGreen ? Color{56, 238, 98, 255}
                                                : (nsGreen ? Color{250, 62, 58, 255} : Color{255, 196, 48, 255});
                    const Color nsCol = nsGreen ? Color{56, 238, 98, 255}
                                                : (ewGreen ? Color{250, 62, 58, 255} : Color{255, 196, 48, 255});
                    DrawCube({1.05f, 5.62f, 0.0f}, 0.30f, 0.72f, 0.30f, Color{26, 30, 38, 255});
                    DrawSphere({1.21f, 5.62f, 0.0f}, 0.11f, ewCol);
                    DrawSphere({0.89f, 5.62f, 0.0f}, 0.11f, ewCol);
                    DrawSphere({1.05f, 5.62f, 0.16f}, 0.11f, nsCol);
                    DrawSphere({1.05f, 5.62f, -0.16f}, 0.11f, nsCol);

                    if (night) {
                        DrawSphere({1.2f, 6.0f, 0.0f}, 0.26f, Color{255, 232, 162, 255});
                        DrawCylinder({1.2f, 0.03f, 0.0f}, 3.6f, 3.6f, 0.02f, 12, Color{255, 224, 138, 42});
                    }
                }
                break;
            }
            case StreetPropKind::Hydrant: {
                DrawCylinder({0.0f, 0.0f, 0.0f}, 0.22f, 0.25f, 0.85f, 8,
                             toRayColor({218, 48, 42, 255}, dayShade));
                break;
            }
            case StreetPropKind::Crate: {
                DrawCube({0.0f, 0.45f, 0.0f}, 0.9f, 0.9f, 0.9f,
                         toRayColor({168, 124, 74, 255}, dayShade));
                break;
            }
            case StreetPropKind::TrafficCone: {
                DrawCylinder({0.0f, 0.0f, 0.0f}, 0.05f, 0.26f, 0.68f, 8, Color{248, 118, 32, 255});
                break;
            }
        }
        rlPopMatrix();
    }
}

void Renderer3D::drawVehicle3D(const Vehicle& v, bool nightMode) {
    const auto& spec = getVehicleSpec(v.kind);
    const float L = spec.halfLength * 2.0f;
    const float W = spec.halfWidth * 2.0f;
    const float H = spec.chassisHeight;

    // Ground drop shadow
    rlPushMatrix();
    rlTranslatef(v.pos.x, 0.035f, v.pos.z);
    rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
    DrawCube({0.0f, 0.0f, 0.0f}, L * 1.04f, 0.01f, W * 1.06f, Color{12, 14, 18, 135});
    rlPopMatrix();

    rlPushMatrix();
    rlTranslatef(v.pos.x, 0.0f, v.pos.z);
    rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(v.bodyPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(v.bodyRoll * kRadToDeg, 1.0f, 0.0f, 0.0f);

    const Color bodyCol = toRayColor(v.paintColor);
    const Color darkTrim{30, 33, 40, 255};
    const Color glassCol{42, 62, 84, 235};

    // Lower body & bumper skirt
    const float lowerH = H * 0.52f;
    DrawCube({0.0f, 0.24f + lowerH * 0.5f, 0.0f}, L, lowerH, W, bodyCol);
    DrawCube({0.0f, 0.22f, 0.0f}, L * 1.02f, 0.14f, W * 1.02f, darkTrim);

    // Racing center stripe on SportsCoupe & MuscleCar
    if ((v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::MuscleCar) && !v.destroyed) {
        DrawCube({0.0f, 0.25f + lowerH, 0.0f}, L * 0.98f, 0.03f, W * 0.22f, toRayColor(v.stripeColor));
    }

    // Upper cabin
    const float cabLen = (v.kind == VehicleKind::DeliveryVan || v.kind == VehicleKind::SUV) ? L * 0.68f : L * 0.50f;
    const float cabOffset = (v.kind == VehicleKind::DeliveryVan) ? -L * 0.06f : -L * 0.05f;
    const float cabH = H * 0.48f;
    const float cabY = 0.24f + lowerH + cabH * 0.5f;
    DrawCube({cabOffset, cabY, 0.0f}, cabLen, cabH, W * 0.86f, bodyCol);
    DrawCube({cabOffset, cabY - 0.02f, 0.0f}, cabLen * 1.02f, cabH * 0.76f, W * 0.88f, glassCol);

    // Rear wing spoiler for SportsCoupe
    if (spec.hasSpoiler) {
        DrawCube({-L * 0.44f, 0.28f + lowerH + 0.22f, -W * 0.34f}, 0.14f, 0.26f, 0.08f, darkTrim);
        DrawCube({-L * 0.44f, 0.28f + lowerH + 0.22f,  W * 0.34f}, 0.14f, 0.26f, 0.08f, darkTrim);
        DrawCube({-L * 0.45f, 0.28f + lowerH + 0.35f,  0.0f},      0.34f, 0.06f, W * 0.96f, bodyCol);
    }

    // Taxi roof sign
    if (v.kind == VehicleKind::Taxi && !v.destroyed) {
        DrawCube({cabOffset, cabY + cabH * 0.5f + 0.11f, 0.0f}, 0.42f, 0.20f, 0.68f, Color{255, 240, 150, 255});
    }

    // 4 spinning & steering wheels
    const float axleX = L * 0.33f;
    const float axleZ = W * 0.50f;
    const float wheelR = 0.34f;
    for (int side = -1; side <= 1; side += 2) {
        for (int front = -1; front <= 1; front += 2) {
            rlPushMatrix();
            rlTranslatef(static_cast<float>(front) * axleX, wheelR, static_cast<float>(side) * axleZ);
            if (front > 0) {
                rlRotatef(-v.steerVisual * 28.0f, 0.0f, 1.0f, 0.0f);
            }
            rlRotatef(-v.wheelSpin * kRadToDeg, 0.0f, 0.0f, 1.0f);
            DrawCube({0.0f, 0.0f, 0.0f}, wheelR * 1.85f, wheelR * 1.85f, 0.24f, Color{22, 24, 28, 255});
            DrawCube({0.0f, 0.0f, static_cast<float>(side) * 0.03f}, wheelR * 1.05f, wheelR * 1.05f, 0.25f, Color{182, 188, 198, 255});
            rlPopMatrix();
        }
    }

    if (!v.destroyed) {
        // Front headlights + night road light cones
        const Color headCol = nightMode ? Color{255, 250, 215, 255} : Color{235, 242, 250, 255};
        DrawCube({L * 0.50f, 0.44f, -W * 0.34f}, 0.08f, 0.16f, 0.26f, headCol);
        DrawCube({L * 0.50f, 0.44f,  W * 0.34f}, 0.08f, 0.16f, 0.26f, headCol);

        if (nightMode && (v.isPlayerDriving || v.hasNpcDriver)) {
            DrawCube({L * 0.5f + 4.2f, 0.05f, 0.0f}, 7.8f, 0.02f, W * 1.75f, Color{255, 246, 198, 48});
        }

        // Rear brake / taillights
        const Color tailCol = v.brakingVisual ? Color{255, 42, 42, 255} : Color{168, 28, 32, 255};
        DrawCube({-L * 0.50f, 0.46f, -W * 0.34f}, 0.08f, 0.15f, 0.28f, tailCol);
        DrawCube({-L * 0.50f, 0.46f,  W * 0.34f}, 0.08f, 0.15f, 0.28f, tailCol);

        // Twin blue Nitrous exhaust flames!
        if (v.nitroActive) {
            DrawCube({-L * 0.56f, 0.28f, -W * 0.26f}, 0.75f, 0.16f, 0.16f, Color{78, 205, 255, 235});
            DrawCube({-L * 0.56f, 0.28f,  W * 0.26f}, 0.75f, 0.16f, 0.16f, Color{78, 205, 255, 235});
        }
    }

    rlPopMatrix();
}

void Renderer3D::drawCharacter3D(const Vec2& pos, float yaw, float animPhase,
                                 ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                                 bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                                 float aimPitch) {
    rlPushMatrix();
    rlTranslatef(pos.x, 0.0f, pos.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);

    if (!alive) {
        rlTranslatef(0.0f, 0.18f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    }

    const float legSwing = alive ? std::sin(animPhase) * 28.0f : 0.0f;
    const Color topC = toRayColor(top);
    const Color botC = toRayColor(bottom);
    const Color skinC = toRayColor(skin);

    // Shadow
    if (alive) {
        DrawCylinder({0.0f, 0.03f, 0.0f}, 0.38f, 0.38f, 0.01f, 10, Color{12, 14, 18, 120});
    }

    // Left & Right Legs
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 0.76f, static_cast<float>(side) * 0.13f);
        rlRotatef(static_cast<float>(side) * legSwing, 0.0f, 0.0f, 1.0f);
        DrawCube({0.0f, -0.36f, 0.0f}, 0.20f, 0.72f, 0.20f, botC);
        rlPopMatrix();
    }

    // Torso
    DrawCube({0.0f, 1.08f, 0.0f}, 0.28f, 0.64f, 0.44f, topC);

    // Head + Hair cap (tilts naturally with vertical aim)
    rlPushMatrix();
    rlTranslatef(0.0f, 1.44f, 0.0f);
    if (alive) {
        rlRotatef(clampf(aimPitch * 0.65f, -0.65f, 0.75f) * kRadToDeg, 0.0f, 0.0f, 1.0f);
    }
    DrawCube({0.0f, 0.12f, 0.0f}, 0.28f, 0.28f, 0.28f, skinC);
    DrawCube({-0.02f, 0.24f, 0.0f}, 0.30f, 0.09f, 0.30f, Color{38, 32, 28, 255});
    rlPopMatrix();

    // Arms & Equipped Weapon
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 1.32f, static_cast<float>(side) * 0.28f);
        if (armed && side == 1 && alive) {
            // Right arm raised aiming weapon along exact 3D elevation pitch
            rlRotatef(aimPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
            DrawCube({0.26f, -0.04f, 0.0f}, 0.52f, 0.15f, 0.15f, topC);
            const float gunLen = (weapon == WeaponId::AssaultRifle || weapon == WeaponId::Shotgun) ? 0.62f : 0.34f;
            DrawCube({0.54f, 0.02f, 0.0f}, gunLen, 0.11f, 0.09f, Color{32, 35, 42, 255});
            if (muzzleFlash > 0.0f) {
                DrawSphere({0.58f + gunLen * 0.5f, 0.03f, 0.0f}, 0.19f, Color{255, 224, 92, 240});
            }
        } else {
            rlRotatef(static_cast<float>(-side) * legSwing * 0.8f, 0.0f, 0.0f, 1.0f);
            DrawCube({0.0f, -0.26f, 0.0f}, 0.16f, 0.54f, 0.16f, topC);
        }
        rlPopMatrix();
    }

    rlPopMatrix();
}

void Renderer3D::drawPoisAndPickups(const GameWorld& world) {
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.0f);

    for (const auto& poi : world.city().pois()) {
        // Hide mission start coronas while a mission is already active
        if (world.mission().kind != ActiveMissionKind::None &&
            (poi.kind == PoiKind::MissionDelivery ||
             poi.kind == PoiKind::MissionRace ||
             poi.kind == PoiKind::MissionGangRaid)) {
            continue;
        }
        Color c = toRayColor(poi.markerColor);
        c.a = static_cast<unsigned char>(110 + pulse * 75.0f);
        DrawCylinder({poi.pos.x, 0.04f, poi.pos.z}, poi.radius * 0.72f, poi.radius * 0.72f, 2.2f, 20, c);
        DrawCylinderWires({poi.pos.x, 0.05f, poi.pos.z}, poi.radius * 0.74f, poi.radius * 0.74f, 2.25f, 20,
                          toRayColor(poi.markerColor));
    }

    // Active mission objective 3D beacon (checkpoint / drop-off / VIP car halo)
    if (world.mission().kind != ActiveMissionKind::None) {
        const Vec2 mp = world.mission().currentObjectivePos;
        const float r = std::max(2.6f, world.mission().objectiveRadius * 0.65f);
        DrawCylinder({mp.x, 0.05f, mp.z}, r, r, 6.5f, 22, Color{255, 218, 56, 95});
        DrawCylinderWires({mp.x, 0.05f, mp.z}, r, r, 6.5f, 22, Color{255, 228, 82, 235});
    }

    for (const auto& pk : world.pickups()) {
        if (pk.collected) continue;
        const float y = 0.55f + 0.18f * std::sin(pk.bobPhase);
        rlPushMatrix();
        rlTranslatef(pk.pos.x, y, pk.pos.z);
        rlRotatef(pk.bobPhase * kRadToDeg, 0.0f, 1.0f, 0.0f);
        if (pk.kind == PickupKind::CashBundle) {
            DrawCube({0.0f, 0.0f, 0.0f}, 0.55f, 0.24f, 0.35f, Color{72, 224, 96, 255});
        } else if (pk.kind == PickupKind::HealthKit) {
            DrawCube({0.0f, 0.0f, 0.0f}, 0.52f, 0.42f, 0.52f, Color{245, 245, 248, 255});
            DrawCube({0.0f, 0.02f, 0.0f}, 0.54f, 0.14f, 0.18f, Color{235, 52, 52, 255});
        } else {
            DrawCube({0.0f, 0.0f, 0.0f}, 0.72f, 0.52f, 0.26f, Color{255, 204, 52, 255});
        }
        rlPopMatrix();
    }
}

void Renderer3D::drawSkidsTracersParticles(const GameWorld& world) {
    for (const auto& s : world.skidMarks()) {
        const Vec2 mid = (s.a + s.b) * 0.5f;
        const Vec2 d = s.b - s.a;
        const float len = d.length();
        const float yaw = std::atan2(d.z, d.x);
        rlPushMatrix();
        rlTranslatef(mid.x, 0.045f, mid.z);
        rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
        DrawCube({0.0f, 0.0f, 0.0f}, len + 0.12f, 0.01f, 0.24f, Color{18, 18, 20, 165});
        rlPopMatrix();
    }

    for (const auto& tr : world.tracers()) {
        DrawLine3D({tr.start.x, tr.start.y, tr.start.z},
                   {tr.end.x, tr.end.y, tr.end.z},
                   toRayColor(tr.color));
    }

    for (const auto& p : world.particles()) {
        const float life = clampf(p.ttl / std::max(0.01f, p.maxTtl), 0.0f, 1.0f);
        Color c = toRayColor(p.color);
        c.a = static_cast<unsigned char>(static_cast<float>(c.a) * life);
        DrawCube({p.pos.x, p.pos.y, p.pos.z}, p.size, p.size, p.size, c);
    }
}

void Renderer3D::drawGpsOverheadArrow(const GameWorld& world) {
    if (world.mission().kind == ActiveMissionKind::None) return;

    const Vec2 origin = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
    const Vec2 delta = world.mission().currentObjectivePos - origin;
    if (delta.length() < 8.0f) return;

    const float yaw = std::atan2(delta.z, delta.x);
    rlPushMatrix();
    rlTranslatef(origin.x, 3.45f, origin.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
    DrawCube({-0.25f, 0.0f, 0.0f}, 0.95f, 0.16f, 0.34f, Color{255, 216, 48, 235});
    rlRotatef(-90.0f, 0.0f, 0.0f, 1.0f);
    DrawCylinder({0.0f, 0.25f, 0.0f}, 0.0f, 0.44f, 0.65f, 8, Color{255, 228, 72, 245});
    rlPopMatrix();
}

} // namespace oc
