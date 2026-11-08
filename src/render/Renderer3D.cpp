#include "render/Renderer3D.hpp"

#include <algorithm>
#include <cmath>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <raymath.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <rlgl.h>
#include <vector>

namespace oc {

namespace {

constexpr int kShadowMapResolution = 2048;
constexpr int kShadowTextureSlot = 14;

// ---------------------------------------------------------------------------
// 1) Fast Depth-Only Shader for 2048x2048 Directional Sun/Moon Shadow Map
// ---------------------------------------------------------------------------
const char* kDepthVertShader = R"GLSL(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kDepthFragShader = R"GLSL(
#version 330
out vec4 finalColor;

void main() {
    finalColor = vec4(1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// 2) Atmospheric Sky Dome Shader (Rayleigh/Mie Sky, Sun Corona, FBM Clouds, Stars)
// ---------------------------------------------------------------------------
const char* kSkyVertShader = R"GLSL(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;

out vec3 vLocalDir;

void main() {
    vLocalDir = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kSkyFragShader = R"GLSL(
#version 330
in vec3 vLocalDir;

uniform vec3 sunDir;      // Unit vector pointing TOWARD the sun/moon light source
uniform float timeHours;  // [0, 24)
uniform float dayFactor;  // [0, 1]
uniform float animTime;

out vec4 finalColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbmClouds(vec2 p) {
    float sum = 0.0;
    float amp = 0.52;
    mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    for (int i = 0; i < 5; ++i) {
        sum += amp * valueNoise(p);
        p = rot * p * 2.04 + vec2(13.7, 7.3);
        amp *= 0.50;
    }
    return sum;
}

vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 dir = normalize(vLocalDir);
    vec3 S = normalize(sunDir);
    float up = clamp(dir.y, -0.25, 1.0);
    float upPos = max(up, 0.0);

    // Golden-hour dawn (5.3..7.6) & California sunset (16.6..19.5) factor
    float dawn = exp(-pow((timeHours - 6.4) / 1.05, 2.0));
    float dusk = exp(-pow((timeHours - 18.0) / 1.25, 2.0));
    float golden = clamp(dawn + dusk, 0.0, 1.0);
    float night = clamp(1.0 - dayFactor * 1.15, 0.0, 1.0);

    // Linear atmospheric palette
    vec3 dayZenith   = vec3(0.045, 0.175, 0.580);
    vec3 dayHorizon  = vec3(0.320, 0.580, 0.880);
    vec3 goldZenith  = vec3(0.065, 0.095, 0.280);
    vec3 goldHorizon = vec3(0.950, 0.310, 0.110);
    vec3 nightZenith = vec3(0.004, 0.008, 0.026);
    vec3 nightHoriz  = vec3(0.016, 0.028, 0.068);

    vec3 zenithCol = mix(nightZenith, dayZenith, dayFactor);
    zenithCol = mix(zenithCol, goldZenith, golden * 0.72);

    vec3 horizCol = mix(nightHoriz, dayHorizon, dayFactor);
    horizCol = mix(horizCol, goldHorizon, golden * 0.88);

    float skyGrad = pow(1.0 - upPos, 2.35);
    vec3 sky = mix(zenithCol, horizCol, skyGrad);

    // Warm directional Mie scattering around the sun / sunset glow
    float sunCos = max(dot(dir, S), 0.0);
    vec3 mieTint = mix(vec3(1.0, 0.86, 0.62), vec3(1.0, 0.42, 0.14), golden);
    if (night > 0.55) {
        mieTint = vec3(0.25, 0.40, 0.68);
    }
    sky += mieTint * pow(sunCos, 6.0) * (0.28 + 0.45 * golden) * (1.0 - night * 0.65);
    sky += mieTint * pow(sunCos, 42.0) * (0.45 + 0.35 * golden) * (1.0 - night * 0.55);

    // Crisp Solar / Lunar Disc
    if (dir.y > -0.02) {
        float sunDisc = smoothstep(0.99915, 0.99972, dot(dir, S));
        vec3 discCol = (night > 0.6) ? vec3(1.8, 2.1, 2.6) : mix(vec3(6.5, 5.6, 4.2), vec3(7.0, 2.8, 0.9), golden);
        sky += discCol * sunDisc;
    }

    // Twinkling stars on clear upper sky at night
    if (night > 0.48 && dir.y > 0.04) {
        vec2 starUv = dir.xz / (dir.y + 0.35) * 95.0;
        vec2 cell = floor(starUv);
        vec2 f = fract(starUv) - 0.5;
        float rnd = hash12(cell);
        if (rnd > 0.968) {
            float d = length(f);
            float twinkle = 0.65 + 0.35 * sin(animTime * 2.7 + rnd * 91.0);
            float star = smoothstep(0.22, 0.02, d) * twinkle * night * smoothstep(0.04, 0.28, dir.y);
            sky += vec3(0.75, 0.86, 1.0) * star * 1.4;
        }
    }

    // Volumetric-style 2-layer drifting FBM clouds
    if (dir.y > 0.015) {
        vec2 cloudUv = dir.xz / (dir.y + 0.19) * 1.35;
        vec2 wind = vec2(animTime * 0.014, animTime * 0.007);
        float n1 = fbmClouds(cloudUv + wind);
        float n2 = fbmClouds(cloudUv * 2.15 - wind * 1.4 + vec2(4.2, -2.8));
        float density = smoothstep(0.49, 0.80, n1 * 0.72 + n2 * 0.28);
        float horizonFade = smoothstep(0.015, 0.20, dir.y);
        density *= horizonFade;

        // Cloud self-shadowing & sunlit silver lining
        float detailLight = clamp(0.45 + 0.55 * n2 + 0.35 * pow(sunCos, 3.0), 0.0, 1.35);
        vec3 cloudLit = mix(vec3(0.95, 0.97, 1.02), vec3(1.18, 0.56, 0.28), golden * 0.85);
        vec3 cloudShade = mix(vec3(0.32, 0.42, 0.58), vec3(0.26, 0.16, 0.30), golden * 0.80);
        if (night > 0.4) {
            cloudLit = mix(cloudLit, vec3(0.08, 0.11, 0.18), night);
            cloudShade = mix(cloudShade, vec3(0.02, 0.03, 0.06), night);
        }
        vec3 cloudCol = mix(cloudShade, cloudLit, clamp(detailLight, 0.0, 1.0));
        sky = mix(sky, cloudCol, density * 0.82);
    }

    // Distant coastal mountain range silhouette along the bay horizon
    float azimuth = atan(dir.z, dir.x);
    float mtnHeight = 0.016 + 0.018 * sin(azimuth * 3.0 + 0.7)
                    + 0.010 * sin(azimuth * 7.0 - 1.4)
                    + 0.005 * sin(azimuth * 17.0);
    if (dir.y > -0.04 && dir.y < mtnHeight) {
        float ridge = smoothstep(mtnHeight, mtnHeight - 0.004, dir.y);
        vec3 mtnCol = mix(horizCol * 0.42, zenithCol * 0.55, 0.35);
        sky = mix(sky, mtnCol, ridge * 0.78);
    }

    // Below horizon fallback matches ocean haze
    if (dir.y < 0.0) {
        sky = mix(sky, horizCol * 0.48, clamp(-dir.y * 6.0, 0.0, 1.0));
    }

    vec3 mapped = pow(acesTonemap(sky * 1.12), vec3(1.0 / 2.2));
    finalColor = vec4(mapped, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// 3) Unified PBR-Lite + PCF Soft Shadow + Procedural City Material Shader
// ---------------------------------------------------------------------------
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
uniform vec4 colSpecular; // x: roughness, y: metallic, z: emissive, w: matId/255

uniform vec3 lightDir;
uniform vec3 sunColor;
uniform vec3 skyAmbient;
uniform vec3 groundAmbient;
uniform vec3 fogColor;
uniform vec3 horizonColor;
uniform vec3 viewPos;
uniform float animTime;
uniform float nightFactor;
uniform int enableShadows;

uniform mat4 lightVP;
uniform sampler2D shadowMap;
uniform float shadowTexelSize;

out vec4 finalColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float computeSoftShadow(vec3 worldPos, vec3 N, vec3 L) {
    if (enableShadows == 0) return 0.0;

    vec4 lightClip = lightVP * vec4(worldPos + N * 0.09, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.0001);
    proj = proj * 0.5 + 0.5;

    if (proj.x <= 0.01 || proj.x >= 0.99 || proj.y <= 0.01 || proj.y >= 0.99 || proj.z >= 0.998 || proj.z <= 0.0) {
        return 0.0;
    }

    // Smooth fade near the boundary of the moving directional shadow box
    vec2 edgeDist = min(proj.xy, 1.0 - proj.xy);
    float edgeFade = smoothstep(0.01, 0.09, min(edgeDist.x, edgeDist.y));

    float ndotl = max(dot(N, L), 0.0);
    float bias = max(0.0011 * (1.0 - ndotl), 0.00032);

    // 9-tap 3x3 PCF soft shadow filter
    float occ = 0.0;
    for (int ox = -1; ox <= 1; ++ox) {
        for (int oy = -1; oy <= 1; ++oy) {
            vec2 uv = proj.xy + vec2(float(ox), float(oy)) * shadowTexelSize * 1.15;
            float closestDepth = texture(shadowMap, uv).r;
            occ += (proj.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    return (occ / 9.0) * edgeFade;
}

float aaBand(float distFromCenter, float halfWidth) {
    float fw = max(fwidth(distFromCenter), 0.002);
    return 1.0 - smoothstep(halfWidth - fw, halfWidth + fw, distFromCenter);
}

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 srgbBase = texel * colDiffuse * fragColor;
    vec3 albedo = pow(clamp(srgbBase.rgb, 0.0, 1.0), vec3(2.2));

    float roughness = clamp(colSpecular.x, 0.05, 0.98);
    float metallic  = clamp(colSpecular.y, 0.0, 1.0);
    float emissive  = colSpecular.z * 3.5;
    int matId       = int(colSpecular.w * 255.0 + 0.5);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(-lightDir);
    vec3 emissiveColor = albedo;

    // -----------------------------------------------------------------------
    // Procedural World-Space Material Detail
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Asphalt Road Network + Analytic Anti-Aliased Lane Markings & Crosswalks
        float grain = valueNoise(fragPosition.xz * 3.5) * 0.14 + valueNoise(fragPosition.xz * 16.0) * 0.08;
        albedo = vec3(0.030 + grain * 0.06, 0.033 + grain * 0.06, 0.040 + grain * 0.07);
        roughness = 0.46 - grain * 0.18;

        vec2 p = fragPosition.xz;
        float rx = clamp(round(p.x / 84.0), -4.0, 4.0) * 84.0;
        float rz = clamp(round(p.y / 84.0), -4.0, 4.0) * 84.0;
        float dx = abs(p.x - rx);
        float dz = abs(p.y - rz);
        bool onVert  = (dx <= 9.02) && (abs(p.y) <= 345.05);
        bool onHoriz = (dz <= 9.02) && (abs(p.x) <= 345.05);

        // Subtle polished tire wear tracks along travel lanes (+-4.2m)
        if (onVert && !onHoriz) {
            float wear = exp(-pow((abs(dx - 4.2) - 0.85) / 0.55, 2.0));
            albedo *= (1.0 - 0.16 * wear);
            roughness = mix(roughness, 0.28, wear * 0.55);
        } else if (onHoriz && !onVert) {
            float wear = exp(-pow((abs(dz - 4.2) - 0.85) / 0.55, 2.0));
            albedo *= (1.0 - 0.16 * wear);
            roughness = mix(roughness, 0.28, wear * 0.55);
        }

        vec3 yellowPaint = pow(vec3(0.93, 0.72, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.88, 0.90, 0.93), vec3(2.2));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;

        if (onVert && !onHoriz) {
            // Zebra crosswalk right along sidewalk crossing corridor (9.35..12.05m from intersection)
            if (dz >= 9.35 && dz <= 12.05 && dx < 7.9) {
                float stripe = abs(fract((p.x - rx + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            // Stop bar before crosswalk
            if (dz >= 12.45 && dz <= 12.90 && dx < 8.4) {
                whiteMask = max(whiteMask, 0.95);
            }
            // Double yellow divider + dashed lane lines + outer shoulder lines
            if (dz > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dx - 0.18), 0.075));
                if ( fract(p.y / 9.0) < 0.38 ) {
                    whiteMask = max(whiteMask, aaBand(abs(dx - 4.35), 0.075));
                }
                whiteMask = max(whiteMask, aaBand(abs(dx - 8.55), 0.07) * 0.65);
            }
        } else if (onHoriz && !onVert) {
            if (dx >= 9.35 && dx <= 12.05 && dz < 7.9) {
                float stripe = abs(fract((p.y - rz + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dx >= 12.45 && dx <= 12.90 && dz < 8.4) {
                whiteMask = max(whiteMask, 0.95);
            }
            if (dx > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dz - 0.18), 0.075));
                if ( fract(p.x / 9.0) < 0.38 ) {
                    whiteMask = max(whiteMask, aaBand(abs(dz - 4.35), 0.075));
                }
                whiteMask = max(whiteMask, aaBand(abs(dz - 8.55), 0.07) * 0.65);
            }
        } else if (onVert && onHoriz) {
            // Cast-iron utility manhole cover at intersection center
            float mh = length(vec2(dx, dz));
            if (mh < 0.68) {
                albedo = vec3(0.055, 0.052, 0.050);
                roughness = 0.35;
                metallic = 0.55;
            }
        }

        albedo = mix(albedo, whitePaint, clamp(whiteMask, 0.0, 1.0) * 0.90);
        albedo = mix(albedo, yellowPaint, clamp(yellowMask, 0.0, 1.0) * 0.94);
    }
    else if (matId == 2) {
        // MAT 2: Concrete Sidewalk Pavers & Plazas
        vec2 tileUv = fragPosition.xz / 1.75;
        vec2 cell = floor(tileUv);
        vec2 g = abs(fract(tileUv) - 0.5);
        float seamDist = 0.5 - max(g.x, g.y);
        float seam = 1.0 - smoothstep(0.0, max(fwidth(seamDist), 0.015) * 1.4, seamDist - 0.018);
        float slabTint = 0.93 + 0.12 * hash12(cell) + 0.05 * valueNoise(fragPosition.xz * 6.0);
        albedo *= slabTint;
        albedo = mix(albedo, albedo * 0.56, seam * 0.68);
        roughness = 0.76;
    }
    else if (matId == 3) {
        // MAT 3: Architectural Glass Curtain-Wall Skyscraper
        if (abs(N.y) > 0.55) {
            // Flat roof gravel & perimeter parapet shade
            float roofNoise = valueNoise(fragPosition.xz * 2.5);
            albedo = mix(vec3(0.09, 0.10, 0.12), vec3(0.16, 0.17, 0.19), roofNoise);
            roughness = 0.85;
        } else {
            float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
            float vCoord = fragPosition.y;
            float bayW = 2.35;
            float floorH = 3.55;

            float uCell = floor(uCoord / bayW);
            float vCell = floor(vCoord / floorH);
            float uFrac = fract(uCoord / bayW);
            float vFrac = fract(vCoord / floorH);

            bool isGroundLobby = (vCoord < 4.1);
            bool isMullion = (uFrac < 0.065 || uFrac > 0.935);
            bool isSpandrel = isGroundLobby ? (vCoord > 3.75) : (vFrac < 0.13 || vFrac > 0.90);

            if (isMullion || isSpandrel) {
                albedo *= 0.34;
                roughness = 0.42;
                metallic = 0.65;
            } else {
                // Architectural reflective glass pane + interior room depth
                vec2 bldSeed = floor(fragPosition.xz / 14.0);
                float roomRnd = hash12(vec2(uCell * 1.37 + bldSeed.x * 7.1, vCell * 2.19 + bldSeed.y * 11.3));
                float blindStripe = 0.88 + 0.12 * sin(vCoord * 18.0);

                // Daytime deep tinted architectural glass with interior variation
                vec3 glassDay = albedo * (0.45 + 0.48 * roomRnd) * blindStripe;
                albedo = glassDay;
                roughness = 0.11;
                metallic = 0.18;

                // Nighttime illuminated office suites & lobbies
                float litThreshold = isGroundLobby ? 0.22 : 0.52;
                if (nightFactor > 0.08 && roomRnd > litThreshold) {
                    vec3 warmSuite = pow(vec3(1.00, 0.76, 0.38), vec3(2.2));
                    vec3 coolSuite = pow(vec3(0.46, 0.80, 1.00), vec3(2.2));
                    vec3 softWhite = pow(vec3(0.96, 0.91, 0.78), vec3(2.2));
                    vec3 roomCol = (roomRnd > 0.84) ? coolSuite : ((roomRnd > 0.68) ? softWhite : warmSuite);
                    float ceilingGrad = mix(0.55, 1.18, vFrac);
                    emissiveColor = roomCol * ceilingGrad * blindStripe;
                    emissive = nightFactor * (isGroundLobby ? 1.15 : 0.92);
                }
            }
        }
    }
    else if (matId == 4) {
        // MAT 4: Modern Office / Brick & Stone Apartment Facade
        if (abs(N.y) > 0.55) {
            albedo = vec3(0.14, 0.14, 0.15) * (0.85 + 0.25 * valueNoise(fragPosition.xz * 2.2));
            roughness = 0.86;
        } else {
            float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
            float vCoord = fragPosition.y;
            float uFrac = fract(uCoord / 2.85);
            float vFrac = fract(vCoord / 3.30);
            float uCell = floor(uCoord / 2.85);
            float vCell = floor(vCoord / 3.30);

            bool inWindow = (vCoord > 3.4) && (uFrac > 0.22 && uFrac < 0.78) && (vFrac > 0.24 && vFrac < 0.78);
            bool inSill   = (vCoord > 3.4) && (uFrac > 0.18 && uFrac < 0.82) && (vFrac >= 0.18 && vFrac <= 0.24);
            bool groundShop = (vCoord > 0.35 && vCoord < 2.95) && (uFrac > 0.12 && uFrac < 0.88);

            if (inSill) {
                albedo *= 1.35;
                roughness = 0.70;
            } else if (inWindow || groundShop) {
                vec2 seed = floor(fragPosition.xz / 12.0);
                float rnd = hash12(vec2(uCell + seed.x * 5.7, vCell + seed.y * 9.3));
                albedo = vec3(0.05, 0.08, 0.12) * (0.7 + 0.6 * rnd);
                roughness = 0.15;
                if (nightFactor > 0.08 && rnd > (groundShop ? 0.28 : 0.56)) {
                    emissiveColor = pow(mix(vec3(1.0, 0.68, 0.30), vec3(1.0, 0.86, 0.58), fract(rnd * 7.3)), vec3(2.2));
                    emissive = nightFactor * 0.90;
                }
            } else {
                // Subtle horizontal masonry / precast panel joints
                float joint = smoothstep(0.92, 0.98, abs(sin(vCoord * 9.5)));
                albedo *= (0.94 - 0.14 * joint + 0.08 * valueNoise(vec2(uCoord, vCoord) * 2.0));
                roughness = 0.80;
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Corrugated Industrial Steel / Shipping Containers
        float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
        if (abs(N.y) < 0.5) {
            float rib = sin(uCoord * 16.0);
            albedo *= (0.90 + 0.14 * rib);
        }
        roughness = 0.44;
        metallic = 0.45;
    }
    else if (matId == 6) {
        // MAT 6: Animated Coastal Ocean Water with Fresnel Reflection, Sun Glitter & Shore Foam
        vec2 wp = fragPosition.xz;
        float w1 = sin(wp.x * 0.22 + animTime * 1.55 + cos(wp.y * 0.14) * 1.2);
        float w2 = cos(wp.y * 0.29 - animTime * 1.25 + sin(wp.x * 0.19));
        float w3 = sin((wp.x + wp.y) * 0.55 + animTime * 2.35) * 0.5;
        vec2 grad = vec2(w1 * 0.055 + w3 * 0.04, w2 * 0.055 + w3 * 0.04);
        N = normalize(vec3(-grad.x, 1.0, -grad.y));

        vec3 deepWater = mix(vec3(0.008, 0.065, 0.145), vec3(0.003, 0.012, 0.032), nightFactor);
        vec3 shallowTurquoise = mix(vec3(0.025, 0.220, 0.285), vec3(0.008, 0.035, 0.065), nightFactor);

        float eastNorthSouthDist = max(wp.x - 375.0, abs(wp.y) - 375.0);
        float westBeachEdge = (abs(wp.y) < 352.0) ? (-392.0 - wp.x) : (-375.0 - wp.x);
        float islandDist = max(eastNorthSouthDist, westBeachEdge);
        float shoreProx = 1.0 - clamp(islandDist / 46.0, 0.0, 1.0);
        albedo = mix(deepWater, shallowTurquoise, shoreProx * 0.80);

        // Animated shoreline surf foam (including gentle waves lapping Sunset Beach!)
        if (islandDist > -7.0 && islandDist < 18.0) {
            float surfWave = sin(islandDist * 0.82 - animTime * 2.7 + valueNoise(wp * 0.25) * 3.0);
            float foam = smoothstep(0.52, 0.92, surfWave) * (1.0 - clamp(islandDist / 18.0, 0.0, 1.0));
            albedo = mix(albedo, vec3(0.80, 0.86, 0.90), foam * 0.78);
        }
        roughness = 0.09;
        metallic = 0.05;
    }
    else if (matId == 10) {
        // MAT 10: Lush Multi-Tone Park Lawn & Subsurface-Lit Foliage
        float patchNoise = valueNoise(fragPosition.xz * 0.45);
        float bladeNoise = valueNoise(fragPosition.xz * 7.5 + vec2(fragPosition.y * 3.0));
        albedo *= mix(0.80, 1.22, patchNoise * 0.65 + bladeNoise * 0.35);
        roughness = 0.68;
    }

    // -----------------------------------------------------------------------
    // Pure Emissive Bypass (Headlights, Taillights, Neon Signs, Traffic Signals, Coronas)
    // -----------------------------------------------------------------------
    if (matId == 9) {
        vec3 glow = albedo * (1.35 + emissive * 1.45);
        float dist = length(viewPos - fragPosition);
        float fogF = clamp(1.0 - exp(-max(0.0, dist - 110.0) * 0.0028), 0.0, 0.85);
        vec3 hdrOut = mix(glow, pow(fogColor, vec3(2.2)), fogF * 0.55);
        vec3 mapped = pow(acesTonemap(hdrOut), vec3(1.0 / 2.2));
        // Keep bright emissive peak close to 1.0 so post-process bloom catches it crisply
        mapped = max(mapped, clamp(srgbBase.rgb * (0.92 + colSpecular.z * 0.25), 0.0, 1.0));
        finalColor = vec4(mapped, srgbBase.a);
        return;
    }

    // -----------------------------------------------------------------------
    // Physically-Inspired Direct + Hemisphere + Clearcoat Reflection Shading
    // -----------------------------------------------------------------------
    float ndotl = max(dot(N, L), 0.0);
    if (matId == 10) {
        // Soft foliage wrap lighting so tree crowns look lush and volumetric
        ndotl = max(dot(N, L) * 0.68 + 0.32, 0.0);
    }

    float shadow = computeSoftShadow(fragPosition, N, L);
    float sunVis = 1.0 - shadow * 0.84;

    // Sky-to-ground hemisphere ambient + subtle ground-contact ambient occlusion
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);
    float contactAo = mix(0.74, 1.0, clamp(fragPosition.y / 1.6, 0.0, 1.0));
    if (N.y > 0.75) contactAo = 1.0; // Don't darken horizontal road/sidewalk tops

    // Blinn-Phong / GGX-approximated Specular
    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
    float shininess = mix(220.0, 10.0, roughness * roughness);
    float specNorm = (shininess + 2.0) * 0.045;
    float specFactor = pow(ndoth, shininess) * specNorm * (1.0 - roughness * 0.65);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float ndotv = max(dot(N, V), 0.001);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - ndotv, 5.0);

    vec3 kD = (vec3(1.0) - fresnel) * (1.0 - metallic * 0.82);
    vec3 directLit = (kD * albedo + fresnel * specFactor) * sunColor * ndotl * sunVis;
    vec3 envLit    = albedo * ambient * contactAo;

    // Environment Sky/Horizon Reflection on Glass Towers (3), Ocean (6), Car Clearcoat (7), Car Glass (8)
    if (matId == 3 || matId == 6 || matId == 7 || matId == 8) {
        vec3 R = reflect(-V, N);
        float reflUp = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 envRefl = mix(pow(horizonColor, vec3(2.2)) * 0.95, skyAmbient * 2.15, pow(reflUp, 1.35));

        // Crisp solar reflection highlight on ocean waves, sculpted car hoods, and glass windshields
        float specExp = (matId == 6) ? 180.0 : ((matId == 7) ? mix(128.0, 22.0, clamp(roughness * 1.6, 0.0, 1.0)) : 96.0);
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : 1.42);

        float coatStrength = (matId == 6) ? 0.88 : ((matId == 8) ? 0.82 : ((matId == 7) ? 0.68 : 0.44));
        if (matId == 7) {
            // Gloss metallic clearcoat vs Matte satin custom finish!
            coatStrength *= (1.0 - smoothstep(0.18, 0.52, roughness) * 0.78);
        }
        float coatFresnel = 0.08 + 0.92 * pow(1.0 - ndotv, 4.0);
        envLit += envRefl * coatFresnel * coatStrength;
    }

    vec3 totalLinear = envLit + directLit + emissiveColor * emissive;

    // Smooth exponential coastal aerial perspective (atmospheric haze)
    float dist = length(viewPos - fragPosition);
    float fogFactor = clamp(1.0 - exp(-max(0.0, dist - 95.0) * 0.0030), 0.0, 0.88);
    vec3 linearFog = pow(fogColor, vec3(2.2));
    vec3 fogged = mix(totalLinear, linearFog, fogFactor);

    vec3 mapped = pow(acesTonemap(fogged * 1.14), vec3(1.0 / 2.2));
    finalColor = vec4(mapped, srgbBase.a);
}
)GLSL";

// ---------------------------------------------------------------------------
// 4) Full-Screen Multi-Ring HDR-Threshold Bloom + Cinematic Color Polish Shader
// ---------------------------------------------------------------------------
const char* kPostFragShader = R"GLSL(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform float bloomStrength;
uniform float exposure;
uniform float nitroStrength;
uniform int enableBloom;

out vec4 finalColor;

vec3 extractBright(vec3 c) {
    float peak = max(c.r, max(c.g, c.b));
    float weight = smoothstep(0.76, 0.99, peak);
    return c * weight;
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 texel = 1.0 / max(resolution, vec2(1.0));

    // Subtle edge radial chromatic speed polish during Nitrous boost!
    vec2 centerVec = uv - vec2(0.5);
    float edgeDist = length(centerVec);
    vec3 baseCol;
    if (nitroStrength > 0.01) {
        vec2 shift = centerVec * edgeDist * 0.010 * nitroStrength;
        baseCol.r = texture(texture0, uv + shift).r;
        baseCol.g = texture(texture0, uv).g;
        baseCol.b = texture(texture0, uv - shift).b;
    } else {
        baseCol = texture(texture0, uv).rgb;
    }

    vec3 bloom = vec3(0.0);
    if (enableBloom == 1) {
        // 12-tap dual-ring optical bloom around bright lights, neon signs, sun corona & brake lamps
        const vec2 dirs[6] = vec2[6](
            vec2( 1.000,  0.000),
            vec2( 0.500,  0.866),
            vec2(-0.500,  0.866),
            vec2(-1.000,  0.000),
            vec2(-0.500, -0.866),
            vec2( 0.500, -0.866)
        );
        vec3 acc = extractBright(baseCol) * 1.4;
        float wSum = 1.4;
        for (int i = 0; i < 6; ++i) {
            vec3 s1 = texture(texture0, uv + dirs[i] * texel * 2.6).rgb;
            vec3 s2 = texture(texture0, uv + dirs[i] * texel * 6.8).rgb;
            acc += extractBright(s1) * 1.0 + extractBright(s2) * 0.65;
            wSum += 1.65;
        }
        bloom = (acc / wSum) * bloomStrength;
    }

    vec3 col = baseCol * exposure + bloom;

    // Crisp cinematic S-curve contrast & subtle lens vignette
    col = clamp(col, 0.0, 1.0);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.16);
    float vignette = 1.0 - smoothstep(0.42, 0.96, edgeDist) * 0.14;
    col *= vignette;

    finalColor = vec4(clamp(col, 0.0, 1.0), 1.0) * fragColor;
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

// Crease-aware smooth-normal procedural mesh builder (eliminates flat LEGO facets on cars!)
struct SmoothMeshBuilder {
    std::vector<Vector3> pos;
    std::vector<Vector3> triAreaNorm;
    std::vector<Vector3> triUnitNorm;
    std::vector<Vector2> uv;

    void addTriOutward(Vector3 a, Vector3 b, Vector3 c, Vector3 outwardHint,
                       Vector2 uva = {0.0f, 0.0f}, Vector2 uvb = {1.0f, 0.0f}, Vector2 uvc = {0.5f, 1.0f}) {
        Vector3 cross = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        const float lenSq = Vector3LengthSqr(cross);
        if (lenSq < 1e-11f) {
            return; // Skip degenerate cap sliver
        }
        if (Vector3DotProduct(cross, outwardHint) < 0.0f) {
            std::swap(b, c);
            std::swap(uvb, uvc);
            cross = Vector3Negate(cross);
        }
        const Vector3 unitN = Vector3Scale(cross, 1.0f / std::sqrt(lenSq));
        pos.push_back(a); pos.push_back(b); pos.push_back(c);
        uv.push_back(uva); uv.push_back(uvb); uv.push_back(uvc);
        triAreaNorm.push_back(cross);
        triUnitNorm.push_back(unitN);
    }

    void addQuadOutward(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outwardHint) {
        addTriOutward(a, b, c, outwardHint, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f});
        addTriOutward(a, c, d, outwardHint, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f});
    }

    void addBox(Vector3 center, Vector3 size) {
        const float hx = size.x * 0.5f;
        const float hy = size.y * 0.5f;
        const float hz = size.z * 0.5f;
        const Vector3 p000{center.x - hx, center.y - hy, center.z - hz};
        const Vector3 p001{center.x - hx, center.y - hy, center.z + hz};
        const Vector3 p010{center.x - hx, center.y + hy, center.z - hz};
        const Vector3 p011{center.x - hx, center.y + hy, center.z + hz};
        const Vector3 p100{center.x + hx, center.y - hy, center.z - hz};
        const Vector3 p101{center.x + hx, center.y - hy, center.z + hz};
        const Vector3 p110{center.x + hx, center.y + hy, center.z - hz};
        const Vector3 p111{center.x + hx, center.y + hy, center.z + hz};
        addQuadOutward(p100, p101, p111, p110, { 1.0f,  0.0f,  0.0f});
        addQuadOutward(p001, p000, p010, p011, {-1.0f,  0.0f,  0.0f});
        addQuadOutward(p010, p110, p111, p011, { 0.0f,  1.0f,  0.0f});
        addQuadOutward(p000, p001, p101, p100, { 0.0f, -1.0f,  0.0f});
        addQuadOutward(p001, p011, p111, p101, { 0.0f,  0.0f,  1.0f});
        addQuadOutward(p100, p110, p010, p000, { 0.0f,  0.0f, -1.0f});
    }

    Mesh uploadSmooth(float creaseAngleDeg) const {
        Mesh m{};
        const int vCount = static_cast<int>(pos.size());
        const int triCount = vCount / 3;
        if (vCount == 0) {
            return GenMeshCube(0.01f, 0.01f, 0.01f);
        }

        m.vertexCount = vCount;
        m.triangleCount = triCount;
        m.vertices  = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 3 * sizeof(float))));
        m.normals   = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 3 * sizeof(float))));
        m.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 2 * sizeof(float))));

        const float cosCrease = std::cos(creaseAngleDeg * kDegToRad);

        // Quantize vertex positions to 0.5mm grid so shared edges smooth seamlessly
        struct VKey {
            int x, y, z;
            bool operator==(const VKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };
        auto makeKey = [](const Vector3& v) -> VKey {
            return {
                static_cast<int>(std::lround(v.x * 2000.0f)),
                static_cast<int>(std::lround(v.y * 2000.0f)),
                static_cast<int>(std::lround(v.z * 2000.0f))
            };
        };

        // Compact spatial hash table (prime size 4099)
        constexpr int kBuckets = 4099;
        std::vector<int> head(kBuckets, -1);
        std::vector<int> next(static_cast<std::size_t>(vCount), -1);
        std::vector<VKey> keys(static_cast<std::size_t>(vCount));
        auto hashKey = [](const VKey& k) -> int {
            const unsigned int h = static_cast<unsigned int>(k.x * 73856093) ^
                                   static_cast<unsigned int>(k.y * 19349663) ^
                                   static_cast<unsigned int>(k.z * 83492791);
            return static_cast<int>(h % static_cast<unsigned int>(kBuckets));
        };

        for (int i = 0; i < vCount; ++i) {
            keys[static_cast<std::size_t>(i)] = makeKey(pos[static_cast<std::size_t>(i)]);
            const int b = hashKey(keys[static_cast<std::size_t>(i)]);
            next[static_cast<std::size_t>(i)] = head[static_cast<std::size_t>(b)];
            head[static_cast<std::size_t>(b)] = i;
        }

        for (int i = 0; i < vCount; ++i) {
            const int tIdx = i / 3;
            const Vector3 myUnit = triUnitNorm[static_cast<std::size_t>(tIdx)];
            const VKey& myKey = keys[static_cast<std::size_t>(i)];
            const int b = hashKey(myKey);

            Vector3 acc{0.0f, 0.0f, 0.0f};
            for (int cur = head[static_cast<std::size_t>(b)]; cur != -1; cur = next[static_cast<std::size_t>(cur)]) {
                if (keys[static_cast<std::size_t>(cur)] == myKey) {
                    const int otherTri = cur / 3;
                    // Avoid counting the same triangle twice if two vertices quantize together
                    if (otherTri == tIdx && cur != i) continue;
                    if (Vector3DotProduct(myUnit, triUnitNorm[static_cast<std::size_t>(otherTri)]) >= cosCrease) {
                        acc = Vector3Add(acc, triAreaNorm[static_cast<std::size_t>(otherTri)]);
                    }
                }
            }
            const float accLen = Vector3Length(acc);
            const Vector3 finalN = (accLen > 1e-8f) ? Vector3Scale(acc, 1.0f / accLen) : myUnit;

            m.vertices[i * 3 + 0]  = pos[static_cast<std::size_t>(i)].x;
            m.vertices[i * 3 + 1]  = pos[static_cast<std::size_t>(i)].y;
            m.vertices[i * 3 + 2]  = pos[static_cast<std::size_t>(i)].z;
            m.normals[i * 3 + 0]   = finalN.x;
            m.normals[i * 3 + 1]   = finalN.y;
            m.normals[i * 3 + 2]   = finalN.z;
            m.texcoords[i * 2 + 0] = uv[static_cast<std::size_t>(i)].x;
            m.texcoords[i * 2 + 1] = uv[static_cast<std::size_t>(i)].y;
        }

        UploadMesh(&m, false);
        return m;
    }
};

// Builds a smooth 24-segment rounded-shoulder tire cylinder along Z (radius=1, z in [-0.5, +0.5])
Mesh buildCenteredWheelMesh(int slices) {
    SmoothMeshBuilder b;
    for (int i = 0; i < slices; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * kTwoPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * kTwoPi;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);

        // Bevelled tire tread & sidewall shoulder rings for authentic round rubber tire profile!
        const float rOuter = 1.00f;
        const float rSide  = 0.91f;
        const float zTread = 0.38f;
        const float zSide  = 0.50f;

        b.addQuadOutward({c0 * rOuter, s0 * rOuter, -zTread}, {c1 * rOuter, s1 * rOuter, -zTread},
                         {c1 * rOuter, s1 * rOuter,  zTread}, {c0 * rOuter, s0 * rOuter,  zTread}, {cm, sm, 0.0f});
        b.addQuadOutward({c0 * rOuter, s0 * rOuter,  zTread}, {c1 * rOuter, s1 * rOuter,  zTread},
                         {c1 * rSide,  s1 * rSide,   zSide},  {c0 * rSide,  s0 * rSide,   zSide},  {cm, sm, 0.5f});
        b.addQuadOutward({c0 * rSide,  s0 * rSide,  -zSide},  {c1 * rSide,  s1 * rSide,  -zSide},
                         {c1 * rOuter, s1 * rOuter, -zTread}, {c0 * rOuter, s0 * rOuter, -zTread}, {cm, sm, -0.5f});

        b.addTriOutward({0.0f, 0.0f,  zSide}, {c0 * rSide, s0 * rSide,  zSide}, {c1 * rSide, s1 * rSide,  zSide}, {0.0f, 0.0f,  1.0f});
        b.addTriOutward({0.0f, 0.0f, -zSide}, {c1 * rSide, s1 * rSide, -zSide}, {c0 * rSide, s0 * rSide, -zSide}, {0.0f, 0.0f, -1.0f});
    }
    return b.uploadSmooth(55.0f);
}

// Builds a concave 5-twin-spoke alloy sport wheel rim (radius=1, outer face at +Z)
Mesh buildSportRimMesh() {
    SmoothMeshBuilder b;
    constexpr int kRimSegs = 20;
    const float rLipOuter = 0.72f;
    const float rLipInner = 0.63f;
    const float zFace = 0.505f;
    const float zHub  = 0.435f;

    // Outer machined alloy barrel lip ring (both +Z and -Z so left & right wheels both face outward!)
    for (int side = -1; side <= 1; side += 2) {
        const float sZ = static_cast<float>(side);
        for (int i = 0; i < kRimSegs; ++i) {
            const float a0 = (static_cast<float>(i) / kRimSegs) * kTwoPi;
            const float a1 = (static_cast<float>(i + 1) / kRimSegs) * kTwoPi;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            b.addQuadOutward({c0 * rLipInner, s0 * rLipInner, sZ * zFace},
                             {c1 * rLipInner, s1 * rLipInner, sZ * zFace},
                             {c1 * rLipOuter, s1 * rLipOuter, sZ * (zFace + 0.015f)},
                             {c0 * rLipOuter, s0 * rLipOuter, sZ * (zFace + 0.015f)},
                             {0.0f, 0.0f, sZ});
            // Center hub cap pentagon/circle
            b.addTriOutward({0.0f, 0.0f, sZ * (zHub + 0.02f)},
                            {c0 * 0.21f, s0 * 0.21f, sZ * zHub},
                            {c1 * 0.21f, s1 * 0.21f, sZ * zHub},
                            {0.0f, 0.0f, sZ});
        }

        // 5 Twin-Spoke Star Alloy Arms sloping from concave center hub out to rim lip!
        for (int sp = 0; sp < 5; ++sp) {
            const float baseAng = (static_cast<float>(sp) / 5.0f) * kTwoPi;
            for (int twin = -1; twin <= 1; twin += 2) {
                const float ang = baseAng + static_cast<float>(twin) * 0.145f;
                const float ca = std::cos(ang), sa = std::sin(ang);
                const float px = -sa * 0.042f, py = ca * 0.042f;

                const Vector3 iA{ca * 0.17f - px, sa * 0.17f - py, sZ * zHub};
                const Vector3 iB{ca * 0.17f + px, sa * 0.17f + py, sZ * zHub};
                const Vector3 oB{ca * 0.65f + px * 0.78f, sa * 0.65f + py * 0.78f, sZ * (zFace + 0.010f)};
                const Vector3 oA{ca * 0.65f - px * 0.78f, sa * 0.65f - py * 0.78f, sZ * (zFace + 0.010f)};
                b.addQuadOutward(iA, iB, oB, oA, {0.0f, 0.0f, sZ});
            }
        }
    }
    return b.uploadSmooth(38.0f);
}

// Builds a complete sculpted smooth-normal automotive body, framed curved glass, and wheel-well/aero trim
struct BuiltCarMeshes {
    Mesh body{};
    Mesh glass{};
    Mesh trim{};
};

BuiltCarMeshes buildSculptedCarMeshes(VehicleKind kind) {
    const auto& spec = getVehicleSpec(kind);
    const float halfL = spec.halfLength;
    const float halfW = spec.halfWidth;
    const float H     = spec.chassisHeight;

    // Class-specific authentic automotive proportions & curves
    float groundClear = 0.20f;
    float wheelR      = 0.35f;
    float axleFrac    = 0.63f; // axle x = +-axleFrac * halfL
    float noseTipY    = 0.36f;
    float hoodBaseY   = 0.78f;
    float beltMidY    = 0.84f;
    float tailDeckY   = 0.88f;
    float roofPeakY   = groundClear + H;
    float sRearBase   = -0.68f;
    float sRearTop    = -0.28f;
    float sFrontTop   =  0.14f;
    float sFrontBase  =  0.46f;
    float tumblehome  =  0.76f; // roof width relative to shoulder width
    float fenderFlare =  0.055f;
    float waistPinch  =  0.026f;
    float noseRound   =  0.14f;
    bool isCoupeGlass = false;
    bool hasPanoRoof  = false;

    switch (kind) {
        case VehicleKind::Hypercar:
            groundClear = 0.15f; wheelR = 0.35f; axleFrac = 0.62f;
            noseTipY = 0.27f; hoodBaseY = 0.64f; beltMidY = 0.72f; tailDeckY = 0.80f;
            roofPeakY = 1.13f;
            sRearBase = -0.74f; sRearTop = -0.18f; sFrontTop = 0.22f; sFrontBase = 0.58f;
            tumblehome = 0.70f; fenderFlare = 0.082f; waistPinch = 0.048f; noseRound = 0.22f;
            isCoupeGlass = true; hasPanoRoof = true;
            break;
        case VehicleKind::SportsCoupe:
            groundClear = 0.17f; wheelR = 0.35f; axleFrac = 0.63f;
            noseTipY = 0.32f; hoodBaseY = 0.72f; beltMidY = 0.78f; tailDeckY = 0.86f;
            roofPeakY = 1.24f;
            sRearBase = -0.72f; sRearTop = -0.24f; sFrontTop = 0.14f; sFrontBase = 0.46f;
            tumblehome = 0.73f; fenderFlare = 0.068f; waistPinch = 0.036f; noseRound = 0.18f;
            isCoupeGlass = true; hasPanoRoof = true;
            break;
        case VehicleKind::MuscleCar:
            groundClear = 0.20f; wheelR = 0.36f; axleFrac = 0.64f;
            noseTipY = 0.42f; hoodBaseY = 0.82f; beltMidY = 0.85f; tailDeckY = 0.88f;
            roofPeakY = 1.31f;
            sRearBase = -0.66f; sRearTop = -0.30f; sFrontTop = 0.08f; sFrontBase = 0.36f;
            tumblehome = 0.76f; fenderFlare = 0.064f; waistPinch = 0.028f; noseRound = 0.09f;
            isCoupeGlass = true;
            break;
        case VehicleKind::LuxurySedan:
            groundClear = 0.19f; wheelR = 0.36f; axleFrac = 0.64f;
            noseTipY = 0.40f; hoodBaseY = 0.82f; beltMidY = 0.86f; tailDeckY = 0.90f;
            roofPeakY = 1.38f;
            sRearBase = -0.68f; sRearTop = -0.34f; sFrontTop = 0.16f; sFrontBase = 0.46f;
            tumblehome = 0.78f; fenderFlare = 0.048f; waistPinch = 0.020f; noseRound = 0.14f;
            hasPanoRoof = true;
            break;
        case VehicleKind::Taxi:
            groundClear = 0.20f; wheelR = 0.34f; axleFrac = 0.62f;
            noseTipY = 0.40f; hoodBaseY = 0.80f; beltMidY = 0.84f; tailDeckY = 0.88f;
            roofPeakY = 1.38f;
            sRearBase = -0.66f; sRearTop = -0.34f; sFrontTop = 0.16f; sFrontBase = 0.46f;
            tumblehome = 0.79f; fenderFlare = 0.040f; waistPinch = 0.015f; noseRound = 0.14f;
            break;
        case VehicleKind::Compact:
            groundClear = 0.19f; wheelR = 0.32f; axleFrac = 0.64f;
            noseTipY = 0.38f; hoodBaseY = 0.78f; beltMidY = 0.84f; tailDeckY = 0.92f;
            roofPeakY = 1.38f;
            sRearBase = -0.88f; sRearTop = -0.62f; sFrontTop = 0.16f; sFrontBase = 0.50f;
            tumblehome = 0.80f; fenderFlare = 0.042f; waistPinch = 0.014f; noseRound = 0.16f;
            break;
        case VehicleKind::SUV:
            groundClear = 0.26f; wheelR = 0.40f; axleFrac = 0.63f;
            noseTipY = 0.54f; hoodBaseY = 0.98f; beltMidY = 1.02f; tailDeckY = 1.05f;
            roofPeakY = 1.66f;
            sRearBase = -0.90f; sRearTop = -0.74f; sFrontTop = 0.14f; sFrontBase = 0.44f;
            tumblehome = 0.84f; fenderFlare = 0.062f; waistPinch = 0.012f; noseRound = 0.12f;
            break;
        case VehicleKind::ArmoredSuv:
            groundClear = 0.29f; wheelR = 0.43f; axleFrac = 0.63f;
            noseTipY = 0.62f; hoodBaseY = 1.08f; beltMidY = 1.12f; tailDeckY = 1.14f;
            roofPeakY = 1.78f;
            sRearBase = -0.92f; sRearTop = -0.78f; sFrontTop = 0.14f; sFrontBase = 0.42f;
            tumblehome = 0.86f; fenderFlare = 0.085f; waistPinch = 0.008f; noseRound = 0.08f;
            break;
        case VehicleKind::DeliveryVan:
        default:
            groundClear = 0.24f; wheelR = 0.38f; axleFrac = 0.62f;
            noseTipY = 0.52f; hoodBaseY = 1.02f; beltMidY = 1.06f; tailDeckY = 1.08f;
            roofPeakY = 1.98f;
            sRearBase = -0.96f; sRearTop = -0.90f; sFrontTop = 0.38f; sFrontBase = 0.72f;
            tumblehome = 0.90f; fenderFlare = 0.042f; waistPinch = 0.0f; noseRound = 0.10f;
            break;
    }

    const float axleX = halfL * axleFrac;
    const float archR = wheelR * 1.14f;

    auto smooth01 = [](float t) -> float {
        t = clampf(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    struct StationEval {
        float x = 0.0f;
        float yUnder = 0.2f;
        float ySill  = 0.2f;
        float yBelly = 0.5f;
        float yBelt  = 0.8f;
        float yUpper = 0.9f;
        float yCrownSide = 1.0f;
        float yTopCenter = 1.05f;
        float wUnder = 0.7f;
        float wSill  = 0.85f;
        float wBelly = 0.95f;
        float wBelt  = 0.90f;
        float wUpper = 0.80f;
        float wCrownSide = 0.65f;
    };

    auto evalStation = [&](float s) -> StationEval {
        StationEval st{};
        st.x = s * halfL;

        // 1) Plan-view width curve: aerodynamic curved nose/tail taper + muscular fender flares over wheels!
        float taper = 1.0f;
        if (s > 0.68f) {
            const float u = (s - 0.68f) / 0.32f;
            taper = 1.0f - noseRound * u * u - 0.22f * std::pow(u, 4.0f);
        } else if (s < -0.74f) {
            const float u = (-0.74f - s) / 0.26f;
            taper = 1.0f - 0.09f * u * u - 0.12f * std::pow(u, 4.0f);
        }
        const float dFront = (st.x - axleX) / (wheelR * 1.28f);
        const float dRear  = (st.x + axleX) / (wheelR * 1.32f);
        const float flare  = fenderFlare * (std::exp(-dFront * dFront) + 1.06f * std::exp(-dRear * dRear));
        const float waist  = waistPinch * std::exp(-std::pow(s / 0.34f, 2.0f));
        const float wMax   = std::max(0.32f, halfW * taper + flare - waist);

        // 2) Underbody diffuser/splitter curve + circular 3D wheel-arch cutout along outer sill!
        float yBot = groundClear;
        if (s < -0.74f) {
            const float u = (-0.74f - s) / 0.26f;
            yBot += 0.13f * u * u; // Rear diffuser upsweep
        } else if (s > 0.78f) {
            const float u = (s - 0.78f) / 0.22f;
            yBot += 0.05f * u * u; // Front chin clearance
        }

        float archLift = 0.0f;
        for (float ax : {-axleX, axleX}) {
            const float dx = std::abs(st.x - ax);
            if (dx < archR) {
                const float cutY = wheelR + std::sqrt(std::max(0.0f, archR * archR - dx * dx));
                archLift = std::max(archLift, cutY - yBot);
            }
        }

        // 3) Sculpted beltline / fender crest / sloped hood curve
        float beltY = beltMidY;
        if (s >= sFrontBase) {
            const float u = (s - sFrontBase) / std::max(0.05f, 1.0f - sFrontBase);
            // Smooth curved aerodynamic hood sloping into front grille
            beltY = lerpf(hoodBaseY, noseTipY, std::pow(clampf(u, 0.0f, 1.0f), 1.48f));
        } else if (s >= 0.0f) {
            const float u = s / std::max(0.05f, sFrontBase);
            beltY = lerpf(beltMidY, hoodBaseY, smooth01(u));
        } else {
            const float u = (-s) / 1.0f;
            beltY = lerpf(beltMidY, tailDeckY, smooth01(u * 1.15f));
            if (s < -0.86f) {
                const float tu = (-0.86f - s) / 0.14f;
                beltY -= 0.07f * tu * tu; // Rounded rear deck lip
            }
        }
        // Subtle muscular fender haunch crest over front & rear wheels
        beltY += flare * 0.52f;

        // 4) Greenhouse cabin elevation (smooth windshield & fastback curves)
        float cabinFrac = 0.0f;
        if (s > sRearBase && s < sFrontBase) {
            if (s < sRearTop) {
                cabinFrac = smooth01((s - sRearBase) / std::max(0.04f, sRearTop - sRearBase));
            } else if (s <= sFrontTop) {
                const float roofMid = 0.5f * (sRearTop + sFrontTop);
                const float halfSpan = std::max(0.05f, 0.5f * (sFrontTop - sRearTop));
                const float ru = (s - roofMid) / halfSpan;
                cabinFrac = 1.0f + 0.025f * (1.0f - ru * ru); // Subtle longitudinal roof arch!
            } else {
                cabinFrac = smooth01((sFrontBase - s) / std::max(0.04f, sFrontBase - sFrontTop));
            }
        }

        const float ySillVal = yBot + archLift;
        // Guarantee lower body points stay cleanly above wheel arch crest even on low-slung hypercars!
        const float minAboveArch = ySillVal + 0.035f;
        const float finalBeltY = std::max(beltY, minAboveArch + 0.06f);
        const float finalBellyY = std::max(lerpf(yBot, finalBeltY, 0.52f), minAboveArch);

        const float hoodCenterCrown = (kind == VehicleKind::MuscleCar && s > sFrontBase + 0.06f && s < 0.84f)
                                          ? 0.085f
                                          : ((kind == VehicleKind::Hypercar && s < sRearTop && s > -0.82f) ? 0.055f : 0.036f);
        const float deckOrRoofSideY = lerpf(finalBeltY + 0.022f, roofPeakY - 0.038f, clampf(cabinFrac, 0.0f, 1.05f));
        const float deckOrRoofTopY  = lerpf(finalBeltY + hoodCenterCrown, roofPeakY, clampf(cabinFrac, 0.0f, 1.05f));

        st.yUnder     = yBot;
        st.ySill      = ySillVal;
        st.yBelly     = finalBellyY;
        st.yBelt      = finalBeltY;
        st.yUpper     = lerpf(finalBeltY, deckOrRoofSideY, 0.52f);
        st.yCrownSide = deckOrRoofSideY;
        st.yTopCenter = std::max(deckOrRoofSideY + 0.015f, deckOrRoofTopY);

        const float roofNarrow = lerpf(0.86f, tumblehome, clampf(cabinFrac, 0.0f, 1.0f));
        st.wUnder     = wMax * 0.76f;
        st.wSill      = wMax * 0.93f;
        st.wBelly     = wMax;
        st.wBelt      = wMax * 0.94f;
        st.wUpper     = wMax * lerpf(0.91f, roofNarrow + 0.06f, clampf(cabinFrac, 0.0f, 1.0f));
        st.wCrownSide = wMax * roofNarrow * 0.88f;
        return st;
    };

    // Build dense station list capturing bumpers, wheel arches, windshield base, roof rails, and decklid
    std::vector<float> sList;
    for (int i = 0; i <= 32; ++i) {
        sList.push_back(-1.0f + 2.0f * (static_cast<float>(i) / 32.0f));
    }
    const float keyS[] = {
        sRearBase, sRearTop, sFrontTop, sFrontBase,
        (-axleX - archR) / halfL, (-axleX) / halfL, (-axleX + archR) / halfL,
        ( axleX - archR) / halfL, ( axleX) / halfL, ( axleX + archR) / halfL,
        -0.96f, -0.88f, 0.88f, 0.96f
    };
    for (float ks : keyS) {
        if (ks > -0.995f && ks < 0.995f) sList.push_back(ks);
    }
    std::sort(sList.begin(), sList.end());
    std::vector<float> stations;
    for (float sv : sList) {
        if (stations.empty() || (sv - stations.back()) > 0.016f) {
            stations.push_back(sv);
        }
    }
    if (1.0f - stations.back() > 0.005f) stations.push_back(1.0f);
    else stations.back() = 1.0f;

    constexpr int kRingPts = 14;
    const std::size_t nSt = stations.size();
    std::vector<std::array<Vector3, kRingPts>> rings(nSt);
    std::vector<StationEval> evals(nSt);

    for (std::size_t i = 0; i < nSt; ++i) {
        const StationEval e = evalStation(stations[i]);
        evals[i] = e;
        auto& r = rings[i];
        r[0]  = {e.x, e.yUnder,      0.0f};
        r[1]  = {e.x, e.yUnder,      e.wUnder};
        r[2]  = {e.x, e.ySill,       e.wSill};
        r[3]  = {e.x, e.yBelly,      e.wBelly};
        r[4]  = {e.x, e.yBelt,       e.wBelt};
        r[5]  = {e.x, e.yUpper,      e.wUpper};
        r[6]  = {e.x, e.yCrownSide,  e.wCrownSide};
        r[7]  = {e.x, e.yTopCenter,  0.0f};
        r[8]  = {e.x, e.yCrownSide, -e.wCrownSide};
        r[9]  = {e.x, e.yUpper,     -e.wUpper};
        r[10] = {e.x, e.yBelt,      -e.wBelt};
        r[11] = {e.x, e.yBelly,     -e.wBelly};
        r[12] = {e.x, e.ySill,      -e.wSill};
        r[13] = {e.x, e.yUnder,     -e.wUnder};
    }

    SmoothMeshBuilder bodyB;
    SmoothMeshBuilder glassB;
    SmoothMeshBuilder trimB;

    // 1) Loft smooth continuous automotive outer body shell
    for (std::size_t i = 0; i + 1 < nSt; ++i) {
        const float yCore = 0.5f * (0.5f * (evals[i].yUnder + evals[i + 1].yUnder) +
                                    0.5f * (evals[i].yTopCenter + evals[i + 1].yTopCenter));
        const float xBias = (stations[i] > 0.75f) ? 0.25f : ((stations[i] < -0.75f) ? -0.25f : 0.0f);

        for (int k = 0; k < kRingPts; ++k) {
            const int kn = (k + 1) % kRingPts;
            const Vector3 a = rings[i][static_cast<std::size_t>(k)];
            const Vector3 b = rings[i + 1][static_cast<std::size_t>(k)];
            const Vector3 c = rings[i + 1][static_cast<std::size_t>(kn)];
            const Vector3 d = rings[i][static_cast<std::size_t>(kn)];
            const Vector3 qMid{
                0.25f * (a.x + b.x + c.x + d.x),
                0.25f * (a.y + b.y + c.y + d.y),
                0.25f * (a.z + b.z + c.z + d.z)
            };
            Vector3 hint{xBias, qMid.y - yCore, qMid.z};
            if (k == 0 || k == 13) hint = {xBias, -1.0f, 0.0f};
            if (k == 6 || k == 7)  hint = {xBias,  1.0f, qMid.z * 0.3f};
            bodyB.addQuadOutward(a, b, c, d, hint);
        }
    }

    // Close rear bumper cap (i = 0, normal -X) and front nose cap (i = nSt - 1, normal +X)
    {
        const auto& rRear = rings.front();
        const Vector3 cRear{evals.front().x, 0.5f * (evals.front().yUnder + evals.front().yTopCenter), 0.0f};
        for (int k = 0; k < kRingPts; ++k) {
            bodyB.addTriOutward(cRear, rRear[static_cast<std::size_t>(k)],
                                rRear[static_cast<std::size_t>((k + 1) % kRingPts)], {-1.0f, 0.0f, 0.0f});
        }
        const auto& rFront = rings.back();
        const Vector3 cFront{evals.back().x, 0.5f * (evals.back().yUnder + evals.back().yTopCenter), 0.0f};
        for (int k = 0; k < kRingPts; ++k) {
            bodyB.addTriOutward(cFront, rFront[static_cast<std::size_t>(k)],
                                rFront[static_cast<std::size_t>((k + 1) % kRingPts)], {1.0f, 0.0f, 0.0f});
        }
    }

    // 2) Framed Curved 3D Windshields & Side Windows (leaves painted A/B/C pillars & painted roof!)
    // Front curved windshield
    {
        constexpr int kWsRows = 4;
        constexpr int kWsCols = 6;
        const float s0 = sFrontTop + 0.025f;
        const float s1 = sFrontBase - 0.022f;
        auto wsPoint = [&](float uRow, float vCol) -> Vector3 {
            const float s = lerpf(s0, s1, uRow);
            const StationEval e = evalStation(s);
            const float zFrac = -0.84f + 1.68f * vCol; // Leaves painted A-pillars at +-0.84..1.0!
            const float absZ = std::abs(zFrac);
            const float z = zFrac * lerpf(e.wCrownSide * 1.03f, e.wUpper * 0.96f, uRow);
            const float yCurve = lerpf(e.yTopCenter, e.yCrownSide, absZ * absZ);
            return {e.x + 0.010f, yCurve + 0.009f, z};
        };
        for (int r = 0; r < kWsRows; ++r) {
            for (int c = 0; c < kWsCols; ++c) {
                const float u0 = static_cast<float>(r) / kWsRows;
                const float u1 = static_cast<float>(r + 1) / kWsRows;
                const float v0 = static_cast<float>(c) / kWsCols;
                const float v1 = static_cast<float>(c + 1) / kWsCols;
                glassB.addQuadOutward(wsPoint(u0, v0), wsPoint(u1, v0), wsPoint(u1, v1), wsPoint(u0, v1),
                                      {0.65f, 0.75f, 0.0f});
            }
        }
    }

    // Rear curved backlight window (except solid rear cargo door on DeliveryVan)
    if (kind != VehicleKind::DeliveryVan) {
        constexpr int kRwRows = 3;
        constexpr int kRwCols = 6;
        const float s0 = sRearBase + 0.028f;
        const float s1 = sRearTop - 0.022f;
        auto rwPoint = [&](float uRow, float vCol) -> Vector3 {
            const float s = lerpf(s0, s1, uRow);
            const StationEval e = evalStation(s);
            const float zFrac = -0.80f + 1.60f * vCol; // Leaves painted C-pillars / flying buttresses!
            const float absZ = std::abs(zFrac);
            const float z = zFrac * lerpf(e.wUpper * 0.94f, e.wCrownSide * 1.02f, uRow);
            const float yCurve = lerpf(e.yTopCenter, e.yCrownSide, absZ * absZ);
            return {e.x - 0.010f, yCurve + 0.009f, z};
        };
        for (int r = 0; r < kRwRows; ++r) {
            for (int c = 0; c < kRwCols; ++c) {
                const float u0 = static_cast<float>(r) / kRwRows;
                const float u1 = static_cast<float>(r + 1) / kRwRows;
                const float v0 = static_cast<float>(c) / kRwCols;
                const float v1 = static_cast<float>(c + 1) / kRwCols;
                glassB.addQuadOutward(rwPoint(u0, v0), rwPoint(u1, v0), rwPoint(u1, v1), rwPoint(u0, v1),
                                      {-0.65f, 0.75f, 0.0f});
            }
        }
    }

    // Left & Right Side Windows (framed below roof rail and above shoulder beltline!)
    {
        const float swStart = (kind == VehicleKind::DeliveryVan) ? 0.14f : (sRearBase + 0.08f);
        const float swEnd   = sFrontBase - 0.06f;
        const float bPillarS = 0.5f * (swStart + swEnd) + 0.03f;
        constexpr int kSideSegs = 8;

        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            for (int seg = 0; seg < kSideSegs; ++seg) {
                const float sa = lerpf(swStart, swEnd, static_cast<float>(seg) / kSideSegs);
                const float sb = lerpf(swStart, swEnd, static_cast<float>(seg + 1) / kSideSegs);
                // Leave a crisp painted B-pillar gap on 4-door Sedans & SUVs!
                if (!isCoupeGlass && kind != VehicleKind::DeliveryVan &&
                    std::abs(0.5f * (sa + sb) - bPillarS) < (swEnd - swStart) * 0.065f) {
                    continue;
                }
                const StationEval ea = evalStation(sa);
                const StationEval eb = evalStation(sb);
                if (ea.yCrownSide - ea.yBelt < 0.09f && eb.yCrownSide - eb.yBelt < 0.09f) continue;

                const float yaMid = lerpf(ea.yBelt, ea.yCrownSide, 0.55f);
                const float ybMid = lerpf(eb.yBelt, eb.yCrownSide, 0.55f);

                const Vector3 aBot{ea.x, ea.yBelt + 0.036f,      sgn * (lerpf(ea.wBelt, ea.wUpper, 0.08f) + 0.018f)};
                const Vector3 bBot{eb.x, eb.yBelt + 0.036f,      sgn * (lerpf(eb.wBelt, eb.wUpper, 0.08f) + 0.018f)};
                const Vector3 aMid{ea.x, yaMid,                  sgn * (ea.wUpper + 0.018f)};
                const Vector3 bMid{eb.x, ybMid,                  sgn * (eb.wUpper + 0.018f)};
                const Vector3 bTop{eb.x, eb.yCrownSide - 0.032f, sgn * (lerpf(eb.wCrownSide, eb.wUpper, 0.10f) + 0.018f)};
                const Vector3 aTop{ea.x, ea.yCrownSide - 0.032f, sgn * (lerpf(ea.wCrownSide, ea.wUpper, 0.10f) + 0.018f)};
                glassB.addQuadOutward(aBot, bBot, bMid, aMid, {0.0f, 0.25f, sgn});
                glassB.addQuadOutward(aMid, bMid, bTop, aTop, {0.0f, 0.45f, sgn});
            }
        }
    }

    // Panoramic tinted glass roof on Hypercar, SportsCoupe & LuxurySedan
    if (hasPanoRoof) {
        const float p0 = lerpf(sRearTop, sFrontTop, 0.16f);
        const float p1 = lerpf(sRearTop, sFrontTop, 0.84f);
        for (int r = 0; r < 3; ++r) {
            const StationEval ea = evalStation(lerpf(p0, p1, static_cast<float>(r) / 3.0f));
            const StationEval eb = evalStation(lerpf(p0, p1, static_cast<float>(r + 1) / 3.0f));
            const float wa = ea.wCrownSide * 0.60f;
            const float wb = eb.wCrownSide * 0.60f;
            glassB.addQuadOutward({ea.x, ea.yTopCenter + 0.018f, -wa},
                                  {eb.x, eb.yTopCenter + 0.018f, -wb},
                                  {eb.x, eb.yTopCenter + 0.018f,  wb},
                                  {ea.x, ea.yTopCenter + 0.018f,  wa},
                                  {0.0f, 1.0f, 0.0f});
        }
    }

    // 3) Dark Wheel-House Liners, Front Splitter, Grille, Side Skirts & Rear Diffuser (trim)
    constexpr int kArchSegs = 12;
    for (int side = -1; side <= 1; side += 2) {
        const float sgn = static_cast<float>(side);
        const float zOuter = halfW * 0.95f;
        const float zInner = halfW * 0.56f;
        for (float ax : {-axleX, axleX}) {
            for (int a = 0; a < kArchSegs; ++a) {
                const float th0 = (static_cast<float>(a) / kArchSegs) * kPi;
                const float th1 = (static_cast<float>(a + 1) / kArchSegs) * kPi;
                const float x0 = ax + std::cos(th0) * (archR * 0.98f);
                const float y0 = wheelR + std::sin(th0) * (archR * 0.98f);
                const float x1 = ax + std::cos(th1) * (archR * 0.98f);
                const float y1 = wheelR + std::sin(th1) * (archR * 0.98f);

                // Cylindrical wheel-well arch liner (faces radially inward toward the tire)
                trimB.addQuadOutward({x0, y0, sgn * zInner}, {x1, y1, sgn * zInner},
                                     {x1, y1, sgn * zOuter}, {x0, y0, sgn * zOuter},
                                     {ax - 0.5f * (x0 + x1), wheelR - 0.5f * (y0 + y1), sgn * 0.25f});
                // Dark inner chassis backing wall behind tire so there is zero see-through gap!
                trimB.addTriOutward({ax, wheelR * 0.65f, sgn * zInner},
                                    {x0, y0, sgn * zInner},
                                    {x1, y1, sgn * zInner},
                                    {0.0f, 0.0f, sgn});
            }
        }

        // Sculpted carbon/satin side rocker skirt between front & rear wheel arches
        const float skirtHalfL = axleX - archR * 1.02f;
        if (skirtHalfL > 0.15f) {
            trimB.addBox({0.0f, groundClear + 0.035f, sgn * (halfW * 0.93f)},
                         {skirtHalfL * 2.0f, 0.075f, 0.09f});
        }
    }

    // Front aerodynamic chin splitter, intake grille & rear lower diffuser fins
    {
        const StationEval ef = evalStation(0.97f);
        const StationEval er = evalStation(-0.97f);
        trimB.addBox({halfL * 0.96f, ef.yUnder + 0.02f, 0.0f}, {0.18f, 0.045f, ef.wBelly * 1.72f});
        trimB.addBox({halfL * 0.985f, ef.yUnder + 0.15f, 0.0f}, {0.06f, 0.19f, ef.wBelly * 1.12f});
        trimB.addBox({-halfL * 0.975f, er.yUnder + 0.06f, 0.0f}, {0.12f, 0.13f, er.wBelly * 1.48f});

        // Diffuser vertical strakes on performance cars
        if (kind == VehicleKind::Hypercar || kind == VehicleKind::SportsCoupe || kind == VehicleKind::MuscleCar) {
            for (int f = -2; f <= 2; ++f) {
                if (f == 0) continue;
                trimB.addBox({-halfL * 0.99f, er.yUnder + 0.06f, static_cast<float>(f) * (halfW * 0.20f)},
                             {0.10f, 0.12f, 0.025f});
            }
        }
        // MuscleCar supercharger intake scoop
        if (kind == VehicleKind::MuscleCar) {
            const StationEval em = evalStation(0.62f);
            trimB.addBox({em.x, em.yTopCenter + 0.05f, 0.0f}, {0.68f, 0.11f, 0.46f});
        }
        // Hypercar side mid-engine air intakes behind doors
        if (kind == VehicleKind::Hypercar) {
            const StationEval eh = evalStation(-0.22f);
            trimB.addBox({eh.x, eh.yBelly + 0.12f,  eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
            trimB.addBox({eh.x, eh.yBelly + 0.12f, -eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
        }
        // ArmoredSuv heavy steel bullbar & tactical roof rack
        if (kind == VehicleKind::ArmoredSuv) {
            trimB.addBox({halfL + 0.07f, 0.56f, 0.0f}, {0.10f, 0.44f, halfW * 1.45f});
            trimB.addBox({-0.25f, roofPeakY + 0.055f,  halfW * 0.72f}, {halfL * 1.18f, 0.06f, 0.07f});
            trimB.addBox({-0.25f, roofPeakY + 0.055f, -halfW * 0.72f}, {halfL * 1.18f, 0.06f, 0.07f});
        } else if (kind == VehicleKind::SUV) {
            trimB.addBox({-0.22f, roofPeakY + 0.04f,  halfW * 0.68f}, {halfL * 1.10f, 0.045f, 0.05f});
            trimB.addBox({-0.22f, roofPeakY + 0.04f, -halfW * 0.68f}, {halfL * 1.10f, 0.045f, 0.05f});
        }
    }

    BuiltCarMeshes out{};
    out.body  = bodyB.uploadSmooth(48.0f);
    out.glass = glassB.uploadSmooth(48.0f);
    out.trim  = trimB.uploadSmooth(42.0f);
    return out;
}

RenderTexture2D createShadowMapFbo(int width, int height, bool& outValid) {
    RenderTexture2D target{};
    outValid = false;
    target.id = rlLoadFramebuffer();
    target.texture.width = width;
    target.texture.height = height;
    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;
        target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        outValid = rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
    }
    return target;
}

} // namespace

Renderer3D::~Renderer3D() {
    shutdown();
}

void Renderer3D::init() {
    // 1) World PBR-lite + PCF shadow shader
    worldShader_ = LoadShaderFromMemory(kWorldVertShader, kWorldFragShader);
    locLightDir_      = GetShaderLocation(worldShader_, "lightDir");
    locSunColor_      = GetShaderLocation(worldShader_, "sunColor");
    locSkyAmbient_    = GetShaderLocation(worldShader_, "skyAmbient");
    locGroundAmbient_ = GetShaderLocation(worldShader_, "groundAmbient");
    locFogColor_      = GetShaderLocation(worldShader_, "fogColor");
    locHorizonColor_  = GetShaderLocation(worldShader_, "horizonColor");
    locViewPos_       = GetShaderLocation(worldShader_, "viewPos");
    locAnimTime_      = GetShaderLocation(worldShader_, "animTime");
    locNightFactor_   = GetShaderLocation(worldShader_, "nightFactor");
    locEnableShadows_ = GetShaderLocation(worldShader_, "enableShadows");
    locLightVP_       = GetShaderLocation(worldShader_, "lightVP");
    locShadowMap_     = GetShaderLocation(worldShader_, "shadowMap");
    locShadowTexel_   = GetShaderLocation(worldShader_, "shadowTexelSize");
    if (worldShader_.locs != nullptr) {
        worldShader_.locs[SHADER_LOC_VECTOR_VIEW]    = locViewPos_;
        worldShader_.locs[SHADER_LOC_COLOR_SPECULAR] = GetShaderLocation(worldShader_, "colSpecular");
    }

    const float shadowTexel = 1.0f / static_cast<float>(kShadowMapResolution);
    SetShaderValue(worldShader_, locShadowTexel_, &shadowTexel, SHADER_UNIFORM_FLOAT);

    // 2) Depth-only shadow pass shader + 2048x2048 depth FBO
    depthShader_ = LoadShaderFromMemory(kDepthVertShader, kDepthFragShader);
    shadowMap_ = createShadowMapFbo(kShadowMapResolution, kShadowMapResolution, shadowMapValid_);

    // 3) Procedural Atmospheric Sky Dome shader
    skyShader_ = LoadShaderFromMemory(kSkyVertShader, kSkyFragShader);
    locSkySunDir_    = GetShaderLocation(skyShader_, "sunDir");
    locSkyTimeHours_ = GetShaderLocation(skyShader_, "timeHours");
    locSkyDayFactor_ = GetShaderLocation(skyShader_, "dayFactor");
    locSkyAnimTime_  = GetShaderLocation(skyShader_, "animTime");

    // 4) Full-screen HDR Bloom + ACES post-process shader
    postShader_ = LoadShaderFromMemory(nullptr, kPostFragShader);
    locPostResolution_    = GetShaderLocation(postShader_, "resolution");
    locPostBloomStrength_ = GetShaderLocation(postShader_, "bloomStrength");
    locPostExposure_      = GetShaderLocation(postShader_, "exposure");
    locPostNitro_         = GetShaderLocation(postShader_, "nitroStrength");
    locPostEnableBloom_   = GetShaderLocation(postShader_, "enableBloom");

    // 5) Shared GPU-resident primitive & 9 sculpted smooth-normal automotive vehicle models
    cubeModel_     = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    cylModel_      = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));
    coneModel_     = LoadModelFromMesh(GenMeshCone(1.0f, 1.0f, 16));
    sphereModel_   = LoadModelFromMesh(GenMeshSphere(1.0f, 14, 16));
    skyDomeModel_  = LoadModelFromMesh(GenMeshSphere(1.0f, 24, 32));
    wheelModel_    = LoadModelFromMesh(buildCenteredWheelMesh(24));
    sportRimModel_ = LoadModelFromMesh(buildSportRimMesh());

    for (std::size_t i = 0; i < carModels_.size(); ++i) {
        const BuiltCarMeshes cm = buildSculptedCarMeshes(static_cast<VehicleKind>(i));
        carModels_[i].body  = LoadModelFromMesh(cm.body);
        carModels_[i].glass = LoadModelFromMesh(cm.glass);
        carModels_[i].trim  = LoadModelFromMesh(cm.trim);
    }

    skyDomeModel_.materials[0].shader = skyShader_;
    setAllModelsShader(worldShader_);

    ensureSceneRenderTarget(std::max(1, GetScreenWidth()), std::max(1, GetScreenHeight()));
    initialized_ = true;
}

void Renderer3D::shutdown() {
    if (!initialized_) return;

    UnloadModel(cubeModel_);
    UnloadModel(cylModel_);
    UnloadModel(coneModel_);
    UnloadModel(sphereModel_);
    UnloadModel(skyDomeModel_);
    UnloadModel(wheelModel_);
    UnloadModel(sportRimModel_);
    for (auto& cm : carModels_) {
        UnloadModel(cm.body);
        UnloadModel(cm.glass);
        UnloadModel(cm.trim);
    }

    if (sceneTarget_.id > 0) {
        UnloadRenderTexture(sceneTarget_);
        sceneTarget_ = RenderTexture2D{};
        sceneTargetW_ = 0;
        sceneTargetH_ = 0;
    }
    if (shadowMap_.id > 0) {
        rlUnloadFramebuffer(shadowMap_.id);
        shadowMap_ = RenderTexture2D{};
        shadowMapValid_ = false;
    }

    UnloadShader(postShader_);
    UnloadShader(skyShader_);
    UnloadShader(depthShader_);
    UnloadShader(worldShader_);
    initialized_ = false;
}

void Renderer3D::cycleQuality() {
    const int next = (static_cast<int>(quality_) + 1) % 3;
    quality_ = static_cast<GraphicsQuality>(next);
}

const char* Renderer3D::qualityNameEn() const {
    switch (quality_) {
        case GraphicsQuality::Ultra:       return "ULTRA (Shadows + HDR Bloom)";
        case GraphicsQuality::High:        return "HIGH (Real-Time Shadows)";
        case GraphicsQuality::Performance: return "PERFORMANCE (Max FPS)";
    }
    return "ULTRA";
}

const char* Renderer3D::qualityNameKo() const {
    switch (quality_) {
        case GraphicsQuality::Ultra:       return "울트라 (실시간 그림자 + 블룸)";
        case GraphicsQuality::High:        return "높음 (실시간 그림자)";
        case GraphicsQuality::Performance: return "고성능 (최대 프레임)";
    }
    return "울트라";
}

void Renderer3D::ensureSceneRenderTarget(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (sceneTarget_.id > 0 && sceneTargetW_ == width && sceneTargetH_ == height) {
        return;
    }
    if (sceneTarget_.id > 0) {
        UnloadRenderTexture(sceneTarget_);
    }
    sceneTarget_ = LoadRenderTexture(width, height);
    SetTextureFilter(sceneTarget_.texture, TEXTURE_FILTER_BILINEAR);
    sceneTargetW_ = width;
    sceneTargetH_ = height;
}

void Renderer3D::setAllModelsShader(const Shader& shader) {
    cubeModel_.materials[0].shader     = shader;
    cylModel_.materials[0].shader      = shader;
    coneModel_.materials[0].shader     = shader;
    sphereModel_.materials[0].shader   = shader;
    wheelModel_.materials[0].shader    = shader;
    sportRimModel_.materials[0].shader = shader;
    for (auto& cm : carModels_) {
        cm.body.materials[0].shader  = shader;
        cm.glass.materials[0].shader = shader;
        cm.trim.materials[0].shader  = shader;
    }
}

void Renderer3D::drawModelStyled(Model& model, Vector3 pos, Vector3 rotAxis, float rotDeg,
                                 Vector3 scale, Color tint, float roughness, float metallic,
                                 float emissive, MatKind matKind) {
    if (!shadowPassActive_) {
        Material& mat = model.materials[0];
        mat.maps[MATERIAL_MAP_SPECULAR].color = Color{
            static_cast<unsigned char>(clampf(roughness * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(clampf(metallic * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(clampf(emissive * 64.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(matKind)
        };
    }
    DrawModelEx(model, pos, rotAxis, rotDeg, scale, tint);
}

Color Renderer3D::computeSkyColor(const GameWorld& world) const {
    const float h = world.timeOfDayHours();
    if (h >= 7.5f && h < 16.8f) {
        return Color{114, 176, 238, 255};
    }
    if (h >= 16.8f && h < 19.5f) {
        const float t = (h - 16.8f) / 2.7f;
        if (t < 0.5f) {
            const float k = t * 2.0f;
            return Color{
                static_cast<unsigned char>(lerpf(114.0f, 238.0f, k)),
                static_cast<unsigned char>(lerpf(176.0f, 122.0f, k)),
                static_cast<unsigned char>(lerpf(238.0f, 96.0f, k)),
                255
            };
        }
        const float k = (t - 0.5f) * 2.0f;
        return Color{
            static_cast<unsigned char>(lerpf(238.0f, 22.0f, k)),
            static_cast<unsigned char>(lerpf(122.0f, 28.0f, k)),
            static_cast<unsigned char>(lerpf(96.0f, 54.0f, k)),
            255
        };
    }
    if (h >= 5.2f && h < 7.5f) {
        const float k = (h - 5.2f) / 2.3f;
        return Color{
            static_cast<unsigned char>(lerpf(22.0f, 114.0f, k)),
            static_cast<unsigned char>(lerpf(28.0f, 176.0f, k)),
            static_cast<unsigned char>(lerpf(54.0f, 238.0f, k)),
            255
        };
    }
    return Color{18, 26, 48, 255};
}

Vector3 Renderer3D::computeSunDirection(const GameWorld& world) const {
    // Returns unit vector pointing DOWNWARD in the direction light rays travel
    const float h = world.timeOfDayHours();
    const float dayAngle = ((h - 6.0f) / 12.0f) * kPi; // 0 at 06:00, pi/2 at noon, pi at 18:00
    if (!world.isNightTime()) {
        const float elev = std::max(0.32f, std::sin(clampf(dayAngle, 0.08f, kPi - 0.08f)) * 0.86f);
        const float azimX = -std::cos(clampf(dayAngle, 0.08f, kPi - 0.08f)) * 0.78f;
        return Vector3Normalize({azimX, -elev, -0.44f});
    }
    // Crisp cool moonlight angle at night
    return Vector3Normalize({0.46f, -0.72f, -0.52f});
}

Camera3D Renderer3D::computeLightCamera(const Vector3& lightDir, const Vec2& focus) const {
    constexpr float kOrthoSpan = 196.0f;
    const Vector3 toLight = Vector3Negate(lightDir);
    const Vector3 upRef{0.0f, 1.0f, 0.0f};
    const Vector3 lightRight = Vector3Normalize(Vector3CrossProduct(upRef, toLight));
    const Vector3 lightUp    = Vector3Normalize(Vector3CrossProduct(toLight, lightRight));

    // Snap orthographic shadow frustum center to shadow-map texel grid to prevent edge swimming
    Vector3 center{focus.x, 0.0f, focus.z};
    const float worldTexel = kOrthoSpan / static_cast<float>(kShadowMapResolution);
    const float rx = std::round(Vector3DotProduct(center, lightRight) / worldTexel) * worldTexel;
    const float ry = std::round(Vector3DotProduct(center, lightUp) / worldTexel) * worldTexel;
    const float rz = Vector3DotProduct(center, toLight);

    const Vector3 snappedCenter = Vector3Add(
        Vector3Add(Vector3Scale(lightRight, rx), Vector3Scale(lightUp, ry)),
        Vector3Scale(toLight, rz));

    Camera3D lightCam{};
    lightCam.target     = snappedCenter;
    lightCam.position   = Vector3Add(snappedCenter, Vector3Scale(toLight, 195.0f));
    lightCam.up         = {0.0f, 1.0f, 0.0f};
    lightCam.fovy       = kOrthoSpan;
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    return lightCam;
}

void Renderer3D::updateShaderUniforms(const GameWorld& world, const Camera3D& camera,
                                      Color skyCol, const Vector3& lightDir,
                                      const Matrix& lightVP, bool shadowsActive) {
    const float day = world.daylightFactor();
    const float h = world.timeOfDayHours();
    const float golden = clampf(
        std::exp(-std::pow((h - 6.4f) / 1.1f, 2.0f)) +
        std::exp(-std::pow((h - 18.0f) / 1.3f, 2.0f)), 0.0f, 1.0f);
    const float nightFactor = clampf(1.0f - day * 1.18f, 0.0f, 1.0f);

    float sunCol[3] = {
        lerpf(0.18f, 1.48f, day),
        lerpf(0.24f, 1.38f, day),
        lerpf(0.42f, 1.24f, day)
    };
    if (golden > 0.05f) {
        sunCol[0] = lerpf(sunCol[0], 1.72f, golden * 0.85f);
        sunCol[1] = lerpf(sunCol[1], 0.82f, golden * 0.85f);
        sunCol[2] = lerpf(sunCol[2], 0.36f, golden * 0.85f);
    }

    const float skyAmb[3] = {
        lerpf(0.055f, 0.28f, day) + golden * 0.08f,
        lerpf(0.075f, 0.36f, day),
        lerpf(0.150f, 0.52f, day)
    };
    const float gndAmb[3] = {
        lerpf(0.030f, 0.16f, day),
        lerpf(0.035f, 0.16f, day),
        lerpf(0.055f, 0.17f, day)
    };
    const float fogCol[3] = {
        static_cast<float>(skyCol.r) / 255.0f,
        static_cast<float>(skyCol.g) / 255.0f,
        static_cast<float>(skyCol.b) / 255.0f
    };
    const float horizCol[3] = {
        clampf(fogCol[0] * 1.08f + golden * 0.18f, 0.0f, 1.0f),
        clampf(fogCol[1] * 1.02f, 0.0f, 1.0f),
        clampf(fogCol[2] * 1.02f, 0.0f, 1.0f)
    };

    const float ldir[3] = {lightDir.x, lightDir.y, lightDir.z};
    const float viewPos[3] = {camera.position.x, camera.position.y, camera.position.z};
    const float animT = static_cast<float>(GetTime());
    const int shadowsInt = shadowsActive ? 1 : 0;

    SetShaderValue(worldShader_, locLightDir_, ldir, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locSunColor_, sunCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locSkyAmbient_, skyAmb, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locGroundAmbient_, gndAmb, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locFogColor_, fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locHorizonColor_, horizCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locViewPos_, viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader_, locAnimTime_, &animT, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader_, locNightFactor_, &nightFactor, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader_, locEnableShadows_, &shadowsInt, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(worldShader_, locLightVP_, lightVP);
}

void Renderer3D::drawSkyDome(const GameWorld& world, const Camera3D& camera, const Vector3& lightDir) {
    const Vector3 sunTowards = Vector3Negate(lightDir);
    const float sdir[3] = {sunTowards.x, sunTowards.y, sunTowards.z};
    const float hours = world.timeOfDayHours();
    const float dayF  = world.daylightFactor();
    const float animT = static_cast<float>(GetTime());

    SetShaderValue(skyShader_, locSkySunDir_, sdir, SHADER_UNIFORM_VEC3);
    SetShaderValue(skyShader_, locSkyTimeHours_, &hours, SHADER_UNIFORM_FLOAT);
    SetShaderValue(skyShader_, locSkyDayFactor_, &dayF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(skyShader_, locSkyAnimTime_, &animT, SHADER_UNIFORM_FLOAT);

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawModelEx(skyDomeModel_, camera.position, {0.0f, 1.0f, 0.0f}, 0.0f,
                {950.0f, 950.0f, 950.0f}, WHITE);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer3D::renderFrame(const GameWorld& world, const Camera3D& camera) {
    const int sw = std::max(1, GetScreenWidth());
    const int sh = std::max(1, GetScreenHeight());
    const Color skyCol = computeSkyColor(world);
    const Vector3 lightDir = computeSunDirection(world);
    const Vec2 focus{camera.target.x, camera.target.z};

    const bool useShadows = (quality_ != GraphicsQuality::Performance) && shadowMapValid_;
    const bool usePostFbo = (quality_ != GraphicsQuality::Performance);

    const double prevNear = rlGetCullDistanceNear();
    const double prevFar  = rlGetCullDistanceFar();

    // -----------------------------------------------------------------------
    // PASS 1: 2048x2048 Real-Time Directional Sun/Moon Shadow Map
    // -----------------------------------------------------------------------
    Matrix lightVP = MatrixIdentity();
    if (useShadows) {
        rlActiveTextureSlot(kShadowTextureSlot);
        rlDisableTexture();
        rlActiveTextureSlot(0);

        setAllModelsShader(depthShader_);
        shadowPassActive_ = true;

        const Camera3D lightCam = computeLightCamera(lightDir, focus);
        rlSetClipPlanes(4.0, 410.0);

        BeginTextureMode(shadowMap_);
        ClearBackground(WHITE);
        BeginMode3D(lightCam);
        const Matrix lightView = rlGetMatrixModelview();
        const Matrix lightProj = rlGetMatrixProjection();
        lightVP = MatrixMultiply(lightView, lightProj);
        drawSceneGeometry(world, camera, true);
        EndMode3D();
        EndTextureMode();

        shadowPassActive_ = false;
        setAllModelsShader(worldShader_);
    }

    // -----------------------------------------------------------------------
    // PASS 2: Sky Dome + PBR-Lite Shaded 3D Open-World City
    // -----------------------------------------------------------------------
    if (usePostFbo) {
        ensureSceneRenderTarget(sw, sh);
        BeginTextureMode(sceneTarget_);
    }

    ClearBackground(skyCol);
    rlSetClipPlanes(0.16, 1750.0);

    updateShaderUniforms(world, camera, skyCol, lightDir, lightVP, useShadows);

    if (useShadows) {
        rlEnableShader(worldShader_.id);
        int slot = kShadowTextureSlot;
        rlActiveTextureSlot(kShadowTextureSlot);
        rlEnableTexture(shadowMap_.depth.id);
        rlSetUniform(locShadowMap_, &slot, SHADER_UNIFORM_INT, 1);
        rlActiveTextureSlot(0);
    }

    BeginMode3D(camera);
    drawSkyDome(world, camera, lightDir);
    drawSceneGeometry(world, camera, false);
    EndMode3D();

    rlSetClipPlanes(prevNear, prevFar);

    // -----------------------------------------------------------------------
    // PASS 3: Full-Screen Multi-Ring HDR Bloom + Cinematic Post-Processing
    // -----------------------------------------------------------------------
    if (usePostFbo) {
        EndTextureMode();

        const float res[2] = {static_cast<float>(sw), static_cast<float>(sh)};
        const float nightBoost = world.isNightTime() ? 0.56f : 0.34f;
        const float exposure = 1.02f;
        const float nitro = (world.playerVehicle() != nullptr && world.playerVehicle()->nitroActive) ? 1.0f : 0.0f;
        const int enableBloom = (quality_ == GraphicsQuality::Ultra) ? 1 : 0;

        SetShaderValue(postShader_, locPostResolution_, res, SHADER_UNIFORM_VEC2);
        SetShaderValue(postShader_, locPostBloomStrength_, &nightBoost, SHADER_UNIFORM_FLOAT);
        SetShaderValue(postShader_, locPostExposure_, &exposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(postShader_, locPostNitro_, &nitro, SHADER_UNIFORM_FLOAT);
        SetShaderValue(postShader_, locPostEnableBloom_, &enableBloom, SHADER_UNIFORM_INT);

        ClearBackground(BLACK);
        BeginShaderMode(postShader_);
        DrawTextureRec(sceneTarget_.texture,
                       Rectangle{0.0f, 0.0f, static_cast<float>(sw), -static_cast<float>(sh)},
                       Vector2{0.0f, 0.0f}, WHITE);
        EndShaderMode();
    }
}

void Renderer3D::renderWorld(const GameWorld& world, const Camera3D& camera) {
    const Color skyCol = computeSkyColor(world);
    const Vector3 lightDir = computeSunDirection(world);
    updateShaderUniforms(world, camera, skyCol, lightDir, MatrixIdentity(), false);
    drawSkyDome(world, camera, lightDir);
    drawSceneGeometry(world, camera, false);
}

void Renderer3D::drawSceneGeometry(const GameWorld& world, const Camera3D& camera, bool shadowPass) {
    const Vec2 focus{camera.target.x, camera.target.z};
    const bool nightMode = world.isNightTime();

    if (!shadowPass) {
        drawOceanAndGround(world);
        drawRoadNetwork(world, focus);
    }

    drawBuildingsAndParks(world, focus, shadowPass);
    drawTreesAndProps(world, focus, shadowPass);

    // Vehicles
    const float vehCull = shadowPass ? (135.0f * 135.0f) : (275.0f * 275.0f);
    for (const auto& v : world.vehicles()) {
        if ((v.pos - focus).lengthSq() > vehCull) continue;
        drawVehicle3D(v, nightMode, shadowPass);
    }

    // Player character when on foot (styled with active boutique 3D outfit!)
    if (world.player().drivingVehicleId < 0) {
        const auto& pl = world.player();
        const auto& outSpec = getOutfitSpec(pl.currentOutfit);
        float gY = world.city().isOnRoad(pl.pos, 0.1f) ? 0.0f : 0.08f;
        if (pl.pos.x < -345.0f) {
            gY = (std::abs(pl.pos.z) < 7.6f) ? 0.24f : 0.02f;
        }
        drawCharacter3D(pl.pos, gY, pl.facingYaw, pl.walkAnimPhase,
                        outSpec.top, outSpec.bottom, {236, 196, 162, 255},
                        !pl.wasted, pl.currentWeapon != WeaponId::Fists,
                        pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                        outSpec.sunglasses, pl.currentOutfit, shadowPass);
    }

    // Pedestrians & Gangsters
    const float npcCull = shadowPass ? (110.0f * 110.0f) : (185.0f * 185.0f);
    for (const auto& npc : world.npcs()) {
        if ((npc.pos - focus).lengthSq() > npcCull) continue;
        float gY = world.city().isOnRoad(npc.pos, 0.1f) ? 0.0f : 0.08f;
        if (npc.pos.x < -345.0f) {
            gY = (std::abs(npc.pos.z) < 7.6f) ? 0.24f : 0.02f;
        }
        drawCharacter3D(npc.pos, gY, npc.facingYaw, npc.animPhase,
                        npc.outfitTop, npc.outfitBottom, npc.skinColor,
                        npc.alive, npc.role == NpcRole::Gangster,
                        WeaponId::Pistol, 0.0f, 0.0f, npc.role == NpcRole::Gangster,
                        OutfitId::StreetCasual, shadowPass);
    }

    if (!shadowPass) {
        drawPoisAndPickups(world);
        drawSkidsTracersParticles(world);
        drawGpsOverheadArrow(world);
    }
}

void Renderer3D::drawOceanAndGround(const GameWorld& world) {
    const bool night = world.isNightTime();

    // Animated coastal ocean water plane (MatKind::OceanWater)
    drawModelStyled(cubeModel_, {0.0f, -1.35f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {2200.0f, 1.0f, 2200.0f}, Color{24, 108, 162, 255},
                    0.08f, 0.05f, 0.0f, MatKind::OceanWater);

    // Golden California sandy beach perimeter + expanded Western Sunset Beach shoreline!
    drawModelStyled(cubeModel_, {0.0f, -0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {CityMap::kHalfCitySpan * 2.08f, 0.88f, CityMap::kHalfCitySpan * 2.08f},
                    Color{224, 204, 158, 255}, 0.88f, 0.0f, 0.0f, MatKind::Standard);

    // Wide walkable Western Sunset Beach sandbar (x in [-392, -344], z in [-348, 348])
    drawModelStyled(cubeModel_, {-368.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {48.0f, 0.48f, 696.0f}, Color{234, 214, 168, 255},
                    0.86f, 0.0f, 0.0f, MatKind::Standard);
    // Damp shoreline wet sand strip meeting the rolling surf
    drawModelStyled(cubeModel_, {-391.5f, -0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {9.0f, 0.36f, 696.0f}, Color{186, 166, 126, 255},
                    0.35f, 0.05f, 0.0f, MatKind::Standard);

    // Coastal promenade concrete seawall apron
    drawModelStyled(cubeModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {CityMap::kHalfCitySpan * 1.97f, 0.42f, CityMap::kHalfCitySpan * 1.97f},
                    Color{166, 170, 176, 255}, 0.78f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

    // Iconic California Blue-and-Wood Lifeguard Tower on Sunset Beach
    {
        const float lx = -372.0f;
        const float lz = -26.0f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cubeModel_, {lx + sx * 1.25f, 1.05f, lz + sz * 1.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 2.10f, 0.18f}, Color{198, 214, 228, 255}, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
        drawModelStyled(cubeModel_, {lx, 2.15f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.6f, 0.18f, 3.6f}, Color{186, 152, 108, 255}, 0.75f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {lx, 3.25f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.75f, 2.10f, 2.75f}, Color{84, 162, 214, 255}, 0.58f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {lx, 3.45f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.84f, 0.68f, 2.84f}, Color{24, 38, 56, 255}, 0.10f, 0.2f, 0.0f, MatKind::CarGlass);
        drawModelStyled(cubeModel_, {lx, 4.42f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.45f, 0.24f, 3.45f}, Color{236, 240, 246, 255}, 0.55f, 0.0f, 0.0f, MatKind::Standard);
    }

    // Sunset Bay Wooden Ocean Pier (x in [-428, -344], z in [-7.6, +7.6])
    {
        const Color woodDeck{148, 108, 72, 255};
        const Color woodBeam{108, 76, 48, 255};
        const Color railWhite{232, 236, 242, 255};

        // Main timber plank deck + dark cross-beams
        drawModelStyled(cubeModel_, {-386.0f, 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {84.0f, 0.22f, 15.2f}, woodDeck, 0.80f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-386.0f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {84.2f, 0.18f, 15.6f}, woodBeam, 0.85f, 0.0f, 0.0f, MatKind::Standard);

        // Timber ocean pilings + balustrades + vintage pier globes
        for (int p = 0; p < 10; ++p) {
            const float px = -350.0f - static_cast<float>(p) * 8.4f;
            for (int side = -1; side <= 1; side += 2) {
                const float pz = static_cast<float>(side) * 7.1f;
                drawModelStyled(cylModel_, {px, -1.55f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.28f, 1.75f, 0.28f}, woodBeam, 0.88f, 0.0f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {px, 0.72f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.22f, 0.96f, 0.22f}, railWhite, 0.58f, 0.0f, 0.0f, MatKind::Standard);

                if (p % 2 == 1) {
                    drawModelStyled(cylModel_, {px, 1.18f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.08f, 2.25f, 0.08f}, Color{54, 58, 66, 255}, 0.35f, 0.7f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {px, 3.52f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.24f, 0.24f, 0.24f}, Color{255, 224, 156, 255},
                                    0.15f, 0.0f, night ? 2.4f : 0.35f, MatKind::EmissiveGlow);
                }
            }
        }
        // Continuous north/south pier safety rails & west end rail
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {-386.0f, 1.12f, static_cast<float>(side) * 7.1f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {83.5f, 0.12f, 0.16f}, railWhite, 0.58f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-386.0f, 0.72f, static_cast<float>(side) * 7.1f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {83.5f, 0.09f, 0.12f}, railWhite, 0.58f, 0.0f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-427.6f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.12f, 14.4f}, railWhite, 0.58f, 0.0f, 0.0f, MatKind::Standard);

        // Pier-end coastal sunset gazebo pavilion
        const float gx = -421.0f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cylModel_, {gx + sx * 3.4f, 0.24f, sz * 3.8f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 3.6f, 0.18f}, railWhite, 0.55f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
        drawModelStyled(coneModel_, {gx, 3.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {6.4f, 2.2f, 6.4f}, Color{44, 138, 192, 255}, 0.52f, 0.1f, 0.0f, MatKind::Standard);
    }
}

void Renderer3D::drawRoadNetwork(const GameWorld& world, const Vec2& focus) {
    const auto& city = world.city();
    const float roadOuterSpan = (city.roadCoord(CityMap::kRoadCount - 1) - city.roadCoord(0)) + CityMap::kRoadWidth;

    // 1) Seamless city-wide asphalt roadbed at y = 0.00m (analytic markings & crosswalks rendered in shader!)
    drawModelStyled(cubeModel_, {0.0f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {roadOuterSpan, 0.08f, roadOuterSpan}, Color{54, 58, 66, 255},
                    0.46f, 0.04f, 0.0f, MatKind::RoadAsphalt);

    // 2) 64 Raised City Block Sidewalk Plazas (8cm physical granite curb + tiled concrete sidewalk!)
    const Color graniteCurb{124, 128, 136, 255};
    const Color sidewalkTile{186, 190, 196, 255};
    const float halfRoad = CityMap::kRoadWidth * 0.5f;

    for (int bz = 0; bz < CityMap::kBlockCount; ++bz) {
        for (int bx = 0; bx < CityMap::kBlockCount; ++bx) {
            const float x0 = city.roadCoord(bx) + halfRoad;
            const float x1 = city.roadCoord(bx + 1) - halfRoad;
            const float z0 = city.roadCoord(bz) + halfRoad;
            const float z1 = city.roadCoord(bz + 1) - halfRoad;
            const Vec2 bc{(x0 + x1) * 0.5f, (z0 + z1) * 0.5f};
            if ((bc - focus).lengthSq() > 380.0f * 380.0f) continue;

            const float bw = x1 - x0;
            const float bd = z1 - z0;

            // Bevelled granite curb rim
            drawModelStyled(cubeModel_, {bc.x, 0.035f, bc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {bw, 0.07f, bd}, graniteCurb, 0.68f, 0.05f, 0.0f, MatKind::Standard);
            // Tiled concrete sidewalk surface (top at y = 0.08m)
            drawModelStyled(cubeModel_, {bc.x, 0.045f, bc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {bw - 0.36f, 0.07f, bd - 0.36f}, sidewalkTile,
                            0.76f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        }
    }

    // 3) Outer perimeter boulevard sidewalks along the 4 coastal ring roads
    const float ringEdge = roadOuterSpan * 0.5f;
    const float outerW = 7.5f;
    const float fullRing = roadOuterSpan + outerW * 2.0f;
    drawModelStyled(cubeModel_, {0.0f, 0.04f, -ringEdge - outerW * 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {fullRing, 0.08f, outerW}, sidewalkTile, 0.76f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    drawModelStyled(cubeModel_, {0.0f, 0.04f,  ringEdge + outerW * 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {fullRing, 0.08f, outerW}, sidewalkTile, 0.76f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    drawModelStyled(cubeModel_, {-ringEdge - outerW * 0.5f, 0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {outerW, 0.08f, roadOuterSpan}, sidewalkTile, 0.76f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    drawModelStyled(cubeModel_, { ringEdge + outerW * 0.5f, 0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {outerW, 0.08f, roadOuterSpan}, sidewalkTile, 0.76f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
}

void Renderer3D::drawBuildingsAndParks(const GameWorld& world, const Vec2& focus, bool shadowPass) {
    const auto& city = world.city();
    const float animT = static_cast<float>(GetTime());
    const bool night = world.isNightTime();

    if (!shadowPass) {
        for (const auto& park : city.parks()) {
            const Vec2 c = park.bounds.center();
            if ((c - focus).lengthSq() > 330.0f * 330.0f) continue;
            const Vec2 he = park.bounds.halfExtents();
            drawModelStyled(cubeModel_, {c.x, 0.09f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.0f, 0.06f, he.z * 2.0f}, toRayColor(park.grassColor, 1.08f),
                            0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
        }

        // Central Park Plaza multi-tier marble fountain & animated water basin
        if (const auto& parks = city.parks(); !parks.empty()) {
            const Vec2 fc = parks.front().bounds.center();
            if ((fc - focus).lengthSq() < 260.0f * 260.0f) {
                drawModelStyled(cylModel_, {fc.x, 0.08f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.8f, 0.55f, 5.8f}, Color{205, 210, 218, 255},
                                0.55f, 0.05f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 0.52f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {4.9f, 0.16f, 4.9f}, Color{42, 182, 238, 255},
                                0.08f, 0.05f, 0.2f, MatKind::OceanWater);
                drawModelStyled(cylModel_, {fc.x, 0.58f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.25f, 2.85f, 1.25f}, Color{220, 224, 232, 255},
                                0.42f, 0.12f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {fc.x, 3.65f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.85f, 0.85f, 0.85f}, Color{135, 228, 255, 255},
                                0.12f, 0.10f, 0.45f, MatKind::EmissiveGlow);
            }
        }
    }

    // Drive-through GLOBE OIL Gas Station Canopies (elevated roof, zero ground blocker!)
    for (const auto& can : city.canopies()) {
        const Vec2 cc = can.bounds.center();
        if ((cc - focus).lengthSq() > 300.0f * 300.0f) continue;
        const Vec2 he = can.bounds.halfExtents();
        const float roofThick = 0.72f;

        // 4 slender steel support columns aligned with pump islands (z = +-3.3m)
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cylModel_, {cc.x + static_cast<float>(sx) * 5.2f, 0.08f, cc.z + static_cast<float>(sz) * 3.3f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, can.clearanceHeight, 0.24f}, Color{210, 216, 224, 255},
                                0.35f, 0.72f, 0.0f, MatKind::Standard);
            }
        }
        // Main overhead canopy roof deck
        drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight + roofThick * 0.5f, cc.z},
                        {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.0f, roofThick, he.z * 2.0f}, Color{236, 240, 246, 255},
                        0.45f, 0.15f, 0.0f, MatKind::Standard);

        if (!shadowPass) {
            // Glowing crimson-orange perimeter brand fascia + under-canopy bright white LED bay lights!
            drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight + roofThick * 0.52f, cc.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.03f, roofThick * 0.44f, he.z * 2.03f}, Color{246, 76, 42, 255},
                            0.15f, 0.0f, night ? 2.2f : 1.1f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight - 0.02f, cc.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.65f, 0.05f, he.z * 1.55f}, Color{245, 250, 255, 255},
                            0.10f, 0.0f, night ? 2.4f : 0.8f, MatKind::EmissiveGlow);
            // Concrete pump island curbs (low 12cm raised islands at dz = +-3.3m)
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cubeModel_, {cc.x, 0.12f, cc.z + static_cast<float>(sz) * 3.3f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {12.6f, 0.12f, 1.35f}, Color{198, 202, 210, 255},
                                0.72f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
    }

    const float bldCull = shadowPass ? (185.0f * 185.0f) : (375.0f * 375.0f);

    for (const auto& b : city.buildings()) {
        const Vec2 c = b.footprint.center();
        const float distSq = (c - focus).lengthSq();
        if (distSq > bldCull) continue;
        const Vec2 he = b.footprint.halfExtents();

        MatKind facadeKind = MatKind::MasonryFacade;
        float rough = 0.65f;
        float metal = 0.08f;
        if (b.style == BuildingStyle::GlassTower || b.style == BuildingStyle::DealershipBuilding) {
            facadeKind = MatKind::GlassSkyscraper;
            rough = 0.14f;
            metal = 0.22f;
        } else if (b.style == BuildingStyle::Warehouse || b.style == BuildingStyle::ShippingStack) {
            facadeKind = MatKind::IndustrialMetal;
            rough = 0.44f;
            metal = 0.45f;
        }

        // Main architectural volume
        drawModelStyled(cubeModel_, {c.x, b.height * 0.5f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.0f, b.height, he.z * 2.0f}, toRayColor(b.primaryColor),
                        rough, metal, 0.0f, facadeKind);

        // Architectural ground podium base trim
        if (!shadowPass && b.height > 14.0f) {
            drawModelStyled(cubeModel_, {c.x, 0.55f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.03f, 0.95f, he.z * 2.03f}, Color{58, 62, 70, 255},
                            0.52f, 0.25f, 0.0f, MatKind::Standard);
        }

        // Crown cornice (glows softly as architectural LED trim on Downtown GlassTowers & Nightclub at night!)
        const bool crownGlow = (!shadowPass && night &&
                                (b.style == BuildingStyle::GlassTower || b.style == BuildingStyle::NightclubBuilding));
        drawModelStyled(cubeModel_, {c.x, b.height + 0.30f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.05f, 0.60f, he.z * 2.05f}, toRayColor(b.accentColor),
                        0.35f, 0.30f, crownGlow ? 1.35f : 0.0f,
                        crownGlow ? MatKind::EmissiveGlow : MatKind::Standard);

        // Rooftop HVAC mechanical unit on mid/high-rise roofs
        if (!shadowPass && b.upperTierHeight <= 0.5f && !b.hasHelipad && b.height > 15.0f && he.x > 6.5f) {
            drawModelStyled(cubeModel_, {c.x + he.x * 0.32f, b.height + 1.15f, c.z - he.z * 0.28f},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {3.4f, 1.6f, 2.8f},
                            Color{116, 122, 130, 255}, 0.62f, 0.35f, 0.0f, MatKind::IndustrialMetal);
        }

        float roofTopY = b.height;

        // Stepped upper skyscraper tier + spire antenna + blinking aviation beacon
        if (b.upperTierHeight > 0.5f) {
            const float uy = b.height + b.upperTierHeight * 0.5f;
            drawModelStyled(cubeModel_, {c.x, uy, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.36f, b.upperTierHeight, he.z * 1.36f}, toRayColor(b.primaryColor),
                            rough, metal, 0.0f, facadeKind);

            roofTopY = b.height + b.upperTierHeight;
            drawModelStyled(cubeModel_, {c.x, roofTopY + 0.22f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.40f, 0.44f, he.z * 1.40f}, toRayColor(b.accentColor),
                            0.35f, 0.30f, crownGlow ? 1.25f : 0.0f,
                            crownGlow ? MatKind::EmissiveGlow : MatKind::Standard);

            if (!b.hasHelipad) {
                drawModelStyled(cylModel_, {c.x, roofTopY, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 7.2f, 0.24f}, Color{205, 212, 224, 255},
                                0.28f, 0.80f, 0.0f, MatKind::Standard);

                if (!shadowPass) {
                    const float blink = 0.5f + 0.5f * std::sin(animT * 3.4f + c.x * 0.13f);
                    if (blink > 0.35f) {
                        drawModelStyled(sphereModel_, {c.x, roofTopY + 7.35f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.34f, 0.34f, 0.34f}, Color{255, 48, 42, 255},
                                        0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                    }
                }
            }
        }

        // Skyscraper VIP Rooftop Helipad (Concrete Pad + Yellow Ring + Geometric 'H' + Green Perimeter Beacons)
        if (!shadowPass && b.hasHelipad) {
            const float padR = std::min(he.x, he.z) * (b.upperTierHeight > 0.5f ? 0.56f : 0.68f);
            if (padR >= 3.2f) {
                const float py = roofTopY + 0.52f;
                drawModelStyled(cylModel_, {c.x, roofTopY + 0.25f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR, 0.32f, padR}, Color{64, 68, 76, 255}, 0.72f, 0.1f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {c.x, py, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR * 0.82f, 0.06f, padR * 0.82f}, Color{245, 202, 52, 255},
                                0.35f, 0.0f, night ? 0.9f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cylModel_, {c.x, py + 0.02f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR * 0.70f, 0.06f, padR * 0.70f}, Color{52, 56, 64, 255},
                                0.72f, 0.1f, 0.0f, MatKind::Standard);
                // Crisp geometric 'H' landing emblem (zero font glyphs!)
                const float hSpan = padR * 0.36f;
                drawModelStyled(cubeModel_, {c.x - hSpan * 0.55f, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 0.28f, 0.04f, hSpan * 1.35f}, WHITE, 0.3f, 0.0f, 0.8f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {c.x + hSpan * 0.55f, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 0.28f, 0.04f, hSpan * 1.35f}, WHITE, 0.3f, 0.0f, 0.8f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {c.x, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 1.15f, 0.04f, hSpan * 0.26f}, WHITE, 0.3f, 0.0f, 0.8f, MatKind::EmissiveGlow);
                for (int k = 0; k < 8; ++k) {
                    const float a = (static_cast<float>(k) / 8.0f) * kTwoPi;
                    drawModelStyled(sphereModel_,
                                    {c.x + std::cos(a) * (padR * 0.92f), py + 0.10f, c.z + std::sin(a) * (padR * 0.92f)},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.16f, 0.16f, 0.16f},
                                    Color{58, 252, 118, 255}, 0.1f, 0.0f, 2.0f, MatKind::EmissiveGlow);
                }
            }
        }

        // Realistic Street-Level Detail (within 215m so close & mid streets look richly alive!)
        if (!shadowPass && distSq < 215.0f * 215.0f) {
            // 1) Ground-Floor Retail Storefronts (Display Glass, Lit Interior, Entry Doors & 3D Projecting Awnings)
            if (b.hasStorefront && he.x >= 5.2f) {
                const Color awnCol = toRayColor(b.awningColor);
                const float shopW = std::min(he.x * 1.62f, 13.5f);
                for (int side = -1; side <= 1; side += 2) {
                    const float fz = (side < 0) ? (b.footprint.min.z - 0.06f) : (b.footprint.max.z + 0.06f);
                    const float outZ = static_cast<float>(side);

                    // Dark anodized storefront frame + warm lit interior display window
                    drawModelStyled(cubeModel_, {c.x, 1.65f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.36f, 3.05f, 0.14f}, Color{30, 34, 42, 255},
                                    0.40f, 0.55f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {c.x, 1.62f, fz + outZ * 0.05f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW, 2.55f, 0.08f},
                                    night ? Color{255, 218, 152, 255} : Color{32, 48, 66, 255},
                                    0.10f, 0.20f, night ? 1.25f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::CarGlass);
                    // Framed double entrance door in center
                    drawModelStyled(cubeModel_, {c.x, 1.22f, fz + outZ * 0.09f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.85f, 2.25f, 0.10f}, Color{24, 28, 34, 255},
                                    0.35f, 0.65f, 0.0f, MatKind::Standard);
                    // Sloped 3D fabric/metal shop awning + crisp white front valence stripe
                    rlPushMatrix();
                    rlTranslatef(c.x, 3.22f, fz + outZ * 0.62f);
                    rlRotatef(outZ * 18.0f, 1.0f, 0.0f, 0.0f);
                    drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.40f, 0.14f, 1.32f}, awnCol,
                                    0.68f, 0.05f, 0.0f, MatKind::Standard);
                    rlPopMatrix();
                    drawModelStyled(cubeModel_, {c.x, 3.00f, fz + outZ * 1.24f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.42f, 0.16f, 0.06f}, Color{242, 244, 248, 255},
                                    0.55f, 0.0f, 0.0f, MatKind::Standard);
                }
            }

            // 2) 3D Residential Apartment Balconies (Concrete Slab + Smoked-Glass/Metal Railing per Floor)
            if (b.hasBalconies && b.height >= 12.0f && he.x >= 5.0f) {
                const int maxFloors = std::min(7, static_cast<int>((b.height - 4.2f) / 3.4f));
                const int cols = (he.x > 8.5f) ? 2 : 1;
                for (int side = -1; side <= 1; side += 2) {
                    const float bz = (side < 0) ? (b.footprint.min.z - 0.55f) : (b.footprint.max.z + 0.55f);
                    const float railZ = bz + static_cast<float>(side) * 0.50f;
                    for (int fl = 0; fl < maxFloors; ++fl) {
                        const float fy = 4.35f + static_cast<float>(fl) * 3.35f;
                        for (int col = 0; col < cols; ++col) {
                            const float bx = (cols == 1) ? c.x : (c.x + (col == 0 ? -he.x * 0.44f : he.x * 0.44f));
                            drawModelStyled(cubeModel_, {bx, fy, bz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                            {3.4f, 0.16f, 1.15f}, Color{196, 200, 208, 255},
                                            0.68f, 0.05f, 0.0f, MatKind::Standard);
                            drawModelStyled(cubeModel_, {bx, fy + 0.52f, railZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                            {3.4f, 0.86f, 0.07f}, Color{36, 48, 62, 255},
                                            0.18f, 0.45f, 0.0f, MatKind::CarGlass);
                        }
                    }
                }
            }
        }

        // 3) Signature Commercial Landmarks (AMMU-TECH, PAY 'N' TUNE, LUXURY AUTOS, MODA BOUTIQUE, PULSE NIGHTCLUB)
        if (!shadowPass) {
            if (b.style == BuildingStyle::ShopBuilding || b.style == BuildingStyle::GarageBuilding) {
                const Color signCol = (b.style == BuildingStyle::ShopBuilding)
                                          ? Color{255, 78, 48, 255}
                                          : Color{42, 218, 255, 255};
                drawModelStyled(cubeModel_, {c.x, 5.3f, b.footprint.min.z - 0.28f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.55f, 1.45f, 0.38f}, signCol,
                                0.15f, 0.0f, 2.1f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {c.x, 3.65f, b.footprint.min.z - 0.95f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.62f, 0.24f, 1.8f}, Color{36, 40, 48, 255},
                                0.45f, 0.40f, 0.0f, MatKind::Standard);
            } else if (b.style == BuildingStyle::DealershipBuilding) {
                // LUXURY AUTOS Panoramic Glass Showroom Atrium + Brushed Titanium Canopy + Gold Neon Fascia
                drawModelStyled(cubeModel_, {c.x, 2.85f, b.footprint.min.z - 0.20f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.86f, 5.4f, 0.32f}, Color{22, 36, 52, 255},
                                0.06f, 0.25f, 0.0f, MatKind::CarGlass);
                drawModelStyled(cubeModel_, {c.x, 5.85f, b.footprint.min.z - 1.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.94f, 0.36f, 2.7f}, Color{215, 220, 228, 255},
                                0.25f, 0.80f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, 6.65f, b.footprint.min.z - 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.72f, 1.15f, 0.42f}, Color{255, 198, 56, 255},
                                0.12f, 0.0f, 2.3f, MatKind::EmissiveGlow);
                // Illuminated circular VIP car turntable pad on the showroom forecourt
                drawModelStyled(cylModel_, {c.x - 6.0f, 0.08f, b.footprint.min.z - 5.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.2f, 0.08f, 3.2f}, Color{42, 46, 56, 255}, 0.25f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {c.x - 6.0f, 0.06f, b.footprint.min.z - 5.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.38f, 0.05f, 3.38f}, Color{255, 206, 72, 255}, 0.1f, 0.0f, 1.8f, MatKind::EmissiveGlow);
            } else if (b.style == BuildingStyle::BoutiqueBuilding) {
                // MODA BOUTIQUE Haute-Couture Rose-Gold Showcase & Illuminated Display Windows
                drawModelStyled(cubeModel_, {c.x, 5.2f, b.footprint.max.z + 0.30f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.58f, 1.15f, 0.36f}, Color{248, 108, 222, 255},
                                0.12f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {c.x, 2.1f, b.footprint.max.z + 0.14f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.66f, 3.6f, 0.18f}, Color{255, 232, 196, 255},
                                0.15f, 0.10f, night ? 1.5f : 0.45f, MatKind::EmissiveGlow);
            } else if (b.style == BuildingStyle::NightclubBuilding) {
                // PULSE NIGHTCLUB Animated Neon Equalizer Bars + VIP Entrance + Night Sky Beams!
                for (int bar = -3; bar <= 3; ++bar) {
                    const float eqWave = 0.45f + 0.55f * std::abs(std::sin(animT * 3.8f + static_cast<float>(bar) * 0.9f));
                    const float barH = (b.height - 3.5f) * eqWave;
                    const Color eqCol = (std::abs(bar) % 2 == 0)
                                            ? Color{192, 68, 255, 255}
                                            : Color{52, 232, 255, 255};
                    drawModelStyled(cubeModel_,
                                    {c.x + static_cast<float>(bar) * 2.15f, 3.2f + barH * 0.5f, b.footprint.max.z + 0.22f},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.52f, barH, 0.24f},
                                    eqCol, 0.10f, 0.0f, 2.6f, MatKind::EmissiveGlow);
                }
                drawModelStyled(cubeModel_, {c.x, 3.15f, b.footprint.max.z + 1.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {6.8f, 0.28f, 2.6f}, Color{22, 18, 32, 255}, 0.30f, 0.6f, 0.0f, MatKind::Standard);
                if (night) {
                    const float sweep = std::sin(animT * 1.15f) * 16.0f;
                    rlPushMatrix();
                    rlTranslatef(c.x - 4.5f, b.height + 0.4f, c.z);
                    rlRotatef(sweep, 0.0f, 0.0f, 1.0f);
                    DrawCylinder({0.0f, 0.0f, 0.0f}, 3.6f, 0.35f, 34.0f, 12, Color{188, 78, 255, 34});
                    rlPopMatrix();
                    rlPushMatrix();
                    rlTranslatef(c.x + 4.5f, b.height + 0.4f, c.z);
                    rlRotatef(-sweep, 0.0f, 0.0f, 1.0f);
                    DrawCylinder({0.0f, 0.0f, 0.0f}, 3.6f, 0.35f, 34.0f, 12, Color{56, 228, 255, 34});
                    rlPopMatrix();
                }
            }
        }
    }
}

void Renderer3D::drawTreesAndProps(const GameWorld& world, const Vec2& focus, bool shadowPass) {
    const float treeCull = shadowPass ? (135.0f * 135.0f) : (235.0f * 235.0f);

    for (const auto& t : world.city().trees()) {
        if ((t.pos - focus).lengthSq() > treeCull) continue;

        // Concrete urban sidewalk tree planter curb & dark soil mulch
        if (!shadowPass && t.inPlanter) {
            drawModelStyled(cubeModel_, {t.pos.x, 0.18f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.55f, 0.24f, 1.55f}, Color{154, 158, 166, 255},
                            0.72f, 0.05f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {t.pos.x, 0.28f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.28f, 0.08f, 1.28f}, Color{68, 50, 36, 255},
                            0.90f, 0.0f, 0.0f, MatKind::Standard);
        }

        if (t.palmStyle) {
            // California coastal palm tree: segmented leaning trunk + 6 arched 3D fronds
            rlPushMatrix();
            rlTranslatef(t.pos.x, 0.06f, t.pos.z);
            drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.26f, t.trunkHeight * 0.55f, 0.26f}, Color{132, 96, 66, 255},
                            0.85f, 0.0f, 0.0f, MatKind::Standard);
            rlTranslatef(0.14f, t.trunkHeight * 0.52f, 0.0f);
            rlRotatef(-7.0f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.21f, t.trunkHeight * 0.58f, 0.21f}, Color{140, 102, 70, 255},
                            0.85f, 0.0f, 0.0f, MatKind::Standard);

            rlTranslatef(0.0f, t.trunkHeight * 0.56f, 0.0f);
            for (int f = 0; f < 6; ++f) {
                rlPushMatrix();
                rlRotatef(static_cast<float>(f) * 60.0f + t.pos.x * 9.0f, 0.0f, 1.0f, 0.0f);
                rlRotatef(26.0f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cubeModel_, {t.crownRadius * 0.58f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {t.crownRadius * 1.25f, 0.12f, 0.62f}, Color{72, 168, 74, 255},
                                0.62f, 0.0f, 0.0f, MatKind::FoliageGrass);
                rlPopMatrix();
            }
            rlPopMatrix();
        } else {
            // Multi-cluster lush park/boulevard deciduous tree
            drawModelStyled(cylModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.30f, t.trunkHeight, 0.30f}, Color{108, 76, 50, 255},
                            0.86f, 0.0f, 0.0f, MatKind::Standard);

            const float cy = t.trunkHeight + t.crownRadius * 0.55f;
            const Color cMain{54, 148, 66, 255};
            const Color cBright{68, 166, 76, 255};
            drawModelStyled(sphereModel_, {t.pos.x, cy, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {t.crownRadius, t.crownRadius * 0.88f, t.crownRadius},
                            cMain, 0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
            if (!shadowPass) {
                drawModelStyled(sphereModel_, {t.pos.x + t.crownRadius * 0.42f, cy - 0.22f, t.pos.z + 0.25f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {t.crownRadius * 0.68f, t.crownRadius * 0.64f, t.crownRadius * 0.68f},
                                cBright, 0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
                drawModelStyled(sphereModel_, {t.pos.x - t.crownRadius * 0.38f, cy + 0.18f, t.pos.z - 0.30f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {t.crownRadius * 0.64f, t.crownRadius * 0.62f, t.crownRadius * 0.64f},
                                cBright, 0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
            }
        }
    }

    const bool night = world.isNightTime();
    const float propCull = shadowPass ? (105.0f * 105.0f) : (195.0f * 195.0f);

    for (const auto& prop : world.city().streetProps()) {
        if ((prop.pos - focus).lengthSq() > propCull) continue;
        if (shadowPass &&
            prop.kind != StreetPropKind::StreetLight &&
            prop.kind != StreetPropKind::BusShelter &&
            prop.kind != StreetPropKind::Billboard &&
            prop.kind != StreetPropKind::BeachUmbrella) {
            continue;
        }

        const float tilt = prop.broken ? 78.0f : 0.0f;
        rlPushMatrix();
        const float baseGroundY = (prop.kind == StreetPropKind::BeachUmbrella) ? 0.02f : 0.08f;
        rlTranslatef(prop.pos.x, baseGroundY, prop.pos.z);
        rlRotatef(-prop.yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
        if (prop.broken) {
            rlRotatef(tilt, 1.0f, 0.0f, 0.3f);
        }

        switch (prop.kind) {
            case StreetPropKind::StreetLight: {
                // Metallic streetlamp mast + overhanging signal arm
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.13f, 6.25f, 0.13f}, Color{78, 84, 94, 255},
                                0.38f, 0.72f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.72f, 6.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.56f, 0.14f, 0.18f}, Color{86, 92, 104, 255},
                                0.38f, 0.72f, 0.0f, MatKind::Standard);

                if (!shadowPass && !prop.broken) {
                    const bool ewGreen = world.isTrafficGreenForHorizontal();
                    const bool nsGreen = world.isTrafficGreenForVertical();
                    const Color ewCol = ewGreen ? Color{48, 248, 96, 255}
                                                : (nsGreen ? Color{255, 54, 48, 255} : Color{255, 202, 42, 255});
                    const Color nsCol = nsGreen ? Color{48, 248, 96, 255}
                                                : (ewGreen ? Color{255, 54, 48, 255} : Color{255, 202, 42, 255});

                    drawModelStyled(cubeModel_, {1.05f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.30f, 0.74f, 0.30f}, Color{24, 28, 34, 255},
                                    0.55f, 0.35f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {1.21f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, ewCol, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {0.89f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, ewCol, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {1.05f, 5.62f, 0.16f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, nsCol, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {1.05f, 5.62f, -0.16f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, nsCol, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);

                    // LED luminaire head (glows warmly at night + casts ground light pool)
                    const Color lampCol = night ? Color{255, 232, 168, 255} : Color{210, 218, 228, 255};
                    drawModelStyled(cubeModel_, {1.35f, 6.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.42f, 0.10f, 0.24f}, lampCol,
                                    0.15f, 0.2f, night ? 2.4f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    if (night) {
                        DrawCylinder({1.35f, -0.02f, 0.0f}, 4.2f, 4.2f, 0.015f, 16, Color{255, 220, 142, 40});
                    }
                }
                break;
            }
            case StreetPropKind::Hydrant: {
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.21f, 0.72f, 0.21f}, Color{224, 42, 38, 255},
                                0.35f, 0.35f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.0f, 0.72f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.20f, 0.16f, 0.20f}, Color{236, 198, 62, 255},
                                0.30f, 0.55f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::Crate: {
                drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.90f, 0.92f}, Color{176, 130, 78, 255},
                                0.78f, 0.0f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::TrafficCone: {
                drawModelStyled(coneModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.70f, 0.26f}, Color{252, 114, 28, 255},
                                0.48f, 0.0f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::BusShelter: {
                // Modern Glass & Anodized Aluminum Transit Shelter + Interior Bench + Lit Route Panel
                const Color frameCol{42, 46, 56, 255};
                drawModelStyled(cubeModel_, {0.0f, 2.36f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.50f, 0.12f, 1.56f}, frameCol, 0.35f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-1.62f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.10f, 2.36f, 0.10f}, frameCol, 0.35f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, { 1.62f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.10f, 2.36f, 0.10f}, frameCol, 0.35f, 0.65f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    // Tempered glass rear wall
                    drawModelStyled(cubeModel_, {0.0f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {3.14f, 2.16f, 0.05f}, Color{28, 44, 62, 255},
                                    0.08f, 0.2f, 0.0f, MatKind::CarGlass);
                    // Illuminated side ad / route map lightbox
                    drawModelStyled(cubeModel_, {1.62f, 1.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.10f, 2.08f, 1.24f}, Color{68, 198, 255, 255},
                                    0.15f, 0.0f, night ? 1.8f : 0.55f, MatKind::EmissiveGlow);
                    // Waiting bench inside shelter
                    drawModelStyled(cubeModel_, {-0.25f, 0.48f, -0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.10f, 0.08f, 0.44f}, Color{168, 118, 72, 255},
                                    0.72f, 0.0f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::Bench: {
                // Slatted Teak & Cast-Iron Boulevard Bench
                drawModelStyled(cubeModel_, {0.0f, 0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.58f, 0.08f, 0.52f}, Color{166, 116, 68, 255},
                                0.74f, 0.0f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 0.74f, -0.22f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.58f, 0.44f, 0.07f}, Color{166, 116, 68, 255},
                                0.74f, 0.0f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-0.66f, 0.24f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.09f, 0.48f, 0.50f}, Color{38, 42, 48, 255},
                                0.45f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, { 0.66f, 0.24f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.09f, 0.48f, 0.50f}, Color{38, 42, 48, 255},
                                0.45f, 0.65f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::TrashBin: {
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.28f, 0.82f, 0.28f}, Color{38, 76, 58, 255},
                                0.55f, 0.25f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.0f, 0.82f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.29f, 0.14f, 0.29f}, Color{196, 204, 214, 255},
                                0.28f, 0.75f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::FuelPump: {
                // GLOBE OIL Digital Fuel Dispenser
                drawModelStyled(cubeModel_, {0.0f, 0.96f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.86f, 1.72f, 0.48f}, Color{236, 240, 246, 255},
                                0.42f, 0.25f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 1.66f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.36f, 0.52f}, Color{238, 62, 36, 255},
                                0.30f, 0.20f, 0.6f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {0.0f, 1.18f, 0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.46f, 0.28f, 0.04f}, Color{72, 224, 255, 255},
                                0.12f, 0.0f, 1.8f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {0.0f, 1.18f, -0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.46f, 0.28f, 0.04f}, Color{72, 224, 255, 255},
                                0.12f, 0.0f, 1.8f, MatKind::EmissiveGlow);
                break;
            }
            case StreetPropKind::BeachUmbrella: {
                const int colorIdx = std::abs(static_cast<int>(prop.pos.z * 0.3f)) % 4;
                const Color canopyCol = (colorIdx == 0) ? Color{246, 74, 68, 255}
                                      : (colorIdx == 1) ? Color{46, 178, 246, 255}
                                      : (colorIdx == 2) ? Color{252, 196, 48, 255}
                                                        : Color{64, 214, 146, 255};
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.06f, 2.25f, 0.06f}, Color{240, 242, 246, 255},
                                0.45f, 0.3f, 0.0f, MatKind::Standard);
                drawModelStyled(coneModel_, {0.0f, 1.95f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.75f, 0.62f, 1.75f}, canopyCol,
                                0.65f, 0.0f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    // Pair of coastal beach sun loungers beside umbrella
                    drawModelStyled(cubeModel_, {0.0f, 0.22f, 1.15f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.65f, 0.12f, 0.68f}, Color{242, 244, 248, 255},
                                    0.65f, 0.0f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {0.0f, 0.22f, -1.15f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.65f, 0.12f, 0.68f}, Color{242, 244, 248, 255},
                                    0.65f, 0.0f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::Billboard: {
                // Tall Steel Monopole + Illuminated Graphic Ad Board (Pure Geometric Artwork — Zero Font Artifacts!)
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 6.2f, 0.26f}, Color{62, 68, 78, 255},
                                0.40f, 0.75f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 7.55f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.90f, 2.95f, 0.36f}, Color{28, 32, 40, 255},
                                0.42f, 0.65f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    const int theme = std::abs(static_cast<int>(prop.pos.x + prop.pos.z)) % 3;
                    const Color bgCol = (theme == 0) ? Color{252, 108, 64, 255}
                                      : (theme == 1) ? Color{36, 168, 248, 255}
                                                     : Color{168, 62, 246, 255};
                    const Color stripeCol = (theme == 0) ? Color{255, 224, 96, 255}
                                          : (theme == 1) ? Color{242, 248, 255, 255}
                                                         : Color{64, 242, 224, 255};
                    for (int face = -1; face <= 1; face += 2) {
                        const float fz = static_cast<float>(face) * 0.20f;
                        drawModelStyled(cubeModel_, {0.0f, 7.55f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {5.55f, 2.60f, 0.04f}, bgCol,
                                        0.15f, 0.0f, night ? 1.85f : 0.45f, MatKind::EmissiveGlow);
                        // Stylized sun disc + horizon speed stripes graphic
                        drawModelStyled(cylModel_, {-1.45f, 7.65f, fz + face * 0.03f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                        {0.78f, 0.04f, 0.78f}, stripeCol,
                                        0.12f, 0.0f, night ? 2.2f : 0.65f, MatKind::EmissiveGlow);
                        drawModelStyled(cubeModel_, {0.95f, 7.82f, fz + face * 0.03f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {2.55f, 0.34f, 0.04f}, stripeCol,
                                        0.12f, 0.0f, night ? 2.1f : 0.65f, MatKind::EmissiveGlow);
                        drawModelStyled(cubeModel_, {0.72f, 7.22f, fz + face * 0.03f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {2.10f, 0.22f, 0.04f}, WHITE,
                                        0.12f, 0.0f, night ? 2.0f : 0.55f, MatKind::EmissiveGlow);
                    }
                }
                break;
            }
        }
        rlPopMatrix();
    }
}

void Renderer3D::drawVehicle3D(const Vehicle& v, bool nightMode, bool shadowPass) {
    const auto& spec = getVehicleSpec(v.kind);
    const float halfL = spec.halfLength;
    const float halfW = spec.halfWidth;
    const float L = halfL * 2.0f;
    const float W = halfW * 2.0f;
    const float H = spec.chassisHeight;

    // 1) Soft contact shadow + Custom LED Neon Underglow Pool on the road!
    if (!shadowPass) {
        rlPushMatrix();
        rlTranslatef(v.pos.x, 0.016f, v.pos.z);
        rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
        DrawCube({0.0f, 0.0f, 0.0f}, L * 0.98f, 0.008f, W * 1.02f, Color{8, 10, 14, 145});

        if (!v.destroyed && v.neonPreset > 0) {
            const Color nc = toRayColor(getNeonPresetColor(v.neonPreset));
            const float alphaMul = nightMode ? 1.0f : 0.58f;
            // 3 concentric soft oval glow rings on the asphalt (no harsh rectangular corners!)
            const float rings[3][3] = {
                {L * 0.49f, W * 0.68f, 34.0f},
                {L * 0.41f, W * 0.56f, 54.0f},
                {L * 0.32f, W * 0.44f, 78.0f}
            };
            for (int r = 0; r < 3; ++r) {
                rlPushMatrix();
                rlScalef(rings[r][0], 1.0f, rings[r][1]);
                DrawCylinder({0.0f, 0.004f + static_cast<float>(r) * 0.0015f, 0.0f},
                             1.0f, 1.0f, 0.004f, 20,
                             Color{nc.r, nc.g, nc.b, static_cast<unsigned char>(rings[r][2] * alphaMul)});
                rlPopMatrix();
            }
            // Bright emissive neon side tubes tucked cleanly under the rocker sills
            drawModelStyled(cubeModel_, {0.0f, 0.14f, -halfW * 0.86f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.52f, 0.03f, 0.05f}, nc, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {0.0f, 0.14f,  halfW * 0.86f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.52f, 0.03f, 0.05f}, nc, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
        }
        rlPopMatrix();
    }

    rlPushMatrix();
    rlTranslatef(v.pos.x, 0.0f, v.pos.z);
    rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(v.bodyPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(v.bodyRoll * kRadToDeg, 1.0f, 0.0f, 0.0f);

    const Color bodyCol = v.destroyed ? Color{44, 42, 40, 255} : toRayColor(v.paintColor);
    const Color darkTrim{24, 27, 34, 255};
    const Color chromeCol{222, 228, 238, 255};
    const Color smokedGlass{14, 24, 38, 255};
    const MatKind paintMat = v.destroyed ? MatKind::Standard : MatKind::CarClearcoat;
    const float paintRough = v.destroyed ? 0.88f : (v.mattePaint ? 0.45f : 0.10f);
    const float paintMetal = v.destroyed ? 0.15f : (v.mattePaint ? 0.25f : 0.48f);

    // 2) Smooth-Normal Sculpted 3D Body Shell + Framed Curved Glass + Wheel-House/Aero Trim!
    auto& carMdl = carModels_[static_cast<std::size_t>(v.kind)];
    drawModelStyled(carMdl.body, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {1.0f, 1.0f, 1.0f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
    drawModelStyled(carMdl.trim, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {1.0f, 1.0f, 1.0f}, darkTrim, 0.46f, 0.35f, 0.0f, MatKind::Standard);

    if (!shadowPass) {
        drawModelStyled(carMdl.glass, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.0f, 1.0f, 1.0f}, smokedGlass, 0.05f, 0.18f, 0.0f, MatKind::CarGlass);

        // Sculpted aerodynamic wing mirrors on sleek stalks
        const float mirrorX = halfL * 0.22f;
        const float mirrorY = 0.22f + H * 0.56f;
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(sphereModel_, {mirrorX, mirrorY, static_cast<float>(side) * (halfW * 0.99f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.11f, 0.065f, 0.095f},
                            bodyCol, paintRough, paintMetal, 0.0f, paintMat);
        }

        // Rear performance grille vent, license plate & twin/quad chrome exhaust tips
        if (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::SportsCoupe) {
            drawModelStyled(cubeModel_, {-halfL * 0.995f, 0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.05f, 0.18f, W * 0.76f}, Color{18, 20, 26, 255},
                            0.55f, 0.35f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-halfL - 0.008f, 0.39f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.03f, 0.13f, 0.36f}, Color{238, 242, 248, 255},
                        0.45f, 0.10f, 0.0f, MatKind::Standard);
        const float exY = (v.kind == VehicleKind::Hypercar) ? 0.33f : 0.24f;
        const float exZ = (v.kind == VehicleKind::Hypercar) ? (halfW * 0.20f) : (halfW * 0.54f);
        drawModelStyled(wheelModel_, {-halfL - 0.015f, exY, -exZ}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {0.058f, 0.058f, 0.16f}, chromeCol, 0.18f, 0.92f, 0.0f, MatKind::Standard);
        drawModelStyled(wheelModel_, {-halfL - 0.015f, exY,  exZ}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {0.058f, 0.058f, 0.16f}, chromeCol, 0.18f, 0.92f, 0.0f, MatKind::Standard);
    }

    // 3) GT Aero Wing Spoiler (Custom Tuned Spoiler!) & Roof Accents
    if (v.customSpoiler) {
        const float deckY = (v.kind == VehicleKind::Hypercar) ? 0.80f
                          : ((v.kind == VehicleKind::SportsCoupe) ? 0.86f : (0.22f + H * 0.62f));
        const float spX = -halfL * 0.86f;
        drawModelStyled(cubeModel_, {spX, deckY + 0.12f, -halfW * 0.56f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.22f, 0.045f}, darkTrim, 0.32f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {spX, deckY + 0.12f,  halfW * 0.56f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.22f, 0.045f}, darkTrim, 0.32f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {spX - 0.03f, deckY + 0.23f, 0.0f}, {0.0f, 0.0f, 1.0f}, 5.0f,
                        {0.30f, 0.042f, W * 0.94f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
    }
    if (v.kind == VehicleKind::Taxi && !v.destroyed && !shadowPass) {
        drawModelStyled(cubeModel_, {-0.12f, 1.46f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.38f, 0.16f, 0.64f}, Color{255, 232, 118, 255},
                        0.2f, 0.0f, 1.8f, MatKind::EmissiveGlow);
    }

    // 4) Recessed Round 24-Segment Bevelled Tires + 5-Twin-Spoke Concave Alloy Rims + Brake Calipers!
    float axleFrac = 0.63f;
    float wheelR   = 0.35f;
    float tireW    = 0.26f;
    switch (v.kind) {
        case VehicleKind::Hypercar:    axleFrac = 0.62f; wheelR = 0.35f; tireW = 0.30f; break;
        case VehicleKind::SportsCoupe: axleFrac = 0.63f; wheelR = 0.35f; tireW = 0.28f; break;
        case VehicleKind::MuscleCar:   axleFrac = 0.64f; wheelR = 0.36f; tireW = 0.28f; break;
        case VehicleKind::LuxurySedan: axleFrac = 0.64f; wheelR = 0.36f; tireW = 0.26f; break;
        case VehicleKind::Taxi:        axleFrac = 0.62f; wheelR = 0.34f; tireW = 0.24f; break;
        case VehicleKind::Compact:     axleFrac = 0.64f; wheelR = 0.32f; tireW = 0.23f; break;
        case VehicleKind::SUV:         axleFrac = 0.63f; wheelR = 0.40f; tireW = 0.29f; break;
        case VehicleKind::ArmoredSuv:  axleFrac = 0.63f; wheelR = 0.43f; tireW = 0.32f; break;
        case VehicleKind::DeliveryVan: axleFrac = 0.62f; wheelR = 0.38f; tireW = 0.26f; break;
        default: break;
    }

    const Color rimCol = (v.rimStyle == 1) ? Color{236, 198, 92, 255}  // Champagne Gold
                       : (v.rimStyle == 2) ? Color{34, 38, 46, 255}    // Gloss Obsidian Black
                                           : Color{218, 226, 236, 255}; // Brushed Silver Alloy
    const float axleX = halfL * axleFrac;
    const float axleZ = halfW * 0.89f; // Recessed flush inside sculpted fender flares!

    for (int side = -1; side <= 1; side += 2) {
        for (int front = -1; front <= 1; front += 2) {
            rlPushMatrix();
            rlTranslatef(static_cast<float>(front) * axleX, wheelR, static_cast<float>(side) * axleZ);
            if (front > 0) {
                rlRotatef(-v.steerVisual * 28.0f, 0.0f, 1.0f, 0.0f);
            }

            // Non-spinning red sport brake caliper inside rim!
            if (!shadowPass) {
                drawModelStyled(cubeModel_, {-wheelR * 0.34f, wheelR * 0.16f, static_cast<float>(side) * (tireW * 0.28f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.11f, 0.16f, 0.06f},
                                Color{226, 42, 38, 255}, 0.30f, 0.55f, 0.0f, MatKind::Standard);
            }

            rlRotatef(-v.wheelSpin * kRadToDeg, 0.0f, 0.0f, 1.0f);

            // Bevelled round rubber tire
            drawModelStyled(wheelModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {wheelR, wheelR, tireW}, Color{20, 22, 26, 255},
                            0.74f, 0.06f, 0.0f, MatKind::Standard);

            if (!shadowPass) {
                // Dark steel brake rotor disc + 5-twin-spoke concave alloy rim!
                drawModelStyled(wheelModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {wheelR * 0.56f, wheelR * 0.56f, tireW * 0.82f}, Color{68, 72, 80, 255},
                                0.35f, 0.80f, 0.0f, MatKind::Standard);
                drawModelStyled(sportRimModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {wheelR, wheelR, tireW}, rimCol,
                                (v.rimStyle == 2) ? 0.16f : 0.20f, 0.88f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();
        }
    }

    // 5) Sleek Contoured LED DRLs / Xenon Headlights, Blade Taillights & Nitrous Afterburners
    if (!shadowPass && !v.destroyed) {
        const Color headCol = nightMode ? Color{242, 248, 255, 255} : Color{228, 240, 252, 255};
        const float headGlow = nightMode ? 2.7f : 1.15f;
        const float headY = (v.kind == VehicleKind::Hypercar) ? 0.43f
                          : ((v.kind == VehicleKind::SportsCoupe) ? 0.48f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv) ? 0.78f : 0.58f));
        const float headX = halfL * 0.95f;
        const float headZ = halfW * 0.65f;

        for (int side = -1; side <= 1; side += 2) {
            rlPushMatrix();
            rlTranslatef(headX, headY, static_cast<float>(side) * headZ);
            rlRotatef(static_cast<float>(-side) * 18.0f, 0.0f, 1.0f, 0.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, (v.kind == VehicleKind::Hypercar ? 0.068f : 0.105f), 0.32f},
                            headCol, 0.1f, 0.0f, headGlow, MatKind::EmissiveGlow);
            rlPopMatrix();
        }

        if (nightMode && (v.isPlayerDriving || v.hasNpcDriver)) {
            DrawCube({halfL + 4.8f, 0.028f, 0.0f}, 8.8f, 0.015f, W * 1.85f, Color{235, 242, 255, 42});
        }

        const Color tailCol = v.brakingVisual ? Color{255, 38, 36, 255} : Color{228, 28, 38, 255};
        const float tailGlow = v.brakingVisual ? 2.9f : 1.75f;
        const float tailY = (v.kind == VehicleKind::Hypercar) ? 0.58f
                          : ((v.kind == VehicleKind::SportsCoupe) ? 0.64f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv) ? 0.86f : 0.65f));

        if (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::LuxurySedan) {
            // Sleek full-width modern LED blade lightbar proud of rear fascia!
            drawModelStyled(cubeModel_, {-halfL - 0.008f, tailY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.05f, 0.062f, W * 0.80f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL * 0.985f, tailY, -halfW * 0.68f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.08f, 0.095f, 0.22f}, tailCol, 0.1f, 0.0f, tailGlow * 1.1f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL * 0.985f, tailY,  halfW * 0.68f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.08f, 0.095f, 0.22f}, tailCol, 0.1f, 0.0f, tailGlow * 1.1f, MatKind::EmissiveGlow);
        } else {
            drawModelStyled(cubeModel_, {-halfL - 0.004f, tailY, -halfW * 0.64f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.12f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL - 0.004f, tailY,  halfW * 0.64f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.12f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
        }

        if (v.nitroActive) {
            const float exZ = (v.kind == VehicleKind::Hypercar) ? (halfW * 0.18f) : (halfW * 0.54f);
            const float exY = (v.kind == VehicleKind::Hypercar) ? 0.34f : 0.24f;
            drawModelStyled(coneModel_, {-halfL - 0.06f, exY, -exZ}, {0.0f, 0.0f, 1.0f}, 90.0f,
                            {0.11f, 0.95f, 0.11f}, Color{68, 214, 255, 255},
                            0.1f, 0.0f, 3.3f, MatKind::EmissiveGlow);
            drawModelStyled(coneModel_, {-halfL - 0.06f, exY,  exZ}, {0.0f, 0.0f, 1.0f}, 90.0f,
                            {0.11f, 0.95f, 0.11f}, Color{68, 214, 255, 255},
                            0.1f, 0.0f, 3.3f, MatKind::EmissiveGlow);
        }
    }

    rlPopMatrix();
}

void Renderer3D::drawCharacter3D(const Vec2& pos, float groundY, float yaw, float animPhase,
                                 ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                                 bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                                 float aimPitch, bool hasSunglasses, OutfitId styleOutfit, bool shadowPass) {
    rlPushMatrix();
    rlTranslatef(pos.x, groundY, pos.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);

    if (!alive) {
        rlTranslatef(0.0f, 0.16f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    }

    const float legSwing = alive ? std::sin(animPhase) * 28.0f : 0.0f;
    const Color topC  = toRayColor(top);
    const Color botC  = toRayColor(bottom);
    const Color skinC = toRayColor(skin);
    const Color accC  = toRayColor(getOutfitSpec(styleOutfit).accent);
    const Color shoeC = (styleOutfit == OutfitId::ViceWhiteSuit) ? Color{230, 232, 238, 255} : Color{26, 28, 34, 255};

    if (!shadowPass && alive) {
        DrawCylinder({0.0f, 0.012f, 0.0f}, 0.36f, 0.36f, 0.008f, 12, Color{10, 12, 16, 115});
    }

    // Left & Right Legs + Sneakers/Dress Shoes
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 0.76f, static_cast<float>(side) * 0.125f);
        rlRotatef(static_cast<float>(side) * legSwing, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.0f, -0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.19f, 0.66f, 0.19f}, botC, 0.78f, 0.0f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {0.04f, -0.70f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.26f, 0.11f, 0.20f}, shoeC, 0.55f, 0.10f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();
    }

    // Belt + Sculpted Jacket/Torso + Bespoke Boutique 3D Outfit Details!
    drawModelStyled(cubeModel_, {0.0f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.27f, 0.08f, 0.43f}, Color{30, 32, 38, 255}, 0.52f, 0.20f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 1.09f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.27f, 0.60f, 0.44f}, topC,
                    (styleOutfit == OutfitId::RacerLeather) ? 0.28f : 0.66f,
                    (styleOutfit == OutfitId::GoldBossCoat) ? 0.35f : 0.04f,
                    0.0f, MatKind::Standard);

    if (!shadowPass) {
        if (styleOutfit == OutfitId::ViceWhiteSuit || styleOutfit == OutfitId::MidnightTuxedo) {
            // Crisp dress shirt V-front + silk necktie!
            drawModelStyled(cubeModel_, {0.132f, 1.16f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.03f, 0.40f, 0.17f}, WHITE, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.145f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.025f, 0.34f, 0.055f}, accC, 0.35f, 0.15f, 0.0f, MatKind::Standard);
        } else if (styleOutfit == OutfitId::TacOpsArmor) {
            // Molded 3D Kevlar plate-carrier vest + mag pouches + shoulder armor!
            drawModelStyled(cubeModel_, {0.0f, 1.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.33f, 0.48f, 0.47f}, accC, 0.42f, 0.35f, 0.0f, MatKind::Standard);
        } else if (styleOutfit == OutfitId::GoldBossCoat) {
            // Knee-length tailored boss overcoat skirt + gold lapel trim!
            drawModelStyled(cubeModel_, {-0.02f, 0.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.30f, 0.36f, 0.47f}, topC, 0.48f, 0.25f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.138f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.03f, 0.48f, 0.15f}, accC, 0.25f, 0.80f, 0.0f, MatKind::Standard);
        } else {
            drawModelStyled(cubeModel_, {0.11f, 1.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.08f, 0.18f, 0.18f}, accC, 0.62f, 0.0f, 0.0f, MatKind::Standard);
        }
    }

    // Neck + Rounded Head + Hair/Helmet + Optional Sunglasses
    rlPushMatrix();
    rlTranslatef(0.0f, 1.42f, 0.0f);
    if (alive) {
        rlRotatef(clampf(aimPitch * 0.65f, -0.65f, 0.75f) * kRadToDeg, 0.0f, 0.0f, 1.0f);
    }
    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.075f, 0.08f, 0.075f}, skinC, 0.65f, 0.0f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 0.16f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.25f, 0.26f, 0.25f}, skinC, 0.62f, 0.0f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {-0.015f, 0.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.27f, 0.085f, 0.27f},
                    (styleOutfit == OutfitId::TacOpsArmor) ? Color{32, 38, 34, 255} : Color{36, 30, 26, 255},
                    0.65f, 0.15f, 0.0f, MatKind::Standard);
    if (!shadowPass && hasSunglasses) {
        drawModelStyled(cubeModel_, {0.125f, 0.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.03f, 0.06f, 0.22f}, Color{16, 20, 28, 255},
                        0.12f, 0.45f, 0.0f, MatKind::CarGlass);
    }
    rlPopMatrix();

    // Arms, Hands & Multi-Part 3D Weapons (including Combat MG & Heavy Tactical Launcher!)
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 1.32f, static_cast<float>(side) * 0.27f);
        if (armed && side == 1 && alive) {
            rlRotatef(aimPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.22f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.44f, 0.14f, 0.14f}, topC, 0.68f, 0.04f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.46f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 0.11f, 0.11f}, skinC, 0.65f, 0.0f, 0.0f, MatKind::Standard);

            if (weapon == WeaponId::HeavyLauncher) {
                // Olive-drab shoulder-mounted tactical explosive launcher tube!
                drawModelStyled(cylModel_, {0.10f, 0.10f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.085f, 0.96f, 0.085f}, Color{62, 76, 56, 255},
                                0.42f, 0.55f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.98f, 0.10f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.11f, 0.12f, 0.11f}, Color{36, 40, 46, 255},
                                0.35f, 0.75f, 0.0f, MatKind::Standard);
            } else {
                const bool heavyMg = (weapon == WeaponId::CombatMG);
                const bool longGun = (weapon == WeaponId::AssaultRifle || weapon == WeaponId::Shotgun || heavyMg);
                const float gunLen = heavyMg ? 0.78f : (longGun ? 0.64f : (weapon == WeaponId::SMG ? 0.44f : 0.32f));
                drawModelStyled(cubeModel_, {0.55f, 0.025f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {gunLen, heavyMg ? 0.12f : 0.10f, 0.075f}, Color{34, 38, 46, 255},
                                0.28f, 0.82f, 0.0f, MatKind::Standard);
                if (!shadowPass && heavyMg) {
                    // Box drum magazine under Combat MG
                    drawModelStyled(cubeModel_, {0.54f, -0.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.16f, 0.16f, 0.14f}, Color{54, 66, 52, 255},
                                    0.45f, 0.45f, 0.0f, MatKind::Standard);
                } else if (!shadowPass && (weapon == WeaponId::SMG || weapon == WeaponId::AssaultRifle)) {
                    drawModelStyled(cubeModel_, {0.56f, -0.09f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, 0.16f, 0.055f}, Color{24, 26, 32, 255},
                                    0.35f, 0.70f, 0.0f, MatKind::Standard);
                }
                if (!shadowPass && muzzleFlash > 0.0f) {
                    drawModelStyled(sphereModel_, {0.58f + gunLen * 0.5f, 0.03f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.19f, 0.14f, 0.14f}, Color{255, 224, 92, 255},
                                    0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
                }
            }
        } else {
            rlRotatef(static_cast<float>(-side) * legSwing * 0.8f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.15f, 0.44f, 0.15f}, topC, 0.68f, 0.04f, 0.0f, MatKind::Standard);
            if (!shadowPass) {
                drawModelStyled(cubeModel_, {0.0f, -0.47f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.12f, 0.11f, 0.12f}, skinC, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
        rlPopMatrix();
    }

    rlPopMatrix();
}

void Renderer3D::drawPoisAndPickups(const GameWorld& world) {
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.0f);

    for (const auto& poi : world.city().pois()) {
        if (world.mission().kind != ActiveMissionKind::None &&
            (poi.kind == PoiKind::MissionDelivery ||
             poi.kind == PoiKind::MissionRace ||
             poi.kind == PoiKind::MissionGangRaid)) {
            continue;
        }
        Color c = toRayColor(poi.markerColor);
        c.a = static_cast<unsigned char>(95 + pulse * 68.0f);
        drawModelStyled(cylModel_, {poi.pos.x, 0.08f, poi.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {poi.radius * 0.72f, 2.15f, poi.radius * 0.72f}, c,
                        0.1f, 0.0f, 1.8f, MatKind::EmissiveGlow);
        DrawCylinderWires({poi.pos.x, 0.09f, poi.pos.z}, poi.radius * 0.74f, poi.radius * 0.74f, 2.2f, 22,
                          toRayColor(poi.markerColor));
    }

    if (world.mission().kind != ActiveMissionKind::None) {
        const Vec2 mp = world.mission().currentObjectivePos;
        const float r = std::max(2.6f, world.mission().objectiveRadius * 0.65f);
        drawModelStyled(cylModel_, {mp.x, 0.06f, mp.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {r, 6.8f, r}, Color{255, 218, 56, 92},
                        0.1f, 0.0f, 2.1f, MatKind::EmissiveGlow);
        DrawCylinderWires({mp.x, 0.07f, mp.z}, r, r, 6.8f, 24, Color{255, 228, 82, 235});
    }

    for (const auto& pk : world.pickups()) {
        if (pk.collected) continue;
        const float y = 0.62f + 0.18f * std::sin(pk.bobPhase);
        rlPushMatrix();
        rlTranslatef(pk.pos.x, y, pk.pos.z);
        rlRotatef(pk.bobPhase * kRadToDeg, 0.0f, 1.0f, 0.0f);
        if (pk.kind == PickupKind::CashBundle) {
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.56f, 0.24f, 0.36f}, Color{72, 232, 98, 255},
                            0.25f, 0.1f, 1.2f, MatKind::EmissiveGlow);
        } else if (pk.kind == PickupKind::HealthKit) {
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.52f, 0.42f, 0.52f}, Color{245, 245, 248, 255},
                            0.35f, 0.1f, 0.4f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.55f, 0.14f, 0.18f}, Color{245, 48, 48, 255},
                            0.2f, 0.0f, 1.6f, MatKind::EmissiveGlow);
        } else {
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.74f, 0.52f, 0.26f}, Color{255, 206, 54, 255},
                            0.22f, 0.75f, 1.5f, MatKind::EmissiveGlow);
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
        rlTranslatef(mid.x, 0.014f, mid.z);
        rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
        DrawCube({0.0f, 0.0f, 0.0f}, len + 0.12f, 0.008f, 0.24f, Color{14, 14, 18, 175});
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
        const bool fiery = (p.color.r > 210 && p.color.g > 90 && p.color.b < 140);
        drawModelStyled(sphereModel_, {p.pos.x, p.pos.y, p.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {p.size * 0.65f, p.size * 0.65f, p.size * 0.65f}, c,
                        0.65f, 0.0f, fiery ? 2.2f : 0.0f,
                        fiery ? MatKind::EmissiveGlow : MatKind::Standard);
    }
}

void Renderer3D::drawGpsOverheadArrow(const GameWorld& world) {
    if (world.mission().kind == ActiveMissionKind::None) return;

    const Vec2 origin = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
    const Vec2 delta = world.mission().currentObjectivePos - origin;
    if (delta.length() < 8.0f) return;

    const float yaw = std::atan2(delta.z, delta.x);
    rlPushMatrix();
    rlTranslatef(origin.x, 3.55f, origin.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
    drawModelStyled(cubeModel_, {-0.25f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.95f, 0.16f, 0.34f}, Color{255, 216, 48, 240},
                    0.15f, 0.0f, 1.8f, MatKind::EmissiveGlow);
    drawModelStyled(coneModel_, {0.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                    {0.44f, 0.65f, 0.44f}, Color{255, 228, 72, 245},
                    0.15f, 0.0f, 2.0f, MatKind::EmissiveGlow);
    rlPopMatrix();
}

} // namespace oc
