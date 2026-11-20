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
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

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

    // Sharp ground-to-horizon-to-sky transition gives metallic car paint REAL stamped-steel definition!
    float horizSpan = isAutomotive ? (0.018 + rough * 0.14) : (0.045 + rough * 0.22);
    float horizBlend = smoothstep(-horizSpan, horizSpan, R.y);

    vec3 darkGroundRefl = isAutomotive
        ? mix(vec3(0.018, 0.020, 0.026), gndAmb * 0.34, 0.45)
        : (gndAmb * 0.56);
    vec3 upperSkyRefl = mix(horCol * (isAutomotive ? 1.15 : 0.88),
                            skyAmb * (isAutomotive ? 1.48 : 1.32),
                            pow(clamp(R.y, 0.0, 1.0), 0.52));
    vec3 envCol = mix(darkGroundRefl, upperSkyRefl, horizBlend);

    // Crisp bright automotive horizon band slicing along body character creases!
    if (isAutomotive) {
        float creaseBand = smoothstep(-0.025, 0.018, R.y) * (1.0 - smoothstep(0.022, 0.115, R.y));
        vec3 bandCol = mix(horCol * 1.35, vec3(1.18, 1.22, 1.28), 0.45) * (1.0 - nightF * 0.62);
        envCol += bandCol * creaseBand * (0.48 * (1.0 - rough * 0.72));
    }

    // Softened Seoul skyline reflection near the horizon
    float az = atan(R.z, R.x);
    float bldColIdx = floor((az + kPi) / (2.0 * kPi) * 48.0);
    float bldRnd = hash12(vec2(bldColIdx, 3.7));
    float skylineH = (0.03 + 0.18 * pow(bldRnd, 1.5)) * (1.0 - rough * 0.55);

    if (R.y > -0.02 && R.y < skylineH) {
        vec3 bldSil = mix(gndAmb * 0.32, skyAmb * 0.24, 0.40);
        bldSil += vec3(0.22, 0.16, 0.09) * nightF * (1.0 - clamp(R.y / max(skylineH, 0.01), 0.0, 1.0));
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
                bldSil += winPalette * (0.016 + nightF * 0.62) * paneMask * detailFade;
            }
        }
        float mask = (1.0 - smoothstep(skylineH - 0.030, skylineH, R.y)) * smoothstep(-0.02, 0.025, R.y);
        envCol = mix(envCol, bldSil, mask * (isAutomotive ? 0.52 : 0.64));
    }
    return envCol;
}

// True 3D Raycasted Interior Mapping inside Seoul tower & apartment windows
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
            surfaceCol = suiteTint * 0.28 * tile;
        }
    } else if (abs(tHit - tz) < 0.002) {
        float shelf = smoothstep(0.35, 0.45, abs(sin(hitPos.y * 12.0)));
        surfaceCol = suiteTint * mix(0.24, 0.42, shelf);
    } else {
        surfaceCol = suiteTint * 0.30;
    }

    float blindDrop = (roomSeed > 0.55) ? (0.16 + 0.20 * fract(roomSeed * 19.3)) : 0.0;
    if (!isLobby && paneUv.y > (1.0 - blindDrop)) {
        float slat = 0.75 + 0.25 * sin(paneUv.y * 95.0);
        return mix(surfaceCol, suiteTint * 0.32 * slat, 0.75) * (0.30 + nightF * 0.82);
    }
    return surfaceCol * depthFade * (0.28 + nightF * 1.05);
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
    bool isWindowPane = false;
    bool isGlassTowerWin = false;

    // -----------------------------------------------------------------------
    // Procedural World-Space Seoul Material Detail & Micro-Normal Perturbation
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Seoul Asphalt + Korean Yellow Centerline + Blue Bus Lane + Crosswalks + Wet Puddles
        vec2 p = fragPosition.xz;
        float distFade = 1.0 - smoothstep(24.0, 110.0, length(viewPos - fragPosition));
        float patchVar = fbm2(p * 0.24);
        float g1 = valueNoise(p * 3.8);
        float g2 = valueNoise(p * 19.5);
        float g3 = valueNoise(p * 48.0);
        float stoneSpeck = g1 * 0.45 + g2 * 0.35 + g3 * 0.20;

        albedo = vec3(0.056, 0.059, 0.064) * mix(0.76, 1.32, patchVar * 0.55 + stoneSpeck * 0.45);
        albedo += vec3(0.022, 0.022, 0.024) * smoothstep(0.76, 0.95, g3) * distFade;
        roughness = mix(0.68, 0.46, stoneSpeck);

        float crackWave = abs(fbm2(p * 0.42 + vec2(4.7, 9.2)) - 0.50);
        float tarCrack = (1.0 - smoothstep(0.004, 0.016, crackWave)) * smoothstep(0.42, 0.68, patchVar);
        albedo *= (1.0 - 0.32 * tarCrack);
        roughness = mix(roughness, 0.22, tarCrack * 0.72);

        float nx = (valueNoise(p * 24.0 + vec2(0.12, 0.0)) - g2) * distFade;
        float nz = (valueNoise(p * 24.0 + vec2(0.0, 0.12)) - g2) * distFade;
        N = normalize(vec3(N.x + nx * 0.20, N.y, N.z + nz * 0.20));

        float rIdxX = round(p.x / 84.0);
        float rIdxZ = round(p.y / 84.0);
        float rx = rIdxX * 84.0;
        float rz = rIdxZ * 84.0;
        float dx = abs(p.x - rx);
        float dz = abs(p.y - rz);
        bool onVert  = (dx <= 11.2);
        bool onHoriz = (dz <= 9.05) && (fragPosition.y < 3.5);

        if (onVert && !onHoriz) {
            float wear = exp(-pow((abs(dx - 4.2) - 0.85) / 0.62, 2.0));
            albedo *= (1.0 - 0.18 * wear);
            roughness = mix(roughness, 0.30, wear * 0.52);
            float gutter = smoothstep(7.7, 9.2, dx);
            albedo *= (1.0 - 0.22 * gutter);
        } else if (onHoriz && !onVert) {
            float wear = exp(-pow((abs(dz - 4.2) - 0.85) / 0.62, 2.0));
            albedo *= (1.0 - 0.18 * wear);
            roughness = mix(roughness, 0.30, wear * 0.52);
            float gutter = smoothstep(7.7, 9.0, dz);
            albedo *= (1.0 - 0.22 * gutter);
        }

        vec3 yellowPaint = pow(vec3(0.86, 0.66, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.80, 0.82, 0.84), vec3(2.2));
        vec3 busBlue     = pow(vec3(0.18, 0.48, 0.86), vec3(2.2));
        float paintWear  = mix(0.78, 1.0, smoothstep(0.18, 0.68, g1 * 0.5 + g2 * 0.5));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;
        float blueMask   = 0.0;

        bool isBlueBusVert = (abs(rIdxX - 7.0) < 0.2 || abs(rIdxX - 29.0) < 0.2 || abs(rIdxX + 11.0) < 0.2);
        bool isBlueBusHoriz = (abs(rIdxZ - 35.0) < 0.2 || abs(rIdxZ + 55.0) < 0.2);

        if (onVert && !onHoriz) {
            if (dz >= 9.35 && dz <= 12.15 && dx < 8.0 && fragPosition.y < 3.5) {
                float stripe = abs(fract((p.x - rx + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dz >= 12.50 && dz <= 12.95 && dx < 8.4 && fragPosition.y < 3.5) {
                whiteMask = max(whiteMask, 0.92);
            }
            if (dz > 13.15 || fragPosition.y >= 3.5) {
                yellowMask = max(yellowMask, aaBand(abs(dx - 0.18), 0.075));
                if (fract(p.y / 9.0) < 0.38) {
                    whiteMask = max(whiteMask, aaBand(abs(dx - 4.35), 0.075));
                }
                if (isBlueBusVert) {
                    blueMask = max(blueMask, aaBand(abs(dx - 6.65), 0.085));
                }
                whiteMask = max(whiteMask, aaBand(abs(dx - 8.55), 0.07) * 0.62);
            }
        } else if (onHoriz && !onVert) {
            if (dx >= 9.35 && dx <= 12.15 && dz < 8.0) {
                float stripe = abs(fract((p.y - rz + 8.0) / 1.85) - 0.5) * 1.85;
                whiteMask = max(whiteMask, aaBand(stripe, 0.42));
            }
            if (dx >= 12.50 && dx <= 12.95 && dz < 8.4) {
                whiteMask = max(whiteMask, 0.92);
            }
            if (dx > 13.15) {
                yellowMask = max(yellowMask, aaBand(abs(dz - 0.18), 0.075));
                if (fract(p.x / 9.0) < 0.38) {
                    whiteMask = max(whiteMask, aaBand(abs(dz - 4.35), 0.075));
                }
                if (isBlueBusHoriz) {
                    blueMask = max(blueMask, aaBand(abs(dz - 6.65), 0.085));
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
        float bPaintA = clamp(blueMask, 0.0, 1.0) * 0.88 * paintWear;
        albedo = mix(albedo, whitePaint, wPaintA);
        albedo = mix(albedo, yellowPaint, yPaintA);
        albedo = mix(albedo, busBlue, bPaintA);
        roughness = mix(roughness, 0.52, max(max(wPaintA, yPaintA), bPaintA) * 0.55);

        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.18, 0.0, 1.0);
        wetRoadSurface = wetAmt;
        if (wetAmt > 0.02) {
            vec2 warp = vec2(fbm2(p * 0.085 + vec2(3.1, 7.4)), fbm2(p * 0.085 + vec2(11.7, 2.3)));
            float pNoise = fbm2(p * 0.145 + warp * 1.45) * 0.72 + valueNoise(p * 0.52) * 0.28;
            float gutterBias = 0.0;
            if (onVert && !onHoriz) gutterBias = smoothstep(5.8, 8.7, dx) * 0.14;
            else if (onHoriz && !onVert) gutterBias = smoothstep(5.8, 8.7, dz) * 0.14;
            puddleMask = smoothstep(0.55 - wetAmt * 0.17, 0.77 - wetAmt * 0.10, pNoise + gutterBias) * wetAmt;

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
                float ripBlend = smoothstep(0.14, 0.58, puddleMask) * smoothstep(0.05, 0.35, wetRoadFactor) * distFade;
                N = normalize(mix(N, vec3(rDir.x * ripple * 0.16, 1.0, rDir.y * ripple * 0.16), ripBlend));
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Seoul Concrete Sidewalk Pavers
        vec2 p = fragPosition.xz;
        float distFade = 1.0 - smoothstep(20.0, 90.0, length(viewPos - fragPosition));
        vec2 cell = fract(p * 0.45);
        vec2 tileId = floor(p * 0.45);
        float seamDist = min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y));
        float seam = 1.0 - smoothstep(0.0, max(fwidth(seamDist), 0.015) * 1.4, seamDist - 0.016);
        float tileVar = hash12(tileId) * 0.11 - 0.055;
        float grain = valueNoise(p * 12.0) * 0.07 + valueNoise(p * 36.0) * 0.04;
        albedo *= (0.94 + tileVar + grain * distFade);
        albedo *= (1.0 - seam * 0.28);
        roughness = mix(0.84, 0.58, wetRoadFactor * 0.72);
        puddleMask = wetRoadFactor * smoothstep(0.45, 0.78, fbm2(p * 0.22)) * 0.42 * (1.0 - seam);
        albedo *= (1.0 - wetRoadFactor * 0.18 - puddleMask * 0.18);
        roughness = mix(roughness, 0.09, puddleMask);
    }
    else if (matId == 2 || matId == 3 || matId == 4) {
        // MAT 2 (Brick/Korean Apartment), MAT 3 (Glass Skyscraper), MAT 4 (Modern Concrete Office)
        if (abs(N.y) < 0.42) {
            float uCoord = (abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x;
            float vCoord = fragPosition.y;
            bool isGlassTower = (matId == 3);
            bool isConcrete   = (matId == 4);

            float bayW = isGlassTower ? 2.40 : (isConcrete ? 3.10 : 3.25);
            float floorH = isGlassTower ? 3.60 : 3.45;
            float colIdx = floor(uCoord / bayW);
            float flrIdx = floor(vCoord / floorH);
            float u = fract(uCoord / bayW);
            float v = fract(vCoord / floorH);

            bool inWindow = false;
            vec2 paneUv = vec2(0.5);
            if (isGlassTower) {
                inWindow = (u > 0.055 && u < 0.945 && v > 0.10 && v < 0.90);
                paneUv = vec2((u - 0.055) / 0.89, (v - 0.10) / 0.80);
            } else if (isConcrete) {
                inWindow = (u > 0.11 && u < 0.89 && v > 0.22 && v < 0.82);
                paneUv = vec2((u - 0.11) / 0.78, (v - 0.22) / 0.60);
            } else {
                // Authentic Korean Apartment (아파트) wide horizontal verandah sliding window
                inWindow = (u > 0.13 && u < 0.87 && v > 0.24 && v < 0.79);
                paneUv = vec2((u - 0.13) / 0.74, (v - 0.24) / 0.55);
            }

            // Crisp aluminum window frame / vertical 2-pane sash mullion & balcony railing bar
            bool onMullion = false;
            if (inWindow && !isGlassTower && vCoord > 0.65) {
                float midDist = abs(paneUv.x - 0.50);
                float frameDist = min(min(paneUv.x, 1.0 - paneUv.x), min(paneUv.y, 1.0 - paneUv.y));
                bool isRail = (!isConcrete) && (abs(paneUv.y - 0.26) < 0.045);
                onMullion = (midDist < 0.032) || (frameDist < 0.042) || isRail;
            }

            if (inWindow && !onMullion && vCoord > 0.65) {
                float roomSeed = hash12(vec2(colIdx * 1.37 + floor(fragPosition.x * 0.05) * 19.0, flrIdx * 2.13));
                bool isLobby = (flrIdx < 1.0);
                vec3 roomCol = sampleInteriorRoom3D(paneUv, V, N, roomSeed, isLobby, nightFactor);

                albedo = isGlassTower ? mix(vec3(0.030, 0.052, 0.082), albedo * 0.34, 0.42)
                                      : vec3(0.038, 0.054, 0.076);
                roughness = isGlassTower ? 0.045 : 0.065;
                metallic = isGlassTower ? 0.28 : 0.18;
                isWindowPane = true;
                isGlassTowerWin = isGlassTower;

                float litProb = isLobby ? 0.88 : (isGlassTower ? 0.56 : 0.44);
                if (roomSeed > (1.0 - litProb)) {
                    emissiveColor = roomCol;
                    emissive = max(emissive, mix(0.20, 1.18, nightFactor) * (isLobby ? 1.15 : 0.90));
                } else {
                    emissiveColor = roomCol * 0.26;
                    emissive = max(emissive, 0.07);
                }
            } else if (onMullion) {
                // Crisp satin aluminum Korean window sash & verandah safety rail
                albedo = vec3(0.32, 0.35, 0.39);
                roughness = 0.34;
                metallic = 0.68;
            } else {
                float spandrel = (v < 0.12 || v > 0.88) ? 0.86 : 1.0;
                float stoneTex = valueNoise(vec2(uCoord, vCoord) * 1.8) * 0.09;
                if (matId == 2) {
                    // Authentic Korean apartment concrete/stone horizontal slab lines
                    float hJoint = smoothstep(0.92, 0.98, abs(sin(vCoord * 6.5)));
                    float vJoint = smoothstep(0.94, 0.99, abs(sin(uCoord * 3.8)));
                    albedo *= (0.93 + stoneTex - (hJoint * 0.08 + vJoint * 0.05));
                } else {
                    albedo *= (spandrel + stoneTex);
                }
                roughness = isGlassTower ? 0.24 : (isConcrete ? 0.66 : 0.78);
                metallic  = isGlassTower ? 0.62 : (isConcrete ? 0.14 : 0.04);
                albedo *= mix(0.76, 1.0, smoothstep(0.0, 2.4, vCoord));
            }
        } else {
            float tarNoise = valueNoise(fragPosition.xz * 0.85) * 0.14;
            albedo *= (0.64 + tarNoise);
            roughness = 0.86;
            metallic = 0.04;
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
        albedo = mix(vec3(0.008, 0.042, 0.088), vec3(0.004, 0.014, 0.030), nightFactor);
        roughness = 0.055;
        metallic = 0.06;
    }
    else if (matId == 10) {
        // MAT 10: Seoul Park Lawn, Mountain Forest Slopes & Ginkgo/Cherry/Pine Tree Canopies
        float distFade = 1.0 - smoothstep(26.0, 120.0, length(viewPos - fragPosition));
        vec2 p = fragPosition.xz;
        float patchVar = fbm2(p * 0.08) * 0.30 + valueNoise(p * 2.2) * 0.12;
        albedo *= (0.82 + patchVar * (0.55 + 0.45 * distFade));
        roughness = 0.88;
        metallic = 0.0;
    }
    else if (matId == 11) {
        vec2 wp = fragPosition.xz + vec2(fragPosition.y * 1.4);
        float weave = valueNoise(wp * 42.0) * 0.09;
        albedo *= (0.95 + weave);
        roughness = max(roughness, 0.72);
    }
    else if (matId == 13) {
        vec3 p = fragPosition;
        if (abs(N.y) > 0.65) {
            float plankU = fract(p.z * 3.4);
            float plankId = floor(p.z * 3.4);
            float jointX = fract(p.x * 0.22 + hash12(vec2(plankId, 1.7)) * 2.0);
            float seamZ = 1.0 - smoothstep(0.025, 0.065, min(plankU, 1.0 - plankU));
            float seamX = 1.0 - smoothstep(0.008, 0.022, min(jointX, 1.0 - jointX));
            float grain = valueNoise(vec2(p.x * 1.6, p.z * 16.0)) * 0.14;
            albedo *= (0.84 + hash12(vec2(plankId, 5.1)) * 0.18 + grain) * (1.0 - max(seamZ, seamX) * 0.34);
            roughness = 0.80;
        } else {
            float bark = valueNoise(vec2((p.x + p.z) * 14.0, p.y * 2.6)) * 0.24;
            albedo *= (0.80 + bark);
            roughness = 0.90;
        }
    }
    else if (matId == 14) {
        // MAT 14: Seoul Granite Mountain Rock & Stone Retaining Walls
        vec3 p = fragPosition;
        float rock = fbm2(p.xz * 0.08) * 0.26 + valueNoise(p.xy * 0.45) * 0.12;
        albedo *= (0.82 + rock);
        roughness = 0.86;
    }
    else if (matId == 15) {
        // MAT 15: Traditional Korean Gyeongbokgung Giwa Roof Tiles & Dancheong Ribs
        float rib = 0.86 + 0.14 * cos(fragPosition.x * 9.5 + fragPosition.z * 9.5);
        albedo *= rib;
        roughness = 0.56;
        metallic = 0.08;
    }

    // -----------------------------------------------------------------------
    // Physically Based Cook-Torrance GGX + Energy-Conserving Automotive Clearcoat
    // -----------------------------------------------------------------------
    float rawNdotL = dot(N, L);
    float ndotl = max(rawNdotL, 0.0);
    float shadow = (ndotl > 0.01) ? computeShadow(fragPosLightSpace, N, L) : 0.0;
    float sunVis = 1.0 - shadow * 0.86;

    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);
    ambient = mix(ambient, ambient * vec3(0.76, 0.86, 1.08), shadow * 0.42);
    float contactAo = mix(0.66, 1.0, clamp(fragPosition.y / 1.7, 0.0, 1.0));
    if (N.y > 0.75) contactAo = 1.0;

    vec3 H = normalize(L + V);
    float ndoth = max(dot(N, H), 0.0);
    float ndotv = max(dot(N, V), 0.001);
    float vdoth = max(dot(V, H), 0.0);

    // Metallic automotive paint angular flop + energy conservation (ZERO pastel washout / ZERO jelly!)
    float clearcoatMask = 0.0;
    float clearcoatF = 0.0;
    if (matId == 7) {
        clearcoatMask = 1.0 - smoothstep(0.24, 0.54, roughness) * 0.82;
        float flop = pow(1.0 - ndotv, 1.95);
        albedo *= mix(vec3(1.00), vec3(0.36, 0.40, 0.48), flop * (0.72 + 0.28 * clearcoatMask));
        clearcoatF = (0.042 + 0.91 * pow(1.0 - ndotv, 4.8)) * clearcoatMask;
    } else if (matId == 8) {
        // Deep luxury smoked automotive glass (sleek tinted look on BOTH raked windshield & side windows!)
        albedo *= 0.04;
        roughness = 0.025;
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

    vec3 directLit = (kD * albedo / kPi + specBrdf * 0.56) * sunColor * ndotl * sunVis * 2.35;
    vec3 envLit    = albedo * ambient * contactAo * (1.0 - clearcoatF * 0.72);

    if (matId == 11) {
        float clothSheen = pow(1.0 - ndotv, 3.0) * 0.14;
        envLit += (albedo * 0.55 + vec3(0.08)) * skyAmbient * clothSheen;
    } else if (matId == 12) {
        float sssWrap = clamp((rawNdotL + 0.45) / 1.45, 0.0, 1.0) * (1.0 - ndotl);
        directLit += albedo * vec3(0.42, 0.13, 0.06) * sssWrap * sunColor * sunVis * 0.85;
    }

    bool reflectiveMat = (isWindowPane || matId == 3 || matId == 4 || matId == 6 || matId == 7 || matId == 8 || puddleMask > 0.04 || wetRoadSurface > 0.15);
    if (reflectiveMat) {
        vec3 R = reflect(-V, N);
        bool isAuto = (matId == 7 || matId == 8);
        vec3 envRefl = sampleSeoulEnvMap(R, fragPosition, roughness, nightFactor, isAuto);

        // Temper zenith sky washout on raked automotive windshields & flat hoods so paint & smoked glass stay deep & rich!
        if (matId == 8) {
            envRefl *= mix(1.0, 0.34, smoothstep(0.08, 0.72, R.y));
        } else if (matId == 7) {
            envRefl *= mix(1.0, 0.32, smoothstep(0.08, 0.56, R.y));
        }

        float specExp = (matId == 6) ? 210.0 : ((matId == 8) ? 340.0 : ((matId == 7) ? mix(260.0, 32.0, clamp(roughness * 1.4, 0.0, 1.0)) : 140.0));
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : ((matId == 8) ? 2.8 : ((matId == 7) ? 2.3 : 1.35)));

        float wetCoat = puddleMask * 0.62 + wetRoadSurface * 0.14;
        float coatStrength = (matId == 6) ? 0.88
                           : ((matId == 8) ? 0.62
                           : ((matId == 7) ? (0.82 * clearcoatMask)
                           : (isWindowPane ? (isGlassTowerWin ? 0.76 : 0.62)
                           : ((matId == 3) ? 0.64
                           : ((matId == 4) ? (0.44 * (1.0 - step(0.3, roughness))) : wetCoat)))));
        if (matId == 7) {
            // Dual-lobe showroom clearcoat + rich colored metallic basecoat flake reflection!
            float flake = valueNoise(fragPosition.xz * 125.0 + vec2(fragPosition.y * 98.0));
            float ccAlpha2 = 0.00016;
            float ccDen = (ndoth * ndoth * (ccAlpha2 - 1.0) + 1.0);
            float ccD = ccAlpha2 / max(kPi * ccDen * ccDen, 0.0001);
            float ccF = 0.042 + 0.958 * pow(1.0 - vdoth, 5.0);
            directLit += vec3(min(ccD * ccF * 0.019, 2.6)) * sunColor * ndotl * sunVis * clearcoatMask;
            envLit += albedo * pow(ndoth, 34.0) * (0.10 + 0.26 * flake) * (1.0 - roughness) * sunVis;
            // Colored metallic basecoat reflection keeps crimson/blue hoods rich instead of pastel pink!
            vec3 metalReflTint = mix(vec3(1.0), albedo * 1.55 + vec3(0.04), metallic * 0.84);
            float clearF = 0.032 + 0.62 * pow(1.0 - ndotv, 4.6);
            float baseMetalF = (0.12 + 0.44 * pow(1.0 - ndotv, 2.4)) * metallic;
            envLit += envRefl * (vec3(clearF * coatStrength) + metalReflTint * baseMetalF * 0.62);
        } else {
            float coatFresnel = (matId == 8)
                                    ? (0.065 + 0.88 * pow(1.0 - ndotv, 4.2))
                                    : (isWindowPane
                                           ? (0.28 + 0.66 * pow(1.0 - ndotv, 3.2))
                                           : (0.06 + 0.90 * pow(1.0 - ndotv, 4.2)));
            envLit += envRefl * coatFresnel * coatStrength;
        }
    }

    vec3 totalLinear = envLit + directLit + emissiveColor * emissive;

    // Real Seoul long-range atmospheric perspective (lets Lotte World Tower & N Seoul Tower remain visible kilometers away!)
    float dist = length(viewPos - fragPosition);
    float fogStart = mix(240.0, 75.0, wetRoadFactor);
    float fogDens  = mix(0.00042, 0.0022, wetRoadFactor);
    float fogFactor = clamp(1.0 - exp(-max(0.0, dist - fogStart) * fogDens), 0.0, 0.84);
    vec3 linearFog = pow(ubo.fogColorAndTexel.xyz, vec3(2.2));
    vec3 fogged = mix(totalLinear, linearFog, fogFactor);

    // Output true linear HDR to 16-bit float framebuffer (ACES + bloom runs in post.frag!)
    outColor = vec4(fogged, alphaOut);
}
