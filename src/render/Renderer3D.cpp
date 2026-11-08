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

        float islandDist = max(abs(wp.x), abs(wp.y)) - 375.0;
        float shoreProx = 1.0 - clamp(islandDist / 42.0, 0.0, 1.0);
        albedo = mix(deepWater, shallowTurquoise, shoreProx * 0.78);

        // Animated shoreline surf foam
        if (islandDist > -6.0 && islandDist < 16.0) {
            float surfWave = sin(islandDist * 0.85 - animTime * 2.8 + valueNoise(wp * 0.25) * 3.0);
            float foam = smoothstep(0.55, 0.92, surfWave) * (1.0 - clamp(islandDist / 16.0, 0.0, 1.0));
            albedo = mix(albedo, vec3(0.78, 0.84, 0.88), foam * 0.75);
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

        // Crisp solar reflection highlight on ocean waves, car hoods, and glass windshields
        float sunRefl = pow(max(dot(R, L), 0.0), (matId == 6) ? 180.0 : 96.0);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : 1.35);

        float coatStrength = (matId == 6) ? 0.88 : ((matId == 8) ? 0.78 : ((matId == 7) ? 0.58 : 0.44));
        float coatFresnel = 0.06 + 0.94 * pow(1.0 - ndotv, 4.2);
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

// Procedural mesh builder that guarantees outward-facing CCW triangles & exact facet normals
struct ProcTriMeshBuilder {
    std::vector<Vector3> pos;
    std::vector<Vector3> norm;
    std::vector<Vector2> uv;

    void addQuadOutward(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outwardHint) {
        Vector3 e1 = Vector3Subtract(b, a);
        Vector3 e2 = Vector3Subtract(c, a);
        Vector3 n = Vector3Normalize(Vector3CrossProduct(e1, e2));
        if (Vector3DotProduct(n, outwardHint) < 0.0f) {
            std::swap(b, d);
            n = Vector3Negate(n);
        }
        pos.push_back(a); pos.push_back(b); pos.push_back(c);
        pos.push_back(a); pos.push_back(c); pos.push_back(d);
        for (int i = 0; i < 6; ++i) norm.push_back(n);
        uv.push_back({0.0f, 0.0f}); uv.push_back({1.0f, 0.0f}); uv.push_back({1.0f, 1.0f});
        uv.push_back({0.0f, 0.0f}); uv.push_back({1.0f, 1.0f}); uv.push_back({0.0f, 1.0f});
    }

    void addTriOutward(Vector3 a, Vector3 b, Vector3 c, Vector3 outwardHint) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        if (Vector3DotProduct(n, outwardHint) < 0.0f) {
            std::swap(b, c);
            n = Vector3Negate(n);
        }
        pos.push_back(a); pos.push_back(b); pos.push_back(c);
        for (int i = 0; i < 3; ++i) norm.push_back(n);
        uv.push_back({0.0f, 0.0f}); uv.push_back({1.0f, 0.0f}); uv.push_back({0.5f, 1.0f});
    }

    Mesh upload() const {
        Mesh m{};
        const int vCount = static_cast<int>(pos.size());
        m.vertexCount = vCount;
        m.triangleCount = vCount / 3;
        m.vertices  = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 3 * sizeof(float))));
        m.normals   = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 3 * sizeof(float))));
        m.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vCount * 2 * sizeof(float))));
        for (int i = 0; i < vCount; ++i) {
            m.vertices[i * 3 + 0] = pos[static_cast<std::size_t>(i)].x;
            m.vertices[i * 3 + 1] = pos[static_cast<std::size_t>(i)].y;
            m.vertices[i * 3 + 2] = pos[static_cast<std::size_t>(i)].z;
            m.normals[i * 3 + 0]  = norm[static_cast<std::size_t>(i)].x;
            m.normals[i * 3 + 1]  = norm[static_cast<std::size_t>(i)].y;
            m.normals[i * 3 + 2]  = norm[static_cast<std::size_t>(i)].z;
            m.texcoords[i * 2 + 0] = uv[static_cast<std::size_t>(i)].x;
            m.texcoords[i * 2 + 1] = uv[static_cast<std::size_t>(i)].y;
        }
        UploadMesh(&m, false);
        return m;
    }
};

// Builds a sculpted trapezoidal car greenhouse/cabin or sloped nose wedge in [-0.5, 0.5]^3
Mesh buildTaperedPrismMesh(float frontTopX, float rearTopX, float topHalfZ, float frontBotY = -0.5f, float frontTopY = 0.5f) {
    ProcTriMeshBuilder b;
    const Vector3 bFL{ 0.5f, frontBotY, -0.5f};
    const Vector3 bFR{ 0.5f, frontBotY,  0.5f};
    const Vector3 bRR{-0.5f, -0.5f,      0.5f};
    const Vector3 bRL{-0.5f, -0.5f,     -0.5f};

    const Vector3 tFL{frontTopX, frontTopY, -topHalfZ};
    const Vector3 tFR{frontTopX, frontTopY,  topHalfZ};
    const Vector3 tRR{rearTopX,  0.5f,       topHalfZ};
    const Vector3 tRL{rearTopX,  0.5f,      -topHalfZ};

    b.addQuadOutward(tFL, tRL, tRR, tFR, { 0.0f,  1.0f,  0.0f}); // Roof
    b.addQuadOutward(bFL, bFR, bRR, bRL, { 0.0f, -1.0f,  0.0f}); // Floor
    b.addQuadOutward(bFL, bFR, tFR, tFL, { 1.0f,  0.4f,  0.0f}); // Angled front windshield / nose
    b.addQuadOutward(bRR, bRL, tRL, tRR, {-1.0f,  0.4f,  0.0f}); // Angled rear backlight
    b.addQuadOutward(bFR, bRR, tRR, tFR, { 0.0f,  0.25f, 1.0f}); // Right side window
    b.addQuadOutward(bRL, bFL, tFL, tRL, { 0.0f,  0.25f,-1.0f}); // Left side window
    return b.upload();
}

// Builds a smooth 20-segment wheel cylinder centered at (0,0,0) along the Z axis (radius=1, z in [-0.5, +0.5])
Mesh buildCenteredWheelMesh(int slices) {
    ProcTriMeshBuilder b;
    for (int i = 0; i < slices; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * kTwoPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * kTwoPi;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);

        // Tread quad
        b.addQuadOutward({c0, s0, -0.5f}, {c1, s1, -0.5f}, {c1, s1, 0.5f}, {c0, s0, 0.5f}, {cm, sm, 0.0f});
        // +Z & -Z caps
        b.addTriOutward({0.0f, 0.0f,  0.5f}, {c0, s0,  0.5f}, {c1, s1,  0.5f}, {0.0f, 0.0f,  1.0f});
        b.addTriOutward({0.0f, 0.0f, -0.5f}, {c1, s1, -0.5f}, {c0, s0, -0.5f}, {0.0f, 0.0f, -1.0f});
    }
    return b.upload();
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

    // 5) Shared GPU-resident primitive & sculpted vehicle meshes
    cubeModel_       = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    cylModel_        = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));
    coneModel_       = LoadModelFromMesh(GenMeshCone(1.0f, 1.0f, 16));
    sphereModel_     = LoadModelFromMesh(GenMeshSphere(1.0f, 14, 16));
    skyDomeModel_    = LoadModelFromMesh(GenMeshSphere(1.0f, 24, 32));
    cabinCoupeModel_ = LoadModelFromMesh(buildTaperedPrismMesh( 0.10f, -0.36f, 0.39f));
    cabinSedanModel_ = LoadModelFromMesh(buildTaperedPrismMesh( 0.20f, -0.30f, 0.42f));
    cabinSuvModel_   = LoadModelFromMesh(buildTaperedPrismMesh( 0.30f, -0.46f, 0.45f));
    hoodWedgeModel_  = LoadModelFromMesh(buildTaperedPrismMesh( 0.48f, -0.50f, 0.48f, -0.5f, 0.15f));
    wheelModel_      = LoadModelFromMesh(buildCenteredWheelMesh(20));

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
    UnloadModel(cabinCoupeModel_);
    UnloadModel(cabinSedanModel_);
    UnloadModel(cabinSuvModel_);
    UnloadModel(hoodWedgeModel_);
    UnloadModel(wheelModel_);

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
    cubeModel_.materials[0].shader       = shader;
    cylModel_.materials[0].shader        = shader;
    coneModel_.materials[0].shader       = shader;
    sphereModel_.materials[0].shader     = shader;
    cabinCoupeModel_.materials[0].shader = shader;
    cabinSedanModel_.materials[0].shader = shader;
    cabinSuvModel_.materials[0].shader   = shader;
    hoodWedgeModel_.materials[0].shader  = shader;
    wheelModel_.materials[0].shader      = shader;
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

    // Player character when on foot
    if (world.player().drivingVehicleId < 0) {
        const auto& pl = world.player();
        const float gY = world.city().isOnRoad(pl.pos, 0.1f) ? 0.0f : 0.08f;
        drawCharacter3D(pl.pos, gY, pl.facingYaw, pl.walkAnimPhase,
                        {34, 118, 226, 255}, {28, 32, 42, 255}, {236, 196, 162, 255},
                        !pl.wasted, pl.currentWeapon != WeaponId::Fists,
                        pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch, true, shadowPass);
    }

    // Pedestrians & Gangsters
    const float npcCull = shadowPass ? (110.0f * 110.0f) : (185.0f * 185.0f);
    for (const auto& npc : world.npcs()) {
        if ((npc.pos - focus).lengthSq() > npcCull) continue;
        const float gY = world.city().isOnRoad(npc.pos, 0.1f) ? 0.0f : 0.08f;
        drawCharacter3D(npc.pos, gY, npc.facingYaw, npc.animPhase,
                        npc.outfitTop, npc.outfitBottom, npc.skinColor,
                        npc.alive, npc.role == NpcRole::Gangster,
                        WeaponId::Pistol, 0.0f, 0.0f, npc.role == NpcRole::Gangster, shadowPass);
    }

    if (!shadowPass) {
        drawPoisAndPickups(world);
        drawSkidsTracersParticles(world);
        drawGpsOverheadArrow(world);
    }
}

void Renderer3D::drawOceanAndGround(const GameWorld& world) {
    (void)world;
    // Animated coastal ocean water plane (MatKind::OceanWater)
    drawModelStyled(cubeModel_, {0.0f, -1.35f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {2200.0f, 1.0f, 2200.0f}, Color{24, 108, 162, 255},
                    0.08f, 0.05f, 0.0f, MatKind::OceanWater);

    // Golden California sandy beach perimeter
    drawModelStyled(cubeModel_, {0.0f, -0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {CityMap::kHalfCitySpan * 2.08f, 0.88f, CityMap::kHalfCitySpan * 2.08f},
                    Color{224, 204, 158, 255}, 0.88f, 0.0f, 0.0f, MatKind::Standard);

    // Coastal promenade concrete seawall apron
    drawModelStyled(cubeModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {CityMap::kHalfCitySpan * 1.97f, 0.42f, CityMap::kHalfCitySpan * 1.97f},
                    Color{166, 170, 176, 255}, 0.78f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
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

    const float bldCull = shadowPass ? (185.0f * 185.0f) : (375.0f * 375.0f);
    const bool night = world.isNightTime();

    for (const auto& b : city.buildings()) {
        const Vec2 c = b.footprint.center();
        if ((c - focus).lengthSq() > bldCull) continue;
        const Vec2 he = b.footprint.halfExtents();

        MatKind facadeKind = MatKind::MasonryFacade;
        float rough = 0.65f;
        float metal = 0.08f;
        if (b.style == BuildingStyle::GlassTower) {
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

        // Crown cornice (glows softly as architectural LED trim on Downtown GlassTowers at night!)
        const bool crownGlow = (!shadowPass && night && b.style == BuildingStyle::GlassTower);
        drawModelStyled(cubeModel_, {c.x, b.height + 0.30f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.05f, 0.60f, he.z * 2.05f}, toRayColor(b.accentColor),
                        0.35f, 0.30f, crownGlow ? 1.15f : 0.0f,
                        crownGlow ? MatKind::EmissiveGlow : MatKind::Standard);

        // Rooftop HVAC mechanical unit on mid/high-rise roofs
        if (!shadowPass && b.upperTierHeight <= 0.5f && b.height > 15.0f && he.x > 6.5f) {
            drawModelStyled(cubeModel_, {c.x + he.x * 0.32f, b.height + 1.15f, c.z - he.z * 0.28f},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {3.4f, 1.6f, 2.8f},
                            Color{116, 122, 130, 255}, 0.62f, 0.35f, 0.0f, MatKind::IndustrialMetal);
        }

        // Stepped upper skyscraper tier + spire antenna + blinking aviation beacon
        if (b.upperTierHeight > 0.5f) {
            const float uy = b.height + b.upperTierHeight * 0.5f;
            drawModelStyled(cubeModel_, {c.x, uy, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.36f, b.upperTierHeight, he.z * 1.36f}, toRayColor(b.primaryColor),
                            rough, metal, 0.0f, facadeKind);

            const float topY = b.height + b.upperTierHeight;
            drawModelStyled(cubeModel_, {c.x, topY + 0.22f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.40f, 0.44f, he.z * 1.40f}, toRayColor(b.accentColor),
                            0.35f, 0.30f, crownGlow ? 1.25f : 0.0f,
                            crownGlow ? MatKind::EmissiveGlow : MatKind::Standard);

            drawModelStyled(cylModel_, {c.x, topY, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.24f, 7.2f, 0.24f}, Color{205, 212, 224, 255},
                            0.28f, 0.80f, 0.0f, MatKind::Standard);

            if (!shadowPass) {
                const float blink = 0.5f + 0.5f * std::sin(animT * 3.4f + c.x * 0.13f);
                if (blink > 0.35f) {
                    drawModelStyled(sphereModel_, {c.x, topY + 7.35f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.34f, 0.34f, 0.34f}, Color{255, 48, 42, 255},
                                    0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                }
            }
        }

        // Glowing Neon Storefront Signs & Canopies for Weapon Shop & Tune Garage
        if (!shadowPass && (b.style == BuildingStyle::ShopBuilding || b.style == BuildingStyle::GarageBuilding)) {
            const Color signCol = (b.style == BuildingStyle::ShopBuilding)
                                      ? Color{255, 78, 48, 255}
                                      : Color{42, 218, 255, 255};
            drawModelStyled(cubeModel_, {c.x, 5.3f, b.footprint.min.z - 0.28f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.55f, 1.45f, 0.38f}, signCol,
                            0.15f, 0.0f, 2.1f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {c.x, 3.65f, b.footprint.min.z - 0.95f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.62f, 0.24f, 1.8f}, Color{36, 40, 48, 255},
                            0.45f, 0.40f, 0.0f, MatKind::Standard);
        }
    }
}

void Renderer3D::drawTreesAndProps(const GameWorld& world, const Vec2& focus, bool shadowPass) {
    const float treeCull = shadowPass ? (135.0f * 135.0f) : (235.0f * 235.0f);

    for (const auto& t : world.city().trees()) {
        if ((t.pos - focus).lengthSq() > treeCull) continue;

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
    const float propCull = shadowPass ? (105.0f * 105.0f) : (175.0f * 175.0f);

    for (const auto& prop : world.city().streetProps()) {
        if ((prop.pos - focus).lengthSq() > propCull) continue;
        if (shadowPass && prop.kind != StreetPropKind::StreetLight && prop.kind != StreetPropKind::Crate) {
            continue;
        }

        const float tilt = prop.broken ? 78.0f : 0.0f;
        rlPushMatrix();
        rlTranslatef(prop.pos.x, 0.06f, prop.pos.z);
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
        }
        rlPopMatrix();
    }
}

void Renderer3D::drawVehicle3D(const Vehicle& v, bool nightMode, bool shadowPass) {
    const auto& spec = getVehicleSpec(v.kind);
    const float L = spec.halfLength * 2.0f;
    const float W = spec.halfWidth * 2.0f;
    const float H = spec.chassisHeight;

    // Soft contact ground shadow directly beneath chassis
    if (!shadowPass) {
        rlPushMatrix();
        rlTranslatef(v.pos.x, 0.015f, v.pos.z);
        rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
        DrawCube({0.0f, 0.0f, 0.0f}, L * 1.03f, 0.008f, W * 1.04f, Color{8, 10, 14, 135});
        rlPopMatrix();
    }

    rlPushMatrix();
    rlTranslatef(v.pos.x, 0.0f, v.pos.z);
    rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(v.bodyPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(v.bodyRoll * kRadToDeg, 1.0f, 0.0f, 0.0f);

    const Color bodyCol = v.destroyed ? Color{46, 44, 42, 255} : toRayColor(v.paintColor);
    const Color darkTrim{26, 29, 36, 255};
    const Color chromeCol{215, 222, 232, 255};
    const Color smokedGlass{18, 28, 42, 255};
    const MatKind paintMat = v.destroyed ? MatKind::Standard : MatKind::CarClearcoat;
    const float paintRough = v.destroyed ? 0.88f : 0.13f;
    const float paintMetal = v.destroyed ? 0.15f : 0.42f;

    // 1) Sculpted lower body, carbon/dark side skirts & sloped front aerodynamic nose
    const float lowerH = H * 0.52f;
    const float baseClearance = 0.25f;

    // Underbody & side rocker skirts
    drawModelStyled(cubeModel_, {0.0f, baseClearance, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {L * 1.01f, 0.13f, W * 1.01f}, darkTrim,
                    0.52f, 0.25f, 0.0f, MatKind::Standard);

    // Main lower beltline body (clears both front & rear wheel arches cleanly)
    drawModelStyled(cubeModel_, {-L * 0.05f, baseClearance + lowerH * 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {L * 0.90f, lowerH, W}, bodyCol,
                    paintRough, paintMetal, 0.0f, paintMat);

    // Aerodynamic sloped front nose wedge ahead of front axle
    drawModelStyled(hoodWedgeModel_, {L * 0.42f, baseClearance + lowerH * 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {L * 0.16f, lowerH, W * 0.99f}, bodyCol,
                    paintRough, paintMetal, 0.0f, paintMat);

    // Front bumper splitter & grille intake
    if (!shadowPass) {
        drawModelStyled(cubeModel_, {L * 0.495f, baseClearance + 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.07f, lowerH * 0.48f, W * 0.54f}, Color{18, 20, 26, 255},
                        0.40f, 0.55f, 0.0f, MatKind::Standard);
        // Rear license plate & dual chrome exhaust pipes
        drawModelStyled(cubeModel_, {-L * 0.502f, baseClearance + 0.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.04f, 0.15f, 0.38f}, Color{236, 240, 246, 255},
                        0.45f, 0.10f, 0.0f, MatKind::Standard);
        drawModelStyled(wheelModel_, {-L * 0.51f, baseClearance + 0.02f, -W * 0.26f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {0.055f, 0.055f, 0.18f}, chromeCol, 0.18f, 0.90f, 0.0f, MatKind::Standard);
        drawModelStyled(wheelModel_, {-L * 0.51f, baseClearance + 0.02f,  W * 0.26f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {0.055f, 0.055f, 0.18f}, chromeCol, 0.18f, 0.90f, 0.0f, MatKind::Standard);
    }

    // Center racing stripe on SportsCoupe & MuscleCar
    if (!shadowPass && (v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::MuscleCar) && !v.destroyed) {
        drawModelStyled(cubeModel_, {-L * 0.04f, baseClearance + lowerH + 0.008f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {L * 0.88f, 0.018f, W * 0.20f}, toRayColor(v.stripeColor),
                        0.15f, 0.30f, 0.0f, MatKind::CarClearcoat);
    }
    // MuscleCar supercharger hood scoop
    if (v.kind == VehicleKind::MuscleCar) {
        drawModelStyled(cabinSedanModel_, {L * 0.22f, baseClearance + lowerH + 0.07f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.72f, 0.15f, 0.52f}, darkTrim, 0.32f, 0.65f, 0.0f, MatKind::Standard);
    }

    // 2) Sculpted Sloped Cabin + Flush Reflective Windshields & Framed A/B/C Pillars
    Model& cabMdl = (v.kind == VehicleKind::SportsCoupe)
                        ? cabinCoupeModel_
                        : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::DeliveryVan)
                               ? cabinSuvModel_
                               : cabinSedanModel_);

    const float cabLen = (v.kind == VehicleKind::DeliveryVan) ? L * 0.72f
                       : ((v.kind == VehicleKind::SUV) ? L * 0.66f : L * 0.54f);
    const float cabOffset = (v.kind == VehicleKind::SportsCoupe) ? -L * 0.06f
                          : ((v.kind == VehicleKind::DeliveryVan) ? -L * 0.08f : -L * 0.04f);
    const float cabH = H * 0.50f;
    const float cabW = W * 0.88f;
    const float cabY = baseClearance + lowerH + cabH * 0.48f;

    // Outer painted roof & corner pillars
    drawModelStyled(cabMdl, {cabOffset, cabY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {cabLen, cabH, cabW}, bodyCol,
                    paintRough, paintMetal, 0.0f, paintMat);

    if (!shadowPass) {
        // Angled front windshield & rear window glass
        drawModelStyled(cabMdl, {cabOffset, cabY - 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {cabLen * 1.016f, cabH * 0.78f, cabW * 0.84f}, smokedGlass,
                        0.06f, 0.15f, 0.0f, MatKind::CarGlass);

        // Tapered side windows (leaving painted A/C pillars & roof rail visible!)
        if (v.kind != VehicleKind::DeliveryVan) {
            drawModelStyled(cabMdl, {cabOffset, cabY - 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {cabLen * 0.82f, cabH * 0.76f, cabW * 1.016f}, smokedGlass,
                            0.06f, 0.15f, 0.0f, MatKind::CarGlass);
        }

        // Aerodynamic side wing mirrors
        const float mirrorX = cabOffset + cabLen * 0.28f;
        drawModelStyled(cubeModel_, {mirrorX, cabY - cabH * 0.16f, -W * 0.49f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.10f, 0.16f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
        drawModelStyled(cubeModel_, {mirrorX, cabY - cabH * 0.16f,  W * 0.49f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.10f, 0.16f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
    }

    // 3) Rear GT Wing Spoiler / SUV Spare Tire / Illuminated Taxi Sign
    if (spec.hasSpoiler) {
        drawModelStyled(cubeModel_, {-L * 0.44f, baseClearance + lowerH + 0.18f, -W * 0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.14f, 0.24f, 0.06f}, darkTrim, 0.35f, 0.50f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-L * 0.44f, baseClearance + lowerH + 0.18f,  W * 0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.14f, 0.24f, 0.06f}, darkTrim, 0.35f, 0.50f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-L * 0.45f, baseClearance + lowerH + 0.31f,  0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.32f, 0.055f, W * 0.96f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
    }
    if (v.kind == VehicleKind::SUV && !shadowPass) {
        drawModelStyled(wheelModel_, {-L * 0.51f, baseClearance + lowerH * 0.68f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {0.34f, 0.34f, 0.20f}, Color{24, 26, 30, 255}, 0.72f, 0.10f, 0.0f, MatKind::Standard);
    }
    if (v.kind == VehicleKind::Taxi && !v.destroyed && !shadowPass) {
        drawModelStyled(cubeModel_, {cabOffset, cabY + cabH * 0.5f + 0.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.40f, 0.18f, 0.66f}, Color{255, 232, 118, 255},
                        0.2f, 0.0f, 1.8f, MatKind::EmissiveGlow);
    }

    // 4) Round 20-Segment 3D Cylindrical Tires + Metallic Alloy Rims & Spokes
    const float axleX = L * 0.33f;
    const float axleZ = W * 0.48f;
    const float wheelR = (v.kind == VehicleKind::SUV) ? 0.38f : 0.34f;
    for (int side = -1; side <= 1; side += 2) {
        for (int front = -1; front <= 1; front += 2) {
            rlPushMatrix();
            rlTranslatef(static_cast<float>(front) * axleX, wheelR, static_cast<float>(side) * axleZ);
            if (front > 0) {
                rlRotatef(-v.steerVisual * 28.0f, 0.0f, 1.0f, 0.0f);
            }
            rlRotatef(-v.wheelSpin * kRadToDeg, 0.0f, 0.0f, 1.0f);

            // Rubber tire cylinder
            drawModelStyled(wheelModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {wheelR, wheelR, 0.25f}, Color{22, 24, 28, 255},
                            0.78f, 0.05f, 0.0f, MatKind::Standard);

            if (!shadowPass) {
                // Brushed alloy outer rim disc + twin cross spokes so wheel spin is crisp!
                const float zOff = static_cast<float>(side) * 0.022f;
                drawModelStyled(wheelModel_, {0.0f, 0.0f, zOff}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {wheelR * 0.68f, wheelR * 0.68f, 0.23f}, chromeCol,
                                0.22f, 0.85f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, static_cast<float>(side) * 0.125f}, {0.0f, 0.0f, 1.0f}, 0.0f,
                                {wheelR * 1.22f, 0.07f, 0.03f}, Color{62, 68, 78, 255},
                                0.30f, 0.80f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, static_cast<float>(side) * 0.125f}, {0.0f, 0.0f, 1.0f}, 90.0f,
                                {wheelR * 1.22f, 0.07f, 0.03f}, Color{62, 68, 78, 255},
                                0.30f, 0.80f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();
        }
    }

    // 5) Emissive Xenon/LED Headlights, Taillight Bars & Twin Nitrous Afterburners
    if (!shadowPass && !v.destroyed) {
        const Color headCol = nightMode ? Color{245, 248, 255, 255} : Color{230, 240, 252, 255};
        const float headGlow = nightMode ? 2.6f : 1.1f;
        drawModelStyled(cubeModel_, {L * 0.495f, baseClearance + lowerH * 0.56f, -W * 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.07f, 0.13f, 0.28f}, headCol, 0.1f, 0.0f, headGlow, MatKind::EmissiveGlow);
        drawModelStyled(cubeModel_, {L * 0.495f, baseClearance + lowerH * 0.56f,  W * 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.07f, 0.13f, 0.28f}, headCol, 0.1f, 0.0f, headGlow, MatKind::EmissiveGlow);

        if (nightMode && (v.isPlayerDriving || v.hasNpcDriver)) {
            DrawCube({L * 0.5f + 4.8f, 0.03f, 0.0f}, 8.8f, 0.015f, W * 1.85f, Color{235, 242, 255, 42});
        }

        const Color tailCol = v.brakingVisual ? Color{255, 42, 38, 255} : Color{205, 28, 34, 255};
        const float tailGlow = v.brakingVisual ? 2.8f : 1.4f;
        if (v.kind == VehicleKind::SportsCoupe) {
            // Modern full-width supercar LED rear lightbar!
            drawModelStyled(cubeModel_, {-L * 0.502f, baseClearance + lowerH * 0.72f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.09f, W * 0.86f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
        } else {
            drawModelStyled(cubeModel_, {-L * 0.502f, baseClearance + lowerH * 0.68f, -W * 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.14f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-L * 0.502f, baseClearance + lowerH * 0.68f,  W * 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.14f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
        }

        if (v.nitroActive) {
            drawModelStyled(coneModel_, {-L * 0.52f, baseClearance + 0.02f, -W * 0.26f}, {0.0f, 0.0f, 1.0f}, 90.0f,
                            {0.11f, 0.92f, 0.11f}, Color{68, 212, 255, 255},
                            0.1f, 0.0f, 3.2f, MatKind::EmissiveGlow);
            drawModelStyled(coneModel_, {-L * 0.52f, baseClearance + 0.02f,  W * 0.26f}, {0.0f, 0.0f, 1.0f}, 90.0f,
                            {0.11f, 0.92f, 0.11f}, Color{68, 212, 255, 255},
                            0.1f, 0.0f, 3.2f, MatKind::EmissiveGlow);
        }
    }

    rlPopMatrix();
}

void Renderer3D::drawCharacter3D(const Vec2& pos, float groundY, float yaw, float animPhase,
                                 ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                                 bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                                 float aimPitch, bool hasSunglasses, bool shadowPass) {
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
    const Color shoeC{26, 28, 34, 255};

    if (!shadowPass && alive) {
        DrawCylinder({0.0f, 0.012f, 0.0f}, 0.36f, 0.36f, 0.008f, 12, Color{10, 12, 16, 115});
    }

    // Left & Right Legs + Sneakers/Boots
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

    // Belt + Sculpted Jacket/Torso + Inner Shirt Collar
    drawModelStyled(cubeModel_, {0.0f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.27f, 0.08f, 0.43f}, Color{30, 32, 38, 255}, 0.52f, 0.20f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 1.09f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.27f, 0.60f, 0.44f}, topC, 0.68f, 0.04f, 0.0f, MatKind::Standard);
    if (!shadowPass) {
        drawModelStyled(cubeModel_, {0.11f, 1.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.08f, 0.18f, 0.18f}, Color{235, 238, 244, 255},
                        0.72f, 0.0f, 0.0f, MatKind::Standard);
    }

    // Neck + Rounded Head + Hair + Optional Sunglasses
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
                    {0.27f, 0.085f, 0.27f}, Color{36, 30, 26, 255}, 0.75f, 0.0f, 0.0f, MatKind::Standard);
    if (!shadowPass && hasSunglasses) {
        drawModelStyled(cubeModel_, {0.125f, 0.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.03f, 0.06f, 0.22f}, Color{16, 20, 28, 255},
                        0.12f, 0.45f, 0.0f, MatKind::CarGlass);
    }
    rlPopMatrix();

    // Arms, Hands & Multi-Part 3D Weapons
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 1.32f, static_cast<float>(side) * 0.27f);
        if (armed && side == 1 && alive) {
            rlRotatef(aimPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.22f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.44f, 0.14f, 0.14f}, topC, 0.68f, 0.04f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.46f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 0.11f, 0.11f}, skinC, 0.65f, 0.0f, 0.0f, MatKind::Standard);

            const bool longGun = (weapon == WeaponId::AssaultRifle || weapon == WeaponId::Shotgun);
            const float gunLen = longGun ? 0.64f : (weapon == WeaponId::SMG ? 0.44f : 0.32f);
            // Receiver & metallic barrel
            drawModelStyled(cubeModel_, {0.55f, 0.025f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {gunLen, 0.10f, 0.075f}, Color{34, 38, 46, 255},
                            0.28f, 0.82f, 0.0f, MatKind::Standard);
            if (!shadowPass && (weapon == WeaponId::SMG || weapon == WeaponId::AssaultRifle)) {
                drawModelStyled(cubeModel_, {0.56f, -0.09f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.07f, 0.16f, 0.055f}, Color{24, 26, 32, 255},
                                0.35f, 0.70f, 0.0f, MatKind::Standard);
            }
            if (!shadowPass && muzzleFlash > 0.0f) {
                drawModelStyled(sphereModel_, {0.58f + gunLen * 0.5f, 0.03f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.19f, 0.14f, 0.14f}, Color{255, 224, 92, 255},
                                0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
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
