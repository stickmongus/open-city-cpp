#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUv;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in vec4 fragPosLightSpace;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 viewProj;
    mat4 lightVP;
    vec4 lightDirAndShadows;   // xyz = lightDir, w = enableShadows
    vec4 sunColorAndTime;      // xyz = sunColor, w = animTime
    vec4 skyAmbientAndNight;   // xyz = skyAmbient, w = nightFactor
    vec4 groundAmbientAndWet;  // xyz = groundAmbient, w = wetRoadFactor
    vec4 fogColorAndTexel;     // xyz = fogColor, w = shadowTexelSize
    vec4 horizonColorAndDay;   // xyz = horizonColor, w = dayFactor
    vec4 viewPosAndHour;       // xyz = viewPos, w = timeHours
    vec4 localLightCount;      // x = active dynamic local lights (0..20)
    vec4 lightPosRadius[20];   // xyz = worldPos, w = radius
    vec4 lightColorType[20];   // rgb = linear HDR intensity, w = type (0 = point, 1 = spot)
    vec4 lightDirCone[20];     // xyz = unit forward dir, w = cosOuterHalfAngle
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D signAtlasTex;
layout(set = 0, binding = 3) uniform sampler2D pbrAlbedoTex;
layout(set = 0, binding = 4) uniform sampler2D pbrNormalRoughTex;

layout(push_constant) uniform DrawPush {
    mat4 model;
    vec4 tint;
    vec4 pbrParams; // x=roughness, y=metallic, z=emissive, w=matId/255
} pc;

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265;

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

float fbm2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * valueNoise(p);
        p = p * 2.03 + vec2(7.1, 13.7);
        a *= 0.5;
    }
    return v;
}

float aaBand(float distFromCenter, float halfWidth) {
    float fw = max(fwidth(distFromCenter), 0.004);
    return 1.0 - smoothstep(halfWidth - fw, halfWidth + fw, distFromCenter);
}

// Seam-free 4x4 PBR Texture Atlas Sampling using explicit tile-scaled gradients (ZERO mip bleeding!)
void samplePbrTile(int tileId, vec2 uv, out vec3 outAlbedoLin, out vec2 outNormXY, out float outRough, out float outAO) {
    vec2 tileOrigin = vec2(float(tileId & 3), float(tileId >> 2)) * 0.25;
    vec2 ddx = clamp(dFdx(uv) * 0.25, vec2(-0.08), vec2(0.08));
    vec2 ddy = clamp(dFdy(uv) * 0.25, vec2(-0.08), vec2(0.08));
    vec2 local = (clamp(fract(uv), 0.004, 0.996)) * 0.25;
    vec2 atlasUv = tileOrigin + local;
    vec3 texAlb = textureGrad(pbrAlbedoTex, atlasUv, ddx, ddy).rgb;
    vec4 texNR  = textureGrad(pbrNormalRoughTex, atlasUv, ddx, ddy);
    outAlbedoLin = pow(clamp(texAlb, 0.0, 1.0), vec3(2.2));
    outNormXY = texNR.xy * 2.0 - 1.0;
    outRough = clamp(texNR.b, 0.03, 0.98);
    outAO = clamp(texNR.a, 0.25, 1.0);
}

// Automatic Screen-Space Cotangent-Frame Tangent-Space Normal Perturbation
vec3 perturbNormal(vec3 N, vec3 pos, vec2 uv, vec2 normalXY, float strength) {
    vec3 dp1 = dFdx(pos);
    vec3 dp2 = dFdy(pos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float maxSq = max(dot(T, T), dot(B, B));
    if (maxSq < 1e-8) return N;
    float invmax = inversesqrt(maxSq);
    vec2 nxy = clamp(normalXY * strength, vec2(-0.85), vec2(0.85));
    float nz = sqrt(max(0.08, 1.0 - dot(nxy, nxy)));
    return normalize(T * (nxy.x * invmax) + B * (nxy.y * invmax) + N * nz);
}

// 16-Tap Rotated Poisson Disk Soft PCF Shadows in Vulkan [0, 1] NDC depth space
float computeShadow(vec4 lightSpacePos, vec3 N, vec3 L) {
    if (ubo.lightDirAndShadows.w < 0.5) return 0.0;
    vec3 proj = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    vec2 uv = proj.xy * 0.5 + 0.5;
    float depth = proj.z;
    if (uv.x <= 0.002 || uv.x >= 0.998 || uv.y <= 0.002 || uv.y >= 0.998 || depth <= 0.0001 || depth >= 0.9999) {
        return 0.0;
    }
    vec2 edgeDist = min(uv, 1.0 - uv);
    float edgeFade = smoothstep(0.008, 0.085, min(edgeDist.x, edgeDist.y));
    float ndotl = max(dot(N, L), 0.0);
    float bias = max(0.00035, 0.0018 * (1.0 - ndotl));

    vec2 poisson[16] = vec2[16](
        vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
    );
    float ang = hash12(gl_FragCoord.xy) * 6.283185;
    float cs = cos(ang), sn = sin(ang);
    mat2 rot = mat2(cs, -sn, sn, cs);
    float spread = ubo.fogColorAndTexel.w * 2.1;

    float occluded = 0.0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(shadowMap, uv + (rot * poisson[i]) * spread).r;
        occluded += (depth - bias > d) ? 1.0 : 0.0;
    }
    return (occluded * (1.0 / 16.0)) * edgeFade;
}

// High-Contrast Automotive & Urban Environment Reflection Map
vec3 sampleSeoulEnvMap(vec3 R, vec3 worldPos, float rough, float nightF, bool isAutomotive) {
    vec3 skyAmb = ubo.skyAmbientAndNight.xyz;
    vec3 gndAmb = ubo.groundAmbientAndWet.xyz;
    vec3 horCol = ubo.horizonColorAndDay.xyz;

    float horizSpan = isAutomotive ? (0.015 + rough * 0.12) : (0.045 + rough * 0.22);
    float horizBlend = smoothstep(-horizSpan, horizSpan, R.y);

    vec3 darkGroundRefl = isAutomotive
        ? mix(vec3(0.014, 0.016, 0.022), gndAmb * 0.28, 0.40)
        : (gndAmb * 0.52);
    vec3 upperSkyRefl = mix(horCol * (isAutomotive ? 1.18 : 0.90),
                            skyAmb * (isAutomotive ? 1.56 : 1.38),
                            pow(clamp(R.y, 0.0, 1.0), 0.48));
    vec3 envCol = mix(darkGroundRefl, upperSkyRefl, horizBlend);

    // Crisp bright automotive horizon band slicing along stamped body character lines!
    if (isAutomotive) {
        float creaseBand = smoothstep(-0.022, 0.016, R.y) * (1.0 - smoothstep(0.022, 0.105, R.y));
        vec3 bandCol = mix(horCol * 1.40, vec3(1.22, 1.26, 1.32), 0.48) * (1.0 - nightF * 0.58);
        envCol += bandCol * creaseBand * (0.54 * (1.0 - rough * 0.72));
    }

    // Softened Seoul skyline reflection near the horizon
    float az = atan(R.z, R.x);
    float bldColIdx = floor((az + kPi) / (2.0 * kPi) * 48.0);
    float bldRnd = hash12(vec2(bldColIdx, 3.7));
    float skylineH = (0.03 + 0.19 * pow(bldRnd, 1.4)) * (1.0 - rough * 0.52);

    if (R.y > -0.02 && R.y < skylineH) {
        vec3 bldSil = mix(gndAmb * 0.30, skyAmb * 0.22, 0.38);
        bldSil += vec3(0.24, 0.17, 0.09) * nightF * (1.0 - clamp(R.y / max(skylineH, 0.01), 0.0, 1.0));
        float uFreq = (az + kPi) * 28.0;
        float vFreq = R.y * 22.0;
        float detailFade = 1.0 - smoothstep(0.16, 0.66, max(fwidth(uFreq), fwidth(vFreq)) + rough * 1.55);
        if (detailFade > 0.02) {
            float winU = fract(uFreq);
            float winV = fract(vFreq);
            float softEdge = clamp(0.24 + rough * 0.62, 0.24, 0.42);
            float paneMask = smoothstep(0.14, 0.14 + softEdge, winU) * (1.0 - smoothstep(0.86 - softEdge, 0.86, winU)) *
                             smoothstep(0.12, 0.12 + softEdge, winV) * (1.0 - smoothstep(0.88 - softEdge, 0.88, winV));
            float winHash = hash12(vec2(bldColIdx * 3.1 + floor(winU * 4.0), floor(vFreq)));
            if (winHash > (0.64 - nightF * 0.28)) {
                vec3 winPalette = (winHash > 0.88)
                                      ? vec3(0.48, 0.76, 1.02)
                                      : ((winHash > 0.76) ? vec3(0.96, 0.72, 0.44) : vec3(1.06, 0.82, 0.52));
                bldSil += winPalette * (0.016 + nightF * 0.65) * paneMask * detailFade;
            }
        }
        float mask = (1.0 - smoothstep(skylineH - 0.030, skylineH, R.y)) * smoothstep(-0.02, 0.025, R.y);
        envCol = mix(envCol, bldSil, mask * (isAutomotive ? 0.54 : 0.66));
    }
    return envCol;
}

void main() {
    vec4 srgbBase = fragColor;
    vec3 albedo = pow(clamp(srgbBase.rgb, 0.0, 1.0), vec3(2.2));
    float alphaOut = srgbBase.a;

    float roughness = clamp(pc.pbrParams.x, 0.02, 0.98);
    float metallic  = clamp(pc.pbrParams.y, 0.0, 1.0);
    float emissive  = pc.pbrParams.z * 3.5;
    int matId       = int(pc.pbrParams.w * 255.0 + 0.5);

    vec3 N = normalize(fragNormal);
    vec3 viewPos = ubo.viewPosAndHour.xyz;
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(-ubo.lightDirAndShadows.xyz);
    vec3 sunColor = ubo.sunColorAndTime.xyz;
    vec3 skyAmbient = ubo.skyAmbientAndNight.xyz;
    vec3 groundAmbient = ubo.groundAmbientAndWet.xyz;
    float animTime = ubo.sunColorAndTime.w;
    float nightFactor = ubo.skyAmbientAndNight.w;
    float wetRoadFactor = ubo.groundAmbientAndWet.w;

    vec3 emissiveColor = albedo;
    float puddleMask = 0.0;
    float wetRoadSurface = 0.0;
    float surfaceAO = 1.0;
    bool isWindowPane = false;
    bool isGlassTowerWin = false;

    float viewDist = length(viewPos - fragPosition);
    float detailFade = 1.0 - smoothstep(32.0, 160.0, viewDist);

    // -----------------------------------------------------------------------
    // 2048x2048 Mipmapped, 8x Anisotropic, Tangent-Space Normal-Mapped PBR!
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Seoul Asphalt (Tile 0) + Real Curved OSM Lane Markings OR Legacy Grid Markings + Wet Puddles
        vec2 p = fragPosition.xz;
        vec2 asphUv = p * 0.16;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(0, asphUv, pAlb, pNxy, pR, pAO);

        float patchVar = fbm2(p * 0.18);
        albedo = pAlb * mix(0.84, 1.18, patchVar);
        roughness = mix(pR, 0.58, 0.25);
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, asphUv, pNxy, 0.65 * detailFade);

        vec3 yellowPaint = pow(vec3(0.88, 0.68, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.82, 0.84, 0.86), vec3(2.2));
        vec3 busBlue     = pow(vec3(0.18, 0.48, 0.88), vec3(2.2));
        float paintWear  = mix(0.82, 1.0, smoothstep(0.15, 0.72, valueNoise(p * 3.5)));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;
        float blueMask   = 0.0;

        if (fragUv.y > 0.5) {
            // REAL OPENSTREETMAP CURVED SEOUL ROAD RIBBON — TRUE KOREAN ROAD HIERARCHY & METRIC MARKINGS!
            // fragUv.x = signed lateral distance from road centerline in METERS [-hwM .. +hwM]
            // fragUv.y = longitudinal distance along road in METERS (> 10.0)
            // srgbBase.r = hwM / 25.5, srgbBase.g = totalLanes * 16 / 255, srgbBase.b = styleByte / 255
            // styleByte: 0=alley, 64=city 2-way, 128=city 1-way, 176=Korean Expressway, 204=Bridge, 240=City Bus-Lane Blvd
            float xM = fragUv.x;
            float sM = fragUv.y;
            float hwM = max(2.4, srgbBase.r * 25.5);
            float totalLanes = floor(srgbBase.g * (255.0 / 16.0) + 0.5);
            float styleByte = floor(srgbBase.b * 255.0 + 0.5);
            float axM = abs(xM);

            bool isAlley   = (totalLanes < 0.5) || (styleByte < 24.0);
            bool isOneway  = (styleByte >= 96.0);
            bool isHighway = (styleByte >= 152.0 && styleByte < 188.0);
            bool isBridge  = (styleByte >= 188.0 && styleByte < 216.0);
            bool hasBusLn  = (styleByte >= 220.0);

            // Subtle asphalt gutter darkening near curb edge (0.65m strip)
            albedo *= (1.0 - 0.18 * smoothstep(hwM - 0.70, hwM, axM));

            if (isHighway) {
                // KOREAN EXPRESSWAY / URBAN MOTORWAY (경부고속도로 · 올림픽대로 · 강변북로 · 내부순환로):
                // Smoother dark SMA highway asphalt, wide paved emergency shoulders (갓길),
                // crisp SOLID WHITE right shoulder line + SOLID YELLOW left median line,
                // authentic Korean highway 8m dash / 12m gap (20m cycle), reflective cat's-eye studs, ZERO crosswalks!
                albedo *= 0.88;
                roughness = mix(roughness, 0.44, 0.25);

                float shR = (hwM >= 7.0) ? 1.15 : 0.55; // Outer emergency shoulder (right)
                float shL = (hwM >= 7.0) ? 0.58 : 0.32; // Inner median shoulder (left)
                float driveLeft  = -hwM + shL;
                float driveRight =  hwM - shR;
                float driveSpan  = max(2.6, driveRight - driveLeft);
                float hwLaneW    = driveSpan / max(1.0, totalLanes);

                // Subtle rumble-strip darkening inside outer emergency shoulder (갓길)
                if (xM > driveRight + 0.12) {
                    float rumble = 0.5 + 0.5 * sin(sM * 14.0);
                    albedo *= mix(0.88, 0.96, rumble);
                }

                // Dual tire-wear polishing per highway lane
                if (xM >= driveLeft && xM <= driveRight) {
                    float laneU = fract((xM - driveLeft) / max(2.4, hwLaneW));
                    float trackL = exp(-pow((laneU - 0.27) / 0.11, 2.0));
                    float trackR = exp(-pow((laneU - 0.73) / 0.11, 2.0));
                    float tireWear = clamp(trackL + trackR, 0.0, 1.0);
                    albedo *= (1.0 - 0.11 * tireWear);
                    roughness = mix(roughness, 0.33, tireWear * 0.45);
                }

                // Crisp 0.18m SOLID WHITE right outer shoulder line & SOLID YELLOW left median shoulder line
                whiteMask  = max(whiteMask,  aaBand(abs(xM - driveRight), 0.090) * 0.96);
                yellowMask = max(yellowMask, aaBand(abs(xM - driveLeft),  0.090) * 0.96);

                // Authentic Korean Highway 8.0m white dash / 12.0m gap (20.0m cycle — distinct from 8m city streets!)
                float hwDashOn = (fract(sM / 20.0) < 0.40) ? 1.0 : 0.0;
                float relX = xM - driveLeft;
                if (totalLanes > 1.5 && hwDashOn > 0.5 && relX > hwLaneW * 0.45 && relX < (driveSpan - hwLaneW * 0.45)) {
                    float laneIdxF = floor(relX / hwLaneW + 0.5);
                    float divDist = abs(relX - laneIdxF * hwLaneW);
                    whiteMask = max(whiteMask, aaBand(divDist, 0.082) * 0.94);
                }

                // Reflective Highway Cat's-Eye Road Studs (표지병) every 12m along BOTH shoulder lines!
                float studS = abs(fract(sM / 12.0) - 0.5) * 12.0;
                if (studS < 0.16) {
                    float studR = aaBand(abs(xM - (driveRight + 0.14)), 0.085);
                    float studL = aaBand(abs(xM - (driveLeft  - 0.14)), 0.085);
                    float studMask = max(studR, studL) * smoothstep(0.16, 0.06, studS);
                    if (studMask > 0.05) {
                        vec3 studCol = (studL > studR) ? vec3(1.0, 0.68, 0.12) : vec3(0.92, 0.97, 1.0);
                        emissiveColor = mix(emissiveColor, studCol, studMask);
                        emissive = max(emissive, studMask * (0.28 + nightFactor * 1.45));
                    }
                }
            } else if (!isAlley) {
                float usableHalf = max(1.8, hwM - 0.34);
                float laneW = (2.0 * usableHalf) / max(1.0, totalLanes);

                // Realistic per-lane dual wheel-track asphalt polishing (every ~3.25m lane!)
                float laneU = fract((xM + usableHalf) / max(2.2, laneW));
                float trackL = exp(-pow((laneU - 0.27) / 0.11, 2.0));
                float trackR = exp(-pow((laneU - 0.73) / 0.11, 2.0));
                float tireWear = clamp(trackL + trackR, 0.0, 1.0);
                albedo *= (1.0 - 0.12 * tireWear);
                roughness = mix(roughness, 0.35, tireWear * 0.42);

                // Crisp 0.15m curbside no-parking yellow line (or white shoulder line on Han River bridges)
                float edgeBand = aaBand(abs(axM - (hwM - 0.28)), 0.075);
                if (isBridge) {
                    whiteMask = max(whiteMask, edgeBand * 0.88);
                } else {
                    yellowMask = max(yellowMask, edgeBand * 0.82);
                }

                // Korean city standard 3.0m white dash / 5.0m gap (8.0m cycle)
                float dashOn = (fract(sM / 8.0) < 0.375) ? 1.0 : 0.0;

                // Periodic Korean crosswalk (4.2m zebra, 0.45m bars @ 0.90m pitch) + 0.40m stop bar (non-bridges only!)
                float cwMod = fract(sM / 96.0) * 96.0;
                bool inCrosswalkZone = (!isBridge && totalLanes >= 2.0 && cwMod > 1.5 && cwMod < 9.2);
                if (!isBridge && totalLanes >= 2.0) {
                    if (cwMod > 3.6 && cwMod < 7.8 && axM < (hwM - 0.55)) {
                        float barDist = abs(fract(xM / 0.90) - 0.5) * 0.90;
                        whiteMask = max(whiteMask, aaBand(barDist, 0.215) * 0.90);
                    }
                    // Crisp 0.40m Korean stop line 1.6m before crosswalk
                    if (cwMod > 1.8 && cwMod < 2.22 && axM < (hwM - 0.45)) {
                        if (isOneway || xM > 0.22) {
                            whiteMask = max(whiteMask, 0.88);
                        }
                    }
                }

                if (!inCrosswalkZone) {
                    if (!isOneway) {
                        // Two-way city street: crisp Korean double yellow centerline (2 x 0.12m @ +/-0.14m)
                        yellowMask = max(yellowMask, aaBand(abs(axM - 0.14), 0.062) * 0.95);
                        float lanesPerDir = max(1.0, floor(totalLanes * 0.5 + 0.5));
                        float dirLaneW = (usableHalf - 0.22) / lanesPerDir;
                        if (lanesPerDir > 1.5 && dashOn > 0.5) {
                            // Exact metric lane dividers inside each travel direction
                            float localX = axM - 0.22;
                            if (localX > dirLaneW * 0.5 && localX < (usableHalf - 0.22) - dirLaneW * 0.4) {
                                float divDist = abs(fract(localX / dirLaneW + 0.5) - 0.5) * dirLaneW;
                                whiteMask = max(whiteMask, aaBand(divDist, 0.072));
                            }
                        }
                    } else {
                        // One-way multi-lane city boulevard / bridge carriageway (exact 3.1-3.4m lanes!)
                        float relX = xM + usableHalf; // [0 .. 2*usableHalf] from left edge to right edge
                        if (totalLanes > 1.5 && relX > laneW * 0.45 && relX < (2.0 * usableHalf - laneW * 0.45)) {
                            float laneIdxF = floor(relX / laneW + 0.5); // nearest interior divider index [1 .. totalLanes-1]
                            float divDist = abs(relX - laneIdxF * laneW);
                            bool isBusDiv = hasBusLn && (abs(laneIdxF - (totalLanes - 1.0)) < 0.25);
                            if (isBusDiv) {
                                // Iconic Seoul Blue Bus-Only Lane solid/dashed divider (파란색 버스전용차로!)
                                blueMask = max(blueMask, aaBand(divDist, 0.080) * 0.95);
                            } else if (dashOn > 0.5) {
                                whiteMask = max(whiteMask, aaBand(divDist, 0.072));
                            }
                        }
                    }
                }
            } else {
                // Narrow residential Korean alley: subtle weathered edge curbing, zero bogus multi-lane dashes
                whiteMask = max(whiteMask, aaBand(abs(axM - (hwM - 0.22)), 0.055) * 0.45);
            }
        } else {
            // Legacy analytic 84m grid road fallback
            float rIdxX = round(p.x / 84.0);
            float rIdxZ = round(p.y / 84.0);
            float rx = rIdxX * 84.0;
            float rz = rIdxZ * 84.0;
            float dx = abs(p.x - rx);
            float dz = abs(p.y - rz);
            bool onVert  = (dx <= 11.2);
            bool onHoriz = (dz <= 9.05) && (fragPosition.y < 3.5);
            bool isBlueBusVert = (abs(rIdxX - 7.0) < 0.2 || abs(rIdxX - 29.0) < 0.2 || abs(rIdxX + 11.0) < 0.2);
            bool isBlueBusHoriz = (abs(rIdxZ - 35.0) < 0.2 || abs(rIdxZ + 55.0) < 0.2);

            if (onVert && !onHoriz) {
                if (dz >= 9.35 && dz <= 12.15 && dx < 8.0 && fragPosition.y < 3.5) {
                    float stripe = abs(fract((p.x - rx + 8.0) / 1.85) - 0.5) * 1.85;
                    whiteMask = max(whiteMask, aaBand(stripe, 0.42));
                }
                if (dz > 13.15 || fragPosition.y >= 3.5) {
                    yellowMask = max(yellowMask, aaBand(abs(dx - 0.18), 0.075));
                    if (fract(p.y / 9.0) < 0.38) whiteMask = max(whiteMask, aaBand(abs(dx - 4.35), 0.075));
                    if (isBlueBusVert) blueMask = max(blueMask, aaBand(abs(dx - 6.65), 0.085));
                    yellowMask = max(yellowMask, aaBand(abs(dx - 8.62), 0.075) * 0.72);
                }
            } else if (onHoriz && !onVert) {
                if (dx >= 9.35 && dx <= 12.15 && dz < 8.0) {
                    float stripe = abs(fract((p.y - rz + 8.0) / 1.85) - 0.5) * 1.85;
                    whiteMask = max(whiteMask, aaBand(stripe, 0.42));
                }
                if (dx > 13.15) {
                    yellowMask = max(yellowMask, aaBand(abs(dz - 0.18), 0.075));
                    if (fract(p.x / 9.0) < 0.38) whiteMask = max(whiteMask, aaBand(abs(dz - 4.35), 0.075));
                    if (isBlueBusHoriz) blueMask = max(blueMask, aaBand(abs(dz - 6.65), 0.085));
                    yellowMask = max(yellowMask, aaBand(abs(dz - 8.62), 0.075) * 0.72);
                }
            }
        }

        float wPaintA = clamp(whiteMask, 0.0, 1.0) * 0.86 * paintWear;
        float yPaintA = clamp(yellowMask, 0.0, 1.0) * 0.90 * paintWear;
        float bPaintA = clamp(blueMask, 0.0, 1.0) * 0.88 * paintWear;
        albedo = mix(albedo, whitePaint, wPaintA);
        albedo = mix(albedo, yellowPaint, yPaintA);
        albedo = mix(albedo, busBlue, bPaintA);
        roughness = mix(roughness, 0.52, max(max(wPaintA, yPaintA), bPaintA) * 0.55);

        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.16, 0.0, 1.0);
        wetRoadSurface = wetAmt;
        if (wetAmt > 0.02) {
            vec2 warp = vec2(fbm2(p * 0.085 + vec2(3.1, 7.4)), fbm2(p * 0.085 + vec2(11.7, 2.3)));
            float pNoise = fbm2(p * 0.145 + warp * 1.45) * 0.72 + valueNoise(p * 0.52) * 0.28;
            puddleMask = smoothstep(0.55 - wetAmt * 0.17, 0.77 - wetAmt * 0.10, pNoise) * wetAmt;
            albedo *= (1.0 - wetAmt * 0.24 - puddleMask * 0.22);
            roughness = mix(roughness, 0.22, wetAmt * 0.48);
            roughness = mix(roughness, 0.04, puddleMask);
            if (puddleMask > 0.12 && wetRoadFactor > 0.05) {
                vec2 rCell = floor(p * 1.65);
                vec2 rLocal = fract(p * 1.65) - 0.5;
                float rSeed = hash12(rCell);
                float rAge = fract(animTime * 1.45 + rSeed * 7.3);
                float ringD = abs(length(rLocal) - rAge * 0.45);
                float ripple = exp(-ringD * 34.0) * (1.0 - rAge);
                vec2 rDir = normalize(rLocal + vec2(0.0001));
                float ripBlend = smoothstep(0.14, 0.58, puddleMask) * smoothstep(0.05, 0.35, wetRoadFactor) * detailFade;
                N = normalize(mix(N, vec3(rDir.x * ripple * 0.16, 1.0, rDir.y * ripple * 0.16), ripBlend));
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Authentic Seoul Sidewalk Interlocking Pavers (Tile 1) + Normal Map + Tactile Paving (Tile 15)
        vec2 p = fragPosition.xz;
        bool isSidewalkStrip = (fragUv.y > 0.5);
        vec2 pvUv = p * (isSidewalkStrip ? 0.28 : 0.46);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(1, pvUv, pAlb, pNxy, pR, pAO);
        if (isSidewalkStrip) {
            albedo = mix(pAlb, pAlb * (albedo * 1.35 + vec3(0.15)), 0.35);
        } else {
            // Subtle smooth Seoul granite plaza / courtyard paving without harsh macro tile seams
            float plazaVar = fbm2(p * 0.08);
            vec3 granBase = mix(vec3(0.24, 0.25, 0.27), pAlb * 0.88, 0.52);
            albedo = granBase * mix(0.88, 1.10, plazaVar);
        }
        roughness = mix(pR, 0.55, wetRoadFactor * 0.65);
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, pvUv, pNxy, (isSidewalkStrip ? 0.75 : 0.48) * detailFade);

        // If Real-OSM sidewalk strip (fragUv.y > 0.5, fragUv.x in [0..1] from curb to building edge)
        if (isSidewalkStrip) {
            if (fragUv.x < 0.14) {
                // Korean granite curbstone edge
                float curbJoint = smoothstep(0.92, 0.98, abs(sin(fragUv.y * 3.14)));
                albedo = mix(vec3(0.46, 0.47, 0.49), vec3(0.28, 0.29, 0.31), curbJoint);
                roughness = 0.60;
            } else if (fragUv.x > 0.34 && fragUv.x < 0.50) {
                // Iconic Korean yellow tactile paving line (노란 점자블록)
                vec3 tAlb; vec2 tNxy; float tR; float tAO;
                samplePbrTile(15, vec2(fragUv.x * 6.0, fragUv.y * 1.4), tAlb, tNxy, tR, tAO);
                albedo = tAlb;
                roughness = tR;
                N = perturbNormal(N, fragPosition, vec2(fragUv.x * 6.0, fragUv.y * 1.4), tNxy, 0.8 * detailFade);
            }
        }
        puddleMask = wetRoadFactor * smoothstep(0.45, 0.78, fbm2(p * 0.22)) * 0.40;
        albedo *= (1.0 - wetRoadFactor * 0.18 - puddleMask * 0.16);
        roughness = mix(roughness, 0.09, puddleMask);
    }
    else if (matId == 2 || matId == 3 || matId == 4 || matId == 16 || matId == 18) {
        // Real 2048x2048 PBR Architectural Building Facades + Normal Maps + Reflective Windows + Night Interior Glow!
        if (abs(N.y) < 0.45) {
            // Real-OSM polygons pass exact wall-metric UV (u = horizontal meters, v = vertical height meters)!
            vec2 wallM = (dot(fragUv, fragUv) > 0.0001)
                ? fragUv
                : vec2((abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x, fragPosition.y);

            bool isGlassTower = (matId == 3);
            bool isKoreaApt   = (matId == 16);
            bool isGroundFlr  = (wallM.y < 3.9);

            int tileId = 4;
            vec2 tileScale = vec2(0.11, 0.11);
            if (isGroundFlr && (matId == 2 || matId == 4 || matId == 18)) {
                tileId = 7; // Korean 1F Commercial Storefront (카페/편의점/식당 유리창!)
                tileScale = vec2(0.125, 0.256);
            } else if (matId == 16) {
                tileId = 2; // Korean Apartment Facade
                tileScale = vec2(0.095, 0.105);
            } else if (matId == 3) {
                tileId = 3; // Glass Curtain-Wall Skyscraper
                tileScale = vec2(0.105, 0.095);
            } else if (matId == 4) {
                tileId = 4; // Granite Korean Office
                tileScale = vec2(0.115, 0.105);
            } else if (matId == 2) {
                tileId = 5; // Brick Commercial / Villa
                tileScale = vec2(0.135, 0.120);
            } else {
                tileId = 6; // Stucco Commercial
                tileScale = vec2(0.135, 0.120);
            }

            vec2 pbrUv = wallM * tileScale;
            vec3 pAlb; vec2 pNxy; float pR; float pAO;
            samplePbrTile(tileId, pbrUv, pAlb, pNxy, pR, pAO);

            // Detect reflective architectural glass windows cleanly via baked PBR roughness < 0.14!
            bool pbrWin = (pR < 0.145);
            if (pbrWin) {
                isWindowPane = true;
                isGlassTowerWin = isGlassTower;
                // Subtle glass pane reflection tint + night interior room luminance variation!
                vec2 winCell = floor(wallM * vec2(0.45, 0.30));
                float roomSeed = hash12(winCell + floor(fragPosition.xz * 0.04));
                albedo = isGlassTower ? mix(pAlb, pAlb * (albedo * 1.4 + vec3(0.15)), 0.45) : (pAlb * 0.82);
                roughness = isGlassTower ? 0.035 : 0.055;
                metallic = isGlassTower ? 0.38 : 0.22;

                float litProb = isGroundFlr ? 0.86 : (isGlassTower ? 0.54 : 0.44);
                if (roomSeed > (1.0 - litProb)) {
                    vec3 warmInt = pow(vec3(0.98, 0.80, 0.54), vec3(2.2));
                    vec3 coolInt = pow(vec3(0.68, 0.84, 0.98), vec3(2.2));
                    vec3 intCol = (roomSeed > 0.84) ? coolInt : warmInt;
                    emissiveColor = intCol;
                    emissive = max(emissive, mix(isGroundFlr ? 0.24 : 0.06, isGroundFlr ? 1.55 : 1.18, nightFactor));
                }
            } else {
                // Modulate baked PBR architectural masonry/stone/concrete cleanly with per-building color variety
                vec3 bldTint = mix(vec3(1.0), clamp(albedo * 1.65 + vec3(0.18), 0.45, 1.35), 0.55);
                albedo = pAlb * bldTint;
                roughness = pR;
                metallic = isGlassTower ? 0.42 : 0.05;
                surfaceAO = pAO;
                N = perturbNormal(N, fragPosition, pbrUv, pNxy, 0.90 * detailFade);
            }
            // Ground-level urban street contact ambient occlusion
            albedo *= mix(0.70, 1.0, smoothstep(0.0, 2.4, wallM.y));
        } else {
            // Flat architectural rooftop (Tile 10 Concrete OR Signature Korean green waterproof urethane roof!)
            vec2 roofUv = fragPosition.xz * 0.09;
            vec3 pAlb; vec2 pNxy; float pR; float pAO;
            samplePbrTile(10, roofUv, pAlb, pNxy, pR, pAO);
            if (matId == 2) {
                vec3 krRoofGreen = pow(vec3(0.24, 0.46, 0.34), vec3(2.2));
                albedo = mix(pAlb, krRoofGreen * (0.75 + pAlb * 0.7), 0.72);
                roughness = 0.54;
            } else {
                albedo = pAlb * mix(0.75, 1.10, 0.5);
                roughness = pR;
            }
            surfaceAO = pAO;
            metallic = 0.04;
            N = perturbNormal(N, fragPosition, roofUv, pNxy, 0.65 * detailFade);
        }
    }
    else if (matId == 6) {
        // MAT 6: Animated Shimmering Han River & Seokchon Lake Water
        vec2 wp = fragPosition.xz;
        float w1 = sin(wp.x * 0.14 + animTime * 1.45 + cos(wp.y * 0.11) * 1.1);
        float w2 = cos(wp.y * 0.20 - animTime * 1.15 + sin(wp.x * 0.14));
        float w3 = sin((wp.x + wp.y) * 0.42 + animTime * 2.05) * 0.45;
        vec2 grad = vec2(w1 * 0.042 + w3 * 0.028, w2 * 0.042 + w3 * 0.028);
        N = normalize(vec3(-grad.x, 1.0, -grad.y));
        albedo = mix(vec3(0.010, 0.048, 0.098), vec3(0.004, 0.014, 0.032), nightFactor);
        roughness = 0.055;
        metallic = 0.06;
    }
    else if (matId == 10) {
        // MAT 10: Real Textured Park Grass (Tile 8) & Mountain Forest (Tile 9) + Normal Map (ZERO flat green rectangles!)
        vec2 p = fragPosition.xz;
        vec2 grassUv = p * 0.16;
        vec3 gAlb; vec2 gNxy; float gR; float gAO;
        vec3 fAlb; vec2 fNxy; float fR; float fAO;
        samplePbrTile(8, grassUv, gAlb, gNxy, gR, gAO);
        samplePbrTile(9, p * 0.055, fAlb, fNxy, fR, fAO);
        float elevBlend = smoothstep(26.0, 75.0, fragPosition.y);
        float macroVar = fbm2(p * 0.018);
        vec3 baseVeg = mix(gAlb, fAlb, elevBlend);
        albedo = baseVeg * mix(0.76, 1.24, macroVar);
        // Subtle tint toward vertex color (e.g., mountain rock vs lush park lawn)
        albedo = mix(albedo, albedo * clamp(pow(srgbBase.rgb, vec3(2.2)) * 2.6 + vec3(0.25), 0.5, 1.4), 0.35);
        roughness = mix(gR, fR, elevBlend);
        surfaceAO = mix(gAO, fAO, elevBlend);
        N = perturbNormal(N, fragPosition, grassUv, mix(gNxy, fNxy, elevBlend), 0.72 * detailFade);
        metallic = 0.0;
    }
    else if (matId == 11) {
        vec2 wp = fragPosition.xz + vec2(fragPosition.y * 1.4);
        float weave = valueNoise(wp * 42.0) * 0.09;
        albedo *= (0.95 + weave);
        roughness = max(roughness, 0.72);
    }
    else if (matId == 13) {
        // MAT 13: Real Fissured Tree Bark & Natural Wood (Tile 13 + Normal Map!)
        vec2 bUv = vec2((fragPosition.x + fragPosition.z) * 0.85, fragPosition.y * 0.65);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(13, bUv, pAlb, pNxy, pR, pAO);
        albedo = pAlb * mix(vec3(1.0), albedo * 2.2, 0.35);
        roughness = pR;
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, bUv, pNxy, 1.05 * detailFade);
    }
    else if (matId == 14) {
        // MAT 14: Seoul Granite Mountain Rock & Retaining Walls (Tile 12 + macro noise!)
        vec2 rUv = vec2(fragPosition.x + fragPosition.z, fragPosition.y) * 0.14;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(12, rUv, pAlb, pNxy, pR, pAO);
        float rock = fbm2(fragPosition.xz * 0.06) * 0.24;
        albedo = mix(albedo, pAlb * (0.82 + rock), 0.55);
        roughness = pR;
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, rUv, pNxy, 0.85 * detailFade);
    }
    else if (matId == 15) {
        // MAT 15: Traditional Korean Gyeongbokgung Giwa Roof Tiles (Tile 11 + Normal Map!)
        vec2 gUv = fragPosition.xz * 0.42;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(11, gUv, pAlb, pNxy, pR, pAO);
        albedo = pAlb;
        roughness = pR;
        surfaceAO = pAO;
        metallic = 0.08;
        N = perturbNormal(N, fragPosition, gUv, pNxy, 0.95 * detailFade);
    }
    else if (matId == 17) {
        // MAT 17: Real Hangul 3D Seoul Sign Atlas (Gantries, Shopfronts, Subway, BIS, Apt Panels, Bus LED, Plates)
        vec4 tex = texture(signAtlasTex, fragUv);
        vec3 signLin = pow(clamp(tex.rgb, 0.0, 1.0), vec3(2.2));
        albedo = signLin * albedo;
        alphaOut *= tex.a;
        roughness = 0.34;
        metallic = 0.05;
        emissiveColor = albedo;
    }
    else if (matId == 19) {
        // MAT 19: Rooftop Concrete Slab (Tile 10 + Normal Map!)
        vec2 rUv = fragPosition.xz * 0.10;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(10, rUv, pAlb, pNxy, pR, pAO);
        albedo = pAlb;
        roughness = pR;
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, rUv, pNxy, 0.70 * detailFade);
    }
    else if (matId == 20) {
        // MAT 20: Cast-in-Place Bridge & Retaining Concrete (Tile 12 + Normal Map!)
        vec2 cUv = vec2(fragPosition.x + fragPosition.z, fragPosition.y) * 0.18;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(12, cUv, pAlb, pNxy, pR, pAO);
        albedo = pAlb * mix(vec3(1.0), albedo * 1.4, 0.35);
        roughness = pR;
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, cUv, pNxy, 0.80 * detailFade);
    }
    else if (matId == 21) {
        // MAT 21: Organic Dappled Tree Foliage Canopy (Tile 14 Leaves + Normal Map + Sunlit Translucency!)
        vec2 lUv = vec2(fragPosition.x * 0.55 + fragPosition.y * 0.35, fragPosition.z * 0.55 - fragPosition.y * 0.35);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        samplePbrTile(14, lUv, pAlb, pNxy, pR, pAO);
        // Tint leaf detail cleanly to Ginkgo green-gold, Cherry Blossom pink, or Korean Pine emerald!
        float leafLum = clamp(dot(pAlb, vec3(0.30, 0.59, 0.11)) * 2.8, 0.45, 1.45);
        albedo = albedo * leafLum;
        roughness = pR;
        surfaceAO = pAO;
        N = perturbNormal(N, fragPosition, lUv, pNxy, 0.90 * detailFade);
        metallic = 0.0;
    }

    // -----------------------------------------------------------------------
    // Physically Based Cook-Torrance GGX + Energy-Conserving Automotive Clearcoat
    // -----------------------------------------------------------------------
    float rawNdotL = dot(N, L);
    float ndotl = max(rawNdotL, 0.0);
    float shadow = (ndotl > 0.01) ? computeShadow(fragPosLightSpace, N, L) : 0.0;
    float sunVis = 1.0 - shadow * 0.90;

    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(groundAmbient * 0.78, skyAmbient * 0.90, hemi) * surfaceAO;
    ambient = mix(ambient, ambient * vec3(0.68, 0.80, 1.08), shadow * 0.50);
    float contactAo = mix(0.62, 1.0, clamp(fragPosition.y / 1.8, 0.0, 1.0));
    if (N.y > 0.75) contactAo = 1.0;

    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
    float ndotv = max(dot(N, V), 0.001);
    float vdoth = max(dot(V, H), 0.0);

    float clearcoatMask = 0.0;
    float clearcoatF = 0.0;
    if (matId == 7) {
        clearcoatMask = 1.0 - smoothstep(0.24, 0.54, roughness) * 0.82;
        float flop = pow(1.0 - ndotv, 1.95);
        albedo *= mix(vec3(1.00), vec3(0.34, 0.38, 0.46), flop * (0.72 + 0.28 * clearcoatMask));
        clearcoatF = (0.045 + 0.92 * pow(1.0 - ndotv, 4.6)) * clearcoatMask;
    } else if (matId == 8) {
        albedo *= 0.04;
        roughness = 0.024;
    }

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
    vec3 kD = (vec3(1.0) - F_schlick) * (1.0 - metallic * 0.88) * (1.0 - clearcoatF * 0.82);

    vec3 directLit = (kD * albedo / kPi + specBrdf * 0.62) * sunColor * ndotl * sunVis * 2.65;
    vec3 envLit    = albedo * ambient * contactAo * (1.0 - clearcoatF * 0.72);

    if (matId == 11) {
        float clothSheen = pow(1.0 - ndotv, 3.0) * 0.14;
        envLit += (albedo * 0.55 + vec3(0.08)) * skyAmbient * clothSheen;
    } else if (matId == 12) {
        float sssWrap = clamp((rawNdotL + 0.45) / 1.45, 0.0, 1.0) * (1.0 - ndotl);
        directLit += albedo * vec3(0.42, 0.13, 0.06) * sssWrap * sunColor * sunVis * 0.85;
    } else if (matId == 21) {
        // Sunlit foliage subsurface wrap backlighting so trees glow naturally in sunlight!
        float leafBack = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * 0.35 + clamp((rawNdotL + 0.40) / 1.40, 0.0, 1.0) * 0.22;
        directLit += albedo * vec3(0.55, 0.68, 0.28) * leafBack * sunColor * sunVis;
    }

    bool reflectiveMat = (isWindowPane || matId == 3 || matId == 4 || matId == 6 || matId == 7 || matId == 8 || puddleMask > 0.04 || wetRoadSurface > 0.15);
    if (reflectiveMat) {
        vec3 R = reflect(-V, N);
        bool isAuto = (matId == 7 || matId == 8);
        vec3 envRefl = sampleSeoulEnvMap(R, fragPosition, roughness, nightFactor, isAuto);

        if (matId == 8) {
            envRefl *= mix(1.0, 0.34, smoothstep(0.08, 0.72, R.y));
        } else if (matId == 7) {
            envRefl *= mix(1.0, 0.34, smoothstep(0.08, 0.56, R.y));
        }

        float specExp = (matId == 6) ? 210.0 : ((matId == 8) ? 340.0 : ((matId == 7) ? mix(260.0, 32.0, clamp(roughness * 1.4, 0.0, 1.0)) : 160.0));
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : ((matId == 8) ? 2.8 : ((matId == 7) ? 2.4 : 1.75)));

        float wetCoat = puddleMask * 0.62 + wetRoadSurface * 0.14;
        float coatStrength = (matId == 6) ? 0.88
                           : ((matId == 8) ? 0.64
                           : ((matId == 7) ? (0.86 * clearcoatMask)
                           : (isWindowPane ? (isGlassTowerWin ? 0.86 : 0.74)
                           : ((matId == 3) ? 0.68
                           : ((matId == 4) ? (0.36 * (1.0 - step(0.3, roughness))) : wetCoat)))));
        if (matId == 7) {
            float flake = valueNoise(fragPosition.xz * 125.0 + vec2(fragPosition.y * 98.0));
            float ccAlpha2 = 0.00016;
            float ccDen = (ndoth * ndoth * (ccAlpha2 - 1.0) + 1.0);
            float ccD = ccAlpha2 / max(kPi * ccDen * ccDen, 0.0001);
            float ccF = 0.042 + 0.958 * pow(1.0 - vdoth, 5.0);
            directLit += vec3(min(ccD * ccF * 0.019, 2.6)) * sunColor * ndotl * sunVis * clearcoatMask;
            envLit += albedo * pow(ndoth, 34.0) * (0.10 + 0.26 * flake) * (1.0 - roughness) * sunVis;
            vec3 metalReflTint = mix(vec3(1.0), albedo * 1.55 + vec3(0.04), metallic * 0.84);
            float clearF = 0.036 + 0.66 * pow(1.0 - ndotv, 4.4);
            float baseMetalF = (0.14 + 0.46 * pow(1.0 - ndotv, 2.4)) * metallic;
            envLit += envRefl * (vec3(clearF * coatStrength) + metalReflTint * baseMetalF * 0.65);
        } else {
            float coatFresnel = (matId == 8)
                                    ? (0.065 + 0.88 * pow(1.0 - ndotv, 4.2))
                                    : (isWindowPane
                                           ? (0.16 + 0.80 * pow(1.0 - ndotv, 3.6))
                                           : (0.06 + 0.90 * pow(1.0 - ndotv, 4.2)));
            envLit += envRefl * coatFresnel * coatStrength;
        }
    }

    // -----------------------------------------------------------------------
    // Dynamic Multi-Point & Spotlight Local Illumination (v3.0.0 Realistic LED
    // Headlight Optics — ZERO blown-out circular white bumper hotspots!)
    // -----------------------------------------------------------------------
    vec3 localLit = vec3(0.0);
    int numLocal = clamp(int(ubo.localLightCount.x + 0.5), 0, 20);
    for (int li = 0; li < numLocal; ++li) {
        vec3 lPos = ubo.lightPosRadius[li].xyz;
        float lRad = ubo.lightPosRadius[li].w;
        vec3 toL = lPos - fragPosition;
        float d2 = dot(toL, toL);
        if (d2 >= lRad * lRad || d2 < 0.0001) continue;
        float distL = sqrt(d2);
        vec3 lDir = toL / distL;
        float rawNdl = dot(N, lDir);
        bool isSpot = (ubo.lightColorType[li].w > 0.5);

        float effNdl = isSpot
            ? max(max(rawNdl, 0.0), smoothstep(0.0, 0.25, rawNdl + 0.06) * 0.24 * step(-0.02, rawNdl))
            : max(rawNdl, 0.0);
        if (effNdl <= 0.0001) continue;

        float uDist = clamp(1.0 - distL / lRad, 0.0, 1.0);
        float atten = (uDist * uDist) / (1.0 + 0.024 * d2);

        if (isSpot) {
            vec3 spotFwd = ubo.lightDirCone[li].xyz;
            float cosOuter = ubo.lightDirCone[li].w;
            float cosTheta = dot(-lDir, spotFwd);
            // Wide cubic optical feather + smooth near-bumper ramp eliminates circular hotspot rings!
            float cosInner = mix(cosOuter, 1.0, 0.72);
            float cone = smoothstep(cosOuter, cosInner, cosTheta);
            cone = cone * cone * (3.0 - 2.0 * cone);
            if (cone <= 0.001) continue;
            float nearRamp = smoothstep(0.65, 4.2, distL);
            float longFade = pow(uDist, 1.65);
            atten = longFade * nearRamp * cone / (1.0 + 0.032 * d2);
        }

        vec3 lCol = ubo.lightColorType[li].rgb * atten;
        vec3 H_l = normalize(lDir + V);
        float ndh_l = max(dot(N, H_l), 0.0);
        float vdh_l = max(dot(V, H_l), 0.0);
        float dDen = (ndh_l * ndh_l * (alpha2 - 1.0) + 1.0);
        float D_l = alpha2 / max(kPi * dDen * dDen, 0.0001);
        float G_l2 = (ndotv / (ndotv * (1.0 - kG) + kG)) * (effNdl / (effNdl * (1.0 - kG) + kG));
        vec3 F_l = f0 + (vec3(1.0) - f0) * pow(1.0 - vdh_l, 5.0);
        vec3 spec_l = min((D_l * G_l2 * F_l) / max(4.0 * ndotv * effNdl, 0.002), vec3(1.65));
        vec3 diff_l = (vec3(1.0) - F_l) * (1.0 - metallic * 0.85) * albedo / kPi;

        float wetGlint = (puddleMask * 0.75 + wetRoadSurface * 0.22) * pow(ndh_l, 48.0);
        localLit += (diff_l + spec_l * 0.45 + vec3(wetGlint)) * lCol * effNdl;
    }

    vec3 totalLinear = envLit + directLit + localLit + emissiveColor * emissive;

    // Real Seoul long-range atmospheric perspective
    float dist = length(viewPos - fragPosition);
    float fogStart = mix(280.0, 75.0, wetRoadFactor);
    float fogDens  = mix(0.00034, 0.0020, wetRoadFactor);
    float fogFactor = clamp(1.0 - exp(-max(0.0, dist - fogStart) * fogDens), 0.0, 0.82);
    vec3 linearFog = pow(ubo.fogColorAndTexel.xyz, vec3(2.2));
    vec3 fogged = mix(totalLinear, linearFog, fogFactor);

    outColor = vec4(fogged, alphaOut);
}
