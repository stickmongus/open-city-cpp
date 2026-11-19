#include "render/Renderer3D.hpp"
#include "render/GeneratedRealisticAssets.hpp"

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
uniform float wetWeather; // [0, 1] Rain / Wet-Road atmospheric overcast factor

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

    if (wetWeather > 0.01) {
        vec3 stormZenith = mix(vec3(0.008, 0.014, 0.032), vec3(0.075, 0.110, 0.175), dayFactor);
        vec3 stormHoriz  = mix(vec3(0.024, 0.038, 0.072), vec3(0.180, 0.235, 0.310), dayFactor);
        zenithCol = mix(zenithCol, stormZenith, wetWeather * 0.65);
        horizCol  = mix(horizCol, stormHoriz, wetWeather * 0.55);
    }

    float skyGrad = pow(1.0 - upPos, 2.35);
    vec3 sky = mix(zenithCol, horizCol, skyGrad);

    // Warm directional Mie scattering around the sun / sunset glow
    float sunCos = max(dot(dir, S), 0.0);
    vec3 mieTint = mix(vec3(1.0, 0.86, 0.62), vec3(1.0, 0.42, 0.14), golden);
    if (night > 0.55) {
        mieTint = vec3(0.25, 0.40, 0.68);
    }
    sky += mieTint * pow(sunCos, 6.0) * (0.28 + 0.45 * golden) * (1.0 - night * 0.65) * (1.0 - wetWeather * 0.45);
    sky += mieTint * pow(sunCos, 42.0) * (0.45 + 0.35 * golden) * (1.0 - night * 0.55) * (1.0 - wetWeather * 0.55);

    // Crisp Solar / Lunar Disc
    if (dir.y > -0.02) {
        float sunDisc = smoothstep(0.99915, 0.99972, dot(dir, S));
        vec3 discCol = (night > 0.6) ? vec3(1.8, 2.1, 2.6) : mix(vec3(6.5, 5.6, 4.2), vec3(7.0, 2.8, 0.9), golden);
        sky += discCol * sunDisc * (1.0 - wetWeather * 0.65);
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
            sky += vec3(0.75, 0.86, 1.0) * star * 1.4 * (1.0 - wetWeather * 0.75);
        }
    }

    // Volumetric-style 2-layer drifting FBM clouds
    if (dir.y > 0.015) {
        vec2 cloudUv = dir.xz / (dir.y + 0.19) * 1.35;
        vec2 wind = vec2(animTime * 0.014, animTime * 0.007);
        float n1 = fbmClouds(cloudUv + wind);
        float n2 = fbmClouds(cloudUv * 2.15 - wind * 1.4 + vec2(4.2, -2.8));
        float lowThresh = mix(0.49, 0.32, wetWeather);
        float density = smoothstep(lowThresh, 0.80, n1 * 0.72 + n2 * 0.28);
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
        sky = mix(sky, cloudCol, density * (0.82 + 0.12 * wetWeather));
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
// 3) Unified Cook-Torrance GGX PBR + 16-Tap Poisson Soft Shadow +
//    3D Raycasted Interior Mapping + Wet-Road Mirror Puddle Shader
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
uniform float wetRoadFactor;
uniform int enableShadows;

uniform mat4 lightVP;
uniform sampler2D shadowMap;
uniform float shadowTexelSize;

out vec4 finalColor;

const float kPi = 3.14159265359;

// 16-Tap Poisson Disk Soft Shadow Kernel (matched with GeneratedRealisticAssets.hpp)
const vec2 kPoisson16[16] = vec2[16](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

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

    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.03) return 0.0; // Grazing/back-facing surfaces are already self-shadowed by N·L

    float normalOffset = mix(0.085, 0.28, 1.0 - ndotl);
    vec4 lightClip = lightVP * vec4(worldPos + N * normalOffset, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.0001);
    proj = proj * 0.5 + 0.5;

    if (proj.x <= 0.01 || proj.x >= 0.99 || proj.y <= 0.01 || proj.y >= 0.99 || proj.z >= 0.998 || proj.z <= 0.0) {
        return 0.0;
    }

    vec2 edgeDist = min(proj.xy, 1.0 - proj.xy);
    float edgeFade = smoothstep(0.01, 0.09, min(edgeDist.x, edgeDist.y));

    float bias = max(0.0034 * (1.0 - ndotl), 0.00080);

    // Rotated 16-tap Poisson Disk soft shadow filter
    float rotAng = hash12(floor(worldPos.xz * 19.0)) * 6.2831853;
    float cs = cos(rotAng);
    float sn = sin(rotAng);
    mat2 rot = mat2(cs, -sn, sn, cs);
    float filterRadius = shadowTexelSize * 1.75;

    float occ = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 uv = proj.xy + (rot * kPoisson16[i]) * filterRadius;
        float closestDepth = texture(shadowMap, uv).r;
        occ += (proj.z - bias > closestDepth) ? 1.0 : 0.0;
    }
    return (occ / 16.0) * edgeFade;
}

float aaBand(float distFromCenter, float halfWidth) {
    float fw = max(fwidth(distFromCenter), 0.002);
    return 1.0 - smoothstep(halfWidth - fw, halfWidth + fw, distFromCenter);
}

// Procedural 360-degree 3D City Skyline, Neon Sign & Sky Environment Map for reflections
vec3 sampleCityEnvMap(vec3 R, vec3 worldPos, float rough, float nightF) {
    float up = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyEnv = mix(pow(horizonColor, vec3(2.2)) * 0.96, skyAmbient * 2.25, pow(up, 1.30));

    // Attenuate synthetic downtown skyline whenever the reflection ray points out toward open ocean!
    vec2 horizDir = normalize(R.xz + vec2(0.0001));
    vec2 farPos = worldPos.xz + horizDir * 120.0;
    float cityMask = smoothstep(425.0, 310.0, max(abs(farPos.x), abs(farPos.y)));
    if (cityMask <= 0.01) {
        return skyEnv;
    }

    // Synthetic city skyline silhouette + illuminated office windows & neon reflections near the horizon
    float az = atan(R.z, R.x);
    float bldColIdx = floor((az + kPi) / (2.0 * kPi) * 64.0);
    float bldRnd = hash12(vec2(bldColIdx, 3.7));
    float skylineH = (0.04 + 0.24 * pow(bldRnd, 1.5)) * (1.0 - rough * 0.55);

    if (R.y > -0.06 && R.y < skylineH) {
        vec3 bldSil = mix(groundAmbient * 0.45, skyAmbient * 0.34, 0.45);
        float uFreq = (az + kPi) * 32.0;
        float vFreq = R.y * 44.0;
        float detailFade = 1.0 - smoothstep(0.18, 0.72, max(fwidth(uFreq), fwidth(vFreq)) + rough * 1.35);
        if (detailFade > 0.02) {
            float winU = fract(uFreq);
            float winV = fract(vFreq);
            float softEdge = clamp(0.12 + rough * 0.55, 0.12, 0.34);
            float paneMask = smoothstep(0.16, 0.16 + softEdge, winU) * smoothstep(0.84, 0.84 - softEdge, winU) *
                             smoothstep(0.18, 0.18 + softEdge, winV) * smoothstep(0.82, 0.82 - softEdge, winV);
            float winHash = hash12(vec2(bldColIdx * 3.1 + floor(winU * 4.0), floor(vFreq)));
            if (winHash > (0.62 - nightF * 0.30)) {
                vec3 neonPalette = (winHash > 0.88)
                                       ? vec3(0.22, 0.82, 1.25)
                                       : ((winHash > 0.76) ? vec3(1.15, 0.34, 0.88) : vec3(1.22, 0.88, 0.42));
                bldSil += neonPalette * (0.045 + nightF * 1.15) * paneMask * detailFade;
            }
        }
        float mask = smoothstep(skylineH, skylineH - 0.020, R.y) * smoothstep(-0.06, 0.0, R.y);
        skyEnv = mix(skyEnv, bldSil, mask * 0.76 * cityMask);
    }
    return skyEnv;
}

// True 3D Raycasted Interior Mapping inside building windows (virtual 3D rooms with ceiling lights, walls & blinds)
vec3 sampleInteriorRoom3D(vec2 paneUv, vec3 V, vec3 N, float roomSeed, bool isLobby, float nightF) {
    vec3 T = (abs(N.x) > 0.5) ? vec3(0.0, 0.0, sign(N.x)) : vec3(-sign(N.z), 0.0, 0.0);
    vec3 B = vec3(0.0, 1.0, 0.0);

    // View ray entering the room in local room coordinates: x in [0,1], y in [0,1], z in [0, roomDepth]
    vec3 I = normalize(-V);
    float rx = dot(I, T);
    float ry = dot(I, B);
    float rz = max(dot(I, -N), 0.06); // Depth into the building interior

    float roomDepth = isLobby ? 1.15 : 0.78;
    float tx = (rx > 0.0) ? ((1.0 - paneUv.x) / max(rx, 0.001)) : ((-paneUv.x) / min(rx, -0.001));
    float ty = (ry > 0.0) ? ((1.0 - paneUv.y) / max(ry, 0.001)) : ((-paneUv.y) / min(ry, -0.001));
    float tz = roomDepth / rz;
    float tHit = min(min(tx, ty), tz);

    vec3 hitPos = vec3(paneUv, 0.0) + vec3(rx, ry, rz) * tHit;
    float depthFade = 1.0 - clamp(hitPos.z / (roomDepth * 1.15), 0.0, 0.75);

    vec3 warmTone = pow(vec3(1.00, 0.78, 0.44), vec3(2.2));
    vec3 coolTone = pow(vec3(0.52, 0.84, 1.00), vec3(2.2));
    vec3 corpTone = pow(vec3(0.95, 0.92, 0.82), vec3(2.2));
    vec3 suiteTint = (roomSeed > 0.78) ? coolTone : ((roomSeed > 0.44) ? corpTone : warmTone);

    vec3 surfaceCol = suiteTint * 0.42;
    if (abs(tHit - ty) < 0.002) {
        if (ry > 0.0) {
            // Ceiling with glowing recessed LED office light panels!
            float stripX = smoothstep(0.18, 0.24, abs(fract(hitPos.x * 2.0) - 0.5));
            float stripZ = smoothstep(0.15, 0.28, abs(fract(hitPos.z * 2.2) - 0.5));
            float lightPanel = (1.0 - stripX) * (1.0 - stripZ);
            surfaceCol = mix(suiteTint * 0.55, suiteTint * 2.35, lightPanel);
        } else {
            // Hardwood / carpeted floor with desk silhouette
            float tile = 0.85 + 0.15 * sin(hitPos.x * 18.0) * sin(hitPos.z * 18.0);
            surfaceCol = suiteTint * 0.24 * tile;
        }
    } else if (abs(tHit - tz) < 0.002) {
        // Back wall with framed artwork / monitor screen / bookshelves
        float shelf = smoothstep(0.35, 0.45, abs(sin(hitPos.y * 12.0)));
        bool monitorGlow = (hitPos.x > 0.34 && hitPos.x < 0.66 && hitPos.y > 0.30 && hitPos.y < 0.58);
        surfaceCol = suiteTint * (0.48 + 0.18 * shelf);
        if (monitorGlow && fract(roomSeed * 13.7) > 0.35) {
            surfaceCol = mix(surfaceCol, coolTone * 1.45, 0.75);
        }
    } else {
        // Side partition walls
        surfaceCol = suiteTint * 0.38;
    }

    // Venetian blinds partially drawn in upper portion of random offices
    float blindCoverage = fract(roomSeed * 7.9) * 0.52;
    if (!isLobby && paneUv.y > (1.0 - blindCoverage)) {
        float slat = 0.58 + 0.42 * sin(paneUv.y * 52.0);
        surfaceCol *= slat * 0.78;
    }

    float baseBrightness = mix(0.45, 1.32, nightF);
    return surfaceCol * depthFade * baseBrightness;
}

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 srgbBase = texel * colDiffuse * fragColor;
    vec3 albedo = pow(clamp(srgbBase.rgb, 0.0, 1.0), vec3(2.2));

    float roughness = clamp(colSpecular.x, 0.03, 0.98);
    float metallic  = clamp(colSpecular.y, 0.0, 1.0);
    float emissive  = colSpecular.z * 3.5;
    int matId       = int(colSpecular.w * 255.0 + 0.5);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(-lightDir);
    vec3 emissiveColor = albedo;
    float puddleMask = 0.0;

    // -----------------------------------------------------------------------
    // Procedural World-Space Material Detail & Normal Perturbation
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Normal-Mapped Asphalt Road Network + Wet-Road Mirror Puddles + Lane Markings
        float g1 = valueNoise(fragPosition.xz * 3.5);
        float g2 = valueNoise(fragPosition.xz * 18.0);
        float grain = g1 * 0.14 + g2 * 0.08;
        albedo = vec3(0.028 + grain * 0.055, 0.031 + grain * 0.055, 0.038 + grain * 0.065);
        roughness = 0.44 - grain * 0.16;

        // Micro-aggregate asphalt normal perturbation
        float nx = valueNoise(fragPosition.xz * 22.0 + vec2(0.15, 0.0)) - g2;
        float nz = valueNoise(fragPosition.xz * 22.0 + vec2(0.0, 0.15)) - g2;
        N = normalize(vec3(N.x + nx * 0.16, N.y, N.z + nz * 0.16));

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
            roughness = mix(roughness, 0.24, wear * 0.60);
        } else if (onHoriz && !onVert) {
            float wear = exp(-pow((abs(dz - 4.2) - 0.85) / 0.55, 2.0));
            albedo *= (1.0 - 0.16 * wear);
            roughness = mix(roughness, 0.24, wear * 0.60);
        }

        vec3 yellowPaint = pow(vec3(0.93, 0.72, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.88, 0.90, 0.93), vec3(2.2));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;

        if (onVert && !onHoriz) {
            if (dz >= 9.35 && dz <= 12.05 && dx < 7.9) {
                float stripe = abs(fract((p.x - rx + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dz >= 12.45 && dz <= 12.90 && dx < 8.4) {
                whiteMask = max(whiteMask, 0.95);
            }
            if (dz > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dx - 0.18), 0.075));
                if (fract(p.y / 9.0) < 0.38) {
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
                if (fract(p.x / 9.0) < 0.38) {
                    whiteMask = max(whiteMask, aaBand(abs(dz - 4.35), 0.075));
                }
                whiteMask = max(whiteMask, aaBand(abs(dz - 8.55), 0.07) * 0.65);
            }
        } else if (onVert && onHoriz) {
            float mh = length(vec2(dx, dz));
            if (mh < 0.68) {
                albedo = vec3(0.055, 0.052, 0.050);
                roughness = 0.32;
                metallic = 0.62;
            }
        }

        albedo = mix(albedo, whitePaint, clamp(whiteMask, 0.0, 1.0) * 0.90);
        albedo = mix(albedo, yellowPaint, clamp(yellowMask, 0.0, 1.0) * 0.94);

        // Dynamic Rain / Wet-Road Mirror Puddles ([U] toggle or twilight dampness)
        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.22, 0.0, 1.0);
        if (wetAmt > 0.02) {
            float pNoise = valueNoise(p * 0.16) * 0.65 + valueNoise(p * 0.52) * 0.35;
            puddleMask = smoothstep(0.56 - wetAmt * 0.20, 0.72 - wetAmt * 0.14, pNoise) * wetAmt;
            albedo *= (1.0 - wetAmt * 0.28 - puddleMask * 0.25);
            roughness = mix(roughness, 0.045, clamp(wetAmt * 0.45 + puddleMask * 0.85, 0.0, 0.96));
            if (wetRoadFactor > 0.1 && puddleMask > 0.25) {
                // Expanding raindrop ring ripples on puddles
                vec2 ripCell = floor(p * 1.8);
                vec2 ripUv = fract(p * 1.8) - 0.5;
                float ripPhase = fract(animTime * 1.9 + hash12(ripCell));
                float ring = sin((length(ripUv) - ripPhase * 0.45) * 48.0) * exp(-length(ripUv) * 6.0) * (1.0 - ripPhase);
                N = normalize(vec3(ripUv.x * ring * 0.12, 1.0, ripUv.y * ring * 0.12));
            } else {
                N = normalize(mix(N, vec3(0.0, 1.0, 0.0), puddleMask * 0.85));
            }
        }
    }
    else if (matId == 2) {
        // MAT 2: Bevelled Concrete Sidewalk Pavers & Plazas
        vec2 tileUv = fragPosition.xz / 1.75;
        vec2 cell = floor(tileUv);
        vec2 g = abs(fract(tileUv) - 0.5);
        float seamDist = 0.5 - max(g.x, g.y);
        float seam = 1.0 - smoothstep(0.0, max(fwidth(seamDist), 0.015) * 1.4, seamDist - 0.018);
        float slabTint = 0.93 + 0.12 * hash12(cell) + 0.05 * valueNoise(fragPosition.xz * 6.0);
        albedo *= slabTint;
        albedo = mix(albedo, albedo * 0.54, seam * 0.72);
        roughness = mix(0.74, 0.18, wetRoadFactor * 0.75);
        puddleMask = wetRoadFactor * 0.42 * (1.0 - seam);
    }
    else if (matId == 3) {
        // MAT 3: Architectural Glass Curtain-Wall Skyscraper with 3D Raycasted Interior Mapping!
        if (abs(N.y) > 0.55) {
            float roofNoise = valueNoise(fragPosition.xz * 2.5);
            albedo = mix(vec3(0.09, 0.10, 0.12), vec3(0.16, 0.17, 0.19), roofNoise);
            roughness = 0.82;
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
            bool isMullion = (uFrac < 0.070 || uFrac > 0.930);
            bool isSpandrel = isGroundLobby ? (vCoord > 3.72) : (vFrac < 0.13 || vFrac > 0.89);

            if (isMullion || isSpandrel) {
                // Anodized aluminum / brushed steel structural mullion & spandrel frame with 3D bevel normal
                albedo *= 0.30;
                roughness = 0.34;
                metallic = 0.78;
                float bevelX = (uFrac < 0.070) ? -0.22 : ((uFrac > 0.930) ? 0.22 : 0.0);
                float bevelY = (vFrac < 0.13) ? -0.22 : ((vFrac > 0.89) ? 0.22 : 0.0);
                N = normalize(N + vec3(bevelX * N.z, bevelY, -bevelX * N.x));
            } else {
                // 3D Raycasted Interior Mapping inside each skyscraper window bay!
                vec2 paneUv = vec2((uFrac - 0.070) / 0.860,
                                   isGroundLobby ? clamp(vCoord / 3.72, 0.0, 1.0) : ((vFrac - 0.13) / 0.76));
                vec2 bldSeed = floor(fragPosition.xz / 14.0);
                float roomRnd = hash12(vec2(uCell * 1.37 + bldSeed.x * 7.1, vCell * 2.19 + bldSeed.y * 11.3));
                vec3 room3D = sampleInteriorRoom3D(paneUv, V, N, roomRnd, isGroundLobby, nightFactor);

                albedo = mix(albedo * 0.32, room3D, 0.74);
                roughness = 0.06;
                metallic = 0.22;

                float litThreshold = isGroundLobby ? 0.18 : 0.44;
                if (roomRnd > litThreshold) {
                    emissiveColor = room3D;
                    emissive = mix(0.26, isGroundLobby ? 1.28 : 1.02, nightFactor);
                }
            }
        }
    }
    else if (matId == 4) {
        // MAT 4: Modern Office / Precast Stone & Masonry Facade with Recessed 3D Interior Windows
        if (abs(N.y) > 0.55) {
            albedo = vec3(0.14, 0.14, 0.15) * (0.85 + 0.25 * valueNoise(fragPosition.xz * 2.2));
            roughness = 0.84;
        } else {
            float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
            float vCoord = fragPosition.y;
            float uFrac = fract(uCoord / 2.85);
            float vFrac = fract(vCoord / 3.30);
            float uCell = floor(uCoord / 2.85);
            float vCell = floor(vCoord / 3.30);

            bool inWindow = (vCoord > 3.4) && (uFrac > 0.20 && uFrac < 0.80) && (vFrac > 0.22 && vFrac < 0.80);
            bool inSill   = (vCoord > 3.4) && (uFrac > 0.16 && uFrac < 0.84) && (vFrac >= 0.16 && vFrac <= 0.22);
            bool groundShop = (vCoord > 0.35 && vCoord < 2.95) && (uFrac > 0.12 && uFrac < 0.88);

            if (inSill) {
                albedo *= 1.38;
                roughness = 0.64;
                N = normalize(N + vec3(0.0, 0.32, 0.0));
            } else if (inWindow || groundShop) {
                vec2 paneUv = groundShop
                                  ? vec2((uFrac - 0.12) / 0.76, (vCoord - 0.35) / 2.60)
                                  : vec2((uFrac - 0.20) / 0.60, (vFrac - 0.22) / 0.58);
                vec2 seed = floor(fragPosition.xz / 12.0);
                float rnd = hash12(vec2(uCell + seed.x * 5.7, vCell + seed.y * 9.3));
                vec3 room3D = sampleInteriorRoom3D(paneUv, V, N, rnd, groundShop, nightFactor);

                albedo = room3D * 0.68;
                roughness = 0.08;
                metallic = 0.16;
                if (rnd > (groundShop ? 0.24 : 0.48)) {
                    emissiveColor = room3D;
                    emissive = mix(0.22, 0.96, nightFactor);
                }
            } else {
                // Architectural rusticated masonry joints + subtle vertical pilaster fluting
                float hJoint = smoothstep(0.91, 0.98, abs(sin(vCoord * 9.5)));
                float vJoint = smoothstep(0.94, 0.99, abs(sin(uCoord * 4.4)));
                float joint = max(hJoint, vJoint * 0.75);
                albedo *= (0.95 - 0.16 * joint + 0.08 * valueNoise(vec2(uCoord, vCoord) * 2.2));
                roughness = 0.76;
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Corrugated Industrial Steel / Shipping Containers with Normal-Perturbed Ribs
        float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
        if (abs(N.y) < 0.5) {
            float rib = sin(uCoord * 16.0);
            float ribGrad = cos(uCoord * 16.0);
            albedo *= (0.90 + 0.14 * rib);
            N = normalize(N + vec3(N.z * ribGrad * 0.18, 0.0, -N.x * ribGrad * 0.18));
        }
        roughness = 0.38;
        metallic = 0.55;
    }
    else if (matId == 6) {
        // MAT 6: Animated Multi-Wave Coastal Ocean Water with Fresnel Reflection, Sun Glitter & Surf Foam
        vec2 wp = fragPosition.xz;
        float w1 = sin(wp.x * 0.22 + animTime * 1.55 + cos(wp.y * 0.14) * 1.2);
        float w2 = cos(wp.y * 0.29 - animTime * 1.25 + sin(wp.x * 0.19));
        float w3 = sin((wp.x + wp.y) * 0.55 + animTime * 2.35) * 0.5;
        float w4 = cos((wp.x * 0.9 - wp.y * 0.7) + animTime * 3.1) * 0.28;
        vec2 grad = vec2(w1 * 0.055 + w3 * 0.04 + w4 * 0.025, w2 * 0.055 + w3 * 0.04 - w4 * 0.025);
        N = normalize(vec3(-grad.x, 1.0, -grad.y));

        vec3 deepWater = mix(vec3(0.008, 0.065, 0.145), vec3(0.003, 0.012, 0.032), nightFactor);
        vec3 shallowTurquoise = mix(vec3(0.025, 0.220, 0.285), vec3(0.008, 0.035, 0.065), nightFactor);

        float eastNorthSouthDist = max(wp.x - 375.0, abs(wp.y) - 375.0);
        float westBeachEdge = (abs(wp.y) < 352.0) ? (-392.0 - wp.x) : (-375.0 - wp.x);
        float islandDist = max(eastNorthSouthDist, westBeachEdge);
        float shoreProx = 1.0 - clamp(islandDist / 46.0, 0.0, 1.0);
        albedo = mix(deepWater, shallowTurquoise, shoreProx * 0.80);

        if (islandDist > -7.0 && islandDist < 18.0) {
            float surfWave = sin(islandDist * 0.82 - animTime * 2.7 + valueNoise(wp * 0.25) * 3.0);
            float foam = smoothstep(0.52, 0.92, surfWave) * (1.0 - clamp(islandDist / 18.0, 0.0, 1.0));
            albedo = mix(albedo, vec3(0.82, 0.88, 0.92), foam * 0.80);
        }
        roughness = 0.07;
        metallic = 0.06;
    }
    else if (matId == 10) {
        // MAT 10: Lush Multi-Tone Park Lawn & Subsurface-Lit Foliage
        float patchNoise = valueNoise(fragPosition.xz * 0.45);
        float bladeNoise = valueNoise(fragPosition.xz * 7.5 + vec2(fragPosition.y * 3.0));
        albedo *= mix(0.80, 1.22, patchNoise * 0.65 + bladeNoise * 0.35);
        roughness = 0.66;
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
        mapped = max(mapped, clamp(srgbBase.rgb * (0.92 + colSpecular.z * 0.25), 0.0, 1.0));
        finalColor = vec4(mapped, srgbBase.a);
        return;
    }

    // -----------------------------------------------------------------------
    // Cook-Torrance GGX Microfacet BRDF + Dual-Layer Clearcoat + City Env Reflections
    // -----------------------------------------------------------------------
    float ndotl = max(dot(N, L), 0.0);
    if (matId == 10) {
        ndotl = max(dot(N, L) * 0.68 + 0.32, 0.0);
    }

    float shadow = computeSoftShadow(fragPosition, N, L);
    float sunVis = 1.0 - shadow * 0.85;

    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);
    float contactAo = mix(0.72, 1.0, clamp(fragPosition.y / 1.7, 0.0, 1.0));
    if (N.y > 0.75) contactAo = 1.0;

    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
    float ndotv = max(dot(N, V), 0.001);
    float vdoth = max(dot(V, H), 0.0);

    // Trowbridge-Reitz GGX Normal Distribution + Schlick-GGX Geometry + Schlick Fresnel
    float alpha = max(0.002, roughness * roughness);
    float alpha2 = alpha * alpha;
    float denomD = (ndoth * ndoth * (alpha2 - 1.0) + 1.0);
    float D_ggx = alpha2 / max(kPi * denomD * denomD, 0.0001);

    float kG = ((roughness + 1.0) * (roughness + 1.0)) * 0.125;
    float G_v = ndotv / (ndotv * (1.0 - kG) + kG);
    float G_l = ndotl / (ndotl * (1.0 - kG) + kG);
    float G_smith = G_v * G_l;

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F_schlick = f0 + (vec3(1.0) - f0) * pow(1.0 - vdoth, 5.0);

    vec3 specBrdf = (D_ggx * G_smith * F_schlick) / max(4.0 * ndotv * ndotl, 0.001);
    vec3 kD = (vec3(1.0) - F_schlick) * (1.0 - metallic * 0.86);

    vec3 directLit = (kD * albedo / kPi + specBrdf * 0.42) * sunColor * ndotl * sunVis * 2.55;
    vec3 envLit    = albedo * ambient * contactAo;

    // 360-Degree City Skyline & Neon Reflections on Glass (3,4), Ocean (6), Car Clearcoat (7), Car Glass (8), and Wet Puddles!
    bool reflectiveMat = (matId == 3 || matId == 4 || matId == 6 || matId == 7 || matId == 8 || puddleMask > 0.05);
    if (reflectiveMat) {
        vec3 R = reflect(-V, N);
        vec3 envRefl = sampleCityEnvMap(R, fragPosition, roughness, nightFactor);

        float specExp = (matId == 6) ? 210.0 : ((matId == 7) ? mix(160.0, 24.0, clamp(roughness * 1.5, 0.0, 1.0)) : 110.0);
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.8 : 1.55);

        float coatStrength = (matId == 6) ? 0.90
                           : ((matId == 8) ? 0.85
                           : ((matId == 7) ? 0.78
                           : ((matId == 3) ? 0.56
                           : ((matId == 4) ? (0.35 * (1.0 - step(0.3, roughness))) : (puddleMask * 0.82)))));
        if (matId == 7) {
            // Dual-layer automotive polyurethane clearcoat + metallic flake sparkle!
            float flake = valueNoise(fragPosition.xz * 85.0 + vec2(fragPosition.y * 65.0));
            envLit += albedo * pow(ndoth, 28.0) * (0.25 + 0.45 * flake) * (1.0 - roughness) * sunVis;
            coatStrength *= (1.0 - smoothstep(0.18, 0.52, roughness) * 0.76);
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

// Builds a complete sculpted smooth-normal automotive body, framed curved glass, wheel-well/aero trim, and curved stripes
struct BuiltCarMeshes {
    Mesh body{};
    Mesh glass{};
    Mesh trim{};
    Mesh stripes{};
    bool hasStripes = false;
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
        case VehicleKind::TrackPrototype:
            groundClear = 0.13f; wheelR = 0.34f; axleFrac = 0.62f;
            noseTipY = 0.48f; hoodBaseY = 0.74f; beltMidY = 0.76f; tailDeckY = 0.78f;
            roofPeakY = 1.08f;
            sRearBase = -0.68f; sRearTop = -0.16f; sFrontTop = 0.24f; sFrontBase = 0.60f;
            tumblehome = 0.62f; fenderFlare = 0.094f; waistPinch = 0.055f; noseRound = 0.25f;
            isCoupeGlass = true; hasPanoRoof = true;
            break;
        case VehicleKind::Hypercar:
            groundClear = 0.15f; wheelR = 0.34f; axleFrac = 0.62f;
            noseTipY = 0.50f; hoodBaseY = 0.76f; beltMidY = 0.78f; tailDeckY = 0.82f;
            roofPeakY = 1.13f;
            sRearBase = -0.74f; sRearTop = -0.18f; sFrontTop = 0.22f; sFrontBase = 0.58f;
            tumblehome = 0.70f; fenderFlare = 0.082f; waistPinch = 0.048f; noseRound = 0.22f;
            isCoupeGlass = true; hasPanoRoof = true;
            break;
        case VehicleKind::ClassicRoadster:
            groundClear = 0.16f; wheelR = 0.34f; axleFrac = 0.63f;
            noseTipY = 0.54f; hoodBaseY = 0.78f; beltMidY = 0.78f; tailDeckY = 0.84f;
            roofPeakY = 1.18f;
            sRearBase = 0.08f; sRearTop = 0.15f; sFrontTop = 0.22f; sFrontBase = 0.48f;
            tumblehome = 0.74f; fenderFlare = 0.068f; waistPinch = 0.038f; noseRound = 0.20f;
            isCoupeGlass = true;
            break;
        case VehicleKind::SportsCoupe:
            groundClear = 0.17f; wheelR = 0.35f; axleFrac = 0.63f;
            noseTipY = 0.54f; hoodBaseY = 0.80f; beltMidY = 0.82f; tailDeckY = 0.86f;
            roofPeakY = 1.24f;
            sRearBase = -0.72f; sRearTop = -0.24f; sFrontTop = 0.14f; sFrontBase = 0.46f;
            tumblehome = 0.73f; fenderFlare = 0.068f; waistPinch = 0.036f; noseRound = 0.18f;
            isCoupeGlass = true; hasPanoRoof = true;
            break;
        case VehicleKind::JdmTuner:
            groundClear = 0.16f; wheelR = 0.35f; axleFrac = 0.63f;
            noseTipY = 0.56f; hoodBaseY = 0.82f; beltMidY = 0.84f; tailDeckY = 0.88f;
            roofPeakY = 1.28f;
            sRearBase = -0.68f; sRearTop = -0.28f; sFrontTop = 0.14f; sFrontBase = 0.44f;
            tumblehome = 0.75f; fenderFlare = 0.078f; waistPinch = 0.026f; noseRound = 0.14f;
            isCoupeGlass = true;
            break;
        case VehicleKind::MuscleCar:
            groundClear = 0.20f; wheelR = 0.36f; axleFrac = 0.64f;
            noseTipY = 0.62f; hoodBaseY = 0.85f; beltMidY = 0.86f; tailDeckY = 0.88f;
            roofPeakY = 1.31f;
            sRearBase = -0.66f; sRearTop = -0.30f; sFrontTop = 0.08f; sFrontBase = 0.36f;
            tumblehome = 0.76f; fenderFlare = 0.064f; waistPinch = 0.028f; noseRound = 0.09f;
            isCoupeGlass = true;
            break;
        case VehicleKind::LuxurySedan:
            groundClear = 0.19f; wheelR = 0.36f; axleFrac = 0.64f;
            noseTipY = 0.60f; hoodBaseY = 0.85f; beltMidY = 0.87f; tailDeckY = 0.90f;
            roofPeakY = 1.38f;
            sRearBase = -0.68f; sRearTop = -0.34f; sFrontTop = 0.16f; sFrontBase = 0.46f;
            tumblehome = 0.78f; fenderFlare = 0.048f; waistPinch = 0.020f; noseRound = 0.14f;
            hasPanoRoof = true;
            break;
        case VehicleKind::PoliceCruiser:
            groundClear = 0.20f; wheelR = 0.35f; axleFrac = 0.64f;
            noseTipY = 0.60f; hoodBaseY = 0.85f; beltMidY = 0.87f; tailDeckY = 0.90f;
            roofPeakY = 1.39f;
            sRearBase = -0.68f; sRearTop = -0.32f; sFrontTop = 0.16f; sFrontBase = 0.46f;
            tumblehome = 0.78f; fenderFlare = 0.056f; waistPinch = 0.020f; noseRound = 0.13f;
            break;
        case VehicleKind::Taxi:
            groundClear = 0.20f; wheelR = 0.34f; axleFrac = 0.62f;
            noseTipY = 0.58f; hoodBaseY = 0.83f; beltMidY = 0.85f; tailDeckY = 0.88f;
            roofPeakY = 1.38f;
            sRearBase = -0.66f; sRearTop = -0.34f; sFrontTop = 0.16f; sFrontBase = 0.46f;
            tumblehome = 0.79f; fenderFlare = 0.040f; waistPinch = 0.015f; noseRound = 0.14f;
            break;
        case VehicleKind::Compact:
            groundClear = 0.19f; wheelR = 0.32f; axleFrac = 0.64f;
            noseTipY = 0.56f; hoodBaseY = 0.81f; beltMidY = 0.84f; tailDeckY = 0.92f;
            roofPeakY = 1.38f;
            sRearBase = -0.88f; sRearTop = -0.62f; sFrontTop = 0.16f; sFrontBase = 0.50f;
            tumblehome = 0.80f; fenderFlare = 0.042f; waistPinch = 0.014f; noseRound = 0.16f;
            break;
        case VehicleKind::SUV:
            groundClear = 0.26f; wheelR = 0.40f; axleFrac = 0.63f;
            noseTipY = 0.78f; hoodBaseY = 1.00f; beltMidY = 1.02f; tailDeckY = 1.05f;
            roofPeakY = 1.66f;
            sRearBase = -0.90f; sRearTop = -0.74f; sFrontTop = 0.14f; sFrontBase = 0.44f;
            tumblehome = 0.84f; fenderFlare = 0.062f; waistPinch = 0.012f; noseRound = 0.12f;
            break;
        case VehicleKind::OffroadPickup:
            groundClear = 0.30f; wheelR = 0.45f; axleFrac = 0.64f;
            noseTipY = 0.88f; hoodBaseY = 1.12f; beltMidY = 1.12f; tailDeckY = 1.04f;
            roofPeakY = 1.78f;
            sRearBase = -0.10f; sRearTop = -0.02f; sFrontTop = 0.24f; sFrontBase = 0.50f;
            tumblehome = 0.84f; fenderFlare = 0.088f; waistPinch = 0.008f; noseRound = 0.09f;
            break;
        case VehicleKind::ArmoredSuv:
            groundClear = 0.29f; wheelR = 0.43f; axleFrac = 0.63f;
            noseTipY = 0.86f; hoodBaseY = 1.10f; beltMidY = 1.12f; tailDeckY = 1.14f;
            roofPeakY = 1.78f;
            sRearBase = -0.92f; sRearTop = -0.78f; sFrontTop = 0.14f; sFrontBase = 0.42f;
            tumblehome = 0.86f; fenderFlare = 0.085f; waistPinch = 0.008f; noseRound = 0.08f;
            break;
        case VehicleKind::Ambulance:
            groundClear = 0.24f; wheelR = 0.39f; axleFrac = 0.63f;
            noseTipY = 0.78f; hoodBaseY = 1.04f; beltMidY = 1.08f; tailDeckY = 1.10f;
            roofPeakY = 2.02f;
            sRearBase = -0.97f; sRearTop = -0.92f; sFrontTop = 0.38f; sFrontBase = 0.72f;
            tumblehome = 0.92f; fenderFlare = 0.046f; waistPinch = 0.0f; noseRound = 0.09f;
            break;
        case VehicleKind::FireTruck:
            groundClear = 0.26f; wheelR = 0.43f; axleFrac = 0.64f;
            noseTipY = 0.88f; hoodBaseY = 1.14f; beltMidY = 1.16f; tailDeckY = 1.16f;
            roofPeakY = 2.08f;
            sRearBase = -0.97f; sRearTop = -0.93f; sFrontTop = 0.52f; sFrontBase = 0.80f;
            tumblehome = 0.93f; fenderFlare = 0.052f; waistPinch = 0.0f; noseRound = 0.06f;
            break;
        case VehicleKind::DeliveryVan:
        default:
            groundClear = 0.24f; wheelR = 0.38f; axleFrac = 0.62f;
            noseTipY = 0.76f; hoodBaseY = 1.02f; beltMidY = 1.06f; tailDeckY = 1.08f;
            roofPeakY = 1.98f;
            sRearBase = -0.96f; sRearTop = -0.90f; sFrontTop = 0.38f; sFrontBase = 0.72f;
            tumblehome = 0.90f; fenderFlare = 0.042f; waistPinch = 0.0f; noseRound = 0.10f;
            break;
    }

    const float axleX = halfL * axleFrac;
    const float archR = wheelR * 1.18f;

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

        // 2) Underbody diffuser/splitter curve + full-clearance semicircular 3D wheel-arch cutout!
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
                const float u = dx / archR;
                const float circleH = std::sqrt(std::max(0.0f, 1.0f - u * u));
                const float archTopY = wheelR * 2.12f;
                const float cutY = yBot + (archTopY - yBot) * std::pow(circleH, 0.72f);
                archLift = std::max(archLift, cutY - yBot);
            }
        }

        // 3) Sculpted beltline / fender crest / sloped hood curve
        float beltY = beltMidY;
        if (s >= sFrontBase) {
            const float u = (s - sFrontBase) / std::max(0.05f, 1.0f - sFrontBase);
            // Smooth curved aerodynamic hood sloping into front grille
            beltY = lerpf(hoodBaseY, noseTipY, std::pow(clampf(u, 0.0f, 1.0f), 1.35f));
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
        beltY += flare * 0.12f;

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
        const float minAboveArch = ySillVal + 0.018f;
        const float finalBeltY = std::max(beltY, minAboveArch + 0.028f);
        const float finalBellyY = std::max(lerpf(yBot, finalBeltY, 0.52f), minAboveArch);

        const float hoodCenterCrown =
            (kind == VehicleKind::MuscleCar && s > sFrontBase + 0.06f && s < 0.84f)
                ? 0.065f
                : (((kind == VehicleKind::Hypercar || kind == VehicleKind::TrackPrototype) && s < sRearTop && s > -0.82f)
                       ? 0.045f
                       : 0.032f);
        const float deckOrRoofSideY = lerpf(finalBeltY + 0.018f, roofPeakY - 0.038f, clampf(cabinFrac, 0.0f, 1.05f));
        const float deckOrRoofTopY  = lerpf(finalBeltY + hoodCenterCrown, roofPeakY, clampf(cabinFrac, 0.0f, 1.05f));

        st.yUnder     = yBot;
        st.ySill      = ySillVal;
        st.yBelly     = finalBellyY;
        st.yBelt      = finalBeltY;
        st.yUpper     = lerpf(finalBeltY, deckOrRoofSideY, 0.52f);
        st.yCrownSide = deckOrRoofSideY;
        st.yTopCenter = std::max(deckOrRoofSideY + 0.014f, deckOrRoofTopY);

        // Open-top convertible cockpit tub (ClassicRoadster) & recessed 4x4 cargo pickup bed (OffroadPickup)
        if (kind == VehicleKind::ClassicRoadster && s > -0.36f && s < 0.07f) {
            st.yCrownSide = finalBeltY + 0.015f;
            st.yTopCenter = finalBeltY - 0.15f;
        } else if (kind == VehicleKind::OffroadPickup && s > -0.90f && s < -0.12f) {
            st.yCrownSide = finalBeltY + 0.020f;
            st.yTopCenter = finalBeltY - 0.16f;
        }

        const float roofNarrow = lerpf(0.86f, tumblehome, clampf(cabinFrac, 0.0f, 1.0f));
        // Pull inner underbody floor rail cleanly behind inner tire sidewall inside every wheel arch!
        const float archFrac = clampf(archLift / std::max(0.08f, wheelR * 0.95f), 0.0f, 1.0f);
        st.wUnder     = lerpf(wMax * 0.76f, wMax * 0.50f, smooth01(archFrac));
        st.wSill      = wMax * 0.93f;
        st.wBelly     = wMax;
        st.wBelt      = wMax * 0.94f;
        st.wUpper     = wMax * lerpf(0.91f, roofNarrow + 0.06f, clampf(cabinFrac, 0.0f, 1.0f));
        st.wCrownSide = wMax * roofNarrow * 0.88f;
        return st;
    };

    // Build dense station list capturing bumpers, semicircular wheel arches, windshield base, roof rails, and decklid
    std::vector<float> sList;
    for (int i = 0; i <= 32; ++i) {
        sList.push_back(-1.0f + 2.0f * (static_cast<float>(i) / 32.0f));
    }
    const float keyS[] = {
        sRearBase, sRearTop, sFrontTop, sFrontBase,
        (-axleX - archR) / halfL, (-axleX - archR * 0.70f) / halfL, (-axleX) / halfL,
        (-axleX + archR * 0.70f) / halfL, (-axleX + archR) / halfL,
        ( axleX - archR) / halfL, ( axleX - archR * 0.70f) / halfL, ( axleX) / halfL,
        ( axleX + archR * 0.70f) / halfL, ( axleX + archR) / halfL,
        -0.96f, -0.88f, 0.88f, 0.96f
    };
    for (float ks : keyS) {
        if (ks > -0.995f && ks < 0.995f) sList.push_back(ks);
    }
    std::sort(sList.begin(), sList.end());
    std::vector<float> stations;
    for (float sv : sList) {
        if (stations.empty() || (sv - stations.back()) > 0.014f) {
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
    SmoothMeshBuilder stripesB;

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

    // Rear curved backlight window (except solid rear box vans/fire engine & open-top roadster)
    if (kind != VehicleKind::DeliveryVan &&
        kind != VehicleKind::Ambulance &&
        kind != VehicleKind::FireTruck &&
        kind != VehicleKind::ClassicRoadster) {
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
    if (kind != VehicleKind::ClassicRoadster) {
        const float swStart = (kind == VehicleKind::FireTruck) ? 0.34f
                            : ((kind == VehicleKind::DeliveryVan || kind == VehicleKind::Ambulance) ? 0.14f : (sRearBase + 0.08f));
        const float swEnd   = sFrontBase - 0.06f;
        const float bPillarS = 0.5f * (swStart + swEnd) + 0.03f;
        constexpr int kSideSegs = 8;

        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            for (int seg = 0; seg < kSideSegs; ++seg) {
                const float sa = lerpf(swStart, swEnd, static_cast<float>(seg) / kSideSegs);
                const float sb = lerpf(swStart, swEnd, static_cast<float>(seg + 1) / kSideSegs);
                // Leave a crisp painted B-pillar gap on 4-door Sedans & SUVs!
                if (!isCoupeGlass &&
                    kind != VehicleKind::DeliveryVan &&
                    kind != VehicleKind::Ambulance &&
                    kind != VehicleKind::FireTruck &&
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

    // Panoramic tinted glass roof on Hypercar, TrackPrototype, SportsCoupe & LuxurySedan
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

    // 2.5) Contour-Hugging Curved Dual GT Vinyl Racing Stripes (lofted directly on body surface!)
    {
        auto addStripeStrip = [&](float sStart, float sEnd, float yLift, int segs) {
            if (sEnd - sStart < 0.08f) return;
            for (int side = -1; side <= 1; side += 2) {
                const float sgn = static_cast<float>(side);
                for (int seg = 0; seg < segs; ++seg) {
                    const float sa = lerpf(sStart, sEnd, static_cast<float>(seg) / static_cast<float>(segs));
                    const float sb = lerpf(sStart, sEnd, static_cast<float>(seg + 1) / static_cast<float>(segs));
                    const StationEval ea = evalStation(sa);
                    const StationEval eb = evalStation(sb);

                    const float maxZa = std::max(0.18f, ea.wCrownSide * 0.84f);
                    const float maxZb = std::max(0.18f, eb.wCrownSide * 0.84f);
                    const float zInA  = std::min(0.085f, maxZa * 0.36f);
                    const float zOutA = std::min(0.220f, maxZa);
                    const float zInB  = std::min(0.085f, maxZb * 0.36f);
                    const float zOutB = std::min(0.220f, maxZb);

                    auto surfY = [&](const StationEval& e, float absZ) -> float {
                        const float f = clampf(absZ / std::max(0.05f, e.wCrownSide), 0.0f, 1.0f);
                        return lerpf(e.yTopCenter, e.yCrownSide, f) + yLift;
                    };

                    const Vector3 p0{ea.x, surfY(ea, zInA),  sgn * zInA};
                    const Vector3 p1{eb.x, surfY(eb, zInB),  sgn * zInB};
                    const Vector3 p2{eb.x, surfY(eb, zOutB), sgn * zOutB};
                    const Vector3 p3{ea.x, surfY(ea, zOutA), sgn * zOutA};
                    stripesB.addQuadOutward(p0, p1, p2, p3, {0.0f, 1.0f, 0.0f});
                }
            }
        };

        // Hood stripes (all passenger/performance cars; vans/fire engines have ultra-short noses)
        if (kind != VehicleKind::DeliveryVan && kind != VehicleKind::Ambulance && kind != VehicleKind::FireTruck) {
            addStripeStrip(sFrontBase + 0.025f, 0.94f, 0.011f, 10);
        }
        // Roof stripes (skip open-top roadster & roof-lightbar emergency vehicles)
        if (kind != VehicleKind::ClassicRoadster && kind != VehicleKind::PoliceCruiser &&
            kind != VehicleKind::Ambulance && kind != VehicleKind::FireTruck) {
            addStripeStrip(sRearTop + 0.025f, sFrontTop - 0.025f, hasPanoRoof ? 0.024f : 0.011f, 8);
        }
        // Rear deck / trunk stripes (sedans, coupes, hypercars, roadster tonneau)
        if (kind == VehicleKind::ClassicRoadster) {
            addStripeStrip(-0.94f, -0.39f, 0.011f, 8);
        } else if (kind != VehicleKind::OffroadPickup && sRearBase > -0.86f) {
            addStripeStrip(-0.94f, sRearBase - 0.025f, 0.011f, 6);
        }
    }

    // 3) Dark Wheel-House Liners (clamped strictly inside sculpted fender openings!), Splitter, Grille, Side Skirts & Diffuser
    constexpr int kArchSegs = 12;
    for (int side = -1; side <= 1; side += 2) {
        const float sgn = static_cast<float>(side);
        for (float ax : {-axleX, axleX}) {
            for (int a = 0; a < kArchSegs; ++a) {
                const float th0 = (static_cast<float>(a) / kArchSegs) * kPi;
                const float th1 = (static_cast<float>(a + 1) / kArchSegs) * kPi;
                const float x0 = ax + std::cos(th0) * (archR * 0.96f);
                const float x1 = ax + std::cos(th1) * (archR * 0.96f);
                const StationEval e0 = evalStation(clampf(x0 / halfL, -0.99f, 0.99f));
                const StationEval e1 = evalStation(clampf(x1 / halfL, -0.99f, 0.99f));

                const float y0 = std::max(groundClear + 0.02f, e0.ySill - 0.012f);
                const float y1 = std::max(groundClear + 0.02f, e1.ySill - 0.012f);
                const float zOuter0 = std::min(halfW * 0.86f, e0.wSill - 0.015f);
                const float zOuter1 = std::min(halfW * 0.86f, e1.wSill - 0.015f);
                const float zInner0 = std::min(halfW * 0.48f, e0.wUnder);
                const float zInner1 = std::min(halfW * 0.48f, e1.wUnder);

                // Cylindrical wheel-well arch liner tucked cleanly beneath painted fender lip
                trimB.addQuadOutward({x0, y0, sgn * zInner0}, {x1, y1, sgn * zInner1},
                                     {x1, y1, sgn * zOuter1}, {x0, y0, sgn * zOuter0},
                                     {ax - 0.5f * (x0 + x1), wheelR - 0.5f * (y0 + y1), sgn * 0.25f});
                // Dark inner chassis backing wall behind tire so there is zero see-through gap!
                const float zBack = 0.5f * (zInner0 + zInner1);
                trimB.addTriOutward({ax, wheelR * 0.65f, sgn * zBack},
                                    {x0, y0, sgn * zInner0},
                                    {x1, y1, sgn * zInner1},
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
        if (kind == VehicleKind::TrackPrototype || kind == VehicleKind::Hypercar ||
            kind == VehicleKind::SportsCoupe || kind == VehicleKind::JdmTuner ||
            kind == VehicleKind::ClassicRoadster || kind == VehicleKind::MuscleCar) {
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
        // Hypercar & TrackPrototype side mid-engine air intakes behind doors
        if (kind == VehicleKind::Hypercar || kind == VehicleKind::TrackPrototype) {
            const StationEval eh = evalStation(-0.22f);
            trimB.addBox({eh.x, eh.yBelly + 0.12f,  eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
            trimB.addBox({eh.x, eh.yBelly + 0.12f, -eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
        }
        // ArmoredSuv & OffroadPickup heavy steel bullbars, PoliceCruiser push-bumper
        if (kind == VehicleKind::ArmoredSuv) {
            trimB.addBox({halfL + 0.07f, 0.56f, 0.0f}, {0.10f, 0.44f, halfW * 1.45f});
            trimB.addBox({-0.25f, roofPeakY + 0.055f,  halfW * 0.72f}, {halfL * 1.18f, 0.06f, 0.07f});
            trimB.addBox({-0.25f, roofPeakY + 0.055f, -halfW * 0.72f}, {halfL * 1.18f, 0.06f, 0.07f});
        } else if (kind == VehicleKind::OffroadPickup) {
            trimB.addBox({halfL + 0.07f, 0.58f, 0.0f}, {0.10f, 0.42f, halfW * 1.38f});
            trimB.addBox({halfL + 0.08f, 0.74f,  0.38f}, {0.09f, 0.36f, 0.065f});
            trimB.addBox({halfL + 0.08f, 0.74f, -0.38f}, {0.09f, 0.36f, 0.065f});
        } else if (kind == VehicleKind::PoliceCruiser) {
            trimB.addBox({halfL + 0.065f, 0.48f,  0.34f}, {0.10f, 0.44f, 0.065f});
            trimB.addBox({halfL + 0.065f, 0.48f, -0.34f}, {0.10f, 0.44f, 0.065f});
            trimB.addBox({halfL + 0.075f, 0.58f,  0.0f},  {0.09f, 0.055f, 0.98f});
            trimB.addBox({halfL + 0.075f, 0.37f,  0.0f},  {0.09f, 0.055f, 0.98f});
        } else if (kind == VehicleKind::SUV) {
            trimB.addBox({-0.22f, roofPeakY + 0.04f,  halfW * 0.68f}, {halfL * 1.10f, 0.045f, 0.05f});
            trimB.addBox({-0.22f, roofPeakY + 0.04f, -halfW * 0.68f}, {halfL * 1.10f, 0.045f, 0.05f});
        }
    }

    BuiltCarMeshes out{};
    out.body  = bodyB.uploadSmooth(48.0f);
    out.glass = glassB.uploadSmooth(48.0f);
    out.trim  = trimB.uploadSmooth(42.0f);
    if (!stripesB.pos.empty()) {
        out.stripes = stripesB.uploadSmooth(48.0f);
        out.hasStripes = true;
    }
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
    locWetRoad_       = GetShaderLocation(worldShader_, "wetRoadFactor");
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
    locSkyWet_       = GetShaderLocation(skyShader_, "wetWeather");

    // 4) Full-screen HDR Bloom + ACES post-process shader
    postShader_ = LoadShaderFromMemory(nullptr, kPostFragShader);
    locPostResolution_    = GetShaderLocation(postShader_, "resolution");
    locPostBloomStrength_ = GetShaderLocation(postShader_, "bloomStrength");
    locPostExposure_      = GetShaderLocation(postShader_, "exposure");
    locPostNitro_         = GetShaderLocation(postShader_, "nitroStrength");
    locPostEnableBloom_   = GetShaderLocation(postShader_, "enableBloom");

    // 5) Shared GPU-resident primitive & 16 sculpted smooth-normal automotive vehicle models
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
        carModels_[i].hasStripes = cm.hasStripes;
        if (cm.hasStripes) {
            carModels_[i].stripes = LoadModelFromMesh(cm.stripes);
        }
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
        if (cm.hasStripes) {
            UnloadModel(cm.stripes);
            cm.hasStripes = false;
        }
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
        if (cm.hasStripes) {
            cm.stripes.materials[0].shader = shader;
        }
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
    const float wetF  = world.isRainWeather() ? 1.0f : 0.0f;
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
    SetShaderValue(worldShader_, locWetRoad_, &wetF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader_, locEnableShadows_, &shadowsInt, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(worldShader_, locLightVP_, lightVP);
}

void Renderer3D::drawSkyDome(const GameWorld& world, const Camera3D& camera, const Vector3& lightDir) {
    const Vector3 sunTowards = Vector3Negate(lightDir);
    const float sdir[3] = {sunTowards.x, sunTowards.y, sunTowards.z};
    const float hours = world.timeOfDayHours();
    const float dayF  = world.daylightFactor();
    const float animT = static_cast<float>(GetTime());
    const float wetF  = world.isRainWeather() ? 1.0f : 0.0f;


    SetShaderValue(skyShader_, locSkySunDir_, sdir, SHADER_UNIFORM_VEC3);
    SetShaderValue(skyShader_, locSkyTimeHours_, &hours, SHADER_UNIFORM_FLOAT);
    SetShaderValue(skyShader_, locSkyDayFactor_, &dayF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(skyShader_, locSkyAnimTime_, &animT, SHADER_UNIFORM_FLOAT);
    SetShaderValue(skyShader_, locSkyWet_, &wetF, SHADER_UNIFORM_FLOAT);

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawModelEx(skyDomeModel_, camera.position, {0.0f, 1.0f, 0.0f}, 0.0f,
                {950.0f, 950.0f, 950.0f}, WHITE);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer3D::renderFrame(const GameWorld& world, const Camera3D& camera,
                             bool firstPerson, float adsBlend, bool lookBack) {
    const int sw = std::max(1, GetScreenWidth());
    const int sh = std::max(1, GetScreenHeight());
    const Color skyCol = computeSkyColor(world);
    const Vector3 lightDir = computeSunDirection(world);
    const Vec2 focus{camera.target.x, camera.target.z};

    const bool useShadows = (quality_ != GraphicsQuality::Performance) && shadowMapValid_;
    const bool usePostFbo = (quality_ != GraphicsQuality::Performance);
    const bool activeFpsView = firstPerson && !lookBack && !world.player().wasted;

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
        drawSceneGeometry(world, camera, true, false);
        EndMode3D();
        EndTextureMode();

        shadowPassActive_ = false;
        setAllModelsShader(worldShader_);
    }

    // -----------------------------------------------------------------------
    // PASS 2: Sky Dome + PBR-Lite Shaded 3D Open-World City + 1st-Person Viewmodel
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
    drawSceneGeometry(world, camera, false, activeFpsView);
    EndMode3D();

    // Dedicated depth-prioritized 3D 1st-Person Weapon Viewmodel / Vehicle Cockpit pass
    if (activeFpsView) {
        rlSetClipPlanes(0.09, 10.0);
        BeginMode3D(camera);
        drawFirstPersonOverlay3D(world, camera, adsBlend);
        EndMode3D();
    }

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
    drawSceneGeometry(world, camera, false, false);
}

void Renderer3D::drawSceneGeometry(const GameWorld& world, const Camera3D& camera,
                                   bool shadowPass, bool hidePlayerForFps) {
    rlSetTexture(0);
    const Vec2 focus{camera.target.x, camera.target.z};
    const Vec3 camPos{camera.position.x, camera.position.y, camera.position.z};
    const bool nightMode = world.isNightTime();

    if (!shadowPass) {
        drawOceanAndGround(world);
        drawRoadNetwork(world, focus);
    }

    drawBuildingsAndParks(world, focus, shadowPass);
    drawTreesAndProps(world, focus, camPos, shadowPass);

    // Vehicles (in 1st-person driving, draw sculpted exterior hood + lights without blocking cabin shell)
    const float vehCull = shadowPass ? (135.0f * 135.0f) : (275.0f * 275.0f);
    for (const auto& v : world.vehicles()) {
        if ((v.pos - focus).lengthSq() > vehCull) continue;
        const bool fpCockpitCar = hidePlayerForFps && !shadowPass && v.isPlayerDriving;
        drawVehicle3D(v, world.surfaceHeightAt(v.pos), nightMode, shadowPass, fpCockpitCar);
    }

    // Player character when on foot (rendered in shadowPass even in FPS so player casts a real 3D ground shadow!)
    if (world.player().drivingVehicleId < 0 && !hidePlayerForFps) {
        const auto& pl = world.player();
        const float surfY = world.surfaceHeightAt(pl.pos);
        const float baseY = std::max(surfY, pl.posY);
        const auto& wSlot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
        drawCharacter3D(pl.pos, baseY, pl.facingYaw, pl.walkAnimPhase,
                        getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx),
                        getPlayerBottomColor(pl.currentOutfit, pl.characterCustom.customBottomColorIdx),
                        getPlayerSkinColor(pl.characterCustom.skinToneIdx),
                        !pl.wasted, pl.currentWeapon != WeaponId::Fists,
                        pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                        pl.characterCustom.glassesStyle > 0, pl.currentOutfit, shadowPass,
                        surfY, !pl.onGround, false, false, false,
                        &pl.characterCustom, &wSlot);

        // Live 3rd-person tactical laser sight beam + pinpoint world dot!
        if (!shadowPass && !pl.wasted && pl.currentWeapon != WeaponId::Fists &&
            wSlot.laserSightColor > 0 && pl.reloadTimer <= 0.0f) {
            const float cy = std::cos(pl.facingYaw);
            const float sy = std::sin(pl.facingYaw);
            const float ca = std::cos(pl.aimPitch);
            const float sa = std::sin(pl.aimPitch);
            constexpr float kGunReach = 0.64f;
            const Vector3 laserStart{
                pl.pos.x - sy * 0.27f + cy * ca * kGunReach,
                baseY + 1.32f + sa * kGunReach,
                pl.pos.z + cy * 0.27f + sy * ca * kGunReach
            };
            const Vector3 laserEnd{
                pl.crosshairPoint3D.x,
                pl.crosshairPoint3D.y,
                pl.crosshairPoint3D.z
            };
            Color beamCol = toRayColor(getLaserSightColorRGBA(wSlot.laserSightColor));
            Color dotCol = beamCol;
            dotCol.a = 255;
            DrawLine3D(laserStart, laserEnd, beamCol);
            drawModelStyled(sphereModel_, laserEnd, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.046f, 0.046f, 0.046f}, dotCol, 0.08f, 0.0f, 3.8f, MatKind::EmissiveGlow);
        }
    }

    // Pedestrians, Gangsters & SBPD Police / SWAT Officers
    const float npcCull = shadowPass ? (110.0f * 110.0f) : (190.0f * 190.0f);
    for (const auto& npc : world.npcs()) {
        if ((npc.pos - focus).lengthSq() > npcCull) continue;
        const float gY = world.surfaceHeightAt(npc.pos);
        const bool isCop = (npc.role == NpcRole::Police);
        const bool isArmed = (npc.role == NpcRole::Gangster || isCop);
        const bool isPanicking = (npc.role == NpcRole::Civilian && npc.behavior == NpcBehavior::PanicFlee);
        drawCharacter3D(npc.pos, gY, npc.facingYaw, npc.animPhase,
                        npc.outfitTop, npc.outfitBottom, npc.skinColor,
                        npc.alive, isArmed, npc.weapon, npc.muzzleFlashTimer,
                        0.0f, (npc.role == NpcRole::Gangster || isCop),
                        OutfitId::StreetCasual, shadowPass,
                        gY, false, isPanicking, isCop, npc.isSwat);
    }

    if (!shadowPass) {
        drawLivingWorldAmbience(world, focus, camPos);
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

    // Iconic California Blue-and-Wood Lifeguard Tower on South Sunset Beach (cleanly clear of Ferris Wheel)
    {
        const float lx = -372.0f;
        const float lz = 26.0f;
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

        // Central Park Plaza: Crossed Stone Footpaths, Tiered Marble Fountain, Arcing Water Jets, Flowerbeds & Vintage Globes!
        const ParkPatch* centralPark = nullptr;
        for (const auto& park : city.parks()) {
            const Vec2 phe = park.bounds.halfExtents();
            if (phe.x > 22.0f && phe.z > 22.0f) {
                centralPark = &park;
                break;
            }
        }
        if (centralPark != nullptr) {
            const Vec2 fc = centralPark->bounds.center();
            const Vec2 he = centralPark->bounds.halfExtents();
            if ((fc - focus).lengthSq() < 260.0f * 260.0f) {
                const Color stonePath{198, 194, 184, 255};
                const Color marbleLight{224, 226, 232, 255};
                const Color marbleMid{196, 200, 210, 255};

                // Crossed crushed-stone park footpaths + circular stone plaza apron
                drawModelStyled(cubeModel_, {fc.x, 0.115f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.98f, 0.03f, 3.6f}, stonePath, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
                drawModelStyled(cubeModel_, {fc.x, 0.115f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.6f, 0.03f, he.z * 1.98f}, stonePath, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
                drawModelStyled(cylModel_, {fc.x, 0.105f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {9.1f, 0.04f, 9.1f}, stonePath, 0.78f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

                // Multi-tiered Beaux-Arts marble fountain basin & upper pedestal bowl
                drawModelStyled(cylModel_, {fc.x, 0.12f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.85f, 0.56f, 5.85f}, marbleMid, 0.52f, 0.05f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 0.64f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {6.05f, 0.12f, 6.05f}, marbleLight, 0.45f, 0.05f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 0.56f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.15f, 0.16f, 5.15f}, Color{42, 186, 242, 255},
                                0.08f, 0.05f, night ? 0.45f : 0.18f, MatKind::OceanWater);
                // Center fluted pedestal + mid stone catch-basin + finial spire
                drawModelStyled(cylModel_, {fc.x, 0.62f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.20f, 2.75f, 1.20f}, marbleLight, 0.40f, 0.10f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 2.05f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.35f, 0.24f, 2.35f}, marbleLight, 0.40f, 0.08f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 2.26f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.05f, 0.08f, 2.05f}, Color{88, 212, 252, 255},
                                0.08f, 0.05f, night ? 0.75f : 0.25f, MatKind::EmissiveGlow);

                // Effervescent central vertical water plume
                const float plumeH = 1.65f + 0.18f * std::sin(animT * 5.2f);
                drawModelStyled(coneModel_, {fc.x, 3.28f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.42f, plumeH, 0.42f}, Color{178, 238, 255, 255},
                                0.12f, 0.05f, night ? 1.45f : 0.42f, MatKind::EmissiveGlow);
                drawModelStyled(sphereModel_, {fc.x, 3.30f + plumeH * 0.85f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.54f, 0.46f, 0.54f}, Color{212, 246, 255, 255},
                                0.12f, 0.05f, night ? 1.35f : 0.38f, MatKind::EmissiveGlow);

                // 6 Animated Parabolic Water Jets arching cleanly from the mid basin into the lower pool!
                for (int j = 0; j < 6; ++j) {
                    const float ang = static_cast<float>(j) * (kTwoPi / 6.0f);
                    const float ca = std::cos(ang);
                    const float sa = std::sin(ang);
                    for (int seg = 0; seg < 5; ++seg) {
                        const float t = std::fmod((static_cast<float>(seg) * 0.20f) + animT * 0.75f, 1.0f);
                        const float r = 1.15f + t * 2.95f;
                        const float y = 2.32f + 1.48f * t - 2.92f * t * t;
                        if (y < 0.74f) continue;
                        const float sz = 0.14f + 0.06f * std::sin(t * kPi);
                        drawModelStyled(sphereModel_, {fc.x + ca * r, y, fc.z + sa * r},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {sz, sz * 1.15f, sz},
                                        Color{184, 240, 255, 255}, 0.10f, 0.05f,
                                        night ? 1.45f : 0.35f, MatKind::EmissiveGlow);
                    }
                    // Underwater aqua LED fountain accent lights glowing at night
                    if (night) {
                        drawModelStyled(sphereModel_, {fc.x + ca * 3.85f, 0.68f, fc.z + sa * 3.85f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.20f, 0.10f, 0.20f},
                                        Color{68, 224, 255, 255}, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
                    }
                }

                // 4 Manicured Diagonal Ornamental Flowerbed Planters (cleanly clear of benches & trash bin!)
                static constexpr Color kFlowerColors[4] = {
                    {246, 86, 118, 255},
                    {252, 204, 58, 255},
                    {198, 112, 248, 255},
                    {252, 134, 62, 255}
                };
                int fbIdx = 0;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        const float fx = fc.x + static_cast<float>(sx) * 5.45f;
                        const float fz = fc.z + static_cast<float>(sz) * 5.45f;
                        drawModelStyled(cylModel_, {fx, 0.12f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {1.08f, 0.32f, 1.08f}, marbleMid, 0.65f, 0.05f, 0.0f, MatKind::Standard);
                        drawModelStyled(cylModel_, {fx, 0.38f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.92f, 0.12f, 0.92f}, Color{48, 128, 56, 255}, 0.75f, 0.0f, 0.0f, MatKind::FoliageGrass);
                        const Color flCol = kFlowerColors[fbIdx++ % 4];
                        for (int b = 0; b < 5; ++b) {
                            const float ba = static_cast<float>(b) * (kTwoPi / 5.0f) + 0.4f;
                            const float br = (b == 0) ? 0.0f : 0.48f;
                            drawModelStyled(sphereModel_, {fx + std::cos(ba) * br, 0.58f, fz + std::sin(ba) * br},
                                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.26f, 0.20f, 0.26f},
                                            flCol, 0.58f, 0.0f, 0.0f, MatKind::Standard);
                        }
                    }
                }

                // 4 Vintage Cast-Iron Park Globe Lamp Posts flanking the fountain plaza
                const float lampOffsets[4][2] = {{-8.7f, -2.5f}, {8.7f, 2.5f}, {-2.5f, 8.7f}, {2.5f, -8.7f}};
                for (int l = 0; l < 4; ++l) {
                    const float lx = fc.x + lampOffsets[l][0];
                    const float lz = fc.z + lampOffsets[l][1];
                    drawModelStyled(cylModel_, {lx, 0.12f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.16f, 0.45f, 0.16f}, Color{36, 40, 46, 255}, 0.42f, 0.68f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {lx, 0.55f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, 3.15f, 0.07f}, Color{40, 46, 54, 255}, 0.38f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {lx, 3.82f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.25f, 0.25f, 0.25f}, night ? Color{255, 232, 168, 255} : Color{238, 242, 248, 255},
                                    0.12f, 0.0f, night ? 2.5f : 0.25f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
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

        // Rich Architectural Rooftop Equipment: Water Towers on Stilts, Spinning HVAC Fan Banks, Satellite Dishes & Access Penthouses!
        if (!shadowPass && distSq < 255.0f * 255.0f && b.upperTierHeight <= 0.5f && !b.hasHelipad && b.height > 10.5f && he.x > 5.2f) {
            const int roofHash = std::abs(static_cast<int>(c.x * 3.0f + c.z * 7.0f)) % 5;
            const Color steelDark{72, 78, 88, 255};
            const Color metalHvac{128, 134, 144, 255};

            // 1) Roof-access stairwell penthouse & twin HVAC fan bank with live spinning blades
            const float hx = c.x + he.x * 0.30f;
            const float hz = c.z - he.z * 0.26f;
            drawModelStyled(cubeModel_, {hx, b.height + 1.05f, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.4f, 1.55f, 2.5f}, metalHvac, 0.58f, 0.42f, 0.0f, MatKind::IndustrialMetal);
            for (int f = -1; f <= 1; f += 2) {
                const float fx = hx + static_cast<float>(f) * 0.85f;
                drawModelStyled(cylModel_, {fx, b.height + 1.80f, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.68f, 0.22f, 0.68f}, steelDark, 0.45f, 0.65f, 0.0f, MatKind::Standard);
                rlPushMatrix();
                rlTranslatef(fx, b.height + 1.98f, hz);
                rlRotatef(animT * (210.0f + static_cast<float>(roofHash) * 28.0f), 0.0f, 1.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.12f, 0.04f, 0.14f}, Color{42, 46, 52, 255}, 0.4f, 0.6f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                                {1.12f, 0.04f, 0.14f}, Color{42, 46, 52, 255}, 0.4f, 0.6f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }

            // 2) Iconic Cylindrical Wooden Rooftop Water Tower on 4 Steel Stilts (Masonry/Brick Mid-Rises!)
            if (b.style != BuildingStyle::GlassTower && b.style != BuildingStyle::Warehouse &&
                b.style != BuildingStyle::ShippingStack && (roofHash == 0 || roofHash == 2 || roofHash == 4) && he.z > 5.5f) {
                const float wx = c.x - he.x * 0.34f;
                const float wz = c.z + he.z * 0.28f;
                const float stiltH = 2.35f;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        drawModelStyled(cylModel_, {wx + sx * 0.92f, b.height + 0.30f, wz + sz * 0.92f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.10f, stiltH, 0.10f},
                                        steelDark, 0.45f, 0.72f, 0.0f, MatKind::Standard);
                    }
                }
                // Cross tie deck + timber barrel tank + dark iron hoops + conical tin roof
                drawModelStyled(cubeModel_, {wx, b.height + 0.30f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.35f, 0.16f, 2.35f}, steelDark, 0.50f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {wx, b.height + 0.38f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.38f, 2.85f, 1.38f}, Color{138, 102, 68, 255}, 0.82f, 0.05f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {wx, b.height + 1.15f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.41f, 0.10f, 1.41f}, Color{48, 52, 58, 255}, 0.45f, 0.75f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {wx, b.height + 2.35f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.41f, 0.10f, 1.41f}, Color{48, 52, 58, 255}, 0.45f, 0.75f, 0.0f, MatKind::Standard);
                drawModelStyled(coneModel_, {wx, b.height + 3.22f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.62f, 1.35f, 1.62f}, Color{84, 90, 98, 255}, 0.55f, 0.45f, 0.0f, MatKind::Standard);
            } else {
                // Satellite Dish + Communications Lattice Antenna
                const float dx = c.x - he.x * 0.32f;
                const float dz = c.z + he.z * 0.25f;
                drawModelStyled(cylModel_, {dx, b.height + 0.30f, dz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.11f, 1.45f, 0.11f}, steelDark, 0.42f, 0.75f, 0.0f, MatKind::Standard);
                rlPushMatrix();
                rlTranslatef(dx, b.height + 1.85f, dz);
                rlRotatef(static_cast<float>(roofHash) * 65.0f, 0.0f, 1.0f, 0.0f);
                rlRotatef(38.0f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.16f, 0.92f}, Color{226, 232, 240, 255}, 0.38f, 0.35f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.0f, 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.05f, 0.65f, 0.05f}, steelDark, 0.40f, 0.70f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }
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

                // Vertical Illuminated Commercial Neon Blade Sign mounted on street-facing building corner!
                if (b.height >= 12.0f) {
                    const int signHash = std::abs(static_cast<int>(c.x * 5.0f + c.z * 11.0f)) % 4;
                    static constexpr Color kBladeCols[4] = {
                        {52, 226, 255, 255},
                        {255, 76, 178, 255},
                        {255, 198, 56, 255},
                        {74, 246, 152, 255}
                    };
                    const Color neonCol = kBladeCols[signHash];
                    const float cornerX = b.footprint.max.x + 0.72f;
                    const float cornerZ = (signHash % 2 == 0) ? (b.footprint.min.z + 1.4f) : (b.footprint.max.z - 1.4f);
                    const float bladeH = std::min(5.2f, b.height * 0.34f);
                    const float bladeY = 4.6f + bladeH * 0.5f;
                    // Twin dark steel wall mounting struts
                    drawModelStyled(cubeModel_, {b.footprint.max.x + 0.32f, bladeY + bladeH * 0.34f, cornerZ},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.72f, 0.09f, 0.09f},
                                    Color{38, 42, 50, 255}, 0.40f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {b.footprint.max.x + 0.32f, bladeY - bladeH * 0.34f, cornerZ},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.72f, 0.09f, 0.09f},
                                    Color{38, 42, 50, 255}, 0.40f, 0.72f, 0.0f, MatKind::Standard);
                    // Vertical blade housing + double-sided emissive neon tube strips
                    drawModelStyled(cubeModel_, {cornerX, bladeY, cornerZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.82f, bladeH, 0.20f}, Color{24, 28, 36, 255},
                                    0.35f, 0.55f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {cornerX, bladeY, cornerZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.62f, bladeH * 0.88f, 0.24f}, neonCol,
                                    0.12f, 0.0f, night ? 2.5f : 0.72f, MatKind::EmissiveGlow);
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

            // 3D Vertical Corner Pilasters & Structural Facade Mullion Columns for Realistic Depth
            if (b.height > 12.0f) {
                const bool isGlass = (b.style == BuildingStyle::GlassTower);
                const Color pilasterCol = toRayColor(b.accentColor);
                const float pilRough = isGlass ? 0.24f : 0.68f;
                const float pilMetal = isGlass ? 0.72f : 0.18f;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        drawModelStyled(cubeModel_,
                                        {c.x + static_cast<float>(sx) * (he.x - 0.14f),
                                         b.height * 0.5f,
                                         c.z + static_cast<float>(sz) * (he.z - 0.14f)},
                                        {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.44f, b.height, 0.44f}, pilasterCol, pilRough, pilMetal, 0.0f, MatKind::Standard);
                    }
                }
            }
        }
    }

    drawLandmarkStructures(world, focus, shadowPass);
}


void Renderer3D::drawLandmarkStructures(const GameWorld& world, const Vec2& focus, bool shadowPass) {
    const bool night = world.isNightTime();
    const float animT = static_cast<float>(GetTime());
    const float cullDistSq = shadowPass ? (190.0f * 190.0f) : (380.0f * 380.0f);

    // 1. 6 City Stunt Jump Ramps (Generated via Python CAD pipeline -> generated::kCityStuntRamps)
    const Color steelGray{62, 68, 78, 255};
    const Color deckAsphalt{44, 48, 56, 255};
    const Color hazardYellow{255, 204, 32, 255};
    for (const auto& r : generated::kCityStuntRamps) {
        const Vec2 rPos{r.centerX, r.centerZ};
        if ((rPos - focus).lengthSq() > cullDistSq) continue;

        const float len = r.length;
        const float wid = r.width;
        const float halfLen = len * 0.5f;
        const float halfWid = wid * 0.5f;
        const float slopeRad = std::atan2(r.height, len);
        const float slopeDeg = slopeRad * kRadToDeg;
        const float deckLen = std::sqrt(len * len + r.height * r.height);
        const float yawDeg = -std::atan2(r.dirZ, r.dirX) * kRadToDeg;

        rlPushMatrix();
        rlTranslatef(r.centerX, 0.02f, r.centerZ);
        rlRotatef(yawDeg, 0.0f, 1.0f, 0.0f);

        // Vertical steel support trusses under the elevated half of the ramp
        for (int side = -1; side <= 1; side += 2) {
            const float sz = static_cast<float>(side) * (halfWid - 0.22f);
            drawModelStyled(cubeModel_, {halfLen * 0.88f, r.height * 0.48f, sz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.32f, r.height * 0.96f, 0.28f}, steelGray, 0.42f, 0.75f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {halfLen * 0.20f, r.height * 0.30f, sz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.28f, r.height * 0.60f, 0.24f}, steelGray, 0.42f, 0.75f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {halfLen * 0.88f, r.height * 0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.24f, 0.22f, wid - 0.20f}, steelGray, 0.42f, 0.75f, 0.0f, MatKind::Standard);

        // Inclined structural steel deck + high-visibility chevron hazard rails
        rlPushMatrix();
        rlTranslatef(0.0f, r.height * 0.5f, 0.0f);
        rlRotatef(slopeDeg, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {deckLen, 0.22f, wid}, deckAsphalt, 0.68f, 0.22f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {0.0f, 0.20f, static_cast<float>(side) * (halfWid - 0.12f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {deckLen, 0.24f, 0.18f}, hazardYellow, 0.25f, 0.20f,
                            night ? 1.4f : 0.35f, MatKind::EmissiveGlow);
        }
        if (!shadowPass) {
            for (int st = -2; st <= 2; ++st) {
                drawModelStyled(cubeModel_, {static_cast<float>(st) * (deckLen * 0.18f), 0.125f, 0.0f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.32f, 0.03f, wid * 0.76f}, Color{56, 232, 255, 255},
                                0.15f, 0.0f, night ? 2.2f : 0.8f, MatKind::EmissiveGlow);
            }
        }
        rlPopMatrix();
        rlPopMatrix();
    }

    // 2. Golden Bay Suspension Bridge (North Bay Waterfront: x = 0, z = 342..396)
    const Vec2 bridgeCenter{0.0f, 369.0f};
    if ((bridgeCenter - focus).lengthSq() <= cullDistSq * 1.35f) {
        const Color bridgeRed{194, 52, 42, 255};
        const Color cableSteel{202, 210, 222, 255};
        const float towerZ[2] = {352.0f, 386.0f};
        const float towerHalfW = 10.6f;
        const float towerH = 42.0f;

        // Bridge deck truss under-structure
        drawModelStyled(cubeModel_, {0.0f, 0.45f, 369.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {22.4f, 0.75f, 62.0f}, steelGray, 0.52f, 0.65f, 0.0f, MatKind::Standard);

        // Twin 42m H-Frame Suspension Towers + 3 Horizontal Portal Struts each
        for (int tz = 0; tz < 2; ++tz) {
            const float z = towerZ[tz];
            for (int side = -1; side <= 1; side += 2) {
                const float x = static_cast<float>(side) * towerHalfW;
                drawModelStyled(cubeModel_, {x, towerH * 0.5f, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.65f, towerH, 1.85f}, bridgeRed, 0.42f, 0.52f, 0.0f, MatKind::Standard);
            }
            for (int strut = 0; strut < 3; ++strut) {
                const float sy = 15.0f + static_cast<float>(strut) * 12.0f;
                drawModelStyled(cubeModel_, {0.0f, sy, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {towerHalfW * 2.0f + 1.4f, 1.35f, 1.45f}, bridgeRed, 0.42f, 0.52f, 0.0f, MatKind::Standard);
            }
            if (!shadowPass) {
                // FAA red aviation warning beacons atop each tower leg
                for (int side = -1; side <= 1; side += 2) {
                    drawModelStyled(sphereModel_, {static_cast<float>(side) * towerHalfW, towerH + 0.6f, z},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.42f, 0.42f, 0.42f},
                                    Color{255, 42, 42, 255}, 0.1f, 0.0f, 3.2f, MatKind::EmissiveGlow);
                }
            }
        }

        // Main Catenary Suspension Cables & Vertical Suspender Ropes (from generated::kSuspensionBridgeCatenary)
        const float spanMidZ = 0.5f * (towerZ[0] + towerZ[1]);
        const float spanHalfZ = 0.5f * (towerZ[1] - towerZ[0]);
        for (int side = -1; side <= 1; side += 2) {
            const float x = static_cast<float>(side) * towerHalfW;
            for (std::size_t s = 0; s + 1 < generated::kSuspensionBridgeCatenary.size(); ++s) {
                const float z0 = spanMidZ + generated::kSuspensionBridgeCatenary[s].x * spanHalfZ;
                const float z1 = spanMidZ + generated::kSuspensionBridgeCatenary[s + 1].x * spanHalfZ;
                const float y0 = 14.5f + generated::kSuspensionBridgeCatenary[s].y * (towerH - 15.0f);
                const float y1 = 14.5f + generated::kSuspensionBridgeCatenary[s + 1].y * (towerH - 15.0f);
                const float midZ = 0.5f * (z0 + z1);
                const float midY = 0.5f * (y0 + y1);
                const float dz = z1 - z0;
                const float dy = y1 - y0;
                const float segLen = std::sqrt(dz * dz + dy * dy);
                const float pitchDeg = -std::atan2(dy, dz) * kRadToDeg;

                rlPushMatrix();
                rlTranslatef(x, midY, midZ);
                rlRotatef(pitchDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.32f, 0.32f, segLen + 0.08f}, bridgeRed, 0.35f, 0.68f, 0.0f, MatKind::Standard);
                rlPopMatrix();

                if (!shadowPass && (s % 2 == 1)) {
                    drawModelStyled(cylModel_, {x, 1.2f, z0}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, y0 - 1.2f, 0.07f}, cableSteel, 0.30f, 0.82f, 0.0f, MatKind::Standard);
                }
            }
        }
    }


    // 3. Sunset Beach Illuminated Ferris Wheel (x = -370.0f, z = -22.0f — cleanly clear of Sunset Pier!)
    const Vec2 ferrisPos{-370.0f, -22.0f};
    if ((ferrisPos - focus).lengthSq() <= cullDistSq) {
        const float hubY = 14.8f;
        const float wheelR = 11.2f;
        const float wheelAngle = animT * 14.0f;

        // Compact wooden boarding platform (z in [-29.5, -14.5], 6.9m clear of Sunset Pier) & twin A-frame legs
        drawModelStyled(cubeModel_, {ferrisPos.x, 0.68f, ferrisPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {10.2f, 1.36f, 15.0f}, Color{132, 96, 66, 255}, 0.78f, 0.08f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            const float lx = ferrisPos.x + static_cast<float>(side) * 2.2f;
            drawModelStyled(cubeModel_, {lx, hubY * 0.52f, ferrisPos.z - 3.6f}, {1.0f, 0.0f, 0.0f}, -15.0f,
                            {0.42f, hubY * 1.04f, 0.42f}, Color{230, 236, 245, 255}, 0.32f, 0.78f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {lx, hubY * 0.52f, ferrisPos.z + 3.6f}, {1.0f, 0.0f, 0.0f},  15.0f,
                            {0.42f, hubY * 1.04f, 0.42f}, Color{230, 236, 245, 255}, 0.32f, 0.78f, 0.0f, MatKind::Standard);
        }

        // Smooth cylindrical central steel axle hub + glowing cyan end-cap rings
        drawModelStyled(cylModel_, {ferrisPos.x + 2.6f, hubY, ferrisPos.z}, {0.0f, 0.0f, 1.0f}, 90.0f,
                        {0.62f, 5.2f, 0.62f}, Color{226, 232, 242, 255}, 0.24f, 0.84f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            for (int side = -1; side <= 1; side += 2) {
                drawModelStyled(sphereModel_, {ferrisPos.x + static_cast<float>(side) * 2.62f, hubY, ferrisPos.z},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.22f, 0.72f, 0.72f}, Color{56, 228, 255, 255},
                                0.14f, 0.0f, night ? 2.6f : 0.85f, MatKind::EmissiveGlow);
            }
        }

        // 12-Spoke Rotating Wheel (structural white steel spokes + glowing neon rim + windowed gondolas)
        for (int i = 0; i < 12; ++i) {
            const int next = (i + 1) % 12;
            const float rad = (static_cast<float>(i) * 30.0f + wheelAngle) * (3.14159265f / 180.0f);
            const float nextRad = (static_cast<float>(next) * 30.0f + wheelAngle) * (3.14159265f / 180.0f);
            const float cz = std::cos(rad) * wheelR;
            const float cy = std::sin(rad) * wheelR;
            const float nz = std::cos(nextRad) * wheelR;
            const float ny = std::sin(nextRad) * wheelR;

            const Color neonCol = (i % 2 == 0) ? Color{56, 234, 255, 255} : Color{255, 72, 212, 255};
            for (int side = -1; side <= 1; side += 2) {
                const float rx = ferrisPos.x + static_cast<float>(side) * 1.65f;
                // White structural steel radial spoke
                rlPushMatrix();
                rlTranslatef(rx, hubY + cy * 0.5f, ferrisPos.z + cz * 0.5f);
                rlRotatef(-(static_cast<float>(i) * 30.0f + wheelAngle), 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.13f, 0.13f, wheelR}, Color{235, 240, 248, 255}, 0.28f, 0.78f,
                                0.0f, MatKind::Standard);
                rlPopMatrix();

                // Neon outer rim chord segment
                const float dz = nz - cz;
                const float dy = ny - cy;
                const float chordLen = std::sqrt(dz * dz + dy * dy);
                const float chordDeg = -std::atan2(dy, dz) * kRadToDeg;
                rlPushMatrix();
                rlTranslatef(rx, hubY + 0.5f * (cy + ny), ferrisPos.z + 0.5f * (cz + nz));
                rlRotatef(chordDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.22f, 0.22f, chordLen + 0.1f}, neonCol, 0.12f, 0.0f,
                                night ? 2.8f : 0.85f, MatKind::EmissiveGlow);
                rlPopMatrix();
            }

            // Self-leveling hanging passenger gondola cabin with panoramic tinted glass & roof cap
            if (!shadowPass) {
                const Color cabCol = (i % 3 == 0) ? Color{255, 92, 118, 255}
                                   : ((i % 3 == 1) ? Color{64, 212, 255, 255} : Color{255, 214, 68, 255});
                const float gx = ferrisPos.x;
                const float gy = hubY + cy - 0.85f;
                const float gz = ferrisPos.z + cz;
                // Suspension cross-axle + lower painted gondola body
                drawModelStyled(cubeModel_, {gx, hubY + cy, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.30f, 0.12f, 0.12f}, Color{220, 226, 236, 255}, 0.30f, 0.82f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {gx, gy - 0.30f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.55f, 0.72f, 1.44f}, cabCol, 0.22f, 0.35f,
                                night ? 0.55f : 0.06f, MatKind::CarClearcoat);
                // Panoramic smoked glass upper window surround
                drawModelStyled(cubeModel_, {gx, gy + 0.28f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.42f, 0.56f, 1.36f}, Color{18, 30, 46, 255}, 0.06f, 0.22f,
                                0.0f, MatKind::CarGlass);
                // Gondola roof canopy
                drawModelStyled(cubeModel_, {gx, gy + 0.62f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.66f, 0.14f, 1.52f}, Color{240, 244, 250, 255}, 0.34f, 0.25f,
                                0.0f, MatKind::Standard);
            }
        }
    }

    // 4. Harbor Industrial Container Gantry Cranes (Harbor District: x = 248 & 278, z = 248)
    const float craneXs[2] = {248.0f, 278.0f};
    const Color craneYellow{234, 178, 38, 255};
    for (int cIdx = 0; cIdx < 2; ++cIdx) {
        const Vec2 cranePos{craneXs[cIdx], 248.0f};
        if ((cranePos - focus).lengthSq() > cullDistSq) continue;

        const float craneH = 19.5f;
        for (int lx = -1; lx <= 1; lx += 2) {
            for (int lz = -1; lz <= 1; lz += 2) {
                drawModelStyled(cubeModel_,
                                {cranePos.x + static_cast<float>(lx) * 4.2f,
                                 craneH * 0.5f,
                                 cranePos.z + static_cast<float>(lz) * 4.2f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.72f, craneH, 0.72f}, craneYellow, 0.46f, 0.62f, 0.0f, MatKind::Standard);
            }
        }
        // Horizontal portal tie beams + cantilevered 26m container boom truss
        drawModelStyled(cubeModel_, {cranePos.x, craneH, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.6f, 1.25f, 26.5f}, craneYellow, 0.44f, 0.65f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {cranePos.x, craneH + 3.8f, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.2f, 6.8f, 1.2f}, craneYellow, 0.44f, 0.65f, 0.0f, MatKind::Standard);
        // Operator cab & suspended intermodal shipping container
        drawModelStyled(cubeModel_, {cranePos.x, craneH - 1.4f, cranePos.z + 5.8f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.8f, 1.8f, 2.6f}, Color{236, 240, 246, 255}, 0.35f, 0.30f, 0.0f, MatKind::Standard);
        const Color containerCol = (cIdx == 0) ? Color{192, 54, 46, 255} : Color{38, 108, 186, 255};
        drawModelStyled(cubeModel_, {cranePos.x, 8.2f, cranePos.z + 6.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.6f, 2.7f, 6.4f}, containerCol, 0.62f, 0.45f, 0.0f, MatKind::Standard);
    }

    // 5. Overhead Highway Directional Sign Gantries across Major Avenues (double-sided N/S signage)
    struct GantryDef {
        float x;
        float z;
        bool alongX;
    };
    constexpr GantryDef kGantries[3] = {
        {  0.0f, 42.0f, true },
        { 84.0f, 42.0f, true },
        {-84.0f, 42.0f, true },
    };
    const Color gantrySteel{148, 156, 168, 255};
    const Color highwayGreen{18, 108, 72, 255};
    for (const auto& g : kGantries) {
        const Vec2 gPos{g.x, g.z};
        if ((gPos - focus).lengthSq() > cullDistSq) continue;

        const float span = 18.8f;
        const float h = 6.8f;
        for (int side = -1; side <= 1; side += 2) {
            const float px = g.alongX ? (g.x + static_cast<float>(side) * (span * 0.5f)) : g.x;
            const float pz = g.alongX ? g.z : (g.z + static_cast<float>(side) * (span * 0.5f));
            drawModelStyled(cylModel_, {px, 0.05f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, h, 0.22f}, gantrySteel, 0.38f, 0.78f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {g.x, h - 0.35f, g.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {span + 0.5f, 0.42f, 0.42f}, gantrySteel, 0.38f, 0.78f, 0.0f, MatKind::Standard);
        // Reflective green highway directional sign boards with crisp double-sided route graphics
        for (int side = -1; side <= 1; side += 2) {
            const float sx = g.x + static_cast<float>(side) * 3.8f;
            drawModelStyled(cubeModel_, {sx, h - 0.35f, g.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {5.2f, 1.65f, 0.16f}, highwayGreen, 0.30f, 0.25f,
                            night ? 0.65f : 0.12f, MatKind::EmissiveGlow);
            if (!shadowPass) {
                const float signGlow = night ? 1.8f : 0.42f;
                for (int face = -1; face <= 1; face += 2) {
                    const float fz = g.z + static_cast<float>(face) * 0.095f;
                    // Crisp reflective white outer border frame (top/bottom/left/right)
                    drawModelStyled(cubeModel_, {sx, h + 0.41f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {4.96f, 0.06f, 0.025f}, WHITE, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {sx, h - 1.11f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {4.96f, 0.06f, 0.025f}, WHITE, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {sx - 2.45f, h - 0.35f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 1.48f, 0.025f}, WHITE, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {sx + 2.45f, h - 0.35f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 1.48f, 0.025f}, WHITE, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    // Interstate-style route shield badge + destination text bars
                    const float badgeX = sx - static_cast<float>(face) * 1.65f;
                    const float textX = sx + static_cast<float>(face) * 0.42f;
                    drawModelStyled(cubeModel_, {badgeX, h - 0.12f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.72f, 0.68f, 0.028f}, Color{24, 92, 208, 255}, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {badgeX, h + 0.16f, fz + static_cast<float>(face) * 0.008f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.70f, 0.16f, 0.025f}, Color{216, 42, 42, 255}, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {textX, h + 0.04f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.95f, 0.24f, 0.028f}, WHITE, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    drawModelStyled(cubeModel_, {textX - static_cast<float>(face) * 0.25f, h - 0.34f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.25f, 0.16f, 0.028f}, Color{230, 240, 235, 255}, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    // Downward yellow lane-exit chevron arrows
                    for (int a = -1; a <= 1; a += 2) {
                        const float ax = sx + static_cast<float>(a) * 1.05f;
                        drawModelStyled(cubeModel_, {ax, h - 0.78f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.14f, 0.28f, 0.028f}, Color{255, 214, 64, 255}, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                        drawModelStyled(coneModel_, {ax, h - 0.86f, fz}, {0.0f, 0.0f, 1.0f}, 180.0f,
                                        {0.18f, 0.22f, 0.03f}, Color{255, 214, 64, 255}, 0.15f, 0.0f, signGlow, MatKind::EmissiveGlow);
                    }
                }
            }
        }
    }
}

void Renderer3D::drawLivingWorldAmbience(const GameWorld& world, const Vec2& focus, const Vec3& camPos) {
    (void)camPos;
    const bool night = world.isNightTime();
    const float animT = static_cast<float>(GetTime());
    constexpr float kWaterY = -0.85f; // Top surface of coastal OceanWater plane

    // 1. Bobbing Coastal Luxury Sailboats & Motor Yachts off Sunset Beach & North Bay!
    struct VesselDef {
        float x;
        float z;
        float yawDeg;
        float scale;
        bool sailboat;
        Color stripe;
    };
    static constexpr VesselDef kVessels[5] = {
        {-462.0f, -44.0f,  18.0f, 1.05f, true,  {36, 128, 214, 255}},
        {-495.0f,  36.0f, -24.0f, 1.20f, false, {24, 48, 86, 255}},
        {-448.0f,  92.0f,  42.0f, 0.95f, true,  {218, 56, 54, 255}},
        {-110.0f, 438.0f,  84.0f, 1.32f, false, {28, 96, 142, 255}},
        {  96.0f, 452.0f, -72.0f, 1.00f, true,  {242, 188, 54, 255}}
    };
    for (int i = 0; i < 5; ++i) {
        const auto& b = kVessels[i];
        const Vec2 bPos{b.x, b.z};
        if ((bPos - focus).lengthSq() > 520.0f * 520.0f) continue;

        const float phase = static_cast<float>(i) * 1.37f;
        const float heave = 0.14f * std::sin(animT * 1.45f + phase);
        const float rollDeg = 2.8f * std::sin(animT * 1.15f + phase * 1.4f);
        const float pitchDeg = 1.6f * std::cos(animT * 0.95f + phase);

        rlPushMatrix();
        rlTranslatef(b.x, kWaterY + heave, b.z);
        rlRotatef(b.yawDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(pitchDeg, 0.0f, 0.0f, 1.0f);
        rlRotatef(rollDeg, 1.0f, 0.0f, 0.0f);
        rlScalef(b.scale, b.scale, b.scale);

        // Sculpted white hull + pointed bow + colored waterline stripe + teak deck
        drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.2f, 1.25f, 3.2f}, Color{242, 245, 250, 255}, 0.25f, 0.15f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(coneModel_, {4.6f, 0.45f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {1.42f, 3.2f, 1.60f}, Color{242, 245, 250, 255}, 0.25f, 0.15f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(cubeModel_, {0.2f, 0.26f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.6f, 0.24f, 3.28f}, b.stripe, 0.22f, 0.25f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(cubeModel_, {-0.2f, 1.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {8.4f, 0.10f, 2.85f}, Color{176, 134, 88, 255}, 0.72f, 0.0f, 0.0f, MatKind::Standard);

        if (b.sailboat) {
            // Compact cabin coachroof + tall brushed aluminum mast + billowing cream mainsail & jib!
            drawModelStyled(cubeModel_, {-0.6f, 1.55f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.6f, 0.85f, 2.05f}, Color{246, 248, 252, 255}, 0.28f, 0.15f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.6f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 11.4f, 0.10f}, Color{210, 218, 228, 255}, 0.25f, 0.85f, 0.0f, MatKind::Standard);
            // Boom + billowing mainsail (slight wind curve)
            const float sailSway = 5.0f * std::sin(animT * 0.9f + phase);
            rlPushMatrix();
            rlTranslatef(0.6f, 2.6f, 0.0f);
            rlRotatef(sailSway, 0.0f, 1.0f, 0.0f);
            drawModelStyled(cubeModel_, {-2.1f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {4.2f, 0.12f, 0.12f}, Color{200, 208, 220, 255}, 0.30f, 0.80f, 0.0f, MatKind::Standard);
            drawModelStyled(coneModel_, {-1.45f, 0.15f, 0.0f}, {0.0f, 0.0f, 1.0f}, -10.0f,
                            {2.05f, 9.2f, 0.16f}, Color{250, 248, 240, 255}, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            rlPopMatrix();
            // Forward triangular jib sail
            drawModelStyled(coneModel_, {2.45f, 2.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, 14.0f,
                            {1.45f, 7.6f, 0.14f}, Color{246, 244, 236, 255}, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            // Masthead white anchor light at night
            if (night) {
                drawModelStyled(sphereModel_, {0.6f, 12.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 0.18f, 0.18f}, Color{255, 242, 198, 255}, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
            }
        } else {
            // Luxury 2-Deck Motor Yacht superstructure + panoramic smoked-glass bridge + radar arch!
            drawModelStyled(cubeModel_, {-0.5f, 1.85f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {5.8f, 1.45f, 2.55f}, Color{245, 247, 252, 255}, 0.22f, 0.18f, 0.0f, MatKind::CarClearcoat);
            drawModelStyled(cubeModel_, {0.2f, 1.98f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {4.6f, 0.64f, 2.62f}, night ? Color{255, 224, 156, 255} : Color{18, 32, 48, 255},
                            0.08f, 0.20f, night ? 1.45f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::CarGlass);
            // Upper flybridge deck + swept radar mast
            drawModelStyled(cubeModel_, {-1.1f, 2.95f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.4f, 0.78f, 2.15f}, Color{248, 250, 254, 255}, 0.24f, 0.15f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-1.4f, 3.72f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                            {0.28f, 1.10f, 2.05f}, Color{236, 240, 248, 255}, 0.28f, 0.35f, 0.0f, MatKind::Standard);
        }

        // Port (Red) & Starboard (Green) Marine Navigation Lights
        if (night) {
            drawModelStyled(sphereModel_, {2.2f, 1.38f, -1.52f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.14f, 0.14f, 0.14f}, Color{255, 48, 48, 255}, 0.1f, 0.0f, 2.4f, MatKind::EmissiveGlow);
            drawModelStyled(sphereModel_, {2.2f, 1.38f,  1.52f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.14f, 0.14f, 0.14f}, Color{52, 248, 108, 255}, 0.1f, 0.0f, 2.4f, MatKind::EmissiveGlow);
        }
        rlPopMatrix();
    }

    // 2. Anchored Southeast Harbor Container Freighter Ship (x = 432, z = 248)
    const Vec2 shipPos{432.0f, 248.0f};
    if ((shipPos - focus).lengthSq() < 540.0f * 540.0f) {
        const float shipRoll = 0.8f * std::sin(animT * 0.65f);
        rlPushMatrix();
        rlTranslatef(shipPos.x, kWaterY, shipPos.z);
        rlRotatef(shipRoll, 0.0f, 0.0f, 1.0f);
        // Red-oxide lower hull + dark navy freeboard + bow bulb
        drawModelStyled(cubeModel_, {0.0f, 1.15f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {14.0f, 2.8f, 52.0f}, Color{154, 44, 38, 255}, 0.68f, 0.25f, 0.0f, MatKind::IndustrialMetal);
        drawModelStyled(cubeModel_, {0.0f, 3.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {14.4f, 2.4f, 52.4f}, Color{28, 40, 56, 255}, 0.58f, 0.35f, 0.0f, MatKind::IndustrialMetal);
        // Multi-colored stacked intermodal cargo containers along main deck
        static constexpr Color kCargoCols[4] = {
            {194, 54, 44, 255}, {36, 106, 178, 255}, {214, 152, 42, 255}, {44, 136, 86, 255}
        };
        for (int row = 0; row < 4; ++row) {
            const float cz = -17.0f + static_cast<float>(row) * 8.2f;
            for (int col = -1; col <= 1; ++col) {
                const float cx = static_cast<float>(col) * 3.6f;
                const int layers = (row == 1 || row == 2) ? 2 : 1;
                for (int l = 0; l < layers; ++l) {
                    const Color cc = kCargoCols[(row + col + l + 4) % 4];
                    drawModelStyled(cubeModel_, {cx, 6.1f + static_cast<float>(l) * 2.7f, cz},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {3.1f, 2.55f, 7.2f},
                                    cc, 0.62f, 0.38f, 0.0f, MatKind::IndustrialMetal);
                }
            }
        }
        // Tall white aft bridge superstructure + lit wheelhouse windows + orange funnel
        drawModelStyled(cubeModel_, {0.0f, 9.8f, 18.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {12.2f, 10.2f, 7.8f}, Color{236, 240, 246, 255}, 0.45f, 0.15f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, 13.4f, 14.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {10.4f, 1.35f, 0.35f}, night ? Color{255, 226, 148, 255} : Color{22, 34, 48, 255},
                        0.10f, 0.25f, night ? 1.8f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::CarGlass);
        drawModelStyled(cubeModel_, {0.0f, 16.4f, 20.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.6f, 4.2f, 3.2f}, Color{226, 86, 38, 255}, 0.50f, 0.30f, 0.0f, MatKind::Standard);
        rlPopMatrix();
    }

    // 3. Soaring Flapping Coastal Seagulls gliding over Sunset Beach, Pier & Central Park!
    struct FlockCenter { float x; float z; float y; float r; };
    static constexpr FlockCenter kFlocks[2] = {
        {-382.0f, -12.0f, 18.5f, 26.0f},
        { -42.0f, -42.0f, 22.0f, 24.0f}
    };
    for (int f = 0; f < 2; ++f) {
        const Vec2 fc{kFlocks[f].x, kFlocks[f].z};
        if ((fc - focus).lengthSq() > 260.0f * 260.0f) continue;
        for (int g = 0; g < 5; ++g) {
            const float gf = static_cast<float>(g);
            const float orbitAng = animT * (0.34f + gf * 0.04f) + gf * 1.26f + static_cast<float>(f) * 2.1f;
            const float rad = kFlocks[f].r * (0.62f + 0.38f * std::sin(gf * 2.3f));
            const float gx = fc.x + std::cos(orbitAng) * rad;
            const float gz = fc.z + std::sin(orbitAng) * rad;
            const float gy = kFlocks[f].y + 2.4f * std::sin(animT * 0.8f + gf * 1.7f);
            const float headingDeg = -orbitAng * kRadToDeg - 90.0f;
            const float flapDeg = 28.0f * std::sin(animT * (6.2f + gf * 0.45f) + gf * 2.0f);

            rlPushMatrix();
            rlTranslatef(gx, gy, gz);
            rlRotatef(headingDeg, 0.0f, 1.0f, 0.0f);
            drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.32f, 0.10f, 0.12f}, Color{246, 248, 252, 255}, 0.55f, 0.0f, 0.0f, MatKind::Standard);
            for (int side = -1; side <= 1; side += 2) {
                rlPushMatrix();
                rlRotatef(static_cast<float>(side) * flapDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {-0.02f, 0.0f, static_cast<float>(side) * 0.34f},
                                {0.0f, 1.0f, 0.0f}, static_cast<float>(-side) * 14.0f,
                                {0.16f, 0.03f, 0.68f}, Color{240, 244, 250, 255}, 0.55f, 0.0f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }
            rlPopMatrix();
        }
    }

    // 4. High-Altitude Commercial City Airliner with Blinking Nav Strobes & Dual Jet Contrails!
    {
        const float cycle = std::fmod(animT * 18.0f, 1100.0f) - 550.0f;
        const float px = cycle;
        const float py = 138.0f;
        const float pz = -140.0f + cycle * 0.22f;
        const bool strobeOn = (std::fmod(animT * 1.4f, 1.0f) < 0.20f);

        rlPushMatrix();
        rlTranslatef(px, py, pz);
        rlRotatef(-12.4f, 0.0f, 1.0f, 0.0f);
        // Fuselage + swept wings + vertical tailfin
        drawModelStyled(cylModel_, {-7.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {1.15f, 14.0f, 1.15f}, Color{242, 246, 252, 255}, 0.25f, 0.45f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-0.5f, -0.1f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.4f, 0.26f, 16.2f}, Color{226, 232, 240, 255}, 0.28f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-6.4f, 1.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                        {2.6f, 3.6f, 0.22f}, Color{36, 112, 204, 255}, 0.30f, 0.35f, 0.0f, MatKind::Standard);
        // Blinking port red / starboard green / tail white strobes
        if (strobeOn || night) {
            drawModelStyled(sphereModel_, {-0.8f, 0.0f, -8.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.42f, 0.42f, 0.42f}, Color{255, 42, 42, 255}, 0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
            drawModelStyled(sphereModel_, {-0.8f, 0.0f,  8.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.42f, 0.42f, 0.42f}, Color{48, 255, 96, 255}, 0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
        }
        // Subtle twin jet contrail ribbons trailing behind engines
        for (int s = -1; s <= 1; s += 2) {
            DrawLine3D({-2.5f, -0.4f, static_cast<float>(s) * 2.8f},
                       {-95.0f, -0.4f, static_cast<float>(s) * 3.8f},
                       Color{235, 242, 252, static_cast<unsigned char>(night ? 45 : 105)});
        }
        rlPopMatrix();
    }

    // 5. Active Circling SBPD Tactical Police Helicopter when Wanted Level >= 3 Stars!
    if (world.player().wantedLevel >= 3 && !world.player().wasted) {
        const Vec2 targetXZ = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
        const float targetY = (world.playerVehicle() != nullptr) ? world.playerVehicle()->posY : world.player().posY;
        const float orbitAng = animT * 0.56f;
        const float orbitR = 30.0f;
        const float heliX = targetXZ.x + std::cos(orbitAng) * orbitR;
        const float heliZ = targetXZ.z + std::sin(orbitAng) * orbitR;
        const float heliY = targetY + 20.5f + 1.2f * std::sin(animT * 1.1f);
        // Tangent heading along circular orbit + inward bank roll!
        const float tanYawRad = orbitAng + kHalfPi;
        const float tanYawDeg = -tanYawRad * kRadToDeg;
        const bool redFlash = (std::fmod(animT * 3.8f, 1.0f) < 0.5f);

        rlPushMatrix();
        rlTranslatef(heliX, heliY, heliZ);
        rlRotatef(tanYawDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(-11.0f, 0.0f, 0.0f, 1.0f); // Nose-down forward airspeed pitch
        rlRotatef( 20.0f, 1.0f, 0.0f, 0.0f); // Banked smoothly into the turn

        const Color heliNavy{22, 30, 44, 255};
        const Color heliWhite{240, 244, 250, 255};
        const Color heliSteel{164, 174, 188, 255};

        // Main sculpted fuselage pod + panoramic bubble nose cockpit + white SBPD belly stripe
        drawModelStyled(sphereModel_, {0.35f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.45f, 1.05f, 1.10f}, heliNavy, 0.28f, 0.45f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(sphereModel_, {1.45f, 0.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.35f, 0.86f, 0.96f}, Color{18, 34, 52, 255}, 0.06f, 0.35f, 0.0f, MatKind::CarGlass);
        drawModelStyled(cubeModel_, {0.10f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.2f, 0.24f, 1.98f}, heliWhite, 0.24f, 0.25f, 0.0f, MatKind::CarClearcoat);

        // Twin tubular landing skids & diagonal struts
        for (int s = -1; s <= 1; s += 2) {
            const float sz = static_cast<float>(s) * 1.15f;
            drawModelStyled(cylModel_, {-1.4f, -1.32f, sz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.075f, 3.6f, 0.075f}, heliSteel, 0.32f, 0.78f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.95f, -0.92f, sz * 0.78f}, {1.0f, 0.0f, 0.0f}, static_cast<float>(-s) * 24.0f,
                            {0.09f, 0.92f, 0.09f}, heliSteel, 0.35f, 0.75f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.65f, -0.92f, sz * 0.78f}, {1.0f, 0.0f, 0.0f}, static_cast<float>(-s) * 24.0f,
                            {0.09f, 0.92f, 0.09f}, heliSteel, 0.35f, 0.75f, 0.0f, MatKind::Standard);
        }

        // Tapered tail boom + horizontal stabilizer + vertical tailfin + spinning tail rotor!
        drawModelStyled(cubeModel_, {-3.35f, 0.24f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {4.6f, 0.40f, 0.38f}, heliNavy, 0.32f, 0.45f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-4.55f, 0.26f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.58f, 0.10f, 1.85f}, heliWhite, 0.35f, 0.25f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-5.45f, 0.78f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                        {0.78f, 1.55f, 0.14f}, heliNavy, 0.32f, 0.45f, 0.0f, MatKind::Standard);
        rlPushMatrix();
        rlTranslatef(-5.55f, 0.78f, 0.24f);
        rlRotatef(animT * 2400.0f, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.35f, 0.12f, 0.04f}, Color{42, 46, 54, 255}, 0.38f, 0.65f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 90.0f,
                        {1.35f, 0.12f, 0.04f}, Color{42, 46, 54, 255}, 0.38f, 0.65f, 0.0f, MatKind::Standard);
        rlPopMatrix();

        // Mast + high-speed spinning 4-blade main rotor + translucent motion-blur disc
        drawModelStyled(cylModel_, {0.15f, 0.92f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.18f, 0.75f, 0.18f}, heliSteel, 0.30f, 0.80f, 0.0f, MatKind::Standard);
        rlPushMatrix();
        rlTranslatef(0.15f, 1.65f, 0.0f);
        rlRotatef(animT * 1650.0f, 0.0f, 1.0f, 0.0f);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {8.8f, 0.055f, 0.24f}, Color{34, 38, 46, 255}, 0.35f, 0.65f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                        {8.8f, 0.055f, 0.24f}, Color{34, 38, 46, 255}, 0.35f, 0.65f, 0.0f, MatKind::Standard);
        rlPopMatrix();
        rlDrawRenderBatchActive();
        rlSetTexture(0);
        rlDisableDepthMask();
        DrawCylinder({0.15f, 1.62f, 0.0f}, 4.35f, 4.35f, 0.02f, 20, Color{180, 195, 215, 36});
        rlDrawRenderBatchActive();
        rlEnableDepthMask();

        // Flashing Red/Blue Police Strobe Beacon + Nose Gimbal Spotlight Lens!
        drawModelStyled(sphereModel_, {0.15f, -0.98f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.26f, 0.26f, 0.26f},
                        redFlash ? Color{255, 38, 48, 255} : Color{42, 138, 255, 255},
                        0.1f, 0.0f, 3.8f, MatKind::EmissiveGlow);
        drawModelStyled(sphereModel_, {2.15f, -0.58f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.34f, 0.34f, 0.34f}, Color{245, 250, 255, 255},
                        0.08f, 0.0f, 4.0f, MatKind::EmissiveGlow);
        rlPopMatrix();

        // Dramatic Additive Volumetric Sweeping Helicopter Searchlight Cone (open-ended gradient frustum, zero depth artifacts!)
        const Vec3 bStart{heliX, heliY - 0.85f, heliZ};
        const Vec3 bEnd{
            targetXZ.x + 1.8f * std::sin(animT * 1.4f),
            targetY + 0.06f,
            targetXZ.z + 1.8f * std::cos(animT * 1.1f)
        };
        const Vec3 bDir = (bEnd - bStart).normalized();
        Vec3 refUp{0.0f, 1.0f, 0.0f};
        if (std::fabs(bDir.y) > 0.92f) refUp = {1.0f, 0.0f, 0.0f};
        const Vec3 bRight = Vec3{
            bDir.y * refUp.z - bDir.z * refUp.y,
            bDir.z * refUp.x - bDir.x * refUp.z,
            bDir.x * refUp.y - bDir.y * refUp.x
        }.normalized();
        const Vec3 bFwd = Vec3{
            bRight.y * bDir.z - bRight.z * bDir.y,
            bRight.z * bDir.x - bRight.x * bDir.z,
            bRight.x * bDir.y - bRight.y * bDir.x
        }.normalized();

        rlDrawRenderBatchActive();
        rlSetTexture(0);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ADDITIVE);
        constexpr int kBeamSides = 24;
        const float rTop = 0.38f;
        const float rBot = 4.6f;
        const unsigned char aTop = static_cast<unsigned char>(night ? 76 : 32);
        const unsigned char aBot = static_cast<unsigned char>(night ? 18 : 8);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < kBeamSides; ++i) {
            const float a0 = static_cast<float>(i) * (kTwoPi / static_cast<float>(kBeamSides));
            const float a1 = static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(kBeamSides));
            const Vec3 d0 = bRight * std::cos(a0) + bFwd * std::sin(a0);
            const Vec3 d1 = bRight * std::cos(a1) + bFwd * std::sin(a1);
            const Vec3 t0 = bStart + d0 * rTop;
            const Vec3 t1 = bStart + d1 * rTop;
            const Vec3 g0 = bEnd + d0 * rBot;
            const Vec3 g1 = bEnd + d1 * rBot;

            rlColor4ub(222, 240, 255, aTop); rlVertex3f(t0.x, t0.y, t0.z);
            rlColor4ub(222, 240, 255, aTop); rlVertex3f(t1.x, t1.y, t1.z);
            rlColor4ub(196, 224, 255, aBot); rlVertex3f(g0.x, g0.y, g0.z);

            rlColor4ub(222, 240, 255, aTop); rlVertex3f(t1.x, t1.y, t1.z);
            rlColor4ub(196, 224, 255, aBot); rlVertex3f(g1.x, g1.y, g1.z);
            rlColor4ub(196, 224, 255, aBot); rlVertex3f(g0.x, g0.y, g0.z);
        }
        rlEnd();
        DrawCylinder({bEnd.x, bEnd.y, bEnd.z}, rBot, rBot, 0.02f, 24,
                     Color{225, 242, 255, static_cast<unsigned char>(night ? 56 : 22)});
        rlDrawRenderBatchActive();
        EndBlendMode();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }
}

void Renderer3D::drawTreesAndProps(const GameWorld& world, const Vec2& focus, const Vec3& camPos, bool shadowPass) {
    const bool night = world.isNightTime();
    const float animT = static_cast<float>(GetTime());
    const float treeCull = shadowPass ? (135.0f * 135.0f) : (235.0f * 235.0f);

    for (const auto& t : world.city().trees()) {
        if ((t.pos - focus).lengthSq() > treeCull) continue;
        const int treeHash = std::abs(static_cast<int>(t.pos.x * 7.0f + t.pos.z * 13.0f));
        const float windSway = std::sin(animT * 1.45f + t.pos.x * 0.11f + t.pos.z * 0.09f);

        // Beveled concrete urban sidewalk tree planter curb, soil mulch & warm night landscape uplight!
        if (!shadowPass && t.inPlanter) {
            drawModelStyled(cubeModel_, {t.pos.x, 0.17f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.58f, 0.22f, 1.58f}, Color{154, 158, 166, 255},
                            0.72f, 0.05f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {t.pos.x, 0.28f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.66f, 0.06f, 1.66f}, Color{186, 190, 198, 255},
                            0.65f, 0.05f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {t.pos.x, 0.28f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.28f, 0.08f, 1.28f}, Color{64, 46, 32, 255},
                            0.90f, 0.0f, 0.0f, MatKind::Standard);
            if (night) {
                drawModelStyled(sphereModel_, {t.pos.x + 0.48f, 0.34f, t.pos.z + 0.48f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.08f, 0.05f, 0.08f}, Color{255, 224, 152, 255},
                                0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
            }
        }

        if (t.palmStyle) {
            // Iconic California Coastal Palm Tree: 4-Segment Curved Bark Trunk + Coconut Cluster + 8 Arched Drooping Fronds!
            const float leanYaw = static_cast<float>(treeHash % 360);
            const float segH = t.trunkHeight * 0.28f;
            const Color barkLight{142, 104, 70, 255};
            const Color barkDark{108, 76, 50, 255};

            rlPushMatrix();
            rlTranslatef(t.pos.x, 0.06f, t.pos.z);
            rlRotatef(leanYaw, 0.0f, 1.0f, 0.0f);
            // Flared fibrous root collar
            drawModelStyled(coneModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.34f, 0.55f, 0.34f}, barkDark, 0.88f, 0.0f, 0.0f, MatKind::Standard);
            for (int seg = 0; seg < 4; ++seg) {
                const float r = 0.25f - static_cast<float>(seg) * 0.025f;
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {r, segH * 1.05f, r}, (seg % 2 == 0) ? barkLight : barkDark,
                                0.86f, 0.0f, 0.0f, MatKind::Standard);
                if (!shadowPass && seg > 0) {
                    // Fibrous palm bark ring collar
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {r * 1.15f, 0.09f, r * 1.15f}, barkDark, 0.90f, 0.0f, 0.0f, MatKind::Standard);
                }
                rlTranslatef(0.0f, segH, 0.0f);
                rlRotatef(-4.5f - static_cast<float>(seg) * 0.9f + windSway * 0.45f, 0.0f, 0.0f, 1.0f);
            }

            // Crown heart + cluster of 3 tropical coconuts nestled beneath fronds!
            if (!shadowPass) {
                drawModelStyled(sphereModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 0.22f, 0.24f}, Color{84, 128, 56, 255}, 0.78f, 0.0f, 0.0f, MatKind::Standard);
                for (int c = 0; c < 3; ++c) {
                    const float ca = static_cast<float>(c) * (kTwoPi / 3.0f);
                    drawModelStyled(sphereModel_, {std::cos(ca) * 0.20f, -0.14f, std::sin(ca) * 0.20f},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.13f, 0.15f, 0.13f},
                                    Color{96, 62, 34, 255}, 0.75f, 0.0f, 0.0f, MatKind::Standard);
                }
            }

            // 8 Gracefully Arched 2-Segment Drooping Tropical Palm Fronds swaying gently in coastal wind!
            const int frondCount = shadowPass ? 6 : 8;
            for (int f = 0; f < frondCount; ++f) {
                const bool upperTier = (f % 2 == 0);
                const float fAng = static_cast<float>(f) * (360.0f / static_cast<float>(frondCount)) + static_cast<float>(treeHash % 45);
                const float liftDeg = (upperTier ? 34.0f : 16.0f) + windSway * 2.6f;
                const Color frondCol = upperTier ? Color{78, 174, 76, 255} : Color{54, 146, 62, 255};
                const float innerLen = t.crownRadius * (upperTier ? 0.68f : 0.74f);
                const float outerLen = t.crownRadius * (upperTier ? 0.76f : 0.82f);

                rlPushMatrix();
                rlRotatef(fAng, 0.0f, 1.0f, 0.0f);
                // Inner rising arch segment
                rlRotatef(liftDeg, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cubeModel_, {innerLen * 0.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {innerLen, 0.075f, 0.44f}, frondCol, 0.62f, 0.0f, 0.0f, MatKind::FoliageGrass);
                // Outer gracefully drooping tapered leaf tip
                rlTranslatef(innerLen * 0.94f, 0.0f, 0.0f);
                rlRotatef(-48.0f + windSway * 3.2f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(coneModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.10f, outerLen, 0.34f}, Color{64, 158, 68, 255},
                                0.62f, 0.0f, 0.0f, MatKind::FoliageGrass);
                rlPopMatrix();
            }
            rlPopMatrix();
        } else {
            // Organic Sculpted Park/Boulevard Deciduous Tree: Flared Roots + 3 Woody Boughs + 6-Lobe Multi-Tone Crown!
            const Color barkCol{102, 72, 48, 255};
            const Color barkDark{82, 56, 36, 255};
            drawModelStyled(coneModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.44f, 0.68f, 0.44f}, barkDark, 0.88f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.27f, t.trunkHeight * 1.04f, 0.27f}, barkCol,
                            0.86f, 0.0f, 0.0f, MatKind::Standard);

            // 3 Spreading Woody Boughs rising into the canopy!
            if (!shadowPass) {
                for (int br = 0; br < 3; ++br) {
                    const float bYaw = static_cast<float>(br) * 120.0f + static_cast<float>(treeHash % 90);
                    rlPushMatrix();
                    rlTranslatef(t.pos.x, t.trunkHeight * 0.68f, t.pos.z);
                    rlRotatef(bYaw, 0.0f, 1.0f, 0.0f);
                    rlRotatef(42.0f, 0.0f, 0.0f, 1.0f);
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.12f, t.crownRadius * 0.88f, 0.12f}, barkDark,
                                    0.86f, 0.0f, 0.0f, MatKind::Standard);
                    rlPopMatrix();
                }
            }

            // Rich Botanical Palette: seasonal flowering Sakura / Jacaranda / Golden Elm trees along parks & boulevards!
            const int palRoll = treeHash % 10;
            Color cDeep{40, 118, 52, 255};
            Color cMain{54, 148, 66, 255};
            Color cSun{84, 178, 76, 255};
            if (palRoll == 0) {
                // Spring Sakura Cherry Blossom!
                cDeep = {208, 128, 152, 255};
                cMain = {238, 162, 186, 255};
                cSun  = {252, 196, 214, 255};
            } else if (palRoll == 1) {
                // Coastal Lavender Jacaranda!
                cDeep = {112, 92, 178, 255};
                cMain = {146, 122, 214, 255};
                cSun  = {178, 154, 238, 255};
            } else if (palRoll == 2) {
                // Warm Autumn Golden Elm!
                cDeep = {168, 112, 38, 255};
                cMain = {212, 152, 48, 255};
                cSun  = {242, 192, 68, 255};
            }

            const float cy = t.trunkHeight + t.crownRadius * 0.64f;
            const float r = t.crownRadius;
            const float swayOff = windSway * 0.06f;
            drawModelStyled(sphereModel_, {t.pos.x + swayOff, cy, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {r * 0.94f, r * 0.80f, r * 0.94f}, cMain, 0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
            if (!shadowPass) {
                // 5 Asymmetric layered shaded/sunlit crown clusters for lush natural silhouette
                const float rotBase = static_cast<float>(treeHash % 360) * (kPi / 180.0f);
                for (int l = 0; l < 4; ++l) {
                    const float la = rotBase + static_cast<float>(l) * kHalfPi + 0.35f;
                    const float dist = r * (0.42f + 0.08f * static_cast<float>(l % 2));
                    const float ly = cy + ((l % 2 == 0) ? (-r * 0.16f) : (r * 0.14f));
                    const float ls = r * ((l % 2 == 0) ? 0.62f : 0.58f);
                    drawModelStyled(sphereModel_,
                                    {t.pos.x + std::cos(la) * dist + swayOff, ly, t.pos.z + std::sin(la) * dist},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {ls, ls * 0.88f, ls},
                                    (l % 2 == 0) ? cDeep : cMain, 0.68f, 0.0f, 0.0f, MatKind::FoliageGrass);
                }
                // Sunlit crowning apex tuft
                drawModelStyled(sphereModel_, {t.pos.x + r * 0.12f + swayOff * 1.4f, cy + r * 0.44f, t.pos.z - r * 0.10f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {r * 0.62f, r * 0.54f, r * 0.62f},
                                cSun, 0.66f, 0.0f, 0.0f, MatKind::FoliageGrass);
            }
        }
    }

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

        // Gushing 3D Pressurized Fire Hydrant Water Geyser when a Hydrant is sheared off!
        if (!shadowPass && prop.broken && prop.kind == StreetPropKind::Hydrant) {
            const float geyserPulse = 4.2f + 0.45f * std::sin(animT * 14.0f + prop.homePos.x);
            DrawCylinder({prop.homePos.x, 0.08f, prop.homePos.z},
                         0.36f, 0.14f, geyserPulse, 12, Color{168, 228, 255, 165});
            DrawSphere({prop.homePos.x, 0.12f + geyserPulse, prop.homePos.z},
                       0.52f, Color{220, 246, 255, 145});
            DrawCylinder({prop.homePos.x, 0.03f, prop.homePos.z},
                         1.45f, 1.45f, 0.02f, 16, Color{82, 168, 232, 110});
        }

        rlPushMatrix();
        const float baseGroundY = ((prop.kind == StreetPropKind::BeachUmbrella) ? 0.02f : 0.08f) + prop.posY;
        rlTranslatef(prop.pos.x, baseGroundY, prop.pos.z);
        rlRotatef(-prop.yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
        if (prop.broken) {
            rlRotatef(prop.pitch * kRadToDeg, 1.0f, 0.0f, 0.0f);
            rlRotatef(prop.roll * kRadToDeg, 0.0f, 0.0f, 1.0f);
        }

        switch (prop.kind) {
            case StreetPropKind::StreetLight: {
                // Fluted Cast-Iron Pedestal Base + Metallic Streetlamp Mast + Curved Arm + Street Name Plate!
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.23f, 0.62f, 0.23f}, Color{58, 64, 74, 255},
                                    0.40f, 0.76f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.18f, 0.24f, 0.18f}, Color{68, 74, 84, 255},
                                    0.38f, 0.74f, 0.0f, MatKind::Standard);
                }
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.125f, 6.25f, 0.125f}, Color{78, 84, 94, 255},
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

                    // Green reflective street-name sign plate mounted near top of mast
                    drawModelStyled(cubeModel_, {0.0f, 5.15f, 0.42f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 0.28f, 0.96f}, Color{20, 118, 74, 255},
                                    0.35f, 0.25f, night ? 0.35f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);

                    // Pedestrian Crosswalk WALK / DON'T WALK Signal Box at eye-level (y = 2.65m)!
                    drawModelStyled(cubeModel_, {0.22f, 2.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.22f, 0.36f, 0.26f}, Color{26, 30, 36, 255},
                                    0.50f, 0.35f, 0.0f, MatKind::Standard);
                    const Color pedCol = ewGreen ? Color{225, 248, 255, 255} : Color{255, 112, 36, 255};
                    drawModelStyled(cubeModel_, {0.335f, 2.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.02f, 0.20f, 0.16f}, pedCol, 0.10f, 0.0f, 2.0f, MatKind::EmissiveGlow);

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

                    // LED luminaire head + warm night volumetric light cone & ground pool!
                    const Color lampCol = night ? Color{255, 232, 168, 255} : Color{210, 218, 228, 255};
                    drawModelStyled(cubeModel_, {1.35f, 6.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.44f, 0.11f, 0.26f}, lampCol,
                                    0.15f, 0.2f, night ? 2.5f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    if (night) {
                        rlDrawRenderBatchActive();
                        rlSetTexture(0);
                        rlDisableDepthMask();
                        BeginBlendMode(BLEND_ADDITIVE);
                        DrawCylinder({1.35f, -0.02f, 0.0f}, 4.3f, 4.3f, 0.015f, 16, Color{255, 220, 142, 34});
                        DrawCylinder({1.35f, 0.02f, 0.0f}, 0.35f, 2.85f, 6.02f, 14, Color{255, 226, 158, 14});
                        rlDrawRenderBatchActive();
                        EndBlendMode();
                        rlEnableDepthMask();
                    }
                }
                break;
            }
            case StreetPropKind::Hydrant: {
                // Multi-Part Cast-Iron Fire Hydrant: Bolted Base Flange + Side Hose Nozzles + Front Steamer Port + Bonnet Nut!
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.12f, 0.26f}, Color{188, 34, 32, 255},
                                0.42f, 0.45f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.0f, 0.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.20f, 0.62f, 0.20f}, Color{224, 42, 38, 255},
                                0.34f, 0.38f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.0f, 0.72f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.21f, 0.14f, 0.21f}, Color{236, 198, 62, 255},
                                0.28f, 0.65f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    // Twin metallic side hose nozzles + front steamer port + top pentagon nut
                    drawModelStyled(cylModel_, {0.0f, 0.52f, -0.27f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                    {0.075f, 0.54f, 0.075f}, Color{212, 218, 226, 255},
                                    0.25f, 0.82f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.06f, 0.44f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                    {0.105f, 0.22f, 0.105f}, Color{212, 218, 226, 255},
                                    0.25f, 0.82f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.065f, 0.09f, 0.065f}, Color{232, 200, 72, 255},
                                    0.24f, 0.85f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::Crate: {
                // Reinforced Cargo Crate with Outer Frame Rails, Corner Battens & Diagonal Cross-Brace!
                drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.88f, 0.86f, 0.88f}, Color{176, 130, 78, 255},
                                0.78f, 0.0f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    const Color battenCol{134, 94, 52, 255};
                    drawModelStyled(cubeModel_, {0.0f, 0.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.96f, 0.12f, 0.96f}, battenCol, 0.82f, 0.0f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {0.0f, 0.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.96f, 0.12f, 0.96f}, battenCol, 0.82f, 0.0f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 0.0f, 1.0f}, 45.0f,
                                    {0.11f, 1.05f, 0.94f}, battenCol, 0.82f, 0.0f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::TrafficCone: {
                // Weighted Black Rubber Base + High-Vis Orange Cone + Twin Reflective White Collars!
                if (!shadowPass) {
                    drawModelStyled(cubeModel_, {0.0f, 0.035f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.54f, 0.07f, 0.54f}, Color{28, 30, 36, 255},
                                    0.78f, 0.05f, 0.0f, MatKind::Standard);
                }
                drawModelStyled(coneModel_, {0.0f, 0.05f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 0.68f, 0.24f}, Color{252, 114, 28, 255},
                                0.46f, 0.0f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.155f, 0.095f, 0.155f}, Color{246, 248, 252, 255},
                                    0.20f, 0.15f, night ? 1.15f : 0.18f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.50f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.105f, 0.075f, 0.105f}, Color{246, 248, 252, 255},
                                    0.20f, 0.15f, night ? 1.15f : 0.18f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::BusShelter: {
                // Modern Glass & Anodized Aluminum Transit Shelter + Interior Bench + Night Downlight + Route Lightbox
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
                    // Overhead warm LED transit shelter ceiling light strip at night
                    drawModelStyled(cubeModel_, {0.0f, 2.28f, -0.10f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.45f, 0.04f, 0.18f}, Color{245, 248, 255, 255},
                                    0.12f, 0.0f, night ? 2.4f : 0.25f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    // Illuminated side ad / route map lightbox
                    drawModelStyled(cubeModel_, {1.62f, 1.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.10f, 2.08f, 1.24f}, Color{68, 198, 255, 255},
                                    0.15f, 0.0f, night ? 1.8f : 0.55f, MatKind::EmissiveGlow);
                    // Waiting bench inside shelter with brushed steel armrests
                    drawModelStyled(cubeModel_, {-0.25f, 0.48f, -0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.10f, 0.08f, 0.44f}, Color{168, 118, 72, 255},
                                    0.72f, 0.0f, 0.0f, MatKind::Standard);
                    for (int a = -1; a <= 1; a += 2) {
                        drawModelStyled(cubeModel_, {-0.25f + static_cast<float>(a) * 0.98f, 0.62f, -0.34f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.05f, 0.26f, 0.40f},
                                        Color{188, 196, 208, 255}, 0.28f, 0.85f, 0.0f, MatKind::Standard);
                    }
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

    // 3D Falling Rain Streaks centered around the active camera when Dynamic Rain / Wet Road ([U]) is active
    if (!shadowPass && world.isRainWeather()) {
        for (int i = 0; i < 108; ++i) {
            const float seed = static_cast<float>(i) * 19.37f;
            const float rx = camPos.x + std::fmod(std::sin(seed * 1.71f) * 43758.5f, 18.0f);
            const float rz = camPos.z + std::fmod(std::cos(seed * 2.39f) * 31415.9f, 18.0f);
            const float fallCycle = std::fmod(animT * 19.5f + seed * 3.1f, 10.3f);
            const float ry = std::max(0.2f, camPos.y - 1.8f + (10.3f - fallCycle));
            DrawLine3D({rx, ry + 0.72f, rz}, {rx - 0.08f, ry, rz - 0.05f}, Color{196, 226, 255, 138});
        }
    }
}

void Renderer3D::drawVehicle3D(const Vehicle& v, float groundSurfY, bool nightMode, bool shadowPass, bool cockpitHoodOnly) {
    const auto& spec = getVehicleSpec(v.kind);
    const float halfL = spec.halfLength;
    const float halfW = spec.halfWidth;
    const float L = halfL * 2.0f;
    const float W = halfW * 2.0f;
    const float H = spec.chassisHeight;

    // 1) Soft contact shadow + Custom LED Neon Underglow Pool anchored on actual ground/ramp surface!
    if (!shadowPass) {
        const float hoverH = std::max(0.0f, v.posY - groundSurfY);
        rlPushMatrix();
        rlTranslatef(v.pos.x, groundSurfY + 0.016f, v.pos.z);
        rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
        const float shadowScale = std::clamp(1.0f - hoverH * 0.055f, 0.45f, 1.0f);
        const unsigned char shadowA = static_cast<unsigned char>(145.0f * shadowScale);
        DrawCube({0.0f, 0.0f, 0.0f}, L * 0.98f * shadowScale, 0.008f, W * 1.02f * shadowScale, Color{8, 10, 14, shadowA});

        if (!v.destroyed && v.neonPreset > 0 && hoverH < 1.2f) {
            const Color nc = toRayColor(getNeonPresetColor(v.neonPreset));
            const float alphaMul = (nightMode ? 1.0f : 0.58f) * shadowScale;
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
            drawModelStyled(cubeModel_, {0.0f, 0.14f + hoverH, -halfW * 0.86f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.52f, 0.03f, 0.05f}, nc, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {0.0f, 0.14f + hoverH,  halfW * 0.86f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.52f, 0.03f, 0.05f}, nc, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
        }

        // Alternating Police (Red/Blue) or EMS/Fire (Red/Amber-White) Emergency Siren Ground Light Pools!
        if (!v.destroyed && v.sirenActive && hoverH < 1.5f) {
            const bool redFlash = (v.sirenPhase < 0.5f);
            const bool isEmsOrFire = (v.kind == VehicleKind::Ambulance || v.kind == VehicleKind::FireTruck);
            const unsigned char mainA = static_cast<unsigned char>(nightMode ? 96 : 62);
            const unsigned char subA  = static_cast<unsigned char>(nightMode ? 38 : 22);
            const Color leftPool  = redFlash ? Color{255, 34, 48, mainA} : Color{255, 34, 48, subA};
            const Color rightBase = isEmsOrFire ? Color{255, 198, 86, 255} : Color{36, 122, 255, 255};
            const Color rightPool = redFlash
                                        ? Color{rightBase.r, rightBase.g, rightBase.b, subA}
                                        : Color{rightBase.r, rightBase.g, rightBase.b, mainA};

            rlPushMatrix();
            rlTranslatef(-0.15f, 0.005f, -W * 1.15f);
            rlScalef(L * 0.68f, 1.0f, W * 1.25f);
            DrawCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 0.004f, 18, leftPool);
            rlPopMatrix();

            rlPushMatrix();
            rlTranslatef(-0.15f, 0.005f, W * 1.15f);
            rlScalef(L * 0.68f, 1.0f, W * 1.25f);
            DrawCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 0.004f, 18, rightPool);
            rlPopMatrix();
        }
        rlPopMatrix();
    }

    const float stanceOffsetY = v.destroyed ? 0.0f
                              : ((v.stanceStyle == 1) ? -0.032f
                              : ((v.stanceStyle == 2) ? -0.048f
                              : ((v.stanceStyle == 3) ?  0.095f : 0.0f)));

    rlPushMatrix();
    rlTranslatef(v.pos.x, stanceOffsetY + v.posY, v.pos.z);
    rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(v.bodyPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlRotatef(v.bodyRoll * kRadToDeg, 1.0f, 0.0f, 0.0f);

    const Color bodyCol = v.destroyed ? Color{44, 42, 40, 255} : toRayColor(v.paintColor);
    const Color stripeCol = toRayColor(getVehicleStripeColorRGBA(v.stripeStyle, v.stripeColor));
    const Color darkTrim{24, 27, 34, 255};
    const Color chromeCol{222, 228, 238, 255};
    const Color smokedGlass{14, 24, 38, 255};
    const MatKind paintMat = v.destroyed ? MatKind::Standard : MatKind::CarClearcoat;
    const float paintRough = v.destroyed ? 0.88f : (v.chromePaint ? 0.03f : (v.mattePaint ? 0.45f : 0.10f));
    const float paintMetal = v.destroyed ? 0.15f : (v.chromePaint ? 0.97f : (v.mattePaint ? 0.25f : 0.48f));


    // 1st-Person Driver View: draw sculpted clearcoat front hood + fenders + hood signature without cabin occlusion
    if (cockpitHoodOnly) {
        const Vec3 eyeLocal = driverSeatEyeLocal(v.kind);
        const float hoodStart = eyeLocal.x + 0.56f;
        const float hoodEnd = halfL * 0.94f;
        if (hoodEnd > hoodStart + 0.18f &&
            v.kind != VehicleKind::DeliveryVan &&
            v.kind != VehicleKind::Ambulance &&
            v.kind != VehicleKind::FireTruck) {
            const float hoodLen = hoodEnd - hoodStart;
            const float hoodMidX = 0.5f * (hoodStart + hoodEnd);
            const float hoodY = eyeLocal.y -
                                ((v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype) ? 0.26f : 0.30f);

            rlPushMatrix();
            rlTranslatef(hoodMidX, hoodY, 0.0f);
            rlRotatef(-4.6f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {hoodLen, 0.08f, W * 0.84f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
            for (int side = -1; side <= 1; side += 2) {
                drawModelStyled(cubeModel_, {0.04f, 0.028f, static_cast<float>(side) * (halfW * 0.68f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {hoodLen * 0.92f, 0.055f, W * 0.16f},
                                bodyCol, paintRough, paintMetal, 0.0f, paintMat);
            }
            if (v.stripeStyle > 0 || v.kind == VehicleKind::ClassicRoadster) {
                drawModelStyled(cubeModel_, {0.02f, 0.044f, -0.18f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hoodLen * 0.94f, 0.014f, 0.16f}, stripeCol, 0.12f, 0.35f, 0.0f, paintMat);
                drawModelStyled(cubeModel_, {0.02f, 0.044f,  0.18f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hoodLen * 0.94f, 0.014f, 0.16f}, stripeCol, 0.12f, 0.35f, 0.0f, paintMat);
            }
            if (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype ||
                v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) {
                drawModelStyled(cubeModel_, {0.08f, 0.036f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hoodLen * 0.52f, 0.025f, W * 0.26f}, darkTrim, 0.38f, 0.45f, 0.0f, MatKind::Standard);
            } else if (v.kind == VehicleKind::MuscleCar) {
                drawModelStyled(cubeModel_, {0.05f, 0.11f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.48f, 0.15f, 0.36f}, chromeCol, 0.14f, 0.92f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.30f, 0.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.04f, 0.11f, 0.32f}, Color{198, 34, 38, 255}, 0.25f, 0.3f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();

            if (v.kind == VehicleKind::PoliceCruiser || v.kind == VehicleKind::ArmoredSuv ||
                v.kind == VehicleKind::OffroadPickup) {
                drawModelStyled(cubeModel_, {halfL * 0.96f, hoodY + 0.06f, -0.32f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.05f, 0.24f, 0.055f}, darkTrim, 0.35f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {halfL * 0.96f, hoodY + 0.06f,  0.32f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.05f, 0.24f, 0.055f}, darkTrim, 0.35f, 0.65f, 0.0f, MatKind::Standard);
            }
        }
        const float mirrorX = eyeLocal.x + 0.44f;
        const float mirrorY = eyeLocal.y - 0.10f;
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(sphereModel_, {mirrorX, mirrorY, static_cast<float>(side) * (halfW * 0.96f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.11f, 0.068f, 0.095f},
                            bodyCol, paintRough, paintMetal, 0.0f, paintMat);
        }
        if (nightMode && !v.destroyed) {
            Color beamCol = toRayColor(getVehicleHeadlightColorRGBA(v.headlightColorIdx, true));
            beamCol.a = static_cast<unsigned char>((v.headlightColorIdx > 0) ? 58 : 48);
            DrawCube({halfL + 4.8f, 0.028f - stanceOffsetY, 0.0f}, 8.8f, 0.015f, W * 1.85f, beamCol);
        }
        rlPopMatrix();
        return;
    }

    // 2) Smooth-Normal Sculpted 3D Body Shell + Framed Curved Glass + Wheel-House/Aero Trim!
    auto& carMdl = carModels_[static_cast<std::size_t>(v.kind)];
    drawModelStyled(carMdl.body, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {1.0f, 1.0f, 1.0f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
    drawModelStyled(carMdl.trim, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {1.0f, 1.0f, 1.0f}, darkTrim, 0.46f, 0.35f, 0.0f, MatKind::Standard);

    if (!shadowPass) {
        drawModelStyled(carMdl.glass, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.0f, 1.0f, 1.0f}, smokedGlass, 0.05f, 0.18f, 0.0f, MatKind::CarGlass);

        // Sculpted aerodynamic wing mirrors on sleek dark stalks with rear glass & amber LED turn strip
        const float mirrorX = (v.kind == VehicleKind::FireTruck) ? (halfL * 0.62f)
                            : ((v.kind == VehicleKind::Ambulance || v.kind == VehicleKind::DeliveryVan) ? (halfL * 0.48f) : (halfL * 0.22f));
        const float mirrorY = 0.22f + H * 0.56f;
        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            // Angled dark mounting stalk from door shoulder to mirror housing
            drawModelStyled(cubeModel_, {mirrorX - 0.018f, mirrorY - 0.024f, sgn * (halfW * 0.915f)},
                            {1.0f, 0.0f, 0.0f}, sgn * 24.0f, {0.062f, 0.026f, 0.125f},
                            darkTrim, 0.40f, 0.35f, 0.0f, MatKind::Standard);
            // Aerodynamic body-color mirror cap
            drawModelStyled(sphereModel_, {mirrorX, mirrorY, sgn * (halfW * 1.00f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.105f, 0.062f, 0.092f},
                            bodyCol, paintRough, paintMetal, 0.0f, paintMat);
            // Reflective rear mirror glass + thin forward amber LED turn-signal strip
            drawModelStyled(cubeModel_, {mirrorX - 0.062f, mirrorY, sgn * (halfW * 1.00f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.016f, 0.064f, 0.112f},
                            Color{130, 152, 174, 255}, 0.05f, 0.88f, 0.0f, MatKind::CarGlass);
            drawModelStyled(cubeModel_, {mirrorX + 0.062f, mirrorY + 0.004f, sgn * (halfW * 1.005f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.022f, 0.015f, 0.088f},
                            Color{255, 176, 44, 255}, 0.12f, 0.10f, nightMode ? 1.55f : 0.45f,
                            nightMode ? MatKind::EmissiveGlow : MatKind::Standard);
        }

        // Rooftop aerodynamic shark-fin GPS antenna on modern closed-roof cars
        if (!v.destroyed &&
            (v.kind == VehicleKind::Compact || v.kind == VehicleKind::LuxurySedan ||
             v.kind == VehicleKind::PoliceCruiser || v.kind == VehicleKind::SportsCoupe ||
             v.kind == VehicleKind::JdmTuner || v.kind == VehicleKind::MuscleCar ||
             v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv ||
             v.kind == VehicleKind::Hypercar)) {
            float finY = 1.38f;
            float finX = -halfL * 0.30f;
            switch (v.kind) {
                case VehicleKind::Hypercar:      finY = 1.125f; finX = -halfL * 0.15f; break;
                case VehicleKind::SportsCoupe:   finY = 1.235f; finX = -halfL * 0.21f; break;
                case VehicleKind::JdmTuner:      finY = 1.275f; finX = -halfL * 0.25f; break;
                case VehicleKind::MuscleCar:     finY = 1.305f; finX = -halfL * 0.26f; break;
                case VehicleKind::LuxurySedan:   finY = 1.375f; finX = -halfL * 0.30f; break;
                case VehicleKind::PoliceCruiser: finY = 1.390f; finX = -halfL * 0.31f; break;
                case VehicleKind::Compact:       finY = 1.375f; finX = -halfL * 0.50f; break;
                case VehicleKind::SUV:           finY = 1.655f; finX = -halfL * 0.64f; break;
                case VehicleKind::ArmoredSuv:    finY = 1.775f; finX = -halfL * 0.68f; break;
                default: break;
            }
            drawModelStyled(cubeModel_, {finX, finY + 0.024f, 0.0f}, {0.0f, 0.0f, 1.0f}, 28.0f,
                            {0.145f, 0.058f, 0.024f}, darkTrim, 0.24f, 0.55f, 0.0f, MatKind::Standard);
        }

        // Side fender aerodynamic brake-cooling louver gills on performance & sports cars
        if (!v.destroyed &&
            (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype ||
             v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner ||
             v.kind == VehicleKind::MuscleCar || v.kind == VehicleKind::ClassicRoadster)) {
            const float ventX = halfL * 0.31f;
            const float ventY = (v.kind == VehicleKind::TrackPrototype || v.kind == VehicleKind::Hypercar) ? 0.44f : 0.48f;
            for (int side = -1; side <= 1; side += 2) {
                const float vz = static_cast<float>(side) * (halfW * 0.964f);
                drawModelStyled(cubeModel_, {ventX, ventY, vz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.14f, 0.024f}, Color{14, 16, 20, 255}, 0.58f, 0.35f, 0.0f, MatKind::Standard);
                for (int g = -1; g <= 1; ++g) {
                    drawModelStyled(cubeModel_, {ventX + static_cast<float>(g) * 0.072f, ventY, vz + static_cast<float>(side) * 0.008f},
                                    {0.0f, 0.0f, 1.0f}, -18.0f, {0.018f, 0.125f, 0.016f},
                                    chromeCol, 0.18f, 0.88f, 0.0f, MatKind::Standard);
                }
            }
        }

        // Rear performance grille vent, license plate & twin/quad chrome exhaust tips (with glowing nitro inner cores!)
        if (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype ||
            v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) {
            drawModelStyled(cubeModel_, {-halfL * 0.995f, 0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.05f, 0.18f, W * 0.76f}, Color{18, 20, 26, 255},
                            0.55f, 0.35f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-halfL - 0.008f, 0.39f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.03f, 0.13f, 0.36f}, Color{238, 242, 248, 255},
                        0.45f, 0.10f, 0.0f, MatKind::Standard);
        const float exY = (v.kind == VehicleKind::TrackPrototype) ? 0.38f
                        : ((v.kind == VehicleKind::Hypercar) ? 0.33f : 0.24f);
        const float exZ = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype)
                              ? (halfW * 0.20f)
                              : (halfW * 0.54f);
        for (int side = -1; side <= 1; side += 2) {
            const float pz = static_cast<float>(side) * exZ;
            drawModelStyled(wheelModel_, {-halfL - 0.015f, exY, pz}, {0.0f, 1.0f, 0.0f}, 90.0f,
                            {0.058f, 0.058f, 0.16f}, chromeCol, 0.18f, 0.92f, 0.0f, MatKind::Standard);
            drawModelStyled(wheelModel_, {-halfL - 0.032f, exY, pz}, {0.0f, 1.0f, 0.0f}, 90.0f,
                            {0.038f, 0.038f, 0.06f},
                            v.nitroActive ? Color{255, 128, 42, 255} : Color{14, 14, 18, 255},
                            v.nitroActive ? 0.12f : 0.75f, v.nitroActive ? 0.0f : 0.20f,
                            v.nitroActive ? 3.2f : 0.0f,
                            v.nitroActive ? MatKind::EmissiveGlow : MatKind::Standard);
        }

        // Realistic Recessed Front Honeycomb Radiator Grille, Chrome/Carbon Surround & Corner Brake-Cooling Intakes!
        const float grilleY = (v.kind == VehicleKind::TrackPrototype || v.kind == VehicleKind::Hypercar) ? 0.28f
                            : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::OffroadPickup ||
                                v.kind == VehicleKind::ArmoredSuv || v.kind == VehicleKind::FireTruck ||
                                v.kind == VehicleKind::Ambulance) ? 0.56f : 0.38f);
        const float grilleH = (v.kind == VehicleKind::SUV || v.kind == VehicleKind::OffroadPickup ||
                               v.kind == VehicleKind::ArmoredSuv || v.kind == VehicleKind::FireTruck) ? 0.26f : 0.16f;
        const float frontCrumpleTilt = v.frontDamage * 14.0f;
        rlPushMatrix();
        rlTranslatef(halfL * 0.990f - v.frontDamage * 0.08f, grilleY, 0.0f);
        rlRotatef(frontCrumpleTilt, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.044f, grilleH + 0.036f, W * 0.36f}, chromeCol, 0.18f, 0.90f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.008f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.042f, grilleH, W * 0.32f}, Color{14, 16, 20, 255}, 0.65f, 0.35f, 0.0f, MatKind::Standard);
        // Horizontal chrome/anodized grille slats + front registration plate
        drawModelStyled(cubeModel_, {0.014f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.036f, 0.022f, W * 0.31f}, chromeCol, 0.16f, 0.92f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.016f, -grilleH * 0.40f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.030f, 0.095f, 0.30f}, Color{242, 245, 250, 255}, 0.40f, 0.10f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {-0.065f, -0.02f, static_cast<float>(side) * (halfW * 0.46f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.045f, grilleH * 0.70f, W * 0.12f}, Color{12, 14, 18, 255}, 0.60f, 0.30f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();

        // Flush Color-Matched / Chrome Door Handles & Dual Windshield Wipers (B-pillars are contoured directly in glassB!)
        const float handleY = 0.24f + H * 0.36f;
        for (int side = -1; side <= 1; side += 2) {
            const float hz = static_cast<float>(side) * (halfW * 0.968f - ((side < 0) ? v.rightDamage : v.leftDamage) * 0.04f);
            drawModelStyled(cubeModel_, {-0.06f, handleY, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.18f, 0.034f, 0.028f}, chromeCol, 0.16f, 0.90f, 0.0f, MatKind::Standard);
            if (v.kind == VehicleKind::Compact || v.kind == VehicleKind::LuxurySedan ||
                v.kind == VehicleKind::PoliceCruiser || v.kind == VehicleKind::Taxi ||
                v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv) {
                drawModelStyled(cubeModel_, {-0.72f, handleY, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 0.034f, 0.028f}, chromeCol, 0.16f, 0.90f, 0.0f, MatKind::Standard);
            }
        }
        // Dual aerodynamic windshield wiper blades at the base of the front windshield cowl
        const float cowlX = (v.kind == VehicleKind::DeliveryVan || v.kind == VehicleKind::Ambulance || v.kind == VehicleKind::FireTruck)
                                ? (halfL * 0.62f)
                                : (halfL * 0.30f);
        const float cowlY = 0.24f + H * 0.50f;
        drawModelStyled(cubeModel_, {cowlX, cowlY, -0.18f}, {0.0f, 1.0f, 0.0f}, 8.0f,
                        {0.035f, 0.025f, 0.44f}, darkTrim, 0.35f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {cowlX, cowlY,  0.22f}, {0.0f, 1.0f, 0.0f}, 8.0f,
                        {0.035f, 0.025f, 0.44f}, darkTrim, 0.35f, 0.55f, 0.0f, MatKind::Standard);

        // 4-Quadrant Crash Crumple Deformation: Popped/tilted front hood & tilted rear bumper when damaged!
        if (!v.destroyed && v.frontDamage > 0.12f &&
            v.kind != VehicleKind::DeliveryVan && v.kind != VehicleKind::Ambulance && v.kind != VehicleKind::FireTruck) {
            const float popDeg = v.frontDamage * 18.0f;
            rlPushMatrix();
            rlTranslatef(halfL * 0.52f, 0.24f + H * 0.46f + v.frontDamage * 0.06f, 0.0f);
            rlRotatef(-10.0f + popDeg, 0.0f, 0.0f, 1.0f);
            rlRotatef((v.leftDamage - v.rightDamage) * 9.0f, 1.0f, 0.0f, 0.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {halfL * 0.62f, 0.045f, W * 0.78f}, bodyCol,
                            std::min(0.85f, paintRough + v.frontDamage * 0.35f), paintMetal, 0.0f, paintMat);
            rlPopMatrix();
        }
        if (!v.destroyed && v.rearDamage > 0.12f) {
            rlPushMatrix();
            rlTranslatef(-halfL * 0.98f + v.rearDamage * 0.05f, 0.28f, 0.0f);
            rlRotatef(-v.rearDamage * 12.0f, 0.0f, 0.0f, 1.0f);
            rlRotatef((v.leftDamage - v.rightDamage) * 11.0f, 1.0f, 0.0f, 0.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.14f, 0.16f, W * 0.92f}, darkTrim, 0.55f, 0.35f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }

        // Contour-hugging curved Dual GT Racing Stripes lofted directly over hood, roof & rear deck!
        if (!v.destroyed && carMdl.hasStripes && (v.stripeStyle > 0 || v.kind == VehicleKind::ClassicRoadster)) {
            const float stripeEmissive = (v.stripeStyle == 3) ? 0.85f : 0.0f;
            const MatKind stripeMat = (v.stripeStyle == 3) ? MatKind::EmissiveGlow : paintMat;
            drawModelStyled(carMdl.stripes, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.0f, 1.0f, 1.0f}, stripeCol, 0.12f, 0.35f, stripeEmissive, stripeMat);
        }
    }

    // Bespoke 3D Signature Architecture for v0.7 Fleet!
    if (v.kind == VehicleKind::ClassicRoadster && !v.destroyed) {
        // Open-top convertible leather bucket seats, center console & twin chrome roll-hoops!
        drawModelStyled(cubeModel_, {-0.22f, 0.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.92f, 0.14f, W * 0.72f}, Color{24, 22, 26, 255}, 0.68f, 0.08f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            const float sz = static_cast<float>(side) * 0.39f;
            drawModelStyled(cubeModel_, {-0.32f, 0.78f, sz}, {0.0f, 0.0f, 1.0f}, 14.0f,
                            {0.14f, 0.44f, 0.36f}, Color{42, 28, 24, 255}, 0.58f, 0.06f, 0.0f, MatKind::Standard);
            if (!shadowPass) {
                // Brushed chrome roll-hoop arch behind each headrest
                drawModelStyled(cylModel_, {-0.44f, 0.76f, sz - 0.13f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.028f, 0.32f, 0.028f}, chromeCol, 0.14f, 0.94f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {-0.44f, 0.76f, sz + 0.13f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.028f, 0.32f, 0.028f}, chromeCol, 0.14f, 0.94f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-0.44f, 1.08f, sz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.052f, 0.052f, 0.30f}, chromeCol, 0.14f, 0.94f, 0.0f, MatKind::Standard);
            }
        }
    } else if (v.kind == VehicleKind::OffroadPickup && !v.destroyed) {
        // Lifted 4x4 dark rugged pickup bedliner + Baja tubular chase rollbar + 4 roof LED floodlights!
        drawModelStyled(cubeModel_, {-halfL * 0.52f, 0.96f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {halfL * 0.78f, 0.06f, W * 0.74f}, Color{22, 24, 28, 255}, 0.78f, 0.12f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            const float bz = static_cast<float>(side) * (halfW * 0.72f);
            drawModelStyled(cylModel_, {-0.34f, 1.12f, bz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.042f, 0.70f, 0.042f}, darkTrim, 0.36f, 0.62f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.72f, 1.44f, bz}, {0.0f, 0.0f, 1.0f}, 38.0f,
                            {0.055f, 0.88f, 0.055f}, darkTrim, 0.36f, 0.62f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-0.34f, 1.82f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.08f, 0.07f, W * 0.76f}, darkTrim, 0.36f, 0.62f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            for (int i = 0; i < 4; ++i) {
                const float lz = (-1.5f + static_cast<float>(i)) * 0.30f;
                drawModelStyled(cubeModel_, {0.32f, 1.84f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.09f, 0.11f, 0.18f}, darkTrim, 0.35f, 0.6f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.368f, 1.84f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.02f, 0.085f, 0.15f}, Color{240, 248, 255, 255},
                                0.1f, 0.0f, nightMode ? 2.8f : 1.3f, MatKind::EmissiveGlow);
            }
        }
    } else if (v.kind == VehicleKind::JdmTuner && !v.destroyed && !shadowPass) {
        // Exposed silver Front-Mount Intercooler (FMIC) core + roof rally intake scoop!
        drawModelStyled(cubeModel_, {halfL * 0.992f, 0.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.048f, 0.14f, 0.58f}, Color{206, 214, 226, 255}, 0.22f, 0.88f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.06f, 1.30f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.34f, 0.075f, 0.28f}, darkTrim, 0.32f, 0.55f, 0.0f, MatKind::Standard);
    } else if (v.kind == VehicleKind::TrackPrototype && !v.destroyed) {
        // Le Mans LMP central dorsal aerodynamic shark-fin stabilizer + accent splitter edge!
        drawModelStyled(cubeModel_, {-halfL * 0.48f, 0.96f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {halfL * 0.72f, 0.26f, 0.032f}, darkTrim, 0.28f, 0.62f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {-halfL * 0.48f, 1.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {halfL * 0.72f, 0.030f, 0.038f}, stripeCol, 0.15f, 0.4f, 0.6f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {halfL * 0.99f, 0.16f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.08f, 0.026f, W * 0.88f}, stripeCol, 0.15f, 0.4f, 0.6f, MatKind::EmissiveGlow);
        }
    } else if (v.kind == VehicleKind::Ambulance && !v.destroyed) {
        // Paramedic EMS crimson flank stripes, Red Cross emblems on sides/roof, and Red/White LED Lightbar!
        const Color emsRed{226, 36, 42, 255};
        for (int side = -1; side <= 1; side += 2) {
            const float sz = static_cast<float>(side);
            drawModelStyled(cubeModel_, {-0.22f, 0.92f, sz * (halfW * 0.965f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.78f, 0.22f, 0.035f}, emsRed, 0.14f, 0.25f, 0.0f, MatKind::CarClearcoat);
            if (!shadowPass) {
                // Red Paramedic Cross (+) on rear medical box flanks
                drawModelStyled(cubeModel_, {-0.68f, 1.48f, sz * (halfW * 0.955f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.42f, 0.13f, 0.035f}, emsRed, 0.16f, 0.2f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-0.68f, 1.48f, sz * (halfW * 0.955f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.13f, 0.42f, 0.035f}, emsRed, 0.16f, 0.2f, 0.0f, MatKind::Standard);
            }
        }
        if (!shadowPass) {
            // Roof Red Cross + Front Cab Red/White Emergency Strobe Bar
            drawModelStyled(cubeModel_, {-0.75f, 2.03f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.68f, 0.025f, 0.20f}, emsRed, 0.18f, 0.2f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.75f, 2.03f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.20f, 0.025f, 0.68f}, emsRed, 0.18f, 0.2f, 0.0f, MatKind::Standard);

            const bool redTurn = (v.sirenPhase < 0.5f);
            const Color redPod = (v.sirenActive && redTurn) ? Color{255, 38, 46, 255} : Color{168, 24, 32, 255};
            const Color whtPod = (v.sirenActive && !redTurn) ? Color{250, 252, 255, 255} : Color{210, 218, 228, 255};
            drawModelStyled(cubeModel_, {halfL * 0.34f, 2.07f, -W * 0.22f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, 0.10f, W * 0.32f}, redPod, 0.1f, 0.0f,
                            v.sirenActive ? (redTurn ? 3.8f : 0.6f) : 0.15f,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
            drawModelStyled(cubeModel_, {halfL * 0.34f, 2.07f,  W * 0.22f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, 0.10f, W * 0.32f}, whtPod, 0.1f, 0.0f,
                            v.sirenActive ? (!redTurn ? 3.6f : 0.5f) : 0.15f,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
        }
    } else if (v.kind == VehicleKind::FireTruck && !v.destroyed) {
        // SBFD Fire Engine white stripe, chrome bumper, equipment shutters, aluminum roof ladder & Red/Amber Lightbar!
        drawModelStyled(cubeModel_, {halfL + 0.06f, 0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.14f, 0.24f, W * 0.96f}, chromeCol, 0.14f, 0.94f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            const float sz = static_cast<float>(side);
            drawModelStyled(cubeModel_, {0.0f, 0.96f, sz * (halfW * 0.968f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {L * 0.88f, 0.18f, 0.035f}, Color{244, 246, 250, 255}, 0.14f, 0.25f, 0.0f, MatKind::CarClearcoat);
            if (!shadowPass) {
                // Brushed aluminum roll-up equipment bay doors
                drawModelStyled(cubeModel_, {-0.45f, 1.38f, sz * (halfW * 0.955f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.54f, 0.032f}, Color{186, 194, 206, 255}, 0.28f, 0.82f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-1.68f, 1.38f, sz * (halfW * 0.955f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.54f, 0.032f}, Color{186, 194, 206, 255}, 0.28f, 0.82f, 0.0f, MatKind::Standard);
            }
        }
        // Rooftop silver aluminum extension ladder rack (2 side rails + 7 cross-rungs)
        drawModelStyled(cubeModel_, {-0.55f, 2.16f, -0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {L * 0.66f, 0.07f, 0.06f}, chromeCol, 0.18f, 0.90f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-0.55f, 2.16f,  0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {L * 0.66f, 0.07f, 0.06f}, chromeCol, 0.18f, 0.90f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            for (int r = -3; r <= 3; ++r) {
                drawModelStyled(cubeModel_, {-0.55f + static_cast<float>(r) * 0.62f, 2.16f, 0.0f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.045f, 0.045f, 0.64f},
                                chromeCol, 0.20f, 0.88f, 0.0f, MatKind::Standard);
            }
            const bool redTurn = (v.sirenPhase < 0.5f);
            const Color redPod = (v.sirenActive && redTurn) ? Color{255, 36, 44, 255} : Color{168, 22, 30, 255};
            const Color ambPod = (v.sirenActive && !redTurn) ? Color{255, 196, 48, 255} : Color{182, 124, 24, 255};
            drawModelStyled(cubeModel_, {halfL * 0.52f, 2.13f, -W * 0.24f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.24f, 0.10f, W * 0.32f}, redPod, 0.1f, 0.0f,
                            v.sirenActive ? (redTurn ? 3.8f : 0.6f) : 0.15f,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
            drawModelStyled(cubeModel_, {halfL * 0.52f, 2.13f,  W * 0.24f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.24f, 0.10f, W * 0.32f}, ambPod, 0.1f, 0.0f,
                            v.sirenActive ? (!redTurn ? 3.8f : 0.6f) : 0.15f,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
        }
    }

    // 3) GT Aero Wing / Time-Attack Swan-Neck Spoiler, SBPD Police Livery & Roof Red/Blue LED Lightbar!
    const int effSpoiler = v.destroyed ? 0 : ((v.spoilerStyle > 0) ? v.spoilerStyle : (v.customSpoiler ? 1 : 0));
    if (effSpoiler > 0) {
        const float deckY = (v.kind == VehicleKind::TrackPrototype) ? 0.78f
                          : ((v.kind == VehicleKind::Hypercar) ? 0.80f
                          : ((v.kind == VehicleKind::ClassicRoadster) ? 0.84f
                          : ((v.kind == VehicleKind::SportsCoupe) ? 0.86f
                          : ((v.kind == VehicleKind::JdmTuner) ? 0.88f : (0.22f + H * 0.62f)))));
        const float spX = -halfL * 0.86f;
        if (effSpoiler == 2) {
            // High-downforce Time-Attack Swan-Neck Wing + Endplates!
            for (int side = -1; side <= 1; side += 2) {
                const float pz = static_cast<float>(side) * (halfW * 0.36f);
                drawModelStyled(cubeModel_, {spX + 0.04f, deckY + 0.19f, pz}, {0.0f, 0.0f, 1.0f}, -14.0f,
                                {0.14f, 0.38f, 0.038f}, darkTrim, 0.28f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {spX - 0.03f, deckY + 0.40f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.22f, 0.055f, 0.042f}, darkTrim, 0.28f, 0.65f, 0.0f, MatKind::Standard);
            }
            drawModelStyled(cubeModel_, {spX - 0.06f, deckY + 0.38f, 0.0f}, {0.0f, 0.0f, 1.0f}, 7.5f,
                            {0.36f, 0.044f, W * 1.06f}, darkTrim, 0.22f, 0.58f, 0.0f, MatKind::Standard);
            if (!shadowPass) {
                drawModelStyled(cubeModel_, {spX - 0.19f, deckY + 0.415f, 0.0f}, {0.0f, 0.0f, 1.0f}, 12.0f,
                                {0.09f, 0.024f, W * 1.05f}, stripeCol, 0.14f, 0.45f, 0.0f, paintMat);
                for (int side = -1; side <= 1; side += 2) {
                    drawModelStyled(cubeModel_, {spX - 0.06f, deckY + 0.37f, static_cast<float>(side) * (W * 0.535f)},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.40f, 0.24f, 0.026f},
                                    bodyCol, paintRough, paintMetal, 0.0f, paintMat);
                }
            }
        } else {
            drawModelStyled(cubeModel_, {spX, deckY + 0.12f, -halfW * 0.56f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.16f, 0.22f, 0.045f}, darkTrim, 0.32f, 0.55f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {spX, deckY + 0.12f,  halfW * 0.56f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.16f, 0.22f, 0.045f}, darkTrim, 0.32f, 0.55f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {spX - 0.03f, deckY + 0.23f, 0.0f}, {0.0f, 0.0f, 1.0f}, 5.0f,
                            {0.30f, 0.042f, W * 0.94f}, bodyCol, paintRough, paintMetal, 0.0f, paintMat);
        }
    }
    if (v.kind == VehicleKind::Taxi && !v.destroyed && !shadowPass) {
        drawModelStyled(cubeModel_, {-0.12f, 1.46f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.38f, 0.16f, 0.64f}, Color{255, 232, 118, 255},
                        0.2f, 0.0f, 1.8f, MatKind::EmissiveGlow);
    }

    // Authentic SBPD Gloss White Doors, Gold Shield Crest, Roof Panel & Dual A-Pillar Spotlights
    if (v.kind == VehicleKind::PoliceCruiser && !v.destroyed) {
        const Color liveryWhite = toRayColor(v.stripeColor);
        for (int side = -1; side <= 1; side += 2) {
            const float sz = static_cast<float>(side);
            drawModelStyled(cubeModel_, {-0.08f, 0.56f, sz * (halfW * 0.962f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {halfL * 0.78f, 0.48f, 0.042f}, liveryWhite, 0.10f, 0.35f, 0.0f, MatKind::CarClearcoat);
            if (!shadowPass) {
                // Gold SBPD Star Shield Emblem on front doors
                drawModelStyled(cubeModel_, {0.22f, 0.62f, sz * (halfW * 0.988f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.17f, 0.18f, 0.025f}, Color{242, 198, 64, 255}, 0.22f, 0.85f, 0.0f, MatKind::Standard);
                // Dual A-pillar interceptor spotlights
                drawModelStyled(sphereModel_, {halfL * 0.35f, 0.97f, sz * (halfW * 0.84f)}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.095f, 0.085f, 0.085f},
                                v.sirenActive ? Color{245, 250, 255, 255} : chromeCol,
                                0.12f, 0.80f, v.sirenActive ? 2.4f : 0.0f,
                                v.sirenActive ? MatKind::EmissiveGlow : MatKind::Standard);
            }
        }
        // High-visibility white aerial unit roof cap
        drawModelStyled(cubeModel_, {-0.14f, 1.385f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {halfL * 0.44f, 0.032f, W * 0.60f}, liveryWhite, 0.12f, 0.30f, 0.0f, MatKind::CarClearcoat);
    }

    // Roof Red & Blue LED Police Lightbar + Grille Wig-Wag Strobes (PoliceCruiser & SWAT ArmoredSuv)
    if ((v.kind == VehicleKind::PoliceCruiser || v.isPolice) && !v.destroyed) {
        const float barY = (v.kind == VehicleKind::ArmoredSuv) ? 1.84f : 1.455f;
        const float barX = -0.10f;
        drawModelStyled(cubeModel_, {barX, barY - 0.035f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.24f, 0.05f, W * 0.64f}, darkTrim, 0.35f, 0.65f, 0.0f, MatKind::Standard);

        if (!shadowPass) {
            const bool redTurn = (v.sirenPhase < 0.5f);
            const bool whiteFlash = v.sirenActive && (std::fmod(v.sirenPhase * 2.0f, 1.0f) < 0.36f);

            const Color redPodCol  = (v.sirenActive && redTurn)  ? Color{255, 36, 44, 255}  : Color{156, 22, 32, 255};
            const Color bluePodCol = (v.sirenActive && !redTurn) ? Color{38, 132, 255, 255} : Color{22, 64, 162, 255};
            const float redGlow    = v.sirenActive ? (redTurn ? 3.8f : 0.55f) : 0.12f;
            const float blueGlow   = v.sirenActive ? (!redTurn ? 3.8f : 0.55f) : 0.12f;

            drawModelStyled(cubeModel_, {barX, barY + 0.015f, -W * 0.18f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, 0.088f, W * 0.28f}, redPodCol, 0.10f, 0.0f, redGlow,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
            drawModelStyled(cubeModel_, {barX, barY + 0.015f,  W * 0.18f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, 0.088f, W * 0.28f}, bluePodCol, 0.10f, 0.0f, blueGlow,
                            v.sirenActive ? MatKind::EmissiveGlow : MatKind::CarGlass);
            drawModelStyled(cubeModel_, {barX, barY + 0.018f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.23f, 0.082f, 0.15f},
                            whiteFlash ? Color{250, 252, 255, 255} : chromeCol,
                            0.12f, 0.65f, whiteFlash ? 3.4f : 0.0f,
                            whiteFlash ? MatKind::EmissiveGlow : MatKind::Standard);

            // Front push-bumper red/blue Wig-Wag grille strobes when siren is active!
            if (v.sirenActive) {
                drawModelStyled(cubeModel_, {halfL + 0.11f, 0.58f, -0.20f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.04f, 0.065f, 0.16f}, redPodCol, 0.1f, 0.0f, redTurn ? 3.5f : 0.4f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {halfL + 0.11f, 0.58f,  0.20f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.04f, 0.065f, 0.16f}, bluePodCol, 0.1f, 0.0f, !redTurn ? 3.5f : 0.4f, MatKind::EmissiveGlow);
            }
        }
    }

    // 4) Recessed Round 24-Segment Bevelled Tires + 5-Twin-Spoke Concave Alloy Rims + Brake Calipers!
    float axleFrac = 0.63f;
    float wheelR   = 0.35f;
    float tireW    = 0.26f;
    switch (v.kind) {
        case VehicleKind::TrackPrototype:  axleFrac = 0.62f; wheelR = 0.35f; tireW = 0.32f; break;
        case VehicleKind::Hypercar:        axleFrac = 0.62f; wheelR = 0.35f; tireW = 0.30f; break;
        case VehicleKind::ClassicRoadster: axleFrac = 0.63f; wheelR = 0.34f; tireW = 0.27f; break;
        case VehicleKind::SportsCoupe:     axleFrac = 0.63f; wheelR = 0.35f; tireW = 0.28f; break;
        case VehicleKind::JdmTuner:        axleFrac = 0.63f; wheelR = 0.35f; tireW = 0.29f; break;
        case VehicleKind::MuscleCar:       axleFrac = 0.64f; wheelR = 0.36f; tireW = 0.28f; break;
        case VehicleKind::LuxurySedan:     axleFrac = 0.64f; wheelR = 0.36f; tireW = 0.26f; break;
        case VehicleKind::PoliceCruiser:   axleFrac = 0.64f; wheelR = 0.35f; tireW = 0.26f; break;
        case VehicleKind::Taxi:            axleFrac = 0.62f; wheelR = 0.34f; tireW = 0.24f; break;
        case VehicleKind::Compact:         axleFrac = 0.64f; wheelR = 0.32f; tireW = 0.23f; break;
        case VehicleKind::SUV:             axleFrac = 0.63f; wheelR = 0.40f; tireW = 0.29f; break;
        case VehicleKind::OffroadPickup:   axleFrac = 0.64f; wheelR = 0.45f; tireW = 0.34f; break;
        case VehicleKind::ArmoredSuv:      axleFrac = 0.63f; wheelR = 0.43f; tireW = 0.32f; break;
        case VehicleKind::DeliveryVan:     axleFrac = 0.62f; wheelR = 0.38f; tireW = 0.26f; break;
        case VehicleKind::Ambulance:       axleFrac = 0.63f; wheelR = 0.39f; tireW = 0.27f; break;
        case VehicleKind::FireTruck:       axleFrac = 0.64f; wheelR = 0.43f; tireW = 0.32f; break;
        default: break;
    }

    const Color rimCol = (v.rimStyle == 1) ? Color{236, 198, 92, 255}  // Champagne Gold
                       : (v.rimStyle == 2) ? Color{34, 38, 46, 255}    // Gloss Obsidian Black
                       : (v.rimStyle == 3) ? Color{44, 226, 255, 255}  // Anodized Cyber Cyan
                       : (v.rimStyle == 4) ? Color{236, 42, 56, 255}   // Anodized Crimson Track
                                           : Color{218, 226, 236, 255}; // Brushed Silver Alloy
    const float rimEmissive = (v.rimStyle == 3) ? 0.55f : 0.0f;
    const MatKind rimMat = (v.rimStyle == 3) ? MatKind::EmissiveGlow : MatKind::Standard;
    const float axleX = halfL * axleFrac;
    const float axleZ = halfW * ((v.stanceStyle == 2) ? 0.92f : 0.89f);

    for (int side = -1; side <= 1; side += 2) {
        for (int front = -1; front <= 1; front += 2) {
            // Wheel index: 0 = FL, 1 = FR, 2 = RL, 3 = RR
            const int wheelIdx = (front > 0 ? 0 : 2) + (side > 0 ? 1 : 0);
            const float suspDeflect = v.destroyed ? 0.0f : (v.wheelCompression[wheelIdx] * 0.85f);
            rlPushMatrix();
            rlTranslatef(static_cast<float>(front) * axleX,
                         wheelR - stanceOffsetY + suspDeflect,
                         static_cast<float>(side) * axleZ);
            if (front > 0) {
                rlRotatef(-v.steerVisual * 28.0f, 0.0f, 1.0f, 0.0f);
            }
            if (v.stanceStyle == 2 && !v.destroyed) {
                // Aggressive Slammed Stance: -6.5 deg inward negative camber!
                rlRotatef(static_cast<float>(-side) * 6.5f, 1.0f, 0.0f, 0.0f);
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
                                (v.rimStyle == 2) ? 0.16f : 0.20f, 0.88f, rimEmissive, rimMat);
                if (v.kind == VehicleKind::PoliceCruiser) {
                    // Classic police interceptor chrome center dog-dish cap
                    drawModelStyled(wheelModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {wheelR * 0.26f, wheelR * 0.26f, tireW * 1.06f}, chromeCol,
                                    0.14f, 0.95f, 0.0f, MatKind::Standard);
                }
            }
            rlPopMatrix();
        }
    }

    // 5) Sleek Contoured LED DRLs / Custom Xenon Headlights, Blade Taillights & Nitrous Afterburners
    if (!shadowPass && !v.destroyed) {
        const Color headCol = toRayColor(getVehicleHeadlightColorRGBA(v.headlightColorIdx, nightMode));
        const float headGlow = (v.headlightColorIdx > 0) ? (nightMode ? 3.2f : 1.9f) : (nightMode ? 2.7f : 1.15f);
        const float headY = (v.kind == VehicleKind::TrackPrototype) ? 0.38f
                          : ((v.kind == VehicleKind::Hypercar) ? 0.43f
                          : ((v.kind == VehicleKind::ClassicRoadster || v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) ? 0.48f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv ||
                              v.kind == VehicleKind::OffroadPickup || v.kind == VehicleKind::Ambulance ||
                              v.kind == VehicleKind::FireTruck) ? 0.78f : 0.58f)));
        const float headX = halfL * 0.95f;
        const float headZ = halfW * 0.65f;

        for (int side = -1; side <= 1; side += 2) {
            rlPushMatrix();
            rlTranslatef(headX, headY, static_cast<float>(side) * headZ);
            rlRotatef(static_cast<float>(-side) * 18.0f, 0.0f, 1.0f, 0.0f);
            const float hHeight = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype) ? 0.068f : 0.105f;
            // Smoked dark chrome projector bezel housing + glowing projector lens + top LED DRL eyebrow bar!
            drawModelStyled(cubeModel_, {-0.012f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.11f, hHeight + 0.028f, 0.35f}, Color{16, 18, 24, 255}, 0.18f, 0.75f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, -0.008f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, hHeight * 0.78f, 0.28f},
                            headCol, 0.1f, 0.0f, headGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {0.01f, hHeight * 0.50f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.09f, 0.022f, 0.33f},
                            headCol, 0.1f, 0.0f, headGlow * 1.25f, MatKind::EmissiveGlow);
            rlPopMatrix();
        }


        if (nightMode && (v.isPlayerDriving || v.hasNpcDriver || v.headlightColorIdx > 0)) {
            Color beamCol = headCol;
            beamCol.a = static_cast<unsigned char>((v.headlightColorIdx > 0) ? 58 : 42);
            DrawCube({halfL + 4.8f, 0.028f - stanceOffsetY, 0.0f}, 8.8f, 0.015f, W * 1.85f, beamCol);
        }

        const Color tailCol = v.brakingVisual ? Color{255, 38, 36, 255} : Color{228, 28, 38, 255};
        const float tailGlow = v.brakingVisual ? 2.9f : 1.75f;
        const float tailY = (v.kind == VehicleKind::TrackPrototype) ? 0.54f
                          : ((v.kind == VehicleKind::Hypercar) ? 0.58f
                          : ((v.kind == VehicleKind::ClassicRoadster || v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) ? 0.64f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv ||
                              v.kind == VehicleKind::OffroadPickup || v.kind == VehicleKind::Ambulance ||
                              v.kind == VehicleKind::FireTruck) ? 0.86f : 0.65f)));

        if (v.kind == VehicleKind::TrackPrototype || v.kind == VehicleKind::Hypercar ||
            v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner ||
            v.kind == VehicleKind::LuxurySedan || v.kind == VehicleKind::PoliceCruiser) {
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

        // White LED reverse backup lights when driving in reverse!
        const float fwdSpeed = v.velocity.dot(forwardFromHeading(v.heading));
        if (fwdSpeed < -0.6f) {
            for (int side = -1; side <= 1; side += 2) {
                drawModelStyled(cubeModel_, {-halfL - 0.011f, tailY - 0.056f, static_cast<float>(side) * (halfW * 0.45f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.042f, 0.042f, 0.16f},
                                Color{242, 248, 255, 255}, 0.08f, 0.0f, 2.7f, MatKind::EmissiveGlow);
            }
        }

        // High-mount Center 3rd Brake Light (CHMSL) along rear roofline/glass when braking!
        if (v.brakingVisual && v.kind != VehicleKind::ClassicRoadster && v.kind != VehicleKind::FireTruck) {
            float chmslX = -halfL * 0.34f;
            float chmslY = 1.35f;
            switch (v.kind) {
                case VehicleKind::TrackPrototype: chmslX = -halfL * 0.22f; chmslY = 1.055f; break;
                case VehicleKind::Hypercar:       chmslX = -halfL * 0.24f; chmslY = 1.105f; break;
                case VehicleKind::SportsCoupe:    chmslX = -halfL * 0.29f; chmslY = 1.215f; break;
                case VehicleKind::JdmTuner:       chmslX = -halfL * 0.32f; chmslY = 1.255f; break;
                case VehicleKind::MuscleCar:      chmslX = -halfL * 0.34f; chmslY = 1.285f; break;
                case VehicleKind::LuxurySedan:
                case VehicleKind::PoliceCruiser:
                case VehicleKind::Taxi:           chmslX = -halfL * 0.37f; chmslY = 1.355f; break;
                case VehicleKind::Compact:        chmslX = -halfL * 0.65f; chmslY = 1.355f; break;
                case VehicleKind::SUV:            chmslX = -halfL * 0.78f; chmslY = 1.635f; break;
                case VehicleKind::OffroadPickup:  chmslX = -halfL * 0.06f; chmslY = 1.755f; break;
                case VehicleKind::ArmoredSuv:     chmslX = -halfL * 0.80f; chmslY = 1.755f; break;
                case VehicleKind::Ambulance:      chmslX = -halfL * 0.94f; chmslY = 1.985f; break;
                case VehicleKind::DeliveryVan:
                default:                          chmslX = -halfL * 0.92f; chmslY = 1.945f; break;
            }
            drawModelStyled(cubeModel_, {chmslX, chmslY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.055f, 0.028f, 0.34f}, Color{255, 34, 34, 255},
                            0.08f, 0.0f, 3.2f, MatKind::EmissiveGlow);
        }

        if (v.nitroActive) {
            const float exZ = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype)
                                  ? (halfW * 0.18f)
                                  : (halfW * 0.54f);
            const float exY = (v.kind == VehicleKind::TrackPrototype) ? 0.38f
                            : ((v.kind == VehicleKind::Hypercar) ? 0.34f : 0.24f);
            const float flick = 0.88f + 0.16f * std::sin(static_cast<float>(GetTime()) * 48.0f);
            for (int side = -1; side <= 1; side += 2) {
                const float pz = static_cast<float>(side) * exZ;
                // Outer blue-cyan afterburner plume shooting straight rearward (-X)
                drawModelStyled(coneModel_, {-halfL - 0.06f, exY, pz}, {0.0f, 0.0f, 1.0f}, 90.0f,
                                {0.11f, 0.95f * flick, 0.11f}, Color{68, 214, 255, 255},
                                0.1f, 0.0f, 3.3f, MatKind::EmissiveGlow);
                // Inner white-hot core cone
                drawModelStyled(coneModel_, {-halfL - 0.04f, exY, pz}, {0.0f, 0.0f, 1.0f}, 90.0f,
                                {0.055f, 0.52f * flick, 0.055f}, Color{235, 248, 255, 255},
                                0.08f, 0.0f, 4.0f, MatKind::EmissiveGlow);
            }
        }
    }

    rlPopMatrix();
}

void Renderer3D::drawCharacter3D(const Vec2& pos, float groundY, float yaw, float animPhase,
                                 ColorRGBA top, ColorRGBA bottom, ColorRGBA skin,
                                 bool alive, bool armed, WeaponId weapon, float muzzleFlash,
                                 float aimPitch, bool hasSunglasses, OutfitId styleOutfit, bool shadowPass,
                                 float shadowGroundY, bool jumping,
                                 bool panicking, bool isPolice, bool isSwat,
                                 const CharacterCustomState* customChar,
                                 const WeaponSlotState* customWep) {
    rlPushMatrix();
    rlTranslatef(pos.x, groundY, pos.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);

    if (!alive) {
        rlTranslatef(0.0f, 0.16f, 0.0f);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    }

    const float strideAmp = panicking ? 38.0f : 28.0f;
    const float legSwing  = alive ? std::sin(animPhase) * strideAmp : 0.0f;
    const Color topC  = toRayColor(top);
    const Color botC  = toRayColor(bottom);
    const Color skinC = toRayColor(skin);
    const Color accC  = toRayColor(getOutfitSpec(styleOutfit).accent);
    const Color shoeC = (styleOutfit == OutfitId::ViceWhiteSuit) ? Color{230, 232, 238, 255} : Color{26, 28, 34, 255};

    // Grounded contact shadow anchored on street or car roof directly beneath the character!
    if (!shadowPass && alive) {
        const float surfY = (shadowGroundY > -900.0f) ? shadowGroundY : groundY;
        const float airH = std::max(0.0f, groundY - surfY);
        const float shScale = clampf(1.0f - airH * 0.22f, 0.42f, 1.0f);
        const auto shAlpha = static_cast<unsigned char>(clampf(115.0f * shScale, 36.0f, 115.0f));
        DrawCylinder({0.0f, (surfY - groundY) + 0.012f, 0.0f},
                     0.36f * shScale, 0.36f * shScale, 0.008f, 12, Color{10, 12, 16, shAlpha});
    }

    // Articulated Left & Right Legs (Thigh, Knee Joint, Shin, Multi-Part Sneakers/Dress Shoes)
    const bool dressShoe = (styleOutfit == OutfitId::MidnightTuxedo || styleOutfit == OutfitId::GoldBossCoat || isPolice);
    const Color soleC = dressShoe ? Color{18, 20, 24, 255} : Color{236, 238, 244, 255};
    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        const float hipLift = (jumping && alive) ? ((side < 0) ? 0.11f : 0.06f) : 0.0f;
        rlTranslatef(0.0f, 0.76f + hipLift, static_cast<float>(side) * 0.118f);
        const float legAngle = (jumping && alive)
                                   ? ((side < 0) ? 38.0f : -20.0f)
                                   : (static_cast<float>(side) * legSwing);
        const float kneeBend = (jumping && alive)
                                   ? ((side < 0) ? -52.0f : -34.0f)
                                   : (alive ? -clampf(std::abs(legSwing) * 0.58f, 0.0f, 22.0f) : 0.0f);
        rlRotatef(legAngle, 0.0f, 0.0f, 1.0f);

        // Rounded hip cap & sculpted tapered thigh
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.094f, 0.092f, 0.094f}, botC, 0.76f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(cylModel_, {0.0f, -0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.092f, 0.34f, 0.092f}, botC, 0.76f, 0.0f, 0.0f, MatKind::Standard);

        // Articulated Knee Joint + Lower Shin + Detailed Multi-Part Shoe
        rlPushMatrix();
        rlTranslatef(0.0f, -0.34f, 0.0f);
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.084f, 0.082f, 0.084f}, botC, 0.76f, 0.0f, 0.0f, MatKind::Standard);
        rlRotatef(kneeBend, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, -0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.078f, 0.34f, 0.078f}, botC, 0.78f, 0.0f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            // Ankle cuff, contoured sneaker/shoe upper, rounded toe cap & contrasting midsole!
            drawModelStyled(cylModel_, {0.0f, -0.345f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.066f, 0.05f, 0.066f}, Color{226, 228, 234, 255}, 0.72f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.036f, -0.358f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.21f, 0.074f, 0.155f}, shoeC, dressShoe ? 0.24f : 0.54f, dressShoe ? 0.22f : 0.08f, 0.0f, MatKind::Standard);
            drawModelStyled(sphereModel_, {0.132f, -0.366f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.064f, 0.042f, 0.074f}, shoeC, dressShoe ? 0.24f : 0.54f, dressShoe ? 0.22f : 0.08f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.048f, -0.398f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.26f, 0.026f, 0.165f}, soleC, 0.62f, 0.05f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();
        rlPopMatrix();
    }

    // Slight forward torso lean when sprinting away in panic!
    rlPushMatrix();
    if (panicking && alive) {
        rlTranslatef(0.04f, 0.0f, 0.0f);
        rlRotatef(-9.5f, 0.0f, 0.0f, 1.0f);
    }

    // Sculpted Pelvis/Belt + Tapered Chest/Jacket + Rounded Deltoid Shoulders & Collar!
    const float topRough = (styleOutfit == OutfitId::RacerLeather) ? 0.28f : 0.62f;
    const float topMetal = (styleOutfit == OutfitId::GoldBossCoat) ? 0.35f : 0.06f;
    drawModelStyled(cubeModel_, {0.0f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.246f, 0.072f, 0.396f}, Color{24, 26, 32, 255}, 0.46f, 0.30f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 0.94f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.242f, 0.28f, 0.394f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 1.21f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.264f, 0.34f, 0.428f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
    // Organic rounded shoulder deltoids + collar ring
    for (int s = -1; s <= 1; s += 2) {
        drawModelStyled(sphereModel_, {0.0f, 1.305f, static_cast<float>(s) * 0.205f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.098f, 0.096f, 0.090f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
    }
    drawModelStyled(cylModel_, {0.0f, 1.375f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.104f, 0.044f, 0.104f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);

    if (!shadowPass) {
        // Metallic silver/brass belt buckle on all characters
        drawModelStyled(cubeModel_, {0.130f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.020f, 0.052f, 0.068f},
                        isPolice ? Color{236, 196, 68, 255} : Color{196, 204, 216, 255},
                        0.22f, 0.88f, 0.0f, MatKind::Standard);

        if (isPolice && isSwat) {
            // Heavy SWAT Kevlar tactical plate-carrier vest + 3 rifle mag pouches + shoulder pauldrons + radio whip!
            drawModelStyled(cubeModel_, {0.0f, 1.11f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.335f, 0.46f, 0.47f}, Color{26, 32, 40, 255}, 0.40f, 0.42f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.172f, 1.19f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.075f, 0.24f}, Color{236, 240, 248, 255}, 0.35f, 0.1f, 0.0f, MatKind::Standard);
            for (int mp = -1; mp <= 1; ++mp) {
                drawModelStyled(cubeModel_, {0.180f, 0.99f, static_cast<float>(mp) * 0.115f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.048f, 0.135f, 0.082f}, Color{20, 24, 30, 255}, 0.50f, 0.25f, 0.0f, MatKind::Standard);
            }
            // Tactical back radio whip antenna
            drawModelStyled(cylModel_, {-0.175f, 1.24f, -0.13f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.008f, 0.42f, 0.008f}, Color{28, 32, 38, 255}, 0.32f, 0.80f, 0.0f, MatKind::Standard);
        } else if (isPolice) {
            // SBPD Officer uniform tie, gold shield chest badge, shoulder radio mic & duty-belt pouches!
            drawModelStyled(cubeModel_, {0.138f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.34f, 0.052f}, Color{16, 20, 30, 255}, 0.45f, 0.15f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.142f, 1.23f, -0.095f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.084f, 0.068f}, Color{246, 204, 62, 255}, 0.20f, 0.90f, 0.0f, MatKind::Standard);
            // Left-shoulder speaker mic + hip duty-belt radio & holster pouches
            drawModelStyled(cubeModel_, {0.132f, 1.31f, -0.145f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.045f, 0.062f, 0.045f}, Color{22, 24, 30, 255}, 0.40f, 0.35f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.02f, 0.76f, -0.225f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.095f, 0.115f, 0.060f}, Color{22, 24, 30, 255}, 0.45f, 0.28f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.04f, 0.74f,  0.225f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.105f, 0.145f, 0.062f}, Color{20, 22, 26, 255}, 0.42f, 0.32f, 0.0f, MatKind::Standard);
        } else if (styleOutfit == OutfitId::ViceWhiteSuit || styleOutfit == OutfitId::MidnightTuxedo) {
            // Crisp dress shirt V-front + silk necktie + metallic tie bar + breast pocket square!
            drawModelStyled(cubeModel_, {0.132f, 1.16f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.028f, 0.40f, 0.165f}, WHITE, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.145f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.34f, 0.052f}, accC, 0.35f, 0.15f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.152f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.014f, 0.062f}, Color{238, 206, 86, 255}, 0.18f, 0.92f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.136f, 1.22f, -0.115f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.028f, 0.068f}, accC, 0.40f, 0.10f, 0.0f, MatKind::Standard);
        } else if (styleOutfit == OutfitId::TacOpsArmor) {
            // Molded 3D Kevlar plate-carrier vest + 3 front mag pouches!
            drawModelStyled(cubeModel_, {0.0f, 1.11f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.325f, 0.46f, 0.46f}, accC, 0.42f, 0.35f, 0.0f, MatKind::Standard);
            for (int mp = -1; mp <= 1; ++mp) {
                drawModelStyled(cubeModel_, {0.174f, 0.99f, static_cast<float>(mp) * 0.11f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.044f, 0.13f, 0.078f}, Color{28, 32, 36, 255}, 0.52f, 0.20f, 0.0f, MatKind::Standard);
            }
        } else if (styleOutfit == OutfitId::GoldBossCoat) {
            // Tailored boss overcoat lower panels cleanly below waist belt (zero Z-fighting!) + gold lapel trim + medallion!
            drawModelStyled(cubeModel_, {-0.012f, 0.585f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.232f, 0.295f, 0.402f}, topC, 0.48f, 0.25f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.136f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.028f, 0.46f, 0.14f}, accC, 0.25f, 0.80f, 0.0f, MatKind::Standard);
            drawModelStyled(sphereModel_, {0.148f, 1.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.036f, 0.036f}, Color{250, 206, 58, 255}, 0.16f, 0.96f, 0.0f, MatKind::Standard);
        } else {
            // Layered inner shirt neckline + sleek metallic front jacket zipper!
            drawModelStyled(cubeModel_, {0.120f, 1.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.055f, 0.16f, 0.16f}, accC, 0.62f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.136f, 1.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.48f, 0.018f}, Color{186, 194, 208, 255}, 0.26f, 0.82f, 0.0f, MatKind::Standard);
        }

        // Custom 3D Back Gear (Tactical Backpack / Heist Duffle Bag / Cyber Neon Wing Pack)!
        if (customChar != nullptr && customChar->backGearStyle > 0) {
            if (customChar->backGearStyle == 1) {
                // Tactical Olive/Charcoal Spec-Ops Backpack + Utility Pouch + Straps
                drawModelStyled(cubeModel_, {-0.20f, 1.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.16f, 0.44f, 0.32f}, Color{44, 52, 46, 255}, 0.55f, 0.18f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-0.29f, 1.00f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.06f, 0.20f, 0.24f}, Color{30, 34, 38, 255}, 0.48f, 0.25f, 0.0f, MatKind::Standard);
                for (int s = -1; s <= 1; s += 2) {
                    drawModelStyled(cubeModel_, {0.02f, 1.16f, static_cast<float>(s) * 0.12f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.32f, 0.36f, 0.045f}, Color{26, 28, 34, 255}, 0.52f, 0.20f, 0.0f, MatKind::Standard);
                }
            } else if (customChar->backGearStyle == 2) {
                // Diagonal Heist Duffle Bag with Gold Zipper Trim!
                rlPushMatrix();
                rlTranslatef(-0.21f, 1.08f, 0.0f);
                rlRotatef(32.0f, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cylModel_, {0.0f, -0.26f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.135f, 0.52f, 0.135f}, Color{24, 26, 32, 255}, 0.46f, 0.22f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-0.125f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.024f, 0.46f, 0.036f}, Color{242, 198, 64, 255}, 0.22f, 0.88f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            } else if (customChar->backGearStyle == 3) {
                // Cyber Neon Thruster Wing Pack!
                drawModelStyled(cubeModel_, {-0.19f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.12f, 0.36f, 0.22f}, Color{24, 28, 38, 255}, 0.26f, 0.78f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {-0.255f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.028f, 0.09f, 0.06f}, Color{42, 238, 255, 255}, 0.08f, 0.0f, 3.2f, MatKind::EmissiveGlow);
                for (int s = -1; s <= 1; s += 2) {
                    rlPushMatrix();
                    rlTranslatef(-0.20f, 1.16f, static_cast<float>(s) * 0.23f);
                    rlRotatef(static_cast<float>(s) * 26.0f, 1.0f, 0.0f, 0.0f);
                    rlRotatef(-14.0f * static_cast<float>(s), 0.0f, 1.0f, 0.0f);
                    drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.032f, 0.42f, 0.22f}, Color{36, 228, 255, 255}, 0.08f, 0.2f, 2.6f, MatKind::EmissiveGlow);
                    rlPopMatrix();
                }
            }
        }
    }

    // Neck + Organic Sculpted Cranium & Jaw + Ears/Eyes/Nose/Brows/Mouth + 7 Custom Hair/Headgear & 4 Eyewear!
    rlPushMatrix();
    rlTranslatef(0.0f, 1.41f, 0.0f);
    if (alive) {
        rlRotatef(clampf(aimPitch * 0.65f, -0.65f, 0.75f) * kRadToDeg, 0.0f, 0.0f, 1.0f);
    }
    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.068f, 0.090f, 0.068f}, skinC, 0.65f, 0.0f, 0.0f, MatKind::Standard);
    // Smooth organic sculpted cranium + tapered lower jaw/chin
    drawModelStyled(sphereModel_, {0.0f, 0.175f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.132f, 0.140f, 0.124f}, skinC, 0.60f, 0.0f, 0.0f, MatKind::Standard);
    drawModelStyled(sphereModel_, {0.020f, 0.098f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.098f, 0.066f, 0.088f}, skinC, 0.60f, 0.0f, 0.0f, MatKind::Standard);

    const int headStyle = (customChar != nullptr) ? customChar->hairOrHatStyle : 0;
    const Color hairCol = (customChar != nullptr)
                              ? toRayColor(getPlayerHairColor(styleOutfit, customChar->hairColorIdx))
                              : ((styleOutfit == OutfitId::TacOpsArmor) ? Color{32, 38, 34, 255} : Color{36, 30, 26, 255});
    const int glassesStyle = (customChar != nullptr) ? customChar->glassesStyle : (hasSunglasses ? 1 : 0);

    // Sculpted Ears, Nose, Expressive Eyes, Eyebrows & Mouth (skipped inside full-face helmets!)
    if (!shadowPass && headStyle != 4) {
        if (!(isPolice && isSwat)) {
            for (int side = -1; side <= 1; side += 2) {
                drawModelStyled(sphereModel_, {-0.008f, 0.162f, static_cast<float>(side) * 0.124f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.022f, 0.036f, 0.018f},
                                skinC, 0.62f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
        // Sculpted nose bridge/tip
        drawModelStyled(sphereModel_, {0.128f, 0.156f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.022f, 0.024f, 0.016f}, skinC, 0.60f, 0.0f, 0.0f, MatKind::Standard);
        // Eyes + eyebrows (visible when not covered by opaque SWAT goggles or cyber visor)
        if (!(isPolice && isSwat) && headStyle != 5 && glassesStyle != 3) {
            const float browY = panicking ? 0.220f : 0.211f;
            for (int side = -1; side <= 1; side += 2) {
                const float ez = static_cast<float>(side) * 0.044f;
                drawModelStyled(sphereModel_, {0.116f, 0.184f, ez}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.016f, 0.015f, 0.020f}, Color{242, 244, 248, 255}, 0.20f, 0.0f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.128f, 0.184f, ez}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.009f, 0.010f, 0.011f}, Color{24, 26, 32, 255}, 0.12f, 0.15f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.120f, browY, ez}, {1.0f, 0.0f, 0.0f},
                                isPolice ? (static_cast<float>(side) * -10.0f) : 0.0f,
                                {0.016f, 0.011f, 0.040f}, hairCol, 0.65f, 0.0f, 0.0f, MatKind::Standard);
            }
        }
        // Subtle closed lip line — or wide open panic gasp while fleeing!
        if (panicking && alive) {
            drawModelStyled(sphereModel_, {0.112f, 0.106f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.016f, 0.022f, 0.024f}, Color{62, 24, 26, 255}, 0.55f, 0.0f, 0.0f, MatKind::Standard);
        } else {
            drawModelStyled(cubeModel_, {0.114f, 0.110f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.009f, 0.044f}, Color{138, 76, 70, 255}, 0.55f, 0.0f, 0.0f, MatKind::Standard);
        }
    }

    auto drawNaturalHairDome = [&]() {
        // Sculpted smooth rounded hair cap hugging crown, back of head & front hairline
        drawModelStyled(sphereModel_, {-0.015f, 0.214f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.136f, 0.118f, 0.130f}, hairCol, 0.60f, 0.12f, 0.0f, MatKind::Standard);
        drawModelStyled(sphereModel_, {0.042f, 0.278f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.092f, 0.042f, 0.104f}, hairCol, 0.56f, 0.14f, 0.0f, MatKind::Standard);
    };

    if (isPolice && isSwat) {
        // Ballistic SWAT tactical helmet + dark visor goggles
        drawModelStyled(sphereModel_, {-0.005f, 0.23f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.168f, 0.155f, 0.168f}, Color{26, 30, 38, 255}, 0.38f, 0.45f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {0.128f, 0.185f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.04f, 0.075f, 0.24f}, Color{14, 22, 34, 255}, 0.08f, 0.55f, 0.0f, MatKind::CarGlass);
        }
    } else if (isPolice) {
        // Peaked Navy SBPD Police Officer Cap + Gloss Visor + Gold Cap Crest!
        drawModelStyled(cylModel_, {-0.01f, 0.275f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.156f, 0.085f, 0.156f}, Color{22, 34, 64, 255}, 0.48f, 0.15f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.105f, 0.272f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.13f, 0.024f, 0.22f}, Color{16, 18, 24, 255}, 0.16f, 0.45f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {0.145f, 0.318f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.022f, 0.048f, 0.045f}, Color{246, 204, 62, 255}, 0.20f, 0.90f, 0.0f, MatKind::Standard);
        }
    } else if (headStyle == 1) {
        // Slick Pompadour (rounded base + swept high front quiff!)
        drawNaturalHairDome();
        drawModelStyled(sphereModel_, {0.068f, 0.326f, 0.0f}, {0.0f, 0.0f, 1.0f}, 14.0f,
                        {0.105f, 0.068f, 0.102f}, hairCol, 0.40f, 0.22f, 0.0f, MatKind::Standard);
    } else if (headStyle == 2) {
        // Spiky Street Mohawk (tapered scalp + 3 stepped crest spikes along center)
        drawModelStyled(sphereModel_, {-0.010f, 0.216f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.132f, 0.105f, 0.126f}, Color{26, 24, 28, 255}, 0.72f, 0.05f, 0.0f, MatKind::Standard);
        drawModelStyled(coneModel_, { 0.07f, 0.28f, 0.0f}, {0.0f, 0.0f, 1.0f}, -14.0f,
                        {0.045f, 0.19f, 0.042f}, hairCol, 0.38f, 0.20f, 0.0f, MatKind::Standard);
        drawModelStyled(coneModel_, {-0.01f, 0.28f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.048f, 0.22f, 0.044f}, hairCol, 0.38f, 0.20f, 0.0f, MatKind::Standard);
        drawModelStyled(coneModel_, {-0.09f, 0.27f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                        {0.042f, 0.17f, 0.040f}, hairCol, 0.38f, 0.20f, 0.0f, MatKind::Standard);
    } else if (headStyle == 3) {
        // Street Snapback Cap (structured dome + flat forward bill!)
        drawModelStyled(cylModel_, {-0.005f, 0.265f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.152f, 0.095f, 0.152f}, hairCol, 0.52f, 0.12f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.135f, 0.268f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.022f, 0.23f}, Color{20, 22, 28, 255}, 0.38f, 0.25f, 0.0f, MatKind::Standard);
    } else if (headStyle == 4) {
        // Full-Face Aero Moto Helmet (-8% incoming damage perk!)
        drawModelStyled(sphereModel_, {0.005f, 0.185f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.188f, 0.182f, 0.176f}, hairCol, 0.10f, 0.62f, 0.0f, MatKind::CarClearcoat);
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {0.145f, 0.185f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.065f, 0.092f, 0.255f}, Color{14, 24, 38, 255}, 0.05f, 0.85f, 0.0f, MatKind::CarGlass);
            drawModelStyled(cubeModel_, {-0.11f, 0.265f, 0.0f}, {0.0f, 0.0f, 1.0f}, 16.0f,
                            {0.12f, 0.045f, 0.16f}, Color{22, 26, 34, 255}, 0.25f, 0.65f, 0.0f, MatKind::Standard);
        }
    } else if (headStyle == 5) {
        // Cyber Neon Visor + Sleek Sculpted Hair!
        drawNaturalHairDome();
        if (!shadowPass) {
            drawModelStyled(cubeModel_, {0.065f, 0.188f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.165f, 0.064f, 0.264f}, Color{42, 238, 255, 255}, 0.08f, 0.2f, 3.1f, MatKind::EmissiveGlow);
        }
    } else if (headStyle == 6) {
        // 24K VIP Gold Crown!
        drawNaturalHairDome();
        const Color crownGold{250, 206, 62, 255};
        drawModelStyled(cylModel_, {0.0f, 0.305f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.145f, 0.065f, 0.145f}, crownGold, 0.12f, 0.96f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            for (int p = 0; p < 4; ++p) {
                const float ang = static_cast<float>(p) * kHalfPi;
                drawModelStyled(coneModel_, {std::cos(ang) * 0.115f, 0.365f, std::sin(ang) * 0.115f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.036f, 0.095f, 0.036f}, crownGold, 0.12f, 0.96f, 0.0f, MatKind::Standard);
            }
        }
    } else {
        drawNaturalHairDome();
    }

    if (!shadowPass && glassesStyle > 0 && !(isPolice && isSwat) && headStyle != 4 && headStyle != 5) {
        if (glassesStyle == 2) {
            // Gold Aviator Sunglasses (gold brow bar + twin tinted lenses)
            drawModelStyled(cubeModel_, {0.130f, 0.208f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.012f, 0.220f}, Color{246, 202, 68, 255}, 0.16f, 0.92f, 0.0f, MatKind::Standard);
            for (int s = -1; s <= 1; s += 2) {
                drawModelStyled(cubeModel_, {0.128f, 0.176f, static_cast<float>(s) * 0.056f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.028f, 0.056f, 0.084f}, Color{36, 28, 18, 255}, 0.08f, 0.65f, 0.0f, MatKind::CarGlass);
            }
        } else if (glassesStyle == 3) {
            // Cyan Tactical Goggles!
            drawModelStyled(cubeModel_, {0.130f, 0.184f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.036f, 0.068f, 0.230f}, Color{44, 232, 255, 255}, 0.08f, 0.3f, 2.2f, MatKind::EmissiveGlow);
        } else {
            drawModelStyled(cubeModel_, {0.128f, 0.182f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.03f, 0.058f, 0.216f}, Color{16, 20, 28, 255},
                            0.12f, 0.45f, 0.0f, MatKind::CarGlass);
        }
    }
    rlPopMatrix();

    // Articulated Arms, Elbows, Jacket Cuffs, Wristwatch, Hands & Multi-Part 3D Weapons!
    auto drawHangingSculptedArm = [&](int side, float elbowDeg) {
        // Upper arm sleeve
        drawModelStyled(cylModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.068f, 0.22f, 0.068f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
        // Articulated elbow joint + lower forearm sleeve + cuff + wristwatch + sculpted hand
        rlPushMatrix();
        rlTranslatef(0.0f, -0.22f, 0.0f);
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.064f, 0.062f, 0.064f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
        rlRotatef(elbowDeg, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, -0.21f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.058f, 0.21f, 0.058f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            drawModelStyled(cylModel_, {0.0f, -0.215f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.062f, 0.028f, 0.062f}, accC, 0.45f, 0.15f, 0.0f, MatKind::Standard);
            if (side < 0) {
                // Sleek metallic wristwatch on left wrist!
                drawModelStyled(cylModel_, {0.0f, -0.236f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.056f, 0.022f, 0.056f}, Color{198, 206, 220, 255}, 0.18f, 0.90f, 0.0f, MatKind::Standard);
            }
            drawModelStyled(sphereModel_, {0.0f, -0.275f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.054f, 0.064f, 0.048f}, skinC, 0.62f, 0.0f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();
    };

    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 1.31f, static_cast<float>(side) * 0.258f);
        if (armed && side == 1 && alive) {
            rlRotatef(aimPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cylModel_, {0.02f, -0.04f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.068f, 0.41f, 0.068f}, topC, topRough, topMetal, 0.0f, MatKind::Standard);
            if (!shadowPass) {
                drawModelStyled(cylModel_, {0.40f, -0.04f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.072f, 0.032f, 0.072f}, accC, 0.45f, 0.15f, 0.0f, MatKind::Standard);
            }
            drawModelStyled(sphereModel_, {0.46f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.058f, 0.054f, 0.052f}, skinC, 0.62f, 0.0f, 0.0f, MatKind::Standard);

            const int wSkin = (customWep != nullptr) ? customWep->skinIndex : 0;
            const auto wFin = getWeaponFinishMaterial(weapon, wSkin);
            const Color wPrimCol = toRayColor(wFin.primary);
            const Color wSecCol  = toRayColor(wFin.secondary);
            const MatKind wMat   = (wFin.emissive > 0.1f) ? MatKind::EmissiveGlow : MatKind::Standard;

            if (weapon == WeaponId::HeavyLauncher) {
                // Olive-drab or custom-finished shoulder-mounted tactical explosive launcher tube!
                drawModelStyled(cylModel_, {0.10f, 0.10f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.085f, 0.96f, 0.085f},
                                (wSkin > 0) ? wPrimCol : Color{62, 76, 56, 255},
                                (wSkin > 0) ? wFin.roughness : 0.42f,
                                (wSkin > 0) ? wFin.metallic : 0.55f, wFin.emissive, wMat);
                drawModelStyled(cylModel_, {0.98f, 0.10f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.11f, 0.12f, 0.11f}, (wSkin > 0) ? wSecCol : Color{36, 40, 46, 255},
                                0.35f, 0.75f, 0.0f, MatKind::Standard);
            } else if (weapon == WeaponId::Minigun) {
                // Underslung 6-Barrel Rotary Gatling Minigun!
                drawModelStyled(cubeModel_, {0.42f, -0.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.34f, 0.18f, 0.16f}, (wSkin > 0) ? wPrimCol : Color{32, 36, 44, 255},
                                (wSkin > 0) ? wFin.roughness : 0.32f,
                                (wSkin > 0) ? wFin.metallic : 0.78f, wFin.emissive, wMat);
                drawModelStyled(cubeModel_, {0.46f, 0.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.16f, 0.08f, 0.14f}, Color{24, 28, 34, 255},
                                0.38f, 0.65f, 0.0f, MatKind::Standard);
                rlPushMatrix();
                rlTranslatef(0.58f, -0.05f, 0.0f);
                if (muzzleFlash > 0.0f) {
                    rlRotatef(static_cast<float>(GetTime()) * 2160.0f, 1.0f, 0.0f, 0.0f);
                }
                for (int b = 0; b < 6; ++b) {
                    const float ang = static_cast<float>(b) * (kTwoPi / 6.0f);
                    drawModelStyled(cylModel_, {0.0f, std::cos(ang) * 0.048f, std::sin(ang) * 0.048f},
                                    {0.0f, 0.0f, 1.0f}, -90.0f,
                                    {0.016f, 0.62f, 0.016f}, (wSkin > 0) ? wSecCol : Color{156, 166, 182, 255},
                                    0.20f, 0.92f, 0.0f, MatKind::Standard);
                }
                drawModelStyled(cylModel_, {0.50f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.072f, 0.045f, 0.072f}, Color{38, 42, 50, 255},
                                0.30f, 0.80f, 0.0f, MatKind::Standard);
                rlPopMatrix();
                if (!shadowPass && muzzleFlash > 0.0f) {
                    drawModelStyled(sphereModel_, {1.24f, -0.05f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.24f, 0.18f, 0.18f}, Color{255, 212, 82, 255},
                                    0.1f, 0.0f, 3.8f, MatKind::EmissiveGlow);
                }
            } else {
                const bool isSniper = (weapon == WeaponId::SniperRifle);
                const bool isRevolver = (weapon == WeaponId::HeavyRevolver);
                const bool isAutoShot = (weapon == WeaponId::AutoShotgun);
                const bool heavyMg = (weapon == WeaponId::CombatMG);
                const bool longGun = (weapon == WeaponId::AssaultRifle || weapon == WeaponId::Shotgun ||
                                      isAutoShot || isSniper || heavyMg);
                const float gunLen = isSniper ? 0.96f
                                   : (heavyMg ? 0.78f
                                   : (longGun ? 0.66f
                                   : (isRevolver ? 0.42f : (weapon == WeaponId::SMG ? 0.44f : 0.32f))));
                const float gunCenterX = 0.55f + (isSniper ? 0.12f : 0.0f);
                const Color bodyTint = (wSkin > 0) ? wPrimCol : (isRevolver ? Color{210, 218, 230, 255} : Color{34, 38, 46, 255});
                const float bodyRough = (wSkin > 0) ? wFin.roughness : (isRevolver ? 0.14f : 0.28f);
                const float bodyMetal = (wSkin > 0) ? wFin.metallic  : (isRevolver ? 0.94f : 0.82f);
                drawModelStyled(cubeModel_, {gunCenterX, 0.025f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {gunLen * 0.72f, heavyMg ? 0.115f : (isSniper ? 0.082f : 0.095f), 0.072f}, bodyTint,
                                bodyRough, bodyMetal, wFin.emissive, wMat);
                // Machined round steel barrel extending cleanly forward + compact buttstock & top optic on long guns
                drawModelStyled(cylModel_, {gunCenterX + gunLen * 0.20f, 0.036f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.020f, gunLen * 0.34f, 0.020f}, Color{46, 52, 62, 255},
                                0.24f, 0.88f, 0.0f, MatKind::Standard);
                if (!shadowPass && longGun) {
                    drawModelStyled(cubeModel_, {gunCenterX - gunLen * 0.44f, 0.008f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.20f, 0.092f, 0.056f}, Color{26, 30, 36, 255},
                                    0.48f, 0.32f, 0.0f, MatKind::Standard);
                    if (!isSniper) {
                        drawModelStyled(cubeModel_, {gunCenterX - 0.03f, 0.092f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.095f, 0.048f, 0.052f}, Color{22, 26, 32, 255},
                                        0.34f, 0.65f, 0.0f, MatKind::Standard);
                    }
                }

                const float extScale = (customWep != nullptr && customWep->extendedMag) ? 1.45f : 1.0f;
                if (!shadowPass && isSniper) {
                    // Telescopic optic tube atop Sniper Rifle
                    drawModelStyled(cylModel_, {0.46f, 0.105f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                    {0.030f, 0.32f, 0.030f}, Color{24, 28, 36, 255},
                                    0.25f, 0.85f, 0.0f, MatKind::Standard);
                } else if (!shadowPass && isAutoShot) {
                    // Round drum magazine under Assault Shotgun
                    drawModelStyled(cylModel_, {0.56f, -0.09f, -0.06f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                    {0.095f * extScale, 0.12f * extScale, 0.095f * extScale}, Color{28, 32, 38, 255},
                                    0.38f, 0.65f, 0.0f, MatKind::Standard);
                } else if (!shadowPass && heavyMg) {
                    // Box drum magazine under Combat MG
                    drawModelStyled(cubeModel_, {0.54f, -0.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.16f * extScale, 0.16f * extScale, 0.14f}, Color{54, 66, 52, 255},
                                    0.45f, 0.45f, 0.0f, MatKind::Standard);
                } else if (!shadowPass && (weapon == WeaponId::SMG || weapon == WeaponId::AssaultRifle ||
                                           (weapon == WeaponId::Pistol && extScale > 1.1f))) {
                    drawModelStyled(cubeModel_, {0.56f, -0.09f - (extScale - 1.0f) * 0.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, 0.16f * extScale, 0.055f}, Color{24, 26, 32, 255},
                                    0.35f, 0.70f, 0.0f, MatKind::Standard);
                }

                // 3D Tactical Barrel Suppressor!
                const float gunFrontX = gunCenterX + gunLen * 0.5f;
                if (!shadowPass && customWep != nullptr && customWep->suppressor && weaponSupportsSuppressor(weapon)) {
                    drawModelStyled(cylModel_, {gunFrontX - 0.015f, 0.028f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                    {0.032f, 0.22f, 0.032f}, Color{26, 30, 38, 255},
                                    0.34f, 0.72f, 0.0f, MatKind::Standard);
                }
                if (!shadowPass && muzzleFlash > 0.0f) {
                    drawModelStyled(sphereModel_, {gunFrontX + 0.03f, 0.03f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.19f, 0.14f, 0.14f}, Color{255, 224, 92, 255},
                                    0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
                }
            }
        } else if (panicking && alive) {
            // Both arms thrown high above the head waving frantically while fleeing!
            const float waveDeg = 152.0f + std::sin(animPhase * 1.45f + static_cast<float>(side) * 1.15f) * 20.0f;
            rlRotatef(waveDeg, 0.0f, 0.0f, 1.0f);
            rlRotatef(static_cast<float>(-side) * 14.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, 22.0f);
        } else if (jumping && alive) {
            // Athletic mid-air arm lift for jump balance
            rlRotatef((side < 0) ? 46.0f : 32.0f, 0.0f, 0.0f, 1.0f);
            rlRotatef(static_cast<float>(-side) * 18.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, 32.0f);
        } else {
            const float armSwing = static_cast<float>(-side) * legSwing * 0.8f;
            rlRotatef(armSwing, 0.0f, 0.0f, 1.0f);
            drawHangingSculptedArm(side, alive ? (10.0f + std::max(0.0f, armSwing * 0.55f)) : 0.0f);
        }
        rlPopMatrix();
    }

    rlPopMatrix(); // end torso lean
    rlPopMatrix(); // end character root
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
        const float skidY = world.surfaceHeightAt(mid) + 0.014f;
        rlPushMatrix();
        rlTranslatef(mid.x, skidY, mid.z);
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
        if (p.kind == ParticleKind::BrassCasing) {
            const float fade = clampf(life * 2.5f, 0.0f, 1.0f);
            Color brassCol = toRayColor(p.color);
            brassCol.a = static_cast<unsigned char>(255.0f * fade);
            rlPushMatrix();
            rlTranslatef(p.pos.x, p.pos.y, p.pos.z);
            rlRotatef(p.spinAngle * kRadToDeg, 0.45f, 0.85f, 0.30f);
            rlRotatef(p.spinAngle * kRadToDeg * 0.65f, 1.0f, 0.0f, 0.0f);
            drawModelStyled(cylModel_, {0.0f, -0.024f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.048f, 0.014f}, brassCol,
                            0.14f, 0.95f, 0.35f, MatKind::Standard);
            rlPopMatrix();
        } else if (p.kind == ParticleKind::ShotgunShell) {
            const float fade = clampf(life * 2.5f, 0.0f, 1.0f);
            Color hullCol{216, 36, 42, static_cast<unsigned char>(255.0f * fade)};
            Color rimCol{242, 204, 88, static_cast<unsigned char>(255.0f * fade)};
            rlPushMatrix();
            rlTranslatef(p.pos.x, p.pos.y, p.pos.z);
            rlRotatef(p.spinAngle * kRadToDeg, 0.65f, 0.70f, 0.25f);
            rlRotatef(p.spinAngle * kRadToDeg * 0.50f, 0.0f, 0.0f, 1.0f);
            // Red 12-gauge hull body + bright metallic brass head
            drawModelStyled(cylModel_, {0.0f, -0.018f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.056f, 0.020f}, hullCol, 0.38f, 0.12f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.0f, -0.036f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.021f, 0.020f, 0.021f}, rimCol, 0.14f, 0.95f, 0.30f, MatKind::Standard);
            rlPopMatrix();
        } else if (p.kind == ParticleKind::WaterSpray) {
            Color sprayCol = toRayColor(p.color);
            sprayCol.a = static_cast<unsigned char>(clampf(static_cast<float>(sprayCol.a) * life * 0.85f, 8.0f, 170.0f));
            const float r = p.size * (0.65f + (1.0f - life) * 0.65f);
            drawModelStyled(sphereModel_, {p.pos.x, p.pos.y, p.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {r, r * 0.78f, r}, sprayCol, 0.35f, 0.05f, 0.0f, MatKind::Standard);
        } else {
            Color c = toRayColor(p.color);
            c.a = static_cast<unsigned char>(static_cast<float>(c.a) * life);
            const bool fiery = (p.color.r > 210 && p.color.g > 90 && p.color.b < 140);
            drawModelStyled(sphereModel_, {p.pos.x, p.pos.y, p.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {p.size * 0.65f, p.size * 0.65f, p.size * 0.65f}, c,
                            0.65f, 0.0f, fiery ? 2.2f : 0.0f,
                            fiery ? MatKind::EmissiveGlow : MatKind::Standard);
        }
    }
}

void Renderer3D::drawGpsOverheadArrow(const GameWorld& world) {
    if (world.mission().kind == ActiveMissionKind::None) return;

    const Vec2 origin = (world.playerVehicle() != nullptr) ? world.playerVehicle()->pos : world.player().pos;
    const float originY = (world.playerVehicle() != nullptr) ? world.playerVehicle()->posY : world.player().posY;
    const Vec2 delta = world.mission().currentObjectivePos - origin;
    if (delta.length() < 8.0f) return;

    const float yaw = std::atan2(delta.z, delta.x);
    rlPushMatrix();
    rlTranslatef(origin.x, originY + 3.55f, origin.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
    drawModelStyled(cubeModel_, {-0.25f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.95f, 0.16f, 0.34f}, Color{255, 216, 48, 240},
                    0.15f, 0.0f, 1.8f, MatKind::EmissiveGlow);
    drawModelStyled(coneModel_, {0.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                    {0.44f, 0.65f, 0.44f}, Color{255, 228, 72, 245},
                    0.15f, 0.0f, 2.0f, MatKind::EmissiveGlow);
    rlPopMatrix();
}

void Renderer3D::drawFirstPersonOverlay3D(const GameWorld& world, const Camera3D& camera, float adsBlend) {
    // Perspective-invariant radial scale around camera.position keeps all interior/viewmodel geometry
    // within [0.095m .. 0.235m] so it never clips into walls while preserving exact lighting & self-depth.
    constexpr float kVmScale = 0.36f;

    const auto& pl = world.player();
    const auto& outSpec = getOutfitSpec(pl.currentOutfit);
    const Color sleeveCol = toRayColor(getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx));
    const Color cuffCol   = toRayColor(outSpec.accent);
    const Color skinCol   = toRayColor(getPlayerSkinColor(pl.characterCustom.skinToneIdx));

    const auto& wSlot = pl.weapons[static_cast<std::size_t>(pl.currentWeapon)];
    const int wSkin = wSlot.skinIndex;
    const auto wFin = getWeaponFinishMaterial(pl.currentWeapon, wSkin);
    const Color gunMetal     = (wSkin > 0) ? toRayColor(wFin.primary)   : Color{34, 38, 46, 255};
    const Color gunSteel     = (wSkin > 0) ? toRayColor(wFin.secondary) : Color{66, 72, 84, 255};
    const Color chromeBright = (wSkin > 0) ? toRayColor(wFin.primary)   : Color{214, 222, 234, 255};
    const Color polyDark{20, 23, 28, 255};
    const float wRough = wFin.roughness;
    const float wMetal = wFin.metallic;
    const float wEmis  = wFin.emissive;
    const MatKind wMat = (wEmis > 0.1f) ? MatKind::EmissiveGlow : MatKind::Standard;
    const float extScale = wSlot.extendedMag ? 1.42f : 1.0f;

    // -----------------------------------------------------------------------
    // A) FIRST-PERSON VEHICLE COCKPIT (Dashboard, Digital Gauges, Steering Wheel & Hands)
    // -----------------------------------------------------------------------
    if (const Vehicle* car = world.playerVehicle()) {
        const auto& spec = getVehicleSpec(car->kind);
        const Vec3 eye = driverSeatEyeLocal(car->kind);
        const float halfW = spec.halfWidth;
        const float rpmRatio = clampf((car->engineRpm - 880.0f) / 7270.0f, 0.0f, 1.0f);
        const float stanceOffsetY = car->destroyed ? 0.0f
                                  : ((car->stanceStyle == 1) ? -0.032f
                                  : ((car->stanceStyle == 2) ? -0.048f
                                  : ((car->stanceStyle == 3) ?  0.095f : 0.0f)));

        rlPushMatrix();
        rlTranslatef(camera.position.x, camera.position.y, camera.position.z);
        rlScalef(kVmScale, kVmScale, kVmScale);
        rlTranslatef(car->pos.x - camera.position.x, car->posY + stanceOffsetY - camera.position.y, car->pos.z - camera.position.z);
        rlRotatef(-car->heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(car->bodyPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
        rlRotatef(car->bodyRoll * kRadToDeg, 1.0f, 0.0f, 0.0f);

        // 1. Sculpted dark stitched-leather dashboard deck, trim strip & dual circular brushed-chrome AC vents!
        const float dashX = eye.x + 0.54f;
        const float dashY = eye.y - 0.275f;
        drawModelStyled(cubeModel_, {dashX, dashY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.26f, 0.18f, halfW * 1.72f}, Color{22, 25, 32, 255}, 0.55f, 0.15f, 0.0f, MatKind::Standard);
        const Color accentStrip = (car->kind == VehicleKind::Hypercar || car->kind == VehicleKind::TrackPrototype ||
                                   car->kind == VehicleKind::SportsCoupe || car->kind == VehicleKind::ClassicRoadster ||
                                   car->kind == VehicleKind::JdmTuner)
                                      ? toRayColor(car->paintColor)
                                      : Color{140, 150, 168, 255};
        drawModelStyled(cubeModel_, {dashX - 0.125f, dashY + 0.055f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.02f, 0.016f, halfW * 1.66f}, accentStrip, 0.22f, 0.65f, 0.0f, MatKind::Standard);

        // Dual circular brushed-chrome AC air vents flanking driver & center console
        const float ventZs[2] = {eye.z - 0.22f, 0.22f};
        for (float vz : ventZs) {
            drawModelStyled(cylModel_, {dashX - 0.130f, dashY + 0.018f, vz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.036f, 0.024f, 0.036f}, Color{198, 206, 218, 255}, 0.18f, 0.90f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {dashX - 0.134f, dashY + 0.018f, vz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.029f, 0.016f, 0.029f}, Color{14, 16, 22, 255}, 0.55f, 0.25f, 0.0f, MatKind::Standard);
            for (int sl = -1; sl <= 1; ++sl) {
                drawModelStyled(cubeModel_, {dashX - 0.136f, dashY + 0.018f + static_cast<float>(sl) * 0.011f, vz},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.008f, 0.004f, 0.048f},
                                Color{150, 158, 172, 255}, 0.25f, 0.80f, 0.0f, MatKind::Standard);
            }
        }

        // 2. Instrument binnacle hood, twin glowing gauge rings & 10-LED progressive RPM shift-light strip!
        const float binX = eye.x + 0.485f;
        const float binY = eye.y - 0.165f;
        drawModelStyled(cubeModel_, {binX, binY, eye.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.12f, 0.108f, 0.29f}, polyDark, 0.48f, 0.20f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {binX - 0.058f, binY - 0.004f, eye.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.012f, 0.080f, 0.252f}, Color{10, 16, 26, 255}, 0.15f, 0.1f, 0.4f, MatKind::EmissiveGlow);
        // Twin circular glowing tachometer & speedometer bezels inside digital binnacle
        for (int g = -1; g <= 1; g += 2) {
            const float gz = eye.z + static_cast<float>(g) * 0.068f;
            const Color dialCol = (g < 0) ? Color{56, 212, 255, 255} : Color{255, 196, 68, 255};
            drawModelStyled(cylModel_, {binX - 0.063f, binY - 0.014f, gz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.028f, 0.006f, 0.028f}, dialCol, 0.1f, 0.0f, 1.75f, MatKind::EmissiveGlow);
            drawModelStyled(cylModel_, {binX - 0.066f, binY - 0.014f, gz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.022f, 0.005f, 0.022f}, Color{10, 16, 26, 255}, 0.2f, 0.1f, 0.35f, MatKind::EmissiveGlow);
        }
        // 10-LED progressive motorsport RPM shift-light strip across top brow of instrument cluster!
        const bool redlineBlink = (rpmRatio > 0.88f) && (std::fmod(static_cast<float>(GetTime()) * 11.0f, 1.0f) < 0.5f);
        for (int led = 0; led < 10; ++led) {
            const float thresh = static_cast<float>(led) / 10.0f;
            const bool lit = (rpmRatio > thresh + 0.04f) && (!redlineBlink || led < 7);
            const float lz = eye.z - 0.099f + static_cast<float>(led) * 0.022f;
            Color ledCol = Color{28, 34, 46, 255};
            if (lit) {
                ledCol = (led < 4) ? Color{52, 224, 255, 255}
                       : ((led < 7) ? Color{68, 245, 142, 255}
                       : ((led < 9) ? Color{255, 198, 46, 255}
                                    : Color{255, 42, 48, 255}));
            }
            drawModelStyled(cubeModel_, {binX - 0.064f, binY + 0.024f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.008f, 0.011f, 0.016f}, ledCol, 0.12f, 0.0f,
                            lit ? 2.8f : 0.0f, lit ? MatKind::EmissiveGlow : MatKind::Standard);
        }

        // 3. Detailed Center Console GPS Navigation Map / Police MDT + 3 Rotary Climate Knobs + Lower Gear Shifter!
        rlPushMatrix();
        rlTranslatef(eye.x + 0.48f, eye.y - 0.185f, 0.03f);
        rlRotatef(14.0f, 0.0f, 1.0f, 0.0f);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.026f, 0.120f, 0.195f}, polyDark, 0.35f, 0.35f, 0.0f, MatKind::Standard);
        const bool emergencyScreen = isEmergencySirenVehicle(*car);
        // Dark screen backdrop
        drawModelStyled(cubeModel_, {-0.013f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.007f, 0.098f, 0.172f}, Color{12, 22, 36, 255}, 0.12f, 0.1f, 0.55f, MatKind::EmissiveGlow);
        if (emergencyScreen) {
            // Police / EMS / Fire MDT dispatch header bar + tactical scan rings!
            const Color hdrCol = (car->sirenActive && car->sirenPhase < 0.5f) ? Color{255, 52, 64, 255} : Color{48, 152, 255, 255};
            drawModelStyled(cubeModel_, {-0.017f, 0.036f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.004f, 0.018f, 0.160f}, hdrCol, 0.1f, 0.0f, 2.4f, MatKind::EmissiveGlow);
            for (int r = 0; r < 3; ++r) {
                drawModelStyled(cubeModel_, {-0.017f, 0.008f - static_cast<float>(r) * 0.020f, -0.025f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.004f, 0.008f, 0.095f - static_cast<float>(r) * 0.015f},
                                Color{72, 212, 255, 255}, 0.1f, 0.0f, 1.7f, MatKind::EmissiveGlow);
            }
        } else {
            // Illuminated GPS city grid + bright route path + pulsing player chevron!
            for (int g = -1; g <= 1; ++g) {
                drawModelStyled(cubeModel_, {-0.016f, static_cast<float>(g) * 0.026f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.004f, 0.003f, 0.158f}, Color{28, 92, 132, 255}, 0.1f, 0.0f, 0.9f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {-0.016f, 0.0f, static_cast<float>(g) * 0.044f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.004f, 0.088f, 0.003f}, Color{28, 92, 132, 255}, 0.1f, 0.0f, 0.9f, MatKind::EmissiveGlow);
            }
            // Active neon route line & triangular GPS vehicle marker
            drawModelStyled(cubeModel_, {-0.018f, 0.012f, 0.016f}, {1.0f, 0.0f, 0.0f}, 24.0f,
                            {0.004f, 0.068f, 0.007f}, Color{68, 245, 168, 255}, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
            drawModelStyled(coneModel_, {-0.019f, -0.020f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.008f, 0.018f, 0.008f}, Color{255, 214, 58, 255}, 0.1f, 0.0f, 2.6f, MatKind::EmissiveGlow);
        }
        // 3 brushed-chrome rotary climate knobs + hazard button beneath screen
        for (int k = -1; k <= 1; ++k) {
            drawModelStyled(cylModel_, {-0.015f, -0.076f, static_cast<float>(k) * 0.048f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.013f, 0.016f, 0.013f}, Color{198, 206, 218, 255}, 0.20f, 0.90f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-0.015f, -0.076f, 0.080f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.010f, 0.014f, 0.016f}, Color{236, 46, 48, 255}, 0.2f, 0.0f, 1.4f, MatKind::EmissiveGlow);
        rlPopMatrix();

        // Lower center tunnel, metallic gated gear shifter stalk & twin recessed cupholders!
        drawModelStyled(cubeModel_, {eye.x + 0.34f, eye.y - 0.355f, 0.02f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.34f, 0.06f, 0.17f}, Color{24, 27, 34, 255}, 0.55f, 0.18f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {eye.x + 0.39f, eye.y - 0.322f, 0.02f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.075f, 0.012f, 0.068f}, Color{168, 176, 190, 255}, 0.20f, 0.88f, 0.0f, MatKind::Standard);
        const float gearTiltDeg = clampf((static_cast<float>(car->currentGear) - 3.0f) * 4.5f, -14.0f, 14.0f);
        rlPushMatrix();
        rlTranslatef(eye.x + 0.39f, eye.y - 0.320f, 0.02f);
        rlRotatef(gearTiltDeg, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.008f, 0.095f, 0.008f}, Color{208, 214, 224, 255}, 0.16f, 0.94f, 0.0f, MatKind::Standard);
        drawModelStyled(sphereModel_, {0.0f, 0.100f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.022f, 0.026f, 0.022f}, Color{32, 36, 44, 255}, 0.30f, 0.35f, 0.0f, MatKind::Standard);
        rlPopMatrix();
        for (int ch = 0; ch < 2; ++ch) {
            drawModelStyled(cylModel_, {eye.x + 0.26f, eye.y - 0.324f, -0.022f + static_cast<float>(ch) * 0.072f},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.026f, 0.008f, 0.026f},
                            Color{14, 16, 20, 255}, 0.55f, 0.20f, 0.0f, MatKind::Standard);
        }

        // 4. Wide-view A-pillars, top sun-visor bar (omitted on open-top ClassicRoadster!), center mirror & animated rain wipers!
        for (int side = -1; side <= 1; side += 2) {
            rlPushMatrix();
            rlTranslatef(eye.x + 0.42f, eye.y + 0.05f, static_cast<float>(side) * (halfW * 0.85f));
            rlRotatef(34.0f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.052f, 0.52f, 0.048f}, polyDark, 0.60f, 0.1f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }
        if (car->kind != VehicleKind::ClassicRoadster) {
            drawModelStyled(cubeModel_, {eye.x + 0.34f, eye.y + 0.265f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 0.036f, halfW * 1.56f}, polyDark, 0.65f, 0.05f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {eye.x + 0.42f, eye.y + 0.195f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.026f, 0.052f, 0.20f}, polyDark, 0.35f, 0.25f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {eye.x + 0.405f, eye.y + 0.195f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.008f, 0.042f, 0.184f}, Color{96, 128, 158, 255}, 0.06f, 0.85f, 0.0f, MatKind::CarGlass);

        // Dual Windshield Wipers mounted flush on the outer cowl windshield plane (sweep smoothly in rain without blocking sightline!)
        const float wiperSweep = world.isRainWeather()
                                     ? (0.5f - 0.5f * std::cos(static_cast<float>(GetTime()) * 3.6f + 1.05f))
                                     : 0.0f;
        const float wiperDeg = lerpf(86.0f, 22.0f, wiperSweep);
        const float wiperPivots[2] = {eye.z - 0.06f, 0.24f};
        for (float wpZ : wiperPivots) {
            rlPushMatrix();
            rlTranslatef(eye.x + 0.655f, eye.y - 0.198f, wpZ);
            rlRotatef(34.0f, 0.0f, 0.0f, 1.0f);   // Match windshield slope plane
            rlRotatef(wiperDeg, 1.0f, 0.0f, 0.0f); // Sweep across lower-to-mid glass
            // Sleek cowl pivot cap + slender steel wiper arm + aerodynamic squeegee blade
            drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0055f, 0.145f, 0.0055f}, Color{26, 30, 36, 255}, 0.35f, 0.70f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.004f, 0.155f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.007f, 0.036f, 0.011f}, Color{36, 40, 48, 255}, 0.35f, 0.65f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.005f, 0.190f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0055f, 0.215f, 0.0085f}, Color{16, 18, 22, 255}, 0.58f, 0.22f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }

        // 5. Smooth 24-Segment Sport Steering Wheel + Thumb Bolsters + Anodized Paddle Shifters + Driver Hands!
        rlPushMatrix();
        rlTranslatef(eye.x + 0.365f, eye.y - 0.195f, eye.z);
        rlRotatef(-18.0f, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.07f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.14f, 0.055f, 0.055f}, polyDark, 0.5f, 0.2f, 0.0f, MatKind::Standard);
        rlRotatef(car->steerVisual * 68.0f, 1.0f, 0.0f, 0.0f);

        constexpr int kRimSegs = 24;
        constexpr float kWheelR = 0.146f;
        const bool isSportWheel = (car->kind == VehicleKind::TrackPrototype ||
                                   car->kind == VehicleKind::Hypercar ||
                                   car->kind == VehicleKind::SportsCoupe ||
                                   car->kind == VehicleKind::ClassicRoadster ||
                                   car->kind == VehicleKind::JdmTuner ||
                                   car->kind == VehicleKind::MuscleCar);
        for (int s = 0; s < kRimSegs; ++s) {
            const float angDeg = static_cast<float>(s) * (360.0f / static_cast<float>(kRimSegs));
            const float angRad = angDeg * (kPi / 180.0f);
            const float ry = std::cos(angRad) * kWheelR;
            const float rz = std::sin(angRad) * kWheelR;
            const bool topStripe = (s == 0) && isSportWheel;
            const bool thumbBolster = (s == 4 || s == 5 || s == 19 || s == 20);
            rlPushMatrix();
            rlTranslatef(0.0f, ry, rz);
            rlRotatef(angDeg, 1.0f, 0.0f, 0.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {thumbBolster ? 0.029f : 0.025f, thumbBolster ? 0.028f : 0.024f, 0.044f},
                            topStripe ? Color{236, 42, 48, 255}
                                      : (thumbBolster ? Color{40, 44, 54, 255} : Color{26, 30, 38, 255}),
                            0.34f, 0.18f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }
        // Center hub, brushed emblem, 3 metallic spokes & dual anodized F1 paddle shifters behind wheel!
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.032f, 0.054f, 0.054f}, polyDark, 0.42f, 0.25f, 0.0f, MatKind::Standard);
        drawModelStyled(sphereModel_, {-0.016f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.012f, 0.022f, 0.022f}, Color{242, 198, 64, 255}, 0.2f, 0.88f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.016f, 0.024f, kWheelR * 1.88f}, gunSteel, 0.25f, 0.80f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, -kWheelR * 0.46f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.016f, kWheelR * 0.92f, 0.026f}, gunSteel, 0.25f, 0.80f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {0.028f, 0.020f, static_cast<float>(side) * (kWheelR * 0.74f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.007f, 0.050f, 0.020f},
                            isSportWheel ? Color{216, 52, 58, 255} : Color{186, 194, 208, 255},
                            0.20f, 0.88f, 0.0f, MatKind::Standard);
        }

        // Driver sculpted hands, cuffs & sleeves gripping 9-and-3 positions (100% clear of camera near-plane!)
        for (int side = -1; side <= 1; side += 2) {
            const float hz = static_cast<float>(side) * kWheelR;
            drawModelStyled(sphereModel_, {-0.008f, 0.01f, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.028f, 0.024f}, skinCol, 0.64f, 0.0f, 0.0f, MatKind::Standard);
            rlPushMatrix();
            rlTranslatef(-0.068f, -0.036f, hz * 1.05f);
            rlRotatef(28.0f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cylModel_, {-0.038f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.026f, 0.095f, 0.026f}, sleeveCol, 0.58f, 0.08f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.048f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.028f, 0.018f, 0.028f}, cuffCol, 0.42f, 0.20f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }
        rlPopMatrix();

        rlPopMatrix();
        return;
    }

    // -----------------------------------------------------------------------
    // B) FIRST-PERSON ON-FOOT WEAPON & HANDS VIEWMODEL (True Co-Axial ADS Optics!)
    // -----------------------------------------------------------------------
    const Vec3 camPos{camera.position.x, camera.position.y, camera.position.z};
    const Vec3 camTgt{camera.target.x, camera.target.y, camera.target.z};
    const Vec3 camFwd = (camTgt - camPos).normalized();
    const float viewYaw = std::atan2(camFwd.z, camFwd.x);
    const float viewPitch = std::asin(clampf(camFwd.y, -0.999f, 0.999f));

    const float ads = clampf(adsBlend, 0.0f, 1.0f);
    const float moveSpd = clampf(pl.velocity.length() / 8.8f, 0.0f, 1.0f);
    const float swayAtten = (1.0f - ads * 0.88f) * (pl.onGround ? 1.0f : 0.25f);
    const float swayZ = std::cos(pl.walkAnimPhase) * 0.0085f * moveSpd * swayAtten;
    const float swayY = std::abs(std::sin(pl.walkAnimPhase)) * -0.0075f * moveSpd * swayAtten;
    const float jumpDipY = pl.onGround ? 0.0f : clampf(-pl.verticalVel * 0.0025f, -0.018f, 0.018f);

    const auto& wSpec = getWeaponSpec(pl.currentWeapon);
    float recoilFrac = 0.0f;
    if (wSpec.fireInterval > 0.01f && pl.fireCooldown > 0.0f) {
        recoilFrac = clampf(pl.fireCooldown / std::min(wSpec.fireInterval, 0.18f), 0.0f, 1.0f);
    }
    const float kickBack = recoilFrac * (ads > 0.5f ? 0.020f : 0.030f);
    const float kickUpDeg = recoilFrac * (ads > 0.5f ? 1.8f : 3.4f);

    float reloadTiltDeg = 0.0f;
    float reloadDropY = 0.0f;
    if (pl.reloadTimer > 0.0f && wSpec.reloadDuration > 0.05f) {
        const float rf = std::sin(clampf(pl.reloadTimer / wSpec.reloadDuration, 0.0f, 1.0f) * kPi);
        reloadTiltDeg = -28.0f * rf;
        reloadDropY = -0.065f * rf;
    }

    // Local viewmodel frame: +X = Forward, +Y = Up, +Z = Right
    rlPushMatrix();
    rlTranslatef(camera.position.x, camera.position.y, camera.position.z);
    rlScalef(kVmScale, kVmScale, kVmScale);
    rlRotatef(-viewYaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(viewPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);

    // Full-screen Optical Mil-Dot Sniper Scope view: hide 3D gun body once zoomed inside the eyepiece!
    if (pl.currentWeapon == WeaponId::SniperRifle && ads > 0.72f) {
        rlPopMatrix();
        return;
    }

    // Slot 1: Unarmed Fists (Dual Guard Hands + Jab Punch)
    if (pl.currentWeapon == WeaponId::Fists) {
        const float punchR = recoilFrac * 0.14f;
        for (int side = -1; side <= 1; side += 2) {
            const float jab = (side > 0) ? punchR : 0.0f;
            const float fz = static_cast<float>(side) * (0.155f - ads * 0.035f) + swayZ;
            const float fy = -0.185f + ads * 0.035f + swayY + jumpDipY;
            const float fx = 0.39f + jab;

            rlPushMatrix();
            rlTranslatef(fx, fy, fz);
            rlRotatef(static_cast<float>(-side) * 10.0f, 0.0f, 1.0f, 0.0f);
            rlRotatef(18.0f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cubeModel_, {0.035f, 0.015f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.068f, 0.062f, 0.064f}, skinCol, 0.68f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.075f, -0.025f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.17f, 0.058f, 0.058f}, sleeveCol, 0.58f, 0.08f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.005f, -0.008f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.062f, 0.062f}, cuffCol, 0.42f, 0.20f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        }
        rlPopMatrix();
        return;
    }

    // Slots 2..11: Firearms — at ads=1, gunPos=(x, -0.105f, 0.0f) and sight center is at (+0.105f, 0.0f) -> EXACT screen center!
    const bool isMinigun = (pl.currentWeapon == WeaponId::Minigun);
    const float gunX = lerpf(isMinigun ? 0.40f : 0.43f, isMinigun ? 0.37f : 0.385f, ads) - kickBack;
    const float gunY = lerpf(isMinigun ? -0.225f : -0.190f, isMinigun ? -0.175f : -0.105f, ads) + swayY + jumpDipY + reloadDropY;
    const float gunZ = lerpf(isMinigun ? 0.165f : 0.195f, isMinigun ? 0.095f : 0.0f, ads) + swayZ;
    const float inwardYawDeg = lerpf(3.6f, isMinigun ? 1.8f : 0.0f, ads);

    rlPushMatrix();
    rlTranslatef(gunX, gunY, gunZ);
    rlRotatef(inwardYawDeg, 0.0f, 1.0f, 0.0f);
    rlRotatef(kickUpDeg + reloadTiltDeg, 0.0f, 0.0f, 1.0f);
    if (pl.reloadTimer > 0.0f) {
        rlRotatef(18.0f * std::sin(clampf(pl.reloadTimer / std::max(0.1f, wSpec.reloadDuration), 0.0f, 1.0f) * kPi),
                  1.0f, 0.0f, 0.0f);
    }

    // Helper: mathematically exact 3D forearm-to-hand connector (cylindrical sleeve + cuff + optional wristwatch + sculpted hand!)
    const Color vmCuffCol = Color{
        static_cast<unsigned char>(std::clamp(static_cast<int>(sleeveCol.r) + 22, 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(sleeveCol.g) + 22, 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(sleeveCol.b) + 24, 0, 255)),
        255
    };
    auto drawConnectedArm = [&](Vector3 elbowPos, Vector3 handPos, Vector3 handScale, bool hasWatch) {
        drawModelStyled(sphereModel_, handPos, {0.0f, 1.0f, 0.0f}, 0.0f,
                        handScale, skinCol, 0.64f, 0.0f, 0.0f, MatKind::Standard);
        const float dx = handPos.x - elbowPos.x;
        const float dy = handPos.y - elbowPos.y;
        const float dz = handPos.z - elbowPos.z;
        const float horiz = std::sqrt(dx * dx + dz * dz);
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 0.02f) return;
        const float yawDeg = std::atan2(-dz, dx) * kRadToDeg;
        const float pitchDeg = std::atan2(dy, horiz) * kRadToDeg;
        const float sleeveLen = std::max(0.04f, dist - 0.024f);
        rlPushMatrix();
        rlTranslatef(elbowPos.x, elbowPos.y, elbowPos.z);
        rlRotatef(yawDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(pitchDeg, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {0.026f, sleeveLen, 0.026f}, sleeveCol, 0.56f, 0.08f, 0.0f, MatKind::Standard);
        drawModelStyled(cylModel_, {sleeveLen - 0.014f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {0.0285f, 0.017f, 0.0285f}, vmCuffCol, 0.44f, 0.18f, 0.0f, MatKind::Standard);
        if (hasWatch) {
            drawModelStyled(cylModel_, {sleeveLen + 0.002f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.022f, 0.012f, 0.022f}, Color{44, 48, 56, 255}, 0.25f, 0.85f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {sleeveLen + 0.008f, 0.021f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.006f, 0.015f}, Color{198, 212, 228, 255}, 0.14f, 0.90f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();
    };

    // Right sculpted trigger hand, cuff & cylindrical forearm sleeve!
    drawConnectedArm({-0.165f, -0.135f, 0.068f * (1.0f - ads * 0.45f)},
                     {-0.028f, -0.042f, 0.0f},
                     {0.025f, 0.030f, 0.023f}, false);

    auto drawLeftSupportArm = [&](Vector3 handPos, Vector3 armPos, float yawDeg, float pitchDeg, float len) {
        (void)yawDeg; (void)pitchDeg; (void)len;
        const Vector3 elbowPos{armPos.x - 0.075f, armPos.y - 0.065f, armPos.z - 0.055f};
        drawConnectedArm(elbowPos, handPos, {0.027f, 0.024f, 0.024f}, true);
    };

    float muzzleTipX = 0.19f;
    float barrelY = 0.058f;

    switch (pl.currentWeapon) {
        case WeaponId::Pistol: {
            muzzleTipX = 0.145f;
            barrelY = 0.068f;
            // Grip & polymer lower frame (+ extended basepad when extendedMag is equipped!)
            drawModelStyled(cubeModel_, {-0.025f, -0.038f - (extScale - 1.0f) * 0.028f, 0.0f}, {0.0f, 0.0f, 1.0f}, -14.0f,
                            {0.038f, 0.085f * extScale, 0.030f}, polyDark, 0.55f, 0.15f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.045f, 0.042f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.175f, 0.028f, 0.032f}, (wSkin > 0) ? gunSteel : polyDark,
                            (wSkin > 0) ? wRough : 0.48f, (wSkin > 0) ? wMetal : 0.25f, 0.0f, MatKind::Standard);
            // Reciprocating metallic slide (kicks back on shot!)
            const float slideKick = recoilFrac * 0.032f;
            drawModelStyled(cubeModel_, {0.042f - slideKick, 0.072f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.182f, 0.034f, 0.034f}, (wSkin > 0) ? gunMetal : gunSteel,
                            (wSkin > 0) ? wRough : 0.24f, (wSkin > 0) ? wMetal : 0.82f, wEmis, wMat);
            // Left support hand
            drawModelStyled(cubeModel_, {-0.015f, -0.042f, -0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.048f, 0.052f, 0.036f}, skinCol, 0.68f, 0.0f, 0.0f, MatKind::Standard);
            // 3-Dot Tritium Iron Sights aligned with y = +0.105f!
            drawModelStyled(cubeModel_, {-0.035f - slideKick, 0.098f, -0.010f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.012f, 0.016f, 0.006f}, polyDark, 0.35f, 0.4f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.035f - slideKick, 0.098f,  0.010f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.012f, 0.016f, 0.006f}, polyDark, 0.35f, 0.4f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.122f, 0.100f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.010f, 0.018f, 0.005f}, polyDark, 0.35f, 0.4f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {82, 255, 168, 255}));
            drawModelStyled(sphereModel_, {0.118f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.004f, 0.004f, 0.004f}, dotCol, 0.1f, 0.0f, 2.8f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::HeavyRevolver: {
            muzzleTipX = 0.215f;
            barrelY = 0.070f;
            // Walnut wood grip + brushed chrome/custom frame + revolving cylinder + ventilated rib barrel!
            drawModelStyled(cubeModel_, {-0.028f, -0.042f, 0.0f}, {0.0f, 0.0f, 1.0f}, -18.0f,
                            {0.042f, 0.094f, 0.032f}, Color{112, 62, 34, 255}, 0.52f, 0.10f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.025f, 0.048f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.11f, 0.056f, 0.032f}, chromeBright,
                            (wSkin > 0) ? wRough : 0.14f, (wSkin > 0) ? wMetal : 0.94f, wEmis, wMat);
            drawModelStyled(cylModel_, {0.005f, 0.054f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.026f * (wSlot.extendedMag ? 1.15f : 1.0f), 0.052f, 0.026f * (wSlot.extendedMag ? 1.15f : 1.0f)},
                            (wSkin > 0) ? gunSteel : Color{186, 194, 208, 255}, 0.18f, 0.92f, 0.0f, MatKind::Standard);
            // Long heavy ventilated-rib .44 barrel + full underlug
            drawModelStyled(cubeModel_, {0.140f, 0.072f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.155f, 0.032f, 0.024f}, chromeBright,
                            (wSkin > 0) ? wRough : 0.12f, (wSkin > 0) ? wMetal : 0.95f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.135f, 0.048f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.140f, 0.018f, 0.020f}, gunSteel, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            // Left support hand
            drawModelStyled(cubeModel_, {-0.015f, -0.045f, -0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.048f, 0.052f, 0.036f}, skinCol, 0.68f, 0.0f, 0.0f, MatKind::Standard);
            // Rear notch + glowing target bead aligned at y = +0.105f!
            drawModelStyled(cubeModel_, {-0.022f, 0.098f, -0.009f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.016f, 0.005f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.022f, 0.098f,  0.009f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.016f, 0.005f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {255, 214, 68, 255}));
            drawModelStyled(sphereModel_, {0.205f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0042f, 0.0042f, 0.0042f}, dotCol, 0.1f, 0.0f, 3.0f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::SMG: {
            muzzleTipX = 0.215f;
            barrelY = 0.052f;
            drawModelStyled(cubeModel_, {0.045f, 0.048f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.24f, 0.056f, 0.040f}, gunMetal,
                            (wSkin > 0) ? wRough : 0.34f, (wSkin > 0) ? wMetal : 0.65f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.185f, 0.052f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.075f, 0.022f, 0.022f}, gunSteel, 0.22f, 0.85f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.065f, -0.032f - (extScale - 1.0f) * 0.045f, 0.0f}, {0.0f, 0.0f, 1.0f}, 8.0f,
                            {0.028f, 0.125f * extScale, 0.024f}, polyDark, 0.45f, 0.35f, 0.0f, MatKind::Standard);
            // Left support hand, wristwatch & cylindrical sleeve under shroud
            drawLeftSupportArm({0.115f, 0.018f, -0.014f}, {0.015f, -0.055f, -0.075f}, 30.0f, 26.0f, 0.18f);
            // Open panoramic Reflex Sight centered at y = +0.105f!
            drawModelStyled(cubeModel_, {0.025f, 0.082f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.052f, 0.012f, 0.036f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.025f, 0.105f, -0.017f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.034f, 0.036f, 0.004f}, polyDark, 0.30f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.025f, 0.105f,  0.017f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.034f, 0.036f, 0.004f}, polyDark, 0.30f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.025f, 0.124f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.034f, 0.004f, 0.038f}, polyDark, 0.30f, 0.6f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {72, 238, 255, 255}));
            drawModelStyled(sphereModel_, {0.025f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0032f, 0.0032f, 0.0032f}, dotCol, 0.1f, 0.0f, 3.2f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::Shotgun: {
            muzzleTipX = 0.255f;
            barrelY = 0.072f;
            drawModelStyled(cubeModel_, {0.035f, 0.056f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.16f, 0.052f, 0.040f}, gunMetal,
                            (wSkin > 0) ? wRough : 0.32f, (wSkin > 0) ? wMetal : 0.72f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.175f, 0.072f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.17f, 0.022f, 0.022f}, gunSteel, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.165f + (extScale - 1.0f) * 0.03f, 0.048f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.15f * extScale, 0.020f, 0.020f}, gunMetal, 0.35f, 0.70f, 0.0f, MatKind::Standard);
            // Tactical ribbed pump forend + sculpted left support arm & wristwatch
            const float pumpSlide = (recoilFrac > 0.25f) ? (-0.035f * std::sin(recoilFrac * kPi)) : 0.0f;
            drawModelStyled(cubeModel_, {0.145f + pumpSlide, 0.042f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.095f, 0.034f, 0.036f}, Color{98, 62, 36, 255}, 0.62f, 0.08f, 0.0f, MatKind::Standard);
            drawLeftSupportArm({0.140f + pumpSlide, 0.024f, -0.012f}, {0.035f + pumpSlide * 0.5f, -0.050f, -0.078f}, 28.0f, 24.0f, 0.19f);
            // Ghost-ring rear ears + fiber-optic front bead at y = +0.105f
            drawModelStyled(cubeModel_, {-0.015f, 0.098f, -0.011f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.012f, 0.018f, 0.005f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.015f, 0.098f,  0.011f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.012f, 0.018f, 0.005f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {255, 72, 54, 255}));
            drawModelStyled(sphereModel_, {0.242f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0045f, 0.0045f, 0.0045f}, dotCol, 0.1f, 0.0f, 3.0f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::AutoShotgun: {
            muzzleTipX = 0.258f;
            barrelY = 0.062f;
            // Heavy tactical automatic shotgun receiver + ventilated shroud + drum magazine!
            drawModelStyled(cubeModel_, {0.055f, 0.054f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, 0.064f, 0.048f}, gunMetal,
                            (wSkin > 0) ? wRough : 0.34f, (wSkin > 0) ? wMetal : 0.72f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.185f, 0.056f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.11f, 0.048f, 0.042f}, (wSkin > 0) ? gunSteel : polyDark, 0.45f, 0.45f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.235f, 0.062f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.015f, 0.052f, 0.015f}, gunSteel, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            // Round drum magazine underneath receiver
            drawModelStyled(cylModel_, {0.068f, -0.028f, -0.036f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                            {0.062f * extScale, 0.072f * extScale, 0.062f * extScale}, Color{28, 32, 40, 255}, 0.42f, 0.55f, 0.0f, MatKind::Standard);
            // Sculpted left foregrip hand, wristwatch & cylindrical sleeve
            drawLeftSupportArm({0.155f, 0.020f, -0.012f}, {0.040f, -0.052f, -0.078f}, 28.0f, 24.0f, 0.19f);
            // Wide CQB Reflex Hood centered at y = +0.105f!
            drawModelStyled(cubeModel_, {0.035f, 0.105f, -0.020f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.042f, 0.036f, 0.0045f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.035f, 0.105f,  0.020f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.042f, 0.036f, 0.0045f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.035f, 0.124f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.042f, 0.0045f, 0.044f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {112, 255, 96, 255}));
            drawModelStyled(sphereModel_, {0.035f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0036f, 0.0036f, 0.0036f}, dotCol, 0.1f, 0.0f, 3.2f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::SniperRifle: {
            muzzleTipX = 0.325f;
            barrelY = 0.058f;
            // Precision Marksman Rifle chassis + fluted long barrel + telescopic scope!
            drawModelStyled(cubeModel_, {0.065f, 0.050f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.26f, 0.052f, 0.038f}, (wSkin > 0) ? gunMetal : Color{44, 52, 46, 255},
                            (wSkin > 0) ? wRough : 0.42f, (wSkin > 0) ? wMetal : 0.48f, wEmis, wMat);
            drawModelStyled(cylModel_, {0.185f, 0.058f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.011f, 0.155f, 0.011f}, gunSteel, 0.20f, 0.90f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.330f, 0.058f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.032f, 0.022f, 0.028f}, polyDark, 0.35f, 0.75f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.055f, -0.018f - (extScale - 1.0f) * 0.028f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.054f, 0.075f * extScale, 0.028f}, polyDark, 0.45f, 0.40f, 0.0f, MatKind::Standard);
            // Dual-ring telescopic sniper scope centered at y = +0.105f
            drawModelStyled(cylModel_, {-0.035f, 0.105f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.019f, 0.175f, 0.019f}, polyDark, 0.28f, 0.72f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.125f, 0.105f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.024f, 0.038f, 0.024f}, polyDark, 0.28f, 0.72f, 0.0f, MatKind::Standard);
            const Color lensCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {48, 178, 232, 255}));
            drawModelStyled(sphereModel_, {-0.035f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.006f, 0.017f, 0.017f}, lensCol, 0.06f, 0.85f, 0.6f, MatKind::CarGlass);
            // Sculpted left support hand & sleeve
            drawLeftSupportArm({0.148f, 0.024f, -0.012f}, {0.042f, -0.050f, -0.076f}, 28.0f, 24.0f, 0.19f);
            break;
        }
        case WeaponId::Minigun: {
            muzzleTipX = 0.315f;
            barrelY = 0.035f;
            // Heavy Rotary Electric Gatling Motor Housing + Top Spade Handle + Live Spinning 6-Barrel Cluster!
            drawModelStyled(cylModel_, {-0.035f, 0.035f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.052f, 0.14f, 0.052f}, gunMetal,
                            (wSkin > 0) ? wRough : 0.32f, (wSkin > 0) ? wMetal : 0.78f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.025f, -0.028f, 0.032f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.095f * extScale, 0.088f * extScale, 0.065f}, Color{46, 56, 44, 255}, 0.48f, 0.35f, 0.0f, MatKind::Standard);
            // Top chainsaw-style crossbar support handle + left hand gripping from above
            drawModelStyled(cubeModel_, {0.075f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.022f, 0.022f, 0.105f}, polyDark, 0.38f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.075f, 0.072f, -0.046f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.022f, 0.068f, 0.018f}, polyDark, 0.38f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.075f, 0.072f,  0.046f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.022f, 0.068f, 0.018f}, polyDark, 0.38f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.075f, 0.112f, -0.018f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.048f, 0.042f, 0.048f}, skinCol, 0.68f, 0.0f, 0.0f, MatKind::Standard);

            // Live Rotating 6-Barrel Cluster + Twin Rotor Clamp Discs!
            rlPushMatrix();
            rlTranslatef(0.095f, 0.035f, 0.0f);
            rlRotatef(pl.minigunSpin * kRadToDeg, 1.0f, 0.0f, 0.0f);
            for (int b = 0; b < 6; ++b) {
                const float ang = static_cast<float>(b) * (kTwoPi / 6.0f);
                drawModelStyled(cylModel_, {0.0f, std::cos(ang) * 0.026f, std::sin(ang) * 0.026f},
                                {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.0075f, 0.215f, 0.0075f}, (wSkin > 0) ? gunSteel : chromeBright,
                                (wSkin > 0) ? wRough : 0.18f, (wSkin > 0) ? wMetal : 0.92f, 0.0f, MatKind::Standard);
            }
            drawModelStyled(cylModel_, {0.095f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.038f, 0.016f, 0.038f}, polyDark, 0.32f, 0.80f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.185f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.038f, 0.020f, 0.038f}, polyDark, 0.32f, 0.80f, 0.0f, MatKind::Standard);
            rlPopMatrix();
            break;
        }
        case WeaponId::HeavyLauncher: {
            muzzleTipX = 0.255f;
            barrelY = 0.056f;
            drawModelStyled(cubeModel_, {0.085f, 0.056f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.31f, 0.068f, 0.068f}, (wSkin > 0) ? gunMetal : Color{52, 60, 50, 255},
                            (wSkin > 0) ? wRough : 0.48f, (wSkin > 0) ? wMetal : 0.45f, wEmis, wMat);
            drawModelStyled(sphereModel_, {0.045f, 0.036f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.105f * (wSlot.extendedMag ? 1.18f : 1.0f), 0.105f, 0.105f * (wSlot.extendedMag ? 1.18f : 1.0f)},
                            (wSkin > 0) ? gunSteel : gunMetal, 0.38f, 0.62f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.165f, -0.005f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.032f, 0.065f, 0.030f}, polyDark, 0.5f, 0.25f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.160f, -0.015f, -0.012f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.048f, 0.048f, 0.044f}, skinCol, 0.68f, 0.0f, 0.0f, MatKind::Standard);
            // Quadrant sight frame centered at y = +0.105f
            drawModelStyled(cubeModel_, {0.095f, 0.105f, -0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.036f, 0.004f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.095f, 0.105f,  0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.036f, 0.004f}, polyDark, 0.35f, 0.5f, 0.0f, MatKind::Standard);
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, {255, 176, 52, 255}));
            drawModelStyled(sphereModel_, {0.095f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0038f, 0.0038f, 0.0038f}, dotCol, 0.1f, 0.0f, 3.0f, MatKind::EmissiveGlow);
            break;
        }
        case WeaponId::CombatMG:
        case WeaponId::AssaultRifle:
        default: {
            const bool isMg = (pl.currentWeapon == WeaponId::CombatMG);
            muzzleTipX = 0.258f;
            barrelY = 0.056f;
            // Upper & lower receiver + rail handguard + barrel
            drawModelStyled(cubeModel_, {0.055f, 0.050f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.25f, isMg ? 0.064f : 0.054f, isMg ? 0.048f : 0.040f},
                            gunMetal, (wSkin > 0) ? wRough : 0.34f, (wSkin > 0) ? wMetal : 0.68f, wEmis, wMat);
            drawModelStyled(cubeModel_, {0.165f, 0.052f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.12f, 0.044f, 0.038f}, (wSkin > 0) ? gunSteel : polyDark,
                            (wSkin > 0) ? wRough : 0.45f, (wSkin > 0) ? wMetal : 0.45f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.228f, 0.056f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.075f, 0.020f, 0.020f}, gunSteel, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            // Magazine (box mag on CombatMG, curved STANAG mag on Carbine)
            if (isMg) {
                drawModelStyled(cubeModel_, {0.065f, -0.028f - (extScale - 1.0f) * 0.032f, 0.008f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.078f * extScale, 0.095f * extScale, 0.068f}, Color{46, 66, 48, 255}, 0.52f, 0.25f, 0.0f, MatKind::Standard);
            } else {
                drawModelStyled(cubeModel_, {0.068f, -0.028f - (extScale - 1.0f) * 0.038f, 0.0f}, {0.0f, 0.0f, 1.0f}, 14.0f,
                                {0.042f, 0.115f * extScale, 0.026f}, polyDark, 0.46f, 0.35f, 0.0f, MatKind::Standard);
            }
            // Left sculpted handguard support hand, wristwatch & cylindrical sleeve
            drawLeftSupportArm({0.148f, 0.024f, -0.012f}, {0.040f, -0.050f, -0.076f}, 28.0f, 24.0f, 0.19f);

            // Tactical Holographic / Red-Dot Optic Hood aligned dead-center at y = +0.105f!
            drawModelStyled(cubeModel_, {0.032f, 0.083f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.064f, 0.012f, 0.040f}, polyDark, 0.35f, 0.55f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.032f, 0.105f, -0.019f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.046f, 0.038f, 0.0045f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.032f, 0.105f,  0.019f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.046f, 0.038f, 0.0045f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.032f, 0.125f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.046f, 0.0045f, 0.042f}, polyDark, 0.32f, 0.6f, 0.0f, MatKind::Standard);
            // Subtle anti-reflective tinted holographic optic glass lens
            DrawCube({0.036f, 0.105f, 0.0f}, 0.003f, 0.033f, 0.033f, Color{72, 210, 238, 34});
            const ColorRGBA defaultDot = isMg ? ColorRGBA{92, 255, 138, 255} : ColorRGBA{255, 62, 68, 255};
            const Color dotCol = toRayColor(getOpticReticleColorRGBA(wSlot.opticColorIdx, defaultDot));
            drawModelStyled(sphereModel_, {0.032f, 0.105f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.0032f, 0.0032f, 0.0032f}, dotCol, 0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
            break;
        }
    }

    // 1st-Person Machined Firearm Micro-Details (Picatinny Rails, M-LOK Slots, Trigger Guard, Bolt, Charging Handle & Muzzle Brake!)
    const bool hasRailSystem = (pl.currentWeapon == WeaponId::SMG ||
                                pl.currentWeapon == WeaponId::AssaultRifle ||
                                pl.currentWeapon == WeaponId::CombatMG ||
                                pl.currentWeapon == WeaponId::AutoShotgun ||
                                pl.currentWeapon == WeaponId::SniperRifle);
    if (hasRailSystem) {
        const float railY = (pl.currentWeapon == WeaponId::CombatMG || pl.currentWeapon == WeaponId::AutoShotgun) ? 0.084f : 0.077f;
        // Continuous Picatinny top rail spine + 8 crisp machined transverse cross-ribs
        drawModelStyled(cubeModel_, {0.145f, railY - 0.003f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.142f, 0.006f, 0.022f}, polyDark, 0.36f, 0.62f, 0.0f, MatKind::Standard);
        for (int r = 0; r < 8; ++r) {
            drawModelStyled(cubeModel_, {0.082f + static_cast<float>(r) * 0.017f, railY + 0.002f, 0.0f},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.0085f, 0.0065f, 0.028f},
                            Color{52, 58, 68, 255}, 0.28f, 0.78f, 0.0f, MatKind::Standard);
        }
        // Machined M-LOK handguard ventilation slots along both sides!
        const float slotZ = (pl.currentWeapon == WeaponId::CombatMG || pl.currentWeapon == WeaponId::AutoShotgun) ? 0.024f : 0.020f;
        for (int s = -1; s <= 1; s += 2) {
            for (int v = 0; v < 3; ++v) {
                drawModelStyled(cubeModel_, {0.125f + static_cast<float>(v) * 0.032f, 0.053f, static_cast<float>(s) * slotZ},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.020f, 0.010f, 0.0035f},
                                Color{14, 16, 20, 255}, 0.70f, 0.20f, 0.0f, MatKind::Standard);
            }
        }
        // Subtle tinted reflex glass for SMG & AutoShotgun optics too
        if (pl.currentWeapon == WeaponId::SMG || pl.currentWeapon == WeaponId::AutoShotgun) {
            const float ox = (pl.currentWeapon == WeaponId::SMG) ? 0.028f : 0.038f;
            DrawCube({ox, 0.105f, 0.0f}, 0.003f, 0.032f, 0.032f, Color{72, 210, 238, 34});
        }
    }
    if (pl.currentWeapon != WeaponId::Minigun && pl.currentWeapon != WeaponId::HeavyLauncher) {
        // Trigger guard loop + curved metallic trigger blade
        drawModelStyled(cubeModel_, {0.012f, 0.002f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.044f, 0.006f, 0.014f}, polyDark, 0.42f, 0.45f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.032f, 0.016f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.006f, 0.026f, 0.014f}, polyDark, 0.42f, 0.45f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.006f, 0.018f, 0.0f}, {0.0f, 0.0f, 1.0f}, 24.0f,
                        {0.005f, 0.022f, 0.006f}, gunSteel, 0.22f, 0.90f, 0.0f, MatKind::Standard);
    }
    if (hasRailSystem || pl.currentWeapon == WeaponId::Shotgun) {
        // Right-side ejection port recess, reciprocating silver bolt carrier & left knurled charging handle!
        const float portZ = (pl.currentWeapon == WeaponId::CombatMG || pl.currentWeapon == WeaponId::AutoShotgun) ? 0.0245f : 0.0205f;
        const float boltKick = recoilFrac * 0.024f;
        drawModelStyled(cubeModel_, {0.072f, 0.060f, portZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.048f, 0.018f, 0.004f}, Color{10, 12, 16, 255}, 0.70f, 0.20f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.076f - boltKick, 0.060f, portZ + 0.001f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.036f, 0.014f, 0.004f}, Color{188, 196, 210, 255}, 0.18f, 0.92f, 0.0f, MatKind::Standard);
        drawModelStyled(cylModel_, {0.045f - boltKick, 0.062f, -portZ - 0.012f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                        {0.0055f, 0.016f, 0.0055f}, gunSteel, 0.25f, 0.85f, 0.0f, MatKind::Standard);
    }

    // 1st-Person 3D Tactical Barrel Suppressor (or Slotted Muzzle Compensator Brake when unsuppressed!)
    if (wSlot.suppressor && weaponSupportsSuppressor(pl.currentWeapon)) {
        drawModelStyled(cylModel_, {muzzleTipX - 0.008f, barrelY, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {0.016f, 0.094f, 0.016f}, Color{26, 30, 38, 255}, 0.32f, 0.76f, 0.0f, MatKind::Standard);
        drawModelStyled(cylModel_, {muzzleTipX + 0.068f, barrelY, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {0.0175f, 0.016f, 0.0175f}, gunSteel, 0.24f, 0.88f, 0.0f, MatKind::Standard);
    } else if (hasRailSystem && pl.currentWeapon != WeaponId::SniperRifle) {
        // Machined 2-chamber slotted muzzle compensator brake!
        drawModelStyled(cubeModel_, {muzzleTipX + 0.006f, barrelY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.032f, 0.022f, 0.024f}, polyDark, 0.30f, 0.78f, 0.0f, MatKind::Standard);
        for (int sl = 0; sl < 2; ++sl) {
            drawModelStyled(cubeModel_, {muzzleTipX + static_cast<float>(sl) * 0.012f, barrelY, 0.0f},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {0.005f, 0.014f, 0.026f},
                            Color{10, 12, 16, 255}, 0.70f, 0.20f, 0.0f, MatKind::Standard);
        }
    }

    // 1st-Person Tactical Side/Underbarrel Laser Module!
    if (wSlot.laserSightColor > 0) {
        Color diodeCol = toRayColor(getLaserSightColorRGBA(wSlot.laserSightColor));
        diodeCol.a = 255;
        drawModelStyled(cubeModel_, {muzzleTipX - 0.062f, barrelY - 0.016f, 0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.048f, 0.018f, 0.016f}, polyDark, 0.36f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(sphereModel_, {muzzleTipX - 0.036f, barrelY - 0.016f, 0.022f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.0045f, 0.0045f, 0.0045f}, diodeCol, 0.08f, 0.0f, 3.6f, MatKind::EmissiveGlow);
    }

    // 3D Emissive Muzzle Flash Star at the barrel tip!
    if (pl.muzzleFlashTimer > 0.0f) {
        const float flashY = isMinigun ? 0.035f : barrelY;
        const float flashX = muzzleTipX + ((wSlot.suppressor && weaponSupportsSuppressor(pl.currentWeapon)) ? 0.095f : 0.02f);
        drawModelStyled(cubeModel_, {flashX, flashY, 0.0f}, {1.0f, 0.0f, 0.0f}, 45.0f,
                        {0.038f, 0.078f, 0.014f}, Color{255, 224, 112, 255}, 0.1f, 0.0f, 3.8f, MatKind::EmissiveGlow);
        drawModelStyled(cubeModel_, {flashX, flashY, 0.0f}, {1.0f, 0.0f, 0.0f}, -45.0f,
                        {0.038f, 0.014f, 0.078f}, Color{255, 182, 72, 255}, 0.1f, 0.0f, 3.6f, MatKind::EmissiveGlow);
    }

    rlPopMatrix();
    rlPopMatrix();

    // World-space 1st-Person Laser Sight Beam + Pinpoint Impact Dot (perspective-aligned to viewmodel diode!)
    if (wSlot.laserSightColor > 0 && pl.reloadTimer <= 0.0f) {
        const Vec3 camRight{-std::sin(viewYaw), 0.0f, std::cos(viewYaw)};
        const Vec3 camUp{
            -std::cos(viewYaw) * std::sin(viewPitch),
             std::cos(viewPitch),
            -std::sin(viewYaw) * std::sin(viewPitch)
        };
        const float lx = (gunX + muzzleTipX - 0.036f) * kVmScale;
        const float ly = (gunY + barrelY - 0.016f) * kVmScale;
        const float lz = (gunZ + 0.022f) * kVmScale;
        const Vec3 origin = camPos + camFwd * lx + camUp * ly + camRight * lz;
        Color beamCol = toRayColor(getLaserSightColorRGBA(wSlot.laserSightColor));
        Color dotCol = beamCol;
        dotCol.a = 255;
        DrawLine3D({origin.x, origin.y, origin.z},
                   {pl.crosshairPoint3D.x, pl.crosshairPoint3D.y, pl.crosshairPoint3D.z},
                   beamCol);
        drawModelStyled(sphereModel_, {pl.crosshairPoint3D.x, pl.crosshairPoint3D.y, pl.crosshairPoint3D.z},
                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.040f, 0.040f, 0.040f},
                        dotCol, 0.08f, 0.0f, 3.8f, MatKind::EmissiveGlow);
    }
}

} // namespace oc
