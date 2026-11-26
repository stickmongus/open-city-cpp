#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUv;
layout(location = 3) in vec4 fragColor;
layout(location = 4) in vec4 fragPosLightSpace;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 viewProj;
    mat4 lightVP;
    vec4 lightDirAndShadows;   // xyz = lightDir, w = shadow PCF taps (0=off, 1/4/8/16)
    vec4 sunColorAndTime;      // xyz = sunColor, w = animTime
    vec4 skyAmbientAndNight;   // xyz = skyAmbient, w = nightFactor
    vec4 groundAmbientAndWet;  // xyz = groundAmbient, w = wetRoadFactor
    vec4 fogColorAndTexel;     // xyz = fogColor, w = shadowTexelSize
    vec4 horizonColorAndDay;   // xyz = horizonColor, w = dayFactor
    vec4 viewPosAndHour;       // xyz = viewPos, w = timeHours
    vec4 localLightCount;      // x = local lights (0..20), y = refl/parallax (0..2), z = fogStartM, w = fogDensityScale
    vec4 lightPosRadius[20];   // xyz = worldPos, w = radius
    vec4 lightColorType[20];   // rgb = linear HDR intensity, w = type (0 = point, 1 = spot)
    vec4 lightDirCone[20];     // xyz = unit forward dir, w = cosOuterHalfAngle
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D signAtlasTex;
layout(set = 0, binding = 3) uniform sampler2D pbrAlbedoTex;
layout(set = 0, binding = 4) uniform sampler2D pbrNormalRoughTex;
layout(set = 0, binding = 5) uniform sampler2D streetAlbedoTex;
layout(set = 0, binding = 6) uniform sampler2D streetNormalRoughTex;

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

// Seam-free 4x4 4096x4096 Architectural PBR Texture Atlas Sampling using explicit tile-scaled gradients (ZERO mip bleeding!)
void samplePbrTile(int tileId, vec2 uv, out vec3 outAlbedoLin, out vec2 outNormXY, out float outRough, out float outAO) {
    vec2 tileOrigin = vec2(float(tileId & 3), float(tileId >> 2)) * 0.25;
    vec2 ddx = clamp(dFdx(uv) * 0.25, vec2(-0.08), vec2(0.08));
    vec2 ddy = clamp(dFdy(uv) * 0.25, vec2(-0.08), vec2(0.08));
    // Trilinear-safe >=1.15-texel inset guarantees adjacent 4x4 atlas tiles NEVER bleed at any mip level (0..7)!
    float rho = max(length(ddx), length(ddy)) * 4096.0;
    float mipLod = clamp(log2(max(rho, 1.0)), 0.0, 7.0);
    float pad = clamp(1.15 * exp2(ceil(mipLod)) / 1024.0, 0.0025, 0.16);
    vec2 local = mix(vec2(pad), vec2(1.0 - pad), fract(uv)) * 0.25;
    vec2 atlasUv = tileOrigin + local;
    vec3 texAlb = textureGrad(pbrAlbedoTex, atlasUv, ddx, ddy).rgb;
    vec4 texNR  = textureGrad(pbrNormalRoughTex, atlasUv, ddx, ddy);
    outAlbedoLin = pow(clamp(texAlb, 0.0, 1.0), vec3(2.2));
    outNormXY = texNR.xy * 2.0 - 1.0;
    outRough = clamp(texNR.b, 0.03, 0.98);
    outAO = clamp(texNR.a, 0.25, 1.0);
}

// Seam-free 4x4 4096x4096 Dedicated Seoul Street/Road/Sidewalk PBR Atlas Sampling (v5.0.0)
// Explicit-gradient variant keeps dFdx/dFdy strictly in uniform control flow (zero edge-seam UB on Adreno/Mali/Desktop/Lavapipe!)
void sampleStreetTileGrad(int tileId, vec2 uv, vec2 uvDdx, vec2 uvDdy,
                          out vec3 outAlbedoLin, out vec2 outNormXY, out float outRough, out float outAO) {
    vec2 tileOrigin = vec2(float(tileId & 3), float(tileId >> 2)) * 0.25;
    vec2 ddx = clamp(uvDdx * 0.25, vec2(-0.08), vec2(0.08));
    vec2 ddy = clamp(uvDdy * 0.25, vec2(-0.08), vec2(0.08));
    // Trilinear-safe >=1.15-texel inset eliminates 100% of cross-tile color bleeding (e.g., yellow Tile 5 into dark highway Tile 1!)
    float rho = max(length(ddx), length(ddy)) * 4096.0;
    float mipLod = clamp(log2(max(rho, 1.0)), 0.0, 7.0);
    float pad = clamp(1.15 * exp2(ceil(mipLod)) / 1024.0, 0.0025, 0.16);
    vec2 local = mix(vec2(pad), vec2(1.0 - pad), fract(uv)) * 0.25;
    vec2 atlasUv = tileOrigin + local;
    vec3 texAlb = textureGrad(streetAlbedoTex, atlasUv, ddx, ddy).rgb;
    vec4 texNR  = textureGrad(streetNormalRoughTex, atlasUv, ddx, ddy);
    outAlbedoLin = pow(clamp(texAlb, 0.0, 1.0), vec3(2.2));
    outNormXY = texNR.xy * 2.0 - 1.0;
    outRough = clamp(texNR.b, 0.03, 0.98);
    outAO = clamp(texNR.a, 0.25, 1.0);
}

void sampleStreetTile(int tileId, vec2 uv, out vec3 outAlbedoLin, out vec2 outNormXY, out float outRough, out float outAO) {
    sampleStreetTileGrad(tileId, uv, dFdx(uv), dFdy(uv), outAlbedoLin, outNormXY, outRough, outAO);
}

// Automatic Screen-Space Cotangent-Frame Tangent-Space Normal Perturbation (explicit-gradient variant)
vec3 perturbNormalGrad(vec3 N, vec3 dp1, vec3 dp2, vec2 duv1, vec2 duv2, vec2 normalXY, float strength) {
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

vec3 perturbNormal(vec3 N, vec3 pos, vec2 uv, vec2 normalXY, float strength) {
    return perturbNormalGrad(N, dFdx(pos), dFdy(pos), dFdx(uv), dFdy(uv), normalXY, strength);
}

// Configurable 1 / 4 / 8 / 16-Tap Rotated Poisson Disk Soft PCF Shadows in Vulkan [0, 1] NDC depth space
float computeShadow(vec4 lightSpacePos, vec3 N, vec3 L) {
    int pcfTaps = clamp(int(ubo.lightDirAndShadows.w + 0.5), 0, 16);
    if (pcfTaps <= 0) return 0.0;
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

    if (pcfTaps == 1) {
        float d = texture(shadowMap, uv).r;
        return ((depth - bias > d) ? 1.0 : 0.0) * edgeFade;
    }

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
    float spread = ubo.fogColorAndTexel.w * ((pcfTaps >= 12) ? 2.1 : 1.65);

    float occluded = 0.0;
    for (int i = 0; i < pcfTaps; ++i) {
        float d = texture(shadowMap, uv + (rot * poisson[i]) * spread).r;
        occluded += (depth - bias > d) ? 1.0 : 0.0;
    }
    return (occluded / float(pcfTaps)) * edgeFade;
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
    vec3 windowReflTint = vec3(1.0);

    float viewDist = length(viewPos - fragPosition);
    float detailFade = 1.0 - smoothstep(32.0, 160.0, viewDist);

    // -----------------------------------------------------------------------
    // 2048x2048 Mipmapped, 8x Anisotropic, Tangent-Space Normal-Mapped PBR!
    // -----------------------------------------------------------------------
    if (matId == 1) {
        // MAT 1: Dedicated 4K Seoul Street PBR Atlas (Urban Asphalt / Expressway SMA / Intersection / Bridge Joint / Alley Cobble)
        //        + Real Curved Korean Lane Markings, Stop Bars, Directional Arrows, Manholes & Gutter Drains!
        vec2 p = fragPosition.xz;
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 dPdx = dFdx(p);
        vec2 dPdy = dFdy(p);
        vec2 dUvDx = dFdx(fragUv);
        vec2 dUvDy = dFdy(fragUv);
        vec2 asphUv = p * 0.26;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;

        bool isRealRibbon = (fragUv.y > 0.5);
        float xM = fragUv.x;
        float sM = fragUv.y;
        float hwM = max(2.4, srgbBase.r * 25.5);
        int gByte = int(floor(srgbBase.g * 255.0 + 0.5));
        float totalLanes = float(gByte >> 4);
        int roadFlags = gByte & 15;
        bool suppressLeftEdge  = ((roadFlags & 1) != 0);
        bool suppressRightEdge = ((roadFlags & 2) != 0);
        bool isInterCrosswalk  = ((roadFlags & 4) != 0);
        bool isCleanInterBox   = ((roadFlags & 8) != 0);
        float styleByte = floor(srgbBase.b * 255.0 + 0.5);
        float axM = abs(xM);

        bool isAlley   = isRealRibbon && ((totalLanes < 0.5) || (styleByte < 24.0));
        bool isOneway  = isRealRibbon && (styleByte >= 96.0);
        bool isHighway = isRealRibbon && (styleByte >= 152.0 && styleByte < 188.0);
        bool isBridge  = isRealRibbon && (styleByte >= 188.0 && styleByte < 216.0);
        bool hasBusLn  = isRealRibbon && (styleByte >= 220.0);

        int streetTile = 0; // Tile 0: Urban Seoul Fine-Aggregate Asphalt
        vec2 roadPbrUv = asphUv;
        vec2 roadDdx = dPdx * 0.26;
        vec2 roadDdy = dPdy * 0.26;
        if (isCleanInterBox) {
            streetTile = 2; // Tile 2: Worn multi-directional intersection asphalt
        } else if (isHighway) {
            streetTile = 1; // Tile 1: Smooth Dark Korean Expressway SMA
            roadPbrUv = vec2(xM * 0.24, sM * 0.24);
            roadDdx = dUvDx * 0.24;
            roadDdy = dUvDy * 0.24;
        } else if (isAlley) {
            streetTile = 14; // Tile 14: Seoul residential brick/cobble alley blend
            roadPbrUv = p * 0.34;
            roadDdx = dPdx * 0.34;
            roadDdy = dPdy * 0.34;
        }
        sampleStreetTileGrad(streetTile, roadPbrUv, roadDdx, roadDdy, pAlb, pNxy, pR, pAO);

        if (isAlley) {
            // Subtle weathered asphalt-cobble blend typical of Seongsu/Bukchon/Itaewon back streets
            vec3 aAlb; vec2 aNxy; float aR; float aAO;
            sampleStreetTileGrad(0, asphUv, dPdx * 0.26, dPdy * 0.26, aAlb, aNxy, aR, aAO);
            float alleyMix = smoothstep(0.35, 0.72, fbm2(p * 0.04));
            pAlb = mix(aAlb * 0.96, pAlb * 0.58, alleyMix * 0.55);
            pNxy = mix(aNxy, pNxy, alleyMix * 0.55);
            pR   = mix(aR, pR, alleyMix * 0.55);
            pAO  = mix(aAO, pAO, alleyMix * 0.55);
        }

        float macroPatina = fbm2(p * 0.045) * 0.55 + fbm2(p * 0.14) * 0.45;
        if (isHighway) {
            // Sleek dark Korean expressway SMA (textured deep slate-charcoal, distinct from urban boulevards)
            albedo = (pAlb * 1.12 + vec3(0.004, 0.004, 0.005)) * mix(0.90, 1.12, macroPatina);
        } else if (!isAlley) {
            // Authentic sunlit weathered Seoul urban granite-aggregate asphalt (zero squiggle artifacts!)
            albedo = (pAlb * 1.08 + vec3(0.007, 0.007, 0.009)) * mix(0.88, 1.14, macroPatina);
        } else {
            albedo = (pAlb * 1.05 + vec3(0.006, 0.006, 0.007)) * mix(0.88, 1.14, macroPatina);
        }
        roughness = mix(pR, 0.56, 0.20);
        surfaceAO = pAO;
        N = perturbNormalGrad(N, dp1, dp2, roadDdx, roadDdy, pNxy, 0.78 * detailFade);

        // Bridge Metallic Steel Finger Expansion Joints (교량 신축이음쇠) every 38m on Han River bridges & viaducts!
        if (isBridge && !isCleanInterBox) {
            float jointMod = abs(fract(sM / 38.0) - 0.5) * 38.0;
            if (jointMod < 0.45) {
                vec3 jAlb; vec2 jNxy; float jR; float jAO;
                vec2 jUv = vec2(xM * 0.65, 0.5 + (fract(sM / 38.0) - 0.5) * (38.0 / 0.90) * 0.12);
                vec2 jDdx = vec2(dUvDx.x * 0.65, dUvDx.y * (0.12 / 0.90));
                vec2 jDdy = vec2(dUvDy.x * 0.65, dUvDy.y * (0.12 / 0.90));
                sampleStreetTileGrad(8, jUv, jDdx, jDdy, jAlb, jNxy, jR, jAO);
                float jBlend = smoothstep(0.45, 0.22, jointMod);
                albedo = mix(albedo, jAlb * 1.08, jBlend);
                roughness = mix(roughness, jR, jBlend);
                metallic = mix(metallic, 0.42, jBlend * smoothstep(0.28, 0.08, jointMod));
                surfaceAO = mix(surfaceAO, jAO, jBlend);
                N = perturbNormalGrad(N, dp1, dp2, jDdx, jDdy, jNxy, 0.85 * detailFade * jBlend);
            }
        }

        // Realistic Cast-Iron Seoul Manhole Covers (서울시 맨홀) & Curb Gutter Drain Grates (빗물받이)!
        if (isRealRibbon && !isHighway && !isBridge && !isAlley && !isCleanInterBox && detailFade > 0.05) {
            // Cast-iron manhole medallion embedded along right-side lane every 44m
            float mhS = (fract(sM / 44.0) - 0.5) * 44.0;
            float mhX = xM - (hwM - 2.15);
            if (abs(mhS) < 0.46 && abs(mhX) < 0.46 && !suppressRightEdge) {
                vec2 mhUv = vec2(mhX, mhS) / 0.92 + 0.5;
                float rDist = length(mhUv - 0.5);
                if (rDist < 0.46) {
                    vec3 mAlb; vec2 mNxy; float mR; float mAO;
                    vec2 mDdx = dUvDx / 0.92;
                    vec2 mDdy = dUvDy / 0.92;
                    sampleStreetTileGrad(7, mhUv, mDdx, mDdy, mAlb, mNxy, mR, mAO);
                    float mBlend = smoothstep(0.46, 0.41, rDist) * detailFade;
                    albedo = mix(albedo, mAlb, mBlend);
                    roughness = mix(roughness, mR, mBlend);
                    metallic = mix(metallic, 0.55, mBlend);
                    surfaceAO = mix(surfaceAO, mAO, mBlend);
                    N = perturbNormalGrad(N, dp1, dp2, mDdx, mDdy, mNxy, 0.90 * mBlend);
                }
            }
            // Curb gutter stormwater drain grates every 18m along non-suppressed outer curb!
            float drS = abs(fract(sM / 18.0) - 0.5) * 18.0;
            bool onOuterEdge = ((xM > hwM - 0.56) && !suppressRightEdge) || ((xM < -hwM + 0.56) && !suppressLeftEdge && !isOneway);
            if (onOuterEdge && drS < 0.34) {
                float slot = step(0.42, fract(sM * 16.0));
                float grateMask = smoothstep(0.34, 0.24, drS) * detailFade;
                vec3 grateCol = mix(vec3(0.015, 0.018, 0.022), vec3(0.18, 0.20, 0.23), slot);
                albedo = mix(albedo, grateCol, grateMask * 0.92);
                roughness = mix(roughness, mix(0.95, 0.36, slot), grateMask);
                metallic = mix(metallic, slot * 0.65, grateMask);
            }
        }

        vec3 yellowPaint = pow(vec3(0.90, 0.70, 0.16), vec3(2.2));
        vec3 whitePaint  = pow(vec3(0.84, 0.86, 0.88), vec3(2.2));
        vec3 busBlue     = pow(vec3(0.16, 0.48, 0.90), vec3(2.2));
        float paintWear  = mix(0.84, 1.0, smoothstep(0.15, 0.72, valueNoise(p * 3.8)));
        float yellowMask = 0.0;
        float whiteMask  = 0.0;
        float blueMask   = 0.0;

        if (isRealRibbon) {
            // Subtle asphalt gutter darkening near non-suppressed curb edges (0.62m strip)
            if (!isCleanInterBox) {
                bool onLeft = (xM < 0.0);
                if ((onLeft && !suppressLeftEdge) || (!onLeft && !suppressRightEdge)) {
                    albedo *= (1.0 - 0.14 * smoothstep(hwM - 0.62, hwM, axM));
                }
            }

            if (isCleanInterBox) {
                // CLEAN REAL-WORLD INTERSECTION BOX: flush dark Seoul asphalt with zero overlapping lane/edge lines!
                albedo *= 0.96;
                roughness = mix(roughness, 0.48, 0.22);
            } else if (isHighway) {
                // KOREAN EXPRESSWAY / URBAN MOTORWAY (경부고속도로 · 올림픽대로 · 강변북로 · 내부순환로 · 분당수서로):
                albedo *= 0.94;
                roughness = mix(roughness, 0.44, 0.24);

                float shR = (hwM >= 7.0) ? 1.15 : 0.55;
                float shL = (hwM >= 7.0) ? 0.58 : 0.32;
                float driveLeft  = -hwM + shL;
                float driveRight =  hwM - shR;
                float driveSpan  = max(2.6, driveRight - driveLeft);
                float hwLaneW    = driveSpan / max(1.0, totalLanes);

                if (!suppressRightEdge && xM > driveRight + 0.12) {
                    float rumble = 0.5 + 0.5 * sin(sM * 14.0);
                    albedo *= mix(0.88, 0.96, rumble);
                }

                if (xM >= driveLeft && xM <= driveRight) {
                    float laneU = fract((xM - driveLeft) / max(2.4, hwLaneW));
                    float trackL = exp(-pow((laneU - 0.27) / 0.11, 2.0));
                    float trackR = exp(-pow((laneU - 0.73) / 0.11, 2.0));
                    float tireWear = clamp(trackL + trackR, 0.0, 1.0);
                    albedo *= (1.0 - 0.10 * tireWear);
                    roughness = mix(roughness, 0.32, tireWear * 0.45);
                }

                if (!suppressRightEdge) {
                    whiteMask = max(whiteMask, aaBand(abs(xM - driveRight), 0.092) * 0.96);
                }
                if (!suppressLeftEdge) {
                    yellowMask = max(yellowMask, aaBand(abs(xM - driveLeft), 0.092) * 0.96);
                }

                // Authentic Korean Highway 8.0m white dash / 12.0m gap (20.0m cycle)
                float hwDashOn = (fract(sM / 20.0) < 0.40) ? 1.0 : 0.0;
                float relX = xM - driveLeft;
                if (totalLanes > 1.5 && hwDashOn > 0.5 && relX > hwLaneW * 0.45 && relX < (driveSpan - hwLaneW * 0.45)) {
                    float laneIdxF = floor(relX / hwLaneW + 0.5);
                    float divDist = abs(relX - laneIdxF * hwLaneW);
                    whiteMask = max(whiteMask, aaBand(divDist, 0.084) * 0.94);
                }

                // Reflective Highway Cat's-Eye Road Studs (표지병) every 12m along shoulders!
                float studS = abs(fract(sM / 12.0) - 0.5) * 12.0;
                if (studS < 0.16) {
                    float studR = suppressRightEdge ? 0.0 : aaBand(abs(xM - (driveRight + 0.14)), 0.085);
                    float studL = suppressLeftEdge  ? 0.0 : aaBand(abs(xM - (driveLeft  - 0.14)), 0.085);
                    float studMask = max(studR, studL) * smoothstep(0.16, 0.06, studS);
                    if (studMask > 0.05) {
                        vec3 studCol = (studL > studR) ? vec3(1.0, 0.68, 0.12) : vec3(0.92, 0.97, 1.0);
                        emissiveColor = mix(emissiveColor, studCol, studMask);
                        emissive = max(emissive, studMask * (0.30 + nightFactor * 1.55));
                    }
                }
            } else if (!isAlley) {
                float usableHalf = max(1.8, hwM - 0.34);
                float laneW = (2.0 * usableHalf) / max(1.0, totalLanes);

                // Realistic per-lane dual wheel-track asphalt polishing
                float laneU = fract((xM + usableHalf) / max(2.2, laneW));
                float trackL = exp(-pow((laneU - 0.27) / 0.11, 2.0));
                float trackR = exp(-pow((laneU - 0.73) / 0.11, 2.0));
                float tireWear = clamp(trackL + trackR, 0.0, 1.0);
                albedo *= (1.0 - 0.11 * tireWear);
                roughness = mix(roughness, 0.34, tireWear * 0.42);

                // Authentic Real-Intersection Korean Zebra Crosswalk (횡단보도) — break yellow/blue lines cleanly!
                if (isInterCrosswalk && !isBridge && totalLanes >= 2.0) {
                    if (axM < (hwM - 0.52)) {
                        float barDist = abs(fract(xM / 0.88) - 0.5) * 0.88;
                        whiteMask = max(whiteMask, aaBand(barDist, 0.215) * 0.94);
                    }
                } else {
                    if (!suppressLeftEdge && xM < 0.0) {
                        float leftBand = aaBand(abs(xM - (-hwM + 0.26)), 0.074);
                        if (isBridge) {
                            whiteMask = max(whiteMask, leftBand * 0.90);
                        } else if (isOneway) {
                            yellowMask = max(yellowMask, leftBand * 0.92);
                        } else {
                            yellowMask = max(yellowMask, leftBand * 0.74);
                        }
                    }
                    if (!suppressRightEdge && xM > 0.0) {
                        float rightBand = aaBand(abs(xM - (hwM - 0.26)), 0.074);
                        if (isBridge) {
                            whiteMask = max(whiteMask, rightBand * 0.90);
                        } else {
                            yellowMask = max(yellowMask, rightBand * 0.78);
                        }
                    }
                    // Korean city standard 3.0m white dash / 5.0m gap (8.0m cycle)
                    float dashOn = (fract(sM / 8.0) < 0.375) ? 1.0 : 0.0;

                    if (!isOneway) {
                        // Two-way city street: crisp Korean double yellow centerline (2 x 0.12m @ +/-0.14m)
                        yellowMask = max(yellowMask, aaBand(abs(axM - 0.14), 0.062) * 0.95);
                        float lanesPerDir = max(1.0, floor(totalLanes * 0.5 + 0.5));
                        float dirLaneW = (usableHalf - 0.22) / lanesPerDir;
                        if (lanesPerDir > 1.5 && dashOn > 0.5) {
                            float localX = axM - 0.22;
                            if (localX > dirLaneW * 0.5 && localX < (usableHalf - 0.22) - dirLaneW * 0.4) {
                                float divDist = abs(fract(localX / dirLaneW + 0.5) - 0.5) * dirLaneW;
                                whiteMask = max(whiteMask, aaBand(divDist, 0.072));
                            }
                        }
                    } else {
                        // One-way multi-lane city boulevard / bridge carriageway
                        float relX = xM + usableHalf;
                        if (totalLanes > 1.5 && relX > laneW * 0.45 && relX < (2.0 * usableHalf - laneW * 0.45)) {
                            float laneIdxF = floor(relX / laneW + 0.5);
                            float divDist = abs(relX - laneIdxF * laneW);
                            bool isBusDiv = hasBusLn && (abs(laneIdxF - (totalLanes - 1.0)) < 0.25);
                            if (isBusDiv) {
                                // Iconic Seoul Blue Bus-Only Lane divider (파란색 버스전용차로!)
                                blueMask = max(blueMask, aaBand(divDist, 0.084) * 0.96);
                            } else if (dashOn > 0.5) {
                                whiteMask = max(whiteMask, aaBand(divDist, 0.074));
                            }
                        }

                        // Crisp Korean Painted White Lane Directional Arrows (직진/좌·우회전 노면표시) & Crosswalk Diamonds (◇)!
                        if (!isBridge && totalLanes >= 2.0 && relX > 0.2 && relX < (2.0 * usableHalf - 0.2)) {
                            float laneIdx = floor(relX / laneW);
                            float lx = (relX - (laneIdx + 0.5) * laneW); // Centered in lane [-laneW/2 .. +laneW/2]
                            float symMod = fract(sM / 40.0) * 40.0;
                            // Painted Straight / Left-Turn / Right-Turn Arrow @ symMod in [14..18.2m]
                            if (symMod > 14.0 && symMod < 18.2 && abs(lx) < 0.85) {
                                float sy = symMod - 16.0; // [-2.0 .. +2.2] along travel direction
                                float shaft = aaBand(abs(lx), 0.11) * step(-1.85, sy) * step(sy, 0.70);
                                // Forward arrowhead triangle (sy in [0.55..1.85], widens linearly toward base)
                                float tipT = clamp((1.85 - sy) / 1.30, 0.0, 1.0);
                                float head = aaBand(abs(lx), tipT * 0.44) * step(0.55, sy) * step(sy, 1.85);
                                float arrowM = max(shaft, head);
                                // Leftmost lane adds subtle left-turn wing; rightmost non-bus lane adds right-turn wing!
                                if (laneIdx < 0.5 && totalLanes >= 3.0) {
                                    float lArm = aaBand(abs(sy - 0.10), 0.11) * step(-0.56, lx) * step(lx, 0.0);
                                    float lTip = aaBand(abs(sy - 0.10), clamp((lx + 0.78) / 0.34, 0.0, 1.0) * 0.26) * step(-0.78, lx) * step(lx, -0.44);
                                    arrowM = max(arrowM, max(lArm, lTip));
                                } else if (laneIdx > totalLanes - 1.5 && totalLanes >= 3.0 && !hasBusLn) {
                                    float rArm = aaBand(abs(sy - 0.10), 0.11) * step(0.0, lx) * step(lx, 0.56);
                                    float rTip = aaBand(abs(sy - 0.10), clamp((0.78 - lx) / 0.34, 0.0, 1.0) * 0.26) * step(0.44, lx) * step(lx, 0.78);
                                    arrowM = max(arrowM, max(rArm, rTip));
                                }
                                whiteMask = max(whiteMask, arrowM * 0.78);
                            }
                            // Korean Crosswalk-Ahead Warning Diamond (◇ 횡단보도 예고표시) @ symMod in [30..33.6m]
                            if (symMod > 30.2 && symMod < 33.8 && abs(lx) < 0.65) {
                                float dy = abs(symMod - 32.0) / 1.55;
                                float dx = abs(lx) / 0.52;
                                float dRing = abs((dx + dy) - 1.0);
                                float diam = aaBand(dRing, 0.14) * step(dx + dy, 1.16);
                                whiteMask = max(whiteMask, diam * 0.76);
                            }
                        }
                    }
                }
            } else {
                albedo *= 0.96;
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

        float wPaintA = clamp(whiteMask, 0.0, 1.0) * 0.88 * paintWear;
        float yPaintA = clamp(yellowMask, 0.0, 1.0) * 0.92 * paintWear;
        float bPaintA = clamp(blueMask, 0.0, 1.0) * 0.90 * paintWear;
        albedo = mix(albedo, whitePaint, wPaintA);
        albedo = mix(albedo, yellowPaint, yPaintA);
        albedo = mix(albedo, busBlue, bPaintA);
        roughness = mix(roughness, 0.50, max(max(wPaintA, yPaintA), bPaintA) * 0.55);

        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.16, 0.0, 1.0);
        wetRoadSurface = wetAmt;
        if (wetAmt > 0.02) {
            vec2 warp = vec2(fbm2(p * 0.085 + vec2(3.1, 7.4)), fbm2(p * 0.085 + vec2(11.7, 2.3)));
            float pNoise = fbm2(p * 0.145 + warp * 1.45) * 0.72 + valueNoise(p * 0.52) * 0.28;
            puddleMask = smoothstep(0.55 - wetAmt * 0.17, 0.77 - wetAmt * 0.10, pNoise) * wetAmt;
            albedo *= (1.0 - wetAmt * 0.24 - puddleMask * 0.22);
            roughness = mix(roughness, 0.22, wetAmt * 0.48);
            roughness = mix(roughness, 0.04, puddleMask);
            // Organic 2-layer rotated & jittered Voronoi raindrop ripples ONLY inside deep puddles (ZERO polka-dot grid!)
            if (ubo.localLightCount.y > 1.5 && puddleMask > 0.26 && wetRoadFactor > 0.12 && detailFade > 0.05) {
                vec2 ripN = vec2(0.0);
                // Layer 1 (prime scale 1.73m^-1)
                vec2 uv1 = p * 1.73;
                vec2 c1 = floor(uv1);
                vec2 f1 = fract(uv1) - 0.5;
                vec2 j1 = (vec2(hash12(c1 + 3.1), hash12(c1 + 19.7)) - 0.5) * 0.64;
                vec2 d1 = f1 - j1;
                float s1 = hash12(c1 + 41.3);
                if (s1 > 0.42) {
                    float age1 = fract(animTime * 1.35 + s1 * 11.7);
                    float dist1 = length(d1);
                    float wave1 = sin((dist1 - age1 * 0.34) * 52.0) * exp(-abs(dist1 - age1 * 0.34) * 28.0);
                    float env1 = smoothstep(0.0, 0.08, age1) * (1.0 - age1) * (1.0 - smoothstep(0.18, 0.36, dist1));
                    ripN += normalize(d1 + vec2(1e-4)) * (wave1 * env1);
                }
                // Layer 2 (rotated 37 deg, prime scale 2.81m^-1)
                vec2 pRot = vec2(p.x * 0.7986 - p.y * 0.6018, p.x * 0.6018 + p.y * 0.7986);
                vec2 uv2 = pRot * 2.81 + vec2(13.4, 7.9);
                vec2 c2 = floor(uv2);
                vec2 f2 = fract(uv2) - 0.5;
                vec2 j2 = (vec2(hash12(c2 + 7.3), hash12(c2 + 29.1)) - 0.5) * 0.64;
                vec2 d2 = f2 - j2;
                float s2 = hash12(c2 + 67.9);
                if (s2 > 0.48) {
                    float age2 = fract(animTime * 1.65 + s2 * 13.1);
                    float dist2 = length(d2);
                    float wave2 = sin((dist2 - age2 * 0.32) * 56.0) * exp(-abs(dist2 - age2 * 0.32) * 32.0);
                    float env2 = smoothstep(0.0, 0.08, age2) * (1.0 - age2) * (1.0 - smoothstep(0.16, 0.34, dist2));
                    vec2 n2Rot = normalize(d2 + vec2(1e-4)) * (wave2 * env2 * 0.75);
                    ripN += vec2(n2Rot.x * 0.7986 + n2Rot.y * 0.6018, -n2Rot.x * 0.6018 + n2Rot.y * 0.7986);
                }
                float ripBlend = smoothstep(0.28, 0.72, puddleMask) * smoothstep(0.12, 0.45, wetRoadFactor) * detailFade;
                N = normalize(mix(N, normalize(vec3(ripN.x * 0.055, 1.0, ripN.y * 0.055)), ripBlend));
            }
        }
    }
    else if (matId == 5) {
        // MAT 5: Dedicated 4K Seoul Sidewalks (v6.4.0 Real-World KS Standard!)
        //   - Tile 3: 30x30cm Seoul Standard Shot-Blasted Pavers (서울형 보도블록)
        //   - Tile 4: 60x30cm Flamed Pocheon Granite Flagstones (공개공지 화강석 판석)
        //   - Tile 5: 30x30cm KS F 4561 4-Rib Yellow Tactile Guide Paving (도로 정렬 선형 점자블록)
        //   - Tile 6: 24x100cm Dressed Pocheon Granite Curbstones (화강석 경계석)
        vec2 p = fragPosition.xz;
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 dPdx = dFdx(p);
        vec2 dPdy = dFdy(p);
        vec2 dUvDx = dFdx(fragUv);
        vec2 dUvDy = dFdy(fragUv);
        bool isSidewalkStrip = (fragUv.y > 0.5);
        bool isOcrmSidewalk  = (fragUv.y < -0.5);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;

        // Shared 2.40m world-metric UV (1 / 2.40 = 0.416667): 8x8 -> 30x30cm pavers, 4x8 -> 60x30cm flagstones
        const float kStoneFreq = 0.4166667;
        vec2 stoneUv  = p * kStoneFreq;
        vec2 stoneDdx = dPdx * kStoneFreq;
        vec2 stoneDdy = dPdy * kStoneFreq;
        float urbanPatina = fbm2(p * 0.075);

        if (isSidewalkStrip) {
            if (fragUv.x < 0.11) {
                // Tile 6: Authentic 1m Chisel-Dressed Pocheon Granite Curbstone (화강석 경계석)
                vec2 cUv = vec2(clamp(fragUv.x / 0.11, 0.0, 1.0), fragUv.y * 0.50);
                vec2 cDdx = vec2(dUvDx.x / 0.11, dUvDx.y * 0.50);
                vec2 cDdy = vec2(dUvDy.x / 0.11, dUvDy.y * 0.50);
                sampleStreetTileGrad(6, cUv, cDdx, cDdy, pAlb, pNxy, pR, pAO);
                albedo = pAlb * mix(0.95, 1.03, urbanPatina);
                roughness = mix(pR, 0.50, wetRoadFactor * 0.60);
                surfaceAO = pAO;
                N = perturbNormalGrad(N, dp1, dp2, cDdx, cDdy, pNxy, 0.62 * detailFade);
            } else if (fragUv.x > 0.34 && fragUv.x < 0.46) {
                // Tile 5: Crisp KS F 4561 30x30cm Yellow Tactile Guide Paving (노란 점자블록)
                vec2 tUv = vec2((fragUv.x - 0.34) / 0.12, fragUv.y * 3.3333);
                vec2 tDdx = vec2(dUvDx.x / 0.12, dUvDx.y * 3.3333);
                vec2 tDdy = vec2(dUvDy.x / 0.12, dUvDy.y * 3.3333);
                sampleStreetTileGrad(5, tUv, tDdx, tDdy, pAlb, pNxy, pR, pAO);
                albedo = pAlb;
                roughness = mix(pR, 0.40, wetRoadFactor * 0.55);
                surfaceAO = pAO;
                N = perturbNormalGrad(N, dp1, dp2, tDdx, tDdy, pNxy, 0.82 * detailFade);
            } else {
                sampleStreetTileGrad(3, stoneUv, stoneDdx, stoneDdy, pAlb, pNxy, pR, pAO);
                albedo = pAlb * mix(0.93, 1.03, urbanPatina);
                roughness = mix(pR, 0.52, wetRoadFactor * 0.65);
                surfaceAO = pAO;
                N = perturbNormalGrad(N, dp1, dp2, stoneDdx, stoneDdy, pNxy, 0.56 * detailFade);
            }
        } else if (isOcrmSidewalk) {
            // OCRM v1 Single-Topology Disjoint Sidewalks, Vertical Dressed Granite Curbs & Pure Granite Median Islands!
            float curbM = fragUv.x;
            vec2 wallBase = vec2(fragPosition.x + fragPosition.z, fragPosition.y);
            vec2 dWdx = dFdx(wallBase);
            vec2 dWdy = dFdy(wallBase);

            // Solve exact world-space curb normal & road-parallel tangent from screen-space derivatives!
            // Keeps every 1m granite curbstone & 30cm KS yellow tactile guide rib 100% aligned to curved/diagonal Seoul avenues!
            float duX = dUvDx.x;
            float duY = dUvDy.x;
            float fwCurb = clamp(abs(duX) + abs(duY), 0.004, 0.055);
            float det = dPdx.x * dPdy.y - dPdx.y * dPdy.x;
            vec2 gradCurb = (abs(det) > 1e-8)
                ? vec2(duX * dPdy.y - duY * dPdx.y, dPdx.x * duY - dPdy.x * duX) / det
                : vec2(1.0, 0.0);
            float gradLen = length(gradCurb);
            bool validCurbGrad = (abs(det) > 1e-8) && (gradLen > 0.35) && (gradLen < 1.85);
            vec2 curbN = validCurbGrad ? (gradCurb / gradLen) : vec2(1.0, 0.0);
            vec2 curbT = vec2(-curbN.y, curbN.x);
            float alongM = dot(p, curbT);
            vec2 dAlongDx = vec2(duX, dot(dPdx, curbT));
            vec2 dAlongDy = vec2(duY, dot(dPdy, curbT));

            if (curbM < 0.0) {
                bool isVertCurb = (abs(N.y) < 0.65);
                if (isVertCurb) {
                    // Vertical dressed Pocheon granite curbstone face
                    vec2 cUv  = vec2(wallBase.y * 2.4, wallBase.x * 0.50);
                    vec2 cDdx = vec2(dWdx.y * 2.4, dWdx.x * 0.50);
                    vec2 cDdy = vec2(dWdy.y * 2.4, dWdy.x * 0.50);
                    sampleStreetTileGrad(6, cUv, cDdx, cDdy, pAlb, pNxy, pR, pAO);
                    albedo = pAlb * 0.90;
                    roughness = mix(pR, 0.50, wetRoadFactor * 0.60);
                    surfaceAO = pAO;
                    N = perturbNormalGrad(N, dp1, dp2, cDdx, cDdy, pNxy, 0.62 * detailFade);
                } else {
                    // Enclosed central boulevard median island: clean honed Seoul Pocheon granite slabs (ZERO noisy bricks!)
                    sampleStreetTileGrad(4, stoneUv, stoneDdx, stoneDdy, pAlb, pNxy, pR, pAO);
                    albedo = pAlb * mix(0.92, 1.02, urbanPatina);
                    roughness = mix(pR, 0.50, wetRoadFactor * 0.60);
                    surfaceAO = pAO;
                    N = perturbNormalGrad(N, dp1, dp2, stoneDdx, stoneDdy, pNxy, 0.60 * detailFade);
                }
            } else if (curbM < 0.24) {
                // Dressed Pocheon granite curbstone top coping (exact 24cm Seoul curb width, 1.00m block joints along road!)
                vec2 cUv  = vec2(clamp(curbM / 0.24, 0.0, 1.0), alongM * 0.50);
                vec2 cDdx = vec2(dAlongDx.x / 0.24, dAlongDx.y * 0.50);
                vec2 cDdy = vec2(dAlongDy.x / 0.24, dAlongDy.y * 0.50);
                sampleStreetTileGrad(6, cUv, cDdx, cDdy, pAlb, pNxy, pR, pAO);
                albedo = pAlb * mix(0.95, 1.03, urbanPatina);
                roughness = mix(pR, 0.50, wetRoadFactor * 0.60);
                surfaceAO = pAO;
                N = perturbNormalGrad(N, dp1, dp2, cDdx, cDdy, pNxy, 0.64 * detailFade);
            } else if (curbM >= 1.45 && curbM <= 1.75 && validCurbGrad) {
                // Authentic Korean KS F 4561 30.0cm Yellow Tactile Guide Paving (노란 선형 점자블록 300x300mm)
                // Positioned 1.45..1.75m from curb (completely clear of guardrails & streetlights!), 4 raised bars parallel to street!
                vec2 tUv  = vec2((curbM - 1.45) / 0.30, alongM / 0.30);
                vec2 tDdx = vec2(dAlongDx.x / 0.30, dAlongDx.y / 0.30);
                vec2 tDdy = vec2(dAlongDy.x / 0.30, dAlongDy.y / 0.30);
                sampleStreetTileGrad(5, tUv, tDdx, tDdy, pAlb, pNxy, pR, pAO);
                vec3 swAlb; vec2 swNxy; float swR; float swAO;
                sampleStreetTileGrad(3, stoneUv, stoneDdx, stoneDdy, swAlb, swNxy, swR, swAO);
                float edgeM = min(curbM - 1.45, 1.75 - curbM);
                float tacMask = smoothstep(0.0, fwCurb * 1.25, edgeM);
                albedo = mix(swAlb * mix(0.93, 1.03, urbanPatina), pAlb, tacMask);
                roughness = mix(mix(swR, pR, tacMask), 0.42, wetRoadFactor * 0.55);
                surfaceAO = mix(swAO, pAO, tacMask);
                N = perturbNormalGrad(N, dp1, dp2, tDdx, tDdy, mix(swNxy, pNxy, tacMask), mix(0.62, 0.86, tacMask) * detailFade);
            } else if (curbM >= 3.25 && curbM < 3.45) {
                // Authentic Seoul 20cm dressed granite divider band (경계띠석) framing the public-plaza boundary!
                vec2 bUv  = vec2(clamp((curbM - 3.25) / 0.20, 0.0, 1.0), alongM * 0.50);
                vec2 bDdx = vec2(dAlongDx.x / 0.20, dAlongDx.y * 0.50);
                vec2 bDdy = vec2(dAlongDy.x / 0.20, dAlongDy.y * 0.50);
                sampleStreetTileGrad(6, bUv, bDdx, bDdy, pAlb, pNxy, pR, pAO);
                albedo = pAlb * mix(0.93, 1.01, urbanPatina);
                roughness = mix(pR, 0.50, wetRoadFactor * 0.60);
                surfaceAO = pAO;
                N = perturbNormalGrad(N, dp1, dp2, bDdx, bDdy, pNxy, 0.60 * detailFade);
            } else if (curbM >= 3.45) {
                // Tile 4: 60x30cm Flamed Pocheon Granite Building-Front Public Plaza Flagstones
                // EXACT SAME UV scale, slab tone & shading as groundMesh below -> 100% SEAMLESS sidewalk-to-plaza continuity!
                sampleStreetTileGrad(4, stoneUv, stoneDdx, stoneDdy, pAlb, pNxy, pR, pAO);
                float slabVar = mix(0.92, 1.04, hash12(floor(p * vec2(1.6667, 3.3333))));
                albedo = pAlb * mix(0.81, 0.91, urbanPatina) * slabVar;
                roughness = mix(pR, 0.48, wetRoadFactor * 0.65);
                surfaceAO = pAO * mix(0.86, 1.0, pAO);
                N = perturbNormalGrad(N, dp1, dp2, stoneDdx, stoneDdy, pNxy, 0.72 * detailFade);
            } else {
                // Tile 3: Authentic Seoul Standard 30x30cm Shot-Blasted Granite-Concrete Sidewalk Pavers!
                sampleStreetTileGrad(3, stoneUv, stoneDdx, stoneDdy, pAlb, pNxy, pR, pAO);
                float paverVar = mix(0.93, 1.03, hash12(floor(p * 3.3333)));
                albedo = pAlb * mix(0.84, 0.94, urbanPatina) * paverVar;
                roughness = mix(pR, 0.50, wetRoadFactor * 0.65);
                surfaceAO = pAO * mix(0.88, 1.0, pAO);
                N = perturbNormalGrad(N, dp1, dp2, stoneDdx, stoneDdy, pNxy, 0.72 * detailFade);
            }
        } else {
            // Urban sidewalk-to-building plaza subbase: EXACT SAME Tile 4 60x30cm Pocheon Granite Flagstones!
            // Zero UV phase jump, zero brightness step across the OCRM sidewalk-to-lot boundary!
            sampleStreetTileGrad(4, stoneUv, stoneDdx, stoneDdy, pAlb, pNxy, pR, pAO);
            float slabVar = mix(0.92, 1.04, hash12(floor(p * vec2(1.6667, 3.3333))));
            albedo = pAlb * mix(0.81, 0.91, urbanPatina) * slabVar;
            roughness = mix(pR, 0.48, wetRoadFactor * 0.65);
            surfaceAO = pAO * mix(0.86, 1.0, pAO);
            N = perturbNormalGrad(N, dp1, dp2, stoneDdx, stoneDdy, pNxy, 0.72 * detailFade);
        }
        puddleMask = wetRoadFactor * smoothstep(0.45, 0.78, fbm2(p * 0.22)) * 0.48;
        albedo *= (1.0 - wetRoadFactor * 0.20 - puddleMask * 0.18);
        roughness = mix(roughness, 0.07, puddleMask);
    }
    else if (matId == 2 || matId == 3 || matId == 4 || matId == 16 || matId == 18) {
        // Real 4096x4096 PBR Architectural Building Facades + 3D Interior Parallax Mapping + Real OCM4 RGB & Variety!
        // Opaque buildings always output alpha = 1.0; vertex alpha carries OCM4 architectural metadata byte!
        int varByte = int(floor(srgbBase.a * 255.0 + 0.5));
        bool isOcm4Meta = (varByte < 255);
        bool metaHasRetail1F = isOcm4Meta ? ((varByte & 128) != 0) : (matId == 2 || matId == 4 || matId == 18);
        bool metaDarkMetal   = isOcm4Meta ? ((varByte & 64) != 0) : false;
        float archSeed = isOcm4Meta
            ? (float(varByte & 63) / 63.0)
            : fract(dot(floor(fragPosition.xz * 0.045), vec2(12.9898, 78.233)));
        alphaOut = 1.0;

        if (abs(N.y) < 0.45) {
            // Real-OSM polygons pass exact wall-metric UV (u = horizontal meters, v = vertical height meters)!
            vec2 wallM = (dot(fragUv, fragUv) > 0.0001)
                ? fragUv
                : vec2((abs(N.x) > 0.5) ? fragPosition.z : fragPosition.x, fragPosition.y);

            bool isGlassTower = (matId == 3);
            bool isKoreaApt   = (matId == 16);
            bool isGroundFlr  = (wallM.y < 3.95) && metaHasRetail1F;

            // Per-building bay width & floor height rhythm so neighboring Seoul buildings never look like clones!
            float uJitter = mix(0.88, 1.14, archSeed);
            float vJitter = mix(0.92, 1.09, fract(archSeed * 3.73));

            int tileId = 4;
            vec2 tileScale = vec2(0.11, 0.11);
            if (isGroundFlr) {
                tileId = 7; // Korean 1F Commercial Storefront (카페/편의점/식당 통유리!)
                tileScale = vec2(0.125 * mix(0.92, 1.10, archSeed), 0.254);
            } else if (isKoreaApt) {
                tileId = 2; // Korean High-Rise Apartment Facade (래미안/자이/힐스테이트)
                tileScale = vec2(0.095 * uJitter, 0.105 * vJitter);
            } else if (isGlassTower) {
                tileId = 3; // Glass & Dark Metallic Curtain-Wall Skyscraper
                tileScale = vec2(0.105 * uJitter, 0.096 * vJitter);
            } else if (matId == 4) {
                tileId = 4; // Honed Granite / Limestone Corporate Office
                tileScale = vec2(0.115 * uJitter, 0.106 * vJitter);
            } else if (matId == 2) {
                tileId = 5; // Brick Commercial / Korean Multi-Family Villa
                tileScale = vec2(0.132 * uJitter, 0.120 * vJitter);
            } else {
                tileId = 6; // Modern Commercial Stone-Tile / Stucco
                tileScale = vec2(0.132 * uJitter, 0.118 * vJitter);
            }

            vec2 pbrUv = wallM * tileScale;
            vec3 pAlb; vec2 pNxy; float pR; float pAO;
            samplePbrTile(tileId, pbrUv, pAlb, pNxy, pR, pAO);

            // Detect reflective architectural glass windows cleanly via baked 4K PBR roughness < 0.145!
            bool pbrWin = (pR < 0.145);
            // Architectural 1F anodized aluminium transom bar & lower kickplate plinth for crisp close-up retail framing
            bool is1FTransom = isGroundFlr && (abs(wallM.y - 2.72) < 0.075 || wallM.y < 0.34);
            if (pbrWin && !is1FTransom) {
                isWindowPane = true;
                isGlassTowerWin = isGlassTower;

                // -----------------------------------------------------------------
                // TRUE 3D INTERIOR PARALLAX MAPPING BEHIND ARCHITECTURAL GLASS!
                // -----------------------------------------------------------------
                vec2 roomFreq = isGroundFlr ? vec2(0.28, 0.26) : (isGlassTower ? vec2(0.40, 0.28) : vec2(0.35, 0.30));
                vec2 roomCoord = floor(wallM * roomFreq);
                vec2 rPos2 = fract(wallM * roomFreq);

                // Floor-clustered Seoul office occupancy (whole office floors glow together at night!)
                float bldKey = floor(archSeed * 63.0 + 0.5);
                float floorSeed = hash12(vec2(roomCoord.y + bldKey * 7.1, floor(fragPosition.x * 0.025) + floor(fragPosition.z * 0.025)));
                float roomSeed  = hash12(roomCoord + vec2(bldKey * 13.7, floor(fragPosition.x * 0.03) - floor(fragPosition.z * 0.03)));

                float depthAO = 0.62;
                vec3 intRoomBase = vec3(0.08, 0.09, 0.11);
                float ceilLightBar = 0.20;
                float blindMask = 0.0;
                float retailDetail = 0.72;

                if (ubo.localLightCount.y > 1.5) {
                    vec3 T_w = normalize(cross(N, vec3(0.0, 1.0, 0.0)));
                    vec3 rayW = -V;
                    vec3 rDir = vec3(dot(rayW, T_w), rayW.y, max(dot(rayW, -N), 0.08));
                    float roomDepth = isGroundFlr ? 1.15 : 0.72;
                    float tBack = roomDepth / rDir.z;
                    float tX = (abs(rDir.x) > 0.001) ? (((rDir.x > 0.0) ? (1.0 - rPos2.x) : rPos2.x) / abs(rDir.x)) : 99.0;
                    float tY = (abs(rDir.y) > 0.001) ? (((rDir.y > 0.0) ? (1.0 - rPos2.y) : rPos2.y) / abs(rDir.y)) : 99.0;
                    float tHit = min(tBack, min(tX, tY));
                    vec3 hitP = vec3(rPos2, 0.0) + rDir * tHit;

                    // Interior surface shading: ceiling slab + linear LED / pendant lights, back wall shelving/counter, side walls, floor
                    depthAO = exp(-hitP.z * (isGroundFlr ? 1.75 : 1.45));
                    intRoomBase = vec3(0.09, 0.10, 0.12);
                    ceilLightBar = 0.0;
                    if (tY <= tX && tY <= tBack) {
                        if (rDir.y > 0.0) {
                            // Office / retail ceiling with crisp recessed LED light strips or cafe downlight spots
                            intRoomBase = vec3(0.16, 0.17, 0.19);
                            if (isGroundFlr) {
                                vec2 spotCell = fract(vec2(hitP.x * 2.0, hitP.z * 2.2)) - 0.5;
                                float spotD = length(spotCell);
                                ceilLightBar = smoothstep(0.24, 0.07, spotD) * 1.15;
                            } else {
                                float barX = 1.0 - smoothstep(0.18, 0.30, abs(hitP.x - 0.5));
                                float barZ = smoothstep(0.12, 0.24, fract(hitP.z * 2.6)) * (1.0 - smoothstep(0.68, 0.80, fract(hitP.z * 2.6)));
                                ceilLightBar = barX * barZ;
                            }
                        } else {
                            // Darker interior floor slab (wood/tile reflection inside retail shops)
                            intRoomBase = isGroundFlr ? vec3(0.045, 0.042, 0.040) : vec3(0.055, 0.058, 0.064);
                            retailDetail = 0.38;
                        }
                    } else if (tBack <= tX) {
                        if (isGroundFlr) {
                            // Authentic Korean 1F shop interior: lower counter (0..0.30), display shelves (0.30..0.72), upper menu/wall (0.72..1.0)
                            float counterMask = 1.0 - smoothstep(0.26, 0.31, hitP.y);
                            float shelfLines  = smoothstep(0.32, 0.36, hitP.y) * (1.0 - smoothstep(0.70, 0.74, hitP.y)) *
                                                (0.55 + 0.45 * smoothstep(0.12, 0.28, fract(hitP.y * 11.0)));
                            float goodsVar    = 0.72 + 0.28 * hash12(floor(vec2(hitP.x * 9.0, hitP.y * 11.0)) + roomCoord);
                            float upperWall   = smoothstep(0.72, 0.76, hitP.y);
                            vec3 counterCol   = vec3(0.042, 0.038, 0.036);
                            vec3 shelfCol     = vec3(0.125, 0.118, 0.112) * goodsVar * (0.65 + 0.35 * shelfLines);
                            vec3 menuCol      = vec3(0.085, 0.088, 0.095);
                            intRoomBase = mix(mix(shelfCol, counterCol, counterMask), menuCol, upperWall) * depthAO;
                            retailDetail = mix(mix(0.88 * goodsVar, 0.26, counterMask), 0.54, upperWall);
                        } else {
                            // Back interior wall with subtle desk/partition band
                            float deskBand = smoothstep(0.28, 0.36, hitP.y);
                            intRoomBase = mix(vec3(0.06, 0.065, 0.075), vec3(0.14, 0.145, 0.155), deskBand) * depthAO;
                        }
                    } else {
                        // Side partition walls (subtle vertical paneling in retail bays)
                        intRoomBase = vec3(0.085, 0.090, 0.100) * depthAO;
                        retailDetail = 0.48;
                    }

                    // Subtle partially-lowered office Venetian blinds on random windows (authentic Teheran-ro detail!)
                    float blindDrop = (roomSeed > 0.45 && !isGroundFlr) ? (0.18 + 0.36 * fract(roomSeed * 5.7)) : 0.0;
                    blindMask = (rPos2.y > (1.0 - blindDrop)) ? 1.0 : 0.0;
                    if (blindMask > 0.5) {
                        float slat = 0.82 + 0.18 * sin(rPos2.y * 58.0);
                        intRoomBase = mix(intRoomBase, vec3(0.24, 0.25, 0.27) * slat, 0.72);
                    }
                }

                // Authentic Low-E architectural glass tint & reflection coating per building
                // (Sapphire-blue, emerald-teal, dark smoked charcoal, warm bronze, crisp silver-mirror!)
                vec3 glassTint;
                if (metaDarkMetal) {
                    float bronzeMix = fract(archSeed * 2.3);
                    glassTint = mix(vec3(0.014, 0.018, 0.026), vec3(0.036, 0.026, 0.018), bronzeMix);
                    windowReflTint = mix(vec3(0.36, 0.42, 0.52), vec3(0.84, 0.66, 0.46), bronzeMix);
                } else if (isGlassTower) {
                    vec3 blueLowE   = vec3(0.016, 0.052, 0.105);
                    vec3 tealLowE   = vec3(0.014, 0.068, 0.072);
                    vec3 silverLowE = vec3(0.034, 0.048, 0.068);
                    glassTint = (archSeed < 0.42) ? mix(blueLowE, tealLowE, archSeed / 0.42)
                                                  : mix(tealLowE, silverLowE, (archSeed - 0.42) / 0.58);
                    glassTint = mix(glassTint, glassTint * (albedo * 1.40 + vec3(0.20)), 0.40);
                    vec3 blueRefl   = vec3(0.40, 0.72, 1.08);
                    vec3 tealRefl   = vec3(0.36, 0.84, 0.78);
                    vec3 silverRefl = vec3(0.70, 0.84, 0.98);
                    windowReflTint = (archSeed < 0.42) ? mix(blueRefl, tealRefl, archSeed / 0.42)
                                                       : mix(tealRefl, silverRefl, (archSeed - 0.42) / 0.58);
                } else if (isGroundFlr) {
                    glassTint = vec3(0.010, 0.016, 0.024);
                    windowReflTint = vec3(0.48, 0.58, 0.70);
                } else {
                    glassTint = mix(vec3(0.016, 0.026, 0.040), vec3(0.026, 0.036, 0.048), archSeed);
                    windowReflTint = mix(vec3(0.56, 0.70, 0.86), vec3(0.68, 0.78, 0.88), archSeed);
                }

                // Physically linear daytime glass: deep sleek tempered glass with crisp 3D parallax interior depth
                albedo = glassTint + intRoomBase * (isGroundFlr ? 0.18 : 0.10);
                roughness = metaDarkMetal ? 0.026 : (isGlassTower ? 0.032 : (isGroundFlr ? 0.038 : 0.048));
                metallic  = metaDarkMetal ? 0.54  : (isGlassTower ? 0.44  : 0.26);

                // Realistic Dusk / Night Interior Illumination (calibrated so 1F shopfronts show 3D depth — NEVER blinding flat white slabs!)
                float duskUpper = smoothstep(0.46, 0.84, nightFactor);
                float dusk1F    = smoothstep(0.28, 0.78, nightFactor);
                bool floorActive = isGlassTower ? (floorSeed > 0.36) : (floorSeed > 0.26);
                float roomLitProb = isGroundFlr ? 0.68 : (floorActive ? (isGlassTower ? 0.74 : 0.58) : 0.09);

                if (roomSeed > (1.0 - roomLitProb)) {
                    vec3 warmHome   = pow(vec3(1.00, 0.78, 0.46), vec3(2.2)); // 2900K Korean cafe / apartment warm glow
                    vec3 crispOff   = pow(vec3(0.84, 0.92, 1.00), vec3(2.2)); // 5000K Korean office / convenience store LED
                    vec3 neutralOff = pow(vec3(0.96, 0.87, 0.70), vec3(2.2)); // 3800K warm-white bistro / boutique
                    vec3 intCol = isKoreaApt
                        ? mix(warmHome, neutralOff, step(0.74, roomSeed))
                        : (isGlassTower ? mix(crispOff, neutralOff, step(0.62, roomSeed))
                                        : (isGroundFlr
                                            ? mix(mix(warmHome, neutralOff, step(0.56, roomSeed)), crispOff, step(0.82, roomSeed))
                                            : mix(warmHome, crispOff, step(0.70, roomSeed))));

                    if (isGroundFlr) {
                        // Balanced glass transmission + parallax interior contrast + warm ceiling spotlights (ZERO ACES white-out!)
                        float ndotvGlass = clamp(dot(N, V), 0.0, 1.0);
                        float glassTransmit = mix(0.58, 1.0, pow(ndotvGlass, 0.55));
                        float bayDim = mix(0.55, 1.0, fract(roomSeed * 7.31));
                        float interiorPat = (0.18 + 0.52 * depthAO * retailDetail + ceilLightBar * 0.78) * bayDim * glassTransmit;
                        float glowAmt = mix(0.02, 0.72, dusk1F);
                        if (glowAmt > 0.005) {
                            emissiveColor = intCol * interiorPat;
                            emissive = max(emissive, glowAmt);
                        }
                    } else {
                        float interiorPat = (0.34 + 0.66 * depthAO + ceilLightBar * 1.15) * (1.0 - blindMask * 0.55);
                        float glowAmt = duskUpper * (isGlassTower ? 1.12 : 0.96);
                        if (glowAmt > 0.005) {
                            emissiveColor = intCol * interiorPat;
                            emissive = max(emissive, glowAmt);
                        }
                    }
                }
            } else if (is1FTransom) {
                // Crisp dark-anodized aluminium storefront transom bar & lower granite/metal kickplate
                albedo = (wallM.y < 0.34) ? vec3(0.045, 0.048, 0.054) : vec3(0.024, 0.027, 0.032);
                roughness = (wallM.y < 0.34) ? 0.52 : 0.28;
                metallic = (wallM.y < 0.34) ? 0.14 : 0.62;
                surfaceAO = 0.85;
            } else {
                // Opaque Architectural Cladding / Masonry / Honed Granite / Anodized Mullions:
                // Preserve full 4K micro-detail while cleanly honoring each real building's 24-bit OCM4 facade RGB!
                float pLum = dot(pAlb, vec3(0.299, 0.587, 0.114));
                vec3 normTex = pAlb / max(pLum, 0.08);
                if (metaDarkMetal && isGlassTower) {
                    // Brushed dark charcoal / bronze / titanium skyscraper structural mullions & spandrel panels
                    albedo = mix(albedo * 0.72, pAlb * (albedo * 1.45 + vec3(0.08)), 0.52);
                    roughness = mix(0.24, pR, 0.45);
                    metallic = 0.68;
                } else if (matId == 2) {
                    // Brick / Villa: keep rich red/terracotta mortar contrast while shifting tone to building's real RGB
                    albedo = pAlb * mix(vec3(1.0), clamp(albedo * 2.05 + vec3(0.14), 0.42, 1.42), 0.68);
                    roughness = pR;
                    metallic = 0.04;
                } else {
                    // Korean Apartments, Honed Granite Offices, Limestone Commercial, Civic: true per-building RGB!
                    float detailScale = clamp(pLum * 1.85, 0.35, 1.38);
                    vec3 tintedClad = albedo * detailScale * mix(vec3(1.0), normTex, 0.35);
                    albedo = mix(pAlb, tintedClad, 0.78);
                    // Modern Seoul apartment 1F-3F dark granite podium base (래미안/자이/힐스테이트 석재 기단부!)
                    if (isKoreaApt && wallM.y < 8.4) {
                        float baseBlend = 1.0 - smoothstep(7.8, 8.5, wallM.y);
                        albedo = mix(albedo, albedo * vec3(0.58, 0.59, 0.62), baseBlend * 0.75);
                    }
                    roughness = pR;
                    metallic = isGlassTower ? 0.46 : (metaDarkMetal ? 0.42 : 0.06);
                }
                surfaceAO = pAO;
                N = perturbNormal(N, fragPosition, pbrUv, pNxy, 0.92 * detailFade);
            }
            // Ground-level urban street contact ambient occlusion (subtle & natural)
            albedo *= mix(0.76, 1.0, smoothstep(0.0, 2.2, wallM.y));
        } else {
            // Architectural Rooftop (Flat Concrete / Korean Green Urethane OR Real 3D Sloped Roofs!)
            vec2 roofUv = fragPosition.xz * 0.095;
            vec3 pAlb; vec2 pNxy; float pR; float pAO;
            samplePbrTile(10, roofUv, pAlb, pNxy, pR, pAO);
            bool isSlopedRoof = (N.y < 0.96);
            if (isSlopedRoof) {
                // Real 3D gabled/hipped/pyramidal/dome/skillion roof material (slate charcoal, terracotta, or standing-seam metal!)
                vec3 slateCol = pow(vec3(0.24, 0.26, 0.30), vec3(2.2));
                vec3 terraCol = pow(vec3(0.50, 0.26, 0.19), vec3(2.2));
                vec3 metalCol = pow(vec3(0.36, 0.40, 0.45), vec3(2.2));
                vec3 rTint = (matId == 2) ? mix(slateCol, terraCol, step(0.42, archSeed))
                                          : ((matId == 3 || metaDarkMetal) ? metalCol : mix(slateCol, albedo * 0.55, 0.35));
                albedo = rTint * (0.72 + pAlb * 0.85);
                roughness = (matId == 3 || metaDarkMetal) ? 0.36 : 0.62;
                metallic = (matId == 3 || metaDarkMetal) ? 0.48 : 0.06;
            } else if (matId == 2) {
                // Signature Korean green waterproof urethane flat roof (옥상 초록 우레탄 방수!)
                vec3 krRoofGreen = pow(vec3(0.23, 0.45, 0.33), vec3(2.2));
                albedo = mix(pAlb, krRoofGreen * (0.76 + pAlb * 0.68), 0.72);
                roughness = 0.54;
                metallic = 0.04;
            } else {
                albedo = pAlb * mix(0.76, 1.08, 0.5);
                roughness = pR;
                metallic = 0.05;
            }
            surfaceAO = pAO;
            N = perturbNormal(N, fragPosition, roofUv, pNxy, 0.68 * detailFade);
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
        vec2 sDdx = clamp(dFdx(fragUv), vec2(-0.04), vec2(0.04));
        vec2 sDdy = clamp(dFdy(fragUv), vec2(-0.04), vec2(0.04));
        vec4 tex = textureGrad(signAtlasTex, fragUv, sDdx, sDdy);
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
        // MAT 20: Dedicated 4K Street PBR — Poured Formwork Viaduct/Pier Concrete (Tile 9) OR Translucent Acoustic Sound Barrier (Tile 11)!
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 baseU = vec2(fragPosition.x + fragPosition.z, fragPosition.y);
        vec2 dUdx = dFdx(baseU);
        vec2 dUdy = dFdy(baseU);
        bool isSoundGlass = (srgbBase.b > srgbBase.r + 0.10) && (srgbBase.b > 0.62);
        bool isAntiGlare = (srgbBase.g > srgbBase.r + 0.18) && (srgbBase.g > srgbBase.b + 0.08);
        float cScale = isSoundGlass ? 0.36 : 0.22;
        vec2 cUv = baseU * cScale;
        vec2 cDdx = dUdx * cScale;
        vec2 cDdy = dUdy * cScale;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        sampleStreetTileGrad(isSoundGlass ? 11 : 9, cUv, cDdx, cDdy, pAlb, pNxy, pR, pAO);
        if (isSoundGlass) {
            albedo = pAlb * 1.05;
            roughness = pR;
            metallic = (pR < 0.20) ? 0.18 : 0.65;
            surfaceAO = pAO;
            N = perturbNormalGrad(N, dp1, dp2, cDdx, cDdy, pNxy, 0.75 * detailFade);
        } else if (isAntiGlare) {
            albedo = albedo * mix(0.84, 1.10, pAO);
            roughness = 0.56;
            metallic = 0.08;
            surfaceAO = pAO;
        } else {
            albedo = pAlb * mix(vec3(1.0), albedo * 1.38, 0.36);
            roughness = pR;
            surfaceAO = pAO;
            N = perturbNormalGrad(N, dp1, dp2, cDdx, cDdy, pNxy, 0.85 * detailFade);
        }
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
    else if (matId == 22) {
        // MAT 22: Traditional Korean Heritage Stone / Hanok Timber-Plaster-Hanji Facades / Monument Pedestals
        // Explicit uniform-flow derivatives; when fragUv carries wall-metric coords (extruded OSM Hanoks), articulate
        // authentic Korean Hanok timber pillars (기둥), lintels (인방), granite plinth (기단) & Hanji lattice windows (창호문)!
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 baseU = vec2(fragPosition.x + fragPosition.z, fragPosition.y) * 0.28;
        vec2 dUdx = dFdx(baseU);
        vec2 dUdy = dFdy(baseU);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        sampleStreetTileGrad(4, baseU, dUdx, dUdy, pAlb, pNxy, pR, pAO);
        float stoneLuma = dot(pAlb, vec3(0.30, 0.59, 0.11));
        bool isHanokWall = (abs(N.y) < 0.45) && (dot(fragUv, fragUv) > 0.0001);
        if (isHanokWall) {
            alphaOut = 1.0;
            vec2 hm = fragUv;
            float bayW = 2.85;
            float bayIdx = floor(hm.x / bayW);
            float bayU = fract(hm.x / bayW);
            float flrH = 3.15;
            float flrY = mod(hm.y, flrH);
            float hSeed = hash12(vec2(bayIdx + floor(fragPosition.x * 0.04), floor(hm.y / flrH) + floor(fragPosition.z * 0.04)));

            vec3 timberCol  = pow(vec3(0.24, 0.15, 0.10), vec3(2.2)); // Traditional dark pine / walnut timber (기둥/인방)
            vec3 plasterCol = pow(vec3(0.84, 0.80, 0.72), vec3(2.2)); // Warm Korean lime plaster (회벽)
            vec3 stoneBase  = pow(vec3(0.46, 0.45, 0.43), vec3(2.2)); // Granite base masonry (기단)
            vec3 hanjiDay   = pow(vec3(0.86, 0.81, 0.70), vec3(2.2)); // Warm mulberry paper (한지 창호)

            bool isPillar   = (bayU < 0.075 || bayU > 0.925);
            bool isLintel   = (abs(flrY - 0.88) < 0.075 || abs(flrY - 2.48) < 0.085 || flrY > 3.00);
            bool isBase     = (hm.y < 0.86);
            bool hasWin     = (hSeed > 0.34) && (bayU > 0.22 && bayU < 0.78) && (flrY > 1.05 && flrY < 2.32);

            if (isBase && !isPillar) {
                albedo = stoneBase * mix(0.80, 1.18, clamp(stoneLuma * 2.2, 0.0, 1.0));
                roughness = clamp(pR * 0.95 + 0.08, 0.55, 0.90);
                surfaceAO = mix(1.0, pAO, 0.72);
                N = perturbNormalGrad(N, dp1, dp2, dUdx, dUdy, pNxy, 0.72 * detailFade);
            } else if (isPillar || isLintel) {
                float grain = 0.88 + 0.24 * valueNoise(hm * vec2(2.5, 18.0));
                albedo = timberCol * grain;
                roughness = 0.68;
                surfaceAO = 0.86;
            } else if (hasWin) {
                // Korean traditional Changho wooden grid lattice (세살창호) over warm Hanji paper
                vec2 wUv = vec2((bayU - 0.22) / 0.56, (flrY - 1.05) / 1.27);
                vec2 gridF = fract(wUv * vec2(6.0, 8.0));
                bool isMuntin = (wUv.x < 0.06 || wUv.x > 0.94 || wUv.y < 0.06 || wUv.y > 0.94 ||
                                 gridF.x < 0.13 || gridF.y < 0.13);
                if (isMuntin) {
                    albedo = timberCol * 1.12;
                    roughness = 0.64;
                } else {
                    albedo = hanjiDay;
                    roughness = 0.78;
                    float duskHanji = smoothstep(0.28, 0.80, nightFactor);
                    if (duskHanji > 0.01 && hSeed > 0.48) {
                        emissiveColor = pow(vec3(1.00, 0.76, 0.42), vec3(2.2)) * (0.75 + 0.25 * (1.0 - abs(wUv.y - 0.5)));
                        emissive = max(emissive, duskHanji * 0.58);
                    }
                }
            } else {
                float plasterTex = 0.90 + 0.16 * valueNoise(hm * 3.8);
                albedo = plasterCol * plasterTex * mix(0.92, 1.06, clamp(stoneLuma * 2.0, 0.0, 1.0));
                roughness = 0.86;
                surfaceAO = 0.94;
                N = perturbNormalGrad(N, dp1, dp2, dUdx, dUdy, pNxy, 0.28 * detailFade);
            }
            albedo *= mix(0.78, 1.0, smoothstep(0.0, 1.8, hm.y));
            metallic = 0.0;
        } else {
            albedo = albedo * mix(0.84, 1.14, clamp(stoneLuma * 2.2, 0.0, 1.0));
            roughness = clamp(pR * 0.95 + 0.06, 0.42, 0.86);
            metallic = 0.02;
            surfaceAO = mix(1.0, pAO, 0.65);
            N = perturbNormalGrad(N, dp1, dp2, dUdx, dUdy, pNxy, 0.65 * detailFade);
        }
    }
    else if (matId == 23) {
        // MAT 23: OCRM v1 Retroreflective Korean Thermoplastic Road Markings (Yellow Centerline, White Dashed, Blue Bus Lane, Zebra Crosswalks)
        vec2 p = fragPosition.xz;
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 dPdx = dFdx(p);
        vec2 dPdy = dFdy(p);
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        sampleStreetTileGrad(0, p * 0.30, dPdx * 0.30, dPdy * 0.30, pAlb, pNxy, pR, pAO);
        float paintWear = mix(0.84, 1.0, smoothstep(0.15, 0.74, valueNoise(p * 3.6)));
        albedo = mix(pAlb * 1.08, albedo * 0.96, paintWear * 0.92);
        roughness = mix(0.46, 0.22, wetRoadFactor * 0.72);
        metallic = 0.02;
        surfaceAO = mix(1.0, pAO, 0.45);
        N = perturbNormalGrad(N, dp1, dp2, dPdx * 0.30, dPdy * 0.30, pNxy, 0.32 * detailFade);
        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.16, 0.0, 1.0);
        wetRoadSurface = wetAmt * 0.75;
        if (wetAmt > 0.02) {
            albedo *= (1.0 - wetAmt * 0.18);
            roughness = mix(roughness, 0.14, wetAmt * 0.65);
        }
    }
    else if (matId == 24) {
        // MAT 24: OCRM v1 Single-Topology Asphalt Polygons (Tile 0 Urban / Tile 1 Dark Expressway SMA / Tile 2 Worn Intersection)
        // ZERO overlapping road ribbons, ZERO Z-fighting, plus full organic 2-layer rotated Voronoi rain puddles & reflections!
        vec2 p = fragPosition.xz;
        vec3 dp1 = dFdx(fragPosition);
        vec3 dp2 = dFdy(fragPosition);
        vec2 dPdx = dFdx(p);
        vec2 dPdy = dFdy(p);
        int streetTile = (srgbBase.r < 0.35) ? 1 : ((srgbBase.r > 0.65) ? 2 : 0);
        vec2 roadUv = p * 0.26;
        vec3 pAlb; vec2 pNxy; float pR; float pAO;
        sampleStreetTileGrad(streetTile, roadUv, dPdx * 0.26, dPdy * 0.26, pAlb, pNxy, pR, pAO);

        float macroPatina = fbm2(p * 0.045) * 0.55 + fbm2(p * 0.14) * 0.45;
        if (streetTile == 1) {
            // Sleek dark Korean expressway SMA (경부고속도로 · 올림픽대로 · 강변북로 · 내부순환로)
            albedo = (pAlb * 1.12 + vec3(0.004, 0.004, 0.005)) * mix(0.90, 1.12, macroPatina);
            roughness = mix(pR, 0.44, 0.24);
        } else if (streetTile == 2) {
            // Worn multi-directional Seoul urban intersection asphalt (강남역 사거리 등)
            albedo = (pAlb * 1.04 + vec3(0.006, 0.006, 0.008)) * mix(0.90, 1.12, macroPatina);
            roughness = mix(pR, 0.48, 0.24);
        } else {
            // Weathered Seoul urban crushed-granite aggregate asphalt + subtle alley/street micro-detail
            float microGrain = valueNoise(p * 11.5) * 0.10 - 0.05;
            albedo = (pAlb * 1.08 + vec3(0.008, 0.008, 0.010)) * mix(0.86, 1.15, macroPatina) * (1.0 + microGrain);
            roughness = mix(pR, 0.56, 0.22);
            // Subtle dark bitumen tar-sealant crack veins on worn urban streets & alleys
            float crackF = abs(fbm2(p * 0.38 + vec2(4.2, 9.1)) - 0.50);
            float crackMask = (1.0 - smoothstep(0.004, 0.016, crackF)) * smoothstep(0.54, 0.72, macroPatina) * detailFade * 0.55;
            albedo = mix(albedo, vec3(0.012, 0.013, 0.015), crackMask);
            roughness = mix(roughness, 0.28, crackMask);
            // Authentic Seoul cast-iron circular manhole covers (맨홀 뚜껑) scattered along urban alleys & streets!
            if (detailFade > 0.08) {
                vec2 mhCell = floor(p / 24.0);
                float mhHash = hash12(mhCell + vec2(17.3, 41.9));
                if (mhHash > 0.42) {
                    vec2 mhOff = vec2(hash12(mhCell + vec2(3.1, 7.7)), hash12(mhCell + vec2(9.4, 2.8))) * 14.0 + 5.0;
                    vec2 mhRel = (p - mhCell * 24.0) - mhOff;
                    float mhR = length(mhRel);
                    if (mhR < 0.46) {
                        float discMask = smoothstep(0.45, 0.41, mhR) * detailFade;
                        float outerRing = smoothstep(0.035, 0.012, abs(mhR - 0.40));
                        float innerRing = smoothstep(0.025, 0.010, abs(mhR - 0.24));
                        float treadPat  = 0.5 + 0.5 * sin(mhRel.x * 52.0) * sin(mhRel.y * 52.0);
                        vec3 ironCol = mix(vec3(0.042, 0.044, 0.048), vec3(0.095, 0.092, 0.088), treadPat * 0.55 + innerRing * 0.45);
                        ironCol = mix(ironCol, vec3(0.018, 0.019, 0.021), outerRing * 0.80);
                        albedo = mix(albedo, ironCol, discMask);
                        roughness = mix(roughness, mix(0.42, 0.26, treadPat), discMask);
                        metallic = mix(metallic, 0.72, discMask);
                    }
                }
            }
        }
        surfaceAO = pAO;
        N = perturbNormalGrad(N, dp1, dp2, dPdx * 0.26, dPdy * 0.26, pNxy, 0.82 * detailFade);

        float wetAmt = clamp(wetRoadFactor + nightFactor * 0.16, 0.0, 1.0);
        wetRoadSurface = wetAmt;
        if (wetAmt > 0.02) {
            vec2 warp = vec2(fbm2(p * 0.085 + vec2(3.1, 7.4)), fbm2(p * 0.085 + vec2(11.7, 2.3)));
            float pNoise = fbm2(p * 0.145 + warp * 1.45) * 0.72 + valueNoise(p * 0.52) * 0.28;
            // Only form mirror puddles when road is actually wet (wetRoadFactor > 0) — zero phantom spots on dry dusk asphalt!
            puddleMask = smoothstep(0.55 - wetRoadFactor * 0.17, 0.77 - wetRoadFactor * 0.10, pNoise) * wetRoadFactor;
            albedo *= (1.0 - wetRoadFactor * 0.24 - puddleMask * 0.22);
            roughness = mix(roughness, 0.22, wetRoadFactor * 0.48);
            roughness = mix(roughness, 0.04, puddleMask);
            if (ubo.localLightCount.y > 1.5 && puddleMask > 0.26 && wetRoadFactor > 0.12 && detailFade > 0.05) {
                vec2 ripN = vec2(0.0);
                vec2 uv1 = p * 1.73;
                vec2 c1 = floor(uv1);
                vec2 f1 = fract(uv1) - 0.5;
                vec2 j1 = (vec2(hash12(c1 + 3.1), hash12(c1 + 19.7)) - 0.5) * 0.64;
                vec2 d1 = f1 - j1;
                float s1 = hash12(c1 + 41.3);
                if (s1 > 0.42) {
                    float age1 = fract(animTime * 1.35 + s1 * 11.7);
                    float dist1 = length(d1);
                    float wave1 = sin((dist1 - age1 * 0.34) * 52.0) * exp(-abs(dist1 - age1 * 0.34) * 28.0);
                    float env1 = smoothstep(0.0, 0.08, age1) * (1.0 - age1) * (1.0 - smoothstep(0.18, 0.36, dist1));
                    ripN += normalize(d1 + vec2(1e-4)) * (wave1 * env1);
                }
                vec2 pRot = vec2(p.x * 0.7986 - p.y * 0.6018, p.x * 0.6018 + p.y * 0.7986);
                vec2 uv2 = pRot * 2.81 + vec2(13.4, 7.9);
                vec2 c2 = floor(uv2);
                vec2 f2 = fract(uv2) - 0.5;
                vec2 j2 = (vec2(hash12(c2 + 7.3), hash12(c2 + 29.1)) - 0.5) * 0.64;
                vec2 d2 = f2 - j2;
                float s2 = hash12(c2 + 67.9);
                if (s2 > 0.48) {
                    float age2 = fract(animTime * 1.65 + s2 * 13.1);
                    float dist2 = length(d2);
                    float wave2 = sin((dist2 - age2 * 0.32) * 56.0) * exp(-abs(dist2 - age2 * 0.32) * 32.0);
                    float env2 = smoothstep(0.0, 0.08, age2) * (1.0 - age2) * (1.0 - smoothstep(0.16, 0.34, dist2));
                    vec2 n2Rot = normalize(d2 + vec2(1e-4)) * (wave2 * env2 * 0.75);
                    ripN += vec2(n2Rot.x * 0.7986 + n2Rot.y * 0.6018, -n2Rot.x * 0.6018 + n2Rot.y * 0.7986);
                }
                float ripBlend = smoothstep(0.28, 0.72, puddleMask) * smoothstep(0.12, 0.45, wetRoadFactor) * detailFade;
                N = normalize(mix(N, normalize(vec3(ripN.x * 0.055, 1.0, ripN.y * 0.055)), ripBlend));
            }
        }
    }
    else if (matId == 25) {
        // MAT 25: Seoul Architectural Tempered Safety Glass (Bus Shelters, Subway Canopies, Street Kiosks!)
        // Real see-through low-iron blue-green tinted tempered glass with Fresnel sky reflections & ZERO skyscraper facade textures!
        if (dot(N, V) < 0.0) N = -N;
        float ndvG = max(dot(N, V), 0.001);
        float fresG = pow(1.0 - ndvG, 3.0);
        albedo = albedo * 0.34;
        roughness = 0.042;
        metallic = 0.14;
        // Preserve opaque white safety frit stripes (srgbBase.a > 0.92), keep tempered glass cleanly see-through!
        if (srgbBase.a < 0.92) {
            alphaOut = clamp(mix(srgbBase.a, 0.84, fresG * 0.85), 0.22, 0.86);
        }
    }

    // -----------------------------------------------------------------------
    // Physically Based Cook-Torrance GGX + Energy-Conserving Automotive Clearcoat
    // -----------------------------------------------------------------------
    float rawNdotL = dot(N, L);
    float ndotl = max(rawNdotL, 0.0);
    float shadow = (ndotl > 0.01) ? computeShadow(fragPosLightSpace, N, L) : 0.0;
    float sunVis = 1.0 - shadow * 0.90;

    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(groundAmbient * 0.86, skyAmbient * 1.02, hemi) * surfaceAO;
    ambient = mix(ambient, ambient * vec3(0.76, 0.88, 1.16), shadow * 0.48);
    float contactAo = mix(0.66, 1.0, clamp(fragPosition.y / 1.8, 0.0, 1.0));
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

    bool reflectiveMat = (ubo.localLightCount.y > 0.5) && (isWindowPane || matId == 3 || matId == 4 || matId == 6 || matId == 7 || matId == 8 || matId == 25 || puddleMask > 0.04 || wetRoadSurface > 0.15);
    if (reflectiveMat) {
        vec3 R = reflect(-V, N);
        bool isAuto = (matId == 7 || matId == 8);
        vec3 envRefl = sampleSeoulEnvMap(R, fragPosition, roughness, nightFactor, isAuto);

        if (matId == 8) {
            envRefl *= mix(1.0, 0.34, smoothstep(0.08, 0.72, R.y));
        } else if (matId == 7) {
            envRefl *= mix(1.0, 0.34, smoothstep(0.08, 0.56, R.y));
        }

        float specExp = (matId == 6) ? 210.0 : ((matId == 8 || matId == 25) ? 340.0 : ((matId == 7) ? mix(260.0, 32.0, clamp(roughness * 1.4, 0.0, 1.0)) : 200.0));
        float sunRefl = pow(max(dot(R, L), 0.0), specExp);
        envRefl += sunColor * sunRefl * sunVis * ((matId == 6) ? 2.6 : ((matId == 8 || matId == 25) ? 2.8 : ((matId == 7) ? 2.4 : 2.15)));

        float wetCoat = puddleMask * 0.86 + wetRoadSurface * 0.20;
        float coatStrength = (matId == 6) ? 0.88
                           : ((matId == 8) ? 0.64
                           : ((matId == 25) ? 0.76
                           : ((matId == 7) ? (0.86 * clearcoatMask)
                           : (isWindowPane ? (isGlassTowerWin ? 0.94 : 0.78)
                           : ((matId == 3) ? 0.72
                           : ((matId == 4) ? (0.36 * (1.0 - step(0.3, roughness))) : wetCoat))))));
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
            float coatFresnel = (matId == 8 || matId == 25)
                                    ? (0.10 + 0.86 * pow(1.0 - ndotv, 3.6))
                                    : (isWindowPane
                                           ? (0.20 + 0.78 * pow(1.0 - ndotv, 3.2))
                                           : (0.06 + 0.90 * pow(1.0 - ndotv, 4.2)));
            envLit += envRefl * (isWindowPane ? windowReflTint : ((matId == 25) ? vec3(0.76, 0.90, 0.96) : vec3(1.0))) * coatFresnel * coatStrength;
        }
    }

    // -----------------------------------------------------------------------
    // Dynamic Multi-Point & Spotlight Local Illumination (v6.7.0 Realistic LED
    // Headlight Ground Projection Cones + Wet Road Specular Glint!)
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
            ? max(max(rawNdl, 0.0), smoothstep(0.0, 0.28, rawNdl + 0.12) * 0.36 * step(-0.04, rawNdl))
            : max(rawNdl, 0.0);
        if (effNdl <= 0.0001) continue;

        float uDist = clamp(1.0 - distL / lRad, 0.0, 1.0);
        float atten = (uDist * uDist) / (1.0 + 0.024 * d2);

        if (isSpot) {
            vec3 spotFwd = ubo.lightDirCone[li].xyz;
            float cosOuter = ubo.lightDirCone[li].w;
            float cosTheta = dot(-lDir, spotFwd);
            // Wide crisp LED low-beam optical cutoff cone with bright mid-field road pool
            float cosInner = mix(cosOuter, 1.0, 0.62);
            float cone = smoothstep(cosOuter, cosInner, cosTheta);
            cone = cone * cone * (3.0 - 2.0 * cone);
            if (cone <= 0.001) continue;
            float nearRamp = smoothstep(0.55, 3.2, distL);
            float longFade = pow(uDist, 1.45);
            atten = 1.45 * longFade * nearRamp * cone / (1.0 + 0.024 * d2);
            // Korean low-beam headlights have sharp horizontal cutoff -> never blast bright discs onto bridge soffits overhead!
            if (N.y < -0.45) atten *= 0.12;
        } else {
            // Korean LED streetlamps are full-cutoff downward luminaires (never shine bright circles UP onto bridge undersides!)
            atten *= smoothstep(-0.42, 0.12, lDir.y);
            if (N.y < -0.45) atten *= 0.18;
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

        float wetGlint = (puddleMask * 1.05 + wetRoadSurface * 0.28) * pow(ndh_l, 44.0);
        localLit += (diff_l + spec_l * 0.48 + vec3(wetGlint)) * lCol * effNdl;
    }

    vec3 totalLinear = envLit + directLit + localLit + emissiveColor * emissive;

    // Real Seoul long-range atmospheric perspective scaled live by GraphicsSettings Render Distance & Fog Mode!
    float fogDensScale = ubo.localLightCount.w;
    vec3 fogged = totalLinear;
    if (fogDensScale > 0.001) {
        float dist = length(viewPos - fragPosition);
        float clearStart = (ubo.localLightCount.z > 10.0) ? ubo.localLightCount.z : 280.0;
        float fogStart = mix(clearStart, min(clearStart, 75.0), wetRoadFactor);
        float clearDens = 0.00034 * clamp(320.0 / max(clearStart, 100.0), 0.35, 1.85);
        float fogDens  = mix(clearDens, 0.0020, wetRoadFactor) * fogDensScale;
        float fogFactor = clamp(1.0 - exp(-max(0.0, dist - fogStart) * fogDens), 0.0, 0.82);
        vec3 linearFog = pow(ubo.fogColorAndTexel.xyz, vec3(2.2));
        fogged = mix(totalLinear, linearFog, fogFactor);
    }

    outColor = vec4(fogged, alphaOut);
}
