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

#if defined(PLATFORM_ANDROID) || defined(__ANDROID__) || defined(GRAPHICS_API_OPENGL_ES3)
#define OC_IS_GLES3 1
#define OC_GLSL_VERT_HEADER "#version 300 es\nprecision highp float;\n"
#define OC_GLSL_FRAG_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
constexpr int kShadowMapResolution = 1024;
#else
#define OC_IS_GLES3 0
#define OC_GLSL_VERT_HEADER "#version 330\n"
#define OC_GLSL_FRAG_HEADER "#version 330\n"
constexpr int kShadowMapResolution = 2048;
#endif
constexpr int kShadowTextureSlot = 6;

// ---------------------------------------------------------------------------
// 1) Fast Depth-Only Shader for Directional Sun/Moon Shadow Map
// ---------------------------------------------------------------------------
const char* kDepthVertShader = OC_GLSL_VERT_HEADER R"GLSL(
in vec3 vertexPosition;
uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kDepthFragShader = OC_GLSL_FRAG_HEADER R"GLSL(
out vec4 finalColor;

void main() {
    finalColor = vec4(1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// 2) Atmospheric Sky Dome Shader (Rayleigh/Mie Sky, Sun Corona, FBM Clouds, Stars)
// ---------------------------------------------------------------------------
const char* kSkyVertShader = OC_GLSL_VERT_HEADER R"GLSL(
in vec3 vertexPosition;
uniform mat4 mvp;

out vec3 vLocalDir;

void main() {
    vLocalDir = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kSkyFragShader = OC_GLSL_FRAG_HEADER R"GLSL(
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

    // Physically grounded atmospheric palette (deep coastal sky + realistic overcast rain)
    vec3 dayZenith   = vec3(0.036, 0.142, 0.455);
    vec3 dayHorizon  = vec3(0.320, 0.505, 0.720);
    vec3 goldZenith  = vec3(0.058, 0.086, 0.255);
    vec3 goldHorizon = vec3(0.880, 0.300, 0.115);
    vec3 nightZenith = vec3(0.004, 0.008, 0.024);
    vec3 nightHoriz  = vec3(0.014, 0.026, 0.062);

    vec3 zenithCol = mix(nightZenith, dayZenith, dayFactor);
    zenithCol = mix(zenithCol, goldZenith, golden * 0.72);

    vec3 horizCol = mix(nightHoriz, dayHorizon, dayFactor);
    horizCol = mix(horizCol, goldHorizon, golden * 0.88);

    if (wetWeather > 0.01) {
        vec3 stormZenith = mix(vec3(0.008, 0.013, 0.026), vec3(0.072, 0.090, 0.118), dayFactor);
        vec3 stormHoriz  = mix(vec3(0.022, 0.032, 0.056), vec3(0.178, 0.205, 0.242), dayFactor);
        zenithCol = mix(zenithCol, stormZenith, wetWeather * 0.82);
        horizCol  = mix(horizCol, stormHoriz, wetWeather * 0.76);
    }

    float skyGrad = pow(1.0 - upPos, 2.45);
    vec3 sky = mix(zenithCol, horizCol, skyGrad);

    // Warm directional Mie scattering around the sun / sunset glow
    float sunCos = max(dot(dir, S), 0.0);
    vec3 mieTint = mix(vec3(1.0, 0.88, 0.68), vec3(1.0, 0.44, 0.16), golden);
    if (night > 0.55) {
        mieTint = vec3(0.24, 0.38, 0.66);
    }
    sky += mieTint * pow(sunCos, 6.0) * (0.22 + 0.42 * golden) * (1.0 - night * 0.65) * (1.0 - wetWeather * 0.75);
    sky += mieTint * pow(sunCos, 46.0) * (0.38 + 0.32 * golden) * (1.0 - night * 0.55) * (1.0 - wetWeather * 0.82);

    // Crisp Solar / Lunar Disc
    if (dir.y > -0.02) {
        float sunDisc = smoothstep(0.99920, 0.99975, dot(dir, S));
        vec3 discCol = (night > 0.6) ? vec3(1.7, 2.0, 2.5) : mix(vec3(5.8, 5.1, 3.9), vec3(6.4, 2.6, 0.9), golden);
        sky += discCol * sunDisc * (1.0 - wetWeather * 0.88);
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
            float star = (1.0 - smoothstep(0.02, 0.22, d)) * twinkle * night * smoothstep(0.04, 0.28, dir.y);
            sky += vec3(0.75, 0.86, 1.0) * star * 1.4 * (1.0 - wetWeather * 0.85);
        }
    }

    // Volumetric-style 2-layer drifting FBM cumulus / storm overcast clouds
    if (dir.y > 0.015) {
        vec2 cloudUv = dir.xz / (dir.y + 0.19) * 1.35;
        vec2 wind = vec2(animTime * 0.014, animTime * 0.007);
        float n1 = fbmClouds(cloudUv + wind);
        float n2 = fbmClouds(cloudUv * 2.15 - wind * 1.4 + vec2(4.2, -2.8));
        float lowThresh = mix(0.50, 0.22, wetWeather);
        float density = smoothstep(lowThresh, mix(0.80, 0.66, wetWeather), n1 * 0.72 + n2 * 0.28);
        float horizonFade = smoothstep(0.015, 0.20, dir.y);
        density *= horizonFade;

        // Cloud self-shadowing, silver lining & moody storm underbelly
        float detailLight = clamp(0.44 + 0.54 * n2 + 0.30 * pow(sunCos, 3.0) * (1.0 - wetWeather * 0.75), 0.0, 1.25);
        vec3 cloudLit = mix(vec3(0.92, 0.94, 0.98), vec3(1.12, 0.54, 0.28), golden * 0.85);
        vec3 cloudShade = mix(vec3(0.28, 0.36, 0.50), vec3(0.24, 0.15, 0.28), golden * 0.80);
        if (wetWeather > 0.01) {
            cloudLit = mix(cloudLit, vec3(0.32, 0.36, 0.42), wetWeather * 0.78);
            cloudShade = mix(cloudShade, vec3(0.10, 0.12, 0.16), wetWeather * 0.82);
        }
        if (night > 0.4) {
            cloudLit = mix(cloudLit, vec3(0.07, 0.10, 0.16), night);
            cloudShade = mix(cloudShade, vec3(0.02, 0.03, 0.05), night);
        }
        vec3 cloudCol = mix(cloudShade, cloudLit, clamp(detailLight, 0.0, 1.0));
        sky = mix(sky, cloudCol, density * (0.80 + 0.16 * wetWeather));
    }

    // Distant coastal mountain range silhouette along the bay horizon
    float azimuth = atan(dir.z, dir.x);
    float mtnHeight = 0.016 + 0.018 * sin(azimuth * 3.0 + 0.7)
                    + 0.010 * sin(azimuth * 7.0 - 1.4)
                    + 0.005 * sin(azimuth * 17.0);
    if (dir.y > -0.04 && dir.y < mtnHeight) {
        float ridge = 1.0 - smoothstep(mtnHeight - 0.004, mtnHeight, dir.y);
        vec3 mtnCol = mix(horizCol * 0.44, zenithCol * 0.55, 0.35);
        sky = mix(sky, mtnCol, ridge * 0.78);
    }

    // Below horizon fallback matches ocean haze
    if (dir.y < 0.0) {
        sky = mix(sky, horizCol * 0.46, clamp(-dir.y * 6.0, 0.0, 1.0));
    }

    vec3 mapped = pow(acesTonemap(sky * 1.02), vec3(1.0 / 2.2));
    finalColor = vec4(mapped, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// 3) Unified Cook-Torrance GGX PBR + 16-Tap Poisson Soft Shadow +
//    3D Raycasted Interior Mapping + Organic Wet-Road Puddle & Material Shader
// ---------------------------------------------------------------------------
const char* kWorldVertShader = OC_GLSL_VERT_HEADER R"GLSL(
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

const char* kWorldFragShader = OC_GLSL_FRAG_HEADER R"GLSL(
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

// Rotated 2-octave fBM eliminates visible axis-aligned square noise cells on puddles & lawns
float fbm2(vec2 p) {
    mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    float v = valueNoise(p) * 0.62;
    p = rot * p * 2.13 + vec2(17.3, 9.1);
    v += valueNoise(p) * 0.38;
    return v;
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
    if (ndotl <= 0.025) return 0.0;

    // Tight slope-scaled normal offset prevents shadow acne while eliminating tire/foot Peter-Panning
    float normalOffset = mix(0.030, 0.135, 1.0 - ndotl);
    vec4 lightClip = lightVP * vec4(worldPos + N * normalOffset, 1.0);
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.0001);
    proj = proj * 0.5 + 0.5;

    if (proj.x <= 0.008 || proj.x >= 0.992 || proj.y <= 0.008 || proj.y >= 0.992 || proj.z >= 0.998 || proj.z <= 0.0) {
        return 0.0;
    }

    vec2 edgeDist = min(proj.xy, 1.0 - proj.xy);
    float edgeFade = smoothstep(0.008, 0.085, min(edgeDist.x, edgeDist.y));

    float bias = max(0.00140 * (1.0 - ndotl), 0.00030);

    // Screen-space Interleaved Gradient Noise eliminates blocky world-space dithering patterns
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float rotAng = ign * 6.2831853;
    float cs = cos(rotAng);
    float sn = sin(rotAng);
    mat2 rot = mat2(cs, -sn, sn, cs);

    // Contact-hardening penumbra (crisp under tires/feet, softer from tall towers)
    float centerDepth = texture(shadowMap, proj.xy).r;
    float blockerGap = clamp((proj.z - bias - centerDepth) * 18.0, 0.0, 1.0);
    float filterRadius = shadowTexelSize * mix(1.05, 2.35, blockerGap);

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

// Physically grounded 360-degree Environment Reflection Map:
// Dark asphalt ground hemisphere (R.y < 0) + crisp horizon reflection band + city skyline + sky dome (R.y > 0)
vec3 sampleCityEnvMap(vec3 R, vec3 worldPos, float rough, float nightF) {
    vec3 horizLin = pow(clamp(horizonColor, 0.0, 1.0), vec3(2.2));
    vec3 skyUp = mix(horizLin * 0.92, skyAmbient * 1.92, pow(clamp(R.y, 0.0, 1.0), 0.65));
    // Dark grounded asphalt / earth bounce below horizon gives curved car fenders photographic depth!
    vec3 darkGround = groundAmbient * mix(0.46, 0.16, clamp(-R.y * 2.4, 0.0, 1.0));
    float horizBlend = smoothstep(-0.045 - rough * 0.22, 0.055 + rough * 0.22, R.y);
    vec3 envCol = mix(darkGround, skyUp, horizBlend);

    // Subtle crisp atmospheric glow right along the horizon reflection crease
    float horizonCrease = exp(-abs(R.y - 0.012) * mix(16.0, 5.5, rough));
    envCol += horizLin * horizonCrease * mix(0.28, 0.06, rough) * (1.0 - nightF * 0.45);

    // Attenuate synthetic downtown skyline whenever the reflection ray points out toward open ocean
    vec2 horizDir = normalize(R.xz + vec2(0.0001));
    vec2 farPos = worldPos.xz + horizDir * 120.0;
    float cityMask = 1.0 - smoothstep(620.0, 780.0, max(abs(farPos.x), abs(farPos.y)));
    if (cityMask <= 0.01) {
        return envCol;
    }

    // Softened architectural skyline silhouette & illuminated windows near the horizon
    float az = atan(R.z, R.x);
    float bldColIdx = floor((az + kPi) / (2.0 * kPi) * 48.0);
    float bldRnd = hash12(vec2(bldColIdx, 3.7));
    float skylineH = (0.03 + 0.19 * pow(bldRnd, 1.5)) * (1.0 - rough * 0.60);

    if (R.y > -0.02 && R.y < skylineH) {
        vec3 bldSil = mix(groundAmbient * 0.36, skyAmbient * 0.28, 0.42);
        // Subtle warm city horizon glow bounce in wet puddles & clearcoat at night
        bldSil += vec3(0.24, 0.17, 0.10) * nightF * (1.0 - clamp(R.y / max(skylineH, 0.01), 0.0, 1.0));
        float uFreq = (az + kPi) * 28.0;
        float vFreq = R.y * 22.0;
        float detailFade = 1.0 - smoothstep(0.16, 0.66, max(fwidth(uFreq), fwidth(vFreq)) + rough * 1.55);
        if (detailFade > 0.02) {
            float winU = fract(uFreq);
            float winV = fract(vFreq);
            float softEdge = clamp(0.24 + rough * 0.65, 0.24, 0.42);
            float paneMask = smoothstep(0.14, 0.14 + softEdge, winU) * (1.0 - smoothstep(0.86 - softEdge, 0.86, winU)) *
                             smoothstep(0.12, 0.12 + softEdge, winV) * (1.0 - smoothstep(0.88 - softEdge, 0.88, winV));
            float winHash = hash12(vec2(bldColIdx * 3.1 + floor(winU * 4.0), floor(vFreq)));
            if (winHash > (0.64 - nightF * 0.28)) {
                vec3 winPalette = (winHash > 0.88)
                                      ? vec3(0.46, 0.74, 0.98)
                                      : ((winHash > 0.76) ? vec3(0.96, 0.72, 0.44) : vec3(1.06, 0.82, 0.52));
                bldSil += winPalette * (0.016 + nightF * 0.62) * paneMask * detailFade;
            }
        }
        float mask = (1.0 - smoothstep(skylineH - 0.030, skylineH, R.y)) * smoothstep(-0.02, 0.025, R.y);
        envCol = mix(envCol, bldSil, mask * 0.64 * cityMask);
    }
    return envCol;
}

// True 3D Raycasted Interior Mapping inside building windows (virtual 3D rooms with ceiling lights, walls & blinds)
vec3 sampleInteriorRoom3D(vec2 paneUv, vec3 V, vec3 N, float roomSeed, bool isLobby, float nightF) {
    vec3 T = (abs(N.x) > 0.5) ? vec3(0.0, 0.0, sign(N.x)) : vec3(-sign(N.z), 0.0, 0.0);
    vec3 B = vec3(0.0, 1.0, 0.0);

    vec3 I = normalize(-V);
    float rx = dot(I, T);
    float ry = dot(I, B);
    float rz = max(dot(I, -N), 0.06);

    float roomDepth = isLobby ? 1.15 : 0.78;
    float tx = (rx > 0.0) ? ((1.0 - paneUv.x) / max(rx, 0.001)) : ((-paneUv.x) / min(rx, -0.001));
    float ty = (ry > 0.0) ? ((1.0 - paneUv.y) / max(ry, 0.001)) : ((-paneUv.y) / min(ry, -0.001));
    float tz = roomDepth / rz;
    float tHit = min(min(tx, ty), tz);

    vec3 hitPos = vec3(paneUv, 0.0) + vec3(rx, ry, rz) * tHit;
    float depthFade = 1.0 - clamp(hitPos.z / (roomDepth * 1.15), 0.0, 0.75);

    vec3 warmTone = pow(vec3(0.96, 0.78, 0.52), vec3(2.2));
    vec3 coolTone = pow(vec3(0.62, 0.80, 0.94), vec3(2.2));
    vec3 corpTone = pow(vec3(0.90, 0.88, 0.82), vec3(2.2));
    vec3 suiteTint = (roomSeed > 0.76) ? coolTone : ((roomSeed > 0.42) ? corpTone : warmTone);

    vec3 surfaceCol = suiteTint * 0.36;
    if (abs(tHit - ty) < 0.002) {
        if (ry > 0.0) {
            float stripX = smoothstep(0.18, 0.24, abs(fract(hitPos.x * 2.0) - 0.5));
            float stripZ = smoothstep(0.15, 0.28, abs(fract(hitPos.z * 2.2) - 0.5));
            float lightPanel = (1.0 - stripX) * (1.0 - stripZ);
            surfaceCol = mix(suiteTint * 0.48, suiteTint * 2.15, lightPanel);
        } else {
            float tile = 0.85 + 0.15 * sin(hitPos.x * 18.0) * sin(hitPos.z * 18.0);
            surfaceCol = suiteTint * 0.22 * tile;
        }
    } else if (abs(tHit - tz) < 0.002) {
        float shelf = smoothstep(0.35, 0.45, abs(sin(hitPos.y * 12.0)));
        bool monitorGlow = (hitPos.x > 0.34 && hitPos.x < 0.66 && hitPos.y > 0.30 && hitPos.y < 0.58);
        surfaceCol = suiteTint * (0.42 + 0.16 * shelf);
        if (monitorGlow && fract(roomSeed * 13.7) > 0.35) {
            surfaceCol = mix(surfaceCol, coolTone * 1.35, 0.70);
        }
    } else {
        surfaceCol = suiteTint * 0.32;
    }

    float blindCoverage = fract(roomSeed * 7.9) * 0.52;
    if (!isLobby && paneUv.y > (1.0 - blindCoverage)) {
        float slat = 0.58 + 0.42 * sin(paneUv.y * 52.0);
        surfaceCol *= slat * 0.78;
    }

    // Subtle shadowed interior depth in bright daylight; warmly illuminated rooms at dusk & night!
    float baseBrightness = mix(0.24, 1.32, nightF);
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
    float wetRoadSurface = 0.0;

    // -----------------------------------------------------------------------
    // Procedural World-Space Material Detail & Micro-Normal Perturbation
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Weathered Mineral Asphalt + Tire Tracks + Tar Cracks + Organic Curved Rain Puddles
        vec2 p = fragPosition.xz;
        float distFade = 1.0 - smoothstep(22.0, 95.0, length(viewPos - fragPosition));
        float patchVar = fbm2(p * 0.24);
        float g1 = valueNoise(p * 3.8);
        float g2 = valueNoise(p * 19.5);
        float g3 = valueNoise(p * 48.0);
        float stoneSpeck = g1 * 0.45 + g2 * 0.35 + g3 * 0.20;

        // Authentic weathered urban charcoal-slate asphalt (not pitch-black plastic!)
        albedo = vec3(0.056, 0.059, 0.064) * mix(0.76, 1.34, patchVar * 0.55 + stoneSpeck * 0.45);
        // Occasional pale crushed-granite aggregate glint specks
        albedo += vec3(0.022, 0.022, 0.024) * smoothstep(0.76, 0.95, g3) * distFade;
        roughness = mix(0.68, 0.46, stoneSpeck);

        // Subtle dark bitumen tar-sealed repair crack lines
        float crackWave = abs(fbm2(p * 0.42 + vec2(4.7, 9.2)) - 0.50);
        float tarCrack = (1.0 - smoothstep(0.004, 0.016, crackWave)) * smoothstep(0.42, 0.68, patchVar);
        albedo *= (1.0 - 0.34 * tarCrack);
        roughness = mix(roughness, 0.22, tarCrack * 0.75);

        // Distance-attenuated aggregate normal perturbation (crisp close-up, zero far shimmer)
        float nx = (valueNoise(p * 24.0 + vec2(0.12, 0.0)) - g2) * distFade;
        float nz = (valueNoise(p * 24.0 + vec2(0.0, 0.12)) - g2) * distFade;
        N = normalize(vec3(N.x + nx * 0.22, N.y, N.z + nz * 0.22));

        float rx = clamp(round(p.x / 84.0), -8.0, 8.0) * 84.0;
        float rz = clamp(round(p.y / 84.0), -8.0, 8.0) * 84.0;
        float dx = abs(p.x - rx);
        float dz = abs(p.y - rz);
        bool onBridge = (abs(p.x) <= 9.02) && (p.y >= 680.5) && (p.y <= 896.0);
        bool onVert  = ((dx <= 9.02) && (abs(p.y) <= 681.05)) || onBridge;
        bool onHoriz = (dz <= 9.02) && (abs(p.x) <= 681.05);
        if (onBridge) {
            dx = abs(p.x);
            dz = 28.0;
            onHoriz = false;
        }

        // Worn darkened tire tracks along travel lanes (+-4.2m) + subtle curb gutter darkening
        if (onVert && !onHoriz) {
            float wear = exp(-pow((abs(dx - 4.2) - 0.85) / 0.62, 2.0));
            albedo *= (1.0 - 0.20 * wear);
            roughness = mix(roughness, 0.30, wear * 0.55);
            float gutter = smoothstep(7.7, 9.0, dx);
            albedo *= (1.0 - 0.24 * gutter);
        } else if (onHoriz && !onVert) {
            float wear = exp(-pow((abs(dz - 4.2) - 0.85) / 0.62, 2.0));
            albedo *= (1.0 - 0.20 * wear);
            roughness = mix(roughness, 0.30, wear * 0.55);
            float gutter = smoothstep(7.7, 9.0, dz);
            albedo *= (1.0 - 0.24 * gutter);
        }

        // Slightly scuffed realistic thermoplastic road paint (shows asphalt grain underneath!)
        vec3 yellowPaint = pow(vec3(0.84, 0.63, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.78, 0.79, 0.81), vec3(2.2));
        float paintWear  = mix(0.76, 1.0, smoothstep(0.18, 0.68, g1 * 0.5 + g2 * 0.5));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;

        if (onVert && !onHoriz) {
            if (dz >= 9.35 && dz <= 12.05 && dx < 7.9) {
                float stripe = abs(fract((p.x - rx + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dz >= 12.45 && dz <= 12.90 && dx < 8.4) {
                whiteMask = max(whiteMask, 0.92);
            }
            if (dz > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dx - 0.18), 0.075));
                if (fract(p.y / 9.0) < 0.38) {
                    whiteMask = max(whiteMask, aaBand(abs(dx - 4.35), 0.075));
                }
                whiteMask = max(whiteMask, aaBand(abs(dx - 8.55), 0.07) * 0.62);
            }
        } else if (onHoriz && !onVert) {
            if (dx >= 9.35 && dx <= 12.05 && dz < 7.9) {
                float stripe = abs(fract((p.y - rz + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dx >= 12.45 && dx <= 12.90 && dz < 8.4) {
                whiteMask = max(whiteMask, 0.92);
            }
            if (dx > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dz - 0.18), 0.075));
                if (fract(p.x / 9.0) < 0.38) {
                    whiteMask = max(whiteMask, aaBand(abs(dz - 4.35), 0.075));
                }
                whiteMask = max(whiteMask, aaBand(abs(dz - 8.55), 0.07) * 0.62);
            }
        } else if (onVert && onHoriz) {
            float mh = length(vec2(dx, dz));
            if (mh < 0.38) {
                float rimGroove = 1.0 - smoothstep(0.012, 0.028, abs(mh - 0.32));
                float hatch = 0.92 + 0.08 * sin(p.x * 58.0) * sin(p.y * 58.0);
                albedo = vec3(0.044, 0.045, 0.048) * hatch * (1.0 - 0.35 * rimGroove);
                roughness = 0.44;
                metallic = 0.56;
            }
        }

        float wPaintA = clamp(whiteMask, 0.0, 1.0) * 0.86 * paintWear;
        float yPaintA = clamp(yellowMask, 0.0, 1.0) * 0.90 * paintWear;
        albedo = mix(albedo, whitePaint, wPaintA);
        albedo = mix(albedo, yellowPaint, yPaintA);
        roughness = mix(roughness, 0.52, max(wPaintA, yPaintA) * 0.55);

        // Organic Domain-Warped Curved Rain Puddles (ZERO square noise tiles or hard edge cutoffs!)
        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.18, 0.0, 1.0);
        wetRoadSurface = wetAmt;
        if (wetAmt > 0.02) {
            vec2 warp = vec2(fbm2(p * 0.085 + vec2(3.1, 7.4)), fbm2(p * 0.085 + vec2(11.7, 2.3)));
            float pNoise = fbm2(p * 0.145 + warp * 1.45) * 0.72 + valueNoise(p * 0.52) * 0.28;
            float gutterBias = 0.0;
            if (onVert && !onHoriz) gutterBias = smoothstep(5.8, 8.7, dx) * 0.14;
            else if (onHoriz && !onVert) gutterBias = smoothstep(5.8, 8.7, dz) * 0.14;
            puddleMask = smoothstep(0.55 - wetAmt * 0.17, 0.77 - wetAmt * 0.10, pNoise + gutterBias) * wetAmt;

            // Wet porous asphalt darkens realistically; pooled water becomes smooth mirror
            albedo *= (1.0 - wetAmt * 0.34 - puddleMask * 0.24);
            roughness = mix(roughness, 0.22, wetAmt * 0.72);
            roughness = mix(roughness, 0.055, puddleMask * 0.92);

            // Smooth raindrop ring ripples faded continuously across puddle edges
            float ripBlend = smoothstep(0.14, 0.58, puddleMask) * smoothstep(0.05, 0.35, wetRoadFactor) * distFade;
            if (ripBlend > 0.01) {
                vec2 ripCell = floor(p * 1.65);
                vec2 ripUv = fract(p * 1.65) - 0.5;
                float ripPhase = fract(animTime * 1.85 + hash12(ripCell));
                float ring = sin((length(ripUv) - ripPhase * 0.44) * 44.0) * exp(-length(ripUv) * 6.5) * (1.0 - ripPhase);
                vec3 ripN = normalize(vec3(ripUv.x * ring * 0.09, 1.0, ripUv.y * ring * 0.09));
                N = normalize(mix(N, ripN, ripBlend));
            }
            N = normalize(mix(N, vec3(0.0, 1.0, 0.0), puddleMask * 0.80));
        }
    }
    else if (matId == 2) {
        // MAT 2: Weathered Concrete Sidewalk Pavers & Stone Plazas
        vec2 p = fragPosition.xz;
        float distFade = 1.0 - smoothstep(20.0, 85.0, length(viewPos - fragPosition));
        vec2 tileUv = p / 1.75;
        vec2 cell = floor(tileUv);
        vec2 g = abs(fract(tileUv) - 0.5);
        float seamDist = 0.5 - max(g.x, g.y);
        float seam = 1.0 - smoothstep(0.0, max(fwidth(seamDist), 0.015) * 1.4, seamDist - 0.016);
        float fineGrain = valueNoise(p * 14.0);
        float weatherPatch = fbm2(p * 0.35);
        float slabTint = 0.88 + 0.11 * hash12(cell) + 0.08 * (weatherPatch - 0.5) + 0.05 * (fineGrain - 0.5);
        albedo *= slabTint;
        albedo = mix(albedo, albedo * 0.50, seam * 0.78);
        albedo *= (1.0 - wetRoadFactor * 0.30);

        float nx = (valueNoise(p * 18.0 + vec2(0.10, 0.0)) - fineGrain) * distFade;
        float nz = (valueNoise(p * 18.0 + vec2(0.0, 0.10)) - fineGrain) * distFade;
        N = normalize(vec3(N.x + nx * 0.11, N.y, N.z + nz * 0.11));

        roughness = mix(0.84, 0.24, wetRoadFactor * 0.72);
        puddleMask = wetRoadFactor * smoothstep(0.45, 0.78, fbm2(p * 0.22)) * 0.42 * (1.0 - seam);
    }
    else if (matId == 3) {
        // MAT 3: Architectural Glass Curtain-Wall Skyscraper with 3D Raycasted Interior Mapping
        if (abs(N.y) > 0.55) {
            float roofNoise = fbm2(fragPosition.xz * 1.4);
            albedo = mix(vec3(0.075, 0.080, 0.092), vec3(0.145, 0.150, 0.162), roofNoise);
            roughness = 0.86;
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
                albedo *= 0.28;
                roughness = 0.36;
                metallic = 0.74;
                float bevelX = (uFrac < 0.070) ? -0.22 : ((uFrac > 0.930) ? 0.22 : 0.0);
                float bevelY = (vFrac < 0.13) ? -0.22 : ((vFrac > 0.89) ? 0.22 : 0.0);
                N = normalize(N + vec3(bevelX * N.z, bevelY, -bevelX * N.x));
            } else {
                vec2 paneUv = vec2((uFrac - 0.070) / 0.860,
                                   isGroundLobby ? clamp(vCoord / 3.72, 0.0, 1.0) : ((vFrac - 0.13) / 0.76));
                vec2 bldSeed = floor(fragPosition.xz / 14.0);
                float roomRnd = hash12(vec2(uCell * 1.37 + bldSeed.x * 7.1, vCell * 2.19 + bldSeed.y * 11.3));
                vec3 room3D = sampleInteriorRoom3D(paneUv, V, N, roomRnd, isGroundLobby, nightFactor);

                // Dark tinted architectural glass in sunlight; warm luminous rooms at dusk/night!
                vec3 glassBase = albedo * 0.22;
                albedo = mix(glassBase, room3D, mix(0.38, 0.84, nightFactor));
                roughness = 0.06;
                metallic = 0.20;

                float litThreshold = isGroundLobby ? 0.20 : 0.46;
                if (roomRnd > litThreshold) {
                    emissiveColor = room3D;
                    emissive = mix(0.05, isGroundLobby ? 1.25 : 1.02, nightFactor);
                }
            }
            // Ground-level urban base splash/weathering AO
            albedo *= mix(0.76, 1.0, smoothstep(0.0, 2.2, vCoord));
        }
    }
    else if (matId == 4) {
        // MAT 4: Precast Stone & Brick Masonry Facade with Realistic Reflective Glass & 3D Interiors
        if (abs(N.y) > 0.55) {
            albedo = vec3(0.11, 0.11, 0.12) * (0.85 + 0.24 * fbm2(fragPosition.xz * 1.8));
            roughness = 0.86;
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
                albedo *= 1.18;
                roughness = 0.72;
                N = normalize(N + vec3(0.0, 0.28, 0.0));
            } else if (inWindow || groundShop) {
                vec2 paneUv = groundShop
                                  ? vec2((uFrac - 0.12) / 0.76, (vCoord - 0.35) / 2.60)
                                  : vec2((uFrac - 0.20) / 0.60, (vFrac - 0.22) / 0.58);
                vec2 seed = floor(fragPosition.xz / 12.0);
                float rnd = hash12(vec2(uCell + seed.x * 5.7, vCell + seed.y * 9.3));
                vec3 room3D = sampleInteriorRoom3D(paneUv, V, N, rnd, groundShop, nightFactor);

                // Realistic deep architectural window panes in daylight, glowing cozy interiors at night!
                vec3 dayPane = vec3(0.025, 0.036, 0.048);
                albedo = mix(dayPane, room3D * 0.72, mix(0.32, 0.86, nightFactor));
                roughness = 0.07;
                metallic = 0.16;
                if (rnd > (groundShop ? 0.26 : 0.50)) {
                    emissiveColor = room3D;
                    emissive = mix(0.04, 0.96, nightFactor);
                }
            } else {
                // Weathered architectural stone/brick courses + subtle micro-relief normal
                float hJoint = smoothstep(0.91, 0.98, abs(sin(vCoord * 9.5)));
                float vJoint = smoothstep(0.94, 0.99, abs(sin(uCoord * 4.4)));
                float joint = max(hJoint, vJoint * 0.75);
                float stoneVar = fbm2(vec2(uCoord, vCoord) * 1.15);
                float microGrain = valueNoise(vec2(uCoord, vCoord) * 9.0);
                albedo *= (0.94 - 0.18 * joint + 0.10 * (stoneVar - 0.5) + 0.05 * (microGrain - 0.5));
                albedo *= mix(0.72, 1.0, smoothstep(0.0, 2.4, vCoord));
                N = normalize(N + vec3(0.0, (cos(vCoord * 9.5) * hJoint) * 0.14, 0.0));
                roughness = 0.82;
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Corrugated Industrial Steel / Shipping Containers with Normal-Perturbed Ribs
        float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
        if (abs(N.y) < 0.5) {
            float rib = sin(uCoord * 16.0);
            float ribGrad = cos(uCoord * 16.0);
            float grime = fbm2(vec2(uCoord, fragPosition.y) * 0.85);
            albedo *= (0.88 + 0.12 * rib) * mix(0.84, 1.08, grime);
            N = normalize(N + vec3(N.z * ribGrad * 0.18, 0.0, -N.x * ribGrad * 0.18));
        }
        roughness = 0.44;
        metallic = 0.52;
    }
    else if (matId == 6) {
        // MAT 6: Animated Multi-Wave Coastal Ocean Water & Calm Grand Central Park Lake
        vec2 wp = fragPosition.xz;
        bool inParkLake = (fragPosition.y > -0.15) && (abs(wp.x) < 220.0) && (abs(wp.y) < 220.0);
        if (inParkLake) {
            // Calm reflective emerald-blue inland lake / fountain basin with gentle micro-ripples
            float r1 = sin(wp.x * 1.45 + animTime * 1.85 + cos(wp.y * 1.15));
            float r2 = cos(wp.y * 1.75 - animTime * 1.55 + sin(wp.x * 1.30));
            N = normalize(vec3(-(r1 + r2) * 0.018, 1.0, -(r2 - r1) * 0.018));
            albedo = mix(vec3(0.014, 0.068, 0.088), vec3(0.005, 0.020, 0.032), nightFactor);
            roughness = 0.045;
            metallic = 0.06;
        } else {
            float w1 = sin(wp.x * 0.18 + animTime * 1.45 + cos(wp.y * 0.12) * 1.2);
            float w2 = cos(wp.y * 0.24 - animTime * 1.15 + sin(wp.x * 0.16));
            float w3 = sin((wp.x + wp.y) * 0.48 + animTime * 2.15) * 0.5;
            float w4 = cos((wp.x * 0.82 - wp.y * 0.68) + animTime * 2.85) * 0.26;
            vec2 grad = vec2(w1 * 0.052 + w3 * 0.036 + w4 * 0.022, w2 * 0.052 + w3 * 0.036 - w4 * 0.022);
            N = normalize(vec3(-grad.x, 1.0, -grad.y));

            vec3 deepWater = mix(vec3(0.007, 0.048, 0.112), vec3(0.003, 0.010, 0.025), nightFactor);
            vec3 shallowTurquoise = mix(vec3(0.020, 0.155, 0.205), vec3(0.007, 0.028, 0.052), nightFactor);

            float eastNorthSouthDist = max(wp.x - 716.0, abs(wp.y) - 716.0);
            float westBeachEdge = (abs(wp.y) < 684.0) ? (-746.0 - wp.x) : (-716.0 - wp.x);
            float mainIslandDist = max(eastNorthSouthDist, westBeachEdge);
            float penDist = max(abs(wp.x) - 130.0, max(815.0 - wp.y, wp.y - 954.0));
            float islandDist = min(mainIslandDist, penDist);

            float shoreProx = 1.0 - clamp(islandDist / 52.0, 0.0, 1.0);
            albedo = mix(deepWater, shallowTurquoise, shoreProx * 0.76);

            if (islandDist > -7.0 && islandDist < 20.0) {
                float surfWave = sin(islandDist * 0.78 - animTime * 2.5 + valueNoise(wp * 0.22) * 3.0);
                float foam = smoothstep(0.54, 0.92, surfWave) * (1.0 - clamp(islandDist / 20.0, 0.0, 1.0));
                albedo = mix(albedo, vec3(0.76, 0.82, 0.86), foam * 0.74);
            }
            roughness = 0.065;
            metallic = 0.06;
        }
    }
    else if (matId == 10) {
        // MAT 10: Natural Multi-Tone Park Lawn (ground) & Dappled Botanical Tree Foliage (canopies)
        float distFade = 1.0 - smoothstep(24.0, 95.0, length(viewPos - fragPosition));
        if (fragPosition.y < 0.35) {
            vec2 p = fragPosition.xz;
            float macro = fbm2(p * 0.22);
            float micro = valueNoise(p * 9.5);
            float blades = valueNoise(p * 28.0);
            // Subtle park turf mowing bands + organic moss/clover/dry-straw variation
            float mowBand = 0.95 + 0.05 * sin(p.x * 1.15);
            vec3 lushTint = mix(vec3(0.84, 0.94, 0.78), vec3(1.16, 1.06, 0.76), macro);
            albedo *= lushTint * mowBand * mix(0.86, 1.12, micro * 0.6 + blades * 0.4);
            albedo *= (1.0 - wetRoadFactor * 0.22);

            float gx = (valueNoise(p * 22.0 + vec2(0.12, 0.0)) - blades) * distFade;
            float gz = (valueNoise(p * 22.0 + vec2(0.0, 0.12)) - blades) * distFade;
            N = normalize(vec3(N.x + gx * 0.24, N.y, N.z + gz * 0.24));
            roughness = mix(0.88, 0.56, wetRoadFactor * 0.65);
        } else {
            // Tree canopy leaves: high-frequency dappled cluster normal bump + shaded interior depth
            vec2 leafUv = fragPosition.xz * 4.8 + vec2(fragPosition.y * 3.9, -fragPosition.y * 2.7);
            float leafCluster = fbm2(leafUv);
            float leafFine = valueNoise(leafUv * 3.1);
            float canopyDepth = mix(0.72, 1.18, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
            albedo *= canopyDepth * mix(0.78, 1.24, leafCluster * 0.65 + leafFine * 0.35);

            float lx = (valueNoise(leafUv * 2.4 + vec2(0.18, 0.0)) - leafFine) * distFade;
            float lz = (valueNoise(leafUv * 2.4 + vec2(0.0, 0.18)) - leafFine) * distFade;
            N = normalize(N + vec3(lx * 0.36, (leafCluster - 0.5) * 0.28 * distFade, lz * 0.36));
            roughness = mix(0.80, 0.48, wetRoadFactor * 0.55);
        }
    }
    else if (matId == 11) {
        // MAT 11: Woven Fabric & Tailored Clothing (micro-weave grain + subtle garment folds)
        vec2 weaveUv = vec2((fragPosition.x + fragPosition.z) * 28.0, fragPosition.y * 28.0);
        float weave = 0.94 + 0.10 * (valueNoise(weaveUv) - 0.5);
        float fold = 0.95 + 0.07 * sin(fragPosition.y * 18.0 + (fragPosition.x - fragPosition.z) * 14.0);
        albedo *= weave * fold;
        roughness = max(roughness, 0.72);
    }
    else if (matId == 12) {
        // MAT 12: Organic Human Skin (soft matte micro-surface + warm subsurface wrap below)
        float toneVar = 0.97 + 0.05 * valueNoise(fragPosition.xz * 18.0 + vec2(fragPosition.y * 18.0));
        albedo *= toneVar;
        roughness = clamp(roughness, 0.52, 0.70);
    }
    else if (matId == 13) {
        // MAT 13: Horizontal Weathered Timber Boardwalk Planks OR Vertical Fissured Natural Tree Bark
        if (abs(N.y) > 0.65) {
            // Crisp coastal timber deck planking (staggered planks, dark seam grooves & wood grain)
            float plankX = fragPosition.x * 2.85;
            float plankId = floor(plankX);
            float plankU = fract(plankX);
            float seamX = 1.0 - smoothstep(0.025, 0.065, min(plankU, 1.0 - plankU));
            float jointZ = fract(fragPosition.z * 0.28 + hash12(vec2(plankId, 3.7)) * 4.0);
            float seamZ = 1.0 - smoothstep(0.008, 0.022, min(jointZ, 1.0 - jointZ));
            float plankVar = 0.86 + 0.24 * hash12(vec2(plankId, floor(fragPosition.z * 0.28)));
            float grain = 0.93 + 0.14 * valueNoise(vec2(fragPosition.x * 26.0, fragPosition.z * 3.8));
            albedo *= plankVar * grain * (1.0 - max(seamX, seamZ * 0.8) * 0.42);
            roughness = mix(0.84, 0.46, wetRoadFactor * 0.55);
        } else {
            // Vertical fissured natural tree bark & weathered wood grain
            float barkCoord = (fragPosition.x + fragPosition.z) * 5.2 + atan(N.z, N.x) * 3.5;
            float ridge = fbm2(vec2(barkCoord, fragPosition.y * 2.4));
            float fissure = sin(barkCoord * 4.2 + ridge * 5.2);
            albedo *= mix(0.68, 1.12, 0.5 + 0.30 * fissure + 0.22 * (ridge - 0.5));
            N = normalize(N + vec3(-N.z * fissure * 0.22, 0.0, N.x * fissure * 0.22));
            roughness = 0.89;
        }
    }
    else if (matId == 14) {
        // MAT 14: Coastal California Beach Sand (windblown micro-ripples, quartz grain & damp tide edge)
        vec2 p = fragPosition.xz;
        float distFade = 1.0 - smoothstep(20.0, 90.0, length(viewPos - fragPosition));
        float duneMacro = fbm2(p * 0.12);
        float sandGrain = valueNoise(p * 26.0);
        float windRip = sin(p.x * 5.2 + fbm2(p * 0.65) * 4.5);
        float dampShore = 1.0 - smoothstep(-746.0, -731.0, p.x);
        albedo *= mix(0.88, 1.12, duneMacro * 0.65 + sandGrain * 0.35);
        albedo *= mix(1.0, 0.64, max(dampShore, wetRoadFactor * 0.42));
        albedo += vec3(0.032, 0.030, 0.024) * smoothstep(0.84, 0.98, valueNoise(p * 52.0)) * distFade * (1.0 - dampShore);
        N = normalize(vec3(N.x + windRip * 0.085 * distFade, N.y, N.z + (sandGrain - 0.5) * 0.075 * distFade));
        roughness = mix(0.88, 0.22, max(dampShore, wetRoadFactor * 0.40));
    }

    // -----------------------------------------------------------------------
    // Pure Emissive Bypass (Headlights, Taillights, Neon Signs, Traffic Signals, Coronas)
    // -----------------------------------------------------------------------
    if (matId == 9) {
        vec3 glow = albedo * (1.28 + emissive * 1.38);
        float dist = length(viewPos - fragPosition);
        float fogStart = mix(110.0, 52.0, wetRoadFactor);
        float fogF = clamp(1.0 - exp(-max(0.0, dist - fogStart) * 0.0028), 0.0, 0.85);
        vec3 hdrOut = mix(glow, pow(fogColor, vec3(2.2)), fogF * 0.55);
        vec3 mapped = pow(acesTonemap(hdrOut), vec3(1.0 / 2.2));
        mapped = max(mapped, clamp(srgbBase.rgb * (0.90 + colSpecular.z * 0.22), 0.0, 1.0));
        finalColor = vec4(mapped, srgbBase.a);
        return;
    }

    // -----------------------------------------------------------------------
    // Cook-Torrance GGX Microfacet BRDF + Dual-Layer Clearcoat + City Env Reflections
    // -----------------------------------------------------------------------
    float rawNdotL = dot(N, L);
    float ndotl = max(rawNdotL, 0.0);
    if (matId == 10) {
        ndotl = max(rawNdotL * 0.72 + 0.28, 0.0);
    }

    float shadow = computeSoftShadow(fragPosition, N, L);
    float sunVis = 1.0 - shadow * 0.88;

    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);
    // Natural cool sky fill inside cast shadows for crisp sunlit-vs-shade contrast
    ambient = mix(ambient, ambient * vec3(0.76, 0.86, 1.08), shadow * 0.44);
    float contactAo = mix(0.64, 1.0, clamp(fragPosition.y / 1.7, 0.0, 1.0));
    if (N.y > 0.75) contactAo = 1.0;

    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
    float ndotv = max(dot(N, V), 0.001);
    float vdoth = max(dot(V, H), 0.0);

    // Metallic paint grazing-angle color flop on automotive body panels
    if (matId == 7) {
        float flop = pow(1.0 - ndotv, 1.70);
        albedo *= mix(vec3(1.02), vec3(0.58, 0.62, 0.70), flop * (1.0 - roughness * 0.60));
    } else if (matId == 8) {
        // Deep luxury smoked automotive glass tint
        albedo *= 0.24;
        roughness = 0.04;
    }

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

    vec3 directLit = (kD * albedo / kPi + specBrdf * 0.44) * sunColor * ndotl * sunVis * 2.35;
    vec3 envLit    = albedo * ambient * contactAo;

    // Organic material response: fabric grazing sheen, skin warm subsurface wrap, backlit tree leaves
    if (matId == 11) {
        float clothSheen = pow(1.0 - ndotv, 3.0) * 0.14;
        envLit += (albedo * 0.55 + vec3(0.08)) * skyAmbient * clothSheen;
    } else if (matId == 12) {
        float sssWrap = clamp((rawNdotL + 0.45) / 1.45, 0.0, 1.0) * (1.0 - ndotl);
        directLit += albedo * vec3(0.42, 0.13, 0.06) * sssWrap * sunColor * sunVis * 0.85;
    } else if (matId == 10 && fragPosition.y >= 0.35) {
        float leafBacklit = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * 0.24;
        directLit += albedo * vec3(0.95, 1.12, 0.42) * leafBacklit * sunColor * sunVis;
    }

    // 360-Degree Physically Grounded Environment Reflections on Glass, Ocean, Car Paint, and Wet Pavements
    bool reflectiveMat = (matId == 3 || matId == 4 || matId == 6 || matId == 7 || matId == 8 || puddleMask > 0.04 || wetRoadSurface > 0.15);
    if (reflectiveMat) {
        vec3 R = reflect(-V, N);
        vec3 envRefl = sampleCityEnvMap(R, fragPosition, roughness, nightFactor);

        float specExp = (matId == 6) ? 210.0 : ((matId == 8) ? 260.0 : ((matId == 7) ? mix(220.0, 28.0, clamp(roughness * 1.5, 0.0, 1.0)) : 140.0));
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : ((matId == 8) ? 2.2 : ((matId == 7) ? 1.55 : 1.35)));

        float wetCoat = puddleMask * 0.62 + wetRoadSurface * 0.14;
        float coatStrength = (matId == 6) ? 0.88
                           : ((matId == 8) ? 0.72
                           : ((matId == 7) ? 0.74
                           : ((matId == 3) ? 0.62
                           : ((matId == 4) ? (0.44 * (1.0 - step(0.3, roughness))) : wetCoat))));
        if (matId == 7) {
            // Dual-lobe showroom clearcoat + subtle metallic basecoat flake + photographic horizon reflection band
            float coatMask = 1.0 - smoothstep(0.20, 0.50, roughness) * 0.78;
            float flake = valueNoise(fragPosition.xz * 92.0 + vec2(fragPosition.y * 74.0));
            float ccAlpha2 = 0.00018;
            float ccDen = (ndoth * ndoth * (ccAlpha2 - 1.0) + 1.0);
            float ccD = ccAlpha2 / max(kPi * ccDen * ccDen, 0.0001);
            float ccF = 0.04 + 0.96 * pow(1.0 - vdoth, 5.0);
            directLit += vec3(min(ccD * ccF * 0.012, 1.85)) * sunColor * ndotl * sunVis * coatMask;
            envLit += albedo * pow(ndoth, 28.0) * (0.12 + 0.28 * flake) * (1.0 - roughness) * sunVis;
            float horizLine = exp(-abs(R.y - 0.025) * 18.0);
            envRefl += mix(horizonColor * 0.55, vec3(0.95, 0.98, 1.04), 0.42) * horizLine * 0.26 * (1.0 - nightFactor * 0.50) * coatMask;
            coatStrength *= coatMask;
        }
        float coatFresnel = (matId == 8)
                                ? (0.12 + 0.84 * pow(1.0 - ndotv, 4.0))
                                : (0.06 + 0.90 * pow(1.0 - ndotv, 4.2));
        envLit += envRefl * coatFresnel * coatStrength;
    }

    vec3 totalLinear = envLit + directLit + emissiveColor * emissive;

    // Exponential coastal aerial perspective + closer rain mist when storm weather is active
    float dist = length(viewPos - fragPosition);
    float fogStart = mix(95.0, 40.0, wetRoadFactor);
    float fogDens  = mix(0.0028, 0.0046, wetRoadFactor);
    float fogFactor = clamp(1.0 - exp(-max(0.0, dist - fogStart) * fogDens), 0.0, 0.88);
    vec3 linearFog = pow(fogColor, vec3(2.2));
    vec3 fogged = mix(totalLinear, linearFog, fogFactor);

    vec3 mapped = pow(acesTonemap(fogged * 1.02), vec3(1.0 / 2.2));
    finalColor = vec4(mapped, srgbBase.a);
}
)GLSL";

// ---------------------------------------------------------------------------
// 4) Full-Screen Multi-Ring HDR-Threshold Bloom + Cinematic Film Color Grade
// ---------------------------------------------------------------------------
const char* kPostVertShader = OC_GLSL_VERT_HEADER R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

const char* kPostFragShader = OC_GLSL_FRAG_HEADER R"GLSL(
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform float bloomStrength;
uniform float exposure;
uniform float nitroStrength;
uniform int enableBloom;

out vec4 finalColor;

// Selective high threshold so only true emissives, headlights & sun glints bloom (never daytime sidewalks!)
vec3 extractBright(vec3 c) {
    float peak = max(c.r, max(c.g, c.b));
    float weight = smoothstep(0.88, 1.04, peak);
    return c * weight;
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 texel = 1.0 / max(resolution, vec2(1.0));

    // Subtle edge radial chromatic speed polish during Nitrous boost
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

    vec3 col = clamp(baseCol * exposure + bloom, 0.0, 1.0);

    // Subtle cinema color grade (grounded cool shadow depth + gentle warm highlight lift) & natural contrast
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowGrade = vec3(0.97, 0.99, 1.03);
    vec3 highGrade   = vec3(1.02, 1.00, 0.98);
    col *= mix(shadowGrade, highGrade, smoothstep(0.15, 0.85, luma));
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.14);

    // Subtle optical lens vignette + 1/255 IGN anti-banding dither for silk-smooth sky gradients
    float vignette = 1.0 - smoothstep(0.45, 0.98, edgeDist) * 0.11;
    col *= vignette;
    float dither = (fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
    col += vec3(dither);

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

// Builds a HOLLOW 24-segment rounded-shoulder rubber tire (radius=1, z in [-0.5, +0.5], inner bead r=0.64)
// Leaving the center rim cavity open so concave 3D alloy spokes, rotors & Brembo calipers are 100% visible!
Mesh buildCenteredWheelMesh(int slices) {
    SmoothMeshBuilder b;
    const float rOuter = 1.00f;
    const float rSide  = 0.93f;
    const float rBead  = 0.65f;
    const float rInner = 0.62f;
    const float zTread = 0.38f;
    const float zSide  = 0.50f;
    const float zBead  = 0.45f;

    for (int i = 0; i < slices; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * kTwoPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * kTwoPi;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);

        // Outer rubber contact tread + curved shoulder transitions
        b.addQuadOutward({c0 * rOuter, s0 * rOuter, -zTread}, {c1 * rOuter, s1 * rOuter, -zTread},
                         {c1 * rOuter, s1 * rOuter,  zTread}, {c0 * rOuter, s0 * rOuter,  zTread}, {cm, sm, 0.0f});
        b.addQuadOutward({c0 * rOuter, s0 * rOuter,  zTread}, {c1 * rOuter, s1 * rOuter,  zTread},
                         {c1 * rSide,  s1 * rSide,   zSide},  {c0 * rSide,  s0 * rSide,   zSide},  {cm, sm, 0.55f});
        b.addQuadOutward({c0 * rSide,  s0 * rSide,  -zSide},  {c1 * rSide,  s1 * rSide,  -zSide},
                         {c1 * rOuter, s1 * rOuter, -zTread}, {c0 * rOuter, s0 * rOuter, -zTread}, {cm, sm, -0.55f});

        // Open annular rubber sidewalls (+Z and -Z) sloping gently into the wheel rim bead!
        b.addQuadOutward({c0 * rSide, s0 * rSide,  zSide}, {c1 * rSide, s1 * rSide,  zSide},
                         {c1 * rBead, s1 * rBead,  zBead}, {c0 * rBead, s0 * rBead,  zBead}, {0.0f, 0.0f, 1.0f});
        b.addQuadOutward({c0 * rBead, s0 * rBead, -zBead}, {c1 * rBead, s1 * rBead, -zBead},
                         {c1 * rSide, s1 * rSide, -zSide}, {c0 * rSide, s0 * rSide, -zSide}, {0.0f, 0.0f, -1.0f});

        // Inner dark barrel wall lining the wheel well cavity
        b.addQuadOutward({c0 * rInner, s0 * rInner,  zBead}, {c1 * rInner, s1 * rInner,  zBead},
                         {c1 * rInner, s1 * rInner, -zBead}, {c0 * rInner, s0 * rInner, -zBead}, {-cm, -sm, 0.0f});
    }
    return b.uploadSmooth(55.0f);
}

// Builds a dimensional 3D deep-concave 5-twin-spoke forged alloy rim (both +Z and -Z faces!)
Mesh buildSportRimMesh() {
    SmoothMeshBuilder b;
    constexpr int kRimSegs = 24;
    const float rLipOuter = 0.695f;
    const float rLipInner = 0.605f;
    const float zFace = 0.485f;
    const float zBarrel = 0.34f;
    const float zHub  = 0.275f;

    for (int side = -1; side <= 1; side += 2) {
        const float sZ = static_cast<float>(side);
        for (int i = 0; i < kRimSegs; ++i) {
            const float a0 = (static_cast<float>(i) / kRimSegs) * kTwoPi;
            const float a1 = (static_cast<float>(i + 1) / kRimSegs) * kTwoPi;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);

            // Machined outer lip rim face + deep stepped inner barrel bevel
            b.addQuadOutward({c0 * rLipInner, s0 * rLipInner, sZ * (zFace - 0.015f)},
                             {c1 * rLipInner, s1 * rLipInner, sZ * (zFace - 0.015f)},
                             {c1 * rLipOuter, s1 * rLipOuter, sZ * (zFace + 0.015f)},
                             {c0 * rLipOuter, s0 * rLipOuter, sZ * (zFace + 0.015f)},
                             {0.0f, 0.0f, sZ});
            b.addQuadOutward({c0 * rLipInner, s0 * rLipInner, sZ * zBarrel},
                             {c1 * rLipInner, s1 * rLipInner, sZ * zBarrel},
                             {c1 * rLipInner, s1 * rLipInner, sZ * (zFace - 0.015f)},
                             {c0 * rLipInner, s0 * rLipInner, sZ * (zFace - 0.015f)},
                             {-cm, -sm, sZ * 0.25f});
            // Recessed forged center hub disc + outer flange
            b.addTriOutward({0.0f, 0.0f, sZ * (zHub + 0.032f)},
                            {c0 * 0.21f, s0 * 0.21f, sZ * (zHub + 0.012f)},
                            {c1 * 0.21f, s1 * 0.21f, sZ * (zHub + 0.012f)},
                            {0.0f, 0.0f, sZ});
        }

        // 5 Dimensional 3D Twin-Spoke Star Alloy Arms sloping deeply from hub to rim lip!
        for (int sp = 0; sp < 5; ++sp) {
            const float baseAng = (static_cast<float>(sp) / 5.0f) * kTwoPi;
            // Raised lug bolt boss on center hub
            const float la = baseAng + (kTwoPi / 10.0f);
            b.addBox({std::cos(la) * 0.135f, std::sin(la) * 0.135f, sZ * (zHub + 0.025f)},
                     {0.032f, 0.032f, 0.030f});

            for (int twin = -1; twin <= 1; twin += 2) {
                const float ang = baseAng + static_cast<float>(twin) * 0.155f;
                const float ca = std::cos(ang), sa = std::sin(ang);
                const float px = -sa * 0.044f, py = ca * 0.044f;
                const float depth = 0.062f;

                const Vector3 iA{ca * 0.16f - px, sa * 0.16f - py, sZ * (zHub + 0.014f)};
                const Vector3 iB{ca * 0.16f + px, sa * 0.16f + py, sZ * (zHub + 0.014f)};
                const Vector3 oB{ca * 0.625f + px * 0.72f, sa * 0.625f + py * 0.72f, sZ * (zFace - 0.008f)};
                const Vector3 oA{ca * 0.625f - px * 0.72f, sa * 0.625f - py * 0.72f, sZ * (zFace - 0.008f)};

                const Vector3 iAb{iA.x, iA.y, iA.z - sZ * depth};
                const Vector3 iBb{iB.x, iB.y, iB.z - sZ * depth};
                const Vector3 oBb{oB.x, oB.y, oB.z - sZ * depth};
                const Vector3 oAb{oA.x, oA.y, oA.z - sZ * depth};

                // Front machined spoke face + two 3D extruded bevel side flanks
                b.addQuadOutward(iA, iB, oB, oA, {0.0f, 0.0f, sZ});
                b.addQuadOutward(iAb, iA, oA, oAb, {-px, -py, 0.0f});
                b.addQuadOutward(iB, iBb, oBb, oB, {px, py, 0.0f});
            }
        }
    }
    return b.uploadSmooth(35.0f);
}

// Builds an organic multi-lobe bumpy botanical foliage cluster (replaces smooth plastic sphere trees!)
Mesh buildFoliageClusterMesh() {
    SmoothMeshBuilder b;
    constexpr int kRings = 12;
    constexpr int kSlices = 18;

    auto evalPoint = [](int r, int s) -> Vector3 {
        const float phi = (static_cast<float>(r) / static_cast<float>(kRings)) * kPi;
        const float th  = (static_cast<float>(s) / static_cast<float>(kSlices)) * kTwoPi;
        const float sp = std::sin(phi);
        const float cp = std::cos(phi);
        const float dx = sp * std::cos(th);
        const float dy = cp;
        const float dz = sp * std::sin(th);
        // Multi-harmonic organic botanical clump displacement (flat bottom slightly tucked, irregular outer leafy tufts)
        const float bump1 = 0.14f * std::sin(dx * 4.3f + 0.7f) * std::cos(dz * 4.1f - 0.5f);
        const float bump2 = 0.10f * std::sin(dy * 5.2f + dx * 3.6f) * std::cos(dz * 5.4f + 1.1f);
        const float bump3 = 0.06f * std::sin((dx + dy + dz) * 8.5f);
        const float bottomFlat = (dy < -0.25f) ? (1.0f + (dy + 0.25f) * 0.24f) : 1.0f;
        const float rad = (0.94f + bump1 + bump2 + bump3) * bottomFlat;
        return {dx * rad, dy * rad, dz * rad};
    };

    for (int r = 0; r < kRings; ++r) {
        for (int s = 0; s < kSlices; ++s) {
            const Vector3 p00 = evalPoint(r, s);
            const Vector3 p10 = evalPoint(r + 1, s);
            const Vector3 p11 = evalPoint(r + 1, s + 1);
            const Vector3 p01 = evalPoint(r, s + 1);
            const Vector3 hint{
                0.25f * (p00.x + p10.x + p11.x + p01.x),
                0.25f * (p00.y + p10.y + p11.y + p01.y),
                0.25f * (p00.z + p10.z + p11.z + p01.z)
            };
            if (r == 0) {
                b.addTriOutward(p00, p10, p11, hint);
            } else if (r == kRings - 1) {
                b.addTriOutward(p00, p10, p01, hint);
            } else {
                b.addQuadOutward(p00, p10, p11, p01, hint);
            }
        }
    }
    return b.uploadSmooth(68.0f);
}

// Builds an organic multi-lobe slender tapered Mediterranean Cypress evergreen spire (y in [0, 1])
Mesh buildCypressMesh() {
    SmoothMeshBuilder b;
    constexpr int kRings = 16;
    constexpr int kSlices = 14;

    auto evalPoint = [](int r, int s) -> Vector3 {
        const float u = static_cast<float>(r) / static_cast<float>(kRings); // 0 = base, 1 = tip
        const float th = (static_cast<float>(s) / static_cast<float>(kSlices)) * kTwoPi;
        // Graceful botanical cypress spindle silhouette: full lower swell tapering smoothly to a fine spire
        float env = 0.0f;
        if (r == 0 || r == kRings) {
            env = 0.0f;
        } else {
            const float lt = clampf(u / 0.18f, 0.0f, 1.0f);
            const float lower = lt * lt * (3.0f - 2.0f * lt);
            const float taper = std::pow(1.0f - u, 0.72f);
            const float tierWave = 1.0f + 0.13f * std::sin(u * 26.0f + th * 2.0f)
                                        + 0.09f * std::cos(th * 5.0f + u * 11.0f);
            env = std::max(0.04f, lower * taper * 1.12f * tierWave);
        }
        return {std::cos(th) * env, u, std::sin(th) * env};
    };

    const float du = 1.0f / static_cast<float>(kRings);
    for (int r = 0; r < kRings; ++r) {
        for (int s = 0; s < kSlices; ++s) {
            const Vector3 p00 = evalPoint(r, s);
            const Vector3 p10 = evalPoint(r + 1, s);
            const Vector3 p11 = evalPoint(r + 1, s + 1);
            const Vector3 p01 = evalPoint(r, s + 1);
            const float rad0 = 0.5f * (std::hypot(p00.x, p00.z) + std::hypot(p01.x, p01.z));
            const float rad1 = 0.5f * (std::hypot(p10.x, p10.z) + std::hypot(p11.x, p11.z));
            const float midTh = ((static_cast<float>(s) + 0.5f) / static_cast<float>(kSlices)) * kTwoPi;
            const Vector3 hint{
                std::cos(midTh) * du,
                -(rad1 - rad0),
                std::sin(midTh) * du
            };
            if (r == 0) {
                b.addTriOutward(p00, p10, p11, hint);
            } else if (r == kRings - 1) {
                b.addTriOutward(p00, p10, p01, hint);
            } else {
                b.addQuadOutward(p00, p10, p11, p01, hint);
            }
        }
    }
    return b.uploadSmooth(62.0f);
}

// Builds a true architectural 4-sided pitched hip roof (unit footprint [-0.5, +0.5] x [-0.5, +0.5], height [0, 1])
Mesh buildHipRoofMesh() {
    SmoothMeshBuilder b;
    const Vector3 fl{-0.5f, 0.0f, -0.5f};
    const Vector3 fr{ 0.5f, 0.0f, -0.5f};
    const Vector3 br{ 0.5f, 0.0f,  0.5f};
    const Vector3 bl{-0.5f, 0.0f,  0.5f};
    const Vector3 rl{-0.26f, 1.0f, 0.0f};
    const Vector3 rr{ 0.26f, 1.0f, 0.0f};

    // North & South main sloped roof pitches
    b.addQuadOutward(fl, fr, rr, rl, {0.0f, 0.7f, -0.7f});
    b.addQuadOutward(br, bl, rl, rr, {0.0f, 0.7f,  0.7f});
    // West & East triangular hip slopes
    b.addTriOutward(bl, fl, rl, {-0.8f, 0.6f, 0.0f});
    b.addTriOutward(fr, br, rr, { 0.8f, 0.6f, 0.0f});
    // Soffit underside cap
    b.addQuadOutward(fl, bl, br, fr, {0.0f, -1.0f, 0.0f});
    return b.uploadSmooth(15.0f);
}

// Builds a sleek flat architectural hollow ground ring (Y-up, outer radius 1.0, inner radius innerRatio, y in [0, 1])
Mesh buildHollowRingMesh(int slices = 32, float innerRatio = 0.87f) {
    SmoothMeshBuilder b;
    const float rOut = 1.00f;
    const float rIn  = innerRatio;
    for (int i = 0; i < slices; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * kTwoPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * kTwoPi;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);

        // Top annular face (y = 1.0)
        b.addQuadOutward({c0 * rIn,  1.0f, s0 * rIn},  {c1 * rIn,  1.0f, s1 * rIn},
                         {c1 * rOut, 1.0f, s1 * rOut}, {c0 * rOut, 1.0f, s0 * rOut}, {0.0f, 1.0f, 0.0f});
        // Outer rim wall
        b.addQuadOutward({c0 * rOut, 0.0f, s0 * rOut}, {c1 * rOut, 0.0f, s1 * rOut},
                         {c1 * rOut, 1.0f, s1 * rOut}, {c0 * rOut, 1.0f, s0 * rOut}, {cm, 0.0f, sm});
        // Inner rim wall
        b.addQuadOutward({c1 * rIn,  0.0f, s1 * rIn},  {c0 * rIn,  0.0f, s0 * rIn},
                         {c0 * rIn,  1.0f, s0 * rIn},  {c1 * rIn,  1.0f, s1 * rIn},  {-cm, 0.0f, -sm});
    }
    return b.uploadSmooth(35.0f);
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

        // 1) Plan-view width curve: aerodynamic curved nose/tail taper + muscular fender flares over wheels
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

        // 2) Underbody diffuser/splitter curve + full-clearance semicircular 3D wheel-arch cutout
        float yBot = groundClear;
        if (s < -0.74f) {
            const float u = (-0.74f - s) / 0.26f;
            yBot += 0.13f * u * u;
        } else if (s > 0.78f) {
            const float u = (s - 0.78f) / 0.22f;
            yBot += 0.05f * u * u;
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
            beltY = lerpf(hoodBaseY, noseTipY, std::pow(clampf(u, 0.0f, 1.0f), 1.35f));
        } else if (s >= 0.0f) {
            const float u = s / std::max(0.05f, sFrontBase);
            beltY = lerpf(beltMidY, hoodBaseY, smooth01(u));
        } else {
            const float u = (-s) / 1.0f;
            beltY = lerpf(beltMidY, tailDeckY, smooth01(u * 1.15f));
            if (s < -0.86f) {
                const float tu = (-0.86f - s) / 0.14f;
                beltY -= 0.07f * tu * tu;
            }
        }
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
                cabinFrac = 1.0f + 0.025f * (1.0f - ru * ru);
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

        if (kind == VehicleKind::ClassicRoadster && s > -0.36f && s < 0.07f) {
            st.yCrownSide = finalBeltY + 0.015f;
            st.yTopCenter = finalBeltY - 0.15f;
        } else if (kind == VehicleKind::OffroadPickup && s > -0.90f && s < -0.12f) {
            st.yCrownSide = finalBeltY + 0.020f;
            st.yTopCenter = finalBeltY - 0.16f;
        }

        const float roofNarrow = lerpf(0.86f, tumblehome, clampf(cabinFrac, 0.0f, 1.0f));
        const float archFrac = clampf(archLift / std::max(0.08f, wheelR * 0.95f), 0.0f, 1.0f);
        st.wUnder     = lerpf(wMax * 0.76f, wMax * 0.50f, smooth01(archFrac));
        st.wSill      = wMax * 0.93f;
        st.wBelly     = wMax;
        st.wBelt      = wMax * 0.94f;
        st.wUpper     = wMax * lerpf(0.91f, roofNarrow + 0.06f, clampf(cabinFrac, 0.0f, 1.0f));
        st.wCrownSide = wMax * roofNarrow * 0.88f;
        return st;
    };

    // High-density 44-station longitudinal loft for silky-smooth wheel arches, pillars, hood & roofline
    std::vector<float> sList;
    for (int i = 0; i <= 44; ++i) {
        sList.push_back(-1.0f + 2.0f * (static_cast<float>(i) / 44.0f));
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
        if (stations.empty() || (sv - stations.back()) > 0.011f) {
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
        constexpr int kWsRows = 5;
        constexpr int kWsCols = 6;
        const float s0 = sFrontTop + 0.025f;
        const float s1 = sFrontBase - 0.024f;
        auto wsPoint = [&](float uRow, float vCol) -> Vector3 {
            const float s = lerpf(s0, s1, uRow);
            const StationEval e = evalStation(s);
            const float zFrac = -0.82f + 1.64f * vCol; // Leaves crisp painted A-pillars!
            const float absZ = std::abs(zFrac);
            const float z = zFrac * lerpf(e.wCrownSide * 1.01f, e.wUpper * 0.94f, uRow);
            const float yCurve = lerpf(e.yTopCenter, e.yCrownSide, absZ * absZ);
            return {e.x + 0.009f, yCurve + 0.008f, z};
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
        constexpr int kRwRows = 4;
        constexpr int kRwCols = 6;
        const float s0 = sRearBase + 0.030f;
        const float s1 = sRearTop - 0.024f;
        auto rwPoint = [&](float uRow, float vCol) -> Vector3 {
            const float s = lerpf(s0, s1, uRow);
            const StationEval e = evalStation(s);
            const float zFrac = -0.78f + 1.56f * vCol; // Leaves painted C-pillars / flying buttresses!
            const float absZ = std::abs(zFrac);
            const float z = zFrac * lerpf(e.wUpper * 0.92f, e.wCrownSide * 1.00f, uRow);
            const float yCurve = lerpf(e.yTopCenter, e.yCrownSide, absZ * absZ);
            return {e.x - 0.009f, yCurve + 0.008f, z};
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

    // Left & Right Side Windows (strictly inset inside painted A/C pillars — zero corner poke-out!)
    if (kind != VehicleKind::ClassicRoadster) {
        const float swStart = (kind == VehicleKind::FireTruck) ? 0.34f
                            : ((kind == VehicleKind::DeliveryVan || kind == VehicleKind::Ambulance) ? 0.14f : (sRearBase + 0.11f));
        const float swEnd   = sFrontBase - 0.11f;
        const float bPillarS = 0.5f * (swStart + swEnd) + 0.03f;
        constexpr int kSideSegs = 10;

        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            for (int seg = 0; seg < kSideSegs; ++seg) {
                const float sa = lerpf(swStart, swEnd, static_cast<float>(seg) / kSideSegs);
                const float sb = lerpf(swStart, swEnd, static_cast<float>(seg + 1) / kSideSegs);
                // Leave a crisp painted B-pillar gap on 4-door Sedans & SUVs
                if (!isCoupeGlass &&
                    kind != VehicleKind::DeliveryVan &&
                    kind != VehicleKind::Ambulance &&
                    kind != VehicleKind::FireTruck &&
                    std::abs(0.5f * (sa + sb) - bPillarS) < (swEnd - swStart) * 0.060f) {
                    continue;
                }
                const StationEval ea = evalStation(sa);
                const StationEval eb = evalStation(sb);
                if ((ea.yCrownSide - ea.yBelt) < 0.12f || (eb.yCrownSide - eb.yBelt) < 0.12f) continue;

                const float yaMid = lerpf(ea.yBelt, ea.yCrownSide, 0.54f);
                const float ybMid = lerpf(eb.yBelt, eb.yCrownSide, 0.54f);

                const Vector3 aBot{ea.x, ea.yBelt + 0.038f,      sgn * (lerpf(ea.wBelt, ea.wUpper, 0.10f) + 0.011f)};
                const Vector3 bBot{eb.x, eb.yBelt + 0.038f,      sgn * (lerpf(eb.wBelt, eb.wUpper, 0.10f) + 0.011f)};
                const Vector3 aMid{ea.x, yaMid,                  sgn * (ea.wUpper + 0.011f)};
                const Vector3 bMid{eb.x, ybMid,                  sgn * (eb.wUpper + 0.011f)};
                const Vector3 bTop{eb.x, eb.yCrownSide - 0.036f, sgn * (lerpf(eb.wCrownSide, eb.wUpper, 0.12f) + 0.011f)};
                const Vector3 aTop{ea.x, ea.yCrownSide - 0.036f, sgn * (lerpf(ea.wCrownSide, ea.wUpper, 0.12f) + 0.011f)};
                glassB.addQuadOutward(aBot, bBot, bMid, aMid, {0.0f, 0.25f, sgn});
                glassB.addQuadOutward(aMid, bMid, bTop, aTop, {0.0f, 0.45f, sgn});

                // Crisp dark rubber lower window beltline weather-strip trim conforming to station width!
                const Vector3 tBotA{ea.x, ea.yBelt + 0.014f, sgn * (lerpf(ea.wBelt, ea.wUpper, 0.04f) + 0.013f)};
                const Vector3 tBotB{eb.x, eb.yBelt + 0.014f, sgn * (lerpf(eb.wBelt, eb.wUpper, 0.04f) + 0.013f)};
                const Vector3 tTopB{eb.x, ea.yBelt + 0.040f, sgn * (lerpf(eb.wBelt, eb.wUpper, 0.10f) + 0.013f)};
                const Vector3 tTopA{ea.x, ea.yBelt + 0.040f, sgn * (lerpf(ea.wBelt, ea.wUpper, 0.10f) + 0.013f)};
                trimB.addQuadOutward(tBotA, tBotB, tTopB, tTopA, {0.0f, 0.15f, sgn});
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
            glassB.addQuadOutward({ea.x, ea.yTopCenter + 0.016f, -wa},
                                  {eb.x, eb.yTopCenter + 0.016f, -wb},
                                  {eb.x, eb.yTopCenter + 0.016f,  wb},
                                  {ea.x, ea.yTopCenter + 0.016f,  wa},
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

        if (kind != VehicleKind::DeliveryVan && kind != VehicleKind::Ambulance && kind != VehicleKind::FireTruck) {
            addStripeStrip(sFrontBase + 0.025f, 0.94f, 0.011f, 10);
        }
        if (kind != VehicleKind::ClassicRoadster && kind != VehicleKind::PoliceCruiser &&
            kind != VehicleKind::Ambulance && kind != VehicleKind::FireTruck) {
            addStripeStrip(sRearTop + 0.025f, sFrontTop - 0.025f, hasPanoRoof ? 0.024f : 0.011f, 8);
        }
        if (kind == VehicleKind::ClassicRoadster) {
            addStripeStrip(-0.94f, -0.39f, 0.011f, 8);
        } else if (kind != VehicleKind::OffroadPickup && sRearBase > -0.86f) {
            addStripeStrip(-0.94f, sRearBase - 0.025f, 0.011f, 6);
        }
    }

    // 3) Dark Wheel-House Liners, Contour-Matched Side Fender Brake Vents, Splitter, Grille, Skirts & Diffuser
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

                trimB.addQuadOutward({x0, y0, sgn * zInner0}, {x1, y1, sgn * zInner1},
                                     {x1, y1, sgn * zOuter1}, {x0, y0, sgn * zOuter0},
                                     {ax - 0.5f * (x0 + x1), wheelR - 0.5f * (y0 + y1), sgn * 0.25f});
                const float zBack = 0.5f * (zInner0 + zInner1);
                trimB.addTriOutward({ax, wheelR * 0.65f, sgn * zBack},
                                    {x0, y0, sgn * zInner0},
                                    {x1, y1, sgn * zInner1},
                                    {0.0f, 0.0f, sgn});
            }
        }

        // Contour-hugging flush front-fender aero vent behind front wheel arch (ZERO Z-fighting!)
        if (kind == VehicleKind::Hypercar || kind == VehicleKind::TrackPrototype ||
            kind == VehicleKind::SportsCoupe || kind == VehicleKind::JdmTuner ||
            kind == VehicleKind::MuscleCar || kind == VehicleKind::ClassicRoadster) {
            const float sVent0 = 0.17f;
            const float sVent1 = 0.30f;
            const StationEval ev0 = evalStation(sVent0);
            const StationEval ev1 = evalStation(sVent1);
            const float yLo0 = lerpf(ev0.ySill, ev0.yBelly, 0.55f);
            const float yHi0 = lerpf(ev0.yBelly, ev0.yBelt, 0.35f);
            const float yLo1 = lerpf(ev1.ySill, ev1.yBelly, 0.55f);
            const float yHi1 = lerpf(ev1.yBelly, ev1.yBelt, 0.35f);
            const float zLo0 = lerpf(ev0.wSill, ev0.wBelly, 0.55f) + 0.011f;
            const float zHi0 = lerpf(ev0.wBelly, ev0.wBelt, 0.35f) + 0.011f;
            const float zLo1 = lerpf(ev1.wSill, ev1.wBelly, 0.55f) + 0.011f;
            const float zHi1 = lerpf(ev1.wBelly, ev1.wBelt, 0.35f) + 0.011f;
            trimB.addQuadOutward({ev0.x, yLo0, sgn * zLo0}, {ev1.x, yLo1, sgn * zLo1},
                                 {ev1.x, yHi1, sgn * zHi1}, {ev0.x, yHi0, sgn * zHi0},
                                 {0.0f, 0.0f, sgn});
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

        if (kind == VehicleKind::TrackPrototype || kind == VehicleKind::Hypercar ||
            kind == VehicleKind::SportsCoupe || kind == VehicleKind::JdmTuner ||
            kind == VehicleKind::ClassicRoadster || kind == VehicleKind::MuscleCar) {
            for (int f = -2; f <= 2; ++f) {
                if (f == 0) continue;
                trimB.addBox({-halfL * 0.99f, er.yUnder + 0.06f, static_cast<float>(f) * (halfW * 0.20f)},
                             {0.10f, 0.12f, 0.025f});
            }
        }
        if (kind == VehicleKind::MuscleCar) {
            const StationEval em = evalStation(0.62f);
            trimB.addBox({em.x, em.yTopCenter + 0.05f, 0.0f}, {0.68f, 0.11f, 0.46f});
        }
        if (kind == VehicleKind::Hypercar || kind == VehicleKind::TrackPrototype) {
            const StationEval eh = evalStation(-0.22f);
            trimB.addBox({eh.x, eh.yBelly + 0.12f,  eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
            trimB.addBox({eh.x, eh.yBelly + 0.12f, -eh.wBelly * 0.96f}, {0.56f, 0.22f, 0.08f});
        }
        if (kind == VehicleKind::ArmoredSuv) {
            trimB.addBox({halfL + 0.07f, 0.56f, 0.0f}, {0.10f, 0.44f, halfW * 1.45f});
            const float rMidX = 0.5f * (sRearTop + sFrontTop) * halfL;
            const float rLen  = (sFrontTop - sRearTop) * halfL * 0.82f;
            for (int side = -1; side <= 1; side += 2) {
                const float rz = static_cast<float>(side) * (halfW * 0.58f);
                trimB.addBox({rMidX, roofPeakY + 0.045f, rz}, {rLen, 0.042f, 0.052f});
                trimB.addBox({rMidX - rLen * 0.42f, roofPeakY + 0.015f, rz}, {0.08f, 0.055f, 0.052f});
                trimB.addBox({rMidX + rLen * 0.42f, roofPeakY + 0.015f, rz}, {0.08f, 0.055f, 0.052f});
            }
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
            const float rMidX = 0.5f * (sRearTop + sFrontTop) * halfL;
            const float rLen  = (sFrontTop - sRearTop) * halfL * 0.80f;
            for (int side = -1; side <= 1; side += 2) {
                const float rz = static_cast<float>(side) * (halfW * 0.56f);
                trimB.addBox({rMidX, roofPeakY + 0.036f, rz}, {rLen, 0.038f, 0.044f});
                trimB.addBox({rMidX - rLen * 0.42f, roofPeakY + 0.010f, rz}, {0.075f, 0.050f, 0.044f});
                trimB.addBox({rMidX + rLen * 0.42f, roofPeakY + 0.010f, rz}, {0.075f, 0.050f, 0.044f});
            }
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
        if (target.depth.id > 0) {
            SetTextureFilter(target.depth, TEXTURE_FILTER_POINT);
            SetTextureWrap(target.depth, TEXTURE_WRAP_CLAMP);
        }
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
#if OC_IS_GLES3
    // On mobile GPUs default to High (PBR + 1024px real-time shadows direct-to-backbuffer at 60 FPS);
    // users can still toggle Ultra HDR Bloom or Performance live via the HUD GFX button.
    quality_ = GraphicsQuality::High;
#endif

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

    // 2) Depth-only shadow pass shader + directional depth FBO
    depthShader_ = LoadShaderFromMemory(kDepthVertShader, kDepthFragShader);
    shadowMap_ = createShadowMapFbo(kShadowMapResolution, kShadowMapResolution, shadowMapValid_);

    // 3) Procedural Atmospheric Sky Dome shader
    skyShader_ = LoadShaderFromMemory(kSkyVertShader, kSkyFragShader);
    locSkySunDir_    = GetShaderLocation(skyShader_, "sunDir");
    locSkyTimeHours_ = GetShaderLocation(skyShader_, "timeHours");
    locSkyDayFactor_ = GetShaderLocation(skyShader_, "dayFactor");
    locSkyAnimTime_  = GetShaderLocation(skyShader_, "animTime");
    locSkyWet_       = GetShaderLocation(skyShader_, "wetWeather");

    // 4) Full-screen HDR Bloom + ACES post-process shader (explicit VS for GLES 3.0 + GL 3.3 parity)
    postShader_ = LoadShaderFromMemory(kPostVertShader, kPostFragShader);
    locPostResolution_    = GetShaderLocation(postShader_, "resolution");
    locPostBloomStrength_ = GetShaderLocation(postShader_, "bloomStrength");
    locPostExposure_      = GetShaderLocation(postShader_, "exposure");
    locPostNitro_         = GetShaderLocation(postShader_, "nitroStrength");
    locPostEnableBloom_   = GetShaderLocation(postShader_, "enableBloom");

    // 5) Shared GPU-resident primitive, botanical foliage cluster & 16 sculpted smooth-normal automotive models
    cubeModel_           = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    cylModel_            = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));
    coneModel_           = LoadModelFromMesh(GenMeshCone(1.0f, 1.0f, 16));
    sphereModel_         = LoadModelFromMesh(GenMeshSphere(1.0f, 14, 16));
    foliageClusterModel_ = LoadModelFromMesh(buildFoliageClusterMesh());
    cypressModel_        = LoadModelFromMesh(buildCypressMesh());
    hipRoofModel_        = LoadModelFromMesh(buildHipRoofMesh());
    skyDomeModel_        = LoadModelFromMesh(GenMeshSphere(1.0f, 24, 32));
    wheelModel_          = LoadModelFromMesh(buildCenteredWheelMesh(24));
    sportRimModel_       = LoadModelFromMesh(buildSportRimMesh());
    ringModel_           = LoadModelFromMesh(buildHollowRingMesh(32, 0.87f));

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
    UnloadModel(foliageClusterModel_);
    UnloadModel(cypressModel_);
    UnloadModel(hipRoofModel_);
    UnloadModel(skyDomeModel_);
    UnloadModel(wheelModel_);
    UnloadModel(sportRimModel_);
    UnloadModel(ringModel_);
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
    cubeModel_.materials[0].shader           = shader;
    cylModel_.materials[0].shader            = shader;
    coneModel_.materials[0].shader           = shader;
    sphereModel_.materials[0].shader         = shader;
    foliageClusterModel_.materials[0].shader = shader;
    cypressModel_.materials[0].shader        = shader;
    hipRoofModel_.materials[0].shader        = shader;
    wheelModel_.materials[0].shader          = shader;
    sportRimModel_.materials[0].shader       = shader;
    ringModel_.materials[0].shader           = shader;
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
    Color base{18, 26, 44, 255};
    if (h >= 7.5f && h < 16.8f) {
        base = Color{138, 174, 212, 255};
    } else if (h >= 16.8f && h < 19.5f) {
        const float t = (h - 16.8f) / 2.7f;
        if (t < 0.5f) {
            const float k = t * 2.0f;
            base = Color{
                static_cast<unsigned char>(lerpf(138.0f, 224.0f, k)),
                static_cast<unsigned char>(lerpf(174.0f, 128.0f, k)),
                static_cast<unsigned char>(lerpf(212.0f, 94.0f, k)),
                255
            };
        } else {
            const float k = (t - 0.5f) * 2.0f;
            base = Color{
                static_cast<unsigned char>(lerpf(224.0f, 20.0f, k)),
                static_cast<unsigned char>(lerpf(128.0f, 26.0f, k)),
                static_cast<unsigned char>(lerpf(94.0f, 44.0f, k)),
                255
            };
        }
    } else if (h >= 5.2f && h < 7.5f) {
        const float k = (h - 5.2f) / 2.3f;
        base = Color{
            static_cast<unsigned char>(lerpf(20.0f, 138.0f, k)),
            static_cast<unsigned char>(lerpf(26.0f, 174.0f, k)),
            static_cast<unsigned char>(lerpf(44.0f, 212.0f, k)),
            255
        };
    }
    if (world.isRainWeather()) {
        const float day = world.daylightFactor();
        const float stormR = lerpf(24.0f, 114.0f, day);
        const float stormG = lerpf(30.0f, 124.0f, day);
        const float stormB = lerpf(42.0f, 138.0f, day);
        base.r = static_cast<unsigned char>(lerpf(static_cast<float>(base.r), stormR, 0.78f));
        base.g = static_cast<unsigned char>(lerpf(static_cast<float>(base.g), stormG, 0.78f));
        base.b = static_cast<unsigned char>(lerpf(static_cast<float>(base.b), stormB, 0.78f));
    }
    return base;
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
#if OC_IS_GLES3
    constexpr float kOrthoSpan = 122.0f;
#else
    constexpr float kOrthoSpan = 132.0f;
#endif
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
    lightCam.position   = Vector3Add(snappedCenter, Vector3Scale(toLight, 185.0f));
    lightCam.up         = {0.0f, 1.0f, 0.0f};
    lightCam.fovy       = kOrthoSpan;
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    return lightCam;
}

void Renderer3D::updateShaderUniforms(const GameWorld& world, const Camera3D& camera,
                                      Color skyCol, const Vector3& lightDir,
                                      const Matrix& lightVP, bool shadowsActive) {
    shadowsActive_ = shadowsActive;
    const float day = world.daylightFactor();
    const float h = world.timeOfDayHours();
    const float wetF = world.isRainWeather() ? 1.0f : 0.0f;
    const float golden = clampf(
        std::exp(-std::pow((h - 6.4f) / 1.1f, 2.0f)) +
        std::exp(-std::pow((h - 18.0f) / 1.3f, 2.0f)), 0.0f, 1.0f) * (1.0f - wetF * 0.65f);
    const float nightFactor = clampf(1.0f - day * 1.18f, 0.0f, 1.0f);

    // Physically grounded sunlight & atmospheric hemisphere fill (with overcast storm attenuation in rain!)
    float sunCol[3] = {
        lerpf(0.16f, 1.24f, day),
        lerpf(0.21f, 1.18f, day),
        lerpf(0.36f, 1.07f, day)
    };
    if (golden > 0.05f) {
        sunCol[0] = lerpf(sunCol[0], 1.56f, golden * 0.85f);
        sunCol[1] = lerpf(sunCol[1], 0.78f, golden * 0.85f);
        sunCol[2] = lerpf(sunCol[2], 0.34f, golden * 0.85f);
    }
    if (wetF > 0.01f) {
        // Thick rain clouds diffuse direct sunlight by ~52% and cool the beam
        sunCol[0] *= lerpf(1.0f, 0.44f, wetF);
        sunCol[1] *= lerpf(1.0f, 0.48f, wetF);
        sunCol[2] *= lerpf(1.0f, 0.56f, wetF);
    }

    float skyAmb[3] = {
        lerpf(0.050f, 0.25f, day) + golden * 0.07f,
        lerpf(0.068f, 0.32f, day),
        lerpf(0.130f, 0.45f, day)
    };
    if (wetF > 0.01f) {
        skyAmb[0] = lerpf(skyAmb[0], lerpf(0.055f, 0.30f, day), wetF * 0.75f);
        skyAmb[1] = lerpf(skyAmb[1], lerpf(0.068f, 0.34f, day), wetF * 0.75f);
        skyAmb[2] = lerpf(skyAmb[2], lerpf(0.105f, 0.40f, day), wetF * 0.75f);
    }
    const float gndAmb[3] = {
        lerpf(0.028f, 0.145f, day) * lerpf(1.0f, 0.82f, wetF),
        lerpf(0.032f, 0.145f, day) * lerpf(1.0f, 0.85f, wetF),
        lerpf(0.048f, 0.155f, day) * lerpf(1.0f, 0.90f, wetF)
    };
    const float fogCol[3] = {
        static_cast<float>(skyCol.r) / 255.0f,
        static_cast<float>(skyCol.g) / 255.0f,
        static_cast<float>(skyCol.b) / 255.0f
    };
    const float horizCol[3] = {
        clampf(fogCol[0] * 1.06f + golden * 0.16f, 0.0f, 1.0f),
        clampf(fogCol[1] * 1.02f, 0.0f, 1.0f),
        clampf(fogCol[2] * 1.01f, 0.0f, 1.0f)
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
                {1650.0f, 1650.0f, 1650.0f}, WHITE);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer3D::renderFrame(const GameWorld& world, const Camera3D& camera,
                             bool firstPerson, float adsBlend, bool lookBack) {
    const int sw = std::max(1, GetScreenWidth());
    const int sh = std::max(1, GetScreenHeight());
    const Color skyCol = computeSkyColor(world);
    const Vector3 lightDir = computeSunDirection(world);
    // Anchor shadow map focus 14m ahead of camera position along view XZ for ultra-crisp near-field shadows!
    const Vec2 camPos2D{camera.position.x, camera.position.z};
    const Vec2 camDir2D = Vec2{camera.target.x - camera.position.x, camera.target.z - camera.position.z}.normalized();
    const Vec2 focus = camPos2D + camDir2D * 14.0f;

    const bool useShadows = (quality_ != GraphicsQuality::Performance) && shadowMapValid_ &&
                            (depthShader_.id > 0) && (worldShader_.id > 0);
#if OC_IS_GLES3
    // On mobile GPUs, reserve full-screen HDR post-FBO for Ultra quality to avoid double-fillrate stalls
    const bool usePostFbo = (quality_ == GraphicsQuality::Ultra) && (postShader_.id > 0);
#else
    const bool usePostFbo = (quality_ != GraphicsQuality::Performance) && (postShader_.id > 0);
#endif
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
    rlSetClipPlanes(0.16, 2450.0);

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
        const float nightBoost = world.isNightTime() ? 0.50f : 0.24f;
        const float exposure = 1.00f;
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
    const float vehCull = shadowPass ? (135.0f * 135.0f) : (290.0f * 290.0f);
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
        const bool playerAimOrFire = pl.aiming || pl.muzzleFlashTimer > 0.0f;
        drawCharacter3D(pl.pos, baseY, pl.facingYaw, pl.walkAnimPhase,
                        getPlayerTopColor(pl.currentOutfit, pl.characterCustom.customTopColorIdx),
                        getPlayerBottomColor(pl.currentOutfit, pl.characterCustom.customBottomColorIdx),
                        getPlayerSkinColor(pl.characterCustom.skinToneIdx),
                        !pl.wasted, pl.currentWeapon != WeaponId::Fists,
                        pl.currentWeapon, pl.muzzleFlashTimer, pl.aimPitch,
                        pl.characterCustom.glassesStyle > 0, pl.currentOutfit, shadowPass,
                        surfY, !pl.onGround, false, false, false,
                        &pl.characterCustom, &wSlot, nullptr, playerAimOrFire);

        // Live 3rd-person tactical laser sight beam + pinpoint world dot when aiming or firing!
        if (!shadowPass && !pl.wasted && pl.currentWeapon != WeaponId::Fists &&
            playerAimOrFire && wSlot.laserSightColor > 0 && pl.reloadTimer <= 0.0f) {
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
        const bool npcCombat = (npc.behavior == NpcBehavior::CombatEngage || npc.muzzleFlashTimer > 0.0f);
        const bool isPanicking = (npc.role == NpcRole::Civilian && npc.behavior == NpcBehavior::PanicFlee);
        CharacterCustomState npcCustom{};
        npcCustom.hairOrHatStyle = npc.hairStyle;
        npcCustom.hairColorIdx = npc.hairColorIdx;
        npcCustom.glassesStyle = isCop ? 1 : (npc.role == NpcRole::Gangster ? 1 : npc.glassesStyle);
        npcCustom.backGearStyle = (npc.role == NpcRole::Civilian) ? npc.backGearStyle : 0;

        NpcVisualExtra extra{};
        extra.heightScale = (npc.role == NpcRole::Civilian) ? npc.heightScale : 1.0f;
        extra.headLookYaw = npc.headLookYaw;
        extra.fallProgress = npc.alive ? 0.0f : npc.fallProgress;
        extra.heldProp = (npc.role == NpcRole::Civilian && !isPanicking) ? npc.heldProp : NpcHeldProp::None;
        extra.activity = (npc.role == NpcRole::Civilian) ? npc.activity : NpcActivity::None;
        extra.holdingUmbrella = npc.holdingUmbrella && npc.alive;
        extra.umbrellaColor = npc.umbrellaColor;
        extra.nightMode = nightMode;

        drawCharacter3D(npc.pos, gY, npc.facingYaw, npc.animPhase,
                        npc.outfitTop, npc.outfitBottom, npc.skinColor,
                        npc.alive, isArmed, npc.weapon, npc.muzzleFlashTimer,
                        0.0f, (npcCustom.glassesStyle > 0),
                        (npc.role == NpcRole::Civilian) ? npc.outfitStyle : OutfitId::StreetCasual,
                        shadowPass, gY, false, isPanicking, isCop, npc.isSwat,
                        &npcCustom, nullptr, &extra, npcCombat);
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
    const float halfSpan = CityMap::kHalfCitySpan; // 702.0m

    // Animated coastal ocean water plane (MatKind::OceanWater, 5200m x 5200m)
    drawModelStyled(cubeModel_, {0.0f, -1.35f, 120.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {5200.0f, 1.0f, 5200.0f}, Color{22, 92, 138, 255},
                    0.07f, 0.05f, 0.0f, MatKind::OceanWater);

    // Coastal California sandy perimeter around the 16x16 metropolis (MatKind::BeachSand)
    drawModelStyled(cubeModel_, {0.0f, -0.52f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {halfSpan * 2.04f, 0.88f, halfSpan * 2.04f},
                    Color{202, 184, 144, 255}, 0.88f, 0.0f, 0.0f, MatKind::BeachSand);

    // Wide walkable Western Sunset Beach sandbar (x in [-746, -686], z in [-684, 684])
    drawModelStyled(cubeModel_, {-716.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {60.0f, 0.48f, 1368.0f}, Color{212, 194, 152, 255},
                    0.86f, 0.0f, 0.0f, MatKind::BeachSand);
    // Damp shoreline wet sand strip meeting the rolling Pacific surf
    drawModelStyled(cubeModel_, {-745.5f, -0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {11.0f, 0.36f, 1368.0f}, Color{162, 144, 110, 255},
                    0.30f, 0.05f, 0.0f, MatKind::BeachSand);

    // Coastal promenade concrete seawall apron
    drawModelStyled(cubeModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {halfSpan * 1.98f, 0.42f, halfSpan * 1.98f},
                    Color{136, 138, 140, 255}, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

    // 3 Iconic California Blue-and-Timber Lifeguard Towers spaced cleanly along Sunset Beach
    const float lgZs[3] = {34.0f, -240.0f, 260.0f};
    for (float lz : lgZs) {
        const float lx = -718.0f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cubeModel_, {lx + sx * 1.25f, 1.05f, lz + sz * 1.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 2.10f, 0.18f}, Color{154, 126, 94, 255}, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
            }
        }
        drawModelStyled(cubeModel_, {lx, 2.15f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.6f, 0.18f, 3.6f}, Color{162, 130, 92, 255}, 0.80f, 0.0f, 0.0f, MatKind::BarkWood);
        drawModelStyled(cubeModel_, {lx, 3.25f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.75f, 2.10f, 2.75f}, Color{68, 124, 166, 255}, 0.62f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {lx, 3.45f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.84f, 0.68f, 2.84f}, Color{22, 34, 48, 255}, 0.08f, 0.2f, 0.0f, MatKind::CarGlass);
        drawModelStyled(cubeModel_, {lx, 4.42f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.45f, 0.24f, 3.45f}, Color{216, 218, 222, 255}, 0.58f, 0.0f, 0.0f, MatKind::Standard);
    }

    // 110m Sunset Bay Weathered Timber Ocean Pier (x in [-796, -686], z in [-7.6, +7.6])
    {
        const Color woodDeck{132, 102, 72, 255};
        const Color woodBeam{88, 64, 44, 255};
        const Color cedarPost{138, 108, 78, 255};
        const Color steelCable{118, 126, 134, 255};

        // Main weathered timber plank deck + heavy dark cross-beams
        drawModelStyled(cubeModel_, {-741.0f, 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {110.0f, 0.22f, 15.2f}, woodDeck, 0.84f, 0.0f, 0.0f, MatKind::BarkWood);
        drawModelStyled(cubeModel_, {-741.0f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {110.2f, 0.18f, 15.6f}, woodBeam, 0.88f, 0.0f, 0.0f, MatKind::BarkWood);

        // Timber ocean pilings + weathered cedar posts + vintage pier lanterns
        for (int p = 0; p < 13; ++p) {
            const float px = -692.0f - static_cast<float>(p) * 8.4f;
            for (int side = -1; side <= 1; side += 2) {
                const float pz = static_cast<float>(side) * 7.1f;
                drawModelStyled(cylModel_, {px, -1.55f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.28f, 1.75f, 0.28f}, woodBeam, 0.90f, 0.0f, 0.0f, MatKind::BarkWood);
                drawModelStyled(cubeModel_, {px, 0.72f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.20f, 0.96f, 0.20f}, cedarPost, 0.84f, 0.0f, 0.0f, MatKind::BarkWood);

                if (p % 2 == 1) {
                    drawModelStyled(cylModel_, {px, 1.18f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.075f, 2.25f, 0.075f}, Color{42, 46, 52, 255}, 0.38f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {px, 3.52f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.22f, 0.22f, 0.22f},
                                    night ? Color{255, 220, 152, 255} : Color{216, 218, 222, 255},
                                    0.18f, 0.0f, night ? 2.2f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
            }
        }
        // Continuous weathered cedar top cap rail & galvanized steel mid safety cables
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {-741.0f, 1.12f, static_cast<float>(side) * 7.1f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {109.5f, 0.11f, 0.16f}, cedarPost, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
            drawModelStyled(cubeModel_, {-741.0f, 0.72f, static_cast<float>(side) * 7.1f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {109.5f, 0.045f, 0.055f}, steelCable, 0.34f, 0.78f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-741.0f, 0.44f, static_cast<float>(side) * 7.1f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {109.5f, 0.035f, 0.045f}, steelCable, 0.34f, 0.78f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {-795.6f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.12f, 14.4f}, cedarPost, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);

        // Pier-end coastal sunset gazebo pavilion
        const float gx = -788.0f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cylModel_, {gx + sx * 3.4f, 0.24f, sz * 3.8f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.17f, 3.6f, 0.17f}, cedarPost, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
            }
        }
        drawModelStyled(coneModel_, {gx, 3.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {6.4f, 2.2f, 6.4f}, Color{56, 86, 108, 255}, 0.62f, 0.10f, 0.0f, MatKind::Standard);
    }

    // North Bay Lighthouse Peninsula Headland (x in [-130, +130], z in [815, 954])
    {
        const float penCenterZ = 884.5f;
        // Sandy coastal shoal & granite rocky shoreline embankment
        drawModelStyled(cubeModel_, {0.0f, -0.52f, penCenterZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {272.0f, 0.88f, 148.0f}, Color{198, 180, 140, 255},
                        0.88f, 0.0f, 0.0f, MatKind::BeachSand);
        drawModelStyled(cubeModel_, {0.0f, -0.18f, penCenterZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {258.0f, 0.48f, 138.0f}, Color{118, 120, 122, 255},
                        0.84f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        // Coastal headland meadow turf (top at y = +0.10m)
        drawModelStyled(cubeModel_, {0.0f, 0.06f, penCenterZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {246.0f, 0.08f, 126.0f}, Color{68, 102, 54, 255},
                        0.86f, 0.0f, 0.0f, MatKind::FoliageGrass);
        // Scenic lighthouse coastal overlook promenade & stone plaza at north tip (z = 928)
        drawModelStyled(cylModel_, {0.0f, 0.07f, 928.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {19.0f, 0.06f, 19.0f}, Color{148, 146, 140, 255},
                        0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        drawModelStyled(cubeModel_, {0.0f, 0.10f, 912.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {5.4f, 0.04f, 18.0f}, Color{148, 146, 140, 255},
                        0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        // Natural coastal granite boulder outcrops framing the northern rocky headland shore
        const Color rockCol{102, 100, 96, 255};
        for (int bi = -5; bi <= 5; ++bi) {
            const float bx = static_cast<float>(bi) * 21.5f + std::sin(static_cast<float>(bi) * 2.1f) * 4.5f;
            const float bz = 951.0f - std::abs(static_cast<float>(bi)) * 1.4f;
            const float br = 3.2f + std::fmod(std::abs(static_cast<float>(bi) * 1.7f), 2.2f);
            drawModelStyled(foliageClusterModel_, {bx, 0.25f, bz}, {0.0f, 1.0f, 0.0f},
                            static_cast<float>(bi) * 52.0f, {br * 1.45f, br * 0.62f, br * 1.15f},
                            rockCol, 0.88f, 0.04f, 0.0f, MatKind::SidewalkConcrete);
        }
    }
}

void Renderer3D::drawRoadNetwork(const GameWorld& world, const Vec2& focus) {
    const auto& city = world.city();
    const float roadOuterSpan = (city.roadCoord(CityMap::kRoadCount - 1) - city.roadCoord(0)) + CityMap::kRoadWidth;

    // 1) Seamless city-wide asphalt roadbed at y = 0.00m (analytic markings & crosswalks rendered in shader!)
    drawModelStyled(cubeModel_, {0.0f, -0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {roadOuterSpan, 0.08f, roadOuterSpan}, Color{52, 55, 60, 255},
                    0.50f, 0.04f, 0.0f, MatKind::RoadAsphalt);

    // 2) Raised City Block Sidewalk Plazas (8cm physical granite curb + weathered concrete sidewalk)
    const Color graniteCurb{108, 110, 114, 255};
    const Color sidewalkTile{142, 140, 134, 255};
    const float halfRoad = CityMap::kRoadWidth * 0.5f;

    for (int bz = 0; bz < CityMap::kBlockCount; ++bz) {
        for (int bx = 0; bx < CityMap::kBlockCount; ++bx) {
            const float x0 = city.roadCoord(bx) + halfRoad;
            const float x1 = city.roadCoord(bx + 1) - halfRoad;
            const float z0 = city.roadCoord(bz) + halfRoad;
            const float z1 = city.roadCoord(bz + 1) - halfRoad;
            const Vec2 bc{(x0 + x1) * 0.5f, (z0 + z1) * 0.5f};
            if ((bc - focus).lengthSq() > 410.0f * 410.0f) continue;

            const float bw = x1 - x0;
            const float bd = z1 - z0;

            // Bevelled granite curb rim
            drawModelStyled(cubeModel_, {bc.x, 0.035f, bc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {bw, 0.07f, bd}, graniteCurb, 0.72f, 0.04f, 0.0f, MatKind::Standard);
            // Weathered concrete sidewalk surface (top at y = 0.08m)
            drawModelStyled(cubeModel_, {bc.x, 0.045f, bc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {bw - 0.36f, 0.07f, bd - 0.36f}, sidewalkTile,
                            0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        }
    }

    // 3) Outer perimeter boulevard sidewalks along the 4 coastal ring roads (split cleanly at North Bridge x in [-9, +9])
    const float ringEdge = roadOuterSpan * 0.5f;
    const float outerW = 7.5f;
    const float fullRing = roadOuterSpan + outerW * 2.0f;
    // South outer sidewalk
    drawModelStyled(cubeModel_, {0.0f, 0.04f, -ringEdge - outerW * 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {fullRing, 0.08f, outerW}, sidewalkTile, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    // North outer sidewalk (left & right of Golden Bay Suspension Bridge road mouth!)
    const float northHalfLen = (fullRing * 0.5f) - halfRoad;
    const float northCenterOffset = halfRoad + northHalfLen * 0.5f;
    drawModelStyled(cubeModel_, {-northCenterOffset, 0.04f, ringEdge + outerW * 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {northHalfLen, 0.08f, outerW}, sidewalkTile, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    drawModelStyled(cubeModel_, { northCenterOffset, 0.04f, ringEdge + outerW * 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {northHalfLen, 0.08f, outerW}, sidewalkTile, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    // West & East outer sidewalks
    drawModelStyled(cubeModel_, {-ringEdge - outerW * 0.5f, 0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {outerW, 0.08f, roadOuterSpan}, sidewalkTile, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
    drawModelStyled(cubeModel_, { ringEdge + outerW * 0.5f, 0.04f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {outerW, 0.08f, roadOuterSpan}, sidewalkTile, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

    // 4) Golden Bay Suspension Bridge Arched Drivable Roadway (z = 681..815m) & North Bay Peninsula Road!
    const Vec2 bridgeMid{0.0f, 748.0f};
    if ((bridgeMid - focus).lengthSq() < 560.0f * 560.0f) {
        constexpr int kBridgeSegs = 16;
        const float zStart = CityMap::kBridgeStartZ;
        const float zEnd   = CityMap::kBridgeEndZ;
        const float stepZ  = (zEnd - zStart) / static_cast<float>(kBridgeSegs);
        const Color steelBarrier{122, 128, 136, 255};
        for (int i = 0; i < kBridgeSegs; ++i) {
            const float z0 = zStart + static_cast<float>(i) * stepZ;
            const float z1 = z0 + stepZ;
            const float y0 = city.bridgeDeckElevationAt({0.0f, z0});
            const float y1 = city.bridgeDeckElevationAt({0.0f, z1});
            const float mz = 0.5f * (z0 + z1);
            const float my = 0.5f * (y0 + y1);
            const float dz = z1 - z0;
            const float dy = y1 - y0;
            const float segLen = std::sqrt(dz * dz + dy * dy);
            const float pitchDeg = -std::atan2(dy, dz) * kRadToDeg;

            rlPushMatrix();
            rlTranslatef(0.0f, my, mz);
            rlRotatef(pitchDeg, 1.0f, 0.0f, 0.0f);
            // Smooth asphalt bridge roadway (top surface matches bridgeDeckElevationAt!)
            drawModelStyled(cubeModel_, {0.0f, -0.09f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {18.0f, 0.18f, segLen + 0.16f}, Color{52, 55, 60, 255},
                            0.50f, 0.04f, 0.0f, MatKind::RoadAsphalt);
            // Raised concrete pedestrian walkways & outer steel crash barriers on both bridge flanks
            for (int side = -1; side <= 1; side += 2) {
                const float sgn = static_cast<float>(side);
                drawModelStyled(cubeModel_, {sgn * 10.2f, 0.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.4f, 0.24f, segLen + 0.14f}, sidewalkTile,
                                0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
                drawModelStyled(cubeModel_, {sgn * 9.08f, 0.38f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 0.72f, segLen + 0.12f}, steelBarrier,
                                0.52f, 0.35f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {sgn * 11.42f, 0.56f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.16f, 1.05f, segLen + 0.12f}, Color{168, 52, 40, 255},
                                0.44f, 0.52f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();
        }
    }

    // North Bay Peninsula Paved Lighthouse Approach Road & Scenic Roundabout (z = 815..896m)
    const Vec2 penRoadMid{0.0f, 865.0f};
    if ((penRoadMid - focus).lengthSq() < 520.0f * 520.0f) {
        drawModelStyled(cubeModel_, {0.0f, 0.04f, 855.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {18.0f, 0.16f, 82.0f}, Color{52, 55, 60, 255},
                        0.50f, 0.04f, 0.0f, MatKind::RoadAsphalt);
        drawModelStyled(cylModel_, {0.0f, 0.03f, 896.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {15.5f, 0.10f, 15.5f}, Color{54, 57, 62, 255},
                        0.52f, 0.04f, 0.0f, MatKind::RoadAsphalt);
        drawModelStyled(cylModel_, {0.0f, 0.09f, 896.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {5.2f, 0.12f, 5.2f}, Color{72, 108, 56, 255},
                        0.85f, 0.0f, 0.0f, MatKind::FoliageGrass);
    }
}

void Renderer3D::drawBuildingsAndParks(const GameWorld& world, const Vec2& focus, bool shadowPass) {
    const auto& city = world.city();
    const float animT = static_cast<float>(GetTime());
    const bool night = world.isNightTime();

    if (!shadowPass) {
        for (const auto& park : city.parks()) {
            const Vec2 c = park.bounds.center();
            if ((c - focus).lengthSq() > 410.0f * 410.0f) continue;
            const Vec2 he = park.bounds.halfExtents();
            drawModelStyled(cubeModel_, {c.x, 0.09f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.0f, 0.06f, he.z * 2.0f}, toRayColor(park.grassColor),
                            0.84f, 0.0f, 0.0f, MatKind::FoliageGrass);
        }

        // Grand Central Park Reflective Lake(s) with Dressed Stone Embankment & Arched Footbridge!
        for (const auto& lake : city.lakes()) {
            const Vec2 lc = lake.bounds.center();
            if ((lc - focus).lengthSq() > 360.0f * 360.0f) continue;
            const Vec2 he = lake.bounds.halfExtents();
            const Color wallStone{138, 136, 130, 255};
            const Color timberWood{118, 86, 58, 255};

            // Recessed calm reflective lake surface cleanly above park grass (top y=0.145m vs grass y=0.120m -> ZERO Z-fighting!)
            drawModelStyled(cubeModel_, {lc.x, 0.095f, lc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.0f, 0.10f, he.z * 2.0f}, Color{22, 66, 82, 255},
                            0.05f, 0.06f, 0.0f, MatKind::OceanWater);
            // Dressed limestone masonry retaining rim around the lake perimeter
            drawModelStyled(cubeModel_, {lc.x, 0.17f, lake.bounds.min.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.0f + 1.1f, 0.28f, 0.85f}, wallStone, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
            drawModelStyled(cubeModel_, {lc.x, 0.17f, lake.bounds.max.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.0f + 1.1f, 0.28f, 0.85f}, wallStone, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
            drawModelStyled(cubeModel_, {lake.bounds.min.x, 0.17f, lc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.85f, 0.28f, he.z * 2.0f}, wallStone, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
            drawModelStyled(cubeModel_, {lake.bounds.max.x, 0.17f, lc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.85f, 0.28f, he.z * 2.0f}, wallStone, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

            // Arched timber & stone pedestrian footbridge spanning North-South across lake center (open architectural balustrade!)
            const float spanZ = he.z * 2.0f + 2.4f;
            for (int seg = 0; seg < 7; ++seg) {
                const float u0 = static_cast<float>(seg) / 7.0f;
                const float u1 = static_cast<float>(seg + 1) / 7.0f;
                const float z0 = (lc.z - spanZ * 0.5f) + u0 * spanZ;
                const float z1 = (lc.z - spanZ * 0.5f) + u1 * spanZ;
                const float y0 = 0.26f + 1.18f * std::sin(u0 * kPi);
                const float y1 = 0.26f + 1.18f * std::sin(u1 * kPi);
                const float dz = z1 - z0;
                const float dy = y1 - y0;
                const float len = std::sqrt(dz * dz + dy * dy);
                const float pitch = -std::atan2(dy, dz) * kRadToDeg;
                rlPushMatrix();
                rlTranslatef(lc.x, 0.5f * (y0 + y1), 0.5f * (z0 + z1));
                rlRotatef(pitch, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.2f, 0.16f, len + 0.08f}, timberWood, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                for (int side = -1; side <= 1; side += 2) {
                    const float sx = static_cast<float>(side) * 1.50f;
                    // Open top handrail + mid rail + vertical timber posts (lets reflective lake shine through!)
                    drawModelStyled(cubeModel_, {sx, 0.86f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.08f, len + 0.08f}, timberWood, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                    drawModelStyled(cubeModel_, {sx, 0.46f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, 0.055f, len + 0.06f}, Color{86, 62, 42, 255}, 0.84f, 0.0f, 0.0f, MatKind::BarkWood);
                    drawModelStyled(cubeModel_, {sx, 0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.12f, 0.86f, 0.12f}, timberWood, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                }
                rlPopMatrix();
            }
        }

        // Grand Central Park Fountain Plaza (East Park Block around {-42, -42})
        const ParkPatch* centralPark = nullptr;
        for (const auto& park : city.parks()) {
            const Vec2 pc = park.bounds.center();
            const Vec2 phe = park.bounds.halfExtents();
            if (phe.x > 22.0f && phe.z > 22.0f && std::abs(pc.x + 42.0f) < 10.0f && std::abs(pc.z + 42.0f) < 10.0f) {
                centralPark = &park;
                break;
            }
        }
        if (centralPark != nullptr) {
            const Vec2 fc = centralPark->bounds.center();
            const Vec2 he = centralPark->bounds.halfExtents();
            if ((fc - focus).lengthSq() < 260.0f * 260.0f) {
                const Color stonePath{156, 152, 144, 255};
                const Color marbleLight{192, 190, 184, 255};
                const Color marbleMid{162, 160, 156, 255};

                // Crossed crushed-stone park footpaths + circular stone plaza apron
                drawModelStyled(cubeModel_, {fc.x, 0.115f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.98f, 0.03f, 3.6f}, stonePath, 0.84f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
                drawModelStyled(cubeModel_, {fc.x, 0.115f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.6f, 0.03f, he.z * 1.98f}, stonePath, 0.84f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
                drawModelStyled(cylModel_, {fc.x, 0.105f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {9.1f, 0.04f, 9.1f}, stonePath, 0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);

                // Multi-tiered Beaux-Arts carved limestone fountain basin & upper pedestal bowl
                drawModelStyled(cylModel_, {fc.x, 0.12f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.85f, 0.56f, 5.85f}, marbleMid, 0.68f, 0.03f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 0.64f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {6.05f, 0.12f, 6.05f}, marbleLight, 0.62f, 0.03f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 0.56f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.15f, 0.16f, 5.15f}, Color{36, 118, 138, 255},
                                0.05f, 0.06f, 0.0f, MatKind::OceanWater);
                // Center fluted pedestal + mid stone catch-basin
                drawModelStyled(cylModel_, {fc.x, 0.62f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.15f, 2.75f, 1.15f}, marbleLight, 0.62f, 0.04f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 2.05f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.35f, 0.24f, 2.35f}, marbleLight, 0.62f, 0.04f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {fc.x, 2.26f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.05f, 0.08f, 2.05f}, Color{42, 126, 146, 255},
                                0.06f, 0.06f, 0.0f, MatKind::OceanWater);

                // Realistic subtle aerated water plume & parabolic droplets (non-emissive in daylight!)
                const float plumeH = 1.45f + 0.15f * std::sin(animT * 5.2f);
                const Color sprayCol{216, 228, 236, 200};
                const MatKind sprayMat = night ? MatKind::EmissiveGlow : MatKind::Standard;
                const float sprayEm = night ? 0.55f : 0.0f;
                drawModelStyled(coneModel_, {fc.x, 3.28f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, plumeH, 0.26f}, sprayCol, 0.22f, 0.05f, sprayEm, sprayMat);
                drawModelStyled(sphereModel_, {fc.x, 3.30f + plumeH * 0.85f, fc.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.34f, 0.30f, 0.34f}, sprayCol, 0.22f, 0.05f, sprayEm, sprayMat);

                for (int j = 0; j < 6; ++j) {
                    const float ang = static_cast<float>(j) * (kTwoPi / 6.0f);
                    const float ca = std::cos(ang);
                    const float sa = std::sin(ang);
                    for (int seg = 0; seg < 5; ++seg) {
                        const float t = std::fmod((static_cast<float>(seg) * 0.20f) + animT * 0.75f, 1.0f);
                        const float r = 1.15f + t * 2.85f;
                        const float y = 2.32f + 1.38f * t - 2.82f * t * t;
                        if (y < 0.74f) continue;
                        const float sz = 0.09f + 0.04f * std::sin(t * kPi);
                        drawModelStyled(sphereModel_, {fc.x + ca * r, y, fc.z + sa * r},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {sz, sz * 1.15f, sz},
                                        sprayCol, 0.18f, 0.05f, sprayEm, sprayMat);
                    }
                    if (night) {
                        drawModelStyled(sphereModel_, {fc.x + ca * 3.85f, 0.68f, fc.z + sa * 3.85f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.18f, 0.08f, 0.18f},
                                        Color{255, 220, 152, 255}, 0.1f, 0.0f, 1.6f, MatKind::EmissiveGlow);
                    }
                }

                // 4 Manicured Diagonal Ornamental Flowerbed Planters (natural muted botanical hues)
                static constexpr Color kFlowerColors[4] = {
                    {174, 72, 82, 255},
                    {194, 154, 62, 255},
                    {128, 92, 148, 255},
                    {188, 108, 62, 255}
                };
                int fbIdx = 0;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        const float fx = fc.x + static_cast<float>(sx) * 5.45f;
                        const float fz = fc.z + static_cast<float>(sz) * 5.45f;
                        drawModelStyled(cylModel_, {fx, 0.12f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {1.08f, 0.32f, 1.08f}, marbleMid, 0.76f, 0.03f, 0.0f, MatKind::Standard);
                        drawModelStyled(cylModel_, {fx, 0.38f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.92f, 0.12f, 0.92f}, Color{54, 96, 52, 255}, 0.84f, 0.0f, 0.0f, MatKind::FoliageGrass);
                        const Color flCol = kFlowerColors[fbIdx++ % 4];
                        for (int b = 0; b < 5; ++b) {
                            const float ba = static_cast<float>(b) * (kTwoPi / 5.0f) + 0.4f;
                            const float br = (b == 0) ? 0.0f : 0.48f;
                            drawModelStyled(foliageClusterModel_, {fx + std::cos(ba) * br, 0.56f, fz + std::sin(ba) * br},
                                            {0.0f, 1.0f, 0.0f}, static_cast<float>(b) * 72.0f, {0.24f, 0.18f, 0.24f},
                                            flCol, 0.72f, 0.0f, 0.0f, MatKind::FoliageGrass);
                        }
                    }
                }

                // 4 Vintage Cast-Iron Park Globe Lamp Posts flanking the fountain plaza
                const float lampOffsets[4][2] = {{-8.7f, -2.5f}, {8.7f, 2.5f}, {-2.5f, 8.7f}, {2.5f, -8.7f}};
                for (int l = 0; l < 4; ++l) {
                    const float lx = fc.x + lampOffsets[l][0];
                    const float lz = fc.z + lampOffsets[l][1];
                    drawModelStyled(cylModel_, {lx, 0.12f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.16f, 0.45f, 0.16f}, Color{36, 40, 44, 255}, 0.48f, 0.68f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {lx, 0.55f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.068f, 3.15f, 0.068f}, Color{40, 44, 50, 255}, 0.44f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {lx, 3.82f, lz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.23f, 0.23f, 0.23f}, night ? Color{255, 224, 158, 255} : Color{216, 220, 224, 255},
                                    0.18f, 0.0f, night ? 2.2f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
            }
        }
    }

    // Drive-through GLOBE OIL Gas Station Canopies (elevated roof, zero ground blocker!)
    for (const auto& can : city.canopies()) {
        const Vec2 cc = can.bounds.center();
        if ((cc - focus).lengthSq() > 320.0f * 320.0f) continue;
        const Vec2 he = can.bounds.halfExtents();
        const float roofThick = 0.72f;

        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cylModel_, {cc.x + static_cast<float>(sx) * 5.2f, 0.08f, cc.z + static_cast<float>(sz) * 3.3f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, can.clearanceHeight, 0.24f}, Color{178, 182, 188, 255},
                                0.40f, 0.68f, 0.0f, MatKind::Standard);
            }
        }
        drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight + roofThick * 0.5f, cc.z},
                        {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.0f, roofThick, he.z * 2.0f}, Color{208, 210, 214, 255},
                        0.50f, 0.14f, 0.0f, MatKind::Standard);

        if (!shadowPass) {
            drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight + roofThick * 0.52f, cc.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.03f, roofThick * 0.44f, he.z * 2.03f}, Color{196, 56, 36, 255},
                            0.32f, 0.12f, night ? 1.6f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
            drawModelStyled(cubeModel_, {cc.x, can.clearanceHeight - 0.02f, cc.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.65f, 0.05f, he.z * 1.55f}, Color{240, 244, 248, 255},
                            0.18f, 0.0f, night ? 1.9f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
            for (int sz = -1; sz <= 1; sz += 2) {
                drawModelStyled(cubeModel_, {cc.x, 0.12f, cc.z + static_cast<float>(sz) * 3.3f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {12.6f, 0.12f, 1.35f}, Color{156, 158, 162, 255},
                                0.78f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
            }
        }
    }

    // O(1) Spatial-Grid Accelerated Building Rendering across the 16x16 Metropolis!
    const float bldRadius = shadowPass ? 185.0f : 420.0f;
    const float bldCullSq = bldRadius * bldRadius;
    city.queryBuildingsInRadius(focus, bldRadius, visIdxBuf_);
    const auto& allBuildings = city.buildings();

    for (int idx : visIdxBuf_) {
        const auto& b = allBuildings[static_cast<std::size_t>(idx)];
        const Vec2 c = b.footprint.center();
        const float distSq = (c - focus).lengthSq();
        if (distSq > bldCullSq) continue;
        const Vec2 he = b.footprint.halfExtents();

        MatKind facadeKind = MatKind::MasonryFacade;
        float rough = 0.76f;
        float metal = 0.06f;
        if (b.style == BuildingStyle::GlassTower || b.style == BuildingStyle::DealershipBuilding) {
            facadeKind = MatKind::GlassSkyscraper;
            rough = 0.12f;
            metal = 0.24f;
        } else if (b.style == BuildingStyle::Warehouse || b.style == BuildingStyle::ShippingStack) {
            facadeKind = MatKind::IndustrialMetal;
            rough = 0.46f;
            metal = 0.48f;
        }

        // Main architectural volume
        drawModelStyled(cubeModel_, {c.x, b.height * 0.5f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {he.x * 2.0f, b.height, he.z * 2.0f}, toRayColor(b.primaryColor),
                        rough, metal, 0.0f, facadeKind);

        // Architectural ground granite podium base trim
        if (!shadowPass && b.height > 12.0f) {
            drawModelStyled(cubeModel_, {c.x, 0.55f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.03f, 0.95f, he.z * 2.03f}, Color{62, 64, 68, 255},
                            0.62f, 0.16f, 0.0f, MatKind::Standard);
        }

        // Building Style Specific Crown & Architectural Details
        if (b.style == BuildingStyle::VillaHouse) {
            // Coastal Suburban Villa: True 4-Sided Pitched Terracotta Clay Hip Roof, Eaves, Chimney & Timber Porch!
            const Color terracotta{152, 74, 50, 255};
            const Color ridgeTile{126, 58, 38, 255};
            const Color stuccoTrim{212, 206, 196, 255};
            // Overhanging white soffit cornice eaves
            drawModelStyled(cubeModel_, {c.x, b.height + 0.12f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.24f, 0.24f, he.z * 2.24f}, stuccoTrim, 0.78f, 0.0f, 0.0f, MatKind::Standard);
            // True sloped 4-sided hipped terracotta clay roof (rotated 90 deg when building is deeper along Z)
            const float roofH = std::clamp(std::min(he.x, he.z) * 0.48f, 2.2f, 4.2f);
            const bool rotateRoof = (he.z > he.x);
            const float roofSpanX = rotateRoof ? (he.z * 2.30f) : (he.x * 2.30f);
            const float roofSpanZ = rotateRoof ? (he.x * 2.30f) : (he.z * 2.30f);
            drawModelStyled(hipRoofModel_, {c.x, b.height + 0.22f, c.z}, {0.0f, 1.0f, 0.0f},
                            rotateRoof ? 90.0f : 0.0f,
                            {roofSpanX, roofH, roofSpanZ}, terracotta, 0.76f, 0.04f, 0.0f, MatKind::Standard);
            // Architectural ridge cap beam along top of pitched hip roof
            drawModelStyled(cubeModel_, {c.x, b.height + 0.24f + roofH, c.z}, {0.0f, 1.0f, 0.0f},
                            rotateRoof ? 90.0f : 0.0f,
                            {roofSpanX * 0.54f, 0.20f, 0.28f}, ridgeTile, 0.80f, 0.04f, 0.0f, MatKind::Standard);
            if (!shadowPass) {
                // Masonry fireplace chimney + shaded entry porch with terracotta roof & timber posts
                drawModelStyled(cubeModel_, {c.x + he.x * 0.36f, b.height + roofH * 0.78f + 0.65f, c.z - he.z * 0.22f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.86f, 1.75f, 0.86f}, stuccoTrim, 0.80f, 0.0f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x + he.x * 0.36f, b.height + roofH * 0.78f + 1.56f, c.z - he.z * 0.22f},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {1.02f, 0.14f, 1.02f}, ridgeTile, 0.76f, 0.04f, 0.0f, MatKind::Standard);
                const float porchZ = b.footprint.min.z - 0.88f;
                drawModelStyled(cubeModel_, {c.x, 3.05f, porchZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {std::min(he.x * 1.15f, 6.4f), 0.22f, 1.80f}, terracotta, 0.78f, 0.0f, 0.0f, MatKind::Standard);
                for (int s = -1; s <= 1; s += 2) {
                    drawModelStyled(cylModel_, {c.x + static_cast<float>(s) * std::min(he.x * 0.48f, 2.7f), 0.08f, porchZ - 0.64f},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.11f, 2.95f, 0.11f},
                                    Color{112, 82, 56, 255}, 0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                }
            }
        } else {
            // Neutral architectural cornice (soft LED crown accent ONLY at night on glass towers/nightclub)
            const bool crownGlow = (!shadowPass && night &&
                                    (b.style == BuildingStyle::GlassTower || b.style == BuildingStyle::NightclubBuilding));
            const Color corniceCol = crownGlow ? toRayColor(b.accentColor)
                                               : Color{
                                                     static_cast<unsigned char>(std::max(36, static_cast<int>(b.primaryColor.r) - 18)),
                                                     static_cast<unsigned char>(std::max(38, static_cast<int>(b.primaryColor.g) - 18)),
                                                     static_cast<unsigned char>(std::max(42, static_cast<int>(b.primaryColor.b) - 16)),
                                                     255};
            drawModelStyled(cubeModel_, {c.x, b.height + 0.30f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 2.05f, 0.60f, he.z * 2.05f}, corniceCol,
                            0.48f, (b.style == BuildingStyle::GlassTower) ? 0.50f : 0.15f,
                            crownGlow ? 1.15f : 0.0f,
                            crownGlow ? MatKind::EmissiveGlow : MatKind::Standard);
        }

        // Neoclassical Civic Landmarks (City Hall, SBPD HQ, Bay Medical Center, Courthouse)
        if (!shadowPass && b.style == BuildingStyle::CivicLandmark && distSq < 280.0f * 280.0f) {
            const Color limestone{194, 190, 180, 255};
            const float fz = b.footprint.min.z - 0.55f;
            const int cols = std::clamp(static_cast<int>(he.x / 2.2f), 4, 6);
            // Grand granite entry steps & fluted classical portico columns
            drawModelStyled(cubeModel_, {c.x, 0.34f, fz - 0.55f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.55f, 0.52f, 2.2f}, Color{154, 152, 146, 255}, 0.80f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
            for (int k = 0; k < cols; ++k) {
                const float frac = (cols > 1) ? (static_cast<float>(k) / static_cast<float>(cols - 1) - 0.5f) : 0.0f;
                const float colX = c.x + frac * (he.x * 1.40f);
                drawModelStyled(cylModel_, {colX, 0.55f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.34f, std::min(b.height - 1.2f, 11.5f), 0.34f},
                                limestone, 0.72f, 0.04f, 0.0f, MatKind::Standard);
            }
            // Classical entablature lintel above colonnade
            drawModelStyled(cubeModel_, {c.x, std::min(b.height - 0.6f, 12.2f), fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.56f, 1.15f, 1.35f}, limestone, 0.74f, 0.04f, 0.0f, MatKind::Standard);
            // Bay Medical Center Hospital Emblem (+)
            if (c.x > 240.0f && c.z < -150.0f) {
                const Color emsRed{192, 34, 38, 255};
                drawModelStyled(cubeModel_, {c.x, b.height - 2.4f, b.footprint.min.z - 0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.6f, 0.72f, 0.20f}, emsRed, 0.25f, 0.1f, night ? 1.5f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, b.height - 2.4f, b.footprint.min.z - 0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.72f, 2.6f, 0.20f}, emsRed, 0.25f, 0.1f, night ? 1.5f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
            }
        }

        // Rich Architectural Rooftop Equipment: Timber Water Towers, Spinning HVAC Banks, Satellite Dishes
        if (!shadowPass && distSq < 255.0f * 255.0f && b.upperTierHeight <= 0.5f && !b.hasHelipad &&
            b.style != BuildingStyle::VillaHouse && b.height > 10.5f && he.x > 5.2f) {
            const int roofHash = std::abs(static_cast<int>(c.x * 3.0f + c.z * 7.0f)) % 5;
            const Color steelDark{62, 66, 72, 255};
            const Color metalHvac{118, 122, 128, 255};

            const float hx = c.x + he.x * 0.30f;
            const float hz = c.z - he.z * 0.26f;
            drawModelStyled(cubeModel_, {hx, b.height + 1.05f, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.4f, 1.55f, 2.5f}, metalHvac, 0.62f, 0.42f, 0.0f, MatKind::IndustrialMetal);
            for (int f = -1; f <= 1; f += 2) {
                const float fx = hx + static_cast<float>(f) * 0.85f;
                drawModelStyled(cylModel_, {fx, b.height + 1.80f, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.68f, 0.22f, 0.68f}, steelDark, 0.48f, 0.65f, 0.0f, MatKind::Standard);
                rlPushMatrix();
                rlTranslatef(fx, b.height + 1.98f, hz);
                rlRotatef(animT * (210.0f + static_cast<float>(roofHash) * 28.0f), 0.0f, 1.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.12f, 0.04f, 0.14f}, Color{42, 46, 52, 255}, 0.4f, 0.6f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f,
                                {1.12f, 0.04f, 0.14f}, Color{42, 46, 52, 255}, 0.4f, 0.6f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }

            if (b.style != BuildingStyle::GlassTower && b.style != BuildingStyle::Warehouse &&
                b.style != BuildingStyle::ShippingStack && b.style != BuildingStyle::CivicLandmark &&
                (roofHash == 0 || roofHash == 2 || roofHash == 4) && he.z > 5.5f) {
                const float wx = c.x - he.x * 0.34f;
                const float wz = c.z + he.z * 0.28f;
                const float stiltH = 2.35f;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        drawModelStyled(cylModel_, {wx + sx * 0.92f, b.height + 0.30f, wz + sz * 0.92f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.10f, stiltH, 0.10f},
                                        steelDark, 0.48f, 0.72f, 0.0f, MatKind::Standard);
                    }
                }
                drawModelStyled(cubeModel_, {wx, b.height + 0.30f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.35f, 0.16f, 2.35f}, steelDark, 0.52f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {wx, b.height + 0.38f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.38f, 2.85f, 1.38f}, Color{124, 92, 62, 255}, 0.86f, 0.0f, 0.0f, MatKind::BarkWood);
                drawModelStyled(cylModel_, {wx, b.height + 1.15f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.41f, 0.10f, 1.41f}, Color{46, 50, 54, 255}, 0.48f, 0.75f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {wx, b.height + 2.35f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.41f, 0.10f, 1.41f}, Color{46, 50, 54, 255}, 0.48f, 0.75f, 0.0f, MatKind::Standard);
                drawModelStyled(coneModel_, {wx, b.height + 3.22f + stiltH, wz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.62f, 1.35f, 1.62f}, Color{80, 86, 92, 255}, 0.58f, 0.45f, 0.0f, MatKind::Standard);
            } else {
                const float dx = c.x - he.x * 0.32f;
                const float dz = c.z + he.z * 0.25f;
                drawModelStyled(cylModel_, {dx, b.height + 0.30f, dz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.11f, 1.45f, 0.11f}, steelDark, 0.44f, 0.75f, 0.0f, MatKind::Standard);
                rlPushMatrix();
                rlTranslatef(dx, b.height + 1.85f, dz);
                rlRotatef(static_cast<float>(roofHash) * 65.0f, 0.0f, 1.0f, 0.0f);
                rlRotatef(38.0f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.16f, 0.92f}, Color{198, 202, 208, 255}, 0.42f, 0.32f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.0f, 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.05f, 0.65f, 0.05f}, steelDark, 0.42f, 0.70f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }
        }

        float roofTopY = b.height;

        // Stepped upper skyscraper tier + spire antenna + blinking FAA aviation beacon
        if (b.upperTierHeight > 0.5f) {
            const float uy = b.height + b.upperTierHeight * 0.5f;
            drawModelStyled(cubeModel_, {c.x, uy, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.36f, b.upperTierHeight, he.z * 1.36f}, toRayColor(b.primaryColor),
                            rough, metal, 0.0f, facadeKind);

            roofTopY = b.height + b.upperTierHeight;
            const bool upperGlow = (!shadowPass && night && b.style == BuildingStyle::GlassTower);
            drawModelStyled(cubeModel_, {c.x, roofTopY + 0.22f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {he.x * 1.40f, 0.44f, he.z * 1.40f},
                            upperGlow ? toRayColor(b.accentColor) : Color{68, 72, 80, 255},
                            0.38f, 0.42f, upperGlow ? 1.05f : 0.0f,
                            upperGlow ? MatKind::EmissiveGlow : MatKind::Standard);

            if (!b.hasHelipad) {
                drawModelStyled(cylModel_, {c.x, roofTopY, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.22f, 7.6f, 0.22f}, Color{176, 182, 190, 255},
                                0.30f, 0.80f, 0.0f, MatKind::Standard);

                if (!shadowPass) {
                    const float blink = 0.5f + 0.5f * std::sin(animT * 3.4f + c.x * 0.13f);
                    if (blink > 0.35f) {
                        drawModelStyled(sphereModel_, {c.x, roofTopY + 7.75f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.32f, 0.32f, 0.32f}, Color{246, 44, 40, 255},
                                        0.1f, 0.0f, night ? 2.4f : 1.0f, MatKind::EmissiveGlow);
                    }
                }
            }
        }

        // Skyscraper & Hospital Rooftop Helipad
        if (!shadowPass && b.hasHelipad) {
            const float padR = std::min(he.x, he.z) * (b.upperTierHeight > 0.5f ? 0.56f : 0.68f);
            if (padR >= 3.2f) {
                const float py = roofTopY + 0.52f;
                drawModelStyled(cylModel_, {c.x, roofTopY + 0.25f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR, 0.32f, padR}, Color{62, 66, 72, 255}, 0.76f, 0.08f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {c.x, py, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR * 0.82f, 0.06f, padR * 0.82f}, Color{206, 168, 52, 255},
                                0.45f, 0.0f, night ? 0.65f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cylModel_, {c.x, py + 0.02f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {padR * 0.70f, 0.06f, padR * 0.70f}, Color{52, 56, 62, 255},
                                0.76f, 0.08f, 0.0f, MatKind::Standard);
                const float hSpan = padR * 0.36f;
                const MatKind hMat = night ? MatKind::EmissiveGlow : MatKind::Standard;
                const float hEm = night ? 0.7f : 0.0f;
                drawModelStyled(cubeModel_, {c.x - hSpan * 0.55f, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 0.28f, 0.04f, hSpan * 1.35f}, Color{226, 228, 232, 255}, 0.4f, 0.0f, hEm, hMat);
                drawModelStyled(cubeModel_, {c.x + hSpan * 0.55f, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 0.28f, 0.04f, hSpan * 1.35f}, Color{226, 228, 232, 255}, 0.4f, 0.0f, hEm, hMat);
                drawModelStyled(cubeModel_, {c.x, py + 0.07f, c.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {hSpan * 1.15f, 0.04f, hSpan * 0.26f}, Color{226, 228, 232, 255}, 0.4f, 0.0f, hEm, hMat);
                if (night) {
                    for (int k = 0; k < 8; ++k) {
                        const float a = (static_cast<float>(k) / 8.0f) * kTwoPi;
                        drawModelStyled(sphereModel_,
                                        {c.x + std::cos(a) * (padR * 0.92f), py + 0.10f, c.z + std::sin(a) * (padR * 0.92f)},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.14f, 0.14f, 0.14f},
                                        Color{58, 236, 112, 255}, 0.1f, 0.0f, 1.8f, MatKind::EmissiveGlow);
                    }
                }
            }
        }

        // Realistic Street-Level Detail (within 215m so close & mid streets look richly authentic!)
        if (!shadowPass && distSq < 215.0f * 215.0f) {
            // 1) Ground-Floor Retail Storefronts (Dark Bronze Frame, Multi-Pane Windows, Woven Fabric Awnings)
            if (b.hasStorefront && he.x >= 5.2f &&
                b.style != BuildingStyle::DealershipBuilding &&
                b.style != BuildingStyle::BoutiqueBuilding) {
                const Color awnCol = toRayColor(b.awningColor);
                const Color frameCol{30, 32, 38, 255};
                const float shopW = std::min(he.x * 1.62f, 13.5f);
                for (int side = -1; side <= 1; side += 2) {
                    const float fz = (side < 0) ? (b.footprint.min.z - 0.06f) : (b.footprint.max.z + 0.06f);
                    const float outZ = static_cast<float>(side);

                    drawModelStyled(cubeModel_, {c.x, 1.65f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.36f, 3.05f, 0.14f}, frameCol,
                                    0.44f, 0.50f, 0.0f, MatKind::Standard);
                    // Subtle warm interior display light at night (below bloom clip threshold!) + reflective dark glass by day
                    drawModelStyled(cubeModel_, {c.x, 1.62f, fz + outZ * 0.05f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW, 2.55f, 0.08f},
                                    night ? Color{174, 132, 82, 255} : Color{24, 34, 44, 255},
                                    0.08f, 0.18f, night ? 0.44f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::CarGlass);
                    // Central entry doorway + dark bronze architectural mullions & upper transom bar
                    drawModelStyled(cubeModel_, {c.x, 1.22f, fz + outZ * 0.09f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.85f, 2.25f, 0.10f}, Color{24, 26, 32, 255},
                                    0.38f, 0.60f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {c.x, 2.42f, fz + outZ * 0.10f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.04f, 0.10f, 0.08f}, frameCol,
                                    0.42f, 0.58f, 0.0f, MatKind::Standard);
                    for (int m = -1; m <= 1; m += 2) {
                        drawModelStyled(cubeModel_, {c.x + static_cast<float>(m) * (shopW * 0.28f), 1.62f, fz + outZ * 0.10f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.12f, 2.60f, 0.08f},
                                        frameCol, 0.42f, 0.58f, 0.0f, MatKind::Standard);
                    }
                    // Woven canvas shop awning (MatKind::FabricCloth)
                    rlPushMatrix();
                    rlTranslatef(c.x, 3.22f, fz + outZ * 0.62f);
                    rlRotatef(outZ * 18.0f, 1.0f, 0.0f, 0.0f);
                    drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.40f, 0.12f, 1.32f}, awnCol,
                                    0.82f, 0.0f, 0.0f, MatKind::FabricCloth);
                    rlPopMatrix();
                    drawModelStyled(cubeModel_, {c.x, 3.00f, fz + outZ * 1.24f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {shopW + 0.42f, 0.14f, 0.05f}, Color{218, 216, 210, 255},
                                    0.78f, 0.0f, 0.0f, MatKind::FabricCloth);
                }

                // Architectural Commercial Blade Sign (grounded painted metal in daylight, illuminated at night!)
                if (b.height >= 12.0f) {
                    const int signHash = std::abs(static_cast<int>(c.x * 5.0f + c.z * 11.0f)) % 4;
                    static constexpr Color kBladeDayCols[4] = {
                        {46, 86, 114, 255},
                        {134, 52, 56, 255},
                        {156, 122, 54, 255},
                        {48, 94, 76, 255}
                    };
                    static constexpr Color kBladeNightCols[4] = {
                        {92, 198, 242, 255},
                        {246, 118, 92, 255},
                        {248, 198, 92, 255},
                        {96, 224, 158, 255}
                    };
                    const Color bladeCol = night ? kBladeNightCols[signHash] : kBladeDayCols[signHash];
                    const float cornerX = b.footprint.max.x + 0.72f;
                    const float cornerZ = (signHash % 2 == 0) ? (b.footprint.min.z + 1.4f) : (b.footprint.max.z - 1.4f);
                    const float bladeH = std::min(4.8f, b.height * 0.32f);
                    const float bladeY = 4.6f + bladeH * 0.5f;
                    drawModelStyled(cubeModel_, {b.footprint.max.x + 0.32f, bladeY + bladeH * 0.34f, cornerZ},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.72f, 0.09f, 0.09f},
                                    Color{38, 42, 48, 255}, 0.42f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {b.footprint.max.x + 0.32f, bladeY - bladeH * 0.34f, cornerZ},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.72f, 0.09f, 0.09f},
                                    Color{38, 42, 48, 255}, 0.42f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {cornerX, bladeY, cornerZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.78f, bladeH, 0.20f}, Color{28, 30, 36, 255},
                                    0.40f, 0.55f, 0.0f, MatKind::Standard);
                    drawModelStyled(cubeModel_, {cornerX, bladeY, cornerZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.58f, bladeH * 0.86f, 0.24f}, bladeCol,
                                    night ? 0.14f : 0.48f, night ? 0.0f : 0.18f,
                                    night ? 1.85f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
            }

            // 2) 3D Residential Apartment Balconies (Weathered Concrete Slab + Smoked-Glass Railing)
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
                                            {3.4f, 0.16f, 1.15f}, Color{162, 160, 156, 255},
                                            0.76f, 0.04f, 0.0f, MatKind::Standard);
                            drawModelStyled(cubeModel_, {bx, fy + 0.52f, railZ}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                            {3.4f, 0.86f, 0.07f}, Color{34, 44, 54, 255},
                                            0.14f, 0.45f, 0.0f, MatKind::CarGlass);
                        }
                    }
                }
            }
        }

        // 3) Signature Commercial Landmarks (Architectural Daylight Signage -> Illuminated at Night!)
        if (!shadowPass) {
            if (b.style == BuildingStyle::ShopBuilding || b.style == BuildingStyle::GarageBuilding) {
                const Color signCol = (b.style == BuildingStyle::ShopBuilding)
                                          ? (night ? Color{238, 86, 54, 255} : Color{154, 52, 42, 255})
                                          : (night ? Color{68, 198, 242, 255} : Color{44, 102, 134, 255});
                drawModelStyled(cubeModel_, {c.x, 5.3f, b.footprint.min.z - 0.24f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.58f, 1.56f, 0.30f}, Color{28, 30, 36, 255},
                                0.42f, 0.55f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, 5.3f, b.footprint.min.z - 0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.48f, 1.24f, 0.16f}, signCol,
                                night ? 0.16f : 0.44f, night ? 0.0f : 0.22f,
                                night ? 1.85f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, 3.65f, b.footprint.min.z - 0.95f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.62f, 0.24f, 1.8f}, Color{38, 42, 48, 255},
                                0.46f, 0.42f, 0.0f, MatKind::Standard);
            } else if (b.style == BuildingStyle::DealershipBuilding) {
                // LUXURY AUTOS Panoramic Glass Showroom Atrium + Brushed Titanium Canopy + Brass Fascia
                drawModelStyled(cubeModel_, {c.x, 2.85f, b.footprint.min.z - 0.20f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.86f, 5.4f, 0.32f}, Color{20, 30, 42, 255},
                                0.06f, 0.24f, 0.0f, MatKind::CarGlass);
                drawModelStyled(cubeModel_, {c.x, 5.85f, b.footprint.min.z - 1.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.94f, 0.36f, 2.7f}, Color{186, 190, 196, 255},
                                0.28f, 0.78f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, 6.65f, b.footprint.min.z - 0.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.72f, 1.05f, 0.36f},
                                night ? Color{242, 194, 78, 255} : Color{176, 146, 82, 255},
                                0.22f, night ? 0.0f : 0.75f, night ? 1.85f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                // Brushed slate turntable pad on showroom forecourt (subtle accent ring)
                drawModelStyled(cylModel_, {c.x - 6.0f, 0.08f, b.footprint.min.z - 5.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.2f, 0.08f, 3.2f}, Color{44, 48, 54, 255}, 0.32f, 0.58f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {c.x - 6.0f, 0.06f, b.footprint.min.z - 5.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.36f, 0.05f, 3.36f},
                                night ? Color{246, 206, 98, 255} : Color{168, 152, 114, 255},
                                0.24f, night ? 0.0f : 0.72f, night ? 1.4f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
            } else if (b.style == BuildingStyle::BoutiqueBuilding) {
                // MODA BOUTIQUE Architectural Limestone & Champagne-Bronze Showcase
                drawModelStyled(cubeModel_, {c.x, 5.2f, b.footprint.max.z + 0.26f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.58f, 1.05f, 0.30f},
                                night ? Color{242, 192, 162, 255} : Color{138, 112, 92, 255},
                                0.26f, night ? 0.0f : 0.65f, night ? 1.65f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cubeModel_, {c.x, 2.1f, b.footprint.max.z + 0.14f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {he.x * 1.66f, 3.6f, 0.18f},
                                night ? Color{246, 224, 184, 255} : Color{28, 36, 46, 255},
                                0.08f, 0.18f, night ? 1.25f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::CarGlass);
            } else if (b.style == BuildingStyle::NightclubBuilding) {
                // PULSE NIGHTCLUB Brushed Charcoal Facade Louvers (subtle at day, animated LED equalizer at night!)
                for (int bar = -3; bar <= 3; ++bar) {
                    const float eqWave = night
                                             ? (0.45f + 0.55f * std::abs(std::sin(animT * 3.8f + static_cast<float>(bar) * 0.9f)))
                                             : 0.72f;
                    const float barH = (b.height - 3.5f) * eqWave;
                    const Color eqCol = night
                                            ? ((std::abs(bar) % 2 == 0) ? Color{176, 86, 232, 255} : Color{68, 198, 238, 255})
                                            : Color{42, 46, 54, 255};
                    drawModelStyled(cubeModel_,
                                    {c.x + static_cast<float>(bar) * 2.15f, 3.2f + barH * 0.5f, b.footprint.max.z + 0.22f},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.48f, barH, 0.22f},
                                    eqCol, night ? 0.12f : 0.36f, night ? 0.0f : 0.65f,
                                    night ? 2.1f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
                drawModelStyled(cubeModel_, {c.x, 3.15f, b.footprint.max.z + 1.35f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {6.8f, 0.28f, 2.6f}, Color{24, 26, 32, 255}, 0.36f, 0.6f, 0.0f, MatKind::Standard);
                if (night) {
                    const float sweep = std::sin(animT * 1.15f) * 16.0f;
                    rlPushMatrix();
                    rlTranslatef(c.x - 4.5f, b.height + 0.4f, c.z);
                    rlRotatef(sweep, 0.0f, 0.0f, 1.0f);
                    DrawCylinder({0.0f, 0.0f, 0.0f}, 3.6f, 0.35f, 34.0f, 12, Color{176, 98, 236, 28});
                    rlPopMatrix();
                    rlPushMatrix();
                    rlTranslatef(c.x + 4.5f, b.height + 0.4f, c.z);
                    rlRotatef(-sweep, 0.0f, 0.0f, 1.0f);
                    DrawCylinder({0.0f, 0.0f, 0.0f}, 3.6f, 0.35f, 34.0f, 12, Color{68, 206, 240, 28});
                    rlPopMatrix();
                }
            }

            // 3D Vertical Corner Pilasters (Neutral architectural stone/bronze derived from primaryColor — zero neon!)
            if (b.height > 12.0f && b.style != BuildingStyle::VillaHouse) {
                const bool isGlass = (b.style == BuildingStyle::GlassTower);
                const Color pilasterCol = isGlass
                                              ? Color{46, 52, 60, 255}
                                              : Color{
                                                    static_cast<unsigned char>(std::max(40, static_cast<int>(b.primaryColor.r) * 82 / 100)),
                                                    static_cast<unsigned char>(std::max(40, static_cast<int>(b.primaryColor.g) * 82 / 100)),
                                                    static_cast<unsigned char>(std::max(42, static_cast<int>(b.primaryColor.b) * 82 / 100)),
                                                    255};
                const float pilRough = isGlass ? 0.28f : 0.74f;
                const float pilMetal = isGlass ? 0.68f : 0.10f;
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
    const auto& city = world.city();
    const bool night = world.isNightTime();
    const float animT = static_cast<float>(GetTime());
    const float cullDistSq = shadowPass ? (210.0f * 210.0f) : (520.0f * 520.0f);
    const Color steelGray{66, 70, 76, 255};

    // 1. Drivable 134m Golden Bay Suspension Bridge (North Bay Strait: x = 0, z = 681..815m)
    const Vec2 bridgeCenter{0.0f, 748.0f};
    if ((bridgeCenter - focus).lengthSq() <= cullDistSq * 1.45f) {
        const Color bridgeOrange{174, 54, 40, 255};
        const Color concretePier{136, 134, 128, 255};
        const Color cableSteel{176, 182, 190, 255};
        const float towerZ[2] = {710.0f, 786.0f};
        const float towerHalfW = 11.4f;
        const float towerH = 54.0f;

        // Arched steel truss girder beneath the curved drivable roadway
        for (int seg = 0; seg < 8; ++seg) {
            const float z0 = CityMap::kBridgeStartZ + static_cast<float>(seg) * 16.75f;
            const float z1 = z0 + 16.75f;
            const float y0 = city.bridgeDeckElevationAt({0.0f, z0});
            const float y1 = city.bridgeDeckElevationAt({0.0f, z1});
            const float dz = z1 - z0;
            const float dy = y1 - y0;
            const float len = std::sqrt(dz * dz + dy * dy);
            const float pitch = -std::atan2(dy, dz) * kRadToDeg;
            rlPushMatrix();
            rlTranslatef(0.0f, 0.5f * (y0 + y1) - 0.68f, 0.5f * (z0 + z1));
            rlRotatef(pitch, 1.0f, 0.0f, 0.0f);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {23.2f, 1.05f, len + 0.14f}, steelGray, 0.54f, 0.62f, 0.0f, MatKind::IndustrialMetal);
            rlPopMatrix();
        }

        // Twin 54m Art-Deco H-Frame Suspension Towers + Massive Concrete Marine Caisson Piers!
        for (int tz = 0; tz < 2; ++tz) {
            const float z = towerZ[tz];
            // Concrete bay caisson pier base rising out of the water
            drawModelStyled(cubeModel_, {0.0f, -0.55f, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {28.5f, 3.2f, 6.2f}, concretePier, 0.84f, 0.04f, 0.0f, MatKind::SidewalkConcrete);
            for (int side = -1; side <= 1; side += 2) {
                const float x = static_cast<float>(side) * towerHalfW;
                // Stepped Art-Deco vertical steel tower leg
                drawModelStyled(cubeModel_, {x, towerH * 0.30f, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.10f, towerH * 0.60f, 2.35f}, bridgeOrange, 0.44f, 0.50f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {x, towerH * 0.78f, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.65f, towerH * 0.44f, 1.85f}, bridgeOrange, 0.44f, 0.50f, 0.0f, MatKind::Standard);
            }
            for (int strut = 0; strut < 4; ++strut) {
                const float sy = 15.0f + static_cast<float>(strut) * 11.8f;
                drawModelStyled(cubeModel_, {0.0f, sy, z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {towerHalfW * 2.0f + 1.6f, 1.45f, 1.55f}, bridgeOrange, 0.44f, 0.50f, 0.0f, MatKind::Standard);
            }
            if (!shadowPass) {
                for (int side = -1; side <= 1; side += 2) {
                    drawModelStyled(sphereModel_, {static_cast<float>(side) * towerHalfW, towerH + 0.65f, z},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.42f, 0.42f, 0.42f},
                                    Color{255, 42, 40, 255}, 0.1f, 0.0f, night ? 2.8f : 1.1f, MatKind::EmissiveGlow);
                }
            }
        }

        // Main Catenary Suspension Cables (between towers) + Side Approach Anchor Cables + Vertical Suspenders!
        const float spanMidZ = 0.5f * (towerZ[0] + towerZ[1]);
        const float spanHalfZ = 0.5f * (towerZ[1] - towerZ[0]);
        for (int side = -1; side <= 1; side += 2) {
            const float x = static_cast<float>(side) * towerHalfW;
            // Center span catenary from Python CAD lookup table
            for (std::size_t s = 0; s + 1 < generated::kSuspensionBridgeCatenary.size(); ++s) {
                const float z0 = spanMidZ + generated::kSuspensionBridgeCatenary[s].x * spanHalfZ;
                const float z1 = spanMidZ + generated::kSuspensionBridgeCatenary[s + 1].x * spanHalfZ;
                const float y0 = 14.5f + generated::kSuspensionBridgeCatenary[s].y * (towerH - 15.2f);
                const float y1 = 14.5f + generated::kSuspensionBridgeCatenary[s + 1].y * (towerH - 15.2f);
                const float dz = z1 - z0;
                const float dy = y1 - y0;
                const float segLen = std::sqrt(dz * dz + dy * dy);
                const float pitchDeg = -std::atan2(dy, dz) * kRadToDeg;

                rlPushMatrix();
                rlTranslatef(x, 0.5f * (y0 + y1), 0.5f * (z0 + z1));
                rlRotatef(pitchDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.36f, 0.36f, segLen + 0.10f}, bridgeOrange, 0.38f, 0.64f, 0.0f, MatKind::Standard);
                rlPopMatrix();

                if (!shadowPass && (s % 2 == 1)) {
                    const float deckY = city.bridgeDeckElevationAt({0.0f, z0});
                    drawModelStyled(cylModel_, {x, deckY + 0.4f, z0}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.07f, std::max(0.5f, y0 - deckY - 0.4f), 0.07f},
                                    cableSteel, 0.34f, 0.80f, 0.0f, MatKind::Standard);
                }
            }

            // South & North Approach Backstay Cables anchoring towers cleanly down to shorelines!
            const float anchorPairs[2][4] = {
                {CityMap::kBridgeStartZ, 2.2f, towerZ[0], towerH - 0.8f},
                {towerZ[1], towerH - 0.8f, CityMap::kBridgeEndZ, 2.2f}
            };
            for (int ap = 0; ap < 2; ++ap) {
                const float az0 = anchorPairs[ap][0];
                const float ay0 = anchorPairs[ap][1];
                const float az1 = anchorPairs[ap][2];
                const float ay1 = anchorPairs[ap][3];
                const float dz = az1 - az0;
                const float dy = ay1 - ay0;
                const float len = std::sqrt(dz * dz + dy * dy);
                const float pitch = -std::atan2(dy, dz) * kRadToDeg;
                rlPushMatrix();
                rlTranslatef(x, 0.5f * (ay0 + ay1), 0.5f * (az0 + az1));
                rlRotatef(pitch, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.34f, 0.34f, len + 0.10f}, bridgeOrange, 0.38f, 0.64f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }

            // Roadway Bridge Streetlamps along both pedestrian walkways
            if (!shadowPass) {
                for (int lp = 0; lp < 5; ++lp) {
                    const float lz = 696.0f + static_cast<float>(lp) * 26.0f;
                    const float dy = city.bridgeDeckElevationAt({0.0f, lz});
                    drawModelStyled(cylModel_, {static_cast<float>(side) * 10.6f, dy + 0.20f, lz},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.09f, 5.2f, 0.09f},
                                    steelGray, 0.40f, 0.72f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {static_cast<float>(side) * 9.8f, dy + 5.45f, lz},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.22f, 0.14f, 0.22f},
                                    night ? Color{255, 222, 152, 255} : Color{210, 214, 220, 255},
                                    0.18f, 0.0f, night ? 2.2f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
            }
        }
    }

    // 2. Sunset Bay Coastal Lighthouse & Keeper's Cottage (North Bay Peninsula: x = 0, z = 928m)
    const Vec2 lhPos{0.0f, 928.0f};
    if ((lhPos - focus).lengthSq() <= cullDistSq * 1.5f) {
        const Color lhWhite{216, 214, 208, 255};
        const Color lhCrimson{168, 46, 38, 255};
        const Color ironDark{36, 40, 46, 255};
        const Color patinatedCopper{68, 112, 96, 255};

        // Dressed stone octagonal base & 5-band tapered cylindrical lighthouse tower (28m tall)
        drawModelStyled(cylModel_, {lhPos.x, 0.10f, lhPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {4.2f, 2.2f, 4.2f}, Color{134, 132, 126, 255}, 0.82f, 0.04f, 0.0f, MatKind::Standard);
        const float bandH = 4.6f;
        for (int b = 0; b < 5; ++b) {
            const float r = 3.35f - static_cast<float>(b) * 0.26f;
            const Color bCol = (b % 2 == 0) ? lhWhite : lhCrimson;
            drawModelStyled(cylModel_, {lhPos.x, 2.2f + static_cast<float>(b) * bandH, lhPos.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f, {r, bandH + 0.08f, r},
                            bCol, 0.68f, 0.05f, 0.0f, MatKind::Standard);
        }
        // Iron observation gallery deck, lantern glass room & patinated copper cupola dome
        const float galY = 2.2f + 5.0f * bandH; // 25.2m
        drawModelStyled(cylModel_, {lhPos.x, galY, lhPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.75f, 0.38f, 2.75f}, ironDark, 0.45f, 0.72f, 0.0f, MatKind::Standard);
        drawModelStyled(cylModel_, {lhPos.x, galY + 0.36f, lhPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.85f, 2.45f, 1.85f},
                        night ? Color{255, 228, 152, 255} : Color{32, 48, 64, 255},
                        0.08f, 0.20f, night ? 2.6f : 0.0f,
                        night ? MatKind::EmissiveGlow : MatKind::CarGlass);
        drawModelStyled(coneModel_, {lhPos.x, galY + 2.80f, lhPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.25f, 2.15f, 2.25f}, patinatedCopper, 0.46f, 0.52f, 0.0f, MatKind::Standard);

        // Keeper's Stone Cottage beside the lighthouse with true sloped terracotta hip roof
        drawModelStyled(cubeModel_, {lhPos.x - 11.5f, 2.25f, lhPos.z - 4.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {8.6f, 4.3f, 6.4f}, lhWhite, 0.78f, 0.04f, 0.0f, MatKind::MasonryFacade);
        drawModelStyled(cubeModel_, {lhPos.x - 11.5f, 4.46f, lhPos.z - 4.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.2f, 0.18f, 7.0f}, lhWhite, 0.76f, 0.0f, 0.0f, MatKind::Standard);
        drawModelStyled(hipRoofModel_, {lhPos.x - 11.5f, 4.54f, lhPos.z - 4.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.5f, 2.2f, 7.3f}, Color{150, 72, 48, 255}, 0.76f, 0.04f, 0.0f, MatKind::Standard);

        // Slowly Rotating Fresnel Lighthouse Beam at Night!
        if (!shadowPass && night) {
            const float beamYaw = animT * 28.0f;
            rlPushMatrix();
            rlTranslatef(lhPos.x, galY + 1.55f, lhPos.z);
            rlRotatef(beamYaw, 0.0f, 1.0f, 0.0f);
            rlDrawRenderBatchActive();
            rlSetTexture(0);
            rlDisableDepthMask();
            BeginBlendMode(BLEND_ADDITIVE);
            for (int side = -1; side <= 1; side += 2) {
                rlPushMatrix();
                rlRotatef(static_cast<float>(side) * 90.0f, 0.0f, 0.0f, 1.0f);
                DrawCylinder({0.0f, 1.4f, 0.0f}, 0.55f, 6.8f, 58.0f, 14, Color{255, 232, 162, 34});
                rlPopMatrix();
            }
            rlDrawRenderBatchActive();
            EndBlendMode();
            rlEnableDepthMask();
            rlPopMatrix();
        }
    }

    // 3. Sunset Beach Coastal Ferris Wheel (x = -718.0f, z = -34.0f — classic architectural steel & warm night lamps)
    const Vec2 ferrisPos{-718.0f, -34.0f};
    if ((ferrisPos - focus).lengthSq() <= cullDistSq) {
        const float hubY = 14.8f;
        const float wheelR = 11.2f;
        const float wheelAngle = animT * 11.0f;

        // Architectural coastal concrete foundation plaza + weathered timber boardwalk deck cap
        drawModelStyled(cubeModel_, {ferrisPos.x, 0.52f, ferrisPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {10.4f, 1.04f, 15.2f}, Color{148, 146, 140, 255}, 0.82f, 0.04f, 0.0f, MatKind::SidewalkConcrete);
        drawModelStyled(cubeModel_, {ferrisPos.x, 1.12f, ferrisPos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {10.8f, 0.16f, 15.6f}, Color{128, 100, 72, 255}, 0.84f, 0.0f, 0.0f, MatKind::BarkWood);
        for (int side = -1; side <= 1; side += 2) {
            const float lx = ferrisPos.x + static_cast<float>(side) * 2.2f;
            drawModelStyled(cubeModel_, {lx, hubY * 0.52f, ferrisPos.z - 3.6f}, {1.0f, 0.0f, 0.0f}, -15.0f,
                            {0.42f, hubY * 1.04f, 0.42f}, Color{208, 210, 214, 255}, 0.40f, 0.65f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {lx, hubY * 0.52f, ferrisPos.z + 3.6f}, {1.0f, 0.0f, 0.0f},  15.0f,
                            {0.42f, hubY * 1.04f, 0.42f}, Color{208, 210, 214, 255}, 0.40f, 0.65f, 0.0f, MatKind::Standard);
        }

        drawModelStyled(cylModel_, {ferrisPos.x + 2.6f, hubY, ferrisPos.z}, {0.0f, 0.0f, 1.0f}, 90.0f,
                        {0.62f, 5.2f, 0.62f}, Color{198, 202, 208, 255}, 0.30f, 0.82f, 0.0f, MatKind::Standard);
        if (!shadowPass) {
            for (int side = -1; side <= 1; side += 2) {
                drawModelStyled(sphereModel_, {ferrisPos.x + static_cast<float>(side) * 2.62f, hubY, ferrisPos.z},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.22f, 0.72f, 0.72f},
                                night ? Color{255, 224, 152, 255} : Color{168, 48, 44, 255},
                                0.34f, night ? 0.0f : 0.35f, night ? 2.0f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
            }
        }

        for (int i = 0; i < 12; ++i) {
            const int next = (i + 1) % 12;
            const float rad = (static_cast<float>(i) * 30.0f + wheelAngle) * (3.14159265f / 180.0f);
            const float nextRad = (static_cast<float>(next) * 30.0f + wheelAngle) * (3.14159265f / 180.0f);
            const float cz = std::cos(rad) * wheelR;
            const float cy = std::sin(rad) * wheelR;
            const float nz = std::cos(nextRad) * wheelR;
            const float ny = std::sin(nextRad) * wheelR;

            const Color rimCol = night
                                     ? ((i % 2 == 0) ? Color{255, 224, 156, 255} : Color{242, 168, 92, 255})
                                     : Color{196, 200, 206, 255};
            for (int side = -1; side <= 1; side += 2) {
                const float rx = ferrisPos.x + static_cast<float>(side) * 1.65f;
                rlPushMatrix();
                rlTranslatef(rx, hubY + cy * 0.5f, ferrisPos.z + cz * 0.5f);
                rlRotatef(-(static_cast<float>(i) * 30.0f + wheelAngle), 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.13f, 0.13f, wheelR}, Color{204, 208, 214, 255}, 0.36f, 0.74f,
                                0.0f, MatKind::Standard);
                rlPopMatrix();

                const float dz = nz - cz;
                const float dy = ny - cy;
                const float chordLen = std::sqrt(dz * dz + dy * dy);
                const float chordDeg = -std::atan2(dy, dz) * kRadToDeg;
                rlPushMatrix();
                rlTranslatef(rx, hubY + 0.5f * (cy + ny), ferrisPos.z + 0.5f * (cz + nz));
                rlRotatef(chordDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.20f, 0.20f, chordLen + 0.1f}, rimCol,
                                night ? 0.16f : 0.38f, night ? 0.0f : 0.70f,
                                night ? 1.85f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                rlPopMatrix();
            }

            if (!shadowPass) {
                const Color cabCol = (i % 3 == 0) ? Color{154, 48, 46, 255}
                                   : ((i % 3 == 1) ? Color{44, 78, 114, 255} : Color{192, 168, 118, 255});
                const float gx = ferrisPos.x;
                const float gy = hubY + cy - 0.85f;
                const float gz = ferrisPos.z + cz;
                drawModelStyled(cubeModel_, {gx, hubY + cy, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.30f, 0.12f, 0.12f}, Color{196, 202, 210, 255}, 0.34f, 0.78f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {gx, gy - 0.30f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.55f, 0.72f, 1.44f}, cabCol, 0.28f, 0.32f, 0.0f, MatKind::CarClearcoat);
                drawModelStyled(cubeModel_, {gx, gy + 0.28f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.42f, 0.56f, 1.36f}, Color{18, 28, 40, 255}, 0.06f, 0.22f,
                                0.0f, MatKind::CarGlass);
                drawModelStyled(cubeModel_, {gx, gy + 0.62f, gz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {2.66f, 0.14f, 1.52f}, Color{212, 214, 218, 255}, 0.42f, 0.22f,
                                0.0f, MatKind::Standard);
            }
        }
    }

    // 4. East Harbor Concrete Container Quay & 4 Ship-to-Shore Gantry Cranes facing Twin Freighter Ships!
    const Vec2 quayCenter{704.0f, 570.0f};
    if ((quayCenter - focus).lengthSq() < cullDistSq * 1.45f) {
        // Concrete deep-water container wharf apron extending east from the ring road (x = 688..720, z = 472..668)
        drawModelStyled(cubeModel_, {704.0f, -0.34f, 570.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {32.0f, 0.84f, 196.0f}, Color{128, 130, 134, 255},
                        0.82f, 0.0f, 0.0f, MatKind::SidewalkConcrete);
        // Parallel steel gantry crane rails along the wharf deck
        if (!shadowPass) {
            for (float rx : {700.5f, 711.5f}) {
                drawModelStyled(cubeModel_, {rx, 0.10f, 570.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.08f, 188.0f}, Color{62, 66, 72, 255},
                                0.36f, 0.78f, 0.0f, MatKind::Standard);
            }
        }
    }

    static constexpr float kCraneZs[4] = {508.0f, 548.0f, 598.0f, 638.0f};
    const Color craneRed{168, 52, 42, 255};
    const Color craneSteel{182, 138, 48, 255};
    for (int cIdx = 0; cIdx < 4; ++cIdx) {
        const Vec2 cranePos{706.0f, kCraneZs[cIdx]};
        if ((cranePos - focus).lengthSq() > cullDistSq * 1.35f) continue;

        const float craneH = 22.0f;
        const Color mainCol = (cIdx % 2 == 0) ? craneSteel : craneRed;
        // 4 Portal legs resting directly on the twin wharf rails (x = 700.5 & 711.5)
        for (int lx = -1; lx <= 1; lx += 2) {
            for (int lz = -1; lz <= 1; lz += 2) {
                drawModelStyled(cubeModel_,
                                {cranePos.x + static_cast<float>(lx) * 5.5f,
                                 craneH * 0.5f,
                                 cranePos.z + static_cast<float>(lz) * 4.4f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.78f, craneH, 0.78f}, mainCol, 0.48f, 0.58f, 0.0f, MatKind::IndustrialMetal);
            }
            // Lower portal sill beam tying paired legs along Z
            drawModelStyled(cubeModel_,
                            {cranePos.x + static_cast<float>(lx) * 5.5f, 1.25f, cranePos.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.84f, 0.90f, 10.6f}, mainCol, 0.50f, 0.56f, 0.0f, MatKind::IndustrialMetal);
            // Mid portal horizontal tie beam
            drawModelStyled(cubeModel_,
                            {cranePos.x + static_cast<float>(lx) * 5.5f, craneH * 0.56f, cranePos.z},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.62f, 0.68f, 9.2f}, mainCol, 0.48f, 0.58f, 0.0f, MatKind::IndustrialMetal);
        }
        // Horizontal Ship-to-Shore Cantilever Boom extending EAST (+X) over the water toward the freighters!
        drawModelStyled(cubeModel_, {cranePos.x + 8.5f, craneH, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {36.0f, 1.35f, 9.6f}, mainCol, 0.46f, 0.62f, 0.0f, MatKind::IndustrialMetal);
        // Upper A-frame Apex Tower + Diagonal Forestay Tie Rods supporting cantilevered boom!
        drawModelStyled(cubeModel_, {cranePos.x, craneH + 5.2f, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.35f, 10.4f, 9.4f}, mainCol, 0.48f, 0.62f, 0.0f, MatKind::IndustrialMetal);
        if (!shadowPass) {
            for (int side = -1; side <= 1; side += 2) {
                const float sz = cranePos.z + static_cast<float>(side) * 4.3f;
                // Forward forestay to waterside boom tip
                rlPushMatrix();
                rlTranslatef(cranePos.x + 12.2f, craneH + 5.4f, sz);
                rlRotatef(-23.5f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {26.2f, 0.24f, 0.24f}, Color{54, 58, 66, 255}, 0.34f, 0.80f, 0.0f, MatKind::Standard);
                rlPopMatrix();
                // Rear backstay to landside boom tail
                rlPushMatrix();
                rlTranslatef(cranePos.x - 4.5f, craneH + 5.4f, sz);
                rlRotatef(48.0f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {13.6f, 0.24f, 0.24f}, Color{54, 58, 66, 255}, 0.34f, 0.80f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }
            // Overhead trolley cab + twin vertical hoist cables + suspended shipping container!
            const float trolleyX = cranePos.x + ((cIdx % 2 == 0) ? 14.5f : 9.5f);
            drawModelStyled(cubeModel_, {trolleyX, craneH - 1.55f, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.2f, 1.9f, 2.8f}, Color{214, 218, 222, 255}, 0.40f, 0.28f, 0.0f, MatKind::Standard);
            for (int cs = -1; cs <= 1; cs += 2) {
                drawModelStyled(cylModel_, {trolleyX, 11.2f, cranePos.z + static_cast<float>(cs) * 2.2f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.07f, 9.6f, 0.07f}, Color{46, 50, 56, 255}, 0.38f, 0.78f, 0.0f, MatKind::Standard);
            }
            const Color containerCol = (cIdx % 2 == 0) ? Color{148, 48, 42, 255} : Color{38, 86, 134, 255};
            drawModelStyled(cubeModel_, {trolleyX, 9.8f, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {2.6f, 2.65f, 6.8f}, containerCol, 0.56f, 0.48f, 0.0f, MatKind::IndustrialMetal);
            // Red aviation warning beacon atop each gantry crane at night
            if (night) {
                drawModelStyled(sphereModel_, {cranePos.x, craneH + 10.65f, cranePos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.26f, 0.26f}, Color{255, 48, 42, 255}, 0.10f, 0.0f, 2.2f, MatKind::EmissiveGlow);
            }
        }
    }

    // 5. Overhead Highway Directional Sign Gantries across Major Avenues & Bridge Approach
    struct GantryDef {
        float x;
        float z;
        bool alongX;
    };
    constexpr GantryDef kGantries[4] = {
        {  0.0f,  42.0f, true },
        { 84.0f,  42.0f, true },
        {-84.0f,  42.0f, true },
        {  0.0f, 630.0f, true }
    };
    const Color gantrySteel{134, 140, 148, 255};
    const Color highwayGreen{22, 86, 56, 255};
    for (const auto& g : kGantries) {
        const Vec2 gPos{g.x, g.z};
        if ((gPos - focus).lengthSq() > cullDistSq) continue;

        const float span = 18.8f;
        const float h = 6.8f;
        for (int side = -1; side <= 1; side += 2) {
            const float px = g.alongX ? (g.x + static_cast<float>(side) * (span * 0.5f)) : g.x;
            const float pz = g.alongX ? g.z : (g.z + static_cast<float>(side) * (span * 0.5f));
            drawModelStyled(cylModel_, {px, 0.05f, pz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.22f, h, 0.22f}, gantrySteel, 0.42f, 0.74f, 0.0f, MatKind::Standard);
        }
        drawModelStyled(cubeModel_, {g.x, h - 0.35f, g.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {span + 0.5f, 0.42f, 0.42f}, gantrySteel, 0.42f, 0.74f, 0.0f, MatKind::Standard);
        const MatKind signMat = night ? MatKind::EmissiveGlow : MatKind::Standard;
        const float signGlow = night ? 0.28f : 0.0f;
        for (int side = -1; side <= 1; side += 2) {
            const float sx = g.x + static_cast<float>(side) * 3.8f;
            drawModelStyled(cubeModel_, {sx, h - 0.35f, g.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {5.2f, 1.65f, 0.16f}, highwayGreen, 0.44f, 0.16f,
                            night ? 0.14f : 0.0f, signMat);
            if (!shadowPass) {
                const Color signWhite{222, 226, 224, 255};
                for (int face = -1; face <= 1; face += 2) {
                    const float fz = g.z + static_cast<float>(face) * 0.112f;
                    drawModelStyled(cubeModel_, {sx, h + 0.41f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {4.96f, 0.06f, 0.030f}, signWhite, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {sx, h - 1.11f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {4.96f, 0.06f, 0.030f}, signWhite, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {sx - 2.45f, h - 0.35f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 1.48f, 0.030f}, signWhite, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {sx + 2.45f, h - 0.35f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 1.48f, 0.030f}, signWhite, 0.35f, 0.0f, signGlow, signMat);
                    const float badgeX = sx - static_cast<float>(face) * 1.65f;
                    const float textX = sx + static_cast<float>(face) * 0.42f;
                    drawModelStyled(cubeModel_, {badgeX, h - 0.12f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.72f, 0.68f, 0.034f}, Color{34, 82, 158, 255}, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {badgeX, h + 0.16f, fz + static_cast<float>(face) * 0.012f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.70f, 0.16f, 0.028f}, Color{178, 44, 42, 255}, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {textX, h + 0.04f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.95f, 0.24f, 0.034f}, signWhite, 0.35f, 0.0f, signGlow, signMat);
                    drawModelStyled(cubeModel_, {textX - static_cast<float>(face) * 0.25f, h - 0.34f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.25f, 0.16f, 0.034f}, Color{208, 214, 210, 255}, 0.35f, 0.0f, signGlow, signMat);
                    for (int a = -1; a <= 1; a += 2) {
                        const float ax = sx + static_cast<float>(a) * 1.05f;
                        drawModelStyled(cubeModel_, {ax, h - 0.78f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {0.14f, 0.28f, 0.028f}, Color{216, 178, 56, 255}, 0.35f, 0.0f, signGlow, signMat);
                        drawModelStyled(coneModel_, {ax, h - 0.86f, fz}, {0.0f, 0.0f, 1.0f}, 180.0f,
                                        {0.18f, 0.22f, 0.03f}, Color{216, 178, 56, 255}, 0.35f, 0.0f, signGlow, signMat);
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

    // 1. Bobbing Coastal Sailboats & Motor Yachts off Western Sunset Beach & North Bay Strait!
    struct VesselDef {
        float x;
        float z;
        float yawDeg;
        float scale;
        bool sailboat;
        Color stripe;
    };
    static constexpr VesselDef kVessels[6] = {
        {-832.0f,  -48.0f,  18.0f, 1.05f, true,  {36, 86, 146, 255}},
        {-868.0f,   44.0f, -24.0f, 1.22f, false, {26, 44, 72, 255}},
        {-824.0f,  140.0f,  42.0f, 0.98f, true,  {158, 46, 42, 255}},
        {-118.0f,  756.0f,  84.0f, 1.28f, false, {32, 74, 108, 255}},
        { 112.0f,  764.0f, -72.0f, 1.02f, true,  {184, 142, 52, 255}},
        { -92.0f,  992.0f,  35.0f, 1.10f, true,  {38, 92, 86, 255}}
    };
    for (int i = 0; i < 6; ++i) {
        const auto& b = kVessels[i];
        const Vec2 bPos{b.x, b.z};
        if ((bPos - focus).lengthSq() > 580.0f * 580.0f) continue;

        const float phase = static_cast<float>(i) * 1.37f;
        const float heave = 0.14f * std::sin(animT * 1.45f + phase);
        const float rollDeg = 2.6f * std::sin(animT * 1.15f + phase * 1.4f);
        const float pitchDeg = 1.5f * std::cos(animT * 0.95f + phase);

        rlPushMatrix();
        rlTranslatef(b.x, kWaterY + heave, b.z);
        rlRotatef(b.yawDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(pitchDeg, 0.0f, 0.0f, 1.0f);
        rlRotatef(rollDeg, 1.0f, 0.0f, 0.0f);
        rlScalef(b.scale, b.scale, b.scale);

        drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.2f, 1.25f, 3.2f}, Color{226, 228, 232, 255}, 0.25f, 0.14f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(coneModel_, {4.6f, 0.45f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {1.42f, 3.2f, 1.60f}, Color{226, 228, 232, 255}, 0.25f, 0.14f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(cubeModel_, {0.2f, 0.26f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {9.6f, 0.24f, 3.28f}, b.stripe, 0.24f, 0.22f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(cubeModel_, {-0.2f, 1.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {8.4f, 0.10f, 2.85f}, Color{144, 108, 72, 255}, 0.80f, 0.0f, 0.0f, MatKind::BarkWood);

        if (b.sailboat) {
            drawModelStyled(cubeModel_, {-0.6f, 1.55f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.6f, 0.85f, 2.05f}, Color{230, 232, 236, 255}, 0.30f, 0.14f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.6f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 11.4f, 0.10f}, Color{196, 202, 210, 255}, 0.28f, 0.84f, 0.0f, MatKind::Standard);
            const float sailSway = 4.5f * std::sin(animT * 0.9f + phase);
            rlPushMatrix();
            rlTranslatef(0.6f, 2.6f, 0.0f);
            rlRotatef(sailSway, 0.0f, 1.0f, 0.0f);
            drawModelStyled(cubeModel_, {-2.1f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {4.2f, 0.12f, 0.12f}, Color{186, 192, 202, 255}, 0.32f, 0.78f, 0.0f, MatKind::Standard);
            drawModelStyled(coneModel_, {-1.45f, 0.15f, 0.0f}, {0.0f, 0.0f, 1.0f}, -10.0f,
                            {2.05f, 9.2f, 0.16f}, Color{230, 226, 216, 255}, 0.76f, 0.0f, 0.0f, MatKind::FabricCloth);
            rlPopMatrix();
            drawModelStyled(coneModel_, {2.45f, 2.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, 14.0f,
                            {1.45f, 7.6f, 0.14f}, Color{226, 222, 212, 255}, 0.76f, 0.0f, 0.0f, MatKind::FabricCloth);
            if (night) {
                drawModelStyled(sphereModel_, {0.6f, 12.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 0.18f, 0.18f}, Color{255, 238, 188, 255}, 0.1f, 0.0f, 2.5f, MatKind::EmissiveGlow);
            }
        } else {
            drawModelStyled(cubeModel_, {-0.5f, 1.85f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {5.8f, 1.45f, 2.55f}, Color{230, 232, 236, 255}, 0.24f, 0.16f, 0.0f, MatKind::CarClearcoat);
            drawModelStyled(cubeModel_, {0.2f, 1.98f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {4.6f, 0.64f, 2.62f}, night ? Color{255, 220, 152, 255} : Color{18, 28, 42, 255},
                            0.08f, 0.20f, night ? 1.35f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::CarGlass);
            drawModelStyled(cubeModel_, {-1.1f, 2.95f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {3.4f, 0.78f, 2.15f}, Color{232, 234, 238, 255}, 0.28f, 0.14f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-1.4f, 3.72f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                            {0.28f, 1.10f, 2.05f}, Color{220, 224, 230, 255}, 0.30f, 0.35f, 0.0f, MatKind::Standard);
        }

        if (night) {
            drawModelStyled(sphereModel_, {2.2f, 1.38f, -1.52f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.14f, 0.14f, 0.14f}, Color{255, 48, 48, 255}, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
            drawModelStyled(sphereModel_, {2.2f, 1.38f,  1.52f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.14f, 0.14f, 0.14f}, Color{52, 240, 108, 255}, 0.1f, 0.0f, 2.2f, MatKind::EmissiveGlow);
        }
        rlPopMatrix();
    }

    // 2. Twin Anchored Southeast Harbor Container Freighter Ships (x = 766, z = 528 & 618)
    static constexpr float kShipZs[2] = {528.0f, 618.0f};
    for (int sIdx = 0; sIdx < 2; ++sIdx) {
        const Vec2 shipPos{766.0f, kShipZs[sIdx]};
        if ((shipPos - focus).lengthSq() >= 620.0f * 620.0f) continue;
        const float shipRoll = 0.7f * std::sin(animT * 0.65f + static_cast<float>(sIdx) * 1.9f);
        rlPushMatrix();
        rlTranslatef(shipPos.x, kWaterY, shipPos.z);
        rlRotatef(shipRoll, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cubeModel_, {0.0f, 1.15f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {14.0f, 2.8f, 54.0f}, Color{134, 42, 36, 255}, 0.70f, 0.24f, 0.0f, MatKind::IndustrialMetal);
        drawModelStyled(cubeModel_, {0.0f, 3.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {14.4f, 2.4f, 54.4f}, Color{30, 40, 52, 255}, 0.60f, 0.34f, 0.0f, MatKind::IndustrialMetal);
        static constexpr Color kCargoCols[4] = {
            {156, 48, 40, 255}, {38, 86, 136, 255}, {172, 126, 44, 255}, {44, 102, 72, 255}
        };
        for (int row = 0; row < 4; ++row) {
            const float cz = -17.0f + static_cast<float>(row) * 8.2f;
            for (int col = -1; col <= 1; ++col) {
                const float cx = static_cast<float>(col) * 3.6f;
                const int layers = (row == 1 || row == 2) ? 2 : 1;
                for (int l = 0; l < layers; ++l) {
                    const Color cc = kCargoCols[(row + col + l + sIdx + 4) % 4];
                    drawModelStyled(cubeModel_, {cx, 6.1f + static_cast<float>(l) * 2.7f, cz},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {3.1f, 2.55f, 7.2f},
                                    cc, 0.64f, 0.36f, 0.0f, MatKind::IndustrialMetal);
                }
            }
        }
        drawModelStyled(cubeModel_, {0.0f, 9.8f, 18.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {12.2f, 10.2f, 7.8f}, Color{216, 218, 222, 255}, 0.50f, 0.14f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.0f, 13.4f, 14.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {10.4f, 1.35f, 0.35f}, night ? Color{255, 224, 148, 255} : Color{22, 32, 44, 255},
                        0.10f, 0.25f, night ? 1.6f : 0.0f, night ? MatKind::EmissiveGlow : MatKind::CarGlass);
        drawModelStyled(cubeModel_, {0.0f, 16.4f, 20.5f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.6f, 4.2f, 3.2f}, Color{188, 76, 38, 255}, 0.52f, 0.30f, 0.0f, MatKind::Standard);
        rlPopMatrix();
    }

    // 3. Soaring Flapping Coastal Seagulls gliding over Sunset Beach, Central Park Lake & Lighthouse!
    struct FlockCenter { float x; float z; float y; float r; };
    static constexpr FlockCenter kFlocks[3] = {
        {-756.0f, -12.0f, 18.5f, 26.0f},
        {-108.0f, -42.0f, 22.0f, 26.0f},
        {   0.0f, 922.0f, 24.0f, 28.0f}
    };
    for (int f = 0; f < 3; ++f) {
        const Vec2 fc{kFlocks[f].x, kFlocks[f].z};
        if ((fc - focus).lengthSq() > 290.0f * 290.0f) continue;
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
                            {0.32f, 0.10f, 0.12f}, Color{236, 238, 242, 255}, 0.58f, 0.0f, 0.0f, MatKind::Standard);
            for (int side = -1; side <= 1; side += 2) {
                rlPushMatrix();
                rlRotatef(static_cast<float>(side) * flapDeg, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cubeModel_, {-0.02f, 0.0f, static_cast<float>(side) * 0.34f},
                                {0.0f, 1.0f, 0.0f}, static_cast<float>(-side) * 14.0f,
                                {0.16f, 0.03f, 0.68f}, Color{226, 230, 236, 255}, 0.58f, 0.0f, 0.0f, MatKind::Standard);
                rlPopMatrix();
            }
            rlPopMatrix();
        }
    }

    // 4. High-Altitude Commercial City Airliner with Blinking Nav Strobes & Dual Jet Contrails!
    {
        const float cycle = std::fmod(animT * 22.0f, 2200.0f) - 1100.0f;
        const float px = cycle;
        const float py = 165.0f;
        const float pz = -180.0f + cycle * 0.22f;
        const bool strobeOn = (std::fmod(animT * 1.4f, 1.0f) < 0.20f);

        rlPushMatrix();
        rlTranslatef(px, py, pz);
        rlRotatef(-12.4f, 0.0f, 1.0f, 0.0f);
        drawModelStyled(cylModel_, {-7.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                        {1.15f, 14.0f, 1.15f}, Color{234, 238, 244, 255}, 0.26f, 0.45f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-0.5f, -0.1f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.4f, 0.26f, 16.2f}, Color{216, 222, 230, 255}, 0.28f, 0.55f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {-6.4f, 1.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, 18.0f,
                        {2.6f, 3.6f, 0.22f}, Color{36, 92, 162, 255}, 0.32f, 0.35f, 0.0f, MatKind::Standard);
        if (strobeOn || night) {
            drawModelStyled(sphereModel_, {-0.8f, 0.0f, -8.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.42f, 0.42f, 0.42f}, Color{255, 42, 42, 255}, 0.1f, 0.0f, 3.0f, MatKind::EmissiveGlow);
            drawModelStyled(sphereModel_, {-0.8f, 0.0f,  8.2f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.42f, 0.42f, 0.42f}, Color{48, 245, 96, 255}, 0.1f, 0.0f, 3.0f, MatKind::EmissiveGlow);
        }
        for (int s = -1; s <= 1; s += 2) {
            DrawLine3D({-2.5f, -0.4f, static_cast<float>(s) * 2.8f},
                       {-95.0f, -0.4f, static_cast<float>(s) * 3.8f},
                       Color{230, 236, 244, static_cast<unsigned char>(night ? 38 : 88)});
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
        const float tanYawRad = orbitAng + kHalfPi;
        const float tanYawDeg = -tanYawRad * kRadToDeg;
        const bool redFlash = (std::fmod(animT * 3.8f, 1.0f) < 0.5f);

        rlPushMatrix();
        rlTranslatef(heliX, heliY, heliZ);
        rlRotatef(tanYawDeg, 0.0f, 1.0f, 0.0f);
        rlRotatef(-11.0f, 0.0f, 0.0f, 1.0f);
        rlRotatef( 20.0f, 1.0f, 0.0f, 0.0f);

        const Color heliNavy{22, 30, 44, 255};
        const Color heliWhite{236, 240, 246, 255};
        const Color heliSteel{160, 170, 182, 255};

        drawModelStyled(sphereModel_, {0.35f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {2.45f, 1.05f, 1.10f}, heliNavy, 0.28f, 0.45f, 0.0f, MatKind::CarClearcoat);
        drawModelStyled(sphereModel_, {1.45f, 0.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {1.35f, 0.86f, 0.96f}, Color{18, 34, 52, 255}, 0.06f, 0.35f, 0.0f, MatKind::CarGlass);
        drawModelStyled(cubeModel_, {0.10f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {3.2f, 0.24f, 1.98f}, heliWhite, 0.24f, 0.25f, 0.0f, MatKind::CarClearcoat);

        for (int s = -1; s <= 1; s += 2) {
            const float sz = static_cast<float>(s) * 1.15f;
            drawModelStyled(cylModel_, {-1.4f, -1.32f, sz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.075f, 3.6f, 0.075f}, heliSteel, 0.32f, 0.78f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.95f, -0.92f, sz * 0.78f}, {1.0f, 0.0f, 0.0f}, static_cast<float>(-s) * 24.0f,
                            {0.09f, 0.92f, 0.09f}, heliSteel, 0.35f, 0.75f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-0.65f, -0.92f, sz * 0.78f}, {1.0f, 0.0f, 0.0f}, static_cast<float>(-s) * 24.0f,
                            {0.09f, 0.92f, 0.09f}, heliSteel, 0.35f, 0.75f, 0.0f, MatKind::Standard);
        }

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

        drawModelStyled(sphereModel_, {0.15f, -0.98f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.26f, 0.26f, 0.26f},
                        redFlash ? Color{255, 38, 48, 255} : Color{42, 138, 255, 255},
                        0.1f, 0.0f, 3.8f, MatKind::EmissiveGlow);
        drawModelStyled(sphereModel_, {2.15f, -0.58f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.34f, 0.34f, 0.34f}, Color{245, 250, 255, 255},
                        0.08f, 0.0f, 4.0f, MatKind::EmissiveGlow);
        rlPopMatrix();

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
    const auto& city = world.city();
    const bool night = world.isNightTime();
    const float animT = static_cast<float>(GetTime());
    const float treeRadius = shadowPass ? 135.0f : 245.0f;
    const float treeCullSq = treeRadius * treeRadius;

    city.queryTreesInRadius(focus, treeRadius, visIdxBuf_);
    const auto& allTrees = city.trees();

    for (int idx : visIdxBuf_) {
        const auto& t = allTrees[static_cast<std::size_t>(idx)];
        if ((t.pos - focus).lengthSq() > treeCullSq) continue;
        const int treeHash = std::abs(static_cast<int>(t.pos.x * 7.0f + t.pos.z * 13.0f));
        const float windSway = std::sin(animT * 1.45f + t.pos.x * 0.11f + t.pos.z * 0.09f);

        // Architectural urban sidewalk tree grate & granite planter border (soil mulch + subtle night uplight)
        if (!shadowPass && t.inPlanter) {
            drawModelStyled(cubeModel_, {t.pos.x, 0.14f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.58f, 0.16f, 1.58f}, Color{118, 120, 124, 255},
                            0.78f, 0.04f, 0.0f, MatKind::SidewalkConcrete);
            drawModelStyled(cubeModel_, {t.pos.x, 0.22f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.34f, 0.05f, 1.34f}, Color{44, 46, 50, 255},
                            0.52f, 0.65f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {t.pos.x, 0.21f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {1.12f, 0.07f, 1.12f}, Color{56, 42, 30, 255},
                            0.92f, 0.0f, 0.0f, MatKind::Standard);
            if (night) {
                drawModelStyled(sphereModel_, {t.pos.x + 0.48f, 0.28f, t.pos.z + 0.48f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.07f, 0.045f, 0.07f}, Color{255, 218, 146, 255},
                                0.1f, 0.0f, 1.8f, MatKind::EmissiveGlow);
            }
        }

        if (t.palmStyle || t.species == TreeSpecies::PalmCoastal) {
            // Coastal California Palm Tree: Curved Fissured Bark Trunk + Coconuts + 8 Drooping Botanical Fronds!
            const float leanYaw = static_cast<float>(treeHash % 360);
            const float segH = t.trunkHeight * 0.28f;
            const Color barkLight{118, 92, 66, 255};
            const Color barkDark{92, 70, 50, 255};

            rlPushMatrix();
            rlTranslatef(t.pos.x, 0.06f, t.pos.z);
            rlRotatef(leanYaw, 0.0f, 1.0f, 0.0f);
            drawModelStyled(coneModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.34f, 0.55f, 0.34f}, barkDark, 0.90f, 0.0f, 0.0f, MatKind::BarkWood);
            for (int seg = 0; seg < 4; ++seg) {
                const float r = 0.25f - static_cast<float>(seg) * 0.025f;
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {r, segH * 1.05f, r}, (seg % 2 == 0) ? barkLight : barkDark,
                                0.88f, 0.0f, 0.0f, MatKind::BarkWood);
                if (!shadowPass && seg > 0) {
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {r * 1.15f, 0.09f, r * 1.15f}, barkDark, 0.92f, 0.0f, 0.0f, MatKind::BarkWood);
                }
                rlTranslatef(0.0f, segH, 0.0f);
                rlRotatef(-4.2f - static_cast<float>(seg) * 0.85f + windSway * 0.45f, 0.0f, 0.0f, 1.0f);
            }

            if (!shadowPass) {
                drawModelStyled(sphereModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 0.22f, 0.24f}, Color{68, 96, 48, 255}, 0.82f, 0.0f, 0.0f, MatKind::FoliageGrass);
                for (int c = 0; c < 3; ++c) {
                    const float ca = static_cast<float>(c) * (kTwoPi / 3.0f);
                    drawModelStyled(sphereModel_, {std::cos(ca) * 0.20f, -0.14f, std::sin(ca) * 0.20f},
                                    {0.0f, 1.0f, 0.0f}, 0.0f, {0.12f, 0.14f, 0.12f},
                                    Color{86, 58, 34, 255}, 0.80f, 0.0f, 0.0f, MatKind::BarkWood);
                }
            }

            const int frondCount = shadowPass ? 6 : 8;
            for (int f = 0; f < frondCount; ++f) {
                const bool upperTier = (f % 2 == 0);
                const float fAng = static_cast<float>(f) * (360.0f / static_cast<float>(frondCount)) + static_cast<float>(treeHash % 45);
                const float liftDeg = (upperTier ? 34.0f : 16.0f) + windSway * 2.4f;
                const Color frondCol = upperTier ? Color{62, 108, 48, 255} : Color{44, 86, 38, 255};
                const float innerLen = t.crownRadius * (upperTier ? 0.68f : 0.74f);
                const float outerLen = t.crownRadius * (upperTier ? 0.76f : 0.82f);

                rlPushMatrix();
                rlRotatef(fAng, 0.0f, 1.0f, 0.0f);
                rlRotatef(liftDeg, 0.0f, 0.0f, 1.0f);
                drawModelStyled(cubeModel_, {innerLen * 0.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {innerLen, 0.075f, 0.44f}, frondCol, 0.78f, 0.0f, 0.0f, MatKind::FoliageGrass);
                rlTranslatef(innerLen * 0.94f, 0.0f, 0.0f);
                rlRotatef(-48.0f + windSway * 3.0f, 0.0f, 0.0f, 1.0f);
                drawModelStyled(coneModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.10f, outerLen, 0.34f}, Color{52, 96, 44, 255},
                                0.78f, 0.0f, 0.0f, MatKind::FoliageGrass);
                rlPopMatrix();
            }
            rlPopMatrix();
        } else if (t.species == TreeSpecies::CypressConifer) {
            // Tall Slender Mediterranean Cypress Evergreen (sculpted organic tapered spire — zero stacked blobs!)
            const Color barkCol{84, 62, 44, 255};
            drawModelStyled(cylModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.18f, t.trunkHeight * 0.55f, 0.18f}, barkCol, 0.90f, 0.0f, 0.0f, MatKind::BarkWood);
            const float spireR = t.crownRadius * 0.62f;
            const float spireH = t.trunkHeight * 2.25f;
            const Color cyCol = (treeHash % 2 == 0) ? Color{36, 66, 38, 255} : Color{44, 74, 42, 255};
            rlPushMatrix();
            rlTranslatef(t.pos.x, t.trunkHeight * 0.34f, t.pos.z);
            rlRotatef(windSway * 1.1f, 0.0f, 0.0f, 1.0f);
            drawModelStyled(cypressModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                            static_cast<float>(treeHash % 360),
                            {spireR, spireH, spireR}, cyCol, 0.84f, 0.0f, 0.0f, MatKind::FoliageGrass);
            rlPopMatrix();
        } else {
            // Realistic Deciduous Park & Boulevard Oak/Olive/Sycamore Tree (7-Tuft Organic Botanical Canopy + BarkWood!)
            const Color barkCol{88, 66, 46, 255};
            const Color barkDark{70, 52, 36, 255};
            drawModelStyled(coneModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.44f, 0.68f, 0.44f}, barkDark, 0.90f, 0.0f, 0.0f, MatKind::BarkWood);
            drawModelStyled(cylModel_, {t.pos.x, 0.06f, t.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.27f, t.trunkHeight * 1.04f, 0.27f}, barkCol,
                            0.88f, 0.0f, 0.0f, MatKind::BarkWood);

            if (!shadowPass) {
                for (int br = 0; br < 3; ++br) {
                    const float bYaw = static_cast<float>(br) * 120.0f + static_cast<float>(treeHash % 90);
                    rlPushMatrix();
                    rlTranslatef(t.pos.x, t.trunkHeight * 0.68f, t.pos.z);
                    rlRotatef(bYaw, 0.0f, 1.0f, 0.0f);
                    rlRotatef(42.0f, 0.0f, 0.0f, 1.0f);
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.12f, t.crownRadius * 0.88f, 0.12f}, barkDark,
                                    0.88f, 0.0f, 0.0f, MatKind::BarkWood);
                    rlPopMatrix();
                }
            }

            // 100% Authentic Botanical Palettes (Live Oak, Olive Sage, Sycamore, subtle Autumn Oak — ZERO neon/purple!)
            const int palRoll = treeHash % 9;
            Color cDeep{34, 68, 36, 255};
            Color cMain{48, 86, 44, 255};
            Color cSun{66, 106, 54, 255};
            if (palRoll == 0 || palRoll == 1) {
                // Coastal Olive / Eucalyptus Sage
                cDeep = {50, 74, 48, 255};
                cMain = {68, 94, 62, 255};
                cSun  = {86, 114, 76, 255};
            } else if (palRoll == 2 || palRoll == 3) {
                // Sunlit Urban Sycamore
                cDeep = {42, 78, 38, 255};
                cMain = {58, 98, 48, 255};
                cSun  = {78, 118, 58, 255};
            } else if (palRoll == 4) {
                // Warm Autumn Golden Oak (1 in 9)
                cDeep = {112, 76, 34, 255};
                cMain = {144, 98, 42, 255};
                cSun  = {170, 122, 52, 255};
            }

            const float cy = t.trunkHeight + t.crownRadius * 0.56f;
            const float r = t.crownRadius;
            const float swayOff = windSway * 0.05f;
            const float baseRotDeg = static_cast<float>(treeHash % 360);
            // Deep shaded interior core
            drawModelStyled(foliageClusterModel_, {t.pos.x + swayOff, cy, t.pos.z}, {0.0f, 1.0f, 0.0f}, baseRotDeg,
                            {r * 0.80f, r * 0.68f, r * 0.80f}, cDeep, 0.82f, 0.0f, 0.0f, MatKind::FoliageGrass);
            if (!shadowPass) {
                const float rotBase = baseRotDeg * (kPi / 180.0f);
                // 5 asymmetrical perimeter leafy tufts + 2 sun-dappled upper crown tufts!
                for (int l = 0; l < 5; ++l) {
                    const float la = rotBase + static_cast<float>(l) * (kTwoPi / 5.0f) + 0.35f;
                    const float dist = r * (0.48f + 0.11f * static_cast<float>(l % 2));
                    const float ly = cy + ((l % 2 == 0) ? (-r * 0.14f) : (r * 0.14f));
                    const float ls = r * ((l % 2 == 0) ? 0.56f : 0.48f);
                    drawModelStyled(foliageClusterModel_,
                                    {t.pos.x + std::cos(la) * dist + swayOff, ly, t.pos.z + std::sin(la) * dist},
                                    {0.0f, 1.0f, 0.0f}, baseRotDeg + static_cast<float>(l) * 71.0f,
                                    {ls * 1.08f, ls * 0.78f, ls},
                                    (l % 2 == 0) ? cMain : cDeep, 0.80f, 0.0f, 0.0f, MatKind::FoliageGrass);
                }
                drawModelStyled(foliageClusterModel_, {t.pos.x + r * 0.20f + swayOff * 1.3f, cy + r * 0.40f, t.pos.z - r * 0.14f},
                                {0.0f, 1.0f, 0.0f}, baseRotDeg + 145.0f, {r * 0.54f, r * 0.44f, r * 0.50f},
                                cSun, 0.78f, 0.0f, 0.0f, MatKind::FoliageGrass);
                drawModelStyled(foliageClusterModel_, {t.pos.x - r * 0.24f + swayOff * 1.3f, cy + r * 0.34f, t.pos.z + r * 0.18f},
                                {0.0f, 1.0f, 0.0f}, baseRotDeg + 230.0f, {r * 0.46f, r * 0.38f, r * 0.48f},
                                cMain, 0.79f, 0.0f, 0.0f, MatKind::FoliageGrass);
            }
        }
    }

    const float propRadius = shadowPass ? 105.0f : 205.0f;
    const float propCullSq = propRadius * propRadius;
    city.queryPropsInRadius(focus, propRadius, visIdxBuf_);
    const auto& allProps = city.streetProps();

    for (int idx : visIdxBuf_) {
        const auto& prop = allProps[static_cast<std::size_t>(idx)];
        if ((prop.pos - focus).lengthSq() > propCullSq) continue;
        if (shadowPass &&
            prop.kind != StreetPropKind::StreetLight &&
            prop.kind != StreetPropKind::BusShelter &&
            prop.kind != StreetPropKind::Billboard &&
            prop.kind != StreetPropKind::BeachUmbrella) {
            continue;
        }

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
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.23f, 0.62f, 0.23f}, Color{54, 58, 66, 255},
                                    0.42f, 0.76f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.18f, 0.24f, 0.18f}, Color{62, 68, 76, 255},
                                    0.40f, 0.74f, 0.0f, MatKind::Standard);
                }
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.125f, 6.25f, 0.125f}, Color{72, 78, 86, 255},
                                0.40f, 0.72f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.72f, 6.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.56f, 0.14f, 0.18f}, Color{80, 86, 94, 255},
                                0.40f, 0.72f, 0.0f, MatKind::Standard);

                if (!shadowPass && !prop.broken) {
                    const bool ewGreen = world.isTrafficGreenForHorizontal();
                    const bool nsGreen = world.isTrafficGreenForVertical();
                    const Color ewCol = ewGreen ? Color{48, 240, 96, 255}
                                                : (nsGreen ? Color{248, 54, 48, 255} : Color{248, 194, 42, 255});
                    const Color nsCol = nsGreen ? Color{48, 240, 96, 255}
                                                : (ewGreen ? Color{248, 54, 48, 255} : Color{248, 194, 42, 255});

                    drawModelStyled(cubeModel_, {0.0f, 5.15f, 0.42f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.06f, 0.28f, 0.96f}, Color{24, 96, 62, 255},
                                    0.42f, 0.20f, night ? 0.35f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);

                    drawModelStyled(cubeModel_, {0.22f, 2.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.22f, 0.36f, 0.26f}, Color{26, 30, 36, 255},
                                    0.50f, 0.35f, 0.0f, MatKind::Standard);
                    const Color pedCol = ewGreen ? Color{225, 244, 252, 255} : Color{246, 108, 36, 255};
                    drawModelStyled(cubeModel_, {0.335f, 2.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.02f, 0.20f, 0.16f}, pedCol, 0.12f, 0.0f, 1.8f, MatKind::EmissiveGlow);

                    drawModelStyled(cubeModel_, {1.05f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.30f, 0.74f, 0.30f}, Color{24, 28, 34, 255},
                                    0.55f, 0.35f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {1.21f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, ewCol, 0.1f, 0.0f, 2.1f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {0.89f, 5.62f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, ewCol, 0.1f, 0.0f, 2.1f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {1.05f, 5.62f, 0.16f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, nsCol, 0.1f, 0.0f, 2.1f, MatKind::EmissiveGlow);
                    drawModelStyled(sphereModel_, {1.05f, 5.62f, -0.16f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.11f, 0.11f, 0.11f}, nsCol, 0.1f, 0.0f, 2.1f, MatKind::EmissiveGlow);

                    const Color lampCol = night ? Color{255, 226, 162, 255} : Color{196, 202, 210, 255};
                    drawModelStyled(cubeModel_, {1.35f, 6.08f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.44f, 0.11f, 0.26f}, lampCol,
                                    0.18f, 0.2f, night ? 2.4f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    if (night) {
                        rlDrawRenderBatchActive();
                        rlSetTexture(0);
                        rlDisableDepthMask();
                        BeginBlendMode(BLEND_ADDITIVE);
                        // 3 softly feathered warm sodium ground pools (eliminates hard circle edge!)
                        DrawCylinder({1.35f, -0.02f, 0.0f}, 4.8f, 4.8f, 0.012f, 18, Color{255, 206, 132, 7});
                        DrawCylinder({1.35f, -0.015f, 0.0f}, 3.1f, 3.1f, 0.012f, 18, Color{255, 214, 142, 11});
                        DrawCylinder({1.35f, -0.01f, 0.0f}, 1.7f, 1.7f, 0.012f, 18, Color{255, 224, 158, 15});
                        DrawCylinder({1.35f, 0.02f, 0.0f}, 0.28f, 2.55f, 6.02f, 14, Color{255, 220, 152, 6});
                        rlDrawRenderBatchActive();
                        EndBlendMode();
                        rlEnableDepthMask();
                    }
                }
                break;
            }
            case StreetPropKind::Hydrant: {
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.12f, 0.26f}, Color{154, 34, 32, 255},
                                0.46f, 0.45f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.0f, 0.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.20f, 0.62f, 0.20f}, Color{178, 38, 36, 255},
                                0.38f, 0.38f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.0f, 0.72f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.21f, 0.14f, 0.21f}, Color{204, 174, 62, 255},
                                0.32f, 0.62f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.52f, -0.27f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                    {0.075f, 0.54f, 0.075f}, Color{192, 198, 206, 255},
                                    0.28f, 0.80f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.06f, 0.44f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                    {0.105f, 0.22f, 0.105f}, Color{192, 198, 206, 255},
                                    0.28f, 0.80f, 0.0f, MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.065f, 0.09f, 0.065f}, Color{198, 172, 68, 255},
                                    0.28f, 0.82f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::Crate: {
                drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.88f, 0.86f, 0.88f}, Color{144, 108, 68, 255},
                                0.84f, 0.0f, 0.0f, MatKind::BarkWood);
                if (!shadowPass) {
                    const Color battenCol{108, 78, 48, 255};
                    drawModelStyled(cubeModel_, {0.0f, 0.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.96f, 0.12f, 0.96f}, battenCol, 0.86f, 0.0f, 0.0f, MatKind::BarkWood);
                    drawModelStyled(cubeModel_, {0.0f, 0.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.96f, 0.12f, 0.96f}, battenCol, 0.86f, 0.0f, 0.0f, MatKind::BarkWood);
                    drawModelStyled(cubeModel_, {0.0f, 0.45f, 0.0f}, {0.0f, 0.0f, 1.0f}, 45.0f,
                                    {0.11f, 1.05f, 0.94f}, battenCol, 0.86f, 0.0f, 0.0f, MatKind::BarkWood);
                }
                break;
            }
            case StreetPropKind::TrafficCone: {
                if (!shadowPass) {
                    drawModelStyled(cubeModel_, {0.0f, 0.035f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.54f, 0.07f, 0.54f}, Color{28, 30, 34, 255},
                                    0.80f, 0.05f, 0.0f, MatKind::Standard);
                }
                drawModelStyled(coneModel_, {0.0f, 0.05f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.24f, 0.68f, 0.24f}, Color{216, 94, 28, 255},
                                0.52f, 0.0f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.155f, 0.095f, 0.155f}, Color{228, 230, 234, 255},
                                    0.26f, 0.15f, night ? 0.95f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    drawModelStyled(cylModel_, {0.0f, 0.50f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.105f, 0.075f, 0.105f}, Color{228, 230, 234, 255},
                                    0.26f, 0.15f, night ? 0.95f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::BusShelter: {
                const Color frameCol{42, 46, 52, 255};
                drawModelStyled(cubeModel_, {0.0f, 2.36f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {3.50f, 0.12f, 1.56f}, frameCol, 0.38f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {-1.62f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.10f, 2.36f, 0.10f}, frameCol, 0.38f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, { 1.62f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.10f, 2.36f, 0.10f}, frameCol, 0.38f, 0.65f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    drawModelStyled(cubeModel_, {0.0f, 1.18f, -0.62f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {3.14f, 2.16f, 0.05f}, Color{24, 36, 48, 255},
                                    0.08f, 0.2f, 0.0f, MatKind::CarGlass);
                    drawModelStyled(cubeModel_, {0.0f, 2.28f, -0.10f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.45f, 0.04f, 0.18f}, Color{236, 240, 246, 255},
                                    0.18f, 0.0f, night ? 2.1f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    // Metro transit map & poster cabinet (grounded paper poster in daylight, softly backlit at night)
                    drawModelStyled(cubeModel_, {1.62f, 1.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.10f, 2.08f, 1.24f}, night ? Color{128, 192, 224, 255} : Color{54, 96, 128, 255},
                                    0.32f, 0.08f, night ? 1.35f : 0.0f,
                                    night ? MatKind::EmissiveGlow : MatKind::Standard);
                    drawModelStyled(cubeModel_, {-0.25f, 0.48f, -0.34f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {2.10f, 0.08f, 0.44f}, Color{134, 96, 62, 255},
                                    0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                    for (int a = -1; a <= 1; a += 2) {
                        drawModelStyled(cubeModel_, {-0.25f + static_cast<float>(a) * 0.98f, 0.62f, -0.34f},
                                        {0.0f, 1.0f, 0.0f}, 0.0f, {0.05f, 0.26f, 0.40f},
                                        Color{174, 180, 188, 255}, 0.32f, 0.82f, 0.0f, MatKind::Standard);
                    }
                }
                break;
            }
            case StreetPropKind::Bench: {
                drawModelStyled(cubeModel_, {0.0f, 0.44f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.58f, 0.08f, 0.52f}, Color{128, 92, 58, 255},
                                0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                drawModelStyled(cubeModel_, {0.0f, 0.74f, -0.22f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.58f, 0.44f, 0.07f}, Color{128, 92, 58, 255},
                                0.82f, 0.0f, 0.0f, MatKind::BarkWood);
                drawModelStyled(cubeModel_, {-0.66f, 0.24f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.09f, 0.48f, 0.50f}, Color{38, 42, 46, 255},
                                0.46f, 0.65f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, { 0.66f, 0.24f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.09f, 0.48f, 0.50f}, Color{38, 42, 46, 255},
                                0.46f, 0.65f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::TrashBin: {
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.28f, 0.82f, 0.28f}, Color{42, 68, 54, 255},
                                0.58f, 0.25f, 0.0f, MatKind::Standard);
                drawModelStyled(sphereModel_, {0.0f, 0.82f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.29f, 0.14f, 0.29f}, Color{176, 182, 190, 255},
                                0.30f, 0.75f, 0.0f, MatKind::Standard);
                break;
            }
            case StreetPropKind::FuelPump: {
                drawModelStyled(cubeModel_, {0.0f, 0.96f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.86f, 1.72f, 0.48f}, Color{212, 214, 218, 255},
                                0.46f, 0.22f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 1.66f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.92f, 0.36f, 0.52f}, Color{186, 52, 34, 255},
                                0.36f, 0.18f, night ? 0.6f : 0.0f,
                                night ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 1.18f, 0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.46f, 0.28f, 0.04f}, Color{46, 134, 168, 255},
                                0.16f, 0.0f, night ? 1.4f : 0.25f, MatKind::EmissiveGlow);
                drawModelStyled(cubeModel_, {0.0f, 1.18f, -0.25f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.46f, 0.28f, 0.04f}, Color{46, 134, 168, 255},
                                0.16f, 0.0f, night ? 1.4f : 0.25f, MatKind::EmissiveGlow);
                break;
            }
            case StreetPropKind::BeachUmbrella: {
                const int colorIdx = std::abs(static_cast<int>(prop.pos.z * 0.3f)) % 4;
                const Color canopyCol = (colorIdx == 0) ? Color{178, 62, 54, 255}
                                      : (colorIdx == 1) ? Color{52, 118, 162, 255}
                                      : (colorIdx == 2) ? Color{198, 158, 62, 255}
                                                        : Color{64, 134, 108, 255};
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.055f, 2.25f, 0.055f}, Color{186, 156, 114, 255},
                                0.78f, 0.0f, 0.0f, MatKind::BarkWood);
                drawModelStyled(coneModel_, {0.0f, 1.95f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {1.75f, 0.62f, 1.75f}, canopyCol,
                                0.82f, 0.0f, 0.0f, MatKind::FabricCloth);
                if (!shadowPass) {
                    drawModelStyled(cubeModel_, {0.0f, 0.22f, 1.15f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.65f, 0.12f, 0.68f}, Color{216, 214, 206, 255},
                                    0.78f, 0.0f, 0.0f, MatKind::FabricCloth);
                    drawModelStyled(cubeModel_, {0.0f, 0.22f, -1.15f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {1.65f, 0.12f, 0.68f}, Color{216, 214, 206, 255},
                                    0.78f, 0.0f, 0.0f, MatKind::FabricCloth);
                }
                break;
            }
            case StreetPropKind::Bollard: {
                // Architectural Dressed Steel & Granite Sidewalk Security Bollard
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.135f, 0.84f, 0.135f}, Color{54, 58, 64, 255},
                                0.42f, 0.72f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    drawModelStyled(cylModel_, {0.0f, 0.70f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.142f, 0.055f, 0.142f}, Color{198, 204, 212, 255},
                                    0.24f, 0.88f, 0.0f, MatKind::Standard);
                    drawModelStyled(sphereModel_, {0.0f, 0.84f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.132f, 0.068f, 0.132f}, Color{62, 66, 74, 255},
                                    0.38f, 0.74f, 0.0f, MatKind::Standard);
                }
                break;
            }
            case StreetPropKind::Billboard: {
                // Tall Galvanized Steel Monopole + Grounded Editorial Ad Board (Backlit ONLY at night!)
                drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 6.2f, 0.26f}, Color{68, 72, 78, 255},
                                0.44f, 0.72f, 0.0f, MatKind::Standard);
                drawModelStyled(cubeModel_, {0.0f, 7.55f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {5.90f, 2.95f, 0.36f}, Color{32, 36, 42, 255},
                                0.46f, 0.62f, 0.0f, MatKind::Standard);
                if (!shadowPass) {
                    const int theme = std::abs(static_cast<int>(prop.pos.x + prop.pos.z)) % 3;
                    const Color bgCol = (theme == 0) ? Color{36, 56, 78, 255}
                                      : (theme == 1) ? Color{154, 72, 52, 255}
                                                     : Color{42, 76, 68, 255};
                    const Color stripeCol = (theme == 0) ? Color{216, 182, 92, 255}
                                          : (theme == 1) ? Color{226, 222, 212, 255}
                                                         : Color{186, 206, 198, 255};
                    const MatKind bbMat = night ? MatKind::EmissiveGlow : MatKind::Standard;
                    const float bbGlow  = night ? 1.15f : 0.0f;
                    for (int face = -1; face <= 1; face += 2) {
                        const float fz = static_cast<float>(face) * 0.20f;
                        drawModelStyled(cubeModel_, {0.0f, 7.55f, fz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {5.55f, 2.60f, 0.04f}, bgCol,
                                        0.48f, 0.05f, night ? 0.95f : 0.0f, bbMat);
                        drawModelStyled(cylModel_, {-1.45f, 7.65f, fz + face * 0.03f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                        {0.76f, 0.04f, 0.76f}, stripeCol,
                                        0.42f, 0.05f, bbGlow, bbMat);
                        drawModelStyled(cubeModel_, {0.95f, 7.82f, fz + face * 0.03f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {2.55f, 0.34f, 0.04f}, stripeCol,
                                        0.42f, 0.05f, bbGlow, bbMat);
                        drawModelStyled(cubeModel_, {0.72f, 7.22f, fz + face * 0.03f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                        {2.10f, 0.22f, 0.04f}, Color{226, 228, 232, 255},
                                        0.42f, 0.05f, bbGlow, bbMat);
                    }
                }
                break;
            }
        }
        rlPopMatrix();
    }

    // Dense 220-Streak Multi-Depth Slanted 3D Coastal Rain Volume + Ground Splash Rings when Raining!
    if (!shadowPass && world.isRainWeather()) {
        for (int i = 0; i < 220; ++i) {
            const float seed = static_cast<float>(i) * 19.37f;
            const bool nearLayer = (i < 96);
            const float spread = nearLayer ? 13.5f : 26.0f;
            const float rx = camPos.x + std::fmod(std::sin(seed * 1.71f) * 43758.5f, spread);
            const float rz = camPos.z + std::fmod(std::cos(seed * 2.39f) * 31415.9f, spread);
            const float fallSpan = nearLayer ? 10.5f : 15.0f;
            const float speed = nearLayer ? 22.5f : 19.0f;
            const float fallCycle = std::fmod(animT * speed + seed * 3.1f, fallSpan);
            const float ry = std::max(0.05f, camPos.y - 2.0f + (fallSpan - fallCycle));
            const float streakLen = nearLayer ? 0.85f : 1.05f;
            const unsigned char alpha = nearLayer ? 118 : 74;
            DrawLine3D({rx, ry + streakLen, rz}, {rx - 0.14f, ry, rz - 0.08f}, Color{206, 224, 240, alpha});
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

    // 1) Multi-layer feathered oval contact AO shadow + 4 tire contact patches + Custom LED Neon Underglow Pool!
    if (!shadowPass) {
        const float hoverH = std::max(0.0f, v.posY - groundSurfY);
        rlPushMatrix();
        rlTranslatef(v.pos.x, groundSurfY + 0.014f, v.pos.z);
        rlRotatef(-v.heading * kRadToDeg, 0.0f, 1.0f, 0.0f);
        const float shadowScale = std::clamp(1.0f - hoverH * 0.055f, 0.42f, 1.0f);

        // 3 concentric soft feathered oval AO layers (tightened when real directional PCF shadows are active)
        const float aoMul = shadowsActive_ ? 0.68f : 1.0f;
        const float aoLayers[3][3] = {
            {halfL * 1.04f * shadowScale, halfW * 1.12f * shadowScale, 28.0f * aoMul}, // Soft outer penumbra
            {halfL * 0.94f * shadowScale, halfW * 0.98f * shadowScale, 58.0f * aoMul}, // Body silhouette AO
            {halfL * 0.78f * shadowScale, halfW * 0.80f * shadowScale, 88.0f}          // Deep under-chassis core
        };
        for (int li = 0; li < 3; ++li) {
            rlPushMatrix();
            rlTranslatef(0.0f, static_cast<float>(li) * 0.0014f, 0.0f);
            rlScalef(aoLayers[li][0], 1.0f, aoLayers[li][1]);
            DrawCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 0.004f, 22,
                         Color{6, 8, 13, static_cast<unsigned char>(aoLayers[li][2] * shadowScale)});
            rlPopMatrix();
        }
        // 4 dark tire contact-patch AO ovals right beneath each wheel when grounded
        if (hoverH < 0.35f) {
            const float tireAxleX = halfL * 0.63f;
            const float tireAxleZ = halfW * 0.88f;
            const auto patchA = static_cast<unsigned char>(118.0f * (1.0f - hoverH / 0.35f));
            for (int side = -1; side <= 1; side += 2) {
                for (int front = -1; front <= 1; front += 2) {
                    rlPushMatrix();
                    rlTranslatef(static_cast<float>(front) * tireAxleX, 0.0045f, static_cast<float>(side) * tireAxleZ);
                    rlScalef(0.38f, 1.0f, 0.20f);
                    DrawCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 0.003f, 12, Color{4, 5, 9, patchA});
                    rlPopMatrix();
                }
            }
        }

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

        // Rear performance grille vent, license plate & twin/quad hollow chrome exhaust tips (with glowing nitro inner cores!)
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
            // Hollow brushed chrome outer exhaust sleeve
            drawModelStyled(wheelModel_, {-halfL - 0.015f, exY, pz}, {0.0f, 1.0f, 0.0f}, 90.0f,
                            {0.058f, 0.058f, 0.16f}, chromeCol, 0.18f, 0.92f, 0.0f, MatKind::Standard);
            // Solid dark soot / nitro-glowing inner exhaust baffle recessed inside hollow pipe
            drawModelStyled(cylModel_, {-halfL - 0.058f, exY, pz}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.036f, 0.075f, 0.036f},
                            v.nitroActive ? Color{255, 128, 42, 255} : Color{14, 14, 18, 255},
                            v.nitroActive ? 0.12f : 0.78f, v.nitroActive ? 0.0f : 0.20f,
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
        // Horizontal chrome/anodized grille slats + metallic center brand emblem + front registration plate
        drawModelStyled(cubeModel_, {0.014f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.036f, 0.022f, W * 0.31f}, chromeCol, 0.16f, 0.92f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.014f, -0.028f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.034f, 0.016f, W * 0.29f}, darkTrim, 0.28f, 0.65f, 0.0f, MatKind::Standard);
        drawModelStyled(sphereModel_, {0.028f, 0.022f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.016f, 0.038f, 0.044f}, chromeCol, 0.10f, 0.96f, 0.0f, MatKind::Standard);
        drawModelStyled(cubeModel_, {0.016f, -grilleH * 0.40f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.030f, 0.095f, 0.30f}, Color{242, 245, 250, 255}, 0.40f, 0.10f, 0.0f, MatKind::Standard);
        for (int side = -1; side <= 1; side += 2) {
            drawModelStyled(cubeModel_, {-0.065f, -0.02f, static_cast<float>(side) * (halfW * 0.46f)},
                            {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.045f, grilleH * 0.70f, W * 0.12f}, Color{12, 14, 18, 255}, 0.60f, 0.30f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();

        // Flush Chrome Door Handles, Lower Side-Skirt Rocker Molding & Dual Windshield Wipers!
        const float handleY = 0.24f + H * 0.36f;
        const bool fourDoorCar = (v.kind == VehicleKind::Compact || v.kind == VehicleKind::LuxurySedan ||
                                  v.kind == VehicleKind::PoliceCruiser || v.kind == VehicleKind::Taxi ||
                                  v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv);
        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            const float hz = sgn * (halfW * 0.985f - ((side < 0) ? v.rightDamage : v.leftDamage) * 0.04f);
            drawModelStyled(cubeModel_, {-0.06f, handleY, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.18f, 0.034f, 0.032f}, chromeCol, 0.16f, 0.90f, 0.0f, MatKind::Standard);
            if (fourDoorCar) {
                drawModelStyled(cubeModel_, {-0.72f, handleY, hz}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.18f, 0.034f, 0.032f}, chromeCol, 0.16f, 0.90f, 0.0f, MatKind::Standard);
            }
            // Sleek lower rocker side-skirt aero molding cleanly seated below curved doors (zero Z-fighting!)
            if (v.kind != VehicleKind::DeliveryVan && v.kind != VehicleKind::Ambulance && v.kind != VehicleKind::FireTruck) {
                drawModelStyled(cubeModel_, {0.0f, 0.23f, sgn * (halfW * 0.982f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {L * 0.45f, 0.030f, 0.026f},
                                (v.kind == VehicleKind::LuxurySedan) ? chromeCol : darkTrim,
                                0.22f, 0.78f, 0.0f, MatKind::Standard);
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

    // 4) Hollow 24-Segment Bevelled Tires + Deep-Concave 5-Twin-Spoke Alloy Rims + Ventilated Rotors & Brembo Calipers!
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

            // Tiered sport brake caliper inside rim (Brembo Gold on exotics, Steel Blue on JDM, Red on sports/muscle)
            if (!shadowPass) {
                const Color calCol = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype)
                                         ? Color{236, 178, 38, 255}
                                         : ((v.kind == VehicleKind::JdmTuner) ? Color{48, 134, 212, 255}
                                         : ((v.kind == VehicleKind::DeliveryVan || v.kind == VehicleKind::Taxi)
                                                ? Color{92, 98, 108, 255}
                                                : Color{216, 42, 38, 255}));
                drawModelStyled(cubeModel_, {-wheelR * 0.34f, wheelR * 0.14f, static_cast<float>(side) * (tireW * 0.24f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.115f, 0.175f, 0.068f},
                                calCol, 0.24f, 0.64f, 0.0f, MatKind::Standard);
            }

            rlRotatef(-v.wheelSpin * kRadToDeg, 0.0f, 0.0f, 1.0f);

            // Hollow annular rubber tire (exposes deep-concave 3D spokes, rotor & Brembo caliper!)
            drawModelStyled(wheelModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {wheelR, wheelR, tireW}, Color{20, 22, 26, 255},
                            0.74f, 0.06f, 0.0f, MatKind::Standard);

            if (!shadowPass) {
                // Solid recessed steel brake rotor disc (cylModel_ rotated +90 deg X so axis aligns with Z)
                const float heat = clampf(v.brakeHeat * ((front > 0) ? 1.0f : 0.72f), 0.0f, 1.0f);
                const bool hotRotor = (!v.destroyed && heat > 0.16f);
                const Color rotorCol = hotRotor
                                           ? Color{
                                                 static_cast<unsigned char>(lerpf(110.0f, 255.0f, heat)),
                                                 static_cast<unsigned char>(lerpf(56.0f, 118.0f, heat)),
                                                 static_cast<unsigned char>(lerpf(42.0f, 24.0f, heat)),
                                                 255}
                                           : Color{128, 134, 144, 255};
                const float rotorThick = tireW * 0.42f;
                drawModelStyled(cylModel_, {0.0f, 0.0f, -rotorThick * 0.5f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                {wheelR * 0.54f, rotorThick, wheelR * 0.54f}, rotorCol,
                                hotRotor ? 0.18f : 0.28f, hotRotor ? 0.35f : 0.88f,
                                hotRotor ? (heat * 2.4f) : 0.0f,
                                hotRotor ? MatKind::EmissiveGlow : MatKind::Standard);
                drawModelStyled(sportRimModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {wheelR, wheelR, tireW}, rimCol,
                                (v.rimStyle == 2) ? 0.14f : 0.18f, 0.90f, rimEmissive, rimMat);
                // Solid machined center hubcap seated snugly into concave hub (or chrome dog-dish on PoliceCruiser)
                const float capR = (v.kind == VehicleKind::PoliceCruiser) ? 0.25f : 0.145f;
                const float capThick = tireW * ((v.kind == VehicleKind::PoliceCruiser) ? 0.94f : 0.68f);
                drawModelStyled(cylModel_, {0.0f, 0.0f, -capThick * 0.5f}, {1.0f, 0.0f, 0.0f}, 90.0f,
                                {wheelR * capR, capThick, wheelR * capR},
                                (v.kind == VehicleKind::PoliceCruiser) ? chromeCol : Color{36, 40, 48, 255},
                                0.14f, 0.92f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();
        }
    }

    // 5) Multi-Element Crystal Projector Headlights, Live Turn Blinkers, Blade Taillights & Exhaust Flames!
    if (!shadowPass && !v.destroyed) {
        const Color headCol = toRayColor(getVehicleHeadlightColorRGBA(v.headlightColorIdx, nightMode));
        const float headGlow = (v.headlightColorIdx > 0) ? (nightMode ? 3.2f : 1.9f) : (nightMode ? 2.7f : 1.15f);
        const float headY = (v.kind == VehicleKind::TrackPrototype) ? 0.38f
                          : ((v.kind == VehicleKind::Hypercar) ? 0.43f
                          : ((v.kind == VehicleKind::ClassicRoadster || v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) ? 0.48f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv ||
                              v.kind == VehicleKind::OffroadPickup || v.kind == VehicleKind::Ambulance ||
                              v.kind == VehicleKind::FireTruck) ? 0.78f : 0.58f)));
        const float headX = halfL * 0.982f;
        const float headZ = halfW * 0.65f;
        const bool blinkOn = (v.turnSignalPhase < 0.50f);
        const bool leftBlink  = (v.steerVisual < -0.34f) && blinkOn;
        const bool rightBlink = (v.steerVisual >  0.34f) && blinkOn;

        for (int side = -1; side <= 1; side += 2) {
            const bool sideBlink = (side < 0) ? leftBlink : rightBlink;
            rlPushMatrix();
            rlTranslatef(headX, headY, static_cast<float>(side) * headZ);
            rlRotatef(static_cast<float>(-side) * 16.0f, 0.0f, 1.0f, 0.0f);
            const float hHeight = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype) ? 0.072f : 0.105f;
            // Smoked dark chrome projector bezel housing + twin circular crystal projector lenses + LED DRL eyebrow!
            drawModelStyled(cubeModel_, {0.002f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.125f, hHeight + 0.032f, 0.36f}, Color{16, 18, 24, 255}, 0.16f, 0.78f, 0.0f, MatKind::Standard);
            for (int lens = -1; lens <= 1; lens += 2) {
                drawModelStyled(sphereModel_, {0.044f, -0.008f, static_cast<float>(lens) * 0.068f},
                                {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.040f, hHeight * 0.44f, hHeight * 0.48f},
                                headCol, 0.06f, 0.10f, headGlow * 1.15f, MatKind::EmissiveGlow);
            }
            // Crisp signature LED DRL eyebrow bar
            drawModelStyled(cubeModel_, {0.034f, hHeight * 0.50f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.085f, 0.022f, 0.34f},
                            headCol, 0.1f, 0.0f, headGlow * 1.30f, MatKind::EmissiveGlow);
            // Outer amber corner side-marker / live blinking turn signal!
            drawModelStyled(cubeModel_, {0.026f, -0.006f, static_cast<float>(side) * 0.155f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.082f, hHeight * 0.62f, 0.052f},
                            Color{255, 168, 28, 255}, 0.12f, 0.15f,
                            sideBlink ? 3.4f : (nightMode ? 0.9f : 0.15f),
                            (sideBlink || nightMode) ? MatKind::EmissiveGlow : MatKind::Standard);
            rlPopMatrix();
        }


        if (nightMode && (v.isPlayerDriving || v.hasNpcDriver || v.headlightColorIdx > 0)) {
            Color beamCol = headCol;
            beamCol.a = static_cast<unsigned char>((v.headlightColorIdx > 0) ? 58 : 42);
            DrawCube({halfL + 4.8f, 0.028f - stanceOffsetY, 0.0f}, 8.8f, 0.015f, W * 1.85f, beamCol);
        }

        const Color tailCol = v.brakingVisual ? Color{255, 38, 36, 255} : Color{228, 28, 38, 255};
        const float tailGlow = v.brakingVisual ? 2.95f : 1.75f;
        const float tailY = (v.kind == VehicleKind::TrackPrototype) ? 0.54f
                          : ((v.kind == VehicleKind::Hypercar) ? 0.58f
                          : ((v.kind == VehicleKind::ClassicRoadster || v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner) ? 0.64f
                          : ((v.kind == VehicleKind::SUV || v.kind == VehicleKind::ArmoredSuv ||
                              v.kind == VehicleKind::OffroadPickup || v.kind == VehicleKind::Ambulance ||
                              v.kind == VehicleKind::FireTruck) ? 0.86f : 0.65f)));

        if (v.kind == VehicleKind::TrackPrototype || v.kind == VehicleKind::Hypercar ||
            v.kind == VehicleKind::SportsCoupe || v.kind == VehicleKind::JdmTuner ||
            v.kind == VehicleKind::LuxurySedan || v.kind == VehicleKind::PoliceCruiser) {
            // Smoked housing backing + sleek full-width modern LED blade lightbar + outer 3D corner pods!
            drawModelStyled(cubeModel_, {-halfL - 0.002f, tailY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.044f, 0.095f, W * 0.84f}, Color{16, 18, 24, 255}, 0.16f, 0.70f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {-halfL - 0.010f, tailY, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.050f, 0.056f, W * 0.80f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL * 0.985f, tailY, -halfW * 0.68f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.082f, 0.095f, 0.22f}, tailCol, 0.1f, 0.0f, tailGlow * 1.15f, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL * 0.985f, tailY,  halfW * 0.68f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.082f, 0.095f, 0.22f}, tailCol, 0.1f, 0.0f, tailGlow * 1.15f, MatKind::EmissiveGlow);
        } else {
            drawModelStyled(cubeModel_, {-halfL - 0.004f, tailY, -halfW * 0.64f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.12f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
            drawModelStyled(cubeModel_, {-halfL - 0.004f, tailY,  halfW * 0.64f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.06f, 0.12f, 0.30f}, tailCol, 0.1f, 0.0f, tailGlow, MatKind::EmissiveGlow);
        }

        // Rear amber blinking turn signals when turning!
        for (int side = -1; side <= 1; side += 2) {
            const bool sideBlink = (side < 0) ? leftBlink : rightBlink;
            if (sideBlink) {
                drawModelStyled(cubeModel_, {-halfL - 0.012f, tailY - 0.048f, static_cast<float>(side) * (halfW * 0.72f)},
                                {0.0f, 1.0f, 0.0f}, 0.0f, {0.044f, 0.036f, 0.16f},
                                Color{255, 174, 30, 255}, 0.08f, 0.0f, 3.3f, MatKind::EmissiveGlow);
            }
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

        const float exZ = (v.kind == VehicleKind::Hypercar || v.kind == VehicleKind::TrackPrototype)
                              ? (halfW * 0.18f)
                              : (halfW * 0.54f);
        const float exY = (v.kind == VehicleKind::TrackPrototype) ? 0.38f
                        : ((v.kind == VehicleKind::Hypercar) ? 0.34f : 0.24f);
        if (v.nitroActive) {
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
        } else if (v.backfireTimer > 0.0f) {
            // Snappy orange-gold + blue twin-exhaust upshift / turbo lift-off backfire pop!
            const float bfScale = clampf(v.backfireTimer / 0.14f, 0.2f, 1.0f);
            for (int side = -1; side <= 1; side += 2) {
                const float pz = static_cast<float>(side) * exZ;
                drawModelStyled(coneModel_, {-halfL - 0.05f, exY, pz}, {0.0f, 0.0f, 1.0f}, 90.0f,
                                {0.085f * bfScale, 0.46f * bfScale, 0.085f * bfScale}, Color{255, 148, 38, 255},
                                0.1f, 0.0f, 3.4f, MatKind::EmissiveGlow);
                drawModelStyled(coneModel_, {-halfL - 0.03f, exY, pz}, {0.0f, 0.0f, 1.0f}, 90.0f,
                                {0.046f * bfScale, 0.24f * bfScale, 0.046f * bfScale}, Color{130, 214, 255, 255},
                                0.08f, 0.0f, 3.8f, MatKind::EmissiveGlow);
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
                                 const WeaponSlotState* customWep,
                                 const NpcVisualExtra* npcExtra,
                                 bool aimingOrFiring) {
    rlPushMatrix();
    rlTranslatef(pos.x, groundY, pos.z);
    rlRotatef(-yaw * kRadToDeg, 0.0f, 1.0f, 0.0f);

    const float hScale = (npcExtra != nullptr) ? clampf(npcExtra->heightScale, 0.88f, 1.12f) : 1.0f;
    const float fallProg = (npcExtra != nullptr) ? clampf(npcExtra->fallProgress, 0.0f, 1.0f) : 1.0f;
    const NpcActivity npcAct = (npcExtra != nullptr) ? npcExtra->activity : NpcActivity::None;
    const NpcHeldProp heldProp = (npcExtra != nullptr) ? npcExtra->heldProp : NpcHeldProp::None;
    const bool holdUmbrella = (npcExtra != nullptr && npcExtra->holdingUmbrella &&
                               alive && !armed && !panicking && npcAct != NpcActivity::HandsUp);
    const bool npcNight = (npcExtra != nullptr && npcExtra->nightMode);

    // Subtle grounded contact AO directly under feet (tightened when real directional PCF shadows are active)
    if (!shadowPass && alive) {
        const float surfY = (shadowGroundY > -900.0f) ? shadowGroundY : groundY;
        const float airH = std::max(0.0f, groundY - surfY);
        const float shScale = clampf(1.0f - airH * 0.22f, 0.42f, 1.0f) * (0.94f + 0.06f * hScale);
        const float shY = (surfY - groundY) + 0.008f;
        if (shadowsActive_) {
            const auto coreA = static_cast<unsigned char>(clampf(42.0f * shScale, 14.0f, 42.0f));
            DrawCylinder({0.0f, shY, 0.0f},
                         0.22f * shScale, 0.22f * shScale, 0.004f, 14, Color{6, 8, 12, coreA});
        } else {
            const auto outerA = static_cast<unsigned char>(clampf(44.0f * shScale, 14.0f, 44.0f));
            const auto coreA  = static_cast<unsigned char>(clampf(86.0f * shScale, 28.0f, 86.0f));
            if (holdUmbrella) {
                const auto umbA = static_cast<unsigned char>(clampf(24.0f * shScale, 8.0f, 24.0f));
                DrawCylinder({0.0f, shY - 0.002f, 0.0f},
                             0.58f * shScale, 0.58f * shScale, 0.004f, 14, Color{8, 10, 15, umbA});
            }
            DrawCylinder({0.0f, shY, 0.0f},
                         0.40f * shScale, 0.40f * shScale, 0.005f, 14, Color{8, 10, 15, outerA});
            DrawCylinder({0.0f, shY + 0.003f, 0.0f},
                         0.24f * shScale, 0.24f * shScale, 0.005f, 14, Color{6, 8, 12, coreA});
        }
    }

    if (!alive) {
        rlTranslatef(0.0f, 0.16f * fallProg, 0.0f);
        rlRotatef(90.0f * fallProg, 0.0f, 0.0f, 1.0f);
    }
    const float wScale = 0.96f + 0.04f * hScale;
    rlScalef(wScale, hScale, wScale);

    const float strideAmp = panicking ? 38.0f : 28.0f;
    const float legSwing  = alive ? std::sin(animPhase) * strideAmp : 0.0f;
    const Color topC  = toRayColor(top);
    const Color botC  = toRayColor(bottom);
    const Color skinC = toRayColor(skin);
    const Color accC  = toRayColor(getOutfitSpec(styleOutfit).accent);
    const Color shoeC = (styleOutfit == OutfitId::ViceWhiteSuit) ? Color{230, 232, 238, 255} : Color{26, 28, 34, 255};
    const MatKind clothMat = (styleOutfit == OutfitId::RacerLeather) ? MatKind::Standard : MatKind::FabricCloth;

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

        // Rounded hip cap & sculpted tapered thigh (FabricCloth woven denim/trouser shader)
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.094f, 0.092f, 0.094f}, botC, 0.80f, 0.0f, 0.0f, MatKind::FabricCloth);
        drawModelStyled(cylModel_, {0.0f, -0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.092f, 0.34f, 0.092f}, botC, 0.80f, 0.0f, 0.0f, MatKind::FabricCloth);

        // Articulated Knee Joint + Lower Shin + Detailed Multi-Part Shoe
        rlPushMatrix();
        rlTranslatef(0.0f, -0.34f, 0.0f);
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.084f, 0.082f, 0.084f}, botC, 0.80f, 0.0f, 0.0f, MatKind::FabricCloth);
        rlRotatef(kneeBend, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, -0.34f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.078f, 0.34f, 0.078f}, botC, 0.82f, 0.0f, 0.0f, MatKind::FabricCloth);
        if (!shadowPass) {
            // Ankle cuff, contoured sneaker/shoe upper, rounded toe cap & contrasting midsole!
            drawModelStyled(cylModel_, {0.0f, -0.345f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.066f, 0.05f, 0.066f}, Color{226, 228, 234, 255}, 0.76f, 0.0f, 0.0f, MatKind::FabricCloth);
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
    const float topRough = (styleOutfit == OutfitId::RacerLeather) ? 0.28f : 0.72f;
    const float topMetal = (styleOutfit == OutfitId::GoldBossCoat) ? 0.35f : 0.04f;
    drawModelStyled(cubeModel_, {0.0f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.246f, 0.072f, 0.396f}, Color{24, 26, 32, 255}, 0.46f, 0.30f, 0.0f, MatKind::Standard);
    drawModelStyled(cubeModel_, {0.0f, 0.94f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.242f, 0.28f, 0.394f}, topC, topRough, topMetal, 0.0f, clothMat);
    drawModelStyled(cubeModel_, {0.0f, 1.21f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.264f, 0.34f, 0.428f}, topC, topRough, topMetal, 0.0f, clothMat);
    // Organic rounded shoulder deltoids + collar ring
    for (int s = -1; s <= 1; s += 2) {
        drawModelStyled(sphereModel_, {0.0f, 1.305f, static_cast<float>(s) * 0.205f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.098f, 0.096f, 0.090f}, topC, topRough, topMetal, 0.0f, clothMat);
    }
    drawModelStyled(cylModel_, {0.0f, 1.375f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.104f, 0.044f, 0.104f}, topC, topRough, topMetal, 0.0f, clothMat);

    if (!shadowPass) {
        // Metallic silver/brass belt buckle on all characters
        drawModelStyled(cubeModel_, {0.130f, 0.77f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.020f, 0.052f, 0.068f},
                        isPolice ? Color{236, 196, 68, 255} : Color{196, 204, 216, 255},
                        0.22f, 0.88f, 0.0f, MatKind::Standard);

        if (isPolice && isSwat) {
            // Heavy SWAT Kevlar tactical plate-carrier vest + 3 rifle mag pouches + shoulder pauldrons + radio whip!
            drawModelStyled(cubeModel_, {0.0f, 1.11f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.335f, 0.46f, 0.47f}, Color{26, 32, 40, 255}, 0.56f, 0.22f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.172f, 1.19f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.075f, 0.24f}, Color{236, 240, 248, 255}, 0.35f, 0.1f, 0.0f, MatKind::Standard);
            for (int mp = -1; mp <= 1; ++mp) {
                drawModelStyled(cubeModel_, {0.180f, 0.99f, static_cast<float>(mp) * 0.115f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.048f, 0.135f, 0.082f}, Color{20, 24, 30, 255}, 0.60f, 0.15f, 0.0f, MatKind::FabricCloth);
            }
            // Tactical back radio whip antenna
            drawModelStyled(cylModel_, {-0.175f, 1.24f, -0.13f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.008f, 0.42f, 0.008f}, Color{28, 32, 38, 255}, 0.32f, 0.80f, 0.0f, MatKind::Standard);
        } else if (isPolice) {
            // SBPD Officer uniform tie, gold shield chest badge, shoulder radio mic & duty-belt pouches!
            drawModelStyled(cubeModel_, {0.138f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.34f, 0.052f}, Color{16, 20, 30, 255}, 0.58f, 0.05f, 0.0f, MatKind::FabricCloth);
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
                            {0.028f, 0.40f, 0.165f}, WHITE, 0.72f, 0.0f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.145f, 1.12f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.024f, 0.34f, 0.052f}, accC, 0.42f, 0.08f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.152f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.014f, 0.062f}, Color{238, 206, 86, 255}, 0.18f, 0.92f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.136f, 1.22f, -0.115f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.020f, 0.028f, 0.068f}, accC, 0.50f, 0.05f, 0.0f, MatKind::FabricCloth);
        } else if (styleOutfit == OutfitId::TacOpsArmor) {
            // Molded 3D Kevlar plate-carrier vest + 3 front mag pouches!
            drawModelStyled(cubeModel_, {0.0f, 1.11f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.325f, 0.46f, 0.46f}, accC, 0.54f, 0.22f, 0.0f, MatKind::FabricCloth);
            for (int mp = -1; mp <= 1; ++mp) {
                drawModelStyled(cubeModel_, {0.174f, 0.99f, static_cast<float>(mp) * 0.11f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.044f, 0.13f, 0.078f}, Color{28, 32, 36, 255}, 0.62f, 0.15f, 0.0f, MatKind::FabricCloth);
            }
        } else if (styleOutfit == OutfitId::GoldBossCoat) {
            // Tailored boss overcoat lower panels cleanly below waist belt (zero Z-fighting!) + gold lapel trim + medallion!
            drawModelStyled(cubeModel_, {-0.012f, 0.585f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.232f, 0.295f, 0.402f}, topC, 0.58f, 0.15f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.136f, 1.13f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.028f, 0.46f, 0.14f}, accC, 0.25f, 0.80f, 0.0f, MatKind::Standard);
            drawModelStyled(sphereModel_, {0.148f, 1.18f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.014f, 0.036f, 0.036f}, Color{250, 206, 58, 255}, 0.16f, 0.96f, 0.0f, MatKind::Standard);
        } else {
            // Tailored inner shirt collar + sleek metallic front jacket zipper
            const Color shirtCol{
                static_cast<unsigned char>(std::min(222, static_cast<int>(accC.r) * 85 / 100 + 20)),
                static_cast<unsigned char>(std::min(224, static_cast<int>(accC.g) * 85 / 100 + 20)),
                static_cast<unsigned char>(std::min(228, static_cast<int>(accC.b) * 85 / 100 + 20)),
                255};
            drawModelStyled(cubeModel_, {0.124f, 1.31f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.036f, 0.11f, 0.105f}, shirtCol, 0.74f, 0.0f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.136f, 1.06f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.012f, 0.46f, 0.015f}, Color{176, 184, 196, 255}, 0.28f, 0.80f, 0.0f, MatKind::Standard);
        }

        // Custom 3D Back Gear (Tactical Backpack / Heist Duffle Bag / Cyber Neon Wing Pack)!
        if (customChar != nullptr && customChar->backGearStyle > 0) {
            if (customChar->backGearStyle == 1) {
                // Tactical Olive/Charcoal Spec-Ops Backpack + Utility Pouch + Straps
                drawModelStyled(cubeModel_, {-0.20f, 1.10f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.16f, 0.44f, 0.32f}, Color{44, 52, 46, 255}, 0.65f, 0.10f, 0.0f, MatKind::FabricCloth);
                drawModelStyled(cubeModel_, {-0.29f, 1.00f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.06f, 0.20f, 0.24f}, Color{30, 34, 38, 255}, 0.58f, 0.15f, 0.0f, MatKind::FabricCloth);
                for (int s = -1; s <= 1; s += 2) {
                    drawModelStyled(cubeModel_, {0.02f, 1.16f, static_cast<float>(s) * 0.12f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                    {0.32f, 0.36f, 0.045f}, Color{26, 28, 34, 255}, 0.60f, 0.12f, 0.0f, MatKind::FabricCloth);
                }
            } else if (customChar->backGearStyle == 2) {
                // Diagonal Heist Duffle Bag with Gold Zipper Trim!
                rlPushMatrix();
                rlTranslatef(-0.21f, 1.08f, 0.0f);
                rlRotatef(32.0f, 1.0f, 0.0f, 0.0f);
                drawModelStyled(cylModel_, {0.0f, -0.26f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.135f, 0.52f, 0.135f}, Color{24, 26, 32, 255}, 0.56f, 0.12f, 0.0f, MatKind::FabricCloth);
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

    // Neck + Organic Sculpted Cranium & Jaw (SkinOrganics subsurface wrap!) + Ears/Eyes/Nose/Brows/Mouth
    rlPushMatrix();
    rlTranslatef(0.0f, 1.41f, 0.0f);
    if (alive) {
        const float headYaw = (npcExtra != nullptr) ? clampf(npcExtra->headLookYaw, -0.78f, 0.78f) : 0.0f;
        if (std::abs(headYaw) > 0.005f) {
            rlRotatef(-headYaw * kRadToDeg, 0.0f, 1.0f, 0.0f);
        }
        const float headPitch = (npcAct == NpcActivity::CheckingPhone)
                                    ? -0.44f
                                    : clampf(aimPitch * 0.65f, -0.65f, 0.75f);
        rlRotatef(headPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
    }
    drawModelStyled(cylModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.068f, 0.090f, 0.068f}, skinC, 0.62f, 0.0f, 0.0f, MatKind::SkinOrganics);
    // Smooth organic sculpted cranium + tapered lower jaw/chin
    drawModelStyled(sphereModel_, {0.0f, 0.175f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.132f, 0.140f, 0.124f}, skinC, 0.58f, 0.0f, 0.0f, MatKind::SkinOrganics);
    drawModelStyled(sphereModel_, {0.020f, 0.098f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                    {0.098f, 0.066f, 0.088f}, skinC, 0.58f, 0.0f, 0.0f, MatKind::SkinOrganics);

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
                                skinC, 0.60f, 0.0f, 0.0f, MatKind::SkinOrganics);
            }
        }
        // Sculpted nose bridge/tip
        drawModelStyled(sphereModel_, {0.128f, 0.156f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.022f, 0.024f, 0.016f}, skinC, 0.58f, 0.0f, 0.0f, MatKind::SkinOrganics);
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
                        {0.156f, 0.085f, 0.156f}, Color{22, 34, 64, 255}, 0.58f, 0.08f, 0.0f, MatKind::FabricCloth);
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
                        {0.152f, 0.095f, 0.152f}, hairCol, 0.62f, 0.05f, 0.0f, MatKind::FabricCloth);
        drawModelStyled(cubeModel_, {0.135f, 0.268f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.16f, 0.022f, 0.23f}, Color{20, 22, 28, 255}, 0.48f, 0.10f, 0.0f, MatKind::FabricCloth);
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
            // Gold Aviator Sunglasses (flush brow bar + twin tinted lenses + slim side temples)
            drawModelStyled(cubeModel_, {0.118f, 0.205f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.018f, 0.010f, 0.214f}, Color{236, 194, 68, 255}, 0.16f, 0.92f, 0.0f, MatKind::Standard);
            for (int s = -1; s <= 1; s += 2) {
                drawModelStyled(cubeModel_, {0.116f, 0.176f, static_cast<float>(s) * 0.054f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.020f, 0.050f, 0.080f}, Color{36, 28, 18, 255}, 0.08f, 0.65f, 0.0f, MatKind::CarGlass);
            }
        } else if (glassesStyle == 3) {
            // Cyan Tactical Goggles!
            drawModelStyled(cubeModel_, {0.120f, 0.184f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.028f, 0.062f, 0.224f}, Color{44, 232, 255, 255}, 0.08f, 0.3f, 2.2f, MatKind::EmissiveGlow);
        } else {
            // Wayfarer dark shades seated cleanly on bridge + thin ear arms
            drawModelStyled(cubeModel_, {0.116f, 0.182f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.022f, 0.052f, 0.212f}, Color{16, 20, 28, 255},
                            0.12f, 0.45f, 0.0f, MatKind::CarGlass);
            for (int s = -1; s <= 1; s += 2) {
                drawModelStyled(cubeModel_, {0.052f, 0.186f, static_cast<float>(s) * 0.112f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.130f, 0.012f, 0.010f}, Color{18, 20, 26, 255}, 0.24f, 0.35f, 0.0f, MatKind::Standard);
            }
        }
    }
    rlPopMatrix();

    // Articulated Arms, Elbows, Jacket Cuffs, Wristwatch, Hands, 3D Umbrellas, Props & Multi-Part 3D Weapons!
    auto drawHangingSculptedArm = [&](int side, float elbowDeg, float shoulderDeg = 0.0f, int handPropMode = 0) {
        // Upper arm sleeve
        drawModelStyled(cylModel_, {0.0f, -0.22f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.068f, 0.22f, 0.068f}, topC, topRough, topMetal, 0.0f, clothMat);
        // Articulated elbow joint + lower forearm sleeve + cuff + wristwatch + sculpted hand
        rlPushMatrix();
        rlTranslatef(0.0f, -0.22f, 0.0f);
        drawModelStyled(sphereModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.064f, 0.062f, 0.064f}, topC, topRough, topMetal, 0.0f, clothMat);
        rlRotatef(elbowDeg, 0.0f, 0.0f, 1.0f);
        drawModelStyled(cylModel_, {0.0f, -0.21f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {0.058f, 0.21f, 0.058f}, topC, topRough, topMetal, 0.0f, clothMat);
        if (!shadowPass) {
            drawModelStyled(cylModel_, {0.0f, -0.215f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.062f, 0.028f, 0.062f}, accC, 0.52f, 0.08f, 0.0f, clothMat);
            if (side < 0) {
                // Sleek metallic wristwatch on left wrist!
                drawModelStyled(cylModel_, {0.0f, -0.236f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.056f, 0.022f, 0.056f}, Color{198, 206, 220, 255}, 0.18f, 0.90f, 0.0f, MatKind::Standard);
            }
            drawModelStyled(sphereModel_, {0.0f, -0.275f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.054f, 0.064f, 0.048f}, skinC, 0.60f, 0.0f, 0.0f, MatKind::SkinOrganics);
        }

        // Held 3D NPC props anchored directly in the hand frame
        if (handPropMode == 1 && npcExtra != nullptr) {
            // Grounded Fabric 3D Rain Umbrella (drawn in shadow pass too so canopy casts real shadows!)
            rlPushMatrix();
            rlTranslatef(0.0f, -0.275f, 0.0f);
            rlRotatef(-(shoulderDeg + elbowDeg), 0.0f, 0.0f, 1.0f);
            rlRotatef(14.0f, 0.0f, 0.0f, 1.0f);
            rlRotatef(14.0f, 1.0f, 0.0f, 0.0f);
            const Color umbCol = toRayColor(npcExtra->umbrellaColor);
            if (!shadowPass) {
                // Ergonomic dark umbrella handle & sleek aluminum shaft
                drawModelStyled(cylModel_, {0.0f, -0.095f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.018f, 0.12f, 0.018f}, Color{26, 28, 34, 255}, 0.48f, 0.20f, 0.0f, MatKind::Standard);
                drawModelStyled(cylModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.011f, 0.86f, 0.011f}, Color{196, 204, 218, 255}, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            }
            // Sculpted 16-panel woven nylon canopy cone + smooth crown dome + metallic ferrule tip
            drawModelStyled(coneModel_, {0.0f, 0.75f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.58f, 0.24f, 0.58f}, umbCol, 0.48f, 0.04f, 0.0f, MatKind::FabricCloth);
            if (!shadowPass) {
                drawModelStyled(sphereModel_, {0.0f, 0.90f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.26f, 0.075f, 0.26f}, umbCol, 0.46f, 0.05f, 0.0f, MatKind::FabricCloth);
                drawModelStyled(cylModel_, {0.0f, 0.95f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.010f, 0.085f, 0.010f}, Color{212, 218, 230, 255}, 0.18f, 0.90f, 0.0f, MatKind::Standard);
            }
            rlPopMatrix();
        } else if (!shadowPass && handPropMode == 2) {
            // Glowing Smartphone held up while CheckingPhone!
            drawModelStyled(cubeModel_, {0.0f, -0.285f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.016f, 0.132f, 0.068f}, Color{22, 26, 34, 255}, 0.24f, 0.76f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.010f, -0.285f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.006f, 0.114f, 0.056f}, Color{156, 224, 255, 255},
                            0.08f, 0.0f, npcNight ? 2.1f : 1.15f, MatKind::EmissiveGlow);
        } else if (!shadowPass && handPropMode == 3) {
            // Espresso Leather Executive Briefcase with Brass Clasps
            drawModelStyled(cubeModel_, {0.0f, -0.335f, 0.015f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.11f, 0.04f, 0.025f}, Color{36, 24, 18, 255}, 0.55f, 0.12f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, -0.475f, 0.02f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.36f, 0.245f, 0.086f}, Color{48, 32, 22, 255}, 0.44f, 0.14f, 0.0f, MatKind::Standard);
            for (int cs = -1; cs <= 1; cs += 2) {
                drawModelStyled(cubeModel_, {static_cast<float>(cs) * 0.095f, -0.365f, 0.065f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {0.028f, 0.022f, 0.012f}, Color{230, 192, 74, 255}, 0.22f, 0.88f, 0.0f, MatKind::Standard);
            }
        } else if (!shadowPass && handPropMode == 4) {
            // Upright Takeaway Coffee Cup with Kraft Cardboard Sleeve & Sip Lid
            rlPushMatrix();
            rlTranslatef(0.0f, -0.275f, 0.0f);
            rlRotatef(-(shoulderDeg + elbowDeg), 0.0f, 0.0f, 1.0f);
            drawModelStyled(cylModel_, {0.0f, -0.055f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.039f, 0.125f, 0.039f}, Color{242, 240, 234, 255}, 0.62f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.0f, -0.015f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.042f, 0.054f, 0.042f}, Color{176, 126, 76, 255}, 0.78f, 0.0f, 0.0f, MatKind::Standard);
            drawModelStyled(cylModel_, {0.0f, 0.068f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.044f, 0.020f, 0.044f}, Color{34, 28, 24, 255}, 0.42f, 0.08f, 0.0f, MatKind::Standard);
            rlPopMatrix();
        } else if (!shadowPass && handPropMode == 5) {
            // Boutique Canvas Shopping Tote Bag
            drawModelStyled(cubeModel_, {0.0f, -0.34f, 0.02f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 0.05f, 0.022f}, Color{236, 232, 224, 255}, 0.72f, 0.0f, 0.0f, MatKind::FabricCloth);
            drawModelStyled(cubeModel_, {0.0f, -0.495f, 0.03f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.27f, 0.28f, 0.13f}, accC, 0.72f, 0.02f, 0.0f, MatKind::FabricCloth);
        } else if (!shadowPass && handPropMode == 6) {
            // Smartphone carried casually in hand while strolling
            drawModelStyled(cubeModel_, {0.02f, -0.295f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.016f, 0.120f, 0.064f}, Color{24, 28, 36, 255}, 0.25f, 0.72f, 0.0f, MatKind::Standard);
        }
        rlPopMatrix();
    };

    for (int side = -1; side <= 1; side += 2) {
        rlPushMatrix();
        rlTranslatef(0.0f, 1.31f, static_cast<float>(side) * 0.258f);
        if (armed && side == 1 && alive) {
            const bool activeAimPose = aimingOrFiring || (muzzleFlash > 0.0f);
            if (activeAimPose) {
                rlRotatef(aimPitch * kRadToDeg, 0.0f, 0.0f, 1.0f);
            } else {
                // Natural relaxed low-ready carry posture when strolling armed without aiming/firing!
                const float walkBobDeg = std::sin(animPhase) * 4.5f;
                rlRotatef(-36.0f + walkBobDeg, 0.0f, 0.0f, 1.0f);
                rlRotatef(14.0f, 0.0f, 1.0f, 0.0f);
            }
            drawModelStyled(cylModel_, {-0.01f, -0.02f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                            {0.066f, 0.43f, 0.066f}, topC, topRough, topMetal, 0.0f, clothMat);
            if (!shadowPass) {
                drawModelStyled(cylModel_, {0.39f, -0.02f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {0.070f, 0.032f, 0.070f}, accC, 0.52f, 0.08f, 0.0f, clothMat);
            }
            drawModelStyled(sphereModel_, {0.45f, -0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.056f, 0.052f, 0.050f}, skinC, 0.60f, 0.0f, 0.0f, MatKind::SkinOrganics);

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
                const bool isHandgun = (!longGun && weapon != WeaponId::SMG);
                const float gunLen = isSniper ? 0.96f
                                   : (heavyMg ? 0.78f
                                   : (longGun ? 0.66f
                                   : (isRevolver ? 0.36f : (weapon == WeaponId::SMG ? 0.44f : 0.27f))));
                const float gunCenterX = isHandgun ? 0.51f : (0.55f + (isSniper ? 0.12f : 0.0f));
                const Color bodyTint = (wSkin > 0) ? wPrimCol : (isRevolver ? Color{210, 218, 230, 255} : Color{34, 38, 46, 255});
                const float bodyRough = (wSkin > 0) ? wFin.roughness : (isRevolver ? 0.14f : 0.28f);
                const float bodyMetal = (wSkin > 0) ? wFin.metallic  : (isRevolver ? 0.94f : 0.82f);
                const float bodyH = heavyMg ? 0.115f : (isHandgun ? 0.056f : (isSniper ? 0.080f : 0.088f));
                const float bodyW = isHandgun ? 0.044f : 0.068f;
                drawModelStyled(cubeModel_, {gunCenterX, isHandgun ? 0.022f : 0.025f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                                {gunLen * (isHandgun ? 0.82f : 0.72f), bodyH, bodyW}, bodyTint,
                                bodyRough, bodyMetal, wFin.emissive, wMat);
                // Angled grip cleanly seated in the character's hand for handguns
                if (!shadowPass && isHandgun) {
                    const Color gripCol = isRevolver ? Color{88, 52, 32, 255} : Color{24, 26, 30, 255};
                    drawModelStyled(cubeModel_, {gunCenterX - gunLen * 0.22f, -0.036f, 0.0f}, {0.0f, 0.0f, 1.0f}, -16.0f,
                                    {0.056f, 0.098f, 0.038f}, gripCol, 0.62f, 0.08f, 0.0f, MatKind::Standard);
                }
                // Machined round steel barrel extending cleanly forward + compact buttstock & top optic on long guns
                drawModelStyled(cylModel_, {gunCenterX + gunLen * 0.20f, isHandgun ? 0.028f : 0.036f, 0.0f}, {0.0f, 0.0f, 1.0f}, -90.0f,
                                {isHandgun ? 0.015f : 0.020f, gunLen * 0.34f, isHandgun ? 0.015f : 0.020f}, Color{46, 52, 62, 255},
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
        } else if (npcAct == NpcActivity::HandsUp && alive) {
            // Gunpoint surrender pose: both hands raised high and steady!
            rlRotatef(148.0f, 0.0f, 0.0f, 1.0f);
            rlRotatef(static_cast<float>(-side) * 20.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, 18.0f, 148.0f, 0);
        } else if (panicking && alive) {
            // Both arms thrown high above the head waving frantically while fleeing!
            const float waveDeg = 152.0f + std::sin(animPhase * 1.45f + static_cast<float>(side) * 1.15f) * 20.0f;
            rlRotatef(waveDeg, 0.0f, 0.0f, 1.0f);
            rlRotatef(static_cast<float>(-side) * 14.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, 22.0f, waveDeg, 0);
        } else if (jumping && alive) {
            // Athletic mid-air arm lift for jump balance
            const float jDeg = (side < 0) ? 46.0f : 32.0f;
            rlRotatef(jDeg, 0.0f, 0.0f, 1.0f);
            rlRotatef(static_cast<float>(-side) * 18.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, 32.0f, jDeg, 0);
        } else if (holdUmbrella && side == -1) {
            // Left arm steady forward holding the open 3D rain umbrella overhead!
            const float shDeg = 22.0f + std::sin(animPhase) * 2.5f;
            const float elDeg = 68.0f;
            rlRotatef(shDeg, 0.0f, 0.0f, 1.0f);
            drawHangingSculptedArm(side, elDeg, shDeg, 1);
        } else if (npcAct == NpcActivity::CheckingPhone && alive && side == 1) {
            // Right arm raised cleanly in front of chest reading a glowing smartphone!
            const float shDeg = 28.0f;
            const float elDeg = 76.0f;
            rlRotatef(shDeg, 0.0f, 0.0f, 1.0f);
            rlRotatef(18.0f, 1.0f, 0.0f, 0.0f);
            drawHangingSculptedArm(side, elDeg, shDeg, 2);
        } else {
            const bool rightCarryCoffee = (alive && side == 1 && heldProp == NpcHeldProp::CoffeeCup);
            if (rightCarryCoffee) {
                const float shDeg = 10.0f + std::sin(animPhase) * 3.5f;
                const float elDeg = 68.0f;
                rlRotatef(shDeg, 0.0f, 0.0f, 1.0f);
                drawHangingSculptedArm(side, elDeg, shDeg, 4);
            } else {
                const float armSwing = static_cast<float>(-side) * legSwing * 0.8f;
                const float swingDamp = (alive && side == 1 &&
                                         (heldProp == NpcHeldProp::Briefcase || heldProp == NpcHeldProp::ShoppingBag))
                                            ? 0.42f
                                            : 1.0f;
                const float shDeg = armSwing * swingDamp;
                const float elDeg = alive ? (10.0f + std::max(0.0f, shDeg * 0.55f)) : 0.0f;
                int propMode = 0;
                if (alive && side == 1) {
                    if (heldProp == NpcHeldProp::Briefcase) propMode = 3;
                    else if (heldProp == NpcHeldProp::ShoppingBag) propMode = 5;
                    else if (heldProp == NpcHeldProp::Smartphone) propMode = 6;
                }
                rlRotatef(shDeg, 0.0f, 0.0f, 1.0f);
                drawHangingSculptedArm(side, elDeg, shDeg, propMode);
            }
        }
        rlPopMatrix();
    }

    rlPopMatrix(); // end torso lean
    rlPopMatrix(); // end character root
}

void Renderer3D::drawPoisAndPickups(const GameWorld& world) {
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 3.2f);
    const bool night = world.isNightTime();

    // Sleek Low-Profile Open-Center Architectural Ground Rings (pavement visible cleanly inside!)
    for (const auto& poi : world.city().pois()) {
        if (world.mission().kind != ActiveMissionKind::None &&
            (poi.kind == PoiKind::MissionDelivery ||
             poi.kind == PoiKind::MissionRace ||
             poi.kind == PoiKind::MissionGangRaid)) {
            continue;
        }
        const float surfY = world.surfaceHeightAt(poi.pos);
        const float outerR = std::clamp(poi.radius * 0.56f, 1.35f, 2.35f);
        const Color c = toRayColor(poi.markerColor);
        const Color ringCol{c.r, c.g, c.b, static_cast<unsigned char>(165.0f + pulse * 55.0f)};
        // Grounded dark bronze/charcoal bezel ring + subtle open LED guide ring
        drawModelStyled(ringModel_, {poi.pos.x, surfY + 0.008f, poi.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {outerR + 0.14f, 0.026f, outerR + 0.14f}, Color{24, 28, 34, 210},
                        0.42f, 0.60f, 0.0f, MatKind::Standard);
        drawModelStyled(ringModel_, {poi.pos.x, surfY + 0.018f, poi.pos.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {outerR, 0.032f, outerR}, ringCol,
                        0.22f, 0.12f, night ? (0.95f + 0.30f * pulse) : (0.42f + 0.20f * pulse),
                        MatKind::EmissiveGlow);
    }

    if (world.mission().kind != ActiveMissionKind::None) {
        const Vec2 mp = world.mission().currentObjectivePos;
        const float surfY = world.surfaceHeightAt(mp);
        const float r = std::clamp(world.mission().objectiveRadius * 0.62f, 2.2f, 4.2f);
        // Clean open golden perimeter ring + soft translucent vertical beacon
        drawModelStyled(ringModel_, {mp.x, surfY + 0.018f, mp.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {r, 0.055f, r}, Color{246, 206, 66, 220},
                        0.18f, 0.12f, 1.25f, MatKind::EmissiveGlow);
        drawModelStyled(ringModel_, {mp.x, surfY + 0.06f, mp.z}, {0.0f, 1.0f, 0.0f}, 0.0f,
                        {r * 0.94f, 4.8f, r * 0.94f}, Color{255, 218, 72, 34},
                        0.15f, 0.0f, 1.05f, MatKind::EmissiveGlow);
    }

    for (const auto& pk : world.pickups()) {
        if (pk.collected) continue;
        const float surfY = world.surfaceHeightAt(pk.pos);
        const float y = surfY + 0.42f + 0.10f * std::sin(pk.bobPhase);
        rlPushMatrix();
        rlTranslatef(pk.pos.x, y, pk.pos.z);
        rlRotatef(pk.bobPhase * kRadToDeg * 0.75f, 0.0f, 1.0f, 0.0f);
        if (pk.kind == PickupKind::CashBundle) {
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.46f, 0.18f, 0.28f}, Color{78, 134, 86, 255},
                            0.68f, 0.05f, night ? 0.42f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.10f, 0.19f, 0.29f}, Color{216, 186, 112, 255},
                            0.55f, 0.15f, 0.0f, MatKind::Standard);
        } else if (pk.kind == PickupKind::HealthKit) {
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.46f, 0.34f, 0.46f}, Color{236, 238, 242, 255},
                            0.36f, 0.12f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.48f, 0.11f, 0.16f}, Color{212, 42, 42, 255},
                            0.28f, 0.08f, night ? 0.55f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.16f, 0.11f, 0.48f}, Color{212, 42, 42, 255},
                            0.28f, 0.08f, night ? 0.55f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
        } else {
            // Executive leather/brass briefcase pickup
            drawModelStyled(cubeModel_, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.64f, 0.44f, 0.20f}, Color{54, 38, 26, 255},
                            0.42f, 0.18f, 0.0f, MatKind::Standard);
            drawModelStyled(cubeModel_, {0.0f, 0.14f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f,
                            {0.66f, 0.05f, 0.21f}, Color{216, 178, 72, 255},
                            0.20f, 0.90f, night ? 0.45f : 0.0f,
                            night ? MatKind::EmissiveGlow : MatKind::Standard);
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
